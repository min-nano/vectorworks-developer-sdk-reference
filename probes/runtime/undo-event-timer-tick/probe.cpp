//
//	probes/runtime/undo-event-timer-tick/probe.cpp
//
//	[issue #206] OS のランループタイマーの刻みが「**VW 自身が undo イベントを開いたまま、
//	VW が自分でイベントループを回している最中**」に当たったら何が起きるかを実測する。
//
//	■ なぜプローブの側からその状況を作れるのか——梃子は 2 つ
//
//	#206 は「**プローブは undo イベントを自分では開かない決まり**なので、VW が開いている
//	イベントの最中をプローブから狙って作れない」として #204 の範囲から外された。だが
//	`Findings/Undo.md` には、**VW（SDK 内部）が勝手にイベントを開く経路**が実測で並んで
//	いる。そのうち最も素直な 1 本を梃子に使えば、**プローブは 1 度も
//	`SetUndoMethod` / `NameUndoEvent` を呼ばないまま**「VW が開いた undo イベントの最中」
//	に入れる:
//
//	  梃子 1（イベントを開かせる）… `DeleteObject(h, useUndo=true)` は
//	    「呼び出し時点で undo イベントが開いていなければ**自分で開く**」。しかも
//	    **プローブ関数が return するまで開いたまま**（`EndUndoEvent` の類を呼ばなくても）。
//	    ——`Findings/Undo.md`「落とし穴: `DeleteObject(handle, true)` は…」の実測そのまま。
//
//	  梃子 2（VW にイベントループを回させる）… `ISDK::AlertQuestion` は **VW 自身の
//	    モーダル確認ダイアログ**で、VW がそこで自分のイベントループを回す。これは #206 が
//	    挙げた 3 つの状況——ツールのドラッグ中・レンダリング中・**VW が出した確認
//	    ダイアログの最中**——の 3 つめに正面から当たる。
//
//	2 つを重ねると「**VW が開いた undo イベント ＋ VW が回しているイベントループ**」が
//	できる。そこへ刻みを当てるのがこのプローブである。
//
//	■ 何を測るか
//
//	  (1) **刻みが届くか**。タイマーを 2 本仕掛け、登録するモードだけを変える:
//	      ・`kCFRunLoopDefaultMode` だけに登録した 1 本
//	      ・`kCFRunLoopCommonModes` に登録した 1 本
//	      モーダル／トラッキングの最中はランループが既定モードでは回らないので、
//	      **どちらが届くかで「刻みを当てるには何が要るか」が決まる**。刻みごとに
//	      `CFRunLoopCopyCurrentMode` を読んで、**VW がどのモードで回しているか**も出す。
//	  (2) 届いたとき、そこから `gSDK` を**読めるか**（`IsCurrentlyBuildingAnUndoEvent`
//	      と `GetNamedObject`）。**刻みの中で `building` が `yes` と読めるかどうか**は
//	      「刻みの中で安全判定ができるか」そのものなので、ここが肝。
//	  (3) そこで `gSDK` で図形を**作ると、VW が開いているイベントに混ざるか**。
//	      判定は**取り消しを 1 回だけ掛けて、何が消えたかを読む**（目視に頼らない）。
//	      取り消しの実行は VectorScript 経由（`Findings/Undo.md`「間接経路」で実測済み）。
//
//	      ・`PROBE-I206-PRE` … **どの undo イベントも開く前**に作った対照。
//	        イベントの外で作ったものは取り消しスタックに載らない（実測済み）ので、
//	        **取り消しても残るのが既知の正**。残らなければ読み方を疑う側の材料になる。
//	      ・`PROBE-I206-TICK` … **刻みの中**で作った本題。これが**消えたら「VW の
//	        undo イベントに混ざった」**＝利用者の 1 回の取り消しで持っていかれる。
//	        **残ったら混ざっていない**。
//
//	■ 利用者がすること
//
//	新規の空図面で走らせ、**VW の確認ダイアログが出たら 2〜3 秒待ってから「はい」を
//	押す**だけ。待つのは、その間に刻みを当てるため（押すのが速いと刻みが 1 回も
//	入らず、(1) が「届かなかった」と見分けられなくなる）。
//
//	■ 実機で分かったこと（1・2 回目。この版は 3 回目用）
//
//	  (1) **答えが出た。** VW のモーダルループの最中に刻みは届く
//	      （`mode=NSModalPanelRunLoopMode`）。ただし**`kCFRunLoopCommonModes` に
//	      登録した 1 本だけ**で、`kCFRunLoopDefaultMode` だけに登録した 1 本は
//	      1 回も届かない。届いた刻みからは `gSDK` を読めて書けた。
//	      1 回目 85 回・2 回目 44 回、どちらも全件 `building=yes` で読めた。
//
//	  (3) **2 回とも判定不能だった。** 理由が 2 回目で割れた——
//	      **`CreateLocus` そのものが undo イベントを開く**（走り出し `building=no`
//	      が、対照 PRE を作った直後に `yes` になった）。つまり「どのイベントの外でも
//	      ないところに作った対照」は素朴には作れない: **作る行為がイベントを開く**。
//	      その結果 PRE も TICK も同じ開いたままのイベントに入り、取り消し 1 段で
//	      両方消えて、TICK が消えた理由が割れなかった。
//
//	      → この版では**開いてしまったイベントを `EndUndoEvent` で閉じて**、PRE を
//	        「閉じた別の段」へ置く。さらに**取り消しを 2 段掛けて**、どちらの段で
//	        何が消えるかを観測する（推論ではなく段の境目を直接見る）。
//
//	  なお `CreateLocus` が開くという事実そのものが知見である——`Findings/Undo.md`
//	  の「SDK 内部が自前でイベントを開く呼び出しがある」の一覧（PIO ＋ `ResetObject`・
//	  ビューポートの生成と更新・`DeleteObject(useUndo=true)`）に、**素の 2D 図形の
//	  生成**が加わる。
//
//	■ 範囲外（ここでは測らない）——ツールのドラッグ中・レンダリング中そのもの
//
//	ドラッグの最中に刻みを当てるには、**プローブが終わった後も生き続けるタイマー**が
//	要る。本体（`.vwpayload`）は殻がプローブの終わりに `dlclose` するので、素朴に
//	仕掛けるとタイマーの行き先が消えて落ちる——が、**不可能ではない**。
//	`probes/runtime/runloop-timer-sdk/`（issue #204 / PR #207）が
//	`dlopen(RTLD_NOLOAD)` で本体をピン留めし、コマンドが戻った後も刻み続ける形を
//	すでに持っている。
//
//	**だからここで同じ仕掛けを二重に持たない。** ピン留めしたタイマーが 2 本同時に
//	走ると、どちらの刻みを見ているのか分からなくなるし、ピン留めした本体が 2 つ
//	居残る。ドラッグ中・レンダリング中の刻みは**あちらの「outside」局面が通る道**
//	なので、あちらの刻みに `IsCurrentlyBuildingAnUndoEvent()` を 1 行足すほうが筋が
//	良い（PR #207 へその旨を伝えてある）。
//
//	ここで測るのは、**同期的に・1 回の実行で閉じる形で確かめられる分**——
//	「VW が開いた undo イベント ＋ VW が回しているイベントループ」そのものと、
//	**書き込みが取り消しの記録に混ざるか**である。後者は取り消しスタックが文書ごとに
//	1 本であることから局面に依らないはずで、ここで決まればドラッグ中にも効く。
//

