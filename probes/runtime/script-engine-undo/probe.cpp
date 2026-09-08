//
//	probes/runtime/script-engine-undo/probe.cpp
//
//	[issue #39] スクリプトエンジン経由（IVectorScriptEngine / IPythonScriptEngine の
//	ExecuteScript）で DoMenuTextByName('Undo', 0) を呼んだときの挙動を確かめる。
//
//	【3 度目である。ここまでに分かったこと】
//
//	1 度目: 「スクリプトは走ったが取り消しが効かない」と読んだ。しかし走らせた利用者から
//	        「画面にスクリプトエラーが出ていた」という指摘があり、前提が崩れた。
//	2 度目: スクリプト自身に足跡（Layer で作るマーカー）を残させたところ、VectorScript 版で
//	        **pre=有 / m1=無 / m2=有** が出た——スクリプトは最後まで走り、
//	        **Undo は効いていた。ただし消えたのはスクリプト自身が直前に作った m1** で、
//	        C++ 側が作った pre は残った。1 度目の「効かない」は誤りだった。
//	        一方 Python 側は、**ロガーを渡した ExecuteScript の 1 回目で VectorWorks ごと
//	        落ちた**（2 回とも同じ場所。EXC_BAD_ACCESS、スタックは
//	        PyRun_SimpleStringFlags → cfunction_call → VW の浅い 2 段）。
//
//	【この版の作り】落ちる場所を二分できる順番に並べ、**すべての ExecuteScript の直前に
//	★ 行を出す**（probe.log は 1 行ごとに flush するので、落ちても末尾が最後に通った場所に
//	なる。2 度目はここを守れておらず、B 以降を取りこぼした）。
//
//	  S1 VectorScript・足跡つき Undo      … 2 度目の結果の追認
//	  S2 VectorScript・Undo だけ          … C++ が作ったものは取り消しスタックに載るか
//	  S3 Python・vs を触らず print だけ   … 落ちたら**ロガー**が原因
//	  S4 Python・vs.Layer だけ            … 落ちたら vs.Layer（ロガー経由の出力）が原因
//	  S5 Python・Undo（ロガー無し）       … 落ちたら Undo の Python 経路が原因
//	  S6 Python・Undo（ロガーあり）       … S5 が通ったときだけ。落ちたら組み合わせが原因
//	  S7 実行時エラーの判別               … 構文エラーとは別物であることを確かめる
//	  S8 開いた undo イベントの中         … Python 経路を避け VectorScript で
//
//	【ダイアログの出所】利用者は「コンパイルに成功しました」というダイアログを見ている。
//	CompileScript は showDialogs=false で呼んでいるので、出所が CompileScript なのか
//	ExecuteScript なのかを切り分けたい。S1 だけ両方の直前に ★ を置いてある。
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

	// ISDK に名前でレイヤを引く呼び出しは無いので、図面の頭から辿る。
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

	// pre = C++ が作ったマーカー / m1 = スクリプトが Undo の直前に作る / m2 = 直後に作る。
	// **この 1 行がこの調査の判定そのものである。**
	std::string Marks(const std::string& pre, const std::string& m1, const std::string& m2)
	{
		return "pre=" + Mark(LayerExistsByName(pre.c_str())) +
			   " m1=" + Mark(LayerExistsByName(m1.c_str())) +
			   " m2=" + Mark(LayerExistsByName(m2.c_str()));
	}

	TXString VsWithFootprints(const std::string& m1, const std::string& m2)
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

	// スクリプトは何も作らない。**取り消しの対象になるのは C++ が作ったものか**を見る。
	TXString VsUndoOnly()
	{
		return TXString("PROCEDURE __ProbeSeUndoOnly;\n"
						"BEGIN\n"
						"\tDoMenuTextByName('Undo', 0);\n"
						"END;\n"
						"Run(__ProbeSeUndoOnly);\n");
	}

	TXString VsLayerOnly(const std::string& name)
	{
		const std::string src = "PROCEDURE __ProbeSeLayer;\n"
								"BEGIN\n"
								"\tLayer('" +
								name +
								"');\n"
								"END;\n"
								"Run(__ProbeSeLayer);\n";
		return TXString(src.c_str());
	}

	// vs を一切触らない。**ロガーだけを試す**ための最小のスクリプト。
	TXString PyPrintOnly()
	{
		return TXString("print('probe: hello from python')\n");
	}

	TXString PyLayerOnly(const std::string& name)
	{
		const std::string src = "import vs\nvs.Layer('" + name + "')\n";
		return TXString(src.c_str());
	}

	TXString PyWithFootprints(const std::string& m1, const std::string& m2)
	{
		const std::string src = "import vs\n"
								"vs.Layer('" +
								m1 +
								"')\n"
								"vs.DoMenuTextByName('Undo', 0)\n"
								"vs.Layer('" +
								m2 + "')\n";
		return TXString(src.c_str());
	}

	void LogPythonLogger(::vwprobe::Report& probe, const char* label, CDefaultPythonLogger& logger)
	{
		const std::string out = static_cast<const char*>(logger.fOutput);
		const std::string err = static_cast<const char*>(logger.fErrors);
		probe.log(std::string(label) + ": stdout=[" + (out.empty() ? "空" : out) + "]");
		probe.log(std::string(label) + ": stderr=[" + (err.empty() ? "空" : err) + "]");
	}

	template <typename EnginePtr>
	VCOMError RunScript(::vwprobe::Report& probe, EnginePtr& engine, const TXString& script,
						const char* label)
	{
		probe.log(std::string("★ ") + label +
				  ": これから ExecuteScript を呼ぶ。落ちたらこの行が最後に残る");
		const VCOMError err = engine->ExecuteScript(script);
		probe.log(std::string(label) + ": ExecuteScript VCOMError=" + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
		return err;
	}
} // namespace

