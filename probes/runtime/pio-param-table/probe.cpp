//
//	probes/runtime/pio-param-table/probe.cpp
//
//	[issue #82] PIO のパラメータ表（GetParamsCount / GetParamName /
//	GetParamLocalizedName）が、同じ種別のインスタンスなら常に同一かを実測する。
//	`ResolveParamName` の結果を**種別ごとに 1 度だけ解決してキャッシュしてよいか**を
//	決めるための調査。
//
//	**これは 5 回目の版で、残っている「スタイルを当てた本」だけを見る。**
//	1 回目で値違い・ポップアップ違い・同じ本の書き換え・別文書は**すべて表が一致**した。
//
//	4 回目までで分かったこと（この版の作りの前提）:
//	  * **`gSDK->CreatePluginStyle(h)` はスタイルを増やさない。** 呼ぶ前と後で文書の
//	    プラグインスタイルは 9 本のまま（4 回目の実測）。**ダイアログ（「フォルダ選択」）
//	    まで出るのに増えない**ので、この口からスタイルを用意するのは諦める。
//	  * 一方で **文書には既に構造材用のスタイルがある**——4 回目のログに
//	    `木質構造材_柱・束(89)` / `木質構造材_横架材(87)` が並んでいた。
//	    **だからダイアログを出す必要が無い。**
//	  * `CreatePluginStyle` は渡したハンドルを無効にし、図面にあった構造材を
//	    型 86 から型 21 へ変える（これは #82 の範囲外。別 issue へ）。
//
//	そこでこの版は**ダイアログを 1 つも出さない**。文書にあるプラグインスタイルを
//	全部並べ、**それぞれが「どの PIO 種別のものか」をスタイルのシンボル定義の中身から
//	読み取り**、構造材用のものを新しい本へ当てて表を突き合わせる。
//
//	  G1  基準の本 A の表を採る。
//	  G2  文書のプラグインスタイルを全部並べ、素性（名前・ref・中に入っている PIO の
//	      universal 名）をログへ出す。
//	  G3  **構造材用**と分かったスタイルを順に当て、(a) 当たったか（styleRef と、
//	      **パラメータの値がいくつ変わったか**）と (b) 表が A と一致するかを見る。
//	      構造材用が 1 本も無ければ、全部のスタイルで同じことをして記録する。
//
//	**利用者の操作は「走らせる」だけ。** 新規の空図面で走らせる。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWPolygon2DObj.h"
#include "VWFC/VWObjects/VWRecordFormatObj.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kProbeTypeName = "StructuralMember";

	std::string Str(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string HandleText(MCObjectHandle h)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", static_cast<const void*>(h));
		return std::string(buf);
	}

	// パラメータ表 1 枚ぶん（名前と選択肢の数まで。値は別に採る）。
	struct ParamTable
	{
		std::vector<std::string> universalNames;
		std::vector<std::string> localizedNames;
		std::vector<size_t> choiceCounts;
		std::vector<std::string> values;
		std::string formatHandle;
	};

	ParamTable DumpTable(MCObjectHandle hObject)
	{
		ParamTable table;
		VWParametricObj obj(hObject);

		table.formatHandle = HandleText(static_cast<MCObjectHandle>(obj.GetRecordFormat()));

		const size_t count = obj.GetParamsCount();
		for (size_t index = 0; index < count; ++index)
		{
			table.universalNames.push_back(Str(obj.GetParamName(index)));
			table.localizedNames.push_back(Str(obj.GetParamLocalizedName(index)));
			table.values.push_back(Str(obj.GetParamAsString(index)));

			TXStringSTLArray choices;
			table.choiceCounts.push_back(obj.GetParamChoices(index, choices) ? choices.size() : 0);
		}
		return table;
	}

	// 表の食い違いを 1 行にする（**名前と選択肢の数だけ**。値は当たり前に変わる）。
	std::string CompareTables(const ParamTable& lhs, const ParamTable& rhs)
	{
		if (lhs.universalNames.size() != rhs.universalNames.size())
			return "**件数が違う**（" + std::to_string(lhs.universalNames.size()) + " 対 " +
				   std::to_string(rhs.universalNames.size()) + "）";

		std::string diffs;
		size_t diffCount = 0;
		for (size_t index = 0; index < lhs.universalNames.size(); ++index)
		{
			if (lhs.universalNames[index] == rhs.universalNames[index] &&
				lhs.localizedNames[index] == rhs.localizedNames[index] &&
				lhs.choiceCounts[index] == rhs.choiceCounts[index])
				continue;

			++diffCount;
			if (diffCount <= 5)
				diffs += " [" + std::to_string(index) + "] " + lhs.universalNames[index] + "/" +
						 lhs.localizedNames[index] + "/" + std::to_string(lhs.choiceCounts[index]) +
						 " → " + rhs.universalNames[index] + "/" + rhs.localizedNames[index] + "/" +
						 std::to_string(rhs.choiceCounts[index]);
		}
		if (diffCount == 0)
			return "一致（件数 " + std::to_string(lhs.universalNames.size()) +
				   "・universal 名もローカライズ名も選択肢の数も全索引で同じ）";
		return "**相違 " + std::to_string(diffCount) + " 件**:" + diffs;
	}

	// 値がいくつ変わったか（スタイルが**効いた**ことの手応え）。
	size_t CountChangedValues(const ParamTable& lhs, const ParamTable& rhs, std::string& outSample)
	{
		const size_t n =
			lhs.values.size() < rhs.values.size() ? lhs.values.size() : rhs.values.size();
		size_t changed = 0;
		for (size_t index = 0; index < n; ++index)
		{
			if (lhs.values[index] == rhs.values[index])
				continue;
			++changed;
			if (changed <= 5)
				outSample += " " + lhs.universalNames[index] + ": " + lhs.values[index] + " → " +
							 rhs.values[index];
		}
		return changed;
	}

	MCObjectHandle CreateMember(double offsetY)
	{
		VWPolygon2DObj path;
		path.AddVertex(0.0, offsetY);
		path.AddVertex(3000.0, offsetY);
		return gSDK->CreateCustomObjectPath(kProbeTypeName, static_cast<MCObjectHandle>(path), nil,
											true);
	}

	// 文書にあるプラグインスタイル 1 本の素性。
	struct StyleInfo
	{
		RefNumber ref = 0;
		MCObjectHandle handle = nil;
		std::string name;
		std::string pioName; // スタイルの中に入っていた PIO の universal 名
		std::string kinds;	 // 中身の型（分からなかったときの手がかり）
	};

	// スタイルのシンボル定義の中身から、**どの PIO 種別のスタイルか**を読み取る。
	void FillStyleKind(StyleInfo& info)
	{
		size_t seen = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(info.handle); m != nil && seen < 30;
			 m = gSDK->NextObject(m))
		{
			++seen;
			info.kinds += " " + std::to_string(static_cast<int>(gSDK->GetObjectTypeN(m)));
			if (!VWParametricObj::IsParametricObject(m))
				continue;
			const std::string univ = Str(VWParametricObj(m).GetParametricName());
			info.kinds += "(" + univ + ")";
			if (info.pioName.empty())
				info.pioName = univ;
		}
	}

	void CollectStyles(MCObjectHandle container, int depth, size_t& visited,
					   std::vector<StyleInfo>& out)
	{
		if (container == nil || depth > 4 || visited > 4000)
			return;

		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil && visited < 4000;
			 h = gSDK->NextObject(h))
		{
			++visited;
			if (gSDK->IsPluginStyle(h))
			{
				StyleInfo info;
				info.ref = gSDK->GetObjectInternalIndex(h);
				info.handle = h;
				TXString name;
				gSDK->GetObjectName(h, name);
				info.name = Str(name);
				if (info.ref > 0)
				{
					FillStyleKind(info);
					out.push_back(info);
				}
			}
			CollectStyles(h, depth + 1, visited, out);
		}
	}
} // namespace

