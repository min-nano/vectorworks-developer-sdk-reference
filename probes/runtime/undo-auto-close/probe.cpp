//
//	probes/runtime/undo-auto-close/probe.cpp
//
//	[issue #247] **自動で開いた undo イベント（図形を作るだけで開く）を閉じずに置くと、
//	何で閉じるのか（閉じないのか）**を実測する。#213 ではメニューコマンドから返した回が
//	53 秒後に閉じ、#245 ではブリッジ（殻の時計の刻み）から走らせた回が 7 分閉じなかった。
//
//	**例外: このプローブは undo イベントを閉じずに置く**（それを調べるため。
//	probes/runtime/README.md「自動で開いた undo イベントは、戻る前に閉じる」の逆）。
//	作るのは線 1 本だけで、undo のスタックには触らない。新規の空図面で走らせる。
//
//	同じものを走らせるたびに、前の段の書き溜めを報告して次の段を仕掛ける:
//	  0 cmd   メニューコマンドの中で `CreateLine` して、閉じずに返す（#213 の形）
//	  1 tick  OS タイマーの刻みの中で `CreateLine` して、閉じずに抜ける（#245 のブリッジの形）
//
//	観測のタイマー（50 ms。mac は `kCFRunLoopCommonModes`、Windows は `SetTimer(nullptr, …)`）が
//	刻みごとに「アクティブな図面・その図面で `IsCurrentlyBuildingAnUndoEvent()`・図面の数・
//	前面の窓（mac はランループのモード）」を読み、変わったときだけ書く。`kNotifyUndoEndEvent`
//	（'Udee'）も書く。書くのは一時ディレクトリの
//	`VwSdkProbes-undo-auto-close-events.txt`（1 行ずつ開いて閉じる）。**人（や調べる側）が
//	同じファイルへ `mark …` の行を足せば、操作の時刻と並ぶ**——どの操作で閉じたかを見るため。
//	時刻はすべて壁時計（ローカル時刻）。観測は 20 分で止まる。
//

#include "Probe.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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
	const char* const kUacSteps[] = {"cmd", "tick"};
	const int kUacStepCount = 2;

	const long long kUacTickMs = 50;
	// 段 1 で線を作るまで。**メニューから走らせると結果ダイアログが出る**ので、閉じる間を取る。
#if GS_WIN
	const long long kUacCreateAtMs = 8000;
#else
	const long long kUacCreateAtMs = 1500;
#endif
	const long long kUacMaxMs = 20LL * 60 * 1000;

	std::filesystem::path UacDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return dir;
	}
	std::string UacEventsPath()
	{
		return (UacDir() / "VwSdkProbes-undo-auto-close-events.txt").string();
	}
	std::string UacOwnerPath()
	{
		return (UacDir() / "VwSdkProbes-undo-auto-close-owner.txt").string();
	}
	// 次に仕掛ける段の番号。消せば 0 からやり直す。
	std::string UacStepPath()
	{
		return (UacDir() / "VwSdkProbes-undo-auto-close-step.txt").string();
	}

	std::string UacReadFirstLine(const std::string& path)
	{
		std::ifstream in(path);
		std::string line;
		std::getline(in, line);
		return line;
	}

	void UacAppend(const std::string& line)
	{
		std::ofstream out(UacEventsPath(), std::ios::app);
		if (!out)
			return;
		out << line << "\n";
	}

	std::string UacYesNo(bool b)
	{
		return b ? "yes" : "no";
	}

	// 壁時計（ローカル時刻、ミリ秒まで）。`mark` の行と突き合わせるため。
	std::string UacClock()
	{
		using namespace std::chrono;
		const auto now = system_clock::now();
		const std::time_t secs = system_clock::to_time_t(now);
		const auto sinceEpoch = duration_cast<milliseconds>(now.time_since_epoch());
		const long long ms = (long long)(sinceEpoch.count() % 1000);
		std::tm local{};
#if GS_WIN
		localtime_s(&local, &secs);
#else
		localtime_r(&secs, &local);
#endif
		char buf[32] = {0};
		std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03lld", local.tm_hour, local.tm_min,
					  local.tm_sec, ms);
		return buf;
	}

	std::string UacFront()
	{
#if GS_WIN
		HWND h = ::GetForegroundWindow();
		if (h == nullptr)
			return "fg=(なし)";
		wchar_t title[256] = {0};
		::GetWindowTextW(h, title, 256);
		char utf8[768] = {0};
		const int room = (int)sizeof(utf8) - 1;
		::WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8, room, nullptr, nullptr);
		std::string out = std::string("fg=\"") + utf8 + "\"";
		if (::GetWindowThreadProcessId(h, nullptr) != ::GetCurrentThreadId())
			out += "（VW の外）";
		return out;
