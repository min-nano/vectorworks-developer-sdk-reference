//
//	probes/runtime/pio-param-table/probe.cpp
//
//	[issue #82] PIO のパラメータ表（GetParamsCount / GetParamName /
//	GetParamLocalizedName）が、同じ種別のインスタンスなら常に同一かを実測する。
//	`ResolveParamName` の結果を**種別ごとに 1 度だけ解決してキャッシュしてよいか**を
//	決めるための調査。
//
//	**これは 2 回目の版である。** 1 回目（PR #84 のコメントに全文が残っている）で、
//	値違い・ポップアップ違い・同じ本の書き換え・別文書の 4 通りは**すべて表が一致**し、
//	コストも採れた。**残ったのは「スタイルを当てた本」だけ**で、そこだけ答えが出なかった:
//
//	  * `gSDK->CreatePluginStyle(h)` は**スタイル名を尋ねるダイアログを出す**
//	    （実測 49818ms＝人を待っていた）。
//	  * **戻ったときには渡したハンドルが無効になっている**（同じハンドルで
//	    GetParamsCount が 0、レコードフォーマットのハンドルが 0x0）。
//	    つまり「スタイルを当てた後のそのオブジェクト」を同じハンドルでは読めない。
//
//	そこでこの版は、スタイルを作った**後で図面から拾い直し**、さらに
//	**そのスタイルを新しい本へ当て直して**（`VWParametricObj::SetStyle`。ダイアログ無し）
//	表を突き合わせる。これで「スタイルの有無で表が変わるか」に決着が付く。
//
//	  G1  基準の本 A を作り、表の署名（件数＋全名前）を採る。
//	  G2  本 E を作り、`CreatePluginStyle(E)` を呼ぶ。**ここでダイアログが出る**ので、
//	      **既定の名前のまま「OK」を押す**（キャンセルするとスタイルが作られず、
//	      この調査は答えが出ない）。
//	  G3  図面の構造材を全部拾い直し、1 本ずつ「スタイル番号」と表を A と突き合わせる。
//	  G4  見つかったスタイルを**新しい本 F へ当て**、`ResetObject` してから表を
//	      A と突き合わせる（ダイアログを通らない経路での確認）。
//
//	新規の空図面で走らせる。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWPolygon2DObj.h"
#include "VWFC/VWObjects/VWRecordFormatObj.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	using SteadyClock = std::chrono::steady_clock;

	const char* const kProbeTypeName = "StructuralMember";

	std::string Str(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string Num(double value, int decimals = 3)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
		return std::string(buf);
	}

	std::string HandleText(MCObjectHandle h)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", static_cast<const void*>(h));
		return std::string(buf);
	}

	double ElapsedMs(SteadyClock::time_point since)
	{
		return std::chrono::duration<double, std::milli>(SteadyClock::now() - since).count();
	}

	// パラメータ表 1 枚ぶん（比較に要るものだけ）。
	struct ParamTable
	{
		std::vector<std::string> universalNames;
		std::vector<std::string> localizedNames;
		std::vector<size_t> choiceCounts;
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

			TXStringSTLArray choices;
			table.choiceCounts.push_back(obj.GetParamChoices(index, choices) ? choices.size() : 0);
		}
		return table;
	}

	// 表の食い違いを 1 行にする（名前と選択肢の数まで見る。値は見ない）。
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

	// 構造材を 1 本作る。パスは 2D ポリライン（Findings「パスの型を間違えると…」）。
	MCObjectHandle CreateMember(double offsetY)
	{
		VWPolygon2DObj path;
		path.AddVertex(0.0, offsetY);
		path.AddVertex(3000.0, offsetY);
		return gSDK->CreateCustomObjectPath(kProbeTypeName, static_cast<MCObjectHandle>(path), nil,
											true);
	}

	// いまのレイヤに載っている構造材を全部拾う（CreatePluginStyle でハンドルが無効に
	// なっても、図面からは拾い直せる——それを確かめるのがこの関数）。
	std::vector<MCObjectHandle> CollectMembers()
	{
		std::vector<MCObjectHandle> found;
		MCObjectHandle layer = gSDK->GetCurrentLayer();
		if (layer == nil)
			return found;

		for (MCObjectHandle h = gSDK->FirstMemberObj(layer); h != nil; h = gSDK->NextObject(h))
		{
			if (!VWParametricObj::IsParametricObject(h))
				continue;
			VWParametricObj obj(h);
			if (Str(obj.GetParametricName()) == kProbeTypeName)
				found.push_back(h);
		}
		return found;
	}
} // namespace

