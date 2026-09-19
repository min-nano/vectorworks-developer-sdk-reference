//
//	probes/runtime/pio-param-table/probe.cpp
//
//	[issue #82] PIO のパラメータ表（GetParamsCount / GetParamName /
//	GetParamLocalizedName）が、同じ種別のインスタンスなら常に同一かを実測する。
//	`ResolveParamName` の結果を**種別ごとに 1 度だけ解決してキャッシュしてよいか**を
//	決めるための調査。
//
//	**これは 4 回目の版である。** 1 回目で、値違い・ポップアップ違い・同じ本の
//	書き換え・別文書の 4 通りは**すべて表が一致**し、コストも採れた。3 回目で
//	図面の辿り方が確かめられ、**スタイルを当てた本の表も一致**した——が、
//	**当てたスタイルが構造材用だったのかを確かめていない**（資源から拾った
//	最初の 1 本を使っており、それは文書に元からあった別種別のスタイルでありうる。
//	`SetStyle` は `IsPluginStyle` しか見ないので、別種別でも通ってしまう）。
//
//	3 回目で分かったこと（この版の作りの前提）:
//	  * 図面の辿り方は **`FirstMemberObj(レイヤ)` ＋ `NextObject` で正しい**
//	    （A の親・親レイヤ・いまのレイヤは同じ handle。3 通りとも A を拾えた）。
//	  * **`CreatePluginStyle` を呼ぶと、図面にあった構造材が型 86 から型 21 へ
//	    変わり、パラメトリックオブジェクトではなくなる**（渡したハンドルが無効に
//	    なるのもこれ）。1・2 回目の「拾えた構造材 = 0 本」は正しい観測だった。
//	  * ダイアログは**スタイル名ではなく「スタイル資源を置くフォルダ」**を尋ねる。
//
//	そこでこの版は、**スタイルの一覧を作る前と後で突き合わせ、増えた 1 本を使う**。
//	増えた 1 本は**その構造材から作られたスタイル**なので、種別を問い合わせなくても
//	「構造材用のスタイル」だと言い切れる。
//
//	  G1  基準の本 A を作り、表と、いま図面にあるプラグインスタイルを全部控える。
//	  G2  `CreatePluginStyle`（**「フォルダ選択」が出る。選ばれているまま OK**）。
//	  G3  スタイルの一覧を採り直し、**増えた 1 本**を新しい本へ当てて表を突き合わせる。
//	      増えていなければ、3 回目と同じく既にあった 1 本で当ててみて、**どちらで
//	      確かめたのかをログに明記する**。
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

	// **資源（シンボル定義）側のプラグインスタイルを全部集める。** CreatePluginStyle が
	// 尋ねてくるのは「どのフォルダへ置くか」なので、置き場所は利用者が選ぶ——
	// どこへ置かれても拾えるように、シンボルライブラリを入れ子ごと辿る。
	void CollectPluginStyles(MCObjectHandle container, int depth, size_t& visited,
							 std::vector<RefNumber>& outRefs, std::vector<std::string>& outNames)
	{
		if (container == nil || depth > 4 || visited > 4000)
			return;

		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil && visited < 4000;
			 h = gSDK->NextObject(h))
		{
			++visited;
			if (gSDK->IsPluginStyle(h))
			{
				const RefNumber ref = gSDK->GetObjectInternalIndex(h);
				if (ref > 0)
				{
					TXString name;
					gSDK->GetObjectName(h, name);
					outRefs.push_back(ref);
					outNames.push_back(Str(name));
				}
			}
			CollectPluginStyles(h, depth + 1, visited, outRefs, outNames);
		}
	}

	// 一覧を採って、件数と中身をログへ出す。
	std::vector<RefNumber> ListPluginStyles(::vwprobe::Report& probe, const char* label)
	{
		std::vector<RefNumber> refs;
		std::vector<std::string> names;
		size_t visited = 0;
		CollectPluginStyles(gSDK->GetSymbolLibraryHeader(), 0, visited, refs, names);

		std::string listing;
		for (size_t i = 0; i < refs.size() && i < 20; ++i)
			listing += " " + names[i] + "(" + std::to_string(static_cast<long>(refs[i])) + ")";
		probe.log(std::string(label) + ": プラグインスタイル " + std::to_string(refs.size()) +
				  " 本（見たシンボル定義 " + std::to_string(visited) +
				  " 件）:" + (listing.empty() ? " （無し）" : listing));
		return refs;
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
		 "スタイルを作る前と後で一覧を突き合わせ、**増えた 1 本＝その構造材から作られた"
		 "スタイル**を新しい本へ当てて、パラメータ表が変わるかを確かめる"
		 "（途中で「フォルダ選択」が出るので、選ばれているまま OK）")
{
	// -------------------------------------------------------------- G1
	probe.log("[G1] 基準の本 A と、いま図面にあるスタイルを控える");
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
	WalkContainer(probe, gSDK->GetCurrentLayer(), "いまのレイヤ（作る前）");

	const std::vector<RefNumber> stylesBefore = ListPluginStyles(probe, "[G1] 作る前");

	// -------------------------------------------------------------- G2
	probe.log("[G2] スタイルを作る。**ここで「フォルダ選択」のダイアログが出ます**"
			  "——尋ねているのは名前ではなく**スタイルを置くフォルダ**なので、"
			  "**選ばれているまま「OK」を押してください**（どのフォルダでも構いません）");
	MCObjectHandle memberForStyle = CreateMember(1000.0);
	if (memberForStyle == nil)
	{
		probe.fail("スタイルの元にする本を作れなかった");
		return;
	}

	const SteadyClock::time_point startStyle = SteadyClock::now();
	gSDK->CreatePluginStyle(memberForStyle);
	const double styleMs = ElapsedMs(startStyle);
	probe.log("[G2] CreatePluginStyle から戻った（所要 " + Num(styleMs) +
			  "ms。1000ms を超えていれば人を待っていた＝ダイアログが出た）");
	probe.log("[G2] 渡したハンドル E = " + HandleText(memberForStyle) +
			  " は今: 件数=" + std::to_string(VWParametricObj(memberForStyle).GetParamsCount()));
	WalkContainer(probe, gSDK->GetCurrentLayer(), "いまのレイヤ（作った後）");

	// -------------------------------------------------------------- G3
	const std::vector<RefNumber> stylesAfter = ListPluginStyles(probe, "[G3] 作った後");

	RefNumber newStyle = 0;
	for (size_t i = 0; i < stylesAfter.size(); ++i)
	{
		bool seenBefore = false;
		for (size_t j = 0; j < stylesBefore.size(); ++j)
			if (stylesBefore[j] == stylesAfter[i])
				seenBefore = true;
		if (!seenBefore)
		{
			newStyle = stylesAfter[i];
			break;
		}
	}

	const char* provenance = nullptr;
	RefNumber useStyle = 0;
	if (newStyle > 0)
	{
		useStyle = newStyle;
		provenance = "**増えた 1 本**（＝この構造材から作られたスタイル）";
	}
	else if (!stylesAfter.empty())
	{
		useStyle = stylesAfter[0];
		provenance = "**増えた本が無かったので、元からあった 1 本**（種別は不明）";
	}

	if (useStyle == 0)
	{
		probe.fail("プラグインスタイルが 1 本も無い（ダイアログをキャンセルしたか、"
				   "CreatePluginStyle がスタイルを作らなかった）");
		probe.log("おわり");
		return;
	}
	probe.log("[G3] 当てるスタイル ref=" + std::to_string(static_cast<long>(useStyle)) +
			  " 出所: " + provenance);

	MCObjectHandle styled = CreateMember(2000.0);
	if (styled == nil)
	{
		probe.fail("スタイルを当てる本を作れなかった");
		probe.log("おわり");
		return;
	}
	const ParamTable beforeStyle = DumpTable(styled);
	probe.log("[G3] 当てる前 " + HandleText(styled) + ": " + CompareTables(tableBase, beforeStyle));

	{
		VWParametricObj obj(styled);
		obj.SetStyle(useStyle);
	}
	const bool reset = gSDK->ResetObject(styled);

	VWParametricObj styledObj(styled);
	const RefNumber nowRef = styledObj.GetStyleRefNumber();
	MCObjectHandle symDef = nil;
	const bool gotSym = gSDK->GetPluginStyleSymbol(styled, symDef);
	const ParamTable afterStyle = DumpTable(styled);
	probe.log("[G3] 当てた後: ResetObject=" + std::string(reset ? "true" : "false") +
			  " styleRef=" + std::to_string(static_cast<long>(nowRef)) +
			  " styleHandle=" + HandleText(styledObj.GetStyleHandle()) +
			  " GetPluginStyleSymbol=" + std::string(gotSym ? "true" : "false") + " " +
			  HandleText(symDef) + " formatHandle=" + afterStyle.formatHandle);
	probe.log("[G3] **A（スタイル無し）vs これ（スタイルあり）**: " +
			  CompareTables(tableBase, afterStyle));

	if (nowRef == 0)
		probe.fail("SetStyle が効かなかった（styleRef が 0 のまま）ので、"
				   "「スタイルを当てた表」の比較になっていない");
	else if (newStyle == 0)
		probe.fail("増えたスタイルが見つからなかったので、当てたスタイルが構造材用か"
				   "どうかが分からない（比較そのものは上のとおり）");

	probe.log("おわり");
}
