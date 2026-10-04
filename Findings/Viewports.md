# ビューポート（平面図・断面）

## クラス表示

- **クラスは明示的に表示へ戻さないと非表示のまま。** 表示レイヤを指定どおりに絞っても、
  クラスが全部消えた状態で生成される。`VWClass::ForEachClass`（＝`ISDK::ForEachClass`）で
  **ゲスト（参照ファイル由来）を含む全クラス**を表示にする。
  **`SetUseDocumentClassVis`（オブジェクト変数 1031）は使えない**——ヘッダの但し書きが
  "for dlvps"（デザインレイヤビューポート専用）。
- **注釈へ後から足した図形のクラスはビューポートで非表示のまま。** ビューポートの
  クラス表示設定より後に注釈へ図形を足したら、全クラスを表示へ戻して再更新する。
  （[Data Tags](Data%20Tags.md)）

## 平面図（2D/平面）への作り直し

**`CreateViewport` が作ったビューポートは、OIP 上は〈2D/平面〉なのに描画キャッシュが
3D の「上」ビューのまま**で、更新ボタンでも直らない。Project 2D（オブジェクト変数
**1005**）を ON にするだけでは作り直されないので、**ユーザーの手動操作をそのまま
なぞる**——ビューの向きを〈上〉（`standardViewTop` = **1007**）にし、Project 2D を
いったん OFF にして**更新を挟み**、再度 ON に戻して最後の更新を行う（表示レイヤを
絞った後・最後の更新の前に行う）。入ったかは `GetProject2D` で**読み戻して**確かめる。

## 断面ビューポート

- **断面ビューポートは `ISDK::CreateSectionViewport` で新規作成できる**
  （pt1, pt2, pt3, depth, startHeight, endHeight, layer）。pt3 は「見る側」を示す点。
  VectorScript 時代の「既製ビューポートを手で用意して流用する」運用は要らない。
- **断面の範囲は 3 つの数値引数が決める唯一の手段で、「無限」にはできない。**
  実測では **`depth` = 0 → 〈切断面より奥: 無限〉**だが、**`startHeight` / `endHeight` = 0 は
  〈有限・0〜0〉**になって断面から建物が消える。したがって
  **奥行き＝0／高さ＝対象を包む実寸＋余白／長さ＝断面線の長さ**で決める。
- 表示の作法（いずれも**更新より前**に設定する）:
  - 切断面より奥を表示するか … オブジェクト変数 **1064**
    （`ovSectionViewportDisplayObjectsBeyondCutPlane`）。**どちらに倒すかは断面線の
    引き方で決まる**ので、機械的に「表示しない」にしてはいけない:
    - **断面線が対象を切っている**（建物を輪切りにする断面図）→ `false` でよい。
    - **断面線を対象の手前に引いて奥を見る**（軸組図・立面のような使い方）→
      **`true` でなければ対象ごと消える。** 対象は丸ごと「切断面より奥」にあるため。
      **実測で踏んだ**——壁の 1500mm 手前に断面線を引き、`false` のままにしていたら、
      壁に高さを与えても断面は空のままだった（[Dimensions](Dimensions.md)）。
  - プレイナー（アクティブレイヤ平面）図形は表示しない … **1035**
  - 2D コンポーネントは表示する … **1059**（**既定は非表示**）
  - **レンダリングは〈隠線消去〉**（`VWViewportObj::SetRenderType(renderFinalHiddenLine)`）。
    **これが先に要る**——シェイドのままでは 1059 を書いても入らない。手作りの断面と OIP の
    項目が違って見えるのもこれが原因で、VW は隠線消去でしか意味を持たない項目を隠す。

- **注釈へレベル（標高）オブジェクトを置くときの作法・パラメータ・高さの基準**は
  [Level Objects](Level%20Objects.md) にある（**既定では注釈の Y を読まない**ので、
  欄を 1 つ切り替える必要がある）。

### SDK で作った断面ビューポートの注釈でレベル基準線に高さを出す（#147 → #151 で訂正）

