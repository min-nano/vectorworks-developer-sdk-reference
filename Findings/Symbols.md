# シンボル

シンボルの配置・定義の組み立て・用紙基準・反転・選択 UI の知見。実測のダンプと表は
[計測記録](../Metrics/Symbols.md)。

ISDK にシンボルを配置する呼び出しは無く（在るのは `CreateSymbolDefinition` だけ）、
`VWSymbolObj(name, VWPoint2D, angleDeg)` がレガシーの `PlaceSymbol` を包んでいる。
**どちらも外すと静かに壊れる**作法が 2 つある。

1. **生成しただけでは図面に現れない。** VWFC の他のラッパーと違い、できたインスタンスは
   アクティブレイヤに入らない。生成後に `gSDK->AddObjectToContainer(handle, layer)` で
   配置先レイヤへ入れ直す。
2. **非 nil のハンドルを成功判定に使わない。** `PlaceSymbol` は「定義が nil なら何もしない」
   仕様で、**何も置けていなくても非 nil のハンドルを返し得る**。
   `VWSymbolObj::IsSymbolObject(handle, name)` まで確かめてから成功と数える。

これが無かったために 4 種 472 件が現れず、完了報告は全数成功だった事故が実際に起きた
（[記録](../Metrics/Symbols.md#配置で起きた事故の記録)）。

- **事前ガードを置かない。** `VWSymbolDefObj::IsSymbolDefObject` で先に弾く形にすると、
  その判定が期待どおりでないとき**1 つも置けないうえ原因を誤って指す**。配置そのものを
  唯一の門にし、失敗したときだけ理由を引き分ける。
- **シンボルはストーリバウンドを持てない。** レイヤ平面から外れた高さへ置くには
  `MoveObject3D(handle, 0, 0, zOffset)` で相対移動するしかない。
  **配置行列（`SetEntityMatrix`）には書かない**——行列は「与えるときは Z が絶対・読み戻す
  ときは Z がレイヤ相対」という混在があり、相対値を書くと取り違える。

## 定義を SDK から組み立てる — 中身を入れたら `ResetObject` を呼ぶ

- `CreateSymbolDefinition(name)` ＋ `AddObjectToContainer(shape, definition)` は素直に効く。
  **抜けやすいのは入れた後の `gSDK->ResetObject(定義)`** で、無いと**定義の外接が計算され
  ない**——編集で何も選べず、インスタンスの外接が `-2147483648`、図に何も出ない。
- **中身は前後で同じで、変わるのは定義の外接と種別だけ。「中身がある」は「絵になる」を
  意味しない**（[前後のダンプ](../Metrics/Symbols.md#定義を-sdk-から組み立てたときの-resetobject-の前後の実測)）。

### 付随して分かったこと

- **`GetFirstMemberObject()` は空の定義でも非 nil を返す**（type 0 のレコードを 1 つ持つ）。
  数えるなら `FirstMemberObj` / `NextObject` で辿って**型番号を見る**。
- **既にある定義を使い回すときも `ResetObject` を通す。** 外接の無い壊れた定義が残って
  いると、いつまでも図に出ない。`ResetObject` は絵を書き換えないので、利用者が編集した定義を
  触らない方針とも両立する。
- **`GS_CreateSymbolDefinition` は「名前が既に使われていれば nil を返す」**
  （`APIBase.Legacy.Defs.h` の本文）。直すなら
  `DeleteSymbolDefinition(hSymDef, bCompletely, useUndo)` で名前を空けてから作る。
- **中身を入れる口は `AddObjectToContainer` ただ 1 つでよい。** マニュアルは
  「`AddObjectToContainer` / `InsertObjectBefore` / `InsertObjectAfter`、または返ってきた
  ハンドルに対する `EnterContainer`」と書くが、**`EnterContainer` は ISDK に無い**。
  `BeginSym` / `EndSym` に当たる口も無く、`SetActiveSymbolDef` は作図先を切り替えない。
  グループに包んでから入れても結果は変わらない。
- **`ResetObject` は定義の種別も付け直す**——2D 多角形を入れた定義が `k2DSym`(0) から
  **`k3DSym`(1)** へ変わった。中身を `SetPlanarRefID(shape, kPlanarRefID_ScreenPlane)` で
  スクリーン平面にすれば **`k2DSym` に落ち着く【推定】**（平面図に正しく出ることは確認、
  種別の読み直しはしていない。[実測](../Metrics/Symbols.md#付随して分かったこと)）。

実装例: ホームズ君プラグインの `draw/ShearWall` の `EnsureMarkSymbols` / `PrepareDefinition`。

## 用紙基準（縮尺無視）の大きさは「定義の図形 × そのレイヤの縮尺」

`VWSymbolDefObj::SetPageBased(true)`（`ovSymDefPageBased` = 130）は効くが、**用紙基準の
「紙」はインスタンスが載っているレイヤの縮尺で決まる**（300mm の定義が 30000mm になった。
[実測](../Metrics/Symbols.md#用紙基準縮尺無視の定義を置いた外接の実測)）。
「紙の上で一定の大きさ」を得るには**3 つで 1 組**（実機で確認）:

1. 定義を `SetPageBased(true)` にする。
2. **定義の図形を用紙 mm で作る**（紙の上で欲しい寸法そのまま）。
3. **そのレイヤの縮尺を、最終的に図が印刷される縮尺へ揃える**（`gSDK->SetLayerScaleN`）。

- 3 が要るのはシートレイヤのビューポート越しに見るから。`CreateLayer` で作ったレイヤは
  図面の既定の縮尺のままなので、意識して揃える。
- **インスタンスの側からビューポートの縮尺は分からない**（`ovViewportScale` はビュー
  ポート自身の変数）。レイヤ縮尺の変化で PIO を描き直させたいなら
  `kObjXPropHasLayerScaleDeps`（= 2。"Object wants to be reset when its layer scale changes"）を立てる。

実装例: ホームズ君プラグインの `draw/ShearWall` の `applyShearWallLayerScale`
（伏図の縮尺が用紙を読むまで決まらないので、シートを組む側から呼んでいる）。

## インスタンスの反転は「負の倍率」で表す

- `VWSymbolObj::SetScaleFactorX/Y`（`ovSymbolXScaleFactor` / `ovSymbolYScaleFactor`）に
  **−1** を与えると、その軸で反転する。X と Y を別々に持たせるには先に
  `SetScaleType(kScaleTypeAsymmetric)`（`ovSymbolScaleType`。`ESymScaleType` は
  `kScaleTypeNone`=1 / `kScaleTypeSymmetric`=2 / `kScaleTypeAsymmetric`=3。`MiniCadCallBacks.h`）。
- **鏡像が要るときに定義を増やさずに済む**（4 通りの直角三角形の記号を、1 つの定義と
  倍率 (±1, ±1) で作れる）。

実装例: ホームズ君プラグインの `Extensions/ExtShearWall` の `AddMarkSymbol`。

## シンボル定義のサムネイル・一覧を UI に出す

実機・VW 2026 / macOS。

- **図面のシンボル定義の一覧は `VWResourceList::BuildList(kSymDefNode, sort)` で
  採れる**（`VWFC::Tools::VWResourceList::BuildList(short objectType, bool sort =
  true)`。`kSymDefNode` は `Kernel/API/Objs.TDType.h` で **16**）。1 件ずつの名前は
  `GetResourceName(i, name)` で引ける。
- **シンボルの絵を出すのは `VWSymbolDisplayCtrl`。** レイアウトダイアログでは
  `CreateControl(dlg, width, height, margin)` → `Update(name, renderMode, view)`
  の順で問題なく使える。
- **`renderMode = 0`（ワイヤーフレーム）・`view = 2`（Top/Plan）を渡すと、2D 部品だけの
  シンボルでも絵が出る**（`SymbolImgInfo` の既定構築子の値【ヘッダ根拠】）。
  **`standardViewTop`（7）は別物で、渡すと 2D 部品が映らない**（実機で確認。
  [根拠](../Metrics/Symbols.md#シンボル定義のサムネイル一覧を採った実測)）。
  「Top（真上からの 3D 直交視点）」と「Top/Plan（画面平面＝2D 部品を特別扱いする視点）」は
  同じ「上から見る」でも別の値なので、名前で類推して 7 を選ぶと取り違える。

### 一覧には「ユーザが置く部品」以外も混ざる——`GetSymbolDefSubType` で分ける

- **`BuildList(kSymDefNode, true)` は、VW が PIO のスタイルとして持つシンボル定義も返す**
  （`図面ラベル - 図番` や `断面寸法` など）。
- **切り分けは `gSDK->GetSymbolDefSubType(hSymDef)`。** 0 なら普通のシンボル定義、0 以外は
  その値の型の PIO のスタイル（[16 件の内訳](../Metrics/Symbols.md#一覧の定義ごとの-getsymboldefsubtype-の実測)）。
- **フォルダ名でも 2D / 3D の別でも切り分けられない**——判定に使えるのは `subType` だけ。
  特定の PIO のスタイルだけなら `subType == <その型>` で絞る。

```cpp
// 「ユーザが図面へ置く部品」だけを選択 UI に出す
for (size_t i = 0, cnt = fList.GetNumItems(); i < cnt; ++i)
{
    const MCObjectHandle hSymDef = fList.GetResource(i);
    if (hSymDef == nil || gSDK->GetSymbolDefSubType(hSymDef) != 0)
        continue;   // プラグインオブジェクトのスタイル → 出さない
    fThumb.AddImageFromResource(fList.GetListID(), i);
}
```

**この絞り込みを掛けると、ポップアップの添字と一覧の添字はずれる。** 選択は
`GetSelectedItem()`（`InternalIndex`）で受けるか、追加した順の対応表を自分で持つこと
（[Layout Dialogs](Layout%20Dialogs.md) の「サムネイル付きのリソース選択」）。
