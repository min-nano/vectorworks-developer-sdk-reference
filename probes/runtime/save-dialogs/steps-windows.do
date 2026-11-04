# [issue #288] Windows の手順。一時ディレクトリの VwSdkProbes-save-dialogs.do へ写して走らせる
# （TEMP の下。指示ファイルのパスはログの先頭に出る）。mac の結果は Findings/Documents.md。
# 空図面で vw_run_probe（new_document=true）。ダイアログは見張りが書いて押す。
#
# ---- 4: 保存先の形
mkdir $T/to
new
saveto $T/to/noext
closefront
new
saveto $T/to/name.txt
closefront
new
saveto $T/to/name.sta
closefront
new
saveto $T/to/name.dwg
closefront
new
saveto $T/nofolder/x.vwx
closefront
write $T/to/afile
new
saveto $T/to/afile/x.vwx
closefront
new
saveto $T/to/
closefront
ls $T/to
# ---- 5: ファイルを消した後（別の図面から同じ文字列・大小違い。自分のパス）
mkdir $T/c
new
saveto $T/c/gone.vwx
mv $T/c/gone.vwx -> $T/c/moved.bak
new
saveto $T/c/gone.vwx
docs
closefront
new
saveto $T/c/GONE.vwx
docs
rm $T/c/GONE.vwx
closefront
switch $T/c/gone.vwx
save
docs
closefront
# ---- 2・3 の API の側: 読み取り専用を OpenDocumentPath で開く
mkdir $T/r
new
rect R1
saveto $T/r/ro.vwx
closefront
ro $T/r/ro.vwx
open $T/r/ro.vwx
docs
list
saveto $T/r/ro.vwx
docs
closefront
# ---- 6: 取り消しの段（DoMenuName の側）。メニューの側は画面から押す
new
event A
event B
saveto $T/c/u1.vwx
list
menu
doundo
list
closefront
new
ev-member M
event B
saveto $T/c/u2.vwx
list
doundo
list
doundo
list
closefront
# ---- 無題の自分のパスへ（Program Files へ書けるかが問い。書けたら手で消す）
new
docs
save
docs
closefront
docs
