# レイヤ・ストーリ・重ね順 — 計測記録

結論は [Findings「Layers and Stories」](../Findings/Layers%20and%20Stories.md)。ここには各節の測った
環境（VW の版・OS・図面）・使ったプローブ・手順・実測の表とダンプ・切り分けの経緯を置く。
節の見出しは Findings と同じ。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

## デザインレイヤの並びと並べ替え

- 辿り方: `VWDocument::GetDrawingHeaderFristMember`（綴りは SDK ママ）＋ `NextObject`。
  並べ替えに使ったのは **`InsertObjectAfter` / `InsertObjectBefore`**（VS の `HMoveForward` に当たる）。
- **実機のダンプ**（実物件をストーリに結んだレイヤで作った図面。レイヤ名 ＋ ストーリ / レベル種別 /
  レベル高さ）:

  ```
  3to3.5-柱[屋根/3to3.5-柱/5961] | R-垂木[屋根/垂木/5961] | … | R-野地板[屋根/野地板/5961]
  2-FL[2階/FL/3361] | 2to2.5-柱[2階/2to2.5-柱/3325] | … | 2-野地板[2階/野地板/3325]
  1-FL[1階/FL/561]  | 1to2-柱[1階/1to2-柱/525] | … | 1-横架材天端[1階/横架材天端/525]
  F-アンカーボルト[基礎/基礎天端/400] | F-床束[…/50] | F-底盤[…/50] | F-立上り[基礎/GL/0]
  柱伏図記号・共通（ストーリ非従属。高さ 0 で最背面）
  ```

  高さは 5961 → 3361 → 3325 → 561 → 525 → 400 → 50 → 0 の順に並んでいた。
  希望順に沿って**作って**も 27 枚中 26 枚が並べ替え対象になった。
  ストーリ非従属レイヤは作った順で、ストーリ従属レイヤの背面に並んだ。
- **並べ替えの後の観察:** 図面の並びは並べ替え後のとおり（読み戻しでも OIP でも）で、
  描き直しも走っている（`IsDirty` が下りる）が、処理中に作ったビューポートの絵は高さ順のまま
  だった。ユーザーが「更新」を 1 回押すか、ファイルを開き直すと、図面の並び（＝並べ替え後）で
  描き直された。
- **処理中の絵へ届かせようとして試した 4 通り（どれも絵は高さ順のまま）:**
  1. 並べ替えをビューポート生成の直前に置く。
  2. 並べ替えを**要素を 1 つも描く前**へ前倒しし、`SetDirty(true)` を立てて描き直す。
  3. 描き直しを **undo イベントを閉じた後**へ回し、口も `ISDK::UpdateViewport` へ替える。
  4. 描き直しを**進捗ダイアログも閉じた処理の外**まで遅らせる。さらに「処理中は 1 枚も
     描かず out-of-date の印だけ立てて VW に描かせる」も試したが、当時は「ビューポートによっては
     `SetDirty(true)` を受け付けない（`IsDirty` が立たない）ので印すら立たない」と記録した
     （**測り直しでは再現しなかった**。下の注）。
     - 【注】**測り直し**（#245）。断面 1 枚（更新済み）と平面 5 枚に掛けて、書いた直後・
       ランループの後・undo イベントを閉じた後・VW へ戻った後の 4 時点で
       `VWViewportObj::IsDirty()` と `ovViewportDirty`（1004）を読んだ:

       | ビューポート | 掛けたもの | `IsDirty` / 1004（4 時点とも） |
       | --- | --- | --- |
       | 断面・更新済み | `SetDirty(true)` | yes / T |
       | 平面・更新済み | `SetDirty(true)` | yes / T |
       | 平面・一度も更新していない | `SetDirty(true)` | yes / T |
       | 平面・更新済み（対照） | 何もしない | no / F |
       | 平面・更新済み | `SetDirty(true)` → `UpdateViewport` | no / F（`SetDirty` 直後は yes） |
       | 平面・更新済み | 1004 へ `true` を書く | yes / T |

       戻った後の読みで変わった欄は 0 / 12。「印だけ立てて VW に描かせる」ときの絵の順序は
       この測り直しでは見ていない。
