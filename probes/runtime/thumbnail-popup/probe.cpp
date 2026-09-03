//
//	probes/runtime/thumbnail-popup/probe.cpp
//
//	**サムネイル付きのリソース選択を、自前のレイアウトダイアログで実機に出す。**
//
//	SDK の実装ソースからは次まで分かっている（Findings「Layout Dialogs」）。
//	  * VWImagePopupCtrl::CreateControl(VWDialog*) は生成呼び出しがコメントアウト
//	    された「return false」のスタブ（ImagePopupCtrl.cpp）。
//	  * 同じ eCompThumbnailPopup を指す VWThumbnailPopupCtrl は実装が生きていて、
//	    gSDK->CreateCustomThumbnailPopup(...) → VWControl::CreateControl(pDlg)。
//
//	ソースから読めないのは「実機で本当にコントロールが出るか」だけなので、それを
//	確かめる。ついでに、宣言だけでは決まらない次の 3 つも同じ 1 周で測る。
//	  1. 未選択のときの GetSelectedItemIndex()（ソース上は (size_t)-1）
//	  2. 選択イベントの届き方（SetImagePopupResourceAdvanced を立てた状態で、
//	     そのコントロール ID へ来たイベントを全部ログへ出す）
//	  3. 選んだ項目から元のリソースへ辿れるか（GetSelectedItem() の InternalIndex
//	     → InternalIndexToNameN の名前が、一覧の名前と一致するか）
//
//	図面は変更しない（読むだけ）。**シンボル定義が 1 つ以上ある図面で走らせること。**
//

#include "Probe.h"

#include <string>

namespace
{
	using namespace VWFC::VWUI;

	// 1 = OK / 2 = キャンセルは予約。
	enum
	{
		kPromptID = 3,
		kImagePopupID = 4, // VWImagePopupCtrl（作れないはずの方）
		kThumbID = 5,      // VWThumbnailPopupCtrl（作れるはずの方）
		kHintID = 6
	};

	class CThumbnailPopupDialog : public VWDialog
	{
	public:
		CThumbnailPopupDialog(vwprobe::Report& report, VWFC::Tools::VWResourceList& list)
			: fProbe(report), fList(list), fPrompt(kPromptID), fHint(kHintID),
			  fImagePopup(kImagePopupID), fThumb(kThumbID)
		{
		}
		~CThumbnailPopupDialog() override = default;

		bool Shown() const
		{
			return fShown;
		}
		bool ThumbCreated() const
		{
			return fThumbCreated;
		}
		bool ImagePopupCreated() const
		{
			return fImagePopupCreated;
		}

	protected:
		bool CreateDialogLayout() override
		{
			if (!this->CreateDialog("画像ポップアップの実機確認", "OK", "キャンセル", false))
			{
				fProbe.fail("CreateDialog が false（ダイアログの枠を作れない）");
				return false;
			}
			if (!fPrompt.CreateControl(this, "シンボル定義をサムネイルから選んでください:"))
				return false;

			// (A) 作れないはずの方。**ここで false になっても続ける**——それが分かって
			// いる結論の裏取りであって、このダイアログの目的ではない。
			fImagePopupCreated = fImagePopup.CreateControl(this);
			fProbe.log(std::string("VWImagePopupCtrl::CreateControl -> ") +
					   (fImagePopupCreated ? "true（ソースの読みと食い違う）" : "false（想定どおり）"));

			// (B) 作れるはずの方。
			fThumbCreated = fThumb.CreateControl(this, kStandardSize);
			fProbe.log(std::string("VWThumbnailPopupCtrl::CreateControl -> ") +
					   (fThumbCreated ? "true" : "false"));

			this->AddFirstGroupControl(&fPrompt);
			// 作れなかったコントロールはレイアウトへ積まない（ID が存在しない）。
			if (fImagePopupCreated)
				this->AddBelowControl(&fPrompt, &fImagePopup);
			if (fThumbCreated)
			{
				this->AddBelowControl(fImagePopupCreated ? static_cast<VWControl*>(&fImagePopup)
														 : static_cast<VWControl*>(&fPrompt),
									  &fThumb);
			}
			if (!fHint.CreateControl(this, "選んだら OK。選ばずに OK でも構いません。"))
				return false;
			this->AddBelowControl(fThumbCreated ? static_cast<VWControl*>(&fThumb)
												: static_cast<VWControl*>(&fPrompt),
								  &fHint, 0, 1);
			return true;
		}

		void OnInitializeContent() override
		{
			VWDialog::OnInitializeContent();
			fShown = true;
			if (!fThumbCreated)
				return;

			// 選択イベント（IsImagePopupSelected 等）を受け取れるようにする。
			// VWImagePopupCtrl::SetAdvanced() と同じ呼び出しを直接行う。
			gSDK->SetImagePopupResourceAdvanced(this->GetControlID(), kThumbID);

			const size_t before = fThumb.GetItemsCount();
			for (size_t i = 0, cnt = fList.GetNumItems(); i < cnt; ++i)
				fThumb.AddImageFromResource(fList.GetListID(), i);
			const size_t after = fThumb.GetItemsCount();
			fProbe.log("項目数: 追加前 " + std::to_string(before) + " → 追加後 " +
					   std::to_string(after) + "（一覧は " + std::to_string(fList.GetNumItems()) +
					   " 件）");

			// 未選択のときの戻り。ソース上は (size_t)-1。
			fProbe.log("未選択の GetSelectedItemIndex() = " + Describe(fThumb.GetSelectedItemIndex()));
		}

