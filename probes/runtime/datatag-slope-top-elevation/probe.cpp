//
//	probes/runtime/datatag-slope-top-elevation/probe.cpp
//
//	[issue #196] データタグで**構造材の天端の高さ**を材に連動して出すための 2 点を実機で測る。
//
//	  1. `#IPZS#` の直後に `#sign#` は効くか（**正の値で**引く）。
//	  2. 実体のある**傾斜した**構造材で `#ZTBBS#`（外接上面_ストーリ基準）は何を返すか。
//	     欲しい注記は `` (2FL -872~-40)``（低い端〜高い端）。
//
//	【2 巡目】1 巡目（ビルド `3f956537df62`）は**舞台の組み方で失敗した**。取れたものと
//	失敗の中身:
//
//	  取れたもの（値に依らない部分なので生きている）
//	    `#IPZS##sign#`  -> `±0`      **修飾子として読まれている**
//	    `#IPZS#sign#`   -> `0sign`   `#` 1 つでは文字が出る（＝綴りは `##` が正しい）
//	    `#IPZS##t196nosuch#` -> `0`  知らない修飾子は消える
//	    → **`#sign#` は `#IPZS#` の直後で効く**。ただし**値が全部 0 だった**ので、
//	      正の値に `+` が付くかはまだ見えていない（`±0` は 0 のときの出力）。
//
//	  失敗の中身——**部材に高さが入らず、3D 実体も作られなかった**
//	    `GetObjectBoundElevation` は ID0/ID1 で**正しく違う値**を返した（S は 2699 / 3531）。
//	    にもかかわらず挿入点Z は**どの部材も 3571**（＝レイヤ面）で、`#IPZ#`=3571 /
//	    `#IPZS#`=0 / 外接は `±DBL_MAX`（＝図形が無い）。つまり**バウンドは書けていたのに
//	    `ResetObject` が部材をそこへ動かさず、断面も作られなかった**。
//
//	  1 巡目との違いは**パスの作り方**だと見当を付けた。1 巡目（と #187）は
//	  `Create3DPoly` ＋ `Add3DVertex` で 3D ポリゴンのパスを与えており、**どちらも実体が
//	  できていない**。一方 #173 の `member-minor-dims` は **`VWPolygon2DObj`（2D）**の
//	  パスで、**実体（型 84）ができている**。
//
//	**この版は見当に賭けない。** 部材を**梯子状に**並べ、どこで壊れるかをログだけで
//	切り分けられるようにした（どれが通っても、通った本で式が測れる）:
//
//	  K  2D パス・基本の欄だけ・**バウンド無し**       ← #173 と同じ形。通らなければ環境の問題
//	  A  K ＋ `AxisAlign`=1 / `StartCondition`=3 等    ← 足した欄が実体を壊すか
//	  P  A ＋ バウンド（両端 **+128**）               ← **正の値**。`#sign#` の本題
//	  O  A ＋ バウンド（両端 0）                      ← 0 のときの出力
//	  N  A ＋ バウンド（両端 −872）                   ← 負（対照）
//	  S  A ＋ バウンド（−872 → −40）**傾斜・始端が低い**
//	  H  A ＋ バウンド（−40 → −872）**傾斜・始端が高い**
//	  X  **3D パス**（1 巡目と同じ）＋ S と同じバウンド ← **失敗した道の対照**
//
//	舞台: 階 `T196-2F` の高さ **3571**、そのレベル `T196-FL`（階内相対Z 0）から作ったレイヤ。
//	断面は **105 × 240**（横架材）。目視は使わない——値は `GetDataTagExtractedData` で
//	読み戻し、実体の有無は**子の型 84 の数**と `GetObjectCube` で判定する。
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
	// 部材を 1 本作る。`use3DPath` で**パスの作り方だけ**を切り替える（失敗した道の対照）。
	// `extraParams` は `AxisAlign` などの追加の欄、`withBounds` はストーリバウンド。
	MCObjectHandle ProbeI196_MakeMember(vwprobe::Report& probe, const std::string& label,
										const TXString& levelType, double y, bool use3DPath,
										bool extraParams, bool withBounds, double offsetStart,
										double offsetEnd)
	{
		MCObjectHandle hPath = nil;
		if (use3DPath)
		{
			hPath = gSDK->Create3DPoly();
			if (hPath != nil)
			{
				gSDK->Add3DVertex(hPath, WorldPt3(0.0, y, 0.0));
				gSDK->Add3DVertex(hPath, WorldPt3(kProbeI196_Run, y, 0.0));
			}
		}
		else
		{
			// **#173 の `member-minor-dims` が実体を作れた形**（2D のパス）。
			VWPolygon2DObj path({VWPoint2D(0.0, y), VWPoint2D(kProbeI196_Run, y)});
			hPath = path.GetThisObject();
		}
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
		gSDK->ResetObject(hMember); // #173 の形（欄を書く前に 1 度）

		ProbeI196_SetValue(probe, hMember, "MemberType", "2"); // 2＝木（寸法 4 欄が断面になる）
		ProbeI196_SetReal(probe, hMember, "MajorBreadth", kProbeI196_Breadth);
		ProbeI196_SetReal(probe, hMember, "MajorDepth", kProbeI196_Depth);
		ProbeI196_SetReal(probe, hMember, "MinorBreadth", kProbeI196_Breadth); // ＝主幅 ⇒ 矩形
		ProbeI196_SetReal(probe, hMember, "MinorDepth", kProbeI196_Depth / 2.0);
		if (extraParams)
		{
			ProbeI196_SetValue(probe, hMember, "AxisAlign", "1");	   // 1＝**天端中央**基準
			ProbeI196_SetValue(probe, hMember, "StartCondition", "3"); // 3＝直切り
			ProbeI196_SetValue(probe, hMember, "EndCondition", "3");
		}

		if (withBounds)
		{
			VectorWorks::SStoryObjectData dataStart;
			dataStart.fBound = VectorWorks::eStoryObjectBound_Story;
			dataStart.fBoundStory = 0;
			dataStart.fLayerLevelType = levelType;
			dataStart.fOffset = offsetStart;
			VectorWorks::SStoryObjectData dataEnd = dataStart;
			dataEnd.fOffset = offsetEnd;
			const bool okStart = gSDK->SetObjectStoryBound(hMember, 0, dataStart);
			const bool okEnd = gSDK->SetObjectStoryBound(hMember, 1, dataEnd);
			if (!okStart || !okEnd)
				probe.log("部材 " + label + ": **バウンドの書き込みに失敗** " +
						  (okStart ? "ok" : "**失敗**") + "/" + (okEnd ? "ok" : "**失敗**"));
		}
		gSDK->ResetObject(hMember);
		return hMember;
	}

	// 実体があるか・どこにあるかを 1 行で出す。**判定はここだけを見れば済む。**
	void ProbeI196_LogGeometry(vwprobe::Report& probe, const std::string& label,
							   MCObjectHandle hMember)
	{
		if (hMember == nil)
		{
			probe.log("部材 " + label + ": **nil**");
			return;
		}
		size_t solids = 0;
		for (MCObjectHandle child = gSDK->FirstMemberObj(hMember); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (gSDK->GetObjectTypeN(child) == 84)
				++solids;
		}

		VWParametricObj pio(hMember);
		const VWPoint3D pos = pio.GetObjectModelPos();
		WorldCube cube;
		gSDK->GetObjectCube(hMember, cube);

		std::string line = "部材 " + label + ": " + (solids != 0 ? "実体84=有" : "**実体84=無**") +
						   " 挿入点Z=" + ProbeI196_Num(pos.z) +
						   " 外接Z 下=" + ProbeI196_Num(double(cube.MinZ())) +
						   " 上=" + ProbeI196_Num(double(cube.MaxZ()));
		if (gSDK->GetObjectStoryBoundsCount(hMember) != 0)
			line += " ／ バウンド解決Z ID0=" +
					ProbeI196_Num(gSDK->GetObjectBoundElevation(hMember, 0)) +
					" ID1=" + ProbeI196_Num(gSDK->GetObjectBoundElevation(hMember, 1));
		else
			line += " ／ バウンド無し";
		probe.log(line);

		probe.log("  読み戻し 断面=" + ProbeI196_Num(pio.GetParamReal(TXString("MajorBreadth"))) +
				  "x" + ProbeI196_Num(pio.GetParamReal(TXString("MajorDepth"))) + " AxisAlign=[" +
				  ProbeI196_FromTX(pio.GetParamValue(TXString("AxisAlign"))) +
				  "] StartCondition=[" +
				  ProbeI196_FromTX(pio.GetParamValue(TXString("StartCondition"))) + "]");
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
		"\" (2FL \"#IPZS##sign#\")\"",				 // **欲しい注記そのもの**
		"\"+\"@#IPZS#>0:\"\"#IPZS#",				 // 代わりの手（条件式）
		"\" (2FL \"\"+\"@#IPZS#>0:\"\"#IPZS#\")\""}; // 条件式版の注記そのもの

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
	//	作ったレイヤを**アクティブにしてから**部材を作る。
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

	// =======================================================================
	// 2. **梯子**。どこで実体が消えるかを切り分ける（1 巡目はここで失敗した）。
	// =======================================================================
	probe.log("=== 2. 部材の梯子（どこで実体が消えるか） ===");

	MCObjectHandle hMemberK = ProbeI196_MakeMember(probe, "K(2D・基本の欄だけ・バウンド無し)",
												   levelType, 0.0, false, false, false, 0.0, 0.0);
	MCObjectHandle hMemberA = ProbeI196_MakeMember(probe, "A(K＋AxisAlign 等・バウンド無し)",
												   levelType, 1000.0, false, true, false, 0.0, 0.0);
	MCObjectHandle hMemberP = ProbeI196_MakeMember(probe, "P(水平・正 +128)", levelType, 2000.0,
												   false, true, true, 128.0, 128.0);
	MCObjectHandle hMemberO =
		ProbeI196_MakeMember(probe, "O(水平・0)", levelType, 3000.0, false, true, true, 0.0, 0.0);
	MCObjectHandle hMemberN = ProbeI196_MakeMember(probe, "N(水平・負 -872)", levelType, 4000.0,
												   false, true, true, -872.0, -872.0);
	MCObjectHandle hMemberS = ProbeI196_MakeMember(probe, "S(傾斜・始端が低い)", levelType, 5000.0,
												   false, true, true, -872.0, -40.0);
	MCObjectHandle hMemberH = ProbeI196_MakeMember(probe, "H(傾斜・始端が高い)", levelType, 6000.0,
												   false, true, true, -40.0, -872.0);
	MCObjectHandle hMemberX = ProbeI196_MakeMember(probe, "X(**3D パス**＝1 巡目の道)", levelType,
												   7000.0, true, true, true, -872.0, -40.0);

	ProbeI196_LogGeometry(probe, "K", hMemberK);
	ProbeI196_LogGeometry(probe, "A", hMemberA);
	ProbeI196_LogGeometry(probe, "P", hMemberP);
	ProbeI196_LogGeometry(probe, "O", hMemberO);
	ProbeI196_LogGeometry(probe, "N", hMemberN);
	ProbeI196_LogGeometry(probe, "S", hMemberS);
	ProbeI196_LogGeometry(probe, "H", hMemberH);
	ProbeI196_LogGeometry(probe, "X", hMemberX);

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
	MCObjectHandle hTagX = nil;
	MCObjectHandle hTextP = ProbeI196_BuildTag(probe, tagSupport, hMemberP, 9000.0, "P", hTagP);
	MCObjectHandle hTextO = ProbeI196_BuildTag(probe, tagSupport, hMemberO, 10000.0, "O", hTagO);
	MCObjectHandle hTextN = ProbeI196_BuildTag(probe, tagSupport, hMemberN, 11000.0, "N", hTagN);
	MCObjectHandle hTextS = ProbeI196_BuildTag(probe, tagSupport, hMemberS, 12000.0, "S", hTagS);
	MCObjectHandle hTextH = ProbeI196_BuildTag(probe, tagSupport, hMemberH, 13000.0, "H", hTagH);
	MCObjectHandle hTextX = ProbeI196_BuildTag(probe, tagSupport, hMemberX, 14000.0, "X", hTagX);

	const size_t signCount = sizeof(kProbeI196_SignCases) / sizeof(kProbeI196_SignCases[0]);
	const size_t slopeCount = sizeof(kProbeI196_SlopeCases) / sizeof(kProbeI196_SlopeCases[0]);

	// =======================================================================
	// 3. **`#sign#` は `#IPZS#` の直後で効くか。** 1 巡目で「修飾子としては読まれている」
	//	ところまでは出た（`#` 1 つなら文字が出る・知らない修飾子は消える）。
	//	**残るのは「正の値に `+` が付くか」**で、それには P の `#IPZS#` が +128 に
	//	なっている必要がある（段 2 で確かめられる）。
	// =======================================================================
	probe.log("=== 3. #IPZS# の直後の #sign#（正 / 0 / 負） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagP, hTextP, "段3-P(正 +128)",
					  kProbeI196_SignCases, signCount);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagO, hTextO, "段3-O(0)",
					  kProbeI196_SignCases, signCount);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagN, hTextN, "段3-N(負 -872)",
					  kProbeI196_SignCases, signCount);

	// =======================================================================
	// 4. **傾斜材の両端の天端。** S は始端が低い端（`#IPZS#`=−872 のはず）、
	//	H は始端が高い端（`#IPZS#`=−40 のはず）。`#ZTBBS#` が高い端の天端 −40 と
	//	一致するか——断面の角が天端より上に出ないか——を段 2 の外接と突き合わせて見る。
	// =======================================================================
	probe.log("=== 4. 傾斜材 S（始端が低い端） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagS, hTextS, "段4-S", kProbeI196_SlopeCases,
					  slopeCount);

	probe.log("=== 5. 傾斜材 H（始端が高い端。低い端を読む口はあるか） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagH, hTextH, "段5-H", kProbeI196_SlopeCases,
					  slopeCount);

	probe.log("=== 6. 水平材 N に同じ式を当てる（両端が同じ値になるはず） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagN, hTextN, "段6-N(水平)",
					  kProbeI196_SlopeCases, slopeCount);

	probe.log("=== 7. 対照: X（3D パス＝1 巡目の道） ===");
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagX, hTextX, "段7-X(3D パス)",
					  kProbeI196_SlopeCases, slopeCount);

	// =======================================================================
	// 8. **バウンドの offset を動かして追随するか。** 利用者の要望は「取り込み後に
	//	高さを変えても注記が追随すること」なので、ここが要望そのものの試験である。
	//	S を両端 −1000 下げる（−1872 / −1040）→ `#IPZS#` は **−1872**、
	//	`#ZTBBS#` は **−1040** になるはず。
	// =======================================================================
	probe.log("=== 8. バウンドの offset を動かして読み直す ===");
	ProbeI196_SetOffsets(hMemberS, levelType, -1872.0, -1040.0);
	ProbeI196_LogGeometry(probe, "S(両端 -1000)", hMemberS);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagS, hTextS, "段8-S(両端 -1000)",
					  kProbeI196_SlopeCases, slopeCount);

	// 片端だけ動かす（勾配そのものが変わる）。
	ProbeI196_SetOffsets(hMemberS, levelType, -1872.0, 128.0);
	ProbeI196_LogGeometry(probe, "S(片端だけ +128 へ)", hMemberS);
	ProbeI196_EvalAll(probe, tagSupport, linkSupport, hTagS, hTextS, "段8-S(片端だけ変更)",
					  kProbeI196_SlopeCases, slopeCount);

	probe.log("=== 終わり ===");
}
