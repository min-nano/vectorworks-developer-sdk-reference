//
//	probes/runtime/runloop-timer-sdk/probe.cpp
//
//	[issue #204] **OS のランループタイマーから gSDK を呼べるか**を実測する。
//
//	背景: 周期的にプラグインのコードを動かす手立ては、いまのところウェブパレットの HTML の
//	`setInterval` しかない。ところがパレットを隠す／Vectorworks が裏に回ると、埋め込みの
//	Chromium が隠れたページのタイマーを 60 秒に 1 回まで間引く。そこで「パレットの外へ
//	時計を移す」——macOS なら `CFRunLoopTimer` をメインのランループへ——が成り立つかを
//	確かめる。ISDK にアイドル／タイマーの口が無いことはヘッダで確認済み（Findings）。
//
//	確かめること:
//	  ① メニューコマンドの中からランループタイマーを仕掛けられるか（自分でランループを
//	     回して刻みを数える）。
//	  ② **モーダルダイアログが開いている間も刻むか**（刻みごとに現在のランループモードを
//	     記録するので、`NSModalPanelRunLoopMode` で来たかがそのまま分かる）。
//	  ③ Vectorworks が**ほかのアプリの裏に回っている間**も 250 ms で刻むか（間引かれないか）。
//	  ④ **メニューコマンドが戻った後**（こちらのコードがスタックに 1 本も無い状態）でも
//	     刻み続け、そこから `gSDK` を読める・書けるか。
//	  ⑤ **本体のピン留めが効くか**（mac は `dlopen(RTLD_NOLOAD)`、Windows は
//	     `GetModuleHandleExW` の `GET_MODULE_HANDLE_EX_FLAG_PIN`）。留めた先の名前を
//	     ログに出すので、**殻ではなく本体を留めたか**まで確かめられる。
//
//	**Windows の実機確認は issue #205**（#204 から切り出したもの）。プローブは最初から
//	両プラットフォーム向けに書いてあるが、**Windows の道は実機でも CI でも一度も通って
//	いなかった**（この調査まで、このプローブを載せた PR が無かったため）。
//
//	仕掛け: ④ は 1 回の実行では見えないので、**2 回走らせる**。1 回目が仕掛けて刻みを
//	ファイルへ書き溜め、2 回目がそれを読んで報告し、ファイルを消す（消えたことが古い
//	タイマーへの「店じまい」の合図になる。下記 VwProbeTickBody）。
//
//	**本体（.vwpayload）は刻みが続く間ピン留めする。** 殻はプローブが終わると本体を
//	`dlclose` するので、ピン留めしないとタイマーの行き先（この関数）が消えて落ちる。
//	ピン留めに失敗したら仕掛けない（落とすだけで何も分からないため）。
//

#include "Probe.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
//	**NOMINMAX を先に立てる。** windows.h（windef.h）は `min` / `max` を**マクロ**で
//	定義するので、下の VwProbeReport の `std::min` / `std::max` がマクロに食われる。
//	SDK 側にも `#undef min` / `#undef max` はあるが（`Include/KernelBaseTypes.h` /
//	`Include/Kernel/Math/MathBasic.h`。どちらも `#if _WINDOWS` の中）、**それは
//	"Probe.h" を読んでいる最中の話**で、その後に windows.h を読むこちらには届かない。
//	念のため呼ぶ側も `(std::min)(…)` と括って、マクロが生きていても展開されないようにした。
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#else
#	include <CoreFoundation/CoreFoundation.h>
#	include <dlfcn.h>
#endif

