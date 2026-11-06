# [issue #298] Windows の続き: OpenDocumentPath がファイルのリンクで false を返す理由を切り分ける。
#
note O1 対照（実体）
mkdir $T\p298\O1
new
rect a
saveto $T\p298\O1\f.vwx
close
docs
openv $T\p298\O1\f.vwx
docs
closefront
docs
note O2 絶対パスのファイルのリンク
mkdir $T\p298\O2
sh mklink "$T\p298\O2\d.vwx" "$T\p298\O1\f.vwx"
openv $T\p298\O2\d.vwx
docs
closefront
docs
note O3 相対パスのファイルのリンク（同じフォルダの実体）
mkdir $T\p298\O3
new
rect a
saveto $T\p298\O3\f.vwx
close
sh mklink "$T\p298\O3\d.vwx" f.vwx
sh dir /a "$T\p298\O3"
openv $T\p298\O3\d.vwx
docs
closefront
docs
note O4 ハードリンク（リンクでないファイルとして開けるかの対照）
mkdir $T\p298\O4
new
rect a
saveto $T\p298\O4\f.vwx
close
sh mklink /H "$T\p298\O4\d.vwx" "$T\p298\O4\f.vwx"
sh dir /a "$T\p298\O4"
openv $T\p298\O4\d.vwx
docs
rect b
saveto $T\p298\O4\d.vwx
docs
sh dir /a "$T\p298\O4"
fs $T\p298\O4\f.vwx
fs $T\p298\O4\d.vwx
close
docs
