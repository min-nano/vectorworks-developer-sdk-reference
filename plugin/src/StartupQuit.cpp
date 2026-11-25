//
//	StartupQuit.cpp
//
//	起動の途中の刻みから終了を頼む記録（issue #312）。決めごとは StartupQuit.h。
//

#include "PluginPrefix.h"
#include "StartupQuit.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#	include <objc/message.h>
#	include <objc/runtime.h>
#	include <sys/stat.h>
#	include <sys/sysctl.h>
#	include <unistd.h>
#else
#	include <processthreadsapi.h>
#endif

#if GS_MAC
// libobjc が公開している（ヘッダには無い）。刻みの中で作られる NSString などを片付ける。
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void*);
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
			kSqMark,
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
			// issue #317: 起動を終えた目印の候補
			std::string markName;	 // mark=<名前>+<ms> の名前
			bool regAtInit = false;	 // reg=init: 通知を plugin_module_main で登録する
			bool registered = false; // 通知を登録したか
			std::map<std::string, long long> marks; // 目印 → 最初に見えた t_ms
			std::map<std::string, long long> notifyCount;
			long long stateLines = 0;
			// VW User Log.txt を追う
			std::string userLogPath;
			unsigned long long userLogOffset = 0;
			unsigned long long userLogIno = 0;
			bool userLogKnown = false;
			std::string userLogRest; // 改行で終わっていない残り
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

		const int kSqStateLinesMax = 600;		 // 状態の変わり目を書く上限
		const long long kSqNotifyLinesEach = 30; // 通知ごとに 1 行ずつ書く上限

		// 目印が初めて見えた時刻を覚える（2 度目以降は何もしない）。
		void SqMark(const std::string& name, const std::string& why)
		{
			if (gSq == nullptr || gSq->marks.count(name) != 0)
				return;
			gSq->marks[name] = SqElapsedMs();
			SqLog("mark " + name + " (" + why + ") mode=" + SqMode());
		}

		std::string SqFourCC(Sint32 v)
		{
			std::string out;
			for (int shift = 24; shift >= 0; shift -= 8)
			{
				const char c = (char)((v >> shift) & 0xFF);
				out += (c >= 0x20 && c < 0x7F) ? c : '?';
			}
			return out;
		}

		// 登録する通知（issue #317 の候補と、起動の前後で届きうるもの）。
		const Sint32 kSqNotifies[] = {
			kNotifyNewWrkSpace,
			kNotifyNewWrkSpaceLoaded,
			kNotifyApplicationActivate,
			kNotifyDialogDisplayImminent,
			kNotifyApplicationShutdown,
			kNotifyPrefsChanged,
			kNotifyPrefsReset,
			kNotifyThemeChanged,
			kNotifyDocPreOpen,
			kNotifyDocOpen,
			kNotifyDocChange,
			kNotifyDocClose,
			kNotifyMenuDone,
			kNotifyUpdate,
			kNotifyToolChange,
			kNotifyBeforePendingUpdate,
			kNotifyAfterPendingUpdate,
			kNotifyWorkspaceEditorRequested,
			kNotifyPlugInUpdate,
		};

		void SqNotifyAny(StatusID id, StatusData data)
		{
			if (gSq == nullptr)
				return;
			const std::string code = SqFourCC((Sint32)id);
			const long long n = ++gSq->notifyCount[code];
			if (n <= kSqNotifyLinesEach)
				SqLog("notify " + code + " n=" + std::to_string(n) +
					  " data=" + std::to_string((long long)data) + " mode=" + SqMode());
			SqMark(code, "通知");
		}

		void SqRegisterNotifies(const char* where)
		{
			if (gSq == nullptr || gSq->registered)
				return;
			gSq->registered = true;
			std::string ok;
			std::string ng;
			for (Sint32 code : kSqNotifies)
			{
				const Boolean reg = gSDK->RegisterNotificationProcedure(&SqNotifyAny, code);
				(reg ? ok : ng) += SqFourCC(code) + " ";
			}
			SqLog(std::string("register where=") + where + " ok=[" + ok + "] ng=[" + ng + "]");
		}

		std::string SqUtf8Tx(const TXString& text)
		{
			const char* const raw = static_cast<const char*>(text);
			return raw != nullptr ? std::string(raw) : std::string();
		}

		// 状態を読む gSDK の口（issue #317 の候補）。
		std::string SqSdkState(bool& ready, std::string& ws)
		{
			ready = gSDK->IsDocumentReady();
			TXString name;
			gSDK->GetCurrentWorkspaceFileName(name);
			ws = SqUtf8Tx(name);
			return std::string("ready=") + (ready ? "1" : "0") + " ws='" + ws + "'";
		}

