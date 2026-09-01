# タグ付きデータ（TaggedData）

`ISDK::TaggedData*` と補助オブジェクトの連鎖に関する一般知識。UI にしか無い設定の保存先が
ここに在ることがある（実例: [Graphic Legends](Graphic%20Legends.md) のフィルタとソース定義）。

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

- **「型で総なめ」して探すのは無駄**（1 度やって騙された）。`TaggedDataGetNumElements` は
  **渡した型 ID を検証しない**らしく、実際に入っている型と違っていても同じ要素数を返し、
  `TaggedDataGet` はその型の大きさで切り出したゴミを返す（同じタグが 13 種類すべての型で
  「見つかる」。文字列に見える値も出るが偽物）。
- **型とタグが分かっていても `TaggedDataGet` の読み値は当てにならない**（正しい容れ物 ID・
  型・タグで読ませたら、件数は生バイトと一致したのに値はポインタらしき数——`-523223174`
  など——が返った）。**件数は正しく、値は壊れる**。書き込み（`TaggedDataCreate` ＋
  `TaggedDataSet`）は実機で効いているので、読みだけの問題。
- したがって**探索は 16 進ダンプでやる**。`MCObjectHandle` は `GSHandle`＝`char**` なので、
  参照外しでそのまま中身が読める（大きさは `GSGetHandleSize`）。

## 補助オブジェクトの連鎖と `'DMDT'`

UI にしか無い設定は、オブジェクトの**補助オブジェクトの連鎖**（オブジェクト変数 703 ＝
`ovFirstAuxObject`。"read/write : used to manipulate the Aux list - Public for VS"）に
データオブジェクトとしてぶら下がっていることがある。実機の生バイト（フィルタを掛けた
グラフィック凡例の先頭の補助オブジェクト）:

| オフセット | 中身 |
| --- | --- |
| +80 | `54 44 4d 44` ＝ データオブジェクトのタグ `'DMDT'`（リトルエンディアン） |
| +86 | `67 4c 72 47` ＝ **本当のタグ付きデータの容れ物 ID `'GrLg'`** |
| +96 | `05 00` ＝ タグ 5 |
| +108 | `0f 00` ＝ 型 15（`kTaggedDataObjectRefArrayTypeID`） |
| +110 | `01 00` ＝ 要素数 1 |
| +114 | `c0 01 00 00` ＝ 448 ＝ 参照先オブジェクトの `InternalIndex` |

**`'DMDT'` は「入れ物の入れ物」**で、`ISDK::TaggedData*` に渡すべき容れ物 ID は +86 の方。
ここを取り違えると総なめしても 1 件も返らない。同じ構造の補助オブジェクトが複数
ぶら下がり、+86 は `'ptd_'` `'rwtt'`（SDK ヘッダの `kTaggedDataContainerNNA_Internal*`
そのもの）や `'GrLe'` `'modl'` `'FSTL'`（文字スタイル）など。

## 書き込みの作法

- `TaggedDataCreate(handle, containerID, type, tag, N)` ＋ `TaggedDataSet(..., i, &value)` を
  i = 0..N-1 で回す。**要素数を決めるのは `TaggedDataCreate` の最後の引数**。
- PIO に対して書くときは **`ResetObject` より前**に呼ぶ（PIO は作り直しのときに読む）。
- 多文字リテラル（`'GrLg'`）は警告になるので 16 進（`0x47724C67`）で書く。
