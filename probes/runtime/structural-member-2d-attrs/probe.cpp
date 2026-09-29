//
//	probes/runtime/structural-member-2d-attrs/probe.cpp
//
//	[issue #158] 構造材（StructuralMember）の 2D 属性パラメータ（索引 48〜152）と
//	3D 属性（161〜168）の実体を採る。既存の Findings「構造材のパラメータ表」は、
//	ローカライズ名が `__NNA_DO_NOT_CHANGE` の索引をまとめて省いているので、
//	**その中身（universal 名・欄型・値・ポップアップの選択肢）が一行も残っていない**。
//
//	採るもの:
//	  G1  索引 48〜180 の全件（universal 名・ローカライズ名・資源から引いた名前・
//	      欄型（EFieldStyle）・値・ポップアップの選択肢をキーと表示の対で）
//	  G2  既定のまま作った 1 本の子（＝描画）の素性。以後の比較の基準
//	  G3  「構造材＝クラススタイル・被覆と中心線は非表示・端部は両端」を**名前と
//	      選択肢から当てて**書き、ResetObject を通してから読み戻す（残るか）＋子を出す
//	      （描画が変わるか）。当てた根拠も 1 行ずつログへ出す
//	  G4  AttributesMode / AttributesMode3D の 4 値を順に当てて、子の by-class の旗が
//	      どう動くかを見る（＝4 つの選択肢が何を切り替えるか）
//	  G5  48〜152 を**全件「今と違う値」へ倒して** ResetObject（名前の当てが外れても
//	      「書いた値は残るか・描画は変わるか」だけは必ず答えが出るように）
//
//	G5 は最も壊しやすいので最後に置く（落ちても G1〜G4 のログは残る）。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace
{
	const char* const kStructMemberProbePio = "StructuralMember";

	// 既存の Findings が省いている帯。2D 属性は 48〜152、3D 属性は 161〜168。
	const size_t kStructMemberProbeDumpFirst = 48;
	const size_t kStructMemberProbeDumpLast = 180;
	const size_t kStructMember2DAttrFirst = 48;
	const size_t kStructMember2DAttrLast = 152;

	std::string ProbeText(const TXString& src)
	{
		const char* utf8 = static_cast<const char*>(src);
		return utf8 ? std::string(utf8) : std::string();
	}

	std::string ProbeWhole(long long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%lld", value);
		return std::string(buffer);
	}

	std::string ProbeDecimal(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.3f", value);
		return std::string(buffer);
	}

	// EFieldStyle（Kernel/API/MiniCadCallBacks.h）の名前。数値だけでは読めないので添える。
	const char* ProbeFieldStyleName(int style)
	{
		switch (style)
		{
		case kFieldLongInt:
			return "LongInt";
		case kFieldBoolean:
			return "Boolean";
		case kFieldReal:
			return "Real";
		case kFieldText:
			return "Text";
		case kFieldCoordDisp:
			return "CoordDisp";
		case kFieldPopUp:
			return "PopUp";
		case kFieldRadio:
			return "Radio";
		case kFieldCoordLocX:
			return "CoordLocX";
		case kFieldCoordLocY:
			return "CoordLocY";
		case kFieldStaticText:
			return "StaticText";
		case kFieldControlPoint:
			return "ControlPoint";
		case kFieldDimStdPopUp:
			return "DimStdPopUp";
		case kFieldPrecisionPopUp:
			return "PrecisionPopUp";
		case kFieldClassesPopup:
			return "ClassesPopup";
		case kFieldLayersPopup:
			return "LayersPopup";
		case kFieldAngle:
			return "Angle";
		case kFieldArea:
			return "Area";
		case kFieldVolume:
			return "Volume";
		case kFieldClass:
			return "Class";
		case kFieldBuildingMaterial:
			return "BuildingMaterial";
		case kFieldFill:
			return "Fill";
		case kFieldPenStyle:
			return "PenStyle";
		case kFieldPenWeight:
			return "PenWeight";
		case kFieldColor:
			return "Color";
		case kFieldTexture:
			return "Texture";
		case kFieldSymDef:
			return "SymDef";
		case kFieldDimUnitPopUp:
			return "DimUnitPopUp";
		default:
			return "（不明）";
		}
	}

	bool ProbeHasText(const std::string& haystack, const char* needle)
	{
		return haystack.find(needle) != std::string::npos;
	}

	bool ProbeEndsWith(const std::string& haystack, const char* tail)
	{
		const std::string suffix(tail);
		return haystack.size() >= suffix.size() &&
			   haystack.compare(haystack.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	bool ProbeStartsWith(const std::string& haystack, const char* head)
	{
		const std::string prefix(head);
		return haystack.size() >= prefix.size() && haystack.compare(0, prefix.size(), prefix) == 0;
	}

	// パラメータ 1 件の素性（表を 1 度だけ舐めて溜める）。
	struct ProbeParamRow
	{
		size_t index = 0;
		std::string universalName;
		std::string localizedName;
		std::string resourceName; // GetLocalizedPluginParameter（アプリの資源）
		int fieldStyle = 0;
		std::string value;
		std::vector<std::pair<std::string, std::string>> choices; // キー → 表示
	};

	void ProbeCollectRows(const VWParametricObj& pio, std::vector<ProbeParamRow>& outRows)
	{
		const size_t count = pio.GetParamsCount();
		outRows.clear();
		outRows.reserve(count);
		for (size_t index = 0; index < count; ++index)
		{
			ProbeParamRow row;
			row.index = index;
			row.universalName = ProbeText(pio.GetParamName(index));
			row.localizedName = ProbeText(pio.GetParamLocalizedName(index));
			TXString fromResource;
			if (gSDK->GetLocalizedPluginParameter(
					kStructMemberProbePio, TXString(row.universalName.c_str()), fromResource))
			{
				row.resourceName = ProbeText(fromResource);
			}
			row.fieldStyle = static_cast<int>(pio.GetParamStyle(index));
			row.value = ProbeText(pio.GetParamValue(index));
			const size_t choiceCount = pio.PopupGetChoicesCount(index);
			for (size_t choice = 0; choice < choiceCount; ++choice)
			{
				TXString key;
				TXString display;
				pio.PopupGetChoice(index, choice, key, display);
				row.choices.push_back(std::make_pair(ProbeText(key), ProbeText(display)));
			}
			outRows.push_back(row);
		}
	}

	std::string ProbeRowLine(const ProbeParamRow& row)
	{
		std::string line = ProbeWhole(static_cast<long long>(row.index)) + " " + row.universalName +
						   " 欄型=" + ProbeWhole(row.fieldStyle) + "(" +
						   ProbeFieldStyleName(row.fieldStyle) + ") 値=[" + row.value + "]";
		line += " loc=[" + row.localizedName + "]";
		if (row.resourceName != row.localizedName)
			line += " 資源=[" + row.resourceName + "]";
		if (!row.choices.empty())
		{
			line += " 選択肢" + ProbeWhole(static_cast<long long>(row.choices.size())) + "=";
			for (size_t at = 0; at < row.choices.size(); ++at)
			{
				if (at != 0)
					line += " / ";
				line += "[" + row.choices[at].first + "]=[" + row.choices[at].second + "]";
			}
		}
		return line;
	}

	// PIO の子（＝描かれた実体）1 つの素性。by-class の 5 旗まで読むのは、
	// 「クラススタイルにする」書き込みが実際に描画へ出たかを目で見ずに判定するため。
	std::string ProbeChildLine(MCObjectHandle child)
	{
		std::string line = "型=" + ProbeWhole(gSDK->GetObjectTypeN(child));
		TXString className;
		gSDK->ClassIDToName(gSDK->GetObjectClass(child), className);
		line += " クラス=[" + ProbeText(className) + "]";
		line += std::string(" byClass(pPat=") + (gSDK->GetPPatByClass(child) ? "y" : "n");
		line += std::string(" fPat=") + (gSDK->GetFPatByClass(child) ? "y" : "n");
		line += std::string(" lw=") + (gSDK->GetLWByClass(child) ? "y" : "n");
		line += std::string(" pCol=") + (gSDK->GetPColorsByClass(child) ? "y" : "n");
		line += std::string(" fCol=") + (gSDK->GetFColorsByClass(child) ? "y" : "n") + ")";
		WorldRect bounds;
		if (gSDK->GetObjectBounds(child, bounds))
		{
			line += " 外接=(" + ProbeDecimal(bounds.left) + "," + ProbeDecimal(bounds.top) + "," +
					ProbeDecimal(bounds.right) + "," + ProbeDecimal(bounds.bottom) + ")";
		}
		return line;
	}

	void ProbeDumpChildren(vwprobe::Report& probe, const std::string& tag, MCObjectHandle pio)
	{
		WorldRect bounds;
		if (gSDK->GetObjectBounds(pio, bounds))
		{
			probe.log(tag + " 本体の外接=(" + ProbeDecimal(bounds.left) + "," +
					  ProbeDecimal(bounds.top) + "," + ProbeDecimal(bounds.right) + "," +
					  ProbeDecimal(bounds.bottom) + ")");
		}
		size_t count = 0;
		for (MCObjectHandle child = gSDK->FirstMemberObj(pio); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (count < 40)
				probe.log(tag + " 子[" + ProbeWhole(static_cast<long long>(count)) + "] " +
						  ProbeChildLine(child));
			++count;
		}
		probe.log(tag + " 子の総数=" + ProbeWhole(static_cast<long long>(count)) +
				  (count > 40 ? "（41 件目以降は省いた）" : ""));
	}

	// 水平の構造材 1 本。2 頂点の 2D ポリラインをパスにするのは、Findings
	//「構造材 PIO の高さと実体」の手順どおり（両端の Z を等しくする＝2D で表せる）。
	MCObjectHandle ProbeMakeMember(vwprobe::Report& probe, const std::string& tag, double originY)
	{
		VWPolygon2DObj path({VWPoint2D(0.0, originY), VWPoint2D(3000.0, originY)});
		MCObjectHandle pathHandle = path.GetThisObject();
		if (pathHandle == nil)
		{
			probe.fail(tag + " パス（VWPolygon2DObj）を作れなかった");
			return nil;
		}
		MCObjectHandle member =
			gSDK->CreateCustomObjectPath(kStructMemberProbePio, pathHandle, nil, false);
		if (member == nil)
		{
			probe.fail(tag + " CreateCustomObjectPath(StructuralMember) が nil を返した"
							 "（DefineCustomObject は先に呼んである）");
			return nil;
		}
		if (!gSDK->ResetObject(member))
			probe.log(tag + " 注意: 作った直後の ResetObject が false を返した");
		return member;
	}

	// 面の接尾辞（_Above / _At / _Below）。2D 属性はこの 3 面ぶんある。
	struct ProbeFaceName
	{
		const char* suffix;
		const char* japanese;
	};

	const ProbeFaceName kProbeFaces[] = {
		{"_Above", "上（切断面より上）"},
		{"_At", "至（切断面）"},
		{"_Below", "下（切断面より下）"},
	};

	// パーツの見分け。universal 名の頭で決める（長いものから順に見る）。
	const char* ProbePartOf(const std::string& universalName)
	{
		if (ProbeStartsWith(universalName, "CenterPointMarker") ||
			ProbeStartsWith(universalName, "CenterPoint") ||
			ProbeStartsWith(universalName, "CenterMark"))
			return "センターマーク";
		if (ProbeStartsWith(universalName, "CenterLine") ||
			ProbeStartsWith(universalName, "Center"))
			return "中心線";
		if (ProbeStartsWith(universalName, "Member"))
			return "構造材";
		if (ProbeStartsWith(universalName, "Cover"))
			return "被覆";
		if (ProbeStartsWith(universalName, "Caps") || ProbeStartsWith(universalName, "Cap"))
			return "端部";
		return "";
	}

	// 選択肢のうち、表示に needle を含む最初のもののキー。無ければ空。
	std::string ProbeChoiceKeyByDisplay(const ProbeParamRow& row, const char* needle)
	{
		for (size_t at = 0; at < row.choices.size(); ++at)
		{
			if (ProbeHasText(row.choices[at].second, needle))
				return row.choices[at].first;
		}
		return std::string();
	}

	struct ProbeWriteRecord
	{
		size_t index = 0;
		std::string universalName;
		std::string wrote;
		std::string before;
		std::string why;
	};

	void ProbeApplyValue(VWParametricObj& pio, const ProbeParamRow& row, const std::string& value,
						 const std::string& why, std::vector<ProbeWriteRecord>& outWrites)
	{
		ProbeWriteRecord record;
		record.index = row.index;
		record.universalName = row.universalName;
		record.before = row.value;
		record.wrote = value;
		record.why = why;
		if (row.fieldStyle == kFieldBoolean)
			pio.SetParamBool(row.index, value == "1" || value == "True" || value == "true");
		else
			pio.SetParamValue(row.index, TXString(value.c_str()));
		outWrites.push_back(record);
	}
} // namespace

VW_PROBE("structural-member-2d-attrs", "構造材の 2D / 3D 属性パラメータを採る",
		 "索引 48〜180 の全件（名前・欄型・値・選択肢）を出し、書いた値が ResetObject を"
		 "通って残るか・描画（子）に出るかと、AttributesMode の 4 値の意味を確かめる")
{
	// 新規の空図面で走らせる前提。構造材を初めて使う文書では、これを呼ばないと
	// 「オブジェクトの設定」ダイアログが出て止まる（Findings / probes/runtime/README.md）。
	probe.log("[G0] DefineCustomObject(StructuralMember, kCustomObjectPrefNever)");
	gSDK->DefineCustomObject(kStructMemberProbePio, kCustomObjectPrefNever);

	MCObjectHandle memberA = ProbeMakeMember(probe, "[G0] A", 0.0);
	if (memberA == nil)
		return;
	VWParametricObj pioA(memberA);
	std::vector<ProbeParamRow> rows;
	ProbeCollectRows(pioA, rows);
	probe.log("[G1] パラメータ総数=" + ProbeWhole(static_cast<long long>(rows.size())));

	// ---- G1: 表そのもの --------------------------------------------------
	probe.log("[G1] 索引 " + ProbeWhole(static_cast<long long>(kStructMemberProbeDumpFirst)) +
			  "〜" + ProbeWhole(static_cast<long long>(kStructMemberProbeDumpLast)) + " の全件");
	for (size_t at = 0; at < rows.size(); ++at)
	{
		if (rows[at].index < kStructMemberProbeDumpFirst ||
			rows[at].index > kStructMemberProbeDumpLast)
			continue;
		probe.log("[G1] " + ProbeRowLine(rows[at]));
	}

	// 面ごとの員数（2D 属性が本当に 3 面ぶんの繰り返しかを数で確かめる）。
	for (size_t face = 0; face < sizeof(kProbeFaces) / sizeof(kProbeFaces[0]); ++face)
	{
		size_t countForFace = 0;
		for (size_t at = 0; at < rows.size(); ++at)
		{
			if (rows[at].index < kStructMember2DAttrFirst ||
				rows[at].index > kStructMember2DAttrLast)
				continue;
			if (ProbeEndsWith(rows[at].universalName, kProbeFaces[face].suffix))
				++countForFace;
		}
		probe.log(std::string("[G1] 面 ") + kProbeFaces[face].suffix + "（" +
				  kProbeFaces[face].japanese +
				  "）の件数=" + ProbeWhole(static_cast<long long>(countForFace)));
	}

	// ---- G2: 既定のままの描画（比較の基準） ------------------------------
	ProbeDumpChildren(probe, "[G2] A（既定）", memberA);

	// ---- G3: 狙った書き込み ----------------------------------------------
	// 「構造材＝クラススタイル・被覆と中心線は非表示・端部は両端」を、名前とポップアップの
	// 表示から**当てて**書く。当てた根拠を 1 行ずつ出すので、外していてもログから分かる。
	MCObjectHandle memberB = ProbeMakeMember(probe, "[G3] B", 5000.0);
	if (memberB != nil)
	{
		VWParametricObj pioB(memberB);
		std::vector<ProbeParamRow> rowsB;
		ProbeCollectRows(pioB, rowsB);
		std::vector<ProbeWriteRecord> writes;
		for (size_t at = 0; at < rowsB.size(); ++at)
		{
			const ProbeParamRow& row = rowsB[at];
			if (row.index < kStructMember2DAttrFirst || row.index > kStructMember2DAttrLast)
				continue;
			const std::string part = ProbePartOf(row.universalName);
			if (part.empty())
				continue;
			if (part == "構造材")
			{
				const std::string classKey = ProbeChoiceKeyByDisplay(row, "クラス");
				if (!classKey.empty())
					ProbeApplyValue(pioB, row, classKey, "構造材: 表示に「クラス」を含む選択肢",
									writes);
				else if (row.fieldStyle == kFieldBoolean &&
						 ProbeHasText(row.universalName, "Class"))
					ProbeApplyValue(pioB, row, "1", "構造材: 名前に Class を含む真偽欄 → true",
									writes);
			}
			else if (part == "被覆" || part == "中心線")
			{
				const std::string hiddenKey = ProbeChoiceKeyByDisplay(row, "非表示");
				if (!hiddenKey.empty())
					ProbeApplyValue(pioB, row, hiddenKey, part + ": 表示に「非表示」を含む選択肢",
									writes);
				else if (row.fieldStyle == kFieldBoolean)
					ProbeApplyValue(pioB, row, "0", part + ": 真偽欄 → false（表示を落とす狙い）",
									writes);
			}
			else if (part == "端部")
			{
				const std::string bothKey = ProbeChoiceKeyByDisplay(row, "両端");
				if (!bothKey.empty())
					ProbeApplyValue(pioB, row, bothKey, "端部: 表示に「両端」を含む選択肢", writes);
			}
		}
		probe.log("[G3] 書いた件数=" + ProbeWhole(static_cast<long long>(writes.size())));
		for (size_t at = 0; at < writes.size(); ++at)
		{
			probe.log("[G3] 書いた " + ProbeWhole(static_cast<long long>(writes[at].index)) + " " +
					  writes[at].universalName + " [" + writes[at].before + "] → [" +
					  writes[at].wrote + "] 理由: " + writes[at].why);
		}
		if (writes.empty())
		{
			probe.fail("[G3] 名前と選択肢から当てられたパラメータが 1 件も無かった"
					   "（G1 の表を読んで索引を直に指す版へ作り替えること）");
		}

		// 書いた直後（ResetObject の前）に読み戻す——「書けたか」と「残るか」を分けて見る。
		size_t stuckBeforeReset = 0;
		for (size_t at = 0; at < writes.size(); ++at)
		{
			if (ProbeText(pioB.GetParamValue(writes[at].index)) == writes[at].wrote)
				++stuckBeforeReset;
		}
		probe.log("[G3] ResetObject の前に書けていた件数=" +
				  ProbeWhole(static_cast<long long>(stuckBeforeReset)) + " / " +
				  ProbeWhole(static_cast<long long>(writes.size())));

		const bool resetOk = gSDK->ResetObject(memberB) != 0;
		probe.log(std::string("[G3] ResetObject=") + (resetOk ? "true" : "false"));
		size_t stuckAfterReset = 0;
		for (size_t at = 0; at < writes.size(); ++at)
		{
			const std::string now = ProbeText(pioB.GetParamValue(writes[at].index));
			if (now == writes[at].wrote)
				++stuckAfterReset;
			else
				probe.log("[G3] 戻った " + ProbeWhole(static_cast<long long>(writes[at].index)) +
						  " " + writes[at].universalName + " 書いた=[" + writes[at].wrote +
						  "] いま=[" + now + "]");
		}
		probe.log("[G3] ResetObject の後も残った件数=" +
				  ProbeWhole(static_cast<long long>(stuckAfterReset)) + " / " +
				  ProbeWhole(static_cast<long long>(writes.size())));
		ProbeDumpChildren(probe, "[G3] B（書いた後）", memberB);
	}

	// ---- G4: AttributesMode / AttributesMode3D の 4 値 --------------------
	MCObjectHandle memberC = ProbeMakeMember(probe, "[G4] C", 10000.0);
	if (memberC != nil)
	{
		VWParametricObj pioC(memberC);
		const char* const kModeNames[] = {"AttributesMode", "AttributesMode3D"};
		for (size_t which = 0; which < sizeof(kModeNames) / sizeof(kModeNames[0]); ++which)
		{
			const size_t index = pioC.GetParamIndex(TXString(kModeNames[which]));
			if (index == size_t(-1) || index >= pioC.GetParamsCount())
			{
				probe.log(std::string("[G4] ") + kModeNames[which] + " を名前で引けなかった");
				continue;
			}
			const std::string before = ProbeText(pioC.GetParamValue(index));
			probe.log(std::string("[G4] ") + kModeNames[which] +
					  " 索引=" + ProbeWhole(static_cast<long long>(index)) + " 既定=[" + before +
					  "] 欄型=" + ProbeFieldStyleName(static_cast<int>(pioC.GetParamStyle(index))));
			const size_t choiceCount = pioC.PopupGetChoicesCount(index);
			for (size_t choice = 0; choice < choiceCount; ++choice)
			{
				TXString key;
				TXString display;
				pioC.PopupGetChoice(index, choice, key, display);
				const std::string keyText = ProbeText(key);
				probe.log(std::string("[G4] ") + kModeNames[which] + " 選択肢[" +
						  ProbeWhole(static_cast<long long>(choice)) + "] キー=[" + keyText +
						  "] 表示=[" + ProbeText(display) + "] を当てる");
				pioC.SetParamValue(index, key);
				const bool ok = gSDK->ResetObject(memberC) != 0;
				probe.log(std::string("[G4] ") + kModeNames[which] + "=[" + keyText +
						  "] 読み戻し=[" + ProbeText(pioC.GetParamValue(index)) +
						  "] ResetObject=" + (ok ? "true" : "false"));
				ProbeDumpChildren(probe,
								  std::string("[G4] ") + kModeNames[which] + "=[" + keyText + "]",
								  memberC);
			}
			// 次の欄を見る前に既定へ戻す（2 つの欄の効き目が混ざらないように）。
			pioC.SetParamValue(index, TXString(before.c_str()));
			gSDK->ResetObject(memberC);
		}
	}

	// ---- G5: 48〜152 を全件「今と違う値」へ倒す ---------------------------
	// 名前の当てが外れても、「書いた値は ResetObject を通って残るか」「描画は変わるか」
	// だけは必ず答えが出るようにする乱暴な試験。**最も壊しやすいので最後**。
	MCObjectHandle memberD = ProbeMakeMember(probe, "[G5] D", 15000.0);
	if (memberD != nil)
	{
		VWParametricObj pioD(memberD);
		std::vector<ProbeParamRow> rowsD;
		ProbeCollectRows(pioD, rowsD);
		std::vector<ProbeWriteRecord> writes;
		for (size_t at = 0; at < rowsD.size(); ++at)
		{
			const ProbeParamRow& row = rowsD[at];
			if (row.index < kStructMember2DAttrFirst || row.index > kStructMember2DAttrLast)
				continue;
			if (!row.choices.empty())
			{
				// いまの値と違う最初の選択肢へ倒す。
				for (size_t choice = 0; choice < row.choices.size(); ++choice)
				{
					if (row.choices[choice].first != row.value)
					{
						ProbeApplyValue(pioD, row, row.choices[choice].first,
										"選択肢のうち今と違う最初のもの", writes);
						break;
					}
				}
			}
			else if (row.fieldStyle == kFieldBoolean)
			{
				const bool now = pioD.GetParamBool(row.index);
				ProbeApplyValue(pioD, row, now ? "0" : "1", "真偽欄の反転", writes);
			}
		}
		probe.log("[G5] 倒した件数=" + ProbeWhole(static_cast<long long>(writes.size())));
		const bool resetOk = gSDK->ResetObject(memberD) != 0;
		probe.log(std::string("[G5] ResetObject=") + (resetOk ? "true" : "false"));
		size_t stuck = 0;
		size_t reverted = 0;
		for (size_t at = 0; at < writes.size(); ++at)
		{
			const std::string now = ProbeText(pioD.GetParamValue(writes[at].index));
			if (now == writes[at].wrote)
			{
				++stuck;
			}
			else
			{
				++reverted;
				if (reverted <= 20)
				{
					probe.log("[G5] 戻った " +
							  ProbeWhole(static_cast<long long>(writes[at].index)) + " " +
							  writes[at].universalName + " 書いた=[" + writes[at].wrote +
							  "] いま=[" + now + "]");
				}
			}
		}
		probe.log("[G5] 残った=" + ProbeWhole(static_cast<long long>(stuck)) +
				  " 戻った=" + ProbeWhole(static_cast<long long>(reverted)) +
				  (reverted > 20 ? "（戻った分の 21 件目以降は省いた）" : ""));
		ProbeDumpChildren(probe, "[G5] D（全件倒した後）", memberD);
	}

	probe.log("おわり");
}
