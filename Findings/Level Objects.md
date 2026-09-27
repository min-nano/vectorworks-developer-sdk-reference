# レベル（標高）オブジェクト — 断面ビューポートの注釈へ置く

軸組図（断面ビューポート）へ **GL・1FL・2FL・軒高**のような高さの印を置くための知見。
[issue #130](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/130) の
実機確認（VW 2026 / mac）による。断面ビューポートそのものの作り方は
[Viewports](Viewports.md)、注釈空間の座標は [Data Tags](Data%20Tags.md) にある。

> **【訂正・再調査中】2 つの記述が実機の画面と食い違っている**（issue #130 を開き直した）。
> 利用者の実機では、**断面ビューポートの注釈に置いたレベル基準線が、測定に使用する
> 座標軸＝〈Z軸（3Dモード）〉のまま `FL-1 612` / `GL-F 0` / `FL-2 3571` を出していた**
> ——つまり**ストーリレベルと関連付いていて、高さもストーリレベルの高さ**である。
> OIP の「測定基準」に入っていたのは **`FL`（レベル種別の名前）**で、
> **「マーカーレイアウトの編集」で `#Elev#` / `#STLT#-#STPS#` の動的テキストを編集できる**。
> したがって下記のうち **「ストーリレベルへの関連付けは SDK からできない」と
> 「レベル基準線には名前を書く口が無い」は撤回**し、取り直している。
> **見当は外れた**——`Datum` に動的な選択肢は無い（`PopupGetChoices` は 0 件）。
> **正体は「関連付けは置き場所で決まる」**で、ストーリ従属のデザインレイヤに置けば
> `Datum` を何も書かなくても `FL-2 階` と高さが出る（下記「ストーリ…」節）。
> 断面注釈で同じことをする道は**まだ詰めている最中**。
> **それ以外の記述（注釈の Y の読ませ方・レガシーの挙動・書いても入らない値）は
> 実測のまま有効。**

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

**ストーリレベルとの関連付けについては上の【訂正・再調査中】を見ること**——実機の UI では
注釈内でも関連付いており、その場合の高さは注釈の Y ではなく**ストーリレベルの高さ**に
なる。ここに書いた「注釈の Y から読ませる」道は、**関連付けを使わないときの道**である。

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

- **【訂正】「レベル基準線には名前を書く口が無い」は誤り。** 実機の UI には
  **「マーカーレイアウトの編集」**があり、`#Elev#` / `#STLT#-#STPS#` という動的
  テキストを差し替えられる（レイアウト側が名前を出す口）。**SDK からそのレイアウトに
  触れるかは再調査中**（issue #130）。以下の「効かなかった」欄の記録はそのまま有効:
  `Note`（備考）・`EPfx`（高さの前記号）・`ESfx`（高さの後記号）へ書いても
  **絵には出ない**（欄には入る）。
  **`Datum` ＝ `Custom` ＋ `CustElev` も同じで、`Elevation` 欄には `GL` が入るのに
  描かれる文字は `0` のまま**（`Axis` の両方で、`ResetObject` を 2 度掛けても変わらず）。
  絵の中の名前らしき文字はレイアウトのトークン `#STLT#-#STPS#`（ストーリレベル名）で、
  **プローブではストーリ従属のデザインレイヤに置いたときだけ埋まった**が、実機の UI では
  注釈でも埋まっている（上の【訂正・再調査中】）。**レイアウトの実体はプロファイル
  グループ**だと分かったので、そこを差し替えられるかを確認中（下記）。

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

**関連付けは `Datum` で選ぶものではなく、置き場所で決まる。** ストーリを 2 つ
（1 階＝0 / 2 階＝2800）作った文書での実測:

| 置いた場所 | `Datum` | 描かれた高さ | 絵に出るストーリレベル名 |
| --- | --- | --- | --- |
| **ストーリ従属のデザインレイヤ**（2 階・高さ 2800） | 既定（`UserReference`）のまま**何も書かない** | `2800` | **`FL-2 階`** |
| 断面ビューポートの注釈（Y=2800） | 同上 | `0` | `-` |

- **`Datum` ＝ `StoryLevel` は SDK からは書けない**（書くと `GroundPlane` になる）。
  **しかしそれは関連付けの可否とは関係が無い**——上のとおり、デザインレイヤに置いた
  個体は `Datum` を触らずに関連付いている。#131 でここを「関連付けられない」と
  結論したのが誤りだった。
- **`Datum` に動的な選択肢は無い。** 実機の OIP は「測定基準」に `FL`（レベル種別名）を
  見せるが、`VWParametricObj::PopupGetChoices`（欄名版・欄索引版とも）は **0 件**を返す
  ——あの一覧はプラグインの UI が実行時に組んでいるもので、レコードには入っていない。
  `Datum` へ `FL` や `GL` と書いても `GroundPlane` になる（知らない値は倒される）。
- **断面ビューポートの注釈に置いた個体をストーリレベルへ結ぶ道は、まだ確定していない**
  （issue #130 で継続中）。実機では UI から置いた個体が注釈の中でも `FL-1 612` を
  出しているので、道はある。

## マーカーレイアウトの実体はプロファイルグループ

**レベル基準線の「マーカーレイアウトの編集」で触るものは、PIO 自身のプロファイル
グループである**（`ISDK::GetCustomObjectProfileGroup` / `SetCustomObjectProfileGroup`。
[Data Tags](Data%20Tags.md) のタグレイアウトと同じ作り）。実測（型は `Objs.TDType.h`）:

```
プロファイルグループ: 型=11（グループ）
  中身の型: 10（テキスト）/ 10（テキスト）/ 21 / 0
  テキストの中身: 〈#Elev#〉 と 〈#STLT#-#STPS#〉
```

- `#Elev#` が高さ、`#STLT#-#STPS#` がストーリレベル名に解決される**動的テキスト**。
  実機の UI では「レベル基準線フィールドの定義...」でこのトークンを編集できる。
- **ここを SDK から差し替えて任意の名前（"GL" など）を出せるかは確認中**（issue #130）。

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
