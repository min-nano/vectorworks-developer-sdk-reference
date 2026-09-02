# シンボル

ISDK にシンボルを配置する呼び出しは無く（在るのは `CreateSymbolDefinition` だけ）、
`VWSymbolObj(name, VWPoint2D, angleDeg)` がレガシーの `PlaceSymbol` を包んでいる。
**どちらも外すと静かに壊れる**作法が 2 つある。

1. **生成しただけでは図面に現れない。** VWFC の他のラッパーと違い、できたインスタンスは
   アクティブレイヤに入らない。生成後に `gSDK->AddObjectToContainer(handle, layer)` で
   配置先レイヤへ入れ直す。
2. **非 nil のハンドルを成功判定に使わない。** `PlaceSymbol` は「定義が nil なら何もしない」
   仕様で、**何も置けていなくても非 nil のハンドルを返し得る**。
   `VWSymbolObj::IsSymbolObject(handle, name)` まで確かめてから成功と数える。

この 2 つが無かったために、定義・レイヤ・名前・座標がすべて正しいのに 4 種 472 件が図面に
現れず、しかも**完了報告は全数成功で嘘をついていた**、という事故が実際に起きた。

- **事前ガードを置かない。** `VWSymbolDefObj::IsSymbolDefObject` で先に弾く形にすると、
  その判定が期待どおりでないとき**1 つも置けないうえ原因を誤って指す**。配置そのものを
  唯一の門にし、失敗したときだけ理由を引き分ける。
- **シンボルはストーリバウンドを持てない。** レイヤ平面から外れた高さへ置くには
  `MoveObject3D(handle, 0, 0, zOffset)` で相対移動するしかない。
  **配置行列（`SetEntityMatrix`）には書かない**——行列は「与えるときは Z が絶対・読み戻す
  ときは Z がレイヤ相対」という混在があり、相対値を書くと取り違える。

## 定義を SDK から組み立てる — 中身を入れたら `ResetObject` を呼ぶ

`gSDK->CreateSymbolDefinition(name)`（`VWSymbolDefObj` の名前の構築子の中身）で定義を作り、
`gSDK->AddObjectToContainer(shape, definition)` で図形を入れる——**ここまでは素直に効く**。
抜けやすいのは**入れた後の `gSDK->ResetObject(定義のハンドル)`** で、これが無いと
**定義の外接が計算されない**。外接の無い定義は絵として成立せず、次のようになる。

- 実機でシンボルを 2D 編集し「すべて選択」しても**何も選べない**。
- 配置したインスタンスの外接が `-2147483648`（＝大きさが無い）。
- 図には何も出ない。

実測（同じ定義を `ResetObject` の前後で覗いたもの。`defType` は `GetSymbolDefinitionType`、
中身は `FirstMemberObj` / `NextObject` で辿った型番号）:

```
入れた後       type=16 defType=0 外接=無効     中身[0] type=5 外接=300x150 頂点=3 閉=yes
ResetObject 後 type=16 defType=1 外接=300x150  中身[0] type=5 外接=300x150 頂点=3 閉=yes
```

**中身は前後で同じで、変わるのは定義の外接と種別だけ。** つまり外接は中身とは別に持たれて
いて、**「中身がある」ことは「絵になる」ことを意味しない**。この違いに気付くまで 3 周かけた。

### 付随して分かったこと

- **`GetFirstMemberObject()` は空の定義でも非 nil を返す。** 空の定義は type 0 のレコードを
  1 つ持つので、「図形が入ったか」の検証にこれを使ってはいけない（これに騙されて
  「入った」と報告し続けた）。数えるなら `FirstMemberObj` / `NextObject` で辿って**型番号を
  見る**（4=楕円 / 5=多角形 / 11=グループ / 16=シンボル定義。`Objs.TDType.h`）。
