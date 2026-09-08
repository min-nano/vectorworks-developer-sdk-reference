//
//	probes/runtime/script-engine-undo/probe.cpp
//
//	[issue #39] スクリプトエンジン経由（IVectorScriptEngine / IPythonScriptEngine）で
//	DoMenuTextByName('Undo', 0) を呼んだときの挙動を確かめる。
//
//	【6 度目。**残っているのは 1 つだけ**である】
//
//	5 度目までで issue の問いはあらかた片付いた（結論は
//	[Findings/Undo.md](../../../Findings/Undo.md) の「間接経路: スクリプトエンジン経由で
//	DoMenuTextByName 相当を呼ぶ」に書いた）:
//
//	  ・VectorScript 経由の Undo は到達し、取り消しが 1 段掛かる（pre=有 m1=無 m2=有）。
//	  ・C++ が undo イベントの外で作ったものは取り消しスタックに載らない。
//	  ・Python の `ScriptContext` 経由も**まったく同じ挙動**。
//	  ・`ExecuteScript` から `vs.*` を呼ぶと落ちる（`ScriptContext` なら通る）。
//	  ・開いた undo イベントを終わらせるのは **取り消しの実行**——`ExecuteScript` でも
//	    失敗の後始末でもない（同じ形でスクリプトだけ 3 通りに差し替えて確定）。
//
//	**残るのは「コンパイルに成功しました」というダイアログを出しているのは誰か。**
//	5 度目は `CompileScript(showDialogs=false)` の所要が 1112ms（他は 0〜17ms）で、
//	この呼び出しが疑わしい。**が、そこは走行中で最初の VectorScript エンジン呼び出しでも
//	ある**——初回の初期化に 1 秒掛かっただけ、という読み方が消えていない。
//
//	【この版の作り】**同じ `CompileScript` を続けて 3 回呼び、所要を並べる**。
//
//	  ・2 回目・3 回目も 1 秒級 → **毎回ダイアログを出している**（`showDialogs=false` は
//	    成功のダイアログを抑えない）。
//	  ・2 回目・3 回目が 0ms 級 → **1 回目の 1112ms は初期化**。ダイアログの出所は
//	    `CompileScript` ではないので、`ExecuteScript` 側を疑う（D2 で測る）。
//
//	D2 では `ExecuteScript` を 3 回呼んで同じように並べる。**どちらの表に 1 秒級が
//	並ぶかで出所が決まる。** 利用者に要るのは「ダイアログが出たら閉じる」だけで、
//	どこで出たかを見ている必要は無い。
//
//	【走らせる人へ】このプローブは**わざと失敗するスクリプトを走らせない**ので、
//	出るとすれば「コンパイルに成功しました」だけである（何度か出るかもしれない）。
//	**出たら閉じてください。**
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Scripting/IVectorScriptEngine.h"

#include <chrono>
#include <string>

namespace
{
	using namespace VectorWorks::Scripting;

	// **どの呼び出しがダイアログを出したかを、目視なしで突き止めるための時計。**
	// モーダルダイアログは人が閉じるまで戻らないので、**その呼び出しだけが秒単位で
	// 掛かる**（5 度目の実測: 失敗するスクリプトの ExecuteScript が 18108ms、他は 0〜17ms）。
	// 画面にコードの位置は出ないので、**人に「どの行で出ましたか」とは訊けない**。
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
			s += " ← **ここで人を待っていた**";
		return s;
	}

	// 何も作らず、失敗もしないスクリプト。**測りたいのは呼び出しの所要時間だけ**なので、
	// 図面を触らないものにしてある（変数への代入だけ）。
	TXString VsHarmless()
	{
		return TXString("PROCEDURE __ProbeSeNoop;\n"
						"VAR i : INTEGER;\n"
						"BEGIN\n"
						"\ti := 1;\n"
						"END;\n"
						"Run(__ProbeSeNoop);\n");
	}
} // namespace

VW_PROBE("script-engine-undo", "スクリプトエンジン経由で Undo メニューを呼ぶ（6 度目）",
		 "「コンパイルに成功しました」のダイアログを出しているのが CompileScript なのか "
		 "ExecuteScript なのかを、同じ呼び出しを 3 回ずつ並べた所要時間で決める")
{
	IVectorScriptEnginePtr vsEngine(IID_VectorScriptEngine);
	if (!vsEngine)
	{
		probe.fail("IVectorScriptEngine を取得できなかった（VCOMPtr が空）");
		return;
	}

	probe.log("**ダイアログが出たら閉じてください。** 何度か出るかもしれません——"
			  "何回目に出たかを覚えている必要はありません（下の elapsed= で分かります）");
	probe.log("読み方: 1 秒級が並ぶほうがダイアログの出所。**1 回目だけが 1 秒級なら、"
			  "それは初期化であってダイアログではない**");

	const TXString script = VsHarmless();

	// =====================================================================
	// D1) CompileScript を 3 回。**1 回目だけが遅いなら初期化、毎回遅いならダイアログ。**
	// =====================================================================
	probe.log("=== D1) CompileScript(showDialogs=false) を 3 回 ===");
	for (int i = 1; i <= 3; ++i)
	{
		bool ok = false;
		Sint32 line = -1;
		TXString errorText;
		const Stopwatch watch;
		const VCOMError err = vsEngine->CompileScript(script, false, ok, &line, &errorText);
		probe.log("D1-" + std::to_string(i) + ": CompileScript VCOMError=" +
				  std::to_string((long)err) + " ok=" + (ok ? "yes" : "no") + Elapsed(watch.ms()));
	}

	// =====================================================================
	// D2) ExecuteScript を 3 回。D1 が全部速ければ、出所はこちらということになる。
	// =====================================================================
	probe.log("=== D2) ExecuteScript を 3 回 ===");
	for (int i = 1; i <= 3; ++i)
	{
		const Stopwatch watch;
		const VCOMError err = vsEngine->ExecuteScript(script);
		probe.log("D2-" + std::to_string(i) +
				  ": ExecuteScript VCOMError=" + std::to_string((long)err) +
				  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no") + Elapsed(watch.ms()));
	}

	probe.log("=== 完了。1 秒級が並んだほうがダイアログの出所である。 ===");
}
