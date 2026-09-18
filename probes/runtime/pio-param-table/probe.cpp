//
//	probes/runtime/pio-param-table/probe.cpp
//
//	[issue #82] PIO のパラメータ表（GetParamsCount / GetParamName /
//	GetParamLocalizedName）が、同じ種別のインスタンスなら常に同一かを実測する。
//	取り込みプラグイン側の `ResolveParamName`（universal 名で引けなかったら表を端から
//	GetParamLocalizedName で舐める）の結果を**種別ごとに 1 度だけ解決してキャッシュして
//	よいか**を決めるための調査。
//
//	SDK の実装を読んで分かっている機構（VWParametricObj.cpp）:
//	  * GetParamsCount / GetParamName は**インスタンスに付いたパラメータレコード**
//	    （aux list 上の、parametric ビットの立った format を持つ record）から採る。
//	    format は PIO 種別ごと・文書ごとの 1 つのはず——なら表は種別ごとに不変。
//	  * GetParamLocalizedName は 1 呼び出しごとに IExtendedProps を作り、対象オブジェクト
//	    から CodeRefID → FileIndex → IExtension → IID_ParametricParamsProvider の
//	    イベントシンクまで辿り、あれば provider->GetParamNameAt(index)、無ければ
//	    format 名と universal 名で GetLocalizedPluginParameter を引く。
//	    **引数はどちらも索引だけ**なので、インスタンスに依らないはず。
//	この「はず」を実機で潰すのがこのプローブ。確かめるのは次の 6 つ。
//
//	  G1  作った直後の同じ種別 4 本で、表（件数・universal 名・ローカライズ名の並び）が
//	      一致するか。レコードフォーマットのハンドルが同一かどうかも見る。
//	      ついでに A の表を全文ダンプする（次に読む人がそのまま使える）。
//	  G2  1 呼び出しのコスト（cold は G1 の先頭で、warm はここで）。universal 名 /
//	      ローカライズ名 / フルスキャン 1 回ぶん。キャッシュの効き目の見積もりに使う。
//	  G3  値を変えた本・ポップアップを倒した本の表が、既定の本と一致するか。
//	  G4  同じインスタンスを書き換えて ResetObject した前後で表が変わるか。
//	  G5  別の文書で作った同じ種別の表が一致するか（キャッシュを文書で捨てるべきか）。
//	  G6  スタイルを当てた後の表が変わるか。**この節だけダイアログが出るかもしれない**
//	      ので最後に置く（出たら閉じてよい。それまでの行はログに残っている）。
//
//	新規の空図面で走らせる。構造材（StructuralMember）を 5 本と、確認用の別文書を
//	1 つ作る（別文書は最後に閉じる）。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWPolygon2DObj.h"
#include "VWFC/VWObjects/VWRecordFormatObj.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	using SteadyClock = std::chrono::steady_clock;

	// 調べる種別。取り込みプラグインで実際に問題になっているものを使う。
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

	// -------------------------------------------------------------------
	// パラメータ表 1 枚ぶん。比較はこの構造体どうしで行う。
	struct ParamTable
	{
		std::vector<std::string> universalNames;
		std::vector<std::string> localizedNames;
		std::vector<size_t> choiceCounts;
		std::vector<std::string> values;
		std::string formatName;
		std::string formatHandle;
	};

	ParamTable DumpTable(MCObjectHandle hObject)
	{
		ParamTable table;
		VWParametricObj obj(hObject);

		VWRecordFormatObj format = obj.GetRecordFormat();
		table.formatName = Str(format.GetFormatName());
		table.formatHandle = HandleText(static_cast<MCObjectHandle>(format));

		const size_t count = obj.GetParamsCount();
		for (size_t index = 0; index < count; ++index)
		{
			table.universalNames.push_back(Str(obj.GetParamName(index)));
			table.localizedNames.push_back(Str(obj.GetParamLocalizedName(index)));

			TXStringSTLArray choices;
			table.choiceCounts.push_back(obj.GetParamChoices(index, choices) ? choices.size() : 0);
			table.values.push_back(Str(obj.GetParamAsString(index)));
		}
		return table;
	}

	// 2 枚の表の食い違いを人が読める 1 行にする。**値は比べない**（値は変えて回って
	// いるので違って当たり前。見たいのは件数・並び・名前）。
	std::string CompareTables(const ParamTable& lhs, const ParamTable& rhs)
	{
		if (lhs.universalNames.size() != rhs.universalNames.size())
			return "件数が違う（" + std::to_string(lhs.universalNames.size()) + " 対 " +
				   std::to_string(rhs.universalNames.size()) + "）";

		std::string diffs;
		size_t diffCount = 0;
		for (size_t index = 0; index < lhs.universalNames.size(); ++index)
		{
			const bool sameUniversal = lhs.universalNames[index] == rhs.universalNames[index];
			const bool sameLocalized = lhs.localizedNames[index] == rhs.localizedNames[index];
			if (sameUniversal && sameLocalized)
				continue;

			++diffCount;
			if (diffCount <= 5)
			{
				diffs += " [" + std::to_string(index) + "] " + lhs.universalNames[index] + "/" +
						 lhs.localizedNames[index] + " → " + rhs.universalNames[index] + "/" +
						 rhs.localizedNames[index];
			}
		}
		if (diffCount == 0)
			return "一致（件数 " + std::to_string(lhs.universalNames.size()) +
				   "・universal 名もローカライズ名も全索引で同じ）";
		return "相違 " + std::to_string(diffCount) + " 件:" + diffs;
	}

	// 選択肢（ポップアップ）の顔ぶれまで比べる。並びが値に依存しないかを見るため。
	std::string CompareChoices(const ParamTable& lhs, const ParamTable& rhs)
	{
		if (lhs.choiceCounts.size() != rhs.choiceCounts.size())
			return "件数が違うので比較不能";

		std::string diffs;
		size_t diffCount = 0;
		for (size_t index = 0; index < lhs.choiceCounts.size(); ++index)
		{
			if (lhs.choiceCounts[index] == rhs.choiceCounts[index])
				continue;
			++diffCount;
			if (diffCount <= 5)
				diffs += " [" + std::to_string(index) + "] " +
						 std::to_string(lhs.choiceCounts[index]) + " → " +
						 std::to_string(rhs.choiceCounts[index]);
		}
		return diffCount == 0 ? "一致（全索引で選択肢の数が同じ）"
							  : "相違 " + std::to_string(diffCount) + " 件:" + diffs;
	}

	// -------------------------------------------------------------------
	// 構造材を 1 本作る。パスは 2D ポリライン（Findings「パスの型を間違えると…」）。
	MCObjectHandle CreateMember(double offsetY)
	{
		VWPolygon2DObj path;
		path.AddVertex(0.0, offsetY);
		path.AddVertex(3000.0, offsetY);
		return gSDK->CreateCustomObjectPath(kProbeTypeName, static_cast<MCObjectHandle>(path), nil,
											true);
	}

	// 数値として読めるパラメータの値を書き換える（「値が違うインスタンス」を作る）。
	// 変えた件数を返す。
	size_t BumpNumericParams(::vwprobe::Report& probe, MCObjectHandle hObject, size_t maxCount)
	{
		VWParametricObj obj(hObject);
		const size_t count = obj.GetParamsCount();
		size_t bumped = 0;
		for (size_t index = 0; index < count && bumped < maxCount; ++index)
		{
			TXStringSTLArray choices;
			if (obj.GetParamChoices(index, choices) && choices.size() > 1)
				continue; // ポップアップは別のインスタンスで扱う

			const std::string value = Str(obj.GetParamAsString(index));
			if (value.empty())
				continue;

			char* end = nullptr;
			const double parsed = std::strtod(value.c_str(), &end);
			if (end == nullptr || *end != '\0')
				continue; // 数値として読めないものは触らない

			const std::string next = Num(parsed * 2.0 + 13.0);
			obj.SetParamAsString(index, TXString(next.c_str()));
			probe.log("  値を変えた [" + std::to_string(index) + "] " +
					  Str(obj.GetParamName(index)) + ": " + value + " → " + next);
			++bumped;
		}
		return bumped;
	}

	// ポップアップを別の選択肢へ倒す（「断面形状で別の形を選んだ」に当たるインスタンス）。
	size_t SwitchPopupParams(::vwprobe::Report& probe, MCObjectHandle hObject, size_t maxCount)
	{
		VWParametricObj obj(hObject);
		const size_t count = obj.GetParamsCount();
		size_t changed = 0;
		for (size_t index = 0; index < count && changed < maxCount; ++index)
		{
			TXStringSTLArray choices;
			if (!obj.GetParamChoices(index, choices) || choices.size() < 2)
				continue;

			const std::string before = Str(obj.GetParamAsString(index));
			const std::string after = Str(choices[choices.size() - 1]);
			if (before == after)
				continue;

			obj.SetParamAsString(index, TXString(after.c_str()));
			probe.log("  ポップアップを倒した [" + std::to_string(index) + "] " +
					  Str(obj.GetParamName(index)) + "（選択肢 " + std::to_string(choices.size()) +
					  " 個）: " + before + " → " + after);
			++changed;
		}
		return changed;
	}

	void LogTable(::vwprobe::Report& probe, const ParamTable& table, const char* label)
	{
		probe.log(std::string(label) + ": 件数=" + std::to_string(table.universalNames.size()) +
				  " format=" + table.formatName + " formatHandle=" + table.formatHandle);
		for (size_t index = 0; index < table.universalNames.size(); ++index)
		{
			probe.log("  [" + std::to_string(index) + "] univ=" + table.universalNames[index] +
					  " loc=" + table.localizedNames[index] + " choices=" +
					  std::to_string(table.choiceCounts[index]) + " value=" + table.values[index]);
		}
	}
} // namespace