namespace
{
	// 刻みの間隔。パレットの JS タイマー（250 ms）と同じにしておく。
	const long long kVwProbeIntervalMs = 250;
	// 刻みの上限（250 ms × 1200 = 300 秒）。報告を忘れられても勝手に止まるように。
	const int kVwProbeMaxTicks = 1200;
	// 1 回目が書き溜めたものを 2 回目が読む。**一時ディレクトリ**に置く（Vectorworks が
	// 落ちても残るので、落ち方そのものも読み取れる）。
	std::string VwProbeTickFilePath()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return (dir / "VwSdkProbes-runloop-timer-sdk-ticks.txt").string();
	}

	// -----------------------------------------------------------------------
	// タイマーが持ち越す状態。**意図して leak する**——タイマーはプローブより長生きで、
	// 止まるのは「報告された」か「上限に達した」ときだけである。
	struct VwProbeTimerState
	{
		std::string filePath;
		std::chrono::steady_clock::time_point armedAt;
		int ticks = 0;
		long long lastMs = 0;
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
#endif

	// 1 行書いて閉じる。**毎回開き直す**ので、Vectorworks ごと落ちてもそこまでが残る。
	void VwProbeAppend(const std::string& path, const std::string& line)
	{
		std::ofstream out(path, std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	// 書き溜めの 1 行は `鍵 ## 値` で組む（値に `##` は現れない）。
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

#if defined(_WIN32)
	// ワイド文字列を UTF-8 へ。ログへ出すだけなので、読めなければ空で構わない。
	std::string VwProbeNarrow(const wchar_t* w)
	{
		if (w == nullptr || w[0] == L'\0')
			return std::string();
		// 入力長に -1 を渡すと、`need` にも書き出しにも**終端の NUL が含まれる**。
		// だから器は `need` で取る——`need - 1` にすると
		// ERROR_INSUFFICIENT_BUFFER で 1 文字も書かれず、**空文字列が黙って返る**。
		const int need = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		if (need <= 1)
			return std::string();
		std::string out((size_t)need, '\0');
		const int wrote = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], need, nullptr, nullptr);
		if (wrote <= 1)
			return std::string();
		out.resize((size_t)(wrote - 1)); // 終端の NUL を落とす
		return out;
	}
#endif

	// いまランループが回っているモード。**モーダルの最中かどうかがこれで分かる**
	// （macOS の alert は `NSModalPanelRunLoopMode` で回す）。
	std::string VwProbeCurrentMode()
	{
#if defined(_WIN32)
		// Windows の WM_TIMER に「ランループモード」の概念は無いので、mac の
		// `NSModalPanelRunLoopMode` に当たるものを USER32 から組み立てる。
		//
		// **`GUITHREADINFO` にモーダルを表す旗は無い**（`flags` にあるのは
		// `GUI_CARETBLINKING` / `GUI_INMOVESIZE` / `GUI_INMENUMODE` /
		// `GUI_SYSTEMMENUMODE` / `GUI_POPUPMENUMODE` / `GUI_16BITTASK` だけ）。
		// そこで②（モーダルの最中に届くか）は次の 2 つで見る:
		//   ・`+ownerdisabled` ——**モーダルダイアログは持ち主の窓を無効にする**。
		//     これが Win32 でのモーダルの定義そのものなので、一番当てになる。
		//   ・`+cls:<クラス名>` ——アクティブな窓のクラス名。Win32 の標準のダイアログは
		//     `#32770` だが、**VW が自前のクラスで出している見込みもある**ので、
		//     決め打ちせず名前をそのまま残す（初回の実機ログで何なのかが分かる）。
		std::string out = "thread-" + std::to_string((unsigned long)::GetCurrentThreadId());
		GUITHREADINFO gti;
		::ZeroMemory(&gti, sizeof(gti));
		gti.cbSize = sizeof(gti);
		if (::GetGUIThreadInfo(::GetCurrentThreadId(), &gti) == 0)
			return out + "+gti-failed";
		if ((gti.flags & GUI_INMENUMODE) != 0)
			out += "+menu";
		if ((gti.flags & GUI_POPUPMENUMODE) != 0)
			out += "+popupmenu";
		if ((gti.flags & GUI_SYSTEMMENUMODE) != 0)
			out += "+sysmenu";
		if ((gti.flags & GUI_INMOVESIZE) != 0)
			out += "+movesize";
		if (gti.hwndActive == nullptr)
			return out + "+noactive";
		wchar_t cls[64] = {0};
		if (::GetClassNameW(gti.hwndActive, cls, 64) > 0)
			out += "+cls:" + VwProbeNarrow(cls);
		const HWND owner = ::GetWindow(gti.hwndActive, GW_OWNER);
		if (owner != nullptr && ::IsWindowEnabled(owner) == 0)
			out += "+ownerdisabled";
		return out;
#else
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return "(none)";
		char buf[128] = {0};
		const Boolean ok = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8);
		::CFRelease(mode);
		return ok ? std::string(buf) : std::string("(unprintable)");
#endif
	}

	// **Vectorworks がいま前面に居るか。** ③（裏に回っている間も刻むか）を
	// 読み違えないための対照である——利用者が切り替えを忘れたまま OK を押しても、
	// 刻みが全部 `fg=yes` なら「裏に回っていなかった＝③は測れていない」と機械で分かる。
	// これが無いと「間引かれなかった」と「切り替えなかった」が見分けられない。
	std::string VwProbeForeground()
	{
#if defined(_WIN32)
		const HWND fg = ::GetForegroundWindow();
		if (fg == nullptr)
			return "none";
		DWORD pid = 0;
		::GetWindowThreadProcessId(fg, &pid);
		return (pid == ::GetCurrentProcessId()) ? "yes" : "no";
#else
		// mac 側は未計装（#204 の範囲なので、こちらでは足さない）。
		return "?";
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
#endif
		if (st == nullptr || st->disarmed)
			return;
		st->disarmed = true;
		std::error_code ec;
		if (std::filesystem::exists(st->filePath, ec))
		{
			VwProbeAppend(st->filePath, "disarm" + VwProbeField("n", VwProbeNum(st->ticks)) +
											VwProbeField("t_ms", VwProbeNum(VwProbeElapsedMs())) +
											VwProbeField("why", why));
		}
	}

	// -----------------------------------------------------------------------
	// 刻み 1 回。**プローブが戻った後も呼ばれる**ので、ここから殻（結果ダイアログ・ログの
	// 受け口）へは触らない——触れるのは自分のファイルと gSDK だけ。
	void VwProbeTickBody()
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
		const long long ms = VwProbeElapsedMs();
		const long long dt = ms - st->lastMs;
		st->lastMs = ms;

		const std::string head =
			"tick" + VwProbeField("n", VwProbeNum(st->ticks)) +
			VwProbeField("t_ms", VwProbeNum(ms)) + VwProbeField("dt_ms", VwProbeNum(dt)) +
			VwProbeField("phase", st->phase) +
			VwProbeField("inside_probe", st->insideProbe ? "yes" : "no") +
			VwProbeField("mode", VwProbeCurrentMode()) + VwProbeField("fg", VwProbeForeground());

		// **gSDK を呼ぶ前に「これから呼ぶ」を残す。** 落ちたらこの行が最後に通った場所に
		// なる（落ち方そのものが知見）。
		VwProbeAppend(st->filePath, head + VwProbeField("sdk", "about-to-call"));

		long long tickCount = -1;
		long long docs = -1;
		long long objs = -1;
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

		VwProbeAppend(st->filePath, head + VwProbeField("tick_count", VwProbeNum(tickCount)) +
										VwProbeField("docs", VwProbeNum(docs)) +
										VwProbeField("objs", VwProbeNum(objs)) +
										VwProbeField("trouble", trouble.empty() ? "-" : trouble));

		// **プローブの外から 1 度だけ書き込みを試す。** 読めるだけでは足りない——
		// 受け付けの時計をここへ移すなら、図面を触れなければ意味が無い。
		if (!st->triedWrite && !st->insideProbe && st->ticks >= 8)
		{
			st->triedWrite = true;
			VwProbeAppend(st->filePath, "write" + VwProbeField("n", VwProbeNum(st->ticks)) +
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
						  "write" + VwProbeField("n", VwProbeNum(st->ticks)) +
							  VwProbeField("locus", outcome) +
							  VwProbeField("objs_after", VwProbeNum(VwProbeCountObjects())));
		}

		if (st->ticks >= kVwProbeMaxTicks)
			VwProbeDisarm("刻みの上限に達した");
	}

