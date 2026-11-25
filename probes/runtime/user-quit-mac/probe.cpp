//
//	probes/runtime/user-quit-mac/probe.cpp
//
//	[issue #325] **利用者が mac の Vectorworks をメニューの「終了」（⌘Q）で閉じる道**で、OS
//	タイマー（50 ms・`kCFRunLoopCommonModes`）の刻みがいつまで来るか、そこから `gSDK` を
//	読んで落ちないかを測る。#269 の `user-quit-ticks`（Windows）を mac 向けに広げたもの。
//
//	走らせると仕掛けて戻るだけ。閉じるのは外（人・画面操作）。刻み・通知を壁時計のミリ秒で
//	一時ファイル `VwSdkProbes-user-quit-mac-events.txt` へ 1 行ずつ書いて閉じる（落ちても
//	そこまでが残る）。次に走らせたときにも報告する。
//
//	  * 閉じる指示の時刻は、AppKit の通知（メニューの追跡・項目の実行・`WillTerminate`）を
//	    `CFNotificationCenter` のローカルセンターで受けて、プロセスの中で書く。
//	  * 刻みの中で `gSDK` を読むかは `…-mode.txt` で選ぶ（`read` / `quiet`。既定 `read`）。
//	  * `…-mark.request` を置くと、次の刻みで `MarkCurrentDocChanged()` を呼ぶ（保存の確認を
//	    出すため）。
//	  * 本体はピン留めする（殻はプローブが終わると本体を降ろすため）。
//

#include "Probe.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if GS_WIN
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#else
#	include <CoreFoundation/CoreFoundation.h>
#	include <dlfcn.h>
#endif

#ifndef VW_PAYLOAD_BUILD_ID
#	define VW_PAYLOAD_BUILD_ID "local"
#endif

namespace
{
	const long long kUqtTickMs = 50;
	const long long kUqtMaxMs = 30LL * 60 * 1000; // 閉じられないまま放っておかれたら止まる

