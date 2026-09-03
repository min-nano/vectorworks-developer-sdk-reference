//
//	UpdateParse.h
//
//	自動アップデートで使う**純粋な**ヘルパー（SDK にもプラットフォームにも依存しない
//	文字列処理）。Update.cpp から切り出してあるのは、SDK 呼び出しの合間に文字列の組み立てや
//	パスの切り貼りが混ざると、どちらの誤りも見つけにくくなるため。
//
//	【出どころ】この中身は実プラグイン
//	（vectorworks-plugin-import-ifc-homeskz の src/UpdaterParse.h）で**実機まで含めて
//	確かめられたもの**を写している。とくに再起動のコマンド（下記 MacRelaunchCommand /
//	WinRelaunchCommand）は「なぜこの形でなければ駄目か」を実機で何度も踏んで確定した部分
//	なので、思いつきで簡略化しない。2 つのリポジトリはビルドを共有しないので、写しである
//	ことを承知のうえで**別々の実体**として持つ（あちらを直したらこちらも見る）。
//

#pragma once

#include <string>

namespace vwprobe::update
{
	// -----------------------------------------------------------------------
	// スクリプト出力（"key=value" 行）のパース。
	// -----------------------------------------------------------------------

	// 前後の空白を落とす（全部空白なら空文字）。
	inline std::string Trim(const std::string& s)
	{
		const std::string::size_type b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos)
			return "";
		const std::string::size_type e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	// 最初に見つかった "key=value" 行の値（無ければ空文字）。
	inline std::string ValueOf(const std::string& out, const std::string& key)
	{
		const std::string needle = key + "=";
		std::string::size_type pos = 0;
		while (pos < out.size())
		{
			const std::string::size_type eol = out.find('\n', pos);
			const std::string line =
				out.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
			if (line.starts_with(needle))
				return Trim(line.substr(needle.size()));
			if (eol == std::string::npos)
				break;
			pos = eol + 1;
		}
		return "";
	}

	// -----------------------------------------------------------------------
	// コマンドラインのクォート。
	// -----------------------------------------------------------------------

	// /bin/sh の 1 語として安全になるようシングルクォートで包む（中の ' も逃がす）。
	inline std::string ShellQuote(const std::string& s)
	{
		std::string out = "'";
		for (const char c : s)
		{
			if (c == '\'')
				out += "'\\''";
			else
				out += c;
		}
		out += "'";
		return out;
	}

	// cmd.exe / PowerShell 向けにダブルクォートで包む。渡すのはリリース資産の URL と
	// 固定の名前だけで、クォートを含むことはない——万一含まれていたら、クォートを壊す
	// くらいなら落とす。
	inline std::string CmdQuote(const std::string& s)
	{
		std::string out = "\"";
		for (const char c : s)
			if (c != '"')
				out += c;
		out += "\"";
		return out;
	}

	// PowerShell のシングルクォート文字列（中の ' は '' に倍化する、が PowerShell の作法）。
	// -Command へ渡すスクリプト全体をダブルクォート 1 組で包めるようにするために使う。
	inline std::string PowerShellQuote(const std::string& s)
	{
		std::string out = "'";
		for (const char c : s)
		{
			if (c == '\'')
				out += "''";
			else
				out += c;
		}
		out += "'";
		return out;
	}

	// -----------------------------------------------------------------------
	// 自分のバイナリの位置から、同梱スクリプトと Plug-Ins フォルダを割り出す。
	// -----------------------------------------------------------------------

	// macOS: .../<name>.vwlibrary/Contents/MacOS/<name>
	//     → .../<name>.vwlibrary/Contents/Resources/<script>
	inline std::string MacScriptPathFromBinary(const std::string& binaryPath,
											   const std::string& scriptName)
	{
		const std::string marker = "/Contents/MacOS/";
		const std::string::size_type at = binaryPath.rfind(marker);
		if (at == std::string::npos)
			return "";
		const std::string contents = binaryPath.substr(0, at + std::string("/Contents/").size());
		return contents + "Resources/" + scriptName;
	}

