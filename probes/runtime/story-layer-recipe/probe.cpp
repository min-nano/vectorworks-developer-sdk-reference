//
//	probes/runtime/story-layer-recipe/probe.cpp
//
//	[issue #221] 2 周目（`story-layer-associate`）で、**「未使用のレベル種別を先に与えて
//	`AssociateLayerWithStory` → `SetLayerLevelType(layer, "")` で外す」**と、
//	「ストーリ＝その階・レベル＝なし」のレイヤが作れると分かった。この手順を実際の使い方で
//	回したときに崩れないかを測る。
//
//	  U1 仮の種別 1 つを使い回して、同じ階へ 2 枚・別の階へ 1 枚、この手順で結べるか
//	     （外した種別はまた空くのか——**レイヤの数だけ種別が要らないか**）
//	  U2 そのレイヤに置いた構造材のバウンド（自階 0 / 上階 1）の解決
//	  U3 階を動かしたとき、レイヤの高さと構造材が追随するか
//	  U4 デザインレイヤの並びでどこへ入るか・InsertObjectAfter が効くか
//	  U5 断面ビューポートの表示レイヤ（断面の見え方は目視）
//	  U6 レベル無しで階に結んだレイヤを、別の階へ結び直せるか
//
//	**新規の空図面で走らせる。** 階・レベル種別・レイヤ・構造材・シートを作る（消さない）。
//	保存して開き直したあとの状態は `story-layer-reopen` で読む。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// ---------------------------------------------------------------- 書式
	std::string ProbeNum(double value)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.4f", value);
		std::string text(buf);
		while (text.size() > 1 && text[text.size() - 1] == '0')
			text.erase(text.size() - 1);
		if (!text.empty() && text[text.size() - 1] == '.')
			text.erase(text.size() - 1);
		return text;
	}

	std::string ProbeInt(long long value)
	{
		return std::to_string(value);
	}

	std::string ProbeText(const TXString& value)
	{
		return std::string(static_cast<const char*>(value));
	}

	std::string ProbeFlag(bool value)
	{
		return value ? "true" : "false";
	}

	std::string ProbeObjName(MCObjectHandle hObject)
	{
		if (hObject == nil)
			return "nil";
		TXString name;
		gSDK->GetObjectName(hObject, name);
		return ProbeText(name);
	}

	// ---------------------------------------------------------------- 読み戻し
	void LogLayerState(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hLayer)
	{
		if (hLayer == nil)
		{
			probe.log(tag + ": nil");
			return;
		}
		MCObjectHandle hStory = gSDK->GetStoryOfLayer(hLayer);
		TXString levelType = gSDK->GetLayerLevelType(hLayer);
		VWFC::VWObjects::VWLayerObj layerObj(hLayer);
		probe.log(tag + ": 名前=" + ProbeObjName(hLayer) +
				  " GetStoryOfLayer=" + ProbeObjName(hStory) + " レベル種別='" +
				  ProbeText(levelType) + "' レイヤ高さ=" + ProbeNum(layerObj.GetElevation()));
	}

	// その階に属するレイヤを数えて並べる（同じ種別が 2 枚置けるかの判定に使う）。
	void LogLayersOfStory(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hStory)
	{
		if (hStory == nil)
		{
			probe.log(tag + ": 階が nil");
			return;
		}
		int count = 0;
		std::string names;
		gSDK->ForEachLayerN(
			[&](MCObjectHandle hLayer)
			{
				if (gSDK->GetStoryOfLayer(hLayer) != hStory)
					return;
				++count;
				if (!names.empty())
					names += " / ";
				names += ProbeObjName(hLayer) + "['" + ProbeText(gSDK->GetLayerLevelType(hLayer)) +
						 "' 高さ=" + ProbeNum(VWFC::VWObjects::VWLayerObj(hLayer).GetElevation()) +
						 "]";
			});
		probe.log(tag + ": 件数=" + ProbeInt(count) + " " + names);
	}

	VectorWorks::SStoryObjectData MakeStoryBound(Sint8 boundStory, const TXString& levelType,
												 double offset)
	{
		VectorWorks::SStoryObjectData data;
		data.fBound = VectorWorks::eStoryObjectBound_Story;
		data.fBoundStory = boundStory;
		data.fLayerLevelType = levelType;
		data.fOffset = offset;
		return data;
	}

	// 構造材 1 本を hLayer の上に作り、ID0 に自階・ID1 に上階のバウンドを書いて作り直す。
	// パスは **2D ポリゴン**（3D ではバウンドが黙って無視される。Findings 参照）。
	MCObjectHandle MakeBoundMember(vwprobe::Report& probe, const std::string& tag,
								   MCObjectHandle hLayer, const TXString& boundLevelType, double x0)
	{
		if (hLayer == nil)
		{
			probe.log(tag + ": レイヤが nil なので構造材は作らない");
			return nil;
		}
		gSDK->SetCurrentLayer(hLayer);
		VWFC::VWObjects::VWPolygon2DObj path(
			{VWFC::Math::VWPoint2D(x0, 0), VWFC::Math::VWPoint2D(x0 + 1000, 0)});
		MCObjectHandle hMember =
			gSDK->CreateCustomObjectPath("StructuralMember", path.GetThisObject(), nil, true);
		if (hMember == nil)
		{
			probe.fail(tag + ": CreateCustomObjectPath(StructuralMember) が nil を返した");
			return nil;
		}
		bool wroteBottom =
			gSDK->SetObjectStoryBound(hMember, 0, MakeStoryBound(0, boundLevelType, 0.0));
		bool wroteTop =
			gSDK->SetObjectStoryBound(hMember, 1, MakeStoryBound(1, boundLevelType, 0.0));
		probe.log(tag + ": SetObjectStoryBound ID0(自階)=" + ProbeFlag(wroteBottom) +
				  " ID1(上階)=" + ProbeFlag(wroteTop) +
				  " 件数=" + ProbeInt((long long)gSDK->GetObjectStoryBoundsCount(hMember)));
		gSDK->ResetObject(hMember);
		return hMember;
	}

	void LogMemberState(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hMember,
						MCObjectHandle hLayer)
	{
		if (hMember == nil)
		{
			probe.log(tag + ": nil");
			return;
		}
		double bottom = gSDK->GetObjectBoundElevation(hMember, 0);
		double top = gSDK->GetObjectBoundElevation(hMember, 1);
		WorldCube cube;
		gSDK->GetObjectCube(hMember, cube);
		VWFC::VWObjects::VWParametricObj memberObj(hMember);
		VWFC::Math::VWPoint3D insertion = memberObj.GetObjectModelPos();
		double layerElevation =
			(hLayer == nil) ? 0.0 : VWFC::VWObjects::VWLayerObj(hLayer).GetElevation();
		probe.log(tag + ": 解決Z 下端(ID0)=" + ProbeNum(bottom) + " 上端(ID1)=" + ProbeNum(top) +
				  " 差=" + ProbeNum(top - bottom) + " / 挿入点Z=" + ProbeNum(insertion.z) +
				  " 外接Z=" + ProbeNum(cube.MinZ()) + "〜" + ProbeNum(cube.MaxZ()) +
				  " / レイヤ高さ=" + ProbeNum(layerElevation));
	}

	MCObjectHandle MakeStory(vwprobe::Report& probe, const char* storyName, const char* storySuffix,
							 double elevation)
	{
		TXString name(storyName);
		TXString suffix(storySuffix);
		bool created = gSDK->CreateStory(name, suffix);
		MCObjectHandle hStory = gSDK->GetNamedObject(storyName);
		bool setElevation = (hStory != nil) && gSDK->SetStoryElevation(hStory, elevation);
		probe.log(std::string("CreateStory(") + storyName + ")=" + ProbeFlag(created) +
				  " ハンドル=" + (hStory == nil ? "nil" : "有") +
				  " SetStoryElevation=" + ProbeFlag(setElevation) +
				  " 読み戻し=" + (hStory == nil ? "-" : ProbeNum(gSDK->GetStoryElevation(hStory))));
		return hStory;
	}

	// デザインレイヤを前面から並べる（kLayerNode = 31）。
	void LogLayerOrder(vwprobe::Report& probe, const std::string& tag)
	{
		probe.log(tag + ": デザインレイヤの並び（前面から）");
		int position = 0;
		for (MCObjectHandle hObject = VWFC::VWObjects::VWDocument::GetDrawingHeaderFristMember();
			 hObject != nil; hObject = gSDK->NextObject(hObject))
		{
			if (gSDK->GetObjectTypeN(hObject) != 31 /* kLayerNode */)
				continue;
			VWFC::VWObjects::VWLayerObj layerObj(hObject);
			if (layerObj.GetLayerType() != kLayerDesign)
				continue;
			++position;
			probe.log("  " + ProbeInt(position) + ". " + ProbeObjName(hObject) +
					  " 高さ=" + ProbeNum(layerObj.GetElevation()) +
					  " 階=" + ProbeObjName(gSDK->GetStoryOfLayer(hObject)) + " レベル種別='" +
					  ProbeText(gSDK->GetLayerLevelType(hObject)) + "'");
		}
	}

	MCObjectHandle LastDesignLayer()
	{
		MCObjectHandle hLast = nil;
		for (MCObjectHandle hObject = VWFC::VWObjects::VWDocument::GetDrawingHeaderFristMember();
			 hObject != nil; hObject = gSDK->NextObject(hObject))
		{
			if (gSDK->GetObjectTypeN(hObject) != 31 /* kLayerNode */)
				continue;
			if (VWFC::VWObjects::VWLayerObj(hObject).GetLayerType() == kLayerDesign)
				hLast = hObject;
		}
		return hLast;
	}

	// 「仮の種別を与える → 結ぶ → 種別を外す」を 1 枚のレイヤに対して行う。
	// hLayer が nil なら新しく作る。戻り値はそのレイヤ。
	MCObjectHandle AttachStoryOnly(vwprobe::Report& probe, const std::string& tag,
								   const char* layerName, MCObjectHandle hLayer,
								   const TXString& tempLevelType, MCObjectHandle hStory)
	{
		if (hLayer == nil)
			hLayer = gSDK->CreateLayer(layerName, kLayerDesign);
		if (hLayer == nil)
		{
			probe.fail(tag + ": CreateLayer が nil を返した");
			return nil;
		}
		TXString tempArg(tempLevelType);
		bool setTemp = gSDK->SetLayerLevelType(hLayer, tempArg);
		bool associated = gSDK->AssociateLayerWithStory(hLayer, hStory);
		MCObjectHandle hFound = gSDK->GetLayerForStory(hStory, tempLevelType);
		TXString emptyType;
		bool cleared = gSDK->SetLayerLevelType(hLayer, emptyType);
		probe.log(tag + ": SetLayerLevelType(仮)=" + ProbeFlag(setTemp) +
				  " AssociateLayerWithStory(" + ProbeObjName(hStory) +
				  ")=" + ProbeFlag(associated) + " その時点の GetLayerForStory(階, 仮)=" +
				  ProbeObjName(hFound) + " SetLayerLevelType('')=" + ProbeFlag(cleared) +
				  " 外した後の GetLayerForStory(階, 仮)=" +
				  ProbeObjName(gSDK->GetLayerForStory(hStory, tempLevelType)));
		LogLayerState(probe, tag + " 手順の後", hLayer);
		return hLayer;
	}
} // namespace