#else
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return "mode=(ランループの外)";
		char buf[256] = {0};
		std::string out = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8)
							  ? std::string(buf)
							  : std::string("(読めない)");
		::CFRelease(mode);
		return "mode=" + out;
#endif
	}

	// アクティブな図面の fileRef と図面の数。
	void UacDocs(Sint32& active, int& count)
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		count = (int)list.GetSize();
		active = -1;
		for (std::size_t i = 0; i < list.GetSize(); ++i)
		{
			if (list.GetAt(i).fIsActive)
				active = list.GetAt(i).fFileRef;
		}
	}

	// -----------------------------------------------------------------------
	// 刻みが持ち越す状態。**意図して leak する**（タイマーはプローブより長生き）。
	struct UacState
	{
		std::string owner;
		int step = 0;
		std::chrono::steady_clock::time_point armedAt;
		bool created = false;
		Sint32 targetDoc = -1; // 線を作った図面
		long long ticks = 0;
		long long ticksBuilding = 0;
		long long notifyEnd = 0;
		std::string lastKey;
		bool disarmed = false;
	};

	UacState* gUacState = nullptr;
#if GS_WIN
	UINT_PTR gUacTimer = 0;
#else
	CFRunLoopTimerRef gUacTimer = nullptr;
#endif

	long long UacElapsedMs(const UacState& st)
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - st.armedAt)
			.count();
	}

	void UacNotifyUndoEnd(StatusID, StatusData);

	void UacDisarm(const std::string& why)
	{
		UacState* st = gUacState;
		const bool write = st != nullptr && !st->disarmed;
		if (st != nullptr)
			st->disarmed = true;
#if GS_WIN
		if (gUacTimer != 0)
			::KillTimer(nullptr, gUacTimer);
		gUacTimer = 0;
#else
		if (gUacTimer != nullptr)
		{
			::CFRunLoopTimerInvalidate(gUacTimer);
			::CFRelease(gUacTimer);
			gUacTimer = nullptr;
		}
#endif
		gSDK->UnregisterNotificationProcedure(&UacNotifyUndoEnd, kNotifyUndoEndEvent);
		if (write)
		{
			std::string line = UacClock() + " disarm why=" + why;
			line += " ticks=" + std::to_string(st->ticks);
			line += " ticks_building=" + std::to_string(st->ticksBuilding);
			line += " notify_undo_end=" + std::to_string(st->notifyEnd);
			UacAppend(line);
		}
	}

	bool UacOwnerOk(UacState* st)
	{
		if (UacReadFirstLine(UacOwnerPath()) == st->owner)
			return true;
		st->disarmed = true; // 書き溜めはもう別の持ち主のもの。何も書かずに止まる
		return false;
	}

	void UacNotifyUndoEnd(StatusID, StatusData)
	{
		UacState* st = gUacState;
		if (st == nullptr || st->disarmed)
			return;
		++st->notifyEnd;
		Sint32 active = -1;
		int count = 0;
		UacDocs(active, count);
		std::string line = UacClock() + " notify UndoEndEvent";
		line += " n=" + std::to_string(st->notifyEnd);
		line += " active=" + std::to_string((long long)active);
		line += " building=" + UacYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
		line += " " + UacFront();
		UacAppend(line);
	}

	// 図形を作る呼び出しを順に呼び、**呼ぶたびに旗を書く**（どれが開くかを見る。Windows の
	// 最初の版では CreateLine だけでは開かなかった。#206 の mac は CreateLocus で開いた）。
	// 開いたものは閉じずに置く。
	void UacCreate(UacState* st, const char* where)
	{
		Sint32 active = -1;
		int count = 0;
		UacDocs(active, count);
		st->created = true;
		st->targetDoc = active;
		std::string line = UacClock() + " create where=" + where;
		line += " doc=" + std::to_string((long long)active);
		line += " before=" + UacYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
		MCObjectHandle h = gSDK->CreateLine(WorldPt(0.0, 0.0), WorldPt(1000.0, 500.0));
		line += " CreateLine=" + std::string(h == nullptr ? "nullptr" : "ok");
		line += "→" + UacYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
		MCObjectHandle locus = gSDK->CreateLocus(WorldPt(500.0, 0.0));
		line += " CreateLocus=" + std::string(locus == nullptr ? "nullptr" : "ok");
		line += "→" + UacYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
		MCObjectHandle extra = gSDK->CreateLine(WorldPt(0.0, 500.0), WorldPt(500.0, 500.0));
		if (extra != nullptr)
			gSDK->DeleteObject(extra, true);
		line += " DeleteObject(useUndo=true)";
		line += "→" + UacYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
		line += "（閉じずに置く）";
		UacAppend(line);
	}

	void UacTick()
	{
		UacState* st = gUacState;
		if (st == nullptr || st->disarmed || !UacOwnerOk(st))
			return;
		++st->ticks;
		const long long now = UacElapsedMs(*st);

		if (!st->created && kUacSteps[st->step] == std::string("tick") && now >= kUacCreateAtMs)
		{
			Sint32 active = -1;
			int count = 0;
			UacDocs(active, count);
			if (count != 1)
			{
				UacAppend(UacClock() + " abort docs=" + std::to_string(count));
				UacDisarm("docs-not-1");
				return;
			}
			UacCreate(st, "tick");
		}

		Sint32 active = -1;
		int count = 0;
		UacDocs(active, count);
		const bool building = gSDK->IsCurrentlyBuildingAnUndoEvent();
		if (building)
			++st->ticksBuilding;
		std::string key = "active=" + std::to_string((long long)active);
		if (active == st->targetDoc)
			key += "(線の図面)";
		key += " docs=" + std::to_string(count);
		key += " building=" + UacYesNo(building);
		key += " " + UacFront();
		if (key != st->lastKey)
		{
			std::string line = UacClock() + " state tick=" + std::to_string(st->ticks);
			UacAppend(line + " " + key);
			st->lastKey = key;
		}
		if (now > kUacMaxMs)
			UacDisarm("time-limit");
	}

