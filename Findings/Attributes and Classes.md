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
  （**それで絵に出るのかは未確認**——下記「未確認のまま残っているもの」。[#120](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/120)）
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
>
> **ただし、それでクラスのマーカーが本当に絵に出るのかは確かめていない**
> ——`SetArrowByClass` を呼んだ線から `GetMarkerPolys` はマーカーの図形を返さなかった
> （下記「未確認のまま残っているもの」。
> [#120](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/120)）。

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
——マーカーが要るなら per-object の `SetArrowByClass` を呼ぶ（無料。**ただしそれで絵に
出るのかは未確認**——下記「未確認のまま残っているもの」。[#120](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/120)）。

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

### 余録: `style=0` は「マーカー無し」ではない

**マーカーの有無は `style` ではなく `visibility`（新口）で持たれている。**
実測でも `style=0` のまま `visible` が `yes` にも `no` にもなった。どの口でも
`0` は「無し」ではなく**矢印**を指す（新口の `MarkerType` では `kArrowMarker = 0`、
旧口の番号でも `0` は塗り矢印。下記「マーカーの様式は口ごとに別の番号体系」）。

> **この節にあった「`ArrowType` は `Sint32` で、値は `MarkerType` の体系である
> 【ヘッダ根拠】」は誤りだったので消した。** `Kernel/API/MiniCadCallBacks.h` は 756 行目から
> `ArrowType`（`arArrow = 0` … `arCross = 4`）と `MarkerType`（`kArrowMarker = 0` …）の
> **2 つの体系を続けて**定義しており、**隣の enum を読み違えていた**。正しい対応は
> 下記「マーカーの様式は口ごとに別の番号体系」に実測で書いてある
> （[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)）。
> `Sint32` であることと、`0` が「無し」ではないことだけが合っていた。

### 余録: `GetObjBeginningMarker` が `false` を返したら、出力引数を読まない

実測はこうである——**マーカーを一度も設定していない線では `false` が返り**、
そのとき `mstyle` / `visibility` には**その図面の既定とも無関係な値**が入っていた
（既定が `size=0.0472` の図面で `size=0.1250`）。**設定した後は `true`** に変わる。

**「戻り値は『値が入っているか』を表す」というのはこの並びからの解釈で、
【推定】である**（#106 の問いではないので、そこは追っていない）。
ただし**実測から直接言えること**——`false` のときの出力引数は当てにならない——
だけで、使う側には足りる。

### per-object のマーカーの `size` が書いたとおりに読めないのは、読みが丸まるから

`SetArrowHeadsN(h, …, size=3.0000)` と書いて読み戻しが旧口 `2.0000` / 新口 `1.8000` に
なるのは、**上限（1.8 インチ）と、旧口の per-object 読みの丸め（整数インチ）が重なった**
ためである（[#108](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/108)
で確定。下記「マーカーの大きさ」）。

## マーカーの大きさはインチで、上限があり、per-object の旧口の読みだけが丸まる

実測（VW 2026 / mac / 新規の空図面。`probes/runtime/arrow-head-size/`。
書いた値を **0.0100〜12.0000 の 16 点で掃引**し、**旧口・新口・ポイント口の 3 経路**で
読み戻した。測ったのはプローブが自分で開いて閉じる空の図面の中だけ）。

### 単位はインチ。図面の単位にも縮尺にも依らない

- **`Set/GetArrowHeadsN` の `size` も `SMarkerStyle.dSize` もインチである。**
  VWFC が `sizeInMilimeters = sizeInInch * 25.4` と換算している
  （`SDKLib/Source/VWSDK/VWFC/VWObjects/VWObjectAttributes.cpp:568-605`）だけでなく、
  実機でも裏が取れた——`SetArrowHeadsN(h, …, 1.0000)` を書いた線を**ポイントの口**
  `GetArrowHeads(h, …, short& sizeInPoints)` で読むと **72**（1 インチ = 72 ポイント）。
  0.5000 → **36**、0.1250 → **9**。
- **測った図面の単位は mm**（`GetUnits` の `unitsPerInch=25.4` / `unitMark=mm`、
  `storedAccuracy=1`）で、**アクティブレイヤの縮尺は 100** だった。それでもインチである
  ——**図面の単位に引きずられない**。
- **縮尺を変えても読みは動かない**（1:1 と 1:50 で、0.5000 → 0.5000 / 3.0000 → 1.8000 と
  まったく同じ）。マーカーの大きさは**紙の上の寸法**である。
- **`N` の付かない旧口はポイント**（VWFC の引数名 `sizeInPoints`）。**同じ線を 2 つの単位で
  読める**ので、疑ったときはこちらで裏が取れる。

### 上限がある——約 2.0 インチ（≈50.8mm）。書く口で切られ方が違う

| 書く口 | 書いたとおり入る範囲 | 超えたとき |
| --- | --- | --- |
| **旧口** `SetArrowHeadsN(h, …)` / `SetDefaultArrowHeadsN` / `SetArrowHeads`（ポイント） | 〜**1.8000 インチ**（130 ポイント） | **1.8000 で頭打ち** |
| **新口** `SetObjBeginningMarker` / `SetObjEndMarker` | 〜**1.9999 インチ** | **1.9999 で頭打ち** |

- 旧口は **1.9000 を書いても 1.8000** になる。新口は 1.9000 がそのまま入り、
  2.0000 以上で 1.9999 に落ち着く（2.2000 でも 12.0000 でも 1.9999）。
- **ポイント口でも同じところで切れる**——144 ポイント（= 2 インチ）を書いて読みは **130**
  （= 1.8056 インチ。1.8 インチ = 129.6 の丸め）。**旧い口はどれも 1.8 インチで切る。**
- **文書の既定も同じ上限**（`SetDefaultArrowHeadsN` に 1.9000 → 既定は 1.8000）。
  「per-object だけの穴」ではない。
- **下は素直**。0.0100 でも書いたとおり入る（文書の既定側だけ 0.0100 → **0.0099** の
  微小な量子化が出た）。
- **矢印を 2 インチ（≈50mm）より大きくする道は、ここまでに測った口には無い。**

### 正体: 大きさは **1/16384 インチの 16 ビット整数**——だから上限が約 2 インチになる

**中口（`Get/SetMarker`）の `size` を掃引して分かった**
（[#116](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/116)。
`probes/runtime/marker-angle-order/`。
[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/117#issuecomment-5823060642)）。
**`SetMarker` の `size`（`short`）はポイントではなく 1/16384 インチ**である:

| 中口へ書いた `size` | 新口 `dSize`（インチ） | `size ÷ 16384` | `size ÷ 72`（ポイント説） |
| --- | --- | --- | --- |
| `1` | 0.000061 | 0.000061 | 0.013889 |
| `18` | 0.001099 | 0.001099 | 0.250000 |
| `72` | 0.004395 | 0.004395 | 1.000000 |
| `512` | 0.031250 | 0.031250 | 7.111111 |
| `4096` | 0.250000 | 0.250000 | 56.888889 |
| `16384` | **1.000000** | 1.000000 | 227.555556 |
| `32767` | **1.999939** | 1.999939 | 455.097222 |
| `-1` | −0.000061 | −0.000061 | −0.013889 |

**11 点すべてで `dSize = size ÷ 16384` だった**（ポイント説はどの点でも合わない）。
逆向きも同じで、新口へ書いたインチは中口で `dSize × 16384` として読める
（0.0625 → `1024` / 0.2500 → `4096` / 1.0000 → `16384`）。

- **上限 `1.9999…` の正体は `short` の上限そのもの。** 新口へ `2.0000` を書くと
  **`1.999939` = 32767 ÷ 16384** で止まり、中口で読むと `32767` である。
  「約 2.0 インチ」は**`short` に 1/16384 インチを詰めた器の上限**だった。
  `1.9999` を書いた線は `32766`（= 1.999878）になる。
- **新口の `dSize` も 1/16384 インチへ量子化されている**——`0.0100` を書くと
  `0.010010`（= 164 ÷ 16384）が読み戻る。**#108 が文書の既定側で見た「0.0100 →
  0.0099」も同じ筋の量子化である【推定】**（0.0099 ≈ 162 ÷ 16384。ただし per-object で
  測った今回は 164 だったので、丸め方までは合わせていない）。
- **`N` の付かない旧口（ポイント）と混同しないこと。** 同じ `short` でも
  `Get/SetArrowHeads` は **72 分の 1 インチ**、`Get/SetMarker` は **16384 分の 1 インチ**
  である。中口へポイントのつもりで `36` を渡すと、0.5 インチではなく
  **0.0022 インチ**（ほぼ見えない大きさ）になる。

### per-object の `GetArrowHeadsN` は大きさを**整数インチに丸めて**返す（読みに使ってはいけない）

**#108 の出発点だった食い違いの正体はこれである。** 同じ線を新口で読むと書いたとおりなのに、
旧口で読むと丸まる:

| 書いた `size`（インチ） | 新口 `dSize`（正しい） | **旧口 `GetArrowHeadsN`** |
| --- | --- | --- |
| 0.0100 / 0.0472 / 0.1250 / 0.2500 | 書いたとおり | **0.0000** |
| 0.5000 / 0.7500 / 1.0000 / 1.2500 | 書いたとおり | **1.0000** |
| 1.5000 / 1.7500 / 1.8000（以上は 1.8000 で頭打ち） | 〃 | **2.0000** |

境目は **0.2500↔0.5000** と **1.2500↔1.5000**——**四捨五入**（0.5 は切り上がる）である。

- **【推定】ポイント口の値を 72 で割って整数へ丸めている。** 実測のポイント値
  （9 / 36 / 72 / 130）を 72 で割って四捨五入すると 0 / 1 / 1 / 2 で、旧口の読みに
  すべて一致する。
- **丸めるのは per-object の `GetArrowHeadsN` だけ。** `GetDefaultArrowHeadsN`（文書の既定）は
  書いた値をそのまま返す（0.0472 → 0.0472、1.2500 → 1.2500）。#108 が
  「既定は往復するのに per-object は合わない」と見たのは、**この非対称そのもの**である。
- **書き込みは壊れていない。** 旧口で書いた値は（1.8 インチの上限まで）正しく入っており、
  読み方を変えれば見える。

> **per-object のマーカーの大きさを読むなら `GetArrowHeadsN` を使わない。**
> `GetObjBeginningMarker` / `GetObjEndMarker` の `dSize`（インチ）か、
> `GetArrowHeads` の `short`（ポイント）で読む。
> **書くのも新口**（`SetObjBeginningMarker` / `SetObjEndMarker`）にする——旧口より
> 上限が広い（1.8 → 2.0 インチ）。
>
> **有無（`starting` / `ending`）は per-object なら旧口のままでよい**（上記
> 「per-object（`SetArrowHeadsN(h, …)`）は 2 つとも正しい」）。**大きさだけが別の話**である。

### per-object の `size=0.0000` は「未設定」と「0.5 インチ未満」の両方を意味する

| 線 | 旧口 `…N` の `size` | 新口の戻り値 | 新口 `dSize` |
| --- | --- | --- | --- |
| 引いた直後（何も書いていない） | 0.0000 | **no** | 0.1250（当てにならない） |
| 旧口で `0.1250` を書いた | **0.0000** | yes | 0.1250 |
| 旧口で `0.0000` を明示的に書いた | 0.0000 | yes | **0.0000** |

- **旧口の `0.0000` だけでは区別が付かない**——上の丸めで、0.5 インチ未満はすべて 0 になる。
- **区別は新口の戻り値で付く**（`no` なら未設定。そのとき出力引数は当てにならない
  ——上記「余録: `GetObjBeginningMarker` が `false` を返したら…」）。
- **`0` は本当に 0 として書ける**（明示的に書くと `dSize=0.0000` で `visible=yes`）。

### `dWidth` は書いても入らない

新口で `dSize=0.5000` と **`dWidth=0.2500`** を書いて読み直すと、`dSize` は `0.5000` に
なったが **`dWidth` は `0.0000` のまま**だった。`nThicknessBasis` と `dThickness` も
測った全区間で `0` / `0.0000` である。**`SMarkerStyle` で実際に効いたのは
`style` / `nAngle` / `dSize` の 3 つだけ**（`nAngle` は全区間 `15` で、振っていない）。

### 様式（根種別）を変えても、大きさの振る舞いは変わらない

`kArrowMarker` / `kCircleMarker` / `kDimSlashMarker` / `kOpenBaseNoFillMarker` の 4 つで
測って、**すべて** 0.5000 → 0.5000、3.0000 → 1.8000 だった。上限も丸めも様式に依らない。

> **ただし、ここで振っていた `style` の数値は、新口で読める `MarkerType` とは
> 別の体系である**——旧口へ書いた `0` / `2` / `3` / `1280` は、新口ではそれぞれ
> `0` / `1280` / `2` / `2048` として読める。**対応表は下記「マーカーの様式は口ごとに
> 別の番号体系——`ArrowType` は `MarkerType` ではない」に確定させた**
> （[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)）。
> **様式を指定して描くなら、そちらを先に読むこと。**

## マーカーの様式は口ごとに別の番号体系——`ArrowType` は `MarkerType` ではない

**結論を先に。`SetArrowHeadsN` / `SetDefaultArrowHeadsN` / `SetMarker` / `SetClMarker` の
`style` に `MarkerType` の定数（`kCircleMarker` など）を渡してはいけない。** この 4 つは
いずれも**`MarkerType` ではない、口ごとに別の小さな番号**を受ける。`MarkerType` をそのまま
**書ける**のは新口（`Set…BeginningMarker` / `Set…EndMarker`——クラスのものも含む）だけである。

| 口 | 呼び出し | `style` に**書く**と | `style` を**読む**と |
| --- | --- | --- | --- |
| **旧口・per-object** | `Set/GetArrowHeadsN(h, …)` | **`EMarkerType` の番号 0〜6**（VWFC の並び） | **どちらの体系でもない。ほぼ常に `0`——読みに使えない** |
| **旧口・文書の既定** | `Set/GetDefaultArrowHeadsN` | **`ArrowType` の enum 0〜4**（`arArrow`…`arCross`） | 同上（0〜2 だけ返る） |
| **中口** | `SetMarker(h, …)` / `GetMarker(h, …)` | **`EMarkerType` の番号 0〜6**（宣言は `MarkerType` だが嘘） | **`MarkerType` そのもの**（新口と一致） |
| **新口** | `Set/GetObjBeginningMarker`・`…EndMarker`・`…Default…` | **`MarkerType` そのもの**（往復する） | **`MarkerType` そのもの** |
| **クラス・新口型** | `Set/GetClassBeginningMarker`・`…EndMarker` | **`MarkerType` そのもの**（往復する） | **`MarkerType` そのもの** |
| **クラス・中口型** | `Set/GetClMarker(index, …)` | **`EMarkerType` の番号 0〜6**（中口と同じ罠） | **`MarkerType` そのもの** |

**同じ `ArrowType` という型・同じ `style` という引数名なのに、per-object と文書の既定で
解釈が違う。** そして**中口は宣言が `MarkerType` なのに書きだけ番号で受ける**。
実測（VW 2026 / mac。`probes/runtime/marker-style-mapping/`。
[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)。
[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/115#issuecomment-5822033817)）。

### per-object の旧口（`SetArrowHeadsN`）は `EMarkerType` の番号 0〜6

`EMarkerType` は VWFC のヘッダにある並びである（`VWFC/VWFCLibrary.h:62`。
`kMarkerFilledArrow = 0` … `kMarkerCross = 6`）。**これが per-object の旧口の `style` の
正体**で、`MarkerType` の定数とは無関係である。1 つの値につき新しい線を 1 本引いて
書き、新口で読み戻した:

| 書いた `style` | `EMarkerType` の名前 | 読める `MarkerType` | 分解（根 / 塗り / 台） | 一緒に入る `nAngle` |
| --- | --- | --- | --- | --- |
| `0` | `kMarkerFilledArrow` | **`0`** = `kArrowMarker` | 矢印 / 線色 / 平 | 15 |
| `1` | `kMarkerEmptyArrow` | **`256`** = `kNoFillMarker` | 矢印 / **無し** / 平 | 15 |
| `2` | `kMarkerOpenArrow` | **`1280`** = `kOpenBaseNoFillMarker` | 矢印 / 無し / **開** | 15 |
| `3` | `kMarkerFilledBall` | **`2`** = `kCircleMarker` | **丸** / 線色 / 平 | 0 |
| `4` | `kMarkerEmptyBall` | **`130`** = `kWhiteFillMarker｜kCircleMarker` | 丸 / **白** / 平 | 0 |
| `5` | `kMarkerSlash` | **`259`** = `kNoFillDimSlashMarker` | **スラッシュ** / 無し / 平 | 45 |
| `6` | `kMarkerCross` | **`260`** = `kNoFillDimCrossMarker` | **十字** / 無し / 平 | 45 |
| `7` 以上・負・範囲外 | — | **`2048`** = `kAngleBaseMarker` | 矢印 / 線色 / **角** | 15 |

- **`nAngle` も一緒に決まる。** 旧口に角度の引数は無いが、様式ごとの既定
  （矢印 15 / 丸 0 / スラッシュ・十字 45）が書き込まれる。**#108 で「全区間 `15`」と
  読めたのは、そこで振っていた様式がすべて矢印の仲間だったから**である。
- **VWFC の名前と、実際に入る `MarkerType` は言葉としては一致しない。**
  `kMarkerEmptyArrow`（白矢印）に入るのは**白塗り**ではなく**塗り無し**の `256`、
  `kMarkerEmptyBall`（白丸）に入るのは**白塗り**の `130` である。名前ではなく
  上の表の数値で読むこと。

### 範囲外は飽和でも下位ビットでもない——per-object は一律 `2048`

`20` / `50` / `100` / `126` / `127` / `128` / `129` / `255` / `256` / `259` / `260` /
`261` / `512` / `1024` / `1280` / `2048` / `3072` / `4096` / `16384` / `32767` /
`32768` / `65535` / `65536` / `-1` / `-2` の **25 点すべてで `2048`**（角台の矢印）
になった。

- **飽和（クランプ）ではない**——飽和なら有効範囲の端（`6` → `260`）になるはずである。
- **下位ビットだけでもない**——`127`/`128`/`255`/`256` はマスクの境目だが、そこで割れない。
- **剰余でもない**——`7` の倍数（`7` / `14` / `21`）で `0` に戻らない。
- **負の値も同じ `2048`** で、符号なしとして大きな値になっているわけでもない。

**つまり「0〜6 以外はすべて `2048`（線色塗りの矢印・角台）という 1 つの値になる」**
という、それ以上分解できない振る舞いである。#110 で `1280` を書いて `2048` が読めたのは、
`1280` が**範囲外だった**というだけのことだった。

### 文書の既定の旧口（`SetDefaultArrowHeadsN`）は**別の表**——`ArrowType` の enum 0〜4

**per-object と同じ呼び出し名・同じ型なのに、番号の意味が違う。** 文書の既定側は
`MiniCadCallBacks.h:756` の `ArrowType` の enum そのもの（`arArrow = 0`,
`arTightArrow = 1`, `arBall = 2`, `arSlash = 3`, `arCross = 4`）で解釈される:

| 既定へ書いた `style` | `ArrowType` の名前 | 既定・新口で読める `MarkerType` | そこから生まれた線 |
| --- | --- | --- | --- |
| `0` | `arArrow` | **`0`** = `kArrowMarker` | `0` |
| `1` | `arTightArrow` | **`1`** = `kConcaveCurvedArrowMarker` | `1` |
| `2` | `arBall` | **`2`** = `kCircleMarker` | `2` |
| `3` | `arSlash` | **`259`** = `kNoFillDimSlashMarker` | `259` |
| `4` | `arCross` | **`260`** = `kNoFillDimCrossMarker` | `260` |
| `5` 以上 | — | **`0`**（矢印） | `0` |

**範囲外の落ち先も per-object（`2048`）とは違って `0` である。** 生まれた線は既定と
同じ値を持つので、**既定が嘘をついているのではなく、本当にその値が書かれている**。

**同じ番号が 2 つの口で別の様式を指す。** 例えば `3` は、per-object では**丸**
（`kCircleMarker`）、文書の既定では**スラッシュ**（`kNoFillDimSlashMarker`）になる。
**旧口で様式を指定するなら、どちらの口を叩いているかで表を選び分けなければならない**
——だから旧口では指定しない、が実務上の答えである（下記「どう書くか」）。

### 旧口の `style` の**読み**は、どちらの体系でもない——使ってはいけない

`GetArrowHeadsN` / `GetDefaultArrowHeadsN` の `style` は、**書いた番号も
`MarkerType` も返さない**。per-object で 0〜19 を書いて読み直すと、
**`3` を書いたときだけ `2` が返り、残りはすべて `0`** だった（`1` を書いても `0`、
`2` を書いても `0`、`4`〜`19` も `0`）。新口で `MarkerType` を書いてから読んでも、
`0` / `1` / `2` の 3 つだけがそのまま返り、`259` / `260` / `261` / `6` / `7` / `11` /
`128` / `256` / `1280` / `2048` / `3072` / `32768` / `16384` は**すべて `0`** になる。

**`style=0` が返ってきても「矢印である」とは限らない。「読めなかった」も同じ `0` である。**
→ **様式を読むのは新口の `SMarkerStyle.style` か、中口の `GetMarker`**（下記）。

これは**大きさの読み**（`GetArrowHeadsN` の `size` が整数インチに丸まる。上記
「per-object の `GetArrowHeadsN` は大きさを整数インチに丸めて返す」）と同じ筋の話で、
**旧口の getter は読み戻しに使えない**——有無（`ending`）・大きさ・様式の 3 つとも嘘をつく。

### マスク（`kMarkerRootTypeMask` ほか）は `MarkerType` の値にだけ意味がある

`kMarkerRootTypeMask = 127` / `kMarkerFillMask = 896` / `kMarkerBaseMask = 7168` /
`kMarkerHalfTickMask = 24576` / `kMarkerTailMask = 32768` は、**新口と中口が返す
`MarkerType` に対してだけ**意味を持つ。旧口へ書く番号（0〜6）も、旧口が返す番号も
`MarkerType` ではないので、**同じマスクで割っても意味を成さない**（実際、旧口の読みは
ほぼ `0` なので、割ると何もかもが「矢印 / 線色 / 平」に見えてしまう）。

### 新口（`SetObjBeginningMarker`）は `MarkerType` をそのまま往復させる

**様式を指定する道はここである。** 21 個の `MarkerType` を書いて読み直し、
**17 個はそのまま往復した**（`0` / `1` / `2` / `6` / `7` / `11` / `128` / `256` /
`258` / `259` / `260` / `261` / `1280` / `2048` / `3072` / `16384` / `32768`）。

**往復しなかった 4 つは、いずれも `kNoFillMarker`（256）が自動で足された**もので、
**壊れているのではなく VW が補正している**:

| 書いた | 読めた | 何が起きたか |
| --- | --- | --- |
| `3` = `kDimSlashMarker` | `259` | `＋kNoFillMarker` |
| `4` = `kDimCrossMarker` | `260` | `＋kNoFillMarker` |
| `5` = `kLassoMarker` | `261` | `＋kNoFillMarker` |
| `12` = `kDoubleLineMarker` | `268` | `＋kNoFillMarker` |

ヘッダが「`kNoFillMarker` is the only valid setting for `kLassoMarker`,
`kDimSlashMarker`, `kDimCrossMarker`」と書いているとおりで、**複合定数
（`kNoFillDimSlashMarker = 259` など）を最初から渡せば往復する**。
`kDoubleLineMarker`（`268` になる）はヘッダのその注記に挙がっていないが、実測では
同じ扱いだった。

### 中口（`Set/GetMarker`）——読みは `MarkerType`、書きは `EMarkerType` の番号

`SetMarker(h, MarkerType style, short size, short angle, Boolean start, Boolean end)` /
`GetMarker(…)`（`Interfaces/VectorWorks/ISDK.h:1401` / `:941`）は、**角度を引数に持つ
唯一の口**である。ところが:

- **`GetMarker` の `style` は `MarkerType` そのもの**を返す（測った全区間で新口と一致）。
  **旧口と違って読みに使える。**
- **`SetMarker` の `style` は `MarkerType` ではない。** 宣言は `MarkerType` だが、
  実際に受けるのは**per-object の旧口とまったく同じ `EMarkerType` の番号 0〜6**
  （`0`→`0` / `1`→`256` / `2`→`1280` / `3`→`2` / `4`→`130` / `5`→`259` / `6`→`260`）。
  **`7` 以上と、`128` / `256` / `1280` / `2048` などの `MarkerType` 定数は、すべて `0`
  （線色塗りの矢印）になる**——旧口の範囲外が `2048` だったのに対し、こちらは `0`。

**`gSDK->SetMarker(h, kCircleMarker, …)` と書くと、丸ではなく「開いた矢印」（`1280`）に
なる**——`kCircleMarker` は `2` で、番号の `2` は `kMarkerOpenArrow` だからである。

- **`size` はポイントではなく 1/16384 インチである**（上記「正体: 大きさは 1/16384
  インチの 16 ビット整数」）。ポイントのつもりで `36` を渡すと 0.5 インチではなく
  **0.0022 インチ**になる。`0.2500` インチなら `4096`、`1.0000` インチなら `16384`。

### 角度（`nAngle`）——**書ける。ただし角度を持たない様式がある**

**「書けるのは中口だけ」と書いていたのは誤りだった**
（[#116](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/116) で訂正。
`probes/runtime/marker-angle-order/`。
[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/117#issuecomment-5823060642)）。
**新口（`SMarkerStyle.nAngle`）でも角度は書ける**——矢印の線へ `-128` / `-45` / `-1` /
`0` / `1` / `2` / `3` / `15` / `30` / `45` / `60` / `90` / `120` / `127` を書いて
**14 点すべてがそのまま読み戻せた**。

**#113 が 9 点すべてで `1` を読んだのは、そのプローブが角度を振るときの様式に
`kCircleMarker`（丸）を固定していたため**である。丸は、下の表のとおり
「新口で書くと角度が `1` になる」5 つの根のひとつだった。

| 口 | 角度を書けるか |
| --- | --- |
| **旧口**（`Set(Default)ArrowHeadsN`） | **書けない**（引数が無い）。様式ごとの既定が入る（矢印 15 / 丸 0 / スラッシュ・十字 45） |
| **新口**（`SMarkerStyle.nAngle`） | **書ける。ただし根が丸・投げ縄・矩形・二重線・`10` の様式では `1` になる** |
| **中口**（`SetMarker` の `angle`） | **書ける。丸にも書ける**（新口が潰す様式でも入る）——ただし**届く様式は番号 0〜6 の 7 つだけ** |
| **クラスの 2 つの口** | per-object とまったく同じ（下記「クラスの口も…」） |

#### 角度が入る根・入らない根（新口で `style` ＋ `dSize` ＋ `nAngle` を 1 回で書いた）

25 点の総当たり。**入らないのは 5 つの根**で、塗り・台・尾・半の変種は関係ない
（丸は `2` も `130`（＋白）も `258`（＋塗り無し）も同じく `1`）。

| | 根 |
| --- | --- |
| 角度が**入る** | `0` 矢印 / `1` 反り矢印 / `3` スラッシュ / `4` 十字 / `6` 六角 / `7` V / `9`（名前の無い根） |
| 角度が**入らない**（`1` になる） | **`2` 丸 / `5` 投げ縄 / `10`（名前の無い根） / `11` 矩形 / `12` 二重線** |

- **矢印の変種はすべて入る**——`128`（白）/ `256`（塗り無し）/ `1280`（開台）/
  `2048`（角台）/ `3072`（弧台）/ `16384`（半チック）/ `32768`（尾）で確かめた。
- **`8` を書くと `256`（矢印＋塗り無し）になる**——根 `8` は存在しない。`9` と `10` は
  そのまま読み戻せるので**名前の無い根が実在する**が、`10` は角度を持たない。
- **`nAngle` は `Sint8`**（`MiniCadCallBacks.h:841`）。`-128`〜`127` がそのまま入る。

#### 丸は「角度を持てない」のではない——**新口の書き込みが潰している**

同じ 14 点を、丸に対して**新口と中口の両方から**書いた:

| 書いた角度（14 点） | **新口** → 丸 | **中口**（番号 3 = 塗り丸）→ 丸 |
| --- | --- | --- |
| `0` / `1` / `2` / `3` / `15` / `30` / `45` / `60` / `90` / `120` / `127` / `-1` / `-45` / `-128` | **すべて `1`** | **すべてそのまま入る** |

矢印では両方とも往復する。つまり**丸の角度そのものは記録できる**のであって、
**新口の書き込みだけが `1` を書いている**。

> **丸のマーカーに角度が要るなら、中口（`SetMarker`）だけで書く。**
> **そのあと新口を叩くと角度は `1` に戻る**——様式や大きさを書き直しただけでもそうなる。
>
> **投げ縄（`261`）・矩形（`11`）・二重線（`268`）・根 `10` には、角度を書く道が無い。**
> 新口では `1` に潰れ、中口では**その様式へ届かない**（中口が受けるのは `EMarkerType` の
> 番号 0〜6 ＝ `0` / `256` / `1280` / `2` / `130` / `259` / `260` の 7 つだけ）。

- **中口の `angle` は `short` だが、行き先はこの `Sint8` である**——`180` を書くと
  `-76`（= 180 − 256）が読めた。**角度は `-128`〜`127` で渡すこと。**

### クラスの口も per-object とまったく同じ体系

`Set/GetClassBeginningMarker`（`SMarkerStyle`。`ISDK.h:734` / `:743`）と
`Set/GetClMarker`（`MarkerType` ＋ `size` ＋ `angle`。`:739` / `:748`）は、
**それぞれ新口・中口と同じに振る舞う**。**どちらも `visibility` の引数を持たない。**

| クラスの口 | `style` に**書く**と | 角度 |
| --- | --- | --- |
| `SetClassBeginningMarker` | **`MarkerType` そのもの**（6 点とも往復） | **書ける**（丸では `1`。per-object の新口と同じ） |
| `SetClMarker` | **`EMarkerType` の番号 0〜6**（`0`→`0` / `1`→`256` / `2`→`1280` / `3`→`2` / `4`→`130` / `5`→`259` / `6`→`260`。`7` 以上と `MarkerType` の定数はすべて `0`） | **書ける**（丸でも入る。per-object の中口と同じ） |

**`gSDK->SetClMarker(index, kCircleMarker, …)` は per-object の中口と同じ罠**
——`kCircleMarker` は `2` なので、丸ではなく「開いた矢印」（`1280`）になる。

- **クラスを作る口は `AddGuidesClass()`（`ISDK.h:1353`）だけ**である。
  **`ClassNameToID` は、新しい名前を渡してもクラスを作らない**（有効な番号が返らない。
  実測）。`ISDK.h` に `CreateClass` の類は無い。
- **クラスへ置いた値が線の絵に出るのかは、別の話**（下記「未確認のまま残っているもの」）。

### どう書くか（マーカーの様式・大きさ・角度を指定する）

```cpp
// --- ふつうの様式: 新口 1 回で様式・大きさ・角度をまとめて書く -----------
// MarkerType の定数をそのまま渡せる唯一の口。3 つとも同じ呼び出しで入る。
SMarkerStyle mstyle{};
Boolean visible = false;
gSDK->GetObjBeginningMarker(line, mstyle, visible); // 他の欄を壊さないよう読んでから
mstyle.style = kNoFillDimSlashMarker;               // ← 複合定数で渡す（259。3 だと補正される）
mstyle.dSize = 0.25;                                // インチ。上限 1.999939（= 32767/16384）
mstyle.nAngle = 45;                                 // 度。-128〜127。丸などでは 1 に潰れる
gSDK->SetObjBeginningMarker(line, mstyle, static_cast<Boolean>(true));
gSDK->SetObjEndMarker(line, mstyle, static_cast<Boolean>(true));

// --- 丸に角度が要るときだけ、中口ひとつで書く ---------------------------
// style は EMarkerType の番号（3 = 塗り丸 → kCircleMarker）。size は 1/16384 インチ
// （ポイントではない）。**このあと新口を叩くと角度が 1 に戻る。**
gSDK->SetMarker(line, /* 3 = 塗り丸 */ 3, /* size */ 4096 /* = 0.2500 インチ */,
                /* angle */ 45, static_cast<Boolean>(true), static_cast<Boolean>(true));

// --- 読み戻し: 新口か GetMarker。旧口の style は使わない -----------------
SMarkerStyle readBack{};
Boolean readVisible = false;
gSDK->GetObjBeginningMarker(line, readBack, readVisible);
const long root = readBack.style & kMarkerRootTypeMask; // マスクはここでだけ意味がある
```

**旧口（`Set(Default)ArrowHeadsN`）で様式を指定しないこと。** per-object と文書の既定で
番号の意味が違い、読み戻しもできないので、**書いた側が何を書いたか分からなくなる**。
旧口を使ってよいのは**有無を点ける向きだけ**である（上記「消すには新口の
`visibility=false` を使う」の復元手順）。

**中口を「角度のためだけ」に挟まないこと。** 中口は `style` と `size` も同時に書くので、
**様式は番号 0〜6 の 7 つへ落ち、大きさは 1/16384 インチで渡し直すことになる**。
角度が要るだけなら新口の `nAngle` で足りる（丸などの 5 つの根を除く）。

### 未確認のまま残っているもの

- **クラスへ置いたマーカーが、線の絵に本当に出るのか。** `SetClassBeginningMarker` で
  クラスへ様式・大きさ・角度を置き（読み戻せることは確認済み）、線をそのクラスへ入れて
  `SetArrowByClass(line)` まで呼んでも、**`GetMarkerPolys(line, …)` はマーカーの図形を
  返さなかった**（by-class の旗は `yes`）。同じ走りの対照では、**per-object で直に書いた
  線は図形を返す**（種別 21・境界 4.4901 × 4.4901）ので、**この口が壊れているわけではない**。
  「絵に出ていない」のか「`GetMarkerPolys` が by-class を映さないだけ」なのかは分けて
  いない——#116 の範囲外なので
  [#120](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/120)
  へ切り出した。**上記「マーカーが要るなら per-object の `SetArrowByClass` を呼ぶ」は、
  そこが片付くまで鵜呑みにしないこと。**
