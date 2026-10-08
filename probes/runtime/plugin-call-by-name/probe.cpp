//
//	probes/runtime/plugin-call-by-name/probe.cpp
//
//	[issue #217] プラグインから、ほかのプラグインの機能を**名前で**呼び出せるかを実測する。
//
//	呼ぶ側はこのプローブ（＝入れ替わる**本体モジュール**の中）。呼ばれる側は殻が登録した
//	プラグインライブラリルーチン（`plugin/src/ProbeLibrary.h`。PR #218 で main に入った）。
//	**別々の dylib / dll で、互いのシンボルを一切知らない**——間を取り持つのは VW の
//	名前解決だけである。
//
//	測るのは 4 つ。
//	  1. `ISDK::CallPluginLibrary` — 名前で呼べるか／引数と結果／相手が居ないとき
//	  2. `ISDK::DoMenuName`        — メニューコマンドを内部名で起動できるか
//	  3. `ISDK::ExternalNameToID`  — 相手の有無を事前に確かめられるか
//	  4. OS タイマーの中から 1 を呼べるか（CLI プラグインの受け口はそこで動く）
//
//	**殻のヘッダは include しない**（公開ビルドの本体には無い。probes/runtime/README.md）。
//	ルーチンの名前は綴りを書き写してある——`plugin/src/ProbeLibrary.h` を直すときは
//	ここも一緒に直すこと。
//

#include "Probe.h"

#include <cstring>
#include <string>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
	// 殻（plugin/src/ProbeLibrary.h）が登録している名前の写し。
	constexpr const char* kProbeEcho = "VwSdkProbes_Echo";
	constexpr const char* kProbeSum = "VwSdkProbes_Sum";
	constexpr const char* kProbeOut = "VwSdkProbes_Out";

	// 居ないはずの相手。**実在しない名前を 1 つ決めておく**のが「相手が居ないときの
	// 振る舞い」の測り方になる。
	constexpr const char* kProbeMissingRoutine = "VwSdkProbes_NoSuchRoutine_217";
	constexpr const char* kProbeMissingCommand = "VwSdkProbes_NoSuchCommand_217";

	using VwArgTable = VWFC::PluginSupport::VWPluginLibraryArgTable;

	std::string Utf8Of(const TXString& s)
	{
		const char* p = static_cast<const char*>(s);
		return p != nullptr ? std::string(p) : std::string();
	}

	std::string YesNo(bool v)
	{
		return v ? "yes" : "no";
	}

	// 結果の欄がどう埋まったかを、**型（argType）ごと**に読んで文字にする。
	// 失敗した呼び出しでは `kNullArgType` のまま残るので、**型を見てから読む**
	// （VWPluginLibraryArgument の getter は型が合わないと VWFC_ASSERT を踏む）。
	std::string DescribeResult(const PluginLibraryArg& arg)
	{
		std::string out = "argType=" + std::to_string(static_cast<int>(arg.argType));
		switch (arg.argType)
		{
		case kNullArgType:
			out += "(null) 値なし";
			break;
		case kStringArgType:
		case kStringVarArgType:
		case kCharDynarrayArgType:
		case kCharDynarrayVarArgType:
			out += "(string) \"" + Utf8Of(arg.strValue) + "\"";
			break;
		case kLongArgType:
		case kLongVarArgType:
			out += "(long) " + std::to_string(static_cast<long>(arg.longValue));
			break;
		case kIntegerArgType:
		case kIntegerVarArgType:
			out += "(integer) " + std::to_string(static_cast<int>(arg.intValue));
			break;
		case kBooleanArgType:
		case kBooleanVarArgType:
			out += std::string("(boolean) ") + (arg.boolValue ? "true" : "false");
			break;
		case kVoidPtr:
			out += std::string("(voidPtr) ") + (arg.voidData != nullptr ? "非 nil" : "nil");
			break;
		default:
			out += " 未対応の型なので値は読まない";
			break;
		}
		return out;
	}

	// -----------------------------------------------------------------------
	// 1 本呼んで、戻り値と結果欄を記録する（引数は呼び出し側が組んでから渡す）。
	Boolean CallAndReport(vwprobe::Report& probe, const char* routineName, VwArgTable& table)
	{
		PluginLibraryArgTable* raw = table;
		const Boolean ok = gSDK->CallPluginLibrary(routineName, raw, 0);

		probe.log(std::string("  CallPluginLibrary(\"") + routineName + "\") -> " + YesNo(ok != 0));
		probe.log("    結果欄: " + DescribeResult(raw->functionResult));
		return ok;
	}

	// -----------------------------------------------------------------------
	// タイマーの刻みの中から CallPluginLibrary を呼ぶ。結果はここへ書き溜める
	// （**同じ呼び出しの中で閉じる**ので、モジュールをピン留めする必要は無い）。
	struct ProbeTimerState
	{
		bool fired = false;
		bool called = false;
		bool callOk = false;
		std::string result;
	};

	ProbeTimerState gProbeTimer; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

	void ProbeTimerTick()
	{
		if (gProbeTimer.fired)
			return; // 1 回だけ測る
		gProbeTimer.fired = true;

		VwArgTable table;
		table.GetArgument(0).SetArgString("from-timer");
		PluginLibraryArgTable* raw = table;

		gProbeTimer.callOk = gSDK->CallPluginLibrary(kProbeEcho, raw, 0) != 0;
		gProbeTimer.called = true;
		gProbeTimer.result = DescribeResult(raw->functionResult);
	}