#if GS_WIN
	VOID CALLBACK UacWinTick(HWND, UINT, UINT_PTR, DWORD)
	{
		UacTick();
	}
#else
	void UacCFTick(CFRunLoopTimerRef, void*)
	{
		UacTick();
	}
#endif

	std::string UacPinSelf()
	{
#if GS_WIN
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&UacPinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した";
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&UacPinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		if (::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY) == nullptr)
			return "dlopen(RTLD_NOLOAD) が失敗した";
		return std::string();
#endif
	}

	void UacReport(vwprobe::Report& probe)
	{
		std::error_code ec;
		std::filesystem::remove(UacOwnerPath(), ec); // 生きている刻みは次の刻みで止まる
		std::ifstream in(UacEventsPath());
		std::string line;
		std::size_t n = 0;
		probe.log("■ 前の段の書き溜め");
		while (std::getline(in, line))
		{
			if (++n <= 400)
				probe.log("  " + line);
		}
		if (n > 400)
			probe.log("  …（" + std::to_string(n - 400) + " 行省略）");
		in.close();
		std::filesystem::remove(UacEventsPath(), ec);
	}

	void UacArm(vwprobe::Report& probe, int step)
	{
		const std::string trouble = UacPinSelf();
		if (!trouble.empty())
		{
			probe.fail("本体をピン留めできなかったので仕掛けない（" + trouble + "）");
			return;
		}
		Sint32 active = -1;
		int count = 0;
		UacDocs(active, count);
		if (count != 1)
		{
			probe.fail("図面を 1 枚だけ開いた状態で走らせてください");
			return;
		}
		gUacState = new UacState();
		UacState* st = gUacState;
		st->owner = UacClock() + "-" + VW_PAYLOAD_BUILD_ID;
		st->step = step;
		st->armedAt = std::chrono::steady_clock::now();
		{
			std::ofstream out(UacOwnerPath(), std::ios::trunc);
			out << st->owner << "\n";
		}
		{
			std::ofstream out(UacStepPath(), std::ios::trunc);
			out << step << "\n";
		}
		std::error_code ec;
		std::filesystem::remove(UacEventsPath(), ec);
		std::string line = UacClock() + " arm step=" + kUacSteps[step];
		line += " build=" + std::string(VW_PAYLOAD_BUILD_ID);
		line += " doc=" + std::to_string((long long)active);
		line += " building=" + UacYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent());
		line += " " + UacFront();
		UacAppend(line);
		const Boolean reg =
			gSDK->RegisterNotificationProcedure(&UacNotifyUndoEnd, kNotifyUndoEndEvent);
		UacAppend(UacClock() + " register UndoEndEvent=" + UacYesNo(reg != 0));

		if (kUacSteps[step] == std::string("cmd"))
			UacCreate(st, "cmd");

