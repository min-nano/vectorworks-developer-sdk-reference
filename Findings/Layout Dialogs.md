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
