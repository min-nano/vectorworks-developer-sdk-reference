# レイアウトダイアログ（`VWDialog`） — 計測記録

結論は [Findings「Layout Dialogs」](../Findings/Layout%20Dialogs.md)。ここには実機で試して
失敗した手・SDK 同梱の実装ソースの抜き書き・サムネイル付きポップアップの実測・
Web Palette のヘッダ調査と構文チェックの記録を置く。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

自前のレイアウトダイアログ（`VWFC::VWUI::VWDialog`）を組むときの実測。
「ふだんは短い本文・困ったときはログ全文」のような 1 枚ダイアログを作った過程で測った。
環境は VW 2026 / macOS。

## レイアウトダイアログの大きさと横幅

- **大きさ**: 折り畳みを `ShowControl(id, false)` でやろうとして 2 度失敗した（いずれも
  実機）: 大きなコントロールを出すとダイアログは大きくなったが**隠しても縮まなかった**。
  `OnInitializeContent` で最初から隠しておいても**その分の高さは空いたまま**だった。
  `Get/SetDialogSize`（`GS_Get/SetLayoutDialogSize`。単位はピクセル、`ViewCoord` は
  `Sint16`）で押し込むと安定しなかった。大きなコントロールを持たない状態で作り直した
  ダイアログは、VW が計算した大きさが最初から正しかった。
- **横幅**: 静的テキストを 1 行にまとめると、その行の長さがそのままダイアログの幅になった。
  実機確認プラグインのピッカーで、素性（ブランチ名・コミット・ビルド ID・時刻）を
  `カタログ: … / 殻: …` と 1 行につないだところ、**プルダウンの倍以上に横へ伸びた
  ダイアログ**になった（VW 2026 / macOS）。プルダウンの幅は `CreateControl(dlg,
  widthInStdChar)` の**標準文字数**で決まり、広く取ると、その幅までダイアログが広がった。

## サムネイル付きのリソース選択（画像ポップアップ）

VectorWorks 本体の「鋼材断面を選択」のような**サムネイル付きのリソース選択**を、自前の
レイアウトダイアログ（`VWFC::VWUI::VWDialog` を継承し `CreateDialogLayout()` を自分で
書く形）で出す話。

### `VWImagePopupCtrl` では作れない（SDK の実装が空のスタブ）

`VWFC::VWUI::VWImagePopupCtrl::CreateControl(VWDialog*)` は**実機（VW 2026 / macOS）で
false を返した**。SDK に同梱された実装ソースを読むと、**生成の呼び出しがコメントアウト
されたまま無条件に false を返している**。

```cpp
// SDKLib/Source/VWSDK/VWFC/VWUI/ImagePopupCtrl.cpp
bool VWImagePopupCtrl::CreateControl(VWDialog* pDlg)
{
	// gSDK->Create...
	//return VWControl::CreateControl( pDlg );
	pDlg;
	return false;
}
```

このソースには呼ぶ順序・`SetAdvanced()`・`VWResourceListCategorized::
DialogImagePopup_Init(dialog, id)` で分かれる道が無い。同じクラスの他のメソッド
（`AddItems` / `GetSelectedItemIndex` …）は実装されていて、どれも `CreateControl` が覚える
親ダイアログ（`fpParentDlg`）を使っている。

### 代わりに `VWThumbnailPopupCtrl` を使う（実機確認済み）

同じコンポーネント種別（`VWControlType::eCompThumbnailPopup`）を指す双子のクラス
`VWFC::VWUI::VWThumbnailPopupCtrl`（`VWFC/VWUI/ThumbnailPopupCtrl.h`）の実装を読んだ。
作りは `VWSymbolDisplayCtrl` と同型（`gSDK->Create…` でコントロールを作り、基底へ親を
覚えさせて返す）。

```cpp
// SDKLib/Source/VWSDK/VWFC/VWUI/ThumbnailPopupCtrl.cpp
bool VWThumbnailPopupCtrl::CreateControl(VWDialog* pDlg, ThumbnailSizeType sizeType /*= kStandardSize*/)
{
	gSDK->CreateCustomThumbnailPopup(pDlg->GetControlID(), fControlID, sizeType);
	return VWControl::CreateControl( pDlg );	// 親を覚えて true を返すだけ
}
```

