# レベル（標高）オブジェクト — 断面ビューポートの注釈へ置く

軸組図（断面ビューポート）へ **GL・1FL・2FL・軒高**のような高さの印を置くための知見。
[issue #130](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/130) の
実機確認（VW 2026 / mac）による。断面ビューポートそのものの作り方は
[Viewports](Viewports.md)、注釈空間の座標は [Data Tags](Data%20Tags.md) にある。

## 先に結論

**断面ビューポートの注釈に「名前＋高さ」を出すなら、レガシーの `Elevation Benchmark`
（レベル（横断面）（レガシー））を使う。** 現行の `Elevation Benchmark2`（レベル基準線）は
高さは出せるが、**名前を書く口が SDK から無い**（`Note` も前後記号もカスタム文字も絵に
出ない。下記）。

```cpp
gSDK->DefineCustomObject("Elevation Benchmark", kCustomObjectPrefNever);  // 何かを作る前に 1 度
MCObjectHandle h = gSDK->CreateCustomObject("Elevation Benchmark", WorldPt(x, y), 0.0);
gSDK->AddViewportAnnotationObject(hSectionVP, h);
VWParametricObj obj(h);
obj.SetPointObjectPos(VWPoint2D(x, y));              // 注釈空間の座標（横＝断面線からの距離、縦＝Z）
obj.SetParamValue("Title", "1FL");                   // 名前。そのまま絵に出る
obj.SetParamValue("Elevation Display",
				  "Y value relative to reference elevation");  // 高さは注釈の Y から読む
obj.SetParamValue("DatumY", "0");                    // 基準（この値を引いた数値が出る）
gSDK->ResetObject(h);
```

数値を出さずに名前だけにしたいなら `Elevation Display` を `Custom` にして `Elevation`
（文字欄）を空にする。数値も自分で決めたいならそこへ `"+2800"` のように書く。

**ストーリレベルとの関連付けは SDK からはできない**（`Datum` ＝ `StoryLevel` は書いても
戻される。下記）ので、高さは注釈の Y から読ませ、名前は自分で書く。

## どのオブジェクトか（VW 2026 には 3 つある）

**SDK ヘッダには内部 ID しか無い**（`MiniCadHookIntf.h`）ので、PIO を作るのに要る
universal 名は実機に尋ねるしかない（`ISDK::GetPluginType` が種別を返し、
`ISDK::GetLocalizedPluginName` がその言語での名前を返す）。VW 2026 日本語版での実測:

| universal 名 | ローカライズ名 | 内部 ID | 欄数 | 素性 |
| --- | --- | --- | --- | --- |
| `Elevation Benchmark2` | レベル基準線 | 663 | 33 | **現行のツール。** 絵はレイアウト（`__Edit Layout`）で作る |
| `Elevation Benchmark` | レベル（横断面）（レガシー） | 102 | 47 | 旧ツール。**タイトルと高さの 2 つの文字**を描く |
| `Stake Object` | レベル | 370 | 51 | 地形モデル（敷地情報）向け。ラベルは 17 択 |

- `ElevationBenchmark`（空白なし）・`Level Marker`・`Datum Marker` などの綴りは**存在しない**
  （`GetPluginType` が false）。
- `Reference Marker`（参照記号（レガシー））も PIO として実在するが、レベルとは別物。
- **どれもスタイル無しで作れる**（`CreateCustomObject` だけで足りる）。
  `FolderSpecifiers.h` に `kObjectStylesElevationBenchmarkFolder` があるので
  オブジェクトスタイルの置き場はあるが、作成にスタイルは要らない。

## 高さは何で決まるか

**どちらのレベルオブジェクトも、既定では注釈空間の Y（＝Z）を読まない。** 注釈の
Y=0 / 2800 / 5600 へ置いても描かれる高さは **3 本とも `0`** で、動かしても変わらない。
**読ませるには欄を 1 つ切り替える。**

| | レガシー `Elevation Benchmark` | レベル基準線 `Elevation Benchmark2` |
| --- | --- | --- |
| 注釈の Y を読ませる欄 | `Elevation Display` ＝ `Y value relative to reference elevation` | `Axis` ＝ `YAxis2DMode`（既定は `ZAxis3DMode`） |
| 実測（注釈 Y=0 / 2800 / 5600） | `0` / `2800` / `5600` | `0` / `2800` / `5600` |
| 動かしたら | **追う**（`MoveObject` で Y=7000 → `7000`） | **追う**（Y=5600 へ動かして `5600`） |
| 基準を引く欄 | `DatumY`（1000 を入れると 2800 → `1800`） | `RefElev`（1000 を入れると 2800 → `1800`） |

- **`Offset`（レベル基準線）は高さの数値に効かない**（500 を入れても `2800` のまま）。
  引出線のオフセット用で、基準をずらす欄ではない。
- レガシーの `Elevation Display` は 5 択で、内部では**真偽欄 `UseY` /
  `Use Control Point`**（どちらも `__NNA_DO_NOT_CHANGE`）として持たれている。
  **内部欄を先に倒すとポップアップへの書き込みが負ける**——`ResetObject` のたびに
  内部欄から作り直されるので、**ポップアップだけを書くこと**（素の個体へ書けば
  5 択とも読み戻しが一致する。実測）。
- `Distance from control point`（＝`Use Control Point`）は **-Y** を返し、
  **動かしても値が変わらない**（制御点が一緒に動くため）。高さの表示には使えない。

### `Elevation Display`（レガシー）の 5 択

| universal 値 | ローカライズ | 注釈 Y=2800 での実測 |
| --- | --- | --- |
| `Custom` | カスタム | `Elevation`（文字欄）の中身がそのまま出る |
| `Z value relative to ground plane` | 基準平面に対するZ値 | `0`（注釈には Z が無い） |
| `Z value relative to reference elevation` | 基準高さに対するZ値 | `0`（同上） |
| `Y value relative to reference elevation` | 基準高さに対するY値 | **`2800`** |
| `Distance from control point` | 制御点からの距離 | `-2800`（移動しても変わらない） |

### `Datum`（レベル基準線）の 6 択

`GroundPlane` / `DesignLayerZ` / `ControlPoint` / `UserReference` / `Custom` / `StoryLevel`
（基準平面 / デザインレイヤのZ高さ / 制御点 / ユーザー基準 / カスタム / ストーリレベル）。

- **`Axis` ＝ `YAxis2DMode` にすると、入るのは `UserReference` / `ControlPoint` /
  `Custom` の 3 つだけ**になる。Z を見る 3 つ（`GroundPlane` / `DesignLayerZ` /
  `StoryLevel`）は書いても **`UserReference` へ戻る**（読み戻しで確認）。
  つまり**「注釈の Y を読む」設定と「Z を基準にする」設定は両立しない**。
- `ControlPoint` は入るが、値は制御点からの距離（実測 `-3000`）。

## 名前（表示名）を書く口

- **レガシーの `Title`（タイトル）は、書いた文字がそのまま絵に出る**（実測）。
  位置は `TitleOrient` の 4 択（ただし `Above Marker` は入らない。下記）。
- **`Elevation Display` ＝ `Custom` にすると、`Elevation`（文字欄）がそのまま
  高さの位置に出る。** 空にすれば数値は出ない。実測:

  | 書いたもの | 描かれた文字 |
  | --- | --- |
  | `Custom` ＋ `Elevation`＝空 ＋ `Title`＝`1FL` | 〈1FL〉〈 〉（**数値なし**） |
  | `Custom` ＋ `Elevation`＝`GL` ＋ `Title`＝空 | 〈 〉〈GL〉 |
  | `Custom` ＋ `Elevation`＝`+2800` ＋ `Title`＝`2FL` | 〈2FL〉〈+2800〉 |

- **レベル基準線には名前を書く口が無い。** `Note`（備考）・`EPfx`（高さの前記号）・
  `ESfx`（高さの後記号）へ書いても**絵には出ない**（欄には入る）。
  **`Datum` ＝ `Custom` ＋ `CustElev` も同じで、`Elevation` 欄には `GL` が入るのに
  描かれる文字は `0` のまま**（`Axis` の両方で、`ResetObject` を 2 度掛けても変わらず）。
  絵の中の名前らしき文字はレイアウトのトークン `#STLT#-#STPS#`（ストーリレベル名）で、
  これはストーリ従属のデザインレイヤに置いたときだけ埋まる（下記）。

## 単位・丸め（レベル基準線）

- `PrimaryUnits` は 12 択で、**選ぶと単位記号が数値に付く**（実測: `0mm` / `0m` /
  `0cm` / `0'` / `0"` / `0yd` / `0mi` / `0µm` / `0km` / `0°`）。
- `ShowUnitMark` ＝ `False` で単位記号だけ消える（数値は残る）。
- `RoundingPrecision` は 10 択（`EPrec1` 〜 `EPrec0000000001`）。既定は `EPrec1`。
- レガシーには単位の欄が無く、数値は図面の単位のまま出る。`Prefix` は
  `None` / `Plus` / `Plus-Minus` の 3 択で、**負の値に `Plus-Minus` を当てると
  `±-2800` になる**（VW 側の作りで、こちらで直せない）。

## 書いても入らない値がある（必ず読み戻す）

`SetParamValue` は**入らなかったことを教えてくれない**ので、書いたら読み戻す
（[Investigation Techniques](Investigation%20Techniques.md) と同じ作法）。実測で戻された例:

| オブジェクト | 書いた値 | 読み戻し |
| --- | --- | --- |
| レガシー | `TitleOrient` ＝ `Above Marker` | `Outside Marker` |
| レガシー | `ElevationOrient` ＝ `Above Marker` | `Outside Marker` |
| レガシー | `Fill` ＝ `Half Filled` | `Filled` |
| レベル基準線（`Axis`＝Y） | `Datum` ＝ `GroundPlane` / `DesignLayerZ` / `StoryLevel` | `UserReference` |
| レベル基準線（`Axis`＝Z） | `Datum` ＝ `StoryLevel` | `GroundPlane`（**場所を問わず**。下記） |

## ストーリ／ストーリレベルとの関連付け

**`Datum` ＝ `StoryLevel`（ストーリレベル）は SDK からは書けない。** ストーリを 2 つ
（1 階＝0 / 2 階＝2800）作った文書で、**ストーリ従属のデザインレイヤ**に置いた個体でも、
**断面ビューポートの注釈**に置いた個体でも、書くと読み戻しは **`GroundPlane`** になった
（`Axis` は既定の `ZAxis3DMode` のまま。両方で同じ）。**置き場所の問題ではない**ので、
「注釈だから紐づかない」ではなく「この欄は SDK からは倒せない」と読む。

一方で**ストーリとの連動そのものは、置き場所で決まる**:

| 置いた場所 | 描かれた高さ（`Datum` を書かない素のまま） | 絵に出るストーリレベル名 |
| --- | --- | --- |
| ストーリ従属のデザインレイヤ（2 階・高さ 2800） | `2800` | **`FL-2 階`**（自動で出る） |
| 断面ビューポートの注釈（Y=2800） | `0` | `-`（紐づかない） |

- **デザインレイヤに置けば、`Datum` を何も書かなくてもストーリレベル名が絵に出る**
  （レイアウトのトークン `#STLT#-#STPS#` が解決される）。**注釈に置いた個体では最後まで
  `-` のまま**で、どの `Datum` でも埋まらなかった。`__StoryName` / `__LevelTypeName` は
  どちらの場所でも空のままで、書いても絵は変わらない。
- **断面ビューポートの注釈では、Z を見る測定はすべて `0` になる**（`GroundPlane` /
  `DesignLayerZ` / 素のまま、いずれも 0）。注釈空間に Z が無いためで、**注釈で高さを
  出す道は「注釈の Y を読ませる」ことだけ**（上記の表）。
- デザインレイヤに置いた個体では `DesignLayerZ` だけ `0` になった（レイヤの Z を基準に
  するので、レイヤ原点からの高さ）。`GroundPlane` / `UserReference` は `2800`。

**したがって「レベルオブジェクトをストーリレベルへ関連付けて、断面の高さに追従させる」
ことは SDK からはできない。** 断面注釈では高さを注釈の Y から読ませ、名前は自分で
書く（レガシーの `Title`）。

## 拘束（`IsElevationBenchmarkConstrained`）

`Interfaces/VectorWorks/Extension/IMarkersPluginSupport.h` に
`IsElevationBenchmarkConstrained(hObject)` と `RemoveElevationBenchmarkConstraining(hObject)`
がある（**このヘッダは `VectorworksSDK.h` から引き込まれないので名指しで include する**）。
**SDK から作って注釈へ置いた個体は、どの設定でも `false` だった**（拘束は UI の操作で
付くものと思われる。【推定】）。

## パラメータ（主なもの）

### `Elevation Benchmark2`（レベル基準線・33 欄）

| universal 名 | ローカライズ名 | 欄型 | 既定 |
| --- | --- | --- | --- |
| `Axis` | 測定に使用する座標軸 | ポップアップ | `ZAxis3DMode` |
| `Datum` | 測定基準 | ポップアップ | `UserReference` |
| `RefElev` | 基準高さ | 座標 | 0 |
| `CustElev` | 高さ（カスタム） | 文字 | 空 |
| `Offset` | オフセット | 座標 | 0 |
| `Elevation` | 高さ | 静的文字（**読み取り用**） | 0 |
| `Note` | 備考 | 文字 | 空 |
| `PrimaryUnits` / `RoundingPrecision` / `ShowUnitMark` | 主単位 / 端数丸めの精度 / 単位記号を表示 | ポップアップ・真偽 | `DocumentUnits` / `EPrec1` / `True` |
| `EPfx` / `ESfx` | 高さの前記号 / 後記号 | 文字 | 空 |
| `MarkerScaleFactor` | マーカーの倍率 | 実数 | 1 |
| `UseHorizontalLeader` / `UseLeaderOffset` | 水平引出線を使用 / 引出線オフセットを使用 | 真偽 | `True` / `False` |
| `RefElevSeaLevel` | 海抜参照高さ | 座標 | 0 |

`__` で始まる欄（`__Edit Layout` / `__StoryName` / `__LevelTypeName` / `__IsHidden` …）は
内部用。`__StoryName` / `__LevelTypeName` へ書いても絵は変わらない。

### `Elevation Benchmark`（レガシー・47 欄）

| universal 名 | ローカライズ名 | 欄型 | 既定 |
| --- | --- | --- | --- |
| `Title` | タイトル | 文字 | ベンチマーク　タイトル |
| `Title Width` | タイトル文字幅の上限 | 座標 | 7500 |
| `Elevation Display` | 高さ表示 | ポップアップ（5 択） | `Z value relative to ground plane` |
| `Elevation` | 高さ（カスタム） | 文字 | レベル基準線 |
| `DatumY` | 基準高さ | 座標 | 0 |
| `Style` | 形式 | ポップアップ（`US` / `ISO`） | `US` |
| `LineOrient` | 引出線の位置 | ポップアップ（2 択） | `Top of Marker` |
| `TitleOrient` / `ElevationOrient` | タイトル / 高さ表示の位置 | ポップアップ（4 択） | `Above Line` / `Below Line` |
| `Orientation` | マーカーの位置 | ポップアップ（`Left` / `Right`） | `Right` |
| `Fill` | マーカーの属性 | ポップアップ（3 択） | `Filled` |
| `Prefix` | 前記号 | ポップアップ（3 択） | `None` |
| `Factor` / `CrossScale` / `MFact` | 記号の倍率 / 十字の倍率 / マーカーサイズ | 実数・座標 | 1 / 1 / 6.35 |
| `ShoulderLength` / `LineLength` / `Offset` / `UseOffset` | 水平線の長さ / 引出線の長さ / オフセット | 座標・真偽 | 1250 / 152.4 / 620 / `False` |

`UseY` / `Use Control Point` / `ControlPoint01X` …（`__NNA_DO_NOT_CHANGE`）は
`Elevation Display` の内部表現と制御点。**直接書かない**（上記）。

## 置くときの作法

- **注釈へ移したら座標を書き直す。** `AddViewportAnnotationObject` は VW が決めた位置へ
  落とすので、`VWParametricObj::SetPointObjectPos` で注釈空間の座標を明示する
  （[Data Tags](Data%20Tags.md)「置いた後に測って動かす」と同じ理由）。
- **`ResetObject` を忘れない。** 欄を書いただけでは絵は変わらない。
- **`DefineCustomObject(name, kCustomObjectPrefNever)` を、作る前に 1 度呼ぶ。**
  呼ばないと「オブジェクトの設定」ダイアログが出て止まる
  （[Parametric Objects](Parametric%20Objects.md)）。
- 注釈へ後から足した図形のクラスはビューポートで非表示のままなので、置いた後に
  全クラスを表示へ戻して再更新する（[Viewports](Viewports.md)）。

## 描いた文字を機械で読む

レベルオブジェクトが「絵に何を出したか」は、**PIO が吐いた図形を辿って
テキスト（`kTextNode`）を読めば分かる**——目視を頼まなくてよい。

```cpp
for (MCObjectHandle child = gSDK->FirstMemberObj(h); child != nil; child = gSDK->NextObject(child))
	if (gSDK->GetObjectTypeN(child) == kTextNode)
		TXString text = gSDK->GetTextChars(child);   // ここに描かれた文字が入っている
```

レベル基準線では、この経路で**レイアウトのトークンそのもの**（`#Elev#` /
`#STLT#-#STPS#`）と**解決後の値**（`2800` / `-`）の両方が拾える。前者はレイアウトの
定義、後者が実際に見えている文字である。
