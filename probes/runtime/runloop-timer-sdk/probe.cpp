//
//	probes/runtime/runloop-timer-sdk/probe.cpp
//
//	[issue #204] **OS のタイマーから gSDK を呼べるか**を実測する。
//
//	背景: 周期的にプラグインのコードを動かす手立ては、いまのところウェブパレットの HTML の
//	`setInterval` しかない。ところがパレットを隠す／Vectorworks が裏に回ると、埋め込みの
//	Chromium が隠れたページのタイマーを 60 秒に 1 回まで間引く。そこで「パレットの外へ
//	時計を移す」が成り立つかを確かめる。ISDK にアイドル／タイマーの口が無いことは
//	ヘッダで確認済み（[Findings「周期実行と通知」](../../../Findings/Timers%20and%20Notifications.md)）。
//
//	**1 回目の計測（ビルド 1d5b70f53506 / 782dd705b13f）で分かったこと**——
//	`CFRunLoopTimer` を**メインのランループの `kCFRunLoopCommonModes` へ**入れると、
//	刻みが来るのは**VW が入れ子のモーダルループを回している間だけ**だった
//	（観測した刻みの `mode=` は全て `NSModalPanelRunLoopMode`）。**メニューコマンドが
//	戻った後は 1 回も来ない**（`outside` 局面の刻みが 0 件）。つまり VW のアイドルの
//	ループは、共通モードの CFRunLoop を回していない。
//
//	そこでこの版は「**できない**」と結論する前に、取りに行ける答えを 3 つ取る:
//
//	  ① **モードを総当たりする。** `CFRunLoopCopyAllModes` で主ランループが知っている
//	     モードを全部並べ、**その全てに**タイマーを入れる（VW が私物のモードで回して
//	     いるなら、共通モードでは届かなくてもこちらで届く）。
//	  ② **別の口を並べて比べる。** `dispatch_source_t`（**メインキュー**のタイマー）を
//	     同時に仕掛ける。メインキューの消化はランループのモードに依らないので、
//	     VW がアイドルでもメインキューを回していれば**こちらだけ刻む**。
//	     刻みの行の `src=` で、どちらの口から来たかが分かる。
//	  ③ **走っている文脈を記録する。** 仕掛けた側がメインスレッドか
//	     （`pthread_main_np`）、刻みのときに VW が undo イベントを開いているか
//	     （`IsCurrentlyBuildingAnUndoEvent`。[#206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)
//	     からの依頼）。
//
//	仕掛け: 「コマンドが戻った後」は 1 回の実行では見えないので、**2 回走らせる**。
//	1 回目が仕掛けて刻みをファイルへ書き溜め、2 回目がそれを読んで報告し、ファイルを
//	消す（消えたことが古いタイマーへの「店じまい」の合図になる）。**書き溜め先は群ごとに
//	分ける**——同じ slug のプローブが複数の PR に居るので、1 本の道を共有すると
//	「どの群が仕掛けた刻みか」が混ざる（実際に混ざった）。
//
//	**本体（.vwpayload）は刻みが続く間ピン留めする。** 殻はプローブが終わると本体を
//	`dlclose` するので、ピン留めしないとタイマーの行き先が消えて落ちる。ピン留めに
//	失敗したら仕掛けない（落とすだけで何も分からないため）。
//

#include "Probe.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#	include <windows.h>
#else
#	include <CoreFoundation/CoreFoundation.h>
#	include <dispatch/dispatch.h>
#	include <dlfcn.h>
#	include <pthread.h>
#endif

// どの群（main / PR ごと）の本体かはビルドのときに決まる（plugin/CMakeLists.txt が
// -D で渡す）。構文チェック（ci-debug の compile）では渡らないので既定を置く。
#ifndef VW_PAYLOAD_GROUP
#	define VW_PAYLOAD_GROUP "local"
#endif

