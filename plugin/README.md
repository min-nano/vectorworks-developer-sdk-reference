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

## 複数の PR のコマンドを 1 本のプラグインへ同居させる仕組み

実機確認は「PR をマージする**前**」に要る（CLAUDE.md「PR とマージ」2）。一方でビルドは
main のワークフローが行う。つまり**まだ open な複数の PR の調査コードを、1 本の
プラグインへ同居させて**配れなければならない。仕掛けは 3 つだけ。

1. **1 プローブ 1 ディレクトリ。** 調査コードは `probes/runtime/<slug>/probe.cpp` に置く。
   集約はディレクトリ単位で行い、`<slug>` がそのまま一意な鍵になる。

2. **プローブのシンボルはすべて内部リンケージ。** `VW_PROBE` マクロが展開するのは
   「本体関数 1 つ＋自己登録する静的オブジェクト 1 つ」で、どちらも `static`
   （[`src/payload/Probe.h`](src/payload/Probe.h)）。**別々の PR が同じ名前を使っていてもリンクで衝突
   しない**ので、ソースを 1 つのターゲットへ並べるだけで同居できる。登録は静的
   初期化で行われ、レジストリ側は関数ローカル static なので初期化順序にも依存しない
   （[`src/payload/Probe.cpp`](src/payload/Probe.cpp) 冒頭）。

3. **出所（どの PR の・どのコミットか）はコードに書かない。** それはビルドのときに
   しか決まらない（同じ調査が PR 中に何度も作り直され、マージ後は main のものになる）。
   [`scripts/gather-probes.sh`](../scripts/gather-probes.sh) が main と指定 PR から
   プローブを集め、「slug → PR / コミット / ブランチ / PR タイトル」の表を
   `ProbeProvenance.cpp` として生成し、CMake がそれをビルドへ入れる。ピッカーの 1 行は
   この表から作られる。

同じ slug がぶつかったときの扱い。**PR のブランチには main のプローブもそのまま載って
いる**ので、まず「中身が同じか」で〈引き継いだだけ〉と〈その PR が変えた〉を分ける
（これをしないと、2 つの PR を同居させた途端に、両方が持っている main のプローブが
「重複」として衝突してしまう）。

| 状況 | 扱い |
| --- | --- |
| 中身が同じ | **黙って飛ばす**（その PR は main の版を引き継いでいるだけ） |
| main と PR で中身が違う | **PR 側で上書き**（マージ前の版を確かめたいので）。ログに出す |
| PR どうしで中身が違う | **エラーで停止**。どちらかの slug を変えてから出し直す |
| ディレクトリ名と `VW_PROBE` の第 1 引数が違う | **エラーで停止**（出所と突き合わなくなるため） |

## 使い方（実機で確かめる人）

1. **ビルドを頼む。** Actions の "Probe plug-in" を `workflow_dispatch` で叩く。
   入力 `prs` に**確かめたい PR 番号をカンマ区切り**で入れる（例 `12,15`）。空なら
   main に入っているプローブだけでビルドする。
   - `workflow_dispatch` は**デフォルトブランチにあるワークフローしか起動できない**
     （GitHub の仕様）。このワークフロー自体を変える PR の間は使えないので、そのときは
     **PR の Actions 実行ページから成果物（`vwlibrary-zip` / `vlb-zip`）を直接
     ダウンロードする**（PR でもビルドはしている。公開だけしない）。
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
     `VwSdkProbesPayload.vwpayload` ＋ `VwSdkProbesPayload.build-info.txt`。
     ローカルディスクへ置き（iCloud Drive は不可）、隔離フラグを外す:
     `xattr -dr com.apple.quarantine VwSdkProbes.vwlibrary VwSdkProbesPayload.vwpayload`。
     CI ビルドは**アドホック署名済み**（Apple Silicon がロードするのに必須。Developer ID
     署名ではない）だが、「壊れている」と言われたら
     `codesign --force --deep --sign - VwSdkProbes.vwlibrary` と
     `codesign --force --sign - VwSdkProbesPayload.vwpayload` で署名し直す。
   - **Windows**: `VwSdkProbes.vlb` / `VwSdkProbes.vwr` /
     `VwSdkProbesPayload.vwpayload` を**同じフォルダへ一緒に**置く
     （`*.build-info.txt` は素性の控えで、自動アップデートが読む）。
   - **殻を入れ替えるときは Vectorworks を終了してから**（読み込み済みのモジュールは
     差し替えられない）。**本体だけなら動かしたままでよい**——それが下記の自動
     アップデートの通常の道。
   - 未署名なので、Vectorworks 2026 は起動時に「不明／未署名のプラグイン」警告を出して
     既定で無効にすることがある。了解して有効化する。
   - **初回はワークスペースへ追加する**: ツール ▸ ワークスペース ▸ 現在のワークスペースを
     編集 ▸ *メニュー*。**ツール** カテゴリの中に **SDK 実機プローブ…** があるので、
     メニューへドラッグする。
4. **走らせる。** メニュー「SDK 実機プローブ…」→ 一覧から選んで「実行」。
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
- PR を作ったら、Actions の "Probe plug-in" をその PR 番号付きで叩いてビルドしてもらう。
  PR 自体でも**コンパイルが通るかの確認**は自動で走る（公開はしない）。

## 殻と本体（＝入れ替えに再起動が要らない理由）

コンパイル済みプラグインは Vectorworks の**起動時にしか読み込まれず**、読み込み済みの
モジュールは差し替えられない。それだけだと、確かめたい PR を変えるたびに再起動が要る
——このプラグインは**入れ替えが日常**なので、それは重い。

そこで**2 つに割ってある**。

```
Vectorworks ──読み込む──▶ 殻（メニュー・ダイアログ・更新）      … 起動時に 1 度きり
   VwSdkProbes.vwlibrary      │ dlopen / LoadLibrary
   VwSdkProbes.vlb            ▼
                          本体（プローブ）                     … メニューを開くたびに読み直す
                          VwSdkProbesPayload.vwpayload
```

**Vectorworks は本体（`.vwpayload`）の存在を知らない。** 読み込むのは殻で、しかも
メニューコマンドが走るたびに読んで、終わったら降ろす。だから**本体のファイルを置き換えて
おけば、次にメニューを開いたときから新しいプローブが動く**（macOS で実測済み。
[Findings「プラグインモジュールの読み込みと入れ替え」](../Findings/Plug-in%20Modules.md)）。

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
- **本体はバンドルの中に置かない**（mac も殻の隣）。バンドルの署名はリソースまで封を
  するので、中のファイルを差し替えると署名が壊れる。
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
作業ツリーの `probes/runtime/*/` をそのまま拾う（出所は "local"）。特定の PR を混ぜて
ローカルで作るなら、先に集約してからマニフェストを渡す:

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
| `src/PayloadHost.{h,cpp}` | 殻の側（本体の読み込み・解決・アンロード・複製） |
| `src/PluginPrefix.h` | SDK アンブレラヘッダ（プリコンパイル対象） |
| `resources/VwSdkProbes.vwr/` | メニュー項目の表示名（UTF-16 の .vwstrings） |

プラグインの識別子は 1 か所（`src/BuildConfig.h` と `CMakeLists.txt`）にまとまっている:
バンドル名 `VwSdkProbes` / バンドル ID `io.github.min-nano.VwSdkProbes` /
VCOM ユニバーサル名 `CExtMenuVwSdkProbes` / 拡張機能 UUID
`40334bc6-d404-444f-bf47-8cad30f6e8c9`。

**実プラグイン（[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)）
とは別の識別子**なので、両方を同時に入れておける。
