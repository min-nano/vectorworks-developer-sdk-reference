//
//	probes/runtime/datatag-height-tokens/probe.cpp
//
//	[issue #187] データタグのタグフィールドの式で、**ストーリ基準・レイヤ基準・基準平面
//	基準の高さ**が何を返すかを測る。
//
//	【3 巡目＝仕上げ】2 巡目（ビルド `7dc0e49e2116`）で**規則は確定した**:
//
//	  末尾 `G` ＝ 基準平面（絶対Z そのもの）
//	  末尾 `L` ＝ 絶対Z − **レイヤの絶対Z**
//	  末尾 `S` ＝ 絶対Z − **階の高さ**（レベルの階内相対Z は引かれない）
//
//	階とレイヤを 40 ずらした舞台で割ったので、この 3 つは取り違えようがない。
//	連結と演算の規則も出た——**混ぜると空になり、`(…)` で括れば通るが括弧が印字される**。
//
//	**残っているのは 1 つだけ。** 上の規則から「`#IPZS#` は引き算なしでそのまま
//	『その階からの高さ』になる」と**導ける**が、**導いただけで測っていない**。
//	この調査ではその手の推論で 2 度外している（連結と演算の組み合わせ／推測した綴り）
//	ので、**推奨する式そのものを走らせてから書く**。
//
//	  部材 Q: **2F の FL レイヤ**（絶対Z 3571）に、**絶対Z 2699** で置く
//	          → `#IPZS#` が **−872** ちょうどなら規則どおり。
//	          → `" (2FL "#IPZS#")"` が `` (2FL -872)`` を出せば、それが推奨の式である。
//	  部材 R: **階に属さないデザインレイヤ**に置く
//	          → `#IPZS#` は何を返すか（推奨が効く条件を言い切るために要る）。
//	  どちらも +1000 動かして、**追随するか**を見る。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	std::string ProbeI187b_FromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string ProbeI187b_Num(double value)
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

	std::string ProbeI187b_OneLine(const std::string& value)
	{
		std::string out;
		out.reserve(value.size());
		for (size_t i = 0; i < value.size(); ++i)
		{
			const char c = value[i];
			if (c == '\r')
				continue;
			if (c == '\n')
				out += "\\n";
			else if (c == '\t')
				out += "\\t";
			else
				out += c;
		}
		return out;
	}

	// 構造材をパスから作り、指定のレイヤへ入れる（`AddObjectToContainer` は
	// **世界座標の Z を保つ**ので、パスに与えた Z がそのまま絶対Z になる）。
	MCObjectHandle ProbeI187b_MakeMember(vwprobe::Report& probe, const std::string& label,
										 MCObjectHandle hLayer, double y, double worldZ)
	{
		MCObjectHandle hPath = gSDK->Create3DPoly();
		if (hPath != nil)
		{
			gSDK->Add3DVertex(hPath, WorldPt3(0.0, y, worldZ));
			gSDK->Add3DVertex(hPath, WorldPt3(4000.0, y, worldZ));
		}
		MCObjectHandle hMember = gSDK->CreateCustomObjectPath("StructuralMember", hPath, nil, true);
		if (hMember == nil)
		{
			probe.fail("部材 " + label + " を作れなかった");
			return nil;
		}
		if (hLayer != nil)
			gSDK->AddObjectToContainer(hMember, hLayer);
		VWFC::VWObjects::VWParametricObj pio(hMember);
		pio.SetParamValue("MemberType", "2");
		pio.SetParamReal("MajorBreadth", 120.0);
		pio.SetParamReal("MajorDepth", 600.0);
		pio.SetParamReal("MinorBreadth", 120.0);
		pio.SetParamReal("MinorDepth", 300.0);
		gSDK->ResetObject(hMember);
		const VWFC::Math::VWPoint3D pos = pio.GetObjectModelPos();
		probe.log("部材 " + label + ": 与えた世界Z=" + ProbeI187b_Num(worldZ) +
				  " 読み戻した挿入点Z=" + ProbeI187b_Num(pos.z));
		return hMember;
	}

	MCObjectHandle ProbeI187b_BuildTag(vwprobe::Report& probe,
									   VectorWorks::Extension::IDataTagSupport* tagSupport,
									   VectorWorks::Extension::IDataTagTextLinkSupport* linkSupport,
									   MCObjectHandle hMember, MCObjectHandle hLayer, double x,
									   const std::string& label, MCObjectHandle& outTag)
	{
		outTag = gSDK->CreateCustomObject("Data Tag", WorldPt(x, 0.0), 0.0, true);
		if (outTag == nil)
		{
			probe.fail("データタグ（" + label + "）を作れなかった");
			return nil;
		}
		if (hLayer != nil)
			gSDK->AddObjectToContainer(outTag, hLayer);
		tagSupport->AssociateWithObject(outTag, hMember);

		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		MCObjectHandle hText = gSDK->CreateTextBlock("T187", WorldPt(0.0, 0.0), false, 0.0);
		if (hGroup == nil || hText == nil)
		{
			probe.fail("タグレイアウト（" + label + "）を組めなかった");
			return nil;
		}
		gSDK->AddObjectToContainer(hText, hGroup);
		gSDK->SetCustomObjectProfileGroup(outTag, hGroup);

		MCObjectHandle hLiveGroup = gSDK->GetCustomObjectProfileGroup(outTag);
		MCObjectHandle hLiveText = nil;
		if (hLiveGroup != nil)
		{
			for (MCObjectHandle m = gSDK->FirstMemberObj(hLiveGroup); m != nil;
				 m = gSDK->NextObject(m))
			{
				if (gSDK->GetObjectTypeN(m) == kTextNode)
				{
					hLiveText = m;
					break;
				}
			}
		}
		if (hLiveText == nil)
			probe.fail("タグレイアウト（" + label + "）にテキストが無い");
		return hLiveText;
	}

	void ProbeI187b_Eval(vwprobe::Report& probe,
						 VectorWorks::Extension::IDataTagSupport* tagSupport,
						 VectorWorks::Extension::IDataTagTextLinkSupport* linkSupport,
						 MCObjectHandle hTag, MCObjectHandle hText, const std::string& stage,
						 const std::string& token)
	{
		linkSupport->SetIsLinked(hText, true);
		linkSupport->SetFormula(hText, TXString(token.c_str()), false);
		tagSupport->UpdateUserDefinedTextsUIDs(hTag);
		tagSupport->UpdateDataTag(hTag);
		gSDK->ResetObject(hTag);

		VectorWorks::Extension::TXStringSTLPairArray extracted;
		tagSupport->GetDataTagExtractedData(hTag, extracted);
		std::string out;
		for (size_t i = 0; i < extracted.size(); ++i)
			out += "'" + ProbeI187b_OneLine(ProbeI187b_FromTX(extracted[i].second)) + "'";
		if (extracted.empty())
			out = "(0 件)";
		probe.log("[" + stage + "] " + ProbeI187b_OneLine(token) + " -> " + out);
	}
} // namespace

