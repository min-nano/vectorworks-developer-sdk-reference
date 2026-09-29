# レベル（標高）オブジェクト — 断面ビューポートの注釈へ置く

軸組図（断面ビューポート）へ **GL・1FL・2FL・軒高**のような高さの印を置くための知見。
[issue #130](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/130) の
実機確認（VW 2026 / mac）による。断面ビューポートそのものの作り方は
[Viewports](Viewports.md)、注釈空間の座標は [Data Tags](Data%20Tags.md) にある。

> **【訂正の記録】#131 でマージした 2 つの結論は誤りだった**（issue #130 を開き直して
> 取り直した）。「ストーリレベルへの関連付けは SDK からできない」「レベル基準線には
> 名前を書く口が無い」——**どちらもできる**。誤りの元は、`Datum` という**ポップアップ欄
> 単体で関連付けを選ぶ**と思い込んだこと（実際は内部の 2 欄と合わせた**3 つ組**）と、
> 名前を**パラメータ**で探したこと（実際は**マーカーレイアウト**が出す）。
> 正しい内容は下記の各節にある。
>
> **【訂正の記録 2】#133 でマージした「注釈では名前と高さは両立しない」も誤りだった**
> （issue #135 で、**UI が置いた個体と SDK 製の個体を同じ図面で全欄突き合わせて**
> 取り直した）。**3 つ組だけで両方出る**——`Axis` は既定の `ZAxis3DMode` のままでよい。
> 誤りの元は、**SDK だけで組み立てた文書で測って一般化したこと**（あの文書では高さが
> `0` になる。その理由は
> [issue #141](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/141)
> へ切り出した）。正しい内容は下記「注釈でも名前と高さは両立する」にある。

## 先に結論

**断面ビューポートの注釈にも「名前＋高さ」は出せる。オブジェクトは 2 つとも使えて、
名前の出し方が違う。**

### 現行の `Elevation Benchmark2`（レベル基準線）——名前はレイアウトかストーリレベル

**ストーリレベルへ結べるなら、それがいちばん良い**——名前も高さもストーリレベルから
来るので、どちらも自分で書かなくてよい（下記「注釈でも名前と高さは両立する」）。

```cpp
gSDK->DefineCustomObject("Elevation Benchmark2", kCustomObjectPrefNever);  // 作る前に 1 度
MCObjectHandle h = gSDK->CreateCustomObject("Elevation Benchmark2", WorldPt(x, y), 0.0);
gSDK->AddViewportAnnotationObject(hSectionVP, h);
VWParametricObj obj(h);
obj.SetPointObjectPos(VWPoint2D(x, y));        // 注釈空間の座標（縦は**高さの数値に効かない**）
obj.SetParamValue("__StoryName", "1階");       // ← この 3 つ組で「名前＋高さ」が両方出る
obj.SetParamValue("__LevelTypeName", "FL");    //    （Axis は既定の ZAxis3DMode のまま）
obj.SetParamValue("Datum", "StoryLevel");
gSDK->ResetObject(h);                          // → 〈FL-1〉〈576〉（レベルの絶対高さ。実測）
```

**ストーリを使っていない図面**では、高さを注釈の Y から読ませ、名前はレイアウトの
固定文字にする（この 2 つは併用できる）。

```cpp
obj.SetParamValue("Axis", "YAxis2DMode");      // 高さは注釈の Y（＝Z）から読む
// 名前はマーカーレイアウトのテキストを作り直して渡し直す（任意の固定文字。下記）
gSDK->ResetObject(h);
```

### レガシーの `Elevation Benchmark`（レベル（横断面）（レガシー））——名前はパラメータ

```cpp
gSDK->DefineCustomObject("Elevation Benchmark", kCustomObjectPrefNever);
MCObjectHandle h = gSDK->CreateCustomObject("Elevation Benchmark", WorldPt(x, y), 0.0);
gSDK->AddViewportAnnotationObject(hSectionVP, h);
VWParametricObj obj(h);
obj.SetPointObjectPos(VWPoint2D(x, y));
obj.SetParamValue("Title", "1FL");             // 名前。そのまま絵に出る
obj.SetParamValue("Elevation Display",
				  "Y value relative to reference elevation");  // 高さは注釈の Y から読む
obj.SetParamValue("DatumY", "0");              // 基準（この値を引いた数値が出る）
gSDK->ResetObject(h);
```

数値を出さずに名前だけにしたいなら、レガシーは `Elevation Display` ＝ `Custom` ＋
`Elevation`（文字欄）を空に。レベル基準線はレイアウトのテキストを差し替える。

**ストーリレベルへ結ばないなら、どちらも既定では注釈の Y を読まない**
（高さが `0` のままになる）ので、上の `Axis` / `Elevation Display` の 1 行を必ず入れる。

**`Axis`＝`YAxis2DMode` と「ストーリレベルへの関連付け」は両立しない**（関連付けが
外れる。下記）。**だから両方を同時に書かない**——ストーリレベルへ結ぶなら `Axis` は
既定のまま触らない。

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

### レガシー（`Elevation Benchmark`）

- **`Title`（タイトル）は、書いた文字がそのまま絵に出る**（実測）。
  位置は `TitleOrient` の 4 択（ただし `Above Marker` は入らない。下記）。
- **`Elevation Display` ＝ `Custom` にすると、`Elevation`（文字欄）がそのまま
  高さの位置に出る。** 空にすれば数値は出ない。実測:

  | 書いたもの | 描かれた文字 |
  | --- | --- |
  | `Custom` ＋ `Elevation`＝空 ＋ `Title`＝`1FL` | 〈1FL〉〈 〉（**数値なし**） |
  | `Custom` ＋ `Elevation`＝`GL` ＋ `Title`＝空 | 〈 〉〈GL〉 |
  | `Custom` ＋ `Elevation`＝`+2800` ＋ `Title`＝`2FL` | 〈2FL〉〈+2800〉 |

### レベル基準線（`Elevation Benchmark2`）——**パラメータではなくレイアウトが出す**

名前を出す道は 2 つある。**どちらもパラメータではない**ので、`Note`（備考）・
`EPfx` / `ESfx`（高さの前後記号）・`CustElev`（高さ（カスタム））をいくら書いても
絵は変わらない（欄には入る。実測）。

1. **ストーリレベルへ結ぶ**と、レベル名（`FL-2 階` のような「レベル種別-ストーリ」）が
   自動で出る（下記「ストーリ…」）。
2. **マーカーレイアウトのテキストを差し替える**と、任意の固定文字が出る（下記）。

### マーカーレイアウトを差し替えて任意の名前を出す

**レイアウトの実体は PIO 自身のプロファイルグループ**（`ISDK::GetCustomObjectProfileGroup` /
`SetCustomObjectProfileGroup`。[Data Tags](Data%20Tags.md) のタグレイアウトと同じ作り）。
実測した中身（型は `Objs.TDType.h`）:

```
プロファイルグループ: 型=11（グループ）
  中身の型: 10（テキスト）/ 10（テキスト）/ 21 / 0
  テキスト: 〈#Elev#〉（高さ）と 〈#STLT#-#STPS#〉（ストーリレベル名）＝**動的テキスト**
```

**差し替えは「新しいテキストを作ってグループへ入れ、`SetObjectProfileGroup` で渡し直す」。
中身を入れ替えるだけでは絵に出ない**（実測。データタグの「中身を入れてから渡す」と同じ筋）:

| 手 | レイアウトの中身 | **描かれた文字** |
| --- | --- | --- |
| `DeleteText` ＋ `AddTextFromBuffer` で中身を入れ替えるだけ | 〈#Elev#〉〈GL〉 | 〈2800〉〈**-**〉（**変わらない**） |
| 古いテキストを消し、`CreateTextBlock` ＋ `AddObjectToContainer` で入れて**渡し直す** | 〈#Elev#〉〈GL〉 | 〈2800〉〈**GL**〉 |

つまり**断面ビューポートの注釈でも「▼GL ＋ 高さ」を出せる**——高さは `Axis`＝
`YAxis2DMode` で注釈の Y から、名前はレイアウトの固定文字から。

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

**関連付けは `Datum`（測定基準）で選ぶものではない。** 選ぶ口は 2 つある:

1. **ストーリ従属のデザインレイヤに置く**——**何も書かなくても関連付く**。
2. **`__StoryName` ＋ `__LevelTypeName` ＋ `Datum`＝`StoryLevel` の 3 つを書く**
   ——注釈の中でも効く（書く順番は問わない）。**3 つ揃って初めて効く**のが要点で、
   `Datum` 単独だと `GroundPlane` へ倒され、名前欄だけだと `Datum` は
   `UserReference` のまま名前も出ない（どちらも実測）。

実測（**SDK だけで組み立てた文書**。ストーリ 1 階＝0 / 2 階＝2800、レベル種別 `FL`）
——**実物件の図面では注釈でも高さが出る**ので、下の「注釈でも名前と高さは両立する」と
併せて読むこと:

| 置いた場所 | 書いたもの | `Datum` の読み戻し | 描かれた高さ | 描かれた名前 |
| --- | --- | --- | --- | --- |
| ストーリ従属のデザインレイヤ（2 階） | **何も書かない** | `UserReference`（既定のまま） | `2800` | **`FL-2 階`** |
| 断面ビューポートの注釈（Y=2800） | 何も書かない | `UserReference` | `0` | `-` |
| 断面ビューポートの注釈（Y=2800） | `__StoryName` ＋ `__LevelTypeName` ＋ `Datum`＝`StoryLevel` | **`StoryLevel`** | `0`（**この文書では**。下記） | **`FL-2 階`** |
| 断面ビューポートの注釈（Y=2800） | 名前欄 2 つだけ（`Datum` を書かない） | `UserReference` | `0` | `-`（**出ない**） |
| シートレイヤへ直に置く | 名前欄 2 つだけ | `UserReference` | **`2800`**（レベルの高さ） | `-` |

- **ストーリの高さを変えると、デザインレイヤの個体は追う**（2800 → 3500 に変えたら
  描かれた高さも `3500`）。
- **`Datum` へ `StoryLevel` と書くだけでは入らない**（`GroundPlane` になる）——名前欄が
  埋まっているときだけ入る。
- **`Datum` に動的な選択肢は無い。** 実機の OIP は「測定基準」に `FL`（レベル種別名）を
  見せるが、`VWParametricObj::PopupGetChoices`（欄名版・欄索引版とも）は **0 件**を返す
  ——あの一覧はプラグインの UI が実行時に組んでいて、レコードには入っていない。
  `Datum` へ `FL` や `GL` と書いても `GroundPlane` に倒される。
### 断面ビューポートの注釈でも、名前と高さは両立する——**3 つ組だけでよい**

**実機の図面で、UI がツールで置いた個体と SDK 製の個体を同じ注釈の中で全欄突き合わせ、
1 段ずつ足して確かめた**（[issue #135](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/135)。
ストーリ 基礎/1階/2階/屋根、レベル種別 GL/FL/軒高 の実物件の図面。2 回の実行）。

- **UI が置いた個体が持っていたのは、3 つ組だけだった。** `Axis`＝`ZAxis3DMode`（既定）・
  `Datum`＝`StoryLevel`・`__StoryName`・`__LevelTypeName`。ほかに特別なものは何も無い
  （ストーリバウンドも拘束オブジェクトも無し。下記）。
- **SDK から 3 つ組を書けば、同じ注釈の中で同じ数値と名前が出る。`Axis` は触らない。**
- **数値を決めているのは 3 つ組である**（`Elev` 欄の持ち越しではない）。決め手の実測——
  **既定値として `6776` を持って生まれた個体に、別のレベル（2階/FL）の 3 つ組を書いたら、
  数値が `3176` に変わった**（描かれた文字も `3176` / `FL-2`）。つまり `ResetObject` は
  **ストーリレベルを引き直して `Elev` を書き直す**。

  | 何をしたか | `Elev` 欄 | 描かれた数値 | 描かれた名前 |
  | --- | --- | --- | --- |
  | 素のまま（`Datum`＝`UserReference`） | `6776`（文書の PIO 既定値） | `6776` | `-` |
  | 屋根/軒高 の 3 つ組 | `6776` | `6776` | `軒高-R` |
  | **2階/FL の 3 つ組**（同じ個体へ上書き） | **`3176`** | **`3176`** | **`FL-2`** |
  | さらに `Elev` へ `1234.5` を書く | `3176`（**入らない**） | `3176` | `FL-2` |

- **`Elev`（`__NNA_DO_NOT_CHANGE` の座標欄）は出力である。** レイアウトのトークン
  `#Elev#` が出すのはこの欄だが、**ストーリレベルへ結んだ個体では書いても入らない**
  （上表の最後の行）。`RefElevSeaLevel` も同じ値になる。
  - **素の個体（`Datum`＝`UserReference`）では、既定値がそのまま残って描かれる。**
    `CreateCustomObject` は文書の PIO 既定値を引き継ぐので、**直前に UI でツールを
    使った図面では、何も書いていない個体がもっともらしい数値を描く**。
    **これに騙されないこと**——数値が出たことを「効いた」証拠にしてはいけない
    （一度そう読み違えた。確かめるなら**別のレベルを書いて動くか**を見る）。
- **全 33 欄を写しても結果は変わらない**（残った差 **0 件**）。UI 製と素の SDK 製の差は
  5〜7 欄で、効くのは 3 つ組の 3 欄だけ。残りは見た目と内部フラグ
  （`__IsHidden` / `__IsFlipped` / `__IsCreatedFromDialog` / `__kTextInsertOffsetX`）で、
  高さにも名前にも効かない。
- **UI 製を複製（`DuplicateObject`）しても、複製は同じ高さと名前を出す**——状態は
  すべて個体の中（レコードの欄）にある。
- **注釈の中の個体の 3D 位置は `z=0`**（変換行列の原点も `z=0`）。「注釈空間に Z は無い」
  ままで、**高さは Z ではなくストーリレベルから来ている**。

#### 同じ 3 つ組でも `0` になるビューポートがある（#141）

**`0` になるかを決めているのは、ストーリの作り方ではなくビューポートである。**
[issue #141](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/141) で
実機確認した（実測値と潰した筋は
[Layers and Stories](Layers%20and%20Stories.md)「注釈に高さが出るかは、ビューポートで決まる」）。

> **【訂正の記録】#133 の `0` を「SDK で作ったストーリのレベルに高さが入っていない」と
> 見当を付けたが、外れだった。** SDK で `CreateStory` ＋ `SetStoryElevation` ＋
> `CreateStoryLevelTemplate`（offset 0）＋ `AddStoryLevelFromTemplate` で作ったストーリも、
> **レベルの絶対Z をきちんと持っている**（4 通りの作り方で確認）。誤りの元は
> **`GetStoryLevelElevation` が返す `0` を「高さが無い」と読んだこと**——あれは
> **階内の相対Z** で、`elevationOffset` を 0 にしたなら 0 が正しい。

**描かれるのはストーリレベルの絶対Z**（＝階の高さ ＋ 階内の相対Z）。3 つの実物件の図面で、
同じ 1 回の実行のうちに「描かれた数値」と「絶対Z」を並べて測り、**全行一致**した。
シートレイヤ（ストーリに属さない）へ置いた個体でも正しく出るので、
**「その個体が乗っているレイヤの Z」ではない。**

**置き先で決まる**（名前はどこでも出る。`0` になるのは数値だけ）:

| 置き先 | 高さ |
| --- | --- |
| そのレベルのデザインレイヤ / シートレイヤへ直に | **絶対Z** |
| **UI が作った断面ビューポートの注釈** | **絶対Z** |
| 伏図（平面）ビューポートの注釈 | **`0`**（`ovIsSectionViewport` が `false`） |
| SDK の `CreateSectionViewport` で作った断面ビューポートの注釈 | **`0`**（[#147](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/147)） |

**使うときは `Elev` 欄（または `Elevation`）を読み戻して `0` でないことを確かめる。**
`0` なら疑うのは**ストーリではなくビューポート**——同じ 3 つ組をデザインレイヤか
シートレイヤへ置けば数値は出る。

#### ストーリバウンド（`SetObjectStoryBound` の一群）は使っていない

`ISDK` にはオブジェクトを階／レベルへ結ぶ**ストーリバウンド**
（`HasObjectStoryBounds` / `GetObjectStoryBound` / `SetObjectStoryBound` /
`GetObjectBoundElevation`。構造材 PIO で使うもの。[Parametric Objects](Parametric%20Objects.md)）
があり、注釈で高さが出る仕組みの第一候補だった。**実測では違った**——UI が置いた個体も
`HasObjectStoryBounds=false` / 件数 0 で、名指しで引いた `id=-3 / 0 / 1` もすべて `false`。
**レベル基準線の関連付けは、バウンドではなく 3 つ組（レコードの欄）で持たれている。**

#### 高さを注釈の Y から読ませたいときだけ、`Axis` を倒す

ストーリを使っていない図面では、`Axis`＝`YAxis2DMode` で注釈の Y を読ませ、名前は
マーカーレイアウトの固定文字にする（上記）。**この 2 つは併用できない**:

| 書いたもの | `Datum` | 描かれた高さ | 描かれた名前 |
| --- | --- | --- | --- |
| 3 つ組 | `StoryLevel` | レベルの高さ | **レベル名** |
| 3 つ組 ＋ `Axis`＝`YAxis2DMode` | `UserReference` | 注釈の Y | **`-`** |

**`Axis` を `YAxis2DMode` にすると関連付けが外れる**——`__StoryName` / `__LevelTypeName` が
**空に戻り**、`Datum` も `UserReference` に戻る（書く順を変えても同じ）。

#### 描かれる名前は「レベル種別名-ストーリの接尾語」

レイアウトのトークン `#STLT#-#STPS#` はそう解決する。実測: ストーリ〈1階〉接尾語 `1` ＋
レベル種別 `FL` → **`FL-1`**、〈屋根〉＋`軒高` → **`軒高-R`**、〈基礎〉＋`GL` → **`GL-F`**。
**接尾語を空で作った文書では階の名前が出た**（#133 の `FL-2 階`）。

## 拘束（`IsElevationBenchmarkConstrained`）

`Interfaces/VectorWorks/Extension/IMarkersPluginSupport.h` に
`IsElevationBenchmarkConstrained(hObject)` と `RemoveElevationBenchmarkConstraining(hObject)`
がある（**このヘッダは `VectorworksSDK.h` から引き込まれないので名指しで include する**）。
**これは UI 固有の印ではない**——3 つ組を書いてストーリレベルへ結んだ時点で `true` に
なる（実測。素の個体・`Axis`＝`YAxis2DMode` の個体はいずれも `false`、UI が置いた個体は
`true`、SDK から 3 つ組を書いた個体も `true`）。つまり読み取れるのは
**「ストーリレベルに拘束されているか」**であって、作った経路ではない。

> 以前ここには「SDK から作った個体はどの設定でも `false` だった（拘束は UI の操作で
> 付くものと思われる【推定】）」と書いていた。**3 つ組を書いていなかったからで、
> 推定は誤り。**

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
| `Elev` | `__NNA_DO_NOT_CHANGE` | 座標 | 0（**描かれる数値はこれ。ただし出力**——ストーリレベルへ結んだ個体では書いても入らない。上記） |
| `RefElevSeaLevel` | 海抜参照高さ | 座標 | 0（ストーリレベルへ結ぶと `Elev` と同値になる） |
| `Note` | 備考 | 文字 | 空 |
| `PrimaryUnits` / `RoundingPrecision` / `ShowUnitMark` | 主単位 / 端数丸めの精度 / 単位記号を表示 | ポップアップ・真偽 | `DocumentUnits` / `EPrec1` / `True` |
| `EPfx` / `ESfx` | 高さの前記号 / 後記号 | 文字 | 空 |
| `MarkerScaleFactor` | マーカーの倍率 | 実数 | 1 |
| `UseHorizontalLeader` / `UseLeaderOffset` | 水平引出線を使用 / 引出線オフセットを使用 | 真偽 | `True` / `False` |

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
