//
//	probes/runtime/render-sync-3d/probe.cpp
//
//	[issue #215] **SDK から「その場で描かせる」ことができるか**を、3D のビューに
//	切り替えてから測る。
//
//	#213（PR #214）では `SetRenderMode(…, immediate=true, doProgress=true)` が **0 ms で
//	戻った**が、測った図面は**上面/平面（2D）ビューのままだった**——「3D だから時間が
//	掛かる」はずの前提が立っていなかった。ここはその切り分けだけを行う。
//
//	ヘッダの記述（`ci-debug` の `sdk-grep` で確認。`APIBase.Legacy.Defs.h`）:
//
//	  * `GS_SetRenderMode`: "This sets the render mode of the specified layer and takes
//	    care of regenerating the rendering. **If immediate is true, then all rendering will
//	    take place before the call returns.** Otherwise, any rendering that can take place
//	    in the background will be postponed until program execution re-enters the main
//	    event loop. doProgress controls whether progress information is displayed…"
//	  * `GS_UpdateViewport`: "Updates the specified viewport: **a dirty viewport, whose
//	    render type is other than wireframe or sketch, will be re-rendered.**"
//	    ——#213 の `UpdateViewport` が 131 ms で戻ったのは、**作りたてのビューポートに
//	    映すものが無かった**か、**dirty でなかった**ためかもしれない。ここでは
//	    **表示レイヤを入れて `SetDirty(true)` を立ててから**測る。
//	  * `GS_SetProjection`: "…If confirmWithUser is true, then the user will be asked if
//	    they want to continue **if a long re-render is invoked as a result of the
//	    projection change**…" ——投影の切り替え自体が描画を起こしうるので、これも測る。
//
//	## 測り方（すべて機械判定。目視に頼らない）
//
//	  S1 **3D のモデルを置く。** 球を格子状に置く。大きさと位置は
//	     `GetViewCenter` ＋ `ViewPt2WorldPt(ViewPt(0,0))` から**いま画面に見えている
//	     世界座標の矩形**を割り出して決める（取れなければ既定の寸法に落ちる）。
//	     ——「画面の外にあるから描くものが無かった」をログで排除するため。
//	  S2 **3D のビューへ切り替える。** `SetProjection(layer, projectionOrthogonal, …)`
//	     ＋ VectorScript の `SetView`。**`GetProjection(layer) != projectionPlan`（6）が
//	     「上面/平面を出た」の機械判定**で、これが取れなければ以後は何も言えない
//	     （`probe.fail`）。
//	  S3 **3D で `SetRenderMode(…, immediate=true, doProgress=true)` の所要時間を測る**
//	     （`renderOpenGL` / `renderFinalShaded` / `renderFinalHiddenLine` /
//	     `renderFinalRenderWorks`。毎回ワイヤフレームへ戻してから測る）。
//	  S4 **同じ図面・同じモデルで、上面/平面（2D）に戻して同じ測定をする**（対照）。
//	     ——#213 の 0 ms が「2D だったから」なのかは、**この 2 枚を並べて初めて**言える。
//	  S5 **物差し: ビューポートに中身を入れて `UpdateViewport` を測る。**
//	     同じモデル・同じ描画モードで**何 ms 掛かるか**が分かれば、S3 の 0 ms は
//	     「速かった」ではなく「描いていない」と確定できる。
//	  S6 **刻み（250 ms のランループタイマー）は上の測定の最中に届くか。**
//	     タイマーは**このプローブの中だけ**で生き、戻る前に必ず外す（本体は殻が
//	     降ろすので、残すと居ない関数が呼ばれる）。所要が短い測定では刻みの回数は
//	     根拠にならないので、そのことをログに明示する。
//
//	## 判定
//
//	  * S3 のどれかが閾値以上 → **3D なら `immediate=true` はその場で描く。**
//	  * S3 が全部 0 ms 級で、S5（同じモデル・同じモード）が閾値以上
//	    → **3D でも `immediate=true` は同期では描かない**（描けば時間が掛かることを
//	      S5 が示しているので、0 ms は「描いていない」の意味しかない）。
//	  * S3 も S5 も短い → **モデルが軽すぎて判定できない**（`probe.fail`）。
//

