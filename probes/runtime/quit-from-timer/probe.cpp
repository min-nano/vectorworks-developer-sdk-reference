//
//	probes/runtime/quit-from-timer/probe.cpp
//
//	[issue #230] **OS タイマーの刻みから `gSDK->CloseAllFilesAndQuitVectorworks(true, false)`
//	を呼ぶと何が起きるか**を実測する。
//
//	Vectorworks が終わる場面があるので、結果はプローブの報告では返らない。**刻みが 1 行ずつ
//	一時ファイルへ書き（書くたびに閉じる）、次に走らせたときにそれを報告する。** だから
//	このプローブは**同じものを何度も走らせて段を進める**作りになっている:
//
//	    走らせる → 前の段の書き溜めを報告 → 次の段を仕掛けて戻る → 1.5 秒後に刻みが呼ぶ
//
//	段（`kQftSteps`）:
//	  0 cancel    図面を汚してから呼ぶ。**人が保存の確認で「キャンセル」を押す**。
//	              → 戻るか・戻った後も刻みと `gSDK` が生きているか・図面は残るか（問い 2）
//	              → 確認の最中に刻みは届くか・そのときの undo の旗とモード（問い 4）
//	  1 nested    0 と同じ。確認の最中の刻みから **2 度目を呼ぶ**。人は出たものを全部
//	              キャンセルする（問い 4 の後半）
//	  2 dontsave  図面を汚してから呼ぶ。**人が「保存しない」を押す** → 終わる（問い 1・5）
//	  3 clean     図面を一時ファイルへ保存してから呼ぶ（未保存の変更なし。問い 3）
//	  4 nodoc     図面を一時ファイルへ保存して閉じ、0 枚になってから呼ぶ（問い 3）
//
//	仕掛け（Findings「Timers and Notifications」4 の作法）:
//	  * **タイマーは 2 本。** 「駆動」（100 ms）が図面の準備と終了の呼び出しをし、「観測」
//	    （50 ms）が刻みごとに状態を読んで、変わったときだけ書く。**分けるのは、mac の
//	    `CFRunLoopTimer` は自分の callout の最中には同じタイマーを発火しない見込みだから**
//	    ——1 本だと「確認の最中に刻みが来ない」のか「そのタイマーだけ来ない」のかを
//	    分けられない。駆動が呼び出しの最中に入り直したら、それも数える（実プラグインは
//	    1 本のタイマーの中から呼ぶ設計なので、そちらの答えにもなる）。
//	  * mac は 2 本とも `kCFRunLoopCommonModes`、Windows は `SetTimer(nullptr, …)`。
//	  * `kNotifyBeforeApplicationQuit`（'BApQ'）と `kNotifyDocClose`（'DCLS'）を登録し、
//	    届いたら 1 行書く。`std::atexit` も登録する（プロセスが exit() で終わるか）。
//	  * 終了を呼んだ後は、心拍（数のファイル）を**毎刻み**書き直す——最後の刻みの時刻が残る。
//	  * 本体はピン留めする（殻はプローブが終わると本体を降ろすため）。止める合図は札
//	    （持ち主のファイル）で渡す（殻は実行のたびに本体を別名で読み込むので、静的変数を
//	    共有しない。#224 で踏んだ）。
//
//	図面は 1 枚だけ開いた状態で走らせる（新規の空図面）。それ以外なら仕掛けずに断る
//	——利用者の図面を閉じたり、保存の確認に巻き込んだりしないため。
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
	struct QftStep
	{
		const char* name;
		const char* howTo; // 人に頼むこと
	};

	const QftStep kQftSteps[] = {
		{"cancel",
		 "保存の確認が出たら「キャンセル」を押す。そのまま 5 秒置いてから、もう一度走らせる"},
		{"nested",
		 "保存の確認が出たら 3 秒待ち、**出ているものを全部「キャンセル」で閉じる**。何が出たか"
		 "（確認が 2 つ重なったか等）を覚えておく。5 秒置いてから、もう一度走らせる"},
		{"dontsave",
		 "保存の確認が出たら「保存しない」を押す（Vectorworks が終わる）。起動し直して、"
		 "もう一度走らせる"},
		{"clean", "何もしない（確認なしで終わる見込み）。終わったら起動し直して、もう一度走らせる"},
		{"nodoc", "何もしない（図面を閉じてから確認なしで終わる見込み）。終わったら起動し直して、"
				  "もう一度走らせる"},
	};
	const int kQftStepCount = (int)(sizeof(kQftSteps) / sizeof(kQftSteps[0]));

	const long long kQftObserveMs = 50;
	const long long kQftDriveMs = 100;
	const long long kQftPrepAtMs = 1500;	  // 仕掛けてから図面を準備するまで
	const long long kQftCallAfterMs = 600;	  // 準備してから終了を呼ぶまで
	const long long kQftNestedAtMs = 1500;	  // 確認の最中、2 度目を呼ぶまで
	const long long kQftAfterReturnMs = 4000; // 戻った後に見続ける時間
	const long long kQftMaxMs = 10LL * 60 * 1000;
	const long long kQftStaleMs = 5000;

	std::filesystem::path QftDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return dir;
	}
	std::string QftEventsPath()
	{
		return (QftDir() / "VwSdkProbes-quit-from-timer-events.txt").string();
	}
	std::string QftCountsPath()
	{
		return (QftDir() / "VwSdkProbes-quit-from-timer-counts.txt").string();
	}
	std::string QftOwnerPath()
	{
		return (QftDir() / "VwSdkProbes-quit-from-timer-owner.txt").string();
	}
	// 次に仕掛ける段の番号。消せば 0 からやり直す。
	std::string QftStepPath()
	{
		return (QftDir() / "VwSdkProbes-quit-from-timer-step.txt").string();
	}
	std::string QftSavePath(const char* step)
	{
		return (QftDir() / (std::string("VwSdkProbes-quit-from-timer-") + step + ".vwx")).string();
	}

	std::string QftReadFirstLine(const std::string& path)
	{
		std::ifstream in(path);
		std::string line;
		std::getline(in, line);
		return line;
	}

	long long QftWallMs()
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	// 1 行書いて閉じる。毎回開き直すので、Vectorworks ごと終わってもそこまでが残る。
	void QftAppend(const std::string& path, const std::string& line)
	{
		std::ofstream out(path, std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	std::string QftYesNo(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string QftMode()
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

	int QftDocCount()
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		return (int)list.GetSize();
	}

	std::string QftSaveActive(const std::string& target)
	{
		std::error_code ec;
		std::filesystem::remove(target, ec);
		const VectorWorks::Filing::IFileIdentifierPtr fileID(
			VectorWorks::Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(target.c_str())) != kVCOMError_NoError)
			return "IFileIdentifier を作れない";
		const GSError saved = gSDK->SaveActiveDocumentPath(fileID);
		return "SaveActiveDocumentPath=" + std::to_string((long long)saved) +
			   "（0 が成功） 保存先に在る=" + QftYesNo(std::filesystem::exists(target, ec));
	}

	// -----------------------------------------------------------------------
	// 刻みが持ち越す状態。**意図して leak する**（タイマーはプローブより長生き）。
	enum QftPhase
	{
		kQftPhaseWaiting,  // 準備待ち
		kQftPhasePrepared, // 図面を準備した。終了を呼ぶ待ち
		kQftPhaseCalling,  // 呼び出しの中
		kQftPhaseReturned, // 呼び出しから戻った
		kQftPhaseDone,
	};

	struct QftState
	{
		std::string owner;
		std::string events;
		std::string counts;
		int step = 0;
		std::chrono::steady_clock::time_point armedAt;
		QftPhase phase = kQftPhaseWaiting;
		long long prepAt = -1;
		long long callAt = -1;
		long long returnedAt = -1;
		int depth = 0; // 終了の呼び出しが何段スタックに載っているか
		bool nestedDone = false;
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

	QftState* gQftState = nullptr;
#if GS_WIN
	UINT_PTR gQftObserveTimer = 0;
	UINT_PTR gQftDriveTimer = 0;
#else
	CFRunLoopTimerRef gQftObserveTimer = nullptr;
	CFRunLoopTimerRef gQftDriveTimer = nullptr;
#endif

	long long QftElapsedMs(const QftState& st)
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - st.armedAt)
			.count();
	}

	std::string QftStamp(const QftState& st)
	{
		return "t_ms=" + std::to_string(QftElapsedMs(st)) + " depth=" + std::to_string(st.depth);
	}

	void QftWriteCounts(const QftState& st)
	{
		std::ofstream out(st.counts, std::ios::trunc);
		if (!out)
			return;
		out << "heartbeat_wall_ms " << QftWallMs() << "\n"
			<< "step " << kQftSteps[st.step].name << "\n"
			<< "elapsed_ms " << QftElapsedMs(st) << "\n"
			<< "phase " << (int)st.phase << "\n"
			<< "observe_ticks " << st.observeTicks << "\n"
			<< "drive_ticks " << st.driveTicks << "\n"
			<< "drive_reentries_in_call " << st.driveReentries << "\n"
			<< "observe_ticks_in_call " << st.ticksInCall << "\n"
			<< "observe_ticks_after_return " << st.ticksAfterReturn << "\n"
			<< "max_gap_ms " << st.maxGapMs << "\n"
			<< "last_tick_wall_ms " << st.lastTickWall << "\n"
			<< "notify_before_app_quit " << st.notifyQuit << "\n"
			<< "notify_doc_close " << st.notifyDocClose << "\n";
	}

	bool QftOwnerOk(QftState* st)
	{
		if (QftReadFirstLine(QftOwnerPath()) == st->owner)
			return true;
		st->disarmed = true; // 書き溜めはもう別の持ち主のもの。何も書かずに止まる
		return false;
	}

	void QftNotifyBeforeQuit(StatusID, StatusData);
	void QftNotifyDocClose(StatusID, StatusData);

	void QftStopTimers()
	{
#if GS_WIN
		if (gQftObserveTimer != 0)
			::KillTimer(nullptr, gQftObserveTimer);
		if (gQftDriveTimer != 0)
			::KillTimer(nullptr, gQftDriveTimer);
		gQftObserveTimer = 0;
		gQftDriveTimer = 0;
#else
		for (CFRunLoopTimerRef* t : {&gQftObserveTimer, &gQftDriveTimer})
		{
			if (*t != nullptr)
			{
				::CFRunLoopTimerInvalidate(*t);
				::CFRelease(*t);
				*t = nullptr;
			}
		}
#endif
		gSDK->UnregisterNotificationProcedure(&QftNotifyBeforeQuit, kNotifyBeforeApplicationQuit);
		gSDK->UnregisterNotificationProcedure(&QftNotifyDocClose, kNotifyDocClose);
	}

	void QftDisarm(const std::string& why)
	{
		QftState* st = gQftState;
		const bool write = st != nullptr && !st->disarmed;
		if (st != nullptr)
			st->disarmed = true;
		QftStopTimers();
		if (write)
		{
			QftWriteCounts(*st);
			QftAppend(st->events, "disarm " + QftStamp(*st) + " why=" + why);
		}
	}

	// --- 通知・atexit ------------------------------------------------------
	void QftNotifyLine(const char* what, long long& counter)
	{
		QftState* st = gQftState;
		if (st == nullptr || st->disarmed)
			return;
		++counter;
		QftAppend(st->events, std::string("notify ") + what + " " + QftStamp(*st) +
								  " n=" + std::to_string(counter) + " mode=" + QftMode() +
								  " → GetOpenFilesList を呼ぶ");
		QftAppend(st->events,
				  std::string("notify ") + what + " docs=" + std::to_string(QftDocCount()));
		QftWriteCounts(*st);
	}
	void QftNotifyBeforeQuit(StatusID, StatusData)
	{
		if (gQftState != nullptr)
			QftNotifyLine("BeforeApplicationQuit", gQftState->notifyQuit);
	}
	void QftNotifyDocClose(StatusID, StatusData)
	{
		if (gQftState != nullptr)
			QftNotifyLine("DocClose", gQftState->notifyDocClose);
	}
	void QftAtExit()
	{
		QftState* st = gQftState;
		if (st == nullptr || st->disarmed)
			return;
		QftWriteCounts(*st);
		QftAppend(st->events, "atexit " + QftStamp(*st) + " observe_ticks=" +
								  std::to_string(st->observeTicks) + " (gSDK は呼ばない)");
	}

	// --- 終了の呼び出し ----------------------------------------------------
	void QftCallQuit(QftState* st, const std::string& who)
	{
		const long long begin = QftElapsedMs(*st);
		QftWriteCounts(*st);
		QftAppend(st->events, "quit-call-begin " + QftStamp(*st) + " who=" + who +
								  " mode=" + QftMode() +
								  " undo=" + QftYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()) +
								  " → CloseAllFilesAndQuitVectorworks(true, false)");
		++st->depth;
		gSDK->CloseAllFilesAndQuitVectorworks(true, false);
		--st->depth;
		const long long end = QftElapsedMs(*st);
		QftAppend(st->events, "quit-call-returned " + QftStamp(*st) + " who=" + who +
								  " took_ms=" + std::to_string(end - begin) + " mode=" + QftMode());
		QftWriteCounts(*st);
	}

	// 駆動: 図面の準備と終了の呼び出し。
	void QftDriveTick()
	{
		QftState* st = gQftState;
		if (st == nullptr || st->disarmed || !QftOwnerOk(st))
			return;
		++st->driveTicks;
		const long long now = QftElapsedMs(*st);
		const std::string step = kQftSteps[st->step].name;

		if (st->depth > 0)
		{
			// **呼び出しの最中に駆動が入り直した。**
			++st->driveReentries;
			if (st->driveReentries <= 3)
				QftAppend(st->events, "drive-reentry " + QftStamp(*st) +
										  " n=" + std::to_string(st->driveReentries) +
										  " mode=" + QftMode());
			if (step == "nested" && !st->nestedDone && now - st->callAt >= kQftNestedAtMs)
			{
				st->nestedDone = true;
				QftCallQuit(st, "drive(入り直し)");
			}
			return;
		}

		if (st->phase == kQftPhaseWaiting && now >= kQftPrepAtMs)
		{
			const int docs = QftDocCount();
			const bool wantDocs = docs == 1;
			if (!wantDocs)
			{
				QftAppend(st->events, "abort " + QftStamp(*st) + " docs=" + std::to_string(docs) +
										  "（図面を 1 枚だけ開いた状態で走らせる）");
				QftDisarm("docs-not-1");
				return;
			}
			if (step == "cancel" || step == "nested" || step == "dontsave")
			{
				// **刻みの中で汚す。** ブリッジは走らせた後に図面を保存するので、コマンドの
				// 中で汚しても未保存の変更が残らない。
				MCObjectHandle h = gSDK->CreateLine(WorldPt(0.0, 0.0), WorldPt(1000.0, 1000.0));
				// **刻みの中の `CreateLine` だけでは「未保存の変更」にならない**（最初の版は
				// 保存の確認が出ずに終わった）。変更の印は明示して立てる。
				gSDK->MarkCurrentDocChanged();
				QftAppend(st->events, "prep " + QftStamp(*st) +
										  " CreateLine=" + (h == nullptr ? "nullptr" : "ok") +
										  " MarkCurrentDocChanged");
			}
			else if (step == "clean")
			{
				QftAppend(st->events,
						  "prep " + QftStamp(*st) + " " + QftSaveActive(QftSavePath("clean")));
			}
			else if (step == "nodoc")
			{
				QftAppend(st->events,
						  "prep " + QftStamp(*st) + " " + QftSaveActive(QftSavePath("nodoc")));
				const bool closed = gSDK->CloseDocument();
				QftAppend(st->events, "prep CloseDocument=" + QftYesNo(closed) +
										  " docs=" + std::to_string(QftDocCount()));
			}
			st->phase = kQftPhasePrepared;
			st->prepAt = now;
			return;
		}

		if (st->phase == kQftPhasePrepared && now - st->prepAt >= kQftCallAfterMs)
		{
			if (step == "nodoc" && QftDocCount() != 0)
			{
				if (now - st->prepAt > 5000)
				{
					QftAppend(st->events, "abort " + QftStamp(*st) + " 図面が 0 枚にならない");
					QftDisarm("doc-not-closed");
				}
				return;
			}
			st->phase = kQftPhaseCalling;
			st->callAt = now;
			QftCallQuit(st, "drive");
			if (st->disarmed)
				return;
			st->phase = kQftPhaseReturned;
			st->returnedAt = QftElapsedMs(*st);
			QftAppend(st->events, "after-return docs=" + std::to_string(QftDocCount()) + " layer=" +
									  (gSDK->GetCurrentLayer() == nullptr ? "nullptr" : "ok"));
			return;
		}

		if (st->phase == kQftPhaseReturned && now - st->returnedAt >= kQftAfterReturnMs)
		{
			st->phase = kQftPhaseDone;
			QftDisarm("observed-after-return");
			return;
		}
		if (now > kQftMaxMs)
			QftDisarm("time-limit");
	}

	// 観測: 刻みごとに読み、変わったときだけ書く。
	void QftObserveTick()
	{
		QftState* st = gQftState;
		if (st == nullptr || st->disarmed || !QftOwnerOk(st))
			return;
		++st->observeTicks;
		const long long now = QftElapsedMs(*st);
		if (st->lastObserveMs >= 0 && now - st->lastObserveMs > st->maxGapMs)
			st->maxGapMs = now - st->lastObserveMs;
		st->lastObserveMs = now;
		st->lastTickWall = QftWallMs();
		if (st->depth > 0)
			++st->ticksInCall;
		if (st->phase == kQftPhaseReturned)
			++st->ticksAfterReturn;
		const bool quitting = st->phase >= kQftPhaseCalling;
		const std::string mode = QftMode();

		// **呼ぶ前に書く**（終了の後は、状況が変わった刻みで 1 度）。落ちたら、この行が最後に残る。
		const std::string preKey =
			std::to_string(st->depth) + "/" + std::to_string(st->phase) + "/" + mode;
		if (quitting && preKey != st->lastPreKey)
		{
			QftAppend(st->events, "pre " + QftStamp(*st) + " phase=" + std::to_string(st->phase) +
									  " mode=" + mode +
									  " → GetOpenFilesList / IsCurrentlyBuildingAnUndoEvent / "
									  "GetCurrentLayer を呼ぶ");
			st->lastPreKey = preKey;
		}
		const int docs = QftDocCount();
		const bool undo = gSDK->IsCurrentlyBuildingAnUndoEvent();
		const bool layer = gSDK->GetCurrentLayer() != nullptr;

		std::ostringstream key;
		key << "depth=" << st->depth << " phase=" << st->phase << " docs=" << docs
			<< " undo=" << QftYesNo(undo) << " layer=" << (layer ? "ok" : "nullptr")
			<< " mode=" << mode;
		if (key.str() != st->lastKey)
		{
			QftAppend(st->events, "state t_ms=" + std::to_string(now) + " tick=" +
									  std::to_string(st->observeTicks) + " " + key.str());
			st->lastKey = key.str();
		}

		// 2 度目の呼び出し。駆動が入り直してこなければ（mac の見込み）、観測から呼ぶ。
		if (kQftSteps[st->step].name == std::string("nested") && st->depth > 0 && !st->nestedDone &&
			now - st->callAt >= kQftNestedAtMs + 1000)
		{
			st->nestedDone = true;
			QftCallQuit(st, "observe（駆動は入り直してこなかった）");
		}

		if (quitting || st->observeTicks % 10 == 0)
			QftWriteCounts(*st);
	}

