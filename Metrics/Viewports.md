# ビューポート（平面図・断面） — 計測記録

結論は [Findings「Viewports」](../Findings/Viewports.md)。ここには各節の測った環境（VW の版・OS・
図面）・使ったプローブ・手順・実測の表とログ・切り分けの経緯を置く。節の見出しは Findings と同じ。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

## クラス表示

- 表示レイヤを指定どおりに絞って作ったビューポートで、クラスが全部非表示の状態で生成された。
  `VWClass::ForEachClass`（＝`ISDK::ForEachClass`）でゲスト（参照ファイル由来）を含む全クラスを
  表示にすると出た。
- `SetUseDocumentClassVis`（オブジェクト変数 1031）のヘッダの但し書きは "for dlvps"
  （デザインレイヤビューポート専用）。
- ビューポートのクラス表示設定より後に注釈へ足した図形のクラスは、ビューポートで非表示のまま
  だった（[Data Tags](../Findings/Data%20Tags.md)）。

## 平面図（2D/平面）への作り直し

- `CreateViewport` が作ったビューポートは、OIP 上は〈2D/平面〉なのに描画キャッシュが 3D の
  「上」ビューのままだった。更新ボタンでも、Project 2D（オブジェクト変数 **1005**）を ON に
  するだけでも作り直されなかった。
- ユーザーの手動操作をなぞった順——ビューの向きを〈上〉（`standardViewTop` = **1007**）にし、
  Project 2D をいったん OFF にして**更新を挟み**、再度 ON に戻して最後の更新（表示レイヤを
  絞った後・最後の更新の前）——で平面になった。入ったかは `GetProject2D` で読み戻した。

## `UpdateViewport` は描く——が、**戻った時点では描き終わっていない**（#215）

