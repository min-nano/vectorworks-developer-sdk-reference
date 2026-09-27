# 寸法（直線寸法・寸法規格）

平面図・断面のビューポートへ**寸法を自動で入れる**ための調査（[#129](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/129)）。
寸法そのものを作る口と、寸法の見え方を決める**寸法規格**（dimension standard）の
扱い方をまとめる。

## まず結論

- **寸法を作る口は `ISDK::CreateLinearDimension` の 1 本だけ。** 直線寸法（水平・垂直・
  傾き・ordinate）はすべてこれで作り、`dimType` で種類を選ぶ。
- **`textOffset` は使われない。** ヘッダに `textOffset is CURRENTLY UNUSED` と明記されて
  いる。文字の位置は作った後に `ovDimTextOffsetInCurrUnits`(44) 等で動かす。
- **`dir` は `(0,0)` を渡せばよい。** 「`p2` - `p1` を正規化した向き」を自分で計算して
  渡してもよいが、`(0,0)` なら VW が同じものを計算する。
- **「寸法スタイル」という名前付きリソースは（VW 2026 の SDK には）無い。**
  シンボル定義として持つ各種スタイル（[Symbols](Symbols.md) の `subType` 表）にも、
  `BuildList` で引ける名前付きリソースにも、寸法のスタイルは無い。あるのは**文書が
  配列で持つ寸法規格**（`dimStandardNode = 39` … "Holds an array of DimStandardType"）
  だけで、**index で指す**。
- **寸法規格の index は組み込みが 1〜9、カスタムが 0〜-8。** 名前は
  `GetDimensionStandardVariable(index, dimStdstandardName, …)` で引く。
- **規格の寸法値は「用紙インチ」**（`page inches`）で定義されている。つまり補助線の
  長さ・文字と寸法線の間隔などは**縮尺に依らず紙の上の大きさ**で決まる。

## `CreateLinearDimension` の引数

```cpp
virtual MCObjectHandle CreateLinearDimension(
    const WorldPt& p1, const WorldPt& p2,
    WorldCoord startOffset, WorldCoord textOffset,
    const Vector2& dir, short dimType) = 0;
```

ヘッダ（`Include/Kernel/API/APIBase.Legacy.Defs.h:2095`。`ISDK.h:1059` の実体）の説明:

> Creates a linear dimension object. `p1` and `p2` are the two endpoints of the distance
> to be measured. `startOffset` is the distance from `p1` to the dimension line.
> `textOffset` is **CURRENTLY UNUSED**. `dir` is the normalized difference between `p2`
> and `p1`. If `dir` is passed in as (0,0), this value is calculated automatically.
> `dimType` indicates whether to allow only horizontal and vertical dimension lines,
> whether to rotate the dimension line to have the same direction as the vector between
> `p1` and `p2`, or whether to create an ordinate dimension.

| 引数 | 意味 |
| --- | --- |
| `p1` / `p2` | 測る 2 点。**測点そのもの**（寸法線の位置ではない） |
| `startOffset` | **`p1` から寸法線までの距離**（`WorldCoord` ＝ mm。[Parametric Objects](Parametric%20Objects.md)） |
| `textOffset` | **使われない。** 0 を渡す |
| `dir` | `p2` - `p1` を正規化した向き。`Vector2` は `WorldPt` の typedef。**`(0,0)` で自動計算** |
| `dimType` | 種類。下記 |

- **`Vector2` は `WorldPt` の別名**（`MathCoordTypes.h:329` の `typedef WorldPt Vector2;`）
  なので、`Vector2(0, 0)` と書ける。
- 作った寸法のオブジェクト型は **`dimHeaderNode = 63`**（`Objs.TDType.h:129`）。
  `GetObjectTypeN` はこれを返す。

## 寸法規格（dimension standard）

### index の体系

`GetDimensionStandardVariable` のヘッダ説明（`APIBase.Legacy.Defs.h:5126`）:

> Returns a property of a dimension standard. The index specifies which dimension to
> examine. **The nine built-in dimension standards use indexes 1 thru 9. Custom
> dimensions use indexes 0 thru -8.** The selector chooses which property will be
> returned … This function returns false if an invalid dimension index or field selector
> is specified.

| index | 中身 |
| --- | --- |
| `1` 〜 `9` | **組み込みの規格 9 つ**（リソース由来。変更できない） |
| `0` 〜 `-8` | **カスタム規格（利用者が作ったもの）最大 9 つ** |

- **カスタム規格の本数は `ISDK::NumberCustomDimensionStandards()`。**
- **カスタム規格だけが `SetCustomDimensionStandardVariable` で書ける。**
  組み込み（1〜9）へ書こうとしても効かない（ヘッダ: "cannot be changed with this
  function"）。
- カスタム規格を**作る**口は `ISDK::CreateCustomDimensionStandard(name)`。
  「アクティブな規格の値を既定として、その名前の新しいカスタム規格を作る」。
  **今回の用途（利用者が図面に持っている規格を名前で選ばせる）では使わない。**

### 一覧の取り方

名前は `dimStdstandardName = 25`（`MiniCadCallBacks.h:1833`。型は `Str255`＝文字列）で
引く。`TVariableBlock::GetTXString` で受ける。

```cpp
// index を総当たりし、名前が引けたものだけを一覧にする。
// 引けない index には GetDimensionStandardVariable が false を返す。
for (short index = 9; index >= -8; --index)
{
    TVariableBlock v;
    if (!gSDK->GetDimensionStandardVariable(index, dimStdstandardName, v))
        continue;            // その index に規格は無い
    TXString name;
    if (!v.GetTXString(name))
        continue;
    // index と name を一覧へ
}
```

- **`NumberCustomDimensionStandards()` の本数からカスタムの index を引き算で出さない。**
  カスタムの並びが 0 から詰まっているとは限らないので、**総当たりして
  `GetDimensionStandardVariable` の戻り値で判定する**のが確実。
- 規格のその他の項目（補助線の隙間・矢印の大きさなど）は `dimStd*` セレクタ
  （`MiniCadCallBacks.h:1809` 以降、1〜52 超）で引ける。**長さ系はすべて「用紙インチ」**
  で、範囲は `-2.0 〜 2.0` page inches と注記されている。

### 寸法へ規格を当てる

寸法 1 本ごとの規格は、オブジェクト変数の**どちらからでも**触れる
（`Include/Kernel/API/ObjectVariables.h`）。

| セレクタ | 番号 | 型 | ヘッダの説明 |
| --- | --- | --- | --- |
| `ovDimStandard` | 0 | `char`（`Sint8`） | 規格の index。**> 0 は組み込み、< 0 はカスタム** |
| `ovDimStandardName` | 27 | `TXString` | 規格の名前 |

- **`ovDimStandard` のヘッダは「0 は無効」と書いている**（"the result for an invalid
  value (zero, greater the the number of builtin standards, less than the number of
  custom standards) is undefined"）。**一方 `GetDimensionStandardVariable` の説明は
  カスタムの範囲に 0 を含める。** ここは食い違っているので、**index で書くより
  `ovDimStandardName`（名前）で書くほうが安全**。
- 利用者に「図面にある規格を名前で選ばせる」用途なら、**一覧を名前で出し、
  `ovDimStandardName` に書き戻す**のが素直。

## 連続寸法（チェーン寸法）

- **`ISDK::CreateChainDimension(h1, h2)` がある。** ヘッダ:
  「渡された 2 つの寸法または連続寸法が 1 つの連続寸法オブジェクトになる条件を満たす
  とき、新しい連続寸法オブジェクトを作って返す」。
  つまり**まず直線寸法を 1 本ずつ作り、それを 2 本ずつ繋いでいく**形になる。
- 連続寸法は**プラグインオブジェクト**らしい（`kInternalID_NNA_ChainDim = 222`）。

## 寸法をビューポートの注釈へ入れる

- 移し方は `ISDK::AddViewportAnnotationObject(viewport, annotation)`
  （[Data Tags](Data%20Tags.md) / [Viewports](Viewports.md) と同じ）。
- **VW 自身が「寸法をビューポート注釈に作る」ことをしている。**
  `ovDimTranslateInVP`(1234) のヘッダに
  "Used to move a dimension that is created in a viewport annotation by the exterior
  wall dimensioner only" とある（内部用なので使わないが、**注釈に寸法を置くのは
  想定された使い方**だという証拠になる）。
- **注釈へ後から足した図形のクラスはビューポートで非表示のまま**（[Viewports](Viewports.md)）。
  寸法も同じなので、足した後に全クラスを表示へ戻して再更新する。
