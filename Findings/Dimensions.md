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
- **寸法規格の index は組み込みが 1〜9、カスタムが 0 から下へ。** 名前は
  `GetDimensionStandardVariable(index, dimStdstandardName, …)` で引く。**`0` は有効な
  index で、カスタム規格の 1 件目がそこに入る**（`ovDimStandard` のヘッダは「0 は無効」と
  書いているが、実機はそうなっていない。下記「index の体系」）。
- **規格は index（`ovDimStandard`）でも名前（`ovDimStandardName`）でも書ける。**
  どちらを書いても他方が即座に追随し、**`ResetObject` は要らない**。存在しない名前を
  書くと `SetObjectVariable` が `false` を返して値は変わらないので、**名前の検証に
  そのまま使える**。
- **規格の寸法値は「用紙インチ」**（`page inches`）で定義されている。つまり補助線の
  長さ・文字と寸法線の間隔などは**縮尺に依らず紙の上の大きさ**で決まる。実測でも
  文字は紙の上で規格どおりの大きさに出る（下記「文字の大きさ」）。
- **作った寸法はその場でアクティブレイヤの中に入る。** 未挿入のハンドルは返らない。

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
  `GetObjectTypeN` はこれを返す（実測で確認）。

### 作った寸法はアクティブレイヤに入る

**`CreateLinearDimension` は「未挿入のハンドル」を返さない。** 呼んだ時点で
**アクティブレイヤ（`GetActiveLayer()`）の中に入っている**——実測でレイヤ直下の要素数が
1 本あたり 1 つ増え、`FirstMemberObj`／`NextObject` で歩くと当の寸法が見つかった。

- したがって**注釈へ入れたいだけの寸法も、いったんどこかのレイヤに置かれる**。
  `AddViewportAnnotationObject` はそこから注釈へ移す（下記）。
- **シートレイヤを作るとアクティブレイヤがそちらへ移る。** 「作った寸法がどこへ
  入ったか」を確かめるときは、**作る直前に `GetActiveLayer()` を取り直す**こと
  （取り違えると「どこにも入っていない」ように見える）。

### 読み戻すときの型（`ovDimClass` は符号付き）

オブジェクト変数は `TVariableBlock` で受け取るが、**ヘッダのコメントの型と実体が
一致しないものがある**。`ovDimClass` はヘッダに `unsigned char` と書いてあるのに、
実体は **`t_Sint8`（型番号 11）**で、`GetUint8` では読めない（`GetSint8` で読む）。

```cpp
// 型を決め打ちせず、効いたものを使う。決め打つと「読めない」で 1 往復むだになる。
TVariableBlock v;
if (gSDK->GetObjectVariable(h, ovDimClass, v))
{
    Sint8 value = 0;
    if (v.GetSint8(value)) { /* … */ }
}
```

型番号は `ObjectVariables.h` の無名 enum:
`t_Boolean=0 / t_Sint16=1 / t_WorldPt=2 / t_Real64=3 / t_TransformMatrix=4 /
t_WorldRect=5 / t_WorldPt3=6 / t_Sint32=7 / t_Str255=8 / t_ViewRect=10 /
t_Sint8=11 / t_Uint8=12 / t_FracPt=13 / t_Fract=14 / t_XCoordPt3=15 /
t_XCoordPt=16 / t_Uint32=17 / t_MCObjectHandle=18 / t_TXString=19`。

**`TVariableBlock` に `SetSint8` のような setter は無い。** `DEFINE_TYPE` が作る setter は
`operator=` だけで、明示的に定義されているのは `SetUint8` と `SetBoolean`（「型が
衝突するので」）の 2 つだけ。書くときは `v = static_cast<Sint8>(index);` とする。

### 文字の大きさは紙の上で決まる

実測（図面の縮尺 1:100）:

| セレクタ | 値 | 意味 |
| --- | --- | --- |
| `ovDimTextSizeInPoints`(40) | `6.000` | **紙の上のポイント** |
| `ovDimFontSize`(17) | `211.666` | 図面上の mm |

6 pt = 6 × 25.4 / 72 = **2.1167 mm**、これを縮尺 1:100 で割り戻すと **211.67 mm** で
`ovDimFontSize` と一致する。つまり**文字は紙の上で規格どおりの大きさに出て、
図面上の実寸のほうが縮尺に応じて変わる**。規格の長さが「用紙インチ」で定義されて
いることと整合している。

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

**実機（VW 2026 / macOS）で index −12〜12 を総当たりした結果**
（`GetDimensionStandardVariable` が `true` を返した index だけ）:

| index | 名前 |
| --- | --- |
| `1` | `Arch` |
| `2` | `ASME` |
| `3` | `BSI` |
| `4` | `DIN` |
| `5` | `ISO` |
| `6` | `JIS` |
| `7` | `SIA` |
| `8` | `ASME Dual SideBySide` |
| `9` | `ASME Dual Stacked` |
| `0` | `min-nano`（その図面のカスタム規格） |

