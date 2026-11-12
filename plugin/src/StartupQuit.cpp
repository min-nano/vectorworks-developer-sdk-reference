//
//	StartupQuit.cpp
//
//	起動の途中の刻みから終了を頼む記録（issue #312）。決めごとは StartupQuit.h。
//

#include "PluginPrefix.h"
#include "StartupQuit.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#	include <unistd.h>
#else
#	include <processthreadsapi.h>
#endif

namespace vwprobe
{
	namespace
	{
		const unsigned kSqIntervalMs = 50;
		const long long kSqWatchLimitMs = 15LL * 60 * 1000; // 呼ばないまま見続ける上限
		const long long kSqAfterReturnMs = 120LL * 1000;	// 戻った後に見続ける上限
		const int kSqAfterReturnLines = 200; // 戻った後の刻みを 1 行ずつ書く上限

		enum SqWhen
		{
			kSqFirst,
			kSqAtMs,
			kSqModal,
			kSqGo,
			kSqNever,
		};

		struct SqState
		{
			SqWhen when = kSqNever;
			long long param = 0;
			std::string request; // 札の中身（記録用）
			std::chrono::steady_clock::time_point startedAt;
			long long ticks = 0;
			long long lastTickMs = -1;
			long long maxGapMs = 0;
			long long modalSinceMs = -1;
			bool calling = false;
			bool called = false;
			long long callBeginMs = -1;
			long long returnedMs = -1;
			long long reentries = 0;
			long long ticksAfterReturn = 0;
			long long notifyQuit = 0;
			bool stopped = false;
			std::string lastKey;
		};

		SqState* gSq = nullptr; // 意図して leak する（Vectorworks が終わるまで持つ）

		std::filesystem::path SqDir()
		{
			std::error_code ec;
			std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
			if (ec)
				dir = std::filesystem::path(".");
			return dir;
		}
		std::filesystem::path SqRequestPath()
		{
			return SqDir() / "VwSdkProbes-startup-quit.request";
		}
		std::filesystem::path SqGoPath()
		{
			return SqDir() / "VwSdkProbes-startup-quit.go";
		}
		std::filesystem::path SqLogPath()
		{
			return SqDir() / "VwSdkProbes-startup-quit.txt";
		}

		long long SqWallMs()
		{
			return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::system_clock::now().time_since_epoch())
				.count();
		}
		long long SqElapsedMs()
		{
			if (gSq == nullptr)
				return -1;
			return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::steady_clock::now() - gSq->startedAt)
				.count();
		}

		// 1 行書いて閉じる。毎回開き直すので、Vectorworks ごと終わってもそこまでが残る。
		void SqLog(const std::string& line)
		{
			std::ofstream out(SqLogPath(), std::ios::app);
			if (!out)
				return;
			out << "t_ms=" << SqElapsedMs() << " wall_ms=" << SqWallMs() << " " << line << "\n";
		}

		std::string SqMode()
		{
#if GS_MAC
			CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
			if (mode == nullptr)
				return "(ランループの外)";
			char buf[256] = {0};
			std::string out = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8)
								  ? std::string(buf)
								  : std::string("(読めない)");
			::CFRelease(mode);
			return out;
#else
			return "win";
#endif
		}

#if GS_WIN
		// 自プロセスの見えているトップレベルの窓を「クラス:題:有効」で並べる。
		struct SqWinScan
		{
			DWORD pid = 0;
			std::string list;
			bool modal = false; // 題のある有効な #32770 がある
		};

		std::string SqUtf8(const wchar_t* w)
		{
			if (w == nullptr || *w == 0)
				return std::string();
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
			if (n <= 1)
				return std::string();
			std::string s((size_t)n - 1, '\0');
			::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
			return s;
		}

		BOOL CALLBACK SqEnumWindow(HWND hwnd, LPARAM lp)
		{
			SqWinScan* scan = reinterpret_cast<SqWinScan*>(lp);
			DWORD pid = 0;
			::GetWindowThreadProcessId(hwnd, &pid);
			if (pid != scan->pid || !::IsWindowVisible(hwnd))
				return TRUE;
			wchar_t cls[128] = {0};
			wchar_t title[256] = {0};
			::GetClassNameW(hwnd, cls, 128);
			::GetWindowTextW(hwnd, title, 256);
			const bool enabled = ::IsWindowEnabled(hwnd) != FALSE;
			if (!scan->list.empty())
				scan->list += " | ";
			scan->list += SqUtf8(cls) + ":" + SqUtf8(title) + ":" + (enabled ? "on" : "off");
			if (enabled && title[0] != 0 && std::wstring(cls) == L"#32770")
				scan->modal = true;
			return TRUE;
		}

		SqWinScan SqScan()
		{
			SqWinScan scan;
			scan.pid = ::GetCurrentProcessId();
			::EnumWindows(&SqEnumWindow, reinterpret_cast<LPARAM>(&scan));
			return scan;
		}
