//
//	McpBridge.cpp
//
//	MCP ブリッジの実装（意図と制約は McpBridge.h）。**道具を足すときに触るのは kTools の
//	1 行と、その実装 1 つだけ**——Python サーバ（plugin/mcp/vw-mcp-server.py）は起動時に
//	`vw_tools` でこの表を取りに行くので、向こうは直さない。
//
//	【道具は 2 種類】（Tool::kind）
//	  * **読む**（Read）… カタログ・ログを読むだけで、何も作らない・変えない。
//	  * **長く走る**（Long）… プローブを走らせる・入れ替える・再起動する・片付ける。走る前に
//	    生存の印へ `busy_until` を書いておく（その間は印を書き直せないので、Python 側が
//	    「止まった」と誤認しないように）。
//
//	【走らせる道は 1 本】プローブはメニューと同じ RunOne（ProbeMenu.cpp）を通る
//	（RunProbesQuietly）。入れ替えも手で押したときと同じ同梱スクリプト（RunRemoteUpdate）、
//	再起動も同じヘルパー（RequestRestart）。ここに持つのは**引数を渡して結末を JSON に
//	詰める**ことだけ。
//
//	使う SDK API:
//	  * OpenDocumentPath(nullptr, false) … 新規の空図面（Findings「Documents」で実機確認済み）
//	  * GetOpenFilesList / SwitchToOpenFile / CloseDocument … ブリッジが
//	    開いた図面を閉じる。**CloseDocument の戻り値は当てにならない**（閉じても false を返す。
//	    同 Findings）ので、閉じたかは一覧を読み戻して確かめる。
//	  * IsCurrentlyBuildingAnUndoEvent … 刻みを見送るか（Findings「Timers and Notifications」§6）。
//	    見送っている間も生存の印は書き、理由（deferred）を載せる（issue #247）
//	  * EndUndoEvent … プローブが閉じずに残した undo イベントを、走らせた後に閉じる（同上）
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "McpBridge.h"

#include "BridgeJson.h"
#include "BridgeParse.h"
#include "BridgeSpool.h"
#include "PayloadHost.h"
#include "ProbeMenu.h"
#include "Update.h"

#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#	include <unistd.h>
#endif

namespace vwprobe
{
	namespace
	{
		using bridge::Json;

		// 生存の印を書き直す間隔（秒）。Python 側は 15 秒古ければ「止まった」と判定する
		// （bridge::kStatusStaleSeconds）。
		constexpr long long kStatusBeatSeconds = 2;
		// スプールを用意できなかったとき、次に試すまで（秒）。
		constexpr long long kPrepareRetrySeconds = 10;

