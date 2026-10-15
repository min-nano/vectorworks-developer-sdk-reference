# 実機確認プラグイン（VwSdkProbes）

Findings に**無印（＝実機確認済み）**で書ける知見は、ローカルの VectorWorks でしか
取れない。そのために実機で走らせる道具がこのプラグインで、中身は

- **メニューコマンド 1 つ**（「SDK 実機プローブ…」）と、
- そこから**選んで走らせるプローブ**（PR ごとの調査コード）の束

だけでできている。プラグインを 1 度入れておけば、**確かめたい PR の調査コードだけを
差し替えたビルド**を配って上書きすれば済む——調査のたびにメニューやワークスペースを
いじる必要はない。

そして中身は**殻と本体の 2 つに割れている**。Vectorworks が起動時に読み込むのは殻
（メニュー・ダイアログ・更新）だけで、プローブは殻が自分で読み込む別のモジュールに
入っている。**だからプローブの入れ替えに Vectorworks の再起動が要らない**（下記
「殻と本体」）。

さらに**本体は群（main と、PR ごと）に分かれている**。殻はメニューを開いた時点では
**どれも読み込まず**、カタログ（索引のテキスト 1 枚）で一覧を出し、**選ばれた 1 本だけ**を
読む。こうしてあるので:

- **1 つの PR がコンパイルできなくても、他の PR のプローブは配れる・選べる。**
- 一覧を出すのに本体を読み込まないので、メニューを開くのが速い。
- 版の食い違いや読み込みの失敗も、**その 1 本の中に閉じる**。

```
メニュー「SDK 実機プローブ…」
   │
   ▼
 ピッカー（プルダウン 1 つ）
   ＊ 新しいプローブビルドを確認して入れ替える…
   ＊ 結果の自動投稿を設定…
   ＊ 一覧のプローブをすべて順に実行…
   #12 3f9a1c2 レイヤの重ね順を実測する [layer-order]
   #15 8b2d004 データタグの式を読み戻す [tag-formula]
   main c99966e スラブの立ち上がりを読む [slab-riser]
   │  ← 選んで「実行」
   ▼
 結果ダイアログ（見出し＋ログ全文。選択してコピーできる）
   ＋ 同じ内容が一時ディレクトリの VwSdkProbes-<slug>.log にも残る
   ＋ **そのプローブの出所（PR、無ければ issue）へ、結果が自動で投稿される**
     （下記「結果を PR / issue へ自動で返す」）
```

**一覧のプローブは、1 件ずつ選ばなくてもよい。** `＊ 一覧のプローブをすべて順に実行…`
を選べば上から順に全部走り、**結果は最後に 1 枚だけ**出る（下記
「一覧のプローブをすべて順に実行する」）。結果は 1 件ずつその場で投稿されるので、
**確かめたいものが何件あっても、実機でする操作は「入れ替える」と「走らせる」の 2 つの
まま**である。

**だから実機でする操作は 2 つだけになる**——「入れ替える」と「走らせる」。ログを選択して
コピーし、チャットへ貼る手間は要らない。

**ローカルの Claude Code からなら、その 2 つも Claude がする**（下記「ローカルの Claude Code
から操作する（MCP ブリッジ）」）。

**入れ替えは自動でできる。** ピッカーの先頭項目（`＊ 新しいプローブビルドを確認して
入れ替える…`）を選べば、プラグイン自身がダウンロードから差し替えまでを済ませる
（下記「自動アップデート」）。zip を落として展開して隔離フラグを外して……を毎回手でやる
必要は無い——このプラグインは**入れ替えが日常**なので、そこが手作業だと続かない。
**起動時には何も確認しない**（起動を待たせないため。確認するのは選んだときだけ）。

## 複数の PR のコマンドを 1 つのプラグインへ同居させる仕組み

実機確認は「PR をマージする**前**」に要る（CLAUDE.md「PR とマージ」2）。一方でビルドは
main のワークフローが行う。つまり**まだ open な複数の PR の調査コードを、1 つの
プラグインへ同居させて**配れなければならない。仕掛けは 4 つ。

1. **1 プローブ 1 ディレクトリ。** 調査コードは `probes/runtime/<slug>/probe.cpp` に置く。
   集約はディレクトリ単位で行い、`<slug>` が群の中での一意な鍵になる。

2. **本体は群ごとに 1 本。** main のプローブが 1 本、PR のプローブが PR ごとに 1 本の
   `.vwpayload` になる（`VwSdkProbesPayload-main.vwpayload` /
   `VwSdkProbesPayload-pr12.vwpayload`）。ワークフローは**本体を 1 本ずつビルドし、
   落ちた群だけを外して**公開する（[`scripts/build-payloads.sh`](../scripts/build-payloads.sh)）。
   **誰か 1 人のプローブがコンパイルできないだけで全員のビルドが落ちる**、という
   一番の詰まりがこれで無くなる。別々の PR が同じ slug を使っていても、別の本体に
   入るので衝突しない（ピッカーには出所付きで 2 行並ぶ——**それが見たい**）。

3. **カタログ（索引）で一覧を出す。** 「どの本体に何が入っているか」は
   `VwSdkProbes.probes.txt`（殻の隣のテキスト）に書いてある。殻はこれだけを読んで
   ピッカーを組み、**選ばれた群の本体だけ**を読み込む
   （[`src/PayloadCatalog.h`](src/PayloadCatalog.h) が読み手、
   [`cmake/ProbeCatalog.cmake`](cmake/ProbeCatalog.cmake) が書き手）。
   表示名と概要は `VW_PROBE` の引数から**ビルドのときに**読み出して載せる。

4. **出所（どの PR の・どのコミットか）はコードに書かない。** それはビルドのときに
   しか決まらない（同じ調査が PR 中に何度も作り直され、マージ後は main のものになる）。
   [`scripts/gather-probes.sh`](../scripts/gather-probes.sh) が main と指定 PR から
   プローブを群に分けて集め、群ごとに「slug → PR / コミット / ブランチ / PR タイトル」の
   表を `ProbeProvenance-<群>.cpp` として生成し、CMake がそれをその群の本体へ入れる。

**PR のブランチには main のプローブもそのまま載っている**ので、そのままだと同じものが
2 度出る。中身（ファイル名と内容の指紋）で分ける:

| 状況 | 扱い |
| --- | --- |
| main と中身が同じ | **PR の群に入れない**（その PR は main の版を引き継いでいるだけ） |
| main と中身が違う | **PR の群に入れる**（main の版と並んで出る。マージ前の版と見比べられる） |
| 別々の PR が同じ slug | **どちらも出る**（別の本体なので衝突しない。出所で見分ける） |
| ディレクトリ名と `VW_PROBE` の第 1 引数が違う | **エラーで停止**（出所と突き合わなくなるため） |
| その群のビルドが落ちた | **カタログには残り、ピッカーに「※本体なし」と出る**（黙って消さない） |

## 使い方（実機で確かめる人）

