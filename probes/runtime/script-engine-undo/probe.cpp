//
//	probes/runtime/script-engine-undo/probe.cpp
//
//	[issue #39] スクリプトエンジン経由（IVectorScriptEngine / IPythonScriptEngine の
//	ExecuteScript）で DoMenuTextByName('Undo', 0) を呼んだときの挙動を確かめる。
//
//	【2 度目である】1 度目は「スクリプトは走ったが取り消しが効かない」と読んだが、
//	走らせた利用者から**画面にスクリプトエラーが出ていた**という指摘があり、前提が
//	崩れた。1 度目のプローブは次の 2 つを取りこぼしていた:
//
//	  * Python の ExecuteScript へ **logger に nullptr を渡していた**（traceback を捨てた）。
//	  * スクリプトが**どこまで到達したか**を見る手立てを持たせていなかった。
//	    そのため「Undo が効かなかった」と「Undo の呼び出しまで行かずに落ちた」が
//	    同じ結果（マーカーが残る）に見えていた。
//
//	【この版の作り】上の 2 つを埋める。
//
//	  1. Python は SDK 同梱の CDefaultPythonLogger を渡し、fOutput / fErrors を丸ごと
//	     ログへ出す（IPythonWrapper.h。ヘッダだけで完結する実装）。VectorScript 側には
//	     ロガーも出力引数も無いので（IVectorScriptEngine.h）、そちらは 2 で見る。
//	  2. **スクリプト自身に足跡を残させる。** 呼び出しの前後で Layer(name) を呼ばせ、
//	     C++ 側からその存在を見る（Layer は無ければ作る。vs.py:25724）。
//	     m1（呼ぶ前）と m2（呼んだ後）の有無で、目視なしに切り分けられる:
//
//	       m1 有 / m2 有 … 通り抜けた。Undo は何もしなかった
//	       m1 無 / m2 有 … **Undo は効いた**——ただし取り消したのは
//	                        「スクリプト自身が直前に作った m1」である
//	       m1 有 / m2 無 … DoMenuTextByName の中で止まった（＝利用者が見たエラー）
//	       m1 無 / m2 無 … スクリプト本体へ入っていない
//
//	     C++ 側で先に作っておくマーカー（pre）も併せて見る。これが消えていれば
//	     「呼び出し元の操作まで遡って取り消せた」ことになり、それが issue の本題。
//	  3. 実行時エラーと構文エラーを**分けて**試す。1 度目は壊れたスクリプト（構文）だけを
//	     見て「ExecuteScript でも成否は分かる」と書いてしまったが、それはコンパイル時に
//	     落ちる誤りの話だった。ここでは**構文は正しいが実行時に失敗する**スクリプトを
//	     渡し、VCOMError が何を返すかを確かめる。
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

	// ISDK に名前でレイヤを引く呼び出しは無いので、図面の頭から辿る
	// （delete-layer / 1 度目のこのプローブと同じ辿り方）。
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

	std::string Mark(bool exists)
	{
		return exists ? "有" : "無";
	}

	// 3 つのマーカーの状態を 1 行にまとめる（pre は C++ が作ったもの、m1 / m2 は
	// スクリプト自身が作るもの）。**この 1 行が、この調査の判定そのものである。**
	std::string Marks(const TXString& pre, const TXString& m1, const TXString& m2)
	{
		return "pre=" + Mark(LayerExistsByName(pre)) + " m1=" + Mark(LayerExistsByName(m1)) +
			   " m2=" + Mark(LayerExistsByName(m2));
	}

	// VectorScript 版。Layer で足跡を残しながら Undo を呼ぶ。
	TXString VsScript(const std::string& m1, const std::string& m2)
	{
		const std::string src = "PROCEDURE __ProbeSeUndo;\n"
								"BEGIN\n"
								"\tLayer('" +
								m1 +
								"');\n"
								"\tDoMenuTextByName('Undo', 0);\n"
								"\tLayer('" +
								m2 +
								"');\n"
								"END;\n"
								"Run(__ProbeSeUndo);\n";
		return TXString(src.c_str());
	}

	// Python 版。足跡に加えて、例外を自分で捕まえて traceback を stdout へ出す
	// （ロガーの fOutput に入る）。エンジンが例外をどう扱うかに依存せずに済む。
	TXString PyScript(const std::string& m1, const std::string& m2)
	{
		const std::string src = "import vs, traceback\n"
								"vs.Layer('" +
								m1 +
								"')\n"
								"try:\n"
								"\tvs.DoMenuTextByName('Undo', 0)\n"
								"\tprint('probe: DoMenuTextByName returned')\n"
								"except Exception:\n"
								"\tprint('probe: raised\\n' + traceback.format_exc())\n"
								"vs.Layer('" +
								m2 +
								"')\n";
		return TXString(src.c_str());
	}

	// ロガーに溜まったものを 1 行ずつログへ移す（空なら「空」と書く——**取れなかった**
	// ことと**エラーが無かった**ことを見分けるため）。
	void LogPythonLogger(::vwprobe::Report& probe, const char* label, CDefaultPythonLogger& logger)
	{
		const std::string out = static_cast<const char*>(logger.fOutput);
		const std::string err = static_cast<const char*>(logger.fErrors);
		probe.log(std::string(label) + ": stdout=[" + (out.empty() ? "空" : out) + "]");
		probe.log(std::string(label) + ": stderr=[" + (err.empty() ? "空" : err) + "]");
	}

	template <typename EnginePtr>
	bool LogCompileScript(::vwprobe::Report& probe, EnginePtr& engine, const TXString& script,
						  const char* label)
	{
		bool compiledOk = false;
		Sint32 errorLine = -1;
		TXString errorText;
		VCOMError err = engine->CompileScript(script, false /*showDialogs*/, compiledOk, &errorLine,
											  &errorText);
		probe.log(std::string(label) + ": CompileScript VCOMError=" + std::to_string((long)err) +
				  " ok=" + (compiledOk ? "yes" : "no") +
				  " line=" + std::to_string((long)errorLine) + " errorText=[" +
				  std::string(static_cast<const char*>(errorText)) + "]");
		return compiledOk;
	}
} // namespace

