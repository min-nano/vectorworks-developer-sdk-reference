//
//	probes/runtime/layer-elevation-absolute-z/probe.cpp
//
//	[issue #194] レイヤの高さが**オブジェクトの絶対Z（`#IPZ#`）をどう決めるか**を確定する。
//
//	1 巡目（`ipzl-layer-elevation`）で本題は片付いた——**`#IPZL#` が引くのはレイヤの
//	高さ**（階 2699 / レイヤ 2659 で `#IPZL#`=−2659・`#IPZS#`=−2699）であり、素の
//	デザインレイヤでも**ちゃんと効いている**（0 が返るのは部材がレイヤ面上にあるから。
//	+300 持ち上げれば 300 が返る）。
//
//	**残ったのは、その走行の `#IPZ#` が 2 通りに読めることである。**
//
//	| 1 巡目の段 | 部材が生まれたレイヤ | 置かれたレイヤ | `#IPZ#` |
//	| --- | --- | --- | --- |
//	| A2（素） | A2（高さ 0 → **後から 1700**） | 同じ | 0 → **1700**（追随した） |
//	| C（素） | D（1600） | **C（1500）へ移した** | **1600**（移した先の高さではない） |
//	| S（ストーリ由来） | 既定レイヤ（0） | **S（2659）へ移した** | **0** |
//	| S（同・後から SetElevation(5000)） | 同上 | 同上 | **0 のまま**（追随しない） |
//
//	A2 は「所属レイヤの高さは絶対Zに乗る」と言い、S は「乗らない」と言う。食い違いの
//	説明は 2 つあり、**1 巡目の作りでは区別が付かない**:
//
//	  (i) **ストーリに属するレイヤでは高さがジオメトリに乗らない**（乗るのは素のレイヤだけ）。
//	  (ii) **`AddObjectToContainer` で移したオブジェクトは絶対Zを保ち、以後は所属レイヤの
//	       高さが変わっても追随しない**（S の部材は既定レイヤで生まれて移されたもの）。
//
//	1 巡目は**部材を作るときのアクティブレイヤを揃えていなかった**（`CreateLayer` は
//	作ったレイヤをアクティブにするが、`AddStoryLevelFromTemplate` で生えたレイヤは
//	アクティブにならない）。そこでこの 2 巡目は **`SetCurrentLayer` で毎回アクティブを
//	明示し**、「生まれたレイヤに留まる」場合と「移した」場合を分けて測る。
//
//	**目視は要らない。** `#IPZ#` / `#IPZS#` / `#IPZL#` を `GetDataTagExtractedData` で
//	読み戻すだけで判定できる。数値はすべて互いに違う:
//	  800（L1 へ後から）/ 900（L2 は最初から）/ 1100→1300（L3 は移した先）/
//	  2699（階）/ 2659（ストーリ由来レイヤ）/ 5000（そこへ後から）
//

#include "Probe.h"

#include "VWFC/VWObjects/VWLayerObj.h"
#include "VWFC/VWObjects/VWParametricObj.h"

#include <cstdio>
#include <string>

