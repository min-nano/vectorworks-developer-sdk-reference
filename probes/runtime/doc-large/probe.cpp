//
//	probes/runtime/doc-large/probe.cpp
//
//	[issue #290] 大きな図面の「閉じる」と保存が呼び出しの中で終わるか・その間イベントを回すか・
//	どれだけかかるか・応答なしになるかを、**指示ファイル**に書いた手順で測る。
//
//	一時ディレクトリの VwSdkProbes-doc-large.do を 1 行ずつ行う（読んだら消す）。空行と # で
//	始まる行は飛ばす。パスの中の $T は作業用フォルダ（一時ディレクトリの VwSdkProbes-doc-large）。
//
//	  図面   new / open <P> / switch <P> / docs / count
//	  保存   saveto <P>（前面の図面をそのパスへ）/ save（前面の図面を、自分のパスへ）
//	  閉じる close（DoMenuName("Close", 0)。前面の図面）
//	  合成   rects <件数> / polys <件数> <頂点数> / syms <定義数> <配置数> / vps <枚数> <描画種別>
//	  ファイル copy <A> -> <B> / rm <P> / fs <P>
//	  その他 follow <ms>（保存・閉じるの後に回す長さ。既定 3000）/ pump <ms> / block <ms>
//	         （メインスレッドを止める。応答なしの検出の対照）/ poke on|off（mac: 測っている間
//	         自分へマウス移動を送る）/ press auto|none
//
//	測る呼び出しの間だけ、次を仕掛ける（戻る前に必ず外す）。
//	  - タイマー 100 ms: mac は共通モードと既定のモードの CFRunLoopTimer を 1 本ずつ、Windows は
//	    SetTimer。**呼び出しの最中に刻んだ回数がそのまま「入り直し」である**（プローブは呼び出しの
//	    外でランループを回していない）。
//	  - 見張りのスレッド: 対象のファイルの大きさ・更新時刻、同じフォルダの出入り、応答なしの判定
//	    （mac は WindowServer の CGSEventIsAppUnresponsive、Windows は IsHungAppWindow と、
//	    .NET の Process.Responding と同じ SendMessageTimeout(WM_NULL, 5 秒)）。
//	ダイアログが出たら save-dialogs と同じく文言を書き、1 秒見せてからキャンセルか OK を押す。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWViewportObj.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#	include <dlfcn.h>
#	include <objc/message.h>
#	include <objc/runtime.h>
#	include <unistd.h>
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
		std::vector<long long> tickAt;	  // 共通モード（mac）/ SetTimer（Windows）
		std::vector<long long> defaultAt; // 既定のモード（mac のみ）
		std::set<std::string> modes; // 刻んだときのランループのモード（mac のみ）
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
		w->tickAt.push_back(now);
#if GS_MAC
		if (CFStringRef mode = CFRunLoopCopyCurrentMode(CFRunLoopGetCurrent()))
		{
			char buf[128] = {};
			CFStringGetCString(mode, buf, sizeof buf, kCFStringEncodingUTF8);
			w->modes.insert(buf);
			CFRelease(mode);
		}
#endif
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

	void SdDefaultFired(CFRunLoopTimerRef, void*)
	{
		if (gSdWatch != nullptr)
			gSdWatch->defaultAt.push_back(SdMs(gSdWatch->t0));
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
			fDefault = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.1,
											0.1, 0, 0, &SdDefaultFired, nullptr);
			CFRunLoopAddTimer(CFRunLoopGetCurrent(), fDefault, kCFRunLoopDefaultMode);
#else
			fTimer = ::SetTimer(nullptr, 0, 100, &SdTimerFired);
#endif
		}

		~SdWatchScope()
		{
#if GS_MAC
			CFRunLoopTimerInvalidate(fTimer);
			CFRelease(fTimer);
			CFRunLoopTimerInvalidate(fDefault);
			CFRelease(fDefault);
#else
			if (fTimer != 0)
				::KillTimer(nullptr, fTimer);
#endif
			gSdWatch = nullptr;
		}

		const SdWatch& watch() const
		{
			return fWatch;
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
		CFRunLoopTimerRef fDefault = nullptr;
#else
		UINT_PTR fTimer = 0;
#endif
	};

	// -----------------------------------------------------------------------
	// 見張りのスレッド

	std::atomic<bool> gLgPoke{false};
	int gLgFollowMs = 3000;