#if defined(_WIN32)
	void CALLBACK VwProbeWinTimerProc(HWND, UINT, UINT_PTR, DWORD)
	{
		VwProbeTickBody();
	}
#else
	void VwProbeCFTimerProc(CFRunLoopTimerRef, void*)
	{
		VwProbeTickBody();
	}
#endif

	// -----------------------------------------------------------------------
	// **本体をピン留めする。** 殻はプローブが終わると本体を降ろすので、ピン留めしないと
	// タイマーの行き先が消える。失敗したら仕掛けてはいけない。
	std::string VwProbePinSelf(std::string& pinnedPath)
	{
#if defined(_WIN32)
		HMODULE mod = nullptr;
		// FROM_ADDRESS なので第 2 引数は**名前ではなくアドレス**（だからこの cast が要る）。
		// PIN は UNCHANGED_REFCOUNT と併用できないが、FROM_ADDRESS とは併用できる。
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&VwProbePinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した（GetLastError=" +
				   std::to_string((unsigned long)::GetLastError()) + "）";
		// **どれを留めたかを名前で残す。** 期待は本体
		// （`VwSdkProbesPayload-<群>.vwpayload`）——殻（`VwSdkProbes.vlb`）を留めて
		// しまっていたら⑤の答えが逆になるので、推測ではなく名前で確かめられるようにする。
		wchar_t buf[MAX_PATH] = {0};
		const DWORD n = ::GetModuleFileNameW(mod, buf, MAX_PATH);
		if (n > 0 && n < MAX_PATH)
			pinnedPath = VwProbeNarrow(buf);
		if (pinnedPath.empty())
			pinnedPath = "(GetModuleFileNameW が読めなかった)";
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
		pinnedPath = info.dli_fname;
		return std::string();
#endif
	}

	// ランループを自分で回して、刻みが来るのを待つ（① の測り方）。
	void VwProbePump(double seconds)
	{
#if defined(_WIN32)
		// **GetTickCount64 を使う。** GetTickCount は約 49.7 日で巻き戻るので、
		// 稼働の長い実機では `until` が巻き戻りを跨いでこのループが即座に抜け、
		// ①が「0 回」と出てしまう——**仕掛かっていないのと見分けが付かない**。
		const ULONGLONG until = ::GetTickCount64() + (ULONGLONG)(seconds * 1000.0);
		// **スレッドメッセージだけを配る**（PeekMessage の hWnd に `-1`）。ウィンドウを
		// 持たないタイマーの WM_TIMER は `hwnd=NULL` のスレッドメッセージとして来るので、
		// ①（仕掛かって刻むか）はこれで足りる。hWnd に nullptr を渡して VW のウィンドウ
		// 向けのメッセージまで配ると、**プラグインの呼び出しの最中に VW のウィンドウ
		// プロシージャへ再入する**ことになるので、そこは踏まない
		// （「**VW のポンプが配るか**」は②③④が測る。そちらが本題である）。
		while (::GetTickCount64() < until)
		{
			MSG msg;
			while (::PeekMessageW(&msg, (HWND)(INT_PTR)-1, 0, 0, PM_REMOVE) != 0)
				::DispatchMessageW(&msg); // WM_TIMER は lParam の TimerProc へ回される
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

		// 局面ごとに「何回刻んだか」と「刻みの間隔」を畳む。間引かれていれば dt_ms が
		// 伸びる（JS タイマーは隠れると 60000 ms になった）。
		struct VwProbePhaseStat
		{
			std::string phase;
			int count = 0;
			long long minDt = 0;
			long long maxDt = 0;
			long long sumDt = 0;
			int fgNo = 0; // Vectorworks が裏に回っていた刻み（③ の対照）
			int modal = 0; // 入れ子のモーダルループの最中に来た刻み（② の答え）
		};
		std::vector<VwProbePhaseStat> stats;
		int ticksWithSdk = 0;
		int ticksWithTrouble = 0;
		for (const std::string& line : lines)
		{
			if (line.compare(0, 5, "tick ") != 0)
				continue;
			if (line.find("sdk=about-to-call") != std::string::npos)
				continue; // 前触れの行は数えない（本行と二重になる）
			const std::string phase = [&line]()
			{
				const size_t at = line.find("phase=");
				if (at == std::string::npos)
					return std::string("?");
				const size_t end = line.find(" ##", at);
				return line.substr(at + 6, (end == std::string::npos) ? end : end - at - 6);
			}();
			const long long dt = [&line]()
			{
				const size_t at = line.find("dt_ms=");
				if (at == std::string::npos)
					return (long long)-1;
				return (long long)std::atoll(line.c_str() + at + 6);
			}();
			if (line.find("trouble=-") == std::string::npos)
				++ticksWithTrouble;
			if (line.find("tick_count=-1") == std::string::npos)
				++ticksWithSdk;

			VwProbePhaseStat* found = nullptr;
			for (VwProbePhaseStat& s : stats)
				if (s.phase == phase)
					found = &s;
			if (found == nullptr)
			{
				stats.push_back(VwProbePhaseStat{phase, 0, dt, dt, 0, 0, 0});
				found = &stats.back();
			}
			++found->count;
			if (line.find("fg=no") != std::string::npos)
				++found->fgNo;
			// モーダルの判定は「持ち主の窓が無効」——VwProbeCurrentMode と同じ基準。
			if (line.find("+ownerdisabled") != std::string::npos)
				++found->modal;
			// **括って呼ぶ。** windows.h の `min` / `max` マクロが生きていても、
			// `(std::min)(…)` の形なら関数形式マクロとして展開されない。
			found->minDt = (std::min)(found->minDt, dt);
			found->maxDt = (std::max)(found->maxDt, dt);
			found->sumDt += dt;
		}

		probe.log("局面ごとの刻み（dt_ms = 前の刻みからの間隔。仕掛けた間隔は 250 ms）:");
		for (const VwProbePhaseStat& s : stats)
		{
			const long long avg = (s.count > 0) ? (s.sumDt / s.count) : 0;
			probe.log("  " + s.phase + ": " + std::to_string(s.count) + " 回, dt 最小 " +
					  std::to_string(s.minDt) + " / 平均 " + std::to_string(avg) + " / 最大 " +
					  std::to_string(s.maxDt) + " ms（裏に回っていた刻み " +
					  std::to_string(s.fgNo) + " 回 / モーダルの最中の刻み " +
					  std::to_string(s.modal) + " 回）");
		}
		probe.log("gSDK から値が読めた刻み: " + std::to_string(ticksWithSdk) +
				  " 回 / 異常のあった刻み: " + std::to_string(ticksWithTrouble) + " 回");
		probe.log("");

		// **読み方をプローブ自身に書かせる。** 刻みの数と dt だけを渡すと、
		// 「切り替えを忘れた」と「間引かれなかった」、「時計の分解能」と「間引き」が
		// 取り違えられる。判定の境目はここに書いておく。
		probe.log("--- 読み方 ---");
		probe.log("・dt が 250 前後なら間引かれていない。60000 前後ならウェブパレットの");
		probe.log("  JS タイマーと同じ間引きに当たっている。");
		probe.log("・`background` の行で「裏に回っていた刻み」が 0 なら、**切り替えが");
		probe.log("  行われていないので③は測れていない**（間引きが無いという根拠には");
		probe.log("  ならない。走らせ直す）。");
#if defined(_WIN32)
		probe.log("・Windows の WM_TIMER はシステムの時計の刻み（約 15.6 ms）へ丸められる");
		probe.log("  ので、250 ms で仕掛けても dt は 250〜266 ms に散る。**これは間引きでは");
		probe.log("  ない。**");
		probe.log("・②の答えは「モーダルの最中の刻み」の列に出る。数えているのは");
		probe.log("  `mode=` に `+ownerdisabled` が付いた刻み——**持ち主の窓が無効**なのが");
		probe.log("  Win32 でのモーダルの定義である。`modal-wait` の行がそこで 0 なら、");
		probe.log("  **入れ子のモーダルループには WM_TIMER が配られていない**ということ。");
		probe.log("・`+cls:` にはアクティブな窓のクラス名が出る（標準のダイアログは");
		probe.log("  `#32770`）。`modal-wait` でここが 0 件なら、VW の alert が");
		probe.log("  持ち主を無効にしない作りだということなので、クラス名の側で判断する。");
		probe.log("・`mode=` の `thread-<id>` が `arm` 行の `mode_at_arm` と一致している");
		probe.log("  ことを確かめる（ウィンドウ無しの SetTimer は仕掛けたスレッドへ来る）。");
#endif
		probe.log("");

		// 要点の行（仕掛け・書き込み・店じまい）は全部出す。刻みの行は多すぎるので、
		// **最後の 24 行**だけ出す（間引かれたかは上の表で足りる）。
		probe.log("--- 要点の行 ---");
		for (const std::string& line : lines)
		{
			if (line.compare(0, 4, "arm ") == 0 || line.compare(0, 6, "write ") == 0 ||
				line.compare(0, 7, "disarm ") == 0 || line.compare(0, 6, "phase ") == 0)
				probe.log(line);
		}
		probe.log("");
		probe.log("--- 刻みの行（最後の 24 行） ---");
		const size_t from = (lines.size() > 24) ? (lines.size() - 24) : 0;
		for (size_t i = from; i < lines.size(); ++i)
			probe.log(lines[i]);

		std::error_code ec;
		std::filesystem::remove(path, ec);
		probe.log("");
		probe.log("書き溜めを消した（まだ刻んでいる古いタイマーは、次の刻みで自分から止まる）。");
	}
} // namespace

