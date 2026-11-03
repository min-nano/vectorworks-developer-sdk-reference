//
//	probes/runtime/class-list/probe.cpp
//
//	[issue #280] 図面のクラスの一覧を `ISDK::ForEachClass` で列挙し、何が・どの順で渡るか、
//	`doGuestClasses` で何が変わるか、OS タイマーの刻みの中・図面 0 枚で呼べるかを実測する。
//
//	走らせるたびに次をする:
//	  1. 前回仕掛けた刻み（下記 5）の書き溜めを報告する。
//	  2. 試験用のクラスを**名前の順と違う順**で足す（既にあれば足さない）。
//	     階層（`-` 区切り）の子だけを足して、親が自動でできるかも見る。
//	  3. `ForEachClass(false)` と `ForEachClass(true)` を並べて、渡ったハンドルの型・
//	     `GetObjectInternalIndex`・`GetObjectName`・`ClassIDToName` / `ClassNameToID` の往復・
//	     参照ファイル由来か（`IsObjectFromReferencedFile`）を 1 行ずつ出す。
//	     1..`MaxClassID()` を `ClassIDToName` で引いた並びとも突き合わせる（順序の物差し）。
//	     「なし」・「寸法」の ID と、`GetDimensionClassID()` を呼ぶとクラスが増えるかも見る。
//	  4. 呼び出しの中で `SetTimer` ＋ `PeekMessage` を 1.5 秒回し、刻みの中から列挙する。
//	  5. 本体をピン留めして刻みを仕掛けて戻る。刻みは 5 分のあいだ、図面の枚数と列挙の
//	     件数を読み、**変わったときだけ**一時ファイルへ書く（呼ぶ前に 1 行書く——落ちても
//	     残る）。人が図面を全部閉じ、新規を開いてもう一度走らせると、1 で報告される。
//
//	殻は実行のたびに本体を別名で読み込むので静的変数は共有しない。止める合図は持ち主の
//	札（ファイル）で渡す（`quit-from-timer` と同じ作法）。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
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

namespace
{
	// 足す順（名前の順とわざと違えてある）。最後の 1 つは階層の子だけ。
	const char* const kClsAdd[] = {"zz280-最後", "AA280-最初", "mm280", "P280-子-孫"};

	const long long kClsTickMs = 200;
	const long long kClsArmMaxMs = 5LL * 60 * 1000;

	std::filesystem::path ClsDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return dir;
	}
	std::string ClsEventsPath()
	{
		return (ClsDir() / "VwSdkProbes-class-list-events.txt").string();
	}
	std::string ClsOwnerPath()
	{
		return (ClsDir() / "VwSdkProbes-class-list-owner.txt").string();
	}

	long long ClsWallMs()
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	void ClsAppend(const std::string& line)
	{
		std::ofstream out(ClsEventsPath(), std::ios::app);
		if (out)
			out << line << "\n";
	}

	std::string ClsFirstLine(const std::string& path)
	{
		std::ifstream in(path);
		std::string line;
		std::getline(in, line);
		return line;
	}

	int ClsDocCount()
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		return (int)list.GetSize();
	}

	std::string ClsName(MCObjectHandle h)
	{
		TXString name;
		gSDK->GetObjectName(h, name);
		return std::string(static_cast<const char*>(name));
	}

	std::string ClsIdName(InternalIndex id)
	{
		TXString name;
		gSDK->ClassIDToName(id, name);
		return std::string(static_cast<const char*>(name));
	}

	struct ClsSeen
	{
		MCObjectHandle h;
		short type;
		InternalIndex index;
		std::string name;
	};

	std::vector<ClsSeen> ClsCollect(bool guests)
	{
		std::vector<ClsSeen> out;
		gSDK->ForEachClass(guests, [&out](MCObjectHandle h) {
			ClsSeen s;
			s.h = h;
			s.type = gSDK->GetObjectTypeN(h);
			s.index = gSDK->GetObjectInternalIndex(h);
			s.name = ClsName(h);
			out.push_back(s);
		});
		return out;
	}

	int ClsCount(bool guests)
	{
		int n = 0;
		gSDK->ForEachClass(guests, [&n](MCObjectHandle) { ++n; });
		return n;
	}

	std::string ClsPtr(const void* p)
	{
		std::ostringstream os;
		os << p;
		return os.str();
	}

	// ---- 刻み（呼び出しの外） -------------------------------------------------

	struct ClsArmed
	{
		std::string owner;
		long long startMs = 0;
		long long ticks = 0;
		std::string last;
		bool stopped = false;
	};
	ClsArmed* gClsArmed = nullptr;
