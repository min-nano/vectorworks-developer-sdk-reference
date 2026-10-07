//
//	probes/runtime/drag-render-timer-tick/probe.cpp
//
//	[issue #213] **ツールのドラッグ中・レンダリング中に OS のタイマーの刻みが当たったら
//	何が起きるか**を実測する。
//
//	背景: #206（PR #209）で「**VW が undo イベントを開いたまま VW 自身がイベントを回して
//	いる最中**に刻みが当たると、読むのは安全・**書くとそのイベントに混ざる**」が確定した。
//	ただし測れたのは 3 つの場面のうち**モーダル確認ダイアログの最中**だけで、
//	**ツールのドラッグ中・レンダリング中は未測**である
//	（[Findings「周期実行と通知」6 節](../../../Findings/Timers%20and%20Notifications.md)）。
//
//	この 2 つを測るには #209 のプローブでは足りない——あちらは同期的に走って `return`
//	するので、**利用者がドラッグする時間が無い**。そこで #207（issue #204）の作法
//	（本体を `dlopen(RTLD_NOLOAD)` / `GET_MODULE_HANDLE_EX_FLAG_PIN` でピン留めし、
//	**コマンドが戻った後も刻み続けるタイマー**を仕掛けて、2 回目の実行で報告させる）を
//	使う。**同じ起動のうちに閉じる**こと（同 4 節の 5。プロセスが終われば仕掛けも消えるので、
//	刻み 0 件が「刻まない」と読めてしまう）。
//
//	測るもの（issue #213 の 3 問に 1 対 1 で対応する）:
//
//	  (1) **ツールのドラッグ中**（点取りの最中）に刻みは届くか。届くなら `gSDK` を読めるか。
//	      → ドラッグが実際に行われたかは**ログに写らない**ので、**機械で確かめ直せる形**に
//	        する。2 つを突き合わせる:
//	          ・`kNotifyBeginToolMode`（`'BTOO'`、"begins collecting points"）→
//	            `kNotifyEndToolMode`（`'ETOO'`）の通知で**点取りの両端を囲む**。
//	            囲みの中で届いた刻みが「ドラッグ中の刻み」である。
//	          ・mac では刻みの `mode=` も読む（`NSEventTrackingRunLoopMode` が出ていれば
//	            トラッキング中の刻み）。
//	        **通知が 1 度も来なければ、この回はドラッグについて何も言えない**（利用者が
//	        ドラッグしなかった）——そう結果の見出しに出す。
//
//	  (2) **レンダリング中**（VW が進捗を出しながら回している最中）はどうか。
//	      **1 回目の実機（ビルド 87d374350eda）で、ここの仕掛けが空振りした**——undo
//	      イベントを開いたまま `SetRenderMode(renderOpenGL, immediate=true,
//	      doProgress=true)` を呼ぶと **`false` が返り 0 ms で戻る**（＝1 度も描かない）。
//	      そこでこの版は**「道具が効くか」と「イベント中だから断られたのか」を切り分ける**
//	      ——イベントの外で経路（SetRenderMode の 3 モード ＋ VectorScript の
//	      `SetLayerRenderMode`）を順に試し、効いたものを**イベント中にもう一度**呼ぶ。
//	      外で効いて中で断られるなら、それ自体が知見である。
//	      → **利用者に頼る部分と、プローブだけで決まる部分の 2 本立てにする。**
//	        ・P-R（プローブの中）: `SetRenderMode(layer, renderOpenGL, immediate=true,
//	          doProgress=true)` で**VW にその場でレンダリングさせる**。しかも
//	          **先に `DeleteObject(h, useUndo=true)` で VW に undo イベントを開かせておく**
//	          （#209 の梃子 1。`Findings/Undo.md` の実測）ので、これは
//	          「**VW が undo イベントを開いたまま、VW がレンダリングでイベントを回している
//	          最中**」そのものである。利用者の操作に依らず毎回同じ形で測れる。
//	        ・利用者の側（コマンドが戻った後）: 「ビュー > レンダリング」で実際に描かせて
//	          もらい、そのときの刻みを書き溜める。`kNotifyRenderModeAboutToChange`
//	          （`'RndC'`）/ `kNotifyRenderModeChanged`（`'RnMC'`）が印になる。
//	          **これはレンダリングの開始／完了の通知ではない**（モードの変更）ので、
//	          囲みとしては使わず**時刻の目印としてだけ**読む。
//
//	  (3) そこで図形を**作る／変える**と、やはり VW が開いている undo イベントに混ざるか。
//	      → P-R の刻み（`building=yes`）の中で 1 つだけ図形を作り、**取り消しを段ごとに
//	        掛けて各段で何が消えたかを名前で読む**（#209 と同じ作法。1 段だけだと
//	        「同じ段に居たのか別の段だったのか」が割れない）。
//	          ・`PROBE-I213-PRE` … **どの undo イベントも開いていないうちに作り、
//	            `EndUndoEvent` で閉じて別の段へ送った**対照。**`CreateLocus` 1 つでも
//	            イベントが開く**ので（`Findings/Undo.md`）、閉じないと対照にならない。
//	          ・`PROBE-I213-RTICK` … レンダリング中の刻みの中で作ったもの。
//	        **1 段目で RTICK だけが消え、2 段目で PRE が消えれば「混ざった」**。
//	        判定文はプローブが出すので、ログを読む側に解釈の余地が無い。
//
//	  (4) おまけ（利用者の側で `building=yes` の刻みが来たとき）: その刻みの中でも 1 つ
//	      作り（`PROBE-I213-UTICK`）、**どの局面で当たったか**を記録する。取り消しは
//	      掛けない（利用者の操作の段が上に積まれているので、段の境目が読めない）。
//
//	**ピン留めに失敗したら仕掛けない**（殻はプローブが終わると本体を `dlclose` するので、
//	タイマーの行き先が消えて落ちる）。通知手続きも**止めるときに必ず外す**。
//

#include "Probe.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#	include <windows.h>
#else
#	include <CoreFoundation/CoreFoundation.h>
#	include <dlfcn.h>
#	include <pthread.h>
#endif

// どの群（main / PR ごと）の本体かはビルドのときに決まる（plugin/CMakeLists.txt が
// -D で渡す）。構文チェック（ci-debug の compile）では渡らないので既定を置く。
#ifndef VW_PAYLOAD_GROUP
#	define VW_PAYLOAD_GROUP "local"
#endif

namespace
{
	// 刻みの間隔。#207 と同じ 250 ms（パレットの JS タイマーと同じ）にして、
	// あちらの数値と並べて読めるようにする。
	const long long kDragTickIntervalMs = 250;
	// 刻みの上限と締切。報告を忘れられても勝手に止まる（口が 2 つあるので倍を見る）。
	const int kDragTickMaxTicks = 6000;
	const long long kDragTickDeadlineMs = 900000; // 15 分

	// 図形の名前。**短い名前・ありふれた名前は使わない**（probes/runtime/README.md）。
	const char kDragTickNamePre[] = "PROBE-I213-PRE";	  // イベントの外（対照）
	const char kDragTickNameRTick[] = "PROBE-I213-RTICK"; // レンダリング中の刻みの中
	// 利用者の局面の書き込みは**場面ごとに別の名前で 1 つずつ**置く。1 回目の実機で
	// 「最初の `building=yes` の刻み」がプローブの結果ダイアログ（`NSModalPanelRunLoopMode`）
	// の最中だったため、ドラッグ・レンダリングの話として読めなかった（#206 のモーダルの
	// 再現にしかならない）。場面を名前で分けておけば、どの場面で書けたのかが残る。
	const char kDragTickNameUTool[] = "PROBE-I213-UTOOL"; // 点取りの最中（通知の囲みの中）
	const char kDragTickNameUTrack[] = "PROBE-I213-UTRACK"; // トラッキング中（mode で判る）
	const char kDragTickNameUOther[] = "PROBE-I213-UOTHER"; // それ以外（モーダル等）
	const char kDragTickNameVictim[] = "PROBE-I213-VICTIM"; // 梃子 1 で消す駒

