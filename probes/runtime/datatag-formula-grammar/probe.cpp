//
//	probes/runtime/datatag-formula-grammar/probe.cpp
//
//	[issue #187] データタグのタグフィールドの式（`IDataTagTextLinkSupport::SetFormula`）で
//	**何が書けて**（四則演算・文字列・条件式・数値の書式）**何が読めるか**（構造材の
//	始端／終端の高さ・ストーリバウンドのオフセット・レイヤ／ストーリ／レベルの高さ）を
//	実測する。
//
//	**目視は要らない作りにしてある。** 式の評価結果は `GetDataTagExtractedData`
//	（タグが抽出した「ラベル → 値」の対）で文字列として読み戻す。ワークシート式は
//	`VWFC::Tools::WSCriteriaExpression`（＝`ISDK::CompileCriteriaExpression` ＋
//	`ISDK::ExecWSExpression`）でハンドルへ直接評価する。
//
//	【5 巡目】4 巡目（ビルド `100801caa370`）で**本題に答えが出た**。残っているのは
//	ただ 1 つ——**その数値は動かしても正しいままか**である。
//
//	4 巡目で確定したこと:
//
//	  * **数値は出せる。** `#StructuralMember#.#DialogStartElevation#` が **−872** を
//	    描いた（`" (2FL "#…#DialogStartElevation#")"` → `` (2FL -872)``）。
//	    ただし**値がパラメータに入っているときだけ**で、`SetObjectStoryBound` で
//	    バウンドを書いただけでは 0 のまま（部材 E は `fOffset=−872` なのに式は 0）。
//	  * **名前と数値は出どころが逆。** バウンドで書いた本は**名前**が読めて
//	    （`T187-FL [上階]`）数値が 0、パラメータで書いた本は**数値**が読めて
//	    （−872）名前が空。**両方書けば両方出るのか**は未確認。
//	  * `#thsep#` は**3 桁のコンマを出さない**（`MajorDepth`=1234 を読み戻しで確かめた上で
//	    `##thsep#` → `1234`）。`#sign#` は同じ値で `+1234` を出すので、
//	    **修飾子が無視されているのではなく `#thsep#` にその働きが無い**。
//
//	**5 巡目の問い**: 利用者の要望は「取り込み後に高さを変えても注記が追随すること」。
//	パラメータに書いた −872 が、**部材を動かした後も正しい値であり続けるか**を確かめる。
//	追随しないなら、その式は**黙って古い数字を出し続ける**——いまの「固定の文字列」より
//	悪い（嘘だと気付けない）ので、そこまで確かめないと答えにならない。
//
//	  H: バウンド **＋** パラメータの両方を書く → 名前と数値が両方出るか
//	  H を +1000 動かす → 数値は追随するか
//	  H のバウンドの `fOffset` を書き換える → パラメータは追随するか
//	  I: パラメータだけを書く → 動かしたときどうなるか

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// -----------------------------------------------------------------------
	// ログ用の小物。**短い名前・ありふれた名前は使わない**（SDK と OS のヘッダが
	// グローバルへ撒いているため。probes/runtime/README.md）。

	std::string ProbeI187_FromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string ProbeI187_Num(double value)
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

	std::string ProbeI187_Int(long long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%lld", value);
		return std::string(buffer);
	}

	std::string ProbeI187_OneLine(const std::string& value)
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
	// タグを 1 つ作って部材へ関連付け、式を持たせるテキストを 1 本だけ持つ
	// レイアウトを渡す。戻り値は「式を持たせる先のテキスト」（駄目なら nil）。
	MCObjectHandle ProbeI187_BuildTag(vwprobe::Report& probe,
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
		probe.log("タグ " + label + ": AssociateWithObject=" +
				  (tagSupport->AssociateWithObject(outTag, hMember) ? "true" : "false"));

		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		MCObjectHandle hText = gSDK->CreateTextBlock("T187", WorldPt(0.0, 0.0), false, 0.0);
		if (hGroup == nil || hText == nil)
		{
			probe.fail("タグレイアウト（" + label + "）を組めなかった");
			return nil;
		}
		gSDK->AddObjectToContainer(hText, hGroup);
		// **中身を入れてから渡す**（Findings「データタグ」）。
		gSDK->SetCustomObjectProfileGroup(outTag, hGroup);

		// 渡したときに VW が群を複製していることがあるので、渡した後に取り直す。
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
		probe.log("タグ " + label + ": 渡した後のテキスト=" + (hLiveText == nil ? "無い" : "在る") +
				  " IsSupported=" +
				  (hLiveText != nil && linkSupport->IsSupported(hLiveText) ? "true" : "false"));
		if (hLiveText == nil)
			probe.fail("タグレイアウト（" + label + "）にテキストが無い");
		return hLiveText;
	}

	// -----------------------------------------------------------------------
	// タグの 1 本のテキストへ式を持たせ、更新して結果を読み戻す。
	void ProbeI187_EvalTagFormula(vwprobe::Report& probe,
								  VectorWorks::Extension::IDataTagSupport* tagSupport,
								  VectorWorks::Extension::IDataTagTextLinkSupport* linkSupport,
								  MCObjectHandle hTag, MCObjectHandle hText,
								  const std::string& label, const std::string& formula,
								  bool worksheetMode)
	{
		const TXString formulaTX(formula.c_str());
		linkSupport->SetIsLinked(hText, true);
		linkSupport->SetFormula(hText, formulaTX, worksheetMode);
		tagSupport->UpdateUserDefinedTextsUIDs(hTag);
		tagSupport->UpdateDataTag(hTag);
		gSDK->ResetObject(hTag);

		std::string line = "[B] " + label + " | ws=";
		line += worksheetMode ? "1" : "0";
		line += " | in='" + ProbeI187_OneLine(formula) + "'";

		const std::string readBack = ProbeI187_FromTX(linkSupport->GetFormula(hText));
		if (readBack != formula)
			line += " | 読み戻し='" + ProbeI187_OneLine(readBack) + "'";

		VectorWorks::Extension::TXStringSTLPairArray extracted;
		tagSupport->GetDataTagExtractedData(hTag, extracted);
		line += " | out=";
		if (extracted.empty())
			line += "(0 件)";
		for (size_t i = 0; i < extracted.size(); ++i)
			line += "'" + ProbeI187_OneLine(ProbeI187_FromTX(extracted[i].second)) + "'";
		probe.log(line);
	}

	// -----------------------------------------------------------------------
	// レベルテンプレートを作り、**出力引数の index を信用せず種別で引き直す**
	// （1 巡目はこれを信用して失敗した）。見つからなければ -1。
	short ProbeI187_LevelTemplateForType(vwprobe::Report& probe, const TXString& levelType)
	{
		const short count = gSDK->GetNumStoryLevelTemplates();
		std::string line = "レベルテンプレート一覧（件数=" + ProbeI187_Int(count) + "）:";
		short found = -1;
		// index の起点が 0 か 1 かも分からないので **0 から count まで**当たる。
		for (short i = 0; i <= count; ++i)
		{
			TXString name;
			TXString type;
			double scale = 0.0;
			double offset = 0.0;
			double wallHeight = 0.0;
			if (!gSDK->GetStoryLevelTemplateInfo(i, name, scale, type, offset, wallHeight))
				continue;
			line += " [" + ProbeI187_Int(i) + "]'" + ProbeI187_FromTX(name) + "'種別='" +
					ProbeI187_FromTX(type) + "'offset=" + ProbeI187_Num(offset);
			if (found < 0 && ProbeI187_FromTX(type) == ProbeI187_FromTX(levelType))
				found = i;
		}
		probe.log(line + " → 種別 '" + ProbeI187_FromTX(levelType) + "' は index " +
				  ProbeI187_Int(found));
		return found;
	}

	// **描ける**構造材を作る。Findings「取り込みでどう書くか」の手順そのまま
	//	——`MemberType` を書き忘れると太さが既定のまま残り、`Minor*` を放っておくと
	//	断面が I 形になって、せいが 200 未満だと 3D 実体が作られない。
	//	3 巡目はこれを省いたので **`GetObjectBounds` が空**（何も描いていない）だった。
	MCObjectHandle ProbeI187_MakeDrawnMember(vwprobe::Report& probe, const std::string& label,
											 MCObjectHandle hLayer, double y, double depth)
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
			probe.fail("部材 " + label + " を作れなかった（CreateCustomObjectPath が nil）");
			return nil;
		}
		if (hLayer != nil)
			gSDK->AddObjectToContainer(hMember, hLayer);
		// **個体へ書く**（VWRecordFormatObj は「書式の既定値」で、既にある個体に効かない）。
		VWFC::VWObjects::VWParametricObj pio(hMember);
		pio.SetParamValue("MemberType", "2"); // 2＝木
		pio.SetParamReal("MajorBreadth", 120.0);
		pio.SetParamReal("MajorDepth", depth);
		pio.SetParamReal("MinorBreadth", 120.0);
		pio.SetParamReal("MinorDepth", depth / 2.0);
		gSDK->ResetObject(hMember);
		probe.log("部材 " + label + ": MajorDepth を " + ProbeI187_Num(depth) +
				  " にした → 読み戻し='" + ProbeI187_FromTX(pio.GetParamValue("MajorDepth")) +
				  "' MemberType='" + ProbeI187_FromTX(pio.GetParamValue("MemberType")) + "'");
		return hMember;
	}

	// 部材の素性をログへ出す（式が返した数字の出どころを突き合わせるため）。
	void ProbeI187_DumpMember(vwprobe::Report& probe, const std::string& label,
							  MCObjectHandle hMember, bool hasBounds)
	{
		if (hMember == nil)
			return;
		const VWFC::VWObjects::VWParametricObj memberObj(hMember);
		const VWFC::Math::VWPoint3D pos = memberObj.GetObjectModelPos();
		std::string line = "部材 " + label + ": 挿入点Z=" + ProbeI187_Num(pos.z);
		if (hasBounds)
		{
			line +=
				" バウンド件数=" +
				ProbeI187_Int(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(hMember))) +
				" 解決Z ID0=" + ProbeI187_Num(gSDK->GetObjectBoundElevation(hMember, 0)) +
				" ID1=" + ProbeI187_Num(gSDK->GetObjectBoundElevation(hMember, 1));
			VectorWorks::SStoryObjectData readBack;
			if (gSDK->GetObjectStoryBound(hMember, 0, readBack))
				line += " ID0.fOffset=" + ProbeI187_Num(readBack.fOffset) + " ID0.種別='" +
						ProbeI187_FromTX(readBack.fLayerLevelType) + "'";
			if (gSDK->GetObjectStoryBound(hMember, 1, readBack))
				line += " ID1.fOffset=" + ProbeI187_Num(readBack.fOffset);
		}
		probe.log(line);

		static const char* const kWatchParams[] = {"StartElevation",
												   "EndElevation",
												   "DialogStartElevation",
												   "DialogEndElevation",
												   "DialogStartElevationReference",
												   "DialogEndElevationReference",
												   "OffsetZ",
												   "MajorDepth",
												   "MemberID"};
		std::string params = "部材 " + label + " レコード:";
		for (size_t i = 0; i < sizeof(kWatchParams) / sizeof(kWatchParams[0]); ++i)
		{
			params +=
				std::string(" ") + kWatchParams[i] + "='" +
				ProbeI187_OneLine(ProbeI187_FromTX(memberObj.GetParamValue(kWatchParams[i]))) + "'";
		}
		probe.log(params);
	}
} // namespace

