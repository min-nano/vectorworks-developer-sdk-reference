//
//	probes/runtime/hot-reload/probe.cpp
//
//	**プラグイン本体を外部モジュールへ出して、Vectorworks を再起動せずに入れ替えられるか**
//	を実機で確かめる（issue #15）。図面は読むだけで、何も作らず・何も変えない。
//
//	筋書きはこう。同じソースから中身の違う 2 つのモジュール（変種 A / B）を作ってあるので、
//	それを**世代 1 → 世代 2**として順に読み込み、間で降ろす:
//
//	  世代 1: A を一時ディレクトリへ写す → 読み込む → SDK を使わせる → 【読み込み中の
//	          ファイルを消せるか試す】→ 降ろす → 本当に降りたか確かめる → 消す
//	  世代 2: B で同じことをする
//	  判定  : 2 つの describe が**違う**なら、Vectorworks を起動したまま
//	          「別のコードに入れ替わった」ことになる。
//
//	確かめている 4 点（どれか 1 つでも駄目なら、この道は成り立たない）:
//	  1. Vectorworks のプロセス内で自前のモジュールを読み込めるか（mac の署名検証を含む）
//	  2. そのモジュールから SDK が使えるか（GS_InitializeVCOM に殻の CallBackPtr を渡す）
//	  3. 降ろせるか（dlclose / FreeLibrary で本当にプロセスから消えるか）
//	  4. 置き換えて読み直せるか（＝再起動が要らないか）
//
//	【このプローブが動かないビルドがある】ペイロードは plugin/ 側の変更なので、
//	**その変更を含まないビルド**（main へ入る前の dispatch ビルドなど）では同梱物が無い。
//	そのときは「ペイロードが見つからない」と出して終わる（それも結果として意味がある）。
//

#include "Probe.h"

