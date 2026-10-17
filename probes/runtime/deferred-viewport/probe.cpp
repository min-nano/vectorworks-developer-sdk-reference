//
//	probes/runtime/deferred-viewport/probe.cpp
//
//	[issue #245] ビューポートと図面ラベルについて「書いた直後に同じ呼び出しの中で読んで」
//	結論した過去の知見を、T0（書いた直後）/ T1（ランループを 1.5 秒回した後）/
//	T2（VW のイベントループへ戻った後の 2 回目の実行）で測り直す。仕立ては Deferred.h。
//
//	  c1 … #202 断面の中身と 1064/1065（切断面がモデルを切っていない配置。(F,F) と (T,F) は
//	       「何も出ない」だった）。キャッシュ群の件数・中身の外接・IsDirty・1052・外接。
//	  c2 … #151 1050 が断面の向きへ入る条件（表示レイヤだけ／クラスだけ／両方／どちらも）。
//	  c3 … #200 注釈を足しても ResetObject しなければ GetObjectBounds(vp) は動かない。
//	  c4 … #177 図面ラベルの Title と 1032（空の Title のラベルが 1032 を消した件）。
//	  c5 … #179 図番の自動採番（同じ注釈の先頭が最後のラベルの図番を映す件）。
//
//	図形の作り方は過去のプローブ（section-vp-cutplane-sides / section-vp-1050 /
//	section-vp-annot-page / drawing-label-title-empty-redraw / drawing-label-auto-number /
//	drawing-label-first-in-annotation）をそのままなぞる。ビューポートと注釈の矩形には
//	名前を付けて引き直し、図面ラベルは名前の付いたビューポートの注釈群から辿る
//	（ラベルには名前を付けない——採番や連携に触らないため）。
//