namespace
{
	// 刻みの間隔。パレットの JS タイマー（250 ms）と同じにしておく。
	const long long kVwProbeIntervalMs = 250;
	// 刻みの上限と締切。報告を忘れられても勝手に止まるように（口が 2 つあるので倍）。
	const int kVwProbeMaxTicks = 2400;
	const long long kVwProbeDeadlineMs = 300000;

	// 1 回目が書き溜めたものを 2 回目が読む。**一時ディレクトリ**に置く（Vectorworks が
	// 落ちても残るので、落ち方そのものも読み取れる）。**群ごとに別の名前**にする。
	std::string VwProbeTickFilePath()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		const std::string name =
			std::string("VwSdkProbes-runloop-timer-sdk-ticks-") + VW_PAYLOAD_GROUP + ".txt";
		return (dir / name).string();
	}

	// -----------------------------------------------------------------------
	// タイマーが持ち越す状態。**意図して leak する**——タイマーはプローブより長生きで、
	// 止まるのは「報告された」か「上限に達した」ときだけである。
	struct VwProbeTimerState
	{
		std::string filePath;
		std::chrono::steady_clock::time_point armedAt;
		int ticks = 0; // 合計（両方の口）
		int ticksCF = 0;
		int ticksDQ = 0;
		long long lastMsCF = 0;
		long long lastMsDQ = 0;
		bool insideProbe = true; // プローブ本体がまだスタックに居るか
		std::string phase = "pump";
		bool triedWrite = false;
		bool disarmed = false;
	};

	VwProbeTimerState* gVwProbeTimerState = nullptr;
#if defined(_WIN32)
	UINT_PTR gVwProbeWinTimer = 0;
#else
	CFRunLoopTimerRef gVwProbeCFTimer = nullptr;
	dispatch_source_t gVwProbeDQTimer = nullptr;
