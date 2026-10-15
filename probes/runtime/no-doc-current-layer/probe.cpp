//
//	probes/runtime/no-doc-current-layer/probe.cpp
//
//	[issue #224] **図面が 1 枚も開いていないとき、OS タイマーの刻みから
//	`gSDK->GetCurrentLayer()` を呼ぶと何が返るか**を実測する。
//
//	確かめること:
//	  ① 戻り値は nullptr か、何かのハンドルか。落ちないか。
//	  ② nullptr でなければ、そのハンドルの名前（GetObjectName）と型（GetObjectTypeN）。
//	  ③ `GetOpenFilesList()` の件数（0 か 1 以上か）と `GetCurrentLayer()` が nullptr か
//	     どうかは、**刻みごとに**常に一致するか。食い違う刻みがあれば全部書き出す。
//
//	仕掛け（Findings「Timers and Notifications」4 の作法をそのまま使う）:
//	  * 1 回目が本体をピン留めし、20 ms の刻みを仕掛けて戻る（mac: `CFRunLoopTimer` を
//	    `kCFRunLoopCommonModes` へ / Windows: `SetTimer(nullptr, …)`）。
//	  * 刻みは毎回 件数 → `GetCurrentLayer()` →（ハンドルがあれば）名前・型 を読み、
//	    **前の刻みと何かが変わったときだけ**書き溜めへ 1 行足す。件数が変わった刻みでは
//	    **呼ぶ前に**「これから呼ぶ」行を書く（落ちたら、その行が最後に残る）。
//	  * その間に **図面を全部閉じる → 新規を開く**（人でもブリッジでもよい）。
//	  * 刻みが「0 枚 → 1 枚以上」を見届けた後の実行が書き溜めを読んで報告し、書き溜めを消す（消えたのを見て刻みが止まる）。
//	  * 書き溜めの心拍が古い（＝Vectorworks が終わって刻みが居ない）ときは、それを報告して
//	    から**その場で仕掛け直す**——再起動直後の 1 回目がそのまま仕掛けになる。
//
//	プローブ自身は図面に何も書かない（読むだけ）。
//

#include "Probe.h"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if GS_WIN
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#else
#	include <CoreFoundation/CoreFoundation.h>
#	include <dlfcn.h>
#endif

#ifndef VW_PAYLOAD_BUILD_ID
#	define VW_PAYLOAD_BUILD_ID "local"
#endif

namespace
{
	const long long kNdclIntervalMs = 20;
	// 報告を忘れられても勝手に止まるように（20 分）。
	const long long kNdclMaxMs = 20LL * 60 * 1000;
	// 心拍がこれより古ければ、刻みはもう居ない（Vectorworks が終了した）と見る。
	const long long kNdclStaleMs = 5000;
	// 食い違いの行は多すぎると読めないので、これを超えたら数えるだけにする。
	const int kNdclMaxMismatchLines = 40;

	std::filesystem::path NdclDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return dir;
	}
	std::string NdclEventsPath()
	{
		return (NdclDir() / "VwSdkProbes-no-doc-current-layer-events.txt").string();
	}
	std::string NdclCountsPath()
	{
		return (NdclDir() / "VwSdkProbes-no-doc-current-layer-counts.txt").string();
	}

	// **刻みの持ち主の札。** 殻は実行のたびに本体を別の名前で複製して読み込む
	// （`VwSdkProbes-<番号>-…vwpayload`）ので、**報告した実行と刻みは別の像**で、静的変数を
	// 共有しない。止める合図はファイルで渡す——札が消えたか、別の札に書き換わったら止まる。
	std::string NdclOwnerPath()
	{
		return (NdclDir() / "VwSdkProbes-no-doc-current-layer-owner.txt").string();
	}

	std::string NdclReadOwner()
	{
		std::ifstream in(NdclOwnerPath());
		std::string id;
		std::getline(in, id);
		return id;
	}

	long long NdclWallMs()
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	// 1 行書いて閉じる。**毎回開き直す**ので、Vectorworks ごと落ちてもそこまでが残る。
	void NdclAppend(const std::string& path, const std::string& line)
	{
		std::ofstream out(path, std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	std::string NdclHex(const void* p)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", p);
		return buf;
	}

	std::string NdclMode()
	{
#if GS_WIN
		return "win";
#else
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return "(ランループの外)";
		char buf[256] = {0};
		std::string out = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8)
							  ? std::string(buf)
							  : std::string("(読めない)");
		::CFRelease(mode);
		return out;
#endif
	}

	// 刻みでも、コマンドの中でも同じものを読む。
	struct NdclSample
	{
		int docs = -1;
		int active = -1;
		const void* layer = nullptr;
		std::string name;
		short type = -1;
	};

	void NdclReadDocs(NdclSample& s)
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		s.docs = (int)list.GetSize();
		s.active = 0;
		for (std::size_t i = 0; i < list.GetSize(); ++i)
		{
			if (list.GetAt(i).fIsActive)
				++s.active;
		}
	}

	void NdclReadLayerDetail(NdclSample& s, MCObjectHandle h)
	{
		TXString name;
		gSDK->GetObjectName(h, name);
		s.name = static_cast<const char*>(name);
		s.type = gSDK->GetObjectTypeN(h);
	}

	std::string NdclDescribe(const NdclSample& s)
	{
		std::ostringstream o;
		o << "docs=" << s.docs << " active=" << s.active
		  << " layer=" << (s.layer == nullptr ? std::string("nullptr") : NdclHex(s.layer));
		if (s.layer != nullptr)
			o << " name=\"" << s.name << "\" type=" << s.type;
		o << " agree=" << (((s.docs > 0) == (s.layer != nullptr)) ? "yes" : "NO");
		return o.str();
	}

	// -----------------------------------------------------------------------
	// 刻みが持ち越す状態。**意図して leak する**（タイマーはプローブより長生き）。
	struct NdclTimerState
	{
		std::string owner;
		std::string events;
		std::string counts;
		std::chrono::steady_clock::time_point armedAt;
		long long ticks = 0;
		long long lastMs = -1;
		long long maxGapMs = 0;
		int lastDocs = -2;
		const void* lastLayer = reinterpret_cast<const void*>(1); // 最初の刻みで必ず「変わった」
		std::string lastKey;
		// 件数 0/1+ × ハンドル nullptr/あり の 4 通り
		long long d0Null = 0, d0Some = 0, dNNull = 0, dNSome = 0;
		long long mismatches = 0;
		int mismatchLines = 0;
		bool disarmed = false;
	};

	NdclTimerState* gNdclState = nullptr;
