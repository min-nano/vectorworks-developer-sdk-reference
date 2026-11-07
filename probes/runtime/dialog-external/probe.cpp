//
//	probes/runtime/dialog-external/probe.cpp
//
//	[issue #302] Vectorworks のダイアログを**別のプロセス**（CLI の代わりのヘルパー）から
//	見つけ、読み、押せるかを測るために、呼び出しの最中にダイアログを出す。プローブ自身は押さない。
//
//	一時ディレクトリの VwSdkProbes-dialog-external.do を 1 行ずつ行う（読んだら消す）。空行と
//	# で始まる行は飛ばす。パスの中の $T は作業用フォルダ（VwSdkProbes-dialog-external）に置き換わる。
//
//	  図面   new / open <P> / openv <P>（エラーの表示を許して）/ switch <P> / closefront / docs
//	  保存   saveto <P> / save（前面の図面を、一覧のパスの文字列のまま）
//	  図形   rect <名前> / event <名前>（undo イベントを開いて閉じる）/ list / find <名前>
//	  取消   doundo / menu（「取り消し」の項目の文言）
//	  ファイル  mkdir <P> / rm <P> / ro <P> / rw <P> / write <P> / fs <P> / ls <P> / sh <コマンド>
//	  他     close / domenu <名前> / note <文> / busy <秒>（メインスレッドを塞ぐ）
//	  見張り  press auto|none|after <秒> / nest <P>（次の呼び出しのダイアログの最中に、見張りの
//	         刻みからそのパスへ保存する。利用者のダイアログの最中に呼び出しが届いた形）
//
//	**見張りは見るだけで、既定では押さない。** 既定は `press after 90`——別のプロセスが押せなかった
//	ときに VW が人待ちで止まり続けないための安全弁で、90 秒待って初めて押す。新しく出た窓・閉じた
//	時刻・呼び出しが戻った時刻をログへ書く（別のプロセスの見え方と突き合わせる）。
//
//	**$T/in-call.txt は呼び出しの間だけある印**（CLI のプラグインが「いま呼び出しの最中」を
//	別のプロセスへ知らせる形の試し）。中身は「タグ 開始のエポック ms pid」。
//
//	event の動詞だけは、取り消しの履歴が問いなので、undo イベントを自分で開いて閉じる
//	（probes/runtime/README.md の例外）。
//

#include "Probe.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#	include <fcntl.h>
#	include <objc/message.h>
#	include <objc/runtime.h>
#	include <sys/stat.h>
#	include <unistd.h>
#else
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <io.h>
#	include <windows.h>
#endif

namespace
{
	using namespace VectorWorks;
	namespace sdfs = std::filesystem;

	std::string SdFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	// 濁点・半濁点の結合文字（U+3099 / U+309A）が入っていれば NFD。無ければ NFC とみなす。
	std::string SdForm(const std::string& s)
	{
		const bool nfd = s.find("\xE3\x82\x99") != std::string::npos ||
						 s.find("\xE3\x82\x9A") != std::string::npos;
		return nfd ? "NFD" : "NFC";
	}

	sdfs::path SdPath(const std::string& s)
	{
#if GS_WIN
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
		std::wstring w(static_cast<std::size_t>(n), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
		w.resize(static_cast<std::size_t>(n) - 1);
		return sdfs::path(w);
#else
		return sdfs::path(s);
#endif
	}

	std::string SdUtf8(const sdfs::path& p)
	{
#if GS_WIN
		const std::wstring w = p.wstring();
		const int n =
			::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string out(static_cast<std::size_t>(n), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, nullptr, nullptr);
		out.resize(static_cast<std::size_t>(n) - 1);
		return out;
#else
		return p.string();
#endif
	}

	std::string SdPathOf(Filing::IFileIdentifier* pFileID)
	{
		if (pFileID == nullptr)
			return "(nullptr)";
		TXString path;
		pFileID->GetFileFullPath(path);
		return static_cast<const char*>(path);
	}

	struct SdDoc
	{
		Sint32 ref = -1;
		bool active = false;
		bool inMemoryOnly = false;
		std::string path;
	};

	std::vector<SdDoc> SdDocs()
	{
		std::vector<SdDoc> out;
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			SdDoc d;
			d.ref = files[i].fFileRef;
			d.active = files[i].fIsActive;
			d.inMemoryOnly = files[i].fIsInMemoryOnly;
			d.path = SdPathOf(files[i].fpFileID);
			out.push_back(d);
		}
		return out;
	}

	std::string SdActivePath()
	{
		for (const SdDoc& d : SdDocs())
		{
			if (d.active)
				return d.path;
		}
		return "(前面なし)";
	}

	void SdLogDocs(vwprobe::Report& probe, const std::string& tag)
	{
		const std::vector<SdDoc> docs = SdDocs();
		probe.log(tag + ": 件数=" + std::to_string(docs.size()));
		for (const SdDoc& d : docs)
		{
			probe.log(tag + ":   fileRef=" + std::to_string(d.ref) + " active=" + SdFlag(d.active) +
					  " inMemoryOnly=" + SdFlag(d.inMemoryOnly) + " form=" + SdForm(d.path) +
					  " path=" + d.path);
		}
	}

	std::uint64_t SdHash(const sdfs::path& p)
	{
		std::ifstream in(p, std::ios::binary);
		std::uint64_t h = 14695981039346656037ULL;
		char buf[65536];
		while (in)
		{
			in.read(buf, sizeof buf);
			const std::streamsize n = in.gcount();
			for (std::streamsize i = 0; i < n; ++i)
			{
				h ^= static_cast<unsigned char>(buf[i]);
				h *= 1099511628211ULL;
			}
		}
		return h;
	}

	void SdLogFile(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		std::error_code ec;
		const sdfs::path p = SdPath(path);
		if (!sdfs::exists(p, ec))
		{
			probe.log(tag + ": ファイル 無い: " + path);
			return;
		}
		const sdfs::perms perms = sdfs::status(p, ec).permissions();
		const bool ownerWrite = (perms & sdfs::perms::owner_write) != sdfs::perms::none;
		if (sdfs::is_directory(p, ec))
		{
			probe.log(tag + ": フォルダ ある 書ける=" + SdFlag(ownerWrite) + ": " + path);
			return;
		}
		const long long when =
			static_cast<long long>(sdfs::last_write_time(p, ec).time_since_epoch().count());
		char hash[32];
		std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(SdHash(p)));
		probe.log(tag + ": ファイル ある size=" + std::to_string(sdfs::file_size(p, ec)) +
				  " 書ける=" + SdFlag(ownerWrite) + " time=" + std::to_string(when) +
				  " hash=" + hash + ": " + path);
	}

	// -----------------------------------------------------------------------
	// 窓と部品を読む（プラットフォームごと）

	struct SdControl
	{
		std::string cls;
		std::string text;
		bool isButton = false;
		int id = 0;
		void* handle = nullptr;
	};

