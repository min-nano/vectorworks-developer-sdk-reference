# 実機確認プラグイン（VwSdkProbes）

Findings に**無印（＝実機確認済み）**で書ける知見は、ローカルの VectorWorks でしか
取れない。そのために実機で走らせる道具がこのプラグインで、中身は

- **メニューコマンド 1 つ**（「SDK 実機プローブ…」）と、
- そこから**選んで走らせるプローブ**（PR ごとの調査コード）の束

だけでできている。プラグインを 1 度入れておけば、**確かめたい PR の調査コードだけを
差し替えたビルド**を配って上書きすれば済む——調査のたびにメニューやワークスペースを
いじる必要はない。

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

## 複数の PR のコマンドを 1 本のプラグインへ同居させる仕組み

実機確認は「PR をマージする**前**」に要る（CLAUDE.md「PR とマージ」2）。一方でビルドは
main のワークフローが行う。つまり**まだ open な複数の PR の調査コードを、1 本の
プラグインへ同居させて**配れなければならない。仕掛けは 3 つだけ。

1. **1 プローブ 1 ディレクトリ。** 調査コードは `probes/runtime/<slug>/probe.cpp` に置く。
   集約はディレクトリ単位で行い、`<slug>` がそのまま一意な鍵になる。

2. **プローブのシンボルはすべて内部リンケージ。** `VW_PROBE` マクロが展開するのは
   「本体関数 1 つ＋自己登録する静的オブジェクト 1 つ」で、どちらも `static`
   （[`src/Probe.h`](src/Probe.h)）。**別々の PR が同じ名前を使っていてもリンクで衝突
   しない**ので、ソースを 1 つのターゲットへ並べるだけで同居できる。登録は静的
   初期化で行われ、レジストリ側は関数ローカル static なので初期化順序にも依存しない
   （[`src/Probe.cpp`](src/Probe.cpp) 冒頭）。

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
3. **入れる。** 展開して Vectorworks の Plug-ins フォルダへ置き、Vectorworks を起動し
   直す（初回はワークスペース編集でメニューへ追加する）。**入れ替えるときは
   Vectorworks を終了してから**——読み込み済みのモジュールは差し替えられない。
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

## プローブを 1 つ足す（調査する人）

[`probes/runtime/README.md`](../probes/runtime/README.md) に手順と作法がある。要点だけ:

- `probes/runtime/example/` を写して `probes/runtime/<slug>/` にし、`VW_PROBE` の
  第 1 引数を**ディレクトリ名と同じ slug**にする。
- 使える API は `probe.log(...)` と `probe.fail(...)` の 2 つだけ（[`src/Probe.h`](src/Probe.h)）。
- PR を作ったら、Actions の "Probe plug-in" をその PR 番号付きで叩いてビルドしてもらう。
  PR 自体でも**コンパイルが通るかの確認**は自動で走る（公開はしない）。

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
| `src/Probe.h` | **プローブが使う唯一のヘッダ**。`VW_PROBE` マクロと `Report`（ログ・失敗） |
| `src/Probe.cpp` | レジストリ（登録・一覧・出所の引き当て）とログファイル |
| `src/ProbeMenu.{h,cpp}` | メニューコマンド・ピッカー・結果ダイアログ |
| `src/ModuleMain.cpp` | モジュールの入口（拡張機能の登録） |
| `src/BuildConfig.h` | プラグインの識別子とビルドスタンプ |
| `src/ProbeProvenance.cpp.in` | 出所表の雛形（中身は CMake が生成する） |
| `src/PluginPrefix.h` | SDK アンブレラヘッダ（プリコンパイル対象） |
| `resources/VwSdkProbes.vwr/` | メニュー項目の表示名（UTF-16 の .vwstrings） |

プラグインの識別子は 1 か所（`src/BuildConfig.h` と `CMakeLists.txt`）にまとまっている:
バンドル名 `VwSdkProbes` / バンドル ID `io.github.min-nano.VwSdkProbes` /
VCOM ユニバーサル名 `CExtMenuVwSdkProbes` / 拡張機能 UUID
`40334bc6-d404-444f-bf47-8cad30f6e8c9`。

**実プラグイン（[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)）
とは別の識別子**なので、両方を同時に入れておける。
