# 描画属性とクラス

オブジェクトの描画属性（ペン色・面色・線の太さ・線種・面パターン・マーカー・不透明度）を
「クラスに従わせる」ための口と、その**費用**。

## 「クラスに従わせる」口は per-object と「文書の既定」の 2 段で揃っている【ヘッダ根拠】

`ISDK` には、**ハンドルを取る per-object 版**と、**ハンドルを取らない「文書の既定」版**が
同じ顔ぶれで揃っている。読み戻し（`Get*`）も両段にある。

| 属性 | per-object | 文書の既定 |
| --- | --- | --- |
| クラスそのもの | `SetObjectClass(h, id)` / `GetObjectClass(h)` | `SetDefaultClass(id)` / `GetDefaultClass()` |
| ペン色 | `SetPColorsByClass(h)` / `GetPColorsByClass(h)` | `SetDefaultPColorsByClass()` / `GetDefaultPColorsByClass()` |
| 面色 | `SetFColorsByClass(h)` / `GetFColorsByClass(h)` | `SetDefaultFColorsByClass()` / `GetDefaultFColorsByClass()` |
| 線の太さ | `SetLWByClass(h)` / `GetLWByClass(h)` | `SetDefaultLWByClass()` / `GetDefaultLWByClass()` |
| 線種 | `SetPPatByClass(h)` / `GetPPatByClass(h)` | `SetDefaultPPatByClass()` / `GetDefaultPPatByClass()` |
| 面パターン | `SetFPatByClass(h)` / `GetFPatByClass(h)` | `SetDefaultFPatByClass()` / `GetDefaultFPatByClass()` |
| マーカー | `SetArrowByClass(h)` / `GetArrowByClass(h)` | `SetDefaultArrowByClass()` / `GetDefaultArrowByClass()` |
| 不透明度 | `SetOpacityByClass(h)` / `GetOpacityByClass(h)` | `SetDefaultOpacityByClass()` / `GetDefaultOpacityByClass()` |

- **属性パレットの「クラスの属性を使用」に当たるのはこの 7 つ**で、
  **テキストスタイルとドロップシャドウは別口**（`SetTextStyleByClass(h)` /
  `IsDropShadowByClass(h)`）。7 つを呼んでも文字には掛からない
  （[Data Tags](Data%20Tags.md) の「描画属性はクラス属性に従わせる場合…」）。
- **不透明度にはペンと面の 2 旗がある**（`SetOpacityByClassN(h, pen, fill)` /
  `GetOpacityByClassN(h, pen&, fill&)`、既定にも `SetDefaultOpacityByClassN` /
  `GetDefaultOpacityByClassN`）。読み戻しでは**こちらを使う**——下記の穴があるため。

### `GetDefaultOpacityByClass()`（1 旗版）は読み戻しに使えない

`SetDefaultOpacityByClass()` を呼んだ**後**も `GetDefaultOpacityByClass()` は `false` を
返す。一方、同じ状態で 2 旗版 `GetDefaultOpacityByClassN` は `pen=true fill=true` を返し、
**その後に作ったオブジェクトは実際に 20/20 が不透明度 by-class** だった。つまり
**書き込みは効いており、1 旗版の読み戻しだけが嘘をつく**。既定の不透明度を確かめるなら
`GetDefaultOpacityByClassN` を使う（per-object の `GetOpacityByClass(h)` は正しく返す）。

**同じ形の穴がマーカーにもある**（そちらは読みだけでなく書き込みも効かない）。
一覧は下記「読み戻しに使えない口（ここまでに分かっているもの）」。

## 全属性をまとめて by-class にする ISDK の口は無い【ソース根拠】

- `SetAllAttributesByClass` も `AllAttr` も、**SDK 全体（ヘッダ・同梱の実装ソース・
  `vs.py`）に 1 件も無い**。`SetClassByName` も無い（どちらも VectorScript 風の名前で、
  `ISDK` の実名は `SetObjectClass` と上表の 7 つ）。
- いちばん近いのは `VWFC::Tools::VWAutoClassing::SetAttrsByClass(MCObjectHandle)` だが、
  実装は**7 つをそのまま順に呼ぶだけ**で、カーネルへの書き込み回数は 1 つも減らない
  （`SDKLib/Source/VWSDK/VWFC/Tools/AutoClassing.cpp:344`）:

  ```cpp
  /*static*/ void VWAutoClassing::SetAttrsByClass(MCObjectHandle hObject)
  {
      if ( hObject ) {
          VWObjectAttr objAttr( hObject );
          objAttr.SetFillColorByClass();   objAttr.SetFillPatternByClass();
          objAttr.SetPenPatternByClass();  objAttr.SetPenColorByClass();
          objAttr.SetLineWeightByClass();  objAttr.SetArrowByClass();
          objAttr.SetOpacityByClass();
      }
  }
  ```

