//
//	probes/runtime/script-engine-undo/probe.cpp
//
//	[issue #39] スクリプトエンジン経由（IVectorScriptEngine / IPythonScriptEngine の
//	ExecuteScript）で DoMenuTextByName('Undo', 0) を呼んだときの挙動を確かめる
//	（issue #36 / Findings/Undo.md「間接経路」節の続き）。ヘッダだけでは分からない:
//
//	  1) ExecuteScript は同期的か（戻ったときにはもう Undo が効いているか）。
//	  2) 自分のプラグインコマンドが走っている最中に呼べるか。特に、自分がまだ
//	     EndUndoEvent していない undo イベントを開いている最中に呼んだら
//	     どうなるか（Findings/Undo.md 冒頭の「半端な記録を取り消すと図面が壊れる」
//	     パターンに該当するか）。
//	  3) 失敗（構文エラー等）を判別できるか。CompileScript と比べる。
//	  4) 連続で 2 回呼んで複数段戻せるか。
//
//	マーカーには（delete-layer プローブと同じ流儀で）新規デザインレイヤを使う。
//	「直前の操作で作ったレイヤが Undo で消えるか」を見れば、実行有無・同期性が
//	名前の存在チェックだけで判定できる。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Scripting/IPythonScriptEngine.h"
#include "Interfaces/VectorWorks/Scripting/IVectorScriptEngine.h"
#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <string>

namespace
{
	using namespace VectorWorks::Scripting;

	// delete-layer プローブと同じ辿り方（ISDK に名前でレイヤを引く呼び出しは無い）。
	bool LayerExistsByName(const TXString& name)
	{
		for (MCObjectHandle h = VWDocument::GetDrawingHeaderFristMember(); h != nil;
			 h = gSDK->NextObject(h))
		{
			if (!VWLayerObj::IsLayerObject(h))
				continue;
			VWLayerObj layer(h);
			if (layer.GetObjectName() == name)
				return true;
		}
		return false;
	}

	std::string ExistsWord(bool exists)
	{
		return exists ? "残っている" : "消えた";
	}

	// VectorScript 版の「Undo を 1 回呼ぶだけ」スクリプト。PROCEDURE / Run の
	// 定型（VectorScript の標準的なスクリプト構造）で包む。この定型が誤っていた
	// 場合は CompileScript の outErrorText に出るはずなので、そこで判定する。
	TXString VectorScriptUndoSource()
	{
		return TXString("PROCEDURE __ProbeScriptEngineUndo;\n"
						 "BEGIN\n"
						 "\tDoMenuTextByName('Undo', 0);\n"
						 "END;\n"
						 "Run(__ProbeScriptEngineUndo);\n");
	}

	// Python 版。vs モジュール越しに同じ呼び出しをする。
	TXString PythonUndoSource()
	{
		return TXString("import vs\n"
						 "vs.DoMenuTextByName('Undo', 0)\n");
	}

	// CompileScript の結果をログへ書く。両エンジンとも同じ形の
	// CompileScript(script, showDialogs, outSuccess, outLine, outErrorText) を持つ。
	template <typename EnginePtr>
	bool LogCompileScript(::vwprobe::Report& probe, EnginePtr& engine, const TXString& script,
						   const char* label)
	{
		bool compiledOk = false;
		Sint32 errorLine = -1;
		TXString errorText;
		VCOMError err =
			engine->CompileScript(script, false /*showDialogs*/, compiledOk, &errorLine, &errorText);
		probe.log(std::string(label) + ": CompileScript VCOMError=" + std::to_string((long)err) +
				  " ok=" + (compiledOk ? "yes" : "no") + " line=" + std::to_string((long)errorLine) +
				  " errorText=[" + std::string(static_cast<const char*>(errorText)) + "]");
		return compiledOk;
	}
} // namespace

