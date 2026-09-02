//
//	ProbeMenu.cpp
//
//	メニューコマンドの実装（ProbeMenu.h の流れ）。
//
//	ダイアログの作法は SDK リファレンスの Findings「Layout Dialogs」に従う:
//
//	  * CreateDialog(title, ok, cancel, hasHelp)  … 枠（ID 1=OK / 2=キャンセルは予約）
//	  * AddFirstGroupControl / AddBelowControl    … 上から順にコントロールを積む
//	  * OnInitializeContent()                     … コントロールができた後に中身を流す
//	  * OnDDXInitialize()                         … 値の結び付け（純粋仮想。空でも要る）
//
//	【本文は 1 行 1 コントロール】VWStaticTextCtrl は 1 行を出すためのもので、埋め込んだ
//	改行がそのまま行になる保証が無い。見出しは改行で切って静的テキストを並べる。
//
//	【ログ欄は VWEditTextCtrl】複数行の編集欄なので**スクロールし、選択してコピーできる**
//	（静的テキストではコピーできず、報告に貼れない）。プローブの出力は「読んで貼る」ため
//	のものなので、結果ダイアログでは**最初から開いておく**（畳む・開くの作り直しはしない
//	——レイアウトの大きさは作るときに 1 度しか決まらない）。
//
//	【出せなかったときの逃げ道】レイアウトを組めなければ gSDK->AlertInform へ落とす。
//	結果を伝えられないまま黙って終わるのが最悪。
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "ProbeMenu.h"
#include "Probe.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <string>
#include <vector>

// IMPLEMENT_VWMenuExtension（下方）は名前空間の外で展開するマクロなので、
// クラス名を素で書けるように名前空間を開いておく（SDK のサンプルと同じ作法）。
using namespace vwprobe;

namespace vwprobe
{
	namespace
	{
		// -------------------------------------------------------------------
		// メニュー項目の定義。SResString の {resource, identifier} は .vwr の中の
		// 文字列を指す（resources/VwSdkProbes.vwr/Strings/…）。
		//
		// Needs = DocIsActive: 文書が開いていないときは VW が自動でグレーアウトする。
		// プローブは図面を触るものが大半なので、文書が無い状態では実行させない
		// （メニュー有効化は Needs フラグで決まる。None にすると常に有効になってしまう）。
		//
		// 関数ローカル static で持つのは、EMenuEnableFlags::DocIsActive が SDK 側（別 TU）の
		// 非ローカル static であり、名前空間スコープ変数の初期化子から参照すると静的
		// 初期化順序に依存するため。
		const SMenuDef& menuDef()
		{
			static const SMenuDef def = {/*Needs*/ EMenuEnableFlags::DocIsActive,
										 /*NeedsNot*/ EMenuEnableFlags::None,
										 /*Title*/ {PLUGIN_VWR_ID, "title"},
										 /*Category*/ {PLUGIN_VWR_ID, "category"},
										 /*HelpText*/ {PLUGIN_VWR_ID, "help"},
										 /*VersionCreated*/ 31,
										 /*VersionModified*/ 0,
										 /*VersionRetired*/ 0,
										 /*OverrideHelpID*/ ""};
			return def;
		}

		// このビルドの素性（ピッカーと結果の見出しに出す 1 行）。
		std::string buildStamp()
		{
			return std::string("ビルド: ") + VW_BUILD_BRANCH + " " + VW_BUILD_VERSION + " (" +
				   VW_BUILD_TIME + ")";
		}

		// 出所を 1 行に畳む（無ければ「ローカル」）。
		std::string provenanceLine(const std::string& id)
		{
			const Provenance* origin = provenanceOf(id);
			if (origin == nullptr)
				return "ローカル（出所の記録なし）";

			// PR 番号があれば PR、無ければ取り込み元のブランチ（ふつうは main）。
			std::string line;
			if (!origin->pr.empty())
				line += "PR #" + origin->pr;
			else
				line += origin->branch.empty() ? std::string("main") : origin->branch;
			if (!origin->commit.empty())
				line += " / " + origin->commit;
			if (!origin->branch.empty())
				line += " / " + origin->branch;
			if (!origin->title.empty())
				line += " / " + origin->title;
			return line;
		}