	// 1 回目が書き溜めたものを 2 回目が読む。**一時ディレクトリ**に置く（Vectorworks が
	// 落ちても残るので、落ち方そのものも読み取れる）。**群ごとに別の名前**にする
	// （同じ slug のプローブが複数の PR に居るので、1 本の道を共有すると混ざる）。
	std::string DragTickFilePath()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		const std::string name =
			std::string("VwSdkProbes-drag-render-timer-tick-") + VW_PAYLOAD_GROUP + ".txt";
		return (dir / name).string();
	}

	// -----------------------------------------------------------------------
	// タイマーが持ち越す状態。**意図して leak する**——タイマーはプローブより長生きで、
	// 止まるのは「報告された」か「上限／締切に達した」ときだけである。
	struct DragTickState
	{
		std::string filePath;
		std::chrono::steady_clock::time_point armedAt;
		int ticks = 0;
		int ticksCommon = 0;  // 共通モード（mac）/ SetTimer（Windows）の口
		int ticksDefault = 0; // 既定モードだけの口（mac だけ）
		long long lastMsCommon = 0;
		long long lastMsDefault = 0;
		bool insideProbe = true; // プローブ本体がまだスタックに居るか
		std::string phase = "P0 仕掛けた直後";
		// 通知が立てる旗。**ドラッグが本当に行われたかの唯一の機械的な証拠**。
		int toolDepth = 0; // BTOO で ++ / ETOO で --
		int toolBegins = 0;
		int toolEnds = 0;
		int undoEnds = 0;	 // kNotifyUndoEndEvent（VW がイベントを閉じる直前）
		int renderNotes = 0; // RndC / RnMC
		bool wantWriteOnBuilding = false; // P-R で building=yes の刻みに 1 つ書く
		bool wroteRender = false;
		bool wroteUTool = false;
		bool wroteUTrack = false;
		bool wroteUOther = false;
		bool quiet = false; // 取り消しを掛けている間は gSDK を触らない
		bool disarmed = false;
	};

	DragTickState* gDragTickState = nullptr;
#if defined(_WIN32)
	UINT_PTR gDragTickWinTimer = 0;
#else
	CFRunLoopTimerRef gDragTickTimerCommon = nullptr;
	CFRunLoopTimerRef gDragTickTimerDefault = nullptr;
	// 札（CFRunLoopTimerContext の info に入れて、どちらの口の刻みかを見分ける）。
	const char kDragTickTagCommon[] = "共通モード";
	const char kDragTickTagDefault[] = "既定モードだけ";
