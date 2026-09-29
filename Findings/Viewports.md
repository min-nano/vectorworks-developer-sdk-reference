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