#if GS_WIN
	UINT_PTR gNdclWinTimer = 0;
#else
	CFRunLoopTimerRef gNdclCFTimer = nullptr;
#endif

	long long NdclElapsedMs(const NdclTimerState& st)
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - st.armedAt)
			.count();
	}

	void NdclWriteCounts(const NdclTimerState& st)
	{
		std::ofstream out(st.counts, std::ios::trunc);
		if (!out)
			return;
		out << "heartbeat_wall_ms " << NdclWallMs() << "\n"
			<< "elapsed_ms " << NdclElapsedMs(st) << "\n"
			<< "ticks " << st.ticks << "\n"
			<< "max_gap_ms " << st.maxGapMs << "\n"
			<< "docs0_layer_null " << st.d0Null << "\n"
			<< "docs0_layer_some " << st.d0Some << "\n"
			<< "docsN_layer_null " << st.dNNull << "\n"
			<< "docsN_layer_some " << st.dNSome << "\n"
			<< "mismatches " << st.mismatches << "\n";
	}

	void NdclDisarm(const std::string& why)
	{
#if GS_WIN
		if (gNdclWinTimer != 0)
		{
			::KillTimer(nullptr, gNdclWinTimer);
			gNdclWinTimer = 0;
		}
#else
		if (gNdclCFTimer != nullptr)
		{
			::CFRunLoopTimerInvalidate(gNdclCFTimer);
			::CFRelease(gNdclCFTimer);
			gNdclCFTimer = nullptr;
		}
#endif
		NdclTimerState* st = gNdclState;
		if (st == nullptr || st->disarmed)
			return;
		st->disarmed = true;
		std::error_code ec;
		if (std::filesystem::exists(st->events, ec))
		{
			NdclWriteCounts(*st);
			NdclAppend(st->events, "disarm t_ms=" + std::to_string(NdclElapsedMs(*st)) +
									   " ticks=" + std::to_string(st->ticks) + " why=" + why);
		}
	}

	void NdclTick()
	{
		NdclTimerState* st = gNdclState;
		if (st == nullptr || st->disarmed)
			return;
		++st->ticks;
		const long long now = NdclElapsedMs(*st);
		if (st->lastMs >= 0 && now - st->lastMs > st->maxGapMs)
			st->maxGapMs = now - st->lastMs;
		st->lastMs = now;

		// **毎刻み、何かを書く前に**札を確かめる（以前は 10 刻みに 1 回で、その間に
		// 報告で消された書き溜めを作り直していた。実際に踏んだ）。
		if (NdclReadOwner() != st->owner)
		{
			st->disarmed = true; // 書き溜めはもう別の持ち主のもの。何も書かずに止まる
			NdclDisarm("owner-changed");
			return;
		}
		if (now > kNdclMaxMs)
		{
			NdclDisarm("time-limit");
			return;
		}

		NdclSample s;
		NdclReadDocs(s);
		// **件数が変わった刻みでは、呼ぶ前に書く**（ここで落ちれば、この行が最後に残る）。
		if (s.docs != st->lastDocs)
		{
			NdclAppend(st->events,
					   "before t_ms=" + std::to_string(now) + " tick=" + std::to_string(st->ticks) +
						   " docs=" + std::to_string(s.docs) + " → GetCurrentLayer() を呼ぶ");
			st->lastDocs = s.docs;
		}
		MCObjectHandle h = gSDK->GetCurrentLayer();
		s.layer = h;
		if (h != nullptr)
		{
			if (s.layer != st->lastLayer)
				NdclAppend(st->events, "before t_ms=" + std::to_string(now) +
										   " layer=" + NdclHex(s.layer) +
										   " → GetObjectName / GetObjectTypeN を呼ぶ");
			NdclReadLayerDetail(s, h);
		}
		st->lastLayer = s.layer;

		if (s.docs == 0)
			++(h == nullptr ? st->d0Null : st->d0Some);
		else
			++(h == nullptr ? st->dNNull : st->dNSome);

		const std::string key = NdclDescribe(s);
		const bool agree = (s.docs > 0) == (h != nullptr);
		if (!agree)
			++st->mismatches;
		if (key != st->lastKey)
		{
			NdclAppend(st->events, "state t_ms=" + std::to_string(now) +
									   " tick=" + std::to_string(st->ticks) + " " + key +
									   " mode=" + NdclMode());
			st->lastKey = key;
		}
		else if (!agree && st->mismatchLines < kNdclMaxMismatchLines)
		{
			++st->mismatchLines;
			NdclAppend(st->events, "mismatch t_ms=" + std::to_string(now) +
									   " tick=" + std::to_string(st->ticks) + " " + key +
									   " mode=" + NdclMode());
		}
		if (st->ticks % 25 == 0)
			NdclWriteCounts(*st);
	}

