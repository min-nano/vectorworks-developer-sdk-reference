# [issue #298] mac: リンクを指す図面と、sticky ビットのフォルダの上書き保存。
# 一時ディレクトリの VwSdkProbes-save-links.do へ写し、vw_run_probe probe=save-links（空図面で）。
# 警告は見張りが読んでログへ書き、OK を押す。図面のパスが /Applications/VW2026/ へ移ることが
# ある（#288）ので、最後に確かめる。
#
# **T の場面は人の準備が要る**（ほかの利用者が持ち主のファイルは、root でないと作れない）。
# 先にこの .do の N0 までを走らせ、$T/p298/T.vwx ができたら、人が端末で次を行う。
#
#   sudo mkdir /Users/Shared/p298t
#   sudo chmod 1777 /Users/Shared/p298t
#   sudo cp "$T/p298/T.vwx" /Users/Shared/p298t/f.vwx
#   sudo chown root /Users/Shared/p298t/f.vwx
#   sudo chmod 666 /Users/Shared/p298t/f.vwx
#   sudo cp "$T/p298/T.vwx" /Users/Shared/p298t/g.vwx
#   sudo chown root /Users/Shared/p298t/g.vwx
#   sudo chmod 666 /Users/Shared/p298t/g.vwx
#
# そのあと、下の「ここから T の場面」より後を別の .do にして走らせる。
#
note L0 ファイルのシンボリックリンク（基準）
mkdir $T/p298/L0real
mkdir $T/p298/L0lnk
new
rect a
saveto $T/p298/L0real/f.vwx
close
sh ln -s "$T/p298/L0real/f.vwx" "$T/p298/L0lnk/d.vwx"
sh ls -la "$T/p298/L0lnk"
open $T/p298/L0lnk/d.vwx
docs
final $T/p298/L0lnk/d.vwx
oscheck $T/p298/L0lnk/d.vwx
oscheck $T/p298/L0real/f.vwx
rect b
save
docs
final $T/p298/L0lnk/d.vwx
fs $T/p298/L0real/f.vwx
sh ls -la "$T/p298/L0lnk"
sh ls -la "$T/p298/L0real"
rect c
save
docs
final $T/p298/L0lnk/d.vwx
fs $T/p298/L0real/f.vwx
sh ls -la "$T/p298/L0lnk"
sh ls -la "$T/p298/L0real"
close
docs
sh ls -la "$T/p298/L0lnk"
sh ls -la "$T/p298/L0real"
note L1 リンクの先のフォルダに足せない
mkdir $T/p298/L1real
mkdir $T/p298/L1lnk
new
rect a
saveto $T/p298/L1real/f.vwx
close
sh ln -s "$T/p298/L1real/f.vwx" "$T/p298/L1lnk/d.vwx"
open $T/p298/L1lnk/d.vwx
docs
sh chmod a-w "$T/p298/L1real"
oscheck $T/p298/L1lnk/d.vwx
oscheck $T/p298/L1real/f.vwx
rect b
save
docs
final $T/p298/L1lnk/d.vwx
fs $T/p298/L1real/f.vwx
sh ls -la "$T/p298/L1lnk"
sh ls -la "$T/p298/L1real"
sh chmod u+w "$T/p298/L1real"
closefront
docs
sh ls -la "$T/p298/L1lnk"
sh ls -la "$T/p298/L1real"
note L2 リンクのあるフォルダに足せない
mkdir $T/p298/L2real
mkdir $T/p298/L2lnk
new
rect a
saveto $T/p298/L2real/f.vwx
close
sh ln -s "$T/p298/L2real/f.vwx" "$T/p298/L2lnk/d.vwx"
open $T/p298/L2lnk/d.vwx
docs
sh chmod a-w "$T/p298/L2lnk"
oscheck $T/p298/L2lnk/d.vwx
oscheck $T/p298/L2real/f.vwx
rect b
save
docs
final $T/p298/L2lnk/d.vwx
fs $T/p298/L2real/f.vwx
sh ls -la "$T/p298/L2lnk"
sh ls -la "$T/p298/L2real"
sh chmod u+w "$T/p298/L2lnk"
closefront
docs
sh ls -la "$T/p298/L2lnk"
sh ls -la "$T/p298/L2real"
note S0 親のフォルダがシンボリックリンク（基準）
mkdir $T/p298/S0real
new
rect a
saveto $T/p298/S0real/f.vwx
close
sh ln -s "$T/p298/S0real" "$T/p298/S0sym"
open $T/p298/S0sym/f.vwx
docs
final $T/p298/S0sym/f.vwx
rect b
save
docs
fs $T/p298/S0real/f.vwx
sh ls -la "$T/p298/S0real"
close
docs
note O OpenDocumentPath がファイルのリンクで false なら、理由を読む（Windows は false だった）
openv $T/p298/L0lnk/d.vwx
docs
note N2 実体の無いリンクへ saveto（Windows は図面のパスがリンクになり、2 回目でリンクが消えた）
mkdir $T/p298/N2real
mkdir $T/p298/N2lnk
sh ln -s "$T/p298/N2real/f.vwx" "$T/p298/N2lnk/d.vwx"
new
rect a
saveto $T/p298/N2lnk/d.vwx
docs
final $T/p298/N2lnk/d.vwx
sh ls -la "$T/p298/N2lnk"
sh ls -la "$T/p298/N2real"
oscheck $T/p298/N2lnk/d.vwx
rect b
saveto $T/p298/N2lnk/d.vwx
docs
final $T/p298/N2lnk/d.vwx
sh ls -la "$T/p298/N2lnk"
sh ls -la "$T/p298/N2real"
close
docs
note P1 図面のパスがリンクのまま、リンクの先のフォルダに足せない
mkdir $T/p298/P1real
mkdir $T/p298/P1lnk
sh ln -s "$T/p298/P1real/f.vwx" "$T/p298/P1lnk/d.vwx"
new
rect a
saveto $T/p298/P1lnk/d.vwx
sh chmod a-w "$T/p298/P1real"
oscheck $T/p298/P1lnk/d.vwx
oscheck $T/p298/P1real/f.vwx
rect b
saveto $T/p298/P1lnk/d.vwx
docs
sh ls -la "$T/p298/P1lnk"
sh ls -la "$T/p298/P1real"
sh chmod u+w "$T/p298/P1real"
closefront
docs
note P2 図面のパスがリンクのまま、リンクのあるフォルダに足せない
mkdir $T/p298/P2real
mkdir $T/p298/P2lnk
sh ln -s "$T/p298/P2real/f.vwx" "$T/p298/P2lnk/d.vwx"
new
rect a
saveto $T/p298/P2lnk/d.vwx
sh chmod a-w "$T/p298/P2lnk"
oscheck $T/p298/P2lnk/d.vwx
oscheck $T/p298/P2real/f.vwx
rect b
saveto $T/p298/P2lnk/d.vwx
docs
sh ls -la "$T/p298/P2lnk"
sh ls -la "$T/p298/P2real"
sh chmod u+w "$T/p298/P2lnk"
closefront
docs
note N0 T の場面の種を作る
new
rect a
saveto $T/p298/T.vwx
close
docs
# ---- ここから T の場面（人の準備の後に、別の .do で）----
note T0 sticky のフォルダの、ほかの利用者のファイル（書き込みは許す）
sh ls -lOde /Users/Shared/p298t
sh ls -lOe /Users/Shared/p298t
open /Users/Shared/p298t/f.vwx
docs
oscheck /Users/Shared/p298t/f.vwx
rect b
save
docs
oscheck /Users/Shared/p298t/f.vwx
fs /Users/Shared/p298t/f.vwx
sh ls -lOe /Users/Shared/p298t
rect c
save
docs
fs /Users/Shared/p298t/f.vwx
sh ls -lOe /Users/Shared/p298t
close
docs
sh ls -lOe /Users/Shared/p298t
sh ls -la /Applications/VW2026 | grep -E ' f\.vwx|VW Backup'
note T1 別名の保存（新しいファイルを足すのは sticky でも通るはず。対照）
new
rect a
saveto /Users/Shared/p298t/new.vwx
docs
rect b
save
docs
sh ls -lOe /Users/Shared/p298t
close
docs
note T2 自分のファイルを sticky のフォルダで上書き（対照）
open /Users/Shared/p298t/new.vwx
rect c
save
docs
close
docs