VW_PROBE("datatag-height-tokens", "タグの式の高さの綴りを測る",
		 "推奨する式そのものを走らせて、階からの高さが出るか・動かすと追随するかを見る")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	// =======================================================================
	// 1. 舞台。**2F の階の高さを 3571** にし、そこへ属するレイヤへ部材を
	//	**絶対Z 2699** で置く。規則（`S` ＝ 絶対Z − 階の高さ）が正しければ
	//	`#IPZS#` は **2699 − 3571 = −872** ちょうどになる——これは issue が
	//	最初から欲しがっていた数そのものである。
	// =======================================================================
	probe.log("=== 1. 舞台 ===");

	TXString levelTypeFL("T187-FL");
	gSDK->CreateLayerLevelType(levelTypeFL);

	TXString storyNameFL("T187-2F");
	TXString storySuffixFL("T187A");
	gSDK->CreateStory(storyNameFL, storySuffixFL);
	MCObjectHandle hStoryFL = gSDK->GetNamedObject("T187-2F");
	if (hStoryFL == nil)
	{
		probe.fail("階 T187-2F を作れなかった");
		return;
	}
	gSDK->SetStoryElevation(hStoryFL, 3571.0);

	short index = -1;
	TXString templateName("T187-TPL-FL");
	gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelTypeFL, 0.0, 2400.0, index);
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
			ProbeI187b_FromTX(type) == ProbeI187b_FromTX(levelTypeFL))
			byType = i;
	}
	MCObjectHandle hLayerFL = nil;
	if (byType >= 0)
	{
		gSDK->AddStoryLevelFromTemplate(hStoryFL, byType);
		hLayerFL = gSDK->GetLayerForStory(hStoryFL, levelTypeFL);
	}
	if (hLayerFL == nil)
	{
		probe.fail("2F のレイヤを取れなかった");
		return;
	}
	probe.log("階 T187-2F の高さ=" + ProbeI187b_Num(gSDK->GetStoryElevation(hStoryFL)) +
			  " 階内相対Z=" + ProbeI187b_Num(gSDK->GetStoryLevelElevation(hStoryFL, levelTypeFL)));

	// **階に属さない**デザインレイヤ（推奨が効く条件を言い切るために要る）。
	MCObjectHandle hLayerNoStory = gSDK->CreateLayer("T187-NOSTORY", kLayerDesign);
	probe.log(std::string("階に属さないレイヤ: ") + (hLayerNoStory != nil ? "作れた" : "**nil**") +
			  " GetStoryOfLayer=" +
			  (hLayerNoStory != nil && gSDK->GetStoryOfLayer(hLayerNoStory) != nil
				   ? "在る"
				   : "nil（無所属）"));

	MCObjectHandle hMemberQ = ProbeI187b_MakeMember(probe, "Q(2F のレイヤ)", hLayerFL, 0.0, 2699.0);
	MCObjectHandle hMemberR =
		ProbeI187b_MakeMember(probe, "R(階に属さない)", hLayerNoStory, 2000.0, 2699.0);

	IDataTagSupportPtr tagSupport(IID_DataTagSupport);
	IDataTagTextLinkSupportPtr linkSupport(IID_DataTagTextLinkSupport);
	if (!tagSupport || !linkSupport)
	{
		probe.fail("IDataTagSupport / IDataTagTextLinkSupport を取れなかった");
		return;
	}

	MCObjectHandle hTagQ = nil;
	MCObjectHandle hTextQ = nil;
	if (hMemberQ != nil)
		hTextQ = ProbeI187b_BuildTag(probe, tagSupport, linkSupport, hMemberQ, hLayerFL, 6000.0,
									 "Q", hTagQ);
	MCObjectHandle hTagR = nil;
	MCObjectHandle hTextR = nil;
	if (hMemberR != nil)
		hTextR = ProbeI187b_BuildTag(probe, tagSupport, linkSupport, hMemberR, hLayerNoStory,
									 9000.0, "R", hTagR);

	// =======================================================================
	// 2. **推奨する式そのものを走らせる。**
	//	`#IPZS#` が −872 ちょうど、`" (2FL "#IPZS#")"` が `` (2FL -872)`` なら、
	//	**引き算なしで「その階からの高さ」が出る**ことが測れたことになる。
	// =======================================================================
	probe.log("=== 2. 推奨する式そのもの ===");
	static const char* const kCases[] = {
		"#IPZ#", "#IPZS#", "#IPZL#", "#ST#", "\" (2FL \"#IPZS#\")\"",
		// 天端（実体が描かれていれば値が出る。`±1.79e308` なら描かれていない）
		"#ZTBBS#", "#ZTBBG#", "\" (2FL \"#ZTBBS#\")\""};

	if (hTextQ != nil)
		for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
			ProbeI187b_Eval(probe, tagSupport, linkSupport, hTagQ, hTextQ, "段1-Q(2F)", kCases[i]);
	if (hTextR != nil)
		for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
			ProbeI187b_Eval(probe, tagSupport, linkSupport, hTagR, hTextR, "段1-R(無所属)",
							kCases[i]);

	// =======================================================================
	// 3. **+1000 動かして追随するか。** 利用者の要望は「取り込み後に高さを変えても
	//	注記が追随すること」なので、ここが要望そのものの試験である。
	//	Q の `#IPZS#` は **128**（＝3699 − 3571）になるはず。
	// =======================================================================
	probe.log("=== 3. +1000 動かして読み直す ===");
	if (hMemberQ != nil && hTextQ != nil)
	{
		gSDK->MoveObject3D(hMemberQ, 0.0, 0.0, 1000.0);
		gSDK->ResetObject(hMemberQ);
		for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
			ProbeI187b_Eval(probe, tagSupport, linkSupport, hTagQ, hTextQ, "段2-Q(移動後)",
							kCases[i]);
	}
	if (hMemberR != nil && hTextR != nil)
	{
		gSDK->MoveObject3D(hMemberR, 0.0, 0.0, 1000.0);
		gSDK->ResetObject(hMemberR);
		for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
			ProbeI187b_Eval(probe, tagSupport, linkSupport, hTagR, hTextR, "段2-R(移動後)",
							kCases[i]);
	}

	probe.log("=== 終わり ===");
}