`VWDialog` 側にも受け口があり（`GetThumbnailPopupCtrlByID(TControlID)`。未登録の ID なら
ラッパを作って親を結び付けて返す）。**実機（VW 2026 / macOS）で**次の手順でコントロールが
作られ、シンボルのサムネイルが並ぶポップアップが実際に出た。

```cpp
// ダイアログのメンバ: VWThumbnailPopupCtrl fThumb{ kThumbID }; VWResourceList fList;
bool CMyDialog::CreateDialogLayout()
{
	// 1) 図面のシンボル定義一覧（kSymDefNode = 16）を作る。
	//    （このリストはダイアログが生きている間持っていた。寿命は下の「リソース一覧の寿命」）
	fList.BuildList(kSymDefNode, /*sort*/ true);

	// 2) 生成 → 積む。他のコントロールと同じ扱い。
	fThumb.CreateControl(this, kStandardSize);
	this->AddBelowControl(&fCaption, &fThumb);

	// 3) 項目を入れる。添字は 0 始まり（内部で +1 されて 1 始まりの API へ渡る）。
	for (size_t i = 0, cnt = fList.GetNumItems(); i < cnt; ++i)
		fThumb.AddImageFromResource(fList.GetListID(), i);

	return true;
}
```

- **項目の並び**: 16 件のシンボル定義を `AddImageFromResource` で足すと `GetItemsCount()` も
  16 になり、2 番目を選んだときの `GetSelectedItemIndex()` は 1、その名前は一覧の 1 番目と
  同じだった。
- **選択の読み口**: `GetSelectedItemIndex()` は 0 始まりの添字、`GetSelectedItem()` は
  **選ばれたリソースの `InternalIndex`** を返す。`InternalIndex` からシンボル定義そのものへは
  `gSDK->InternalIndexToHandle(index)`、名前だけなら
  `gSDK->InternalIndexToNameN(index, outName)`（`ISDK.h`）。`GetItemObject(i)` / `GetObjectItemIndex(item)` で相互に引ける。
- **未選択**: `AddImageFromResource` を済ませた直後の `GetSelectedItemIndex()` は **0**
  （＝先頭）だった。ソースは `GS_GetImagePopupSelectedItem` の戻りから 1 を引くので、
  何も選ばれていなければ `(size_t)-1` になる書き方だが、実機では 0 が返った。
- **選択イベント**: `VWDialogEventArgs::IsImagePopupSelected()` の実体は
  `!fbNegativeControlID`（そのコントロール ID への通常のディスパッチイベント）。
  `gSDK->SetImagePopupResourceAdvanced(dialogID, controlID)` を立てると、これに加えて
  `IsImagePopupBeforeOpen()`（ポップアップを開く直前。負のコントロール ID で来る advanced
  イベント。`SImagePopupAdvancedMsgData` を伴う）も届いた。実機のログ例（1 回開いて 2 番目を
  選んだとき）:

  ```
  イベント: before-open   → GetSelectedItemIndex() = 0（先頭が選ばれている状態）
  イベント: selected      → GetSelectedItemIndex() = 1 / 'アンカーボルト_M16'
  ```

  選んだ値は、この `selected` で読んでも OK のときに読んでも同じ値が返った。
- **DDX**: `VWDialog::AddDDX_ImagePopup` の実装は
  `VWImagePopupCtrl::PullDownResourceLayout{Set,Get}SelectedResourceName` を呼ぶだけ
  （`PullDownResourceLayout` 系＝リソース名の文字列でやり取りする経路）。
- **大きさ**: `ThumbnailSizeType` は `short` の別名で、定数は `kStandardSize = 0` と
  `kLineTypeSize = 1`（`Kernel/API/MiniCadCallBacks.h`）。描画モード・視点を渡す引数は
  無い（`VWSymbolDisplayCtrl` にはある）。
- **リソース一覧の寿命**: `VWResourceList` は参照カウント式で、最後の 1 つが消えるときに
  `gSDK->DisposeResourceList(listID)` を呼ぶ（ソース）。

### `VWImagePopupCtrl` を基底の `CreateControl` で結ぶ形の構文チェック

カテゴリ切り替え（`SetCategories`）・区切り（`AddItemSeparator`）・`ShowImage` は
`VWImagePopupCtrl` にしかない。`VWControl::CreateControl(VWDialog*)` は
**public かつ非 virtual**（`VWImagePopupCtrl::CreateControl` は override ではなく
名前の隠蔽）。コントロールの生成を `gSDK` で行い、親の記憶だけを基底へ明示的に呼んで
任せる次の形は `compile` を通った（実機では走らせていない）。

