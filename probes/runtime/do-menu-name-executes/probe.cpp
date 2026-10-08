//
//	probes/runtime/do-menu-name-executes/probe.cpp
//
//	[issue #217] `ISDK::DoMenuName` が本当にメニューコマンドを**実行する**かを実測する。
//
//	1 周目（プローブ `plugin-call-by-name`）で取れたのは「**名前解決は働く**」までだった
//	——在る名前で `0`、居ない名前で `-3` が返る。だが**走ったことの確証にはならない**:
//	新規の空図面では取り消すものが無く、undo イベントの外で作った目印が残るのは
//	[Findings「Undo」](../../../Findings/Undo.md) どおりの当然の結果だからである。
//
//	そこで**取り消しスタックに確実に 1 つ積んでから**呼ぶ。積むのはスクリプトエンジン
//	——`ExecuteScript` で図形を作ると**スクリプトエンジンが undo イベントを開いて積み**、
//	戻った時点で `IsCurrentlyBuildingAnUndoEvent()` が `yes` になる（同上で実測済み）。
//	**プローブは自分では undo イベントを開かない**（probes/runtime/README.md）ので、
//	積む役はスクリプトエンジンに任せる形になる。
//
//	読み方: 積んだレイヤが `DoMenuName("Undo", 0)` のあとに**消えていれば、実行された**。
//	残っていれば「名前は引けるが実行はされない（か、別のものが取り消された）」である。
//

#include "Probe.h"

#include <string>

namespace
{
	// 取り消しの的。**この図面に他で使われていない名前**にする（残っていたら
	// 前回の走行の残りなので、そこで打ち切る）。
	constexpr const char* kUndoTargetLayer = "i217-undo-target";

	// 1 周目と同じ「居ない相手」。戻り値の対照に使う。
	constexpr const char* kMissingCommand = "VwSdkProbes_NoSuchCommand_217";

	std::string YesNo(bool v)
	{
		return v ? "yes" : "no";
	}

	bool UndoBuilding()
	{
		return gSDK->IsCurrentlyBuildingAnUndoEvent() != 0;
	}

	bool TargetLayerExists()
	{
		return gSDK->GetNamedLayer(kUndoTargetLayer) != nil;
	}
} // namespace

VW_PROBE("do-menu-name-executes", "DoMenuName は本当にコマンドを実行するか",
		 "取り消しスタックに 1 つ積んでから DoMenuName(\"Undo\", 0) を呼ぶ（#217）")
{
	using namespace VectorWorks::Scripting;

	probe.log(std::string("走り出しの undo: building=") + YesNo(UndoBuilding()));

	// ---------------------------------------------------------------------
	probe.log("");
	probe.log("=== 0. 前回の残りが無いことを確かめる ===");
	if (TargetLayerExists())
	{
		probe.fail(std::string("レイヤ \"") + kUndoTargetLayer +
				   "\" が既に在る。新規の空図面で走らせ直してください");
		return;
	}
	probe.log(std::string("  レイヤ \"") + kUndoTargetLayer + "\" は無い: ok");

	// ---------------------------------------------------------------------
	probe.log("");
	probe.log("=== 1. 居ないコマンドの戻り値（対照） ===");
	{
		const short ret = gSDK->DoMenuName(kMissingCommand, 0);
		probe.log(std::string("  DoMenuName(\"") + kMissingCommand + "\", 0) -> " +
				  std::to_string(static_cast<int>(ret)));
	}

	// ---------------------------------------------------------------------
	probe.log("");
	probe.log("=== 2. スクリプトエンジンに取り消しスタックへ 1 つ積ませる ===");
	{
		VCOMPtr<IVectorScriptEngine> engine(IID_VectorScriptEngine);
		if (engine == nullptr)
		{
			probe.fail("IVectorScriptEngine を取得できなかった");
			return;
		}

		// **TXString の連結に頼らない**（`+` の多重定義が手元で確かめられないため）。
		const std::string script = std::string("Layer('") + kUndoTargetLayer + "');";
		const VCOMError err = engine->ExecuteScript(TXString(script.c_str()));
		probe.log(std::string("  ExecuteScript(\"Layer('") + kUndoTargetLayer +
				  "');\") -> VCOMError=" + std::to_string(static_cast<long>(err)));
	}

	const bool made = TargetLayerExists();
	probe.log(std::string("  レイヤができたか: ") + YesNo(made));
	// Findings「Undo」: スクリプトエンジンは undo イベントを開いたまま返す。
	probe.log(std::string("  積んだ後の undo: building=") + YesNo(UndoBuilding()));
	if (!made)
	{
		probe.fail("スクリプトがレイヤを作れなかったので、積めていない（この先は測れない）");
		return;
	}

	// ---------------------------------------------------------------------
	probe.log("");
	probe.log("=== 3. DoMenuName(\"Undo\", 0) を呼ぶ ===");
	{
		const short ret = gSDK->DoMenuName("Undo", 0);
		probe.log("  DoMenuName(\"Undo\", 0) -> " + std::to_string(static_cast<int>(ret)));
	}

	const bool stillThere = TargetLayerExists();
	probe.log(std::string("  レイヤはまだ在るか: ") + YesNo(stillThere));
	probe.log(std::string("  呼んだ後の undo: building=") + YesNo(UndoBuilding()));

	probe.log("");
	if (stillThere)
	{
		// **これも知見**（「名前は引けるが実行はされない」）。fail にはしない——
		// 測れなかったわけではなく、答えが出ている。
		probe.log("=> 判定: **取り消しは掛からなかった**。名前は引けるが実行はされない");
		probe.log("   （または、積んだものとは別のものが取り消された）");
	}
	else
	{
		probe.log("=> 判定: **DoMenuName は本当にコマンドを実行する**");
		probe.log("   （積んだレイヤが消えた＝取り消しが実際に走った）");
	}

	// 後片付け。取り消しが掛からなかったときは的が残っているので、消しておく
	// （次に走らせる人が手順 0 で止まらないように）。
	if (stillThere)
	{
		const MCObjectHandle layer = gSDK->GetNamedLayer(kUndoTargetLayer);
		if (layer != nil)
		{
			gSDK->DeleteObject(layer, true);
			probe.log(std::string("後片付け: 的のレイヤを消した。残っているか: ") +
					  YesNo(TargetLayerExists()));
		}
	}
}
