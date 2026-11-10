# 描画属性とクラス

オブジェクトの描画属性（ペン色・面色・線の太さ・線種・面パターン・マーカー・不透明度）を
「クラスに従わせる」ための口と、その**費用**。末尾に、図面のクラスの一覧を列挙する
手順（`ForEachClass`）。特記の無い限り VW 2026 / mac の実測。測った環境・プローブ・
実測の表・潰した筋は [計測記録](../Metrics/Attributes%20and%20Classes.md)。

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

- **`SetDefaultOpacityByClass()` の後も `false` を返すが、書き込みは効いている**（2 旗版
  `GetDefaultOpacityByClassN` は `pen=true fill=true`、その後に作ったものは 20/20 が by-class）。
  既定を確かめるなら `GetDefaultOpacityByClassN`。per-object の `GetOpacityByClass(h)` は正しい
  （[実測](../Metrics/Attributes%20and%20Classes.md#getdefaultopacitybyclass1-旗版の読み戻しの実測)）。
- **同じ形の穴がマーカーにもある**（書き込みも効かない）。一覧は下記
  「[読み戻しに使えない口](#読み戻しに使えない口ここまでに分かっているもの)」。

## 全属性をまとめて by-class にする ISDK の口は無い【ソース根拠】

- `SetAllAttributesByClass` も `AllAttr` も `SetClassByName` も、**SDK 全体（ヘッダ・同梱の
  実装ソース・`vs.py`）に 1 件も無い**。
- いちばん近い `VWFC::Tools::VWAutoClassing::SetAttrsByClass(MCObjectHandle)` は**7 つを順に
  呼ぶだけ**で、書き込み回数は減らない（`SDKLib/Source/VWSDK/VWFC/Tools/AutoClassing.cpp:344`。
  [実装](../Metrics/Attributes%20and%20Classes.md#全属性をまとめて-by-class-にする口を探したvwfc-のソースを読んだ)）。
- **「1 回で済ませる」道は無い。** 減らせるのは「呼ばずに済ませる」道だけ（次節）。

## 費用は「属性の書き込み」ではなく「それが起こす PIO の作り直し」に掛かる

- **by-class の書き込みそのものは無料。高いのは PIO を作り直させたときだけ**（構造材 PIO で
  1 呼び出し約 **9ms**、矩形・球は 0.00ms。
  [実測の表](../Metrics/Attributes%20and%20Classes.md#by-class-の属性の書き込みの費用の実測pio-と矩形)）。
- **効くのは「PIO かどうか」で、「実体を持つか」ではない**（球は 3D ソリッドでも 7 つとも無料）。
- **`SetArrowByClass`（マーカー）だけは PIO でも無料**——マーカーの by-class 化は作り直しを
  起こさない。「文書の既定で既に by-class だから」ではない（新規の図面では 7 つとも既定は `false`）。
- **費用は「書き込み」ではなく「状態の変化」に掛かる。** 既に by-class のものへ同じ書き込みを
  もう一度しても 0.00ms。**`SetObjectClass` も同額**（9.27ms）——クラスの割り当てそのものが
  作り直しを起こす。
- 作り直しは [Parametric Objects](Parametric%20Objects.md) の「リセット（再生成）をまとめられるか」
  と同じ機構で、**止める口もまとめる口も無い**。残る道は「作り直しを起こさせない」（次節）。

## 回数を減らす道は「文書の既定を先に立てる」（＝ per-object の 8 つを呼ばない）

- **`SetDefaultClass` と 7 つの `SetDefault*ByClass()` を作る前に立てると、生まれたものは
  最初からそのクラスに属し、属性も by-class になる**（PIO・矩形とも 20 / 20）。per-object の
  書き込みは要らない（マーカーを除く。下記。[実測](../Metrics/Attributes%20and%20Classes.md#文書の既定を先に立てper-object-の書き込みを呼ばずに作った実測)）。
- **既定を立てる費用は無視してよい**（8 回で 0.01ms）。クラスの数に比例するので、クラスが
  変わるたびに `SetDefaultClass` を呼び直す。**作成も速くなる**（PIO で 14.56 → 12.05ms/個）。
- **「作る前」でなければ効かない**（既定は生まれる瞬間に読まれる）。
- **マーカーだけは継承されない**（PIO 1/20・矩形 0/20）。正体は次節。マーカーは per-object の
  `SetArrowByClass` で付ける（無料）——**ただしそれだけでは絵に出ない**（下記
  「[クラスのマーカーは…](#クラスのマーカーはper-object-で有無を点けてはじめて描かれる)」。
  [#120](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/120)）。
- **他の 6 旗に巻き添えは無い**——大量生成の途中で高速化が崩れることはない。

### マーカーの既定を下ろしているのは「PIO の作り直し（regen）」——1/20 の正体

- **マーカーの既定の旗は、PIO の再生成で 1 回下りる。** 作成（`doRegen=true`）でも
  `ResetObject` でも下りる。`CreateCustomObjectPath(..., doRegen=false)` なら下りない。
  矩形の作成・`ResetObject` では下りない
  （[実測の表](../Metrics/Attributes%20and%20Classes.md#マーカーの既定の-by-class-が-120-で下りた件の切り分け)）。
- **`UpdateStyledObjects` でも下りるはず**【推定】（ジオメトリを作り直すため。直接は測っていない）。
- **旗は「立っている間に生まれた最初のもの」にだけ継がれ、その PIO 自身の再生成で下りる**——
  これが 1/20。矩形が先なら矩形が継ぐ。**他の 6 旗は全区間 `yes` のまま。**
- **VW へ戻った後も同じ**（undo イベントを閉じた後に読み直して変わった欄 0。VW が後から作り
  直して下ろすことは無い。[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）。
- 作る直前に毎回立て直せば毎回継ぐ（3/3）が毎回下りる。**per-object の `SetArrowByClass(h)`
  のほうが素直**（無料で図面の既定を触らない）。

### マーカーの既定 by-class は、意図して下ろせない

- **`SetDefaultArrowHeadsN` を書いても旗は下りない**（同じ値でも違う値でも。値そのものは
  書けている）。**下ろす道は「PIO を 1 つ再生成する」しか無く、後始末に使えない**
  （[実測](../Metrics/Attributes%20and%20Classes.md#マーカーの既定-by-class-を下ろす手を試した)）。

> **帰結: `SetDefaultArrowByClass()` を呼んではいけない。** 呼ぶと**その図面の既定を
> 元へ戻せない**（実測でも締めの後にマーカーの旗だけが残った）。継承もされない旗を、
> 戻せない代償で立てることになる。**マーカーが要るなら per-object の口を叩く**——ただし
> **`SetArrowByClass(h)` だけでは絵に出ない**（下記「クラスのマーカーは、per-object で有無を
> 点けてはじめて描かれる」。[#120](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/120)）。

### `SetObjectClass` だけでは属性は by-instance のまま（前提は今も正しい）

- 生成直後も `SetObjectClass` の直後も **7 つとも `false`**（PIO・矩形・球）。クラスを与える
  だけでは済まない——**済ませたいなら「文書の既定」を使う**。

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

## クラスのマーカーは、per-object で有無を点けてはじめて描かれる

**クラスへ値を置いて線へ `SetArrowByClass` を呼ぶだけでは、マーカーは 1 つも描かれない**
（5 回の再現・最後は目視でも確認。[#120](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/120)）。
絵に出す道は 1 つだけで、**クラスの値を読んで per-object の新口へ `visibility=true` で
書き写し、その後で `SetArrowByClass` を呼ぶ**——SDK 自身の参照実装
（`VWTaggedObj::CreateLeaderLine`。`Source/VWSDK/VWFC/Tools/VWTaggedObj.cpp:660` 付近）と同じ手順。

```cpp
// クラスのマーカーを線の絵に出す唯一の道。**順序が効く**（下記「順序が効く」）。
gSDK->SetObjectClass(line, classIndex);

SMarkerStyle fromClass{};
gSDK->GetClassBeginningMarker(classIndex, fromClass);
gSDK->SetObjBeginningMarker(line, fromClass, static_cast<Boolean>(true)); // ← 有無はここだけ
SMarkerStyle fromClassEnd{};
gSDK->GetClassEndMarker(classIndex, fromClassEnd);
gSDK->SetObjEndMarker(line, fromClassEnd, static_cast<Boolean>(true));

gSDK->SetArrowByClass(line); // ← 書き写した**後**に呼ぶ（先に呼ぶと旗が消える）
```

**理由は「有無（visibility）を持つ口が per-object にしか無い」ことである。**【ヘッダ根拠】

| 口 | 宣言 | 有無 |
| --- | --- | --- |
| per-object・新口 | `SetObjBeginningMarker(MCObjectHandle, SMarkerStyle, Boolean visibility)` | **ある** |
| per-object・中口 | `SetMarker(MCObjectHandle, MarkerType, short, short, Boolean start, Boolean end)` | **ある** |
| **クラス・新口** | `SetClassBeginningMarker(InternalIndex, SMarkerStyle)` | **無い** |
| **クラス・中口** | `SetClMarker(InternalIndex, MarkerType, short size, short angle)` | **無い**（`start` / `end` すら無い） |

VectorScript 側も同じで、`SetClassBeginningMarker(name, style, angle, size, width,
thicknessBasis, thickness)` / `SetClassEndMarker` / `SetClassArrow(className, style, size,
angle)` のどれにも有無の引数は無く、戻り値の `BOOLEAN` は「Return TRUE if operation was
successful」＝**成否**である。**クラス属性の口を全数当たっても**有無に当たるものは無い
（`GetClColor` / `GetClFillPat` / `GetClLineWeight` / `GetClMarker` / `GetClUseGraphic` /
`GetClVisibility` / `GetClPenPatN` ＋テクスチャとテキストスタイルだけ。`GetClVisibility` は
**クラス自体の表示・非表示**でマーカーとは無関係）。

### 物差しの校正——`GetMarkerPolys` は「描かれるか」を映す。`GetObjectBounds` は映さない

- **`GetMarkerPolys(object, startPoly&, endPoly&)`（`ISDK.h:851`）は有無に従う**——同じ値でも
  `visibility=false` の線では図形を返さない。**「描かれているか」を目視なしで知る唯一の口**
  （[校正の表](../Metrics/Attributes%20and%20Classes.md#物差しの校正getmarkerpolys-と-getobjectbounds)）。
- **線の `GetObjectBounds` はマーカーを含まない**（1.9999 インチのマーカーでも幅 0 のまま）。
  場所を計算するなら `GetMarkerPolys` の図形の境界を自分で足す。図形の種別はどの様式でも 21。
- **`ConvertToGroup` / `ConvertObjectToLines` はマーカーを映さない**（前者は素の線で `nil`
  ——シンボルや PIO のように「中身を持つもの」専用らしい。後者は線の芯だけを写す）。
  図形として取り出せるのは `GetMarkerPolys` だけ。

### クラス経由の道は、どれも描かれない（7 通り試した）

- **`GetArrowByClass` が `yes` でも絵には何も無い。** `ResetObject`・`SetClUseGraphic(true)`・
  クラス側を中口（`SetClMarker`）で書く・順序を逆にする、のどれでも出ない。出るのは
  per-object へ書き写した線だけ（[実測の表](../Metrics/Attributes%20and%20Classes.md#クラス経由の道を-7-通り試した)）。
- **`SetClUseGraphic`（クラスの「グラフィック属性を使用」）は関係ない。** VWFC の参照実装が
  この旗で `SetArrowByClass` を囲っているのは**ほかの 5 つの属性と揃えているだけ**で、
  マーカーが出る条件ではない。

### 目で見ても同じだった

- 4 本並べて目視でも、**クラスに入れて `SetArrowByClass` しただけの線は見えず**、書き写した線と
  per-object で直に書いた線だけが見えた——`GetMarkerPolys` の「図形なし」は「絵に出ていない」
  と読んでよい（[実測](../Metrics/Attributes%20and%20Classes.md#目視での突き合わせ)）。

### ただし by-class そのものは生きている——per-object が渡すのは**有無だけ**

- 書き写して `SetArrowByClass` を呼んだ線は、**クラスの値を後から変えると、クラスの新しい値で
  描き直される**（0.25 → 1.0 インチ）。**per-object への書き写しは「有無を点ける」ためだけ**で、
  値の出どころはクラスのまま（[実測](../Metrics/Attributes%20and%20Classes.md#per-object-で有無を点けた線でクラスの値を書き換えた実測)）。
- 書き写しただけ（旗を立てない）の線はクラスに追随しないはず【推定】（測っていない）。

### 順序が効く——per-object へ書き写すのは `SetArrowByClass` より**先**

- **`SetArrowByClass` の後に per-object のマーカーを書くと、by-class の旗が下りる**
  （`GetArrowByClass` が `no`）。図形は出るが per-object の値で、クラスに追随しない。
  **SDK の参照実装が「書き写し → `SetArrowByClass`」の順である理由がこれ**で、逆順では
  by-class にならない（[実測](../Metrics/Attributes%20and%20Classes.md#per-object-へ書き写す順序setarrowbyclass-の前後を変えた実測)）。

### by-class の線では、per-object の読みは信用できない

- 書き写していない by-class の線は `GetObjBeginningMarker` が `false`。書き写した線は読めるが
  **値が嘘**（`style=0` と読めてスラッシュが描かれていた。クラスを変えても dSize は旧値のまま）。
  **by-class の線のマーカーはクラスの口（`GetClassBeginningMarker`）で読む**
  （[実測](../Metrics/Attributes%20and%20Classes.md#by-class-の線で-per-object-の口を読んだ実測)）。

### 文書の既定から生まれた線は per-object の値を持つ（クラス経由だけが特別）

- **`SetDefaultBeginningMarker`（visibility=true）を立ててから生まれた線は、有無ごと per-object
  の値を持って描かれる**（[#94](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/94)）。
  詰まるのはクラス経由だけで、理由は「クラス側に有無が無い」の 1 点
  （[実測](../Metrics/Attributes%20and%20Classes.md#文書の既定から生まれた線の-per-object-の読み)）。

## 既定の by-class は「既定の値を書き戻す」と下りる（退避・復元できる）

前節の高速化には**後始末**が要る。**戻せる**——ただし「off にする口」ではなく、
**既定の値そのものを書く口**で戻す。

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
  `==` / `!=` も定義済み。**「ペン色だけを書く」口は無い。**
- **丸ごと退避・復元する口は無い**（`GetDefaultAttributes` 的なもの）。SDK 全体の
  `(Set|Get)Default*` はこの顔ぶれしかない（`sdk-grep` で全識別子を列挙して確認）。
- **マーカーの行は、この表のとおりには使えない。** 有無（`starting` / `ending`）は
  読めも消せもしないので、**新口（`Get/SetDefaultBeginningMarker` /
  `…EndMarker`）を使う**（下記）。`style` と `size` は旧口でも往復する。
- **VWFC の `VWObjectAttr` は、ハンドル無しで作ると文書の既定を読み書きする**
  （`fhObject == nil` のとき getter / setter が `*Default*` 系へ落ちる。
  `SDKLib/Source/VWSDK/VWFC/VWObjects/VWObjectAttributes.cpp`。例: `GetPenForeColor()` は
  ハンドルがあれば `GetColor(h, …)`、無ければ `GetDefaultColors(…)`）【ソース根拠】。
  **ただしここにも「旗を下ろす」口は無い**（`SetFillColorByClass()` はあるが逆が無い）。

### 既定の値を書くと、その属性の旗だけが下りる

- **対応は 1 対 1**（色だけが 1 対 2——`SetDefaultColors` 1 本でペン色と面色の両方が下りる）
  （[実測の表](../Metrics/Attributes%20and%20Classes.md#既定の値を-1-本ずつ書き戻したときの旗の実測)）。
- **同じ値を書いても旗は下りる**（退避した値そのものを書いて下りた）。
- **値は完全に往復する。旗を立てても値は壊れない**（旗と値は別に持たれている）。退避を
  「立てる前」に済ませ忘れても取り返せる（とはいえ、退避は先に済ませるのが素直）。

### 戻した後に作ったものは by-instance で生まれる

- 戻した後に作った矩形・構造材 PIO は **5 旗とも no** で、値は退避した既定と一致した。
  利用者が後で描くものは、取り込み前と同じ既定で生まれる。

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

- **元から by-class の図面を壊さないために、旗も退避して立て直す。** 値だけ書き戻すと、
  利用者が「クラスの属性を使用」にしていた図面を by-instance へ変えてしまう。
- **マーカー（`SetDefaultArrowByClass()`）はこの後始末に載らない**——立てたら戻せないので
  **そもそも立てない**（上記）。マーカーの「値」の退避・復元も旧口（`SetDefaultArrowHeadsN`）
  ではできず、**`SetDefaultBeginningMarker` / `SetDefaultEndMarker` を使う**（次節。
  [#106](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/106)）。

## 文書の既定のマーカーの有無は、旧口（`ArrowHeads`）では読めも消せもしない

**マーカーには口が 2 系統ある**（いずれも `Interfaces/VectorWorks/ISDK.h`）:

| | 文書の既定 | per-object | クラス |
| --- | --- | --- | --- |
| **旧口**（真偽値 2 つ） | `Get/SetDefaultArrowHeadsN` | `Get/SetArrowHeadsN(h, …)` | — |
| **新口**（`SMarkerStyle` ＋ `visibility`） | `Get/SetDefaultBeginningMarker` / `…EndMarker` | `Get/SetObjBeginningMarker` / `…ObjEndMarker` | `Get/SetClassBeginningMarker` / `…EndMarker` |

**文書の既定の旧口には穴が 2 つある。** どちらも by-class の旗を一度も立てていない図面で
出たので、`GetDefaultOpacityByClass` の穴とは**別物**である。

### 穴 1: `GetDefaultArrowHeadsN` の `ending` は、終点ではなく `starting` を返す

- **`ending` は終点の状態ではなく `starting` のコピー**（OR でもない）。新口と、直後に引いた
  線は書いたとおり。**文書の既定の終点の有無は旧口では読めない。** `starting` は正しい
  （[実測の表](../Metrics/Attributes%20and%20Classes.md#その-1-getdefaultarrowheadsn-の-ending-の読みの実測)）。

### 穴 2: `SetDefaultArrowHeadsN` は `starting` / `ending` の `false` を無視する

- **一度点けたマーカーはこの口では消せない。** `style` と `size` は往復するのに、真偽値は
  点ける向きにしか効かない（3 経路で一致——読みの嘘ではなく書き込みが無視されている。
  [実測](../Metrics/Attributes%20and%20Classes.md#その-2-setdefaultarrowheadsn-に-false-を書いた実測)）。

### 消すには新口の `visibility=false` を使う（これが退避・復元の道）

- `SetDefaultBeginningMarker(mstyle, false)` / `SetDefaultEndMarker(mstyle, false)` で
  **3 経路とも `no` になった。** 復元は「**新口で消してから、旧口で点け直す**」の 2 手——
  どちらの口も実測できている向きだけを使う
  （[実測](../Metrics/Attributes%20and%20Classes.md#新口の-visibilityfalse-で消した実測)）。

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

- **新口で「点ける」（`visibility=true`）のは、文書の既定では測っていない**【未確認】
  （per-object では効いた）。上の手順はその向きを使わない。
- **戻り値（`Boolean`）で成否を判定しない**——消えた回も消えなかった回も区別が付かなかった。
  [調査の作法](Investigation%20Techniques.md)のとおり**読み戻して確かめる**。

### per-object（`SetArrowHeadsN(h, …)`）は 2 つとも正しい

- **穴は文書の既定だけ。** per-object の `GetArrowHeadsN(h, …)` の `ending` は終点を正しく返し、
  `SetArrowHeadsN(h, no, no, …)` で消せる。**per-object の有無は旧口のままでよい**
  （大きさと様式は別。下記）。

### #104 の観測は、この 2 つで説明が付く

- [#104](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/104) の
  「`(yes, no)` が `(yes, yes)` と読めた」は穴 1、「`(no, no)` を書き戻しても `(yes, yes)`」は
  穴 2 と穴 1 の合わせ技。**by-class の旗とは無関係**だった（「by-class のとき getter が嘘を
  つく」筋は潰れた）。

### 読み戻しに使えない口（ここまでに分かっているもの）

| 口 | 何が起きるか | 代わりに使うもの |
| --- | --- | --- |
| `GetDefaultOpacityByClass()`（1 旗版） | 立てた後も `false` を返す（上記） | `GetDefaultOpacityByClassN(pen&, fill&)` |
| `GetDefaultArrowHeadsN` の `ending` | 終点ではなく `starting` を返す | `GetDefaultEndMarker(mstyle&, visibility&)` |
| `SetDefaultArrowHeadsN` の `starting` / `ending` | `false` が無視される（読みの問題ではない） | `SetDefaultBeginningMarker` / `SetDefaultEndMarker` |

旧口の getter は、ほかに**大きさ**（per-object で整数インチに丸まる）と**様式**（ほぼ常に `0`）
も嘘をつく（下記）。

### 余録: `style=0` は「マーカー無し」ではない

- **有無は `style` ではなく `visibility`（新口）で持たれる。** `0` は「無し」ではなく**矢印**
  （新口の `kArrowMarker = 0`、旧口の番号でも塗り矢印）。
- 以前ここにあった「`ArrowType` は `MarkerType` の体系【ヘッダ根拠】」は**誤り**で消した
  （`MiniCadCallBacks.h` の隣の enum を読み違えていた。正しい対応は下記「マーカーの様式は
  口ごとに別の番号体系」。[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)。
  [経緯](../Metrics/Attributes%20and%20Classes.md#余録-style0-で書いた実測)）。

### 余録: `GetObjBeginningMarker` が `false` を返したら、出力引数を読まない

- マーカーを一度も設定していない線では `false` が返り、出力引数は**図面の既定とも無関係な値**
  だった。設定後は `true`。「戻り値は値が入っているかを表す」は【推定】。

### per-object のマーカーの `size` が書いたとおりに読めないのは、読みが丸まるから

- 書き込みは正しい。**上限（旧口 1.8 インチ）と、per-object の旧口の読みの丸め（整数インチ）が
  重なった**だけ（[#108](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/108)。次節）。

## マーカーの大きさはインチで、上限があり、per-object の旧口の読みだけが丸まる

測った範囲は 0.0100〜12.0000 の 16 点・旧口 / 新口 / ポイント口の 3 経路
（[実測](../Metrics/Attributes%20and%20Classes.md#マーカーの大きさの単位上限読みの実測)）。

### 単位はインチ。図面の単位にも縮尺にも依らない

- **`Set/GetArrowHeadsN` の `size` も `SMarkerStyle.dSize` もインチ**（VWFC の換算
  `sizeInMilimeters = sizeInInch * 25.4` と、ポイント口で 1.0000 → **72** の両方で裏付け）。
- **図面の単位（mm）にも縮尺（1:1 / 1:50）にも引きずられない**——紙の上の寸法である。
- **`N` の付かない旧口はポイント**（`sizeInPoints`）。疑ったときの裏取りに使える。

### 上限がある——約 2.0 インチ（≈50.8mm）。書く口で切られ方が違う

- **旧口**（`SetArrowHeadsN` / `SetDefaultArrowHeadsN` / ポイントの `SetArrowHeads`）は
  **1.8000 インチ**（130 ポイント）で頭打ち。**新口**（`SetObjBeginningMarker` / `…EndMarker`）は
  **1.9999 インチ**で頭打ち。文書の既定も同じ上限。下は素直（0.0100 も入る）
  （[実測の表](../Metrics/Attributes%20and%20Classes.md#上限の実測書く口ごと)）。
- **2 インチより大きくする道は、測った口には無い。**

### 正体: 大きさは **1/16384 インチの 16 ビット整数**——だから上限が約 2 インチになる

- **中口（`Get/SetMarker`）の `size`（`short`）は 1/16384 インチ**（11 点すべてで
  `dSize = size ÷ 16384`。ポイントではない）。上限 1.999939 = 32767 ÷ 16384 は `short` の上限
  （[#116](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/116)。
  [実測の表](../Metrics/Attributes%20and%20Classes.md#中口の-size-を掃引した実測格納形式と上限)）。
- **新口の `dSize` も 1/16384 インチへ量子化される。** 文書の既定の「0.0100 → 0.0099」も同じ
  筋【推定】。
- **同じ `short` でも `Get/SetArrowHeads` は 1/72 インチ、`Get/SetMarker` は 1/16384 インチ。**
  中口へポイントのつもりで `36` を渡すと 0.0022 インチになる。

### per-object の `GetArrowHeadsN` は大きさを**整数インチに丸めて**返す（読みに使ってはいけない）

- **四捨五入して整数インチ**（0.25 以下は 0、0.5〜1.25 は 1、1.5 以上は 2）。丸めるのは
  per-object だけで、`GetDefaultArrowHeadsN` は書いた値を返す。書き込みは壊れていない
  （[実測の表](../Metrics/Attributes%20and%20Classes.md#per-object-の-getarrowheadsn-の大きさの読みを新口と比べた実測)）。
- ポイント口の値を 72 で割って丸めているらしい【推定】。

> **per-object のマーカーの大きさを読むなら `GetArrowHeadsN` を使わない。**
> `GetObjBeginningMarker` / `GetObjEndMarker` の `dSize`（インチ）か、`GetArrowHeads` の
> `short`（ポイント）で読む。**書くのも新口**にする（上限が 1.8 → 2.0 インチと広い）。
> **有無は per-object なら旧口のままでよい。大きさだけが別の話である。**

### per-object の `size=0.0000` は「未設定」と「0.5 インチ未満」の両方を意味する

- 旧口の `0.0000` だけでは区別が付かない。**区別は新口の戻り値で付く**（`no` なら未設定）。
  `0` は本当に 0 として書ける（[実測](../Metrics/Attributes%20and%20Classes.md#per-object-の旧口で-size00000-が読めた線)）。

### `dWidth` は書いても入らない

- `dWidth`・`nThicknessBasis`・`dThickness` は入らない。**`SMarkerStyle` で効くのは `style` /
  `nAngle` / `dSize` の 3 つだけ。**

### 様式（根種別）を変えても、大きさの振る舞いは変わらない

- 4 つの様式で上限も丸めも同じ。ただし**旧口で振った `style` の番号は `MarkerType` ではない**
  （次節。[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)）。

## マーカーの様式は口ごとに別の番号体系——`ArrowType` は `MarkerType` ではない

**`SetArrowHeadsN` / `SetDefaultArrowHeadsN` / `SetMarker` / `SetClMarker` の `style` に
`MarkerType` の定数（`kCircleMarker` など）を渡してはいけない。** `MarkerType` をそのまま
**書ける**のは新口（`Set…BeginningMarker` / `Set…EndMarker`——クラスのものも含む）だけ
（[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)。
[実測](../Metrics/Attributes%20and%20Classes.md#マーカーの様式の番号を口ごとに書いて読んだ実測)）。

| 口 | 呼び出し | `style` に**書く**と | `style` を**読む**と |
| --- | --- | --- | --- |
| **旧口・per-object** | `Set/GetArrowHeadsN(h, …)` | **`EMarkerType` の番号 0〜6**（VWFC の並び） | **どちらの体系でもない。ほぼ常に `0`——読みに使えない** |
| **旧口・文書の既定** | `Set/GetDefaultArrowHeadsN` | **`ArrowType` の enum 0〜4**（`arArrow`…`arCross`） | 同上（0〜2 だけ返る） |
| **中口** | `SetMarker(h, …)` / `GetMarker(h, …)` | **`EMarkerType` の番号 0〜6**（宣言は `MarkerType` だが嘘） | **`MarkerType` そのもの**（新口と一致） |
| **新口** | `Set/GetObjBeginningMarker`・`…EndMarker`・`…Default…` | **`MarkerType` そのもの**（往復する） | **`MarkerType` そのもの** |
| **クラス・新口型** | `Set/GetClassBeginningMarker`・`…EndMarker` | **`MarkerType` そのもの**（往復する） | **`MarkerType` そのもの** |
| **クラス・中口型** | `Set/GetClMarker(index, …)` | **`EMarkerType` の番号 0〜6**（中口と同じ罠） | **`MarkerType` そのもの** |

### per-object の旧口（`SetArrowHeadsN`）は `EMarkerType` の番号 0〜6

- `EMarkerType`（`VWFC/VWFCLibrary.h:62`。`kMarkerFilledArrow = 0` … `kMarkerCross = 6`）の番号で、
  `0`→`0` / `1`→`256` / `2`→`1280` / `3`→`2` / `4`→`130` / `5`→`259` / `6`→`260` の `MarkerType` に
  なる。`nAngle` も様式ごとの既定（矢印 15 / 丸 0 / スラッシュ・十字 45）が入る（#108 で
  「全区間 `15`」と読めたのは、振っていた様式がすべて矢印の仲間だったから）
  （[対応表](../Metrics/Attributes%20and%20Classes.md#per-object-の旧口setarrowheadsnに番号を書いた実測)）。
- **VWFC の名前と入る値は言葉として一致しない**（`kMarkerEmptyArrow` は塗り無し `256`、
  `kMarkerEmptyBall` は白塗り `130`）。名前ではなく数値で読む。

### 範囲外は飽和でも下位ビットでもない——per-object は一律 `2048`

- **0〜6 以外は負も含め一律 `2048`**（`kAngleBaseMarker`。25 点で確認）。飽和・下位ビット・
  剰余のどれでもない。#110 で `1280` を書いて `2048` が読めたのは、`1280` が範囲外だった
  というだけのこと。

### 文書の既定の旧口（`SetDefaultArrowHeadsN`）は**別の表**——`ArrowType` の enum 0〜4

- `MiniCadCallBacks.h:756` の `ArrowType`（`arArrow = 0` / `arTightArrow = 1` / `arBall = 2` /
  `arSlash = 3` / `arCross = 4`）で解釈され、`0` / `1` / `2` / `259` / `260` になる。**範囲外は `0`**。
  生まれた線も同じ値を持つので、既定の読みの嘘ではなく本当にその値が書かれている
  （[対応表](../Metrics/Attributes%20and%20Classes.md#文書の既定の旧口setdefaultarrowheadsnに番号を書いた実測)）。
- **同じ番号が 2 つの口で別の様式を指す**（`3` は per-object で丸、既定でスラッシュ）。
  **だから旧口では様式を指定しない。**

### 旧口の `style` の**読み**は、どちらの体系でもない——使ってはいけない

- `GetArrowHeadsN` / `GetDefaultArrowHeadsN` の `style` は**ほぼ常に `0`**（`0` / `1` / `2` だけ
  返ることがある）。**`0` は「読めなかった」とも区別できない。** 様式は新口の
  `SMarkerStyle.style` か中口の `GetMarker` で読む。**旧口の getter は有無・大きさ・様式の
  3 つとも読み戻しに使えない。**

### マスク（`kMarkerRootTypeMask` ほか）は `MarkerType` の値にだけ意味がある

- `kMarkerRootTypeMask = 127` / `kMarkerFillMask = 896` / `kMarkerBaseMask = 7168` /
  `kMarkerHalfTickMask = 24576` / `kMarkerTailMask = 32768` は、**新口と中口が返す値にだけ**使う。

### 新口（`SetObjBeginningMarker`）は `MarkerType` をそのまま往復させる

- **様式を指定する道はここ。** 21 個中 17 個がそのまま往復し、残る 4 つ（`3` / `4` / `5` /
  `12`）は VW が `kNoFillMarker`（256）を足して補正した。**複合定数（`kNoFillDimSlashMarker = 259`
  など）を最初から渡せば往復する**（[実測](../Metrics/Attributes%20and%20Classes.md#新口setobjbeginningmarkerで-markertype-を往復させた実測)）。

### 中口（`Set/GetMarker`）——読みは `MarkerType`、書きは `EMarkerType` の番号

- `SetMarker` / `GetMarker`（`Interfaces/VectorWorks/ISDK.h:1401` / `:941`）。**`GetMarker` は
  `MarkerType` を返す**（読みに使える）。**`SetMarker` の `style` は宣言が `MarkerType` なのに
  `EMarkerType` の番号 0〜6 を受け、それ以外はすべて `0`**（旧口の範囲外 `2048` とは違う）。
- **`gSDK->SetMarker(h, kCircleMarker, …)` は丸ではなく「開いた矢印」（`1280`）になる。**
- **`size` は 1/16384 インチ**（0.25 インチなら `4096`、1.0 インチなら `16384`）。

### 角度（`nAngle`）——**書ける。ただし角度を持たない様式がある**

- **新口（`SMarkerStyle.nAngle`）でも書ける**（矢印で 14 点が往復。以前の「中口だけ」は誤りで
  [#116](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/116) で訂正。
  #113 が 9 点すべてで `1` を読んだのは、角度を振るときの様式を丸に固定していたため）。
  `nAngle` は `Sint8`（`MiniCadCallBacks.h:841`）で `-128`〜`127`。
- **新口で書くと、根が丸 `2`・投げ縄 `5`・`10`・矩形 `11`・二重線 `12` の様式では `1` になる**
  （[実測](../Metrics/Attributes%20and%20Classes.md#角度nangleを書いた実測)）。
- **旧口は角度を書けない**（様式ごとの既定）。**中口は丸にも書ける**が、届く様式は番号 0〜6 の
  7 つだけ。中口の `angle` は `short` だが行き先は `Sint8`（`180` → `-76`）。

#### 角度が入る根・入らない根（新口で `style` ＋ `dSize` ＋ `nAngle` を 1 回で書いた）

| | 根 |
| --- | --- |
| 角度が**入る** | `0` 矢印 / `1` 反り矢印 / `3` スラッシュ / `4` 十字 / `6` 六角 / `7` V / `9`（名前の無い根） |
| 角度が**入らない**（`1` になる） | **`2` 丸 / `5` 投げ縄 / `10`（名前の無い根） / `11` 矩形 / `12` 二重線** |

- 塗り・台・尾・半の変種は関係ない。`8` を書くと `256` になる（根 `8` は無い）。

#### 丸は「角度を持てない」のではない——**新口の書き込みが潰している**

> **丸のマーカーに角度が要るなら、中口（`SetMarker`）だけで書く。**
> **そのあと新口を叩くと角度は `1` に戻る**——様式や大きさを書き直しただけでもそうなる。
>
> **投げ縄（`261`）・矩形（`11`）・二重線（`268`）・根 `10` には、角度を書く道が無い。**
> 新口では `1` に潰れ、中口ではその様式へ届かない。

### クラスの口も per-object とまったく同じ体系

- `Set/GetClassBeginningMarker`（`ISDK.h:734` / `:743`）は新口と、`Set/GetClMarker`
  （`:739` / `:748`）は中口と同じに振る舞う。**どちらも `visibility` を持たない。**
  `SetClMarker(index, kCircleMarker, …)` も「開いた矢印」になる
  （[実測](../Metrics/Attributes%20and%20Classes.md#クラスの口で書いて読んだ実測)）。
- **クラスを作る口は `AddGuidesClass()`（`ISDK.h:1353`）だけ。** `ClassNameToID` は新しい名前を
  渡してもクラスを作らない（実測）。`ISDK.h` に `CreateClass` の類は無い。
- クラスへ置いた値は、per-object で有無を点けない限り描かれない（上記）。

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

- **旧口（`Set(Default)ArrowHeadsN`）で様式を指定しない**（口ごとに番号の意味が違い、読み戻しも
  できない）。旧口を使ってよいのは**有無を点ける向きだけ**（上記の復元手順）。
- **中口を「角度のためだけ」に挟まない。** 中口は `style` と `size` も同時に書くので、様式は
  7 つへ落ち、大きさは 1/16384 インチで渡し直すことになる。

## クラスの一覧は `ForEachClass` で列挙する——順序は作った順、ゲストは `NNA#<n>_` 付きで後ろに足される

図面のクラスを全部なめる口は `ISDK::ForEachClass(Boolean doGuestClasses,
std::function<void(MCObjectHandle h)>)`（`ISDK.h:2697`）ひとつ。VWFC の
`VWClass::ForEachClass(bool, std::function<void(const VWClass&)>)`（`VWClass.cpp:213`）は
これを包んで、型が `kClassDefNode`（94）のものだけを `GetObjectInternalIndex(h)` で
`VWClass` にして渡すだけ【ソース根拠】。以下は Windows・VW 2026 の実測
（[#280](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/280)。
[実測](../Metrics/Attributes%20and%20Classes.md#foreachclass-でクラスを列挙した実測)）。

### 渡ってくるもの

- **クラス 1 つにつき型 94 のハンドル 1 本。** `GetObjectInternalIndex(h)` が ID、
  `GetObjectName(h)` が名前で `ClassIDToName(id)` と同じ。`ClassNameToID` で往復する。
- **`IsObjectFromReferencedFile(h)` はゲストでも `false`**——参照由来の判定に使えない。
- **列挙されるのはアクティブな図面のクラスだけ。**

### 新規の図面の中身と ID の歯抜け

- 新規の空図面（テンプレート無し）は **「一般」（ID 2。`GetNoneClassID()` = `GetDefaultClass()` =
  `GetActiveClass()`）と「寸法」（ID 3。`GetDimensionClassID()`）の 2 つだけ**。
  `GetDimensionClassID()` を呼んでもクラスは増えない。`AddClass` の最初の 1 つは 11
  （[ID の表](../Metrics/Attributes%20and%20Classes.md#新規の図面の中身と-id-の歯抜け)）。
- **ID は詰まっていない**（`1..MaxClassID()` を回すと空文字が混ざる）。一覧は `ForEachClass` で。
- **`MaxClassID()` はゲストのクラスを含まない。**
- **無いクラス名の `ClassNameToID` は `0` ではなく `-1`。** `> 0` で判定する。

### 順序は ID の順（作った順）。名前順でもオーガナイザの順でもない

- `ForEachClass` は ID の順（＝作った順）で渡す。**オーガナイザ（クラスタブ）は名前の昇順**
  なので、画面と同じ並びが欲しいなら自分で名前で並べ替える。

### 階層名（`-` 区切り）は 1 つのクラス。親は作られない

- `AddClass("P280-子-孫")` は名前に `-` を含むクラスを 1 つ作るだけで、`P280` / `P280-子` は
  存在しない（`ClassNameToID` が `-1`）。オーガナイザの木の親は表示のためだけの節。
  木が要るなら名前を `-` で割って自分で組む。

### `doGuestClasses` は「参照している図面のクラス」を足す

- **`true` にすると、ファイル参照した図面のクラスが `NNA#<n>_<参照先での名前>` という名前で
  自分のクラスの後ろ（ID 順）に足される**（参照先の「一般」「寸法」も入る）。ゲストは
  この図面のクラス表に実在し、ID は `MaxClassID()` を超える
  （[実測の表](../Metrics/Attributes%20and%20Classes.md#doguestclasses-を立てて参照のある図面で列挙した実測)）。
- `<n>` は参照の番号と見られる【推定】（参照 1 つでしか測っていない）。
- **オーガナイザとナビゲーションパレットにゲストは出ない。** 画面と揃えるなら `doGuestClasses=false`。
- ゲストの見分けは「`false` の列挙に無い」か名前の `NNA#` 接頭辞（前者が確か）。
- デザインレイヤビューポートで参照した場合の顔ぶれは測っていない。

### タイマーの刻みから呼べる。図面 0 枚でも落ちない

- `SetTimer` の刻み（Windows）から呼べ、件数はそのときのアクティブな図面のもの。
- **図面を全部閉じた状態（`GetCurrentLayer()` が `nullptr`）でも落ちず 0 件**で戻る。
- 刻みの中で undo イベントが開いていることは無かった（[Undo](Undo.md)）。

### 書き方

```cpp
// 画面（オーガナイザ）と同じ顔ぶれ。順序は ID 順なので、要るなら名前で並べ替える
std::vector<std::pair<InternalIndex, TXString>> classes;
gSDK->ForEachClass(false, [&](MCObjectHandle h) {
	if (gSDK->GetObjectTypeN(h) != kClassDefNode)
		return;
	const InternalIndex id = gSDK->GetObjectInternalIndex(h);
	classes.emplace_back(id, gSDK->ClassIDToName(id)); // GetObjectName(h) と同じ
});

// 名前から引くときは -1 が「無い」
if (gSDK->ClassNameToID(name) > 0) { /* ある */ }
```