VW_PROBE("script-engine-undo", "スクリプトエンジン経由で Undo メニューを呼ぶ（3 度目）",
		 "Undo の取り消し対象がどれかを足跡で確かめ、Python 側で VectorWorks ごと落ちる"
		 "原因（ロガー / vs.Layer / Undo）を二分して切り分ける")
{
	IVectorScriptEnginePtr vsEngine(IID_VectorScriptEngine);
	IPythonScriptEnginePtr pyEngine(IID_PythonScriptEngine);
	if (!vsEngine || !pyEngine)
	{
		probe.fail("スクリプトエンジンを取得できなかった（VCOMPtr が空）");
		return;
	}
	probe.log("両エンジンを取得できた");
	probe.log("判定の読み方: pre=C++ が作ったマーカー（消えれば呼び出し元の操作まで"
			  "取り消せたということ）/ m1=スクリプトが Undo の直前に作る / m2=直後に作る");

	// =====================================================================
	// S1) VectorScript・足跡つき（2 度目の結果の追認）
	// =====================================================================
	probe.log("=== S1) VectorScript・足跡つき Undo ===");
	{
		const std::string pre = "probe-s1-pre";
		const std::string m1 = "probe-s1-m1";
		const std::string m2 = "probe-s1-m2";
		gSDK->CreateLayer(pre.c_str(), kLayerDesign);
		probe.log("S1: 実行前 " + Marks(pre, m1, m2));

		const TXString script = VsWithFootprints(m1, m2);
		probe.log("★ S1: これから CompileScript(showDialogs=false) を呼ぶ。**ここで"
				  "「コンパイルに成功しました」が出たら、その旨を控えてください**");
		bool ok = false;
		Sint32 line = -1;
		TXString errorText;
		const VCOMError cerr = vsEngine->CompileScript(script, false, ok, &line, &errorText);
		probe.log(std::string("S1: CompileScript VCOMError=") + std::to_string((long)cerr) +
				  " ok=" + (ok ? "yes" : "no") + " line=" + std::to_string((long)line) +
				  " errorText=[" + std::string(static_cast<const char*>(errorText)) + "]");

		RunScript(probe, vsEngine, script, "S1(VS)");
		probe.log("S1: 実行後 " + Marks(pre, m1, m2) +
				  "（m1=無 かつ m2=有 なら、Undo が消したのはスクリプト自身の直前の操作）");
	}

	// =====================================================================
	// S2) VectorScript・Undo だけ。スクリプトは何も作らないので、取り消しの対象に
	//     なり得るのは **C++ が直前に作ったレイヤ** しかない。消えなければ、
	//     「undo イベントの外で C++ が作ったものは取り消しスタックに載らない」。
	// =====================================================================
	probe.log("=== S2) VectorScript・Undo だけ（スクリプトは何も作らない） ===");
	{
		const std::string pre = "probe-s2-pre";
		gSDK->CreateLayer(pre.c_str(), kLayerDesign);
		probe.log("S2: 実行前 pre=" + Mark(LayerExistsByName(pre.c_str())));
		RunScript(probe, vsEngine, VsUndoOnly(), "S2(VS)");
		probe.log("S2: 実行後 pre=" + Mark(LayerExistsByName(pre.c_str())) +
				  "（有のままなら、C++ が undo イベントの外で作ったものは取り消せない）");
	}

	// =====================================================================
	// S3) Python・vs を触らない。**落ちたらロガーが原因**。
	//     2 度目はここに相当する呼び出し（ロガー付き Python の 1 回目）で落ちている。
	// =====================================================================
	probe.log("=== S3) Python・print だけ（ロガーあり、vs を触らない） ===");
	bool pythonWithLoggerOk = false;
	{
		CDefaultPythonLogger logger;
		probe.log("★ S3: これから ExecuteScript(Python, ロガーあり) を呼ぶ。"
				  "落ちたらこの行が最後に残る＝**ロガーを渡すこと自体が原因**");
		const VCOMError err = pyEngine->ExecuteScript(PyPrintOnly(), &logger);
		probe.log(std::string("S3: ExecuteScript VCOMError=") + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
		LogPythonLogger(probe, "S3", logger);
		pythonWithLoggerOk = true;
		probe.log("S3: 落ちなかった。ロガーを渡すこと自体は安全である");
	}

	// =====================================================================
	// S4) Python・vs.Layer だけ（ロガーあり）。落ちたら vs.* の呼び出しが原因。
	// =====================================================================
	probe.log("=== S4) Python・vs.Layer だけ（ロガーあり） ===");
	{
		const std::string name = "probe-s4-py-layer";
		CDefaultPythonLogger logger;
		probe.log("★ S4: これから ExecuteScript(Python, vs.Layer, ロガーあり) を呼ぶ。"
				  "落ちたらこの行が最後に残る＝**ロガー越しの vs.* 呼び出しが原因**");
		const VCOMError err = pyEngine->ExecuteScript(PyLayerOnly(name), &logger);
		probe.log(std::string("S4: ExecuteScript VCOMError=") + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
		LogPythonLogger(probe, "S4", logger);
		probe.log("S4: 作られたか=" + Mark(LayerExistsByName(name.c_str())));
	}

	// =====================================================================
	// S5) Python・Undo（**ロガー無し**）。1 度目はこの形で落ちていない。
	// =====================================================================
	probe.log("=== S5) Python・足跡つき Undo（ロガー無し） ===");
	{
		const std::string pre = "probe-s5-pre";
		const std::string m1 = "probe-s5-m1";
		const std::string m2 = "probe-s5-m2";
		gSDK->CreateLayer(pre.c_str(), kLayerDesign);
		probe.log("S5: 実行前 " + Marks(pre, m1, m2));
		RunScript(probe, pyEngine, PyWithFootprints(m1, m2), "S5(Python)");
		probe.log("S5: 実行後 " + Marks(pre, m1, m2));
	}

	// =====================================================================
	// S6) Python・Undo（ロガーあり）。S3 が通っていれば試す価値がある。
	// =====================================================================
	if (pythonWithLoggerOk)
	{
		probe.log("=== S6) Python・足跡つき Undo（ロガーあり） ===");
		const std::string pre = "probe-s6-pre";
		const std::string m1 = "probe-s6-m1";
		const std::string m2 = "probe-s6-m2";
		gSDK->CreateLayer(pre.c_str(), kLayerDesign);
		probe.log("S6: 実行前 " + Marks(pre, m1, m2));
		CDefaultPythonLogger logger;
		probe.log("★ S6: これから ExecuteScript(Python, Undo, ロガーあり) を呼ぶ。"
				  "落ちたらこの行が最後に残る＝**Undo とロガーの組み合わせが原因**");
		const VCOMError err = pyEngine->ExecuteScript(PyWithFootprints(m1, m2), &logger);
		probe.log(std::string("S6: ExecuteScript VCOMError=") + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
		LogPythonLogger(probe, "S6", logger);
		probe.log("S6: 実行後 " + Marks(pre, m1, m2));
	}

	// =====================================================================
	// S7) 実行時エラーは戻り値に出るか（構文エラーとは別物）。
	// =====================================================================
	probe.log("=== S7) 実行時エラー（構文は正しい） ===");
	{
		const TXString script = "import vs\nvs.Layer('probe-s7-before')\n1 / 0\n"
								"vs.Layer('probe-s7-after')\n";
		CDefaultPythonLogger logger;
		probe.log("★ S7: これから ExecuteScript(Python, 1/0) を呼ぶ");
		const VCOMError err = pyEngine->ExecuteScript(script, &logger);
		probe.log(std::string("S7: ExecuteScript VCOMError=") + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no") +
				  "（0 なら、実行時の失敗は戻り値では分からないということ）");
		LogPythonLogger(probe, "S7", logger);
		probe.log(std::string("S7: 落ちる前=") + Mark(LayerExistsByName("probe-s7-before")) +
				  " 落ちた後=" + Mark(LayerExistsByName("probe-s7-after")));
	}

	// =====================================================================
	// S8) 開いた undo イベントの中で呼ぶ（Python 経路を避け、VectorScript で）。
	//     **エラーの出ようが無いスクリプト**なので、それでもイベントが終わるなら
	//     原因はエラー処理ではなく ExecuteScript そのものだと分かる。
	// =====================================================================
	probe.log("=== S8) 開いている undo イベントの中で呼ぶ（VectorScript） ===");
	{
		const std::string inner = "probe-s8-inner";
		probe.log(std::string("S8: 開始前の undo building=") +
				  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent("probe-s8-open-event");
		probe.log(std::string("S8: 自分の undo イベントを開いた。building=") +
				  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
		gSDK->CreateLayer(inner.c_str(), kLayerDesign);
		probe.log("S8: イベントの中でレイヤを作った。存在=" +
				  Mark(LayerExistsByName(inner.c_str())));

		RunScript(probe, vsEngine, VsLayerOnly("probe-s8-script-layer"), "S8(VS)");
		probe.log(std::string("S8: 呼び出し後の undo building=") +
				  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no") +
				  "（no なら、自分で閉じていないのに終わっている）");
		probe.log(
			"S8: イベントの中で作ったレイヤ=" + Mark(LayerExistsByName(inner.c_str())) +
			" / スクリプトが作ったレイヤ=" + Mark(LayerExistsByName("probe-s8-script-layer")));

		if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		{
			const Boolean removed = gSDK->EndAndRemoveUndoEvent();
			probe.log(std::string("S8: イベントが残っていたので EndAndRemoveUndoEvent() = ") +
					  (removed ? "true" : "false"));
		}
	}

	probe.log("=== 完了。後片付けはしない（新規の空図面で走らせる運用）。 ===");
}