#endif

	// 1 行書いて閉じる。**毎回開き直す**ので、Vectorworks ごと落ちてもそこまでが残る。
	void DragTickAppend(const std::string& path, const std::string& line)
	{
		std::ofstream out(path, std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	// 書き溜めの 1 行は `鍵=値` を ` ## ` で継ぐ（値に `##` は現れない）。
	std::string DragTickField(const std::string& key, const std::string& value)
	{
		return " ## " + key + "=" + value;
	}

	std::string DragTickNum(long long v)
	{
		return std::to_string(v);
	}

	std::string DragTickYesNo(bool v)
	{
		return v ? "yes" : "no";
	}

	long long DragTickElapsedMs()
	{
		if (gDragTickState == nullptr)
			return 0;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now - gDragTickState->armedAt)
			.count();
	}

	// いまランループが回っているモード。**ドラッグ中かどうかがこれで分かる**
	// （macOS のトラッキングは `NSEventTrackingRunLoopMode`）。主ランループが止まって
	// いれば nullptr が返る。
	std::string DragTickCurrentMode()
	{
#if defined(_WIN32)
		// Windows の WM_TIMER にモードの概念は無い。代わりにどのスレッドで来たかを出す。
		return std::string("thread-") + std::to_string((unsigned long)::GetCurrentThreadId());
#else
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return "(not-running)";
		char buf[160] = {0};
		const Boolean ok = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8);
		::CFRelease(mode);
		return ok ? std::string(buf) : std::string("(unprintable)");
#endif
	}

	// アクティブレイヤの図形の数。書き込みが届いたかを読み戻すのに使う。
	long long DragTickCountObjects()
	{
		if (gSDK == nil)
			return -1;
		MCObjectHandle layer = gSDK->GetActiveLayer();
		if (layer == nil)
			return -1;
		long long n = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(layer); h != nil; h = gSDK->NextObject(h))
		{
			++n;
			if (n >= 10000)
				break;
		}
		return n;
	}

	bool DragTickExists(const char* name)
	{
		return (gSDK != nil) && (gSDK->GetNamedObject(name) != nil);
	}

	// -----------------------------------------------------------------------
	// 通知手続き。**1 本で複数の `whichChange` を受け、`id` で見分ける**
	// （`StatusProcPtr` は `void(*)(StatusID, StatusData)`）。
	// ここは VW の中から呼ばれるので、**自分のファイルと `gSDK` の読みだけ**にする。
	void DragTickNotify(StatusID id, StatusData /*data*/)
	{
		DragTickState* st = gDragTickState;
		if (st == nullptr || st->disarmed)
			return;

		std::string what;
		if (id == kNotifyBeginToolMode)
		{
			++st->toolDepth;
			++st->toolBegins;
			what = "tool-begin";
		}
		else if (id == kNotifyEndToolMode)
		{
			if (st->toolDepth > 0)
				--st->toolDepth;
			++st->toolEnds;
			what = "tool-end";
		}
		else if (id == kNotifyUndoEndEvent)
		{
			++st->undoEnds;
			what = "undo-end";
		}
		else if (id == kNotifyRenderModeAboutToChange)
		{
			++st->renderNotes;
			what = "render-mode-about-to-change";
		}
		else if (id == kNotifyRenderModeChanged)
		{
			++st->renderNotes;
			what = "render-mode-changed";
		}
		else
		{
			what = "other-" + std::to_string((long long)id);
		}

		std::string building = "?";
		if (!st->quiet && gSDK != nil)
		{
			try
			{
				building = DragTickYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
			}
			catch (...)
			{
				building = "(例外)";
			}
		}
		DragTickAppend(st->filePath, "note" + DragTickField("what", what) +
										 DragTickField("t_ms", DragTickNum(DragTickElapsedMs())) +
										 DragTickField("tick_n", DragTickNum(st->ticks)) +
										 DragTickField("tool_depth", DragTickNum(st->toolDepth)) +
										 DragTickField("phase", st->phase) +
										 DragTickField("building", building) +
										 DragTickField("mode", DragTickCurrentMode()));
	}

	const StatusID kDragTickNotifications[] = {kNotifyBeginToolMode, kNotifyEndToolMode,
											   kNotifyUndoEndEvent, kNotifyRenderModeAboutToChange,
											   kNotifyRenderModeChanged};

	// -----------------------------------------------------------------------
	void DragTickDisarm(const std::string& why)
	{
		DragTickState* st = gDragTickState;
#if defined(_WIN32)
		if (gDragTickWinTimer != 0)
		{
			::KillTimer(nullptr, gDragTickWinTimer);
			gDragTickWinTimer = 0;
		}
#else
		if (gDragTickTimerCommon != nullptr)
		{
			::CFRunLoopTimerInvalidate(gDragTickTimerCommon);
			::CFRelease(gDragTickTimerCommon);
			gDragTickTimerCommon = nullptr;
		}
		if (gDragTickTimerDefault != nullptr)
		{
			::CFRunLoopTimerInvalidate(gDragTickTimerDefault);
			::CFRelease(gDragTickTimerDefault);
			gDragTickTimerDefault = nullptr;
		}
#endif
		if (st == nullptr || st->disarmed)
			return;
		st->disarmed = true;
		// **通知手続きを外す。** 外さないまま本体が降りると、VW が居ない関数を呼ぶ。
		if (gSDK != nil)
		{
			for (StatusID id : kDragTickNotifications)
				gSDK->UnregisterNotificationProcedure(&DragTickNotify, (OSType)id);
		}
		std::error_code ec;
		if (std::filesystem::exists(st->filePath, ec))
		{
			DragTickAppend(st->filePath,
						   "disarm" + DragTickField("n", DragTickNum(st->ticks)) +
							   DragTickField("common", DragTickNum(st->ticksCommon)) +
							   DragTickField("default", DragTickNum(st->ticksDefault)) +
							   DragTickField("tool_begins", DragTickNum(st->toolBegins)) +
							   DragTickField("tool_ends", DragTickNum(st->toolEnds)) +
							   DragTickField("undo_ends", DragTickNum(st->undoEnds)) +
							   DragTickField("t_ms", DragTickNum(DragTickElapsedMs())) +
							   DragTickField("why", why));
		}
	}

	// -----------------------------------------------------------------------
	// 刻み 1 回。**プローブが戻った後も呼ばれる**ので、ここから殻（結果ダイアログ・
	// ログの受け口）へは触らない——触れるのは自分のファイルと `gSDK` だけ。
	void DragTickTockBody(const char* tag, bool isDefaultMode)
	{
		DragTickState* st = gDragTickState;
		if (st == nullptr)
			return;

		// **書き溜め先が消えていたら店じまい。** 報告（2 回目の実行）はファイルを消す。
		// 2 回目は別の読み込み像で動くので、古い像のタイマーを直接止められない——
		// 「ファイルの有無」がその代わりの合図である。
		std::error_code ec;
		if (!std::filesystem::exists(st->filePath, ec))
		{
			DragTickDisarm("書き溜め先が消えた（報告済み）");
			return;
		}

		++st->ticks;
		const long long ms = DragTickElapsedMs();
		long long dt = 0;
		if (isDefaultMode)
		{
			++st->ticksDefault;
			dt = ms - st->lastMsDefault;
			st->lastMsDefault = ms;
		}
		else
		{
			++st->ticksCommon;
			dt = ms - st->lastMsCommon;
			st->lastMsCommon = ms;
		}

		const std::string head =
			"tick" + DragTickField("tag", tag) + DragTickField("n", DragTickNum(st->ticks)) +
			DragTickField("t_ms", DragTickNum(ms)) + DragTickField("dt_ms", DragTickNum(dt)) +
			DragTickField("phase", st->phase) +
			DragTickField("inside_probe", DragTickYesNo(st->insideProbe)) +
			// **ドラッグ中かどうか**。通知で囲んだ深さと、ランループのモードの 2 本立て。
			DragTickField("tool", DragTickYesNo(st->toolDepth > 0)) +
			DragTickField("mode", DragTickCurrentMode());

		if (st->quiet)
		{
			// 取り消しを掛けている間は gSDK を触らない（段の境目に刻みを混ぜない）。
			DragTickAppend(st->filePath, head + DragTickField("sdk", "skipped-quiet"));
			return;
		}

		// **gSDK を呼ぶ前に「これから呼ぶ」を残す。** 落ちたらこの行が最後に通った場所に
		// なる（落ち方そのものが知見）。
		DragTickAppend(st->filePath, head + DragTickField("sdk", "about-to-call"));

		std::string building = "?";
		std::string readPre = "?";
		long long objs = -1;
		std::string trouble;
		try
		{
			if (gSDK == nil)
			{
				trouble = "gSDK が nil";
			}
			else
			{
				building = DragTickYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
				readPre = DragTickYesNo(gSDK->GetNamedObject(kDragTickNamePre) != nil);
				objs = DragTickCountObjects();
			}
		}
		catch (const std::exception& e)
		{
			trouble = std::string("例外: ") + e.what();
		}
		catch (...)
		{
			trouble = "不明な例外";
		}

		DragTickAppend(st->filePath, head + DragTickField("undo_building", building) +
										 DragTickField("read_pre", readPre) +
										 DragTickField("objs", DragTickNum(objs)) +
										 DragTickField("trouble", trouble.empty() ? "-" : trouble));

		// ---- 書き込み。**`building=yes` の刻みにだけ当てる**（混ざるかを測るため）----
		const bool buildingYes = (building == "yes");
		const char* writeName = nullptr;
		if (buildingYes && st->wantWriteOnBuilding && !st->wroteRender)
		{
			st->wroteRender = true;
			writeName = kDragTickNameRTick;
		}
		else if (buildingYes && !st->insideProbe)
		{
			// **場面で名前を分ける。** 同じ「刻みの中の書き込み」でも、ドラッグ中なのか
			// トラッキング中なのか、ただモーダルが開いているだけなのかで意味が違う。
			const std::string nowMode = DragTickCurrentMode();
			if (st->toolDepth > 0 && !st->wroteUTool)
			{
				st->wroteUTool = true;
				writeName = kDragTickNameUTool;
			}
			else if (nowMode.find("EventTracking") != std::string::npos && !st->wroteUTrack)
			{
				st->wroteUTrack = true;
				writeName = kDragTickNameUTrack;
			}
			else if (st->toolDepth == 0 && nowMode.find("EventTracking") == std::string::npos &&
					 !st->wroteUOther)
			{
				st->wroteUOther = true;
				writeName = kDragTickNameUOther;
			}
		}
		if (writeName != nullptr)
		{
			DragTickAppend(st->filePath,
						   "write" + DragTickField("name", writeName) +
							   DragTickField("n", DragTickNum(st->ticks)) +
							   DragTickField("phase", st->phase) +
							   DragTickField("tool", DragTickYesNo(st->toolDepth > 0)) +
							   DragTickField("mode", DragTickCurrentMode()) +
							   DragTickField("step", "about-to-create-locus"));
			std::string outcome = "作れた";
			try
			{
				MCObjectHandle hNew =
					(gSDK != nil) ? gSDK->CreateLocus(WorldPt(1000, 2000)) : MCObjectHandle(nil);
				if (hNew == nil)
					outcome = "nil が返った";
				else
					gSDK->SetObjectName(hNew, writeName);
			}
			catch (...)
			{
				outcome = "例外で止まった";
			}
			DragTickAppend(st->filePath,
						   "write" + DragTickField("name", writeName) +
							   DragTickField("n", DragTickNum(st->ticks)) +
							   DragTickField("locus", outcome) +
							   DragTickField("objs_after", DragTickNum(DragTickCountObjects())));
		}

		if (st->ticks >= kDragTickMaxTicks)
			DragTickDisarm("刻みの上限に達した");
		else if (ms >= kDragTickDeadlineMs)
			DragTickDisarm("締切に達した");
	}

#if defined(_WIN32)
	void CALLBACK DragTickWinTimerProc(HWND, UINT, UINT_PTR, DWORD)
	{
		DragTickTockBody("win", false);
	}
#else
	void DragTickCFTimerProc(CFRunLoopTimerRef, void* info)
	{
		const char* tag = static_cast<const char*>(info);
		DragTickTockBody(tag, std::string(tag) == kDragTickTagDefault);
	}
