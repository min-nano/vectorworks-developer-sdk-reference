//
//	UpdateParseTests.cpp
//
//	自動アップデートの**純粋な**部分（plugin/src/UpdateParse.h）の単体テスト。
//
//	【なぜこれだけテストがあるか】このリポジトリのプラグインは、実機でしか確かめられない
//	ものが大半で、CI が確かめられるのは「コンパイルとリンクが通る」ことまで。ただし
//	UpdateParse.h は SDK にもプラットフォームにも依存しない文字列処理なので、**普通の
//	コンパイラだけで動かして確かめられる**——しかも壊れても静かに壊れる（更新が来ない・
//	常に来る・再起動のコマンドが空振りする）ので、気付ける仕掛けを置く価値がある。
//
//	走らせ方（CI の lint ワークフローも同じ）:
//	    g++ -std=c++20 -Wall -Wextra -Werror -I plugin/src
//	        plugin/tests/UpdateParseTests.cpp -o /tmp/t && /tmp/t
//

#include "UpdateParse.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace vwprobe::update;

namespace
{
	int gFailures = 0;

	void check(bool ok, const char* what)
	{
		if (!ok)
		{
			std::printf("FAIL: %s\n", what);
			++gFailures;
		}
	}

	void checkEq(const std::string& actual, const std::string& expected, const char* what)
	{
		if (actual != expected)
		{
			std::printf("FAIL: %s\n  期待: [%s]\n  実際: [%s]\n", what, expected.c_str(),
						actual.c_str());
			++gFailures;
		}
	}
} // namespace

