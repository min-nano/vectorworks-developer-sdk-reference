# [issue #298] Windows: リンクを指す図面と、削除を拒む ACE の上書き保存。
# 一時ディレクトリの VwSdkProbes-save-links.do へ写し、vw_run_probe probe=save-links（空図面で）。
# 警告は見張りが読んでログへ書き、OK を押す。mklink と icacls は昇格した VW の子で動く。
# ACE は Everyone（*S-1-1-0）に付ける。名前は OS の言語で変わるので SID で書く。
#
note L0 ファイルのシンボリックリンク（基準）
mkdir $T\p298\L0real
mkdir $T\p298\L0lnk
new
rect a
saveto $T\p298\L0real\f.vwx
close
sh mklink "$T\p298\L0lnk\d.vwx" "$T\p298\L0real\f.vwx"
sh dir /a "$T\p298\L0lnk"
open $T\p298\L0lnk\d.vwx
docs
final $T\p298\L0lnk\d.vwx
oscheck $T\p298\L0lnk\d.vwx
oscheck $T\p298\L0real\f.vwx
rect b
save
docs
final $T\p298\L0lnk\d.vwx
fs $T\p298\L0real\f.vwx
sh dir /a "$T\p298\L0lnk"
sh dir /a "$T\p298\L0real"
rect c
save
docs
final $T\p298\L0lnk\d.vwx
fs $T\p298\L0real\f.vwx
sh dir /a "$T\p298\L0lnk"
sh dir /a "$T\p298\L0real"
close
docs
sh dir /a "$T\p298\L0lnk"
sh dir /a "$T\p298\L0real"
note L1 リンクの先のフォルダに足せない
mkdir $T\p298\L1real
mkdir $T\p298\L1lnk
new
rect a
saveto $T\p298\L1real\f.vwx
close
sh mklink "$T\p298\L1lnk\d.vwx" "$T\p298\L1real\f.vwx"
open $T\p298\L1lnk\d.vwx
docs
sh icacls "$T\p298\L1real" /deny *S-1-1-0:(WD,AD)
oscheck $T\p298\L1lnk\d.vwx
oscheck $T\p298\L1real\f.vwx
rect b
save
docs
final $T\p298\L1lnk\d.vwx
fs $T\p298\L1real\f.vwx
sh dir /a "$T\p298\L1lnk"
sh dir /a "$T\p298\L1real"
sh icacls "$T\p298\L1real" /remove:d *S-1-1-0
closefront
docs
sh dir /a "$T\p298\L1lnk"
sh dir /a "$T\p298\L1real"
note L2 リンクのあるフォルダに足せない
mkdir $T\p298\L2real
mkdir $T\p298\L2lnk
new
rect a
saveto $T\p298\L2real\f.vwx
close
sh mklink "$T\p298\L2lnk\d.vwx" "$T\p298\L2real\f.vwx"
open $T\p298\L2lnk\d.vwx
docs
sh icacls "$T\p298\L2lnk" /deny *S-1-1-0:(WD,AD)
oscheck $T\p298\L2lnk\d.vwx
oscheck $T\p298\L2real\f.vwx
rect b
save
docs
final $T\p298\L2lnk\d.vwx
fs $T\p298\L2real\f.vwx
sh dir /a "$T\p298\L2lnk"
sh dir /a "$T\p298\L2real"
sh icacls "$T\p298\L2lnk" /remove:d *S-1-1-0
closefront
docs
sh dir /a "$T\p298\L2lnk"
sh dir /a "$T\p298\L2real"