#endif

	// -----------------------------------------------------------------------
	// **本体をピン留めする。** 殻はプローブが終わると本体を降ろすので、ピン留めしないと
	// タイマーと通知手続きの行き先が消える。失敗したら仕掛けてはいけない。
	std::string DragTickPinSelf(std::string& outPath)
	{
#if defined(_WIN32)
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&DragTickPinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した";
		outPath = "(windows)";
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&DragTickPinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		// RTLD_NOLOAD は「すでに読み込まれているときだけ」ハンドルを返す。返ったハンドルは
		// 参照を 1 つ持つ——**閉じない**のがここの目的（殻の dlclose では降りなくなる）。
		void* handle = ::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
		if (handle == nullptr)
			return std::string("dlopen(RTLD_NOLOAD) が失敗した: ") + info.dli_fname;
		outPath = info.dli_fname;
		return std::string();
#endif
	}

	// -----------------------------------------------------------------------
	// 書き溜めの 1 行から値を切り出す（`鍵=値` を ` ##` で区切った形）。
	std::string DragTickReadField(const std::string& line, const std::string& key)
	{
		const std::string needle = " ## " + key + "=";
		const size_t at = line.find(needle);
		if (at == std::string::npos)
			return std::string();
		const size_t from = at + needle.size();
		const size_t end = line.find(" ##", from);
		return line.substr(from, (end == std::string::npos) ? end : end - from);
	}

	// 「局面 × 口」ごとの畳み込み。
	struct DragTickSlice
	{
		std::string key;
		int count = 0;
		long long minDt = 0;
		long long maxDt = 0;
		long long sumDt = 0;
		int buildingYes = 0;
		int readOk = 0;
		std::string modes;
	};

	// -----------------------------------------------------------------------
	// 2 回目の実行（報告）。書き溜めを読んで畳み、**読み終えたら消す**。
	void DragTickReport(vwprobe::Report& probe, const std::string& path)
	{
		std::vector<std::string> lines;
		{
			std::ifstream in(path);
			std::string line;
			while (std::getline(in, line))
				lines.push_back(line);
		}
		probe.log("書き溜め: " + path + "（" + std::to_string(lines.size()) + " 行）");
		probe.log("");

		// 刻み 1 行ぶんの素。モードの数え上げと「レンダリングの窓」の突き合わせに使う。
		struct DragTickRow
		{
			long long tMs = 0;
			std::string mode;
			std::string phase;
			bool building = false;
			bool tool = false;
			std::string raw;
		};
		std::vector<DragTickRow> rows;
		std::vector<std::pair<long long, std::string>> renderMarks; // RndC / RnMC の時刻

		std::vector<DragTickSlice> slices;
		int ticksTotal = 0;
		int ticksTool = 0;		   // 通知で囲まれた中（＝点取りの最中）
		int ticksToolBuilding = 0; // そのうち undo イベントが開いていたもの
		int ticksToolRead = 0;
		int ticksTracking = 0; // mode に NSEventTracking を含むもの
		int ticksTrouble = 0;
		int toolBegins = 0;
		int toolEnds = 0;
		int undoEnds = 0;
		int renderNotes = 0;
		std::string toolModes;
		for (const std::string& line : lines)
		{
			if (line.compare(0, 5, "note ") == 0)
			{
				const std::string what = DragTickReadField(line, "what");
				if (what == "tool-begin")
					++toolBegins;
				else if (what == "tool-end")
					++toolEnds;
				else if (what == "undo-end")
					++undoEnds;
				else if (what.compare(0, 11, "render-mode") == 0)
				{
					++renderNotes;
					renderMarks.emplace_back(std::atoll(DragTickReadField(line, "t_ms").c_str()),
											 what);
				}
				continue;
			}
			if (line.compare(0, 5, "tick ") != 0)
				continue;
			const std::string sdk = DragTickReadField(line, "sdk");
			if (sdk == "about-to-call")
				continue; // 前触れの行は数えない（本行と二重になる）

			++ticksTotal;
			const std::string phase = DragTickReadField(line, "phase");
			const std::string tag = DragTickReadField(line, "tag");
			const std::string mode = DragTickReadField(line, "mode");
			const bool buildingYes = (DragTickReadField(line, "undo_building") == "yes");
			const bool readOk = (DragTickReadField(line, "read_pre") == "yes");
			const bool inTool = (DragTickReadField(line, "tool") == "yes");
			const long long dt = std::atoll(DragTickReadField(line, "dt_ms").c_str());
			if (!DragTickReadField(line, "trouble").empty() &&
				DragTickReadField(line, "trouble") != "-")
				++ticksTrouble;
			if (inTool)
			{
				++ticksTool;
				if (buildingYes)
					++ticksToolBuilding;
				if (readOk)
					++ticksToolRead;
				if (toolModes.find(mode) == std::string::npos)
					toolModes += (toolModes.empty() ? "" : ",") + mode;
			}
			if (mode.find("EventTracking") != std::string::npos)
				++ticksTracking;
			DragTickRow row;
			row.tMs = std::atoll(DragTickReadField(line, "t_ms").c_str());
			row.mode = mode;
			row.phase = phase;
			row.building = buildingYes;
			row.tool = inTool;
			row.raw = line;
			rows.push_back(row);

			const std::string key = phase + " / " + tag;
			DragTickSlice* found = nullptr;
			for (DragTickSlice& s : slices)
				if (s.key == key)
					found = &s;
			if (found == nullptr)
			{
				slices.push_back(DragTickSlice{key, 0, dt, dt, 0, 0, 0, std::string()});
				found = &slices.back();
			}
			++found->count;
			found->minDt = std::min(found->minDt, dt);
			found->maxDt = std::max(found->maxDt, dt);
			found->sumDt += dt;
			if (buildingYes)
				++found->buildingYes;
			if (readOk)
				++found->readOk;
			if (found->modes.find(mode) == std::string::npos)
				found->modes += (found->modes.empty() ? "" : ",") + mode;
		}

		probe.log("■ 局面 / 口ごとの刻み（dt_ms = 同じ口の前の刻みからの間隔。"
				  "仕掛けた間隔は 250 ms）");
		for (const DragTickSlice& s : slices)
		{
			const long long avg = (s.count > 0) ? (s.sumDt / s.count) : 0;
			probe.log("  " + s.key + ": " + std::to_string(s.count) + " 回, dt 最小 " +
					  std::to_string(s.minDt) + " / 平均 " + std::to_string(avg) + " / 最大 " +
					  std::to_string(s.maxDt) + " ms, building=yes " +
					  std::to_string(s.buildingYes) + " 回, 読めた " + std::to_string(s.readOk) +
					  " 回, mode=" + s.modes);
		}
		probe.log("  刻みの合計=" + std::to_string(ticksTotal) +
				  " / 異常のあった刻み=" + std::to_string(ticksTrouble) + " 回");
		probe.log("");

		// ---- (1) ドラッグ中の刻み -------------------------------------------
		probe.log("■ (1) ツールのドラッグ中（点取りの最中）の刻み");
		probe.log("  点取りの通知: kNotifyBeginToolMode=" + std::to_string(toolBegins) +
				  " 回 / kNotifyEndToolMode=" + std::to_string(toolEnds) + " 回");
		probe.log("  VW が undo イベントを閉じた回数（kNotifyUndoEndEvent）=" +
				  std::to_string(undoEnds) + " 回");
		probe.log("  レンダリングモードの通知（RndC / RnMC）=" + std::to_string(renderNotes) +
				  " 回");
		probe.log("  mode に EventTracking が出た刻み=" + std::to_string(ticksTracking) + " 回");
		probe.log("  **点取りの通知で囲まれた中で届いた刻み=" + std::to_string(ticksTool) +
				  " 回**（うち building=yes " + std::to_string(ticksToolBuilding) +
				  " 回 / 読めた " + std::to_string(ticksToolRead) + " 回）");
		if (!toolModes.empty())
			probe.log("  その刻みの mode=" + toolModes);

		if (toolBegins == 0)
		{
			probe.fail("**点取りの通知が 1 度も来ていない**——この回はツールのドラッグに"
					   "ついて何も言えない（ドラッグされなかったか、通知が届かなかった）。"
					   "もう一度、仕掛けてから**図面でツールをドラッグして**走らせること");
		}
		else if (ticksTool == 0)
		{
			probe.log("  → 点取りは " + std::to_string(toolBegins) +
					  " 回あったが、その最中に刻みは 1 回も届かなかった。"
					  "**これは「ドラッグ中には当たらない」の根拠として読める。**");
		}
		else if (ticksToolBuilding == 0)
		{
			probe.log("  → ドラッグ中に刻みは届いたが、**そのとき undo イベントは"
					  "開いていなかった**（building=yes が 0 回）。"
					  "＝点取りの最中は VW はまだイベントを開いていない。");
		}
		else
		{
			probe.log("  → **ドラッグ中に、VW が undo イベントを開いたまま刻みが届いた**"
					  "（building=yes が " +
					  std::to_string(ticksToolBuilding) + " 回）。");
		}
		probe.log("");

		// ---- モードごとの数え上げ（どのモードが刻みを配ったか）----------------
		probe.log("■ モードごとの刻み（mac。どのランループのモードで配られたか）");
		{
			struct DragTickModeStat
			{
				std::string mode;
				int count = 0;
				int building = 0;
				int tool = 0;
			};
			std::vector<DragTickModeStat> hist;
			for (const DragTickRow& r : rows)
			{
				DragTickModeStat* found = nullptr;
				for (DragTickModeStat& h : hist)
					if (h.mode == r.mode)
						found = &h;
				if (found == nullptr)
				{
					hist.push_back(DragTickModeStat{r.mode, 0, 0, 0});
					found = &hist.back();
				}
				++found->count;
				if (r.building)
					++found->building;
				if (r.tool)
					++found->tool;
			}
			for (const DragTickModeStat& h : hist)
				probe.log("  " + h.mode + ": " + std::to_string(h.count) +
						  " 回（うち building=yes " + std::to_string(h.building) +
						  " 回 / 点取りの囲みの中 " + std::to_string(h.tool) + " 回）");
			probe.log("  ← **どのモードの刻みが undo イベント中に当たったか**がここで分かる。");
		}
		probe.log("");

		// ---- ドラッグ中の刻みの実物（最大 12 行）------------------------------
		if (ticksTool > 0)
		{
			probe.log("■ ドラッグ中（点取りの通知に囲まれた中）の刻み——実物（最大 12 行）");
			int shown = 0;
			for (const DragTickRow& r : rows)
			{
				if (!r.tool || shown >= 12)
					continue;
				probe.log("  " + r.raw);
				++shown;
			}
			probe.log("");
		}

		// ---- レンダリングの窓（モード変更の通知のあと 15 秒）------------------
		if (!renderMarks.empty())
		{
			probe.log("■ 利用者のレンダリングの窓（RndC / RnMC の通知のあと 15 秒の刻み）");
			probe.log("  **これはレンダリングの開始通知ではない**（モードの変更）。描き始めの"
					  "目印として読む。");
			for (const std::pair<long long, std::string>& mark : renderMarks)
			{
				int inWindow = 0;
				int buildingYes = 0;
				std::string modes;
				for (const DragTickRow& r : rows)
				{
					if (r.tMs < mark.first || r.tMs > mark.first + 15000)
						continue;
					++inWindow;
					if (r.building)
						++buildingYes;
					if (modes.find(r.mode) == std::string::npos)
						modes += (modes.empty() ? "" : ",") + r.mode;
				}
				probe.log("  " + mark.second + " @" + std::to_string(mark.first) + " ms → 刻み " +
						  std::to_string(inWindow) + " 回（うち building=yes " +
						  std::to_string(buildingYes) + " 回）mode=" + modes);
			}
			probe.log("");
		}
		else
		{
			probe.log("■ 利用者のレンダリングの窓: **モード変更の通知が 1 度も来ていない**"
					  "——この回はレンダリングされていない見込み。");
			probe.log("");
		}

		// ---- 要点の行 --------------------------------------------------------
		probe.log("■ 要点の行（仕掛け・通知・書き込み・店じまい）");
		for (const std::string& line : lines)
		{
			if (line.compare(0, 4, "arm ") == 0 || line.compare(0, 6, "phase ") == 0 ||
				line.compare(0, 6, "write ") == 0 || line.compare(0, 7, "disarm ") == 0 ||
				line.compare(0, 6, "modes ") == 0 || line.compare(0, 5, "note ") == 0 ||
				line.compare(0, 7, "render ") == 0)
				probe.log(line);
		}
		probe.log("");
		probe.log("■ 刻みの行（最後の 20 行）");
		const size_t from = (lines.size() > 20) ? (lines.size() - 20) : 0;
		for (size_t i = from; i < lines.size(); ++i)
			probe.log(lines[i]);

		// ---- 刻みの中で作ったものが残っているか ------------------------------
		probe.log("");
		probe.log("■ 刻みの中で作った図形（取り消しは掛けない——利用者の操作の段が上に"
				  "積まれているので、段の境目が読めない）");
		probe.log("  " + std::string(kDragTickNameUTool) +
				  "（点取りの最中）は在るか=" + DragTickYesNo(DragTickExists(kDragTickNameUTool)));
		probe.log("  " + std::string(kDragTickNameUTrack) + "（トラッキング中）は在るか=" +
				  DragTickYesNo(DragTickExists(kDragTickNameUTrack)));
		probe.log("  " + std::string(kDragTickNameUOther) +
				  "（それ以外）は在るか=" + DragTickYesNo(DragTickExists(kDragTickNameUOther)));
		probe.log("  （在れば「利用者の局面の刻みの中で、building=yes のまま書けた」"
				  "ということ。混ざるかどうかは 1 回目の P-R で段ごとに測ってある）");

		std::error_code ec;
		std::filesystem::remove(path, ec);
		probe.log("");
		probe.log("書き溜めを消した（まだ刻んでいる古いタイマーは、次の刻みで自分から止まる）。");
	}
} // namespace

