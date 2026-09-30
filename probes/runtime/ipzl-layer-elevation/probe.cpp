//
//	probes/runtime/ipzl-layer-elevation/probe.cpp
//
//	[issue #194] データタグの式の `#IPZL#` が引く「レイヤの高さ」が何なのかを確定する。
//
//	#190 の走行で、**素のデザインレイヤへ `VWLayerObj::SetElevation(872)` を与えても
//	`#IPZL#` が 0 のまま**だった（`GetElevation()` の読み戻しは 872）。#187 の舞台
//	——ストーリのレベルから作ったレイヤ——では `#IPZL#` が −2699 と出て追随したので、
//	切り分けたい候補は 3 つある:
//
//	  (a) `#IPZL#` が見るのは**ストーリ由来の高さだけ**で、素のレイヤの高さは見ない。
//	  (b) `SetElevation`（`GS_Kludge` 1002）が書く場所は `GetElevation`（同 1000）しか
//	      読まない箱で、**図面としてのレイヤの高さには届いていない**。
//	  (c) レイヤの高さが**部材の絶対Z も一緒に持ち上げていて**、差し引き 0 になっている
//	      （＝ `#IPZL#` の式は正しく、舞台の読み方のほうが誤っている）。
//
//	**目視は要らない。** 判定はすべて数値の読み戻しで付く。
//
//	切り分けの仕掛けは 2 つ。
//
//	1. **式を 3 本まとめて引く**（`#IPZ#`＝絶対Z / `#IPZS#`＝ストーリ基準 /
//	   `#IPZL#`＝レイヤ基準）。(c) なら素のレイヤで `#IPZ#` が 872 になる——
//	   `#IPZL#` 単独では (a)(b) と見分けが付かない。
//	2. **ストーリのレベルの階内相対Z を 0 でない値（−40）にする。** #187 / #190 の舞台は
//	   どちらも相対Z が 0 で、**階の高さとレイヤの高さが一致していた**ので、
//	   −2699 が「階の高さ」なのか「レイヤの高さ」なのかを言い分けられていない。
//	   階 2699 ＋ 相対Z −40 ＝ **レイヤの絶対Z 2659** と食い違わせれば、
//	   `#IPZL#` が −2659 か −2699 かで**どちらを引いているかが 1 行で決まる**。
//
//	さらに「素のデザインレイヤでも `#IPZL#` を効かせる手があるか」を、レイヤの高さを
//	与える**4 通り**で測る（どれが図面へ届くかは、レイヤ側の読み戻し 4 経路で見る）:
//
//	  * `VWFC::VWObjects::VWLayerObj::SetElevation`（`GS_Kludge` 1002）
//	  * オブジェクト変数 `ovLayerHeightInCurrUnits`(157)
//	    ——ヘッダに "the base elevation height of the layer" とある
//	  * VectorScript の `SetLayerElevation` / `SetLayerElevationN`
//	    （`IVectorScriptEngine::ExecuteScript` 経由。ISDK には対応する口が無い）
//	  * ストーリのレベルから作る（#187 の舞台。対照）
//
//	レイヤ側の読み戻しは 4 経路:
//	  `VWLayerObj::GetElevation`(1000) / `GetHeight`(1001) /
//	  `ovLayerHeightInCurrUnits`(157) / `ovLayerThicknessInCurrUnits`(158) /
//	  `GetStoryObjectDataBoundHeight` に `eStoryObjectBound_LayerElevation` を解かせる道
//	  （Findings「レイヤ・ストーリ・重ね順」が「VS の GetLayerElevation に当たる ISDK の
//	   口は無いが、この経路で取れる」と書いている道）。
//
//	**数値はすべて互いに違う**ので、出た数字だけで出どころが分かる:
//	  872（素A1）/ 1700（素A2・後から）/ 1234（ov157）/ 1500（VS）/ 1600（VS の N 版）/
//	  2699（階）/ 2659（ストーリ由来レイヤ）/ 5000（ストーリ由来レイヤへ後から SetElevation）/
//	  300（部材を持ち上げる量）
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Scripting/IVectorScriptEngine.h"
#include "VWFC/VWObjects/VWLayerObj.h"
#include "VWFC/VWObjects/VWParametricObj.h"