namespace
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	// ログ用の小物。**短い名前・ありふれた名前は使わない**（SDK と OS のヘッダが
	// グローバルへ撒いているため。probes/runtime/README.md）。

	std::string ProbeI194b_FromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string ProbeI194b_Num(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		std::string text(buffer);
		while (text.size() > 1 && text[text.size() - 1] == '0')
			text.erase(text.size() - 1);
		if (!text.empty() && text[text.size() - 1] == '.')
			text.erase(text.size() - 1);
		return text;
	}

	// レイヤの高さを 5 経路で読む（1 巡目で 5 経路とも一致すると実測済みだが、
	// **ストーリ付きレイヤへ後から書いた場合だけは食い違い得る**ので毎回出す）。
	void ProbeI194b_DescribeLayer(vwprobe::Report& probe, const std::string& label,
								  MCObjectHandle hLayer)
	{
		if (hLayer == nil)
		{
			probe.log("[レイヤ] " + label + " | **nil**");
			return;
		}
		VWFC::VWObjects::VWLayerObj layerObj(hLayer);
		std::string line = "[レイヤ] " + label;
		line += " | GetElevation=" + ProbeI194b_Num(layerObj.GetElevation());
		line += " GetHeight=" + ProbeI194b_Num(layerObj.GetHeight());

		TVariableBlock value;
		Real64 asReal = 0.0;
		if (gSDK->GetObjectVariable(hLayer, 157, value) && value.GetReal64(asReal))
			line += " ov157=" + ProbeI194b_Num(asReal);
		else
			line += " ov157=**取れない**";

		SStoryObjectData bound;
		bound.fBound = eStoryObjectBound_LayerElevation;
		bound.fBoundStory = 0;
		bound.fOffset = 0.0;
		line +=
			" バウンド経路=" + ProbeI194b_Num(gSDK->GetStoryObjectDataBoundHeight(bound, hLayer));

		MCObjectHandle hStory = gSDK->GetStoryOfLayer(hLayer);
		line += std::string(" ストーリ=") + (hStory == nil ? "無し" : "有り");
		if (hStory != nil)
			line += "(階の高さ=" + ProbeI194b_Num(gSDK->GetStoryElevation(hStory)) + ")";

		// **アクティブレイヤが狙いどおりかを毎回出す。** 1 巡目はここを揃えて
		// いなかったせいで結論が 2 通りに読めた。
		MCObjectHandle hActive = gSDK->GetActiveLayer();
		line += std::string(" これがアクティブ=") + (hActive == hLayer ? "yes" : "**no**");
		probe.log(line);
	}

	// タグを 1 つ作って部材へ関連付け、式を持たせるテキストを 1 本だけ持つ
	// レイアウトを渡す。**中身を入れてから渡し、渡した後に取り直す**
	// （Findings「データタグ」。VW が群を複製していることがある）。
	MCObjectHandle ProbeI194b_BuildTag(vwprobe::Report& probe, IDataTagSupport* tagSupport,
									   IDataTagTextLinkSupport* linkSupport, MCObjectHandle hMember,
									   MCObjectHandle hLayer, MCObjectHandle& outTag)
	{
		outTag = gSDK->CreateCustomObject("Data Tag", WorldPt(6000.0, 0.0), 0.0, true);
		if (outTag == nil)
		{
			probe.fail("データタグを作れなかった（CreateCustomObject が nil）");
			return nil;
		}
		if (hLayer != nil)
			gSDK->AddObjectToContainer(outTag, hLayer);
		tagSupport->AssociateWithObject(outTag, hMember);

		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		MCObjectHandle hText = gSDK->CreateTextBlock("T194B", WorldPt(0.0, 0.0), false, 0.0);
		if (hGroup == nil || hText == nil)
		{
			probe.fail("タグレイアウトを組めなかった（CreateGroup / CreateTextBlock が nil）");
			return nil;
		}
		gSDK->AddObjectToContainer(hText, hGroup);
		gSDK->SetCustomObjectProfileGroup(outTag, hGroup);

		MCObjectHandle hLiveGroup = gSDK->GetCustomObjectProfileGroup(outTag);
		MCObjectHandle hLiveText = nil;
		if (hLiveGroup != nil)
		{
			for (MCObjectHandle member = gSDK->FirstMemberObj(hLiveGroup); member != nil;
				 member = gSDK->NextObject(member))
			{
				if (gSDK->GetObjectTypeN(member) == kTextNode)
				{
					hLiveText = member;
					break;
				}
			}
		}
		if (hLiveText == nil)
			probe.fail("タグレイアウトにテキストが無い（式を持たせる先が作れない）");
		return hLiveText;
	}

	std::string ProbeI194b_EvalOne(IDataTagSupport* tagSupport,
								   IDataTagTextLinkSupport* linkSupport, MCObjectHandle hTag,
								   MCObjectHandle hText, const char* formula)
	{
		linkSupport->SetIsLinked(hText, true);
		linkSupport->SetFormula(hText, TXString(formula), false);
		tagSupport->UpdateUserDefinedTextsUIDs(hTag);
		tagSupport->UpdateDataTag(hTag);
		gSDK->ResetObject(hTag);

		TXStringSTLPairArray extracted;
		tagSupport->GetDataTagExtractedData(hTag, extracted);
		if (extracted.empty())
			return "(0 件)";
		std::string out;
		for (size_t i = 0; i < extracted.size(); ++i)
			out += ProbeI194b_FromTX(extracted[i].second);
		return out;
	}

	void ProbeI194b_EvalTriple(vwprobe::Report& probe, IDataTagSupport* tagSupport,
							   IDataTagTextLinkSupport* linkSupport, MCObjectHandle hTag,
							   MCObjectHandle hText, const std::string& label)
	{
		if (hTag == nil || hText == nil)
		{
			probe.log("[式] " + label + " | **タグが無いので引けない**");
			return;
		}
		probe.log("[式] " + label + " | #IPZ#='" +
				  ProbeI194b_EvalOne(tagSupport, linkSupport, hTag, hText, "#IPZ#") + "' #IPZS#='" +
				  ProbeI194b_EvalOne(tagSupport, linkSupport, hTag, hText, "#IPZS#") +
				  "' #IPZL#='" +
				  ProbeI194b_EvalOne(tagSupport, linkSupport, hTag, hText, "#IPZL#") + "'");
	}

	// **描ける**構造材を作る（#187 の 4 巡目で確立した手順）。**アクティブレイヤを
	// 明示してから呼ぶこと**——この 2 巡目の眼目がそこにある。
	MCObjectHandle ProbeI194b_MakeMember(vwprobe::Report& probe, const std::string& label,
										 MCObjectHandle hActiveLayer, double y)
	{
		if (hActiveLayer != nil)
			gSDK->SetCurrentLayer(hActiveLayer);
		MCObjectHandle hPath = gSDK->Create3DPoly();
		if (hPath != nil)
		{
			gSDK->Add3DVertex(hPath, WorldPt3(0.0, y, 0.0));
			gSDK->Add3DVertex(hPath, WorldPt3(4000.0, y, 0.0));
		}
		MCObjectHandle hMember = gSDK->CreateCustomObjectPath("StructuralMember", hPath, nil, true);
		if (hMember == nil)
		{
			probe.fail("構造材 " + label + " を作れなかった（CreateCustomObjectPath が nil）");
			return nil;
		}
		VWFC::VWObjects::VWParametricObj pio(hMember);
		pio.SetParamValue("MemberType", "2"); // 2＝木
		pio.SetParamReal("MajorBreadth", 120.0);
		pio.SetParamReal("MajorDepth", 600.0);
		pio.SetParamReal("MinorBreadth", 120.0);
		pio.SetParamReal("MinorDepth", 300.0);
		gSDK->ResetObject(hMember);
		// **どのレイヤに生まれたかをログに残す**（アクティブが狙いどおりかの裏取り）。
		MCObjectHandle hOwner = pio.GetParentLayer();
		TXString ownerName;
		if (hOwner != nil)
			gSDK->GetObjectName(hOwner, ownerName);
		probe.log("[部材] " + label + " | 生まれたレイヤ='" + ProbeI194b_FromTX(ownerName) + "'");
		return hMember;
	}
} // namespace

