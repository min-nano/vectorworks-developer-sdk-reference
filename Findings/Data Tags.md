# データタグ

データタグ（`Data Tag` PIO）を SDK から生成し、スタイルを使わずに per-instance で
中身（タグレイアウト）を組むときの実測。

- **スタイル無しでもタグは組める**が、間接段（スタイル）を挟む場合はシンボル定義の
  サブタイプ（`SetSymbolDefSubType`）とパラメータ対応表（`SetAllPluginStyleParameters`）が
  要り、そのどれかが漏れると実機で「タグは出るのに中身が空」になる。per-instance に直接
  組む方が壊れにくい。
- **タグレイアウトはタグ自身のプロファイルグループ**（`GetCustomObjectProfileGroup` /
  `SetCustomObjectProfileGroup`。VW2020 で足された `…InAux` の入り口もあるので両方見る）。
  中身は**式を持たせたテキスト**（`IDataTagTextLinkSupport::SetIsLinked` / `SetFormula`）で、
  **フィールドラベルはテキストの名前ではない**（手で作ったタグのテキストには名前が無い）。
  生成直後のタグが既定で持っているロクスは取り除き、手作りと同じ並びに揃える。
- **中身を入れてから渡す。** 空のグループを先に `SetCustomObjectProfileGroup` へ渡して
  後から足すと、VW が渡した時点でグループを複製していた場合に足したテキストが迷子になる
  （実機で「オブジェクトは出るのにタグレイアウトが空」として現れた）。渡した後は実際に
  持っているものを取り直し、複製されていればこちらのグループを消す。
- **組んだら `IDataTagSupport::UpdateUserDefinedTextsUIDs` をタグへ呼ぶ**（SDK のコメント
  どおり「データタグ**または**データタグスタイル」に使える）。これが無いとタグが式を拾わず、
  値が空のまま出る。
- **関連付けはレイアウトを組むより先。** タグの本文は関連付け先のレコードから来るので、
  相手を決めてから中身を組み、最後に `UpdateDataTag` で流し込む。
- **タグは指定した挿入点に留まらない。** しかも**タグの実寸はレイアウトの中身が決めるので、
  逃がす量も描くまで分からない**。したがって**置いた後に `GetObjectBounds` で実位置と実寸を
  測って動かす**のが最終位置を決める唯一確実な手。VW がどこへ落とすかはビューポートに
  よってもばらばらで、当てにできない。
- **「関連付け先からの相対で置く」は成り立たない（打ち切った調査）。** 「VW がタグを
  関連付け先へ吸着させるので、そこからの相対で置けば座標系を知らなくてよい」という方式を
  2 度試して外した（基準点の当て推量 → 実測からの較正）。較正の残差が大きく、どの基準点
  候補も当てはまらない＝**前提そのものが成り立っていない**。実際の挙動は「高さは合っていて
  横だけ一定量ずれ、タグ同士の間隔は対象の間隔に比例」（＝純粋な平行移動）で、上記の
  「置いた後に測って動かす」が正解だった。
- **注釈へ後から足した図形のクラスはビューポートで非表示のまま。** ビューポートの設定は
  タグを置く前に走っているので、置いた後に全クラスを表示へ戻して再更新する。
- **描画属性はクラス属性に従わせる場合、タグ本体だけでは足りない**（`SetObjectClass` ＋ 7 つの
  `Set*ByClass`。**`SetClassByName` / `SetAllAttributesByClass` という名前は SDK に無い**——一括で
  済ませる口も無い。[Attributes and Classes](Attributes%20and%20Classes.md)）。**タグレイアウトの中のテキストにも要る**——レイアウトの中身は
  本体のクラスを継がないので、本体だけに与えると文字が既定クラスのまま残る。文字スタイルを
  当てた**後**にクラスを与える（書体・大きさは文字スタイル、色・線の太さはクラスが受け持つ）。
- **図面ラベル（`Drawing Label2`）のレイアウトは、置き場も式の綴りも同じだが組み方が違う。**
  式は `#Drawing Label2#.#Title#` のように同じ `#レコード#.#フィールド#` だが、
  **`IDataTagTextLinkSupport` が効かない**（`IsSupported` が `false`）ので `SetFormula` で
  持たせられず、**既定のレイアウトのテキストを複製する**しかない
  （[Drawing Labels](Drawing%20Labels.md)）。

## タグフィールドの式——文法は 2 系統あり、綴りは SDK に 1 文字も書いていない

