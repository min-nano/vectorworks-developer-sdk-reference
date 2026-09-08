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
//	【横幅はいちばん長い行で決まる】レイアウトの大きさは作るときに 1 度だけ決まるので、
//	長い 1 行を混ぜるとダイアログはそこに合わせて横へ伸び、後から縮められない。素性
//	（ブランチ・コミット・ビルド ID・時刻）は 1 行に並べると 90 文字を超えるので、
//	**見出しごとに行を分けて詰める**（組み立ては ProbeMenuText.h。長さは
//	plugin/tests/ProbeMenuTextTests.cpp が見張る）。
//
//	【出せなかったときの逃げ道】レイアウトを組めなければ gSDK->AlertInform へ落とす。
//	結果を伝えられないまま黙って終わるのが最悪。
//
//	【走らせる道は 1 本】単発でも一括（ピッカーの「すべて順に実行」）でも通るのは RunOne
//	で、下ごしらえ・本体の読み込み・実行・PR への投稿はそこに全部ある。**結果ダイアログに
//	出すものと PR へ送るものが食い違わない**ことが要点なので、経路を増やしても組み立ては
//	1 か所に置く。一括のほうは結果を最後に 1 枚へまとめる（RunAll）。
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "ProbeMenu.h"
#include "ProbeMenuText.h"
#include "Alerts.h"
#include "Feedback.h"
#include "FeedbackParse.h"
#include "PayloadCatalog.h"
#include "PayloadHost.h"
#include "Update.h"

