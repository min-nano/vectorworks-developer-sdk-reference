//
//	probes/runtime/script-engine-undo/probe.cpp
//
//	[issue #39] スクリプトエンジン経由（IVectorScriptEngine / IPythonScriptEngine の
//	ExecuteScript）で DoMenuTextByName('Undo', 0) を呼んだときの挙動を確かめる。
//
//	【4 度目である。ここまでに分かったこと】
//
//	1 度目: 「スクリプトは走ったが取り消しが効かない」と読んだ。走らせた利用者から
//	        「画面にスクリプトエラーが出ていた」という指摘があり、前提が崩れた。
//	2 度目: スクリプト自身に足跡を残させたところ VectorScript 版で pre=有 / m1=無 / m2=有。
//	        **Undo は効いていた**——消えたのはスクリプト自身が直前に作った m1。
//	3 度目: 落ちる場所を二分できた。**print だけ・ロガーあり（S3）は通り**、stdout も
//	        取れた。**vs.Layer・ロガーあり（S4）で VectorWorks ごと落ちた**。つまり
//	        原因はロガーではなく **Python から vs.* を呼ぶこと**の側にある。S2 も取れ、
//	        **C++ が undo イベントの外で作ったものは取り消しスタックに載らない**と分かった。
//
//	【この版の作り】3 度目は S4 で落ちたせいで S5 以降を丸ごと取りこぼした。落ちると
//	VectorWorks ごと終わるので、**1 回の走行で確かめられる「落ちるかもしれないもの」は
//	1 つだけ**——そこで次の 2 つを入れた。
//
//	  ・落ちる見込みのあるもの（Python から vs.* を呼ぶもの）を**全部後ろへ回す**。
//	  ・**足跡簿**（<ログ>.steps）を付ける。節に入る前と出た後を書き足しておき、次の
//	    走行では「入ったのに出ていない節」＝**前回落ちた節**を飛ばす。だから走らせ直す
//	    だけで先へ進む（利用者に頼むのは「もう一度走らせてください」だけで済む）。
//
//	  N1 VectorScript・足跡つき Undo        … 2・3 度目の追認
//	  N2 VectorScript・Undo だけ            … 3 度目の追認（C++ の作り物は載らない）
//	  N3 VectorScript・開いた undo イベント … 呼ぶとイベントが終わらされるか
//	  N4 Python・print だけ（ロガー無し）   … 3 度目に無かった素の経路
//	  N5 Python・print だけ（ロガーあり）   … 3 度目の追認
//	  N6 Python・1/0（vs を触らない）       … 実行時エラーが戻り値に出るか
//	  --- ここから先は落ちる見込みがある（落ちても上は取れている） ---
//	  C1 Python・vs.Layer（**ロガー無し**） … 落ちたら **vs.* 呼び出しそのもの**が原因
//	  C2 Python・vs.Layer（ロガーあり）     … 3 度目に落ちた形そのもの
//	  C3 Python・足跡つき Undo（ロガーあり）… 本題を Python でも見る
//	  C4 Python・ScriptContext 経由         … ExecuteScript とは別の口。逃げ道があるか
//
//	【ダイアログの出所】利用者は「コンパイルに成功しました」というダイアログを見ている。
//	CompileScript は showDialogs=false で呼んでいるので、N1 の CompileScript と
//	ExecuteScript の両方の直前に ★ を置いて、どちらの後で出たかを言ってもらう。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Scripting/IPythonScriptEngine.h"
#include "Interfaces/VectorWorks/Scripting/IVectorScriptEngine.h"
#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	using namespace VectorWorks::Scripting;

	// -----------------------------------------------------------------------
	// 足跡簿。**落ちた節を次の走行で飛ばす**ためだけの、行指向の小さなファイル。
	//
	// 節に入る前に "<節> begin"、抜けたら "<節> end" を書き足す（1 行ごとに閉じるので
	// 落ちても残る）。begin があって end が無い節＝**前回そこで落ちた**ので飛ばす。
	// 置き場はログの隣（Report::logPath() + ".steps"）。ログが開けていないときは
	// 何もしない（＝いつも通り全部走る）。
	class StepLedger
	{
	public:
		explicit StepLedger(const std::string& logPath)
		{
			if (logPath.empty())
				return;
			fPath = logPath + ".steps";
			if (std::FILE* f = std::fopen(fPath.c_str(), "r"))
			{
				char line[256];
				while (std::fgets(line, sizeof(line), f) != nullptr)
				{
					std::string s(line);
					while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
						s.pop_back();
					if (!s.empty())
						fPrevious.push_back(s);
				}
				std::fclose(f);
			}
		}

		bool enabled() const
		{
			return !fPath.empty();
		}

		const std::string& path() const
		{
			return fPath;
		}

		const std::vector<std::string>& previous() const
		{
			return fPrevious;
		}

		// 前回「入ったのに出ていない」節か。
		bool crashedBefore(const std::string& step) const
		{
			return has(step + " begin") && !has(step + " end");
		}

		void begin(const std::string& step)
		{
			append(step + " begin");
		}

		void end(const std::string& step)
		{
			append(step + " end");
		}

	private:
		bool has(const std::string& line) const
		{
			for (const std::string& s : fPrevious)
				if (s == line)
					return true;
			return false;
		}

		void append(const std::string& line)
		{
			if (fPath.empty())
				return;
			if (std::FILE* f = std::fopen(fPath.c_str(), "a"))
			{
				std::fputs(line.c_str(), f);
				std::fputc('\n', f);
				std::fclose(f);
			}
		}

		std::string fPath;
		std::vector<std::string> fPrevious;
	};

	// 節 1 つ分の入り／出を必ず対にするための小道具。
	class Step
	{
	public:
		Step(::vwprobe::Report& probe, StepLedger& ledger, const char* name, const char* title)
			: fLedger(ledger), fName(name)
		{
			fSkip = ledger.crashedBefore(fName);
			if (fSkip)
			{
				probe.log(std::string("=== ") + name +
						  ") 飛ばす——**前回の走行はここで"
						  "落ちた**（足跡簿に begin だけが残っている） ===");
				return;
			}
			probe.log(std::string("=== ") + name + ") " + title + " ===");
			ledger.begin(fName);
		}

		~Step()
		{
			if (!fSkip)
				fLedger.end(fName);
		}

		Step(const Step&) = delete;
		Step& operator=(const Step&) = delete;

		bool skip() const
		{
			return fSkip;
		}

	private:
		StepLedger& fLedger;
		std::string fName;
		bool fSkip = false;
	};

	// -----------------------------------------------------------------------
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

	// vs を一切触らない。**Python の実行そのもの**を試すための最小のスクリプト。
	TXString PyPrintOnly(const std::string& tag)
	{
		const std::string src = "print('probe: hello from python (" + tag + ")')\n";
		return TXString(src.c_str());
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

	void LogResult(::vwprobe::Report& probe, const char* label, VCOMError err)
	{
		probe.log(std::string(label) + ": ExecuteScript VCOMError=" + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));
	}

	VCOMError RunVs(::vwprobe::Report& probe, IVectorScriptEnginePtr& engine,
					const TXString& script, const char* label)
	{
		probe.log(std::string("★ ") + label +
				  ": これから ExecuteScript を呼ぶ。落ちたらこの行が最後に残る");
		const VCOMError err = engine->ExecuteScript(script);
		LogResult(probe, label, err);
		return err;
	}
} // namespace