タグレイアウトのテキストが持つ式（`IDataTagTextLinkSupport::SetFormula` / `GetFormula`）に
ついて、**先に押さえること**。

- **文法は 2 系統ある。** `SetFormula` の第 3 引数が `isWorksheetFormula`（既定 `false`）で、
  読み戻しは `GetIsWorksheetFormula`。つまり同じ 1 本のテキストを、**独自の `#…#` 記法**と
  **ワークシート式**のどちらでも持たせられる。【ヘッダ根拠】
- **`#…#` 記法の綴りは SDK のどこにも無い。** `IPZL|thsep|#sign#|DataTagField|TagField` を
  SDKLib（ヘッダ＋同梱の VWFC 実装ソース、961 ファイル全部）へ掛けたときのヒットは
  **`ShowDefineTagFieldDlg()` の 1 行だけ**だった（`sdk-grep` 実測）。**数値の修飾子の一覧も
  条件式の形も VW 本体側**にあるので、**`sdk-grep` では 1 件も出ない**——ここは実機で
  総当たりするしかない。「ヘッダに無いから無い」と読み違えないこと。
- **`#レコード#.#フィールド#` のレコードは PIO の universal 名、フィールドはその
  パラメータ名。** 構造材なら `#StructuralMember#.#StartElevation#`。パラメータの全件は
  [Parametric Objects](Parametric%20Objects.md)「構造材（`StructuralMember`）のパラメータ表」
  にある（始端・終端の高さらしい欄は索引 28〜33 の
  `DialogStartElevationReference` / `DialogStartElevation` /
  `DialogEndElevationReference` / `DialogEndElevation` / `StartElevation` / `EndElevation`）。
- **`#IPZL#` はレコードのフィールドではない。** 構造材のパラメータ**181 件の表に `IPZL` は
  無い**。`#…#` が 1 対しか無い綴りは、レコード参照とは**別の名前空間**（関数）である。

### 式の評価結果は機械で読み戻せる——目視は要らない

式を入れて「何が出たか」を確かめるのに、結果ダイアログの絵は要らない。**文字列で読み戻す
口が 2 つある**ので、プローブのログだけで判定できる。

- **`IDataTagSupport::GetDataTagExtractedData(hTag, outArrExtractedData, txtLabel = "")`**
  ——タグが抽出した結果を「ラベル → 値」の対
  （`TXStringSTLPairArray` ＝ `std::vector<std::pair<TXString, TXString>>`）で返す。【ヘッダ根拠】
- **タグが描いたテキスト**——`FirstMemberObj` / `NextObject` でタグの生成物を走査し、
  型が `kTextNode` のものを `VWFC::VWObjects::VWTextBlockObj::GetText` で読む。
- ワークシート式のほうは、タグを介さずに**ハンドルへ直接**評価できる
  （`ISDK::CompileCriteriaExpression` ＋ `ISDK::ExecWSExpression`。
  [調査の作法](Investigation%20Techniques.md)「ワークシート式は…」）。**コンパイル誤りが
  enum で返る**ので、候補の識別子を総当たりするときは**まずこちらで引く**。

### `#…#` 記法の文法——**実測**（VW 2026 / mac）

