//
//	probes/runtime/quit-twice/probe.cpp
//
//	[issue #313] 図面 0 枚で刻みから `CloseAllFilesAndQuitVectorworks(true, false)` を呼び、
//	**呼び出しが戻った後（終了がイベントループで進んでいる途中）に届いた刻みから、もう一度
//	呼ぶ**と何が起きるかを実測する。
//
//	Vectorworks が終わるので、結果はプローブの報告では返らない。**刻みが 1 行ずつ一時ファイルへ
//	書き（書くたびに閉じる）、次に走らせたときにそれを報告する**（#230 の quit-from-timer と
//	同じ作り）。同じものを何度も走らせて段を進める:
//
//	    走らせる → 前の段の書き溜めを報告 → 次の段を仕掛けて戻る → 刻みが呼ぶ（mac 1.5 秒後・Windows 10 秒後）
//
//	段（`kQtwSteps`）。どれも図面を一時ファイルへ保存して閉じ、0 枚になってから 1 度目を呼ぶ:
//	  0 once    1 度だけ呼ぶ（比べるための基準。#230 の nodoc と同じ）
//	  1 first   戻った後に届いた**最初の**刻みから 2 度目を呼ぶ
//	  2 every   戻った後に届いた**刻みのたびに**呼ぶ（最後に届く刻み——`atexit` に近いほう——
//	            を含む。どれが最後かは前もって分からないので、全部で呼ぶ）
//
//	仕掛け（Findings「Timers and Notifications」4 の作法。#230 と同じ）:
//	  * タイマーは 2 本。「駆動」（100 ms）が図面の準備と 1 度目の呼び出しをし、「観測」（50 ms）が
//	    刻みごとに状態を読んで変わったときだけ書く。**戻った後の 2 度目は、先に来たほうの刻みが
//	    呼ぶ**（mac の戻った後の刻みは 1〜数回しか来ないので、どちらか一方に限らない）。
//	  * mac は 2 本とも `kCFRunLoopCommonModes`、Windows は `SetTimer(nullptr, …)`。
//	  * `kNotifyBeforeApplicationQuit`（'BApQ'）・`kNotifyDocClose`（'DCLS'）・`std::atexit`。
//	  * 殻に頼み札を置き、`plugin_module_main` の action（-2 など）を時刻付きで書いてもらう。
//	  * 本体はピン留めする。止める合図は持ち主のファイルで渡す。
//
//	図面は 1 枚だけ開いた状態で走らせる（新規の空図面）。それ以外なら仕掛けずに断る。
//

#include "Probe.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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
	struct QtwStep
	{
		const char* name;
		const char* howTo; // 人に頼むこと
	};

	const QtwStep kQtwSteps[] = {
		{"once", "何もしない（図面を閉じてから確認なしで終わる見込み）。終わったら起動し直して、"
				 "もう一度走らせる"},
		{"first",
		 "何もしない。終わったら（終わらなければ 1 分待って）起動し直して、もう一度走らせる"},
		{"every",
		 "何もしない。終わったら（終わらなければ 1 分待って）起動し直して、もう一度走らせる"},
	};
	const int kQtwStepCount = (int)(sizeof(kQtwSteps) / sizeof(kQtwSteps[0]));

	const long long kQtwObserveMs = 50;
	const long long kQtwDriveMs = 100;
	// 仕掛けてから図面を準備するまで。**メニューから走らせると結果ダイアログが出る**ので、
	// それを閉じる間を取る（ブリッジから走らせる mac は待たなくてよい）。
#if GS_WIN
	const long long kQtwPrepAtMs = 10000;
#else
	const long long kQtwPrepAtMs = 1500;
