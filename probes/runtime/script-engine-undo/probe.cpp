//
//	probes/runtime/script-engine-undo/probe.cpp
//
//	[issue #36] ISDK / VWFC に DoMenuTextByName 相当の直接呼び出しは無いことは
//	[issue #27] で確定した。一方、VectorScript / Python の**スクリプトそのものを
//	実行させる** API（VectorWorks::Scripting::IVectorScriptEngine::ExecuteScript /
//	VectorWorks::Scripting::IPythonScriptEngine::ExecuteScript）は SDK に実在する
//	（sdk-grep で確認済み・実装例あり【ヘッダ根拠】。詳細は Findings/Undo.md）。
//	そのスクリプトの中で `DoMenuTextByName('Undo', 0)` を呼べば、間接的にメニューの
//	「取り消し」を起動できるはず——というのが #36 の仮説。
//
//	ヘッダだけでは分からない実機の挙動を確かめる:
//	  A) 素の状態（プローブ自身のコマンド実行中。他に undo イベントを開いていない）で
//	     ExecuteScript 経由の DoMenuTextByName('Undo', 0) が実際に直前の操作を戻すか。
//	     VectorScript エンジンと Python エンジンの両方で試す。
//	  B) 自分がまだ閉じていない undo イベントを開いている最中に呼んだらどうなるか
//	     ——Findings/Undo.md の「半端な記録を取り消すと図面が壊れる」パターンに、
//	     このスクリプト経由の呼び出しが当てはまるかどうかを見るのが本題。
//	  C) 連続で 2 回呼んで複数段戻せるか。
//
//	図面を壊す前提で書く（probes/runtime/README.md）。新規の空図面で走らせること。
//

#include "Probe.h"

#include <string>

namespace
{
	using VectorWorks::Scripting::IID_PythonScriptEngine;
	using VectorWorks::Scripting::IID_VectorScriptEngine;
	using VectorWorks::Scripting::IPythonScriptEnginePtr;
	using VectorWorks::Scripting::IVectorScriptEnginePtr;

	std::string DescribeError(VCOMError err)
	{
		return std::to_string(static_cast<long long>(err)) +
			   (VCOM_SUCCEEDED(err) ? " (success)" : " (failure)");
	}

	std::string DescribeBuilding()
	{
		return gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no";
	}

	// 指定コンテナ（ここではレイヤ）の「先頭の子オブジェクト」を返す。ISDK に
	// VectorScript の FInLayer に相当する専用 API は無く（sdk-grep で確認）、汎用の
	// NextObject を「コンテナのハンドルを渡すと最初の子を返す」という VWFC の慣例
	// （VWDocument::GetDrawingHeaderFristMember + NextObject(h) がトップレベルで
	// 同じ形をしている）に沿って使う。予想が外れて別のものを指していても、
	// nil か非 nil かのログ自体は「矩形がまだ残っているか」の手がかりになる。
	MCObjectHandle FirstChildOf(MCObjectHandle container)
	{
		return gSDK->NextObject(container);
	}

	std::string DescribeHandle(MCObjectHandle h)
	{
		return h == nil ? "(nil)" : "(non-nil)";
	}

	// レイヤを 1 枚作り、矩形を 1 個描く。戻り値は作ったレイヤ（失敗したら nil）。
	MCObjectHandle SetUpLayerWithOneRect(vwprobe::Report& probe, const TXString& layerName)
	{
		MCObjectHandle layer = gSDK->CreateLayer(layerName, kLayerDesign);
		if (layer == nil)
		{
			probe.fail(std::string("CreateLayer(") + static_cast<const char*>(layerName) +
					   ") が nil を返した");
			return nil;
		}
		gSDK->SetCurrentLayer(layer);
		MCObjectHandle rect = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		if (rect == nil)
		{
			probe.fail(std::string("CreateRectangle(") + static_cast<const char*>(layerName) +
					   ") が nil を返した");
			return nil;
		}
		return layer;
	}
} // namespace

