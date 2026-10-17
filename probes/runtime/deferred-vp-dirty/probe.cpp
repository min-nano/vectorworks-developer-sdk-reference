//
//	probes/runtime/deferred-vp-dirty/probe.cpp
//
//	[issue #245] 「ビューポートによっては SetDirty(true)
//を受け付けない（IsDirty が立たない）」 	（Findings「Layers and
//Stories」）を、書いた直後・ランループの後・undo イベントを閉じた 	後・VW
//へ戻った後の 4 時点で読み直す。#245 の 1 巡目で、UpdateViewport 直後に no
//だった 	IsDirty は undo イベントの終わりで yes
//に戻ると分かったので、「立たない」がその時点の 	読みだったのかを確かめる。
//
//	断面ビューポートは図面の中で最初に 1 枚だけ作る（#248: 2
//枚目以降に中身が入らない）。
//

#include "Deferred.h"

#include <string>
#include <vector>

namespace
{
	const double kVdLineY = 2500.0;
	const double kVdLineX0 = 0.0;
	const double kVdLineX1 = 3000.0;
	const double kVdPt3Off = 9000.0;

	std::string VdReadBool(MCObjectHandle h, short selector)
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

	void VdWriteBool(MCObjectHandle h, short selector, bool value)
	{
		TVariableBlock block(static_cast<Boolean>(value));
		gSDK->SetObjectVariable(h, selector, block);
	}

	std::string VdIsDirty(MCObjectHandle vp)
	{
		if (vp == nullptr)
			return "(nil)";
		VWFC::VWObjects::VWViewportObj vpObj(vp);
		return DfBool(vpObj.IsDirty());
	}

	void VdShowAllClasses(MCObjectHandle vp)
	{
		gSDK->ForEachClass(
			true, [vp](MCObjectHandle cls)
			{ gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0); });
	}

	MCObjectHandle VdMakeBox(double x0, double x1, double y0, double y1, double baseZ, double thick)
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

	// 平面ビューポート 1 枚（デザインレイヤを表示・全クラス表示・1/50）。update
	// なら更新もする。
	MCObjectHandle VdMakePlan(vwprobe::Report& probe, MCObjectHandle sheet, MCObjectHandle design,
							  bool update)
	{
		MCObjectHandle vp = gSDK->CreateViewport(sheet);
		if (vp == nullptr)
		{
			probe.fail("CreateViewport が nil");
			return nullptr;
		}
		gSDK->SetViewportLayerVisibility(vp, design, 0);
		TVariableBlock scaleVar;
		scaleVar = static_cast<Real64>(50.0);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
		VdShowAllClasses(vp);
		VWFC::VWObjects::VWViewportObj(vp).SetRenderType(renderFinalHiddenLine);
		if (update)
			gSDK->UpdateViewport(vp);
		return vp;
	}

	struct VdArm
	{
		const char* name;
		const char* what;
	};

	const VdArm kVdArms[] = {
		{"vd-sec-set", "断面・更新済み → SetDirty(true)"},
		{"vd-plan-set", "平面・更新済み → SetDirty(true)"},
		{"vd-plan-raw-set", "平面・一度も更新していない → SetDirty(true)"},
		{"vd-plan-ctrl", "平面・更新済み → 何もしない（対照）"},
		{"vd-plan-set-upd", "平面・更新済み → SetDirty(true) → もう一度 UpdateViewport"},
		{"vd-plan-ov", "平面・更新済み → ovViewportDirty(1004) へ true を書く"},
	};
} // namespace

