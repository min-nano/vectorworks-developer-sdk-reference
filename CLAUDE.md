# CLAUDE.md

このファイルは Claude Code（claude.ai/code）がこのリポジトリで作業するときの指針です。
**この指示は既定の挙動より優先されます。正確に従ってください。**

## このリポジトリについて

Vectorworks 公式の SDK リファレンス（[Vectorworks/developer-sdk](https://github.com/Vectorworks/developer-sdk)）の
フォークに、**実測知見（`Findings/`）と調査用 CI** を足したものです。公式リファレンスは
実際の開発には情報が足りないため、実機（ローカルの VectorWorks）と CI（SDK の検索・
コンパイル確認）で調査した結果をここへ蓄積し、**プラグイン開発（現在は
[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)）
から参照する唯一の置き場**にします。

| 場所 | 中身 | 書き換え |
| --- | --- | --- |
| `README.md` | 公式リファレンスの入口＋このフォークの追加分への導線 | 追加分の節だけ触る |
| `Info/` / `Versions/` | **上流（公式）由来のリファレンス** | **原則書き換えない**（上流の更新を取り込めるように保つ。誤りを見つけたら Findings 側に注記を書く） |
| `Findings/` | **実測知見**。トピック別のファイル＋[索引と規約](Findings/README.md) | ここが本体。知見はここへ足す |
| `plugin/` | **実機確認プラグイン**（VwSdkProbes）。メニュー 1 つから、複数の PR の調査コードを同居させて実機で走らせる（[説明](plugin/README.md)） | 仕組みを変えるときだけ |
| `probes/` | 調査用のコンパイルスニペット（[規約](probes/README.md)） | 調査中だけ。役目を終えたら消す |
| `probes/runtime/` | **実機で走らせる調査（プローブ）**。1 調査 1 ディレクトリ（[規約](probes/runtime/README.md)） | 調査中だけ。役目を終えたら消す |
| `scripts/` / `.github/workflows/` | 調査用 CI（`ci-debug`）と待機スクリプト・lint・上流の取り込み（`upstream-sync`） | — |
| `CLAUDE.md`（本ファイル） | 作業時の規約。調査のフロー・PR とマージ・CI の待ち方 | — |

## 調査のフロー

調査は issue 単位で回す。**調べたいことが出たら issue を立て、このリポジトリで調査して
PR にし、必要な実機確認を経て Findings へ確定内容を反映する。**

1. **issue を立てる**（テンプレート `調査` がある）。何を・なぜ・どの水準まで確かめるか。
2. **既に答えが無いか確かめる。** `Findings/` の該当トピックと「打ち切った調査」を先に
   読む。**打ち切った調査に書いてあることは再調査しない**（状況——VW のバージョン・SDK の
   版——が変わらない限り）。
3. **作業ブランチで調査する。** 問いの水準で道具が決まる:
   - 「この API は SDK にあるか」「宣言はどうなっているか」「**なぜこの関数は失敗するか**」
     → `ci-debug` の `sdk-grep` / `sdk-ls`（下記「CI デバッグ」）。**ヘッダだけでなく
     `SDKLib/Source` の VWFC 実装 `.cpp` も検索範囲に入っている**ので、理由はたいてい
     そこに書いてある。
   - 「この呼び出しはコンパイルが通るか」→ `probes/` にスニペットを置いて `compile`。
   - **「呼んだら何が起きるか」→ `probes/runtime/<slug>/probe.cpp` にプローブを書き、
     実機確認プラグインで実機の VectorWorks で走らせる**（下記「実機確認プラグイン」）。
     ヘッダと構文チェックで答えが出る問いを、わざわざ実機へ持っていかない。
4. **Findings へ反映する PR を作る。** 確認水準の印（無印＝実機確認済み・【推定】・
   【ヘッダ根拠】）を正直に付ける（[`Findings/README.md`](Findings/README.md)）。
   調査に使ったスニペット・一時計装は PR をマージする前に消す（結論は文章で残る）。
   PR 本文に `Closes #<issue 番号>` を書いて issue と紐付ける——**プラグイン側はその
   issue が閉じる（＝Findings に反映される）のを待って実装に入る**運用なので、調査は
   issue 単位で確実に閉じること。
5. **実機確認が要るものは確認後にマージする**（下記「PR とマージ」）。

## 知見の書き方

- **なぜ（根拠）と実測値を書く。** 「動かない」だけでなく、何を試してどう切り分けたかを
  残す。表・実測の数値・ダンプは省略しない。
- **確認水準を偽らない。** 実機で確かめていないことを確認済みのように書かない。
  【推定】【ヘッダ根拠】の印は、実機確認が取れた時点で外す。
- **「できない」も知見。** 打ち切った調査は消さずに、潰した道と教訓ごと残す。
- **実装例へのリンクは実プラグインのソースを指す**（`draw/DrawUtil` 等のパス表記）。
  こちらへコードを複製しない。
- 日本語で書く。既存ファイルの密度・体裁（手折りの箇条書き）に合わせる。

## 上流（フォーク元）の取り込み

`Info/` と `Versions/` を上流由来のまま保つには、上流
（[Vectorworks/developer-sdk](https://github.com/Vectorworks/developer-sdk)）の更新を
流し込み続ける必要がある。これは **`.github/workflows/upstream-sync.yml`（週 1 回 +
手動）が自動でやる**——上流に新しいコミットがあれば `upstream-sync` ブランチへ merge し、
取り込み PR を作る（既に開いていれば同じ PR を更新する）。実体は
[`scripts/upstream-sync.sh`](scripts/upstream-sync.sh)。

- **取り込み PR のレビューでは `Info/` `Versions/` を書き換えない。** 上流の内容を
  そのまま入れる。誤りを見つけたら `Findings/` 側に注記を書く。
- **競合したときは PR が draft（タイトルに `[競合あり]`）で立ち、run は失敗する。**
  競合マーカーを含んだままコミットしてあるので、**そのままマージしないこと**。
  手で解消して push すると、そのブランチは以後自動更新の対象から外れる（bot 以外の
  コミットがあるブランチには触らない作りなので、解消内容が force push で消えることはない）。
- この PR は `GITHUB_TOKEN` で作られるので **lint の CI は自動では走らない**（GitHub の
  仕様）。走らせたいときは close → reopen する。
- 手動で回したいとき・挙動を確かめたいときは Actions の "Upstream sync" を
  `workflow_dispatch` で叩く（`dry_run` を立てると push も PR 作成もせず、何をするかだけ出す）。

## 開発プロセス: PR とマージ

1. **PR は自動で作ってよい。** 作成後は `subscribe_pr_activity` で CI 結果とレビューを
   監視し、CI の失敗は診断して修正を push する（待機は必ず `ci-wait`。下記）。
2. **「実機確認済み」として書く内容の PR は、確認が済んでからマージする。** 実機でしか
   確かめられない挙動を無印（＝実機確認済み）で Findings に書く PR は、ユーザーが
   VectorWorks 実機で確認し「確認できた」と伝えるまで open のまま待つ。未確認の記述が
   確認済みの顔をして蓄積すると、リファレンス全体の信頼が崩れる。
3. **実機確認の要らない変更は CI green で自動マージしてよい。** 【推定】【ヘッダ根拠】の
   印付きの追記、`sdk-grep` の結果の転記、目次・体裁・スクリプト・CI 設定など。
   判断に迷うなら 2 に倒す。
4. **`plugin/` を触る PR は、実機で動いたと聞いてからマージする。** CI が確かめられるのは
   「コンパイルとリンクが通る」ことまでで、**ダイアログが出るか・プローブが走るか・
   結果が読めるかは実機でしか分からない**（実プラグイン側の規約と同じ理由）。ビルドを
   ディスパッチして、ユーザーが入れて動かし「確認できた」と伝えるまで open で待つ。
   プローブを足すだけの PR（`probes/runtime/` だけの変更）はこれに当たらない——そちらは
   2 の対象（プローブで確かめた結果を Findings に書く PR）として扱う。
5. **コミットメッセージ**には Claude セッション URL を入れる
   （`https://claude.ai/code/session_<SESSION_ID>` の形式）。

## 実機確認プラグイン（`plugin/` — 実機で走らせて確かめる）

「呼んだら何が起きるか」はヘッダにも構文チェックにも答えが無い。それを実機で確かめる
ための小さなプラグインが `plugin/`（VwSdkProbes）で、**メニューコマンド 1 つ**から
**プローブ**（PR ごとの調査コード）を選んで走らせる。仕組みと使い方の全体像は
[`plugin/README.md`](plugin/README.md)。ここには作業時の規約だけを書く。

- **調査コードは `probes/runtime/<slug>/probe.cpp` に置く。** 1 調査 1 ディレクトリ・
  1 ファイル 1 `VW_PROBE`。**slug（ディレクトリ名）と `VW_PROBE` の第 1 引数は
  必ず一致させる**（集約がそこで突き合わせる）。作法は
  [`probes/runtime/README.md`](probes/runtime/README.md)。
- **PR 番号・コミットをコードに書かない。** 出所はビルドのときに決まり、
  `scripts/gather-probes.sh` が表を生成する。だから**同じプローブを PR 中に何度
  作り直しても、マージ後に main のものになっても、コードは変えなくてよい**。
- **ビルドは main のワークフロー**（Actions の "Probe plug-in"）。`workflow_dispatch` の
  入力 `prs` に**確かめたい PR 番号をカンマ区切り**で入れると、その PR たちのプローブが
  main のものと同居した 1 本のプラグインになる。公開先は転がりタグ `probes`
  （プレリリース）で、**リリースノートに何が入っているかの表が必ず載る**。
- **PR では公開せず、ビルドが通るかだけを見る**（ビルドに入るもの——`plugin/src/**` /
  `plugin/resources/**` / `plugin/CMakeLists.txt` / `probes/runtime/**`——を触った PR で
  自動的に走る）。「ディスパッチしたらコンパイルエラーだった」を防ぐ門。
- **実機で走らせるのはユーザー。** AI はビルドをディスパッチし、リリースができたことを
  伝えるところまで。結果（ログ）を受け取ってから `Findings/` へ反映する。
- **入れ替えは自動で、ふつうは Vectorworks の再起動も要らない。** プラグインは
  「殻（VW が起動時に読み込む）」と「本体（殻が自分で読み込む `.vwpayload`）」に割れて
  いて、**プローブは本体に入っている**。プローブだけの入れ替えなら本体を置き換えるだけで
  済み、次にメニューを開いたときから新しいものが動く（`plugin/README.md`「殻と本体」）。
  再起動を尋ねるのは `plugin/src/**` など**殻**が変わったときだけ。だからディスパッチの
  後は「リリースができた」と伝えればよく、zip の場所を毎回案内しなくてよい。**新旧はコミットではなくビルド ID で比べる**
  ——同じ sha から、同居させる PR を変えて何度もビルドされるため。ID は「main のコミット＋
  各 PR の head」から計算するので、**同じ顔ぶれで作り直しても変わらない**
  （`scripts/gather-probes.sh` / `plugin/src/UpdateParse.h`）。
- **役目を終えたプローブは消す**（結論は `Findings/` に文章で残る。上記「調査のフロー」4）。
  `probes/runtime/example/` だけは雛形かつ煙試験として残す。
- **プローブは undo イベントを自分では開かない**（[Findings「Undo」](Findings/Undo.md) の
  半端な記録を避けるため）。図面が戻らない前提で、新規の空図面で走らせる。

## CI の完了を待つ（待機は必ず `ci-wait` / `ci-debug` で行う）

リモートセッションから「CI が終わった」ことを知る手段は、**完了した瞬間に exit する
プロセスをバックグラウンドで走らせる**ことだけである（PR 購読で配信されるのは CI の
**失敗**とコメントだけで、**成功は配信されない**）。バックグラウンドコマンドの終了は
ハーネスが通知するので、**exit がそのまま完了通知になる**。

したがって次の 2 つは**禁止**する。

- **`sleep` で待つ**（完了時刻の予測が要るうえ、外れれば無駄待ちか取りこぼし）。
- **待機ループをその場で手書きする**（`while : ; do gh/curl …; sleep 30; done`）。
  締切もウォッチドッグも HTTP の時間上限も無いので、API が固まればぶら下がる。

```
Bash(run_in_background: true):
  scripts/ci-wait.sh --pr 12        # PR の head（新しい push が入ったら追随する）
  scripts/ci-wait.sh --ref main     # ブランチ / タグ
  scripts/ci-wait.sh                # いま checkout しているブランチ
```

投げたら別作業を続け、終了通知が来たら出力ファイルを `Read` するだけでよい。出力の
最終行は必ず `ci-wait: done (conclusion=<結果> exit=<終了コード>)` で、
`conclusion=success` 以外は exit 1（`no-checks` は「チェックが 1 件も登録されなかった」、
`timed-out-waiting` / `api-error` は「待機側が見届けられなかった」——CI の失敗ではない）。
このリポジトリの PR CI は `lint.yml`（shellcheck / actionlint / clang-format / 無 SDK の
単体テスト）と、
`plugin/src/**` や `probes/runtime/**`（ビルドに入るもの）を触ったときだけ走る
`probe-build.yml`（mac / Windows の実ビルド）。出力に並ぶチェック名を読み、`debug`（ci-debug の run）だけを見て green と
誤読しないこと。

待機の土台は `scripts/ci-common.sh`（`ci-wait.sh` と `ci-debug.sh` が共有）。
**どんな異常でも必ず有限時間で exit する**ことが唯一にして最大の要件で、HTTP の時間上限・
締切判定・ウォッチドッグの三重の歯止めを持つ。新しく「何かの完了を待つ」道具が要るときは
`poll_until` の上に probe を 1 つ書く。**待機ループを増やさない。**

## CI デバッグ（SDK 調査は `ci-debug` を使う）

リモートセッション（クラウド上のコンテナ）には **Vectorworks SDK が無い**。SDK に関する
問いは `.github/workflows/ci-debug.yml`（`workflow_dispatch` 専用。push / PR では決して
走らない）で CI 上に答えさせる。

### 使い方（リモートセッションの AI はこの 2 手順）

リモートセッションの `GITHUB_TOKEN` は読み取り専用で `actions: write` を持たない
（REST でのディスパッチは 403）。**起動は GitHub MCP、待機はスクリプト**の 2 手順で行う。

```
1. mcp__github__actions_run_trigger
     method: run_workflow, workflow_id: "ci-debug.yml", ref: <調査したいブランチ>,
     inputs: {mode, platform, label, args, script, notify_pr}
     ※ label は一意な文字列にする（これで run を特定する）

2. Bash(run_in_background: true):
     scripts/ci-debug.sh wait --label <label>
```

**手順 2 は必ず `run_in_background: true` で投げる**（完了した瞬間に exit する＝通知に
なる）。**`sleep` で待ってはいけない。** 書き込み権限のあるトークンが使える環境では
`scripts/ci-debug.sh run --mode sdk-grep --args 'GetLayerByName'` が 1 コマンドで両方やる。

| mode | 用途 | `--args` |
| --- | --- | --- |
| `sdk-grep` | SDK（`SDKLib` のヘッダ＋同梱の実装ソース）を拡張正規表現で検索（**調査の主力**） | 検索パターン |
| `sdk-ls` | ファイルの全文表示 / パス部分一致の一覧 | `SDKLib` からの相対パス（`Include/…` は省略可）または部分文字列 |
| `compile` | `probes/` のスニペット 1 ファイルを SDK ヘッダに対して構文チェック（mac 専用。リンクはしない） | ソースのパス（例 `probes/example.cpp`） |
| `shell` | 任意の bash（`--script`）。逃げ道 | — |

`--platform` は `mac`（既定）/ `windows` / `linux`。`windows` は Windows 版 SDK ヘッダを
引きたいときだけ。`linux` は SDK を用意しないので `shell` 専用。

**SDK は VWFC の実装ソースを同梱している。** `SDKLib/Source/VWSDK/VWFC/…` に
`VWFC::VWUI` / `VWFC::Tools` の `.cpp` が丸ごと入っており、`sdk-grep` / `sdk-ls` の
検索範囲はそこを含む。「ヘッダには宣言しか無いので理由が分からない」で打ち切る前に、
必ず実装を読むこと（実例: `VWImagePopupCtrl::CreateControl` が `return false` の
スタブだと分かり、実機での条件出しが不要になった）。

**`compile` が通っても「動く」とは限らない**——構文チェック（`-fsyntax-only`）であって、
リンクも実行もしていない。実挙動の確認は実機（または実プラグイン側のビルド）で行い、
Findings に書くときの確認水準の印もそれに合わせる。

### 結果の読み方

出力は必ず次のマーカーで挟まれている。`truncated=yes` のときは**全部は見えていない**ので、
`--args` を絞るか `mode=shell` で件数を数える。

```
===== BEGIN PAYLOAD (mode=... platform=...) =====
...
===== END PAYLOAD (exit=N lines_total=N truncated=yes|no) =====
```

`... (annotation truncated by GitHub's 4096-char limit …)` が出ていたら注釈経路の上限で
切られたということ（END の `lines_total` が本当の行数）。ペイロードの取得経路は
① チェックラン注釈（api.github.com だけで読める。通常はこちら）② ジョブログ（署名付き
ストレージへの 302 が egress ポリシーで拒否される環境では取れない。そのときは GitHub MCP の
`get_job_logs`）。失敗して調査コマンドに到達しなかった場合はマーカーが無く、理由が出る。
全文ログは run のアーティファクト（`ci-debug-<label>`）に残るが、**AI はアーティファクトを
取得できない**ので、必要な情報は必ずログ側に出すこと（モードを足すときの原則）。

**注釈経路の 4096 文字（おおむね 100 行）が実質の読める上限**なので、長いファイルは
`sdk-ls` で丸ごと出しても頭しか読めない。`mode=shell` で `sed -n '<開始>,<終了>p'` と
範囲を切るか、`sdk-grep` を絞る。なお `mode=shell` と `compile` の出力は**ダイジェスト
（診断行＋末尾 80 行）**なので、`shell` で読みたいものは 80 行以内に収めること。

### 制約

- `workflow_dispatch` は**デフォルトブランチに存在するワークフロー**しか起動できない。
  `ci-debug.yml` が `main` にマージされて初めて、作業ブランチを `ref` に指定して使える。
- **モードの追加・修正は `scripts/ci-debug-job.sh`（ランナー側）で行う。** ワークフロー
  本体は薄く保ってあるので、作業ブランチに push するだけで新しいモードを試せる
  （`ref` がそのブランチのため）。ワークフロー本体を変えると main へのマージが要る。
- SDK（ヘッダ＋実装ソース）はキャッシュされる（初回だけ〜140MB のダウンロードが走る）。

## 関連リポジトリ

- [vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
  — 実測知見の出どころで、Findings の「実装例」が指す先。あちらの開発で SDK の新しい
  挙動が分かったときは、**あちらの開発メモではなくこのリポジトリの `Findings/` へ足す**
  （あちらの CLAUDE.md にも同じ規約がある）。
