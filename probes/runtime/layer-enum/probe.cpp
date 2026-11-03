//
//	probes/runtime/layer-enum/probe.cpp
//
//	[issue #281] 前面の図面のレイヤを列挙する道を実測する。
//
//	  1. `GetDrawingHeader` → `FirstMemberObj` → `NextObject` の列にシートレイヤも並ぶか。
//	     `VWDocument::GetDrawingHeaderFristMember()` と同じ先頭か。`ForEachLayer` の並びと
//	     一致するか。レイヤ以外のもの（ビューポートを作った後など）が混ざるか。
//	  2. デザイン / シートの見分け: `ovLayerType`(154) と `VWLayerObj::GetLayerType()`。
//	  3. 順序: レイヤ高さ・作った順・シートの名前の順との関係（オーガナイザとは目で突き合わせる）。
//	  4. 現在のレイヤ: `GetCurrentLayer()` と列のハンドルの `==`。シートレイヤを現在にしたとき。
//	  5. 縮尺: `GetLayerScaleN` の値の形（1/50 で 50 か 0.02 か）。シートレイヤでの値。
//	  6. 名前（`GetObjectName`）と、直下のオブジェクトの数（参考）。
//	  7. OS タイマーの刻みの中（メニューから戻った後）での列挙と、図面 0 枚での列挙。
//
//	1〜6 はメニューの呼び出しの中で測る。7 は本体をピン留めしてタイマー（50 ms）を仕掛けて
//	戻り、**主窓が操作できる状態（モーダルが無い）になってから**刻みの中で列挙して一時ファイル
//	`VwSdkProbes-layer-enum-events.txt` へ書く（次に走らせたときに報告する）。
//	一時ディレクトリの `VwSdkProbes-layer-enum-mode.txt` の 1 行目が `nodoc` のときだけ、
//	刻みの中で**開いている図面をすべて閉じ（保存しない）**、図面 0 枚で列挙してから新規を開く。
//
//	図面にはレイヤ `LE-…` を足す（新規の空図面で走らせる）。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
	const long long kLeTickMs = 50;
	const long long kLeMaxMs = 10LL * 60 * 1000;

	std::filesystem::path LeDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return dir;
	}
	std::string LeEventsPath()
	{
		return (LeDir() / "VwSdkProbes-layer-enum-events.txt").string();
	}
	std::string LeModePath()
	{
		return (LeDir() / "VwSdkProbes-layer-enum-mode.txt").string();
	}

	std::string LeReadFirstLine(const std::string& path)
	{
		std::ifstream in(path);
		std::string line;
		std::getline(in, line);
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		return line;
	}

	long long LeWallMs()
	{
		return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	std::string LeNum(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.10g", v);
		return buf;
	}

	std::string LePtr(const void* p)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", p);
		return buf;
	}

	std::string LeName(MCObjectHandle h)
	{
		TXString name;
		gSDK->GetObjectName(h, name);
		return std::string(static_cast<const char*>(name));
	}

	// オブジェクト変数を、受け取れた型の名前つきで文字にする（型が分からないため総当たり）。
	std::string LeVar(MCObjectHandle h, short sel)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, sel, v))
			return "(GetObjectVariable=false)";
		Sint16 s16 = 0;
		if (v.GetSint16(s16))
			return "s16:" + std::to_string(s16);
		Sint32 s32 = 0;
		if (v.GetSint32(s32))
			return "s32:" + std::to_string(s32);
		Sint8 s8 = 0;
		if (v.GetSint8(s8))
			return "s8:" + std::to_string((int)s8);
		Real64 r = 0;
		if (v.GetReal64(r))
			return "r64:" + LeNum(r);
		bool b = false;
		if (v.GetBoolean(b))
			return std::string("bool:") + (b ? "true" : "false");
		TXString s;
		if (v.GetTXString(s))
			return "str:\"" + std::string(static_cast<const char*>(s)) + "\"";
		return "(型が読めない)";
	}

	long long LeCountMembers(MCObjectHandle h)
	{
		long long n = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(h); m != nullptr && n < 1000000;
			 m = gSDK->NextObject(m))
			++n;
		return n;
	}

	// 1 件ぶんの行。
	std::string LeDescribe(MCObjectHandle h, MCObjectHandle current)
	{
		const short type = gSDK->GetObjectTypeN(h);
		std::string line = "type=" + std::to_string(type) + " name=\"" + LeName(h) + "\"";
		if (type != kLayerNode)
			return line + " （レイヤではない）";
		VWLayerObj layer(h);
		double_gs scale = -1;
		gSDK->GetLayerScaleN(h, scale);
		line += " ovLayerType=" + LeVar(h, ovLayerType);
		line += " GetLayerType=" + std::to_string((int)layer.GetLayerType());
		line += " GetLayerScaleN=" + LeNum(scale);
		line += " VWLayerObj::GetScale=" + LeNum(layer.GetScale());
		line += " elev=" + LeNum(layer.GetElevation());
		line += " sheetTitle(162)=" + LeVar(h, ovLayerExpandedSheetName);
		line += " members=" + std::to_string(LeCountMembers(h));
		line += std::string(" current==") + (h == current ? "yes" : "no");
		return line;
	}

	// 図面の列を辿る。**呼ぶ前に 1 行書く**（図面 0 枚で落ちるなら、その行が最後に残る）。
	std::vector<std::string> LeWalk(void (*say)(const std::string&))
	{
		std::vector<std::string> out;
		say("  GetDrawingHeader() を呼ぶ");
		MCObjectHandle header = gSDK->GetDrawingHeader();
		say("  header=" + LePtr(header) +
			(header ? (" type=" + std::to_string(gSDK->GetObjectTypeN(header))) : ""));
		say("  VWDocument::GetDrawingHeaderFristMember() を呼ぶ");
		MCObjectHandle firstV = VWDocument::GetDrawingHeaderFristMember();
		MCObjectHandle first = header ? gSDK->FirstMemberObj(header) : nullptr;
		say("  first(FirstMemberObj)=" + LePtr(first) + " first(VWDocument)=" + LePtr(firstV) +
			(first == firstV ? " 一致" : " 不一致"));
		say("  GetCurrentLayer() を呼ぶ");
		MCObjectHandle current = gSDK->GetCurrentLayer();
		say("  current=" + LePtr(current) + (current ? " \"" + LeName(current) + "\"" : ""));
		int i = 0;
		for (MCObjectHandle h = firstV; h != nullptr && i < 1000; h = gSDK->NextObject(h), ++i)
			out.push_back("[" + std::to_string(i) + "] " + LePtr(h) + " " + LeDescribe(h, current));
		return out;
	}

	void LeForEachCb(MCObjectHandle h, CallBackPtr, void* env)
	{
		auto* v = static_cast<std::vector<MCObjectHandle>*>(env);
		v->push_back(h);
	}

	std::vector<std::string> LeForEach()
	{
		std::vector<MCObjectHandle> hs;
		gSDK->ForEachLayer(&LeForEachCb, &hs);
		std::vector<std::string> out;
		for (std::size_t i = 0; i < hs.size(); ++i)
			out.push_back("(" + std::to_string(i) + ") " + LePtr(hs[i]) + " type=" +
						  std::to_string(gSDK->GetObjectTypeN(hs[i])) + " \"" + LeName(hs[i]) +
						  "\"");
		return out;
	}

	int LeOpenDocs()
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		return (int)list.GetSize();
	}

	// --- 刻み -------------------------------------------------------------------
	struct LeState
	{
		std::string events;
		bool nodoc = false;
		long long armedWall = 0;
		long long ticks = 0;
		int step = 0;
		std::vector<std::string> directDump; // メニューの中で最後に辿った列
		bool disarmed = false;
	};
	LeState* gLeState = nullptr;
