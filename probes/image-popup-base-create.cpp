//
//	image-popup-base-create.cpp — VWImagePopupCtrl を「基底の CreateControl を
//	明示的に呼ぶ」形で使えるかを確かめる。
//
//	VWImagePopupCtrl::CreateControl(VWDialog*) は return false のスタブだが、
//	基底 VWControl::CreateControl(pDlg) は fpParentDlg を覚えて true を返すだけ
//	（SDKLib/Source/VWSDK/VWFC/VWUI/Control.cpp）。実際のコントロール生成は
//	gSDK->CreateCustomThumbnailPopup が行う（VWThumbnailPopupCtrl と同じ）。
//	つまり「gSDK で作ってから基底の CreateControl で親を覚えさせる」と、
//	VWImagePopupCtrl だけが持つ機能（カテゴリ・区切り・ShowImage）にも
//	手が届くのではないか——という筋の、構文レベルの成立確認。
//

#include "VectorworksSDK.h"

void probe_image_popup_base_create(VWFC::VWUI::VWDialog* pDlg)
{
	const VWFC::VWUI::TControlID	kPopupID	= 1002;

	VWFC::Tools::VWResourceList		list;
	list.BuildList(kSymDefNode, true);

	VWFC::VWUI::VWImagePopupCtrl	popup(kPopupID);

	// 本来の CreateControl は必ず false を返すので、生成は自分で行い、
	// 親ダイアログの記憶だけを基底の実装に任せる（仮想関数の明示呼び出し）。
	gSDK->CreateCustomThumbnailPopup(pDlg->GetControlID(), kPopupID, kStandardSize);
	bool	attached	= popup.VWFC::VWUI::VWControl::CreateControl(pDlg);

	popup.AddItems(list);
	size_t	added		= popup.AddItem(list, 0);
	size_t	sep			= popup.AddItemSeparator(TXString("---"));
	size_t	sel			= popup.GetSelectedItemIndex();
	size_t	count		= popup.GetItemsCount();
	popup.SetAdvanced();

	(void)attached; (void)added; (void)sep; (void)sel; (void)count;
}
