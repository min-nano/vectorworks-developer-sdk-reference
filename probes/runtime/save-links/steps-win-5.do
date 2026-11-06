# [issue #298] Windows の続き: 図面のパスがリンクのまま（実体の無いリンクへ saveto した直後）に、
# どちらかのフォルダへ足せなくして上書きする。無題の図面に save を流さないよう saveto で渡す。
#
note P1 リンクの先のフォルダに足せない
mkdir $T\p298\P1real
mkdir $T\p298\P1lnk
sh mklink "$T\p298\P1lnk\d.vwx" "$T\p298\P1real\f.vwx"
new
rect a
saveto $T\p298\P1lnk\d.vwx
docs
sh icacls "$T\p298\P1real" /deny *S-1-1-0:(WD,AD)
oscheck $T\p298\P1lnk\d.vwx
oscheck $T\p298\P1real\f.vwx
rect b
saveto $T\p298\P1lnk\d.vwx
docs
final $T\p298\P1lnk\d.vwx
sh dir /a "$T\p298\P1lnk"
sh dir /a "$T\p298\P1real"
sh icacls "$T\p298\P1real" /remove:d *S-1-1-0
closefront
docs
sh dir /a "$T\p298\P1lnk"
sh dir /a "$T\p298\P1real"
note P2 リンクのあるフォルダに足せない
mkdir $T\p298\P2real
mkdir $T\p298\P2lnk
sh mklink "$T\p298\P2lnk\d.vwx" "$T\p298\P2real\f.vwx"
new
rect a
saveto $T\p298\P2lnk\d.vwx
docs
sh icacls "$T\p298\P2lnk" /deny *S-1-1-0:(WD,AD)
oscheck $T\p298\P2lnk\d.vwx
oscheck $T\p298\P2real\f.vwx
rect b
saveto $T\p298\P2lnk\d.vwx
docs
final $T\p298\P2lnk\d.vwx
sh dir /a "$T\p298\P2lnk"
sh dir /a "$T\p298\P2real"
close
docs
sh icacls "$T\p298\P2lnk" /remove:d *S-1-1-0
closefront
docs
sh dir /a "$T\p298\P2lnk"
sh dir /a "$T\p298\P2real"
note P3 図面のパスがリンクのまま閉じて、そのリンクを開き直す
mkdir $T\p298\P3real
mkdir $T\p298\P3lnk
sh mklink "$T\p298\P3lnk\d.vwx" "$T\p298\P3real\f.vwx"
new
rect a
saveto $T\p298\P3lnk\d.vwx
close
docs
sh dir /a "$T\p298\P3lnk"
open $T\p298\P3lnk\d.vwx
docs
closefront
docs
open $T\p298\P3real\f.vwx
docs
closefront
docs
note P4 親のフォルダがリンク・ジャンクションの場面の 2 回目の保存（リンクが残るか）
sh dir /a "$T\p298"
