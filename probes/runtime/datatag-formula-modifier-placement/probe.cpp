//
//	probes/runtime/datatag-formula-modifier-placement/probe.cpp
//
//	[issue #190] データタグのタグフィールドの式で、数値の書式修飾子（`#sign#` /
//	`#thsep#`）を**フィールドの直後ではなく「演算の結果」の後ろ**に置いたとき効くかを
//	実測する。
//
//	  #StructuralMember#.#MajorDepth##sign#   ← フィールドの直後。**効く**（#187 で実測）
//	  #IPZL#-872##sign#                       ← 演算の結果の後ろ。**これが未確認**
//
//	**目視は要らない作りにしてある。** 式の評価結果は `GetDataTagExtractedData`
//	（タグが抽出した「ラベル → 値」の対）で文字列として読み戻す（#187 で確立した手）。
//
//	**判定できるのは「式の結果が正のとき」だけ。** `#sign#` の働きは「正の値に `+` を
//	付ける」なので、結果が負なら付いていても付いていなくても `-` しか出ず、区別が付かない
//	（#187 の 1 巡目はここで読み違えた）。そこで舞台は
//
//	  * `MajorDepth` = **1234**（正） … 演算しても正のまま
//	  * `DialogStartElevation` = **−872**（負） … 演算で**正に変える**（`*-1` / `+2000`）
//
//	の 2 本を用意し、**結果が正になる式にだけ修飾子を付けて引く**。
//
//	**「知らない修飾子」を対照に置く。** #187 で「知らない修飾子は黙って捨てられる」
//	（`##t187nosuch#` → 値だけ）・「`#` が 1 つだと文字がそのまま出る」（`#t187nosuch#`
//	→ `600t187nosuch`）と分かっているので、式の後ろでも同じ対照を引けば
//
//	  * 値だけが出る            → **その位置は修飾子として読まれている**（捨てられた）
//	  * `…t190nosuch` と出る     → **修飾子として読まれていない**（文字になった）
//
//	を機械的に切り分けられる。`#sign#` が効かない場合に「位置が修飾子でないのか、
//	`#sign#` にその働きが無いのか」を言い分けるための対照である。
//
//	**代わりの手も同じ走行で測る。** 修飾子が式の後ろで効かないなら、条件式
//	`値@条件:代替`（#187 で実測済み）で `+` を足せるかが次の手になるので、
//	`"+"@<式>>0:""` の形を併せて引く。
//

#include "Probe.h"

#include <cstdio>
#include <string>

// 式を短く書くための綴り。`#レコード#.#フィールド#` の形は #187 で実測済み。
// **文字列リテラルの連結で組む**ので、`#` の数（二重か単一か）がソースで数えられる。
#define VWP190_MAJOR "#StructuralMember#.#MajorDepth#"
#define VWP190_START "#StructuralMember#.#DialogStartElevation#"

namespace
{
	// -----------------------------------------------------------------------
	// ログ用の小物。**短い名前・ありふれた名前は使わない**（SDK と OS のヘッダが
	// グローバルへ撒いているため。probes/runtime/README.md）。

	std::string ProbeI190_FromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string ProbeI190_Num(double value)
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

	std::string ProbeI190_OneLine(const std::string& value)
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
	// #187 の作りそのまま——**中身を入れてから渡し、渡した後に取り直す**
	// （Findings「データタグ」。VW が群を複製していることがある）。
	MCObjectHandle ProbeI190_BuildTag(vwprobe::Report& probe,
									  VectorWorks::Extension::IDataTagSupport* tagSupport,
									  VectorWorks::Extension::IDataTagTextLinkSupport* linkSupport,
									  MCObjectHandle hMember, MCObjectHandle hLayer, double x,
									  MCObjectHandle& outTag)
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
		MCObjectHandle hText = gSDK->CreateTextBlock("T190", WorldPt(0.0, 0.0), false, 0.0);
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

