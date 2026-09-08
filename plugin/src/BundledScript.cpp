//
//	BundledScript.cpp
//
//	BundledScript.h の実装。**プラットフォーム依存はここに閉じる**:
//	  * 自分のバイナリの場所（mac: dladdr / win: GetModuleHandleExW）
//	  * スクリプトの起動と標準出力の取り込み（popen / _popen）
//	パスの組み立てそのものは UpdateParse.h（純粋）にあり、単体テストが押さえている。
//
//	【出どころ】もとは Update.cpp の中にあった。結果の自動投稿（Feedback.cpp）でも
//	同じ道が要るので、**2 つ目の呼び出し元ができた時点で**切り出した——同じ popen の
//	作法を 2 か所に書くと、片方だけ直す事故が必ず起きる。
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
		// 読み込み元のフォルダを教える（そこから読み、そこへ入れる）。
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
#endif

		out.clear();
		std::array<char, 4096> buf{};
		size_t n = 0;
		while ((n = ::fread(buf.data(), 1, buf.size(), pipe)) > 0)
			out.append(buf.data(), n);

#if defined(_WIN32)
		::_pclose(pipe);
		if (!pluginsDir.empty())
			::SetEnvironmentVariableW(L"VW_PLUGINS_DIR", nullptr);
#else
		::pclose(pipe);
#endif
		return true;
	}
} // namespace vwprobe