VW_PROBE("drag-render-timer-tick", "ドラッグ中・レンダリング中の刻み",
		 "コマンドが戻った後も生きるタイマーを仕掛け、点取りとレンダリングの最中を測る")
{
	const std::string path = DragTickFilePath();
	std::error_code ec;
	const bool haveTicks = std::filesystem::exists(path, ec);

	probe.log("書き溜め先: " + path);
	if (haveTicks)
	{
		probe.log("→ **2 回目の実行**と見なして、前回の書き溜めを報告する。");
		probe.log("");
		DragTickReport(probe, path);
		return;
	}

	// ---------------------------------------------------------------- 仕掛ける
	std::string pinnedPath;
	const std::string pinTrouble = DragTickPinSelf(pinnedPath);
	if (!pinTrouble.empty())
	{
		probe.fail("本体をピン留めできなかったので仕掛けない（" + pinTrouble + "）");
		return;
	}
	probe.log("本体をピン留めした（殻が降ろしても、タイマーと通知の行き先は残る）。");

	gDragTickState = new DragTickState();
	gDragTickState->filePath = path;
	gDragTickState->armedAt = std::chrono::steady_clock::now();
	gDragTickState->phase = "P0 仕掛けた直後";

	const bool buildingAtStart = gSDK->IsCurrentlyBuildingAnUndoEvent();
	probe.log("走り出しの building=" + DragTickYesNo(buildingAtStart));
	if (buildingAtStart)
	{
		probe.log("【重大】走り出しから開いている（前のコマンドの置き土産。Findings「Undo」）。");
		probe.log("対照 PRE が「どのイベントにも属さない」ものにならないので、**この回は");
		probe.log("(3)（混ざるか）を判定できない**。(1)(2)（届くか・読めるか）は測れる。");
	}

	// --- レンダリングに時間を使わせる 3D 図形を、**いちばん下の段へ**置く ---------
	// 空の図面はレンダリングが一瞬で終わるので、刻みを当てる窓が開かない。
	// **これを対照 PRE より先に作るのが肝**——あとで取り消しを 2 段掛けるので、
	// 球が PRE と同じか上の段に居ると、2 段目で球まで消えて利用者のレンダリングが
	// 一瞬で終わってしまう（段は下から 球 → PRE → VW が開くもの の順になる）。
	probe.log("");
	probe.log("■ レンダリング用の 3D 図形を作る（取り消し 2 段では消えない下の段へ）");
	int spheres = 0;
	for (int i = 0; i < 6; ++i)
	{
		MCObjectHandle hBall =
			gSDK->CreateSphere(WorldPt3(i * 2000.0, 0.0, 1000.0), WorldCoord(900.0));
		if (hBall != nil)
			++spheres;
	}
	probe.log("  球を " + std::to_string(spheres) + " 個作った");
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
	{
		gSDK->EndUndoEvent();
		probe.log("  球で開いたイベントを閉じた。building=" +
				  DragTickYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()));
	}

	// --- 対照 PRE を作り、**その場で閉じて別の段へ送る** -------------------------
	// `CreateLocus` 1 つでも undo イベントが開く（Findings「Undo」。#206 の実機で判明）。
	// 閉じないと PRE が「このあと VW が開くイベント」と同じ段に乗りかねない。
	probe.log("");
	probe.log("■ 対照 PRE を作る（作った直後に EndUndoEvent で閉じて、別の段へ送る）");
	MCObjectHandle hPre = gSDK->CreateLocus(WorldPt(0, 0));
	if (hPre == nil)
	{
		probe.fail("CreateLocus が nil を返した（対照 PRE を作れなかった）");
	}
	else
	{
		gSDK->SetObjectName(hPre, kDragTickNamePre);
		if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		{
			const Boolean ended = gSDK->EndUndoEvent();
			probe.log(std::string("PRE を作ったらイベントが開いたので閉じた。EndUndoEvent=") +
					  (ended ? "true" : "false") + " / 閉じた後の building=" +
					  DragTickYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()));
		}
		else
		{
			probe.log("PRE を作ってもイベントは開かなかった（そのまま対照になる）。");
		}
		probe.log("PRE を名前で引けた=" + DragTickYesNo(DragTickExists(kDragTickNamePre)));
	}

	// --- 通知手続きを登録する（ドラッグの両端を機械で押さえる）-------------------
	probe.log("");
	probe.log("■ 通知手続きを登録する（点取り・undo の閉じ・レンダリングモードの変更）");
	std::string registered;
	for (StatusID id : kDragTickNotifications)
	{
		const Boolean ok = gSDK->RegisterNotificationProcedure(&DragTickNotify, (OSType)id);
		char tag[8] = {0};
		tag[0] = (char)((id >> 24) & 0xFF);
		tag[1] = (char)((id >> 16) & 0xFF);
		tag[2] = (char)((id >> 8) & 0xFF);
		tag[3] = (char)(id & 0xFF);
		registered += (registered.empty() ? "" : " / ") + std::string(tag) + "=" +
					  (ok ? std::string("ok") : std::string("失敗"));
	}
	probe.log("  " + registered);
	probe.log("  （BTOO/ETOO が点取りの両端、Udee が『VW がイベントを閉じる直前』、");
	probe.log("   RndC/RnMC はレンダリングモードの変更。**レンダリングの開始通知は無い**）");

	// --- タイマーを仕掛ける ------------------------------------------------------
	probe.log("");