	std::filesystem::path UqtDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return dir;
	}
	std::string UqtEventsPath()
	{
		return (UqtDir() / "VwSdkProbes-user-quit-mac-events.txt").string();
	}
	std::string UqtModePath()
	{
		return (UqtDir() / "VwSdkProbes-user-quit-mac-mode.txt").string();
	}
	std::string UqtMarkPath()
	{
		return (UqtDir() / "VwSdkProbes-user-quit-mac-mark.request").string();
	}
	std::string UqtOwnerPath()
	{
		return (UqtDir() / "VwSdkProbes-user-quit-mac-owner.txt").string();
	}
	// 殻（plugin/src/ModuleMain.cpp）の頼み札と記録。**殻の綴りを書き写したもの。**
	std::string UqtActionsRequestPath()
	{
		return (UqtDir() / "VwSdkProbes-module-actions.request").string();
	}
	std::string UqtActionsPath()
	{
		return (UqtDir() / "VwSdkProbes-module-actions.txt").string();
	}

	std::string UqtReadFirstLine(const std::string& path)
	{
		std::ifstream in(path);
		std::string line;
		std::getline(in, line);
		return line;
	}

	std::vector<std::string> UqtReadLines(const std::string& path)
	{
		std::vector<std::string> lines;
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line))
			lines.push_back(line);
		return lines;
	}

	long long UqtWallMs()
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	void UqtAppend(const std::string& path, const std::string& line)
	{
		std::ofstream out(path, std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	std::string UqtYesNo(bool b)
	{
		return b ? "yes" : "no";
	}

	// --- 主窓 ------------------------------------------------------------------
#if GS_WIN
	HWND gUqtMainWnd = nullptr;

	BOOL CALLBACK UqtFindMain(HWND h, LPARAM out)
	{
		DWORD pid = 0;
		::GetWindowThreadProcessId(h, &pid);
		if (pid != ::GetCurrentProcessId() || !::IsWindowVisible(h) ||
			::GetWindow(h, GW_OWNER) != nullptr)
			return TRUE;
		wchar_t cls[16] = {0};
		::GetClassNameW(h, cls, 16);
		if (std::wstring(cls).rfind(L"Afx:", 0) != 0)
			return TRUE;
		*reinterpret_cast<HWND*>(out) = h;
		return FALSE;
	}

	std::string UqtMainState()
	{
		if (gUqtMainWnd == nullptr)
			return "unknown";
		if (!::IsWindow(gUqtMainWnd))
			return "gone";
		return std::string(::IsWindowVisible(gUqtMainWnd) ? "" : "hidden-") +
			   (::IsWindowEnabled(gUqtMainWnd) ? "en" : "dis");
	}
#else
	std::string UqtMainState()
	{
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return "mode=(none)";
		char buf[128] = {0};
		std::string out = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8)
							  ? std::string(buf)
							  : std::string("?");
		::CFRelease(mode);
		return "mode=" + out;
	}
#endif

	// ---------------------------------------------------------------------------
	// 刻みが持ち越す状態。**意図して leak する**（タイマーはプローブより長生き）。
	struct UqtState
	{
		std::string owner;
		std::string events;
		bool readSdk = true;
		long long armedWall = 0;
		long long ticks = 0;
		long long lastTickWall = -1;
		long long maxGapMs = 0;
		long long notifyQuit = 0;
		long long notifyDocClose = 0;
		bool disarmed = false;
	};

	UqtState* gUqtState = nullptr;
#if GS_WIN
	UINT_PTR gUqtTimer = 0;
#else
	CFRunLoopTimerRef gUqtTimer = nullptr;
#endif

	void UqtNotifyBeforeQuit(StatusID, StatusData);
	void UqtNotifyDocClose(StatusID, StatusData);

	void UqtStopTimer()
	{
#if GS_WIN
		if (gUqtTimer != 0)
			::KillTimer(nullptr, gUqtTimer);
		gUqtTimer = 0;
#else
		if (gUqtTimer != nullptr)
		{
			::CFRunLoopTimerInvalidate(gUqtTimer);
			::CFRelease(gUqtTimer);
			gUqtTimer = nullptr;
		}
#endif
		gSDK->UnregisterNotificationProcedure(&UqtNotifyBeforeQuit, kNotifyBeforeApplicationQuit);
		gSDK->UnregisterNotificationProcedure(&UqtNotifyDocClose, kNotifyDocClose);
	}

	void UqtDisarm(const std::string& why)
	{
		UqtState* st = gUqtState;
		if (st == nullptr || st->disarmed)
			return;
		st->disarmed = true;
		UqtStopTimer();
		UqtAppend(st->events, "disarm wall_ms=" + std::to_string(UqtWallMs()) + " why=" + why);
	}

	void UqtNotifyBeforeQuit(StatusID, StatusData)
	{
		UqtState* st = gUqtState;
		if (st == nullptr || st->disarmed)
			return;
		++st->notifyQuit;
		UqtAppend(st->events,
				  "notify BeforeApplicationQuit wall_ms=" + std::to_string(UqtWallMs()) +
					  " main=" + UqtMainState() + " ticks=" + std::to_string(st->ticks));
	}

	void UqtNotifyDocClose(StatusID, StatusData)
	{
		UqtState* st = gUqtState;
		if (st == nullptr || st->disarmed)
			return;
		++st->notifyDocClose;
		UqtAppend(st->events, "notify DocClose wall_ms=" + std::to_string(UqtWallMs()) + " main=" +
								  UqtMainState() + " ticks=" + std::to_string(st->ticks));
	}

#if GS_MAC
	// AppKit の通知。閉じる指示（メニューの項目の実行）と terminate: の時刻を取る。
	const char* const kUqtAppKitNames[] = {
		"NSMenuDidBeginTrackingNotification",	  "NSMenuDidEndTrackingNotification",
		"NSMenuWillSendActionNotification",		  "NSMenuDidSendActionNotification",
		"NSApplicationWillTerminateNotification",
	};
	void UqtOnAppKit(CFNotificationCenterRef, void*, CFNotificationName name, const void*,
					 CFDictionaryRef)
	{
		UqtState* st = gUqtState;
		if (st == nullptr || st->disarmed)
			return;
		char buf[128] = {0};
		::CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
		UqtAppend(st->events,
				  std::string("appkit ") + buf + " wall_ms=" + std::to_string(UqtWallMs()) +
					  " main=" + UqtMainState() + " ticks=" + std::to_string(st->ticks));
	}
#endif

	void UqtAtExit()
	{
		// 書き溜めがまだこの像のものなら書く（gSDK は呼ばない）。
		UqtState* st = gUqtState;
		if (st == nullptr || UqtReadFirstLine(UqtOwnerPath()) != st->owner)
			return;
		UqtAppend(st->events, "atexit wall_ms=" + std::to_string(UqtWallMs()) +
								  " ticks=" + std::to_string(st->ticks) + " (gSDK は呼ばない)");
	}

	// 1 刻み 1 行。**gSDK を読む前に書く**——落ちたら、その行が最後に残る。
	void UqtTick()
	{
		UqtState* st = gUqtState;
		if (st == nullptr || st->disarmed)
			return;
		if (UqtReadFirstLine(UqtOwnerPath()) != st->owner)
		{
			st->disarmed = true; // 書き溜めはもう別の像のもの。何も書かずに止まる
			UqtStopTimer();
			return;
		}
		++st->ticks;
		const long long now = UqtWallMs();
		const long long gap = st->lastTickWall >= 0 ? now - st->lastTickWall : 0;
		if (gap > st->maxGapMs)
			st->maxGapMs = gap;
		st->lastTickWall = now;
		std::string line = "tick " + std::to_string(st->ticks) + " wall_ms=" + std::to_string(now) +
						   " gap_ms=" + std::to_string(gap) + " main=" + UqtMainState();
		if (!st->readSdk)
		{
			UqtAppend(st->events, line);
		}
		else
		{
			UqtAppend(st->events, line + " → gSDK を読む");
			VectorWorks::TVWArray_OpenFileInformation list;
			gSDK->GetOpenFilesList(list);
			const bool undo = gSDK->IsCurrentlyBuildingAnUndoEvent();
			const bool layer = gSDK->GetCurrentLayer() != nil;
			TXString ws;
			gSDK->GetCurrentWorkspaceFileName(ws);
			UqtAppend(st->events, "  docs=" + std::to_string((int)list.GetSize()) +
									  " undo=" + UqtYesNo(undo) + " layer=" + UqtYesNo(layer) +
									  " ready=" + UqtYesNo(gSDK->IsDocumentReady()) +
									  " ws=" + UqtYesNo(!ws.IsEmpty()) +
									  " end_ms=" + std::to_string(UqtWallMs()));
		}
		std::error_code ec;
		if (std::filesystem::remove(UqtMarkPath(), ec))
		{
			gSDK->MarkCurrentDocChanged();
			UqtAppend(st->events, "mark-doc-changed wall_ms=" + std::to_string(UqtWallMs()));
		}
		if (now - st->armedWall > kUqtMaxMs)
			UqtDisarm("time-limit");
	}

#if GS_WIN
	VOID CALLBACK UqtWinTick(HWND, UINT, UINT_PTR, DWORD)
	{
		UqtTick();
	}
#else
	void UqtCFTick(CFRunLoopTimerRef, void*)
	{
		UqtTick();
	}
#endif

	std::string UqtPinSelf(std::string& pinnedPath)
	{
#if GS_WIN
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&UqtPinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した（GetLastError=" +
				   std::to_string((unsigned long)::GetLastError()) + "）";
		char buf[MAX_PATH] = {0};
		const DWORD n = ::GetModuleFileNameA(mod, buf, MAX_PATH);
		pinnedPath = (n > 0 && n < MAX_PATH) ? std::string(buf) : std::string("(名前が読めない)");
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&UqtPinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		void* handle = ::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
		if (handle == nullptr)
			return std::string("dlopen(RTLD_NOLOAD) が失敗した: ") + info.dli_fname;
		pinnedPath = info.dli_fname;
		return std::string();
#endif
	}

	// 前に仕掛けた回の書き溜めを報告する（最初の数行と、最後の 120 行）。
	void UqtReport(vwprobe::Report& probe)
	{
		const std::vector<std::string> events = UqtReadLines(UqtEventsPath());
		probe.log("■ 前の回の書き溜め（" + std::to_string(events.size()) + " 行）");
		const std::size_t kHead = 12;
		const std::size_t kTail = 120;
		for (std::size_t i = 0; i < events.size(); ++i)
		{
			if (i == kHead && events.size() > kHead + kTail)
			{
				probe.log("  …（" + std::to_string(events.size() - kHead - kTail) + " 行省略）");
				i = events.size() - kTail;
			}
			probe.log("  " + events[i]);
		}
		const std::vector<std::string> actions = UqtReadLines(UqtActionsPath());
		probe.log("■ 殻の action の記録（" + std::to_string(actions.size()) +
				  " 行。最後の 20 行）");
		for (std::size_t i = actions.size() > 20 ? actions.size() - 20 : 0; i < actions.size(); ++i)
			probe.log("  " + actions[i]);
	}
} // namespace

