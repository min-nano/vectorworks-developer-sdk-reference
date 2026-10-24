//
//	probes/runtime/quit-in-user-modal/probe.cpp
//
//	[issue #235] **利用者が GUI で開いたモーダル（環境設定など）の最中に、OS タイマーの刻みは
//	届くか。届くなら、そこから `CloseAllFilesAndQuitVectorworks(true, false)` を呼ぶと何が
//	起きるか**を実測する。下敷きは #230 の `quit-from-timer`（PR #238 の履歴）。
//
//	Vectorworks が終わりうるので、結果はプローブの報告では返らない。**刻みが 1 行ずつ
//	一時ファイルへ書き（書くたびに閉じる）、次に走らせたときにそれを報告する。** 同じものを
//	何度も走らせて段を進める:
//
//	  0 observe  終了は呼ばない。人が環境設定を開き、10 秒ほど置いて閉じる。
//	             → 開いている間に刻みが届くか（問い 1）。届かなければ、閉じた後に
//	               「間（gap）」として残る。
//	  1 quit     刻みが図面を汚し（`MarkCurrentDocChanged`）、**モーダルが開いたのを見て
//	             3 秒後に**終了を呼ぶ。人は環境設定を開いて待ち、保存の確認が出たら
//	             「キャンセル」を押す（問い 2・3）。
//
//	「モーダルが開いているか」は機械で読む:
//	  * Windows: 走らせたときの前面の窓（の根の持ち主＝ VW の主窓）を覚え、刻みごとに
//	    `IsWindowEnabled(主窓)` を読む（モーダルは持ち主を無効にする）。前面の窓の題と
//	    クラス名も、変わったときに書く。
//	  * mac: ランループのモード（`NSModalPanelRunLoopMode` ならモーダルの中）。
//	**呼び出しから戻った後もモーダルが開いたままか**を同じ道で読む（問い 3）。
//
//	タイマーは 1 本（50 ms）。mac は `kCFRunLoopCommonModes`、Windows は `SetTimer(nullptr, …)`。
//	終了の呼び出しの最中は旗（depth）で持ち、入り直した刻みは読むだけにする（#230 の線引き）。
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
	struct QumStep
	{
		const char* name;
		const char* howTo; // 人に頼むこと
	};

	const QumStep kQumSteps[] = {
		{"observe",
		 "結果ダイアログを閉じたら、環境設定（ツール ＞ オプション ＞ 環境設定）を開き、10 秒ほど"
		 "置いてから閉じる。さらに 5 秒置いてから、もう一度走らせる"},
		{"quit",
		 "結果ダイアログを閉じたら、環境設定を開いて**そのまま待つ**（閉じない）。保存の確認が"
		 "出たら、何がどこに出たか（環境設定の上に重なったか・環境設定が閉じたか）を覚えて"
		 "「キャンセル」を押す。その後も環境設定が開いているかを見てから閉じ、5 秒置いて"
		 "もう一度走らせる。何も出ないまま 30 秒過ぎたら、環境設定を閉じて様子を覚えておく"},
	};
	const int kQumStepCount = (int)(sizeof(kQumSteps) / sizeof(kQumSteps[0]));

	const long long kQumTickMs = 50;
	// 仕掛けてから図面を準備するまで。**メニューから走らせると結果ダイアログが出る**ので、
	// それを閉じる間を取る（ブリッジから走らせる mac は短くてよい）。
#if GS_WIN
	const long long kQumPrepAtMs = 8000;
#else
	const long long kQumPrepAtMs = 1500;
