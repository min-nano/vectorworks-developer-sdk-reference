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

### 代わりに `VWThumbnailPopupCtrl` を使う【ソース根拠】

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
ことが分かる。最小の手順は次のとおり（`compile` で構文確認済み・**実機未確認**）。

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

- **選択は添字でもリソースそのものでも引ける。** `GetSelectedItemIndex()` は 0 始まりの
  添字（未選択のときは `(size_t)-1`）、`GetSelectedItem()` は**選ばれたリソースの
  `InternalIndex`** を返す。後者を使えば「追加した順と添字が一致するか」を気にしなくて
  よい（`GetItemObject(i)` / `GetObjectItemIndex(item)` で相互に引ける）。
  `InternalIndex` からシンボル定義そのものへは `gSDK->InternalIndexToHandle(index)`、
  名前だけなら `gSDK->InternalIndexToNameN(index, outName)`（`ISDK.h`）。
- **選択イベントは `SetAdvanced` 無しで届く。** `VWDialogEventArgs::IsImagePopupSelected()`
  の実体は `!fbNegativeControlID`——つまり**そのコントロール ID への通常のディスパッチ
  イベント**が「選ばれた」を意味する。`gSDK->SetImagePopupResourceAdvanced(dialogID,
  controlID)` が要るのは `IsImagePopupBeforeOpen()` /
  `IsImagePopupCategoryChanged()`（負のコントロール ID で来る advanced イベント。
  `SImagePopupAdvancedMsgData` を伴う）のほうだけ。
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

### 実機で確かめるべきこと（未確認）

- `VWThumbnailPopupCtrl::CreateControl` が実機でコントロールを作れるか（サムネイルが
  実際に出るか。シンボル定義の絵が期待どおりに描かれるか）。
- 未選択のときの `GetSelectedItemIndex()` の実際の戻り（ソース上は `(size_t)-1`）。
- `SetImagePopupResourceAdvanced` を立てたときの advanced イベントの届き方。
- 上の「基底の `CreateControl` を明示呼び出し」が実機で通るか。

### 代わりの道（実機で確認済み）

サムネイル付きポップアップが使えなくても、**名前のプルダウン（`VWPullDownMenuCtrl`）
＋選択中の絵を出す `VWSymbolDisplayCtrl`** の組み合わせで機能は成立する（絵は選んだ
後にしか出ず、VectorWorks 本体のポップアップとは見た目が異なる二段構えになる点だけ
違う）。シンボル定義一覧の採り方と `VWSymbolDisplayCtrl` の使い方は
[`Symbols.md`](Symbols.md) の「シンボル定義のサムネイル・一覧を UI に出す」を参照。
