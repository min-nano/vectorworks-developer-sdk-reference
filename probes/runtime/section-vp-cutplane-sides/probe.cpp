//
//	probes/runtime/section-vp-cutplane-sides/probe.cpp
//
//	[issue #202] 断面ビューポートで **`1064`（奥）と `1065`（手前）がそれぞれ「どちら側の
//	モデル」を出すのか**を決め、併せて **`ovViewportUnscaledBoundsWithoutAnnotations`
//	（1052）と `ovViewportAngleWithXAxis`（1026）が何を返すか**を測る。
//
//	■ なぜ要るか——#189 と #200 の記録が逆に見える
//
//	・Findings「Viewports」（#189 由来）: 「モデルを `pt3` の反対側に置いた枚は **`1064`
//	  だけ**で映り、同じ側に置いた枚は `1065` を立てても空だった」
//	・#200 の 8 通り: モデルを `pt3` の反対側に置いた枚は **`1064` だけでは空**で、
//	  **`1065` を立てた枚だけが映った**
//
//	**どちらの数値も実測である。** ただし #189 のログを読み直すと、6 枚すべてで
//	**1064 と 1065 の両方が `true`**（読み戻しも両方 `true`）だった——つまり #189 は
//	「どちらのフラグが効いたか」を分けていない。分かっているのは「`pt3` を反対側へ
//	振ると空になった」ことだけである。したがって**フラグとモデルの側の対応は、まだ
//	誰も測っていない**。ここで測る。
//
//	■ 読み違えようのない測り方——**2 つの箱を Z で離す**
//
//	「描けたか／空か」だけでは、どちら側のモデルが出たのか分からない。そこで切断面の
//	両側に箱を 1 つずつ置き、**Z の範囲を重ならないようにずらす**:
//
//	  ・S 箱（切断面の **−Y 側**）… Z **0〜3000**
//	  ・N 箱（切断面の **+Y 側**）… Z **5000〜8000**（浮かせてある）
//
//	断面の中身の外接は注釈座標で読め、**その y はモデルの Z そのもの**（#189・#200）。
//	だから外接の y を見るだけで「S だけ／N だけ／両方／空」が一意に決まる——プローブが
//	その判定まで済ませて 1 行で出す。箱は切断面から **4500〜5500mm 離して**ある
//	（#189 は 2000〜6000・#200 は 5000 離れていた。近付けて別の条件を混ぜないため）。
//
//	■ 何を振るか（16 通り = 2 × 2 × 4）
//
//	  ・断面線の向き pt1→pt2 … **+x / −x**（#189 も #200 も +x だけだった）
//	  ・`pt3`（見る側）… 切断面の **−Y 側 / +Y 側**
//	  ・1064 / 1065 … **(F,F) / (T,F) / (F,T) / (T,T)**（(F,F) は対照）
//
//	これで「奥／手前を決めているのは断面線の向きか・`pt3` の側か・その両方か」が割れる。
//	続けて (2) 切断面が実際にモデルを切る配置（#200 の通り 3）、(3) #189 の配置の再現、
//	(4) 中身のある枚での 1052 / 1026 を同じ図面の中で測る。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const double kProbeI202Scale = 100.0; // 1:100 のつもりで ovViewportScale へ書く値

	// 断面線と箱（固定）。切断面は y = 2500。
	const double kProbeI202LineY = 2500.0;
	const double kProbeI202LineX0 = 0.0;
	const double kProbeI202LineX1 = 3000.0;
	const double kProbeI202BoxX0 = 500.0;
	const double kProbeI202BoxX1 = 2500.0;
	const double kProbeI202Pt3Off = 9000.0; // pt3 を切断面からこれだけ離す
	const double kProbeI202StartH = -1000.0;
	const double kProbeI202EndH = 9000.0;
	// S 箱（−Y 側）: y −3000〜−2000・Z 0〜3000 ／ N 箱（+Y 側）: y 7000〜8000・Z 5000〜8000
	const double kProbeI202SouthY0 = -3000.0;
	const double kProbeI202SouthY1 = -2000.0;
	const double kProbeI202NorthY0 = 7000.0;
	const double kProbeI202NorthY1 = 8000.0;
	const double kProbeI202NorthBase = 5000.0; // 浮かせる高さ（S と Z で重ならない）
	const double kProbeI202SplitZ = 4000.0;	   // S（〜3000）と N（5000〜）を分ける境

	std::string ProbeI202Num(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.3f", v);
		return std::string(buf);
	}

	// 空の容れ物の外接は「反転した矩形」で返る（左 > 右）。#200 で確定。
	bool ProbeI202RectIsEmpty(const WorldRect& r)
	{
		return static_cast<double>(r.left) > static_cast<double>(r.right) ||
			   static_cast<double>(r.bottom) > static_cast<double>(r.top);
	}

	std::string ProbeI202RectText(const WorldRect& r)
	{
		if (ProbeI202RectIsEmpty(r))
			return "【空＝反転した矩形】";
		return "左=" + ProbeI202Num(static_cast<double>(r.left)) +
			   " 下=" + ProbeI202Num(static_cast<double>(r.bottom)) +
			   " 右=" + ProbeI202Num(static_cast<double>(r.right)) +
			   " 上=" + ProbeI202Num(static_cast<double>(r.top)) +
			   " / 幅=" + ProbeI202Num(static_cast<double>(r.right) - static_cast<double>(r.left)) +
			   " 高=" + ProbeI202Num(static_cast<double>(r.top) - static_cast<double>(r.bottom));
	}

	bool ProbeI202RectNear(const WorldRect& a, const WorldRect& b, double tol)
	{
		if (ProbeI202RectIsEmpty(a) || ProbeI202RectIsEmpty(b))
			return false;
		return std::fabs(static_cast<double>(a.left) - static_cast<double>(b.left)) <= tol &&
			   std::fabs(static_cast<double>(a.bottom) - static_cast<double>(b.bottom)) <= tol &&
			   std::fabs(static_cast<double>(a.right) - static_cast<double>(b.right)) <= tol &&
			   std::fabs(static_cast<double>(a.top) - static_cast<double>(b.top)) <= tol;
	}

	bool ProbeI202ReadRealVar(MCObjectHandle h, short selector, double& out)
	{
		double scratch = 0.0;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return false;
		return block.GetReal64(out) != 0;
	}

	bool ProbeI202ReadBoolVar(MCObjectHandle h, short selector, bool& out)
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

	bool ProbeI202ReadRectVar(MCObjectHandle h, short selector, WorldRect& out)
	{
		WorldRect scratch;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return false;
		return block.GetWorldRect(out) != 0;
	}

	void ProbeI202WriteBoolVar(MCObjectHandle h, short selector, bool value)
	{
		TVariableBlock block(static_cast<Boolean>(value));
		gSDK->SetObjectVariable(h, selector, block);
	}

	size_t ProbeI202CountMembers(MCObjectHandle group)
	{
		size_t count = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(group); h != nil; h = gSDK->NextObject(h))
			++count;
		return count;
	}

	// 直方体（x0〜x1 × y0〜y1 × Z baseZ〜baseZ+thick）。**アクティブレイヤに入る**ので、
	// 呼ぶ前に SetCurrentLayer しておく（レイヤごとに表示を切り替えて測るため）。
	MCObjectHandle ProbeI202MakeBox(double x0, double x1, double y0, double y1, double baseZ,
									double thick)
	{
		VWFC::Math::VWPolygon2D base;
		base.AddVertex(x0, y0);
		base.AddVertex(x1, y0);
		base.AddVertex(x1, y1);
		base.AddVertex(x0, y1);
		base.SetClosed(true);
		VWFC::VWObjects::VWExtrudeObj extrude(base, baseZ, thick);
		return extrude;
	}

	// 指定したデザインレイヤだけを表示にする（他は非表示）。クラスは全部表示へ戻す
	// ——1050 が断面の向きへ入る条件（Findings「Viewports」#151）。
	void ProbeI202SetVisibility(MCObjectHandle vp, const std::vector<MCObjectHandle>& showLayers)
	{
		const std::vector<MCObjectHandle>* shown = &showLayers;
		gSDK->ForEachLayerN(
			[vp, shown](MCObjectHandle layer)
			{
				bool visible = false;
				for (size_t i = 0; i < shown->size(); ++i)
					if ((*shown)[i] == layer)
						visible = true;
				gSDK->SetViewportLayerVisibility(vp, layer, visible ? 0 : -1);
			});
		gSDK->ForEachClass(
			true, [vp](MCObjectHandle cls)
			{ gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0); });
	}

	// 1 枚作って下ごしらえ（レンダ・1064/1065・縮尺）を済ませ、更新する。
	MCObjectHandle ProbeI202MakeViewport(MCObjectHandle sheet, double lineY, double lineX0,
										 double lineX1, bool dirPlusX, double pt3Y, bool beyond,
										 bool before, const std::vector<MCObjectHandle>& showLayers)
	{
		const WorldPt pt1(dirPlusX ? lineX0 : lineX1, lineY);
		const WorldPt pt2(dirPlusX ? lineX1 : lineX0, lineY);
		const WorldPt pt3((lineX0 + lineX1) * 0.5, pt3Y);
		MCObjectHandle vp = gSDK->CreateSectionViewport(pt1, pt2, pt3, 0.0, kProbeI202StartH,
														kProbeI202EndH, sheet);
		if (vp == nil)
			return nil;
		ProbeI202SetVisibility(vp, showLayers);
		VWFC::VWObjects::VWViewportObj vpObj(vp);
		vpObj.SetRenderType(renderFinalHiddenLine);
		ProbeI202WriteBoolVar(vp, ovSectionViewportDisplayObjectsBeyondCutPlane, beyond);
		ProbeI202WriteBoolVar(vp, ovSectionViewportDisplayObjectsBeforeCutPlane, before);
		ProbeI202WriteBoolVar(vp, ovViewportDisplayPlanar, false);
		TVariableBlock scaleBlock(kProbeI202Scale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleBlock);
		gSDK->UpdateViewport(vp);
		return vp;
	}

	// キャッシュ群（3/5/6/7/15）の件数を 1 行に畳み、中身の外接を合成する。
	// 群 4（断面群）は断面が空でも必ず在るので、合成には使わない（#200 と同じ扱い）。
	std::string ProbeI202Caches(MCObjectHandle vp, WorldRect& outContent, bool& outHasContent)
	{
		outHasContent = false;
		const short kProbeI202Groups[] = {3, 4, 5, 6, 7, 15};
		std::string line;
		for (size_t i = 0; i < sizeof(kProbeI202Groups) / sizeof(kProbeI202Groups[0]); ++i)
		{
			const short groupType = kProbeI202Groups[i];
			if (!line.empty())
				line += " ";
			line += std::to_string(groupType) + "=";
			MCObjectHandle group = gSDK->GetViewportGroup(vp, groupType);
			if (group == nil)
			{
				line += "nil";
				continue;
			}
			line += std::to_string(ProbeI202CountMembers(group));
			WorldRect rect;
			if (!gSDK->GetObjectBounds(group, rect) || ProbeI202RectIsEmpty(rect))
			{
				line += "(空)";
				continue;
			}
			if (groupType == 4)
				continue;
			if (!outHasContent)
			{
				outContent = rect;
				outHasContent = true;
			}
			else
			{
				if (static_cast<double>(rect.left) < static_cast<double>(outContent.left))
					outContent.left = rect.left;
				if (static_cast<double>(rect.bottom) < static_cast<double>(outContent.bottom))
					outContent.bottom = rect.bottom;
				if (static_cast<double>(rect.right) > static_cast<double>(outContent.right))
					outContent.right = rect.right;
				if (static_cast<double>(rect.top) > static_cast<double>(outContent.top))
					outContent.top = rect.top;
			}
		}
		return line;
	}

	// 中身の外接の y（＝モデルの Z）から、どちらの箱が出たのかを決める。
	// S 箱は Z 0〜3000・N 箱は Z 5000〜8000 なので、境（4000）の上下で一意に割れる。
	enum ProbeI202Side
	{
		kProbeI202SideNone = 0,
		kProbeI202SideSouth = 1,
		kProbeI202SideNorth = 2,
		kProbeI202SideBoth = 3
	};

	ProbeI202Side ProbeI202WhichBox(bool hasContent, const WorldRect& r)
	{
		if (!hasContent)
			return kProbeI202SideNone;
		const bool south = static_cast<double>(r.bottom) < kProbeI202SplitZ;
		const bool north = static_cast<double>(r.top) > kProbeI202SplitZ;
		if (south && north)
			return kProbeI202SideBoth;
		if (south)
			return kProbeI202SideSouth;
		if (north)
			return kProbeI202SideNorth;
		return kProbeI202SideNone;
	}

	const char* ProbeI202SideText(ProbeI202Side side)
	{
		switch (side)
		{
		case kProbeI202SideSouth:
			return "S だけ（切断面の −Y 側・Z 0〜3000）";
		case kProbeI202SideNorth:
			return "N だけ（切断面の +Y 側・Z 5000〜8000）";
		case kProbeI202SideBoth:
			return "S と N の両方";
		default:
			return "どちらも出ない（空）";
		}
	}

	// 1052 / 1026 / 1024 / 1025 と用紙の外接を 2 行で出す。
	void ProbeI202LogPageVars(vwprobe::Report& probe, MCObjectHandle vp, const std::string& tag)
	{
		WorldRect page;
		const bool okPage = gSDK->GetObjectBounds(vp, page) != 0;
		WorldRect unscaled;
		const bool ok1052 =
			ProbeI202ReadRectVar(vp, ovViewportUnscaledBoundsWithoutAnnotations, unscaled);
		double px = 0.0, py = 0.0, angle = 0.0, scale = 0.0;
		const bool ok1024 = ProbeI202ReadRealVar(vp, ovViewportXPosition, px);
		const bool ok1025 = ProbeI202ReadRealVar(vp, ovViewportYPosition, py);
		const bool ok1026 = ProbeI202ReadRealVar(vp, ovViewportAngleWithXAxis, angle);
		const bool ok1003 = ProbeI202ReadRealVar(vp, ovViewportScale, scale);
		probe.log("    " + tag + " 用紙の外接 " +
				  (okPage ? ProbeI202RectText(page) : std::string("読めず")));
		probe.log("    " + tag + " 1052 " +
				  (ok1052 ? ProbeI202RectText(unscaled) : std::string("読めず")) +
				  " / 1024=" + (ok1024 ? ProbeI202Num(px) : std::string("×")) +
				  " 1025=" + (ok1025 ? ProbeI202Num(py) : std::string("×")) +
				  " 1026=" + (ok1026 ? ProbeI202Num(angle) : std::string("×")) +
				  " 1003=" + (ok1003 ? ProbeI202Num(scale) : std::string("×")));
	}
} // namespace