[issue #187](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/187) で
**実機確認済み**（`probes/runtime/datatag-formula-grammar/`。値は実行ログそのまま。
この走行では `#IPZL#` が **−2699**、`#StructuralMember#.#MajorDepth#` が **600**）。

**四則演算は効く。括弧も演算の括弧として読まれる。**

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

- **`/` の結果は図面の単位設定の書式で出る**（この走行では `-1349 1/2`）。
  **小数が欲しければ式の側では決められない**ので、割り算を式に入れるときは
  この見え方を前提にする。

**文字列は二重引用符で囲む。連結は「並置」だけで、`+` も `&` も連結ではない。**

| 入れた式 | 出た値 |
| --- | --- |
| `" (2FL "` | `` (2FL `` |
| `" (2FL "#IPZL#")"` | `` (2FL -2699)``（**並置で連結される**） |
| `(2FL )`（引用符なし・括弧を含む） | **空文字列**（式全体が値を出せなくなる） |
| `× #IPZL#`（引用符なし・演算子でない文字） | `× -2699`（**通る**） |
| `"a"+"b"` | **`a+b`**（`+` が文字として出る＝連結ではない） |
| `"a"&"b"` | **`a&b`**（同じ。`&` も連結ではない） |

- **引用符なしで括弧を書くと、その式は空になる。** issue の見立て（「本文のまま出る」）
  とは違い、**何も出ない**。括弧付きの文字を出したいなら**必ず引用符で囲む**。

**数値の書式（修飾子）は `#フィールド##修飾子#` と二重の `#` で始め、以降は `#` を
共有して連ねる。**【実測】（`MajorDepth` = 600 で引いた）

| 入れた式 | 出た値 | 読み |
| --- | --- | --- |
| `#…#MajorDepth#` | `600` | 基準 |
| `#…#MajorDepth##sign#` | **`+600`** | **`#sign#` は効く**（`#` が 2 つ要る） |
| `#…#MajorDepth#sign#` | **`600sign`** | `#` が 1 つだと**修飾子として読まれず文字が出る** |
| `#…#MajorDepth##thsep#sign#` | **`+600`** | **`#` を共有して 2 つ連ねられる**（issue に挙がっていた綴りはこれ） |
| `#…#MajorDepth##t187nosuch#` | `600` | **知らない修飾子は黙って捨てられる**（エラーにならない） |
| `#…#MajorDepth#t187nosuch#` | `600t187nosuch` | 同じく `#` が 1 つだと文字が出る |
| `#…#MajorDepth##thsep#` | `600` | 3 桁なので区切る桁が無い（下記） |
| `#…#MajorDepth#*10#thsep#` | `6000` | **コンマが入らない** |
| `#…#MajorDepth#/7` | `85.71` | 演算の結果は小数で出る（`#IPZL#/2` が `-1349 1/2` になったのは元の値の単位の書式） |

- **`#sign#` は正の値に `+` を付ける。** 1 巡目に「修飾子はどれも効かない」と読んだのは、
  試した値（−2699）が既に `-` を持っていたから——**負の値で `#sign#` を試しても何も
  変わらない**。修飾子を確かめるときは**正の値で引く**こと。
- **`#` を 1 つにすると修飾子ではなく文字になる。** `#IPZL#thsep#` は `…thsep` と出る。
  **フィールドの閉じ `#` と修飾子の開き `#` が続いて `##` になる**のが正しい綴り。

**条件式 `値@条件:代替` は効く。入れ子にもできる。**

| 入れた式 | 出た値 |
| --- | --- |
| `#IPZL#@#IPZL#<>0:"ゼロ"` | `-2699`（条件が真なので左の値） |
| `" ("@#IPZL#<>0:""#IPZL##thsep##sign#@#IPZL#<>0:""")"@#IPZL#<>0:""` | `` (-2699)`` |

**知らない `#…#` は空文字列になる**（エラーにはならない）。`#LAYERELEVATION#` /
`#STORYELEVATION#` / `#LAYERNAME#` / `#Z#` は**どれも空**だった。
**`#IPZL#` だけが「レコードのフィールドでない綴り」として実際に値を返した。**

### ワークシート式モードは効く——ただし高さを返す関数は見つからない

`SetFormula(hText, 式, /*isWorksheetFormula=*/true)` は**そのまま効く**。書いた式は
**先頭に `=` を付けられて**保存される（`GetFormula` の読み戻しが `=1+1` になる）。

| 入れた式（ws=1） | 出た値 |
| --- | --- |
| `1+1` | `2` |
| `=1+1`（自分で `=` を付けても同じ） | `2` |
| `'StructuralMember'.'StartElevation'` | `0` |
| `'StructuralMember'.'StartElevation'+872` | `872`（**レコード参照と演算が混ぜられる**） |
| `Z` / `IPZL` / `LAYERELEVATION` | **入れた綴りがそのまま**（＝そんな関数は無い） |

- **ワークシート式では `#IPZL#` は使えない。** `IPZL` は**データタグの `#…#` 記法だけの
  綴り**で、ワークシート式の関数ではない（裸の `IPZL` は文字列 `IPZL` に評価される）。
- **`ISDK::ExecWSExpression` で引いた結果も同じ**（[調査の作法](Investigation%20Techniques.md)
  「ワークシート式は…」）。タグを組まずに同じ問いを引けるので、候補の総当たりは
  そちらが速い。実在すると分かった関数と、外れた候補:

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

- **したがって「レイヤの高さ」「ストーリの高さ」「レベルの高さ」を数値で返す関数は、
  ここまでの総当たりでは 1 つも見つかっていない。** 取れるのは**名前**（`LAYER` / `STORY`）
  までである。
