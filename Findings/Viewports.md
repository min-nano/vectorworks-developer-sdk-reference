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

### SDK で作った断面ビューポートの注釈でレベル基準線に高さを出す（#147）

**`CreateSectionViewport` が作るビューポートは、断面の向き（`ovSheetLayerSectionViewportViewMatrix`
＝ 1055）は持つのに、ビューポート自身のビュー行列（`ovViewportViewMatrix` ＝ 1050）が
単位行列のまま残る。** だから注釈に縦方向の基準が無く、レベル基準線が `0` を描く。
UI が断面ツールで作ったビューポートでは **1050 と 1055 が完全に同じ値**で、ビューの向き
（`ovViewportViewType` ＝ 1007）も **`standardViewRight`（6）**——SDK 製はそこが
**`standardViewTop`（7）**、つまり〈上から見た〉ままである。

**手順——1055 を 1050 へ写してから、注釈の個体を作り直す。**

```cpp
// 1) 作る → 2) 表示レイヤ・1064・レンダ・UpdateViewport の下ごしらえを済ませる
// 3) 注釈へレベル基準線を置き、3 つ組を書く（Level Objects.md）
// 4) **最後に** 断面の向きをビュー行列へ写す
TVariableBlock sectionMatrix;
if (gSDK->GetObjectVariable(vp, 1055, sectionMatrix))   // ovSheetLayerSectionViewportViewMatrix
	gSDK->SetObjectVariable(vp, 1050, sectionMatrix);   // ovViewportViewMatrix
// 5) 置いてある個体を作り直すと、そこで初めて絶対Z が入る
gSDK->ResetObject(marker);
```

実測（実物件の図面。1階 / `耐力壁`、絶対Z `540`。3 回の実行）:

| 何をしたか | 描かれた数値 |
| --- | --- |
| SDK 製の断面ビューポートの注釈へ、3 つ組だけ書いて置く | `0` |
| **1055 を 1050 へ写してから置く** | **`540`** |
| 先に 3 本置いて（3 本とも `0`）、**後から写して 3 本を `ResetObject`** | **3 本とも `540`** |
| UI 製を `DuplicateObject` した複製の注釈へ置く | **`540`**（表示レイヤを書き換えて更新しても保たれる） |

**`UpdateViewport` は 1050 を単位行列へ戻す。** だから**写すのは下ごしらえより後**に置く。
戻ったあとの挙動も実測してある:

| 更新の後 | 描かれた数値 |
| --- | --- |
| 置いてある個体を**触らずに読む** | **`540`**（＝**更新だけでは壊れない**。書かれた `Elev` は残る） |
| 置いてある個体を `ResetObject` してから読む | `0`（**作り直すと消える**） |
| **1055 を 1050 へ写し直してから** `ResetObject` | **`540`**（＝回復できる） |

**人が後から触る図面へ書き出すなら、この手順は脆い。** 測ってあるのは「`ResetObject` を
明示的に呼んだら `0` になる」ところまでで、**属性やパラメータの変更で VW が個体を
作り直すかは測っていない**——作り直されるなら `0` に落ちる**【推定】**。そういう図面では
**UI 製を `DuplicateObject` した複製のほうが安全**（複製は更新しても保つ。上表）。

**そもそも「更新のたびに戻る」のが不可解である。** 1050 は更新のときに引き直される
派生値で、UI 製ではそれが断面の向きになるのに SDK 製では単位行列になる——つまり
**引き直しの素になるものが SDK 製には備わっていない**見込みが高い。
**作り方から見直す調査は
[issue #151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)**
（SDK 製がキャッシュ群を 1 つも持たない件も、そちらで答えを付ける）。

- **効くのは 1050 ただ 1 つ。** オブジェクト変数 1000〜1150 を UI 製と SDK 製で総当りに
  突き合わせると**差は 14 件**。そのうち 12 件を 1 つずつ書き分けて、**数値が出たのは
  1050 だけ**だった（他の 11 件はすべて `0` のまま）。残る 2 件は、1055 が書けず
  （`SetObjectVariable` が `false`）、1064 は 1050 と併せて書いても打ち消さなかった。
  **差を 14 件まとめて書き写すと `0` のまま**だが、これは途中に挟んだ `UpdateViewport` が
  1050 を戻していたためで、打ち消していた変数は無い（1050 へ他の 7 件を 1 つずつ足しても
  `540` のまま）。
- **ビューの向きそのものは変えられない。** `VWViewportObj::SetViewType(standardViewRight)`
  を呼んでも `GetViewType` は `7` のまま・1050 も単位行列のまま・注釈も `0`。
  `ovViewportViewType`（1007）は `SetObjectVariable` も `false` を返す。
  **だから「向きを直す」道は無く、1050 を写す（＝上の手順）しかない。**
- **1050 を写しても断面は壊れない**（`ovIsSectionViewport`・`ovSectionViewportSectionViewMatrix`・
  断面群（`kViewportGroupSection`）の中身の個数が、写していない対照と一致）。
- **作り方の引数では出せない**——`depth` を 0 以外に / 高さの範囲を建物に合わせる /
  断面線を短く引く / `pt3` を反対側にする、の 4 通りとも `0` のままだった。
- **断面線オブジェクトとの結び付きでもない**——総当りで**ハンドル型の差は 0 件**。
  UI 製の断面ビューポートが何かを指していて SDK 製が指していない、という欄は無かった。

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