#endif

	// 1 行書いて閉じる。**毎回開き直す**ので、Vectorworks ごと落ちてもそこまでが残る。
	void VwProbeAppend(const std::string& path, const std::string& line)
	{
		std::ofstream out(path, std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	// 書き溜めの 1 行は `鍵=値` を ` ## ` で継ぐ（値に `##` は現れない）。
	std::string VwProbeField(const std::string& key, const std::string& value)
	{
		return " ## " + key + "=" + value;
	}

	std::string VwProbeNum(long long v)
	{
		return std::to_string(v);
	}

	long long VwProbeElapsedMs()
	{
		if (gVwProbeTimerState == nullptr)
			return 0;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now -
																	 gVwProbeTimerState->armedAt)
			.count();
	}

	// いまランループが回っているモード。**モーダルの最中かどうかがこれで分かる**
	// （macOS の alert は `NSModalPanelRunLoopMode` で回す）。主ランループが止まって
	// いれば nullptr が返る。
	std::string VwProbeCurrentMode()
	{
#if defined(_WIN32)
		// Windows の WM_TIMER にモードの概念は無い。入れ子のモーダルループに居るかは
		// 分からないので、代わりに「どのスレッドで来たか」を出す。
		return std::string("thread-") + std::to_string((unsigned long)::GetCurrentThreadId());
#else
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return "(not-running)";
		char buf[128] = {0};
		const Boolean ok = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8);
		::CFRelease(mode);
		return ok ? std::string(buf) : std::string("(unprintable)");
#endif
	}

	// アクティブレイヤの図形の数。書き込みが届いたかを読み戻すのに使う。
	long long VwProbeCountObjects()
	{
		if (gSDK == nil)
			return -1;
		MCObjectHandle layer = gSDK->GetActiveLayer();
		if (layer == nil)
			return -1;
		long long n = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(layer); h != nil; h = gSDK->NextObject(h))
		{
			++n;
			if (n >= 10000)
				break;
		}
		return n;
	}

	void VwProbeDisarm(const std::string& why)
	{
		VwProbeTimerState* st = gVwProbeTimerState;
#if defined(_WIN32)
		if (gVwProbeWinTimer != 0)
		{
			::KillTimer(nullptr, gVwProbeWinTimer);
			gVwProbeWinTimer = 0;
		}
#else
		if (gVwProbeCFTimer != nullptr)
		{
			::CFRunLoopTimerInvalidate(gVwProbeCFTimer);
			::CFRelease(gVwProbeCFTimer);
			gVwProbeCFTimer = nullptr;
		}
		if (gVwProbeDQTimer != nullptr)
		{
			::dispatch_source_cancel(gVwProbeDQTimer);
			::dispatch_release(gVwProbeDQTimer);
			gVwProbeDQTimer = nullptr;
		}
#endif
		if (st == nullptr || st->disarmed)
			return;
		st->disarmed = true;
		std::error_code ec;
		if (std::filesystem::exists(st->filePath, ec))
		{
			VwProbeAppend(st->filePath, "disarm" + VwProbeField("n", VwProbeNum(st->ticks)) +
											VwProbeField("cf", VwProbeNum(st->ticksCF)) +
											VwProbeField("dq", VwProbeNum(st->ticksDQ)) +
											VwProbeField("t_ms", VwProbeNum(VwProbeElapsedMs())) +
											VwProbeField("why", why));
		}
	}

	// -----------------------------------------------------------------------
	// 刻み 1 回。`src` は口の名前（`cf` ＝ CFRunLoopTimer / `dq` ＝ メインキューの
	// dispatch タイマー / `win` ＝ SetTimer）。**プローブが戻った後も呼ばれる**ので、
	// ここから殻（結果ダイアログ・ログの受け口）へは触らない——触れるのは自分の
	// ファイルと gSDK だけ。
	void VwProbeTickBody(const char* src)
	{
		VwProbeTimerState* st = gVwProbeTimerState;
		if (st == nullptr)
			return;

		// **書き溜め先が消えていたら店じまい。** 報告（2 回目の実行）はファイルを消す。
		// 2 回目は別の読み込み像で動くので、古い像のタイマーを直接止められない——
		// 「ファイルの有無」がその代わりの合図である。
		std::error_code ec;
		if (!std::filesystem::exists(st->filePath, ec))
		{
			VwProbeDisarm("書き溜め先が消えた（報告済み）");
			return;
		}

		++st->ticks;
		const bool isDQ = (std::string(src) == "dq");
		const long long ms = VwProbeElapsedMs();
		long long dt = 0;
		if (isDQ)
		{
			++st->ticksDQ;
			dt = ms - st->lastMsDQ;
			st->lastMsDQ = ms;
		}
		else
		{
			++st->ticksCF;
			dt = ms - st->lastMsCF;
			st->lastMsCF = ms;
		}

		const std::string head =
			"tick" + VwProbeField("src", src) + VwProbeField("n", VwProbeNum(st->ticks)) +
			VwProbeField("t_ms", VwProbeNum(ms)) + VwProbeField("dt_ms", VwProbeNum(dt)) +
			VwProbeField("phase", st->phase) +
			VwProbeField("inside_probe", st->insideProbe ? "yes" : "no") +
			VwProbeField("mode", VwProbeCurrentMode());

		// **gSDK を呼ぶ前に「これから呼ぶ」を残す。** 落ちたらこの行が最後に通った場所に
		// なる（落ち方そのものが知見）。
		VwProbeAppend(st->filePath, head + VwProbeField("sdk", "about-to-call"));

		long long tickCount = -1;
		long long docs = -1;
		long long objs = -1;
		std::string undoBuilding = "?";
		std::string trouble;
		try
		{
			if (gSDK == nil)
			{
				trouble = "gSDK が nil";
			}
			else
			{
				tickCount = (long long)gSDK->TickCount();
				// **#206 からの依頼**: 刻みが当たった瞬間に VW が undo イベントを
				// 開いているか。開いた瞬間を知らせる通知は無い（閉じる直前の
				// `kNotifyUndoEndEvent` だけ）ので、ここで直に問うしかない。
				undoBuilding = gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no";
				VectorWorks::TVWArray_OpenFileInformation files;
				gSDK->GetOpenFilesList(files);
				docs = (long long)files.GetSize();
				objs = VwProbeCountObjects();
			}
		}
		catch (const std::exception& e)
		{
			trouble = std::string("例外: ") + e.what();
		}
		catch (...)
		{
			trouble = "不明な例外";
		}

		VwProbeAppend(st->filePath, head + VwProbeField("undo_building", undoBuilding) +
										VwProbeField("tick_count", VwProbeNum(tickCount)) +
										VwProbeField("docs", VwProbeNum(docs)) +
										VwProbeField("objs", VwProbeNum(objs)) +
										VwProbeField("trouble", trouble.empty() ? "-" : trouble));

		// **プローブの外から 1 度だけ書き込みを試す。** 読めるだけでは足りない——
		// 受け付けの時計をここへ移すなら、図面を触れなければ意味が無い。
		if (!st->triedWrite && !st->insideProbe)
		{
			st->triedWrite = true;
			VwProbeAppend(st->filePath, "write" + VwProbeField("src", src) +
											VwProbeField("n", VwProbeNum(st->ticks)) +
											VwProbeField("step", "about-to-create-locus"));
			std::string outcome = "作れた";
			MCObjectHandle locus = nil;
			try
			{
				if (gSDK != nil)
					locus = gSDK->CreateLocus(WorldPt(1000, 2000));
				if (locus == nil)
					outcome = "nil が返った";
			}
			catch (...)
			{
				outcome = "例外で止まった";
			}
			VwProbeAppend(st->filePath,
						  "write" + VwProbeField("src", src) +
							  VwProbeField("n", VwProbeNum(st->ticks)) +
							  VwProbeField("locus", outcome) +
							  VwProbeField("objs_after", VwProbeNum(VwProbeCountObjects())));
		}

		if (st->ticks >= kVwProbeMaxTicks)
			VwProbeDisarm("刻みの上限に達した");
		else if (ms >= kVwProbeDeadlineMs)
			VwProbeDisarm("締切に達した");
	}

