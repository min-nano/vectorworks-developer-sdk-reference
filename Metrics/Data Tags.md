# データタグ — 計測記録

結論は [Findings「Data Tags」](../Findings/Data%20Tags.md)。ここには各節の測った環境（VW の版・OS・
舞台）・使ったプローブ・式ごとの実測の表（実行ログそのまま）・訂正の経緯を置く。節の並びは
Findings に対応する。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

データタグ（`Data Tag` PIO）を SDK から生成し、スタイルを使わずに per-instance で
中身（タグレイアウト）を組んだときの実測。

- スタイル（間接段）を挟んだ組み方で、シンボル定義のサブタイプ（`SetSymbolDefSubType`）か
  パラメータ対応表（`SetAllPluginStyleParameters`）のどれかが漏れると、実機で「タグは出るのに
  中身が空」になった。
- タグレイアウトの置き場: `GetCustomObjectProfileGroup` / `SetCustomObjectProfileGroup`
  （VW2020 で足された `…InAux` の入り口もある）。中身は式を持たせたテキスト
  （`IDataTagTextLinkSupport::SetIsLinked` / `SetFormula`）。手で作ったタグのテキストには名前が
  無かった。生成直後のタグは既定でロクスを持っていた。
- 空のグループを先に `SetCustomObjectProfileGroup` へ渡して後から足すと、実機で「オブジェクトは
  出るのにタグレイアウトが空」になった（VW が渡した時点でグループを複製していた場合に、足した
  テキストが迷子になる形）。
- `IDataTagSupport::UpdateUserDefinedTextsUIDs` の SDK のコメントは「データタグ**または**データタグ
  スタイル」に使えるとしている。呼ばないと、タグが式を拾わず値が空のまま出た。
- タグの本文は関連付け先のレコードから来る。
- **置いた後の外接**（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）:
  既定のテンプレートから開いた図面のデザインレイヤで、矩形に関連付けたタグ（`#IPZ#`）を
  (3000, −5500) に置いて `UpdateDataTag` すると、挿入点はその点のまま、外接は
  L1000 T25138.8 R28841.7 B−36138.8（中心は点から横へ +11920.8・縦 0）で、undo イベントを
  閉じた後・戻った後も 1 桁も動かなかった。外接の左端 1000 は矩形の中心 x と同じ値。
  ビューポートの注釈に置いたタグはこの測り直しに入っていない。
  - `MoveObject` で動かしたタグは、外接が直後から追随した（+1000, +500 で右・上・下が
    その分動き、左端は 1000 のまま。`ResetObject` 無し）。PIO を `SetEntityMatrix` で
    動かしたときのように undo イベントの終わりを待つことは無かった
    （[Investigation Techniques](../Findings/Investigation%20Techniques.md)「いつ読むか」）。
  - VW がタグをどこへ落とすかは、ビューポートによってばらばらだった。
- **打ち切った調査「関連付け先からの相対で置く」で試したこと**: 「VW がタグを関連付け先へ
  吸着させるので、そこからの相対で置けば座標系を知らなくてよい」という方式を 2 度試した
  （基準点の当て推量 → 実測からの較正）。較正の残差が大きく、どの基準点候補も当てはまらなかった。
  実際の挙動は「高さは合っていて横だけ一定量ずれ、タグ同士の間隔は対象の間隔に比例」だった。
- 注釈へ後から足した図形のクラスは、ビューポートで非表示のままだった。
- クラス: SDK に `SetClassByName` / `SetAllAttributesByClass` という名前は無く、一括で済ませる口も
  見当たらなかった（`SetObjectClass` ＋ 7 つの `Set*ByClass` を使った。
  [Attributes and Classes](../Findings/Attributes%20and%20Classes.md)）。タグ本体だけに与えると、
  タグレイアウトの中の文字は既定クラスのまま残った（レイアウトの中身は本体のクラスを継がない）。
- 図面ラベル（`Drawing Label2`）: 式は `#Drawing Label2#.#Title#` の形。`IDataTagTextLinkSupport`
  の `IsSupported` が `false` を返した（[Drawing Labels](../Findings/Drawing%20Labels.md)）。

## タグフィールドの式——綴りを SDK に探した

式は `IDataTagTextLinkSupport::SetFormula` / `GetFormula` で読み書きした。

- ヘッダ: `SetFormula` の第 3 引数が `isWorksheetFormula`（既定 `false`）、読み戻しは
  `GetIsWorksheetFormula`。
- `IPZL|thsep|#sign#|DataTagField|TagField` を SDKLib（ヘッダ＋同梱の VWFC 実装ソース、961 ファイル
  全部）へ掛けたときのヒットは **`ShowDefineTagFieldDlg()` の 1 行だけ**だった（`sdk-grep` 実測）。
- 構造材のレコード参照は `#StructuralMember#.#StartElevation#` の形で値を返した。パラメータの全件は
  [Parametric Objects](../Findings/Parametric%20Objects.md)「構造材（`StructuralMember`）のパラメータ表」
  にある（始端・終端の高さらしい欄は索引 28〜33 の
  `DialogStartElevationReference` / `DialogStartElevation` /
  `DialogEndElevationReference` / `DialogEndElevation` / `StartElevation` / `EndElevation`）。
- 構造材のパラメータ**181 件の表に `IPZL` は無い**が、`#IPZL#` は値を返した。

### 式の評価結果を機械で読み戻した方法

以下の表の値は、プローブのログへ文字列で読み戻したもの。