**実機確認済み**（VW 2026 / macOS。`probes/runtime/render-sync-3d/` を 4 回。
[issue #215](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/215)。
プローブは役目を終えたので消してある）。詳しい数字と、レイヤの描画モード
（`SetRenderMode`）側の話は
[周期実行と通知](../Findings/Timers%20and%20Notifications.md) の 7 節「道具について」にある。

- 表示レイヤを入れて（`SetViewportLayerVisibility(vp, layer, 0)`）`SetDirty(true)` を立ててから
  `UpdateViewport` を呼ぶと、キャッシュ群（`kViewportGroupCache` = **3**）に中身が
  入った（球 196 個のモデルで **2 件**・外接 `左-1341.3 上1341.3 右1323.6 下-1323.6`）。
- ヘッダ（`APIBase.Legacy.Defs.h:5539`）: "Updates the specified
  viewport: **a dirty viewport, whose render type is other than wireframe or sketch**,
  will be re-rendered."。作りたてのビューポートに `UpdateViewport` を呼ぶと
  0 ms で戻り、何も起きなかった。
- 戻るまでは 355 ms（初回）/ 68 ms（2 度目）。戻った後にランループを回すと更に 545 ms ぶん
  仕事が出てきた（1 周が 301 ms まで伸びる）。利用者の画面では、プローブが戻った後も
  ステータスバーに「Viewport-1 の更新」が **16 秒**出ていた。
- 更新の直後に `IsDirty()` を読むと **`no`** を返したが、描画はまだ続いていた。
- `UpdateViewport` の直後に `no` だった `IsDirty()` は、undo イベントが閉じると `yes`
  へ戻った（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。
  断面・注釈つきの 11 枚すべてで、`ovViewportDirty`（1004）も同じ動き）。書いた直後と
  呼び出しの中でランループを 1.5 秒回した後は `no`、自動で開いた undo イベントを
  `EndUndoEvent()` で閉じた直後から `yes`、VW へ戻った後も `yes` のまま。中身
  （キャッシュ群）・外接・1050 は 1 つも変わらなかった。
- 外接（`GetObjectBounds(viewport)`）は更新の前後とも空枠（±26.649mm）のままだった。
- `VWViewportObj::SetScale()` を 100 → 50 にしたら、キャッシュ群の外接が ±2682.5 → ±1341.3 と
  半分になった。
- `VWLayerObj::SetSheetPrintDPI(300)` の直後の `GetSheetPrintDPI()` が **72** を返した
  （シートレイヤに対して。2 回再現）。

## 断面ビューポート

- 作った口: `ISDK::CreateSectionViewport`（pt1, pt2, pt3, depth, startHeight, endHeight, layer）。
  pt3 は「見る側」を示す点。
- `depth` = 0 → OIP で〈切断面より奥: 無限〉。`startHeight` / `endHeight` = 0 → 〈有限・0〜0〉に
  なって断面から建物が消えた。
- オブジェクト変数: `ovSectionViewportDisplayObjectsBeyondCutPlane` ＝ **1064** /
  `ovSectionViewportDisplayObjectsBeforeCutPlane` ＝ **1065**、プレイナー（アクティブレイヤ平面）
  図形の表示 … **1035**、2D コンポーネントの表示 … **1059**（既定は非表示）。
- レンダリングがシェイドのままでは 1059 を書いても入らなかった。
  `VWViewportObj::SetRenderType(renderFinalHiddenLine)`（隠線消去）を先にすると入った。
  手作りの断面と OIP の項目が違って見えたのもレンダリングの違いだった。
- 注釈へレベル（標高）オブジェクトを置いたときの記録は
  [Level Objects](../Findings/Level%20Objects.md)。

### 断面に中身が入る条件——`1065` が主スイッチで、`1064` は「奥だけ」への絞り込み（#202）

（**VW へ戻った後に読み直しても同じ**——[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。下の 16 通りを同じプローブで作り直し、
undo イベントを閉じた直後と、VW へ戻った後に名前から引き直して読んだ。**16 枚ともキャッシュ群の
件数と出た箱が 1 つも変わらなかった**——(F,F)/(T,F) の「何も出ない」も、戻った後に中身が
入ってくることはない。変わるのは `IsDirty` だけ（上記「`UpdateViewport` は描く」）。）

実機確認済み（VW 2026 / macOS・新規の空図面。
[issue #202](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/202)。
プローブ `section-vp-cutplane-sides`（ビルド `1d5b70f53506`）の実行ログそのままで、
プローブは役目を終えたので消してある）。

**測り方**——切断面（`y=2500`）の両側に箱を 1 つずつ置き、
**Z の範囲が重ならないようにずらした**: **−Y 側の箱は Z 0〜3000 / +Y 側の箱は Z 5000〜8000**
（どちらも切断面から 4500〜5500mm 離れている）。断面の中身の外接の y はモデルの Z
（下記「注釈座標と用紙座標の対応」）なので、外接の y で「どちら側が出たか」を見分けた。
1064/1065 はどちらも明示的に書いて測った（`CreateSectionViewport` が置く既定値は測っていない）。
断面線の向き（`pt1`→`pt2`）は `+x` と `−x` を 8 枚ずつ振った。

#### 切断面がモデルを**切っていない**とき——4 通りで全部違う

| `1064`（奥） | `1065`（手前） | 断面に出るもの | キャッシュ群 3/5/6/7/15 |
| --- | --- | --- | --- |
| F | F | **何も出ない** | 3=nil 5=nil 6=nil 7=nil 15=nil |
| **T** | F | **何も出ない** | 3=nil 5=nil 6=nil 7=nil 15=nil |
| F | **T** | **切断面の両側が出る** | 3=nil **5=9** 6=nil 7=3(空) 15=nil |
| **T** | **T** | **`pt3` の反対側（＝奥）だけ**（手前側は落ちる） | 3=nil **5=7** 6=nil 7=2(空) 15=nil |

- 群 4（断面群）は 4 通りとも 2 件（外接は空）。
- `(T,F)` では `1064` の読み戻しは `true` なのに 1 件も入らなかった。
- 断面線の向き `+x` / `−x` の 8 枚ずつで、4 通りすべて同じ結果だった（プローブ側の機械判定でも
  「断面線の向きで変わるか=いいえ」）。

#### 切断面がモデルを**切っている**とき——4 通りとも描ける

| `1064` | `1065` | キャッシュ群 3/5/6/7/15 | 中身の外接（注釈座標） |
| --- | --- | --- | --- |
| F | F | 5=3(空) **6=3** 7=2(空) **15=7** | `−2500 / 0 / −500 / 3000` |
| T | F | **5=3** 6=3 7=2(空) 15=7 | 同じ |
| F | T | **5=9** 6=3 7=3(空) 15=7 | 同じ |
| T | T | **5=8** 6=3 7=3(空) 15=7 | 同じ |

- 群 **6**（3 件）と **15**（7 件）は 4 通りとも同じ。変わったのは群 5 の件数だけ。
  中身の外接は 4 通りとも同一。

#### これで #189 と #200 の「逆の結果」が両方説明できる

| 記録 | 配置 | `1064` | `1065` | 実測 | 上の規則では |
| --- | --- | --- | --- | --- | --- |
| **#189 の A** | 切っていない・モデルは `pt3` の**反対側** | T | **T** | 描けた | `(T,T)` ＋ 奥側 → **出る** ✓ |
| **#189 の E** | 切っていない・モデルは `pt3` と**同じ側** | T | **T** | 空 | `(T,T)` ＋ 手前側 → **落ちる** ✓ |
| **#200 の 0・2・4・6** | 切っていない | T | F | 空 | `(T,F)` → **何も出ない** ✓ |
| **#200 の 3** | **切っている** | T | F | 描けた | 切り口はフラグ不問 → **出る** ✓ |
| **#200 の 5** | 切っていない・奥側 | T | T | 描けた | `(T,T)` ＋ 奥側 → **出る** ✓ |
| **#200 の 7** | 切っていない・奥側 | F | T | 描けた | `(F,T)` → **両側** ✓ |

- #189 は 6 枚すべてで `1064` と `1065` の**両方**を `true` にしていた。#200 は `1065` を振ったが、
  モデルを切断面の片側にしか置いていなかった。

#189 の配置（断面線 `y=4000` / 箱は +Y 側の `y 6000〜8000` だけ）を同じ図面で再現した枚:

| 枚 | `pt3` | `1064` | `1065` | 実測 |
| --- | --- | --- | --- | --- |
| #189 の A 相当 | −Y 側（箱の反対側） | T | T | **描けた**（群 5=7・外接 `−8000 / 0 / 0 / 3000`） |
| #189 の E 相当 | **+Y 側（箱と同じ側）** | T | T | **空**（#189 と同じ） |
| 同じ配置 | −Y 側 | **T** | **F** | **空** |
| 同じ配置 | −Y 側 | **F** | **T** | **描けた**（A 相当と同じ外接） |

### 【罠】作ったばかりの押し出しは、外接を読むか `ResetObject` するまで断面に映らない（#248）

（実測。VW 2026 Update 6・Windows 11 ARM64 の VM。2026-10-27。
[issue #248](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/248)。
プローブ `section-vp-order`（ビルド `edf8b5962323`）。1 本の新規図面に、場面（デザインレイヤ
2 枚に S 箱・N 箱。上の #202 と同じ配置。箱は `VWExtrudeObj(VWPolygon2D, 基準高, 厚み)` で作った）
→ 用紙 → (1064,1065)=(F,T) の断面、を腕 A〜G の
順に作った——A 対照／B 同じ場面で 2 枚目／C 場面を作り直す（#245 の c1 と同じ形）／D・E
用紙と場面を入れ替える／F `SetCurrentLayer` してから／G `DeselectAll` してから。）

| 箱を作った直後に | A〜G（7 枚） |
| --- | --- |
| 何もしない | **7 枚とも空**（群 3/5/6/7/15 が `nil`、群 4 だけ 2）。VW へ戻った後に名前で引き直しても、`UpdateViewport` し直しても空 |
| 何もしない（切断面を切る X 箱を、映さないレイヤに足す） | 7 枚とも空 |
| `GetObjectBounds(箱)` | **7 枚とも中身あり**（群 5=9・群 7=3、外接の y 0〜8000＝S と N の両方。#202 の (F,T) と同じ） |
| `ResetObject(箱)` | **7 枚とも中身あり**（同上） |

- #245 の c0 はログのために箱の外接を読んでいて（`DfBounds`）、腕ごとに場面を作り直した c1 は
  読んでいなかった。#202 のプローブも箱を作った直後に外接と `GetExtrudeValues` を読んでいた
  （同じプローブを Windows でもう一度走らせて、mac と同じ結果を確かめた）。
- SDK 同梱のソース: VWFC の `VWExtrudeObj.cpp` で `ResetObject` を呼ぶのは、始点・終点を
  受け取る 2 つのコンストラクタ（`:112`・`:138`）だけで、`(VWPolygon2D, 基準高, 厚み)`
  （`:25`）は呼ばない。
- ほかの種類の図形・`CreateExtrude` 系の ISDK 直呼びは測っていない。
- この VM で、断面ビューポートを作った後に **VW が描画のスレッド
  （`d3d10warp.dll`。VM のソフトウェア描画）の中の番地 0 の実行で落ちた**
  （作ってから 30 秒後。WER のダンプで確かめた。もう 1 回、数分後に同じ「番地 0」で落ちたが
  ダンプは無い）。プローブの呼び出しの最中ではない
  （[#270](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/270)
  と同じく Direct3D 11 の WARP）。

### SDK で作った断面ビューポートの注釈でレベル基準線に高さを出す（#147 → #151 で訂正）

> **【訂正の記録】** #147 でマージした手順は「注釈を置き終えてから 1055 を 1050 へ写し、
> 個体を `ResetObject` する」だった。[issue #151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)
> で測り直すと、表示レイヤとクラスを表示へ戻してから更新すれば、`UpdateViewport` が 1055 と
> 同じ値を 1050 へ入れ、以後の更新でも保った（写さずに入った）。#147 で「写した値が更新の
> たびに単位行列へ戻る」と見えたときは、更新の時点で表示設定が揃っていなかった。#147 は
> 「SDK 製はキャッシュ群を 1 つも持たない」とも記録していた。#147 の【推定】「属性や
> パラメータの変更で個体が作り直されて `0` に落ちる」は、下の表で落ちなかった。

（**VW へ戻った後に読み直しても同じ**——[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。表示レイヤだけ・クラスだけの枚の 1050 は
undo イベントを閉じた後も戻った後も単位行列のまま、両方の枚は 1055 と一致したまま。）
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

- C はキャッシュ群が空のまま、1050 だけが断面の向きになった。
- D で単位行列へ戻った後、表示へ戻して更新すると再び入った。
- `CreateSectionViewport` で作った直後は、表示レイヤが 1 枚も表示になっていなかった。
- B の枚で呼んだ順は Findings の手順のコードと同じ（`CreateSectionViewport` → `ForEachLayerN` で
  デザインレイヤを `SetViewportLayerVisibility(vp, layer, 0)` → `ForEachClass` で
  `SetViewportClassVisibility(vp, GetObjectInternalIndex(cls), 0)` → レンダ・1064・1035・1059 →
  `UpdateViewport` → 注釈へレベル基準線を置き 3 つ組を書いて `ResetObject`。1050 へは何も
  書いていない）。1007 も更新のときに値が変わった（下記）。

実測（実物件の図面。`1階` / `FL`、絶対Z **`612`**。正解をログへ出してから突き合わせた）:

| どこへ置いたか | 1050 へ写したか | `ResetObject` の後の `Elev` |
| --- | --- | --- |
| **B**（表示を倒して更新した SDK 製） | **写していない** | **`612`**（正解） |
| 既存の断面ビューポート（1050 が入っているもの） | 写していない | **`612`** |
| 置いた直後（まだ作り直していない） | — | `0`（＝**作り直しは要る**） |

**後から触ったとき**（B・既存の両方で同じ結果）:

| したこと | `Elev` |
| --- | --- |
| ラベルを 100mm 動かす（`SetPointObjectPos`） | `612` |
| 見た目の欄（`EPfx`）を書く | `612` |
| 線の太さを変える（`GS_SetLineWeight`） | `612` |
| **クラスを変える**（`SetObjectClass`） | `612` |
| `ResetObject` する | `612` |
| ビューポートを更新する | `612` |
| 更新の後にもう一度 `ResetObject` する | `612` |

- 既存の断面ビューポート（インポータが作ったもの 2 枚）で、更新の前後でオブジェクト変数
  1000〜1150 を総当り比較すると、変わったのは `ovViewportDirty`（1004）の 1 欄だけだった。
- `ovViewportViewType`（1007）へ `SetObjectVariable` は `false`。`VWViewportObj::SetViewType` も
  入らなかった。1050 に値が入ると 1007 も変わった（作った直後は `7`＝上、横向きの断面なら
  `6`＝右、真横なら `4`＝後）。
- `ovViewportResetForOnlyAnnotationsChange`（1053）は書けた（`true` が返り、1050 も保たれた）。
  `ovViewportDirty`（1004）も書けた。
- `IsElevationBenchmarkConstrained`（`IMarkersPluginSupport`。`VectorworksSDK.h` からは
  引き込まれないので名指しで include した）は、断面ビューポートの注釈に置いた個体では `true`
  （UI／インポータが置いたものも、SDK でいま置いたものも）。空図面の断面ビューポートでは
  `false` だった。
- 断面線（`Section Line2` ＝ `kInternalID_SectionLine2` ＝ 645）: デザインレイヤ 24 枚を走査して
  **0 件**。ビューポートの断面群（`kViewportGroupSection` ＝ 4）の中に居た。
  `CreateSectionViewport` が作った直後の SDK 製にも 1 本入っている（断面群は
  `type0` × 1 ＋ 断面線 × 1 の 2 件）。UI／インポータ製のその 1 本は 20 欄を持ち、
  **`Linked To` にビューポートの名前**（`Viewport-7`）・`Drawing Title` / `Drawing Number` /
  `Sheet Number`・`Configuration = Section`・`Link State = 2`・`Auto-Coordinate = True` などが
  入っている。
- `GS_CreateSectionLineInstance` / `GS_IsSectionLineLinkedToViewport` はヘッダにあるが
  リンクできなかった（`APP_API_FUNCTION` 製の未公開 API。
  [調査の作法](../Findings/Investigation%20Techniques.md)）。
- #147 が UI 製の姿として記録した「1050 ＝ 1055・1007 ＝ 6」は、インポータ（SDK）が作った
  断面ビューポート 29 枚がそのまま持っていた姿だった。更新しても保たれた。
  - **測った範囲**: 実機で開いた図面の断面ビューポート 29 枚（すべてインポータ製）と、
    その場で作った SDK 製 5 枚。**UI の断面ビューポートツールで作ったものは測っていない**
    （2 × 2 が SDK 製だけで割れたため）。

#### #147 で潰した筋（そのまま有効）

- オブジェクト変数 1000〜1150 を UI 製と SDK 製で総当りに突き合わせると**差は 14 件**。
  そのうち 12 件を 1 つずつ書き分けて、**数値が出たのは 1050 だけ**だった（他の 11 件は
  すべて `0` のまま）。残る 2 件は、1055 が書けず（`SetObjectVariable` が `false`。ヘッダの
  コメントも `read` のみ）、1064 は 1050 と併せて書いても打ち消さなかった。
- 1050 を写した枚は、`ovIsSectionViewport`・`ovSectionViewportSectionViewMatrix`・
  断面群の中身の個数が、写していない対照と一致した。
- 作り方の引数——`depth` を 0 以外に / 高さの範囲を建物に合わせる / 断面線を短く引く /
  `pt3` を反対側にする、の 4 通りとも `0` のままだった。
- 断面線オブジェクトとの結び付き——総当りで**ハンドル型の差は 0 件**。

### 断面ビューポートの注釈に出るグリッド線（通り芯）——符号の位置を決めるのは `ShoulderLengthAtStart`（#189）

断面ビューポートを更新すると、切断面を横切る通り芯が「グリッド線」として注釈の中に
現れた（UI の「ビューポート注釈の編集」で選べるもの）。

実機確認済み（VW 2026 / mac・日本語 UI・新規の空図面。
[issue #189](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/189)。
プローブ `section-vp-grid-annotation` を **5 版**走らせた実測で、以下の数値は実行ログそのまま。
プローブは役目を終えたので消してある）。

#### 正体と見つけ方

- 注釈の中の個体は `GridAxis`（内部 ID **647** ＝ `kInternalID_GridAxis`、ローカライズ名
  **「グリッド線」**）。パラメータ表は**同じ 32 件**で、デザインレイヤの個体と universal 名・欄型・
  既定値が一致した。通り芯 1 本につき個体 1 つ。見つけたのは注釈群を自分で降りる走査
  （下記「注釈の中の図形は…」）。
- `ovPositionLocked`（709）が `true`。SDK ヘッダのコメントに
  *"GridAxisInstances are always position locked."* とある。ロックされた個体へ `SetParamReal` と
  `ResetObject` は通った。
- `SDKLib` 全体（`Include` ＋同梱 `Source`）を `GridAxis|HorizLine|GridLineLength|SectionGrid|GridBubble`
  で引いて出るのは 4 件だけ——上記 647、`kInternalID_GridBubble = 127`、
  `kObjectStylesGridAxisFolder = 332`、そして `ovPositionLocked` のコメント。

#### いつできるか——`UpdateViewport` が作り、更新し直しても作り直さない

| 段 | 注釈群 | 中身 |
| --- | --- | --- |
| `CreateSectionViewport` の直後 | **`nil`** | — |
| `UpdateViewport` の後 | 取れる | 通り芯 3 本 → **`GridAxis` 3 個**（＋型 76・型 90） |
| もう一度 `UpdateViewport` | 同じ | **ハンドルが 3 つとも同一**・件数も同じ・**書いた値も残る** |
| `ovViewportResetForOnlyAnnotationsChange`(1053) を立てて更新 | 同じ | 同上 |

#### 出すには「通り芯が切断面を横切っている」ことが要る——しかも点で置いた通り芯では出ない

- `CreateCustomObject("GridAxis", 位置, 角度)` で置いた通り芯は線を 1 本も持たなかった。
  外接は **750 × 1225**（ラベル枠と水平線だけ）で、切断面まで届かない。
- `GridAxis` は `LineLength` を持たず、`Set/GetLinearObjectPos` は何も変えなかった（no-op。
  [Parametric Objects「線分 PIO の両端と長さ」](../Findings/Parametric%20Objects.md)）。
  32 件のパラメータに長さのものは無い。
- `CreateCustomObjectPath` に 2 頂点の 2D ポリライン（`VWPolygon2DObj`・`SetClosed(false)`）を
  渡して作った通り芯は、`GetObjectPath()` が非 nil になり、外接がパスの範囲まで伸びた
  （作り方のコードは Findings）。

#### 符号を動かす欄——`ShoulderLengthAtStart`。**単位は用紙 mm**

座標欄（`kFieldCoordDisp` ＝ 7）は 32 件中この 2 つだけである。

| 索引 | universal 名 | ローカライズ名 | 既定 | 絵への効き |
| --- | --- | --- | --- | --- |
| 4 | **`ShoulderLengthAtStart`** | 水平線の長さ（先端） | `5` | **符号がこのぶん上へ動く** |
| 5 | `ShoulderLengthAtEnd` | 水平線の長さ（終端） | `5` | **動かない**（下記） |

`5 → 15`（＋10）で測った実測:

| 測った場所 | 縮尺 | 外接の上端の動き | 倍率 |
| --- | --- | --- | --- |
| デザインレイヤの通り芯 | 1/100 | **1000** | **×100** |
| デザインレイヤの通り芯 | 1/50 | **500** | **×50** |
| **注釈のグリッド線** | ビューポート 1/100 | **1000** | **×100** |
| **注釈のグリッド線** | ビューポート 1/50 | **500** | **×50** |

- issue #189 では OIP で「5 → 15 で符号が用紙で約 10mm 上へ移る」のを見ていた。
  パラメータ `World-based` は `False` だった。
- `ShoulderLengthAtEnd` は `5 → 15` でも `5 → 12000` でも外接が変わらなかった。
  `ShowBubbleAt` は **`Start Point`** で、終端側には何も描かれていなかった。
- 値は書いた直後に読み戻せた（`15.000`）が、外接は `ResetObject` まで動かなかった
  （書いた直後の上端 Δ = 0、Reset 後 Δ = 1000）。
- ツールのプラグインスタイル（`styleRef=51`・「グリッド線」）が当たっていた。
  `GetPluginStyleParameterType` は `ShoulderLengthAtStart` / `ShoulderLengthAtEnd` /
  `ShowBubbleAt` / `AddElbowToSholder` に **`2`（`kPluginStyleParameter_AllwaysByInstance`）**、
  `BubbleScaleFactor` と `World-based` に `0`（`_ByInstance`）を返した。スタイルが当たったままの
  個体へ書いて絵が動いた。

#### 符号の位置の基準は「**映っているモデルの上端**」——高さ範囲では動かない

**1 つずつしか違わない枚を並べて実測した**（注釈座標での外接 y の上端）:

| 枚 | 映っているモデルの高さ | 高さ範囲の上端 | 縮尺 | グリッド線の外接 y の上端 |
| --- | --- | --- | --- | --- |
| **A** | 3000 | 9000 | 1/100 | **4235** |
| **B** | 3000 | **4000** | 1/100 | **4235**（A と一致） |
| **D** | **6000** | 9000 | 1/100 | **7235**（A ＋ 3000） |
| **C** | 3000 | 9000 | **1/50** | **3622.5** |

- 高さ範囲の上端（`endHeight`）を 9000 → 4000 に変えても 1 桁も動かなかった（A と B）。
- 映っているモデルを 3000 → 6000 にすると、ちょうど 3000 動いた（A と D）。
- 検算（モデルの上端 ＋ 固定分）:

  | 縮尺 | 固定分 | 検算 |
  | --- | --- | --- |
  | 1/100 | **1235** | 3000 + 1235 = **4235**（A・B） / 6000 + 1235 = **7235**（D） |
  | 1/50 | **622.5** | 3000 + 622.5 = **3622.5**（C） |

  固定分を縮尺で割ると用紙で 12.35mm / 12.45mm（このとき `ShoulderLengthAtStart` ＝ 5 用紙 mm）。
- 断面が空のときの上端は固定分だけだった（1/100 で `1235`・1/50 で `622.5`）。
- [plugin#181](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz/pull/181) は
  高さ範囲の上端を 3600mm 上げたが、寸法と符号は重なったままだった。

#### 断面を空でなくするのに足を取られた記録（条件そのものは #202 で確定した）

この節を測るために断面を空でなくする必要があり、そこで 3 巡ぶん足を取られた。#189 で実際に
出た数値:

- `pt3` と同じ側にモデルを置いた枚は、断面が空のままだった（キャッシュ群 3/5/6/7/15 が
  すべて 0 件）。反対側に置いた枚は映り、キャッシュ群 5 に 13 件・7 に 2 件が入った。
  > **【訂正の記録】** このとき `1064` と `1065` は 6 枚すべてで「両方 `true`」だった（実行ログで
  > 書き込みも読み戻しも両方 `true`）。当時「反対側に置いた枚は `1064` **だけ**で映り、`1065` は
  > 手前側を呼び戻してくれない」と書いたが、#189 はどちらのフラグが効いたかを分けていなかった。
  > [#202](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/202) で
  > 分けて測ると、`1065` だけを立てた枚では `pt3` と同じ側のモデルも映った（上記「断面に中身が
  > 入る条件」）。上の 2 つの数値そのものは変わらない。
- グリッド線は断面が空でも注釈に出た（そのときの上端は上記の固定分だけ）。`pt3` を反対側に
  すると 3 本の x の順序が逆になった。
- `ovViewportDisplay2DComponents`（1059）は、`SetObjectVariable` が `true` を返すのに読み戻しは
  `false` だった（レンダは先に隠線消去にしてある）。
- 壁は `CreateWall` のままでは高さ 0 で、`SetWallOverallHeights` で高さを与えて映した
  （[Walls](../Findings/Walls.md)「`CreateWall` が建てた壁は、新規の空図面では高さ 0 になる」）。

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

## 断面ビューポートの注釈座標と用紙座標の対応（#200）

- 注釈座標の横: 断面線 `x 0→3000` でモデル `x 500〜2500` の枚と、断面線 `x 5000→8000` でモデル
  `x 5500〜7500` の枚が、どちらも注釈座標で `−2500〜−500`（＝ `モデル x − 終点 x`）だった
  （[Dimensions](../Findings/Dimensions.md)「断面ビューポートの注釈空間の座標」と同じ値）。

### GL は映っているモデルの高さに依らない——実測

高さだけが違う直方体を映す 2 枚を、**同じ高さ範囲（−1000〜9000）・同じ縮尺（1/100）**で作り、
断面に描かれた中身（キャッシュ群）の外接を読んだ:

| 枚 | モデルの Z | 中身の外接の y（注釈座標） | 用紙座標の y | 位置（1025） |
| --- | --- | --- | --- | --- |
| **低** | 0〜**3000** | **0〜3000** | **0〜30** | 0 |
| **高** | 0〜**6000** | **0〜6000** | **0〜60** | 0 |

- 2 枚の下端は `0.0000` で一致。注釈座標の y はモデルの Z と一致（上端の差も `0.0000`）。
  測ったのはレイヤ Z = 0 の素のデザインレイヤ。

係数とオフセットは、注釈へ**4 辺とも空枠の外へ出る大きさ**の矩形（注釈座標
`−300000〜100000 × −400000〜200000`）を置き、用紙座標の外接と突き合わせて解いた:

| | 値 |
| --- | --- |
| k（注釈 → 用紙の倍率） | **(0.0100, 0.0100)** ＝ 1/100 ちょうど |
| off（**左下**の角から解いた） | **(0.0000, 0.0000)** |
| off（**右上**の角から解いた） | **(0.0000, 0.0000)**（左下とのずれ `0.0000`） |

- この矩形はビューポートを (137, −59) 動かした後に測っている。off は 0 のままだった。
- 右上だけ外へ出した版では左下が空枠の縁（±26.649）のままになり、幅から解いた k が
  `1.0366` になった。

### 注釈座標は「ビューポートが用紙のどこに在るか」を含む

ビューポートを `MoveObject` で動かしたときの実測（1/100・(137, −59) 動かした）:

| 読んだもの | 動かす前 | 動かした後 | 差 |
| --- | --- | --- | --- |
| 注釈の矩形（注釈座標） | 左 `−300000` 下 `−400000` | 左 **`−286300`** 下 **`−405900`** | **+13700 / −5900** |
| ビューポート（用紙座標） | 左 `−3000` 下 `−4000` | 左 **`−2863`** 下 **`−4059`** | +137 / −59 |
| 1024 / 1025 | (0, 0) | **(137, −59)** | — |

- `CreateSectionViewport` 直後の 1024 / 1025 は `(0, 0)` だった。

### `GetObjectBounds(viewport)` が何で決まるか

| 状態 | 外接（用紙座標） |
| --- | --- |
| 断面が空・注釈も空 | **±26.649（53.3mm 角）の「×」印の空枠** |
| 断面に中身がある | **映っている中身そのもの**（低 `−25/0/−5/30`・高 `−25/0/−5/60`） |
| 注釈を足した直後（`ResetObject` 前） | **1mm も動かない** |
| 注釈を足して `ResetObject` した後 | **注釈も入る**（空枠より大きければ注釈の矩形そのもの） |

- 「注釈を足した直後は 1mm も動かない」は、`ResetObject` を呼ばない限り VW へ戻った後も
  同じだった（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。足した後に何も呼ばない枚は、undo イベントを閉じても、戻って
  数十秒置いても ±26.6 の空枠のまま。`ResetObject` を通した枚だけ注釈が入る）。
- 16 枚すべて同じ高さ範囲 `−1000〜9000` で作ったのに、外接の高さは低 30mm・高 60mm と
  映っているモデルのぶんだけ違った（差 `30.0000`）。
- 中身が入った枚の外接は `−25/0/−5/30` で、±26.649 の縁は 1 つも残っていなかった。
- 空枠より小さいものの記録は [Dimensions](../Findings/Dimensions.md) の訂正の記録。

### 併せて測れた値

| 読んだもの | 結果 |
| --- | --- |
| `ovViewportXPosition` / `YPosition`（**1024 / 1025**） | **用紙 mm での位置。** ヘッダは read only だが **`MoveObject` で動く**（(0,0) → (137,−59)）。`VWViewportObj::GetPosition()` がこの 2 つを読む |
| `ovViewportTransformMatrix`（1049） | 平行移動 = **位置 × 縮尺**（`(13700, −5900, 0)`）。回転部は単位行列 |
| `ovViewportViewMatrix`（1050）/ `ovSheetLayerSectionViewportViewMatrix`（1055） | **互いに一致。モデル → 断面ビューの行列**で、平行移動は `(pt1.x, 0, −pt1.y)`・回転部は `[−1 0 0 / 0 0 1 / 0 1 0]`。**用紙とは無関係で、`MoveObject` でも動かない** |
| `ovViewportOperatingTransform`（1051） | **読めない**（`GetObjectVariable` が `false` を返す） |
| `ovSectionViewportSectionViewMatrix`（1056） | **単位行列** |
| `ovViewportUnscaledBoundsWithoutAnnotations`（**1052**） | **名前のとおり「注釈を除いた外接」を縮尺前（モデル mm）で返す**＝**用紙の外接から注釈を除いたもの × 縮尺**。中身が無い枚では「空（反転した矩形）」（下記「1052 と 1026 は何を返すか」。#202） |
| `ovViewportAngleWithXAxis`（**1026**） | **用紙の上での回転角（度）。** 回していない枚は `0`、`RotateObjectN` で 30 度回すと `30.000`（#202） |
| crop（`ViewportHasCropObject` / `GetViewportCropObject`） | **無い**（`CreateSectionViewport` 製。`false` / `nil`） |
| 注釈群（`GetViewportGroup(vp, kViewportGroupAnnotation)`） | **最初の注釈を足すまで `nil`**。足した後も、外接が読めるのは `ResetObject` の後 |

- 空の容れ物の外接は左 = `+DBL_MAX`・右 = `−DBL_MAX` の反転した矩形で返り、
  `GetObjectBounds` は `true` を返した。1052 も注釈群もこの形だった。

### `ovViewportUnscaledBoundsWithoutAnnotations`（1052）と `ovViewportAngleWithXAxis`（1026）は何を返すか（#202）

#200 は 1052 を「断面が空の枚」でしか読んでいなかった。中身のある枚で読み直した実測
（1/100・位置 1024/1025 ＝ (0, 0)・中身の外接（注釈座標）は `−2500 / 0 / −500 / 8000`）:

| 段 | したこと | `GetObjectBounds(vp)`（用紙） | **1052** | 1026 |
| --- | --- | --- | --- | --- |
| **a** | 更新直後（注釈は空） | `−25 / 0 / −5 / 80` | **`−2500 / 0 / −500 / 8000`** | `0` |
| **b** | 注釈へ**遠くの**矩形（注釈座標 100000〜101000 × 200000〜201000）を足して `ResetObject` | `−25 / 0 / 1010 / 2010`（**注釈のぶん膨れる**） | **`−2500 / 0 / −500 / 8000`（1 桁も動かない）** | `0` |
| **c** | `RotateObjectN`（中心 ＝ 1024/1025・**30 度**） | `−138.975 / −12.5 / −4.330 / 2245.711` | `−6165.064 / −1250 / −433.013 / 6678.203` | **`30.000`** |
| **c'** | さらに `ResetObject` | c と同じ | c と同じ | `30.000` |

- a: 1052 は中身の外接（注釈座標）と完全に一致し、用紙の外接のちょうど縮尺倍
  （幅の比 `2000 ÷ 20 = 100.000`）。
- b: 注釈を足して用紙の外接が `1035 × 2010` まで膨れても 1052 は動かなかった。
- c の検算: 用紙の 4 隅 `(−25,0) (−5,0) (−5,80) (−25,80)` を原点まわりに 30 度回すと
  x `−61.650〜−4.330`・y `−12.500〜66.782`。**× 100 が 1052 と一致**。
- 1026 は read only だが、`RotateObjectN` で回すと追随した（`0` → `30.000`）。`1024` / `1025` は
  回転の中心に取ったので動かなかった。
- 中身が空の枚では 1052 も反転した矩形だった（#200 の 5 段階）。

### 断面に中身が入る条件——1 つずつ変えた 8 通り

判定はキャッシュ群 3 / 5 / 6 / 7 / 15 に件数が入ったかで見た（群 4（断面群）は空でも 2 件）。

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

- 描けた 3 通りの中身の外接は完全に同一（`−2500/0/−500/3000`）。
- `depth` は 0 と 30000 で差が無く（0 と 1）、高さ範囲の下限も −1000 と 0 で差が無かった
  （0 と 4）。
- 空だった枚も表示レイヤ 3/3・クラス 46/46 の設定が成功し、`1064` の読み戻しも `true`、1050 も
  断面の向きに入っていた（#151 の表の行 C と同じ状態）。
- 【訂正の記録】当時この 8 通りから「当面は両方 `true` にするのが安全」と書いた。後に #202 で
  16 通りを総当たりした（上記「断面に中身が入る条件——`1065` が主スイッチで…」）。

## 注釈の中の図形は、検索条件（criteria）では見つからない

UI で置いたレベル基準線が 2 つの断面ビューポートの注釈に計 8 本ある図面で、
`ISDK::ForEachObjectInCriteria` の `(PON='Elevation Benchmark2')` の件数は **0**。次の自前の走査では
8 件見つかった:

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

- この経路で拾った個体は、`ISDK::IsViewportGroupContainedObject(h, kViewportGroupAnnotation)` が
  すべて `true` だった。
- 注釈群そのものは `ISDK::GetViewportGroup(vp, kViewportGroupAnnotation)` で取った。

## 打ち切った調査: 断面ビューポートの範囲を「無限」にする

UI で手作りすると〈無限〉が既定なので、公開ヘッダに載っていないオブジェクト変数
（欠番 1060〜1090）に範囲が載っているのではと疑い、読み取り専用の一時診断で実機で
確かめた（手で 1 枚だけ〈無限〉に変えてから再生成し、値の違う変数を探す）。
**結果は断面ビューポート 66 枚・値の組み合わせ 1 通り＝差のある変数は 1 つも無し。**

## `CreateViewport(parentHandle)` の `parentHandle` は「**どの容れ物に置くか**」

実測（VW 2026 / mac。[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)
で 3 回踏んだ）:

| 渡したもの | `ParentObject(vp)` | `AddViewportAnnotationObject` | `GetViewportGroup(vp, kViewportGroupAnnotation)` |
| --- | --- | --- | --- |
| **デザインレイヤ** | そのデザインレイヤ | **`false`**（矩形でも PIO でも） | **`nil`**（呼んだ後も） |
| **シートレイヤ** | そのシートレイヤ | `true` | 取れる（`IsViewportGroupContainedObject` も `true`） |

- デザインレイヤを渡したときも、`CreateViewport`・`SetObjectVariable(ovViewportScale)`・
  `UpdateViewport` は成功した。
- ヘッダの `GS_CreateViewport` の説明: "The specified parent handle may only be a layer or a group
  contained within a layer, nested or otherwise"（[Undo](../Findings/Undo.md) に既出）。
- `CreateLayer(..., kLayerSheet)` の後、アクティブレイヤが新しいシートレイヤに切り替わった。
- 縮尺を 1/50 → 1/100 に変えると、注釈の中身の世界座標が比例して書き換わり、紙の見え方は
  保たれた（図面ラベルで実測。寸法でも同じ。[Dimensions](../Findings/Dimensions.md)）。