#include <algorithm>
#include <cstddef>
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
			return text::StampLine("殻", VW_BUILD_BRANCH, VW_BUILD_VERSION, VW_BUILD_TIME,
								   VW_BUILD_ID);
		}

		std::string payloadStamp(const Payload& payload)
		{
			if (!payload.isLoaded())
				return "本体: 読み込めていません";
			return text::StampLine("本体", payload.branch(), payload.commit(), payload.buildTime(),
								   payload.buildId());
		}

		// カタログの見出し（メニューを開いた時点で言えるのはここまで——**本体はまだ
		// 1 つも読み込んでいない**）。
		std::string catalogStamp(const catalog::Catalog& cat)
		{
			if (cat.empty())
				return "カタログ: 読めていません";
			return text::StampLine("カタログ", cat.branch, cat.commit, cat.buildTime, cat.buildId);
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
		// **ここが更新確認の唯一の入口**（起動時には確認しない。Update.h「いつ走るか」）。
		// Vectorworks を動かしたままビルドを頼んでも、ここから取り込めば本体だけの
		// 入れ替えは再起動なしで済み、そのままピッカーへ戻って新しいプローブを選べる。
		// メニュー項目を増やさないのは、増やすたびにワークスペースへの登録が要るため
		// （このプラグインのメニューコマンドは 1 つ、という設計。plugin/README.md）。
		constexpr const char* kUpdateItem = "＊ 新しいプローブビルドを確認して入れ替える…";

		// ピッカーの 2 番目。**結果を PR へ自動で投稿するかの設定**（Feedback.h）。
		// ふだんは触らない——初めて PR 由来のプローブを走らせるときに 1 度だけ尋ねられ、
		// あとは黙って投稿される。ここは「断ったあとで気が変わった」「トークンの期限が
		// 切れた」ときの入り口で、**メニュー項目を増やさずに済ませる**ための置き場所。
		constexpr const char* kFeedbackItem = "＊ 結果の自動投稿を設定…";

		// ピッカーの 3 番目。**一覧に挙がっているプローブを、上から順に全部走らせる**。
		//
		// 【なぜ要るか】結果は PR へ自動で投稿されるようになった（Feedback.h）ので、
		// 実機でする操作は「走らせる」だけになった。だとすれば**確かめたいものを 1 件ずつ
		// 選び直す理由が無い**——ピッカーを開き直す回数が、そのまま検証の手間になる。
		// ここを選べば、一覧のプローブが順に走り、**結果は最後に 1 枚だけ**出る。
		//
		// メニュー項目でもボタンでもなくピッカーの項目にしてあるのは、入れ替え・設定と
		// 同じ理由——**メニューを増やさない**（増やすたびにワークスペースへの登録が要る）、
		// そして**選択に連動して動くコントロールを置かない**（レイアウトの大きさは作る
		// ときに 1 度だけ決まる。Findings「Layout Dialogs」）ため。
		constexpr const char* kRunAllItem = "＊ 一覧のプローブをすべて順に実行…";

		// ピッカーの頭にある**プローブでない項目**の数（入れ替え・設定・一括実行）。
		// 選ばれた添字をプローブの一覧へ読み替えるときに引く。
		constexpr std::size_t kFixedItems = 3;

		// 一括実行のまとめに**行として並べる**上限。ダイアログの高さは作るときに 1 度
		// だけ決まるので、件数が増えても縦に伸び続けないように頭打ちにする（あふれた
		// ぶんもログ欄には全部入っている）。
		constexpr std::size_t kMaxSummaryLines = 20;

		// ピッカーの幅（標準文字）と、項目に入れる表示名の上限（文字）。**ダイアログの
		// 横幅はここと素性の行で決まる**ので、広げるときは実機で見てから決めること
		// （作った後では縮められない。Findings「Layout Dialogs」）。
		constexpr short kPopupWidthChars = 52;
		constexpr std::size_t kTitleChars = 30;

		// 群を指す**いちばん短い見出し**（`#12` / `main` / ブランチ名）。ピッカーの項目にも
		// 一括実行のまとめにも出るので、組み立ては 1 か所に置く。
		std::string choiceHead(const Choice& choice)
		{
			if (!choice.group.pr.empty())
				return "#" + choice.group.pr;
			if (!choice.group.branch.empty())
				return choice.group.branch;
			if (!choice.group.commit.empty())
				return "main";
			return "local";
		}

		// ピッカーの 1 項目。「どの PR の・どのコミットの・何を調べるプローブか」を
		// この 1 行だけで判断できるようにする（選ぶ前に見えるのはこれだけなので）。
		//
		// **入っていない本体も隠さずに出す。** 群のビルドが落ちれば、その .vwpayload だけが
		// 配られない（他の群は配られる。plugin/CMakeLists.txt）。黙って消すと「なぜ自分の
		// プローブが無いのか」が実機からは分からないので、印を付けて残す。
		std::string pickerItem(const Choice& choice)
		{
			std::string head = choiceHead(choice);
			if (!choice.group.commit.empty())
				head += " " + choice.group.commit;
			// **表示名は詰める。** プルダウンの幅は作るときに決まる（kPopupWidthChars）
			// ので、長い表示名をそのまま入れると末尾の `[slug]` が見切れる——slug は
			// 出所と突き合わせる鍵なので、そちらを残す。
			std::string line = head + " " + text::Ellipsize(choice.probe.title, kTitleChars) +
							   " [" + choice.probe.id + "]";
			if (!choice.available)
				line += " ※本体なし";
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
			CProbePickerDialog(const std::string& prompt, const std::vector<std::string>& footer,
							   const std::vector<TXString>& items, short initialSelection)
				: fPrompt(kPromptID), fWarning(kWarningID), fPopup(kPopupID),
				  fPromptText(prompt.c_str()), fFooter(footer), fItems(items),
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
				if (!fPopup.CreateControl(this, kPopupWidthChars))
					return false;
				if (!fWarning.CreateControl(this,
											"※ 図面を変更します。新規の空図面で実行してください。"))
					return false;

				this->AddFirstGroupControl(&fPrompt);
				this->AddBelowControl(&fPrompt, &fPopup);
				this->AddBelowControl(&fPopup, &fWarning, 0, 1);

				// 素性は**行に分けて**積む（1 行にまとめると、その 1 行の長さが
				// そのままダイアログの横幅になる）。行数は呼び出し側で変わるので、
				// **deque に直接作る**——vector だと追加のたびに既存の要素が動くが、
				// ダイアログは生存中ずっとコントロールのアドレスを持つ。
				TControlID id = kFirstFooterID;
				VWControl* previous = &fWarning;
				short spacing = 1;
				for (const std::string& line : fFooter)
				{
					if (line.empty())
						continue;
					VWStaticTextCtrl& control = fFooterLines.emplace_back(id++);
					if (!control.CreateControl(this, line.c_str()))
						return false;
					this->AddBelowControl(previous, &control, 0, spacing);
					previous = &control;
					spacing = 0;
				}
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
				kFirstFooterID = 10
			}; // 1 = OK, 2 = キャンセルは予約

			VWStaticTextCtrl fPrompt;
			VWStaticTextCtrl fWarning;
			VWPullDownMenuCtrl fPopup;
			std::deque<VWStaticTextCtrl> fFooterLines;
			TXString fPromptText;
			std::vector<std::string> fFooter;
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
			Inform(text, "");
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
				const std::string text = (line != nullptr) ? line : "";
				collector->text += text;
				collector->text += '\n';
				// **控えにも 1 行ずつ流す。** プローブが VectorWorks ごと落としても、
				// そこまでのログが残って次の起動で PR へ送られる（Feedback.h）。
				AppendPendingLine(text);
			}
			catch (...)
			{
				// 握り潰す（1 行落ちるだけ。境界を壊すよりよい）
			}
		}

		// -------------------------------------------------------------------
		// **本体（ペイロード）を 1 本抱える器。** 群が変わるまで使い回す。
		//
		// 単発なら 1 回読んで降ろすだけだが、一括実行では続けて何件も走らせる。読み込みは
		// 0.3〜0.4 秒（PayloadHost.h）なので、**同じ群のあいだは読み直さない**。
		//
		// ログの受け口（collector）を本体と**同じ器に持たせている**のが肝——本体には
		// 読み込みのときにこのポインタを渡し、降ろすまで使われる（PayloadAbi.h の
		// 「寿命」）。別々に置くと、片方だけ先に消える書き方ができてしまう。
		class PayloadSession
		{
		public:
			PayloadSession() = default;
			~PayloadSession()
			{
				release();
			}

			PayloadSession(const PayloadSession&) = delete;
			PayloadSession& operator=(const PayloadSession&) = delete;

			// choice の群の本体を使える状態にする（すでにその群なら何もしない）。
			bool ensure(const Choice& choice, std::string& error)
			{
				if (fPayload.isLoaded() && fGroup == choice.group.id)
					return true;
				release();
				if (!fPayload.load(choice.payloadPath, (void*)gCBP, &fCollector, &CollectLine,
								   error))
					return false;
				fGroup = choice.group.id;
				return true;
			}

			// **降ろす。** ダイアログを出す前に必ず呼ぶ（出している間に本体を抱えたままに
			// しない——その間に入れ替えを試されると Windows で失敗する）。
			void release()
			{
				if (fPayload.isLoaded())
					fPayload.unload();
				fGroup.clear();
			}

			Payload& payload()
			{
				return fPayload;
			}

			// 1 件走らせる前に空にする（ログは件ごとに切り分けて見せる）。
			void clearLog()
			{
				fCollector.text.clear();
			}
			const std::string& log() const
			{
				return fCollector.text;
			}

		private:
			Payload fPayload;
			LogCollector fCollector;
			std::string fGroup;
		};

		// -------------------------------------------------------------------
		// このビルドが走っている環境（コメントの「実行」欄に出る）。
		constexpr const char* kPlatformName =
