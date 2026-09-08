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
//	結果が PR へ自動で投稿されるようになった以上、1 件ずつ選び直す理由が無いため
//	（Feedback.h）。走らせる道は単発と同じ 1 本（ProbeMenu.cpp の RunOne）で、違うのは
//	**結果を最後に 1 枚へまとめる**ことだけ——1 件ごとにダイアログを出したら、件数ぶん
//	クリックが要る。
//
//	実装（ProbeMenu.cpp）の作法は SDK リファレンスの Findings「Layout Dialogs」に従う。
//

#pragma once

#include "VectorworksSDK.h"

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
} // namespace vwprobe
