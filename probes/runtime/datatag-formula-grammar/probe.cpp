//
//	probes/runtime/datatag-formula-grammar/probe.cpp
//
//	[issue #187] データタグのタグフィールドの式（`IDataTagTextLinkSupport::SetFormula`）で
//	**何が書けて**（四則演算・文字列・条件式・数値の書式）**何が読めるか**（構造材の
//	始端／終端の高さ・ストーリバウンドのオフセット・レイヤ／ストーリ／レベルの高さ）を
//	実測する。
//
//	**目視は要らない作りにしてある。** 式の評価結果は 2 つの口から読み戻してログへ出す:
//
//	  ① `IDataTagSupport::GetDataTagExtractedData`（タグが抽出した「ラベル → 値」の対）
//	  ② タグが描いた図形を走査して拾ったテキストの文字列
//
//	どちらも文字列なので、**「何が出たか」はログだけで判定できる**（ダイアログの絵を
//	見る必要が無い）。
//
//	もう 1 つ、**ワークシート式の評価器**（`VWFC::Tools::WSCriteriaExpression` ＝
//	`ISDK::CompileCriteriaExpression` ＋ `ISDK::ExecWSExpression`）でも同じ問いを引く。
//	こちらは**コンパイル段階の誤りを enum で返す**ので、「その識別子は存在しない」と
//	「存在するが値が空」を機械的に区別できる——候補を総当たりするのに向いている。
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
		// 末尾の 0 を落として読みやすくする（2699.0000 → 2699）。
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

	// 制御文字（改行・タブ）が混ざったログ行で表がずれないように 1 行へ畳む。
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
	// コンパイル誤りの enum を読める名前に直す（`ECriteriaExpressionError`）。

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
	// 【harness A】ワークシート式を 1 本、対象ハンドルに対して評価する。
	//
	//	`=` は付けない（SDK のコメントの例が `"Area()/2"` である）。ローカル文脈と
	//	universal 文脈の両方でコンパイルを試し、**通ったほうで実行する**——どちらで
	//	書くのかがそもそも分からないので、ここで決めさせる。
	void ProbeI187_EvalWS(vwprobe::Report& probe, const std::string& label, MCObjectHandle hTarget,
						  const std::string& expression)
	{
		std::string line = "[A] " + label + " | " + expression + " | ";

		VWFC::Tools::WSCriteriaExpression compiledLocal;
		const bool okLocal =
			compiledLocal.Compile(TXString(expression.c_str()), VectorWorks::eLocal);

		VWFC::Tools::WSCriteriaExpression compiledUniv;
		const bool okUniv =
			compiledUniv.Compile(TXString(expression.c_str()), VectorWorks::eUniversal);

		line += "compile local=";
		line += okLocal ? "ok" : ProbeI187_CompileError(compiledLocal.GetLastError());
		line += " univ=";
		line += okUniv ? "ok" : ProbeI187_CompileError(compiledUniv.GetLastError());

		VWFC::Tools::WSCriteriaExpression* used = nullptr;
		const char* usedName = nullptr;
		if (okLocal)
		{
			used = &compiledLocal;
			usedName = "local";
		}
		else if (okUniv)
		{
			used = &compiledUniv;
			usedName = "univ";
		}

		if (used == nullptr)
		{
			probe.log(line + " | exec=(コンパイルできないので実行しない)");
			return;
		}

		VWVariant result;
		const bool ran = used->ExecWSExpression(hTarget, result);
		line += std::string(" | exec(") + usedName + ")=";
		line += ran ? "true" : "false";
		line += " -> " + ProbeI187_VariantText(result);
		probe.log(line);
	}

	// -----------------------------------------------------------------------
	// 【harness B】タグの 1 本のテキストへ式を持たせ、更新して結果を読み戻す。
	//
	//	読み戻しは 2 経路。どちらも文字列なので、目視は要らない。
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

		// 書いた式がそのまま読み戻るか（VW が式を書き換える／捨てることがあるため）。
		const std::string readBack = ProbeI187_FromTX(linkSupport->GetFormula(hText));
		if (readBack != formula)
			line += " | 式の読み戻し='" + ProbeI187_OneLine(readBack) + "'";
		line += std::string(" | wsFlag=") + (linkSupport->GetIsWorksheetFormula(hText) ? "1" : "0");

		// ① タグが抽出した値。
		VectorWorks::Extension::TXStringSTLPairArray extracted;
		const bool gotExtracted = tagSupport->GetDataTagExtractedData(hTag, extracted);
		line += " | extracted(" + std::string(gotExtracted ? "true" : "false") + ")=";
		if (extracted.empty())
			line += "(0 件)";
		for (size_t i = 0; i < extracted.size(); ++i)
		{
			line += "{'" + ProbeI187_OneLine(ProbeI187_FromTX(extracted[i].first)) + "'->'" +
					ProbeI187_OneLine(ProbeI187_FromTX(extracted[i].second)) + "'}";
		}

		// ② 描かれた文字列（タグの生成した図形を 1 段だけ走査する）。
		line += " | drawn=";
		size_t drawnCount = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hTag); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) != kTextNode)
				continue;
			const VWFC::VWObjects::VWTextBlockObj textObj(member);
			line += "'" + ProbeI187_OneLine(ProbeI187_FromTX(textObj.GetText())) + "'";
			++drawnCount;
			if (drawnCount >= 8)
			{
				line += "…";
				break;
			}
		}
		if (drawnCount == 0)
			line += "(テキスト 0 件)";

		probe.log(line);
	}
} // namespace

