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
#include "PayloadCatalog.h"
#include "PayloadHost.h"
#include "Update.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
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

		// **素性は 2 つある。** 殻（Vectorworks が起動時に読み込んだこのモジュール）と、
		// 本体（メニューを開くたびに読み直す外部モジュール）。入れ替えで日常的に動くのは
		// 本体だけなので、2 つ並べないと「何が新しくなったのか」が分からない。
		// どちらもビルド ID を出す——更新の新旧を比べるのがこれで、「更新されない」を
		// 追うときに最初に見たい値になる（Update.h）。
		std::string shellStamp()
		{
			return std::string("殻: ") + VW_BUILD_BRANCH + " " + VW_BUILD_VERSION + " (" +
				   VW_BUILD_TIME + ") id=" + VW_BUILD_ID;
		}

		std::string payloadStamp(const Payload& payload)
		{
			if (!payload.isLoaded())
				return "本体: 読み込めていません";
			return "本体: " + payload.branch() + " " + payload.commit() + " (" +
				   payload.buildTime() + ") id=" + payload.buildId();
		}

		// カタログの見出し（メニューを開いた時点で言えるのはここまで——**本体はまだ
		// 1 つも読み込んでいない**）。
		std::string catalogStamp(const catalog::Catalog& cat)
		{
			if (cat.empty())
				return "カタログ: 読めていません";
			return "カタログ: " + cat.branch + " " + cat.commit + " (" + cat.buildTime +
				   ") id=" + cat.buildId;
		}

		// -------------------------------------------------------------------
		// ピッカーの 1 行ぶん。**カタログから作る**（プローブの素性と、それが入っている
		// 本体の在り処）。選ばれて初めて、その本体を読み込む。
		struct Choice
		{
			catalog::Probe probe;
			catalog::Group group;
			std::string payloadPath; // 殻の隣の .vwpayload（空なら在り処が割り出せない）
			bool available = false; // その本体が実際に置かれているか
		};

		// 出所を 1 行に畳む（無ければ「ローカル」）。**同じ形をペイロード側もログの見出し
		// 用に持っている**（plugin/src/payload/PayloadMain.cpp）——境界を跨いで文字列を
		// 組み立てさせるより、それぞれが自分の表示を組むほうが単純。
		std::string provenanceLine(const catalog::Group& group)
		{
			if (group.commit.empty() && group.pr.empty() && group.branch.empty())
				return "ローカル（出所の記録なし）";

			// 見出しは PR 番号。無ければ取り込み元のブランチ（ふつうは main）を見出しに
			// 使い、**そのときは末尾でブランチを繰り返さない**（実機のログで
			// 「claude/… / 0a0fff2 / claude/…」と 2 度出ていた）。
			std::string line;
			const bool hasPr = !group.pr.empty();
			if (hasPr)
				line += "PR #" + group.pr;
			else
				line += group.branch.empty() ? std::string("main") : group.branch;
			if (!group.commit.empty())
				line += " / " + group.commit;
			if (hasPr && !group.branch.empty())
				line += " / " + group.branch;
			if (!group.prTitle.empty())
				line += " / " + group.prTitle;
			return line;
		}

		// ピッカーの**先頭に置く項目**。プローブではなく「新しいビルドを取り込む」を選ぶ。
		//
		// **なぜメニューの中に置くか**: 入れ替えは起動時にも尋ねる（Update.h）が、それだけ
		// だと Vectorworks を動かしたままビルドを頼んだとき、気付くのに 1 回・反映に 1 回で
		// **再起動が 2 回**要る。ここから確認できれば 1 回で済む。メニュー項目を増やさない
		// のは、増やすたびにワークスペースへの登録が要るため（このプラグインの
		// メニューコマンドは 1 つ、という設計。plugin/README.md）。
		constexpr const char* kUpdateItem = "＊ 新しいプローブビルドを確認して入れ替える…";

		// ピッカーの 1 項目。「どの PR の・どのコミットの・何を調べるプローブか」を
		// この 1 行だけで判断できるようにする（選ぶ前に見えるのはこれだけなので）。
		//
		// **入っていない本体も隠さずに出す。** 群のビルドが落ちれば、その .vwpayload だけが
		// 配られない（他の群は配られる。plugin/CMakeLists.txt）。黙って消すと「なぜ自分の
		// プローブが無いのか」が実機からは分からないので、印を付けて残す。
		std::string pickerItem(const Choice& choice)
		{
			std::string head = "local";
			if (!choice.group.pr.empty())
				head = "#" + choice.group.pr;
			else if (!choice.group.branch.empty())
				head = choice.group.branch;
			else if (!choice.group.commit.empty())
				head = "main";
			if (!choice.group.commit.empty())
				head += " " + choice.group.commit;
			std::string line = head + "  " + choice.probe.title + "  [" + choice.probe.id + "]";
			if (!choice.available)
				line += "  ※本体なし";
			return line;
		}

		// PR 番号を数値で（無ければ -1）。**表示順を決めるためだけ**に使う。
		long prNumberOf(const Choice& choice)
		{
			if (choice.group.pr.empty())
				return -1;
			return std::strtol(choice.group.pr.c_str(), nullptr, 10);
		}

		// 表示順: **PR のものを新しい順に先頭へ**（いま確認したいのはたいてい最新の PR）、
		// その後ろに main 由来を id 昇順で。カタログの並びは決定的なので、この並べ替えも
		// 決定的になる。
		std::vector<size_t> displayOrder(const std::vector<Choice>& all)
		{
			std::vector<size_t> order(all.size());
			for (size_t i = 0; i < all.size(); ++i)
				order[i] = i;
			std::stable_sort(order.begin(), order.end(),
							 [&all](size_t a, size_t b)
							 {
								 const long prA = prNumberOf(all[a]);
								 const long prB = prNumberOf(all[b]);
								 if (prA != prB)
									 return prA > prB;
								 return all[a].probe.id < all[b].probe.id;
							 });
			return order;
		}

		// カタログを読んで、選べるものを並べる。**ここでは本体を 1 つも読み込まない。**
		std::vector<Choice> ReadChoices(catalog::Catalog& cat, std::string& error)
		{
			std::vector<Choice> out;
			const std::string catalogPath = SiblingFilePath(payload::CatalogFileName());
			if (catalogPath.empty())
			{
				error = "カタログ（" + payload::CatalogFileName() +
						"）の置き場所を割り出せませんでした。";
				return out;
			}
			std::string text;
			std::string why;
			if (!ReadTextFile(catalogPath, text, why))
			{
				error = "カタログを読めませんでした。\n" + catalogPath +
						" が殻（プラグイン）の隣にありますか？\n（" + why + "）";
				return out;
			}
			cat = catalog::Parse(text);

			for (const catalog::Probe& probe : cat.probes)
			{
				const catalog::Group* group = cat.groupOf(probe.group);
				if (group == nullptr)
					continue; // 群の行が無い（カタログが壊れている）。飛ばす。
				Choice choice;
				choice.probe = probe;
				choice.group = *group;
				choice.payloadPath = SiblingFilePath(group->file);
				choice.available = FileExists(choice.payloadPath);
				out.push_back(choice);
			}
			if (out.empty() && error.empty())
				error = "カタログにプローブがありません。";
			return out;
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
			CProbePickerDialog(const std::string& prompt, const std::string& stamp,
							   const std::vector<TXString>& items, short initialSelection)
				: fPrompt(kPromptID), fStamp(kStampID), fWarning(kWarningID), fPopup(kPopupID),
				  fPromptText(prompt.c_str()), fStampText(stamp.c_str()), fItems(items),
				  fSelection(initialSelection)
			{
			}
			~CProbePickerDialog() override = default;

			short GetSelection() const
			{
				return fSelection;
			}

			// **実際に出せたか。** 組めなかったときは呼び出し側が素のアラートへ落とす
			// （Findings「Layout Dialogs」——出せなかったときの逃げ道を必ず持つ）。
			bool Shown() const
			{
				return fShown;
			}

		protected:
			bool CreateDialogLayout() override
			{
				if (!this->CreateDialog("SDK 実機プローブ", "実行", "キャンセル", false))
					return false;
				if (!fPrompt.CreateControl(this, fPromptText))
					return false;
				if (!fPopup.CreateControl(this, 72 /* width in standard chars */))
					return false;
				if (!fWarning.CreateControl(
						this,
						"※ プローブは図面を変更します。作業中の図面では実行しないでください。"))
					return false;
				if (!fStamp.CreateControl(this, fStampText))
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
				fShown = true;
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
			TXString fPromptText;
			TXString fStampText;
			std::vector<TXString> fItems;
			short fSelection;
			bool fShown = false;
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
		// プローブのログを溜める器。**本体から C の関数ポインタで呼ばれる**ので、
		// 例外を絶対に外へ出さない（越えた先は本体で、巻き戻せない）。
		struct LogCollector
		{
			std::string text;
		};

		void CollectLine(void* ctx, const char* line)
		{
			try
			{
				if (ctx == nullptr)
					return;
				LogCollector* collector = static_cast<LogCollector*>(ctx);
				collector->text += (line != nullptr) ? line : "";
				collector->text += '\n';
			}
			catch (...)
			{
				// 握り潰す（1 行落ちるだけ。境界を壊すよりよい）
			}
		}

		// -------------------------------------------------------------------
		// プローブ 1 件を走らせて、結果ダイアログの見出しを組み立てる。
		//
		// **走らせるのは本体（ペイロード）側**（plugin/src/payload/PayloadMain.cpp）。
		// 例外も undo の記録も所要時間もあちらが持っていて、こちらは結果を受け取って
		// 見せるだけ。ログはこの呼び出しの間に collector へ 1 行ずつ流れてくる。
		std::vector<std::string> RunProbe(Payload& payload, const Choice& choice,
										  const catalog::Catalog& cat)
		{
			std::string outcome;
			std::string logPath;
			double seconds = 0.0;
			std::string error;
			if (!payload.run(choice.probe.id, outcome, logPath, seconds, error))
				outcome = "走らせられなかった: " + error;

			char elapsed[64] = {0};
			(void)std::snprintf(elapsed, sizeof(elapsed), "%.2f", seconds);

			std::vector<std::string> body;
			body.push_back("プローブ: " + choice.probe.title);
			body.push_back("出所: " + provenanceLine(choice.group));
			if (!choice.probe.summary.empty())
				body.push_back("概要: " + choice.probe.summary);
			body.emplace_back("");
			body.push_back("結果: " + outcome);
			body.push_back(std::string("所要: ") + elapsed + " 秒");
			if (!logPath.empty())
				body.push_back("ログ: " + logPath);
			body.emplace_back("");
			body.push_back(payloadStamp(payload) + "  [群 " + choice.group.id + "]");
			body.push_back(catalogStamp(cat));
			body.push_back(shellStamp());
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
	// 0. **カタログを読む（本体はまだ 1 つも読み込まない）。** どの本体に何が入って
	//    いるかは、ビルドが並べて置いたテキスト 1 枚に書いてある
	//    （plugin/src/PayloadCatalog.h）。読み込むのは**選ばれた 1 本だけ**——群の
	//    どれかが壊れていても、他の群は選んで走らせられる。
	catalog::Catalog cat;
	std::string catalogError;
	const std::vector<Choice> all = ReadChoices(cat, catalogError);
	const std::vector<size_t> order = displayOrder(all);

	// 1. 選ばせる。**先頭は「新しいビルドに入れ替える」**で、その後ろにプローブが並ぶ
	//    （カタログを読めなかったときでも、入れ替えだけは選べる——たいていそれが直し方）。
	std::vector<TXString> items;
	items.reserve(order.size() + 1);
	items.emplace_back(kUpdateItem);
	for (const size_t index : order)
		items.emplace_back(pickerItem(all[index]).c_str());

	std::string prompt = "実行するプローブを選んでください:";
	if (all.empty())
		prompt = "プローブがありません。新しいビルドを取り込めます:";

	std::string stamp = catalogStamp(cat) + "  /  " + shellStamp();
	if (cat.skippedLines > 0)
		stamp += "  ※カタログに読めない行が " + std::to_string(cat.skippedLines) + " 行";

	// 既定の選択は**先頭のプローブ**（あれば）。入れ替えは意識して選ぶものにする。
	CProbePickerDialog picker(prompt, stamp, items, all.empty() ? 0 : 1);
	const bool accepted = (picker.RunDialogLayout("") == VWFC::VWUI::kDialogButton_Ok);
	if (!picker.Shown())
	{
		// ダイアログを組めなかった。**黙って終わらない**——プローブは 1 件も走らない
		// ので、なぜ何も起きなかったのかを伝える（Findings「Layout Dialogs」）。
		gSDK->AlertInform("プローブの選択ダイアログを組めませんでした。", stamp.c_str(), false);
		return;
	}
	if (!accepted)
		return; // キャンセルなら静かに終える
	const short selection = picker.GetSelection();
	if (selection < 0)
		return;
	if (selection == 0)
	{
		// 先頭 = 入れ替え。確認・ダウンロード・再起動の案内はすべて Update.cpp が持つ
		// （例外もあちらで受け止める）。**本体は読み込んでいない**ので、ここで降ろす
		// ものは無い（Windows でも入れ替えが必ず通る）。
		RunManualUpdateCheck();
		return;
	}

	if (all.empty())
	{
		// プローブを選べる状態ではない（一覧が空なので、ここへは来ないはずだが念のため）。
		gSDK->AlertInform("プローブの一覧を読めませんでした。", catalogError.c_str(), false);
		return;
	}

	const size_t choiceIndex = size_t(selection) - 1; // 先頭の 1 項目ぶんずらす
	if (choiceIndex >= order.size())
		return;
	const Choice& choice = all[order[choiceIndex]];

	if (!choice.available)
	{
		// その群の本体が配られていない（たいていはその PR のビルドが落ちた）。
		// **何が起きているかを言う**——黙って何も起きないのが一番たちが悪い。
		const std::string why = "この本体は入っていません（" + choice.group.file +
								"）。\nその群のビルドが通らなかったか、入れ替えが途中で"
								"止まっています。\n他の群のプローブはそのまま選べます。";
		gSDK->AlertInform(why.c_str(), provenanceLine(choice.group).c_str(), false);
		return;
	}

	// 2. **選ばれた群の本体だけ**を読み込む。読み終わったら必ず降ろす。
	LogCollector collector;
	Payload payload;
	std::string loadError;
	if (!payload.load(choice.payloadPath, (void*)gCBP, &collector, &CollectLine, loadError))
	{
		gSDK->AlertInform("本体を読み込めませんでした。", loadError.c_str(), false);
		return;
	}

	// **カタログと本体が食い違っていないか。** 入れ替えが半端に済んだ（カタログだけ
	// 新しい・本体だけ古い）と、選んだプローブがその本体に無いことがある。走らせて
	// 「知らない id」と言われる前に、何が起きているかを言う。
	bool inPayload = false;
	for (const PayloadProbeInfo& entry : payload.probes())
	{
		if (entry.id == choice.probe.id)
		{
			inPayload = true;
			break;
		}
	}
	if (!inPayload)
	{
		const std::string why = "カタログと本体が食い違っています（" + choice.probe.id +
								" がこの本体にありません）。\n新しいビルドに入れ替えてください。";
		// **降ろす前に**素性を控える（降ろした後だと「読み込めていません」しか言えない）。
		const std::string stampNow = payloadStamp(payload);
		payload.unload();
		gSDK->AlertInform(why.c_str(), stampNow.c_str(), false);
		return;
	}

	// 3. 走らせる（例外は本体側が受け止める）。
	const std::vector<std::string> body = RunProbe(payload, choice, cat);

	// 4. **本体を降ろしてから**結果を見せる。ダイアログを出している間に本体を抱えたまま
	//    にしない（その間に入れ替えを試されると Windows で失敗する）。ログはこちらの
	//    collector に写してあるので、降ろしても失わない。
	const std::string logText = collector.text;
	payload.unload();
	ShowResult(body, logText);
}