#endif
	const long long kQtwCallAfterMs = 600;	  // 準備してから終了を呼ぶまで
	const long long kQtwAfterReturnMs = 4000; // 戻った後に見続ける時間
	const long long kQtwMaxMs = 10LL * 60 * 1000;
	const long long kQtwStaleMs = 5000;

	std::filesystem::path QtwDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return dir;
	}
	std::string QtwEventsPath()
	{
		return (QtwDir() / "VwSdkProbes-quit-twice-events.txt").string();
	}
	std::string QtwCountsPath()
	{
		return (QtwDir() / "VwSdkProbes-quit-twice-counts.txt").string();
	}
	std::string QtwOwnerPath()
	{
		return (QtwDir() / "VwSdkProbes-quit-twice-owner.txt").string();
	}
	// 次に仕掛ける段の番号。消せば 0 からやり直す。
	// **殻への頼み札と、殻が書く module_main の記録**（plugin/src/ModuleMain.cpp）。
	// `kPluginModuleDeinit`（-2）は VW が読み込む殻にしか届かないので、殻に書いてもらう。
	std::string QtwModuleRequestPath()
	{
		return (QtwDir() / "VwSdkProbes-module-actions.request").string();
	}
	std::string QtwModuleActionsPath()
	{
		return (QtwDir() / "VwSdkProbes-module-actions.txt").string();
	}
	std::string QtwStepPath()
	{
		return (QtwDir() / "VwSdkProbes-quit-twice-step.txt").string();
	}
	std::string QtwSavePath(const char* step)
	{
		return (QtwDir() / (std::string("VwSdkProbes-quit-twice-") + step + ".vwx")).string();
	}

	std::string QtwReadFirstLine(const std::string& path)
	{
		std::ifstream in(path);
		std::string line;
		std::getline(in, line);
		return line;
	}

	long long QtwWallMs()
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	// 1 行書いて閉じる。毎回開き直すので、Vectorworks ごと終わってもそこまでが残る。
	void QtwAppend(const std::string& path, const std::string& line)
	{
		std::ofstream out(path, std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	std::string QtwYesNo(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string QtwMode()
	{
#if GS_WIN
		return "win";
#else
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return "(ランループの外)";
		char buf[256] = {0};
		std::string out = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8)
							  ? std::string(buf)
							  : std::string("(読めない)");
		::CFRelease(mode);
		return out;
#endif
	}

	int QtwDocCount()
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		return (int)list.GetSize();
	}

	std::string QtwSaveActive(const std::string& target)
	{
		std::error_code ec;
		std::filesystem::remove(target, ec);
		const VectorWorks::Filing::IFileIdentifierPtr fileID(
			VectorWorks::Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(target.c_str())) != kVCOMError_NoError)
			return "IFileIdentifier を作れない";
		const GSError saved = gSDK->SaveActiveDocumentPath(fileID);
		return "SaveActiveDocumentPath=" + std::to_string((long long)saved) +
			   "（0 が成功） 保存先に在る=" + QtwYesNo(std::filesystem::exists(target, ec));
	}

	// -----------------------------------------------------------------------
	// 刻みが持ち越す状態。**意図して leak する**（タイマーはプローブより長生き）。
	enum QtwPhase
	{
		kQtwPhaseWaiting,  // 準備待ち
		kQtwPhasePrepared, // 図面を準備した。終了を呼ぶ待ち
		kQtwPhaseCalling,  // 呼び出しの中
		kQtwPhaseReturned, // 呼び出しから戻った
		kQtwPhaseDone,
	};

	struct QtwState
	{
		std::string owner;
		std::string events;
		std::string counts;
		int step = 0;
		std::chrono::steady_clock::time_point armedAt;
		QtwPhase phase = kQtwPhaseWaiting;
		long long prepAt = -1;
		long long callAt = -1;
		long long returnedAt = -1;
		int depth = 0;			  // 終了の呼び出しが何段スタックに載っているか
		long long againCalls = 0; // 戻った後に呼んだ回数
		long long observeTicks = 0;
		long long driveTicks = 0;
		long long driveReentries = 0; // 呼び出しの最中に駆動が入り直した回数
		long long ticksInCall = 0;	  // 呼び出しの最中に観測が刻んだ回数
		long long ticksAfterReturn = 0;
		long long maxGapMs = 0;
		long long lastObserveMs = -1;
		long long lastTickWall = 0;
		long long notifyQuit = 0;
		long long notifyDocClose = 0;
		std::string lastKey;
		std::string lastPreKey;
		bool disarmed = false;
	};

	QtwState* gQtwState = nullptr;