- **組み込みは 1〜9 でヘッダどおり**、名前もこの順で固定。
- **カスタムは 0 から下へ詰まる。** この図面は `NumberCustomDimensionStandards() = 1` で、
  index は `0` だった（`-1` 以下は `false`）。
- **`ovDimStandard` のヘッダの「zero is invalid」は実機と合わない。** 0 は正常な
  カスタム規格の index で、この図面では**文書の既定**でもあった
  （`GetProgramVariable(varDimStandard)` が `0` を返した。`varDimStandard` は `short`）。
- それでも**名前で書くほうを勧める**——index の意味（何番が何か）は図面ごとに変わる
  のに対し、名前は利用者が選んだものそのままだから。

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

**実測（VW 2026 / macOS）。作った直後の寸法（`ovDimStandard = 0` / `min-nano`）に対して:**

| やったこと | `SetObjectVariable` | 直後の `ovDimStandard` | 直後の `ovDimStandardName` |
| --- | --- | --- | --- |
| `ovDimStandard` に `1` を書く | `true` | `1` | `Arch` |
| `ovDimStandardName` に `"ASME Dual Stacked"` を書く | `true` | `9` | `ASME Dual Stacked` |
| `ovDimStandardName` に存在しない名前を書く | **`false`** | `9`（変わらず） | `ASME Dual Stacked`（変わらず） |

- **index と名前は同じ 1 つの値の 2 つの顔で、どちらを書いても他方が即座に追随する。**
- **`ResetObject` は要らない。** 書いた直後にもう新しい値が読み戻せる（`ResetObject` を
  呼んだ後も値は同じ）。
- **存在しない名前は `false` で弾かれ、元の値が残る。** 「利用者が選んだ名前が図面に
  あるか」を別途照合しなくても、**書いてみて戻り値を見れば済む**。

## 連続寸法（チェーン寸法）

- **`ISDK::CreateChainDimension(h1, h2)` がある。** ヘッダ:
  「渡された 2 つの寸法または連続寸法が 1 つの連続寸法オブジェクトになる条件を満たす
  とき、新しい連続寸法オブジェクトを作って返す」。
  つまり**まず直線寸法を 1 本ずつ作り、それを 2 本ずつ繋いでいく**形になる。
- 連続寸法は**プラグインオブジェクト**（`kInternalID_NNA_ChainDim = 222`）。実測の
  `GetObjectTypeN` は **86**（直線寸法の 63 とは別）。
- **実測で、端点を共有する同じ向きの直線寸法 2 本は 1 つの連続寸法になった。**
  元の 2 本は**レイヤ直下から消えて**連続寸法の中へ取り込まれる
  （`FirstMemberObj`/`NextObject` で歩くと中身が 4 件あった）。
- **連続寸法そのものには寸法のオブジェクト変数が効かない。** `ovDimStandardName` を
  読もうとすると `GetObjectVariable` が `false` を返す。**規格は繋ぐ前の直線寸法へ
  当てる**のが素直。

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

**実測（VW 2026 / macOS）:**

- `AddViewportAnnotationObject(viewport, dimension)` は **`true`** を返し、移した後も
  その寸法は**型 63 のまま生きていて**、`ovDimStandardName` などのオブジェクト変数も
  そのまま読める。
- **外接矩形は移す前後で変わらない。** 注釈空間の座標は `CreateLinearDimension` に
  渡した座標そのままで、移動や座標変換は掛からない。
- 平面ビューポートの `GetObjectTypeN` は **122**。

## まだ確かめていないもの（[#129](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/129) で調査中）

**この節は調査が終わったら消す。** 印を付けて残すためのものではない。

- **`dimType` のどの数値がどの種類か。** ヘッダは「水平/垂直だけ」「p1→p2 の向きへ
  回す」「ordinate」の 3 択としか書いておらず、数値との対応が無い。1 巡目の実測では
  同じ 2 点に対して **`dimType` 0 と 1 は外接矩形まで完全に同じ**、2 と 3 はそれぞれ
  違う矩形になった（水平 2 点・`startOffset=300`: 0/1 は `上654.800 右1000.000`、
  2 は `上832.976 右1152.400`、3 は `上354.800 右1421.708`）。**斜めの 2 点でも 0 と 1 は
  同じ**だった。種類そのものは `ovDimClass` で決まるが、1 巡目は型を取り違えて
  読めていない（`t_Sint8`）。2 巡目で読み直す。
- **`startOffset` の符号がどちら側を指すのか。** 1 巡目は水平（`+` で上）と垂直
  （`+` で右）の 2 例だけで、一般の規則にならなかった。2 巡目で 4 方向を測る。
- **連続寸法を作るとレイヤ直下の要素数が 11 → 13（`+2`）になった理由。**
  元の 2 本は直下から消えているのに増えている。2 巡目で直下の型を列挙する。
