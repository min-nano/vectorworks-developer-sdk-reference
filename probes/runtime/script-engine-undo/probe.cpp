//
//	probes/runtime/script-engine-undo/probe.cpp
//
//	[issue #39] スクリプトエンジン経由の Undo。**書き残していた 2 項目だけを確かめる。**
//
//	#39 はいったん閉じたが、**この issue が確かめると決めた項目のうち 2 つが
//	Findings に書かれていない**ことに気付いて開き直した。残っているのは次の 2 つで、
//	それ以外（取り消しの対象・呼べる文脈・失敗の判別・Python 経路・ダイアログの出所）は
//	[Findings/Undo.md](../../../Findings/Undo.md) の「間接経路: スクリプトエンジン経由で
//	DoMenuTextByName 相当を呼ぶ」に実測で書いてある。
//
//	  M1 **連続で呼んだら複数段戻るか**（元の問い C）。**実機で測れていない。**
//	     1 度目のプローブは連続 2 回呼んでいたが、判定に **C++ が作ったマーカー**を
//	     使っていた——いまは「C++ が undo イベントの外で作ったものは取り消しスタックに
//	     載らない」と分かっているので、**あの観測は 1 段目すら測れていない**（何回
//	     呼んでも同じに見える）。**スクリプト自身に足跡を 3 つ作らせ、Undo を 3 回挟む。**
//	  M2 **`ExecuteScript` は同期的か**（元の問い 1）。答えは出ているが書いていない。
//	     ここでは**呼び出しから戻った直後にレイヤの有無を読む**という形で、その 1 点に
//	     絞って残す。
//	  M3 M1 と同じことを **Python（ScriptContext 経由）**でも見る。1 段のときは
//	     VectorScript 版と挙動が同じだったので、複数段でも同じかを確かめる。
//
//	**落ちる経路（IPythonScriptEngine::ExecuteScript から vs.* を呼ぶ）には触らない。**
//	`CompileScript` も呼ばない（`showDialogs=false` でも成功のダイアログが毎回出るため）。
//	わざと失敗するスクリプトも走らせない。**ダイアログは出ない見込みである。**
//
//	【M1 の読み方】足跡は a → b → c の順にスクリプトが作り、そのあと Undo を 3 回呼び、
//	最後に d を作る。d が有ならスクリプトは最後まで走っている。消えた足跡の数がそのまま
//	戻れた段数になる。
//
//	  a=有 b=有 c=無 → **1 段で頭打ち**（2 回目以降の Undo は何もしていない）
//	  a=有 b=無 c=無 → **2 段まで戻る**
//	  a=無 b=無 c=無 → **3 段戻る**（頭打ちが見えないので、次は回数を増やして測る）
//	  pre は C++ が作るので、**どの場合でも有のまま**のはず（既に確定している性質の再確認）
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Scripting/IPythonScriptEngine.h"
#include "Interfaces/VectorWorks/Scripting/IVectorScriptEngine.h"
#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <chrono>
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

	std::string Mark(const std::string& name)
	{
		return LayerExistsByName(name.c_str()) ? "有" : "無";
	}

	// モーダルダイアログが出れば人が閉じるまで戻らないので、秒単位になる（5 度目の
	// 実測: 失敗するスクリプトの ExecuteScript が 18108ms、他は 0〜17ms）。
	// このプローブでは**出ない見込み**なので、出たらそれ自体が知見である。
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

	std::string Elapsed(long ms)
	{
		std::string s = " elapsed=" + std::to_string(ms) + "ms";
		if (ms >= 1000)
			s += " ← **ここで人を待っていた（ダイアログが出た）**";
		return s;
	}

	// 足跡を 3 つ作り、Undo を 3 回呼び、最後にもう 1 つ作る。
	const char* kA = "probe-step-a";
	const char* kB = "probe-step-b";
	const char* kC = "probe-step-c";
	const char* kD = "probe-step-d";

	TXString VsThreeUndos()
	{
		return TXString("PROCEDURE __ProbeSeSteps;\n"
						"BEGIN\n"
						"\tLayer('probe-step-a');\n"
						"\tLayer('probe-step-b');\n"
						"\tLayer('probe-step-c');\n"
						"\tDoMenuTextByName('Undo', 0);\n"
						"\tDoMenuTextByName('Undo', 0);\n"
						"\tDoMenuTextByName('Undo', 0);\n"
						"\tLayer('probe-step-d');\n"
						"END;\n"
						"Run(__ProbeSeSteps);\n");
	}

	TXString PyThreeUndos()
	{
		return TXString("import vs\n"
						"vs.Layer('probe-py-step-a')\n"
						"vs.Layer('probe-py-step-b')\n"
						"vs.Layer('probe-py-step-c')\n"
						"vs.DoMenuTextByName('Undo', 0)\n"
						"vs.DoMenuTextByName('Undo', 0)\n"
						"vs.DoMenuTextByName('Undo', 0)\n"
						"vs.Layer('probe-py-step-d')\n");
	}

	TXString VsOneLayer(const std::string& name)
	{
		const std::string src = "PROCEDURE __ProbeSeOne;\n"
								"BEGIN\n"
								"\tLayer('" +
								name +
								"');\n"
								"END;\n"
								"Run(__ProbeSeOne);\n";
		return TXString(src.c_str());
	}

	std::string Steps(const char* a, const char* b, const char* c, const char* d)
	{
		return std::string("a=") + Mark(a) + " b=" + Mark(b) + " c=" + Mark(c) + " d=" + Mark(d);
	}

	// 消えた足跡の数＝戻れた段数。d はスクリプトが最後まで走った印なので数えない。
	std::string Readback(const char* a, const char* b, const char* c)
	{
		int undone = 0;
		if (!LayerExistsByName(a))
			++undone;
		if (!LayerExistsByName(b))
			++undone;
		if (!LayerExistsByName(c))
			++undone;
		std::string s = "戻れた段数=" + std::to_string(undone);
		if (undone == 0)
			s += "（Undo が 1 度も効いていない。1 段のときと食い違うので条件を疑う）";
		else if (undone == 1)
			s += "（**1 段で頭打ち**。2 回目以降の Undo は何もしていない）";
		else if (undone == 3)
			s += "（**3 段とも戻った**。頭打ちが見えないので、次は回数を増やして測る）";
		else
			s += "（**" + std::to_string(undone) + " 段まで戻る**）";
		return s;
	}
} // namespace