#include "PayloadAbi.h"
#include "PayloadHost.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
	using vwprobe::PayloadModule;
	using vwprobe::Report;

	std::string Hex(const void* p)
	{
		char buf[32] = {0};
		(void)std::snprintf(buf, sizeof(buf), "0x%llx",
							static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(p)));
		return buf;
	}

	// ペイロードから殻へログを渡す橋。**C の関数ポインタとして境界を越える**ので、
	// 例外を絶対に外へ出さない（越えた先はペイロード側で、巻き戻せない）。
	void LogBridge(void* ctx, const char* line)
	{
		try
		{
			if (ctx != nullptr)
				static_cast<Report*>(ctx)->log((line != nullptr) ? line : "");
		}
		catch (...)
		{
			// 握り潰す（ログが 1 行落ちるだけ。境界を壊すよりよい）
		}
	}

	std::string StatusName(int status)
	{
		switch (status)
		{
		case kVwPayloadOk:
			return "ok";
		case kVwPayloadErrAbi:
			return "ABI 不一致";
		case kVwPayloadErrHost:
			return "殻から渡したものが足りない";
		case kVwPayloadErrVcom:
			return "GS_InitializeVCOM に失敗";
		case kVwPayloadErrNotInit:
			return "init が済んでいない";
		case kVwPayloadErrUnknownId:
			return "知らない id";
		case kVwPayloadErrException:
			return "例外";
		default:
			return "不明(" + std::to_string(status) + ")";
		}
	}

	long long MillisSince(const std::chrono::steady_clock::time_point& from)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - from)
			.count();
	}

	// 1 世代ぶんの読み込み〜アンロード。成功したら describe の文字列を outDescription へ。
	// 【第 1 世代だけ】読み込み中のファイルを消せるか試す（Windows と mac の違いを見る）。
	bool RunGeneration(Report& probe, const std::string& variant, const std::string& tag,
					   bool tryDeleteWhileLoaded, std::string& outDescription)
	{
		probe.log("── 世代 " + tag + "（変種 " + variant + "）");

		const std::string source = vwprobe::BundledPayloadPath(variant);
		if (source.empty())
		{
			probe.fail("ペイロードの置き場所を割り出せなかった（自分の位置が取れない）");
			return false;
		}
		probe.log("  同梱物: " + source);

		const std::string tempDir = vwprobe::TempDirectory();
		if (tempDir.empty())
		{
			probe.fail("一時ディレクトリが取れなかった");
			return false;
		}
		const std::string copy = vwprobe::payload::TempCopyPath(
			tempDir, tag, vwprobe::payload::FileName(variant), vwprobe::PathSeparator());

		// ① 複製する（＝「新しいビルドを落としてきた」に相当）。ここで失敗するなら、
		//    そもそも同梱物が無い（ペイロードを含まないビルド）。
		std::string error;
		if (!vwprobe::CopyFileTo(source, copy, error))
		{
			probe.fail("複製できなかった: " + error);
			probe.log("  （このビルドにペイロードが同梱されていない可能性が高い）");
			return false;
		}
		probe.log("  複製先: " + copy);

		// ② 読み込む。
		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		PayloadModule payload;
		if (!payload.open(copy, error))
		{
			probe.fail("読み込めなかった: " + error);
			return false;
		}
		probe.log("  読み込めた: handle=" + Hex(payload.handle()) + " " +
				  std::to_string(MillisSince(started)) + "ms");

		// ③ export を引く。
		auto abiFn = reinterpret_cast<VwPayloadAbiVersionFn>(payload.symbol(VW_PAYLOAD_SYM_ABI));
		auto initFn = reinterpret_cast<VwPayloadInitFn>(payload.symbol(VW_PAYLOAD_SYM_INIT));
		auto describeFn =
			reinterpret_cast<VwPayloadDescribeFn>(payload.symbol(VW_PAYLOAD_SYM_DESCRIBE));
		auto runFn = reinterpret_cast<VwPayloadRunFn>(payload.symbol(VW_PAYLOAD_SYM_RUN));
		auto shutdownFn =
			reinterpret_cast<VwPayloadShutdownFn>(payload.symbol(VW_PAYLOAD_SYM_SHUTDOWN));
		if (abiFn == nullptr || initFn == nullptr || describeFn == nullptr || runFn == nullptr ||
			shutdownFn == nullptr)
		{
			probe.fail("export が揃っていない（境界の綴りが食い違っている？）");
			(void)payload.close(error);
			return false;
		}

		const unsigned int abi = abiFn();
		probe.log("  ABI: ペイロード=" + std::to_string(abi) +
				  " 殻=" + std::to_string(VW_PAYLOAD_ABI_VERSION));
		if (abi != VW_PAYLOAD_ABI_VERSION)
		{
			probe.fail("ABI が食い違っている");
			(void)payload.close(error);
			return false;
		}

		// describe が返す const char* は**その場で写す**（降ろした後は消えている）。
		outDescription = describeFn();
		probe.log("  素性: " + outDescription);

		// ④ SDK を使えるようにして、使わせる。**gCBP は殻が GS_InitializeVCOM で
		//    受け取った CallBackPtr そのもの**（SDKLib の MiniCadHookIntf.h が公開している）。
		VwPayloadHost host{};
		host.size = static_cast<unsigned int>(sizeof(VwPayloadHost));
		host.abiVersion = VW_PAYLOAD_ABI_VERSION;
		host.callbacks = (void*)gCBP;
		host.logCtx = &probe;
		host.log = &LogBridge;

		const int initStatus = initFn(&host);
		probe.log("  init: " + StatusName(initStatus));
		if (initStatus != kVwPayloadOk)
		{
			probe.fail("ペイロード側で SDK を初期化できなかった（" + StatusName(initStatus) + "）");
			(void)payload.close(error);
			return false;
		}

		const int runStatus = runFn("self-test");
		probe.log("  run: " + StatusName(runStatus));
		if (runStatus != kVwPayloadOk)
			probe.fail("ペイロードの自己診断が通らなかった（" + StatusName(runStatus) + "）");

		// ⑤【第 1 世代だけ】読み込み中のファイルを消せるか。Windows は消せないはずで、
		//    それが「世代ごとに別名で置く」設計の根拠になる。
		if (tryDeleteWhileLoaded)
		{
			std::string whyNot;
			const bool deleted = vwprobe::RemoveFileAt(copy, whyNot);
			probe.log(std::string("  読み込み中の削除: ") +
					  (deleted ? "できた（mac はこれが普通）" : ("できない — " + whyNot)));
			if (deleted)
			{
				// 消してしまったので、以降の後始末のために戻しておく。
				std::string ignored;
				(void)vwprobe::CopyFileTo(source, copy, ignored);
			}
		}

		// ⑥ 降ろす。**ペイロードのコードがスタックに無い**ここで行う。
		shutdownFn();
		abiFn = nullptr;
		initFn = nullptr;
		describeFn = nullptr;
		runFn = nullptr;
		shutdownFn = nullptr;

		const std::chrono::steady_clock::time_point closing = std::chrono::steady_clock::now();
		if (!payload.close(error))
		{
			probe.fail("降ろせなかった: " + error);
			return false;
		}
		probe.log("  降ろせた: " + std::to_string(MillisSince(closing)) + "ms");

		// ⑦ 本当にプロセスから消えたか。**dlclose が 0 を返しても消えているとは限らない**
		//    （参照が残っていれば残る）ので、別の目で見る。
		const bool stillThere = vwprobe::IsModuleStillLoaded(copy);
		probe.log(std::string("  アンロードの確認: ") +
				  (stillThere ? "**まだ残っている**（参照が残っている）" : "消えた"));
		if (stillThere)
			probe.fail("降ろしたのにモジュールがプロセスに残っている");

		// ⑧ 後始末（降ろした後なら消せるはず）。
		std::string removeError;
		const bool removed = vwprobe::RemoveFileAt(copy, removeError);
		probe.log(std::string("  降ろした後の削除: ") +
				  (removed ? "できた" : ("できない — " + removeError)));
		if (!removed)
			probe.fail("降ろした後もファイルを置き換えられない（入れ替えが成り立たない）");

		return true;
	}
} // namespace

