//
//	Update.cpp
//
//	自動アップデートの実装（流れは Update.h）。利用者に見せるものはすべて Vectorworks の
//	ネイティブダイアログ（gSDK->AlertInform / AlertQuestion）で、ネットワークと入れ替えの
//	実務は同梱スクリプトへ渡す。
//
//	プラットフォーム依存はこの 3 つだけ:
//	  * 自分のバイナリの場所（mac: dladdr / win: GetModuleHandleEx）
//	  * スクリプトの起動と標準出力の取り込み（popen / _popen）
//	  * 再起動ヘルパーの投げ方（nohup … & / CreateProcess）
//	文字列の処理と判断は UpdateParse.h（純粋）にある。
//
//	【出どころ】実プラグイン（vectorworks-plugin-import-ifc-homeskz の src/Updater.cpp）で
//	実機まで確かめた形を写している。とくに「再起動を自分でやらない」ことには理由があるので、
//	簡略化しないこと（UpdateParse.h「入れ替えたあとの再起動」）。
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "Update.h"
#include "UpdateParse.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

using namespace vwprobe::update;

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#	include <dlfcn.h>
#	include <mach-o/dyld.h>
#	include <unistd.h>
#endif

namespace
{
#if GS_MAC

	// 同梱スクリプトの名前（CMake がこの名前でバンドルの Contents/Resources へ置く）。
	constexpr const char* kScriptName = "vw-probes-update.sh";

	// 同梱スクリプトの絶対パス（見つからなければ空）。自分の binary は
	// <name>.vwlibrary/Contents/MacOS/<name> なので、末尾を Resources/<script> に替える。
	std::string BundledScriptPath()
	{
		Dl_info info{};
		if (::dladdr(reinterpret_cast<const void*>(&BundledScriptPath), &info) == 0 ||
			info.dli_fname == nullptr)
			return "";
		return MacScriptPathFromBinary(info.dli_fname, kScriptName);
	}

	// このビルドが**実際に読み込まれた** Plug-Ins フォルダ（既定パスの決め打ちではない）。
	std::string BundlePluginsDir()
	{
		Dl_info info{};
		if (::dladdr(reinterpret_cast<const void*>(&BundlePluginsDir), &info) == 0 ||
			info.dli_fname == nullptr)
			return "";
		return MacPluginsDirFromBinary(info.dli_fname);
	}

	// スクリプトを実行して標準出力を out に取り込む（終わるまで待つ）。
	bool RunBundledScript(const std::vector<std::string>& args, std::string& out)
	{
		const std::string script = BundledScriptPath();
		if (script.empty())
			return false;

		// 読み込み元のフォルダを教える（そこから読み、そこへ入れる）。
		std::string env;
		const std::string pluginsDir = BundlePluginsDir();
		if (!pluginsDir.empty())
			env = "VW_PLUGINS_DIR=" + ShellQuote(pluginsDir) + " ";

		std::string cmd = env + "/bin/bash " + ShellQuote(script);
		for (const std::string& a : args)
			cmd += " " + ShellQuote(a);
		cmd += " 2>/dev/null";

		// NOLINTNEXTLINE(cert-env33-c): 実行するのは自分が同梱したスクリプトだけ。
		FILE* pipe = ::popen(cmd.c_str(), "r");
		if (pipe == nullptr)
			return false;

		out.clear();
		std::array<char, 4096> buf{};
		size_t n = 0;
		while ((n = ::fread(buf.data(), 1, buf.size(), pipe)) > 0)
			out.append(buf.data(), n);
		::pclose(pipe);
		return true;
	}

	// 起動し直す対象の .app（ホストアプリ＝Vectorworks 本体のバンドル）。
	std::string HostAppPath()
	{
		std::uint32_t size = 1024;
		std::string buf(size, '\0');
		if (::_NSGetExecutablePath(buf.data(), &size) != 0)
		{
			buf.assign(size, '\0');
			if (::_NSGetExecutablePath(buf.data(), &size) != 0)
				return "";
		}
		buf.resize(std::strlen(buf.c_str()));
		return MacAppBundleFromExecutable(buf);
	}

	// ホストアプリの bundle id（終了要求の宛先。ローカライズされたアプリ名に依存しない）。
	std::string HostBundleId()
	{
		CFBundleRef mainBundle = ::CFBundleGetMainBundle();
		if (mainBundle == nullptr)
			return "";
		CFStringRef identifier = ::CFBundleGetIdentifier(mainBundle);
		if (identifier == nullptr)
			return "";
		std::array<char, 512> buf{};
		if (::CFStringGetCString(identifier, buf.data(), buf.size(), kCFStringEncodingUTF8) == 0)
			return "";
		return buf.data();
	}

	// ヘルパーを**切り離して**起動する（Vectorworks が終了した後も生き残る必要がある）。
	bool SpawnDetachedShell(const std::string& script)
	{
		const std::string cmd = "nohup /bin/sh -c " + ShellQuote(script) + " >/dev/null 2>&1 &";
		// NOLINTNEXTLINE(cert-env33-c): コマンドは自前で組み立てたもの（外部入力は含まない）。
		return std::system(cmd.c_str()) == 0;
	}

