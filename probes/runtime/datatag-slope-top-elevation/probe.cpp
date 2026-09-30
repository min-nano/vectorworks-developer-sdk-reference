//
//	probes/runtime/datatag-slope-top-elevation/probe.cpp
//
//	[issue #196] データタグで**構造材の天端の高さ**を材に連動して出すための調査。
//
//	【3 巡目＝修飾子の正式な綴り】1・2 巡目で**本題は片付いた**（2 巡目＝ビルド
//	`d714e9370d7a`。値はすべて実行ログそのまま）:
//
//	  傾斜材 S（始端が低い端。解決Z 2699 → 3531）
//	    `#IPZS#`  -> `-872`（**始端**の天端）   `#ZTBBS#` -> `-40`（**高い端**の天端）
//	    `#ZTBBG#` -> `3531` ＝ `GetObjectCube` の上面と一致（**断面の角は天端より上に
//	    出ない**）。勾配を変えても成り立つ。
//	    `" (2FL "#IPZS#"~"@#ZTBBS#<>#IPZS#:""#ZTBBS#@#ZTBBS#<>#IPZS#:""")"`
//	      -> 傾斜材 `` (2FL -872~-40)`` ／ 水平材 `` (2FL -872)``（**1 本で両方**）
//	  `#IPZS##sign#` -> `+128`（正）／ **`±0`**（0）／ `-872`（負）
//	  2 巡目で舞台も直った——**構造材のパスは 2D（`VWPolygon2DObj`）で渡す**。
//	  3D ポリゴンで渡すとストーリバウンドが黙って無視され、実体も作られない（対照で確認）。
//
//	**そこへ利用者から「タグフィールドの定義」ダイアログの実物が届いた。** ダイアログで
//	単位・精度・3 桁位取り・符号を設定すると、上段の定義欄はこう綴られる:
//
//	    #StructuralMember#.#MajorBreadth##mm_0_1#usp#thsep#sign#
//	      └ フィールド ┘└ 単位_精度 ┘└ 値と単位の間にスペース
//	                                    └ 3 桁位取り └ 負以外に符号
//
//	**これは今まで書いていた綴りより長い。** ここから 2 つが出てくる:
//
//	  1. **`#thsep#` が効かなかったのは、単位・精度の修飾子を前に置いていなかったから**
//	     ではないか。Findings は「式の側から桁区切りを付ける手は見つかっていない」と
//	     書いているが、**ダイアログがその綴りを生成している**以上、効く道がある見込みが高い。
//	     （これは #187 / #190 の測り方——修飾子を 1 つずつ単独で置いた——では出ない。）
//	  2. **`±0` を避けられるのではないか。** `#sign#` は 0 のとき `±0` を出すが、
//	     単位・精度の修飾子を挟んだ連鎖では違うかもしれない。推奨する式がここで変わる。
//
//	この版で測ること（**正の値・0・4 桁の値**を用意して引く）:
//
//	  * ダイアログの連鎖そのもの `##mm_0_1#usp#thsep#sign#` を `#IPZS#` と
//	    **レコードのフィールド**の両方に当てる。
//	  * `#thsep#` を**単独**で置いた場合（＝これまでの綴り）と**単位の後ろ**に置いた場合を
//	    並べ、桁区切りが出るのはどちらかを決める。**4 桁の値**（1234）で引く。
//	  * `mm_0_1` の文法を探る（`_` の 2 つの数は何か。精度・単位を振って見る）。
//	  * 0 のとき連鎖が `±0` を出すか。
//	  * 最終形——傾斜材の注記に連鎖を入れたものが期待どおり出るか。
//
//	舞台（2 巡目と同じ。パスは平面の 2 点・高さはストーリバウンドだけ・天端中央基準）:
//	  階 `T196-2F` の高さ **3571**、レベル `T196-FL`（階内相対Z 0）から作ったレイヤ。
//	  P: offset **+128**／O: **0**／N: **−872**／W: **+1234**（**主高さも 1234**）／
//	  S: **−872 → −40**（傾斜・始端が低い端）
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const double kProbeI196_StoryZ = 3571.0; // 階 T196-2F の高さ（＝2FL）
	const double kProbeI196_Depth = 240.0;	 // 断面のせい（既定）
	const double kProbeI196_Breadth = 105.0; // 断面の幅
	const double kProbeI196_Run = 4000.0;	 // 平面上の材長（X 方向）

	std::string ProbeI196_FromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	// **`±1.79e308` は「図形が無い」の意味**（反転した空矩形＝`DBL_MAX`）なので、
	// 桁を並べずにそう書く。数として読み違えないため。
	std::string ProbeI196_Num(double value)
	{
		if (value > 1.0e300)
			return "+DBL_MAX(図形なし)";
		if (value < -1.0e300)
			return "-DBL_MAX(図形なし)";
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		std::string text(buffer);
		while (text.size() > 1 && text[text.size() - 1] == '0')
			text.erase(text.size() - 1);
		if (!text.empty() && text[text.size() - 1] == '.')
			text.erase(text.size() - 1);
		return text;
	}

	std::string ProbeI196_OneLine(const std::string& value)
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

	void ProbeI196_SetReal(vwprobe::Report& probe, MCObjectHandle member, const char* name,
						   double value)
	{
		VWParametricObj pio(member);
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.fail(std::string("欄 ") + name + " を名前で引けなかった");
			return;
		}
		pio.SetParamReal(index, value);
	}

	void ProbeI196_SetValue(vwprobe::Report& probe, MCObjectHandle member, const char* name,
							const char* value)
	{
		VWParametricObj pio(member);
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.fail(std::string("欄 ") + name + " を名前で引けなかった");
			return;
		}
		pio.SetParamValue(index, TXString(value));
	}

	// -------------------------------------------------------------------
	// 部材を 1 本作る。**パスは 2D**（2 巡目で確定——3D ポリゴンで渡すとストーリバウンドが
	// 黙って無視され、実体も作られない）。高さはバウンドだけが与える。
	MCObjectHandle ProbeI196_MakeMember(vwprobe::Report& probe, const std::string& label,
										const TXString& levelType, double y, double majorDepth,
										double offsetStart, double offsetEnd)
	{
		VWPolygon2DObj path({VWPoint2D(0.0, y), VWPoint2D(kProbeI196_Run, y)});
		MCObjectHandle hPath = path.GetThisObject();
		if (hPath == nil)
		{
			probe.fail("部材 " + label + " のパスを作れなかった");
			return nil;
		}
		MCObjectHandle hMember =
			gSDK->CreateCustomObjectPath("StructuralMember", hPath, nil, false);
		if (hMember == nil)
		{
			probe.fail("部材 " + label + " を作れなかった（CreateCustomObjectPath が nil）");
			return nil;
		}
		gSDK->ResetObject(hMember);

		ProbeI196_SetValue(probe, hMember, "MemberType", "2"); // 2＝木（寸法 4 欄が断面になる）
		ProbeI196_SetReal(probe, hMember, "MajorBreadth", kProbeI196_Breadth);
		ProbeI196_SetReal(probe, hMember, "MajorDepth", majorDepth);
		ProbeI196_SetReal(probe, hMember, "MinorBreadth", kProbeI196_Breadth); // ＝主幅 ⇒ 矩形
		ProbeI196_SetReal(probe, hMember, "MinorDepth", majorDepth / 2.0);
		ProbeI196_SetValue(probe, hMember, "AxisAlign", "1");	   // 1＝**天端中央**基準
		ProbeI196_SetValue(probe, hMember, "StartCondition", "3"); // 3＝直切り
		ProbeI196_SetValue(probe, hMember, "EndCondition", "3");

		VectorWorks::SStoryObjectData dataStart;
		dataStart.fBound = VectorWorks::eStoryObjectBound_Story;
		dataStart.fBoundStory = 0;
		dataStart.fLayerLevelType = levelType;
		dataStart.fOffset = offsetStart;
		VectorWorks::SStoryObjectData dataEnd = dataStart;
		dataEnd.fOffset = offsetEnd;
		gSDK->SetObjectStoryBound(hMember, 0, dataStart);
		gSDK->SetObjectStoryBound(hMember, 1, dataEnd);
		gSDK->ResetObject(hMember);

		size_t solids = 0;
		for (MCObjectHandle child = gSDK->FirstMemberObj(hMember); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (gSDK->GetObjectTypeN(child) == 84)
				++solids;
		}
		VWParametricObj pio(hMember);
		WorldCube cube;
		gSDK->GetObjectCube(hMember, cube);
		probe.log("部材 " + label + ": " + (solids != 0 ? "実体84=有" : "**実体84=無**") +
				  " 挿入点Z=" + ProbeI196_Num(pio.GetObjectModelPos().z) + " 外接Z 下=" +
				  ProbeI196_Num(double(cube.MinZ())) + " 上=" + ProbeI196_Num(double(cube.MaxZ())) +
				  " ／ 解決Z ID0=" + ProbeI196_Num(gSDK->GetObjectBoundElevation(hMember, 0)) +
				  " ID1=" + ProbeI196_Num(gSDK->GetObjectBoundElevation(hMember, 1)) +
				  " ／ 主高さ=" + ProbeI196_Num(pio.GetParamReal(TXString("MajorDepth"))));
		return hMember;
	}

	MCObjectHandle ProbeI196_BuildTag(vwprobe::Report& probe,
									  VectorWorks::Extension::IDataTagSupport* tagSupport,
									  MCObjectHandle hMember, double x, const std::string& label,
									  MCObjectHandle& outTag)
	{
		if (hMember == nil)
			return nil;
		outTag = gSDK->CreateCustomObject("Data Tag", WorldPt(x, 0.0), 0.0, true);
		if (outTag == nil)
		{
			probe.fail("データタグ（" + label + "）を作れなかった");
			return nil;
		}
		tagSupport->AssociateWithObject(outTag, hMember);

		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		MCObjectHandle hText = gSDK->CreateTextBlock("T196", WorldPt(0.0, 0.0), false, 0.0);
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

	void ProbeI196_Eval(vwprobe::Report& probe, VectorWorks::Extension::IDataTagSupport* tagSupport,
						VectorWorks::Extension::IDataTagTextLinkSupport* linkSupport,
						MCObjectHandle hTag, MCObjectHandle hText, const std::string& stage,
						const std::string& formula)
	{
		if (hTag == nil || hText == nil)
			return;
		linkSupport->SetIsLinked(hText, true);
		linkSupport->SetFormula(hText, TXString(formula.c_str()), false);
		tagSupport->UpdateUserDefinedTextsUIDs(hTag);
		tagSupport->UpdateDataTag(hTag);
		gSDK->ResetObject(hTag);

		VectorWorks::Extension::TXStringSTLPairArray extracted;
		tagSupport->GetDataTagExtractedData(hTag, extracted);
		std::string out;
		for (size_t i = 0; i < extracted.size(); ++i)
			out += "'" + ProbeI196_OneLine(ProbeI196_FromTX(extracted[i].second)) + "'";
		if (extracted.empty())
			out = "(0 件)";
		probe.log("[" + stage + "] " + ProbeI196_OneLine(formula) + " -> " + out);
	}

	void ProbeI196_EvalAll(vwprobe::Report& probe,
						   VectorWorks::Extension::IDataTagSupport* tagSupport,
						   VectorWorks::Extension::IDataTagTextLinkSupport* linkSupport,
						   MCObjectHandle hTag, MCObjectHandle hText, const std::string& stage,
						   const char* const* formulas, size_t count)
	{
		for (size_t i = 0; i < count; ++i)
			ProbeI196_Eval(probe, tagSupport, linkSupport, hTag, hText, stage, formulas[i]);
	}

	// **ダイアログが生成する連鎖**と、これまで書いていた綴りを並べる。
	// `#IPZS#`（レコードのフィールドでない綴り）に当てる版。
	const char* const kProbeI196_ChainCases[] = {
		"#IPZS#",		 // 基準
		"#IPZS##sign#",	 // これまでの綴り（2 巡目で効くと確定。0 で `±0`）
		"#IPZS##thsep#", // **これまでの綴り**（#187 では桁区切りが出なかった）
		"#IPZS##mm_0_1#",				 // **ダイアログの単位・精度の修飾子**だけ
		"#IPZS##mm_0_1#sign#",			 // 単位 ＋ 符号
		"#IPZS##mm_0_1#thsep#",			 // **単位 ＋ 桁区切り**（本題 1）
		"#IPZS##mm_0_1#thsep#sign#",	 // 単位 ＋ 桁区切り ＋ 符号
		"#IPZS##mm_0_1#usp#thsep#sign#", // **ダイアログの連鎖そのもの**
		"\" (2FL \"#IPZS##mm_0_1#sign#\")\"", // 注記に入れた形
		"\" (2FL \"#IPZS##mm_0_1#thsep#sign#\")\""};

	// **レコードのフィールド**に当てる版（ダイアログが出していたのはこちら）。
	const char* const kProbeI196_FieldCases[] = {
		"#StructuralMember#.#MajorDepth#",
		"#StructuralMember#.#MajorDepth##thsep#", // #187 で桁区切りが出なかった綴り
		"#StructuralMember#.#MajorDepth##mm_0_1#",
		"#StructuralMember#.#MajorDepth##mm_0_1#thsep#",		  // **本題 1**
		"#StructuralMember#.#MajorDepth##mm_0_1#usp#thsep#sign#", // **ダイアログそのもの**
		"#StructuralMember#.#MajorBreadth##mm_0_1#usp#thsep#sign#"};

	// `mm_0_1` の**文法**を探る。`_` の 2 つの数が何か・単位の綴りが何を受けるか。
	const char* const kProbeI196_UnitCases[] = {
		"#IPZS##mm_0_0#", "#IPZS##mm_0_1#",	   "#IPZS##mm_0_2#", "#IPZS##mm_0_3#",
		"#IPZS##mm_1_1#", "#IPZS##mm_2_1#",	   "#IPZS##m_0_1#",	 "#IPZS##cm_0_1#",
		"#IPZS##mm_0_1#", "#IPZS##mm#",		   "#IPZS##mm_0#",	 "#IPZS##t196nosuch_0_1#",
		"#IPZS##usp#",	  "#IPZS##mm_0_1#usp#"};

	// **最終形。** 傾斜材の注記に連鎖を入れたもの（水平材では条件で畳まれる）。
	const char* const kProbeI196_FinalCases[] = {
		"#IPZS#", "#ZTBBS#",
		"\" (2FL \"#IPZS#\"~\"@#ZTBBS#<>#IPZS#:\"\"#ZTBBS#@#ZTBBS#<>#IPZS#:\"\"\")\"",
		"\" (2FL "
		"\"#IPZS##mm_0_1#sign#\"~\"@#ZTBBS#<>#IPZS#:\"\"#ZTBBS##mm_0_1#sign#@#ZTBBS#<>#IPZS#:"
		"\"\"\")\"",
		"\" (2FL \"#IPZS##sign#\"~\"@#ZTBBS#<>#IPZS#:\"\"#ZTBBS##sign#@#ZTBBS#<>#IPZS#:\"\"\")\""};
} // namespace

