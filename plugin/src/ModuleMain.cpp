//
//	ModuleMain.cpp
//
//	プラグインモジュールの入口。Vectorworks がビルド済みの .vwlibrary / .vlb を読み込み、
//	plugin_module_main を呼んで「このモジュールが提供する拡張機能」を登録させる。
//
//	このプラグインが登録するのは**メニューコマンド 1 つ**（ProbeMenu.h）。プローブ
//	（PR ごとの調査コード）は拡張機能ではなく、そのコマンドの中から選んで走らせる
//	——そうしないと PR が増えるたびにメニュー項目とワークスペースの登録が増えてしまう。
//
//	**例外は「拡張機能でなければ調べられないこと」**。いまは issue #183 のために PIO を
//	1 つ登録している（ExtPioRecalcEnv.h）——`Recalculate` の中から何が見えるかは、
//	メニューコマンドからは測れないため。**調査が終わったら外す**。
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "ExtPioRecalcEnv.h"
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

	// **ここでは更新を確認しない。** 起動のたびにネットワークを叩けば、その待ちが
	// Vectorworks の起動に乗る（オフラインなら尚更）。入れ替えは日常だが、**入れ替えたく
	// なるのはプローブを走らせようとしたとき**なので、確認はピッカーの先頭項目を選んだ
	// ときだけに寄せてある（Update.h / ProbeMenu.cpp）。本体（.vwpayload）だけの
	// 入れ替えなら再起動が要らないので、その場で取り込んでそのまま選べる。

	Sint32 reply = 0L;

	using namespace VWFC::PluginSupport;

	// メニューコマンド 1 つ。
	REGISTER_Extension<vwprobe::CExtMenuProbes>(GROUPID_ExtensionMenu, action, moduleInfo, iid,
												inOutInterface, cbp, reply);

	// **調査のための一時的な PIO**（issue #183）。`Recalculate` の中から何が見えるかは
	// メニューコマンドからは測れない——`Recalculate` に立てられるのは自前の PIO の中だけ
	// なので、道具としてここに 1 つ登録する。**役目を終えたら外す**
	// （ExtPioRecalcEnv.h / PioRecalcTrace.h）。
	REGISTER_Extension<vwprobe::CExtObjPioRecalcEnv>(GROUPID_ExtensionParametric, action,
													 moduleInfo, iid, inOutInterface, cbp, reply);

	return reply;
}
