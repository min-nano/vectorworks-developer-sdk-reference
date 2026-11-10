# タグ付きデータ（TaggedData） — 計測記録

結論は [Findings「Tagged Data」](../Findings/Tagged%20Data.md)。ここには読み出しが壊れた
実測と、補助オブジェクトの生バイトのダンプを置く。どちらもグラフィック凡例の
「ビューポートでフィルタ」の保存先を探した調査（[Graphic Legends](../Findings/Graphic%20Legends.md)）で
得たもの。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

## `TaggedDataGetNumElements` / `TaggedDataGet` の読み出しの実測

- 型 ID を総なめして読ませた（1 度）。実際に入っている型と違う型 ID を渡しても
  `TaggedDataGetNumElements` は同じ要素数を返し、`TaggedDataGet` はその型の大きさで
  切り出した値を返した（同じタグが 13 種類すべての型で「見つかった」。文字列に見える
  値も出た）。
- 正しい容れ物 ID・型・タグで読ませたら、件数は生バイトと一致し、値はポインタらしき数
  （`-523223174` など）が返った。
- 書き込み（`TaggedDataCreate` ＋ `TaggedDataSet`）は実機で効いた。
- 生バイトは `MCObjectHandle`（`GSHandle`＝`char**`）を参照外しして読み、大きさは
  `GSGetHandleSize` で取った（下の表）。

## 補助オブジェクトの連鎖と `'DMDT'`

フィルタを掛けたグラフィック凡例の**補助オブジェクトの連鎖**（オブジェクト変数 703 ＝
`ovFirstAuxObject`。ヘッダの説明は "read/write : used to manipulate the Aux list - Public for VS"）を
たどり、先頭の補助オブジェクトの生バイトを読んだ:

| オフセット | 中身 |
| --- | --- |
| +80 | `54 44 4d 44` ＝ データオブジェクトのタグ `'DMDT'`（リトルエンディアン） |
| +86 | `67 4c 72 47` ＝ **本当のタグ付きデータの容れ物 ID `'GrLg'`** |
| +96 | `05 00` ＝ タグ 5 |
| +108 | `0f 00` ＝ 型 15（`kTaggedDataObjectRefArrayTypeID`） |
| +110 | `01 00` ＝ 要素数 1 |
| +114 | `c0 01 00 00` ＝ 448 ＝ 参照先オブジェクトの `InternalIndex` |

同じ構造の補助オブジェクトが複数ぶら下がっていた。+86 は `'ptd_'` `'rwtt'`（SDK ヘッダの
`kTaggedDataContainerNNA_Internal*` そのもの）や `'GrLe'` `'modl'` `'FSTL'`（文字スタイル）など。