	// macOS: .../<PlugIns>/<name>.vwlibrary/Contents/MacOS/<name> → .../<PlugIns>
	//
	// **読み込まれたバンドルの隣へ入れる**のが肝。利用者のユーザフォルダは既定の場所とは
	// 限らないので、既定パスを決め打ちすると「入れたのに反映されない」が起きる。
	inline std::string MacPluginsDirFromBinary(const std::string& binaryPath)
	{
		const std::string::size_type at = binaryPath.rfind("/Contents/MacOS/");
		if (at == std::string::npos)
			return "";
		const std::string bundle = binaryPath.substr(0, at);
		const std::string::size_type slash = bundle.rfind('/');
		if (slash == std::string::npos)
			return "";
		return bundle.substr(0, slash);
	}

	// macOS: ホストアプリの実行ファイル
	//   /Applications/Vectorworks 2026/Vectorworks.app/Contents/MacOS/Vectorworks
	// → 起動し直す対象のバンドル /Applications/Vectorworks 2026/Vectorworks.app
	//
	// `open -a` へ渡すのは**バンドル**（LaunchServices 経由＝ダブルクリックと同じ）。
	// 中の実行ファイルを直接起動すると、Vectorworks はサポートファイルを見つけられない。
	inline std::string MacAppBundleFromExecutable(const std::string& exePath)
	{
		const std::string marker = ".app/Contents/MacOS/";
		const std::string::size_type at = exePath.rfind(marker);
		if (at == std::string::npos)
			return "";
		return exePath.substr(0, at + std::string(".app").size());
	}

	// Windows: モジュールのあるディレクトリ（＝Plug-Ins フォルダ。.vlb は直に置かれる）。
	inline std::string WinModuleDirFromPath(const std::string& modulePath)
	{
		const std::string::size_type slash = modulePath.find_last_of("\\/");
		if (slash == std::string::npos)
			return "";
		return modulePath.substr(0, slash);
	}

	// Windows: 同梱スクリプトはモジュールの隣。
	inline std::string WinScriptPathFromDir(const std::string& moduleDir,
											const std::string& scriptName)
	{
		if (moduleDir.empty())
			return "";
		return moduleDir + "\\" + scriptName;
	}

	// -----------------------------------------------------------------------
	// 入れ替えたあとの再起動。
	//
	// **終了も起動も、この プロセスからは行わない。** 理由は実機で 2 つとも踏んである
	// （実プラグイン src/UpdaterParse.h の同じ節）:
	//
	//  * 起動し直しは Vectorworks の中からはできない。古いプロセスが消えてからでないと、
	//    新しいインスタンスがサポートファイルを読めずに落ちる。
	//  * 終了を SDK に頼むのも駄目。このチェックはプラグインの**読み込み中**（スプラッシュ
	//    が出たまま）に走るので、その時点の Vectorworks は自分を畳める状態にない
	//    （「サポートファイルの読み込みに失敗しました」になる）。
	//
	// そこで、外に出した小さなヘルパーから **OS の通常の終了要求**（⌘Q と同じもの。mac は
	// bundle id 宛の 'quit' Apple event、Windows は WM_CLOSE）を投げる。イベントループが
	// 動き出してから届くので、保存の確認も普通に出る。
	// -----------------------------------------------------------------------

	// Vectorworks の終了を待つ上限（秒）。保存の確認をキャンセルすれば起動したままになる
	// ので、作業中のものを勝手に起動し直さないための歯止め。
	inline constexpr int kRelaunchWaitSeconds = 300;
	// 終了要求を 1 度だけ投げ直すまでの秒数（1 度目が起動処理に紛れて落ちた場合の保険）。
	inline constexpr int kRelaunchRetrySeconds = 15;
	// 古いプロセスの後始末が落ち着くのを待つ秒数。
	inline constexpr int kRelaunchSettleSeconds = 2;