VW_PROBE("datatag-formula-grammar", "データタグの式を総当たりで実測する",
		 "式の演算・文字列・条件・書式と、高さを読めるフィールドをログだけで判定する")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	// 何かを作る前に 1 度。これが無いと最初の 1 個で「オブジェクトの設定」ダイアログが
	// 出て止まる（probes/runtime/README.md）。
	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	// =======================================================================
	// 1. 舞台を作る。**数値はすべて互いに違う値**にして、式が何を返したのかを
	//    ログの数字だけで言い当てられるようにする。
	//
	//	  ストーリ T187-2F   高さ 3571（レベル T187-FL は階内相対 0 ＝ 絶対 3571）
	//	  ストーリ T187-PLAN 高さ 2699（レベル T187-PLAN-L。伏図レイヤの見立て）
	//	  部材 A: T187-FL レイヤに置き、両端へストーリバウンド（オフセット −872 / −302）
	//	  部材 B: T187-PLAN レイヤに置き、バウンドは書かない（局所Z 0 → 570）
	//
	//	現場の形（伏図レイヤの高さが FL と違う）は部材 B が写している。
	//	「FL からの高さ」＝ 2699 − 3571 ＋ 局所Z なので、**式がレイヤ高さ 2699 と
	//	ストーリ高さ 3571 のどちらかへ届くかどうか**が答えを分ける。
	// =======================================================================
	probe.log("=== 1. 舞台を作る ===");

	TXString levelTypeFL("T187-FL");
	TXString levelTypePlan("T187-PLAN-L");
	probe.log(std::string("CreateLayerLevelType(T187-FL)=") +
			  (gSDK->CreateLayerLevelType(levelTypeFL) ? "true" : "false"));
	probe.log(std::string("CreateLayerLevelType(T187-PLAN-L)=") +
			  (gSDK->CreateLayerLevelType(levelTypePlan) ? "true" : "false"));

	TXString storyNameFL("T187-2F");
	TXString storySuffixFL("T187A");
	probe.log(std::string("CreateStory(T187-2F)=") +
			  (gSDK->CreateStory(storyNameFL, storySuffixFL) ? "true" : "false"));
	MCObjectHandle hStoryFL = gSDK->GetNamedObject("T187-2F");
	if (hStoryFL == nil)
		probe.fail("T187-2F の階を GetNamedObject で引けなかった");
	else
		probe.log(std::string("SetStoryElevation(T187-2F, 3571)=") +
				  (gSDK->SetStoryElevation(hStoryFL, 3571.0) ? "true" : "false"));

	TXString storyNamePlan("T187-PLAN");
	TXString storySuffixPlan("T187B");
	probe.log(std::string("CreateStory(T187-PLAN)=") +
			  (gSDK->CreateStory(storyNamePlan, storySuffixPlan) ? "true" : "false"));
	MCObjectHandle hStoryPlan = gSDK->GetNamedObject("T187-PLAN");
	if (hStoryPlan == nil)
		probe.fail("T187-PLAN の階を GetNamedObject で引けなかった");
	else
		probe.log(std::string("SetStoryElevation(T187-PLAN, 2699)=") +
				  (gSDK->SetStoryElevation(hStoryPlan, 2699.0) ? "true" : "false"));

	// レベル（＝レイヤ）はレベルテンプレート経由でしか紐付かない
	// （Findings「レイヤ・ストーリ・重ね順」）。
	short templateIndexFL = -1;
	TXString templateNameFL("T187-TPL-FL");
	gSDK->CreateStoryLevelTemplate(templateNameFL, 1.0, levelTypeFL, 0.0, 2400.0, templateIndexFL);
	short templateIndexPlan = -1;
	TXString templateNamePlan("T187-TPL-PLAN");
	gSDK->CreateStoryLevelTemplate(templateNamePlan, 1.0, levelTypePlan, 0.0, 2400.0,
								   templateIndexPlan);
	probe.log("レベルテンプレート index: FL=" + ProbeI187_Int(templateIndexFL) +
			  " PLAN=" + ProbeI187_Int(templateIndexPlan));

	MCObjectHandle hLayerFL = nil;
	MCObjectHandle hLayerPlan = nil;
	if (hStoryFL != nil && templateIndexFL >= 0)
	{
		gSDK->AddStoryLevelFromTemplate(hStoryFL, templateIndexFL);
		hLayerFL = gSDK->GetLayerForStory(hStoryFL, levelTypeFL);
	}
	if (hStoryPlan != nil && templateIndexPlan >= 0)
	{
		gSDK->AddStoryLevelFromTemplate(hStoryPlan, templateIndexPlan);
		hLayerPlan = gSDK->GetLayerForStory(hStoryPlan, levelTypePlan);
	}
	probe.log(std::string("レイヤ: FL=") + (hLayerFL != nil ? "取れた" : "nil") +
			  " PLAN=" + (hLayerPlan != nil ? "取れた" : "nil"));
	if (hLayerFL == nil || hLayerPlan == nil)
		probe.fail("ストーリのレベルからレイヤを取れなかった（以後の高さの解釈が付かない）");

	if (hStoryFL != nil)
	{
		probe.log(
			"実測 T187-2F: GetStoryElevation=" + ProbeI187_Num(gSDK->GetStoryElevation(hStoryFL)) +
			" GetStoryLevelElevation(T187-FL)=" +
			ProbeI187_Num(gSDK->GetStoryLevelElevation(hStoryFL, levelTypeFL)));
	}
	if (hStoryPlan != nil)
	{
		probe.log("実測 T187-PLAN: GetStoryElevation=" +
				  ProbeI187_Num(gSDK->GetStoryElevation(hStoryPlan)) +
				  " GetStoryLevelElevation(T187-PLAN-L)=" +
				  ProbeI187_Num(gSDK->GetStoryLevelElevation(hStoryPlan, levelTypePlan)));
	}

	// ---- 部材 A（FL レイヤ＋ストーリバウンド）
	MCObjectHandle hPathA = gSDK->Create3DPoly();
	if (hPathA != nil)
	{
		gSDK->Add3DVertex(hPathA, WorldPt3(0.0, 0.0, 0.0));
		gSDK->Add3DVertex(hPathA, WorldPt3(4000.0, 0.0, 0.0));
	}
	MCObjectHandle hMemberA = gSDK->CreateCustomObjectPath("StructuralMember", hPathA, nil, true);
	if (hMemberA == nil)
		probe.fail("部材 A を作れなかった（CreateCustomObjectPath が nil）");
	else
	{
		if (hLayerFL != nil)
			gSDK->AddObjectToContainer(hMemberA, hLayerFL);
		SStoryObjectData boundStart;
		boundStart.fBound = eStoryObjectBound_Story;
		boundStart.fBoundStory = 0;
		boundStart.fLayerLevelType = levelTypeFL;
		boundStart.fOffset = -872.0;
		SStoryObjectData boundEnd = boundStart;
		boundEnd.fOffset = -302.0;
		probe.log(std::string("部材 A バウンド: ID0=") +
				  (gSDK->SetObjectStoryBound(hMemberA, 0, boundStart) ? "true" : "false") +
				  " ID1=" + (gSDK->SetObjectStoryBound(hMemberA, 1, boundEnd) ? "true" : "false") +
				  " 件数=" +
				  ProbeI187_Int(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(hMemberA))));
		gSDK->ResetObject(hMemberA);
		probe.log("部材 A 解決Z: ID0=" + ProbeI187_Num(gSDK->GetObjectBoundElevation(hMemberA, 0)) +
				  " ID1=" + ProbeI187_Num(gSDK->GetObjectBoundElevation(hMemberA, 1)));
	}

	// ---- 部材 B（伏図レイヤ・バウンド無し。局所Z 0 → 570）
	MCObjectHandle hPathB = gSDK->Create3DPoly();
	if (hPathB != nil)
	{
		gSDK->Add3DVertex(hPathB, WorldPt3(0.0, 1000.0, 0.0));
		gSDK->Add3DVertex(hPathB, WorldPt3(4000.0, 1000.0, 570.0));
	}
	MCObjectHandle hMemberB = gSDK->CreateCustomObjectPath("StructuralMember", hPathB, nil, true);
	if (hMemberB == nil)
		probe.fail("部材 B を作れなかった（CreateCustomObjectPath が nil）");
	else if (hLayerPlan != nil)
		gSDK->AddObjectToContainer(hMemberB, hLayerPlan);

	// 部材のレコードから、始端／終端の高さらしいパラメータの実値を読んでおく
	// （式が返した数字がどのパラメータなのかを突き合わせるため）。
	if (hMemberB != nil)
	{
		const VWFC::VWObjects::VWParametricObj memberObjB(hMemberB);
		static const char* const kWatchParams[] = {"StartElevation",
												   "EndElevation",
												   "DialogStartElevation",
												   "DialogEndElevation",
												   "DialogStartElevationReference",
												   "DialogEndElevationReference",
												   "OffsetZ",
												   "MajorDepth",
												   "MemberID"};
		for (size_t i = 0; i < sizeof(kWatchParams) / sizeof(kWatchParams[0]); ++i)
		{
			probe.log(
				std::string("部材 B レコード: ") + kWatchParams[i] + "='" +
				ProbeI187_OneLine(ProbeI187_FromTX(memberObjB.GetParamValue(kWatchParams[i]))) +
				"'");
		}
	}

	// =======================================================================
	// 2. 【harness A】ワークシート式の評価器で総当たりする。
	//
	//	コンパイル誤りが enum で返るので、`InvalIdent` なら「そんな識別子は無い」、
	//	`None` なら「識別子は在る」と**機械的に**言える。ここが候補の総当たりを
	//	当て推量から実測へ変える。
	// =======================================================================
	probe.log("=== 2. ワークシート式（CompileCriteriaExpression + ExecWSExpression）===");
	probe.log("[A] 対象 | 式 | コンパイル結果 | 実行結果");

	if (hMemberB != nil)
	{
		static const char* const kExpressions[] = {
			// --- 算術そのもの（式の言語に演算があるか）
			"1+1", "872-40", "2*3", "6/2", "(1+2)*3", "-872",
			// --- 部材のレコードを引く 2 通りの綴り
			"'StructuralMember'.'StartElevation'", "'StructuralMember'.'EndElevation'",
			"'StructuralMember'.'StartElevation'+872", "'StructuralMember'.'MemberID'",
			"OBJECTDATA('StructuralMember','StartElevation')",
			// --- 高さ・位置を返しそうな関数の候補
			"Z", "Z()", "ZHEIGHT", "HEIGHT", "Height", "HEIGHT()", "ELEVATION", "TOPELEVATION",
			"BOTELEVATION", "BOTTOMELEVATION", "IPZL", "IPZ", "IPX", "IPY", "LENGTH", "Length",
			// --- レイヤ・ストーリ・レベルへ届くか（**この調査の要**）
			"LAYER", "LAYERNAME", "LAYERELEVATION", "LAYERZ", "LAYERDELTAZ", "LAYERCUTPLANE",
			"STORY", "STORYNAME", "STORYELEVATION", "LEVELNAME", "LEVELELEVATION", "TOPBOUNDOFFSET",
			"BOTBOUNDOFFSET", "TOPBOUND", "BOTBOUND",
			// --- 文字列・書式・条件の関数
			"CONCAT('a','b')", "NUMTOSTR(0,872)", "ROUND(872.4)", "IF(1=1,'a','b')", "ABS(-872)",
			// --- 存在しないはずの識別子（対照。InvalIdent が返る形を見るため）
			"T187NOSUCHFUNCTION"};
		for (size_t i = 0; i < sizeof(kExpressions) / sizeof(kExpressions[0]); ++i)
			ProbeI187_EvalWS(probe, "部材B", hMemberB, kExpressions[i]);

		// 同じ式を**部材 A**（バウンド付き・FL レイヤ）でも引いて、値の出どころを
		// 突き合わせる。数の多い総当たりは B で済んでいるので、ここは高さだけ。
		if (hMemberA != nil)
		{
			static const char* const kHeightOnly[] = {"Z",
													  "IPZL",
													  "LAYERELEVATION",
													  "STORYELEVATION",
													  "TOPBOUNDOFFSET",
													  "BOTBOUNDOFFSET",
													  "'StructuralMember'.'StartElevation'",
													  "'StructuralMember'.'EndElevation'"};
			for (size_t i = 0; i < sizeof(kHeightOnly) / sizeof(kHeightOnly[0]); ++i)
				ProbeI187_EvalWS(probe, "部材A", hMemberA, kHeightOnly[i]);
		}
		// レイヤ・ストーリのハンドルを直接対象にしても引けるのか。
		if (hLayerPlan != nil)
			ProbeI187_EvalWS(probe, "伏図レイヤ", hLayerPlan, "LAYERELEVATION");
		if (hStoryFL != nil)
			ProbeI187_EvalWS(probe, "階2F", hStoryFL, "STORYELEVATION");
	}

	// =======================================================================
	// 3. 【harness B】データタグのタグフィールドの式。
	// =======================================================================
	probe.log("=== 3. データタグのタグフィールドの式 ===");

	IDataTagSupportPtr tagSupport(IID_DataTagSupport);
	IDataTagTextLinkSupportPtr linkSupport(IID_DataTagTextLinkSupport);
	if (!tagSupport || !linkSupport)
	{
		probe.fail("IDataTagSupport / IDataTagTextLinkSupport を取れなかった");
		return;
	}

	MCObjectHandle hTag = gSDK->CreateCustomObject("Data Tag", WorldPt(6000.0, 0.0), 0.0, true);
	if (hTag == nil)
	{
		probe.fail("データタグを作れなかった（CreateCustomObject が nil）");
		return;
	}
	if (hLayerPlan != nil)
		gSDK->AddObjectToContainer(hTag, hLayerPlan);
	if (hMemberB != nil)
	{
		probe.log(std::string("AssociateWithObject(タグ, 部材B)=") +
				  (tagSupport->AssociateWithObject(hTag, hMemberB) ? "true" : "false"));
	}

	// 式を持たせるテキストを 1 本だけ用意する。
	//
	//	**既定のレイアウトに `IsSupported` なテキストが在れば、それを複製して使う**
	//	——図面ラベルでは「効いているのは文字ではなくテキストが抱えている隠れた状態」
	//	だった（Findings「図面ラベル」）。データタグでは `SetFormula` が使えるはずだが、
	//	使えなかったときに 1 回の実機確認を丸ごと無駄にしないよう、先に既定のテキストを
	//	当たっておく。
	MCObjectHandle hDefaultGroup = gSDK->GetCustomObjectProfileGroup(hTag);
	MCObjectHandle hSupportedSeed = nil;
	if (hDefaultGroup != nil)
	{
		for (MCObjectHandle member = gSDK->FirstMemberObj(hDefaultGroup); member != nil;
			 member = gSDK->NextObject(member))
		{
			const short type = gSDK->GetObjectTypeN(member);
			probe.log(
				"既定レイアウト: 型=" + ProbeI187_Int(type) +
				(type == kTextNode
					 ? std::string(" IsSupported=") +
						   (linkSupport->IsSupported(member) ? "true" : "false") + " 式='" +
						   ProbeI187_OneLine(ProbeI187_FromTX(linkSupport->GetFormula(member))) +
						   "'"
					 : std::string()));
			if (type == kTextNode && hSupportedSeed == nil && linkSupport->IsSupported(member))
				hSupportedSeed = member;
		}
	}
	else
		probe.log("既定レイアウト: GetCustomObjectProfileGroup が nil");

	MCObjectHandle hGroup = gSDK->CreateGroup(false);
	MCObjectHandle hText = nil;
	if (hSupportedSeed != nil)
	{
		hText = gSDK->DuplicateObject(hSupportedSeed);
		probe.log("式を持たせるテキスト: 既定レイアウトのものを複製した");
	}
	else
	{
		hText = gSDK->CreateTextBlock("T187", WorldPt(0.0, 0.0), false, 0.0);
		probe.log(std::string("式を持たせるテキスト: CreateTextBlock で作った IsSupported=") +
				  (hText != nil && linkSupport->IsSupported(hText) ? "true" : "false"));
	}
	if (hGroup == nil || hText == nil)
	{
		probe.fail("タグレイアウトを組めなかった（群またはテキストが nil）");
		return;
	}
	gSDK->AddObjectToContainer(hText, hGroup);
	// **中身を入れてから渡す**（Findings「データタグ」）。
	probe.log(std::string("SetCustomObjectProfileGroup=") +
			  (gSDK->SetCustomObjectProfileGroup(hTag, hGroup) ? "true" : "false"));

	// 渡したときに VW が群を複製していることがあるので、**渡した後に取り直して**
	// その中のテキストを使う（Findings「データタグ」の「中身を入れてから渡す」）。
	MCObjectHandle hLiveGroup = gSDK->GetCustomObjectProfileGroup(hTag);
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
	probe.log(std::string("渡した後のテキスト: ") +
			  (hLiveText == nil ? "見つからない" : (hLiveText == hText ? "同一" : "複製された")) +
			  " IsSupported=" +
			  (hLiveText != nil && linkSupport->IsSupported(hLiveText) ? "true" : "false"));
	if (hLiveText == nil)
	{
		probe.fail("渡したレイアウトの中にテキストが無い（式を持たせる先が無い）");
		return;
	}

	probe.log("[B] 名 | ws | 入れた式 | 読み戻し | 抽出値 | 描かれた文字");

	// 3-0. 見張り。これが評価されないなら harness そのものが壊れている。
	ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTag, hLiveText, "見張り",
							 "#StructuralMember#.#MemberID#", false);

	struct ProbeI187_Case
	{
		const char* label;
		const char* formula;
		bool worksheetMode;
	};

	static const ProbeI187_Case kCases[] = {
		// --- 素の 1 フィールド（基準）
		{"素-IPZL", "#IPZL#", false},
		{"素-StartElevation", "#StructuralMember#.#StartElevation#", false},
		{"素-EndElevation", "#StructuralMember#.#EndElevation#", false},
		{"素-DialogStartElev", "#StructuralMember#.#DialogStartElevation#", false},
		{"素-DialogStartRef", "#StructuralMember#.#DialogStartElevationReference#", false},
		{"素-OffsetZ", "#StructuralMember#.#OffsetZ#", false},
		// --- 四則演算（issue の問い 1 の中心）
		{"演算-加", "#IPZL#+872", false},
		{"演算-加-空白付き", "#IPZL# + 872", false},
		{"演算-減", "#IPZL#-872", false},
		{"演算-乗", "#IPZL#*2", false},
		{"演算-除", "#IPZL#/2", false},
		{"演算-括弧", "(#IPZL#+872)*2", false},
		{"演算-定数のみ", "872+40", false},
		{"演算-フィールド同士",
		 "#StructuralMember#.#StartElevation#+#StructuralMember#.#EndElevation#", false},
		// --- 文字列
		{"文字-引用符あり", "\" (2FL \"", false},
		{"文字-連結", "\" (2FL \"#IPZL#\")\"", false},
		{"文字-引用符なし括弧", "(2FL )", false},
		{"文字-引用符なし記号", "\xc3\x97 #IPZL#", false},
		{"文字-plus連結", "\"a\"+\"b\"", false},
		{"文字-amp連結", "\"a\"&\"b\"", false},
		// --- 数値の書式（修飾子の綴り）
		{"書式-thsep", "#IPZL##thsep#", false},
		{"書式-sign", "#IPZL##sign#", false},
		{"書式-thsep+sign", "#IPZL##thsep##sign#", false},
		{"書式-未知の修飾子", "#IPZL##t187nosuch#", false},
		{"書式-unit", "#IPZL##unit#", false},
		{"書式-dim", "#IPZL##dim#", false},
		{"書式-round", "#IPZL##round#", false},
		// --- 条件式
		{"条件-単純", "#IPZL#@#IPZL#<>0:\"ゼロ\"", false},
		{"条件-既知の形",
		 "\" (\"@#IPZL#<>0:\"\"#IPZL##thsep##sign#@#IPZL#<>0:\"\"\")\"@#IPZL#<>0:\"\"", false},
		// --- レイヤ・ストーリへ届くか（**この調査の要**）
		{"外-LAYERELEVATION", "#LAYERELEVATION#", false},
		{"外-STORYELEVATION", "#STORYELEVATION#", false},
		{"外-LAYERNAME", "#LAYERNAME#", false},
		{"外-Z", "#Z#", false},
		// --- ワークシート式モード（第 3 引数 true）
		{"WS-定数", "1+1", true},
		{"WS-Z", "Z", true},
		{"WS-IPZL", "IPZL", true},
		{"WS-レコード", "'StructuralMember'.'StartElevation'", true},
		{"WS-レコード+定数", "'StructuralMember'.'StartElevation'+872", true},
		{"WS-LAYERELEVATION", "LAYERELEVATION", true},
		{"WS-イコール付き", "=1+1", true}};

	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTag, hLiveText, kCases[i].label,
								 kCases[i].formula, kCases[i].worksheetMode);
	}

	probe.log("=== 終わり ===");
}