VW_PROBE("script-engine-undo", "スクリプトエンジン経由で DoMenuTextByName('Undo', 0) を呼ぶ",
		 "IVectorScriptEngine / IPythonScriptEngine の ExecuteScript の中から Undo を起動し、"
		 "戻り値・実際に取り消されるか・半端な undo イベント中に呼んだ場合を確かめる")
{
	IVectorScriptEnginePtr vsEngine(IID_VectorScriptEngine);
	if (!vsEngine)
	{
		probe.fail("IVectorScriptEngine シングルトンが取れなかった");
		return;
	}
	IPythonScriptEnginePtr pyEngine(IID_PythonScriptEngine);
	if (!pyEngine)
	{
		probe.fail("IPythonScriptEngine シングルトンが取れなかった");
		return;
	}
	probe.log("両エンジンのシングルトンを取得できた");

	// --- A-1) VectorScript エンジンで素の状態から呼ぶ ---
	{
		MCObjectHandle layer = SetUpLayerWithOneRect(probe, "probe-script-undo-a1");
		if (layer == nil)
			return;
		probe.log("[A1] VectorScript / 素の状態: 矩形を 1 個描いた。呼び出し前の building=" +
				  DescribeBuilding() + " / レイヤ先頭子: " + DescribeHandle(FirstChildOf(layer)));

		VCOMError err = vsEngine->ExecuteScript("DoMenuTextByName('Undo', 0);");
		probe.log("[A1] ExecuteScript(VectorScript) の戻り値: " + DescribeError(err) +
				  " / 呼び出し後の building=" + DescribeBuilding() +
				  " / レイヤ先頭子: " + DescribeHandle(FirstChildOf(layer)) +
				  "（矩形が消えていれば取り消しが効いたと判断できる）");
	}

	// --- A-2) Python エンジンで素の状態から呼ぶ（vs.DoMenuTextByName 経由） ---
	{
		MCObjectHandle layer = SetUpLayerWithOneRect(probe, "probe-script-undo-a2");
		if (layer == nil)
			return;
		probe.log("[A2] Python / 素の状態: 矩形を 1 個描いた。呼び出し前の building=" +
				  DescribeBuilding() + " / レイヤ先頭子: " + DescribeHandle(FirstChildOf(layer)));

		VCOMError err = pyEngine->ExecuteScript("import vs\nvs.DoMenuTextByName('Undo', 0)\n");
		probe.log("[A2] ExecuteScript(Python) の戻り値: " + DescribeError(err) +
				  " / 呼び出し後の building=" + DescribeBuilding() +
				  " / レイヤ先頭子: " + DescribeHandle(FirstChildOf(layer)) +
				  "（矩形が消えていれば取り消しが効いたと判断できる）");
	}

	// --- B) 自分がまだ閉じていない undo イベントを開いている最中に呼ぶ ---
	//     Findings/Undo.md の「半端な記録を取り消すと図面が壊れる」パターンに、この
	//     スクリプト経由の呼び出しが当てはまるかどうかを見るのが本題。図面を壊す
	//     前提でそのまま呼ぶ（probes/runtime/README.md）。
	{
		MCObjectHandle layer = SetUpLayerWithOneRect(probe, "probe-script-undo-b");
		if (layer == nil)
			return;

		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent("probe script-engine-undo B");
		probe.log("[B] 自分の undo イベントを開いた（まだ閉じていない）。building=" +
				  DescribeBuilding() + "。ここから先で図面が壊れても、この行までは残る");

		VCOMError err = vsEngine->ExecuteScript("DoMenuTextByName('Undo', 0);");
		probe.log("[B] 開いたままの undo イベントの中から ExecuteScript(VectorScript) を呼んだ。"
				  "戻り値: " +
				  DescribeError(err) + " / 呼び出し後の building=" + DescribeBuilding() +
				  " / レイヤ先頭子: " + DescribeHandle(FirstChildOf(layer)));

		// 自分のイベントがまだ開いていれば、素材を登録していないので空のまま残さず
		// 閉じる（EndAndRemoveUndoEvent）。ExecuteScript 側が既に閉じていた場合は
		// building=no のはずなので、その分岐もログに残す。
		if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		{
			Boolean removed = gSDK->EndAndRemoveUndoEvent();
			probe.log(std::string("[B] 後始末: EndAndRemoveUndoEvent() = ") +
					  (removed ? "true" : "false"));
		}
		else
		{
			probe.log("[B] 後始末: 呼び出し後すでに building=no だったので "
					  "EndAndRemoveUndoEvent は呼ばなかった");
		}
	}

	// --- C) 連続で 2 回呼んで複数段戻せるか ---
	{
		MCObjectHandle layer = gSDK->CreateLayer("probe-script-undo-c", kLayerDesign);
		if (layer == nil)
		{
			probe.fail("CreateLayer(C) が nil を返した");
			return;
		}
		gSDK->SetCurrentLayer(layer);
		MCObjectHandle rect1 = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		MCObjectHandle rect2 = gSDK->CreateRectangle(WorldRect(20, 10, 30, 0));
		if (rect1 == nil || rect2 == nil)
		{
			probe.fail("CreateRectangle(C) が nil を返した");
			return;
		}
		probe.log("[C] 矩形を 2 個、別々の操作として描いた");

		VCOMError err1 = vsEngine->ExecuteScript("DoMenuTextByName('Undo', 0);");
		probe.log("[C] 1 回目: 戻り値 " + DescribeError(err1) +
				  " / レイヤ先頭子: " + DescribeHandle(FirstChildOf(layer)));

		VCOMError err2 = vsEngine->ExecuteScript("DoMenuTextByName('Undo', 0);");
		probe.log("[C] 2 回目: 戻り値 " + DescribeError(err2) +
				  " / レイヤ先頭子: " + DescribeHandle(FirstChildOf(layer)) +
				  "（1 回目で矩形 2 が、2 回目で矩形 1 も消えていれば複数段戻せている）");
	}

	probe.log("完了");
}