#if GS_WIN
	UINT_PTR gQtwObserveTimer = 0;
	UINT_PTR gQtwDriveTimer = 0;
#else
	CFRunLoopTimerRef gQtwObserveTimer = nullptr;
	CFRunLoopTimerRef gQtwDriveTimer = nullptr;
#endif

	long long QtwElapsedMs(const QtwState& st)
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - st.armedAt)
			.count();
	}

	// wall_ms は殻の module_main の記録と突き合わせるため。
	std::string QtwStamp(const QtwState& st)
	{
		return "t_ms=" + std::to_string(QtwElapsedMs(st)) + " depth=" + std::to_string(st.depth) +
			   " wall_ms=" + std::to_string(QtwWallMs());
	}

	void QtwWriteCounts(const QtwState& st)
	{
		std::ofstream out(st.counts, std::ios::trunc);
		if (!out)
			return;
		out << "heartbeat_wall_ms " << QtwWallMs() << "\n"
			<< "step " << kQtwSteps[st.step].name << "\n"
			<< "elapsed_ms " << QtwElapsedMs(st) << "\n"
			<< "phase " << (int)st.phase << "\n"
			<< "observe_ticks " << st.observeTicks << "\n"
			<< "drive_ticks " << st.driveTicks << "\n"
			<< "drive_reentries_in_call " << st.driveReentries << "\n"
			<< "observe_ticks_in_call " << st.ticksInCall << "\n"
			<< "observe_ticks_after_return " << st.ticksAfterReturn << "\n"
			<< "max_gap_ms " << st.maxGapMs << "\n"
			<< "last_tick_wall_ms " << st.lastTickWall << "\n"
			<< "notify_before_app_quit " << st.notifyQuit << "\n"
			<< "notify_doc_close " << st.notifyDocClose << "\n"
			<< "again_calls " << st.againCalls << "\n";
	}

	bool QtwOwnerOk(QtwState* st)
	{
		if (QtwReadFirstLine(QtwOwnerPath()) == st->owner)
			return true;
		st->disarmed = true; // 書き溜めはもう別の持ち主のもの。何も書かずに止まる
		return false;
	}

	void QtwNotifyBeforeQuit(StatusID, StatusData);
	void QtwNotifyDocClose(StatusID, StatusData);

	void QtwStopTimers()
	{
#if GS_WIN
		if (gQtwObserveTimer != 0)
			::KillTimer(nullptr, gQtwObserveTimer);
		if (gQtwDriveTimer != 0)
			::KillTimer(nullptr, gQtwDriveTimer);
		gQtwObserveTimer = 0;
		gQtwDriveTimer = 0;
#else
		for (CFRunLoopTimerRef* t : {&gQtwObserveTimer, &gQtwDriveTimer})
		{
			if (*t != nullptr)
			{
				::CFRunLoopTimerInvalidate(*t);
				::CFRelease(*t);
				*t = nullptr;
			}
		}
#endif
		gSDK->UnregisterNotificationProcedure(&QtwNotifyBeforeQuit, kNotifyBeforeApplicationQuit);
		gSDK->UnregisterNotificationProcedure(&QtwNotifyDocClose, kNotifyDocClose);
	}

	void QtwDisarm(const std::string& why)
	{
		QtwState* st = gQtwState;
		const bool write = st != nullptr && !st->disarmed;
		if (st != nullptr)
			st->disarmed = true;
		QtwStopTimers();
		if (write)
		{
			QtwWriteCounts(*st);
			QtwAppend(st->events, "disarm " + QtwStamp(*st) + " why=" + why);
		}
	}

	// --- 通知・atexit ------------------------------------------------------
	void QtwNotifyLine(const char* what, long long& counter)
	{
		QtwState* st = gQtwState;
		if (st == nullptr || st->disarmed)
			return;
		++counter;
		QtwAppend(st->events, std::string("notify ") + what + " " + QtwStamp(*st) +
								  " n=" + std::to_string(counter) + " mode=" + QtwMode() +
								  " → GetOpenFilesList を呼ぶ");
		QtwAppend(st->events,
				  std::string("notify ") + what + " docs=" + std::to_string(QtwDocCount()));
		QtwWriteCounts(*st);
	}
	void QtwNotifyBeforeQuit(StatusID, StatusData)
	{
		if (gQtwState != nullptr)
			QtwNotifyLine("BeforeApplicationQuit", gQtwState->notifyQuit);
	}
	void QtwNotifyDocClose(StatusID, StatusData)
	{
		if (gQtwState != nullptr)
			QtwNotifyLine("DocClose", gQtwState->notifyDocClose);
	}
	void QtwAtExit()
	{
		// 刻みを止めた後でも、書き溜めがまだこの像のものなら書く（gSDK は呼ばない）。
		QtwState* st = gQtwState;
		if (st == nullptr || QtwReadFirstLine(QtwOwnerPath()) != st->owner)
			return;
		QtwWriteCounts(*st);
		QtwAppend(st->events, "atexit " + QtwStamp(*st) + " observe_ticks=" +
								  std::to_string(st->observeTicks) + " (gSDK は呼ばない)");
	}

	// --- 終了の呼び出し ----------------------------------------------------
	void QtwCallQuit(QtwState* st, const std::string& who)
	{
		const long long begin = QtwElapsedMs(*st);
		QtwWriteCounts(*st);
		QtwAppend(st->events, "quit-call-begin " + QtwStamp(*st) + " who=" + who +
								  " mode=" + QtwMode() +
								  " undo=" + QtwYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()) +
								  " → CloseAllFilesAndQuitVectorworks(true, false)");
		++st->depth;
		gSDK->CloseAllFilesAndQuitVectorworks(true, false);
		--st->depth;
		const long long end = QtwElapsedMs(*st);
		QtwAppend(st->events, "quit-call-returned " + QtwStamp(*st) + " who=" + who +
								  " took_ms=" + std::to_string(end - begin) + " mode=" + QtwMode());
		QtwWriteCounts(*st);
	}

	// **戻った後に届いた刻みから、もう一度呼ぶ**（段 first は最初の 1 回だけ、every は毎回）。
	// 呼び出しの最中（depth > 0）には呼ばない——入れ子は #230 で測った別の問い。
	void QtwMaybeAgain(QtwState* st, const char* who)
	{
		if (st->depth > 0 || st->disarmed)
			return;
		const std::string step = kQtwSteps[st->step].name;
		const bool again = (step == "first" && st->againCalls == 0) ||
						   (step == "every" && st->againCalls < 50); // 終わらない場合の歯止め
		if (!again)
			return;
		++st->againCalls;
		const long long sinceReturn = QtwElapsedMs(*st) - st->returnedAt;
		QtwAppend(st->events, "again n=" + std::to_string(st->againCalls) +
								  " since_first_return_ms=" + std::to_string(sinceReturn));
		QtwCallQuit(st,
					std::string(who) + "(戻った後 " + std::to_string(st->againCalls) + " 度目)");
		QtwAppend(st->events, "after-again docs=" + std::to_string(QtwDocCount()) + " layer=" +
								  (gSDK->GetCurrentLayer() == nullptr ? "nullptr" : "ok"));
	}

	// 駆動: 図面の準備と終了の呼び出し。
	void QtwDriveTick()
	{
		QtwState* st = gQtwState;
		if (st == nullptr || st->disarmed || !QtwOwnerOk(st))
			return;
		++st->driveTicks;
		const long long now = QtwElapsedMs(*st);

		if (st->depth > 0)
		{
			// **呼び出しの最中に駆動が入り直した。**
			++st->driveReentries;
			if (st->driveReentries <= 3)
				QtwAppend(st->events, "drive-reentry " + QtwStamp(*st) +
										  " n=" + std::to_string(st->driveReentries) +
										  " mode=" + QtwMode());
			return;
		}

		if (st->phase == kQtwPhaseWaiting && now >= kQtwPrepAtMs)
		{
			const int docs = QtwDocCount();
			const bool wantDocs = docs == 1;
			if (!wantDocs)
			{
				QtwAppend(st->events, "abort " + QtwStamp(*st) + " docs=" + std::to_string(docs) +
										  "（図面を 1 枚だけ開いた状態で走らせる）");
				QtwDisarm("docs-not-1");
				return;
			}
			QtwAppend(st->events,
					  "prep " + QtwStamp(*st) + " " + QtwSaveActive(QtwSavePath("nodoc")));
			const bool closed = gSDK->CloseDocument();
			QtwAppend(st->events, "prep CloseDocument=" + QtwYesNo(closed) +
									  " docs=" + std::to_string(QtwDocCount()));
			st->phase = kQtwPhasePrepared;
			st->prepAt = now;
			return;
		}

		if (st->phase == kQtwPhasePrepared && now - st->prepAt >= kQtwCallAfterMs)
		{
			if (QtwDocCount() != 0)
			{
				if (now - st->prepAt > 5000)
				{
					QtwAppend(st->events, "abort " + QtwStamp(*st) + " 図面が 0 枚にならない");
					QtwDisarm("doc-not-closed");
				}
				return;
			}
			st->phase = kQtwPhaseCalling;
			st->callAt = now;
			QtwCallQuit(st, "drive");
			if (st->disarmed)
				return;
			st->phase = kQtwPhaseReturned;
			st->returnedAt = QtwElapsedMs(*st);
			QtwAppend(st->events, "after-return docs=" + std::to_string(QtwDocCount()) + " layer=" +
									  (gSDK->GetCurrentLayer() == nullptr ? "nullptr" : "ok"));
			return;
		}

		if (st->phase == kQtwPhaseReturned)
		{
			QtwMaybeAgain(st, "drive");
			if (st->disarmed)
				return;
		}

		// **終わっていく回（図面が 0 枚で戻った）では止めない**——止めると atexit も書かない
		// ので、殻の -2 と atexit の前後が分からなくなる（実際に踏んだ）。
		if (st->phase == kQtwPhaseReturned && now - st->returnedAt >= kQtwAfterReturnMs &&
			(QtwDocCount() != 0 || now - st->returnedAt >= 60000))
		{
			st->phase = kQtwPhaseDone;
			QtwDisarm("observed-after-return");
			return;
		}
		if (now > kQtwMaxMs)
			QtwDisarm("time-limit");
	}

	// 観測: 刻みごとに読み、変わったときだけ書く。
	void QtwObserveTick()
	{
		QtwState* st = gQtwState;
		if (st == nullptr || st->disarmed || !QtwOwnerOk(st))
			return;
		++st->observeTicks;
		const long long now = QtwElapsedMs(*st);
		if (st->lastObserveMs >= 0 && now - st->lastObserveMs > st->maxGapMs)
			st->maxGapMs = now - st->lastObserveMs;
		st->lastObserveMs = now;
		st->lastTickWall = QtwWallMs();
		if (st->depth > 0)
			++st->ticksInCall;
		if (st->phase == kQtwPhaseReturned)
			++st->ticksAfterReturn;
		const bool quitting = st->phase >= kQtwPhaseCalling;
		const std::string mode = QtwMode();

		// **呼ぶ前に書く**（終了の後は、状況が変わった刻みで 1 度）。落ちたら、この行が最後に残る。
		const std::string preKey =
			std::to_string(st->depth) + "/" + std::to_string(st->phase) + "/" + mode;
		if (quitting && preKey != st->lastPreKey)
		{
			QtwAppend(st->events, "pre " + QtwStamp(*st) + " phase=" + std::to_string(st->phase) +
									  " mode=" + mode +
									  " → GetOpenFilesList / IsCurrentlyBuildingAnUndoEvent / "
									  "GetCurrentLayer を呼ぶ");
			st->lastPreKey = preKey;
		}
		const int docs = QtwDocCount();
		const bool undo = gSDK->IsCurrentlyBuildingAnUndoEvent();
		const bool layer = gSDK->GetCurrentLayer() != nullptr;

		std::ostringstream key;
		key << "depth=" << st->depth << " phase=" << st->phase << " docs=" << docs
			<< " undo=" << QtwYesNo(undo) << " layer=" << (layer ? "ok" : "nullptr")
			<< " mode=" << mode;
		if (key.str() != st->lastKey)
		{
			QtwAppend(st->events, "state t_ms=" + std::to_string(now) + " tick=" +
									  std::to_string(st->observeTicks) + " " + key.str());
			st->lastKey = key.str();
		}

		if (st->phase == kQtwPhaseReturned)
			QtwMaybeAgain(st, "observe");

		if (quitting || st->observeTicks % 10 == 0)
			QtwWriteCounts(*st);
	}

