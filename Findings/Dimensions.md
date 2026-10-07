# 寸法（直線寸法・寸法規格）

平面図・断面のビューポートへ**寸法を自動で入れる**ための調査（[#129](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/129)）。
寸法そのものを作る口と、寸法の見え方を決める**寸法規格**（dimension standard）の
扱い方をまとめる。

> **【訂正の記録】[#132](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/132)
> でマージした「注釈空間は用紙 1:1 なので、文字は紙のポイントのまま出る」は誤りだった**
> （[issue #143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
> で取り直した）。**ビューポートの注釈は、そのビューポートの縮尺で描かれる。**
> 誤りの元は 2 つ——比べた 2 本が「注釈へ移したから小さい」のか「**シートレイヤ（1:1）が
> アクティブなときに作った**から小さい」のかを分けていなかったことと、そのときの平面
> ビューポートが `CreateViewport` の既定の縮尺（**1:1**）のままだったこと。正しい内容は
> 下記「[文字の大きさは作るときのアクティブレイヤの縮尺で焼き付く](#文字の大きさは作るときのアクティブレイヤの縮尺で焼き付く)」と
> 「[注釈はビューポートの縮尺で描かれる](#注釈はビューポートの縮尺で描かれる用紙-11-ではない)」にある。

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
- **規格の寸法値は「用紙インチ」**（`page inches`）で定義されている。補助線の長さと
  隙間・寸法線の出・端記号の大きさ・文字と寸法線の間隔は**縮尺に依らず紙の上の
  大きさ**で決まり、**寸法 1 本ごとに写し取られることはない——描くたびに、そのとき
  当たっている規格と、そのとき入っている容れ物の縮尺から解き直される**（実測確定。
  下記「[規格の用紙インチは描くたびに解かれる](#焼き付くのは文字の大きさだけ規格の用紙インチは描くたびに解かれる)」）。
  **ただし文字の大きさだけは例外で、「図面上の mm」として焼き付く**（すぐ下）。
- **寸法が自分で持っている大きさは `ovDimFontSize`（図面上の mm）ただ 1 つで、
  `CreateLinearDimension` を呼んだ時点のアクティブレイヤの縮尺で焼き付く。**
  後からレイヤの縮尺を変えても追随しない（下記「文字の大きさは作るときの
  アクティブレイヤの縮尺で焼き付く」）。
- **`ovDimTextSizeInPoints` は寸法が持っている値ではない。** 読むたびに
  「`ovDimFontSize` ÷ **読む時点のアクティブレイヤ**の縮尺」を pt に直したものが返る
  （入れ物の縮尺ですらない）。**紙の上で何 pt かの判断に使ってはいけない。**
- **★ `ovDimFontSize` へ書いた大きさは、書いた瞬間には絵に出ない——`ResetObject` を
  呼ぶまで絵は書く前のままである**（実機の絵で確定。[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)）。
  **読み戻しは書いた値を返すので、呼び忘れても読み戻しでは気付けない。**
  引き直しになるのは `ResetObject` / `CreateChainDimension` / ビューポートの縮尺変更で、
  **`AddViewportAnnotationObject` も `UpdateViewport` も引き直しではない。**
  下記「[★ 絵の文字の大きさは、書いた瞬間には変わらない](#-絵の文字の大きさは書いた瞬間には変わらない引き直しで反映される確定)」。
- **注釈へ値（数字）を出す道は 2 つあり、どちらでもよい。**
  (a) **`ovDimFontSize` を書いて `ResetObject`**（#143 の道。図面に何も増やさない）、
  (b) **`gSDK->SetTextStyleRef(dim, ref)` で文字スタイルを明示**（#157 の道。
  `SetTextStyleRef` は絵の側もその場で書き換えるので引き直しが要らない。大きさは
  **`ovTextStyleSize`（インチ）＝ 紙の pt ÷ 72 × ビューポートの縮尺**）。
  **`ovDimTextStyle`(1248) へ番号を書くだけの道は使えない**——番号は変わるが大きさが
  付いてこない。**3 つは読み戻しで見分けが付かない。**
  寸法規格も文字スタイルを持ち、`GetDimensionStandardVariable(index,
  `dimStdTextStyle`(51), …)` で ref number として読める（組み込み規格 1〜9 は `0`
  ＝持たない）。下記「[寸法の文字スタイル](#寸法の文字スタイル)」。
- **寸法から文字スタイルを外す口は `gSDK->SetTextStyleRef(dim, 0)`**（＝ Un-Styled。
  `ResetObject` を越えて残る）。**ただし外しても絵は変わらない**ので、大きさを直したい
  だけなら外さなくてよい（#161）。
- **規格の側から文字の大きさを決める道は無い。** 規格に「文字の大きさ」のセレクタは
  存在せず（`dimStd*` は 1〜52 で全部。文字に関わるのは `dimStdTextStyle`(51) と
  `dimStdTextPos`(52) と公差用の `dimStdTolSizeFac`(26) だけ）、**結び付いた文字スタイルを
  `SetCustomDimensionStandardVariable` で差し替えられはするが（カスタム規格のみ。
  組み込みは `false` で弾かれる）、それは寸法に届かない**——規格を替えても、規格を
  文書の既定にしてから作っても、寸法の `GetTextStyleRef` は動かず大きさも変わらない
  （実測確定。下記「[規格に「文字の大きさ」は無い](#規格に文字の大きさは無い持っているのは文字スタイルへの参照-1-本でそれは寸法に届かない)」）。
  **紙の大きさを決めるのは `SetTextStyleRef` で当てる文字スタイルだけである。**
- **ビューポートの注釈は、そのビューポートの縮尺で描かれる**（用紙 1:1 ではない）。
  注釈へ置く寸法には **`ovDimFontSize` ＝ 紙の pt × 25.4 / 72 × ビューポートの縮尺**
  を書く（下記「注釈へ寸法を置くときの作り方」）。**`CreateViewport` /
  `CreateSectionViewport` が作るビューポートの縮尺の既定は `1:1`** なので、
  縮尺は自分で書く。
- **連続寸法（チェーン寸法）の中の直線寸法は、繋ぐときに作り直される。**
  （**繋ぐ直前に `ovDimFontSize` を書いておけばその値で作り直される**——#161 の実測で、
  繋いだ後の中の直線寸法も絵も書いた値どおりだった。以下は**書かずに繋いだとき**の話。）
  〈クラスの文字スタイル〉のままで**後から**直そうとすると道が無い
  ——繋いだ後・連続寸法そのもの（型 86）・注釈の中、どこへ書いても捨てられ、
  **入る値は「by-class で解決される文字スタイルの大きさ × 繋ぐときのアクティブレイヤの
  縮尺」ただ 1 つ**になる（元の寸法をどこで作ったかも、容れ物の縮尺も、後から変えた
  アクティブレイヤも効かない。双方向で実測）。**左の因子は「当てた規格」のものではない**
  ——規格を差し替えても動かない（#163。すぐ下）。**文字スタイルを明示してあれば保つ**ので（すぐ上・#157）、
  **注釈は文字スタイル版の手順でよい**。by-class のまま置く連続寸法の大きさだけが
  この規則に従う——下記
  「[〈クラスの文字スタイル〉のままなら文字の大きさは「繋ぐときのアクティブレイヤ」で決まる](#クラスの文字スタイルのままなら文字の大きさは繋ぐときのアクティブレイヤで決まる)」。
- **作った寸法はその場でアクティブレイヤの中に入る。** 未挿入のハンドルは返らない。
- **`AssociateLinearDimension` は効く。ただし「解く段」を自分で呼ぶ。** 関連付けは
  パラメトリック制約（**同一点上＝coincident**）の仕組みの一部で、制約は図形を動かせば
  勝手に解かれるのではなく、**`CreateConstraintModel(nil, true)`（動かす前）→ 動かす →
  `UpdateConstraintModel()`（解く）**という段を呼び出し側が踏む。これを呼べば測点が
  追う。`MoveObject` ＋ `ResetObject` だけでは 1mm も動かない（[下記](#寸法の関連付け図形が動いたら寸法も追うか)）。
- **利用者が画面で図形を動かしたぶんには、何もしなくても追う。** 解く段まで VW が自分で
  やっている。つまり**寸法を入れるときに関連付けておけば、その後の追従は任せられる**
  ——[#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134) の
  「図形が動いたら寸法を作り直す前提でよい」は不要になった。
- **関連付いたかは `HasConstraint(h)` で読める**（`void` の代わり）。**門は文書環境設定
  `varAssociateDims`(28) ただ 1 つ**で、これが off だと `AssociateLinearDimension` は
  何もしない（`varAutoAssociateDims`(134) は関与しない）。**新規図面の既定は on** なので、
  ふつうは読んで確かめるだけでよい。

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

#### `AssociateLinearDimension` が効かなかったのは、解く段を呼んでいなかったから

**この節の「効かない」は
[#138](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/138)
で撤回した。** [下記「寸法の関連付け」](#寸法の関連付け図形が動いたら寸法も追うか)が
確定した内容で、ここには #134 で何を測ったかだけを残す。

寸法の測点とちょうど重なる端点を持つ線分を 2 本立て、`AssociateLinearDimension` を
呼んでから線分を `MoveObject` で動かし、`ResetObject` を呼ぶ——を **8 通り**
（`h` に**寸法**を渡す／**図形**を渡す × `selectedObjectsMode` `true`／`false` ×
`dimType` `0`／`1`）試して、**どれも測点が 1mm も動かなかった**（軌跡点を相手にしても
同じ）。ここまでは事実で、**`dimType` `0` と `1` に差が出ないことの根拠としては
そのまま有効**である。

**誤っていたのは、そこから「関連付いていない」と読んだこと。** #138 で
`HasConstraint` を見たところ、**この 8 通りでも関連付けそのものは成立していた**。
追従しなかったのは、**制約を解く段（`UpdateConstraintModel`）を一度も呼んで
いなかった**から。呼び方は[下記](#こう呼べば効く関連付けは成立していて足りなかったのは解く段だった)。

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
`operator=` だけで、**明示的に定義されている setter は `SetUint8` ただ 1 つ**（`Uint8` は
`DEFINE_TYPE` の並びに無く、型が衝突するため別に置かれている）。書くときは
`v = static_cast<Sint8>(index);` とする。

- **`SetBoolean` は無い**（VW 2026 の SDK。`-fsyntax-only` で踏んだ）。`Boolean` は
  `DEFINE_TYPE(Boolean)` の並びに入っているので、**`operator=` で書く**:
  `v = static_cast<Boolean>(1);`。読みは `GetBoolean(bool&)` が別に定義されている。

### 文字の大きさは作るときのアクティブレイヤの縮尺で焼き付く

**寸法が自分で持っている大きさは `ovDimFontSize`(17)（図面上の mm）ただ 1 つ。**
`CreateLinearDimension` を呼んだ時点の**アクティブレイヤの縮尺**で決まり、以後ひとりでに
変わることはない。実測（VW 2026 / macOS。同じ規格・同じ 6pt の設定で作り分けた）:

| 作ったときのアクティブレイヤ | `ovDimFontSize` |
| --- | --- |
| デザインレイヤ 1/100 | `211.666` |
| デザインレイヤ 1/50 | `105.833` |
| シートレイヤ 1:1 | `2.1166` |

6 pt = 6 × 25.4 / 72 = **2.1167 mm** で、どの行も `2.1167 × 縮尺` にきれいに一致する。
つまり**「紙で決めた大きさ」を、作るときの縮尺で割り戻した実寸が焼き付く**。

> **【訂正】その「紙で決めた大きさ」は規格のものではない。** 実体は**その寸法に
> 当たっている文字スタイルの `ovTextStyleSize`（インチ）**で、#157 が式ごと押さえ、
> [#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)
> が**規格と文字スタイルを別々にして**「規格を差し替えても動かない」ことを確かめた
> ——上記「[規格に「文字の大きさ」は無い](#規格に文字の大きさは無い持っているのは文字スタイルへの参照-1-本でそれは寸法に届かない)」。
> この図面では規格 `min-nano` が `寸法(6pt)` を指しているので**数値は一致する。**

- **後からレイヤの縮尺を変えても追随しない。** 1/100 で作った寸法（`211.666`）を
  載せたまま `SetLayerScaleN` でそのレイヤを 1/50 にしても、`ovDimFontSize` は
  `211.666` のまま——**紙の上では 6pt のつもりが 12pt になる**。読み戻しでも絵でも
  そうなった（同じレイヤに並べた 1/50 生まれの寸法より明らかに大きく描かれた。
  計算どおり 2 倍）。
- **`ovDimFontSize` は書ける。** 書いた値は `ResetObject`・規格の当て直し
  （`ovDimStandardName` の書き戻し）・`ovDimShowValue` の立て直しのいずれでも
  巻き戻らない（実測）。**大きさを直す手はこれ。**
  **例外は連続寸法（チェーン寸法）の中の直線寸法**——そちらは繋ぐときと `ResetObject` の
  たびに作り直されるので、**by-class のままなら書いた値は残らない**（下記
  「[〈クラスの文字スタイル〉のままなら文字の大きさは「繋ぐときのアクティブレイヤ」で決まる](#クラスの文字スタイルのままなら文字の大きさは繋ぐときのアクティブレイヤで決まる)」）。
- **焼き付くのは文字の大きさだけである**（[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)
  で測り直して確定した）。補助線・寸法線の出・端記号・文字と寸法線の間隔は、規格の
  用紙インチから**描くたびに解き直される**ので、どの縮尺で作った寸法でも見え方は狂わない。
  下記「[規格の用紙インチは描くたびに解かれる](#焼き付くのは文字の大きさだけ規格の用紙インチは描くたびに解かれる)」。

#### `ovDimTextSizeInPoints` は「読む時点のアクティブレイヤ」で割った値——当てにしない

`ovDimTextSizeInPoints`(40) は**寸法が持っている値ではない**。読むたびに
**`ovDimFontSize` ÷（そのときアクティブなレイヤの縮尺）** を pt に直したものが返る。
**その寸法が入っている容れ物の縮尺ですらない。**

**決め手の実測。** デザインレイヤ（1/50）に置いたままの 1 本を、2 つの時点で読んだ
（`ovDimFontSize` は両方とも `105.8333` で動いていない）:

| 読んだ時点のアクティブレイヤ | `ovDimTextSizeInPoints` |
| --- | --- |
| そのデザインレイヤ（1/50） | **`6`** |
| シートレイヤ（1:1）※間にシートレイヤを作っただけ | **`300`** |

間に変えたのは**アクティブレイヤだけ**である——寸法も、寸法が乗っているレイヤも、
その縮尺も動かしていない（`105.8333mm` は 1:1 で読めば ちょうど 300pt）。

- **これを読んで「紙の上で何 pt か」を判断してはいけない。** シートレイヤや
  ビューポートの注釈を触っている最中はアクティブがシートレイヤ（1:1）になって
  いることが多く、そこでの読みは **`ovDimFontSize` を mm→pt に直しただけの数字**になる。
- **書くときも同じ縮尺で解釈される。** アクティブが 1:1 のときに
  `ovDimTextSizeInPoints` へ `6` を書くと、入るのは `ovDimFontSize` = `2.1167`
  ——紙の 6pt ではない（実測。書き込み自体は `true` を返すので、戻り値では気付けない）。
- **判断にも指定にも `ovDimFontSize` を使う。** 紙で `P` pt に見せたいなら
  `ovDimFontSize = P × 25.4 / 72 × （その寸法が描かれる場所の縮尺）`。

### 焼き付くのは文字の大きさだけ——規格の用紙インチは描くたびに解かれる

**確定（VW 2026 / macOS。[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)）。**
補助線の長さと隙間・寸法線の出（overhang）・端記号の大きさ・文字と寸法線の間隔は、
**寸法の中に写し取られていない**。描くたびに **① そのとき当たっている規格の用紙インチ**
と **② そのとき入っている容れ物の縮尺**（デザインレイヤならレイヤの縮尺、ビューポートの
注釈ならビューポートの縮尺）から解き直される。**作ったときの縮尺は一切効かない。**

> **`ovDimFontSize` だけが例外**（上記）。同じ 1 本の中で「文字の大きさは動かないのに
> 線の幾何は動く」が観測できるので、これが物差しの校正にもなっている。

#### 測り方——文字を消した寸法の外接矩形で線の幾何を測る

`ovDimShowValue = false` にした水平な寸法（測点 `(0,y)`-`(L,y)`・オフセット `d`）の
外接矩形から、規格の用紙長さが割り出せる:

```
出（overhang） = (幅 - L) / 2
補助線の出     = 上端 - (y + d)
補助線の隙間   = 下端 - y
```

**使う前に 2 つ校正が要る**（どちらも実測で踏んだ）:

- **作った直後の外接矩形は値の文字を含んでいる。** `CreateLinearDimension` の直後に
  `ovDimShowValue = false` を書いても、**描き直される（`ResetObject`・レイヤやビュー
  ポートの縮尺の変更）まで外接矩形は縮まない**。どの縮尺で作っても「寸法線から上へ
  紙の 3.548mm」という同じ値が出て、**描き直した後はそれが規格どおりの 1.2mm に
  縮んだ**ので、差の 2.348mm は文字ぶんと読める（規格 `min-nano`・6pt）。
  **測る前に `ResetObject` を呼ぶこと。**
- **端記号は外接矩形に入らない**（素の線のマーカーと同じ。[Attributes and Classes](Attributes%20and%20Classes.md)）。
  **`GetMarkerPolys` も寸法に対しては始・終とも「なし」を返す**——値を持っていても
  返さないのではなく、寸法では使えない。**端記号の大きさを機械で測る道は無い**ので、
  そこだけは絵で確かめた（下記）。
- **`SetLayerScaleN` が効いたことを読み戻して確かめる。** 効かなかった文書が実在する
  （[Layers and Stories](Layers%20and%20Stories.md)「レイヤの縮尺」）。効かないまま先へ
  進むと、**後で作る寸法も全部「変える前の縮尺で生まれる」**ので、ログ上は「生まれの
  違う 2 本が違って見える」——**実際には 2 本とも同じ縮尺生まれで、比べ物になっていない**。
  下の ① と ② は、**読み戻しで 1/100 → 1/50 → 1:1 と実際に変わったことを確かめた走行**の値である。

#### ① レイヤの縮尺を変えると、既にある寸法の線の幾何は追随する

規格 `min-nano` の `witExtend`（補助線が寸法線より出る長さ）は **0.0472 page inch =
紙の 1.2mm**。1/100 のレイヤで作った 1 本を、レイヤの縮尺だけ変えて読み直した:

| レイヤの縮尺 | 補助線の出（実測） | 紙の 1.2mm × 縮尺 | `ovDimFontSize`（対照） |
| --- | --- | --- | --- |
| 1/50 | **`60.000`** | 60 | `211.666`（**動かない**） |
| 1:1 | **`1.200`** | 1.2 | `211.666`（**動かない**） |

**ぴたりと一致する。** 同じ 1 本の中で**文字の大きさだけが取り残されている**——
これが「焼き付くのは文字だけ」の一番はっきりした形である。

#### ② 生まれの縮尺が違う 2 本は、同じ縮尺の下では完全に一致する

1/100 で作った寸法と 1/50 で作った寸法を同じレイヤに置き、レイヤを 1:1 にして測った:

| 寸法 | 出 | 補助線の出 | 隙間 |
| --- | --- | --- | --- |
| 1/100 生まれ | `0.000` | **`1.200`** | `0.000` |
| 1/50 生まれ | `0.000` | **`1.200`** | `0.000` |

**1 つの桁も違わない。** 線の幾何に「生まれ」は残っていない。

#### ③ 規格を差し替えると、既にある寸法の線の幾何がその場で変わる

1/50 のレイヤに置いた 1 本に規格を当て直して測った（実測値は**すべて「紙の値 × 50」
そのもの**）:

| 規格 | `witExtend`（紙） | 補助線の出（実測） | `overHang`（紙） | 出（実測） |
| --- | --- | --- | --- | --- |
| `min-nano` | 1.2mm | `60.000` | 0mm | `0.000` |
| `Arch` | 3.175mm | **`158.750`** | 1.5875mm | **`79.375`** |
| `DIN` | 2.0mm | `100.000` | 0mm | `0.000` |
| `ISO` | 2.0mm | `100.000` | 0mm | `0.000` |
| `JIS` | 2.0mm | `100.000` | 0mm | `0.000` |

**規格の値が寸法の中に写し取られていたら、当て直しただけでは変わらないはずである。**
変わったので、**引きに行っているのは規格そのもの**だと分かる。

#### ④ 注釈の中でも同じ——ビューポートの縮尺で解かれる

1/50 のビューポートの注釈へ 2 本（`JIS` の 1/50 生まれと、`min-nano` の 1:1 生まれ）を
入れ、ビューポートの縮尺だけを変えて測った:

| ビューポートの縮尺 | 1/50 生まれ・`JIS`（紙 2.0mm） | 1:1 生まれ・`min-nano`（紙 1.2mm） |
| --- | --- | --- |
| 1/50 | `100.000` | `60.000` |
| 1/100 | `200.000` | `120.000` |

**どちらも「紙の値 × ビューポートの縮尺」ちょうど。** **1:1 で作った寸法も、注釈の中では
ビューポートの縮尺で線が描かれる**——つまり **`ovDimFontSize` を直しさえすれば、
1:1 生まれの寸法と正しい縮尺で作った寸法は見分けが付かない**（下記「注釈へ寸法を
置くときの作り方」は、これで足りると確定した）。

**この ④ は 2 つの文書で同じ数値が出た**（レイヤの縮尺が 1/100 で始まった文書と、
1:1 の新規の空図面。`SetLayerScaleN` が効かなかった後者でも、ビューポートの縮尺の
ほうは効いてこの表のとおりになった）。**注釈の中の解き直しはレイヤの縮尺に依らない。**

**絵でも確かめた。** 同じ注釈に「1/50 生まれ」と「1:1 生まれに `ovDimFontSize` だけ
書いて直したもの」を並べたところ、**値の文字・端記号・補助線のどれも同じ大きさで、
2 本は見分けが付かなかった**。**端記号は外接矩形にも `GetMarkerPolys` にも出ない**
ので、ここだけは絵が唯一の証拠である。

#### 読み戻せる `ov*` は「作ったとき／規格を当てたときの縮尺」で固まる——当てにしない

`ovDimTextAboveLineInCurrUnits`(43) と `ovDimCust*Wit*`(1236-1239) は、**規格の用紙値を
そのとき有効だった縮尺で図面上の長さに直したもの**が入っている。**作ったとき（または
規格を当て直したとき）の値のまま固まり、以後の縮尺の変更には追随しない**——
**描かれる絵のほうは追随するのに、である。**

| セレクタ | 1/100 生まれ | 1/50 生まれ | 1:1 生まれ | 元の規格値（紙） |
| --- | --- | --- | --- | --- |
| `ovDimTextAboveLineInCurrUnits`(43) | `50.000` | `25.000` | `0.500` | `aboveGap` 0.5mm |
| `ovDimCustStartWitLength`(1236) | `1016.000` | `508.000` | `10.160` | `fixedWitLength` 10.16mm |
| `ovDimCustStartWitOffset`(1238) | `100.000` | `50.000` | `1.000` | `witGap` 1.0mm |

- **縮尺を変えても動かない。** 1/100 生まれの 1 本をレイヤ 1/50 → 1:1 と動かしても
  `50 / 1016 / 100` のままだった（**絵は追随している**のに）。注釈でも同じで、
  ビューポートを 1/50 → 1/100 にしても `50` のまま（絵は `100 → 200` と動いた）。
- **規格を当て直すと、そのときの縮尺で計算し直される**（`Arch` を当てると
  `ovDimTextAboveLineInCurrUnits` が `79.375` ＝ 1.5875mm × 50 になった）。
- **つまり `ovDimTextSizeInPoints` と同じ罠である**（上記）。**これらを読んで「いま
  どう描かれているか」を判断してはいけない。** 判断は**規格の用紙値**（`dimStd*`）と
  **容れ物の縮尺**から自分で計算する。

#### per-object の上書きは効く——単位は「図面上の長さ」

補助線だけを規格から外したいときは `ovDimWitnessOverride`(1235) を立てる。**実測**:

| やったこと | 結果 |
| --- | --- |
| `ovDimWitnessOverride` ← `1`（`Sint16` で書く） | `true` / 読み戻し `1` |
| `ovDimCustStartWitLength` ← `2000` | `true` / **補助線が図面上 2000 の長さで描かれた**（外接矩形の下端が寸法線から 2000 下がった） |

- **`ovDimCust*` の単位は用紙インチではなく図面上の長さ**（`in current units` の名前どおり）。
  紙で `W` mm にしたいなら `W × 容れ物の縮尺` を書く。
- **`ovDimWitnessOverride` は `Sint16` で書ける**（ヘッダは `short` と書いている）。
- **ふつうは要らない。** 規格どおりでよいなら何も書かなくてよい——上の ①〜④ のとおり、
  縮尺は VW が面倒を見る。

#### 端記号の大きさは規格が `Sint16` で持つ（1/16384 インチ）

`dimStdLinearASize`(22) はヘッダに単位の記載が無い `Sint16` だが、**マーカーの大きさと
同じ 1/16384 インチの 16 ビット整数**として読むと辻褄が合う（[Attributes and Classes](Attributes%20and%20Classes.md)）:

| 規格 | `linearASize`(22) | 1/16384 インチとして | 紙の大きさ |
| --- | --- | --- | --- |
| `min-nano` | `774` | 0.0472 インチ | 1.200mm |
| `Arch` / `DIN` / `ISO` / `JIS` | `2048` | 0.1250 インチ | 3.175mm |

`min-nano` の `774` は同じ規格の `witExtend`（0.0472 page inch）と**同じ値**で、
**用紙インチ系の長さと同じ土俵にある**ことが分かる。`linearAWidth`(36) はどの規格でも
`0`（「zero value also allowed」とヘッダにある）。

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

### 規格に「文字の大きさ」は無い——持っているのは文字スタイルへの参照 1 本で、それは寸法に届かない

**確定（VW 2026 / macOS。[#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)
で実機のプローブを走らせた）。** 結論から書くと、`SetCustomDimensionStandardVariable` で
規格の文字スタイルは**確かに書ける**が、**書いても寸法の文字の大きさは 1 mm も動かない**
——**規格の側から文字の大きさを決める道は無い。**

> **「できない」の側の結論なので 2 回走らせた。** 別のビルドで走らせた 2 回のログは
> **1 行も違わなかった**（下に挙げる値はすべて両方で同じ）。**見落としや 1 回きりの
> まぐれではない。**

#### ① セレクタに「文字の大きさ」が無い（【ヘッダ根拠】）

`dimStd*` セレクタは **1〜52 で全部**（`Kernel/API/MiniCadCallBacks.h:1809-1860`）。
用紙インチの長さ系は補助線・寸法線・端記号ばかりで、**文字に関わるのは次の 3 つだけ**:

| セレクタ | 番号 | 型 | ヘッダの説明 |
| --- | --- | --- | --- |
| `dimStdTextStyle` | 51 | `Sint32` | *the ref number of the text style that the dimension standard is linked to* |
| `dimStdTextPos` | 52 | `UInt8` | 文字の位置（0: above/left, 1: above/right, 2: outside） |
| `dimStdTolSizeFac` | 26 | `UInt8` | **公差**文字の大きさ（寸法文字に対する％） |

**つまり規格が「紙で決めた大きさ」を持つとしたら、それは結び付いた文字スタイルの
`ovTextStyleSize`（インチ）しかない。** `dimStdTolSizeFac` は公差用の比率なので、
寸法値そのものの大きさには使えない。

#### ② 規格の文字スタイルは書ける——カスタムだけ、組み込みは弾かれる

**実測。文字スタイルを 2 つ（`6pt` ＝ `0.0833333` インチ／`300pt` ＝ `4.16667` インチ）
作ってから、`SetCustomDimensionStandardVariable(index, dimStdTextStyle, ref)` を叩いた:**

| 相手 | index | 戻り値 | 読み戻した `dimStdTextStyle` |
| --- | --- | --- | --- |
| カスタム規格（作ったばかりのもの） | `-1` | **`true`** | `113`（狙った 6pt の文字スタイル） |
| カスタム規格（作ったばかりのもの） | `-2` | **`true`** | `114`（狙った 300pt の文字スタイル） |
| **組み込み規格 `JIS`** | `6` | **`false`** | `0`（変わらず＝文字スタイル無し） |

- **ヘッダの "cannot be changed with this function" は実機どおり。** 組み込み（1〜9）は
  戻り値 `false` で弾かれ、値も動かない。**書けたかは戻り値で判る。**
- **結び付ける先の文字スタイルは `CreateTextStyleResource` で作って
  `ovTextStyleSize`（インチ）を書けばよい**（下記「[`ovTextStyleSize` の単位はインチ](#ovtextstylesize-の単位はインチpt-ではない)」）。

#### ③ ところが、その規格を当てた寸法は 1 mm も動かない（これが答え）

**狙いは「1/50 のビューポートの注釈で紙の上 6pt」。** #155 の式
（「規格が紙で決めた大きさ × 繋ぐときのアクティブレイヤの縮尺」）を信じるなら、
**規格の紙の大きさを `300pt`（＝ 6pt × 50）にしておけば、1:1 がアクティブなままでも
`6pt × 25.4/72 × 50 = 105.833mm` が焼き付く**はずである。**ならなかった。**

**すべて 1:1 のシートレイヤをアクティブにしたまま測った**（規格 `300pt` の下でも
`6pt` の下でも、焼き付いた値は**終始 `2.11667mm`** ＝ 1/50 の紙で 0.12pt ＝ 見えない）:

| # | やったこと | 寸法の `ovDimStandardName` | 寸法の `GetTextStyleRef` | 繋いだ後の中の `ovDimFontSize` |
| --- | --- | --- | --- | --- |
| 5a | 対照: `6pt` の規格を**文書の既定**にしてから作る | `規格6pt` | `30` | `2.11667` |
| **5b** | **`300pt` の規格を文書の既定にしてから作る** | `規格300pt` | **`30`** | **`2.11667`** |
| 6 | `6pt` の規格で作った寸法へ、後から `ovDimStandardName` で `300pt` の規格を当てる | `規格300pt` | **`30`** | **`2.11667`** |

- **規格そのものは確かに当たっている**（`ovDimStandardName` は書いたとおりに読み戻せる）。
  **効いていないのは文字の大きさだけ**である。
- **決定的なのは `GetTextStyleRef` が `30` のまま動かないこと。** `30` はこの図面の
  **元からの**カスタム規格 `min-nano` が指す `寸法(6pt)`——**新しく当てた規格が指す
  `113` / `114` ではない。** 規格を差し替えても、**寸法に当たっている文字スタイルは
  付いて来ない。**
- これは #157 の「[作った直後は〈クラスの文字スタイル〉——`GetTextStyleRef` はクラスを
  見ていない](#作った直後はクラスの文字スタイルgettextstyleref-はクラスを見ていない)」で
  測った 4 点目（**規格を替えても寸法の文字スタイルは外れない**）と同じ現象で、
  **今回は「規格を替える」だけでなく「規格を文書の既定にしてから作る」道でも同じ**
  だと分かった。**作る前に既定を差し替えても駄目である。**
- **描かれている文字図形（型 `10`）の大きさも終始 `2.11667mm`** で、読み戻した値と
  絵は食い違っていない。**目視は要らない**（#155 のような「値は動いたが絵は古い」は
  起きていない——値そのものが動かないため）。

#### ④ 既にある連続寸法も追随しない——3 つの道すべて

**`6pt` の規格で作って繋いだ連続寸法（中の `ovDimFontSize` ＝ `2.11667`）**に対して、
規格側を動かしてから `ResetObject` した。**どれも `2.11667` のまま**だった:

| 試したこと | 規格側の読み戻し | `ResetObject` の**前** | `ResetObject` の**後** |
| --- | --- | --- | --- |
| a. 規格が指す**文字スタイルの大きさ**を `6pt` → `300pt` に変える | `300pt` に変わった | `2.11667` | **`2.11667`** |
| b. 規格を**別の文字スタイル**（`300pt`）へ結び直す | `114` に変わった | `2.11667` | **`2.11667`** |
| c. **中の直線寸法**へ `ovDimStandardName` で `300pt` の規格を当てる | 書き込みは `true` | `2.11667` | **`2.11667`** |

- **a と b で規格の側は確かに変わっている**（読み戻しで確認済み）。**それでも中は動かない。**
  つまり**連続寸法は、作り直しのときに規格を引きに行っていない。**
- **c では、中の直線寸法へ書いた `ovDimStandardName` そのものが `ResetObject` で
  捨てられた**——書いた直後は `規格300pt` と読めるのに、`ResetObject` の後は
  **`規格6pt` に戻っていた**。#155 の「中へ書いた `ovDimFontSize` は `ResetObject` で
  捨てられる」と同じ筋で、**捨てられるのは大きさだけではない。**
- **絵（中に描かれている文字図形）も終始 `2.11667mm`。**

#### ⑤ では「繋ぐときに焼き付く値」の左の因子は何か——規格ではなく文字スタイル

**「規格が紙で決めた大きさ」という言い方は不正確だった。** 焼き付く値は

> **`ovDimFontSize` ＝ その寸法に当たっている文字スタイルの `ovTextStyleSize`（インチ）
> × 25.4 × 繋ぐときのアクティブレイヤの縮尺**

で、**規格はその文字スタイルの出どころの候補でしかなく、SDK から差し替えても寸法へは
伝わらない**（③）。左の因子は #157 が実測で押さえた「[文字スタイルを当てる 2 つの口は、
`ovDimFontSize` の扱いが違う](#文字スタイルを当てる-2-つの口はovdimfontsize-の扱いが違う)」の
`SetTextStyleRef` の式そのままである。

#### ⑥ 逃げ道は 2 つある——どちらも規格ではない

**#163 が探していたのは「アクティブレイヤを動かさずに、連続寸法の紙の大きさを決める
道」だった。それは規格の側には無いが、別の 2 つが既に実測で確かめられている。**
どちらも `CreateChainDimension` を**1:1 がアクティブなまま**呼んでよく、作業用レイヤを
増やす必要は無い:

| 道 | やること | 実測の出どころ |
| --- | --- | --- |
| **(a)** | **繋ぐ直前**に、繋ぐ 2 本へ `ovDimFontSize` ＝ 紙の pt × 25.4/72 × ビューポートの縮尺 を書く | #161（「[#143 の手順は正しかった](#143-の-ovdimfontsize-を書く手順は正しかった抜けていたのは-resetobject)」。繋いだ後の中の直線寸法も**絵も**書いた値どおりだった） |
| **(b)** | **繋ぐ前**に、`SetTextStyleRef(dim, ref)` で文字スタイルを明示する（`ovTextStyleSize` ＝ 狙いの紙 pt ÷ 72 × ビューポートの縮尺） | #157（「[連続寸法へ繋いでも残る](#連続寸法へ繋いでも残る)」と「[連続寸法は中の `ovDimFontSize` で描かれる](#連続寸法は中の-ovdimfontsize-で描かれる直線寸法と食い違う)」の 3 巡目） |

- **注釈へ置くなら (b)。** 注釈で値（数字）が出るかどうかは別の話で、#157 の
  手順に従う——「[注釈へ寸法を置くときの作り方（文字スタイル版）](#注釈へ寸法を置くときの作り方文字スタイル版157-以降はこちら)」。
- **(b) は推論ではなく、#157 がその条件そのままで測っている**——1:1 のシートレイヤが
  アクティブなまま `ovTextStyleSize` ＝ `10.4167` インチ（＝ 6/72 × **125**）の文字
  スタイルを当てて作り、**1/125 のビューポートの注釈で紙の 6pt として読めた**（行 F2）。
  同じ文字スタイルだけを当てた 2 本を繋いだ連続寸法も、**直線寸法と同じ
  `264.5833mm`（＝ `10.4167 × 25.4 × 1`）の絵**で出ている（3 巡目）。
- **「アクティブレイヤを合わせてから繋ぐ」（#155）が要るのは、(a) も (b) もせずに
  by-class のまま繋ぐときだけ**である。by-class では左の因子がこちらから決められない
  （③ のとおり規格でも動かせない）ので、**縮尺のほうを合わせるしか残らない。**
- **#163 が規格でやろうとしたことは、(a) と (b) では既に通っている**——だから
  「規格でも決められない」と分かっても行き止まりにはならない。

- **`ovDimFontSize` は触らない。** #155 のとおり連続寸法では捨てられるし、触ると直線寸法と
  連続寸法で食い違う（#157）。**決めるのは文字スタイルの大きさだけ。**
- **「アクティブレイヤを合わせてから繋ぐ」は、文字スタイルを明示しない（by-class の）
  ときの道である。** by-class のままだと左の因子が「図面が持っていた文字スタイル」に
  なってしまい、こちらから決められないので、**縮尺のほうを合わせるしか残らない。**

#### 付け足し: `CreateCustomDimensionStandard` の実測（この調査で分かったこと）

| 測ったこと | 結果 |
| --- | --- |
| 作った規格が入る index | **`0` が埋まっていれば `-1`、次は `-2`**（0 から下へ詰まる） |
| `NumberCustomDimensionStandards()` | `1` → `2` → `3` と 1 つずつ増える |
| 作った直後の `dimStdTextStyle` | **アクティブな規格のものを引き継ぐ**（この図面では `min-nano` の `30`） |
| 戻り値 | 規格を表す `MCObjectHandle`（非 nil） |

- **ヘッダの「アクティブな規格の値を既定として作る」は文字スタイルにも及ぶ。**
  作った直後の規格は**元の規格と同じ文字スタイルを指している**ので、自分の大きさで
  使いたいなら `dimStdTextStyle` を書き替える（②）。
- **`0` が空いているとは限らない。** #155 の時点ではカスタムが 1 件で index `0` だったが、
  そこへ 2 つ足したら `-1` `-2` に入った。**index は総当りで引く**
  （上記「[一覧の取り方](#一覧の取り方)」）。

## 寸法の文字スタイル

**文字の大きさ（`ovDimFontSize`）とは別に、寸法は「文字スタイル」という名前付きリソース
への参照を 1 つ持つ。** OIP の「文字 → スタイル」がそれで、既定は
**〈クラスの文字スタイル〉**。[#157](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/157)
で実機（VW 2026 / macOS）から測った。

### まず結論

- **寸法規格は文字スタイルを持つ。** `GetDimensionStandardVariable(index,
  **`dimStdTextStyle`（= 51）**, block)` で読む。返るのは **`t_Sint32`（型番号 7）の
  ref number**——名前ではない（名前は `InternalIndexToNameN` で引く）。
- **組み込み規格（1〜9）はどれも `0`（文字スタイル無し）。持つのはカスタム規格だけ。**
- **`ovTextStyleSize`(1361) の単位は「インチ」**（ヘッダは "the size of the text" としか
  書いていない）。pt で指定したいなら **`pt / 72`** を書く。
- **作った直後の寸法は〈クラスの文字スタイル〉**（`GetTextStyleByClass` = `true`、
  `ovDimTextStyle` = **`-2`**）。**`-2` は「クラス由来」を表す番兵で、ref number ではない。**
- **その状態でも `GetTextStyleRef` は番号を返す。ただしその番号はクラスにも、いま
  当たっている寸法規格にも追随しない**（一致したのは**作った時点**の規格の文字スタイル）。
  **文字スタイルの出どころを知る用途に使ってはいけない。**
- **注釈で値を出したいなら `ISDK::SetTextStyleRef(dim, ref)` を呼ぶ。これだけが効く。**
  〈クラスの文字スタイル〉のままでは `ovDimFontSize` を正しく書いても**値が出ない**し、
  **`ovDimTextStyle`(1248) へ番号を書く道も絵には効かない**——しかも
  **`SetTextStyleRef` を呼んだときと読み戻しが完全に同じになる**ので、**読んで確かめる
  ことができない**（下記「[注釈で値が出るのは `SetTextStyleRef` で明示したときだけ](#注釈で値が出るのは-settextstyleref-で明示したときだけ-誤り原因は上記の引き直し)」）。
- **文字スタイルを当てる 2 つの口は `ovDimFontSize` の扱いも違う**——
  **`SetTextStyleRef` は `ovDimFontSize` を書き換える**が、
  **`ovDimTextStyle` へ番号を書く道は動かさない。**
- **大きさを決めるのは文字スタイルであって `ovDimFontSize` ではない。**
  狙いの大きさは文字スタイルへ書く——**`ovTextStyleSize`（インチ）＝ 紙の pt ÷ 72 ×
  ビューポートの縮尺**（実測で確定）。**`ovDimFontSize` は触らない**——触ると
  直線寸法（文字スタイルの大きさで描く）と連続寸法（中の `ovDimFontSize` で描く）で
  **大きさが食い違う**のに、読み戻しでは気付けない。
- **図面にある `寸法(6pt)` のような文字スタイルを、そのまま当ててはいけない。**
  絵の文字は「`ovTextStyleSize`（インチ）× 25.4 × **そのときのアクティブレイヤの縮尺**」に
  なるので、1:1 のシートレイヤがアクティブなまま当てると 1/125 の紙で **0.048pt** に潰れる
  （**値は描かれている。小さすぎて見えないだけ**）。伏図（1/50）で読めているのは
  1/50 がアクティブなうちに作っているからにすぎない。**紙の pt を出す道は 3 つあり、
  規格の文字スタイルを使い続けるなら「当てた後に `ovDimFontSize` を書いて引き直す」**
  ——下記「[図面にある「紙の pt で名付けられた文字スタイル」をそのまま当てると縮尺で焼かれる](#図面にある紙の-pt-で名付けられた文字スタイルをそのまま当てると縮尺で焼かれる確定)」
  （[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)）。
- **`SetTextStyleRef` が焼く縮尺は容れ物のもの**——**注釈の中の寸法へ掛けると、
  アクティブレイヤが 1:1 でもそのビューポートの縮尺で焼かれる。** だから
  **既に小さく置いてしまった連続寸法は、型 86 へ `SetTextStyleRef` を掛け直して
  `ResetObject` を呼べば後から直せる**（これが OIP で選び直すと直る仕組み。
  **中の型 63 へ掛ける道と、後から `ovDimFontSize` を書く道は引き直しで捨てられる**）。
  下記「[既に注釈へ置いてしまった連続寸法を後から直す](#既に注釈へ置いてしまった連続寸法を後から直す通る道は-1-つだけ確定)」。
- **連続寸法へ繋いでも残る。** 繋ぐ前に直線寸法へ当てておけば、中の型 63 は
  `ovDimTextStyle` も `ovDimFontSize` も保つ。
- **注釈へ移しても何も変わらない**（`AddViewportAnnotationObject` は文字スタイルにも
  `ovDimFontSize` にも触らない）。

### 触る口の一覧

**寸法 1 本の文字スタイル**——`ISDK` の口とオブジェクト変数の 2 通りがある。

| 何 | 口 | 型・値 |
| --- | --- | --- |
| 読む | `ISDK::GetTextStyleRef(h)` | `InternalIndex`（文字スタイルの ref number） |
| 書く | `ISDK::SetTextStyleRef(h, styleRef)` | `void`。**`ovDimFontSize` も書き換わる**（下記） |
| 〈クラスの文字スタイル〉か | `ISDK::GetTextStyleByClass(h)` | `bool` |
| 〈クラスの文字スタイル〉へ戻す | `ISDK::SetTextStyleByClass(h)` | `void`。**`ovDimFontSize` も戻る** |
| 同じものをオブジェクト変数で | `ovDimTextStyle`(**1248**) | `Sint32`。**`-2` ＝ クラス由来**。**書いても `ovDimFontSize` は動かない** |

- **`ovObjectTextStyle` は `ovDimTextStyle` と同じ番号**（`= ovDimTextStyle`）だが、
  ヘッダに `char - Not for public use` と付いている。**使うなら `ovDimTextStyle` のほう。**
- 文字**列**（1 字ごと）用の `…RefN` 系（`SetTextStyleRefN` / `GetTextStyleByClassN`）は
  文字ブロック用で、寸法には要らない。

**クラスが持つ文字スタイル**（by-class 属性 7 つとは別口。[Attributes and Classes](Attributes%20and%20Classes.md)）:

| 何 | 口 |
| --- | --- |
| クラスが文字スタイルを持つか | `ISDK::GetClUseTextStyle(classId)` / `SetClUseTextStyle(classId, use)` |
| その文字スタイル | `ISDK::GetClTextStyleRef(classId)` / `SetClTextStyleRef(classId, ref)` |

**文字スタイルそのもの**は `ISDK::CreateTextStyleResource(name)` で作り、
`ovTextStyleSize`(1361。`double`。**インチ**)・`ovTextStyleFontIndex`(1360) などの
オブジェクト変数で触る。

### 寸法規格が持つ文字スタイルを読む

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

- **組み込み規格は文字スタイルを持たない。** 文字スタイルを持つのは
  **カスタム規格だけ**で、この図面ではカスタムは index `0` の 1 件しかない
  （−1 以下は「無い」と返る。上記「[index の体系](#index-の体系)」の裏取りにもなっている）。
- 書くほうは**カスタム規格だけ**——`SetCustomDimensionStandardVariable(index, 51, block)`
  （組み込み規格 1〜9 を書き換える口は無い）。

### `ovTextStyleSize` の単位はインチ（pt ではない）

`CreateTextStyleResource` で作った直後の `ovTextStyleSize` は **`0.1667`**
（= 1/6 インチ = **12pt**）。ここへ `6` を書くと **6 インチ = 432pt** になり、
その文字スタイルを当てた寸法の `ovDimTextSizeInPoints` は実際に **`432`** を返した。

> **pt で指定したいなら `ovTextStyleSize` へ `pt / 72` を書く。** 6pt なら `0.08333`。

### 作った直後は〈クラスの文字スタイル〉——`GetTextStyleRef` はクラスを見ていない

作った直後の寸法（型 63）は:

| 読むもの | 値 |
| --- | --- |
| `GetTextStyleByClass(dim)` | **`true`** |
| `ovDimTextStyle`(1248) | **`-2`**（＝「クラス由来」の番兵。ref number ではない） |
| `GetTextStyleRef(dim)` | **`30` ＝ `寸法(6pt)`** |

**`30` は、寸法を作った時点で当たっていた寸法規格（`min-nano`）の `dimStdTextStyle`
そのものである。** ただし**「いまの規格を引いている」のではない**——次の 4 つを測った:

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

> **つまり by-class の寸法について `GetTextStyleRef` が返す番号は、クラスにも、
> いま当たっている規格にも追随しない。** 一致したのは**作った時点の規格の文字
> スタイル**だけである。**文字スタイルの出どころを知るためにこの口を読んではいけない**
> ——クラスを見に行っても（`GetClTextStyleRef`）、規格を見に行っても
> （`dimStdTextStyle`）、この値とは合わないことがある。

### 文字スタイルを当てる 2 つの口は、`ovDimFontSize` の扱いが違う

**同じ 1 本（1:1 のシートレイヤがアクティブなときに作った寸法）で往復した実測:**

| やったこと | `GetTextStyleByClass` | `ovDimTextStyle` | **`ovDimFontSize`** |
| --- | --- | --- | --- |
| 作った直後 | `true` | `-2` | `2.1167` |
| `SetTextStyleRef(dim, 113)` | `false` | `113` | **`152.4000` へ書き換わる** |
| `SetTextStyleByClass(dim)` で戻す | `true` | `-2` | **`2.1167` へ戻る** |
| `SetObjectVariable(dim, ovDimTextStyle, 113)` | `false` | `113` | **`2.1167` のまま（動かない）** |

- **`SetTextStyleRef` は、当てた文字スタイルの大きさを `ovDimFontSize` へ焼き直す。**
  入る値は

  > **`ovDimFontSize` ＝ `ovTextStyleSize`（インチ）× 25.4 ×（縮尺）**

  **その「縮尺」は容れ物の縮尺である**——**ビューポートの注釈の中にある寸法へ
  `SetTextStyleRef` を呼ぶと、アクティブレイヤが 1:1 でも、そのビューポートの縮尺で
  焼かれる**（[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)
  で実測。1:1 のシートレイヤがアクティブなまま 1/125 の注釈の中の連続寸法へ掛け直したら
  `0.0833 × 25.4 × 125` ＝ `264.5833` が入った）。**作る時点の寸法はアクティブレイヤに
  入るので、ふつうは「アクティブレイヤの縮尺」と一致していて区別が付かない**
  ——分離できたのは注釈の中へ掛け直したこの 1 例だけで、そこでは**容れ物が勝った**。
  以下の式の検算はどれも両者が一致している場合である。

  で説明が付く（6 インチ × 25.4 × 1 = `152.4`。シートレイヤがアクティブなので縮尺 1）。
  **同じ式が「作った直後」にも当てはまる**——`寸法(6pt)` は 6/72 インチなので、
  1:1 では `6/72 × 25.4 × 1 = 2.1167`、1/50 のデザインレイヤでは `× 50` して
  `105.8333`。**上記「[文字の大きさは作るときのアクティブレイヤの縮尺で焼き付く](#文字の大きさは作るときのアクティブレイヤの縮尺で焼き付く)」
  の「焼き付く素」は、当たっている文字スタイルの大きさだった。**
- **`ovDimTextStyle`(1248) へ番号を書く道は `ovDimFontSize` を動かさない。**
  結び付けだけを変えたいならこちら。
- **どちらの道でも、後から `ovDimFontSize` を書けばその値が残る**（`264.5833` を書いて
  そのまま読み戻せた）。**順番は「文字スタイル → `ovDimFontSize`」にする**
  ——逆にすると `SetTextStyleRef` が書いた値を潰す。

### ★ 絵の文字の大きさは、書いた瞬間には変わらない——引き直しで反映される（確定）

> **この節は [#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
> の結論で、以下の 2 節（#157 の観察）はこの法則の特別な場合である。** 先にここを読む。

**寸法が絵に使う文字の大きさは、`ovDimFontSize` へ書いた時点では変わらない。**
書いた値は保持され、**その寸法が次に「引き直された」ときに初めて絵へ反映される。**

| 書いたあとに通したもの | 絵に反映されるか |
| --- | --- |
| 何も通さない | **されない**（書く前の大きさのまま） |
| **`ResetObject(dim)`** | **される**（これが最短の 1 手） |
| `CreateChainDimension` で繋ぐ | される（繋ぎ直しが引き直しになる） |
| ビューポートの縮尺を変える | される（注釈の中身が引き直される） |
| `AddViewportAnnotationObject` / `UpdateViewport` | **されない**——移動も更新も引き直しではない |

**`SetTextStyleRef` だけは例外で、呼んだその場で絵の側も書き換える**（だから引き直し
無しでも効く。下記 2 節で「文字スタイルを明示したときだけ出た」と見えたのはこれが理由）。

実測（VW 2026 / macOS。1/50 の平面ビューポートの注釈へ 7 行。**測る長さを行ごとに
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

- **`ovDimFontSize` は効く。** H4 が H1 のちょうど 2 倍で出たので、**書いた値どおりの
  大きさになる**（「たまたま合った」ではない）。
- **文字スタイルを外す必要は無い。** H2 は〈クラスの文字スタイル〉のままで効いている。
- **書いた値は捨てられていない。** H3 / H6 は `ResetObject` を呼ばなかったので絵は
  変わらなかったが、**後からビューポートの縮尺を往復させたら紙 6pt になった**
  ——値は寸法の中にずっと残っていて、引き直しを待っていただけである。
- **読み戻しでは絶対に気付けない。** H1 と H6 は `GetTextStyleByClass` /
  `GetTextStyleRef` / `ovDimTextStyle` / `ovDimFontSize` の **4 つとも完全に同じ**なのに、
  絵は片方しか出ない。違うのは「`ResetObject` を通したかどうか」だけ。
  → **読み戻しの代わりに「実際に描かれている文字の大きさ」を測る**
  （[調査の作法「絵の文字の大きさを目視に頼らず測る」](Investigation%20Techniques.md)）。

#### 寸法から文字スタイルを外す口（`SetTextStyleRef(dim, 0)`）

ついでに確定した。ヘッダ（`SDKLib/Include/vs.py`）が言うとおりだった:

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

- **ただし外しても絵は変わらない**（H3 / H6）。**外すこと自体に効き目は無い**ので、
  大きさを直したいだけなら**外さずに `ovDimFontSize` を書いて `ResetObject`** でよい。
- `GetTextStyleRef` が by-class の寸法に対して返すのが**実効値**だという裏取りでもある
  ——クラスから文字スタイルを取り去っても実効は `30` のままだった（下記
  「作った直後は〈クラスの文字スタイル〉」の観察と一致する）。

### 注釈で値が出るのは `SetTextStyleRef` で明示したときだけ（← 誤り。原因は上記の引き直し）

> **この節の表（D1〜D6）の実測は正しいが、そこから引いた法則が誤っていた。**
> [#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
> で原因が分かった——**どの行も `ResetObject` を呼んでいなかった**ので、`ovDimFontSize`
> を書いた行は絵が書く前のままだっただけである（上記「★ 絵の文字の大きさは…」）。
> **「`SetTextStyleRef` で明示しないと値が出ない」は成り立たない**——`ovDimFontSize` を
> 書いて `ResetObject` すれば、by-class のままでも出る。**表はそのまま残す**（どの作り方
> がどう見えたかの記録として正しいので）。

**読み戻した値では「値が出るか」を判定できない。** 1/125 の平面ビューポートの注釈へ、
作り方だけを変えた寸法を 6 行ならべて絵を見た（VW 2026 / macOS）。**測る長さを行ごとに
変えてあるので、出た数字がそのままどの行かを表す。**

| 行 | やったこと | `GetTextStyleByClass` | `ovDimTextStyle` | `ovDimFontSize` | **絵に値が出たか** |
| --- | --- | --- | --- | --- | --- |
| D1 | 〈クラスの文字スタイル〉のまま `ovDimFontSize` を書く | `true` | `-2` | `264.5833` | **出ない** |
| D2 | **`SetTextStyleRef`** → `ovDimFontSize` を書く | `false` | `113` | `264.5833` | **出る** |
| D3 | **`SetTextStyleRef`** だけ | `false` | `113` | `152.4000` | **出る** |
| D4 | **`ovDimTextStyle` へ番号を書く** → `ovDimFontSize` を書く | `false` | `113` | `264.5833` | **出ない** |
| D5 | 何もしない（対照） | `true` | `-2` | `2.1167` | 出ない |
| D6 | 繋ぐ前に両方へ `SetTextStyleRef` ＋ `ovDimFontSize` → `CreateChainDimension` | `false` | `113` | `264.5833` | **出る** |

> **`D2` と `D4` は読み戻しが 4 つとも完全に同じなのに、絵は片方しか出ない。**
> つまり **`SetTextStyleRef` は、`ovDimTextStyle` へ番号を書くのとは違うことをしている**
> ——そしてその違いは **`ISDK` から読めない**。

**その「違うこと」が #161 で分かった: `SetTextStyleRef` は絵に使う文字の大きさを
その場で書き換え、`ovDimTextStyle` への書き込みは書き換えない。** D4 は番号だけが
変わって大きさが元のまま（`2.1167`mm ＝紙 0.017mm）だったので、小さすぎて読めなかった。

- **【利用側への答え】** 注釈へ置く寸法には `gSDK->SetTextStyleRef(dim, ref)` を呼ぶか、
  **`ovDimFontSize` を書いて `gSDK->ResetObject(dim)` を呼ぶ**。どちらでも出る。
  `ovDimTextStyle`(1248) へ番号を書くだけの道は**使ってはいけない**（D4。大きさが
  付いてこない）。
- ~~**`ovDimFontSize` を正しく書いたかどうかは関係なかった。**~~ **← 誤り。**
  D1 が `264.5833` を持っているのに出なかったのは、**`ResetObject` を呼んでおらず
  絵が書き換わっていなかった**ため。実際に描かれていた文字は `2.1167`mm
  （1/125 の紙で 0.048pt）で、**まさに「小さすぎて見えない」だった**
  ——[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
  の「1:1 生まれだから小さい」と**同じ原因**である（#161 で実測）。
- **当てる文字スタイルは何でもよい。** D2・D3・D4 が使ったのはプローブが作った
  無名の文字スタイルで、寸法規格の `寸法(6pt)` ではない。**「明示されている」こと自体が
  効いている。**
- **連続寸法でも同じ**（D6）。繋ぐ前に中身の直線寸法へ `SetTextStyleRef` を呼んでおけば、
  繋いだ後も注釈へ移した後も値が出る。

**〈クラスの文字スタイル〉そのものが壊れているわけではない。** 同じビューポートには
**デザインレイヤ（1/50）に置いた by-class の寸法**も写っており、そちらは**値が出ていた**
（`ovDimFontSize = 105.8333` → 1/125 の紙で 0.85mm ≒ 2.4pt の小さな数字として読めた）。
**出なくなるのは、注釈へ置いた by-class の寸法だけ**である。

### 文字スタイルを当てた後に `ovDimFontSize` を書いても、その場では絵が変わらない

> **見出しはもと「大きさを決めるのは文字スタイルであって `ovDimFontSize` ではない」
> だった。誤りである**（[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)）。
> 下の実測（E1〜E3 が同じ大きさ）は正しいが、**3 本とも `ResetObject` を呼んでいない**
> ——だから絵が `SetTextStyleRef` の書いた大きさのままだっただけで、`ovDimFontSize` が
> 無力なのではない。**`ResetObject` を通せば書いた値のとおりになる**（上記「★ 絵の
> 文字の大きさは、書いた瞬間には変わらない」の H4 が、H1 のちょうど 2 倍で出た）。

**`SetTextStyleRef` を呼んだ後に `ovDimFontSize` をいくら書いても、（引き直すまでは）
描かれる文字の大きさは変わらない。** 同じ文字スタイル（6 インチ）を当てた直線寸法を 3 本、
`ovDimFontSize` だけ変えて 1/125 の注釈へならべた:

| 行 | `ovDimFontSize` | 紙の上の見込み | **絵の大きさ** |
| --- | --- | --- | --- |
| E1 | `152.4000`（`SetTextStyleRef` が書いた値のまま） | 1.22mm ≒ 3.5pt | 基準 |
| E2 | `264.5833`（紙 6pt のつもり） | 2.12mm ＝ 6pt | **E1 と同じ** |
| E3 | `529.1667`（紙 12pt のつもり） | 4.23mm ＝ 12pt | **E1 と同じ** |

**3.47 倍まで違う値を持たせても、絵は 3 本ともまったく同じ大きさだった。**

- **`ovDimFontSize` は書けて読み戻せるのに、絵には効いていない**——ただしそれは
  **引き直していないから**であって、`ovDimFontSize` が無視されているからではない
  （#161）。書いた値は残っていて、`ResetObject` を呼べばその大きさで描き直される。
  **読み戻しで確かめられない**のは D2/D4（上記）と同じ筋の罠である。
- **注釈へ移しても値は 1 つも動かない**（移す前後で `ovDimFontSize` が同じ）。
  つまり大きさの違いは「移動で書き換わった」せいではない。

### 紙で狙った大きさを出す——文字スタイルの大きさの決め方（確定）

大きさは文字スタイルが握っているので、**狙いの pt は文字スタイルの大きさへ書く。**

> **`ovTextStyleSize`（インチ）＝ 紙の pt ÷ 72 × ビューポートの縮尺**

**0 倍・1 倍・2 倍の 3 本を 1/125 の注釈へならべて確かめた**（`ovDimFontSize` は
いっさい触らず、文字スタイルの大きさだけを変えてある）:

| 行 | `ovTextStyleSize` | 狙い | **絵** |
| --- | --- | --- | --- |
| F1 | `0.0833`（＝ 6/72。素直に「6pt」と書いたつもりの値） | — | **見えない**（紙で 6/125 ＝ 0.048pt） |
| F2 | `10.4167`（＝ 6/72 × 125） | 紙 6pt | **読める** |
| F3 | `20.8333`（＝ 12/72 × 125） | 紙 12pt | **F2 のちょうど 2 倍** |

- **「6pt の文字スタイル」をそのまま当てても紙で 6pt にはならない。**
  文字スタイルの大きさは**図面上の長さ**として扱われ、ビューポートの縮尺で割られて
  紙に出る——つまり
  [#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
  の「焼き付き」とまったく同じ扱いである。
- **数としては #143 の式そのまま。** #143 が `ovDimFontSize` へ書けと言っている
  `紙pt × 25.4/72 × 縮尺`（mm）を、インチに直して（÷ 25.4）文字スタイルへ書くだけ:
  `紙pt / 72 × 縮尺`。**変わったのは「どこへ書くか」だけ。**
- 図面に既にある文字スタイル（規格が持つ `寸法(6pt)` など）は**紙の pt で名付けられて
  いても、その縮尺でしか正しくない**。ビューポートの縮尺ごとに別の文字スタイルが要る。

### 図面にある「紙の pt で名付けられた文字スタイル」をそのまま当てると縮尺で焼かれる（確定）

**寸法規格 `min-nano` が持つ `寸法(6pt)`（`ovTextStyleSize` = 6/72 = `0.0833` インチ）を
`SetTextStyleRef` でそのまま当てると、絵の文字は「`0.0833` × 25.4 × **そのときのアクティブ
レイヤの縮尺**」になる。** つまり**当てた文字スタイルの名前は紙の pt でも、絵が紙の pt に
なるのは 1 つの縮尺のときだけ**である（上記 F 表の言い直し。
[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164) で
10 行を測って確定した）。

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

- **これが「軸組図（1/125）では値が読めないのに伏図（1/50）では読める」の正体である。**
  伏図は **1/50 のデザインレイヤがアクティブなうちに作っている**ので焼き付く値が
  `6/72 × 25.4 × 50` ＝ `105.833`mm ＝ 1/50 の紙でちょうど 6pt になる。断面の段取りは
  シートレイヤ（1:1）を先に作るため `2.1167`mm になり、1/125 の紙で 0.048pt に潰れる。
  **単独（I）でも連続（J）でも同じ**なので、「連続寸法だけの問題」ではない。
- **症状は「値が描かれない」ではなく「描かれているが小さすぎて見えない」。**
  A も D も文字図形そのものは在って、大きさが `2.1167`mm だった。
  [#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
  が D1 について言ったことと同じである。**OIP の「文字 → スタイル」に `寸法(6pt)` と
  出るのも正しい**——当たっているものは当たっている。

#### 注釈の連続寸法で紙の pt を出す道は 3 つ（どれも実測で ★）

**同じ 1 回の実行で、1/125 の注釈に並べて測り分けた。**

| 道 | やること | 連続（型 86） | 単独（型 63） |
| --- | --- | --- | --- |
| **(a)** | 文字スタイルを当てた**後に** `ovDimFontSize` ＝ 紙 pt × 25.4/72 × 縮尺 を書き、**引き直す** | **★**（`CreateChainDimension` が引き直しなので、**繋ぐ前に書けばそれで済む**＝ B） | **★**（`ResetObject` を 1 回＝ C2。呼ばないと × ＝ C） |
| **(b)** | **縮尺用の文字スタイルを作る**（`ovTextStyleSize` ＝ 紙 pt ÷ 72 × 縮尺）。`ovDimFontSize` は触らない | **★**（E） | **★**（F） |
| **(c)** | **ビューポートと同じ縮尺のレイヤをアクティブにして**から当てて繋ぐ | **★**（G） | ★（I / J は 1/50 で同じ） |

- **`寸法(6pt)` のような図面にある文字スタイルを使い続けたいなら (a)。** 図面に文字
  スタイルを増やさずに済み、規格が持つものをそのまま当てられる。
- **(c) は縮尺ごとにレイヤが要る**（切り替えそのものは `ISDK::SetCurrentLayer` でできる
  ——下記「[注釈へ寸法を置くときの作り方](#注釈へ寸法を置くときの作り方ここだけ読めばよい)」
  の【訂正】）。**図ごとに縮尺が違うと成立しない**ので、一般には (a) か (b) を使う。
- **(a) で `ovDimFontSize` を書く相手は「繋ぐ前の直線寸法」である。** 繋いだ後の型 86 の
  `ovDimFontSize` は `2.1167` のまま（B の実測）なのに絵は `264.5833` で描かれていた
  ——**型 86 の `ovDimFontSize` は読んでも意味が無い。**
- **判定に使えるのは「描かれている文字の大きさ」だけ。** B / C / C2 は
  `GetTextStyleByClass` / `GetTextStyleRef` / `ovDimTextStyle` / `ovDimFontSize` の
  **4 つとも完全に同じ**（`false` / `113` / `113` / `264.5833`）なのに、絵は C だけが
  出ない。違うのは引き直しを通したかどうかだけである
  （[調査の作法「絵の文字の大きさを目視に頼らず測る」](Investigation%20Techniques.md)）。

#### 既に注釈へ置いてしまった連続寸法を後から直す——通る道は 1 つだけ（確定）

**`SetTextStyleRef` を「連続寸法そのもの（型 86）」へ掛け直し、`ResetObject` を呼ぶ。**
これ以外の道は通らなかった（[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)。
`寸法(6pt)` を 1:1 で当てて繋いだ連続寸法を 6 本、1/125 の注釈へ置いてから、
**1 本に 1 つだけ**直しを当てて測った）:

| 行 | 注釈へ置いた後にしたこと | 掛けた／書いた直後 | `ResetObject(型 86)` の後 | 最終 |
| --- | --- | --- | --- | --- |
| A | **何もしない**（対照） | — | — | **×**（0.048pt） |
| K1 | 中の型 63 へ `ovDimFontSize` ＝ `264.5833` | 値は入るが絵は `2.1167` | **値が `2.1167` へ戻された** | **×** |
| K2 | 同じ書き込みで `ResetObject` を呼ばない | 値は入るが絵は `2.1167` | （呼ばない） | **×** |
| H0 | **型 86 へ `SetTextStyleRef`**（1:1 がアクティブ） | 型 86 は `264.5833` ★／**中の型 63 は `2.1167`** | **中の型 63 も `264.5833`** | **★ 6pt** |
| H1 | 同じことを 1/125 がアクティブな文脈で | H0 と 1 つも違わない | H0 と同じ | **★ 6pt** |
| H2 | **中の型 63 へ `SetTextStyleRef`** | 中の型 63 は `264.5833` ★ | **`2.1167` へ戻された** | **×** |

- **掛け直しても読み戻しの 3 つは 1 つも動かない。** `GetTextStyleByClass`（`false`）・
  `GetTextStyleRef`（`113`）・`ovDimTextStyle`（`113`）は前も後も同じ値で、
  **動くのは `ovDimFontSize` と絵だけ**である（`2.1167` → `264.5833`）。
  **だから「OIP で選び直したかどうか」は読んで確かめられない**——同じ罠が
  D2/D4・B/C・H1/H6 と続いているので、確かめたいときは**描かれている文字の大きさ**を測る。
- **掛けるのは型 86。中の型 63 へ掛けてはいけない**（H2）——掛けた直後は効くのに、
  連続寸法を引き直した時点で捨てられる。**「直った」と見えるところで止めると、
  次に何かが `ResetObject` を呼んだ瞬間に消える。**
- **`ResetObject` は要る**（H0 / H1 の 3 列目）。`SetTextStyleRef(型 86)` だけでは
  **型 86 自身の文字しか書き換わらず、中の型 63 は古い大きさのまま**である。
- **アクティブレイヤの縮尺は関係が無い**（H0 ＝ H1）。**1:1 のシートレイヤがアクティブな
  ままでも直る**——注釈の中の寸法は**そのビューポートの縮尺**で焼かれるため（上記
  「[文字スタイルを当てる 2 つの口は、`ovDimFontSize` の扱いが違う](#文字スタイルを当てる-2-つの口はovdimfontsize-の扱いが違う)」の注記）。
  **つまり OIP で選び直すと直るのは「注釈を編集中だから」ではなく「その寸法が注釈の
  中にあるから」である。** SDK からも同じことができる。
- **後から `ovDimFontSize` を書く道は無い**（K1 / K2）。
  [#155](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/155)
  の「繋いだ後に中へ書いても捨てられる」は**文字スタイルを明示してあっても成り立つ**
  ——by-class 限定ではなかった。**単独の直線寸法なら書けば効く**（上記 C2）ので、
  「連続寸法の中身だけが書けない」と覚える。
- **とはいえ、この直しに頼らないほうがよい。** 作るときに正しく作る道（上記
  「[注釈の連続寸法で紙の pt を出す道は 3 つ](#注釈の連続寸法で紙の-pt-を出す道は-3-つどれも実測で-)」）
  なら 1 手で済む。この節は**既に間違った大きさで置いてしまった図面を救う**ためのものである。

### 連続寸法は中の `ovDimFontSize` で描かれる（← 食い違いは恒久的ではない）

> **【訂正】見出しはもと「（直線寸法と食い違う）」だった。下の実測は正しいが、
> 「直線寸法は文字スタイルの大きさで描かれるので `ovDimFontSize` は効かない」という
> 読みは誤りである**（[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)
> で実測）。**食い違ったのは 2 巡目が直線寸法を引き直していなかったからで**、
> 文字スタイルを当てたまま `ovDimFontSize` を書いて `ResetObject` を通せば、
> **単独の直線寸法も書いた値のとおりの大きさで描かれる**（上記
> 「[注釈の連続寸法で紙の pt を出す道は 3 つ](#注釈の連続寸法で紙の-pt-を出す道は-3-つどれも実測で-)」の C2）。
> つまりこれも
> [#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
> の「引き直しで反映される」の現れであって、型 63 と型 86 の性質の違いではない。
> **「だから `ovDimFontSize` は触らない」という結論も要らない**——触るなら引き直すこと。

**`ovDimFontSize` を触らなければ、直線寸法と連続寸法は同じ大きさになる。**
触ると食い違う——2 つの実行の絵を突き合わせると、こうなっていた:

| | 直線寸法へ当てたもの | 直線寸法の絵 | 連続寸法の絵 |
| --- | --- | --- | --- |
| 2 巡目 | 文字スタイル（6 インチ ＝ `152.4`）→ **`ovDimFontSize` を `264.5833` で上書き** | `152.4` の大きさ | **`264.5833` の大きさ**（明らかに大きい） |
| 3 巡目 | 文字スタイル（`10.4167` インチ ＝ `264.5833`）**だけ**。上書きしない | `264.5833` の大きさ | **同じ**（食い違わない） |

> **直線寸法（型 63）は文字スタイルの大きさで描かれ、連続寸法（型 86）は中の直線寸法の
> `ovDimFontSize` で描かれている。** 両者が一致するのは、`ovDimFontSize` を触らず
> `SetTextStyleRef` に書かせたときだけである（そのとき同じ値が入るため）。

- **だから `ovDimFontSize` は触らない。** 文字スタイルの大きさだけで決めれば、
  直線寸法でも連続寸法でも同じ絵になる（3 巡目で確認済み）。
- 上書きしてしまうと、**同じ設定に見える 2 本が違う大きさで出る**——しかも
  読み戻しでは 4 つとも同じ値なので、**気付けない。**

### 連続寸法へ繋いでも残る

繋ぐ前に直線寸法 2 本へ `SetTextStyleRef(113)` と `ovDimFontSize = 264.5833` を当ててから
`CreateChainDimension` した結果:

| 読んだ相手 | `GetTextStyleByClass` | `GetTextStyleRef` | `ovDimTextStyle` | `ovDimFontSize` |
| --- | --- | --- | --- | --- |
| 中の直線寸法（型 63）2 本とも | `false` | `113` | `113` | `264.5833` |
| 連続寸法そのもの（型 86） | `false` | `113` | **読めず** | `152.4000` |

- **繋ぐ前に当てれば、中の直線寸法はそのまま保つ。**
  **ただしこれは「文字スタイルを明示したとき」の話である**——`SetTextStyleRef` を呼んで
  いない（〈クラスの文字スタイル〉のままの）寸法では、**繋ぐと中の `ovDimFontSize` は
  捨てられ、「規格の紙の pt × 繋ぐときのアクティブレイヤの縮尺」に戻る**
  （[#155](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/155)
  で 4 回測った。下記「[〈クラスの文字スタイル〉のままなら文字の大きさは「繋ぐときのアクティブレイヤ」で決まる](#クラスの文字スタイルのままなら文字の大きさは繋ぐときのアクティブレイヤで決まる)」）。
  **つまり作り直しの大きさは「当たっている文字スタイル／無ければ規格 × アクティブレイヤ」
  で解かれる**——両方の実測はここで噛み合う。
- **連続寸法そのもの（型 86 の PIO）も文字スタイルを持つ。** `GetTextStyleByClass` /
  `GetTextStyleRef` は**読める**（上記「連続寸法には寸法の ov\* が効かない」の例外ではない
  ——これらは `ISDK` の口であってオブジェクト変数ではない）。一方 `ovDimTextStyle` は
  `GetObjectVariable` が `false` を返す。
- **`ISDK::SetPIOTextStyle(chain, 113, true)` を当てても何も変わらなかった**が、**既に
  `113` が入っていたので「効かない」ことの証明にはならない**【未確認】。
  **繋ぐ前に直線寸法へ当てる道が通っているので、この口は要らない。**

### 注釈へ移しても変わらない

6 本すべて `AddViewportAnnotationObject` が `true` を返し、**移した後の読み戻しは移す前と
完全に同じ**だった（`GetTextStyleByClass` / `GetTextStyleRef` / `ovDimTextStyle` /
`ovDimFontSize` の 4 つとも）。**移動は文字スタイルにも大きさにも触らない。**

### 注釈へ寸法を置くときの作り方（文字スタイル版。#157 以降はこちら）

**この手順と、上記「[注釈へ寸法を置くときの作り方](#注釈へ寸法を置くときの作り方ここだけ読めばよい)」
（#143 の `ovDimFontSize` を書く手順）は、どちらも正しい。**
[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
で「当たっている文字スタイルがあると `ovDimFontSize` が効かない」が**否定された**ので、
好きなほうを使ってよい。選び方だけ書いておく:

| | 文字スタイル版（この節） | `ovDimFontSize` 版（#143） |
| --- | --- | --- |
| 呼ぶもの | `CreateTextStyleResource` ＋ `SetTextStyleRef` | `SetObjectVariable(ovDimFontSize)` ＋ **`ResetObject`** |
| 図面に増えるもの | **文字スタイルが縮尺ごとに 1 つ** | 増えない |
| 引き直し | 要らない（`SetTextStyleRef` がその場で絵を書き換える） | **`ResetObject` が要る**（忘れると絵が変わらない） |
| 向くところ | 図面の中で大きさを揃えたい・利用者が後から触る | 1 本ずつ大きさを決めたい・図面を汚したくない |

以下は文字スタイル版の手順である。

```cpp
// 1. ビューポートの縮尺を読む（1/125 なら 125）。
double vpScale = 1.0;
TVariableBlock scaleVar;
if (gSDK->GetObjectVariable(viewport, ovViewportScale, scaleVar))
{
    Real64 s = 0.0;
    if (scaleVar.GetReal64(s) && s > 0.0)
        vpScale = s;
}

// 2. そのビューポート用の文字スタイルを 1 つ用意する（縮尺ごとに 1 つ要る）。
//    **ovTextStyleSize の単位はインチ**。紙で 6pt にしたいなら 6/72 × 縮尺。
MCObjectHandle style = gSDK->CreateTextStyleResource("注釈 6pt 1-125");
TVariableBlock sizeVar;
sizeVar = static_cast<Real64>(6.0 / 72.0 * vpScale);
gSDK->SetObjectVariable(style, ovTextStyleSize, sizeVar);
const InternalIndex styleRef = gSDK->GetObjectInternalIndex(style);

// 3. ふつうに寸法を作り、**文字スタイルを明示する**。
MCObjectHandle dim = gSDK->CreateLinearDimension(p1, p2, startOffset, 0, Vector2(0, 0), 0);
// … ovDimStandardName / ovDimShowValue などをここで当てる
gSDK->SetTextStyleRef(dim, styleRef);      // ← これを呼ばないと値が描かれない

// 4. **ovDimFontSize は触らない**（この版では文字スタイルに決めさせる。触るなら
//    書いたあとに ResetObject まで呼ぶこと——中途半端だと読み戻しと絵がずれる）。
// 5. 連続寸法にするなら、ここまでを 2 本ぶん済ませてから CreateChainDimension。
// 6. 注釈へ移す。
gSDK->AddViewportAnnotationObject(viewport, dim);
```

- **文字スタイルはビューポートの縮尺ごとに要る。** 名前に縮尺を入れておくと、同じ
  図面で 1/50 と 1/125 を混ぜたときに取り違えない。
- **既に図面にある文字スタイルを使い回すなら、`ovTextStyleSize` を読んで確かめる。**
  名前が `寸法(6pt)` でも、それが紙の 6pt になるのは 1 つの縮尺のときだけである。
- 規格が持つ文字スタイルは `GetDimensionStandardVariable(index, dimStdTextStyle, …)`
  で引ける（上記）。**そのまま当てても紙の pt にはならない**ので、大きさは作り直す。
- **規格の文字スタイル（`寸法(6pt)` など）をそのまま使い続けたいなら、この版ではなく
  「当てた後に `ovDimFontSize` を書いて引き直す」道を使う**——図面に文字スタイルを
  増やさずに済む。連続寸法なら `CreateChainDimension` が引き直しになるので、**繋ぐ前に
  書けば `ResetObject` は要らない**。上記
  「[注釈の連続寸法で紙の pt を出す道は 3 つ](#注釈の連続寸法で紙の-pt-を出す道は-3-つどれも実測で-)」
  （[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)）。

### #143 の「`ovDimFontSize` を書く」手順は正しかった——抜けていたのは `ResetObject`

**[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
で決着した。** #157 の 3 回の実行（D1 / E5 / F5）で
[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
の手順が再現しなかった理由は、**図面の違いでも文字スタイルでもなく、
`ovDimFontSize` を書いたあと寸法を引き直していなかったこと**だった。

**by-class のまま `ovDimFontSize` ＝紙 6pt 相当を書き、`ResetObject` を呼んだ寸法は、
注釈で紙 6pt に出た**（上記「[★ 絵の文字の大きさは、書いた瞬間には変わらない](#-絵の文字の大きさは書いた瞬間には変わらない引き直しで反映される確定)」の H2）。
**#143 の記述に足りなかったのは `ResetObject` の 1 行だけ**で、手順そのものは生きている。

当初の見立て（「**文字スタイルが当たっているときだけ効かない**」）は**反証された**:

| 確かめたこと | 結果 |
| --- | --- |
| 文字スタイルを外せるか | **外せる**（`SetTextStyleRef(dim, 0)`。上記） |
| 外したら `ovDimFontSize` は効くか | **外しても外さなくても同じ**——効くかどうかは `ResetObject` を通したかだけで決まった（H1 と H2 が同じ絵） |
| 外したまま `ResetObject` を呼ばなければ | **効かない**（H3 / H6）。＝ 文字スタイルの有無は関係が無い |

**#157 が「規格を `JIS` に替えても `GetTextStyleRef` が `30` のまま」で行き詰まったのは、
そもそも外す必要が無かったから**でもある（外す口は別にあり、外しても結論は変わらない）。

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
  **ただし `ovDimFontSize` は読めて書けてしまう**——`true` が返り、読み戻しも書いた値に
  なるのに、**中の直線寸法には効かない**（下記「〈クラスの文字スタイル〉のままなら、
  文字の大きさは『繋ぐときのアクティブレイヤ』で決まる」）。「戻り値で確かめたから
  入った」と読み違えない。
- **レイヤ直下の「件数」で判断しないこと。** 実測ではレイヤ直下が 19 → 21 と
  **増えて**見えたが、内訳を型で見ると `63` が 2 つ消えて
  **`90`（`kUndoPlaceholderNode`）が 3 つ**と `86` が 1 つ増えていた。
  `kUndoPlaceholderNode` は**undo の記録用の置き石で図形ではない**ので、
  「図形が増えた」と読み違えない。

### 〈クラスの文字スタイル〉のままなら、文字の大きさは「繋ぐときのアクティブレイヤ」で決まる

**確定（VW 2026 / macOS。[#155](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/155)
で実機のプローブを 4 回走らせた）。** 単独の直線寸法と違い、**〈クラスの文字スタイル〉の
ままの連続寸法では、中の直線寸法に `ovDimFontSize` を書いて大きさを決める道が無い。**
決まるのは**繋ぐ瞬間**だけである。

> **ここは「文字スタイルを明示していない（＝作った直後の〈クラスの文字スタイル〉の）
> 寸法」の話である。** 上記「[寸法の文字スタイル](#寸法の文字スタイル)」
> （[#157](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/157)）で
> **`SetTextStyleRef` を当ててから繋げば中の `ovDimFontSize` は保たれる**と実測されて
> いて、**両者は矛盾しない**——#155 の 4 回はすべて by-class（`ovDimTextStyle` = `-2`）で
> 走らせている。**つまり作り直しのときの大きさは「当たっている文字スタイルの
> 大きさ × そのときのアクティブレイヤの縮尺」で解かれる**（明示してあればその文字
> スタイル、by-class なら by-class で解決される文字スタイル）。
> **【訂正】この節が下で「紙で決めた大きさ」と呼んでいる左の因子は、規格のもの
> ではない**（#155 当時は「規格が紙で決めた大きさ」と書いていた）**。** #155 の図面ではカスタム規格が 1 件（`min-nano`）だけで、当たっている
> 規格と by-class で解決される文字スタイルが同じだったので、区別が付いていなかった。
> [#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)
> が**両者を別々にして測った**ところ、**大きさは by-class で解決される文字スタイルに
> 従い、当てた規格が指す文字スタイルには従わなかった**——上記
> 「[規格に「文字の大きさ」は無い](#規格に文字の大きさは無い持っているのは文字スタイルへの参照-1-本でそれは寸法に届かない)」。
> **下の表の数値はそのまま正しい**（同じ図面では一致するため）が、**「規格を変えれば
> 大きさが変わる」とは読まないこと。**
> **注釈で値（数字）を出したいなら、そもそも by-class では出ない**（#157 が絵で確定）
> ——手順は上記
> 「[注釈へ寸法を置くときの作り方（文字スタイル版）](#注釈へ寸法を置くときの作り方文字スタイル版157-以降はこちら)」に従う。
> **この節が効くのは、by-class のまま置く連続寸法**（伏図のようにデザインレイヤへ置いて
> 値が出ている使い方。#157 の「デザインレイヤに置いた by-class の寸法は値が出ていた」）
> **の大きさ**である。

**`CreateChainDimension` は中の直線寸法を作り直す。** 繋ぐ前の 2 本の手（ハンドル）は
中に現れず（両方とも別の手になった）、繋ぐ前に書いた `ovDimFontSize` は捨てられる。
繋ぐ前と繋いだ後で変わった欄は **`ov17`（＝`ovDimFontSize`）と、そこから計算される
`ov40`（＝`ovDimTextSizeInPoints`）の 2 つだけ**だった（`ov0`〜`ov60` を総当りした）。

**入る値は「紙で決めた大きさ × 繋ぐときのアクティブレイヤの縮尺」**（左の因子は
**規格のものではなく by-class で解決される文字スタイルのもの**。上の【訂正】）**。**
元の寸法をどこで作ったかは効かない——**双方向で入れ替えて測った**（規格 `min-nano` は
紙で 6pt。6pt = 2.1167mm）:

| 元の 2 本を作ったときのアクティブ | **繋いだときの**アクティブ | 繋いだ後の中の `ovDimFontSize` | 1/50 の紙の上 |
| --- | --- | --- | --- |
| 1:1（シートレイヤ） | 1:1 | `2.11667` | 0.12pt（値が見えない） |
| **1:1** | **1/50** | **`105.833`** | **6pt** |
| **1/50**（デザインレイヤ） | **1:1** | **`2.11667`** | **0.12pt** |
| 1/50 | 1/50 | `105.833` | 6pt |

**シートレイヤは `CreateLayer(..., kLayerSheet)` でアクティブになる**ので、断面の段取りで
シートレイヤを先に作ると、**そこで繋いだ連続寸法は必ず 1:1 の大きさ（紙で 0.12pt）になる**
——デザインレイヤへ置くぶんも含めて、**小さすぎて読めない字**になる。
**なお「軸組図の注釈で値が出ない」ことの原因はこれではない**（#157 が絵で確定した
とおり、注釈では by-class だと**大きさが正しくても値が出ない**）。**大きさの話と
「出るか出ないか」の話を混ぜない。**

**後から直そうとして潰した道**（すべて実測。by-class の寸法で、いずれも紙の上で
0.12pt のまま）:

> **文字スタイルを明示してあっても同じだった**（[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)
> で実測）。`SetTextStyleRef` を当てて繋いだ連続寸法でも、**繋いだ後に中の型 63 へ
> `ovDimFontSize` を書くと `ResetObject` で捨てられる**。つまりこの表は by-class 限定では
> なく、**「連続寸法の中身は後から `ovDimFontSize` で直せない」という規則**である
> （単独の直線寸法なら書いて引き直せば効く）。**後から直す道は 1 つだけあり、それは
> 型 86 へ `SetTextStyleRef` を掛け直すことである**——上記
> 「[既に注釈へ置いてしまった連続寸法を後から直す](#既に注釈へ置いてしまった連続寸法を後から直す通る道は-1-つだけ確定)」。

| 試したこと | 結果 |
| --- | --- |
| 繋ぐ**前**に中の直線寸法へ書く | 繋いだ時点で捨てられる |
| 繋いだ**後**に中の直線寸法（型 63）へ書く | 書ける（`true`・読み戻せる）が**絵は変わらない**。連続寸法へ `ResetObject` を呼ぶと捨てられる |
| **連続寸法そのもの（型 86）へ書く** | 書けて読み戻せて、`ResetObject` を越えても連続寸法自身は保つのに、**中には効かない** |
| 中へ `999`・連続寸法へ `105.833` と別々に書いて `ResetObject` | 中は**どちらでもない `2.11667`**（＝繋いだときの値）になった |
| 注釈へ移す（`AddViewportAnnotationObject`）・`UpdateViewport` | 中へ書いた値は残るが、**絵は小さいまま**（値と絵が食い違う） |
| 注釈（1/50 のビューポート）の中で `ResetObject` | `2.11667` のまま——**容れ物の縮尺では解き直さない** |
| **アクティブレイヤを 1/50 にしてから**注釈の中で `ResetObject` | `2.11667` のまま——**後からの解き直しは起きない**（効くのは繋ぐ瞬間だけ） |

- **中の値が後から動くのは「ビューポートの縮尺を変えたとき」だけ**——VW が注釈の中身を
  比例して書き換えるため（下記「[注釈はビューポートの縮尺で描かれる](#注釈はビューポートの縮尺で描かれる用紙-11-ではない)」と同じ仕掛け）。
  実測では、中へ `105.833` を書いた状態（値と絵が食い違っていた）で 1/50 → 1/100 に
  すると中の値が `211.667` になり、**絵もその大きさになった**。1/50 へ戻すと `105.833` で
  絵も `105.833` だった。**「中へ書く ＋ 縮尺の往復」で絵を合わせることはできてしまうが、
  手順にしてはいけない**（縮尺を触らない限り絵は直らず、比例で戻っているだけなので、
  書いた値が「正しい」保証もない）。ここで大事なのは**正しく作ってあれば、後から
  ビューポートの縮尺を変えても崩れない**ということである。
- **対照（同じ 1 回の実行で測った）: 単独の直線寸法は書けば効く。** 1:1 生まれに
  `105.833` を書いて注釈へ入れ `ResetObject` すると、**描かれている文字も `105.833`**
  （紙で 6pt）になった。何も書かない 1 本は `2.11667` のままだった。**`ResetObject` は
  絵を描き直す引き金ではある**——連続寸法だけが中の値を捨てている。

#### by-class の連続寸法で狙った大きさを出す手順

**注釈へ置くなら、まず上記「[注釈へ寸法を置くときの作り方（文字スタイル版）](#注釈へ寸法を置くときの作り方文字スタイル版157-以降はこちら)」を読む**
——注釈では文字スタイルを明示しない限り値が出ないので、**この節の道は注釈では使えない。**
ここは**デザインレイヤへ置く連続寸法**（値が出ている使い方）の大きさを決める話である。

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

- **実測（#155）で入った値はここまで**——`1/50` がアクティブなうちに繋ぐと、中の
  `ovDimFontSize` は `105.833`（紙で 6pt 相当）になり、**寸法が内部に持つ文字図形**の
  1 文字目も `105.833mm` だった。**「注釈の絵に数字が出るか」はこの測り方では分からない**
  （#155 では絵を見ていない。注釈での出方は #157 の絵で決まっている）。
- **単独の寸法とは手順が違う。** 単独なら `ovDimFontSize` を書けば効く（ただし文字
  スタイルが当たっていればそちらが勝つ。#157）。**連続寸法では書く道が無い**ので、
  by-class のまま大きさを決めたいなら**アクティブレイヤを合わせるしかない**。
  逆に、**連続寸法にしない**（単独の直線寸法を並べる）なら書く道が使える。
- **寸法規格の紙の pt を変える道は無い**（[#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)
  で実測確定。#155 の時点の【推定】はここで否定された）。規格に「文字の大きさ」の
  セレクタは無く、結び付いた文字スタイルを `SetCustomDimensionStandardVariable` で
  差し替えても**寸法には届かない**——上記
  「[規格に「文字の大きさ」は無い](#規格に文字の大きさは無い持っているのは文字スタイルへの参照-1-本でそれは寸法に届かない)」。
  **アクティブレイヤを合わせずに済ませたいなら、規格ではなく次の 2 つを使う**（どちらも
  実測確定）: **繋ぐ直前に `ovDimFontSize` を書く**（#161。上記
  「[#143 の手順は正しかった](#143-の-ovdimfontsize-を書く手順は正しかった抜けていたのは-resetobject)」）、
  または**繋ぐ前に `SetTextStyleRef` で文字スタイルを明示する**（#157。注釈へ置くなら
  こちらの手順に従う）。

#### 中身を読むときの注意（測り方）

- **中の 63 の `ovDimFontSize` を読んでも「絵に出ている大きさ」とは限らない。** 書いた
  直後は値だけが変わり、絵は古い大きさのままだった。**絵は、寸法の中に描かれている
  文字図形（型 `10` ＝ `kTextNode`）を `GetTextSize` で測る**か、外接矩形の高さで見る
  （[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)
  の物差し）。
- **2D 表現のグループ（型 `11`）の中にも直線寸法の写しが入っているが、そこは作り直しでも
  古い大きさのまま残った**（この群の外接矩形は高さ `0`）。読むなら**連続寸法の直下の
  `63`** を読む（連続寸法の外接矩形も直下の `63` に追従した）。
- **作り直しのたびに中の `63` の手（ハンドル）は変わる。** 掴んだまま使い回さず、
  そのときに `FirstMemberObj` から引き直す。

### `FirstMemberObj` / `NextObject` の walk は終端も数える

**リストの末尾に `kTermNode`（型 `0`）が 1 つ入っている。** 素直に
`FirstMemberObj` → `NextObject` で歩いて数えると、**実際の図形より 1 多く出る**。
件数を使うなら終端を除くか、そもそも件数ではなく**型の並び**を見る
（上の連続寸法の件は、型を見なければ原因に辿り着けなかった）。

## 寸法の関連付け（図形が動いたら寸法も追うか）

[#138](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/138) の調査。
`ISDK::AssociateLinearDimension(h, selectedObjectsMode)` を**どう呼べば、図形が動いたときに
寸法が追従するのか**。

**答えは「効く」。** [#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134)
の 8 通りが空振りだったのは関連付けの失敗ではなく、**制約を解く段を呼んでいなかった**
だけだった（[下記](#こう呼べば効く関連付けは成立していて足りなかったのは解く段だった)）。

### 関連付けは「パラメトリック制約」の仕組みの一部である

制約の口の一覧（宣言と説明はヘッダの写し。仕組みそのものは下記のとおり実機で確かめた）。

寸法の関連付けは寸法だけの特別な仕掛けではなく、SDK が持つ**パラメトリック制約**
（parametric constraints）の一部として持たれている。制約は図形にぶら下がる
**制約ノード**（`kConstraintNode = 110`。`Objs.TDType.h:176`）で、`ISDK` にはそれを
張る・読む・**解く**ための一式がある。

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

**ここで要点は `UpdateConstraintModel` の説明**である。制約は「図形を動かせば勝手に
解かれる」ものではなく、**呼び出し側が「動かす前に模型を作り、動かした後に解く」段を
踏む**作りになっている。

```cpp
gSDK->CreateConstraintModel(nil, true);   // ① 動かす **前** に（選択中の図形で模型を作る）
gSDK->MoveObject(line, 1200, 0);          // ② 動かす
if (!gSDK->UpdateConstraintModel())       // ③ 解く
	/* 解けなかった。undo すべき */;
```

**#134 が呼んだのは `MoveObject` と `ResetObject` だけ**なので、③ が一度も走っていない。

### こう呼べば効く——関連付けは成立していて、足りなかったのは「解く段」だった

**`AssociateLinearDimension` はちゃんと関連付けていた。** [#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134)
の「8 通り空振り」は関連付けの失敗ではなく、**制約を解く段を一度も呼んでいなかった**
だけだった。効く手順:

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

- **`selectedObjectsMode` は `true` でも `false` でも同じ**（どちらでも関連付き、
  どちらでも追った）。ヘッダどおり `false` は「図面の全図形を調べる」の意味なので、
  **呼ぶ前に選択しておく必要は無い**。
- 表は 1 つの `dimType`（`0`）で取ったが、[上記](#0fix_ang-と-1sloped-に差は無い角度は-ovdimdirection-が決める)の
  とおり `dimType` は追従に関与しない。
- **`UpdateConstraintModel` は 4 通りとも `true`（解けた）を返した。** 4 行目が追わない
  のは「解けなかった」からではない（下記）。

#### 関連付いたかは `HasConstraint` で読める

`AssociateLinearDimension` は `void` だが、**成否は読める**。実測で
`HasConstraint(寸法)` と `FindAuxObject(寸法, 110)` は**常に一致**した。

| | 寸法 | 相手の線分 |
| --- | --- | --- |
| 関連付ける前 | `HasConstraint=いいえ` / 補助オブジェクトの型 `[76]` | 同左 |
| `AssociateLinearDimension` の後 | **`はい`** / `[110]`（`kConstraintNode`） | **`はい`** / `[110]` |
| 拘束が消えた後 | `いいえ` / `[90]`（`kUndoPlaceholderNode`） | — |

**これが [#138](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/138)
の「関連付いているかを問い合わせる口はあるか」の答え**である。`[76]` は関連付けとは
無関係に最初から付いている別物なので、**型 `110` の有無で見る**（または
`HasConstraint`）。`GetDimensionsAssociatedToPlugin` は引数が PIO で、一般の図形には
使えない。

#### 関連付けの正体は「同一点上（coincident）」の拘束——VW 自身がそう名乗る

拘束と両立しない編集をしようとすると、VW は確認ダイアログを出す:

> **関連する拘束を削除しますか？**
> 1 つ以上の関連する拘束がありこの編集操作を実行できません。関連する拘束を削除すると
> 操作が続行されます。

続く「拘束確認」の一覧に並ぶのは **タイプ「同一点上」／カテゴリ「寸法」**。つまり
関連付けは、寸法の測点と図形の頂点を重ねる **coincident 拘束**（`SetBinaryConstraint`
の `type = 1` と同じもの）そのものである。

**上の表の 4 行目はこのダイアログを出す。** 「はい」を押すと拘束が消え
（`HasConstraint` が `いいえ` に戻り、補助オブジェクトの型が `[90]` になる）、当然
追従しない。**`CreateConstraintModel(obj, false)` + `AddToConstraintModel` の経路を
使ってはいけない**——`CreateConstraintModel(nil, true)`（選択で渡す）なら出ない。
**このダイアログを抑止する環境設定は SDK に無い**（`ProgramVariables.h` を当たった）。

#### 門は `varAssociateDims`(28) ただ 1 つ——`varAutoAssociateDims`(134) は関与しない

| selector | 名前 | 置き場所 |
| --- | --- | --- |
| `28` | `varAssociateDims` | `ProgramVariables.h:43`（Boolean selectors） |
| `134` | `varAutoAssociateDims` | `ProgramVariables.h:162`（More Boolean Selectors） |

どちらも `ISDK::GetProgramVariable` / `SetProgramVariable` で読み書きする
（**Boolean selector** なので 1 バイト）。4 通りを総当たりした実測:

| `varAssociateDims`(28) | `varAutoAssociateDims`(134) | 関連付いたか | 測点が追ったか |
| --- | --- | --- | --- |
| `1` | `1` | はい | **追従した** |
| `1` | `0` | はい | **追従した** |
| `0` | `1` | **いいえ** | しない |
| `0` | `0` | **いいえ** | しない |

**`varAssociateDims`(28) が `0` だと、`AssociateLinearDimension` は何もしない**
——`HasConstraint` は `いいえ` のまま、補助オブジェクトの型も `[76]` のままで、
その後 `UpdateConstraintModel` を呼んでも当然追わない。**`varAutoAssociateDims`(134)
は、`1` でも `0` でも結果を変えなかった。**

**実機（VW 2026）の新規図面の走り出しは `28=1` / `134=0`** なので、**ふつうは何も
しなくてよい**。プラグイン側は `varAssociateDims` を**読んで確かめるだけ**でよく、
書き換える必要は無い（利用者が文書環境設定で切っている場合に備えるなら、
書き換えるのではなく「関連付けができない」と伝えるほうが筋が通る）。

**画面でも見分けが付く。** 上の 4 通りを 1 つの図面に上から順に並べて描かせたところ、
**拘束マークが出たのは上の 2 段（`28=1` の 2 通り）だけ**だった。`HasConstraint` の
読みと、VW が画面に描く拘束マークは一致する——**関連付いたかどうかは、SDK から
読んでも画面を見ても同じ答えになる**。

#### 通らなかった道

- **`SetBinaryConstraint` で自分で coincident を張ることはできない。** 頂点番号を引く
  `GetClosestPt` が、**寸法に対して `-1`（＝型が非対応）**を返す（線分側は `1` を返す
  ので、非対応なのは寸法のほう）。`-1` のまま呼ぶと `SetBinaryConstraint` は `false`。
  **関連付けを張る口は `AssociateLinearDimension` の 1 本だけ**と考えてよい。
- **`SetHorizontalDimensionConstraint(線分, pt1, pt2, distance, offset)` は寸法を作らない。**
  戻り値は `true` で、渡した線分に拘束ノード（`[110]`）は付くが、**レイヤの図形数は
  増えない**（32 → 32）。これは「図形自身に寸法拘束を掛ける」口であって、追従する
  寸法図形を作る口ではない。

#### 画面でドラッグしたときは、VW が自分で解く——何もしなくても追う

**利用者が画面で図形をつまんで動かすと、寸法の測点はそのまま追う。** SDK のように
`UpdateConstraintModel` を呼ぶ必要は無い——**画面の操作は解く段まで自分でやっている**。

実測（関連付け済みの 1 組を残しておき、利用者が線A を画面でドラッグしたあとに読み直した）:

| | 線A の x | 寸法の測点（始点） |
| --- | --- | --- |
| ドラッグ前 | `0` | `0` |
| ドラッグ後 | **`1001.783`** | **`1001.783`**（ぴったり一致） |

線B（触らない側）は `3000` のままで、寸法の終点も `3000` のまま。**動かした端だけが
追い、動かしていない端は動かない**——関連付けが端点ごとに効いていることが分かる。

**つまり引き金は 2 通りある。**

| 動かす人 | 追従の引き金 |
| --- | --- |
| **利用者（画面の操作）** | **要らない。VW が自分で解く** |
| **プラグイン（SDK）** | **`CreateConstraintModel` → 動かす → `UpdateConstraintModel` を自分で踏む** |

#### 実装への含意——関連付けておけば、作り直さなくてよい

**伏図・軸組図へ寸法を自動で入れる用途では、これは大きい。** 寸法を入れるときに
`AssociateLinearDimension` を呼んでおけば、**その後で利用者が図形を動かしても、寸法は
自分で追う**（プラグインは何もしなくてよい）。プラグイン自身が図形を動かすときだけ、
上の 3 段を踏む。

**[#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134) の
「図形が動いたら寸法を作り直す前提でよい」は、この結果で不要になった。**
作り直しが要るのは、**関連付けが張れない相手**（測点と一致する頂点を持たない図形）か、
**拘束が壊れたとき**（上記「関連する拘束を削除しますか？」で消えた場合）だけである。

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
3. 断面の表示の作法（`ovSectionViewportDisplayObjectsBeyondCutPlane`(1064) ＋
   **`ovSectionViewportDisplayObjectsBeforeCutPlane`(1065)** /
   `ovViewportDisplayPlanar`(1035) / `ovViewportDisplay2DComponents`(1059)）を
   **すべて更新より前に**設定する。**1064 だけでは足りない**——切断面が何も切っていない
   配置では、`1065` が `false` の間は `1064` を立てても断面は空のままである
   （[Viewports](Viewports.md)「断面に中身が入る条件」。#202 で実測）。
4. 表示レイヤを表示にする（`SetViewportLayerVisibility`）。
5. **注釈へ寸法を足した後、もう一度クラスを全部表示へ戻して再更新する。**
   注釈へ**後から**足した図形のクラスは非表示のままだから（[Viewports](Viewports.md)）
   ——ここを飛ばすと、断面は描かれているのに**寸法だけが見えない**。

**「寸法が出ない」と判断する前に、ビューポートに何か 1 つでも描かれているかを見る。**
空枠のままなら、それは寸法の問題ではない。

**実測で、この手順を踏んだら注釈の寸法は見えるようになった。** 踏む前は断面
ビューポートが「×」印の空枠で寸法も何も出ず、踏んだ後は**寸法が縦横 1 本ずつ
はっきり見え、値も渡した 2 点間の距離どおり**（4000mm と 2400mm）に出た。

### ビューポートの外接矩形——53.3mm 角の空枠より小さいものは隠れる

> **【訂正の記録】ここには「ビューポートの外接矩形は中身を勘定に入れない。注釈へ図形を
> 足しても増えないし、断面に写る図形が増えても増えない」と書いていた。**
> **条件を 2 つ落としていた**（[#200](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/200)
> で実機確認して取り直した。数値は [Viewports](Viewports.md)「`GetObjectBounds(viewport)` が
> 何で決まるか」）。
>
> 1. **注釈が外接に入るのは `ResetObject` を通した後だけ。** 足した直後は 1mm も動かない。
> 2. **空枠（53.3mm 角 ＝ ±26.649）より大きいものだけが外接に現れる。**
>    空枠は中身や注釈に**置き換わる**（和を取るのではない）。
>
> **あのときの寸法 4000mm / 2400mm は、1/100 なら用紙で 40mm / 24mm**——**どちらも
> 53.3mm 角の内側に収まる。** だから「増えなかった」のであって、「勘定に入れない」の
> ではなかった。注釈へ 4 辺とも空枠の外へ出る矩形を置いた枚では、外接は**注釈の矩形
> そのもの**になった。**高さ 0 → 2800mm の壁（用紙で 28mm）も同じ理由で隠れていた。**

**実測で踏んだ落とし穴はそれ自体は有効である。** 「中身が描かれたか」を機械で確かめようとして
`GetObjectBounds(viewport)` を作法の前後で比べたが、**注釈に寸法が 2 本見えている
状態でも、外接矩形は空枠のときと 1mm も変わらなかった**（どちらも
`左-26.64 上26.64 右26.64 下-26.64` の 53.3mm 角）。

- **したがって「外接矩形が変わらない＝何も描かれていない」と読んではいけない**
  ——用紙で 53.3mm に収まるものは、描かれていても外接に出ない。
- **「描かれたか」を機械で確かめるならキャッシュ群を数える。**
  `GetViewportGroup(vp, 3 / 5 / 6 / 7 / 15)` のどれかに件数が入っていれば断面は描けている
  （群 4 は断面群で、空でも必ず在る）。絵を見る必要は無い（[Viewports](Viewports.md)
  「断面に中身が入る条件」）。

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

### 注釈はビューポートの縮尺で描かれる（用紙 1:1 ではない）

> **ここには以前「注釈空間は用紙 1:1 なので、文字は紙のポイントのまま出る」と
> 書いていた（#132）。誤りである**（[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
> で取り直した）。あのときの 2 本は**置き場所以外も違っていた**——注釈へ入れた側は
> シートレイヤ（1:1）がアクティブなときに作った寸法で、しかもその平面ビューポートの
> 縮尺は既定の 1:1 のままだった。だから「注釈だから 2.116 になった」とは言えない。

**ビューポートの注釈の中身は、そのビューポートの縮尺で描かれる。** 注釈へ置く寸法も、
**ビューポートの縮尺で割り戻した `ovDimFontSize`** を持っていなければ紙の上で正しい
大きさにならない。

実測（VW 2026 / macOS。縮尺 **1/50** の平面ビューポートと断面ビューポートへ、同じ規格・
`ovDimShowValue = true` の寸法を移した。**絵で確認済み**）:

| 作ったときのアクティブレイヤ | `ovDimFontSize` | 紙の上（＝ ÷ 50） | 実機の絵 |
| --- | --- | --- | --- |
| デザインレイヤ 1/50 | `105.833` | **2.1167mm ＝ 6pt** | **値が出る** |
| シートレイヤ 1:1 | `2.1166` | 0.0423mm ＝ **0.12pt** | **寸法線は出るが値が出ない** |

**これが「軸組図では寸法の値が出ない」の正体である。** 断面ビューポートだから出ない
のではなく、**シートレイヤ（1:1）がアクティブなときに作ったから**——シートレイヤは
`CreateLayer(..., kLayerSheet)` を呼んだ時点でアクティブになる（上記「作った寸法は
アクティブレイヤに入る」）ので、**断面の段取りでシートレイヤを先に作ると、そこから
作った寸法は全部 1:1 生まれになる**。伏図が無事だったのは、そちらが 1/50 の
デザインレイヤがアクティブなうちに作られていたからにすぎない。

- **平面と断面で扱いに差は無い。** どの絵で何が見えたかを正確に書いておく——
  **1:1 生まれが「寸法線だけ・値なし」になったのを今回の絵で見たのは平面ビューポート
  の注釈**で、**断面ビューポート**で同じことが起きていたのが利用側の最初の実測
  （[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)）。
  **読み戻した数値のほうは平面・断面で完全に同じ**だった（移しても `ovDimFontSize` は
  変わらず、ビューポートの縮尺を変えれば両方とも同じように書き換わった）。断面の注釈へ
  正しい大きさ（`105.833`）の寸法を 2 本置いた絵では、**どちらも値が読め、大きさも
  揃っていた**。
- **`AddViewportAnnotationObject` は `ovDimFontSize` を変えない。** 大きさは焼き付いた
  まま移る（座標が変わらないのと同じ。上記）。
- **ビューポートの縮尺を変えると、注釈の中身の `ovDimFontSize` が比例して書き換わる。**
  実測で 1/50 → 1/100 にすると `105.833` → `211.666` になり、1/50 へ戻すと
  `105.833` に戻った（平面・断面とも）。つまり **VW は「紙の上の見え方が変わらない
  ように」注釈の中身を作り直している**——これも「注釈がビューポートの縮尺で描かれて
  いる」ことの裏取りであり、かつ**一度正しく作れば、後でビューポートの縮尺を変えても
  崩れない**ということでもある。
- **注釈群そのものからは縮尺を読めない。** `GetViewportGroup(vp, kViewportGroupAnnotation)`
  が返すのは型 `11`（`kGroupNode`）で、`GetLayerScaleN` を当てても `1` が返るだけ
  （意味のある値ではない）。**縮尺はビューポートの `ovViewportScale`(1003) から読む。**
- **`CreateViewport` / `CreateSectionViewport` が作るビューポートの縮尺の既定は `1:1`**
  （実測。どちらも直後の `ovViewportScale` が `1.0` だった）。**縮尺は自分で書く。**

### 注釈へ寸法を置くときの作り方（ここだけ読めばよい）

> **【この手順には `ResetObject` が 1 行足りなかった】**
> ここには一時「当たっている文字スタイルがあると効かないので使うな」と書いていた
> （#157）。**それは誤りで、[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
> が取り消した**——効かなかったのは文字スタイルのせいではなく、**書いたあとに寸法を
> 引き直していなかった**せいである。手順 4 に `ResetObject` を足してあるので、
> **この道はそのまま使ってよい。** 文字スタイル側で決める道
> 「[注釈へ寸法を置くときの作り方（文字スタイル版）](#注釈へ寸法を置くときの作り方文字スタイル版157-以降はこちら)」
> も同じく正しいので、あちらの表を見て選ぶ。

紙で 6pt に見せたい寸法を、ビューポートの注釈へ置く手順:

```cpp
// 1. ビューポートの縮尺を読む（1/50 なら 50。既定は 1 なので、自分で書いたなら その値）。
double vpScale = 1.0;
TVariableBlock scaleVar;
if (gSDK->GetObjectVariable(viewport, ovViewportScale, scaleVar))
{
    Real64 s = 0.0;
    if (scaleVar.GetReal64(s) && s > 0.0)
        vpScale = s;
}

// 2. ふつうに作る（アクティブレイヤが何であってもよい）。
MCObjectHandle dim = gSDK->CreateLinearDimension(p1, p2, startOffset, 0, Vector2(0, 0), 0);
// … ovDimStandardName / ovDimShowValue などをここで当てる

// 3. **文字の大きさを、そのビューポートの縮尺で割り戻して書く。**
TVariableBlock fontSize;
fontSize = static_cast<Real64>(6.0 * 25.4 / 72.0 * vpScale);   // 紙で 6pt
gSDK->SetObjectVariable(dim, ovDimFontSize, fontSize);

// 4. **書いたら引き直す。これを忘れると絵は 1 ミリも変わらない**（#161）。
//    読み戻しは書いた値を返すので、呼び忘れても読み戻しでは気付けない。
gSDK->ResetObject(dim);

// 5. **連続寸法へ繋ぐなら、繋ぐこと自体が引き直しになる**ので 4 は省いてよい
//    （#161 の実測で、繋ぐ前に書いた値が中の直線寸法に残って絵にも出た）。
//    ただし by-class のままだと「繋ぐときのアクティブレイヤ」で焼き直される道もある
//    ——上記「[連続寸法](#クラスの文字スタイルのままなら文字の大きさは繋ぐときのアクティブレイヤで決まる)」。
// 6. 注釈へ移す。
gSDK->AddViewportAnnotationObject(viewport, dim);
```

> **手順 4 は [#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
> で足した。** #143 当時の記述にこれが無く、そのせいで #157 の再現実験は 3 回とも
> 「値が出ない」になった（上記
> 「[★ 絵の文字の大きさは、書いた瞬間には変わらない](#-絵の文字の大きさは書いた瞬間には変わらない引き直しで反映される確定)」）。

- **この `ovDimFontSize` を書く道を勧める。** 作るときのアクティブレイヤが何であっても
  結果が同じになるので、**伏図のデザインレイヤの縮尺と断面ビューポートの縮尺が違って
  いても通る**（[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)
  の 4 番目の問い）。実測でも、1:1 で作った寸法にこれを書いたら、1/50 のビューポートと
  同じ縮尺で作った寸法と**見分けが付かない大きさで値が出た**。
- 「**ビューポートと同じ縮尺のレイヤをアクティブにしてから作る**」道でも同じ結果になる
  （実測）。図ごとに縮尺が違うとレイヤを縮尺ぶん用意することになるので、**書く道のほうが
  一般的**である。

  > **【訂正】ここには一時「`ISDK` にアクティブレイヤを切り替える口は無い——`CreateLayer`
  > が副作用で切り替えるだけ」と書いていたが、誤りだった。**
  > **`ISDK::SetCurrentLayer(MCObjectHandle)`**（`ISDK.h:1394`）と
  > **`SetCurrentLayerN(MCObjectHandle, Boolean bAllowUnifiedViewMaintenance = false)`**
  > （`ISDK.h:2761`）があり、いまのアクティブレイヤは **`ISDK::GetActiveLayer()`**
  > （`ISDK.h:2020`）で読める。実機で効くことを
  > [#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175) の
  > プローブで確かめた——同じ文字スタイルを `SetCurrentLayer` で 1/50 と 1:1 を
  > 切り替えて当てたら、焼き付いた値が `176.38889` と `3.52778` に分かれた。
  > 「**縮尺ごとにレイヤが要る**」ことは変わらないので、道の選び方の結論は同じである。
- **`ovDimTextSizeInPoints` へ書く道は勧めない。** 上記のとおり「読む時点のアクティブ
  レイヤの縮尺」で解釈されるので、**アクティブが何かを知らないと何を書けばよいかが
  決まらない**（アクティブが 1:1 なら `6` ではなく `6 × ビューポート縮尺` を書くことに
  なる）。`ovDimFontSize` なら容れ物にも状態にも依らない。
- **直すのは注釈へ移す前でも後でもよい**（実測では移した後に書いて効いた）。
  **ただしどちらの場合も、書いたあとに `ResetObject` が要る**——
  `AddViewportAnnotationObject` も `UpdateViewport` も引き直しにはならない（#161）。
- **直すのは `ovDimFontSize`（と、そのあとの `ResetObject`）だけでよい——ほかに書く
  ものは無い**
  （[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)
  で確定）。補助線・寸法線の出・端記号・文字と寸法線の間隔は規格の用紙インチから
  **ビューポートの縮尺で描くたびに解き直される**ので、1:1 で作った寸法でも狂わない
  （上記「[規格の用紙インチは描くたびに解かれる](#焼き付くのは文字の大きさだけ規格の用紙インチは描くたびに解かれる)」）。
  **実測でも、1:1 生まれに `ovDimFontSize` だけ書いた 1 本と、1/50 生まれの 1 本を
  同じ注釈へ並べたら、値も端記号も補助線も見分けが付かなかった。**
- **後からビューポートの縮尺を変えても崩れない。** `ovDimFontSize` は VW 自身が比例して
  書き換え（上記「注釈はビューポートの縮尺で描かれる」）、線の幾何は解き直される
  ——**一度正しく作れば、以後は触らなくてよい。**
