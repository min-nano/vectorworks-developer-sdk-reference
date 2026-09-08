//
//	Feedback.cpp
//
//	結果の自動投稿の実装（意図と流れは Feedback.h）。
//
//	ここが持つのは 3 つだけ:
//	  * 設定（送ってよいか）の置き場所と読み書き
//	  * ダイアログ（1 度だけの確認とトークンの貼り付け）
//	  * 走行中の控えの開け閉め
//	本文の組み立てと控えの書式は FeedbackParse.h（純粋。単体テストがある）、ネットワークと
//	トークンの保管は同梱スクリプト（vw-probes-feedback.sh / .ps1）。**C++ は curl も
//	トークンも触らない**——自動アップデートとまったく同じ分担である（Update.cpp）。
//
//	【ダイアログの作法】Findings「Layout Dialogs」に従う（ProbeMenu.cpp と同じ）。
//	組めなかったときは素のアラートへ落とす。
//

#include "PluginPrefix.h"
#include "Feedback.h"
#include "Alerts.h"
#include "BundledScript.h"
#include "FeedbackParse.h"
#include "UpdateParse.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace vwprobe::update;

namespace vwprobe
{
	namespace
	{
		// 同梱スクリプトの基底名（拡張子は BundledScript.h が付ける）。
		constexpr const char* kFeedbackScript = "vw-probes-feedback";

		// ダイアログのコントロール ID（1 = OK / 2 = キャンセルは SDK の予約）。
		constexpr TControlID kTokenLabelID = 4;
		constexpr TControlID kTokenID = 5;
		constexpr TControlID kTokenNoteID = 6;
		constexpr TControlID kTokenWhereID = 7;
		constexpr short kTokenWidthChars = 72;

		// -------------------------------------------------------------------
		// 設定の置き場所。**一時ディレクトリには置かない**（消えると毎回尋ねることに
		// なる）。試験では VW_PROBE_FEEDBACK_STATE で差し替えられる。
		// -------------------------------------------------------------------

		std::string EnvValue(const char* name)
		{
			// NOLINTNEXTLINE(concurrency-mt-unsafe): 起動直後・メニュー内でしか読まない。
			const char* value = std::getenv(name);
			return (value != nullptr) ? std::string(value) : std::string();
		}

		std::string SettingsPath()
		{
			const std::string custom = EnvValue("VW_PROBE_FEEDBACK_STATE");
			if (!custom.empty())
				return custom;
#if defined(_WIN32)
			std::string dir = EnvValue("LOCALAPPDATA");
			if (dir.empty())
				dir = EnvValue("APPDATA");
			if (dir.empty())
				return "";
			return dir + "\\VwSdkProbes\\feedback.txt";
#else
			const std::string home = EnvValue("HOME");
			if (home.empty())
				return "";
			return home + "/Library/Application Support/VwSdkProbes/feedback.txt";
#endif
		}

		feedback::Settings LoadSettings()
		{
			const std::string path = SettingsPath();
			if (path.empty())
				return {};
			std::ifstream in(path, std::ios::binary);
			if (!in)
				return {};
			const std::string text((std::istreambuf_iterator<char>(in)),
								   std::istreambuf_iterator<char>());
			return feedback::ParseSettings(text);
		}

		bool SaveSettings(const feedback::Settings& settings)
		{
			const std::string path = SettingsPath();
			if (path.empty())
				return false;
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out)
				return false;
			const std::string text = feedback::FormatSettings(settings);
			out.write(text.data(), static_cast<std::streamsize>(text.size()));
			return out.good();
		}

		// -------------------------------------------------------------------
		// 一時ファイル（本文とトークンの受け渡し）。**引数に本文や秘密を乗せない**
		// ——コマンドラインは長さに限りがあり、プロセス一覧からも見える。
		// -------------------------------------------------------------------

