# [issue #294] mac: . と .. と // を含む保存先。残るか、畳まれるか。上書きと「閉じる」が通るか。
# 一時ディレクトリの VwSdkProbes-save-paths.do へ写し、vw_run_probe probe=save-paths（空図面で）。
#
mkdir $T/p294/x
note 7a ..
new
rect a
saveto $T/p294/x/../dd.vwx
docs
rect b
saveto $T/p294/x/../dd.vwx
save
note 7a2 畳んだ文字列で、開いている図面と比べる
new
saveto $T/p294/dd.vwx
closefront
docs
switch $T/p294/dd.vwx
switch $T/p294/x/../dd.vwx
close
docs
note 7b .
new
rect a
saveto $T/p294/./d.vwx
docs
rect b
saveto $T/p294/./d.vwx
save
close
docs
note 7c . が残るなら、畳んだ文字列と比べる
new
rect a
saveto $T/p294/./d3.vwx
new
rect b
saveto $T/p294/d3.vwx
closefront
new
rect c
saveto $T/p294/./d3.vwx
closefront
docs
switch $T/p294/./d3.vwx
switch $T/p294/d3.vwx
close
docs
note 7d 無いフォルダを .. で越える
new
rect a
saveto $T/p294/nox/../dd2.vwx
docs
close
docs
note 7e . と .. の混在（Windows は x/./../d5 が x/d5 へ保存された）
new
rect a
saveto $T/p294/./x/../d4.vwx
docs
close
new
rect a
saveto $T/p294/x/./../d5.vwx
docs
close
new
rect a
saveto $T/p294/x/../../p294/d6.vwx
docs
close
docs
note 8 //
new
rect a
saveto $T/p294//s.vwx
docs
rect b
saveto $T/p294//s.vwx
save
close
docs
note 8b 先頭と途中の //
new
rect a
saveto /$T/p294//s2.vwx
docs
close
docs
tree $T/p294
