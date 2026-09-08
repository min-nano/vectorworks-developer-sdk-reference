//
//	FeedbackParseTests.cpp
//
//	結果の自動投稿の**純粋な部分**（plugin/src/FeedbackParse.h）の単体テスト。
//
//	【なぜテストが要るか】ここが壊れても**静かに壊れる**。設定の読み書きが壊れれば
//	「毎回尋ねる／二度と投稿しない」になり、控えの読み書きが壊れれば落ちた回の記録が
//	静かに消え、本文の組み立てが壊れれば PR に読めないコメントが積まれる——どれも
//	実機で 1 周回して初めて気付く類の壊れ方で、mac / Windows の実ビルドでは捕まらない。
//
//	走らせ方（CI の lint ワークフローも同じ）:
//	    g++ -std=c++20 -Wall -Wextra -Werror -I plugin/src
//	        plugin/tests/FeedbackParseTests.cpp -o /tmp/t && /tmp/t
//

#include "FeedbackParse.h"

#include <cstdio>
#include <string>

using namespace vwprobe::feedback;

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

	void checkTrue(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++gFailures;
		}
	}

	void checkContains(const std::string& haystack, const std::string& needle, const char* what)
	{
		if (haystack.find(needle) == std::string::npos)
		{
			std::printf("FAIL: %s\n  [%s] が入っていません\n", what, needle.c_str());
			++gFailures;
		}
	}

	void checkNotContains(const std::string& haystack, const std::string& needle, const char* what)
	{
		if (haystack.find(needle) != std::string::npos)
		{
			std::printf("FAIL: %s\n  [%s] が入っています\n", what, needle.c_str());
			++gFailures;
		}
	}

	// 試験に使う 1 件（走り切ったプローブ）。
	Report sample()
	{
		Report report;
		report.probeId = "layer-order";
		report.title = "レイヤの重ね順を実測する";
		report.summary = "レイヤを 3 枚作り、並べ替えてから読み戻す";
		report.pr = "12";
		report.prTitle = "調査: レイヤの重ね順";
		report.group = "pr12";
		report.commit = "8b2d004";
		report.branch = "claude/layer-order";
		report.buildId = "592b5b938687";
		report.payloadStamp = "本体: claude/layer-order 8b2d004 id=592b5b938687";
		report.catalogStamp = "カタログ: main c99966e id=592b5b938687";
		report.shellStamp = "殻: main c99966e id=592b5b938687";
		report.platform = "macOS";
		report.outcome = "成功";
		report.seconds = 0.42;
		report.logPath = "/tmp/VwSdkProbes-layer-order.log";
		report.startedAt = "2026-09-08 12:34:56";
		report.log = "はじめ\nできた\n";
		return report;
	}
} // namespace

