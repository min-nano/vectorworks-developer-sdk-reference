# 寸法（直線寸法・寸法規格）

平面図・断面のビューポートへ**寸法を自動で入れる**ための調査（[#129](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/129)）。
寸法そのものを作る口と、寸法の見え方を決める**寸法規格**（dimension standard）・文字スタイル・
連続寸法・関連付け・注釈への置き方をまとめる。実測はすべて **VW 2026 / macOS**（Windows では
測っていない）。測った手順・実測の表・潰した道は [計測記録](../Metrics/Dimensions.md)。

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
- **関連付くのは、画面に表示されている範囲の中にある寸法だけ**（[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)）。
  範囲の外では黙って何もしない（`HasConstraint`=no）。表示の外へ置く可能性があるなら、
  呼ぶ前に `SetViewCenter` で寸法へ寄せ、呼んだ後に戻す（下記
  「[関連付くのは表示範囲の中だけ](#関連付くのは表示範囲の中だけ249-で確定)」）。

## `CreateLinearDimension` の引数

```cpp
virtual MCObjectHandle CreateLinearDimension(
    const WorldPt& p1, const WorldPt& p2,
    WorldCoord startOffset, WorldCoord textOffset,
    const Vector2& dir, short dimType) = 0;
```

| 引数 | 意味 |
| --- | --- |
| `p1` / `p2` | 測る 2 点。**測点そのもの**（寸法線の位置ではない） |
| `startOffset` | **`p1` から寸法線までの距離**（`WorldCoord` ＝ mm。[Parametric Objects](Parametric%20Objects.md)） |
| `textOffset` | **使われない。** 0 を渡す |
| `dir` | `p2` - `p1` を正規化した向き。`Vector2` は `WorldPt` の typedef。**`(0,0)` で自動計算** |
| `dimType` | 種類。**`ovDimClass` にそのまま入る**（下記） |

- 宣言と説明は `Include/Kernel/API/APIBase.Legacy.Defs.h:2095`（`ISDK.h:1059` の実体）。
  ヘッダに `textOffset is CURRENTLY UNUSED` と明記されている
  （[ヘッダの写し](../Metrics/Dimensions.md#createlineardimension-の引数)）。

### `dimType` は `ovDimClass` そのもの

- **渡した `dimType` がそのまま `ovDimClass`(26) として読み戻せる**（0〜5 で全一致）。
  `0` `fix_ang`（角度固定）/ `1` `sloped` / `2` `ordinate` / `3` `radial` /
  `4` `diametrical` / `5` `ang`（角度寸法）。
- **2 点間の直線寸法として意味があるのは `0` / `1` / `2` だけ。** `3`〜`5` は 2 点を渡しても
  寸法として成立しない形が出る。
- **水平／垂直の寸法が欲しいなら、`dimType` ではなく `p1` と `p2` をそう取る。** 斜めの 2 点を
  渡せば `dimType = 0` でも寸法線は斜めになる。`dir` に `(0,0)` を渡すと `p2`-`p1` を正規化した
  向きが入る（[実測](../Metrics/Dimensions.md#dimtype-は-ovdimclass-そのもの)）。

### `0`（`fix_ang`）と `1`（`sloped`）に差は無い——角度は `ovDimDirection` が決める

- **作った後に SDK から触っても、利用者が画面で端点をドラッグしても差は出ない**
  （[#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134)）。
  名前から期待する「`fix_ang` は角度を保ち、`sloped` は追従する」はどちらにも起きない。
- **寸法線の角度を持っているのは `ovDimDirection`(12) 1 つだけ。** ヘッダは `FracPt` /
  `Not for public use` だが、**`WorldPt` として読み書きでき、書けば実機で寸法線が回る。**
- **測点（`ovDimStartPt`(13) / `ovDimEndPt`(14)）を書き換えても `ovDimDirection` は再計算
  されない**（`ResetObject` でも戻らない）。画面のドラッグでも保たれる。
- **`dimType` は `0` と `1` のどちらで作ってもよい。** 狙った角度は `p1`/`p2` か
  `ovDimDirection` で決める（[実測](../Metrics/Dimensions.md#0fix_angと-1slopedに差は無い角度は-ovdimdirection-が決める)）。

#### 表示される寸法値は「射影」であって実長ではない

- **寸法値は測点間ベクトルを `ovDimDirection` へ射影した長さ**（実機の絵で 3 例とも一致。
  [実測](../Metrics/Dimensions.md#表示される寸法値は射影であって実長ではない)）。
- **使い道:** 斜めに離れた 2 点へ「水平距離」の寸法を入れたいなら、測点はそのままで
  `ovDimDirection` に `(1,0)` を書けばよい（伏図・軸組図で効く）。
- **表示値は、寸法の中の群（型 11）の中の文字（型 10）として読める**（`GetTextChars`。
  作った直後から在る。[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）。
  VectorScript の `GetDimText(h)` に当たる `ISDK` の口は無いが、要らない。
  - 【訂正】以前は「表示値は SDK から読めない」と書いていた——直下しか見ていなかった。
    歩き方は [Investigation Techniques](Investigation%20Techniques.md)。

#### `AssociateLinearDimension` が効かなかったのは、解く段を呼んでいなかったから

- **#134 の「効かない」は [#138](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/138)
  で撤回した。** 8 通り試して測点が動かなかったのは事実だが、関連付け自体は成立しており、
  **`UpdateConstraintModel` を呼んでいなかった**だけ。確定した内容は下記
  「[寸法の関連付け](#寸法の関連付け図形が動いたら寸法も追うか)」
  （[#134 で測ったこと](../Metrics/Dimensions.md#associatelineardimension-が効かなかったのは解く段を呼んでいなかったから)）。

### `startOffset` の符号は図面の座標軸で決まる（測る向きには依らない）

- **`+` は水平な寸法なら上（`+y`）、垂直な寸法なら右（`+x`）。** `p1`→`p2` の向きを反転しても
  変わらない——「測る向きに対して左／右」という規則ではない。
- 反対側へ出したいときは **`startOffset` を負にする**（[実測](../Metrics/Dimensions.md#startoffset-の符号は図面の座標軸で決まる測る向きには依らない)）。
- `Vector2` は `WorldPt` の別名（`MathCoordTypes.h:329`）なので `Vector2(0, 0)` と書ける。
  作った寸法の型は **`dimHeaderNode = 63`**（`Objs.TDType.h:129`。`GetObjectTypeN` も 63）。

### 作った寸法はアクティブレイヤに入る

- **`CreateLinearDimension` は未挿入のハンドルを返さない。** 呼んだ時点で
  **アクティブレイヤ（`GetActiveLayer()`）の中に入っている**。注釈へ入れたいだけの寸法も
  いったんレイヤに置かれ、`AddViewportAnnotationObject` がそこから移す。
- **シートレイヤを作るとアクティブレイヤがそちらへ移る。** 入った先を確かめるときは
  **作る直前に `GetActiveLayer()` を取り直す**（[実測](../Metrics/Dimensions.md#作った寸法はアクティブレイヤに入る)）。

### 読み戻すときの型（`ovDimClass` は符号付き）

- **`ovDimClass` はヘッダに `unsigned char` とあるが、実体は `t_Sint8`（型番号 11）**で、
  `GetUint8` では読めない。`GetSint8` で読む。型は決め打たず、効いたものを使う。
- **`TVariableBlock` の setter は `operator=` だけ**（明示的な setter は `SetUint8` のみ。
  **`SetSint8` も `SetBoolean` も無い**）。`v = static_cast<Sint8>(index);` /
  `v = static_cast<Boolean>(1);` と書く（`-fsyntax-only` で踏んだ。型番号の一覧とコードは
  [計測記録](../Metrics/Dimensions.md#読み戻すときの型ovdimclass-は符号付き)）。

### 文字の大きさは作るときのアクティブレイヤの縮尺で焼き付く

- **寸法が自分で持っている大きさは `ovDimFontSize`(17)（図面上の mm）ただ 1 つ**で、
  `CreateLinearDimension` を呼んだ時点の**アクティブレイヤの縮尺**で決まる。6pt の設定で
  1/100 → `211.666`、1/50 → `105.833`、1:1 → `2.1166`（＝ 2.1167mm × 縮尺）。
- **【訂正】焼き付く素は規格ではなく、その寸法に当たっている文字スタイルの
  `ovTextStyleSize`（インチ）**（#157 / [#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)。
  下記「[規格に「文字の大きさ」は無い](#規格に文字の大きさは無い持っているのは文字スタイルへの参照-1-本でそれは寸法に届かない)」）。
- **後からレイヤの縮尺を変えても追随しない**（1/100 で作った寸法はレイヤを 1/50 にすると
  紙で 2 倍になる）。
- **`ovDimFontSize` は書ける**（`ResetObject`・規格の当て直し・`ovDimShowValue` の立て直しで
  巻き戻らない）。**大きさを直す手はこれ**——ただし絵へ出すには `ResetObject` が要る
  （下記「★」）。**例外は連続寸法の中の直線寸法**（by-class なら書いた値は残らない。下記
  「[〈クラスの文字スタイル〉のままなら…](#クラスの文字スタイルのままなら文字の大きさは繋ぐときのアクティブレイヤで決まる)」）。
- **焼き付くのは文字の大きさだけ**（[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)。
  すぐ下）。（[実測](../Metrics/Dimensions.md#文字の大きさは作るときのアクティブレイヤの縮尺で焼き付く)）

#### `ovDimTextSizeInPoints` は「読む時点のアクティブレイヤ」で割った値——当てにしない

- `ovDimTextSizeInPoints`(40) は**寸法が持っている値ではない**。読むたびに
  **`ovDimFontSize` ÷（そのときアクティブなレイヤの縮尺）** を pt に直したものが返る
  （容れ物の縮尺ですらない）。同じ 1 本がアクティブを替えただけで `6` → `300` になった。
- **書くときも同じ縮尺で解釈される**（1:1 がアクティブなときに `6` を書くと
  `ovDimFontSize` = `2.1167`。戻り値は `true` なので気付けない）。
- **判断にも指定にも `ovDimFontSize` を使う**: 紙で `P` pt なら
  `ovDimFontSize = P × 25.4 / 72 × （その寸法が描かれる場所の縮尺）`
  （[実測](../Metrics/Dimensions.md#ovdimtextsizeinpoints-は読む時点のアクティブレイヤで割った値当てにしない)）。

### 焼き付くのは文字の大きさだけ——規格の用紙インチは描くたびに解かれる

- **確定（[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)）。**
  補助線の長さと隙間・寸法線の出・端記号の大きさ・文字と寸法線の間隔は**寸法の中に写し
  取られていない**。描くたびに **① そのとき当たっている規格の用紙インチ** と **② そのとき
  入っている容れ物の縮尺**（レイヤ／ビューポート）から解き直される。**作ったときの縮尺は
  効かない。** `ovDimFontSize` だけが例外。

#### 測り方——文字を消した寸法の外接矩形で線の幾何を測る

- `ovDimShowValue = false` にした水平な寸法の外接矩形から出・補助線の出・隙間が割り出せる。
  **測る前に `ResetObject`**（作った直後の外接矩形は値の文字を含んでいる）。
- **端記号は外接矩形に入らず、`GetMarkerPolys` も寸法には「なし」を返す**——端記号の大きさを
  機械で測る道は無い（絵で確かめた）。
- **`SetLayerScaleN` が効いたことを読み戻して確かめる**（効かない文書が実在する。
  [Layers and Stories](Layers%20and%20Stories.md)「レイヤの縮尺」）。式と校正の詳細は
  [計測記録](../Metrics/Dimensions.md#測り方文字を消した寸法の外接矩形で線の幾何を測る)。

#### ① レイヤの縮尺を変えると、既にある寸法の線の幾何は追随する

- 補助線の出は「紙の 1.2mm × 縮尺」にぴたりと一致し、同じ 1 本の `ovDimFontSize` だけが
  取り残される（[実測](../Metrics/Dimensions.md#①-レイヤの縮尺を変えると既にある寸法の線の幾何は追随する)）。

#### ② 生まれの縮尺が違う 2 本は、同じ縮尺の下では完全に一致する

- 線の幾何に「生まれ」は残っていない（[実測](../Metrics/Dimensions.md#②-生まれの縮尺が違う-2-本は同じ縮尺の下では完全に一致する)）。

#### ③ 規格を差し替えると、既にある寸法の線の幾何がその場で変わる

- **線の幾何は規格そのものを引きに行っている**（`Arch` / `DIN` / `ISO` / `JIS` / `min-nano` で
  すべて「紙の値 × 50」。[実測](../Metrics/Dimensions.md#③-規格を差し替えると既にある寸法の線の幾何がその場で変わる)）。

#### ④ 注釈の中でも同じ——ビューポートの縮尺で解かれる

- **注釈の中の寸法の線は、生まれに依らずビューポートの縮尺で描かれる**（レイヤの縮尺にも
  依らない）。**`ovDimFontSize` を直しさえすれば、1:1 生まれの寸法と正しい縮尺で作った
  寸法は見分けが付かない**（絵で確認。端記号は絵だけが証拠。
  [実測](../Metrics/Dimensions.md#④-注釈の中でも同じビューポートの縮尺で解かれる)）。

#### 読み戻せる `ov*` は「作ったとき／規格を当てたときの縮尺」で固まる——当てにしない

- `ovDimTextAboveLineInCurrUnits`(43) と `ovDimCust*Wit*`(1236-1239) は、**作ったとき（または
  規格を当て直したとき）の縮尺で図面上の長さに直した値のまま固まる**。絵は縮尺に追随するのに
  これらは動かない——`ovDimTextSizeInPoints` と同じ罠。**判断は規格の用紙値（`dimStd*`）と
  容れ物の縮尺から自分で計算する**（[実測](../Metrics/Dimensions.md#読み戻せる-ov-は作ったとき規格を当てたときの縮尺で固まる当てにしない)）。

#### per-object の上書きは効く——単位は「図面上の長さ」

- 補助線だけ規格から外すなら `ovDimWitnessOverride`(1235) を立てる（`Sint16` で書ける）。
  **`ovDimCust*` の単位は図面上の長さ**で、紙で `W` mm なら `W × 容れ物の縮尺` を書く。
- **ふつうは要らない**——規格どおりなら縮尺は VW が面倒を見る
  （[実測](../Metrics/Dimensions.md#per-object-の上書きは効く単位は図面上の長さ)）。

#### 端記号の大きさは規格が `Sint16` で持つ（1/16384 インチ）

- `dimStdLinearASize`(22) は**マーカーと同じ 1/16384 インチの 16 ビット整数**として読むと辻褄が
  合う（`min-nano` `774` ＝ 1.2mm、`Arch` ほか `2048` ＝ 3.175mm。
  [Attributes and Classes](Attributes%20and%20Classes.md)）。`linearAWidth`(36) はどの規格でも `0`
  （[実測](../Metrics/Dimensions.md#端記号の大きさは規格が-sint16-で持つ116384-インチ)）。

## 寸法規格（dimension standard）

### index の体系

- **組み込みは 1〜9**（`Arch` / `ASME` / `BSI` / `DIN` / `ISO` / `JIS` / `SIA` /
  `ASME Dual SideBySide` / `ASME Dual Stacked` の順で固定）、**カスタムは 0 から下へ詰まる**
  （`0`〜`-8`。ヘッダ `APIBase.Legacy.Defs.h:5126` どおり）。
- **`0` は有効なカスタム規格の index**——`ovDimStandard` のヘッダの「zero is invalid」は実機と
  合わない（その図面では文書の既定 `GetProgramVariable(varDimStandard)` も `0` だった。
  `varDimStandard` は `short`）。
- **それでも名前で書くほうを勧める**（index の意味は図面ごとに変わる）。
- カスタム規格の本数は `ISDK::NumberCustomDimensionStandards()`。書けるのはカスタムだけ
  （`SetCustomDimensionStandardVariable`）。作る口は `ISDK::CreateCustomDimensionStandard(name)`
  （[総当たりの結果](../Metrics/Dimensions.md#index-の体系)）。

### 一覧の取り方

- **index を総当たりし、`GetDimensionStandardVariable(index, dimStdstandardName, v)` が
  `true` を返したものだけを並べる**（`dimStdstandardName = 25`、`MiniCadCallBacks.h:1833`、
  `GetTXString` で受ける）。**本数から index を引き算で出さない**（コードは
  [計測記録](../Metrics/Dimensions.md#一覧の取り方)）。
- 規格のその他の項目は `dimStd*`（`MiniCadCallBacks.h:1809` 以降）。**長さ系はすべて用紙インチ**
  （ヘッダの注記で `-2.0 〜 2.0`）。

### 寸法へ規格を当てる

- 寸法 1 本の規格は **`ovDimStandard`(0。`Sint8`。> 0 組み込み / < 0 カスタム)** と
  **`ovDimStandardName`(27。`TXString`)** のどちらからでも書ける。
- **index と名前は同じ値の 2 つの顔で、どちらを書いても他方が即座に追随する。
  `ResetObject` は要らない。**
- **存在しない名前は `SetObjectVariable` が `false` を返して元の値が残る**——名前の検証に
  そのまま使える。図面にある規格を名前で選ばせ、`ovDimStandardName` に書き戻すのが素直
  （[実測](../Metrics/Dimensions.md#寸法へ規格を当てる)）。

### 規格に「文字の大きさ」は無い——持っているのは文字スタイルへの参照 1 本で、それは寸法に届かない

**確定（[#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163)。
「できない」側の結論なので 2 回走らせ、ログは 1 行も違わなかった）。** 規格の側から文字の
大きさを決める道は無い。

#### ① セレクタに「文字の大きさ」が無い（【ヘッダ根拠】）

- `dimStd*` は **1〜52 で全部**（`Kernel/API/MiniCadCallBacks.h:1809-1860`）。文字に関わるのは
  `dimStdTextStyle`(51。`Sint32`。結び付いた文字スタイルの ref number)・
  `dimStdTextPos`(52)・公差用の `dimStdTolSizeFac`(26) だけ
  （[表](../Metrics/Dimensions.md#①-セレクタに文字の大きさが無いヘッダ根拠)）。

#### ② 規格の文字スタイルは書ける——カスタムだけ、組み込みは弾かれる

- `SetCustomDimensionStandardVariable(index, dimStdTextStyle, ref)` は**カスタムで `true`**、
  **組み込み（`JIS` 等）で `false`**（値も動かない）。書けたかは戻り値で判る。
- 結び付ける先は `CreateTextStyleResource` で作り `ovTextStyleSize`（インチ）を書く
  （[実測](../Metrics/Dimensions.md#②-規格の文字スタイルは書けるカスタムだけ組み込みは弾かれる)）。

#### ③ ところが、その規格を当てた寸法は 1 mm も動かない（これが答え）

- **規格を文書の既定にしてから作っても、後から `ovDimStandardName` で当てても、寸法の
  `GetTextStyleRef` は動かず `ovDimFontSize` も変わらない**（規格そのものは当たっている）。
  描かれた文字図形も同じ大きさで、読み戻しと絵は食い違っていない。
- 規格が指す文字スタイル資源の大きさを書き換えても、VW へ戻った後まで動かない（#245）。
- `GetTextStyleRef` が動かないのは、by-class の寸法が追うのが**寸法が入ったクラス**だから
  【推定】（#249。下記「[寸法はどのクラスへ入り…](#寸法はどのクラスへ入りgettextstyleref-はどのクラスを追うか249)」。
  [実測](../Metrics/Dimensions.md#③-ところがその規格を当てた寸法は-1-mm-も動かないこれが答え)）。

#### ④ 既にある連続寸法も追随しない——3 つの道すべて

- 規格が指す文字スタイルの大きさを変える／別の文字スタイルへ結び直す／中の直線寸法へ別の規格を
  当てる——**どれも `ResetObject` の後まで中の `ovDimFontSize` は動かない。** 連続寸法は作り直しの
  ときに規格を引きに行っていない。
- **中の直線寸法へ書いた `ovDimStandardName` そのものが `ResetObject` で捨てられる**
  （捨てられるのは大きさだけではない。[実測](../Metrics/Dimensions.md#④-既にある連続寸法も追随しない3-つの道すべて)）。

#### ⑤ では「繋ぐときに焼き付く値」の左の因子は何か——規格ではなく文字スタイル

> **`ovDimFontSize` ＝ その寸法に当たっている文字スタイルの `ovTextStyleSize`（インチ）
> × 25.4 × 繋ぐときのアクティブレイヤの縮尺**

- 規格はその文字スタイルの出どころの候補でしかなく、SDK から差し替えても寸法へは伝わらない
  （下記「[文字スタイルを当てる 2 つの口は…](#文字スタイルを当てる-2-つの口はovdimfontsize-の扱いが違う)」の式そのもの）。

#### ⑥ 逃げ道は 2 つある——どちらも規格ではない

アクティブレイヤを動かさず（1:1 がアクティブなままで）連続寸法の紙の大きさを決める道:

| 道 | やること | 根拠 |
| --- | --- | --- |
| **(a)** | **繋ぐ直前**に、繋ぐ 2 本へ `ovDimFontSize` ＝ 紙の pt × 25.4/72 × ビューポートの縮尺 を書く | #161（「[#143 の手順は正しかった](#143-のovdimfontsize-を書く手順は正しかった抜けていたのは-resetobject)」） |
| **(b)** | **繋ぐ前**に `SetTextStyleRef(dim, ref)` で文字スタイルを明示する（`ovTextStyleSize` ＝ 紙 pt ÷ 72 × ビューポートの縮尺） | #157（「[連続寸法へ繋いでも残る](#連続寸法へ繋いでも残る)」と「[連続寸法は中の `ovDimFontSize` で描かれる](#連続寸法は中の-ovdimfontsize-で描かれる-食い違いは恒久的ではない)」） |

- **注釈へ置くなら (b)**（手順は「[注釈へ寸法を置くときの作り方（文字スタイル版）](#注釈へ寸法を置くときの作り方文字スタイル版157-以降はこちら)」）。
- **「アクティブレイヤを合わせてから繋ぐ」（#155）が要るのは、(a) も (b) もせずに by-class の
  まま繋ぐときだけ**（[実測の出どころ](../Metrics/Dimensions.md#⑥-逃げ道は-2-つあるどちらも規格ではない)）。

#### 付け足し: `CreateCustomDimensionStandard` の実測（この調査で分かったこと）

- **作った規格は `0` が埋まっていれば `-1`、次は `-2` に入る**（`NumberCustomDimensionStandards()`
  は 1 つずつ増える）。戻り値は規格を表す `MCObjectHandle`（非 nil）。
- **作った直後の `dimStdTextStyle` はアクティブな規格のものを引き継ぐ**——自分の大きさで
  使いたいなら書き替える。**index は総当りで引く**（[実測](../Metrics/Dimensions.md#付け足し-createcustomdimensionstandard-の実測この調査で分かったこと)）。

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
- **その状態で `GetTextStyleRef` が返すのは、寸法が入っているクラスの文字スタイル**
  である。**寸法はアクティブクラスではなく「寸法」クラスへ入る**（文書の
  `varCreateDimsInDimClass`(44) が `1` のとき。既定のテンプレートでも空の図面でも `1`）
  ので、**アクティブクラスを書き換えても動かない**——#157 はそれを見て「クラスに
  追随しない」と書いたが、誤りだった（[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)。下記「作った直後は…」の訂正の記録）。
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
        gSDK->InternalIndexToNameN(static_cast<InternalIndex>(styleRef), styleName);
}
```

- **組み込み規格（1〜9）はどれも `0`（文字スタイル無し）。持つのはカスタム規格だけ**
  （利用者の図面では index `0` の `min-nano` が `30` ＝ `寸法(6pt)`。
  [実測](../Metrics/Dimensions.md#寸法規格が持つ文字スタイルを読む)）。
- 書くほうはカスタム規格だけ——`SetCustomDimensionStandardVariable(index, 51, block)`。

### `ovTextStyleSize` の単位はインチ（pt ではない）

- `CreateTextStyleResource` で作った直後は **`0.1667`**（= 1/6 インチ = 12pt）。`6` を書くと
  6 インチ = 432pt になる（当てた寸法の `ovDimTextSizeInPoints` が `432`）。
- **pt で指定したいなら `pt / 72` を書く**（6pt なら `0.08333`。
  [実測](../Metrics/Dimensions.md#ovtextstylesize-の単位はインチpt-ではない)）。

### 作った直後は〈クラスの文字スタイル〉——`GetTextStyleRef` は寸法が入ったクラスを見る（#249 で訂正）

- **作った直後の寸法は `GetTextStyleByClass` = `true`、`ovDimTextStyle` = `-2`**（クラス由来の
  番兵。ref number ではない）。**`GetTextStyleRef` はその寸法が入っているクラスの文字スタイル**
  （実効値）を返す——寸法クラスが文字スタイルを持つ図面での話。
- **【訂正】#157 は「`GetTextStyleRef` はクラスを見ていない」と結論したが誤り**
  （[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)）。
  #157 が書き換えたのはアクティブクラス `一般` で、寸法は**「寸法」クラス**にいた。その
  「寸法」クラスがもともと `寸法(6pt)`（`30`）を持っていたので、規格を覚えているように見えた
  （観察 1〜4 は [計測記録](../Metrics/Dimensions.md#作った直後はクラスの文字スタイルgettextstyleref-は寸法が入ったクラスを見る249-で訂正)）。
- **描かれた大きさは、クラスの文字スタイルを差し替えても VW へ戻った後まで変わらない**
  （[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）。番号は
  差し替えた直後から新しい値を返す。

### 寸法はどのクラスへ入り、`GetTextStyleRef` はどのクラスを追うか（#249）

**実機確認済み**（[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)。
テンプレートの図面と空の図面で比べた。[実測の表](../Metrics/Dimensions.md#寸法はどのクラスへ入りgettextstyleref-はどのクラスを追うか249)）。

- **寸法はアクティブクラスではなく「寸法」クラスへ入る**（`varCreateDimsInDimClass`(44)=`1`。
  既定のテンプレートでも空の図面でも `1`）。クラスの文字スタイルを当てたいなら
  **`GetObjectClass(dim)` のクラスを書き換える**。
- **by-class の寸法の `GetTextStyleRef` は、入っているクラスの `GetClTextStyleRef` と同じ値を
  その場で返す**（`SetObjectClass` で移せば移した先の値。`SetTextStyleByClass` は関係しない）。
- **ただし描かれた大きさは付いて来ない**（`ResetObject` を挟んでも）。**番号と絵は別。**
  **新しく作る寸法は、作った時点のクラスの文字スタイルの大きさで生まれる**
  （テンプレート 1/100 で 30pt → `1058.3333`）。
- **空の図面では、作った寸法は by-class ですらない**（`GetTextStyleByClass`=`false`・`ref=0`）。
  「寸法」クラスが文字スタイルを持っていないためと見ている【推定】。
- #163 の「規格を差し替えても `GetTextStyleRef` が `30` のまま」（上記
  「[④ 既にある連続寸法も追随しない](#④-既にある連続寸法も追随しない3-つの道すべて)」の上）も同じ仕掛けで
  読める【推定】（#163 の図面でクラスを読み直してはいない）。

### 文字スタイルを当てる 2 つの口は、`ovDimFontSize` の扱いが違う

- **`SetTextStyleRef` は当てた文字スタイルの大きさを `ovDimFontSize` へ焼き直す**（
  `SetTextStyleByClass` で戻すと元へ戻る）:

  > **`ovDimFontSize` ＝ `ovTextStyleSize`（インチ）× 25.4 ×（縮尺）**

  **その縮尺は容れ物のもの**——注釈の中の寸法へ掛けると、アクティブレイヤが 1:1 でも
  ビューポートの縮尺で焼かれる（[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)。
  分離できたのはこの 1 例だけで、ふつうは作る時点のアクティブレイヤと一致する）。
  同じ式が「作った直後」にも当てはまる——**焼き付く素は当たっている文字スタイルの大きさ**。
- **`ovDimTextStyle`(1248) へ番号を書く道は `ovDimFontSize` を動かさない**（結び付けだけ変わる）。
- **どちらの道でも、後から `ovDimFontSize` を書けばその値が残る。順番は「文字スタイル →
  `ovDimFontSize`」**（逆だと `SetTextStyleRef` が潰す。
  [実測](../Metrics/Dimensions.md#文字スタイルを当てる-2-つの口はovdimfontsize-の扱いが違う)）。

### ★ 絵の文字の大きさは、書いた瞬間には変わらない——引き直しで反映される（確定）

**[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161) の結論。
以下の #157 の節はこの法則の特別な場合なので、先にここを読む。**
`ovDimFontSize` へ書いた値は保持され、**その寸法が次に「引き直された」ときに初めて絵へ出る。**

| 書いたあとに通したもの | 絵に反映されるか |
| --- | --- |
| 何も通さない | **されない**（書く前の大きさのまま） |
| **`ResetObject(dim)`** | **される**（これが最短の 1 手） |
| `CreateChainDimension` で繋ぐ | される（繋ぎ直しが引き直しになる） |
| ビューポートの縮尺を変える | される（注釈の中身が引き直される） |
| `AddViewportAnnotationObject` / `UpdateViewport` | **されない**——移動も更新も引き直しではない |

- **`SetTextStyleRef` だけは例外で、呼んだその場で絵の側も書き換える。**
- **`ovDimFontSize` は効く**（紙 12pt を書いた行は紙 6pt の行のちょうど 2 倍）。**文字スタイルを
  外す必要は無い**（by-class のままで効く）。
- **読み戻しでは絶対に気付けない**——`ResetObject` を通した行と通さない行は読み戻し 4 つとも
  同じで、絵は片方しか出ない。**描かれている文字の大きさを測る**
  （[調査の作法「絵の文字の大きさを目視に頼らず測る」](Investigation%20Techniques.md)。
  7 行の実測は [計測記録](../Metrics/Dimensions.md#-絵の文字の大きさは書いた瞬間には変わらない引き直しで反映される確定)）。

#### 寸法から文字スタイルを外す口（`SetTextStyleRef(dim, 0)`）

- **`gSDK->SetTextStyleRef(dim, 0)` で外れる**（Un-Styled。`ResetObject` を越えて残る。
  `SDKLib/Include/vs.py` の説明どおり）。`ovDimTextStyle` ← `0` も同じ。`-1` を書く道・
  クラスを Un-Styled にして `SetTextStyleByClass` する道は外れない。
- **外しても絵は変わらない**——大きさを直したいだけなら外さずに `ovDimFontSize` を書いて
  `ResetObject`（[実測](../Metrics/Dimensions.md#寸法から文字スタイルを外す口settextstylerefdim-0)）。

### 注釈で値が出るのは `SetTextStyleRef` で明示したときだけ（← 誤り。原因は上記の引き直し）

- **【訂正】見出しの法則は成り立たない**（#161）。D1〜D6 の実測は正しいが、どの行も
  `ResetObject` を呼んでいなかった——**`ovDimFontSize` を書いて `ResetObject` すれば by-class の
  ままでも出る。**
- **`ovDimTextStyle`(1248) へ番号を書くだけの道は使ってはいけない**——番号は変わるが大きさが
  付いてこない（`SetTextStyleRef` との差は読み戻しでは分からない）。
- **当てる文字スタイルは何でもよい**（明示されていること自体が効く）。連続寸法でも同じで、
  繋ぐ前に中身へ `SetTextStyleRef` を呼んでおけば繋いだ後も注釈へ移した後も値が出る。
- デザインレイヤに置いた by-class の寸法は注釈を通しても値が出ていた
  （[D1〜D6 の表](../Metrics/Dimensions.md#注釈で値が出るのは-settextstyleref-で明示したときだけ-誤り原因は上記の引き直し)）。

### 文字スタイルを当てた後に `ovDimFontSize` を書いても、その場では絵が変わらない

- **【訂正】見出しはもと「大きさを決めるのは文字スタイルであって `ovDimFontSize` ではない」
  だった——誤り**（#161）。E1〜E3 が同じ大きさだったのは 3 本とも `ResetObject` を呼んで
  いなかったから。書いた値は残っていて、**`ResetObject` を呼べばその大きさで描き直される**。
- 注釈へ移しても値は動かない（[E 表](../Metrics/Dimensions.md#文字スタイルを当てた後に-ovdimfontsize-を書いてもその場では絵が変わらない)）。

### 紙で狙った大きさを出す——文字スタイルの大きさの決め方（確定）

> **`ovTextStyleSize`（インチ）＝ 紙の pt ÷ 72 × ビューポートの縮尺**

- **「6pt の文字スタイル」（`0.0833`）をそのまま当てても紙で 6pt にはならない**（1/125 で
  0.048pt）。文字スタイルの大きさは図面上の長さとして扱われ、縮尺で割られて紙に出る。
- 数は #143 の `ovDimFontSize` の式（`紙pt × 25.4/72 × 縮尺` mm）をインチに直したもの。
  **ビューポートの縮尺ごとに別の文字スタイルが要る**（F1〜F3。
  [実測](../Metrics/Dimensions.md#紙で狙った大きさを出す文字スタイルの大きさの決め方確定)）。

### 図面にある「紙の pt で名付けられた文字スタイル」をそのまま当てると縮尺で焼かれる（確定）

- **`寸法(6pt)` を `SetTextStyleRef` でそのまま当てると、絵の文字は「`0.0833` × 25.4 ×
  そのときのアクティブレイヤの縮尺」になる**（[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)。
  10 行を測って確定）。名前が紙の pt でも、紙の pt になるのは 1 つの縮尺のときだけ。
- **これが「軸組図（1/125）では値が読めないのに伏図（1/50）では読める」の正体。** 伏図は 1/50 が
  アクティブなうちに作っているので `105.833`mm ＝ 紙 6pt。断面の段取りはシートレイヤ（1:1）を
  先に作るので `2.1167`mm ＝ 1/125 の紙で 0.048pt。単独でも連続でも同じ。
- **症状は「値が描かれない」ではなく「描かれているが小さすぎて見えない」**
  （[実測](../Metrics/Dimensions.md#図面にある紙の-pt-で名付けられた文字スタイルをそのまま当てると縮尺で焼かれる確定)）。

#### 注釈の連続寸法で紙の pt を出す道は 3 つ（どれも実測で ★）

| 道 | やること | 連続（型 86） | 単独（型 63） |
| --- | --- | --- | --- |
| **(a)** | 文字スタイルを当てた**後に** `ovDimFontSize` ＝ 紙 pt × 25.4/72 × 縮尺 を書き、**引き直す** | ★（**繋ぐ前に書けばそれで済む**） | ★（`ResetObject` を 1 回） |
| **(b)** | **縮尺用の文字スタイルを作る**（`ovTextStyleSize` ＝ 紙 pt ÷ 72 × 縮尺）。`ovDimFontSize` は触らない | ★ | ★ |
| **(c)** | **ビューポートと同じ縮尺のレイヤをアクティブにして**から当てて繋ぐ | ★ | ★ |

- **図面にある文字スタイルを使い続けたいなら (a)**（文字スタイルを増やさずに済む）。
  **(c) は縮尺ごとにレイヤが要る**ので一般には (a) か (b)（切り替えは `ISDK::SetCurrentLayer`）。
- **(a) で書く相手は「繋ぐ前の直線寸法」。** 繋いだ後の型 86 の `ovDimFontSize` は読んでも意味が無い。
- **判定に使えるのは描かれている文字の大きさだけ**（読み戻し 4 つは引き直しの有無で変わらない。
  [行 B / C / C2 / E / F / G / I / J](../Metrics/Dimensions.md#注釈の連続寸法で紙の-pt-を出す道は-3-つどれも実測で-)）。

#### 既に注釈へ置いてしまった連続寸法を後から直す——通る道は 1 つだけ（確定）

- **`SetTextStyleRef` を連続寸法そのもの（型 86）へ掛け直し、`ResetObject` を呼ぶ。** これ以外は
  通らなかった（[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)）。
  OIP で選び直すと直るのも同じ仕組み（その寸法が注釈の中にあるから、ビューポートの縮尺で
  焼かれる）。**アクティブレイヤの縮尺は関係しない**（1:1 のままでも直る）。
- **中の型 63 へ掛けてはいけない**——掛けた直後は効くが、連続寸法を引き直した時点で捨てられる。
- **後から中へ `ovDimFontSize` を書く道は無い**（文字スタイルを明示してあっても捨てられる）。
  単独の直線寸法なら書けば効く。
- **掛け直しても読み戻し 3 つ（`GetTextStyleByClass` / `GetTextStyleRef` / `ovDimTextStyle`）は
  動かない**——直ったかは描かれている文字の大きさで確かめる。
- `ResetObject` を呼ばなくても、**VW はランループが回ると連続寸法を自分で作り直す**（#245。
  利用者の図面では揃う）。**同じ呼び出しの中で中の大きさを読むなら `ResetObject` が要る**。
- **この直しに頼らないほうがよい**——作るときに上の 3 つの道で作れば 1 手で済む
  （[実測](../Metrics/Dimensions.md#既に注釈へ置いてしまった連続寸法を後から直す通る道は-1-つだけ確定)）。

### 連続寸法は中の `ovDimFontSize` で描かれる（← 食い違いは恒久的ではない）

- **【訂正】見出しはもと「（直線寸法と食い違う）」**（#164）。直線寸法と連続寸法が食い違って
  見えたのは、2 巡目が直線寸法を引き直していなかったから。**文字スタイルを当てたまま
  `ovDimFontSize` を書いて `ResetObject` を通せば、単独の直線寸法も書いた値で描かれる。**
  「だから `ovDimFontSize` は触らない」という結論も要らない——触るなら引き直す。
- **連続寸法（型 86）は中の直線寸法の `ovDimFontSize` で描かれる**
  （[2 巡目・3 巡目の表](../Metrics/Dimensions.md#連続寸法は中の-ovdimfontsize-で描かれる-食い違いは恒久的ではない)）。

### 連続寸法へ繋いでも残る

- **繋ぐ前に直線寸法へ `SetTextStyleRef` を当てておけば、中の型 63 は `ovDimTextStyle` も
  `ovDimFontSize` も保つ。** 〈クラスの文字スタイル〉のままだと繋いだ時点で捨てられる（下記
  「[〈クラスの文字スタイル〉のままなら…](#クラスの文字スタイルのままなら文字の大きさは繋ぐときのアクティブレイヤで決まる)」）。
- 連続寸法そのもの（型 86）も `GetTextStyleByClass` / `GetTextStyleRef` は読めるが、
  `ovDimTextStyle` は `GetObjectVariable` が `false`。
- **`ISDK::SetPIOTextStyle(chain, 113, true)` が効くかは【未確認】**（既に `113` が入っていた）。
  繋ぐ前に当てる道が通っているので要らない（[実測](../Metrics/Dimensions.md#連続寸法へ繋いでも残る)）。

### 注釈へ移しても変わらない

- **`AddViewportAnnotationObject` は文字スタイルにも `ovDimFontSize` にも触らない**（6 本すべて
  `true`、読み戻し 4 つが移す前と同じ。[実測](../Metrics/Dimensions.md#注釈へ移しても変わらない)）。

### 注釈へ寸法を置くときの作り方（文字スタイル版。#157 以降はこちら）

**この手順と「[注釈へ寸法を置くときの作り方](#注釈へ寸法を置くときの作り方ここだけ読めばよい)」
（`ovDimFontSize` を書く版）は、どちらも正しい**（#161）。選び方:

| | 文字スタイル版（この節） | `ovDimFontSize` 版（#143） |
| --- | --- | --- |
| 呼ぶもの | `CreateTextStyleResource` ＋ `SetTextStyleRef` | `SetObjectVariable(ovDimFontSize)` ＋ **`ResetObject`** |
| 図面に増えるもの | **文字スタイルが縮尺ごとに 1 つ** | 増えない |
| 引き直し | 要らない（`SetTextStyleRef` がその場で絵を書き換える） | **`ResetObject` が要る**（忘れると絵が変わらない） |
| 向くところ | 図面の中で大きさを揃えたい・利用者が後から触る | 1 本ずつ大きさを決めたい・図面を汚したくない |

```cpp
// vpScale は ovViewportScale(1003) から読む（1/125 なら 125）。
MCObjectHandle style = gSDK->CreateTextStyleResource("注釈 6pt 1-125");
TVariableBlock sizeVar;
sizeVar = static_cast<Real64>(6.0 / 72.0 * vpScale);   // インチ。紙で 6pt
gSDK->SetObjectVariable(style, ovTextStyleSize, sizeVar);
gSDK->SetTextStyleRef(dim, gSDK->GetObjectInternalIndex(style));
gSDK->AddViewportAnnotationObject(viewport, dim);      // 連続寸法なら 2 本済ませてから繋ぐ
```

- **文字スタイルはビューポートの縮尺ごとに要る**（名前に縮尺を入れる）。既にある文字スタイルを
  使い回すなら `ovTextStyleSize` を読んで確かめる。
- **規格の文字スタイル（`寸法(6pt)` など）をそのまま使い続けたいなら、「当てた後に
  `ovDimFontSize` を書いて引き直す」道を使う**（連続寸法なら繋ぐ前に書けば `ResetObject` は
  要らない。[#164](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/164)）。
  省略の無いコードは [計測記録](../Metrics/Dimensions.md#注釈へ寸法を置くときの作り方文字スタイル版157-以降はこちら)。

### #143 の「`ovDimFontSize` を書く」手順は正しかった——抜けていたのは `ResetObject`

- **[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161) で決着。**
  #157 の 3 回の実行（D1 / E5 / F5）で
  [#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143) が再現
  しなかったのは、**`ovDimFontSize` を書いたあと寸法を引き直していなかった**から。
- 当初の見立て「文字スタイルが当たっているときだけ効かない」は**反証された**——外しても外さなくても
  同じで、効くかどうかは `ResetObject` を通したかだけで決まった
  （[実測](../Metrics/Dimensions.md#143-のovdimfontsize-を書く手順は正しかった抜けていたのは-resetobject)）。

## 連続寸法（チェーン寸法）

- **`ISDK::CreateChainDimension(h1, h2)` で作る。** まず直線寸法を 1 本ずつ作り、2 本ずつ繋ぐ。
  端点を共有する同じ向きの 2 本は 1 つの連続寸法になり、**元の 2 本はレイヤ直下から消えて
  中へ取り込まれる**（中身は `11`（2D 表現のグループ）・`63`・`63`・`0`）。
- 連続寸法は**プラグインオブジェクト**（`kInternalID_NNA_ChainDim = 222`。`GetObjectTypeN` は
  **86 = `kParametricNode`**）。
- **連続寸法そのものには寸法のオブジェクト変数が効かない**（`ovDimStandard` /
  `ovDimStandardName` は `false`）。**規格は繋ぐ前の直線寸法へ当てる。** **`ovDimFontSize` だけは
  読めて書けてしまうが中には効かない**——戻り値で「入った」と読み違えない。
- **レイヤ直下の件数で判断しない**（`90` ＝ `kUndoPlaceholderNode` の置き石が増えて、図形が
  増えたように見える。[実測](../Metrics/Dimensions.md#連続寸法チェーン寸法)）。

### 〈クラスの文字スタイル〉のままなら、文字の大きさは「繋ぐときのアクティブレイヤ」で決まる

**確定（[#155](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/155)。
4 回走らせた）。** 文字スタイルを明示していない（by-class の）寸法の話で、#157 の「明示して
あれば保つ」とは矛盾しない。

- **`CreateChainDimension` は中の直線寸法を作り直す**（手は別物になり、変わる欄は `ov17`
  ＝`ovDimFontSize` と `ov40` ＝`ovDimTextSizeInPoints` だけ）。
- **入る値は「by-class で解決される文字スタイルの大きさ × 繋ぐときのアクティブレイヤの縮尺」**。
  元の寸法をどこで作ったかは効かない（双方向で入れ替えて測った）。1/50 で繋げば `105.833`、
  1:1 で繋げば `2.11667`（1/50 の紙で 0.12pt）。
- **【訂正】左の因子は規格のものではない**（#155 当時は「規格が紙で決めた大きさ」と書いていた。
  [#163](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/163) が規格と
  文字スタイルを別々にして否定した）。**「規格を変えれば大きさが変わる」とは読まない。**
- **シートレイヤは `CreateLayer(..., kLayerSheet)` でアクティブになる**ので、断面の段取りで
  先に作ると、そこで繋いだ連続寸法は必ず 1:1 の大きさになる。
- **後から直す道は潰れている**——繋ぐ前／後に中へ書く・型 86 へ書く・注釈へ移す・注釈の中で
  `ResetObject`・アクティブを合わせてから `ResetObject`、どれも効かない（**文字スタイルを明示して
  あっても同じ**。#164）。**通るのは型 86 へ `SetTextStyleRef` を掛け直すことだけ**（上記
  「[既に注釈へ置いてしまった連続寸法を後から直す](#既に注釈へ置いてしまった連続寸法を後から直す通る道は-1-つだけ確定)」）。
- **中の値が後から動くのはビューポートの縮尺を変えたときだけ**（比例で書き換わる）。
  「中へ書く ＋ 縮尺の往復」で絵を合わせる手は**手順にしてはいけない**。
- 対照: **単独の直線寸法は書いて `ResetObject` すれば効く**（[表と潰した道](../Metrics/Dimensions.md#クラスの文字スタイルのままなら文字の大きさは繋ぐときのアクティブレイヤで決まる)）。

#### by-class の連続寸法で狙った大きさを出す手順

- **デザインレイヤへ by-class のまま置く連続寸法**の話（注釈へ置くなら文字スタイル版の手順）。
- **出したい縮尺のレイヤをアクティブにしてから `CreateChainDimension` を呼ぶ。** 文字の大きさは
  書かない（書いても捨てられる）。元の寸法はどこで作ってもよい。
  切り替えは `ISDK::SetCurrentLayer`（下記の【訂正】。手順のコードは
  [計測記録](../Metrics/Dimensions.md#by-class-の連続寸法で狙った大きさを出す手順)）。
- **寸法規格の紙の pt を変える道は無い**（#163 で実測確定。#155 の時点の【推定】は否定された）。
  アクティブレイヤを合わせずに済ませたいなら、**繋ぐ直前に `ovDimFontSize` を書く**（#161）か
  **繋ぐ前に `SetTextStyleRef` で文字スタイルを明示する**（#157）。

#### 中身を読むときの注意（測り方）

- **中の 63 の `ovDimFontSize` は絵に出ている大きさとは限らない**——絵は中の文字図形（型 `10`
  ＝ `kTextNode`）を `GetTextSize` で測るか、外接矩形の高さで見る。
- **2D 表現のグループ（型 `11`）の中の写しは作り直しでも古いまま**なので、読むなら連続寸法の
  直下の `63`。**作り直しのたびに中の `63` の手は変わる**——そのつど `FirstMemberObj` から引き直す
  （[実測](../Metrics/Dimensions.md#中身を読むときの注意測り方)）。

### `FirstMemberObj` / `NextObject` の walk は終端も数える

- **リストの末尾に `kTermNode`（型 `0`）が 1 つ入っている。** 素直に数えると 1 多く出る。
  件数を使うなら終端を除くか、**型の並び**を見る。

## 寸法の関連付け（図形が動いたら寸法も追うか）

[#138](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/138) の調査。
**答えは「効く」**——[#134](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/134)
の 8 通りが空振りだったのは、制約を解く段を呼んでいなかっただけ。

### 関連付けは「パラメトリック制約」の仕組みの一部である

- 寸法の関連付けは SDK の**パラメトリック制約**の一部で、図形にぶら下がる**制約ノード**
  （`kConstraintNode = 110`。`Objs.TDType.h:176`）として持たれる。`ISDK` には張る
  （`AssociateLinearDimension` / `SetBinaryConstraint` / `SetSingularConstraint`）・読む
  （`HasConstraint` / `GetBinaryConstraint`）・**解く**（`CreateConstraintModel` /
  `AddToConstraintModel` / `UpdateConstraintModel`）口がある（宣言と説明はヘッダの写し。
  仕組みは下記のとおり実機で確かめた。表は [計測記録](../Metrics/Dimensions.md#関連付けはパラメトリック制約の仕組みの一部である)）。
- **要点は `UpdateConstraintModel` の説明**: 制約は図形を動かせば勝手に解かれるものではなく、
  **呼び出し側が「動かす前に模型を作り、動かした後に解く」段を踏む**。解けなければ `false` を
  返し、そのときは undo を呼ぶべき（ヘッダ）。

### こう呼べば効く——関連付けは成立していて、足りなかったのは「解く段」だった

```cpp
gSDK->AssociateLinearDimension(dim, false);  // ① h は寸法。false＝全図形を調べる（選択は要らない）
gSDK->DeselectAll();
gSDK->SelectObject(line, true);
gSDK->CreateConstraintModel(nil, true);      // ② 動かす前に模型を作る
gSDK->MoveObject(line, 1200, 0);
gSDK->UpdateConstraintModel();               // ③ 解く。呼ばないと測点は 1mm も動かない
```

- **`MoveObject` ＋ `ResetObject` だけでは追わない**（VW へ戻った後も 0 のまま。#245）。
  `BuildConstraintModelForObject` → 動かす → `RecordModifiedObjectInConstraintModel` →
  `UpdateConstraintModel` でも追う。
- **`CreateConstraintModel(線分, false)` ＋ `AddToConstraintModel(寸法)` の経路は使ってはいけない**
  ——確認ダイアログが出て拘束が消える（下記）。
- `selectedObjectsMode` は `true` でも `false` でも同じ。`dimType` も関与しない
  （[上記](#0fix_angと-1slopedに差は無い角度は-ovdimdirection-が決める)）。`UpdateConstraintModel` は
  4 通りとも `true`（[実測](../Metrics/Dimensions.md#こう呼べば効く関連付けは成立していて足りなかったのは解く段だった)）。

#### 関連付くのは表示範囲の中だけ（#249 で確定）

- **`AssociateLinearDimension` は、いま画面に見えている範囲の外にある寸法を黙って関連付けない**
  （`HasConstraint`=no。[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)）。
  図面の出どころ・用紙・レイヤ数・`varCreateDimsInDimClass`・選択の有無は効かない。縮尺を上げるか
  縮小表示にすると範囲が広がる。境目の正確な形は測っていない。
- **確実に関連付けたいなら、`GetViewCenter` で控え → `SetViewCenter(寸法の中ほど)` →
  `AssociateLinearDimension` → `SetViewCenter(控え)`。** 寄せたときだけ効く（`SetZoomFactorN` で
  縮小する手は利用者の表示倍率を変えてしまう）。
- ツールの自動関連付けの「見えているものに吸着する」判定を流用しているのだろう【推定】
  （[実測](../Metrics/Dimensions.md#関連付くのは表示範囲の中だけ249-で確定)）。

#### 関連付いたかは `HasConstraint` で読める

- `AssociateLinearDimension` は `void` だが、**`HasConstraint(寸法)` で成否が読める**
  （`FindAuxObject(寸法, 110)` と常に一致。最初から付いている `[76]` は無関係なので型 `110` で見る）。
  `GetDimensionsAssociatedToPlugin` は引数が PIO で、一般の図形には使えない
  （[実測](../Metrics/Dimensions.md#関連付いたかは-hasconstraint-で読める)）。

#### 関連付けの正体は「同一点上（coincident）」の拘束——VW 自身がそう名乗る

- 拘束と両立しない編集をすると VW は「**関連する拘束を削除しますか？**」を出し、一覧には
  **タイプ「同一点上」／カテゴリ「寸法」** が並ぶ（`SetBinaryConstraint` の `type = 1`）。
- 「はい」で拘束が消え（補助オブジェクトの型が `[90]`）、追従しなくなる。
  **このダイアログを抑止する環境設定は SDK に無い**（`ProgramVariables.h` を当たった）。

#### 門は `varAssociateDims`(28) ただ 1 つ——`varAutoAssociateDims`(134) は関与しない

- **`varAssociateDims`(28)（`ProgramVariables.h:43`）が `0` だと `AssociateLinearDimension` は何も
  しない。** `varAutoAssociateDims`(134)（`ProgramVariables.h:162`）は結果を変えない（4 通り総当たり）。
  どちらも Boolean selector で `GetProgramVariable` / `SetProgramVariable`。
- **新規図面の走り出しは `28=1` / `134=0`**——ふつうは読んで確かめるだけでよく、切られていたら
  書き換えるより「関連付けができない」と伝えるほうが筋が通る。
- 画面の拘束マークも `HasConstraint` の読みと一致する
  （[実測](../Metrics/Dimensions.md#門は-varassociatedims28-ただ-1-つvarautoassociatedims134-は関与しない)）。

#### 通らなかった道

- **`SetBinaryConstraint` で自分で coincident を張れない**——`GetClosestPt` が寸法に `-1`（型が
  非対応）を返す。関連付けを張る口は `AssociateLinearDimension` の 1 本だけ。
- **`SetHorizontalDimensionConstraint` は寸法を作らない**（図形自身に寸法拘束を掛ける口。
  [実測](../Metrics/Dimensions.md#通らなかった道)）。

#### 画面でドラッグしたときは、VW が自分で解く——何もしなくても追う

- **利用者が画面で図形を動かすと、寸法の測点はそのまま追う**（動かした端だけが追う）。
  **追従の引き金は、利用者なら要らない／プラグインなら `CreateConstraintModel` → 動かす →
  `UpdateConstraintModel` を自分で踏む**（[実測](../Metrics/Dimensions.md#画面でドラッグしたときはvw-が自分で解く何もしなくても追う)）。

#### 実装への含意——関連付けておけば、作り直さなくてよい

- 寸法を入れるときに `AssociateLinearDimension` を呼んでおけば、利用者が図形を動かしても寸法は
  自分で追う。**#134 の「図形が動いたら寸法を作り直す前提でよい」は不要になった。**
  作り直しが要るのは、関連付けが張れない相手（測点と一致する頂点を持たない図形）か、拘束が
  壊れたときだけ。

## 寸法をビューポートの注釈へ入れる

- 移し方は `ISDK::AddViewportAnnotationObject(viewport, annotation)`
  （[Data Tags](Data%20Tags.md) / [Viewports](Viewports.md) と同じ）。`true` を返し、移した後も
  **型 63 のまま**でオブジェクト変数も読める。
- **外接矩形も座標も移す前後で変わらない**（注釈空間の座標は `CreateLinearDimension` に渡した
  座標そのまま）。**移ったことは「元いたレイヤから消えた」で確かめる**（アクティブレイヤを
  取り違えない）。平面ビューポートの `GetObjectTypeN` は **122**。
- **注釈に寸法を置くのは想定された使い方**（`ovDimTranslateInVP`(1234) のヘッダに exterior wall
  dimensioner の記述がある。内部用なので使わない）。
- **注釈へ後から足した図形のクラスはビューポートで非表示のまま**——足した後に全クラスを表示へ
  戻して再更新する（[Viewports](Viewports.md)。[実測](../Metrics/Dimensions.md#寸法をビューポートの注釈へ入れる)）。

### 注釈へ寸法を置くだけでは見えない——ビューポートの作法を先に踏む

**ビューポートの作法（[Viewports](Viewports.md)）を踏まないと、断面も寸法も描かれず 53.3mm 角の
「×」印の空枠のまま**になる。寸法を注釈へ置くときも同じ手順が要る:

1. **レンダリングを隠線消去にする**（`ovViewportRenderType`(1001) ← `renderFinalHiddenLine` = 6）。
   **これが先**——シェイドのままでは `ovViewportDisplay2DComponents`(1059) が入らない。
2. **クラスをすべて表示へ戻す**（`ForEachClass(true, …)` ＋
   `SetViewportClassVisibility(vp, GetObjectInternalIndex(cls), 0)`）。
3. 断面の表示の作法（`ovSectionViewportDisplayObjectsBeyondCutPlane`(1064) ＋
   **`ovSectionViewportDisplayObjectsBeforeCutPlane`(1065)** / `ovViewportDisplayPlanar`(1035) /
   `ovViewportDisplay2DComponents`(1059)）を**すべて更新より前に**設定する。**1064 だけでは
   足りない**（[Viewports](Viewports.md)「断面に中身が入る条件」。#202）。
4. 表示レイヤを表示にする（`SetViewportLayerVisibility`）。
5. **注釈へ寸法を足した後、もう一度クラスを全部表示へ戻して再更新する。**

- **「寸法が出ない」と判断する前に、ビューポートに何か 1 つでも描かれているかを見る**
  （[実測](../Metrics/Dimensions.md#注釈へ寸法を置くだけでは見えないビューポートの作法を先に踏む)）。

### ビューポートの外接矩形——53.3mm 角の空枠より小さいものは隠れる

- **【訂正】以前は「ビューポートの外接矩形は中身を勘定に入れない」と書いていた**
  （[#200](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/200)）。正しくは
  **注釈が外接に入るのは `ResetObject` を通した後だけ**で、**空枠（53.3mm 角 ＝ ±26.649）より
  大きいものだけが外接に現れる**（空枠は中身に置き換わる。数値は [Viewports](Viewports.md)
  「`GetObjectBounds(viewport)` が何で決まるか」）。
- **「外接矩形が変わらない＝何も描かれていない」と読んではいけない。** 描かれたかを機械で
  確かめるなら `GetViewportGroup(vp, 3 / 5 / 6 / 7 / 15)` のキャッシュ群を数える（群 4 は空でも在る。
  [実測](../Metrics/Dimensions.md#ビューポートの外接矩形533mm-角の空枠より小さいものは隠れる)）。

### 断面ビューポートの注釈空間の座標——横の原点は断面線の「終点」

- **横 = （断面線に沿った位置）−（断面線の終点）。** 原点は断面線の**終点**で、始点側へ負になる。
  始点基準だと思うと断面線の長さぶんずれる。**縦 = Z。** 視線の右手が模型の `+x`
  （断面線 `+x` 向き・`+y` を見る配置）。置き直して壁にぴったり掛かったので**式は確定**
  （[実測](../Metrics/Dimensions.md#断面ビューポートの注釈空間の座標横の原点は断面線の終点)）。

### 注釈はビューポートの縮尺で描かれる（用紙 1:1 ではない）

- **【訂正】以前は「注釈空間は用紙 1:1」と書いていた（#132）——誤り**
  （[#143](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/143)）。
- **注釈の中身はそのビューポートの縮尺で描かれる。** 注釈へ置く寸法は、ビューポートの縮尺で
  割り戻した `ovDimFontSize` を持たないと紙で正しい大きさにならない（1/50 の注釈で、1/50 生まれ
  `105.833` は値が出て、1:1 生まれ `2.1166` は寸法線だけで値が出ない。絵で確認）。
- **これが「軸組図では寸法の値が出ない」の正体**——断面だからではなく、シートレイヤ（1:1）が
  アクティブなときに作ったから。**平面と断面で扱いに差は無い。**
- **`AddViewportAnnotationObject` は `ovDimFontSize` を変えない。** **ビューポートの縮尺を変えると
  注釈の中身の `ovDimFontSize` が比例して書き換わる**（1/50 → 1/100 で `105.833` → `211.666`）
  ——一度正しく作れば後で縮尺を変えても崩れない。
- **注釈群からは縮尺を読めない**（`GetLayerScaleN` は `1`）。**縮尺は `ovViewportScale`(1003)
  から読む。** **`CreateViewport` / `CreateSectionViewport` の縮尺の既定は `1:1`**——自分で書く
  （[実測](../Metrics/Dimensions.md#注釈はビューポートの縮尺で描かれる用紙-11-ではない)）。

### 注釈へ寸法を置くときの作り方（ここだけ読めばよい）

> **【この手順には `ResetObject` が 1 行足りなかった】** #157 で一時「文字スタイルがあると効かない
> ので使うな」と書いたが、[#161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)
> が取り消した。手順 4 の `ResetObject` を足して**そのまま使ってよい。** 文字スタイル版
> 「[注釈へ寸法を置くときの作り方（文字スタイル版）](#注釈へ寸法を置くときの作り方文字スタイル版157-以降はこちら)」
> も正しいので、あちらの表を見て選ぶ。

紙で 6pt に見せたい寸法を、ビューポートの注釈へ置く手順（`vpScale` は `ovViewportScale` から読む）:

```cpp
MCObjectHandle dim = gSDK->CreateLinearDimension(p1, p2, startOffset, 0, Vector2(0, 0), 0);
TVariableBlock fontSize;
fontSize = static_cast<Real64>(6.0 * 25.4 / 72.0 * vpScale);   // 3. 紙で 6pt
gSDK->SetObjectVariable(dim, ovDimFontSize, fontSize);
gSDK->ResetObject(dim);              // 4. 書いたら引き直す（連続寸法へ繋ぐなら繋ぐことが引き直し）
gSDK->AddViewportAnnotationObject(viewport, dim);
```

- **この道を勧める。** 作るときのアクティブレイヤが何であっても結果が同じなので、伏図の
  デザインレイヤと断面ビューポートの縮尺が違っていても通る（#143 の 4 番目の問い）。
- 「ビューポートと同じ縮尺のレイヤをアクティブにしてから作る」道でも同じ結果になるが、縮尺ごとに
  レイヤが要るので書く道のほうが一般的。
  > **【訂正】`ISDK` にアクティブレイヤを切り替える口は無い、は誤り。**
  > **`ISDK::SetCurrentLayer(MCObjectHandle)`**（`ISDK.h:1394`）と
  > **`SetCurrentLayerN(MCObjectHandle, Boolean bAllowUnifiedViewMaintenance = false)`**
  > （`ISDK.h:2761`）があり、いまのアクティブは **`ISDK::GetActiveLayer()`**（`ISDK.h:2020`）。
  > [#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175) で効くことを確かめた。
- **`ovDimTextSizeInPoints` へ書く道は勧めない**（読む時点のアクティブレイヤの縮尺で解釈される）。
- **直すのは注釈へ移す前でも後でもよい。どちらでも書いたあとに `ResetObject` が要る**
  （`AddViewportAnnotationObject` も `UpdateViewport` も引き直しではない）。
- **書くのは `ovDimFontSize`（と `ResetObject`）だけでよい**——補助線・出・端記号・間隔は
  ビューポートの縮尺で描くたびに解き直される（[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)）。
  1:1 生まれに書いた 1 本と 1/50 生まれの 1 本は注釈で見分けが付かなかった。
- **後からビューポートの縮尺を変えても崩れない**——一度正しく作れば、以後は触らなくてよい
  （省略の無いコードは [計測記録](../Metrics/Dimensions.md#注釈へ寸法を置くときの作り方ここだけ読めばよい)）。