#if GS_WIN
	UINT_PTR gLeTimer = 0;
	HWND gLeMainWnd = nullptr;

	BOOL CALLBACK LeFindMain(HWND h, LPARAM out)
	{
		DWORD pid = 0;
		::GetWindowThreadProcessId(h, &pid);
		if (pid != ::GetCurrentProcessId() || !::IsWindowVisible(h) ||
			::GetWindow(h, GW_OWNER) != nullptr)
			return TRUE;
		wchar_t cls[16] = {0};
		::GetClassNameW(h, cls, 16);
		if (std::wstring(cls).rfind(L"Afx:", 0) != 0)
			return TRUE;
		*reinterpret_cast<HWND*>(out) = h;
		return FALSE;
	}
	bool LeIdle()
	{
		return gLeMainWnd == nullptr || !::IsWindow(gLeMainWnd) || ::IsWindowEnabled(gLeMainWnd);
	}
#else
	CFRunLoopTimerRef gLeTimer = nullptr;
	bool LeIdle()
	{
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return false;
		const bool idle = ::CFStringCompare(mode, kCFRunLoopDefaultMode, 0) == kCFCompareEqualTo;
		::CFRelease(mode);
		return idle;
	}
#endif

	void LeAppend(const std::string& line)
	{
		if (gLeState == nullptr)
			return;
		std::ofstream out(gLeState->events, std::ios::app);
		if (out)
			out << line << "\n";
	}

	void LeStopTimer()
	{
#if GS_WIN
		if (gLeTimer != 0)
			::KillTimer(nullptr, gLeTimer);
		gLeTimer = 0;
#else
		if (gLeTimer != nullptr)
		{
			::CFRunLoopTimerInvalidate(gLeTimer);
			::CFRelease(gLeTimer);
			gLeTimer = nullptr;
		}
#endif
	}

	void LeDisarm(const std::string& why)
	{
		if (gLeState == nullptr || gLeState->disarmed)
			return;
		gLeState->disarmed = true;
		LeStopTimer();
		LeAppend("disarm wall_ms=" + std::to_string(LeWallMs()) + " why=" + why);
	}

	void LeTickWalk(const char* label)
	{
		LeAppend(std::string("■ 刻みの中で辿る（") + label + "） docs=" + std::to_string(LeOpenDocs()) +
				 " undo=" + (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
		const std::vector<std::string> rows = LeWalk(&LeAppend);
		for (const std::string& r : rows)
			LeAppend("  " + r);
		LeAppend("  件数=" + std::to_string(rows.size()));
		LeAppend("  ForEachLayer を呼ぶ");
		const std::vector<std::string> fe = LeForEach();
		for (const std::string& r : fe)
			LeAppend("  " + r);
		LeAppend("  ForEachLayer 件数=" + std::to_string(fe.size()));
	}

	void LeTick()
	{
		LeState* st = gLeState;
		if (st == nullptr || st->disarmed)
			return;
		++st->ticks;
		const long long now = LeWallMs();
		if (now - st->armedWall > kLeMaxMs)
		{
			LeDisarm("time-limit");
			return;
		}
		if (!LeIdle() || now - st->armedWall < 500)
			return; // 結果ダイアログなどのモーダルが閉じるまで待つ
		const std::string head = "tick " + std::to_string(st->ticks) +
								 " wall_ms=" + std::to_string(now) + " step=" +
								 std::to_string(st->step);
		switch (st->step)
		{
		case 0:
		{
			LeAppend(head);
			LeTickWalk("図面あり。メニューから戻った後");
			// メニューの中の最後の列と比べる（アドレスごと）。
			const std::vector<std::string> again = LeWalk([](const std::string&) {});
			LeAppend(std::string("  メニューの中で辿った列と") +
					 (again == st->directDump ? "一致" : "不一致"));
			if (again != st->directDump)
			{
				for (const std::string& r : st->directDump)
					LeAppend("    menu: " + r);
			}
			st->step = st->nodoc ? 1 : 99;
			break;
		}
		case 1:
		{
			LeAppend(head);
			int docs = LeOpenDocs();
			LeAppend("■ 開いている図面をすべて閉じる（保存しない） docs=" + std::to_string(docs));
			for (int k = 0; k < 20 && docs > 0; ++k)
			{
				const bool r = gSDK->CloseDocument();
				docs = LeOpenDocs();
				LeAppend(std::string("  CloseDocument()=") + (r ? "true" : "false") +
						 " → docs=" + std::to_string(docs));
			}
			st->step = 2;
			break;
		}
		case 2:
			LeAppend(head);
			LeTickWalk("図面 0 枚");
			st->step = 3;
			break;
		case 3:
		{
			LeAppend(head);
			LeAppend("■ OpenDocumentPath(nullptr, false) で新規を開く");
			const bool r = gSDK->OpenDocumentPath(nullptr, false);
			LeAppend(std::string("  →") + (r ? "true" : "false") +
					 " docs=" + std::to_string(LeOpenDocs()));
			st->step = 4;
			break;
		}
		case 4:
			LeAppend(head);
			LeTickWalk("新規を開いた後");
			st->step = 99;
			break;
		default:
			break;
		}
		if (st->step == 99)
		{
			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
			{
				gSDK->EndUndoEvent();
				LeAppend("  開いていた undo イベントを閉じた");
			}
			LeDisarm("done");
		}
	}

#if GS_WIN
	VOID CALLBACK LeWinTick(HWND, UINT, UINT_PTR, DWORD)
	{
		LeTick();
	}
#else
	void LeCFTick(CFRunLoopTimerRef, void*)
	{
		LeTick();
	}
#endif

	std::string LePinSelf()
	{
#if GS_WIN
		HMODULE mod = nullptr;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
		if (::GetModuleHandleExW(flags, (LPCWSTR)&LePinSelf, &mod) == 0)
			return "GetModuleHandleExW(PIN) が失敗した";
		return std::string();
#else
		::Dl_info info;
		if (::dladdr((const void*)&LePinSelf, &info) == 0 || info.dli_fname == nullptr)
			return "dladdr が失敗した";
		if (::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY) == nullptr)
			return "dlopen(RTLD_NOLOAD) が失敗した";
		return std::string();
#endif
	}

	vwprobe::Report* gLeReport = nullptr;
	void LeSay(const std::string& line)
	{
		if (gLeReport != nullptr)
			gLeReport->log(line);
	}

	void LeLogWalk(vwprobe::Report& probe, const std::string& title)
	{
		probe.log("■ " + title);
		const std::vector<std::string> rows = LeWalk(&LeSay);
		for (const std::string& r : rows)
			probe.log("  " + r);
		probe.log("  件数=" + std::to_string(rows.size()));
	}

	MCObjectHandle LeMakeLayer(vwprobe::Report& probe, const char* name, short kind)
	{
		MCObjectHandle h = gSDK->GetNamedLayer(name);
		if (h != nullptr)
		{
			probe.log(std::string("  ") + name + " は既にある（作らない）");
			return h;
		}
		h = gSDK->CreateLayer(name, kind);
		probe.log(std::string("  CreateLayer(\"") + name + "\", " + std::to_string(kind) +
				  ") → " + LePtr(h));
		return h;
	}

	void LeSetScale(vwprobe::Report& probe, MCObjectHandle h, double want)
	{
		if (h == nullptr)
			return;
		double_gs before = -1, after = -1;
		gSDK->GetLayerScaleN(h, before);
		gSDK->SetLayerScaleN(h, want);
		gSDK->GetLayerScaleN(h, after);
		probe.log("  \"" + LeName(h) + "\" GetLayerScaleN " + LeNum(before) + " → SetLayerScaleN(" +
				  LeNum(want) + ") → " + LeNum(after));
	}
} // namespace

