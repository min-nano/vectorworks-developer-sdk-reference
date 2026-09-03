//
//	PayloadCatalogTests.cpp
//
//	**カタログ**（plugin/src/PayloadCatalog.h）の読み手の単体テスト。
//
//	【なぜこれだけテストがあるか】カタログは「どの本体に何が入っているか」の唯一の索引で、
//	殻はこれだけを見てピッカーを組み、選ばれた群の本体を読む。**壊れても静かに壊れる**
//	——1 行の綴じ違いでプローブが 1 件消えるだけなので、実機では「なぜか出ない」としか
//	見えない。SDK にもプラットフォームにも依存しない文字列処理なので、CI の lint で
//	毎回走らせる。
//
//	走らせ方（CI の lint ワークフローも同じ）:
//	    g++ -std=c++20 -Wall -Wextra -Werror -I plugin/src
//	        plugin/tests/PayloadCatalogTests.cpp -o /tmp/t && /tmp/t
//

#include "PayloadCatalog.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace vwprobe::catalog;

namespace
{
	int gFailures = 0;

	void checkEq(const std::string& actual, const std::string& expected, const char* what)
	{
		if (actual != expected)
		{
			std::printf("FAIL: %s\n  期待: [%s]\n  実際: [%s]\n", what, expected.c_str(),
						actual.c_str());
			++gFailures;
		}
	}

	void checkEq(size_t actual, size_t expected, const char* what)
	{
		if (actual != expected)
		{
			std::printf("FAIL: %s\n  期待: [%zu]\n  実際: [%zu]\n", what, expected, actual);
			++gFailures;
		}
	}

	void checkTrue(bool value, const char* what)
	{
		if (!value)
		{
			std::printf("FAIL: %s\n", what);
			++gFailures;
		}
	}

	// ビルドが書くのと同じ形（plugin/cmake/ProbeCatalog.cmake）。
	const char* kCatalog =
		"# VwSdkProbes probe catalog — 自動生成\n"
		"version=1\n"
		"build=2663132f4081\n"
		"shell=b3aceb717d1f\n"
		"branch=main\n"
		"commit=c99966e\n"
		"built=2026-09-03T09:00:00Z\n"
		"probes=example layer-order(#12)\n"
		"group|main|VwSdkProbesPayload-main.vwpayload||c99966e|main|\n"
		"group|pr12|VwSdkProbesPayload-pr12.vwpayload|12|3f9a1c2|feat/x|重ね順を測る\n"
		"probe|main|example|煙試験: レイヤを数える|図面のレイヤ数を読む\n"
		"probe|pr12|layer-order|レイヤの重ね順を実測する|3 枚作って並べ替える\n";
} // namespace

int main()
{
	// --- ふつうに読める ------------------------------------------------------
	{
		const Catalog cat = Parse(kCatalog);
		checkEq(cat.version, "1", "version");
		checkEq(cat.buildId, "2663132f4081", "ビルド ID");
		checkEq(cat.shellId, "b3aceb717d1f", "殻の ID");
		checkEq(cat.buildTime, "2026-09-03T09:00:00Z", "ビルド時刻");
		checkEq(cat.probesLine, "example layer-order(#12)", "1 行の要約");
		checkEq(cat.groups.size(), size_t(2), "群の数");
		checkEq(cat.probes.size(), size_t(2), "プローブの数");
		checkEq(cat.skippedLines, size_t(0), "読めなかった行");

		// 群の行。**PR 番号が空の群（main）でも列がずれない**ことを確かめる。
		checkEq(cat.groups[0].id, "main", "群の名前");
		checkEq(cat.groups[0].file, "VwSdkProbesPayload-main.vwpayload", "群のファイル");
		checkEq(cat.groups[0].pr, "", "main の PR 番号は空");
		checkEq(cat.groups[1].pr, "12", "PR 番号");
		checkEq(cat.groups[1].prTitle, "重ね順を測る", "PR タイトル");

		// プローブの行と、群との突き合わせ。
		checkEq(cat.probes[1].group, "pr12", "プローブの群");
		checkEq(cat.probes[1].title, "レイヤの重ね順を実測する", "表示名");
		checkEq(cat.probes[1].summary, "3 枚作って並べ替える", "概要");
		checkTrue(cat.groupOf("pr12") != nullptr, "群を引ける");
		checkTrue(cat.groupOf("pr99") == nullptr, "知らない群は nullptr");
	}

	// --- CRLF（Windows で展開されたとき）------------------------------------
	{
		const Catalog cat = Parse("version=1\r\ngroup|main|P.vwpayload||abc|main|\r\n"
								  "probe|main|example|題|概要\r\n");
		checkEq(cat.version, "1", "CRLF: version");
		checkEq(cat.groups.size(), size_t(1), "CRLF: 群");
		checkEq(cat.groups[0].file, "P.vwpayload", "CRLF: 行末の CR を落とす");
		checkEq(cat.probes[0].summary, "概要", "CRLF: 最後の項目も CR を落とす");
	}

	// --- 壊れた行は飛ばして数える -------------------------------------------
	// **1 行の綴じ違いで全部を捨てない。** 読めた行だけで一覧を出せるほうが、実機では
	// ずっとましである（何も走らせられないのが最悪）。
	{
		const Catalog cat = Parse("version=1\n"
								  "group|main\n"   // 項目が足りない
								  "probe|main\n"   // 同上
								  "なんだこれは\n" // key=value でもない
								  "group|main|P.vwpayload||abc|main|\n"
								  "probe|main|example|題|概要\n");
		checkEq(cat.groups.size(), size_t(1), "壊れた group 行は飛ばす");
		checkEq(cat.probes.size(), size_t(1), "壊れた probe 行は飛ばす");
		checkEq(cat.skippedLines, size_t(3), "飛ばした行を数える");
	}

	// --- 区切り文字が混ざっても、後ろの項目に押し込むだけで壊れない -----------
	{
		const Catalog cat = Parse("probe|main|example|題|概要|付き\n");
		checkEq(cat.probes.size(), size_t(1), "余分な | があっても 1 件");
		checkEq(cat.probes[0].summary, "概要|付き", "余りは最後の項目に入る");
	}

	// --- 表示名が空なら slug で代用 -----------------------------------------
	{
		const Catalog cat = Parse("probe|main|example||\n");
		checkEq(cat.probes[0].title, "example", "表示名が空なら slug");
	}

	// --- 空・コメントだけ ----------------------------------------------------
	{
		const Catalog cat = Parse("# コメントだけ\n\n");
		checkTrue(cat.empty(), "空のカタログ");
		checkEq(cat.skippedLines, size_t(0), "コメントと空行は数えない");
	}

	if (gFailures > 0)
	{
		std::printf("\n%d 件失敗しました。\n", gFailures);
		return EXIT_FAILURE;
	}
	std::printf("PayloadCatalog: すべて通りました。\n");
	return EXIT_SUCCESS;
}