#endif
	const long long kQumCallAfterModalMs = 3000; // モーダルを見てから終了を呼ぶまで
	const long long kQumAfterReturnMs = 20000;	 // 戻った後に見続ける時間
	const long long kQumObserveMs = 90000;		 // 段 0 を見続ける時間
	const long long kQumMaxMs = 5LL * 60 * 1000;
	const long long kQumGapMs = 400; // これより空いた刻みは「間」として書く
	const long long kQumStaleMs = 5000;

	std::filesystem::path QumDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return dir;
	}
	std::string QumEventsPath()
	{
		return (QumDir() / "VwSdkProbes-quit-in-user-modal-events.txt").string();
	}
	std::string QumCountsPath()
	{
		return (QumDir() / "VwSdkProbes-quit-in-user-modal-counts.txt").string();
	}
	std::string QumOwnerPath()
	{
		return (QumDir() / "VwSdkProbes-quit-in-user-modal-owner.txt").string();
	}
	// 次に仕掛ける段の番号。消せば 0 からやり直す。
	std::string QumStepPath()
	{
		return (QumDir() / "VwSdkProbes-quit-in-user-modal-step.txt").string();
	}

	std::string QumReadFirstLine(const std::string& path)
	{
		std::ifstream in(path);
		std::string line;
		std::getline(in, line);
		return line;
	}

	long long QumWallMs()
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	// 1 行書いて閉じる。毎回開き直すので、Vectorworks ごと終わってもそこまでが残る。
	void QumAppend(const std::string& path, const std::string& line)
	{
		std::ofstream out(path, std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	std::string QumYesNo(bool b)
	{
		return b ? "yes" : "no";
	}

	int QumDocCount()
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		return (int)list.GetSize();
	}

	// --- モーダルの見分け --------------------------------------------------
#if GS_WIN
	HWND gQumMainWnd = nullptr;

	std::string QumUtf8(const wchar_t* w)
	{
		if (w == nullptr || *w == 0)
			return std::string();
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		if (n <= 1)
			return std::string();
		std::string out((std::size_t)n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
		out.resize((std::size_t)n - 1);
		return out;
	}

	std::string QumWndText(HWND h)
	{
		if (h == nullptr)
			return "(なし)";
		wchar_t title[256] = {0};
		wchar_t cls[128] = {0};
		::GetWindowTextW(h, title, 256);
		::GetClassNameW(h, cls, 128);
		const DWORD tid = ::GetWindowThreadProcessId(h, nullptr);
		return "\"" + QumUtf8(title) + "\"/" + QumUtf8(cls) +
			   (tid == ::GetCurrentThreadId() ? "" : "（別スレッド）");
	}

	// 主窓が無効＝その上にモーダルが出ている。
	bool QumModal()
	{
		return gQumMainWnd != nullptr && ::IsWindow(gQumMainWnd) && !::IsWindowEnabled(gQumMainWnd);
	}

	std::string QumMode()
	{
		return std::string("main_enabled=") + QumYesNo(!QumModal()) +
			   " fg=" + QumWndText(::GetForegroundWindow());
	}
#else
	std::string QumRunLoopMode()
	{
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return "(ランループの外)";
		char buf[256] = {0};
		std::string out = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8)
							  ? std::string(buf)
							  : std::string("(読めない)");
		::CFRelease(mode);
		return out;
	}

	bool QumModal()
	{
		return QumRunLoopMode() == "NSModalPanelRunLoopMode";
	}

	std::string QumMode()
	{
		return "mode=" + QumRunLoopMode();
	}
#endif

	// -----------------------------------------------------------------------
	// 刻みが持ち越す状態。**意図して leak する**（タイマーはプローブより長生き）。
	// 段 1 の進み: 準備待ち → 準備した（結果ダイアログが閉じる＝モーダルでない刻みを待つ）
	// → 利用者のモーダルが開くのを待つ → モーダルを見た（終了を呼ぶ待ち）→ 呼び出しの中
	// → 呼び出しから戻った。
	enum QumPhase
	{
		kQumPhaseWaiting,
		kQumPhaseWaitNoModal,
		kQumPhaseWaitModal,
		kQumPhaseModalSeen,
		kQumPhaseCalling,
		kQumPhaseReturned,
		kQumPhaseDone,
	};

	struct QumState
	{
		std::string owner;
		std::string events;
		std::string counts;
		int step = 0;
		std::chrono::steady_clock::time_point armedAt;
		QumPhase phase = kQumPhaseWaiting;
		long long modalSeenAt = -1;
		long long returnedAt = -1;
		int depth = 0; // 終了の呼び出しが何段スタックに載っているか
		long long ticks = 0;
		// モーダルと見えた刻み・呼び出しの最中の刻み（入り直し）・戻った後の刻み
		long long ticksModal = 0;
		long long ticksInCall = 0;
		long long ticksAfterReturn = 0;
		long long maxGapMs = 0;
		long long gaps = 0;
		long long lastTickMs = -1;
		long long notifyQuit = 0;
		std::string lastKey;
		bool disarmed = false;
	};

	QumState* gQumState = nullptr;
#if GS_WIN
	UINT_PTR gQumTimer = 0;
#else
	CFRunLoopTimerRef gQumTimer = nullptr;
#endif

	long long QumElapsedMs(const QumState& st)
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - st.armedAt)
			.count();
	}

	std::string QumStamp(const QumState& st)
	{
		return "t_ms=" + std::to_string(QumElapsedMs(st)) + " depth=" + std::to_string(st.depth);
	}

	void QumWriteCounts(const QumState& st)
	{
		std::ofstream out(st.counts, std::ios::trunc);
		if (!out)
			return;
		out << "heartbeat_wall_ms " << QumWallMs() << "\n"
			<< "step " << kQumSteps[st.step].name << "\n"
			<< "elapsed_ms " << QumElapsedMs(st) << "\n"
			<< "phase " << (int)st.phase << "\n"
			<< "ticks " << st.ticks << "\n"
			<< "ticks_modal " << st.ticksModal << "\n"
			<< "ticks_in_call " << st.ticksInCall << "\n"
			<< "ticks_after_return " << st.ticksAfterReturn << "\n"
			<< "gaps_over_" << kQumGapMs << "ms " << st.gaps << "\n"
			<< "max_gap_ms " << st.maxGapMs << "\n"
			<< "notify_before_app_quit " << st.notifyQuit << "\n";
	}

	bool QumOwnerOk(QumState* st)
	{
		if (QumReadFirstLine(QumOwnerPath()) == st->owner)
			return true;
		st->disarmed = true; // 書き溜めはもう別の持ち主のもの。何も書かずに止まる
		return false;
	}

	void QumNotifyBeforeQuit(StatusID, StatusData);

	void QumStopTimer()
	{
#if GS_WIN
		if (gQumTimer != 0)
			::KillTimer(nullptr, gQumTimer);
		gQumTimer = 0;
#else
		if (gQumTimer != nullptr)
		{
			::CFRunLoopTimerInvalidate(gQumTimer);
			::CFRelease(gQumTimer);
			gQumTimer = nullptr;
		}
#endif
		gSDK->UnregisterNotificationProcedure(&QumNotifyBeforeQuit, kNotifyBeforeApplicationQuit);
	}

	void QumDisarm(const std::string& why)
	{
		QumState* st = gQumState;
		const bool write = st != nullptr && !st->disarmed;
		if (st != nullptr)
			st->disarmed = true;
		QumStopTimer();
		if (write)
		{
			QumWriteCounts(*st);
			QumAppend(st->events, "disarm " + QumStamp(*st) + " why=" + why);
		}
	}

	void QumNotifyBeforeQuit(StatusID, StatusData)
	{
		QumState* st = gQumState;
		if (st == nullptr || st->disarmed)
			return;
		++st->notifyQuit;
		QumAppend(st->events, "notify BeforeApplicationQuit " + QumStamp(*st) + " " + QumMode());
		QumWriteCounts(*st);
	}

	void QumAtExit()
	{
		// 書き溜めがまだこの像のものなら書く（gSDK は呼ばない）。
		QumState* st = gQumState;
		if (st == nullptr || QumReadFirstLine(QumOwnerPath()) != st->owner)
			return;
		QumWriteCounts(*st);
		QumAppend(st->events, "atexit " + QumStamp(*st) + " (gSDK は呼ばない)");
	}

	void QumCallQuit(QumState* st)
	{
		const long long begin = QumElapsedMs(*st);
		QumWriteCounts(*st);
		std::string line = "quit-call-begin " + QumStamp(*st);
		line += " modal=" + QumYesNo(QumModal()) + " " + QumMode();
		line += " undo=" + QumYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
		line += " → CloseAllFilesAndQuitVectorworks(true, false)";
		QumAppend(st->events, line);
		++st->depth;
		gSDK->CloseAllFilesAndQuitVectorworks(true, false);
		--st->depth;
		const long long end = QumElapsedMs(*st);
		line = "quit-call-returned " + QumStamp(*st);
		line += " took_ms=" + std::to_string(end - begin);
		line += " modal=" + QumYesNo(QumModal()) + " " + QumMode();
		line += " docs=" + std::to_string(QumDocCount());
		QumAppend(st->events, line);
		QumWriteCounts(*st);
	}

	void QumTick()
	{
		QumState* st = gQumState;
		if (st == nullptr || st->disarmed || !QumOwnerOk(st))
			return;
		++st->ticks;
		const long long now = QumElapsedMs(*st);
		if (st->lastTickMs >= 0)
		{
			const long long gap = now - st->lastTickMs;
			if (gap > st->maxGapMs)
				st->maxGapMs = gap;
			if (gap > kQumGapMs)
			{
				++st->gaps;
				QumAppend(st->events, "gap " + QumStamp(*st) + " gap_ms=" + std::to_string(gap) +
										  "（" + std::to_string(st->lastTickMs) + " → " +
										  std::to_string(now) + " の間、刻みが来なかった）");
			}
		}
		st->lastTickMs = now;
		const bool modal = QumModal();
		if (modal)
			++st->ticksModal;
		if (st->depth > 0)
			++st->ticksInCall;
		if (st->phase == kQumPhaseReturned)
			++st->ticksAfterReturn;

		// 状態が変わったときだけ書く。**呼ぶ前に書く**——落ちたら、この行が最後に残る。
		const std::string mode = QumMode();
		std::ostringstream key;
		key << "depth=" << st->depth << " phase=" << st->phase << " modal=" << QumYesNo(modal)
			<< " " << mode;
		if (key.str() != st->lastKey)
		{
			std::string line = "state t_ms=" + std::to_string(now);
			line += " tick=" + std::to_string(st->ticks) + " " + key.str();
			line += " → GetOpenFilesList / IsCurrentlyBuildingAnUndoEvent を呼ぶ";
			QumAppend(st->events, line);
			line = "  docs=" + std::to_string(QumDocCount());
			line += " undo=" + QumYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
			QumAppend(st->events, line);
			st->lastKey = key.str();
			QumWriteCounts(*st);
		}
		else if (st->ticks % 10 == 0)
			QumWriteCounts(*st);

		if (st->depth > 0)
			return; // 終了の呼び出しの最中は読むだけ（入れ子は見送る）

		const std::string step = kQumSteps[st->step].name;
		if (step == "observe")
		{
			if (now >= kQumObserveMs)
				QumDisarm("observed");
			return;
		}

		// --- 段 1 quit ---
		switch (st->phase)
		{
		case kQumPhaseWaiting:
			if (now >= kQumPrepAtMs)
			{
				const int docs = QumDocCount();
				if (docs != 1)
				{
					std::string line = "abort " + QumStamp(*st);
					line += " docs=" + std::to_string(docs);
					line += "（図面を 1 枚だけ開いた状態で走らせる）";
					QumAppend(st->events, line);
					QumDisarm("docs-not-1");
					return;
				}
				MCObjectHandle h = gSDK->CreateLine(WorldPt(0.0, 0.0), WorldPt(1000.0, 1000.0));
				// 刻みの中の CreateLine だけでは「未保存の変更」にならない（#230）。
				gSDK->MarkCurrentDocChanged();
				QumAppend(st->events, "prep " + QumStamp(*st) +
										  " CreateLine=" + (h == nullptr ? "nullptr" : "ok") +
										  " MarkCurrentDocChanged");
				st->phase = kQumPhaseWaitNoModal;
			}
			break;
		case kQumPhaseWaitNoModal:
			// 結果ダイアログ（これもモーダル）を取り違えないよう、一度モーダルでない刻みを見る。
			if (!modal)
			{
				QumAppend(st->events, "no-modal " + QumStamp(*st) + " 利用者のモーダルを待つ");
				st->phase = kQumPhaseWaitModal;
			}
			break;
		case kQumPhaseWaitModal:
			if (modal)
			{
				st->modalSeenAt = now;
				st->phase = kQumPhaseModalSeen;
				QumAppend(st->events, "modal-seen " + QumStamp(*st) + " " + mode);
			}
			break;
		case kQumPhaseModalSeen:
			if (!modal)
			{
				// 呼ぶ前に閉じられた。待ち直す。
				QumAppend(st->events, "modal-gone " + QumStamp(*st) + " 呼ぶ前に閉じた");
				st->phase = kQumPhaseWaitModal;
			}
			else if (now - st->modalSeenAt >= kQumCallAfterModalMs)
			{
				st->phase = kQumPhaseCalling;
				QumCallQuit(st);
				if (st->disarmed)
					return;
				st->phase = kQumPhaseReturned;
				st->returnedAt = QumElapsedMs(*st);
			}
			break;
		case kQumPhaseReturned:
			// 終わっていく回（図面 0 枚）では止めない——atexit まで書く。
			if (now - st->returnedAt >= kQumAfterReturnMs &&
				(QumDocCount() != 0 || now - st->returnedAt >= 60000))
			{
				st->phase = kQumPhaseDone;
				QumDisarm("observed-after-return");
				return;
			}
			break;
		default:
			break;
		}
		if (now > kQumMaxMs)
			QumDisarm("time-limit");
	}

