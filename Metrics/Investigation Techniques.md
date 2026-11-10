# 調査の作法 — 計測記録

結論（作法の一覧）は [Findings「Investigation Techniques」](../Findings/Investigation%20Techniques.md)。
ここには、各作法を確立した実例——どの調査で何を踏み、何が出たか（数値・コード・ログ・
run へのリンク）——を元の文言のまま置く。各作法の環境は、引いている issue / PR と
その Findings に書いてある。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

## 書く・読む

### 組み込み PIO の universal 名の総当たり（#181）

- `gSDK->GetPluginType(名前, 種別)` は図形を 1 つも作らずに「その綴りのプラグインが在るか」と
  「オブジェクト（`kVSPluginObject` ＝ 2）か」を返した。綴りの候補を並べて総当たりした
  （[#181](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/181)
  では 18 候補のうち 10 件が当たり、8 件が `false`）。当たった綴りは英語の UI 名そのままで、
  空白を詰めると当たらなかった（`Break Line` は在るが `BreakLine` は無い）。候補は
  `SDKLib/Include/Kernel/API/MiniCadHookIntf.h` の `kInternalID_*`（定数名がほぼ綴りになって
  いる）から起こした。
  - `ForEachFilePathInPluginFolderN` は 2450 ファイルを返したが、`.vso` / `.vst` の該当は
    **0 件**だった（組み込み PIO はフォルダ内の単体ファイルとして置かれていない）。

### setter の戻り値と名前の切り詰め（#181）

- 書けなくても true を返した setter: `SetViewportLayerStackingOverride`・`SetUseDocumentClassVis`
  （[Layers and Stories](../Findings/Layers%20and%20Stories.md)）。
- `SetObjectName` は `0`（成功）を返したが、64 文字目以降が落ち、`GetObjectName` は
  ちょうど 63 文字を返した（[#181](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/181) で
  実際に踏み、プローブが 1 巡まるごと無駄になった）。

### モード欄と読み取り専用の鏡（#169）

- **実例**: 構造材の `MajorDepth` は書けて読み戻せるのに断面が 1mm も動かなかった。
  `MemberType`（材質）が「スチール」で、そこでは断面がカタログ
  （`ProfileShape` ＋ `ProfileSize` の綴り）から来ていた。
  「コンクリート」「木」へ倒すと**同じ欄がそのまま断面になった**（#169）。
  **4 択を採るまで 16 個体を無駄に振った。**
- 実例: 構造材の `B` / `B1` / `D` / `D1` は寸法 4 欄の鏡で、絵が 1mm も動いていなくても
  書いた値を返した（#169）。
- 実例: 凡例の枠が合わなかった原因は「中身を変えるのが作り直しより後だった」という
  ただそれだけだった（書く順番を並べ替えて直った）。

## 測る・比べる

### 何も描いていない PIO の外接（#181）

- パスを与えずに作った `StructuralMember` と `Linear Material`
  （[#181](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/181)）で、
  `GetObjectBounds` は `true` を返し、`left` = `bottom` = `+DBL_MAX`（1.7976931348623157e308）、
  `right` = `top` = `-DBL_MAX` が入っていた。幅・高さを計算すると `-inf` になった。

### 前提を測る・輪郭は頂点で・3D の有無（#169・#173）

- 構造材の断面は外接（`GetObjectBounds`）が「主幅 × 主高さ」に一致したので**矩形だと
  読んでしまったが、実際は I 形**で、断面群の子の**12 頂点を 1 点ずつ出して初めて**
  「効かないと思っていた 2 欄は板厚だった」と分かった（[Findings「副幅・副高さは板厚」](../Findings/Parametric%20Objects.md)。
  #169 → #173）。頂点はポリゴン・ポリラインなら `VWPolygon2DObj(handle)` の
  `GetVertexCount` / `GetVertexPoint` で引いた。
- 同じ構造材で、断面群の輪郭も外接も正しいのに**型 84（`kCSGTree`）だけが作られていない**
  ——平面図には出るのに 3D が無い——状態が起きた。`ResetObject` は `true`、読み戻しも一致、
  群の外接も正しかった。
- 同じ入力の範囲外でも、3D が作られない個体と、作られるが寸法が違う個体があった。

### 正解と並べて差分を取る

- 読み取り専用のダンプ（パラメトリックレコード・付いているレコード・オブジェクト変数・
  補助オブジェクト）を、UI で設定する前と後で 2 回取って diff を見る手順で、`ModifySlab` の
  噛み合わせも断面ビューポートの範囲も凡例のフィルタも決着した（実装例: ホームズ君
  プラグインの `scripts/vw-dump-pio-fields.py`）。

### 描かれている文字を測る（#161・#177・#156）

寸法や PIO は、描いた結果を子の図形として持っている。深さ数段まで潜って最初の型 `10`
（`kTextNode`）を拾い、`GetTextSize(text, 1, charSize)` で 1 文字目の大きさを読んだ
（容れ物の縮尺で割ると紙の上の pt）。使ったコード:

```cpp
// container の中から最初の文字図形を探し、その大きさ（図面上の mm）を返す。
bool FindDrawnTextSize(MCObjectHandle container, int depth, double& outMM)
{
    if (container == nil || depth > 4) return false;
    for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil;
         m = gSDK->NextObject(m))
    {
        const short type = gSDK->GetObjectTypeN(m);
        if (type == 0) break;                      // kTermNode（walk の終端）
        if (type == kTextNode)
        {
            WorldCoord size = 0;
            gSDK->GetTextSize(m, 1, size);
            outMM = static_cast<double>(size);
            return true;
        }
        if (FindDrawnTextSize(m, depth + 1, outMM)) return true;
    }
    return false;
}
```

- **実機の絵との突き合わせ**（[issue #161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)）:
  1/50 のビューポートの注釈へ作り方を変えた寸法を 7 行ならべ、この物差しが「紙 6pt」と
  言った行だけが絵で読め、「紙 0.12pt」と言った行は寸法線だけだった——**7 行とも一致**。
  2 巡目では「紙 12pt」と言った 1 行だけが絵でも 2 倍に見えた。
- #157 は「`GetTextStyleByClass` / `GetTextStyleRef` / `ovDimTextStyle` / `ovDimFontSize` の
  4 つとも同じなのに絵が違う」で行き止まりになったが、描かれている文字を測ると
  その場で割れた（[Dimensions](../Findings/Dimensions.md)）。
- **レイアウト（プロファイルグループ）が混ざった例**: データタグ・レベルオブジェクト・
  図面ラベルのようにレイアウトをプロファイルグループで持つ PIO では、`FirstMemberObj` の
  再帰が描いた図形だけでなくレイアウトそのものも拾った。そこに入っているのは
  「レイアウトへ与えた値」（縮尺を掛ける前の、紙の上の値）で、文字を 1 つも描いていない
  個体を測ると、その値が「描かれた文字」として返ってきた。
  [#177](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/177)
  では、これで「縮尺が掛からない」という**存在しない現象**を 7 回の走行にわたって
  追いかけた（実際には `Title` が空で文字が描かれていなかっただけ。
  [Drawing Labels](../Findings/Drawing%20Labels.md)）。
  - 大きさ `s` の文字が描かれているとき、外接の高さは `s × 1.5` だった（実測。フォント
    依存だが、この比は安定していた）。文字が描かれていない個体の外接の高さは `0` だった。
- **古い写しを拾った例**: 連続寸法（型 86）は中に 2D 表現のグループ（型 11）を持ち、
  そこだけ引き直される前の文字が残っていた
  （[issue #156](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/156)
  で実際に踏んだ）。

### 型で数える・欄ごとに個体を分ける（#166）

- 実例: コンテナの中の節点型（`GetObjectTypeN`）を数える 1 行を診断へ出して、凡例イメージが
  ビューポートだと分かった。中を見ずにヘッダだけで推測していた間はずっと外していた。
- 「描かれた子」を型 5 / 21 / 84 だけと決めて数えたせいで、網から漏れた型 11（`kGroupNode`）の
  子が「消えた」と読めてしまった
  （[#166](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/166)。
  実際には何も消えていなかった）。
- 同じ [#166](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/166)
  で、1 個体に欄を順に書いて測り、表示欄が消した子を、直前に振っていた `AttributesMode` の
  せいだと読んだ。

### 実装ソースと索引の外（#151）

- `SDKLib/Source/VWSDK/` に VWFC（`VWFC::VWUI` / `VWFC::Tools` …）の `.cpp` が丸ごと入って
  いる。実例: `VWImagePopupCtrl::CreateControl` が **`return false` のスタブ**だと分かり、
  実機での条件出しが丸ごと不要になった（[Layout Dialogs](../Findings/Layout%20Dialogs.md)）。
- 旧 API の一部は `Include/Kernel/API/APIBase.Legacy.h` の中で `APP_API_FUNCTION(...)` /
  `APP_API_PROCEDURE(...)` の**引数として**書かれており、索引を作るパーサはそれを宣言として
  読み取れない。**実例**: 断面ビューポートの作り方を調べていて、索引には 1 行も無い
  `GS_CreateSectionLineInstance(CallBackPtr, MCObjectHandle inSectionView)` と
  `GS_IsSectionLineLinkedToViewport(CallBackPtr, MCObjectHandle inSectionLine)` が
  `sdk-grep` で出た（[issue #151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)）。

### 呼べない `APP_API_*` の 12 本

- `APP_API_FUNCTION` / `APP_API_PROCEDURE` について、ヘッダのコメントは
  *SDK APIs waiting to be published for external use* と書いている。マクロが作るのは
  `CB_<名前>` を呼ぶ**インライン**の `GS_<名前>` だけで、その `CB_` シンボルは
  `SDKLib/LibMac/Release/libVWSDK.a` に入っていなかった——`libVWSDK.a` にある `CB_` は
  **メモリ確保系の 8 本だけ**（`CB_NewHandle` 等）。呼ぶとリンクで落ちた:

  ```
  Undefined symbols for architecture arm64:
    "_CB_CreateSectionLineInstance", referenced from: …
  ld: symbol(s) not found for architecture arm64
  ```

  `compile`（`-fsyntax-only`）では通った（プローブの構文チェックが緑のままプラグインの
  ビルドで落ちた。
  [run](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/36519158213)）。
  該当するのは次の 12 本:
  `GS_CreateSectionLineInstance` / `GS_IsSectionLineLinkedToViewport` /
  `GS_IsDetailCalloutLinkedToViewport` / `GS_DisplayImagePopup` /
  `GS_{Begin,Cancel,Draw,End,Update}FreehandInteractive` /
  `GS_GetFreehandInteractivePoly` / `GS_SetFreehandInteractivePen{,Style}`。
- `APIBase.Legacy.Defs.h` に `extern "C" … GS_…(CallBackPtr, …);` として並んでいる
  ふつうの旧 API は、`libVWSDK.a` に実体がある。

### 絵に出ているか・双方向で測る

- 実例: 連続寸法の中の直線寸法は、`ovDimFontSize` を読み戻すと書き換わっていたのに、
  文字の大きさが変わらなかった（[Dimensions](../Findings/Dimensions.md)）。
- 実例: 「作ったときのアクティブレイヤ」と「繋ぐときのアクティブレイヤ」を片方ずつ変えた
  A・B を同じ 1 回の実行に並べ、連続寸法の文字の大きさは**繋ぐときの**アクティブレイヤだけで
  決まると 1 往復で確定した（[Dimensions](../Findings/Dimensions.md)）。

### 生バイトと浮動小数の走査（#67・#71・#73）

- clang の既定は `-ffp-contract=on` で、`v / 25.4 * 25.4 - v` は `fma(v / 25.4, 25.4, -v)` に
  なり、丸めが 1 回減るぶん答えが変わった——実例: 3531 で `-2.885e-13` と出たが、本当の
  往復誤差は `-4.547e-13` だった（[issue #67](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/67)。
  1 往復を無駄にした）。
- 同じ #67 で、8 点では当たって見えた仮説が、値を散らした 18 点の走査で落ちた。
- 実例: [issue #71](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/71) の
  491 ケース・11.3 秒を、1 ケース 1 文字・50 文字ずつ並べて 10 行ほどの地図にした
  （全文の明細は「0 でなかったケース」だけ）。`0000000001111000000000000` のような
  並びを見て初めて「隣り合う値がまとまって出る」が分かった——1 ケース 1 行では
  埋もれていた。
- 実例: [issue #73](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/73) で
  「残差は 3 次元長で決まるのか、成分の組で決まるのか」を、**ピタゴラス数**
  （`(1600, 0, 3000)` / `(3000, 0, 1600)` / `(0, 0, 3400)` は 3 次元長が厳密に 3400）で
  割った——**5 家族 15 ケースで済み**、うち 4 家族で食い違って決着した。
- #73 では `dz` × `dx` の 21 × 21 を 21 行で出し、**`dx = 0` の列（＝鉛直）に
  だけ負符号が無い**ことが目で見えた。
- #71 では、走らせる前に手元で 26 点を整理して (1) 残差は必ず `k × ULP` (2) `k` は仮数で
  決まる (3) 指数には依らないらしい——の 3 つに畳み、**その 3 つを正面から試す群**を
  プローブに置いた。結果、(3) が確定して「危ない値の話は仮数だけの話」に縮んだ。

### Windows 版 SDK は mac 版と同一

- `ci-debug` の `--platform windows` と `--platform mac` が用意する SDK を比べた。
  **`SDKLib/` の 961 ファイルすべてが改行コードを除いて同一**だった（CR を落とした集約ハッシュが
  `a859b31735d9572d2b7c967aa3512a19` で一致。ディレクトリ単位でも全 19 群が一致。
  [windows](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/35883156877) /
  [mac](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/35883177907)）。
  `Include/OnlyWin` も `Include/OnlyMac` も、どちらの SDK にも両方入っていた
  （13 ファイル / 27 ファイル）。
  - 最初は `tr -d '\r'` で比べた。macOS の BSD `tr` は UTF-8 でないバイトを含むファイルで
    `Illegal byte sequence` を出して**そのファイルだけ空にした**ので、GNU `tr` の Windows 側と
    比べると差が無いのに差が出た（961 ファイル中 6 ファイルで踏み、4 つのディレクトリが
    「違う」と出た）。`perl -0777` で読んで `s/\r//g` すると両方で同じ挙動になった。

### 図面ラベルの調査で踏んだもの（#175・#177・#179）

- 図面ラベルでは `FirstMemberObj(pio)` の直下は群（型 11）で、テキストはその中にあった。
  1 階層だけ舐めて「テキストが無い」と読み、[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)
  で 1 往復無駄にした。
- ビューポートの注釈へ「紙で 6pt の寸法」（`ovDimFontSize` ＝ 紙の pt × 25.4/72 × 縮尺。
  [Dimensions](../Findings/Dimensions.md) で確定済み）を置いて物差しにした。
  - テキストの外接の高さは大きさの 1.4〜1.5 倍で、書体によって比が違った（寸法の文字で
    1.44、ラベルの文字で 1.5）。大きさと外接を混ぜて比べ、10pt を `10.4167pt` と読んだ。
- 図面ラベルの文字の大きさでは「容れ物の縮尺」と「アクティブレイヤの縮尺」が 5 走行に
  わたって常に一致していて、どちらが効いているのか決まっていないのに「容れ物だ」と
  書いてしまった（[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)。
  翌走行で否定され、取り消した）。1/50 の容れ物 × アクティブ 1:1 と、1:1 の容れ物 ×
  アクティブ 1/50 の交差させた対を 1 回の走行で取り、1 回で決まった。
  （同じ経緯は [Dimensions](../Findings/Dimensions.md) の「双方向で測る」にもある。）
- 図面ラベルの図番の採番は、作った直後の値を並べると `1 2 1 3 4 5` と乱数のように見えた
  （[#179](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/179)）。
  1 本足すたびに「いま在るもの全部」を読み直したら、先頭のラベルが毎回書き換わって
  いることが 1 回の走行で出た。
- #179 で立てた規則（「使われていない最小の正整数」＋「注釈の先頭は最後に入れた本を
  映す」）を、[#177](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/177) の
  11 本（`1 2 1 3 4 5 4 6 4 7 4`）へ当てたら**1 本の例外も無く一致した**。
- #179 では 6〜8 本を 1 フェーズにした。3 本では乱数に見えたものが、6 本並べると規則に
  なった。

### ワークシート式の評価（#187）

- ヘッダの読み: `ISDK::CompileCriteriaExpression(式, 誤りのコールバック, 文脈)` で
  コンパイルし、`ISDK::ExecWSExpression(h, compiled, outResult)` で実行する
  （ラッパーは `VWFC::Tools::WSCriteriaExpression`。`Compile` / `ExecWSExpression` /
  `GetLastError` / `GetLastErrorOffset` が生えている）。SDK のコメントの例は `"Area()/2"`
  （`=` を付けない）。文脈は `eLocal`（ローカライズされた綴り）と `eUniversal`。
- 対照として入れた `T187NOSUCHFUNCTION` が、コンパイル `ok` のまま
  `s:'T187NOSUCHFUNCTION'` を返した
  （[#187](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/187)）。
- `Z()` のように**関数でないものに `()` を付けた**ときは、コンパイル誤り（`InvalExpr` 等）が
  返った。
- 当初この節は「コンパイルの失敗（`InvalIdent`）で『そんな名前は無い』と判定できる」と
  書いていた。実測では `InvalIdent` は 1 度も返らず、知らない名前はすべて `ok` ＋ 自分自身の
  文字列だった。対照が入っていたのでその場で分かった——自分自身の文字列を返した候補は
  20 以上あった。
- 戻り値は `VWVariant` で、型（`GetType`）をログへ出した（`eVWVariantType_Empty` と
  「空文字列」は別物）。
- ヘッダの読み: 量（長さ・面積・体積など）だけなら `ISDK::ExecQTOFunction` ＋ `EQTOFunction`
  （`Angle` / `Count` / `Length` / `Perimeter` / `Width` / `Height` / `Depth` / `Weight` /
  `Area` / `SurfaceArea` / `ProjectedArea` / `FootPrintArea` / `CrossSectionArea` /
  `SpecialArea` / `Volume` / `ObjectData` / `Thickness` の 17 個）がある。

## いつ読むか——同じ呼び出しの中・undo イベントの後・VW へ戻った後

[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)
で、過去に「効かない／変わらない」と結論した項目を、同じ図形について次の 4 時点で
読み比べた:

| 時点 | いつ |
| --- | --- |
| T0 | 書いた直後（同じ呼び出しの中）——これまでの調査はすべてここ |
| T1 | 同じ呼び出しの中でランループを 1.5 秒回した後（`CFRunLoopRunInMode` を短く刻む） |
| Te | 自動で開いた undo イベントを `EndUndoEvent()` で閉じた直後（まだ同じ呼び出し） |
| T2 | 別の走行（VW へ制御を返し、数十秒置いた後） |

後から変わったものは 2 種類あり、引き金が違った:

| 引き金 | 変わったもの | T0 | 変わった時点 |
| --- | --- | --- | --- |
| **ランループ**（VW へ戻れば回る） | 連続寸法へ `SetTextStyleRef` だけ掛けたときの中の寸法の大きさ（[Dimensions](../Findings/Dimensions.md)） | 古い | T1 |
| | 連続寸法の中の寸法へ直接書いた `ovDimFontSize`（捨てられる） | 書いた値 | T1 |
| **undo イベントの終わり** | `ResetObject` の後に `SetEntityMatrix` で動かした PIO の外接（[Parametric Objects](../Findings/Parametric%20Objects.md)） | 古い位置 | Te |
| | `UpdateViewport` 直後のビューポートの `IsDirty()`（`no` → `yes`。[Viewports](../Findings/Viewports.md)） | `no` | Te |

それ以外は変わらなかった——PIO・スタイル 97 欄中 96、データタグ・レイヤ高さ 50 欄、
寸法 107 欄中 100、ビューポート・図面ラベル 97 欄中 80（変わったのは上表の 2 種類だけ）、
断面ビューポートの (1064,1065) × 向き × pt3 の 16 枚は 16 枚とも同じ。2 巡目
（[PR #250](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/250)）の
ビューポートの `SetDirty(true)` 12 欄・データタグとグラフィック凡例 44 欄・構造材の
`AttributesMode` / 3 面の目印 / 既定 by-class の旗 74 欄も、変わった欄は 0。
`UpdateStyledObjects` のずれと累積・`SetPluginObjectStyle` だけでは記録が変わらない・
`RedrawRect` / `ResetObject(レイヤ)` は作り直さない・3D パスの構造材の `#ZTBB*#` が
±1.79e308・`#IPZ#` が追随しない・クラスや規格の文字スタイルが寸法へ届かない・
`AttributesMode` が描画に効かない・平面で使われるのは `_Below`・既定のマーカー旗は
再生成で下りる・ソースの無い凡例は空、などの結論は**戻った後に読み直しても同じ**だった
（各ファイルに注記した）。`doRegen=false` で作ったまま `ResetObject` しない PIO は、
戻った後も子 2 つ・外接は空のままだった。
旧結論「ビューポートによっては `SetDirty(true)` が立たない」は再現せず、掛けた 4 枚すべて
（断面・更新済みの平面・一度も更新していない平面・直後に更新し直す平面）で、掛けた直後に
立った（[Layers and Stories](../Findings/Layers%20and%20Stories.md)）。

- 仕立て: [PR #246](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/246) の
  `probes/runtime/deferred-*/Deferred.h`（`RunDeferred`。役目を終えたので消したが、履歴から
  取り出せる）——1 本のプローブを 2 回走らせ、状態を一時ファイルで受け渡し、図形は名前で
  引き直す。1 回目の最後に自動で開いた undo イベントを閉じ、閉じた直後にも読む（Te）。
- 図形を作るだけで undo イベントは開いた（[Undo](../Findings/Undo.md)）。閉じずに戻ると、
  利用者がクリックしても閉じず、外接も `IsDirty` も古いままで、MCP ブリッジは受け付けを
  見送って止まった（7 分。
  [#247](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/247)）。
  **閉じずに置いた 7 分のあいだ、外接は 1mm も追い付かなかった。**
- 戻った後に呼ばれるもの（タイマー・`dispatch_after`・通知）を仕掛けたプローブは、殻が
  走らせた直後に本体を降ろすので、VW ごと落ちた（#245 で落とした。
  [probes/runtime/README.md](../probes/runtime/README.md)）。
- `OpenDocumentPath(nullptr)` で開いた図面では、利用者が ⌘N で作る図面と違い、図面ラベルの
  ツールにスタイルが無く（既定レイアウトに文字が無い）、「寸法」クラスも文字スタイルを
  持たなかった。縮尺は 1/1 で用紙も小さく、画面に見えている範囲が狭い——
  `AssociateLinearDimension` が成立しなかったのは資源のせいではなく、寸法を表示範囲の外に
  置いたからだった（[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)。
  [Dimensions](../Findings/Dimensions.md)「関連付くのは表示範囲の中だけ」）。
  同じ出どころの図面は `.sta` を `OpenDocumentPath` へ渡して作った（[Documents](../Findings/Documents.md)）。
  MCP ブリッジの `vw_run_probe` が開く新規図面も `OpenDocumentPath(nullptr)` である。
- #249 で「画面の状態」を読む呼び出しを 2 つ踏んだ——図面ラベルは**選択中のビューポート**の
  図面タイトルを持って生まれ（[Drawing Labels](../Findings/Drawing%20Labels.md)）、
  `AssociateLinearDimension` は**表示範囲の外**の寸法を関連付けなかった
  （[Dimensions](../Findings/Dimensions.md)）。`CreateViewport` は作ったビューポートを選択した
  まま残し、前の手順が後の手順の結果を変えた。
- 場面の作り方が少し違うだけで、断面ビューポートに中身が入らない
  （[#248](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/248)）・
  図面ラベルが文字を描かない、といった旧調査との食い違いが T0 の時点で出た。#245 では
  旧プローブそのものを 2 回走らせる形にして測り直した。
- 所要時間（ms）の表が測っているのは「呼び出しの中の時間」だけで、戻った後の仕事
  （上表のランループ側）は含まれていない。