1. **ビルドを頼む——たいていは何もしなくてよい。** PR の `probes/runtime/` に
   プローブが push されると、[`probe-auto-update.yml`](../.github/workflows/probe-auto-update.yml)
   が **"Probe plug-in" を自動で叩き**（`prs=` に**その時点で open な PR を全部**）、
   完了まで見届けて PR へ「公開しました」と 1 つコメントする。つまり**プローブを書いて
   push した時点で、実機で走らせられるビルドが出来上がる**。
   - **open な PR は全部載る。** 本体が群ごとに分かれていて、コンパイルできない群だけ
     外れるので、他人の PR に巻き込まれない（入らなかった群はリリースノートの表と
     ピッカーの「※本体なし」で分かる）。
   - **main への push で走るビルドも同じ顔ぶれ**（main ＋ その時点で open な PR 全部）。
     以前はここだけが main に狭まっていて、**プローブと関係の無いマージ 1 つで
     open な PR の群がリリースから落ちていた**——転がりタグは 1 つなので、実機の
     ピッカーからもその PR のプローブが消える（[#79](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/79)）。
   - **手で叩くのは、載せる顔ぶれを絞りたいときだけ。** Actions の "Probe plug-in" を
     `workflow_dispatch` で叩き、入力 `prs` に PR 番号をカンマ区切りで入れる
     （例 `12,15`）。**空で叩くと main に入っているプローブだけになる**＝いま open な
     PR の群はそのリリースから外れるので、絞るつもりが無いなら空で叩かない
     （外れたままにはならない——下の点検が戻す）。
   - `workflow_dispatch` は**デフォルトブランチにあるワークフローしか起動できない**
     （GitHub の仕様）。このワークフロー自体を変える PR の間は使えないので、そのときは
     **PR の Actions 実行ページから成果物をダウンロードする**（PR でもビルドはしている。
     公開だけしない）。**どれを落とすかはプラットフォームで違う。**
     - **macOS は `vwlibrary-zip`。** 展開すると `VwSdkProbes.vwlibrary.zip` が出てくるので、
       それをもう一度展開して Plug-Ins へ置く（**展開は Finder ではなく `unzip -d` で**。
       下記「展開と設置で踏む入れ子 2 つ」）。**二度手間に見えるが、こちらが正しい道**
       ——**GitHub の成果物は 1 度 zip になり、その zip は実行権限を保たない**ので、素の
       ファイルで上げてある `VwSdkProbes-mac` を展開すると `.vwlibrary` の実行ビットが落ち、
       **`dlopen` が失敗して Vectorworks が「互換性のないプラグイン情報／コンパイルした
       バージョン: 不明」を出す**（版が合わないのではなく、**開けないので版を読めていない**）。
       `vwlibrary-zip` の中身は runner 上で `zip -qry` した zip なので、実行権限もアドホック
       署名もそのまま残っている——リリース資産と同じものである
       （[#184](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/184) で実測）。
     - **Windows は `VwSdkProbes-windows` でよい**（実行権限の概念が無いので、素のファイルの
       まま展開して置ける）。`vlb-zip` は同じ中身の zip 版。
     - **`VwSdkProbes-mac` を使ってしまったときは**、置いたあとに実行権限を戻し、隔離フラグを
       外して、署名し直す。

       ```sh
       chmod +x VwSdkProbes.vwlibrary/Contents/MacOS/VwSdkProbes
       xattr -dr com.apple.quarantine VwSdkProbes.vwlibrary VwSdkProbesPayload-*.vwpayload
       codesign --force --deep --sign - VwSdkProbes.vwlibrary
       for p in VwSdkProbesPayload-*.vwpayload; do codesign --force --sign - "$p"; done
       ```
   - 転がりタグ `probes` は**最後に公開したビルド**を指す。プローブを持つ PR が複数
     動いていると、後から push した方で置き換わる（何が入っているかはリリースノートの
     表とピッカーの出所欄で分かる）。
   - **PR が閉じたら点検が入る。** [`probe-release-guard.yml`](../.github/workflows/probe-release-guard.yml)
     が、リリースの隠しメタデータ `inputs=` と「いま open な PR」を突き合わせ、
     ずれていれば "Probe plug-in" を叩き直して**完了まで見届ける**。
     **「リリースに何が入っているべきか」は main のツリーだけでは決まらない**
     ——open な PR の集合という、コミットを 1 つも作らずに変わるものが効いているので、
     `paths` では引けない次元がある。調査に使ったプローブは**マージする前に消す**
     規約なので、プローブを持っていた PR がマージされる瞬間の差分には
     `probes/runtime/**` が 1 行も現れず、**消したプローブを含むビルドがリリースに
     残り続けていた**（[#76](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/76)）。
     そこを塞ぐのがこの点検で、ずれていなければ何もしない。
2. **リリースから落とす。** 公開先は転がり続けるタグ [`probes`]（プレリリース）。
   リリースノートに**そのビルドに入っているプローブの表**が必ず載っている。
   - macOS: `VwSdkProbes.vwlibrary.zip`
   - Windows: `VwSdkProbes.vlb.zip`
3. **入れる（初回だけ）。** 2 回目以降は手で入れ直さなくてよい（下記「自動アップデート」）
   ——ただし**入れ替えを始めるのは人**で、ピッカーの先頭項目を選んだときだけである
   （**起動時には確認しない**）。
   置き場所は Vectorworks 2026 の**ユーザフォルダ**内の `Plug-Ins`
   ディレクトリ（Vectorworks ▸ 環境設定 ▸ *ユーザフォルダ* から辿れる）。
   **zip の中身はすべて同じフォルダへ置く**——プラグインと本体（`.vwpayload`）は
   隣同士でなければならない（下記「殻と本体」）。
   - **macOS**: `VwSdkProbes.vwlibrary` バンドルと、その隣に
     `VwSdkProbesPayload-<群>.vwpayload`（**群の数だけある**）＋ カタログ
     `VwSdkProbes.probes.txt`。ローカルディスクへ置き（iCloud Drive は不可）、
     隔離フラグを外す:
     `xattr -dr com.apple.quarantine VwSdkProbes.vwlibrary VwSdkProbesPayload-*.vwpayload`。
     CI ビルドは**アドホック署名済み**（Apple Silicon がロードするのに必須。Developer ID
     署名ではない）だが、「壊れている」と言われたら
     `codesign --force --deep --sign - VwSdkProbes.vwlibrary` と
     `codesign --force --sign - VwSdkProbesPayload-<群>.vwpayload` で署名し直す。
   - **Windows**: `VwSdkProbes.vlb` / `VwSdkProbes.vwr` / `VwSdkProbes.probes.txt` /
     `VwSdkProbesPayload-<群>.vwpayload` / `vw-probes-*.ps1` を**同じフォルダへ一緒に**
     置く（`VwSdkProbes.build-info.txt` は殻の素性の控えで、自動アップデートが読む。
     `.ps1` は入れ替えと結果の投稿にプラグインが叩く同梱スクリプト）。
   - **カタログ（`VwSdkProbes.probes.txt`）を忘れない。** これが無いとピッカーが空に
     なる（殻はこれを見て一覧を出す）。
   - **展開と設置で踏む入れ子 2 つ**（[#184](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/184)
     で実際に踏んだ）。**置けた形は「3 種が Plug-Ins の直下に横に並ぶ」**で、
     `VwSdkProbes.vwlibrary` というフォルダの中にそれらが入っている状態は誤りである。

     ```
     VwSdkProbes.vwlibrary.zip の中身（＝ Plug-Ins へ置く形）
     ├── VwSdkProbes.vwlibrary/                 ← バンドル（中に Contents/）
     ├── VwSdkProbes.probes.txt                 ← カタログ
     └── VwSdkProbesPayload-<群>.vwpayload      ← 群の数だけ
     ```

     1. **Finder で開くと容れ物フォルダができる。** アーカイブ名が
        `VwSdkProbes.vwlibrary.zip` で、中に同名の `VwSdkProbes.vwlibrary` が入っている。
        アーカイブユーティリティは**複数項目のアーカイブをアーカイブ名のフォルダへ展開する**
        ので、`VwSdkProbes.vwlibrary/VwSdkProbes.vwlibrary/Contents/…` になる。外側は
        Finder が作った容れ物で、バンドルではない（`.vwlibrary` 拡張子が付くので
        バンドルに見えてしまう）。**`unzip -o <zip> -d <別名のフォルダ>` で展開する。**
     2. **`cp -R` は同名ディレクトリがあると中へ入れ子にする。** 既に入っている
        `$PLUGINS/VwSdkProbes.vwlibrary` へ `cp -R … VwSdkProbes.vwlibrary "$PLUGINS"/` を
        かけると `…/VwSdkProbes.vwlibrary/VwSdkProbes.vwlibrary` になる。
        **置く前に古いバンドルを `rm -rf` する。**

     ```sh
     PLUGINS="$HOME/Library/Application Support/Vectorworks/2026/Plug-Ins"
     unzip -o vwlibrary-zip.zip -d vwzip
     unzip -o vwzip/VwSdkProbes.vwlibrary.zip -d vwplugin
     ls -1 vwplugin                       # 3 種が横に並ぶことを確かめる
     rm -rf "$PLUGINS/VwSdkProbes.vwlibrary"
     cp -R vwplugin/* "$PLUGINS"/
     xattr -dr com.apple.quarantine "$PLUGINS/VwSdkProbes.vwlibrary" \
       "$PLUGINS"/VwSdkProbesPayload-*.vwpayload
     ls -1 "$PLUGINS" | grep VwSdkProbes  # Plug-Ins の直下に並んだことを確かめる
     ```
   - **殻を入れ替えるときは Vectorworks を終了してから**（読み込み済みのモジュールは
     差し替えられない）。**本体だけなら動かしたままでよい**——それが下記の自動
     アップデートの通常の道。
   - 未署名なので、Vectorworks 2026 は起動時に「不明／未署名のプラグイン」警告を出して
     既定で無効にすることがある。了解して有効化する。
   - **初回はワークスペースへ追加する**: ツール ▸ ワークスペース ▸ 現在のワークスペースを
     編集 ▸ *メニュー*。**ツール** カテゴリの中に **SDK 実機プローブ…** があるので、
     メニューへドラッグする。
4. **走らせる。** メニュー「SDK 実機プローブ…」→ 一覧から選んで「実行」。
   **選んだ時点でその群の本体が読み込まれる**（一覧を出すだけなら何も読み込まない）。
   - **新しいビルドを頼まれているなら、走らせる前に先頭項目
     `＊ 新しいプローブビルドを確認して入れ替える…` を選ぶ。** ここを飛ばすと
     **入っている古い版のプローブが走り、その結果が返る**——ピッカーの表示は同じなので、
     ビルド ID（結果の投稿に載る `build=`）を突き合わせるまで気付けない。
   - **全部まとめて確かめたいなら `＊ 一覧のプローブをすべて順に実行…`**（一覧の 3 番目）。
     上から順に全部走り、**結果は最後に 1 枚だけ**出る（下記
     「一覧のプローブをすべて順に実行する」）。投稿は 1 件ずつその場で行われる。
   - 行末に `※本体なし` と出ているものは、**その群のビルドが落ちている**（あるいは
     入れ替えが途中で止まっている）。選ぶと理由が出る。他の行はそのまま使える。
   - **プローブは図面を変更することがある。作業中の図面では実行しない**
     （新規の空図面で走らせる）。取り消しは保証しない——プローブは undo イベントを
     自分では開かない（[Findings「Undo」](../Findings/Undo.md) の半端な記録を避けるため）。
   - 結果ダイアログのログ欄は選択してコピーできる。同じ内容が一時ディレクトリの
     `VwSdkProbes-<slug>.log` にも 1 行ずつ書き出される（**VectorWorks ごと落ちても
     そこまでが残る**）。書き出し先は環境変数 `VW_PROBE_LOG` で差し替えられる。
5. **結果は自動で PR / issue へ返る。** 走らせ終えると、そのプローブの出所（PR、
   無ければプローブ本体の `[issue #N]`）に結果（結末・所要・出所・ログ全文）が
   コメントとして投稿される（下記「結果を PR / issue へ自動で返す」）。**初回だけ**
   「投稿してよいか」と GitHub のトークンを尋ねられるので、そこで 1 度だけ答える。
   あとは黙って投稿される。
   - 絵や所見（「レイヤの順は合っているが名前が化けている」等）は**チャットへ直接書く**。
     それは自動化で代われない唯一のもので、書くには実機を見ている必要があり、見ている
     ならチャットのほうが速い（実プラグイン側で一度作って外した経緯がある）。
6. **結果を Findings へ**（ここは Claude の仕事）。確かめられたことを `Findings/` の
   該当トピックへ書き、確認水準の印（無印＝実機確認済み）を正す。**役目を終えた
   プローブは消す**（結論は文章で残る。CLAUDE.md「調査のフロー」）。

## ローカルの Claude Code から操作する（MCP ブリッジ）

**Vectorworks が動いている Mac で Claude Code を起動すれば、Claude が自分でプローブを
入れ替え・走らせ・ログを受け取れる。** 人がするのは Vectorworks を起動しておくこと
（起動も Claude ができる）と、絵で確かめることだけになり、「ピッカー先頭から入れ替えて
走らせてください」の往復が無くなる。仕組みは実プラグイン
（[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
の「MCP ブリッジ」）と同じ。

```
Claude Code ──MCP(stdio)──▶ plugin/mcp/vw-mcp-server.py ──ファイル──▶ Vectorworks（殻の時計）
                              （リポジトリ直下の .mcp.json が登録）      一時ディレクトリの VwSdkProbes-mcp
```

> **実機での確認はまだ済んでいない。** 仕組み（OS のタイマーから受け付ける・新規の空図面を
> 開いて閉じる・再起動のヘルパー）はどれも実機で確かめた部品の組み合わせだが
> （[Findings「Timers and Notifications」](../Findings/Timers%20and%20Notifications.md)・
> [「Documents」](../Findings/Documents.md)・下記「自動アップデート」）、ブリッジとして
> 通したのは CI の代役（`tests/vw-mcp-server.test.py`）までである。

### 用意する（1 度だけ）

1. **ブリッジ入りのビルドを入れる。** 殻に入っているので、ブリッジより古いビルドが入って
   いるなら入れ替えて再起動する（ピッカー先頭の「入れ替える」でよい。プラグインが無い
   計算機なら、Claude の `vw_install` が入れる）。
2. **このリポジトリで Claude Code を起動する。** 直下の `.mcp.json` が
   `plugin/mcp/vw-mcp-server.py` を登録し、`.claude/settings.json` がそれを有効にする
   （初回だけ「このサーバを使ってよいか」と尋ねられる）。Python 3.8 以降が要る（macOS には
   最初から入っている。Windows で `python3` が無ければ `.mcp.json` の `command` を `py` に）。
3. **iPhone などから操作したいなら**、その Mac で `claude remote-control` を起動する
   （端末を閉じても残るよう `tmux` の中で。Mac はスリープさせない）。

**クラウドのセッション（claude.ai/code）からは使えない。** 受け渡しは同じ計算機の一時
ディレクトリのファイルなので、クラウドのコンテナからは届かない。橋をネットワークへ開く
（トンネル等）こともしない——相手が「同じ計算機の同じ利用者」であることを前提に、
頼まれれば図面を開き・再起動まで行う作りだからである。実機の要らない作業（Findings の
執筆・CI・`sdk-grep`）はクラウドで並行してよい。

### 道具

| 道具 | 誰が答えるか | 何をするか |
| --- | --- | --- |
| `vw_bridge_status` | サーバ | ブリッジが動いているか（動いていれば道具の一覧、動いていなければ直し方） |
| `vw_launch` | サーバ | Vectorworks を起動し、ブリッジが受け付けるまで待つ |
| `vw_install` | サーバ | 公開されているビルドを Plug-Ins へ入れる（**橋が架からないとき用**。同梱スクリプトの `q` → `do-install`。起動していれば `restart=true` で再起動） |
| `vw_call` | サーバ | 下の道具を名前で呼ぶ（Claude の道具の一覧が古いとき用） |
| `vw_lock_status` / `vw_lock_release` | サーバ | どのセッションが Vectorworks を占有しているか／片付けて占有を解く |
| `vw_ping` | プラグイン | 殻と本体の素性・開いている図面の数 |
| `vw_probes` | プラグイン | プローブの一覧（ピッカーと同じ並び。slug・群・出所・本体の有無） |
| `vw_run_probe` | プラグイン | 1 件走らせ、結末・所要・**ログ全文**を返す。既定で**新規の空図面を開いてから**走らせる。Vectorworks が居なければ起動してから |
| `vw_run_all` | プラグイン | 一覧を上から全部走らせる（ピッカーの「すべて順に実行」と同じ） |
| `vw_probe_log` | プラグイン（橋が落ちていればサーバ） | ログファイル `VwSdkProbes-<slug>.log` を読む |
| `vw_update` | プラグイン | 公開されている最新ビルドを入れる（**尋ねない**）。本体だけならそのまま効く。殻まで変わったら `restart_if_needed=true` で続けて再起動 |
| `vw_restart` | プラグイン | Vectorworks を再起動する（ブリッジが開いた図面は先に保存せずに閉じる） |
| `vw_cleanup` | プラグイン | ブリッジが開いた図面を閉じる（占有を解くときにサーバが呼ぶ） |

**道具の一覧の真実はプラグイン側の表**（`src/McpBridge.cpp` の `kTools`）で、サーバは起動時に
`vw_tools` で取りに行く。**道具を足すときに直すのはその 1 行と実装 1 つだけ**。

### 1 周の回し方（Claude がすること）

```
① プローブを書いて push → probe-auto-update が公開する（scripts/ci-wait.sh --pr <番号> で待つ）
② vw_update（outcome=installed なら再起動なしで効いている。needs_restart なら人に断ってから
   vw_restart か restart_if_needed=true）
③ vw_run_probe（probe=<slug>。同じ slug が複数の群にあれば group=pr<番号>）
④ 返ったログを読む → Findings へ（絵で確かめることがあれば、人に具体的に尋ねる）→ ① へ
⑤ 当分使わないなら vw_lock_release（ブリッジが開いた図面を閉じて占有を解く）
```

- **結果の投稿はメニューと同じ**（出所の PR / issue へ自動で投稿される）。ただし**ブリッジは
  何も尋ねない**ので、自動投稿をまだ設定していなければ投稿せずにその旨を返す（1 度メニューで
  設定すれば、以後はブリッジからも投稿する）。`post=false` で投稿しない。
- **図面。** `vw_run_probe` は既定で新規の空図面を開いてから走らせ、**走らせた後にその図面を
  一時ファイル**（一時ディレクトリの `VwSdkProbes-bridge-docs/`）**へ保存する**——未保存の
  変更を残すと、誰が頼んだ再起動でも（import-ifc のブリッジの `vw_restart` でも）保存の確認で
  止まるため。**次に走らせる前・`vw_cleanup`・再起動の前に閉じて、一時ファイルも消す**
  （人の図面には触れない——閉じるのは、開いた本人であることを fileRef とパスで確かめた
  ものだけ）。開いている図面で走らせたいときだけ `new_document=false`。
  - **この保存は取り消しの段を 1 つ積む**（[Findings「Undo」](../Findings/Undo.md)。#223）。
    1 回目で作り、`new_document=false` の 2 回目で `DoMenuName("Undo", 0)` するような
    **コマンドをまたいだ取り消し**を測るときは、1 段目で何も戻らないのが正常で、2 段目で
    1 回目の分が戻る。段を重ねて「消えるまで掛ける」形に書いておく。
- **プローブが Vectorworks ごと落としたら**、サーバは待ち切らずに（プロセスの消滅で気付く）
  **落ちるまでに書けたログ**を添えて返す（`crashed: true`）。結果の控えは次に走らせたとき
  （またはメニューを開いたとき）に「結末不明」として投稿される。起こし直して同じプローブを
  勝手に走らせ直すことはしない。
- **受け付けを見送るとき**: メニュー（ピッカー）が開いている間・undo の記録が開いている間・
  起動の直後 10 秒。要求はスプールに残り、終われば処理される。
- **Vectorworks を使えるのは 1 度に 1 つのセッションだけ。** 最初の操作で占有を取り、他の
  セッションの操作は断られる（`vw_lock_status` の `wait` で空くのを待てる）。最後の操作から
  60 分で解ける（CI の往復を待てる長さ）。
- `vw_restart` / `vw_install` / `vw_call` は `.claude/settings.json` で**確認を残してある**
  （再起動は保存の確認を出しうる——人に断ってから頼む）。

### import-ifc（実プラグイン）のブリッジと同居させる

同じ Vectorworks に import-ifc の開発版（`min-nano_structureDev`）が入っていれば、そちらの
ブリッジも同じ作法で動く。**取り違えないように、名前をすべて分けてある**:

| | このリポジトリ | import-ifc |
| --- | --- | --- |
| MCP サーバの登録名（道具は `mcp__<名前>__vw_*`） | `vwprobes` | `vectorworks` |
| スプール（一時ディレクトリの下） | `VwSdkProbes-mcp` | `min-nano_structureDev-mcp` |
| 環境変数 | `VWPROBES_MCP_*` | `VW_MCP_*` |

- **サーバは生存の印の `plugin` を確かめてから繋ぐ**（`VwSdkProbes` 以外は使わない）。
  受け渡しの版も道具の名前（`vw_update` / `vw_restart` …）も重なるので、場所を取り違えて
  繋がると別のプラグインを入れ替えてしまう。別のプラグインの印を見つけたら
  `vw_bridge_status` の `ignored` に出る。
- **占有（1 度に 1 セッション）はプラグインごとに別**で、両方のセッションが同じ
  Vectorworks を同時に使えてしまう。片方の `vw_restart` はもう片方の実行を途中で終わらせる
  ので、**両方を動かしているときは、再起動の前に相手のセッションが走らせていないかを人に
  確かめる**。

### 仕組みの置き場所

| ファイル | 役割 |
| --- | --- |
| `src/McpBridge.{h,cpp}` | 殻の時計（OS のタイマー）・道具の表と実装・ブリッジが開いた図面の管理 |
| `src/BridgeSpool.h` / `src/BridgeJson.h` / `src/BridgeParse.h` | **純粋な部分**（受け渡しの作法・最小の JSON・ログの詰め方・プローブの引き当て）。`tests/BridgeSpoolTests.cpp` |
| `mcp/vw-mcp-server.py` | Claude 側の MCP サーバ（標準ライブラリだけ）。`tests/vw-mcp-server.test.py` がプラグインの代役を立てて確かめる |
| `../.mcp.json` / `../.claude/settings.json` | サーバの登録と、道具の許可 |

**サーバを `plugin/scripts/` に置かない。** そこは殻の ID の材料（`scripts/gather-probes.sh`）で、
置けばサーバを直すたびに「殻が変わった」ことになって利用者に再起動を強いる（サーバは
プラグインに入らないのに）。**ブリッジは丸ごと殻にある**（実プラグインは受け付けを本体に
置いたので更新・再起動を殻へ頼み直す往復が要ったが、こちらの本体は走らせる間しか
読み込まれないので要らない）。**境界（`src/PayloadAbi.h`）は変えていない。**

## 一覧のプローブをすべて順に実行する

ピッカーの 3 番目 `＊ 一覧のプローブをすべて順に実行…` を選ぶと、**そのビルドに載って
いるプローブが、ピッカーに並んでいる順で全部走る**。

**なぜ要るか。** 結果は PR / issue へ自動で投稿される（下記「結果を PR / issue へ
自動で返す」）ので、実機でする操作は「走らせる」だけになった。だとすれば、確かめたい
ものを 1 件ずつ選び直す理由が無い——**ピッカーを開き直す回数が、そのまま検証の手間**
になる。open な PR が何本あっても、1 回で片が付く。

決めごとは 4 つ。

| | |
| --- | --- |
| **まとめは最後に 1 枚** | 1 件ごとに結果ダイアログを出したら、件数ぶんクリックが要る（それを無くすための項目である）。走り終えてから**まとめのダイアログを 1 枚だけ**出す。1 件ぶんは 1 行に畳み、**見出しと結末とログの全文は同じダイアログのログ欄**に、区切り（`===== 3/7 #12 [layer-order] =====`）を挟んで順に積む（選択してコピーできる） |
| **投稿は 1 件ずつその場で** | まとめて送らない。プローブごとに宛先（PR、無ければ issue）が違ううえ、**プローブは VectorWorks ごと落とすことがある**——1 件ずつ送っておけば、落ちるまでの結果は宛先に残る（落ちた 1 件も控えから次の起動で送られる。下記「落ちても拾う」）。**尋ねられるとしたら 1 件目の 1 度だけ**で、2 件目からは答えを覚えている |
| **走らせられない件は飛ばして続ける** | 本体が配られていない群（ピッカーの「※本体なし」）や、カタログと本体が食い違う件はそこで打ち切らずに、**理由を結末として控えて次へ進む**。まとめの「走らず N 件」がその数で、理由は各件の行とログに出る |
| **落ちたらそこで終わる** | 一括の途中でプローブが VectorWorks を落とせば、当然そこから先は走らない（まとめも出ない）。落ちるまでの結果は PR に残り、落ちた 1 件も次の起動で「結末不明」として送られるので、**残りをもう一度一括で走らせればよい**（済んだ件は同じ結果をもう 1 通投稿するだけ） |

まとめのダイアログはこんな形になる。

```
一括実行: 7 件中 成功 5 / 失敗 1 / 走らず 1
所要: 12.43 秒 / 投稿 6 件

1/7 #15 [tag-formula] 成功 (0.42 秒)
2/7 #15 [tag-anchor] 失敗: 式が空で返った (0.31 秒)
3/7 #12 [layer-order] この本体は入っていません
…
（ログ欄に 7 件ぶんの見出しとログ全文）
```

**プローブは図面を変更する。** 一括なら件数ぶん変更するので、**新規の空図面で走らせる**
（プローブは undo イベントを自分では開かない。CLAUDE.md「実機確認プラグイン」）。

## 結果を PR / issue へ自動で返す

プローブの答え（ログ）は必ず Claude へ渡って `Findings/` になる。その受け渡しを人が
——結果ダイアログを選択してコピーし、チャットへ貼って——やっている限り、1 往復ごとに
その手間が乗る。**だからプラグイン自身が、そのプローブの出所（PR、無ければ issue）へ
投稿する**（実プラグイン側の「実機フィードバックの往復」と同じ考え方）。

```
プローブを走らせる
   │
   ├─ 結果ダイアログ（いつもどおり。末尾に「投稿: <コメントの URL>」が 1 行増える）
   │
   └─ PR #12（無ければ issue #34）へコメント 1 通
        <!-- vw-probes-result v1 probe=layer-order group=pr12 pr=12 issue= build=… result=ok -->
        ## 実機プローブ: レイヤの重ね順を実測する `[layer-order]`
        | 結果 | 成功 |／| 所要 | 0.42 秒 |／| 出所 | PR #12 / 3f9a1c2 / … |
        ### ログ（全文）
```

守っている決めごとは 4 つ。

| | |
| --- | --- |
| **宛先を尋ねない** | 番号を人に打たせない。決め方は 3 段——①**プローブの出所（PR）**（ビルドのときに決まる。`scripts/gather-probes.sh`）、②出所に無く、**動いているビルドが main のものでもない**なら、そのブランチから open な PR を引く（下記「PR のビルドを手で入れて確かめるとき」）、③ PR がどちらでも決まらなければ、**プローブ本体の先頭コメントにある `[issue #N]`**（開いている issue に限る。閉じていれば投稿しない）。それでも決まらなければ投稿しない（issue 番号も書かれていないプローブ——実害は無い。詳しくは [`probes/runtime/README.md`](../probes/runtime/README.md)） |
| **尋ねるのは 1 度だけ** | 初めて宛先のあるプローブを走らせるとき、「投稿してよいか」と GitHub のトークンを尋ねる。答えは覚える。**断ったら二度と尋ねない**（気が変わったらピッカーの「＊ 結果の自動投稿を設定…」から入れ直す） |
| **走ったあとは何も尋ねない** | 投稿は黙って行い、結果（コメントの URL か失敗の理由）は結果ダイアログに 1 行添えるだけ。ここでアラートを増やすと、走らせるたびにクリックが 1 つ増える |
| **落ちても拾う** | プローブは VectorWorks ごと落とすことがある（落ち方そのものが知見になるので異常ではない）。走らせる前に控えを置き、ログの行が届くたびに書き足すので、**落ちても次にメニューを開いたときに「結末不明」として投稿される**。投稿だけ失敗した控えも、結末付きのまま次に送り直す（issue 宛てなら、送り直す前にもう一度開いているかを確かめる） |

### PR のビルドを手で入れて確かめるとき

公開ビルド（タグ `probes`）は **main のワークフローと main の殻**で作られ、PR からは
`probes/runtime/` だけを取り込む。つまり**この仕組み自体（`plugin/src/**`）を直す PR は、
公開ビルドでは自分の直したものを走らせられない**——確かめるには PR の Actions から
成果物を落として手で入れる（上記「使い方」1）。**macOS は `vwlibrary-zip`**（`VwSdkProbes-mac`
は GitHub の zip 化で実行権限が落ち、読み込めない）、**Windows は `VwSdkProbes-windows`**。

そのビルドではプローブが**群 main に入る**（PR のツリーをそのままビルドするため）ので、
出所に PR 番号が無い。それだけだと投稿先が決まらず、**投稿を直した PR が投稿を確かめ
られない**ので、そのときは**動いているビルドのブランチから open な PR を引く**
（同梱スクリプトの `find-pr`。トークンは要らない）。ブランチが `main` / `local` の
ときは引かない——公開ビルドで main のプローブを走らせるたびに GitHub を叩くことになるし、
そちらは宛先が無いのが正しい。

### トークン

必要な権限は**そのリポジトリの Pull requests への読み書き**だけ（fine-grained PAT なら
"Pull requests: Read and write"）。保存先は **macOS のキーチェーン** /
**Windows の DPAPI で暗号化したファイル**（`%LOCALAPPDATA%\VwSdkProbes\`）で、図面にも
ログにも残らない。開発機に `gh` が入っていれば、その認証を使うので登録は要らない。

貼り付けるとき以外、トークンが引数や出力に現れることは無い（プラグインは一時ファイル
経由でスクリプトへ渡し、スクリプトは読んだ直後にそのファイルを消す）。

### 仕組みの置き場所

| ファイル | 役割 |
| --- | --- |
| `src/FeedbackParse.h` | **純粋な部分**（設定・走行中の控え・コメント本文の組み立て）。`tests/FeedbackParseTests.cpp` が確かめる |
| `src/Feedback.{h,cpp}` | ダイアログ（1 度だけの確認とトークン）、控えの開け閉め、投稿の呼び出し |
| `scripts/vw-probes-feedback.{sh,ps1}` | 網とトークンの保管、宛先の引き当て（`find-pr`）と issue が開いているかの確認（`issue-state`）。**C++ は curl もトークンも触らない**。`tests/vw-probes-feedback.test.sh` |
| `src/BundledScript.{h,cpp}` | 同梱スクリプトの起動（自動アップデートと共有） |

**投稿しないとき**（設定が「投稿しない」・main のプローブ・同梱スクリプトが無い）は、
プローブはこれまでどおり走って結果ダイアログに出るだけである。

> **この仕組みが入る前のビルドから入れ替えた Windows の場合、1 回では届かないことがある。**
> 入れ替えを行うのは常に「いま入っている＝古い」版のスクリプトで、そちらは新しく足した
> `vw-probes-feedback.ps1` を知らない（macOS はバンドルごと置き換わるので起きない）。
> そのときは「結果の自動投稿は使えません（同梱スクリプトがありません）」と出るので、
> **もう一度「新しいプローブビルドを確認して入れ替える」を実行する**（以後は
> zip の直下にあるものをそのまま置くようになったので、二度と起きない）。

## 自動アップデート

**入れ替えはプラグイン自身が行う。** 入口は 1 つだけ:

| いつ | 何が起きるか |
| --- | --- |
| **メニューの先頭項目**（`＊ 新しいプローブビルドを確認して入れ替える…`） | 明示的に確認する。最新だったときも失敗したときも必ず結果を出す |

**起動時には確認しない。** 起動のたびにネットワークを叩けば、その待ちが Vectorworks の
起動に乗る。そして入れ替えたくなるのは「新しいプローブを走らせたい」ときであって、
起動した瞬間ではない——本体だけの入れ替えは再起動が要らないので、走らせようとした
その場で取り込めば足りる。

押すと、同梱スクリプトがリリースの zip を落として展開し、macOS なら隔離フラグを外して
アドホック署名をかけ直し、**いま読み込まれているバンドルの隣へ**差し替える（既定パスの
決め打ちではないので、ユーザフォルダを変えていても正しい場所に入る）。

**そこから先は、何が変わったかで分かれる。**

**落とす zip はどちらも同じ 1 つ**（殻＋本体）。違うのは、そこから何を置き換えるか。

| 変わったもの | 置き換えるもの | 締め |
| --- | --- | --- |
| プローブだけ（ふつうはこちら） | 本体（`.vwpayload`）だけ | **何も尋ねない。** メニューから入れ替えたときは**そのままピッカーへ戻る**（一覧を読み直すので、入れ替えたプローブがその場で選べる） |
| 殻まで | まるごと | 「再起動しますか？」——押せば Vectorworks が終了して自動で起動し直す（開いている書類は保存を確認する）。**ピッカーへは戻らない**（再起動するまで動いているのは古い殻なので、新しい本体と組み合わせない） |

zip を「まるごと用」と「本体だけ用」に分けていないのは、**人が手で入れるときの展開の手間を
増やさないため**。分けて節約できるのは数百 KB でしかない。

どちらになるかは押す前のダイアログに出る。判断は**殻の ID の一致**で行う（上記「殻と本体」）。

確認をメニュー（ピッカー）に置いてあるのは、**Vectorworks を動かしたままビルドを頼んだ
とき、その場で取り込める**ようにするため。入れ替えを選ぶのは、たいてい新しいプローブを
走らせたいからなので、**再起動が要らない道ならピッカーへ戻す**
（`src/Update.h` の `UpdateOutcome` を `src/ProbeMenu.cpp` が見て決める）。

### 取れなかったときは、**理由を名指しする**

更新の確認は 2 つの道を順に試す。

1. **GitHub の API**（`api.github.com/repos/…/releases/tags/probes`）。リリース名も
   資産の URL も本文の隠しメタデータも、ここで一度に取れる。
2. **決まった URL**（`https://github.com/<repo>/releases/download/probes/VwSdkProbes.release-info.txt`）。
   資産の URL はタグと名前から決まるので、**API を通さなくてもリリースの素性は読める**。
   中身は本文の隠しメタデータと同じ `key=value`（`scripts/probe-release-notes.sh --info`
   が本文と同じ 1 か所から作って、資産として置く）。

1 が駄目でも 2 で通るので、**API の呼び出し上限（未認証は 1 時間あたり 60 回）や、
`api.github.com` だけが塞がれている網でも入れ替えられる**。

両方駄目なときだけ「取得できませんでした」を出すが、**そこには理由を並べる**——
curl の終了コード（macOS）か WebException の種別（Windows）と HTTP のコード、そして
それが何を意味するか。

```
リリース（probes）を取得できませんでした。
GitHub API: HTTP 403: GitHub API の呼び出し上限に達しました（未認証は 1 時間あたり 60 回）。あと 12 分で戻ります
／直接取得: curl 終了コード 6（名前解決に失敗しました（DNS））: curl: (6) Could not resolve host: github.com
```

「ネットワークを確認してください」だけで終わらせない。**網は生きているのに落ちる**
（回数制限・プロキシ・社内フィルタ・証明書）ほうが多く、そのときに何も言わないと、
利用者にも直す側にも次の一手が無くなる。

### 新旧は「ビルド ID」で比べる（コミットではない）

このプラグインは**同じ main の sha から、同居させる PR を変えて何度もビルドされる**。
コミットで比べると「中身は別物なのに最新扱い」になって取りこぼすので、専用のビルド ID を
使う。

**ID は「何から作ったか」から計算する**（いつ作ったかではない）。材料はこれだけ:

```
main=<チェックアウトしたコミットの full sha>
pr=<番号>:<その PR の head の full sha>   … 同居させる PR のぶんだけ、番号順
```

これを並べた文字列のハッシュ（先頭 12 桁）が ID になる（`scripts/gather-probes.sh`）。
この決め方だと:

- **同じ顔ぶれで作り直しても ID は変わらない** → 中身が同じなら入れ替えを勧めない。
  実行ごとに変わる値（ワークフローの run id など）にすると、作り直すたびに誘ってしまう。
- **main が動けば ID が変わる** → プラグイン本体・CMake・ワークフローといった
  「ビルド設定」の変更もちゃんと拾う（それらは全部 main に乗っている）。
- **PR を force push しても head が動くので ID が変わる。**
- **引数の順序に依存しない**（`12,15` と `15,12` は同じ ID）。

| どこ | 何に入っているか |
| --- | --- |
| 公開されているビルド | リリース本文の隠しメタデータ `<!-- vw-probes … build=… -->`（ページには出ないが API の body には入る。`inputs=` に材料も入れてある）。**同じ行は資産 `VwSdkProbes.release-info.txt` にも入っている**（API が使えないときはそちらを読む） |
| 入っているビルド | macOS: バンドルの `Info.plist` の `VWBuildId` / Windows: `VwSdkProbes.build-info.txt` の `build=` |
| ビルドへ焼く値 | 集約が書いた `build-probes/manifest.cmake` の `VW_PROBE_BUILD_ID`（リリースへ書く値と**同じファイル由来**なので、ずれようがない） |

いま動いているビルドの ID は、ピッカーと結果ダイアログの「ビルド:」の行末（`id=…`）に
出ている。ローカルビルドは `local` なので、常に「新しいものがある」と出る（想定どおり
——手元のビルドを最新扱いするほうが危ない）。

### 仕組みの置き場所

| ファイル | 役割 |
| --- | --- |
| `src/Update.h` / `src/Update.cpp` | 流れとネイティブダイアログ、プラットフォーム依存の糊（自分の位置・スクリプト起動・再起動ヘルパー） |
| `src/UpdateParse.h` | **純粋な**部分（出力のパース・判断・クォート・パス・再起動コマンドの組み立て） |
| `tests/UpdateParseTests.cpp` | その単体テスト（SDK 不要。CI の lint で毎回走る） |
| `scripts/vw-probes-update.sh` / `.ps1` | ダウンロードと差し替えの実務。**非対話**で、`q` と `do-install <url>` の 2 モードだけ |
| `tests/vw-probes-update.test.sh` / `.test.ps1` | その単体テスト（source / ドットソースして curl だけ差し替える）。**取得の 2 つの道と、失敗したときの理由**を押さえる。CI の lint で毎回走る |

**再起動を自分でやらない**のには理由がある（起動中の Vectorworks は自分を畳めず、
古いプロセスが消える前に起動し直すとサポートファイルを読めずに落ちる）。切り離した
ヘルパーから OS の通常の終了要求を投げる形で、実プラグイン側で実機まで確かめてある
——簡略化しないこと（`src/UpdateParse.h` の「入れ替えたあとの再起動」）。

## プローブを 1 つ足す（調査する人）

[`probes/runtime/README.md`](../probes/runtime/README.md) に手順と作法がある。要点だけ:

- `probes/runtime/<slug>/probe.cpp` を作り、`VW_PROBE` の第 1 引数を**ディレクトリ名と
  同じ slug**にする（雛形は [`probes/runtime/README.md`](../probes/runtime/README.md)）。
- 使える API は `probe.log(...)` と `probe.fail(...)` の 2 つだけ（[`src/payload/Probe.h`](src/payload/Probe.h)）。
- PR を作れば**ビルドと公開は自動**（`probe-auto-update.yml` が "Probe plug-in" を
  叩き、完了まで待つ）。**その待機がそのまま PR のチェック**で、**自分の群の本体が
  コンパイルできなければ、その PR だけが赤くなる**（他の PR のビルドは止めない）。
  実機で走らせるのは、リリースが更新されたあと（次にそのプローブを選んだときから
  新しい本体で動く）。

## 殻と本体（＝入れ替えに再起動が要らない理由）

コンパイル済みプラグインは Vectorworks の**起動時にしか読み込まれず**、読み込み済みの
モジュールは差し替えられない。それだけだと、確かめたい PR を変えるたびに再起動が要る
——このプラグインは**入れ替えが日常**なので、それは重い。

そこで**2 つに割ってある**。

```
Vectorworks ──読み込む──▶ 殻（メニュー・ダイアログ・更新）      … 起動時に 1 度きり
   VwSdkProbes.vwlibrary      │
   VwSdkProbes.vlb            ├─ 読む ─▶ カタログ VwSdkProbes.probes.txt  … メニューを開くたび
                              │            （どの本体に何が入っているかの索引）
                              │
                              └─ dlopen / LoadLibrary ─▶ 本体（プローブ）  … **選ばれた 1 本だけ**
                                   VwSdkProbesPayload-main.vwpayload
                                   VwSdkProbesPayload-pr12.vwpayload
                                   VwSdkProbesPayload-pr15.vwpayload
```

**Vectorworks は本体（`.vwpayload`）の存在を知らない。** 読み込むのは殻で、しかも
**ダイアログで選ばれてから**読んで、終わったら降ろす。だから**本体のファイルを置き換えて
おけば、次にそのプローブを選んだときから新しいものが動く**（macOS で実測済み。
[Findings「プラグインモジュールの読み込みと入れ替え」](../Findings/Plug-in%20Modules.md)）。

**本体が群ごとに分かれているので、失敗はその 1 本の中に閉じる。** ビルドできなかった群は
配られず、ピッカーに「※本体なし」と出るだけ。版が違う・壊れているといった読み込みの
失敗も、選んだその 1 本の話で終わる。

| どこを変えたか | 何が要るか |
| --- | --- |
| プローブ（`probes/runtime/**`） | **入れ替えるだけ。再起動は要らない** |
| 殻（`plugin/src/**` ほか） | まるごと入れ替えて**再起動**（境界が変わっているかもしれないため） |

見分けるのは**殻の ID**（`shell=`）。リリースと、入っているビルドの両方に入っていて、
一致していれば本体だけを落として置き換える（[`src/UpdateParse.h`](src/UpdateParse.h) の
`Evaluate`）。判断できないときは必ず「まるごと＋再起動」へ倒す——**再起動を惜しんで版の
食い違ったまま動かすほうが危ない**（境界が変わっていれば本体は読み込み時に弾かれ、
プローブが 1 つも動かなくなる）。

### 決めごと（触るときに壊しやすいところ）

- **境界（[`src/PayloadAbi.h`](src/PayloadAbi.h)）は C。** 例外・C++ のオブジェクト・
  vtable を跨がせない。本体は降ろされるので、殻に残ってよいのは「値を写したもの」だけ
  （返る `const char*` はその場で `std::string` へ写す）。形を変えたら
  `VW_PAYLOAD_ABI_VERSION` を上げる。
- **本体は必ず複製してから読む。** 一時ディレクトリへ世代ごとの名前で写し、その複製を
  読む（[`src/PayloadHost.h`](src/PayloadHost.h)）。Windows は読み込み中の DLL を
  置き換えられないので、直接読むと**入れ替えられなくなる**＝この仕組みが死ぬ。
- **カタログと本体は同じビルドから出す。** 索引（`VwSdkProbes.probes.txt`）だけ新しい・
  本体だけ古い、が起こると「選んだプローブがその本体に無い」になる。入れ替えは
  **本体 → カタログの順**に置き（途中で落ちても索引が先走らない）、殻は走らせる前に
  「選んだ id がその本体にあるか」を確かめてから呼ぶ。
- **群の名前はファイル名になる**（`VwSdkProbesPayload-<群>.vwpayload`）。書き手
  （[`cmake/ProbeCatalog.cmake`](cmake/ProbeCatalog.cmake)）と読み手
  （[`src/PayloadHost.h`](src/PayloadHost.h) の `payload::FileNameFor`）で綴りが
  ずれると、実機では「※本体なし」としか出ない。単体テストで両側から押さえてある。
- **本体はバンドルの中に置かない**（mac も殻の隣）。バンドルの署名はリソースまで封を
  するので、中のファイルを差し替えると署名が壊れる。
- **殻の記憶域を本体に持たせない。** 境界を越えて渡した構造体（`VwPayloadHost`）は
  **受け取った側がその場で写す**（[`src/PayloadHostHolder.h`](src/PayloadHostHolder.h)）。
  ここを落として**実機で Vectorworks ごと落とした**——殻がそれを `load()` のローカルに
  置いていたので、戻った時点で番地が使い回され、プローブの最初の 1 行で「ログの受け口」
  としてスタックのゴミを呼んだ。**コンパイルもリンクも CI のビルドも通る壊れ方**なので、
  写しているかだけは単体テストで押さえてある（`tests/PayloadHostHolderTests.cpp`）。
- **SDK をリンクするモジュールは `plugin_module_ver()` と `DefaultPluginVWRIdentifier()` を
  定義する。** 本体も libVWSDK.a を自分でリンクするので必要（Findings に理由と実際の
  リンクエラー）。

## ローカルでビルドする

```bash
# macOS
cmake -S plugin -B build -DVW_SDK_DIR=/path/to/2026-NNA-eng-mac-SDK
cmake --build build --config Release

# Windows
cmake -S plugin -B build -A x64 -DVW_SDK_DIR=C:/path/to/2026-NNA-eng-win-SDK
cmake --build build --config Release
```

`VW_SDK_DIR` は `SDKLib` を**含む**フォルダ。マニフェストを渡さないローカルビルドでは、
作業ツリーの `probes/runtime/*/` を群 `main` としてそのまま拾う（出所は "local"）。
特定の PR を混ぜてローカルで作るなら、先に集約してからマニフェストを渡す:

```bash
scripts/gather-probes.sh --prs 12,15
cmake -S plugin -B build -DVW_SDK_DIR=... \
  -DVW_PROBE_MANIFEST="$PWD/build-probes/manifest.cmake"
```

## ソースの構成

| ファイル | 中身 |
| --- | --- |
| `src/payload/Probe.h` | **プローブが使う唯一のヘッダ**。`VW_PROBE` マクロと `Report`（ログ・失敗） |
| `src/payload/Probe.cpp` | レジストリ（登録・一覧・出所の引き当て）とログファイル |
| `src/payload/PayloadMain.cpp` | **本体の入口。** 自分で `GS_InitializeVCOM` を呼び、プローブを数えて走らせる |
| `src/ProbeMenu.{h,cpp}` | メニューコマンド・ピッカー・結果ダイアログ |
| `src/ModuleMain.cpp` | モジュールの入口（拡張機能の登録） |
| `src/BuildConfig.h` | プラグインの識別子とビルドスタンプ |
| `src/payload/ProbeProvenance.cpp.in` | 出所表の雛形（中身は CMake が生成する） |
| `src/Update.{h,cpp}` | 自動アップデート（メニューからの確認、入れ替え、再起動） |
| `src/UpdateParse.h` | その純粋な部分（`tests/UpdateParseTests.cpp` が確かめる） |
| `src/Feedback.{h,cpp}` | **結果の PR / issue への自動投稿**（上記「結果を PR / issue へ自動で返す」） |
| `src/FeedbackParse.h` | その純粋な部分（`tests/FeedbackParseTests.cpp` が確かめる） |
| `src/BundledScript.{h,cpp}` | 同梱スクリプトの起動（更新と投稿が共有する 1 本の道） |
| `src/Alerts.h` | ネイティブなアラート 2 種（知らせる／尋ねる）の薄い包み |
| `scripts/vw-probes-update.{sh,ps1}` | 同梱の更新スクリプト（プラグインが非対話で叩く） |
| `scripts/vw-probes-feedback.{sh,ps1}` | 同梱の投稿スクリプト（同上。トークンもこちらが持つ） |
| `src/PayloadAbi.h` | **殻と本体の間の C の ABI**（下記「殻と本体」） |
| `src/McpBridge.{h,cpp}` | **MCP ブリッジ**（ローカルの Claude Code からの操作。上記「ローカルの Claude Code から操作する」） |
| `src/BridgeSpool.h` / `src/BridgeJson.h` / `src/BridgeParse.h` | その純粋な部分（`tests/BridgeSpoolTests.cpp` が確かめる） |
| `mcp/vw-mcp-server.py` | その Claude 側（MCP サーバ。`tests/vw-mcp-server.test.py`） |
| `src/PayloadCatalog.h` | **カタログの読み手**（どの本体に何が入っているか。`tests/PayloadCatalogTests.cpp`） |
| `cmake/ProbeCatalog.cmake` | その書き手（`tests/probe-catalog-test.cmake` が `cmake -P` で確かめる） |
| `src/PayloadHost.{h,cpp}` | 殻の側（本体の読み込み・解決・アンロード・複製） |
| `src/PayloadHostHolder.h` | 本体の側。**殻から渡されたものを写して持つ**（`tests/PayloadHostHolderTests.cpp` が確かめる） |
| `src/PluginPrefix.h` | SDK アンブレラヘッダ（プリコンパイル対象） |
| `resources/VwSdkProbes.vwr/` | メニュー項目の表示名（UTF-16 の .vwstrings） |

プラグインの識別子は 1 か所（`src/BuildConfig.h` と `CMakeLists.txt`）にまとまっている:
バンドル名 `VwSdkProbes` / バンドル ID `io.github.min-nano.VwSdkProbes` /
VCOM ユニバーサル名 `CExtMenuVwSdkProbes` / 拡張機能 UUID
`40334bc6-d404-444f-bf47-8cad30f6e8c9`。

**実プラグイン（[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)）
とは別の識別子**なので、両方を同時に入れておける。

## 拡張機能を一時的に足して調べる（[issue #183](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/183) の例）

ふつうこのプラグインが登録する拡張機能は**メニューコマンド 1 つだけ**だが、
**拡張機能でなければ調べられないことがある**。例が「`Recalculate` の中から何が見えるか」
——`Recalculate` に自分のコードを立てられるのは**自前の PIO の中だけ**で、その登録は
モジュールの読み込みのときに起きるため、入れ替えできる本体（`.vwpayload`）には置けない。

issue #183 ではそのために**殻へ調査用の PIO を 1 つ登録し、調査が済んだら外した**
（結論は [Findings「`Recalculate` の中から見える環境」](../Findings/Parametric%20Objects.md)）。
同じことをするときの形:

- **殻（`plugin/src/**`）に置き、`ModuleMain.cpp` で登録する。** したがって
  **公開ビルド（転がりタグ `probes`）では確かめられない**——公開ビルドの殻は main の
  ものなので、PR で足した拡張機能は入っていない。上記
  「[PR のビルドを手で入れて確かめるとき](#pr-のビルドを手で入れて確かめるとき)」の
  手順で PR の成果物を手で入れる。
- **プローブとの受け渡しはファイルで行う。** 境界（`src/PayloadAbi.h`）は殻 → 本体の
  一方向しか持たず、しかも利用者が OIP を編集した瞬間には本体が読み込まれてさえいない。
  プロセスにも読み込み状態にも依らないもので渡すこと（1 行ごとに開いて閉じれば、
  Vectorworks ごと落ちてもそこまでが残る）。
- **プローブはその殻のヘッダを include できない。** 公開ビルドは「main の殻 ＋ 各 PR の
  `probes/runtime/` だけ」で組まれるので、同じ PR で足したヘッダは向こうに無い
  （[`probes/runtime/README.md`](../probes/runtime/README.md) の「決まり」）。約束事は
  プローブ側へ書き写して自己完結させる。
- **入れ替え忘れを機械で弾く。** プローブの先頭で `gSDK->DefineCustomObject(名前, …)` を
  呼び、nil なら「この殻には入っていない」と言って止める。これが無いと、公開ビルドの殻で
  走らせた結果を「実測」と誤読しかねない。
- **常駐させない。** 済んだら殻・登録・`CMakeLists.txt` のソース・`.vwr` の文字列・
  プローブを一緒に落とす。

[issue #185](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/185)
でも同じことをした（`Recalculate` の中で行列を読む口が何を返すか。結論は
[Findings「PIO の行列を読む口」](../Findings/Parametric%20Objects.md)）。**どちらも外して
あるので、いま殻が登録するのはメニューコマンド 1 つだけ**である。#185 で分かった作法を
足しておく。

- **測った値を「`Recalculate` の外から読んだ真値」と突き合わせる行を必ず 1 本入れる。**
  中と中（文脈 A と文脈 B）だけを比べると、**両方が同じように化けていても「同じ」と出る**
  ——#183 の表がその形だった（被験体が原点・無回転だったため）。
- **被験体は「化けたら値が変わる」ように置く。** 原点・無回転・`90°` の倍数は、
  単位行列や軸の取り違えと見分けが付かない。#185 は `(5000, 3000)`・`30°` に置いた。
- **書き溜めの 1 行は `鍵 ## 値` で書く。** #183 は「最初の `:` で切る」形にしていて、
  `:` を含まない行で鍵が値ごと切り出され、**同じ項目を「片方だけ」と数えた**。値に絶対
  現れない区切りを決めておけば、突き合わせの読み違えは起きない。
- **`理由=` のようなマルチバイトの綴りを「文字数」で数えない。** `substr(at + 9)` は
  UTF-8 では 2 バイト余計に食う。長さは `std::string("理由=").size()` で採る。
- **門のメッセージには「入れ替え先の見分け方」まで書く。** #185 では
  `DefineCustomObject` が nil で止める門は効いていたのに、**ピッカー先頭の入れ替え**と
  **リリース（タグ `probes`）の zip** を試して**同じ失敗を 2 回踏んだ**——どちらも殻は
  main のものなので、この形の調査では入らない。「PR の成果物を手で入れて」だけでは足りず、
  **その 2 つでは入らないこと**と、**いま何が入っているかの見分け方**（結果に載る
  「ビルドの素性」の `殻:` の行。`main` のままなら入れ替え先が違う——id が新しくなって
  いても同じ）を言うこと。