		long long NowSeconds()
		{
			return static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
											  std::chrono::system_clock::now().time_since_epoch())
											  .count());
		}

		long long ProcessId()
		{
#if GS_MAC
			return static_cast<long long>(::getpid());
#else
			return static_cast<long long>(::GetCurrentProcessId());
#endif
		}

		std::string Utf8(const TXString& text)
		{
			const char* const raw = static_cast<const char*>(text);
			return raw != nullptr ? std::string(raw) : std::string();
		}

		Json Strings(const std::vector<std::string>& lines)
		{
			Json list = Json::array();
			for (const std::string& line : lines)
				list.push(Json::string(line));
			return list;
		}

		// 引数の真偽（無ければ既定）。
		bool BoolArg(const Json& args, const char* key, bool fallback)
		{
			return args.at(key).asBool(fallback);
		}

		std::size_t MaxBytesArg(const Json& args)
		{
			const auto wanted = static_cast<long long>(args.at("max_bytes").asNumber(0.0));
			return wanted > 0 ? static_cast<std::size_t>(wanted) : bridge::kDefaultLogBytes;
		}

		// =====================================================================
		// ブリッジが開いた図面（McpBridge.h「図面」）。
		// =====================================================================

		struct OwnedDocument
		{
			Sint32 fileRef = -1; // SwitchToOpenFile の鍵（不透明な値。連番ではない）
			std::string path; // 無題でも「アプリのある場所＋名称未設定 N」が入る
			std::string savedPath; // 一時ファイルへ保存したならその場所（閉じたら消す）
		};

		std::vector<OwnedDocument>& OwnedDocuments()
		{
			static std::vector<OwnedDocument> sOwned;
			return sOwned;
		}

		std::string PathOf(const VectorWorks::Filing::IFileIdentifierPtr& fileID)
		{
			if (!fileID)
				return "";
			TXString path;
			if (fileID->GetFileFullPath(path) != kVCOMError_NoError)
				return "";
			return Utf8(path);
		}

		struct OpenFile
		{
			Sint32 fileRef = -1;
			std::string path;
			bool active = false;
		};

		// 開いている図面の一覧。**1 つ閉じるたびに取得し直す**（fileRef を使い回さない）。
		std::vector<OpenFile> OpenFiles()
		{
			std::vector<OpenFile> out;
			VectorWorks::TVWArray_OpenFileInformation list;
			gSDK->GetOpenFilesList(list);
			for (std::size_t i = 0; i < list.GetSize(); ++i)
			{
				const VectorWorks::SOpenFileInformation& info = list.GetAt(i);
				out.push_back(OpenFile{info.fFileRef, PathOf(info.fpFileID), info.fIsActive});
			}
			return out;
		}

		// 同じファイルを指しているか。**文字列が違っても同じことがある**（macOS の一時
		// ディレクトリは /var/… と /private/var/… の両方で呼べる）ので、文字列で決まらなければ
		// std::filesystem に確かめさせる（実プラグイン側の SamePath と同じ）。
		bool SamePath(const std::string& left, const std::string& right)
		{
			if (left.empty() || right.empty())
				return false;
			if (left == right)
				return true;
			std::error_code ec;
			return std::filesystem::equivalent(std::filesystem::path(left),
											   std::filesystem::path(right), ec) &&
				   !ec;
		}

		bool IsSameDocument(const OpenFile& file, const OwnedDocument& owned)
		{
			return file.fileRef == owned.fileRef && SamePath(file.path, owned.path);
		}

		// その図面をアクティブにし、**本当にそれがアクティブになったか**を確かめる。
		bool ActivateOwned(const OwnedDocument& doc)
		{
			if (!gSDK->SwitchToOpenFile(doc.fileRef))
				return false;
			for (const OpenFile& file : OpenFiles())
			{
				if (file.active && IsSameDocument(file, doc))
					return true;
			}
			return false;
		}

		void RemoveSavedFile(const OwnedDocument& doc)
		{
			if (doc.savedPath.empty())
				return;
			std::error_code ec;
			std::filesystem::remove(std::filesystem::path(doc.savedPath), ec);
		}

		bool StillOpen(const OwnedDocument& owned)
		{
			for (const OpenFile& file : OpenFiles())
			{
				if (IsSameDocument(file, owned))
					return true;
			}
			return false;
		}

		// **ブリッジが開いた図面を、保存せずに閉じる。** 人の図面は閉じない——切り替えた後に
		// アクティブな図面がその 1 枚であることを確かめてから CloseDocument を呼ぶ
		// （CloseDocument は確認なしに未保存の変更を捨てる。Findings「Documents」）。
		// 戻り値は報告の 1 行（何もしなければ空）。
		std::string CloseOwnedDocuments()
		{
			std::vector<OwnedDocument>& owned = OwnedDocuments();
			std::size_t closed = 0;
			std::vector<OwnedDocument> stuck;
			for (const OwnedDocument& doc : owned)
			{
				if (!StillOpen(doc))
				{
					RemoveSavedFile(doc); // 人が閉じた・再起動で消えた
					continue;
				}
				if (!ActivateOwned(doc))
				{
					stuck.push_back(doc);
					continue;
				}
				(void)gSDK->CloseDocument(); // 戻り値は当てにならない（上記）
				if (StillOpen(doc))
					stuck.push_back(doc);
				else
				{
					RemoveSavedFile(doc);
					++closed;
				}
			}
			owned = stuck;
			if (closed == 0 && stuck.empty())
				return "";
			std::string note =
				"ブリッジが開いた図面 " + std::to_string(closed) + " 枚を保存せずに閉じました";
			if (!stuck.empty())
				note += "（閉じられなかったもの " + std::to_string(stuck.size()) + " 枚）";
			return note;
		}

		// 一時ファイルの置き場（**まだ無い名前**を選ぶ。既にあるファイルへの別名保存が
		// 通らなかった実測が実プラグイン側にある——round 12）。作れなければ空。
		std::string FreshSavePath()
		{
			std::error_code ec;
			const std::filesystem::path dir =
				std::filesystem::path(TempDirectory()) / "VwSdkProbes-bridge-docs";
			std::filesystem::create_directories(dir, ec);
			if (!std::filesystem::is_directory(dir, ec))
				return "";
			const long long stamp = NowSeconds();
			for (int i = 1; i <= 100; ++i)
			{
				std::string name = "bridge-";
				name += std::to_string(stamp);
				name += "-";
				name += std::to_string(i);
				name += ".vwx";
				std::filesystem::path candidate = dir / name;
				if (!std::filesystem::exists(candidate, ec))
					return candidate.string();
			}
			return "";
		}

		// **ブリッジが開いた図面を一時ファイルへ保存する**（走らせた後に呼ぶ）。未保存の
		// 変更を残しておくと、**誰が頼んだ再起動でも**（実プラグイン側のブリッジの vw_restart
		// を含む）保存の確認で止まる——実プラグイン側は自分の実機テストの図面を同じ理由で
		// 一時ファイルへ保存している（M39）。保存したかは戻り値ではなく、アクティブな図面が
		// そのパスになったかで確かめる。戻り値は報告の 1 行（何もしなければ空）。
		std::string SaveOwnedDocuments()
		{
			std::size_t saved = 0;
			std::size_t failed = 0;
			for (OwnedDocument& doc : OwnedDocuments())
			{
				if (!StillOpen(doc))
					continue;
				const std::string target = FreshSavePath();
				if (target.empty() || !ActivateOwned(doc))
				{
					++failed;
					continue;
				}
				const VectorWorks::Filing::IFileIdentifierPtr fileID(
					VectorWorks::Filing::IID_FileIdentifier);
				if (!fileID || fileID->Set(TXString(target.c_str())) != kVCOMError_NoError ||
					gSDK->SaveActiveDocumentPath(fileID) != 0)
				{
					++failed;
					continue;
				}
				bool confirmed = false;
				for (const OpenFile& file : OpenFiles())
				{
					if (file.active && SamePath(file.path, target))
					{
						RemoveSavedFile(doc); // 前の周に保存したもの
						doc.fileRef = file.fileRef;
						doc.path = file.path;
						doc.savedPath = target;
						confirmed = true;
					}
				}
				if (confirmed)
					++saved;
				else
					++failed;
			}
			if (saved == 0 && failed == 0)
				return "";
			std::string note = "ブリッジが開いた図面を一時ファイルへ保存しました（" +
							   std::to_string(saved) + " 枚）";
			if (failed > 0)
				note += "。保存できなかったもの " + std::to_string(failed) +
						" 枚（再起動のときに保存の確認が出ることがあります）";
			return note;
		}

		// **新規の空図面を開く**（前にブリッジが開いた図面は先に閉じる）。開けたかは
		// 戻り値ではなく一覧の件数で確かめる（Findings「Documents」——戻り値を信じない）。
		bool OpenFreshDocument(std::vector<std::string>& notes, std::string& error)
		{
			const std::string closed = CloseOwnedDocuments();
			if (!closed.empty())
				notes.push_back(closed);
			const std::size_t before = OpenFiles().size();
			(void)gSDK->OpenDocumentPath(nullptr, false);
			const std::vector<OpenFile> after = OpenFiles();
			if (after.size() != before + 1)
			{
				error = "新規の図面を開けませんでした（開いている図面 " + std::to_string(before) +
						" → " + std::to_string(after.size()) + " 枚）。";
				return false;
			}
			for (const OpenFile& file : after)
			{
				if (file.active)
				{
					OwnedDocuments().push_back(OwnedDocument{file.fileRef, file.path});
					notes.push_back("新規の図面を開きました: " + file.path);
					return true;
				}
			}
			error = "新規の図面を開きましたが、アクティブな図面が見つかりません。";
			return false;
		}

		// =====================================================================
		// 道具の実装。
		// =====================================================================

		Json ProbeJson(const QuietProbe& probe)
		{
			Json value = Json::object();
			value.set("id", Json::string(probe.id));
			value.set("group", Json::string(probe.group));
			value.set("title", Json::string(probe.title));
			value.set("summary", Json::string(probe.summary));
			value.set("pr", Json::string(probe.pr));
			value.set("issue", Json::string(probe.issue));
			value.set("commit", Json::string(probe.commit));
			value.set("branch", Json::string(probe.branch));
			value.set("pr_title", Json::string(probe.prTitle));
			value.set("available", Json::boolean(probe.available));
			return value;
		}

		Json ShellJson()
		{
			Json value = Json::object();
			value.set("plugin", Json::string(PLUGIN_VWR_ID));
			value.set("build_id", Json::string(VW_BUILD_ID));
			value.set("shell_id", Json::string(VW_SHELL_ID));
			value.set("branch", Json::string(VW_BUILD_BRANCH));
			value.set("commit", Json::string(VW_BUILD_VERSION));
			value.set("built", Json::string(VW_BUILD_TIME));
			return value;
		}

		Json PingTool(const Json& /*args*/, std::string& /*error*/)
		{
			Json value = Json::object();
			value.set("shell", ShellJson());
			const QuietCatalog cat = ListProbesQuietly();
			value.set("catalog", Json::string(cat.catalogStamp));
			value.set("payload_build_id", Json::string(cat.buildId));
			value.set("probes", Json::integer(static_cast<long long>(cat.probes.size())));
			value.set("protocol", Json::integer(bridge::kProtocolVersion));
			value.set("pid", Json::integer(ProcessId()));
			value.set("document_open", Json::boolean(gSDK->GetCurrentLayer() != nullptr));
			value.set("open_documents", Json::integer(static_cast<long long>(OpenFiles().size())));
			value.set("bridge_documents",
					  Json::integer(static_cast<long long>(OwnedDocuments().size())));
			return value;
		}

		Json ProbesTool(const Json& /*args*/, std::string& error)
		{
			const QuietCatalog cat = ListProbesQuietly();
			if (cat.probes.empty() && !cat.error.empty())
			{
				error = cat.error;
				return Json::null();
			}
			Json list = Json::array();
			for (const QuietProbe& probe : cat.probes)
				list.push(ProbeJson(probe));
			Json value = Json::object();
			value.set("probes", list);
			value.set("count", Json::integer(static_cast<long long>(cat.probes.size())));
			value.set("payload_build_id", Json::string(cat.buildId));
			value.set("catalog", Json::string(cat.catalogStamp));
			value.set("shell", Json::string(cat.shellStamp));
			if (cat.skippedLines > 0)
				value.set("skipped_lines", Json::integer(static_cast<long long>(cat.skippedLines)));
			return value;
		}

		Json RunJson(const QuietRun& run, std::size_t maxBytes)
		{
			Json value = Json::object();
			value.set("probe", Json::string(run.id));
			value.set("group", Json::string(run.group));
			value.set("ran", Json::boolean(run.ran));
			value.set("failed", Json::boolean(run.failed || !run.ran));
			value.set("outcome", Json::string(run.outcome));
			value.set("seconds", Json::number(run.seconds));
			if (!run.advice.empty())
				value.set("advice", Json::string(run.advice));
			value.set("header", Strings(run.body));
			value.set("log_path", Json::string(run.logPath));
			value.set("log_bytes", Json::integer(static_cast<long long>(run.log.size())));
			value.set("log_truncated", Json::boolean(run.log.size() > maxBytes));
			value.set("log", Json::string(bridge::KeepTail(run.log, maxBytes)));
			return value;
		}

		// 走らせる前の下ごしらえ（図面の用意）。notes に経過を積む。
		//
		// **前の走行の残り（Feedback.h「落ちても拾う」）はここでは投稿しない。** ブリッジから
		// 走らせた周は投稿しない（ログは呼び出し側へ直に返る。RunProbesQuietly）ので、控えが
		// 残っているとしたらメニューから走らせた周のもの——それはメニューを開いたときに拾う。
		bool PrepareRun(const Json& args, std::vector<std::string>& notes, std::string& error)
		{
			if (BoolArg(args, "new_document", true))
				return OpenFreshDocument(notes, error);
			if (gSDK->GetCurrentLayer() == nullptr)
			{
				error = "図面が開いていません（new_document を true にすると新規の空図面を開いて"
						"から走らせます）。";
				return false;
			}
			return true;
		}

		Json RunTargets(const std::vector<QuietTarget>& targets, const Json& args,
						std::vector<std::string>& notes)
		{
			const std::size_t maxBytes = MaxBytesArg(args);
			// **プローブが閉じずに残した undo イベントは、ここで閉じる**（issue #247）。図形を
			// 作るだけでイベントは開き（Findings「Undo」）、メニューと違ってブリッジの実行は
			// VW のコマンドではないので、誰も閉じない——その間ブリッジは受け付けを見送り続け、
			// 止まったように見える（#245 で 7 分）。**走らせる前に開いていなかったときだけ**
			// 閉じる（人が開いているイベントは閉じない）。
			const bool buildingBefore = gSDK->IsCurrentlyBuildingAnUndoEvent();
			const std::vector<QuietRun> runs = RunProbesQuietly(targets);
			if (!buildingBefore && gSDK->IsCurrentlyBuildingAnUndoEvent())
			{
				(void)gSDK->EndUndoEvent();
				if (gSDK->IsCurrentlyBuildingAnUndoEvent())
					notes.push_back("残った undo イベントを閉じられませんでした");
				else
					notes.push_back("プローブが残した undo イベントを閉じました");
			}
			const std::string saved = SaveOwnedDocuments();
			if (!saved.empty())
				notes.push_back(saved);
			Json list = Json::array();
			std::size_t ok = 0;
			std::size_t failed = 0;
			std::size_t blocked = 0;
			for (const QuietRun& run : runs)
			{
				if (!run.ran)
					++blocked;
				else if (run.failed)
					++failed;
				else
					++ok;
				list.push(RunJson(run, maxBytes));
			}
			Json value = Json::object();
			value.set("shell", ShellJson());
			value.set("payload_build_id", Json::string(ListProbesQuietly().buildId));
			value.set("ok", Json::integer(static_cast<long long>(ok)));
			value.set("failed", Json::integer(static_cast<long long>(failed)));
			value.set("not_run", Json::integer(static_cast<long long>(blocked)));
			value.set("results", list);
			if (!notes.empty())
				value.set("notes", Strings(notes));
			return value;
		}

		Json RunProbeTool(const Json& args, std::string& error)
		{
			const std::string id = args.at("probe").asString();
			const std::string group = args.at("group").asString();
			if (id.empty())
			{
				error = "probe（slug）を渡してください。";
				return Json::null();
			}
			const QuietCatalog cat = ListProbesQuietly();
			std::vector<bridge::ProbeKey> keys;
			for (const QuietProbe& probe : cat.probes)
				keys.push_back(bridge::ProbeKey{probe.id, probe.group});
			const std::size_t index = bridge::FindProbe(keys, id, group, error);
			if (index == bridge::kNoProbe)
			{
				if (!cat.error.empty())
					error += "\n" + cat.error;
				return Json::null();
			}

			std::vector<std::string> notes;
			if (!PrepareRun(args, notes, error))
				return Json::null();
			return RunTargets({QuietTarget{cat.probes[index].id, cat.probes[index].group}}, args,
							  notes);
		}

		Json RunAllTool(const Json& args, std::string& error)
		{
			const QuietCatalog cat = ListProbesQuietly();
			if (cat.probes.empty())
			{
				error =
					cat.error.empty() ? std::string("走らせるプローブがありません。") : cat.error;
				return Json::null();
			}
			std::vector<QuietTarget> targets;
			for (const QuietProbe& probe : cat.probes)
				targets.push_back(QuietTarget{probe.id, probe.group});
			std::vector<std::string> notes;
			if (!PrepareRun(args, notes, error))
				return Json::null();
			return RunTargets(targets, args, notes);
		}

		// プローブのログファイルの在り処（本体の payload/Probe.cpp の defaultLogPath と同じ
		// 決め方。**綴りを変えたら両方と、Python 側の probe_log_candidates を直す**）。
		std::string LogPathFor(const std::string& id)
		{
			const char* const custom = std::getenv("VW_PROBE_LOG");
			if (custom != nullptr && *custom != '\0')
				return custom;
			std::string dir = TempDirectory();
			if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
				dir.pop_back();
			return dir + PathSeparator() + "VwSdkProbes-" + id + ".log";
		}

		Json ProbeLogTool(const Json& args, std::string& error)
		{
			const std::string id = args.at("probe").asString();
			if (!bridge::IsValidId(id))
			{
				error = "probe（slug）を渡してください。";
				return Json::null();
			}
			const std::string path = LogPathFor(id);
			std::string text;
			std::string why;
			if (!ReadTextFile(path, text, why))
			{
				error = "ログがまだありません（" + path + "）。";
				return Json::null();
			}
			const std::size_t limit = MaxBytesArg(args);
			Json value = Json::object();
			value.set("path", Json::string(path));
			value.set("bytes", Json::integer(static_cast<long long>(text.size())));
			value.set("truncated", Json::boolean(text.size() > limit));
			value.set("log", Json::string(bridge::KeepTail(text, limit)));
			return value;
		}

		// 再起動を頼む前の片付け（ブリッジが開いた図面の保存確認で止まらないように）。
		Json RestartJson(std::string& error)
		{
			Json value = Json::object();
			const std::string closed = CloseOwnedDocuments();
			if (!closed.empty())
				value.set("note", Json::string(closed));
			std::string why;
			if (!RequestRestart(why))
			{
				error = why;
				return Json::null();
			}
			// **頼んでから応答を書いてよい。** ヘルパーの終了要求は OS のイベントとして届き、
			// この刻みが戻ってから処理される（UpdateParse.h「入れ替えたあとの再起動」）。
			value.set("requested", Json::boolean(true));
			return value;
		}

		Json UpdateTool(const Json& args, std::string& error)
		{
			const RemoteUpdateResult update = RunRemoteUpdate();
			Json value = Json::object();
			value.set("outcome", Json::string(update.outcome));
			value.set("installed_before", Json::string(update.installed));
			value.set("latest", Json::string(update.latest));
			value.set("title", Json::string(update.title));
			value.set("probes", Json::string(update.probes));
			value.set("payload_only", Json::boolean(update.payloadOnly));
			if (!update.message.empty())
				value.set("message", Json::string(update.message));
			const bool needsRestart = update.outcome == "needs_restart";
			value.set("restart_required", Json::boolean(needsRestart));
			if (update.outcome == "failed" || update.outcome == "check_failed")
			{
				error =
					update.message.empty() ? std::string("更新できませんでした。") : update.message;
				return Json::null();
			}
			bool restarting = false;
			if (needsRestart && BoolArg(args, "restart_if_needed", false))
			{
				std::string why;
				const Json restart = RestartJson(why);
				restarting = why.empty();
				if (!restarting)
					value.set("restart_error", Json::string(why));
				else if (restart.has("note"))
					value.set("note", restart.at("note"));
			}
			value.set("restarting", Json::boolean(restarting));
			return value;
		}

		Json RestartTool(const Json& /*args*/, std::string& error)
		{
			return RestartJson(error);
		}

		Json CleanupTool(const Json& /*args*/, std::string& /*error*/)
		{
			const std::string closed = CloseOwnedDocuments();
			Json value = Json::object();
			value.set("done", Json::boolean(OwnedDocuments().empty()));
			value.set(
				"message",
				Json::string(closed.empty() ? std::string("片付けるものはありません。") : closed));
			return value;
		}

		// =====================================================================
		// 道具の表（**ここが唯一の真実**。Python サーバは `vw_tools` でこれを読む）。
		// =====================================================================

		using ToolFn = Json (*)(const Json& args, std::string& error);

		enum class ToolKind
		{
			Read, // その場で答える
			Long, // 時間がかかる（生存の印に busy_until を書いてから実行する）
		};

		struct Tool
		{
			const char* name;
			const char* description;
			const char* schema;
			ToolFn run;
			ToolKind kind;
			int timeoutSeconds; // Python が応答を待つ上限（0 = 既定）
		};

		constexpr int kRunProbeTimeoutSeconds = 1800;
		constexpr int kRunAllTimeoutSeconds = 3600;
		constexpr int kUpdateTimeoutSeconds = 600;
		constexpr int kRestartTimeoutSeconds = 60;