#endif

		int SqDocCount()
		{
			VectorWorks::TVWArray_OpenFileInformation list;
			gSDK->GetOpenFilesList(list);
			return (int)list.GetSize();
		}

		void SqNotifyBeforeQuit(StatusID, StatusData)
		{
			if (gSq == nullptr)
				return;
			++gSq->notifyQuit;
			SqLog("notify BeforeApplicationQuit n=" + std::to_string(gSq->notifyQuit) +
				  " mode=" + SqMode());
		}

		void SqAtExit()
		{
			if (gSq == nullptr)
				return;
			SqLog("atexit ticks=" + std::to_string(gSq->ticks) +
				  " ticks_after_return=" + std::to_string(gSq->ticksAfterReturn) +
				  " max_gap_ms=" + std::to_string(gSq->maxGapMs) + " (gSDK は呼ばない)");
		}

		void SqStop(const std::string& why);

		void SqCallQuit(const std::string& reason)
		{
			SqState* st = gSq;
			st->called = true;
			const Boolean reg = gSDK->RegisterNotificationProcedure(&SqNotifyBeforeQuit,
																	kNotifyBeforeApplicationQuit);
			SqLog("quit-call-begin reason=" + reason + " mode=" + SqMode() +
				  " docs=" + std::to_string(SqDocCount()) +
				  " register_BApQ=" + std::string(reg ? "yes" : "no") +
				  " → CloseAllFilesAndQuitVectorworks(true, false)");
			st->calling = true;
			st->callBeginMs = SqElapsedMs();
			gSDK->CloseAllFilesAndQuitVectorworks(true, false);
			st->calling = false;
			st->returnedMs = SqElapsedMs();
			SqLog("quit-call-returned took_ms=" + std::to_string(st->returnedMs - st->callBeginMs) +
				  " mode=" + SqMode() + " reentries=" + std::to_string(st->reentries));
			SqLog("after-return docs=" + std::to_string(SqDocCount()));
		}

		void SqTick()
		{
			SqState* st = gSq;
			if (st == nullptr || st->stopped)
				return;
			const long long now = SqElapsedMs();
			if (st->calling)
			{
				// 呼び出しの最中に入り直した（Windows の見込み）。呼ばずに数えるだけ。
				++st->reentries;
				if (st->reentries <= 3)
					SqLog("reentry n=" + std::to_string(st->reentries) + " mode=" + SqMode());
				return;
			}
			++st->ticks;
			if (st->lastTickMs >= 0 && now - st->lastTickMs > st->maxGapMs)
				st->maxGapMs = now - st->lastTickMs;
			const long long gap = st->lastTickMs >= 0 ? now - st->lastTickMs : 0;
			st->lastTickMs = now;
			const std::string mode = SqMode();

			if (st->called)
			{
				++st->ticksAfterReturn;
				if (st->ticksAfterReturn <= kSqAfterReturnLines)
					SqLog("tick-after-return n=" + std::to_string(st->ticksAfterReturn) +
						  " gap_ms=" + std::to_string(gap) + " mode=" + mode +
						  " → GetOpenFilesList docs=" + std::to_string(SqDocCount()));
				if (now - st->returnedMs > kSqAfterReturnMs)
					SqStop("after-return-limit");
				return;
			}

			// 状態の変わり目だけ書く。**呼ぶ前に書く**——落ちれば、この行が最後に残る。
			bool modal = false;
#if GS_MAC
			modal = mode == "NSModalPanelRunLoopMode";
			std::string key = "mode=" + mode;
#else
			const SqWinScan scan = SqScan();
			modal = scan.modal;
			std::string key = "windows=[" + scan.list + "]";
#endif
			key += " docs=" + std::to_string(SqDocCount());
			if (key != st->lastKey)
			{
				SqLog("state tick=" + std::to_string(st->ticks) + " gap_ms=" + std::to_string(gap) +
					  " " + key);
				st->lastKey = key;
			}
			if (modal && st->modalSinceMs < 0)
				st->modalSinceMs = now;
			if (!modal)
				st->modalSinceMs = -1;

			bool fire = false;
			std::string reason;
			switch (st->when)
			{
			case kSqFirst:
				fire = true;
				reason = "first";
				break;
			case kSqAtMs:
				fire = now >= st->param;
				reason = "ms>=" + std::to_string(st->param);
				break;
			case kSqModal:
				fire = st->modalSinceMs >= 0 && now - st->modalSinceMs >= st->param;
				reason = "modal for " + std::to_string(st->param) + " ms";
				break;
			case kSqGo:
			{
				std::error_code ec;
				fire = std::filesystem::exists(SqGoPath(), ec);
				if (fire)
					std::filesystem::remove(SqGoPath(), ec);
				reason = "go-file";
				break;
			}
			case kSqNever:
				break;
			}
			if (fire)
			{
				SqCallQuit(reason);
				return;
			}
			if (now > kSqWatchLimitMs)
				SqStop("watch-limit");
		}

