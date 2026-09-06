//
//	issue19-web-palette.cpp — 調査 (issue #19): モードレス（非モーダル）のダイアログ／
//	パレットを出せるか。
//
//	SDK ヘッダに `IExtensionWebPalette` / `VWExtensionWebPalette`
//	（Include/VWFC/PluginSupport/VWExtensionWebPalette.h、
//	Include/Interfaces/VectorWorks/Extension/IExtensionWebPalette.h）が見つかった。
//	公式リファレンス（Info/Plug-in Module.md）が挙げる 4 種類の拡張
//	（Menu / Parametric / Tool / VS Functions）には無いが、同じ `REGISTER_Extension<T>`
//	の枠組みで登録する 5 つ目の拡張種別として存在する（`GROUPID_ExtensionWebPalettes`
//	が他の `GROUPID_Extension*` と並んでいる）。
//
//	ここでは「クラス階層が実際に派生・実装できるか」だけを構文チェックする
//	（-fsyntax-only。実行・リンクはしない）。`REGISTER_Extension` 呼び出し自体は
//	Menu 等の既存の拡張と同じ枠組みなので、ここでは確認しない。
//

#include "VectorworksSDK.h"

using namespace VWFC::PluginSupport;
using namespace VectorWorks::Extension;

namespace Issue19Probe
{
	// JS からの関数呼び出し（ボタン押下・入力送信など）を受け取る側。
	class CPaletteJSProvider : public VWExtensionPaletteJSProvider
	{
	public:
		CPaletteJSProvider(IVWUnknown* parent) : VWExtensionPaletteJSProvider(parent) {}

		DEFINE_WebPalette_DISPATCH_MAP;
	};

	BEGIN_WebPalette_DISPATCH_MAP(CPaletteJSProvider)
	END_WebPalette_DISPATCH_MAP2

	// パレット本体。
	class CWebPalette : public VWExtensionWebPalette
	{
		DEFINE_VWPaletteExtension;

	public:
		virtual TXString VCOM_CALLTYPE GetTitle() override { return TXString("issue19 probe"); }

		virtual void DefineSinks() override
		{
			DefineSink<CPaletteJSProvider>(IID_WebCallbacksProvider);
		}
	};
}

// メニューコマンドの DoInterface から戻った後でも、`gSDK` 経由で表示を切り替えたり
// フォーカスしたりできるはず、という想定を構文だけ確かめる。
void Issue19_Probe_ShowHide(const VWIID& iid)
{
	bool wasVisible = gSDK->GetWebPaletteVisibility(iid);
	gSDK->SetWebPaletteVisibility(iid, !wasVisible);

	VectorWorks::Extension::IWebPaletteFrame* frame = gSDK->GetWebPaletteFrame(iid);
	if (frame != nullptr)
		frame->Focus();
}
