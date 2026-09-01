# 屋根面

- **屋根面オブジェクトは `VWRoofFaceObj` をハンドル版コンストラクタで作り直す。**
  `VWRoofFaceObj(type, poly, z, upSlopeDir, rise, run)` は**使えない**（SDK のソース上 `z` を
  読まず、屋根軸を原点を通る線として置く）。外形ポリゴンから作ってから、屋根軸・棟側の点・
  勾配・軸の Z を**オブジェクト変数**（`ovSlabRoofPt1/Pt2/UpslopePt/Rise/Run`・`ovSlabHeight`）
  で与える。`ovSlabRoofUpslopePt` は**方向ではなく棟側の点**。
- シンボルと同じく**生成しただけでは図面に入らない**（`AddObjectToContainer` が要る。
  [Symbols](Symbols.md)）。
- VS の 3D 変換状態（`Move3D` / `Rotate3D`）と `BeginRoof` 系が SDK に無いので、**配置行列**
  （`VWTransformMatrix` ＋ `CreateCustomObjectByMatrix`）で置く。副産物として、VectorScript
  版の作法が抱えていた落とし穴（テンプレートのポリゴンを屋根と誤認する／確定後の後付け
  操作で VW がクラッシュする）が構造的に起きない。