		// ピッカーの 1 項目。「どの PR の・どのコミットの・何を調べるプローブか」を
		// この 1 行だけで判断できるようにする（選ぶ前に見えるのはこれだけなので）。
		std::string pickerItem(const Probe& probe)
		{
			const Provenance* origin = provenanceOf(probe.id);
			std::string head = "local";
			if (origin != nullptr)
			{
				if (!origin->pr.empty())
					head = "#" + origin->pr;
				else
					head = origin->branch.empty() ? std::string("main") : origin->branch;
				if (!origin->commit.empty())
					head += " " + origin->commit;
			}
			return head + "  " + probe.title + "  [" + probe.id + "]";
		}

		// PR 番号を数値で（無ければ -1）。**表示順を決めるためだけ**に使う。
		long prNumberOf(const std::string& id)
		{
			const Provenance* origin = provenanceOf(id);
			if (origin == nullptr || origin->pr.empty())
				return -1;
			return std::strtol(origin->pr.c_str(), nullptr, 10);
		}

		// 表示順: **PR のものを新しい順に先頭へ**（いま確認したいのはたいてい最新の PR）、
		// その後ろに main 由来を id 昇順で。probes() が id 昇順で決定的なので、この
		// 並べ替えも決定的になる。
		std::vector<size_t> displayOrder()
		{
			const std::vector<Probe>& all = probes();
			std::vector<size_t> order(all.size());
			for (size_t i = 0; i < all.size(); ++i)
				order[i] = i;
			std::stable_sort(order.begin(), order.end(),
							 [&all](size_t a, size_t b)
							 {
								 const long prA = prNumberOf(all[a].id);
								 const long prB = prNumberOf(all[b].id);
								 if (prA != prB)
									 return prA > prB;
								 return all[a].id < all[b].id;
							 });
			return order;
		}

		// -------------------------------------------------------------------
		// プローブを選ぶダイアログ。プルダウン 1 つ＋説明の静的テキスト。
		//
		// **選択に連動して中身を書き換えるコントロールは置かない。** レイアウトの
		// 大きさは作るときに 1 度だけ決まるので、行数の変わる説明を後から差し替えると
		// はみ出す・空くのどちらかになる（Findings「Layout Dialogs」）。選ぶのに要る
		// 情報は 1 項目の文字列へ畳んである（pickerItem）。
		class CProbePickerDialog : public VWDialog
		{
		public:
			CProbePickerDialog(const std::vector<TXString>& items, short initialSelection)
				: fPrompt(kPromptID), fStamp(kStampID), fWarning(kWarningID), fPopup(kPopupID),
				  fItems(items), fSelection(initialSelection)
			{
			}
			~CProbePickerDialog() override = default;

			short GetSelection() const
			{
				return fSelection;
			}

		protected:
			bool CreateDialogLayout() override
			{
				if (!this->CreateDialog("SDK 実機プローブ", "実行", "キャンセル", false))
					return false;
				if (!fPrompt.CreateControl(this, "実行するプローブを選んでください:"))
					return false;
				if (!fPopup.CreateControl(this, 72 /* width in standard chars */))
					return false;
				if (!fWarning.CreateControl(
						this,
						"※ プローブは図面を変更します。作業中の図面では実行しないでください。"))
					return false;
				if (!fStamp.CreateControl(this, buildStamp().c_str()))
					return false;

				this->AddFirstGroupControl(&fPrompt);
				this->AddBelowControl(&fPrompt, &fPopup);
				this->AddBelowControl(&fPopup, &fWarning, 0, 1);
				this->AddBelowControl(&fWarning, &fStamp);
				return true;
			}

			void OnInitializeContent() override
			{
				VWDialog::OnInitializeContent();
				for (const TXString& item : fItems)
					fPopup.AddItem(item);
				if (fSelection >= 0 && size_t(fSelection) < fItems.size())
					fPopup.SelectIndex(size_t(fSelection));
			}

			// 選択した添字を fSelection へ結び付ける（双方向）。
			void OnDDXInitialize() override
			{
				this->AddDDX_PulldownMenu(kPopupID, &fSelection);
			}

			// コントロール個別のイベントは受けないが、VWDialog がマップを要求する。
			DEFINE_EVENT_DISPATH_MAP;

		private:
			enum
			{
				kPromptID = 3,
				kPopupID = 4,
				kWarningID = 5,
				kStampID = 6
			}; // 1 = OK, 2 = キャンセルは予約

			VWStaticTextCtrl fPrompt;
			VWStaticTextCtrl fStamp;
			VWStaticTextCtrl fWarning;
			VWPullDownMenuCtrl fPopup;
			std::vector<TXString> fItems;
			short fSelection;
		};

