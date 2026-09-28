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
- **`dimType` の `0`（`fix_ang`）と `1`（`sloped`）に差は無い。** 寸法線の角度を持って
  いるのは **`ovDimDirection`(12) 1 つだけ**で、`ovDimClass` はそこに関与しない
  （下記「`0`（`fix_ang`）と `1`（`sloped`）に差は無い」）。
- **表示される寸法値は「測点間ベクトルの `ovDimDirection` への射影」で、実長ではない。**
  `ovDimDirection` は**書ける**ので、これを使えば**斜めに離れた 2 点の「水平距離」を
  出せる**。
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
| `dimType` | 種類。**`ovDimClass` にそのまま入る**（下記） |

### `dimType` は `ovDimClass` そのもの

ヘッダは「水平と垂直だけを許すか、`p1`→`p2` の向きへ寸法線を回すか、ordinate を作るか
を示す」としか書いておらず、**どの数値がどれなのかを書いていない**。実測すると、
**渡した `dimType` がそのまま `ovDimClass`(26) として読み戻せた**（0〜5 を試して全一致）。
つまり `dimType` の値は `ovDimClass` の割り当てそのもの:

| `dimType` = `ovDimClass` | 種類 |
| --- | --- |
| `0` | `fix_ang`（角度固定） |
| `1` | `sloped`（`p1`→`p2` の向きに沿う） |
| `2` | `ordinate` |
| `3` | `radial` |
| `4` | `diametrical` |
| `5` | `ang`（角度寸法） |

**2 点間の直線寸法として意味があるのは `0` / `1` / `2` だけ。** `3`〜`5` は円弧・角度の
寸法の種類で、2 点を渡しても寸法として成立しない形が出る（実測では `3` と `4` が
まったく同じ外接矩形になり、`5` は `ovDimDirection` が測る向きと直交した）。

**水平／垂直の寸法が欲しいなら、`dimType` ではなく `p1` と `p2` をそう取る。**
実測で、同じ 2 点に対して `dimType = 0`（`fix_ang`）と `1`（`sloped`）は
**外接矩形も `ovDimDirection` も完全に同じ**だった——斜めの 2 点を渡せば
`dimType = 0` でも寸法線は斜めになる。

| 2 点 | `dimType` | `ovDimDirection` | 外接矩形 |
| --- | --- | --- | --- |
| 水平 (0,0)-(1000,0) | `0` / `1` | `(1.000, 0.000)` | 同一 |
| 斜め (0,2000)-(1000,2600) | `0` / `1` | `(0.857, 0.514)` | 同一 |

`(0.857, 0.514)` は `(1000, 600)` を正規化したもので、**`dir` に `(0,0)` を渡せば
`p2`-`p1` が入る**というヘッダの説明どおり。

### `0`（`fix_ang`）と `1`（`sloped`）に差は無い——角度は `ovDimDirection` が決める