#include "Deferred.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// ---------------------------------------------------------------------------
	// 定数（section-vp-cutplane-sides と同じ配置）

	const double kVpSecScale = 100.0;
	const double kVpLineY = 2500.0; // 切断面 y = 2500
	const double kVpLineX0 = 0.0;
	const double kVpLineX1 = 3000.0;
	const double kVpBoxX0 = 500.0;
	const double kVpBoxX1 = 2500.0;
	const double kVpPt3Off = 9000.0;
	const double kVpStartH = -1000.0;
	const double kVpEndH = 9000.0;
	const double kVpSplitZ = 4000.0; // S 箱（Z 0〜3000）と N 箱（Z 5000〜8000）の境

	const double kVpPlanScale = 50.0;
	const double kVpTenPtMm = 10.0 * 25.4 / 72.0; // 3.52778

	const char* const kVpLabelPio = "Drawing Label2";

	// ---------------------------------------------------------------------------
	// 小道具

	std::string VpStr(const TXString& s)
	{
		const char* p = static_cast<const char*>(s);
		return p != nullptr ? std::string(p) : std::string();
	}

	bool VpRectEmpty(const WorldRect& r)
	{
		return static_cast<double>(r.left) > static_cast<double>(r.right) ||
			   static_cast<double>(r.bottom) > static_cast<double>(r.top);
	}

	std::string VpRectText(const WorldRect& r)
	{
		if (VpRectEmpty(r))
			return "(空)";
		return "L" + DfNum(r.left, 1) + " B" + DfNum(r.bottom, 1) + " R" + DfNum(r.right, 1) +
			   " T" + DfNum(r.top, 1);
	}

	std::string VpReadBool(MCObjectHandle h, short selector)
	{
		if (h == nullptr)
			return "(nil)";
		Boolean scratch = false;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return "(読めない)";
		Boolean value = false;
		if (!block.GetBoolean(value))
			return "(型違い)";
		return value ? "T" : "F";
	}

	void VpWriteBool(MCObjectHandle h, short selector, bool value)
	{
		TVariableBlock block(static_cast<Boolean>(value));
		gSDK->SetObjectVariable(h, selector, block);
	}

	std::string VpReadRect(MCObjectHandle h, short selector)
	{
		if (h == nullptr)
			return "(nil)";
		WorldRect scratch;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return "(読めない)";
		WorldRect r;
		if (!block.GetWorldRect(r))
			return "(型違い)";
		return VpRectText(r);
	}

	std::string VpReadText(MCObjectHandle h, short selector)
	{
		if (h == nullptr)
			return "(nil)";
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(h, selector, block))
			return "(読めない)";
		TXString s;
		block.GetTXString(s);
		return "「" + VpStr(s) + "」";
	}

	std::string VpMatrixDigest(const TransformMatrix& m)
	{
		std::string out = "[";
		for (int row = 0; row < 4; ++row)
		{
			for (int col = 0; col < 3; ++col)
			{
				if (row != 0 || col != 0)
					out += " ";
				char buf[32];
				std::snprintf(buf, sizeof buf, "%.4g", m.mat[row][col]);
				out += buf;
			}
			if (row != 3)
				out += " |";
		}
		return out + "]";
	}

	bool VpMatrixIdentity(const TransformMatrix& m)
	{
		for (int row = 0; row < 4; ++row)
			for (int col = 0; col < 3; ++col)
				if (std::fabs(m.mat[row][col] - ((row == col) ? 1.0 : 0.0)) > 1e-9)
					return false;
		return true;
	}

	bool VpReadMatrix(MCObjectHandle h, short selector, TransformMatrix& out)
	{
		TVariableBlock block;
		return gSDK->GetObjectVariable(h, selector, block) && block.GetTransformMatrix(out);
	}

	MCObjectHandle VpFindLayer(const std::string& name)
	{
		MCObjectHandle found = nullptr;
		MCObjectHandle* foundPtr = &found;
		gSDK->ForEachLayerN(
			[foundPtr, name](MCObjectHandle layer)
			{
				if (*foundPtr == nullptr && DfName(layer) == name)
					*foundPtr = layer;
			});
		return found;
	}

	MCObjectHandle VpEnsureLayer(vwprobe::Report& probe, const std::string& name, short type)
	{
		MCObjectHandle layer = VpFindLayer(name);
		if (layer != nullptr)
			return layer;
		layer = gSDK->CreateLayer(TXString(name.c_str()), type);
		if (layer == nullptr)
			probe.fail("CreateLayer が nil: " + name);
		return layer;
	}

	MCObjectHandle VpMakeBox(double x0, double x1, double y0, double y1, double baseZ, double thick)
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

	// 断面の場面: S 箱（−Y 側・Z 0〜3000）/ N 箱（+Y 側・Z 5000〜8000）/ X 箱（切断面を切る）。
	// 1 レイヤに 1 箱。2 度目以降は作らずに見つけて返す。
	struct VpScene
	{
		MCObjectHandle layerS = nullptr;
		MCObjectHandle layerN = nullptr;
		MCObjectHandle layerX = nullptr;
		MCObjectHandle sheet = nullptr;
	};

	bool VpEnsureScene(vwprobe::Report& probe, VpScene& sc)
	{
		const bool fresh = VpFindLayer("dvL-S") == nullptr;
		sc.layerS = VpEnsureLayer(probe, "dvL-S", static_cast<short>(kLayerDesign));
		sc.layerN = VpEnsureLayer(probe, "dvL-N", static_cast<short>(kLayerDesign));
		sc.layerX = VpEnsureLayer(probe, "dvL-X", static_cast<short>(kLayerDesign));
		sc.sheet = VpEnsureLayer(probe, "dvL-sec", static_cast<short>(kLayerSheet));
		if (sc.layerS == nullptr || sc.layerN == nullptr || sc.layerX == nullptr ||
			sc.sheet == nullptr)
			return false;
		if (fresh)
		{
			gSDK->SetCurrentLayer(sc.layerS);
			MCObjectHandle s = VpMakeBox(kVpBoxX0, kVpBoxX1, -3000.0, -2000.0, 0.0, 3000.0);
			gSDK->SetCurrentLayer(sc.layerN);
			MCObjectHandle n = VpMakeBox(kVpBoxX0, kVpBoxX1, 7000.0, 8000.0, 5000.0, 3000.0);
			gSDK->SetCurrentLayer(sc.layerX);
			MCObjectHandle x = VpMakeBox(kVpBoxX0, kVpBoxX1, 2000.0, 3000.0, 0.0, 3000.0);
			if (s == nullptr || n == nullptr || x == nullptr)
			{
				probe.fail("箱を作れなかった（VWExtrudeObj が nil）");
				return false;
			}
			probe.log("  断面の場面を作った: S 箱 Z0〜3000（−Y 側）/ N 箱 Z5000〜8000（+Y 側）/ "
					  "X 箱（切断面 y=2500 を切る）");
		}
		return true;
	}

	void VpShowAllClasses(MCObjectHandle vp)
	{
		gSDK->ForEachClass(
			true, [vp](MCObjectHandle cls)
			{ gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0); });
	}

	// 指定したデザインレイヤだけを表示（他は非表示）・クラスは全部表示（#202 の作法）。
	void VpShowOnly(MCObjectHandle vp, const std::vector<MCObjectHandle>& showLayers)
	{
		const std::vector<MCObjectHandle>* shown = &showLayers;
		gSDK->ForEachLayerN(
			[vp, shown](MCObjectHandle layer)
			{
				bool visible = false;
				for (MCObjectHandle s : *shown)
					visible = visible || s == layer;
				gSDK->SetViewportLayerVisibility(vp, layer, visible ? 0 : -1);
			});
		VpShowAllClasses(vp);
	}

	// 断面ビューポートを 1 枚（向き +x・pt3 は −Y 側）。更新はしない。
	MCObjectHandle VpMakeSection(vwprobe::Report& probe, MCObjectHandle sheet,
								 const std::string& name)
	{
		const WorldPt pt1(kVpLineX0, kVpLineY);
		const WorldPt pt2(kVpLineX1, kVpLineY);
		const WorldPt pt3((kVpLineX0 + kVpLineX1) * 0.5, kVpLineY - kVpPt3Off);
		MCObjectHandle vp =
			gSDK->CreateSectionViewport(pt1, pt2, pt3, 0.0, kVpStartH, kVpEndH, sheet);
		if (vp == nullptr)
		{
			probe.fail("CreateSectionViewport が nil: " + name);
			return nullptr;
		}
		if (!DfSetName(probe, vp, name))
			return nullptr;
		return vp;
	}

	void VpPrepSection(MCObjectHandle vp, bool beyond, bool before)
	{
		VWFC::VWObjects::VWViewportObj vpObj(vp);
		vpObj.SetRenderType(renderFinalHiddenLine);
		VpWriteBool(vp, ovSectionViewportDisplayObjectsBeyondCutPlane, beyond);
		VpWriteBool(vp, ovSectionViewportDisplayObjectsBeforeCutPlane, before);
		VpWriteBool(vp, ovViewportDisplayPlanar, false);
		TVariableBlock scaleBlock(kVpSecScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleBlock);
	}

	// キャッシュ群（3/4/5/6/7/15）の件数を 1 行に。中身の外接（群 4 以外の和）も返す。
	std::string VpGroups(MCObjectHandle vp, WorldRect& content, bool& hasContent)
	{
		hasContent = false;
		const short groups[] = {3, 4, 5, 6, 7, 15};
		std::string line;
		for (short g : groups)
		{
			if (!line.empty())
				line += " ";
			line += std::to_string(g) + "=";
			MCObjectHandle grp = gSDK->GetViewportGroup(vp, g);
			if (grp == nullptr)
			{
				line += "nil";
				continue;
			}
			int n = 0;
			for (MCObjectHandle c = gSDK->FirstMemberObj(grp); c != nullptr;
				 c = gSDK->NextObject(c))
				++n;
			line += std::to_string(n);
			WorldRect r;
			if (!gSDK->GetObjectBounds(grp, r) || VpRectEmpty(r))
			{
				line += "(空)";
				continue;
			}
			if (g == 4)
				continue;
			if (!hasContent)
			{
				content = r;
				hasContent = true;
				continue;
			}
			if (static_cast<double>(r.left) < static_cast<double>(content.left))
				content.left = r.left;
			if (static_cast<double>(r.bottom) < static_cast<double>(content.bottom))
				content.bottom = r.bottom;
			if (static_cast<double>(r.right) > static_cast<double>(content.right))
				content.right = r.right;
			if (static_cast<double>(r.top) > static_cast<double>(content.top))
				content.top = r.top;
		}
		return line;
	}

	std::string VpSide(bool hasContent, const WorldRect& r)
	{
		if (!hasContent)
			return "none";
		const bool south = static_cast<double>(r.bottom) < kVpSplitZ;
		const bool north = static_cast<double>(r.top) > kVpSplitZ;
		if (south && north)
			return "S+N";
		if (south)
			return "S";
		if (north)
			return "N";
		return "none";
	}

	std::string VpIsDirty(MCObjectHandle vp)
	{
		if (vp == nullptr)
			return "(nil)";
		VWFC::VWObjects::VWViewportObj vpObj(vp);
		return DfBool(vpObj.IsDirty());
	}

	// 断面ビューポートを 1 枚読む（c1 / c3 共通）。
	void VpReadSection(DeferredReadings& rs, const std::string& key, const std::string& name)
	{
		MCObjectHandle vp = DfFind(name);
		if (vp == nullptr)
		{
			rs.emplace_back(key + ".vp", "(nil)");
			return;
		}
		WorldRect content;
		bool hasContent = false;
		rs.emplace_back(key + ".groups", VpGroups(vp, content, hasContent));
		rs.emplace_back(key + ".content", hasContent ? VpRectText(content) : "無し");
		rs.emplace_back(key + ".side", VpSide(hasContent, content));
		rs.emplace_back(key + ".vpBounds", DfBounds(vp));
		rs.emplace_back(key + ".1052", VpReadRect(vp, ovViewportUnscaledBoundsWithoutAnnotations));
		rs.emplace_back(key + ".IsDirty", VpIsDirty(vp));
		rs.emplace_back(key + ".1004", VpReadBool(vp, ovViewportDirty));
		rs.emplace_back(key + ".1064/1065",
						VpReadBool(vp, ovSectionViewportDisplayObjectsBeyondCutPlane) + "/" +
							VpReadBool(vp, ovSectionViewportDisplayObjectsBeforeCutPlane));
	}

	// ---------------------------------------------------------------------------
	// 図面ラベル（c4 / c5）

	MCObjectHandle VpEnsurePlanLayer(vwprobe::Report& probe)
	{
		MCObjectHandle design = VpFindLayer("dvL-plan");
		if (design != nullptr)
			return design;
		design = VpEnsureLayer(probe, "dvL-plan", static_cast<short>(kLayerDesign));
		if (design == nullptr)
			return nullptr;
		gSDK->SetLayerScaleN(design, kVpPlanScale);
		gSDK->SetCurrentLayer(design);
		gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 10000.0, 10000.0);
		return design;
	}

	// 平面のビューポート（drawing-label-* と同じ: 1/50・クラスを戻して更新）。
	MCObjectHandle VpMakePlan(vwprobe::Report& probe, MCObjectHandle sheet, MCObjectHandle design,
							  const std::string& name, const char* description, double moveX)
	{
		MCObjectHandle vp = gSDK->CreateViewport(sheet);
		if (vp == nullptr)
		{
			probe.fail("CreateViewport が nil: " + name);
			return nullptr;
		}
		gSDK->SetViewportLayerVisibility(vp, design, VWFC::VWObjects::kLayerVisibilityNormal);
		TVariableBlock scaleVar;
		scaleVar = static_cast<Real64>(kVpPlanScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
		if (description != nullptr)
		{
			TVariableBlock descVar;
			descVar = TXString(description);
			gSDK->SetObjectVariable(vp, ovViewportDescription, descVar);
		}
		VpShowAllClasses(vp);
		gSDK->UpdateViewport(vp);
		if (moveX != 0.0)
			gSDK->MoveObject(vp, static_cast<WorldCoord>(moveX), 0);
		if (!DfSetName(probe, vp, name))
			return nullptr;
		probe.log("  " + name + " を作った（名前を付けた後の 1032=" +
				  VpReadText(vp, ovViewportDescription) + "）");
		return vp;
	}

	// 注釈群の図面ラベルを並び順に集める。
	std::vector<MCObjectHandle> VpLabelsIn(MCObjectHandle vp)
	{
		std::vector<MCObjectHandle> out;
		if (vp == nullptr)
			return out;
		MCObjectHandle annot = gSDK->GetViewportGroup(vp, kViewportGroupAnnotation);
		if (annot == nullptr)
			return out;
		for (MCObjectHandle m = gSDK->FirstMemberObj(annot); m != nullptr; m = gSDK->NextObject(m))
		{
			if (gSDK->GetObjectTypeN(m) != kParametricNode)
				continue;
			VWFC::VWObjects::VWParametricObj obj(m);
			if (obj.GetParametricName() == TXString(kVpLabelPio))
				out.push_back(m);
		}
		return out;
	}

	// drawing-label-title-empty-redraw と同じ: 既定のレイアウトからテキストと線を 1 つずつ
	// 複製し、テキストに紙で 10pt を与えて差し替える。
	void VpRebuildLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		MCObjectHandle hOld = gSDK->GetCustomObjectProfileGroup(h);
		MCObjectHandle hGroup = (hOld != nullptr) ? gSDK->CreateGroup(false) : nullptr;
		if (hGroup == nullptr)
		{
			probe.log("  " + tag + ": レイアウトを組み直せない（元のレイアウトが nil）");
			return;
		}
		bool tookText = false;
		bool tookLine = false;
		for (MCObjectHandle m = gSDK->FirstMemberObj(hOld); m != nullptr; m = gSDK->NextObject(m))
		{
			const short type = gSDK->GetObjectTypeN(m);
			const bool wantText = (type == 10 && !tookText);
			const bool wantLine = (type == 2 && !tookLine);
			if (!wantText && !wantLine)
				continue;
			MCObjectHandle dup = gSDK->DuplicateObject(m);
			if (dup == nullptr)
				continue;
			gSDK->AddObjectToContainer(dup, hGroup);
			if (wantText)
			{
				tookText = true;
				const Sint32 len = gSDK->GetTextLength(dup);
				gSDK->SetTextSize(dup, 0, len > 0 ? len : 1, static_cast<WorldCoord>(kVpTenPtMm));
			}
			else
			{
				tookLine = true;
			}
		}
		if (!tookText)
			probe.log("  " + tag + ": 元のレイアウトにテキストが無かった");
		gSDK->SetCustomObjectProfileGroup(h, hGroup);
	}

	// ラベルが描いた文字（レイアウト＝プロファイルグループは除く）を数える。
	void VpCollectTexts(MCObjectHandle container, MCObjectHandle layout, int depth, int& count,
						std::string& detail)
	{
		if (container == nullptr || depth > 4)
			return;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nullptr;
			 m = gSDK->NextObject(m))
		{
			if (m == layout)
				continue;
			if (gSDK->GetObjectTypeN(m) == 10)
			{
				++count;
				WorldCoord size = 0;
				gSDK->GetTextSize(m, 0, size);
				if (count <= 3)
					detail += " [" + DfNum(static_cast<double>(size), 5) + " 「" +
							  VpStr(gSDK->GetTextChars(m)) + "」]";
			}
			VpCollectTexts(m, layout, depth + 1, count, detail);
		}
	}

	std::string VpLabelSize(MCObjectHandle h)
	{
		WorldRect r;
		if (h == nullptr || !gSDK->GetObjectBounds(h, r))
			return "(取れない)";
		if (VpRectEmpty(r))
			return "(空)";
		return "幅" + DfNum(std::fabs(r.right - r.left), 5) + " 高" +
			   DfNum(std::fabs(r.top - r.bottom), 5);
	}

	// c4: ラベル 1 本のビューポートを読む。
	void VpReadTitleArm(DeferredReadings& rs, const std::string& key, const std::string& name)
	{
		MCObjectHandle vp = DfFind(name);
		if (vp == nullptr)
		{
			rs.emplace_back(key + ".vp", "(nil)");
			return;
		}
		rs.emplace_back(key + ".1032", VpReadText(vp, ovViewportDescription));
		const std::vector<MCObjectHandle> labels = VpLabelsIn(vp);
		rs.emplace_back(key + ".labels", std::to_string(labels.size()));
		if (labels.empty())
			return;
		MCObjectHandle h = labels.front();
		VWFC::VWObjects::VWParametricObj obj(h);
		rs.emplace_back(key + ".Title", "「" + VpStr(obj.GetParamString("Title")) + "」");
		rs.emplace_back(key + ".LinkState", std::to_string(obj.GetParamLong("Link State")));
		rs.emplace_back(key + ".labelSize", VpLabelSize(h));
		int count = 0;
		std::string detail;
		VpCollectTexts(h, gSDK->GetCustomObjectProfileGroup(h), 0, count, detail);
		rs.emplace_back(key + ".drawnText", std::to_string(count) + detail);
	}

	// c5: 注釈のラベル全員の図番を並び順に（y は置いた位置。どの本かを見分けるため）。
	std::string VpDrawingNumbers(const std::string& name)
	{
		MCObjectHandle vp = DfFind(name);
		if (vp == nullptr)
			return "(nil)";
		const std::vector<MCObjectHandle> labels = VpLabelsIn(vp);
		std::string out = std::to_string(labels.size()) + " 本:";
		for (MCObjectHandle h : labels)
		{
			VWFC::VWObjects::VWParametricObj obj(h);
			const VWPoint2D pos = obj.GetPointObjectPos();
			out += " [" + VpStr(obj.GetParamString("Drawing")) + " L" +
				   std::to_string(obj.GetParamLong("Link State")) + " y" + DfNum(pos.y, 0) + "]";
		}
		return out;
	}

	MCObjectHandle VpAddNumberedLabel(vwprobe::Report& probe, MCObjectHandle vp, double y)
	{
		MCObjectHandle h = gSDK->CreateCustomObject(kVpLabelPio, WorldPt(0, y), 0.0, true);
		if (h == nullptr)
		{
			probe.fail("CreateCustomObject(Drawing Label2) が nil");
			return nullptr;
		}
		if (!gSDK->AddViewportAnnotationObject(vp, h))
			probe.log("  AddViewportAnnotationObject が false");
		gSDK->ResetObject(h);
		return h;
	}

	void VpWriteDrawing(MCObjectHandle h, const char* value)
	{
		if (h == nullptr)
			return;
		VWFC::VWObjects::VWParametricObj(h).SetParamValue("Drawing", value);
		gSDK->ResetObject(h);
	}

	// ---------------------------------------------------------------------------
	// ケース

	struct VpFlagArm
	{
		const char* name;
		bool beyond;
		bool before;
	};

	const VpFlagArm kVpFlagArms[] = {
		{"dv1-FF", false, false},
		{"dv1-TF", true, false},
		{"dv1-FT", false, true},
		{"dv1-TT", true, true},
	};

	DeferredCase VpCase1()
	{
		DeferredCase c;
		c.id = "c1";
		c.what = "#202 切っていない断面: (1064,1065) 4 通りのキャッシュ群・中身・IsDirty・1052";
		c.setup = [](vwprobe::Report& probe)
		{
			VpScene sc;
			if (!VpEnsureScene(probe, sc))
				return;
			const std::vector<MCObjectHandle> showSN = {sc.layerS, sc.layerN};
			for (const VpFlagArm& arm : kVpFlagArms)
			{
				MCObjectHandle vp = VpMakeSection(probe, sc.sheet, arm.name);
				if (vp == nullptr)
					continue;
				VpShowOnly(vp, showSN);
				VpPrepSection(vp, arm.beyond, arm.before);
				gSDK->UpdateViewport(vp);
			}
			probe.log("  過去の実測（T0 相当）: FF/TF は群 3/5/6/7/15 が全部 nil（何も出ない）、"
					  "FT は 5=9（S+N）、TT は 5=7（N＝奥だけ）");
		};
		c.measure = []()
		{
			DeferredReadings rs;
			for (const VpFlagArm& arm : kVpFlagArms)
				VpReadSection(rs, std::string(arm.name).substr(4), arm.name);
			return rs;
		};
		return c;
	}

	struct VpVisArm
	{
		const char* name;
		bool layers;
		bool classes;
	};

	const VpVisArm kVpVisArms[] = {
		{"dv2-none", false, false},
		{"dv2-L", true, false},
		{"dv2-C", false, true},
		{"dv2-LC", true, true},
	};

	DeferredCase VpCase2()
	{
		DeferredCase c;
		c.id = "c2";
		c.what = "#151 1050 が断面の向きへ入る条件（表示レイヤ／クラス × 2）";
		c.setup = [](vwprobe::Report& probe)
		{
			VpScene sc;
			if (!VpEnsureScene(probe, sc))
				return;
			std::vector<MCObjectHandle> designLayers;
			std::vector<MCObjectHandle>* dl = &designLayers;
			gSDK->ForEachLayerN(
				[dl](MCObjectHandle layer)
				{
					TVariableBlock block;
					Sint16 layerType = 0;
					if (gSDK->GetObjectVariable(layer, 154 /* ovLayerType */, block))
						block.GetSint16(layerType);
					if (layerType == 1)
						dl->push_back(layer);
				});
			probe.log("  デザインレイヤ " + std::to_string(designLayers.size()) + " 枚");
			for (const VpVisArm& arm : kVpVisArms)
			{
				MCObjectHandle vp = VpMakeSection(probe, sc.sheet, arm.name);
				if (vp == nullptr)
					continue;
				// section-vp-1050 と同じ: 倒すときは全デザインレイヤ／全クラスを表示。
				// 倒さない側には触らない。レンダ（隠線消去）と 1064=true はどの枚も同じ。
				if (arm.layers)
					for (MCObjectHandle layer : designLayers)
						gSDK->SetViewportLayerVisibility(vp, layer, 0);
				if (arm.classes)
					VpShowAllClasses(vp);
				{
					VWFC::VWObjects::VWViewportObj vpObj(vp);
					vpObj.SetRenderType(renderFinalHiddenLine);
				}
				VpWriteBool(vp, ovSectionViewportDisplayObjectsBeyondCutPlane, true);
				gSDK->UpdateViewport(vp);
			}
			probe.log("  過去の実測（T0 相当）: 両方を表示にした枚（LC）だけ 1050 が 1055 と一致、"
					  "他は単位行列");
		};
		c.measure = []()
		{
			DeferredReadings rs;
			for (const VpVisArm& arm : kVpVisArms)
			{
				const std::string key = std::string(arm.name).substr(4);
				MCObjectHandle vp = DfFind(arm.name);
				if (vp == nullptr)
				{
					rs.emplace_back(key + ".vp", "(nil)");
					continue;
				}
				TransformMatrix m1050;
				TransformMatrix m1055;
				const bool got1050 = VpReadMatrix(vp, ovViewportViewMatrix, m1050);
				const bool got1055 = VpReadMatrix(vp, 1055, m1055);
				rs.emplace_back(key + ".1050",
								got1050 ? VpMatrixDigest(m1050) : std::string("(読めない)"));
				rs.emplace_back(key + ".1050id",
								got1050 ? DfBool(VpMatrixIdentity(m1050)) : std::string("-"));
				rs.emplace_back(key + ".1050=1055",
								(got1050 && got1055)
									? DfBool(VpMatrixDigest(m1050) == VpMatrixDigest(m1055))
									: std::string("-"));
				WorldRect content;
				bool hasContent = false;
				rs.emplace_back(key + ".groups", VpGroups(vp, content, hasContent));
				rs.emplace_back(key + ".IsDirty", VpIsDirty(vp));
			}
			return rs;
		};
		return c;
	}

	struct VpAnnotArm
	{
		const char* name;
		const char* mark;
		bool content; // 表示レイヤを入れて中身を描かせるか
		bool reset;	  // 注釈を足した後に ResetObject(vp) するか
	};

	const VpAnnotArm kVpAnnotArms[] = {
		{"dv3-noR", "dv3-noR-m", true, false},
		{"dv3-R", "dv3-R-m", true, true},
		{"dv3-E", "dv3-E-m", false, false},
	};

	DeferredCase VpCase3()
	{
		DeferredCase c;
		c.id = "c3";
		c.what = "#200 注釈に遠くの矩形を足して ResetObject しないと、外接は動かないか";
		c.setup = [](vwprobe::Report& probe)
		{
			VpScene sc;
			if (!VpEnsureScene(probe, sc))
				return;
			const std::vector<MCObjectHandle> showSN = {sc.layerS, sc.layerN};
			const std::vector<MCObjectHandle> showNone;
			for (const VpAnnotArm& arm : kVpAnnotArms)
			{
				MCObjectHandle vp = VpMakeSection(probe, sc.sheet, arm.name);
				if (vp == nullptr)
					continue;
				VpShowOnly(vp, arm.content ? showSN : showNone);
				VpPrepSection(vp, false, true); // (F,T) ＝ 両側が出る
				gSDK->UpdateViewport(vp);
				probe.log("  " + std::string(arm.name) + " 更新後・注釈を足す前の外接 " +
						  DfBounds(vp));
				WorldRect want;
				want.left = 100000.0;
				want.right = 101000.0;
				want.bottom = 200000.0;
				want.top = 201000.0;
				MCObjectHandle mark = gSDK->CreateRectangle(want);
				if (mark == nullptr || !gSDK->AddViewportAnnotationObject(vp, mark))
				{
					probe.fail(std::string(arm.name) + ": 注釈へ矩形を置けなかった");
					continue;
				}
				(void)DfSetName(probe, mark, arm.mark);
				if (arm.reset)
					gSDK->ResetObject(vp);
			}
			probe.log(
				"  過去の実測（T0 相当）: ResetObject 前は外接が 1mm も動かない／後は注釈が入る");
		};
		c.measure = []()
		{
			DeferredReadings rs;
			for (const VpAnnotArm& arm : kVpAnnotArms)
			{
				const std::string key = std::string(arm.name).substr(4);
				MCObjectHandle vp = DfFind(arm.name);
				if (vp == nullptr)
				{
					rs.emplace_back(key + ".vp", "(nil)");
					continue;
				}
				rs.emplace_back(key + ".vpBounds", DfBounds(vp));
				rs.emplace_back(key + ".annotGroup",
								DfBounds(gSDK->GetViewportGroup(vp, kViewportGroupAnnotation)));
				rs.emplace_back(key + ".mark", DfBounds(DfFind(arm.mark)));
				rs.emplace_back(key + ".1052",
								VpReadRect(vp, ovViewportUnscaledBoundsWithoutAnnotations));
				rs.emplace_back(key + ".IsDirty", VpIsDirty(vp));
			}
			return rs;
		};
		return c;
	}

	struct VpTitleArm
	{
		const char* name;
		const char* preset;		// 置く前に 1032 へ入れる値（nullptr なら入れない）
		const char* labelTitle; // ラベルの Title へ書く値（nullptr なら書かない）
		double moveX;
	};

	const VpTitleArm kVpTitleArms[] = {
		{"dv4-pre", "VP の図面タイトル", nullptr, 0.0},
		{"dv4-E", nullptr, nullptr, 300.0},
		{"dv4-T", nullptr, "調査用の図面タイトル", 600.0},
	};

	DeferredCase VpCase4()
	{
		DeferredCase c;
		c.id = "c4";
		c.what = "#177 図面ラベルの Title と 1032（空の Title が 1032 を消した件）・描いた文字";
		c.setup = [](vwprobe::Report& probe)
		{
			gSDK->DefineCustomObject(kVpLabelPio, kCustomObjectPrefNever);
			MCObjectHandle design = VpEnsurePlanLayer(probe);
			MCObjectHandle sheet = VpEnsureLayer(probe, "dvL-s4", static_cast<short>(kLayerSheet));
			if (design == nullptr || sheet == nullptr)
				return;
			gSDK->SetCurrentLayer(sheet);
			for (const VpTitleArm& arm : kVpTitleArms)
			{
				MCObjectHandle vp =
					VpMakePlan(probe, sheet, design, arm.name, arm.preset, arm.moveX);
				if (vp == nullptr)
					continue;
				MCObjectHandle h = gSDK->CreateCustomObject(kVpLabelPio, WorldPt(0, 0), 0.0, false);
				if (h == nullptr)
				{
					probe.fail(std::string(arm.name) + ": CreateCustomObject が nil");
					continue;
				}
				VpRebuildLayout(probe, arm.name, h);
				if (!gSDK->AddViewportAnnotationObject(vp, h))
					probe.log("  " + std::string(arm.name) +
							  ": AddViewportAnnotationObject が false");
				VWFC::VWObjects::VWParametricObj(h).SetPointObjectPos(VWPoint2D(0, 0));
				if (arm.labelTitle != nullptr)
					VWFC::VWObjects::VWParametricObj(h).SetParamValue("Title", arm.labelTitle);
				gSDK->ResetObject(h);
			}
			probe.log("  過去の実測（T0 相当）: pre は Title 空・1032 が空に潰れる／E は両方空・"
					  "文字無し（高 0）／T は Title=1032=調査用の図面タイトル・文字 176.38889");
		};
		c.measure = []()
		{
			DeferredReadings rs;
			for (const VpTitleArm& arm : kVpTitleArms)
				VpReadTitleArm(rs, std::string(arm.name).substr(4), arm.name);
			return rs;
		};
		return c;
	}

	DeferredCase VpCase5()
	{
		DeferredCase c;
		c.id = "c5";
		c.what = "#179 図番の自動採番（同じ注釈に 3 本／書いた値の乗っ取り／1 枚 1 本）";
		c.setup = [](vwprobe::Report& probe)
		{
			gSDK->DefineCustomObject(kVpLabelPio, kCustomObjectPrefNever);
			MCObjectHandle design = VpEnsurePlanLayer(probe);
			if (design == nullptr)
				return;

			// P: 1 つの注釈へ 3 本（書かない）。過去の実測 [1] → [2,2] → [1,2,1]。
			MCObjectHandle sheetP =
				VpEnsureLayer(probe, "dvL-s5P", static_cast<short>(kLayerSheet));
			if (sheetP != nullptr)
			{
				gSDK->SetCurrentLayer(sheetP);
				MCObjectHandle vp = VpMakePlan(probe, sheetP, design, "dv5-P", nullptr, 0.0);
				if (vp != nullptr)
					for (int i = 0; i < 3; ++i)
					{
						(void)VpAddNumberedLabel(probe, vp, -2000.0 * i);
						probe.log("  P " + std::to_string(i + 1) +
								  " 本目の直後: " + VpDrawingNumbers("dv5-P"));
					}
			}

			// S: 3 本入れて S-1〜S-3 を書く。過去の実測 [S-3, S-2, S-3]。
			MCObjectHandle sheetS =
				VpEnsureLayer(probe, "dvL-s5S", static_cast<short>(kLayerSheet));
			if (sheetS != nullptr)
			{
				gSDK->SetCurrentLayer(sheetS);
				MCObjectHandle vp = VpMakePlan(probe, sheetS, design, "dv5-S", nullptr, 0.0);
				if (vp != nullptr)
				{
					MCObjectHandle hs[3] = {nullptr, nullptr, nullptr};
					for (int i = 0; i < 3; ++i)
						hs[i] = VpAddNumberedLabel(probe, vp, -2000.0 * i);
					VpWriteDrawing(hs[0], "S-1");
					VpWriteDrawing(hs[1], "S-2");
					VpWriteDrawing(hs[2], "S-3");
					probe.log("  S 3 本へ書いた直後: " + VpDrawingNumbers("dv5-S"));
				}
			}

			// U: 2 枚のビューポートへ 1 本ずつ。過去の実測 [1] / [2]。
			MCObjectHandle sheetU =
				VpEnsureLayer(probe, "dvL-s5U", static_cast<short>(kLayerSheet));
			if (sheetU != nullptr)
			{
				gSDK->SetCurrentLayer(sheetU);
				MCObjectHandle vp1 = VpMakePlan(probe, sheetU, design, "dv5-U1", nullptr, 0.0);
				MCObjectHandle vp2 = VpMakePlan(probe, sheetU, design, "dv5-U2", nullptr, 300.0);
				if (vp1 != nullptr)
					(void)VpAddNumberedLabel(probe, vp1, 0.0);
				if (vp2 != nullptr)
					(void)VpAddNumberedLabel(probe, vp2, -2000.0);
			}
		};
		c.measure = []()
		{
			DeferredReadings rs;
			rs.emplace_back("P", VpDrawingNumbers("dv5-P"));
			rs.emplace_back("S", VpDrawingNumbers("dv5-S"));
			rs.emplace_back("U1", VpDrawingNumbers("dv5-U1"));
			rs.emplace_back("U2", VpDrawingNumbers("dv5-U2"));
			return rs;
		};
		return c;
	}
} // namespace

VW_PROBE("deferred-viewport", "ビューポート系の遅延の測り直し",
		 "断面・注釈外接・図面ラベルを T0/T1/T2 で")
{
	const std::vector<DeferredCase> cases = {VpCase1(), VpCase2(), VpCase3(), VpCase4(), VpCase5()};
	RunDeferred(probe, "deferred-viewport", cases);
}