#include "Probe.h"

#include <string>
#include <vector>

#if !defined(_WIN32)
#	include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
#if !defined(_WIN32)
	// 図形の名前。**短い名前・ありふれた名前は使わない**（probes/runtime/README.md）。
	const char kProbeI206NamePre[] = "PROBE-I206-PRE";	 // イベントの外で作る（対照）
	const char kProbeI206NameTick[] = "PROBE-I206-TICK"; // 刻みの中で作る（本題）
	const char kProbeI206NameVictim[] = "PROBE-I206-VICTIM"; // 梃子 1 で消す駒

	// タイマー 2 本の見分け札（CFRunLoopTimerContext の info に入れる）。
	const char kProbeI206TagDefaultMode[] = "既定モードだけ";
	const char kProbeI206TagCommonModes[] = "共通モード";

	// 刻み 1 回の記録。
	struct ProbeI206Tick
	{
		int fSeq = 0;
		const char* fTimerTag = "";
		std::string fMode;	// CFRunLoopCopyCurrentMode の文字列
		std::string fPhase; // 当たった局面
		bool fBuilding = false;
		bool fReadOk = false;
		bool fWrote = false;
	};

	// 刻みのコールバックは C の関数なので、やり取りはここを通す。
	vwprobe::Report* gProbeI206Report = nullptr;
	std::vector<ProbeI206Tick> gProbeI206Ticks;
	std::string gProbeI206Phase = "(まだ始まっていない)";
	int gProbeI206Seq = 0;
	bool gProbeI206WantWrite = false; // 次に届いた刻み 1 回だけ図形を作る
	int gProbeI206Wrote = 0;

	std::string ProbeI206ModeName(CFStringRef mode)
	{
		if (mode == nullptr)
			return "(nil＝ランループを回していない)";
		char buf[256] = {0};
		if (CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8))
			return std::string(buf);
		return "(文字列にできない)";
	}

	void ProbeI206Log(const std::string& line)
	{
		if (gProbeI206Report != nullptr)
			gProbeI206Report->log(line);
	}

	std::string ProbeI206YesNo(bool v)
	{
		return v ? "yes" : "no";
	}

	// 刻み本体。**gSDK を呼ぶ前に必ずログへ書く**——落ちたらそこが最後の通過点になる
	// （probes/runtime/README.md「落ちてもよいが、落ちる前に書く」）。
	void ProbeI206Tock(CFRunLoopTimerRef /*timer*/, void* info)
	{
		ProbeI206Tick tick;
		tick.fSeq = ++gProbeI206Seq;
		tick.fTimerTag = static_cast<const char*>(info);
		CFStringRef mode = CFRunLoopCopyCurrentMode(CFRunLoopGetCurrent());
		tick.fMode = ProbeI206ModeName(mode);
		if (mode != nullptr)
			CFRelease(mode);
		tick.fPhase = gProbeI206Phase;

		const std::string head =
			"  刻み " + std::to_string(tick.fSeq) + "（" + std::string(tick.fTimerTag) + "）";
		ProbeI206Log(head + " が届いた: mode=" + tick.fMode + " / 局面=" + tick.fPhase +
					 " → これから gSDK を読む");

		tick.fBuilding = gSDK->IsCurrentlyBuildingAnUndoEvent();
		tick.fReadOk = (gSDK->GetNamedObject(kProbeI206NamePre) != nullptr);
		ProbeI206Log(head + " 読めた: building=" + ProbeI206YesNo(tick.fBuilding) +
					 " / PRE を名前で引けた=" + ProbeI206YesNo(tick.fReadOk));

		if (gProbeI206WantWrite)
		{
			gProbeI206WantWrite = false;
			ProbeI206Log(head + " **これから書く**（CreateLocus → SetObjectName）");
			MCObjectHandle hNew = gSDK->CreateLocus(WorldPt(1000.0, 1000.0));
			if (hNew != nullptr)
			{
				gSDK->SetObjectName(hNew, kProbeI206NameTick);
				tick.fWrote = true;
				++gProbeI206Wrote;
				ProbeI206Log(head + " 書けた（名前 " + kProbeI206NameTick + "）");
			}
			else
			{
				ProbeI206Log(head + " CreateLocus が nil を返した（書けなかった）");
			}
		}

		gProbeI206Ticks.push_back(tick);
	}

	// 名前で引いて在るかどうかを返す。
	bool ProbeI206Exists(const char* name)
	{
		return gSDK->GetNamedObject(name) != nullptr;
	}