VW_PROBE("layer-elevation-absolute-z", "レイヤ高さは絶対Zをどう決めるか",
		 "生まれたレイヤ・移したレイヤ・ストーリ由来で #IPZ# の決まり方を分けて測る")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	// **何かを作る前に 1 度**（既定は kCustomObjectPrefAlways で、最初の 1 個で
	// 「オブジェクトの設定」ダイアログが出て止まる。probes/runtime/README.md）。
	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	IDataTagSupportPtr tagSupport(IID_DataTagSupport);
	IDataTagTextLinkSupportPtr linkSupport(IID_DataTagTextLinkSupport);
	if (!tagSupport || !linkSupport)
	{
		probe.fail("IDataTagSupport / IDataTagTextLinkSupport を取れなかった");
		return;
	}

	// =======================================================================
	// P. 素のレイヤ・**生まれたレイヤに留まる**。高さは後から与える。
	//	  1 巡目の A2 の再現だが、アクティブレイヤを明示する。
	//	  #IPZ# が 0 → 800 になれば「所属レイヤの高さは絶対Zに乗る」。
	// =======================================================================
	probe.log("=== P. 素のレイヤ L1（そこで生まれ、後から高さ 800）===");

	MCObjectHandle hL1 = gSDK->CreateLayer("T194B-L1", kLayerDesign);
	MCObjectHandle hMemberP = ProbeI194b_MakeMember(probe, "P", hL1, 0.0);
	MCObjectHandle hTagP = nil;
	MCObjectHandle hTextP = nil;
	if (hMemberP != nil)
		hTextP = ProbeI194b_BuildTag(probe, tagSupport, linkSupport, hMemberP, hL1, hTagP);
	ProbeI194b_DescribeLayer(probe, "L1(高さを与える前)", hL1);
	ProbeI194b_EvalTriple(probe, tagSupport, linkSupport, hTagP, hTextP, "P: 高さを与える前");

	if (hL1 != nil)
	{
		VWFC::VWObjects::VWLayerObj(hL1).SetElevation(800.0);
		ProbeI194b_DescribeLayer(probe, "L1(800 を与えた後)", hL1);
		ProbeI194b_EvalTriple(probe, tagSupport, linkSupport, hTagP, hTextP,
							  "P: 800 を与えた後 → #IPZ# が 800 なら所属レイヤの高さが乗る");
	}

	// =======================================================================
	// Q. 素のレイヤ・**最初から高さ 900**。そこで生まれてそこに留まる。
	// =======================================================================
	probe.log("=== Q. 素のレイヤ L2（最初から高さ 900。そこで生まれる）===");

	MCObjectHandle hL2 = gSDK->CreateLayer("T194B-L2", kLayerDesign);
	if (hL2 != nil)
		VWFC::VWObjects::VWLayerObj(hL2).SetElevation(900.0);
	ProbeI194b_DescribeLayer(probe, "L2(900)", hL2);

	MCObjectHandle hMemberQ = ProbeI194b_MakeMember(probe, "Q", hL2, 1000.0);
	MCObjectHandle hTagQ = nil;
	MCObjectHandle hTextQ = nil;
	if (hMemberQ != nil)
		hTextQ = ProbeI194b_BuildTag(probe, tagSupport, linkSupport, hMemberQ, hL2, hTagQ);
	ProbeI194b_EvalTriple(probe, tagSupport, linkSupport, hTagQ, hTextQ,
						  "Q: L2(900) で生まれた → #IPZ# が 900 なら作成時も乗る");

	// =======================================================================
	// R. **移す**——L2(900) で生まれた部材を L3(1100) へ入れ直し、その後 L3 を 1300 へ。
	//	  1 巡目の C と S を、素のレイヤだけで再現する段。
	//	    移した直後の #IPZ# が 900 → **移しても絶対Zは保たれる**
	//	    1100     → 移した先の高さが乗り直る
	//	  そのあと L3 を 1300 にして、**移した先の高さ変更に追随するか**を見る。
	// =======================================================================
	probe.log("=== R. L2(900) で生まれた部材を L3(1100) へ移し、L3 を 1300 にする ===");

	MCObjectHandle hL3 = gSDK->CreateLayer("T194B-L3", kLayerDesign);
	if (hL3 != nil)
		VWFC::VWObjects::VWLayerObj(hL3).SetElevation(1100.0);
	MCObjectHandle hMemberR = ProbeI194b_MakeMember(probe, "R", hL2, 2000.0);
	MCObjectHandle hTagR = nil;
	MCObjectHandle hTextR = nil;
	if (hMemberR != nil)
		hTextR = ProbeI194b_BuildTag(probe, tagSupport, linkSupport, hMemberR, hL2, hTagR);
	ProbeI194b_EvalTriple(probe, tagSupport, linkSupport, hTagR, hTextR, "R: L2(900) で生まれた");

	if (hMemberR != nil && hL3 != nil)
	{
		gSDK->AddObjectToContainer(hMemberR, hL3);
		if (hTagR != nil)
			gSDK->AddObjectToContainer(hTagR, hL3);
		gSDK->ResetObject(hMemberR);
		ProbeI194b_DescribeLayer(probe, "L3(1100)", hL3);
		ProbeI194b_EvalTriple(probe, tagSupport, linkSupport, hTagR, hTextR,
							  "R: L3(1100) へ移した直後 → 900 なら絶対Zを保つ");

		VWFC::VWObjects::VWLayerObj(hL3).SetElevation(1300.0);
		ProbeI194b_DescribeLayer(probe, "L3(1300 へ)", hL3);
		ProbeI194b_EvalTriple(probe, tagSupport, linkSupport, hTagR, hTextR,
							  "R: L3 を 1300 にした後 → 移した部材も追随するか");
	}

	// =======================================================================
	// T / U. **ストーリ由来のレイヤ上で生まれた部材**。1 巡目の S は既定レイヤで
	//	  生まれて移したものだったので、(i) と (ii) を区別できなかった。
	//	    #IPZ# が 2659 → ストーリ由来レイヤでも素と同じく高さが乗る（＝1 巡目の 0 は
	//	                    「移したから」＝候補 (ii)）
	//	    #IPZ# が 0    → ストーリ由来レイヤは高さがジオメトリに乗らない（＝候補 (i)）
	//	  そのあと SetElevation(5000) を掛けて、追随するかと読み戻しの食い違いを見る。
	// =======================================================================
	probe.log("=== T. ストーリ由来のレイヤ（階 2699 / 相対Z -40 → レイヤ 2659）で生まれる ===");

	MCObjectHandle hLayerStory = nil;
	MCObjectHandle hStory = nil;
	{
		TXString levelType("T194B-LV");
		gSDK->CreateLayerLevelType(levelType);
		TXString storyName("T194B-S");
		TXString storySuffix("T194BS");
		gSDK->CreateStory(storyName, storySuffix);
		hStory = gSDK->GetNamedObject("T194B-S");
		if (hStory != nil)
			gSDK->SetStoryElevation(hStory, 2699.0);

		// **出力引数の index を信用せず種別で引き直す**（#188 の 1 巡目はこれを
		// 信用して舞台を作り損ねた）。
		short index = -1;
		TXString templateName("T194B-TPL");
		gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelType, -40.0, 2400.0, index);
		short byType = -1;
		const short count = gSDK->GetNumStoryLevelTemplates();
		for (short i = 0; i <= count && byType < 0; ++i)
		{
			TXString name;
			TXString type;
			double scale = 0.0;
			double offset = 0.0;
			double wallHeight = 0.0;
			if (gSDK->GetStoryLevelTemplateInfo(i, name, scale, type, offset, wallHeight) &&
				ProbeI194b_FromTX(type) == "T194B-LV")
				byType = i;
		}
		if (byType >= 0 && hStory != nil)
		{
			gSDK->AddStoryLevelFromTemplate(hStory, byType);
			hLayerStory = gSDK->GetLayerForStory(hStory, levelType);
		}
		if (hStory != nil)
			probe.log(
				"階 T194B-S: GetStoryElevation=" + ProbeI194b_Num(gSDK->GetStoryElevation(hStory)) +
				" 階内相対Z=" + ProbeI194b_Num(gSDK->GetStoryLevelElevation(hStory, levelType)));
	}
	if (hLayerStory == nil)
	{
		probe.fail("ストーリのレベルからレイヤを取れなかった（T / U の段が測れない）");
		probe.log("=== 終わり ===");
		return;
	}
	ProbeI194b_DescribeLayer(probe, "S(ストーリ由来・2659)", hLayerStory);

	// **アクティブにしてからそこで生む**——これが 1 巡目との唯一の違い。
	MCObjectHandle hMemberT = ProbeI194b_MakeMember(probe, "T", hLayerStory, 0.0);
	MCObjectHandle hTagT = nil;
	MCObjectHandle hTextT = nil;
	if (hMemberT != nil)
		hTextT = ProbeI194b_BuildTag(probe, tagSupport, linkSupport, hMemberT, hLayerStory, hTagT);
	ProbeI194b_DescribeLayer(probe, "S(部材を生んだ後)", hLayerStory);
	ProbeI194b_EvalTriple(probe, tagSupport, linkSupport, hTagT, hTextT,
						  "T: ストーリ由来レイヤで生まれた → 2659 なら素と同じ・0 なら乗らない");

	probe.log("=== U. そのレイヤへ後から SetElevation(5000) ===");

	VWFC::VWObjects::VWLayerObj(hLayerStory).SetElevation(5000.0);
	ProbeI194b_DescribeLayer(probe, "S(後から 5000)", hLayerStory);
	if (hStory != nil)
		probe.log("階 T194B-S: 後から見た GetStoryElevation=" +
				  ProbeI194b_Num(gSDK->GetStoryElevation(hStory)) + " 階内相対Z=" +
				  ProbeI194b_Num(gSDK->GetStoryLevelElevation(hStory, TXString("T194B-LV"))));
	ProbeI194b_EvalTriple(probe, tagSupport, linkSupport, hTagT, hTextT,
						  "U: SetElevation(5000) の後 → #IPZ# が追随するか");

	probe.log("=== 終わり ===");
}