VW_PROBE("script-engine-undo", "スクリプトエンジン経由で Undo メニューを呼ぶ（4 度目）",
		 "Python から vs.* を呼ぶと落ちる件を切り分ける。落ちた節は足跡簿に残り、次に"
		 "走らせたときは飛ばすので、**走らせ直すだけで先へ進む**")
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

	StepLedger ledger(probe.logPath());
	if (!ledger.enabled())
		probe.log("足跡簿: ログを開けていないので使えない（今回は全部の節を走らせる）");
	else if (ledger.previous().empty())
		probe.log("足跡簿: 前回の記録は無い（初回）");
	else
	{
		std::string joined;
		for (const std::string& s : ledger.previous())
			joined += (joined.empty() ? "" : " / ") + s;
		probe.log("足跡簿（前回まで）: " + joined);
		probe.log("足跡簿の置き場: " + ledger.path() + "（消せば飛ばした節もまた走る）");
	}

	// =====================================================================
	// N1) VectorScript・足跡つき（2・3 度目の追認）
	// =====================================================================
	{
		Step step(probe, ledger, "N1", "VectorScript・足跡つき Undo");
		if (!step.skip())
		{
			const std::string pre = "probe-n1-pre";
			const std::string m1 = "probe-n1-m1";
			const std::string m2 = "probe-n1-m2";
			gSDK->CreateLayer(pre.c_str(), kLayerDesign);
			probe.log("N1: 実行前 " + Marks(pre, m1, m2));

			const TXString script = VsWithFootprints(m1, m2);
			probe.log("★ N1: これから CompileScript(showDialogs=false) を呼ぶ。**ここで"
					  "「コンパイルに成功しました」が出たら、その旨を控えてください**");
			bool ok = false;
			Sint32 line = -1;
			TXString errorText;
			const VCOMError cerr = vsEngine->CompileScript(script, false, ok, &line, &errorText);
			probe.log(std::string("N1: CompileScript VCOMError=") + std::to_string((long)cerr) +
					  " ok=" + (ok ? "yes" : "no") + " line=" + std::to_string((long)line) +
					  " errorText=[" + std::string(static_cast<const char*>(errorText)) + "]");

			RunVs(probe, vsEngine, script, "N1(VS)");
			probe.log("N1: 実行後 " + Marks(pre, m1, m2) +
					  "（m1=無 かつ m2=有 なら、Undo が消したのはスクリプト自身の直前の操作）");
		}
	}

	// =====================================================================
	// N2) VectorScript・Undo だけ。スクリプトは何も作らないので、取り消しの対象に
	//     なり得るのは **C++ が直前に作ったレイヤ** しかない。
	// =====================================================================
	{
		Step step(probe, ledger, "N2", "VectorScript・Undo だけ（スクリプトは何も作らない）");
		if (!step.skip())
		{
			const std::string pre = "probe-n2-pre";
			gSDK->CreateLayer(pre.c_str(), kLayerDesign);
			probe.log("N2: 実行前 pre=" + Mark(LayerExistsByName(pre.c_str())));
			RunVs(probe, vsEngine, VsUndoOnly(), "N2(VS)");
			probe.log("N2: 実行後 pre=" + Mark(LayerExistsByName(pre.c_str())) +
					  "（有のままなら、C++ が undo イベントの外で作ったものは取り消せない）");
		}
	}

	// =====================================================================
	// N3) 開いた undo イベントの中で呼ぶ。**エラーの出ようが無いスクリプト**なので、
	//     それでもイベントが終わるなら、原因はエラー処理ではなく ExecuteScript そのもの。
	// =====================================================================
	{
		Step step(probe, ledger, "N3", "開いている undo イベントの中で呼ぶ（VectorScript）");
		if (!step.skip())
		{
			const std::string inner = "probe-n3-inner";
			probe.log(std::string("N3: 開始前の undo building=") +
					  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
			gSDK->SetUndoMethod(kUndoSwapObjects);
			gSDK->NameUndoEvent("probe-n3-open-event");
			probe.log(std::string("N3: 自分の undo イベントを開いた。building=") +
					  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
			gSDK->CreateLayer(inner.c_str(), kLayerDesign);
			probe.log("N3: イベントの中でレイヤを作った。存在=" +
					  Mark(LayerExistsByName(inner.c_str())));

			RunVs(probe, vsEngine, VsLayerOnly("probe-n3-script-layer"), "N3(VS)");
			probe.log(std::string("N3: 呼び出し後の undo building=") +
					  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no") +
					  "（no なら、自分で閉じていないのに終わっている）");
			probe.log(
				"N3: イベントの中で作ったレイヤ=" + Mark(LayerExistsByName(inner.c_str())) +
				" / スクリプトが作ったレイヤ=" + Mark(LayerExistsByName("probe-n3-script-layer")));

			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
			{
				const Boolean removed = gSDK->EndAndRemoveUndoEvent();
				probe.log(std::string("N3: イベントが残っていたので EndAndRemoveUndoEvent() = ") +
						  (removed ? "true" : "false"));
			}
		}
	}

	// =====================================================================
	// N4) Python・print だけ・**ロガー無し**（既定引数の NULL）。
	//     3 度目に無かった経路。ここが通れば「Python の実行そのもの」は安全。
	// =====================================================================
	{
		Step step(probe, ledger, "N4", "Python・print だけ（ロガー無し）");
		if (!step.skip())
		{
			probe.log("★ N4: これから ExecuteScript(Python, print, ロガー無し) を呼ぶ");
			const VCOMError err = pyEngine->ExecuteScript(PyPrintOnly("no-logger"));
			LogResult(probe, "N4", err);
		}
	}

	// =====================================================================
	// N5) Python・print だけ・ロガーあり（3 度目の追認）。
	// =====================================================================
	{
		Step step(probe, ledger, "N5", "Python・print だけ（ロガーあり）");
		if (!step.skip())
		{
			CDefaultPythonLogger logger;
			probe.log("★ N5: これから ExecuteScript(Python, print, ロガーあり) を呼ぶ");
			const VCOMError err = pyEngine->ExecuteScript(PyPrintOnly("with-logger"), &logger);
			LogResult(probe, "N5", err);
			LogPythonLogger(probe, "N5", logger);
		}
	}

	// =====================================================================
	// N6) 実行時エラーは戻り値に出るか。**vs を触らない**形で確かめる
	//     （3 度目は vs.Layer を混ぜていたので、落ちて取りこぼした）。
	// =====================================================================
	{
		Step step(probe, ledger, "N6", "Python・実行時エラー 1/0（vs を触らない）");
		if (!step.skip())
		{
			CDefaultPythonLogger logger;
			probe.log("★ N6: これから ExecuteScript(Python, 1/0) を呼ぶ");
			const VCOMError err = pyEngine->ExecuteScript(
				TXString("print('probe: before')\n1 / 0\nprint('probe: after')\n"), &logger);
			probe.log(std::string("N6: ExecuteScript VCOMError=") + std::to_string((long)err) +
					  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no") +
					  "（0 なら、実行時の失敗は戻り値では分からないということ）");
			LogPythonLogger(probe, "N6", logger);
		}
	}

	// =====================================================================
	// ここから先は **落ちる見込みがある**。3 度目はロガーつきの vs.Layer で落ちた。
	// ロガーを外した形を先に置いて、原因がロガーなのか vs.* そのものなのかを分ける。
	// =====================================================================
	probe.log("=== ここから先は落ちる見込みがある（手前の結果はもう取れている） ===");

	// C1) Python・vs.Layer・**ロガー無し**。
	{
		Step step(probe, ledger, "C1", "Python・vs.Layer だけ（ロガー無し）");
		if (!step.skip())
		{
			const std::string name = "probe-c1-py-layer";
			probe.log("★ C1: これから ExecuteScript(Python, vs.Layer, **ロガー無し**) を呼ぶ。"
					  "落ちたらこの行が最後に残る＝**Python から vs.* を呼ぶこと自体が原因**"
					  "（ロガーは無関係）");
			const VCOMError err = pyEngine->ExecuteScript(PyLayerOnly(name));
			LogResult(probe, "C1", err);
			probe.log("C1: 作られたか=" + Mark(LayerExistsByName(name.c_str())));
		}
	}

	// C2) Python・vs.Layer・ロガーあり（3 度目に落ちた形そのもの）。
	{
		Step step(probe, ledger, "C2", "Python・vs.Layer だけ（ロガーあり＝3 度目に落ちた形）");
		if (!step.skip())
		{
			const std::string name = "probe-c2-py-layer";
			CDefaultPythonLogger logger;
			probe.log("★ C2: これから ExecuteScript(Python, vs.Layer, ロガーあり) を呼ぶ。"
					  "落ちたらこの行が最後に残る＝**ロガーと vs.* の組み合わせ**が原因");
			const VCOMError err = pyEngine->ExecuteScript(PyLayerOnly(name), &logger);
			LogResult(probe, "C2", err);
			LogPythonLogger(probe, "C2", logger);
			probe.log("C2: 作られたか=" + Mark(LayerExistsByName(name.c_str())));
		}
	}

	// C3) Python・足跡つき Undo。ここまで来られたら本題を Python でも見る。
	{
		Step step(probe, ledger, "C3", "Python・足跡つき Undo（ロガーあり）");
		if (!step.skip())
		{
			const std::string pre = "probe-c3-pre";
			const std::string m1 = "probe-c3-m1";
			const std::string m2 = "probe-c3-m2";
			gSDK->CreateLayer(pre.c_str(), kLayerDesign);
			probe.log("C3: 実行前 " + Marks(pre, m1, m2));
			CDefaultPythonLogger logger;
			probe.log("★ C3: これから ExecuteScript(Python, 足跡つき Undo, ロガーあり) を呼ぶ");
			const VCOMError err = pyEngine->ExecuteScript(PyWithFootprints(m1, m2), &logger);
			LogResult(probe, "C3", err);
			LogPythonLogger(probe, "C3", logger);
			probe.log("C3: 実行後 " + Marks(pre, m1, m2));
		}
	}

	// C4) ScriptContext 経由。ExecuteScript とは**別の口**（ヘッダにある
	//     ScriptContext_Begin → ScriptContext_Run）。ExecuteScript が落ちるなら、
	//     こちらが逃げ道になるかを見る。
	{
		Step step(probe, ledger, "C4", "Python・ScriptContext 経由で vs.Layer");
		if (!step.skip())
		{
			const std::string name = "probe-c4-py-layer";
			CDefaultPythonLogger logger;
			probe.log("★ C4: これから ScriptContext_Begin → ScriptContext_Run を呼ぶ");
			const VCOMError berr = pyEngine->ScriptContext_Begin(PyLayerOnly(name), &logger);
			probe.log(std::string("C4: ScriptContext_Begin VCOMError=") +
					  std::to_string((long)berr));
			const VCOMError rerr = pyEngine->ScriptContext_Run();
			probe.log(std::string("C4: ScriptContext_Run VCOMError=") + std::to_string((long)rerr) +
					  " succeeded=" + (VCOM_SUCCEEDED(rerr) ? "yes" : "no"));
			LogPythonLogger(probe, "C4", logger);
			probe.log("C4: 作られたか=" + Mark(LayerExistsByName(name.c_str())));
		}
	}

	probe.log("=== 完了。後片付けはしない（新規の空図面で走らせる運用）。 ===");
}