VW_PROBE("datatag-slope-top-elevation", "傾斜材の天端と符号を測る",
		 "タグフィールド定義ダイアログが出す修飾子の連鎖が式でも効くかを測る（3 巡目）")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	// =======================================================================
	// 1. 舞台（2 巡目と同じ）。
	// =======================================================================
	probe.log("=== 1. 舞台 ===");

	TXString levelType("T196-FL");
	gSDK->CreateLayerLevelType(levelType);

	TXString storyName("T196-2F");
	TXString storySuffix("T196A");
	gSDK->CreateStory(storyName, storySuffix);
	MCObjectHandle hStory = gSDK->GetNamedObject("T196-2F");
	if (hStory == nil)
	{
		probe.fail("階 T196-2F を作れなかった");
		return;
	}
	gSDK->SetStoryElevation(hStory, kProbeI196_StoryZ);

	short index = -1;
	TXString templateName("T196-TPL-FL");
	gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelType, 0.0, 2400.0, index);
	short byType = -1;
	const short templateCount = gSDK->GetNumStoryLevelTemplates();
	for (short i = 0; i <= templateCount && byType < 0; ++i)
	{
		TXString name;
		TXString type;
		double scale = 0.0;
		double offset = 0.0;
		double wallHeight = 0.0;
		if (gSDK->GetStoryLevelTemplateInfo(i, name, scale, type, offset, wallHeight) &&
			ProbeI196_FromTX(type) == ProbeI196_FromTX(levelType))
			byType = i;
	}
	MCObjectHandle hLayer = nil;
	if (byType >= 0)
	{
		gSDK->AddStoryLevelFromTemplate(hStory, byType);
		hLayer = gSDK->GetLayerForStory(hStory, levelType);
	}
	if (hLayer == nil)
	{
		probe.fail("2F のレイヤを取れなかった");
		return;
	}
	gSDK->SetCurrentLayer(hLayer);
	probe.log("階 T196-2F の高さ=" + ProbeI196_Num(gSDK->GetStoryElevation(hStory)) +
			  " 階内相対Z=" + ProbeI196_Num(gSDK->GetStoryLevelElevation(hStory, levelType)));

	MCObjectHandle hMemberP =
		ProbeI196_MakeMember(probe, "P(正 +128)", levelType, 0.0, kProbeI196_Depth, 128.0, 128.0);
	MCObjectHandle hMemberO =
		ProbeI196_MakeMember(probe, "O(0)", levelType, 1000.0, kProbeI196_Depth, 0.0, 0.0);
	MCObjectHandle hMemberN = ProbeI196_MakeMember(probe, "N(負 -872)", levelType, 2000.0,
												   kProbeI196_Depth, -872.0, -872.0);
	// **4 桁の値**を 2 つ持たせる（`#IPZS#`=1234 と `MajorDepth`=1234）。
	// 桁区切りは 3 桁を超えないと見えない——#187 が 600 で引いて「効かない」と読んだ轍。
	MCObjectHandle hMemberW = ProbeI196_MakeMember(probe, "W(+1234・主高さ 1234)", levelType,
												   3000.0, 1234.0, 1234.0, 1234.0);
	MCObjectHandle hMemberS = ProbeI196_MakeMember(probe, "S(傾斜 -872→-40)", levelType, 4000.0,
												   kProbeI196_Depth, -872.0, -40.0);

	IDataTagSupportPtr tagSupport(IID_DataTagSupport);
	IDataTagTextLinkSupportPtr linkSupport(IID_DataTagTextLinkSupport);
	if (!tagSupport || !linkSupport)
	{
		probe.fail("IDataTagSupport / IDataTagTextLinkSupport を取れなかった");
		return;
	}

	MCObjectHandle hTagP = nil;
	MCObjectHandle hTagO = nil;
	MCObjectHandle hTagN = nil;
	MCObjectHandle hTagW = nil;
	MCObjectHandle hTagS = nil;
	MCObjectHandle hTextP = ProbeI196_BuildTag(probe, tagSupport, hMemberP, 6000.0, "P", hTagP);
	MCObjectHandle hTextO = ProbeI196_BuildTag(probe, tagSupport, hMemberO, 7000.0, "O", hTagO);
	MCObjectHandle hTextN = ProbeI196_BuildTag(probe, tagSupport, hMemberN, 8000.0, "N", hTagN);
	MCObjectHandle hTextW = ProbeI196_BuildTag(probe, tagSupport, hMemberW, 9000.0, "W", hTagW);
	MCObjectHandle hTextS = ProbeI196_BuildTag(probe, tagSupport, hMemberS, 10000.0, "S", hTagS);

	const size_t chainCount = sizeof(kProbeI196_ChainCases) / sizeof(kProbeI196_ChainCases[0]);
	const size_t fieldCount = sizeof(kProbeI196_FieldCases) / sizeof(kProbeI196_FieldCases[0]);
	const size_t unitCount = sizeof(kProbeI196_UnitCases) / sizeof(kProbeI196_UnitCases[0]);
	const size_t finalCount = sizeof(kProbeI196_FinalCases) / sizeof(kProbeI196_FinalCases[0]);

	// =======================================================================
	// 2. **ダイアログの連鎖を `#IPZS#` に当てる。** 4 桁（W）・正（P）・0（O）で引く。
	//	W の `#IPZS#` は **1234** なので、**桁区切りが出れば `1,234` になる**。
	// =======================================================================
	probe.log("=== 2. 修飾子の連鎖（#IPZS# に当てる） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagW, hTextW, "段2-W(1234)",
					  kProbeI196_ChainCases, chainCount);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagP, hTextP, "段2-P(128)",
					  kProbeI196_ChainCases, chainCount);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagO, hTextO, "段2-O(0)",
					  kProbeI196_ChainCases, chainCount);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagN, hTextN, "段2-N(-872)",
					  kProbeI196_ChainCases, chainCount);

	// =======================================================================
	// 3. **レコードのフィールドに当てる**（ダイアログが出していたのはこの形）。
	//	W の `MajorDepth` は **1234**。#187 は 600 と 1234 で `##thsep#` を引いて
	//	「桁区切りが出ない」と読んだが、**単位の修飾子を前に置いていなかった**。
	// =======================================================================
	probe.log("=== 3. 修飾子の連鎖（レコードのフィールドに当てる） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagW, hTextW, "段3-W(主高さ 1234)",
					  kProbeI196_FieldCases, fieldCount);

	// =======================================================================
	// 4. **`mm_0_1` の文法。** `_` の 2 つの数が何か、単位の綴りが何を受けるか。
	//	W（1234）で引くので、精度が変われば小数点以下の桁数で見える。
	// =======================================================================
	probe.log("=== 4. 単位・精度の綴りの文法 ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagW, hTextW, "段4-W(1234)",
					  kProbeI196_UnitCases, unitCount);

	// =======================================================================
	// 5. **最終形。** 傾斜材の注記（条件で畳む 1 本）に連鎖を入れたもの。
	//	S では `` (2FL -872~-40)``、水平材 N では `` (2FL -872)`` になるはず。
	// =======================================================================
	probe.log("=== 5. 最終形（傾斜材 S） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagS, hTextS, "段5-S(傾斜)",
					  kProbeI196_FinalCases, finalCount);
	probe.log("=== 6. 最終形（水平材 N。畳まれるはず） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagN, hTextN, "段6-N(水平)",
					  kProbeI196_FinalCases, finalCount);
	probe.log("=== 7. 最終形（水平材 P＝正の値。符号が付くか） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagP, hTextP, "段7-P(正)",
					  kProbeI196_FinalCases, finalCount);

	probe.log("=== 終わり ===");
}