- **既にある定義を使い回すときも `ResetObject` を通す。** 上の不具合で作られた「中身は
  あるのに外接が無い」定義が図面に残っていると、「中身があるから使える」と判定して
  そのまま使い、**いつまでも図に出ない**。`ResetObject` は絵を書き換えないので、
  「利用者が編集した定義は触らない」という方針とも両立する。
- **`GS_CreateSymbolDefinition` は「名前が既に使われていれば nil を返す」**
  （`APIBase.Legacy.Defs.h` の本文）。一度壊れた定義を作ってしまった図面では次から
  作り直せないので、直すなら `DeleteSymbolDefinition(hSymDef, bCompletely, useUndo)` で
  名前を空けてから作る。
- **中身を入れる口は `AddObjectToContainer` ただ 1 つでよい。** マニュアルは
  「`AddObjectToContainer` / `InsertObjectBefore` / `InsertObjectAfter`、または返ってきた
  ハンドルに対する `EnterContainer`」と書くが、**`EnterContainer` は ISDK に無い**
  （レガシーの説明文に名前が出るだけ）。VectorScript の `BeginSym` / `EndSym` に当たる口も
  無く、`SetActiveSymbolDef` は**作図先を切り替えない**（VWFC のラッパーはアクティブ
  **レイヤ**へ作られる）。グループに包んでから入れても結果は変わらない。
- **`ResetObject` は定義の種別も付け直す。** VWFC で作った 2D 多角形を入れた定義は、
  `ResetObject` の後に `GetSymbolDefinitionType` が `k2DSym`(0) から **`k3DSym`(1) へ
  変わった**（テンプレート由来の 2D シンボルは 3 つとも 0 だった）。定義の中身を
  スクリーン平面の 2D 図形にする口は
  `gSDK->SetPlanarRefID(shape, kPlanarRefID_ScreenPlane)`（`kPlanarRefID_ScreenPlane` は 0。
  `TypesBase.h`）。**【推定】これで種別が `k2DSym` に落ち着く**——実装ではこれを入れた
  うえで平面図に正しく出ることまで確かめたが、**入れた後の種別を読み直してはいない**ので、
  「種別が 3D だったこと」と「スクリーン平面にすれば 2D になること」は水準が違う。

実装例: ホームズ君プラグインの `draw/ShearWall` の `EnsureMarkSymbols` / `PrepareDefinition`。

## 用紙基準（縮尺無視）の大きさは「定義の図形 × そのレイヤの縮尺」

`VWSymbolDefObj::SetPageBased(true)`（オブジェクト変数 `ovSymDefPageBased` = 130）は効く
（`GetPageBased()` が true を返す）。ただし**用紙基準の「紙」は、そのインスタンスが載って
いるレイヤの縮尺で決まる**。

実測: 図形 300mm の定義を用紙基準にして置いたインスタンスの外接が **30000mm** になった
（＝100 倍。そのレイヤの縮尺は直接読んでいないが、比がちょうど縮尺と一致する）。
この規則そのものは、下記の 3 点を揃えたうえで**狙いどおりの紙寸法で出ることを実機で
確認**している。

したがって「紙の上で一定の大きさ」を得るには**3 つで 1 組**になる。どれが欠けても記号が
桁違いに大きく／小さく出る。

1. 定義を `SetPageBased(true)` にする。
2. **定義の図形を用紙 mm で作る**（紙の上で欲しい寸法そのまま）。
3. **そのレイヤの縮尺を、最終的に図が印刷される縮尺へ揃える**（`gSDK->SetLayerScaleN`）。

3 が要るのは、デザインレイヤの図形を**シートレイヤのビューポート越しに見る**からで、
レイヤ縮尺とビューポート縮尺が食い違えばそのぶんずれる。プラグインが `CreateLayer` で
作ったレイヤは**図面の既定の縮尺**のままなので、意識して揃えないと合わない。