#include "Probe.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#	include <windows.h>
#else
#	include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
	// 刻みの間隔。#204 / #206 と同じ 250 ms にして、あちらの数値と並べて読めるように
	// する。
	const long long kRenderSyncTickMs = 250;

	// 「その場で描いた」と見なす所要時間の下限。#213 の実測（`SetRenderMode`＝0 ms /
	// `ReDrawAll`＝20〜22 ms / 中身の無いビューポートの更新＝131 ms）より十分上に置く。
	const long long kRenderSyncDrewMs = 400;

	// 名前は用途が分かる長さにする（probes/runtime/README.md「短い名前・ありふれた
	// 名前を使わない」）。
	const char kRenderSyncSheetName[] = "PROBE-I215-SHEET";

	// -----------------------------------------------------------------------
	// 刻みの計数。**タイマーはこのプローブの中だけで生きる**——戻る前に必ず外す
	// （殻はプローブが終わると本体を降ろすので、残すと居ない関数が呼ばれる）。
	struct RenderSyncTickState
	{
		int ticks = 0;
		std::string modes; // 届いた刻みのランループモード（重複は畳む）
	};

	RenderSyncTickState gRenderSyncTicks;

	void RenderSyncNoteTick()
	{
		++gRenderSyncTicks.ticks;
#if !defined(_WIN32)
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode != nullptr)
		{
			char buf[128] = {0};
			if (::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8))
			{
				const std::string one(buf);
				if (gRenderSyncTicks.modes.find(one) == std::string::npos)
				{
					if (!gRenderSyncTicks.modes.empty())
						gRenderSyncTicks.modes += ", ";
					gRenderSyncTicks.modes += one;
				}
			}
			::CFRelease(mode);
		}
#else
		if (gRenderSyncTicks.modes.empty())
			gRenderSyncTicks.modes = "(Windows にモードの概念は無い)";
#endif
	}

#if defined(_WIN32)
	UINT_PTR gRenderSyncWinTimer = 0;

	void CALLBACK RenderSyncWinTimerProc(HWND, UINT, UINT_PTR, DWORD)
	{
		RenderSyncNoteTick();
	}
#else
	CFRunLoopTimerRef gRenderSyncCFTimer = nullptr;

	void RenderSyncCFTimerProc(CFRunLoopTimerRef, void*)
	{
		RenderSyncNoteTick();
	}
