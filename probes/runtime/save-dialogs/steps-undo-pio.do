# [issue #288] mac で、PIO の空の段の条件を確かめる（Windows の結果は Findings/Undo.md）。
# 一時ディレクトリの VwSdkProbes-save-dialogs.do へ写し、空図面で vw_run_probe（new_document=true）。
#
mkdir $T/p
# 1: 構造材・別名保存・矩形（以後保存なし）。Windows は B・空・M
new
ev-member M
saveto $T/p/1.vwx
event B
list
doundo
list
doundo
list
doundo
list
closefront
# 2: 構造材・別名保存・矩形・上書き。Windows は B・空・M
new
ev-member M
saveto $T/p/2.vwx
event B
save
list
doundo
list
doundo
list
doundo
list
closefront
# 3: 構造材・別名保存・構造材・上書き。Windows は N・空・M
new
ev-member M
saveto $T/p/3.vwx
ev-member N
save
list
doundo
list
doundo
list
doundo
list
closefront
# 4: 矩形・別名保存・構造材・上書き。Windows は 空・N
new
event A
saveto $T/p/4.vwx
ev-member N
save
list
doundo
list
doundo
list
doundo
list
closefront
# 5: 構造材のある図面を開き直し、矩形・上書き。Windows は C（空なし）
open $T/p/2.vwx
event C
save
list
doundo
list
doundo
list
closefront
# 6: 構造材のある図面を開き直し、構造材・上書き。Windows は X（空なし）
open $T/p/2.vwx
ev-member X
save
list
doundo
list
doundo
list
closefront
# 7: 構造材の無い図面を開き直し、構造材・上書き。Windows は 空・X
new
rect R
saveto $T/p/7.vwx
closefront
open $T/p/7.vwx
ev-member X
save
list
doundo
list
doundo
list
doundo
list
closefront
docs