#define VW_RUN_ARGS                                                                                                                                                                                \
	R"("new_document":{"type":"boolean","description":"新規の空図面を開いてから走らせるか（既定 true。前にブリッジが開いた図面は保存せずに閉じる）"},)" \
	R"("max_bytes":{"type":"integer","description":"1 件のログを返す上限（バイト。既定 60000。超えたら古いほうを削る）"})"

		const auto kTools = std::to_array<Tool>({
			{"vw_ping",
			 "ブリッジが生きているかと、入っているプラグイン（殻と本体）の素性・開いている図面の数"
			 "を返す。",
			 R"({"type":"object","properties":{},"additionalProperties":false})", &PingTool,
			 ToolKind::Read, 0},
			{"vw_probes",
			 "このビルドに入っているプローブの一覧（slug・群・表示名・概要・出所の PR / issue・"
			 "本体が置かれているか）を、ピッカーと同じ並びで返す。",
			 R"({"type":"object","properties":{},"additionalProperties":false})", &ProbesTool,
			 ToolKind::Read, 0},
			{"vw_run_probe",
			 "プローブを 1 件走らせ、結末・所要・ログ全文を返す（メニューの「実行」と同じ道。"
			 "ダイアログは出さない）。既定では新規の空図面を開いてから走らせる。同じ slug が"
			 "複数の群にあるときは group で選ぶ。Vectorworks が起動していなければ起動してから"
			 "走らせる。プローブが Vectorworks ごと落としたときは、落ちるまでのログを返す。",
			 R"({"type":"object","properties":{)"
			 R"("probe":{"type":"string","description":"プローブの slug（vw_probes の id）"},)"
			 R"("group":{"type":"string","description":"群（main / pr12 など。同じ slug が複数あるときだけ要る）"},)" VW_RUN_ARGS
			 R"(},"required":["probe"],"additionalProperties":false})",
			 &RunProbeTool, ToolKind::Long, kRunProbeTimeoutSeconds},
			{"vw_run_all",
			 "一覧のプローブを上から順に全部走らせ、1 件ずつの結末とログを返す（メニューの"
			 "「一覧のプローブをすべて順に実行」と同じ）。走らせられない件は飛ばして続ける。",
			 R"({"type":"object","properties":{)" VW_RUN_ARGS R"(},"additionalProperties":false})",
			 &RunAllTool, ToolKind::Long, kRunAllTimeoutSeconds},
			{"vw_probe_log",
			 "プローブのログファイル（一時ディレクトリの VwSdkProbes-<slug>.log）を読む。"
			 "最後に走らせたときのものが残っている。",
			 R"({"type":"object","properties":{)"
			 R"("probe":{"type":"string","description":"プローブの slug"},)"
			 R"("max_bytes":{"type":"integer","description":"返す上限（バイト。既定 60000）"}},)"
			 R"("required":["probe"],"additionalProperties":false})",
			 &ProbeLogTool, ToolKind::Read, 0},
			{"vw_update",
			 "公開されている最新のプローブビルド（タグ probes）を確かめ、新しければ入れる"
			 "（尋ねない。ピッカー先頭の「入れ替える」と同じ道）。本体だけの入れ替えなら"
			 "再起動なしでそのまま効く（outcome=installed）。殻まで変わったビルドは再起動するまで"
			 "効かない（outcome=needs_restart）——restart_if_needed を true "
			 "にすると続けて再起動する。",
			 R"({"type":"object","properties":{)"
			 R"("restart_if_needed":{"type":"boolean","description":"再起動が要るなら続けて再起動するか（既定 false）"}},)"
			 R"("additionalProperties":false})",
			 &UpdateTool, ToolKind::Long, kUpdateTimeoutSeconds},
			{"vw_restart",
			 "Vectorworks を再起動する（ブリッジが開いた図面は先に保存せずに閉じる）。人が開いて"
			 "いる図面に未保存の変更があれば、Vectorworks の保存の確認が出る（人の応答が要る）。",
			 R"({"type":"object","properties":{},"additionalProperties":false})", &RestartTool,
			 ToolKind::Long, kRestartTimeoutSeconds},
			{"vw_cleanup",
			 "ブリッジが開いた図面を保存せずに閉じる。vw_lock_release が占有を解くときに自動で"
			 "呼ぶので、ふつうは直接呼ばない。",
			 R"({"type":"object","properties":{},"additionalProperties":false})", &CleanupTool,
			 ToolKind::Long, kRestartTimeoutSeconds},
		});