**つまり「1 回で済ませる」道は無い。**減らせるとすれば「呼ばずに済ませる」道だけで、
それが次節。

## 費用は「属性の書き込み」ではなく「それが起こす PIO の作り直し」に掛かる

**by-class の書き込みそのものは無料である。**高いのは**PIO を作り直させたときだけ**。

実測（VW 2026 / mac / 新規の空図面。`probes/runtime/byclass-attr-cost/`。
構造材 PIO と矩形を各 20 個、1 呼び出しあたりの平均）:

| 呼び出し | 構造材 PIO | 矩形（2D） | 球（3D ソリッド） |
| --- | ---: | ---: | ---: |
| `SetObjectClass` | **9.27ms** | 0.00ms | 0.00ms |
| `SetPColorsByClass`（ペン色） | **9.04ms** | 0.00ms | 0.00ms |
| `SetFColorsByClass`（面色） | **9.18ms** | 0.00ms | 0.00ms |
| `SetLWByClass`（線の太さ） | **8.82ms** | 0.00ms | 0.00ms |
| `SetPPatByClass`（線種） | **8.99ms** | 0.00ms | 0.00ms |
| `SetFPatByClass`（面パターン） | **9.03ms** | 0.00ms | 0.00ms |
| `SetOpacityByClass`（不透明度） | **8.92ms** | 0.00ms | 0.00ms |
| **`SetArrowByClass`（マーカー）** | **0.00ms** | 0.00ms | 0.00ms |
| （参考）作る | 14.56ms/個 | 0.01ms/個 | 0.39ms/個 |

読みどころが 4 つある。

1. **効くのは「PIO かどうか」であって「実体を持つかどうか」ではない。**
   **球は 3D ソリッドを持つのに 7 つとも無料**だった。取り込み側の内訳（構造材 PIO で
   10ms、データタグで 0.3ms）も、「実体の重さ」ではなく「作り直しの重さ」の差である。
2. **`SetArrowByClass` だけ無料なのは、マーカーの by-class 化が作り直しを起こさないから。**
   同じ PIO の同じ 1 周で、他の 6 つが 9ms 掛かる横でマーカーだけが 0.00ms だった
   （**800 倍以上の差が実機で再現した**）。「マーカーは描画キャッシュに関与しないから」
   という読みで**合っている**。
   - **「マーカーだけ文書の既定で既に by-class だから無料なのだ」は誤り**
     （潰した筋として残す）。新規の空図面では**7 つとも既定は `false`** で、マーカーも
     例外ではなかった。無料なのは「変化が無いから」ではなく「変化が作り直しを呼ばないから」。
3. **費用は「書き込み」ではなく「状態の変化」に掛かる。** 既に by-class になっている
   オブジェクトへ**同じ書き込みをもう一度**すると、**7 つとも 0.00ms**（PIO でも）。
   だから「呼んだ回数」ではなく「**実際に変えた回数**」が効く。
4. **`SetObjectClass` も同額（9.27ms）。** クラスの割り当てそのものが作り直しを起こす。

この作り直しは [Parametric Objects](Parametric%20Objects.md) の
「リセット（再生成）をまとめられるか」と同じ機構で、
**止める口も、まとめて 1 度で済ませる口も無い**（そちらで潰し済み）。
残る道は「**作り直しを起こさせない**」——それが次節。

## 回数を減らす道は「文書の既定を先に立てる」（＝ per-object の 8 つを呼ばない）

**`SetDefaultClass` と 7 つの `SetDefault*ByClass()` を、オブジェクトを作る前に立てておくと、
生まれたオブジェクトは最初からそのクラスに属し、属性も by-class になる。**
per-object の書き込みは**1 つも要らない**。

実測（既定を立ててから作り、**per-object の書き込みを 1 度も呼ばずに**読み戻した）:

| | 構造材 PIO | 矩形 |
| --- | ---: | ---: |
| 既定のクラスで生まれた数 | **20 / 20** | **20 / 20** |
| ペン色・面色・線の太さ・線種・面パターン・不透明度 | **各 20 / 20** | **各 20 / 20** |
| **マーカー** | **1 / 20** | **0 / 20** |
| 作る（既定を立てない場合） | 12.05ms/個（14.56ms） | 0.01ms/個（0.01ms） |

- **既定を立てる費用は無視してよい**（8 回ぶん合計 0.01ms）。オブジェクトの数ではなく
  **クラスの数**に比例するので、クラスが変わるたびに `SetDefaultClass` を呼び直せばよい。
