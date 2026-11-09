# レイヤ・ストーリ・重ね順

デザインレイヤ／シートレイヤの列挙・重ね順・高さ・縮尺と、階（ストーリ）・レベルを SDK から
作って結ぶ手順（[#141](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/141)・
[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)・
[#194](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/194)・
[#221](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/221)・
[#223](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/223)・
[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)・
[#281](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/281)）。
測った環境・プローブ・実測の表とダンプは [計測記録](../Metrics/Layers%20and%20Stories.md)。

## デザインレイヤの並びと並べ替え

- **デザインレイヤは図面のオブジェクト列に並ぶ。** `VWDocument::GetDrawingHeaderFristMember`
  （綴りは SDK ママ）＋ `NextObject` で辿れ、**`InsertObjectAfter` / `InsertObjectBefore`**
  （VS の `HMoveForward` 相当）が**レイヤ重ね順を変える唯一の手段**。
- **並べ替えはビューポート生成より前に行う**（ビューポートは生成時の重ね順で描かれる）。
- **VW はストーリ従属のデザインレイヤを「レイヤ高さの降順」で並べる。** 作った順でも
  レベル種別の登録順でもない（希望順に作っても 27 枚中 26 枚が並べ替え対象になった）。
  **ストーリ非従属レイヤは作った順**で、ストーリ従属レイヤの背面へ回る
  （[実測](../Metrics/Layers%20and%20Stories.md#デザインレイヤの並びと並べ替え)）。
- **プラグインが入れた並べ替えは、その処理中に作ったビューポートの絵には届かない。** 図面の
  並びは正しく、利用者が「更新」を 1 回押すか開き直すと正しく描き直される。処理中の絵へ
  届かせる 4 通りは全滅した（もう一度やらない。[潰した道](../Metrics/Layers%20and%20Stories.md#デザインレイヤの並びと並べ替え)）。
  - 【注】当時の「`SetDirty(true)` を受け付けないビューポートがある」は測り直し（#245）で
    再現しなかった——**`SetDirty(true)` はどのビューポートでも立った**。取り違えと見られる
    【推定】。**印だけ立てて VW に描かせると絵の順序が直るかは確かめていない。**
  - SDK に遅延実行の口は無い（`RegisterDialogForTimerEvents`＝ダイアログのタイマーだけ）。
    周期的に呼んでもらう口の一覧は [周期実行と通知](Timers%20and%20Notifications.md)。
- **「動かせた数」を並べ替えの成否と読まない。** 既に希望どおりでも失敗しても 0 件。
  並びそのものを読み戻して確かめる。
- **【訂正】レイヤ高さを読む口も書く口も ISDK にある**（下記「レイヤの高さ」。当初の
  「取得する呼び出しが無い」は誤り）。

## レイヤを列挙する（デザインとシート・種類・順序・現在・縮尺）

VW 2026 / Windows で実測（[#281](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/281)。
[見本と手順](../Metrics/Layers%20and%20Stories.md#レイヤを列挙するデザインとシート種類順序現在縮尺)）。

- **シートレイヤも同じ列に並ぶ。** `GetDrawingHeader()`（型 32）→ `FirstMemberObj` →
  `NextObject` の 1 本の列にデザインとシートが混ざる。`GetDrawingHeaderFristMember()` も
  同じ先頭を返し、`ISDK::ForEachLayer` も両方を同じ順で配る。
- **【罠】列の末尾にレイヤでないもの（型 `0`・名前なし）が 1 つ居る**（空図面でも）。
  `ForEachLayer` はこれを配らない。**列を辿るなら `GetObjectTypeN(h) == kLayerNode`（31）で
  絞る。** 正体は【未確認】。
- ビューポートの注釈などは列に混ざらない（増えるのはシートレイヤの直下の数だけ）。
- **デザインとシートの見分けは `ovLayerType`(154) か `VWLayerObj::GetLayerType()`**（同じ値）。
  デザイン `1`（`kLayerDesign`）・シート `2`（`kLayerSheet`）。`Sint16` で受け取れる。
  `kLayerPresentation = 2`（旧名）・`kGuestLayerDesign = 3`【ヘッダ根拠】（`MiniCadCallBacks.h:4560`）。
  **`3` は参照で入ってきたデザインレイヤ**（下記）。
- **シートの名前（番号）は `GetObjectName`。`ovLayerExpandedSheetName`(162) はシートでだけ読め**、
  タイトルが空なら番号と同じ文字列を返す（タイトル入りは未測定）。デザインレイヤでは `false`。

### 参照で入ってきたレイヤ（種類 `3`）——同じ列に並び、名前に接頭辞が付く

`ISDK::CreateReference` で別図面を参照した実測（Windows。
[表](../Metrics/Layers%20and%20Stories.md#参照で入ってきたレイヤ種類-3同じ列に並び名前に接頭辞が付く)）。

- **参照元のデザインレイヤが全部、型 `31` のまま同じ列に入る**（自分のレイヤの後、末尾の型 0 の
  前）。`ForEachLayer` も配る。**型だけでは自分のレイヤと見分けられない**——
  `ovLayerType == 3` か `IsObjectFromReferencedFile(layer)` で見分ける（同じ結果）。
- **名前は `NNA#1_<元の名前>` に変わる。** `GetNamedLayer` は元の名前では `nullptr`、接頭辞付きで
  引ける。`#1` / `NNA` の由来は【未確認】。
- 現在のレイヤにでき（`SetCurrentLayer` → `GetCurrentLayer` と `==`）、縮尺も `GetLayerScaleN` で
  読める（参照元の縮尺を写すかは未確認）。`ovGuestLayerHasRelativePath`(163) は `false` だった。
- **参照で入ったレイヤの直下には図形が見えない**（型 0 だけ）。参照の中身は `FirstMemberObj` →
  `NextObject` では辿れない。
- 副次: `DeleteObject(layer, false)` の直後の列に型 `90` が 1 つ挟まった。型で絞れば害は無い。

### 順序——列は作った順。オーガナイザはデザインだけ逆に見せる

- **列はデザインもシートも作った順**（両者が入り混じる。タブごとに分けるなら `ovLayerType` で
  振り分ける）。**オーガナイザの `#` は、デザインレイヤだけ列の逆順、シートは同じ順**
  （[表](../Metrics/Layers%20and%20Stories.md#順序列は作った順オーガナイザはデザインだけ逆に見せる)）。
- 作った後に高さを変えても、オーガナイザを開閉しても列は変わらなかった（「高さの降順」は
  ストーリに結んだレイヤを作ったときの規則で、食い違わない）。

### 現在のレイヤ——ハンドルの `==` で判定してよい

- `GetCurrentLayer()` は列で辿ったハンドルと `==` で一致する（シートを現在にしても同じ）。
- **ハンドルは図面をまたいで持ち越さない**——閉じて新規を開くと、別のレイヤが前と同じアドレスで
  返った（[周期実行と通知](Timers%20and%20Notifications.md) の 4 の 8 と同じ現象）。列挙のたびに引き直す。

### 縮尺——1/50 は `50`。シートレイヤは常に `1` で、書いても効かない

- **`GetLayerScaleN` / `VWLayerObj::GetScale()` の値は縮尺の分母**（1/50 → `50`、1:1 → `1`）。
- **シートレイヤの縮尺は常に `1`**——`SetLayerScaleN(sheet, 50)` は黙って無視される。結果に
  出すなら `null` 扱いが正直（[表](../Metrics/Layers%20and%20Stories.md#縮尺150-は-50シートレイヤは常に-1-で書いても効かない)）。
- この図面（1:1 生まれの新規）ではデザインレイヤへの `SetLayerScaleN` が効いた——下記
  「`SetLayerScaleN` は黙って効かないことがある」の実例とは別の結果で、何が分けるかは不明。

### OS タイマーの刻みの中・図面 0 枚

- **刻みの中でも同じ列が取れる**（アドレスまで一致。undo イベントは開いていなかった）。
- **図面 0 枚でも落ちない。** `GetDrawingHeader()` / `GetDrawingHeaderFristMember()` /
  `GetCurrentLayer()` は `nullptr`、`ForEachLayer` は 1 度も呼ばずに戻る。**呼ぶ前に図面の有無を
  確かめる必要は無い。**
- 刻みの中の `OpenDocumentPath(nullptr, false)` は `true` で新規を開くが、次の刻みまで 30 秒
  VW が応答なしになった【推定: 新規を開いた後の何かの処理】
  （[実測](../Metrics/Layers%20and%20Stories.md#os-タイマーの刻みの中図面-0-枚)）。

### `objects`（直下の数）について

- 何も描いていないレイヤでも `FirstMemberObj` → `NextObject` の数は `1`（型 `0`）。数え方は
  [Layer Objects](Layer%20Objects.md) で決めた（終端の型 0 を除く）。

## レイヤの高さ（読み書きの 4 経路と、絶対Z の決まり方）

[#194](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/194) で実機確認済み
（VW 2026 / mac）。

### 書く口は 4 つあり、**すべて同じ箱に届く**

- **`VWLayerObj::SetElevation` / `SetObjectVariable(h, 157, …)` / VS `SetLayerElevation` /
  `SetLayerElevationN` のどれで書いても、`VWLayerObj::GetElevation`・
  `ovLayerHeightInCurrUnits`(157)・バウンド経路が同じ値を返す**
  （[表](../Metrics/Layers%20and%20Stories.md#書く口は-4-つありすべて同じ箱に届く)）。
  **読むだけなら `GetElevation` が一番短い。**
- `SetElevation` は `GS_Kludge` 1002、`GetElevation` は 1000（SDK 同梱の `VWLayerObj.cpp` を読んだ）——
  書きと読みでセレクタが違うので、届いたことは絶対Z で確かめた。
- **厚さ（ΔZ）まで書けるのは VS の `SetLayerElevation` / `SetLayerElevationN` だけ**
  （`ExecuteScript` 経由。`VWLayerObj::SetHeight` が `GS_Kludge` 1003 で厚さを書く【ヘッダ根拠】）。
- **`eStoryObjectBound_LayerWallHeight` は「高さ＋厚さ」**（1500＋2400 で 3900）。厚さ 0 だと
  高さと同値なので取り違えに気付きにくい。
- **ストーリ由来のレイヤの高さは「階の高さ＋階内相対Z」**（2699 − 40 = 2659）で、同じ箱に入る。

### オブジェクトの絶対Z は「**生まれたレイヤ**の高さ ＋ ローカルZ」

[Data Tags](Data%20Tags.md)「式から読める高さ」の前提
（[段ごとの表](../Metrics/Layers%20and%20Stories.md#オブジェクトの絶対z-は生まれたレイヤの高さ--ローカルz)）。

- **レイヤの高さはその上のオブジェクトの絶対Z を持ち上げ、後から変えても追随する。**
  ストーリ由来でも素のレイヤでも同じ。
- **`AddObjectToContainer` で別のレイヤへ移したオブジェクトは、移した時点の絶対Z を保ち、
  以後レイヤ高さに追随しない**（VW へ戻った後・undo イベントを閉じた後も同じ。#245）。
  **レイヤ面に置くなら `gSDK->SetCurrentLayer(layer)` してから作る。** `#IPZL#` / `#ZTBBL#`
  （レイヤ基準）に直接効き、`#IPZS#`（階基準）は掛からない。
- `#IPZS#` は「絶対Z − 階の高さ」なので、レイヤ面上の部材では階内相対Z（例 `-40`）が出る。

### ストーリに属するレイヤへ `SetElevation` を掛けてはいけない

- **レイヤ（と上の部材）だけ動き、`GetStoryElevation`・`GetStoryLevelElevation` は動かない**
  （VW へ戻った後も同じ。#245）。「階＋相対Z」と実際のレイヤ高さが食い違い、`#IPZS#` が
  **どこにも対応しない数**を返す（[表](../Metrics/Layers%20and%20Stories.md#ストーリに属するレイヤへ-setelevation-を掛けてはいけない)）。
- **階に属するレイヤの高さは、階（`SetStoryElevation`）かレベル（`SetStoryLevelElevation`）から
  動かす。**

## レイヤの縮尺（`SetLayerScaleN` は黙って効かないことがある）

- **読みは `GetLayerScaleN(h, double_gs&)`、書きは `SetLayerScaleN(h, double)`。** どちらも
  戻り値が `void` なので、**書いたら読み戻して確かめる。**
- **`SetLayerScaleN(layer, 50)` が何も起こさなかった実例がある**（[#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145)）。
  1/100 で始まった文書では 3 回とも効き、1:1 の新規の空図面では効かなかった。**原因は特定
  できていない**【未確認】（文書ごとに決まっているようには見える。
  [表](../Metrics/Layers%20and%20Stories.md#レイヤの縮尺setlayerscalen-は黙って効かないことがある)）。
  **変えた直後に `GetLayerScaleN` で確かめ、違っていたら止まる**——黙って効かないと以後の作図が
  全部旧縮尺で生まれ、症状はずっと後に出る（寸法の `ovDimFontSize` が 50 倍ずれる。[Dimensions](Dimensions.md)）。
- **ビューポートの縮尺は別物**で `ovViewportScale`(1003) に書く（[Dimensions](Dimensions.md)）。

## 階（ストーリ）を SDK から作る — レベルは**レベルテンプレート経由**で生やす

**階は SDK だけで作れる。** ただしレベル（＝レイヤ）を生やす道は 2 系統あり、効かないほうも
`true` を返す。実装例はホームズ君プラグインの `draw/Story`（`createStories` /
`CreateStoryLevelViaTemplate`）。手順（この順番に意味がある）:

1. `CreateLayerLevelType(type)` で**レベル種別を先に登録**（引数は非 const `TXString&`。以下も同じ）。
2. `CreateStory(name, suffix)`——**戻り値は `bool` でハンドルは返らない。**
3. **`GetNamedObject(name)` で階のハンドルを取る**（レベルの無い階はレイヤを持たないので
   `ForEachLayerN` ＋ `GetStoryOfLayer` では見つからない）。
4. **`SetStoryElevation(story, elevation)` をレベルを足す前に呼ぶ**（高さ 0 の階が複数になると
   次の `CreateStory` が衝突して失敗し得る。高さのためではない——下記）。
5. `CreateStoryLevelTemplate(name, scale, levelType, offset, wallHeight, index)` ＋
   **`AddStoryLevelFromTemplate(story, index)`** でレベルとレイヤを生やす。
6. `GetLayerForStory(story, levelType)` でレイヤを取り、`SetObjectName` で名前を直す
   （テンプレート経由だと suffix が付く。`1-FL` → `1-FL-1`）。

落とし穴（すべて実測）:

- **`AddStoryLevel` ＋ `AssociateLayerWithStory` では紐付かない**（VW 2026）。`AddStoryLevel` は
  `true` を返すのに `GetLayerForStory` は nil——**戻り値ではなく `GetLayerForStory` で判定する**
  （[Investigation Techniques](Investigation%20Techniques.md)「setter の戻り値を信用しない」）。
  `AssociateLayerWithStory` は種別 `''` のレイヤを弾く（下記）。
- **`Story Layer Template` と `Story Level Template` を取り違えない。** `CreateStoryLayerTemplate`
  は `true` で件数も増えるが、レイヤは生えない（[Parametric Objects](Parametric%20Objects.md)
  「ストーリを触る API」の落とし穴 2）。
- `ResetDefaultStoryLevels(false)` は `true` を返すが、レベルの無い階にレイヤを生やさない。
- `CreateStoryWithUI()` はダイアログが出るので無人の処理では使えない【ヘッダ根拠】。

> **この節は一度「できない」と書いて誤った**
> （[PR #66](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/66)）。
> 教訓: **「できない」と書く前に動いている実装を当たる。** 同じ名前で始まる API が 2 系統あるときは
> **両方試すまで「できない」と言わない。**

### レベルを持たないレイヤを階に結ぶ — **仮のレベル種別を与えて結び、外す**

[#221](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/221) で実機確認済み
（VW 2026 / mac。3 周の経緯と表は [計測記録](../Metrics/Layers%20and%20Stories.md#レベルを持たないレイヤを階に結ぶ--仮のレベル種別を与えて結び外す)）。

#### 結論: **未使用のレベル種別を先に与えれば結べ、結んだあとに種別だけ外せる**

```
CreateLayer(name, kLayerDesign);
SetLayerLevelType(layer, 仮の種別);      // その階で未使用の種別（CreateLayerLevelType で登録済み）
AssociateLayerWithStory(layer, story);  // → true。GetStoryOfLayer がその階を返す
SetLayerLevelType(layer, "");           // → true。階はそのまま、種別だけ '' に
```

- **`AssociateLayerWithStory` の失敗条件は「その階に同じ種別のレイヤが既に在る」で、種別 `''` の
  レイヤも弾かれる**（レベルを持たない階でも `false`）。**戻り値は実態と一致した。**
  一方 `SetLayerLevelType` は結ばれていないレイヤにも使用中の種別にも `true` を返す——
  setter の `true` を信用しない事例（[Investigation Techniques](Investigation%20Techniques.md)）
  （[表](../Metrics/Layers%20and%20Stories.md#結論-未使用のレベル種別を先に与えれば結べ結んだあとに種別だけ外せる)）。
- **結んだレイヤの高さは「階の高さ」**（相対Z を持たない）で、種別を外した後も
  `SetStoryElevation` に追随する。
- **バウンドの解決は基準のレベル付きレイヤと同値**で、種別を外した後も変わらない——宛先は
  入れ物の種別ではなく**バウンドに書いた種別**（`fLayerLevelType`）で決まり、入れ物から要るのは
  **階だけ**。`GetStoryObjectDataBoundHeight` も一致。

#### 同じ階に同じレベル種別のレイヤは 2 枚置けるか

- **`AddStoryLevelFromTemplate` を同じ階へ 2 回呼ぶと、同じ種別のレイヤが 2 枚属する**（どちらも
  `true`）。`AssociateLayerWithStory` では 2 枚目は `false`。テンプレート経由の 2 枚目で
  `GetLayerForStory` がどちらを返すかは決まらないので、**この状態を意図して作らない**
  （「レベル種別を共有する」案は採らない）。

#### 実際の使い方で回す——仮の種別は 1 つで足り、何枚でも結べる

- **外した種別はまた空く**——仮の種別 1 つで同じ階の 2 枚目も別の階も結べる。**レイヤの数だけ
  種別を作る必要は無い**（[表](../Metrics/Layers%20and%20Stories.md#実際の使い方で回す仮の種別は-1-つで足り何枚でも結べる)）。
- **種別 `''` のレイヤは同じ階に何枚あってもよい**が、`GetLayerForStory(階, '')` はそのうち 1 枚を
  返す——**種別 `''` でレイヤを引かない**（名前で引く）。
- オーガナイザでは「ストーリ」欄に階、「ストーリレベル」欄は空（目視）。
- **バウンドは基準と同値で、階を動かせば追随する。** レイヤ高さは階の高さなので、**この上で
  レイヤ面基準の図形を描くと、レベル付きのレイヤとは相対Z の分（例 40）ずれる。**
- **種別なしで結んだレイヤは高さの降順に並ばない**（作った順で、レベル付きのレイヤの背面）。
  **`InsertObjectAfter` は効く**ので、階の順に並べたいなら自分で並べる。
- ビューポートの表示レイヤ（`SetViewportLayerVisibility`）は、レベル付きのレイヤと差なし。
- **別の階へ結び直す**: 種別 `''` のままの `AssociateLayerWithStory` は `false`。**同じ手順
  （仮の種別 → 結ぶ → 外す）をもう一度回せば結び直せる。**
- **保存して開き直しても階・種別・高さ・並び・バウンドは保たれる。** ただし**開き直した図面では
  `GetNamedObject` でデザインレイヤが引けなかった**（nil。階は引けた）——**レイヤは
  `ForEachLayerN` かドローイングヘッダから辿って名前で突き合わせる。**
- 断面ビューポートにもレベル付きのレイヤの材と同じに映る（目視）。
- **undo（⌘Z。VW の自動記録）では、レベル付きもレベル無しも同じ壊れ方**——階と構造材が消え、
  レイヤは階を失って残る。高さは階を動かす前の値に戻る
  （[表](../Metrics/Layers%20and%20Stories.md#実際の使い方で回す仮の種別は-1-つで足り何枚でも結べる)）。

#### プラグイン自身の undo イベントの中で回すと、取り消しで**階・種別・テンプレートまで全部消える**

[#223](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/223) で実機確認済み
（VW 2026 / mac。`kUndoSwapObjects` ＋ `AddAfterSwapObject` で作ったレイヤを登録。
[表と手順](../Metrics/Layers%20and%20Stories.md#プラグイン自身の-undo-イベントの中で回すと取り消しで階種別テンプレートまで全部消える)）。

- **階・レベル種別（仮の種別も）・レベルテンプレート・クラスも、同じイベントの中で作っていれば
  一緒に消える**——取り消し後の図面は作る前と件数まで同じ（[Undo](Undo.md)「戻らないもの」の
  【訂正】）。仮の種別の後始末は要らない。レベル付き・レベル無しで差は無い。
- **登録しなかったレイヤは残り、階を失う**（高さは結んでいたときの値）。その上の構造材は
  登録していなくても消えた（VW 自身が記録していると読める【推定】）。**作ったレイヤは漏れなく
  登録する。**
- コマンドをまたいで取り消しても同じ（ただし別名保存が段を 1 つ積む。[Undo](Undo.md)
  「別名保存は取り消しの段を 1 つ積む」）。

#### 階に属さないレイヤの上では、`eStoryObjectBound_Story` が「そのレイヤの高さ」へ落ちる

- **階に属さないレイヤでは `fBoundStory` を 0 / 1 / −1 のどれにしても同じ値＝そのレイヤの高さ**
  になり、構造材の両端が同じ Z へ解けて **0 長に潰れる**
  （[表](../Metrics/Layers%20and%20Stories.md#階に属さないレイヤの上ではestoryobjectbound_story-がそのレイヤの高さへ落ちる)）。
- これは [Parametric Objects](Parametric%20Objects.md)「階やレベルを指すバウンドは
  `eStoryObjectBound_Story` で書く」への条件追加——「`_Story` はレイヤに依らず一定」は**入れ物の
  レイヤが階に属している場合**の話で、属さなければ `LayerElevation` と同じに落ちる
  （`fBoundStory` も `fLayerLevelType` も見ない）。
- **`GetObjectBoundElevation` は `true` も例外も返さずにこの値を返す**ので、**書き込み側で
  「階に属しているか」を確かめる。**

#### 階に属さないレイヤで、ほかに差が出たもの・出なかったもの

- 階を動かしても無関係・**並びは作った順でストーリ従属レイヤの背面**（高さを持たせても
  高さ順にはならない——「高さの降順」はストーリ従属レイヤの中の規則）・`InsertObjectAfter` は
  効く・`DeleteObject` で消える（階は残る）・ビューポートの表示レイヤは差なし
  （[表](../Metrics/Layers%20and%20Stories.md#階に属さないレイヤでほかに差が出たもの出なかったもの)）。

### レベルの高さは「階の高さ＋階内の相対Z」——SDK で作ったレベルもちゃんと持っている

[#141](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/141) で実機確認済み
（VW 2026 / mac。[4 通りの表と実物件の表](../Metrics/Layers%20and%20Stories.md#レベルの高さは階の高さ階内の相対zsdk-で作ったレベルもちゃんと持っている)）。

- **`GetStoryLevelElevation` / `SetStoryLevelElevation` が扱うのは階内の相対Z**（階の高さを
  含まない）。**絶対Z は「階の高さ＋相対Z」**（解き方は [Parametric Objects](Parametric%20Objects.md)
  「階やレベルを指すバウンド」）。
- **テンプレートの `elevationOffset` がそのまま相対Z になる。** `SetStoryLevelElevation` も
  相対Z を取る（絶対Z を渡す口ではない）。
- **`SetStoryElevation` を呼ぶ順は高さに効かない**（手順 4 の「先に呼ぶ」は名前の衝突を避ける
  ためだけ）。
- **`GetStoryLevelElevation` の `0` を「高さが無い」と読み違えない**（#141 の出発点の見当は外れ）。
  高さの有無は絶対Z で見る。
- **UI で作ったストーリも同じ持ち方**で、**相対Z は負にもなる**（`−40`）。

### レベル基準線が描くのは、この絶対Z である

- **レベル基準線が描く数値・レベルの絶対Z・レイヤの高さは、測った全行で一致した**（UI 製でも
  SDK 製のストーリでも。シートレイヤに置いた個体も同じ）——読むのは乗っているレイヤの Z ではなく、
  **名前で指したストーリレベルの絶対Z**
  （[表](../Metrics/Layers%20and%20Stories.md#レベル基準線が描くのはこの絶対z-である)）。
- **【訂正】`VWLayerObj::GetElevation` と `ovLayerHeightInCurrUnits`(157) がレイヤ高さを返す**
  （当初の「VS の `GetLayerElevation` に当たる ISDK の口は無い」は誤り）。
  `eStoryObjectBound_LayerElevation` を解かせる道も同じ値。レイヤ高さとレベルの絶対Z が
  **ずれ得るのかは確かめていない**【未確認】。
- **教訓: 2 つの数を比べるなら、同じ 1 回の実行で両方を測る**（別ファイルのログを見比べて
  「一致しない」と一度誤った）。

### 注釈に高さが出るかは、ビューポートで決まる

レベル基準線を注釈へ置いたとき、**数値が出るかは「どのビューポートの注釈か」だけで決まる**
（実物件の 29 件で実測。[表](../Metrics/Layers%20and%20Stories.md#注釈に高さが出るかはビューポートで決まる)）。

| 置き先 | 高さ | 名前 |
| --- | --- | --- |
| そのレベルのストーリ従属デザインレイヤ | **出る**（絶対Z） | 出る |
| シートレイヤへ直に（注釈ではない） | **出る**（絶対Z） | 出る |
| **UI が作った断面ビューポートの注釈** | **出る**（絶対Z） | 出る |
| SDK の `CreateSectionViewport` で作った断面ビューポートの注釈 | **表示レイヤとクラスを表示へ戻して更新すれば出る**（どちらかを欠くと `0`。[Viewports](Viewports.md)） | 出る |
| 伏図（平面）ビューポートの注釈 | **`0`** | 出る |

- **平面ビューポートの注釈では出ない**（`ovIsSectionViewport`（1054）が `false`）。
- **【訂正】SDK 製の断面で「全レイヤを表示にしても `0`」だったのは、クラスが非表示のままだった
  ため**（[#151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)）。
  差はビュー行列 `ovViewportViewMatrix`（1050）が単位行列のまま残ることで、**表示レイヤと
  クラスの両方**を戻して更新すると入る。#147 の「1055 を 1050 へ写す」は要らない——手順は
  [Viewports](Viewports.md)「SDK で作った断面ビューポートの注釈でレベル基準線に高さを出す」。
- **名前はどこでも出る**（`耐力壁-1` の形）。**`Elev` 欄（または `Elevation`）を読み戻して `0` で
  ないことを確かめ、`0` ならストーリではなくビューポートを疑う。**
- 潰した筋（ストーリの作り方・作り方の引数・断面線との結び付き）は
  [計測記録](../Metrics/Layers%20and%20Stories.md#注釈に高さが出るかはビューポートで決まる)。

## 打ち切った調査: ビューポート単位のレイヤ重ね順上書き

- **`SetViewportLayerStackingOverride` は書けない**（2 度試して確定）。毎回 `true` を返すのに
  `GetNumViewportLayerStackingOverrides` は 0 のまま。**デザインレイヤの並べ替え
  （`InsertObjectAfter`）が唯一の手段。**
- **読み出しはできる**（GUI で付けた上書きが 27 件読め、位置は 1 始まり・1 が最前面・全デザイン
  レイヤに振る）。形式が分かっていても書けない。潰した呼び方 3 通りと、`CreateViewportLayerOverride`
  （色・不透明度の族で別物）・レイヤの索引（`InternalIndex`）・オブジェクト変数・VectorScript を
  当たった記録は [計測記録](../Metrics/Layers%20and%20Stories.md#打ち切った調査-ビューポート単位のレイヤ重ね順上書き)。

> **教訓**: setter の戻り値を信用しない。この族は書けなくても true を返すので、
> **読み戻しで確かめる**しかない（`SetUseDocumentClassVis` も同様）。同じ形の setter に当たったら、
> まず GUI で作った実物を読んで「読める形式」と「書けるか」を切り分ける
> （[Investigation Techniques](Investigation%20Techniques.md)）。