#if defined(_WIN32)
	const std::string threadNote =
		std::string("win-thread-") + std::to_string((unsigned long)::GetCurrentThreadId());
	probe.log("■ SetTimer で仕掛ける（250 ms。Windows にモードの概念は無いので 1 本）");
#else
	const std::string threadNote = (::pthread_main_np() != 0) ? "main" : "worker";
	probe.log("■ CFRunLoopTimer を 2 本仕掛ける（250 ms。**登録するモードだけが違う**）");
#endif
	probe.log("  走っているスレッド: " + threadNote);

	DragTickAppend(path, "arm" + DragTickField("group", VW_PAYLOAD_GROUP) +
							 DragTickField("interval_ms", DragTickNum(kDragTickIntervalMs)) +
							 DragTickField("thread", threadNote) +
							 DragTickField("mode_at_arm", DragTickCurrentMode()) +
							 DragTickField("pinned", pinnedPath) +
							 DragTickField("building_at_arm", DragTickYesNo(buildingAtStart)) +
							 DragTickField("objs_at_arm", DragTickNum(DragTickCountObjects())));

#if defined(_WIN32)
	gDragTickWinTimer = ::SetTimer(nullptr, 0, (UINT)kDragTickIntervalMs, &DragTickWinTimerProc);
	const bool armed = (gDragTickWinTimer != 0);
	probe.log(armed ? "  SetTimer で仕掛けた" : "  SetTimer が 0 を返した");
#else
	const CFAbsoluteTime interval = (CFAbsoluteTime)kDragTickIntervalMs / 1000.0;
	static CFRunLoopTimerContext ctxCommon = {0, const_cast<char*>(kDragTickTagCommon), nullptr,
											  nullptr, nullptr};
	static CFRunLoopTimerContext ctxDefault = {0, const_cast<char*>(kDragTickTagDefault), nullptr,
											   nullptr, nullptr};
	gDragTickTimerCommon =
		::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + interval,
							   interval, 0, 0, &DragTickCFTimerProc, &ctxCommon);
	gDragTickTimerDefault =
		::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + interval,
							   interval, 0, 0, &DragTickCFTimerProc, &ctxDefault);
	const bool armed = (gDragTickTimerCommon != nullptr && gDragTickTimerDefault != nullptr);
	if (armed)
	{
		CFRunLoopRef mainLoop = ::CFRunLoopGetMain();
		// **共通モードの 1 本に加えて、主ランループが知っている全モードへも入れる**
		// （#204 の 4: 全モードへ入れると取りこぼしが減る）。もう 1 本は
		// `kCFRunLoopDefaultMode` だけに入れて、**既定モードだけのタイマーが
		// ドラッグ／レンダリングの最中に止まるか**を同じ瞬間に見比べる。
		::CFRunLoopAddTimer(mainLoop, gDragTickTimerCommon, kCFRunLoopCommonModes);
		std::string modeList;
		CFArrayRef modes = ::CFRunLoopCopyAllModes(mainLoop);
		if (modes != nullptr)
		{
			const CFIndex count = ::CFArrayGetCount(modes);
			for (CFIndex i = 0; i < count; ++i)
			{
				CFStringRef mode = (CFStringRef)::CFArrayGetValueAtIndex(modes, i);
				if (mode == nullptr)
					continue;
				::CFRunLoopAddTimer(mainLoop, gDragTickTimerCommon, mode);
				char buf[160] = {0};
				if (::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8))
					modeList += (modeList.empty() ? "" : ",") + std::string(buf);
			}
			::CFRelease(modes);
		}
		::CFRunLoopAddTimer(mainLoop, gDragTickTimerDefault, kCFRunLoopDefaultMode);
		DragTickAppend(path,
					   "modes" + DragTickField("all", modeList.empty() ? "(none)" : modeList));
		probe.log("  1 本目: kCFRunLoopCommonModes ＋ 主ランループが知る全モード");
		probe.log("  2 本目: kCFRunLoopDefaultMode だけ");
		probe.log("  主ランループが知っているモード: " + (modeList.empty() ? "(none)" : modeList));
	}
	else
	{
		probe.log("  CFRunLoopTimerCreate が nullptr を返した");
	}