#if GS_MAC
	id SdSend(id o, const char* sel)
	{
		return reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(o, sel_registerName(sel));
	}

	long SdSendLong(id o, const char* sel)
	{
		return reinterpret_cast<long (*)(id, SEL)>(objc_msgSend)(o, sel_registerName(sel));
	}

	bool SdSendBool(id o, const char* sel)
	{
		return reinterpret_cast<signed char (*)(id, SEL)>(objc_msgSend)(o, sel_registerName(sel)) !=
			   0;
	}

	id SdAt(id array, long i)
	{
		SEL sel = sel_registerName("objectAtIndex:");
		return reinterpret_cast<id (*)(id, SEL, long)>(objc_msgSend)(array, sel, i);
	}

	bool SdIsKind(id o, const char* className)
	{
		SEL sel = sel_registerName("isKindOfClass:");
		Class cls = objc_getClass(className);
		return reinterpret_cast<signed char (*)(id, SEL, Class)>(objc_msgSend)(o, sel, cls) != 0;
	}

	std::string SdNsString(id s)
	{
		if (s == nullptr)
			return std::string();
		const char* utf8 = reinterpret_cast<const char* (*)(id, SEL)>(objc_msgSend)(
			s, sel_registerName("UTF8String"));
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	id SdApp()
	{
		return SdSend(reinterpret_cast<id>(objc_getClass("NSApplication")), "sharedApplication");
	}

	std::vector<void*> SdTopWindows()
	{
		std::vector<void*> out;
		id windows = SdSend(SdApp(), "windows");
		const long n = SdSendLong(windows, "count");
		for (long i = 0; i < n; ++i)
		{
			id w = SdAt(windows, i);
			if (SdSendBool(w, "isVisible"))
				out.push_back(w);
		}
		return out;
	}

	void SdWalkView(id view, std::vector<SdControl>& out)
	{
		if (SdIsKind(view, "NSButton"))
		{
			SdControl c;
			c.cls = object_getClassName(view);
			c.text = SdNsString(SdSend(view, "title"));
			c.isButton = true;
			c.handle = view;
			out.push_back(c);
		}
		else if (SdIsKind(view, "NSTextField"))
		{
			SdControl c;
			c.cls = object_getClassName(view);
			c.text = SdNsString(SdSend(view, "stringValue"));
			out.push_back(c);
		}
		id subviews = SdSend(view, "subviews");
		const long n = SdSendLong(subviews, "count");
		for (long i = 0; i < n; ++i)
			SdWalkView(SdAt(subviews, i), out);
	}

	std::vector<SdControl> SdControlsOf(void* window)
	{
		std::vector<SdControl> out;
		id content = SdSend(static_cast<id>(window), "contentView");
		if (content != nullptr)
			SdWalkView(content, out);
		return out;
	}

	std::string SdWindowText(void* window)
	{
		id w = static_cast<id>(window);
		return std::string("\"") + SdNsString(SdSend(w, "title")) +
			   "\" class=" + object_getClassName(w) + " sheet=" + SdFlag(SdSendBool(w, "isSheet"));
	}

	void SdPressControl(const SdControl& c)
	{
		SEL sel = sel_registerName("performClick:");
		reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(static_cast<id>(c.handle), sel,
															  nullptr);
	}

	// 「取り消し」の項目（⌘Z）を、メニューの木から探す。
	void SdWalkMenu(vwprobe::Report& probe, id menu, const std::string& parent)
	{
		SdSend(menu, "update");
		id items = SdSend(menu, "itemArray");
		const long n = SdSendLong(items, "count");
		for (long i = 0; i < n; ++i)
		{
			id item = SdAt(items, i);
			const std::string key = SdNsString(SdSend(item, "keyEquivalent"));
			const std::string title = SdNsString(SdSend(item, "title"));
			if (key == "z" || key == "Z")
			{
				probe.log("menu: [" + parent + "] \"" + title + "\" key=" + key +
						  " enabled=" + SdFlag(SdSendBool(item, "isEnabled")));
			}
			id sub = SdSend(item, "submenu");
			if (sub != nullptr)
				SdWalkMenu(probe, sub, title);
		}
	}

	void SdLogUndoMenu(vwprobe::Report& probe)
	{
		id menu = SdSend(SdApp(), "mainMenu");
		if (menu == nullptr)
		{
			probe.log("menu: mainMenu が無い");
			return;
		}
		SdWalkMenu(probe, menu, "");
	}
#else
	std::string SdWide(const wchar_t* w)
	{
		if (w == nullptr || *w == 0)
			return std::string();
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		if (n <= 1)
			return std::string();
		std::string out(static_cast<std::size_t>(n), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
		out.resize(static_cast<std::size_t>(n) - 1);
		return out;
	}

	BOOL CALLBACK SdCollectTop(HWND h, LPARAM param)
	{
		DWORD pid = 0;
		::GetWindowThreadProcessId(h, &pid);
		if (pid == ::GetCurrentProcessId() && ::IsWindowVisible(h))
			reinterpret_cast<std::vector<void*>*>(param)->push_back(h);
		return TRUE;
	}

	std::vector<void*> SdTopWindows()
	{
		std::vector<void*> out;
		::EnumWindows(&SdCollectTop, reinterpret_cast<LPARAM>(&out));
		return out;
	}

	BOOL CALLBACK SdCollectChild(HWND h, LPARAM param)
	{
		if (!::IsWindowVisible(h))
			return TRUE;
		SdControl c;
		wchar_t cls[128] = {0};
		wchar_t text[512] = {0};
		::GetClassNameW(h, cls, 128);
		::GetWindowTextW(h, text, 512);
		c.cls = SdWide(cls);
		c.text = SdWide(text);
		c.id = ::GetDlgCtrlID(h);
		c.isButton = c.cls == "Button";
		c.handle = h;
		reinterpret_cast<std::vector<SdControl>*>(param)->push_back(c);
		return TRUE;
	}

	std::vector<SdControl> SdControlsOf(void* window)
	{
		std::vector<SdControl> out;
		::EnumChildWindows(static_cast<HWND>(window), &SdCollectChild,
						   reinterpret_cast<LPARAM>(&out));
		return out;
	}

	std::string SdWindowText(void* window)
	{
		wchar_t title[256] = {0};
		wchar_t cls[128] = {0};
		::GetWindowTextW(static_cast<HWND>(window), title, 256);
		::GetClassNameW(static_cast<HWND>(window), cls, 128);
		return "\"" + SdWide(title) + "\" class=" + SdWide(cls);
	}

	void SdPressControl(const SdControl& c)
	{
		::SendMessageW(static_cast<HWND>(c.handle), BM_CLICK, 0, 0);
	}

	void SdWalkMenu(vwprobe::Report& probe, HMENU menu, const std::string& parent)
	{
		const int n = ::GetMenuItemCount(menu);
		for (int i = 0; i < n; ++i)
		{
			wchar_t text[256] = {0};
			::GetMenuStringW(menu, static_cast<UINT>(i), text, 256, MF_BYPOSITION);
			const std::string title = SdWide(text);
			if (title.find("取り消し") != std::string::npos ||
				title.find("Undo") != std::string::npos)
			{
				const UINT state = ::GetMenuState(menu, static_cast<UINT>(i), MF_BYPOSITION);
				probe.log("menu: [" + parent + "] \"" + title +
						  "\" enabled=" + SdFlag((state & (MF_GRAYED | MF_DISABLED)) == 0));
			}
			HMENU sub = ::GetSubMenu(menu, i);
			if (sub != nullptr)
				SdWalkMenu(probe, sub, title);
		}
	}

	void SdLogUndoMenu(vwprobe::Report& probe)
	{
		for (void* w : SdTopWindows())
		{
			HMENU menu = ::GetMenu(static_cast<HWND>(w));
			if (menu != nullptr)
			{
				SdWalkMenu(probe, menu, "");
				return;
			}
		}
		probe.log("menu: メニューを持つ窓が無い");
	}
#endif

	// -----------------------------------------------------------------------
	// 見張り

	struct SdWatch
	{
		vwprobe::Report* probe = nullptr;
		std::string tag;
		std::chrono::steady_clock::time_point t0;
		std::set<void*> before;
		std::set<void*> logged;
		std::set<void*> gone;
		void* dialog = nullptr;
		long long seenAt = -1;
		long long ticks = 0;
		int presses = 0;
	};

	SdWatch* gSdWatch = nullptr;
	bool gSdAutoPress = true;
	long long gSdPressAfterMs = 90000;
	std::string gSdMarker;
	std::string gSdNestPath;
	bool gSdInNest = false;

	long long SdSelfPid()
	{
#if GS_MAC
		return static_cast<long long>(::getpid());
#else
		return static_cast<long long>(::GetCurrentProcessId());
#endif
	}

	long long SdMs(std::chrono::steady_clock::time_point t0)
	{
		const auto d = std::chrono::steady_clock::now() - t0;
		return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
	}

	bool SdPick(const std::vector<SdControl>& controls, bool lastResort, SdControl& out)
	{
		for (const SdControl& c : controls)
		{
			if (c.isButton && (c.text.find("キャンセル") != std::string::npos ||
							   c.text.find("Cancel") != std::string::npos))
			{
				out = c;
				return true;
			}
		}
		for (const SdControl& c : controls)
		{
			if (c.isButton && (c.text == "OK" || c.text == "&OK" || c.text == "ＯＫ"))
			{
				out = c;
				return true;
			}
		}
		if (!lastResort)
			return false;
		for (const SdControl& c : controls)
		{
			if (c.isButton)
				out = c;
		}
		return out.handle != nullptr;
	}

	void SdWatchTick()
	{
		SdWatch* w = gSdWatch;
		if (w == nullptr || gSdInNest)
			return;
		++w->ticks;
		const long long now = SdMs(w->t0);
		const std::vector<void*> windows = SdTopWindows();
		for (void* h : windows)
		{
			if (w->before.count(h) != 0 || w->logged.count(h) != 0)
				continue;
			w->logged.insert(h);
			w->dialog = h;
			w->seenAt = now;
			w->probe->log(w->tag + ":   [" + std::to_string(now) + "ms] 新しい窓 " +
						  SdWindowText(h));
			for (const SdControl& c : SdControlsOf(h))
			{
				w->probe->log(w->tag + ":     " + (c.isButton ? "ボタン" : "文字") + " class=" +
							  c.cls + " id=" + std::to_string(c.id) + " text=\"" + c.text + "\"");
			}
		}
		if (!gSdNestPath.empty() && w->dialog != nullptr && now - w->seenAt >= 1000)
		{
			const std::string path = gSdNestPath;
			gSdNestPath.clear();
			gSdInNest = true;
			w->probe->log(w->tag + ":   [" + std::to_string(now) + "ms] 重ねて保存する: " + path);
			Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
			fileID->Set(TXString(path.c_str()));
			const GSError err = gSDK->SaveActiveDocumentPath(fileID);
			w->probe->log(w->tag + ":   [" + std::to_string(SdMs(w->t0)) +
						  "ms] 重ねた保存が戻った GSError=" + std::to_string((long long)err));
			gSdInNest = false;
			return;
		}
		for (void* h : w->logged)
		{
			bool visible = false;
			for (void* o : windows)
				visible = visible || o == h;
			if (!visible && w->gone.insert(h).second)
			{
				w->probe->log(w->tag + ":   [" + std::to_string(now) + "ms] 窓が閉じた");
			}
		}
		if (!gSdAutoPress || w->dialog == nullptr || now - w->seenAt < gSdPressAfterMs)
			return;
		bool stillThere = false;
		for (void* h : windows)
		{
			if (h == w->dialog)
				stillThere = true;
		}
		if (!stillThere)
		{
			w->dialog = nullptr;
			return;
		}
		SdControl button;
		if (!SdPick(SdControlsOf(w->dialog), now - w->seenAt >= gSdPressAfterMs + 5000, button))
			return;
		++w->presses;
		w->probe->log(w->tag + ":   [" + std::to_string(now) + "ms] 見張りが押す: \"" +
					  button.text + "\"");
		w->dialog = nullptr;
		SdPressControl(button);
	}

#if GS_MAC
	void SdTimerFired(CFRunLoopTimerRef, void*)
	{
		SdWatchTick();
	}
#else
	VOID CALLBACK SdTimerFired(HWND, UINT, UINT_PTR, DWORD)
	{
		SdWatchTick();
	}
#endif

	// 呼び出しの間だけ見張る。作った範囲を抜けるときに必ず外す。
	class SdWatchScope
	{
	public:
		SdWatchScope(vwprobe::Report& probe, const std::string& tag)
		{
			fWatch.probe = &probe;
			fWatch.tag = tag;
			for (void* h : SdTopWindows())
				fWatch.before.insert(h);
			fWatch.t0 = std::chrono::steady_clock::now();
			gSdWatch = &fWatch;
			if (!gSdMarker.empty())
			{
				const long long epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
											std::chrono::system_clock::now().time_since_epoch())
											.count();
				std::ofstream(SdPath(gSdMarker), std::ios::binary)
					<< tag << " " << epoch << " " << SdSelfPid() << "\n";
			}
#if GS_MAC
			fTimer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.1,
										  0.1, 0, 0, &SdTimerFired, nullptr);
			CFRunLoopAddTimer(CFRunLoopGetCurrent(), fTimer, kCFRunLoopCommonModes);
#else
			fTimer = ::SetTimer(nullptr, 0, 100, &SdTimerFired);
#endif
		}

		~SdWatchScope()
		{
#if GS_MAC
			CFRunLoopTimerInvalidate(fTimer);
			CFRelease(fTimer);
#else
			if (fTimer != 0)
				::KillTimer(nullptr, fTimer);
#endif
			gSdWatch = nullptr;
			if (!gSdMarker.empty())
			{
				std::error_code ec;
				sdfs::remove(SdPath(gSdMarker), ec);
			}
		}

		// 呼び出しが戻った直後に呼ぶ。ダイアログが出たか・所要を 1 行で。
		void report(const std::string& result)
		{
			fWatch.probe->log(fWatch.tag + ": " + result +
							  " 所要=" + std::to_string(SdMs(fWatch.t0)) +
							  "ms 刻み=" + std::to_string(fWatch.ticks) +
							  " 新しい窓=" + std::to_string(fWatch.logged.size()) +
							  " 押した=" + std::to_string(fWatch.presses));
		}

		SdWatchScope(const SdWatchScope&) = delete;
		SdWatchScope& operator=(const SdWatchScope&) = delete;

	private:
		SdWatch fWatch;
#if GS_MAC
		CFRunLoopTimerRef fTimer = nullptr;
#else
		UINT_PTR fTimer = 0;
#endif
	};

	// -----------------------------------------------------------------------
	// 動詞

	void SdSaveTo(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(path.c_str()));
		const std::string readback = SdPathOf(fileID);
		probe.log(tag + ": 渡す form=" + SdForm(path) + " path=" + path);
		probe.log(tag + ": 読み戻し form=" + SdForm(readback) +
				  " 同じ=" + SdFlag(readback == path) + " path=" + readback);
		probe.log(tag + ": 保存の前の前面=" + SdActivePath());
		GSError err = 0;
		{
			SdWatchScope watch(probe, tag);
			err = gSDK->SaveActiveDocumentPath(fileID);
			char hex[32];
			std::snprintf(hex, sizeof hex, "0x%08x", static_cast<unsigned int>(err));
			watch.report("GSError=" + std::to_string(static_cast<long long>(err)) + " (" + hex +
						 ")");
		}
		bool saved = false;
		Filing::IFileIdentifierPtr active(Filing::IID_FileIdentifier);
		gSDK->GetActiveDocument(&active, saved);
		probe.log(tag + ": 保存の後の前面=" + SdActivePath() + " outSaved=" + SdFlag(saved));
		SdLogFile(probe, tag, path);
	}

	void SdOpen(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(path.c_str()));
		SdWatchScope watch(probe, tag);
		const bool ok = gSDK->OpenDocumentPath(fileID, false);
		watch.report("OpenDocumentPath=" + SdFlag(ok));
	}

	// 開く前に VW がパスをどう見ているかを読み、エラーの表示を許して開く。
	void SdOpenLoud(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(path.c_str()));
		bool exists = false;
		const VCOMError existsErr = fileID->ExistsOnDisk(exists);
		size_t size = 0;
		const VCOMError sizeErr = fileID->GetFileSize(size);
		probe.log(tag + ": ExistsOnDisk=" + SdFlag(exists) + " err=" + std::to_string(existsErr));
		probe.log(tag + ": GetFileSize=" + std::to_string(size) +
				  " err=" + std::to_string(sizeErr));
		SdWatchScope watch(probe, tag);
		const bool ok = gSDK->OpenDocumentPath(fileID, true);
		watch.report("OpenDocumentPath(表示あり)=" + SdFlag(ok));
	}

	void SdNew(vwprobe::Report& probe, const std::string& tag)
	{
		SdWatchScope watch(probe, tag);
		const bool ok = gSDK->OpenDocumentPath(nullptr, false);
		watch.report("OpenDocumentPath(nullptr)=" + SdFlag(ok) + " 前面=" + SdActivePath());
	}

	void SdSwitch(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		for (const SdDoc& d : SdDocs())
		{
			if (d.path == path)
			{
				const bool ok = gSDK->SwitchToOpenFile(d.ref);
				probe.log(tag + ": SwitchToOpenFile=" + SdFlag(ok) + " 前面=" + SdActivePath());
				return;
			}
		}
		probe.fail(tag + ": 開いていない: " + path);
	}

	void SdRect(vwprobe::Report& probe, const std::string& tag, const std::string& name)
	{
		MCObjectHandle h = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		if (h == nullptr)
		{
			probe.fail(tag + ": CreateRectangle が nil");
			return;
		}
		gSDK->SetObjectName(h, TXString(name.c_str()));
		gSDK->EndUndoEvent();
		probe.log(tag + ": 作った（イベントの外）: " + name);
	}

	void SdEvent(vwprobe::Report& probe, const std::string& tag, const std::string& name)
	{
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent(TXString(name.c_str()));
		MCObjectHandle h = gSDK->CreateRectangle(WorldRect(20, 10, 30, 0));
		if (h != nullptr)
		{
			gSDK->SetObjectName(h, TXString(name.c_str()));
			gSDK->AddAfterSwapObject(h);
		}
		const Boolean closed = gSDK->EndUndoEvent();
		probe.log(tag + ": 作った（イベントの中）: " + name + " 作れた=" + SdFlag(h != nullptr) +
				  " EndUndoEvent=" + SdFlag(closed != 0));
	}

	// 階（CreateStory）とレイヤをイベントの中で作る。#223 の条件の切り分け用。
	void SdStoryEvent(vwprobe::Report& probe, const std::string& tag, const std::string& name)
	{
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent(TXString(name.c_str()));
		TXString storyName(name.c_str());
		TXString storySuffix(name.c_str());
		const bool created = gSDK->CreateStory(storyName, storySuffix);
		const Boolean closed = gSDK->EndUndoEvent();
		probe.log(tag + ": 階 " + name + " 作れた=" + SdFlag(created) +
				  " EndUndoEvent=" + SdFlag(closed != 0));
	}

	void SdLayerEvent(vwprobe::Report& probe, const std::string& tag, const std::string& name)
	{
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent(TXString(name.c_str()));
		MCObjectHandle layer = gSDK->CreateLayer(name.c_str(), kLayerDesign);
		if (layer != nullptr)
			gSDK->AddAfterSwapObject(layer);
		const Boolean closed = gSDK->EndUndoEvent();
		probe.log(tag + ": レイヤ " + name + " 作れた=" + SdFlag(layer != nullptr) +
				  " EndUndoEvent=" + SdFlag(closed != 0));
	}

	// 構造材（PIO）をイベントの中で作る。パスも登録する（#223 と同じ）。
	void SdMemberEvent(vwprobe::Report& probe, const std::string& tag, const std::string& name)
	{
		gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
		gSDK->EndUndoEvent();
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent(TXString(name.c_str()));
		MCObjectHandle path = gSDK->CreateLine(WorldPt(0, 0), WorldPt(1000, 0));
		bool made = false;
		if (path != nullptr)
		{
			gSDK->AddAfterSwapObject(path);
			MCObjectHandle member =
				gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr, true);
			if (member != nullptr)
			{
				made = true;
				gSDK->SetObjectName(member, TXString(name.c_str()));
				gSDK->ResetObject(member);
			}
		}
		const Boolean closed = gSDK->EndUndoEvent();
		probe.log(tag + ": 構造材 " + name + " 作れた=" + SdFlag(made) +
				  " EndUndoEvent=" + SdFlag(closed != 0));
	}

	// 名前で引く（階・レイヤは list に出ない）。
	void SdFind(vwprobe::Report& probe, const std::string& tag, const std::string& name)
	{
		probe.log(tag + ": " + name +
				  " 引ける=" + SdFlag(gSDK->GetNamedObject(TXString(name.c_str())) != nullptr));
	}

	void SdList(vwprobe::Report& probe, const std::string& tag)
	{
		int live = 0;
		int stones = 0;
		std::string names;
		MCObjectHandle layer = gSDK->GetCurrentLayer();
		MCObjectHandle h = layer != nullptr ? gSDK->FirstMemberObj(layer) : nullptr;
		for (; h != nullptr; h = gSDK->NextObject(h))
		{
			const short type = gSDK->GetObjectTypeN(h);
			if (type == 90)
			{
				++stones;
				continue;
			}
			if (type == 0)
				continue;
			++live;
			TXString name;
			gSDK->GetObjectName(h, name);
			names += " [" + std::to_string(type) + ":" + static_cast<const char*>(name) + "]";
		}
		probe.log(tag + ": 図形=" + std::to_string(live) + " 置き石=" + std::to_string(stones) +
				  names);
	}

	void SdDoUndo(vwprobe::Report& probe, const std::string& tag)
	{
		SdWatchScope watch(probe, tag);
		const short r = gSDK->DoMenuName("Undo", 0);
		watch.report("DoMenuName(Undo)=" + std::to_string(r));
	}

	void SdLs(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		std::error_code ec;
		sdfs::directory_iterator it(SdPath(path), ec);
		if (ec)
		{
			probe.log(tag + ": 読めない: " + ec.message());
			return;
		}
		int n = 0;
		for (const sdfs::directory_entry& e : it)
		{
			const std::string name = SdUtf8(e.path().filename());
			probe.log(tag + ":   form=" + SdForm(name) + " " + name);
			++n;
		}
		probe.log(tag + ": 件数=" + std::to_string(n));
	}

	void SdSetWritable(vwprobe::Report& probe, const std::string& tag, const std::string& path,
					   bool writable)
	{
		std::error_code ec;
		const sdfs::perm_options opt =
			writable ? sdfs::perm_options::add : sdfs::perm_options::remove;
		sdfs::permissions(SdPath(path),
						  sdfs::perms::owner_write | sdfs::perms::group_write |
							  sdfs::perms::others_write,
						  opt, ec);
		probe.log(tag + ": " + (ec ? ec.message() : std::string("ok")));
		SdLogFile(probe, tag, path);
	}

	void SdMove(vwprobe::Report& probe, const std::string& tag, const std::string& args)
	{
		const std::string arrow = " -> ";
		const std::size_t at = args.find(arrow);
		if (at == std::string::npos)
		{
			probe.fail(tag + ": mv の書式は <A> -> <B>");
			return;
		}
		std::error_code ec;
		sdfs::rename(SdPath(args.substr(0, at)), SdPath(args.substr(at + arrow.size())), ec);
		probe.log(tag + ": " + (ec ? ec.message() : std::string("ok")));
	}

	// -----------------------------------------------------------------------
	// #293: 呼ぶ前に OS へ聞く候補の答え。1 回で全部の候補を並べて書く。

	std::string SdParent(const std::string& path)
	{
		const std::size_t at = path.find_last_of(GS_WIN ? "\\/" : "/");
		return at == std::string::npos ? std::string() : path.substr(0, at);
	}