#if GS_WIN
	UINT_PTR gClsTimer = 0;
#else
	CFRunLoopTimerRef gClsTimer = nullptr;
#endif

	void ClsStopTimer()
	{
#if GS_WIN
		if (gClsTimer != 0)
			::KillTimer(nullptr, gClsTimer);
		gClsTimer = 0;
#else
		if (gClsTimer != nullptr)
		{
			::CFRunLoopTimerInvalidate(gClsTimer);
			::CFRelease(gClsTimer);
		}
		gClsTimer = nullptr;
#endif
	}

	void ClsArmedTick()
	{
		ClsArmed* st = gClsArmed;
		if (st == nullptr || st->stopped)
			return;
		const long long t = ClsWallMs() - st->startMs;
		if (ClsFirstLine(ClsOwnerPath()) != st->owner || t > kClsArmMaxMs)
		{
			st->stopped = true;
			ClsAppend("stop ## t=" + std::to_string(t) + " ## ticks=" + std::to_string(st->ticks));
			ClsStopTimer();
			return;
		}
		++st->ticks;
		const int docs = ClsDocCount();
		const bool layer = gSDK->GetCurrentLayer() != nullptr;
		const std::string pre = "docs=" + std::to_string(docs) + " layer=" + (layer ? "yes" : "no");
		if (pre != st->last.substr(0, pre.size()))
			ClsAppend("pre ## t=" + std::to_string(t) + " ## " + pre + " ## (これから列挙する)");
		const int nf = ClsCount(false);
		const int nt = ClsCount(true);
		const std::string now = pre + " false=" + std::to_string(nf) + " true=" + std::to_string(nt);
		if (now != st->last)
		{
			ClsAppend("tick ## t=" + std::to_string(t) + " ## tick=" + std::to_string(st->ticks) +
					  " ## " + now + " ## undo=" +
					  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
			st->last = now;
		}
	}

#if GS_WIN
	VOID CALLBACK ClsWinArmed(HWND, UINT, UINT_PTR, DWORD)
	{
		ClsArmedTick();
	}
#else
	void ClsCFArmed(CFRunLoopTimerRef, void*)
	{
		ClsArmedTick();
	}
#endif

	std::string ClsPinSelf()
	{
#if GS_WIN
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&ClsPinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した";
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&ClsPinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		if (::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY) == nullptr)
			return "dlopen(RTLD_NOLOAD) が失敗した";
		return std::string();
#endif
	}

	// ---- 刻み（呼び出しの中） -------------------------------------------------

	struct ClsInCall
	{
		int ticks = 0;
		int countFalse = -1;
		int countTrue = -1;
	};
	ClsInCall* gClsInCall = nullptr;

#if GS_WIN
	VOID CALLBACK ClsWinInCall(HWND, UINT, UINT_PTR, DWORD)
	{
		if (gClsInCall == nullptr)
			return;
		++gClsInCall->ticks;
		gClsInCall->countFalse = ClsCount(false);
		gClsInCall->countTrue = ClsCount(true);
	}
#else
	void ClsCFInCall(CFRunLoopTimerRef, void*)
	{
		if (gClsInCall == nullptr)
			return;
		++gClsInCall->ticks;
		gClsInCall->countFalse = ClsCount(false);
		gClsInCall->countTrue = ClsCount(true);
	}
#endif
}