		// EVENT_DISPATCH_MAP_BEGIN は SDK のマクロ。展開の中で clang-tidy が const を
		// 求める局所変数を作るが、それはマクロ側のコードでこちらのものではない。
		// NOLINTNEXTLINE(misc-const-correctness)
		EVENT_DISPATCH_MAP_BEGIN(CProbePickerDialog);
		EVENT_DISPATCH_MAP_END;

		// -------------------------------------------------------------------
		// 結果ダイアログ。見出し（数行の静的テキスト）＋ログ全文（コピーできる編集欄）。
		class CProbeResultDialog : public VWDialog
		{
		public:
			CProbeResultDialog(const std::vector<std::string>& body, const std::string& log)
				: fLog(kLogID), fLogText(log.c_str()), fBody(body), fHasLog(!log.empty())
			{
			}
			~CProbeResultDialog() override = default;

			// **実際に出せたか**（false なら呼び出し側は素のアラートへ落とす）。
			bool Shown() const
			{
				return fShown;
			}

		protected:
			bool CreateDialogLayout() override
			{
				// キャンセルは空文字＝OK だけのダイアログ。
				if (!this->CreateDialog("SDK 実機プローブ — 結果", "OK", "", false))
					return false;

				TControlID id = kFirstBodyID;
				VWControl* previous = nullptr;
				short pendingSpacing = 0;
				for (const std::string& line : fBody)
				{
					if (line.empty())
					{
						pendingSpacing = 1; // 次の行の前に 1 行ぶん空ける
						continue;
					}
					// **deque に直接作る。** 行数は結果で変わるので器が要るが、vector だと
					// 追加のたびに既存の要素が動く（ダイアログは生存中ずっとコントロールの
					// アドレスを持つ）。deque は追加しても既存の要素を動かさない。
					VWStaticTextCtrl& control = fLines.emplace_back(id++);
					if (!control.CreateControl(this, line.c_str()))
						return false;
					if (previous == nullptr)
						this->AddFirstGroupControl(&control);
					else
						this->AddBelowControl(previous, &control, 0, pendingSpacing);
					previous = &control;
					pendingSpacing = 0;
				}
				if (previous == nullptr)
					return false; // 見出しが空（呼び出し側の誤り）

				if (!fHasLog)
					return true;
				if (!fLog.CreateControl(this, "", kLogWidthChars, kLogHeightLines))
					return false;
				this->AddBelowControl(previous, &fLog, 0, 1);
				return true;
			}

			void OnInitializeContent() override
			{
				VWDialog::OnInitializeContent();
				if (fHasLog)
					fLog.SetText(fLogText);
				fShown = true;
			}

			// 値は集めない（見せるだけ）。それでも純粋仮想なので空実装が要る。
			void OnDDXInitialize() override {}

			DEFINE_EVENT_DISPATH_MAP;

		private:
			enum
			{
				kLogID = 4,
				kFirstBodyID = 10
			};

			// ログ欄の大きさ（標準文字幅・行数）。プローブの出力は 1 行が長くなりがち
			// なので幅を広めに取り、画面に収まる高さで止める。
			static constexpr short kLogWidthChars = 92;
			static constexpr short kLogHeightLines = 20;

			VWEditTextCtrl fLog;
			TXString fLogText;
			std::deque<VWStaticTextCtrl> fLines;
			std::vector<std::string> fBody;
			bool fHasLog = false;
			bool fShown = false;
		};

		// NOLINTNEXTLINE(misc-const-correctness)
		EVENT_DISPATCH_MAP_BEGIN(CProbeResultDialog);
		EVENT_DISPATCH_MAP_END;

		// -------------------------------------------------------------------
		// 結果を見せる（出せなければ素のアラートへ落とす）。
		void ShowResult(const std::vector<std::string>& body, const std::string& log)
		{
			CProbeResultDialog dialog(body, log);
			(void)dialog.RunDialogLayout("");
			if (dialog.Shown())
				return;

			std::string text;
			for (const std::string& line : body)
			{
				text += line;
				text += '\n';
			}
			gSDK->AlertInform(text.c_str(), "", false /* modal */);
		}

