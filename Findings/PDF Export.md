# PDF の書き出し（`IExportPDF`）

シートレイヤを、ダイアログを出さずにプラグインから PDF へ書き出す話
（[issue #332](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/332)）。
VW 2026 の macOS（Apple Silicon）で確認。手順・ログ・比べた結果は
[計測記録](../Metrics/PDF%20Export.md)。

どれも実機確認プラグインのブリッジの受け付け（OS タイマーの刻み。
[Timers and Notifications](Timers%20and%20Notifications.md)）から呼んだ結果である。

## 結論

| 問い | 答え |
| --- | --- |
| ダイアログを出さずに書き出せるか | **できる。** `VectorWorks::Filing::IExportPDF` の `OpenPDFDocument` → `ExportPDFPages` → `ClosePDFDocument` で、どの段でもダイアログは出なかった |
| どのシートを書き出すかをどう指定するか | **`ExportPDFPages(name)` にシートレイヤの名前を渡す。** 現在のレイヤに依らず、名前のシートが出る。空の名前は現在のレイヤを出す |
| 1 つの PDF に何枚入れられるか | **開いてから閉じるまでに `ExportPDFPages` を呼んだ数だけ**、呼んだ順に頁が並ぶ（12 枚で確かめた） |
| GUI の「PDF の取り出し」・パブリッシュと同じものが出るか | **同じ。** 同じ計算機・同じ設定で、GUI の「PDF の取り出し」（1 枚）・パブリッシュ（12 枚）の PDF と画素単位で一致し、取り出した文字も同じだった。別の計算機で作った PDF とは書体の分だけ違う（下記「出力」） |
| パブリッシュのセットは使えるか | **使えない（作れない）。** `IPublish` にあるのはセットの一覧と書き出しだけで、セットを作る・項目を足す・設定を変える口は無い【ヘッダ根拠】 |
| ISDK の `OpenPDFDocument(path)` の並びは使えるか | **使えない。** 設定のダイアログ（`AcquireExportPDFSettingsAndLocation`）を先に出していないと `OpenPDFDocument` が `false` を返し、ファイルもできない |
| 前面でない図面のシートを書き出せるか | **前面にしてからなら書き出せる。** 前面の図面から後ろの図面のシートの名前を渡すと、無い名前と同じく失敗する |
| 書き終わるのはいつか | **`ClosePDFDocument` から戻った時点。** 戻った直後と 2 秒後でファイルの大きさと更新時刻は変わらなかった |
| 図面に跡が残るか | **残らない。** undo の記録は開かれず、現在のレイヤも書き出す前のまま |

## 呼び方

```cpp
#include "Interfaces/VectorWorks/Filing/IExportPDF.h"
using namespace VectorWorks::Filing;

IExportPDFPtr pdf(IID_ExportPDF);
SExportPDFOptions saved;
pdf->GetOptions(saved);

SExportPDFOptions opts = saved;
opts.fOpenInViewer = false;              // 保存される設定。既定がオンのことがある
opts.fExportRangeKind = SExportPDFOptions::eWholeAreaAsOnePage;
pdf->SetOptions(opts);

IFileIdentifierPtr file(IID_FileIdentifier);
file->Set(TXString(pathUtf8));
if (VCOM_SUCCEEDED(pdf->OpenPDFDocument(file)))
{
    for (const TXString& sheet : sheetNames)
        pdf->ExportPDFPages(sheet);      // 失敗（無い名前）は 1 を返す
    pdf->ClosePDFDocument();             // ここで書き終わる
}
pdf->SetOptions(saved);                  // 人の設定を戻す
// 戻り値だけで成否を決めない。ファイルがあるか・頁の数を確かめる（下記「失敗の見え方」）
```

- 対象の図面は前面にしておく（`SwitchToOpenFile`）。書き出した後に元の前面へ戻す。
  シートレイヤの名前の列挙は [Layers and Stories](Layers%20and%20Stories.md)、図面の切り替えは
  [Documents](Documents.md)。

## 設定（`SExportPDFOptions`）

- **`GetOptions` / `SetOptions` は読み戻せる**（`fResolution` を変えて読み戻すと変わった値が返る）
  （[実測](../Metrics/PDF%20Export.md#設定の読み書き)）。
- **既定値**: `fStandardFormat=ePDF`・`fResolution=300`・`fSubsetFonts=true`・
  `fExportRangeKind=eWholeAreaAsOnePage`・`fUpdateViewports` / `fResetPluginObjects` /
  `fRecalculateWorksheets` は `true`。GUI の「PDF の取り出し」の既定と同じ並びである。
- **`fOpenInViewer` は始めの値を当てにしない。** 同じ計算機でも、起動し直すと既定の値が
  `true` から `false` に変わっていた。書き出すたびに `false` にし、終わったら戻す。
- **`fExportRangeKind=eCurrentView` は画面の大きさの頁になる**（711×540 pt。用紙でない）。
  シートを用紙の大きさで出すには `eWholeAreaAsOnePage`（既定）。`eAllPages` も 1 頁のシートでは同じ。
- **GUI のダイアログにあって構造体に無い項目がある**（「リンクされているオブジェクトを PDF
  ハイパーリンクとして取り出す」など）。SDK からは変えられない【ヘッダ根拠】。

## 出力

- **頁の大きさはシートの用紙の大きさ**（`ovLayerSheetPaperWidth` / `Height` を pt にしたもの。
  A4 縦 595×842・A3 横 1191×842）。
- 所要（A3 の構造図 12 枚）: 1 枚の `ExportPDFPages` は 0.4〜1.1 秒、12 枚を 1 つにすると
  `ClosePDFDocument` に 5〜10 秒、合計 12〜17 秒。**起動後の最初の書き出しは 6〜10 秒かかる**
  （小さなシートでも）。どれもダイアログは出ていない（戻るまで人の手は入っていない）。
- **埋め込む書体は、書き出す計算機に入っている書体で決まる。** 書体の揃い方が違う計算機で
  作った PDF とは、見た目がわずかに違い、文字の取り出しも違いうる。
  - 可変フォント版の Noto Sans JP（`NotoSansJP-VariableFont_wght.ttf`）が入った計算機では、
    `NotoSansJP-Thin_Regular` として埋め込まれた。**描かれる文字は正しいが、PDF の文字の
    対応表（ToUnicode）が欠けている・部首の文字を指している**ため、検索・コピーでは数字が
    記号に（`2,685` → `!,#$%`）、一部の漢字が部首の文字（木 → ⽊、工 → ⼯）になる。
    読む側の問題ではなく、PDF に書かれた対応表がそうなっている。
  - GUI の「PDF の取り出し」でも、同じ計算機のパブリッシュでも同じに化けた（API の問題ではない）。`fSubsetFonts=false`・
    `fStandardFormat=ePDF_A2U`・その両方でも化け方は変わらなかった
    （[実測](../Metrics/PDF%20Export.md#書体と文字の取り出し)）。
  - `ePDF_A2U` は PDF 1.7 で書かれ、12 枚で 1.18 MB → 0.97 MB に小さくなった。

## 失敗の見え方

戻り値が成功でもファイルが無いことがある。**書き出した後にファイルと頁の数を確かめる。**

| 場面 | `OpenPDFDocument` | `ExportPDFPages` | `ClosePDFDocument` | ファイル |
| --- | --- | --- | --- | --- |
| 無いシートの名前（前面でない図面のシートの名前も） | 0 | **1** | 0 | **0 頁の PDF が残る** |
| 出力先のフォルダが無い | 0 | 0 | 0 | **できない（どれも成功を返す）** |
| 書き込めないフォルダ | **1** | 1 | 1 | できない |
| 開かずに `ExportPDFPages` | — | 1 | 1 | — |
| 同じパスに既にファイルがある | 0 | 0 | 0 | **確認なしに上書きされる** |

（[実測](../Metrics/PDF%20Export.md#失敗と上書き)）

## 未確認のまま残っているもの

- **Windows で同じに振る舞うか**は確かめていない。
- 図面の中身（ビューポートの更新・プラグインオブジェクトのリセット・ワークシートの再計算）が
  書き出しで変わったかは確かめていない（未保存の変更は読めない。[Documents](Documents.md)）。
- パブリッシュ（`IPublish::PublishSavedSet`）は動かしていない。作れない以上、書き出す対象を
  呼ぶ側が決める用途には使えないため。
