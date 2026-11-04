//
//	probes/runtime/save-dialogs/probe.cpp
//
//	[issue #288] 上書き保存（SaveActiveDocumentPath で自分のパスへ）がダイアログを出す場面と、
//	保存先の文字列の違い・取り消しの段を、**指示ファイル**に書いた手順で測る。手順を変えるたびに
//	ビルドし直さなくてよいように、プローブ自身は動詞を持つだけにしてある。
//
//	一時ディレクトリの VwSdkProbes-save-dialogs.do を 1 行ずつ行う（読んだら消す）。空行と
//	# で始まる行は飛ばす。パスの中の $T は作業用フォルダ（一時ディレクトリの
//	VwSdkProbes-save-dialogs。無ければ作る）に置き換わる。
//
//	  図面   new / open <P> / switch <P> / closefront / docs
//	  保存   saveto <P>（前面の図面をそのパスへ）/ save（前面の図面を、一覧のパスの文字列のまま）
//	  図形   rect <名前>（undo イベントの外）/ event <名前>（自分で開いて閉じる）/ list
//	  取消   doundo（DoMenuName("Undo", 0)）/ menu（「取り消し」の項目の文言）
//	  ファイル  mkdir <P> / mv <A> -> <B> / rm <P> / ro <P> / rw <P> / write <P> / fs <P> / ls <P>
//	  見張り  press auto|none
//
//	**ダイアログは人を待たない。** 保存・開く・取り消しの呼び出しの間だけ「見張り」のタイマー
//	（mac は共通モードの CFRunLoopTimer、Windows は SetTimer）を仕掛け、新しく出た窓の文言を
//	ログへ書き、1 秒見せてから「キャンセル」か「OK」を押す。どちらも無ければ 30 秒待って
//	最後のボタンを押す（作業用の図面しか相手にしないこと）。press none なら押さず人に任せる。
//	見張りは呼び出しから戻る前に必ず外す（戻った後に本体の関数を呼ばせない）。
//
//	event の動詞だけは、取り消しで何が戻るかが問いなので、undo イベントを自分で開いて閉じる
//	（probes/runtime/README.md の例外）。
//

#include "Probe.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#	include <objc/message.h>
#	include <objc/runtime.h>
#else
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
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
		const auto when = sdfs::last_write_time(p, ec).time_since_epoch().count();
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
		void* dialog = nullptr;
		long long seenAt = -1;
		long long ticks = 0;
		int presses = 0;
	};

	SdWatch* gSdWatch = nullptr;
	bool gSdAutoPress = true;

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
		if (w == nullptr)
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
		if (!gSdAutoPress || w->dialog == nullptr || now - w->seenAt < 1000)
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
		if (!SdPick(SdControlsOf(w->dialog), now - w->seenAt >= 30000, button))
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
			std::ofstream(SdPath(arg), std::ios::binary) << "save-dialogs\n";
		else if (verb == "fs")
			SdLogFile(probe, tag, arg);
		else if (verb == "ls")
			SdLs(probe, tag, arg);
		else if (verb == "press")
			gSdAutoPress = arg != "none";
		else
			probe.fail(tag + ": 知らない動詞");
	}
} // namespace

VW_PROBE("save-dialogs", "保存のダイアログと取り消し", "指示ファイルの手順で測る")
{
	std::error_code ec;
	const sdfs::path tmp = sdfs::temp_directory_path(ec);
	const sdfs::path doFile = tmp / "VwSdkProbes-save-dialogs.do";
	const sdfs::path work = tmp / "VwSdkProbes-save-dialogs";
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
	int no = 0;
	for (const std::string& line : lines)
		SdRun(probe, line, SdUtf8(work), ++no);
}
