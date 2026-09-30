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