		// -------------------------------------------------------------------
		// プローブ 1 件を走らせて、結果ダイアログの見出しを組み立てる。
		//
		// **例外はここで受け止める**（呼び出し元の DoInterface は SDK コールバック）。
		// 落ちたところまでのログはファイルにも残っているので、例外の種類と併せて見せる。
		std::vector<std::string> RunProbe(const Probe& probe, Report& report)
		{
			report.openLog(defaultLogPath(probe.id));
			report.log("=== " + probe.title + " [" + probe.id + "] ===");
			report.log("出所: " + provenanceLine(probe.id));
			report.log(buildStamp());
			// undo イベントの状態は**プローブ自身の観測対象になりうる**ので、前後を記録する
			// （Findings「Undo」——VW は処理の開始時にイベントを開かないが、SDK 内部が
			// 途中で開くことがある）。
			report.log(std::string("undo: before building=") +
					   (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
			report.log("--- ここからプローブ本体 ---");

			const auto started = std::chrono::steady_clock::now();
			std::string aborted;
			try
			{
				if (probe.run != nullptr)
					probe.run(report);
				else
					report.fail("本体が登録されていない（VW_PROBE の書き方を確認）");
			}
			catch (const std::exception& error)
			{
				aborted = (error.what() != nullptr) ? error.what() : "std::exception";
			}
			catch (...)
			{
				aborted = "不明な例外（std::exception ではないもの）";
			}
			const double seconds =
				std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

			report.log("--- プローブ本体ここまで ---");
			report.log(std::string("undo: after building=") +
					   (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));

			std::string outcome = "成功";
			if (!aborted.empty())
				outcome = "例外で中断: " + aborted;
			else if (report.failed())
				outcome = "失敗: " + report.failure();
			report.log("結果: " + outcome);

			char elapsed[64] = {0};
			(void)std::snprintf(elapsed, sizeof(elapsed), "%.2f", seconds);

			std::vector<std::string> body;
			body.push_back("プローブ: " + probe.title);
			body.push_back("出所: " + provenanceLine(probe.id));
			if (!probe.summary.empty())
				body.push_back("概要: " + probe.summary);
			body.emplace_back("");
			body.push_back("結果: " + outcome);
			body.push_back(std::string("所要: ") + elapsed + " 秒");
			if (!report.logPath().empty())
				body.push_back("ログ: " + report.logPath());
			body.emplace_back("");
			body.push_back(buildStamp());
			return body;
		}
	} // namespace
} // namespace vwprobe

// ---------------------------------------------------------------------------
// 拡張機能の一意な ID とユニバーサル名。
//
// NOLINT: IMPLEMENT_VWMenuExtension は SDK のマクロで、展開の中に clang-tidy が const を
// 求める `static VWIID iid` がある（マクロ側のコード）。
// NOLINTBEGIN(misc-const-correctness)
// UUID: 40334bc6-d404-444f-bf47-8cad30f6e8c9
IMPLEMENT_VWMenuExtension(
	/*Extension class*/ CExtMenuProbes,
	/*Event sink*/ CProbeMenu_EventSink,
	/*Universal name*/ PLUGIN_UNIVERSAL_NAME,
	/*Version*/ 1,
	/*UUID*/ 0x40334bc6, 0xd404, 0x444f, 0xbf, 0x47, 0x8c, 0xad, 0x30, 0xf6, 0xe8, 0xc9);
// NOLINTEND(misc-const-correctness)

// ---------------------------------------------------------------------------
vwprobe::CExtMenuProbes::CExtMenuProbes(CallBackPtr cbp) : VWExtensionMenu(cbp, vwprobe::menuDef())
{
}

vwprobe::CExtMenuProbes::~CExtMenuProbes() = default;

// ---------------------------------------------------------------------------
vwprobe::CProbeMenu_EventSink::CProbeMenu_EventSink(IVWUnknown* parent) : VWMenu_EventSink(parent)
{
}

vwprobe::CProbeMenu_EventSink::~CProbeMenu_EventSink() = default;

void vwprobe::CProbeMenu_EventSink::DoInterface()
{
	const std::vector<Probe>& all = probes();
	if (all.empty())
	{
		// プローブが 1 つも入っていないビルド（main に何も無い状態で作った等）。
		gSDK->AlertInform("このビルドにはプローブが入っていません。", buildStamp().c_str(), false);
		return;
	}

	// 1. 選ばせる。
	const std::vector<size_t> order = displayOrder();
	std::vector<TXString> items;
	items.reserve(order.size());
	for (const size_t index : order)
		items.emplace_back(pickerItem(all[index]).c_str());

	CProbePickerDialog picker(items, 0);
	if (picker.RunDialogLayout("") != VWFC::VWUI::kDialogButton_Ok)
		return; // キャンセルなら静かに終える
	const short selection = picker.GetSelection();
	if (selection < 0 || size_t(selection) >= order.size())
		return;

	// 2. 走らせる（例外は RunProbe が受け止める）＋ 3. 結果を見せる。
	Report report;
	const std::vector<std::string> body = RunProbe(all[order[size_t(selection)]], report);
	ShowResult(body, report.text());
}