[#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134) の実測。
上の表は「作った直後は区別が付かない」までしか言っていないが、**作った後に触っても
差は出ない——SDK から触っても、利用者が画面で触っても**。名前（角度固定／傾き）から
期待してしまう「`fix_ang` は角度を保ち、`sloped` は測点に追従する」という挙動は、
**どちらにも起きない**。

寸法線の角度を持っているのは **`ovDimDirection`(12) 1 つだけ**である。

- **`ovDimDirection` は書ける。** ヘッダは `FracPt` / `Not for public use` と書いて
  いるが、`WorldPt` として読み書きできる。`SetObjectVariable` が `true` を返し、
  読み戻しも一致し、**実機の図面でも寸法線が回った**。
- **測点（`ovDimStartPt`(13) / `ovDimEndPt`(14)）を書き換えても `ovDimDirection` は
  再計算されない。** 測点の値そのものは入る（`true` が返り、読み戻しも外接矩形も
  変わる）のに、**向きは作ったときのまま**。`ResetObject` を呼んでも戻らない。
- 3 通り——水平で作って測点を動かす／斜めで作って水平に戻す／`ovDimDirection` を直に
  書く——× `dimType` `0` と `1` で、**`ovDimDirection`・測点・外接矩形・描かれた絵が
  すべて一致**した。
- **画面で端点（制御点）をドラッグしても同じ。** `0` と `1` を並べて同じように
  つまんで動かしたところ、**どちらも寸法線は水平のまま**で、値は**水平距離**に
  なった（`3,720` と `3,790`——差はドラッグ量であって挙動ではない）。
  **`ovDimDirection` は手で触っても保たれ、射影の規則もそのまま効いている。**

つまり **`dimType` は `0` と `1` のどちらで作ってもよい**（この 2 つで迷う必要は無い）。
狙った角度で出したいなら、**`p1`/`p2` をそう取るか、`ovDimDirection` を書く**
——`dimType` では決まらない。

#### 表示される寸法値は「射影」であって実長ではない

`ovDimDirection` と測点がずれていると、**寸法値は測点間ベクトルを `ovDimDirection` へ
射影した長さ**になる。実機に描かれた寸法を読んだ実測:

| `ovDimDirection` | 測点差 | 測点間の実長 | 射影 | **実機に描かれた値** |
| --- | --- | --- | --- | --- |
| `(1.000, 0.000)` | `(1000, 600)` | 1166.19 | 1000.0 | **1,000** |
| `(0.857, 0.514)` | `(1000, 0)` | 1000.00 | 857.5 | **857 1/2** |
| `(0.707, 0.707)` | `(1000, 0)` | 1000.00 | 707.1 | **707 1/20** |

3 例とも射影にぴったり一致した（`dimType` `0` と `1` で同じ絵）。

**これは使い道になる。** 斜めに離れた 2 点へ「水平距離」の寸法を入れたいとき、
測点を足元へ落とし直さなくても、**測点はそのままで `ovDimDirection` に `(1,0)` を
書けばよい**（伏図・軸組図の寸法で効く）。

**表示値は SDK から読めない。** 寸法（型 63）の中身を `FirstMemberObj` で歩いても
文字を持つ要素は無く（5 つ歩いて 0 件）、`GetTextChars` では取れなかった。
VectorScript には `GetDimText(h)` があるが、**`ISDK` に対応するものは無い**。
値を確かめたいときは、上の射影を自分で計算する（実測 3 例はすべてそれと一致した）。

#### `AssociateLinearDimension` は、SDK から図形を動かしたぶんには効かない

寸法の測点とちょうど重なる端点を持つ線分を 2 本立て、`AssociateLinearDimension` を
呼んでから線分を `MoveObject` で動かし、`ResetObject` を呼ぶ——を **8 通り**
（`h` に**寸法**を渡す／**図形**を渡す × `selectedObjectsMode` `true`／`false` ×
`dimType` `0`／`1`）試した。

**どれも測点が 1mm も動かなかった。** しかも**平行移動の対照（線分を 2 本とも同じだけ
動かす）すら追わない**ので、これは「関連付いたが向きが変わらなかった」ではなく
**そもそも追従が起きていない**。当然 `dimType` `0` と `1` の差も出ない。

- **ヘッダは `h` が寸法と図形のどちらを指すのか書いていない**（"Associates a linear
  dimension with an object when the dimension's endpoints are coincident with objects
  in the drawing"）。**両方試したが、どちらでも変わらなかった。**
- `AssociateLinearDimension` は **`void`** なので、成否は戻り値から分からない。
  **追従したかどうかを自分で確かめる以外に、関連付いたかを知る手立てが無い。**
- 軌跡点（`CreateLocus`）を相手にしても同じだった。

**「関連付けという仕組みが無い」とまでは言えない**——確かめたのは
「`MoveObject` で動かして `ResetObject` を呼ぶ」という SDK の経路だけで、
**画面の寸法ツールで作った寸法が追うかどうか・画面でドラッグしたときに追うかどうかは
別の話**。そちらは
[#138](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/138)
へ切り出した。

**プラグインから寸法を入れる用途では、図形が動いたら寸法を作り直す前提でよい。**
「関連付けておけば後は VW が面倒を見てくれる」とは期待しないこと。

### `startOffset` の符号は図面の座標軸で決まる（測る向きには依らない）

**`+` は水平な寸法なら上（`+y`）、垂直な寸法なら右（`+x`）。** `p1`→`p2` をどちら向きに
取っても変わらない。長さ 1000mm の寸法を 4 方向へ `startOffset = +300` で作り、
外接矩形の中心が線分の中点からどれだけずれるかを測った実測:

| `p1`→`p2` | 中点からのずれ | 「進行方向の左」への射影 |
| --- | --- | --- |
| `+x` 向き | `(0, +327.4)` | `+327.4` |
| `-x` 向き | `(0, +327.4)` | `-327.4` |
| `+y` 向き | `(+182.6, 0)` | `-182.6` |
| `-y` 向き | `(+182.6, 0)` | `+182.6` |

- **ずれの向きは 4 方向とも `+y` か `+x`** で、`p1`→`p2` の向きを反転しても動かない。
- 「進行方向の左」への射影は符号が入れ替わる——つまり**「測る向きに対して左／右」という
  規則ではない**。ここを取り違えると、始点と終点を入れ替えただけで寸法が反対側へ
  飛ぶと思い込む（実際には飛ばない）。
- 反対側へ出したいときは **`startOffset` を負にする**（実測で寸法線が反対側へ移った）。

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
  `GetObjectTypeN` は **86 = `kParametricNode`**（直線寸法の 63 とは別）。
- **実測で、端点を共有する同じ向きの直線寸法 2 本は 1 つの連続寸法になった。**
  元の 2 本は**レイヤ直下から消えて**連続寸法の中へ取り込まれる。中身を
  `FirstMemberObj`/`NextObject` で歩くと
  **`11`（`kGroupNode` ＝ 2D 表現のグループ）・`63`・`63`・`0`** だった。
- **連続寸法そのものには寸法のオブジェクト変数が効かない。** `ovDimStandard` /
  `ovDimStandardName` のどちらも `GetObjectVariable` が `false` を返す（中身の
  グループも同じ）。**規格は繋ぐ前の直線寸法へ当てる。**
- **レイヤ直下の「件数」で判断しないこと。** 実測ではレイヤ直下が 19 → 21 と
  **増えて**見えたが、内訳を型で見ると `63` が 2 つ消えて
  **`90`（`kUndoPlaceholderNode`）が 3 つ**と `86` が 1 つ増えていた。
  `kUndoPlaceholderNode` は**undo の記録用の置き石で図形ではない**ので、
  「図形が増えた」と読み違えない。

### `FirstMemberObj` / `NextObject` の walk は終端も数える

**リストの末尾に `kTermNode`（型 `0`）が 1 つ入っている。** 素直に
`FirstMemberObj` → `NextObject` で歩いて数えると、**実際の図形より 1 多く出る**。
件数を使うなら終端を除くか、そもそも件数ではなく**型の並び**を見る
（上の連続寸法の件は、型を見なければ原因に辿り着けなかった）。

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
- **移ったことは「元いたレイヤから消えた」で確かめる。** 実測では、作った直後は
  アクティブレイヤ直下に**いて**、`AddViewportAnnotationObject` の後は**いなくなった**。
  - **ただしアクティブレイヤを取り違えないこと。** `CreateLayer(..., kLayerSheet)` で
    シートレイヤを作ると**アクティブレイヤがそちらへ移る**ので、寸法を作る直前に
    `GetActiveLayer()` を取り直してから判定する。
- 平面ビューポートの `GetObjectTypeN` は **122**。

### 注釈へ寸法を置くだけでは見えない——ビューポートの作法を先に踏む

**実機で踏んだ。** 断面ビューポートを作って注釈へ寸法を 2 本足し、`UpdateViewport`
まで呼んだのに、シートレイヤ上のビューポートは **53.3mm 角の「×」印の空枠**
（`GetObjectBounds` も `左-26.64 上26.64 右26.64 下-26.64`）のままで、
**断面も寸法も一切描かれなかった**。

原因は寸法側ではなく、[Viewports](Viewports.md) に書いてある**ビューポートの作法を
踏んでいなかった**こと。寸法を注釈へ置くときも同じ手順が要る:

1. **レンダリングを隠線消去にする**（`ovViewportRenderType`(1001) ←
   `renderFinalHiddenLine` = 6）。**これが先**——シェイドのままでは
   `ovViewportDisplay2DComponents`(1059) が入らない。
2. **クラスをすべて表示へ戻す。** ビューポートは**既定でクラスが全部消えている**
   （`ForEachClass(true, …)` ＋ `SetViewportClassVisibility(vp, GetObjectInternalIndex(cls), 0)`）。
3. 断面の表示の作法（`ovSectionViewportDisplayObjectsBeyondCutPlane`(1064) /
   `ovViewportDisplayPlanar`(1035) / `ovViewportDisplay2DComponents`(1059)）を
   **すべて更新より前に**設定する。
4. 表示レイヤを表示にする（`SetViewportLayerVisibility`）。
5. **注釈へ寸法を足した後、もう一度クラスを全部表示へ戻して再更新する。**
   注釈へ**後から**足した図形のクラスは非表示のままだから（[Viewports](Viewports.md)）
   ——ここを飛ばすと、断面は描かれているのに**寸法だけが見えない**。

**「寸法が出ない」と判断する前に、ビューポートに何か 1 つでも描かれているかを見る。**
空枠のままなら、それは寸法の問題ではない。

**実測で、この手順を踏んだら注釈の寸法は見えるようになった。** 踏む前は断面
ビューポートが「×」印の空枠で寸法も何も出ず、踏んだ後は**寸法が縦横 1 本ずつ
はっきり見え、値も渡した 2 点間の距離どおり**（4000mm と 2400mm）に出た。

### ビューポートの外接矩形は注釈を含まない——「描かれたか」の判定に使えない

**実測で踏んだ落とし穴。** 「中身が描かれたか」を機械で確かめようとして
`GetObjectBounds(viewport)` を作法の前後で比べたが、**注釈に寸法が 2 本見えている
状態でも、外接矩形は空枠のときと 1mm も変わらなかった**（どちらも
`左-26.64 上26.64 右26.64 下-26.64` の 53.3mm 角）。

- **ビューポートの外接矩形は中身を勘定に入れない。** 注釈へ図形を足しても増えないし、
  断面に写る図形が増えても増えない（**高さ 0 の壁を高さ 2800mm に直した後も 1mm も
  変わらなかった**）。53.3mm 角という値は**ビューポートの枠そのもの**で、中身とは無関係。
- したがって**「外接矩形が変わらない＝何も描かれていない」と読んではいけない。**
  何が描かれているかは、**絵を見るしかない**。

### 断面ビューポートの注釈空間の座標——横の原点は断面線の「終点」

**実測（VW 2026 / macOS）。** 断面線を `(-500, -1500)` → `(4500, -1500)`（長さ 5000mm、
`+x` 向き）に引き、`(2000, 1500)` 側から見る断面ビューポートを作って、
`(0,0)`-`(4000,0)` の壁を写した。そのときの注釈空間の座標:

| 模型上の位置 | 注釈空間の横座標 |
| --- | --- |
| 壁の左端 `x = 0` | **`-4500`** |
| 壁の右端 `x = 4000` | **`-500`** |

**横 = （断面線に沿った位置）−（断面線の終点）。** つまり**原点は断面線の「終点」**で、
始点側へ向かって負になる。始点基準だと思って置くと、**断面線の長さちょうどぶん
（この例では 5000mm）ずれる**——実際にそれを踏んだ。

**裏取り済み。** この式で `-4500`〜`-500` に寸法を置き直したところ、**断面に写った壁の
左端から右端までぴったりに掛かった**（縦も壁の足元から Z=2400 まで）。ずれていた版と
直した版を実機で見比べているので、**式は確定**。

- **縦 = Z**（実測で一致）。
- 向きは**視線の右手が模型の `+x`** になる並びだった（断面線 `+x` 向き・`+y` を見る配置）。

### 注釈空間は用紙 1:1 なので、文字は紙のポイントのまま出る

同じ寸法を、縮尺 1:100 のデザインレイヤとビューポートの注釈とで読み比べた実測:

| 置き場所 | `ovDimTextSizeInPoints` | `ovDimFontSize`（図面上 mm） |
| --- | --- | --- |
| デザインレイヤ（1:100） | `6.000` | `211.666` |
| ビューポートの注釈 | `6.000` | **`2.116`** |

6 pt = 2.1167 mm なので、**注釈では図面上の実寸がそのまま紙の寸法に等しい**
（＝注釈空間は 1:1）。デザインレイヤ側は縮尺で割り戻されて 100 倍になっている。
**つまり注釈へ寸法を置けば、規格で決めた紙の上の大きさがそのまま出る**——
ビューポートの縮尺を変えても文字の大きさは変わらない。