		std::string WriteTempFile(const std::string& tag, const std::string& contents,
								  bool ownerOnly)
		{
			std::error_code ec;
			const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
			if (ec)
				return "";
			const std::filesystem::path path =
				dir / ("VwSdkProbes-" + tag + "-" +
					   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			{
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				if (!out)
					return "";
				out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
				if (!out.good())
					return "";
			}
			if (ownerOnly)
			{
				// **トークンを渡すファイルは本人しか読めなくする。** 一時ディレクトリは
				// 共有なので、既定の許可のまま置くと他のユーザーに読まれうる。
				std::filesystem::permissions(
					path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
					std::filesystem::perm_options::replace, ec);
			}
			return path.string();
		}

		void RemoveTempFile(const std::string& path)
		{
			if (path.empty())
				return;
			std::error_code ec;
			std::filesystem::remove(std::filesystem::path(path), ec);
		}

		// -------------------------------------------------------------------
		// トークンを 1 度だけ受け取るダイアログ。
		// -------------------------------------------------------------------

		class CTokenDialog : public VWDialog
		{
		public:
			CTokenDialog()
				: fLabel(kTokenLabelID), fNote(kTokenNoteID), fWhere(kTokenWhereID),
				  fToken(kTokenID)
			{
			}
			~CTokenDialog() override = default;

			const TXString& Token() const
			{
				return fTokenText;
			}

			bool Shown() const
			{
				return fShown;
			}

		protected:
			bool CreateDialogLayout() override
			{
				if (!this->CreateDialog("GitHub のトークン", "保存", "やめる", false))
					return false;
				// **1 行 1 コントロール**（VWStaticTextCtrl に埋めた改行は行にならない）。
				if (!fLabel.CreateControl(this, "プローブの結果を PR へ投稿するためのトークンを、"
												"1 度だけ登録します。"))
					return false;
				if (!fNote.CreateControl(
						this, "GitHub の Fine-grained token（このリポジトリの Pull requests: "
							  "Read and write）を貼り付けてください。"))
					return false;
				if (!fWhere.CreateControl(this, "保存先は macOS のキーチェーン / Windows の"
												"暗号化ファイルで、ログにも図面にも残りません。"))
					return false;
				if (!fToken.CreateControl(this, "", kTokenWidthChars, 1))
					return false;
				this->AddFirstGroupControl(&fLabel);
				this->AddBelowControl(&fLabel, &fNote, 0, 0);
				this->AddBelowControl(&fNote, &fWhere, 0, 0);
				this->AddBelowControl(&fWhere, &fToken, 0, 1);
				return true;
			}

			void OnInitializeContent() override
			{
				VWDialog::OnInitializeContent();
				fShown = true;
			}

			void OnDDXInitialize() override
			{
				this->AddDDX_EditText(kTokenID, &fTokenText);
			}

			DEFINE_EVENT_DISPATH_MAP;

		private:
			VWStaticTextCtrl fLabel;
			VWStaticTextCtrl fNote;
			VWStaticTextCtrl fWhere;
			VWEditTextCtrl fToken;
			TXString fTokenText;
			bool fShown = false;
		};

		// NOLINTNEXTLINE(misc-const-correctness)
		EVENT_DISPATCH_MAP_BEGIN(CTokenDialog);
		EVENT_DISPATCH_MAP_END;

		// トークンが使えるか（スクリプトに訊く。トークンそのものは受け取らない）。
		bool HaveToken()
		{
			std::string out;
			if (!RunBundledScript(kFeedbackScript, {"token-status"}, out))
				return false;
			return ValueOf(out, "ok") == "yes";
		}

		// **トークンを 1 度だけ貼り付けてもらう。** 入れられたら true。
		bool RegisterToken(std::string& note)
		{
			CTokenDialog dialog;
			const bool accepted = (dialog.RunDialogLayout("") == VWFC::VWUI::kDialogButton_Ok);
			if (!dialog.Shown())
			{
				note = "トークンの入力ダイアログを組めませんでした。";
				return false;
			}
			if (!accepted)
			{
				note = "トークンの登録をやめました。";
				return false;
			}
			const std::string token = static_cast<const char*>(dialog.Token());
			if (token.empty())
			{
				note = "トークンが空でした。";
				return false;
			}

			// **引数に乗せずファイル経由で渡す。** 読んだスクリプトがその場で消す約束だが、
			// こちらでも消す（どちらかが落ちても残らないように）。
			const std::string file = WriteTempFile("token", token, /*ownerOnly*/ true);
			if (file.empty())
			{
				note = "トークンを一時ファイルへ書けませんでした。";
				return false;
			}
			std::string out;
			const bool ran = RunBundledScript(kFeedbackScript, {"login", file}, out);
			RemoveTempFile(file);
			// 同梱スクリプトは**成功を素の `ok` 1 行**で返す（自動アップデートと同じ
			// 約束事なので、判定も同じものを使う。UpdateParse.h）。
			if (!ran || !InstallReportedOk(out))
			{
				const std::string reason = ValueOf(out, "error");
				note = reason.empty() ? "トークンを保存できませんでした。" : reason;
				return false;
			}
			return true;
		}

		// -------------------------------------------------------------------
		// 走行中の控え（Feedback.h 冒頭「落ちても拾う」）。
		// -------------------------------------------------------------------

		std::string sPendingPath;  // 開いている控え（空なら控えていない）
		std::ofstream sPendingLog; // その書き出し口（1 行ごとに flush する）

		// 書き出し口を閉じるだけ（**ファイルは消さない**）。送れずに残っている控えを、
		// 次のプローブを走らせたときに巻き添えで消さないための区別——消してよいのは
		// 「送れた」か「そもそも送らない」と決まったときだけ（DisarmPendingRun）。
		void ClosePendingLog()
		{
			if (sPendingLog.is_open())
				sPendingLog.close();
			sPendingPath.clear();
		}

		std::string PendingPathFor(const std::string& probeId)
		{
			std::error_code ec;
			const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
			if (ec)
				return "";
			return (dir / feedback::PendingFileName(probeId)).string();
		}

		// **出所に PR 番号が無いときの逃げ道**（Feedback.h「宛先の決め方」2）。動いている
		// ビルドのブランチから open な PR を引く。引けなければ空を返す（投稿しない）。
		//
		// main と local は引かない——公開ビルドで main のプローブを走らせるたびに
		// GitHub を叩くことになるうえ、そちらには宛先が無いのが正しい。
		std::string ResolvePullRequestFromBranch(const std::string& branch)
		{
			if (branch.empty() || branch == "main" || branch == "local")
				return "";
			std::string out;
			if (!RunBundledScript(kFeedbackScript, {"find-pr", "", branch}, out))
				return "";
			return ValueOf(out, "pr");
		}

		// 投稿を 1 通。成功なら url、失敗なら error を埋めて返す（どちらも空なら起動失敗）。
		void PostOnce(const feedback::Report& report, const feedback::Settings& settings,
					  std::string& url, std::string& error)
		{
			const std::string file =
				WriteTempFile("body", feedback::CommentBody(report), /*ownerOnly*/ false);
			if (file.empty())
			{
				error = "コメント本文を一時ファイルへ書けませんでした。";
				return;
			}
			std::string out;
			const bool ran =
				RunBundledScript(kFeedbackScript, {"post", settings.repo, report.pr, file}, out);
			RemoveTempFile(file);
			if (!ran)
			{
				error = "投稿スクリプトを起動できませんでした。";
				return;
			}
			error = ValueOf(out, "error");
			url = ValueOf(out, "url");
			if (error.empty() && url.empty() && !InstallReportedOk(out))
				error = "投稿の結果を読み取れませんでした。";
		}
		// -------------------------------------------------------------------
		// 前の走行の残り（控え）を拾って投稿する。**見つけた控えは、送れたら消す**
		// ——送れなかったものは残し、次にメニューを開いたときに再挑戦する。
		// -------------------------------------------------------------------
		std::vector<std::string> CollectLeftovers()
		{
			std::vector<std::string> lines;

			std::error_code ec;
			const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
			if (ec)
				return lines;

			const feedback::Settings settings = LoadSettings();
			const std::string prefix = feedback::kPendingPrefix;

			for (const std::filesystem::directory_entry& entry :
				 std::filesystem::directory_iterator(dir, ec))
			{
				const std::string name = entry.path().filename().string();
				if (!name.starts_with(prefix) || !name.ends_with(feedback::kPendingSuffix))
					continue;

				// **設定が「投稿しない」なら、拾わずに片付ける。** 本体が書いたログ
				// （VwSdkProbes-<id>.log）は別に残っているので、失われるものは無い。
				if (settings.consent != feedback::Consent::Send || !FeedbackAvailable())
				{
					std::error_code rm;
					std::filesystem::remove(entry.path(), rm);
					continue;
				}

				std::ifstream in(entry.path(), std::ios::binary);
				std::string text;
				if (in)
					text.assign((std::istreambuf_iterator<char>(in)),
								std::istreambuf_iterator<char>());
				in.close();

				feedback::Report report;
				if (feedback::ParsePending(text, report) && !report.pr.empty())
				{
					std::string url;
					std::string error;
					PostOnce(report, settings, url, error);
					if (!error.empty())
					{
						lines.push_back("前回の走行（" + report.probeId + "）の記録を PR #" +
										report.pr + " へ送れませんでした: " + error);
						continue;
					}
					lines.push_back("前回の走行（" + report.probeId + "）の記録を PR #" +
									report.pr + " へ送りました");
				}
				std::error_code rm;
				std::filesystem::remove(entry.path(), rm);
			}
			return lines;
		}
	} // namespace

