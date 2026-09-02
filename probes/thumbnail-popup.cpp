//
//	thumbnail-popup.cpp — 自前レイアウトダイアログでサムネイル付きリソース選択を
//	作る道（VWThumbnailPopupCtrl）が、意図した引数で呼べるかを確かめる。
//
//	背景: VWImagePopupCtrl::CreateControl(VWDialog*) は SDK の実装が
//	「return false;」のスタブで、レイアウトダイアログでは決して作れない
//	（SDKLib/Source/VWSDK/VWFC/VWUI/ImagePopupCtrl.cpp）。同じ
//	eCompThumbnailPopup を使う VWThumbnailPopupCtrl は
//	gSDK->CreateCustomThumbnailPopup(...) → VWControl::CreateControl(pDlg) と
//	実装されており、こちらが本来の道と読める。その呼び出し一式が
//	SDK ヘッダに対して通るかを見る（実行はしない）。
//

#include "VectorworksSDK.h"

void probe_thumbnail_popup(VWFC::VWUI::VWDialog* pDlg, VWFC::VWUI::VWControl* pAbove)
{
	const VWFC::VWUI::TControlID	kThumbID	= 1001;

	// 図面のシンボル定義一覧（kSymDefNode = 16）。
	VWFC::Tools::VWResourceList		list;
	list.BuildList(kSymDefNode, true);

	// 生成 → レイアウトへの積み方は他のコントロールと同じ。
	VWFC::VWUI::VWThumbnailPopupCtrl	thumb(kThumbID);
	bool	created	= thumb.CreateControl(pDlg, kStandardSize);
	pDlg->AddBelowControl(pAbove, &thumb);

	// 項目の入れ方: リソース一覧の ID と 0 始まりの添字で足す。
	for (size_t i = 0, cnt = list.GetNumItems(); i < cnt; ++i)
	{
		thumb.AddImageFromResource(list.GetListID(), i);
	}

	// 選択の読み方: 添字でも、リソースの内部インデックスでも引ける。
	size_t			selIndex	= thumb.GetSelectedItemIndex();
	InternalIndex	selObject	= thumb.GetSelectedItem();
	InternalIndex	itemObject	= thumb.GetItemObject(0);
	size_t			itemIndex	= thumb.GetObjectItemIndex(selObject);
	size_t			count		= thumb.GetItemsCount();
	bool			selected	= thumb.SelectItem(size_t(0));
	bool			selected2	= thumb.SelectItem(selObject);
	thumb.RemoveAllImages();

	// 選択イベント（IsImagePopupSelected 等）を受けるには advanced を立てる。
	// VWImagePopupCtrl::SetAdvanced() と同じ呼び出しを直接行う。
	gSDK->SetImagePopupResourceAdvanced(pDlg->GetControlID(), kThumbID);

	// ダイアログ側から後で引く道。
	VWFC::VWUI::VWThumbnailPopupCtrl*	pByID	= pDlg->GetThumbnailPopupCtrlByID(kThumbID);

	(void)created; (void)selIndex; (void)itemObject; (void)itemIndex;
	(void)count; (void)selected; (void)selected2; (void)pByID;
}

void probe_thumbnail_popup_event(VWFC::VWUI::TControlID id, VWFC::VWUI::VWDialogEventArgs& args)
{
	size_t	categoryIndex	= 0;
	bool	isSelected		= args.IsImagePopupSelected();
	bool	isBeforeOpen	= args.IsImagePopupBeforeOpen();
	bool	isCategory		= args.IsImagePopupCategoryChanged(categoryIndex);
	(void)id; (void)isSelected; (void)isBeforeOpen; (void)isCategory;
}
