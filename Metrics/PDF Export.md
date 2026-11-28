# PDF の書き出し（`IExportPDF`） — 計測記録

結論は [Findings「PDF Export」](../Findings/PDF%20Export.md)。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

[issue #332](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/332) の調査。

## 測った環境

- VW 2026（Vectorworks Architect 2026）/ macOS / Apple Silicon。2026-11-28。
- 実機確認プラグイン（VwSdkProbes）のプローブ `export-sheet-pdf`（PR #333）を、MCP ブリッジの
  `vw_run_probe` で走らせた（ブリッジの受け付けは OS タイマーの刻みの中）。プローブは役目を
  終えたので消してある。
- 走らせた回:

| 回 | プローブの版 | 前面 | 後ろ |
| --- | --- | --- | --- |
| 1 | 71adf10 | 新規の空図面 | なし |
| 2 | fcfcdfb | 新規の空図面 | 利用者の図面（木造の構造図。シートレイヤ 12 枚。以下「図面 A」） |
| 3 | fcf6df7 | 新規の空図面 | 図面 A（Vectorworks を起動し直して開き直した） |

- 回 2 と回 3 の間に、Vectorworks は一度終了していた（クラッシュレポートは無い）。
- 比べる相手:
  - パブリッシュの PDF: 利用者が**別の計算機で**図面 A のシート 12 枚を GUI のパブリッシュで
    書き出したもの（2026-10-10 作成。Producer / Creator は `Vectorworks`）。
  - この計算機のパブリッシュの PDF: 回 3 の後、同じ計算機で利用者が同じシート 12 枚を GUI の
    パブリッシュで書き出したもの。
  - GUI の書き出しの PDF: 回 3 の後、同じ計算機で利用者が GUI の「ファイル → 取り出す → PDF の
    取り出し」でシート「30」を既定の設定のまま書き出したもの。

## プローブの手順

何をどの順で呼んだか（コードの要点）。

1. `IExportPDFPtr pdf(IID_ExportPDF)` → `GetOptions(defaults)` を記録。出力の根は
   `<temp>/VwSdkProbes-export-pdf/<time>/`。
2. `opts = defaults; opts.fOpenInViewer = false; SetOptions(opts)` → `GetOptions` で読み戻す。
   `fResolution` を 150 にして読み戻し、戻す。
3. 前面の空図面に `CreateLayer("VwSdkProbes S1", kLayerSheet)`・`CreateLayer("VwSdkProbes S2", kLayerSheet)`
   を作り、それぞれに `CreateTextBlock` で名前の文字を置く。
4. 1 回の書き出しは次の並び（`EpExport`）。各段の戻り値と所要（`steady_clock`）を記録し、
   閉じた直後と 2 秒後にファイルの大きさ・更新時刻・先頭 5 バイト・`/Type /Page` の数・
   `/MediaBox` を読む。

   ```cpp
   IFileIdentifierPtr file(IID_FileIdentifier);
   file->Set(TXString(pathUtf8));
   pdf->OpenPDFDocument(file);
   for (const EpStep& s : steps)
   {
       if (s.layer != nullptr)
           gSDK->SetCurrentLayer(s.layer);
       pdf->ExportPDFPages(TXString(s.name));
   }
   pdf->ClosePDFDocument();
   ```

5. 前面で: 現在 S1 ＋ 空の名前・現在 S1 ＋ S2 の名前・現在 S2 ＋ S2 の名前・無い名前・
   S1 と S2 を 1 つに・同じパスへ 2 回目・`fExportRangeKind` を 0 / 1 / 3 にして S1。
6. 失敗: 開かずに `ExportPDFPages("")` と `ClosePDFDocument()`・無いフォルダ（`missing/deeper/x.pdf`）・
   権限を `r-x` にしたフォルダ。
7. ISDK の並び: `gSDK->OpenPDFDocument(path)` → 成功すれば `ExportPDFPages("")` → `ClosePDFDocument()`。
8. 後ろの図面のうちシートレイヤのある最初の 1 つ（回 2・3）: `SwitchToOpenFile` で前面にして
   シートの名前を集め、一度前の図面へ戻して、前面からその図面の最初のシートの名前で書き出す。
   もう一度その図面を前面にして、シートを名前だけで 1 枚ずつ・全部を 1 つに書き出す。
9. 回 3 だけ: 最初のシートと全部を、次の 3 つの設定で書き出す——`fSubsetFonts=false`・
   `fStandardFormat=ePDF_A2U`・その両方。
10. `SetOptions(defaults)` で戻し、始めの図面を前面に戻し、`IsCurrentlyBuildingAnUndoEvent()`
    を記録する。

出力は PDFKit（`osascript -l JavaScript`）で頁ごとの大きさと取り出した文字を読み、
Swift（PDFKit・CoreGraphics）で 2400 px 幅に描いて画素を比べた（RGB の差の和が 48 を超える
画素を数える）。埋め込まれた書体は、ファイルの中の `/BaseFont` を（Flate のストリームを
展開して）数えた。

## 設定の読み書き

回 3 の `GetOptions`（既定）:

```text
fSizeOf=56 fStandardFormat=0 fExportDesignLayersAsPDFLayers=no fExportClassesAsPDFLayers=no
fGrayedLayersInitiallyInvisible=no fGrayedLayersWithNormalAttributes=no fResolution=300
fExportPatternsAtOnScreenResolution=yes fDownsampleImages=no fDownsampleResolution=300 fApplyJPEG=no
fRasterizeText=no fSubsetFonts=yes fOpacityForTransferModes=50 fOpenInViewer=no fExportRangeKind=0
fExportRangeFromPage=0 fExportRangeToPage=0 fUpdateViewports=yes fResetPluginObjects=yes
fRecalculateWorksheets=yes
```

- 回 2 の既定では `fOpenInViewer=yes`、他は同じだった。回 2 の終わりに既定（`yes`）へ
  `SetOptions` で戻し、Vectorworks が終了・起動し直した後の回 3 では `no` だった。
- `SetOptions` の戻り値はどれも 0。`fResolution=150` にした直後の `GetOptions` は 150、戻すと 300。
- 利用者が GUI で開いた「PDF の取り出し」ダイアログ（回 3 の後）の既定は、形式 PDF・解像度 300・
  画面解像度で模様を取り出す オン・文字をラスター化 オフ・サブセットフォント オン・
  印刷可能領域全体を 1 ページ・更新の 3 項目 オン・デフォルトのビューアで開く オフ・
  グレイ表示のレイヤ / クラス出力濃度 25%・リンクされているオブジェクトを PDF ハイパーリンクとして
  取り出す オン（スクリーンショットで読んだ）。

## 名前の受け付けとまとめ方

前面の空図面（回 3）。S1・S2 の用紙は 8.263333 × 11.693333 in（A4 縦）。

| 場面 | 現在のレイヤ | 渡した名前 | `ExportPDFPages` | 所要 | 頁 | `/MediaBox` |
| --- | --- | --- | --- | --- | --- | --- |
| 現在 S1 ＋ 空 | S1 | 「」 | 0 | **9661 ms** | 1 | 595×842 |
| 現在 S1 ＋ S2 | S1 | 「VwSdkProbes S2」 | 0 | 39 ms | 1 | 595×842 |
| 現在 S2 ＋ S2 | S2 | 「VwSdkProbes S2」 | 0 | 48 ms | 1 | 595×842 |
| 無い名前 | S1 | 「VwSdkProbes 無い 332」 | **1** | 0 ms | **0** | なし |
| S1 と S2 を 1 つに | S1 → S2 | 「」 → 「」 | 0, 0 | 25 / 9 ms | **2** | 595×842 ×2 |

- 回 1 の最初の書き出し（現在 S1 ＋ 空）の所要は 6.5 秒だった。どちらも、その後の小さな
  シートの書き出しは数十 ms。
- 現在 S1 ＋ S2 の名前の PDF（`name-s2.pdf`）と現在 S2 ＋ S2 の名前の PDF は、どちらも
  頁の文字が「VwSdkProbes S2」だった（回 1 で PDFKit で読んだ）。

## 書き出す範囲

S1（A4 縦）を前面で。

| `fExportRangeKind` | 頁 | `/MediaBox` |
| --- | --- | --- |
| 0 `eWholeAreaAsOnePage` | 1 | 595×842 |
| 1 `eAllPages` | 1 | 595×842 |
| 3 `eCurrentView` | 1 | **711×540** |

## 失敗と上書き

回 3。

| 場面 | `OpenPDFDocument` | `ExportPDFPages` | `ClosePDFDocument` | ファイル |
| --- | --- | --- | --- | --- |
| 無い名前 | 0 | 1 | 0 | 1893 バイト・頁 0 |
| 開かずに | — | 1 | 1 | — |
| 無いフォルダ `missing/deeper/x.pdf` | 0 | 0 | 0 | 無い |
| 権限 `r-x` のフォルダ | 1 | 1 | 1 | 無い |
| 同じパスへ 2 回目（`current-s1-empty.pdf`） | 0 | 0 | 0 | 5882 → 5861 バイト、更新時刻が新しくなった。所要 35 ms |
| 前面の空図面から、後ろの図面のシート「30」の名前 | 0 | 1 | 0 | 1909 バイト・頁 0 |

- ISDK の並び: `gSDK->OpenPDFDocument(path)` が `false`（所要 0〜1 ms）。ファイルは無い。
- どの場面でも、所要は短く（最大でも書き出しの 9.7 秒）、画面にダイアログは残っていなかった。

## 後ろの図面のシートの書き出し

図面 A のシートレイヤ 12 枚（「30」〜「41」）。どれも用紙 16.535000 × 11.691667 in、
シート 16.535433 × 11.692913 in。保存したビューは 0。

回 3（既定の設定・`fOpenInViewer=no`）:

| シート | `ExportPDFPages` | `ClosePDFDocument` | 合計 | 大きさ | 頁 | `/MediaBox` |
| --- | --- | --- | --- | --- | --- | --- |
| 30〜41 を 1 枚ずつ | 0（各 0.4〜1.1 秒） | 0（50〜760 ms） | 0.5〜1.5 秒 | 100〜350 KB | 1 | 1191×842 |
| 全部を 1 つ（12 回） | 0（各 0.4〜0.8 秒） | 0（5.3〜7.2 秒） | 12.3〜13.8 秒 | 1,183,890 バイト | 12 | 1191×842 ×12 |

- 回 2 の値（1 枚ずつ: 0.54〜1.53 秒。全部: 12,271 ms、`ClosePDFDocument` 5,330 ms、1,183,895 バイト）。
- 書き出しの前後で、undo は `no`、現在のレイヤは「36」のまま、前面は対象の図面のまま。
  終わりに `SwitchToOpenFile` で始めの図面に戻した後も `IsCurrentlyBuildingAnUndoEvent()=no`。

## GUI の書き出しとの比較

| 比べたもの | プローブ（回 3）`30.pdf` | GUI の「PDF の取り出し」 |
| --- | --- | --- |
| 大きさ | 348,452 バイト | 348,488 バイト |
| 頁・`/MediaBox` | 1・1191×842 | 1・1191×842 |
| `/BaseFont` | HiraginoSans-W3 ×2・NotoSansJP-Thin_Regular ×117 | 同じ |
| 取り出した文字の先頭 | `! ! ! ! ! ! ! ! !,#$% !,#$% …` | 同じ |
| 2400 px で描いた画素の差 | — | **0 画素** |

## この計算機のパブリッシュの PDF との比較

プローブ（回 3）の `all-sheets.pdf`（既定の設定）と、この計算機のパブリッシュの PDF。

| 比べたもの | プローブ | この計算機のパブリッシュ |
| --- | --- | --- |
| 大きさ | 1,183,890 バイト | 1,183,867 バイト |
| 頁・`/MediaBox` | 12・1191×842 ×12 | 12・1191×842 ×12 |
| 取り出した文字（PDFKit） | — | **12 頁とも同じ**（頁 1 は 1381 文字。どちらも数字が記号になる） |
| 2400 × 1696 px で描いた画素の差 | — | **12 頁とも 0 画素** |

## 別の計算機のパブリッシュの PDF との比較

プローブ（回 2）の `all-sheets.pdf` と、別の計算機のパブリッシュの PDF。

| 比べたもの | プローブ | パブリッシュ |
| --- | --- | --- |
| 頁 | 12 | 12 |
| `/MediaBox` | 1191×842 ×12 | 1191×842 ×12 |
| 大きさ | 1,183,895 バイト | 418,062 バイト |
| `/BaseFont`（サブセットの接頭辞を除く） | HiraginoSans-W3・NotoSansJP-Thin_Regular | GenShinGothic-Regular・HelveticaNeue・HiraginoSans-W3・NotoSansJP-Regular |
| 取り出した文字の長さ（頁 1 / 2 / 3） | 1381 / 866 / 1095 | 1380 / 866 / 1095 |

2400 × 1696 px で描いて、差のある画素:

| 頁 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 差（%） | 2.155 | 1.189 | 1.428 | 1.154 | 1.030 | 1.241 | 1.210 | 0.741 | 0.479 | 0.730 | 0.568 | 0.463 |

- 差の画素は文字の上だけに出て、線には出なかった（頁 1 の差の図を目で見た）。
- 図枠の日付の欄は、プローブが 2026/11/28、パブリッシュが 2026/10/10。
- 図枠と注記の文字を拡大して並べると、書体・位置・大きさは目で見て同じだった。

## 書体と文字の取り出し

取り出した文字（PDFKit）の頁 1 の先頭:

```text
プローブ   : ! ! ! ! ! ! ! ! !,#$% !,#$% !,#$% !"" !,#$% …
パブリッシュ: 8 7 6 5 4 3 2 1 2,685 3,640 6,825 200 1,675 …
```

頁 6・8 では、プローブの「垂⽊」「構造⽤」「軒⾼」が、パブリッシュでは「垂木」「構造用」「軒高」
（U+2F4A などの康熙部首の文字と、通常の漢字）。

回 3 で書体の設定を変えた結果（図面 A の全 12 枚）:

| 設定 | 先頭のバージョン | 大きさ | 取り出した文字 |
| --- | --- | --- | --- |
| 既定（`fSubsetFonts=true`・`ePDF`） | `%PDF-1.6` | 1,183,890 | 化ける（上と同じ） |
| `fSubsetFonts=false` | `%PDF-1.6` | 1,171,267 | 同じに化ける |
| `fStandardFormat=ePDF_A2U` | `%PDF-1.7` | 973,572 | 同じに化ける |
| `ePDF_A2U` ＋ `fSubsetFonts=false` | `%PDF-1.7` | 973,237 | 同じに化ける |

- シート「30」だけの PDF（1 枚ずつの書き出し）でも同じに化けた。
- 利用者が GUI の書き出しの PDF をプレビュー.app で開くと、数字・漢字は正しく描かれていた
  （スクリーンショット）。

GUI の書き出しの PDF の書体と ToUnicode の中身（ファイルを Python で読み、
Flate のストリームを展開した）:

- 書体の項目は 118。`Type0` の HiraginoSans-W3 が 1、`TrueType` の
  `AAAAAC+NotoSansJP-Thin_Regular` が 117（どれも `/Encoding` なし・1 バイトの文字コード
  `<00>`〜`<FF>`・それぞれに ToUnicode がある）。
- ToUnicode の例（`文字コード → Unicode`）:

| 書体の項目 | 対応表 |
| --- | --- |
| obj 12（ToUnicode 133、7 件） | `21`〜`25`・`27` は通常の漢字（図枠の工事名）・**`26→2F2F ⼯`**（工でなく康熙部首） |
| obj 13（ToUnicode 135） | `24→002F /` の 1 件だけ |
| obj 34（ToUnicode 177、29 件） | **`21→2FBC ⾼`**・`22→8010 耐`・**`23→2F12 ⼒`**・`24→30D5 フ`・…・`30→0028 (`・…・**`36→2ED1 ⻑`**・…・`3D→0029 )` |

- obj 13 は図枠の日付（`2026/11/28`）の書体で、`/` 以外の文字コードには対応が無い。
- パブリッシュの PDF の ToUnicode は 2 バイトの文字コードで、
  obj 52 は 326 件・obj 54 は 18 件。数字の文字コードは `0011→0030 0`・`0012→0031 1`… のように
  対応がある。
- この計算機に入っている書体: `~/Library/Fonts/NotoSansJP-Regular.ttf`・
  `~/Library/Fonts/NotoSansJP-VariableFont_wght.ttf`・`/System/Library/Fonts/HelveticaNeue.ttc`。
  `GenShin` を名前に含む書体のファイルは無い（Spotlight で探した）。