VW_PROBE("class-list", "クラスの一覧を列挙する", "ForEachClass の中身と順序")
{
	// ---- 1. 前回の刻みの報告 ----
	{
		std::ifstream in(ClsEventsPath());
		std::string line;
		int n = 0;
		probe.log("== 1. 前回仕掛けた刻みの書き溜め ==");
		while (std::getline(in, line))
		{
			probe.log("  " + line);
			++n;
		}
		if (n == 0)
			probe.log("  (無し)");
	}
	// 前の刻みを止め、書き溜めを空にする（持ち主の札を替えると前の刻みは次の刻みで止まる）。
	{
		std::ofstream(ClsOwnerPath(), std::ios::trunc) << "handover-" << ClsWallMs() << "\n";
		std::ofstream(ClsEventsPath(), std::ios::trunc);
	}

	probe.log("docs=" + std::to_string(ClsDocCount()));

	// ---- 2. 試験用のクラスを足す ----
	const InternalIndex maxBefore = gSDK->MaxClassID();
	const int countBeforeAdd = ClsCount(false);
	probe.log("== 2. 足す前: MaxClassID=" + std::to_string(maxBefore) +
			  " ForEachClass(false) の件数=" + std::to_string(countBeforeAdd));
	for (const char* name : kClsAdd)
	{
		const InternalIndex existing = gSDK->ClassNameToID(name);
		if (existing != 0)
		{
			probe.log(std::string("  既にある: ") + name + " id=" + std::to_string(existing));
			continue;
		}
		const InternalIndex id = gSDK->AddClass(name);
		probe.log(std::string("  AddClass(") + name + ") -> id=" + std::to_string(id) +
				  " MaxClassID=" + std::to_string(gSDK->MaxClassID()));
	}
	for (const char* parent : {"P280", "P280-子"})
		probe.log(std::string("  親は自動でできたか: ClassNameToID(") + parent +
				  ")=" + std::to_string(gSDK->ClassNameToID(parent)));

	// ---- 3. 列挙 ----
	const InternalIndex noneId = gSDK->GetNoneClassID();
	const InternalIndex maxBeforeDim = gSDK->MaxClassID();
	probe.log("== 3. 列挙 ==");
	probe.log("  GetNoneClassID=" + std::to_string(noneId) + " (" + ClsIdName(noneId) + ")" +
			  " GetDefaultClass=" + std::to_string(gSDK->GetDefaultClass()) +
			  " GetActiveClass=" + std::to_string(gSDK->GetActiveClass()) +
			  " MaxClassID=" + std::to_string(maxBeforeDim));

	const std::vector<ClsSeen> withF = ClsCollect(false);
	const std::vector<ClsSeen> withT = ClsCollect(true);
	std::map<InternalIndex, int> inF;
	for (size_t i = 0; i < withF.size(); ++i)
		inF[withF[i].index] = (int)i;

	auto dump = [&probe](const char* label, const std::vector<ClsSeen>& seen) {
		probe.log(std::string("  -- ") + label + " 件数=" + std::to_string(seen.size()));
		for (size_t i = 0; i < seen.size(); ++i)
		{
			const ClsSeen& s = seen[i];
			const std::string byId = ClsIdName(s.index);
			const InternalIndex back = gSDK->ClassNameToID(s.name.c_str());
			probe.log("  [" + std::to_string(i) + "] type=" + std::to_string(s.type) +
					  " index=" + std::to_string(s.index) + " name=\"" + s.name + "\"" +
					  " IDToName=" + (byId == s.name ? "一致" : "\"" + byId + "\"") +
					  " NameToID=" + (back == s.index ? "一致" : std::to_string(back)) +
					  " ref=" + (gSDK->IsObjectFromReferencedFile(s.h) ? "yes" : "no") +
					  " vis=" + std::to_string(gSDK->GetClVisibility(s.index)) +
					  " h=" + ClsPtr(s.h));
		}
	};
	dump("ForEachClass(false)", withF);
	dump("ForEachClass(true)", withT);
	{
		std::string extra;
		for (const ClsSeen& s : withT)
			if (inF.find(s.index) == inF.end())
				extra += " " + std::to_string(s.index) + ":\"" + s.name + "\"";
		probe.log("  true にだけ渡ったもの:" + (extra.empty() ? std::string(" (無し)") : extra));
		bool sameOrder = true;
		size_t k = 0;
		for (const ClsSeen& s : withT)
			if (inF.find(s.index) != inF.end())
				sameOrder = sameOrder && k < withF.size() && withF[k++].index == s.index;
		probe.log(std::string("  共通部分の順序は同じか: ") + (sameOrder ? "yes" : "no"));
	}

	// 順序の物差し: 1..MaxClassID を ID 順に引く
	probe.log("  -- 1..MaxClassID を ClassIDToName で引いた並び");
	for (InternalIndex id = 0; id <= maxBeforeDim + 1; ++id)
	{
		const std::string n = ClsIdName(id);
		const bool listed = inF.find(id) != inF.end();
		probe.log("  id=" + std::to_string(id) + " name=\"" + n + "\"" +
				  (listed ? " (false の [" + std::to_string(inF[id]) + "])" : " (false に無し)"));
	}

	const InternalIndex dimId = gSDK->GetDimensionClassID();
	probe.log("  GetDimensionClassID=" + std::to_string(dimId) + " (" + ClsIdName(dimId) + ")" +
			  " MaxClassID " + std::to_string(maxBeforeDim) + " -> " +
			  std::to_string(gSDK->MaxClassID()) + " 件数(false) " +
			  std::to_string(withF.size()) + " -> " + std::to_string(ClsCount(false)));

	// ---- 4. 呼び出しの中で刻ませる ----
	probe.log("== 4. 呼び出しの中の刻みから列挙 ==");
	{
		ClsInCall st;
		gClsInCall = &st;
		const long long t0 = ClsWallMs();
#if GS_WIN
		const UINT_PTR timer = ::SetTimer(nullptr, 0, 100, &ClsWinInCall);
		while (ClsWallMs() - t0 < 1500)
		{
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, WM_TIMER, WM_TIMER, PM_REMOVE))
				::DispatchMessageW(&msg);
			::Sleep(10);
		}
		::KillTimer(nullptr, timer);