#undef VW_RUN_ARGS

		Json ToolCatalog()
		{
			Json list = Json::array();
			for (const Tool& tool : kTools)
			{
				Json entry = Json::object();
				entry.set("name", Json::string(tool.name));
				entry.set("description", Json::string(tool.description));
				Json schema;
				std::string error;
				if (!Json::parse(tool.schema, schema, error))
					schema = Json::object(); // 表の綴り間違いで一覧ごと失敗させない
				entry.set("inputSchema", schema);
				if (tool.timeoutSeconds > 0)
					entry.set("timeoutSeconds", Json::integer(tool.timeoutSeconds));
				list.push(entry);
			}
			Json value = Json::object();
			value.set("tools", list);
			value.set("protocol", Json::integer(bridge::kProtocolVersion));
			return value;
		}

		const Tool* FindTool(const std::string& name)
		{
			for (const Tool& tool : kTools)
			{
				if (name == tool.name)
					return &tool;
			}
			return nullptr;
		}

		bridge::Response Handle(const bridge::Request& request)
		{
			bridge::Response response;
			response.id = request.id;
			try
			{
				if (request.tool == "vw_tools")
				{
					response.ok = true;
					response.result = ToolCatalog();
					return response;
				}
				const Tool* const tool = FindTool(request.tool);
				if (tool == nullptr)
					return bridge::Failure(request.id, "知らない道具です: " + request.tool);
				std::string error;
				const Json result = tool->run(request.args, error);
				if (!error.empty())
					return bridge::Failure(request.id, error);
				response.ok = true;
				response.result = result.isNull() ? Json::object() : result;
			}
			catch (const std::exception& e)
			{
				return bridge::Failure(request.id, std::string("例外: ") + e.what());
			}
			catch (...)
			{
				return bridge::Failure(request.id, "不明な例外");
			}
			return response;
		}

		// =====================================================================
		// 受け付け（殻の時計が 1 回ずつ呼ぶ）。
		// =====================================================================

		struct ServeState
		{
			bool prepared = false;
			std::string dir;
			std::string error;
			long long lastPrepareAt = -1;
			long long lastBeat = 0;
			long long served = 0;
			long long failed = 0;
			// 前面の図面で undo イベントが開いていて、受け付けを見送り始めた時刻（0 = 見送って
			// いない）。生存の印に載せる——載せないと、呼ぶ側には理由の分からない無応答に見える。
			long long deferredSince = 0;
		};

		ServeState& State()
		{
			static ServeState sState;
			return sState;
		}

		std::string SpoolDirectory()
		{
			const char* const chosen = std::getenv("VWPROBES_MCP_SPOOL");
			if (chosen != nullptr && *chosen != '\0')
				return {chosen};
			return bridge::SpoolDir(TempDirectory(), PLUGIN_VWR_ID);
		}

		Json StatusJson(const ServeState& state, const std::string& busyTool = {},
						long long busyUntil = 0)
		{
			Json value = Json::object();
			value.set("plugin", Json::string(PLUGIN_VWR_ID));
			value.set("build_id", Json::string(VW_BUILD_ID));
			value.set("shell_id", Json::string(VW_SHELL_ID));
			value.set("branch", Json::string(VW_BUILD_BRANCH));
			value.set("commit", Json::string(VW_BUILD_VERSION));
			value.set("protocol", Json::integer(bridge::kProtocolVersion));
			value.set("pid", Json::integer(ProcessId()));
			value.set("beat", Json::integer(NowSeconds()));
			value.set("served", Json::integer(state.served));
			value.set("failed", Json::integer(state.failed));
			if (state.deferredSince > 0)
			{
				value.set("deferred", Json::string("undo"));
				value.set("deferred_since", Json::integer(state.deferredSince));
			}
			if (!busyTool.empty())
			{
				value.set("busy", Json::string(busyTool));
				value.set("busy_until", Json::integer(busyUntil));
			}
			return value;
		}

		bool Prepare(ServeState& state, long long now)
		{
			if (state.prepared)
				return true;
			if (state.lastPrepareAt >= 0 && now - state.lastPrepareAt < kPrepareRetrySeconds)
				return false;
			state.lastPrepareAt = now;
			state.dir = SpoolDirectory();
			const bridge::Spool spool(state.dir);
			if (!spool.prepare(state.error))
				return false;
			// **生きた橋がいなければ**前の回の残骸を消す（異常終了した回の応答を取り違えない）。
			if (!spool.statusIsLive(now, bridge::kStatusStaleSeconds))
				(void)spool.sweep();
			state.error.clear();
			state.prepared = true;
			return true;
		}

		// **undo イベントが開いている間は受け付けない**が、生存の印だけは書き続ける（理由を
		// 載せて）。印はファイルを書くだけで gSDK を呼ばないので、開いているイベントに混ざる
		// ものは無い。
		void NoteDeferred()
		{
			ServeState& state = State();
			const long long now = NowSeconds();
			if (state.deferredSince == 0)
			{
				state.deferredSince = now;
				state.lastBeat = 0; // 見送り始めたことをすぐ知らせる
			}
			if (!Prepare(state, now) || now - state.lastBeat < kStatusBeatSeconds)
				return;
			const bridge::Spool spool(state.dir);
			std::string statusError;
			if (spool.writeStatus(StatusJson(state), statusError))
				state.lastBeat = now;
		}

		void ServeOnce()
		{
			ServeState& state = State();
			long long now = NowSeconds();
			if (state.deferredSince != 0)
			{
				state.deferredSince = 0;
				state.lastBeat = 0; // 見送りが終わったことをすぐ知らせる
			}
			if (!Prepare(state, now))
				return;
			const bridge::Spool spool(state.dir);
			std::vector<std::string> broken;
			const std::vector<bridge::Request> requests = spool.poll(broken);

			std::string replyError;
			for (const std::string& id : broken)
			{
				(void)spool.reply(
					bridge::Failure(id, "要求を読めませんでした（壊れているか大きすぎます）。"),
					replyError);
				++state.failed;
			}

			for (const bridge::Request& request : requests)
			{
				const Tool* const tool = FindTool(request.tool);
				const bool longRun = tool != nullptr && tool->kind == ToolKind::Long;
				if (longRun)
				{
					// 走っている間は印を書き直せないので、先に「いつまでかかりうるか」を書く。
					std::string statusError;
					(void)spool.writeStatus(
						StatusJson(state, tool->name, now + tool->timeoutSeconds), statusError);
				}
				const bridge::Response response = Handle(request);
				(void)spool.reply(response, replyError);
				if (response.ok)
					++state.served;
				else
					++state.failed;
				if (longRun)
				{
					now = NowSeconds();
					state.lastBeat = 0; // 印をすぐ書き直す（busy を外す）
				}
				// **再起動を頼んだら、それ以上は受けない**（次の要求は新しい Vectorworks が受ける）。
				if (response.ok && (request.tool == "vw_restart" ||
									(request.tool == "vw_update" &&
									 response.result.at("restarting").asBool(false))))
					return;
			}

			if (now - state.lastBeat >= kStatusBeatSeconds)
			{
				std::string statusError;
				if (spool.writeStatus(StatusJson(state), statusError))
					state.lastBeat = now;
				else
				{
					// **書けなくなったら用意し直す**（一時ディレクトリが掃除された等）。
					state.prepared = false;
					state.error = "生存の印を書けません: " + statusError;
				}
			}
		}

		// =====================================================================
		// 殻の時計（OS のタイマー）。
		// =====================================================================

		// 起動の直後は待つ（起動の最中に本体やスクリプトを触らない）。間隔は前提にしない
		// （平均 250 ms 強・数秒空くことがある。Findings「Timers and Notifications」§4）。
		constexpr long long kClockFirstTickSeconds = 10;
		constexpr unsigned kClockIntervalMs = 250;

		long long& ClockStartedAt()
		{
			static long long sStartedAt = 0;
			return sStartedAt;
		}

		// NOLINTBEGIN(bugprone-empty-catch): OS のタイマーのコールバックへ例外を漏らさない。
		// 受け付けの失敗は応答に載る。
		void ClockTick()
		{
			static bool sServing = false;
			if (sServing)
				return; // 入れ子で入らない（走らせている最中にイベントが処理されうる）
			try
			{
				if (gSDK == nullptr || NowSeconds() < ClockStartedAt() + kClockFirstTickSeconds)
					return;
				if (ProbeMenuActive())
					return;
				sServing = true;
				if (gSDK->IsCurrentlyBuildingAnUndoEvent())
					NoteDeferred();
				else
					ServeOnce();
			}
			catch (...)
			{
			}
			sServing = false;
		}
		// NOLINTEND(bugprone-empty-catch)

