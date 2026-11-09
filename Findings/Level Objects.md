# レベル（標高）オブジェクト — 断面ビューポートの注釈へ置く

軸組図（断面ビューポート）へ **GL・1FL・2FL・軒高**のような高さの印を置くための知見。
[issue #130](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/130) の
実機確認（VW 2026 / mac）による。断面ビューポートそのものの作り方は
[Viewports](Viewports.md)、注釈空間の座標は [Data Tags](Data%20Tags.md) にある。
実測の表・読み戻しの記録・訂正の経緯は [計測記録](../Metrics/Level%20Objects.md)。

> **【訂正の記録】#131 の「ストーリレベルへの関連付けは SDK からできない」「レベル基準線には
> 名前を書く口が無い」は誤り——どちらもできる。** 関連付けは `Datum` 単体ではなく内部の
> 2 欄と合わせた**3 つ組**、名前はパラメータではなく**マーカーレイアウト**が出す。
>
> **【訂正の記録 2】#133 の「注釈では名前と高さは両立しない」も誤り**（issue #135）。
> **3 つ組だけで両方出る**（`Axis` は既定の `ZAxis3DMode` のまま）。誤りの元は SDK だけで
> 組み立てた文書で測って一般化したこと（`0` になる理由は
> [issue #141](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/141)）。
> 経緯は [計測記録](../Metrics/Level%20Objects.md)。

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

- **SDK ヘッダには内部 ID しか無い**（`MiniCadHookIntf.h`）ので、universal 名は実機に尋ねる
  （`ISDK::GetPluginType` / `ISDK::GetLocalizedPluginName`）。
- **`Elevation Benchmark2`（レベル基準線・663・33 欄）が現行**、`Elevation Benchmark`
  （レベル（横断面）（レガシー）・102・47 欄）が旧ツール、`Stake Object`（レベル・370）は
  地形モデル向け（[実測の表](../Metrics/Level%20Objects.md#どのオブジェクトかvw-2026-には-3-つある)）。
- `ElevationBenchmark`（空白なし）・`Level Marker`・`Datum Marker` などの綴りは**存在しない**。
- **どれもスタイル無しで作れる**（`CreateCustomObject` だけで足りる）。

## 高さは何で決まるか

- **どちらのレベルオブジェクトも、既定では注釈空間の Y（＝Z）を読まない**（高さは `0` の
  まま）。**読ませるには欄を 1 つ切り替える**——レガシーは `Elevation Display` ＝
  `Y value relative to reference elevation`、レベル基準線は `Axis` ＝ `YAxis2DMode`。
  どちらも動かせば追う。基準を引く欄はレガシー `DatumY`・レベル基準線 `RefElev`
  （[実測](../Metrics/Level%20Objects.md#高さは何で決まるか)）。
- **`Offset`（レベル基準線）は高さの数値に効かない**（引出線のオフセット用）。
- **レガシーの `Elevation Display` はポップアップだけを書く。** 内部の真偽欄 `UseY` /
  `Use Control Point` を先に倒すと `ResetObject` のたびにそこから作り直され、書き込みが負ける。
- `Distance from control point` は **-Y** を返し、動かしても変わらない——高さの表示に使えない。

### `Elevation Display`（レガシー）の 5 択

- 注釈で使えるのは **`Y value relative to reference elevation`**（Y=2800 で `2800`）と
  **`Custom`**（`Elevation` 文字欄がそのまま出る）だけ。Z を見る 2 つは注釈では `0`
  （[5 択の実測](../Metrics/Level%20Objects.md#elevation-displayレガシーの-5-択)）。

### `Datum`（レベル基準線）の 6 択

- `GroundPlane` / `DesignLayerZ` / `ControlPoint` / `UserReference` / `Custom` / `StoryLevel`。
- **`Axis` ＝ `YAxis2DMode` にすると入るのは `UserReference` / `ControlPoint` / `Custom` だけ**。
  Z を見る 3 つは `UserReference` へ戻る——**「注釈の Y を読む」と「Z を基準にする」は
  両立しない**（[実測](../Metrics/Level%20Objects.md#datumレベル基準線の-6-択)）。

## 名前（表示名）を書く口

### レガシー（`Elevation Benchmark`）

- **`Title` は書いた文字がそのまま絵に出る。** `Elevation Display` ＝ `Custom` なら
  `Elevation`（文字欄）が高さの位置にそのまま出る（空なら数値なし）
  （[実測](../Metrics/Level%20Objects.md#レガシーelevation-benchmark)）。

### レベル基準線（`Elevation Benchmark2`）——**パラメータではなくレイアウトが出す**

- `Note` / `EPfx` / `ESfx` / `CustElev` をいくら書いても絵は変わらない（欄には入る）。
- 名前を出す道は 2 つ: **ストーリレベルへ結ぶ**（レベル名が自動で出る）か、
  **マーカーレイアウトのテキストを差し替える**（任意の固定文字）。

### マーカーレイアウトを差し替えて任意の名前を出す

- **レイアウトの実体は PIO 自身のプロファイルグループ**（`ISDK::GetCustomObjectProfileGroup` /
  `SetCustomObjectProfileGroup`。[Data Tags](Data%20Tags.md) のタグレイアウトと同じ作り）。
  中身は動的テキスト 〈#Elev#〉 と 〈#STLT#-#STPS#〉。
- **新しいテキストを `CreateTextBlock` で作ってグループへ入れ、渡し直す。中身を
  `DeleteText` ＋ `AddTextFromBuffer` で入れ替えるだけでは絵に出ない**
  （[実測](../Metrics/Level%20Objects.md#マーカーレイアウトを差し替えて任意の名前を出す)）。

## 単位・丸め（レベル基準線）

- **`PrimaryUnits`（12 択）を選ぶと単位記号が数値に付く。** `ShowUnitMark` ＝ `False` で記号
  だけ消える。`RoundingPrecision` は 10 択、既定 `EPrec1`。
- レガシーには単位の欄が無い。`Prefix` ＝ `Plus-Minus` を負の値に当てると `±-2800` になる
  （VW 側の作り）（[実測](../Metrics/Level%20Objects.md#単位丸めレベル基準線)）。

## 書いても入らない値がある（必ず読み戻す）

- **`SetParamValue` は入らなかったことを教えてくれない**ので、書いたら読み戻す
  （[Investigation Techniques](Investigation%20Techniques.md)）。
- 戻される例: レガシーの `Above Marker`（→ `Outside Marker`）・`Half Filled`（→ `Filled`）、
  レベル基準線の `Datum` ＝ `StoryLevel` 単独（→ `GroundPlane`）など
  （[一覧](../Metrics/Level%20Objects.md#書いても入らない値がある必ず読み戻す)）。

## ストーリ／ストーリレベルとの関連付け

**関連付けは `Datum`（測定基準）単独で選ぶものではない。** 口は 2 つ:

1. **ストーリ従属のデザインレイヤに置く**——何も書かなくても関連付く（名前も高さも出て、
   ストーリの高さを変えれば追う）。
2. **`__StoryName` ＋ `__LevelTypeName` ＋ `Datum`＝`StoryLevel` の 3 つを書く**——注釈の
   中でも効く（順不同）。**3 つ揃って初めて効く。** `Datum` 単独は `GroundPlane` へ倒され、
   名前欄だけだと名前も出ない
   （[実測](../Metrics/Level%20Objects.md#ストーリストーリレベルとの関連付け)）。

- **`Datum` に動的な選択肢は無い**——`PopupGetChoices` は 0 件。OIP が見せる `FL` などは
  UI が実行時に組んでいる。

### 断面ビューポートの注釈でも、名前と高さは両立する——**3 つ組だけでよい**

UI が置いた個体と SDK 製を同じ注釈で全欄突き合わせた（issue #135。実物件の図面）。

- **UI が置いた個体が持っていたのは 3 つ組だけ**（`Axis` は既定の `ZAxis3DMode`）。
  **SDK から 3 つ組を書けば同じ数値と名前が出る。`Axis` は触らない。**
- **数値を決めているのは 3 つ組**——`ResetObject` がストーリレベルを引き直して `Elev` を
  書き直す（別のレベルを書いたら `6776` → `3176` に変わった）。
- **`Elev`（`__NNA_DO_NOT_CHANGE`）は出力**で、結んだ個体では書いても入らない。
- **素の個体は文書の PIO 既定値をそのまま描く**ので、直前に UI でツールを使った図面では
  何も書いていない個体がもっともらしい数値を出す。**数値が出たことを効いた証拠にしない**
  ——確かめるなら別のレベルを書いて動くかを見る。
- 全 33 欄を写しても結果は変わらない。複製しても同じ高さと名前を出す。注釈の中の個体の
  3D 位置は `z=0`——**高さは Z ではなくストーリレベルから来る**
  （[実測](../Metrics/Level%20Objects.md#断面ビューポートの注釈でも名前と高さは両立する3-つ組だけでよい)）。

#### 同じ 3 つ組でも `0` になるビューポートがある（#141）

- **描かれるのはストーリレベルの絶対Z**（階の高さ ＋ 階内の相対Z）。乗っているレイヤの Z
  ではない。**`0` になるかを決めるのはストーリの作り方ではなくビューポート**
  （[Layers and Stories](Layers%20and%20Stories.md)「注釈に高さが出るかは、ビューポートで決まる」）。
- 置き先ごと: デザインレイヤ / シートレイヤへ直に・UI が作った断面ビューポートの注釈は
  **絶対Z**、伏図ビューポートの注釈は **`0`**、SDK の `CreateSectionViewport` の注釈は
  **素のままなら `0`**（下記で直せる）
  （[実測](../Metrics/Level%20Objects.md#同じ-3-つ組でも-0-になるビューポートがある141)）。
- **使うときは `Elev`（または `Elevation`）を読み戻して `0` でないことを確かめる。**
- 【訂正の記録】`GetStoryLevelElevation` の `0` は**階内の相対Z**で、「SDK で作ったストーリに
  高さが無い」のではなかった。

#### SDK で作った断面ビューポートでも出せる——**表示レイヤとクラスを表示へ戻して更新する**（#151）

- `CreateSectionViewport` 直後はビュー行列（`ovViewportViewMatrix` ＝ 1050）が単位行列。
  **表示レイヤとクラスを表示へ戻してから `UpdateViewport` すると、1050 に断面の向き（1055）
  と同じ値が入り、以後も保たれる。** そのうえで 3 つ組を書いて `ResetObject` すれば絶対Z が
  出る。**1050 へ自分で書く必要は無い。**
- 【訂正の記録】#147 の「1055 を 1050 へ写す」は回り道だった。手順と実測は
  [Viewports](Viewports.md)「SDK で作った断面ビューポートの注釈でレベル基準線に高さを出す」。

#### ストーリバウンド（`SetObjectStoryBound` の一群）は使っていない

- UI が置いた個体も `HasObjectStoryBounds=false`。**関連付けはバウンドではなく 3 つ組
  （レコードの欄）で持たれている**（[Parametric Objects](Parametric%20Objects.md)）。

#### 高さを注釈の Y から読ませたいときだけ、`Axis` を倒す

- **`Axis` を `YAxis2DMode` にすると関連付けが外れる**——`__StoryName` / `__LevelTypeName` が
  空に戻り、`Datum` も `UserReference` に戻る（書く順を変えても同じ）。名前は `-` になる。

#### 描かれる名前は「レベル種別名-ストーリの接尾語」

- `#STLT#-#STPS#` の解決。〈1階〉接尾語 `1` ＋ `FL` → **`FL-1`**、〈屋根〉＋`軒高` →
  **`軒高-R`**。接尾語が空なら階の名前が出る（`FL-2 階`）。

## 拘束（`IsElevationBenchmarkConstrained`）

- `IMarkersPluginSupport.h` の `IsElevationBenchmarkConstrained` /
  `RemoveElevationBenchmarkConstraining`（**`VectorworksSDK.h` から引き込まれないので名指しで
  include する**）。
- **UI 固有の印ではない**——3 つ組でストーリレベルへ結んだ時点で `true`。読めるのは
  「ストーリレベルに拘束されているか」で、作った経路ではない
  （[実測](../Metrics/Level%20Objects.md#拘束iselevationbenchmarkconstrained)。以前の【推定】は誤り）。

## パラメータ（主なもの）

欄名・ローカライズ名・欄型・既定の一覧は
[計測記録](../Metrics/Level%20Objects.md#パラメータ主なもの)。

### `Elevation Benchmark2`（レベル基準線・33 欄）

- 要は `Axis`（既定 `ZAxis3DMode`）・`Datum`（既定 `UserReference`）・`RefElev`、描かれる
  数値の `Elev`（出力）。`__` で始まる欄は内部用。

### `Elevation Benchmark`（レガシー・47 欄）

- 要は `Title`・`Elevation Display`・`Elevation`・`DatumY`。`UseY` / `Use Control Point` /
  制御点の欄（`__NNA_DO_NOT_CHANGE`）は直接書かない。

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

- **PIO が吐いた図形を `FirstMemberObj` → `NextObject` で辿り、`kTextNode` を
  `GetTextChars` で読めば**、絵に何を出したかが分かる——目視を頼まなくてよい。レベル基準線
  ではトークン（定義）と解決後の値（見えている文字）の両方が拾える
  （[コード](../Metrics/Level%20Objects.md#描いた文字を機械で読む)）。