#endif

	// 仕掛けと店じまいを対にする。**例外で抜けても外れる**ようにデストラクタへ入れる。
	class RenderSyncTimerGuard
	{
	public:
		RenderSyncTimerGuard() = default;
		~RenderSyncTimerGuard()
		{
			Disarm();
		}

		RenderSyncTimerGuard(const RenderSyncTimerGuard&) = delete;
		RenderSyncTimerGuard& operator=(const RenderSyncTimerGuard&) = delete;

		// 仕掛けられたら true。失敗しても調査は続ける（刻みの話だけが落ちる）。
		bool Arm()
		{
#if defined(_WIN32)
			gRenderSyncWinTimer =
				::SetTimer(nullptr, 0, (UINT)kRenderSyncTickMs, &RenderSyncWinTimerProc);
			return gRenderSyncWinTimer != 0;
#else
			const CFTimeInterval interval = (CFTimeInterval)kRenderSyncTickMs / 1000.0;
			gRenderSyncCFTimer =
				::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + interval,
									   interval, 0, 0, &RenderSyncCFTimerProc, nullptr);
			if (gRenderSyncCFTimer == nullptr)
				return false;
			CFRunLoopRef mainLoop = ::CFRunLoopGetMain();
			// #206 の実測どおり、**VW が回すループに当てたいなら共通モードが要る**。
			// 念のため主ランループが知っている全モードにも足す（VW が私物のモードで
			// 回していても刻みが届くように）。
			::CFRunLoopAddTimer(mainLoop, gRenderSyncCFTimer, kCFRunLoopCommonModes);
			CFArrayRef all = ::CFRunLoopCopyAllModes(mainLoop);
			if (all != nullptr)
			{
				const CFIndex n = ::CFArrayGetCount(all);
				for (CFIndex i = 0; i < n; ++i)
				{
					CFStringRef mode = (CFStringRef)::CFArrayGetValueAtIndex(all, i);
					if (mode != nullptr)
						::CFRunLoopAddTimer(mainLoop, gRenderSyncCFTimer, mode);
				}
				::CFRelease(all);
			}
			return true;
#endif
		}

		void Disarm()
		{
#if defined(_WIN32)
			if (gRenderSyncWinTimer != 0)
			{
				::KillTimer(nullptr, gRenderSyncWinTimer);
				gRenderSyncWinTimer = 0;
			}
#else
			if (gRenderSyncCFTimer != nullptr)
			{
				::CFRunLoopTimerInvalidate(gRenderSyncCFTimer);
				::CFRelease(gRenderSyncCFTimer);
				gRenderSyncCFTimer = nullptr;
			}
#endif
		}
	};

	// -----------------------------------------------------------------------
	std::string RenderSyncYesNo(bool v)
	{
		return v ? "yes" : "no";
	}

	std::string RenderSyncNum(double v)
	{
		char buf[64] = {0};
		std::snprintf(buf, sizeof(buf), "%.1f", v);
		return std::string(buf);
	}

	// 投影の番号を読める形に（`MiniCadCallBacks.h` の `TProjection`）。
	std::string RenderSyncProjectionName(int proj)
	{
		switch (proj)
		{
		case projectionOrthogonal:
			return "orthogonal(0)=直交投影（3D）";
		case projectionPerspective:
			return "perspective(1)=透視投影（3D）";
		case projectionPlan:
			return "plan(6)=**上面/平面（2D）**";
		default:
			return std::string("その他(") + std::to_string(proj) + ")";
		}
	}

	// 描画モードの番号を読める形に（`MiniCadCallBacks.h` の `TRenderMode`）。
	std::string RenderSyncRenderName(int mode)
	{
		switch (mode)
		{
		case renderWireFrame:
			return "wireFrame(0)";
		case renderFinalShaded:
			return "finalShaded(5)";
		case renderFinalHiddenLine:
			return "finalHiddenLine(6)";
		case renderOpenGL:
			return "openGL(11)";
		case renderFinalRenderWorks:
			return "finalRenderWorks(14)";
		default:
			return std::string("mode(") + std::to_string(mode) + ")";
		}
	}

	// 1 件の測定の記録。
	struct RenderSyncShot
	{
		std::string where; // "3D" / "2D"
		int mode = 0;	   // 頼んだ描画モード
		long long ms = 0;  // `SetRenderMode` に掛かった時間
		bool ret = false; // 戻り値（#213 で「false は失敗ではない」と判っている）
		int modeAfter = 0; // `GetRenderMode` の読み戻し
		int projAfter = 0; // `GetProjection` の読み戻し（勝手に 3D へ移ることがある）
		int ticks = 0;	   // その最中に届いた刻み
	};
} // namespace