#if GS_MAC
		void OnClock(CFRunLoopTimerRef /*timer*/, void* /*info*/)
		{
			ClockTick();
		}
#else
		void CALLBACK OnClock(HWND /*hwnd*/, UINT /*msg*/, UINT_PTR /*id*/, DWORD /*time*/)
		{
			ClockTick();
		}
#endif
	} // namespace

	void StartMcpBridgeClock()
	{
		static bool sStarted = false;
		if (sStarted)
			return;
		sStarted = true;
		ClockStartedAt() = NowSeconds();
#if GS_MAC
		// **既定のモードにだけ登録する**（共通モードだと Vectorworks のモーダルの最中にも
		// 刻み、開いている undo の記録へ書き込みが混ざる。Findings「Timers and
		// Notifications」§6）。Vectorworks が終わるまで持ち続ける（解放しない）。
		static CFRunLoopTimerRef sTimer = nullptr;
		sTimer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent(),
									  kClockIntervalMs / 1000.0, 0, 0, &OnClock, nullptr);
		if (sTimer != nullptr)
			CFRunLoopAddTimer(CFRunLoopGetMain(), sTimer, kCFRunLoopDefaultMode);
#else
		// **先に殻を留める**（issue #253）。Windows の VW は起動時に殻を読み込んで -1・-3 を
		// 呼んだ後、-2 を送らずに一度降ろし、別の番地へ読み込み直して -4・-5 を呼ぶ。留めずに
		// タイマーを作ると、最初の読み込みのタイマーが**もう無い殻の OnClock の番地**を呼び続け、
		// そこへ後から入った別のモジュールのコードを踏んで起動中に落ちる（Findings
		// 「Timers and Notifications」§4）。留めれば VW の FreeLibrary では降りず、番地が生き続ける。
		// 留められなければ時計を動かさない（ブリッジが使えないだけで、落ちるよりよい）。
		constexpr DWORD kPinSelf =
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		HMODULE self = nullptr;
		if (!GetModuleHandleExW(kPinSelf, reinterpret_cast<LPCWSTR>(&OnClock), &self))
			return;
		// ウィンドウを持たないタイマー。WM_TIMER は Vectorworks のメッセージポンプが配る。
		(void)SetTimer(nullptr, 0, kClockIntervalMs, &OnClock);
#endif
	}
} // namespace vwprobe
