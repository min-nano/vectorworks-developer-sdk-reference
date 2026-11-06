# [issue #298] Windows の続き: 親のフォルダがジャンクション・フォルダのリンク。
#
note J0 親のフォルダがジャンクション（基準）
mkdir $T\p298\J0real
new
rect a
saveto $T\p298\J0real\f.vwx
close
sh mklink /J "$T\p298\J0jn" "$T\p298\J0real"
open $T\p298\J0jn\f.vwx
docs
final $T\p298\J0jn\f.vwx
oscheck $T\p298\J0jn\f.vwx
rect b
save
docs
final $T\p298\J0jn\f.vwx
fs $T\p298\J0real\f.vwx
sh dir /a "$T\p298\J0real"
sh dir /a "$T\p298"
close
docs
note J1 ジャンクションの先のフォルダに足せない
mkdir $T\p298\J1real
new
rect a
saveto $T\p298\J1real\f.vwx
close
sh mklink /J "$T\p298\J1jn" "$T\p298\J1real"
open $T\p298\J1jn\f.vwx
docs
sh icacls "$T\p298\J1real" /deny *S-1-1-0:(WD,AD)
oscheck $T\p298\J1jn\f.vwx
oscheck $T\p298\J1real\f.vwx
rect b
save
docs
fs $T\p298\J1real\f.vwx
sh dir /a "$T\p298\J1real"
sh icacls "$T\p298\J1real" /remove:d *S-1-1-0
closefront
docs
note J2 ジャンクションそのものに足す拒否の ACE（/L。先のフォルダには付けない）
mkdir $T\p298\J2real
new
rect a
saveto $T\p298\J2real\f.vwx
close
sh mklink /J "$T\p298\J2jn" "$T\p298\J2real"
open $T\p298\J2jn\f.vwx
docs
sh icacls "$T\p298\J2jn" /L /deny *S-1-1-0:(WD,AD)
sh icacls "$T\p298\J2jn" /L
sh icacls "$T\p298\J2real"
oscheck $T\p298\J2jn\f.vwx
oscheck $T\p298\J2real\f.vwx
rect b
save
docs
fs $T\p298\J2real\f.vwx
sh dir /a "$T\p298\J2real"
sh icacls "$T\p298\J2jn" /L /remove:d *S-1-1-0
closefront
docs
note S0 親のフォルダがシンボリックリンク（基準）
mkdir $T\p298\S0real
new
rect a
saveto $T\p298\S0real\f.vwx
close
sh mklink /D "$T\p298\S0sym" "$T\p298\S0real"
open $T\p298\S0sym\f.vwx
docs
final $T\p298\S0sym\f.vwx
rect b
save
docs
fs $T\p298\S0real\f.vwx
sh dir /a "$T\p298\S0real"
close
docs