- **作成そのものも速くなる**（PIO で 14.56 → 12.05ms/個）。作った後に 7 回作り直させる
  ぶんが消えるため。
- **マーカーだけは継承されない。** 既定は `GetDefaultArrowByClass()` が `true` を返すのに、
  生まれたオブジェクトは by-instance のままだった（PIO 20 個中 1 個、矩形は 0 個しか
  by-class にならない）。**マーカーが要るなら per-object の `SetArrowByClass` を呼ぶ**
  ——が、**それは無料**（0.00ms。上記の表）なので、費用の話には影響しない。
  - **この 1/20 の正体は「立てた旗が、最初の PIO の作り直しで下りる」だった**
    （下記「マーカーの既定を下ろしているのは…」で確定）。**旗は立っている間に生まれた
    最初の PIO にだけ継承され、その PIO 自身の再生成で下りる**ので、PIO は 1/20 に、
    その後に作った矩形は 0/20 になる。
  - **巻き添えは無い。** 他の 6 旗は PIO を作っても下りない（同上）。
    **この高速化が取り込みの途中で崩れることはない。**
- **「作る前」でなければ効かない。** 既定はオブジェクトが生まれる瞬間に読まれるので、
  作った後に立てても既存のオブジェクトは変わらない。

### マーカーの既定を下ろしているのは「PIO の作り直し（regen）」——1/20 の正体

実測（VW 2026 / mac / 新規の空図面。`probes/runtime/default-arrow-byclass-drop/`。
7 つの既定を立ててから、**1 つ作るごとに**文書の既定の 7 旗と、そのオブジェクトの 7 旗を
読み直した。per-object の書き込みは 1 度もしていない）。

**文書の既定の旗**（作った直後に読み直したもの）:

| 何をした後 | 他の 6 つ | **マーカー** |
| --- | --- | --- |
| 7 つを立てた後 | yes | **yes** |
| 矩形 A を作った後 | yes | yes |
| **構造材 PIO 1 を作った後** | yes | **no** ← ここで下りる |
| 構造材 PIO 2 を作った後 | yes | no |
| 矩形 B を作った後 | yes | no |

**生まれたオブジェクトの旗**:

| | 他の 6 つ | **マーカー** |
| --- | --- | --- |
| 矩形 A | yes | **yes** |
| **構造材 PIO 1** | yes | **yes** |
| 構造材 PIO 2 | yes | no |
| 矩形 B | yes | no |

**下ろしているのは「作成」ではなく「作り直し（regen）」である。** 3 手で切り分けた
（どの手も、測る前に旗を立て直してから行った——「下りているものは下りない」を
読み違えないため）:

| 何をした後 | マーカーの旗 |
| --- | --- |
| `CreateCustomObjectPath(..., doRegen=false)` で PIO を作る | **yes（下りない）** |
| その PIO へ `ResetObject` | **no（下りる）** |
| 矩形へ `ResetObject` | yes（下りない） |

読みどころが 5 つある。

1. **`doRegen=false` なら下りない。** 下ろしているのは「PIO を作ること」ではなく、
   作成に含まれる**再生成**である。
2. **作成を伴わない `ResetObject` でも下りる。** だから**PIO の再生成を起こす経路は
   すべて疑う**——作成（`doRegen=true`）・`ResetObject`・**`UpdateStyledObjects`**
   （ジオメトリの作り直しまで行う。[Parametric Objects](Parametric%20Objects.md)）。
   最後の 1 つは**【推定】**——直接は測っていない。前 2 つは実測である。
3. **矩形の `ResetObject` では下りない。** 再生成を持たないものでは何も起こらない。
   **PIO でないものを何個作っても旗は保つ**（矩形 A で確認済み）。
4. **1 回の再生成で 1 回だけ下りる。これが #94 の 1/20 の正体。** 旗は「立っている間に
   生まれた最初の PIO」にだけ継承され、**その PIO 自身の再生成で下りる**。だから PIO は
   20 個中 1 個になり、その後に作った矩形は 20 個中 0 個になる（#94 の表は PIO → 矩形の
   順で走らせている）。**矩形が先なら矩形が継ぐ**——上の表の矩形 A が実際にそうなった。
5. **巻き添えは無い。** 他の 6 旗（ペン色・面色・線の太さ・線種・面パターン・不透明度）は
   **全区間で `yes` のまま**だった。**前節の高速化は、大量生成の途中で崩れない。**
   崩れるのはマーカーだけである。

