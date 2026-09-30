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
//	【2 巡目】1 巡目（ビルド `b463199a44c6`）で分かったことと、直した点:
//
//	  * **知らない識別子もコンパイルは通り、「自分自身の文字列」に評価される**
//	    （`T187NOSUCHFUNCTION` → `s:'T187NOSUCHFUNCTION'`）。だから
//	    **コンパイル誤りでは関数の有無を判定できない**——判定材料は**戻り値の型と中身**で、
//	    「入れた綴りがそのまま文字列で返ったら、その関数は無い」。`ProbeI187_EvalWS` が
//	    その判定を自分で付けるようにした。
//	  * **`CreateStoryLevelTemplate` の出力引数 index を信用してはいけない。** 2 つ作って
//	    **どちらも 1 が返り**、`AddStoryLevelFromTemplate` が 2 つの階へ同じ
//	    テンプレートを足したので、`GetLayerForStory(2F, FL)` が nil になり、
//	    **ストーリバウンドを持つ部材を用意できなかった**（1 巡目が失敗した理由）。
//	    2 巡目は**種別で引き直す**（`GetNumStoryLevelTemplates` ＋
//	    `GetStoryLevelTemplateInfo`）。
//	  * バウンド付きの部材が作れなかったので、**`TOPBOUND` / `BOTBOUND`（実在する関数。
//	    1 巡目はバウンド無しの部材で 0 だった）と、始端／終端の高さのパラメータが
//	    バウンドのオフセットを持つのか**が未確認のまま残った。ここが 2 巡目の主目的。
//	  * 量の名前空間は当て推量でなく**列挙できる**ので、`EQTOFunction` の 17 個を
//	    `ISDK::ExecQTOFunction` で全部試す。
//

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

	std::string ProbeI187_CompileError(VectorWorks::ECriteriaExpressionError err)
	{
		using VectorWorks::ECriteriaExpressionError;
		switch (err)
		{
		case ECriteriaExpressionError::None:
			return "None";
		case ECriteriaExpressionError::Unkonwn:
			return "Unkonwn";
		case ECriteriaExpressionError::CommaExpected:
			return "CommaExpected";
		case ECriteriaExpressionError::InvalCellRef:
			return "InvalCellRef";
		case ECriteriaExpressionError::InvalChar:
			return "InvalChar";
		case ECriteriaExpressionError::InvalExpr:
			return "InvalExpr";
		case ECriteriaExpressionError::InvalFactor:
			return "InvalFactor";
		case ECriteriaExpressionError::InvalIdent:
			return "InvalIdent";
		case ECriteriaExpressionError::InvalOperator:
			return "InvalOperator";
		case ECriteriaExpressionError::InvalRecordRef:
			return "InvalRecordRef";
		case ECriteriaExpressionError::InvalString:
			return "InvalString";
		case ECriteriaExpressionError::InvalTypes:
			return "InvalTypes";
		case ECriteriaExpressionError::LeftBracketExpected:
			return "LeftBracketExpected";
		case ECriteriaExpressionError::LeftParenExpected:
			return "LeftParenExpected";
		case ECriteriaExpressionError::PeriodExpected:
			return "PeriodExpected";
		case ECriteriaExpressionError::RightBracketExpected:
			return "RightBracketExpected";
		case ECriteriaExpressionError::RightParenExpected:
			return "RightParenExpected";
		default:
			break;
		}
		return std::string("(") + ProbeI187_Int(static_cast<long long>(err)) + ")";
	}

	std::string ProbeI187_VariantText(const VWVariant& value)
	{
		switch (value.GetType())
		{
		case eVWVariantType_Empty:
			return "(empty)";
		case eVWVariantType_SignedInteger:
			return "i:" + ProbeI187_Int(value.GetSint32());
		case eVWVariantType_UnsignedInteger:
			return "u:" + ProbeI187_Int(value.GetUint32());
		case eVWVariantType_Double:
			return "d:" + ProbeI187_Num(value.GetDouble());
		case eVWVariantType_Float:
			return "f:" + ProbeI187_Num(value.GetFloat());
		case eVWVariantType_Bool:
			return std::string("b:") + (value.GetBool() ? "true" : "false");
		case eVWVariantType_String:
			return "s:'" + ProbeI187_OneLine(ProbeI187_FromTX(value.GetTXString())) + "'";
		default:
			break;
		}
		return "type=" + ProbeI187_Int(static_cast<long long>(value.GetType())) + " s:'" +
			   ProbeI187_OneLine(ProbeI187_FromTX(value.GetTXString())) + "'";
	}

	// -----------------------------------------------------------------------
	// ワークシート式を 1 本、対象ハンドルに対して評価する。
	//
	//	**判定を呼び出し側に任せない。** 1 巡目で分かったとおり、知らない識別子も
	//	コンパイルは通って「自分自身の文字列」に評価されるので、
	//	**「戻り値が入れた綴りそのままの文字列なら、その関数は存在しない」**が唯一の
	//	判定条件である。それをここで付けてログへ出す。
	void ProbeI187_EvalWS(vwprobe::Report& probe, const std::string& label, MCObjectHandle hTarget,
						  const std::string& expression)
	{
		std::string line = "[A] " + label + " | " + expression + " | ";

		VWFC::Tools::WSCriteriaExpression compiled;
		const bool okLocal = compiled.Compile(TXString(expression.c_str()), VectorWorks::eLocal);
		if (!okLocal)
		{
			probe.log(
				line + "compile=" + ProbeI187_CompileError(compiled.GetLastError()) +
				" offset=" + ProbeI187_Int(static_cast<long long>(compiled.GetLastErrorOffset())) +
				" | 判定=**構文として通らない**");
			return;
		}

		VWVariant result;
		const bool ran = compiled.ExecWSExpression(hTarget, result);
		line += std::string("exec=") + (ran ? "true" : "false") + " -> " +
				ProbeI187_VariantText(result);

		// 判定。
		const bool echoed = result.GetType() == eVWVariantType_String &&
							ProbeI187_FromTX(result.GetTXString()) == expression;
		if (!ran)
			line += " | 判定=実行が false（関数は在るが値を出せない見込み）";
		else if (echoed)
			line += " | 判定=**入れた綴りがそのまま返った＝この名前の関数は無い**";
		else
			line += " | 判定=値が返った";
		probe.log(line);
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

	// ---- 部材 A（FL レイヤ＋ストーリバウンド）
	MCObjectHandle hPathA = gSDK->Create3DPoly();
	if (hPathA != nil)
	{
		gSDK->Add3DVertex(hPathA, WorldPt3(0.0, 0.0, 0.0));
		gSDK->Add3DVertex(hPathA, WorldPt3(4000.0, 0.0, 0.0));
	}
	MCObjectHandle hMemberA = gSDK->CreateCustomObjectPath("StructuralMember", hPathA, nil, true);
	if (hMemberA == nil)
		probe.fail("部材 A を作れなかった");
	else
	{
		if (hLayerFL != nil)
			gSDK->AddObjectToContainer(hMemberA, hLayerFL);
		SStoryObjectData bound;
		bound.fBound = eStoryObjectBound_Story;
		bound.fBoundStory = 0;
		bound.fLayerLevelType = levelTypeFL;
		bound.fOffset = -872.0;
		gSDK->SetObjectStoryBound(hMemberA, 0, bound);
		bound.fOffset = -302.0;
		gSDK->SetObjectStoryBound(hMemberA, 1, bound);
		gSDK->ResetObject(hMemberA);
		ProbeI187_DumpMember(probe, "A", hMemberA, true);
	}

	// ---- 部材 B（伏図レイヤ・バウンド無し）
	MCObjectHandle hPathB = gSDK->Create3DPoly();
	if (hPathB != nil)
	{
		gSDK->Add3DVertex(hPathB, WorldPt3(0.0, 1000.0, 0.0));
		gSDK->Add3DVertex(hPathB, WorldPt3(4000.0, 1000.0, 570.0));
	}
	MCObjectHandle hMemberB = gSDK->CreateCustomObjectPath("StructuralMember", hPathB, nil, true);
	if (hMemberB == nil)
		probe.fail("部材 B を作れなかった");
	else
	{
		if (hLayerPlan != nil)
			gSDK->AddObjectToContainer(hMemberB, hLayerPlan);
		ProbeI187_DumpMember(probe, "B", hMemberB, false);
	}

	// =======================================================================
	// 2. ワークシート式。**判定は戻り値で行う**（1 巡目の教訓）。
	// =======================================================================
	probe.log("=== 2. ワークシート式（判定は戻り値。入れた綴りが返ったら関数は無い）===");

	if (hMemberA != nil)
	{
		// バウンド付きの部材で、1 巡目に「実在するが 0 だった」ものを引き直す。
		static const char* const kBoundExpressions[] = {
			"TOPBOUND",
			"BOTBOUND",
			"HEIGHT",
			"LENGTH",
			"LAYER",
			"STORY",
			"'StructuralMember'.'StartElevation'",
			"'StructuralMember'.'EndElevation'",
			"'StructuralMember'.'DialogStartElevation'",
			"'StructuralMember'.'DialogEndElevation'",
			"'StructuralMember'.'DialogStartElevationReference'",
			"'StructuralMember'.'DialogStartElevation'+3571"};
		for (size_t i = 0; i < sizeof(kBoundExpressions) / sizeof(kBoundExpressions[0]); ++i)
			ProbeI187_EvalWS(probe, "部材A(バウンド有)", hMemberA, kBoundExpressions[i]);
	}

	if (hMemberB != nil)
	{
		// レイヤ・ストーリの**高さ**へ届く名前を探す。1 巡目で外れた綴りは除いてある。
		static const char* const kCandidates[] = {"LAYERELEV",
												  "LAYERELEVATIONVALUE",
												  "LAYERZVALUE",
												  "LAYERHEIGHT",
												  "LAYERWALLHEIGHT",
												  "LAYERDELTA",
												  "ELEV",
												  "ELEVATIONVALUE",
												  "OBJECTELEV",
												  "ZVALUE",
												  "DELTAZ",
												  "LEVEL",
												  "STORYLEVEL",
												  "STORYHEIGHT",
												  "STORYELEV",
												  "LEVELELEV",
												  "TOPBOUNDELEV",
												  "BOTBOUNDELEV",
												  "TOPBOUNDHEIGHT",
												  "BOTBOUNDHEIGHT",
												  "WALLHEIGHT",
												  "TOPZ",
												  "BOTZ",
												  "MINZ",
												  "MAXZ",
												  "LAYERNUM",
												  "LAYERSCALE"};
		for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i)
			ProbeI187_EvalWS(probe, "部材B", hMemberB, kCandidates[i]);

		// 量の名前空間は**列挙できる**（EQTOFunction は 17 個で全部）。
		probe.log("--- ExecQTOFunction（EQTOFunction の 17 個を全部）---");
		static const struct
		{
			const char* name;
			EQTOFunction function;
		} kQTO[] = {{"Angle", EQTOFunction::Angle},
					{"Count", EQTOFunction::Count},
					{"Length", EQTOFunction::Length},
					{"Perimeter", EQTOFunction::Perimeter},
					{"Width", EQTOFunction::Width},
					{"Height", EQTOFunction::Height},
					{"Depth", EQTOFunction::Depth},
					{"Weight", EQTOFunction::Weight},
					{"Area", EQTOFunction::Area},
					{"SurfaceArea", EQTOFunction::SurfaceArea},
					{"ProjectedArea", EQTOFunction::ProjectedArea},
					{"FootPrintArea", EQTOFunction::FootPrintArea},
					{"CrossSectionArea", EQTOFunction::CrossSectionArea},
					{"SpecialArea", EQTOFunction::SpecialArea},
					{"Volume", EQTOFunction::Volume},
					{"ObjectData", EQTOFunction::ObjectData},
					{"Thickness", EQTOFunction::Thickness}};
		for (size_t i = 0; i < sizeof(kQTO) / sizeof(kQTO[0]); ++i)
		{
			VWVariant result;
			const bool ran = gSDK->ExecQTOFunction(hMemberB, kQTO[i].function, "", result);
			probe.log(std::string("[QTO] ") + kQTO[i].name + " | exec=" + (ran ? "true" : "false") +
					  " -> " + ProbeI187_VariantText(result));
		}
	}

	// =======================================================================
	// 3. データタグのタグフィールドの式。
	// =======================================================================
	probe.log("=== 3. データタグのタグフィールドの式 ===");

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
		bool worksheetMode;
	};

	// ---- 3-A. バウンド付きの部材（**この調査の要**。1 巡目で取り損ねた分）
	if (hMemberA != nil)
	{
		MCObjectHandle hTagA = nil;
		MCObjectHandle hTextA = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberA,
												   hLayerFL, 6000.0, "A", hTagA);
		if (hTextA != nil)
		{
			static const ProbeI187_Case kCasesA[] = {
				{"A-見張り", "#StructuralMember#.#MemberID#", false},
				{"A-IPZL", "#IPZL#", false},
				{"A-StartElevation", "#StructuralMember#.#StartElevation#", false},
				{"A-EndElevation", "#StructuralMember#.#EndElevation#", false},
				{"A-DialogStartElev", "#StructuralMember#.#DialogStartElevation#", false},
				{"A-DialogEndElev", "#StructuralMember#.#DialogEndElevation#", false},
				{"A-DialogStartRef", "#StructuralMember#.#DialogStartElevationReference#", false},
				{"A-DialogEndRef", "#StructuralMember#.#DialogEndElevationReference#", false},
				{"A-連動の候補", "\" (2FL \"#StructuralMember#.#DialogStartElevation#\")\"", false},
				{"A-WS-TOPBOUND", "TOPBOUND", true},
				{"A-WS-BOTBOUND", "BOTBOUND", true},
				{"A-WS-DialogStartElev", "'StructuralMember'.'DialogStartElevation'", true}};
			for (size_t i = 0; i < sizeof(kCasesA) / sizeof(kCasesA[0]); ++i)
			{
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagA, hTextA,
										 kCasesA[i].label, kCasesA[i].formula,
										 kCasesA[i].worksheetMode);
			}
		}
	}

	// ---- 3-B. 書式修飾子の綴りを詰める（1 巡目は「どう書いても同じ」で終わった）
	if (hMemberB != nil)
	{
		MCObjectHandle hTagB = nil;
		MCObjectHandle hTextB = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberB,
												   hLayerPlan, 9000.0, "B", hTagB);
		if (hTextB != nil)
		{
			// `MajorDepth` は 600（正の 3 桁）。`*10` で 6000 にすれば、桁区切りが
			// 効いているかどうかが 1 目で分かる。
			static const ProbeI187_Case kCasesB[] = {
				{"B-見張り", "#StructuralMember#.#MajorDepth#", false},
				{"B-桁区切り-無印", "#StructuralMember#.#MajorDepth#*10", false},
				{"B-桁区切り-thsep", "#StructuralMember#.#MajorDepth#*10#thsep#", false},
				{"B-桁区切り-二重#", "#StructuralMember#.#MajorDepth##thsep#", false},
				{"B-桁区切り-単一#", "#StructuralMember#.#MajorDepth#thsep#", false},
				{"B-符号-二重#", "#StructuralMember#.#MajorDepth##sign#", false},
				{"B-符号-単一#", "#StructuralMember#.#MajorDepth#sign#", false},
				{"B-連鎖-issue の綴り", "#StructuralMember#.#MajorDepth##thsep#sign#", false},
				{"B-未知の修飾子-二重#", "#StructuralMember#.#MajorDepth##t187nosuch#", false},
				{"B-未知の修飾子-単一#", "#StructuralMember#.#MajorDepth#t187nosuch#", false},
				// 小数の見え方（1 巡目の `#IPZL#/2` が `-1349 1/2` になった件）。
				{"B-小数", "#StructuralMember#.#MajorDepth#/7", false},
				// 引用符なしの演算子・括弧はどう壊れるか（1 巡目は空文字列だった）。
				{"B-引用符なし括弧のみ", "(2FL )", false},
				{"B-引用符なし後置", "#IPZL# (2FL )", false},
				{"B-引用符あり後置", "#IPZL#\" (2FL )\"", false}};
			for (size_t i = 0; i < sizeof(kCasesB) / sizeof(kCasesB[0]); ++i)
			{
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagB, hTextB,
										 kCasesB[i].label, kCasesB[i].formula,
										 kCasesB[i].worksheetMode);
			}
		}
	}

	probe.log("=== 終わり ===");
}
