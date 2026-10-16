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
//	**例外は「拡張機能でなければ調べられないこと」**だけで、そのときは調査の間だけ登録し、
//	終わったら外す（issue #183 で PIO を 1 つ登録した例。Findings「`Recalculate` の中から
//	見える環境」）。issue #185 でも同じことをした（行列を読む口が `Recalculate` の中で
//	何を返すか。Findings「PIO の行列を読む口」）——いまはどちらも外してある。
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "McpBridge.h"
#include "ProbeLibrary.h"
#include "ProbeMenu.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

// Vectorworks が実行時にこのプラグインのリソース（.vwr）を引くときの識別子。
// パッケージされる .vwr の基底名と一致していなければならない（BuildConfig.h）。
const char* DefaultPluginVWRIdentifier()
{
	return PLUGIN_VWR_ID;
}

//------------------------------------------------------------------
// **頼まれたときだけ、VW が渡してきた action を書き残す**（issue #230）。
//
// `kPluginModuleDeinit`（-2）が終了の時に届くか・いつ届くかは、**VW が読み込む殻にしか
// 見えない**（本体は殻が自分で読み込むので、VW から module_main は呼ばれない）。プローブが
// 一時ディレクトリに頼み札（VwSdkProbes-module-actions.request）を置いている間だけ、
// 1 行ずつ書いて閉じる。札が無ければ exists を 1 度見るだけで何もしない。
// ここでは gSDK を呼ばない（-2 のときに生きている保証が無い）。
namespace
{
	void RecordModuleAction(Sint32 action)
	{
		std::error_code ec;
		const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec || !std::filesystem::exists(dir / "VwSdkProbes-module-actions.request", ec))
			return;
		std::ofstream out(dir / "VwSdkProbes-module-actions.txt", std::ios::app);
		if (!out)
			return;
		const long long wallMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
									 std::chrono::system_clock::now().time_since_epoch())
									 .count();
		out << "module_main action=" << action << " wall_ms=" << wallMs << "\n";
	}
} // namespace

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
	RecordModuleAction(action);

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

	// **「呼ばれる側」のプラグインライブラリルーチン**（plugin/src/ProbeLibrary.h）。
	// プローブ（本体モジュール）から `ISDK::CallPluginLibrary` でモジュールを跨いで
	// 呼べるかを実機で測るために、ここで登録しておく——**登録は殻でしかできない**
	// （VW が番地を握るので、入れ替わる本体には置けない）。
	REGISTER_Extension<vwprobe::CExtProbeLibrary>(
		VectorWorks::Extension::GROUPID_ExtensionVSFunctions, action, moduleInfo, iid,
		inOutInterface, cbp, reply);

	// **MCP ブリッジの時計を動かす**（McpBridge.h）。ローカルの Claude Code がプローブの
	// 一覧・実行・ログ・入れ替え・再起動を頼めるようにする受け口で、ネットワークは使わない
	// （一時ディレクトリのファイルで受け渡す）。最初の刻みは 10 秒後なので、ここでは
	// 何も待たない。何度呼ばれても 1 度しか動かさない。
	vwprobe::StartMcpBridgeClock();

	return reply;
}