#if defined(_WIN32)
	void CALLBACK VwProbeWinTimerProc(HWND, UINT, UINT_PTR, DWORD)
	{
		VwProbeTickBody("win");
	}
#else
	void VwProbeCFTimerProc(CFRunLoopTimerRef, void*)
	{
		VwProbeTickBody("cf");
	}

	void VwProbeDQTimerProc(void*)
	{
		VwProbeTickBody("dq");
	}
#endif

	// -----------------------------------------------------------------------
	// **本体をピン留めする。** 殻はプローブが終わると本体を降ろすので、ピン留めしないと
	// タイマーの行き先が消える。失敗したら仕掛けてはいけない。成功したら、留めた
	// ファイルの道を返す（どの群の本体が刻んでいるかが結果から分かるように）。
	std::string VwProbePinSelf(std::string& outPath)
	{
#if defined(_WIN32)
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&VwProbePinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した";
		outPath = "(windows)";
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&VwProbePinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		// RTLD_NOLOAD は「すでに読み込まれているときだけ」ハンドルを返す。返ったハンドルは
		// 参照を 1 つ持つ——**閉じない**のがここの目的（殻の dlclose では降りなくなる）。
		void* handle = ::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
		if (handle == nullptr)
			return std::string("dlopen(RTLD_NOLOAD) が失敗した: ") + info.dli_fname;
		outPath = info.dli_fname;
		return std::string();
#endif
	}

	// ランループを自分で回して、刻みが来るのを待つ。
	void VwProbePump(double seconds)
	{
#if defined(_WIN32)
		const DWORD until = ::GetTickCount() + (DWORD)(seconds * 1000.0);
		while (::GetTickCount() < until)
		{
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0)
			{
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}
			::Sleep(10);
		}
