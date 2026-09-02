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

## 打ち切った調査: サムネイル付きのリソース選択（`VWImagePopupCtrl`）

自前のレイアウトダイアログ（`VWFC::VWUI::VWDialog` を継承し `CreateDialogLayout()` を
自分で書く形）で、VectorWorks 本体の「鋼材断面を選択」のようなサムネイル付き
ポップアップ（`VWFC::VWUI::VWImagePopupCtrl`）を使おうとしたが、**実機（VW 2026 /
macOS）でコントロールを作れなかった**。

- **`VWImagePopupCtrl::CreateControl(this)` が false を返す。** `CreateDialogLayout()`
  の中で、ダイアログの枠・静的テキスト・チェックボックスの生成に成功した後、行ごとに
  コントロールを作る途中の**最初の 1 個で** false になる。**`AddItem` で項目を足す前**
  の段階で失敗しており、項目数や中身は無関係。失敗すると `CreateDialogLayout()` も
  false を返し、ダイアログ自体が出せなくなる。
- **同じダイアログの中で他のコントロールは問題なく作れる。** `VWCheckButtonCtrl` /
  `VWStaticTextCtrl` / `VWPullDownMenuCtrl` / `VWSymbolDisplayCtrl` は同じ
  `CreateDialogLayout()` の中で生成に成功しており、レイアウトダイアログの組み方
  自体が壊れているわけではない。`VWImagePopupCtrl` に固有の問題。
- **失敗の原因はヘッダからは分からない。** `CreateControl` の実装は SDK ヘッダに無く
  （ライブラリ側でコンパイル済み）、`ImagePopupCtrl.h` にも事前条件を示すコメントは
  無い。作る前後の手順を変えれば false を避けられるのかは、**実機でしか確かめられない**。

### ヘッダから読み取れたこと（【ヘッダ根拠】・実機未検証）

- `VWFC::Tools::VWResourceListCategorized`（`VWResourceListCategorized.h`）は、画像
  ポップアップの初期化・イベント処理・更新に **2 系統のオーバーロード**を持つ。
  - `DialogImagePopup_Init(VWFC::VWUI::VWDialog* dialog, TControlID id)` /
    `_Event(...)` / `_Update(...)` — **`VWFC::VWUI::VWDialog`（今回使っている方の
    レイアウトダイアログ）向け**。
  - `DialogImagePopup_Init(Sint32 dialogID, Sint32 imagePopupCtrlID)` /
    `_Event(...)` / `_Update(...)` — ヘッダのコメントに **"direct event handler
    from ISDK::RunLayoutDialog"** とあり、旧来の・リソースファイルで組む方の
    レイアウトダイアログ向け（`vs.py` に対応する `InsertImagePopupResource` /
    `GetImagePopupSelectedItem` / `GetNumImagePopupItems` などの VectorScript
    関数があるのはこちら側）。

  `VWFC::VWUI::VWDialog` 向けのオーバーロードが別立てで存在すること自体は、
  `VWImagePopupCtrl` が `VWDialog` 系のレイアウトダイアログでも使われる想定である
  ことを示している。ただし、`VWResourceListCategorized::DialogImagePopup_Init` を
  **`CreateControl` の前後どちらで呼ぶべきか、あるいはこれを呼ぶことで
  `CreateControl` が false を返す状況が変わるのか**は、宣言だけからは判断できない
  （試すには実機が要る）。
- `VWFC::VWUI::DialogEventArgs.h` には「`IsImagePopupSelected()` /
  `IsImagePopupBeforeOpen()` / `IsImagePopupCategoryChanged()` は `SetAdvanced` を
  呼んで初めて効く」という趣旨のコメントがある
  （`// image popup events support (call SetAdvanced to enable)`）。**ただしこれは
  イベントの受け取り方についての注記で、`CreateControl` が false になることの説明
  にはならない**——`SetAdvanced()` は `CreateControl` と独立した関数として宣言されて
  おり、呼び順の制約はヘッダに書かれていない。
- `VWFC::VWUI::Dialog.h` の `AddDDX_ImagePopup(TControlID, TXString*, const
  TXString& key="")` は、`AddDDX_ChooseLayerPopup` などの他の DDX 関数と同じ並びで
  宣言されているだけで、「`PullDownResourceLayout` 系専用」と読める記述はヘッダに
  無い（実際に効くかどうかは未検証）。

### 分からないまま残った点

`CreateControl` が false を返す段階で止まっているため、次の 3 点は**確かめようが
無かった**（`ImagePopupCtrl.h` の宣言を読んだだけで、実機での挙動は未検証）。

- `AddItem(const VWResourceList&, size_t)` / `AddItems(const VWResourceList&)` で
  足した項目の添字が、`GetSelectedItemIndex()` の戻り値と追加順で対応するか。
- `ShowImage(true)` で閉じた状態でも選択中の絵が出るか。
- 選択の確定値を DDX（`AddDDX_ImagePopup`）で受けられるか。

### 代わりの道（実機で確認済み）

サムネイル付きポップアップが使えなくても、**名前のプルダウン（`VWPullDownMenuCtrl`）
＋選択中の絵を出す `VWSymbolDisplayCtrl`** の組み合わせで機能は成立する（絵は選んだ
後にしか出ず、VectorWorks 本体のポップアップとは見た目が異なる二段構えになる点だけ
違う）。シンボル定義一覧の採り方と `VWSymbolDisplayCtrl` の使い方は
[`Symbols.md`](Symbols.md) の「シンボル定義のサムネイル・一覧を UI に出す」を参照。

`VWResourceListCategorized` 経由の道（`CreateControl` の前後で
`DialogImagePopup_Init(dialog, id)` を呼んでみる、など）を実機で試すのは今回は
行っていない——次に踏み込むならここから。