#if GS_WIN
	VOID CALLBACK QftWinObserve(HWND, UINT, UINT_PTR, DWORD)
	{
		QftObserveTick();
	}
	VOID CALLBACK QftWinDrive(HWND, UINT, UINT_PTR, DWORD)
	{
		QftDriveTick();
	}
#else
	void QftCFObserve(CFRunLoopTimerRef, void*)
	{
		QftObserveTick();
	}
	void QftCFDrive(CFRunLoopTimerRef, void*)
	{
		QftDriveTick();
	}
#endif

	std::string QftPinSelf(std::string& pinnedPath)
	{
#if GS_WIN
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&QftPinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した（GetLastError=" +
				   std::to_string((unsigned long)::GetLastError()) + "）";
		char buf[MAX_PATH] = {0};
		const DWORD n = ::GetModuleFileNameA(mod, buf, MAX_PATH);
		pinnedPath = (n > 0 && n < MAX_PATH) ? std::string(buf) : std::string("(名前が読めない)");
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&QftPinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		void* handle = ::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
		if (handle == nullptr)
			return std::string("dlopen(RTLD_NOLOAD) が失敗した: ") + info.dli_fname;
		pinnedPath = info.dli_fname;
		return std::string();
#endif
	}

	std::vector<std::string> QftReadLines(const std::string& path)
	{
		std::vector<std::string> lines;
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line))
			lines.push_back(line);
		return lines;
	}

	long long QftCountValue(const std::vector<std::string>& counts, const std::string& name)
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
	bool QftReport(vwprobe::Report& probe)
	{
		std::error_code ec;
		std::filesystem::remove(QftOwnerPath(), ec); // 生きている刻みは次の刻みで止まる
		const std::vector<std::string> counts = QftReadLines(QftCountsPath());
		const long long beat = QftCountValue(counts, "heartbeat_wall_ms");
		const long long age = QftWallMs() - beat;
		const bool alive = beat > 0 && age >= 0 && age < kQftStaleMs;
		probe.log(std::string("■ 前の段の書き溜め（心拍 ") + std::to_string(age) + " ms 前・" +
				  (alive ? "刻みはまだ生きている——Vectorworks は終わっていない"
						 : "刻みは居ない——Vectorworks は終わった（か、刻みが止まった）") +
				  "）");
		probe.log("  -- 経過 --");
		const std::vector<std::string> events = QftReadLines(QftEventsPath());
		const std::size_t kMax = 300;
		for (std::size_t i = 0; i < events.size() && i < kMax; ++i)
			probe.log("  " + events[i]);
		if (events.size() > kMax)
			probe.log("  …（" + std::to_string(events.size() - kMax) + " 行省略）");
		probe.log("  -- 数 --");
		for (const std::string& line : counts)
			probe.log("  " + line);
		const long long lastTick = QftCountValue(counts, "last_tick_wall_ms");
		if (lastTick > 0 && beat > 0)
			probe.log("  last_tick_to_heartbeat_ms " + std::to_string(beat - lastTick));
		std::filesystem::remove(QftEventsPath(), ec);
		std::filesystem::remove(QftCountsPath(), ec);
		for (const std::string& line : events)
		{
			if (line.rfind("abort ", 0) == 0)
				return true;
		}
		return false;
	}

	void QftArm(vwprobe::Report& probe, int step)
	{
		std::string pinnedPath;
		const std::string trouble = QftPinSelf(pinnedPath);
		if (!trouble.empty())
		{
			probe.fail("本体をピン留めできなかったので仕掛けない（" + trouble + "）");
			return;
		}
		gQftState = new QftState();
		QftState* st = gQftState;
		st->owner = std::to_string(QftWallMs()) + "-" + VW_PAYLOAD_BUILD_ID;
		st->events = QftEventsPath();
		st->counts = QftCountsPath();
		st->step = step;
		st->armedAt = std::chrono::steady_clock::now();
		{
			std::ofstream out(QftOwnerPath(), std::ios::trunc);
			out << st->owner << "\n";
		}
		{
			std::ofstream out(QftStepPath(), std::ios::trunc);
			out << step << "\n";
		}
		std::error_code ec;
		std::filesystem::remove(st->events, ec);
		QftAppend(st->events, std::string("arm step=") + kQftSteps[step].name +
								  " owner=" + st->owner + " build=" + VW_PAYLOAD_BUILD_ID +
								  " pinned=" + pinnedPath);
		QftWriteCounts(*st);

		const Boolean q =
			gSDK->RegisterNotificationProcedure(&QftNotifyBeforeQuit, kNotifyBeforeApplicationQuit);
		const Boolean c = gSDK->RegisterNotificationProcedure(&QftNotifyDocClose, kNotifyDocClose);
		QftAppend(st->events, std::string("register BeforeApplicationQuit=") + QftYesNo(q != 0) +
								  " DocClose=" + QftYesNo(c != 0));
		static bool sAtExit = false; // 像ごとに 1 度
		if (!sAtExit)
			sAtExit = std::atexit(&QftAtExit) == 0;

#if GS_WIN
		gQftObserveTimer = ::SetTimer(nullptr, 0, (UINT)kQftObserveMs, &QftWinObserve);
		gQftDriveTimer = ::SetTimer(nullptr, 0, (UINT)kQftDriveMs, &QftWinDrive);
		const bool armed = gQftObserveTimer != 0 && gQftDriveTimer != 0;
#else
		const CFAbsoluteTime o = (CFAbsoluteTime)kQftObserveMs / 1000.0;
		const CFAbsoluteTime d = (CFAbsoluteTime)kQftDriveMs / 1000.0;
		const CFAbsoluteTime now = ::CFAbsoluteTimeGetCurrent();
		gQftObserveTimer =
			::CFRunLoopTimerCreate(kCFAllocatorDefault, now + o, o, 0, 0, &QftCFObserve, nullptr);
		gQftDriveTimer =
			::CFRunLoopTimerCreate(kCFAllocatorDefault, now + d, d, 0, 0, &QftCFDrive, nullptr);
		const bool armed = gQftObserveTimer != nullptr && gQftDriveTimer != nullptr;
		if (armed)
		{
			::CFRunLoopAddTimer(::CFRunLoopGetMain(), gQftObserveTimer, kCFRunLoopCommonModes);
			::CFRunLoopAddTimer(::CFRunLoopGetMain(), gQftDriveTimer, kCFRunLoopCommonModes);
		}
#endif
		if (!armed)
		{
			probe.fail("タイマーを仕掛けられなかった");
			QftDisarm("arm-failed");
			return;
		}
		probe.log("");
		probe.log(std::string("■ 段 ") + std::to_string(step) + "/" +
				  std::to_string(kQftStepCount - 1) + " 「" + kQftSteps[step].name +
				  "」を仕掛けた。1.5 秒後に刻みが図面を準備し、その 0.6 秒後に終了を呼ぶ。");
		probe.log(std::string("このあと: ") + kQftSteps[step].howTo);
	}
} // namespace

VW_PROBE("quit-from-timer", "刻みから終了を呼ぶ", "段を進めながら終了の振る舞いを記録")
{
	probe.log("コマンドの中: docs=" + std::to_string(QftDocCount()) + " mode=" + QftMode());

	int next = 0;
	std::error_code ec;
	if (std::filesystem::exists(QftEventsPath(), ec))
	{
		const std::string measured = QftReadFirstLine(QftStepPath());
		const bool aborted = QftReport(probe);
		next = measured.empty() ? 0 : std::atoi(measured.c_str()) + (aborted ? 0 : 1);
	}
	else
	{
		const std::string saved = QftReadFirstLine(QftStepPath());
		next = saved.empty() ? 0 : std::atoi(saved.c_str());
	}
	if (next >= kQftStepCount)
	{
		std::filesystem::remove(QftStepPath(), ec);
		probe.log("");
		probe.log("■ 全段を測り終えた（次に走らせると段 0 から始める）");
		return;
	}
	if (next < 0)
		next = 0;
	QftArm(probe, next);
}