VW_PROBE("pio-param-table", "PIO のパラメータ表がインスタンス間で不変かを確かめる",
		 "同じ種別（StructuralMember）のインスタンスを値違い・ポップアップ違い・別文書・"
		 "スタイル付きで作り、GetParamsCount / GetParamName / GetParamLocalizedName の"
		 "返す表を突き合わせる。1 呼び出しのコストも測る")
{
	// -------------------------------------------------------------- G1
	// **順序について。** 図面を触る（値を書き換える・ResetObject する・文書を開く）ほど
	// 落ちる目が増えるので、**落ちても惜しくない順**に並べてある——先に「A の表の全文」と
	// 「コスト」を採り切り、その後で書き換え・別文書・スタイルへ進む。
	probe.log("[G1] 種別を定義してインスタンスを 4 本作る");
	// 生成時に「オブジェクトの設定」ダイアログを出さない（Findings「生成時に…」）。
	gSDK->DefineCustomObject(kProbeTypeName, kCustomObjectPrefNever);

	MCObjectHandle memberDefault = CreateMember(0.0);
	if (memberDefault == nil)
	{
		probe.fail("CreateCustomObjectPath が nil を返した（種別 StructuralMember を作れない）");
		return;
	}

	// **表に触る前に**、いちばん最初の 1 呼び出しのコストを測る（cold）。
	{
		const SteadyClock::time_point startCtor = SteadyClock::now();
		VWParametricObj obj(memberDefault);
		const double ctorMs = ElapsedMs(startCtor);

		const SteadyClock::time_point startCount = SteadyClock::now();
		const size_t count = obj.GetParamsCount();
		const double countMs = ElapsedMs(startCount);

		const SteadyClock::time_point startUniv = SteadyClock::now();
		const TXString univName = obj.GetParamName(0);
		const double univMs = ElapsedMs(startUniv);

		const SteadyClock::time_point startLoc = SteadyClock::now();
		const TXString locName = obj.GetParamLocalizedName(0);
		const double locMs = ElapsedMs(startLoc);

		probe.log("[G1] cold（この走行での最初の 1 呼び出し）: VWParametricObj 構築=" +
				  Num(ctorMs) + "ms GetParamsCount=" + Num(countMs) + "ms（件数 " +
				  std::to_string(count) + "） GetParamName(0)=" + Num(univMs) +
				  "ms GetParamLocalizedName(0)=" + Num(locMs) + "ms");
		probe.log("[G1] [0] univ=" + Str(univName) + " loc=" + Str(locName));
	}

	MCObjectHandle memberValues = CreateMember(1000.0);
	MCObjectHandle memberPopups = CreateMember(2000.0);
	MCObjectHandle memberStyled = CreateMember(3000.0);
	if (memberValues == nil || memberPopups == nil || memberStyled == nil)
	{
		probe.fail("2 本目以降の CreateCustomObjectPath が nil を返した");
		return;
	}
	probe.log("[G1] A（既定）= " + HandleText(memberDefault) + " B（値違い）= " +
			  HandleText(memberValues) + " C（ポップアップ違い）= " + HandleText(memberPopups) +
			  " D（後でスタイル）= " + HandleText(memberStyled));

	// 作った直後の 4 本を突き合わせる。**まだ何も書き換えていない**ので、ここで
	// 食い違えば「同じ種別でも本ごとに表が違う」ことになる。
	const ParamTable tableDefault = DumpTable(memberDefault);
	const ParamTable tableValuesPre = DumpTable(memberValues);
	const ParamTable tablePopupsPre = DumpTable(memberPopups);
	const ParamTable tableStyledPre = DumpTable(memberStyled);
	LogTable(probe, tableDefault, "[G1] A（既定）の表 全文");
	probe.log("[G1] 作った直後の A vs B: " + CompareTables(tableDefault, tableValuesPre));
	probe.log("[G1] 作った直後の A vs C: " + CompareTables(tableDefault, tablePopupsPre));
	probe.log("[G1] 作った直後の A vs D: " + CompareTables(tableDefault, tableStyledPre));
	probe.log("[G1] レコードフォーマットのハンドル: A=" + tableDefault.formatHandle +
			  " B=" + tableValuesPre.formatHandle + " C=" + tablePopupsPre.formatHandle +
			  " D=" + tableStyledPre.formatHandle + " → " +
			  ((tableDefault.formatHandle == tableValuesPre.formatHandle &&
				tableDefault.formatHandle == tablePopupsPre.formatHandle &&
				tableDefault.formatHandle == tableStyledPre.formatHandle)
				   ? "4 本とも同一のフォーマットを共有している"
				   : "**フォーマットが本ごとに違う**"));

	// -------------------------------------------------------------- G2
	// **コストは図面を書き換える前に測る。** 落ちる目のある操作の後だと、測れずに終わる。
	probe.log("[G2] 1 呼び出しのコストを測る（warm）");
	{
		VWParametricObj obj(memberDefault);
		const size_t count = obj.GetParamsCount();
		if (count == 0)
		{
			probe.fail("パラメータが 0 件なのでコストを測れない");
		}
		else
		{
			const size_t kRepeatCtor = 2000;
			const size_t kRepeatScans = 50;
			size_t sink = 0;

			const SteadyClock::time_point startCtor = SteadyClock::now();
			for (size_t i = 0; i < kRepeatCtor; ++i)
			{
				VWParametricObj each(memberDefault);
				sink += each.GetParamsCount();
			}
			const double ctorMs = ElapsedMs(startCtor);

			const SteadyClock::time_point startUniv = SteadyClock::now();
			for (size_t rep = 0; rep < kRepeatScans; ++rep)
				for (size_t index = 0; index < count; ++index)
					sink += obj.GetParamName(index).GetLength();
			const double univMs = ElapsedMs(startUniv);

			const SteadyClock::time_point startLoc = SteadyClock::now();
			for (size_t rep = 0; rep < kRepeatScans; ++rep)
				for (size_t index = 0; index < count; ++index)
					sink += obj.GetParamLocalizedName(index).GetLength();
			const double locMs = ElapsedMs(startLoc);

			const SteadyClock::time_point startIndex = SteadyClock::now();
			for (size_t rep = 0; rep < kRepeatScans; ++rep)
				for (size_t index = 0; index < count; ++index)
					sink += obj.GetParamIndex(TXString(tableDefault.universalNames[index].c_str()));
			const double indexMs = ElapsedMs(startIndex);

			const double scans = static_cast<double>(kRepeatScans);
			const double calls = scans * static_cast<double>(count);
			probe.log("[G2] パラメータ件数=" + std::to_string(count) + " 走行=" +
					  std::to_string(kRepeatScans) + " 周（sink=" + std::to_string(sink) + "）");
			probe.log("[G2] VWParametricObj 構築 + GetParamsCount: " + Num(ctorMs) + "ms / " +
					  std::to_string(kRepeatCtor) +
					  " 回 = " + Num(ctorMs * 1000.0 / static_cast<double>(kRepeatCtor)) + "us/回");
			probe.log("[G2] GetParamName: " + Num(univMs) +
					  "ms 合計 = " + Num(univMs * 1000.0 / calls) +
					  "us/回、フルスキャン 1 回 = " + Num(univMs / scans) + "ms");
			probe.log("[G2] GetParamLocalizedName: " + Num(locMs) +
					  "ms 合計 = " + Num(locMs * 1000.0 / calls) +
					  "us/回、フルスキャン 1 回 = " + Num(locMs / scans) + "ms");
			probe.log("[G2] GetParamIndex（universal 名で 1 個引く）: " + Num(indexMs) +
					  "ms 合計 = " + Num(indexMs * 1000.0 / calls) + "us/回");
			const double fullScanMs = locMs / scans; // ローカライズ名フルスキャン 1 回
			const double indexCallMs = indexMs / calls; // universal 名で 1 個引く
			probe.log("[G2] ローカライズ名のフルスキャン 1 回は、universal 名で 1 個引くのの約 " +
					  Num(fullScanMs / indexCallMs, 1) + " 倍");
		}
	}

	// -------------------------------------------------------------- G3
	probe.log("[G3] 値とポップアップを変えたインスタンスの表を突き合わせる");
	probe.log("[G3] B の値を変える:");
	const size_t bumped = BumpNumericParams(probe, memberValues, 8);
	probe.log("[G3] B で変えた件数 = " + std::to_string(bumped));
	probe.log("[G3] C のポップアップを倒す:");
	const size_t switched = SwitchPopupParams(probe, memberPopups, 8);
	probe.log("[G3] C で倒した件数 = " + std::to_string(switched));
	probe.log(
		"[G3] ResetObject(B)=" + std::string(gSDK->ResetObject(memberValues) ? "true" : "false") +
		" ResetObject(C)=" + std::string(gSDK->ResetObject(memberPopups) ? "true" : "false"));

	const ParamTable tableValues = DumpTable(memberValues);
	const ParamTable tablePopups = DumpTable(memberPopups);
	probe.log("[G3] A vs B（値違い）: " + CompareTables(tableDefault, tableValues));
	probe.log("[G3] A vs C（ポップアップ違い）: " + CompareTables(tableDefault, tablePopups));
	probe.log("[G3] 選択肢の数 A vs C: " + CompareChoices(tableDefault, tablePopups));
	probe.log("[G3] フォーマットのハンドル: A=" + tableDefault.formatHandle +
			  " B=" + tableValues.formatHandle + " C=" + tablePopups.formatHandle);

	// -------------------------------------------------------------- G4
	probe.log("[G4] 同じインスタンス（A）を変えて ResetObject した前後で表が変わるか");
	const size_t bumpedA = BumpNumericParams(probe, memberDefault, 4);
	const size_t switchedA = SwitchPopupParams(probe, memberDefault, 4);
	probe.log("[G4] A で変えた件数 = " + std::to_string(bumpedA) + " / " +
			  std::to_string(switchedA) + "（値 / ポップアップ）ResetObject=" +
			  std::string(gSDK->ResetObject(memberDefault) ? "true" : "false"));
	const ParamTable tableAfterReset = DumpTable(memberDefault);
	probe.log("[G4] A（変更前）vs A（変更後）: " + CompareTables(tableDefault, tableAfterReset));
	probe.log("[G4] 選択肢の数 A（変更前）vs A（変更後）: " +
			  CompareChoices(tableDefault, tableAfterReset));
	probe.log("[G4] フォーマットのハンドル: 変更前=" + tableDefault.formatHandle +
			  " 変更後=" + tableAfterReset.formatHandle);

	// -------------------------------------------------------------- G5
	probe.log("[G5] 別の文書で作った同じ種別の表と突き合わせる");
	{
		MockUp::TVWArray_OpenFileInformation filesBefore;
		gSDK->GetOpenFilesList(filesBefore);
		Sint32 originalRef = -1;
		for (size_t i = 0; i < filesBefore.GetSize(); ++i)
			if (filesBefore[i].fIsActive)
				originalRef = filesBefore[i].fFileRef;
		probe.log("[G5] 開いている文書 = " + std::to_string(filesBefore.GetSize()) +
				  " 件 いまの fileRef = " + std::to_string(static_cast<long>(originalRef)));

		const bool opened = gSDK->OpenDocumentPath(nil, false);
		probe.log("[G5] OpenDocumentPath(nil, false) = " + std::string(opened ? "true" : "false"));
		if (opened)
		{
			gSDK->DefineCustomObject(kProbeTypeName, kCustomObjectPrefNever);
			MCObjectHandle memberOther = CreateMember(0.0);
			if (memberOther == nil)
			{
				probe.fail("別文書で CreateCustomObjectPath が nil を返した");
			}
			else
			{
				const ParamTable tableOther = DumpTable(memberOther);
				probe.log("[G5] A（文書 1）vs 別文書: " + CompareTables(tableDefault, tableOther));
				probe.log("[G5] 選択肢の数 A vs 別文書: " +
						  CompareChoices(tableDefault, tableOther));
				probe.log("[G5] フォーマット: 文書 1 = " + tableDefault.formatName + " " +
						  tableDefault.formatHandle + " / 別文書 = " + tableOther.formatName + " " +
						  tableOther.formatHandle + " → " +
						  (tableDefault.formatHandle == tableOther.formatHandle
							   ? "**同一ハンドル**"
							   : "別のハンドル（文書ごとに別のフォーマット実体）"));
			}

			// 後始末。CloseDocument は false を返しながら閉じる（Findings/Documents.md）ので、
			// 件数で確かめる。
			const bool closed = gSDK->CloseDocument();
			MockUp::TVWArray_OpenFileInformation filesAfter;
			gSDK->GetOpenFilesList(filesAfter);
			probe.log("[G5] CloseDocument() = " + std::string(closed ? "true" : "false") +
					  " 開いている文書 = " + std::to_string(filesAfter.GetSize()) + " 件");
			if (originalRef >= 0)
				probe.log(
					"[G5] SwitchToOpenFile(" + std::to_string(static_cast<long>(originalRef)) +
					") = " + std::string(gSDK->SwitchToOpenFile(originalRef) ? "true" : "false"));
		}
	}

	// -------------------------------------------------------------- G6
	probe.log("[G6] スタイルを当てた後の表（**ここでダイアログが出ることがある**）");
	{
		RefNumber styleBefore = 0;
		const Boolean hadStyle = gSDK->GetPluginObjectStyle(memberStyled, styleBefore);
		probe.log(
			"[G6] 当てる前: GetPluginObjectStyle = " + std::string(hadStyle ? "true" : "false") +
			" ref=" + std::to_string(static_cast<long>(styleBefore)));

		const SteadyClock::time_point startStyle = SteadyClock::now();
		gSDK->CreatePluginStyle(memberStyled);
		const double styleMs = ElapsedMs(startStyle);

		RefNumber styleAfter = 0;
		const Boolean hasStyleNow = gSDK->GetPluginObjectStyle(memberStyled, styleAfter);
		probe.log("[G6] CreatePluginStyle 所要 = " + Num(styleMs) +
				  "ms（1000ms を超えていたら人を待っている＝ダイアログが出た）");
		probe.log(
			"[G6] 当てた後: GetPluginObjectStyle = " + std::string(hasStyleNow ? "true" : "false") +
			" ref=" + std::to_string(static_cast<long>(styleAfter)));

		const ParamTable tableStyledAfter = DumpTable(memberStyled);
		probe.log("[G6] A（スタイル無し）vs D（スタイルを当てた後）: " +
				  CompareTables(tableDefault, tableStyledAfter));
		probe.log("[G6] 選択肢の数 A vs D: " + CompareChoices(tableDefault, tableStyledAfter));
		probe.log("[G6] フォーマットのハンドル: A=" + tableDefault.formatHandle +
				  " D=" + tableStyledAfter.formatHandle);
	}

	probe.log("おわり");
}
