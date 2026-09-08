//
//	FeedbackParse.h
//
//	**プローブの結果を PR コメントへ自動で返す仕組みの、純粋な部分。**（意図と流れは
//	Feedback.h、全体像は plugin/README.md「結果を PR へ自動で返す」）
//
//	ここに置くのは SDK にもプラットフォームにも依存しない文字列処理だけ:
//
//	  * 設定（送ってよいか）の読み書き               … Settings / ParseSettings / FormatSettings
//	  * PR コメントの本文                            … Report / CommentBody
//	  * 走行中の控え（落ちたときに次で拾う）の読み書き … FormatPending / ParsePending
//
//	【なぜ切り出すか】UpdateParse.h と同じ理由である——SDK 呼び出しやダイアログの合間に
//	文字列の組み立てが混ざると、どちらの誤りも見つけにくい。切っておけば普通のコンパイラで
//	単体テストできる（plugin/tests/FeedbackParseTests.cpp。CI の lint で毎回走る）。
//
//	【本文の先頭に機械可読の目印を置く】読むのは人ではなく Claude なので、見た目の整形に
//	依らずに「これはプラグインの自動投稿である・どのプローブの・どの群の・どのビルドか」を
//	拾えなければならない。目印の形は実プラグイン
//	（vectorworks-plugin-import-ifc-homeskz の parse/Feedback.h）に倣う。
//
//	【書式は key=value を 1 行 1 つ】設定も控えも、読み書きするのはこのヘッダと単体テスト
//	だけで、値はすべて平たい。パーサを持ち込むより `=` の左右で切るほうが小さく確実に済む
//	（同梱スクリプトの機械可読出力とも同じ流儀）。**値に改行は入れない**（入っていたら
//	空白へ潰す）。
//

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace vwprobe::feedback
{
	// -----------------------------------------------------------------------
	// 小さな文字列の道具。**UpdateParse.h と重ねない**——あちらは「スクリプトの出力を
	// 解く」ための道具で、こちらは「自分が書いたファイルを解く」ための道具である。
	// 同じ名前で違う流儀のものを混ぜないよう、必要な最小限だけをここに持つ。
	// -----------------------------------------------------------------------

	inline std::string TrimSpace(const std::string& s)
	{
		const std::string::size_type b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos)
			return "";
		const std::string::size_type e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	// 1 行へ畳む（改行とタブを空白へ）。key=value の値に入れる前に必ず通す。
	inline std::string OneLine(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		for (const char c : s)
			out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
		return TrimSpace(out);
	}

	// "key=value" の並びから値を引く（無ければ空）。**最初の `=` で切る**ので、値の中の
	// `=`（URL など）はそのまま残る。
	inline std::string ValueOf(const std::string& text, const std::string& key)
	{
		std::string::size_type pos = 0;
		while (pos <= text.size())
		{
			const std::string::size_type eol = text.find('\n', pos);
			const std::string line =
				text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
			const std::string::size_type eq = line.find('=');
			if (eq != std::string::npos && TrimSpace(line.substr(0, eq)) == key)
				return TrimSpace(line.substr(eq + 1));
			if (eol == std::string::npos)
				break;
			pos = eol + 1;
		}
		return "";
	}

	// -----------------------------------------------------------------------
	// 設定（＝**送ってよいか**）。
	//
	// 覚えておくのはこれだけである。宛先の PR はプローブの出所（ビルドのときに決まる）が
	// 持っているので尋ねる必要が無く、トークンはキーチェーン側にある。**1 度答えたら
	// 二度と尋ねない**ために、断ったこと（Never）も残す——尋ね直すのは、利用者が
	// ピッカーの設定項目を自分で開いたときだけ。
	// -----------------------------------------------------------------------

	enum class Consent
	{
		Unset, // まだ尋ねていない（＝次に PR 由来のプローブを走らせるとき 1 度だけ尋ねる）
		Send,  // 走らせたら自動で投稿する
		Never  // 投稿しない（二度と尋ねない）
	};

	struct Settings
	{
		Consent consent = Consent::Unset;
		// 投稿先 "owner/repo"。空なら同梱スクリプトの既定（このリポジトリ）。
		// **持たせてあるのは、公開したくない調査を私有リポジトリへ逃がすため。**
		std::string repo;
	};

	inline std::string FormatSettings(const Settings& settings)
	{
		std::string out = "version=1\nconsent=";
		switch (settings.consent)
		{
		case Consent::Send:
			out += "send";
			break;
		case Consent::Never:
			out += "never";
			break;
		case Consent::Unset:
		default:
			out += "unset";
			break;
		}
		out += "\n";
		if (!settings.repo.empty())
			out += "repo=" + OneLine(settings.repo) + "\n";
		return out;
	}

	// 知らない値・壊れた行は既定へ倒す（**古い版が書いたファイルで機能を止めない**）。
	inline Settings ParseSettings(const std::string& text)
	{
		Settings settings;
		const std::string consent = ValueOf(text, "consent");
		if (consent == "send")
			settings.consent = Consent::Send;
		else if (consent == "never")
			settings.consent = Consent::Never;
		settings.repo = ValueOf(text, "repo");
		return settings;
	}

	// -----------------------------------------------------------------------
	// 1 件ぶんの結果（コメント本文の材料）。**殻が知っていることだけ**で組み立てる
	// ——本体（ペイロード）は降ろした後かもしれないので、値は先に写しておく。
	// -----------------------------------------------------------------------

	struct Report
	{
		// プローブの素性（カタログ由来）。
		std::string probeId;
		std::string title;
		std::string summary;

		// 出所（ビルドのときに決まる。scripts/gather-probes.sh）。
		std::string pr; // PR 番号。**空なら投稿しない**（main のプローブには宛先が無い）
		std::string prTitle;
		std::string group;
		std::string commit;
		std::string branch;

		// どのビルドで走ったか（ピッカー・結果ダイアログに出るのと同じ行）。
		std::string buildId;
		std::string payloadStamp;
		std::string catalogStamp;
		std::string shellStamp;
		std::string platform; // "macOS" / "Windows"

		// 走らせた結果。
		std::string outcome; // 「成功」「失敗: …」「例外で中断: …」
		bool failed = false;
		double seconds = 0.0;
		std::string logPath; // ログの実ファイル（控えから拾い直すときの鍵にもなる）
		std::string startedAt; // 走らせた時刻（ローカル。ISO 8601 風）
		std::string log;	   // ログ全文

		// **前回の走行が終わらずに残っていたものを拾った**（＝VectorWorks ごと落ちた）。
		// 落ちたことそのものが知見なので、隠さずコメントの見出しに出す。
		bool recovered = false;
	};

	// コメント 1 通の上限（バイト）。GitHub の 65536 文字より十分低く取る。
	inline constexpr std::size_t kMaxCommentBytes = 60000;

	// ログを予算内へ収める。**削るのは古いほう**——落ちた原因は末尾に出るので、
	// 頭を落として末尾を残す（削ったことは 1 行で断る）。
	inline std::string ClipLog(const std::string& log, std::size_t budget)
	{
		if (log.size() <= budget)
			return log;
		const std::string note = "…（前半を省略しました）\n";
		if (budget <= note.size())
			return note;
		std::string tail = log.substr(log.size() - (budget - note.size()));
		// 行の途中で切らない（先頭の欠けた行は捨てる）。
		const std::string::size_type nl = tail.find('\n');
		if (nl != std::string::npos)
			tail = tail.substr(nl + 1);
		return note + tail;
	}

	// 出所を 1 行に畳む（ピッカーと同じ言い回し。人が突き合わせられるように）。
	inline std::string ProvenanceLine(const Report& report)
	{
		std::string line;
		if (!report.pr.empty())
			line += "PR #" + report.pr;
		else
			line += report.branch.empty() ? std::string("main") : report.branch;
		if (!report.commit.empty())
			line += " / " + report.commit;
		if (!report.pr.empty() && !report.branch.empty())
			line += " / " + report.branch;
		if (!report.prTitle.empty())
			line += " / " + report.prTitle;
		return line;
	}

	// **機械可読の目印**（コメントの 1 行目）。読む側（Claude）はこれで自動投稿だと分かる。
	inline std::string CommentMarker(const Report& report)
	{
		std::string out = "<!-- vw-probes-result v1 probe=" + OneLine(report.probeId);
		out += " group=" + OneLine(report.group);
		out += " pr=" + OneLine(report.pr);
		out += " build=" + OneLine(report.buildId);
		out += report.failed ? " result=failed" : " result=ok";
		if (report.recovered)
			out += " recovered=yes";
		out += " -->";
		return out;
	}

	// 秒を 2 桁で（printf を持ち込まずに済ませる。テストが値を突き合わせられるように）。
	inline std::string FormatSeconds(double seconds)
	{
		if (seconds < 0.0)
			seconds = 0.0;
		const long long hundredths = static_cast<long long>(seconds * 100.0 + 0.5);
		std::string frac = std::to_string(hundredths % 100);
		if (frac.size() < 2)
			frac = "0" + frac;
		return std::to_string(hundredths / 100) + "." + frac;
	}

	// 表のセルへ入れる（`|` は表の区切りなので逃がす。改行は畳む）。
	inline std::string Cell(const std::string& s)
	{
		const std::string line = OneLine(s);
		std::string out;
		out.reserve(line.size());
		for (const char c : line)
		{
			if (c == '|')
				out += '\\';
			out += c;
		}
		return out;
	}

	// **囲みの長さは中身で決める。** ログには ``` を含む行が入りうる（SDK の出力を
	// そのまま流すため）ので、固定の 3 本だと囲みがそこで閉じて以降が崩れる。
	inline std::string CodeFence(const std::string& body)
	{
		std::size_t longest = 0;
		std::size_t run = 0;
		for (const char c : body)
		{
			if (c == '`')
			{
				++run;
				longest = (run > longest) ? run : longest;
			}
			else
			{
				run = 0;
			}
		}
		return std::string((longest < 3) ? 3 : longest + 1, '`');
	}

	// **PR コメントの本文**（Markdown）。組み立ての順は「目印 → 見出し → 表 → ログ →
	// 素性」で、**ログは最後に、残った予算のぶんだけ**入れる（上限を超えるのはログしか
	// 無いので、そこだけを削れば他の情報は必ず残る）。
	inline std::string CommentBody(const Report& report)
	{
		std::string head = CommentMarker(report) + "\n";
		head +=
			"## 実機プローブ: " + OneLine(report.title.empty() ? report.probeId : report.title) +
			" `[" + OneLine(report.probeId) + "]`\n\n";
		if (report.recovered)
		{
			// **落ちたことを隠さない。** プローブは VectorWorks ごと落とすことがあり、
			// 落ち方そのものが知見になる（probes/runtime/README.md）。ただし
			// 「走り切ったログ」と読まれては困るので、いちばん上で断る。
			head += "> **前の走行が終わらないまま残っていたログです。** 結果ダイアログが"
					"出る前に VectorWorks が終了しています（落ちたか、終了させられたか）。\n"
					"> 下のログは**そこまでに書かれた行**で、末尾がそのまま最後に通った"
					"場所です。\n\n";
		}
		if (!report.summary.empty())
			head += OneLine(report.summary) + "\n\n";

		head += "| | |\n| --- | --- |\n";
		head += "| 結果 | " + Cell(report.outcome) + " |\n";
		head += "| 所要 | " + FormatSeconds(report.seconds) + " 秒 |\n";
		if (!report.startedAt.empty() || !report.platform.empty())
		{
			std::string when = OneLine(report.startedAt);
			if (!report.platform.empty())
				when += (when.empty() ? "" : " / ") + OneLine(report.platform);
			head += "| 実行 | " + Cell(when) + " |\n";
		}
		head += "| 出所 | " + Cell(ProvenanceLine(report)) + " |\n";
		if (!report.buildId.empty())
			head += "| ビルド | `" + Cell(report.buildId) + "`" +
					(report.group.empty() ? "" : "（群 " + Cell(report.group) + "）") + " |\n";
		if (!report.logPath.empty())
			head += "| ログ | `" + Cell(report.logPath) + "` |\n";

		std::string tail = "\n<details><summary>ビルドの素性</summary>\n\n```\n";
		for (const std::string& line :
			 {report.payloadStamp, report.catalogStamp, report.shellStamp})
			if (!line.empty())
				tail += OneLine(line) + "\n";
		tail += "```\n\n</details>\n\n";
		tail += "<sub>VwSdkProbes（実機確認プラグイン）が自動で投稿しました。"
				"人は 1 文字も書いていません — 絵や所見はチャットへ。</sub>\n";

		const std::string fence = CodeFence(report.log);
		const std::string open = "\n### ログ\n\n" + fence + "text\n";
		const std::string close = "\n" + fence + "\n";

		const std::size_t used = head.size() + tail.size() + open.size() + close.size();
		const std::size_t budget = (used >= kMaxCommentBytes) ? 0 : kMaxCommentBytes - used;
		std::string log = ClipLog(report.log, budget);
		while (!log.empty() && (log.back() == '\n' || log.back() == '\r'))
			log.pop_back();

		return head + open + log + close + tail;
	}

	// -----------------------------------------------------------------------
	// 走行中の控え。**プローブは VectorWorks ごと落とすことがある**（落とし方まで含めて
	// 知見になる調査があるので、それは異常ではない）。落ちれば投稿は走らないが、ログは
	// 1 行ごとにファイルへ流してあるので**そこまでの行は残っている**——だから走らせる前に
	// 「どのプローブを・どこのログへ・どの PR へ」を控えておき、**次にメニューを開いた
	// ときに拾って投稿する**。人の操作は増えない（plugin/README.md）。
	//
	// 控えにログ本文は入れない（ファイルにあるものを二重に持たない）。
	// -----------------------------------------------------------------------

	inline constexpr const char* kPendingPrefix = "VwSdkProbes-pending-";
	inline constexpr const char* kPendingSuffix = ".txt";

	// 控えの中で「ここから下はログ」を表す行。**ログは殻が受け取った行をそのまま
	// 書き足していく**（本体が書くログファイルとは別に持つ——本体の書き出し先は
	// 走らせてみるまで分からないので、落ちたときに拾えない）。
	inline constexpr const char* kPendingLogMarker = "--- log ---";

	inline std::string PendingFileName(const std::string& probeId)
	{
		std::string safe;
		for (const char c : probeId)
			safe += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_')
						? c
						: '-';
		return std::string(kPendingPrefix) + safe + kPendingSuffix;
	}

	// finished = 走り切ったか。**走らせる前に false で書き、走り終えたら true で
	// 書き直す**——投稿に失敗した控えは残して次に再挑戦するので、そのとき「結末不明」
	// と読まれないようにするため（Feedback.cpp）。
	inline std::string FormatPending(const Report& report, bool finished)
	{
		std::string out = "version=1\n";
		out += std::string("finished=") + (finished ? "yes" : "no") + "\n";
		out += "outcome=" + OneLine(report.outcome) + "\n";
		out += std::string("failed=") + (report.failed ? "yes" : "no") + "\n";
		out += "seconds=" + FormatSeconds(report.seconds) + "\n";
		out += "probe=" + OneLine(report.probeId) + "\n";
		out += "title=" + OneLine(report.title) + "\n";
		out += "summary=" + OneLine(report.summary) + "\n";
		out += "pr=" + OneLine(report.pr) + "\n";
		out += "prTitle=" + OneLine(report.prTitle) + "\n";
		out += "group=" + OneLine(report.group) + "\n";
		out += "commit=" + OneLine(report.commit) + "\n";
		out += "branch=" + OneLine(report.branch) + "\n";
		out += "build=" + OneLine(report.buildId) + "\n";
		out += "payloadStamp=" + OneLine(report.payloadStamp) + "\n";
		out += "catalogStamp=" + OneLine(report.catalogStamp) + "\n";
		out += "shellStamp=" + OneLine(report.shellStamp) + "\n";
		out += "platform=" + OneLine(report.platform) + "\n";
		out += "startedAt=" + OneLine(report.startedAt) + "\n";
		out += "logPath=" + OneLine(report.logPath) + "\n";
		out += kPendingLogMarker;
		out += "\n";
		return out;
	}

	// 控えを Report へ復元する（**プローブの id が読めなければ false**——それが無いと
	// どのログのことか分からない）。ログは控えの中（marker の下）に入っている。
	inline bool ParsePending(const std::string& text, Report& out)
	{
		// ログの手前までが見出し（ログには何が書かれるか分からないので、そこを
		// key=value として読ませない）。
		std::string header = text;
		std::string log;
		const std::string marker = std::string(kPendingLogMarker) + "\n";
		const std::string::size_type at = text.find(marker);
		if (at != std::string::npos)
		{
			header = text.substr(0, at);
			log = text.substr(at + marker.size());
		}

		if (ValueOf(header, "version").empty())
			return false;
		const std::string id = ValueOf(header, "probe");
		if (id.empty())
			return false;

		out = Report{};
		out.log = log;

		// **走り切っていれば、その結末をそのまま使う。** 走り切っていない（＝結果
		// ダイアログが出る前に VectorWorks が終わった）ものだけを recovered にする。
		const bool finished = (ValueOf(header, "finished") == "yes");
		out.recovered = !finished;
		if (finished)
		{
			out.outcome = ValueOf(header, "outcome");
			out.failed = (ValueOf(header, "failed") == "yes");
			out.seconds = std::strtod(ValueOf(header, "seconds").c_str(), nullptr);
		}
		else
		{
			out.failed = true; // 結末を書けないまま終わっている
			out.outcome = "結末不明（結果を出す前に VectorWorks が終了しました）";
		}
		out.probeId = id;
		out.title = ValueOf(header, "title");
		out.summary = ValueOf(header, "summary");
		out.pr = ValueOf(header, "pr");
		out.prTitle = ValueOf(header, "prTitle");
		out.group = ValueOf(header, "group");
		out.commit = ValueOf(header, "commit");
		out.branch = ValueOf(header, "branch");
		out.buildId = ValueOf(header, "build");
		out.payloadStamp = ValueOf(header, "payloadStamp");
		out.catalogStamp = ValueOf(header, "catalogStamp");
		out.shellStamp = ValueOf(header, "shellStamp");
		out.platform = ValueOf(header, "platform");
		out.startedAt = ValueOf(header, "startedAt");
		out.logPath = ValueOf(header, "logPath");
		return true;
	}
} // namespace vwprobe::feedback