#if GS_MAC
	std::string SdErrno(int e)
	{
		return std::to_string(e) + "(" + std::strerror(e) + ")";
	}

	void SdCheckOne(vwprobe::Report& probe, const std::string& tag, const std::string& label,
					const std::string& path)
	{
		errno = 0;
		const int acc = ::access(path.c_str(), W_OK);
		const std::string accErr = acc == 0 ? "0" : SdErrno(errno);
		struct stat st = {};
		const int sr = ::stat(path.c_str(), &st);
		char mode[32];
		std::snprintf(mode, sizeof mode, "%o", static_cast<unsigned int>(st.st_mode & 07777));
		char flags[32];
		std::snprintf(flags, sizeof flags, "0x%x", static_cast<unsigned int>(st.st_flags));
		std::string line = label + ": access(W_OK)=" + std::to_string(acc) + " errno=" + accErr +
						   " stat=" + std::to_string(sr) + " mode=" + mode + " st_flags=" + flags +
						   " UF_IMMUTABLE=" + SdFlag((st.st_flags & UF_IMMUTABLE) != 0);
		line += " st_uid=" + std::to_string(st.st_uid);
		line += " geteuid=" + std::to_string(::geteuid());
		line += " S_ISVTX=" + SdFlag((st.st_mode & S_ISVTX) != 0);
		if (S_ISDIR(st.st_mode))
		{
			const std::string temp = path + "/.sd-probe-write-test";
			errno = 0;
			const int fd = ::open(temp.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
			line += " 一時ファイルを作る=" + (fd >= 0 ? std::string("ok") : SdErrno(errno));
			if (fd >= 0)
			{
				::close(fd);
				::unlink(temp.c_str());
			}
		}
		else
		{
			errno = 0;
			const int fd = ::open(path.c_str(), O_WRONLY);
			line += " open(O_WRONLY)=" + (fd >= 0 ? std::string("ok") : SdErrno(errno));
			if (fd >= 0)
				::close(fd);
		}
		probe.log(tag + ": " + line);
	}

	std::string SdShell(const std::string& command, int& exitCode)
	{
		std::string out;
		const std::string full = command + " 2>&1";
		FILE* pipe = ::popen(full.c_str(), "r");
		if (pipe == nullptr)
		{
			exitCode = -1;
			return "popen に失敗";
		}
		char buf[4096];
		while (std::fgets(buf, sizeof buf, pipe) != nullptr)
			out += buf;
		exitCode = ::pclose(pipe);
		return out;
	}
#else
	std::string SdWinErr(DWORD e)
	{
		return e == 0 ? std::string("ok") : "err=" + std::to_string(e);
	}

	std::string SdCreateFile(const std::wstring& path, DWORD access, DWORD share, DWORD disposition,
							 DWORD flags)
	{
		HANDLE h = ::CreateFileW(path.c_str(), access, share, nullptr, disposition, flags, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return SdWinErr(::GetLastError());
		::CloseHandle(h);
		return "ok";
	}

	// プロセスのトークンで、その ACL が desired を許すか。共有違反に左右されない。
	std::string SdAccessCheck(const std::wstring& path, DWORD desired)
	{
		const DWORD who = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION;
		const SECURITY_INFORMATION info = who | DACL_SECURITY_INFORMATION;
		DWORD need = 0;
		::GetFileSecurityW(path.c_str(), info, nullptr, 0, &need);
		if (need == 0)
			return "GetFileSecurity " + SdWinErr(::GetLastError());
		std::vector<char> descriptor(need);
		if (!::GetFileSecurityW(path.c_str(), info, descriptor.data(), need, &need))
			return "GetFileSecurity " + SdWinErr(::GetLastError());
		HANDLE process = nullptr;
		HANDLE token = nullptr;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process))
			return "OpenProcessToken " + SdWinErr(::GetLastError());
		const BOOL dup = ::DuplicateToken(process, SecurityImpersonation, &token);
		::CloseHandle(process);
		if (!dup)
			return "DuplicateToken " + SdWinErr(::GetLastError());
		GENERIC_MAPPING mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE,
								   FILE_ALL_ACCESS};
		DWORD mask = desired;
		::MapGenericMask(&mask, &mapping);
		PRIVILEGE_SET privileges = {};
		DWORD privilegesSize = sizeof privileges;
		DWORD granted = 0;
		BOOL allowed = FALSE;
		const BOOL ok = ::AccessCheck(descriptor.data(), token, mask, &mapping, &privileges,
									  &privilegesSize, &granted, &allowed);
		const DWORD err = ::GetLastError();
		::CloseHandle(token);
		if (!ok)
			return "AccessCheck " + SdWinErr(err);
		return allowed ? "許す" : "拒む";
	}

	void SdCheckOne(vwprobe::Report& probe, const std::string& tag, const std::string& label,
					const std::string& path)
	{
		const std::wstring wide = SdPath(path).wstring();
		const DWORD attr = ::GetFileAttributesW(wide.c_str());
		if (attr == INVALID_FILE_ATTRIBUTES)
		{
			probe.log(tag + ": " + label + ": GetFileAttributesW " + SdWinErr(::GetLastError()));
			return;
		}
		char hex[32];
		std::snprintf(hex, sizeof hex, "0x%x", static_cast<unsigned int>(attr));
		const bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
		const DWORD shareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
		std::string line = label + ": attr=" + hex;
		line += " READONLY=" + SdFlag((attr & FILE_ATTRIBUTE_READONLY) != 0);
		line += " REPARSE=" + SdFlag((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
		errno = 0;
		const int acc = ::_waccess(wide.c_str(), 2);
		line += " _waccess(W)=" + std::to_string(acc) + " errno=" + std::to_string(errno);
		if (isDir)
		{
			const DWORD backup = FILE_FLAG_BACKUP_SEMANTICS;
			const DWORD existing = OPEN_EXISTING;
			const std::string add = SdCreateFile(wide, FILE_ADD_FILE, shareAll, existing, backup);
			line += " CreateFileW(FILE_ADD_FILE)=" + add;
			line += " ACL(FILE_ADD_FILE)=" + SdAccessCheck(wide, FILE_ADD_FILE);
			line += " ACL(FILE_DELETE_CHILD)=" + SdAccessCheck(wide, FILE_DELETE_CHILD);
			const std::wstring temp = wide + L"\\~sd-probe-write-test";
			const DWORD gone = FILE_FLAG_DELETE_ON_CLOSE;
			const std::string made = SdCreateFile(temp, GENERIC_WRITE, 0, CREATE_NEW, gone);
			line += " 一時ファイルを作る=" + made;
		}
		else
		{
			line += " CreateFileW(GENERIC_WRITE・共有すべて)=" +
					SdCreateFile(wide, GENERIC_WRITE, shareAll, OPEN_EXISTING, 0);
			line += " ACL(FILE_GENERIC_WRITE)=" + SdAccessCheck(wide, FILE_GENERIC_WRITE);
			line += " ACL(DELETE)=" + SdAccessCheck(wide, DELETE);
			const std::string del = SdCreateFile(wide, DELETE, shareAll, OPEN_EXISTING, 0);
			line += " CreateFileW(DELETE・共有すべて)=" + del;
		}
		probe.log(tag + ": " + line);
	}

	std::string SdShell(const std::string& command, int& exitCode)
	{
		SECURITY_ATTRIBUTES sa = {};
		sa.nLength = sizeof sa;
		sa.bInheritHandle = TRUE;
		HANDLE readEnd = nullptr;
		HANDLE writeEnd = nullptr;
		exitCode = -1;
		if (!::CreatePipe(&readEnd, &writeEnd, &sa, 0))
			return "CreatePipe に失敗";
		::SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW si = {};
		si.cb = sizeof si;
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = writeEnd;
		si.hStdError = writeEnd;
		PROCESS_INFORMATION pi = {};
		std::wstring line = L"cmd.exe /d /c " + SdPath(command).wstring();
		const BOOL started = ::CreateProcessW(nullptr, &line[0], nullptr, nullptr, TRUE,
											  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
		::CloseHandle(writeEnd);
		std::string raw;
		if (started)
		{
			char buf[4096];
			DWORD got = 0;
			while (::ReadFile(readEnd, buf, sizeof buf, &got, nullptr) && got > 0)
				raw.append(buf, got);
			::WaitForSingleObject(pi.hProcess, INFINITE);
			DWORD code = 0;
			::GetExitCodeProcess(pi.hProcess, &code);
			exitCode = static_cast<int>(code);
			::CloseHandle(pi.hProcess);
			::CloseHandle(pi.hThread);
		}
		::CloseHandle(readEnd);
		if (!started)
			return "CreateProcessW に失敗";
		const int n = ::MultiByteToWideChar(CP_OEMCP, 0, raw.data(), static_cast<int>(raw.size()),
											nullptr, 0);
		std::wstring wide(static_cast<std::size_t>(n), L'\0');
		::MultiByteToWideChar(CP_OEMCP, 0, raw.data(), static_cast<int>(raw.size()), &wide[0], n);
		return SdWide(wide.c_str());
	}
#endif

	// リンクとジャンクションを OS が解決した先。開いている図面でも聞ける（アクセス権を求めない）。
	void SdFinal(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
#if GS_MAC
		char resolved[PATH_MAX] = {0};
		errno = 0;
		if (::realpath(path.c_str(), resolved) == nullptr)
			probe.log(tag + ": realpath " + SdErrno(errno));
		else
			probe.log(tag + ": realpath=" + resolved);
#else
		const std::wstring wide = SdPath(path).wstring();
		const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
		const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
		HANDLE h = ::CreateFileW(wide.c_str(), 0, share, nullptr, OPEN_EXISTING, flags, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			probe.log(tag + ": CreateFileW " + SdWinErr(::GetLastError()));
			return;
		}
		wchar_t resolved[2048] = {0};
		const DWORD n = ::GetFinalPathNameByHandleW(h, resolved, 2048, 0);
		const DWORD err = ::GetLastError();
		::CloseHandle(h);
		if (n == 0)
			probe.log(tag + ": GetFinalPathNameByHandleW " + SdWinErr(err));
		else
			probe.log(tag + ": GetFinalPathNameByHandleW=" + SdWide(resolved));
#endif
	}

	// Win32 が字面で正規化した結果。ファイルシステムは見ない。
	void SdFullPath(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
#if GS_MAC
		probe.log(tag + ": Windows だけ: " + path);
#else
		const std::wstring wide = SdPath(path).wstring();
		wchar_t full[2048] = {0};
		const DWORD n = ::GetFullPathNameW(wide.c_str(), 2048, full, nullptr);
		if (n == 0)
			probe.log(tag + ": GetFullPathNameW " + SdWinErr(::GetLastError()));
		else
			probe.log(tag + ": GetFullPathNameW=[" + SdWide(full) + "]");
#endif
	}

	void SdOsCheck(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		SdCheckOne(probe, tag, "ファイル", path);
		const std::string parent = SdParent(path);
		if (!parent.empty())
			SdCheckOne(probe, tag, "親", parent);
	}

	void SdSh(vwprobe::Report& probe, const std::string& tag, const std::string& command)
	{
		int exitCode = 0;
		const std::string out = SdShell(command, exitCode);
		probe.log(tag + ": 終了コード=" + std::to_string(exitCode));
		std::size_t from = 0;
		while (from < out.size())
		{
			std::size_t to = out.find('\n', from);
			if (to == std::string::npos)
				to = out.size();
			std::string one = out.substr(from, to - from);
			if (!one.empty() && one.back() == '\r')
				one.pop_back();
			if (!one.empty())
				probe.log(tag + ":   " + one);
			from = to + 1;
		}
	}

	void SdClose(vwprobe::Report& probe, const std::string& tag)
	{
		const std::size_t before = SdDocs().size();
		SdWatchScope watch(probe, tag);
		const short r = gSDK->DoMenuName("Close", 0);
		watch.report("DoMenuName(Close)=" + std::to_string(r) + " 件数 " + std::to_string(before) +
					 " -> " + std::to_string(SdDocs().size()) + " 前面=" + SdActivePath());
	}

	// メニューのコマンドを名前で起こす（Export MiniCAD File など）。press none で人に任せる。
	void SdDoMenu(vwprobe::Report& probe, const std::string& tag, const std::string& name)
	{
		SdWatchScope watch(probe, tag);
		const short r = gSDK->DoMenuName(name.c_str(), 0);
		watch.report("DoMenuName=" + std::to_string(r) + " 前面=" + SdActivePath());
	}

	// 保存先がどこになったかを見るための再帰の一覧。
	void SdTree(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		std::error_code ec;
		sdfs::recursive_directory_iterator it(SdPath(path), ec);
		if (ec)
		{
			probe.log(tag + ": 読めない: " + ec.message());
			return;
		}
		int n = 0;
		for (const sdfs::directory_entry& e : it)
		{
			std::error_code sizeEc;
			const bool isDir = e.is_directory(sizeEc);
			const long long size = isDir ? -1 : static_cast<long long>(e.file_size(sizeEc));
			probe.log(tag + ":   " + (isDir ? std::string("[dir] ") : std::to_string(size) + " ") +
					  SdUtf8(e.path()));
			++n;
		}
		probe.log(tag + ": 件数=" + std::to_string(n));
	}

	std::string SdExpand(std::string s, const std::string& work)
	{
		const std::string mark = "$T";
		for (std::size_t at = s.find(mark); at != std::string::npos; at = s.find(mark, at))
		{
			s.replace(at, mark.size(), work);
			at += work.size();
		}
		return s;
	}

	void SdRun(vwprobe::Report& probe, const std::string& line, const std::string& work, int no)
	{
		const std::size_t space = line.find(' ');
		const std::string verb = line.substr(0, space);
		const std::string arg =
			space == std::string::npos ? std::string() : SdExpand(line.substr(space + 1), work);
		const std::string tag = std::to_string(no) + " " + verb;
		probe.log("--- " + std::to_string(no) + ": " + line);
		std::error_code ec;
		if (verb == "new")
			SdNew(probe, tag);
		else if (verb == "open")
			SdOpen(probe, tag, arg);
		else if (verb == "openv")
			SdOpenLoud(probe, tag, arg);
		else if (verb == "switch")
			SdSwitch(probe, tag, arg);
		else if (verb == "closefront")
			probe.log(tag + ": CloseDocument=" + SdFlag(gSDK->CloseDocument()) +
					  " 前面=" + SdActivePath());
		else if (verb == "docs")
			SdLogDocs(probe, tag);
		else if (verb == "saveto")
			SdSaveTo(probe, tag, arg);
		else if (verb == "save")
			SdSaveTo(probe, tag, SdActivePath());
		else if (verb == "rect")
			SdRect(probe, tag, arg);
		else if (verb == "event")
			SdEvent(probe, tag, arg);
		else if (verb == "ev-story")
			SdStoryEvent(probe, tag, arg);
		else if (verb == "ev-layer")
			SdLayerEvent(probe, tag, arg);
		else if (verb == "ev-member")
			SdMemberEvent(probe, tag, arg);
		else if (verb == "find")
			SdFind(probe, tag, arg);
		else if (verb == "list")
			SdList(probe, tag);
		else if (verb == "doundo")
			SdDoUndo(probe, tag);
		else if (verb == "menu")
			SdLogUndoMenu(probe);
		else if (verb == "mkdir")
			probe.log(tag + ": " + SdFlag(sdfs::create_directories(SdPath(arg), ec)));
		else if (verb == "mv")
			SdMove(probe, tag, arg);
		else if (verb == "rm")
			probe.log(tag + ": " + SdFlag(sdfs::remove(SdPath(arg), ec)));
		else if (verb == "ro")
			SdSetWritable(probe, tag, arg, false);
		else if (verb == "rw")
			SdSetWritable(probe, tag, arg, true);
		else if (verb == "write")
			std::ofstream(SdPath(arg), std::ios::binary) << "dialog-external\n";
		else if (verb == "fs")
			SdLogFile(probe, tag, arg);
		else if (verb == "ls")
			SdLs(probe, tag, arg);
		else if (verb == "oscheck")
			SdOsCheck(probe, tag, arg);
		else if (verb == "sh")
			SdSh(probe, tag, arg);
		else if (verb == "final")
			SdFinal(probe, tag, arg);
		else if (verb == "fullpath")
			SdFullPath(probe, tag, arg);
		else if (verb == "domenu")
			SdDoMenu(probe, tag, arg);
		else if (verb == "close")
			SdClose(probe, tag);
		else if (verb == "tree")
			SdTree(probe, tag, arg);
		else if (verb == "cwd")
			probe.log(tag + ": " + SdUtf8(sdfs::current_path(ec)));
		else if (verb == "note")
			probe.log("=== " + arg);
		else if (verb == "press")
		{
			gSdAutoPress = arg != "none";
			const bool later = arg.rfind("after ", 0) == 0;
			gSdPressAfterMs = later ? std::atoll(arg.c_str() + 6) * 1000 : 1000;
		}
		else if (verb == "nest")
			gSdNestPath = arg;
		else if (verb == "busy")
		{
			probe.log(tag + ": メインスレッドを塞ぐ " + arg + " 秒");
			const auto t0 = std::chrono::steady_clock::now();
			while (SdMs(t0) < std::atoll(arg.c_str()) * 1000)
				;
			probe.log(tag + ": 塞ぎ終えた");
		}
		else
			probe.fail(tag + ": 知らない動詞");
	}
} // namespace

VW_PROBE("dialog-external", "別プロセスから押すダイアログ", "指示ファイルで出す")
{
	std::error_code ec;
	const sdfs::path tmp = sdfs::temp_directory_path(ec);
	const sdfs::path doFile = tmp / "VwSdkProbes-dialog-external.do";
	const sdfs::path work = tmp / "VwSdkProbes-dialog-external";
	sdfs::create_directories(work, ec);
	probe.log("指示ファイル: " + SdUtf8(doFile));
	probe.log("作業用フォルダ（$T）: " + SdUtf8(work));

	std::ifstream in(doFile);
	if (!in)
	{
		probe.log("指示ファイルが無いので、図面の一覧だけ出す");
		SdLogDocs(probe, "docs");
		return;
	}
	std::vector<std::string> lines;
	for (std::string line; std::getline(in, line);)
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (!line.empty() && line[0] != '#')
			lines.push_back(line);
	}
	in.close();
	sdfs::remove(doFile, ec);

	gSdAutoPress = true;
	gSdPressAfterMs = 90000;
	gSdNestPath.clear();
	gSdInNest = false;
	gSdMarker = SdUtf8(work / "in-call.txt");
	int no = 0;
	for (const std::string& line : lines)
		SdRun(probe, line, SdUtf8(work), ++no);
}