#if GS_WIN
	VOID CALLBACK QumWinTick(HWND, UINT, UINT_PTR, DWORD)
	{
		QumTick();
	}
#else
	void QumCFTick(CFRunLoopTimerRef, void*)
	{
		QumTick();
	}
#endif

	std::string QumPinSelf(std::string& pinnedPath)
	{
#if GS_WIN
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&QumPinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した（GetLastError=" +
				   std::to_string((unsigned long)::GetLastError()) + "）";
		char buf[MAX_PATH] = {0};
		const DWORD n = ::GetModuleFileNameA(mod, buf, MAX_PATH);
		pinnedPath = (n > 0 && n < MAX_PATH) ? std::string(buf) : std::string("(名前が読めない)");
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&QumPinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		void* handle = ::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
		if (handle == nullptr)
			return std::string("dlopen(RTLD_NOLOAD) が失敗した: ") + info.dli_fname;
		pinnedPath = info.dli_fname;
		return std::string();
#endif
	}

	std::vector<std::string> QumReadLines(const std::string& path)
	{
		std::vector<std::string> lines;
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line))
			lines.push_back(line);
		return lines;
	}

	long long QumCountValue(const std::vector<std::string>& counts, const std::string& name)
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
	bool QumReport(vwprobe::Report& probe)
	{
		std::error_code ec;
		std::filesystem::remove(QumOwnerPath(), ec); // 生きている刻みは次の刻みで止まる
		const std::vector<std::string> counts = QumReadLines(QumCountsPath());
		const long long beat = QumCountValue(counts, "heartbeat_wall_ms");
		const long long age = QumWallMs() - beat;
		const bool alive = beat > 0 && age >= 0 && age < kQumStaleMs;
		const std::vector<std::string> events = QumReadLines(QumEventsPath());
		bool exited = false;
		bool stopped = false;
		bool aborted = false;
		for (const std::string& line : events)
		{
			exited = exited || line.rfind("atexit ", 0) == 0;
			stopped = stopped || line.rfind("disarm ", 0) == 0;
			aborted = aborted || line.rfind("abort ", 0) == 0;
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
		std::filesystem::remove(QumEventsPath(), ec);
		std::filesystem::remove(QumCountsPath(), ec);
		return aborted;
	}

	void QumArm(vwprobe::Report& probe, int step)
	{
		std::string pinnedPath;
		const std::string trouble = QumPinSelf(pinnedPath);
		if (!trouble.empty())
		{
			probe.fail("本体をピン留めできなかったので仕掛けない（" + trouble + "）");
			return;
		}
#if GS_WIN
		// コマンドの中の前面の窓＝ VW の主窓（の根の持ち主）。
		HWND fg = ::GetForegroundWindow();
		gQumMainWnd = fg != nullptr ? ::GetAncestor(fg, GA_ROOTOWNER) : nullptr;
		const bool mainEnabled = gQumMainWnd != nullptr && ::IsWindowEnabled(gQumMainWnd);
		probe.log("主窓: " + QumWndText(gQumMainWnd) + " enabled=" + QumYesNo(mainEnabled));
#endif
		gQumState = new QumState();
		QumState* st = gQumState;
		st->owner = std::to_string(QumWallMs()) + "-" + VW_PAYLOAD_BUILD_ID;
		st->events = QumEventsPath();
		st->counts = QumCountsPath();
		st->step = step;
		st->armedAt = std::chrono::steady_clock::now();
		{
			std::ofstream out(QumOwnerPath(), std::ios::trunc);
			out << st->owner << "\n";
		}
		{
			std::ofstream out(QumStepPath(), std::ios::trunc);
			out << step << "\n";
		}
		std::error_code ec;
		std::filesystem::remove(st->events, ec);
		QumAppend(st->events, std::string("arm step=") + kQumSteps[step].name +
								  " owner=" + st->owner + " build=" + VW_PAYLOAD_BUILD_ID +
								  " pinned=" + pinnedPath + " " + QumMode());
		QumWriteCounts(*st);

		const Boolean q =
			gSDK->RegisterNotificationProcedure(&QumNotifyBeforeQuit, kNotifyBeforeApplicationQuit);
		QumAppend(st->events, std::string("register BeforeApplicationQuit=") + QumYesNo(q != 0));
		static bool sAtExit = false; // 像ごとに 1 度
		if (!sAtExit)
			sAtExit = std::atexit(&QumAtExit) == 0;

#if GS_WIN
		gQumTimer = ::SetTimer(nullptr, 0, (UINT)kQumTickMs, &QumWinTick);
		const bool armed = gQumTimer != 0;
#else
		const CFAbsoluteTime t = (CFAbsoluteTime)kQumTickMs / 1000.0;
		gQumTimer = ::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + t, t,
										   0, 0, &QumCFTick, nullptr);
		const bool armed = gQumTimer != nullptr;
		if (armed)
			::CFRunLoopAddTimer(::CFRunLoopGetMain(), gQumTimer, kCFRunLoopCommonModes);
#endif
		if (!armed)
		{
			probe.fail("タイマーを仕掛けられなかった");
			QumDisarm("arm-failed");
			return;
		}
		probe.log("");
		probe.log(std::string("■ 段 ") + std::to_string(step) + "/" +
				  std::to_string(kQumStepCount - 1) + " 「" + kQumSteps[step].name +
				  "」を仕掛けた。");
		probe.log(std::string("このあと: ") + kQumSteps[step].howTo);
	}
} // namespace

VW_PROBE("quit-in-user-modal", "モーダル中の終了", "環境設定の最中の刻みと終了")
{
	probe.log("コマンドの中: docs=" + std::to_string(QumDocCount()) + " " +
#if GS_WIN
			  std::string("fg=") + QumWndText(::GetForegroundWindow()));
#else
			  QumMode());
#endif

	int next = 0;
	std::error_code ec;
	if (std::filesystem::exists(QumEventsPath(), ec))
	{
		const std::string measured = QumReadFirstLine(QumStepPath());
		const bool aborted = QumReport(probe);
		next = measured.empty() ? 0 : std::atoi(measured.c_str()) + (aborted ? 0 : 1);
	}
	else
	{
		const std::string saved = QumReadFirstLine(QumStepPath());
		next = saved.empty() ? 0 : std::atoi(saved.c_str());
	}
	if (next >= kQumStepCount)
	{
		std::filesystem::remove(QumStepPath(), ec);
		probe.log("");
		probe.log("■ 全段を測り終えた（次に走らせると段 0 から始める）");
		return;
	}
	if (next < 0)
		next = 0;
	QumArm(probe, next);
}