	// -----------------------------------------------------------------------
	// タグの 1 本のテキストへ式を持たせ、更新して結果を読み戻す。
	void ProbeI190_Eval(vwprobe::Report& probe, VectorWorks::Extension::IDataTagSupport* tagSupport,
						VectorWorks::Extension::IDataTagTextLinkSupport* linkSupport,
						MCObjectHandle hTag, MCObjectHandle hText, const std::string& label,
						const std::string& formula)
	{
		const TXString formulaTX(formula.c_str());
		linkSupport->SetIsLinked(hText, true);
		linkSupport->SetFormula(hText, formulaTX, false);
		tagSupport->UpdateUserDefinedTextsUIDs(hTag);
		tagSupport->UpdateDataTag(hTag);
		gSDK->ResetObject(hTag);

		std::string line = "[式] " + label + " | in='" + ProbeI190_OneLine(formula) + "'";

		const std::string readBack = ProbeI190_FromTX(linkSupport->GetFormula(hText));
		if (readBack != formula)
			line += " | 読み戻し='" + ProbeI190_OneLine(readBack) + "'";

		VectorWorks::Extension::TXStringSTLPairArray extracted;
		tagSupport->GetDataTagExtractedData(hTag, extracted);
		line += " | out=";
		if (extracted.empty())
			line += "(0 件)";
		for (size_t i = 0; i < extracted.size(); ++i)
			line += "'" + ProbeI190_OneLine(ProbeI190_FromTX(extracted[i].second)) + "'";
		probe.log(line);
	}

