# [issue #305] Windows: 大きな図面（#290 と同じ合成。矩形 10 万・シンボル定義 100・配置 1 万）を
# CloseDocument() で閉じる。描画の前（作った・開いた同じ刻みの中）と後（pump で回してから）。
# 一時ディレクトリの VwSdkProbes-newpath-close.do へ写し、vw_run_probe probe=newpath-close（空図面で）。
# 大きいので 1 周で 1 つずつ走らせてもよい（コミットが上限に近づくと VM の見張りが止める）。
#
mkdir $T\p305
mark
note 305-J 3 枚のとき、裏から前へ出して閉じた後にどれが前へ出るか（c → d の順に開き、走り出しの図面を前へ）
open $T\p305\c.vwx
open $T\p305\d.vwx
back
switch $T\p305\c.vwx
closedoc
back
switch $T\p305\d.vwx
closedoc
back
note 305-L1 無題・描画の前（作った刻みの中で閉じる）
new
rects 100000
syms 100 10000
count
closedoc
back
note 305-L2 保存してから・描画の前
new
rects 100000
syms 100 10000
saveto $T\p305\big.vwx
closedoc
back
note 305-L3 開いただけ・描画の前（DoMenuName("Close") なら確認が出る場面）
open $T\p305\big.vwx
count
closedoc
back
note 305-L4 開いて描画させてから
open $T\p305\big.vwx
pump 60000
closedoc
back
docs