VW_PROBE("user-quit-mac", "閉じる道の刻み（mac）", "終了メニューで閉じたときの刻みを測る")
{
	std::error_code ec;
	if (std::filesystem::exists(UqtEventsPath(), ec))
		UqtReport(probe);
	if (gUqtState != nullptr && !gUqtState->disarmed)
	{
		probe.log("");
		probe.log("■ この像では既に仕掛けてある（Vectorworks を閉じて測る）");
		return;
	}

	std::string pinnedPath;
	const std::string trouble = UqtPinSelf(pinnedPath);
	if (!trouble.empty())
	{
		probe.fail("本体をピン留めできなかったので仕掛けない（" + trouble + "）");
		return;
	}
#if GS_WIN
	::EnumWindows(&UqtFindMain, reinterpret_cast<LPARAM>(&gUqtMainWnd));
#endif
	gUqtState = new UqtState();
	UqtState* st = gUqtState;
	st->owner = std::to_string(UqtWallMs()) + "-" + VW_PAYLOAD_BUILD_ID;
	st->events = UqtEventsPath();
	st->readSdk = UqtReadFirstLine(UqtModePath()) != "quiet";
	st->armedWall = UqtWallMs();
	{
		std::ofstream out(UqtOwnerPath(), std::ios::trunc);
		out << st->owner << "\n";
	}
	{
		// 殻に action を書いてもらう（-2 がいつ届くか）。前の回の記録は消す。
		std::filesystem::remove(UqtActionsPath(), ec);
		std::ofstream out(UqtActionsRequestPath(), std::ios::trunc);
		out << "user-quit-mac\n";
	}
	std::filesystem::remove(st->events, ec);
	UqtAppend(st->events, "arm wall_ms=" + std::to_string(st->armedWall) + " owner=" + st->owner +
							  " build=" + VW_PAYLOAD_BUILD_ID +
							  " read_sdk=" + UqtYesNo(st->readSdk) + " main=" + UqtMainState() +
							  " pinned=" + pinnedPath);

	const Boolean q =
		gSDK->RegisterNotificationProcedure(&UqtNotifyBeforeQuit, kNotifyBeforeApplicationQuit);
	const Boolean d = gSDK->RegisterNotificationProcedure(&UqtNotifyDocClose, kNotifyDocClose);
	UqtAppend(st->events, std::string("register BeforeApplicationQuit=") + UqtYesNo(q != 0) +
							  " DocClose=" + UqtYesNo(d != 0));
#if GS_MAC
	for (const char* name : kUqtAppKitNames)
	{
		CFStringRef n =
			::CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
		::CFNotificationCenterAddObserver(::CFNotificationCenterGetLocalCenter(), st, &UqtOnAppKit,
										  n, nullptr,
										  CFNotificationSuspensionBehaviorDeliverImmediately);
		::CFRelease(n);
	}
#endif
	static bool sAtExit = false; // 像ごとに 1 度
	if (!sAtExit)
		sAtExit = std::atexit(&UqtAtExit) == 0;

#if GS_WIN
	gUqtTimer = ::SetTimer(nullptr, 0, (UINT)kUqtTickMs, &UqtWinTick);
	const bool armed = gUqtTimer != 0;
#else
	const CFAbsoluteTime t = (CFAbsoluteTime)kUqtTickMs / 1000.0;
	gUqtTimer = ::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + t, t, 0,
									   0, &UqtCFTick, nullptr);
	const bool armed = gUqtTimer != nullptr;
	if (armed)
		::CFRunLoopAddTimer(::CFRunLoopGetMain(), gUqtTimer, kCFRunLoopCommonModes);
#endif
	if (!armed)
	{
		probe.fail("タイマーを仕掛けられなかった");
		UqtDisarm("arm-failed");
		return;
	}
	probe.log("");
	probe.log(std::string("■ 仕掛けた（read_sdk=") + UqtYesNo(st->readSdk) +
			  " main=" + UqtMainState() +
			  "）。このあと Vectorworks をメニューの「終了」で閉じ、起動し直して"
			  "もう一度走らせると、閉じる途中の刻みを報告する。");
}