**立て直せば毎回継げる（が、毎回下りる）。** 「作る直前に `SetDefaultArrowByClass()` を
呼ぶ」を 3 本続けたところ、**3 本とも継ぎ、3 本とも直後に下りた**（3/3）。ただし
per-object の `SetArrowByClass(h)` を呼ぶのと手間は変わらず、そちらは**作り直しを
起こさない（0.00ms）うえ図面の既定を触らない**ので、**per-object のほうが素直**である。

### マーカーの既定 by-class は、意図して下ろせない

5 つで効いた筋（「**既定の値を書けば旗が下りる**」。下記「既定の by-class は…」）は、
**マーカーでは効かない**。`SetDefaultArrowHeadsN` を書いても旗は下りなかった——
**同じ値でも違う値でも**である（同じプローブ。値そのものは書けている——`size` は
`0.1250` → `0.2500` に変わった）。

| 何をした後 | マーカーの旗 |
| --- | --- |
| `SetDefaultArrowByClass()` を立て直した | yes |
| `SetDefaultArrowHeadsN`（**同じ値**） | **yes（下りない）** |
| `SetDefaultArrowHeadsN`（**違う値**） | **yes（下りない）** |

**下ろす道は「PIO を 1 つ再生成する」しか無い**（上記）。それは図面へオブジェクトを
足すか既存の PIO を作り直すことなので、**後始末には使えない**。

> **帰結: `SetDefaultArrowByClass()` を呼んではいけない。** 呼ぶと**その図面の既定を
> 元へ戻せない**——実測でも、プローブの締めで他の 6 旗は走らせる前へ戻ったのに、
> **マーカーの旗だけが「走らせる前 = no / 締めの後 = yes」のまま残った**。
> 継承もされない旗を、戻せない代償で立てることになる。
> **マーカーが要るなら per-object の `SetArrowByClass(h)` を呼ぶ**——無料で、
> 図面の既定を触らない。

### `SetObjectClass` だけでは属性は by-instance のまま（前提は今も正しい）

クラスを与えても属性は付いてこない。生成直後も `SetObjectClass` の直後も、
**7 つとも `false`** のままだった（PIO・矩形・球のいずれでも）。
だから「クラスを与えるだけで済ませる」ことはできない——**済ませたいなら上記の
「文書の既定」を使う**。

### 取り込みのような大量生成での書き方

```cpp
// 取り込みの初めに 1 度（クラスが変わるたびに SetDefaultClass だけ呼び直す）
gSDK->SetDefaultClass(classID);
gSDK->SetDefaultPColorsByClass();  gSDK->SetDefaultFColorsByClass();
gSDK->SetDefaultLWByClass();       gSDK->SetDefaultPPatByClass();
gSDK->SetDefaultFPatByClass();     gSDK->SetDefaultOpacityByClass();
// SetDefaultArrowByClass() は**呼ばない**——継承されないうえ、下ろす口が無い
// （下記「マーカーの既定 by-class は、意図して下ろせない」）。

// 以後、オブジェクトを作るだけ。per-object の 6 つも SetObjectClass も呼ばない。
MCObjectHandle object = /* CreateCustomObjectPath など */;
gSDK->SetArrowByClass(object);     // マーカーだけ。無料
```

**効き目**: 1 オブジェクトあたり「9ms × 7」が消える。作り直しを起こす書き込みが
残らないため。

## 既定の by-class は「既定の値を書き戻す」と下りる（退避・復元できる）

前節の高速化には**後始末**が要る。文書の既定はその図面の設定なので、取り込みのような
一時の処理で立てたなら元へ戻さなければならない。**戻せる**——ただし「off にする口」では
なく、**既定の値そのものを書く口**で戻す。

### 下ろす専用の口は無い【ヘッダ根拠】

`SetDefault*ByClass()` は**いずれも引数なし**で、立てる一方である。off にする引数を持つのは
2 つだけ:

- `SetDefaultClass(InternalIndex classID)` — クラスそのもの
- `SetDefaultOpacityByClassN(Boolean pen, Boolean fill)` — 不透明度（2 旗版）

残る 5 つ（ペン色・面色・線の太さ・線種・面パターン）とマーカーには無い。代わりに
**既定の値を読む／書く口**が揃っている。

| 属性 | 既定の値を読む | 既定の値を書く |
| --- | --- | --- |
| ペン色・面色 | `GetDefaultColors(ObjectColorType&)` | `SetDefaultColors(ObjectColorType)` |
| 線の太さ | `GetDefaultLineWeight()` → `short`（mils） | `SetDefaultLineWeight(short mils)` |
| 線種 | `GetDefaultPenPatN()` → `InternalIndex` | `SetDefaultPenPatN(InternalIndex)` |
| 面パターン | `GetDefaultFillPat()` → `InternalIndex` | `SetDefaultFillPat(InternalIndex)` |
| マーカー | `GetDefaultArrowHeadsN(Boolean&, Boolean&, ArrowType&, double_gs&)` **（有無は読めない）** | `SetDefaultArrowHeadsN(Boolean, Boolean, ArrowType, double_param)` **（有無の `false` は無視される）** |
| 不透明度 | `GetDefaultOpacityN(OpacityRef&, OpacityRef&)` | `SetDefaultOpacityN(OpacityRef, OpacityRef)` |