	// macOS: 終了を頼み → pid の消滅を待ち → アプリを開き直す /bin/sh の 1 行。
	inline std::string MacRelaunchCommand(const std::string& pid, const std::string& appPath,
										  const std::string& bundleId,
										  int waitSeconds = kRelaunchWaitSeconds,
										  int retrySeconds = kRelaunchRetrySeconds,
										  int settleSeconds = kRelaunchSettleSeconds)
	{
		// AppleScript はシングルクォート 1 語の中に置き、bundle id をダブルクォートで
		// 囲む。id にクォートが入ることは無いが、入っていたら落とす（クォートを壊さない）。
		std::string safeId;
		for (const char c : bundleId)
			if (c != '"' && c != '\'')
				safeId += c;

		return "q() { osascript -e 'tell application id \"" + safeId +
			   "\" to quit' >/dev/null 2>&1; }; q; i=0; while kill -0 " + pid +
			   " 2>/dev/null; do [ \"$i\" -eq " + std::to_string(retrySeconds) +
			   " ] && q; [ \"$i\" -ge " + std::to_string(waitSeconds) +
			   " ] && exit 0; sleep 1; i=$((i+1)); done; sleep " + std::to_string(settleSeconds) +
			   "; exec open -a " + ShellQuote(appPath);
	}

	// Windows: 同じ形の powershell -Command スクリプト。CloseMainWindow は WM_CLOSE を
	// 投げる＝閉じるボタンと同じ要求なので、Vectorworks は普段どおりに畳まれる。
	inline std::string WinRelaunchCommand(const std::string& pid, const std::string& exePath,
										  int waitSeconds = kRelaunchWaitSeconds,
										  int retrySeconds = kRelaunchRetrySeconds,
										  int settleSeconds = kRelaunchSettleSeconds)
	{
		const int restSeconds = waitSeconds > retrySeconds ? waitSeconds - retrySeconds : 0;
		const std::string close =
			"$p = Get-Process -Id " + pid + "; if ($p) { $null = $p.CloseMainWindow() }; ";

		return "$ErrorActionPreference='SilentlyContinue'; " + close + "Wait-Process -Id " + pid +
			   " -Timeout " + std::to_string(retrySeconds) + "; " + close + "Wait-Process -Id " +
			   pid + " -Timeout " + std::to_string(restSeconds) + "; if (Get-Process -Id " + pid +
			   ") { exit }; Start-Sleep -Seconds " + std::to_string(settleSeconds) +
			   "; Start-Process -FilePath " + PowerShellQuote(exePath);
	}

	// -----------------------------------------------------------------------
	// 判断（スクリプトの出力を受けて「入れ替えを勧めるか」を決める）。
	// -----------------------------------------------------------------------

	// `q` の出力を読んだ結果。
	struct Status
	{
		bool offerUpdate = false; // 別のビルドが公開されている → 勧める
		std::string installed;	  // 入っているビルド ID（"none" / 空 = 不明）
		std::string latest;		  // 公開されているビルド ID
		std::string url;		  // 資産のダウンロード URL
		std::string title;		  // リリースの名前（ダイアログに出す）
		std::string probes;		  // 入っているプローブの一覧（同上）
		std::string error;		  // スクリプトが返した理由（あれば）
	};

	// **比べるのはコミットではなくビルド ID。** このプラグインは同じ main の sha から、
	// 同居させる PR を変えて何度もビルドされるので、コミットで比べると「中身は別物なのに
	// 最新扱い」になって取りこぼす。ビルド ID はワークフローの run id で、ビルドのたびに
	// 必ず変わる（plugin/CMakeLists.txt の VW_BUILD_ID）。
	inline Status Evaluate(const std::string& out)
	{
		Status s;
		s.error = ValueOf(out, "error");
		if (!s.error.empty())
			return s; // オフライン等 → 黙って何もしない
		s.installed = ValueOf(out, "installed");
		s.latest = ValueOf(out, "latest");
		s.url = ValueOf(out, "url");
		s.title = ValueOf(out, "title");
		s.probes = ValueOf(out, "probes");
		if (s.latest.empty() || s.url.empty())
		{
			s.error = "リリースの情報が不完全です。";
			return s;
		}
		if (s.installed == s.latest)
			return s; // 同じビルド → 何もしない
		s.offerUpdate = true;
		return s;
	}

	// `do-install` が成功したか（成功の印は "ok" ただ 1 つ）。
	inline bool InstallReportedOk(const std::string& out)
	{
		return Trim(out) == "ok";
	}

	// 失敗したときに見せる文言（スクリプトの error= があればそれ、無ければ既定）。
	inline std::string InstallErrorText(const std::string& out, const std::string& fallback)
	{
		const std::string e = ValueOf(out, "error");
		return e.empty() ? fallback : e;
	}
} // namespace vwprobe::update
