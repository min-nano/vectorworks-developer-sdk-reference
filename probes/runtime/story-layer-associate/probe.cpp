//
//	probes/runtime/story-layer-associate/probe.cpp
//
//	[issue #221] 1 周目（`story-only-layer`）で **`AssociateLayerWithStory` は素のレイヤを
//	階に結べない**（4 回とも `false`・`GetStoryOfLayer` は nil）と分かった。ただし
//	**1 周目は「結んでから `SetLayerLevelType`」の順しか試していない。** `vs.py` の説明は
//	「同じ階の別レイヤと**レベル種別が同じ**なら失敗する」——つまり**レベル種別を見ている**
//	ので、**先に種別を与えてから結べば通る**かもしれない。そこを測る。
//
//	  T1 未使用のレベル種別を先に与えてから結ぶ（通るか）
//	  T2 その階で既に使われている種別を与えてから結ぶ（`vs.py` の失敗条件の確認）
//	  T3 通ったら: レイヤ高さ・構造材のバウンド（自階 0 / 上階 1）の解決・階を動かした追随
//	  T4 同じ階へ同じレベル種別のレイヤを 2 枚置けるか（テンプレートを 2 回足す）
//	  T5 結べたレイヤと同じ種別で 2 枚目を結べるか／別の未使用種別なら結べるか
//	  T6 結んだ後に `SetLayerLevelType(layer, "")` でレベルだけ外せるか
//	     （＝issue が求める「ストーリ＝その階・レベル＝なし」へ到達する最後の道）
//
//	**新規の空図面で走らせる。** 階・レベル種別・レイヤ・構造材を作る（消さない）。
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

	// 素のデザインレイヤを 1 枚作り、**先にレベル種別を与えてから**階へ結ぶ。
	// 戻り値はそのレイヤ（作れなければ nil）。
	MCObjectHandle AssociateWithLevelTypeFirst(vwprobe::Report& probe, const std::string& tag,
											   const char* layerName, const TXString& levelType,
											   MCObjectHandle hStory)
	{
		MCObjectHandle hLayer = gSDK->CreateLayer(layerName, kLayerDesign);
		if (hLayer == nil)
		{
			probe.fail(tag + ": CreateLayer が nil を返した");
			return nil;
		}
		TXString levelTypeArg(levelType);
		bool setType = gSDK->SetLayerLevelType(hLayer, levelTypeArg);
		probe.log(tag + ": SetLayerLevelType('" + ProbeText(levelType) +
				  "')=" + ProbeFlag(setType));
		LogLayerState(probe, tag + " 種別を与えた後", hLayer);
		bool associated = gSDK->AssociateLayerWithStory(hLayer, hStory);
		probe.log(tag + ": AssociateLayerWithStory(" + ProbeObjName(hStory) +
				  ")=" + ProbeFlag(associated));
		LogLayerState(probe, tag + " 結んだ後", hLayer);
		return hLayer;
	}
} // namespace

