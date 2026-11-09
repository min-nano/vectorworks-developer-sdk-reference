# 描画属性とクラス — 計測記録

結論は [Findings「Attributes and Classes」](../Findings/Attributes%20and%20Classes.md)。
ここには測った環境・プローブ・実測の表・潰した筋を置く。測ったのは特記の無い限り VW 2026 / mac の新規の空図面で、プローブ
（`probes/runtime/<名前>/`）は役目を終えて消してある。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

## 「クラスに従わせる」口は per-object と「文書の既定」の 2 段で揃っている【ヘッダ根拠】

宣言の対応表（per-object と文書の既定の 2 段）は [Findings](../Findings/Attributes%20and%20Classes.md#クラスに従わせる口は-per-object-と文書の既定の-2-段で揃っているヘッダ根拠)。
ここには実測で見つかった穴の記録だけを置く。

### `GetDefaultOpacityByClass()`（1 旗版）は読み戻しに使えない

`SetDefaultOpacityByClass()` を呼んだ**後**も `GetDefaultOpacityByClass()` は `false` を
返した。同じ状態で 2 旗版 `GetDefaultOpacityByClassN` は `pen=true fill=true` を返し、
**その後に作ったオブジェクトは 20/20 が不透明度 by-class** だった。per-object の
`GetOpacityByClass(h)` は正しく返した。

## 全属性をまとめて by-class にする ISDK の口は無い【ソース根拠】

- `SetAllAttributesByClass` も `AllAttr` も、**SDK 全体（ヘッダ・同梱の実装ソース・
  `vs.py`）に 1 件も無い**。`SetClassByName` も無い（どちらも VectorScript 風の名前で、
  `ISDK` の実名は `SetObjectClass` と Findings の表の 7 つ）。
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

## 費用は「属性の書き込み」ではなく「それが起こす PIO の作り直し」に掛かる

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

表に加えて観察したこと:

1. **球（3D ソリッドを持つ）は 7 つとも 0.00ms** だった。取り込み側の内訳は構造材 PIO で
   10ms、データタグで 0.3ms。
2. 同じ PIO の同じ 1 周で、他の 6 つが 9ms 掛かる横で `SetArrowByClass` だけが 0.00ms
   だった（800 倍以上の差）。
   - 潰した筋: 「マーカーだけ文書の既定で既に by-class だから無料なのだ」を確かめたところ、
     新規の空図面では**7 つとも既定は `false`** で、マーカーも例外ではなかった。
3. 既に by-class になっているオブジェクトへ**同じ書き込みをもう一度**すると、
   **7 つとも 0.00ms**（PIO でも）だった。
4. `SetObjectClass` も 9.27ms（他の 6 つと同額）だった。

## 回数を減らす道は「文書の既定を先に立てる」（＝ per-object の 8 つを呼ばない）

実測（既定を立ててから作り、**per-object の書き込みを 1 度も呼ばずに**読み戻した）:

| | 構造材 PIO | 矩形 |
| --- | ---: | ---: |
| 既定のクラスで生まれた数 | **20 / 20** | **20 / 20** |
| ペン色・面色・線の太さ・線種・面パターン・不透明度 | **各 20 / 20** | **各 20 / 20** |
| **マーカー** | **1 / 20** | **0 / 20** |
| 作る（既定を立てない場合） | 12.05ms/個（14.56ms） | 0.01ms/個（0.01ms） |

- 既定を立てる 8 回の呼び出しは合計 0.01ms だった。
- 作成は PIO で 14.56 → 12.05ms/個になった（既定を立てない場合との比較）。
- マーカー: `GetDefaultArrowByClass()` は `true` を返したが、生まれたオブジェクトは
  by-instance のままだった（PIO 20 個中 1 個、矩形は 0 個しか by-class にならなかった）。
  1/20 の切り分けは次節、他の 6 旗が下りないことも次節の表。

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

作成と作り直し（regen）を 3 手で切り分けた（どの手も、測る前に旗を立て直してから行った）:

| 何をした後 | マーカーの旗 |
| --- | --- |
| `CreateCustomObjectPath(..., doRegen=false)` で PIO を作る | **yes（下りない）** |
| その PIO へ `ResetObject` | **no（下りる）** |
| 矩形へ `ResetObject` | yes（下りない） |

**VW へ戻った後に読み直しても同じ**（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。
既定のテンプレートから開いた図面で #104 の手順を再現し、どの段の旗も旧走行と一致した
うえで、文書の 7 旗・各図形の 7 旗・PIO の子と外接を、undo イベントを閉じた後・戻った後に
読み直して変わった欄は 0）。とくに**旗を立て直してから `doRegen=false` で作り、
`ResetObject` を呼ばずに置いた PIO** は、戻った後も子 2 つ（型 0 と 11 だけ）・外接は空・
文書の旗は yes のままだった。

観察したこと:

1. `doRegen=false` で PIO を作ったときは旗は下りなかった。
2. 作成を伴わない PIO への `ResetObject` で旗が下りた。`UpdateStyledObjects` は測っていない。
3. 矩形への `ResetObject` では下りなかった。矩形 A を作った後も旗は yes だった。
4. 1 回の再生成で 1 回下りた。旗が継がれたのは「立っている間に生まれた最初の PIO」
   （と、それより前に作った矩形 A）で、その PIO 自身の作成で旗が下りた。#94 の表は
   PIO → 矩形の順で走らせており、PIO は 20 個中 1 個、その後の矩形は 20 個中 0 個だった。
5. 他の 6 旗（ペン色・面色・線の太さ・線種・面パターン・不透明度）は
   **全区間で `yes` のまま**だった。

**立て直した場合:** 「作る直前に `SetDefaultArrowByClass()` を呼ぶ」を 3 本続けたところ、
**3 本とも継ぎ、3 本とも直後に下りた**（3/3）。

### マーカーの既定 by-class は、意図して下ろせない

他の 5 つで旗が下りた手（「既定の値を書く」。下記「既定の by-class は…」）を
マーカーで試した。`SetDefaultArrowHeadsN` を書いても旗は下りなかった——
**同じ値でも違う値でも**である（同じプローブ。値そのものは書けていた——`size` は
`0.1250` → `0.2500` に変わった）。

| 何をした後 | マーカーの旗 |
| --- | --- |
| `SetDefaultArrowByClass()` を立て直した | yes |
| `SetDefaultArrowHeadsN`（**同じ値**） | **yes（下りない）** |
| `SetDefaultArrowHeadsN`（**違う値**） | **yes（下りない）** |

プローブの締めで他の 6 旗は走らせる前へ戻ったが、**マーカーの旗だけが
「走らせる前 = no / 締めの後 = yes」のまま残った**。

### `SetObjectClass` だけでは属性は by-instance のまま（前提は今も正しい）

生成直後も `SetObjectClass` の直後も、
**7 つとも `false`** のままだった（PIO・矩形・球のいずれでも）。

## クラスのマーカーは、per-object で有無を点けてはじめて描かれる

結論・書き方・有無を持つ口の対応表【ヘッダ根拠】は [Findings](../Findings/Attributes%20and%20Classes.md#クラスのマーカーはper-object-で有無を点けてはじめて描かれる)。

以下は実測（VW 2026 / mac / 新規の空図面。`probes/runtime/class-marker-drawn/`。
[#120](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/120)。
[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/121#issuecomment-5825309331)）。
線はすべて (x, 0)–(x, 1000) の縦線で、クラスには style=259（スラッシュ・塗り無し）・
dSize=0.2500・nAngle=45 を置いてある。

**5 回走らせて、下の表の数値はすべて同じだった**（独立した 5 回の再現）。
**最後は目視でも突き合わせてある**——下記「目で見ても同じだった」。

### 物差しの校正——`GetMarkerPolys` は「描かれるか」を映す。`GetObjectBounds` は映さない

`GetMarkerPolys(object, startPoly&, endPoly&)`（`ISDK.h:851`）が**値はあるが消えている線**
（per-object で `visibility=false`）で何を返すかを、値を持つ線・持たない線と並べて測った。

| 線 | 新口の読み | 図形（始 / 終） | 図形の境界 | 線そのものの境界 |
| --- | --- | --- | --- | --- |
| **対照**: 素の線 | ok=no vis=no | なし / なし | — | 0.0000 × 1000.0000 |
| per-object・**visibility=true** | ok=yes vis=yes style=259 dSize=0.2500 | **あり** / **あり** | 4.4901 × 4.4901 | 0.0000 × 1000.0000 |
| per-object・**visibility=false**（値はある） | ok=yes **vis=no** style=259 dSize=0.2500 | **なし** / **なし** | — | 0.0000 × 1000.0000 |
| per-object・visibility=true・dSize=**2.0000** | ok=yes vis=yes style=259 dSize=**1.9999** | **あり** / **あり** | **35.9199 × 35.9199** | 0.0000 × 1000.0000 |
| per-object・visibility=true・**矢印**（style=0） | ok=yes vis=yes style=0 dSize=0.2500 | **あり** / **あり** | **12.7000 × 6.3500** | 0.0000 × 1000.0000 |

- 同じ値を持つ 2 本で、`visibility=true` は図形を返し、**`visibility=false` は返さなかった**。
- 線そのものの境界（`GetObjectBounds`）は、**1.9999 インチ（≈50.8mm）のマーカーを
  点けても幅 0 のまま**だった。
- 図形の種別は**どの様式でも 21**。境界の数値: 0.2500 インチ = 6.3500mm を 45° に倒した
  スラッシュで 6.3500 ÷ √2 = **4.4901**、1.9999 インチで **35.9199**、矢印（45° ではない）は
  **12.7000 × 6.3500** ＝ 0.5 × 0.25 インチだった。
- `dSize=2.0000` が `1.9999` になるのは上限（下記「マーカーの大きさはインチで…」）。

「描かれているものを実体の図形に変える」系の口を 2 つ試した（2 インチ＝上限の塗り矢印を
per-object で点けた対照でも、どちらもマーカーを映さなかった）:

| 試した口 | 結果 |
| --- | --- |
| `ConvertToGroup(object, 0)`（`ISDK.h:2731`。「グループに変換」） | **素の線では `nil`**——4 件すべて |
| `ConvertObjectToLines(object, true, ConvertWireframeMode)`（`:2732`。「線に変換」） | レイヤの図形の数は 4 件とも同じだけ増え（2 → 3）、全体の境界も幅 0 のまま動かなかった |

### クラス経由の道は、どれも描かれない（7 通り試した）

| 線 | by-class の旗 | 新口の読み | 図形（始 / 終） |
| --- | --- | --- | --- |
| **対照**: 素の線 | no | ok=no vis=no | なし / なし |
| クラスに入れただけ | no | ok=no vis=no | なし / なし |
| **クラスに入れて `SetArrowByClass`** | **yes** | ok=no vis=no | **なし / なし** |
| 同じもの＋`ResetObject` | yes | ok=no vis=no | **なし / なし** |
| `SetClUseGraphic(true)` の後にクラス＋`SetArrowByClass` | yes | ok=no vis=no | **なし / なし** |
| クラスの値を**中口**（`SetClMarker`）で書いて＋`SetArrowByClass` | yes | ok=no vis=no | **なし / なし** |
| `SetArrowByClass` を**先**に、`SetObjectClass` を**後**に | yes | ok=no vis=no | **なし / なし** |
| **クラスから読んで per-object へ書き写す＋`SetArrowByClass`** | **yes** | ok=yes vis=yes | **あり / あり**（4.4901 × 4.4901） |
| 書き写すだけ（`SetArrowByClass` を呼ばない） | no | ok=yes vis=yes style=259 | **あり / あり**（4.4901 × 4.4901） |
| **対照**: per-object で直に書いた | no | ok=yes vis=yes style=259 | **あり / あり**（4.4901 × 4.4901） |

- `GetArrowByClass` が `yes` を返した線でも、図形は返らなかった。
- `ResetObject` の後も図形は返らなかった。
- `SetClUseGraphic`（クラスの「グラフィック属性を使用」）は、`AddClass` で作ったクラスでは
  初期値が `no` だった。`true` にしても図形は返らなかった。VWFC の参照実装は
  この旗で `SetArrowByClass` を囲っている。
- クラス側の口を新口から中口（`SetClMarker`）へ替えても変わらなかった。
- 順序を逆にしても変わらなかった。

### 目で見ても同じだった

`GetMarkerPolys` の結果を目視で突き合わせた。
同じ図面に 4 本並べて（塗りのある矢印・1 インチ）確かめたもの:

| 線 | 矢印は見えるか |
| --- | --- |
| **対照**: 何も書いていない素の線 | 見えない |
| **クラスに入れて `SetArrowByClass`** | **見えない** |
| クラスから読んで per-object へ書き写す＋`SetArrowByClass` | 見える |
| **対照**: per-object で直に書いた | 見える |

### ただし by-class そのものは生きている——per-object が渡すのは**有無だけ**

書き写して `SetArrowByClass` を呼んだ線を
残したまま、**クラスの値だけを**後から style=260（十字）・dSize=1.0000 へ書き換えた:

| 線 | クラスを変える前の図形 | 変えた後の図形 |
| --- | --- | --- |
| `SetArrowByClass` だけの線 | なし | **なし**（変わらず） |
| **書き写して `SetArrowByClass` を呼んだ線** | あり 4.4901 × 4.4901（= 0.25 インチ） | **あり 17.9605 × 17.9605**（= **1.0 インチ**） |

書き写して `SetArrowByClass` を呼んだ線は、per-object へ書き写した値（dSize=0.2500）
ではなく、**クラスの新しい値（dSize=1.0000）の大きさの図形**になった。
書き写しただけ（旗を立てていない）の線は、クラスを変えて測っていない。

### 順序が効く——per-object へ書き写すのは `SetArrowByClass` より**先**

`SetArrowByClass` を呼んだ後に per-object のマーカーを書くと、`GetArrowByClass` が
`no` に戻った（上の表の「`SetArrowByClass` の後に per-object へ書き写す」行）。図形は出た。
SDK の参照実装は「書き写し → `SetArrowByClass`」の順である。

### by-class の線では、per-object の読みは信用できない

- 何も書き写していない by-class の線は、旗が `yes` でも `GetObjBeginningMarker` が
  `false`（`ok=no`）を返した。
- 書き写した by-class の線では、`ok=yes vis=yes` の後ろで **`style=0`（矢印）と読めたが、
  描かれていたのはスラッシュ**（境界 4.4901 × 4.4901 は 45° のスラッシュのもの）だった。
  クラスを dSize=1.0000 へ変えた後も、**読みは dSize=0.2500 のまま**で図形だけが大きくなった。

### 文書の既定から生まれた線は per-object の値を持つ（クラス経由だけが特別）

既定のマーカーを立ててから生まれた線を読んだ（[#94](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/94)）:

| 線 | 新口の読み | 図形（始 / 終） |
| --- | --- | --- |
| `SetDefaultBeginningMarker`（visibility=true）を立ててから生まれた線 | ok=yes **vis=yes** style=259 dSize=0.2500 | **あり / あり**（4.4901 × 4.4901） |
| **対照**: 既定を戻した後（vis=no）に生まれた線 | ok=no vis=no | なし / なし |

## 既定の by-class は「既定の値を書き戻す」と下りる（退避・復元できる）

下ろす専用の口が無いこと・既定の値を読む／書く口の対応表【ヘッダ根拠】は
[Findings](../Findings/Attributes%20and%20Classes.md#下ろす専用の口は無いヘッダ根拠)。

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

表に加えて観察したこと:

1. 書いた属性の旗だけが下り、他は動かなかった。`SetDefaultColors` 1 本でペン色と面色の
   両方が下りた。
2. 上の表で書いたのは**退避した値そのもの**で、値は 1 つも変わっていない。それでも
   旗は下りた。
3. 書き戻した後に読み直した既定は、退避した値と**一致**した
   （`penFore=257 penBack=256 fillFore=257 fillBack=256 / lineWeight=2 penPat=2 fillPat=1`）。
4. `SetDefault*ByClass()` を 5 つ立てた**後**に読んだ既定の値も、立てる前と同じだった。

### 戻した後に作ったものは by-instance で生まれる

戻した後に作った**矩形と構造材 PIO**は、どちらも **5 旗とも no** で、値は**退避した既定と
一致**した（上記と同じプローブ）。

## 文書の既定のマーカーの有無は、旧口（`ArrowHeads`）では読めも消せもしない

測った口（いずれも `Interfaces/VectorWorks/ISDK.h`）。`ArrowHeads` 系が古い方で、
`SMarkerStyle` と **`visibility`（有無）を別々の引数で持つ**系統が別にある。

| | 文書の既定 | per-object | クラス |
| --- | --- | --- | --- |
| **旧口**（真偽値 2 つ） | `Get/SetDefaultArrowHeadsN` | `Get/SetArrowHeadsN(h, …)` | — |
| **新口**（`SMarkerStyle` ＋ `visibility`） | `Get/SetDefaultBeginningMarker` / `…EndMarker` | `Get/SetObjBeginningMarker` / `…ObjEndMarker` | `Get/SetClassBeginningMarker` / `…EndMarker` |

次の 2 つの穴は、どちらも**`SetDefaultArrowByClass()` を一度も呼んでいない図面**
（by-class の旗は全区間 `no`）で出た。

### 穴 1: `GetDefaultArrowHeadsN` の `ending` は、終点ではなく `starting` を返す

実測（VW 2026 / mac。組み合わせごとに `OpenDocumentPath(nullptr, false)` で新しい空の
図面を開き、真っさらな既定から測った。`probes/runtime/default-arrow-heads-false/`）。
**同じ状態を 3 経路で読んだ**——旧口・新口・**書いた直後に引いた線**:

| 旧口へ書いた値 | 旧口の読み | 新口の読み（始点 / 終点） | 直後に引いた線（始点 / 終点） |
| --- | --- | --- | --- |
| （触っていない） | start=no **end=no** | no / no | no / no |
| `(start=yes, end=no)` | start=yes **end=yes** | **yes / no** | **yes / no** |
| `(start=no, end=yes)` | start=no **end=no** | **no / yes** | **no / yes** |
| `(start=yes, end=yes)` | start=yes end=yes | yes / yes | yes / yes |

- 新口と線は 4 行とも書いたとおりで、互いに一致した。食い違ったのは
  旧口の `ending` だけで、その値は**毎回 `starting` と同じ**だった。
- 潰した筋: 「どちらかが点いていれば両方 `yes`」——3 行目 `(no, yes)` で旧口が
  `end=no` を返した。
- `starting` は 4 行とも書いたとおりだった。

### 穴 2: `SetDefaultArrowHeadsN` は `starting` / `ending` の `false` を無視する

上の各行のあと、続けて `(start=no, end=no)` を**`size` を変えて**書いた
（`size` が変わったかで、呼び出しが届いたかを見る）:

| 直前の実態（始点 / 終点） | `(no, no)` を書いた後の新口 | 直後に引いた線 | `size` |
| --- | --- | --- | --- |
| yes / no | **yes** / no | **yes** / no | 0.0472 → **0.0945**（書いたとおり） |
| no / yes | no / **yes** | no / **yes** | 同上 |
| yes / yes | **yes / yes** | **yes / yes** | 同上 |

`style` と `size` は書いたとおりに変わり、点いていた端は 3 経路とも点いたままだった。

### 消すには新口の `visibility=false` を使う（これが退避・復元の道）

同じ 3 つの状態から `SetDefaultBeginningMarker(mstyle, false)` /
`SetDefaultEndMarker(mstyle, false)` を呼ぶと、**3 経路とも `no` になった**
（旧口の読みも `start=no end=no` へ戻った）。

- 旧口で `(yes, no)` を書くと始点だけが点いた（穴 2 の表の ②）。
- 消えている端へ旧口で `false` を書いても消えたままだった（穴 1 の表の 1〜2 行目）。
- 文書の既定へ新口で `visibility=true` を書く向きは測っていない（per-object では点いた）。
- 戻り値（`Boolean`）は、`visibility=false` を書いて実際に消えた回も、消えなかった旧口の
  呼び出しも、区別が付かなかった。

### per-object（`SetArrowHeadsN(h, …)`）は 2 つとも正しい

線を 1 本ごとに新しく引いて同じ電池を通したところ、per-object では:

- `GetArrowHeadsN(h, …)` の `ending` は**終点を返した**（`(no, yes)` を書けば
  `start=no end=yes` と読めた）。
- `SetArrowHeadsN(h, no, no, …)` で**マーカーが消えた**（3 通りとも消えた）。
- 旧口を一度も通さず、新口だけで点けて消すこともできた。

### #104 の観測は、この 2 つで説明が付く

[#104](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/104)
で観測したのは「`(yes, no)` を書いたら `(yes, yes)` と読めた」「退避した `(no, no)` を
書き戻しても `(yes, yes)` のままだった」の 2 つ。上の 2 つの穴の表と突き合わせると:

- 1 つ目は**穴 1**の表の 2 行目と同じ並び（実際の終点は no）。
- 2 つ目は**穴 2**（始点の `false` が無視されて点いたまま）と穴 1（終点の読みが始点の
  コピー）の表と同じ並び。

当時は「by-class のときに getter が嘘をつくのでは」と疑ったが、旗を一度も立てていない
図面で同じ食い違いが再現した。

### 余録: `style=0` は「マーカー無し」ではない

実測では `style=0` のまま `visible` が `yes` にも `no` にもなった。`0` はどの口でも
**矢印**を指した（新口の `MarkerType` では `kArrowMarker = 0`、旧口の番号でも `0` は
塗り矢印。下記「マーカーの様式は口ごとに別の番号体系」）。

> **経緯:** この節には以前「`ArrowType` は `Sint32` で、値は `MarkerType` の体系である
> 【ヘッダ根拠】」と書いていたが、[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)
> の実測で誤りと分かり消した。`Kernel/API/MiniCadCallBacks.h` は 756 行目から
> `ArrowType`（`arArrow = 0` … `arCross = 4`）と `MarkerType`（`kArrowMarker = 0` …）の
> **2 つの体系を続けて**定義しており、**隣の enum を読み違えていた**。正しい対応の実測は
> 下記「マーカーの様式は口ごとに別の番号体系」。`Sint32` であることと、`0` が「無し」では
> ないことだけが合っていた。

### 余録: `GetObjBeginningMarker` が `false` を返したら、出力引数を読まない

**マーカーを一度も設定していない線では `false` が返り**、
そのとき `mstyle` / `visibility` には**その図面の既定とも無関係な値**が入っていた
（既定が `size=0.0472` の図面で `size=0.1250`）。**設定した後は `true`** に変わった。
戻り値の意味は #106 の問いではないので追っていない。

### per-object のマーカーの `size` が書いたとおりに読めないのは、読みが丸まるから

`SetArrowHeadsN(h, …, size=3.0000)` と書くと、読み戻しは旧口 `2.0000` / 新口 `1.8000`
だった。切り分けは [#108](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/108)
（下記「マーカーの大きさ」の上限の表と丸めの表）。

## マーカーの大きさはインチで、上限があり、per-object の旧口の読みだけが丸まる

実測（VW 2026 / mac / 新規の空図面。`probes/runtime/arrow-head-size/`。
書いた値を **0.0100〜12.0000 の 16 点で掃引**し、**旧口・新口・ポイント口の 3 経路**で
読み戻した。測ったのはプローブが自分で開いて閉じる空の図面の中だけ）。

### 単位はインチ。図面の単位にも縮尺にも依らない

- VWFC は `sizeInMilimeters = sizeInInch * 25.4` と換算している
  （`SDKLib/Source/VWSDK/VWFC/VWObjects/VWObjectAttributes.cpp:568-605`）。
  実機では `SetArrowHeadsN(h, …, 1.0000)` を書いた線を**ポイントの口**
  `GetArrowHeads(h, …, short& sizeInPoints)` で読むと **72** だった（1 インチ = 72 ポイント）。
  0.5000 → **36**、0.1250 → **9**。
- **測った図面の単位は mm**（`GetUnits` の `unitsPerInch=25.4` / `unitMark=mm`、
  `storedAccuracy=1`）で、**アクティブレイヤの縮尺は 100** だった。
- 縮尺を変えても読みは動かなかった（1:1 と 1:50 で、0.5000 → 0.5000 / 3.0000 → 1.8000 と
  まったく同じ）。
- `N` の付かない旧口の引数名は VWFC で `sizeInPoints`。

### 上限がある——約 2.0 インチ（≈50.8mm）。書く口で切られ方が違う

| 書く口 | 書いたとおり入る範囲 | 超えたとき |
| --- | --- | --- |
| **旧口** `SetArrowHeadsN(h, …)` / `SetDefaultArrowHeadsN` / `SetArrowHeads`（ポイント） | 〜**1.8000 インチ**（130 ポイント） | **1.8000 で頭打ち** |
| **新口** `SetObjBeginningMarker` / `SetObjEndMarker` | 〜**1.9999 インチ** | **1.9999 で頭打ち** |

- 旧口は **1.9000 を書いても 1.8000** になる。新口は 1.9000 がそのまま入り、
  2.0000 以上で 1.9999 に落ち着く（2.2000 でも 12.0000 でも 1.9999）。
- ポイント口でも同じところで切れた——144 ポイント（= 2 インチ）を書いて読みは **130**
  （= 1.8056 インチ。1.8 インチ = 129.6 の丸め）。
- 文書の既定も同じ上限（`SetDefaultArrowHeadsN` に 1.9000 → 既定は 1.8000）。
- 下は 0.0100 でも書いたとおり入った（文書の既定側だけ 0.0100 → **0.0099** の
  微小な量子化が出た）。
- 2 インチより大きい値が入る口は、測った口には無かった。

### 正体: 大きさは **1/16384 インチの 16 ビット整数**——だから上限が約 2 インチになる

**中口（`Get/SetMarker`）の `size` を掃引した**
（[#116](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/116)。
`probes/runtime/marker-angle-order/`。
[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/117#issuecomment-5823060642)）:

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

- 新口へ `2.0000` を書くと **`1.999939` = 32767 ÷ 16384** で止まり、中口で読むと `32767`
  だった。`1.9999` を書いた線は `32766`（= 1.999878）になった。
- 新口へ `0.0100` を書くと `0.010010`（= 164 ÷ 16384）が読み戻った。#108 が文書の既定側で
  見たのは「0.0100 → 0.0099」（0.0099 ≈ 162 ÷ 16384）で、per-object で測った今回は 164
  だった（丸め方までは合わせていない）。
- 中口へ `36` を渡すと **0.0022 インチ**になった（`Get/SetArrowHeads` では 36 ポイント
  = 0.5 インチ）。

### per-object の `GetArrowHeadsN` は大きさを**整数インチに丸めて**返す（読みに使ってはいけない）

同じ線を新口と旧口で読み比べた（#108 の出発点だった食い違い）:

| 書いた `size`（インチ） | 新口 `dSize`（正しい） | **旧口 `GetArrowHeadsN`** |
| --- | --- | --- |
| 0.0100 / 0.0472 / 0.1250 / 0.2500 | 書いたとおり | **0.0000** |
| 0.5000 / 0.7500 / 1.0000 / 1.2500 | 書いたとおり | **1.0000** |
| 1.5000 / 1.7500 / 1.8000（以上は 1.8000 で頭打ち） | 〃 | **2.0000** |

境目は **0.2500↔0.5000** と **1.2500↔1.5000**（0.5 は切り上がった）。

- 実測のポイント値（9 / 36 / 72 / 130）を 72 で割って四捨五入すると 0 / 1 / 1 / 2 で、
  旧口の読みにすべて一致する。
- `GetDefaultArrowHeadsN`（文書の既定）は書いた値をそのまま返した（0.0472 → 0.0472、
  1.2500 → 1.2500）。
- 旧口で書いた値は（1.8 インチの上限まで）新口では書いたとおりに読めた。

### per-object の `size=0.0000` は「未設定」と「0.5 インチ未満」の両方を意味する

| 線 | 旧口 `…N` の `size` | 新口の戻り値 | 新口 `dSize` |
| --- | --- | --- | --- |
| 引いた直後（何も書いていない） | 0.0000 | **no** | 0.1250（当てにならない） |
| 旧口で `0.1250` を書いた | **0.0000** | yes | 0.1250 |
| 旧口で `0.0000` を明示的に書いた | 0.0000 | yes | **0.0000** |

- 明示的に `0` を書くと `dSize=0.0000` で `visible=yes` だった。

### `dWidth` は書いても入らない

新口で `dSize=0.5000` と **`dWidth=0.2500`** を書いて読み直すと、`dSize` は `0.5000` に
なったが **`dWidth` は `0.0000` のまま**だった。`nThicknessBasis` と `dThickness` も
測った全区間で `0` / `0.0000` だった（`nAngle` は全区間 `15` で、振っていない）。

### 様式（根種別）を変えても、大きさの振る舞いは変わらない

`kArrowMarker` / `kCircleMarker` / `kDimSlashMarker` / `kOpenBaseNoFillMarker` の 4 つで
測って、**すべて** 0.5000 → 0.5000、3.0000 → 1.8000 だった。

ここで旧口へ書いた `style` の `0` / `2` / `3` / `1280` は、新口ではそれぞれ
`0` / `1280` / `2` / `2048` として読めた（番号体系の実測は下記「マーカーの様式は口ごとに
別の番号体系——`ArrowType` は `MarkerType` ではない」。
[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)）。

## マーカーの様式は口ごとに別の番号体系——`ArrowType` は `MarkerType` ではない

口ごとの書き・読みの対応表は [Findings](../Findings/Attributes%20and%20Classes.md#マーカーの様式は口ごとに別の番号体系arrowtype-は-markertype-ではない)。
実測（VW 2026 / mac。`probes/runtime/marker-style-mapping/`。
[#113](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/113)。
[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/115#issuecomment-5822033817)）。

### per-object の旧口（`SetArrowHeadsN`）は `EMarkerType` の番号 0〜6

`EMarkerType` は VWFC のヘッダにある並び（`VWFC/VWFCLibrary.h:62`。
`kMarkerFilledArrow = 0` … `kMarkerCross = 6`）。1 つの値につき新しい線を 1 本引いて
per-object の旧口で書き、新口で読み戻した:

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

- 旧口に角度の引数は無いが、`nAngle` には様式ごとの値（矢印 15 / 丸 0 / スラッシュ・
  十字 45）が入った。#108 で振っていた様式はすべて矢印の仲間で、そこでは「全区間 `15`」と
  読めた。
- `kMarkerEmptyArrow`（白矢印）に入ったのは塗り無しの `256`、`kMarkerEmptyBall`（白丸）に
  入ったのは白塗りの `130` だった。

### 範囲外は飽和でも下位ビットでもない——per-object は一律 `2048`

`20` / `50` / `100` / `126` / `127` / `128` / `129` / `255` / `256` / `259` / `260` /
`261` / `512` / `1024` / `1280` / `2048` / `3072` / `4096` / `16384` / `32767` /
`32768` / `65535` / `65536` / `-1` / `-2` の **25 点すべてで `2048`**（角台の矢印）
になった。

- 有効範囲の端（`6` → `260`）にはならなかった。
- `127`/`128`/`255`/`256`（マスクの境目）の前後で値が割れなかった。
- `7` の倍数（`7` / `14` / `21`）で `0` に戻らなかった。
- 負の値も同じ `2048` だった。

#110 で `1280` を書いて `2048` が読めたのも、この範囲外の振る舞いと同じだった。

### 文書の既定の旧口（`SetDefaultArrowHeadsN`）は**別の表**——`ArrowType` の enum 0〜4

文書の既定側へ同じ番号を書いた。読めた値は `MiniCadCallBacks.h:756` の `ArrowType` の enum
（`arArrow = 0`, `arTightArrow = 1`, `arBall = 2`, `arSlash = 3`, `arCross = 4`）の並びと一致した:

| 既定へ書いた `style` | `ArrowType` の名前 | 既定・新口で読める `MarkerType` | そこから生まれた線 |
| --- | --- | --- | --- |
| `0` | `arArrow` | **`0`** = `kArrowMarker` | `0` |
| `1` | `arTightArrow` | **`1`** = `kConcaveCurvedArrowMarker` | `1` |
| `2` | `arBall` | **`2`** = `kCircleMarker` | `2` |
| `3` | `arSlash` | **`259`** = `kNoFillDimSlashMarker` | `259` |
| `4` | `arCross` | **`260`** = `kNoFillDimCrossMarker` | `260` |
| `5` 以上 | — | **`0`**（矢印） | `0` |

範囲外の落ち先は `0` だった（per-object は `2048`）。生まれた線は既定と同じ値を持った。
`3` は per-object では丸（`kCircleMarker`）、文書の既定ではスラッシュ
（`kNoFillDimSlashMarker`）になった。

### 旧口の `style` の**読み**は、どちらの体系でもない——使ってはいけない

`GetArrowHeadsN` / `GetDefaultArrowHeadsN` の `style` を読んだ。per-object で 0〜19 を
書いて読み直すと、
**`3` を書いたときだけ `2` が返り、残りはすべて `0`** だった（`1` を書いても `0`、
`2` を書いても `0`、`4`〜`19` も `0`）。新口で `MarkerType` を書いてから読んでも、
`0` / `1` / `2` の 3 つだけがそのまま返り、`259` / `260` / `261` / `6` / `7` / `11` /
`128` / `256` / `1280` / `2048` / `3072` / `32768` / `16384` は**すべて `0`** になった。

### マスク（`kMarkerRootTypeMask` ほか）は `MarkerType` の値にだけ意味がある

ヘッダの値: `kMarkerRootTypeMask = 127` / `kMarkerFillMask = 896` / `kMarkerBaseMask = 7168` /
`kMarkerHalfTickMask = 24576` / `kMarkerTailMask = 32768`。旧口の `style` の読みは
ほぼ `0` だった（上記）。

### 新口（`SetObjBeginningMarker`）は `MarkerType` をそのまま往復させる

21 個の `MarkerType` を書いて読み直し、
**17 個はそのまま往復した**（`0` / `1` / `2` / `6` / `7` / `11` / `128` / `256` /
`258` / `259` / `260` / `261` / `1280` / `2048` / `3072` / `16384` / `32768`）。

**往復しなかった 4 つは、いずれも `kNoFillMarker`（256）が足されて読めた**:

| 書いた | 読めた | 何が起きたか |
| --- | --- | --- |
| `3` = `kDimSlashMarker` | `259` | `＋kNoFillMarker` |
| `4` = `kDimCrossMarker` | `260` | `＋kNoFillMarker` |
| `5` = `kLassoMarker` | `261` | `＋kNoFillMarker` |
| `12` = `kDoubleLineMarker` | `268` | `＋kNoFillMarker` |

ヘッダの注記は「`kNoFillMarker` is the only valid setting for `kLassoMarker`,
`kDimSlashMarker`, `kDimCrossMarker`」。複合定数（`259` / `260` / `261`）は上の 17 個のとおり
往復した。`kDoubleLineMarker`（`268` になる）はヘッダのその注記に挙がっていないが、
同じく `256` が足された。

### 中口（`Set/GetMarker`）——読みは `MarkerType`、書きは `EMarkerType` の番号

`SetMarker(h, MarkerType style, short size, short angle, Boolean start, Boolean end)` /
`GetMarker(…)`（`Interfaces/VectorWorks/ISDK.h:1401` / `:941`）は角度を引数に持つ。

- `GetMarker` の `style` は、測った全区間で新口と同じ `MarkerType` を返した。
- `SetMarker` の `style` は宣言が `MarkerType` だが、書いた値は**per-object の旧口と
  まったく同じ `EMarkerType` の番号 0〜6** として入った
  （`0`→`0` / `1`→`256` / `2`→`1280` / `3`→`2` / `4`→`130` / `5`→`259` / `6`→`260`）。
  **`7` 以上と、`128` / `256` / `1280` / `2048` などの `MarkerType` 定数は、すべて `0`
  （線色塗りの矢印）になった**（旧口の範囲外は `2048`）。
- `kCircleMarker`（`2`）を `SetMarker` へ渡すと、番号の `2`（`kMarkerOpenArrow`）として
  「開いた矢印」（`1280`）になった。
- `size` は 1/16384 インチだった（上記「正体: 大きさは 1/16384 インチの 16 ビット整数」）。
  `36` を渡すと **0.0022 インチ**、`0.2500` インチは `4096`、`1.0000` インチは `16384`。

### 角度（`nAngle`）——**書ける。ただし角度を持たない様式がある**

**経緯:** 以前は「書けるのは中口だけ」と書いていたが、
[#116](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/116) で測り直した
（`probes/runtime/marker-angle-order/`。
[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/117#issuecomment-5823060642)）。
**新口（`SMarkerStyle.nAngle`）で**矢印の線へ `-128` / `-45` / `-1` /
`0` / `1` / `2` / `3` / `15` / `30` / `45` / `60` / `90` / `120` / `127` を書いて
**14 点すべてがそのまま読み戻せた**。

#113 のプローブは角度を振るときの様式に `kCircleMarker`（丸）を固定しており、9 点すべてで
`1` を読んでいた。丸は、下の表の「新口で書くと角度が `1` になる」5 つの根のひとつだった。

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
- `8` を書くと `256`（矢印＋塗り無し）になった。`9` と `10` はそのまま読み戻せた。
- `nAngle` は `Sint8`（`MiniCadCallBacks.h:841`）。`-128`〜`127` がそのまま入った。

#### 丸は「角度を持てない」のではない——**新口の書き込みが潰している**

同じ 14 点を、丸に対して**新口と中口の両方から**書いた:

| 書いた角度（14 点） | **新口** → 丸 | **中口**（番号 3 = 塗り丸）→ 丸 |
| --- | --- | --- |
| `0` / `1` / `2` / `3` / `15` / `30` / `45` / `60` / `90` / `120` / `127` / `-1` / `-45` / `-128` | **すべて `1`** | **すべてそのまま入る** |

矢印では両方とも往復した。中口で丸へ角度を書いた後に新口を叩くと、様式や大きさを
書き直しただけでも角度は `1` に戻った。中口が受けるのは `EMarkerType` の番号 0〜6
（＝ `0` / `256` / `1280` / `2` / `130` / `259` / `260` の 7 つ）だけで、投げ縄（`261`）・
矩形（`11`）・二重線（`268`）・根 `10` へは中口から届かなかった。

- 中口の `angle` は `short` だが、`180` を書くと `-76`（= 180 − 256）が読めた。

### クラスの口も per-object とまったく同じ体系

`Set/GetClassBeginningMarker`（`SMarkerStyle`。`ISDK.h:734` / `:743`）と
`Set/GetClMarker`（`MarkerType` ＋ `size` ＋ `angle`。`:739` / `:748`）は、
それぞれ新口・中口と同じに振る舞った。どちらも宣言に `visibility` の引数は無い。

| クラスの口 | `style` に**書く**と | 角度 |
| --- | --- | --- |
| `SetClassBeginningMarker` | **`MarkerType` そのもの**（6 点とも往復） | **書ける**（丸では `1`。per-object の新口と同じ） |
| `SetClMarker` | **`EMarkerType` の番号 0〜6**（`0`→`0` / `1`→`256` / `2`→`1280` / `3`→`2` / `4`→`130` / `5`→`259` / `6`→`260`。`7` 以上と `MarkerType` の定数はすべて `0`） | **書ける**（丸でも入る。per-object の中口と同じ） |

- `SetClMarker(index, kCircleMarker, …)` は「開いた矢印」（`1280`）になった。
- `ClassNameToID` に新しい名前を渡してもクラスは作られなかった（有効な番号が返らない）。
  `ISDK.h` に `CreateClass` の類は無く、クラスを作る口として挙がるのは
  `AddGuidesClass()`（`ISDK.h:1353`）だった（ヘッダを読んだ）。

## クラスの一覧は `ForEachClass` で列挙する——順序は作った順、ゲストは `NNA#<n>_` 付きで後ろに足される

図面のクラスを全部なめる口は `ISDK::ForEachClass(Boolean doGuestClasses,
std::function<void(MCObjectHandle h)>)`（`ISDK.h:2697`）ひとつで、VWFC の
`VWClass::ForEachClass(bool, std::function<void(const VWClass&)>)`（`VWClass.cpp:213`）は
これを包み、渡ってきたハンドルの型が `kClassDefNode`（94）のものだけを
`GetObjectInternalIndex(h)` で `VWClass` にして渡す（ソースを読んだ）。以下は Windows・
VW 2026 の実測（[#280](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/280)）。

### 渡ってくるもの

- ハンドルは**クラス 1 つにつき 1 本**で、型は必ず 94。`GetObjectInternalIndex(h)` が
  クラスの ID、`GetObjectName(h)` が名前で、**`ClassIDToName(id)` と同じ文字列**、
  `ClassNameToID(名前)` で同じ ID へ戻る（全件で往復一致）。
- **`IsObjectFromReferencedFile(h)` は、参照先から来たゲストのクラスでも `false`** だった
  （下記「ゲスト」）。
- 図面を 2 枚開いて切り替えると、
  タイマーの刻みから呼んだ同じ呼び出しが 6 件 → 2 件に変わった。

### 新規の図面の中身と ID の歯抜け

新規の空図面（テンプレート無し）のクラスは **「一般」（なしのクラス）と「寸法」の 2 つだけ**。

| ID | `ClassIDToName` | 備考 |
| --- | --- | --- |
| 0, 1 | `""` | 空き |
| 2 | 一般 | `GetNoneClassID()` = `GetDefaultClass()` = `GetActiveClass()` |
| 3 | 寸法 | `GetDimensionClassID()`。**呼んでもクラスは増えない**（件数・`MaxClassID` とも不変） |
| 4〜10 | `""` | 空き（`AddClass` の最初の 1 つは 11 になった） |

- `1..MaxClassID()` を回して `ClassIDToName` で引くと空文字が混ざった。
- ゲストの ID は `MaxClassID()` を超えた（下記）。
- **無いクラス名の `ClassNameToID` は `0` ではなく `-1`** を返した。このプローブは最初
  「`== 0` なら無い」と判定していて、無いクラスを「ある」と読んだ。

### 順序は ID の順（作った順）。名前順でもオーガナイザの順でもない

`zz280-最後` → `AA280-最初` → `mm280` → `P280-子-孫` の順に `AddClass` すると、ID は
11〜14 になり、`ForEachClass` は **一般(2)・寸法(3)・zz280-最後(11)・AA280-最初(12)・
mm280(13)・P280-子-孫(14)** の順で渡した。`ClassIDToName` で ID 順に引いた並びと完全に
同じだった。

オーガナイザ（クラスタブ）は名前の昇順で並んだ（`AA280-最初`・`mm280`・`P280-子-孫`・
`zz280-最後`・一般・寸法。目視）。顔ぶれは 6 件で一致した。

### 階層名（`-` 区切り）は 1 つのクラス。親は作られない

`AddClass("P280-子-孫")` の後、
`ClassNameToID("P280")` も `ClassNameToID("P280-子")` も `-1`（無い）だった。列挙にも
`P280` / `P280-子` は現れない。

オーガナイザの階層表示とナビゲーションパレットは `P280` ▸ `子` ▸ `孫` の木に描くが、
`P280` と `子` の行は属性の欄が空だった（平らな表示に切り替えると `P280-子-孫` の 1 行に
なった。目視）。

### `doGuestClasses` は「参照している図面のクラス」を足す

ゲストのクラスを 2 つ（`G280-ゲスト` / `G280-親-子`）だけ足した図面を保存し、別の図面から
**オーガナイザの「ファイル参照」タブで参照を 1 つ足しただけ**（ビューポートは作らない）で
比べた。

| | `ForEachClass(false)` | `ForEachClass(true)` |
| --- | --- | --- |
| 自分のクラス（一般・寸法・280 の 4 つ） | 6 件（ID 2, 3, 11〜14） | 同じ 6 件・同じ順 |
| 参照先のクラス | 渡らない | **`NNA#1_一般`(18)・`NNA#1_寸法`(19)・`NNA#1_G280-ゲスト`(20)・`NNA#1_G280-親-子`(21)** が後ろに続く |
| `MaxClassID()` | 14 | 14（ゲストの ID 18〜21 はこれを超える） |

- ゲストのクラスの名前は `NNA#<n>_<参照先での名前>` で、`ClassIDToName` /
  `ClassNameToID` で往復した。参照先の「一般」「寸法」もゲストとして入った。
  参照は 1 つでしか測っていない（`<n>` は `1`）。
- 共通部分の順序は `true` / `false` で同じで、ゲストは自分のクラスの**後ろ**に渡った
  （ID の順）。
- オーガナイザ（すべてのクラス・平らな表示でも）とナビゲーションパレットには
  ゲストのクラスは出なかった（目視）。
- `IsObjectFromReferencedFile(h)` はゲストのハンドルでも `false` だった。
- デザインレイヤビューポートで参照した場合の顔ぶれは測っていない（ファイル参照だけで
  ゲストが入ることまで）。

### タイマーの刻みから呼べる。図面 0 枚でも落ちない

- OS のタイマー（`SetTimer`、Windows）の刻みから、呼び出しの最中（13 刻み）・呼び出しが
  戻った後（5 分で約 1000 刻み）とも普通に呼べ、件数はそのときのアクティブな図面のものだった。
- **図面を全部閉じた状態（`GetCurrentLayer()` が `nullptr`）で呼んでも落ちず、`true` /
  `false` とも 0 件**で戻った。図面 8 枚のときも 2 件（アクティブな新規図面のもの）。
- 刻みの中で undo イベントが開いていることは無かった（[Undo](../Findings/Undo.md)）。
