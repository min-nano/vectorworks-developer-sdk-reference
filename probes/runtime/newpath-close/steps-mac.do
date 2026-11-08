# [issue #304][issue #305] mac: Windows（steps-win-*.do）と同じ場面を mac で測る。
# 一時ディレクトリ（$TMPDIR）の VwSdkProbes-newpath-close.do へ写し、vw_run_probe probe=newpath-close
# （空図面で）。長いので、=== の区切りで分けて何周かに分けて走らせてよい。
# 見張りが窓の文言を書き、キャンセルか OK を押す。$T は $TMPDIR/VwSdkProbes-newpath-close。
#
# 前もって人（か AI）が用意するもの:
#   - 305-H の「使用中」: #288 と同じく、Vectorworks をもう 1 つ起動して（open -n）、
#     305-H の手前で $T/p305/held.vwx を開いておく。用意できなければ 305-H は飛ばす。
#   - 305-K の古い版: 「ファイル > 取り出す > Vectorworks 2024」で $T/p305/v2024.vwx を作っておく
#     （#288 で使った手）。無ければ 305-K は飛ばす（open が false になるだけ）。
#
# ===== #304 新しいパスへの保存で、親のフォルダに書き込めない =====
note 304-0 基準: 書けるフォルダ
mkdir $T/p304/ok
oscheck $T/p304/ok/new.vwx
new
rect a
saveto $T/p304/ok/new.vwx
tree $T/p304/ok
closefront
note 304-1 親を chmod a-w
mkdir $T/p304/ro
sh chmod a-w "$T/p304/ro"
oscheck $T/p304/ro/new.vwx
new
rect a
saveto $T/p304/ro/new.vwx
docs
tree $T/p304/ro
closefront
sh chmod u+w "$T/p304/ro"
note 304-2 親に deny add_file の ACL（ファイルだけ足せない）
mkdir $T/p304/addfile
sh chmod +a "everyone deny add_file" "$T/p304/addfile"
oscheck $T/p304/addfile/new.vwx
new
rect a
saveto $T/p304/addfile/new.vwx
docs
tree $T/p304/addfile
closefront
sh chmod -N "$T/p304/addfile"
note 304-3 親に deny add_subdirectory の ACL（ファイルは足せるが VW Backup を作れない。Windows ではダイアログが出た）
mkdir $T/p304/addsub
sh chmod +a "everyone deny add_subdirectory" "$T/p304/addsub"
oscheck $T/p304/addsub/new.vwx
new
rect a
saveto $T/p304/addsub/new.vwx
docs
tree $T/p304/addsub
rect b
save
closefront
sh chmod -N "$T/p304/addsub"
note 304-4 VW Backup の場所にファイルがある
mkdir $T/p304/bkfile
write $T/p304/bkfile/VW Backup
new
rect a
saveto $T/p304/bkfile/new.vwx
tree $T/p304/bkfile
closefront
note 304-5 親に Finder のロック（chflags uchg）
mkdir $T/p304/lock
sh chflags uchg "$T/p304/lock"
oscheck $T/p304/lock/new.vwx
new
rect a
saveto $T/p304/lock/new.vwx
tree $T/p304/lock
closefront
sh chflags nouchg "$T/p304/lock"
note 304-6 読み取り専用でマウントしたディスクイメージ（下のフォルダと根）
sh hdiutil create -size 20m -fs APFS -volname P304RO "$T/p304ro.dmg"
sh mkdir -p "$T/mnt-rw" "$T/mnt-ro" && hdiutil attach "$T/p304ro.dmg" -mountpoint "$T/mnt-rw" && mkdir "$T/mnt-rw/sub" && hdiutil detach "$T/mnt-rw"
sh hdiutil attach -readonly "$T/p304ro.dmg" -mountpoint "$T/mnt-ro"
oscheck $T/mnt-ro/sub/new.vwx
new
rect a
saveto $T/mnt-ro/sub/new.vwx
docs
tree $T/mnt-ro
closefront
oscheck $T/mnt-ro/new.vwx
new
rect a
saveto $T/mnt-ro/new.vwx
docs
closefront
sh hdiutil detach "$T/mnt-ro"
docs
#
# ===== #305 CloseDocument() =====
mkdir $T/p305
mark
note 305-E OpenDocumentPath で開いただけ（「閉じる」では確認が出る場面）
new
rect a
saveto $T/p305/d.vwx
closedoc
open $T/p305/d.vwx
ls $T/p305
closedoc
ls $T/p305
back
note 305-F 3 枚のとき、裏から前へ出して閉じる（CLI の close の形）
new
rect a
saveto $T/p305/c.vwx
closedoc
open $T/p305/c.vwx
open $T/p305/d.vwx
back
switch $T/p305/c.vwx
closedoc
back
switch $T/p305/d.vwx
closedoc
back
note 305-G 読み取り専用のファイルを開いたコピー（図形を足してから閉じる回も）
new
rect a
saveto $T/p305/ro.vwx
closedoc
ro $T/p305/ro.vwx
open $T/p305/ro.vwx
docs
closedoc
ls $T/p305
open $T/p305/ro.vwx
rect x
closedoc
rw $T/p305/ro.vwx
back
note 305-H 使用中（もう 1 つの Vectorworks が開いている）のファイルを開いたコピー
open $T/p305/held.vwx
docs
closedoc
ls $T/p305
back
note 305-K 古い版のファイルを開いた変換後の図面
open $T/p305/v2024.vwx
docs
closedoc
ls $T/p305
back
note 305-L1 大きな図面: 無題・描画の前
new
rects 100000
syms 100 10000
count
closedoc
back
note 305-L2 保存してから・描画の前
new
rects 100000
syms 100 10000
saveto $T/p305/big.vwx
closedoc
back
note 305-L3 開いただけ・描画の前
open $T/p305/big.vwx
closedoc
back
note 305-L4 開いて描画させてから
open $T/p305/big.vwx
pump 60000
closedoc
back
docs
