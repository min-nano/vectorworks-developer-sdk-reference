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
//	【3 巡目】1 巡目・2 巡目で文法（演算・文字列・条件・書式の綴り）は確定した。
//	残っているのは issue の本題ただ 1 つ——**「その階の FL から測った高さ」の数値を
//	式から出せるか**である。2 巡目までに分かったこと:
//
//	  * `#DialogStartElevationReference#` は**バウンド先のレベル名**を返す
//	    （部材 A で `T187-FL`、他階へ繋いだ部材 C では `T187-FL [上階]`）。名前は読める。
//	  * ところが `#DialogStartElevation#` は **0** のままで、バウンドに書いた
//	    オフセット（−872）が出てこない。`#StartElevation#` / `#EndElevation#` も 0。
//	  * ワークシート式にも高さを返す関数は無い（**候補 37 本が全部、自分自身の文字列を
//	    返した**）。取れるのは名前だけ（`LAYER` / `STORY`）。
//	  * ただし QTO の `Length` / `Area` / `Volume` がどれも 0 だったので、
//	    **部材のジオメトリが退化していた疑いが残る**——退化した本の読み値は当てにできない。
//
//	そこで 3 通りの経路を、**パスを読み戻してジオメトリが在ることを確かめてから**引く:
//
//	  経路 1: `SetObjectStoryBound` で書く（2 巡目と同じ。退化の有無を確かめた上で）
//	  経路 2: **パラメータの側から書く**（OIP の「始端高さ基準 / 始端高さオフセット」）
//	  経路 3: **動かしてから読み直す**——利用者の要望は「取り込み後に高さを変えても
//	          注記が追随すること」なので、**動かした後に式の出力が変わるか**が
//	          要望そのものの試験である
//
//	あわせて `#thsep#` を 4 桁の値（`MajorDepth` を 12345 にする）で引き直す
//	——2 巡目は 600 と −2699 でしか試せず決着しなかった。`#sign#` が `+600` を出した
//	ので**修飾子の仕組み自体は効いている**ことは分かっている。

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

	// パスを読み戻して、ジオメトリが退化していないかを確かめる。
	//	**退化した本（長さ 0）の読み値は当てにできない**（Findings「ResetObject が
	//	バウンドから作り直すのは『長さ 0』か『長さ 1e-7 以上』のパスだけ」）。
	void ProbeI187_DumpPath(vwprobe::Report& probe, const std::string& label,
							MCObjectHandle hMember)
	{
		MCObjectHandle hPath = gSDK->GetCustomObjectPath(hMember);
		if (hPath == nil)
		{
			probe.log("パス " + label + ": GetCustomObjectPath が nil");
			return;
		}
		WorldPt3 v0;
		WorldPt3 v1;
		gSDK->Get3DVertex(hPath, 1, v0);
		gSDK->Get3DVertex(hPath, 2, v1);
		WorldRect bounds;
		const bool gotBounds = gSDK->GetObjectBounds(hMember, bounds);
		probe.log("パス " + label + ": v1=(" + ProbeI187_Num(v0.x) + "," + ProbeI187_Num(v0.y) +
				  "," + ProbeI187_Num(v0.z) + ") v2=(" + ProbeI187_Num(v1.x) + "," +
				  ProbeI187_Num(v1.y) + "," + ProbeI187_Num(v1.z) +
				  ") Δz=" + ProbeI187_Num(v1.z - v0.z) + " 外接=" +
				  (gotBounds && bounds.right > bounds.left
					   ? "幅 " + ProbeI187_Num(bounds.right - bounds.left)
					   : "**空（図形が無い）**"));
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

	// ---- 部材 C（**現場の形そのまま**: 伏図レイヤ（2699）に置き、**別の階**
	//      T187-2F の FL レベル（3571）へバウンドする）。`fBoundStory` は
	//      **相対の階番号**なので、PLAN から見て 1 つ上の 2F は `+1`
	//      （Findings「同じオブジェクトに 2 本のバウンドを書く」）。
	//
	//	ここが issue #187 の核心。部材 A（FL レイヤに置いた本）で高さが読めても、
	//	**レイヤと基準の階が食い違うこの形で読めなければ意味が無い。**
	MCObjectHandle hPathC = gSDK->Create3DPoly();
	if (hPathC != nil)
	{
		gSDK->Add3DVertex(hPathC, WorldPt3(0.0, 2000.0, 0.0));
		gSDK->Add3DVertex(hPathC, WorldPt3(4000.0, 2000.0, 0.0));
	}
	MCObjectHandle hMemberC = gSDK->CreateCustomObjectPath("StructuralMember", hPathC, nil, true);
	if (hMemberC == nil)
		probe.fail("部材 C を作れなかった");
	else
	{
		if (hLayerPlan != nil)
			gSDK->AddObjectToContainer(hMemberC, hLayerPlan);
		SStoryObjectData boundC;
		boundC.fBound = eStoryObjectBound_Story;
		boundC.fBoundStory = 1; // PLAN から 1 つ上＝T187-2F
		boundC.fLayerLevelType = levelTypeFL;
		boundC.fOffset = -872.0;
		gSDK->SetObjectStoryBound(hMemberC, 0, boundC);
		boundC.fOffset = -302.0;
		gSDK->SetObjectStoryBound(hMemberC, 1, boundC);
		gSDK->ResetObject(hMemberC);
		ProbeI187_DumpMember(probe, "C(伏図レイヤ＋他階の FL バウンド)", hMemberC, true);
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
	// 2. 【3 巡目の本題】「FL からの高さ」の数値を式から出せるか。
	//
	//	2 巡目で分かったこと:
	//	  * `#DialogStartElevationReference#` は**バウンド先のレベル名**を返す
	//	    （部材 A で `T187-FL`、他階へ繋いだ部材 C では `T187-FL [上階]`）。
	//	    **名前は読める。**
	//	  * ところが `#DialogStartElevation#` は **0** のままで、バウンドに書いた
	//	    オフセット（−872）が出てこない。
	//	  * ただし QTO の `Length` / `Area` / `Volume` がどれも 0 だったので、
	//	    **部材のジオメトリが退化していた疑いが残る**（Findings「ResetObject が
	//	    バウンドから作り直すのは…」）。退化した本の読み値は当てにできない。
	//
	//	そこで 3 通りの経路を、**パスを読み戻してジオメトリが在ることを確かめてから**
	//	引く:
	//	  経路 1: `SetObjectStoryBound` で書く（2 巡目と同じ。退化の有無を確かめた上で）
	//	  経路 2: **パラメータの側から書く**（OIP の「始端高さ基準 / 始端高さオフセット」）
	//	  経路 3: **動かしてから読み直す**（`MoveObject3D`）——これが「オブジェクトと
	//	          連動しているか」の直接の試験である
	// =======================================================================
	probe.log("=== 2. 「FL からの高さ」を式から出せるか（3 巡目の本題）===");

	if (hMemberC != nil)
	{
		ProbeI187_DumpPath(probe, "C 作成直後", hMemberC);
	}

	// ---- 経路 2: パラメータの側から書いてみる。
	MCObjectHandle hMemberD = nil;
	{
		MCObjectHandle hPathD = gSDK->Create3DPoly();
		if (hPathD != nil)
		{
			gSDK->Add3DVertex(hPathD, WorldPt3(0.0, 3000.0, 0.0));
			gSDK->Add3DVertex(hPathD, WorldPt3(4000.0, 3000.0, 570.0));
		}
		hMemberD = gSDK->CreateCustomObjectPath("StructuralMember", hPathD, nil, true);
		if (hMemberD == nil)
			probe.fail("部材 D を作れなかった");
		else
		{
			if (hLayerPlan != nil)
				gSDK->AddObjectToContainer(hMemberD, hLayerPlan);
			const VWFC::VWObjects::VWParametricObj objD(hMemberD);
			VWFC::VWObjects::VWRecordFormatObj formatD = objD.GetRecordFormat();
			// **パラメータへ直に書く**（OIP の「始端高さ基準」「始端高さオフセット」）。
			formatD.SetParamValue("DialogStartElevationReference", levelTypeFL);
			formatD.SetParamReal("DialogStartElevation", -872.0);
			formatD.SetParamValue("DialogEndElevationReference", levelTypeFL);
			formatD.SetParamReal("DialogEndElevation", -302.0);
			gSDK->ResetObject(hMemberD);
			probe.log("部材 D: パラメータから書いた後の読み戻し DialogStartElevationReference='" +
					  ProbeI187_FromTX(objD.GetParamValue("DialogStartElevationReference")) +
					  "' DialogStartElevation='" +
					  ProbeI187_FromTX(objD.GetParamValue("DialogStartElevation")) +
					  "' DialogEndElevation='" +
					  ProbeI187_FromTX(objD.GetParamValue("DialogEndElevation")) + "'");
			probe.log(
				"部材 D: バウンド件数=" +
				ProbeI187_Int(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(hMemberD))) +
				" 解決Z ID0=" + ProbeI187_Num(gSDK->GetObjectBoundElevation(hMemberD, 0)) +
				" ID1=" + ProbeI187_Num(gSDK->GetObjectBoundElevation(hMemberD, 1)));
			ProbeI187_DumpPath(probe, "D 作成直後", hMemberD);
		}
	}

	// =======================================================================
	// 3. データタグのタグフィールドの式で読み直す。
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

	// 高さを読む式の一式（部材ごとに同じものを当てて見比べる）。
	static const ProbeI187_Case kHeightCases[] = {
		{"IPZL", "#IPZL#", false},
		{"StartElevation", "#StructuralMember#.#StartElevation#", false},
		{"EndElevation", "#StructuralMember#.#EndElevation#", false},
		{"DialogStartElev", "#StructuralMember#.#DialogStartElevation#", false},
		{"DialogEndElev", "#StructuralMember#.#DialogEndElevation#", false},
		{"DialogStartRef", "#StructuralMember#.#DialogStartElevationReference#", false},
		{"連動の候補", "\" (2FL \"#StructuralMember#.#DialogStartElevation#\")\"", false}};

	// ---- 3-C. 経路 1: `SetObjectStoryBound` で書いた本（現場の形）
	MCObjectHandle hTagC = nil;
	MCObjectHandle hTextC = nil;
	if (hMemberC != nil)
	{
		hTagC = nil;
		hTextC = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberC, hLayerPlan, 12000.0,
									"C", hTagC);
		if (hTextC != nil)
		{
			for (size_t i = 0; i < sizeof(kHeightCases) / sizeof(kHeightCases[0]); ++i)
			{
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagC, hTextC,
										 std::string("C(バウンドで書いた)-") +
											 kHeightCases[i].label,
										 kHeightCases[i].formula, kHeightCases[i].worksheetMode);
			}
		}
	}

	// ---- 3-D. 経路 2: **パラメータの側から書いた**本
	MCObjectHandle hTagD = nil;
	MCObjectHandle hTextD = nil;
	if (hMemberD != nil)
	{
		hTagD = nil;
		hTextD = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberD, hLayerPlan, 15000.0,
									"D", hTagD);
		if (hTextD != nil)
		{
			for (size_t i = 0; i < sizeof(kHeightCases) / sizeof(kHeightCases[0]); ++i)
			{
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagD, hTextD,
										 std::string("D(パラメータで書いた)-") +
											 kHeightCases[i].label,
										 kHeightCases[i].formula, kHeightCases[i].worksheetMode);
			}
		}
	}

	// ---- 3-移動. 経路 3: **動かしてから読み直す**。
	//
	//	ここが「オブジェクトと連動しているか」の直接の試験である。利用者は
	//	「取り込み後に高さを変えることがある」と言っているので、**動かした後に
	//	式の出力が変わるか**が要望そのものに当たる。
	probe.log("--- 部材を +1000 動かして読み直す（連動の試験）---");
	if (hMemberC != nil)
	{
		gSDK->MoveObject3D(hMemberC, 0.0, 0.0, 1000.0);
		gSDK->ResetObject(hMemberC);
		ProbeI187_DumpMember(probe, "C 移動後", hMemberC, true);
		ProbeI187_DumpPath(probe, "C 移動後", hMemberC);
		if (hTextC != nil)
		{
			for (size_t i = 0; i < sizeof(kHeightCases) / sizeof(kHeightCases[0]); ++i)
			{
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagC, hTextC,
										 std::string("C 移動後-") + kHeightCases[i].label,
										 kHeightCases[i].formula, kHeightCases[i].worksheetMode);
			}
		}
	}
	if (hMemberD != nil)
	{
		gSDK->MoveObject3D(hMemberD, 0.0, 0.0, 1000.0);
		gSDK->ResetObject(hMemberD);
		ProbeI187_DumpMember(probe, "D 移動後", hMemberD, true);
		if (hTextD != nil)
		{
			for (size_t i = 0; i < sizeof(kHeightCases) / sizeof(kHeightCases[0]); ++i)
			{
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagD, hTextD,
										 std::string("D 移動後-") + kHeightCases[i].label,
										 kHeightCases[i].formula, kHeightCases[i].worksheetMode);
			}
		}
	}

	// ---- 3-B. `#thsep#` は 3 桁のコンマを出せるか。
	//
	//	2 巡目は `MajorDepth`=600（3 桁）と −2699 でしか試せず決着しなかった。
	//	**4 桁以上の値をフィールドの直後に置いて**引き直す。`#sign#` が `+600` を
	//	出したので**修飾子の仕組み自体は効いている**ことは分かっている。
	if (hMemberB != nil)
	{
		const VWFC::VWObjects::VWParametricObj objB(hMemberB);
		VWFC::VWObjects::VWRecordFormatObj formatB = objB.GetRecordFormat();
		formatB.SetParamReal("MajorDepth", 12345.0);
		gSDK->ResetObject(hMemberB);
		probe.log("部材 B: MajorDepth を 12345 にした → 読み戻し='" +
				  ProbeI187_FromTX(objB.GetParamValue("MajorDepth")) + "'");

		MCObjectHandle hTagB = nil;
		MCObjectHandle hTextB = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberB,
												   hLayerPlan, 18000.0, "B", hTagB);
		if (hTextB != nil)
		{
			static const ProbeI187_Case kFormatCases[] = {
				{"B-4桁-無印", "#StructuralMember#.#MajorDepth#", false},
				{"B-4桁-thsep", "#StructuralMember#.#MajorDepth##thsep#", false},
				{"B-4桁-未知の修飾子", "#StructuralMember#.#MajorDepth##t187nosuch#", false},
				{"B-4桁-sign", "#StructuralMember#.#MajorDepth##sign#", false},
				{"B-4桁-thsep+sign", "#StructuralMember#.#MajorDepth##thsep#sign#", false},
				{"B-4桁-sign+thsep", "#StructuralMember#.#MajorDepth##sign#thsep#", false}};
			for (size_t i = 0; i < sizeof(kFormatCases) / sizeof(kFormatCases[0]); ++i)
			{
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagB, hTextB,
										 kFormatCases[i].label, kFormatCases[i].formula,
										 kFormatCases[i].worksheetMode);
			}
		}
	}

	probe.log("=== 終わり ===");
}
