# [issue #298] Windows の続き: ファイルの削除を拒む ACE と、親のフォルダの FILE_DELETE_CHILD。
# D1 と D3 の違いが問い（DELETE を拒まれても、親の FILE_DELETE_CHILD で消せるのが NTFS の規則）。
#
note D0 親に (DC) の拒否だけ
mkdir $T\p298\D0
new
rect a
saveto $T\p298\D0\f.vwx
sh icacls "$T\p298\D0" /deny *S-1-1-0:(DC)
oscheck $T\p298\D0\f.vwx
rect b
save
docs
oscheck $T\p298\D0\f.vwx
fs $T\p298\D0\f.vwx
sh dir /a "$T\p298\D0"
sh icacls "$T\p298\D0" /remove:d *S-1-1-0
closefront
docs
note D1 ファイルに (DE) の拒否だけ。親はふつう
mkdir $T\p298\D1
new
rect a
saveto $T\p298\D1\f.vwx
sh icacls "$T\p298\D1\f.vwx" /deny *S-1-1-0:(DE)
oscheck $T\p298\D1\f.vwx
rect b
save
docs
oscheck $T\p298\D1\f.vwx
fs $T\p298\D1\f.vwx
sh icacls "$T\p298\D1\f.vwx"
sh dir /a "$T\p298\D1"
rect c
save
docs
fs $T\p298\D1\f.vwx
sh icacls "$T\p298\D1\f.vwx"
sh dir /a "$T\p298\D1"
sh icacls "$T\p298\D1\f.vwx" /remove:d *S-1-1-0
closefront
docs
note D3 ファイルに (DE) の拒否と、親に (DC) の拒否
mkdir $T\p298\D3
new
rect a
saveto $T\p298\D3\f.vwx
sh icacls "$T\p298\D3\f.vwx" /deny *S-1-1-0:(DE)
sh icacls "$T\p298\D3" /deny *S-1-1-0:(DC)
oscheck $T\p298\D3\f.vwx
rect b
save
docs
oscheck $T\p298\D3\f.vwx
fs $T\p298\D3\f.vwx
sh icacls "$T\p298\D3\f.vwx"
sh dir /a "$T\p298\D3"
rect c
save
docs
fs $T\p298\D3\f.vwx
sh icacls "$T\p298\D3\f.vwx"
sh dir /a "$T\p298\D3"
close
docs
sh icacls "$T\p298\D3" /remove:d *S-1-1-0
sh icacls "$T\p298\D3\f.vwx" /remove:d *S-1-1-0
closefront
docs
sh dir /a "$T\p298\D3"
