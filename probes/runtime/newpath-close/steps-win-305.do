# [issue #305] Windows: CloseDocument() は保存の確認を出さずに閉じるか（小さな図面）。
# 一時ディレクトリの VwSdkProbes-newpath-close.do へ写し、vw_run_probe probe=newpath-close（空図面で）。
# 見張りが付くのは closedoc の呼び出しの間だけ。窓が出れば文言をログへ書き、キャンセルを押す。
# 各場面で、走り出しの図面（ブリッジの空図面）を mark しておき、閉じた後に back で戻す。
#
mkdir $T\p305
mark
docs
note 305-A 無題・未保存の変更あり
new
rect a
closedoc
back
note 305-B 無題・無変更
new
closedoc
back
note 305-C 保存済み・保存の後に変更（undo イベントの外の矩形）
new
rect a
saveto $T\p305\c.vwx
rect b
ls $T\p305
closedoc
ls $T\p305
back
note 305-D 保存済み・保存した直後（変更なし）
new
rect a
saveto $T\p305\d.vwx
closedoc
back
note 305-E OpenDocumentPath で開いただけ（「閉じる」では確認が出る場面）
open $T\p305\d.vwx
docs
ls $T\p305
closedoc
ls $T\p305
back
note 305-F 裏の図面を前へ出して閉じる（CLI の close の形）
open $T\p305\c.vwx
back
switch $T\p305\c.vwx
closedoc
back
note 305-G 読み取り専用のファイルを開いたコピー
new
rect a
saveto $T\p305\ro.vwx
closedoc
ro $T\p305\ro.vwx
open $T\p305\ro.vwx
docs
ls $T\p305
closedoc
ls $T\p305
rw $T\p305\ro.vwx
back
note 305-H 使用中（ほかのプロセスが書き込みで握る）のファイルを開いたコピー
new
rect a
saveto $T\p305\held.vwx
closedoc
hold $T\p305\held.vwx
open $T\p305\held.vwx
docs
closedoc
unhold
ls $T\p305
back
note 305-I コピーに図形を足してから閉じる
ro $T\p305\ro.vwx
open $T\p305\ro.vwx
rect x
closedoc
rw $T\p305\ro.vwx
back
docs