#if GS_MAC
	struct LgPsn
	{
		std::uint32_t high;
		std::uint32_t low;
	};
	using LgMainConn = int (*)();
	using LgIsUnresponsive = bool (*)(int, const LgPsn*);
	using LgGetProcess = short (*)(LgPsn*);
	struct LgCgPoint
	{
		double x;
		double y;
	};
	using LgEventCreate = void* (*)(void*);
	using LgEventLocation = LgCgPoint (*)(void*);
	using LgMouseEvent = void* (*)(void*, std::uint32_t, LgCgPoint, std::uint32_t);
	using LgPostToPid = void (*)(int, void*);
	using LgCfRelease = void (*)(const void*);

	void* LgSym(const char* name)
	{
		return ::dlsym(RTLD_DEFAULT, name);
	}
#else
	HWND LgMainWindow()
	{
		for (void* h : SdTopWindows())
		{
			HWND w = static_cast<HWND>(h);
			if (::GetWindow(w, GW_OWNER) == nullptr)
				return w;
		}
		return nullptr;
	}
#endif

	class LgMonitor
	{
	public:
		explicit LgMonitor(const std::string& path)
		{
			if (!path.empty())
				fPath = SdPath(path);
			fT0 = std::chrono::steady_clock::now();
#if GS_WIN
			fMain = LgMainWindow();
			fPing = std::thread([this] { PingLoop(); });
#endif
			fFiles = std::thread([this] { FileLoop(); });
			fResp = std::thread([this] { RespLoop(); });
		}

		~LgMonitor()
		{
			stop();
		}

		void stop()
		{
			if (fStop.exchange(true))
				return;
			fFiles.join();
			fResp.join();
#if GS_WIN
			fPing.join();
#endif
		}

		void report(vwprobe::Report& probe, const std::string& tag)
		{
			stop();
			probe.log(tag + ": 見張り 判定の口=" + fRespMethod +
					  " 応答なしの変化=" + std::to_string(fRespEvents.size()) +
					  "件 判定回数=" + std::to_string(fRespChecks.load()));
			for (const std::string& e : fRespEvents)
				probe.log(tag + ":   " + e);
			probe.log(tag + ": 見張り ファイルの変化=" + std::to_string(fFileEvents.size()) +
					  "件 取りこぼし=" + std::to_string(fDropped));
			for (const std::string& e : fFileEvents)
				probe.log(tag + ":   " + e);
		}

		LgMonitor(const LgMonitor&) = delete;
		LgMonitor& operator=(const LgMonitor&) = delete;

	private:
		long long Now() const
		{
			return SdMs(fT0);
		}

		void Push(const std::string& line)
		{
			std::lock_guard<std::mutex> lock(fMutex);
			if (fFileEvents.size() >= 200)
				++fDropped;
			else
				fFileEvents.push_back("[" + std::to_string(Now()) + "ms] " + line);
		}

		void PushResp(const std::string& line)
		{
			std::lock_guard<std::mutex> lock(fMutex);
			fRespEvents.push_back("[" + std::to_string(Now()) + "ms] " + line);
		}

		void FileLoop()
		{
			if (fPath.empty())
				return;
			std::error_code ec;
			const sdfs::path dir = fPath.parent_path();
			std::set<std::string> names;
			auto list = [&](std::set<std::string>& out)
			{
				out.clear();
				for (sdfs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
					out.insert(SdUtf8(it->path().filename()));
			};
			list(names);
			long long lastSize = -2;
			long long lastTime = -2;
			int loop = 0;
			while (!fStop.load())
			{
				std::error_code fe;
				const long long size = static_cast<long long>(sdfs::file_size(fPath, fe));
				const long long when =
					fe ? -1
					   : static_cast<long long>(
							 sdfs::last_write_time(fPath, fe).time_since_epoch().count());
				const long long s = fe ? -1 : size;
				if (s != lastSize || when != lastTime)
				{
					Push("対象 size=" + std::to_string(s) + " time=" + std::to_string(when));
					lastSize = s;
					lastTime = when;
				}
				if (++loop % 4 == 0)
				{
					std::set<std::string> now;
					list(now);
					for (const std::string& n : now)
					{
						if (names.count(n) == 0)
							Push("フォルダに現れた: " + n);
					}
					for (const std::string& n : names)
					{
						if (now.count(n) == 0)
							Push("フォルダから消えた: " + n);
					}
					names.swap(now);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		void RespLoop()
		{
			bool last = false;
#if GS_MAC
			auto mainConn = reinterpret_cast<LgMainConn>(LgSym("CGSMainConnectionID"));
			auto isUnresp = reinterpret_cast<LgIsUnresponsive>(LgSym("CGSEventIsAppUnresponsive"));
			auto getProc = reinterpret_cast<LgGetProcess>(LgSym("GetCurrentProcess"));
			auto evCreate = reinterpret_cast<LgEventCreate>(LgSym("CGEventCreate"));
			auto evLoc = reinterpret_cast<LgEventLocation>(LgSym("CGEventGetLocation"));
			auto evMouse = reinterpret_cast<LgMouseEvent>(LgSym("CGEventCreateMouseEvent"));
			auto evPost = reinterpret_cast<LgPostToPid>(LgSym("CGEventPostToPid"));
			auto cfRel = reinterpret_cast<LgCfRelease>(LgSym("CFRelease"));
			LgPsn psn{0, 0};
			if (getProc != nullptr)
				getProc(&psn);
			fRespMethod = (mainConn != nullptr && isUnresp != nullptr && getProc != nullptr)
							  ? "CGSEventIsAppUnresponsive"
							  : "無し（シンボルが引けない）";
			int loop = 0;
			while (!fStop.load())
			{
				if (mainConn != nullptr && isUnresp != nullptr && getProc != nullptr)
				{
					++fRespChecks;
					const bool now = isUnresp(mainConn(), &psn);
					if (now != last)
						PushResp(std::string("応答なし=") + SdFlag(now));
					last = now;
				}
				if (gLgPoke.load() && ++loop % 5 == 0 && evCreate != nullptr && evLoc != nullptr &&
					evMouse != nullptr && evPost != nullptr && cfRel != nullptr)
				{
					void* here = evCreate(nullptr);
					const LgCgPoint at = evLoc(here);
					cfRel(here);
					void* move = evMouse(nullptr, 5 /* kCGEventMouseMoved */, at, 0);
					evPost(::getpid(), move);
					cfRel(move);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
#else
			fRespMethod = fMain != nullptr ? "IsHungAppWindow" : "無し（主窓が無い）";
			while (!fStop.load())
			{
				if (fMain != nullptr)
				{
					++fRespChecks;
					const bool now = ::IsHungAppWindow(fMain) != FALSE;
					if (now != last)
						PushResp(std::string("IsHungAppWindow=") + SdFlag(now));
					last = now;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
#endif
			if (last)
				PushResp("見張りの終わりで まだ応答なし");
		}

#if GS_WIN
		void PingLoop()
		{
			bool last = true;
			while (!fStop.load())
			{
				if (fMain != nullptr)
				{
					DWORD_PTR result = 0;
					const LRESULT ok = ::SendMessageTimeoutW(fMain, WM_NULL, 0, 0, SMTO_ABORTIFHUNG,
															 5000, &result);
					const bool now = ok != 0;
					if (now != last)
						PushResp(std::string("Responding=") + SdFlag(now));
					last = now;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
		}

		HWND fMain = nullptr;
		std::thread fPing;
#endif

		sdfs::path fPath;
		std::chrono::steady_clock::time_point fT0;
		std::atomic<bool> fStop{false};
		std::atomic<long long> fRespChecks{0};
		std::mutex fMutex;
		std::string fRespMethod = "未判定";
		std::vector<std::string> fRespEvents;
		std::vector<std::string> fFileEvents;
		long long fDropped = 0;
		std::thread fFiles;
		std::thread fResp;
	};

	// -----------------------------------------------------------------------
	// 測り方

	std::string LgSeries(const std::vector<long long>& at, long long endMs)
	{
		long long gap = 0;
		long long prev = 0;
		for (long long t : at)
		{
			gap = std::max(gap, t - prev);
			prev = t;
		}
		gap = std::max(gap, endMs - prev);
		std::string out = std::to_string(at.size()) + "回 最大間隔=" + std::to_string(gap) + "ms";
		if (!at.empty())
		{
			out += " 初回=" + std::to_string(at.front()) + "ms 最後=" + std::to_string(at.back()) +
				   "ms 列=[";
			const std::size_t show = std::min<std::size_t>(at.size(), 30);
			for (std::size_t i = 0; i < show; ++i)
				out += (i == 0 ? "" : ",") + std::to_string(at[i]);
			out += at.size() > show ? ",…]" : "]";
		}
		return out;
	}

	void LgLogTicks(vwprobe::Report& probe, const std::string& tag, const SdWatch& w, long long ms)
	{
#if GS_MAC
		std::string modes;
		for (const std::string& m : w.modes)
			modes += (modes.empty() ? "" : ",") + m;
		probe.log(tag + ": 刻み 共通モード=" + LgSeries(w.tickAt, ms) + " モード={" + modes + "}");
		probe.log(tag + ": 刻み 既定のモード=" + LgSeries(w.defaultAt, ms));
#else
		probe.log(tag + ": 刻み SetTimer=" + LgSeries(w.tickAt, ms));
#endif
	}

	std::string LgStat(const std::string& path)
	{
		std::error_code ec;
		const sdfs::path p = SdPath(path);
		const auto size = sdfs::file_size(p, ec);
		if (ec)
			return "無い";
		const long long when =
			static_cast<long long>(sdfs::last_write_time(p, ec).time_since_epoch().count());
		return "size=" + std::to_string(size) + " time=" + std::to_string(when);
	}

	// f を呼ぶ。戻った「直後に」after を呼ぶ（所要は f だけ）。
	template <class Fn, class After>
	void LgMeasure(vwprobe::Report& probe, const std::string& tag, const std::string& path, Fn f,
				   After after)
	{
		SdWatchScope watch(probe, tag);
		LgMonitor monitor(path);
		const auto t0 = std::chrono::steady_clock::now();
		const std::string result = f();
		const long long ms = SdMs(t0);
		after();
		watch.report(result + " 呼び出し=" + std::to_string(ms) + "ms");
		LgLogTicks(probe, tag, watch.watch(), ms);
		monitor.report(probe, tag);
	}

	void LgPump(int ms)
	{
		const auto t0 = std::chrono::steady_clock::now();
		while (SdMs(t0) < ms)
		{
#if GS_MAC
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
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
	}

	void LgFollow(vwprobe::Report& probe, const std::string& tag, const std::string& path, int ms)
	{
		LgMeasure(
			probe, tag + " 後", path,
			[&]
			{
				LgPump(ms);
				return std::string("回した");
			},
			[] {});
	}

	// -----------------------------------------------------------------------
	// 動詞

	void LgSave(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(path.c_str()));
		probe.log(tag + ": 前 " + LgStat(path) + " path=" + path);
		SdLogDocs(probe, tag + " 前");
		LgMeasure(
			probe, tag, path,
			[&]
			{
				const GSError err = gSDK->SaveActiveDocumentPath(fileID);
				return "GSError=" + std::to_string(static_cast<long long>(err));
			},
			[&]
			{
				// 大きさと更新時刻は、ここがいちばん早い読み。
				const std::string stat = LgStat(path);
				bool saved = false;
				Filing::IFileIdentifierPtr active(Filing::IID_FileIdentifier);
				gSDK->GetActiveDocument(&active, saved);
				probe.log(tag + ": 直後 " + stat + " outSaved=" + SdFlag(saved));
				SdLogDocs(probe, tag + " 直後");
			});
		const auto t0 = std::chrono::steady_clock::now();
		SdLogFile(probe, tag + " 直後のハッシュ", path);
		probe.log(tag + ": ハッシュ=" + std::to_string(SdMs(t0)) + "ms");
		LgFollow(probe, tag, path, gLgFollowMs);
		SdLogFile(probe, tag + " 回した後のハッシュ", path);
	}

	void LgClose(vwprobe::Report& probe, const std::string& tag)
	{
		SdDoc target;
		for (const SdDoc& d : SdDocs())
		{
			if (d.active)
				target = d;
		}
		probe.log(tag + ": 対象 fileRef=" + std::to_string(target.ref) + " path=" + target.path);
		const std::size_t before = SdDocs().size();
		auto present = [&]
		{
			for (const SdDoc& d : SdDocs())
			{
				if (d.ref == target.ref)
					return true;
			}
			return false;
		};
		const std::string watchPath = target.inMemoryOnly ? std::string() : target.path;
		LgMeasure(
			probe, tag, watchPath,
			[&] {
				return "DoMenuName=" +
					   std::to_string(static_cast<int>(gSDK->DoMenuName("Close", 0)));
			},
			[&]
			{
				const std::size_t count = SdDocs().size();
				probe.log(tag + ": 直後 件数 " + std::to_string(before) + "→" +
						  std::to_string(count) + " 対象が残っている=" + SdFlag(present()));
			});
		LgFollow(probe, tag, watchPath, gLgFollowMs);
		probe.log(tag + ": 回した後 件数=" + std::to_string(SdDocs().size()) +
				  " 対象が残っている=" + SdFlag(present()));
		SdLogDocs(probe, tag + " 回した後");
	}

	void LgOpen(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		if (!path.empty())
			fileID->Set(TXString(path.c_str()));
		LgMeasure(
			probe, tag, path,
			[&]
			{
				const bool ok = gSDK->OpenDocumentPath(path.empty() ? nullptr : fileID, false);
				return "OpenDocumentPath=" + SdFlag(ok);
			},
			[] {});
		SdLogDocs(probe, tag + " 後");
	}

	void LgSwitch(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		for (const SdDoc& d : SdDocs())
		{
			if (d.path == path)
			{
				const auto t0 = std::chrono::steady_clock::now();
				gSDK->SwitchToOpenFile(d.ref);
				probe.log(tag + ": " + std::to_string(SdMs(t0)) + "ms 前面=" + SdActivePath());
				return;
			}
		}
		probe.fail(tag + ": 開いていない: " + path);
	}

	void LgCount(vwprobe::Report& probe, const std::string& tag)
	{
		const auto t0 = std::chrono::steady_clock::now();
		long long layers = 0;
		gSDK->ForEachLayerN([&](MCObjectHandle) { ++layers; });
		long long objects = 0;
		gSDK->ForEachObjectN(allObjects, [&](MCObjectHandle) { ++objects; });
		long long deep = 0;
		gSDK->ForEachObjectN(allObjects | descendIntoAll, [&](MCObjectHandle) { ++deep; });
		probe.log(tag + ": レイヤ=" + std::to_string(layers) + " 図形=" + std::to_string(objects) +
				  " 中まで=" + std::to_string(deep) + " (" + std::to_string(SdMs(t0)) + "ms)");
	}

	// 乱数は再現できるように固定の種から。
	std::uint32_t gLgSeed = 290;
	double LgRand(double range)
	{
		gLgSeed = gLgSeed * 1664525u + 1013904223u;
		return (static_cast<double>(gLgSeed >> 8) / 16777216.0) * range;
	}

	void LgRects(vwprobe::Report& probe, const std::string& tag, long long count)
	{
		const auto t0 = std::chrono::steady_clock::now();
		for (long long i = 0; i < count; ++i)
		{
			const double x = LgRand(100000.0);
			const double y = LgRand(100000.0);
			gSDK->CreateRectangle(WorldRect(x, y + 100.0, x + 100.0, y));
		}
		gSDK->EndUndoEvent();
		probe.log(tag + ": 作った " + std::to_string(count) + " 件 " + std::to_string(SdMs(t0)) +
				  "ms");
	}

	void LgPolys(vwprobe::Report& probe, const std::string& tag, long long count, long long verts)
	{
		const auto t0 = std::chrono::steady_clock::now();
		for (long long i = 0; i < count; ++i)
		{
			MCObjectHandle poly = gSDK->CreatePolyshape();
			if (poly == nullptr)
			{
				probe.fail(tag + ": CreatePolyshape が nil");
				return;
			}
			for (long long v = 0; v < verts; ++v)
			{
				gSDK->AddVertex(poly, WorldPt(LgRand(100000.0), LgRand(100000.0)), nullptr,
								vtCorner, 0, v + 1 == verts);
			}
			if (i % 1000 == 999)
				probe.log(tag + ":   " + std::to_string(i + 1) + " 件 " + std::to_string(SdMs(t0)) +
						  "ms");
		}
		gSDK->EndUndoEvent();
		probe.log(tag + ": 作った " + std::to_string(count) + " 件 × " + std::to_string(verts) +
				  " 頂点 " + std::to_string(SdMs(t0)) + "ms");
	}

	void LgSyms(vwprobe::Report& probe, const std::string& tag, long long defs, long long places)
	{
		const auto t0 = std::chrono::steady_clock::now();
		MCObjectHandle layer = gSDK->GetCurrentLayer();
		std::vector<MCObjectHandle> made;
		for (long long i = 0; i < defs; ++i)
		{
			TXString name(("lg-def-" + std::to_string(gLgSeed) + "-" + std::to_string(i)).c_str());
			MCObjectHandle def = gSDK->CreateSymbolDefinition(name);
			if (def == nullptr)
			{
				probe.fail(tag + ": CreateSymbolDefinition が nil");
				return;
			}
			for (int k = 0; k < 20; ++k)
			{
				const double x = LgRand(1000.0);
				const double y = LgRand(1000.0);
				MCObjectHandle r = gSDK->CreateRectangle(WorldRect(x, y + 50.0, x + 50.0, y));
				gSDK->AddObjectToContainer(r, def);
			}
			gSDK->ResetObject(def);
			made.push_back(def);
		}
		long long placed = 0;
		for (long long i = 0; i < places && !made.empty(); ++i)
		{
			MCObjectHandle def = made[static_cast<std::size_t>(i) % made.size()];
			MCObjectHandle s = gSDK->PlaceSymbolN(def, WorldPt(LgRand(100000.0), LgRand(100000.0)));
			if (s != nullptr)
			{
				gSDK->AddObjectToContainer(s, layer);
				++placed;
			}
		}
		gSDK->EndUndoEvent();
		probe.log(tag + ": 定義 " + std::to_string(defs) + " 配置 " + std::to_string(placed) + " " +
				  std::to_string(SdMs(t0)) + "ms");
	}

	void LgViewports(vwprobe::Report& probe, const std::string& tag, long long count, short render)
	{
		const auto t0 = std::chrono::steady_clock::now();
		std::vector<MCObjectHandle> designs;
		gSDK->ForEachLayerN(
			[&](MCObjectHandle layer)
			{
				TVariableBlock type;
				gSDK->GetObjectVariable(layer, ovLayerType, type);
				Sint16 value = 0;
				if (type.GetSint16(value) && value == kLayerDesign)
					designs.push_back(layer);
			});
		TXString sheetName(("lg-sheet-" + std::to_string(gLgSeed)).c_str());
		MCObjectHandle sheet = gSDK->CreateLayer(sheetName, kLayerSheet);
		if (sheet == nullptr)
		{
			probe.fail(tag + ": シートレイヤが nil");
			return;
		}
		for (long long i = 0; i < count; ++i)
		{
			MCObjectHandle vp = gSDK->CreateViewport(sheet);
			if (vp == nullptr)
			{
				probe.fail(tag + ": CreateViewport が nil");
				return;
			}
			for (MCObjectHandle layer : designs)
				gSDK->SetViewportLayerVisibility(vp, layer, 0);
			const Boolean set = gSDK->SetObjectVariable(
				vp, ovViewportRenderType, TVariableBlock(static_cast<Sint16>(render)));
			VWFC::VWObjects::VWViewportObj viewport(vp);
			viewport.SetDirty(true);
			const auto t1 = std::chrono::steady_clock::now();
			gSDK->UpdateViewport(vp);
			probe.log(tag + ":   " + std::to_string(i + 1) + " 枚目 描画種別を書けた=" +
					  SdFlag(set != FALSE) + " 更新=" + std::to_string(SdMs(t1)) + "ms");
		}
		gSDK->EndUndoEvent();
		probe.log(tag + ": 作った " + std::to_string(count) + " 枚 " + std::to_string(SdMs(t0)) +
				  "ms");
	}

	std::string LgExpand(std::string s, const std::string& work)
	{
		const std::string mark = "$T";
		for (std::size_t at = s.find(mark); at != std::string::npos; at = s.find(mark, at))
		{
			s.replace(at, mark.size(), work);
			at += work.size();
		}
		return s;
	}

	void LgRun(vwprobe::Report& probe, const std::string& line, const std::string& work, int no)
	{
		const std::size_t space = line.find(' ');
		const std::string verb = line.substr(0, space);
		const std::string arg =
			space == std::string::npos ? std::string() : LgExpand(line.substr(space + 1), work);
		const std::string tag = std::to_string(no) + " " + verb;
		const std::size_t argSpace = arg.find(' ');
		const long long n1 = std::atoll(arg.c_str());
		const long long n2 =
			argSpace == std::string::npos ? 0 : std::atoll(arg.c_str() + argSpace + 1);
		probe.log("--- " + std::to_string(no) + ": " + line);
		std::error_code ec;
		if (verb == "new")
			LgOpen(probe, tag, std::string());
		else if (verb == "open")
			LgOpen(probe, tag, arg);
		else if (verb == "switch")
			LgSwitch(probe, tag, arg);
		else if (verb == "docs")
			SdLogDocs(probe, tag);
		else if (verb == "count")
			LgCount(probe, tag);
		else if (verb == "saveto")
			LgSave(probe, tag, arg);
		else if (verb == "save")
			LgSave(probe, tag, SdActivePath());
		else if (verb == "close")
			LgClose(probe, tag);
		else if (verb == "rects")
			LgRects(probe, tag, n1);
		else if (verb == "polys")
			LgPolys(probe, tag, n1, n2);
		else if (verb == "syms")
			LgSyms(probe, tag, n1, n2);
		else if (verb == "vps")
			LgViewports(probe, tag, n1, static_cast<short>(n2));
		else if (verb == "copy")
		{
			const std::string arrow = " -> ";
			const std::size_t at = arg.find(arrow);
			sdfs::copy_file(SdPath(arg.substr(0, at)), SdPath(arg.substr(at + arrow.size())),
							sdfs::copy_options::overwrite_existing, ec);
			probe.log(tag + ": " + (ec ? ec.message() : std::string("ok")));
		}
		else if (verb == "rm")
			probe.log(tag + ": " + SdFlag(sdfs::remove(SdPath(arg), ec)));
		else if (verb == "fs")
			SdLogFile(probe, tag, arg);
		else if (verb == "follow")
			gLgFollowMs = static_cast<int>(n1);
		else if (verb == "pump")
			LgFollow(probe, tag, std::string(), static_cast<int>(n1));
		else if (verb == "block")
		{
			LgMeasure(
				probe, tag, std::string(),
				[&]
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(n1));
					return std::string("止めた");
				},
				[] {});
		}
		else if (verb == "poke")
			gLgPoke.store(arg == "on");
		else if (verb == "press")
			gSdAutoPress = arg != "none";
		else
			probe.fail(tag + ": 知らない動詞");
	}
} // namespace

VW_PROBE("doc-large", "大きな図面の閉じる・保存", "指示ファイルの手順で測る")
{
	std::error_code ec;
	const sdfs::path tmp = sdfs::temp_directory_path(ec);
	const sdfs::path doFile = tmp / "VwSdkProbes-doc-large.do";
	const sdfs::path work = tmp / "VwSdkProbes-doc-large";
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
	gLgPoke.store(false);
	gLgFollowMs = 3000;
	int no = 0;
	for (const std::string& line : lines)
		LgRun(probe, line, SdUtf8(work), ++no);
	probe.log("終わり");
}