#endif // !_WIN32
} // namespace

VW_PROBE("undo-event-timer-tick", "VW の undo イベント中の刻み",
		 "VW が開いた undo イベントと VW のモーダルループの最中に刻みを当てて読み書きを測る")
{
#if defined(_WIN32)
	probe.log("この調査は macOS 専用（`CFRunLoopTimer` を使う）ので、Windows では何もしない。");
	probe.log("Windows の相当（`SetTimer` ＋ メッセージループ）は別の調査に切り出す。");
#else
	gProbeI206Report = &probe;
	gProbeI206Ticks.clear();
	gProbeI206Seq = 0;
	gProbeI206Wrote = 0;
	gProbeI206WantWrite = false;
	gProbeI206Phase = "P0 前提の確認";

	probe.log("■ 前提の確認");
	const bool buildingAtStart = gSDK->IsCurrentlyBuildingAnUndoEvent();
	probe.log("走り出しの building=" + ProbeI206YesNo(buildingAtStart));
	const bool cleanStart = !buildingAtStart;
	if (!cleanStart)
	{
		probe.log("【重大】走り出しから開いている。これは前のコマンドの置き土産で起こる");
		probe.log("（Findings「Undo」: スクリプトエンジンはイベントを開いたまま返し、");
		probe.log("コマンドをまたいで残る）。");
		probe.log("**この回は (3)（混ざるか）を判定できない。** 対照 PRE は「どの undo");
		probe.log("イベントも開いていないうちに作ったもの」でなければ対照にならないが、");
		probe.log("すでに開いているので、PRE もその置き土産のイベントに入ってしまう");
		probe.log("——取り消し 1 回で PRE まで消え、TICK が消えた理由が「VW のイベントに");
		probe.log("混ざった」なのか「置き土産のイベントごと戻った」なのか割れない。");
		probe.log("**(1)（刻みが届くか・読めるか）は影響を受けない**ので、そちらは測る。");
	}

	// --- 対照 PRE を、どの undo イベントも開いていないうちに作る -----------------
	probe.log("");
	probe.log("■ 対照 PRE を作る（undo イベントの外。取り消しても残るのが既知の正）");
	MCObjectHandle hPre = gSDK->CreateLocus(WorldPt(0.0, 0.0));
	if (hPre == nullptr)
	{
		probe.fail("CreateLocus が nil を返した（対照 PRE を作れなかった）");
	}
	else
	{
		gSDK->SetObjectName(hPre, kProbeI206NamePre);
		probe.log("PRE を作った。名前で引けた=" +
				  ProbeI206YesNo(ProbeI206Exists(kProbeI206NamePre)));
	}
	// **`CreateLocus` そのものが undo イベントを開く**（2 回目の実機で判明——走り出し
	// `building=no` が、PRE を作った直後に `yes` になった）。だから「どのイベントの外
	// でもないところに作った対照」は、素朴には作れない——作る行為がイベントを開く。
	//
	// そこで**開いてしまったイベントをここで閉じる**。閉じれば PRE は「閉じた別の
	// イベント」に入るので、この後 VW が開く別のイベント（梃子 1）とは**別の段**に
	// なり、取り消し 1 段では消えない＝対照として使える。
	//
	// **これは「プローブは undo イベントを自分では開かない」決まりに反しない**——
	// 開いたのは SDK（`CreateLocus`）であって、ここでやるのは**閉じる**ことである。
	// しかも `Findings/Undo.md` はまさにこれを勧めている（「`useUndo` に任せきりに
	// せず、終わったら `EndUndoEvent` まで自分で閉じる」）。半端に開いたままにする
	// ほうが危うく、閉じるのは安全側である。
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
	{
		probe.log("PRE を作ったらイベントが開いた（CreateLocus が自分で開く）。**閉じる**"
				  "——閉じれば PRE は別の段になり、対照として使える。");
		const Boolean ended = gSDK->EndUndoEvent();
		probe.log(
			std::string("EndUndoEvent の戻り値=") + (ended ? "true" : "false") +
			" / 閉じた後の building=" + ProbeI206YesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()));
	}
	else
	{
		probe.log("PRE を作ってもイベントは開かなかった（この場合 PRE はどのイベントにも"
				  "属さないので、そのまま対照になる）。");
	}

	// --- タイマーを 2 本仕掛ける -------------------------------------------------
	probe.log("");
	probe.log("■ タイマーを 2 本仕掛ける（0.15 秒ごと。登録するモードだけが違う）");
	CFRunLoopTimerContext ctxDefault = {0, const_cast<char*>(kProbeI206TagDefaultMode), nullptr,
										nullptr, nullptr};
	CFRunLoopTimerContext ctxCommon = {0, const_cast<char*>(kProbeI206TagCommonModes), nullptr,
									   nullptr, nullptr};
	CFRunLoopTimerRef timerDefault =
		CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.1, 0.15, 0, 0,
							 ProbeI206Tock, &ctxDefault);
	CFRunLoopTimerRef timerCommon =
		CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.1, 0.15, 0, 0,
							 ProbeI206Tock, &ctxCommon);
	if (timerDefault == nullptr || timerCommon == nullptr)
	{
		probe.fail("CFRunLoopTimerCreate が nil を返した（タイマーを作れなかった）");
	}
	else
	{
		// **メインのランループへ入れる**（`CFRunLoopGetCurrent()` ではなく）。メニュー
		// コマンドが別スレッドで走っていた場合、current だと誰も回さないループへ
		// 入ってしまう。同じかどうかも記録しておく。
		CFRunLoopRef mainLoop = CFRunLoopGetMain();
		probe.log(std::string("current == main か=") +
				  ProbeI206YesNo(CFRunLoopGetCurrent() == mainLoop));
		CFRunLoopAddTimer(mainLoop, timerDefault, kCFRunLoopDefaultMode);
		CFRunLoopAddTimer(mainLoop, timerCommon, kCFRunLoopCommonModes);
		probe.log("1 本は kCFRunLoopDefaultMode だけに、もう 1 本は kCFRunLoopCommonModes に");
		probe.log("登録した。**どちらが届くかが (1) の答えになる。**");
		probe.log(
			std::string("登録できたか: 既定モードだけ=") +
			ProbeI206YesNo(CFRunLoopContainsTimer(mainLoop, timerDefault, kCFRunLoopDefaultMode)) +
			" / 共通モードの 1 本が既定モードにも居るか=" +
			ProbeI206YesNo(CFRunLoopContainsTimer(mainLoop, timerCommon, kCFRunLoopDefaultMode)));
	}

	// --- P0 対照: こちらでランループを回す --------------------------------------
	probe.log("");
	probe.log("■ P0 対照: こちらでランループを 0.6 秒ぶん回す（#204 の 1 と同じ立場）");
	gProbeI206Phase = "P0 自分でランループを回した";
	const int seqBeforeP0 = gProbeI206Seq;
	// **1 回の長い `CFRunLoopRunInMode` にしない。** 即座に返る場合（モードに用が無い・
	// 誰かが `CFRunLoopStop` した）があり、1 回だと「0 回だった」しか残らない。
	// 12 回に刻んで、**戻り値と実際に費やした時間**を出す。
	const CFAbsoluteTime p0Start = CFAbsoluteTimeGetCurrent();
	SInt32 lastResult = 0;
	for (int i = 0; i < 12; ++i)
		lastResult = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
	const double p0Spent = CFAbsoluteTimeGetCurrent() - p0Start;
	probe.log("P0 で届いた刻み=" + std::to_string(gProbeI206Seq - seqBeforeP0) + " 回");
	probe.log("  CFRunLoopRunInMode の最後の戻り値=" + std::to_string((int)lastResult) +
			  "（1=Finished 2=Stopped 3=TimedOut 4=HandledSource）" +
			  " / 実際に費やした時間=" + std::to_string(p0Spent) + " 秒（0.6 が期待値）");
	if (gProbeI206Seq == seqBeforeP0)
	{
		// **ここで `probe.fail` を呼ばない。** P0 はあくまで対照であって、本題は P2 で
		// ある。P2 で刻みが届けば「タイマーは動いている」ことは P2 自身が示すので、
		// P0 の 0 回は**こちらの回し方の問題**として切り分けられる。最後にまとめて
		// 判定する（P2 も 0 回だったときだけ失敗にする）。
		probe.log("  **P0 では 1 回も届かなかった。** ただしこれは失敗ではない——");
		probe.log("  本題は P2（VW がイベントループを回している最中）であり、そこで");
		probe.log("  届けばタイマーが動いていることは P2 自身が示す。上の戻り値と");
		probe.log("  費やした時間を見れば、回し方の問題かどうかが分かる。");
	}

	// --- P1 VW に undo イベントを開かせる（プローブは開かない）------------------
	probe.log("");
	probe.log("■ P1 VW に undo イベントを開かせる（梃子 1）");
	probe.log("DeleteObject(h, useUndo=true) は、開いていなければ自分で開き、プローブが");
	probe.log("return するまで開いたままになる（Findings「Undo」で実測済み）。");
	gProbeI206Phase = "P1 VW が undo イベントを開いた後";
	MCObjectHandle hVictim = gSDK->CreateLocus(WorldPt(2000.0, 0.0));
	if (hVictim == nullptr)
	{
		probe.fail("CreateLocus が nil を返した（梃子 1 の駒を作れなかった）");
	}
	else
	{
		gSDK->SetObjectName(hVictim, kProbeI206NameVictim);
		probe.log("消す前の building=" + ProbeI206YesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()));
		gSDK->DeleteObject(hVictim, true);
	}
	const bool buildingAfterDelete = gSDK->IsCurrentlyBuildingAnUndoEvent();
	probe.log("消した後の building=" + ProbeI206YesNo(buildingAfterDelete) + " ← **ここが yes で");
	probe.log("なければ、本題（P2）の前提『VW が開いた undo イベントの最中』が成立していない**");
	if (!buildingAfterDelete)
	{
		probe.fail("DeleteObject(useUndo=true) の後も undo イベントが開かなかった"
				   "——P2 の結果は『イベント中』の測定として読めない");
	}

	// --- P2 本題: VW のモーダル確認ダイアログの最中 -----------------------------
	probe.log("");
	probe.log("■ P2 本題: VW 自身のモーダル確認ダイアログを、そのイベントの最中に出す（梃子 2）");
	probe.log("**ダイアログが出たら 2〜3 秒待ってから「はい」を押してください。**");
	probe.log("待つのはその間に刻みを当てるためで、速く押すと刻みが 1 回も入りません。");
	gProbeI206Phase = "P2 VW のモーダル確認ダイアログの最中";
	gProbeI206WantWrite = true; // この局面で届いた最初の刻みが 1 つだけ図形を作る
	probe.log("AreAlertsAllowed=" + ProbeI206YesNo(gSDK->AreAlertsAllowed()) +
			  "（no ならダイアログは出ない＝この局面は作れていない）");
	const int seqBeforeP2 = gProbeI206Seq;
	const CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
	const short answer = gSDK->AlertQuestion(
		"調査 #206: このまま 2〜3 秒待ってから「はい」を押してください。",
		"その間、ランループタイマーの刻みが VW のイベントループへ届くかを測っています。", 1);
	const double heldSec = CFAbsoluteTimeGetCurrent() - t0;
	probe.log("ダイアログの答え=" + std::to_string(static_cast<int>(answer)));
	probe.log("ダイアログが開いていた時間=" + std::to_string(heldSec) + " 秒");
	probe.log("**P2 で届いた刻み=" + std::to_string(gProbeI206Seq - seqBeforeP2) + " 回**");
	gProbeI206WantWrite = false;

	// 「刻みが届かなかった」と「ダイアログがそもそも出ていない／すぐ閉じた」を
	// 混同させない。開いていた時間が短ければ、刻みの回数は読めない。
	if (heldSec < 0.5)
	{
		probe.fail("ダイアログが開いていたのは " + std::to_string(heldSec) +
				   " 秒しかない——**P2 の刻みの回数は『届かない』の根拠にならない**" +
				   "（抑止されて出なかったか、すぐ押されたか）。もう一度走らせ、" +
				   "出たら 2〜3 秒待ってから押すこと");
	}
	else if (gProbeI206Seq == seqBeforeP2)
	{
		probe.log("**ダイアログは " + std::to_string(heldSec) +
				  " 秒開いていたのに、刻みが 1 回も届かなかった**" +
				  "——これは『届かない』の根拠として読める。");
	}

	// --- P3 ダイアログを閉じた後の状態 ------------------------------------------
	probe.log("");
	probe.log("■ P3 ダイアログを閉じた後");
	gProbeI206Phase = "P3 ダイアログを閉じた後";
	probe.log("building=" + ProbeI206YesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()));
	probe.log("刻みの中で作った図形（TICK）は在るか=" +
			  ProbeI206YesNo(ProbeI206Exists(kProbeI206NameTick)));
	probe.log("対照 PRE は在るか=" + ProbeI206YesNo(ProbeI206Exists(kProbeI206NamePre)));
	probe.log("梃子 1 で消した駒（VICTIM）は在るか=" +
			  ProbeI206YesNo(ProbeI206Exists(kProbeI206NameVictim)) + "（消えているはず）");

	// --- タイマーを止める（取り消しの最中に刻みを当てない）----------------------
	if (timerDefault != nullptr)
	{
		CFRunLoopTimerInvalidate(timerDefault);
		CFRelease(timerDefault);
	}
	if (timerCommon != nullptr)
	{
		CFRunLoopTimerInvalidate(timerCommon);
		CFRelease(timerCommon);
	}
	probe.log("");
	probe.log("タイマーを 2 本とも止めた（この先の取り消しに刻みを混ぜないため）。");

	// --- P4 取り消しを 1 回だけ掛けて、何が消えたかを読む ------------------------
	probe.log("");
	probe.log("■ P4 取り消しを 1 回だけ掛けて、何が消えたかを読む（(3) の答え）");
	gProbeI206Phase = "P4 取り消しの後";
	const bool tickBeforeUndo = ProbeI206Exists(kProbeI206NameTick);
	const bool preBeforeUndo = ProbeI206Exists(kProbeI206NamePre);
	probe.log("取り消す前: TICK=" + ProbeI206YesNo(tickBeforeUndo) +
			  " / PRE=" + ProbeI206YesNo(preBeforeUndo));

	VCOMPtr<VectorWorks::Scripting::IVectorScriptEngine> engine(
		VectorWorks::Scripting::IID_VectorScriptEngine);
	if (!engine)
	{
		probe.fail("IVectorScriptEngine を取れなかった（取り消しを掛けられないので (3) は未測）");
	}
	else
	{
		probe.log("VectorScript 経由で DoMenuTextByName('Undo', 0) を 1 回だけ呼ぶ");
		const VCOMError err = engine->ExecuteScript("DoMenuTextByName('Undo', 0);");
		probe.log("ExecuteScript の VCOMError=" + std::to_string(static_cast<int>(err)) +
				  "（0 は『実行時エラーが起きなかった』を意味しない。Findings「Undo」）");

		const bool tickAfterUndo = ProbeI206Exists(kProbeI206NameTick);
		const bool preAfterUndo = ProbeI206Exists(kProbeI206NamePre);
		probe.log("**1 段目の取り消しの後**: TICK=" + ProbeI206YesNo(tickAfterUndo) +
				  " / PRE=" + ProbeI206YesNo(preAfterUndo));
		probe.log("  （1 段目の後の building=" +
				  ProbeI206YesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()) + "）");

		// **2 段目も掛ける。** 1 段で何が消え、2 段目で何が消えるかが分かれば、
		// 「同じ段に居たのか・別の段だったのか」が推論ではなく観測で決まる。
		probe.log("もう 1 段だけ取り消す（段の境目を直接見るため）");
		engine->ExecuteScript("DoMenuTextByName('Undo', 0);");
		const bool tickAfter2 = ProbeI206Exists(kProbeI206NameTick);
		const bool preAfter2 = ProbeI206Exists(kProbeI206NamePre);
		probe.log("**2 段目の取り消しの後**: TICK=" + ProbeI206YesNo(tickAfter2) +
				  " / PRE=" + ProbeI206YesNo(preAfter2));
		probe.log("");
		probe.log("── (3) の読み方——**対照を先に見る** ──");
		// **順序が肝。** 対照（PRE）が成立していないのに TICK だけを読むと、
		// 「消えた＝混ざった」という結論が独り歩きする（前の回で実際にそうなった）。
		if (!cleanStart)
		{
			probe.fail("(3) は判定不能——走り出しから undo イベントが開いていたので、対照 PRE が"
					   "対照になっていない。**VectorWorks を起動し直し、新規の空図面で**"
					   "走らせ直すこと（(1) の結果はこの回でも有効）");
			probe.log("参考値（判定には使わない）: TICK は取り消し後 " +
					  ProbeI206YesNo(tickAfterUndo) + " / PRE は " + ProbeI206YesNo(preAfterUndo));
		}
		else if (!tickBeforeUndo)
		{
			probe.fail("(3) は未測——刻みの中で作った TICK がそもそも無い（刻みが P2 に"
					   "届かなかったか、CreateLocus が失敗した）。P2 の刻み数を見ること");
		}
		else if (preBeforeUndo && !preAfterUndo)
		{
			probe.fail("(3) は判定不能——**1 段目で対照 PRE まで消えた。** PRE は閉じた別の段に"
					   "入れたので、1 段目では残るはずだった。両方が同じ段に居たということ"
					   "なので、TICK が消えたことを『VW が開いていたイベントに混ざった』の"
					   "証拠にできない。2 段目の行と EndUndoEvent の戻り値を見ること");
		}
		else if (!tickAfterUndo)
		{
			probe.log("**1 段目で、対照 PRE は残り TICK だけが消えた。**");
			probe.log("→ 刻みの中での書き込みは、**そのとき開いていた undo イベントに");
			probe.log("  混ざる**。利用者の 1 回の取り消しが、刻みが作ったものまで");
			probe.log("  持っていく（しかも刻みと無関係な作業と同じ段で消える）。");
			probe.log("  PRE が 2 段目で消えていれば、段が分かれていたことの裏も取れている。");
		}
		else
		{
			probe.log("**1 段目で TICK も PRE も残った。**");
			probe.log("→ 刻みの中での書き込みは、そのとき開いていたイベントには");
			probe.log("  **混ざらない**（1 段の取り消しでは消えない）。2 段目の行も見ること。");
		}
	}

	// --- 刻みの一覧と畳んだ表 ---------------------------------------------------
	probe.log("");
	probe.log("■ 届いた刻みの全件（局面 / 札 / mode / building / 読めた / 書いた）");
	if (gProbeI206Ticks.empty())
	{
		probe.log("(1 件も無い)");
	}
	for (size_t i = 0; i < gProbeI206Ticks.size(); ++i)
	{
		const ProbeI206Tick& t = gProbeI206Ticks[i];
		probe.log(std::to_string(t.fSeq) + " | " + t.fPhase + " | " + t.fTimerTag + " | " +
				  t.fMode + " | building=" + ProbeI206YesNo(t.fBuilding) + " | 読めた=" +
				  ProbeI206YesNo(t.fReadOk) + " | 書いた=" + ProbeI206YesNo(t.fWrote));
	}

	probe.log("");
	probe.log("■ 畳んだ表（局面 × 札 → 届いた回数。(1) の答え）");
	const char* phases[] = {"P0 自分でランループを回した", "P1 VW が undo イベントを開いた後",
							"P2 VW のモーダル確認ダイアログの最中", "P3 ダイアログを閉じた後",
							"P4 取り消しの後"};
	const char* tags[] = {kProbeI206TagDefaultMode, kProbeI206TagCommonModes};
	for (size_t p = 0; p < sizeof(phases) / sizeof(phases[0]); ++p)
	{
		for (size_t g = 0; g < 2; ++g)
		{
			int n = 0;
			int buildingYes = 0;
			int readOk = 0;
			for (size_t i = 0; i < gProbeI206Ticks.size(); ++i)
			{
				if (gProbeI206Ticks[i].fPhase == phases[p] &&
					std::string(gProbeI206Ticks[i].fTimerTag) == tags[g])
				{
					++n;
					if (gProbeI206Ticks[i].fBuilding)
						++buildingYes;
					if (gProbeI206Ticks[i].fReadOk)
						++readOk;
				}
			}
			probe.log(std::string(phases[p]) + " | " + tags[g] + " | 届いた=" + std::to_string(n) +
					  " | うち building=yes " + std::to_string(buildingYes) + " | うち読めた " +
					  std::to_string(readOk));
		}
	}

	probe.log("");
	probe.log("刻みの中で書いた回数=" + std::to_string(gProbeI206Wrote));
	// **(1) の最終判定。** P0（自分で回した）が 0 回でも、P2（VW が回している最中）で
	// 届いていれば本題は測れている。両方 0 のときだけ「届かない」として失敗にする。
	{
		int p2Ticks = 0;
		for (size_t i = 0; i < gProbeI206Ticks.size(); ++i)
			if (gProbeI206Ticks[i].fPhase == "P2 VW のモーダル確認ダイアログの最中")
				++p2Ticks;
		if (p2Ticks == 0)
			probe.fail("(1) は未測——本題の P2（VW が開いた undo イベント ＋ VW のモーダル"
					   "ループ）で刻みが 1 回も届かなかった。ダイアログが開いていた時間と"
					   "登録できたかの行を見ること");
	}
	probe.log("**この局面で undo イベントは開いたままのはず**——プローブは 1 度も");
	probe.log("SetUndoMethod / NameUndoEvent / EndUndoEvent を呼んでいない（梃子 1 で VW が");
	probe.log("開いたものを、そのまま VW に任せて return する）。");

	gProbeI206Report = nullptr;
#endif // _WIN32
}
