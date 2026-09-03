//
//	payload/PayloadMain.cpp
//
//	**本体（ペイロード）の入口。** Vectorworks はこのモジュールを知らない——読み込むのは
//	殻（plugin/src/PayloadHost.cpp）で、境界は C の ABI（PayloadAbi.h）。だから**降ろして、
//	置き換えて、読み直せる**＝プローブの入れ替えに Vectorworks の再起動が要らない。
//
//	ここが持っているのは「プローブを数えて、名乗って、走らせる」ところまで。ダイアログも
//	メニューも更新も殻の側にある（そちらは滅多に変わらない＝再起動も滅多に要らない）。
//
//	【SDK をどう使えるようにするか】gSDK / gCBP / gVWMM は静的ライブラリ（libVWSDK.a /
//	VWSDK.lib）が持つ**モジュールごとのグローバル**である。このモジュールは自分の複製を
//	持っているので、読み込んだだけでは全部 nil のまま。殻が受け取った CallBackPtr を
//	もらって ::GS_InitializeVCOM へ渡すと、そこで埋まる——普通のプラグインの
//	plugin_module_main がやっているのと同じことを、外から材料をもらって行う形
//	（Findings「プラグインモジュールの読み込みと入れ替え」で実測済み）。
//
//	【境界を越えさせないもの】例外（すべてここで受ける）、C++ のオブジェクト、
//	降ろした後も使われる文字列（返す const char* は殻がその場で写す約束）。
//

#include "PluginPrefix.h"

#include "BuildConfig.h"
#include "PayloadAbi.h"
#include "PayloadHostHolder.h"
#include "Probe.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

// CMake が -D で渡す（このビルドの素性）。渡らないローカルビルドでも壊れないように。
#ifndef VW_PAYLOAD_BUILD_ID
#	define VW_PAYLOAD_BUILD_ID "local"
#endif
#ifndef VW_PAYLOAD_COMMIT
#	define VW_PAYLOAD_COMMIT "local"
#endif
#ifndef VW_PAYLOAD_BRANCH
#	define VW_PAYLOAD_BRANCH "local"
#endif
#ifndef VW_PAYLOAD_BUILD_TIME
#	define VW_PAYLOAD_BUILD_TIME "unknown"
#endif

namespace
{
	using namespace vwprobe;

	// **殻から渡されたものは、ポインタで持たずに写す。** そうしないと、殻の load から
	// 戻った時点で腐ったポインタを持つことになる——実際にそれで Vectorworks ごと
	// 落とした（理由と落ち方は PayloadHostHolder.h）。
	payload::HostHolder gHost;
	bool gPayloadReady = false;

	// **殻へ返す文字列の置き場。** 返した const char* は「次にペイロードを呼ぶまで」
	// 生きていればよい（PayloadAbi.h）ので、呼び出しごとにここへ作り直す。1 か所へ
	// まとめてあるのは、寿命の規則を 1 つに保つため。
	struct Held
	{
		std::string outcome;
		std::string logPath;
	};

	Held& held()
	{
		static Held sHeld;
		return sHeld;
	}

	// Report が 1 行書くたびに呼ぶ受け口。そのまま殻へ流す。
	void SinkLine(void* ctx, const char* line)
	{
		(void)ctx;
		gHost.log(line);
	}

	void Log(const std::string& line)
	{
		SinkLine(nullptr, line.c_str());
	}

	// このビルドの素性（プローブのログの見出しに出す 1 行）。
	std::string buildStamp()
	{
		return std::string("本体: ") + VW_PAYLOAD_BRANCH + " " + VW_PAYLOAD_COMMIT + " (" +
			   VW_PAYLOAD_BUILD_TIME + ") id=" + VW_PAYLOAD_BUILD_ID;
	}

	// 出所を 1 行に畳む（無ければ「ローカル」）。
	std::string provenanceLine(const std::string& id)
	{
		const Provenance* origin = provenanceOf(id);
		if (origin == nullptr)
			return "ローカル（出所の記録なし）";

		// 見出しは PR 番号。無ければ取り込み元のブランチ（ふつうは main）を見出しに
		// 使い、**そのときは末尾でブランチを繰り返さない**。
		std::string line;
		const bool hasPr = !origin->pr.empty();
		if (hasPr)
			line += "PR #" + origin->pr;
		else
			line += origin->branch.empty() ? std::string("main") : origin->branch;
		if (!origin->commit.empty())
			line += " / " + origin->commit;
		if (hasPr && !origin->branch.empty())
			line += " / " + origin->branch;
		if (!origin->title.empty())
			line += " / " + origin->title;
		return line;
	}

	// プローブ 1 件を走らせる。**例外はここで受け止める**（境界を越えさせない）。
	// 落ちたところまでのログはファイルにも残っているので、例外の種類と併せて見せる。
	int RunOne(const Probe& probe, VwPayloadResult* out)
	{
		Report report;
		report.setSink(nullptr, &SinkLine);
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

		held().outcome = outcome;
		held().logPath = report.logPath();

		out->failed = (aborted.empty() && !report.failed()) ? 0 : 1;
		out->outcome = held().outcome.c_str();
		out->logPath = held().logPath.c_str();
		out->seconds = seconds;
		return kVwPayloadOk;
	}
} // namespace

// ---------------------------------------------------------------------------
// **SDK の静的ライブラリをリンクするモジュールが必ず定義しなければならない 2 つ。**
// どちらも libVWSDK.a / VWSDK.lib の中から参照されるので、Vectorworks にプラグインとして
// 登録されないこのモジュールでも要る（無いとリンクで未解決になる。実ビルドで確認。
// Findings「プラグインモジュールの読み込みと入れ替え」）。

