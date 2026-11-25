//
//	probes/runtime/ghost-close/probe.cpp
//
//	[issue #323] CloseDocument() で閉じた図面について、あとの終了が保存の確認を出す条件と、
//	避ける手立てを測る。土台は #321 の ghost-close。
//
//	一時ディレクトリの VwSdkProbes-ghost-close.do を 1 行ずつ行う（読んだら消す）。# は注釈。
//	図面は new / open で付けた名前で指す（before = 走り出しに前面だった図面）。
//	  new <名> / open <名> <P> / front <名> / close（前面を CloseDocument）/ closemenu
//	  drop <名>（front → close）/ saveto <P> / save / docs / active / mark（MarkCurrentDocChanged）
//	  rect（イベントの外）/ event（イベントの中）/ layer（イベントの中）/ undo
//	  clearundo / endremove / pump <ms>（OS のイベントも配る）
//	  peek（閉じた図面が別の口から見えるか）/ quit ask|force / quitmenu（mac）
//	  press cancel|discard|save|none（見張りが確認で押すもの）/ note <文>
//	  try <名>（閉じた図面の fileRef へ切り替える）。$T は作業用フォルダ。
//	  find <名> <パスの一部>（前の実行で開いた図面に名前を付ける）/ count（カレントレイヤの図形の数）
//	  stat <P>（大きさと更新時刻）
//
//	見張りは呼び出しの間だけ仕掛け、戻る前に必ず外す。undo イベントは変更の種類が問いなので
//	自分で開いて閉じる（probes/runtime/README.md の例外）。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/IApplication.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
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
					  " inMemoryOnly=" + SdFlag(d.inMemoryOnly) + " path=" + d.path);
		}
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

	long long SdMs(std::chrono::steady_clock::time_point t0)
	{
		const auto d = std::chrono::steady_clock::now() - t0;
		return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
	}

	std::string gGcPress = "cancel"; // cancel / discard / save / none

	bool SdPick(const std::vector<SdControl>& controls, bool lastResort, SdControl& out)
	{
		std::vector<std::string> wants = {"キャンセル", "Cancel", "OK"};
		if (gGcPress == "discard")
			wants = {"保存しない", "いいえ", "Don", "&No"};
		else if (gGcPress == "save")
			wants = {"保存", "はい", "Save", "&Yes"};
		for (const std::string& want : wants)
		{
			for (const SdControl& c : controls)
			{
				if (!c.isButton || c.text.find(want) == std::string::npos)
					continue;
				if (gGcPress == "save" && c.text.find("しない") != std::string::npos)
					continue;
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
			bool hasButton = false;
			for (const SdControl& c : SdControlsOf(h))
				hasButton = hasButton || c.isButton;
			// 確認と一緒に飾りの窓（ボタンなし）も出る。押す相手はボタンのある窓だけ。
			if (hasButton)
			{
				w->dialog = h;
				w->seenAt = now;
			}
			w->probe->log(w->tag + ":   [" + std::to_string(now) + "ms] 新しい窓 " +
						  SdWindowText(h));
			for (const SdControl& c : SdControlsOf(h))
			{
				w->probe->log(w->tag + ":     " + (c.isButton ? "ボタン" : "文字") + " class=" +
							  c.cls + " id=" + std::to_string(c.id) + " text=\"" + c.text + "\"");
			}
		}
		if (gGcPress == "none" || w->dialog == nullptr || now - w->seenAt < 1000)
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
		if (!SdPick(SdControlsOf(w->dialog), now - w->seenAt >= 20000, button))
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

	struct GcDoc
	{
		Sint32 ref = 0;
		MCObjectHandle layer = nullptr;
	};

	std::map<std::string, GcDoc> gGcDocs;

	Sint32 GcActiveRef()
	{
		for (const SdDoc& d : SdDocs())
		{
			if (d.active)
				return d.ref;
		}
		return 0;
	}

	void GcRemember(vwprobe::Report& probe, const std::string& name)
	{
		GcDoc d;
		d.ref = GcActiveRef();
		d.layer = gSDK->GetCurrentLayer();
		gGcDocs[name] = d;
		probe.log("  " + name + " = fileRef " + std::to_string(d.ref) + " " + SdActivePath());
	}

	void GcFront(vwprobe::Report& probe, const std::string& name)
	{
		const bool ok = gSDK->SwitchToOpenFile(gGcDocs[name].ref);
		probe.log("  SwitchToOpenFile(" + name + ")=" + SdFlag(ok) + " 前面=" + SdActivePath());
	}

	// 前面の図面の口が何を返すか（1 行）。
	void GcActive(vwprobe::Report& probe)
	{
		bool saved = false;
		Filing::IFileIdentifierPtr active(Filing::IID_FileIdentifier);
		const bool got = gSDK->GetActiveDocument(&active, saved);
		probe.log("  active: 件数=" + std::to_string(SdDocs().size()) +
				  " GetActiveDocument=" + SdFlag(got) + " outSaved=" + SdFlag(saved) +
				  " path=" + (got ? SdPathOf(active) : std::string("-")) +
				  " GetCurrentLayer=" + SdFlag(gSDK->GetCurrentLayer() != nullptr));
	}

	void GcClose(vwprobe::Report& probe, bool menu)
	{
		const std::size_t before = SdDocs().size();
		const std::string path = SdActivePath();
		SdWatchScope watch(probe, "close");
		long r = 0;
		if (menu)
			r = gSDK->DoMenuName("Close", 0);
		else
			r = gSDK->CloseDocument() ? 1 : 0;
		watch.report(std::string(menu ? "DoMenuName(Close)=" : "CloseDocument=") +
					 std::to_string(r) + " " + path + " 件数 " + std::to_string(before) + " -> " +
					 std::to_string(SdDocs().size()) + " 前面=" + SdActivePath());
		GcActive(probe);
	}

	void GcSave(vwprobe::Report& probe, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(path.c_str()));
		SdWatchScope watch(probe, "save");
		const GSError err = gSDK->SaveActiveDocumentPath(fileID);
		GcActive(probe);
		watch.report("GSError=" + std::to_string(static_cast<long long>(err)) + " " + path);
	}

	void GcOpen(vwprobe::Report& probe, const std::string& path)
	{
		SdWatchScope watch(probe, "open");
		bool ok = false;
		if (path.empty())
			ok = gSDK->OpenDocumentPath(nullptr, false);
		else
		{
			Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
			fileID->Set(TXString(path.c_str()));
			ok = gSDK->OpenDocumentPath(fileID, false);
		}
		watch.report("OpenDocumentPath=" + SdFlag(ok));
	}

	void GcEvent(vwprobe::Report& probe, const std::string& kind)
	{
		MCObjectHandle h = nullptr;
		const bool inEvent = kind != "rect";
		if (inEvent)
		{
			gSDK->SetUndoMethod(kUndoSwapObjects);
			gSDK->NameUndoEvent("ghost");
		}
		if (kind == "layer")
			h = gSDK->CreateLayer("ghost layer", kLayerDesign);
		else
			h = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		if (inEvent && h != nullptr)
			gSDK->AddAfterSwapObject(h);
		const Boolean closed = gSDK->EndUndoEvent();
		probe.log("  " + kind + ": 作れた=" + SdFlag(h != nullptr) +
				  " EndUndoEvent=" + SdFlag(closed != 0));
	}

	void GcPeek(vwprobe::Report& probe)
	{
		SdLogDocs(probe, "peek");
		IApplicationPtr app(IID_Application);
		if (app)
			probe.log("peek: IApplication::GetDocumentsCount=" +
					  std::to_string(app->GetDocumentsCount()));
		else
			probe.log("peek: IApplication が取れない");
		bool saved = false;
		Filing::IFileIdentifierPtr active(Filing::IID_FileIdentifier);
		const bool got = gSDK->GetActiveDocument(&active, saved);
		probe.log("peek: GetActiveDocument=" + SdFlag(got) + " outSaved=" + SdFlag(saved) +
				  " path=" + (got ? SdPathOf(active) : std::string("-")));
		probe.log("peek: GetCurrentLayer=" + SdFlag(gSDK->GetCurrentLayer() != nullptr) +
				  " IsDocumentReady=" + SdFlag(gSDK->IsDocumentReady()));
#if GS_MAC
		id windows = SdSend(SdApp(), "windows");
		const long n = SdSendLong(windows, "count");
		for (long i = 0; i < n; ++i)
		{
			id w = SdAt(windows, i);
			const std::string title = SdNsString(SdSend(w, "title"));
			if (title.empty())
				continue;
			probe.log("peek: 窓 " + SdWindowText(w) +
					  " 見える=" + SdFlag(SdSendBool(w, "isVisible")) +
					  " 編集済み=" + SdFlag(SdSendBool(w, "isDocumentEdited")));
		}
#else
		for (void* w : SdTopWindows())
			probe.log("peek: 窓 " + SdWindowText(w));
#endif
	}

	// 閉じた図面の fileRef へ切り替えられるか（前面を動かしうるので peek と分ける）。
	void GcTry(vwprobe::Report& probe, const std::string& name)
	{
		const bool ok = gSDK->SwitchToOpenFile(gGcDocs[name].ref);
		probe.log("try: SwitchToOpenFile(閉じた " + name +
				  " ref=" + std::to_string(gGcDocs[name].ref) + ")=" + SdFlag(ok));
		SdLogDocs(probe, "try");
	}

	void GcPump(vwprobe::Report& probe, int ms)
	{
		const auto t0 = std::chrono::steady_clock::now();
		while (SdMs(t0) < ms)
		{
#if GS_MAC
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, false);
			using NextEvent = id (*)(id, SEL, unsigned long long, id, id, signed char);
			id ev = reinterpret_cast<NextEvent>(objc_msgSend)(
				SdApp(), sel_registerName("nextEventMatchingMask:untilDate:inMode:dequeue:"), ~0ULL,
				nullptr,
				reinterpret_cast<id>(
					const_cast<void*>(static_cast<const void*>(kCFRunLoopDefaultMode))),
				1);
			if (ev != nullptr)
				reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
					SdApp(), sel_registerName("sendEvent:"), ev);
#else
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}
			::Sleep(20);
