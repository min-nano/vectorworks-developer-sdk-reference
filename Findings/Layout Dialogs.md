# レイアウトダイアログ（`VWDialog`）

自前のレイアウトダイアログ（`VWFC::VWUI::VWDialog`）を組むときの知見（VW 2026 / macOS）。
「ふだんは短い本文・困ったときはログ全文」のような 1 枚ダイアログを作った過程で確定した。
失敗した手・SDK の実装ソース・実測のログは [計測記録](../Metrics/Layout%20Dialogs.md)。

- **`gSDK->AlertInform` では本文を出すことしかできない。** 追加のコントロール（ログ欄・
  ボタン）が要るなら自前のレイアウトダイアログを組むしかない。
- **複数行のコピーできるテキスト欄は `VWEditTextCtrl`。**
  `CreateControl(dlg, text, widthInStdChar, heightInLines)` で複数行の編集欄になり、
  **スクロールでき・選択してコピーできる**。静的テキストではコピーできない。
- **本文は 1 行 1 コントロールで積む。** `VWStaticTextCtrl` は埋め込んだ改行がそのまま行に
  なる保証が無い。空行は `AddBelowControl` の行間で表す。
- **レイアウトダイアログの大きさは、作るときに 1 度だけ決まる。** `ShowControl(id, false)`
  で隠しても縮まず、最初から隠しても高さは空いたまま。`Get/SetDialogSize` で押し込むのも
  安定しない。**大きさを変えたいなら作り直す**——折り畳みは**一方通行で開く**（作り直し
  1 回）形にする（[実測](../Metrics/Layout%20Dialogs.md#レイアウトダイアログの大きさと横幅)）。
- **横幅は「いちばん長い行」で決まり、作った後では縮められない。** 長い情報は見出しごとに
  行を分け、ブランチ名のように青天井で伸びる文字列は詰める（実装例:
  `plugin/src/ProbeMenuText.h` の `StampLine` / `Ellipsize`）。プルダウンの幅は
  `CreateControl(dlg, widthInStdChar)` の標準文字数で決まるので、項目の実際の長さに合わせる。
- **作り直すときは位置を引き継ぐ**（`Get/SetDialogPosition`）。ハンドラの中からモーダルを
  閉じるのは `VWDialog::SetDialogClose(bCloseWithOK)`（protected）。
- **ボタン行（OK のある行）へコントロールを足す API は無い。** OK の隣にボタンを出したい
  なら**キャンセルのボタンに別の名前を付ける**のが唯一の手（その行は `GS_CreateLayout` が
  `CreateDialog(title, ok, cancel, hasHelp)` の引数から作る。押されたことは
  `OnCancelButtonEvent` で分かる。Esc も同じ扱いになる）。
- **ボタンのクリックは `EVENT_DISPATCH_MAP` で受ける。** `ADD_DISPATCH_EVENT(id, f)` の
  `f` は `void f(TControlID, VWDialogEventArgs&)`（`VWFC/VWUI/DialogEventArgs.h` の
  `CDialogEventHandlers`）。`CreateDialog(title, "OK", "", false)` の
  ように**キャンセルを空文字**にすると OK だけのダイアログになる。
- **出せなかったときの逃げ道を必ず持つ。** レイアウトを組めなければ `AlertInform` へ落とす。

## サムネイル付きのリソース選択（画像ポップアップ）

VectorWorks 本体の「鋼材断面を選択」のような**サムネイル付きのリソース選択**を自前の
レイアウトダイアログで出す話。**使うクラスを間違えると永久に作れない。**

### `VWImagePopupCtrl` では作れない（SDK の実装が空のスタブ）

- **`VWImagePopupCtrl::CreateControl(VWDialog*)` は実機で必ず false を返す。** SDK 同梱の
  実装（`ImagePopupCtrl.cpp`）が、生成をコメントアウトしたまま `return false` するスタブ
  （[ソース](../Metrics/Layout%20Dialogs.md#vwimagepopupctrl-では作れないsdk-の実装が空のスタブ)）。
  呼ぶ順序・`SetAdvanced()`・`DialogImagePopup_Init` では直らない。

### 代わりに `VWThumbnailPopupCtrl` を使う（実機確認済み）

- **同じ種別（`eCompThumbnailPopup`）の `VWThumbnailPopupCtrl` は実装が生きていて、実機で
  シンボルのサムネイルが並ぶポップアップが出る。** 手順は `fList.BuildList(kSymDefNode, true)`
  → `fThumb.CreateControl(this, kStandardSize)` → `AddImageFromResource(fList.GetListID(), i)`
  （[最小の手順と実測](../Metrics/Layout%20Dialogs.md#代わりに-vwthumbnailpopupctrl-を使う実機確認済み)）。
- **項目は `AddImageFromResource` の順に並び、添字は一覧の添字と一致する**（0 始まり）。
- **選択は添字でもリソースでも引ける。** `GetSelectedItemIndex()` は添字、`GetSelectedItem()`
  は `InternalIndex`（`InternalIndexToHandle` / `InternalIndexToNameN` で引ける）。一覧を
  作り直す作りなら `InternalIndex` 側で持つ。
- **「未選択」は読み取れない——項目を足した時点で先頭（`0`）が選ばれている。** 必要なら
  選択イベントが来たかを自分で覚える。
- **選択イベントは `selected`（`IsImagePopupSelected()`）として届く。**
  `SetImagePopupResourceAdvanced` を立てると開く直前の `IsImagePopupBeforeOpen()` も届く。
- **DDX では受けられない**（`AddDDX_ImagePopup` は `PullDownResourceLayout` 系専用）。
- **大きさは `kStandardSize = 0` / `kLineTypeSize = 1` の 2 種類だけ**。描画モード・視点は
  指定できない。
- **リソース一覧（`VWResourceList`）はダイアログのメンバに置く。** 参照カウント式で、
  ローカル変数にすると抜けた時点でリスト ID が無効になる。

### `VWImagePopupCtrl` の機能（カテゴリ・区切り）がどうしても要るとき【推定】

- `SetCategories` / `AddItemSeparator` / `ShowImage` は `VWImagePopupCtrl` にしかない。
  **生成を `gSDK->CreateCustomThumbnailPopup` で行い、基底の
  `VWControl::CreateControl(this)` を明示的に呼ぶ**抜け道は文法上成立する（`compile` で
  確認。**実機未確認**。[コード](../Metrics/Layout%20Dialogs.md#vwimagepopupctrl-の機能カテゴリ区切りがどうしても要るとき推定)）。
  まず `VWThumbnailPopupCtrl` で足りるかを確かめる。

### 一覧には「ユーザが置くシンボル」以外も混ざる

- `BuildList(kSymDefNode, true)` は**図面のシンボル定義を全部**返し、PIO のスタイルとして
  持たれているシンボル定義（図面ラベル・図面枠など）が混ざる。**切り分けは
  `gSDK->GetSymbolDefSubType(hSymDef)`**（0 なら普通のシンボル定義）。詳細は
  [`Symbols.md`](Symbols.md) の「一覧には『ユーザが置く部品』以外も混ざる」。

### 代わりの道（実機で確認済み）

サムネイル付きポップアップが使えなくても、**名前のプルダウン（`VWPullDownMenuCtrl`）
＋選択中の絵を出す `VWSymbolDisplayCtrl`** の組み合わせで機能は成立する（絵は選んだ
後にしか出ず、VectorWorks 本体のポップアップとは見た目が異なる二段構えになる点だけ
違う）。シンボル定義一覧の採り方と `VWSymbolDisplayCtrl` の使い方は
[`Symbols.md`](Symbols.md) の「シンボル定義のサムネイル・一覧を UI に出す」を参照。

## モードレス（非モーダル）なパレット（`IExtensionWebPalette`）

（issue [#19](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/19)）
「図面を操作したまま開いていられるウィンドウを出せるか」の調査。**口はある。**
`VWFC::VWUI::VWDialog::RunDialogLayout` はモーダルだが、SDK には **`IExtensionWebPalette`（基底クラスは
`VWFC::PluginSupport::VWExtensionWebPalette`）というモードレスのパレットの拡張種別**がある。

**この節は全体が【ヘッダ根拠】＋ CI の `compile`（構文チェックのみ）までで、
実機（VectorWorks 本体）では未確認。** 実機で開閉・入力・イベント到達を確かめてから
この注記を外すこと。

**パレットの HTML から周期的に C++ を呼ぶ話は
[周期実行と通知](Timers%20and%20Notifications.md)へ移した**——`setInterval` は**パレットを
隠すと 60 秒に 1 回まで間引かれる**（実測）ので、受け付けの時計をパレットの中に置くと
止まる。SDK 側にそれを止める口は無い。

### 見つかった場所

- `VWFC/PluginSupport/VWExtensionWebPalette.h`（基底クラスとディスパッチマクロ）・
  `Interfaces/VectorWorks/Extension/IExtensionWebPalette.h`（VCOM インターフェースと
  `GROUPID_ExtensionWebPalettes`）・`ISDK.h` の `Get/SetWebPaletteVisibility` /
  `GetWebPaletteFrame`（[一覧](../Metrics/Layout%20Dialogs.md#見つかった場所)）。
- **Web Palette は公式リファレンス（[Plug-in Module](../Info/Plug-in%20Module.md#extensions)）
  未掲載の 5 つ目の拡張種別**で、`GROUPID_ExtensionMenu` などと同じ並びで定義されている。
  登録は [Menu General Info](../Info/Menu%20General%20Info.md#register-menu-extension) の
  `REGISTER_Extension<…>` と同型で、group ID とクラスを差し替えるだけのはず（未確認）。

### 1. モードレスに出す口はある

- **`gSDK->SetWebPaletteVisibility(iid, true)` は `RunDialogLayout` と違って呼び出しをブロックしない
  `bool` の setter**
  で、「開いたまま制御を返す」設計そのもの。**中身は HTML/JS**（`GetInitialURL()` の URL を
  埋め込みブラウザで表示。既定は
  `VWFC::PluginSupport::GetStandardURL(htmlFolderName = "html", htmlFile = "index.html")`
  が組み立てる＝プラグインの `.vwr` の `html/index.html`）で、`VWDialog` のようにネイティブ
  コントロールを並べる作りではない【ヘッダ根拠】。

### 2. 入力は JS 経由で受け取る

- **入力欄やボタンは HTML 側に置く。** JS から C++ へは
  `VWExtensionPaletteJSProvider::OnFunctionCall(objName, functionName, args, context)`
  （`args` は `std::vector<nlohmann::json>`）で届き、`DEFINE_WebPalette_DISPATCH_MAP` 系の
  マクロで**関数名の文字列で分岐する**【ヘッダ根拠】。

### 3. `DoInterface` が戻った後も生きるはず【推定】

- `SetWebPaletteVisibility` / `GetWebPaletteFrame` は `gSDK` の生の関数なので、**別の
  メニューコマンドの `DoInterface` からでも同じ `iid` で同じパレットに触れる**はず
  （[Plug-in Module#Extensions](../Info/Plug-in%20Module.md#extensions)）。開いたまま図面を
  操作できるか・戻った後もイベントが届くかは実機でしか確かめられない。

### 4. 殻と本体の割り方【推定・要実機確認】——おそらく PIO と同じ制約

- **拡張の登録（`VWExtensionWebPalette` の継承クラス・`IWebCallbacksProvider` の実装）は
  殻に置くべき**——VW に番地を握られる側なので、本体を降ろすと次の `OnFunctionCall` で
  落ちる見込み（[Plug-in Modules「落とし穴: 殻の記憶域を本体に持たせると落ちる」](Plug-in%20Modules.md)）。
- **HTML/JS は `.vwr` のリソースフォルダから読むのが既定**（`.vwr` は殻にしか置けない。
  [Plug-in Modules](Plug-in%20Modules.md)）。HTML/JS だけの書き換えで済むかは未確認。
- **`OnFunctionCall` 以降の実処理は、PIO の `Recalculate()` と同じく本体へ委譲できる見込み**
  ——`args` をその場で写してから C の ABI 越しに渡す（[Plug-in Modules
  の決めごと](Plug-in%20Modules.md)）。
- **明示的に閉じる／登録解除する API はヘッダに見つからない。** `IWebPaletteFrame` には
  `Reload` / `LoadURL` / `ExecuteJavaScript` / `SetTitle` / `Focus` しか無く、できるのは
  `SetWebPaletteVisibility(iid, false)` で**隠す**ことだけ。登録解除は `plugin_main` の
  `action` で行う設計だと推測されるが未確認。

### 構文チェック（CI, `compile`）

- 基底 2 クラスの継承・ディスパッチマクロ・`gSDK->{Get,Set}WebPaletteVisibility` /
  `GetWebPaletteFrame` を呼ぶ最小の階層は **`-fsyntax-only` を通った**。
  `REGISTER_Extension` の呼び出しは構文チェックしていない
  （[記録](../Metrics/Layout%20Dialogs.md#構文チェックci-compile)）。

### まだ分かっていないこと（実機確認が必要）

- 実際に**モードレスに開き、図面を操作しながら見える**か。
- `DoInterface` が戻った後、**本当に `OnFunctionCall` が届く**か。
- 拡張の登録（≒本体では持てない側）を**本体に置くと実際に落ちる**のか
  （PIO と同型の話だが、Web Palette 自体での実機確認はまだ無い）。
- **登録解除・アンロード時の `action` の扱い**——降ろす前に安全に手放せるか。
- `.vwr` の外（本体側）に HTML/JS を置けるか、それとも `GetStandardURL` の既定どおり
  殻の `.vwr` の中に置くしかないか。