	// -----------------------------------------------------------------------
	std::string LocalTimestamp()
	{
		const std::time_t now = std::time(nullptr);
		// NOLINTNEXTLINE(concurrency-mt-unsafe): メニューの中でしか呼ばない。
		const std::tm* local = std::localtime(&now);
		if (local == nullptr)
			return "";
		std::array<char, 32> buf{};
		if (std::strftime(buf.data(), buf.size(), "%Y-%m-%d %H:%M:%S", local) == 0)
			return "";
		return buf.data();
	}

	// -----------------------------------------------------------------------
	bool FeedbackAvailable()
	{
		return BundledScriptExists(kFeedbackScript);
	}

	// -----------------------------------------------------------------------
	bool PrepareFeedback(std::string& pr, const std::string& branch, std::string& note)
	{
		note.clear();

		feedback::Settings settings = LoadSettings();
		if (settings.consent == feedback::Consent::Never)
			return false; // 断られている。**二度と尋ねない**

		if (!FeedbackAvailable())
		{
			// 古い版から入れ替えた直後は、Windows だと**新しいスクリプトがまだ無い**
			// （入れ替えを行うのは常に「入っている＝古い」版のスクリプトなので）。
			// もう 1 度入れ替えれば揃うので、そう言う。
			note = "結果の自動投稿は使えません（同梱スクリプトがありません）。"
				   "もう一度「新しいプローブビルドを確認して入れ替える」を実行してください。";
			return false;
		}

		// **宛先を決める。** 出所に無ければブランチから引く（Feedback.h「宛先の決め方」）。
		if (pr.empty())
			pr = ResolvePullRequestFromBranch(branch);
		if (pr.empty())
			return false; // 宛先が無い（main に入っているプローブ）。黙って投稿しない

		if (settings.consent == feedback::Consent::Unset)
		{
			// **尋ねるのはここ 1 回だけ。** 走らせた後には何も尋ねない。
			const bool yes =
				Ask("プローブの結果を PR へ自動で投稿しますか？",
					"投稿先は、そのプローブが来た PR（このプローブなら #" + pr +
						"）です。\n"
						"送るのは結果・所要時間・出所・ログ全文で、投稿は毎回黙って行います"
						"（走らせたあとは何も尋ねません）。\n\n"
						"「投稿する」を選ぶと、続けて GitHub のトークンを 1 度だけ尋ねます。\n"
						"断ると二度と尋ねません（ピッカーの「結果の自動投稿を設定…」から"
						"いつでも入れ直せます）。",
					"投稿する", "投稿しない");
			if (!yes)
			{
				settings.consent = feedback::Consent::Never;
				(void)SaveSettings(settings);
				return false;
			}
			settings.consent = feedback::Consent::Send;
			if (!SaveSettings(settings))
				note = "設定を保存できませんでした（次も同じことを尋ねます）。";
		}

		if (HaveToken())
			return true;

		std::string why;
		if (RegisterToken(why))
			return true;

		// **トークンを入れずに終えたら、そこで打ち切る。** 走らせるたびに尋ね直すと、
		// 「入れ替える・走らせる」の 2 手で済むという前提が崩れる。
		settings.consent = feedback::Consent::Never;
		(void)SaveSettings(settings);
		note = (why.empty() ? std::string("トークンを登録できませんでした。") : why) +
			   "\n以後は投稿しません（ピッカーの「結果の自動投稿を設定…」から入れ直せます）。";
		return false;
	}

