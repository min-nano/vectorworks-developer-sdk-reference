# レイアウトダイアログ（`VWDialog`）

自前のレイアウトダイアログ（`VWFC::VWUI::VWDialog`）を組むときの実測。
「ふだんは短い本文・困ったときはログ全文」のような 1 枚ダイアログを作った過程で確定した。

- **`gSDK->AlertInform` では本文を出すことしかできない。** 追加のコントロール（ログ欄・
  ボタン）が要るなら自前のレイアウトダイアログを組むしかない。
- **複数行のコピーできるテキスト欄は `VWEditTextCtrl`。**
  `CreateControl(dlg, text, widthInStdChar, heightInLines)` で複数行の編集欄になり、
  **スクロールでき・選択してコピーできる**。静的テキストではコピーできず、報告に貼れない
  （編集できてしまうが、閉じるときに捨てるだけなら害は無い）。
- **本文は 1 行 1 コントロールで積む。** `VWStaticTextCtrl` は 1 行を出すためのもので、
  埋め込んだ改行がそのまま行になる保証が無い。本文を改行で切って静的テキストを並べ、
  空行は `AddBelowControl` の行間で表す。
- **レイアウトダイアログの大きさは、作るときに 1 度だけ決まる。** 折り畳みを
  `ShowControl(id, false)` でやろうとして 2 度失敗した（いずれも実機で確認）:
  大きなコントロールを出すとダイアログは大きくなるが**隠しても縮まない**し、
  `OnInitializeContent` で最初から隠しておいても**その分の高さは空いたまま**だった。
  `Get/SetDialogSize`（`GS_Get/SetLayoutDialogSize`。単位はピクセル、`ViewCoord` は
  `Sint16`）で押し込むのも安定しない。**大きさを変えたいなら、その状態のダイアログを
  作り直すしかない**——大きなコントロールを持たない状態は、VW が計算する大きさが最初から
  正しい。したがって「開閉できる」ように見せず、**一方通行で開く**（作り直し 1 回）の
  ほうが落ち着く。
- **横幅は「いちばん長い行」で決まる。** 静的テキストを 1 行にまとめると、その行の長さが
  そのままダイアログの幅になる。実機確認プラグインのピッカーで、素性（ブランチ名・
  コミット・ビルド ID・時刻）を `カタログ: … / 殻: …` と 1 行につないだところ、
  **プルダウンの倍以上に横へ伸びたダイアログ**になった（VW 2026 / macOS）。高さと違って
  横の間延びは目に付くうえ、**作った後では縮められない**（上記）。したがって
  **長い情報は見出しごとに行を分け、ブランチ名のように青天井で伸びる文字列は詰める**
  （実装例: `plugin/src/ProbeMenuText.h` の `StampLine` / `Ellipsize`。詰める長さは
  `plugin/tests/ProbeMenuTextTests.cpp` が見張っている）。プルダウンの幅は
  `CreateControl(dlg, widthInStdChar)` の**標準文字数**で決まるので、そちらは項目の実際の
  長さに合わせて決める（広く取ると、その幅までダイアログが広がる）。
- **作り直すときは位置を引き継ぐ**（`Get/SetDialogPosition`）——引き継がないと画面中央へ
  飛び、開き直したのが丸見えになる。ハンドラの中からモーダルを閉じるのは
  `VWDialog::SetDialogClose(bCloseWithOK)`（protected。押されたボタンを差し替える）。
- **ボタン行（OK のある行）へコントロールを足す API は無い。** その行は
  `GS_CreateLayout` が `CreateDialog(title, ok, cancel, hasHelp)` の引数から作る。
  OK の隣にボタンを出したいなら、**キャンセルのボタンに別の名前を付ける**のが唯一の手
  （押されたことは `OnCancelButtonEvent` で分かる。Esc も同じ扱いになる点だけ注意）。
- **ボタンのクリックは `EVENT_DISPATCH_MAP` で受ける。** `ADD_DISPATCH_EVENT(id, f)` の
  `f` は `void f(TControlID, VWDialogEventArgs&)`（`VWFC/VWUI/DialogEventArgs.h` の
  `CDialogEventHandlers`）。`CreateDialog(title, "OK", "", false)` のように**キャンセルを
  空文字**にすると OK だけのダイアログになる。
- **出せなかったときの逃げ道を必ず持つ。** レイアウトを組めなければ `AlertInform` へ落とす
  ——結果を伝えられないまま黙って終わるのが最悪。

## サムネイル付きのリソース選択（画像ポップアップ）

