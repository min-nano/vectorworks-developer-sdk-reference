# タグ付きデータ（TaggedData）

`ISDK::TaggedData*` と補助オブジェクトの連鎖に関する一般知識。UI にしか無い設定の保存先が
ここに在ることがある（実例: [Graphic Legends](Graphic%20Legends.md) のフィルタとソース定義）。
実測とダンプは [計測記録](../Metrics/Tagged%20Data.md)。

## 型は 6 種類しかない

`Kernel/API/MiniCadCallBacks.h` の `kTaggedData*TypeID` と `VWFC/Tools/TaggedData.h` の
`ETaggedDataType`:

| 型 ID | 中身 |
| --- | --- |
| 1 | byte 配列 |
| 6 | double 配列 |
| 7 | 変換行列 |
| 8 | uint32 配列 |
| 13 | ColorRef 配列 |
| 15 | オブジェクト参照配列（`kTaggedDataObjectRefArrayTypeID`） |

（ほかに 2・17・18・20・21・22・23 の ID が定義だけある。）
**16 ビット幅の値も文字列も byte 配列（型 1）に載る**——VWFC の `CTaggedDataContainer` が
`CreateTagUint16` / `CreateTagString` を byte 配列で実現している。したがって
「奇数バイト数・偶数バイト数の中身」は素直に型 1 と読んでよい。

## 読み出しは信用できない（書き込みは効く）

- **「型で総なめ」して探すのは無駄。** `TaggedDataGetNumElements` は渡した型 ID を検証しない
  らしく、違う型でも同じ要素数を返し、`TaggedDataGet` はゴミを返す。
- **型とタグが分かっていても `TaggedDataGet` の読み値は当てにならない——件数は正しく、値は
  壊れる**（[実測](../Metrics/Tagged%20Data.md#読み出しは信用できない書き込みは効く)）。
  書き込み（`TaggedDataCreate` ＋ `TaggedDataSet`）は実機で効く。
- **探索は 16 進ダンプでやる。** `MCObjectHandle` は `GSHandle`＝`char**` なので参照外しで
  中身が読める（大きさは `GSGetHandleSize`）。

## 補助オブジェクトの連鎖と `'DMDT'`

- UI にしか無い設定は、**補助オブジェクトの連鎖**（703 ＝ `ovFirstAuxObject`）にデータ
  オブジェクトとしてぶら下がっていることがある。
- **`'DMDT'` は「入れ物の入れ物」**で、`ISDK::TaggedData*` に渡すべき容れ物 ID はその後ろ
  （+86）にある方（凡例のフィルタなら `'GrLg'`）。ここを取り違えると総なめしても 1 件も
  返らない（[生バイト](../Metrics/Tagged%20Data.md#補助オブジェクトの連鎖と-dmdt)）。

## 書き込みの作法

- `TaggedDataCreate(handle, containerID, type, tag, N)` ＋ `TaggedDataSet(..., i, &value)` を
  i = 0..N-1 で回す。**要素数を決めるのは `TaggedDataCreate` の最後の引数**。
- PIO に対して書くときは **`ResetObject` より前**に呼ぶ（PIO は作り直しのときに読む）。
- 多文字リテラル（`'GrLg'`）は警告になるので 16 進（`0x47724C67`）で書く。
