//
//	probes/runtime/datatag-slope-top-elevation/probe.cpp
//
//	[issue #196] データタグで**構造材の天端の高さ**を材に連動して出すために、残った 2 点を
//	実機で測る。舞台は**プラグインが実際に作る形**——パスは平面（Z=0）の 2 点、高さは
//	始端・終端とも `SetObjectStoryBound` だけ（レベル ＋ オフセット）、断面は天端中央基準
//	（`AxisAlign`=1）、材はストーリのレベルから作ったレイヤに置く。
//
//	1. **`#IPZS#` の直後に `#sign#` は効くか。** `#sign#` が**レコードのフィールド**の
//	   直後で効くことは #190 で実測済みだが、`#IPZS#` のような「レコードのフィールドでない
//	   綴り」の直後では一度も試されていない。**正の値で引く**（負の値では効き目が見えない
//	   ——#190 の教訓）。0 のときに何が出るかも見る。効かないときの代わり
//	   （条件式 `"+"@<式>>0:""<式>`）が `#IPZS#` でも書けるかを同じ走行で測る。
//	2. **傾斜材の両端の天端を、それぞれ材に連動する形で読めるか。** 欲しい注記は
//	   `` (2FL -872~-40)``（低い端〜高い端）。始端は `#IPZS#` で読めるが、**もう一方の端**を
//	   読む綴りが要る。候補は `#ZTBBS#`（バウンディングボックス上面_ストーリ基準）で、
//	   #187 では**実体の無い構造材で `±1.79e308`（＝`DBL_MAX`）のまま**だった
//	   ——ここでは**バウンドを両端に書くので実体が作られる**。
//
//	舞台の数値（#187 と同じ現場の形にしてある）:
//
//	  階 `T196-2F` の高さ **3571**、その階のレベル `T196-FL`（階内相対Z 0）から作ったレイヤ。
//	  部材の断面は **105 × 240**（横架材）、断面基準点は**天端中央**。
//
//	  部材 P（水平・**正**）  : 両端 offset **+128**  → 絶対Z 3699  → `#IPZS#` = **+128**
//	  部材 O（水平・**0**）   : 両端 offset **0**     → 絶対Z 3571  → `#IPZS#` = **0**
//	  部材 N（水平・**負**）  : 両端 offset **−872**  → 絶対Z 2699  → `#IPZS#` = **−872**
//	  部材 S（傾斜・**始端が低い**）: ID0 −872 / ID1 −40 → `#IPZS#` = −872、高い端の天端は −40
//	  部材 H（傾斜・**始端が高い**）: ID0 −40 / ID1 −872 → `#IPZS#` = −40（低い端を読む口は？）
//
//	**式の答えを目視に頼らない**——値は `GetDataTagExtractedData` で読み戻し、
//	`#ZTBB…#` / `#ZBBB…#` が何を指しているかは `GetObjectCube`（3D の外接）と
//	突き合わせる。`GetObjectBoundElevation` で両端の解決Zも出すので、**ログだけで
//	検算できる**。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const double kProbeI196_StoryZ = 3571.0; // 階 T196-2F の高さ（＝2FL）
	const double kProbeI196_Depth = 240.0;	 // 断面のせい
	const double kProbeI196_Breadth = 105.0; // 断面の幅
	const double kProbeI196_Run = 4000.0;	 // 平面上の材長（X 方向）

	std::string ProbeI196_FromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string ProbeI196_Num(double value)
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

	// -------------------------------------------------------------------
	// **プラグインと同じ作り方**で構造材を 1 本作る。パスは平面（Z=0）の 2 点で、
	// 高さは `SetObjectStoryBound` だけが決める（始端＝ID 0 / 終端＝ID 1。
	// [Findings「Parametric Objects」](../../../Findings/Parametric%20Objects.md)）。
	MCObjectHandle ProbeI196_MakeMember(vwprobe::Report& probe, const std::string& label,
										MCObjectHandle hLayer, const TXString& levelType, double y,
										double offsetStart, double offsetEnd)
	{
		MCObjectHandle hPath = gSDK->Create3DPoly();
		if (hPath != nil)
		{
			gSDK->Add3DVertex(hPath, WorldPt3(0.0, y, 0.0));
			gSDK->Add3DVertex(hPath, WorldPt3(kProbeI196_Run, y, 0.0));
		}
		MCObjectHandle hMember = gSDK->CreateCustomObjectPath("StructuralMember", hPath, nil, true);
		if (hMember == nil)
		{
			probe.fail("部材 " + label + " を作れなかった（CreateCustomObjectPath が nil）");
			return nil;
		}
		if (hLayer != nil)
			gSDK->AddObjectToContainer(hMember, hLayer);

		VWFC::VWObjects::VWParametricObj pio(hMember);
		pio.SetParamValue("MemberType", "2"); // 2＝木（寸法 4 欄がそのまま断面になる）
		pio.SetParamValue("AxisAlign", "1");	  // 1＝**天端中央**基準
		pio.SetParamValue("StartCondition", "3"); // 3＝直切り（材軸に直角）
		pio.SetParamValue("EndCondition", "3");
		pio.SetParamReal("MajorBreadth", kProbeI196_Breadth);
		pio.SetParamReal("MajorDepth", kProbeI196_Depth);
		pio.SetParamReal("MinorBreadth", kProbeI196_Breadth); // ＝主幅 ⇒ 無垢の矩形
		pio.SetParamReal("MinorDepth", kProbeI196_Depth / 2.0);

		VectorWorks::SStoryObjectData dataStart;
		dataStart.fBound = VectorWorks::eStoryObjectBound_Story;
		dataStart.fBoundStory = 0;
		dataStart.fLayerLevelType = levelType;
		dataStart.fOffset = offsetStart;
		VectorWorks::SStoryObjectData dataEnd = dataStart;
		dataEnd.fOffset = offsetEnd;
		const bool okStart = gSDK->SetObjectStoryBound(hMember, 0, dataStart);
		const bool okEnd = gSDK->SetObjectStoryBound(hMember, 1, dataEnd);
		gSDK->ResetObject(hMember);

		// **書いたら数えて読む。** 解決Zが上下で違わなければ材は 0 長になる（#59）。
		probe.log("部材 " + label + ": バウンド書き込み=" + (okStart ? "ok" : "**失敗**") + "/" +
				  (okEnd ? "ok" : "**失敗**") +
				  " 件数=" + ProbeI196_Num(double(gSDK->GetObjectStoryBoundsCount(hMember))) +
				  " 解決Z ID0=" + ProbeI196_Num(gSDK->GetObjectBoundElevation(hMember, 0)) +
				  " ID1=" + ProbeI196_Num(gSDK->GetObjectBoundElevation(hMember, 1)));
		return hMember;
	}

	// 実体が作られたか・3D の外接がどこにあるかを出す。**`#ZTBB…#` / `#ZBBB…#` が
	// 何を指しているかは、この外接と突き合わせて読む。**
	void ProbeI196_LogGeometry(vwprobe::Report& probe, const std::string& label,
							   MCObjectHandle hMember)
	{
		if (hMember == nil)
			return;
		VWFC::VWObjects::VWParametricObj pio(hMember);
		const VWFC::Math::VWPoint3D pos = pio.GetObjectModelPos();
		WorldCube cube;
		gSDK->GetObjectCube(hMember, cube);
		probe.log("部材 " + label + ": 挿入点Z=" + ProbeI196_Num(pos.z) +
				  " 外接 left=" + ProbeI196_Num(cube.left) + " right=" + ProbeI196_Num(cube.right) +
				  " bottom=" + ProbeI196_Num(cube.bottom) + " top=" + ProbeI196_Num(cube.top) +
				  " back=" + ProbeI196_Num(cube.back) + " front=" + ProbeI196_Num(cube.front));

		MCObjectHandle hPath = gSDK->GetCustomObjectPath(hMember);
		if (hPath != nil)
		{
			WorldPt3 v0;
			WorldPt3 v1;
			gSDK->Get3DVertex(hPath, 1, v0);
			gSDK->Get3DVertex(hPath, 2, v1);
			probe.log("部材 " + label + ": 作り直し後のパス 始端Z=" + ProbeI196_Num(pos.z + v0.z) +
					  " 終端Z=" + ProbeI196_Num(pos.z + v1.z) + "（挿入点＋パスZ）");
		}
	}

	MCObjectHandle ProbeI196_BuildTag(vwprobe::Report& probe,
									  VectorWorks::Extension::IDataTagSupport* tagSupport,
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

	void ProbeI196_SetOffsets(MCObjectHandle hMember, const TXString& levelType, double offsetStart,
							  double offsetEnd)
	{
		if (hMember == nil)
			return;
		VectorWorks::SStoryObjectData data;
		data.fBound = VectorWorks::eStoryObjectBound_Story;
		data.fBoundStory = 0;
		data.fLayerLevelType = levelType;
		data.fOffset = offsetStart;
		gSDK->SetObjectStoryBound(hMember, 0, data);
		data.fOffset = offsetEnd;
		gSDK->SetObjectStoryBound(hMember, 1, data);
		gSDK->ResetObject(hMember);
	}

	// **符号の試験**（正・0・負の水平材で同じ綴りを引く）。
	const char* const kProbeI196_SignCases[] = {
		"#IPZS#",			  // 基準
		"#IPZS##sign#",		  // **本題**: フィールドでない綴りの直後で効くか
		"#IPZS#sign#",		  // 対照: `#` 1 つ（効かないなら文字が出る）
		"#IPZS##t196nosuch#", // 対照: 知らない修飾子（黙って消えるはず）
		"\" (2FL \"#IPZS##sign#\")\"",			   // **欲しい注記そのもの**
		"\"+\"@#IPZS#>0:\"\"#IPZS#",			   // 代わりの手（条件式）
		"\" (2FL \"\"+\"@#IPZS#>0:\"\"#IPZS#\")\"" // 条件式版の注記そのもの
	};

	// **傾斜材の試験**（両端の天端を読む口を探す）。
	const char* const kProbeI196_SlopeCases[] = {
		"#IPZ#",  // 挿入点の絶対Z
		"#IPZS#", // 挿入点_ストーリ基準（＝始端の天端のはず）
		"#IPZL#", // 挿入点_レイヤ基準
		"#ZTBBG#", // 外接**上面**_基準平面（＝絶対Z。`GetObjectCube` と突き合わせる）
		"#ZTBBS#", // 外接**上面**_ストーリ基準（＝高い端の天端か？）
		"#ZTBBL#", // 外接上面_レイヤ基準
		"#ZBBBG#", // 外接**底面**_基準平面
		"#ZBBBS#", // 外接**底面**_ストーリ基準
		"#ZBBBL#", // 外接底面_レイヤ基準
		"#ST#",	   // 部材が居る階の名前
		"#StructuralMember#.#StartElevation#",
		"#StructuralMember#.#EndElevation#",
		"#StructuralMember#.#DialogStartElevation#",
		"#StructuralMember#.#DialogEndElevation#",
		"\" (2FL \"#IPZS#\"~\"#ZTBBS#\")\"", // **欲しい注記そのもの**（連結だけ）
		"\" (2FL \"#IPZS##sign#\"~\"#ZTBBS##sign#\")\"", // 同・符号付き
		"\"~\"@#ZTBBS#<>#IPZS#:\"\"", // 条件に**綴り同士の比較**を書けるか
		"\" (2FL \"#IPZS#\"~\"@#ZTBBS#<>#IPZS#:\"\"#ZTBBS#@#ZTBBS#<>#IPZS#:\"\"\")\""};
} // namespace