	std::string OwnProcessId()
	{
		return std::to_string(static_cast<long long>(::getpid()));
	}

	std::string RestartCommand()
	{
		const std::string app = HostAppPath();
		const std::string bundleId = HostBundleId();
		if (app.empty() || bundleId.empty())
			return "";
		return MacRelaunchCommand(OwnProcessId(), app, bundleId);
	}

#elif GS_WIN

	// 同梱スクリプトの名前（CMake がこの名前で .vlb の隣へ置く）。
	constexpr const char* kScriptName = "vw-probes-update.ps1";

	std::wstring Widen(const std::string& s)
	{
		if (s.empty())
			return L"";
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
		return w;
	}

	std::string Narrow(const std::wstring& w)
	{
		if (w.empty())
			return "";
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0,
											nullptr, nullptr);
		std::string s(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	// 自分（読み込まれている .vlb）のフルパス。
	std::string OwnModulePath()
	{
		HMODULE self = nullptr;
		if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
									 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								 reinterpret_cast<LPCWSTR>(&OwnModulePath), &self) == 0 ||
			self == nullptr)
			return "";

		std::wstring buf(MAX_PATH, L'\0');
		DWORD len = ::GetModuleFileNameW(self, buf.data(), (DWORD)buf.size());
		while (len == buf.size())
		{
			buf.resize(buf.size() * 2, L'\0');
			len = ::GetModuleFileNameW(self, buf.data(), (DWORD)buf.size());
		}
		if (len == 0)
			return "";
		buf.resize(len);
		return Narrow(buf);
	}

	// Windows では .vlb が Plug-Ins フォルダに直に置かれるので、そこがスクリプトの場所でも
	// 入れ先でもある。
	std::string OwnModuleDir()
	{
		return WinModuleDirFromPath(OwnModulePath());
	}

	std::string BundledScriptPath()
	{
		return WinScriptPathFromDir(OwnModuleDir(), kScriptName);
	}

	std::string BundlePluginsDir()
	{
		return OwnModuleDir();
	}

	bool RunBundledScript(const std::vector<std::string>& args, std::string& out)
	{
		const std::string script = BundledScriptPath();
		if (script.empty())
			return false;

		const std::string pluginsDir = BundlePluginsDir();
		if (!pluginsDir.empty())
			::SetEnvironmentVariableW(L"VW_PLUGINS_DIR", Widen(pluginsDir).c_str());

		std::string cmd = "powershell -NoProfile -ExecutionPolicy Bypass -File " + CmdQuote(script);
		for (const std::string& a : args)
			cmd += " " + CmdQuote(a);
		cmd += " 2>NUL";

		FILE* pipe = ::_popen(cmd.c_str(), "r");
		if (pipe == nullptr)
		{
			if (!pluginsDir.empty())
				::SetEnvironmentVariableW(L"VW_PLUGINS_DIR", nullptr);
			return false;
		}

		out.clear();
		std::array<char, 4096> buf{};
		size_t n = 0;
		while ((n = ::fread(buf.data(), 1, buf.size(), pipe)) > 0)
			out.append(buf.data(), n);
		::_pclose(pipe);

		if (!pluginsDir.empty())
			::SetEnvironmentVariableW(L"VW_PLUGINS_DIR", nullptr);
		return true;
	}

	// ホストの実行ファイル（＝Vectorworks 本体）。
	std::string HostAppPath()
	{
		std::wstring buf(MAX_PATH, L'\0');
		DWORD len = ::GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
		while (len == buf.size())
		{
			buf.resize(buf.size() * 2, L'\0');
			len = ::GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
		}
		if (len == 0)
			return "";
		buf.resize(len);
		return Narrow(buf);
	}

	// ヘルパーを切り離して起動する（ウィンドウ無し・パイプ無し・待たない）。
	bool SpawnDetachedShell(const std::string& script)
	{
		const std::string cmd =
			"powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command " +
			CmdQuote(script);

		std::wstring wcmd = Widen(cmd);
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};
		if (::CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
							 nullptr, nullptr, &si, &pi) == 0)
			return false;

		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
		return true;
	}

	std::string OwnProcessId()
	{
		return std::to_string(static_cast<long long>(::GetCurrentProcessId()));
	}

	std::string RestartCommand()
	{
		const std::string exe = HostAppPath();
		if (exe.empty())
			return "";
		return WinRelaunchCommand(OwnProcessId(), exe);
	}

#endif // GS_WIN
} // namespace

namespace vwprobe
{
	namespace
	{
		// モーダルの通知（false = 最小アラートではなくダイアログ。advice 行も出る）。
		void Inform(const std::string& text, const std::string& advice)
		{
			gSDK->AlertInform(text.c_str(), advice.c_str(), false);
		}