VW_PROBE("runloop-timer-sdk", "OS のタイマーから gSDK を呼ぶ",
		 "ランループタイマーを仕掛け、モーダル中・裏に回った間・コマンドが戻った後に刻むかを測る")
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
	// ⑤ の答えがここに出る——**本体（…Payload-<群>.vwpayload）の名前が出ていなければ
	// 留め先を間違えている**ので、刻みが続いたとしてもピン留めの根拠にはならない。
	probe.log("  留めた先: " + pinnedPath);

	gVwProbeTimerState = new VwProbeTimerState();
	gVwProbeTimerState->filePath = path;
	gVwProbeTimerState->armedAt = std::chrono::steady_clock::now();
	gVwProbeTimerState->phase = "pump";

	// 仕掛けたスレッドを残す。**ウィンドウ無しの SetTimer は仕掛けたスレッドの
	// キューへ来る**ので、刻みの `mode=thread-<id>` がこれと一致するかが確かめられる。
	VwProbeAppend(path, "arm" + VwProbeField("interval_ms", VwProbeNum(kVwProbeIntervalMs)) +
							VwProbeField("max_ticks", VwProbeNum(kVwProbeMaxTicks)) +
							VwProbeField("mode_at_arm", VwProbeCurrentMode()) +
							VwProbeField("fg_at_arm", VwProbeForeground()) +
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
	const bool armed = (gVwProbeCFTimer != nullptr);
	if (armed)
	{
		// **メインのランループの kCFRunLoopCommonModes へ入れる。** AppKit は共通モードに
		// モーダルパネル・イベント追跡のモードを足しているので、これ 1 つでモーダルの
		// 最中にも届くはずである（届いたかは刻みの mode= で分かる）。
		::CFRunLoopAddTimer(::CFRunLoopGetMain(), gVwProbeCFTimer, kCFRunLoopCommonModes);
	}
	probe.log(armed ? "CFRunLoopTimer を仕掛けた（250 ms, メインのランループ / CommonModes）"
					: "CFRunLoopTimerCreate が nullptr を返した");
#endif
	if (!armed)
	{
		probe.fail("タイマーを仕掛けられなかった");
		return;
	}

	// ---------------------------------------------------- ① 自分で回して数える
	const int before1 = gVwProbeTimerState->ticks;
	probe.log("");
	probe.log("① ランループを 1.5 秒ぶん自分で回す（期待: 250 ms で 6 回前後）");
	VwProbePump(1.5);
	probe.log("  刻んだ回数: " + std::to_string(gVwProbeTimerState->ticks - before1));

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
	// `DoYield` は VW の再描画とイベント処理へ戻る（[進捗・診断](Progress%20and%20Diagnostics.md)）。
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

	// ------------------------------------------- ③ 裏に回っている間も刻むか
	gVwProbeTimerState->phase = "background";
	const int before3 = gVwProbeTimerState->ticks;
	VwProbeAppend(path, std::string("phase") + VwProbeField("name", "background"));
	gSDK->AlertInform("③ Vectorworks が裏に回っている間の刻みを測ります。",
					  "OK を押す前に、ほかのアプリへ切り替えて 20 秒ほど待ち、"
					  "それから Vectorworks へ戻って OK を押してください。");
	probe.log("③ ほかのアプリの裏に回っていた間の刻み: " +
			  std::to_string(gVwProbeTimerState->ticks - before3) + " 回");
	probe.log("  （間引かれていなければ 20 秒で 80 回前後。間引かれていれば数回）");

	// ------------------------------------------ ④ コマンドが戻った後も刻むか
	probe.log("");
	probe.log("④ ここから先は、このコマンドが戻った後の刻みを書き溜める。");
	probe.log("   **もう一度このプローブを走らせると、その結果が出る。**");
	probe.log("   走らせるまでに 60 秒ほど、パレットを閉じたり、ほかのアプリへ切り替えたり、");
	probe.log("   そのまま放っておいたりして構わない（どの間も 250 ms で刻むのが期待値）。");
	probe.log("   8 回目の刻みで**図面に 2D 基準点を 1 つ作る**ところまで試す。");
	probe.log("   タイマーは 300 秒（1200 刻み）で自分から止まる。");
	VwProbeAppend(path, std::string("phase") + VwProbeField("name", "outside"));
	gVwProbeTimerState->phase = "outside";
	gVwProbeTimerState->insideProbe = false;
}
