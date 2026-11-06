# [issue #298] Windows の続き: 開けないリンクの代わりに、saveto でリンクへ保存する。
#
note N1 既にある図面を指すリンクへ、無題の図面を saveto する
new
rect a
saveto $T\p298\L0lnk\d.vwx
docs
final $T\p298\L0lnk\d.vwx
sh dir /a "$T\p298\L0lnk"
sh dir /a "$T\p298\L0real"
closefront
docs
note N2 実体の無いリンク（先のフォルダはある）へ saveto する
mkdir $T\p298\N2real
mkdir $T\p298\N2lnk
sh mklink "$T\p298\N2lnk\d.vwx" "$T\p298\N2real\f.vwx"
sh dir /a "$T\p298\N2lnk"
new
rect a
saveto $T\p298\N2lnk\d.vwx
docs
final $T\p298\N2lnk\d.vwx
sh dir /a "$T\p298\N2lnk"
sh dir /a "$T\p298\N2real"
oscheck $T\p298\N2lnk\d.vwx
oscheck $T\p298\N2real\f.vwx
rect b
saveto $T\p298\N2lnk\d.vwx
docs
final $T\p298\N2lnk\d.vwx
sh dir /a "$T\p298\N2lnk"
sh dir /a "$T\p298\N2real"
closefront
docs
