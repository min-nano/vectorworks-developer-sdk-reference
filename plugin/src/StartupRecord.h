//
//	StartupRecord.h
//
//	**起動直後の記録**——Vectorworks が起動してから最初の刻みで、殻の時計（McpBridge.cpp の
//	OS のタイマー）から `GetOpenFilesList()` の件数と `GetCurrentLayer()` を読み、一時
//	ディレクトリのファイルへ書く。
//
//	【なぜ殻か】プローブ（本体）はメニューかブリッジからしか走らず、メニューは図面が無いと
//	グレーアウトし、ブリッジ（MCP サーバ）は mac のローカルにしか無い。だから**「図面を
//	開かずに起動した直後」の刻みから SDK を呼ぶ**には、起動時から動いている殻の時計に
//	載せるしかない（issue #224 で Windows の起動直後を測るために足した）。
//
//	【頼まれたときだけ】一時ディレクトリに頼み札
//	`VwSdkProbes-startup-record.request` があるときだけ記録する（プローブが置く）。
//	起動のたびに読むのは札の有無だけで、SDK は呼ばない。**札は最初の刻みで、SDK を呼ぶ
//	前に消す**——記録中に Vectorworks が落ちても、次の起動では記録しない（落ち続けない）。
//
//	記録は `VwSdkProbes-startup-record.txt`。変わった刻みだけを 1 行ずつ書き、件数が
//	変わった刻みでは**呼ぶ前に**書く（落ちても最後の行が残る）。図面が開いてから 20 刻み、
//	または 3 分で終える。読むのはプローブ（記録を報告して消す）。
//

#pragma once

namespace vwprobe
{
	// 殻の時計の刻みごとに呼ぶ（McpBridge.cpp の ClockTick。起動直後の待ちより前）。
	// 札が無ければ最初の 1 回で止まり、以後は何もしない。例外は外へ出さない。
	void StartupRecordTick();
} // namespace vwprobe