- ヘッダ: `IDataTagSupport::GetDataTagExtractedData(hTag, outArrExtractedData, txtLabel = "")` は
  「ラベル → 値」の対（`TXStringSTLPairArray` ＝ `std::vector<std::pair<TXString, TXString>>`）で返す。
- タグが描いたテキストは、`FirstMemberObj` / `NextObject` でタグの生成物を走査し、型が
  `kTextNode` のものを `VWFC::VWObjects::VWTextBlockObj::GetText` で読んだ。
- ワークシート式は `ISDK::CompileCriteriaExpression` ＋ `ISDK::ExecWSExpression` でハンドルへ直接
  評価した（[調査の作法](../Findings/Investigation%20Techniques.md)「ワークシート式は…」）。

### `#…#` 記法の文法の実測（VW 2026 / mac）

[issue #187](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/187) で
**実機確認済み**（`probes/runtime/datatag-formula-grammar/`。値は実行ログそのまま。
この走行では `#IPZL#` が **−2699**、`#StructuralMember#.#MajorDepth#` が **600**）。

**四則演算と括弧**

| 入れた式 | 出た値 |
| --- | --- |
| `#IPZL#` | `-2699` |
| `#IPZL#+872` | `-1827` |
| `#IPZL# + 872`（空白付き） | `-1827`（**空白は効かない**＝同じ） |
| `#IPZL#-872` | `-3571` |
| `#IPZL#*2` | `-5398` |
| `#IPZL#/2` | **`-1349 1/2`**（**分数で出る**。下記） |
| `(#IPZL#+872)*2` | `-3654`（＝ `-1827 × 2`。**括弧が効いている**） |
| `872+40`（フィールドを含まない定数式） | `912` |

- `/` の結果はこの走行では `-1349 1/2`（図面の単位設定の書式）で出た。
- [issue #190](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/190)
  で `(#…#MajorDepth#*1)`（`MajorDepth` = 1234）を引いたら **`(1234)`** が出た（一番外側の括弧が
  文字として出て、中は評価された）。上の `(#IPZL#+872)*2` は `*2` が続いていて `-3654` だった。

**文字列と連結**

| 入れた式 | 出た値 |
| --- | --- |
| `" (2FL "` | `` (2FL `` |
| `" (2FL "#IPZL#")"` | `` (2FL -2699)``（**並置で連結される**） |
| `(2FL )`（引用符なし・括弧を含む） | **空文字列**（式全体が値を出せなくなる） |
| `× #IPZL#`（引用符なし・演算子でない文字） | `× -2699`（**通る**） |
| `"a"+"b"` | **`a+b`**（`+` が文字として出る＝連結ではない） |
| `"a"&"b"` | **`a&b`**（同じ。`&` も連結ではない） |

- 引用符なしで括弧を書いた式は空になった（issue の見立ては「本文のまま出る」だった）。

**数値の書式（修飾子）**（`MajorDepth` = 600 で引いた）:

| 入れた式 | 出た値 | 読み |
| --- | --- | --- |
| `#…#MajorDepth#` | `600` | 基準 |
| `#…#MajorDepth##sign#` | **`+600`** | **`#sign#` は効く**（`#` が 2 つ要る） |
| `#…#MajorDepth#sign#` | **`600sign`** | `#` が 1 つだと**修飾子として読まれず文字が出る** |
| `#…#MajorDepth##thsep#sign#` | **`+600`** | **`#` を共有して 2 つ連ねられる**（issue に挙がっていた綴りはこれ） |
| `#…#MajorDepth##t187nosuch#` | `600` | **知らない修飾子は黙って捨てられる**（エラーにならない） |
| `#…#MajorDepth#t187nosuch#` | `600t187nosuch` | 同じく `#` が 1 つだと文字が出る |
| `#…#MajorDepth##thsep#` | `600` | 3 桁なので区切る桁が無い（下記） |
| `#…#MajorDepth#*10#thsep#` | `6000` | **コンマが入らない**（ただしこれは「式の後ろ」なので、そもそも修飾子が効かない位置。下記） |
| `#…#MajorDepth#/7` | `85.71` | 演算の結果は小数で出る（`#IPZL#/2` が `-1349 1/2` になったのは元の値の単位の書式） |

- 1 巡目は負の値（−2699）で試して、どの修飾子でも値が変わらなかった（「修飾子はどれも
  効かない」と記録した）。正の値（600）で `##sign#` は `+600` を出した。
- `#IPZL#thsep#`（`#` 1 つ）は `…thsep` と出た。
- `MajorDepth` を **1234** にして（`SetParamReal` の読み戻しで 1234 を確かめた上で）引いたとき、
  `##thsep#` は `1234`、無印も `1234`、**知らない修飾子も `1234`** で、3 つが 1 文字も違わなかった。
  同じ値で `##sign#` は `+1234` を出した。
- > **【訂正の記録】** ここには当初「`#thsep#` にその働きが無い」「式の側から桁区切りを
  > 付ける手は見つかっていない」と書いていた。
  > [issue #196](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/196)
  > で `#IPZS#`＝**1234** / `MajorDepth`＝**1234** に対して引いた:
  >
  > | 入れた式 | 出た値 |
  > | --- | --- |
  > | `#IPZS##thsep#` | `1234`（**これまでの綴り。区切らない**） |
  > | **`#IPZS##mm_0_1#thsep#`** | **`1,234mm`**（**区切る**） |
  > | `#…#MajorDepth##thsep#` | `1234` |
  > | **`#…#MajorDepth##mm_0_1#thsep#`** | **`1,234mm`** |
  >
  > #187 / #190 では修飾子を 1 つずつ単独で置いて試していた。VW の「タグフィールドの定義」
  > ダイアログで単位・精度・3 桁位取り・符号を設定すると、定義欄に
  > `#StructuralMember#.#MajorBreadth##mm_0_1#usp#thsep#sign#` と綴られた。

**単位・精度の修飾子——`#<単位>_<小数桁数>_<単位記号を出すか>#`**
（[issue #196](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/196)。
`#IPZS#`＝**1234**（mm）で引いた。値は実行ログそのまま）:

| 入れた式 | 出た値 | 読み |
| --- | --- | --- |
| `#IPZS##mm_0_0#` | **`1234`** | 3 つ目が `0` なら**単位記号を出さない** |
| `#IPZS##mm_0_1#` | `1234mm` | 3 つ目が `1` で出す |
| `#IPZS##mm_0_2#` / `#mm_0_3#` | `1234mm` | `1` 以上は同じ（旗） |
| `#IPZS##mm_1_1#` | **`1234.0mm`** | 2 つ目は**小数の桁数** |
| `#IPZS##mm_2_1#` | **`1234.00mm`** | 同 |
| `#IPZS##m_0_1#` | **`1m`** | 1 つ目は**単位**。**値が換算される** |
| `#IPZS##cm_0_1#` | **`123cm`** | 同（**四捨五入**。[#198](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/198) で確定） |
| `#IPZS##mm_0#` | `1234mm` | 3 つ目は省ける（既定は「出す」） |
| `#IPZS##mm#` | `1234` | **`_` が無いと効かない**（修飾子として読まれない） |
| `#IPZS##t196nosuch_0_1#` | `1234` | 知らない単位は黙って捨てられる |
| `#IPZS##usp#` | `1234` | **単独では効かない** |
| `#IPZS##mm_0_1#usp#` | **`1234 mm`** | `usp` ＝ **値と単位の間にスペース** |
| `#IPZS##mm_0_1#usp#thsep#sign#` | **`+1,234 mm`** | **ダイアログが生成する連鎖そのもの** |

- `#m_0_1#` は 1234mm を `1m` にした（図面の単位が mm のとき）。

**単位記号なし・桁区切りあり——`##<図面の単位>_0_0#thsep#`**
（[issue #198](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/198)。
`probes/runtime/datatag-thsep-without-unit/`。図面の単位は **mm**。`#IPZS#` を
**1234** / **1234567** / **−1234** / **0** の 4 本の部材で振って引いた。値は実行ログそのまま）。

| 入れた式 | 1234 | 1234567 | −1234 | 0 |
| --- | --- | --- | --- | --- |
| `#IPZS#` | `1234` | `1234567` | `-1234` | `0` |
| `#IPZS##thsep#` | `1234` | `1234567` | `-1234` | `0` |
| `#IPZS##mm_0_0#` | `1234` | `1234567` | `-1234` | `0` |
| `#IPZS##mm_0_1#thsep#` | `1,234mm` | `1,234,567mm` | `-1,234mm` | `0mm` |
| **`#IPZS##mm_0_0#thsep#`** | **`1,234`** | **`1,234,567`** | **`-1,234`** | `0` |
| `#IPZS##mm_0_0#sign#` | `+1234` | `+1234567` | `-1234` | `±0` |
| **`#IPZS##mm_0_0#thsep#sign#`** | **`+1,234`** | **`+1,234,567`** | `-1,234` | `±0` |
| `#IPZS##mm_0_0#usp#` | `1234` | `1234567` | `-1234` | `0` |
| `#IPZS##mm_0_0#usp#thsep#sign#` | `+1,234` | `+1,234,567` | `-1,234` | `±0` |
| `#IPZS##mm_0_0#thsep#usp#sign#` | `+1,234` | `+1,234,567` | `-1,234` | `±0` |
| `#IPZS##mm_2_0#thsep#` | `1,234.00` | `1,234,567.00` | `-1,234.00` | `0.00` |
| `" (2FL "#IPZS##mm_0_0#thsep#")"` | `` (2FL 1,234)`` | `` (2FL 1,234,567)`` | `` (2FL -1,234)`` | `` (2FL 0)`` |

- 旗 0 の `#mm_0_0#`（単位記号を落とす）と `#mm_0_1#thsep#`（区切る）を組み合わせた `#mm_0_0#thsep#` は
  区切った。`#mm_2_0#thsep#` → `1,234.00`（整数部だけ区切られた）。
- `#mm_0_0#usp#` は `1234` で、末尾に空白は残らなかった（プローブは値を `'…'` で括ってログへ
  出しているので、残れば `'1234 '` と見える）。
- `#mm_0_0#thsep#usp#sign#` は、ダイアログが生成する順（`#mm_0_0#usp#thsep#sign#`）と
  1 文字も違わなかった。
- レコードのフィールド `#StructuralMember#.#MajorDepth#`（＝1234）でも `##mm_0_0#thsep#` →
  **`1,234`**、`##mm_0_0#usp#thsep#sign#` → **`+1,234`** だった。
- 同じ部材（**1234mm** と **1234567mm**）に単位だけ変えて当てた:
  >
  > | 入れた式 | 1234mm の材 | 1234567mm の材 |
  > | --- | --- | --- |
  > | `#IPZS##mm_0_0#` | `1234` | `1234567` |
  > | `#IPZS##m_0_1#` | `1m` | `1235m` |
  > | **`#IPZS##m_0_0#`** | **`1`** | **`1235`** |
  > | `#IPZS##m_3_0#` | **`1.234`** | **`1234.567`** |
  > | `#IPZS##m_3_1#` | `1.234m` | `1234.567m` |
  > | **`#IPZS##m_0_0#thsep#`** | **`1`** | **`1,235`** |
  > | `#IPZS##cm_0_0#` | `123` | `123457` |
  > | `#IPZS##cm_1_0#` | `123.4` | `123456.7` |
  > | **`#IPZS##mm_0_0#thsep#`** | **`1,234`** | **`1,234,567`** |
  >
  > - `#m_3_0#` は `1.234` / `1234.567` を出した。表の最後の 2 行で、同じ `#…_0_0#thsep#` が
  >   `1,235` と `1,234,567` になった。
  > - 1234567mm を `#m_0_0#` で引くと 1234.567 → **`1235`**、`#cm_0_0#` で引くと 123456.7 →
  >   **`123457`** と繰り上がった。上の表（#196）の `#cm_0_1#` → `123cm` には当初「切り捨て」と
  >   書いていた（123.4 は丸めても切り捨てても `123` で、あの 1 行だけでは区別が付かなかった）。

**修飾子を置く位置**
[issue #190](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/190)
で**実機確認済み**（VW 2026 / mac。`probes/runtime/datatag-formula-modifier-placement/`。
`MajorDepth` = **1234**、`DialogStartElevation` = **−872** で引いた。値は実行ログそのまま）。

| 入れた式 | 出た値 | 読み |
| --- | --- | --- |
| `#…#MajorDepth#` | `1234` | 基準 |
| `#…#MajorDepth##sign#` | **`+1234`** | フィールドの直後＝**効く** |
| `#…#MajorDepth##sign#*1` | **`+1234`** | **直後に置けば、その後ろで演算してよい** |
| `#…#MajorDepth##sign#+1000` | **`+2234`** | **符号は演算の結果に付く**（1234 ＋ 1000 に `+`） |
| `#…#MajorDepth##sign#" mm"` | **`+1234 mm`** | 後ろに文字列を並べてもよい |
| `#…#MajorDepth#*1` | `1234` | 基準（演算だけ） |
| `#…#MajorDepth#*1#sign#` | `1234` | **式の後ろでは効かない**（`+` が付かない） |
| `#…#MajorDepth#*1#t190nosuch#` | `1234` | **知らない修飾子と 1 文字も違わない** |
| `#…#MajorDepth#*1##sign#` | **`1234sign`** | 式の後ろで `#` を 2 つにすると**文字が出る** |
| `#…#MajorDepth#*1##t190nosuch#` | `1234t190nosuch` | 同じ |
| `#…#MajorDepth#+1000#sign#` | `2234` | 加算でも同じ（効かない） |
| `#…#DialogStartElevation#*-1` | `872` | 基準（**負を演算で正に変えた**） |
| `#…#DialogStartElevation#*-1#sign#` | `872` | **効かない**（正の値なのに `+` が付かない） |
| `#…#DialogStartElevation#+2000#sign#` | `1128` | 同じ |
| `#IPZL#-872##sign#` | **`-872sign`** | **issue に挙がっていた綴りは誤り**（`sign` が文字で出る） |
| `(#…#MajorDepth#*1)#sign#` | `(1234)` | 括弧の後ろでも効かない |
| `" ("#…#MajorDepth#")"#sign#` | `` (1234)`` | **式全体の末尾**に置いても効かない |
| `1200+34#sign#` | `1234` | フィールドを含まない式でも効かない |
| `1234#sign#` | **空文字列** | **数値リテラルの直後に `#…#` を置くと式が壊れる** |

- フィールドの直後で `#` 1 つ／式の後ろで `#` 2 つにした綴りは、修飾子の名前が文字として出た。
- 式の後ろでは、知らない修飾子（`#t190nosuch#`）は消え、`#sign#` を置いた値は無印・知らない
  修飾子と 1 文字も違わなかった。

**条件式 `値@条件:代替`**

| 入れた式 | 出た値 |
| --- | --- |
| `#IPZL#@#IPZL#<>0:"ゼロ"` | `-2699`（条件が真なので左の値） |
| `" ("@#IPZL#<>0:""#IPZL##thsep##sign#@#IPZL#<>0:""")"@#IPZL#<>0:""` | `` (-2699)`` |

**条件式で `+` を足す**
[issue #190](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/190)
で**実機確認済み**（同じ走行。`MajorDepth` = 1234、`DialogStartElevation` = −872）。

| 入れた式 | 出た値 |
| --- | --- |
| `"+"@#…#MajorDepth#>0:""` | `+`（条件が真なので左の値） |
| `"+"@#…#MajorDepth#>0:""#…#MajorDepth#` | **`+1234`** |
| `"+"@#…#DialogStartElevation#>0:""#…#DialogStartElevation#` | **`-872`**（負には付かない） |
| `"+"@#…#DialogStartElevation#+2000>0:""#…#DialogStartElevation#+2000` | **`+1128`**（**条件にも式が書ける**） |
| `"+"@(#…#DialogStartElevation#+2000)>0:""(#…#DialogStartElevation#+2000)` | `+(1128)`（括弧は文字で出る。上記） |
| `"+"@#…#MajorDepth#>=0:""` | `+`（`>=` も効く） |
| `"-"@#…#DialogStartElevation#<0:""` | `-`（`<` も効く） |

**知らない `#…#`**: `#LAYERELEVATION#` / `#STORYELEVATION#` / `#LAYERNAME#` / `#Z#` は**どれも空**
（エラーにはならない）だった。この走行で「レコードのフィールドでない綴り」として値を返したのは
`#IPZL#` だけだった。

### ワークシート式モードと、高さを返す関数の候補の実測

`SetFormula(hText, 式, /*isWorksheetFormula=*/true)` で入れた。書いた式は先頭に `=` が付いて
保存された（`GetFormula` の読み戻しが `=1+1` になる）。

| 入れた式（ws=1） | 出た値 |
| --- | --- |
| `1+1` | `2` |
| `=1+1`（自分で `=` を付けても同じ） | `2` |
| `'StructuralMember'.'StartElevation'` | `0` |
| `'StructuralMember'.'StartElevation'+872` | `872`（**レコード参照と演算が混ぜられる**） |
| `Z` / `IPZL` / `LAYERELEVATION` | **入れた綴りがそのまま**（＝そんな関数は無い） |

- 裸の `IPZL` は文字列 `IPZL` に評価された。
- `ISDK::ExecWSExpression` で引いた結果も同じだった（[調査の作法](../Findings/Investigation%20Techniques.md)
  「ワークシート式は…」）。そちらで引いた候補:

  | 引いたもの | 結果 |
  | --- | --- |
  | `HEIGHT` / `Height` / `HEIGHT()` / `LENGTH` / `Length` / `TOPBOUND` / `BOTBOUND` | **数値を返す（実在）** |
  | `LAYER` | **レイヤ名**を返す（`T187-TPL-PLAN-T187B`） |
  | `STORY` | **階名**を返す（`T187-PLAN`） |
  | `CONCAT('a','b')` → `ab` / `ROUND(872.4)` → `872` / `IF(1=1,'a','b')` → `a` / `ABS(-872)` → `872` | **実在** |
  | `'レコード'.'フィールド'` | 値を返す（`'StructuralMember'.'MemberID'` → `B14`） |
  | `Z` `ZHEIGHT` `ELEVATION` `TOPELEVATION` `BOTELEVATION` `BOTTOMELEVATION` `IPZL` `IPZ` `IPX` `IPY` `LAYERNAME` `LAYERELEVATION` `LAYERZ` `LAYERDELTAZ` `LAYERCUTPLANE` `STORYNAME` `STORYELEVATION` `LEVELNAME` `LEVELELEVATION` `TOPBOUNDOFFSET` `BOTBOUNDOFFSET` | **すべて自分自身の文字列＝存在しない** |
  | `Z()` / `NUMTOSTR(0,872)` | コンパイルが通らない（`InvalExpr`） |
  | `OBJECTDATA('StructuralMember','StartElevation')` | コンパイルは通るが**実行が `false`** |


## 式から読める高さの実測

[issue #187](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/187) で
**実機確認済み**（VW 2026 / mac。プローブ 2 本・計 8 回走らせた
——`probes/runtime/datatag-formula-grammar/` を 5 版、`probes/runtime/datatag-height-tokens/` を
3 版。以下の値は実行ログそのまま）。舞台は**現場の形**——伏図レイヤ（高さ **2699**）に
置いた構造材を、**別の階**のレベル `T187-FL`（絶対Z **3571**）へストーリバウンドで
繋いだもの。欲しい注記は `(2FL -872)` で、**−872 ＝ 2699 − 3571**。

### 読める / 読めないの一覧

| 引いたもの | 出た値 | 動かすと（+1000） |
| --- | --- | --- |
| **`#IPZL#`** | **−2699** | **−1699**（**追随する**） |
| `#StructuralMember#.#StartElevation#` / `#EndElevation#` | `0` | `0`（**この舞台では**常に 0。[訂正](#傾斜材の両端の天端の実測ipzs--ztbbs)） |
| `#StructuralMember#.#DialogStartElevation#`（バウンドだけ書いた本） | **`0`** | `0` |
| `#StructuralMember#.#DialogStartElevation#`（**パラメータへ書いた本**） | **−872** | **−872 のまま（追随しない）** |
| `#StructuralMember#.#DialogStartElevationReference#`（バウンドを書いた本） | **`T187-FL [上階]`** | 同じ（保たれる） |
| 同（パラメータへ書いた本） | **空** | 空 |
| `#LAYERELEVATION#` / `#STORYELEVATION#` / `#LAYERNAME#` / `#Z#` | **すべて空** | — |
| ワークシート式の高さ関数（候補 **37 本**） | **すべて自分自身の文字列＝存在しない** | — |

- `#IPZL#`: 挿入点の絶対Z が 0 でレイヤが 2699 の部材で **−2699**。+1000 動かすと **−1699**。
  この走行で、動かして追随した綴りはこれだけだった（`#IPZS#` などはまだ試していなかった。下記）。
  - 素のデザインレイヤ（[issue #194](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/194)）:
    `CreateLayer` で作った素のデザインレイヤに `VWFC::VWObjects::VWLayerObj::SetElevation(872)` を
    与え、そこで作った部材（ローカルZ 0）で引くと `#IPZL#` は **`0`**、同じ走行の `#IPZ#`（絶対Z）は
    **`872`**。部材を +300 持ち上げると `#IPZ#`=`1172` / `#IPZL#`=**`300`**。
    - > **【訂正の記録】** 当初「`SetElevation` で与えた高さを `#IPZL#` は拾っていない」と
      > 書いていた（[#190](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/190)
      > の走行で `#IPZL#` が `0` だったため）。そのときは `#IPZ#` を読んでいなかった。
- `Dialog*Elevation`: `VWParametricObj::SetParamReal` で −872 を書くとタグは **−872 を描いた**。
  部材を +1000 動かしても −872 のまま、バウンドの `fOffset` を −1872 へ書き換えて
  解決Zが 2699 → 1699 に変わっても −872 のままだった。
  **VW へ戻った後に読み直しても同じ**（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。変えた後に更新手順を掛け直した値も、
  何も触らずに undo イベントを閉じた後・戻って数十秒置いた後に読んだ値も −872。
  タグの描いた文字も同じ）。
- `SetObjectStoryBound` で書いたバウンド（`fOffset=−872`）の部材で、`Dialog*Elevation` の式は `0`、
  `#DialogStartElevationReference#` は `T187-FL [上階]` を返した。バウンドとパラメータの両方を
  書いた部材 H では、名前 `T187-FL [上階]` と数値 −872 が同時に出た。
- > **【訂正の記録】（[#187](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/187) 再開）**
  > 当初「レイヤ高さ・ストーリ高さ・レベル高さを数値で返す口は、式にもワークシート式にも
  > 無い」と書いていた。測ったのは推測した綴り（`#…#` 記法 4 本＋ワークシート式 37 本）だけ
  > だった。VW の「タグフィールドの定義」ダイアログ（`IDataTagSupport::ShowDefineTagFieldDlg` が
  > 開くもの）には次の高さが並んでいた:
  >
  > | 見えている名前 | 綴り |
  > | --- | --- |
  > | 挿入点の高さ (Z)_**ストーリの高さ** | **`#IPZS#`** |
  > | 挿入点の高さ (Z)_レイヤの高さ | `#IPZL#` |
  > | バウンディングボックス**上面**の高さ (Z)_ストーリ / レイヤ / 基準平面 | **`#ZTBBS#` / `#ZTBBL#` / `#ZTBBG#`** |
  > | バウンディングボックス底面の高さ (Z)_ストーリ / レイヤ / 基準平面 | **`#ZBBBS#` / `#ZBBBL#` / `#ZBBBG#`** |
  >
  > 上の表の「読める / 読めない」の時点では `#IPZS#` と `#ZTBBS#` は試していなかった
  > （その後の実測は下記）。
- 推測した綴り——`#…#` 記法で `#LAYERELEVATION#` / `#STORYELEVATION#` / `#LAYERNAME#` / `#Z#` の
  4 本、ワークシート式で 37 本——は全部外れた。ワークシート式で取れたのは名前（`LAYER` /
  `STORY`）だけだった（上記「ワークシート式モードと、高さを返す関数の候補の実測」）。

### 高さの綴り（量 × 基準）の実測

**実機確認済み**（VW 2026 / mac。`probes/runtime/datatag-height-tokens/`。
**階とレイヤの高さをわざと 40 ずらした舞台**で測った——階 `T187-PLAN` の高さ **2699**、
その中のレベルの階内相対Z **−40** ＝ **レイヤの絶対Z 2659**。ずらしたのは `#IPZS#` と `#IPZL#` を
見分けるため）。

| 末尾 | 基準 | 実測（押し出し。絶対Zで上面 **700** / 底面 **100**） |
| --- | --- | --- |
| **`G`** | **基準平面＝絶対Z そのもの** | `#ZTBBG#` → **`700`** / `#ZBBBG#` → **`100`** |
| **`L`** | **レイヤ**（絶対Z − レイヤの絶対Z 2659） | `#ZTBBL#` → **`-1959`** / `#ZBBBL#` → **`-2559`** |
| **`S`** | **ストーリ**（絶対Z − **階の高さ** 2699） | `#ZTBBS#` → **`-1999`** / `#ZBBBS#` → **`-2599`** |

同じ規則が挿入点の側にもそのまま効く（部材の絶対Z **0**、同じレイヤ）:

| 綴り | 意味 | 出た値 | +1000 動かすと |
| --- | --- | --- | --- |
| `#IPZ#` | 挿入点 Z（**絶対Z**） | `0` | **`1000`** |
| **`#IPZS#`** | 挿入点の高さ (Z)_**ストーリの高さ** | **`-2699`**（＝0 − 2699） | **`-1699`** |
| `#IPZL#` | 挿入点の高さ (Z)_レイヤの高さ | `-2659`（＝0 − 2659） | `-1659` |

- `#IPZS#` は `2699`（階の高さ）を引いており、レベルの絶対Z `2659` ではなかった。
- 部材を動かすと 3 つとも同じだけ動いた（差は一定）。
- パスとバウンドだけで作った構造材では `#ZTBB…#` / `#ZBBB…#` が `±1.7976931348623e308`
  （`DBL_MAX`。何も描いていない PIO の反転した空矩形と同じ値。
  [調査の作法](../Findings/Investigation%20Techniques.md)）を返した。
  - 3D パス（`Create3DPoly`）の構造材の `#ZTBBS#` / `#ZTBBG#` が ±1.79e308、`#BX#`
    `#BY#` `#BZ#` が空、`#StartElevation#` / `#EndElevation#` が 0 なのは、**VW へ戻った
    後に読み直しても同じ**だった（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。2D パスの対照は 253.71 / 3824.71 / 2699 /
    3531 を返し、こちらも変わらない）。
- `#ST#` は、他階のレベルへバウンドしている部材でも、自分のレイヤが属する階の名前
  （`T187-PLAN`）を返した。
- **`#ST#` / `#STLT#` / `#L#` / `#C#` は名前を返す**（`T187-PLAN` / `T187-PLAN-L` /
  `T187-TPL-PLAN-T187B` / `一般`）。
- **数値に単位が付く綴りがある**（`#AREA#` → `0m2`、`#ROTATIONX#` → `0°`）。
- 空が返った綴り（読み取り違いか、値が無い）: `#BX#` `#BY#` `#BZ#` `#LD#` `#CD#` `#N#`
  `#SUBFAREA#` `#MATERIAL#` `#OSTYLE#` `#SYMBOL#`。対照の `#T187NOSUCHTOKEN#` も空。

### 連結と演算を混ぜた式の実測（括弧で括った場合を含む）

> **【訂正の記録】** 当初「`" (2FL "#IPZL#-872")"` で連動する注記が作れる」と書いていた
> （連結と演算を別々に確かめ、組み合わせは確かめていなかった）。実機で引いたら空文字列だった。

実測（12 通り引いた）:

| 入れた式 | 出た値 |
| --- | --- |
| `#IPZL#` | `-1659` |
| `#IPZL#-872` | `-2531`（**演算だけなら通る**） |
| `" (2FL "#IPZL#")"` | `` (2FL -1659)``（**連結だけなら通る**） |
| **`" (2FL "#IPZL#-872")"`** | **空**（混ぜると壊れる） |
| **`" (2FL "(#IPZL#-872)")"`** | **`` (2FL (-2531))``**（**括弧で括れば通る。ただし括弧が出る**） |
| `(" (2FL "#IPZL#-872")")` | 空（全体を括ってもだめ） |
| `#IPZL#-872" (2FL)"` | 空（演算を先にしてもだめ） |
| `"("#IPZL#-872")"` | 空 |
| `" (2FL "#IPZL#"-872)"` | `` (2FL -1659-872)``（**`-872` が文字として出る**） |


### 連動する注記の式 `" (2FL "#IPZS#")"` の実測

**実機確認済み**（VW 2026 / mac。**Findings の注記の式そのものを走らせた**実行ログ。
階 `T187-2F` の高さ **3571**、その階のレイヤに**絶対Z 2699** で置いた構造材）。

| 入れた式 | 出た値 | +1000 動かすと |
| --- | --- | --- |
| `#IPZ#` | `2699`（絶対Z） | `3699` |
| **`#IPZS#`** | **`-872`**（＝2699 − 3571） | **`128`**（＝3699 − 3571） |
| `#ST#` | `T187-2F` | 同じ |
| **`" (2FL "#IPZS#")"`** | **`` (2FL -872)``** | **`` (2FL 128)``** |

- 同じ部材を**階に属さないデザインレイヤ**（`GetStoryOfLayer` が `nil`）へ置くと、`#IPZS#` は
  **`2699`**（＝絶対Z そのもの）を返し、動かすと `3699` になった。`#ST#` は**空**だった。
- この調査で作った構造材は（パスとバウンドだけでは）描かれず、`#ZTBBS#` は `±1.79e308` の
  ままだった（実体のある部材での実測は下記「傾斜材の両端の天端」）。
- `#ST#` が返したのは取り込みが付けた階の名前（`T187-2F`）だった。
- `AddObjectToContainer`（[#194](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/194)）:
  高さ 900 のレイヤで作った部材を高さ 1100 のレイヤへ移すと、絶対Z は 900 のままで
  `#IPZL#` は **`-200`**。その後そのレイヤを 1300 にしても絶対Z は 900 のままで
  `#IPZL#` は **`-400`** になった（[Layers and Stories](../Findings/Layers%20and%20Stories.md)
  「レイヤの高さ」）。

### 傾斜材の両端の天端の実測（`#IPZS#` / `#ZTBBS#`）

**実機確認済み**（VW 2026 / mac。[issue #196](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/196)。
`probes/runtime/datatag-slope-top-elevation/` を **3 版・計 3 回**走らせた。舞台は**プラグインが実際に
作る形**——パスは平面（Z=0）の 2 点、高さは始端・終端とも `SetObjectStoryBound` だけ、
断面は **105 × 240** の**天端中央基準**（`AxisAlign`=`1`）、端は直切り
（`StartCondition` / `EndCondition`=`3`）、材はストーリのレベルから作ったレイヤ。
階 `T196-2F` の高さ **3571**、レベルの階内相対Z **0**。以下の値は実行ログそのまま）。

部材 S（**始端が低い端**。バウンド offset −872 → −40 ＝ 解決Z **2699 → 3531**）:

| 綴り | 出た値 | 何を指しているか |
| --- | --- | --- |
| `#IPZ#` | `2699` | **始端**の天端の絶対Z |
| **`#IPZS#`** | **`-872`** | **始端**の天端（階基準）＝この材では**低い端** |
| **`#ZTBBS#`** | **`-40`** | **高い端**の天端（階基準） |
| `#ZTBBG#` | `3531` | 同・絶対Z。**`GetObjectCube` の上面と一致** |
| `#ZBBBG#` | `2453.86` | 外接の底面（絶対Z）。低い端の天端 − **245.14** |
| `#ZBBBS#` | `-1117.14` | 同・階基準 |

- バウンドの解決Z（**3531**）・`GetObjectCube` の上面（**3531**）・`#ZTBBG#`（**3531**）の
  3 つが一致した。両端 −1000 で `#ZTBBS#`=**−1040**、片端だけ +128 へ変えて `#ZTBBS#`=**`128`**
  （どちらもそのときの高い端の天端）。
- 外接の底面は **低い端の天端 − せい ÷ cos(勾配)** に一致した:

  | 水平距離 | 高低差 | cos | せい ÷ cos | 低い端の天端 | 外接の底面（実測） |
  | --- | --- | --- | --- | --- | --- |
  | 4000 | 832 | 0.97905 | **245.1367** | 2699 | **2453.8633** |
  | 4000 | 2000 | 0.89443 | **268.3282** | 1699 | **1430.6718** |

  水平材では せい ぶん下がった（2699 → **2459**、実測）。

注記の式（連結だけで演算を混ぜない形。
[上記の「連結と演算を混ぜた式の実測」](#連結と演算を混ぜた式の実測括弧で括った場合を含む)）を引いた:

| 入れた式 | 傾斜材 S | 水平材 N |
| --- | --- | --- |
| `" (2FL "#IPZS#"~"#ZTBBS#")"` | `` (2FL -872~-40)`` | `` (2FL -872~-872)``（**同じ値が 2 度出る**） |
| **下の 1 本**（条件で畳む） | **`` (2FL -872~-40)``** | **`` (2FL -872)``**（**畳まれる**） |

```
" (2FL "#IPZS#"~"@#ZTBBS#<>#IPZS#:""#ZTBBS#@#ZTBBS#<>#IPZS#:""")"
```

- `"~"@#ZTBBS#<>#IPZS#:""` は傾斜材で `~`、水平材で**空**を返した（それまで条件に書いたのは
  定数との比較だけだった。上記「条件式」）。
- 始端が高い端の材（offset −40 → −872）では `#IPZS#` が **`-40`**（高い端）、`#ZTBBS#` も `-40` で、
  注記は `` (2FL -40)`` になった。
- > **【訂正の記録】** 上記「読める / 読めない」では `#StructuralMember#.#StartElevation#` /
  > `#EndElevation#` を `0`（常に 0）と書いていた。
  > [#187](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/187)
  > の走行は 3D のパスで作っており、実体も高さも入っていなかった
  > （[Parametric Objects](../Findings/Parametric%20Objects.md)「構造材のパスは 2D で渡す」）。
  > 高さの入った部材では両端の天端の絶対Zを返した——S で `2699` / `3531`、
  > 始端が高い材で `3531` / `2699`、offset を動かすと追随した。
- `#DialogStartElevation#` / `#DialogEndElevation#` は高さが入っても `0` のままだった。
- この舞台は**レベルの階内相対Zが 0** なので、`#ZTBBS#` と `#ZTBBL#`（`#IPZS#` と
  `#IPZL#` も）が同じ値を返した（`S` と `L` の違いは
  [#187](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/187)
  の 40 ずらした舞台で測った。上記「高さの綴り（量 × 基準）の実測」）。

### `#IPZS#` の直後に置いた `#sign#` の実測

**実機確認済み**（VW 2026 / mac。[issue #196](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/196)。
同じ走行。水平材を 3 本、バウンドの offset で **+128 / 0 / −872** に置いて同じ綴りを
引いた）。

| 入れた式 | 正（+128） | 0 | 負（−872） |
| --- | --- | --- | --- |
| `#IPZS#` | `128` | `0` | `-872` |
| **`#IPZS##sign#`** | **`+128`** | **`±0`** | `-872` |
| `#IPZS#sign#`（`#` 1 つ） | `128sign` | `0sign` | `-872sign` |
| `#IPZS##t196nosuch#`（知らない修飾子） | `128` | `0` | `-872` |
| **`" (2FL "#IPZS##sign#")"`** | **`` (2FL +128)``** | `` (2FL ±0)`` | `` (2FL -872)`` |
| `"+"@#IPZS#>0:""#IPZS#`（条件式） | **`+128`** | **`0`** | `-872` |
| `" (2FL ""+"@#IPZS#>0:""#IPZS#")"` | **`` (2FL +128)``** | **`` (2FL 0)``** | `` (2FL -872)`` |

- [#190](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/190) で測ったのは
  レコードのフィールドだけだった。
- `#IPZS##mm_0_1#sign#` は 0 で **`±0mm`**、`#IPZS##mm_0_1#usp#thsep#sign#` は **`±0 mm`** だった
  （上記「単位・精度の修飾子」）。

注記の 2 つの形を走らせた実行ログ（傾斜材 S は −872 → −40、水平材 P は +128、水平材 N は −872）:

| 入れた式 | 傾斜材 S | 水平材 P（正） | 水平材 N（負） |
| --- | --- | --- | --- |
| **符号なし**（下） | `` (2FL -872~-40)`` | `` (2FL 128)`` | `` (2FL -872)`` |
| **符号あり**（下） | `` (2FL -872~-40)`` | **`` (2FL +128)``** | `` (2FL -872)`` |

```
符号なし: " (2FL "#IPZS#"~"@#ZTBBS#<>#IPZS#:""#ZTBBS#@#ZTBBS#<>#IPZS#:""")"
符号あり: " (2FL "#IPZS##sign#"~"@#ZTBBS#<>#IPZS#:""#ZTBBS##sign#@#ZTBBS#<>#IPZS#:""")"
```

- 単位の修飾子を入れると `` (2FL -872mm~-40mm)`` のように項ごとに `mm` が付いた。