- **ペン色と面色は 1 本に同居している。** `ObjectColorType { ColorRef fillFore, fillBack,
  penFore, penBack; }`（`Kernel/API/MiniCadCallBacks.h:519`。`ColorRef` は `Uint16`）で、
  `==` / `!=` も定義済み——退避した値との一致は構造体ごと比べられる。
  **「ペン色だけを書く」口は無い。**
- **丸ごと退避・復元する口は無い**（`GetDefaultAttributes` 的なもの）。SDK 全体の
  `(Set|Get)Default*` はこの顔ぶれしかない（`sdk-grep` で全識別子を列挙して確認）。
- **マーカーの行は、この表のとおりには使えない。** 有無（`starting` / `ending`）は
  読めも消せもしないので、**新口（`Get/SetDefaultBeginningMarker` /
  `…EndMarker`）を使う**——下記「文書の既定のマーカーの有無は、旧口（`ArrowHeads`）
  では読めも消せもしない」。`style` と `size` は旧口でも往復する。
- **VWFC の `VWObjectAttr` は、ハンドル無しで作ると文書の既定を読み書きする。**
  `fhObject == nil` のとき、すべての getter / setter が `*Default*` 系へ落ちる作りで、
  per-object と同じ顔で既定を触れる（`SDKLib/Source/VWSDK/VWFC/VWObjects/VWObjectAttributes.cpp`。
  例: `GetPenForeColor()` はハンドルがあれば `GetColor(h, …)`、無ければ
  `GetDefaultColors(…)`）【ソース根拠】。**ただしここにも「旗を下ろす」口は無い**
  （`SetFillColorByClass()` はあるが逆が無い）ので、戻す道は結局「値を書く」である。

### 既定の値を書くと、その属性の旗だけが下りる

実測（VW 2026 / mac / 新規の空図面。`probes/runtime/default-byclass-restore/`。
5 つを立てた状態から、**退避しておいた値を 1 本ずつ書き戻して**、書くたびに 5 旗を
読み直した）:

| 何をした後 | PColors | FColors | LW | PPat | FPat |
| --- | --- | --- | --- | --- | --- |
| 5 つを立てた後 | yes | yes | yes | yes | yes |
| `SetDefaultColors(退避した値)` | **no** | **no** | yes | yes | yes |
| `SetDefaultLineWeight(退避した値)` | no | no | **no** | yes | yes |
| `SetDefaultPenPatN(退避した値)` | no | no | no | **no** | yes |
| `SetDefaultFillPat(退避した値)` | no | no | no | no | **no** |

読みどころが 4 つある。

1. **対応は 1 対 1**（色だけが 1 対 2）。書いた属性の旗だけが下り、他は動かない。
   **`SetDefaultColors` は 1 本でペン色と面色の両方を下ろす**——片方だけを下ろす道は無い。
2. **同じ値を書いても旗は下りる。** 上の表で書いたのは**退避した値そのもの**で、値は 1 つも
   変わっていない。それでも旗は下りた。**前節の費用の話（「費用は変化に掛かる」）とは
   別の理屈で動く**ので、「値が同じなら無視されるのでは」と心配して余計な書き込みを
   挟まなくてよい。
3. **値は完全に往復する。** 書き戻した後に読み直した既定は、退避した値と**一致**した
   （`penFore=257 penBack=256 fillFore=257 fillBack=256 / lineWeight=2 penPat=2 fillPat=1`）。
4. **旗を立てても値は壊れない。** `SetDefault*ByClass()` を 5 つ立てた**後**に読んだ既定の
   値も、立てる前と同じだった。**旗と値は別に持たれている**ので、退避を「立てる前」に
   済ませ忘れても取り返せる（とはいえ、退避は先に済ませるのが素直）。

### 戻した後に作ったものは by-instance で生まれる

戻した後に作った**矩形と構造材 PIO**は、どちらも **5 旗とも no** で、値は**退避した既定と
一致**した（上記と同じプローブ）。つまり**戻した後に利用者が描くものは、取り込み前と
同じ既定で生まれる**——前節の
「既定はオブジェクトが生まれる瞬間に読まれる」がそのまま裏返しに働く。