VW_PROBE("script-engine-undo", "スクリプトエンジン経由で Undo メニューを呼ぶ（2 度目）",
		 "ExecuteScript の中で DoMenuTextByName('Undo', 0) がどこまで到達したかを、"
		 "スクリプト自身の足跡と Python のロガーで見る。実行時エラーが VCOMError に"
		 "出るかどうかも分ける")
{
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
	probe.log("判定の読み方: pre=C++ が作ったマーカー（消えれば呼び出し元の操作まで"
			  "取り消せたということ）/ m1=スクリプトが Undo の直前に作る / "
			  "m2=Undo の直後に作る");

	// =====================================================================
	// A) VectorScript。足跡だけが頼り（このエンジンにはロガーが無い）。
	// =====================================================================
	probe.log("=== A) VectorScript ===");
	{
		const TXString pre = "probe-se-undo-vs-pre";
		const TXString m1 = "probe-se-undo-vs-m1";
		const TXString m2 = "probe-se-undo-vs-m2";
		const TXString script = VsScript("probe-se-undo-vs-m1", "probe-se-undo-vs-m2");

		if (gSDK->CreateLayer(pre, kLayerDesign) == nil)
			probe.fail("A: pre マーカーの CreateLayer が nil を返した");
		probe.log("A: 実行前 " + Marks(pre, m1, m2));

		if (LogCompileScript(probe, vsEngine, script, "A(VS)"))
		{
			probe.log("A: これから ExecuteScript を呼ぶ。**画面にスクリプトエラーが出たら、"
					  "その文言を控えてください**（VectorScript 版はエラーを呼び出し側へ"
					  "返す口がヘッダに無く、ここには写りません）");
			const VCOMError err = vsEngine->ExecuteScript(script);
			probe.log(std::string("A(VS): ExecuteScript VCOMError=") + std::to_string((long)err) +
					  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
			probe.log("A: 実行後 " + Marks(pre, m1, m2));
		}
	}

	// =====================================================================
	// B) Python。ロガーを渡す（1 度目はここを nullptr にしていた）。
	// =====================================================================
	probe.log("=== B) Python（ロガーつき） ===");
	{
		const TXString pre = "probe-se-undo-py-pre";
		const TXString m1 = "probe-se-undo-py-m1";
		const TXString m2 = "probe-se-undo-py-m2";
		const TXString script = PyScript("probe-se-undo-py-m1", "probe-se-undo-py-m2");

		if (gSDK->CreateLayer(pre, kLayerDesign) == nil)
			probe.fail("B: pre マーカーの CreateLayer が nil を返した");
		probe.log("B: 実行前 " + Marks(pre, m1, m2));

		if (LogCompileScript(probe, pyEngine, script, "B(Python)"))
		{
			CDefaultPythonLogger logger;
			const VCOMError err = pyEngine->ExecuteScript(script, &logger);
			probe.log(std::string("B(Python): ExecuteScript VCOMError=") +
					  std::to_string((long)err) +
					  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
			LogPythonLogger(probe, "B(Python)", logger);
			probe.log("B: 実行後 " + Marks(pre, m1, m2));
		}
	}

	// =====================================================================
	// C) 実行時エラーは VCOMError に出るか（構文エラーとは別物）。
	//    Python でわざと実行時に落ちるスクリプトを渡す。ここで VCOMError=0 が
	//    返るなら、「戻り値では実行時の失敗を判別できない」が確定する。
	// =====================================================================
	probe.log("=== C) 実行時エラー（構文は正しい） ===");
	{
		const TXString script = "import vs\n"
								"vs.Layer('probe-se-undo-rt-before')\n"
								"1 / 0\n"
								"vs.Layer('probe-se-undo-rt-after')\n";
		if (LogCompileScript(probe, pyEngine, script, "C(Python)"))
		{
			CDefaultPythonLogger logger;
			const VCOMError err = pyEngine->ExecuteScript(script, &logger);
			probe.log(std::string("C(Python): ExecuteScript VCOMError=") +
					  std::to_string((long)err) +
					  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no") +
					  "（0 なら、実行時の失敗は戻り値では分からないということ）");
			LogPythonLogger(probe, "C(Python)", logger);
			probe.log(std::string("C: 落ちる前のレイヤ=") +
					  Mark(LayerExistsByName("probe-se-undo-rt-before")) + " 落ちた後のレイヤ=" +
					  Mark(LayerExistsByName("probe-se-undo-rt-after")) +
					  "（前だけ有なら、確かに途中で止まっている）");
		}
	}

	// =====================================================================
	// D) 開いたままの undo イベントの中で呼ぶと、そのイベントが終わる（1 度目の
	//    観測の追認）。C++ から直接見た値なので、スクリプトがエラーで終わっても
	//    観測自体は有効——ただし**エラー処理が閉じた**のかどうかは分けて見たい。
	//    そこで「必ず成功する短いスクリプト」でも同じことが起きるかを確かめる。
	// =====================================================================
	probe.log("=== D) 開いている undo イベントの中で呼ぶ ===");
	{
		const TXString innocuous = "import vs\nvs.Layer('probe-se-undo-d-inner-script')\n";
		probe.log(std::string("D: 開始前の undo building: ") +
				  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent("probe-se-undo-d-open-event");
		probe.log(std::string("D: 自分の undo イベントを開いた。building: ") +
				  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));

		const TXString inner = "probe-se-undo-d-inner";
		gSDK->CreateLayer(inner, kLayerDesign);
		probe.log("D: イベントの中でレイヤを作った。存在: " + Mark(LayerExistsByName(inner)));

		probe.log("D: これから**エラーの出ようが無いスクリプト**（レイヤを 1 枚作るだけ）を"
				  "ExecuteScript で呼ぶ。それでもイベントが終わるなら、原因はエラー処理では"
				  "なく ExecuteScript そのものだと分かる");
		CDefaultPythonLogger logger;
		const VCOMError err = pyEngine->ExecuteScript(innocuous, &logger);
		probe.log(std::string("D: ExecuteScript VCOMError=") + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
		LogPythonLogger(probe, "D", logger);
		probe.log(std::string("D: 呼び出し後の undo building: ") +
				  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no") +
				  "（no なら、自分で閉じていないのに終わっている）");
		probe.log("D: イベントの中で作ったレイヤの存在: " + Mark(LayerExistsByName(inner)) +
				  " / スクリプトが作ったレイヤの存在: " +
				  Mark(LayerExistsByName("probe-se-undo-d-inner-script")));

		if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		{
			const Boolean removed = gSDK->EndAndRemoveUndoEvent();
			probe.log(std::string("D: イベントが残っていたので EndAndRemoveUndoEvent() = ") +
					  (removed ? "true" : "false"));
		}
	}

	probe.log("=== 完了。後片付けはしない（新規の空図面で走らせる運用）。 ===");
	probe.log("★ 画面にスクリプトエラーのダイアログが出た場合は、その文言と"
			  "「A / B / C / D のどこで出たか」を控えてください。VectorScript 版のエラーは"
			  "ここには写りません");
}
