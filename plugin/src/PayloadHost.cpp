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

	std::string BundledPayloadPath()
	{
		const std::string self = OwnModulePath();
		if (self.empty())
			return "";
		const std::string name = payload::FileName();
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

	// -----------------------------------------------------------------------
	// Payload — 本体との付き合い 1 回ぶん（PayloadHost.h）。
	// -----------------------------------------------------------------------
	namespace
	{
		// 世代の通し番号。**複製先の名前を毎回変える**ためのもの（Windows は読み込み中の
		// ファイルを置き換えられないので、使い回すと 2 回目が古いまま読まれる）。
		unsigned NextGeneration()
		{
			static unsigned sGeneration = 0;
			return ++sGeneration;
		}
	} // namespace

	Payload::~Payload()
	{
		this->unload();
	}

	bool Payload::load(void* callbacks, void* logCtx, void (*log)(void*, const char*),
					   std::string& error)
	{
		error.clear();
		if (fLoaded)
			return true;
		if (callbacks == nullptr)
		{
			error = "SDK のコールバックが空です（殻の初期化が済んでいない）。";
			return false;
		}

		fSourcePath = BundledPayloadPath();
		if (fSourcePath.empty())
		{
			error = "本体（" + payload::FileName() + "）の置き場所を割り出せませんでした。";
			return false;
		}

		const std::string tempDir = TempDirectory();
		if (tempDir.empty())
		{
			error = "一時ディレクトリを取得できませんでした。";
			return false;
		}
		const std::string tag = std::to_string(NextGeneration());
		fTempPath = payload::TempCopyPath(tempDir, tag, payload::FileName(), PathSeparator());

		// 複製してから読む（PayloadHost.h の「必ず複製してから読む」）。
		std::string why;
		if (!CopyFileTo(fSourcePath, fTempPath, why))
		{
			error = "本体を読み込めませんでした。\n" + fSourcePath +
					" が殻（プラグイン）の隣にありますか？\n（" + why + "）";
			fTempPath.clear();
			return false;
		}

		if (!fModule.open(fTempPath, why))
		{
			error = "本体を読み込めませんでした。\n" + why;
			this->unload();
			return false;
		}

		auto abiFn = reinterpret_cast<VwPayloadAbiVersionFn>(fModule.symbol(VW_PAYLOAD_SYM_ABI));
		auto initFn = reinterpret_cast<VwPayloadInitFn>(fModule.symbol(VW_PAYLOAD_SYM_INIT));
		auto infoFn = reinterpret_cast<VwPayloadInfoFn>(fModule.symbol(VW_PAYLOAD_SYM_INFO));
		auto probeAtFn =
			reinterpret_cast<VwPayloadProbeAtFn>(fModule.symbol(VW_PAYLOAD_SYM_PROBE_AT));
		fRunFn = reinterpret_cast<VwPayloadRunFn>(fModule.symbol(VW_PAYLOAD_SYM_RUN));
		fShutdownFn =
			reinterpret_cast<VwPayloadShutdownFn>(fModule.symbol(VW_PAYLOAD_SYM_SHUTDOWN));
		if (abiFn == nullptr || initFn == nullptr || infoFn == nullptr || probeAtFn == nullptr ||
			fRunFn == nullptr || fShutdownFn == nullptr)
		{
			error = "本体の形が違います（必要な関数が見つかりません）。\n"
					"殻と本体の版が食い違っている可能性があります。";
			this->unload();
			return false;
		}

		// **版が違うなら呼ばない。** 殻と本体は独立に配られるので、ここでしか気付けない。
		const unsigned int abi = abiFn();
		if (abi != VW_PAYLOAD_ABI_VERSION)
		{
			error = "殻と本体の版が違います（本体=" + std::to_string(abi) +
					" 殻=" + std::to_string(VW_PAYLOAD_ABI_VERSION) +
					"）。\nプラグインごと入れ替えてください。";
			this->unload();
			return false;
		}

		// 素性は init の前でも取れる（読んだものが何かを先に言えるように）。
		VwPayloadInfo info{};
		info.size = static_cast<unsigned int>(sizeof(VwPayloadInfo));
		if (infoFn(&info) != kVwPayloadOk)
		{
			error = "本体の素性を取得できませんでした。";
			this->unload();
			return false;
		}
		fBuildId = (info.buildId != nullptr) ? info.buildId : "";
		fCommit = (info.commit != nullptr) ? info.commit : "";
		fBranch = (info.branch != nullptr) ? info.branch : "";
		fBuildTime = (info.buildTime != nullptr) ? info.buildTime : "";

		// **これはメンバである（load のローカルではない）。** 古い本体は渡された
		// VwPayloadHost のポインタを持ち続けることがあり、その先がローカルだと
		// load から戻った時点で腐る——実際にそれで Vectorworks ごと落ちた
		// （Findings「プラグインモジュールの読み込みと入れ替え」の「殻の記憶域を
		//  本体に持たせない」）。**降ろすまで生かす**のが殻の側の歯止め。
		fHost = VwPayloadHost{};
		fHost.size = static_cast<unsigned int>(sizeof(VwPayloadHost));
		fHost.abiVersion = VW_PAYLOAD_ABI_VERSION;
		fHost.callbacks = callbacks;
		fHost.logCtx = logCtx;
		fHost.log = log;

		const int status = initFn(&fHost);
		if (status != kVwPayloadOk)
		{
			error = "本体を初期化できませんでした（コード " + std::to_string(status) + "）。";
			this->unload();
			return false;
		}
		fLoaded = true;

		// 一覧を写し取る（向こうの const char* は次の呼び出しまでしか生きていない）。
		fProbes.clear();
		fProbes.reserve(info.probeCount);
		for (unsigned int i = 0; i < info.probeCount; ++i)
		{
			VwPayloadProbe entry{};
			entry.size = static_cast<unsigned int>(sizeof(VwPayloadProbe));
			if (probeAtFn(i, &entry) != kVwPayloadOk)
				continue;
			PayloadProbeInfo copy;
			copy.id = (entry.id != nullptr) ? entry.id : "";
			copy.title = (entry.title != nullptr) ? entry.title : "";
			copy.summary = (entry.summary != nullptr) ? entry.summary : "";
			copy.pr = (entry.pr != nullptr) ? entry.pr : "";
			copy.commit = (entry.commit != nullptr) ? entry.commit : "";
			copy.branch = (entry.branch != nullptr) ? entry.branch : "";
			copy.prTitle = (entry.prTitle != nullptr) ? entry.prTitle : "";
			fProbes.push_back(copy);
		}
		return true;
	}

	bool Payload::run(const std::string& id, std::string& outcome, std::string& logPath,
					  double& seconds, std::string& error)
	{
		error.clear();
		outcome.clear();
		logPath.clear();
		seconds = 0.0;
		if (!fLoaded || fRunFn == nullptr)
		{
			error = "本体が読み込まれていません。";
			return false;
		}

		VwPayloadResult result{};
		result.size = static_cast<unsigned int>(sizeof(VwPayloadResult));
		const int status = fRunFn(id.c_str(), &result);
		if (status != kVwPayloadOk)
		{
			error = "プローブを走らせられませんでした（コード " + std::to_string(status) + "）。";
			return false;
		}
		// **その場で写す**（次の呼び出しで無効になる。PayloadAbi.h）。
		outcome = (result.outcome != nullptr) ? result.outcome : "";
		logPath = (result.logPath != nullptr) ? result.logPath : "";
		seconds = result.seconds;
		return true;
	}

	void Payload::unload()
	{
		// 順序が肝。① 本体に殻への参照を手放させる ② 降ろす ③ 複製を消す。
		if (fLoaded && fShutdownFn != nullptr)
			fShutdownFn();
		fLoaded = false;
		fRunFn = nullptr;
		fShutdownFn = nullptr;
		fProbes.clear();
		// 本体は shutdown で手放したはず。殻の側も、渡していたものをここで捨てる
		// （降ろした後に触られても、少なくとも「腐った値」ではなくなる）。
		fHost = VwPayloadHost{};

		std::string ignored;
		(void)fModule.close(ignored);
		if (!fTempPath.empty())
		{
			// **降りたことを別の目で確かめてから消す。** dlclose が 0 を返しても消えて
			// いるとは限らない（参照が残っていれば残る）し、Windows では読み込み中の
			// ファイルは消せない。残しても世代ごとに名前が違うので次回に響かない。
			if (!IsModuleStillLoaded(fTempPath))
				(void)RemoveFileAt(fTempPath, ignored);
			fTempPath.clear();
		}
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