VW_PROBE("datatag-formula-grammar", "データタグの式を総当たりで実測する",
		 "式の演算・文字列・条件・書式と、高さを読めるフィールドをログだけで判定する")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	// =======================================================================
	// 1. 舞台。数値はすべて互いに違う値にして、式が返した数字だけで出どころが
	//    言い当てられるようにする。
	//
	//	  ストーリ T187-2F   高さ 3571 / レベル T187-FL（階内相対 0）
	//	  ストーリ T187-PLAN 高さ 2699 / レベル T187-PLAN-L（伏図レイヤの見立て）
	//	  部材 A: T187-FL レイヤ。ストーリバウンド ID0 = −872 / ID1 = −302
	//	  部材 B: T187-PLAN レイヤ。バウンド無し
	// =======================================================================
	probe.log("=== 1. 舞台を作る ===");

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

	// **1 つずつ作って、種別で引き直してから足す。**
	MCObjectHandle hLayerFL = nil;
	if (hStoryFL != nil)
	{
		short index = -1;
		TXString templateName("T187-TPL-FL");
		probe.log(
			std::string("CreateStoryLevelTemplate(FL)=") +
			(gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelTypeFL, 0.0, 2400.0, index)
				 ? "true"
				 : "false") +
			" 出力引数 index=" + ProbeI187_Int(index));
		const short byType = ProbeI187_LevelTemplateForType(probe, levelTypeFL);
		if (byType >= 0)
		{
			probe.log(std::string("AddStoryLevelFromTemplate(2F, ") + ProbeI187_Int(byType) + ")=" +
					  (gSDK->AddStoryLevelFromTemplate(hStoryFL, byType) ? "true" : "false"));
			hLayerFL = gSDK->GetLayerForStory(hStoryFL, levelTypeFL);
		}
	}

	MCObjectHandle hLayerPlan = nil;
	if (hStoryPlan != nil)
	{
		short index = -1;
		TXString templateName("T187-TPL-PLAN");
		probe.log(
			std::string("CreateStoryLevelTemplate(PLAN)=") +
			(gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelTypePlan, 0.0, 2400.0, index)
				 ? "true"
				 : "false") +
			" 出力引数 index=" + ProbeI187_Int(index));
		const short byType = ProbeI187_LevelTemplateForType(probe, levelTypePlan);
		if (byType >= 0)
		{
			probe.log(
				std::string("AddStoryLevelFromTemplate(PLAN, ") + ProbeI187_Int(byType) +
				")=" + (gSDK->AddStoryLevelFromTemplate(hStoryPlan, byType) ? "true" : "false"));
			hLayerPlan = gSDK->GetLayerForStory(hStoryPlan, levelTypePlan);
		}
	}

	probe.log(std::string("レイヤ: FL=") + (hLayerFL != nil ? "取れた" : "**nil**") +
			  " PLAN=" + (hLayerPlan != nil ? "取れた" : "**nil**"));
	if (hLayerFL == nil)
		probe.fail("T187-FL のレイヤを取れなかった（バウンド付きの部材が用意できない）");

	// ---- 部材を 2 本作る。
	//	H: バウンド **＋** パラメータの両方を書く
	//	I: パラメータだけを書く
	MCObjectHandle hMemberH = ProbeI187_MakeDrawnMember(probe, "H", hLayerPlan, 0.0, 600.0);
	MCObjectHandle hMemberI = ProbeI187_MakeDrawnMember(probe, "I", hLayerPlan, 1500.0, 600.0);

	if (hMemberH != nil)
	{
		SStoryObjectData bound;
		bound.fBound = eStoryObjectBound_Story;
		bound.fBoundStory = 1; // 伏図の階から 1 つ上＝T187-2F
		bound.fLayerLevelType = levelTypeFL;
		bound.fOffset = -872.0;
		gSDK->SetObjectStoryBound(hMemberH, 0, bound);
		bound.fOffset = -302.0;
		gSDK->SetObjectStoryBound(hMemberH, 1, bound);
		VWFC::VWObjects::VWParametricObj pioH(hMemberH);
		pioH.SetParamReal("DialogStartElevation", -872.0);
		pioH.SetParamReal("DialogEndElevation", -302.0);
		gSDK->ResetObject(hMemberH);
		ProbeI187_DumpMember(probe, "H(バウンド＋パラメータ)", hMemberH, true);
	}
	if (hMemberI != nil)
	{
		VWFC::VWObjects::VWParametricObj pioI(hMemberI);
		pioI.SetParamReal("DialogStartElevation", -872.0);
		pioI.SetParamReal("DialogEndElevation", -302.0);
		gSDK->ResetObject(hMemberI);
		ProbeI187_DumpMember(probe, "I(パラメータだけ)", hMemberI, false);
	}

	// =======================================================================
	// 2. タグの式で読む → 動かす → 読み直す。
	// =======================================================================
	probe.log("=== 2. 読む → 動かす → 読み直す（連動の試験）===");

	IDataTagSupportPtr tagSupport(IID_DataTagSupport);
	IDataTagTextLinkSupportPtr linkSupport(IID_DataTagTextLinkSupport);
	if (!tagSupport || !linkSupport)
	{
		probe.fail("IDataTagSupport / IDataTagTextLinkSupport を取れなかった");
		return;
	}

	struct ProbeI187_Case
	{
		const char* label;
		const char* formula;
	};

	static const ProbeI187_Case kCases[] = {
		{"IPZL", "#IPZL#"},
		{"DialogStartElev", "#StructuralMember#.#DialogStartElevation#"},
		{"DialogEndElev", "#StructuralMember#.#DialogEndElevation#"},
		{"DialogStartRef", "#StructuralMember#.#DialogStartElevationReference#"},
		{"注記の形", "\" (2FL \"#StructuralMember#.#DialogStartElevation#\")\""}};

	MCObjectHandle hTagH = nil;
	MCObjectHandle hTextH = nil;
	if (hMemberH != nil)
		hTextH = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberH, hLayerPlan, 6000.0,
									"H", hTagH);
	MCObjectHandle hTagI = nil;
	MCObjectHandle hTextI = nil;
	if (hMemberI != nil)
		hTextI = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberI, hLayerPlan, 9000.0,
									"I", hTagI);

	// --- 段 1: そのまま読む
	probe.log("--- 段 1: 作ったまま ---");
	if (hTextH != nil)
		for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
			ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagH, hTextH,
									 std::string("段1-H-") + kCases[i].label, kCases[i].formula,
									 false);
	if (hTextI != nil)
		for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
			ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagI, hTextI,
									 std::string("段1-I-") + kCases[i].label, kCases[i].formula,
									 false);

	// --- 段 2: **+1000 動かす**。要望（高さを変えたら注記が追随する）の直接の試験。
	//	`#IPZL#` は追随すると分かっている（3 巡目）。問題は **−872 のほう**で、
	//	追随しなければ**黙って古い数字を出し続ける**。
	probe.log("--- 段 2: +1000 動かした後（追随するか）---");
	if (hMemberH != nil)
	{
		gSDK->MoveObject3D(hMemberH, 0.0, 0.0, 1000.0);
		gSDK->ResetObject(hMemberH);
		ProbeI187_DumpMember(probe, "H 移動後", hMemberH, true);
		if (hTextH != nil)
			for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagH, hTextH,
										 std::string("段2-H-") + kCases[i].label, kCases[i].formula,
										 false);
	}
	if (hMemberI != nil)
	{
		gSDK->MoveObject3D(hMemberI, 0.0, 0.0, 1000.0);
		gSDK->ResetObject(hMemberI);
		ProbeI187_DumpMember(probe, "I 移動後", hMemberI, false);
		if (hTextI != nil)
			for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagI, hTextI,
										 std::string("段2-I-") + kCases[i].label, kCases[i].formula,
										 false);
	}

	// --- 段 3: **バウンドの `fOffset` を書き換える**。VW の側から高さが変わる形
	//	（Findings「始点をずらせば ID 0 の fOffset も書き換わる」）に近いのはこちら。
	probe.log("--- 段 3: バウンドの fOffset を -1872 に書き換えた後 ---");
	if (hMemberH != nil)
	{
		SStoryObjectData bound;
		if (gSDK->GetObjectStoryBound(hMemberH, 0, bound))
		{
			bound.fOffset = -1872.0;
			gSDK->SetObjectStoryBound(hMemberH, 0, bound);
			gSDK->ResetObject(hMemberH);
			ProbeI187_DumpMember(probe, "H fOffset 書き換え後", hMemberH, true);
			if (hTextH != nil)
				for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
					ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagH, hTextH,
											 std::string("段3-H-") + kCases[i].label,
											 kCases[i].formula, false);
		}
	}

	probe.log("=== 終わり ===");
}
