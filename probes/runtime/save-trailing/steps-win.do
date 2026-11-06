# [issue #299] Windows: 保存先の途中の段の末尾が . か空白のとき、どこへ保存されるか。
# 一時ディレクトリの VwSdkProbes-save-trailing.do へ写し、vw_run_probe probe=save-trailing（空図面で）。
# 末尾が . や空白のフォルダは \\?\ でしか作れないので、mkdir には \\?\ を付ける。
#
note 正規化の字面（ファイルシステムを見ない）
fullpath $T\p299\x.\a.vwx
fullpath $T\p299\x..\a.vwx
fullpath $T\p299\x...\a.vwx
fullpath $T\p299\x \a.vwx
fullpath $T\p299\x  \a.vwx
fullpath $T\p299\x. \a.vwx
fullpath $T\p299\x .\a.vwx
note A1 x. で、x も x. も無い
mkdir $T\p299\A1
new
rect a
saveto $T\p299\A1\x.\a.vwx
docs
sh dir /a /s /b "$T\p299\A1"
closefront
docs
note A2 x.. で、x も x.. も無い
mkdir $T\p299\A2
new
rect a
saveto $T\p299\A2\x..\a.vwx
docs
sh dir /a /s /b "$T\p299\A2"
closefront
docs
note A3 x空白 で、x も x空白 も無い
mkdir $T\p299\A3
new
rect a
saveto $T\p299\A3\x \a.vwx
docs
sh dir /a /s /b "$T\p299\A3"
closefront
docs
note B1 x. で、x だけある
mkdir $T\p299\B1\x
new
rect a
saveto $T\p299\B1\x.\a.vwx
docs
sh dir /a /s /b "$T\p299\B1"
final $T\p299\B1\x.\a.vwx
rect b
saveto $T\p299\B1\x.\a.vwx
docs
fs $T\p299\B1\x\a.vwx
rect c
save
docs
fs $T\p299\B1\x\a.vwx
oscheck $T\p299\B1\x.\a.vwx
close
docs
sh dir /a /s /b "$T\p299\B1"
note B2 x.. で、x だけある
mkdir $T\p299\B2\x
new
rect a
saveto $T\p299\B2\x..\a.vwx
docs
sh dir /a /s /b "$T\p299\B2"
closefront
docs
note B3 x空白 で、x だけある
mkdir $T\p299\B3\x
new
rect a
saveto $T\p299\B3\x \a.vwx
docs
sh dir /a /s /b "$T\p299\B3"
closefront
docs
note C1 x.. で、x.. がある（x は無い）
mkdir \\?\$T\p299\C1\x..
sh dir /a /s /b "$T\p299\C1"
new
rect a
saveto $T\p299\C1\x..\a.vwx
docs
sh dir /a /s /b "$T\p299\C1"
final $T\p299\C1\x..\a.vwx
rect b
saveto $T\p299\C1\x..\a.vwx
docs
rect c
save
docs
oscheck $T\p299\C1\x..\a.vwx
close
docs
sh dir /a /s /b "$T\p299\C1"
note C2 x空白 で、x空白 がある（x は無い）
mkdir \\?\$T\p299\C2\x
sh dir /a /s /b "$T\p299\C2"
new
rect a
saveto $T\p299\C2\x \a.vwx
docs
sh dir /a /s /b "$T\p299\C2"
final $T\p299\C2\x \a.vwx
rect b
saveto $T\p299\C2\x \a.vwx
docs
rect c
save
docs
oscheck $T\p299\C2\x \a.vwx
close
docs
sh dir /a /s /b "$T\p299\C2"
note C3 x. で、x. がある（\\?\ で作る。x は無い）
mkdir \\?\$T\p299\C3\x.
sh dir /a /s /b "$T\p299\C3"
new
rect a
saveto $T\p299\C3\x.\a.vwx
docs
sh dir /a /s /b "$T\p299\C3"
closefront
docs
note E 比較から外れるか（x.\e.vwx を開いたまま、別の図面から x\e.vwx と x.\e.vwx へ）
mkdir $T\p299\E\x
new
rect a
saveto $T\p299\E\x.\e.vwx
docs
new
rect a
saveto $T\p299\E\x\e.vwx
docs
saveto $T\p299\E\x.\e.vwx
docs
fs $T\p299\E\x\e.vwx
closefront
docs
note E2 同じファイルを別の文字列で開く
open $T\p299\E\x\e.vwx
docs
closefront
docs
closefront
docs
note E3 閉じた後に x.\e.vwx の文字列で開く
open $T\p299\E\x.\e.vwx
docs
rect z
save
docs
close
docs
sh dir /a /s /b "$T\p299\E"
