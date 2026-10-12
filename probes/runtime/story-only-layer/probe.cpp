//
//	probes/runtime/story-only-layer/probe.cpp
//
//	[issue #221] ストーリレベルを持たないデザインレイヤを、階（ストーリ）にだけ
//	結び付けられるか（ISDK::AssociateLayerWithStory）。結べたとき、
//	  1. 読み戻し（GetStoryOfLayer / GetLayerLevelType）が実態と一致するか
//	  2. レイヤの高さは何になるか・階を動かしたら追随するか
//	  3. その上に置いた構造材のストーリバウンド（storyOffset 0 / 1）が
//	     レベル付きのレイヤと同じ絶対Zへ解決されるか
//	  4. デザインレイヤの重ね順でどこへ並ぶか・InsertObjectAfter が効くか
//	  5. ビューポートの表示レイヤで差が出るか
//	を、レベル付きのレイヤ（レベルテンプレート経由）と並べて 1 回の実行で測る。
//
//	**新規の空図面で走らせる。** 階・レベル種別・レイヤ・構造材を作る（消さない）。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

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
	// レイヤ 1 枚の状態を 1 行に畳む。**階との結び付きとレベル種別とレイヤ高さ**が要点。
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
				  " GetStoryOfLayer=" + ProbeObjName(hStory) +
				  "(索引=" + ProbeInt(hStory == nil ? 0 : gSDK->GetObjectInternalIndex(hStory)) +
				  ") レベル種別='" + ProbeText(levelType) +
				  "' レイヤ高さ=" + ProbeNum(layerObj.GetElevation()) +
				  " VWLayerObj::GetStory=" + ProbeInt(layerObj.GetStory()));
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

	// 構造材 1 本を hLayer の上に作り、ID 0 に自階・ID 1 に上階のバウンドを書いて
	// ResetObject する。パスは **2D ポリゴン**（3D ポリゴンではバウンドが黙って無視される。
	// Findings「構造材のパスは 2D で渡す」）。
	MCObjectHandle MakeBoundMember(vwprobe::Report& probe, const std::string& tag,
								   MCObjectHandle hLayer, const TXString& levelType, double x0)
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
		bool wroteBottom = gSDK->SetObjectStoryBound(hMember, 0, MakeStoryBound(0, levelType, 0.0));
		bool wroteTop = gSDK->SetObjectStoryBound(hMember, 1, MakeStoryBound(1, levelType, 0.0));
		probe.log(tag + ": SetObjectStoryBound ID0(自階)=" + ProbeFlag(wroteBottom) +
				  " ID1(上階)=" + ProbeFlag(wroteTop) +
				  " 件数=" + ProbeInt((long long)gSDK->GetObjectStoryBoundsCount(hMember)));
		gSDK->ResetObject(hMember);
		return hMember;
	}

	// 構造材 1 本の「解決された絶対Z」と「実体の幾何」を 1 行に畳む。
	void LogMemberState(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hMember)
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
		probe.log(tag + ": 解決Z 下端(ID0)=" + ProbeNum(bottom) + " 上端(ID1)=" + ProbeNum(top) +
				  " 差=" + ProbeNum(top - bottom) + " / 挿入点Z=" + ProbeNum(insertion.z) +
				  " 外接Z=" + ProbeNum(cube.MinZ()) + "〜" + ProbeNum(cube.MaxZ()));
	}

	// いまのデザインレイヤの並び（図面のオブジェクト列の順）を全部出す。
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
			MCObjectHandle hStory = gSDK->GetStoryOfLayer(hObject);
			TXString levelType = gSDK->GetLayerLevelType(hObject);
			probe.log("  " + ProbeInt(position) + ". " + ProbeObjName(hObject) +
					  " 高さ=" + ProbeNum(layerObj.GetElevation()) + " 階=" + ProbeObjName(hStory) +
					  " レベル種別='" + ProbeText(levelType) + "'");
		}
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
				  " ハンドル=" + (hStory == nil ? "nil" : "有") + " SetStoryElevation(" +
				  ProbeNum(elevation) + ")=" + ProbeFlag(setElevation) +
				  " 読み戻し=" + (hStory == nil ? "-" : ProbeNum(gSDK->GetStoryElevation(hStory))));
		return hStory;
	}
} // namespace

