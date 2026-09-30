//
//	probes/runtime/datatag-thsep-without-unit/probe.cpp
//
//	[issue #198] データタグのタグフィールドの式で、**単位記号を出さずに 3 桁の桁区切りだけ**
//	を出せるか——`#<綴り>##mm_0_0#thsep#` が `1,234` になるかを実機で決める。
//
//	**取り残されている 1 点だけを埋める調査である。** [#196](…/issues/196) の 3 巡目
//	（ビルド `09d9f325f521`）で次の 2 つは実測できたが、**その 2 つを組み合わせた形は
//	一度も引いていない**（#196 の範囲＝傾斜材の天端と `#sign#` の外だったため）:
//
//	    `#IPZS##mm_0_0#`       -> `1234`    （**単位記号を出さない**。3 つ目の数が旗）
//	    `#IPZS##mm_0_1#thsep#` -> `1,234mm` （**桁区切りが出る**。ただし `mm` が付く）
//	    `#IPZS##thsep#`        -> `1234`    （単位・精度の修飾子が無いと区切らない）
//
//	だから問いは 1 つに絞れる——**旗を 0 にしても `#thsep#` は効くのか。**
//	効けば `1,234`（単位記号なし・桁区切りあり）が出る。効かなければ
//	**桁区切りと単位記号は外せない組**だという結論になり、注記に桁区切りを入れたいなら
//	`mm` を受け入れるか、式の側で諦めるかの二択になる。どちらに転んでも結論は確定する。
//
//	ついでに同じ 1 回で採る（#198「期待する成果」2）:
//
//	  * **`usp`（値と単位の間のスペース）は旗 0 のときどうなるか。** 単位記号が出ないなら
//	    置き場が無いので、末尾に空白が 1 つ残るのか消えるのか。**ログは値を `'…'` で
//	    括って出す**ので、末尾の空白はそこで見える。
//	  * **単位を図面の単位と変えたとき（`#m_0_0#`）、換算されたまま単位記号だけ落ちるか。**
//	    落ちるなら、`1234`（mm）が黙って `1` になる——**単位記号が無いので読み手は
//	    1/1000 になったことに気付けない**。落とし穴として書く価値がある。
//	    換算されている確証を取るため、**精度を上げた形（`#m_3_0#`）も並べて引く**
//	    （`1.234` が出れば「値は m へ換算された上で単位記号だけ落ちた」と確定する）。
//	  * **桁区切りの群が 2 つ以上になる値**（1234567）でも同じか。ここで
//	    `#mm_0_0#thsep#` と `#m_0_0#thsep#` を並べると、**同じ式が `1,234,567` と
//	    `1,235` に化ける**のが 1 行で見える。
//	  * **修飾子の並び順を入れ替えても同じか**（`#thsep#usp#sign#` の順）。ダイアログが
//	    出す順（`usp` → `thsep` → `sign`）でしか効かないなら、それも結論に要る。
//	  * **綴りの種類に依らないか。** `#IPZS#`（レコードのフィールドでない綴り）と
//	    `#StructuralMember#.#MajorDepth#`（レコードのフィールド）の両方に当てる。
//
//	舞台（#196 の 3 巡目と同じ組み方。**パスは 2D**・高さはストーリバウンドだけ・
//	天端中央基準。名前は衝突を避けて `T198-*`）:
//
//	    階 `T198-2F` の高さ **3571** ／ レベル `T198-FL`（階内相対Z 0）から作ったレイヤ
//	    A: offset **+1234**    → `#IPZS#` = **1234**（4 桁＝群が 1 つ。**本題**）
//	    B: offset **+1234567** → `#IPZS#` = **1234567**（7 桁＝群が 2 つ）
//	    C: offset **−1234**    → `#IPZS#` = **−1234**（負）
//	    D: offset **0**        → `#IPZS#` = **0**（`#sign#` が `±0` を出す値）
//	    A の `MajorDepth` は **1234**（レコードのフィールドでも同じ 4 桁で引くため）
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	const double kProbeI198_StoryZ = 3571.0;		// 階 T198-2F の高さ
	const double kProbeI198_Breadth = 105.0;		// 断面の幅
	const double kProbeI198_Run = 4000.0;			// 平面上の材長（X 方向）
	const double kProbeI198_Depth = 240.0;			// 断面のせい（既定）
	const double kProbeI198_FourDigit = 1234.0;		// 群が 1 つになる値
	const double kProbeI198_SevenDigit = 1234567.0; // 群が 2 つになる値

	std::string ProbeI198_FromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string ProbeI198_Num(double value)
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

	// 改行・タブは見える形に畳む。**空白はそのまま残す**——`usp` の効き目が末尾の
	// 空白として出るので、潰すと本題の一部が読めなくなる（値は `'…'` で括って出す）。
	std::string ProbeI198_OneLine(const std::string& value)
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

	void ProbeI198_SetReal(vwprobe::Report& probe, MCObjectHandle member, const char* name,
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

	void ProbeI198_SetValue(vwprobe::Report& probe, MCObjectHandle member, const char* name,
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

	// 部材を 1 本作る。**パスは 2D で渡す**（3D ポリゴンで渡すとストーリバウンドが黙って
	// 無視され、実体も作られない。#196 の 2 巡目で確定）。高さはバウンドだけが与える。
	MCObjectHandle ProbeI198_MakeMember(vwprobe::Report& probe, const std::string& label,
										const TXString& levelType, double y, double majorDepth,
										double offset)
	{
		VWPolygon2DObj path({VWPoint2D(0.0, y), VWPoint2D(kProbeI198_Run, y)});
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

		ProbeI198_SetValue(probe, hMember, "MemberType", "2"); // 2＝木（寸法 4 欄が断面になる）
		ProbeI198_SetReal(probe, hMember, "MajorBreadth", kProbeI198_Breadth);
		ProbeI198_SetReal(probe, hMember, "MajorDepth", majorDepth);
		ProbeI198_SetReal(probe, hMember, "MinorBreadth", kProbeI198_Breadth);
		ProbeI198_SetReal(probe, hMember, "MinorDepth", majorDepth / 2.0);
		ProbeI198_SetValue(probe, hMember, "AxisAlign", "1");	   // 1＝天端中央基準
		ProbeI198_SetValue(probe, hMember, "StartCondition", "3"); // 3＝直切り
		ProbeI198_SetValue(probe, hMember, "EndCondition", "3");

		VectorWorks::SStoryObjectData data;
		data.fBound = VectorWorks::eStoryObjectBound_Story;
		data.fBoundStory = 0;
		data.fLayerLevelType = levelType;
		data.fOffset = offset;
		gSDK->SetObjectStoryBound(hMember, 0, data);
		gSDK->SetObjectStoryBound(hMember, 1, data);
		gSDK->ResetObject(hMember);

		size_t solids = 0;
		for (MCObjectHandle child = gSDK->FirstMemberObj(hMember); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (gSDK->GetObjectTypeN(child) == 84)
				++solids;
		}
		VWParametricObj pio(hMember);
		probe.log("部材 " + label + ": " + (solids != 0 ? "実体84=有" : "**実体84=無**") +
				  " 挿入点Z=" + ProbeI198_Num(pio.GetObjectModelPos().z) +
				  " ／ 解決Z ID0=" + ProbeI198_Num(gSDK->GetObjectBoundElevation(hMember, 0)) +
				  " ／ 主高さ=" + ProbeI198_Num(pio.GetParamReal(TXString("MajorDepth"))));
		return hMember;
	}

	MCObjectHandle ProbeI198_BuildTag(vwprobe::Report& probe,
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
		MCObjectHandle hText = gSDK->CreateTextBlock("T198", WorldPt(0.0, 0.0), false, 0.0);
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

	void ProbeI198_Eval(vwprobe::Report& probe, VectorWorks::Extension::IDataTagSupport* tagSupport,
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
			out += "'" + ProbeI198_OneLine(ProbeI198_FromTX(extracted[i].second)) + "'";
		if (extracted.empty())
			out = "(0 件)";
		probe.log("[" + stage + "] " + ProbeI198_OneLine(formula) + " -> " + out);
	}

	void ProbeI198_EvalAll(vwprobe::Report& probe,
						   VectorWorks::Extension::IDataTagSupport* tagSupport,
						   VectorWorks::Extension::IDataTagTextLinkSupport* linkSupport,
						   MCObjectHandle hTag, MCObjectHandle hText, const std::string& stage,
						   const char* const* formulas, size_t count)
	{
		for (size_t i = 0; i < count; ++i)
			ProbeI198_Eval(probe, tagSupport, linkSupport, hTag, hText, stage, formulas[i]);
	}

	// **本題。** 旗（3 つ目の数）を 0 にしたまま `#thsep#` を足せるか。
	// 対照として、旗 1（単位記号あり）・修飾子なし・`#thsep#` 単独を同じ値で並べる。
	const char* const kProbeI198_MainCases[] = {
		"#IPZS#",		  // 素の値
		"#IPZS##thsep#",  // 単位・精度の修飾子なし（#196 で区切らないと確定）
		"#IPZS##mm_0_0#", // 旗 0＝単位記号なし（#196 で確定）
		"#IPZS##mm_0_1#thsep#",		 // 旗 1 ＋ 桁区切り（#196 で `1,234mm` と確定）
		"#IPZS##mm_0_0#thsep#",		 // **本題**——旗 0 ＋ 桁区切り
		"#IPZS##mm_0_0#sign#",		 // 旗 0 ＋ 符号
		"#IPZS##mm_0_0#thsep#sign#", // 旗 0 ＋ 桁区切り ＋ 符号
		"#IPZS##mm_0_0#usp#", // 旗 0 ＋ **単位の前のスペース**（置き場が無い）
		"#IPZS##mm_0_0#usp#thsep#sign#", // ダイアログの連鎖の旗を 0 にしたもの
		"#IPZS##mm_0_0#thsep#usp#sign#", // **並び順を入れ替えたもの**
		"#IPZS##mm_2_0#thsep#",			 // 旗 0 ＋ 精度 2 ＋ 桁区切り
		"\" (2FL \"#IPZS##mm_0_0#thsep#\")\""}; // 注記に入れた形（連結だけ）

	// **単位を図面の単位（mm）と変えたとき。** 旗 0 で単位記号が落ちるなら、
	// 換算された数だけが黙って出る。精度を上げた形を並べて換算の確証を取る。
	const char* const kProbeI198_UnitCases[] = {
		"#IPZS##mm_0_0#",	   // mm・旗 0（基準）
		"#IPZS##m_0_1#",	   // m・旗 1（#196 で `1m` と確定）
		"#IPZS##m_0_0#",	   // **m・旗 0**——単位記号なしで換算値だけ出るか
		"#IPZS##m_3_0#",	   // m・旗 0・精度 3——`1.234` なら換算の確証
		"#IPZS##m_3_1#",	   // m・旗 1・精度 3（対照）
		"#IPZS##m_0_0#thsep#", // m・旗 0 ＋ 桁区切り
		"#IPZS##cm_0_0#",	   // cm・旗 0
		"#IPZS##cm_1_0#",	   // cm・旗 0・精度 1
		"#IPZS##mm_0_0#thsep#"}; // mm・旗 0 ＋ 桁区切り（同じ値・同じ式の並べ比べ）

	// **レコードのフィールドに当てる版**（答えが綴りの種類に依らないかを見る）。
	const char* const kProbeI198_FieldCases[] = {
		"#StructuralMember#.#MajorDepth#", "#StructuralMember#.#MajorDepth##mm_0_0#",
		"#StructuralMember#.#MajorDepth##mm_0_0#thsep#", // **本題（フィールド版）**
		"#StructuralMember#.#MajorDepth##mm_0_1#thsep#",
		"#StructuralMember#.#MajorDepth##mm_0_0#usp#thsep#sign#"};
} // namespace

VW_PROBE("datatag-thsep-without-unit", "単位記号なしの桁区切りを測る",
		 "旗を 0 にしたまま #thsep# が効くかを測る")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	// =======================================================================
	// 1. 舞台（#196 の 3 巡目と同じ組み方）。
	// =======================================================================
	probe.log("=== 1. 舞台 ===");

	TXString levelType("T198-FL");
	gSDK->CreateLayerLevelType(levelType);

	TXString storyName("T198-2F");
	TXString storySuffix("T198A");
	gSDK->CreateStory(storyName, storySuffix);
	MCObjectHandle hStory = gSDK->GetNamedObject("T198-2F");
	if (hStory == nil)
	{
		probe.fail("階 T198-2F を作れなかった");
		return;
	}
	gSDK->SetStoryElevation(hStory, kProbeI198_StoryZ);

	short index = -1;
	TXString templateName("T198-TPL-FL");
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
			ProbeI198_FromTX(type) == ProbeI198_FromTX(levelType))
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
	probe.log("階 T198-2F の高さ=" + ProbeI198_Num(gSDK->GetStoryElevation(hStory)) +
			  " 階内相対Z=" + ProbeI198_Num(gSDK->GetStoryLevelElevation(hStory, levelType)));

	// A は `MajorDepth` も 1234 にしておく（レコードのフィールド版で同じ 4 桁を引くため）。
	MCObjectHandle hMemberA = ProbeI198_MakeMember(probe, "A(+1234・主高さ 1234)", levelType, 0.0,
												   kProbeI198_FourDigit, kProbeI198_FourDigit);
	MCObjectHandle hMemberB = ProbeI198_MakeMember(probe, "B(+1234567)", levelType, 1000.0,
												   kProbeI198_Depth, kProbeI198_SevenDigit);
	MCObjectHandle hMemberC = ProbeI198_MakeMember(probe, "C(-1234)", levelType, 2000.0,
												   kProbeI198_Depth, -kProbeI198_FourDigit);
	MCObjectHandle hMemberD =
		ProbeI198_MakeMember(probe, "D(0)", levelType, 3000.0, kProbeI198_Depth, 0.0);

	IDataTagSupportPtr tagSupport(IID_DataTagSupport);
	IDataTagTextLinkSupportPtr linkSupport(IID_DataTagTextLinkSupport);
	if (!tagSupport || !linkSupport)
	{
		probe.fail("IDataTagSupport / IDataTagTextLinkSupport を取れなかった");
		return;
	}

	MCObjectHandle hTagA = nil;
	MCObjectHandle hTagB = nil;
	MCObjectHandle hTagC = nil;
	MCObjectHandle hTagD = nil;
	MCObjectHandle hTextA = ProbeI198_BuildTag(probe, tagSupport, hMemberA, 6000.0, "A", hTagA);
	MCObjectHandle hTextB = ProbeI198_BuildTag(probe, tagSupport, hMemberB, 7000.0, "B", hTagB);
	MCObjectHandle hTextC = ProbeI198_BuildTag(probe, tagSupport, hMemberC, 8000.0, "C", hTagC);
	MCObjectHandle hTextD = ProbeI198_BuildTag(probe, tagSupport, hMemberD, 9000.0, "D", hTagD);

	const size_t mainCount = sizeof(kProbeI198_MainCases) / sizeof(kProbeI198_MainCases[0]);
	const size_t unitCount = sizeof(kProbeI198_UnitCases) / sizeof(kProbeI198_UnitCases[0]);
	const size_t fieldCount = sizeof(kProbeI198_FieldCases) / sizeof(kProbeI198_FieldCases[0]);

	// =======================================================================
	// 2. **本題。** A の `#IPZS#` は 1234 なので、桁区切りが出れば `1,234` になる。
	// =======================================================================
	probe.log("=== 2. 本題: 旗 0 ＋ #thsep#（A＝1234） ===");
	ProbeI198_EvalAll(probe, tagSupport, linkSupport, hTagA, hTextA, "段2-A(1234)",
					  kProbeI198_MainCases, mainCount);

	// =======================================================================
	// 3. **群が 2 つになる値**（B＝1234567）。区切りが 2 つ入るか。
	// =======================================================================
	probe.log("=== 3. 群が 2 つ（B＝1234567） ===");
	ProbeI198_EvalAll(probe, tagSupport, linkSupport, hTagB, hTextB, "段3-B(1234567)",
					  kProbeI198_MainCases, mainCount);

	// =======================================================================
	// 4. 負（C＝−1234）と 0（D）。`-1,234` になるか・`±0` はどうなるか。
	// =======================================================================
	probe.log("=== 4. 負（C＝-1234） ===");
	ProbeI198_EvalAll(probe, tagSupport, linkSupport, hTagC, hTextC, "段4-C(-1234)",
					  kProbeI198_MainCases, mainCount);
	probe.log("=== 5. 0（D） ===");
	ProbeI198_EvalAll(probe, tagSupport, linkSupport, hTagD, hTextD, "段5-D(0)",
					  kProbeI198_MainCases, mainCount);

	// =======================================================================
	// 6. **単位を変えたとき。** 旗 0 で `#m_0_0#` が何を出すか（換算値か・素の値か）。
	//	A（1234mm＝1.234m）と B（1234567mm＝1234.567m）の両方で引く。
	// =======================================================================
	probe.log("=== 6. 単位違い（A＝1234mm＝1.234m） ===");
	ProbeI198_EvalAll(probe, tagSupport, linkSupport, hTagA, hTextA, "段6-A(1234mm)",
					  kProbeI198_UnitCases, unitCount);
	probe.log("=== 7. 単位違い（B＝1234567mm＝1234.567m） ===");
	ProbeI198_EvalAll(probe, tagSupport, linkSupport, hTagB, hTextB, "段7-B(1234567mm)",
					  kProbeI198_UnitCases, unitCount);

	// =======================================================================
	// 8. **レコードのフィールドに当てる**（A の `MajorDepth`＝1234）。
	// =======================================================================
	probe.log("=== 8. レコードのフィールド（A の主高さ＝1234） ===");
	ProbeI198_EvalAll(probe, tagSupport, linkSupport, hTagA, hTextA, "段8-A(主高さ 1234)",
					  kProbeI198_FieldCases, fieldCount);

	probe.log("=== 終わり ===");
}