#if GS_MAC
		CFRunLoopTimerRef gSqTimer = nullptr;
		void SqOnTimer(CFRunLoopTimerRef, void*)
		{
			SqTick();
		}
#else
		UINT_PTR gSqTimer = 0;
		VOID CALLBACK SqOnTimer(HWND, UINT, UINT_PTR, DWORD)
		{
			SqTick();
		}
#endif

		void SqStop(const std::string& why)
		{
			if (gSq != nullptr)
			{
				gSq->stopped = true;
				SqLog("stop why=" + why + " ticks=" + std::to_string(gSq->ticks));
			}
#if GS_MAC
			if (gSqTimer != nullptr)
			{
				::CFRunLoopTimerInvalidate(gSqTimer);
				::CFRelease(gSqTimer);
				gSqTimer = nullptr;
			}
#else
			if (gSqTimer != 0)
				::KillTimer(nullptr, gSqTimer);
			gSqTimer = 0;
#endif
		}

		bool SqParse(const std::string& line, SqState& st)
		{
			std::string s = line;
			while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
				s.pop_back();
			st.request = s;
			if (s == "first")
				st.when = kSqFirst;
			else if (s == "go")
				st.when = kSqGo;
			else if (s == "never")
				st.when = kSqNever;
			else if (s.rfind("ms=", 0) == 0)
			{
				st.when = kSqAtMs;
				st.param = std::atoll(s.c_str() + 3);
			}
			else if (s.rfind("modal=", 0) == 0)
			{
				st.when = kSqModal;
				st.param = std::atoll(s.c_str() + 6);
			}
			else
				return false;
			return true;
		}
	} // namespace

	void StartStartupQuitWatch()
	{
		static bool sStarted = false;
		if (sStarted)
			return;
		sStarted = true;

		std::error_code ec;
		const std::filesystem::path request = SqRequestPath();
		if (!std::filesystem::exists(request, ec))
			return;
		std::string line;
		{
			std::ifstream in(request);
			std::getline(in, line);
		}
		// **札は読んだら退ける**——置いたままだと、起動のたびに終わってしまう。
		std::filesystem::path used = request;
		used += ".used";
		std::filesystem::remove(used, ec);
		std::filesystem::rename(request, used, ec);
		if (ec)
			std::filesystem::remove(request, ec);
		std::filesystem::remove(SqGoPath(), ec);

		SqState* st = new SqState();
		st->startedAt = std::chrono::steady_clock::now();
		gSq = st;
		if (!SqParse(line, *st))
		{
			SqLog("start request='" + st->request + "' を読めないので何もしない");
			gSq = nullptr;
			return;
		}
#if GS_MAC
		const long long pid = (long long)::getpid();
#else
		const long long pid = (long long)::GetCurrentProcessId();
#endif
		SqLog("start request='" + st->request + "' pid=" + std::to_string(pid) +
			  " mode=" + SqMode());

#if GS_MAC
		const CFAbsoluteTime interval = kSqIntervalMs / 1000.0;
		gSqTimer = ::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent(),
										  interval, 0, 0, &SqOnTimer, nullptr);
		if (gSqTimer == nullptr)
		{
			SqLog("CFRunLoopTimerCreate が失敗した");
			return;
		}
		::CFRunLoopAddTimer(::CFRunLoopGetMain(), gSqTimer, kCFRunLoopCommonModes);
#else
		// 先に殻を留める（McpBridge.cpp の StartMcpBridgeClock と同じ。issue #253）。
		constexpr DWORD kPinSelf =
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		HMODULE self = nullptr;
		if (!::GetModuleHandleExW(kPinSelf, reinterpret_cast<LPCWSTR>(&SqOnTimer), &self))
		{
			SqLog("殻を留められなかったので時計を動かさない");
			return;
		}
		gSqTimer = ::SetTimer(nullptr, 0, kSqIntervalMs, &SqOnTimer);
		if (gSqTimer == 0)
		{
			SqLog("SetTimer が失敗した");
			return;
		}
#endif
		(void)std::atexit(&SqAtExit);
	}
} // namespace vwprobe
