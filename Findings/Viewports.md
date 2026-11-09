# ビューポート（平面図・断面）

平面・断面ビューポートを SDK から作り、表示・更新・注釈を扱うときの挙動
（[#147](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/147)・
[#151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)・
[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)・
[#189](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/189)・
[#200](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/200)・
[#202](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/202)・
[#215](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/215)・
[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)・
[#248](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/248)）。
断りが無ければ VW 2026 / mac で実測。測った環境・プローブ・実測の表とログは
[計測記録](../Metrics/Viewports.md)。

## クラス表示

- **クラスは明示的に表示へ戻さないと非表示のまま**生成される。`VWClass::ForEachClass`
  （＝`ISDK::ForEachClass`）で**ゲスト（参照ファイル由来）を含む全クラス**を表示にする。
  **`SetUseDocumentClassVis`（1031）は使えない**（ヘッダの但し書きが "for dlvps"）。
- **注釈へ後から足した図形のクラスはビューポートで非表示のまま。** 全クラスを表示へ戻して
  再更新する（[Data Tags](Data%20Tags.md)）。
- クラスを表示へ戻すのは絵のためだけでなく、**断面の 1050 のためにも要る**（下記）。

## 平面図（2D/平面）への作り直し

- **`CreateViewport` が作ったビューポートは、OIP 上は〈2D/平面〉なのに描画キャッシュが 3D の
  「上」のまま**で、更新でも Project 2D（1005）を ON にするだけでも直らない。
- **利用者の手動操作をなぞる**: ビューの向きを〈上〉（`standardViewTop` = 1007）にし、Project 2D を
  OFF → **更新** → ON → 最後の更新（表示レイヤを絞った後に）。`GetProject2D` で読み戻して確かめる。

## `UpdateViewport` は描く——が、**戻った時点では描き終わっていない**（#215）

[#215](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/215) で実機確認済み
（[数字](../Metrics/Viewports.md#updateviewport-は描くが戻った時点では描き終わっていない215)。
描画モード側は [周期実行と通知](Timers%20and%20Notifications.md) の 7 節）。

- **表示レイヤを入れ（`SetViewportLayerVisibility(vp, layer, 0)`）`SetDirty(true)` を立ててから
  `UpdateViewport` を呼べば、キャッシュ群（`kViewportGroupCache` = 3）に中身が入る。**
- **ヘッダの条件は「dirty」かつ「描画種別がワイヤフレーム／スケッチ以外」**
  （`APIBase.Legacy.Defs.h:5539`）。**作りたてのまま呼ぶと 0 ms で戻って何も起きない。**
- **戻っても描き終わっていない**——戻った後にランループを回すと更に数百 ms 仕事が出て、利用者の
  画面では「更新」が 16 秒出ていた。**戻り値（`void`）も `IsDirty()` も完了の合図ではなく、
  「更新して終わるまで待つ」口は無い。** 待つなら「ランループを回して 1 周が伸びなくなるまで」——
  当て推量なので**締切を必ず持つ**。
- **`UpdateViewport` 直後に `no` だった `IsDirty()` は、undo イベントが閉じると `yes` へ戻る**
  （#245。1004 も同じ。中身・外接・1050 は変わらない＝印だけ立て直す）。**`IsDirty()` を
  「同じ呼び出しの中で更新したか」の印に使わない。**
- **外接（`GetObjectBounds(viewport)`）は中身が入っても動かない**（空枠のまま）。**中身が入ったかは
  キャッシュ群の件数で読む**（外接が動くのは `ResetObject` の後だけ。下記）。
- **`VWViewportObj::SetScale()` は数が大きいほど絵が大きい**（100 → 50 で絵が半分）。
- **`VWLayerObj::SetSheetPrintDPI(300)` は効かない**（読み戻しが 72。2 回再現）。

## 断面ビューポート

- **`ISDK::CreateSectionViewport(pt1, pt2, pt3, depth, startHeight, endHeight, layer)` で新規に
  作れる。** pt3 は「見る側」。既製ビューポートを流用する運用は要らない。
- **範囲は 3 つの数値引数だけで決まり、「無限」にはできない。** `depth` = 0 は〈奥: 無限〉だが、
  `startHeight` / `endHeight` = 0 は〈0〜0〉で建物が消える。**奥行き＝0／高さ＝対象を包む実寸＋
  余白／長さ＝断面線の長さ**で決める。
- 表示の作法（**更新より前**に設定する）:
  - **奥／手前（`ovSectionViewportDisplayObjectsBeyondCutPlane` = 1064 /
    `…BeforeCutPlane` = 1065）は「どちらかを選ぶ」ものではない**——下記の表で倒す。
    **とりあえず映ってほしいなら `1064=false` / `1065=true`。機械的に両方 `true` にしない。**
  - プレイナー図形は表示しない … **1035**／2D コンポーネントは表示する … **1059**（既定は非表示）
  - **レンダリングは〈隠線消去〉**（`SetRenderType(renderFinalHiddenLine)`）。**これが先に要る**
    ——シェイドのままでは 1059 を書いても入らない（VW は隠線消去でしか意味の無い項目を隠す）。
- 注釈へレベル（標高）オブジェクトを置く作法は [Level Objects](Level%20Objects.md)（既定では
  注釈の Y を読まない）。

### 断面に中身が入る条件——`1065` が主スイッチで、`1064` は「奥だけ」への絞り込み（#202）

[#202](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/202) で 16 通りを
総当たり（mac。VW へ戻った後も同じ——#245）。[測り方と表](../Metrics/Viewports.md#断面に中身が入る条件1065-が主スイッチで1064-は奥だけへの絞り込み202)。

#### 切断面がモデルを**切っていない**とき——4 通りで全部違う

| `1064`（奥） | `1065`（手前） | 断面に出るもの |
| --- | --- | --- |
| F | F | **何も出ない** |
| T | F | **何も出ない** |
| F | T | **切断面の両側** |
| T | T | **`pt3` の反対側（奥）だけ**（手前側は落ちる） |

- **`1065` が主スイッチ**（`false` の間は `1064` を立てても空）。**`1064` は引き算**として働き、
  手前側（`pt3` と同じ側）を落とす（名前 `Display…BeyondCutPlane` から読める「奥を足す」ではない）。
- **断面線の向き（`pt1`→`pt2`）は一切効かない。**
- 群 4（断面群）は空でも必ず 2 件在るので判定に使えない。`CreateSectionViewport` が置く
  1064/1065 の既定値は測っていない（倒し方は既定値に依らない）。

#### 切断面がモデルを**切っている**とき——4 通りとも描ける

- **切り口（群 6・15）はフラグに関わらず出る。** 変わるのは群 5（奥／手前の立面）の件数だけで、
  **中身の外接は 4 通りとも同一**（注釈座標の変換はフラグに依らない）。

#### 倒し方——**用途で決める。両方 `true` は「安全側」ではない**

| 欲しい絵 | `(1064, 1065)` |
| --- | --- |
| 建物を輪切りにした断面図（切り口だけでよい） | **どれでもよい**（切っていれば切り口は出る） |
| **切断面の向こうを見た立面・軸組図**（手前を消したい） | **`(T, T)`** |
| **何が映るか分からない／とりあえず映ってほしい** | **`(F, T)`** |
| 切り口以外を全部消す | `(F, F)` または `(T, F)` |

- **「両方 `true` なら安全」は誤り**（#200 の当面の指針）——手前側を落とすので、断面線を対象の
  奥に引いた配置では空になる。**どの配置でも映るのは `1064=false` / `1065=true` だけ。**

#### これで #189 と #200 の「逆の結果」が両方説明できる

- **#189 は 6 枚すべて両方 `true` で、#200 はモデルを片側にしか置いていなかった**——どちらの
  数値も正しく、規則が足りなかっただけ。#189 の E が空だったのは `pt3` と同じ側だからではなく
  **`1064` を併せて立てていたから**（[突き合わせの表](../Metrics/Viewports.md#これで-189-と-200-の逆の結果が両方説明できる)）。

### 【罠】作ったばかりの押し出しは、外接を読むか `ResetObject` するまで断面に映らない（#248）

VW 2026 Update 6・Windows で実測（[#248](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/248)。
[腕 A〜G の表](../Metrics/Viewports.md#罠作ったばかりの押し出しは外接を読むか-resetobject-するまで断面に映らない248)）。

- **`VWExtrudeObj(VWPolygon2D, 基準高, 厚み)` で作った押し出しは、そのまま断面を作っても映らない。
  作った直後に `GetObjectBounds(h)` を読むか `ResetObject(h)` を呼べば映る。** 断面を作る順序・
  枚数・用紙・アクティブレイヤ・選択は効いていない。#245 の「最初の 1 枚にしか入らない」の正体。
- 理由【ソース根拠】: `VWExtrudeObj.cpp` で `ResetObject` を呼ぶのは始点・終点を取る 2 つの
  コンストラクタ（`:112`・`:138`）だけで、`(VWPolygon2D, 基準高, 厚み)`（`:25`）は呼ばない。
  外接を読むとそこで形が作られる【推定】。
- **SDK で図形を作って同じ呼び出しの中で断面を作るなら、作った図形に `ResetObject` を呼んでから。**
  実プラグインの押し出し（`VWExtrudeObj`）も同じ形で作っているなら当てはまる。
  他の種類の図形・`CreateExtrude` 系の直呼びで同じかは測っていない。
- この VM では断面を作った後に描画のスレッド（`d3d10warp.dll`）で落ちることがあった——VM の描画の
  側の問題【推定】（[#270](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/270)）。

### SDK で作った断面ビューポートの注釈でレベル基準線に高さを出す（#147 → #151 で訂正）

> **【訂正の記録】#147 の手順「1055 を 1050 へ写して `ResetObject`」は回り道だった**
> （[#151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)）。**写す必要は無い。**
> #147 の【推定】「属性やパラメータの変更で個体が作り直されて `0` に落ちる」も外れ（落ちない）。

- **1050（`ovViewportViewMatrix`）が更新のときに断面の向きへ入るのは、表示レイヤとクラスの両方が
  表示になっているときだけ。** 片方だけでは単位行列のまま。**断面が描けたかは関係なく、可逆**
  （VW へ戻った後も同じ。[2 × 2 の表](../Metrics/Viewports.md#sdk-で作った断面ビューポートの注釈でレベル基準線に高さを出す147--151-で訂正)）。
  #141 の「全レイヤを表示にしても `0`」の答えでもある（[Layers and Stories](Layers%20and%20Stories.md)）。

```cpp
MCObjectHandle vp = gSDK->CreateSectionViewport(pt1, pt2, pt3, 0.0, startZ, endZ, sheetLayer);
gSDK->ForEachLayerN([vp](MCObjectHandle layer) {               // 1) 表示レイヤを表示へ
	gSDK->SetViewportLayerVisibility(vp, layer, 0 /* 表示 */);  // デザインレイヤだけ渡す
});
gSDK->ForEachClass(true, [vp](MCObjectHandle cls) {            // 2) クラスも表示へ（省くと 1050 は入らない）
	gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0);
});
// 3) レンダ（隠線消去）・1064・1035・1059 などの下ごしらえ
gSDK->UpdateViewport(vp);                                      // 4) ここで 1050 に 1055 と同じ値が入る
// 5) 注釈へレベル基準線を置き、3 つ組を書いて作り直す（Level Objects.md）。1050 へは何も書かない
gSDK->ResetObject(marker);                                     // → 絶対Z が出る
```

- **置いた直後は `0`＝作り直しは要る。** 1050 が入っていれば、**ラベルを動かす・欄を書く・線の
  太さやクラスを変える・`ResetObject`・更新のどれでも高さは落ちない**——人が後から触る図面でも
  壊れない（`DuplicateObject` へ逃げる必要も無い）。
- **既存の断面ビューポートを更新しても 1050 は保たれる**（1000〜1150 で変わるのは 1004 だけ）。
- **`ovViewportViewType`（1007）は書けない**（`SetObjectVariable` が `false`。`SetViewType` も
  入らない）。1050 の従属物で VW が引き直す。**向きを直すときも触らない。**
- 1053・1004 は書けるが要らない。
- `IsElevationBenchmarkConstrained`（`IMarkersPluginSupport`。名指しで include）は断面の注釈に
  置いた個体で `true`。
- **断面線（`kInternalID_SectionLine2` = 645）は断面群（`kViewportGroupSection` = 4）の中だけに
  いて、SDK 製にも 1 本入っている**——「断面線が無いから向きが出ない」ではない。
  `GS_CreateSectionLineInstance` / `GS_IsSectionLineLinkedToViewport` はヘッダにあるがリンクできない
  （[調査の作法](Investigation%20Techniques.md)）。要らない。
- **「UI 製と SDK 製の非対称」は存在しなかった**——効いているのは上の 2 × 2 だけ（UI のツールで
  作ったものは測っていない）。

#### #147 で潰した筋（そのまま有効）

- **効くのは 1050 ただ 1 つ**（1000〜1150 の差 14 件を書き分けた）。1055 は書けない。
- 作り方の引数（`depth`・高さの範囲・断面線・`pt3`）でも、断面線との結び付きでも出ない
  （[記録](../Metrics/Viewports.md#147-で潰した筋そのまま有効)）。

### 断面ビューポートの注釈に出るグリッド線（通り芯）——符号の位置を決めるのは `ShoulderLengthAtStart`（#189）

[#189](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/189) で実機確認済み
（mac・日本語 UI。[計測記録](../Metrics/Viewports.md#断面ビューポートの注釈に出るグリッド線通り芯符号の位置を決めるのは-shoulderlengthatstart189)）。

#### 正体と見つけ方

- **正体はデザインレイヤの通り芯と同じ `GridAxis`**（内部 ID **647** = `kInternalID_GridAxis`、
  loc 名「グリッド線」）で、**同じ 32 件のパラメータ表**を持つ。通り芯 1 本につき 1 個体。
- **注釈群を自分で降りて見つける**（criteria では見つからない。下記）。
- **`ovPositionLocked`（709）が `true`** だが、`SetParamReal` と `ResetObject` は通る。
- **SDK にグリッド線専用の API・オブジェクト変数は無い**【ヘッダ根拠】——触る道はパラメータだけ。

#### いつできるか——`UpdateViewport` が作り、更新し直しても作り直さない

- `CreateSectionViewport` 直後は注釈群そのものが `nil`。**`UpdateViewport` が置き、更新し直しても
  ハンドルは同一で書いた値も残る**（1053 を立てても同じ）。自分で作るものではない。

#### 出すには「通り芯が切断面を横切っている」ことが要る——しかも点で置いた通り芯では出ない

- **`CreateCustomObject("GridAxis", 位置, 角度)` で置いた通り芯は線を 1 本も持たない**（切断面まで
  届かない）。`GridAxis` は線分 PIO ではなく（`LineLength` が無い。
  [Parametric Objects「線分 PIO の両端と長さ」](Parametric%20Objects.md)）、長さのパラメータも無い。
- **線は `CreateCustomObjectPath` に 2 頂点の 2D ポリラインで与える**
  （[Parametric Objects「パスの型を間違えると…」](Parametric%20Objects.md)）:

  ```cpp
  VWFC::VWObjects::VWPolygon2DObj path({VWFC::Math::VWPoint2D(x, y0),
                                        VWFC::Math::VWPoint2D(x, y1)});
  path.SetClosed(false);
  MCObjectHandle axis = gSDK->CreateCustomObjectPath("GridAxis", path);
  gSDK->AddObjectToContainer(axis, designLayer);   // 作ったものはアクティブレイヤに入る
  gSDK->ResetObject(axis);
  ```

#### 符号を動かす欄——`ShoulderLengthAtStart`。**単位は用紙 mm**

- **符号（ラベル枠）を動かす欄は `ShoulderLengthAtStart`（水平線の長さ（先端））ただ 1 つ。**
  **単位は用紙 mm**（容れ物の縮尺が掛かる。1/100 で +10 → 1000、1/50 で +10 → 500）——
  **「用紙で 10mm 上げたい」なら縮尺に関わらず `+10`**
  （[表](../Metrics/Viewports.md#符号を動かす欄shoulderlengthatstart単位は用紙-mm)）。
- **`ShoulderLengthAtEnd` は絵を 1mm も動かさない**（`ShowBubbleAt` が `Start Point`）。
- **書いた後に `ResetObject` が要る**（値は即読み戻せるが、絵は Reset まで動かない）。
- **スタイルは握っていない**（`GetPluginStyleParameterType` が 2 = `_AllwaysByInstance`）——
  スタイルを外さずに個体へ書けば効く。

#### 符号の位置の基準は「**映っているモデルの上端**」——高さ範囲では動かない

- **符号の上端 ＝ 映っているモデルの上端 ＋ 用紙スケールの固定分**（1/100 で 1235・1/50 で 622.5
  ＝ 水平線とラベル枠の用紙 mm）。断面が空ならモデルの上端は 0 扱い
  （[表](../Metrics/Viewports.md#符号の位置の基準は映っているモデルの上端高さ範囲では動かない)）。
- **`endHeight` を広げても符号は動かない**（[plugin#181](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz/pull/181)
  で重なったままだった理由）。**用紙の上で動かす唯一の手は `ShoulderLengthAtStart` を足すこと。**

#### 断面を空でなくするのに足を取られた記録（条件そのものは #202 で確定した）

- 倒し方は上記「断面に中身が入る条件」を読む。#189 で出た数値と【訂正】は
  [計測記録](../Metrics/Viewports.md#断面を空でなくするのに足を取られた記録条件そのものは-202-で確定した)。
- **グリッド線は断面が空でも注釈に出る。** x の並びは見る側（`pt3`）で反転する。
- この回では `ovViewportDisplay2DComponents`（1059）が書いても入らなかった（`true` を返し読み戻しは
  `false`）。
- **壁を断面に映すなら高さを `SetWallOverallHeights` で与える**（[Walls](Walls.md)）。

#### `GridAxis` のパラメータ表（VW 2026 / mac・日本語 UI。全 32 件）

- 32 件の表（索引・universal 名・ローカライズ名・欄型・既定値）は
  [計測記録](../Metrics/Viewports.md#gridaxis-のパラメータ表vw-2026--mac日本語-ui全-32-件)。
  座標欄（7）は索引 4 `ShoulderLengthAtStart` と 5 `ShoulderLengthAtEnd` だけ。

#### 書き方（全断面ビューポートの符号を用紙で 10mm 上げる）

```cpp
gSDK->UpdateViewport(vp);                                    // ① 更新してからでないと注釈群が無い
MCObjectHandle annotation = gSDK->GetViewportGroup(vp, kViewportGroupAnnotation);  // ② 自分で降りる
for (MCObjectHandle h = gSDK->FirstMemberObj(annotation); h != nil; h = gSDK->NextObject(h))
{
    if (gSDK->GetObjectTypeN(h) != kParametricNode)
        continue;
    VWFC::VWObjects::VWParametricObj pio(h);
    if (pio.GetInternalID() != kInternalID_GridAxis)   // 647
        continue;
    const double paper = pio.GetParamReal("ShoulderLengthAtStart");   // ③ 用紙 mm で書く
    pio.SetParamReal("ShoulderLengthAtStart", paper + 10.0);
    gSDK->ResetObject(h);                                    // ④ 絵は ResetObject まで動かない
}
```

この後にビューポートを更新しても、書いた値は残る。

## 断面ビューポートの注釈座標と用紙座標の対応（#200）

```
用紙座標 = 注釈座標 ÷ 縮尺 ＋ ビューポートの位置（1024 / 1025）
```

- **注釈座標 → 用紙座標はこの 1 本の式**（k は 1/縮尺ちょうど・オフセットは左下と右上の両方から 0）。
  外接を測る必要は無い（[解き方](../Metrics/Viewports.md#断面ビューポートの注釈座標と用紙座標の対応200)）。
- 注釈座標の横は「断面線に沿った位置 − **断面線の終点**」、縦は**モデルの絶対Z**
  （[Dimensions](Dimensions.md)「断面ビューポートの注釈空間の座標」と同じ値になった）。
- **したがって GL（Z = 0）の用紙 y は `ovViewportYPosition`（1025）そのもの。**

### GL は映っているモデルの高さに依らない——実測

- 高さだけが違うモデルを映す 2 枚で、中身の下端は `0.0000` で一致した
  （[表](../Metrics/Viewports.md#gl-は映っているモデルの高さに依らない実測)）。
- 係数を解くなら**4 辺とも空枠の外へ出る大きさ**の矩形で測る（右上だけでは左下が空枠の縁のままに
  なり、無意味な k が出た）。

### 注釈座標は「ビューポートが用紙のどこに在るか」を含む

- **ビューポートを `MoveObject` すると、注釈の中の図形の外接が「移動量 × 縮尺」ずれる**——
  **注釈空間は用紙座標を縮尺倍した写し**で、置いた値をそのまま読み戻せるのは動かしていない間だけ
  （[表](../Metrics/Viewports.md#注釈座標はビューポートが用紙のどこに在るかを含む)）。
  プラグインの経験則（`draw/DrawUtil.h`）の正体で、作法は**測る → タグを置く → 動かす**
  （[Sheet Layers and Page Layout](Sheet%20Layers%20and%20Page%20Layout.md)）。
- **GL を揃えて並べるなら外接を測らない。** `MoveObject` で **1025 を目標の用紙 y に合わせる**だけ
  （作った直後の 1024 / 1025 は `(0, 0)`）。外接は映る架構の高さと通り芯の符号で通りごとに違う。

### `GetObjectBounds(viewport)` が何で決まるか

- 断面も注釈も空なら **±26.649（53.3mm 角）の空枠**。中身があれば**映っている中身そのもの**で、
  空枠は和を取らずに**置き換わる**（[表](../Metrics/Viewports.md#getobjectboundsviewport-が何で決まるか)）。
- **注釈は `ResetObject` の後でだけ外接に入る**（足しただけでは VW へ戻った後も動かない。#245）。
- **高さ範囲は入らない**——外接は映っている中身で決まる（同じ範囲でも 30mm / 60mm と違った）。
- 外接で「描かれたか」を判定できるのは `ResetObject` の後で、**空枠より大きいものが入ったときだけ**
  （小さいものは隠れる。[Dimensions](Dimensions.md) の訂正の記録）。

### 併せて測れた値

- **1024 / 1025 は用紙 mm での位置**（ヘッダは read only だが `MoveObject` で動く。
  `VWViewportObj::GetPosition()` が読む）。**1049** の平行移動は位置 × 縮尺。
- **1050 / 1055 は互いに一致するモデル → 断面ビューの行列で、用紙とは無関係**（`MoveObject` でも
  動かない）。**1051 は読めない**（`false`）・**1056 は単位行列**・**crop は無い**・**注釈群は最初の
  注釈を足すまで `nil`**（[表](../Metrics/Viewports.md#併せて測れた値)）。
- **1052 / 1026** は下記。
- **空の容れ物の外接は「反転した矩形」**（左 = `+DBL_MAX`・右 = `−DBL_MAX`）で、`GetObjectBounds` は
  `true` を返す——**`左 > 右` で空を判定する。`true` を「読めた」と読まない。**

### `ovViewportUnscaledBoundsWithoutAnnotations`（1052）と `ovViewportAngleWithXAxis`（1026）は何を返すか（#202）

- **1052 は「注釈を除いた用紙の外接 × 縮尺」**——回っていなければ中身の実寸（モデル mm）で、
  **注釈を足しても 1 桁も動かない**。「中身だけの大きさ」を知る唯一の口。**回すと回した外接を
  囲み直した矩形**になるので、回した枚では断面ビューの寸法ではない。中身が空なら反転した矩形
  （[表と検算](../Metrics/Viewports.md#ovviewportunscaledboundswithoutannotations1052と-ovviewportanglewithxaxis1026は何を返すか202)）。
- **1026 は用紙の上での回転角（度）**（回していなければ 0、`RotateObjectN` で 30 度回すと 30）。

### 断面に中身が入る条件——1 つずつ変えた 8 通り

- **「描けたか」の判定はキャッシュ群 3 / 5 / 6 / 7 / 15 の件数**（群 4 は空でも 2 件）。絵を見なくても
  機械で分かる。
- **描けた枚の中身の外接は同一**——1064 / 1065 や断面線の位置は「映るか」を決めるだけで、
  「どこに映るか」は変えない。`depth`・高さ範囲の下限・表示設定は空になる原因ではない
  （[8 通りの表](../Metrics/Viewports.md#断面に中身が入る条件1-つずつ変えた-8-通り)）。

> **【決着】この 8 通りは「`1065` が主スイッチで、`1064` は奥だけへの絞り込み」でそのまま説明できる**
> （#202）。ここで挙げていた「当面は両方 `true` にするのが安全」は**誤り**で、どの配置でも映るのは
> **`1064=false` / `1065=true`**。

## 注釈の中の図形は、検索条件（criteria）では見つからない

- **`ISDK::ForEachObjectInCriteria` は注釈の中の図形を 1 件も返さない**（8 本ある図面で 0 件）。
  **探すなら自分で降りる**（[計測記録](../Metrics/Viewports.md#注釈の中の図形は検索条件criteriaでは見つからない)）:

```cpp
// レイヤ → 直下 → グループ → ビューポートの注釈群 の順に降りる
void Walk(MCObjectHandle container, int depth)
{
	if (container == nil || depth > 8)
		return;
	for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil; h = gSDK->NextObject(h))
	{
		const short type = gSDK->GetObjectTypeN(h);
		if (type == kGroupNode)
			Walk(h, depth + 1);
		else if (type == kViewportNode)
			Walk(gSDK->GetViewportGroup(h, kViewportGroupAnnotation), depth + 1);  // 注釈群
		else
			/* ここで用があるか見る */;
	}
}
gSDK->ForEachLayerN([](MCObjectHandle layer) { Walk(layer, 0); });
```

- 注釈の中にいるかは `ISDK::IsViewportGroupContainedObject(h, kViewportGroupAnnotation)`。
  逆向き（群 → ビューポート）は `GetViewportGroupParent`。

## 打ち切った調査: 断面ビューポートの範囲を「無限」にする

- **範囲はオブジェクト変数の外に保持されており、SDK からは触れない**（欠番 1060〜1090 も含めて、
  〈無限〉に変えた枚と差のある変数は 1 つも無かった）。対象の大きさから決めた有限範囲で確定
  （実用上は無限と同じ見え方）。[記録](../Metrics/Viewports.md#打ち切った調査-断面ビューポートの範囲を無限にする)。

## `CreateViewport(parentHandle)` の `parentHandle` は「**どの容れ物に置くか**」

[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175) で 3 回踏んだ
（[表](../Metrics/Viewports.md#createviewportparenthandle-の-parenthandle-はどの容れ物に置くか)）。

- **デザインレイヤを渡すとデザインレイヤの上にでき、注釈を持てない**（`AddViewportAnnotationObject`
  が `false`、注釈群は `nil`）。他の呼び出しは全部成功するので気付けない。
  （`GS_CreateViewport` の説明【ヘッダ根拠】は [Undo](Undo.md) にある。）
- **シートレイヤを渡し、表示するデザインレイヤは作成後に決める**:

```cpp
MCObjectHandle vp = gSDK->CreateViewport(sheetLayer);        // ★ 置く容れ物を渡す
gSDK->SetViewportLayerVisibility(vp, designLayer,
                                 VWFC::VWObjects::kLayerVisibilityNormal);  // 何を映すか
TVariableBlock scale;
scale = static_cast<Real64>(50.0);
gSDK->SetObjectVariable(vp, ovViewportScale, scale);          // 既定は 1:1 なので自分で書く
gSDK->UpdateViewport(vp);
```

- **`CreateLayer(..., kLayerSheet)` はアクティブレイヤを切り替える**が、`CreateViewport` は渡した
  `parentHandle` に置くので、アクティブレイヤを合わせても直らない。
- 注釈は**ビューポートの縮尺の容れ物**——縮尺を変えると注釈の世界座標が比例して書き換わり、
  紙の見え方は保たれる（[Dimensions](Dimensions.md)）。
