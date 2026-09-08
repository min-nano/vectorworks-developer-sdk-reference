//
//	Feedback.h
//
//	**プローブの結果を、そのプローブが来た PR へ自動で投稿する。**
//
//	【なぜ要るか】プローブは「実機でしか答えの出ない問い」を確かめるためのもので、
//	答え（ログ）は必ず Claude へ渡って `Findings/` になる。その受け渡しを人が手で
//	——結果ダイアログを選択してコピーし、チャットへ貼って——やっている限り、1 往復ごとに
//	その手間が乗る。**プラグイン自身に投稿させれば、人がするのは「入れ替える」と
//	「走らせる」の 2 つだけになる**（実プラグイン側の実機フィードバックの往復と同じ
//	考え方。vectorworks-plugin-import-ifc-homeskz の docs/DEV-NOTES.md M23）。
//
//	【宛先を尋ねない】投稿先の PR は**プローブの出所**が知っている（ビルドのときに決まり、
//	カタログと出所表に載っている。scripts/gather-probes.sh）。だから人に番号を打たせる
//	必要が無い——**main のプローブには宛先が無い**ので、その場合は投稿しない（そちらは
//	もう Findings になっている調査である）。
//
//	【尋ねるのは 1 度だけ】初めて PR 由来のプローブを走らせるときに「投稿してよいか」と
//	トークンを 1 度だけ尋ね、答えを覚える（FeedbackParse.h の Settings）。**断られたら
//	二度と尋ねない**——尋ね直すのは、利用者がピッカーの設定項目を自分で開いたときだけ。
//
//	【走ったあとは何も尋ねない】投稿は黙って行い、結果（コメントの URL か失敗の理由）は
//	**いつもの結果ダイアログに 1 行添えるだけ**にする。ここでアラートを 1 枚増やすと、
//	プローブを走らせるたびにクリックが 1 つ増える。
//
//	【落ちても拾う】プローブは VectorWorks ごと落とすことがある（落ち方そのものが知見に
//	なる調査があるので、それは異常ではない）。落ちれば投稿は走らないので、**走らせる前に
//	控えを置き、ログの行が届くたびにそこへ書き足す**。次にメニューを開いたときに残って
//	いれば、それを「結末不明」として投稿して控えを消す——**人の操作は 1 つも増えない。**
//
//	【ネットワークは持たない】投稿は同梱スクリプト（vw-probes-feedback.sh / .ps1）が行い、
//	トークンはキーチェーン（mac）／DPAPI で暗号化したファイル（Windows）に入る。C++ 側は
//	その機械可読な出力を読むだけ（自動アップデートと同じ分担。Update.h / BundledScript.h）。
//
//	【SDK 依存】実装（Feedback.cpp）はダイアログのために SDK を使う。純粋な部分——設定と
//	控えの読み書き、コメント本文の組み立て——は FeedbackParse.h にあり、単体テストが
//	押さえている（plugin/tests/FeedbackParseTests.cpp）。
//

#pragma once

#include "FeedbackParse.h"

#include <string>
#include <vector>

namespace vwprobe
{
	// 投稿の仕組みが**そもそも使えるか**（同梱スクリプトが置かれているか）。
	// 使えないときは、プローブはこれまでどおり走って結果ダイアログに出るだけである。
	bool FeedbackAvailable();

	// **走らせる前の下ごしらえ。** 投稿するつもりなら true。
	//   * pr が空で branch からも引けない  … false（宛先が無い）
	//   * 断られている                     … false（尋ねない）
	//   * まだ尋ねていない                 … ここで 1 度だけ尋ねる
	//   * トークンが無い                   … ここで 1 度だけ貼り付けてもらう
	// pr は**入力かつ出力**——空のまま渡すと、branch から引けた番号がここへ入る
	// （上記「宛先の決め方」2）。branch は動いているビルドのブランチ。
	// note には利用者へ伝えたいことが入る（呼び出し側が結果ダイアログへ添える）。
	bool PrepareFeedback(std::string& pr, const std::string& branch, std::string& note);

	// **走行中の控え**（落ちたときに次で拾うためのもの。Feedback.h 冒頭）。
	//   Arm    … 走らせる直前にファイルを作る（**投稿する周でだけ呼ぶ**）
	//   Append … ログの行が届くたびに書き足す（1 行ごとに flush する）
	//   Finish … 走り終えたら結末を書き込んで閉じる（投稿が失敗しても、次に開いた
	//            ときに**結末付きで**送り直せる）
	//   Disarm … 控えを消す（投稿できたとき・そもそも投稿しないとき）
	void ArmPendingRun(const feedback::Report& report);
	void AppendPendingLine(const std::string& line);
	void FinishPendingRun(const feedback::Report& report);
	void DisarmPendingRun();

	// **投稿する。** 結果ダイアログへ添える行を返す（投稿しないときは空）。
	// posted には**送れたか**が入る（一括実行のまとめが「N 件送った」と言うために要る
	// ——返す行は送れなかったときも 1 行あるので、行の有無では区別できない）。
	std::vector<std::string> PostReport(const feedback::Report& report, bool* posted = nullptr);

	// **前の走行の残りを拾って投稿する**（メニューを開いたときに 1 度）。拾ったものが
	// あれば、その旨をピッカーの脚注へ出すための行を返す。
	std::vector<std::string> PostLeftovers();

	// ピッカーの設定項目。投稿の入／切と、トークンの登録し直し。
	void RunFeedbackSettings();

	// いま（ローカル時刻。"2026-09-08 12:34:56"）。コメントと控えの「実行」欄に出る。
	std::string LocalTimestamp();
} // namespace vwprobe