#if GS_MAC
		// NSApp の見えている窓を「クラス:題」で並べる（objc ランタイムで読む。AppKit の
		// ヘッダは SDK のヘッダとぶつかりうるので入れない）。
		id SqMsg(id obj, const char* sel)
		{
			if (obj == nil)
				return nil;
			return ((id(*)(id, SEL))objc_msgSend)(obj, sel_registerName(sel));
		}
		std::string SqNsString(id str)
		{
			if (str == nil)
				return std::string();
			const char* c =
				((const char* (*)(id, SEL))objc_msgSend)(str, sel_registerName("UTF8String"));
			return c != nullptr ? std::string(c) : std::string();
		}
		std::string SqWindowName(id w)
		{
			return std::string(object_getClassName(w)) + ":" + SqNsString(SqMsg(w, "title"));
		}
		std::string SqNsWindows()
		{
			void* pool = objc_autoreleasePoolPush();
			std::string out;
			id app = ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"),
													sel_registerName("sharedApplication"));
			id wins = SqMsg(app, "windows");
			const unsigned long n =
				wins == nil
					? 0
					: ((unsigned long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count"));
			int visible = 0;
			std::string list;
			for (unsigned long i = 0; i < n; ++i)
			{
				id w = ((id(*)(id, SEL, unsigned long))objc_msgSend)(
					wins, sel_registerName("objectAtIndex:"), i);
				if (!((BOOL(*)(id, SEL))objc_msgSend)(w, sel_registerName("isVisible")))
					continue;
				++visible;
				if (!list.empty())
					list += " | ";
				list += SqWindowName(w);
			}
			id modal = SqMsg(app, "modalWindow");
			id mainW = SqMsg(app, "mainWindow");
			out = "nswin=" + std::to_string(visible) + "/" + std::to_string(n) + " main=[" +
				  (mainW == nil ? std::string("-") : SqWindowName(mainW)) + "] modal=[" +
				  (modal == nil ? std::string("-") : SqWindowName(modal)) + "] wins=[" + list + "]";
			objc_autoreleasePoolPop(pool);
			return out;
		}

		long long SqProcStartWallMs()
		{
			int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)::getpid()};
			struct kinfo_proc info;
			size_t size = sizeof(info);
			std::memset(&info, 0, sizeof(info));
			if (::sysctl(mib, 4, &info, &size, nullptr, 0) != 0)
				return -1;
			return (long long)info.kp_proc.p_starttime.tv_sec * 1000 +
				   info.kp_proc.p_starttime.tv_usec / 1000;
		}
