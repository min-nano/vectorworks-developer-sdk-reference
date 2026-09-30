//
//	probes/runtime/section-vp-annot-page/probe.cpp
//
//	[issue #200] 断面ビューポートの「注釈座標（モデル mm）」と「シートレイヤの用紙座標
//	（用紙 mm）」の対応を実測する。
//
//	狙いは 1 つ——**注釈空間の決まった点（高さ Z＝GL＝0 の水平線）が用紙のどこに来るか**を
//	呼び出しだけで求める道があるか。候補は次の 4 つで、どれも同じ図面の中で同時に測る。
//
//	  ① ovViewportXPosition(1024) / ovViewportYPosition(1025)
//	     ——ヘッダは "the X/Y coordinate of the viewport on the sheet layer"（read only）。
//	       `VWViewportObj::GetPosition()` がこの 2 つを読んで WorldPt で返す。
//	  ② ovViewportUnscaledBoundsWithoutAnnotations(1052)
//	     ——ヘッダは "WorldRect read" だけ。「縮尺前の・注釈を除いた外接」が何座標で来るか。
//	  ③ ovViewportTransformMatrix(1049) / ovViewportViewMatrix(1050) /
//	     ovViewportOperatingTransform(1051) / ovSheetLayerSectionViewportViewMatrix(1055) /
//	     ovSectionViewportSectionViewMatrix(1056) の平行移動成分。
//	  ④ トリミング（crop）オブジェクトの外接。
//
//	どれも駄目なときの逃げ道（issue の 4 番）も同時に測る——**注釈へ置いた矩形を、
//	注釈座標（その矩形自身の GetObjectBounds）と用紙座標（それを含んだビューポートの
//	GetObjectBounds）の 2 通りで測って差を取る**。そのために注釈へ置く矩形は
//	「断面の中身よりはるかに外側」に置き、ビューポートの外接の縁を必ずその矩形が作るように
//	する（縁が中身でできていると差が取れない）。
//
//	併せて issue の 2・3 を測る:
//	  2. GetObjectBounds(viewport) が何で決まるか——高さの違う架構を映す 2 枚で比べ、
//	     高さ範囲（startHeight〜endHeight）の矩形が入るのか、映っているモデルだけなのか、
//	     自分で入れた注釈が入るのかを、同じ 2 枚で 3 段階（更新直後 / 注釈を置いた後 /
//	     ResetObject の後）測る。
//	  3. 用紙の上で MoveObject した後も、注釈の中の図形の GetObjectBounds は同じ値か。
//
//	**縮尺の解釈（ovViewportScale が 100 なのか 0.01 なのか）は当てにしない**——
//	注釈座標と用紙座標の両方で測った同じ矩形の幅から係数を割り出し、読み戻した値と併記する。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// 「ありふれた短い名前」を避ける（probes/runtime/README.md）。
	const double kProbeI200Scale = 100.0; // 1:100 のつもりで ovViewportScale へ書く値
	const double kProbeI200StartH = -1000.0;
	const double kProbeI200EndH = 9000.0;

	// 注釈へ置く目印の矩形。**断面の中身より十分に外側**へ置く（縁を必ずこれが作るように）。
	const double kProbeI200MarkLeft = 100000.0;
	const double kProbeI200MarkRight = 101000.0;
	const double kProbeI200MarkBottom = 200000.0;
	const double kProbeI200MarkTop = 201000.0;

	// 用紙の上でビューポートを動かす量（用紙 mm）。
	const double kProbeI200MoveDX = 137.0;
	const double kProbeI200MoveDY = -59.0;

	std::string ProbeI200Num(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.4f", v);
		return std::string(buf);
	}

	std::string ProbeI200RectText(const WorldRect& r)
	{
		const double left = static_cast<double>(r.left);
		const double top = static_cast<double>(r.top);
		const double right = static_cast<double>(r.right);
		const double bottom = static_cast<double>(r.bottom);
		return "左=" + ProbeI200Num(left) + " 下=" + ProbeI200Num(bottom) +
			   " 右=" + ProbeI200Num(right) + " 上=" + ProbeI200Num(top) +
			   " / 幅=" + ProbeI200Num(right - left) + " 高=" + ProbeI200Num(top - bottom);
	}

	// 行列は平行移動成分（xOff/yOff/zOff）と回転部を全部出す。どれが用紙上の位置を
	// 持っているか分からないため、当たりを付けずに並べる。
	std::string ProbeI200MatText(const TransformMatrix& m)
	{
		return "off=(" + ProbeI200Num(static_cast<double>(m.v1.xOff)) + ", " +
			   ProbeI200Num(static_cast<double>(m.v1.yOff)) + ", " +
			   ProbeI200Num(static_cast<double>(m.v1.zOff)) + ") a=[" + ProbeI200Num(m.v1.a00) +
			   " " + ProbeI200Num(m.v1.a01) + " " + ProbeI200Num(m.v1.a02) + " / " +
			   ProbeI200Num(m.v1.a10) + " " + ProbeI200Num(m.v1.a11) + " " +
			   ProbeI200Num(m.v1.a12) + " / " + ProbeI200Num(m.v1.a20) + " " +
			   ProbeI200Num(m.v1.a21) + " " + ProbeI200Num(m.v1.a22) + "]";
	}

	bool ProbeI200ReadRectVar(MCObjectHandle h, short selector, WorldRect& out)
	{
		WorldRect scratch;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return false;
		return block.GetWorldRect(out) != 0;
	}

	bool ProbeI200ReadMatVar(MCObjectHandle h, short selector, TransformMatrix& out)
	{
		TransformMatrix scratch;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return false;
		return block.GetTransformMatrix(out) != 0;
	}

	bool ProbeI200ReadRealVar(MCObjectHandle h, short selector, double& out)
	{
		double scratch = 0.0;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return false;
		return block.GetReal64(out) != 0;
	}

	bool ProbeI200ReadBoolVar(MCObjectHandle h, short selector, bool& out)
	{
		Boolean scratch = false;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return false;
		Boolean value = false;
		if (!block.GetBoolean(value))
			return false;
		out = (value != 0);
		return true;
	}

	// 高さ z1 の直方体を 1 つ作る（x0〜x1 × y 0〜1000 × z 0〜z1）。作成先はアクティブレイヤ。
	MCObjectHandle ProbeI200MakeBox(double x0, double x1, double z1)
	{
		VWFC::Math::VWPolygon2D base;
		base.AddVertex(x0, 0.0);
		base.AddVertex(x1, 0.0);
		base.AddVertex(x1, 1000.0);
		base.AddVertex(x0, 1000.0);
		base.SetClosed(true);
		VWFC::VWObjects::VWExtrudeObj extrude(base, 0.0, z1);
		return extrude;
	}

	// 1 枚のビューポートについて、測れるものを全部出す。
	void ProbeI200DumpViewport(vwprobe::Report& probe, const std::string& tag, MCObjectHandle vp)
	{
		WorldRect rect;
		if (gSDK->GetObjectBounds(vp, rect))
			probe.log("  [" + tag + "] GetObjectBounds(vp)（用紙座標） " + ProbeI200RectText(rect));
		else
			probe.log("  [" + tag + "] GetObjectBounds(vp) が false");

		double x = 0.0, y = 0.0;
		const bool okX = ProbeI200ReadRealVar(vp, ovViewportXPosition, x);
		const bool okY = ProbeI200ReadRealVar(vp, ovViewportYPosition, y);
		probe.log("  [" + tag + "] 1024/1025 位置 = (" +
				  (okX ? ProbeI200Num(x) : std::string("読めず")) + ", " +
				  (okY ? ProbeI200Num(y) : std::string("読めず")) + ")");

		WorldRect unscaled;
		if (ProbeI200ReadRectVar(vp, ovViewportUnscaledBoundsWithoutAnnotations, unscaled))
			probe.log("  [" + tag + "] 1052 縮尺前・注釈なしの外接 " + ProbeI200RectText(unscaled));
		else
			probe.log("  [" + tag + "] 1052 が読めず");

		const struct
		{
			short selector;
			const char* name;
		} kProbeI200Mats[] = {
			{ovViewportTransformMatrix, "1049 Transform"},
			{ovViewportViewMatrix, "1050 View"},
			{ovViewportOperatingTransform, "1051 Operating"},
			{ovSheetLayerSectionViewportViewMatrix, "1055 SLVP View"},
			{ovSectionViewportSectionViewMatrix, "1056 Section View"},
		};
		for (const auto& entry : kProbeI200Mats)
		{
			TransformMatrix mat;
			if (ProbeI200ReadMatVar(vp, entry.selector, mat))
				probe.log("  [" + tag + "] " + entry.name + " " + ProbeI200MatText(mat));
			else
				probe.log("  [" + tag + "] " + entry.name + " が読めず");
		}

		if (gSDK->ViewportHasCropObject(vp))
		{
			MCObjectHandle crop = gSDK->GetViewportCropObject(vp);
			WorldRect cropRect;
			if (crop != nil && gSDK->GetObjectBounds(crop, cropRect))
				probe.log("  [" + tag + "] crop の外接 " + ProbeI200RectText(cropRect));
			else
				probe.log("  [" + tag + "] crop はあるが外接が読めず（handle=" +
						  std::string(crop == nil ? "nil" : "非nil") + "）");
		}
		else
		{
			probe.log("  [" + tag + "] crop は無い（ViewportHasCropObject=false）");
		}

		MCObjectHandle annots = gSDK->GetViewportGroup(vp, kViewportGroupAnnotation);
		if (annots == nil)
		{
			probe.log("  [" + tag + "] 注釈群が nil");
		}
		else
		{
			WorldRect annotRect;
			if (gSDK->GetObjectBounds(annots, annotRect))
				probe.log("  [" + tag + "] 注釈群の外接 " + ProbeI200RectText(annotRect));
			else
				probe.log("  [" + tag + "] 注釈群の外接が読めず");
		}
	}
} // namespace