#if GS_MAC
	void ProbeTimerCallbackCF(CFRunLoopTimerRef /*timer*/, void* /*info*/)
	{
		ProbeTimerTick();
	}
#else
	void CALLBACK ProbeTimerCallbackWin(HWND /*hwnd*/, UINT /*msg*/, UINT_PTR /*id*/,
										DWORD /*tick*/)
	{
		ProbeTimerTick();
	}
#endif

	// 刻みが届くまでイベントを回す。**mac は「短く刻んで何度も」でなければ回らない**
	// （1 回を長く取ると即座に戻る。Findings「周期実行と通知」4）。
	void PumpUntilTimerFired()
	{
#if GS_MAC
		CFRunLoopTimerRef timer =
			CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.1, 0, 0, 0,
								 &ProbeTimerCallbackCF, nullptr);
		if (timer == nullptr)
			return;
		// **共通モードへ入れる。** 既定モードだけに入れると VW のループの局面によって
		// 長時間止まる（同 6）。
		CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopCommonModes);
		for (int i = 0; i < 40 && !gProbeTimer.fired; ++i)
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
		CFRunLoopTimerInvalidate(timer);
		CFRelease(timer);
#else
		const UINT_PTR id = ::SetTimer(nullptr, 0, 100, &ProbeTimerCallbackWin);
		if (id == 0)
			return;
		// Windows は自分で PeekMessage を回せばその場で配られる（同 4）。
		for (int i = 0; i < 400 && !gProbeTimer.fired; ++i)
		{
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}
			::Sleep(5);
		}
		::KillTimer(nullptr, id);
#endif
	}
} // namespace