	// -----------------------------------------------------------------------
	void ArmPendingRun(const feedback::Report& report)
	{
		ClosePendingLog();
		const std::string path = PendingPathFor(report.probeId);
		if (path.empty())
			return;
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
			return;
		const std::string header = feedback::FormatPending(report, /*finished*/ false);
		out.write(header.data(), static_cast<std::streamsize>(header.size()));
		out.flush();
		if (!out.good())
			return;
		out.close();

		sPendingPath = path;
		sPendingLog.open(path, std::ios::binary | std::ios::app);
	}

	void AppendPendingLine(const std::string& line)
	{
		if (!sPendingLog.is_open())
			return;
		sPendingLog << line << '\n';
		// **1 行ごとに flush する。** 落ちたときに残っているのがこの仕組みの全部なので、
		// 溜めてはならない。
		sPendingLog.flush();
	}

	void FinishPendingRun(const feedback::Report& report)
	{
		if (sPendingPath.empty())
			return;
		if (sPendingLog.is_open())
			sPendingLog.close();

		// **結末とログを揃えて書き直す。** 投稿がこの後で失敗しても、控えは次に
		// メニューを開いたときに「結末付きの記録」として送り直せる。
		std::ofstream out(sPendingPath, std::ios::binary | std::ios::trunc);
		if (!out)
			return;
		const std::string text = feedback::FormatPending(report, /*finished*/ true) + report.log;
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
		out.flush();
	}

