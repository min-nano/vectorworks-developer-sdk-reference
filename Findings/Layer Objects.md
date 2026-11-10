# レイヤの中のオブジェクトの列挙と数え方

レイヤの中の図形をどう辿り、どう数え、何を読めるか（[issue #282](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/282)）。
VW 2026（Build 872399）の mac と Windows で確認。測った環境・見本・実測の表は
[計測記録](../Metrics/Layer%20Objects.md)。

## まとめ: 画面の数に合わせて数える手順

- **情報パレットの「N 図形」に合うのは、レイヤの直下の図形の数**（グループ・シンボル・PIO・壁は
  中身を数えず 1 つ）。**オーガナイザには図形の数の列が無い**（下記 3）。
- **`FirstMemberObj` → `NextObject` で辿るなら、型 `0`（終端）と型 `90`
  （`kUndoPlaceholderNode`）を除く。** 除かないと、操作の履歴がある図面で多く出る（下記 2・4）。
- **`ForEachObjectN(allDrawing, …)` なら、どちらも渡されない**ので除かなくてよい。ただしレイヤごとに
  分けるなら `ParentObject` で分ける。シートレイヤの図形も混ざる（下記 7）。
- 列の並びは**先頭が最背面・末尾が最前面**（下記 1）。

## 1. 辿る順

- **`FirstMemberObj(layer)` → `NextObject` の順は、作った順と同じ。** `LastMemberObj(layer)` →
  `PrevObject` はそのちょうど逆。
- **`ForEachObjectN` がレイヤの直下の図形を渡す順も同じ**（`traverseWhat` を変えても同じ）。
- **先頭が最背面・末尾が最前面。** 「最前へ」で末尾へ、「最後へ」で先頭へ移り、元の位置には型 `90`
  の置き石が残る。番地は変わらない（mac。[実測](../Metrics/Layer%20Objects.md#1-辿る順)）。
- **レイヤの並びも同じ向き**（mac）。`GetDrawingHeader` の子は、デザインレイヤについてオーガナイザの
  `#` の逆順に並ぶ。シートレイヤも同じ列の中にある。

## 2. 列に混ざる、図形でないもの

- **終端（`kTermNode` = 型 `0`）が各レイヤの列の末尾に 1 つ入る**（[Dimensions](Dimensions.md)
  の既知のとおり）。空のレイヤでも 1 つある。**数えるときは除く。**
- **undo の置き石（`kUndoPlaceholderNode` = 型 `90`）がレイヤの直下にも入る**（mac）。
  **並べ替え・消去・複製の取り消しのたびに増える**（下記 4）。名前は空、`GetObjectBounds` は
  `false`。**数えるときは除く。**
  - 中まで辿るときも同じ（[Parametric Objects](Parametric%20Objects.md) の置き石と同じもの。
    壁の子にも出る。Windows）。
- **`kMarkedForRemoval = -128` という型の値がヘッダにある**【ヘッダ根拠】が、消した図形でも
  取り消した図形でも、この型では出なかった（mac）。

## 3. 入れ子と `ForEachObjectN` の `traverseWhat`

辿り方ごとの数は [計測記録の表](../Metrics/Layer%20Objects.md#3-入れ子と-foreachobjectn-の-traversewhat)。

- **`allDrawing = 3` / `allObjects = 6` はレイヤの直下だけ**を渡す（`FirstMemberObj` → `NextObject`
  と同じ数・同じ並び）。
- **`descendIntoGroups (32)` はグループの中だけ**に入る。**`descendIntoAll (64)` は PIO・壁・
  押し出しの子にも入る。** `descendIntoViewports (128)` を足してもビューポートの中には入らなかった。
  `descendIntoAuxLists (256)` はレイヤ外の内部の記録（数千件）を足すだけ。
- **シンボルのインスタンスは子を持たない**（`FirstMemberObj` が nil）。中身は定義（型 `16`）に
  あり、**定義はどのレイヤにも属さない**。`descendIntoAll` でもインスタンスの中には入らない。
- **`allObjects` は `allDrawing` にシンボル定義（とその中身）を足す**。`allObjectsAndResources = 7`
  は更に資源（クラス定義 `94` など）を足す。
- **`allSelected = 0` / `allEditable = 1` はアクティブレイヤの図形しか渡さない**（mac）。
  **`allVisible = 2` は表示の設定で変わる**（mac）。**どれも図面全体を数えるのに使わない。**
- **`NumSelectedObjects` はアクティブレイヤの分だけを数える**（mac）。
- **作った直後の図形は選択されている**（`IsSelected` が真。入れ子の中も）。
- 画面の数との対応: **情報パレットの「N 図形」は `0` と `90` を除いた直下の数・`allDrawing` の数と
  一致する**（置き石があっても一致）。**オーガナイザ「デザインレイヤ」には図形の数の列が無い**
  （[突き合わせの表](../Metrics/Layer%20Objects.md#3-入れ子と-foreachobjectn-の-traversewhat)）。
- 情報パレットは **SDK からアクティブレイヤを替えただけでは更新されない**ことがある（mac）。

## 4. 消した図形・取り消した図形（mac）

**どちらも型 `90` の置き石になる。** `ForEachObjectN` は置き石を渡さないので、数は画面と合う。
`FirstMemberObj` → `NextObject` では置き石が見えるので、型で除けば画面と合う
（[操作ごとの数](../Metrics/Layer%20Objects.md#4-消した図形取り消した図形mac)）。

- **図形が別の型のまま残るのではない。** #242 で「取り消した図形は保存するまで一覧に残る」と
  見えていたものの正体はこの置き石である（[Documents](Documents.md)）。保存で減るのは取り消した
  作成の分だけで、**並べ替えと消去の置き石は保存しても減らない**。
- **消した図形は名前でも引けない**（`GetNamedObject` が nil）。取り消せば戻った図形を返す。
- **消去を取り消して戻った図形は番地が変わる。ハンドルを取り消しをまたいで持ち続けない。**
  並べ替えでは番地は変わらなかった。
- ブリッジの別名保存を挟んでも、メニューの「取り消し」は 1 段で消去や複製を戻した。空の段を積むのは
  図面に PIO を初めて足したあとの最初の保存だけで、見本を作った回のブリッジの保存で使い切っていた
  ためと読める【推定】（#288。[Undo](Undo.md)。[実測](../Metrics/Layer%20Objects.md#4-消した図形取り消した図形mac)）。

## 5. 種別の名前

- **`GetObjectTypeN` の番号から名前を引く口は SDK に無い**【ヘッダ根拠】（`ISDK` にも VWFC にも
  無い。`TypeName` で引けるのは破線・レベル種別・部品の名前だけ）。**プラグインは自前の表を
  持つ。** 番号の一覧は `Objs.TDType.h`（見本で出た番号は [計測記録](../Metrics/Layer%20Objects.md#5-種別の名前)）。
- **PIO の名前は `VWParametricObj(h).GetParametricName()`**（`"Door"` / `"Window"`）。

## 6. 名前・クラス・外形

- **名前の無い図形の `GetObjectName` は空文字列。**
- **`GetObjectClass` → `ClassIDToName` でクラスの名前が引ける。** 置き石（型 90）は空。
- **複製すると名前に連番が付く**（`LO-文字` → `LO-文字-2`）。
- **`GetObjectBounds` はレイヤの縮尺を掛けない世界座標の mm。** 縮尺の違うレイヤに同じ座標で
  作った図形は、同じ外形を返す。
- **回した図形は外接矩形**を返す。
- **文字は紙の上の大きさ×縮尺**（1:50 のレイヤで作るとちょうど 50 倍）。ただし**別の縮尺のレイヤ
  から `AddObjectToContainer` で移した文字は、作ったときの大きさのまま**（mac。描き直しで変わるかは
  測っていない。[実測](../Metrics/Layer%20Objects.md#6-名前クラス外形)）。
- **3D の図形は平面の外接矩形**を返す（押し出しは底面の矩形）。高さは `GetObjectCube` の Z で読める。
- シートレイヤのビューポートの外形は紙の上の mm。

## 7. シートレイヤ

- **シートレイヤを辿ると、ビューポート（型 `122`）と終端だけ**が出る。`allDrawing` はシートレイヤの
  図形も渡す。図面全体を数えるときにシートレイヤを含めるかは呼ぶ側の決め（型 `122` で見分けられる）。
- **`CreateCustomObject` は、新しい PIO をアクティブレイヤに置く。** アクティブがシートレイヤ
  のときに壁の上の位置で窓を作っても、デザインレイヤの壁には入らない（Windows・mac）。
  **作ったあとに `AddObjectToContainer(窓, 壁)` を呼ぶと `true` が返るのに、窓の親が nil になる**
  （どこにも属さない PIO が残る。Windows）。壁へ入れる口としては使わない。

## 8. 刻みの中の所要

- **刻みの中（ブリッジの要求・`SetTimer` の刻み）から辿ってよい**（落ちない）。矩形 3000 を名前・
  外形・クラスを読みながら辿って、Windows の VM でも 10 ms に届かない・mac は 1 ms 未満
  （[実測](../Metrics/Layer%20Objects.md#8-刻みの中の所要)）。

## Windows の VM では描画のスレッドで落ち続けた

見本を作ったあとの画面の操作で VW が 7 回落ちたが、**落ちたスレッドは `d3d10warp.dll`
（Windows のソフトウェア描画）の中で、プローブ（SDK で作った図形）が原因ではない**——人がメニューで
シンボルを作っても落ちた。何が引き金かまでは絞れず、VM そのものが不安定だった見込み【推定】。
mac では同じ操作のどれでも落ちなかった。上の Windows の値は落ちる前にプローブが成功で返した
ログで、落ちとは関係しない（[切り分け](../Metrics/Layer%20Objects.md#windows-の-vm-では描画のスレッドで落ち続けた)）。
