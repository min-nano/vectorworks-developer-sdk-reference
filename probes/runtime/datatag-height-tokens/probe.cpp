//
//	probes/runtime/datatag-height-tokens/probe.cpp
//
//	[issue #187] データタグのタグフィールドの式で、**ストーリ基準・レイヤ基準・基準平面
//	基準の高さ**（`#IPZS#` / `#ZTBBS#` / `#ZTBBG#` …）が実際に何を返すかを測る。
//
//	【2 巡目】1 巡目（ビルド `09488cfbfb4b`）で**綴りは全部生きている**と分かった
//	（`#IPZ#`＝絶対Z・`#IPZS#`＝ストーリ基準・`#IPZL#`＝レイヤ基準。どれも動かすと追随する）。
//	ただし**3 つ取り残した**:
//
//	  1. **`#IPZS#` と `#IPZL#` が同じ値（−2699）を返した。** 舞台のレベルの階内相対Z を
//	     0 にしていたので、**階の高さとレイヤの高さが一致していた**——2 つを見分けられない
//	     舞台だった。**レベルに相対Z −40 を与えて割る。**
//	  2. **`#ZTBB…#` / `#ZBBB…#` が `±1.7976931348623e308`（＝`DBL_MAX`）を返した。**
//	     これは**何も描いていない PIO の反転した空矩形**で、綴りが悪いのではない。
//	     構造材を描かせられていないので、**確実に描かれる押し出し（`CreateExtrude`）**を
//	     別に置いて、そちらで上面・底面の基準が何かを測る。
//	  3. **`" (2FL "#IPZL#-872")"` が空になった。** `" (2FL "#IPZL#")"`（連結だけ）も
//	     `#IPZL#-872`（演算だけ）も通るのに、**混ぜると空**。#188 でこの組み合わせを
//	     確かめずに推奨へ書いてしまった。**綴りを何通りか試して、通る形を見つける。**
//
//	舞台の数値はすべて互いに違えてあるので、**返った数字だけで出どころが言い当てられる**。

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

	std::string ProbeI187b_Int(long long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%lld", value);
		return std::string(buffer);
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

	// **描ける**構造材を作る（Findings「取り込みでどう書くか」の手順。`MemberType` と
	//	`Minor*` を省くと何も描かれず、バウンディングボックス系の綴りが測れない）。
	MCObjectHandle ProbeI187b_MakeMember(vwprobe::Report& probe, const std::string& label,
										 MCObjectHandle hLayer, double y)
	{
		MCObjectHandle hPath = gSDK->Create3DPoly();
		if (hPath != nil)
		{
			gSDK->Add3DVertex(hPath, WorldPt3(0.0, y, 0.0));
			gSDK->Add3DVertex(hPath, WorldPt3(4000.0, y, 0.0));
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
		pio.SetParamValue("MemberType", "2"); // 2＝木
		pio.SetParamReal("MajorBreadth", 120.0);
		pio.SetParamReal("MajorDepth", 600.0);
		pio.SetParamReal("MinorBreadth", 120.0);
		pio.SetParamReal("MinorDepth", 300.0);
		gSDK->ResetObject(hMember);

		WorldRect bounds;
		const bool got = gSDK->GetObjectBounds(hMember, bounds);
		probe.log("部材 " + label + ": MajorDepth='" +
				  ProbeI187b_FromTX(pio.GetParamValue("MajorDepth")) + "' 外接=" +
				  (got && bounds.right > bounds.left
					   ? "幅 " + ProbeI187b_Num(bounds.right - bounds.left)
					   : "**空（図形が無い）**"));
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
		probe.log("[" + stage + "] " + token + " -> " + out);
	}
} // namespace

VW_PROBE("datatag-height-tokens", "タグの式の高さの綴りを測る",
		 "ストーリ基準・レイヤ基準・基準平面基準の高さが何を返し、動かすと追随するかを測る")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	// =======================================================================
	// 1. 舞台。**数値はすべて互いに違う**ので、返った数字だけで出どころが分かる。
	//
	//	  ストーリ T187-2F   高さ 3571 / レベル T187-FL（階内相対 0 ＝ 絶対 3571）
	//	  ストーリ T187-PLAN 高さ 2699 / レベル T187-PLAN-L（伏図レイヤの見立て）
	//	  部材 P: 伏図レイヤに置き、**別の階**の FL へバウンド（−872 / −302）
	//
	//	欲しい値は「2FL から測った天端の高さ」＝ **−872**。
	//	見分けの目安（部材は局所Z 0・せい 600 なので天端は挿入点と同じ高さ）:
	//	  −2699 … レイヤ基準（＝いまの `#IPZL#`）   0 … 基準平面から見た挿入点
	//	  −3571 … 2F の FL を基準にした値           −872 … **欲しい値**
	// =======================================================================
	probe.log("=== 1. 舞台 ===");

	TXString levelTypeFL("T187-FL");
	TXString levelTypePlan("T187-PLAN-L");
	gSDK->CreateLayerLevelType(levelTypeFL);
	gSDK->CreateLayerLevelType(levelTypePlan);

	TXString storyNameFL("T187-2F");
	TXString storySuffixFL("T187A");
	gSDK->CreateStory(storyNameFL, storySuffixFL);
	MCObjectHandle hStoryFL = gSDK->GetNamedObject("T187-2F");
	if (hStoryFL != nil)
		gSDK->SetStoryElevation(hStoryFL, 3571.0);

	TXString storyNamePlan("T187-PLAN");
	TXString storySuffixPlan("T187B");
	gSDK->CreateStory(storyNamePlan, storySuffixPlan);
	MCObjectHandle hStoryPlan = gSDK->GetNamedObject("T187-PLAN");
	if (hStoryPlan != nil)
		gSDK->SetStoryElevation(hStoryPlan, 2699.0);

	// レベルテンプレートは**出力引数の index を信用せず種別で引き直す**
	// （#188 の 1 巡目はこれを信用して舞台を作り損ねた）。
	MCObjectHandle hLayerFL = nil;
	MCObjectHandle hLayerPlan = nil;
	for (int pass = 0; pass < 2; ++pass)
	{
		TXString& levelType = (pass == 0) ? levelTypeFL : levelTypePlan;
		MCObjectHandle hStory = (pass == 0) ? hStoryFL : hStoryPlan;
		if (hStory == nil)
			continue;
		short index = -1;
		TXString templateName = (pass == 0) ? TXString("T187-TPL-FL") : TXString("T187-TPL-PLAN");
		// **伏図のレベルだけ階内相対Z を −40 にする。** 1 巡目は 0 にしていたので
		// 階の高さとレイヤの高さが一致し、`#IPZS#` と `#IPZL#` を見分けられなかった。
		//   階 PLAN の高さ 2699 ＋ 相対Z −40 → **レイヤの絶対Z は 2659**
		const double levelOffset = (pass == 0) ? 0.0 : -40.0;
		gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelType, levelOffset, 2400.0, index);
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
				ProbeI187b_FromTX(type) == ProbeI187b_FromTX(levelType))
				byType = i;
		}
		if (byType >= 0)
		{
			gSDK->AddStoryLevelFromTemplate(hStory, byType);
			if (pass == 0)
				hLayerFL = gSDK->GetLayerForStory(hStory, levelType);
			else
				hLayerPlan = gSDK->GetLayerForStory(hStory, levelType);
		}
	}
	probe.log(std::string("レイヤ: FL=") + (hLayerFL != nil ? "取れた" : "**nil**") +
			  " PLAN=" + (hLayerPlan != nil ? "取れた" : "**nil**"));
	if (hLayerPlan == nil)
	{
		probe.fail("伏図レイヤを用意できなかった（以後の高さの解釈が付かない）");
		return;
	}
	probe.log("実測: 階 2F の高さ=" + ProbeI187b_Num(gSDK->GetStoryElevation(hStoryFL)) +
			  " 階内相対Z(T187-FL)=" +
			  ProbeI187b_Num(gSDK->GetStoryLevelElevation(hStoryFL, levelTypeFL)) +
			  " / 階 PLAN の高さ=" + ProbeI187b_Num(gSDK->GetStoryElevation(hStoryPlan)) +
			  " 階内相対Z(T187-PLAN-L)=" +
			  ProbeI187b_Num(gSDK->GetStoryLevelElevation(hStoryPlan, levelTypePlan)));

	MCObjectHandle hMemberP = ProbeI187b_MakeMember(probe, "P", hLayerPlan, 0.0);
	if (hMemberP == nil)
		return;
	{
		SStoryObjectData bound;
		bound.fBound = eStoryObjectBound_Story;
		bound.fBoundStory = 1; // 伏図の階から 1 つ上＝T187-2F
		bound.fLayerLevelType = levelTypeFL;
		bound.fOffset = -872.0;
		gSDK->SetObjectStoryBound(hMemberP, 0, bound);
		bound.fOffset = -302.0;
		gSDK->SetObjectStoryBound(hMemberP, 1, bound);
		gSDK->ResetObject(hMemberP);
		probe.log(
			"部材 P: バウンド件数=" +
			ProbeI187b_Int(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(hMemberP))) +
			" 解決Z ID0=" + ProbeI187b_Num(gSDK->GetObjectBoundElevation(hMemberP, 0)) +
			" ID1=" + ProbeI187b_Num(gSDK->GetObjectBoundElevation(hMemberP, 1)));
	}

	// ---- **確実に描かれる本**を 1 つ置く（押し出し）。構造材を描かせられていないので、
	//	`#ZTBB…#` / `#ZBBB…#` の基準はこちらで測る。
	//	  伏図レイヤ（絶対Z **2659**）の上に、**局所Z 100 → 700** の押し出しを置く。
	//	  したがって期待される値は互いに違う:
	//	    上面: 基準平面 **3359** / レイヤ **700** / ストーリ **660**（2659+700−2699）
	//	    底面: 基準平面 **2759** / レイヤ **100** / ストーリ **60**
	MCObjectHandle hExtrude = nil;
	{
		MCObjectHandle hProfile =
			gSDK->CreateRectangleN(WorldPt(0.0, 5000.0), Vector2(1.0, 0.0), 2000.0, 1000.0);
		hExtrude = gSDK->CreateExtrude(100.0, 700.0);
		if (hExtrude != nil && hProfile != nil)
		{
			gSDK->AddObjectToContainer(hProfile, hExtrude);
			gSDK->ResetObject(hExtrude);
			if (hLayerPlan != nil)
				gSDK->AddObjectToContainer(hExtrude, hLayerPlan);
			WorldRect bounds;
			const bool got = gSDK->GetObjectBounds(hExtrude, bounds);
			probe.log(std::string("押し出し: 外接=") +
					  (got && bounds.right > bounds.left
						   ? "幅 " + ProbeI187b_Num(bounds.right - bounds.left)
						   : "**空（図形が無い）**"));
		}
		else
			probe.log("押し出しを作れなかった（以後 bbox 系は構造材でしか測れない）");
	}

	// =======================================================================
	// 2. ダイアログに並んでいた綴りを、そのまま順に引く。
	// =======================================================================
	probe.log("=== 2. 高さの綴りを引く ===");

	IDataTagSupportPtr tagSupport(IID_DataTagSupport);
	IDataTagTextLinkSupportPtr linkSupport(IID_DataTagTextLinkSupport);
	if (!tagSupport || !linkSupport)
	{
		probe.fail("IDataTagSupport / IDataTagTextLinkSupport を取れなかった");
		return;
	}

	MCObjectHandle hTagP = nil;
	MCObjectHandle hTextP = ProbeI187b_BuildTag(probe, tagSupport, linkSupport, hMemberP,
												hLayerPlan, 6000.0, "P", hTagP);
	if (hTextP == nil)
		return;

	// **「タグフィールドの定義」ダイアログに並んでいた綴り**（利用者の画面から書き写した）。
	// 読み取りに自信の無いものも混ぜてある——空が返れば「その綴りは無い」と分かる
	// （#188 で「知らない `#…#` は空文字列になる」ことを実測済み）。
	static const char* const kTokens[] = {
		// --- 挿入点
		"#IPX#", "#IPY#", "#IPZ#",
		"#IPZS#", // 挿入点の高さ (Z)_ストーリの高さ  ← **未試験だったもの**
		"#IPZL#", // 挿入点の高さ (Z)_レイヤの高さ    ← #188 で測った唯一のもの
		// --- バウンディングボックス上面＝**天端**（この調査が欲しかったもの）
		"#ZTBBS#", // _ストーリの高さ
		"#ZTBBL#", // _レイヤの高さ
		"#ZTBBG#", // _基準平面
		// --- バウンディングボックス底面
		"#ZBBBS#", "#ZBBBL#", "#ZBBBG#",
		// --- 大きさ・中心
		"#BX#", "#BY#", "#BZ#", "#XCTR#", "#YCTR#", "#ZCTR#",
		// --- ストーリ・レイヤ・クラス
		"#ST#", "#STLT#", "#STPS#", "#L#", "#LD#", "#C#", "#CD#", "#N#",
		// --- その他（読み取りが怪しいものを含む。空なら綴りが違う）
		"#PERIM#", "#AREA#", "#SUBFAREA#", "#MATERIAL#", "#OSTYLE#", "#SYMBOL#", "#ROTATIONX#",
		"#ROTATIONY#", "#ROTATIONZ#",
		// --- 対照: 存在しないはずの綴り（空が返る形を同じ走行に持っておく）
		"#T187NOSUCHTOKEN#"};

	for (size_t i = 0; i < sizeof(kTokens) / sizeof(kTokens[0]); ++i)
		ProbeI187b_Eval(probe, tagSupport, linkSupport, hTagP, hTextP, "段1", kTokens[i]);

	// =======================================================================
	// 3. **動かしたら追随するか。** `#IPZL#` は追随すると実測済み（#188）。
	//	ストーリ基準・基準平面基準のものも同じかを見る。
	// =======================================================================
	probe.log("=== 3. +1000 動かして読み直す ===");
	gSDK->MoveObject3D(hMemberP, 0.0, 0.0, 1000.0);
	gSDK->ResetObject(hMemberP);
	probe.log(
		"部材 P 移動後: 解決Z ID0=" + ProbeI187b_Num(gSDK->GetObjectBoundElevation(hMemberP, 0)) +
		" ID1=" + ProbeI187b_Num(gSDK->GetObjectBoundElevation(hMemberP, 1)));

	static const char* const kAfterMove[] = {"#IPZ#",	"#IPZS#",  "#IPZL#",  "#ZTBBS#",
											 "#ZTBBL#", "#ZTBBG#", "#ZBBBS#", "#ZBBBL#",
											 "#ZBBBG#", "#ST#",	   "#STLT#"};
	for (size_t i = 0; i < sizeof(kAfterMove) / sizeof(kAfterMove[0]); ++i)
		ProbeI187b_Eval(probe, tagSupport, linkSupport, hTagP, hTextP, "段2(移動後)",
						kAfterMove[i]);

	// =======================================================================
	// 4. 「その階の FL から測った天端の高さ」を**定数なしで**出せる綴りがあれば、
	//	そのまま注記の形にして確かめる。
	// =======================================================================
	// =======================================================================
	// 4. **確実に描かれる本**（押し出し）で bbox 系の基準を測る。
	// =======================================================================
	probe.log("=== 4. 押し出しで bbox 系の基準を測る ===");
	if (hExtrude != nil)
	{
		MCObjectHandle hTagE = nil;
		MCObjectHandle hTextE = ProbeI187b_BuildTag(probe, tagSupport, linkSupport, hExtrude,
													hLayerPlan, 9000.0, "E", hTagE);
		if (hTextE != nil)
		{
			static const char* const kBBox[] = {"#ZTBBS#", "#ZTBBL#", "#ZTBBG#", "#ZBBBS#",
												"#ZBBBL#", "#ZBBBG#", "#IPZ#",	 "#IPZS#",
												"#IPZL#",  "#ZCTR#",  "#ST#",	 "#L#"};
			for (size_t i = 0; i < sizeof(kBBox) / sizeof(kBBox[0]); ++i)
				ProbeI187b_Eval(probe, tagSupport, linkSupport, hTagE, hTextE, "段4(押し出し)",
								kBBox[i]);
		}
	}

	// =======================================================================
	// 5. **連結と演算を混ぜる綴り**を詰める。1 巡目で
	//	`" (2FL "#IPZL#-872")"` が空になった——単独ではどちらも通るのに、である。
	//	通る形が 1 つでも見つかれば、それが推奨の式になる。
	// =======================================================================
	probe.log("=== 5. 連結と演算を混ぜる綴り ===");
	static const char* const kSyntax[] = {
		"#IPZL#",						// 基準: フィールドだけ
		"#IPZL#-872",					// 基準: 演算だけ（通ると実測済み）
		"\" (2FL \"#IPZL#\")\"",		// 基準: 連結だけ（通ると実測済み）
		"\" (2FL \"#IPZL#-872\")\"",	// 1 巡目に空だった形
		"\" (2FL \"(#IPZL#-872)\")\"",	// 括弧で括る
		"(\" (2FL \"#IPZL#-872\")\")",	// 全体を括る
		"\" (2FL \"(#IPZL#-872)\" )\"", // 括弧＋閉じ側に空白
		"#IPZL#-872\" (2FL)\"",			// 演算を先、文字を後ろ
		"\"(\"#IPZL#-872\")\"",			// いちばん短い形
		"\" (2FL \"#IPZL#\"-872)\"",	// 演算子を文字側へ入れた形（対照）
		"\" (2FL \"#IPZS#-40\")\"",		// ストーリ基準でも同じか
		"\" (2FL \"(#IPZS#-40)\")\""};
	for (size_t i = 0; i < sizeof(kSyntax) / sizeof(kSyntax[0]); ++i)
		ProbeI187b_Eval(probe, tagSupport, linkSupport, hTagP, hTextP, "段5", kSyntax[i]);

	probe.log("=== 終わり ===");
}