VW_PROBE("script-engine-undo", "スクリプトエンジン経由で Undo メニューを呼ぶ",
		 "IVectorScriptEngine / IPythonScriptEngine の ExecuteScript から "
		 "DoMenuTextByName('Undo', 0) を実行し、同期性・開いた undo イベントへの"
		 "影響・複数段の取り消し・エラー判別を確かめる")
{
	// --- エンジンを取得する（取れなければここで打ち切り） ---
	IVectorScriptEnginePtr vsEngine(IID_VectorScriptEngine);
	if (!vsEngine)
	{
		probe.fail("IVectorScriptEngine を取得できなかった（VCOMPtr が空）");
		return;
	}
	IPythonScriptEnginePtr pyEngine(IID_PythonScriptEngine);
	if (!pyEngine)
	{
		probe.fail("IPythonScriptEngine を取得できなかった（VCOMPtr が空）");
		return;
	}
	probe.log("両エンジンを取得できた（IVectorScriptEngine / IPythonScriptEngine）");

	const TXString vsScript = VectorScriptUndoSource();
	const TXString pyScript = PythonUndoSource();

	// =====================================================================
	// A) 素の状態: 直前に作ったレイヤ（＝直前の undo 可能な操作）を
	//    Undo で消せるか。VectorScript / Python 双方で試す。
	// =====================================================================
	probe.log("=== A) 素の状態（他に undo イベントを開いていない） ===");

	// --- A1) VectorScript ---
	{
		const TXString name = "probe-script-engine-undo-a1-vs";
		MCObjectHandle layer = gSDK->CreateLayer(name, kLayerDesign);
		if (layer == nil)
		{
			probe.fail("A1: CreateLayer が nil を返した");
		}
		else
		{
			probe.log("A1: マーカーレイヤを作った（" + std::string(static_cast<const char*>(name)) +
					  "）。存在確認: " + ExistsWord(LayerExistsByName(name)));

			bool compiledOk = LogCompileScript(probe, vsEngine, vsScript, "A1(VS)");
			if (!compiledOk)
			{
				probe.fail("A1: VectorScript 版 Undo スクリプトがコンパイルできなかった"
						   "（PROCEDURE/Run の定型を見直すこと）");
			}
			else
			{
				VCOMError err = vsEngine->ExecuteScript(vsScript);
				probe.log(std::string("A1(VS): ExecuteScript 直後 VCOMError=") +
						  std::to_string((long)err) +
						  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
				probe.log("A1(VS): ExecuteScript が戻った直後のマーカーレイヤ存在確認: " +
						  ExistsWord(LayerExistsByName(name)) +
						  "（消えていれば、少なくとも呼び出し元へ戻るまでに Undo が"
						  "効いている＝同期的とみなせる）");
			}
		}
	}

	// --- A2) Python ---
	{
		const TXString name = "probe-script-engine-undo-a2-py";
		MCObjectHandle layer = gSDK->CreateLayer(name, kLayerDesign);
		if (layer == nil)
		{
			probe.fail("A2: CreateLayer が nil を返した");
		}
		else
		{
			probe.log("A2: マーカーレイヤを作った（" + std::string(static_cast<const char*>(name)) +
					  "）。存在確認: " + ExistsWord(LayerExistsByName(name)));

			bool compiledOk = LogCompileScript(probe, pyEngine, pyScript, "A2(Python)");
			if (!compiledOk)
			{
				probe.fail("A2: Python 版 Undo スクリプトがコンパイルできなかった");
			}
			else
			{
				VCOMError err = pyEngine->ExecuteScript(pyScript, nullptr /*logger*/);
				probe.log(std::string("A2(Python): ExecuteScript 直後 VCOMError=") +
						  std::to_string((long)err) +
						  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
				probe.log("A2(Python): ExecuteScript が戻った直後のマーカーレイヤ存在確認: " +
						  ExistsWord(LayerExistsByName(name)));
			}
		}
	}

	// =====================================================================
	// B) 自分がまだ閉じていない undo イベントを開いている最中に呼ぶ。
	//    Findings/Undo.md 冒頭の「半端な記録を取り消すと図面が壊れる」パターンに
	//    該当するかを見る。ここから先で VectorWorks が落ちる可能性がある——
	//    危険な呼び出しの前に必ずログを出す（probe.log は毎行 flush する）。
	// =====================================================================
	probe.log("=== B) 自分の undo イベントを開いたまま呼ぶ（ここから危険域） ===");
	probe.log("B: undo building（開始前）: " +
			  std::string(gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));

	gSDK->SetUndoMethod(kUndoSwapObjects);
	gSDK->NameUndoEvent("probe-script-engine-undo-b-open-event");
	probe.log("B: SetUndoMethod(kUndoSwapObjects) + NameUndoEvent 済み（＝自分の undo "
			  "イベントを開いた）。undo building: " +
			  std::string(gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));

	const TXString nameB = "probe-script-engine-undo-b-inner";
	MCObjectHandle layerB = gSDK->CreateLayer(nameB, kLayerDesign);
	probe.log("B: 開いたイベントの中でマーカーレイヤを作った（" +
			  std::string(static_cast<const char*>(nameB)) +
			  "）。存在確認: " + ExistsWord(LayerExistsByName(nameB)));

	probe.log("B: これから ExecuteScript(VectorScript, Undo) を、自分の undo イベントを"
			  "閉じていない状態のまま呼ぶ。落ちた場合、この行が最後に残るログになる");
	VCOMError errB = vsEngine->ExecuteScript(vsScript);
	probe.log(std::string("B: ExecuteScript 直後 VCOMError=") + std::to_string((long)errB) +
			  " succeeded=" + (VCOM_SUCCEEDED(errB) ? "yes" : "no"));
	probe.log("B: 呼び出し後の undo building: " +
			  std::string(gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
	probe.log("B: 呼び出し後のマーカーレイヤ(b-inner)存在確認: " + ExistsWord(LayerExistsByName(nameB)));

	// 自分が開いたイベントが（Undo 側の後始末で）既に閉じられていなければ、
	// ここで明示的に後始末する。放置すると「半端な記録」がそのまま次の操作へ
	// 持ち越されてしまう（本ファイルの調査対象そのものの罠なので、ここは
	// 慎重に：閉じられているのに再度閉じようとして落ちないよう、必ず
	// IsCurrentlyBuildingAnUndoEvent() で確認してから呼ぶ）。
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
	{
		probe.log("B: 呼び出し後もイベントが開いたままだったので、"
				  "EndAndRemoveUndoEvent() で自分から後始末する");
		Boolean removed = gSDK->EndAndRemoveUndoEvent();
		probe.log(std::string("B: EndAndRemoveUndoEvent() = ") + (removed ? "true" : "false") +
				  "。後始末後の undo building: " +
				  std::string(gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no") +
				  " / マーカーレイヤ(b-inner)存在確認: " + ExistsWord(LayerExistsByName(nameB)));
	}
	else
	{
		probe.log("B: 呼び出し後、イベントは既に開いていない状態になっていた"
				  "（自分で EndUndoEvent 等を呼んでいないのに、というのがここでの観測点）");
	}

	// =====================================================================
	// C) 連続で 2 回呼んで複数段戻せるか。
	// =====================================================================
	probe.log("=== C) 連続 2 回呼び出し（複数段の取り消し） ===");
	const TXString nameC1 = "probe-script-engine-undo-c1";
	const TXString nameC2 = "probe-script-engine-undo-c2";
	MCObjectHandle layerC1 = gSDK->CreateLayer(nameC1, kLayerDesign);
	MCObjectHandle layerC2 = gSDK->CreateLayer(nameC2, kLayerDesign);
	if (layerC1 == nil || layerC2 == nil)
	{
		probe.fail("C: CreateLayer が nil を返した（c1/c2）");
	}
	else
	{
		probe.log("C: マーカーレイヤを 2 枚作った。c1 存在: " + ExistsWord(LayerExistsByName(nameC1)) +
				  " / c2 存在: " + ExistsWord(LayerExistsByName(nameC2)));

		VCOMError errC1 = vsEngine->ExecuteScript(vsScript);
		probe.log(std::string("C: 1 回目 ExecuteScript VCOMError=") + std::to_string((long)errC1) +
				  " succeeded=" + (VCOM_SUCCEEDED(errC1) ? "yes" : "no") +
				  " / c1 存在: " + ExistsWord(LayerExistsByName(nameC1)) +
				  " / c2 存在: " + ExistsWord(LayerExistsByName(nameC2)));

		VCOMError errC2 = vsEngine->ExecuteScript(vsScript);
		probe.log(std::string("C: 2 回目 ExecuteScript VCOMError=") + std::to_string((long)errC2) +
				  " succeeded=" + (VCOM_SUCCEEDED(errC2) ? "yes" : "no") +
				  " / c1 存在: " + ExistsWord(LayerExistsByName(nameC1)) +
				  " / c2 存在: " + ExistsWord(LayerExistsByName(nameC2)));
	}

	// =====================================================================
	// D) エラー判別: 構文が壊れたスクリプトを渡したとき、CompileScript /
	//    ExecuteScript の戻り値からそれと分かるか。
	// =====================================================================
	probe.log("=== D) 壊れたスクリプトでのエラー判別 ===");
	const TXString brokenVs = "PROCEDURE __ProbeBroken;\nBEGIN\n\tThisIsNotAValidCall(;\nEND;\n"
							   "Run(__ProbeBroken);\n";
	bool brokenCompiledOk = LogCompileScript(probe, vsEngine, brokenVs, "D(VS broken)");
	if (!brokenCompiledOk)
	{
		probe.log("D: CompileScript は構文エラーを検出できた（想定どおり）。"
				  "参考として ExecuteScript にも同じ壊れたスクリプトを渡してみる");
	}
	VCOMError errBroken = vsEngine->ExecuteScript(brokenVs);
	probe.log(std::string("D: 壊れたスクリプトを ExecuteScript に渡した結果 VCOMError=") +
			  std::to_string((long)errBroken) +
			  " succeeded=" + (VCOM_SUCCEEDED(errBroken) ? "yes" : "no") +
			  "（CompileScript の判定と食い違うなら、ExecuteScript 単体ではエラー判別が"
			  "できないということ）");

	probe.log("=== 完了。後片付けはしない（新規の空図面で走らせる運用）。 ===");
}