int main()
{
	// --- key=value の読み取り ------------------------------------------------
	checkEq(ValueOf("a=1\nb=2\n", "b"), "2", "2 行目の値");
	checkEq(ValueOf("url=https://x/y?a=b\n", "url"), "https://x/y?a=b",
			"値の中の = は残る（最初の = で切る）");
	checkEq(ValueOf("a=1\n", "c"), "", "無い鍵は空");
	checkEq(OneLine(" 改行\nと\tタブ "), "改行 と タブ", "1 行へ畳む");

	// --- 設定（送ってよいか）-------------------------------------------------
	{
		Settings settings;
		checkTrue(settings.consent == Consent::Unset, "既定はまだ尋ねていない");

		settings.consent = Consent::Send;
		settings.repo = "min-nano/private";
		const Settings back = ParseSettings(FormatSettings(settings));
		checkTrue(back.consent == Consent::Send, "送る設定が往復する");
		checkEq(back.repo, "min-nano/private", "投稿先が往復する");

		settings.consent = Consent::Never;
		settings.repo.clear();
		checkTrue(ParseSettings(FormatSettings(settings)).consent == Consent::Never,
				  "断った設定が往復する（＝二度と尋ねない）");

		// **知らない値・壊れた行で機能を止めない**（古い版が書いたファイルを読む）。
		checkTrue(ParseSettings("consent=maybe\n").consent == Consent::Unset,
				  "知らない値は既定へ倒す");
		checkTrue(ParseSettings("").consent == Consent::Unset, "空のファイルは既定");
	}

	// --- コメント本文 --------------------------------------------------------
	{
		const Report report = sample();
		const std::string body = CommentBody(report);

		// 1 行目は**機械可読の目印**（読むのは Claude。見た目に依らず拾えること）。
		checkTrue(body.starts_with("<!-- vw-probes-result v1 probe=layer-order group=pr12 pr=12 "
								   "issue= build=592b5b938687 result=ok -->\n"),
				  "1 行目が目印");
		checkContains(body, "| 結果 | 成功 |", "結末が表に出る");
		checkContains(body, "| 所要 | 0.42 秒 |", "所要が表に出る");
		checkContains(body, "PR #12 / 8b2d004 / claude/layer-order / 調査: レイヤの重ね順",
					  "出所が 1 行に畳まれる");
		checkContains(body, "はじめ\nできた", "ログ全文が入る");
		checkContains(body, report.shellStamp, "素性が入る");
		checkNotContains(body, "前の走行が終わらないまま", "走り切った回に落ちた断りは出ない");
	}

	// --- 宛先が issue のとき（PR が無い。main に入ったプローブなど）-----------
	// 優先順位は PR → issue（Feedback.h「宛先の決め方」）。pr が空で issue だけ
	// あるときの見出し・目印を確かめる。
	{
		Report report = sample();
		report.pr.clear();
		report.prTitle.clear();
		report.group = "main";
		report.issue = "34";
		const std::string body = CommentBody(report);

		checkTrue(body.starts_with("<!-- vw-probes-result v1 probe=layer-order group=main pr= "
								   "issue=34 build=592b5b938687 result=ok -->\n"),
				  "目印の pr は空、issue に番号");
		checkContains(body, "issue #34 / 8b2d004 / claude/layer-order",
					  "出所は issue 番号で始まる");
		checkEq(ProvenanceLine(report), "issue #34 / 8b2d004 / claude/layer-order", "1 行の出所");
	}

	// --- 秒の整形 ------------------------------------------------------------
	checkEq(FormatSeconds(0.0), "0.00", "0 秒");
	checkEq(FormatSeconds(0.425), "0.43", "四捨五入");
	checkEq(FormatSeconds(12.5), "12.50", "整数部と小数部");
	checkEq(FormatSeconds(-1.0), "0.00", "負の値は 0 へ");

	// --- ログの囲み ----------------------------------------------------------
	{
		// **囲みの長さは中身で決める。** 固定の ``` だと、ログに ``` が出た時点で
		// 囲みが閉じて以降のコメントが崩れる。
		Report report = sample();
		report.log = "前\n```\n後";
		const std::string body = CommentBody(report);
		checkContains(body, "````text\n", "ログに ``` があれば囲みを 1 本長くする");
		checkContains(body, "\n````\n", "閉じも同じ長さ");
	}

	// --- 長すぎるログは頭から削る --------------------------------------------
	{
		Report report = sample();
		report.log.clear();
		for (int i = 0; i < 20000; ++i)
			report.log += "行 " + std::to_string(i) + "\n";
		const std::string body = CommentBody(report);
		checkTrue(body.size() <= kMaxCommentBytes, "上限に収まる");
		checkContains(body, "前半を省略しました", "削ったことを断る");
		checkContains(body, "行 19999", "**末尾は残る**（落ちた原因は末尾に出る）");
		checkNotContains(body, "行 0\n", "頭のほうは落ちている");
	}

	// --- 控え（走行中の記録）------------------------------------------------
	{
		const Report report = sample();

		// 走らせる**前**に書く形。結末はまだ無い。
		const std::string armed = FormatPending(report, /*finished*/ false);
		checkContains(armed, "finished=no\n", "走らせる前は未了と書く");
		checkTrue(armed.ends_with(std::string(kPendingLogMarker) + "\n"),
				  "目印で終わる（この下にログを書き足していく）");

		// 落ちた（＝未了のまま拾われた）ときの読み取り。
		Report recovered;
		checkTrue(ParsePending(armed + "1 行目\n2 行目\n", recovered), "控えを読める");
		checkEq(recovered.probeId, "layer-order", "プローブの id");
		checkEq(recovered.pr, "12", "宛先の PR");
		checkEq(recovered.issue, "", "PR 宛てのときは issue が空のまま往復する");
		checkEq(recovered.log, "1 行目\n2 行目\n", "ログは目印の下の全部");
		checkTrue(recovered.recovered, "未了の控えは「拾ったもの」になる");
		checkTrue(recovered.failed, "結末が書けていないので失敗扱い");
		checkContains(CommentBody(recovered), "前の走行が終わらないまま",
					  "落ちたことを本文の頭で断る");
		checkContains(CommentMarker(recovered), "recovered=yes", "目印にも出る");

		// 走り切ったのに投稿だけ失敗した控え（次に開いたときに送り直す）。
		Report finished;
		checkTrue(ParsePending(FormatPending(report, /*finished*/ true) + "ログ\n", finished),
				  "走り切った控えを読める");
		checkTrue(!finished.recovered, "走り切っていれば「拾ったもの」にしない");
		checkEq(finished.outcome, "成功", "結末をそのまま使う");
		checkEq(FormatSeconds(finished.seconds), "0.42", "所要も残る");

		// 壊れた控えは黙って捨てる（読めないものを投稿しない）。
		Report ignored;
		checkTrue(!ParsePending("", ignored), "空は読めない");
		checkTrue(!ParsePending("version=1\n", ignored), "id が無ければ読めない");

		// ログに key=value めいた行が混ざっても、見出しの値を汚さない。
		Report tricky;
		checkTrue(ParsePending(armed + "pr=999\nprobe=other\n", tricky), "ログ混じりでも読める");
		checkEq(tricky.pr, "12", "**ログの中の pr= を見出しと取り違えない**");
		checkEq(tricky.probeId, "layer-order", "ログの中の probe= も見出しではない");

		// **issue 宛ての控えも同じように往復する**（main に入ったプローブが落ちても、
		// 次の起動で issue へ送り直せる）。
		Report issueReport = sample();
		issueReport.pr.clear();
		issueReport.issue = "34";
		const std::string issueArmed = FormatPending(issueReport, /*finished*/ false);
		Report issueRecovered;
		checkTrue(ParsePending(issueArmed + "落ちる前の行\n", issueRecovered),
				  "issue 宛ての控えを読める");
		checkEq(issueRecovered.pr, "", "issue 宛ては pr が空のまま往復する");
		checkEq(issueRecovered.issue, "34", "issue 番号が往復する");
	}

	// --- 控えのファイル名 ----------------------------------------------------
	checkEq(PendingFileName("layer-order"), "VwSdkProbes-pending-layer-order.txt", "そのまま");
	checkEq(PendingFileName("a/b c"), "VwSdkProbes-pending-a-b-c.txt",
			"パスになりうる字は落とす（一時ディレクトリの外へ書かせない）");

	if (gFailures == 0)
		std::printf("FeedbackParseTests: すべて通りました。\n");
	return (gFailures == 0) ? 0 : 1;
}