#endif
		}
		probe.log("  回した " + std::to_string(SdMs(t0)) + "ms");
	}

	void GcQuit(vwprobe::Report& probe, bool ask)
	{
		SdWatchScope watch(probe, "quit");
		gSDK->CloseAllFilesAndQuitVectorworks(ask, false);
		watch.report(std::string("CloseAllFilesAndQuitVectorworks(") + (ask ? "true" : "false") +
					 ") 戻った");
	}

#if GS_MAC
	// メニューの「終了」（⌘Q の項目）を押したのと同じこと。
	bool GcPerformQuit(id menu)
	{
		id items = SdSend(menu, "itemArray");
		const long n = SdSendLong(items, "count");
		for (long i = 0; i < n; ++i)
		{
			id item = SdAt(items, i);
			if (SdNsString(SdSend(item, "keyEquivalent")) == "q")
			{
				reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend)(
					menu, sel_registerName("performActionForItemAtIndex:"), i);
				return true;
			}
			id sub = SdSend(item, "submenu");
			if (sub != nullptr && GcPerformQuit(sub))
				return true;
		}
		return false;
	}
#endif

	void GcQuitMenu(vwprobe::Report& probe)
	{
#if GS_MAC
		SdWatchScope watch(probe, "quitmenu");
		const bool found = GcPerformQuit(SdSend(SdApp(), "mainMenu"));
		GcPump(probe, 6000);
		watch.report("終了の項目=" + SdFlag(found));
#else
		probe.log("quitmenu: mac だけ");
#endif
	}

	// パスの一部で、開いている図面に名前を付ける。
	void GcFind(vwprobe::Report& probe, const std::string& name, const std::string& part)
	{
		for (const SdDoc& d : SdDocs())
		{
			if (d.path.find(part) == std::string::npos)
				continue;
			GcDoc g;
			g.ref = d.ref;
			gGcDocs[name] = g;
			probe.log("  " + name + " = fileRef " + std::to_string(d.ref) + " " + d.path);
			return;
		}
		probe.fail("見つからない: " + part);
	}

	void GcCount(vwprobe::Report& probe)
	{
		long n = 0;
		MCObjectHandle layer = gSDK->GetCurrentLayer();
		if (layer != nullptr)
		{
			MCObjectHandle h = gSDK->FirstMemberObj(layer);
			for (; h != nullptr; h = gSDK->NextObject(h))
				++n;
		}
		probe.log("  図形の数=" + std::to_string(n) + " 前面=" + SdActivePath());
	}

	void GcStat(vwprobe::Report& probe, const std::string& path)
	{
		std::error_code ec;
		const auto size = sdfs::file_size(SdPath(path), ec);
		const auto t = sdfs::last_write_time(SdPath(path), ec);
		if (ec)
		{
			probe.log("  stat 無い " + path);
			return;
		}
		const std::string when = std::to_string(t.time_since_epoch().count());
		probe.log("  stat 大きさ=" + std::to_string(size) + " 更新=" + when + " " + path);
	}

	void GcRun(vwprobe::Report& probe, const std::string& line, const std::string& work)
	{
		const std::size_t sp = line.find(' ');
		const std::string verb = line.substr(0, sp);
		std::string arg = sp == std::string::npos ? std::string() : line.substr(sp + 1);
		const std::size_t at = arg.find("$T");
		if (at != std::string::npos)
			arg.replace(at, 2, work);
		probe.log("--- " + line);
		if (verb == "new")
		{
			GcOpen(probe, std::string());
			GcRemember(probe, arg);
		}
		else if (verb == "open")
		{
			const std::size_t s2 = arg.find(' ');
			GcOpen(probe, arg.substr(s2 + 1));
			GcRemember(probe, arg.substr(0, s2));
		}
		else if (verb == "front")
			GcFront(probe, arg);
		else if (verb == "close" || verb == "closemenu")
			GcClose(probe, verb == "closemenu");
		else if (verb == "drop")
		{
			GcFront(probe, arg);
			GcClose(probe, false);
		}
		else if (verb == "saveto")
			GcSave(probe, arg);
		else if (verb == "save")
			GcSave(probe, SdActivePath());
		else if (verb == "active")
			GcActive(probe);
		else if (verb == "docs")
			SdLogDocs(probe, "docs");
		else if (verb == "mark")
			gSDK->MarkCurrentDocChanged();
		else if (verb == "rect" || verb == "event" || verb == "layer")
			GcEvent(probe, verb);
		else if (verb == "undo")
			probe.log("  Undo=" + std::to_string(gSDK->DoMenuName("Undo", 0)));
		else if (verb == "clearundo")
			gSDK->ClearUndoTableDueToUnsupportedAction();
		else if (verb == "endremove")
			probe.log("  EndAndRemoveUndoEvent=" + SdFlag(gSDK->EndAndRemoveUndoEvent() != 0));
		else if (verb == "pump")
			GcPump(probe, std::atoi(arg.c_str()));
		else if (verb == "peek")
			GcPeek(probe);
		else if (verb == "find")
		{
			const std::size_t s2 = arg.find(' ');
			GcFind(probe, arg.substr(0, s2), arg.substr(s2 + 1));
		}
		else if (verb == "count")
			GcCount(probe);
		else if (verb == "stat")
			GcStat(probe, arg);
		else if (verb == "try")
			GcTry(probe, arg);
		else if (verb == "quit")
			GcQuit(probe, arg != "force");
		else if (verb == "quitmenu")
			GcQuitMenu(probe);
		else if (verb == "press")
			gGcPress = arg;
		else if (verb != "note")
			probe.fail("知らない動詞: " + verb);
	}
} // namespace

VW_PROBE("ghost-close", "閉じた図面の終了の確認", "指示ファイルの手順で測る")
{
	std::error_code ec;
	const sdfs::path tmp = sdfs::temp_directory_path(ec);
	const sdfs::path doFile = tmp / "VwSdkProbes-ghost-close.do";
	const sdfs::path work = tmp / "VwSdkProbes-ghost-close";
	sdfs::create_directories(work, ec);
	std::ifstream in(doFile);
	if (!in)
	{
		probe.log("指示ファイルが無い: " + SdUtf8(doFile));
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
	gGcDocs.clear();
	gGcPress = "cancel";
	GcRemember(probe, "before");
	for (const std::string& line : lines)
		GcRun(probe, line, SdUtf8(work));
}