	// **描ける**構造材を作る（#187 の 4 巡目で確立した手順。`MemberType` を書き忘れると
	// 太さが既定のまま残り、`Minor*` を放っておくと断面が I 形になって、せいが 200 未満だと
	// 3D 実体が作られない）。ここでは値を読めれば足りるが、**描けない部材だと
	// パラメータの読み戻しまで当てにならない**ので同じ手順を踏む。
	MCObjectHandle ProbeI190_MakeMember(vwprobe::Report& probe, MCObjectHandle hLayer, double depth,
										double dialogStartElevation)
	{
		MCObjectHandle hPath = gSDK->Create3DPoly();
		if (hPath != nil)
		{
			gSDK->Add3DVertex(hPath, WorldPt3(0.0, 0.0, 0.0));
			gSDK->Add3DVertex(hPath, WorldPt3(4000.0, 0.0, 0.0));
		}
		MCObjectHandle hMember = gSDK->CreateCustomObjectPath("StructuralMember", hPath, nil, true);
		if (hMember == nil)
		{
			probe.fail("構造材を作れなかった（CreateCustomObjectPath が nil）");
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
		// 負の数値フィールドを 1 つ用意する。#187 の 4 巡目で「パラメータへ書いた値は
		// 式から読める」と分かっているのがこのフィールド。
		pio.SetParamReal("DialogStartElevation", dialogStartElevation);
		gSDK->ResetObject(hMember);
		probe.log("部材: MajorDepth を " + ProbeI190_Num(depth) + " / DialogStartElevation を " +
				  ProbeI190_Num(dialogStartElevation) + " にした → 読み戻し MajorDepth='" +
				  ProbeI190_FromTX(pio.GetParamValue("MajorDepth")) + "' DialogStartElevation='" +
				  ProbeI190_FromTX(pio.GetParamValue("DialogStartElevation")) + "' MemberType='" +
				  ProbeI190_FromTX(pio.GetParamValue("MemberType")) + "'");
		return hMember;
	}

	struct ProbeI190_Case
	{
		const char* label;
		const char* formula;
	};

	// =======================================================================
	// 引く式。**どの行も「無印の基準」と対にしてある**ので、出た値だけで
	// 修飾子が効いたかどうかが読める。
	// =======================================================================
	const ProbeI190_Case kProbeI190Cases[] = {
		// --- A: フィールドの直後（#187 の再現。ここが効かなければ舞台が違う）
		{"A1-直後-無印(基準)", VWP190_MAJOR},
		{"A2-直後-二重#sign", VWP190_MAJOR "#sign#"},
		{"A3-直後-単一#sign", "#StructuralMember#.#MajorDepth#sign#"},
		{"A4-直後-二重#未知(対照)", VWP190_MAJOR "#t190nosuch#"},

		// --- B: 正の値のまま演算した結果の後ろ（本題。1234*1 と 1234+1000 の 2 通り）
		{"B1-式*1-無印(基準)", VWP190_MAJOR "*1"},
		{"B2-式*1-単一#sign", VWP190_MAJOR "*1#sign#"},
		{"B3-式*1-二重#sign", VWP190_MAJOR "*1##sign#"},
		{"B4-式*1-単一#未知(対照)", VWP190_MAJOR "*1#t190nosuch#"},
		{"B5-式*1-二重#未知(対照)", VWP190_MAJOR "*1##t190nosuch#"},
		{"B6-式+1000-無印(基準)", VWP190_MAJOR "+1000"},
		{"B7-式+1000-単一#sign", VWP190_MAJOR "+1000#sign#"},
		{"B8-式+1000-二重#sign", VWP190_MAJOR "+1000##sign#"},
		{"B9-括弧-無印(基準)", "(" VWP190_MAJOR "*1)"},
		{"B10-括弧-単一#sign", "(" VWP190_MAJOR "*1)#sign#"},
		{"B11-括弧-二重#sign", "(" VWP190_MAJOR "*1)##sign#"},

		// --- C: **負のフィールドを演算で正に変える**（issue の用途そのもの）
		{"C1-負の素(基準)", VWP190_START},
		{"C2-負*-1-無印(基準)", VWP190_START "*-1"},
		{"C3-負*-1-単一#sign", VWP190_START "*-1#sign#"},
		{"C4-負*-1-二重#sign", VWP190_START "*-1##sign#"},
		{"C5-負*-1-単一#未知(対照)", VWP190_START "*-1#t190nosuch#"},
		{"C6-負*-1-二重#未知(対照)", VWP190_START "*-1##t190nosuch#"},
		{"C7-負+2000-無印(基準)", VWP190_START "+2000"},
		{"C8-負+2000-単一#sign", VWP190_START "+2000#sign#"},
		{"C9-負+2000-二重#sign", VWP190_START "+2000##sign#"},

		// --- D: issue 本文の綴りそのまま（`#IPZL#` は「レコードのフィールドでない綴り」で
		//	  唯一値を返すもの。#187）。`-872` は負になるので、正になる `+2000` も対で引く。
		{"D1-IPZL-素(基準)", "#IPZL#"},
		{"D2-IPZL-872-無印(基準)", "#IPZL#-872"},
		{"D3-IPZL-872-二重#sign(issue の綴り)", "#IPZL#-872##sign#"},
		{"D4-IPZL-872-単一#sign", "#IPZL#-872#sign#"},
		{"D5-IPZL+2000-無印(基準)", "#IPZL#+2000"},
		{"D6-IPZL+2000-単一#sign", "#IPZL#+2000#sign#"},
		{"D7-IPZL+2000-二重#sign", "#IPZL#+2000##sign#"},

		// --- E: 修飾子を**式の途中**に置く（フィールドの直後に置いて、その後ろで演算する）
		{"E1-直後#sign-の後に*1", VWP190_MAJOR "#sign#*1"},
		{"E2-直後#sign-の後に+1000", VWP190_MAJOR "#sign#+1000"},

		// --- F: フィールドを含まない定数式（#187 で `872+40` → `912` と実測）
		{"F1-定数-無印(基準)", "1234"},
		{"F2-定数-単一#sign", "1234#sign#"},
		{"F3-定数式-無印(基準)", "1200+34"},
		{"F4-定数式-単一#sign", "1200+34#sign#"},
		{"F5-定数式-二重#sign", "1200+34##sign#"},

		// --- G: 文字列と並べたとき（修飾子が「式全体の末尾」で効くのかを見る）
		{"G1-文字列連結-無印(基準)", "\" (\"" VWP190_MAJOR "\")\""},
		{"G2-文字列の後に二重#sign", "\" (\"" VWP190_MAJOR "\")\"#sign#"},
		{"G3-直後#sign-の後に文字列", VWP190_MAJOR "#sign#\" mm\""},

		// --- H: 代わりの手——条件式 `値@条件:代替` で `+` を足せるか
		{"H1-条件で+だけ出す", "\"+\"@" VWP190_MAJOR ">0:\"\""},
		{"H2-条件で+を足す(正)", "\"+\"@" VWP190_MAJOR ">0:\"\"" VWP190_MAJOR},
		{"H3-条件で+を足す(負→付かないはず)", "\"+\"@" VWP190_START ">0:\"\"" VWP190_START},
		{"H4-条件に式を書く(負+2000)", "\"+\"@" VWP190_START "+2000>0:\"\"" VWP190_START "+2000"},
		{"H5-条件に括弧付きの式", "\"+\"@(" VWP190_START "+2000)>0:\"\"(" VWP190_START "+2000)"},
		{"H6-条件>=", "\"+\"@" VWP190_MAJOR ">=0:\"\""},
		{"H7-条件<0で-を出す(対照)", "\"-\"@" VWP190_START "<0:\"\""}};

#undef VWP190_MAJOR
#undef VWP190_START
} // namespace

VW_PROBE("datatag-formula-modifier-placement", "書式修飾子は式の後ろでも効くか",
		 "#sign# をフィールド直後と演算結果の後ろで引き比べ、条件式での代替も測る")
{
	using namespace VectorWorks;
	using namespace VectorWorks::Extension;

	// **何かを作る前に 1 度**（既定は kCustomObjectPrefAlways で、最初の 1 個で
	// 「オブジェクトの設定」ダイアログが出て止まる。probes/runtime/README.md）。
	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
	gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);

