# [issue #304] Windows: 無題の図面を新しいパスへ保存する。親のフォルダはあるが書き込めないとき。
# 一時ディレクトリの VwSdkProbes-newpath-close.do へ写し、vw_run_probe probe=newpath-close（空図面で）。
# 前もって用意するもの（プローブの外。昇格した PowerShell で）:
#   R: … 64 MB の VHD を diskpart で付け、R:\sub を作ってから attributes volume set readonly
#   \\localhost\p304ro … $T\p304\share を net share /GRANT:Everyone,READ で共有
#
note 304-0 基準: 書けるフォルダ
mkdir $T\p304\ok
oscheck $T\p304\ok\new.vwx
new
rect a
saveto $T\p304\ok\new.vwx
docs
closefront
note 304-1 親に Everyone:(AD) の拒否（FILE_ADD_FILE だけ）
mkdir $T\p304\ad
sh icacls "$T\p304\ad" /deny *S-1-1-0:(AD)
oscheck $T\p304\ad\new.vwx
new
rect a
saveto $T\p304\ad\new.vwx
docs
ls $T\p304\ad
closefront
sh icacls "$T\p304\ad" /remove:d *S-1-1-0
note 304-2 親に Everyone:(WD,AD) の拒否（#293 と同じ形）
mkdir $T\p304\wdad
sh icacls "$T\p304\wdad" /deny *S-1-1-0:(WD,AD)
oscheck $T\p304\wdad\new.vwx
new
rect a
saveto $T\p304\wdad\new.vwx
docs
ls $T\p304\wdad
closefront
sh icacls "$T\p304\wdad" /remove:d *S-1-1-0
note 304-3 親に読み取り専用の属性（フォルダでは効かないはず。対照）
mkdir $T\p304\attr
sh attrib +r "$T\p304\attr"
oscheck $T\p304\attr\new.vwx
new
rect a
saveto $T\p304\attr\new.vwx
docs
ls $T\p304\attr
closefront
sh attrib -r "$T\p304\attr"
note 304-4 読み取り専用のボリューム（VHD の R:）の下のフォルダ
oscheck R:\sub\new.vwx
new
rect a
saveto R:\sub\new.vwx
docs
ls R:\sub
closefront
note 304-5 読み取り専用のボリュームの根
oscheck R:\new.vwx
new
rect a
saveto R:\new.vwx
docs
closefront
note 304-6 読み取り専用の共有（UNC）
oscheck \\localhost\p304ro\new.vwx
new
rect a
saveto \\localhost\p304ro\new.vwx
docs
ls $T\p304\share
closefront
docs