#if GS_WIN
	VOID CALLBACK NdclWinTimerProc(HWND, UINT, UINT_PTR, DWORD)
	{
		NdclTick();
	}
#else
	void NdclCFTimerProc(CFRunLoopTimerRef, void*)
	{
		NdclTick();
	}
#endif

	// 殻はプローブが終わると本体を降ろすので、刻みの行き先を残すためにピン留めする。
	std::string NdclPinSelf(std::string& pinnedPath)
	{
#if GS_WIN
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&NdclPinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した（GetLastError=" +
				   std::to_string((unsigned long)::GetLastError()) + "）";
		char buf[MAX_PATH] = {0};
		const DWORD n = ::GetModuleFileNameA(mod, buf, MAX_PATH);
		pinnedPath = (n > 0 && n < MAX_PATH) ? std::string(buf) : std::string("(名前が読めない)");
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&NdclPinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		void* handle = ::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
		if (handle == nullptr)
			return std::string("dlopen(RTLD_NOLOAD) が失敗した: ") + info.dli_fname;
		pinnedPath = info.dli_fname;
		return std::string();
#endif
	}

	std::vector<std::string> NdclReadLines(const std::string& path)
	{
		std::vector<std::string> lines;
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line))
			lines.push_back(line);
		return lines;
	}

	long long NdclHeartbeat(const std::vector<std::string>& counts)
	{
		for (const std::string& line : counts)
		{
			if (line.rfind("heartbeat_wall_ms ", 0) == 0)
				return std::atoll(line.c_str() + 18);
		}
		return 0;
	}

	// 書き溜めを報告して消す。刻みがまだ生きていれば、消えたのを見て止まる。
	void NdclReport(vwprobe::Report& probe, bool alive)
	{
		// 札を消す——刻みは次の刻みでそれを見て、何も書かずに止まる。
		std::error_code ec0;
		std::filesystem::remove(NdclOwnerPath(), ec0);
		probe.log(alive
					  ? "■ 仕掛けた刻みの報告（刻みはまだ生きている。報告の後に止まる）"
					  : "■ 前の仕掛けの書き溜め（心拍が古い＝Vectorworks が終わって刻みは居ない）");
		probe.log("  -- 経過（変わった刻みだけ）--");
		const std::vector<std::string> events = NdclReadLines(NdclEventsPath());
		const std::size_t kMax = 400;
		for (std::size_t i = 0; i < events.size() && i < kMax; ++i)
			probe.log("  " + events[i]);
		if (events.size() > kMax)
			probe.log("  …（" + std::to_string(events.size() - kMax) + " 行省略）");
		probe.log("  -- 数 --");
		for (const std::string& line : NdclReadLines(NdclCountsPath()))
			probe.log("  " + line);
		std::error_code ec;
		std::filesystem::remove(NdclEventsPath(), ec);
		std::filesystem::remove(NdclCountsPath(), ec);
	}

	// 刻みが「図面 0 枚 → 1 枚以上」を見届けたか。**ブリッジは図面を開いてすぐ同じ刻みの中で
	// プローブを走らせる**ので、その実行で報告すると「開いた後」を刻みが 1 度も見ていない
	// （実際に踏んだ）。見届けるまでは報告せずに刻みを続ける。
	bool NdclSawReopen()
	{
		bool sawZero = false;
		for (const std::string& line : NdclReadLines(NdclEventsPath()))
		{
			if (line.rfind("state ", 0) != 0)
				continue;
			const bool zero = line.find(" docs=0 ") != std::string::npos;
			if (zero)
				sawZero = true;
			else if (sawZero)
				return true;
		}
		return false;
	}

	bool NdclArm(vwprobe::Report& probe)
	{
		std::string pinnedPath;
		const std::string trouble = NdclPinSelf(pinnedPath);
		if (!trouble.empty())
		{
			probe.fail("本体をピン留めできなかったので仕掛けない（" + trouble + "）");
			return false;
		}
		probe.log("本体をピン留めした: " + pinnedPath);

		gNdclState = new NdclTimerState();
		gNdclState->owner = std::to_string(NdclWallMs()) + "-" + VW_PAYLOAD_BUILD_ID;
		gNdclState->events = NdclEventsPath();
		gNdclState->counts = NdclCountsPath();
		gNdclState->armedAt = std::chrono::steady_clock::now();
		{
			// 札を書き換えた時点で、前に仕掛けた刻み（別の像でも）は止まる。
			std::ofstream out(NdclOwnerPath(), std::ios::trunc);
			out << gNdclState->owner << "\n";
		}
		std::error_code ec;
		std::filesystem::remove(gNdclState->events, ec);
		NdclAppend(gNdclState->events, std::string("arm owner=") + gNdclState->owner +
										   " interval_ms=" + std::to_string(kNdclIntervalMs) +
										   " build=" + VW_PAYLOAD_BUILD_ID);
		NdclWriteCounts(*gNdclState);

#if GS_WIN
		gNdclWinTimer = ::SetTimer(nullptr, 0, (UINT)kNdclIntervalMs, &NdclWinTimerProc);
		const bool armed = (gNdclWinTimer != 0);
#else
		const CFAbsoluteTime interval = (CFAbsoluteTime)kNdclIntervalMs / 1000.0;
		gNdclCFTimer =
			::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + interval,
								   interval, 0, 0, &NdclCFTimerProc, nullptr);
		const bool armed = (gNdclCFTimer != nullptr);
		if (armed)
			::CFRunLoopAddTimer(::CFRunLoopGetMain(), gNdclCFTimer, kCFRunLoopCommonModes);