VW_PROBE("datatag-slope-top-elevation", "傾斜材の天端と符号を測る",
		 "傾斜した構造材の両端の天端を読む綴りと、#IPZS# の直後の #sign# が効くかを測る")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	// =======================================================================
	// 1. 舞台。階 `T196-2F`（高さ 3571）＋ そのレベル `T196-FL`（階内相対Z 0）から
	//	作ったレイヤ。**部材の高さはバウンドの offset だけが決める。**
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
			  " 階内相対Z=" + ProbeI196_Num(gSDK->GetStoryLevelElevation(hStory, levelType)) +
			  "（この 2 つの和がレイヤの絶対Z）");

	MCObjectHandle hMemberP =
		ProbeI196_MakeMember(probe, "P(水平・正 +128)", hLayer, levelType, 0.0, 128.0, 128.0);
	MCObjectHandle hMemberO =
		ProbeI196_MakeMember(probe, "O(水平・0)", hLayer, levelType, 1000.0, 0.0, 0.0);
	MCObjectHandle hMemberN =
		ProbeI196_MakeMember(probe, "N(水平・負 -872)", hLayer, levelType, 2000.0, -872.0, -872.0);
	MCObjectHandle hMemberS = ProbeI196_MakeMember(probe, "S(傾斜・始端が低い)", hLayer, levelType,
												   3000.0, -872.0, -40.0);
	MCObjectHandle hMemberH = ProbeI196_MakeMember(probe, "H(傾斜・始端が高い)", hLayer, levelType,
												   4000.0, -40.0, -872.0);

	probe.log("--- 実体（3D の外接）。**`#ZTBB…#` はこれと突き合わせて読む** ---");
	ProbeI196_LogGeometry(probe, "P", hMemberP);
	ProbeI196_LogGeometry(probe, "O", hMemberO);
	ProbeI196_LogGeometry(probe, "N", hMemberN);
	ProbeI196_LogGeometry(probe, "S", hMemberS);
	ProbeI196_LogGeometry(probe, "H", hMemberH);

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
	MCObjectHandle hTagS = nil;
	MCObjectHandle hTagH = nil;
	MCObjectHandle hTextP =
		ProbeI196_BuildTag(probe, tagSupport, hMemberP, hLayer, 6000.0, "P", hTagP);
	MCObjectHandle hTextO =
		ProbeI196_BuildTag(probe, tagSupport, hMemberO, hLayer, 7000.0, "O", hTagO);
	MCObjectHandle hTextN =
		ProbeI196_BuildTag(probe, tagSupport, hMemberN, hLayer, 8000.0, "N", hTagN);
	MCObjectHandle hTextS =
		ProbeI196_BuildTag(probe, tagSupport, hMemberS, hLayer, 9000.0, "S", hTagS);
	MCObjectHandle hTextH =
		ProbeI196_BuildTag(probe, tagSupport, hMemberH, hLayer, 10000.0, "H", hTagH);

	const size_t signCount = sizeof(kProbeI196_SignCases) / sizeof(kProbeI196_SignCases[0]);
	const size_t slopeCount = sizeof(kProbeI196_SlopeCases) / sizeof(kProbeI196_SlopeCases[0]);

	// =======================================================================
	// 2. **`#sign#` は `#IPZS#` の直後で効くか。** 正（+128）・0・負（−872）の 3 本で
	//	同じ綴りを引く。**正で `+` が出れば効いている**（負では効き目が見えない）。
	// =======================================================================
	probe.log("=== 2. #IPZS# の直後の #sign#（正 / 0 / 負） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagP, hTextP, "段2-P(正 +128)",
					  kProbeI196_SignCases, signCount);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagO, hTextO, "段2-O(0)",
					  kProbeI196_SignCases, signCount);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagN, hTextN, "段2-N(負 -872)",
					  kProbeI196_SignCases, signCount);

	// =======================================================================
	// 3. **傾斜材の両端の天端。** S は始端が低い端（`#IPZS#`=−872 のはず）、
	//	H は始端が高い端（`#IPZS#`=−40 のはず）。`#ZTBBS#` が高い端の天端 −40 と
	//	一致するか——断面の角が天端より上に出ないか——を外接と突き合わせて見る。
	// =======================================================================
	probe.log("=== 3. 傾斜材 S（始端が低い端） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagS, hTextS, "段3-S", kProbeI196_SlopeCases,
					  slopeCount);

	probe.log("=== 4. 傾斜材 H（始端が高い端。低い端を読む口はあるか） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagH, hTextH, "段4-H", kProbeI196_SlopeCases,
					  slopeCount);

	probe.log("=== 5. 水平材 N に同じ式を当てる（両端が同じ値になるはず） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagN, hTextN, "段5-N(水平)",
					  kProbeI196_SlopeCases, slopeCount);

	// =======================================================================
	// 6. **バウンドの offset を動かして追随するか。** 利用者の要望は「取り込み後に
	//	高さを変えても注記が追随すること」なので、ここが要望そのものの試験である。
	//	S を両端 −1000 下げる（−1872 / −1040）→ `#IPZS#` は **−1872**、
	//	`#ZTBBS#` は **−1040** になるはず。
	// =======================================================================
	probe.log("=== 6. バウンドの offset を −1000 して読み直す ===");
	ProbeI196_SetOffsets(hMemberS, levelType, -1872.0, -1040.0);
	ProbeI196_LogGeometry(probe, "S(offset 変更後)", hMemberS);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagS, hTextS, "段6-S(offset 変更後)",
					  kProbeI196_SlopeCases, slopeCount);

	// 片端だけ動かす（勾配そのものが変わる）。
	ProbeI196_SetOffsets(hMemberS, levelType, -1872.0, 128.0);
	ProbeI196_LogGeometry(probe, "S(片端だけ +128 へ)", hMemberS);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagS, hTextS, "段6-S(片端だけ変更)",
					  kProbeI196_SlopeCases, slopeCount);

	probe.log("=== 終わり ===");
}
