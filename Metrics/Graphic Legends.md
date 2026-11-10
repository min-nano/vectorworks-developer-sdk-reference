# グラフィック凡例 — 計測記録

結論は [Findings「Graphic Legends」](../Findings/Graphic%20Legends.md)。ここには全欄ダンプの
読み・タグ付きデータの生バイト・節点型の数・打ち切った調査で潰した道を置く。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

`GraphicLegend` PIO を SDK から生成・制御するときの実測（VW 2026。後半の確認は
[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）。
タグ付きデータの読み出しの実測と生バイトは [Tagged Data](Tagged%20Data.md) にある。

## `ResetObject` を呼ばずに置いた凡例の実測（#245）

- #245: `ResetObject` を呼ばずに置いた凡例は、VW へ戻った後もタイトルだけでイメージ 0 のまま
  だった。作り直した後で足したシンボルは、戻った後も凡例に入らなかった。

## ソース定義（何を並べるか）を設定する口を探した

- パラメトリックレコードを読むと、ソース定義はボタンのフィールド（type=14）で、選んだ
  ソースを保持する欄は無かった。ISDK にもソースへ触る呼び出しは無かった。
- スタイル無しで置いた凡例は中身が 0 セットで、何も出なかった（実機。VW へ戻った後に
  読み直してもイメージ 0 のまま——#245）。それまでは「既定のソースが集めたものがフィルタで
  絞られる」と読んでいたが、#245 のこの実測で覆った。
- VW 2026 SDK のヘッダを `Legend` で全文検索した。凡例について公開されていたのは
  `ovGraphicLegend*`（1645〜1650。ヘッダの説明では**PIO のリセット中しか有効でない**画像
  再生成用のオブジェクト変数）と、条件ダイアログの文脈 `eGraphicLegendCriteria` だけで、
  ソース定義やフィルタに触る呼び出しは `ISDK` にも VWFC にも無かった（`Filter…Viewport` /
  `LegendSource` / `LegendCriteria` / `Get|SetGraphicLegend…` のどれも 0 件）。
  VWFC の `VWObject.cpp` が使う `IGraphicLegendCoreSupport` /
  `IGraphicLegendCoreOperationsSupport` は**ヘッダが SDK に同梱されていなかった**。

## パラメトリックレコードに載っているもの・いないもの

- 実機の全欄ダンプで、スタイルの違う凡例ごとに違う値が入っていた。値を持つ欄:
  `SummarizeBy`（`StyleSymName` / `ClassName`）・`SortBy`（`StyleSymName` /
  `DesignLayerName`）・`SortOrder`・`HideEmptyCells`・`FixedCellWidth` / `FixedCellHeight`・
  `LayoutBy`・`TitleWidth`・`ScaleFactor`（記号の倍率）・`BoxWidth`。
  値を持たない type=14＝ボタンは `DefineSource` / `DefineImage` / `EditCellLayout` /
  `EditTitleLayout` / `ImageClasses` の 5 つ。レコードの版は `__version` ＝ 3100。
- UI で「ビューポートでフィルタ」を掛けた前後で、36 フィールド中**変わったのは `BoxWidth`
  だけ**だった（153.99 → 48.80。並ぶセルが減って凡例が細くなった）。
- フィルタを掛けた凡例で `associations=0`。
- 凡例の中身はセルごとにグループ（`type=11`）＋ `GraphicLegendFrame`（＋画像）で、
  **UUID を値に持つレコード欄は 1 つも無かった**。

## 「ビューポートでフィルタ」の保存先を探した（タグ付きデータ `'GrLg'`）

フィルタを掛ける前後でオブジェクト変数を総当たりで比べると、**703（`ovFirstAuxObject`）
だけが違った**。その先の補助オブジェクトの連鎖にぶら下がるデータオブジェクトに、
タグ付きデータがあった（生バイトは [Tagged Data](Tagged%20Data.md#補助オブジェクトの連鎖と-dmdt)）。

- 書いた形: `TaggedDataCreate(legend, 'GrLg', 15, 5, N)` ＋
  `TaggedDataSet(legend, 'GrLg', 15, 5, i, &GetObjectInternalIndex(viewport))` を
  i = 0..N-1 で回し、**`ResetObject` より前**に呼んだ。型 15 は
  `kTaggedDataObjectRefArrayTypeID`、タグは 5、値は**フィルタ先ビューポートの
  `InternalIndex`**。
- UI はチェックボックスの一覧でフィルタ先を複数選べた。3 つ選んだ凡例の `'GrLg'` は
  同じ容れ物・同じタグで、型 15・要素数 3・要素は 3 つのビューポートの `InternalIndex`
  だった（別のタグは増えていなかった）。

## ソース定義の保存先を探した（`'GrLe'`）

**ソース定義とイメージの定義の保存先は `'GrLe'`**（`'GrLg'` と 1 文字違い。Graphic Legend の
別の容れ物）。**手で「凡例ソースの定義...」を設定した凡例にだけ**ぶら下がっており
（データオブジェクト 142 バイト、タグ 0、中身 22 バイト）、素のままの凡例には無い。

検索条件の文字列（`INVIEWPORT` 等）はどこにも現れなかった。`(INVIEWPORT & (T=SYMBOL))` は
ASCII で 25 文字あって 22 バイトに入らない。単純な 1 条件の 16 進（16 ビットずつ読んだ値を併記）:

```
9a 02  96 02  1e 03  96 02  09 03  0f 00  47 06  65 06  4f 06  a5 02  90 06
 666    662    798    662    777     15   1607   1637   1615    677   1680
```

- 検索条件だけが違う凡例を突き合わせると、変わったのは 6 番目の 16 ビット値だけだった:

  | 6 番目 | 検索条件 |
  | --- | --- |
  | 15 | `(INVIEWPORT & (T=SYMBOL))`（シンボル） |
  | 86 | `(INVIEWPORT & (T=PLUGINOBJECT))`（プラグインオブジェクト） |

  15 と 86 はオブジェクトの型番号と同じ値（凡例自身の型も `type=86`）。
- **条件のトークン列の文法**（複数条件・グループ・文字列つきの凡例を 2 度ダンプして
  詰めた）。16 ビットずつ読む。**枠**は `666 662 798 …（条件）… 677 1680` で、`798` が
  `INVIEWPORT`。セレクタ（何で絞るか）と値の入れ方:

  | 値 | 意味 | 値の入れ方 |
  | --- | --- | --- |
  | 798 | `INVIEWPORT` | 無し |
  | 777 | `T`（タイプ） | **生の 16 ビット値**（11=グループ / 15=シンボル / 86=プラグインオブジェクト） |
  | 773 | `L`（レイヤ） | 文字列（マーカー 1627） |
  | 774 | `C`（クラス） | 文字列（マーカー 675） |
  | 808 | `PON`（プラグインオブジェクト名） | 文字列（マーカー 1627） |
  | 822 | 基準面 | 文字列（マーカー 1627） |

  **文字列は `<マーカー> 0 0 <UTF-16LE> 0` の形で出た。** マーカーは `773`/`808`/`822` で
  1627、`774` で 675 だった。同じ `774 675` が「＝」でも「≠」でも出た。比較の向きは条件の
  後ろに出た（後置）: **1607 ＝「右項目のもの」（＝）／1606 ＝「右項目ではないもの」（≠）**。
  実例（ダイアログの 8 行とそのまま対応した）:

  ```
  666 662 798                                   ← 枠 ＋ INVIEWPORT
    777 11                             1607     ← タイプが ＝ グループ
    1637 773 1627 0 0 "共通" 0          1607     ← レイヤが ＝ 共通
    1637 1608 822 1627 0 0 "スクリーンプレーン" 0  1606  ← 基準面が ≠ スクリーンプレーン
    1637 1608 777 86                   1607     ← タイプが ＝ プラグインオブジェクト
    1637 774 675 0 0 "一般" 0            1607     ← クラスが ＝ 一般
    1637 1615 773 1627 0 0 "共通" 0      1607     ← レイヤが ＝ 共通
  1637 1615 1637 1608 1637 1615 677 1680        ← 閉じ
  ```

  **1637 / 1608 / 1615** は条件の区切りとグループの開閉の位置に出た。どれがどれかはまだ
  詰めていない（グループの種類だけを変えた 2 枚の差分は取っていない）。
- 単純な 1 条件の 22 バイトのうち、6 番目以外の 10 個の値の意味は解けていない。
- [#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245):
  VW 2026 の既定テンプレートから開いた図面で、上の 22 バイト（型 15＝シンボル）を
  `TaggedDataCreate(legend, 'GrLe', 1, 0, 22)` ＋ `TaggedDataSet(..., i, &byte)` を 22 回、
  フィルタと同じく **`ResetObject` より前**に書いて `ResetObject` した凡例に、**凡例イメージ
  （型 122）が 2 つ**入った。図面に置いたのはシンボル 2 種・3 個（`lt-s1` × 2・`lt-s2` × 1。
  1/50）で、イメージの寸法は 6 × 4mm と 10 × 10mm——300 × 200 と 500 × 500 のシンボルを 1/50
  にした大きさそのもの＝**定義ごとに 1 行**（既定の `SummarizeBy`＝`StyleSymName`）。
  ソースを書かない凡例はイメージ 0 だった。VW へ戻った後に読み直しても同じだった。

## 凡例イメージの中身（内部 ID と節点型）

SDK のヘッダを `GraphicLegend|LegendImage|ImageScale` で引くと、凡例は 4 つの内部 ID に
分かれている（`Kernel/API/MiniCadHookIntf.h`）:

| 定数 | 値 |
| --- | --- |
| `kInternalID_GraphicLegend` | 658 |
| `kInternalID_GraphicLegendImage` | 659 |
| `kInternalID_GraphicLegendFrame` | 660 |
| `kInternalID_GraphicLegendTitleFrame` | 661 |

ただし**これは `GetObjectTypeN` が返す値ではない**（あちらは `Kernel/API/Objs.TDType.h` の
節点型）。実機の凡例の中を `FirstMemberObj` / `NextObject` で辿って節点型を数えると:

```
凡例の中で見えた節点型: 0×102, 3×66, 10×6, 11×36, 86×42, 122×12
```

| 型 | 名前 | 中身 |
| --- | --- | --- |
| 0 | `kTermNode` | リストの終端 |
| 3 | `kBoxNode` | セルの枠 |
| 10 | `kTextNode` | 見出し・ラベル |
| 11 | `kGroupNode` | セルのまとまり |
| 86 | `kParametricNode` | 並べている PIO |
| 122 | **`kViewportNode`** | **凡例イメージ**（凡例 1 つに 1 つ） |

- `ovGraphicLegendViewType` / `BgRenderMode` / `FgRenderMode`（`ObjectVariables.h`）は
  ビューポートの属性と同じ顔ぶれ。
- 型 122 の縮尺は、通常のビューポートと同じ `VWViewportObj::SetScale` ＋ `Update` で書けた
  （下の打ち切った調査）。
- ヘッダだけから推測していた `kWorksheetImageNode`（＝56）は**実機の凡例に 1 つも
  入っていなかった**。
- 作り直していない凡例にはイメージ（型 122）が無かった。VW へ戻った後に読み直しても
  現れなかった（#245）。

## 打ち切った調査: 凡例のイメージの縮率を外から合わせる

- 凡例イメージ（型 122）の縮尺を SDK から書くと、イメージの中身の縮尺は変わったが、
  **セルの枠は既定（1:50）のまま残った**。手で OIP の「イメージの縮率」を変えると、
  枠も中身も変わった。
- 「中身を変えてから作り直せば枠も揃う」と読んで試したところ、中身 1/75・枠 1:50 のまま
  だった。

決め手になった実測（同じ文書に、手で設定した凡例と SDK が置いた凡例を並べて全欄を比較）:

| | レコードの `ImageScale` | 絵 |
| --- | --- | --- |
| 手で OIP のポップアップを 1:5 にした凡例 | **5 に変わる** | **1:5 に変わる** |
| SDK が `SetParamReal` / `SetParamAsString` で 75 を書いた凡例 | **75 になる** | **変わらない** |

OIP のポップアップで選んでも「カスタム 1:」で入れても、同じ `ImageScale` 欄に値が入った。

潰した道:

- **レコードを実数で書く**（`SetParamReal`）… レコードは変わる。絵は変わらない。
- **レコードを文字列で書く**（`SetParamAsString`。OIP の欄は文字列で入るので、同じ変換・
  通知の経路を通ることを期待した）… 結果は実数と同じ。
- **書く位置を総当たり**（作り直しの前／後／最後、undo イベントの内／外）… どれも同じ。
- **ポップアップ用の別の欄を探す** … レコードの全欄を実機で並べたが、種別 8（ポップアップ）は
  `TitleWidth` / `LayoutBy` / `SummarizeBy` / `SortBy` / `SortOrder` の 5 つだけ。縮率の
  ポップアップに当たる欄は無い。
- **文書内の全凡例を比べる** … SDK が書いた内容は、手で設定した凡例のレコードと
  一致していた。
- **パラメータ変更を PIO へ伝える** … `kParameterChangedReset` は PIO 側が受け取る
  メッセージで、外から送る API は見つからなかった（[Parametric Objects](../Findings/Parametric%20Objects.md)）。
- タグ付きデータにもオブジェクト変数にも縮率は無い。
