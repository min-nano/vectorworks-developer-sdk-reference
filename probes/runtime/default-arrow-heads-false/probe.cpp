//
//	probes/runtime/default-arrow-heads-false/probe.cpp
//
//	[issue #106] **`SetDefaultArrowHeadsN` にマーカーの有無（starting / ending）の
//	`false` を書いても戻らない**のは、**setter が `false` を無視している**からなのか、
//	**getter が嘘をついている**からなのかを切り分ける。あわせて、**マーカーの有無を
//	確実に書ける口があるか**（退避・復元が書けるか）を確かめる。
//
//	## 出どころ（#104 / PR #105 の実測ログ）
//
//	  | いつ | `GetDefaultArrowByClass()` | 読めた値 |
//	  | --- | --- | --- |
//	  | 走らせる前（触っていない新規の空図面） | no | start=no end=no style=0 size=0.1250 |
//	  | `SetDefaultArrowHeadsN(yes, **no**, 0, 0.2500)` の後 | yes | start=yes end=**yes** … |
//	  | `SetDefaultArrowHeadsN(no, no, 0, 0.1250)` の後 | yes | start=**yes** end=**yes** … |
//
//	`style` と `size` は 2 度とも書いたとおりに往復しているので**呼び出し自体は効いて
//	いる**。戻らないのは**真偽値 2 つだけ**である。
//
//	## この調査の設計（なぜこう書いたか）
//
//	1. **by-class の旗（`SetDefaultArrowByClass()`）は一切立てない。** #104 で
//	   「一度立てた旗は PIO を作らない限り下りない」と分かっているので、立てれば以降の
//	   読みがすべて「by-class のときの読み」と混ざる。**立てなければ**
//	   「`GetDefaultOpacityByClass` と同じ穴（by-class のときだけ読みが嘘）」の筋を
//	   まるごと除外できる。旗は毎回読んで、no のままであることを記録する。
//	2. **証人を 3 つ立てる。** 読み戻しが 1 本だけでは「setter が無視した」と
//	   「getter が嘘をついた」を区別できない。同じ状態を独立な 3 経路で読む:
//	     - **旧口**（`GetDefaultArrowHeadsN`）…… 食い違いを出している当人
//	     - **新口**（`GetDefaultBeginningMarker` / `GetDefaultEndMarker`）……
//	       `SMarkerStyle` と **`visibility`（有無）を別々に**持つ、今どきの口
//	     - **生まれたオブジェクト**…… 書いた直後に線を 1 本引き、その per-object の
//	       マーカーを読む。既定はオブジェクトが生まれる瞬間に読まれる（#94）ので、
//	       **実際に何が書かれていたか**の物証になる
//	   3 つが割れたら、割れ方そのものが答えになる。
//	3. **組み合わせごとに新しい空の図面を開く。** 「一度 yes にすると戻せない」なら、
//	   同じ図面で (yes,no) → (no,yes) と続けて測っても 2 つ目が汚れる。
//	   `OpenDocumentPath(nullptr, false)` は**コマンド実行中に呼べて**、
//	   `CloseDocument()` は**未保存でも確認ダイアログを出さずに閉じる**
//	   （[Findings「図面（ドキュメント）を開く・作る」](Findings/Documents.md)）ので、
//	   組み合わせごとに真っさらな既定から測り直せる。**利用者の図面の既定は
//	   1 度も書き換えない**（書き換えるのは、このプローブが開いて閉じる図面だけ）。
//	4. **新口で消せるかまで確かめる。** 旧口で消せないと分かっただけでは
//	   「じゃあどうするのか」が残る。新口（`SetDefaultBeginningMarker` /
//	   `SetDefaultEndMarker` の `visibility` 引数）で消せるなら、それが退避・復元の道。
//	5. **per-object も同じ電池を通す。** `SetArrowHeadsN(h, …)` /
//	   `SetObjBeginningMarker(h, …)` が同じ顔で並んでいるので、
//	   **文書の既定だけの穴か、マーカー一般の穴か**が分かる。
//
//	新規の空図面で走らせる（プローブは undo イベントを自分では開かない）。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// --- 書き出しの小道具 ------------------------------------------------

	const char* YesNo(Boolean value)
	{
		return value != 0 ? "yes" : "no";
	}

	std::string Num(long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%ld", value);
		return std::string(buffer);
	}

	std::string Real(double value)
	{
		char buffer[48];
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		return std::string(buffer);
	}

	// --- 証人 1: 旧口（食い違いを出している当人） -------------------------
	//
	// `GetDefaultArrowHeadsN(Boolean& starting, Boolean& ending, ArrowType& style,
	//  double_gs& size)`。真偽値 2 つ・様式・大きさを 1 度に返す。

	struct LegacyArrow
	{
		Boolean starting = 0;
		Boolean ending = 0;
		ArrowType style = 0;
		double_gs size = 0;
	};

	LegacyArrow ReadLegacyDefault()
	{
		LegacyArrow value;
		gSDK->GetDefaultArrowHeadsN(value.starting, value.ending, value.style, value.size);
		return value;
	}

	LegacyArrow ReadLegacyObject(MCObjectHandle object)
	{
		LegacyArrow value;
		gSDK->GetArrowHeadsN(object, value.starting, value.ending, value.style, value.size);
		return value;
	}

	std::string Describe(const LegacyArrow& value)
	{
		return std::string("start=") + YesNo(value.starting) + " end=" + YesNo(value.ending) +
			   " style=" + Num(static_cast<long>(value.style)) +
			   " size=" + Real(static_cast<double>(value.size));
	}

	// --- 証人 2: 新口（`SMarkerStyle` ＋ `visibility`） --------------------
	//
	// `GetDefaultBeginningMarker(SMarkerStyle& mstyle, Boolean& visibility)` /
	// `GetDefaultEndMarker(…)`。**有無（visibility）を様式（style）と別に持つ**のが
	// 旧口との違いで、そこがこの調査の肝になる。戻り値 Boolean は成否（らしい）だが、
	// **setter の戻り値は信じない**（[Findings「調査の作法」](Findings/Investigation%20Techniques.md)）
	// ので、値は必ず読み戻して判断する。

	struct MarkerEnd
	{
		Boolean ok = 0;			// 呼び出しの戻り値（参考。判断には使わない）
		Boolean visibility = 0; // マーカーが付いているか
		SMarkerStyle style{};
	};

	std::string Describe(const MarkerEnd& end)
	{
		return std::string("visible=") + YesNo(end.visibility) +
			   " style=" + Num(static_cast<long>(end.style.style)) +
			   " size=" + Real(end.style.dSize) + " width=" + Real(end.style.dWidth) +
			   " angle=" + Num(static_cast<long>(end.style.nAngle)) + " (ret=" + YesNo(end.ok) +
			   ")";
	}

	MarkerEnd ReadModernDefaultBeginning()
	{
		MarkerEnd end;
		end.ok = gSDK->GetDefaultBeginningMarker(end.style, end.visibility);
		return end;
	}

	MarkerEnd ReadModernDefaultEnd()
	{
		MarkerEnd end;
		end.ok = gSDK->GetDefaultEndMarker(end.style, end.visibility);
		return end;
	}

	MarkerEnd ReadModernObjectBeginning(MCObjectHandle object)
	{
		MarkerEnd end;
		end.ok = gSDK->GetObjBeginningMarker(object, end.style, end.visibility);
		return end;
	}

	MarkerEnd ReadModernObjectEnd(MCObjectHandle object)
	{
		MarkerEnd end;
		end.ok = gSDK->GetObjEndMarker(object, end.style, end.visibility);
		return end;
	}

	// --- 証人 3: 生まれたオブジェクト ------------------------------------
	//
	// 既定はオブジェクトが生まれる瞬間に読まれる（#94）。だから「書いた直後に線を
	// 1 本引いて、その線のマーカーを読む」と、**そのとき既定に本当に入っていた値**が
	// 分かる。getter の嘘は、ここには写らない（別の経路で読むため）。

	int gSerial = 0;

	MCObjectHandle CreateWitnessLine()
	{
		const WorldCoord x = static_cast<WorldCoord>(gSerial++) * 1000;
		return gSDK->CreateLine(WorldPt(x, 0), WorldPt(x, 1000));
	}

	// --- 3 つの証人を 1 度に書き出す --------------------------------------

	void LogDefaultWitnesses(vwprobe::Report& probe, const std::string& label)
	{
		const LegacyArrow legacy = ReadLegacyDefault();
		const MarkerEnd beginning = ReadModernDefaultBeginning();
		const MarkerEnd ending = ReadModernDefaultEnd();

		probe.log("**" + label + "**");
		probe.log("");
		probe.log("| 証人 | 読めた値 |");
		probe.log("| --- | --- |");
		probe.log("| 旧口 `GetDefaultArrowHeadsN` | " + Describe(legacy) + " |");
		probe.log("| 新口 `GetDefaultBeginningMarker` | " + Describe(beginning) + " |");
		probe.log("| 新口 `GetDefaultEndMarker` | " + Describe(ending) + " |");

		MCObjectHandle line = CreateWitnessLine();
		if (line == nil)
		{
			probe.log("| 直後に引いた線 | **引けなかった（nil）** |");
		}
		else
		{
			probe.log("| 直後に引いた線（旧口） | " + Describe(ReadLegacyObject(line)) + " |");
			probe.log("| 直後に引いた線（新口・始点） | " +
					  Describe(ReadModernObjectBeginning(line)) + " |");
			probe.log("| 直後に引いた線（新口・終点） | " + Describe(ReadModernObjectEnd(line)) +
					  " |");
		}
		probe.log("| 既定の by-class の旗 | " + std::string(YesNo(gSDK->GetDefaultArrowByClass())) +
				  "（この調査では一度も立てない） |");
		probe.log("");
	}

	// --- 図面を開く・閉じる ----------------------------------------------
	//
	// 組み合わせごとに真っさらな既定から測り直すため。**利用者の図面は閉じない**
	// ——閉じる前に「いま開いている件数が、自分が開いた分だけ増えているか」を必ず見る。

	size_t CountOpenDocuments()
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		return static_cast<size_t>(files.GetSize());
	}

	// 新しい空の図面を開く。開けたら true。**開けなかったら、以降はいまの図面で
	// 測るしかない**（前の節の状態を引きずるので、読み手にそう伝える）。
	bool OpenFreshDocument(vwprobe::Report& probe, size_t& outCountBefore)
	{
		outCountBefore = CountOpenDocuments();
		const bool opened = gSDK->OpenDocumentPath(nullptr, false);
		const size_t after = CountOpenDocuments();
		if (!opened || after != outCountBefore + 1)
		{
			probe.log("※ **新しい空の図面を開けなかった**（戻り値=" +
					  std::string(opened ? "true" : "false") + " 件数 " +
					  Num(static_cast<long>(outCountBefore)) + " → " +
					  Num(static_cast<long>(after)) +
					  "）。この節はいまの図面で測る——**前の節の状態を引きずっている**ので、"
					  "はじめの行が『触っていない既定』でないなら、そのつもりで読むこと。");
			probe.log("");
			return false;
		}
		gSerial = 0; // 新しい図面なので、線を引く位置も原点から
		return true;
	}

	// 開いた図面を閉じる。**件数が開く前へ戻ったときだけ成功と見る**
	// （`CloseDocument()` は閉じていても false を返す。Findings「図面…」）。
	void CloseFreshDocument(vwprobe::Report& probe, size_t countBefore)
	{
		const size_t before = CountOpenDocuments();
		if (before != countBefore + 1)
		{
			probe.fail(
				"閉じる前の図面の件数が合わない（開く前=" + Num(static_cast<long>(countBefore)) +
				" いま=" + Num(static_cast<long>(before)) +
				"）。**利用者の図面を閉じないため、閉じるのをやめる**");
			return;
		}
		gSDK->CloseDocument();
		const size_t after = CountOpenDocuments();
		if (after != countBefore)
		{
			probe.fail("開いた図面を閉じられなかった（件数 " + Num(static_cast<long>(before)) +
					   " → " + Num(static_cast<long>(after)) + "）");
		}
	}

	// --- 1 つの組み合わせを通しで測る ------------------------------------
	//
	//   ① 真っさらな既定を読む
	//   ② 旧口で (starting, ending) を書いて読む        ← 書いたとおりに入るか
	//   ③ 旧口で (no, no) を書いて読む                  ← 消せるか
	//   ④ 新口の visibility=false で消して読む          ← 新口なら消せるか
	//
	// ③ では **size をわざと変える**。真偽値が戻らなくても size が変われば
	// 「呼び出しは届いている（無視されたのは真偽値だけ）」と言い切れるため。

	void RunCombination(vwprobe::Report& probe, const char* heading, Boolean starting,
						Boolean ending)
	{
		probe.log(std::string("## ") + heading);
		probe.log("");

		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		LogDefaultWitnesses(probe, "① 触っていない既定（この図面の出発点）");

		const LegacyArrow start = ReadLegacyDefault();

		gSDK->SetDefaultArrowHeadsN(starting, ending, start.style, start.size);
		LogDefaultWitnesses(
			probe, std::string("② `SetDefaultArrowHeadsN(start=") + YesNo(starting) +
					   ", end=" + YesNo(ending) + ", style=" + Num(static_cast<long>(start.style)) +
					   ", size=" + Real(static_cast<double>(start.size)) + ")` を書いた後");

		// size を変えて (no, no) を書く。真偽値が戻らなくても size が変われば、
		// 呼び出しそのものは届いている。
		const double_gs otherSize = start.size > 0 ? start.size * 2 : 3;
		gSDK->SetDefaultArrowHeadsN(static_cast<Boolean>(0), static_cast<Boolean>(0), start.style,
									otherSize);
		LogDefaultWitnesses(probe,
							std::string("③ `SetDefaultArrowHeadsN(start=no, end=no, style=") +
								Num(static_cast<long>(start.style)) +
								", size=" + Real(static_cast<double>(otherSize)) +
								")` を書いた後（**消せるか**。size は変えてある）");

		// 新口で消す。style はいま入っているものをそのまま書き戻し、
		// **visibility だけを false にする**。
		{
			MarkerEnd beginning = ReadModernDefaultBeginning();
			MarkerEnd finish = ReadModernDefaultEnd();
			const Boolean retBeginning =
				gSDK->SetDefaultBeginningMarker(beginning.style, static_cast<Boolean>(0));
			const Boolean retEnd = gSDK->SetDefaultEndMarker(finish.style, static_cast<Boolean>(0));
			LogDefaultWitnesses(
				probe, std::string("④ 新口で `visibility=false` を書いた後（戻り値: 始点=") +
						   YesNo(retBeginning) + " 終点=" + YesNo(retEnd) + "）");
		}

		if (fresh)
		{
			CloseFreshDocument(probe, countBefore);
		}
	}
} // namespace