#if GS_WIN
	VOID CALLBACK QtwWinObserve(HWND, UINT, UINT_PTR, DWORD)
	{
		QtwObserveTick();
	}
	VOID CALLBACK QtwWinDrive(HWND, UINT, UINT_PTR, DWORD)
	{
		QtwDriveTick();
	}
#else
	void QtwCFObserve(CFRunLoopTimerRef, void*)
	{
		QtwObserveTick();
	}
	void QtwCFDrive(CFRunLoopTimerRef, void*)
	{
		QtwDriveTick();
	}
#endif

	std::string QtwPinSelf(std::string& pinnedPath)
	{
#if GS_WIN
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&QtwPinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した（GetLastError=" +
				   std::to_string((unsigned long)::GetLastError()) + "）";
		char buf[MAX_PATH] = {0};
		const DWORD n = ::GetModuleFileNameA(mod, buf, MAX_PATH);
		pinnedPath = (n > 0 && n < MAX_PATH) ? std::string(buf) : std::string("(名前が読めない)");
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&QtwPinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		void* handle = ::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
		if (handle == nullptr)
			return std::string("dlopen(RTLD_NOLOAD) が失敗した: ") + info.dli_fname;
		pinnedPath = info.dli_fname;
		return std::string();
#endif
	}

	std::vector<std::string> QtwReadLines(const std::string& path)
	{
		std::vector<std::string> lines;
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line))
			lines.push_back(line);
		return lines;
	}

	long long QtwCountValue(const std::vector<std::string>& counts, const std::string& name)
	{
		const std::string head = name + " ";
		for (const std::string& line : counts)
		{
			if (line.rfind(head, 0) == 0)
				return std::atoll(line.c_str() + head.size());
		}
		return 0;
	}

	// 書き溜めを報告して消す。**その段を測り損ねていたら（abort）真を返す**——同じ段を仕掛け直す。
	bool QtwReport(vwprobe::Report& probe)
	{
		std::error_code ec;
		std::filesystem::remove(QtwOwnerPath(), ec); // 生きている刻みは次の刻みで止まる
		const std::vector<std::string> counts = QtwReadLines(QtwCountsPath());
		const long long beat = QtwCountValue(counts, "heartbeat_wall_ms");
		const long long age = QtwWallMs() - beat;
		const bool alive = beat > 0 && age >= 0 && age < kQtwStaleMs;
		const std::vector<std::string> events = QtwReadLines(QtwEventsPath());
		bool exited = false; // atexit まで届いた＝Vectorworks は終わった
		bool stopped = false; // 刻みが自分で止まった（Vectorworks は終わっていない）
		for (const std::string& line : events)
		{
			exited = exited || line.rfind("atexit ", 0) == 0;
			stopped = stopped || line.rfind("disarm ", 0) == 0;
		}
		probe.log(std::string("■ 前の段の書き溜め（心拍 ") + std::to_string(age) + " ms 前・" +
				  (exited ? "atexit まで届いた——Vectorworks は終わった"
				   : stopped ? "刻みは自分で止まった——Vectorworks は終わっていない"
				   : alive ? "刻みはまだ生きている"
						   : "刻みが途中で消えた——atexit も無い（落ちた見込み）") +
				  "）");
		probe.log("  -- 経過 --");
		const std::size_t kMax = 300;
		for (std::size_t i = 0; i < events.size() && i < kMax; ++i)
			probe.log("  " + events[i]);
		if (events.size() > kMax)
			probe.log("  …（" + std::to_string(events.size() - kMax) + " 行省略）");
		probe.log("  -- 数 --");
		for (const std::string& line : counts)
			probe.log("  " + line);
		const long long lastTick = QtwCountValue(counts, "last_tick_wall_ms");
		if (lastTick > 0 && beat > 0)
			probe.log("  last_tick_to_heartbeat_ms " + std::to_string(beat - lastTick));
		std::filesystem::remove(QtwEventsPath(), ec);
		std::filesystem::remove(QtwCountsPath(), ec);

		// 殻の module_main の記録（終了の回なら、終了と次の起動の分が入っている）。
		// -3 / -4 / -5（インタフェースの登録・問い合わせ・解放）は数が多いので数だけにする。
		probe.log("  -- 殻の module_main（wall_ms。-1 初期化 / -2 後始末）--");
		const std::vector<std::string> actions = QtwReadLines(QtwModuleActionsPath());
		if (actions.empty())
			probe.log("  （記録なし——殻が古い（札を読まない）か、module_main が呼ばれなかった）");
		long long others = 0;
		std::string lastOther;
		for (const std::string& line : actions)
		{
			const bool initDeinit = line.find("action=-1 ") != std::string::npos ||
									line.find("action=-2 ") != std::string::npos;
			if (initDeinit)
			{
				if (others > 0)
					probe.log("  （その他の action " + std::to_string(others) +
							  " 回。最後: " + lastOther + "）");
				others = 0;
				probe.log("  " + line);
			}
			else
			{
				++others;
				lastOther = line;
			}
		}
		if (others > 0)
			probe.log("  （その他の action " + std::to_string(others) + " 回。最後: " + lastOther +
					  "）");
		std::filesystem::remove(QtwModuleRequestPath(), ec);
		std::filesystem::remove(QtwModuleActionsPath(), ec);
		for (const std::string& line : events)
		{
			if (line.rfind("abort ", 0) == 0)
				return true;
		}
		return false;
	}

	void QtwArm(vwprobe::Report& probe, int step)
	{
		std::string pinnedPath;
		const std::string trouble = QtwPinSelf(pinnedPath);
		if (!trouble.empty())
		{
			probe.fail("本体をピン留めできなかったので仕掛けない（" + trouble + "）");
			return;
		}
		gQtwState = new QtwState();
		QtwState* st = gQtwState;
		st->owner = std::to_string(QtwWallMs()) + "-" + VW_PAYLOAD_BUILD_ID;
		st->events = QtwEventsPath();
		st->counts = QtwCountsPath();
		st->step = step;
		st->armedAt = std::chrono::steady_clock::now();
		{
			std::ofstream out(QtwOwnerPath(), std::ios::trunc);
			out << st->owner << "\n";
		}
		{
			std::ofstream out(QtwStepPath(), std::ios::trunc);
			out << step << "\n";
		}
		std::error_code ec;
		std::filesystem::remove(st->events, ec);
		std::filesystem::remove(QtwModuleActionsPath(), ec);
		{
			std::ofstream out(QtwModuleRequestPath(), std::ios::trunc);
			out << st->owner << "\n";
		}
		QtwAppend(st->events, std::string("arm step=") + kQtwSteps[step].name +
								  " owner=" + st->owner + " build=" + VW_PAYLOAD_BUILD_ID +
								  " pinned=" + pinnedPath);
		QtwWriteCounts(*st);

		const Boolean q =
			gSDK->RegisterNotificationProcedure(&QtwNotifyBeforeQuit, kNotifyBeforeApplicationQuit);
		const Boolean c = gSDK->RegisterNotificationProcedure(&QtwNotifyDocClose, kNotifyDocClose);
		QtwAppend(st->events, std::string("register BeforeApplicationQuit=") + QtwYesNo(q != 0) +
								  " DocClose=" + QtwYesNo(c != 0));
		static bool sAtExit = false; // 像ごとに 1 度
		if (!sAtExit)
			sAtExit = std::atexit(&QtwAtExit) == 0;

#if GS_WIN
		gQtwObserveTimer = ::SetTimer(nullptr, 0, (UINT)kQtwObserveMs, &QtwWinObserve);
		gQtwDriveTimer = ::SetTimer(nullptr, 0, (UINT)kQtwDriveMs, &QtwWinDrive);
		const bool armed = gQtwObserveTimer != 0 && gQtwDriveTimer != 0;
#else
		const CFAbsoluteTime o = (CFAbsoluteTime)kQtwObserveMs / 1000.0;
		const CFAbsoluteTime d = (CFAbsoluteTime)kQtwDriveMs / 1000.0;
		const CFAbsoluteTime now = ::CFAbsoluteTimeGetCurrent();
		gQtwObserveTimer =
			::CFRunLoopTimerCreate(kCFAllocatorDefault, now + o, o, 0, 0, &QtwCFObserve, nullptr);
		gQtwDriveTimer =
			::CFRunLoopTimerCreate(kCFAllocatorDefault, now + d, d, 0, 0, &QtwCFDrive, nullptr);
		const bool armed = gQtwObserveTimer != nullptr && gQtwDriveTimer != nullptr;
		if (armed)
		{
			::CFRunLoopAddTimer(::CFRunLoopGetMain(), gQtwObserveTimer, kCFRunLoopCommonModes);
			::CFRunLoopAddTimer(::CFRunLoopGetMain(), gQtwDriveTimer, kCFRunLoopCommonModes);
		}
#endif
		if (!armed)
		{
			probe.fail("タイマーを仕掛けられなかった");
			QtwDisarm("arm-failed");
			return;
		}
		probe.log("");
		probe.log(std::string("■ 段 ") + std::to_string(step) + "/" +
				  std::to_string(kQtwStepCount - 1) + " 「" + kQtwSteps[step].name +
				  "」を仕掛けた。刻みが図面を保存して閉じ、その 0.6 秒後に終了を呼ぶ。");
		probe.log(std::string("このあと: 結果ダイアログを閉じて待つ（") +
				  std::to_string(kQtwPrepAtMs / 1000) + " 秒ほどで刻みが動く）。" +
				  kQtwSteps[step].howTo);
	}
} // namespace

VW_PROBE("quit-twice", "戻った後に終了をもう一度", "段を進めながら終了の振る舞いを記録")
{
	probe.log("コマンドの中: docs=" + std::to_string(QtwDocCount()) + " mode=" + QtwMode());

	int next = 0;
	std::error_code ec;
	if (std::filesystem::exists(QtwEventsPath(), ec))
	{
		const std::string measured = QtwReadFirstLine(QtwStepPath());
		const bool aborted = QtwReport(probe);
		next = measured.empty() ? 0 : std::atoi(measured.c_str()) + (aborted ? 0 : 1);
	}
	else
	{
		const std::string saved = QtwReadFirstLine(QtwStepPath());
		next = saved.empty() ? 0 : std::atoi(saved.c_str());
	}
	if (next >= kQtwStepCount)
	{
		std::filesystem::remove(QtwStepPath(), ec);
		probe.log("");
		probe.log("■ 全段を測り終えた（次に走らせると段 0 から始める）");
		return;
	}
	if (next < 0)
		next = 0;
	QtwArm(probe, next);
}