VW_PROBE("deferred-vp-dirty", "SetDirty(true) を戻った後に読み直す", "4 時点で IsDirty を読む")
{
	std::vector<DeferredCase> cases;
	cases.push_back(
		{"d", "ビューポート 6 枚へ SetDirty(true) を掛け、IsDirty と 1004 を読む",
		 [](vwprobe::Report& probe)
		 {
			 // 断面の場面（#202 と同じ順序: デザインレイヤ → 箱 → シート → 断面）。
			 MCObjectHandle ls = gSDK->CreateLayer("vd-S", static_cast<short>(kLayerDesign));
			 MCObjectHandle ln = gSDK->CreateLayer("vd-N", static_cast<short>(kLayerDesign));
			 if (ls == nullptr || ln == nullptr)
			 {
				 probe.fail("CreateLayer が nil");
				 return;
			 }
			 gSDK->SetCurrentLayer(ls);
			 (void)VdMakeBox(500.0, 2500.0, -3000.0, -2000.0, 0.0, 3000.0);
			 gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 10000.0, 10000.0);
			 gSDK->SetCurrentLayer(ln);
			 (void)VdMakeBox(500.0, 2500.0, 7000.0, 8000.0, 5000.0, 3000.0);
			 MCObjectHandle sheet = gSDK->CreateLayer("vd-用紙", static_cast<short>(kLayerSheet));
			 if (sheet == nullptr)
			 {
				 probe.fail("シートを作れない");
				 return;
			 }
			 const WorldPt pt1(kVdLineX0, kVdLineY);
			 const WorldPt pt2(kVdLineX1, kVdLineY);
			 const WorldPt pt3((kVdLineX0 + kVdLineX1) * 0.5, kVdLineY - kVdPt3Off);
			 MCObjectHandle sec =
				 gSDK->CreateSectionViewport(pt1, pt2, pt3, 0.0, -1000.0, 9000.0, sheet);
			 if (sec != nullptr)
			 {
				 gSDK->ForEachLayerN(
					 [sec, ls, ln](MCObjectHandle layer) {
						 gSDK->SetViewportLayerVisibility(sec, layer,
														  (layer == ls || layer == ln) ? 0 : -1);
					 });
				 VdShowAllClasses(sec);
				 VWFC::VWObjects::VWViewportObj(sec).SetRenderType(renderFinalHiddenLine);
				 VdWriteBool(sec, ovSectionViewportDisplayObjectsBeforeCutPlane, true);
				 VdWriteBool(sec, ovViewportDisplayPlanar, false);
				 TVariableBlock scaleBlock(100.0);
				 gSDK->SetObjectVariable(sec, ovViewportScale, scaleBlock);
				 gSDK->UpdateViewport(sec);
			 }
			 std::vector<MCObjectHandle> vps;
			 vps.push_back(sec);
			 vps.push_back(VdMakePlan(probe, sheet, ls, true));
			 vps.push_back(VdMakePlan(probe, sheet, ls, false));
			 vps.push_back(VdMakePlan(probe, sheet, ls, true));
			 vps.push_back(VdMakePlan(probe, sheet, ls, true));
			 vps.push_back(VdMakePlan(probe, sheet, ls, true));
			 for (size_t i = 0; i < vps.size(); ++i)
			 {
				 MCObjectHandle vp = vps[i];
				 const VdArm& arm = kVdArms[i];
				 if (vp == nullptr)
					 continue;
				 const std::string before = VdIsDirty(vp);
				 VWFC::VWObjects::VWViewportObj vpObj(vp);
				 switch (i)
				 {
				 case 0:
				 case 1:
				 case 2:
					 vpObj.SetDirty(true);
					 break;
				 case 3:
					 break;
				 case 4:
					 vpObj.SetDirty(true);
					 probe.log("  " + std::string(arm.name) +
							   " SetDirty 直後 IsDirty=" + VdIsDirty(vp));
					 gSDK->UpdateViewport(vp);
					 break;
				 case 5:
					 VdWriteBool(vp, ovViewportDirty, true);
					 break;
				 }
				 probe.log("  " + std::string(arm.name) + "（" + arm.what +
						   "）掛ける前 IsDirty=" + before);
				 (void)DfSetName(probe, vp, arm.name);
			 }
			 probe.log("  過去の記述: ビューポートによっては SetDirty(true) で "
					   "IsDirty が立たない");
		 },
		 []()
		 {
			 DeferredReadings rs;
			 for (const VdArm& arm : kVdArms)
			 {
				 MCObjectHandle vp = DfFind(arm.name);
				 rs.emplace_back(std::string(arm.name) + ".IsDirty", VdIsDirty(vp));
				 rs.emplace_back(std::string(arm.name) + ".1004", VdReadBool(vp, ovViewportDirty));
			 }
			 return rs;
		 }});
	RunDeferred(probe, "deferred-vp-dirty", cases, false, true);
}