VW_PROBE("default-arrow-heads-false", "マーカーの有無に false を書けるかを実測する",
		 "`SetDefaultArrowHeadsN` に false を書いても戻らないのは setter か getter かを、"
		 "新口（visibility）と生まれたオブジェクトを証人に立てて切り分ける。"
		 "per-object と、確実に書ける口があるかも測る")
{
	probe.log("**この調査は `SetDefaultArrowByClass()` を一度も呼ばない。**"
			  "#104 で「一度立てた旗は PIO を作らない限り下りない」と分かっているので、"
			  "立てなければ「by-class のときだけ読みが嘘をつく」筋をまるごと除外できる。"
			  "旗は毎回読んで、no のままであることを記録する。");
	probe.log("");
	probe.log("**利用者の図面の既定は書き換えない。** 既定を書くのは、このプローブが"
			  "自分で開いて自分で閉じる空の図面の中だけである。");
	probe.log("");

	probe.log("## 0. 走らせている図面の素性");
	probe.log("");
	probe.log("開いている図面の件数: " + Num(static_cast<long>(CountOpenDocuments())));
	probe.log("");
	LogDefaultWitnesses(probe, "この図面の既定（読むだけ。書き換えない）");

	// =====================================================================
	// 1〜3. 組み合わせごとに、真っさらな既定から測る。
	// =====================================================================
	RunCombination(probe, "1. 始点だけ点ける (start=yes, end=no)", static_cast<Boolean>(1),
				   static_cast<Boolean>(0));
	RunCombination(probe, "2. 終点だけ点ける (start=no, end=yes)", static_cast<Boolean>(0),
				   static_cast<Boolean>(1));
	RunCombination(probe, "3. 両端を点ける (start=yes, end=yes)", static_cast<Boolean>(1),
				   static_cast<Boolean>(1));

	// =====================================================================
	// 4. per-object も同じ電池を通す。**文書の既定だけの穴か、マーカー一般の穴か。**
	//    オブジェクトは 1 本ごとに新しく引けるので、図面を開き直さなくても
	//    真っさらな出発点が手に入る。
	// =====================================================================
	probe.log(
		"## 4. per-object も同じか（`SetArrowHeadsN(h, …)` / `SetObjBeginningMarker(h, …)`）");
	probe.log("");
	probe.log("**線を 1 本ごとに新しく引く**ので、組み合わせごとに真っさらな出発点から測れる"
			  "（図面を開き直さなくてよい）。");
	probe.log("");

	size_t objectCountBefore = 0;
	const bool objectFresh = OpenFreshDocument(probe, objectCountBefore);

	{
		probe.log("| 線 | 何をした後 | 旧口 `GetArrowHeadsN` | 新口・始点 | 新口・終点 |");
		probe.log("| --- | --- | --- | --- | --- |");

		const Boolean kOff = static_cast<Boolean>(0);
		const Boolean kOn = static_cast<Boolean>(1);
		const Boolean combos[3][2] = {{kOn, kOff}, {kOff, kOn}, {kOn, kOn}};

		for (int i = 0; i < 3; ++i)
		{
			const Boolean wantStart = combos[i][0];
			const Boolean wantEnd = combos[i][1];
			const std::string name = "線 " + Num(static_cast<long>(i + 1));

			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + name + " | — | **引けなかった（nil）** | | |");
				continue;
			}

			const LegacyArrow born = ReadLegacyObject(line);
			probe.log("| " + name + " | 引いた直後 | " + Describe(born) + " | " +
					  Describe(ReadModernObjectBeginning(line)) + " | " +
					  Describe(ReadModernObjectEnd(line)) + " |");

			gSDK->SetArrowHeadsN(line, wantStart, wantEnd, born.style, born.size);
			probe.log("| " + name + " | `SetArrowHeadsN(start=" + YesNo(wantStart) +
					  ", end=" + YesNo(wantEnd) + ")` | " + Describe(ReadLegacyObject(line)) +
					  " | " + Describe(ReadModernObjectBeginning(line)) + " | " +
					  Describe(ReadModernObjectEnd(line)) + " |");

			const double_gs otherSize = born.size > 0 ? born.size * 2 : 3;
			gSDK->SetArrowHeadsN(line, kOff, kOff, born.style, otherSize);
			probe.log("| " + name + " | `SetArrowHeadsN(start=no, end=no, size=" +
					  Real(static_cast<double>(otherSize)) + ")`（**消せるか**） | " +
					  Describe(ReadLegacyObject(line)) + " | " +
					  Describe(ReadModernObjectBeginning(line)) + " | " +
					  Describe(ReadModernObjectEnd(line)) + " |");

			MarkerEnd beginning = ReadModernObjectBeginning(line);
			MarkerEnd finish = ReadModernObjectEnd(line);
			const Boolean retBeginning = gSDK->SetObjBeginningMarker(line, beginning.style, kOff);
			const Boolean retEnd = gSDK->SetObjEndMarker(line, finish.style, kOff);
			probe.log("| " + name +
					  " | 新口で `visibility=false`（戻り値: 始点=" + YesNo(retBeginning) +
					  " 終点=" + YesNo(retEnd) + "） | " + Describe(ReadLegacyObject(line)) +
					  " | " + Describe(ReadModernObjectBeginning(line)) + " | " +
					  Describe(ReadModernObjectEnd(line)) + " |");
		}
		probe.log("");
	}

	// =====================================================================
	// 5. 新口だけで点けられるか。**旧口を一度も通さずに**マーカーを点け、消す。
	//    「旧口を通した瞬間に壊れる」のか「マーカーの有無そのものが片道」なのかが
	//    ここで分かれる。
	// =====================================================================
	probe.log("## 5. 旧口を一度も通さずに、新口だけで点けて消す");
	probe.log("");
	probe.log("4 節までは必ず旧口（`SetArrowHeadsN`）を先に通している。**旧口を通さなければ"
			  "消せる**なら、壊しているのは旧口の書き込みである。**新口だけでも消せない**なら、"
			  "マーカーの有無そのものが片道だということになる。");
	probe.log("");
	{
		probe.log("| 何をした後 | 旧口 `GetArrowHeadsN` | 新口・始点 | 新口・終点 |");
		probe.log("| --- | --- | --- | --- |");

		MCObjectHandle line = CreateWitnessLine();
		if (line == nil)
		{
			probe.log("| — | **線を引けなかった（nil）** | | |");
		}
		else
		{
			probe.log("| 引いた直後 | " + Describe(ReadLegacyObject(line)) + " | " +
					  Describe(ReadModernObjectBeginning(line)) + " | " +
					  Describe(ReadModernObjectEnd(line)) + " |");

			MarkerEnd beginning = ReadModernObjectBeginning(line);
			MarkerEnd finish = ReadModernObjectEnd(line);
			gSDK->SetObjBeginningMarker(line, beginning.style, static_cast<Boolean>(1));
			gSDK->SetObjEndMarker(line, finish.style, static_cast<Boolean>(1));
			probe.log("| 新口で `visibility=true`（点ける） | " + Describe(ReadLegacyObject(line)) +
					  " | " + Describe(ReadModernObjectBeginning(line)) + " | " +
					  Describe(ReadModernObjectEnd(line)) + " |");

			beginning = ReadModernObjectBeginning(line);
			finish = ReadModernObjectEnd(line);
			gSDK->SetObjBeginningMarker(line, beginning.style, static_cast<Boolean>(0));
			gSDK->SetObjEndMarker(line, finish.style, static_cast<Boolean>(0));
			probe.log("| 新口で `visibility=false`（消す） | " + Describe(ReadLegacyObject(line)) +
					  " | " + Describe(ReadModernObjectBeginning(line)) + " | " +
					  Describe(ReadModernObjectEnd(line)) + " |");
		}
		probe.log("");
	}

	if (objectFresh)
	{
		CloseFreshDocument(probe, objectCountBefore);
	}

	// =====================================================================
	// 締め: 利用者の図面へ戻ってきたことを確かめる。
	// =====================================================================
	probe.log("## 6. 締め");
	probe.log("");
	probe.log("開いている図面の件数: " + Num(static_cast<long>(CountOpenDocuments())) +
			  "（0 節と同じなら、開いた図面はすべて閉じられている）");
	probe.log("");
	LogDefaultWitnesses(probe, "いまアクティブな図面の既定（0 節と同じなら書き換えていない）");
	probe.log("**上の行で線が 1 本引かれている**——証人を立てるために必ず 1 本引くため。"
			  "利用者の図面に残るのはこの 1 本だけで、既定は書き換えていない。");
	probe.log("");

	probe.log("## 読み方");
	probe.log("");
	probe.log("**1〜3 節の ② の行**が主役である。");
	probe.log("");
	probe.log("- **旧口・新口・生まれた線の 3 つが書いたとおり**なら、`false` は正しく"
			  "書けており、#104 で見た食い違いは **by-class の旗が立っていたこと**に"
			  "由来する（＝ getter の嘘。`GetDefaultOpacityByClass` と同じ穴）。");
	probe.log("- **旧口だけが `yes` を返し、新口と線が `no`** なら、**旧口の getter が"
			  "嘘をついている**。書き込みは効いているので、読むときは新口を使えばよい。");
	probe.log("- **3 つとも `yes`** なら、**setter が `false` を無視している**"
			  "（片方でも点ければ両端が点く、という筋もここに入る——② で "
			  "(yes,no) を書いて両端が yes になるかで分かる）。");
	probe.log("- **③ で size だけが変わって真偽値が戻らない**なら、呼び出しは届いている"
			  "——無視されているのは真偽値だけである。");
	probe.log("- **④ または 5 節で消せる**なら、それが退避・復元の道になる。");
}