VectorWorks 本体の「鋼材断面を選択」のような**サムネイル付きのリソース選択**を、自前の
レイアウトダイアログ（`VWFC::VWUI::VWDialog` を継承し `CreateDialogLayout()` を自分で
書く形）で出す話。**使うクラスを間違えると永久に作れない**——ここが最大の落とし穴。

### `VWImagePopupCtrl` では作れない（SDK の実装が空のスタブ）

`VWFC::VWUI::VWImagePopupCtrl::CreateControl(VWDialog*)` は**実機（VW 2026 / macOS）で
必ず false を返す**。理由は SDK に同梱された実装ソースにそのまま書いてあり、
**生成の呼び出しがコメントアウトされたまま無条件に false を返すスタブ**である。

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

したがって**呼ぶ順序も `SetAdvanced()` も `VWResourceListCategorized::
DialogImagePopup_Init(dialog, id)` も関係ない**。項目を足す前に落ちるのは当然で、
条件出しをしても直らない。同じクラスの他のメソッド（`AddItems` /
`GetSelectedItemIndex` …）自体は実装されているが、どれも `CreateControl` が覚える
親ダイアログ（`fpParentDlg`）を使うため、生成に失敗したままでは呼べない。

### 代わりに `VWThumbnailPopupCtrl` を使う（実機確認済み）

同じコンポーネント種別（`VWControlType::eCompThumbnailPopup`）を指す双子のクラス
`VWFC::VWUI::VWThumbnailPopupCtrl`（`VWFC/VWUI/ThumbnailPopupCtrl.h`）は**実装が
生きている**。作りは、実機で問題なく使えている `VWSymbolDisplayCtrl` と**同型**
（`gSDK->Create…` でコントロールを作り、基底へ親を覚えさせて返す）。

```cpp
// SDKLib/Source/VWSDK/VWFC/VWUI/ThumbnailPopupCtrl.cpp
bool VWThumbnailPopupCtrl::CreateControl(VWDialog* pDlg, ThumbnailSizeType sizeType /*= kStandardSize*/)
{
	gSDK->CreateCustomThumbnailPopup(pDlg->GetControlID(), fControlID, sizeType);
	return VWControl::CreateControl( pDlg );	// 親を覚えて true を返すだけ
}
```

`VWDialog` 側にも受け口があり（`GetThumbnailPopupCtrlByID(TControlID)`。未登録の ID なら
ラッパを作って親を結び付けて返す）、レイアウトダイアログで使う想定のコントロールである
ことが分かる。**実機（VW 2026 / macOS）で確認した**——この形でコントロールが作られ、
シンボルのサムネイルが並ぶポップアップが実際に出る。最小の手順は次のとおり。

