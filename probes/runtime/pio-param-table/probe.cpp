//
//	probes/runtime/pio-param-table/probe.cpp
//
//	[issue #82] PIO のパラメータ表（GetParamsCount / GetParamName /
//	GetParamLocalizedName）が、同じ種別のインスタンスなら常に同一かを実測する。
//	`ResolveParamName` の結果を**種別ごとに 1 度だけ解決してキャッシュしてよいか**を
//	決めるための調査。
//
//	**これは 3 回目の版である。** 1 回目で、値違い・ポップアップ違い・同じ本の書き換え・
//	別文書の 4 通りは**すべて表が一致**し、コストも採れた（結果は PR #84 のコメント）。
//	**残っているのは「スタイルを当てた本」だけ。**
//
//	2 回目で分かったこと:
//	  * `gSDK->CreatePluginStyle(h)` は**ダイアログを出し**（9363ms 人を待った）、
//	    **戻ったときには渡したハンドルが無効**（件数 0 / フォーマット 0x0）。
//	  * **そのあと図面から構造材を拾い直そうとしたら 0 本だった**——スタイルを当てて
//	    いない基準の本 A まで見つからないので、**拾い方（GetCurrentLayer →
//	    FirstMemberObj）が違う**と見るのが自然。ここを潰さないと先へ進めない。
//
//	そこでこの版は、
//	  G1  基準の本 A を作り、**A がどこに居るのか**を A 自身から聞く
//	      （ParentObject / GetParentLayer / いまのレイヤ）。**ダイアログの前に**
//	      3 通りの辿り方で拾えるかを確かめ、全部ログへ出す。
//	  G2  **ダイアログを通らない道を先に試す**——`GetPluginStyleForTool` で
//	      既にあるスタイルを引き、取れたらそれを新しい本へ当てて表を突き合わせる。
//	  G3  取れなかったときだけ `CreatePluginStyle`（**ここで「フォルダ選択」の
//	      ダイアログが出る**——尋ねているのは名前ではなく**スタイル資源を置くフォルダ**
//	      で、選択されたまま OK を押せばよい）。そのあと
//	      **シンボル定義（資源）側から `IsPluginStyle` でスタイルを探す**
//	      ——どのフォルダへ置かれても見つかるように。合わせて全レイヤからも
//	      構造材を拾い直し、見つかったスタイルを新しい本へ当てて表を突き合わせる。
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

	MCObjectHandle CreateMember(double offsetY)
	{
		VWPolygon2DObj path;
		path.AddVertex(0.0, offsetY);
		path.AddVertex(3000.0, offsetY);
		return gSDK->CreateCustomObjectPath(kProbeTypeName, static_cast<MCObjectHandle>(path), nil,
											true);
	}

	// 入れ物 1 つの中身を数えて、構造材だけを集める。中身の顔ぶれもログへ出す
	// （2 回目の版が 0 本しか拾えなかったので、**何が見えているのか**から確かめる）。
	std::vector<MCObjectHandle> WalkContainer(::vwprobe::Report& probe, MCObjectHandle container,
											  const char* label)
	{
		std::vector<MCObjectHandle> members;
		if (container == nil)
		{
			probe.log(std::string("  ") + label + ": 入れ物が nil");
			return members;
		}

		size_t total = 0;
		std::string kinds;
		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil && total < 50;
			 h = gSDK->NextObject(h))
		{
			++total;
			const short type = gSDK->GetObjectTypeN(h);
			kinds += " " + std::to_string(static_cast<int>(type));
			if (!VWParametricObj::IsParametricObject(h))
				continue;

			VWParametricObj obj(h);
			const std::string univ = Str(obj.GetParametricName());
			kinds += "(" + univ + ")";
			if (univ == kProbeTypeName)
				members.push_back(h);
		}
		probe.log(std::string("  ") + label + " " + HandleText(container) + ": 中身=" +
				  std::to_string(total) + " 件 構造材=" + std::to_string(members.size()) +
				  " 件 型:" + (kinds.empty() ? " （空）" : kinds));
		return members;
	}

	// 全レイヤを回って構造材を集める（ForEachLayerN はレイヤ列挙の唯一の手段——
	// Findings「ストーリを触る API」）。
	std::vector<MCObjectHandle> CollectMembersFromAllLayers(::vwprobe::Report& probe)
	{
		std::vector<MCObjectHandle> all;
		size_t layerCount = 0;
		gSDK->ForEachLayerN(
			[&](MCObjectHandle layer)
			{
				++layerCount;
				const std::string label = "レイヤ " + std::to_string(layerCount);
				std::vector<MCObjectHandle> found = WalkContainer(probe, layer, label.c_str());
				all.insert(all.end(), found.begin(), found.end());
			});
		probe.log("  レイヤ " + std::to_string(layerCount) + " 枚から構造材 " +
				  std::to_string(all.size()) + " 本");
		return all;
	}

	// **資源（シンボル定義）側からプラグインスタイルを探す。** CreatePluginStyle が
	// 尋ねてくるのは「どのフォルダへ置くか」なので、置き場所は利用者が選ぶ——
	// どこへ置かれても見つかるように、シンボルライブラリを入れ子ごと辿る。
	RefNumber FindPluginStyleInResources(::vwprobe::Report& probe, MCObjectHandle container,
										 int depth, size_t& visited)
	{
		if (container == nil || depth > 4 || visited > 2000)
			return 0;

		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil && visited < 2000;
			 h = gSDK->NextObject(h))
		{
			++visited;
			if (gSDK->IsPluginStyle(h))
			{
				TXString name;
				gSDK->GetObjectName(h, name);
				const RefNumber ref = gSDK->GetObjectInternalIndex(h);
				probe.log("  スタイルを見つけた: " + HandleText(h) + " 名前=" + Str(name) +
						  " ref=" + std::to_string(static_cast<long>(ref)) +
						  " 深さ=" + std::to_string(depth));
				if (ref > 0)
					return ref;
			}

			// 入れ子（フォルダ・シンボル定義）の中も見る。
			const RefNumber inner = FindPluginStyleInResources(probe, h, depth + 1, visited);
			if (inner > 0)
				return inner;
		}
		return 0;
	}

	// スタイル番号を新しい本へ当てて、表を基準と突き合わせる。
	void CompareStyledMember(::vwprobe::Report& probe, const ParamTable& tableBase,
							 RefNumber styleRef, const char* label)
	{
		MCObjectHandle member = CreateMember(4000.0);
		if (member == nil)
		{
			probe.fail("スタイルを当てる本を作れなかった");
			return;
		}
		const ParamTable before = DumpTable(member);
		probe.log(std::string(label) + " 当てる前 " + HandleText(member) + ": " +
				  CompareTables(tableBase, before));

		{
			VWParametricObj obj(member);
			obj.SetStyle(styleRef);
		}
		const bool reset = gSDK->ResetObject(member);

		VWParametricObj styled(member);
		const RefNumber nowRef = styled.GetStyleRefNumber();
		const ParamTable after = DumpTable(member);
		probe.log(std::string(label) +
				  " 当てた後: ResetObject=" + std::string(reset ? "true" : "false") +
				  " styleRef=" + std::to_string(static_cast<long>(nowRef)) + " styleHandle=" +
				  HandleText(styled.GetStyleHandle()) + " formatHandle=" + after.formatHandle);
		probe.log(std::string(label) + " **A（スタイル無し）vs これ（スタイルあり）**: " +
				  CompareTables(tableBase, after));
		if (nowRef == 0)
			probe.fail("SetStyle が効かなかった（styleRef が 0 のまま）ので、"
					   "「スタイルを当てた表」の比較になっていない");
	}
} // namespace