#endif
		if (!armed)
		{
			probe.fail("タイマーを仕掛けられなかった");
			return false;
		}
		probe.log(
			"20 ms の刻みを仕掛けた（mac: CFRunLoopTimer / CommonModes, Windows: SetTimer）。");
		probe.log("");
		probe.log("このあと:");
		probe.log("  1. 開いている図面を**全部閉じる**（数秒そのまま置く）");
		probe.log("  2. **新規の図面を開く**");
		probe.log("  3. 数秒置いてから、もう一度このプローブを走らせる（それが報告になる）");
		return true;
	}
} // namespace

VW_PROBE("no-doc-current-layer", "図面が無いときの GetCurrentLayer",
		 "刻みから件数とレイヤを並べて記録")
{
	// コマンドの中（メニュー、またはブリッジの刻み）で 1 回読む。
	{
		NdclSample s;
		NdclReadDocs(s);
		probe.log("コマンドの中で読む: docs=" + std::to_string(s.docs) +
				  " → GetCurrentLayer() を呼ぶ（mode=" + NdclMode() + "）");
		MCObjectHandle h = gSDK->GetCurrentLayer();
		s.layer = h;
		if (h != nullptr)
			NdclReadLayerDetail(s, h);
		probe.log("  " + NdclDescribe(s));
	}
	probe.log("");

	std::error_code ec;
	if (std::filesystem::exists(NdclEventsPath(), ec))
	{
		const long long beat = NdclHeartbeat(NdclReadLines(NdclCountsPath()));
		const long long age = NdclWallMs() - beat;
		const bool alive = beat > 0 && age >= 0 && age < kNdclStaleMs;
		probe.log("書き溜めがある（心拍 " + std::to_string(age) + " ms 前）");
		if (alive && !NdclSawReopen())
		{
			probe.log("刻みはまだ「図面 0 枚 → 1 枚以上」を見届けていないので、報告せずに続ける。");
			probe.log("（図面を開いて数秒置いてから、もう一度走らせると報告になる）");
			return;
		}
		NdclReport(probe, alive);
		if (alive)
			return; // 報告で 1 周が閉じる
		probe.log("");
		probe.log("■ 刻みが居ないので、この起動で仕掛け直す");
	}
	(void)NdclArm(probe);
}