		// はい／いいえ。肯定側を選んだら true。
		bool Ask(const std::string& text, const std::string& advice, const std::string& okText,
				 const std::string& cancelText)
		{
			// 戻り値 0 = 否定／キャンセル、1 = 肯定。defaultButton 1 = 肯定側が既定。
			const short r =
				gSDK->AlertQuestion(text.c_str(), advice.c_str(),
									/*defaultButton*/ 1, okText.c_str(), cancelText.c_str(),
									/*customButtonA*/ "", /*customButtonB*/ "");
			return r == 1;
		}

		// 同梱スクリプトで入れ替える。失敗したら errorOut に理由を入れて false。
		bool Install(const std::string& url, std::string& errorOut)
		{
			std::string out;
			if (!RunBundledScript({"do-install", url}, out))
			{
				errorOut = "アップデータを起動できませんでした。";
				return false;
			}
			if (InstallReportedOk(out))
				return true;
			errorOut = InstallErrorText(out, "インストールに失敗しました。");
			return false;
		}

		// 入れ替えが済んだあとの締め。**コンパイル済みプラグインは次の起動でしか
		// 読み込まれない**ので、単なる通知ではなく「再起動しますか？」の問いにする。
		void OfferRestart(const std::string& text, const std::string& detail)
		{
			std::string advice = detail;
			if (!advice.empty())
				advice += "\n\n";
			// 終了要求は外から届き、起動が終わってから効く。そう言っておかないと、
			// 押した直後は何も起きていないように見える。
			advice += "反映するには Vectorworks の再起動が必要です。\n"
					  "今すぐ再起動しますか？（起動の完了後に終了し、自動で起動し直します。\n"
					  "開いているファイルは保存を確認します）";

			if (!Ask(text, advice, "再起動", "後で"))
				return;

			// 再起動を**組み立てられなかった**場合（起動し直す対象が分からない・ヘルパーを
			// 起動できない）。入れ替え自体は済んでいるので失うものは無いが、黙っていると
			// 「再起動が効かなかった」と区別が付かない。
			const std::string command = RestartCommand();
			if (command.empty() || !SpawnDetachedShell(command))
				Inform("再起動できませんでした。",
					   "お手数ですが、手動で Vectorworks を再起動してください。\n"
					   "（入れ替え自体は完了しているので、次回の起動で反映されます）");
		}

		// 入れ替えるビルドの中身を 1 行ずつに畳む（ダイアログの advice 行）。
		std::string DetailLines(const Status& status)
		{
			std::string detail =
				"インストール済み: " +
				(status.installed.empty() ? std::string("不明") : status.installed) +
				"\n最新: " + status.latest;
			if (!status.title.empty())
				detail += "\n" + status.title;
			if (!status.probes.empty())
				detail += "\n入っているプローブ: " + status.probes;
			return detail;
		}

		// チェック本体。interactive = メニューから押されたとき（＝最新でも失敗でも必ず
		// 何か見せる）。起動時は逆に、**最新なら無言**で通り過ぎる。
		void CheckAndOffer(bool interactive)
		{
			std::string out;
			if (!RunBundledScript({"q"}, out))
			{
				if (interactive)
					Inform("アップデータを起動できませんでした。",
						   "同梱スクリプトが見つかりません。zip を展開したときの構成のまま "
						   "Plug-Ins フォルダへ置かれているか確認してください。");
				return;
			}

			const Status status = Evaluate(out);
			if (!status.error.empty())
			{
				if (interactive)
					Inform("更新を確認できませんでした。", status.error);
				return;
			}
			if (!status.offerUpdate)
			{
				if (interactive)
					Inform("最新のビルドです。", DetailLines(status));
				return;
			}

			if (!Ask("新しいプローブビルドがあります。入れ替えますか？", DetailLines(status),
					 "入れ替える", "後で"))
				return;

			std::string err;
			if (Install(status.url, err))
				OfferRestart("プローブビルドを入れ替えました。", "build: " + status.latest);
			else
				Inform("入れ替えに失敗しました。", err);
		}
	} // namespace

	void RunStartupUpdateCheck()
	{
		// plugin_module_main は 1 セッションに複数回呼ばれうる。チェックは 1 度だけ。
		static bool sDone = false;
		if (sDone)
			return;
		sDone = true;

		// 例外を SDK コールバックの外へ漏らさない（起動を巻き込んで落とさない）。
		// NOLINTBEGIN(bugprone-empty-catch): 起動中は報告先が無く、アップデートは
		// 付随機能。黙って諦めるのが**ここでは正しい**（オフラインのときと同じ扱い）。
		try
		{
			CheckAndOffer(/*interactive*/ false);
		}
		catch (...)
		{
		}
		// NOLINTEND(bugprone-empty-catch)
	}

	void RunManualUpdateCheck()
	{
		try
		{
			CheckAndOffer(/*interactive*/ true);
		}
		catch (const std::exception& error)
		{
			Inform("更新の確認中にエラーが起きました。",
				   (error.what() != nullptr) ? error.what() : "");
		}
		catch (...)
		{
			Inform("更新の確認中にエラーが起きました。", "");
		}
	}
} // namespace vwprobe
