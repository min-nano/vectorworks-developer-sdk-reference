# [issue #304] Windows の続き。icacls の AD は「サブフォルダの追加」、WD が「ファイルの追加」
# （FILE_ADD_FILE）。1 周目の 304-1 は AD だけを拒否していて、保存は通り VW Backup の作成で
# ダイアログが出た。ファイルの追加だけを拒否する場面と、VW Backup が既にある場面を足す。
#
note 304-7 親に Everyone:(WD) の拒否（FILE_ADD_FILE だけ。サブフォルダは作れる）
mkdir $T\p304\wd
sh icacls "$T\p304\wd" /deny *S-1-1-0:(WD)
oscheck $T\p304\wd\new.vwx
new
rect a
saveto $T\p304\wd\new.vwx
docs
tree $T\p304\wd
closefront
sh icacls "$T\p304\wd" /remove:d *S-1-1-0
note 304-8 親に Everyone:(AD) の拒否・VW Backup は前もって作ってある
mkdir $T\p304\adb\VW Backup
sh icacls "$T\p304\adb" /deny *S-1-1-0:(AD)
oscheck $T\p304\adb\new.vwx
new
rect a
saveto $T\p304\adb\new.vwx
docs
tree $T\p304\adb
closefront
sh icacls "$T\p304\adb" /remove:d *S-1-1-0
note 304-9 親に Everyone:(AD) の拒否（1 周目の 304-1 をもう一度。再現するか）
mkdir $T\p304\ad2
sh icacls "$T\p304\ad2" /deny *S-1-1-0:(AD)
new
rect a
saveto $T\p304\ad2\new.vwx
tree $T\p304\ad2
rect b
save
tree $T\p304\ad2
closefront
sh icacls "$T\p304\ad2" /remove:d *S-1-1-0
note 304-10 VW Backup の場所にファイルがある（フォルダを作れない別の形）
mkdir $T\p304\bkfile
write $T\p304\bkfile\VW Backup
new
rect a
saveto $T\p304\bkfile\new.vwx
tree $T\p304\bkfile
closefront
docs