#if GS_WIN
		gUacTimer = ::SetTimer(nullptr, 0, (UINT)kUacTickMs, &UacWinTick);
		const bool armed = gUacTimer != 0;
#else
		const CFAbsoluteTime t = (CFAbsoluteTime)kUacTickMs / 1000.0;
		gUacTimer = ::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + t, t,
										   0, 0, &UacCFTick, nullptr);
		const bool armed = gUacTimer != nullptr;
		if (armed)
			::CFRunLoopAddTimer(::CFRunLoopGetMain(), gUacTimer, kCFRunLoopCommonModes);
#endif
		if (!armed)
		{
			probe.fail("タイマーを仕掛けられなかった");
			UacDisarm("arm-failed");
			return;
		}
		probe.log(std::string("■ 段 ") + std::to_string(step) + " 「" + kUacSteps[step] + "」");
		probe.log("記録: " + UacEventsPath());
		probe.log("undo の旗を 20 分見続ける。操作は mark 行で同じファイルへ足す。");
	}
} // namespace

VW_PROBE("undo-auto-close", "自動の undo はいつ閉じるか", "閉じずに置いた undo の行方")
{
	int next = 0;
	std::error_code ec;
	if (std::filesystem::exists(UacEventsPath(), ec))
	{
		const std::string measured = UacReadFirstLine(UacStepPath());
		UacReport(probe);
		next = measured.empty() ? 0 : std::atoi(measured.c_str()) + 1;
	}
	else
	{
		const std::string saved = UacReadFirstLine(UacStepPath());
		next = saved.empty() ? 0 : std::atoi(saved.c_str());
	}
	if (next >= kUacStepCount || next < 0)
	{
		std::filesystem::remove(UacStepPath(), ec);
		probe.log("■ 全段を測り終えた（次に走らせると段 0 から始める）");
		return;
	}
	UacArm(probe, next);
}