> **【訂正の記録】#147 でマージした手順「注釈を置き終えてから 1055 を 1050 へ写し、
> 個体を `ResetObject` する」は回り道だった**（[issue #151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)
> で実機確認して取り直した）。**写す必要は無い。** 1050 は更新のときに引き直される派生値で、
> **表示レイヤとクラスを表示へ戻してから更新すれば、`UpdateViewport` が 1055 と同じ値を
> 自分で入れ、以後の更新でも保つ。** 「写した値が更新のたびに単位行列へ戻る」と見えたのは、
> **更新の時点でその表示設定が揃っていなかった**ためである（#147 が「SDK 製はキャッシュ群を 1 つも持たない」と
> 記録したのも同じ一つの事柄）。あわせて #147 の**【推定】「属性やパラメータの変更で
> 個体が作り直されて `0` に落ちる」も外れ**——落ちない（下表）。

**1050 が更新のときに断面の向きへ入るのは、表示レイヤとクラスの両方が表示になっている
ときだけ。片方だけでは入らない。断面が実際に描けたかどうかは関係ない。**
同じ図面・同じ座標で SDK 製を 5 枚作り、レンダ（隠線消去）と 1064 は 5 枚とも同じに与えて
**表示設定だけを 2 × 2 に振って**更新した（実測）:

| 更新の前にしたこと | 更新後の 1050 | キャッシュ群（3/5/6/7/15） |
| --- | --- | --- |
| **A** 表示レイヤ ×・クラス × | 単位行列 | 無（断面群 4 だけ） |
| **E** 表示レイヤ ○（24 枚）・クラス × | **単位行列** | 無（断面群 4 だけ） |
| **F** 表示レイヤ ×・クラス ○（47 件） | **単位行列** | 無（断面群 4 だけ） |
| **B** 表示レイヤ ○・クラス ○ | **1055 と一致** | 有（5 / 54 / 259 件…） |
| **C** B と同じだが、断面線を建物から 1,000,000mm 離す（切るものが無い） | **1055 と一致** | **無** |
| **D** B のあと**全レイヤを非表示**にして再更新（クラスは表示のまま） | **単位行列へ戻る** | 無 |

- **E と F が単位行列** → **両方要る。** どちらか一方を倒しただけでは入らない。
- **C** は何も描けていないのに入る → **「描けた断面から引き直す」のではない**（キャッシュ群は
  空のまま、1050 だけが断面の向きになる）。**要るのは「表示設定が揃っていること」だけ。**
- **D** で単位行列へ戻り、表示へ戻して更新すると再び入る → **可逆**。
- **これが #141 の「全レイヤを表示にしても `0` のまま」の答えでもある**——あのとき
  **クラスが非表示のまま**だった（[Layers and Stories](Layers%20and%20Stories.md)）。
  クラスを表示へ戻す作法（上記「クラス表示」）は**絵のためだけでなく、1050 のためにも要る。**

**手順——ビューポートの表示設定を済ませてから更新する。1050 へは何も書かない。**

```cpp
MCObjectHandle vp = gSDK->CreateSectionViewport(pt1, pt2, pt3, 0.0, startZ, endZ, sheetLayer);

// 1) **表示レイヤを表示へ倒す。** 作った直後は 1 枚も表示になっていない。
gSDK->ForEachLayerN([vp](MCObjectHandle layer) {
	gSDK->SetViewportLayerVisibility(vp, layer, 0 /* 表示 */);  // デザインレイヤだけ渡す
});
// 2) **クラスも表示へ戻す。** ここを省くと 1050 は入らない（上記 E）——絵のためだけの
//    作法ではない
gSDK->ForEachClass(true, [vp](MCObjectHandle cls) {
	gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0);
});
// 3) レンダ（隠線消去）・1064・1035・1059 などの下ごしらえ（上記「表示の作法」）
// 4) 更新する —— **1 と 2 が済んでいれば、ここで 1050 に 1055 と同じ値が入る**
//    （1007 も VW が引き直す）
gSDK->UpdateViewport(vp);
// 5) 注釈へレベル基準線を置き、3 つ組を書いて作り直す（Level Objects.md）
//    **1050 へは何も書かない**
gSDK->ResetObject(marker);        // → 絶対Z が出る
```

実測（実物件の図面。`1階` / `FL`、絶対Z **`612`**。正解をログへ出してから突き合わせた）:

| どこへ置いたか | 1050 へ写したか | `ResetObject` の後の `Elev` |
| --- | --- | --- |
| **B**（表示を倒して更新した SDK 製） | **写していない** | **`612`**（正解） |
| 既存の断面ビューポート（1050 が入っているもの） | 写していない | **`612`** |
| 置いた直後（まだ作り直していない） | — | `0`（＝**作り直しは要る**） |

**後から触っても落ちない**（#147 の【推定】の答え。B・既存の両方で同じ結果）:

| したこと | `Elev` |
| --- | --- |
| ラベルを 100mm 動かす（`SetPointObjectPos`） | `612` |
| 見た目の欄（`EPfx`）を書く | `612` |
| 線の太さを変える（`GS_SetLineWeight`） | `612` |
| **クラスを変える**（`SetObjectClass`） | `612` |
| `ResetObject` する | `612` |
| ビューポートを更新する | `612` |
| 更新の後にもう一度 `ResetObject` する | `612` |

**＝ 1050 に断面の向きが入っているビューポートなら、個体を作り直しても高さは出る。**
#147 が見た「作り直すと `0` に落ちる」は、**1050 が単位行列のときだけ**の話だった。
だから人が後から触る図面へ書き出しても壊れない——`DuplicateObject` へ逃げる必要も無い。

- **既存の断面ビューポートを更新しても 1050 は保たれる**（実測。インポータが作った
  断面ビューポート 2 枚で、更新の前後でオブジェクト変数 1000〜1150 を総当り比較して
  **変わったのは `ovViewportDirty`（1004）の 1 欄だけ**）。
- **`ovViewportViewType`（1007）は書けない**（`SetObjectVariable` が `false`。
  `VWViewportObj::SetViewType` も入らない）。**1007 は 1050 の従属物**で、1050 に値が入ると
  VW がそこから引き直す（作った直後は `7`＝上、横向きの断面なら `6`＝右、真横なら `4`＝後）。
  **向きを直したいときも 1007 は触らない。**
- **`ovViewportResetForOnlyAnnotationsChange`（1053）は書ける**（`true` が返り、1050 も保つ）。
  `ovViewportDirty`（1004）も書ける。**どちらも要らない**——表示を倒してあれば 1050 は
  そのまま保たれる。
- **`IsElevationBenchmarkConstrained`**（`IMarkersPluginSupport`。`VectorworksSDK.h` からは
  引き込まれないので名指しで include）は、**断面ビューポートの注釈に置いた個体では `true`**
  （UI／インポータが置いたものも、SDK でいま置いたものも）。空図面の断面ビューポートでは
  `false` だった。
- **断面線（`Section Line2` ＝ `kInternalID_SectionLine2` ＝ 645）はビューポートの断面群
  （`kViewportGroupSection` ＝ 4）の中だけにいる**——デザインレイヤ 24 枚を走査して **0 件**。
  `CreateSectionViewport` が作った直後の SDK 製にも 1 本入っている（断面群は
  `type0` × 1 ＋ 断面線 × 1 の 2 件）。**「断面線が無いから向きが出ない」ではない。**
  UI／インポータ製のその 1 本は 20 欄を持ち、**`Linked To` にビューポートの名前**
  （`Viewport-7`）・`Drawing Title` / `Drawing Number` / `Sheet Number`・
  `Configuration = Section`・`Link State = 2`・`Auto-Coordinate = True` などが入っている。
- **UI と同じ経路で作る道は塞がっている。** `GS_CreateSectionLineInstance` /
  `GS_IsSectionLineLinkedToViewport` は**ヘッダにあるがリンクできない**（`APP_API_FUNCTION`
  製の未公開 API。[調査の作法](Investigation%20Techniques.md)）。もっとも、表示レイヤと
  クラスを倒せば済むので要らない。
- **「UI 製と SDK 製の非対称」は、そもそも存在しなかった。** #147 が UI 製の姿として
  記録した「1050 ＝ 1055・1007 ＝ 6」は、**インポータ（SDK）が作った断面ビューポート
  29 枚がそのまま持っていた姿**である（実測）。更新しても保たれる。**出どころは関係なく、
  効いているのは上の 2 × 2**。
  - **測った範囲**: 実機で開いた図面の断面ビューポート 29 枚（すべてインポータ製）と、
    その場で作った SDK 製 5 枚。**UI の断面ビューポートツールで作ったものは測っていない**
    ——2 × 2 が SDK 製だけで割れており、UI 製を測っても条件は変わらないため。

#### #147 で潰した筋（そのまま有効）

- **効くのは 1050 ただ 1 つ。** オブジェクト変数 1000〜1150 を UI 製と SDK 製で総当りに
  突き合わせると**差は 14 件**。そのうち 12 件を 1 つずつ書き分けて、**数値が出たのは
  1050 だけ**だった（他の 11 件はすべて `0` のまま）。残る 2 件は、1055 が書けず
  （`SetObjectVariable` が `false`。ヘッダのコメントも `read` のみ）、1064 は 1050 と
  併せて書いても打ち消さなかった。
- **1050 を写しても断面は壊れない**（`ovIsSectionViewport`・`ovSectionViewportSectionViewMatrix`・
  断面群の中身の個数が、写していない対照と一致）。**ただし写す必要はもう無い。**
- **作り方の引数では出せない**——`depth` を 0 以外に / 高さの範囲を建物に合わせる /
  断面線を短く引く / `pt3` を反対側にする、の 4 通りとも `0` のままだった
  （＝**引数の話ではなく表示の話だった**）。
- **断面線オブジェクトとの結び付きでもない**——総当りで**ハンドル型の差は 0 件**。

### 断面ビューポートの注釈に出るグリッド線（通り芯）——符号の位置を決めるのは `ShoulderLengthAtStart`（#189）

断面ビューポートを更新すると、**切断面を横切る通り芯が「グリッド線」として注釈の中に
現れる**（UI の「ビューポート注釈の編集」で選べるもの）。符号（ラベル枠）を用紙の上で
動かしたいなら**触るのはこの個体の 1 欄だけ**で、断面の高さ範囲では動かない。

実機確認済み（VW 2026 / mac・日本語 UI・新規の空図面。
[issue #189](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/189)。
プローブ `section-vp-grid-annotation` を **5 版**走らせた実測で、以下の数値は実行ログそのまま。
プローブは役目を終えたので消してある）。

#### 正体と見つけ方

- **正体はデザインレイヤの通り芯と同じ PIO**——`GridAxis`（内部 ID **647** ＝
  `kInternalID_GridAxis`、ローカライズ名 **「グリッド線」**）。パラメータ表も**同じ 32 件**で、
  デザインレイヤの個体と universal 名・欄型・既定値が一致する。**別物を探さなくてよい。**
- **注釈群を自分で降りる**（criteria では見つからない。下記「注釈の中の図形は…」）。
  通り芯 1 本につき個体 1 つ。
- **`ovPositionLocked`（709）が `true`**。SDK ヘッダのコメント
  （*"GridAxisInstances are always position locked."*）がこの個体を指している。
  **ロックされていても `SetParamReal` と `ResetObject` は通る。**
- **SDK 側にグリッド線専用の API・オブジェクト変数は 1 つも無い**【ヘッダ根拠】。
  `SDKLib` 全体（`Include` ＋同梱 `Source`）を `GridAxis|HorizLine|GridLineLength|SectionGrid|GridBubble`
  で引いて出るのは 4 件だけ——上記 647、`kInternalID_GridBubble = 127`、
  `kObjectStylesGridAxisFolder = 332`、そして `ovPositionLocked` のコメント。
  **触る道はパラメータ経由しか無い。**

#### いつできるか——`UpdateViewport` が作り、更新し直しても作り直さない

| 段 | 注釈群 | 中身 |
| --- | --- | --- |
| `CreateSectionViewport` の直後 | **`nil`** | — |
| `UpdateViewport` の後 | 取れる | 通り芯 3 本 → **`GridAxis` 3 個**（＋型 76・型 90） |
| もう一度 `UpdateViewport` | 同じ | **ハンドルが 3 つとも同一**・件数も同じ・**書いた値も残る** |
| `ovViewportResetForOnlyAnnotationsChange`(1053) を立てて更新 | 同じ | 同上 |

**＝書いた値が更新で消えることはない。** 注釈へレベル基準線などを置くときと違って、
グリッド線は**自分で作るものではなく、更新が置いていくもの**である。

#### 出すには「通り芯が切断面を横切っている」ことが要る——しかも点で置いた通り芯では出ない

- **`CreateCustomObject("GridAxis", 位置, 角度)` で置いた通り芯は「線」を 1 本も持たない。**
  外接は **750 × 1225**（ラベル枠と水平線だけ）で、切断面まで届かない。
- **`GridAxis` は線分 PIO ではない**——`LineLength` を持たないので
  `Set/GetLinearObjectPos` は**完全な no-op**（下記
  [Parametric Objects「線分 PIO の両端と長さ」](Parametric%20Objects.md)のとおり）。
  **長さのパラメータも 32 件のどこにも無い。**
- **したがって線は「パス」で与える**——`CreateCustomObjectPath` に**2 頂点の 2D ポリライン**を
  渡す（パスの型は [Parametric Objects「パスの型を間違えると…」](Parametric%20Objects.md)）。
  こうして作った通り芯は `GetObjectPath()` が非 nil になり、外接がパスの範囲まで伸びる。

  ```cpp
  VWFC::VWObjects::VWPolygon2DObj path({VWFC::Math::VWPoint2D(x, y0),
                                        VWFC::Math::VWPoint2D(x, y1)});
  path.SetClosed(false);
  MCObjectHandle axis = gSDK->CreateCustomObjectPath("GridAxis", path);
  gSDK->AddObjectToContainer(axis, designLayer);   // 作ったものはアクティブレイヤに入る
  gSDK->ResetObject(axis);
  ```

#### 符号を動かす欄——`ShoulderLengthAtStart`。**単位は用紙 mm**

座標欄（`kFieldCoordDisp` ＝ 7）は 32 件中この 2 つだけである。

| 索引 | universal 名 | ローカライズ名 | 既定 | 絵への効き |
| --- | --- | --- | --- | --- |
| 4 | **`ShoulderLengthAtStart`** | 水平線の長さ（先端） | `5` | **符号がこのぶん上へ動く** |
| 5 | `ShoulderLengthAtEnd` | 水平線の長さ（終端） | `5` | **動かない**（下記） |

**単位は用紙 mm**——書いた値に**その容れ物の縮尺**が掛かって世界座標になる。
`5 → 15`（＋10）で測った実測:

| 測った場所 | 縮尺 | 外接の上端の動き | 倍率 |
| --- | --- | --- | --- |
| デザインレイヤの通り芯 | 1/100 | **1000** | **×100** |
| デザインレイヤの通り芯 | 1/50 | **500** | **×50** |
| **注釈のグリッド線** | ビューポート 1/100 | **1000** | **×100** |
| **注釈のグリッド線** | ビューポート 1/50 | **500** | **×50** |

**＝「用紙で 10mm 上げたい」なら、縮尺に関わらず `+10` を書けばよい。** issue #189 が OIP で
見た「5 → 15 で符号が用紙で約 10mm 上へ移る」がそのまま説明できる。
パラメータ `World-based` が `False` であることもこれと合う。

- **`ShoulderLengthAtEnd` は絵を 1mm も動かさない**（`5 → 15` でも `5 → 12000` でも外接が
  変わらない）。`ShowBubbleAt` が **`Start Point`** で、終端側には何も描かれていないため。
  **「始端と終端の両方を書けば確実」と考えて終端を書いても無駄である。**
- **書いた後に `ResetObject` が要る。** 値は書いた直後に読み戻せる（`15.000`）が、
  **外接は `ResetObject` まで動かない**（書いた直後の上端 Δ = 0、Reset 後 Δ = 1000）。
- **スタイルは握っていない。** ツールのプラグインスタイル（`styleRef=51`・「グリッド線」）が
  勝手に当たっているが、`GetPluginStyleParameterType` は `ShoulderLengthAtStart` /
  `ShoulderLengthAtEnd` / `ShowBubbleAt` / `AddElbowToSholder` に **`2`
  （`kPluginStyleParameter_AllwaysByInstance`）**を返す。
  **＝個体へ書けば効く。スタイルを外す必要は無い**（実測でも、スタイルが当たったままの
  個体へ書いて絵が動いた）。`BubbleScaleFactor` と `World-based` は `0`（`_ByInstance`）。

#### 符号の位置の基準は「**映っているモデルの上端**」——高さ範囲では動かない

**1 つずつしか違わない枚を並べて実測した**（注釈座標での外接 y の上端）:

| 枚 | 映っているモデルの高さ | 高さ範囲の上端 | 縮尺 | グリッド線の外接 y の上端 |
| --- | --- | --- | --- | --- |
| **A** | 3000 | 9000 | 1/100 | **4235** |
| **B** | 3000 | **4000** | 1/100 | **4235**（A と一致） |
| **D** | **6000** | 9000 | 1/100 | **7235**（A ＋ 3000） |
| **C** | 3000 | 9000 | **1/50** | **3622.5** |

- **高さ範囲の上端（`endHeight`）を 9000 → 4000 に変えても 1 桁も動かない**（A と B）。
- **映っているモデルを 3000 → 6000 にすると、ちょうど 3000 動く**（A と D）。
- **式は「映っているモデルの上端 ＋ 用紙スケールの固定分」**:

  | 縮尺 | 固定分 | 検算 |
  | --- | --- | --- |
  | 1/100 | **1235** | 3000 + 1235 = **4235**（A・B） / 6000 + 1235 = **7235**（D） |
  | 1/50 | **622.5** | 3000 + 622.5 = **3622.5**（C） |

  固定分は**水平線（`ShoulderLengthAtStart` ＝ 5 用紙 mm）とラベル枠の合計**で、
  用紙 mm なので縮尺で割れる（12.35mm / 12.45mm）。
- **断面が空のときの上端は固定分だけ**（1/100 で `1235`・1/50 で `622.5`）
  ＝**モデルの上端が 0 として扱われる**。

**＝符号は「断面に実際に描かれているものの上端」から、用紙で一定の高さに乗る。**
だから `CreateSectionViewport` の高さ範囲を広げても符号は動かない
（[plugin#181](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz/pull/181)
が上端を 3600mm 上げても寸法と符号が重なったままだった理由）。**用紙の上で符号を動かしたい
なら `ShoulderLengthAtStart` を足す**——それが唯一の手である。

#### 断面に何か映すには、モデルを `pt3` の**反対側**へ置く（`pt3` は「見る側」）

この節を測るために断面を空でなくする必要があり、そこで 3 巡ぶん足を取られたので記録しておく。

- **`pt3` と同じ側にモデルを置いた枚は、断面が空のままだった**——
  `ovSectionViewportDisplayObjectsBeforeCutPlane`（**1065**）を `true` にして
  （書き込みも読み戻しも `true`）**空のまま**で、キャッシュ群も 3/5/6/7/15 すべて 0 件。
  反対側に置いた枚は `1064` だけで映り、**キャッシュ群 5 に 13 件・7 に 2 件**が入った。
  **＝「奥」は `pt3` の反対側**であり、`1065` は手前側を呼び戻してくれない。
  > **【食い違いあり・未解決】** [#200](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/200)
  > で 1 つずつ変えた 8 通りを測ると**逆の結果になった**——断面線がモデルを切っていない
  > 配置では `1064` だけの枚が 5 通りすべて空で、**`1065` を立てた枚だけが描けた**
  > （下記「断面に中身が入る条件」の表）。どちらも実測なので、配置のどの違いが効いて
  > いるかが分かっていない。[#202](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/202)
  > で切り分ける。**当面は 1064 と 1065 の両方を `true` にし、キャッシュ群で描けたかを
  > 確かめる。**
- **グリッド線は断面が空でも注釈に出る**（そのときの上端は上記の固定分だけ）。
  **x の並びは見る側で反転する**（`pt3` を反対側にすると 3 本の順序が逆になる）。
- **`ovViewportDisplay2DComponents`（1059）は書いても入らなかった**——`SetObjectVariable` が
  `true` を返すのに読み戻しは `false`（レンダは先に隠線消去にしてある）。
- **壁を断面に映すなら高さを専用関数で与える**（`SetWallOverallHeights`。
  [Walls](Walls.md)「`CreateWall` が建てた壁は、新規の空図面では高さ 0 になる」）。

#### `GridAxis` のパラメータ表（VW 2026 / mac・日本語 UI。全 32 件）

デザインレイヤの通り芯と、断面ビューポートの注釈のグリッド線で**同一**だった。
`欄型` は `EFieldStyle`（2=真偽 / 3=実数 / 4=文字列 / 7=座標 / 8=ポップアップ /
14=ボタン / 18=クラス）。

| 索引 | universal 名 | ローカライズ名 | 欄型 | 既定値 |
| --- | --- | --- | --- | --- |
| 0 | `Label` | ラベル | 4 | `1`（置いた順に増える） |
| 1 | `Note` | 注釈 | 4 | （空） |
| 2 | `GridLineClass` | グリッド線のクラス | 18 | （空） |
| 3 | `ShoulderLineClass` | 水平線のクラス | 18 | （空） |
| 4 | `ShoulderLengthAtStart` | 水平線の長さ（先端） | **7** | `5` |
| 5 | `ShoulderLengthAtEnd` | 水平線の長さ（終端） | **7** | `5` |
| 6 | `ShowBubbleAt` | ラベル枠の表示 | 8 | `Start Point` |
| 7 | `BubbleScaleFactor` | ラベル枠の倍率 | 3 | `1` |
| 8 | `AddElbowToSholder` | 水平線にひじ部を追加 | 2 | `False` |
| 9–18 | `GridLineStyleByClass` / `GridLineStyle` / `GridLineWeightByClass` / `GridLineWeight` / `GridLineColorByClass` / `GridLineColor_C` / `_M` / `_Y` / `_K` / `_Name` | （空） | 2/1/2/1/2/3/3/3/3/4 | `True` / `2` / `True` / `2` / `True` / `0` / `0` / `0` / `1` / （空） |
| 19–28 | `ShoulderLineStyleByClass` / `ShoulderLineStyle` / `ShoulderLineWeightByClass` / `ShoulderLineWeight` / `ShoulderLineColorByClass` / `ShoulderLineColor_C` / `_M` / `_Y` / `_K` / `_Name` | （空） | 同上 | 同上 |
| 29 | `EditBubbleLayout` | （空） | 14 | （空） |
| 30 | `EditBubbleData` | （空） | 14 | （空） |
| 31 | `World-based` | `__NNA_DO_NOT_CHANGE` | 2 | `False` |

#### 書き方（全断面ビューポートの符号を用紙で 10mm 上げる）

```cpp
// ① 更新してからでないと、注釈群も中のグリッド線もまだ無い
gSDK->UpdateViewport(vp);

// ② 注釈群を降りる（criteria では見つからない）
MCObjectHandle annotation = gSDK->GetViewportGroup(vp, kViewportGroupAnnotation);
for (MCObjectHandle h = gSDK->FirstMemberObj(annotation); h != nil; h = gSDK->NextObject(h))
{
    if (gSDK->GetObjectTypeN(h) != kParametricNode)
        continue;
    VWFC::VWObjects::VWParametricObj pio(h);
    if (pio.GetInternalID() != kInternalID_GridAxis)   // 647
        continue;

    // ③ **用紙 mm** で書く（縮尺は VW が掛ける）。スタイルは外さなくてよい
    const double paper = pio.GetParamReal("ShoulderLengthAtStart");
    pio.SetParamReal("ShoulderLengthAtStart", paper + 10.0);

    // ④ **絵は ResetObject まで動かない**（値だけは書いた直後に読み戻せる）
    gSDK->ResetObject(h);
}
```

**この後にビューポートを更新しても、書いた値は残る**（個体が作り直されないため）。

## 断面ビューポートの注釈座標と用紙座標の対応（#200）

**注釈座標の点が用紙のどこに来るかは 1 本の式で決まる。外接を測る必要は無い。**

```
用紙座標 = 注釈座標 ÷ 縮尺 ＋ ビューポートの位置（1024 / 1025）
```

**注釈座標そのものの読み方**（横 ＝ 断面線に沿った位置 − **断面線の終点**／縦 ＝ モデルの Z）は
[Dimensions](Dimensions.md)「断面ビューポートの注釈空間の座標」にある。**この調査でも同じ値に
なった**——断面線 `x 0→3000` でモデル `x 500〜2500` の枚と、断面線 `x 5000→8000` でモデル
`x 5500〜7500` の枚が、どちらも注釈座標で `−2500〜−500`（＝ `モデル x − 終点 x`）。

**したがって GL（Z = 0）の用紙 y は `ovViewportYPosition`（1025）そのものである。**

### GL は映っているモデルの高さに依らない——実測

高さだけが違う直方体を映す 2 枚を、**同じ高さ範囲（−1000〜9000）・同じ縮尺（1/100）**で作り、
断面に描かれた中身（キャッシュ群）の外接を読んだ:

| 枚 | モデルの Z | 中身の外接の y（注釈座標） | 用紙座標の y | 位置（1025） |
| --- | --- | --- | --- | --- |
| **低** | 0〜**3000** | **0〜3000** | **0〜30** | 0 |
| **高** | 0〜**6000** | **0〜6000** | **0〜60** | 0 |

- **2 枚の下端は `0.0000` でぴったり一致**——GL の在り所は映っているモデルの高さで動かない。
- 注釈座標の y は**モデルの Z とぴったり一致**（上端の差も `0.0000`）。レイヤ Z = 0 の素の
  デザインレイヤで測っているので、**一致するのは絶対 Z** と読む。

係数とオフセットは、注釈へ**4 辺とも空枠の外へ出る大きさ**の矩形（注釈座標
`−300000〜100000 × −400000〜200000`）を置き、用紙座標の外接と突き合わせて解いた:

| | 値 |
| --- | --- |
| k（注釈 → 用紙の倍率） | **(0.0100, 0.0100)** ＝ 1/100 ちょうど |
| off（**左下**の角から解いた） | **(0.0000, 0.0000)** |
| off（**右上**の角から解いた） | **(0.0000, 0.0000)**（左下とのずれ `0.0000`） |

**この矩形はビューポートを (137, −59) 動かした後に測っている。** それでも off は 0 のまま
——**つまり 1024 / 1025 はこの式の「オフセット」として現れているのではない。** 式に位置が
効くのは、注釈座標の側が位置を含んでいるからである（次項）。

- **4 辺とも矩形が作る大きさにすること。** 右上だけ外へ出した版では左下が空枠の縁
  （±26.649）のままになり、幅から解いた k が `1.0366` という無意味な値になった（実際に踏んだ）。

### 注釈座標は「ビューポートが用紙のどこに在るか」を含む

**ビューポートを `MoveObject` で動かすと、注釈の中の図形の `GetObjectBounds` が
「動かした量 × 縮尺」ずれる。** 実測（1/100・(137, −59) 動かした）:

| 読んだもの | 動かす前 | 動かした後 | 差 |
| --- | --- | --- | --- |
| 注釈の矩形（注釈座標） | 左 `−300000` 下 `−400000` | 左 **`−286300`** 下 **`−405900`** | **+13700 / −5900** |
| ビューポート（用紙座標） | 左 `−3000` 下 `−4000` | 左 **`−2863`** 下 **`−4059`** | +137 / −59 |
| 1024 / 1025 | (0, 0) | **(137, −59)** | — |

- **＝注釈空間は「用紙座標を縮尺倍した写し」である。** 同じ図形の注釈座標は、ビューポートが
  動けば書き換わる。**置いた値をそのまま読み戻せるのは、動かしていない間だけ。**
- **プラグイン側の経験則（`draw/DrawUtil.h`「注釈へ置いた実位置の実測はビューポートが用紙の
  どこに在るかに影響される」）の正体がこれである。** 作法も変わらない——
  **測る → タグを置く → 動かす**の順（[Sheet Layers and Page Layout](Sheet%20Layers%20and%20Page%20Layout.md)）。
- **GL を揃えて並べるなら、外接を測ってはいけない。** `MoveObject` で **1025 を目標の用紙 y に
  合わせる**だけでよい（`CreateSectionViewport` 直後の 1024 / 1025 は `(0, 0)` なので、
  1 回動かせば済む）。**外接の中心で合わせると、通りごとに違う外接——映る架構の高さ・通り芯の
  符号（上記「符号の位置の基準は『映っているモデルの上端』」）——に引きずられて GL が揃わない。**

### `GetObjectBounds(viewport)` が何で決まるか

| 状態 | 外接（用紙座標） |
| --- | --- |
| 断面が空・注釈も空 | **±26.649（53.3mm 角）の「×」印の空枠** |
| 断面に中身がある | **映っている中身そのもの**（低 `−25/0/−5/30`・高 `−25/0/−5/60`） |
| 注釈を足した直後（`ResetObject` 前） | **1mm も動かない** |
| 注釈を足して `ResetObject` した後 | **注釈も入る**（空枠より大きければ注釈の矩形そのもの） |

- **高さ範囲（`startHeight`〜`endHeight`）は入らない。** 16 枚すべて同じ `−1000〜9000` で
  作ったのに、外接の高さは低 30mm・高 60mm と**映っているモデルのぶんだけ違った**（差 `30.0000`）。
  **高さ範囲を広げても外接は 1mm も広がらない。**
- **空枠は中身や注釈に「置き換わる」**（和を取るのではない）——中身が入った枚の外接は
  `−25/0/−5/30` で、±26.649 の縁は 1 つも残っていない。
- **したがって外接は「描かれたか」の判定に使える**——ただし**`ResetObject` を通した後**で、
  かつ**空枠（53.3mm 角）より大きいものが入ったとき**だけ。小さいものは空枠に隠れる
  （[Dimensions](Dimensions.md) の訂正の記録を参照）。

### 併せて測れた値

| 読んだもの | 結果 |
| --- | --- |
| `ovViewportXPosition` / `YPosition`（**1024 / 1025**） | **用紙 mm での位置。** ヘッダは read only だが **`MoveObject` で動く**（(0,0) → (137,−59)）。`VWViewportObj::GetPosition()` がこの 2 つを読む |
| `ovViewportTransformMatrix`（1049） | 平行移動 = **位置 × 縮尺**（`(13700, −5900, 0)`）。回転部は単位行列 |
| `ovViewportViewMatrix`（1050）/ `ovSheetLayerSectionViewportViewMatrix`（1055） | **互いに一致。モデル → 断面ビューの行列**で、平行移動は `(pt1.x, 0, −pt1.y)`・回転部は `[−1 0 0 / 0 0 1 / 0 1 0]`。**用紙とは無関係で、`MoveObject` でも動かない** |
| `ovViewportOperatingTransform`（1051） | **読めない**（`GetObjectVariable` が `false` を返す） |
| `ovSectionViewportSectionViewMatrix`（1056） | **単位行列** |
| `ovViewportUnscaledBoundsWithoutAnnotations`（1052） | **断面が空の枚では「空（反転した矩形）」**。**中身のある枚では未測**（[#202](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/202) へ切り出した） |
| crop（`ViewportHasCropObject` / `GetViewportCropObject`） | **無い**（`CreateSectionViewport` 製。`false` / `nil`） |
| 注釈群（`GetViewportGroup(vp, kViewportGroupAnnotation)`） | **最初の注釈を足すまで `nil`**。足した後も、外接が読めるのは `ResetObject` の後 |

- **空の容れ物の外接は「反転した矩形」で返る**（左 = `+DBL_MAX`・右 = `−DBL_MAX`）。
  `GetObjectBounds` は `true` を返すので、**`左 > 右` で空を判定する**。1052 も注釈群も
  この形だった。**`true` が返ったことを「読めた」と読んではいけない。**

### 断面に中身が入る条件——1 つずつ変えた 8 通り

**キャッシュ群が「描けたか」の唯一の判定手段である。** 群 4（断面群）は空でも必ず 2 件在るので、
**3 / 5 / 6 / 7 / 15 に件数が入ったか**を見る（上記「1050 が更新のときに断面の向きへ入るのは…」の
表と同じ読み方）。絵を見なくても機械で分かる。

モデルを固定（x 500〜2500 / 5500〜7500・**y 2000〜3000**・Z 0〜3000 / 0〜6000）して実測:

| 通り | 断面線 | pt3 | depth | 高さ範囲 | 1064 | 1065 | 描けたか |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **0** 基準 | y=−3000（モデルの手前） | y=−8000 | 0 | −1000〜9000 | T | F | **空** |
| **1** | 基準と同じ | 同じ | **30000** | 同じ | T | F | **空** |
| **2** | 基準と同じ | **y=+8000** | 0 | 同じ | T | F | **空** |
| **3** | **y=2500（モデルを切る）** | y=−2500 | 0 | 同じ | T | F | **描けた**（群 5=3 / 6=3 / 15=7） |
| **4** | 基準と同じ | 同じ | 0 | **0〜9000** | T | F | **空** |
| **5** | 基準と同じ | 同じ | 0 | 同じ | T | **T** | **描けた**（群 5=7） |
| **6** | **y=6000（モデルの奥）** | y=11000 | 0 | 同じ | T | F | **空** |
| **7** | 基準と同じ | 同じ | 0 | 同じ | **F** | **T** | **描けた**（群 5=7） |

- **描けた 3 通りの「中身の外接」は完全に同一**（`−2500/0/−500/3000`）。
  **＝ 1064 / 1065 や断面線の位置は「映るか」を決めるだけで、「どこに映るか」は変えない。**
  だから上の変換は、どの通りで描いても同じ式である。
- `depth` は 0 と 30000 で差が無く（0 と 1）、高さ範囲の下限も −1000 と 0 で差が無かった
  （0 と 4）。**どちらも「空になる原因」ではない。**
- **表示設定（表示レイヤ・クラス）は原因ではない**ことも同時に確かめてある——空だった枚も
  表示レイヤ 3/3・クラス 46/46 が成功し、`1064` の読み戻しも `true`、1050 も断面の向きに
  入っていた。**#151 の表の行 C（＝切るものが無い）と同じ顔**である。

> **【既存の記述と食い違う】** 上記「断面に何か映すには、モデルを `pt3` の**反対側**へ置く」は
> 「**`1065` は手前側を呼び戻してくれない**」、[Dimensions](Dimensions.md) は「断面線を対象の
> 手前に引いて奥を見る配置では `1064` が `true` でなければ対象ごと消える」と書いているが、
> **この 8 通りでは逆の結果になった**——`1064` だけでは 5 通りすべて空で、`1065` を立てた枚が
> 描けた。**どちらも実測なので、配置（断面線の向き・pt3 の側・モデルの側）のどの違いが効いて
> いるかがまだ分かっていない**ということである。
> [#202](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/202) へ切り出した。
> **当面は `1064` と `1065` の両方を `true` にし、キャッシュ群で描けたかを確かめるのが安全。**

## 注釈の中の図形は、検索条件（criteria）では見つからない

**`ISDK::ForEachObjectInCriteria` は、ビューポートの注釈の中の図形を 1 件も返さない**
（実測。UI で置いたレベル基準線が 2 つの断面ビューポートの注釈に計 8 本ある図面で、
`(PON='Elevation Benchmark2')` の件数は **0**。自前で走査すれば 8 件見つかる）。
注釈は「レイヤ直下の図形の連なり」の外にある容れ物なので、**探すなら自分で降りる**。

```cpp
// レイヤ → 直下 → グループ → **ビューポートの注釈群** の順に降りる。
// kParametricNode=86 / kGroupNode=11 / kViewportNode=122（Objs.TDType.h）。
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

- **注釈の中にいるかは `ISDK::IsViewportGroupContainedObject(h, kViewportGroupAnnotation)`**
  で確かめられる（上の経路で拾った個体はすべて `true` だった）。
- 注釈群そのものは `ISDK::GetViewportGroup(vp, kViewportGroupAnnotation)`、
  逆向き（群 → ビューポート）は `GetViewportGroupParent`。

## 打ち切った調査: 断面ビューポートの範囲を「無限」にする

UI で手作りすると〈無限〉が既定なので、公開ヘッダに載っていないオブジェクト変数
（欠番 1060〜1090）に範囲が載っているのではと疑い、読み取り専用の一時診断で実機で
確かめた（手で 1 枚だけ〈無限〉に変えてから再生成し、値の違う変数を探す）。
**結果は断面ビューポート 66 枚・値の組み合わせ 1 通り＝差のある変数は 1 つも無し。**
範囲はオブジェクト変数の外に保持されており、SDK からは触れない。対象の大きさから決めた
有限範囲で確定（実用上は無限と同じ見え方になる）。

## `CreateViewport(parentHandle)` の `parentHandle` は「**どの容れ物に置くか**」

**デザインレイヤを渡すと、デザインレイヤの上にビューポートができ、注釈を持てない。**
実測（VW 2026 / mac。[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)
で 3 回踏んだ）:

| 渡したもの | `ParentObject(vp)` | `AddViewportAnnotationObject` | `GetViewportGroup(vp, kViewportGroupAnnotation)` |
| --- | --- | --- | --- |
| **デザインレイヤ** | そのデザインレイヤ | **`false`**（矩形でも PIO でも） | **`nil`**（呼んだ後も） |
| **シートレイヤ** | そのシートレイヤ | `true` | 取れる（`IsViewportGroupContainedObject` も `true`） |

**シートレイヤを渡し、表示するデザインレイヤは作成後に決める:**

```cpp
MCObjectHandle vp = gSDK->CreateViewport(sheetLayer);        // ★ 置く容れ物を渡す
gSDK->SetViewportLayerVisibility(vp, designLayer,
                                 VWFC::VWObjects::kLayerVisibilityNormal);  // 何を映すか
TVariableBlock scale;
scale = static_cast<Real64>(50.0);
gSDK->SetObjectVariable(vp, ovViewportScale, scale);          // 既定は 1:1 なので自分で書く
gSDK->UpdateViewport(vp);
```

- **【ヘッダ根拠】はもともと [Undo](Undo.md) に書いてあった**（`GS_CreateViewport` の説明:
  "The specified parent handle may only be a layer or a group contained within a layer,
  nested or otherwise"）。**「どのデザインレイヤを表示するか」と読み違えると、注釈を
  持てないビューポートが黙って出来上がる**——`CreateViewport` も
  `SetObjectVariable(ovViewportScale)` も `UpdateViewport` も成功するので、
  `AddViewportAnnotationObject` が `false` を返すまで気付けない。
- **`CreateLayer(..., kLayerSheet)` はアクティブレイヤを切り替える**（実測で確認）。
  ただし `CreateViewport` は**アクティブレイヤではなく渡した `parentHandle`** に置くので、
  アクティブレイヤを合わせても直らない。
- 注釈は**ビューポートの縮尺の容れ物**である——縮尺を 1/50 → 1/100 に変えると、注釈の
  中身の世界座標が比例して書き換わり、**紙の見え方は保たれる**（図面ラベルで実測。
  寸法でも同じ。[Dimensions](Dimensions.md)）。