int main()
{
	// --- q の出力を読む -----------------------------------------------------
	{
		const std::string out = "installed=33700000000\n"
								"latest=33701234567\n"
								"url=https://example/VwSdkProbes.vwlibrary.zip\n"
								"title=Probes (0c6b38c + PR 12)\n"
								"probes=example, tag-formula(#12)\n";
		const Status s = Evaluate(out);
		check(s.offerUpdate, "別のビルドが公開されていれば勧める");
		checkEq(s.installed, "33700000000", "installed");
		checkEq(s.latest, "33701234567", "latest");
		checkEq(s.title, "Probes (0c6b38c + PR 12)", "title");
		checkEq(s.probes, "example, tag-formula(#12)", "probes");
	}

	// 同じビルド ID なら何もしない（**ここがコミット比較との違い**。同じ sha でも
	// 同居させる PR が変われば ID が変わるので、ちゃんと更新が来る）。
	check(!Evaluate("installed=1\nlatest=1\nurl=u\n").offerUpdate, "同じビルドなら勧めない");

	// まだ入っていない（none）なら勧める。
	check(Evaluate("installed=none\nlatest=1\nurl=u\n").offerUpdate, "未インストールなら勧める");

	// スクリプトが理由を返したとき（オフライン等）は勧めず、理由を持ち帰る。
	{
		const Status s = Evaluate("error=オフラインです。\n");
		check(!s.offerUpdate, "エラーなら勧めない");
		checkEq(s.error, "オフラインです。", "error をそのまま持ち帰る");
	}

	// 情報が欠けているとき（url が無い）は勧めず、理由を作る。
	{
		const Status s = Evaluate("installed=1\nlatest=2\n");
		check(!s.offerUpdate, "不完全なら勧めない");
		check(!s.error.empty(), "不完全なら理由が付く");
	}

	// --- 「再起動が要るか」の判断 -------------------------------------------
	// **本体（.vwpayload）だけが新しいなら再起動は要らない。** ここを取り違えると、
	// 再起動を促さずに殻の古い版で動かす（＝境界が食い違ってプローブが 1 つも動かない）
	// か、要らない再起動を毎回させることになる。どちらも静かに壊れる。
	{
		const std::string out = "installed=1\nlatest=2\n"
								"installedShell=aaa\nlatestShell=aaa\n"
								"url=u\n";
		const Status s = Evaluate(out);
		check(s.offerUpdate, "本体が新しければ勧める");
		check(s.payloadOnly, "殻が同じなら本体だけで済む（再起動なし）");
		checkEq(s.url, "u", "落とすのは殻ごとの zip 1 つだけ（中から本体だけを取り出す）");
		checkEq(s.installedShell, "aaa", "installedShell");
	}
	{
		// 殻まで変わった → まるごと入れ替え（再起動が要る）。
		const Status s =
			Evaluate("installed=1\nlatest=2\ninstalledShell=aaa\nlatestShell=bbb\nurl=u\n");
		check(s.offerUpdate, "殻が変わっていれば勧める");
		check(!s.payloadOnly, "殻が違うならまるごと入れ替える");
	}
	{
		// 本体は同じだが殻だけ変わった（プラグインの作りを直したとき）。
		const Status s =
			Evaluate("installed=1\nlatest=1\ninstalledShell=aaa\nlatestShell=bbb\nurl=u\n");
		check(s.offerUpdate, "殻だけ変わっていても勧める");
		check(!s.payloadOnly, "殻が違うならまるごと入れ替える");
	}
	{
		// 中身も殻も同じ → 何もしない。
		const Status s =
			Evaluate("installed=1\nlatest=1\ninstalledShell=a\nlatestShell=a\nurl=u\n");
		check(!s.offerUpdate, "どちらも同じなら勧めない");
	}
	// **判断できないときは必ず「まるごと」へ倒す。**
	check(!Evaluate("installed=1\nlatest=2\ninstalledShell=a\nurl=u\n").payloadOnly,
		  "公開側が殻の ID を載せていなければ、まるごと入れ替える");
	check(!Evaluate("installed=1\nlatest=2\ninstalledShell=none\nlatestShell=none\nurl=u\n")
			   .payloadOnly,
		  "殻の ID が分からなければ（none）、まるごと入れ替える");

	// --- do-install の判定 --------------------------------------------------
	check(InstallReportedOk("ok\n"), "ok は成功");
	check(!InstallReportedOk("error=だめ\n"), "error= は成功ではない");
	checkEq(InstallErrorText("error=だめ\n", "既定"), "だめ", "スクリプトの理由を優先");
	checkEq(InstallErrorText("", "既定"), "既定", "理由が無ければ既定の文言");

	// --- 自分の位置からパスを割り出す ---------------------------------------
	checkEq(MacScriptPathFromBinary("/P/VwSdkProbes.vwlibrary/Contents/MacOS/VwSdkProbes",
									"vw-probes-update.sh"),
			"/P/VwSdkProbes.vwlibrary/Contents/Resources/vw-probes-update.sh",
			"mac: 同梱スクリプト");
	checkEq(MacPluginsDirFromBinary("/P/VwSdkProbes.vwlibrary/Contents/MacOS/VwSdkProbes"), "/P",
			"mac: 読み込み元の Plug-Ins フォルダ");
	checkEq(MacAppBundleFromExecutable(
				"/A/Vectorworks 2026/Vectorworks.app/Contents/MacOS/Vectorworks"),
			"/A/Vectorworks 2026/Vectorworks.app", "mac: 起動し直す .app");
	checkEq(MacScriptPathFromBinary("/どこでもない", "x"), "", "形が違えば空を返す");
	checkEq(WinModuleDirFromPath("C:\\P\\VwSdkProbes.vlb"), "C:\\P", "win: モジュールのフォルダ");
	checkEq(WinScriptPathFromDir("C:\\P", "vw-probes-update.ps1"), "C:\\P\\vw-probes-update.ps1",
			"win: 同梱スクリプト");

	// --- クォート -----------------------------------------------------------
	checkEq(ShellQuote("a'b"), "'a'\\''b'", "sh: シングルクォートを逃がす");
	checkEq(PowerShellQuote("a'b"), "'a''b'", "PowerShell: シングルクォートを倍にする");
	checkEq(CmdQuote("a\"b"), "\"ab\"", "cmd: ダブルクォートは落とす");

	// --- 再起動のコマンド ---------------------------------------------------
	// 形が変わると**実機でしか気付けない**（押しても何も起きない）ので、要点を固定する。
	{
		const std::string mac = MacRelaunchCommand("123", "/A/V.app", "com.x.y");
		check(mac.find("tell application id \"com.x.y\" to quit") != std::string::npos,
			  "mac: bundle id 宛に quit を送る");
		check(mac.find("kill -0 123") != std::string::npos, "mac: pid の消滅を待つ");
		check(mac.find("exec open -a '/A/V.app'") != std::string::npos,
			  "mac: バンドルを open で開き直す");
		// bundle id にクォートが混じっても AppleScript を壊さない。
		check(MacRelaunchCommand("1", "/A", "a\"b'c").find("\"abc\"") != std::string::npos,
			  "mac: bundle id のクォートは落とす");

		const std::string win = WinRelaunchCommand("123", "C:\\V.exe");
		check(win.find("CloseMainWindow()") != std::string::npos, "win: WM_CLOSE を投げる");
		check(win.find("Wait-Process -Id 123") != std::string::npos, "win: pid の消滅を待つ");
		check(win.find("Start-Process -FilePath 'C:\\V.exe'") != std::string::npos,
			  "win: 実行ファイルを開き直す");
	}

	if (gFailures > 0)
	{
		std::printf("\n%d 件失敗しました。\n", gFailures);
		return EXIT_FAILURE;
	}
	std::printf("UpdateParse: すべて通りました。\n");
	return EXIT_SUCCESS;
}
