# 図面ラベル（`Drawing Label2`）

シートレイヤのビューポートに図面タイトル・縮尺・図番を出す PIO を、SDK から置いて
ラベルレイアウトを per-instance で組むときの結論（[issue #149](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/149)・
#175・#177・#179・#245・#249）。VW 2026 / mac・日本語版で確認。実測の表・ダンプ・切り分けは
[計測記録](../Metrics/Drawing%20Labels.md)。

## 先に結論

- **図面タイトルを出すだけなら、レイアウトに触らず `Title` を書く。** 注釈へ入れればリンクは
  勝手に張られるが、**置いた直後の `Title` は当てにならない**——ラベルは**作った瞬間に選択
  されているビューポートの 1032 を持って生まれ**、`CreateViewport` は作った VP を選択した
  まま残すので、**前に作った別の VP の題を拾う**（下記「[生まれたときの `Title` は…](#生まれたときの-title-は選択中のビューポートの-1032249-で確定)」）。
- **`Title` が空のラベルは下線しか描かず、注釈へ入れると VP の 1032 まで消す。**
  `CreateViewport` で作りたての VP は 1032 が空なので、**空なら自分の文字列を書く**。

  ```cpp
  gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);  // 作る前に 1 度

  MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", WorldPt(x, y), 0.0, false);
  gSDK->AddViewportAnnotationObject(hViewport, h);   // 注釈へ入れる（GetViewportGroup は使わない）

  // ビューポートの図面タイトルは ovViewportDescription（1032）。そこから読んで書き写す。
  TVariableBlock block;
  TXString        title;
  gSDK->GetObjectVariable(hViewport, 1032, block);
  block.GetTXString(title);

  // **空なら自分の文字列を入れる。** CreateViewport で作りたてなら 1032 は空で、
  // 空の Title のまま置くとラベルは下線しか描かず、ビューポートの 1032 まで消える。
  // **書かずに済ませない**——ラベルは選択中の（＝前に作った別の）VP の題を持って生まれ、
  // それをこの VP の 1032 へ押し込む（#249）。
  if (title.IsEmpty())
  	title = "出したい図面タイトル";

  VWParametricObj(h).SetParamValue("Title", title);  // Link State が 0 でも 1 でも効く
  gSDK->ResetObject(h);
  ```
- **図番（`Drawing`）は 1 ビューポートに 1 本ずつ置くかぎり SDK 任せでよい**（`1,2,3,…`・
  重複しない）。**同じ注釈に 2 本以上入れると先頭のラベルが図番を持てなくなる**
  （下記「[図番（`Drawing`）の自動採番](#図番drawingの自動採番179-で確定)」）。
- **図番と縮尺を消すには、レイアウトから落とす**（表示を切るパラメータは無い）。
  **要るものだけを複製**して新しい群へ入れ、渡し直す（下記「SDK から組む手順」）。
- **スタイルは勝手に当たる。** 外すなら**組み直した後に** `gSDK->SetPluginObjectStyle(h, 0)`
  （下記「[スタイルは勝手に当たる](#スタイルは勝手に当たる外し方は-setpluginobjectstyleh-0-だけ)」）。
- **レイアウトのテキストに与える大きさは「紙の上の mm」。** 容れ物の縮尺は VW が掛ける。
  紙で 10pt なら `10 * 25.4 / 72 = 3.5278`。**寸法（`ovDimFontSize`）とは逆**
  （下記「[レイアウトの文字の大きさ](#レイアウトの文字の大きさ与えるのは紙の上の-mm)」）。

## どのオブジェクトか

| | |
| --- | --- |
| universal 名 | `Drawing Label2` |
| 内部 ID / スタイルのサブタイプ | **642**（`kInternalID_DrawingLabel2`。[Parametric Objects](Parametric%20Objects.md) / [Symbols](Symbols.md)） |
| 旧版 | `kInternalID_DrawingLabel = 96`（`Drawing Label`。VW 2026 の既定は 2 のほう） |

- **SDK に `DrawingLabel` という名前の API は 1 つも無い**【ヘッダ根拠】。専用の口は無く、
  **PIO のパラメータとプロファイルグループだけで組む**。

## パラメータ（16 件）

一覧（欄型・作った直後の値・表示名）は [計測記録](../Metrics/Drawing%20Labels.md#パラメータ16-件)。

- 要の欄は **`Title`**（図面タイトル）・**`Drawing`**（図番。作るたびに VW が自動で入れる）・
  **`Link State`**（`0` / `1`）。
- **「図番を出さない」「引出線を出さない」ための真偽値の欄は無い。** 出す / 出さないは
  **レイアウトに何を置くか**で決まる。`Length` はタイトルの下線の長さで、引出線の欄ではない。

## ラベルレイアウトはプロファイルグループにある

- **レイアウトの実体はラベル自身のプロファイルグループ**（`GetCustomObjectProfileGroup` /
  `SetCustomObjectProfileGroup`）。[Data Tags](Data%20Tags.md)・[Level Objects](Level%20Objects.md)
  と同じ作り。**`…InAux` は `nil`**。
- 既定はテキスト「タイトル」・下線・テキスト「縮尺」・図番の丸（円弧）・テキスト「#」
  （[ダンプ](../Metrics/Drawing%20Labels.md#ラベルレイアウトはプロファイルグループにある)）。
  **テキストに名前もレコードも付いていない。**
- ラベルの描いた図形を走査すると **型 90（`kUndoPlaceholderNode`）が混ざる**ことがある。数えない。

## 動的な文字列の式——**データタグと同じ綴り**だが、**書き込む口が無い**

- 式の綴りは**データタグと同じ `#レコード#.#フィールド#`**（`#Drawing Label2#.#Title#` など。
  `#Drawing Label2#.#Scale#` の `Scale` は 16 件に無い計算欄）。
- **保存先が違う**: 式はテキストにぶら下がるユーザーデータ（型 76・容れ物 ID `'oidl'`）に
  UTF-16 で入っている。**`IDataTagTextLinkSupport` は使えない**（`IsSupported` が `false`）ので
  **`SetFormula` で式を持たせられない**（[実測](../Metrics/Drawing%20Labels.md#動的な文字列の式データタグと同じ綴りだが書き込む口が無い)）。
- **`IDataTagSupport::UpdateUserDefinedTextsUIDs` は要らない**（呼ばずに組んで正しく描いた）。

## SDK から組む手順——**複製して、要らないものを落とす**

- **新しく作ったテキストは式を持たない**（`CreateTextBlock` の「タイトル」は文字どおり描く）。
  **既定のテキストを `DuplicateObject` で複製したもの**は、文字を `SetText` で潰しても `Title` を
  描く——**効いているのはテキストが抱える隠れた状態**（[実測](../Metrics/Drawing%20Labels.md#sdk-から組む手順複製して要らないものを落とす)）。
- だから**要るものだけを複製して新しい群へ入れ、`SetCustomObjectProfileGroup` で渡し直し、
  `ResetObject`**。タイトル＋下線だけを残せば縮尺も図番も丸も消える（コードは計測記録）。
- **中身を入れてから渡す**（空の群を先に渡すと迷子）。**中身の入れ替えだけでは絵に出ない**——
  新しい群を作って渡し直す（[Data Tags](Data%20Tags.md)・[Level Objects](Level%20Objects.md) と同じ筋）。

## スタイルは勝手に当たる——外し方は `SetPluginObjectStyle(h, 0)` だけ

（#175。[実測](../Metrics/Drawing%20Labels.md#スタイルは勝手に当たる外し方は-setpluginobjectstyleh-0-だけ)）

- **`CreateCustomObject` の直後に、ツールにいま設定されているスタイルが当たっている。**
  新規の空図面でも既定（`ref=179`「図面ラベル - 図番」）が文書に入っている。
- **外すのは `gSDK->SetPluginObjectStyle(h, 0)`**（`ResetObject` も `UpdateViewport` も越えて残る）。
  **`SetPluginStyleForTool(tool, 0)` にしてから作っても**スタイル無しで生まれる（下記の理由で勧めない）。
- **`VWParametricObj::SetStyle(0)` では外せない**（実機で無反応。【ソース根拠】実装が
  `InternalIndexToHandle(0)` が `nullptr` だと何もしない。`VWParametricObj.cpp:1703`）。
- **PIO 一般向けの「スタイル無しにする」口は無い**（`ConvertToUnstyledWall` / `…Slab` /
  `…Roof` の 3 つだけ。【ヘッダ根拠】）。サブタイプは **642**。スタイルを自分で用意する手順は
  [Parametric Objects](Parametric%20Objects.md)「スタイルは SDK だけで作れる」。

## **既定のレイアウトはスタイルが持っている**——だから外す順序が決まる

- **スタイル無しで作ったラベルはレイアウトが空（`型 17 0`）で何も描かない**（外接 0×0）。
  **外してもインスタンスのレイアウトは残る**（[実測](../Metrics/Drawing%20Labels.md#既定のレイアウトはスタイルが持っているだから外す順序が決まる)）。
- したがって順序は **①スタイルが当たった状態で作る → ②レイアウトを複製で組み直す（文字の
  大きさもここで）→ ③`SetPluginObjectStyle(h, 0)` → `ResetObject`**。
  ツールのスタイルを 0 にして作るだけで済ませない（レイアウトが空のまま、何も描かない
  ラベルになる）。

  ```cpp
  // 1. **スタイルが当たった状態で作る**（既定のレイアウトはスタイルが持っているので、
  //    ツールのスタイルを 0 にしてから作ると複製する元が無くなる）。
  MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", WorldPt(x, y), 0.0, false);

  // 2. レイアウトを組み直す（下記「SDK から組む手順」。複製でしか組めない）。
  //    ここで文字の大きさも当てる（下記「レイアウトの文字の大きさ」）。

  // 3. **組み直した後に外す。** レイアウトはインスタンスに残る。
  gSDK->SetPluginObjectStyle(h, 0);
  gSDK->ResetObject(h);
  ```

- スタイルは「作るときに既定のレイアウトを配る」役で、当たり続けている必要は無い。

## レイアウトの文字の大きさ——**与えるのは「紙の上の mm」**

（#175。同じ注釈の 6pt の寸法を物差しにした。[実測](../Metrics/Drawing%20Labels.md#レイアウトの文字の大きさ与えるのは紙の上の-mm)）

- **`SetTextSize` で与えた値がそのまま紙の上の mm になる**——ビューポートの注釈（1/50・1/100）・
  デザインレイヤ（1/50）・シートレイヤ直下（1:1）のどれでも紙で 10.00000pt。
  **自分で縮尺を掛けると紙で 500pt になる。** 同じ図面でも**寸法とラベルで作法が逆**
  （[Dimensions](Dimensions.md)「注釈へ寸法を置くときの作り方」）。
  ```cpp
  // 紙で 10pt にしたいなら、ラベルの置き場がどこであってもこれ 1 つ。
  const WorldCoord paperMm = 10.0 * 25.4 / 72.0;      // 3.5278
  gSDK->SetTextSize(hDupText, 0, gSDK->GetTextLength(hDupText), paperMm);
  ```

- **`ISDK::SetTextSize` の第 4 引数は `WorldCoord`＝ mm で、pt ではない**（`10` は 28.3pt。#175 の発端）。
- **掛けるのは容れ物で、アクティブレイヤは効かない**（`SetTextStyleRef` のときだけは効く。すぐ下）。
- **組み直しの時機（注釈へ入れる前／後）は効かなかった**が、比べた 2 走行がどちらも縮尺の
  掛かっていない側で、「掛かった側でも同じ」とまでは言えていない。**先に組んでから入れる**のを勧める。
- **VP の縮尺を後から変えても紙の見え方は保たれる。** `ScaleFactor` は更に掛かる（2 で 20pt）。
  `World-based`=True は大きさを変えない。既定のタイトルは `6.0`（紙で 6mm）。

### 文字スタイル（`SetTextStyleRef`）を使うなら、当てるときのアクティブレイヤを 1:1 にする

- 文字スタイルを当てると、**その場で「紙の大きさ × 当てたときのアクティブレイヤの縮尺」が
  焼き付き**、ラベルが更に容れ物の縮尺を掛ける（1/50 をアクティブにしたままだと紙で 500pt）
  （[実測](../Metrics/Drawing%20Labels.md#文字スタイルsettextstylerefを使うなら当てるときのアクティブレイヤを-11-にする)）。
- **`SetTextSize` で直に書くほうを勧める**（外の状態に依らない）。[Dimensions](Dimensions.md) の
  〈クラスの文字スタイル〉と同じ筋。

## ビューポートとの紐づき

- **図面タイトルはビューポートの `ovViewportDescription`（1032）が持ち、ラベルが読む**
  （1033 ＝ `ovViewportLocator` は Item 欄。【ヘッダ根拠】`ObjectVariables.h`）。
- **注釈へ置くと `Link State`=1** で、VP のタイトルを変えるとホストの VP に追随する。シート
  レイヤ直下なら `0` で追随しない（[実測](../Metrics/Drawing%20Labels.md#ビューポートとの紐づき)）。
- **`SetParamValue("Title", …)` は `Link State` が 0 でも 1 でも効く。** ただしリンクは生きて
  いるので、後で VP のタイトルが変わると上書きされる。**手で書いた値を守るならシートレイヤ直下へ。**
- **注釈へ入れる口は `AddViewportAnnotationObject`。** 作りたての VP では
  `GetViewportGroup(vp, kViewportGroupAnnotation)` が `nil`（入れた後は取れる）。
- **注釈のラベルは VP のクラス表示を戻さないと絵に出ない**（`CreateViewport` の VP は全クラス
  非表示。[Viewports](Viewports.md)「クラス表示」）。`Title` の問題と取り違えない——外接が 0 かで分かる。

### **1 つのビューポートの注釈に、図面ラベルは 1 本だけ**（VW 自身の警告）

- **複数置いた注釈を開くと VW が警告を出す**（「自動作図調整は、1 つのビューポートに複数の図面
  ラベルがあると正常に機能しません」。[文言](../Metrics/Drawing%20Labels.md#1-つのビューポートの注釈に図面ラベルは-1-本だけvw-自身の警告)）。
  置くこと自体は成功する。
- **SDK から置くときも 1 VP 1 本にする。** 図番の乱れはこれが原因（#179 で確定）。`Title` の
  話はこれに左右されない。

### 置いたときに流れるのは「ラベル → ビューポート」（#177 で確定）

- **`Link State` が 0 → 1 に変わる瞬間、押し込まれるのはラベルの `Title`**。VP の 1032 が
  降りてくるのではない（[実測](../Metrics/Drawing%20Labels.md#置いたときに流れるのはラベル--ビューポート177-で確定)）。
- **`Title` が空のラベルを注釈へ置くと、VP の図面タイトルが消える。** 1032 から写す意味が
  あるのは、そこに既に値が入っているとき（人が作った VP など）だけ。
- **注釈へ入れた後・`ResetObject` の前に `Title` を書けばよい。**

### 生まれたときの `Title` は、選択中のビューポートの 1032（#249 で確定）

（[#249](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/249)。[状態ごとの表](../Metrics/Drawing%20Labels.md#生まれたときの-title-は選択中のビューポートの-1032249-で確定)）

- **`CreateCustomObject` の直後の `Title` は、その瞬間に選択されている VP の 1032**（複数なら
  並びの先のもの。その場で読む）。VP の選択が無ければ空。前のラベルからは引き継がない。
- **`CreateViewport` は作った VP を選択したまま残し、それまでの選択も外さない**ので、続けて
  作ると**最初の VP の題**で生まれ、注釈へ入れるとそれが押し込まれる。
- デザインレイヤがアクティブだと**文書名**が入る。図面ラベルがシートレイヤ以外では
  文書（シート）の名前を題として拾う作りだからと見ている【推定】。
- `DeselectAll()` してから作ってもよいが、それは「空で生まれる」に揃うだけで、空のまま
  置けば 1032 を潰す。
- **#177 と #245 で結果が逆になったのは作った順の違い**で、図面の出どころ（テンプレート）は
  関係しなかった。**結論: `Title` は必ず自分で書く。**

## 図番（`Drawing`）の自動採番（#179 で確定）

（[issue #179](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/179)。
**SDK に採番の口は無い**【ヘッダ根拠】。[実測](../Metrics/Drawing%20Labels.md#図番drawingの自動採番179-で確定)）

### 先に結論

- **1 つの VP の注釈に 1 本だけなら、図番は `1, 2, 3, …` と増えて重複しない——SDK 任せでよい。**
  自分で `SetParamValue("Drawing", …)` を書いた値も残る（[VW 自身の警告](#1-つのビューポートの注釈に図面ラベルは-1-本だけvw-自身の警告)
  に従う置き方）。
- **同じ注釈に 2 本以上入れると、並び順の先頭のラベルは図番を持てない**——書いた値ごと、
  その注釈へ最後に入れたラベルの図番に乗っ取られる。どう書いても守れない。

### 規則 1: 作った瞬間に入る値は「**使われていない最小の正整数**」

- **そのシートレイヤで図番として使われている正整数のうち、空いている最小のもの**。`max + 1`
  ではない（空いた番号を埋める・`100` を書いても次は `1`）。**数として読めない図番
  （`A-01`）は数えない。採番の単位はシートレイヤ**（[実測](../Metrics/Drawing%20Labels.md#規則-1-作った瞬間に入る値は使われていない最小の正整数)）。
- **`bInsert=false` のまま放置したラベルは採番から見えず、続けて作ると全部 `1`。** 注釈へ
  入れるならこの差は消える。
- **`varUseAutoDrawCoord`（544）は無関係**（`false` でも採番された）。**`IAutomaticNumbering` も
  別物**（既存を振り直すダイアログ前提の口）。

### 規則 2: 注釈の**先頭のラベル**は、最後に入れたラベルの図番を映す

- **注釈の並び順の先頭は、最後に入れたラベルの図番を映す**。先頭が持っていた番号が空き、
  規則 1 で次がそこへ落ちる——**これが「番号が戻る」の正体**（[実測](../Metrics/Drawing%20Labels.md#規則-2-注釈の先頭のラベルは最後に入れたラベルの図番を映す)）。
- **先頭以外は自分の値を保つ。** 乗っ取りは入れた瞬間（`AddViewportAnnotationObject` →
  `ResetObject`）に起きる。**先頭を消しても、次に先頭になった本が同じ役を負う。**
- VW へ戻った後に読み直しても同じ（#245）。**一時的な状態ではない。**

#### 自分で書いた図番も、先頭では守れない

- 先頭へ書いた値（文字列でも数字でも）は、2 本目を入れた時点で消える。注釈に 1 本しか
  無ければ残る（[実測](../Metrics/Drawing%20Labels.md#自分で書いた図番も先頭では守れない)）。

#### 1 ビューポートに 1 本ずつなら重複しない（実測）

- VP 5 枚に 1 本ずつで `1`〜`5`、書いた `U-1`〜`U-5` も `UpdateViewport` を越えて残った
  （[実測](../Metrics/Drawing%20Labels.md#1-ビューポートに-1-本ずつなら重複しない実測)）。

### #177 の 11 本は、この 2 つの規則で全部説明できる

- #177 の `1 2 1 3 4 5 4 6 4 7 4` は規則 1 ＋規則 2 で 1 本の例外も無く再現できる。
  **乱数でもバグでもない**（[なぞった表](../Metrics/Drawing%20Labels.md#177-の-11-本はこの-2-つの規則で全部説明できる)）。

## `Title` が空だと、ラベルは文字を描かない（#177 で確定）

- **`Title` が空なら、ラベルは下線 1 本だけを描き文字を 1 文字も描かない**（外接の高さ `0`）。
  置いた本数・容れ物・VP の 1032・スタイルの有無は関係せず、**`Title` だけで決まる**
  （[実測](../Metrics/Drawing%20Labels.md#title-が空だとラベルは文字を描かない177-で確定)）。
- **どんなビューポートへ置くかはこの現象に関係しない**——`Title` は `CreateCustomObject` した
  時点で決まり、容れ物とは無関係なので、既存のビューポートでも防ぎ方は同じ（＝`Title` を書く）。
  実プラグインがこれを踏んでいないのは最初から `Title` を自分で書いているからで、容れ物が
  既存だからではない。
- **縮尺は 1 本目から正しく掛かる**（`Title` があれば紙で 10.00000pt）。
- **【訂正】#177 の「作りたての VP の注釈では先頭の 1〜2 本が縮尺を拾わない」は誤り。**
  文字が描かれておらず、`FirstMemberObj` で辿るとレイアウト側の値（`3.52778`）を拾っていた。
  **外接の高さを併せて読めば割れた**（[調査の作法](Investigation%20Techniques.md)「描かれている文字を測る」）。

### 測り直し——「描くまで計算が遅れていたのでは」は無い

- **`ResetObject` はその場で計算する。** クラスを表示して `UpdateViewport` で描かせた後も、
  undo イベントを閉じた後・VW へ戻って数十秒後も、`Title`・外接・描いた文字・1032 は
  1 桁も変わらなかった（#245。[実測](../Metrics/Drawing%20Labels.md#測り直し描くまで計算が遅れていたのではは無い)）。
  **描かせていない状態で測った値を信用してよい。**

## データタグとの違い（まとめ）

| | データタグ（`Data Tag`） | 図面ラベル（`Drawing Label2`） |
| --- | --- | --- |
| レイアウトの置き場 | プロファイルグループ | **同じ**（`…InAux` は `nil`） |
| 式の綴り | `#レコード#.#フィールド#` | **同じ**（`#Drawing Label2#.#Title#`） |
| 式の読み書き | `IDataTagTextLinkSupport`（`SetIsLinked` / `SetFormula`） | **使えない**（`IsSupported`=`false`）。式はユーザーデータの中 |
| レイアウトの作り方 | テキストを**作って**式を持たせる | **既定のテキストを複製**して要らないものを落とす |
| `UpdateUserDefinedTextsUIDs` | 要る | **要らない** |
| 本文の出どころ | 関連付けた図形のレコード | ビューポートの `ovViewportDescription`(1032)。`Title` へ直接書いてもよい |

## 未確認のまま残っているもの

- **ユーザーデータに式を書き込んで、新しいテキストに欄を持たせられるか。** 容れ物 ID
  （`'oidl'`）と、中に UTF-16 の式が入っていることまでは割れているが、書き込みは試して
  いない（[Tagged Data](Tagged%20Data.md) のとおり読み出し API は当てにならないので、
  書くなら生バイトの構造を先に割る必要がある）。**複製で足りている**ため追っていない。
  必要になったら別の issue で。