#endif
	if (!armed)
	{
		probe.fail("タイマーを仕掛けられなかった");
		return;
	}

	// ------------------------------------------------------------------------
	// P-R **レンダリング中 ＋ VW が開いた undo イベントの最中**（利用者に依らない測定）
	// ------------------------------------------------------------------------
	probe.log("");
	// **1 回目の実機（ビルド 87d374350eda）で分かったこと**: undo イベントを開いたまま
	// `SetRenderMode(renderOpenGL, true, true)` を呼ぶと **`false` が返って 0 ms で戻る**
	// ——レンダリングが 1 度も走らなかった。ヘッダのコメントは返り値について何も言わない
	// （`ci-debug` で確認済み。「immediate なら戻る前に全部描く」だけ）。そこで
	// **「道具が効くかどうか」と「イベント中だから断られたのか」を切り分ける**:
	//
	//   P-R0（対照）… **イベントの外**で、経路と描画モードを順に試して効くものを見つける。
	//   P-R （本題）… 効いた経路を、**VW にイベントを開かせたまま**もう一度呼ぶ。
	//
	// P-R0 が効いて P-R が断られたなら、それ自体が知見である（VW は undo イベント中は
	// レンダリングを始めない）。両方断られたなら、この梃子では場面を作れないということ
	// ——そのときは利用者の側のレンダリング（P-D）だけが頼りになる。
	probe.log("■ P-R0 対照: **undo イベントの外**でレンダリングさせてみる（道具が効くか）");
	gDragTickState->phase = "P-R0 レンダリング中（イベントの外）";
	DragTickAppend(path, std::string("phase") + DragTickField("name", "P-R0"));
	MCObjectHandle layer = gSDK->GetActiveLayer();
	probe.log("  いまのレンダリングモード GetRenderMode=" +
			  std::to_string((layer != nil) ? (int)gSDK->GetRenderMode(layer) : -1) +
			  "（0=ワイヤーフレーム 3=陰線処理（シェイド） 5=仕上げ 11=OpenGL）");

	// 試す経路。**効いた最初のものを本題で使い回す。**
	struct DragTickRenderTry
	{
		const char* name;
		TRenderMode mode;
		bool viaScript; // true なら VectorScript の SetLayerRenderMode を使う
	};
	const DragTickRenderTry kTries[] = {
		{"SetRenderMode(renderOpenGL)", renderOpenGL, false},
		{"SetRenderMode(renderShadedSolid)", renderShadedSolid, false},
		{"SetRenderMode(renderFinalShaded)", renderFinalShaded, false},
		{"VectorScript SetLayerRenderMode(renderOpenGL)", renderOpenGL, true},
	};

	VCOMPtr<VectorWorks::Scripting::IVectorScriptEngine> engine(
		VectorWorks::Scripting::IID_VectorScriptEngine);
	if (!engine)
		probe.log("  （IVectorScriptEngine を取れなかったので、スクリプト経路は試せない）");

	// 1 つ試して「効いたか」を返す。効いた＝**時間が掛かった**か**モードが変わった**か。
	const auto tryRender = [&](const DragTickRenderTry& t, const char* phaseTag) -> bool
	{
		if (layer == nil)
			return false;
		const int ticksBefore = gDragTickState->ticks;
		const std::chrono::steady_clock::time_point from = std::chrono::steady_clock::now();
		std::string ret = "-";
		if (t.viaScript)
		{
			if (!engine)
				return false;
			// 取り消しの段を増やさない呼び方（描画モードの変更そのものは図形を作らない）。
			const std::string code =
				"SetLayerRenderMode(ActLayer, " + std::to_string((int)t.mode) + ", TRUE, TRUE);";
			const VCOMError err = engine->ExecuteScript(code.c_str());
			ret = std::string("VCOMError=") + std::to_string((int)err);
		}
		else
		{
			ret = gSDK->SetRenderMode(layer, t.mode, true, true) ? "true" : "false";
		}
		const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
								 std::chrono::steady_clock::now() - from)
								 .count();
		const int ticks = gDragTickState->ticks - ticksBefore;
		const int now = (int)gSDK->GetRenderMode(layer);
		const bool worked = (ms >= 150) || (now == (int)t.mode);
		probe.log(std::string("  ") + phaseTag + " " + t.name + " → 戻り=" + ret +
				  " / 掛かった時間=" + std::to_string(ms) + " ms / 刻み=" + std::to_string(ticks) +
				  " 回 / 後の GetRenderMode=" + std::to_string(now) + " → " +
				  (worked ? "**効いた**" : "効かなかった"));
		DragTickAppend(path, std::string("render") + DragTickField("phase", phaseTag) +
								 DragTickField("try", t.name) + DragTickField("ret", ret) +
								 DragTickField("ms", DragTickNum(ms)) +
								 DragTickField("ticks", DragTickNum(ticks)) +
								 DragTickField("mode_after", DragTickNum(now)) +
								 DragTickField("worked", DragTickYesNo(worked)));
		return worked;
	};

	const DragTickRenderTry* chosen = nullptr;
	for (const DragTickRenderTry& t : kTries)
	{
		// 毎回ワイヤーフレームへ戻してから試す（「すでにそのモードだから一瞬で済んだ」を
		// 「効かなかった」と読み違えないため）。
		if (layer != nil)
			gSDK->SetRenderMode(layer, renderWireFrame, true, false);
		if (tryRender(t, "P-R0"))
		{
			chosen = &t;
			break;
		}
	}
	if (chosen == nullptr)
	{
		probe.log("  → **どの経路でもレンダリングを起こせなかった。** この回は P-R"
				  "（イベント中のレンダリング）を作れない——利用者の側のレンダリング"
				  "（P-D ②）だけが頼りになる。");
	}
	else
	{
		probe.log("  → 効いた経路: " + std::string(chosen->name) + "。本題でこれを使う。");
	}

	// ------------------------------------------------------------------------
	probe.log("");
	probe.log("■ P-R 本題: VW に undo イベントを開かせたまま、同じ経路でレンダリングさせる");
	probe.log("  梃子 1: DeleteObject(h, useUndo=true) は、開いていなければ自分で開き、");
	probe.log("  プローブが return するまで開いたまま（Findings「Undo」で実測済み）。");
	gDragTickState->phase = "P-R レンダリング中（VW のイベント中）";
	DragTickAppend(path, std::string("phase") + DragTickField("name", "P-R"));

	MCObjectHandle hVictim = gSDK->CreateLocus(WorldPt(2000, 0));
	if (hVictim != nil)
	{
		gSDK->SetObjectName(hVictim, kDragTickNameVictim);
		gSDK->DeleteObject(hVictim, true);
	}
	const bool buildingBeforeRender = gSDK->IsCurrentlyBuildingAnUndoEvent();
	probe.log("  レンダリング前の building=" + DragTickYesNo(buildingBeforeRender) +
			  " ← **ここが yes でなければ、P-R は『イベント中』の測定として読めない**");

	bool renderWorkedInEvent = false;
	if (chosen != nullptr)
	{
		// **ワイヤーフレームへ戻すのもイベントの中でやる**（戻す呼び出しが効くかどうかも
		// 「イベント中は断られる」の証拠になる）。
		if (layer != nil)
			gSDK->SetRenderMode(layer, renderWireFrame, true, false);
		gDragTickState->wantWriteOnBuilding = true;
		renderWorkedInEvent = tryRender(*chosen, "P-R");
		gDragTickState->wantWriteOnBuilding = false;
	}
	probe.log("  レンダリング後の building=" +
			  DragTickYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()));
	probe.log("  刻みの中で作った " + std::string(kDragTickNameRTick) +
			  " は在るか=" + DragTickYesNo(DragTickExists(kDragTickNameRTick)));
	probe.log("");
	if (chosen != nullptr && !renderWorkedInEvent)
	{
		probe.log("  → **イベントの外では効いた経路が、イベント中は効かなかった。**");
		probe.log("    ＝VW は undo イベントが開いている間はレンダリングを始めない"
				  "（この梃子では『レンダリング中 ＋ イベント中』は作れない）。");
	}
	else if (renderWorkedInEvent)
	{
		probe.log("  → イベント中でもレンダリングは走った。上の『刻み』の回数が"
				  "「レンダリング中に刻みが届くか」の答えである。");
	}
	if (!buildingBeforeRender)
	{
		probe.fail("DeleteObject(useUndo=true) の後も undo イベントが開かなかった"
				   "——P-R の結果は『VW のイベント中』の測定として読めない");
	}

	// ------------------------------------------------------------------------
	// P-U 取り消しを段ごとに掛けて、刻みが作ったものがどの段に居たかを読む
	// ------------------------------------------------------------------------
	probe.log("");
	probe.log("■ P-U 取り消しを段ごとに掛けて、何が消えたかを名前で読む（(3) の答え）");
	gDragTickState->quiet = true; // 取り消しの最中に刻みを混ぜない
	gDragTickState->phase = "P-U 取り消しの最中（刻みは gSDK を触らない）";
	DragTickAppend(path, std::string("phase") + DragTickField("name", "P-U"));

	const bool rtickBefore = DragTickExists(kDragTickNameRTick);
	const bool preBefore = DragTickExists(kDragTickNamePre);
	probe.log("  取り消す前: RTICK=" + DragTickYesNo(rtickBefore) +
			  " / PRE=" + DragTickYesNo(preBefore));
	if (!rtickBefore)
	{
		probe.log("  → **刻みの中で図形を作れていないので、(3) はこの回では測れない**");
		probe.log("    （レンダリング中に building=yes の刻みが 1 回も来なかった）。");
	}
	else
	{
		if (!engine) // 上の P-R0 で取ったものを使い回す
		{
			probe.fail(
				"IVectorScriptEngine を取れなかった（取り消しを掛けられないので (3) は未測）");
		}
		else
		{
			// 取り消しは VectorScript 経由（Findings「Undo」「間接経路」で実測済み）。
			engine->ExecuteScript("DoMenuTextByName('Undo', 0);");
			const bool rtick1 = DragTickExists(kDragTickNameRTick);
			const bool pre1 = DragTickExists(kDragTickNamePre);
			probe.log("  **1 段目の取り消しの後**: RTICK=" + DragTickYesNo(rtick1) +
					  " / PRE=" + DragTickYesNo(pre1));
			engine->ExecuteScript("DoMenuTextByName('Undo', 0);");
			const bool rtick2 = DragTickExists(kDragTickNameRTick);
			const bool pre2 = DragTickExists(kDragTickNamePre);
			probe.log("  **2 段目の取り消しの後**: RTICK=" + DragTickYesNo(rtick2) +
					  " / PRE=" + DragTickYesNo(pre2));

			probe.log("");
			if (!rtick1 && pre1)
			{
				probe.log("  → **判定: 混ざった。** 1 段目で、レンダリング中の刻みが作った"
						  "ものだけが消え、対照 PRE は残った。");
				if (!pre2)
					probe.log("    2 段目で PRE が消えたので、2 つは**本当に別の段に居た**"
							  "——1 段目の結果は『取り消しが広く効いた』ではない。");
				else
					probe.log("    ただし 2 段目でも PRE が消えていない（段の境目は読めて"
							  "いない）。");
			}
			else if (rtick1)
			{
				probe.log("  → **判定: 混ざっていない。** 1 段目の取り消しで、刻みが作った"
						  "ものは消えなかった（＝VW が開いていた段には入っていない）。");
			}
			else
			{
				probe.log("  → **判定不能。** 1 段目で対照 PRE まで消えた——この取り消しは"
						  "想定より広く効いているので、RTICK が消えたことを『混ざった』の"
						  "証拠にできない（#206 の 2 回目と同じ形）。");
			}
		}
	}
	gDragTickState->quiet = false;

	// ------------------------------------------------------------------------
	// P-D コマンドが戻った後——**ここからが利用者の出番**
	// ------------------------------------------------------------------------
	probe.log("");
	// **プローブが開けさせた undo イベントを、ここで閉じる。** 1 回目の実機では開いたまま
	// 返しており（殻のログの `undo: after building=yes`）、**戻った直後の刻みが
	// その置き土産のイベントを見て `building=yes` と読んでいた**——しかもそのときの
	// mode は `NSModalPanelRunLoopMode`（殻の結果ダイアログ）で、ドラッグでも
	// レンダリングでもない。置き土産を残したままでは、**この先の `building=yes` が
	// VW のものか自分のものか区別できない**ので、必ず閉じる。
	probe.log("");
	probe.log("■ 置き土産の始末: プローブが開けさせた undo イベントを閉じる");
	int closed = 0;
	while (gSDK->IsCurrentlyBuildingAnUndoEvent() && closed < 4)
	{
		gSDK->EndUndoEvent();
		++closed;
	}
	const bool buildingAtReturn = gSDK->IsCurrentlyBuildingAnUndoEvent();
	probe.log("  EndUndoEvent を " + std::to_string(closed) +
			  " 回呼んだ / 残り building=" + DragTickYesNo(buildingAtReturn));
	probe.log("  ← **ここが no でなければ、この先の building=yes は自分の置き土産かもしれない**");
	DragTickAppend(path, std::string("phase") + DragTickField("name", "P-D の前に始末") +
							 DragTickField("closed", DragTickNum(closed)) +
							 DragTickField("building_at_return", DragTickYesNo(buildingAtReturn)));

	probe.log("");
	probe.log("■ P-D ここから先は、このコマンドが戻った後の刻みを書き溜める");
	probe.log("  **このまま 30 秒〜1 分ほど、図面で次をしてみてください:**");
	probe.log("    ① ツールでドラッグする（長方形ツールで何度か描く・図形を選んで");
	probe.log("       ドラッグで動かす）——これが (1) の本命です。**ゆっくり、2〜3 秒");
	probe.log("       かけて**動かしてください（刻みは 250 ms ごとなので、速いと入りません）");
	probe.log("    ② ビュー > レンダリング で描かせる（OpenGL → Final Quality Renderworks）");
	probe.log("       ——球を 6 個置いてあるので、少し時間が掛かります");
	probe.log("  そのあと**もう一度このプローブを走らせる**と、結果が出ます。");
	probe.log("  （ドラッグしたかどうかは点取りの通知で機械的に押さえるので、");
	probe.log("    こちらから見て『本当にドラッグされたか』が分かります）");
	probe.log("  タイマーは 15 分で自分から止まります。");
	DragTickAppend(path, std::string("phase") + DragTickField("name", "P-D 戻った後"));
	gDragTickState->phase = "P-D 戻った後";
	gDragTickState->insideProbe = false;
}
