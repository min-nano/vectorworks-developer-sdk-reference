//
//	PayloadHost.cpp
//
//	PayloadHost.h の実装。**SDK の型は使わない**（境界を SDK から独立に保つため。
//	プリコンパイルヘッダ経由で宣言は入ってくるが、ここでは触らない）。プラットフォーム
//	判別も SDK の GS_MAC / GS_WIN ではなく素の __APPLE__ / _WIN32 で行う。
//
//	プラットフォーム依存はここに閉じている:
//	  * 自分の位置       … mac: dladdr / win: GetModuleHandleExW
//	  * 読み込み・解決・アンロード … mac: dlopen/dlsym/dlclose / win: LoadLibraryExW ほか
//	  * 残っているかの確認 … mac: dlopen(RTLD_NOLOAD) / win: GetModuleHandleW
//	ファイルの複製と削除は std::filesystem で足りるので分岐しない。
//

#include "PluginPrefix.h"
#include "PayloadHost.h"

#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif

namespace vwprobe
{
	namespace
	{
#if defined(_WIN32)
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

		// GetLastError() を読める形に（メッセージまでは要らない。番号があれば追える）。
		std::string LastError(const char* what)
		{
			return std::string(what) + " が失敗しました（GetLastError=" +
				   std::to_string((unsigned long)::GetLastError()) + "）";
		}
#endif
	} // namespace

	// -----------------------------------------------------------------------
	PayloadModule::~PayloadModule()
	{
		// **ここでは降ろさない**（PayloadHost.h の注記）。open したまま捨てられた場合は
		// モジュールがプロセスに残るだけで、壊れはしない。
	}

	bool PayloadModule::open(const std::string& path, std::string& error)
	{
		error.clear();
		if (fHandle != nullptr)
		{
			error = "すでに読み込んでいる（" + fPath + "）";
			return false;
		}
		if (path.empty())
		{
			error = "パスが空";
			return false;
		}

#if defined(_WIN32)
		// LOAD_WITH_ALTERED_SEARCH_PATH: 依存 DLL を**そのファイルの隣**から探させる
		// （Vectorworks 本体のフォルダではなく）。
		HMODULE handle =
			::LoadLibraryExW(Widen(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (handle == nullptr)
		{
			error = LastError("LoadLibraryExW");
			return false;
		}
		fHandle = (void*)handle;
#else
		// RTLD_NOW: 未解決シンボルを**読み込んだ時点で**弾く（呼んだ瞬間に落ちるより、
		// 読み込みが失敗して理由が出るほうが調査になる）。
		// RTLD_LOCAL: このモジュールの記号をプロセス全体へ晒さない。ペイロードは自分の
		// SDK（libVWSDK.a）の複製を持つので、晒すと殻の側の同名記号と混ざりうる。
		void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (handle == nullptr)
		{
			const char* why = ::dlerror();
			error =
				std::string("dlopen が失敗しました: ") + ((why != nullptr) ? why : "(理由なし)");
			return false;
		}
		fHandle = handle;
#endif
		fPath = path;
		return true;
	}

	bool PayloadModule::close(std::string& error)
	{
		error.clear();
		if (fHandle == nullptr)
			return true;

#if defined(_WIN32)
		const BOOL ok = ::FreeLibrary((HMODULE)fHandle);
		fHandle = nullptr;
		if (ok == 0)
		{
			error = LastError("FreeLibrary");
			return false;
		}
#else
		const int rc = ::dlclose(fHandle);
		fHandle = nullptr;
		if (rc != 0)
		{
			const char* why = ::dlerror();
			error =
				std::string("dlclose が失敗しました: ") + ((why != nullptr) ? why : "(理由なし)");
			return false;
		}
#endif
		return true;
	}

	void* PayloadModule::symbol(const char* name) const
	{
		if (fHandle == nullptr || name == nullptr)
			return nullptr;
#if defined(_WIN32)
		return (void*)::GetProcAddress((HMODULE)fHandle, name);
#else
		return ::dlsym(fHandle, name);
#endif
	}

	// -----------------------------------------------------------------------
	std::string OwnModulePath()
	{
#if defined(_WIN32)
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
#else
		Dl_info info{};
		if (::dladdr(reinterpret_cast<const void*>(&OwnModulePath), &info) == 0 ||
			info.dli_fname == nullptr)
			return "";
		return info.dli_fname;
#endif
	}

	std::string BundledPayloadPath(const std::string& variant)
	{
		const std::string self = OwnModulePath();
		if (self.empty())
			return "";
		const std::string name = payload::FileName(variant);
#if defined(_WIN32)
		return payload::WinPayloadPathFromModule(self, name);
#else
		return payload::MacPayloadPathFromBinary(self, name);
#endif
	}

	std::string TempDirectory()
	{
		std::error_code ec;
		const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			return "";
		return dir.string();
	}

	char PathSeparator()
	{
#if defined(_WIN32)
		return '\\';
#else
		return '/';
#endif
	}

	bool CopyFileTo(const std::string& from, const std::string& to, std::string& error)
	{
		error.clear();
		std::error_code ec;
		// 上書きで写す（世代ごとに別名にするので通常は新規だが、作り直しでも通るように）。
		std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
		{
			error = "複製に失敗しました（" + from + " → " + to + "）: " + ec.message();
			return false;
		}
		return true;
	}

	bool RemoveFileAt(const std::string& path, std::string& error)
	{
		error.clear();
		std::error_code ec;
		const bool removed = std::filesystem::remove(path, ec);
		if (ec)
		{
			error = "削除に失敗しました（" + path + "）: " + ec.message();
			return false;
		}
		if (!removed)
		{
			error = "そのファイルはありませんでした（" + path + "）";
			return false;
		}
		return true;
	}

	bool IsModuleStillLoaded(const std::string& path)
	{
		if (path.empty())
			return false;
#if defined(_WIN32)
		// 読み込まれていればハンドルが返る（参照数は増えない）。
		return ::GetModuleHandleW(Widen(path).c_str()) != nullptr;
#else
		// RTLD_NOLOAD: **すでに読み込まれているときだけ**ハンドルを返す。返ってきた
		// ハンドルは参照を 1 つ持つので、必ず dlclose して元へ戻す。
		void* handle = ::dlopen(path.c_str(), RTLD_NOLOAD | RTLD_LAZY);
		if (handle == nullptr)
			return false;
		(void)::dlclose(handle);
		return true;
#endif
	}
} // namespace vwprobe
