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
  この 1/20 という半端な数の理由は分かっていない。
- **「作る前」でなければ効かない。** 既定はオブジェクトが生まれる瞬間に読まれるので、
  作った後に立てても既存のオブジェクトは変わらない。

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
gSDK->SetDefaultArrowByClass();    // 立つが継承はされない（下の 1 行が要る）

// 以後、オブジェクトを作るだけ。per-object の 7 つも SetObjectClass も呼ばない。
MCObjectHandle object = /* CreateCustomObjectPath など */;
gSDK->SetArrowByClass(object);     // マーカーだけ。無料
```

**効き目**: 1 オブジェクトあたり「9ms × 7」が消える。作り直しを起こす書き込みが
残らないため。