### 取り込みのような大量生成での後始末

```cpp
// --- 取り込みの初めに 1 度: 退避する -----------------------------------
ObjectColorType savedColors;
gSDK->GetDefaultColors(savedColors);
const short         savedLineWeight = gSDK->GetDefaultLineWeight();
const InternalIndex savedPenPat     = gSDK->GetDefaultPenPatN();
const InternalIndex savedFillPat    = gSDK->GetDefaultFillPat();
const InternalIndex savedClass      = gSDK->GetDefaultClass();
Boolean savedPenOpacityByClass = false, savedFillOpacityByClass = false;
gSDK->GetDefaultOpacityByClassN(savedPenOpacityByClass, savedFillOpacityByClass);
// 旗そのものも退避する（戻した後に立て直すため。元から by-class の図面もある）
const Boolean savedPColorsByClass = gSDK->GetDefaultPColorsByClass();
const Boolean savedFColorsByClass = gSDK->GetDefaultFColorsByClass();
const Boolean savedLWByClass      = gSDK->GetDefaultLWByClass();
const Boolean savedPPatByClass    = gSDK->GetDefaultPPatByClass();
const Boolean savedFPatByClass    = gSDK->GetDefaultFPatByClass();

// --- 立てて、大量に作る（前節） ----------------------------------------

// --- 取り込みの終わりに 1 度: 戻す -------------------------------------
// **値を書くことが旗を下ろすことである**（対応が 1 対 1 なので順番は問わない）。
gSDK->SetDefaultColors(savedColors);          // ペン色と面色の旗が下りる
gSDK->SetDefaultLineWeight(savedLineWeight);  // 線の太さ
gSDK->SetDefaultPenPatN(savedPenPat);         // 線種
gSDK->SetDefaultFillPat(savedFillPat);        // 面パターン
gSDK->SetDefaultOpacityByClassN(savedPenOpacityByClass, savedFillOpacityByClass);
gSDK->SetDefaultClass(savedClass);

// 元から by-class だった旗だけを立て直す（値を書いた時点で全部下りているため）。
if (savedPColorsByClass) gSDK->SetDefaultPColorsByClass();
if (savedFColorsByClass) gSDK->SetDefaultFColorsByClass();
if (savedLWByClass)      gSDK->SetDefaultLWByClass();
if (savedPPatByClass)    gSDK->SetDefaultPPatByClass();
if (savedFPatByClass)    gSDK->SetDefaultFPatByClass();
```

**元から by-class の図面を壊さないために、旗も退避して立て直すこと。** 値だけ書き戻すと
「利用者が『クラスの属性を使用』にしていた図面を by-instance へ変えてしまう」ので、
**戻すつもりで設定を書き換える**ことになる。

**マーカー（`SetDefaultArrowByClass()`）は、この後始末に載らない**——`SetDefaultArrowHeadsN`
を書いても旗は下りない（同じ値でも違う値でも。上記「マーカーの既定 by-class は、意図して
下ろせない」で確定）。**立てたら戻せない**ので、**そもそも立てない**のが唯一の正解である
——マーカーが要るなら per-object の `SetArrowByClass` を呼ぶ（無料）。