VW_PROBE("render-sync-3d", "3D ビューで同期レンダリングできるか",
		 "3D へ切り替えて SetRenderMode(immediate=true) の所要時間を測る")
{
	using Clock = std::chrono::steady_clock;
	const auto elapsedMsSince = [](Clock::time_point from) -> long long
	{ return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - from).count(); };

	MCObjectHandle layer = gSDK->GetActiveLayer();
	if (layer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した（図面が開いていない？）");
		return;
	}

	TXString docName;
	gSDK->GetObjectName(layer, docName);
	probe.log(std::string("アクティブレイヤ=") + static_cast<const char*>(docName));
	probe.log(std::string("走り出しの投影=") +
			  RenderSyncProjectionName(gSDK->GetProjection(layer)) +
			  " / 描画モード=" + RenderSyncRenderName(gSDK->GetRenderMode(layer)));
	probe.log(std::string("走り出しの building=") +
			  RenderSyncYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent() != false));

	// -----------------------------------------------------------------------
	// S1 3D のモデルを置く——**いま画面に見えている範囲に収まる大きさ**で
	// -----------------------------------------------------------------------
	// 「画面の外にあったから描くものが無かった」をログで排除する。見えている世界座標の
	// 矩形は `GetViewCenter`（中心）と `ViewPt2WorldPt(ViewPt(0,0))`（画面左上）から
	// 割り出せる。取れなければ既定の寸法に落ちる（そのこともログに出す）。
	probe.log("");
	probe.log("■ S1 3D のモデル（球の格子）を置く");
	WorldPt viewCenter;
	gSDK->GetViewCenter(viewCenter);
	WorldPt topLeft;
	gSDK->ViewPt2WorldPt(ViewPt(0, 0), topLeft);
	double halfW = std::fabs((double)viewCenter.x - (double)topLeft.x);
	double halfH = std::fabs((double)viewCenter.y - (double)topLeft.y);
	probe.log(std::string("  画面中心=(") + RenderSyncNum((double)viewCenter.x) + ", " +
			  RenderSyncNum((double)viewCenter.y) + ") / 画面左上=(" +
			  RenderSyncNum((double)topLeft.x) + ", " + RenderSyncNum((double)topLeft.y) + ")");
	bool fittedToScreen = true;
	if (!(halfW > 1.0) || !(halfH > 1.0))
	{
		// 取れなかった（0 や NaN）。**既定の寸法に落ちる**——1 辺 3000mm の範囲に置く。
		fittedToScreen = false;
		halfW = 1500.0;
		halfH = 1500.0;
		probe.log("  **画面の範囲が取れなかったので既定（±1500mm）に落ちた**"
				  "——画面に映っているかどうかはこの回では言えない");
	}
	else
	{
		probe.log(std::string("  見えている範囲=横 ") + RenderSyncNum(halfW * 2.0) + "mm × 縦 " +
				  RenderSyncNum(halfH * 2.0) + "mm（この 60% に収める）");
	}
	// 格子は 6 × 6 ＝ 36 個。重すぎると実機の待ちが長くなるので、まずこの数で測る。
	const int kRenderSyncGrid = 6;
	const double spanX = halfW * 1.2; // 中心から ±60%
	const double spanY = halfH * 1.2;
	const double stepX = spanX / (double)kRenderSyncGrid;
	const double stepY = spanY / (double)kRenderSyncGrid;
	const double radius = (stepX < stepY ? stepX : stepY) * 0.45;
	int placed = 0;
	for (int ix = 0; ix < kRenderSyncGrid; ++ix)
	{
		for (int iy = 0; iy < kRenderSyncGrid; ++iy)
		{
			const double x = (double)viewCenter.x - spanX * 0.5 + stepX * ((double)ix + 0.5);
			const double y = (double)viewCenter.y - spanY * 0.5 + stepY * ((double)iy + 0.5);
			const double z = radius * (1.0 + 0.5 * (double)((ix + iy) % 3));
			if (gSDK->CreateSphere(WorldPt3((WorldCoord)x, (WorldCoord)y, (WorldCoord)z),
								   (WorldCoord)radius) != nil)
				++placed;
		}
	}
	probe.log(std::string("  球を ") + std::to_string(placed) + " 個置いた（半径 " +
			  RenderSyncNum(radius) + "mm。Z は 3 段に散らしてある）");
	if (placed == 0)
	{
		probe.fail("CreateSphere が 1 つも作れなかった——描くものが無いので以後は測れない");
		return;
	}
	// 置いたことで VW が undo イベントを開いているかもしれない。閉じてから測る
	// （開いたままの測定は #206 の話と混ざる）。
	int closed = 0;
	while (gSDK->IsCurrentlyBuildingAnUndoEvent() && closed < 4)
	{
		gSDK->EndUndoEvent();
		++closed;
	}
	probe.log(std::string("  EndUndoEvent を ") + std::to_string(closed) +
			  " 回呼んだ / いまの building=" +
			  RenderSyncYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent() != false));

	// -----------------------------------------------------------------------
	// S6（仕掛けだけ先に） 刻みのタイマーを仕掛ける
	// -----------------------------------------------------------------------
	RenderSyncTimerGuard timer;
	const bool armed = timer.Arm();
	probe.log("");
	probe.log(std::string("■ 刻み（250 ms）を仕掛けた=") + RenderSyncYesNo(armed) +
			  "（**このプローブが戻る前に必ず外す**）");

	// -----------------------------------------------------------------------
	// S2 3D のビューへ切り替える
	// -----------------------------------------------------------------------
	probe.log("");
	probe.log("■ S2 3D のビューへ切り替える（#213 がここを踏んでいなかった）");
	// 経路 1: ISDK の `SetProjection`。**これ自体が描画を起こしうる**ので時間も測る
	// （ヘッダ: "a long re-render … as a result of the projection change"）。
	// `confirmWithUser=false` にする——true だと利用者に尋ねるダイアログが出る。
	int ticksBefore = gRenderSyncTicks.ticks;
	Clock::time_point from = Clock::now();
	gSDK->SetProjection(layer, projectionOrthogonal, false, true);
	const long long projMs = elapsedMsSince(from);
	const int projTicks = gRenderSyncTicks.ticks - ticksBefore;
	probe.log(std::string("  経路 1 `SetProjection(layer, projectionOrthogonal, false, true)` = ") +
			  std::to_string(projMs) + " ms / 刻み " + std::to_string(projTicks) +
			  " 回 / 読み戻し=" + RenderSyncProjectionName(gSDK->GetProjection(layer)));

	// 経路 2: VectorScript の `SetView` で視点を斜めへ振る（真上から見た直交投影でも
	// 3D ではあるが、利用者が「3D のビュー」と言うときの絵に寄せる）。
	VCOMPtr<VectorWorks::Scripting::IVectorScriptEngine> engine(
		VectorWorks::Scripting::IID_VectorScriptEngine);
	if (engine == nil)
	{
		probe.log("  （IVectorScriptEngine を取れなかったので経路 2 は試せない）");
	}
	else
	{
		ticksBefore = gRenderSyncTicks.ticks;
		from = Clock::now();
		const VCOMError err = engine->ExecuteScript("SetView(-60, 0, 30, 0, 0, 0);");
		const long long viewMs = elapsedMsSince(from);
		probe.log(std::string("  経路 2 VectorScript `SetView(-60,0,30,0,0,0)` = ") +
				  std::to_string(viewMs) + " ms / 刻み " +
				  std::to_string(gRenderSyncTicks.ticks - ticksBefore) +
				  " 回 / VCOMError=" + std::to_string((int)err) +
				  " / 読み戻し=" + RenderSyncProjectionName(gSDK->GetProjection(layer)));
	}

	const int projNow = gSDK->GetProjection(layer);
	const bool in3D = (projNow != projectionPlan);
	probe.log(std::string("  → **いま上面/平面（2D）を出ているか=") + RenderSyncYesNo(in3D) +
			  "**（投影=" + RenderSyncProjectionName(projNow) + "）");
	if (!in3D)
	{
		probe.fail("3D のビューへ切り替えられなかった（投影が plan(6) のまま）"
				   "——この回は issue #215 の問いに何も答えていない");
		// 切り替わっていなくても測定は続ける（2D での再測として読める）。
	}

	// -----------------------------------------------------------------------
	// S3 / S4 `SetRenderMode(…, immediate=true, doProgress=true)` の所要時間
	// -----------------------------------------------------------------------
	const TRenderMode kRenderSyncModes[] = {renderOpenGL, renderFinalShaded, renderFinalHiddenLine,
											renderFinalRenderWorks};
	std::vector<RenderSyncShot> shots;

	const auto measureOne = [&](const std::string& where, TRenderMode mode)
	{
		// 毎回ワイヤフレームへ戻してから測る（同じモードを続けて書くと「変化が無い」
		// ぶんの短絡が入りうる）。戻す側は進捗を出さない。
		gSDK->SetRenderMode(layer, renderWireFrame, true, false);
		RenderSyncShot shot;
		shot.where = where;
		shot.mode = mode;
		const int before = gRenderSyncTicks.ticks;
		const Clock::time_point t0 = Clock::now();
		shot.ret = (gSDK->SetRenderMode(layer, mode, true, true) != false);
		shot.ms = elapsedMsSince(t0);
		shot.ticks = gRenderSyncTicks.ticks - before;
		shot.modeAfter = gSDK->GetRenderMode(layer);
		shot.projAfter = gSDK->GetProjection(layer);
		shots.push_back(shot);
		probe.log(std::string("  ") + where + " " + RenderSyncRenderName(mode) + " … **" +
				  std::to_string(shot.ms) + " ms** / 戻り値=" + RenderSyncYesNo(shot.ret) +
				  " / 刻み " + std::to_string(shot.ticks) +
				  " 回 / 読み戻しモード=" + RenderSyncRenderName(shot.modeAfter) +
				  " / 投影=" + RenderSyncProjectionName(shot.projAfter));
	};

	probe.log("");
	probe.log("■ S3 **3D のビューで** SetRenderMode(immediate=true, doProgress=true) を測る");
	for (TRenderMode mode : kRenderSyncModes)
		measureOne("3D", mode);

	probe.log("");
	probe.log("■ S4 **対照: 同じ図面・同じモデルを上面/平面（2D）に戻して**同じ測定をする");
	gSDK->SetRenderMode(layer, renderWireFrame, true, false);
	gSDK->SetProjection(layer, projectionPlan, false, true);
	probe.log(std::string("  投影を戻した → ") +
			  RenderSyncProjectionName(gSDK->GetProjection(layer)));
	for (TRenderMode mode : kRenderSyncModes)
		measureOne("2D", mode);

	// 3D へ戻しておく（利用者が絵を見て確かめられるように。描画モードはワイヤフレーム）。
	gSDK->SetRenderMode(layer, renderWireFrame, true, false);
	gSDK->SetProjection(layer, projectionOrthogonal, false, false);
	if (engine != nil)
		engine->ExecuteScript("SetView(-60, 0, 30, 0, 0, 0);");
	probe.log(std::string("  測り終えたので 3D のワイヤフレームへ戻した（投影=") +
			  RenderSyncProjectionName(gSDK->GetProjection(layer)) + "）");

	// -----------------------------------------------------------------------
	// S5 物差し——ビューポートに**中身を入れて** `UpdateViewport` を測る
	// -----------------------------------------------------------------------
	// ヘッダ: "a **dirty** viewport, **whose render type is other than wireframe or
	// sketch**, will be re-rendered"。#213 はこの 2 つの条件を踏んでいなかった疑いが
	// あるので、ここでは**表示レイヤを入れ・描画モードを書き・`SetDirty(true)` を
	// 立ててから**測る。段取りは Findings「寸法」「ビューポート」のとおり。
	probe.log("");
	probe.log("■ S5 物差し: ビューポートに中身を入れて UpdateViewport を測る");
	MCObjectHandle layerBefore = gSDK->GetActiveLayer();
	MCObjectHandle sheet = gSDK->CreateLayer(kRenderSyncSheetName, kLayerSheet);
	probe.log(std::string("  シートレイヤを作った=") + RenderSyncYesNo(sheet != nil) +
			  "（アクティブレイヤが移るので後で戻す）");
	MCObjectHandle viewport = (sheet != nil) ? gSDK->CreateViewport(sheet) : MCObjectHandle(nil);
	probe.log(std::string("  ビューポートを作った=") + RenderSyncYesNo(viewport != nil));
	long long emptyMs = -1;
	long long filledMs = -1;
	if (viewport != nil)
	{
		// (a) まず #213 と同じ条件——**作りたてのまま**更新して時間を測る。
		int before = gRenderSyncTicks.ticks;
		Clock::time_point t0 = Clock::now();
		gSDK->UpdateViewport(viewport);
		emptyMs = elapsedMsSince(t0);
		probe.log(std::string("  (a) **作りたてのまま**の UpdateViewport = **") +
				  std::to_string(emptyMs) + " ms** / 刻み " +
				  std::to_string(gRenderSyncTicks.ticks - before) + " 回（#213 は 131 ms だった）");

		// (b) 中身を入れる。
		bool prepared = false;
		try
		{
			VWViewportObj vpObj(viewport);
			vpObj.SetScale(100.0);
			vpObj.SetProjectionType(projectionOrthogonal);
			vpObj.SetProject2D(false);
			vpObj.SetViewType(standardViewRightIso);
			vpObj.SetRenderType(renderFinalRenderWorks);
			prepared = true;
		}
		catch (...)
		{
			probe.log("  **VWViewportObj で下ごしらえできなかった**（例外）");
		}
		// クラスは既定で全部非表示なので、ゲストを含めて全部表示へ戻す
		// （Findings「ビューポート」「寸法」）。
		int classes = 0;
		gSDK->ForEachClass(true,
						   [&](MCObjectHandle cls)
						   {
							   if (cls == nil)
								   return;
							   if (gSDK->SetViewportClassVisibility(
									   viewport, gSDK->GetObjectInternalIndex(cls), 0))
								   ++classes;
						   });
		// 表示レイヤ——**球を置いたデザインレイヤ**を表示にする。これが無いと
		// 「映すものが決まらない」（#213 の見立て）。
		const bool layerShown = (gSDK->SetViewportLayerVisibility(viewport, layer, 0) != false);
		probe.log(std::string("  下ごしらえ=") + RenderSyncYesNo(prepared) +
				  " / 表示へ戻したクラス=" + std::to_string(classes) +
				  " 件 / 表示レイヤを入れた=" + RenderSyncYesNo(layerShown));
		// **dirty を立てる**（ヘッダの条件）。
		bool dirtyBefore = false;
		try
		{
			VWViewportObj vpObj(viewport);
			vpObj.SetDirty(true);
			dirtyBefore = vpObj.IsDirty();
		}
		catch (...)
		{
		}
		WorldRect boundsBefore;
		gSDK->GetObjectBounds(viewport, boundsBefore);
		probe.log(std::string("  更新の前: dirty=") + RenderSyncYesNo(dirtyBefore) + " / 外接=左" +
				  RenderSyncNum((double)boundsBefore.left) + " 上" +
				  RenderSyncNum((double)boundsBefore.top) + " 右" +
				  RenderSyncNum((double)boundsBefore.right) + " 下" +
				  RenderSyncNum((double)boundsBefore.bottom));

		before = gRenderSyncTicks.ticks;
		t0 = Clock::now();
		gSDK->UpdateViewport(viewport);
		filledMs = elapsedMsSince(t0);
		const int filledTicks = gRenderSyncTicks.ticks - before;
		bool dirtyAfter = true;
		MCObjectHandle cache = nil;
		try
		{
			VWViewportObj vpObj(viewport);
			dirtyAfter = vpObj.IsDirty();
			vpObj.GetGroup(kViewportGroupCache, cache);
		}
		catch (...)
		{
		}
		WorldRect boundsAfter;
		gSDK->GetObjectBounds(viewport, boundsAfter);
		probe.log(std::string("  (b) **中身を入れてからの UpdateViewport = ") +
				  std::to_string(filledMs) + " ms** / 刻み " + std::to_string(filledTicks) + " 回");
		probe.log(std::string("  更新の後: dirty=") + RenderSyncYesNo(dirtyAfter) +
				  " / キャッシュ群=" + RenderSyncYesNo(cache != nil) + " / 外接=左" +
				  RenderSyncNum((double)boundsAfter.left) + " 上" +
				  RenderSyncNum((double)boundsAfter.top) + " 右" +
				  RenderSyncNum((double)boundsAfter.right) + " 下" +
				  RenderSyncNum((double)boundsAfter.bottom));
		probe.log("  ← **中身が入ったかの機械判定**: キャッシュ群が在る、または外接が"
				  "空枠（±26.649mm）より大きい（Findings「ビューポート」）");
	}
	if (layerBefore != nil)
	{
		gSDK->SetCurrentLayer(layerBefore);
		probe.log(std::string("  アクティブレイヤを元へ戻した（元 == いま＝") +
				  RenderSyncYesNo(gSDK->GetActiveLayer() == layerBefore) + "）");
	}

	// -----------------------------------------------------------------------
	// 判定——**読む側に解釈の余地を残さない**
	// -----------------------------------------------------------------------
	long long max3D = 0;
	long long max2D = 0;
	int ticks3D = 0;
	std::string slowest3D;
	for (const RenderSyncShot& s : shots)
	{
		if (s.where == "3D")
		{
			if (s.ms > max3D)
			{
				max3D = s.ms;
				slowest3D = RenderSyncRenderName(s.mode);
			}
			ticks3D += s.ticks;
		}
		else if (s.ms > max2D)
		{
			max2D = s.ms;
		}
	}
	const long long yardstick = (filledMs > emptyMs) ? filledMs : emptyMs;

	probe.log("");
	probe.log("■ まとめ");
	probe.log(std::string("  3D での SetRenderMode の最長=") + std::to_string(max3D) + " ms（" +
			  slowest3D + "） / 2D での最長=" + std::to_string(max2D) + " ms");
	probe.log(std::string("  物差し（ビューポート更新）= 作りたて ") + std::to_string(emptyMs) +
			  " ms / 中身あり " + std::to_string(filledMs) + " ms");
	probe.log(
		std::string("  刻みは全部で ") + std::to_string(gRenderSyncTicks.ticks) +
		" 回届いた（うち S3 の測定中が " + std::to_string(ticks3D) + " 回）/ モード=" +
		(gRenderSyncTicks.modes.empty() ? std::string("(届かなかった)") : gRenderSyncTicks.modes));
	probe.log("");

	if (!in3D)
	{
		probe.log("  → **判定できない。** 3D のビューへ切り替えられていないので、この回は"
				  "#213 の再測にしかなっていない（上の `probe.fail` のとおり）。");
	}
	else if (max3D >= kRenderSyncDrewMs)
	{
		probe.log(
			std::string("  → **判定: 3D のビューなら `SetRenderMode(…, immediate=true)` はその場で"
						"描く。** 最長 ") +
			std::to_string(max3D) + " ms（" + slowest3D +
			"）掛かって戻った。#213 の『0 ms で戻る』は**上面/平面（2D）ビューでの"
			"話だった**ということ。2D での最長は " +
			std::to_string(max2D) + " ms。");
		probe.log(std::string("    その最中に刻みが届いたか=") + RenderSyncYesNo(ticks3D > 0) +
				  "（" + std::to_string(ticks3D) + " 回）。");
	}
	else if (yardstick >= kRenderSyncDrewMs)
	{
		probe.log(
			std::string("  → **判定: 3D へ切り替えても `immediate=true` は同期では描かない。** "
						"3D での最長が ") +
			std::to_string(max3D) +
			" ms しかないのに、**同じ図面・同じモデルを"
			"ビューポートで描かせると " +
			std::to_string(yardstick) +
			" ms 掛かる**——つまり『描けば時間が掛かる』ことは示されている。"
			"0 ms 級で戻るのは『速かった』ではなく『描いていない』の意味である。");
		probe.log("    ＝ヘッダの『If immediate is true, then all rendering will take place "
				  "before the call returns.』は、**少なくともレイヤの画面描画については"
				  "実際と合わない**。");
	}
	else
	{
		probe.log(std::string("  → **判定できない。** 3D でも 2D でもビューポートでも所要が短く"
							  "（最長 ") +
				  std::to_string(yardstick) +
				  " ms）、『描いていない』と『速かった』を分けられない。");
		probe.fail(std::string("モデルが軽すぎて判定できない（球 ") + std::to_string(placed) +
				   " 個では描画に時間が掛からない）——格子を増やして測り直す必要がある");
	}

	probe.log("");
	probe.log("（ビューポートに中身が入ったかは S5 の『キャッシュ群』と『外接』で読む。"
			  "入っていなければ物差しとして使えないので、判定は上の『判定できない』に"
			  "倒れる。）");
	// タイマーは `timer` のデストラクタで外れる（戻る前に必ず）。
}
