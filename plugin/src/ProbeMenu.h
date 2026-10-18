//
//	ProbeMenu.h
//
//	プラグインが登録する**唯一のメニューコマンド**「SDK 実機プローブ…」。実行すると
//	このビルドに同居しているプローブ（＝どの PR から取り込んだ調査コードか）を選ぶ
//	ダイアログが出て、選んだものをその場で走らせ、結果を 1 枚で見せる。
//
//	流れ:
//	  1. 一覧を作る（vwprobe::probes() ＋ 出所表）
//	  2. ピッカー（プルダウン 1 つ）で 1 件選ばせる
//	  3. 走らせる（例外は必ずここで受け止める——**未捕捉例外は VectorWorks ごと落とす**）
//	  4. 結果ダイアログ（見出し数行＋ログ全文のテキスト欄）
//
//	**一覧をすべて順に走らせる道もある**（ピッカーの `＊ 一覧のプローブをすべて順に実行…`）。
//	1 件ずつ選び直す理由が無いため。走らせる道は単発と同じ 1 本（ProbeMenu.cpp の
//	RunOne）で、違うのは**結果を最後に 1 枚へまとめる**ことだけ——1 件ごとにダイアログを
//	出したら、件数ぶんクリックが要る。
//
//	実装（ProbeMenu.cpp）の作法は SDK リファレンスの Findings「Layout Dialogs」に従う。
//

#pragma once

#include "VectorworksSDK.h"

#include <cstddef>
#include <string>
#include <vector>

namespace vwprobe
{
	using namespace VWFC::PluginSupport;

	// メニュー項目を実行したときの本体。
	class CProbeMenu_EventSink : public VWMenu_EventSink
	{
	public:
		explicit CProbeMenu_EventSink(IVWUnknown* parent);
		~CProbeMenu_EventSink() override;

		void DoInterface() override;
	};

	// メニューコマンド拡張そのもの。
	class CExtMenuProbes : public VWExtensionMenu
	{
		DEFINE_VWMenuExtension;

	public:
		explicit CExtMenuProbes(CallBackPtr cbp);
		~CExtMenuProbes() override;
	};

	// -----------------------------------------------------------------------
	// **MCP ブリッジ向けの口**（McpBridge.h）。ダイアログを 1 枚も出さず、何も尋ねず、
	// **PR / issue へも投稿しない**。走らせる道はメニューと同じ（ProbeMenu.cpp の RunOne）
	// ——**結果ダイアログに出すもの・ブリッジが返すものが食い違わない**ように、経路を
	// 増やさない。

	// メニュー（DoInterface）が動いているか。動いている間、ブリッジは受け付けを見送る
	// （Windows の OS タイマーはモーダルダイアログの中でも届くため）。
	bool ProbeMenuActive();

	// カタログの 1 件（ピッカーの 1 行と同じ並び＝PR の新しい順、次に main）。
	struct QuietProbe
	{
		std::string id;
		std::string title;
		std::string summary;
		std::string group;
		std::string pr;
		std::string issue;
		std::string commit;
		std::string branch;
		std::string prTitle;
		std::string file; // 本体のファイル名
		bool available = false; // 本体が置かれているか（false はピッカーの「※本体なし」）
	};

	struct QuietCatalog
	{
		std::vector<QuietProbe> probes;
		std::string catalogStamp;
		std::string shellStamp;
		std::string buildId; // カタログの build=（いま入っている本体のビルド ID）
		std::string error;	 // カタログを読めなかった理由
		std::size_t skippedLines = 0;
	};

	QuietCatalog ListProbesQuietly();

	// 走らせる 1 件（slug と群）。
	struct QuietTarget
	{
		std::string id;
		std::string group;
	};

	// 1 件ぶんの結末。
	struct QuietRun
	{
		std::string id;
		std::string group;
		bool ran = false;	 // 走らせるところまで行けた
		bool failed = false; // プローブ自身が失敗した
		double seconds = 0.0;
		std::string outcome; // 1 行の結末（走らせられなければその理由）
		std::string advice;
		std::string log; // ログ全文
		std::string logPath;
		std::vector<std::string> body; // 結果ダイアログの見出しと同じもの
	};

	// which を順に走らせる。**走らせられない件は飛ばして続ける**（一括実行と同じ）。
	//
	// **結果は PR / issue へ投稿しない**（Feedback.h）。ログは呼び出し側（MCP ブリッジ）へ
	// 直に返り、それを受け取った Claude が読む——同じログを PR へも流すと、読み手の無い
	// コメントが PR に積もるだけになる。
	std::vector<QuietRun> RunProbesQuietly(const std::vector<QuietTarget>& which);
} // namespace vwprobe