// ① GS_InitializeVCOM が呼ぶ（Include/VectorworksSDK.h:56-61 の注記どおり）。
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_ver()
{
	return SDK_VERSION;
}

// ② リソース（.vwr）の識別子。TXResStr / TXLegacyResource / GS_GetLayoutFromRsrc から
//    参照される。**このモジュールは .vwr を持たず、リソースを引きもしない**が、
//    リンクを通すために定義だけ要る。値は殻と同じものにしておく（引かれないので害は無い）。
const char* DefaultPluginVWRIdentifier()
{
	return PLUGIN_VWR_ID;
}

// ---------------------------------------------------------------------------
// ここから下が殻との境界（PayloadAbi.h）。**例外を外へ出さない。**

VW_PAYLOAD_EXPORT unsigned int vw_payload_abi_version()
{
	return VW_PAYLOAD_ABI_VERSION;
}

VW_PAYLOAD_EXPORT int vw_payload_init(const VwPayloadHost* host)
{
	try
	{
		// **受け取ってその場で写す**（版と大きさの確認も入れ物の側でやる）。以降、殻から
		// 渡された記憶域には二度と触らない。
		const int adopted = gHost.adopt(host);
		if (adopted != kVwPayloadOk)
			return adopted;

		// **ここが要（かなめ）。** 自分の側の gSDK / gCBP / gVWMM を埋める。
		const VCOMError err = ::GS_InitializeVCOM(gHost.callbacks());
		if (err != kVCOMError_NoError)
		{
			Log("[payload] GS_InitializeVCOM が失敗: " + std::to_string((long)err));
			gHost.forget();
			return kVwPayloadErrVcom;
		}
		if (gSDK == nil)
		{
			Log("[payload] GS_InitializeVCOM は通ったが gSDK が nil のまま");
			gHost.forget();
			return kVwPayloadErrVcom;
		}

		gPayloadReady = true;
		return kVwPayloadOk;
	}
	catch (...)
	{
		gHost.forget();
		gPayloadReady = false;
		return kVwPayloadErrException;
	}
}

VW_PAYLOAD_EXPORT int vw_payload_info(VwPayloadInfo* out)
{
	try
	{
		// **init の前でも答える。** 殻は「読んだものが何か」を先に言えたほうがよい
		// （ABI が合わずに捨てるときも、何を捨てたのか出せる）。
		if (out == nullptr || out->size < sizeof(VwPayloadInfo))
			return kVwPayloadErrAbi;
		out->buildId = VW_PAYLOAD_BUILD_ID;
		out->commit = VW_PAYLOAD_COMMIT;
		out->branch = VW_PAYLOAD_BRANCH;
		out->buildTime = VW_PAYLOAD_BUILD_TIME;
		out->probeCount = static_cast<unsigned int>(probes().size());
		return kVwPayloadOk;
	}
	catch (...)
	{
		return kVwPayloadErrException;
	}
}

VW_PAYLOAD_EXPORT int vw_payload_probe_at(unsigned int index, VwPayloadProbe* out)
{
	try
	{
		if (out == nullptr || out->size < sizeof(VwPayloadProbe))
			return kVwPayloadErrAbi;
		const std::vector<Probe>& all = probes();
		if (index >= all.size())
			return kVwPayloadErrUnknownId;

		const Probe& probe = all[index];
		out->id = probe.id.c_str();
		out->title = probe.title.c_str();
		out->summary = probe.summary.c_str();

		// 出所は別の表から引く（無いことがある＝ローカルで足しただけのプローブ）。
		// **空文字を返す**——殻が nullptr を気にしなくて済むように。
		static const char* const kEmpty = "";
		const Provenance* origin = provenanceOf(probe.id);
		out->pr = (origin != nullptr) ? origin->pr.c_str() : kEmpty;
		out->commit = (origin != nullptr) ? origin->commit.c_str() : kEmpty;
		out->branch = (origin != nullptr) ? origin->branch.c_str() : kEmpty;
		out->prTitle = (origin != nullptr) ? origin->title.c_str() : kEmpty;
		return kVwPayloadOk;
	}
	catch (...)
	{
		return kVwPayloadErrException;
	}
}

VW_PAYLOAD_EXPORT int vw_payload_run(const char* id, VwPayloadResult* out)
{
	try
	{
		if (out == nullptr || out->size < sizeof(VwPayloadResult))
			return kVwPayloadErrAbi;
		if (!gPayloadReady || gSDK == nil)
			return kVwPayloadErrNotInit;

		const std::string which = (id != nullptr) ? id : "";
		for (const Probe& probe : probes())
		{
			if (probe.id == which)
				return RunOne(probe, out);
		}
		return kVwPayloadErrUnknownId;
	}
	catch (const std::exception& error)
	{
		Log(std::string("[payload] 例外: ") + error.what());
		return kVwPayloadErrException;
	}
	catch (...)
	{
		Log("[payload] 素性の分からない例外");
		return kVwPayloadErrException;
	}
}

VW_PAYLOAD_EXPORT void vw_payload_shutdown()
{
	// 降ろす直前に殻が呼ぶ。**殻へ渡したものを手放す**のがここの仕事——このモジュールの
	// 番地を持たれたまま降ろすと、次に触った瞬間に落ちる。
	gPayloadReady = false;
	gHost.forget();
}