VW_PROBE("story-layer-recipe", "レベル無しで階に結ぶ手順",
		 "仮の種別で結んで外す手順を使い回して測る")
{
	const TXString kBaseLevelType("試験天端"); // 各階にレベルを生やす種別（バウンドの宛先）
	const TXString kTempLevelType("試験仮"); // 結ぶときだけ与える種別

	// ===== U0 下ごしらえ: 階 3 つ（各階にレベル付きのレイヤ） =====
	probe.log("===== U0 下ごしらえ =====");
	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever); // 設定ダイアログ抑止

	TXString baseTypeName(kBaseLevelType);
	probe.log("CreateLayerLevelType(試験天端)=" +
			  ProbeFlag(gSDK->CreateLayerLevelType(baseTypeName)));
	TXString tempTypeName(kTempLevelType);
	probe.log("CreateLayerLevelType(試験仮)=" +
			  ProbeFlag(gSDK->CreateLayerLevelType(tempTypeName)));

	MCObjectHandle hStory1 = MakeStory(probe, "試験1階", "t1", 1000.0);
	MCObjectHandle hStory2 = MakeStory(probe, "試験2階", "t2", 4000.0);
	MCObjectHandle hStory3 = MakeStory(probe, "試験3階", "t3", 7000.0);
	if (hStory1 == nil || hStory2 == nil || hStory3 == nil)
	{
		probe.fail("階のハンドルが取れなかった（GetNamedObject）。以降は測れない");
		return;
	}
	TXString templateName("試験天端雛形");
	TXString templateLevelType(kBaseLevelType);
	short templateIndex = 0;
	probe.log("CreateStoryLevelTemplate(相対Z=-40)=" +
			  ProbeFlag(gSDK->CreateStoryLevelTemplate(templateName, 1.0, templateLevelType, -40.0,
													   0.0, templateIndex)) +
			  " index=" + ProbeInt(templateIndex));
	probe.log("AddStoryLevelFromTemplate 1階/2階/3階=" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory1, templateIndex)) + "/" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory2, templateIndex)) + "/" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory3, templateIndex)));
	MCObjectHandle hLevelLayer1 = gSDK->GetLayerForStory(hStory1, kBaseLevelType);
	MCObjectHandle hLevelLayer2 = gSDK->GetLayerForStory(hStory2, kBaseLevelType);
	LogLayerState(probe, "基準(レベル付き・1階)", hLevelLayer1);
	LogLayerState(probe, "基準(レベル付き・2階)", hLevelLayer2);

	// ===== U1 仮の種別 1 つを使い回す =====
	probe.log("===== U1 仮の種別 1 つを使い回して 3 枚結ぶ =====");
	MCObjectHandle hLayerX =
		AttachStoryOnly(probe, "X(1階)", "試験-柱X", nil, kTempLevelType, hStory1);
	MCObjectHandle hLayerY =
		AttachStoryOnly(probe, "Y(1階・2 枚目)", "試験-柱Y", nil, kTempLevelType, hStory1);
	MCObjectHandle hLayerZ =
		AttachStoryOnly(probe, "Z(2階)", "試験-柱Z", nil, kTempLevelType, hStory2);
	probe.log(
		"結ばれたか: X=" + ProbeFlag(hLayerX != nil && gSDK->GetStoryOfLayer(hLayerX) == hStory1) +
		" Y=" + ProbeFlag(hLayerY != nil && gSDK->GetStoryOfLayer(hLayerY) == hStory1) +
		" Z=" + ProbeFlag(hLayerZ != nil && gSDK->GetStoryOfLayer(hLayerZ) == hStory2));
	LogLayersOfStory(probe, "試験1階に属するレイヤ", hStory1);
	LogLayersOfStory(probe, "試験2階に属するレイヤ", hStory2);
	probe.log("GetLayerForStory(試験1階, 試験天端)=" +
			  ProbeObjName(gSDK->GetLayerForStory(hStory1, kBaseLevelType)) +
			  " GetLayerForStory(試験1階, '')=" +
			  ProbeObjName(gSDK->GetLayerForStory(hStory1, TXString())));

	// ===== U2 バウンドの解決 =====
	probe.log("===== U2 構造材のバウンド（自階 0 / 上階 1・種別は試験天端） =====");
	MCObjectHandle hMemberRef =
		MakeBoundMember(probe, "基準(1階)", hLevelLayer1, kBaseLevelType, 0.0);
	LogMemberState(probe, "基準(1階)", hMemberRef, hLevelLayer1);
	MCObjectHandle hMemberX = MakeBoundMember(probe, "X(1階)", hLayerX, kBaseLevelType, 2000.0);
	LogMemberState(probe, "X(1階)", hMemberX, hLayerX);
	MCObjectHandle hMemberY = MakeBoundMember(probe, "Y(1階)", hLayerY, kBaseLevelType, 4000.0);
	LogMemberState(probe, "Y(1階)", hMemberY, hLayerY);
	MCObjectHandle hMemberZ = MakeBoundMember(probe, "Z(2階)", hLayerZ, kBaseLevelType, 6000.0);
	LogMemberState(probe, "Z(2階)", hMemberZ, hLayerZ);

	// ===== U3 階を動かす =====
	probe.log("===== U3 階を動かす（1階 1000→1800・2階 4000→4500） =====");
	probe.log("SetStoryElevation 1階=" + ProbeFlag(gSDK->SetStoryElevation(hStory1, 1800.0)) +
			  " 2階=" + ProbeFlag(gSDK->SetStoryElevation(hStory2, 4500.0)));
	LogLayerState(probe, "基準(1階) 動かした後", hLevelLayer1);
	LogLayerState(probe, "X 動かした後", hLayerX);
	LogLayerState(probe, "Y 動かした後", hLayerY);
	LogLayerState(probe, "Z 動かした後", hLayerZ);
	probe.log("ResetObject する前の解決Z");
	LogMemberState(probe, "  X", hMemberX, hLayerX);
	const MCObjectHandle members[] = {hMemberRef, hMemberX, hMemberY, hMemberZ};
	for (MCObjectHandle hMember : members)
		if (hMember != nil)
			gSDK->ResetObject(hMember);
	LogMemberState(probe, "基準(1階) ResetObject 後", hMemberRef, hLevelLayer1);
	LogMemberState(probe, "X ResetObject 後", hMemberX, hLayerX);
	LogMemberState(probe, "Y ResetObject 後", hMemberY, hLayerY);
	LogMemberState(probe, "Z ResetObject 後", hMemberZ, hLayerZ);

	// ===== U4 並び =====
	probe.log("===== U4 デザインレイヤの並び =====");
	LogLayerOrder(probe, "並べ替える前");
	MCObjectHandle hLast = LastDesignLayer();
	if (hLayerX != nil && hLast != nil && hLast != hLayerX)
	{
		probe.log("InsertObjectAfter(X, 最背面=" + ProbeObjName(hLast) +
				  ")=" + ProbeFlag(gSDK->InsertObjectAfter(hLayerX, hLast)));
		LogLayerOrder(probe, "並べ替えた後");
	}
	if (hLevelLayer2 != nil && hLayerZ != nil && hLevelLayer2 != hLayerZ)
	{
		probe.log("InsertObjectAfter(Z, 基準(2階))=" +
				  ProbeFlag(gSDK->InsertObjectAfter(hLayerZ, hLevelLayer2)));
		LogLayerOrder(probe, "Z を基準(2階)の直後へ動かした後");
	}

	// ===== U5 断面ビューポート =====
	probe.log("===== U5 断面ビューポート =====");
	MCObjectHandle hSheet = gSDK->CreateLayer("試験シート", kLayerSheet);
	MCObjectHandle hViewport =
		(hSheet == nil)
			? nil
			: gSDK->CreateSectionViewport(WorldPt(-2000, -500), WorldPt(9000, -500),
										  WorldPt(-2000, 2000), 0.0, -5000.0, 15000.0, hSheet);
	probe.log("CreateSectionViewport=" + std::string(hViewport == nil ? "nil" : "有"));
	if (hViewport != nil)
	{
		// 何も映らないと比べられないので、どの配置でも映る倒し方にする
		// （Findings「Viewports」: 隠線消去が先・1064=false / 1065=true）。
		VWFC::VWObjects::VWViewportObj(hViewport).SetRenderType(renderFinalHiddenLine);
		probe.log("1064=false: " +
				  ProbeFlag(gSDK->SetObjectVariable(hViewport, 1064,
													TVariableBlock(Boolean(false))) != 0) +
				  " 1065=true: " +
				  ProbeFlag(gSDK->SetObjectVariable(hViewport, 1065,
													TVariableBlock(Boolean(true))) != 0));
		int visibleCount = 0;
		gSDK->ForEachLayerN(
			[&](MCObjectHandle hLayer)
			{
				if (VWFC::VWObjects::VWLayerObj(hLayer).GetLayerType() != kLayerDesign)
					return;
				if (gSDK->SetViewportLayerVisibility(hViewport, hLayer, 0))
					++visibleCount;
			});
		gSDK->ForEachClass(true,
						   [&](MCObjectHandle hClass) {
							   gSDK->SetViewportClassVisibility(
								   hViewport, gSDK->GetObjectInternalIndex(hClass), 0);
						   });
		probe.log("SetViewportLayerVisibility が true を返したデザインレイヤ=" +
				  ProbeInt(visibleCount));
		const MCObjectHandle targets[] = {hLevelLayer1, hLayerX, hLayerY, hLayerZ};
		for (MCObjectHandle hTarget : targets)
		{
			if (hTarget == nil)
				continue;
			short visibility = -99;
			Boolean read = gSDK->GetViewportLayerVisibility(hViewport, hTarget, visibility);
			probe.log("  " + ProbeObjName(hTarget) +
					  ": GetViewportLayerVisibility 戻り値=" + ProbeFlag(read != 0) +
					  " 表示=" + ProbeInt(visibility) + "（0=表示 / -1=非表示 / 2=グレー）");
		}
		gSDK->UpdateViewport(hViewport);
		probe.log("UpdateViewport を呼んだ（シート『試験シート』。材は x=0 基準・2000 X・4000 Y・6000 Z。見え方は目視）");
	}

	// ===== U6 別の階へ結び直す =====
	probe.log("===== U6 Y を 2階へ結び直す =====");
	if (hLayerY != nil)
	{
		probe.log("そのまま AssociateLayerWithStory(Y, 試験2階)=" +
				  ProbeFlag(gSDK->AssociateLayerWithStory(hLayerY, hStory2)));
		LogLayerState(probe, "Y そのまま結び直した後", hLayerY);
		if (gSDK->GetStoryOfLayer(hLayerY) != hStory2)
			AttachStoryOnly(probe, "Y(手順で 2階へ)", "", hLayerY, kTempLevelType, hStory2);
		if (hMemberY != nil)
			gSDK->ResetObject(hMemberY);
		LogMemberState(probe, "Y 結び直した後（ResetObject 後）", hMemberY, hLayerY);
		LogLayersOfStory(probe, "試験1階に属するレイヤ", hStory1);
		LogLayersOfStory(probe, "試験2階に属するレイヤ", hStory2);
	}

	probe.log("===== おわり =====");
}
