# シンボル — 計測記録

結論は [Findings「Symbols」](../Findings/Symbols.md)。ここには実際に起きた事故の記録・
定義を `ResetObject` の前後で覗いたダンプ・用紙基準の外接の実測・シンボル定義の
`subType` を数えた表を置く。

## 配置の作法（事故の記録）

`VWSymbolObj(name, VWPoint2D, angleDeg)`（レガシーの `PlaceSymbol` の包み）で、生成後に
`AddObjectToContainer` で入れ直さず、非 nil のハンドルを成功と数えていた。
この 2 つが無かったために、定義・レイヤ・名前・座標がすべて正しいのに 4 種 472 件が図面に
現れず、しかも**完了報告は全数成功で嘘をついていた**、という事故が実際に起きた。

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
確認**している（3 点は [Findings](../Findings/Symbols.md#用紙基準縮尺無視の大きさは定義の図形--そのレイヤの縮尺)）。

## シンボル定義のサムネイル・一覧を UI に出す

シンボル選択 UI 用に、**図面のシンボル定義一覧**と**選んだ定義の絵**を採る実測
（実機・VW 2026 / macOS）。

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

### 一覧には「ユーザが置く部品」以外も混ざる——`GetSymbolDefSubType` で分ける

**`BuildList(kSymDefNode, true)` は図面のシンボル定義を全部返す。** そこには、ユーザが
図面へ置く部品のほかに、**VectorWorks 自身がプラグインオブジェクトのスタイルとして
持っているシンボル定義**が混ざる。選択 UI にそのまま出すと、`図面ラベル - 図番` や
`断面寸法` のような「部品ではないもの」が並んでしまう。

**切り分けは `gSDK->GetSymbolDefSubType(hSymDef)`。** 0 なら普通のシンボル定義、
**0 以外なら「そのプラグインオブジェクトのスタイル」**で、値はプラグインオブジェクトの
型を表す（`VWFC::VWObjects::VWSymbolDefObj::HasPluginStyleSupport()` が見ているのも
これ）。実機（VW 2026 / macOS）で 16 件を数えた実測:

| subType | 中身 | 例 | フォルダ |
| --- | --- | --- | --- |
| `0` | **普通のシンボル定義**（ユーザが置く部品） | `アンカーボルト_M12` / `鋼製火打` / `仕口` / `床束` / `束伏図記号` / `柱伏図記号` | （無し） |
| `537` | 木質構造材（構造部材）のスタイル | `木質構造材_横架材` / `木質構造材_柱・束` | **（無し）** |
| `552` | 図面枠（タイトルブロック）のスタイル | `遠山信夫アトリエ一級建築士事務所` | `図面枠スタイル` |
| `599` | データタグのスタイル | `断面寸法` | `データタグスタイル` |
| `642` | 図面ラベルのスタイル | `図面ラベル - 図番` | `図面ラベルスタイル` |
| `645` | 立断面指示線のスタイル | `立断面指示線` | `立断面指示線スタイル` |
| `647` | グリッド線のスタイル | `グリッド線` | （無し） |
| `658` | グラフィック凡例のスタイル | `基礎伏図凡例` / `床伏図凡例` | `グラフィック凡例スタイル` |

- **フォルダ名で切り分けようとしない。** スタイルの多くは `…スタイル` フォルダに入るが、
  `木質構造材_*`（537）と `グリッド線`（647）は**フォルダ無し**だった。判定に使えるのは
  `subType` だけ。
- **2D / 3D / ハイブリッドの別（`VWSymbolDefObj::GetType()`）は無関係。** スタイルにも
  ハイブリッド（537）や 3D（645）があり、ユーザ部品と混ざる。
- 逆に、**特定のプラグインオブジェクトのスタイルだけを選ばせたい**なら
  `subType == <その型>` で絞ればよい（例: 構造部材のスタイルだけなら 537）。
