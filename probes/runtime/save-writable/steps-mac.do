# [issue #293] mac: 開いた後で書き込めなくした図面の上書き保存と、呼ぶ前に OS へ聞く候補の答え。
# 一時ディレクトリの VwSdkProbes-save-writable.do へ写し、vw_run_probe probe=save-writable（空図面で）。
# 警告は見張りが読んでログへ書き、OK を押す。2 回目の保存で図面のパスが
# /Applications/VW2026/ へ移ることがある（#288）ので、最後に片付ける。
#
mkdir $T/p293/a
mkdir $T/p293/ro
mkdir $T/p293/lock
mkdir $T/p293/acl
mkdir $T/p293/dir
mkdir $T/p293/diracl
note A 何もしない（基準）
new
rect a
saveto $T/p293/a/f.vwx
oscheck $T/p293/a/f.vwx
rect b
save
oscheck $T/p293/a/f.vwx
close
docs
note R 読み取り専用（chmod a-w。#288 と同じ場面に OS の答えを足す）
new
rect a
saveto $T/p293/ro/f.vwx
sh chmod a-w "$T/p293/ro/f.vwx"
oscheck $T/p293/ro/f.vwx
rect b
save
docs
oscheck $T/p293/ro/f.vwx
sh ls -la "$T/p293/ro"
sh chmod u+w "$T/p293/ro/f.vwx"
closefront
docs
note 1 Finder のロック（chflags uchg）
new
rect a
saveto $T/p293/lock/f.vwx
sh chflags uchg "$T/p293/lock/f.vwx"
sh ls -lO "$T/p293/lock"
oscheck $T/p293/lock/f.vwx
rect b
save
docs
oscheck $T/p293/lock/f.vwx
sh ls -lO "$T/p293/lock"
rect c
save
docs
sh ls -lO "$T/p293/lock"
sh chflags nouchg "$T/p293/lock/f.vwx"
closefront
docs
note 2 書き込みを拒否する ACL
new
rect a
saveto $T/p293/acl/f.vwx
sh chmod +a "everyone deny write" "$T/p293/acl/f.vwx"
sh ls -le "$T/p293/acl"
oscheck $T/p293/acl/f.vwx
rect b
save
docs
oscheck $T/p293/acl/f.vwx
sh ls -le "$T/p293/acl"
rect c
save
docs
sh ls -le "$T/p293/acl"
sh chmod -N "$T/p293/acl/f.vwx"
closefront
docs
note 3a 親のフォルダを chmod a-w
new
rect a
saveto $T/p293/dir/f.vwx
sh chmod a-w "$T/p293/dir"
oscheck $T/p293/dir/f.vwx
rect b
save
docs
oscheck $T/p293/dir/f.vwx
sh ls -la "$T/p293/dir"
rect c
save
docs
sh ls -la "$T/p293/dir"
sh chmod u+w "$T/p293/dir"
closefront
docs
note 3b 親のフォルダに追加を拒否する ACL
new
rect a
saveto $T/p293/diracl/f.vwx
sh chmod +a "everyone deny add_file,add_subdirectory" "$T/p293/diracl"
sh ls -lde "$T/p293/diracl"
oscheck $T/p293/diracl/f.vwx
rect b
save
docs
oscheck $T/p293/diracl/f.vwx
sh ls -la "$T/p293/diracl"
sh chmod -N "$T/p293/diracl"
closefront
docs
tree $T/p293
note 片付け（2 回目の保存でアプリのフォルダへ移ったファイル）
sh ls -la /Applications/VW2026 | grep -E ' f\.vwx|VW Backup'
rm /Applications/VW2026/f.vwx
