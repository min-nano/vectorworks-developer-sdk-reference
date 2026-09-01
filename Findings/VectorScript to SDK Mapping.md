# VectorScript → SDK の対応（無いものが多い）

**VectorScript（`vs.*`）の名前をそのまま ISDK に探しても無いことが多い。**
VectorScript の資料（`vs.py`、スクリプトのエクスポート）から SDK の設計を推測すると、
名前が違う・そもそも存在しない・作法ごと違う、のどれかで外れる。**着手前に必ず
`sdk-grep`（CI の調査ワークフロー。[`CLAUDE.md`](../CLAUDE.md)）で実在を確かめる**こと。

実機での切り分けとヘッダ全文検索で確定した対応・非対応の一覧:

| VectorScript | ISDK / VWFC | 備考 |
| --- | --- | --- |
| `Move3D` / `Rotate3D`（3D 変換状態） | **無い** | 配置は行列で与える（`VWTransformMatrix` ＋ `CreateCustomObjectByMatrix`、または `SetEntityMatrix`）。[Roof Faces](Roof%20Faces.md) |
| `BeginRoof` / `GetZVals` ほか屋根作成の一連 | **無い** | 屋根面はポリゴンから作ってオブジェクト変数で属性を与える。[Roof Faces](Roof%20Faces.md) |
| `GetLayerElevation`（レイヤ高さの取得） | **無い** | 高さは自分で持ち回る。[Layers and Stories](Layers%20and%20Stories.md) |
| `Message`（ステータスバー表示） | **無い** | 進捗は `VWFC::Tools::CProgressDlg` で出す。[Progress and Diagnostics](Progress%20and%20Diagnostics.md) |
| `GetVersionEx`（VW 本体のバージョン） | **無い** | SDK ヘッダに相当 API が見当たらない（`sdk-grep` で全ヘッダ走査済み）。[Progress and Diagnostics](Progress%20and%20Diagnostics.md) |
| `AddVertex3D`（NURBS 曲線へ頂点追加） | `ISDK::Add3DVertex` | `Insert3DVertex` は**別物**で頂点が増えない。`VWNURBSCurve` は評価専用（制御点から構築できない）。[Parametric Objects](Parametric%20Objects.md) |
| `DoubLines` → `Wall` の 2 手順 | `CreateWall`（壁厚を引数に取る） | 1 呼び出しで済む。[Walls](Walls.md) |
| `HMoveForward` / `HMoveBackward`（レイヤ重ね順） | `InsertObjectAfter` / `InsertObjectBefore` | デザインレイヤは図面のオブジェクト列に並んでいる。[Layers and Stories](Layers%20and%20Stories.md) |
| `PlaceSymbol` | `VWSymbolObj(name, VWPoint2D, angleDeg)` | レガシー `PlaceSymbol` のラッパー。**2 つの作法**を外すと静かに壊れる。[Symbols](Symbols.md) |
| ビューポートの重ね順上書き | **書けない**（読み出しのみ） | VectorScript にも無い（`vs.py` に `Stacking` のヒット無し）。[Layers and Stories](Layers%20and%20Stories.md) |

**逆に、VectorScript 運用より SDK が楽になった例**: 断面ビューポートは
`ISDK::CreateSectionViewport` で**新規作成できる**（VectorScript 時代に既製ビューポートを
手で用意して流用する運用は要らない）。[Viewports](Viewports.md)
