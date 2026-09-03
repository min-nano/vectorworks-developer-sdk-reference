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
   #12 3f9a1c2  レイヤの重ね順を実測する      [layer-order]
   #15 8b2d004  データタグの式を読み戻す      [tag-formula]
   main c99966e 煙試験: 図面のレイヤを数える  [example]
   │  ← 選んで「実行」
   ▼
 結果ダイアログ（見出し＋ログ全文。選択してコピーできる）
   ＋ 同じ内容が一時ディレクトリの VwSdkProbes-<slug>.log にも残る
```

**入れ替えは自動でできる。** 新しいビルドが公開されていれば、Vectorworks の起動時に
プラグイン自身が「入れ替えますか？」と尋ね、押すだけでダウンロードから差し替えまで済む
（下記「自動アップデート」）。zip を落として展開して隔離フラグを外して……を毎回手でやる
必要は無い——このプラグインは**入れ替えが日常**なので、そこが手作業だと続かない。

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
   - **手で叩くのは、載せる顔ぶれを絞りたいときだけ。** Actions の "Probe plug-in" を
     `workflow_dispatch` で叩き、入力 `prs` に PR 番号をカンマ区切りで入れる
     （例 `12,15`）。空なら main に入っているプローブだけ。
   - `workflow_dispatch` は**デフォルトブランチにあるワークフローしか起動できない**
     （GitHub の仕様）。このワークフロー自体を変える PR の間は使えないので、そのときは
     **PR の Actions 実行ページから成果物 `VwSdkProbes-mac` / `VwSdkProbes-windows` を
     ダウンロードする**（PR でもビルドはしている。公開だけしない）。この 2 つは
     **展開したらそのまま Plug-Ins へ置ける**——中に zip を作っていない（GitHub の成果物
     自体が 1 度 zip になるので、その中がさらに zip だと展開と置き直しが二度手間になる）。
     - 成果物の zip は**実行権限を保たないことがある**。macOS で読み込まれないときは
       `chmod +x VwSdkProbes.vwlibrary/Contents/MacOS/VwSdkProbes` を足してから、下記の
       隔離フラグ外しと署名し直しを行う。
     - `vwlibrary-zip` / `vlb-zip` はリリース資産（＝自動アップデータが落とす zip）を
       作るための内部用で、人が落とすものではない。
   - 転がりタグ `probes` は**最後に公開したビルド**を指す。プローブを持つ PR が複数
     動いていると、後から push した方で置き換わる（何が入っているかはリリースノートの
     表とピッカーの出所欄で分かる）。
2. **リリースから落とす。** 公開先は転がり続けるタグ [`probes`]（プレリリース）。
   リリースノートに**そのビルドに入っているプローブの表**が必ず載っている。
   - macOS: `VwSdkProbes.vwlibrary.zip`
   - Windows: `VwSdkProbes.vlb.zip`
3. **入れる（初回だけ）。** 2 回目以降は自動アップデートに任せる（下記）。
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
     `VwSdkProbesPayload-<群>.vwpayload` を**同じフォルダへ一緒に**置く
     （`VwSdkProbes.build-info.txt` は殻の素性の控えで、自動アップデートが読む）。
   - **カタログ（`VwSdkProbes.probes.txt`）を忘れない。** これが無いとピッカーが空に
     なる（殻はこれを見て一覧を出す）。
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
   - 行末に `※本体なし` と出ているものは、**その群のビルドが落ちている**（あるいは
     入れ替えが途中で止まっている）。選ぶと理由が出る。他の行はそのまま使える。
   - **プローブは図面を変更することがある。作業中の図面では実行しない**
     （新規の空図面で走らせる）。取り消しは保証しない——プローブは undo イベントを
     自分では開かない（[Findings「Undo」](../Findings/Undo.md) の半端な記録を避けるため）。
   - 結果ダイアログのログ欄は選択してコピーできる。同じ内容が一時ディレクトリの
     `VwSdkProbes-<slug>.log` にも 1 行ずつ書き出される（**VectorWorks ごと落ちても
     そこまでが残る**）。書き出し先は環境変数 `VW_PROBE_LOG` で差し替えられる。
5. **結果を Findings へ。** 確かめられたことを `Findings/` の該当トピックへ書き、
   確認水準の印（無印＝実機確認済み）を正す。**役目を終えたプローブは消す**
   （結論は文章で残る。CLAUDE.md「調査のフロー」）。

## 自動アップデート

**入れ替えはプラグイン自身が行う。** 入口は 2 つ:

| いつ | 何が起きるか |
| --- | --- |
| **Vectorworks の起動時** | 公開されているビルドが今のと違えば「入れ替えますか？」と尋ねる。同じなら**無言**。オフラインでも無言（起動は止めない） |
| **メニューの先頭項目**（`＊ 新しいプローブビルドを確認して入れ替える…`） | 明示的に確認する。最新だったときも失敗したときも必ず結果を出す |

押すと、同梱スクリプトがリリースの zip を落として展開し、macOS なら隔離フラグを外して
アドホック署名をかけ直し、**いま読み込まれているバンドルの隣へ**差し替える（既定パスの
決め打ちではないので、ユーザフォルダを変えていても正しい場所に入る）。

**そこから先は、何が変わったかで分かれる。**

**落とす zip はどちらも同じ 1 つ**（殻＋本体）。違うのは、そこから何を置き換えるか。

| 変わったもの | 置き換えるもの | 締め |
| --- | --- | --- |
| プローブだけ（ふつうはこちら） | 本体（`.vwpayload`）だけ | **何も尋ねない。** 次にメニューを開いたときから新しいプローブが動く |
| 殻まで | まるごと | 「再起動しますか？」——押せば Vectorworks が終了して自動で起動し直す（開いている書類は保存を確認する） |

zip を「まるごと用」と「本体だけ用」に分けていないのは、**人が手で入れるときの展開の手間を
増やさないため**。分けて節約できるのは数百 KB でしかない。

どちらになるかは押す前のダイアログに出る。判断は**殻の ID の一致**で行う（上記「殻と本体」）。

起動時だけでなくメニューからも確認できるようにしてあるのは、**Vectorworks を動かしたまま
ビルドを頼んだとき、その場で取り込める**ようにするため。

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
| 公開されているビルド | リリース本文の隠しメタデータ `<!-- vw-probes … build=… -->`（ページには出ないが API の body には入る。`inputs=` に材料も入れてある） |
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

**再起動を自分でやらない**のには理由がある（起動中の Vectorworks は自分を畳めず、
古いプロセスが消える前に起動し直すとサポートファイルを読めずに落ちる）。切り離した
ヘルパーから OS の通常の終了要求を投げる形で、実プラグイン側で実機まで確かめてある
——簡略化しないこと（`src/UpdateParse.h` の「入れ替えたあとの再起動」）。

## プローブを 1 つ足す（調査する人）

[`probes/runtime/README.md`](../probes/runtime/README.md) に手順と作法がある。要点だけ:

- `probes/runtime/example/` を写して `probes/runtime/<slug>/` にし、`VW_PROBE` の
  第 1 引数を**ディレクトリ名と同じ slug**にする。
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
| `src/Update.{h,cpp}` | 自動アップデート（起動時・メニューからの確認、入れ替え、再起動） |
| `src/UpdateParse.h` | その純粋な部分（`tests/UpdateParseTests.cpp` が確かめる） |
| `scripts/vw-probes-update.{sh,ps1}` | 同梱の更新スクリプト（プラグインが非対話で叩く） |
| `src/PayloadAbi.h` | **殻と本体の間の C の ABI**（下記「殻と本体」） |
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
