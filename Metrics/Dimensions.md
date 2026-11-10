# 寸法（直線寸法・寸法規格） — 計測記録

結論は [Findings「Dimensions」](../Findings/Dimensions.md)。ここには測った環境・手順・
実測の表・ヘッダの写し・潰した道と【訂正】の経緯を、元の文言のまま置く。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

平面図・断面のビューポートへ**寸法を自動で入れる**ための調査（[#129](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/129)）
と、その後の #134・#138・#143・#145・#155・#157・#161・#163・#164・#175・#200・#245・#249 の
実測。環境は各節に書いてあるとおりで、ほぼすべて **VW 2026 / macOS** の実機で
`probes/runtime/` のプローブを走らせて測った（#249 は `probes/runtime/t0-mismatch`。どれも役目を終えて消してある）。
各節は Findings の節に対応する（見出しは記録の言い方にしてある）。

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

（引数ごとの意味の表は [Findings](../Findings/Dimensions.md#createlineardimension-の引数) にある。）

### `dimType` と `ovDimClass` の読み戻しの実測

ヘッダは「水平と垂直だけを許すか、`p1`→`p2` の向きへ寸法線を回すか、ordinate を作るか
を示す」としか書いておらず、**どの数値がどれなのかを書いていない**。実測すると、
**渡した `dimType` がそのまま `ovDimClass`(26) として読み戻せた**（0〜5 を試して全一致）:

| `dimType` = `ovDimClass` | 種類 |
| --- | --- |
| `0` | `fix_ang`（角度固定） |
| `1` | `sloped`（`p1`→`p2` の向きに沿う） |
| `2` | `ordinate` |
| `3` | `radial` |
| `4` | `diametrical` |
| `5` | `ang`（角度寸法） |

2 点を渡して `3`〜`5` で作ると、`3` と `4` がまったく同じ外接矩形になり、`5` は
`ovDimDirection` が測る向きと直交した。

同じ 2 点に対して `dimType = 0`（`fix_ang`）と `1`（`sloped`）で作ると、
**外接矩形も `ovDimDirection` も完全に同じ**だった——斜めの 2 点を渡すと
`dimType = 0` でも寸法線は斜めになった。

| 2 点 | `dimType` | `ovDimDirection` | 外接矩形 |
| --- | --- | --- | --- |
| 水平 (0,0)-(1000,0) | `0` / `1` | `(1.000, 0.000)` | 同一 |
| 斜め (0,2000)-(1000,2600) | `0` / `1` | `(0.857, 0.514)` | 同一 |

`(0.857, 0.514)` は `(1000, 600)` を正規化したもので、`dir` に `(0,0)` を渡すと
`p2`-`p1` が入るというヘッダの説明どおりの値だった。

### `0`（`fix_ang`）と `1`（`sloped`）を並べて比べた実測（#134）

[#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134) の実測。
作った後に SDK から触ったときと、画面で触ったときを、`dimType` `0` と `1` で並べて比べた。

- **`ovDimDirection`(12) への書き込み。** ヘッダは `FracPt` / `Not for public use` と書いて
  いるが、`WorldPt` として読み書きできた。`SetObjectVariable` が `true` を返し、
  読み戻しも一致し、**実機の図面でも寸法線が回った**。
- **測点（`ovDimStartPt`(13) / `ovDimEndPt`(14)）の書き換え。** 測点の値そのものは入った
  （`true` が返り、読み戻しも外接矩形も変わった）が、**`ovDimDirection` は作ったときのまま**
  だった。`ResetObject` を呼んでも戻らなかった。
- 3 通り——水平で作って測点を動かす／斜めで作って水平に戻す／`ovDimDirection` を直に
  書く——× `dimType` `0` と `1` で、**`ovDimDirection`・測点・外接矩形・描かれた絵が
  すべて一致**した。
- **画面で端点（制御点）をドラッグした。** `0` と `1` を並べて同じように
  つまんで動かしたところ、**どちらも寸法線は水平のまま**で、値は**水平距離**に
  なった（`3,720` と `3,790`——差はドラッグ量）。

#### `ovDimDirection` と測点がずれた寸法に描かれた値の実測

`ovDimDirection` と測点がずれている寸法を作り、実機に描かれた寸法値を読んだ実測:

| `ovDimDirection` | 測点差 | 測点間の実長 | 射影 | **実機に描かれた値** |
| --- | --- | --- | --- | --- |
| `(1.000, 0.000)` | `(1000, 600)` | 1166.19 | 1000.0 | **1,000** |
| `(0.857, 0.514)` | `(1000, 0)` | 1000.00 | 857.5 | **857 1/2** |
| `(0.707, 0.707)` | `(1000, 0)` | 1000.00 | 707.1 | **707 1/20** |

3 例とも「測点間ベクトルを `ovDimDirection` へ射影した長さ」に一致した（`dimType` `0` と
`1` で同じ絵）。

**表示値を寸法の中から読んだ記録**（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）:

- 【訂正】以前は「表示値は SDK から読めない」と書いていた。根拠は「`FirstMemberObj` で
  5 つ歩いて文字を持つ要素は 0 件」だったが、**直下しか見ていなかった**。直下の子は
  `[0:1 2:5 11:1]`（線 5 本と群 1 つ）で、**文字は群（型 11）の中に 1 つある**。
  作った直後に `ResetObject` 無しで読んでも `'1100'` / `'1200'` / `'1300'`（作った長さ
  そのまま）が取れ、undo イベントを閉じても VW へ戻った後も同じだった。
  歩き方は [Investigation Techniques](../Findings/Investigation%20Techniques.md) の「絵で読める大きさか」は
  目視に頼らず測れる（再帰して型 10 を拾い、`GetTextChars` で文字列も読める）。
- 射影を自分で計算した値も、実測 3 例すべてと一致した。

#### `AssociateLinearDimension` を 8 通りで呼んだ記録（#134）

この節は #134 で何を測ったかの記録（「効かない」は
[#138](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/138)
で撤回した。#138 の記録は[下記「寸法の関連付け」](#寸法の関連付け図形が動いたら寸法も追うか)）。

寸法の測点とちょうど重なる端点を持つ線分を 2 本立て、`AssociateLinearDimension` を
呼んでから線分を `MoveObject` で動かし、`ResetObject` を呼ぶ——を **8 通り**
（`h` に**寸法**を渡す／**図形**を渡す × `selectedObjectsMode` `true`／`false` ×
`dimType` `0`／`1`）試して、**どれも測点が 1mm も動かなかった**（軌跡点を相手にしても
同じ）。`dimType` `0` と `1` で差は出なかった。

#138 で `HasConstraint` を見たところ、**この 8 通りでも関連付けそのものは成立していた**。
この 8 通りでは**制約を解く段（`UpdateConstraintModel`）を一度も呼んでいなかった**。
呼んだときの記録は[下記](#解く段updateconstraintmodelまで呼んだ実測138)。

### `startOffset` の符号を 4 方向で測った実測

長さ 1000mm の寸法を 4 方向へ `startOffset = +300` で作り、
外接矩形の中心が線分の中点からどれだけずれるかを測った実測:

| `p1`→`p2` | 中点からのずれ | 「進行方向の左」への射影 |
| --- | --- | --- |
| `+x` 向き | `(0, +327.4)` | `+327.4` |
| `-x` 向き | `(0, +327.4)` | `-327.4` |
| `+y` 向き | `(+182.6, 0)` | `-182.6` |
| `-y` 向き | `(+182.6, 0)` | `+182.6` |

- **ずれの向きは 4 方向とも `+y` か `+x`** で、`p1`→`p2` の向きを反転しても動かなかった。
- 「進行方向の左」への射影は向きによって符号が入れ替わった。
- `startOffset` を負にすると、寸法線が反対側へ移った。

- `MathCoordTypes.h:329` に `typedef WorldPt Vector2;` とある。
- 作った寸法のオブジェクト型は **`dimHeaderNode = 63`**（`Objs.TDType.h:129`）。
  `GetObjectTypeN` はこれを返した（実測）。

### 作った寸法が入ったレイヤの実測

`CreateLinearDimension` を呼んだ直後に、**アクティブレイヤ（`GetActiveLayer()`）の中に入っていた**
——実測でレイヤ直下の要素数が 1 本あたり 1 つ増え、`FirstMemberObj`／`NextObject` で歩くと
当の寸法が見つかった。

- シートレイヤを作ると、アクティブレイヤがそちらへ移った（作る前に取った `GetActiveLayer()`
  の中を探すと、寸法は「どこにも入っていない」ように見えた）。

### 読み戻すときの型の実測（`ovDimClass`）

`ovDimClass` はヘッダに `unsigned char` と書いてあるが、`TVariableBlock` で受け取ると
実体は **`t_Sint8`（型番号 11）**で、`GetUint8` では読めなかった（`GetSint8` で読めた）。
読みに使ったコード:

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

ヘッダの写し: `TVariableBlock` で `DEFINE_TYPE` が作る setter は `operator=` だけで、
**明示的に定義されている setter は `SetUint8` ただ 1 つ**（`Uint8` は `DEFINE_TYPE` の並びに
無く、型が衝突するため別に置かれている）。`SetSint8` は無い。

- **`SetBoolean` は無い**（VW 2026 の SDK。`-fsyntax-only` で踏んだ）。`Boolean` は
  `DEFINE_TYPE(Boolean)` の並びに入っており、`v = static_cast<Boolean>(1);` で通った。
  読みは `GetBoolean(bool&)` が別に定義されている。

### 作るときのアクティブレイヤを替えた `ovDimFontSize` の実測

実測（VW 2026 / macOS。同じ規格・同じ 6pt の設定で、アクティブレイヤを替えて作り分けた
寸法の `ovDimFontSize`(17)（図面上の mm））:

| 作ったときのアクティブレイヤ | `ovDimFontSize` |
| --- | --- |
| デザインレイヤ 1/100 | `211.666` |
| デザインレイヤ 1/50 | `105.833` |
| シートレイヤ 1:1 | `2.1166` |

6 pt = 6 × 25.4 / 72 = **2.1167 mm** で、どの行も `2.1167 × 縮尺` に一致した。

> **【訂正】** この 6pt は当初「規格の大きさ」と読んでいたが、#157 が
> **その寸法に当たっている文字スタイルの `ovTextStyleSize`（インチ）**を式ごと押さえ、
> [#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)
> が**規格と文字スタイルを別々にして**規格を差し替えても動かないことを測った
> ——下記「[規格と文字の大きさの関係を測った記録（#163）](#規格と文字の大きさの関係を測った記録163)」。
> この図面では規格 `min-nano` が `寸法(6pt)` を指しているので**数値は一致する。**

- **後からレイヤの縮尺を変えた。** 1/100 で作った寸法（`211.666`）を
  載せたまま `SetLayerScaleN` でそのレイヤを 1/50 にしても、`ovDimFontSize` は
  `211.666` のままだった。読み戻しでも絵でもそうなった（同じレイヤに並べた 1/50 生まれの
  寸法より明らかに大きく描かれた。計算どおり 2 倍）。
- **`ovDimFontSize` へ書いた。** 書いた値は `ResetObject`・規格の当て直し
  （`ovDimStandardName` の書き戻し）・`ovDimShowValue` の立て直しのいずれでも
  巻き戻らなかった（実測）。連続寸法の中の直線寸法での記録は下記
  「[〈クラスの文字スタイル〉のまま繋いだ連続寸法の文字の大きさの実測（#155）](#クラスの文字スタイルのまま繋いだ連続寸法の文字の大きさの実測155)」。
- 線の幾何の側は [#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)
  で測り直した——下記「[縮尺・規格を変えて線の幾何を測った実測（#145）](#縮尺規格を変えて線の幾何を測った実測145)」。

#### `ovDimTextSizeInPoints` を 2 つの時点で読んだ実測

デザインレイヤ（1/50）に置いたままの 1 本を、2 つの時点で `ovDimTextSizeInPoints`(40) を
読んだ（`ovDimFontSize` は両方とも `105.8333` で動いていない）:

| 読んだ時点のアクティブレイヤ | `ovDimTextSizeInPoints` |
| --- | --- |
| そのデザインレイヤ（1/50） | **`6`** |
| シートレイヤ（1:1）※間にシートレイヤを作っただけ | **`300`** |

間に変えたのは**アクティブレイヤだけ**である——寸法も、寸法が乗っているレイヤも、
その縮尺も動かしていない（`105.8333mm` は 1:1 で読めば ちょうど 300pt）。

- **書いたとき。** アクティブが 1:1 のときに `ovDimTextSizeInPoints` へ `6` を書くと、
  入ったのは `ovDimFontSize` = `2.1167` だった（実測。書き込み自体は `true` を返した）。

### 縮尺・規格を変えて線の幾何を測った実測（#145）

VW 2026 / macOS。[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145) の実測。
補助線の長さと隙間・寸法線の出（overhang）・端記号の大きさ・文字と寸法線の間隔を、
レイヤの縮尺・生まれの縮尺・規格・ビューポートの縮尺を変えて測った（①〜④）。
同じ 1 本の中で `ovDimFontSize` は動かず線の幾何だけが動いたので、これを物差しの対照に使った。

#### 測り方——文字を消した寸法の外接矩形で線の幾何を測る

`ovDimShowValue = false` にした水平な寸法（測点 `(0,y)`-`(L,y)`・オフセット `d`）の
外接矩形から、規格の用紙長さを割り出した:

```
出（overhang） = (幅 - L) / 2
補助線の出     = 上端 - (y + d)
補助線の隙間   = 下端 - y
```

校正で踏んだこと（どちらも実測）:

- **作った直後の外接矩形は値の文字を含んでいた。** `CreateLinearDimension` の直後に
  `ovDimShowValue = false` を書いても、**描き直される（`ResetObject`・レイヤやビュー
  ポートの縮尺の変更）まで外接矩形は縮まなかった**。どの縮尺で作っても「寸法線から上へ
  紙の 3.548mm」という同じ値が出て、**描き直した後はそれが規格どおりの 1.2mm に
  縮んだ**（差は 2.348mm。規格 `min-nano`・6pt）。以下の値は `ResetObject` の後に測った。
- **端記号は外接矩形に入らなかった**（素の線のマーカーと同じ。[Attributes and Classes](../Findings/Attributes%20and%20Classes.md)）。
  **`GetMarkerPolys` も寸法に対しては始・終とも「なし」を返した。** 端記号の大きさは
  絵で確かめた（下記 ④）。
- **`SetLayerScaleN` が効かなかった文書が実在した**
  （[Layers and Stories](../Findings/Layers%20and%20Stories.md)「レイヤの縮尺」）。その文書では
  後で作る寸法も全部「変える前の縮尺」で生まれたので、ログ上は「生まれの違う 2 本が
  違って見える」が、実際には 2 本とも同じ縮尺生まれだった。
  下の ① と ② は、**読み戻しで 1/100 → 1/50 → 1:1 と実際に変わったことを確かめた走行**の値である。

#### ① レイヤの縮尺を変えて、既にある寸法の線の幾何を測った

規格 `min-nano` の `witExtend`（補助線が寸法線より出る長さ）は **0.0472 page inch =
紙の 1.2mm**。1/100 のレイヤで作った 1 本を、レイヤの縮尺だけ変えて読み直した:

| レイヤの縮尺 | 補助線の出（実測） | 紙の 1.2mm × 縮尺 | `ovDimFontSize`（対照） |
| --- | --- | --- | --- |
| 1/50 | **`60.000`** | 60 | `211.666`（**動かない**） |
| 1:1 | **`1.200`** | 1.2 | `211.666`（**動かない**） |

補助線の出は「紙の 1.2mm × 縮尺」と一致し、`ovDimFontSize` は動かなかった。

#### ② 生まれの縮尺が違う 2 本を同じ縮尺の下で測った

1/100 で作った寸法と 1/50 で作った寸法を同じレイヤに置き、レイヤを 1:1 にして測った:

| 寸法 | 出 | 補助線の出 | 隙間 |
| --- | --- | --- | --- |
| 1/100 生まれ | `0.000` | **`1.200`** | `0.000` |
| 1/50 生まれ | `0.000` | **`1.200`** | `0.000` |

1 つの桁も違わなかった。

#### ③ 規格を差し替えて、既にある寸法の線の幾何を測った

1/50 のレイヤに置いた 1 本に規格を当て直して測った（実測値は**すべて「紙の値 × 50」
そのもの**だった）:

| 規格 | `witExtend`（紙） | 補助線の出（実測） | `overHang`（紙） | 出（実測） |
| --- | --- | --- | --- | --- |
| `min-nano` | 1.2mm | `60.000` | 0mm | `0.000` |
| `Arch` | 3.175mm | **`158.750`** | 1.5875mm | **`79.375`** |
| `DIN` | 2.0mm | `100.000` | 0mm | `0.000` |
| `ISO` | 2.0mm | `100.000` | 0mm | `0.000` |
| `JIS` | 2.0mm | `100.000` | 0mm | `0.000` |

#### ④ 注釈の中でビューポートの縮尺を変えて測った

1/50 のビューポートの注釈へ 2 本（`JIS` の 1/50 生まれと、`min-nano` の 1:1 生まれ）を
入れ、ビューポートの縮尺だけを変えて測った:

| ビューポートの縮尺 | 1/50 生まれ・`JIS`（紙 2.0mm） | 1:1 生まれ・`min-nano`（紙 1.2mm） |
| --- | --- | --- |
| 1/50 | `100.000` | `60.000` |
| 1/100 | `200.000` | `120.000` |

どちらも「紙の値 × ビューポートの縮尺」ちょうどだった（1:1 生まれの寸法も同じ）。

**この ④ は 2 つの文書で同じ数値が出た**（レイヤの縮尺が 1/100 で始まった文書と、
1:1 の新規の空図面。`SetLayerScaleN` が効かなかった後者でも、ビューポートの縮尺の
ほうは効いてこの表のとおりになった）。

**絵でも確かめた（目視）。** 同じ注釈に「1/50 生まれ」と「1:1 生まれに `ovDimFontSize` だけ
書いて直したもの」を並べたところ、**値の文字・端記号・補助線のどれも同じ大きさで、
2 本は見分けが付かなかった**。端記号は外接矩形にも `GetMarkerPolys` にも出ないので、
端記号についてはこの目視だけが記録である。

#### 読み戻せる `ov*` を生まれの縮尺ごとに読んだ実測

`ovDimTextAboveLineInCurrUnits`(43) と `ovDimCust*Wit*`(1236-1239) を、生まれの縮尺ごとに
読んだ。入っていたのは**規格の用紙値をそのとき有効だった縮尺で図面上の長さに直した値**だった:

| セレクタ | 1/100 生まれ | 1/50 生まれ | 1:1 生まれ | 元の規格値（紙） |
| --- | --- | --- | --- | --- |
| `ovDimTextAboveLineInCurrUnits`(43) | `50.000` | `25.000` | `0.500` | `aboveGap` 0.5mm |
| `ovDimCustStartWitLength`(1236) | `1016.000` | `508.000` | `10.160` | `fixedWitLength` 10.16mm |
| `ovDimCustStartWitOffset`(1238) | `100.000` | `50.000` | `1.000` | `witGap` 1.0mm |

- **縮尺を変えても動かなかった。** 1/100 生まれの 1 本をレイヤ 1/50 → 1:1 と動かしても
  `50 / 1016 / 100` のままだった（**絵は追随した**）。注釈でも同じで、
  ビューポートを 1/50 → 1/100 にしても `50` のまま（絵は `100 → 200` と動いた）。
- **規格を当て直すと、そのときの縮尺で計算し直された**（`Arch` を当てると
  `ovDimTextAboveLineInCurrUnits` が `79.375` ＝ 1.5875mm × 50 になった）。

#### per-object の上書き（`ovDimWitnessOverride`）を書いた実測

`ovDimWitnessOverride`(1235) を立てて補助線の長さを書いた。**実測**:

| やったこと | 結果 |
| --- | --- |
| `ovDimWitnessOverride` ← `1`（`Sint16` で書く） | `true` / 読み戻し `1` |
| `ovDimCustStartWitLength` ← `2000` | `true` / **補助線が図面上 2000 の長さで描かれた**（外接矩形の下端が寸法線から 2000 下がった） |

- 書いた `2000` は用紙インチではなく図面上の長さとして描かれた（`in current units` の名前どおり）。
- **`ovDimWitnessOverride` は `Sint16` で書けた**（ヘッダは `short` と書いている）。

#### 端記号の大きさ（`dimStdLinearASize`）の読みと換算

`dimStdLinearASize`(22) はヘッダに単位の記載が無い `Sint16`。**マーカーの大きさと
同じ 1/16384 インチの 16 ビット整数**として換算した値（[Attributes and Classes](../Findings/Attributes%20and%20Classes.md)）:

| 規格 | `linearASize`(22) | 1/16384 インチとして | 紙の大きさ |
| --- | --- | --- | --- |
| `min-nano` | `774` | 0.0472 インチ | 1.200mm |
| `Arch` / `DIN` / `ISO` / `JIS` | `2048` | 0.1250 インチ | 3.175mm |

`min-nano` の `774` を換算した値は、同じ規格の `witExtend`（0.0472 page inch）と**同じ値**
だった。`linearAWidth`(36) はどの規格でも `0`（「zero value also allowed」とヘッダにある）。

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

- 組み込みは 1〜9 でヘッダどおりだった。
- この図面は `NumberCustomDimensionStandards() = 1` で、カスタムの index は `0` だった
  （`-1` 以下は `false`）。
- index `0` のカスタム規格は、この図面では**文書の既定**でもあった
  （`GetProgramVariable(varDimStandard)` が `0` を返した。`varDimStandard` は `short`）。
  `ovDimStandard` のヘッダは「zero is invalid」と書いている（下記「寸法へ規格を当てる」）。

ヘッダの写し:

- カスタム規格の本数は `ISDK::NumberCustomDimensionStandards()`。
- `SetCustomDimensionStandardVariable` は組み込み（1〜9）について "cannot be changed with this
  function" と書いている（実測は下記 ②）。
- カスタム規格を**作る**口は `ISDK::CreateCustomDimensionStandard(name)`。
  「アクティブな規格の値を既定として、その名前の新しいカスタム規格を作る」（実測は下記
  「付け足し」）。

### 一覧の取り方

名前は `dimStdstandardName = 25`（`MiniCadCallBacks.h:1833`。型は `Str255`＝文字列）で
引き、`TVariableBlock::GetTXString` で受けた。総当たりに使ったコード:

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

- 規格のその他の項目（補助線の隙間・矢印の大きさなど）は `dimStd*` セレクタ
  （`MiniCadCallBacks.h:1809` 以降）。ヘッダの注記では長さ系はすべて「用紙インチ」
  で、範囲は `-2.0 〜 2.0` page inches。

### 寸法へ規格を当てる

寸法 1 本ごとの規格を持つオブジェクト変数（`Include/Kernel/API/ObjectVariables.h`）:

| セレクタ | 番号 | 型 | ヘッダの説明 |
| --- | --- | --- | --- |
| `ovDimStandard` | 0 | `char`（`Sint8`） | 規格の index。**> 0 は組み込み、< 0 はカスタム** |
| `ovDimStandardName` | 27 | `TXString` | 規格の名前 |

- `ovDimStandard` のヘッダの写し: "the result for an invalid
  value (zero, greater the the number of builtin standards, less than the number of
  custom standards) is undefined"。`GetDimensionStandardVariable` の説明は
  カスタムの範囲に 0 を含めている。

**実測（VW 2026 / macOS）。作った直後の寸法（`ovDimStandard = 0` / `min-nano`）に対して:**

| やったこと | `SetObjectVariable` | 直後の `ovDimStandard` | 直後の `ovDimStandardName` |
| --- | --- | --- | --- |
| `ovDimStandard` に `1` を書く | `true` | `1` | `Arch` |
| `ovDimStandardName` に `"ASME Dual Stacked"` を書く | `true` | `9` | `ASME Dual Stacked` |
| `ovDimStandardName` に存在しない名前を書く | **`false`** | `9`（変わらず） | `ASME Dual Stacked`（変わらず） |

- どちらを書いても、他方が書いた直後に追随して読み戻せた。`ResetObject` を
  呼んだ後も値は同じだった。

### 規格と文字の大きさの関係を測った記録（#163）

VW 2026 / macOS。[#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)
で実機のプローブを走らせた。**2 回走らせた**——別のビルドで走らせた 2 回のログは
**1 行も違わなかった**（下に挙げる値はすべて両方で同じ）。

#### ① セレクタに「文字の大きさ」があるか（ヘッダを読んだ）

ヘッダを読んだ記録。`dimStd*` セレクタは **1〜52 で全部**（`Kernel/API/MiniCadCallBacks.h:1809-1860`）。
用紙インチの長さ系は補助線・寸法線・端記号ばかりで、**文字に関わるのは次の 3 つだけ**:

| セレクタ | 番号 | 型 | ヘッダの説明 |
| --- | --- | --- | --- |
| `dimStdTextStyle` | 51 | `Sint32` | *the ref number of the text style that the dimension standard is linked to* |
| `dimStdTextPos` | 52 | `UInt8` | 文字の位置（0: above/left, 1: above/right, 2: outside） |
| `dimStdTolSizeFac` | 26 | `UInt8` | **公差**文字の大きさ（寸法文字に対する％） |

#### ② 規格の文字スタイルを書いた実測（カスタムと組み込み）

**実測。文字スタイルを 2 つ（`6pt` ＝ `0.0833333` インチ／`300pt` ＝ `4.16667` インチ）
作ってから、`SetCustomDimensionStandardVariable(index, dimStdTextStyle, ref)` を叩いた:**

| 相手 | index | 戻り値 | 読み戻した `dimStdTextStyle` |
| --- | --- | --- | --- |
| カスタム規格（作ったばかりのもの） | `-1` | **`true`** | `113`（狙った 6pt の文字スタイル） |
| カスタム規格（作ったばかりのもの） | `-2` | **`true`** | `114`（狙った 300pt の文字スタイル） |
| **組み込み規格 `JIS`** | `6` | **`false`** | `0`（変わらず＝文字スタイル無し） |

- 組み込み（1〜9）は戻り値 `false` で、値も動かなかった（ヘッダの
  "cannot be changed with this function" どおり）。
- 文字スタイルは `CreateTextStyleResource` で作り、`ovTextStyleSize`（インチ）を書いた
  （下記「[`ovTextStyleSize` の単位の実測](#ovtextstylesize-の単位の実測)」）。

#### ③ その規格を当てた寸法の大きさの実測

（**VW へ戻った後に読み直しても同じ**——[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。規格が指す文字スタイル資源の大きさを
6pt → 24pt に書き換えた後も、その規格・その文字スタイルを持つ寸法 5 本（`ResetObject`
あり／なし）の描かれた文字は元の大きさのまま、undo イベントを閉じても戻った後も動かなかった。）

**確かめた仮説は「1/50 のビューポートの注釈で紙の上 6pt」。** #155 の式
（「規格が紙で決めた大きさ × 繋ぐときのアクティブレイヤの縮尺」）が正しければ、
**規格の紙の大きさを `300pt`（＝ 6pt × 50）にしておけば、1:1 がアクティブなままでも
`6pt × 25.4/72 × 50 = 105.833mm` が焼き付く**はずだった。**ならなかった。**

**すべて 1:1 のシートレイヤをアクティブにしたまま測った**（規格 `300pt` の下でも
`6pt` の下でも、焼き付いた値は**終始 `2.11667mm`** ＝ 1/50 の紙で 0.12pt ＝ 見えない）:

| # | やったこと | 寸法の `ovDimStandardName` | 寸法の `GetTextStyleRef` | 繋いだ後の中の `ovDimFontSize` |
| --- | --- | --- | --- | --- |
| 5a | 対照: `6pt` の規格を**文書の既定**にしてから作る | `規格6pt` | `30` | `2.11667` |
| **5b** | **`300pt` の規格を文書の既定にしてから作る** | `規格300pt` | **`30`** | **`2.11667`** |
| 6 | `6pt` の規格で作った寸法へ、後から `ovDimStandardName` で `300pt` の規格を当てる | `規格300pt` | **`30`** | **`2.11667`** |

- `ovDimStandardName` は書いたとおりに読み戻せた。
- **`GetTextStyleRef` は `30` のまま動かなかった。** `30` はこの図面の
  **元からの**カスタム規格 `min-nano` が指す `寸法(6pt)`で、新しく当てた規格が指す
  `113` / `114` ではなかった。
- #157 の「[作った直後の文字スタイルと `GetTextStyleRef` の読みの実測
  （#157・#249）](#作った直後の文字スタイルと-gettextstyleref-の読みの実測157249)」で
  測った 4 点目（規格を替えても寸法の文字スタイルは外れない）と同じ値の動きで、
  今回は「規格を文書の既定にしてから作る」道（5b）でも同じだった。
  この図面の「寸法」クラスは `寸法(6pt)`（`30`）を持っている（[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)。下記「[寸法はどのクラスへ入り…](#寸法はどのクラスへ入りgettextstyleref-はどのクラスを追うか249)」）。
- **描かれている文字図形（型 `10`）の大きさも終始 `2.11667mm`** で、読み戻した値と
  絵は食い違っていなかった（#155 のような「値は動いたが絵は古い」は起きていない——値
  そのものが動かなかった）。

#### ④ 既にある連続寸法で規格側を動かした実測（3 つの道）

**`6pt` の規格で作って繋いだ連続寸法（中の `ovDimFontSize` ＝ `2.11667`）**に対して、
規格側を動かしてから `ResetObject` した。**どれも `2.11667` のまま**だった:

| 試したこと | 規格側の読み戻し | `ResetObject` の**前** | `ResetObject` の**後** |
| --- | --- | --- | --- |
| a. 規格が指す**文字スタイルの大きさ**を `6pt` → `300pt` に変える | `300pt` に変わった | `2.11667` | **`2.11667`** |
| b. 規格を**別の文字スタイル**（`300pt`）へ結び直す | `114` に変わった | `2.11667` | **`2.11667`** |
| c. **中の直線寸法**へ `ovDimStandardName` で `300pt` の規格を当てる | 書き込みは `true` | `2.11667` | **`2.11667`** |

- a と b で規格の側が変わったことは読み戻しで確認した。
- **c では、中の直線寸法へ書いた `ovDimStandardName` そのものが `ResetObject` の後に
  戻っていた**——書いた直後は `規格300pt` と読めるのに、`ResetObject` の後は
  **`規格6pt`** だった（#155 の「中へ書いた `ovDimFontSize` は `ResetObject` で捨てられる」と
  同じ動き）。
- **絵（中に描かれている文字図形）も終始 `2.11667mm`。**

#### ⑤ 「繋ぐときに焼き付く値」の左の因子を突き合わせた記録

③④ の値と、#157 の「[文字スタイルを当てる 2 つの口で
`ovDimFontSize` を往復した実測](#文字スタイルを当てる-2-つの口で-ovdimfontsize-を往復した実測)」の
`SetTextStyleRef` の実測を突き合わせた。式は Findings にある。

#### ⑥ 規格以外の道を 2 通り試した

1:1 がアクティブなまま `CreateChainDimension` を呼んで連続寸法の紙の大きさが決まった
実測の出どころ:

| 道 | やること | 実測の出どころ |
| --- | --- | --- |
| **(a)** | **繋ぐ直前**に、繋ぐ 2 本へ `ovDimFontSize` ＝ 紙の pt × 25.4/72 × ビューポートの縮尺 を書く | #161（「[#143 の手順を `ResetObject` 付きで測り直した記録](#143-のovdimfontsize-を書く手順を-resetobject-付きで測り直した記録161)」。繋いだ後の中の直線寸法も**絵も**書いた値どおりだった） |
| **(b)** | **繋ぐ前**に、`SetTextStyleRef(dim, ref)` で文字スタイルを明示する（`ovTextStyleSize` ＝ 狙いの紙 pt ÷ 72 × ビューポートの縮尺） | #157（「[連続寸法へ繋いだ後の読み戻し](#連続寸法へ繋いだ後の読み戻し)」と「[連続寸法の中の `ovDimFontSize` と描かれた大きさの実測](#連続寸法の中の-ovdimfontsize-と描かれた大きさの実測)」の 3 巡目） |

- **(b) は #157 がその条件そのままで測っている**——1:1 のシートレイヤが
  アクティブなまま `ovTextStyleSize` ＝ `10.4167` インチ（＝ 6/72 × **125**）の文字
  スタイルを当てて作り、**1/125 のビューポートの注釈で紙の 6pt として読めた**（行 F2）。
  同じ文字スタイルだけを当てた 2 本を繋いだ連続寸法も、**直線寸法と同じ
  `264.5833mm`（＝ `10.4167 × 25.4 × 1`）の絵**で出ている（3 巡目）。

#### 付け足し: `CreateCustomDimensionStandard` の実測

| 測ったこと | 結果 |
| --- | --- |
| 作った規格が入る index | **`0` が埋まっていれば `-1`、次は `-2`**（0 から下へ詰まる） |
| `NumberCustomDimensionStandards()` | `1` → `2` → `3` と 1 つずつ増える |
| 作った直後の `dimStdTextStyle` | **アクティブな規格のものを引き継ぐ**（この図面では `min-nano` の `30`） |
| 戻り値 | 規格を表す `MCObjectHandle`（非 nil） |

- 作った直後の規格は**元の規格と同じ文字スタイルを指していた**。
- #155 の時点ではカスタムが 1 件で index `0` だったが、そこへ 2 つ足したら `-1` `-2` に入った。

## 寸法の文字スタイル

寸法の文字スタイル（OIP の「文字 → スタイル」）を
[#157](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/157)
で実機（VW 2026 / macOS）から測った。

### 寸法規格が持つ文字スタイルを読む

読みに使ったコード:

```cpp
TVariableBlock block;
if (gSDK->GetDimensionStandardVariable(standardIndex, dimStdTextStyle, block))
{
    Sint32 styleRef = 0;
    if (block.GetSint32(styleRef) && styleRef != 0)
    {
        TXString styleName;
        gSDK->InternalIndexToNameN(static_cast<InternalIndex>(styleRef), styleName);
    }
}
```

**実測（利用者の図面環境。index 1〜9 と 0〜−5 を総当り）:**

| index | 規格名 | `dimStdTextStyle` | 指す名前 |
| --- | --- | --- | --- |
| 1〜9 | `Arch` / `ASME` / `BSI` / `DIN` / `ISO` / `JIS` / `SIA` / `ASME Dual SideBySide` / `ASME Dual Stacked` | **`0`** | (なし) |
| **0** | **`min-nano`** | **`30`** | **`寸法(6pt)`** |
| −1〜−5 | — | `GetDimensionStandardVariable` が **`false`** | — |

- この図面ではカスタムは index `0` の 1 件しかなく、−1 以下は「無い」と返った
  （上記「[index の体系](#index-の体系)」の総当たりと同じ結果）。

### `ovTextStyleSize` の単位の実測

`CreateTextStyleResource` で作った直後の `ovTextStyleSize` は **`0.1667`**
（= 1/6 インチ = **12pt**）。ここへ `6` を書くと **6 インチ = 432pt** になり、
その文字スタイルを当てた寸法の `ovDimTextSizeInPoints` は実際に **`432`** を返した。

### 作った直後の文字スタイルと `GetTextStyleRef` の読みの実測（#157・#249）

> **【訂正の記録】** この節は最初（#157）「`GetTextStyleRef` はクラスを見ていない」と読んでいた。
> [#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)
> （VW 2026 / mac・`probes/runtime/t0-mismatch`）で測り直したところ、寸法は作られたときに
> **「寸法」クラス**へ入っており（`varCreateDimsInDimClass`(44)=`1`）、#157 が書き換えたのは
> **アクティブクラス `一般`** だった。**利用者のテンプレートの「寸法」クラスは、もともと
> `寸法(6pt)`（`30`）を持っていた**（規格 `min-nano` の文字スタイルと**同じ番号**）。
> 下の 1〜4 の観察値はそのまま残す。測り直しの表は下記
> 「[寸法はどのクラスへ入り、`GetTextStyleRef` はどのクラスを追うか](#寸法はどのクラスへ入りgettextstyleref-はどのクラスを追うか249)」。

作った直後の寸法（型 63）は:

| 読むもの | 値 |
| --- | --- |
| `GetTextStyleByClass(dim)` | **`true`** |
| `ovDimTextStyle`(1248) | **`-2`**（＝「クラス由来」の番兵。ref number ではない） |
| `GetTextStyleRef(dim)` | **`30` ＝ `寸法(6pt)`** |

`30` は、寸法を作った時点で当たっていた寸法規格（`min-nano`）の `dimStdTextStyle` と
同じ番号だった。続けて次の 4 つを測った:

1. 寸法を作った時点で、アクティブクラス（`一般`）は文字スタイルを**持っていなかった**
   （`GetClUseTextStyle` = `false` / `GetClTextStyleRef` = `0`）。それでも
   `GetTextStyleRef` は `30` を返した。
2. そのあとクラスへ**別の文字スタイル**を与えても（`SetClUseTextStyle(true)` ＋
   `SetClTextStyleRef(113)`。読み戻しで `use=true ref=113` を確認）、**同じ寸法は
   `30` のまま**。`ResetObject` を挟んでも変わらない。
3. **その後に新しく作った寸法**も `30` のままだった。
4. **`ovDimStandardName` を `JIS`（`dimStdTextStyle` = `0` ＝文字スタイルを持たない）へ
   替えても、`GetTextStyleRef` は `30` のまま**、`ovDimTextStyle` も `-2` のままだった
   （読み戻しで規格が `JIS` になったことは確認済み）。

> ~~つまり by-class の寸法について `GetTextStyleRef` が返す番号は、クラスにも、
> いま当たっている規格にも追随しない。~~ **（#157 当時の読み。#249 で撤回。上の訂正の記録）**

- **#245 の記録**（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）。
  専用のクラスへ入れて by-class にした寸法 4 本（2 本は `ResetObject` あり）で、
  クラスの文字スタイルを 30pt のものへ差し替えた。描かれた文字は 4 本とも元の大きさ
  （空の図面で 4.2333 / 3.1750、テンプレートの図面で 211.6667 / 317.5000）のまま、
  undo イベントを閉じても、VW へ戻って数十秒置いても動かなかった。
  - この回では **`GetTextStyleRef` は差し替えた直後から新しい番号を返した**（こちらは
    寸法を入れたクラスそのものを書き換えた。#249 の表の最下行と同じ操作）。

### 寸法はどのクラスへ入り、`GetTextStyleRef` はどのクラスを追うか（#249）

VW 2026 / mac。[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)。腕ごとに図面を新しく開いた。
`CreateLinearDimension` で作ったままの寸法（#157 の作り方）と、専用クラスへ移した寸法
（#245 の作り方）を、既定のテンプレート（`Default.sta`）と `OpenDocumentPath(nullptr)` の
空の図面で並べた。

| | テンプレートの図面 | 空の図面 |
| --- | --- | --- |
| `varCreateDimsInDimClass`(44) | `1` | `1` |
| 文書の規格（`varDimStandard`） | `0`「min-nano」・規格の文字スタイル `30(寸法(6pt))` | `1`「Arch」・`0` |
| アクティブクラス | `一般`（文字スタイル無し） | `一般`（文字スタイル無し） |
| 寸法クラス（`GetDimensionClassID`） | `寸法`（**`use=yes ref=30(寸法(6pt))`**） | `寸法`（文字スタイル無し） |
| 作った直後の寸法 | **クラス `寸法`**・`ref=30`・by-class・`font=211.6667` | **クラス `寸法`**・`ref=0`・**明示**（by-class でない）・`font=4.2333` |
| アクティブクラス `一般` へ別の文字スタイルを当てる | `30` のまま | `0` のまま |
| **寸法の入った `寸法` クラス**へ別の文字スタイルを当てる | **その場で新しい番号**（`131`） | `0` のまま（by-class でないので） |
| その後に作った寸法 | クラス `寸法`・`ref=131`・by-class・`font=1058.3333` | クラス `寸法`・**`ref=7`・by-class**・`font=10.5833` |
| 専用クラスへ `SetObjectClass`（文字スタイルを持たないクラス） | `ref=0`・by-class | `ref=0`・by-class |
| そのクラスへ文字スタイルを当てる | **その場で新しい番号**（`SetTextStyleByClass` を呼んでも呼ばなくても同じ） | 同左 |

- クラスを書き換えた後も、既にある寸法の `ovDimFontSize` と描いた文字は元のままだった
  （`ResetObject` を挟んでも。上記の #245 の記録）。
- テンプレート 1/100 で 30pt の文字スタイルをクラスへ当てた後に作った寸法は
  `1058.3333`（＝ 30 × 25.4 / 72 × 100）で生まれた。
- 空の図面では、「寸法」クラスへ文字スタイルを持たせた後に作った寸法は by-class で生まれた。
- #163 の図面ではクラスを読み直していない。

### 文字スタイルを当てる 2 つの口で `ovDimFontSize` を往復した実測

**同じ 1 本（1:1 のシートレイヤがアクティブなときに作った寸法）で往復した実測:**

| やったこと | `GetTextStyleByClass` | `ovDimTextStyle` | **`ovDimFontSize`** |
| --- | --- | --- | --- |
| 作った直後 | `true` | `-2` | `2.1167` |
| `SetTextStyleRef(dim, 113)` | `false` | `113` | **`152.4000` へ書き換わる** |
| `SetTextStyleByClass(dim)` で戻す | `true` | `-2` | **`2.1167` へ戻る** |
| `SetObjectVariable(dim, ovDimTextStyle, 113)` | `false` | `113` | **`2.1167` のまま（動かない）** |

- `SetTextStyleRef` の行の `152.4` は、当てた文字スタイル（6 インチ）× 25.4 × 1
  （シートレイヤがアクティブなので縮尺 1）に一致した。作った直後の値も、`寸法(6pt)`
  （6/72 インチ）で 1:1 なら `6/72 × 25.4 × 1 = 2.1167`、1/50 のデザインレイヤなら
  `× 50` して `105.8333` に一致した（上記「[作るときのアクティブレイヤを替えた `ovDimFontSize` の実測](#作るときのアクティブレイヤを替えた-ovdimfontsize-の実測)」の表）。
- **容れ物の縮尺とアクティブレイヤの縮尺が分かれた 1 例**（[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)）:
  1:1 のシートレイヤがアクティブなまま、1/125 の注釈の中の連続寸法へ `SetTextStyleRef` を
  掛け直したら `0.0833 × 25.4 × 125` ＝ `264.5833` が入った。ほかの検算はどれも、
  作る時点で寸法がアクティブレイヤに入っており両者が一致している場合である。
- どちらの道でも、後から `ovDimFontSize` を書くとその値が残った（`264.5833` を書いて
  そのまま読み戻せた）。

### 書いた直後と引き直した後の絵の文字の大きさの実測（#161）

[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161) の実測
（VW 2026 / macOS。1/50 の平面ビューポートの注釈へ 7 行。**測る長さを行ごとに
変えてあるので、出た数字がそのまま行の名前になる**。絵で確認済み）:

| 行 | やったこと | `ovDimFontSize`（読み戻し） | **実際に描かれた文字** | 絵 |
| --- | --- | --- | --- | --- |
| H1 | 外す → 紙 6pt を書く → **`ResetObject`** | `105.8333` | **`105.8333`mm ＝紙 6pt** | **読める** |
| H2 | **外さず**（by-class のまま）紙 6pt を書く → **`ResetObject`** | `105.8333` | **`105.8333`mm ＝紙 6pt** | **読める** |
| H3 | 外す → 紙 6pt を書く（`ResetObject` を呼ばない） | `105.8333` | `2.1167`mm ＝紙 0.12pt | 出ない（**縮尺を往復させたら紙 6pt になった**） |
| H4 | 外す → **紙 12pt** を書く → `ResetObject` | `211.6667` | **`211.6667`mm ＝紙 12pt** | **読める。H1 のちょうど 2 倍** |
| H5 | `SetTextStyleRef`（紙 6pt の文字スタイル）だけ | `105.8333` | `105.8333`mm ＝紙 6pt | 読める |
| H6 | 外す → 紙 6pt を書く（`ResetObject` を呼ばない） | `105.8333` | `2.1167`mm ＝紙 0.12pt | 出ない（H3 と同じく縮尺の往復で出た） |
| H7 | 繋ぐ前に紙 6pt を書いて `CreateChainDimension` | `105.8333` | `105.8333`mm ＝紙 6pt | 読める |

- H4 は H1 のちょうど 2 倍で描かれた。
- H2 は〈クラスの文字スタイル〉のまま紙 6pt で描かれた。
- H3 / H6 は `ResetObject` を呼ばなかったので絵は変わらなかったが、
  **後からビューポートの縮尺を往復させたら紙 6pt になった**。
- H1 と H6 は `GetTextStyleByClass` / `GetTextStyleRef` / `ovDimTextStyle` / `ovDimFontSize` の
  **4 つとも完全に同じ**値を読み戻したが、絵は片方しか出なかった。違うのは
  「`ResetObject` を通したかどうか」だけ。描かれた文字の大きさは
  [調査の作法「絵の文字の大きさを目視に頼らず測る」](../Findings/Investigation%20Techniques.md) の
  道で測った。

#### 寸法から文字スタイルを外す口（`SetTextStyleRef(dim, 0)`）

ヘッダ（`SDKLib/Include/vs.py`）の写し:

> `SetTextStyleRef`: … **Reference 0 means Un-Styled.** This procedure will replace
> by-class styling.
> `GetTextStyleRef`: … If the text object is using class text style, this returns the
> **effective** style.

| 道 | 結果（実測） |
| --- | --- |
| **`gSDK->SetTextStyleRef(dim, 0)`** | **外れる。** `GetTextStyleByClass` = `false` / `GetTextStyleRef` = `0` / `ovDimTextStyle` = `0`。**`ResetObject` を越えて残る** |
| `ovDimTextStyle`(1248) ← `0` | 外れる（読み戻しは上と完全に同じ） |
| `ovDimTextStyle` ← `-1` | **外れない。**「番号 `-1` が当たっている」という別の状態になる |
| クラスを Un-Styled にして `SetTextStyleByClass` | **外れない。** by-class のまま、実効は `30`（`寸法(6pt)`） |

- 外しても絵は変わらなかった（H3 / H6）。
- クラスから文字スタイルを取り去っても `GetTextStyleRef` の実効は `30` のままだった。

### `ResetObject` を呼ばずに注釈で値が出たかの実測（#157 の D1〜D6）

> **【訂正の記録】** この節の表（D1〜D6）は #157 の実測で、どの行も `ResetObject` を
> 呼んでいなかった。当時は表から「`SetTextStyleRef` で明示しないと値が出ない」と読んだが、
> [#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
> で `ovDimFontSize` を書いて `ResetObject` した by-class の寸法が出ることを測った
> （上記「書いた直後と引き直した後の絵の文字の大きさの実測」の H2）。表は測ったときのまま残す。

1/125 の平面ビューポートの注釈へ、作り方だけを変えた寸法を 6 行ならべて絵を見た
（VW 2026 / macOS）。**測る長さを行ごとに変えてあるので、出た数字がそのままどの行かを表す。**

| 行 | やったこと | `GetTextStyleByClass` | `ovDimTextStyle` | `ovDimFontSize` | **絵に値が出たか** |
| --- | --- | --- | --- | --- | --- |
| D1 | 〈クラスの文字スタイル〉のまま `ovDimFontSize` を書く | `true` | `-2` | `264.5833` | **出ない** |
| D2 | **`SetTextStyleRef`** → `ovDimFontSize` を書く | `false` | `113` | `264.5833` | **出る** |
| D3 | **`SetTextStyleRef`** だけ | `false` | `113` | `152.4000` | **出る** |
| D4 | **`ovDimTextStyle` へ番号を書く** → `ovDimFontSize` を書く | `false` | `113` | `264.5833` | **出ない** |
| D5 | 何もしない（対照） | `true` | `-2` | `2.1167` | 出ない |
| D6 | 繋ぐ前に両方へ `SetTextStyleRef` ＋ `ovDimFontSize` → `CreateChainDimension` | `false` | `113` | `264.5833` | **出る** |

- **`D2` と `D4` は読み戻しが 4 つとも完全に同じなのに、絵は片方しか出なかった。**
- #161 で測り直した値: D4 は番号だけが変わって描かれた大きさが元のまま
  （`2.1167`mm ＝紙 0.017mm）だった。
- 【訂正】当時は「`ovDimFontSize` を正しく書いたかどうかは関係なかった」と書いていた。
  #161 の実測では、D1 は `264.5833` を持っていたが実際に描かれていた文字は `2.1167`mm
  （1/125 の紙で 0.048pt）だった——[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
  の「1:1 生まれだから小さい」と同じ値である。
- D2・D3・D4 が使ったのはプローブが作った無名の文字スタイルで、寸法規格の
  `寸法(6pt)` ではない。
- D6（連続寸法）: 繋ぐ前に中身の直線寸法へ `SetTextStyleRef` を呼んでおいたものは、
  繋いだ後も注釈へ移した後も値が出た。

同じビューポートには**デザインレイヤ（1/50）に置いた by-class の寸法**も写っており、
そちらは**値が出ていた**（`ovDimFontSize = 105.8333` → 1/125 の紙で 0.85mm ≒ 2.4pt の
小さな数字として読めた）。

### 文字スタイルを当てた後に `ovDimFontSize` を書いた実測（E1〜E3）

> **【訂正の記録】** 見出しはもと「大きさを決めるのは文字スタイルであって `ovDimFontSize` ではない」
> だった（[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161) で改めた）。
> 下の E1〜E3 は **3 本とも `ResetObject` を呼んでいない**。#161 で `ResetObject` を通した
> H4 は H1 のちょうど 2 倍で描かれた（上記「書いた直後と引き直した後の絵の文字の大きさの実測」）。

同じ文字スタイル（6 インチ）を当てた直線寸法を 3 本、`ovDimFontSize` だけ変えて
1/125 の注釈へならべた（`ResetObject` 無し）:

| 行 | `ovDimFontSize` | 紙の上の見込み | **絵の大きさ** |
| --- | --- | --- | --- |
| E1 | `152.4000`（`SetTextStyleRef` が書いた値のまま） | 1.22mm ≒ 3.5pt | 基準 |
| E2 | `264.5833`（紙 6pt のつもり） | 2.12mm ＝ 6pt | **E1 と同じ** |
| E3 | `529.1667`（紙 12pt のつもり） | 4.23mm ＝ 12pt | **E1 と同じ** |

**3.47 倍まで違う値を持たせても、絵は 3 本ともまったく同じ大きさだった。**

- `ovDimFontSize` は書けて、書いた値が読み戻せた。
- **注釈へ移しても値は 1 つも動かなかった**（移す前後で `ovDimFontSize` が同じ）。

### 文字スタイルの大きさを 0・1・2 倍にして注釈で測った実測

**0 倍・1 倍・2 倍の 3 本を 1/125 の注釈へならべて確かめた**（`ovDimFontSize` は
いっさい触らず、文字スタイルの大きさ `ovTextStyleSize` だけを変えてある）:

| 行 | `ovTextStyleSize` | 狙い | **絵** |
| --- | --- | --- | --- |
| F1 | `0.0833`（＝ 6/72。素直に「6pt」と書いたつもりの値） | — | **見えない**（紙で 6/125 ＝ 0.048pt） |
| F2 | `10.4167`（＝ 6/72 × 125） | 紙 6pt | **読める** |
| F3 | `20.8333`（＝ 12/72 × 125） | 紙 12pt | **F2 のちょうど 2 倍** |

- F2 の値は、[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
  の `ovDimFontSize` の式 `紙pt × 25.4/72 × 縮尺`（mm）をインチに直した（÷ 25.4）
  `紙pt / 72 × 縮尺` である。

### 「紙の pt で名付けられた文字スタイル」をそのまま当てた実測（#164）

[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164) で 10 行を測った。
寸法規格 `min-nano` が持つ `寸法(6pt)`（`ovTextStyleSize` = 6/72 = `0.0833` インチ）を
`SetTextStyleRef` でそのまま当てた。

**実測（VW 2026 / macOS。`寸法(6pt)` 相当の文字スタイルだけを当て、`ovDimFontSize` は
触らない。判定は「中に描かれている文字図形（型 10）の 1 文字目」を `GetTextSize` で
測った値）:**

| 行 | 作るときのアクティブ | 置いた注釈 | `ovDimFontSize` | **描かれた文字** | 紙の上 | 絵 |
| --- | --- | --- | --- | --- | --- | --- |
| D | 1:1（シートレイヤ） | 1/125 | `2.1167` | `2.1167`mm | **0.048pt** | **×読めない** |
| A | 1:1（連続寸法） | 1/125 | `2.1167` | `2.1167`mm | **0.048pt** | **×読めない** |
| G | **1/125**（連続寸法） | 1/125 | `264.5833` | `264.5833`mm | **6pt** | **★読める** |
| I | **1/50**（単独） | 1/50 | `105.8333` | `105.8333`mm | **6pt** | **★読める** |
| J | **1/50**（連続寸法） | 1/50 | `105.8333` | `105.8333`mm | **6pt** | **★読める** |

- 描かれた文字はどの行も「`0.0833` × 25.4 × 作るときのアクティブレイヤの縮尺」に一致した
  （1/50 なら `6/72 × 25.4 × 50` ＝ `105.833`mm、1:1 なら `2.1167`mm）。
- A も D も文字図形そのものは在って、大きさが `2.1167`mm だった。
- OIP の「文字 → スタイル」には `寸法(6pt)` と出ていた。

#### 注釈の連続寸法で紙の pt を出す 3 つの道の実測

**同じ 1 回の実行で、1/125 の注釈に並べて測り分けた。**

| 道 | やること | 連続（型 86） | 単独（型 63） |
| --- | --- | --- | --- |
| **(a)** | 文字スタイルを当てた**後に** `ovDimFontSize` ＝ 紙 pt × 25.4/72 × 縮尺 を書き、**引き直す** | **★**（`CreateChainDimension` が引き直しなので、**繋ぐ前に書けばそれで済む**＝ B） | **★**（`ResetObject` を 1 回＝ C2。呼ばないと × ＝ C） |
| **(b)** | **縮尺用の文字スタイルを作る**（`ovTextStyleSize` ＝ 紙 pt ÷ 72 × 縮尺）。`ovDimFontSize` は触らない | **★**（E） | **★**（F） |
| **(c)** | **ビューポートと同じ縮尺のレイヤをアクティブにして**から当てて繋ぐ | **★**（G） | ★（I / J は 1/50 で同じ） |

- (c) のレイヤの切り替えは `ISDK::SetCurrentLayer` で行った（下記
  「[注釈へ寸法を置く手順の根拠の実測](#注釈へ寸法を置く手順の根拠の実測)」の【訂正】）。
- B: 繋いだ後の型 86 の `ovDimFontSize` は `2.1167` のままだったが、絵は `264.5833` で描かれていた。
- B / C / C2 は `GetTextStyleByClass` / `GetTextStyleRef` / `ovDimTextStyle` / `ovDimFontSize` の
  **4 つとも完全に同じ**（`false` / `113` / `113` / `264.5833`）だったが、絵は C だけが
  出なかった。描かれた大きさは
  [調査の作法「絵の文字の大きさを目視に頼らず測る」](../Findings/Investigation%20Techniques.md) の道で測った。

#### 既に注釈へ置いた連続寸法を後から直す 6 通りの実測

[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)。
`寸法(6pt)` を 1:1 で当てて繋いだ連続寸法を 6 本、1/125 の注釈へ置いてから、
**1 本に 1 つだけ**直しを当てて測った:

| 行 | 注釈へ置いた後にしたこと | 掛けた／書いた直後 | `ResetObject(型 86)` の後 | 最終 |
| --- | --- | --- | --- | --- |
| A | **何もしない**（対照） | — | — | **×**（0.048pt） |
| K1 | 中の型 63 へ `ovDimFontSize` ＝ `264.5833` | 値は入るが絵は `2.1167` | **値が `2.1167` へ戻された** | **×** |
| K2 | 同じ書き込みで `ResetObject` を呼ばない | 値は入るが絵は `2.1167` | （呼ばない） | **×** |
| H0 | **型 86 へ `SetTextStyleRef`**（1:1 がアクティブ） | 型 86 は `264.5833` ★／**中の型 63 は `2.1167`** | **中の型 63 も `264.5833`** | **★ 6pt** |
| H1 | 同じことを 1/125 がアクティブな文脈で | H0 と 1 つも違わない | H0 と同じ | **★ 6pt** |
| H2 | **中の型 63 へ `SetTextStyleRef`** | 中の型 63 は `264.5833` ★ | **`2.1167` へ戻された** | **×** |

- **掛け直しても読み戻しの 3 つは 1 つも動かなかった。** `GetTextStyleByClass`（`false`）・
  `GetTextStyleRef`（`113`）・`ovDimTextStyle`（`113`）は前も後も同じ値で、
  **動いたのは `ovDimFontSize` と絵だけ**だった（`2.1167` → `264.5833`）。
- H2: 掛けた直後は中の型 63 が `264.5833` になったが、連続寸法の `ResetObject` の後に
  `2.1167` へ戻された。
- H0 / H1: `SetTextStyleRef(型 86)` の直後は型 86 自身の文字だけが書き換わり、中の型 63 は
  古い大きさのままだった。`ResetObject(型 86)` の後に中の型 63 も `264.5833` になった。
  - 【訂正】以前は「`ResetObject` は要る」とだけ書いていた。[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)
    で測り直した（新規図面の 1:1 で、`ResetObject` を呼ばずに `SetTextStyleRef(型 86, 24pt)`）:
    中の型 63 の `ovDimFontSize` と描かれた文字は、書いた直後は `4.2333`（古い）だったが、
    **同じ呼び出しの中でランループを 1.5 秒回しただけで `8.4667`（新しい）になり**、
    undo イベントを閉じた後も VW へ戻った後もそのままだった。
  - 同じ回で、**中の型 63 へ直接書いた `ovDimFontSize = 12.7` は、ランループを回した
    時点で `4.2333` へ戻された**。
- H0 と H1（アクティブレイヤが 1:1 と 1/125）は 1 つも違わなかった。
- K1 / K2: 文字スタイルを明示してある連続寸法でも、中へ書いた `ovDimFontSize` は戻された
  （[#155](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/155)
  の「繋いだ後に中へ書いても捨てられる」は by-class の連続寸法で測っていた）。単独の直線寸法では
  書いた値で描かれた（上記 C2）。

### 連続寸法の中の `ovDimFontSize` と描かれた大きさの実測

> **【訂正の記録】** 見出しはもと「（直線寸法と食い違う）」だった。#157 の当時は下の表から
> 「直線寸法は文字スタイルの大きさで描かれるので `ovDimFontSize` は効かない」と読んでいたが、
> [#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)
> で、文字スタイルを当てたまま `ovDimFontSize` を書いて `ResetObject` を通した単独の
> 直線寸法が書いた値のとおりの大きさで描かれることを測った（上記
> 「[注釈の連続寸法で紙の pt を出す 3 つの道の実測](#注釈の連続寸法で紙の-pt-を出す-3-つの道の実測)」の C2）。
> 2 巡目は直線寸法を引き直していなかった。

2 つの実行（#157 の 2 巡目・3 巡目）の絵を突き合わせた:

| | 直線寸法へ当てたもの | 直線寸法の絵 | 連続寸法の絵 |
| --- | --- | --- | --- |
| 2 巡目 | 文字スタイル（6 インチ ＝ `152.4`）→ **`ovDimFontSize` を `264.5833` で上書き** | `152.4` の大きさ | **`264.5833` の大きさ**（明らかに大きい） |
| 3 巡目 | 文字スタイル（`10.4167` インチ ＝ `264.5833`）**だけ**。上書きしない | `264.5833` の大きさ | **同じ**（食い違わない） |

- 2 巡目の 2 本は、読み戻しでは 4 つとも同じ値だった。

### 連続寸法へ繋いだ後の読み戻し

繋ぐ前に直線寸法 2 本へ `SetTextStyleRef(113)` と `ovDimFontSize = 264.5833` を当ててから
`CreateChainDimension` した結果:

| 読んだ相手 | `GetTextStyleByClass` | `GetTextStyleRef` | `ovDimTextStyle` | `ovDimFontSize` |
| --- | --- | --- | --- | --- |
| 中の直線寸法（型 63）2 本とも | `false` | `113` | `113` | `264.5833` |
| 連続寸法そのもの（型 86） | `false` | `113` | **読めず** | `152.4000` |

- 対照: `SetTextStyleRef` を呼んでいない（〈クラスの文字スタイル〉のままの）寸法を繋いだ
  [#155](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/155) の 4 回では、
  中の `ovDimFontSize` は繋いだ時点で捨てられた（下記「[〈クラスの文字スタイル〉のまま繋いだ連続寸法の文字の大きさの実測（#155）](#クラスの文字スタイルのまま繋いだ連続寸法の文字の大きさの実測155)」）。
- 連続寸法そのもの（型 86 の PIO）でも `GetTextStyleByClass` / `GetTextStyleRef`（`ISDK` の口）は
  読めた。`ovDimTextStyle` は `GetObjectVariable` が `false` を返した。
- **`ISDK::SetPIOTextStyle(chain, 113, true)` を当てても何も変わらなかった**が、当てる前から
  `113` が入っていた。

### 注釈へ移した後の読み戻し

6 本すべて `AddViewportAnnotationObject` が `true` を返し、**移した後の読み戻しは移す前と
完全に同じ**だった（`GetTextStyleByClass` / `GetTextStyleRef` / `ovDimTextStyle` /
`ovDimFontSize` の 4 つとも）。

### #143 の「`ovDimFontSize` を書く」手順を `ResetObject` 付きで測り直した記録（#161）

[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161) の記録。
#157 の 3 回の実行（D1 / E5 / F5）で
[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
の手順が再現しなかった。#161 では、**by-class のまま `ovDimFontSize` ＝紙 6pt 相当を書き、
`ResetObject` を呼んだ寸法は、注釈で紙 6pt に出た**（上記「[書いた直後と引き直した後の絵の文字の大きさの実測（#161）](#書いた直後と引き直した後の絵の文字の大きさの実測161)」の H2）。

当初の見立て（「**文字スタイルが当たっているときだけ効かない**」）を確かめた:

| 確かめたこと | 結果 |
| --- | --- |
| 文字スタイルを外せるか | **外せる**（`SetTextStyleRef(dim, 0)`。上記） |
| 外したら `ovDimFontSize` は効くか | **外しても外さなくても同じ**——効くかどうかは `ResetObject` を通したかだけで決まった（H1 と H2 が同じ絵） |
| 外したまま `ResetObject` を呼ばなければ | **効かない**（H3 / H6）。＝ 文字スタイルの有無は関係が無い |

## 連続寸法（チェーン寸法）

- **`ISDK::CreateChainDimension(h1, h2)` がある。** ヘッダ:
  「渡された 2 つの寸法または連続寸法が 1 つの連続寸法オブジェクトになる条件を満たす
  とき、新しい連続寸法オブジェクトを作って返す」。
- 連続寸法は**プラグインオブジェクト**（`kInternalID_NNA_ChainDim = 222`）。実測の
  `GetObjectTypeN` は **86 = `kParametricNode`**（直線寸法の 63 とは別）。
- **実測で、端点を共有する同じ向きの直線寸法 2 本は 1 つの連続寸法になった。**
  元の 2 本は**レイヤ直下から消えて**連続寸法の中へ取り込まれた。中身を
  `FirstMemberObj`/`NextObject` で歩くと
  **`11`（`kGroupNode` ＝ 2D 表現のグループ）・`63`・`63`・`0`** だった。
- 連続寸法そのものに対して `ovDimStandard` / `ovDimStandardName` は、どちらも
  `GetObjectVariable` が `false` を返した（中身のグループも同じ）。
  `ovDimFontSize` は `true` が返り、読み戻しも書いた値になったが、中の直線寸法には効かなかった
  （下記「〈クラスの文字スタイル〉のまま繋いだ連続寸法の文字の大きさの実測（#155）」）。
- 繋ぐ前後でレイヤ直下が 19 → 21 と**増えて**見えた。内訳を型で見ると `63` が 2 つ消えて
  **`90`（`kUndoPlaceholderNode`）が 3 つ**と `86` が 1 つ増えていた。

### 〈クラスの文字スタイル〉のまま繋いだ連続寸法の文字の大きさの実測（#155）

VW 2026 / macOS。[#155](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/155)
で実機のプローブを 4 回走らせた。4 回はすべて by-class（`ovDimTextStyle` = `-2`）の寸法で
走らせている（#157 の「明示してから繋ぐ」は上記「連続寸法へ繋いだ後の読み戻し」）。

> **【訂正の記録】** #155 当時は、下の表の左の因子を「規格が紙で決めた大きさ」と書いていた。
> #155 の図面ではカスタム規格が 1 件（`min-nano`）だけで、当たっている
> 規格と by-class で解決される文字スタイルが同じだった。
> [#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)
> が**両者を別々にして測った**ところ、**大きさは by-class で解決される文字スタイルに
> 従い、当てた規格が指す文字スタイルには従わなかった**——上記
> 「[規格と文字の大きさの関係を測った記録（#163）](#規格と文字の大きさの関係を測った記録163)」。
> 下の表の数値は測ったときのまま（同じ図面では一致する）。

**`CreateChainDimension` の前後を比べた。** 繋ぐ前の 2 本の手（ハンドル）は
中に現れず（両方とも別の手になった）、繋ぐ前に書いた `ovDimFontSize` は捨てられた。
繋ぐ前と繋いだ後で変わった欄は **`ov17`（＝`ovDimFontSize`）と、そこから計算される
`ov40`（＝`ovDimTextSizeInPoints`）の 2 つだけ**だった（`ov0`〜`ov60` を総当りした）。

元の寸法を作ったときと繋いだときのアクティブレイヤを**双方向で入れ替えて測った**
（規格 `min-nano` は紙で 6pt。6pt = 2.1167mm）:

| 元の 2 本を作ったときのアクティブ | **繋いだときの**アクティブ | 繋いだ後の中の `ovDimFontSize` | 1/50 の紙の上 |
| --- | --- | --- | --- |
| 1:1（シートレイヤ） | 1:1 | `2.11667` | 0.12pt（値が見えない） |
| **1:1** | **1/50** | **`105.833`** | **6pt** |
| **1/50**（デザインレイヤ） | **1:1** | **`2.11667`** | **0.12pt** |
| 1/50 | 1/50 | `105.833` | 6pt |

- `CreateLayer(..., kLayerSheet)` でシートレイヤを作ると、そのシートレイヤがアクティブになった。

**後から直そうとして潰した道**（すべて実測。by-class の寸法で、いずれも紙の上で
0.12pt のまま。文字スタイルを明示した連続寸法で中の型 63 へ書いた場合は、
[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164) の K1 / K2——上記
「[既に注釈へ置いた連続寸法を後から直す 6 通りの実測](#既に注釈へ置いた連続寸法を後から直す-6-通りの実測)」）:

| 試したこと | 結果 |
| --- | --- |
| 繋ぐ**前**に中の直線寸法へ書く | 繋いだ時点で捨てられる |
| 繋いだ**後**に中の直線寸法（型 63）へ書く | 書ける（`true`・読み戻せる）が**絵は変わらない**。連続寸法へ `ResetObject` を呼ぶと捨てられる |
| **連続寸法そのもの（型 86）へ書く** | 書けて読み戻せて、`ResetObject` を越えても連続寸法自身は保つのに、**中には効かない** |
| 中へ `999`・連続寸法へ `105.833` と別々に書いて `ResetObject` | 中は**どちらでもない `2.11667`**（＝繋いだときの値）になった |
| 注釈へ移す（`AddViewportAnnotationObject`）・`UpdateViewport` | 中へ書いた値は残るが、**絵は小さいまま**（値と絵が食い違う） |
| 注釈（1/50 のビューポート）の中で `ResetObject` | `2.11667` のまま——**容れ物の縮尺では解き直さない** |
| **アクティブレイヤを 1/50 にしてから**注釈の中で `ResetObject` | `2.11667` のまま——**後からの解き直しは起きない**（効くのは繋ぐ瞬間だけ） |

- **ビューポートの縮尺を変えたとき**は中の値が動いた。中へ `105.833` を書いた状態
  （値と絵が食い違っていた）で 1/50 → 1/100 にすると中の値が `211.667` になり、
  **絵もその大きさになった**。1/50 へ戻すと `105.833` で絵も `105.833` だった
  （下記「[注釈の縮尺の実測（#143）](#注釈の縮尺の実測143)」と同じ比例の書き換え）。
- **対照（同じ 1 回の実行で測った）: 単独の直線寸法。** 1:1 生まれに
  `105.833` を書いて注釈へ入れ `ResetObject` すると、**描かれている文字も `105.833`**
  （紙で 6pt）になった。何も書かない 1 本は `2.11667` のままだった。

#### by-class の連続寸法で #155 が使った手順

#155 で使った手順（アクティブレイヤの切り替えは `CreateLayer` の副作用で行った。
`ISDK::SetCurrentLayer` での切り替えは下記「注釈へ寸法を置く手順の根拠の実測」の【訂正】）:

```cpp
// 1. **出したい縮尺のレイヤをアクティブにする。** ISDK にアクティブレイヤを切り替える
//    口は無いが、CreateLayer が副作用で切り替える。
MCObjectHandle work = gSDK->CreateLayer("作業用", kLayerDesign);
gSDK->SetLayerScaleN(work, 50.0);           // 1/50 で描くなら 50
// 2. 直線寸法を作り、2 本ずつ繋ぐ。**元の寸法はどこで作ってもよい**——効くのは
//    「繋ぐときの」アクティブレイヤだけ（上の表）。規格は繋ぐ前の 1 本ずつへ当てる。
MCObjectHandle chain = gSDK->CreateChainDimension(dim1, dim2);
// 3. **文字の大きさは書かない**（書いても捨てられる）。by-class の文字スタイルの
//    紙の大きさ（規格のものではない。#163）が、
//    そのレイヤの縮尺で解かれて入る。
```

- **実測（#155）で入った値**——`1/50` がアクティブなうちに繋ぐと、中の
  `ovDimFontSize` は `105.833`（紙で 6pt 相当）になり、**寸法が内部に持つ文字図形**の
  1 文字目も `105.833mm` だった。#155 では注釈の絵は見ていない。
- 規格の紙の pt を変える道を #155 の時点では【推定】で残していたが、
  [#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163) で
  測った（上記「[規格と文字の大きさの関係を測った記録（#163）](#規格と文字の大きさの関係を測った記録163)」）。

#### 中身の読み方（測り方）

- 中の 63 に `ovDimFontSize` を書いた直後は、値だけが変わり、絵は古い大きさのままだった。
  絵の大きさは、寸法の中に描かれている文字図形（型 `10` ＝ `kTextNode`）を `GetTextSize` で
  測るか、外接矩形の高さで見た（[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)
  の物差し）。
- **2D 表現のグループ（型 `11`）の中にも直線寸法の写しが入っていたが、そこは作り直しでも
  古い大きさのまま残った**（この群の外接矩形は高さ `0`）。連続寸法の外接矩形は直下の
  `63` に追従した。
- **作り直しのたびに中の `63` の手（ハンドル）は変わった。**

### `FirstMemberObj` / `NextObject` の walk で数えた件数の内訳

**リストの末尾に `kTermNode`（型 `0`）が 1 つ入っていた。** `FirstMemberObj` → `NextObject` で
歩いて数えると、実際の図形より 1 多く出た（上の連続寸法の件数の食い違いは、型の並びを
見て内訳が分かった）。

## 寸法の関連付け（図形が動いたら寸法も追うか）

[#138](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/138) の調査。
`ISDK::AssociateLinearDimension(h, selectedObjectsMode)` を呼んだ寸法が、図形を動かしたときに
追従するかを、呼び方を変えて測った（[#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134)
の 8 通りの記録は上記「`AssociateLinearDimension` を 8 通りで呼んだ記録（#134）」）。

### 関連付けと「パラメトリック制約」の口（ヘッダを読んだ）

制約の口の一覧（宣言と説明はヘッダの写し）。制約は図形にぶら下がる
**制約ノード**（`kConstraintNode = 110`。`Objs.TDType.h:176`）。

| 口 | 宣言 | ヘッダの説明 |
| --- | --- | --- |
| `AssociateLinearDimension` | `void (MCObjectHandle h, Boolean selectedObjectsMode)` | 「**寸法**の端点が図面の図形と一致しているとき、その直線寸法を図形に関連付ける。`selectedObjectsMode` が true のときは、**選択されている図形だけ**を調べる」 |
| `HasConstraint` | `Boolean (MCObjectHandle obj)` | 「その図形が制約ノードを持つか」 |
| `CreateConstraintModel` | `void (MCObjectHandle obj, Boolean useSelection)` | 「パラメトリック制約を持ちうる図形の**位置や幾何を変える前に**呼ぶ。`useSelection` が true なら選択中の図形すべてと、それらに制約された図形・関係する制約が模型に入る」 |
| `AddToConstraintModel` | `void (MCObjectHandle obj)` | 「その図形を、いま組み立て中の模型へ足す（その図形の制約と、その図形に制約された他の図形・その制約も一緒に）」 |
| `UpdateConstraintModel` | `Boolean ()` | 「模型へ明示的に入れた図形の幾何を**図面の今の位置・幾何へ合わせ**、そのうえで模型のパラメトリック制約を**すべて解こうとする**。解けなかった制約があれば `false` を返し、**そのときは undo を呼ぶべきである**」 |
| `SetBinaryConstraint` | `Boolean (short type, MCObjectHandle obj1, MCObjectHandle obj2, short obj1VertexA, short obj1VertexB, short obj2VertexA, short obj2VertexB, Sint32 containedObj1, Sint32 containedObj2)` | 2 つの図形（または 1 図形の 2 か所）の間に制約を張る。`type` は **1 coincident / 2 colinear / 3 parallel / 6 tangent / 7 concentric / 8 distance / 9 horizontal distance / 10 vertical distance / 12 angle / 13 perpendicular**。頂点が要らない引数は `-1` |
| `SetSingularConstraint` | `Boolean (short type, MCObjectHandle obj, short vertexA, short vertexB)` | 1 図形への制約。`type` は **4 vertical / 5 horizontal / 8 distance / 9 vertical distance / 10 horizontal distance / 11 radius** |
| `GetSingularConstraint` / `GetBinaryConstraint` | `MCObjectHandle (…)` | その制約ノードを引く（**無ければ 0**） |
| `DeleteConstraint` | `void (MCObjectHandle obj, MCObjectHandle constraint)` | その図形からその制約ノードを消す |
| `GetClosestPt` | `void (MCObjectHandle& obj, const WorldPt& pt, short& index, Sint32& containedObj)` | 制約に渡す**頂点番号**を座標から引く（2D 図形のみ。近い頂点が無ければ `0`、型が非対応なら `-1`） |
| `BuildConstraintModelForObject` / `RecordModifiedObjectInConstraintModel` | `void (…)` | **説明文が SDK に無い**（名前から見て上の一式の別入口） |
| `SetHorizontalDimensionConstraint` / `SetVerticalDimensionConstraint` / `DeleteDimensionConstraints` | VW 2021 で追加 | **説明文が SDK に無い**。`Set…(obj1, pt1, pt2, distance, offset)` |

ヘッダの説明から組んだ呼び順（下の実測の 2 行目）:

```cpp
gSDK->CreateConstraintModel(nil, true);   // ① 動かす **前** に（選択中の図形で模型を作る）
gSDK->MoveObject(line, 1200, 0);          // ② 動かす
if (!gSDK->UpdateConstraintModel())       // ③ 解く
	/* 解けなかった。undo すべき */;
```

#134 が呼んだのは `MoveObject` と `ResetObject` だけで、③ は一度も呼んでいない。

### 解く段（`UpdateConstraintModel`）まで呼んだ実測（#138）

測ったコード:

```cpp
// ① 関連付ける。h は **寸法**。false は「図面の全図形を調べる」（選択は要らない）
gSDK->AssociateLinearDimension(dim, false);

// ② 動かすときは、動かす **前** に模型を作り、動かした **後** に解く
gSDK->DeselectAll();
gSDK->SelectObject(line, true);
gSDK->CreateConstraintModel(nil, true);
gSDK->MoveObject(line, 1200, 0);
gSDK->UpdateConstraintModel();   // ← これを呼ばないと測点は 1mm も動かない
```

実測（測点 `(0, y)`→`(3000, y)` の寸法と、両端に端点を重ねた縦棒 2 本。**線A だけを
`+1200` 平行移動**して、測点の x が `0` のままか `1200` になるかを見た）:

| 更新の引き金 | 関連付いたか（`HasConstraint`） | 測点が追ったか |
| --- | --- | --- |
| `MoveObject` + `ResetObject` だけ（#134 の経路） | **はい** | **いいえ**（`0` のまま） |
| `CreateConstraintModel(nil, true)` → `MoveObject` → `UpdateConstraintModel` | はい | **はい**（`1200`） |
| `BuildConstraintModelForObject` → `MoveObject` → `RecordModifiedObjectInConstraintModel` → `UpdateConstraintModel` | はい | **はい**（`1200`） |
| `CreateConstraintModel(線分, false)` + `AddToConstraintModel(寸法)` → `MoveObject` → `UpdateConstraintModel` | はい → **消える** | いいえ（※下記） |

- **`selectedObjectsMode` は `true` でも `false` でも同じ**だった（どちらでも関連付き、
  どちらでも追った）。選択せずに `false` で呼んでも関連付いた。
- 表は 1 つの `dimType`（`0`）で取った（`dimType` `0` / `1` の比較は
  [上記](#0fix_angと-1slopedを並べて比べた実測134)）。
- **`UpdateConstraintModel` は 4 通りとも `true`（解けた）を返した。**
- 1 行目は VW へ戻った後に読み直しても同じだった
  （[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。関連付けた寸法の線を `MoveObject` で +1200 動かし、`ResetObject` だけで
  `UpdateConstraintModel` を呼ばない——測点は undo イベントを閉じても、戻って数十秒置いても
  0 のまま）。
- #245 では「`OpenDocumentPath(nullptr)` の空の図面では成立しない」と見えた。そのときは
  1/1 の空の図面で y=60000 に寸法を置いていた（下記「[表示範囲と関連付けの実測（#249）](#表示範囲と関連付けの実測249)」で測り直した）。

#### 表示範囲と関連付けの実測（#249）

VW 2026 / mac。[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)。
`probes/runtime/t0-mismatch`。測点 `(x, y)`→`(x+3000, y)` の寸法と、両端に端点を重ねた
縦棒 2 本を立て、選択せずに `AssociateLinearDimension(dim, false)` を呼んで `HasConstraint(dim)` を読んだ。
`varAssociateDims`=`1`。

| 図面・表示 | 成立した | 成立しなかった |
| --- | --- | --- |
| 空の図面・1/1・表示の中心 (0,0)・用紙 ±99×±138 | y = 0 / 2000 / 4000 | y = 6000 以上（〜60000）、x = ±20000 |
| 同じ図面で**用紙を ±100000 へ広げた** | y = 0〜4000 | y = 6000 以上（**用紙は関係しない**） |
| 同じ図面で**表示の中心を (1500, 60000) へ動かした** | **y = 60000 / 61000** | **y = 0 / 4000**（今度は原点側が外れる） |
| 空の図面・**1/50** | y = 0〜60000（振った全部） | — |
| 空の図面・1/1 で **`SetZoomFactorN(0.01)`** | y = 70000 | y = 400000 |
| テンプレートの図面・1/100・中心 (4099, −5904) | y = 0〜400000 | y = 1000000 |

- 図面の出どころ・用紙・レイヤ数・`varCreateDimsInDimClass`・選択の有無を変えても結果は
  変わらなかった。境目の正確な形（ウインドウの大きさに対する比など）は測っていない。
- `GetViewCenter` で控え、`SetViewCenter(寸法の中ほど)` → `AssociateLinearDimension` →
  `SetViewCenter(控え)` とすると成立した。寄せて関連付けた後に表示を戻した対照（y=66000）は
  成立しなかった。

#### `HasConstraint` と `FindAuxObject` の読みの実測

実測で `HasConstraint(寸法)` と `FindAuxObject(寸法, 110)` は**常に一致**した。

| | 寸法 | 相手の線分 |
| --- | --- | --- |
| 関連付ける前 | `HasConstraint=いいえ` / 補助オブジェクトの型 `[76]` | 同左 |
| `AssociateLinearDimension` の後 | **`はい`** / `[110]`（`kConstraintNode`） | **`はい`** / `[110]` |
| 拘束が消えた後 | `いいえ` / `[90]`（`kUndoPlaceholderNode`） | — |

- `[76]` は関連付ける前から付いていた。
- `GetDimensionsAssociatedToPlugin` は引数が PIO（ヘッダ）。

#### 関連付けた後に VW が出したダイアログ（coincident の拘束）

上の表の 4 行目（`CreateConstraintModel(線分, false)` + `AddToConstraintModel`）で、VW は
確認ダイアログを出した（目視）:

> **関連する拘束を削除しますか？**
> 1 つ以上の関連する拘束がありこの編集操作を実行できません。関連する拘束を削除すると
> 操作が続行されます。

続く「拘束確認」の一覧に並んだのは **タイプ「同一点上」／カテゴリ「寸法」**。
「はい」を押すと拘束が消え（`HasConstraint` が `いいえ` に戻り、補助オブジェクトの型が `[90]`
になった）、追従しなかった。`CreateConstraintModel(nil, true)`（選択で渡す）の経路では
ダイアログは出なかった。このダイアログを抑止する環境設定を `ProgramVariables.h` で探したが
見つからなかった。

#### `varAssociateDims`(28) と `varAutoAssociateDims`(134) を総当たりした実測

| selector | 名前 | 置き場所 |
| --- | --- | --- |
| `28` | `varAssociateDims` | `ProgramVariables.h:43`（Boolean selectors） |
| `134` | `varAutoAssociateDims` | `ProgramVariables.h:162`（More Boolean Selectors） |

どちらも `ISDK::GetProgramVariable` / `SetProgramVariable` で読み書きした
（**Boolean selector** なので 1 バイト）。4 通りを総当たりした実測:

| `varAssociateDims`(28) | `varAutoAssociateDims`(134) | 関連付いたか | 測点が追ったか |
| --- | --- | --- | --- |
| `1` | `1` | はい | **追従した** |
| `1` | `0` | はい | **追従した** |
| `0` | `1` | **いいえ** | しない |
| `0` | `0` | **いいえ** | しない |

- `varAssociateDims`(28) が `0` の 2 通りは、`HasConstraint` は `いいえ` のまま、補助
  オブジェクトの型も `[76]` のままで、その後 `UpdateConstraintModel` を呼んでも追わなかった。
- **実機（VW 2026）の新規図面の走り出しは `28=1` / `134=0`** だった。
- **画面（目視）。** 上の 4 通りを 1 つの図面に上から順に並べて描かせたところ、
  **拘束マークが出たのは上の 2 段（`28=1` の 2 通り）だけ**だった。

#### 他に試した道

- **`SetBinaryConstraint` で自分で coincident を張る。** 頂点番号を引く
  `GetClosestPt` が、**寸法に対して `-1`（＝型が非対応）**を返した（線分側は `1` を返した）。
  `-1` のまま呼ぶと `SetBinaryConstraint` は `false` だった。
- **`SetHorizontalDimensionConstraint(線分, pt1, pt2, distance, offset)`。**
  戻り値は `true` で、渡した線分に拘束ノード（`[110]`）は付いたが、**レイヤの図形数は
  増えなかった**（32 → 32）。

#### 画面でドラッグしたときの寸法の実測

実測（関連付け済みの 1 組を残しておき、利用者が線A を画面でドラッグしたあとに読み直した）:

| | 線A の x | 寸法の測点（始点） |
| --- | --- | --- |
| ドラッグ前 | `0` | `0` |
| ドラッグ後 | **`1001.783`** | **`1001.783`**（ぴったり一致） |

線B（触らない側）は `3000` のままで、寸法の終点も `3000` のままだった。

## 寸法をビューポートの注釈へ入れる

- 移し方は `ISDK::AddViewportAnnotationObject(viewport, annotation)`
  （[Data Tags](../Findings/Data%20Tags.md) / [Viewports](../Findings/Viewports.md) と同じ）。
- ヘッダの写し: `ovDimTranslateInVP`(1234) のヘッダに
  "Used to move a dimension that is created in a viewport annotation by the exterior
  wall dimensioner only" とある。
- 注釈へ後から足した図形のクラスがビューポートで非表示のままになる件は
  [Viewports](../Findings/Viewports.md)。寸法でも同じだった（下記「作法」の 5）。

**実測（VW 2026 / macOS）:**

- `AddViewportAnnotationObject(viewport, dimension)` は **`true`** を返し、移した後も
  その寸法は**型 63 のまま生きていて**、`ovDimStandardName` などのオブジェクト変数も
  そのまま読めた。
- **外接矩形は移す前後で変わらなかった。** 注釈空間の座標は `CreateLinearDimension` に
  渡した座標そのままで、移動や座標変換は掛からなかった。
- 作った直後はアクティブレイヤ直下に**いて**、`AddViewportAnnotationObject` の後は
  **いなくなった**。
  - `CreateLayer(..., kLayerSheet)` でシートレイヤを作るとアクティブレイヤがそちらへ
    移ったので、判定は寸法を作る直前に `GetActiveLayer()` を取り直して行った。
- 平面ビューポートの `GetObjectTypeN` は **122**。

### 注釈へ寸法を置いただけのビューポートの実測

**実機で踏んだ。** 断面ビューポートを作って注釈へ寸法を 2 本足し、`UpdateViewport`
まで呼んだのに、シートレイヤ上のビューポートは **53.3mm 角の「×」印の空枠**
（`GetObjectBounds` も `左-26.64 上26.64 右26.64 下-26.64`）のままで、
**断面も寸法も一切描かれなかった**。

[Viewports](../Findings/Viewports.md) に書いてあるビューポートの作法を次の順で踏んだ:

1. **レンダリングを隠線消去にする**（`ovViewportRenderType`(1001) ←
   `renderFinalHiddenLine` = 6）。シェイドのままでは
   `ovViewportDisplay2DComponents`(1059) が入らなかった。
2. **クラスをすべて表示へ戻す。** ビューポートは**既定でクラスが全部消えていた**
   （`ForEachClass(true, …)` ＋ `SetViewportClassVisibility(vp, GetObjectInternalIndex(cls), 0)`）。
3. 断面の表示の作法（`ovSectionViewportDisplayObjectsBeyondCutPlane`(1064) ＋
   **`ovSectionViewportDisplayObjectsBeforeCutPlane`(1065)** /
   `ovViewportDisplayPlanar`(1035) / `ovViewportDisplay2DComponents`(1059)）を
   **すべて更新より前に**設定する。切断面が何も切っていない
   配置では、`1065` が `false` の間は `1064` を立てても断面は空のままだった
   （[Viewports](../Findings/Viewports.md)「断面に中身が入る条件」。#202 で実測）。
4. 表示レイヤを表示にする（`SetViewportLayerVisibility`）。
5. **注釈へ寸法を足した後、もう一度クラスを全部表示へ戻して再更新する。**
   ここを飛ばすと、断面は描かれているのに**寸法だけが見えなかった**。

**この手順を踏んだら注釈の寸法は見えるようになった。** 踏む前は断面
ビューポートが「×」印の空枠で寸法も何も出ず、踏んだ後は**寸法が縦横 1 本ずつ
はっきり見え、値も渡した 2 点間の距離どおり**（4000mm と 2400mm）に出た。

### ビューポートの外接矩形の実測（#200）

> **【訂正の記録】ここには「ビューポートの外接矩形は中身を勘定に入れない。注釈へ図形を
> 足しても増えないし、断面に写る図形が増えても増えない」と書いていた。**
> [#200](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/200)
> で実機確認して取り直した（数値は [Viewports](../Findings/Viewports.md)「`GetObjectBounds(viewport)` が
> 何で決まるか」）:
>
> 1. **注釈が外接に入ったのは `ResetObject` を通した後だけ。** 足した直後は 1mm も動かなかった。
> 2. **空枠（53.3mm 角 ＝ ±26.649）より大きいものだけが外接に現れた。**
>    空枠は中身や注釈に**置き換わった**（和を取るのではない）。
>
> **あのときの寸法 4000mm / 2400mm は、1/100 なら用紙で 40mm / 24mm**——**どちらも
> 53.3mm 角の内側に収まる。** 注釈へ 4 辺とも空枠の外へ出る矩形を置いた枚では、外接は**注釈の矩形
> そのもの**になった。**高さ 0 → 2800mm の壁（用紙で 28mm）も外接に出ていなかった。**

当時の観察: 「中身が描かれたか」を機械で確かめようとして
`GetObjectBounds(viewport)` を作法の前後で比べたが、**注釈に寸法が 2 本見えている
状態でも、外接矩形は空枠のときと 1mm も変わらなかった**（どちらも
`左-26.64 上26.64 右26.64 下-26.64` の 53.3mm 角）。

- `GetViewportGroup(vp, 3 / 5 / 6 / 7 / 15)` のキャッシュ群を数えると、断面が描けているときは
  どれかに件数が入っていた（群 4 は断面群で、空でも必ず在った。[Viewports](../Findings/Viewports.md)
  「断面に中身が入る条件」）。

### 断面ビューポートの注釈空間の座標の実測

**実測（VW 2026 / macOS）。** 断面線を `(-500, -1500)` → `(4500, -1500)`（長さ 5000mm、
`+x` 向き）に引き、`(2000, 1500)` 側から見る断面ビューポートを作って、
`(0,0)`-`(4000,0)` の壁を写した。そのときの注釈空間の座標:

| 模型上の位置 | 注釈空間の横座標 |
| --- | --- |
| 壁の左端 `x = 0` | **`-4500`** |
| 壁の右端 `x = 4000` | **`-500`** |

値は「（断面線に沿った位置）−（断面線の終点）」に一致した。最初は始点基準で置いて、
**断面線の長さちょうどぶん（この例では 5000mm）ずれた**。

**置き直して確かめた。** この式で `-4500`〜`-500` に寸法を置き直したところ、**断面に写った壁の
左端から右端までぴったりに掛かった**（縦も壁の足元から Z=2400 まで）。ずれていた版と
直した版を実機で見比べた。

- **縦 = Z**（実測で一致）。
- 向きは**視線の右手が模型の `+x`** になる並びだった（断面線 `+x` 向き・`+y` を見る配置）。

### 注釈の縮尺の実測（#143）

> **【訂正の記録】** ここには以前「注釈空間は用紙 1:1 なので、文字は紙のポイントのまま出る」と
> 書いていた（#132）。[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
> で取り直した。#132 で比べた 2 本は**置き場所以外も違っていた**——注釈へ入れた側は
> シートレイヤ（1:1）がアクティブなときに作った寸法で、しかもその平面ビューポートの
> 縮尺は既定の 1:1 のままだった（当時はそれを「注釈だから 2.116 になった」と読んでいた）。

実測（VW 2026 / macOS。縮尺 **1/50** の平面ビューポートと断面ビューポートへ、同じ規格・
`ovDimShowValue = true` の寸法を移した。**絵で確認済み**）:

| 作ったときのアクティブレイヤ | `ovDimFontSize` | 紙の上（＝ ÷ 50） | 実機の絵 |
| --- | --- | --- | --- |
| デザインレイヤ 1/50 | `105.833` | **2.1167mm ＝ 6pt** | **値が出る** |
| シートレイヤ 1:1 | `2.1166` | 0.0423mm ＝ **0.12pt** | **寸法線は出るが値が出ない** |

- 1:1 生まれの寸法は、`CreateLayer(..., kLayerSheet)` で作ったシートレイヤがアクティブな
  ときに作ったものだった。伏図の寸法は 1/50 のデザインレイヤがアクティブなうちに作っていた。
- どの絵で何が見えたか——**1:1 生まれが「寸法線だけ・値なし」になったのを今回の絵で見たのは
  平面ビューポートの注釈**で、**断面ビューポート**で同じことが起きていたのが利用側の最初の実測
  （[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)）。
  **読み戻した数値のほうは平面・断面で完全に同じ**だった（移しても `ovDimFontSize` は
  変わらず、ビューポートの縮尺を変えれば両方とも同じように書き換わった）。断面の注釈へ
  正しい大きさ（`105.833`）の寸法を 2 本置いた絵では、**どちらも値が読め、大きさも
  揃っていた**。
- **`AddViewportAnnotationObject` の前後で `ovDimFontSize` は変わらなかった。**
- **ビューポートの縮尺を変えた。** 1/50 → 1/100 にすると注釈の中身の `ovDimFontSize` が
  `105.833` → `211.666` になり、1/50 へ戻すと `105.833` に戻った（平面・断面とも）。
- **注釈群そのものの縮尺。** `GetViewportGroup(vp, kViewportGroupAnnotation)`
  が返すのは型 `11`（`kGroupNode`）で、`GetLayerScaleN` を当てると `1` が返った。
- **`CreateViewport` / `CreateSectionViewport` が作ったビューポートは、どちらも直後の
  `ovViewportScale` が `1.0` だった。**

### 注釈へ寸法を置く手順の根拠の実測

手順のコードは [Findings](../Findings/Dimensions.md#注釈へ寸法を置くときの作り方ここだけ読めばよい) にある。
ここにはその根拠の実測だけを置く。

- **手順 4（`ResetObject`）は [#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
  で足した。** #143 当時の記述にこれが無く、#157 の再現実験は 3 回とも
  「値が出ない」になった（上記
  「[書いた直後と引き直した後の絵の文字の大きさの実測（#161）](#書いた直後と引き直した後の絵の文字の大きさの実測161)」）。
  #157 では一時「当たっている文字スタイルがあると効かないので使うな」と書いていたが、#161 が取り消した。
- 1:1 で作った寸法に `ovDimFontSize` ＝ `6 × 25.4 / 72 × 50` を書いたら、1/50 のビューポートと
  同じ縮尺で作った寸法と**見分けが付かない大きさで値が出た**。
- 「ビューポートと同じ縮尺のレイヤをアクティブにしてから作る」道でも同じ結果になった（実測）。
- 移した後に `ovDimFontSize` を書いても効いた（`ResetObject` 込み）。
- 1:1 生まれに `ovDimFontSize` だけ書いた 1 本と、1/50 生まれの 1 本を
  同じ注釈へ並べたら、値も端記号も補助線も見分けが付かなかった（[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)。
  上記 ④）。

> **【訂正の記録】** ここには一時「`ISDK` にアクティブレイヤを切り替える口は無い——`CreateLayer`
> が副作用で切り替えるだけ」と書いていた。
> **`ISDK::SetCurrentLayer(MCObjectHandle)`**（`ISDK.h:1394`）と
> **`SetCurrentLayerN(MCObjectHandle, Boolean bAllowUnifiedViewMaintenance = false)`**
> （`ISDK.h:2761`）があり、いまのアクティブレイヤは **`ISDK::GetActiveLayer()`**
> （`ISDK.h:2020`）で読める。実機で効くことを
> [#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175) の
> プローブで確かめた——同じ文字スタイルを `SetCurrentLayer` で 1/50 と 1:1 を
> 切り替えて当てたら、焼き付いた値が `176.38889` と `3.52778` に分かれた。