```cpp
// ダイアログのメンバ: VWThumbnailPopupCtrl fThumb{ kThumbID }; VWResourceList fList;
bool CMyDialog::CreateDialogLayout()
{
	// 1) 図面のシンボル定義一覧（kSymDefNode = 16）を作る。
	//    ★ このリストはダイアログが生きている間ずっと保持すること（後述）。
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

- **項目は `AddImageFromResource` の順に並び、添字は一覧の添字と一致する。** 実測:
  16 件のシンボル定義を足すと `GetItemsCount()` も 16 になり、2 番目を選んだときの
  `GetSelectedItemIndex()` は 1、その名前は一覧の 1 番目と同じだった。
- **選択は添字でもリソースそのものでも引ける。** `GetSelectedItemIndex()` は 0 始まりの
  添字、`GetSelectedItem()` は**選ばれたリソースの `InternalIndex`** を返す。
  `InternalIndex` からシンボル定義そのものへは `gSDK->InternalIndexToHandle(index)`、
  名前だけなら `gSDK->InternalIndexToNameN(index, outName)`（`ISDK.h`）。添字は一致する
  ものの、**一覧を作り直したり項目を出し入れする作りなら `InternalIndex` 側で持つ**ほうが
  崩れない（`GetItemObject(i)` / `GetObjectItemIndex(item)` で相互に引ける）。
- **「未選択」は読み取れない——項目を足した時点で先頭が選ばれている。** 実測で、
  `AddImageFromResource` を済ませた直後の `GetSelectedItemIndex()` は **0**（＝先頭）
  だった。ソースは `GS_GetImagePopupSelectedItem` の戻りから 1 を引くので「本当に何も
  選ばれていなければ `(size_t)-1`」になるはずだが、**実機ではそうならない**。
  したがって**「まだ選んでいない」を添字で判別することはできない**——必要なら
  「ユーザが選択イベントを起こしたか」を自分で覚えておく。
- **選択イベントは `selected` として届く。** `VWDialogEventArgs::IsImagePopupSelected()`
  の実体は `!fbNegativeControlID`——**そのコントロール ID への通常のディスパッチイベント**
  が「選ばれた」を意味する。`gSDK->SetImagePopupResourceAdvanced(dialogID, controlID)`
  を立てると、これに加えて `IsImagePopupBeforeOpen()`（ポップアップを開く直前。負の
  コントロール ID で来る advanced イベント。`SImagePopupAdvancedMsgData` を伴う）も
  届く。実機のログ例（1 回開いて 2 番目を選んだとき）:

  ```
  イベント: before-open   → GetSelectedItemIndex() = 0（先頭が選ばれている状態）
  イベント: selected      → GetSelectedItemIndex() = 1 / 'アンカーボルト_M16'
  ```

  選んだ値はこの `selected` で読んでもよいし、OK のときにまとめて読んでもよい
  （どちらでも同じ値が返る）。
- **DDX では受けられない。** `VWDialog::AddDDX_ImagePopup` の実装は
  `VWImagePopupCtrl::PullDownResourceLayout{Set,Get}SelectedResourceName` を呼ぶだけで、
  **`PullDownResourceLayout` 系（リソース名の文字列でやり取りする経路）専用**。この
  ポップアップの選択は自分で読む（イベントで読むか、OK のときに読む）。
- **大きさの指定は 2 種類だけ。** `ThumbnailSizeType` は `short` の別名で、定数は
  `kStandardSize = 0` と `kLineTypeSize = 1`（`Kernel/API/MiniCadCallBacks.h`）。
  サムネイルの描画モード・視点は `VWSymbolDisplayCtrl` と違って**指定できない**。
- **リソース一覧はダイアログより長生きさせる。** `VWResourceList` は参照カウント式で、
  最後の 1 つが消えるときに `gSDK->DisposeResourceList(listID)` を呼ぶ。
  `CreateDialogLayout()` のローカル変数にすると、抜けた時点でポップアップが参照している
  リスト ID が無効になる。**ダイアログのメンバに置く。**

### `VWImagePopupCtrl` の機能（カテゴリ・区切り）がどうしても要るとき【推定】

カテゴリ切り替え（`SetCategories`）・区切り（`AddItemSeparator`）・`ShowImage` は
`VWImagePopupCtrl` にしかない。`VWControl::CreateControl(VWDialog*)` は
**public かつ非 virtual**（`VWImagePopupCtrl::CreateControl` は override ではなく
名前の隠蔽）なので、**コントロールの生成は `gSDK` で行い、親の記憶だけを基底へ明示的に
呼んで任せる**という抜け道は文法上成立する（`compile` で確認済み。実機未確認）。

```cpp
gSDK->CreateCustomThumbnailPopup(this->GetControlID(), kPopupID, kStandardSize);
popup.VWFC::VWUI::VWControl::CreateControl(this);	// ← 基底を明示呼び出し
popup.AddItems(fList);
```

両クラスとも中身は `(dialogID, controlID)` を使う `GS_*ImagePopup*` の薄い包みなので、
**同じコントロールをどちらのクラスからでも触れる**道理だが、実挙動は未確認。まず
`VWThumbnailPopupCtrl` で足りるかを確かめ、足りないときだけこの道を試すこと。

### 一覧には「ユーザが置くシンボル」以外も混ざる

`VWResourceList::BuildList(kSymDefNode, true)` は**図面にあるシンボル定義を全部**返す。
実測（16 件）の内訳には、ユーザが置く部品（`アンカーボルト_M12` / `床束` / `仕口` …）に
混じって、**VectorWorks 自身がプラグインオブジェクトのスタイルとして持っている
シンボル定義**が並んでいた——`図面ラベル - 図番`・`断面寸法`・`立断面指示線`・
`グリッド線`・`遠山信夫アトリエ一級建築士事務所`（図面枠）など。

**切り分けは `gSDK->GetSymbolDefSubType(hSymDef)`**（0 なら普通のシンボル定義、
0 以外ならプラグインオブジェクトのスタイル）。実測の対応表と、絞り込むと添字がずれる
点は [`Symbols.md`](Symbols.md) の「一覧には『ユーザが置く部品』以外も混ざる」を参照。

### 代わりの道（実機で確認済み）

サムネイル付きポップアップが使えなくても、**名前のプルダウン（`VWPullDownMenuCtrl`）
＋選択中の絵を出す `VWSymbolDisplayCtrl`** の組み合わせで機能は成立する（絵は選んだ
後にしか出ず、VectorWorks 本体のポップアップとは見た目が異なる二段構えになる点だけ
違う）。シンボル定義一覧の採り方と `VWSymbolDisplayCtrl` の使い方は
[`Symbols.md`](Symbols.md) の「シンボル定義のサムネイル・一覧を UI に出す」を参照。

## モードレス（非モーダル）なパレット（`IExtensionWebPalette`）

（issue [#19](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/19)）
「図面を操作したまま開いていられるウィンドウを出せるか」の調査。**口はある。**
`VWFC::VWUI::VWDialog::RunDialogLayout` はモーダル（閉じるまで戻らない）だが、それとは
別に SDK には **`IExtensionWebPalette`（SDK 側の基底クラスは
`VWFC::PluginSupport::VWExtensionWebPalette`）という、モードレスのパレットを出すための
拡張種別がもう 1 つある**。

**この節は全体が【ヘッダ根拠】＋ CI の `compile`（構文チェックのみ）までで、
実機（VectorWorks 本体）では未確認。** 実機で開閉・入力・イベント到達を確かめてから
この注記を外すこと。

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

**公式リファレンス（[Plug-in Module](../Info/Plug-in%20Module.md#Extensions)）が挙げる
拡張は Menu / Parametric / Tool / VS Functions の 4 つだけ**だが、
`GROUPID_ExtensionWebPalettes` は `GROUPID_ExtensionMenu` /
`GROUPID_ExtensionParametric` / `GROUPID_ExtensionTool` /
`GROUPID_ExtensionVSFunctions`（と `Animation` / `WSFunctionOptions`）と**まったく
同じ並びで定義されている**（`grep -rnIE 'GROUPID_Extension[A-Za-z]*\s*='` で一括して
出てくる）。つまり Web Palette は**公式リファレンス未掲載なだけで、同じ
`REGISTER_Extension<T>(groupID, action, moduleInfo, iid, ioInterface, cbp, reply)` の
枠組みで `plugin_main` から登録する、5 つ目の拡張種別**である。登録の書き方は
[Menu General Info](../Info/Menu%20General%20Info.md#Register%20Menu%20Extension) の
`REGISTER_Extension<TesterModule::CExtMenu>(GROUPID_ExtensionMenu, action, pInfo, ioData, cbp, reply)`
と同型で、`GROUPID_ExtensionMenu` を `GROUPID_ExtensionWebPalettes` に、
`CExtMenu` を `VWExtensionWebPalette` を継承したクラスに差し替えるだけのはずである
（この置き換え自体は未確認）。

### 1. モードレスに出す口はある

`gSDK->SetWebPaletteVisibility(iid, true)` で表示・非表示を切り替える。これは
`RunDialogLayout` と違って**呼び出しをブロックしない**単純な `bool` の setter で、
「開いたまま制御を返す」設計そのものである。**中身は HTML/JS**
（`GetInitialURL()` が返す URL を埋め込みブラウザで表示する。既定の URL は
`VWFC::PluginSupport::GetStandardURL(htmlFolderName = "html", htmlFile = "index.html")`
が組み立てる）ので、`VWDialog` のようにネイティブコントロール
（`VWEditTextCtrl` 等）を並べる作りではない。

### 2. 入力は JS 経由で受け取る

パレットの中身は HTML（既定でプラグインの `.vwr` の `html/index.html`）なので、
**複数行の入力欄やボタンは HTML 側に置く**。JS から C++ 側へは
`VWExtensionPaletteJSProvider::OnFunctionCall(objName, functionName, args, context)`
（`args` は `std::vector<nlohmann::json>`）で届く。受け口の組み立ては
`DEFINE_WebPalette_DISPATCH_MAP` 系のマクロで行う——書き味は
`EVENT_DISPATCH_MAP`（上記「レイアウトダイアログ」）に似るが、**コントロール ID
ではなく JS 側が呼んだ関数名の文字列で分岐する**点が異なる。入力された文字列は
JS 側が `args`（JSON）に載せて渡す想定である。

### 3. `DoInterface` が戻った後も生きるはず【推定】

`SetWebPaletteVisibility` / `GetWebPaletteFrame` は特定の `DoInterface` 呼び出しに
閉じていない `gSDK` の生の関数で、**別のメニューコマンドの `DoInterface` からでも
同じ `iid` で同じパレットに触れる**はずである。拡張の登録が Menu / Parametric / Tool
と同じ枠組みだからで、それらは `plugin_main` で 1 度登録すれば以後 Vectorworks が
持ち続ける（[Plug-in Module#Extensions](../Info/Plug-in%20Module.md#Extensions)）。
ただし「開いたまま図面を操作できるか」「戻った後も本当にイベントが届くか」は
実機でしか確かめられない。

### 4. 殻と本体の割り方【推定・要実機確認】——おそらく PIO と同じ制約

**登録した拡張オブジェクトの寿命は VW 側が握る。** `REGISTER_Extension` で VCOM の
参照を Vectorworks に渡した後の状態は、[Plug-in Modules「PIO のイベントも殻から
本体へ取り次げる」](Plug-in%20Modules.md)で確認した PIO の登録と同じ形——
**「VW に番地を握られる」側**である。したがって:

- **拡張の登録そのもの（`VWExtensionWebPalette` を継承したクラス、
  `IWebCallbacksProvider` シンクの実装）は殻に置くべき**。降ろせる本体に置くと、
  本体を降ろした瞬間に VW が握っている vtable が消え、次にユーザーが JS 側の
  ボタンを押した（`OnFunctionCall` が呼ばれた）ときに落ちる——PIO の
  `Recalculate()` を本体に出したときと同じ理屈（[Plug-in Modules
  「落とし穴: 殻の記憶域を本体に持たせると落ちる」](Plug-in%20Modules.md)）。
- **HTML/JS の中身（front-end）は `.vwr` のリソースフォルダから読む**のが既定
  （`GetStandardURL`）。`.vwr` は殻にしか置けないと既に確認済み（[Plug-in Modules
  「本体側からダイアログを開ける」](Plug-in%20Modules.md)の「`.vwr` は殻の側にしか
  無くてよい」）。パレットの見た目・入力欄を変えるだけなら**殻を再ビルドせず
  HTML/JS だけ書き換えれば済む**可能性があるが未確認。
- **`OnFunctionCall` を受けてからの実処理（今回の用途なら「見た所見を PR へ送る」）は、
  PIO の `Recalculate()` と同じやり方で本体へ委譲できる見込み**——殻の
  `IWebCallbacksProvider` 実装が、受け取った `args` をその場でコピーしてから
  （境界を越える記憶域は必ずその場で写す。[Plug-in Modules
  の決めごと](Plug-in%20Modules.md)）、本体へ C の ABI 越しに渡す。
- **明示的に閉じる／登録解除する API は、ヘッダ検索の範囲では見つからなかった。**
  `IWebPaletteFrame` にあるのは `Reload` / `LoadURL` / `ExecuteJavaScript` /
  `SetTitle` / `Focus` のみで、**閉じる・破棄する操作は無い**。ヘッダから保証できる
  唯一の手立ては `SetWebPaletteVisibility(iid, false)` で**隠す**ことだけである。
  拡張自体の登録解除は、他の拡張種別と同じく `plugin_main` の `action` 引数
  （アンロード時に呼ばれるもの）で行う設計だと推測されるが、その `action` の値と
  挙動はこの調査の範囲では確認していない。

### 構文チェック（CI, `compile`）

`VWExtensionWebPalette` / `VWExtensionPaletteJSProvider` を継承し、ディスパッチ
マクロと `gSDK->{Get,Set}WebPaletteVisibility` / `GetWebPaletteFrame` を呼ぶ
最小のクラス階層は **`-fsyntax-only` を通った**（調査用スニペットはマージ前に削除
済み——結論はここに残す）。`REGISTER_Extension` の実際の呼び出し
（`plugin_main` からの登録）は Menu 等の既存の拡張と同じ枠組みのため、この調査では
構文チェックしていない。

### まだ分かっていないこと（実機確認が必要）

- 実際に**モードレスに開き、図面を操作しながら見える**か。
- `DoInterface` が戻った後、**本当に `OnFunctionCall` が届く**か。
- 拡張の登録（≒本体では持てない側）を**本体に置くと実際に落ちる**のか
  （PIO と同型の話だが、Web Palette 自体での実機確認はまだ無い）。
- **登録解除・アンロード時の `action` の扱い**——降ろす前に安全に手放せるか。
- `.vwr` の外（本体側）に HTML/JS を置けるか、それとも `GetStandardURL` の既定どおり
  殻の `.vwr` の中に置くしかないか。