VW_PROBE("story-layer-associate", "レベル種別を先に与えて結ぶ",
		 "SetLayerLevelType を先に掛けてから AssociateLayerWithStory を呼ぶ順序を測る")
{
	const TXString kBaseLevelType("試験天端"); // 両方の階にレベルを生やす種別（基準）
	const TXString kFreeLevelType("試験柱");   // 誰も使っていない種別
	const TXString kFreeLevelType2("試験柱2"); // もう 1 つの未使用の種別

	// ===== T0 下ごしらえ: 階 2 つと、レベル付きのレイヤ（比較の基準） =====
	probe.log("===== T0 下ごしらえ =====");
	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever); // 設定ダイアログ抑止

	TXString baseTypeName(kBaseLevelType);
	probe.log("CreateLayerLevelType(試験天端)=" +
			  ProbeFlag(gSDK->CreateLayerLevelType(baseTypeName)));
	TXString freeTypeName(kFreeLevelType);
	probe.log("CreateLayerLevelType(試験柱)=" +
			  ProbeFlag(gSDK->CreateLayerLevelType(freeTypeName)));
	TXString freeTypeName2(kFreeLevelType2);
	probe.log("CreateLayerLevelType(試験柱2)=" +
			  ProbeFlag(gSDK->CreateLayerLevelType(freeTypeName2)));

	MCObjectHandle hStory1 = MakeStory(probe, "試験1階", "t1", 1000.0);
	MCObjectHandle hStory2 = MakeStory(probe, "試験2階", "t2", 4000.0);
	if (hStory1 == nil || hStory2 == nil)
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
	probe.log("AddStoryLevelFromTemplate(試験1階)=" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory1, templateIndex)));
	probe.log("AddStoryLevelFromTemplate(試験2階)=" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory2, templateIndex)));
	MCObjectHandle hLevelLayer1 = gSDK->GetLayerForStory(hStory1, kBaseLevelType);
	LogLayerState(probe, "基準(レベル付き・1階)", hLevelLayer1);
	LogLayerState(probe, "基準(レベル付き・2階)", gSDK->GetLayerForStory(hStory2, kBaseLevelType));
	LogLayersOfStory(probe, "試験1階に属するレイヤ", hStory1);

	// ===== T1 未使用のレベル種別を先に与えてから結ぶ（1 周目で試していない順序） =====
	probe.log("===== T1 未使用の種別を先に与えてから結ぶ =====");
	MCObjectHandle hLayerP =
		AssociateWithLevelTypeFirst(probe, "P", "試験-柱P", kFreeLevelType, hStory1);
	bool associatedP = (hLayerP != nil) && (gSDK->GetStoryOfLayer(hLayerP) == hStory1);
	probe.log("P は階に結ばれたか（読み戻しで判定）=" + ProbeFlag(associatedP));
	probe.log("GetLayerForStory(試験1階, 試験柱)=" +
			  ProbeObjName(gSDK->GetLayerForStory(hStory1, kFreeLevelType)) +
			  " 階内相対Z=" + ProbeNum(gSDK->GetStoryLevelElevation(hStory1, kFreeLevelType)));
	LogLayersOfStory(probe, "試験1階に属するレイヤ（P のあと）", hStory1);

	// ===== T2 その階で既に使われている種別を与えてから結ぶ（失敗条件の確認） =====
	probe.log("===== T2 使用中の種別を先に与えてから結ぶ =====");
	MCObjectHandle hLayerQ =
		AssociateWithLevelTypeFirst(probe, "Q", "試験-柱Q", kBaseLevelType, hStory1);
	probe.log("Q は階に結ばれたか=" +
			  ProbeFlag((hLayerQ != nil) && (gSDK->GetStoryOfLayer(hLayerQ) == hStory1)));

	// ===== T3 結べたなら、バウンドの解決と階の移動への追随を測る =====
	probe.log("===== T3 バウンドの解決（バウンドが指す種別は『試験天端』で揃える） =====");
	probe.log("GetStoryObjectDataBoundHeight で解かせる（入れ物＝レイヤ）");
	if (hLevelLayer1 != nil)
		probe.log("  基準(レベル付き): 自階(0)=" +
				  ProbeNum(gSDK->GetStoryObjectDataBoundHeight(
					  MakeStoryBound(0, kBaseLevelType, 0.0), hLevelLayer1)) +
				  " 上階(1)=" +
				  ProbeNum(gSDK->GetStoryObjectDataBoundHeight(
					  MakeStoryBound(1, kBaseLevelType, 0.0), hLevelLayer1)));
	if (hLayerP != nil)
		probe.log("  P: 自階(0)=" +
				  ProbeNum(gSDK->GetStoryObjectDataBoundHeight(
					  MakeStoryBound(0, kBaseLevelType, 0.0), hLayerP)) +
				  " 上階(1)=" +
				  ProbeNum(gSDK->GetStoryObjectDataBoundHeight(
					  MakeStoryBound(1, kBaseLevelType, 0.0), hLayerP)));
	MCObjectHandle hMemberRef =
		MakeBoundMember(probe, "基準(レベル付き)", hLevelLayer1, kBaseLevelType, 0.0);
	LogMemberState(probe, "基準(レベル付き)", hMemberRef, hLevelLayer1);
	MCObjectHandle hMemberP = MakeBoundMember(probe, "P", hLayerP, kBaseLevelType, 2000.0);
	LogMemberState(probe, "P", hMemberP, hLayerP);

	probe.log("SetStoryElevation(試験1階, 1800) で階を動かす=" +
			  ProbeFlag(gSDK->SetStoryElevation(hStory1, 1800.0)));
	LogLayerState(probe, "基準 階を動かした後", hLevelLayer1);
	LogLayerState(probe, "P 階を動かした後", hLayerP);
	if (hMemberRef != nil)
		gSDK->ResetObject(hMemberRef);
	if (hMemberP != nil)
		gSDK->ResetObject(hMemberP);
	LogMemberState(probe, "基準 階を動かして ResetObject 後", hMemberRef, hLevelLayer1);
	LogMemberState(probe, "P 階を動かして ResetObject 後", hMemberP, hLayerP);

	// ===== T4 同じ階へ同じレベル種別のレイヤを 2 枚置けるか（テンプレートを 2 回） =====
	probe.log("===== T4 同じ階へ同じ種別を 2 回足す =====");
	probe.log("AddStoryLevelFromTemplate(試験1階) 2 回目=" +
			  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory1, templateIndex)));
	LogLayersOfStory(probe, "試験1階に属するレイヤ（2 回目のあと）", hStory1);

	// ===== T5 同じ種別で 2 枚目／別の未使用種別なら結べるか =====
	probe.log("===== T5 2 枚目を結ぶ =====");
	MCObjectHandle hLayerR =
		AssociateWithLevelTypeFirst(probe, "R(同じ種別)", "試験-柱R", kFreeLevelType, hStory1);
	probe.log("R は階に結ばれたか=" +
			  ProbeFlag((hLayerR != nil) && (gSDK->GetStoryOfLayer(hLayerR) == hStory1)));
	MCObjectHandle hLayerS = AssociateWithLevelTypeFirst(probe, "S(別の未使用種別)", "試験-柱S",
														 kFreeLevelType2, hStory1);
	probe.log("S は階に結ばれたか=" +
			  ProbeFlag((hLayerS != nil) && (gSDK->GetStoryOfLayer(hLayerS) == hStory1)));
	LogLayersOfStory(probe, "試験1階に属するレイヤ（T5 のあと）", hStory1);

	// ===== T6 結んだ後にレベルだけ外せるか（issue が求める状態への最後の道） =====
	probe.log("===== T6 結んだ後に SetLayerLevelType(layer, '') でレベルを外す =====");
	if (hLayerP != nil)
	{
		TXString emptyType;
		probe.log("SetLayerLevelType(P, '')=" +
				  ProbeFlag(gSDK->SetLayerLevelType(hLayerP, emptyType)));
		LogLayerState(probe, "P レベルを外した後", hLayerP);
		probe.log("GetLayerForStory(試験1階, 試験柱)=" +
				  ProbeObjName(gSDK->GetLayerForStory(hStory1, kFreeLevelType)));
		if (hMemberP != nil)
			gSDK->ResetObject(hMemberP);
		LogMemberState(probe, "P の構造材（レベルを外して ResetObject 後）", hMemberP, hLayerP);
		LogLayersOfStory(probe, "試験1階に属するレイヤ（T6 のあと）", hStory1);
	}

	probe.log("===== おわり =====");
}