#endif

		std::string SqUserLogPath()
		{
#if GS_MAC
			const char* home = std::getenv("HOME");
			if (home == nullptr)
				return std::string();
			return std::string(home) +
				   "/Library/Application Support/Vectorworks/2026/VW User Log.txt";
#else
			const char* appData = std::getenv("APPDATA");
			if (appData == nullptr)
				return std::string();
			return std::string(appData) + "\\Nemetschek\\Vectorworks\\2026\\VW User Log.txt";
#endif
		}

		bool SqUserLogStat(unsigned long long& size, unsigned long long& ino)
		{
#if GS_MAC
			struct stat sb;
			if (::stat(gSq->userLogPath.c_str(), &sb) != 0)
				return false;
			size = (unsigned long long)sb.st_size;
			ino = (unsigned long long)sb.st_ino;
			return true;
#else
			std::error_code ec;
			const auto sz = std::filesystem::file_size(gSq->userLogPath, ec);
			if (ec)
				return false;
			size = (unsigned long long)sz;
			ino = 0;
			return true;
#endif
		}

		// "key":"value" の value を抜く（JSON 1 行。ざっくりで足りる）。
		std::string SqJsonField(const std::string& line, const char* key)
		{
			const std::string pat = std::string("\"") + key + "\":\"";
			const size_t at = line.find(pat);
			if (at == std::string::npos)
				return std::string();
			const size_t begin = at + pat.size();
			const size_t end = line.find('"', begin);
			return line.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
		}

		void SqUserLogLine(const std::string& line)
		{
			const std::string cat = SqJsonField(line, "cat");
			if (cat != "startupTIMER" && cat != "HomeScreen" && cat != "SHUTDOWN")
				return;
			const std::string msg = SqJsonField(line, "message");
			SqLog("userlog cat=" + cat + " ts='" + SqJsonField(line, "ts") + "' message='" + msg +
				  "'");
			if (msg.find("Section = HomeScreen") != std::string::npos)
				SqMark("home", "VW User Log.txt");
			if (msg.rfind("End of Startup", 0) == 0)
				SqMark("endofstartup", "VW User Log.txt");
		}

		// 刻みのたびに、増えた分だけ読む。差し替わったら（inode が変わる・縮む）頭から読む。
		void SqPollUserLog()
		{
			SqState* st = gSq;
			if (st == nullptr || st->userLogPath.empty())
				return;
			unsigned long long size = 0;
			unsigned long long ino = 0;
			if (!SqUserLogStat(size, ino))
				return;
			if (!st->userLogKnown)
				return;
			if (ino != st->userLogIno || size < st->userLogOffset)
			{
				SqLog("userlog-replaced size=" + std::to_string(size) + " (前 " +
					  std::to_string(st->userLogOffset) + ")");
				st->userLogIno = ino;
				st->userLogOffset = 0;
				st->userLogRest.clear();
			}
			if (size <= st->userLogOffset)
				return;
			std::ifstream in(st->userLogPath, std::ios::binary);
			if (!in)
				return;
			in.seekg((std::streamoff)st->userLogOffset);
			std::string chunk((size_t)(size - st->userLogOffset), '\0');
			in.read(&chunk[0], (std::streamsize)chunk.size());
			chunk.resize((size_t)in.gcount());
			st->userLogOffset += chunk.size();
			std::string buf = st->userLogRest + chunk;
			size_t start = 0;
			for (;;)
			{
				const size_t nl = buf.find('\n', start);
				if (nl == std::string::npos)
					break;
				SqUserLogLine(buf.substr(start, nl - start));
				start = nl + 1;
			}
			st->userLogRest = buf.substr(start);
		}

		void SqUserLogInit()
		{
			SqState* st = gSq;
			st->userLogPath = SqUserLogPath();
			unsigned long long size = 0;
			unsigned long long ino = 0;
			if (!st->userLogPath.empty() && SqUserLogStat(size, ino))
			{
				st->userLogOffset = size;
				st->userLogIno = ino;
				st->userLogKnown = true;
				SqLog("userlog-start size=" + std::to_string(size) + " path='" + st->userLogPath +
					  "'");
			}
			else
			{
				// まだ無い。現れたら頭から読む。
				st->userLogKnown = true;
				st->userLogOffset = 0;
				st->userLogIno = 0;
				SqLog("userlog-start (まだ無い) path='" + st->userLogPath + "'");
			}
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
			if (st->ticks == 1)
				SqLog("first-tick mode=" + mode);
			SqRegisterNotifies("first-tick");
			SqPollUserLog();

			if (st->called)
			{
				SqPollUserLog();
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
			bool ready = false;
			std::string ws;
			key += " " + SqSdkState(ready, ws);
			if (ready)
				SqMark("ready", "IsDocumentReady");
			if (!ws.empty())
				SqMark("ws", "GetCurrentWorkspaceFileName");
#if GS_MAC
			key += " " + SqNsWindows();
#endif
			if (key != st->lastKey)
			{
				++st->stateLines;
				if (st->stateLines <= kSqStateLinesMax)
					SqLog("state tick=" + std::to_string(st->ticks) +
						  " gap_ms=" + std::to_string(gap) + " " + key);
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
			case kSqMark:
			{
				const auto it = st->marks.find(st->markName);
				fire = it != st->marks.end() && now - it->second >= st->param;
				reason = "mark " + st->markName + "+" + std::to_string(st->param);
				break;
			}
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
			else if (s.rfind("mark=", 0) == 0)
			{
				// mark=<名前>[+<ms>]。名前は通知の 4 文字（WKSL など）・home・endofstartup・
				// ready・ws。
				st.when = kSqMark;
				const std::string rest = s.substr(5);
				const size_t plus = rest.find('+');
				st.markName = rest.substr(0, plus);
				st.param = plus == std::string::npos ? 0 : std::atoll(rest.c_str() + plus + 1);
				if (st.markName.empty())
					return false;
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
		std::vector<std::string> options; // 2 行目以降（reg=init など）
		{
			std::ifstream in(request);
			std::getline(in, line);
			std::string opt;
			while (std::getline(in, opt))
			{
				while (!opt.empty() && (opt.back() == '\r' || opt.back() == ' '))
					opt.pop_back();
				if (!opt.empty())
					options.push_back(opt);
			}
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
		std::string optText;
		for (const std::string& opt : options)
		{
			optText += opt + " ";
			if (opt == "reg=init")
				st->regAtInit = true;
		}
		SqLog("start request='" + st->request + "' options=[" + optText +
			  "] pid=" + std::to_string(pid) + " mode=" + SqMode());
#if GS_MAC
		SqLog("process-start wall_ms=" + std::to_string(SqProcStartWallMs()));
#endif
		SqUserLogInit();
		if (st->regAtInit)
			SqRegisterNotifies("plugin_module_main");

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