VW_PROBE("layer-enum", "レイヤの列挙", "種類・順序・現在・縮尺・刻み・図面0枚")
{
	gLeReport = &probe;
	{
		std::error_code ec;
		if (std::filesystem::exists(LeEventsPath(), ec))
		{
			probe.log("■ 前の回の刻みの書き溜め（" + LeEventsPath() + "）");
			std::ifstream in(LeEventsPath());
			std::string line;
			while (std::getline(in, line))
				probe.log("  | " + line);
			probe.log("");
		}
	}
	if (gLeState != nullptr && !gLeState->disarmed)
	{
		probe.log("この像では刻みがまだ動いている。終わってから走らせ直す");
		gLeReport = nullptr;
		return;
	}

	probe.log("build=" + std::string(VW_PAYLOAD_BUILD_ID) + " docs=" + std::to_string(LeOpenDocs()));
	probe.log("ELayerType: kLayerDesign=" + std::to_string((int)kLayerDesign) +
			  " kLayerSheet=" + std::to_string((int)kLayerSheet) +
			  " kGuestLayerDesign=" + std::to_string((int)kGuestLayerDesign));
	MCObjectHandle original = gSDK->GetCurrentLayer();
	LeLogWalk(probe, "走らせる前の列");

	probe.log("■ 足す（デザイン: 低→高の順に作る。シート: 30→10→20 の順に作る）");
	MCObjectHandle dLow = LeMakeLayer(probe, "LE-D-low", kLayerDesign);
	MCObjectHandle dHigh = LeMakeLayer(probe, "LE-D-high", kLayerDesign);
	if (dHigh != nullptr)
	{
		VWLayerObj(dHigh).SetElevation(3000);
		probe.log("  LE-D-high の高さ → " + LeNum(VWLayerObj(dHigh).GetElevation()));
	}
	if (dLow != nullptr)
	{
		VWLayerObj(dLow).SetElevation(-1000);
		probe.log("  LE-D-low の高さ → " + LeNum(VWLayerObj(dLow).GetElevation()));
	}
	LeSetScale(probe, dLow, 50);
	LeSetScale(probe, dHigh, 200);
	MCObjectHandle s30 = LeMakeLayer(probe, "LE-30", kLayerSheet);
	MCObjectHandle s10 = LeMakeLayer(probe, "LE-10", kLayerSheet);
	MCObjectHandle s20 = LeMakeLayer(probe, "LE-20", kLayerSheet);
	LeSetScale(probe, s20, 50); // シートレイヤへ縮尺を書いたら
	(void)s30;
	LeLogWalk(probe, "足した後の列");

	if (s10 != nullptr)
	{
		probe.log("■ シート LE-10 にビューポートを作る（列に何か増えるか）");
		MCObjectHandle vp = gSDK->CreateViewport(s10);
		probe.log("  CreateViewport → " + LePtr(vp) +
				  (vp ? " type=" + std::to_string(gSDK->GetObjectTypeN(vp)) : ""));
		LeLogWalk(probe, "ビューポートを作った後の列");
	}

	probe.log("■ ForEachLayer の並び");
	const std::vector<std::string> fe = LeForEach();
	for (const std::string& r : fe)
		probe.log("  " + r);
	probe.log("  件数=" + std::to_string(fe.size()));

	if (s20 != nullptr)
	{
		probe.log("■ SetCurrentLayer(LE-20)（シート）");
		gSDK->SetCurrentLayer(s20);
		MCObjectHandle cur = gSDK->GetCurrentLayer();
		probe.log("  GetCurrentLayer → " + LePtr(cur) + " \"" + (cur ? LeName(cur) : "") +
				  "\" ==LE-20: " + (cur == s20 ? "yes" : "no"));
		LeLogWalk(probe, "シートが現在のときの列");
	}
	if (dHigh != nullptr)
	{
		probe.log("■ SetCurrentLayer(LE-D-high)（デザイン）");
		gSDK->SetCurrentLayer(dHigh);
		MCObjectHandle cur = gSDK->GetCurrentLayer();
		probe.log("  GetCurrentLayer → " + LePtr(cur) + " \"" + (cur ? LeName(cur) : "") +
				  "\" ==LE-D-high: " + (cur == dHigh ? "yes" : "no"));
	}
	if (original != nullptr)
	{
		gSDK->SetCurrentLayer(original);
		probe.log("  現在のレイヤを元（\"" + LeName(original) + "\"）へ戻した");
	}
	LeLogWalk(probe, "最後の列（刻みと比べる）");
	const std::vector<std::string> finalDump = LeWalk([](const std::string&) {});

	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
	{
		gSDK->EndUndoEvent();
		probe.log("開いていた undo イベントを閉じた");
	}

	// --- 刻みを仕掛ける ---
	const std::string trouble = LePinSelf();
	if (!trouble.empty())
	{
		probe.fail("本体をピン留めできなかったので刻みは仕掛けない（" + trouble + "）");
		gLeReport = nullptr;
		return;
	}
#if GS_WIN
	::EnumWindows(&LeFindMain, reinterpret_cast<LPARAM>(&gLeMainWnd));
#endif
	delete gLeState;
	gLeState = new LeState();
	gLeState->events = LeEventsPath();
	gLeState->nodoc = LeReadFirstLine(LeModePath()) == "nodoc";
	gLeState->armedWall = LeWallMs();
	gLeState->directDump = finalDump;
	{
		std::error_code ec;
		std::filesystem::remove(gLeState->events, ec);
	}
	LeAppend("arm wall_ms=" + std::to_string(gLeState->armedWall) + " build=" +
			 VW_PAYLOAD_BUILD_ID + " nodoc=" + (gLeState->nodoc ? "yes" : "no"));
#if GS_WIN
	gLeTimer = ::SetTimer(nullptr, 0, (UINT)kLeTickMs, &LeWinTick);
	const bool armed = gLeTimer != 0;
#else
	const CFAbsoluteTime t = (CFAbsoluteTime)kLeTickMs / 1000.0;
	gLeTimer = ::CFRunLoopTimerCreate(kCFAllocatorDefault, ::CFAbsoluteTimeGetCurrent() + t, t, 0, 0,
									  &LeCFTick, nullptr);
	const bool armed = gLeTimer != nullptr;
	if (armed)
		::CFRunLoopAddTimer(::CFRunLoopGetMain(), gLeTimer, kCFRunLoopCommonModes);
#endif
	if (!armed)
	{
		probe.fail("タイマーを仕掛けられなかった");
		LeDisarm("arm-failed");
	}
	else
	{
		probe.log(std::string("■ 刻みを仕掛けた（nodoc=") + (gLeState->nodoc ? "yes" : "no") +
				  "）。結果は " + LeEventsPath() + " に書く（次に走らせたときにも報告する）");
	}
	gLeReport = nullptr;
}
