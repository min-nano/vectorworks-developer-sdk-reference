//
//	ProbeMenuTextTests.cpp
//
//	ダイアログに出す文字列の組み立て（plugin/src/ProbeMenuText.h）の単体テスト。
//
//	【なぜテストが要るか】**ここが横幅を決める**。素性の行が長くなると、レイアウト
//	ダイアログはその 1 行に合わせて横へ伸びる（大きさは作るときに 1 度だけ決まるので、
//	後から縮められない。Findings「Layout Dialogs」）。実機で「横長すぎる」と気付いて
//	直したので、**長さそのものを試験にしておく**。
//
//	走らせ方（CI の lint ワークフローも同じ）:
//	    g++ -std=c++20 -Wall -Wextra -Werror -I plugin/src
//	        plugin/tests/ProbeMenuTextTests.cpp -o /tmp/t && /tmp/t
//

#include "ProbeMenuText.h"

#include <cstdio>
#include <string>

using namespace vwprobe::text;

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

	void checkAtMost(const std::string& actual, std::size_t limit, const char* what)
	{
		const std::size_t n = CharCount(actual);
		if (n > limit)
		{
			std::printf("FAIL: %s\n  %zu 文字（上限 %zu）: [%s]\n", what, n, limit, actual.c_str());
			++gFailures;
		}
	}
} // namespace

int main()
{
	// --- 文字数の数え方（UTF-8）---------------------------------------------
	checkEq(std::to_string(CharCount("abc")), "3", "ASCII の文字数");
	checkEq(std::to_string(CharCount("あいう")), "3", "多バイトの文字数（バイト数ではない）");

	// --- 詰める --------------------------------------------------------------
	checkEq(Ellipsize("abcdef", 10), "abcdef", "上限以下ならそのまま");
	checkEq(Ellipsize("abcdef", 6), "abcdef", "ちょうどならそのまま");
	checkEq(Ellipsize("abcdef", 4), "abc…", "超えたら末尾を … にする");
	// **多バイト文字を割らない**（割ると実機で文字化けする）。
	checkEq(Ellipsize("あいうえお", 3), "あい…", "多バイトでも文字の境で切る");
	checkEq(Ellipsize("abc", 0), "", "上限 0 なら空");

	// --- 時刻を詰める --------------------------------------------------------
	checkEq(ShortTime("2026-09-07T10:46:35Z"), "09-07 10:46", "年と秒を落とす");
	// 形が違えば触らない（ローカルビルドの "local" のような値が入りうる）。
	checkEq(ShortTime("local"), "local", "形が違えばそのまま");
	checkEq(ShortTime(""), "", "空はそのまま");

	// --- 素性の 1 行 ---------------------------------------------------------
	checkEq(StampLine("カタログ", "main", "8188852", "2026-09-07T10:46:35Z", "95f0558e87c3"),
			"カタログ: main 8188852 id=95f0558e87c3 (09-07 10:46)", "カタログの行");
	// 長いブランチ名（自動生成のもの）は頭打ちにする。これを止めないと、この 1 行だけで
	// ダイアログが倍の幅になる。
	checkEq(StampLine("殻", "claude/probe-auto-update-workflow-2r7a8d", "68c14dc",
					  "2026-09-03T22:24:03Z", "592b5b938687"),
			"殻: claude/probe-auto-upd… 68c14dc id=592b5b938687 (09-03 22:24)", "殻の行");
	// 空の項目は詰める（「本体: 　 id=」のような隙間を作らない）。
	checkEq(StampLine("本体", "", "", "", "abc"), "本体: id=abc", "空の項目は出さない");

	// **横幅の歯止め。** 素性の行が伸びれば、ダイアログはその 1 行に合わせて横へ伸びる
	// （プルダウンの幅は ProbeMenu.cpp の kPopupWidthChars = 52 標準文字）。ここは
	// 目安の歯止め——全角を 1 文字と数えているので厳密な幅ではないが、
	// 「ブランチ名がそのまま伸びる」たぐいの後戻りはこれで止まる。
	checkAtMost(StampLine("殻", "claude/probe-auto-update-workflow-2r7a8d", "68c14dc",
						  "2026-09-03T22:24:03Z", "592b5b938687"),
				64, "素性の行の長さ（半角換算でプルダウン幅に収まること）");

	if (gFailures == 0)
		std::printf("ProbeMenuTextTests: すべて通りました。\n");
	return (gFailures == 0) ? 0 : 1;
}
