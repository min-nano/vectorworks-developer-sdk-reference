//
//	probes/runtime/script-engine-undo/probe.cpp
//
//	[issue #39] スクリプトエンジン経由（IVectorScriptEngine / IPythonScriptEngine）で
//	DoMenuTextByName('Undo', 0) を呼んだときの挙動を確かめる。
//
//	【5 度目である。ここまでに分かったこと】
//
//	1 度目: 「スクリプトは走ったが取り消しが効かない」と読んだ。走らせた利用者から
//	        「画面にスクリプトエラーが出ていた」という指摘があり、前提が崩れた。
//	2・3 度目: スクリプト自身に足跡を残させたところ VectorScript 版で pre=有 / m1=無 / m2=有。
//	        **Undo は効いていた**——消えたのはスクリプト自身が直前に作った m1。C++ が
//	        undo イベントの外で作ったものは取り消しスタックに載らない（N2）。
//	4 度目: 足跡簿のおかげで 4 回の走行で全部の節を通せた。分かったのは 4 つ。
//	        ・**`IPythonScriptEngine::ExecuteScript` から `vs.*` を呼ぶと必ず落ちる**
//	          （C1 ロガー無し / C2 ロガーあり / C3 Undo つき——3 通りとも落ちた）。
//	        ・**`ScriptContext_Begin` → `ScriptContext_Run` なら通る**（C4。`vs.Layer` が
//	          実際にレイヤを作った）。**Python から vs を使う道はこちらである。**
//	        ・**Python のロガーは実行時エラーの traceback を拾える**（N6 で
//	          ZeroDivisionError。`VCOMError` は 0 のまま）。
//	        ・**`ExecuteScript` そのものは、こちらが開いた undo イベントを閉じない**
//	          （N3 で building=yes のまま戻った）。1 度目の「閉じられた」の犯人は別にいる。
//
//	【この版で確かめること】
//
//	  ・**1 度目に undo イベントを終わらせたのは何か。** 候補は 2 つ——スクリプトが呼んだ
//	    `DoMenuTextByName('Undo', 0)` そのもの（N7）か、スクリプトが失敗したこと（N8）か。
//	  ・**「コンパイルに成功しました」のダイアログを出しているのはどの呼び出しか。**
//	    利用者に「どの行で出たか」は答えられない——**画面にコードの位置は出ない**。
//	    そこで**呼び出しごとに時間を測る**。モーダルダイアログは人が閉じるまで戻らないので、
//	    **秒単位で掛かった呼び出しがダイアログを出した呼び出し**である（他は数ミリ秒）。
//	    利用者に要るのは「出たら閉じる」だけになる。
//	  ・**通る道（ScriptContext）で Python から Undo は効くか**（C6）。ロガーはこの道でも
//	    働くか（C5。`ScriptContext_RunEx` はロガーを取る）。
//	  ・**スクリプトを走らせると undo イベントが開いたまま残るのか。** 4 度目の走行は
//	    最後に `undo: after building=yes` で終わっていた。節ごとに測って出所を絞る。
//
//	【この版の作り】落ちる見込みのある節を後ろへ回し、**足跡簿**（<ログ>.steps）で
//	「入ったのに出ていない節」＝前回落ちた節を次の走行では飛ばす。だから走らせ直す
//	だけで先へ進む（利用者に頼むのは「もう一度走らせてください」だけで済む）。
//
//	  N1 VectorScript・足跡つき Undo        … 追認。CompileScript と ExecuteScript を計測
//	  N2 VectorScript・Undo だけ            … C++ の作り物は取り消しスタックに載らない
//	  N3 VectorScript・開いた undo イベント … 無害なスクリプトなら閉じられない（追認）
//	  N7 同・**Undo を呼ぶスクリプト**      … 閉じたら **Undo が原因**
//	  N8 同・**失敗するスクリプト**         … 閉じたら **失敗の後始末が原因**
//	  N4 Python・print だけ（ロガー無し）
//	  N5 Python・print だけ（ロガーあり）
//	  N6 Python・1/0（vs を触らない）       … 実行時エラーはロガーで拾える
//	  C4 Python・ScriptContext で vs.Layer  … 4 度目に通った道の追認
//	  C5 同・print（RunEx でロガーを渡す）  … この道でもロガーは働くか
//	  C6 同・**足跡つき Undo**              … **Python から Undo は効くか（本題）**
//	  C1 Python・ExecuteScript で vs.Layer  … 落ちることの記録。**必ず最後に置く**
//
//	C2（ロガーつき）と C3（Undo つき）は落とした——C1 と合わせて 3 通りとも落ちると
//	分かったので、同じことを二度落として確かめる意味が無い。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Scripting/IPythonScriptEngine.h"
#include "Interfaces/VectorWorks/Scripting/IVectorScriptEngine.h"
#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <chrono>
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
						  ") 飛ばす——**前回の走行はここで落ちた**"
						  "（足跡簿に begin だけが残っている） ===");
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
	// **どの呼び出しがダイアログを出したかを、目視なしで突き止めるための時計。**
	// モーダルダイアログは人が閉じるまで戻らないので、**その呼び出しだけが秒単位で
	// 掛かる**。他は数ミリ秒で戻る。だから利用者に要るのは「出たら閉じる」だけで、
	// どこで出たかはログの `elapsed=` を読めば分かる（4 度目までは「どの行で出たか」を
	// 尋ねていたが、**画面にコードの位置は出ない**ので答えようが無かった）。
	class Stopwatch
	{
	public:
		Stopwatch() : fStart(std::chrono::steady_clock::now()) {}

		long ms() const
		{
			return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::steady_clock::now() - fStart)
				.count();
		}

	private:
		std::chrono::steady_clock::time_point fStart;
	};

	// 1 秒を超えた呼び出しには目印を付ける（＝そこで人を待っていた＝ダイアログが出た）。
	std::string Elapsed(long ms)
	{
		std::string s = " elapsed=" + std::to_string(ms) + "ms";
		if (ms >= 1000)
			s += " ← **ここで人を待っていた＝ダイアログを出したのはこの呼び出し**";
		return s;
	}

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

	std::string Building()
	{
		return gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no";
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

	// **必ず失敗する** VectorScript。3 度目に確かめた形（`CompileScript` は ok=no、
	// `ExecuteScript` は VCOMError=1）をそのまま使う。「失敗した実行」が呼び出し側の
	// undo イベントを閉じるかどうかを見るための材料。
	TXString VsBroken()
	{
		return TXString("ThisIsNotAValidCall(;\n");
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

	void LogResult(::vwprobe::Report& probe, const char* label, VCOMError err, long ms)
	{
		probe.log(std::string(label) + ": ExecuteScript VCOMError=" + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no") + Elapsed(ms) +
				  " / 呼び出し後の undo building=" + Building());
	}

	VCOMError RunVs(::vwprobe::Report& probe, IVectorScriptEnginePtr& engine,
					const TXString& script, const char* label)
	{
		probe.log(std::string("★ ") + label +
				  ": これから ExecuteScript を呼ぶ。落ちたらこの行が最後に残る");
		const Stopwatch watch;
		const VCOMError err = engine->ExecuteScript(script);
		LogResult(probe, label, err, watch.ms());
		return err;
	}

	// ScriptContext 経由（4 度目に通った道）。ロガーを渡すときは RunEx を使う。
	void RunScriptContext(::vwprobe::Report& probe, IPythonScriptEnginePtr& engine,
						  const TXString& script, const char* label, CDefaultPythonLogger* logger)
	{
		probe.log(std::string("★ ") + label + ": これから ScriptContext_Begin → " +
				  (logger != nullptr ? "ScriptContext_RunEx(ロガー)" : "ScriptContext_Run") +
				  " を呼ぶ");
		const Stopwatch beginWatch;
		const VCOMError berr = engine->ScriptContext_Begin(script, logger);
		probe.log(std::string(label) + ": ScriptContext_Begin VCOMError=" +
				  std::to_string((long)berr) + Elapsed(beginWatch.ms()));

		const Stopwatch runWatch;
		const VCOMError rerr =
			logger != nullptr ? engine->ScriptContext_RunEx(logger) : engine->ScriptContext_Run();
		probe.log(std::string(label) +
				  ": ScriptContext_Run VCOMError=" + std::to_string((long)rerr) +
				  " succeeded=" + (VCOM_SUCCEEDED(rerr) ? "yes" : "no") + Elapsed(runWatch.ms()) +
				  " / 呼び出し後の undo building=" + Building());
		if (logger != nullptr)
			LogPythonLogger(probe, label, *logger);
	}
} // namespace

VW_PROBE("script-engine-undo", "スクリプトエンジン経由で Undo メニューを呼ぶ（5 度目）",
		 "開いた undo イベントを終わらせた犯人を分け、ダイアログの出所を所要時間で突き止め、"
		 "通る道（ScriptContext）で Python から Undo が効くかを見る")
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
	probe.log("**ダイアログが出たら閉じてください。どの呼び出しで出たかは elapsed= で分かる**"
			  "——秒単位で掛かった 1 本がそれです（他は数ミリ秒で戻ります）");

	StepLedger ledger(probe.logPath());
	if (!ledger.enabled())
		probe.log("足跡簿: ログを開けていないので使えない（今回は全部の節を走らせる）");
	else if (ledger.previous().empty())
		probe.log("足跡簿: 前回の記録は無い（初回）");
	else
		probe.log("足跡簿: " + std::to_string(ledger.previous().size()) +
				  " 行の記録がある。置き場は " + ledger.path() + "（消せば飛ばした節もまた走る）");

	// =====================================================================
	// N1) VectorScript・足跡つき（2〜4 度目の追認）。**ここが唯一 CompileScript を
	//     呼ぶ節**なので、ダイアログの出所を測るのもここ。
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
			probe.log("★ N1: これから CompileScript(showDialogs=false) を呼ぶ");
			bool ok = false;
			Sint32 line = -1;
			TXString errorText;
			const Stopwatch watch;
			const VCOMError cerr = vsEngine->CompileScript(script, false, ok, &line, &errorText);
			probe.log(std::string("N1: CompileScript VCOMError=") + std::to_string((long)cerr) +
					  " ok=" + (ok ? "yes" : "no") + " line=" + std::to_string((long)line) +
					  " errorText=[" + std::string(static_cast<const char*>(errorText)) + "]" +
					  Elapsed(watch.ms()));

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
	// N3) 開いた undo イベントの中で、**無害なスクリプト**を走らせる（4 度目の追認）。
	//     4 度目は building=yes のまま戻った＝`ExecuteScript` は犯人ではない。
	// =====================================================================
	{
		Step step(probe, ledger, "N3",
				  "開いた undo イベントの中で無害なスクリプト（VectorScript）");
		if (!step.skip())
		{
			const std::string inner = "probe-n3-inner";
			probe.log("N3: 開始前の undo building=" + Building());
			gSDK->SetUndoMethod(kUndoSwapObjects);
			gSDK->NameUndoEvent("probe-n3-open-event");
			gSDK->CreateLayer(inner.c_str(), kLayerDesign);
			probe.log("N3: イベントを開いてレイヤを作った。building=" + Building() +
					  " / レイヤ=" + Mark(LayerExistsByName(inner.c_str())));

			RunVs(probe, vsEngine, VsLayerOnly("probe-n3-script-layer"), "N3(VS)");
			probe.log(
				"N3: イベントの中で作ったレイヤ=" + Mark(LayerExistsByName(inner.c_str())) +
				" / スクリプトが作ったレイヤ=" + Mark(LayerExistsByName("probe-n3-script-layer")));

			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
			{
				const Boolean removed = gSDK->EndAndRemoveUndoEvent();
				probe.log(std::string("N3: 後始末 EndAndRemoveUndoEvent() = ") +
						  (removed ? "true" : "false"));
			}
		}
	}

	// =====================================================================
	// N7) 開いた undo イベントの中で、**Undo を呼ぶスクリプト**を走らせる。
	//     N3 が閉じなかったので、ここで閉じるなら犯人は **Undo そのもの**である。
	// =====================================================================
	{
		Step step(probe, ledger, "N7", "開いた undo イベントの中で Undo を呼ばせる");
		if (!step.skip())
		{
			const std::string inner = "probe-n7-inner";
			gSDK->SetUndoMethod(kUndoSwapObjects);
			gSDK->NameUndoEvent("probe-n7-open-event");
			gSDK->CreateLayer(inner.c_str(), kLayerDesign);
			probe.log("N7: イベントを開いてレイヤを作った。building=" + Building() +
					  " / レイヤ=" + Mark(LayerExistsByName(inner.c_str())));

			RunVs(probe, vsEngine, VsUndoOnly(), "N7(VS)");
			probe.log("N7: イベントの中で作ったレイヤ=" + Mark(LayerExistsByName(inner.c_str())) +
					  "（building が no なら **イベントを終わらせたのは Undo そのもの**。"
					  "レイヤも無なら、開きかけのイベントごと取り消された）");

			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
			{
				const Boolean removed = gSDK->EndAndRemoveUndoEvent();
				probe.log(std::string("N7: 後始末 EndAndRemoveUndoEvent() = ") +
						  (removed ? "true" : "false"));
			}
		}
	}

	// =====================================================================
	// N8) 開いた undo イベントの中で、**必ず失敗するスクリプト**を走らせる。
	//     N7 と対にして「Undo が犯人か、失敗の後始末が犯人か」を分ける。
	// =====================================================================
	{
		Step step(probe, ledger, "N8", "開いた undo イベントの中で失敗するスクリプト");
		if (!step.skip())
		{
			const std::string inner = "probe-n8-inner";
			gSDK->SetUndoMethod(kUndoSwapObjects);
			gSDK->NameUndoEvent("probe-n8-open-event");
			gSDK->CreateLayer(inner.c_str(), kLayerDesign);
			probe.log("N8: イベントを開いてレイヤを作った。building=" + Building());

			RunVs(probe, vsEngine, VsBroken(), "N8(VS)");
			probe.log("N8: イベントの中で作ったレイヤ=" + Mark(LayerExistsByName(inner.c_str())) +
					  "（building が no なら **失敗の後始末がイベントを終わらせた**）");

			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
			{
				const Boolean removed = gSDK->EndAndRemoveUndoEvent();
				probe.log(std::string("N8: 後始末 EndAndRemoveUndoEvent() = ") +
						  (removed ? "true" : "false"));
			}
		}
	}

	// =====================================================================
	// N4) Python・print だけ・**ロガー無し**（既定引数の NULL）。
	// =====================================================================
	{
		Step step(probe, ledger, "N4", "Python・print だけ（ロガー無し）");
		if (!step.skip())
		{
			probe.log("★ N4: これから ExecuteScript(Python, print, ロガー無し) を呼ぶ");
			const Stopwatch watch;
			const VCOMError err = pyEngine->ExecuteScript(PyPrintOnly("no-logger"));
			LogResult(probe, "N4", err, watch.ms());
		}
	}

	// =====================================================================
	// N5) Python・print だけ・ロガーあり。
	// =====================================================================
	{
		Step step(probe, ledger, "N5", "Python・print だけ（ロガーあり）");
		if (!step.skip())
		{
			CDefaultPythonLogger logger;
			probe.log("★ N5: これから ExecuteScript(Python, print, ロガーあり) を呼ぶ");
			const Stopwatch watch;
			const VCOMError err = pyEngine->ExecuteScript(PyPrintOnly("with-logger"), &logger);
			LogResult(probe, "N5", err, watch.ms());
			LogPythonLogger(probe, "N5", logger);
		}
	}

	// =====================================================================
	// N6) 実行時エラーは戻り値に出るか。**vs を触らない**形で確かめる。
	// =====================================================================
	{
		Step step(probe, ledger, "N6", "Python・実行時エラー 1/0（vs を触らない）");
		if (!step.skip())
		{
			CDefaultPythonLogger logger;
			probe.log("★ N6: これから ExecuteScript(Python, 1/0) を呼ぶ");
			const Stopwatch watch;
			const VCOMError err = pyEngine->ExecuteScript(
				TXString("print('probe: before')\n1 / 0\nprint('probe: after')\n"), &logger);
			probe.log(std::string("N6: ExecuteScript VCOMError=") + std::to_string((long)err) +
					  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no") + Elapsed(watch.ms()) +
					  "（0 なら、実行時の失敗は戻り値では分からないということ）");
			LogPythonLogger(probe, "N6", logger);
		}
	}

	// =====================================================================
	// C4) ScriptContext 経由（4 度目に**通った**道）の追認。
	// =====================================================================
	{
		Step step(probe, ledger, "C4", "Python・ScriptContext で vs.Layer（通る道の追認）");
		if (!step.skip())
		{
			const std::string name = "probe-c4-py-layer";
			RunScriptContext(probe, pyEngine, PyLayerOnly(name), "C4", nullptr);
			probe.log("C4: 作られたか=" + Mark(LayerExistsByName(name.c_str())));
		}
	}

	// =====================================================================
	// C5) 同じ道で print。**この道でもロガーは働くか**（RunEx がロガーを取る）。
	//     4 度目は Begin にだけロガーを渡していて、stdout / stderr とも空だった。
	// =====================================================================
	{
		Step step(probe, ledger, "C5", "Python・ScriptContext で print（RunEx にロガー）");
		if (!step.skip())
		{
			CDefaultPythonLogger logger;
			RunScriptContext(probe, pyEngine, PyPrintOnly("script-context"), "C5", &logger);
		}
	}

	// =====================================================================
	// C6) **本題。通る道で Python から Undo は効くか。** VectorScript 版（N1）と同じ
	//     足跡で測るので、結果はそのまま比べられる。
	// =====================================================================
	{
		Step step(probe, ledger, "C6", "Python・ScriptContext で足跡つき Undo（本題）");
		if (!step.skip())
		{
			const std::string pre = "probe-c6-pre";
			const std::string m1 = "probe-c6-m1";
			const std::string m2 = "probe-c6-m2";
			gSDK->CreateLayer(pre.c_str(), kLayerDesign);
			probe.log("C6: 実行前 " + Marks(pre, m1, m2));
			CDefaultPythonLogger logger;
			RunScriptContext(probe, pyEngine, PyWithFootprints(m1, m2), "C6", &logger);
			probe.log("C6: 実行後 " + Marks(pre, m1, m2) +
					  "（VectorScript 版と同じ pre=有 m1=無 m2=有 なら、Python でも同じ挙動）");
		}
	}

	// =====================================================================
	// C1) **必ず最後に置く。** `ExecuteScript` から `vs.*` を呼ぶ道は 4 度目に 3 通り
	//     とも落ちた。ここは「落ちること」の記録であって、新しい問いではない。
	//     足跡簿に前回の記録があれば飛ばされる。
	// =====================================================================
	{
		Step step(probe, ledger, "C1", "Python・ExecuteScript で vs.Layer（落ちることの記録）");
		if (!step.skip())
		{
			const std::string name = "probe-c1-py-layer";
			probe.log("★ C1: これから ExecuteScript(Python, vs.Layer, ロガー無し) を呼ぶ。"
					  "**4 度目はここで落ちた**（ロガーの有無に関わらず落ちる）。"
					  "落ちても、ここまでの結果はもう取れている");
			const Stopwatch watch;
			const VCOMError err = pyEngine->ExecuteScript(PyLayerOnly(name));
			LogResult(probe, "C1", err, watch.ms());
			probe.log("C1: 作られたか=" + Mark(LayerExistsByName(name.c_str())) +
					  "（落ちなかった＝4 度目と違う。条件を洗い直すこと）");
		}
	}

	probe.log("=== 完了。後片付けはしない（新規の空図面で走らせる運用）。 ===");
}