VW_PROBE("pio-param-table", "スタイルを当てた PIO のパラメータ表を確かめる",
		 "図面の辿り方を確かめたうえで、スタイルを当てた構造材と当てていない構造材の"
		 "パラメータ表を突き合わせる（既にスタイルがあればダイアログは出ない）")
{
	// -------------------------------------------------------------- G1
	probe.log("[G1] 基準の本 A を作り、A がどこに居るのかを確かめる");
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

	MCObjectHandle parentOfA = gSDK->ParentObject(memberBase);
	MCObjectHandle layerOfA = VWParametricObj(memberBase).GetParentLayer();
	MCObjectHandle current = gSDK->GetCurrentLayer();
	probe.log(
		"[G1] A の親 = " + HandleText(parentOfA) + "（型 " +
		std::to_string(static_cast<int>(parentOfA != nil ? gSDK->GetObjectTypeN(parentOfA) : 0)) +
		"） A の親レイヤ = " + HandleText(layerOfA) + " いまのレイヤ = " + HandleText(current) +
		" → 親レイヤといまのレイヤは" + (layerOfA == current ? "同じ" : "**違う**"));

	probe.log("[G1] 3 通りの辿り方で A を拾えるか（**ダイアログの前に**確かめる）:");
	WalkContainer(probe, current, "いまのレイヤ");
	WalkContainer(probe, layerOfA, "A の親レイヤ");
	WalkContainer(probe, parentOfA, "A の親");
	std::vector<MCObjectHandle> viaLayers = CollectMembersFromAllLayers(probe);
	if (viaLayers.empty())
		probe.log("[G1] **どの辿り方でも A を拾えていない**——拾い方が間違っている");

	// -------------------------------------------------------------- G2
	probe.log("[G2] ダイアログを通らない道: GetPluginStyleForTool で既にあるスタイルを引く");
	RefNumber toolStyleRef = 0;
	const bool gotToolStyle = gSDK->GetPluginStyleForTool(kProbeTypeName, toolStyleRef);
	MCObjectHandle toolStyle = toolStyleRef > 0 ? gSDK->InternalIndexToHandle(toolStyleRef) : nil;
	probe.log("[G2] GetPluginStyleForTool(\"StructuralMember\") = " +
			  std::string(gotToolStyle ? "true" : "false") +
			  " ref=" + std::to_string(static_cast<long>(toolStyleRef)) +
			  " handle=" + HandleText(toolStyle) + " IsPluginStyle=" +
			  std::string(toolStyle != nil && gSDK->IsPluginStyle(toolStyle) ? "true" : "false"));

	if (toolStyle != nil && gSDK->IsPluginStyle(toolStyle))
	{
		CompareStyledMember(probe, tableBase, toolStyleRef, "[G2]");
		probe.log("おわり（ダイアログを通らずに済んだ）");
		return;
	}

	// -------------------------------------------------------------- G3
	probe.log("[G3] スタイルが無いので作る。**ここで「フォルダ選択」のダイアログが出ます**"
			  "——尋ねているのは名前ではなく**スタイルを置くフォルダ**なので、"
			  "**選ばれているまま「OK」を押してください**（どのフォルダでも構いません）。"
			  "キャンセルするとスタイルが作られません");
	MCObjectHandle memberForStyle = CreateMember(1000.0);
	if (memberForStyle == nil)
	{
		probe.fail("スタイルの元にする本を作れなかった");
		return;
	}
	probe.log("[G3] E = " + HandleText(memberForStyle) + " → CreatePluginStyle を呼ぶ");

	const SteadyClock::time_point startStyle = SteadyClock::now();
	gSDK->CreatePluginStyle(memberForStyle);
	const double styleMs = ElapsedMs(startStyle);
	probe.log("[G3] CreatePluginStyle から戻った（所要 " + Num(styleMs) +
			  "ms。1000ms を超えていれば人を待っていた＝ダイアログが出た）");
	probe.log("[G3] 渡したハンドル E は今: 件数=" +
			  std::to_string(VWParametricObj(memberForStyle).GetParamsCount()));

	probe.log("[G3] 資源（シンボル定義）側からスタイルを探す:");
	size_t visited = 0;
	const RefNumber styleInResources =
		FindPluginStyleInResources(probe, gSDK->GetSymbolLibraryHeader(), 0, visited);
	probe.log("[G3] 見たシンボル定義 = " + std::to_string(visited) +
			  " 件 スタイル ref=" + std::to_string(static_cast<long>(styleInResources)));

	probe.log("[G3] 作った後にもう一度、全レイヤから拾い直す:");
	std::vector<MCObjectHandle> after = CollectMembersFromAllLayers(probe);
	RefNumber foundStyle = 0;
	for (size_t i = 0; i < after.size(); ++i)
	{
		VWParametricObj obj(after[i]);
		const RefNumber ref = obj.GetStyleRefNumber();
		probe.log("[G3] [" + std::to_string(i) + "] " + HandleText(after[i]) +
				  " styleRef=" + std::to_string(static_cast<long>(ref)) +
				  " styleHandle=" + HandleText(obj.GetStyleHandle()) +
				  " / A と: " + CompareTables(tableBase, DumpTable(after[i])));
		if (ref > 0 && foundStyle == 0)
			foundStyle = ref;
	}

	// ツールの既定としても引き直してみる（作った直後なら入っているかもしれない）。
	RefNumber afterToolRef = 0;
	gSDK->GetPluginStyleForTool(kProbeTypeName, afterToolRef);
	probe.log("[G3] 作った後の GetPluginStyleForTool = " +
			  std::to_string(static_cast<long>(afterToolRef)));
	if (foundStyle == 0)
		foundStyle = afterToolRef;
	if (foundStyle == 0)
		foundStyle = styleInResources;

	if (foundStyle == 0)
	{
		probe.fail("スタイルを掴めなかった（ダイアログをキャンセルしたか、"
				   "CreatePluginStyle がスタイルを作らなかった）");
		probe.log("おわり");
		return;
	}
	CompareStyledMember(probe, tableBase, foundStyle, "[G3]");
	probe.log("おわり");
}