	void DisarmPendingRun()
	{
		if (sPendingLog.is_open())
			sPendingLog.close();
		if (!sPendingPath.empty())
		{
			std::error_code ec;
			std::filesystem::remove(std::filesystem::path(sPendingPath), ec);
			sPendingPath.clear();
		}
	}

	// -----------------------------------------------------------------------
	std::vector<std::string> PostReport(const feedback::Report& report)
	{
		std::vector<std::string> lines;
		if (report.pr.empty())
			return lines;

		// **投稿の失敗でプローブの結果を失わない。** ここから先で何が起きても、
		// 呼び出し側は結果ダイアログを出す（例外を外へ出せば VectorWorks ごと落ちる）。
		try
		{
			const feedback::Settings settings = LoadSettings();
			if (settings.consent != feedback::Consent::Send || !FeedbackAvailable())
				return lines;

			std::string url;
			std::string error;
			PostOnce(report, settings, url, error);
			if (!error.empty())
				lines.push_back("投稿: PR #" + report.pr + " へ送れませんでした（" + error + "）");
			else if (!url.empty())
				lines.push_back("投稿: " + url);
			else
				lines.push_back("投稿: PR #" + report.pr + " へ送りました");

			// **送れたら控えを消す。** 送れなかったものは残し、次にメニューを開いた
			// ときに拾い直す（CollectLeftovers）。
			if (error.empty())
				DisarmPendingRun();
		}
		catch (const std::exception& error)
		{
			lines.push_back(std::string("投稿: 送れませんでした（") +
							((error.what() != nullptr) ? error.what() : "") + "）");
		}
		catch (...)
		{
			lines.push_back("投稿: 送れませんでした（不明なエラー）");
		}
		return lines;
	}

	// -----------------------------------------------------------------------
	std::vector<std::string> PostLeftovers()
	{
		std::vector<std::string> lines;
		try
		{
			lines = CollectLeftovers();
		}
		// NOLINTBEGIN(bugprone-empty-catch): 拾えなかった控えは次にメニューを開いた
		// ときに拾い直せる。ここで騒いでも利用者にできることは無い。
		catch (...)
		{
		}
		// NOLINTEND(bugprone-empty-catch)
		return lines;
	}

	// -----------------------------------------------------------------------
	void RunFeedbackSettings()
	{
		if (!FeedbackAvailable())
		{
			Inform("結果の自動投稿は使えません。",
				   "同梱スクリプト（vw-probes-feedback）が見つかりません。\n"
				   "もう一度「新しいプローブビルドを確認して入れ替える」を実行してください。");
			return;
		}

		feedback::Settings settings = LoadSettings();
		const bool sending = (settings.consent == feedback::Consent::Send);
		const bool haveToken = HaveToken();

		std::string state = sending ? "いまの設定: 投稿する" : "いまの設定: 投稿しない";
		if (settings.consent == feedback::Consent::Unset)
			state = "いまの設定: まだ決めていません（初めて PR のプローブを走らせるときに"
					"尋ねます）";
		state += haveToken ? "\nトークン: 登録済み" : "\nトークン: 未登録";

		if (!Ask("プローブの結果を PR へ自動で投稿しますか？",
				 state + "\n\n投稿先は、そのプローブが来た PR です（出所も動いている"
						 "ビルドのブランチも main なら、宛先が無いので投稿しません）。\n"
						 "送るのは結果・所要時間・出所・ログ全文で、投稿は毎回黙って行います。",
				 "投稿する", "投稿しない"))
		{
			settings.consent = feedback::Consent::Never;
			(void)SaveSettings(settings);
			Inform("結果を投稿しません。", "この設定はいつでもここで変えられます。");
			return;
		}

		settings.consent = feedback::Consent::Send;
		(void)SaveSettings(settings);

		if (haveToken &&
			!Ask("トークンは登録済みです。入れ直しますか？",
				 "入れ直す必要があるのは、期限が切れたときや権限を変えたときだけです。", "入れ直す",
				 "このまま"))
		{
			Inform("結果を PR へ投稿します。",
				   "プローブを走らせると、その結果が出所の PR へ自動で投稿されます。");
			return;
		}

		std::string why;
		if (RegisterToken(why))
		{
			Inform("結果を PR へ投稿します。",
				   "プローブを走らせると、その結果が出所の PR へ自動で投稿されます。");
			return;
		}
		Inform("トークンを登録できませんでした。",
			   why + (haveToken ? "\n（前のトークンはそのまま残っています）" : ""));
	}
} // namespace vwprobe