#else
		::CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
#endif
	}

	// -----------------------------------------------------------------------
	// 2 回目の実行（報告）。書き溜めを読んで畳み、**読み終えたら消す**。
	void VwProbeReport(vwprobe::Report& probe, const std::string& path)
	{
		std::vector<std::string> lines;
		{
			std::ifstream in(path);
			std::string line;
			while (std::getline(in, line))
				lines.push_back(line);
		}
		probe.log("書き溜め: " + path + "（" + std::to_string(lines.size()) + " 行）");
		probe.log("");

		// 値を 1 つ切り出す（`鍵=値` を ` ##` で区切った形）。
		const auto field = [](const std::string& line, const std::string& key)
		{
			const std::string needle = key + "=";
			const size_t at = line.find(needle);
			if (at == std::string::npos)
				return std::string();
			const size_t from = at + needle.size();
			const size_t end = line.find(" ##", from);
			return line.substr(from, (end == std::string::npos) ? end : end - from);
		};

		// 「局面 × 口」ごとに刻みの数と間隔を畳む。間引かれていれば dt_ms が伸びる
		// （JS タイマーは隠れると 60000 ms になった）。
		struct VwProbeSliceStat
		{
			std::string key;
			int count = 0;
			long long minDt = 0;
			long long maxDt = 0;
			long long sumDt = 0;
			int undoYes = 0;
			std::string modes;
		};
		std::vector<VwProbeSliceStat> stats;
		int ticksWithSdk = 0;
		int ticksWithTrouble = 0;
		for (const std::string& line : lines)
		{
			if (line.compare(0, 5, "tick ") != 0)
				continue;
			if (line.find("sdk=about-to-call") != std::string::npos)
				continue; // 前触れの行は数えない（本行と二重になる）
			const std::string key = field(line, "phase") + " / " + field(line, "src");
			const long long dt = std::atoll(field(line, "dt_ms").c_str());
			if (field(line, "trouble") != "-")
				++ticksWithTrouble;
			if (field(line, "tick_count") != "-1")
				++ticksWithSdk;

			VwProbeSliceStat* found = nullptr;
			for (VwProbeSliceStat& s : stats)
				if (s.key == key)
					found = &s;
			if (found == nullptr)
			{
				stats.push_back(VwProbeSliceStat{key, 0, dt, dt, 0, 0, std::string()});
				found = &stats.back();
			}
			++found->count;
			found->minDt = std::min(found->minDt, dt);
			found->maxDt = std::max(found->maxDt, dt);
			found->sumDt += dt;
			if (field(line, "undo_building") == "yes")
				++found->undoYes;
			const std::string mode = field(line, "mode");
			if (found->modes.find(mode) == std::string::npos)
				found->modes += (found->modes.empty() ? "" : ",") + mode;
		}

		probe.log(
			"局面 / 口ごとの刻み（dt_ms = 同じ口の前の刻みからの間隔。仕掛けた間隔は 250 ms）:");
		for (const VwProbeSliceStat& s : stats)
		{
			const long long avg = (s.count > 0) ? (s.sumDt / s.count) : 0;
			probe.log("  " + s.key + ": " + std::to_string(s.count) + " 回, dt 最小 " +
					  std::to_string(s.minDt) + " / 平均 " + std::to_string(avg) + " / 最大 " +
					  std::to_string(s.maxDt) + " ms, undo 中 " + std::to_string(s.undoYes) +
					  " 回, mode=" + s.modes);
		}
		probe.log("gSDK から値が読めた刻み: " + std::to_string(ticksWithSdk) +
				  " 回 / 異常のあった刻み: " + std::to_string(ticksWithTrouble) + " 回");
		probe.log("");

		// 要点の行（仕掛け・モードの一覧・書き込み・店じまい）は全部出す。刻みの行は
		// 多すぎるので、**最後の 16 行**だけ出す（間引かれたかは上の表で足りる）。
		probe.log("--- 要点の行 ---");
		for (const std::string& line : lines)
		{
			if (line.compare(0, 4, "arm ") == 0 || line.compare(0, 6, "modes ") == 0 ||
				line.compare(0, 6, "write ") == 0 || line.compare(0, 7, "disarm ") == 0 ||
				line.compare(0, 6, "phase ") == 0)
				probe.log(line);
		}
		probe.log("");
		probe.log("--- 刻みの行（最後の 16 行） ---");
		const size_t from = (lines.size() > 16) ? (lines.size() - 16) : 0;
		for (size_t i = from; i < lines.size(); ++i)
			probe.log(lines[i]);

		std::error_code ec;
		std::filesystem::remove(path, ec);
		probe.log("");
		probe.log("書き溜めを消した（まだ刻んでいる古いタイマーは、次の刻みで自分から止まる）。");
	}
} // namespace