#if GS_MAC
			"macOS";
#else
			"Windows";
#endif

		// 結果を**投稿する形**（FeedbackParse.h）に詰める。走らせる前に分かるところまで
		// を埋め、結末は走らせてから足す（RunProbe）。
		//
		// pr は**下ごしらえが決めた宛先**を渡す（出所に無ければブランチから引いた番号が
		// 入っている。Feedback.h「宛先の決め方」）ので、choice.group.pr をここで直接
		// 読まない。
		feedback::Report MakeReport(const Payload& payload, const Choice& choice,
									const catalog::Catalog& cat, const std::string& pr)
		{
			feedback::Report report;
			report.probeId = choice.probe.id;
			report.title = choice.probe.title;
			report.summary = choice.probe.summary;
			report.pr = pr;
			report.prTitle = choice.group.prTitle;
			report.group = choice.group.id;
			report.commit = choice.group.commit;
			report.branch = choice.group.branch;
			report.buildId = payload.buildId();
			report.payloadStamp = payloadStamp(payload) + "  [群 " + choice.group.id + "]";
			report.catalogStamp = catalogStamp(cat);
			report.shellStamp = shellStamp();
			report.platform = kPlatformName;
			report.startedAt = LocalTimestamp();
			return report;
		}

		// -------------------------------------------------------------------
		// プローブ 1 件を走らせて、結果ダイアログの見出しを組み立てる。
		//
		// **走らせるのは本体（ペイロード）側**（plugin/src/payload/PayloadMain.cpp）。
		// 例外も undo の記録も所要時間もあちらが持っていて、こちらは結果を受け取って
		// 見せるだけ。ログはこの呼び出しの間に collector へ 1 行ずつ流れてくる。
		//
		// 同じ値を report にも書き込む——**結果ダイアログに出すものと PR へ送るものは
		// 同じ**でなければならないので、組み立ては 1 か所で行う。
		std::vector<std::string> RunProbe(Payload& payload, const Choice& choice,
										  const catalog::Catalog& cat, feedback::Report& report)
		{
			Payload::RunResult result;
			std::string error;
			if (!payload.run(choice.probe.id, result, error))
			{
				result.outcome = "走らせられなかった: " + error;
				result.failed = true;
			}

			report.outcome = result.outcome;
			report.failed = result.failed;
			report.seconds = result.seconds;
			report.logPath = result.logPath;

			std::vector<std::string> body;
			body.push_back("プローブ: " + choice.probe.title);
			body.push_back("出所: " + provenanceLine(choice.group));
			if (!choice.probe.summary.empty())
				body.push_back("概要: " + choice.probe.summary);
			body.emplace_back("");
			body.push_back("結果: " + result.outcome);
			body.push_back("所要: " + feedback::FormatSeconds(result.seconds) + " 秒");
			if (!result.logPath.empty())
				body.push_back("ログ: " + result.logPath);
			body.emplace_back("");
			body.push_back(payloadStamp(payload) + "  [群 " + choice.group.id + "]");
			body.push_back(catalogStamp(cat));
			body.push_back(shellStamp());
			return body;
		}

		// -------------------------------------------------------------------
		// **プローブ 1 件ぶんの結末。** 単発でも一括でもこれを作る——「結果ダイアログに
		// 出すもの」「PR へ送るもの」「まとめの 1 行」が食い違わないように、走らせる道は
		// 1 本にしておく。
		struct OneResult
		{
			bool ran = false; // 走らせるところまで行けた
			bool failed = false; // プローブ自身が失敗した（走らせられたかどうかとは別）
			bool posted = false;	 // PR へ投稿できた
			bool postFailed = false; // 投稿するつもりだったが送れなかった
			double seconds = 0.0;
			std::string outcome; // 1 行の結末（走らせられなければその理由）
			std::string advice;	 // 走らせられなかったときの補足
			std::string note;	 // 投稿の下ごしらえが利用者へ伝えたいこと
			std::vector<std::string> body; // 見出し（単発ならそのままダイアログへ）
			std::string log;
		};

		// 走らせられなかった（本体が無い・読めない・カタログと食い違う）。**黙って飛ばす
		// のではなく、何が起きているかを結末として持ち回る**——一括実行では 1 件ずつ
		// アラートを出せないので、まとめとログに残ることがそのまま説明になる。
		OneResult Blocked(const Choice& choice, const std::string& why, const std::string& advice)
		{
			OneResult out;
			out.outcome = why;
			out.advice = advice;
			out.body.push_back("プローブ: " + choice.probe.title);
			out.body.push_back("出所: " + provenanceLine(choice.group));
			out.body.emplace_back("");
			out.body.push_back("結果: " + why);
			if (!advice.empty())
				out.body.push_back(advice);
			return out;
		}

		// -------------------------------------------------------------------
		// プローブ 1 件を、下ごしらえから投稿まで通す。**本体は session が抱える**ので、
		// 一括実行では群が変わるまで読み直さない。**降ろすのは呼び出し側**（ダイアログを
		// 出す前に必ず降ろす）。
		OneResult RunOne(PayloadSession& session, const Choice& choice, const catalog::Catalog& cat)
		{
			if (!choice.available)
				return Blocked(choice, "この本体は入っていません（" + choice.group.file + "）",
							   "その群のビルドが通らなかったか、入れ替えが途中で止まって"
							   "います。\n他の群のプローブはそのまま選べます。");

			// 1. **走らせる前に、投稿の下ごしらえを済ませる。** 尋ねることがあるとしたら
			//    ここだけで（初回の 1 度きり）、走らせたあとには何も尋ねない
			//    （Feedback.h「走ったあとは何も尋ねない」）。一括実行でも同じ——2 件目
			//    からは答えを覚えているので、もう尋ねられない。
			std::string feedbackNote;
			std::string feedbackPr = choice.group.pr;
			const bool posting = PrepareFeedback(feedbackPr, choice.group.branch, feedbackNote);

			// 2. **その群の本体を用意する**（すでに読んでいればそのまま使う）。
			std::string loadError;
			if (!session.ensure(choice, loadError))
				return Blocked(choice, "本体を読み込めませんでした", loadError);

			Payload& payload = session.payload();

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
				return Blocked(choice,
							   "カタログと本体が食い違っています（" + choice.probe.id +
								   " がこの本体にありません）",
							   "新しいビルドに入れ替えてください。\n" + payloadStamp(payload));

			// 3. 走らせる（例外は本体側が受け止める）。**投稿するなら、走らせる直前に控えを
			//    置く**——プローブが VectorWorks ごと落としても、そこまでのログが残って次の
			//    起動で送られる（Feedback.h「落ちても拾う」）。
			session.clearLog();
			feedback::Report report = MakeReport(payload, choice, cat, feedbackPr);
			if (posting)
				ArmPendingRun(report);

			OneResult out;
			out.ran = true;
			out.body = RunProbe(payload, choice, cat, report);
			out.log = session.log();
			report.log = out.log;
			out.failed = report.failed;
			out.seconds = report.seconds;
			out.outcome = report.outcome;

			// 4. **投稿する（黙って）。** 結果は行として添えるだけ——ここでアラートを
			//    増やすと、走らせるたびにクリックが 1 つ増える（一括ならその件数ぶん）。
			std::vector<std::string> posted;
			if (posting)
			{
				// 結末を控えへ書き込んでから投稿する。**送れたら控えは消える**（PostReport）。
				// 送れなければ残り、次にメニューを開いたときに結末付きのまま送り直される
				// ——だから投稿しない周でここに Disarm を置いてはいけない（前の周の、まだ
				// 送れていない控えを巻き添えで消すことになる）。
				FinishPendingRun(report);
				bool sent = false;
				posted = PostReport(report, &sent);
				out.posted = sent;
				// **送れなかったことは数える。** 一括実行では 1 件ずつ読まないので、まとめが
				// 黙っていると「全部 PR に載った」と読めてしまう（控えは残っていて次に
				// 送り直されるが、それはあとの話である）。
				out.postFailed = !sent && !posted.empty();
			}
			if (!posted.empty() || !feedbackNote.empty())
				out.body.emplace_back("");
			for (const std::string& line : posted)
				out.body.push_back(line);
			if (!feedbackNote.empty())
				out.body.push_back(feedbackNote);
			out.note = feedbackNote;
			return out;
		}

		// -------------------------------------------------------------------
		// **一覧のプローブを、上から順に全部走らせる**（ピッカーの kRunAllItem）。
		//
		// 【まとめは最後に 1 枚】1 件ごとに結果ダイアログを出すと、件数ぶんクリックが要る
		// ——それを無くすための項目なので、ここでは**走り終えてから 1 枚だけ**出す。
		// 1 件ぶんは 1 行に畳み（ProbeMenuText.h）、見出し・結末・ログの全文は
		// **同じダイアログのログ欄**へ順に積む（選択してコピーできる）。
		//
		// 【投稿は 1 件ずつその場で】まとめて送らない。PR ごとに宛先が違ううえ、
		// **プローブが VectorWorks ごと落とすことがある**（落ち方そのものが知見になる
		// 調査がある）——1 件ずつ送っておけば、そこまでの結果は PR に残り、落ちた 1 件も
		// 控えから次の起動で送られる（Feedback.h「落ちても拾う」）。
		//
		// 【走らせられない件は飛ばして続ける】本体が無い群・カタログと食い違う件は、
		// そこで打ち切らずに結末を控えて次へ進む（単発なら「なぜ何も起きないか」を
		// アラートで言えばよいが、一括では残りを走らせられなくなるほうが困る）。
		void RunAll(const std::vector<Choice>& all, const std::vector<size_t>& order,
					const catalog::Catalog& cat)
		{
			PayloadSession session;
			const std::size_t total = order.size();
			std::size_t ok = 0;
			std::size_t failed = 0;
			std::size_t blocked = 0;
			std::size_t posted = 0;
			std::size_t postFailed = 0;
			std::string note; // 投稿の下ごしらえが伝えたいこと（最初の 1 つだけ出す）
			double seconds = 0.0;
			std::vector<std::string> lines; // まとめの 1 件 1 行
			std::string log;

			for (std::size_t i = 0; i < total; ++i)
			{
				const Choice& choice = all[order[i]];
				const OneResult result = RunOne(session, choice, cat);

				if (!result.ran)
					++blocked;
				else if (result.failed)
					++failed;
				else
					++ok;
				if (result.posted)
					++posted;
				if (result.postFailed)
					++postFailed;
				if (note.empty())
					note = result.note;
				seconds += result.seconds;

				const std::string head = choiceHead(choice);
				lines.push_back(text::BatchResultLine(
					i + 1, total, head, choice.probe.id, result.outcome,
					result.ran ? feedback::FormatSeconds(result.seconds) : std::string()));

				log += text::BatchLogHeader(i + 1, total, head, choice.probe.id);
				log += '\n';
				for (const std::string& line : result.body)
				{
					log += line;
					log += '\n';
				}
				if (!result.log.empty())
				{
					log += '\n';
					log += result.log;
				}
				log += '\n';
			}

			// **降ろしてから見せる**（ダイアログを出している間に本体を抱えたままにしない。
			// ログは上に写してあるので、降ろしても失わない）。
			session.release();

			std::vector<std::string> body;
			body.push_back(text::BatchSummaryLine(total, ok, failed, blocked));
			std::string spent = "所要: " + feedback::FormatSeconds(seconds) + " 秒";
			if (posted > 0)
				spent += " / PR へ投稿 " + std::to_string(posted) + " 件";
			if (postFailed > 0)
				spent += " / 送れず " + std::to_string(postFailed) + " 件";
			body.push_back(spent);
			if (!note.empty())
				body.push_back(note);
			body.emplace_back("");
			const std::size_t shown = std::min(lines.size(), kMaxSummaryLines);
			for (std::size_t i = 0; i < shown; ++i)
				body.push_back(lines[i]);
			if (shown < lines.size())
				body.push_back("…ほか " + std::to_string(lines.size() - shown) +
							   " 件（下のログ欄に全部あります）");
			body.emplace_back("");
			body.push_back(catalogStamp(cat));
			body.push_back(shellStamp());
			ShowResult(body, log);
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
	// **ピッカーは繰り返し出す。** 「入れ替える」を選ぶのは、たいてい新しいプローブを
	// 走らせたいからである。本体だけの入れ替え（＝再起動が要らない道。Update.h）なら
	// **その場で一覧を読み直して選ばせる**——ここで終わってしまうと、入れ替えるたびに
	// メニューを開き直すことになる。殻まで入れ替わったときだけ抜ける（再起動するまで
	// 動いているのは古い殻なので、そのまま選ばせない）。
	// **前の走行の残りを先に片付ける。** プローブが VectorWorks ごと落ちていたら、
	// 控え（ログ）が一時ディレクトリに残っている。ここで PR へ送って消す——人の操作は
	// 増えず、落ちた回の記録も失われない（Feedback.h「落ちても拾う」）。
	const std::vector<std::string> leftovers = PostLeftovers();

	for (;;)
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
		//    （カタログを読めなかったときでも、入れ替えだけは選べる——たいていそれが
		//    直し方）。
		std::vector<TXString> items;
		items.reserve(order.size() + kFixedItems);
		items.emplace_back(kUpdateItem);
		items.emplace_back(kFeedbackItem);
		items.emplace_back(kRunAllItem);
		for (const size_t index : order)
			items.emplace_back(pickerItem(all[index]).c_str());

		std::string prompt = "実行するプローブを選んでください:";
		if (all.empty())
			prompt = "プローブがありません。新しいビルドを取り込めます:";

		// 素性は**1 行 1 見出し**（つないで 1 行にすると、その長さがダイアログの横幅に
		// なる。冒頭「横幅はいちばん長い行で決まる」）。
		std::vector<std::string> footer;
		footer.push_back(catalogStamp(cat));
		footer.push_back(shellStamp());
		if (cat.skippedLines > 0)
			footer.push_back("※ カタログに読めない行が " + std::to_string(cat.skippedLines) +
							 " 行あります");
		// 拾った控えのことは**ここで 1 行だけ**言う（アラートを増やさない）。
		for (const std::string& line : leftovers)
			footer.push_back("※ " + line);

		// 既定の選択は**先頭のプローブ**（あれば）。入れ替えと設定は意識して選ぶものにする。
		CProbePickerDialog picker(prompt, footer, items, all.empty() ? 0 : short(kFixedItems));
		const bool accepted = (picker.RunDialogLayout("") == VWFC::VWUI::kDialogButton_Ok);
		if (!picker.Shown())
		{
			// ダイアログを組めなかった。**黙って終わらない**——プローブは 1 件も走らない
			// ので、なぜ何も起きなかったのかを伝える（Findings「Layout Dialogs」）。
			std::string why;
			for (const std::string& line : footer)
				why += line + "\n";
			Inform("プローブの選択ダイアログを組めませんでした。", why);
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
			if (RunManualUpdateCheck() == UpdateOutcome::RestartNeeded)
				return;
			continue; // 一覧を読み直して選ばせる（新しいプローブはここで出てくる）
		}
		if (selection == 1)
		{
			// 2 番目 = 結果の自動投稿の設定（Feedback.h）。ふだんは触らない項目なので、
			// 済んだら**ピッカーへ戻す**——ここへ来た人はたいてい、このあとプローブを
			// 走らせたい。
			RunFeedbackSettings();
			continue;
		}

		if (selection == 2)
		{
			// 3 番目 = **一覧のプローブをすべて順に実行**（RunAll）。結果は最後に 1 枚だけ
			// 出る。ここでピッカーへは戻さない——まとめを閉じた直後にまた一覧が出てくると、
			// 「全部終わった」ことが伝わらない。
			if (all.empty())
			{
				// 走らせるものが無い（プローブが 1 つも載っていないビルド）。**ピッカーへ
				// 戻す**——たいていの直し方は「新しいビルドに入れ替える」である。
				Inform("走らせるプローブがありません。", catalogError);
				continue;
			}
			RunAll(all, order, cat);
			return;
		}

		if (all.empty())
		{
			// プローブを選べる状態ではない（一覧が空なので、ここへは来ないはずだが念のため）。
			Inform("プローブの一覧を読めませんでした。", catalogError);
			return;
		}

		const size_t choiceIndex = size_t(selection) - kFixedItems; // 頭の固定項目ぶんずらす
		if (choiceIndex >= order.size())
			return;
		const Choice& choice = all[order[choiceIndex]];

		// 2. **走らせる道は一括実行と同じ**（RunOne）——下ごしらえ・読み込み・実行・投稿は
		//    そちらに全部あり、ここは結果の見せ方だけを持つ。本体を抱えるのは session で、
		//    **結果ダイアログを出す前に必ず降ろす**（抱えたままだと Windows で入れ替えが
		//    失敗する）。ログは写してあるので、降ろしても失わない。
		PayloadSession session;
		const OneResult result = RunOne(session, choice, cat);
		const std::string logText = result.log;
		session.release();

		if (!result.ran)
		{
			// 走らせられなかった（本体が無い・読めない・カタログと食い違う）。**何が
			// 起きているかを言う**——黙って何も起きないのが一番たちが悪い。
			Inform(result.outcome + "。", result.advice);
			return;
		}

		ShowResult(result.body, logText);
		return;
	}
}
