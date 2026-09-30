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
//	【4 巡目】文法は 1〜3 巡目で確定した。残っているのは issue の本題ただ 1 つ
//	——**「その階の FL から測った高さ」の数値を式から出せるか**である。
//	3 巡目で分かったこと（と、**こちらの書き方の誤り 2 つ**）:
//
//	  * **`#IPZL#` は生きている。** `MoveObject3D` で +1000 動かしたら `-2699` → `-1699`
//	    に変わった。**オブジェクトと連動する値は在る**——足りないのは「レイヤと FL の差」
//	    という定数だけである。
//	  * `#DialogStartElevationReference#` は**バウンド先のレベル名**を返し、他階へ
//	    繋いだ材では `T187-FL [上階]` まで出る。**移動しても保たれる。**
//	  * `#DialogStartElevation#` は 0 のまま（バウンドの −872 が出てこない）。
//	  * **誤り 1: パラメータを `VWRecordFormatObj` へ書いていた。** それは
//	    **書式の既定値**を変える口で、既にある個体には効かない
//	    （Findings「取り込みでどう書くか」）。`MajorDepth` に 12345 を書いたのに
//	    読み戻しが `600` だったのがその証拠。**個体へ書くのは
//	    `VWParametricObj::SetParamReal` / `SetParamValue`。**
//	  * **誤り 2: 部材が何も描いていなかった**（`GetObjectBounds` が空。`MemberType` と
//	    `Minor*` を書いていないため。Findings「取り込みでどう書くか」）。ジオメトリが
//	    無い本の読み値は当てにできない。
//
//	そこで 4 巡目は**Findings の手順どおりに描ける部材を作ってから**、
//
//	  経路 1: `SetObjectStoryBound` で書いた本を読む
//	  経路 2: **個体のパラメータへ**「始端高さ基準 / 始端高さオフセット」を書いて読む
//	          （選択肢の綴りが分からないので `GetParamChoices` で一覧を出してから書く）
//	  経路 3: 動かしてから読み直す（連動の試験）
//
//	を引き直す。あわせて `#thsep#` を**4 桁の値**で引き直す（3 巡目は誤り 1 のせいで
//	値が 600 のままだった）。

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

	// ポップアップの選択肢を出す（`PopupGetChoicesCount` は 0 を返すので使えない。
	//	Findings「選択肢は GetParamChoices でしか採れない」）。
	void ProbeI187_DumpChoices(vwprobe::Report& probe, MCObjectHandle hMember, const char* param)
	{
		VWFC::Tools::CObjectParamProvider provider(hMember);
		TXStringSTLArray choices;
		const bool got = provider.GetParamChoices(TXString(param), choices);
		std::string line = std::string("選択肢 ") + param + " (got=" + (got ? "true" : "false") +
						   " 件数=" + ProbeI187_Int(static_cast<long long>(choices.size())) + "):";
		for (size_t i = 0; i < choices.size(); ++i)
			line += " [" + ProbeI187_Int(static_cast<long long>(i)) + "]'" +
					ProbeI187_OneLine(ProbeI187_FromTX(choices[i])) + "'";
		probe.log(line);
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

	// ---- **描ける**部材を 3 本作る（3 巡目はここを省いて空の本を測っていた）
	//	E: 伏図レイヤ＋他階の FL バウンド（現場の形）
	//	F: 伏図レイヤ＋**個体のパラメータへ**始端高さ基準／オフセットを書く
	//	G: 書式修飾子の試験用（せい 1234 ＝ 4 桁）
	MCObjectHandle hMemberE = ProbeI187_MakeDrawnMember(probe, "E", hLayerPlan, 0.0, 600.0);
	MCObjectHandle hMemberF = ProbeI187_MakeDrawnMember(probe, "F", hLayerPlan, 1500.0, 600.0);
	MCObjectHandle hMemberG = ProbeI187_MakeDrawnMember(probe, "G", hLayerPlan, 3000.0, 1234.0);

	if (hMemberE != nil)
	{
		SStoryObjectData bound;
		bound.fBound = eStoryObjectBound_Story;
		bound.fBoundStory = 1; // 伏図の階から 1 つ上＝T187-2F
		bound.fLayerLevelType = levelTypeFL;
		bound.fOffset = -872.0;
		gSDK->SetObjectStoryBound(hMemberE, 0, bound);
		bound.fOffset = -302.0;
		gSDK->SetObjectStoryBound(hMemberE, 1, bound);
		gSDK->ResetObject(hMemberE);
		ProbeI187_DumpMember(probe, "E(バウンドで書いた)", hMemberE, true);
		ProbeI187_DumpPath(probe, "E", hMemberE);
	}

	// =======================================================================
	// 2. 経路 2: **個体のパラメータへ**「始端高さ基準 / 始端高さオフセット」を書く。
	//	綴りが分からないので、まず選択肢の一覧を出す。
	// =======================================================================
	probe.log("=== 2. 個体のパラメータへ始端高さ基準／オフセットを書く ===");

	if (hMemberF != nil)
	{
		ProbeI187_DumpChoices(probe, hMemberF, "DialogStartElevationReference");
		ProbeI187_DumpChoices(probe, hMemberF, "DialogEndElevationReference");

		VWFC::VWObjects::VWParametricObj pioF(hMemberF);
		// 選択肢の綴りが分からないので**3 通り**書いてみて、読み戻しで効いたものを見る。
		static const char* const kRefCandidates[] = {"T187-FL", "2", "1"};
		for (size_t i = 0; i < sizeof(kRefCandidates) / sizeof(kRefCandidates[0]); ++i)
		{
			pioF.SetParamValue("DialogStartElevationReference", TXString(kRefCandidates[i]));
			pioF.SetParamReal("DialogStartElevation", -872.0);
			gSDK->ResetObject(hMemberF);
			probe.log(
				std::string("部材 F: 基準へ '") + kRefCandidates[i] + "' を書いた → 基準='" +
				ProbeI187_FromTX(pioF.GetParamValue("DialogStartElevationReference")) +
				"' オフセット='" + ProbeI187_FromTX(pioF.GetParamValue("DialogStartElevation")) +
				"' バウンド件数=" +
				ProbeI187_Int(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(hMemberF))));
		}
		ProbeI187_DumpMember(probe, "F(パラメータで書いた)", hMemberF, false);
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

	static const ProbeI187_Case kHeightCases[] = {
		{"IPZL", "#IPZL#", false},
		{"StartElevation", "#StructuralMember#.#StartElevation#", false},
		{"EndElevation", "#StructuralMember#.#EndElevation#", false},
		{"DialogStartElev", "#StructuralMember#.#DialogStartElevation#", false},
		{"DialogEndElev", "#StructuralMember#.#DialogEndElevation#", false},
		{"DialogStartRef", "#StructuralMember#.#DialogStartElevationReference#", false},
		{"連動の候補", "\" (2FL \"#StructuralMember#.#DialogStartElevation#\")\"", false}};

	MCObjectHandle hTagE = nil;
	MCObjectHandle hTextE = nil;
	if (hMemberE != nil)
	{
		hTextE = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberE, hLayerPlan, 6000.0,
									"E", hTagE);
		if (hTextE != nil)
			for (size_t i = 0; i < sizeof(kHeightCases) / sizeof(kHeightCases[0]); ++i)
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagE, hTextE,
										 std::string("E(バウンド)-") + kHeightCases[i].label,
										 kHeightCases[i].formula, kHeightCases[i].worksheetMode);
	}

	MCObjectHandle hTagF = nil;
	MCObjectHandle hTextF = nil;
	if (hMemberF != nil)
	{
		hTextF = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberF, hLayerPlan, 9000.0,
									"F", hTagF);
		if (hTextF != nil)
			for (size_t i = 0; i < sizeof(kHeightCases) / sizeof(kHeightCases[0]); ++i)
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagF, hTextF,
										 std::string("F(パラメータ)-") + kHeightCases[i].label,
										 kHeightCases[i].formula, kHeightCases[i].worksheetMode);
	}

	// ---- 連動の試験: 動かしてから読み直す。
	probe.log("--- 部材を +1000 動かして読み直す（連動の試験）---");
	if (hMemberE != nil && hTextE != nil)
	{
		gSDK->MoveObject3D(hMemberE, 0.0, 0.0, 1000.0);
		gSDK->ResetObject(hMemberE);
		ProbeI187_DumpMember(probe, "E 移動後", hMemberE, true);
		ProbeI187_DumpPath(probe, "E 移動後", hMemberE);
		for (size_t i = 0; i < sizeof(kHeightCases) / sizeof(kHeightCases[0]); ++i)
			ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagE, hTextE,
									 std::string("E 移動後-") + kHeightCases[i].label,
									 kHeightCases[i].formula, kHeightCases[i].worksheetMode);
	}

	// ---- `#thsep#` を **4 桁の値**で引き直す（3 巡目は値が 600 のままだった）。
	if (hMemberG != nil)
	{
		MCObjectHandle hTagG = nil;
		MCObjectHandle hTextG = ProbeI187_BuildTag(probe, tagSupport, linkSupport, hMemberG,
												   hLayerPlan, 12000.0, "G", hTagG);
		if (hTextG != nil)
		{
			static const ProbeI187_Case kFormatCases[] = {
				{"G-4桁-無印", "#StructuralMember#.#MajorDepth#", false},
				{"G-4桁-thsep", "#StructuralMember#.#MajorDepth##thsep#", false},
				{"G-4桁-未知の修飾子", "#StructuralMember#.#MajorDepth##t187nosuch#", false},
				{"G-4桁-sign", "#StructuralMember#.#MajorDepth##sign#", false},
				{"G-4桁-thsep+sign", "#StructuralMember#.#MajorDepth##thsep#sign#", false}};
			for (size_t i = 0; i < sizeof(kFormatCases) / sizeof(kFormatCases[0]); ++i)
				ProbeI187_EvalTagFormula(probe, tagSupport, linkSupport, hTagG, hTextG,
										 kFormatCases[i].label, kFormatCases[i].formula,
										 kFormatCases[i].worksheetMode);
		}
	}

	probe.log("=== 終わり ===");
}
