//
//	BundledScript.cpp
//
//	BundledScript.h の実装。**プラットフォーム依存はここに閉じる**:
//	  * 自分のバイナリの場所（mac: dladdr / win: GetModuleHandleExW）
//	  * スクリプトの起動と標準出力の取り込み（mac: popen / win: CreateProcessW）
//	パスの組み立てそのものは UpdateParse.h（純粋）にあり、単体テストが押さえている。
//
//	【出どころ】もとは Update.cpp の中にあった。結果の自動投稿（Feedback.cpp）でも
//	同じ道が要るので、**2 つ目の呼び出し元ができた時点で**切り出した——同じ popen の
//	作法を 2 か所に書くと、片方だけ直す事故が必ず起きる。
//
//	【Windows で _popen を使わない理由】_popen は `cmd.exe /c …` を立てる。Vectorworks は
//	コンソールを持たない GUI アプリなので、そのたびに**新しいコンソール（黒い窓）が開き**、
//	入れ替えやプローブの結果投稿のあいだ前面に居座っていた。CreateProcessW に
//	CREATE_NO_WINDOW を付けて直接 powershell を起動し、標準出力だけを自前のパイプで
//	受ける（標準入力とエラーは NUL）。パイプの書き込み口は PROC_THREAD_ATTRIBUTE_HANDLE_LIST
//	で**子へ渡すハンドルを限定**して継承させる——Vectorworks が開いている継承可能な
//	ハンドルまで子へ漏らさないため。ついでにコマンドラインが UTF-16 で渡るので、
//	ANSI コードページに無い文字（日本語のユーザ名を含むパスなど）も壊れない。
//

#include "PluginPrefix.h"
#include "BundledScript.h"
#include "UpdateParse.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace vwprobe::update;

#if defined(_WIN32)
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif

namespace vwprobe
{
	namespace
	{
#if !defined(_WIN32)
		// 自分（読み込まれている殻）のバイナリのパス。**このファイルの中の関数の
		// 番地**から引く（モジュールが分かればよいので、どの関数でもよい）。
		std::string OwnBinaryPath()
		{
			Dl_info info{};
			if (::dladdr(reinterpret_cast<const void*>(&OwnBinaryPath), &info) == 0 ||
				info.dli_fname == nullptr)
				return "";
			return info.dli_fname;
		}
#else
		std::wstring Widen(const std::string& s)
		{
			if (s.empty())
				return L"";
			const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
			std::wstring w((size_t)n, L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
			return w;
		}

		std::string Narrow(const std::wstring& w)
		{
			if (w.empty())
				return "";
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0,
												nullptr, nullptr);
			std::string s((size_t)n, '\0');
			::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr,
								  nullptr);
			return s;
		}

		// 自分（読み込まれている .vlb）のフルパス。
		std::string OwnBinaryPath()
		{
			HMODULE self = nullptr;
			if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
										 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
									 reinterpret_cast<LPCWSTR>(&OwnBinaryPath), &self) == 0 ||
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