		void OnDefaultButtonEvent() override
		{
			if (fThumbCreated)
				ReadSelection("OK");
			VWDialog::OnDefaultButtonEvent();
		}

		// サムネイルポップアップへ来たイベントを全部ログへ出す（届き方そのものが調査対象）。
		void OnThumbEvent(TControlID /*id*/, VWDialogEventArgs& args)
		{
			size_t categoryIndex = size_t(-1);
			std::string kinds;
			if (args.IsImagePopupSelected())
				kinds += " selected";
			if (args.IsImagePopupBeforeOpen())
				kinds += " before-open";
			if (args.IsImagePopupCategoryChanged(categoryIndex))
				kinds += " category-changed(" + std::to_string(categoryIndex) + ")";
			if (kinds.empty())
				kinds = " (どの述語にも当たらない)";
			fProbe.log("イベント:" + kinds);
			ReadSelection("イベント");
		}

		DEFINE_EVENT_DISPATH_MAP;

	private:
		static std::string Describe(size_t index)
		{
			return index == size_t(-1) ? "(size_t)-1" : std::to_string(index);
		}

		void ReadSelection(const std::string& when)
		{
			const size_t index = fThumb.GetSelectedItemIndex();
			fProbe.log(when + ": GetSelectedItemIndex() = " + Describe(index));
			if (index == size_t(-1))
				return;

			const InternalIndex selected = fThumb.GetSelectedItem();
			TXString byInternalIndex;
			if (selected != 0)
				gSDK->InternalIndexToNameN(selected, byInternalIndex);

			TXString byListIndex;
			if (index < fList.GetNumItems())
				fList.GetResourceName(index, byListIndex);

			fProbe.log(when + ": GetSelectedItem() の名前 = '" +
					   std::string(static_cast<const char*>(byInternalIndex)) + "' / 一覧の " +
					   std::to_string(index) + " 番目 = '" +
					   std::string(static_cast<const char*>(byListIndex)) + "'");
			if (byInternalIndex != byListIndex)
				fProbe.log("  → **添字と追加順は一致しない**。InternalIndex 側を使うこと。");
		}

		vwprobe::Report& fProbe;
		VWFC::Tools::VWResourceList& fList;
		VWStaticTextCtrl fPrompt;
		VWStaticTextCtrl fHint;
		VWImagePopupCtrl fImagePopup;
		VWThumbnailPopupCtrl fThumb;
		bool fShown = false;
		bool fThumbCreated = false;
		bool fImagePopupCreated = false;
	};

	// NOLINTNEXTLINE(misc-const-correctness)
	EVENT_DISPATCH_MAP_BEGIN(CThumbnailPopupDialog);
	ADD_DISPATCH_EVENT(kThumbID, OnThumbEvent);
	EVENT_DISPATCH_MAP_END;
} // namespace

VW_PROBE("thumbnail-popup", "サムネイル付きのリソース選択を実機に出す",
		 "VWThumbnailPopupCtrl でシンボル定義のサムネイル選択を作り、選択の読み方と"
		 "イベントの届き方を測る（図面は変更しない。シンボル定義のある図面で）")
{
	// リストはダイアログより長生きさせる（VWResourceList は参照カウント式で、最後の
	// 1 つが消えるときに DisposeResourceList する＝ポップアップの参照先が消える）。
	VWFC::Tools::VWResourceList list;
	list.BuildList(kSymDefNode, true);
	probe.log("シンボル定義の一覧: " + std::to_string(list.GetNumItems()) + " 件（listID=" +
			  std::to_string(long(list.GetListID())) + "）");
	if (list.GetNumItems() == 0)
	{
		probe.fail("図面にシンボル定義が 1 つも無い（サムネイルの出方を確かめられない）");
		return;
	}

	CThumbnailPopupDialog dialog(probe, list);
	const bool accepted = (dialog.RunDialogLayout("") == VWFC::VWUI::kDialogButton_Ok);
	probe.log(std::string("ダイアログ: ") + (dialog.Shown() ? "出た" : "出なかった") + " / " +
			  (accepted ? "OK" : "キャンセル"));

	if (!dialog.Shown())
		probe.fail("ダイアログを出せなかった（CreateDialogLayout が false）");
	else if (!dialog.ThumbCreated())
		probe.fail("VWThumbnailPopupCtrl::CreateControl が false（この道も使えない）");
	if (dialog.ImagePopupCreated())
		probe.fail("VWImagePopupCtrl::CreateControl が true を返した（Findings の記述を直すこと）");
}
