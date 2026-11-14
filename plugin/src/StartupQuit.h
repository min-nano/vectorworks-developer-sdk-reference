//
//	StartupQuit.h
//
//	**起動の途中の刻みから終了を頼む記録**（issue #312。測るための道具）。
//
//	プローブは本体に入っていて、走らせるまで読み込まれない。だから「起動画面の頃の刻み」や
//	「起動時の『開発元不明のプラグイン』のダイアログの最中の刻み」から何かを呼ぶことは、
//	プローブにはできない——VW が起動時に読み込む**殻**の `plugin_module_main` から時計を
//	動かすしかない（#224 の「殻の時計の最初の刻み」と同じ事情）。
//
//	**一時ディレクトリに頼み札 `VwSdkProbes-startup-quit.request` があるときだけ**動く。札は
//	読んだ時点で `.used` へ改名する（置いたままだと、起動のたびに終わってしまう）。札が
//	無ければ exists を 1 度見るだけで何もしない。札の中身は 1 行で、いつ呼ぶか:
//
//	    first       最初の刻みで呼ぶ
//	    ms=<N>      時計を動かしてから N ms 以後の最初の刻みで呼ぶ
//	    modal=<N>   起動のモーダル（mac は NSModalPanelRunLoopMode、Windows は自プロセスの
//	                題のある有効な #32770）を N ms 見続けた刻みで呼ぶ
//	    go          一時ディレクトリに `VwSdkProbes-startup-quit.go` が現れた刻みで呼ぶ
//	                （人や AI が画面を見て「いま」と合図する。起動を終えた直後など）
//	    never       呼ばない（起動の並びだけ記録する）
//	    mark=<名前>[+<ms>]
//	                目印が初めて見えてから ms 以後の最初の刻みで呼ぶ（issue #317）。名前は
//	                通知の 4 文字（`WKSL` `NWKS` `AppA` `DlgO` など）・`home`（`VW User Log.txt`
//	                に `Section = HomeScreen` の行が現れた）・`endofstartup`（同 `End of
//	                Startup`）・`ready`（`IsDocumentReady()` が真）・`ws`（
//	                `GetCurrentWorkspaceFileName` が空でない）
//
//	2 行目以降は選択肢。`reg=init` を書くと、通知を `plugin_module_main` の中で登録する
//	（既定は最初の刻みで登録する）。
//
//	**起動を終えた目印の候補も並べて書く**（issue #317）: 登録した通知（`kSqNotifies`）の
//	到着、刻みごとの `IsDocumentReady()` / `GetCurrentWorkspaceFileName` / mac の NSApp の
//	見えている窓（主窓・モーダル）の変わり目、`VW User Log.txt` に増えた起動の行
//	（`startupTIMER` / `HomeScreen` / `SHUTDOWN`）。目印が初めて見えたら `mark <名前>` を書く。
//
//	呼ぶのは `CloseAllFilesAndQuitVectorworks(true, false)` を 1 度だけ。刻み（50 ms。mac は
//	`kCFRunLoopCommonModes`、Windows は留めた殻の `SetTimer(nullptr, …)`）が、状態（モード・
//	図面の数・Windows は自プロセスの窓）の変わり目・呼び出しの前後・戻った後の刻み・
//	`kNotifyBeforeApplicationQuit`・`atexit` を `VwSdkProbes-startup-quit.txt` へ 1 行ずつ
//	書いて閉じる（落ちても終わっても、そこまでが残る）。
//
//	調べ終えたら外す（#228 の起動直後の記録と同じ扱い）。
//
#pragma once

namespace vwprobe
{
	// plugin_module_main から呼ぶ。何度呼ばれても 1 度しか動かさない。
	void StartStartupQuitWatch();
} // namespace vwprobe