VW_PROBE("pio-param-table", "スタイルを当てた PIO のパラメータ表を確かめる",
		 "文書にあるプラグインスタイルの素性（どの PIO 種別のものか）を読み取り、"
		 "構造材用のものを当てて、パラメータ表が変わるかを確かめる"
		 "（ダイアログは 1 つも出ない。走らせるだけ）")
{
	// -------------------------------------------------------------- G1
	probe.log("[G1] 基準の本 A を作る");
	gSDK->DefineCustomObject(kProbeTypeName, kCustomObjectPrefNever);

	MCObjectHandle memberBase = CreateMember(0.0);
	if (memberBase == nil)
	{
		probe.fail("CreateCustomObjectPath が nil を返した（種別 StructuralMember を作れない）");
		return;
	}
	const ParamTable tableBase = DumpTable(memberBase);
	probe.log("[G1] A = " + HandleText(memberBase) +
			  " 件数=" + std::to_string(tableBase.universalNames.size()) +
			  " formatHandle=" + tableBase.formatHandle + " styleRef=" +
			  std::to_string(static_cast<long>(VWParametricObj(memberBase).GetStyleRefNumber())));

	// -------------------------------------------------------------- G2
	probe.log("[G2] 文書にあるプラグインスタイルの素性を読む");
	std::vector<StyleInfo> styles;
	size_t visited = 0;
	CollectStyles(gSDK->GetSymbolLibraryHeader(), 0, visited, styles);
	probe.log("[G2] プラグインスタイル " + std::to_string(styles.size()) +
			  " 本（見たシンボル定義 " + std::to_string(visited) + " 件）");

	std::vector<size_t> forStructuralMember;
	for (size_t i = 0; i < styles.size(); ++i)
	{
		probe.log("[G2] [" + std::to_string(i) + "] " + styles[i].name +
				  " ref=" + std::to_string(static_cast<long>(styles[i].ref)) + " 中の PIO=" +
				  (styles[i].pioName.empty() ? "（見つからず）" : styles[i].pioName) +
				  " 中身の型:" + (styles[i].kinds.empty() ? " （空）" : styles[i].kinds));
		if (styles[i].pioName == kProbeTypeName)
			forStructuralMember.push_back(i);
	}
	probe.log("[G2] **構造材用と分かったスタイル = " + std::to_string(forStructuralMember.size()) +
			  " 本**");

	if (styles.empty())
	{
		probe.fail("この文書にはプラグインスタイルが 1 本も無いので、"
				   "スタイルを当てた表を確かめられない");
		probe.log("おわり");
		return;
	}

	// -------------------------------------------------------------- G3
	// 構造材用が分かっていればそれだけ、分からなければ全部を順に当てる。
	std::vector<size_t> targets = forStructuralMember;
	if (targets.empty())
	{
		probe.log("[G3] 構造材用と特定できたスタイルが無いので、**全部**順に当ててみる");
		for (size_t i = 0; i < styles.size(); ++i)
			targets.push_back(i);
	}

	double offsetY = 1000.0;
	for (size_t t = 0; t < targets.size(); ++t)
	{
		const StyleInfo& info = styles[targets[t]];

		MCObjectHandle member = CreateMember(offsetY);
		offsetY += 1000.0;
		if (member == nil)
		{
			probe.fail("スタイルを当てる本を作れなかった");
			break;
		}
		const ParamTable before = DumpTable(member);

		{
			VWParametricObj obj(member);
			obj.SetStyle(info.ref);
		}
		const bool reset = gSDK->ResetObject(member);

		VWParametricObj styledObj(member);
		const RefNumber nowRef = styledObj.GetStyleRefNumber();
		const ParamTable after = DumpTable(member);

		std::string sample;
		const size_t changed = CountChangedValues(before, after, sample);

		probe.log("[G3] " + info.name + "(" + std::to_string(static_cast<long>(info.ref)) +
				  "・中の PIO=" + (info.pioName.empty() ? "?" : info.pioName) +
				  ") を当てた: ResetObject=" + std::string(reset ? "true" : "false") +
				  " styleRef=" + std::to_string(static_cast<long>(nowRef)) +
				  " formatHandle=" + after.formatHandle +
				  " **値が変わった数=" + std::to_string(changed) + "**" + sample);
		probe.log("[G3]   → A（スタイル無し）vs これ: " + CompareTables(tableBase, after));

		if (nowRef == 0)
			probe.log("[G3]   ※ styleRef が 0 のまま＝このスタイルは当たっていない");
	}

	if (forStructuralMember.empty())
		probe.fail("構造材用と特定できたスタイルが無かった（全部当ててみた結果は上のとおり）");

	probe.log("おわり");
}