VW_PROBE("script-engine-undo", "スクリプトエンジン経由の Undo・複数段と同期性",
		 "連続で呼んだら複数段戻るのか（足跡を 3 つ作らせて Undo を 3 回）と、"
		 "ExecuteScript が同期的かを確かめる。落ちる経路には触らない")
{
	IVectorScriptEnginePtr vsEngine(IID_VectorScriptEngine);
	IPythonScriptEnginePtr pyEngine(IID_PythonScriptEngine);
	if (!vsEngine || !pyEngine)
	{
		probe.fail("スクリプトエンジンを取得できなかった（VCOMPtr が空）");
		return;
	}
	probe.log("このプローブはダイアログを出さない見込みです（CompileScript を呼ばず、"
			  "失敗するスクリプトも走らせません）。**出たらチャットで教えてください**");

	// =====================================================================
	// M2) 同期性。**戻った直後に読む**——待たない。
	//     戻った時点で結果が見えているなら、呼び出しは同期的である。
	// =====================================================================
	probe.log("=== M2) ExecuteScript は同期的か（戻った直後に読む） ===");
	{
		const std::string name = "probe-sync";
		probe.log("M2: 実行前 " + name + "=" + Mark(name));
		const Stopwatch watch;
		const VCOMError err = vsEngine->ExecuteScript(VsOneLayer(name));
		// **ここで待たない。** 戻った直後の 1 行目でそのまま読む。
		const std::string after = Mark(name);
		probe.log(std::string("M2: ExecuteScript VCOMError=") + std::to_string((long)err) +
				  Elapsed(watch.ms()));
		probe.log("M2: 戻った直後 " + name + "=" + after +
				  "（有なら、戻った時点でスクリプトは走り終わっている＝同期的）");
	}

	// =====================================================================
	// M1) 複数段（VectorScript）。**この issue の書き残し。**
	// =====================================================================
	probe.log("=== M1) VectorScript・足跡 3 つに Undo 3 回 ===");
	{
		const std::string pre = "probe-step-pre";
		gSDK->CreateLayer(pre.c_str(), kLayerDesign);
		probe.log("M1: 実行前 pre=" + Mark(pre) + " " + Steps(kA, kB, kC, kD));

		const Stopwatch watch;
		const VCOMError err = vsEngine->ExecuteScript(VsThreeUndos());
		probe.log(std::string("M1: ExecuteScript VCOMError=") + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no") + Elapsed(watch.ms()));

		probe.log("M1: 実行後 pre=" + Mark(pre) + " " + Steps(kA, kB, kC, kD));
		probe.log("M1: " + Readback(kA, kB, kC) + " / d=" + Mark(kD) +
				  "（d=有 ならスクリプトは最後まで走っている）");
		probe.log("M1: pre=" + Mark(pre) +
				  "（有のままなら、C++ の作り物は何段戻っても取り消されない）");
	}

	// =====================================================================
	// M3) 同じことを Python（ScriptContext 経由）でも。1 段のときは VectorScript 版と
	//     同じ挙動だったので、複数段でも揃うかを見る。
	//     **ExecuteScript は使わない**（vs.* を呼ぶと落ちるため）。
	// =====================================================================
	probe.log("=== M3) Python（ScriptContext）・足跡 3 つに Undo 3 回 ===");
	{
		const char* a = "probe-py-step-a";
		const char* b = "probe-py-step-b";
		const char* c = "probe-py-step-c";
		const char* d = "probe-py-step-d";
		probe.log(std::string("M3: 実行前 ") + Steps(a, b, c, d));

		CDefaultPythonLogger logger;
		const Stopwatch watch;
		const VCOMError berr = pyEngine->ScriptContext_Begin(PyThreeUndos(), &logger);
		const VCOMError rerr = pyEngine->ScriptContext_RunEx(&logger);
		probe.log(std::string("M3: ScriptContext_Begin=") + std::to_string((long)berr) +
				  " ScriptContext_RunEx=" + std::to_string((long)rerr) +
				  " succeeded=" + (VCOM_SUCCEEDED(rerr) ? "yes" : "no") + Elapsed(watch.ms()));
		const std::string out = static_cast<const char*>(logger.fOutput);
		const std::string errs = static_cast<const char*>(logger.fErrors);
		probe.log(std::string("M3: stdout=[") + (out.empty() ? "空" : out) + "]");
		probe.log(std::string("M3: stderr=[") + (errs.empty() ? "空" : errs) + "]");

		probe.log(std::string("M3: 実行後 ") + Steps(a, b, c, d));
		probe.log("M3: " + Readback(a, b, c) + " / d=" + Mark(d) +
				  "（M1 と同じ段数なら、エンジンによる違いは無い）");
	}

	probe.log("=== 完了。後片付けはしない（新規の空図面で走らせる運用）。 ===");
}
