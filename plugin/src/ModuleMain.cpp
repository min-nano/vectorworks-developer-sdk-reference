//
//	ModuleMain.cpp
//
//	プラグインモジュールの入口。Vectorworks がビルド済みの .vwlibrary / .vlb を読み込み、
//	plugin_module_main を呼んで「このモジュールが提供する拡張機能」を登録させる。
//
//	このプラグインが登録するのは**メニューコマンド 1 つだけ**（ProbeMenu.h）。プローブ
//	（PR ごとの調査コード）は拡張機能ではなく、そのコマンドの中から選んで走らせる
//	——そうしないと PR が増えるたびにメニュー項目とワークスペースの登録が増えてしまう。
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "ProbeMenu.h"

// Vectorworks が実行時にこのプラグインのリソース（.vwr）を引くときの識別子。
// パッケージされる .vwr の基底名と一致していなければならない（BuildConfig.h）。
const char* DefaultPluginVWRIdentifier()
{
	return PLUGIN_VWR_ID;
}

//------------------------------------------------------------------
// コンパイル時の SDK 版を申告する（VW が読み込んでよいか判断する）。
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_ver()
{
	return SDK_VERSION;
}

//------------------------------------------------------------------
// モジュールの入口。
// 参考: Info/Plug-in Module.md
//
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_main(Sint32 action, void* moduleInfo,
													   const VWIID& iid,
													   IVWUnknown*& inOutInterface, CallBackPtr cbp)
{
	// VCOM（Vectorworks Component Object Model）の初期化。
	::GS_InitializeVCOM(cbp);

	Sint32 reply = 0L;

	using namespace VWFC::PluginSupport;

	// メニューコマンド 1 つだけ。
	REGISTER_Extension<vwprobe::CExtMenuProbes>(GROUPID_ExtensionMenu, action, moduleInfo, iid,
												inOutInterface, cbp, reply);

	return reply;
}