VW_PROBE("runloop-timer-sdk", "OS のタイマーから gSDK を呼ぶ",
		 "ランループタイマーとメインキューのタイマーを仕掛け、どの場面で刻むかを測る")
{
	const std::string path = VwProbeTickFilePath();
	std::error_code ec;
	const bool haveTicks = std::filesystem::exists(path, ec);

	probe.log("書き溜め先: " + path);
	if (haveTicks)
	{
		probe.log("→ **2 回目の実行**と見なして、前回の書き溜めを報告する。");
		probe.log("");
		VwProbeReport(probe, path);
		return;
	}

	// ---------------------------------------------------------------- 仕掛ける
	std::string pinnedPath;
	const std::string pinTrouble = VwProbePinSelf(pinnedPath);
	if (!pinTrouble.empty())
	{
		probe.fail("本体をピン留めできなかったので仕掛けない（" + pinTrouble + "）");
		return;
	}
	probe.log("本体をピン留めした（殻が降ろしても、タイマーの行き先は残る）。");

	gVwProbeTimerState = new VwProbeTimerState();
	gVwProbeTimerState->filePath = path;
	gVwProbeTimerState->armedAt = std::chrono::steady_clock::now();
	gVwProbeTimerState->phase = "pump";

	// **走っている文脈を残す。** メニューコマンドがメインスレッドで走っているかは、
	// 「刻みがプラグインの処理と並走しうるか」に直結する。
#if defined(_WIN32)
	const std::string threadNote =
		std::string("win-thread-") + std::to_string((unsigned long)::GetCurrentThreadId());
	const std::string loopNote = "(windows)";
#else
	const std::string threadNote = (::pthread_main_np() != 0) ? "main" : "worker";
	const std::string loopNote =
		(::CFRunLoopGetCurrent() == ::CFRunLoopGetMain()) ? "same-as-main" : "not-main";
#endif
	probe.log("走っているスレッド: " + threadNote + " / ランループ: " + loopNote);

	VwProbeAppend(path, "arm" + VwProbeField("group", VW_PAYLOAD_GROUP) +
							VwProbeField("interval_ms", VwProbeNum(kVwProbeIntervalMs)) +
							VwProbeField("max_ticks", VwProbeNum(kVwProbeMaxTicks)) +
							VwProbeField("thread", threadNote) + VwProbeField("runloop", loopNote) +
							VwProbeField("mode_at_arm", VwProbeCurrentMode()) +
							VwProbeField("pinned", pinnedPath) +
							VwProbeField("objs_at_arm", VwProbeNum(VwProbeCountObjects())));

#if defined(_WIN32)
	// ウィンドウを持たないタイマー。WM_TIMER はスレッドのキューへ入り、Vectorworks の
	// メッセージポンプが配る（＝配り手は VW 本体）。
	gVwProbeWinTimer = ::SetTimer(nullptr, 0, (UINT)kVwProbeIntervalMs, &VwProbeWinTimerProc);
	const bool armed = (gVwProbeWinTimer != 0);
	probe.log(armed ? "SetTimer で仕掛けた（250 ms）" : "SetTimer が 0 を返した");
#else
	const CFAbsoluteTime interval = (CFAbsoluteTime)kVwProbeIntervalMs / 1000.0;
	gVwProbeCFTimer =
		::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + interval,
							   interval, 0, 0, &VwProbeCFTimerProc, nullptr);
	bool armed = (gVwProbeCFTimer != nullptr);
	if (armed)
	{
		// **共通モードだけでは足りなかった**（前回の計測: 刻みはモーダルループの最中に
		// しか来ず、コマンドが戻った後は 1 回も来ない）。そこで**主ランループが知っている
		// モードを全部並べて、その全てに入れる**——VW が私物のモードで回しているなら、
		// 共通モードでは届かなくてもこちらで届く。
		::CFRunLoopAddTimer(::CFRunLoopGetMain(), gVwProbeCFTimer, kCFRunLoopCommonModes);
		std::string modeList;
		CFArrayRef modes = ::CFRunLoopCopyAllModes(::CFRunLoopGetMain());
		if (modes != nullptr)
		{
			const CFIndex count = ::CFArrayGetCount(modes);
			for (CFIndex i = 0; i < count; ++i)
			{
				CFStringRef mode = (CFStringRef)::CFArrayGetValueAtIndex(modes, i);
				if (mode == nullptr)
					continue;
				::CFRunLoopAddTimer(::CFRunLoopGetMain(), gVwProbeCFTimer, mode);
				char buf[128] = {0};
				if (::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8))
					modeList += (modeList.empty() ? "" : ",") + std::string(buf);
			}
			::CFRelease(modes);
		}
		VwProbeAppend(path, "modes" + VwProbeField("all", modeList.empty() ? "(none)" : modeList));
		probe.log(
			"CFRunLoopTimer を仕掛けた（250 ms, 主ランループ / CommonModes ＋ 次の全モード）");
		probe.log("  主ランループが知っているモード: " + (modeList.empty() ? "(none)" : modeList));
	}
	else
	{
		probe.log("CFRunLoopTimerCreate が nullptr を返した");
	}

	// **別の口を並べる。** メインキューの消化はランループのモードに依らないので、
	// VW がアイドルでもメインキューを回していれば、こちらだけ刻む。
	gVwProbeDQTimer =
		::dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, ::dispatch_get_main_queue());
	if (gVwProbeDQTimer != nullptr)
	{
		const int64_t ns = (int64_t)kVwProbeIntervalMs * 1000000;
		::dispatch_source_set_timer(gVwProbeDQTimer, ::dispatch_time(DISPATCH_TIME_NOW, ns),
									(uint64_t)ns, (uint64_t)(50 * 1000000));
		::dispatch_source_set_event_handler_f(gVwProbeDQTimer, &VwProbeDQTimerProc);
		::dispatch_resume(gVwProbeDQTimer);
		armed = true;
		probe.log("dispatch のタイマーも仕掛けた（250 ms, メインキュー）");
	}
	else
	{
		probe.log("dispatch_source_create が nullptr を返した");
	}