**インスタンスの側からビューポートの縮尺は分からない**（同じ図形が別々の縮尺の複数の
ビューポートに映り得る。`ovViewportScale` はビューポート自身の変数）。レイヤ縮尺が
変わったときに PIO を描き直させたいなら、オブジェクトプロパティ
`kObjXPropHasLayerScaleDeps`（= 2。"Object wants to be reset when its layer scale changes"）を
立てる。

実装例: ホームズ君プラグインの `draw/ShearWall` の `applyShearWallLayerScale`
（伏図の縮尺が用紙を読むまで決まらないので、レイヤを作るときではなくシートを組む側から
呼んでいる）。

## インスタンスの反転は「負の倍率」で表す

`VWSymbolObj::SetScaleFactorX/Y`（中身はオブジェクト変数 `ovSymbolXScaleFactor` /
`ovSymbolYScaleFactor`）に **−1** を与えると、その軸で反転したインスタンスになる。
X と Y を別々に持たせるには、先に `SetScaleType(kScaleTypeAsymmetric)`
（`ovSymbolScaleType`。`ESymScaleType` は `kScaleTypeNone`=1 / `kScaleTypeSymmetric`=2 /
`kScaleTypeAsymmetric`=3。`MiniCadCallBacks.h`）を立てる。

**回転では作れない姿（鏡像）が要るときに、定義を増やさずに済む。** 例えば「斜辺の向き ×
記号を寄せる側」で 4 通りある直角三角形の記号は、1 つの定義と倍率 (±1, ±1) の組み合わせで
すべて作れる（回転だけで済ませようとすると鏡像が作れず定義が 2 つ要る）。

実装例: ホームズ君プラグインの `Extensions/ExtShearWall` の `AddMarkSymbol`。

## シンボル定義のサムネイル・一覧を UI に出す

シンボル選択 UI 用に、**図面のシンボル定義一覧**と**選んだ定義の絵**を採る実測
（実機・VW 2026 / macOS）。

- **図面のシンボル定義の一覧は `VWResourceList::BuildList(kSymDefNode, sort)` で
  採れる**（`VWFC::Tools::VWResourceList::BuildList(short objectType, bool sort =
  true)`。`kSymDefNode` は `Kernel/API/Objs.TDType.h` で **16**）。1 件ずつの名前は
  `GetResourceName(i, name)` で引ける。
- **シンボルの絵を出すのは `VWSymbolDisplayCtrl`。** レイアウトダイアログでは
  `CreateControl(dlg, width, height, margin)` → `Update(name, renderMode, view)`
  の順で問題なく使える（自前のレイアウトダイアログでの生成も成功している）。
- **`renderMode = 0`（ワイヤーフレーム）・`view = 2`（Top/Plan）を渡すと、2D 部品
  だけのシンボルでも絵が出る。** この 2 値は当てずっぽうではなく、
  `Kernel/API/MiniCadCallBacks.h` の `SymbolImgInfo` の既定構築子が使っている値
  そのもの（`sdk-grep` で確認済み・【ヘッダ根拠】）。

  ```cpp
  // Kernel/API/MiniCadCallBacks.h（引数順は width, height, margin, view, renderMode, ...）
  SymbolImgInfo() : SymbolImgInfo(-1, -1, -1, 2/*TopPlan*/, 0/*Wireframe*/,
      EImageViewComponent::StandardView2D, ELevelsOfDetail::Medium,
      false /*sizing is done by layer scale*/, false) {};
  ```

  **`TStandardView` の名前つき定数 `standardViewTop` は 7 で、既定構築子が使う
  `2`（Top/Plan）とは別物。** `standardViewTop`（7）を渡すと、実機で 2D 部品が
  映らないことを確認している——「Top（真上からの 3D 直交視点）」と「Top/Plan
  （画面平面＝2D 部品を特別扱いする視点）」は同じ「上から見る」でも別の値なので、
  名前で類推して 7 を選ぶと取り違える。