#else
		CFRunLoopTimerRef timer = ::CFRunLoopTimerCreate(
			nullptr, ::CFAbsoluteTimeGetCurrent() + 0.1, 0.1, 0, 0, &ClsCFInCall, nullptr);
		::CFRunLoopAddTimer(::CFRunLoopGetMain(), timer, kCFRunLoopCommonModes);
		while (ClsWallMs() - t0 < 1500)
			::CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
		::CFRunLoopTimerInvalidate(timer);
		::CFRelease(timer);
#endif
		gClsInCall = nullptr;
		probe.log("  刻み=" + std::to_string(st.ticks) + " 最後の件数 false=" +
				  std::to_string(st.countFalse) + " true=" + std::to_string(st.countTrue));
	}

	// ---- 5. 戻った後の刻みを仕掛ける ----
	probe.log("== 5. 図面 0 枚を測る刻みを仕掛ける ==");
	{
		const std::string pinErr = ClsPinSelf();
		if (!pinErr.empty())
		{
			probe.log("  仕掛けない: " + pinErr);
		}
		else
		{
			ClsArmed* st = new ClsArmed(); // ピン留めした本体の中で生き続ける
			st->owner = "owner-" + std::to_string(ClsWallMs());
			st->startMs = ClsWallMs();
			std::ofstream(ClsOwnerPath(), std::ios::trunc) << st->owner << "\n";
			gClsArmed = st;
			ClsAppend("armed ## owner=" + st->owner);
#if GS_WIN
			gClsTimer = ::SetTimer(nullptr, 0, (UINT)kClsTickMs, &ClsWinArmed);
			probe.log(std::string("  SetTimer=") + (gClsTimer != 0 ? "ok" : "失敗"));
#else
			gClsTimer = ::CFRunLoopTimerCreate(nullptr, ::CFAbsoluteTimeGetCurrent() + 0.2,
											   kClsTickMs / 1000.0, 0, 0, &ClsCFArmed, nullptr);
			::CFRunLoopAddTimer(::CFRunLoopGetMain(), gClsTimer, kCFRunLoopCommonModes);
			probe.log("  CFRunLoopTimer を仕掛けた");
#endif
			probe.log("  5 分のあいだ刻む。図面を全部閉じ、新規を開いてもう一度走らせると報告する。");
		}
	}

	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		gSDK->EndUndoEvent();
}