> **マーカーの「値」の退避・復元も、旧口（`SetDefaultArrowHeadsN`）ではできない。**
> 有無（`starting` / `ending`）が `false` を受け付けず、読み戻しも嘘をつくため。
> **`SetDefaultBeginningMarker` / `SetDefaultEndMarker` を使う**——次節で確定した
> （[#106](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/106)）。

## 文書の既定のマーカーの有無は、旧口（`ArrowHeads`）では読めも消せもしない

**マーカーには口が 2 系統ある。** 上表に挙げた `ArrowHeads` 系が古い方で、
`SMarkerStyle` と **`visibility`（有無）を別々の引数で持つ**系統が別にある
（いずれも `Interfaces/VectorWorks/ISDK.h`）。

| | 文書の既定 | per-object | クラス |
| --- | --- | --- | --- |
| **旧口**（真偽値 2 つ） | `Get/SetDefaultArrowHeadsN` | `Get/SetArrowHeadsN(h, …)` | — |
| **新口**（`SMarkerStyle` ＋ `visibility`） | `Get/SetDefaultBeginningMarker` / `…EndMarker` | `Get/SetObjBeginningMarker` / `…ObjEndMarker` | `Get/SetClassBeginningMarker` / `…EndMarker` |

**文書の既定の旧口には穴が 2 つある。** どちらも**`SetDefaultArrowByClass()` を一度も
呼んでいない図面**（by-class の旗は全区間 `no`）で出たので、
`GetDefaultOpacityByClass` の穴（by-class のときだけ嘘）とは**別物**である。

### 穴 1: `GetDefaultArrowHeadsN` の `ending` は、終点ではなく `starting` を返す

実測（VW 2026 / mac。組み合わせごとに `OpenDocumentPath(nullptr, false)` で新しい空の
図面を開き、真っさらな既定から測った。`probes/runtime/default-arrow-heads-false/`）。
**同じ状態を 3 経路で読んでいる**——旧口・新口・**書いた直後に引いた線**（既定は
オブジェクトが生まれる瞬間に読まれるので、実際に何が書かれていたかの物証になる）:

| 旧口へ書いた値 | 旧口の読み | 新口の読み（始点 / 終点） | 直後に引いた線（始点 / 終点） |
| --- | --- | --- | --- |
| （触っていない） | start=no **end=no** | no / no | no / no |
| `(start=yes, end=no)` | start=yes **end=yes** | **yes / no** | **yes / no** |
| `(start=no, end=yes)` | start=no **end=no** | **no / yes** | **no / yes** |
| `(start=yes, end=yes)` | start=yes end=yes | yes / yes | yes / yes |

- **新口と線は 4 行とも書いたとおり**で、互いに一致している。**食い違っているのは
  旧口の `ending` だけ**で、その値は**毎回 `starting` と同じ**である。
  つまり `ending` は**終点の状態ではなく、`starting` のコピー**——
  **文書の既定の終点マーカーの有無は、旧口では読めない。**
- **「どちらかが点いていれば両方 `yes`」ではない**（潰した筋）。3 行目
  `(no, yes)` で旧口が `end=no` を返しているので、OR ではなくコピーである。
- **`starting` のほうは正しい。** 嘘をつくのは `ending` の 1 つだけ。

### 穴 2: `SetDefaultArrowHeadsN` は `starting` / `ending` の `false` を無視する

**一度点けたマーカーは、この口では消せない。** 上の各行のあと、続けて
`(start=no, end=no)` を**`size` を変えて**書いた（`size` が変われば「呼び出しは
届いている」と言い切れる）:

| 直前の実態（始点 / 終点） | `(no, no)` を書いた後の新口 | 直後に引いた線 | `size` |
| --- | --- | --- | --- |
| yes / no | **yes** / no | **yes** / no | 0.0472 → **0.0945**（書いたとおり） |
| no / yes | no / **yes** | no / **yes** | 同上 |
| yes / yes | **yes / yes** | **yes / yes** | 同上 |

**`style` と `size` は往復するのに、真偽値だけが点ける方向にしか効かない。**
3 経路とも一致しているので、これは読みの嘘ではなく**書き込みが無視されている**。

### 消すには新口の `visibility=false` を使う（これが退避・復元の道）

同じ 3 つの状態から `SetDefaultBeginningMarker(mstyle, false)` /
`SetDefaultEndMarker(mstyle, false)` を呼ぶと、**3 経路とも `no` になった**
（旧口の読みも `start=no end=no` へ戻る）。**文書の既定のマーカーを一時的に変えて
元へ戻す作りは、新口でなら書ける。**

**復元は「新口で消してから、旧口で点け直す」の 2 手で書く。** 2 つの口の、
**それぞれ実測できている向き**だけを使う形である——新口は消す向き（上記）、
旧口は点ける向き（穴 2 の表の ②。`(yes, no)` を書けば始点だけが点く）。
**旧口で `false` が無視されるのは、直前に新口で消してあるので害が無い**
（消えている端へ `false` を書いても消えたままなのは、穴 1 の表の 1〜2 行目で
測れている）。

```cpp
// --- 退避: 有無は新口で読む（旧口の ending は信用できない） ---------------
SMarkerStyle savedBegin{}, savedEnd{};
Boolean savedBeginVisible = false, savedEndVisible = false;
gSDK->GetDefaultBeginningMarker(savedBegin, savedBeginVisible);
gSDK->GetDefaultEndMarker(savedEnd, savedEndVisible);
// 様式と大きさは旧口でまとめて退避できる（往復する）。
Boolean ignoredStart = false, ignoredEnd = false;
ArrowType savedStyle = 0;
double_gs savedSize = 0;
gSDK->GetDefaultArrowHeadsN(ignoredStart, ignoredEnd, savedStyle, savedSize);

// --- …取り込みのあいだだけ変える… ---------------------------------------

// --- 復元: ① 新口で両端を消す（これが唯一の「消す」道） -------------------
gSDK->SetDefaultBeginningMarker(savedBegin, static_cast<Boolean>(false));
gSDK->SetDefaultEndMarker(savedEnd, static_cast<Boolean>(false));

// --- 復元: ② 元から点いていた端だけを旧口で点け直す ----------------------
// 点ける向きは効く。false は無視されるが、①で消してあるので害が無い。
gSDK->SetDefaultArrowHeadsN(savedBeginVisible, savedEndVisible, savedStyle, savedSize);
```

**新口で「点ける」（`visibility=true`）のは、文書の既定では測っていない**
【未確認】——per-object では効いた（下記）が、文書の既定で同じかは確かめていない。
**上の手順はその向きを使わない**ので、確かめなくても書ける。

**戻り値（`Boolean`）で成否を判定しないこと**——`visibility=false` を書いて
実際に消えた回も、消えなかった旧口の呼び出しも、どちらも区別が付かなかった。
[調査の作法](Investigation%20Techniques.md)のとおり**読み戻して確かめる**。

### per-object（`SetArrowHeadsN(h, …)`）は 2 つとも正しい

**穴は文書の既定だけの話である。** 線を 1 本ごとに新しく引いて同じ電池を通したところ、
per-object では:

- `GetArrowHeadsN(h, …)` の `ending` は**終点を正しく返す**（`(no, yes)` を書けば
  `start=no end=yes` と読める）。
- `SetArrowHeadsN(h, no, no, …)` は**マーカーを消せる**（3 通りとも消えた）。
- 旧口を一度も通さず、新口だけで点けて消すこともできる。

だから **per-object のマーカーを触るぶんには、旧口のままでよい**。新口が要るのは
**文書の既定**を読み書きするときである。

### #104 の観測は、この 2 つで説明が付く

[#104](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/104)
で「`(yes, no)` を書いたら `(yes, yes)` と読めた」「退避した `(no, no)` を書き戻しても
`(yes, yes)` のままだった」のは、**by-class の旗とは無関係**だった:

- 1 つ目は**穴 1**（`ending` が `starting` のコピー）。実際には終点は点いていない。
- 2 つ目は**穴 2 と穴 1 の合わせ技**。始点は `false` が無視されて点いたままで、
  終点はそのコピーが出ている。

**当時「by-class のときに getter が嘘をつくのでは」と疑った筋は、ここで潰れた**
——旗を一度も立てていない図面で、同じ食い違いがそのまま再現している。

### 読み戻しに使えない口（ここまでに分かっているもの）

| 口 | 何が起きるか | 代わりに使うもの |
| --- | --- | --- |
| `GetDefaultOpacityByClass()`（1 旗版） | 立てた後も `false` を返す（上記） | `GetDefaultOpacityByClassN(pen&, fill&)` |
| `GetDefaultArrowHeadsN` の `ending` | 終点ではなく `starting` を返す | `GetDefaultEndMarker(mstyle&, visibility&)` |
| `SetDefaultArrowHeadsN` の `starting` / `ending` | `false` が無視される（読みの問題ではない） | `SetDefaultBeginningMarker` / `SetDefaultEndMarker` |

### 余録: `style=0` は「マーカー無し」ではない【ヘッダ根拠】

`ArrowType` は `Sint32` で、値は `MarkerType` の体系である
（`Kernel/API/MiniCadCallBacks.h`）。**`kArrowMarker = 0`** なので、
**`style=0` は「無し」ではなく矢印そのもの**——有無は `style` ではなく
`visibility`（新口）で持たれている。実測でも `style=0` のまま `visible` が
`yes` にも `no` にもなった。根種別は `kMarkerRootTypeMask = 127` で取り出す。

### 余録: `GetObjBeginningMarker` が `false` を返したら、出力引数を読まない

実測はこうである——**マーカーを一度も設定していない線では `false` が返り**、
そのとき `mstyle` / `visibility` には**その図面の既定とも無関係な値**が入っていた
（既定が `size=0.0472` の図面で `size=0.1250`）。**設定した後は `true`** に変わる。

**「戻り値は『値が入っているか』を表す」というのはこの並びからの解釈で、
【推定】である**（#106 の問いではないので、そこは追っていない）。
ただし**実測から直接言えること**——`false` のときの出力引数は当てにならない——
だけで、使う側には足りる。

### 範囲外: per-object のマーカーの `size` は書いたとおりにならない

`SetArrowHeadsN(h, …, size=3.0000)` と書いて、読み戻しは旧口 `2.0000` /
新口 `1.8000` だった（文書の既定側では `0.0472` → `0.0945` と書いたとおりに
往復する）。**この調査の問いは有無（真偽値）なので、ここは追っていない**——
[#108](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/108)
へ切り出した。マーカーの**大きさ**を per-object で指定するなら、そちらを先に読むこと。