VW_PROBE("hot-reload", "外部モジュールを読み・使い・降ろし・入れ替える",
		 "プラグイン本体の外部化とホットリロードが成り立つかの検証（図面は読むだけ）")
{
	probe.log("殻（Vectorworks が読み込んだモジュール）: " + vwprobe::OwnModulePath());
	// 値と、変数そのものの番地の両方。**比べるのは後者**——値が同じなのは当たり前で
	// （VW 側の同じ実装を指すポインタなので）、複製かどうかは番地でしか分からない。
	probe.log("殻の gSDK の値=" + Hex(gSDK) + " gCBP の値=" + Hex((void*)gCBP));
	probe.log("殻の &gSDK=" + Hex((const void*)&gSDK) + " &gCBP=" + Hex((const void*)&gCBP));
	probe.log("殻から見た undo: building=" +
			  std::string(gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
	probe.log("");

	if (gCBP == nullptr)
	{
		probe.fail("gCBP が空（殻の GS_InitializeVCOM が済んでいない？）。ここで打ち切る");
		return;
	}

	std::string firstDescription;
	std::string secondDescription;

	const bool first =
		RunGeneration(probe, "A", "1", /*tryDeleteWhileLoaded=*/true, firstDescription);
	probe.log("");
	const bool second =
		RunGeneration(probe, "B", "2", /*tryDeleteWhileLoaded=*/false, secondDescription);
	probe.log("");

	// 判定。**2 つの素性が違う**ことが「入れ替わった」の実測。
	if (!first || !second)
	{
		probe.log("結論: 2 世代を通せなかった（上の失敗を参照）");
		return;
	}
	probe.log("世代 1 の素性: " + firstDescription);
	probe.log("世代 2 の素性: " + secondDescription);
	if (firstDescription == secondDescription)
	{
		probe.fail("2 つの世代で素性が同じ。**入れ替わったと言えない**"
				   "（同じ変種を 2 回読んだ？ ビルドの同梱物を確かめること）");
		return;
	}
	probe.log("結論: Vectorworks を再起動せずに、別の中身のモジュールへ入れ替わった。");
}