#include <cstdio>
#include <string>

namespace
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;
	using namespace VectorWorks::Scripting;

	// -----------------------------------------------------------------------
	// ログ用の小物。**短い名前・ありふれた名前は使わない**（SDK と OS のヘッダが
	// グローバルへ撒いているため。probes/runtime/README.md）。

	std::string ProbeI194_FromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string ProbeI194_Num(double value)
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

	std::string ProbeI194_OneLine(const std::string& value)
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

	// -----------------------------------------------------------------------
	// **レイヤの高さを 5 経路で読む。** 「どの書き方が図面へ届いたか」は、
	// 書いた側の戻り値ではなくこの読み戻しで判定する
	// （Findings「調査の作法」——setter の戻り値を信用しない）。
	void ProbeI194_DescribeLayer(vwprobe::Report& probe, const std::string& label,
								 MCObjectHandle hLayer)
	{
		if (hLayer == nil)
		{
			probe.log("[レイヤ] " + label + " | **nil**");
			return;
		}

		VWFC::VWObjects::VWLayerObj layerObj(hLayer);
		std::string line = "[レイヤ] " + label;
		line += " | GetElevation(1000)=" + ProbeI194_Num(layerObj.GetElevation());
		line += " GetHeight(1001)=" + ProbeI194_Num(layerObj.GetHeight());

		// オブジェクト変数（ヘッダの説明は 157＝base elevation / 158＝thickness、
		// どちらも "current units"）。**型も出す**——Real64 で返らないなら
		// 読み方そのものが違うので、0 を「高さが 0」と読み違えないため。
		for (int pass = 0; pass < 2; ++pass)
		{
			const short selector = (pass == 0) ? 157 : 158;
			TVariableBlock value;
			const bool got = gSDK->GetObjectVariable(hLayer, selector, value) != 0;
			Real64 asReal = 0.0;
			const bool isReal = value.GetReal64(asReal) != 0;
			line += std::string(" ov") + (pass == 0 ? "157" : "158") + "=";
			if (!got)
				line += "**取れない**";
			else if (!isReal)
				line += "**Real64 でない**(type=" + ProbeI194_Num((double)value.GetType()) + ")";
			else
				line += ProbeI194_Num(asReal);
		}

		// Findings「レイヤ・ストーリ・重ね順」が書いている「レイヤ高さを読む道」。
		SStoryObjectData bound;
		bound.fBound = eStoryObjectBound_LayerElevation;
		bound.fBoundStory = 0;
		bound.fOffset = 0.0;
		line += " バウンド経路(LayerElevation)=" +
				ProbeI194_Num(gSDK->GetStoryObjectDataBoundHeight(bound, hLayer));
		bound.fBound = eStoryObjectBound_LayerWallHeight;
		line += " (LayerWallHeight)=" +
				ProbeI194_Num(gSDK->GetStoryObjectDataBoundHeight(bound, hLayer));

		MCObjectHandle hStory = gSDK->GetStoryOfLayer(hLayer);
		line += std::string(" ストーリ=") + (hStory == nil ? "無し" : "有り");
		if (hStory != nil)
			line += "(高さ=" + ProbeI194_Num(gSDK->GetStoryElevation(hStory)) + ")";

		probe.log(line);
	}

	// -----------------------------------------------------------------------
	// タグを 1 つ作って部材へ関連付け、式を持たせるテキストを 1 本だけ持つ
	// レイアウトを渡す。戻り値は「式を持たせる先のテキスト」（駄目なら nil）。
	// #187 / #190 の作りそのまま——**中身を入れてから渡し、渡した後に取り直す**
	// （Findings「データタグ」。VW が群を複製していることがある）。
	MCObjectHandle ProbeI194_BuildTag(vwprobe::Report& probe, IDataTagSupport* tagSupport,
									  IDataTagTextLinkSupport* linkSupport, MCObjectHandle hMember,
									  MCObjectHandle hLayer, double x, MCObjectHandle& outTag)
	{
		outTag = gSDK->CreateCustomObject("Data Tag", WorldPt(x, 0.0), 0.0, true);
		if (outTag == nil)
		{
			probe.fail("データタグを作れなかった（CreateCustomObject が nil）");
			return nil;
		}
		if (hLayer != nil)
			gSDK->AddObjectToContainer(outTag, hLayer);
		probe.log(std::string("タグ: AssociateWithObject=") +
				  (tagSupport->AssociateWithObject(outTag, hMember) ? "true" : "false"));

		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		MCObjectHandle hText = gSDK->CreateTextBlock("T194", WorldPt(0.0, 0.0), false, 0.0);
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
		probe.log(std::string("タグ: 渡した後のテキスト=") + (hLiveText == nil ? "無い" : "在る") +
				  " IsSupported=" +
				  (hLiveText != nil && linkSupport->IsSupported(hLiveText) ? "true" : "false"));
		if (hLiveText == nil)
			probe.fail("タグレイアウトにテキストが無い（式を持たせる先が作れない）");
		return hLiveText;
	}

	// タグの 1 本のテキストへ式を持たせ、更新して結果を読み戻す。
	std::string ProbeI194_EvalOne(IDataTagSupport* tagSupport, IDataTagTextLinkSupport* linkSupport,
								  MCObjectHandle hTag, MCObjectHandle hText,
								  const std::string& formula)
	{
		const TXString formulaTX(formula.c_str());
		linkSupport->SetIsLinked(hText, true);
		linkSupport->SetFormula(hText, formulaTX, false);
		tagSupport->UpdateUserDefinedTextsUIDs(hTag);
		tagSupport->UpdateDataTag(hTag);
		gSDK->ResetObject(hTag);

		TXStringSTLPairArray extracted;
		tagSupport->GetDataTagExtractedData(hTag, extracted);
		if (extracted.empty())
			return "(0 件)";
		std::string out;
		for (size_t i = 0; i < extracted.size(); ++i)
			out += ProbeI194_OneLine(ProbeI194_FromTX(extracted[i].second));
		return out;
	}

	// **3 本まとめて引く。** `#IPZ#`（絶対Z）を併せて読むのが肝で、これが無いと
	// 「レイヤ高さが部材ごと持ち上げたので差が 0」（候補 c）を否定できない。
	void ProbeI194_EvalTriple(vwprobe::Report& probe, IDataTagSupport* tagSupport,
							  IDataTagTextLinkSupport* linkSupport, MCObjectHandle hTag,
							  MCObjectHandle hText, const std::string& label)
	{
		if (hTag == nil || hText == nil)
		{
			probe.log("[式] " + label + " | **タグが無いので引けない**");
			return;
		}
		probe.log("[式] " + label + " | #IPZ#='" +
				  ProbeI194_EvalOne(tagSupport, linkSupport, hTag, hText, "#IPZ#") + "' #IPZS#='" +
				  ProbeI194_EvalOne(tagSupport, linkSupport, hTag, hText, "#IPZS#") + "' #IPZL#='" +
				  ProbeI194_EvalOne(tagSupport, linkSupport, hTag, hText, "#IPZL#") + "'");
	}

	// **描ける**構造材を作る（#187 の 4 巡目で確立した手順。`MemberType` を書き忘れると
	// 太さが既定のまま残り、`Minor*` を放っておくと断面が I 形になって、せいが 200 未満だと
	// 3D 実体が作られない）。
	MCObjectHandle ProbeI194_MakeMember(vwprobe::Report& probe, const std::string& label,
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
			probe.fail("構造材 " + label + " を作れなかった（CreateCustomObjectPath が nil）");
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
		return hMember;
	}

	// VectorScript を 1 本流す（ISDK にレイヤ高さを書く口は無いので、VS の
	// `SetLayerElevation` はこの経路でしか試せない）。**Python 経由で `vs.*` を
	// 呼ぶと VW ごと落ちる**ので、使うのは VectorScript のほうだけ
	// （Findings「Undo」）。
	void ProbeI194_RunVectorScript(vwprobe::Report& probe, IVectorScriptEngine* engine,
								   const std::string& label, const std::string& body)
	{
		if (engine == nullptr)
		{
			probe.log("[VS] " + label + " | **エンジンが無いので流せない**");
			return;
		}
		const TXString script(
			("PROCEDURE __ProbeI194;\nBEGIN\n\t" + body + "\nEND;\nRun(__ProbeI194);\n").c_str());
		bool compiledOk = false;
		Sint32 errorLine = -1;
		TXString errorText;
		engine->CompileScript(script, false, compiledOk, &errorLine, &errorText);
		if (!compiledOk)
		{
			probe.log("[VS] " + label + " | **コンパイルできない** line=" +
					  ProbeI194_Num((double)errorLine) + " [" + ProbeI194_FromTX(errorText) + "]");
			return;
		}
		const VCOMError err = engine->ExecuteScript(script);
		probe.log("[VS] " + label + " | 流した: " + ProbeI194_OneLine(body) +
				  " | VCOMError=" + ProbeI194_Num((double)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
	}
} // namespace

VW_PROBE("ipzl-layer-elevation", "#IPZL# はどのレイヤ高さを引くか",
		 "素のデザインレイヤとストーリ由来のレイヤで #IPZ#/#IPZS#/#IPZL# を引き比べる")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;
	using namespace VectorWorks::Scripting;

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
	IVectorScriptEnginePtr vsEngine(IID_VectorScriptEngine);
	if (!vsEngine)
		probe.log("IVectorScriptEngine を取れなかった（VS 経由の段だけ飛ばす）");

	// =======================================================================
	// 1. **決め手の段**——ストーリ由来のレイヤで、階の高さとレイヤの高さを食い違わせる。
	//	  階 T194-S の高さ 2699 ＋ レベルの階内相対Z −40 → **レイヤの絶対Z は 2659**。
	//	  部材の挿入点Z は 0 なので:
	//	    #IPZL# が **−2659** → 引いているのは**レイヤの高さ**
	//	    #IPZL# が **−2699** → 引いているのは**階（ストーリ）の高さ**
	//	  #187 / #190 の舞台は相対Z が 0 でこの 2 つが一致していたため、
	//	  ここは**まだ一度も測られていない**。
	// =======================================================================
	probe.log("=== 1. ストーリ由来のレイヤ（階 2699 / 相対Z -40 → レイヤ 2659）===");

	MCObjectHandle hLayerStory = nil;
	MCObjectHandle hStory = nil;
	{
		TXString levelType("T194-LV");
		gSDK->CreateLayerLevelType(levelType);
		TXString storyName("T194-S");
		TXString storySuffix("T194S");
		gSDK->CreateStory(storyName, storySuffix);
		hStory = gSDK->GetNamedObject("T194-S");
		if (hStory != nil)
			gSDK->SetStoryElevation(hStory, 2699.0);

		// **出力引数の index を信用せず種別で引き直す**（#188 の 1 巡目はこれを
		// 信用して舞台を作り損ねた）。
		short index = -1;
		TXString templateName("T194-TPL");
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
				ProbeI194_FromTX(type) == "T194-LV")
				byType = i;
		}
		if (byType >= 0 && hStory != nil)
		{
			gSDK->AddStoryLevelFromTemplate(hStory, byType);
			hLayerStory = gSDK->GetLayerForStory(hStory, levelType);
		}
		if (hStory != nil)
			probe.log("階 T194-S: GetStoryElevation=" +
					  ProbeI194_Num(gSDK->GetStoryElevation(hStory)) + " 階内相対Z(T194-LV)=" +
					  ProbeI194_Num(gSDK->GetStoryLevelElevation(hStory, levelType)));
	}
	if (hLayerStory == nil)
	{
		// **ここが取れないと本題が測れない**ので、素のレイヤの段は続けつつ失敗を立てる。
		probe.fail("ストーリのレベルからレイヤを取れなかった（決め手の段が測れない）");
	}
	ProbeI194_DescribeLayer(probe, "S(ストーリ由来)", hLayerStory);

	MCObjectHandle hTagStory = nil;
	MCObjectHandle hTextStory = nil;
	if (hLayerStory != nil)
	{
		MCObjectHandle hMemberStory = ProbeI194_MakeMember(probe, "S", hLayerStory, 0.0);
		if (hMemberStory != nil)
			hTextStory = ProbeI194_BuildTag(probe, tagSupport, linkSupport, hMemberStory,
											hLayerStory, 6000.0, hTagStory);
	}
	ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagStory, hTextStory,
						 "S: ストーリ由来レイヤ(2659) / 部材Z=0 → -2659 ならレイヤ・-2699 なら階");

	// =======================================================================
	// 2. 素のデザインレイヤ A1——**部材を作る前に** SetElevation(872)。
	//	  #190 が踏んだ形そのもの。
	// =======================================================================
	probe.log("=== 2. 素のデザインレイヤ A1（SetElevation(872) を先に）===");

	MCObjectHandle hLayerA1 = gSDK->CreateLayer("T194-A1", kLayerDesign);
	if (hLayerA1 != nil)
		VWFC::VWObjects::VWLayerObj(hLayerA1).SetElevation(872.0);
	ProbeI194_DescribeLayer(probe, "A1(素・872)", hLayerA1);

	MCObjectHandle hMemberA1 = ProbeI194_MakeMember(probe, "A1", hLayerA1, 0.0);
	MCObjectHandle hTagA1 = nil;
	MCObjectHandle hTextA1 = nil;
	if (hMemberA1 != nil)
		hTextA1 =
			ProbeI194_BuildTag(probe, tagSupport, linkSupport, hMemberA1, hLayerA1, 6000.0, hTagA1);
	ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagA1, hTextA1,
						 "A1: 素レイヤ(872) / 部材Z=0 → #IPZ# が 872 なら候補(c)");

	// --- 部材を +300 持ち上げる。**素のレイヤでも `#IPZL#` は部材に追随するのか**を
	//	見る（追随するなら「連動しない」のではなく「基準が 0 に固定されている」）。
	if (hMemberA1 != nil)
	{
		gSDK->MoveObject3D(hMemberA1, 0.0, 0.0, 300.0);
		gSDK->ResetObject(hMemberA1);
		ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagA1, hTextA1,
							 "A1: 部材を +300 した後 → #IPZL# が 300 なら基準は 0 のまま");
	}

	// =======================================================================
	// 3. 素のデザインレイヤ A2——**部材とタグを作った後に** SetElevation(1700)。
	//	  「順番のせい」「作り直しが要るだけ」を潰す。ResetObject も掛ける。
	// =======================================================================
	probe.log("=== 3. 素のデザインレイヤ A2（後から SetElevation(1700) ＋ ResetObject）===");

	MCObjectHandle hLayerA2 = gSDK->CreateLayer("T194-A2", kLayerDesign);
	MCObjectHandle hMemberA2 = ProbeI194_MakeMember(probe, "A2", hLayerA2, 1000.0);
	MCObjectHandle hTagA2 = nil;
	MCObjectHandle hTextA2 = nil;
	if (hMemberA2 != nil)
		hTextA2 =
			ProbeI194_BuildTag(probe, tagSupport, linkSupport, hMemberA2, hLayerA2, 6000.0, hTagA2);
	ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagA2, hTextA2, "A2: 与える前（基準）");

	if (hLayerA2 != nil)
	{
		VWFC::VWObjects::VWLayerObj(hLayerA2).SetElevation(1700.0);
		ProbeI194_DescribeLayer(probe, "A2(素・後から 1700)", hLayerA2);
		ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagA2, hTextA2, "A2: 与えた直後");

		gSDK->ResetObject(hLayerA2);
		if (hMemberA2 != nil)
			gSDK->ResetObject(hMemberA2);
		ProbeI194_DescribeLayer(probe, "A2(ResetObject の後)", hLayerA2);
		ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagA2, hTextA2,
							 "A2: ResetObject の後");
	}

	// =======================================================================
	// 4. 素のデザインレイヤ B——オブジェクト変数 `ovLayerHeightInCurrUnits`(157) で書く。
	//	  ヘッダの説明は "current units, the base elevation height of the layer"。
	// =======================================================================
	probe.log("=== 4. 素のデザインレイヤ B（ovLayerHeightInCurrUnits(157) で 1234）===");

	MCObjectHandle hLayerB = gSDK->CreateLayer("T194-B", kLayerDesign);
	if (hLayerB != nil)
	{
		TVariableBlock value(Real64(1234.0));
		probe.log(std::string("B: SetObjectVariable(157, 1234)=") +
				  (gSDK->SetObjectVariable(hLayerB, 157, value) ? "true" : "false"));
	}
	ProbeI194_DescribeLayer(probe, "B(ov157・1234)", hLayerB);

	MCObjectHandle hMemberB = ProbeI194_MakeMember(probe, "B", hLayerB, 0.0);
	MCObjectHandle hTagB = nil;
	MCObjectHandle hTextB = nil;
	if (hMemberB != nil)
		hTextB =
			ProbeI194_BuildTag(probe, tagSupport, linkSupport, hMemberB, hLayerB, 6000.0, hTagB);
	ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagB, hTextB,
						 "B: ov157 で 1234 を書いた");

	// =======================================================================
	// 5. 素のデザインレイヤ C / D——VectorScript の `SetLayerElevation` /
	//	  `SetLayerElevationN`。ISDK には対応する口が無いので、効くならこれが
	//	  「素のレイヤでも #IPZL# を効かせる手」の本命になる。
	// =======================================================================
	probe.log("=== 5. 素のデザインレイヤ C / D（VectorScript の SetLayerElevation）===");

	MCObjectHandle hLayerC = gSDK->CreateLayer("T194-C", kLayerDesign);
	MCObjectHandle hLayerD = gSDK->CreateLayer("T194-D", kLayerDesign);
	ProbeI194_RunVectorScript(probe, vsEngine, "C",
							  "SetLayerElevation(GetLayerByName('T194-C'), 1500, 2400);");
	ProbeI194_RunVectorScript(probe, vsEngine, "D",
							  "SetLayerElevationN(GetLayerByName('T194-D'), 1600, 2400);");
	ProbeI194_DescribeLayer(probe, "C(VS SetLayerElevation・1500)", hLayerC);
	ProbeI194_DescribeLayer(probe, "D(VS SetLayerElevationN・1600)", hLayerD);

	MCObjectHandle hMemberC = ProbeI194_MakeMember(probe, "C", hLayerC, 0.0);
	MCObjectHandle hTagC = nil;
	MCObjectHandle hTextC = nil;
	if (hMemberC != nil)
		hTextC =
			ProbeI194_BuildTag(probe, tagSupport, linkSupport, hMemberC, hLayerC, 6000.0, hTagC);
	ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagC, hTextC,
						 "C: VS の SetLayerElevation(1500)");

	MCObjectHandle hMemberD = ProbeI194_MakeMember(probe, "D", hLayerD, 0.0);
	MCObjectHandle hTagD = nil;
	MCObjectHandle hTextD = nil;
	if (hMemberD != nil)
		hTextD =
			ProbeI194_BuildTag(probe, tagSupport, linkSupport, hMemberD, hLayerD, 6000.0, hTagD);
	ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagD, hTextD,
						 "D: VS の SetLayerElevationN(1600)");

	// =======================================================================
	// 6. ストーリ由来のレイヤへ**後から** `SetElevation(5000)` を掛ける。
	//	  「SetElevation が書く箱」と「ストーリが決める高さ」が同じものかを見る
	//	  ——同じなら 5000 が全経路に出て `#IPZL#` も −5000 になる。
	// =======================================================================
	probe.log("=== 6. ストーリ由来のレイヤへ後から SetElevation(5000) ===");

	if (hLayerStory != nil)
	{
		VWFC::VWObjects::VWLayerObj(hLayerStory).SetElevation(5000.0);
		ProbeI194_DescribeLayer(probe, "S(後から 5000)", hLayerStory);
		if (hStory != nil)
			probe.log("階 T194-S: 後から見た GetStoryElevation=" +
					  ProbeI194_Num(gSDK->GetStoryElevation(hStory)));
		ProbeI194_EvalTriple(probe, tagSupport, linkSupport, hTagStory, hTextStory,
							 "S: SetElevation(5000) の後");
	}

	probe.log("=== 終わり ===");
}
