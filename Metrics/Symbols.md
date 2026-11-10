# シンボル — 計測記録

結論は [Findings「Symbols」](../Findings/Symbols.md)。ここには実際に起きた事故の記録・
定義を `ResetObject` の前後で覗いたダンプ・用紙基準の外接の実測・シンボル定義の
`subType` を数えた表を置く。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

## 配置の作法（事故の記録）

`VWSymbolObj(name, VWPoint2D, angleDeg)`（レガシーの `PlaceSymbol` の包み）で、生成後に
`AddObjectToContainer` で入れ直さず、非 nil のハンドルを成功と数えていた。
この状態で、定義・レイヤ・名前・座標がすべて正しいのに 4 種 472 件が図面に現れず、
完了報告は全数成功だった、という事故が実際に起きた。

## 定義を SDK から組み立てる — 中身を入れたら `ResetObject` を呼ぶ

`gSDK->CreateSymbolDefinition(name)`（`VWSymbolDefObj` の名前の構築子の中身）で定義を作り、
`gSDK->AddObjectToContainer(shape, definition)` で図形を入れた。入れた後に
`gSDK->ResetObject(定義のハンドル)` を呼ばなかったときは次のようになった。

- 実機でシンボルを 2D 編集し「すべて選択」しても**何も選べなかった**。
- 配置したインスタンスの外接が `-2147483648` だった。
- 図には何も出なかった。

実測（同じ定義を `ResetObject` の前後で覗いたもの。`defType` は `GetSymbolDefinitionType`、
中身は `FirstMemberObj` / `NextObject` で辿った型番号）:

```
入れた後       type=16 defType=0 外接=無効     中身[0] type=5 外接=300x150 頂点=3 閉=yes
ResetObject 後 type=16 defType=1 外接=300x150  中身[0] type=5 外接=300x150 頂点=3 閉=yes
```

前後で中身は同じで、違ったのは定義の外接と種別だけだった。この違いに気付くまで
3 周かかった。

### 付随して分かったこと

- **`GetFirstMemberObject()` は空の定義でも非 nil を返した。** 空の定義は type 0 の
  レコードを 1 つ持っていた（これを検証に使って「入った」と報告し続けた）。
  `FirstMemberObj` / `NextObject` で辿って型番号を見た（4=楕円 / 5=多角形 / 11=グループ /
  16=シンボル定義。`Objs.TDType.h`）。
- **`ResetObject` の後に定義の種別が変わった。** VWFC で作った 2D 多角形を入れた定義は、
  `ResetObject` の後に `GetSymbolDefinitionType` が `k2DSym`(0) から **`k3DSym`(1) へ
  変わった**（テンプレート由来の 2D シンボルは 3 つとも 0 だった）。
- 定義の中身をスクリーン平面の 2D 図形にする口は
  `gSDK->SetPlanarRefID(shape, kPlanarRefID_ScreenPlane)`（`kPlanarRefID_ScreenPlane` は 0。
  `TypesBase.h`）。実装ではこれを入れたうえで平面図に正しく出ることまで確かめた。
  **入れた後の種別は読み直していない。**

実装例: ホームズ君プラグインの `draw/ShearWall` の `EnsureMarkSymbols` / `PrepareDefinition`。

## 用紙基準（縮尺無視）の大きさは「定義の図形 × そのレイヤの縮尺」

`VWSymbolDefObj::SetPageBased(true)`（オブジェクト変数 `ovSymDefPageBased` = 130）を
呼ぶと、`GetPageBased()` が true を返した。

実測: 図形 300mm の定義を用紙基準にして置いたインスタンスの外接が **30000mm** になった
（＝100 倍。そのレイヤの縮尺は直接読んでいないが、比がちょうど縮尺と一致する）。
[Findings](../Findings/Symbols.md#用紙基準縮尺無視の大きさは定義の図形--そのレイヤの縮尺) の
3 点を揃えたうえで、狙いどおりの紙寸法で出ることを実機で見た。

## シンボル定義のサムネイル・一覧を UI に出す

シンボル選択 UI 用に、**図面のシンボル定義一覧**と**選んだ定義の絵**を採った
（実機・VW 2026 / macOS）。

- `renderMode = 0`（ワイヤーフレーム）・`view = 2`（Top/Plan）を渡すと、2D 部品
  だけのシンボルでも絵が出た。この 2 値は `Kernel/API/MiniCadCallBacks.h` の
  `SymbolImgInfo` の既定構築子が使っている値（`sdk-grep` で読んだ）。

  ```cpp
  // Kernel/API/MiniCadCallBacks.h（引数順は width, height, margin, view, renderMode, ...）
  SymbolImgInfo() : SymbolImgInfo(-1, -1, -1, 2/*TopPlan*/, 0/*Wireframe*/,
      EImageViewComponent::StandardView2D, ELevelsOfDetail::Medium,
      false /*sizing is done by layer scale*/, false) {};
  ```

- `TStandardView` の名前つき定数 `standardViewTop` は 7（既定構築子が使う `2` とは
  別の値）。`standardViewTop`（7）を渡すと、実機で 2D 部品が映らなかった。

### 一覧には「ユーザが置く部品」以外も混ざる——`GetSymbolDefSubType` で分ける

`BuildList(kSymDefNode, true)` で図面のシンボル定義を全部取り、1 件ずつ
`gSDK->GetSymbolDefSubType(hSymDef)` を読んだ（`VWFC::VWObjects::VWSymbolDefObj::HasPluginStyleSupport()`
が見ているのもこの値——ソースを読んだ）。実機（VW 2026 / macOS）で 16 件を数えた実測:

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

- スタイルの多くは `…スタイル` フォルダに入っていたが、`木質構造材_*`（537）と
  `グリッド線`（647）は**フォルダ無し**だった。
- `VWSymbolDefObj::GetType()`（2D / 3D / ハイブリッドの別）は、スタイルにもハイブリッド
  （537）や 3D（645）があった。