VW_PROBE("section-vp-annot-page", "断面VPの注釈→用紙座標", "外接と位置と行列を測る")
{
	// ---- 1. モデル（高さの違う直方体 2 つ）----------------------------------
	probe.log("■ 1. デザインレイヤと直方体 2 つ（高さ 3000 / 6000）を作る");
	MCObjectHandle designLayer = gSDK->CreateLayer("I200 モデル", static_cast<short>(kLayerDesign));
	if (designLayer == nil)
	{
		probe.fail("CreateLayer(kLayerDesign) が nil を返した");
		return;
	}

	MCObjectHandle boxLow = ProbeI200MakeBox(500.0, 2500.0, 3000.0);
	MCObjectHandle boxHigh = ProbeI200MakeBox(5500.0, 7500.0, 6000.0);
	if (boxLow == nil || boxHigh == nil)
	{
		probe.fail(std::string("VWExtrudeObj が nil を返した（低=") +
				   (boxLow == nil ? "nil" : "ok") + " 高=" + (boxHigh == nil ? "nil" : "ok") +
				   "）");
		return;
	}
	WorldRect boxRect;
	if (gSDK->GetObjectBounds(boxLow, boxRect))
		probe.log("  低い方の平面上の外接 " + ProbeI200RectText(boxRect));
	if (gSDK->GetObjectBounds(boxHigh, boxRect))
		probe.log("  高い方の平面上の外接 " + ProbeI200RectText(boxRect));

	// ---- 2. シートレイヤ ---------------------------------------------------
	probe.log("■ 2. シートレイヤを作る");
	MCObjectHandle sheet = gSDK->CreateLayer("I200 用紙", static_cast<short>(kLayerSheet));
	if (sheet == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	WorldPt sheetOrigin;
	if (gSDK->GetSheetLayerUserOrigin(sheet, sheetOrigin))
		probe.log("  GetSheetLayerUserOrigin = (" +
				  ProbeI200Num(static_cast<double>(sheetOrigin.x)) + ", " +
				  ProbeI200Num(static_cast<double>(sheetOrigin.y)) + ")");
	else
		probe.log("  GetSheetLayerUserOrigin が false");

	// ---- 3. 断面ビューポート 2 枚（同じ高さ範囲・同じ縮尺・映る高さだけが違う）----
	// 断面線は y=-3000 の水平線、pt3 はその手前（y=-8000）。モデルは y>=0 なので
	// **pt3 の反対側**＝断面に映る（Findings「Viewports」）。断面線の x の範囲で
	// どちらの直方体が映るかを分ける。
	probe.log("■ 3. 断面ビューポート 2 枚を作る（高さ範囲は両方 " + ProbeI200Num(kProbeI200StartH) +
			  "〜" + ProbeI200Num(kProbeI200EndH) + "）");

	MCObjectHandle vps[2] = {nil, nil};
	const char* const kProbeI200Names[2] = {"低(3000)", "高(6000)"};
	const double kProbeI200X0[2] = {0.0, 5000.0};
	const double kProbeI200X1[2] = {3000.0, 8000.0};

	for (int i = 0; i < 2; ++i)
	{
		const WorldPt pt1(kProbeI200X0[i], -3000.0);
		const WorldPt pt2(kProbeI200X1[i], -3000.0);
		const WorldPt pt3((kProbeI200X0[i] + kProbeI200X1[i]) * 0.5, -8000.0);
		vps[i] = gSDK->CreateSectionViewport(pt1, pt2, pt3, 0.0, kProbeI200StartH, kProbeI200EndH,
											 sheet);
		if (vps[i] == nil)
		{
			probe.fail(std::string("CreateSectionViewport が nil（") + kProbeI200Names[i] + "）");
			return;
		}

		// 表示の下ごしらえ（Findings「Viewports」「Layers and Stories」）。
		// **更新より前**に: 隠線消去 → 切断面より奥を表示 → レイヤとクラスを表示へ。
		VWFC::VWObjects::VWViewportObj vpObj(vps[i]);
		vpObj.SetRenderType(renderFinalHiddenLine);
		TVariableBlock beyond(static_cast<Boolean>(true));
		gSDK->SetObjectVariable(vps[i], ovSectionViewportDisplayObjectsBeyondCutPlane, beyond);
		TVariableBlock scaleBlock(kProbeI200Scale);
		gSDK->SetObjectVariable(vps[i], ovViewportScale, scaleBlock);
		gSDK->SetViewportLayerVisibility(
			vps[i], designLayer, static_cast<short>(VWFC::VWObjects::kLayerVisibilityNormal));
		MCObjectHandle capturedVP = vps[i];
		gSDK->ForEachClass(true,
						   [capturedVP](MCObjectHandle hClass)
						   {
							   gSDK->SetViewportClassVisibility(
								   capturedVP, VWFC::VWObjects::VWClass::GetClassFromHandle(hClass),
								   0);
						   });
		gSDK->UpdateViewport(vps[i]);

		double readScale = 0.0;
		bool isSection = false;
		ProbeI200ReadRealVar(vps[i], ovViewportScale, readScale);
		ProbeI200ReadBoolVar(vps[i], ovIsSectionViewport, isSection);
		probe.log(std::string("  ") + kProbeI200Names[i] + ": 1003 縮尺の読み戻し=" +
				  ProbeI200Num(readScale) + " / 1054 断面か=" + (isSection ? "true" : "false"));
	}

	// ---- 4. 段階 A: 更新直後（注釈は空）------------------------------------
	probe.log("■ 4. 段階 A（更新直後・注釈は空）——GetObjectBounds が何で決まるか");
	probe.log("  ※ 高さ範囲は 2 枚とも同じなので、外接の高さが違えば「範囲は入らない」、"
			  "同じなら「範囲が入る」と読める");
	for (int i = 0; i < 2; ++i)
		ProbeI200DumpViewport(probe, std::string("A ") + kProbeI200Names[i], vps[i]);

	// ---- 5. 注釈へ目印の矩形を置く -----------------------------------------
	// **中身よりはるかに外側**（100000〜101000, 200000〜201000）に置く。こうすると
	// ビューポートの外接の右上の縁は必ずこの矩形が作るので、
	//   用紙座標の縁 ↔ 注釈座標の縁
	// という 1 対 1 が取れる（issue の 4 番）。
	probe.log("■ 5. 注釈へ目印の矩形を置く（注釈座標 左" + ProbeI200Num(kProbeI200MarkLeft) +
			  " 下" + ProbeI200Num(kProbeI200MarkBottom) + " 右" +
			  ProbeI200Num(kProbeI200MarkRight) + " 上" + ProbeI200Num(kProbeI200MarkTop) + "）");
	MCObjectHandle marks[2] = {nil, nil};
	for (int i = 0; i < 2; ++i)
	{
		WorldRect want;
		want.left = kProbeI200MarkLeft;
		want.right = kProbeI200MarkRight;
		want.bottom = kProbeI200MarkBottom;
		want.top = kProbeI200MarkTop;
		marks[i] = gSDK->CreateRectangle(want);
		if (marks[i] == nil)
		{
			probe.fail(std::string("CreateRectangle が nil（") + kProbeI200Names[i] + "）");
			return;
		}
		if (!gSDK->AddViewportAnnotationObject(vps[i], marks[i]))
		{
			probe.fail(std::string("AddViewportAnnotationObject が false（") + kProbeI200Names[i] +
					   "）");
			return;
		}
		WorldRect got;
		if (gSDK->GetObjectBounds(marks[i], got))
			probe.log(std::string("  ") + kProbeI200Names[i] +
					  " 注釈に入れた矩形の GetObjectBounds " + ProbeI200RectText(got));
		else
			probe.log(std::string("  ") + kProbeI200Names[i] + " 矩形の外接が読めず");
	}

	probe.log("■ 6. 段階 B（注釈を置いた直後・ResetObject はまだ）");
	for (int i = 0; i < 2; ++i)
		ProbeI200DumpViewport(probe, std::string("B ") + kProbeI200Names[i], vps[i]);

	for (int i = 0; i < 2; ++i)
		gSDK->ResetObject(vps[i]);
	probe.log("■ 7. 段階 C（ResetObject の後）——ここで注釈が外接に入るかが出る");
	for (int i = 0; i < 2; ++i)
	{
		ProbeI200DumpViewport(probe, std::string("C ") + kProbeI200Names[i], vps[i]);
		WorldRect got;
		if (gSDK->GetObjectBounds(marks[i], got))
			probe.log(std::string("  C ") + kProbeI200Names[i] + " 注釈の矩形の外接 " +
					  ProbeI200RectText(got));
	}

	// ---- 8. 用紙の上で動かす ----------------------------------------------
	probe.log("■ 8. MoveObject で用紙の上を (" + ProbeI200Num(kProbeI200MoveDX) + ", " +
			  ProbeI200Num(kProbeI200MoveDY) + ") 動かす");
	for (int i = 0; i < 2; ++i)
		gSDK->MoveObject(vps[i], kProbeI200MoveDX, kProbeI200MoveDY);
	probe.log("■ 9. 段階 D（動かした直後・ResetObject はまだ）");
	for (int i = 0; i < 2; ++i)
	{
		ProbeI200DumpViewport(probe, std::string("D ") + kProbeI200Names[i], vps[i]);
		WorldRect got;
		if (gSDK->GetObjectBounds(marks[i], got))
			probe.log(std::string("  D ") + kProbeI200Names[i] + " 注釈の矩形の外接 " +
					  ProbeI200RectText(got));
	}

	for (int i = 0; i < 2; ++i)
		gSDK->ResetObject(vps[i]);
	probe.log("■ 10. 段階 E（動かして ResetObject した後）");
	for (int i = 0; i < 2; ++i)
	{
		ProbeI200DumpViewport(probe, std::string("E ") + kProbeI200Names[i], vps[i]);
		WorldRect got;
		if (gSDK->GetObjectBounds(marks[i], got))
			probe.log(std::string("  E ") + kProbeI200Names[i] + " 注釈の矩形の外接 " +
					  ProbeI200RectText(got));
	}

	// ---- 11. 割り出した対応 ------------------------------------------------
	// 目印の矩形は中身よりはるかに外側なので、ビューポートの外接の右上の縁はそれが作る。
	// 用紙 = 注釈 × k + off として k と off をその場で解く（縮尺の解釈に依らない）。
	probe.log("■ 11. 目印の矩形から割り出した「注釈座標 → 用紙座標」");
	for (int i = 0; i < 2; ++i)
	{
		WorldRect page, annot;
		if (!gSDK->GetObjectBounds(vps[i], page) || !gSDK->GetObjectBounds(marks[i], annot))
		{
			probe.log(std::string("  ") + kProbeI200Names[i] + ": 外接が読めず割り出せない");
			continue;
		}
		const double annotW = static_cast<double>(annot.right) - static_cast<double>(annot.left);
		const double annotH = static_cast<double>(annot.top) - static_cast<double>(annot.bottom);
		if (annotW == 0.0 || annotH == 0.0)
		{
			probe.log(std::string("  ") + kProbeI200Names[i] + ": 矩形の幅か高さが 0");
			continue;
		}
		// 縁がこの矩形で作られている前提の係数（x は右、y は上の縁で解く）。
		const double kx =
			(static_cast<double>(page.right) - static_cast<double>(page.left)) / annotW;
		const double ky =
			(static_cast<double>(page.top) - static_cast<double>(page.bottom)) / annotH;
		const double offX = static_cast<double>(page.right) - static_cast<double>(annot.right) * kx;
		const double offY = static_cast<double>(page.top) - static_cast<double>(annot.top) * ky;
		probe.log(std::string("  ") + kProbeI200Names[i] + ": k=(" + ProbeI200Num(kx) + ", " +
				  ProbeI200Num(ky) + ") off=(" + ProbeI200Num(offX) + ", " + ProbeI200Num(offY) +
				  ")");
		probe.log("    ＝注釈 y=0（GL）の用紙 y は " + ProbeI200Num(offY) +
				  " という見込み。上の 1024/1025 と 1049/1051 の off と見比べる");
		double px = 0.0, py = 0.0;
		ProbeI200ReadRealVar(vps[i], ovViewportXPosition, px);
		ProbeI200ReadRealVar(vps[i], ovViewportYPosition, py);
		probe.log("    1024/1025 との差 = (" + ProbeI200Num(offX - px) + ", " +
				  ProbeI200Num(offY - py) + ")");
	}

	probe.log("■ 終わり。図面は壊れたままでよい（新規の空図面で走らせる前提）");
}