VW_PROBE("plugin-call-by-name", "ほかのプラグインの機能を名前で呼べるか",
		 "CallPluginLibrary / DoMenuName / ExternalNameToID を実測する（#217）")
{
	probe.log("呼ぶ側 = この本体モジュール（.vwpayload）");
	probe.log("呼ばれる側 = 殻が登録したプラグインライブラリルーチン（別モジュール）");
	probe.log(std::string("走り出しの undo: building=") +
			  YesNo(gSDK->IsCurrentlyBuildingAnUndoEvent() != 0));

	// =====================================================================
	probe.log("");
	probe.log("=== 1. 相手が居ないとき（存在しないルーチン名） ===");
	{
		VwArgTable table;
		table.GetArgument(0).SetArgString("x");
		const Boolean ok = CallAndReport(probe, kProbeMissingRoutine, table);
		probe.log(std::string("  → 落ちずに戻った。戻り値で失敗を知れる: ") + YesNo(ok == 0));
	}

	// =====================================================================
	probe.log("");
	probe.log("=== 2. 文字列を渡して文字列を受け取る（VwSdkProbes_Echo） ===");
	bool echoReached = false;
	{
		VwArgTable table;
		table.GetArgument(0).SetArgString("i217");
		const Boolean ok = CallAndReport(probe, kProbeEcho, table);
		PluginLibraryArgTable* raw = table;
		const bool gotEcho = raw->functionResult.argType == kStringArgType &&
							 Utf8Of(raw->functionResult.strValue) == "echo:i217";
		echoReached = ok != 0 && gotEcho;
		probe.log(std::string("  期待どおり \"echo:i217\" が返ったか: ") + YesNo(gotEcho));
		if (!echoReached)
			probe.fail("殻が登録したルーチンを名前で呼べなかった（#217 の本題）");
	}

	// =====================================================================
	probe.log("");
	probe.log("=== 3. 数値を複数渡して数値を受け取る（VwSdkProbes_Sum） ===");
	{
		VwArgTable table;
		table.GetArgument(0).SetArgLong(40);
		table.GetArgument(1).SetArgLong(2);
		CallAndReport(probe, kProbeSum, table);
		PluginLibraryArgTable* raw = table;
		probe.log(std::string("  42 が返ったか: ") +
				  YesNo(raw->functionResult.argType == kLongArgType &&
						raw->functionResult.longValue == 42));
	}

	// =====================================================================
	probe.log("");
	probe.log("=== 4. VAR（出力）引数で返せるか（VwSdkProbes_Out） ===");
	{
		VwArgTable table;
		table.GetArgument(0).SetArgString("i217");
		PluginLibraryArgTable* raw = table;
		// **VAR の文字列引数は VWPluginLibraryArgTable に setter が無い**
		// （SetArgString は kStringArgType にしてしまう）。生の argType を自分で立てる。
		raw->args[1].argType = kStringVarArgType;
		// VAR の数値は getter が型を立ててくれる（kNullArgType のときだけ）。
		table.GetArgument(2).GetArgLongVar() = 0;

		CallAndReport(probe, kProbeOut, table);
		probe.log("    args[1]: " + DescribeResult(raw->args[1]));
		probe.log("    args[2]: " + DescribeResult(raw->args[2]));
	}

	// =====================================================================
	probe.log("");
	probe.log("=== 5. VW 同梱のレガシー経路（IFC_QueryInterface。参考） ===");
	{
		// IIFCSupport.h が使っている形。**あのヘッダは `_VWFC_FOR_VW125x` の中**で、
		// いまのビルドでは誰も定義していない＝死んだコードなので、VW 2026 でまだ
		// 応えるかは分からない。応えれば「ほかのプラグインのルーチンも名前で引ける」
		// 傍証になる（IID は何にも一致しないものを渡す）。
		const VWIID iidNothing = {
			0x00000217, 0x0217, 0x0217, {0x02, 0x17, 0x02, 0x17, 0x02, 0x17, 0x02, 0x17}};
		VwArgTable table;
		PluginLibraryArgTable* raw = table;
		raw->args[0].argType = kVoidPtr;
		raw->args[0].voidData = static_cast<void*>(gCBP);
		raw->args[1].argType = kVoidPtr;
		raw->args[1].voidData = const_cast<void*>(static_cast<const void*>(&iidNothing));
		CallAndReport(probe, "IFC_QueryInterface", table);
	}

	// =====================================================================
	probe.log("");
	probe.log("=== 6. ISDK::DoMenuName（メニューコマンドを内部名で起動） ===");
	{
		const short missing = gSDK->DoMenuName(kProbeMissingCommand, 0);
		probe.log(std::string("  DoMenuName(\"") + kProbeMissingCommand + "\", 0) -> " +
				  std::to_string(static_cast<int>(missing)));

		// 実在するコマンドで試す。**undo イベントは開かない**（probes/runtime/README.md）
		// ので、ここで作るものは取り消しスタックに載らない＝Undo では消えない
		// （Findings「Undo」で確定済み）。消えなければ「届いたが、こちらの作ったものには
		// 掛からない」、消えれば「それ以上のことが起きた」と読める。
		const MCObjectHandle locus = gSDK->CreateLocus(WorldPt(0, 0));
		probe.log(std::string("  目印の locus を作った: ") + YesNo(locus != nil));

		const short undoRet = gSDK->DoMenuName("Undo", 0);
		probe.log("  DoMenuName(\"Undo\", 0) -> " + std::to_string(static_cast<int>(undoRet)));
		probe.log(std::string("  locus はまだ在るか: ") +
				  YesNo(locus != nil && gSDK->GetObjectTypeN(locus) != 0));
		probe.log(std::string("  呼んだ後の undo: building=") +
				  YesNo(gSDK->IsCurrentlyBuildingAnUndoEvent() != 0));
	}

	// =====================================================================
	probe.log("");
	probe.log("=== 7. ISDK::ExternalNameToID（相手の有無を事前に確かめられるか） ===");
	{
		probe.log("  ExternalNameToID(\"VwSdkProbes\") -> " +
				  std::to_string(static_cast<long>(gSDK->ExternalNameToID("VwSdkProbes"))));
		probe.log("  ExternalNameToID(\"NoSuchExternal217\") -> " +
				  std::to_string(static_cast<long>(gSDK->ExternalNameToID("NoSuchExternal217"))));
	}

	// =====================================================================
	probe.log("");
	probe.log("=== 8. OS タイマーの刻みの中から呼べるか ===");
	{
		PumpUntilTimerFired();
		probe.log(std::string("  刻みが届いたか: ") + YesNo(gProbeTimer.fired));
		if (gProbeTimer.called)
		{
			probe.log(std::string("  刻みの中の CallPluginLibrary -> ") +
					  YesNo(gProbeTimer.callOk));
			probe.log("    結果欄: " + gProbeTimer.result);
		}
		else
		{
			probe.log("  刻みが届かなかったので測れていない（回し方の問題）");
		}
	}

	probe.log("");
	probe.log(std::string("終わりの undo: building=") +
			  YesNo(gSDK->IsCurrentlyBuildingAnUndoEvent() != 0));
}