VW_PROBE("pio-param-table", "スタイルを当てた PIO のパラメータ表を確かめる",
		 "スタイルを作って当て直し、GetParamsCount / GetParamName / "
		 "GetParamLocalizedName の返す表がスタイルの有無で変わるかを突き合わせる"
		 "（途中でスタイル名のダイアログが出るので、既定のまま OK を押す）")
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
			  " formatHandle=" + tableBase.formatHandle);
	{
		VWParametricObj obj(memberBase);
		probe.log("[G1] A のスタイル: refNumber=" +
				  std::to_string(static_cast<long>(obj.GetStyleRefNumber())) +
				  " styleHandle=" + HandleText(obj.GetStyleHandle()));
	}

	// -------------------------------------------------------------- G2
	MCObjectHandle memberForStyle = CreateMember(1000.0);
	if (memberForStyle == nil)
	{
		probe.fail("2 本目の CreateCustomObjectPath が nil を返した");
		return;
	}
	probe.log("[G2] E = " + HandleText(memberForStyle) + " → CreatePluginStyle を呼ぶ");
	probe.log("[G2] **ここでスタイル名を尋ねるダイアログが出ます。既定の名前のまま「OK」を"
			  "押してください**（キャンセルするとスタイルが作られず、この調査は答えが"
			  "出ません）");

	const SteadyClock::time_point startStyle = SteadyClock::now();
	gSDK->CreatePluginStyle(memberForStyle);
	const double styleMs = ElapsedMs(startStyle);
	probe.log("[G2] CreatePluginStyle から戻った（所要 " + Num(styleMs) +
			  "ms。1000ms を超えていれば人を待っていた＝ダイアログが出た）");

	// 渡したハンドルがどうなったか（1 回目の版で無効になっていた）。
	{
		VWParametricObj obj(memberForStyle);
		probe.log(
			"[G2] 渡したハンドル E は今: 件数=" + std::to_string(obj.GetParamsCount()) +
			" formatHandle=" + HandleText(static_cast<MCObjectHandle>(obj.GetRecordFormat())) +
			" → " + (obj.GetParamsCount() == 0 ? "**無効になっている**" : "まだ読める"));
	}

	// -------------------------------------------------------------- G3
	probe.log("[G3] 図面から構造材を拾い直して突き合わせる");
	RefNumber foundStyleRef = 0;
	std::vector<MCObjectHandle> members = CollectMembers();
	probe.log("[G3] 拾えた構造材 = " + std::to_string(members.size()) + " 本");
	for (size_t i = 0; i < members.size(); ++i)
	{
		VWParametricObj obj(members[i]);
		const RefNumber styleRef = obj.GetStyleRefNumber();
		if (styleRef > 0 && foundStyleRef == 0)
			foundStyleRef = styleRef;

		const ParamTable table = DumpTable(members[i]);
		probe.log("[G3] [" + std::to_string(i) + "] " + HandleText(members[i]) +
				  " styleRef=" + std::to_string(static_cast<long>(styleRef)) + " styleHandle=" +
				  HandleText(obj.GetStyleHandle()) + " formatHandle=" + table.formatHandle +
				  " / A と: " + CompareTables(tableBase, table));
	}

	// -------------------------------------------------------------- G4
	if (foundStyleRef == 0)
	{
		probe.fail("スタイルの付いた構造材が見つからなかった（ダイアログをキャンセルした"
				   "か、CreatePluginStyle がスタイルを作らなかった）。G4 は走らせられない");
		probe.log("おわり");
		return;
	}

	probe.log(
		"[G4] 見つかったスタイル refNumber=" + std::to_string(static_cast<long>(foundStyleRef)) +
		" を新しい本 F へ当てる（ダイアログを通らない経路）");
	MCObjectHandle memberStyled = CreateMember(2000.0);
	if (memberStyled == nil)
	{
		probe.fail("F の CreateCustomObjectPath が nil を返した");
		probe.log("おわり");
		return;
	}
	const ParamTable tableBeforeStyle = DumpTable(memberStyled);
	probe.log("[G4] F（当てる前）= " + HandleText(memberStyled) +
			  " / A と: " + CompareTables(tableBase, tableBeforeStyle));

	{
		VWParametricObj obj(memberStyled);
		obj.SetStyle(foundStyleRef);
	}
	const bool reset = gSDK->ResetObject(memberStyled);

	VWParametricObj styledObj(memberStyled);
	const ParamTable tableAfterStyle = DumpTable(memberStyled);
	probe.log("[G4] SetStyle 後: ResetObject=" + std::string(reset ? "true" : "false") +
			  " styleRef=" + std::to_string(static_cast<long>(styledObj.GetStyleRefNumber())) +
			  " styleHandle=" + HandleText(styledObj.GetStyleHandle()) +
			  " formatHandle=" + tableAfterStyle.formatHandle);
	probe.log("[G4] A（スタイル無し）vs F（スタイルを当てた後）: " +
			  CompareTables(tableBase, tableAfterStyle));
	if (styledObj.GetStyleRefNumber() == 0)
		probe.fail("F にスタイルが付かなかった（SetStyle が効いていない）ので、"
				   "「スタイルを当てた表」の比較にならない");

	probe.log("おわり");
}
