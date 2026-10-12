//
//	McpBridge.h
//
//	**MCP ブリッジ**——同じ計算機で動くローカルの Claude Code が、Vectorworks の中の
//	このプラグインを操作するための受け口。Claude は MCP の道具として
//
//	  * プローブの一覧を読む（`vw_probes`）
//	  * プローブを走らせて、結末とログ全文を受け取る（`vw_run_probe` / `vw_run_all`）
//	  * ログファイルを読み直す（`vw_probe_log`）
//	  * 新しいビルドを入れる（`vw_update`）・Vectorworks を再起動する（`vw_restart`）
//
//	を呼べる。**人がするのは Vectorworks を起動しておくことだけ**になり、「ピッカーから
//	入れ替えて走らせてください」の往復が無くなる。
//
//	    Claude Code ──MCP(stdio)──▶ plugin/mcp/vw-mcp-server.py
//	                                   │ <id>.req.json を書く／<id>.res.json を待つ
//	                                   ▼
//	                             スプール（一時ディレクトリの VwSdkProbes-mcp。BridgeSpool.h）
//	                                   ▲
//	                                   │ 殻の時計（OS のタイマー）が 250 ms ごとに取得して応答する
//	                             Vectorworks（このファイルの StartMcpBridgeClock）
//
//	【出どころ】実プラグイン（vectorworks-plugin-import-ifc-homeskz）の MCP ブリッジ
//	（src/core/Bridge・src/draw/McpBridge・src/Extensions/ExtMcpPalette・
//	scripts/mcp/vw-mcp-server.py）と同じ仕組み。違いは 1 つで、**ブリッジを丸ごと殻に置く**
//	こと——あちらは受け付けを本体に置いたので「本体は自分をアンロードできない」ために更新と
//	再起動を殻へ頼み直す往復が要ったが、こちらの本体はプローブを走らせる間しか読み込まれて
//	いない（ProbeMenu.cpp）ので、殻がそのまま入れ替えられる。**境界（PayloadAbi.h）は
//	変えていない。**
//
//	【なぜ OS のタイマーなのか】SDK に「常時呼ばれる入口」は無く、ウェブパレットの JS タイマーは
//	隠れると間引かれ、図面が無いとパレットが出ない。OS のタイマーはどちらでも届き、そこから
//	gSDK を読み書きできる（Findings「Timers and Notifications」§4。mac / Windows とも実測）。
//	決めごとも同じ節から: **既定のモード（kCFRunLoopDefaultMode）にだけ登録し、undo の記録が
//	開いている間は見送る**（書けば開いている記録へ混ざる。同 §6）。
//
//	【見送る】次の間は受け付けない（要求はスプールに残り、次の刻みで処理する）:
//	  * メニュー（「SDK 実機プローブ…」）が動いている間（ProbeMenuActive）。Windows の
//	    WM_TIMER はモーダルダイアログの中でも配られるので、人がピッカーを出している間に
//	    同じ本体・同じ控えへ割り込ませない。
//	  * undo の記録が開いている間（IsCurrentlyBuildingAnUndoEvent）。
//	  * 起動の直後 10 秒。
//
//	【図面】プローブは図面を変更する。だから `vw_run_probe` は既定で**新規の空図面を開いて
//	から**走らせる（`OpenDocumentPath(nullptr, false)`。Findings「Documents」）。ブリッジが
//	開いた図面は覚えておき、**次に走らせる前・`vw_cleanup`・再起動の前に保存せずに閉じる**
//	——人の図面には触れない（開いた本人であることを fileRef とパスの両方で確かめてから閉じる）。
//	閉じないと、再起動のたびに無題の図面の保存確認が並んで止まる。
//
//	【隠れていても受け付ける理由】相手は**同じ計算機の、同じ利用者の** Claude だけで
//	（スプールは 0700。BridgeSpool.h）、走らせる・入れ替える・再起動するは**頼まれたときにしか
//	起きない**——自発的に何かを起こす時計は持たない。
//
//	【SDK 依存】実装（McpBridge.cpp）は SDK を使う。受け渡しの作法（BridgeSpool.h）と
//	道具が使う純粋な部分（BridgeParse.h）は SDK を知らず、単体テストが押さえている。
//

#pragma once

namespace vwprobe
{
	// **殻の時計を動かす。** plugin_module_main が呼ぶ（何度呼んでも 1 度しか動かさない）。
	// 最初の刻みは起動の 10 秒後。
	void StartMcpBridgeClock();
} // namespace vwprobe