- 「動かせた数」: 既に希望どおりの並びでも 0 件、並べ替えが失敗しても 0 件が返った。
- 【訂正の記録】当初ここに「レイヤ高さを取得する呼び出しが無い」と書いていた。後に
  `VWFC::VWObjects::VWLayerObj::GetElevation` / `SetElevation` と、オブジェクト変数
  `ovLayerHeightInCurrUnits`(157) が同じ箱を読み書きすることを測った
  （[#194](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/194)。下記「レイヤの高さ」）。

## レイヤを列挙する（デザインとシート・種類・順序・現在・縮尺）

**実測**（VW 2026 / Windows。`probes/runtime/layer-enum/` を MCP ブリッジのスプールから回し、
オーガナイザと目で突き合わせた。ビルド `6f0d1d834668`。
[issue #281](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/281)）。
新規の空図面（既定のデザインレイヤ「レイヤ-1」1 枚）に、デザインレイヤ `LE-D-low`（高さ
`-1000`・縮尺 1/50）→ `LE-D-high`（高さ `3000`・縮尺 1/200）、シートレイヤ `LE-30` → `LE-10` →
`LE-20` の順に `CreateLayer` で足し、`LE-10` にビューポートを 1 つ作ってから読んだ。

- `GetDrawingHeader()`（型 32）→ `FirstMemberObj` → `NextObject` の列に、デザインレイヤと
  シートレイヤが混ざって 1 本で並んだ。`VWDocument::GetDrawingHeaderFristMember()` の返す先頭は
  `FirstMemberObj(GetDrawingHeader())` と同じハンドルだった。`ISDK::ForEachLayer` もデザインと
  シートを同じ順で全部配った。
- 列の最後に、型 `0`・名前が空のオブジェクトが 1 つ並んだ（新規の空図面でも。アドレスは図面ごとに
  違う）。`ForEachLayer` はこれを配らなかった（7 件の列に対して 6 件）。
- シートにビューポート（型 122）を作っても列は 7 件のまま変わらず、増えたのはそのシートレイヤの
  直下の数（1 → 2）だけだった。
- `ovLayerType`(154) と `VWLayerObj::GetLayerType()` は同じ値を返した: デザイン `1`
  （`kLayerDesign`）・シート `2`（`kLayerSheet`）。`ovLayerType` は `Sint16` で受け取った。
  ヘッダの `ELayerType`: `kLayerDesign = 1`・`kLayerPresentation = 2`（＝`kLayerSheet`。旧名）・
  `kGuestLayerDesign = 3`（`MiniCadCallBacks.h:4560`）。`3` は下記「参照で入ってきたレイヤ」で出た。
- シートの名前（番号）は `GetObjectName` で読んだ。タイトルを空のまま作ったシートでは
  `ovLayerExpandedSheetName`(162) は番号と同じ文字列（`"LE-30"`）を返した（タイトルを入れたときの
  形は測っていない）。デザインレイヤに対しては `GetObjectVariable` が `false` を返した。

### 参照で入ってきたレイヤ（種類 `3`）の並びと名前

**実測**（VW 2026 / Windows。`probes/runtime/layer-guest/` を MCP ブリッジのスプールから回した。
ビルド `347e44924739` と `df9b22412610` の 2 回。どちらも同じ列）。新規の空図面にデザインレイヤ `LG-src` と点を 1 つ足して
一時フォルダの `…-src.vwx` へ別名保存し、続けて `…-host.vwx` へ別名保存して図面を移し、
`LG-src` を消してから `ISDK::CreateReference(<src の IFileIdentifier>)` を呼んだ
（ダイアログは出ず `true`。参照元の図面にはレイヤ-1 と LG-src の 2 枚のデザインレイヤがある）。

| # | 名前 | `ovLayerType` / `GetLayerType` | `IsObjectFromReferencedFile` | 現在 |
| --- | --- | --- | --- | --- |
| 0 | `レイヤ-1`（自分の） | `1` / `1` | `false` | ○ |
| 1 | `NNA#1_レイヤ-1` | **`3` / `3`** | **`true`** | |
| 2 | `NNA#1_LG-src` | **`3` / `3`** | **`true`** | |
| 3 | （型 0・名前なし） | — | `false` | |

- 参照で入ったレイヤは型 `31`（`kLayerNode`）で、自分のレイヤの後、末尾の型 0 の前に、
  参照元のデザインレイヤが全部入った。`ForEachLayer` も 3 件とも配った。`ovLayerType == 3`（`kGuestLayerDesign`）と
  `IsObjectFromReferencedFile(layer)` は同じ結果だった。
- `GetNamedLayer("LG-src")` は `nullptr` で、`GetNamedLayer("NNA#1_LG-src")` は列のハンドルと
  同じものを返した。画面のレイヤのポップアップにも `NNA#1_LG-src` と出た。参照は 1 回だけなので、
  `#1` が参照の通し番号か、`NNA` が何から来るかは見ていない。
- `SetCurrentLayer(<参照で入ったレイヤ>)` の後、`GetCurrentLayer()` はそのハンドルと `==` で
  一致し、列でも `current==yes` がそのレイヤに移った。
- `GetLayerScaleN` は `1` を返した（参照元のレイヤが 1:1 だったので、参照元の縮尺を写すのかは
  この見本では分からない）。
- `ovGuestLayerHasRelativePath`(163) は参照で入ったレイヤでも `GetObjectVariable` が `false` を
  返した。
- 参照元の `LG-src` の直下は保存の前に `2[17,0]`（点＋型 0）だったが、参照で入った
  `NNA#1_LG-src` の直下は `1[0]`（型 0 だけ）だった。参照の中身がどこに置かれるかは調べていない
  （図形の数え方は別 issue）。
- `DeleteObject(layer, false)` で消した直後の列に、名前の無い型 `90` のものが 1 つ挟まった
  （`CreateReference` の後の列では消えていた）。

### 列の順序とオーガナイザの並びの比較

| | 列（`NextObject` / `ForEachLayer`） | オーガナイザの `#` 列 |
| --- | --- | --- |
| デザインレイヤ | レイヤ-1 → LE-D-low → LE-D-high（**作った順**） | 1 LE-D-high・2 LE-D-low・3 レイヤ-1（**列の逆**） |
| シートレイヤ | LE-30 → LE-10 → LE-20（**作った順**） | 1 LE-30・2 LE-10・3 LE-20（**列と同じ**） |

- この図面ではデザインを先に作ったので、列の前半にデザインが固まった。
- 作った後に高さを変えても（`LE-D-low` を `-1000`、`LE-D-high` を `3000`）列は作った順のままだった。
- オーガナイザを開いて閉じても列は変わらなかった（閉じた後の再走で同じ並び・同じアドレス）。

### 現在のレイヤのハンドルの比較

- `GetCurrentLayer()` の返すハンドルは、列で辿ったハンドルとそのまま `==` で一致した。
  シートレイヤを現在にしても同じ（`SetCurrentLayer(LE-20)` の後、`GetCurrentLayer()` は
  `LE-20` の列のハンドルと同値。列の中で一致するのは 1 枚だけ）。
- 図面を閉じて新規を開いたら、新しい図面の「レイヤ-1」が閉じた図面の `LE-D-low` と同じ
  アドレスで返り、ヘッダも閉じる前と同じアドレスだった。

### 縮尺の読みと書き込みの実測

| レイヤ | `SetLayerScaleN` で書いた値 | `GetLayerScaleN` / `VWLayerObj::GetScale` | オーガナイザ |
| --- | --- | --- | --- |
| LE-D-low（デザイン） | `50` | `50` | 1:50 |
| LE-D-high（デザイン） | `200` | `200` | 1:200 |
| レイヤ-1（デザイン。既定） | — | `1` | 1:1 |
| LE-30 / LE-10（シート） | — | `1` | （縮尺の欄は無い） |
| LE-20（シート） | `50` | **`1`**（書いても変わらない） | （同） |

- この図面（新規の空図面・縮尺 1:1 生まれ）では、デザインレイヤへの `SetLayerScaleN` は
  読み戻しに反映された（下の「レイヤの縮尺（`SetLayerScaleN` の書き込みの実測）」の 1:1 の新規図面とは
  別の結果）。

### OS タイマーの刻みの中・図面 0 枚

プローブは本体をピン留めし、`SetTimer`（50 ms）を仕掛けてメニューから戻り、**主窓が
操作できる状態になってから**刻みの中で同じ列挙をした。

- 刻みの中の列は、メニューの中で最後に辿った列とアドレスまで含めて一致した。
  `ForEachLayer` も同じ 6 件。刻みの中で undo イベントは開いていなかった。
- 刻みの中で `CloseDocument()` で全部閉じた後（件数 0）、`GetDrawingHeader()` は `nullptr`、
  `VWDocument::GetDrawingHeaderFristMember()` も `nullptr`、`GetCurrentLayer()` も `nullptr`、
  `ForEachLayer` は 1 度もコールバックせずに戻った。落ちなかった。
- 刻みの中で `OpenDocumentPath(nullptr, false)` を呼ぶと `true` で新規が開き（件数 1）、
  次の刻みでその図面の列（レイヤ-1 ＋ 末尾の型 0）が取れた。この間、次の刻みまで
  30 秒空き、その間 VW は応答なし（`Responding=False`）だった。メニューやブリッジから
  開いたときにはこの待ちは見ていない。

### `objects`（直下の数）について

列で辿った各レイヤの `FirstMemberObj` → `NextObject` の数は、何も描いていない
レイヤでも `1` だった（ビューポートを 1 つ置いたシートで `2`）。その 1 つの型は `0`
（`layer-guest` の再走で確かめた）。

## レイヤの高さ（読み書きの 4 経路と、絶対Z の決まり方）

[issue #194](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/194) で
**実機確認済み**（VW 2026 / mac。`probes/runtime/ipzl-layer-elevation/` と
`probes/runtime/layer-elevation-absolute-z/` を新規の空図面で 1 回ずつ。以下の値は
実行ログそのまま）。

### 4 つの書く口を試した

レイヤ高さを 4 通りで与え、**5 経路で読み戻して突き合わせた**（数値はすべて互いに違う）。

| 与え方 | `VWLayerObj::GetElevation` | `ovLayerHeightInCurrUnits`(157) | バウンド経路 | `GetHeight` / `ov158`(厚さ) |
| --- | --- | --- | --- | --- |
| `VWLayerObj::SetElevation(872)` | 872 | 872 | 872 | 0 |
| `SetObjectVariable(h, 157, 1234)` | 1234 | 1234 | 1234 | 0 |
| VS `SetLayerElevation(h, 1500, 2400)` | 1500 | 1500 | 1500 | **2400** |
| VS `SetLayerElevationN(h, 1600, 2400)` | 1600 | 1600 | 1600 | **2400** |
| ストーリのレベル（階 2699・階内相対Z −40） | **2659** | 2659 | 2659 | 0 |

- ヘッダの `ovLayerHeightInCurrUnits`(157) のコメントは "current units, **the base elevation
  height of the layer**"（`Kernel/API/ObjectVariables.h`）。
- SDK 同梱の `VWLayerObj.cpp` を読むと、`VWLayerObj::SetElevation` の実体は
  `GS_Kludge(gCBP, 1002, …)`、`GetElevation` は同 `1000`、`VWLayerObj::SetHeight` は
  `GS_Kludge` 1003。図面に届いたことは下記の絶対Z で確かめた。
- VS の 2 つは `IVectorScriptEngine::ExecuteScript` 経由で呼んだ（`ISDK` に厚さまで書く口は
  ヘッダに見当たらなかった）。`SetLayerElevation` と
  `SetLayerElevationN` はこの舞台では同じ振る舞いだった。
- `eStoryObjectBound_LayerWallHeight` を解かせると、高さ 1500 ＋ 厚さ 2400 のレイヤで **3900** が
  返った。
- ストーリ由来のレイヤ: 2699 − 40 = **2659**。

### オブジェクトの絶対Z の実測

データタグの `#IPZ#`（絶対Z）で測った。

| 段 | 何をしたか | `#IPZ#`（絶対Z） |
| --- | --- | --- |
| P | 高さ 0 のレイヤで部材を作る | `0` |
| P | **そのレイヤへ後から** 800 を与える | **`800`**（追随した） |
| Q | 最初から高さ 900 のレイヤで部材を作る | **`900`**（作成時も乗る） |
| R | Q の部材を**高さ 1100 のレイヤへ `AddObjectToContainer`** | **`900` のまま** |
| R | さらに移した先を 1300 にする | **`900` のまま**（追随しない） |
| T | **ストーリ由来のレイヤ**（2659）で部材を作る | **`2659`**（素のレイヤと同じ） |
| U | そのレイヤへ後から `SetElevation(5000)` | **`5000`**（追随した） |

- 段 R の「`900` のまま」は、**VW へ戻った後に読み直しても同じ**だった
  （[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。部材の挿入点 Z も 900 のまま、`#IPZL#` は −400＝900−1300）。
  undo イベントを閉じても、戻って数十秒置いても追随しなかった。
- 段 R で移した直後（移した先が 1100 のとき）、移した先のレイヤの面から見た高さは −200
  （900 − 1100）だった。
- 段 T で `#IPZS#` は **`-40`** だった（2659 − 2699）。

### ストーリに属するレイヤへ `SetElevation` を掛けた実測

段 U の実測。ストーリ由来のレイヤ（階 2699 ＋ 相対Z −40 ＝ 2659）へ
`SetElevation(5000)` を掛けると:

| 読んだもの | 値 |
| --- | --- |
| レイヤの高さ（4 経路とも） | **5000** |
| 部材の絶対Z（`#IPZ#`） | **5000**（図面ごと動いた） |
| `GetStoryElevation`（階の高さ） | **2699**（変わらない） |
| `GetStoryLevelElevation`（階内相対Z） | **−40 のまま**（更新されない） |

（この表は **VW へ戻った後に読み直しても同じ**だった——[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。階 6000・相対Z −40・
レイヤ 5000・`#IPZS#` −1000 の組で測り直し、undo イベントを閉じた後も戻った後も動かない。）

このとき `#IPZS#` は **2301**（＝5000 − 2699）を返した。

## レイヤの縮尺（`SetLayerScaleN` の書き込みの実測）

- 使った口: 読み `GetLayerScaleN(h, double_gs&)`・書き `SetLayerScaleN(h, double)`（どちらも
  戻り値は `void`）。
- [#145](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/145) の
  プローブを 2 つの文書で走らせた（同じ 1 本のコード・同じビルド）:

  | 走らせた文書のデザインレイヤ | 走らせた回数 | `SetLayerScaleN(layer, 50)` の後に読み戻した縮尺 |
  | --- | --- | --- |
  | 縮尺 **1/100** で始まった文書 | **3 回** | **1/50**（毎回効いた。**3 回のログは数値が 1 桁も違わない**） |
  | 縮尺 **1:1** で始まった新規の空図面 | 1 回 | **1/1**（**効かなかった**。以後に作った寸法も 1:1 生まれのままだった） |

- ビューポートの縮尺 `ovViewportScale`(1003) へは、`SetObjectVariable` が `true` を返し、
  読み戻しも追随した（[Dimensions](../Findings/Dimensions.md)）。

## 階（ストーリ）を SDK から作った手順と実測（レベルテンプレート経由）

動いている実装はホームズ君プラグインの `draw/Story`（`createStories` /
`CreateStoryLevelViaTemplate`）で、この手順で階を作っている。手順そのものは Findings にある。
各手順で観察したこと:

- `CreateLayerLevelType` などの `Create*` の引数は非 const `TXString&` で、名前付き lvalue を
  渡した。
- `CreateStory(name, suffix)` の戻り値は `bool` でハンドルは返らない。レベルの無い階は
  `ForEachLayerN` ＋ `GetStoryOfLayer` では見つからず、`GetNamedObject(name)`（VS の `GetObject`
  相当）では引けた。
- テンプレート経由（`CreateStoryLevelTemplate(name, scale, levelType, offset, wallHeight, index)` ＋
  `AddStoryLevelFromTemplate(story, index)`）で生やしたレイヤは、名前に `CreateStory` の suffix が
  付いた（`1-FL` が `1-FL-1`）。

試して効かなかったもの（すべて実測）:

- **`AddStoryLevel` ＋ `AssociateLayerWithStory`**（VW 2026）: UI 上、レイヤ→レベルの紐付けが
  「なし」になった（`CreateStoryLevelTemplate` ＋ `AddStoryLevelFromTemplate` の側では紐付いた）。
  `GetNamedObject` で引いた階のハンドルへ `AddStoryLevel` を渡すと `true` が
  返るのに、`GetLayerForStory` は **nil** のままだった。
- **`CreateStoryLayerTemplate`**: `true` を返し件数も増えたが、その後に `CreateStory` しても
  レイヤは生えなかった。
- **`ResetDefaultStoryLevels(false)`**: `true` を返したが、レベルの無い階にレイヤは生えなかった。
- `CreateStoryWithUI()` はヘッダではハンドルを返す宣言で、ダイアログが出る（走らせていない）。

> **【訂正の記録】** 最初は `AddStoryLevel` と `CreateStoryLayerTemplate`（Layer 系）だけを試し、
> どちらも `true` を返すのにレイヤが生えないのを見て「SDK だけでは階を用意できない」と書いた
> （[PR #66](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/66)）。
> その後、ホームズ君プラグインに `Story Level` 系のテンプレートで階を作る実装が既にあり、
> それで生えることを確かめた。

### レベルを持たないレイヤを階に結ぶ試み（仮のレベル種別を与えて結び、外す）

[issue #221](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/221) で
**実機確認済み**（VW 2026 / mac。新規の空図面で 1 回ずつ。以下の値は実行ログそのまま）。
1 周目（`story-only-layer`）は素のレイヤを結ぼうとして全部失敗し、2 周目
（`story-layer-associate`）で**呼ぶ順を入れ替えたら通った**。どちらも、レベルテンプレート
経由のレベル付きレイヤ（相対Z −40）を比較の基準に置いて測った。3 周目（`story-layer-recipe` /
`story-layer-reopen`）で実際の使い方に回した。プローブは役目を終えたので消してある。

#### 2 周目——未使用のレベル種別を先に与えて結び、種別を外す

2 周目で呼んだ順:

1. `CreateLayer(name, kLayerDesign)` で素のデザインレイヤを作る。
2. `SetLayerLevelType(layer, 仮の種別)`——その階でまだ使われていない種別（`CreateLayerLevelType`
   で先に登録）を与える。
3. `AssociateLayerWithStory(layer, story)` → **`true`**。`GetStoryOfLayer` がその階を返し、
   `GetLayerForStory(階, 仮の種別)` もそのレイヤを返した。
4. `SetLayerLevelType(layer, "")` → **`true`**。`GetStoryOfLayer` はその階のままで、
   レベル種別だけ `''` になった。`GetLayerForStory(階, 仮の種別)` は nil に戻った。

実測（2 周目。`試験1階`=1000 / `試験2階`=4000、基準のレベル付きレイヤは `試験天端`）:

| 試したこと | `Associate…` の戻り値 | 読み戻し（階・種別・レイヤ高さ） |
| --- | --- | --- |
| **素のレイヤ**を結ぶ（1 周目。レベル付きの階・レベルの無い階・別の階で計 5 回） | **`false`** | 階 nil・種別 `''`・高さ 0 |
| **未使用の種別**（`試験柱`）を先に与えた P を `試験1階` へ | **`true`** | **`試験1階`**・`試験柱`・**1000** |
| **その階で使用中の種別**（`試験天端`）を先に与えた Q を `試験1階` へ | **`false`** | 階 nil・`試験天端`・0 |
| P と**同じ種別**（`試験柱`）を先に与えた R を `試験1階` へ | **`false`** | 階 nil・`試験柱`・0 |
| **別の未使用の種別**（`試験柱2`）を先に与えた S を `試験1階` へ | **`true`** | **`試験1階`**・`試験柱2`・1800（階を動かした後） |
| 結べた P へ `SetLayerLevelType(P, "")` | （setter は `true`） | **`試験1階`**・**`''`**・1800 |

- `vs.py` の `AssociateLayerWithStory` の説明は「その階に同じレベル種別のレイヤが既に在ると
  失敗する」。1 周目はレベルを 1 枚も持たない階へ種別 `''` のレイヤを結んでも `false` だった。
- `Associate…` が `true` の 3 回は全部結ばれ、`false` の回は全部結ばれていなかった。
  `SetLayerLevelType` は、階に結ばれていないレイヤにも、その階で使用中の種別にも `true` を返した
  （種別の欄に入るだけで、階の側からは見えない。1 周目の D）。
- 結んだ直後の P の高さは 1000（＝階の高さ。レベル付きのレイヤは 960）。仮の種別の
  `GetStoryLevelElevation` は 0。種別を外した後も高さは階の高さ（1800）で、`SetStoryElevation` で
  階を 1000 → 1800 へ動かすと P も追随した。
- P の上の構造材に、下端 `{0, 試験天端}`・上端 `{1, 試験天端}` を書くと、下端 960・上端 3960
  （差 3000）で、基準のレベル付きレイヤと同値だった。階を 1800 へ動かして `ResetObject` すると
  **1760 / 3960**（基準も同じ）。種別を外してからも 1760 / 3960 のままだった。
- `GetStoryObjectDataBoundHeight`（入れ物＝P）も自階 960・上階 3960 で基準と一致した。

#### 同じ階に同じレベル種別のレイヤは 2 枚置けるか

`AddStoryLevelFromTemplate` を同じ階へ 2 回呼ぶと、2 回とも `true` で、同じ種別
（`試験天端`）のレイヤが 2 枚その階に属した（`試験天端雛形-t1` と `-t3`。どちらも
高さ 1760）。`AssociateLayerWithStory` では同じ種別の 2 枚目は `false` だった（上の R）。
テンプレート経由の 2 枚目で `GetLayerForStory(階, 種別)` がどちらを返すかは決まらない。

#### 実際の使い方で回した（仮の種別 1 つで複数枚を結ぶ）

3 周目（`story-layer-recipe`）。階 3 つ（1000 / 4000 / 7000）に `試験天端` のレベルを生やし、
仮の種別 `試験仮` **1 つだけ**で上の手順を 3 枚に回した。

| レイヤ | 結ぶ先 | `Associate…` | 手順の後（階・種別・高さ） |
| --- | --- | --- | --- |
| X | 試験1階 | `true` | 試験1階・`''`・1000 |
| Y（**同じ階の 2 枚目・同じ仮の種別**） | 試験1階 | **`true`** | 試験1階・`''`・1000 |
| Z | 試験2階 | `true` | 試験2階・`''`・4000 |

- `GetLayerForStory(階, 仮)` は外した直後に nil へ戻り、同じ階の 2 枚目も同じ仮の種別で結べた。
- X と Y（どちらも種別 `''`）が同じ階に同居した。`GetLayerForStory(試験1階, '')` は X を返した。
- UI（オーガナイザ → デザインレイヤ）では「ストーリ」欄に階、「ストーリレベル」欄は空だった
  （ユーザーが目視。X＝試験1階、Y・Z＝試験2階〔Y は下の結び直しの後〕）。

**バウンドと高さ**（構造材の下端 `{0, 試験天端}`・上端 `{1, 試験天端}`）:

| 入れ物 | 結んだ直後の解決Z（下端 / 上端） | 1階→1800・2階→4500 へ動かし `ResetObject` 後 | レイヤ高さ（動かした後） |
| --- | --- | --- | --- |
| 基準（レベル付き・1階） | 960 / 3960 | 1760 / 4460 | 1760 |
| X（1階・種別なし） | **960 / 3960** | **1760 / 4460** | **1800**（＝階の高さ） |
| Y（1階・種別なし） | **960 / 3960** | **1760 / 4460** | 1800 |
| Z（2階・種別なし） | **3960 / 6960** | **4460 / 6960** | 4500 |

- X は `ResetObject` を**掛ける前から** 1760 / 4460 に追随していた。
- 構造材の挿入点Z はバウンドの解決値（1760）で、レイヤ高さ（1800）とは 40 違った。

**並び**:

- 種別なしで階に結んだレイヤは、作った順のまま、ストーリ従属レイヤ（レベル付き）の**背面**へ
  付いた（前面から t1 1760・t2 4460・t3 6960・X 1800・Y 1800・Z 4500）。
- `InsertObjectAfter` で X を最背面へ、Z を `試験天端雛形-t2` の直後（t2 と t3 の間）へ
  移せた（`true` で、読み戻しの並びもそのとおり）。

**ビューポートの表示レイヤ**: 断面ビューポートへ `SetViewportLayerVisibility(…, 0)` は
全デザインレイヤで `true`、X / Y / Z の読み戻しも `表示=0`（レベル付きのレイヤと同じ）。

**別の階へ結び直す**: 種別 `''` のまま `AssociateLayerWithStory(Y, 試験2階)` は **`false`**
（Y は試験1階に残った）。同じ手順（仮の種別 → 結ぶ → 外す）をもう一度回すと結び直せた
（試験2階・高さ 4500・構造材も 4460 / 6960 へ追随。試験1階の件数は 1 減った）。

**保存して開き直した後**（`story-layer-reopen`。`story-layer-recipe` を走らせた図面を
保存し、閉じて開き直してから読んだ）:

| レイヤ | 階 | 種別 | 高さ | 構造材の解決Z（開いたまま → `ResetObject` 後） |
| --- | --- | --- | --- | --- |
| 試験天端雛形-t1（基準） | 試験1階 | `試験天端` | 1760 | 1760 / 4460 → 1760 / 4460 |
| 試験-柱X | **試験1階** | **`''`** | 1800 | **1760 / 4460** → 同じ |
| 試験-柱Y（2階へ結び直したもの） | **試験2階** | **`''`** | 4500 | **4460 / 6960** → 同じ |
| 試験-柱Z | **試験2階** | **`''`** | 4500 | **4460 / 6960** → 同じ |

- 階・種別・高さ・並び（`InsertObjectAfter` で動かした順）・バウンド
  （`fBound`=2〔`_Story`〕・`fBoundStory`=1・`試験天端`）が、すべて保存前のまま残った。
- 開き直した図面では、`GetNamedObject` でデザインレイヤが引けなかった（X / Y / Z も、
  レベル付きの t1 も **nil**。同じ図面で階 `試験1階` などは引けた。⌘Z の後の図面でも nil）。
  開き直す前の図面で引けたかは測っていない。

**断面ビューポート**（ユーザーが目視。開き直した図面の断面ビューポートで、
断面線を材が映る位置へ動かして更新した）: 柱 4 本が映り、基準と X が下の段、Y と Z が
その上の段に並んだ（解決Z 1760〜4460 / 4460〜6960 の位置）。プローブが作ったままの位置
（断面線 y=−500）では、`1064=false` / `1065=true`・隠線消去にしても基準の材も含めて空だった。

**undo（⌘Z）**（新規図面で `story-layer-recipe` を走らせた直後に ⌘Z を 1 回。オーガナイザの目視と、
そのあと `story-layer-reopen` で読み戻した値。プローブは自分では undo イベントを開かないので、
ここで戻ったのは VW が自動で記録した分）:

| 見たもの | ⌘Z の後 |
| --- | --- |
| 階（`試験1階`〜`3階`） | **消えた**（`GetNamedObject` で見つからない。オーガナイザの「ストーリ」欄も全レイヤで空） |
| デザインレイヤ（X / Y / Z も t1〜t3 も） | **全部残った** |
| レイヤの階 | **全部 nil**（レベル付きの t1〜t3 も） |
| レベル種別 | t1〜t3 は `試験天端` のまま・X / Y / Z は `''` のまま |
| レイヤ高さ | X・Y 1000 / Z 4000 / t1 960 / t2 3960 / t3 6960——**階を動かす前（U3 の前）の値** |
| 並び | `InsertObjectAfter` で動かした後の順のまま |
| 構造材 | **全部消えた**（どのレイヤにもストーリバウンドを持つ図形が無い） |

#### プラグイン自身の undo イベントの中で回して取り消した実測

[issue #223](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/223) で
**実機確認済み**（VW 2026 / mac。新規の空図面。以下の値は実行ログそのまま）。イベントの開き方は
import-ifc の `draw::ImportUndoScope` と同じ——`SetUndoMethod(kUndoSwapObjects)` ＋
`NameUndoEvent` で開き、**作ったレイヤを `AddAfterSwapObject` で登録**し（構造材へ渡すパスも
登録）、`EndUndoEvent` で閉じる。その中で、レベル種別 2 つ（天端・仮）・階 2 つ（1000 / 4000）・
レベルテンプレート 1 つ（相対Z −40）を作り、テンプレート経由のレベル付きのレイヤ（各階）と、
上の手順のレベル無しのレイヤ X（1 階）を置いて、1 階のレベル付きのレイヤと X に構造材を
1 本ずつ載せた。取り消しは `DoMenuName("Undo", 0)`（⌘Z と同じコマンド。
[Plug-in Interop](../Findings/Plug-in%20Interop.md) の 2）。プローブ（`story-layer-undo` の V1・V2 と、
2 回走らせる `story-layer-undo-later` の V3）は役目を終えたので消してある。

| 見たもの | 閉じる前 | 全部登録（V1・V3） | **X だけ登録しない**（V2） |
| --- | --- | --- | --- |
| 階（`GetNumStories` / 名前で引く） | 2 / 在る | **0 / 無い** | **0 / 無い** |
| レベル種別（天端・仮とも） | 在る（件数 7→9） | **無い**（件数 7） | **無い**（件数 7） |
| レベルテンプレート | 在る（件数 7→8） | **無い**（件数 7） | **無い**（件数 7） |
| クラス（`AddClass`。V3 だけ） | 在る | **無い** | — |
| レベル付きのレイヤ（1 階・2 階） | 在る | **無い** | **無い** |
| レベル無しのレイヤ X | 1 階・`''`・高さ 1000 | **無い** | **在る**: 階 **nil**・種別 `''`・高さ **1000** |
| X の上の構造材 | 在る | （X ごと無い） | **消えた**（X に残った子は型 90・90・0） |

- V3 は 1 回目のコマンドで作って閉じて返り、**別のコマンド**から取り消した。この回は 1 段目の
  取り消しでは何も戻らず、**2 段目で上の表のとおり全部消えた**。間に MCP ブリッジの
  **別名保存**（`SaveActiveDocumentPath`）が入っていた
  （[Undo](../Findings/Undo.md)「別名保存は取り消しの段を 1 つ積む」）。

#### 階に属さないレイヤの上での `eStoryObjectBound_Story` の実測

同じバウンド（`fLayerLevelType` = `試験天端`）を、入れ物のレイヤだけ変えて
`GetStoryObjectDataBoundHeight` に解かせた（1 周目）:

| 入れ物のレイヤ | レイヤ高さ | 自階(`fBoundStory`=0) | 上階(1) | 下階(−1) |
| --- | --- | --- | --- | --- |
| 基準（レベル付き・1階） | 960 | **960** | **3960** | 960 |
| A（素のレイヤ・1階へ結べなかった） | 0 | **0** | **0** | **0** |
| B（同上） | 0 | **0** | **0** | **0** |
| C（同上・3階のつもり） | 0 | **0** | **0** | **0** |

- 階に属さないレイヤ上の構造材は、両端とも `0`、`ResetObject` 後の差 `0` だった（基準のほうは
  960 / 3960 で差 3000）。
- レベル無しのレイヤへ `SetElevation(2500)` を掛けてから解かせ直すと、下端も上端も `2500`
  になった（階の高さは 1800 のまま）。
- `GetObjectBoundElevation` はこのとき `true` も例外も返さずにこの値を返した。

#### 階に属さないレイヤで、ほかに差が出たもの・出なかったもの

| 見たもの | レベル付きのレイヤ | 階に属さないレイヤ |
| --- | --- | --- |
| 階を動かしたとき（`SetStoryElevation` 1000→1800） | レイヤ高さが 960 → **1760** へ追随 | 無関係（0 のまま） |
| デザインレイヤの並び | **高さの降順**（1760 / 3960 / 6960） | **作った順**で、ストーリ従属レイヤの**背面**へ |
| `InsertObjectAfter` での並べ替え | — | **効く**（A を最背面へ移せた） |
| `SetViewportLayerVisibility` / `GetViewportLayerVisibility`（断面 VP） | `true` / 表示=0 | **`true` / 表示=0（差なし）** |
| `DeleteObject` | — | **消える。** 階もその階のレベル付きレイヤも残る |

- 並びには高さを持たせた階に属さないレイヤも混ざっていた——高さ 2500 の B が高さ 0 の
  A / C より前に並んだ（どれもストーリ従属レイヤの背面）。

### レベルの高さの読みの実測（SDK で作ったレベルを含む）

[issue #141](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/141) で
**実機確認済み**（VW 2026 / mac。新規の空図面で、上の手順を 4 通りに変えてストーリを作り、
3 つの口で読み戻した。以下は実行ログそのまま）。

| ストーリの作り方 | `GetStoryElevation` | `GetStoryLevelElevation` | 解決した絶対Z |
| --- | --- | --- | --- |
| 上の手順そのまま（`elevationOffset` = 0） | 2800 | **0** | **2800** |
| ＋ あとから `SetStoryLevelElevation(story, 種別, 300)` | 5600 | **300** | **5900** |
| `SetStoryElevation` を**レベルを足した後**に呼ぶ | 8400 | **0** | **8400** |
| テンプレートの `elevationOffset` = 100 | 11200 | **100** | **11300** |

- 表の右端は `GetStoryObjectDataBoundHeight` に解かせた値（`fBound = eStoryObjectBound_Story` ＋
  `fBoundStory = 0` ＋ レベル種別。解き方は
  [Parametric Objects](../Findings/Parametric%20Objects.md)「階やレベルを指すバウンド」）。
- `elevationOffset` に 100 を渡すと相対Z は 100 だった。
- `SetStoryLevelElevation` に 300 を渡すと相対Z が 300・絶対Z が 5900 になった。
- `SetStoryElevation` をレベルを足した後に呼んでも絶対Z は 8400 だった。
- #141 の出発点は、`GetStoryLevelElevation` の `0` を見て「SDK で作ったレベルは高さを
  持っていない」と見当を付けたことだった。絶対Z を読むと高さは入っていた。

UI で作ったストーリでも同じプローブを走らせた（実物件の図面。ストーリ 基礎 / 1階 / 2階 / 屋根、
レベル種別 15 個）:

| ストーリ | 階の高さ | レベル種別 | 相対Z | 絶対Z |
| --- | --- | --- | --- | --- |
| 基礎 | 0 | `GL` / `床束`・`底盤天端` / `基礎天端` | 0 / 50 / 400 | 0 / 50 / 400 |
| 1階 | 612 | `FL` / 他 4 つ | 0 / **−40** | 612 / 572 |
| 2階 | 3571 | `FL` / 他 5 つ | 0 / **−40** | 3571 / 3531 |
| 屋根 | 6374 | `軒高` ほか 5 つとも | 0 | 6374 |

### レベル基準線が描く高さの実測

**同じ 1 回の実行で「描かれた数値」と「レベルの絶対Z」と「レイヤの高さ」を並べて測った**
（実物件の図面。3 つ組を書いた個体を置いて読み、最後に消した）:

| ストーリ / レベル種別 | レベルの絶対Z | レイヤの高さ | **描かれた数値** | 描かれた名前 |
| --- | --- | --- | --- | --- |
| 1階 / `FL` | 561 | 561 | **561** | `FL-1` |
| 2階 / `登り梁` | 3325 | 3325 | **3325** | `登り梁-2` |
| 屋根 / `軒高` | 5961 | 5961 | **5961** | `軒高-R` |
| 基礎 / `底盤天端` | 50 | 50 | **50** | `底盤天端-F` |

- 新規の空図面（SDK で作ったストーリ）でも、デザインレイヤへ置いた個体もシートレイヤへ置いた
  個体も絶対Z を描いた（`2800` / `5900` / `8400` / `11300`）。
- この表のレイヤの高さは、`eStoryObjectBound_LayerElevation` のバウンドをそのレイヤを入れ物にして
  `GetStoryObjectDataBoundHeight` に解かせて読んだ。
- 【訂正の記録】当時は「VS の `GetLayerElevation` に当たる ISDK の口は無い」と書いていた。後に
  `VWLayerObj::GetElevation` と `ovLayerHeightInCurrUnits`(157) が同じ値を返すことを測った
  （上記「レイヤの高さ」。3 経路とも一致）。

> **【突き合わせの誤りの記録】**「描かれた数値が絶対Z と一致しない」と一度書いた
> （`FL-1` が 612 に対して 576 など）。別々のファイルのログを見比べていた——一方は 1階=612、
> もう一方は 1階=561 の図面で、数値はどちらもその図面では正しかった。

### 注釈に高さが出るかをビューポートごとに見た

実物件の図面で、同じレベル（1階 / `耐力壁`、絶対Z `540`）の 3 つ組を**その文書のビューポート
29 件すべて**の注釈へ 1 本ずつ置いて読んだ実測（置いた個体は全部消した）:

| ビューポート | `ovIsSectionViewport` | 表示レイヤ | **描かれた数値** |
| --- | --- | --- | --- |
| `Viewport-1`〜`6`（UI 製の伏図） | **false**（平面） | 5〜8 / 37 | **`0`** |
| `Viewport-7`〜`29`（UI 製の断面・軸組） | **true**（断面） | 24 / 37 | **`540`** |
| SDK の `CreateSectionViewport` 製・作った直後 | **true**（断面） | 0 / 38 | **`0`** |
| SDK の `CreateSectionViewport` 製・**全レイヤを表示にした後** | **true**（断面） | **38 / 38** | **`0`** |

- 平面ビューポートでは `ovIsSectionViewport`（オブジェクト変数 1054）が `false` だった。
- SDK 製の断面では、`SetViewportLayerVisibility` が 38 枚とも `true` を返し、読み戻しも 38 枚とも
  表示だったが、数値は `0` のままだった。このときビュー行列（`ovViewportViewMatrix` ＝ 1050）は
  単位行列のままだった。
- 名前はどのビューポートでも出た（`耐力壁-1` のような「レベル種別-ストーリの接尾語」）。

> **【訂正の記録】** 上の「全レイヤを表示にしても `0`」は、クラスが非表示のままだった
> （[#151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)
> で 2 × 2 に振って実測。表示レイヤだけ／クラスだけでは 1050 は入らず、**両方**を表示へ
> 戻して更新したときだけ 1055 と同じ値が入った）。#147 の「1055 を 1050 へ写す」手順は
> 使わずに入った——手順と実測値は [Viewports](../Findings/Viewports.md)「SDK で作った断面
> ビューポートの注釈でレベル基準線に高さを出す」にある。

**試した筋（どれも数値は `0` のまま）:**

- **ストーリの作り方**——4 通り試してどれも高さは正しく入っていた（上記）。
- **レイヤの表示（だけ）**——書き込みは読み戻しで効いていたが `0` のまま。全レイヤを
  表示にして更新し、置いてある個体を `ResetObject` しても、新しく置いても、はじめから
  表示にして作った 2 つ目のビューポートでも `0`。（後に #151 でクラスも表示へ戻すと
  1050 が入り、高さが出た。上の訂正の記録。）
- **断面ビューポートとして成立していないのでは**——`ovIsSectionViewport` は `true` だった。
- **作り方の引数**——`depth` / 高さの範囲 / 断面線の位置と向きを 4 通り振っても `0`（#147）。
- **断面線オブジェクトとの結び付き**——オブジェクト変数 1000〜1150 の総当りで
  **ハンドル型の差は 0 件**（#147）。

## 打ち切った調査: ビューポート単位のレイヤ重ね順上書き

- 書き込み: `SetViewportLayerStackingOverride` は 2 度の試行とも毎回 true を返したが、
  `GetNumViewportLayerStackingOverrides` は 0 のままで、OIP も「順序を上書き: いいえ」だった。
- 読み出し: GUI で「順序を上書き」を付けたビューポートを
  `GetNumViewportLayerStackingOverrides` / `GetViewportLayerStackingOverrideLayer` /
  `GetViewportLayerStackingOverride` で読むと 27 件が返り、レイヤ名も引けた:

  ```
  共通=1  2-柱伏図記号=2 … 1-FL=17 … R-野地板=25  2-FL=26  2-野地板=27
  ```

  （位置は 1 始まり・**1 が最前面**・図面の全デザインレイヤに振ってあった。）

実機で試した書き方は次の 3 通りで、いずれも記録 0 件:

| 位置の基点・向き | 対象 | 呼ぶ時点 | 結果 |
| --- | --- | --- | --- |
| 0 始まり・0 が最背面 | その図に映るレイヤだけ（5 件） | 生成直後 | 記録 0 件 |
| 1 始まり・1 が最前面 | 全デザインレイヤ（27 件） | 生成直後 | 記録 0 件 |
| 1 始まり・1 が最前面 | 全デザインレイヤ（27 件） | **仕上げ・更新の後** | 記録 0 件 |

同時に当たったもの:

- `CreateViewportLayerOverride` はヘッダでは**色・不透明度**の上書きの族（`SViewportLayerOverride` は
  `fFillFore` / `fPenFore` / `fPercentOpacity`）。重ね順の族には `Create…` が無く、`Set…` だけが
  ある。
- **レイヤの索引**: ビューポート側の API はレイヤをハンドルではなく `InternalIndex`
  で指す（`VWViewportObj::SetLayerVisibility` も索引。ハンドルを取るのは
  `ISDK::SetViewportLayerVisibility` だけ）。`GetObjectInternalIndex(レイヤ)` がその索引で
  あることは、読み出した索引からレイヤ名が全件引けたこと＋
  `VWViewportObj::GetLayerVisibility(索引, …)` が読めることの両方で確認した。
- **オブジェクト変数**: `ObjectVariables.h` を `stack` / `override` で全文検索して該当なし
  （ヒットは `ovDimWitnessOverride` と `ovViewportLightingDevice` だけ）。ビューポートの変数は
  1030〜1079 に並ぶが重ね順は無い。`ovSheetSaveStackLayers` / `ovSheetStackLayersStatus` は
  保存ビューの「レイヤを重ねる」のもの。
- **VectorScript**: `vs.py` に `Stacking` のヒット無し。