VW_PROBE("section-vp-cutplane-sides", "断面の奥/手前と1052", "1064/1065 がどちら側を出すか")
{
	// ---- 1. モデル。レイヤ 1 枚につき箱 1 つ（表示を切り替えて測るため）----------
	probe.log("■ 1. 箱を 3 つ、別々のデザインレイヤへ置く");
	probe.log("  切断面は y=2500。S 箱＝y -3000〜-2000・Z 0〜3000（−Y 側） / "
			  "N 箱＝y 7000〜8000・Z 5000〜8000（+Y 側） / X 箱＝y 2000〜3000・Z 0〜3000（切る）");
	MCObjectHandle layerSouth = gSDK->CreateLayer("i202-S", static_cast<short>(kLayerDesign));
	MCObjectHandle layerNorth = gSDK->CreateLayer("i202-N", static_cast<short>(kLayerDesign));
	MCObjectHandle layerCross = gSDK->CreateLayer("i202-X", static_cast<short>(kLayerDesign));
	MCObjectHandle layer189 = gSDK->CreateLayer("i202-189", static_cast<short>(kLayerDesign));
	if (layerSouth == nil || layerNorth == nil || layerCross == nil || layer189 == nil)
	{
		probe.fail("CreateLayer(kLayerDesign) が nil を返した");
		return;
	}

	gSDK->SetCurrentLayer(layerSouth);
	MCObjectHandle boxSouth = ProbeI202MakeBox(kProbeI202BoxX0, kProbeI202BoxX1, kProbeI202SouthY0,
											   kProbeI202SouthY1, 0.0, 3000.0);
	gSDK->SetCurrentLayer(layerNorth);
	MCObjectHandle boxNorth = ProbeI202MakeBox(kProbeI202BoxX0, kProbeI202BoxX1, kProbeI202NorthY0,
											   kProbeI202NorthY1, kProbeI202NorthBase, 3000.0);
	gSDK->SetCurrentLayer(layerCross);
	MCObjectHandle boxCross =
		ProbeI202MakeBox(kProbeI202BoxX0, kProbeI202BoxX1, 2000.0, 3000.0, 0.0, 3000.0);
	// #189 の配置（線 y=4000・箱 x 0〜8000 × y 6000〜8000 × Z 0〜3000）をそのまま作る。
	gSDK->SetCurrentLayer(layer189);
	MCObjectHandle box189 = ProbeI202MakeBox(0.0, 8000.0, 6000.0, 8000.0, 0.0, 3000.0);
	if (boxSouth == nil || boxNorth == nil || boxCross == nil || box189 == nil)
	{
		probe.fail("VWExtrudeObj が nil を返した（箱を作れなかった）");
		return;
	}

	struct ProbeI202BoxInfo
	{
		const char* name;
		MCObjectHandle h;
	};
	const ProbeI202BoxInfo kProbeI202Boxes[] = {
		{"S", boxSouth}, {"N", boxNorth}, {"X", boxCross}, {"189", box189}};
	for (size_t i = 0; i < sizeof(kProbeI202Boxes) / sizeof(kProbeI202Boxes[0]); ++i)
	{
		VWFC::VWObjects::VWExtrudeObj ext(kProbeI202Boxes[i].h);
		double baseElev = 0.0, thickness = 0.0;
		ext.GetExtrudeValues(baseElev, thickness);
		WorldRect plan;
		const bool okPlan = gSDK->GetObjectBounds(kProbeI202Boxes[i].h, plan) != 0;
		probe.log(std::string("  ") + kProbeI202Boxes[i].name +
				  " 箱: 基準高=" + ProbeI202Num(baseElev) + " 厚み=" + ProbeI202Num(thickness) +
				  " 平面の外接 " + (okPlan ? ProbeI202RectText(plan) : std::string("読めず")));
	}

	MCObjectHandle sheet = gSDK->CreateLayer("i202-用紙", static_cast<short>(kLayerSheet));
	if (sheet == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}

	// ---- 2. 本題: 断面線の向き × pt3 の側 × 1064/1065 の 16 通り -----------------
	probe.log("■ 2. 【本題】断面線の向き（+x / -x）× pt3 の側（-Y / +Y）× 1064/1065 の 16 通り");
	probe.log("  S 箱と N 箱の両方を表示にして 1 枚ずつ作る。**出たのがどちらの箱かは"
			  "中身の外接の y（＝モデルの Z）で一意に決まる**（S は〜3000・N は 5000〜）");

	std::vector<MCObjectHandle> showSN;
	showSN.push_back(layerSouth);
	showSN.push_back(layerNorth);

	// 16 通りの結果を覚えておく（後で表に畳み、1052 用の「中身のある枚」を選ぶ）。
	struct ProbeI202Result
	{
		bool dirPlusX;
		bool pt3MinusY;
		bool beyond;
		bool before;
		ProbeI202Side side;
	};
	std::vector<ProbeI202Result> results;

	for (int dirIdx = 0; dirIdx < 2; ++dirIdx)
	{
		const bool dirPlusX = (dirIdx == 0);
		for (int eyeIdx = 0; eyeIdx < 2; ++eyeIdx)
		{
			const bool pt3MinusY = (eyeIdx == 0);
			const double pt3Y =
				pt3MinusY ? kProbeI202LineY - kProbeI202Pt3Off : kProbeI202LineY + kProbeI202Pt3Off;
			for (int flagIdx = 0; flagIdx < 4; ++flagIdx)
			{
				const bool beyond = (flagIdx == 1 || flagIdx == 3);
				const bool before = (flagIdx == 2 || flagIdx == 3);
				const std::string head = std::string("— 向き=") + (dirPlusX ? "+x" : "-x") +
										 " pt3=" + (pt3MinusY ? "-Y 側" : "+Y 側") +
										 " 1064=" + (beyond ? "T" : "F") +
										 " 1065=" + (before ? "T" : "F");
				MCObjectHandle vp =
					ProbeI202MakeViewport(sheet, kProbeI202LineY, kProbeI202LineX0,
										  kProbeI202LineX1, dirPlusX, pt3Y, beyond, before, showSN);
				if (vp == nil)
				{
					probe.log(head + " → CreateSectionViewport が nil");
					continue;
				}
				bool readBeyond = false, readBefore = false;
				ProbeI202ReadBoolVar(vp, ovSectionViewportDisplayObjectsBeyondCutPlane, readBeyond);
				ProbeI202ReadBoolVar(vp, ovSectionViewportDisplayObjectsBeforeCutPlane, readBefore);
				WorldRect content;
				bool hasContent = false;
				const std::string cacheLine = ProbeI202Caches(vp, content, hasContent);
				const ProbeI202Side side = ProbeI202WhichBox(hasContent, content);
				probe.log(head + " 読戻し 1064=" + (readBeyond ? "T" : "F") +
						  " 1065=" + (readBefore ? "T" : "F"));
				probe.log(std::string("    ★出たのは: ") + ProbeI202SideText(side));
				probe.log("    群 " + cacheLine + " / 中身の外接（注釈座標） " +
						  (hasContent ? ProbeI202RectText(content) : std::string("無し")));
				ProbeI202Result rec;
				rec.dirPlusX = dirPlusX;
				rec.pt3MinusY = pt3MinusY;
				rec.beyond = beyond;
				rec.before = before;
				rec.side = side;
				results.push_back(rec);
			}
		}
	}

	// ---- 3. 16 通りを畳んで、何が「奥／手前」を決めているのかを出す ---------------
	probe.log("■ 3. 畳んだ表——(1064,1065) ごとに、向きと pt3 で出た箱がどう変わるか");
	const char* const kProbeI202FlagNames[4] = {"(F,F)", "(T,F)", "(F,T)", "(T,T)"};
	for (int flagIdx = 0; flagIdx < 4; ++flagIdx)
	{
		std::string row = std::string("  1064/1065=") + kProbeI202FlagNames[flagIdx] + ":";
		ProbeI202Side cell[2][2] = {{kProbeI202SideNone, kProbeI202SideNone},
									{kProbeI202SideNone, kProbeI202SideNone}};
		for (size_t i = 0; i < results.size(); ++i)
		{
			const ProbeI202Result& r = results[i];
			const int idx = (r.beyond ? 1 : 0) + (r.before ? 2 : 0);
			if (idx != flagIdx)
				continue;
			cell[r.dirPlusX ? 0 : 1][r.pt3MinusY ? 0 : 1] = r.side;
		}
		for (int d = 0; d < 2; ++d)
			for (int e = 0; e < 2; ++e)
				row += std::string(" [向き") + (d == 0 ? "+x" : "-x") + "/pt3" +
					   (e == 0 ? "-Y" : "+Y") + "→" + ProbeI202SideText(cell[d][e]) + "]";
		probe.log(row);
		// 何に依っているかを機械で判定する（人が読み違えないように）。
		bool dependsOnDir = false, dependsOnEye = false;
		for (int e = 0; e < 2; ++e)
			if (cell[0][e] != cell[1][e])
				dependsOnDir = true;
		for (int d = 0; d < 2; ++d)
			if (cell[d][0] != cell[d][1])
				dependsOnEye = true;
		probe.log(std::string("    → 断面線の向きで変わるか=") +
				  (dependsOnDir ? "はい" : "いいえ") +
				  " / pt3 の側で変わるか=" + (dependsOnEye ? "はい" : "いいえ"));
	}

	// ---- 4. 切断面がモデルを切る配置（#200 の通り 3）---------------------------
	probe.log("■ 4. 切断面がモデルを切る配置（X 箱だけを表示。#200 の通り 3 と同じ）");
	std::vector<MCObjectHandle> showX;
	showX.push_back(layerCross);
	for (int flagIdx = 0; flagIdx < 4; ++flagIdx)
	{
		const bool beyond = (flagIdx == 1 || flagIdx == 3);
		const bool before = (flagIdx == 2 || flagIdx == 3);
		MCObjectHandle vp =
			ProbeI202MakeViewport(sheet, kProbeI202LineY, kProbeI202LineX0, kProbeI202LineX1, true,
								  kProbeI202LineY - kProbeI202Pt3Off, beyond, before, showX);
		if (vp == nil)
		{
			probe.log(std::string("  1064/1065=") + kProbeI202FlagNames[flagIdx] +
					  " → CreateSectionViewport が nil");
			continue;
		}
		WorldRect content;
		bool hasContent = false;
		const std::string cacheLine = ProbeI202Caches(vp, content, hasContent);
		probe.log(std::string("  1064/1065=") + kProbeI202FlagNames[flagIdx] +
				  " 向き=+x pt3=-Y 側 → " + (hasContent ? "描けた" : "空") + " / 群 " + cacheLine);
		probe.log("    中身の外接（注釈座標） " +
				  (hasContent ? ProbeI202RectText(content) : std::string("無し")));
	}

	// ---- 5. #189 の配置の再現（線 y=4000・箱は +Y 側だけ）----------------------
	probe.log("■ 5. #189 の配置の再現（断面線 y=4000・x 0〜8000。箱は +Y 側の y 6000〜8000 だけ）");
	probe.log("  #189 は 6 枚すべてで 1064 と 1065 の両方を true にしていた。pt3 を +Y 側へ"
			  "振ると空になったのがあのときの観測で、そこを同じ図面で測り直す");
	std::vector<MCObjectHandle> show189;
	show189.push_back(layer189);
	struct ProbeI202Case189
	{
		const char* name;
		bool pt3MinusY;
		bool beyond;
		bool before;
	};
	const ProbeI202Case189 kProbeI202Cases189[] = {
		{"#189 の A 相当（pt3=-Y・1064/1065 両方 T）", true, true, true},
		{"#189 の E 相当（pt3=+Y・1064/1065 両方 T）", false, true, true},
		{"pt3=-Y・1064 だけ T", true, true, false},
		{"pt3=-Y・1065 だけ T", true, false, true},
	};
	for (size_t i = 0; i < sizeof(kProbeI202Cases189) / sizeof(kProbeI202Cases189[0]); ++i)
	{
		const ProbeI202Case189& c = kProbeI202Cases189[i];
		const double pt3Y = c.pt3MinusY ? -5000.0 : 14000.0;
		MCObjectHandle vp = ProbeI202MakeViewport(sheet, 4000.0, 0.0, 8000.0, true, pt3Y, c.beyond,
												  c.before, show189);
		if (vp == nil)
		{
			probe.log(std::string("  ") + c.name + " → CreateSectionViewport が nil");
			continue;
		}
		WorldRect content;
		bool hasContent = false;
		const std::string cacheLine = ProbeI202Caches(vp, content, hasContent);
		probe.log(std::string("  ") + c.name + " → " + (hasContent ? "描けた" : "空") + " / 群 " +
				  cacheLine);
		probe.log("    中身の外接（注釈座標） " +
				  (hasContent ? ProbeI202RectText(content) : std::string("無し")));
	}

	// ---- 6. 中身のある枚で 1052 と 1026 を読む --------------------------------
	probe.log("■ 6. 【本題 2】中身のある断面ビューポートで 1052 と 1026 を読む");
	probe.log("  #200 は 1052 を「断面が空の枚」でしか読んでおらず、5 段階すべて空（反転した"
			  "矩形）だった。中身が入る配置で読み直す");
	int winner = -1;
	for (size_t i = 0; i < results.size(); ++i)
		if (results[i].side != kProbeI202SideNone)
		{
			winner = static_cast<int>(i);
			break;
		}
	if (winner < 0)
	{
		probe.fail("16 通りのどれでも断面に中身が入らなかった——1052 は中身のある枚で読めていない");
	}
	else
	{
		const ProbeI202Result& w = results[static_cast<size_t>(winner)];
		const double pt3Y =
			w.pt3MinusY ? kProbeI202LineY - kProbeI202Pt3Off : kProbeI202LineY + kProbeI202Pt3Off;
		probe.log(std::string("  使う配置: 向き=") + (w.dirPlusX ? "+x" : "-x") +
				  " pt3=" + (w.pt3MinusY ? "-Y 側" : "+Y 側") + " 1064=" + (w.beyond ? "T" : "F") +
				  " 1065=" + (w.before ? "T" : "F"));
		MCObjectHandle vp =
			ProbeI202MakeViewport(sheet, kProbeI202LineY, kProbeI202LineX0, kProbeI202LineX1,
								  w.dirPlusX, pt3Y, w.beyond, w.before, showSN);
		if (vp == nil)
		{
			probe.fail("1052 用のビューポートを作れなかった");
		}
		else
		{
			WorldRect content;
			bool hasContent = false;
			const std::string cacheLine = ProbeI202Caches(vp, content, hasContent);
			probe.log("  [a] 更新直後（注釈は空）: 群 " + cacheLine);
			probe.log("    中身の外接（注釈座標） " +
					  (hasContent ? ProbeI202RectText(content) : std::string("無し")));
			ProbeI202LogPageVars(probe, vp, "[a]");

			// 1052 が何座標・何の外接なのかを、その場で突き合わせる。
			WorldRect unscaled;
			if (ProbeI202ReadRectVar(vp, ovViewportUnscaledBoundsWithoutAnnotations, unscaled))
			{
				WorldRect page;
				const bool okPage = gSDK->GetObjectBounds(vp, page) != 0;
				std::string verdict = "  [a] 1052 は";
				if (ProbeI202RectIsEmpty(unscaled))
					verdict += " **空（反転した矩形）のまま**——中身があっても空";
				else
				{
					if (hasContent && ProbeI202RectNear(unscaled, content, 1.0))
						verdict += " **中身の外接（注釈座標＝モデル mm）と一致**";
					else if (okPage && ProbeI202RectNear(unscaled, page, 1.0))
						verdict += " **用紙の外接と一致**";
					else
						verdict += " どちらとも一致しない（値は上の行のとおり）";
					if (okPage && !ProbeI202RectIsEmpty(page))
					{
						const double wr =
							(static_cast<double>(unscaled.right) -
							 static_cast<double>(unscaled.left)) /
							(static_cast<double>(page.right) - static_cast<double>(page.left));
						verdict += " / 幅の比（1052 ÷ 用紙）=" + ProbeI202Num(wr);
					}
				}
				probe.log(verdict);
			}

			// [b] 注釈へ**遠くの矩形**を足す → 1052 が動かなければ「注釈を除く」が本当。
			WorldRect want;
			want.left = 100000.0;
			want.right = 101000.0;
			want.bottom = 200000.0;
			want.top = 201000.0;
			MCObjectHandle mark = gSDK->CreateRectangle(want);
			if (mark == nil || !gSDK->AddViewportAnnotationObject(vp, mark))
			{
				probe.log("  [b] 注釈へ矩形を置けなかった——「注釈を除く」の確認は飛ばす");
			}
			else
			{
				gSDK->ResetObject(vp);
				probe.log("  [b] 注釈へ遠くの矩形（注釈座標 100000〜101000 × 200000〜201000）を"
						  "足して ResetObject した後");
				ProbeI202LogPageVars(probe, vp, "[b]");
			}

			// [c] 用紙の上で回して 1026 を読む（1026 は read only なので回して確かめる）。
			double px = 0.0, py = 0.0;
			ProbeI202ReadRealVar(vp, ovViewportXPosition, px);
			ProbeI202ReadRealVar(vp, ovViewportYPosition, py);
			MCObjectHandle rotTarget = vp;
			gSDK->RotateObjectN(rotTarget, WorldPt(px, py), 30.0);
			probe.log("  [c] RotateObjectN(中心=1024/1025, 30 度)の後");
			ProbeI202LogPageVars(probe, vp, "[c]");
			gSDK->ResetObject(vp);
			probe.log("  [c'] さらに ResetObject した後");
			ProbeI202LogPageVars(probe, vp, "[c']");
		}
	}

	probe.log("■ 終わり。図面は壊れたままでよい（新規の空図面で走らせる前提）");
}