#endif
	if (!armed)
	{
		probe.fail("タイマーを 1 つも仕掛けられなかった");
		return;
	}

	// ---------------------------------------------------- ① 自分で回して数える
	const int before1 = gVwProbeTimerState->ticks;
	probe.log("");
	probe.log("① ランループを 1.5 秒ぶん自分で回す（期待: 250 ms で 6 回前後 × 口の数）");
	VwProbePump(1.5);
	probe.log("  刻んだ回数: " + std::to_string(gVwProbeTimerState->ticks - before1) + "（CF " +
			  std::to_string(gVwProbeTimerState->ticksCF) + " / dispatch " +
			  std::to_string(gVwProbeTimerState->ticksDQ) + "）");

	// ------------------- ①b 同期処理の最中（ランループを回さない間）に割り込まれるか
	// **ここが「取り消しの記録中・描画の最中に呼ばれるか」の答えになる。** ランループ
	// タイマーはランループが回るときにしか配られないので、こちらが回さずに回し続ける
	// 処理（＝ふつうのプラグインの処理）の最中には割り込めないはずである。
	gVwProbeTimerState->phase = "busy-no-runloop";
	VwProbeAppend(path, std::string("phase") + VwProbeField("name", "busy-no-runloop"));
	const int beforeBusy = gVwProbeTimerState->ticks;
	{
		const std::chrono::steady_clock::time_point until =
			std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (std::chrono::steady_clock::now() < until)
			(void)gSDK->TickCount(); // 回さずに 3 秒ぶん働く
	}
	probe.log("①b ランループを回さず 3 秒ぶん働いた間の刻み: " +
			  std::to_string(gVwProbeTimerState->ticks - beforeBusy) + " 回");
	probe.log("  （0 なら、こちらの同期処理に割り込まれることは無い）");

	// -------------------- ①c 進捗ダイアログの `DoYield` の最中に刻むか
	// `DoYield` は VW の再描画とイベント処理へ戻る（Findings「進捗・診断」）。
	// **VW が自分でイベントを回す場面で割り込まれるか**がここで分かる。
	gVwProbeTimerState->phase = "do-yield";
	VwProbeAppend(path, std::string("phase") + VwProbeField("name", "do-yield"));
	const int beforeYield = gVwProbeTimerState->ticks;
	{
		VWFC::Tools::CProgressDlg progress;
		// 遅延なしで開く（OpenDelayed では 3 秒のあいだ出ないので、測る前に閉じてしまう）。
		progress.Open("①c DoYield の最中に刻むかを測っています", false);
		progress.Start(100, 1000);
		const std::chrono::steady_clock::time_point until =
			std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (std::chrono::steady_clock::now() < until)
			progress.DoYield(1, true);
		progress.End();
		progress.Close();
	}
	probe.log("①c 進捗ダイアログの DoYield を 3 秒ぶん回した間の刻み: " +
			  std::to_string(gVwProbeTimerState->ticks - beforeYield) + " 回");

	// ------------------------------------------------- ② モーダルの最中に刻むか
	gVwProbeTimerState->phase = "modal-wait";
	const int before2 = gVwProbeTimerState->ticks;
	VwProbeAppend(path, std::string("phase") + VwProbeField("name", "modal-wait"));
	gSDK->AlertInform("② モーダルの最中にタイマーが刻むかを測ります。",
					  "このまま 10 秒ほど待ってから OK を押してください。");
	probe.log("② モーダル（alert）が開いている間の刻み: " +
			  std::to_string(gVwProbeTimerState->ticks - before2) + " 回");

	// ------------------------------------------ ③ コマンドが戻った後も刻むか
	probe.log("");
	probe.log("③ ここから先は、このコマンドが戻った後の刻みを書き溜める。");
	probe.log("   **もう一度このプローブを走らせると、その結果が出る。**");
	probe.log("   走らせるまでの 60 秒ほどの間に、できれば次をしてみてください:");
	probe.log("     ・何もせず放っておく（これが本命——アイドルで刻むかどうか）");
	probe.log("     ・ほかのアプリへ切り替えて戻る（間引かれないかどうか）");
	probe.log("     ・図形を 1 つ選んでツールでドラッグする（VW が undo を開いている最中）");
	probe.log("   戻った後の最初の刻みで**図面に 2D 基準点を 1 つ作る**ところまで試します。");
	probe.log("   タイマーは 300 秒で自分から止まります。");
	VwProbeAppend(path, std::string("phase") + VwProbeField("name", "outside"));
	gVwProbeTimerState->phase = "outside";
	gVwProbeTimerState->insideProbe = false;
}