```cpp
gSDK->CreateCustomThumbnailPopup(this->GetControlID(), kPopupID, kStandardSize);
popup.VWFC::VWUI::VWControl::CreateControl(this);	// ← 基底を明示呼び出し
popup.AddItems(fList);
```

両クラスとも中身は `(dialogID, controlID)` を使う `GS_*ImagePopup*` の薄い包みだった
（ソース）。

### 一覧には「ユーザが置くシンボル」以外も混ざる

`VWResourceList::BuildList(kSymDefNode, true)` は**図面にあるシンボル定義を全部**返す。
実測（16 件）の内訳には、ユーザが置く部品（`アンカーボルト_M12` / `床束` / `仕口` …）に
混じって、**VectorWorks 自身がプラグインオブジェクトのスタイルとして持っている
シンボル定義**が並んでいた——`図面ラベル - 図番`・`断面寸法`・`立断面指示線`・
`グリッド線`・`遠山信夫アトリエ一級建築士事務所`（図面枠）など。

`gSDK->GetSymbolDefSubType(hSymDef)` で分けた 16 件の表は
[Symbols の計測記録](Symbols.md#一覧にはユーザが置く部品以外も混ざるgetsymboldefsubtype-で分ける)。

## モードレス（非モーダル）なパレット（`IExtensionWebPalette`）

（issue [#19](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/19)）
この節はヘッダを読んだ記録と CI の `compile`（構文チェックのみ）の記録で、実機
（VectorWorks 本体）では走らせていない。

### 見つかった場所

- `Include/VWFC/PluginSupport/VWExtensionWebPalette.h` — SDK 基底クラス
  `VWExtensionWebPalette`（パレット本体）／`VWExtensionPaletteJSProvider`（JS からの
  呼び出しを受ける側）と、ディスパッチマクロ一式
  （`DEFINE_WebPalette_DISPATCH_MAP` / `BEGIN_WebPalette_DISPATCH_MAP` /
  `ADD_WebPalette_FUNCTION` / `END_WebPalette_DISPATCH_MAP(2)`）。
- `Include/Interfaces/VectorWorks/Extension/IExtensionWebPalette.h` — VCOM
  インターフェース `IExtensionWebPalette` / `IWebPaletteFrame` /
  `IWebCallbacksProvider`、グループ ID `GROUPID_ExtensionWebPalettes`。
- `Include/Interfaces/VectorWorks/ISDK.h` — `gSDK` に生えている
  `GetWebPaletteVisibility` / `SetWebPaletteVisibility` / `GetWebPaletteFrame`
  （`ISDK.h:3543-3544, 3800`）。

公式リファレンス（[Plug-in Module](../Info/Plug-in%20Module.md#extensions)）が挙げる
拡張は Menu / Parametric / Tool / VS Functions の 4 つだけ。
`GROUPID_ExtensionWebPalettes` は `GROUPID_ExtensionMenu` /
`GROUPID_ExtensionParametric` / `GROUPID_ExtensionTool` /
`GROUPID_ExtensionVSFunctions`（と `Animation` / `WSFunctionOptions`）と**まったく
同じ並びで定義されていた**（`grep -rnIE 'GROUPID_Extension[A-Za-z]*\s*='` で一括して
出てくる）。登録の枠組みは
`REGISTER_Extension<T>(groupID, action, moduleInfo, iid, ioInterface, cbp, reply)`、
[Menu General Info](../Info/Menu%20General%20Info.md#register-menu-extension) の用例は
`REGISTER_Extension<TesterModule::CExtMenu>(GROUPID_ExtensionMenu, action, pInfo, ioData, cbp, reply)`。

### 構文チェック（CI, `compile`）

`VWExtensionWebPalette` / `VWExtensionPaletteJSProvider` を継承し、ディスパッチ
マクロと `gSDK->{Get,Set}WebPaletteVisibility` / `GetWebPaletteFrame` を呼ぶ
最小のクラス階層は **`-fsyntax-only` を通った**（調査用スニペットはマージ前に削除
済み）。`REGISTER_Extension` の実際の呼び出し（`plugin_main` からの登録）は、この調査では
構文チェックしていない。