VW_PROBE("story-only-layer", "レベル無しのレイヤを階に結ぶ",
		 "AssociateLayerWithStory だけでレイヤを階に結び、バウンドの解決まで測る")
{
	const TXString kLevelType("試験天端");
	const TXString kSpareLevelType("試験柱");

	// ===== S0 下ごしらえ: 階 3 つと、レベル付きのレイヤ（比較の基準） =====
	probe.log("===== S0 下ごしらえ =====");
	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever); // 設定ダイアログ抑止

	TXString levelTypeName(kLevelType);
	probe.log("CreateLayerLevelType(試験天端)=" +
			  ProbeFlag(gSDK->CreateLayerLevelType(levelTypeName)));

	MCObjectHandle hStory1 = MakeStory(probe, "試験1階", "t1", 1000.0);
	MCObjectHandle hStory2 = MakeStory(probe, "試験2階", "t2", 4000.0);
	MCObjectHandle hStory3 = MakeStory(probe, "試験3階", "t3", 7000.0);
	if (hStory1 == nil || hStory2 == nil || hStory3 == nil)
	{
		probe.fail("階のハンドルが取れなかった（GetNamedObject）。以降は測れない");
		return;
	}

	// レベルテンプレート経由でレベル（＝レイヤ）を生やす。これが「レベル付きのレイヤ」で、
	// 以降の比較の基準になる（Findings「階（ストーリ）を SDK から作る」）。
	TXString templateName("試験天端雛形");
	TXString templateLevelType(kLevelType);
	short templateIndex = 0;
	bool madeTemplate = gSDK->CreateStoryLevelTemplate(templateName, 1.0, templateLevelType, -40.0,
													   0.0, templateIndex);
	probe.log("CreateStoryLevelTemplate(相対Z=-40)=" + ProbeFlag(madeTemplate) +
			  " index=" + ProbeInt(templateIndex));
	probe.log("AddStoryLevelFromTemplate(試験1階)=" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory1, templateIndex)));
	probe.log("AddStoryLevelFromTemplate(試験2階)=" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory2, templateIndex)));

	MCObjectHandle hLevelLayer1 = gSDK->GetLayerForStory(hStory1, kLevelType);
	MCObjectHandle hLevelLayer2 = gSDK->GetLayerForStory(hStory2, kLevelType);
	LogLayerState(probe, "基準(レベル付き・1階)", hLevelLayer1);
	LogLayerState(probe, "基準(レベル付き・2階)", hLevelLayer2);
	probe.log("階内相対Z: 1階=" + ProbeNum(gSDK->GetStoryLevelElevation(hStory1, kLevelType)) +
			  " 2階=" + ProbeNum(gSDK->GetStoryLevelElevation(hStory2, kLevelType)));
	probe.log("GetStoryAbove(試験1階)=" + ProbeObjName(gSDK->GetStoryAbove(hStory1)) +
			  " GetStoryBelow(試験2階)=" + ProbeObjName(gSDK->GetStoryBelow(hStory2)) +
			  " GetNumStories=" + ProbeInt(gSDK->GetNumStories()));

	// ===== S1 素のデザインレイヤを階にだけ結ぶ（調べたいこと 1・2） =====
	probe.log("===== S1 素のレイヤを階へ結ぶ =====");
	MCObjectHandle hPlainA = gSDK->CreateLayer("試験-柱A", kLayerDesign);
	if (hPlainA == nil)
	{
		probe.fail("CreateLayer(試験-柱A) が nil を返した");
		return;
	}
	LogLayerState(probe, "A 結ぶ前", hPlainA);
	bool associatedA = gSDK->AssociateLayerWithStory(hPlainA, hStory1);
	probe.log("AssociateLayerWithStory(A, 試験1階)=" + ProbeFlag(associatedA));
	LogLayerState(probe, "A 結んだ後", hPlainA);

	// ===== S2 同じ階へ「レベル無し」を 2 枚目（vs.py は同じレベル種別なら失敗すると言う） =====
	probe.log("===== S2 同じ階へ 2 枚目のレベル無しレイヤ =====");
	MCObjectHandle hPlainB = gSDK->CreateLayer("試験-柱B", kLayerDesign);
	bool associatedB = (hPlainB != nil) && gSDK->AssociateLayerWithStory(hPlainB, hStory1);
	probe.log("AssociateLayerWithStory(B, 試験1階)=" + ProbeFlag(associatedB));
	LogLayerState(probe, "B 結んだ後", hPlainB);
	LogLayerState(probe, "A 再読み（B を結んだ後）", hPlainA);

	// ===== S3 時機: レベルを 1 つも持たない階へ結び、あとからレベルを生やす =====
	probe.log("===== S3 レベルの無い階へ結び、あとからレベルを生やす =====");
	MCObjectHandle hPlainC = gSDK->CreateLayer("試験-柱C", kLayerDesign);
	bool associatedC = (hPlainC != nil) && gSDK->AssociateLayerWithStory(hPlainC, hStory3);
	probe.log("AssociateLayerWithStory(C, 試験3階＝レベル無し)=" + ProbeFlag(associatedC));
	LogLayerState(probe, "C 結んだ直後", hPlainC);
	probe.log("AddStoryLevelFromTemplate(試験3階)=" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory3, templateIndex)));
	LogLayerState(probe, "C レベルが生えた後", hPlainC);
	LogLayerState(probe, "試験3階のレベル付きレイヤ", gSDK->GetLayerForStory(hStory3, kLevelType));

	// ===== S4 バウンドを解かせる（オブジェクトを作らずに。調べたいこと 3） =====
	probe.log("===== S4 GetStoryObjectDataBoundHeight（入れ物＝レイヤ）=====");
	struct ProbeContainer
	{
		const char* tag;
		MCObjectHandle handle;
	};
	const ProbeContainer containers[] = {{"基準(レベル付き・1階)", hLevelLayer1},
										 {"A(レベル無し・1階)", hPlainA},
										 {"B(レベル無し・1階)", hPlainB},
										 {"C(レベル無し・3階)", hPlainC}};
	for (const ProbeContainer& container : containers)
	{
		if (container.handle == nil)
		{
			probe.log(std::string(container.tag) + ": nil");
			continue;
		}
		double self = gSDK->GetStoryObjectDataBoundHeight(MakeStoryBound(0, kLevelType, 0.0),
														  container.handle);
		double above = gSDK->GetStoryObjectDataBoundHeight(MakeStoryBound(1, kLevelType, 0.0),
														   container.handle);
		double below = gSDK->GetStoryObjectDataBoundHeight(MakeStoryBound(-1, kLevelType, 0.0),
														   container.handle);
		probe.log(std::string(container.tag) + ": 自階(0)=" + ProbeNum(self) +
				  " 上階(1)=" + ProbeNum(above) + " 下階(-1)=" + ProbeNum(below));
	}

	// ===== S5 実際に構造材を置いて、両端のバウンドを解かせる =====
	probe.log("===== S5 構造材を置く（ID0=自階・ID1=上階の横架材天端相当）=====");
	MCObjectHandle hMemberRef =
		MakeBoundMember(probe, "基準(レベル付き)", hLevelLayer1, kLevelType, 0.0);
	LogMemberState(probe, "基準(レベル付き)", hMemberRef);
	MCObjectHandle hMemberA = MakeBoundMember(probe, "A(レベル無し)", hPlainA, kLevelType, 2000.0);
	LogMemberState(probe, "A(レベル無し)", hMemberA);
	MCObjectHandle hMemberB = MakeBoundMember(probe, "B(レベル無し)", hPlainB, kLevelType, 4000.0);
	LogMemberState(probe, "B(レベル無し)", hMemberB);

	// ===== S6 階を動かしたら追随するか（調べたいこと 2・3 の後半） =====
	probe.log("===== S6 SetStoryElevation(試験1階, 1800) で階を動かす =====");
	probe.log("SetStoryElevation=" + ProbeFlag(gSDK->SetStoryElevation(hStory1, 1800.0)) +
			  " 読み戻し=" + ProbeNum(gSDK->GetStoryElevation(hStory1)) +
			  " 階内相対Z=" + ProbeNum(gSDK->GetStoryLevelElevation(hStory1, kLevelType)));
	LogLayerState(probe, "基準(レベル付き) 階を動かした後", hLevelLayer1);
	LogLayerState(probe, "A 階を動かした後", hPlainA);
	LogLayerState(probe, "B 階を動かした後", hPlainB);
	LogMemberState(probe, "基準 階を動かした後", hMemberRef);
	LogMemberState(probe, "A 階を動かした後", hMemberA);
	probe.log("作り直してから読み直す（ResetObject）");
	if (hMemberRef != nil)
		gSDK->ResetObject(hMemberRef);
	if (hMemberA != nil)
		gSDK->ResetObject(hMemberA);
	LogMemberState(probe, "基準 ResetObject 後", hMemberRef);
	LogMemberState(probe, "A ResetObject 後", hMemberA);

	// ===== S7 重ね順（調べたいこと 4） =====
	probe.log("===== S7 レイヤの重ね順 =====");
	LogLayerOrder(probe, "並べ替える前");
	MCObjectHandle hLastDesignLayer = nil;
	for (MCObjectHandle hObject = VWFC::VWObjects::VWDocument::GetDrawingHeaderFristMember();
		 hObject != nil; hObject = gSDK->NextObject(hObject))
	{
		if (gSDK->GetObjectTypeN(hObject) != 31 /* kLayerNode */)
			continue;
		if (VWFC::VWObjects::VWLayerObj(hObject).GetLayerType() == kLayerDesign)
			hLastDesignLayer = hObject;
	}
	if (hPlainA != nil && hLastDesignLayer != nil && hLastDesignLayer != hPlainA)
	{
		probe.log("InsertObjectAfter(A, 最背面のデザインレイヤ=" + ProbeObjName(hLastDesignLayer) +
				  ")=" + ProbeFlag(gSDK->InsertObjectAfter(hPlainA, hLastDesignLayer)));
		LogLayerOrder(probe, "並べ替えた後");
	}
	else
	{
		probe.log("A が既に最背面か、最背面が取れなかったので並べ替えは試さない");
	}

	// ===== S8 レベル種別を付け替えられるか（結べない場合の代替手段の当否） =====
	probe.log("===== S8 SetLayerLevelType で後からレベルを与える =====");
	MCObjectHandle hPlainD = gSDK->CreateLayer("試験-柱D", kLayerDesign);
	bool associatedD = (hPlainD != nil) && gSDK->AssociateLayerWithStory(hPlainD, hStory2);
	probe.log("AssociateLayerWithStory(D, 試験2階)=" + ProbeFlag(associatedD));
	LogLayerState(probe, "D 結んだ後", hPlainD);
	if (hPlainD != nil)
	{
		// ① 既に同じ階の別レイヤが使っているレベル種別（＝レベルの共有）を試す
		TXString usedLevelType(kLevelType);
		probe.log("SetLayerLevelType(D, 試験天端＝2階で既に使われている)=" +
				  ProbeFlag(gSDK->SetLayerLevelType(hPlainD, usedLevelType)));
		LogLayerState(probe, "D ①の後", hPlainD);
		// ② 誰も使っていないレベル種別を試す
		TXString spareName(kSpareLevelType);
		probe.log("CreateLayerLevelType(試験柱)=" +
				  ProbeFlag(gSDK->CreateLayerLevelType(spareName)));
		TXString spareLevelType(kSpareLevelType);
		probe.log("SetLayerLevelType(D, 試験柱＝未使用)=" +
				  ProbeFlag(gSDK->SetLayerLevelType(hPlainD, spareLevelType)));
		LogLayerState(probe, "D ②の後", hPlainD);
		probe.log("GetLayerForStory(試験2階, 試験柱)=" +
				  ProbeObjName(gSDK->GetLayerForStory(hStory2, kSpareLevelType)) +
				  " 階内相対Z=" + ProbeNum(gSDK->GetStoryLevelElevation(hStory2, kSpareLevelType)));
	}

	// ===== S9 ビューポート（調べたいこと 5 の機械で測れる分） =====
	probe.log("===== S9 断面ビューポートの表示レイヤ =====");
	MCObjectHandle hSheet = gSDK->CreateLayer("試験シート", kLayerSheet);
	MCObjectHandle hViewport = nil;
	if (hSheet == nil)
	{
		probe.log("シートレイヤを作れなかったので断面ビューポートは試さない");
	}
	else
	{
		hViewport =
			gSDK->CreateSectionViewport(WorldPt(-2000, -500), WorldPt(8000, -500),
										WorldPt(-2000, 2000), 0.0, -5000.0, 15000.0, hSheet);
		probe.log("CreateSectionViewport=" + std::string(hViewport == nil ? "nil" : "有"));
	}
	if (hViewport != nil)
	{
		// 全デザインレイヤと全クラスを表示へ倒してから更新する（Findings「SDK で作った
		// 断面ビューポート…」）。**レイヤだけでは足りない。**
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
		const ProbeContainer visibilityTargets[] = {{"基準(レベル付き)", hLevelLayer1},
													{"A(レベル無し)", hPlainA},
													{"B(レベル無し)", hPlainB},
													{"C(レベル無し)", hPlainC}};
		for (const ProbeContainer& target : visibilityTargets)
		{
			if (target.handle == nil)
				continue;
			short visibility = -99;
			Boolean read = gSDK->GetViewportLayerVisibility(hViewport, target.handle, visibility);
			probe.log(std::string(target.tag) +
					  ": GetViewportLayerVisibility 戻り値=" + ProbeFlag(read != 0) +
					  " 表示=" + ProbeInt(visibility) + "（0=表示 / -1=非表示 / 2=グレー）");
		}
		gSDK->UpdateViewport(hViewport);
		probe.log("UpdateViewport を呼んだ。シートレイヤ『試験シート』に断面が描かれる"
				  "——A / B の構造材が基準と同じように映っているかは**目視**で確かめる");
	}

	// ===== S10 後片付けできるか（レベル無しで階に結んだレイヤを消す） =====
	probe.log("===== S10 レベル無しで階に結んだレイヤを消せるか =====");
	if (hPlainC != nil)
	{
		gSDK->DeleteObject(hPlainC, false);
		probe.log(
			"DeleteObject(C) の後: GetNamedObject(試験-柱C)=" +
			std::string(gSDK->GetNamedObject("試験-柱C") == nil ? "nil（消えた）" : "まだ有る") +
			" / 試験3階=" + ProbeObjName(gSDK->GetNamedObject("試験3階")) +
			" / 試験3階のレベル付きレイヤ=" +
			ProbeObjName(gSDK->GetLayerForStory(hStory3, kLevelType)));
	}

	probe.log("===== おわり =====");
	probe.log("目視で確かめてほしいこと: (1) 組織→デザインレイヤ で 試験-柱A / B の"
			  "「ストーリ」「レベル」欄がどう出ているか (2) シートレイヤ『試験シート』の"
			  "断面に A / B の構造材が映っているか");
}