	// =======================================================================
	// 1. 舞台。**正のフィールドと負のフィールドを 1 本の部材に同居させる。**
	//	ストーリもレベルも要らない（#187 は「式から高さが読めるか」を見るために
	//	組んだが、ここで問うのは**式の文法**なので、値が互いに違えば足りる）。
	//	レイヤの高さだけは `#IPZL#` を 0 でない値にするために与える。
	// =======================================================================
	probe.log("=== 1. 舞台を作る ===");

	MCObjectHandle hLayer = gSDK->CreateLayer("T190-L", kLayerDesign);
	if (hLayer != nil)
	{
		VWFC::VWObjects::VWLayerObj layerObj(hLayer);
		layerObj.SetElevation(872.0);
		probe.log("レイヤ T190-L: SetElevation(872) → 読み戻し=" +
				  ProbeI190_Num(layerObj.GetElevation()));
	}
	else
	{
		// 作れなくても続ける——**`#IPZL#` が 0 になるだけ**で、本題（B・C 群）は
		// レイヤの高さに依らない。
		probe.log("レイヤ T190-L: **作れなかった**（#IPZL# は 0 になる見込み）");
	}

	MCObjectHandle hMember = ProbeI190_MakeMember(probe, hLayer, 1234.0, -872.0);
	if (hMember == nil)
		return;

	// =======================================================================
	// 2. タグを組んで、式を 1 本ずつ入れ替えながら読み戻す。
	// =======================================================================
	probe.log("=== 2. タグフィールドの式を引く ===");

	IDataTagSupportPtr tagSupport(IID_DataTagSupport);
	IDataTagTextLinkSupportPtr linkSupport(IID_DataTagTextLinkSupport);
	if (!tagSupport || !linkSupport)
	{
		probe.fail("IDataTagSupport / IDataTagTextLinkSupport を取れなかった");
		return;
	}

	MCObjectHandle hTag = nil;
	MCObjectHandle hText =
		ProbeI190_BuildTag(probe, tagSupport, linkSupport, hMember, hLayer, 6000.0, hTag);
	if (hText == nil)
		return;

	probe.log("[式] ラベル | 入れた式 | 読み戻し（入れたものと違うときだけ）| 抽出値");
	for (size_t i = 0; i < sizeof(kProbeI190Cases) / sizeof(kProbeI190Cases[0]); ++i)
		ProbeI190_Eval(probe, tagSupport, linkSupport, hTag, hText, kProbeI190Cases[i].label,
					   kProbeI190Cases[i].formula);

	probe.log("=== 終わり ===");
}