		// コマンドを**ウィンドウ無しで**走らせ、標準出力を out に取り込む（終わるまで待つ）。
		// 標準入力とエラーは NUL。起動できなければ false。理由は冒頭の【Windows で _popen を
		// 使わない理由】。
		bool RunHidden(std::wstring cmdLine, std::string& out)
		{
			SECURITY_ATTRIBUTES sa{};
			sa.nLength = sizeof(sa);
			sa.bInheritHandle = TRUE;

			HANDLE readEnd = nullptr;
			HANDLE writeEnd = nullptr;
			if (::CreatePipe(&readEnd, &writeEnd, &sa, 0) == 0)
				return false;
			// 読み口は親だけが持つ（子へ渡すと、子が終わってもパイプが閉じない）。
			::SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

			HANDLE nul =
				::CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
							  FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
			if (nul == INVALID_HANDLE_VALUE)
			{
				::CloseHandle(readEnd);
				::CloseHandle(writeEnd);
				return false;
			}

			// 子へ渡すハンドルをこの 2 つに限る（属性リストが作れない古い環境では、限定せずに
			// 継承させる——出力が受け取れないよりはよい）。
			HANDLE inherit[2] = {writeEnd, nul};
			SIZE_T attrSize = 0;
			::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
			std::vector<unsigned char> attrBuf(attrSize);
			auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
			bool attrsReady = ::InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) != 0;
			if (attrsReady &&
				::UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
											sizeof(inherit), nullptr, nullptr) == 0)
			{
				::DeleteProcThreadAttributeList(attrs);
				attrsReady = false;
			}

			STARTUPINFOEXW si{};
			si.StartupInfo.cb = attrsReady ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW);
			si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			si.StartupInfo.wShowWindow = SW_HIDE;
			si.StartupInfo.hStdInput = nul;
			si.StartupInfo.hStdOutput = writeEnd;
			si.StartupInfo.hStdError = nul;
			si.lpAttributeList = attrsReady ? attrs : nullptr;

			PROCESS_INFORMATION pi{};
			const DWORD flags = CREATE_NO_WINDOW | (attrsReady ? EXTENDED_STARTUPINFO_PRESENT : 0);
			const BOOL started = ::CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
												  flags, nullptr, nullptr, &si.StartupInfo, &pi);

			if (attrsReady)
				::DeleteProcThreadAttributeList(attrs);
			// 書き口は子に渡したので親の分は閉じる（閉じないと、子が終わっても ReadFile が
			// EOF を返さない）。
			::CloseHandle(writeEnd);
			::CloseHandle(nul);
			if (started == 0)
			{
				::CloseHandle(readEnd);
				return false;
			}
			::CloseHandle(pi.hThread);

			out.clear();
			std::array<char, 4096> buf{};
			DWORD n = 0;
			while (::ReadFile(readEnd, buf.data(), (DWORD)buf.size(), &n, nullptr) != 0 && n > 0)
				out.append(buf.data(), n);

			::WaitForSingleObject(pi.hProcess, INFINITE);
			::CloseHandle(pi.hProcess);
			::CloseHandle(readEnd);
			return true;
		}
#endif
	} // namespace

	std::string BundledScriptPath(const std::string& baseName)
	{
		const std::string self = OwnBinaryPath();
		if (self.empty())
			return "";
#if defined(_WIN32)
		return WinScriptPathFromDir(WinModuleDirFromPath(self), baseName + ".ps1");
#else
		return MacScriptPathFromBinary(self, baseName + ".sh");
#endif
	}

	bool BundledScriptExists(const std::string& baseName)
	{
		const std::string path = BundledScriptPath(baseName);
		if (path.empty())
			return false;
		std::error_code ec;
		return std::filesystem::exists(std::filesystem::path(path), ec) && !ec;
	}

	std::string BundlePluginsDir()
	{
		const std::string self = OwnBinaryPath();
		if (self.empty())
			return "";
#if defined(_WIN32)
		return WinModuleDirFromPath(self);
#else
		return MacPluginsDirFromBinary(self);
#endif
	}

	bool RunBundledScript(const std::string& baseName, const std::vector<std::string>& args,
						  std::string& out)
	{
		const std::string script = BundledScriptPath(baseName);
		if (script.empty())
			return false;

		const std::string pluginsDir = BundlePluginsDir();

#if defined(_WIN32)
		std::string cmd = "powershell -NoProfile -ExecutionPolicy Bypass -File " + CmdQuote(script);
		for (const std::string& a : args)
			cmd += " " + CmdQuote(a);

		// 読み込み元のフォルダを教える（そこから読み、そこへ入れる）。子は親の環境を
		// 受け継ぐので、起動の間だけ自分の環境に置く。
		if (!pluginsDir.empty())
			::SetEnvironmentVariableW(L"VW_PLUGINS_DIR", Widen(pluginsDir).c_str());
		const bool ok = RunHidden(Widen(cmd), out);
		if (!pluginsDir.empty())
			::SetEnvironmentVariableW(L"VW_PLUGINS_DIR", nullptr);
		return ok;
#else
		std::string env;
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
#endif
	}
} // namespace vwprobe
