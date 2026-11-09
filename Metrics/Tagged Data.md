# タグ付きデータ（TaggedData） — 計測記録

結論は [Findings「Tagged Data」](../Findings/Tagged%20Data.md)。ここには読み出しが壊れた
実測と、補助オブジェクトの生バイトのダンプを置く。どちらもグラフィック凡例の
「ビューポートでフィルタ」の保存先を探した調査（[Graphic Legends](../Findings/Graphic%20Legends.md)）で
得たもの。

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

同じ構造の補助オブジェクトが複数ぶら下がり、+86 は `'ptd_'` `'rwtt'`（SDK ヘッダの
`kTaggedDataContainerNNA_Internal*` そのもの）や `'GrLe'` `'modl'` `'FSTL'`（文字スタイル）など。
