//
//	probes/runtime/marker-angle-order/probe.cpp
//
//	[issue #116] **様式（`MarkerType`）と角度（`nAngle`）を、両方とも狙いどおりに
//	書く手順はあるのか。** #113（[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/115#issuecomment-5822033817)）で
//	確定したのは次の 2 つで、**この 2 つを組み合わせたときどうなるかは測っていない**:
//
//	  * **角度を書けるのは中口（`SetMarker` の `angle`）だけ。** 新口
//	    （`SMarkerStyle.nAngle`）は 0 / 5 / 15 / 30 / 45 / 60 / 90 / 120 / 180 の
//	    9 点すべてで、読み戻すと `1` になった。
//	  * **`MarkerType` をそのまま書けるのは新口（`SetObjBeginningMarker`）だけ。**
//	    中口の `SetMarker` は宣言が `MarkerType` でも、実際に受けるのは
//	    `EMarkerType` の番号 0〜6（`0`→`0` / `1`→`256` / `2`→`1280` / `3`→`2` /
//	    `4`→`130` / `5`→`259` / `6`→`260`。7 以上と `MarkerType` の定数はすべて `0`）。
//
//	つまり**「`MarkerType` の複合定数で様式を指定し、なおかつ角度も指定する」には
//	2 つの口を続けて叩くしかない**が、新口で書くと `nAngle` が `1` になるので、
//	**順番によっては角度が消える**見込みが高い。どちらの順でも駄目なら
//	「角度は SDK からは実質指定できない」と確定させる。
//
//	## 先に `sdk-grep` で分かったこと（実機へ持っていく前に潰した筋）
//
//	**1. `SMarkerStyle.nAngle` は `Sint8`**（`MiniCadCallBacks.h:839-847`）:
//
//	    struct SMarkerStyle {
//	        MarkerType style;   // Uint16
//	        Sint8      nAngle;  // ← 角度。-128〜127 しか入らない
//	        double     dSize;   // インチ
//	        double     dWidth;  // 書いても入らない（#108）
//	        Uint8      nThicknessBasis;
//	        double     dThickness;
//	    };
//
//	**中口の `angle` が `short` なのに符号付き 8 ビットへ切り詰められる**
//	（#113 で `180` → `-76`）のは、行き先がこの `Sint8` だからである。
//	**器の狭さは `1` に潰れる説明にはならない**——45 も 15 も `Sint8` に収まる。
//
//	**2. ヘッダの用例は `nAngle = 15` を書いている**（`MiniCadCallBacks.h:851-853`。
//	`marker.style = kArrowMarker + kOpenBaseNoFillMarker; marker.nAngle = 15;`）
//	——**ヘッダは「新口で角度を書ける」つもりで書かれている**。実測はそうならない。
//
//	**3. クラスの口は 2 つある**（`Interfaces/VectorWorks/ISDK.h:734-748`）。
//	形は per-object の新口・中口とそれぞれ同じで、**どちらも `visibility` を持たない**:
//
//	    Boolean GetClassBeginningMarker(InternalIndex index, SMarkerStyle& mstyle);   // :734
//	    Boolean SetClassBeginningMarker(InternalIndex index, SMarkerStyle mstyle);    // :743
//	    void    GetClMarker(InternalIndex index, MarkerType& style, short& size, short& angle); // :739
//	    void    SetClMarker(InternalIndex index, MarkerType style, short size, short angle);    // :748
//
//	**ヘッダからは「どちらの番号体系か」は分からない**——per-object でも、宣言が
//	`MarkerType` の中口が実際には `EMarkerType` の番号を受けていた。だから実測する。
//
//	## 確かめること（issue の切り分け項目に 1 節ずつ対応させてある）
//
//	  | 節 | 何を見るか | issue の項目 |
//	  | --- | --- | --- |
//	  | 1 | **中口で角度 → 新口で様式・大きさ**。角度は残るか | 1 つ目 |
//	  | 2 | **新口で様式 → 中口で角度**。様式は番号の表へ落ちるか | 2 つ目 |
//	  | 3 | `nAngle` の `1` は「無視されて既定」か「潰される」か | 3 つ目 |
//	  | 4 | **様式・大きさ・角度を同時に指定する手順**を 6 通り試して結論を出す | 4 つ目 |
//	  | 5 | **クラスの新口**（`Set/GetClassBeginningMarker`）は `MarkerType` が往復するか | 5 つ目 |
//	  | 6 | **クラスの中口**（`Set/GetClMarker`）に同じ罠があるか | 6 つ目 |
//	  | 7 | クラスへ書いた値が、そのクラスの線へ本当に下りるか（物証） | — |
//
//	**利用者の図面は触らない**——測るのはすべて、このプローブが自分で開いて自分で閉じる
//	空の図面の中である（`OpenDocumentPath(nullptr, false)` / `CloseDocument()`）。
//	節ごとに開き直すので前の節を引きずらない。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// --- 書き出しの小道具 ------------------------------------------------

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

	const char* SameOrNot(long got, long wanted)
	{
		return got == wanted ? "**入った**" : "**違う**";
	}

	// --- `MarkerType` の分解と命名（#113 のプローブと同じ割り方） ---------

	long RootOf(long style)
	{
		return style & static_cast<long>(kMarkerRootTypeMask);
	}

	const char* RootName(long style)
	{
		switch (RootOf(style))
		{
		case kArrowMarker:
			return "矢印";
		case kConcaveCurvedArrowMarker:
			return "反り矢印";
		case kCircleMarker:
			return "丸";
		case kDimSlashMarker:
			return "スラッシュ";
		case kDimCrossMarker:
			return "十字";
		case kLassoMarker:
			return "投げ縄";
		case kHexagonMarker:
			return "六角";
		case kVShapedMarker:
			return "V";
		case kRectangleMarker:
			return "矩形";
		case kDoubleLineMarker:
			return "二重線";
		default:
			return "（未知）";
		}
	}

	// --- 中口（`Get/SetMarker`。角度を持つ唯一の per-object の口） --------

	struct MiddleMarker
	{
		MarkerType style = 0;
		short size = 0;
		short angle = 0;
		Boolean start = 0;
		Boolean end = 0;
	};

	MiddleMarker ReadMiddle(MCObjectHandle object)
	{
		MiddleMarker value;
		gSDK->GetMarker(object, value.style, value.size, value.angle, value.start, value.end);
		return value;
	}

	// --- 新口（`SMarkerStyle` ＋ `visibility`） ---------------------------

	struct MarkerEnd
	{
		Boolean ok = 0; // 呼び出しの戻り値（参考。判断には使わない——#113）
		Boolean visibility = 0; // マーカーが付いているか
		SMarkerStyle style{};
	};

	MarkerEnd ReadModern(MCObjectHandle object)
	{
		MarkerEnd end;
		end.ok = gSDK->GetObjBeginningMarker(object, end.style, end.visibility);
		return end;
	}

	long ModernStyleOf(const MarkerEnd& end)
	{
		return static_cast<long>(end.style.style);
	}

	long ModernAngleOf(const MarkerEnd& end)
	{
		return static_cast<long>(end.style.nAngle);
	}

	// 新口で「様式と大きさだけを書く」——**読んでから差し替えて書き戻す**（他の欄を
	// 壊さないため。#113 の「どう書くか」がそう書いている）。両端に同じものを書く。
	void WriteModernStyleAndSize(MCObjectHandle object, long style, double sizeInInch)
	{
		MarkerEnd begin = ReadModern(object);
		begin.style.style = static_cast<MarkerType>(style);
		begin.style.dSize = sizeInInch;
		gSDK->SetObjBeginningMarker(object, begin.style, static_cast<Boolean>(1));
		gSDK->SetObjEndMarker(object, begin.style, static_cast<Boolean>(1));
	}

	// --- 証人の線 --------------------------------------------------------

	int gSerial = 0;

	MCObjectHandle CreateWitnessLine()
	{
		const WorldCoord x = static_cast<WorldCoord>(gSerial++) * 1000;
		return gSDK->CreateLine(WorldPt(x, 0), WorldPt(x, 1000));
	}

	// --- 図面を開く・閉じる（#108 / #113 と同じ作法） ---------------------

	size_t CountOpenDocuments()
	{
		MockUp::TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		return static_cast<size_t>(files.GetSize());
	}

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
					  "）。この節はいまの図面で測る——**前の節の状態を引きずっている**うえ、"
					  "**クラスを触る節では利用者の図面のクラスを書き換えてしまう**ので、"
					  "そのつもりで読むこと。");
			probe.log("");
			return false;
		}
		gSerial = 0;
		return true;
	}

	void CloseFreshDocument(vwprobe::Report& probe, size_t countBefore, bool opened)
	{
		if (!opened)
			return;
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

	// --- 掃引する値 ------------------------------------------------------

	// 狙う `MarkerType`。**中口の番号 0〜6 で届くもの**（`259` = スラッシュ・塗り無し、
	// `2` = 丸）と、**届かないもの**（`6` 六角 / `2048` 角台の矢印 / `258` 丸・塗り無し）を
	// 混ぜてある——「複合定数と角度を両立させられるか」は、この 2 群で答えが変わりうる。
	struct Target
	{
		long style;		  // 狙う MarkerType
		const char* name; // 定数名
		long middleNumber; // 同じ様式へ中口で届く EMarkerType の番号（無ければ -1）
	};

	const Target kTargets[] = {
		{kNoFillDimSlashMarker, "kNoFillDimSlashMarker(259)", 5},
		{kCircleMarker, "kCircleMarker(2)", 3},
		{kOpenBaseNoFillMarker, "kOpenBaseNoFillMarker(1280)", 2},
		{kHexagonMarker, "kHexagonMarker(6)", -1},
		{kAngleBaseMarker, "kAngleBaseMarker(2048)", -1},
		{kCircleMarker | kNoFillMarker, "kCircleMarker|kNoFillMarker(258)", -1},
	};
	const size_t kTargetsCount = sizeof(kTargets) / sizeof(kTargets[0]);

	// 3 節。**#113 が振っていない小さな値（1 / 2 / 3）と、`Sint8` の両端**を入れてある
	// ——「`1` だけは書ける」「小さな値だけ通る」なら、ここで割れる。
	const long kAngleSweep[] = {1, 2, 3, 15, 45, 90, 127, -1, -128};
	const size_t kAngleSweepCount = sizeof(kAngleSweep) / sizeof(kAngleSweep[0]);

	// 測るときの大きさ。新口はインチ、中口はポイント（72 分の 1 インチ）。
	// **同じ大きさを 2 つの単位で書く**ので、どちらの口が最後に効いたかが読める。
	const double kSizeInch = 0.2500;
	const short kSizePoints = 18; // = 0.2500 インチ
	const short kProbeAngle = 45;

	// 中口で「様式・大きさ・角度」を書く（両端）。
	void WriteMiddle(MCObjectHandle object, long style, short sizePoints, short angle)
	{
		gSDK->SetMarker(object, static_cast<MarkerType>(style), sizePoints, angle,
						static_cast<Boolean>(1), static_cast<Boolean>(1));
	}

	// 1 本の線のいまの姿を、表の 1 行ぶんのセルにする（新口 style / dSize / nAngle と
	// 中口の angle）。**2 つの口から同じ角度を読む**ので、片方の嘘に気付ける。
	std::string StateCells(MCObjectHandle object)
	{
		const MarkerEnd modern = ReadModern(object);
		const MiddleMarker middle = ReadMiddle(object);
		return Num(ModernStyleOf(modern)) + " | " + RootName(ModernStyleOf(modern)) + " | " +
			   Real(modern.style.dSize) + " | " + Num(ModernAngleOf(modern)) + " | " +
			   Num(static_cast<long>(middle.angle)) + " | " + Num(static_cast<long>(middle.size));
	}
} // namespace

VW_PROBE("marker-angle-order", "マーカーの角度と様式を両立させる手順があるかを確かめる",
		 "中口と新口を続けて叩く順番を総当たりし、クラスの 2 つの口の番号体系も測る")
{
	probe.log("**様式（`MarkerType`）と角度（`nAngle`）を両方とも狙いどおりに書く手順は"
			  "あるのか**——これがこの調査の問いである。#113 で確定したのは"
			  "「**角度を書けるのは中口（`SetMarker`）だけ**」「**`MarkerType` をそのまま"
			  "書けるのは新口（`SetObjBeginningMarker`）だけ**」の 2 つで、"
			  "**組み合わせたときどうなるかは測っていない**。");
	probe.log("");
	probe.log("**先に `sdk-grep` で分かっていること**——`SMarkerStyle.nAngle` は `Sint8` "
			  "（`MiniCadCallBacks.h:841`）。中口の `angle`（`short`）が `180` → `-76` と"
			  "切り詰められたのは行き先がこの器だからである。**ただし器の狭さは "
			  "`1` に潰れる説明にはならない**——`45` も `15` も `Sint8` に収まる。"
			  "しかも**ヘッダの用例は `marker.nAngle = 15;` と書いている**"
			  "（同 :851-853）——ヘッダは「新口で角度を書ける」つもりでいる。");
	probe.log("");
	probe.log("**クラスの口は 2 つある**（`ISDK.h:734-748`）。`Set/GetClassBeginningMarker` は"
			  "新口と同じ形（`SMarkerStyle`）、`Set/GetClMarker` は中口と同じ形"
			  "（`MarkerType` ＋ `size` ＋ `angle`）で、**どちらも `visibility` を持たない**。"
			  "per-object では「宣言が `MarkerType` でも実際は番号」という罠があったので、"
			  "**ヘッダの宣言では決められない**——5・6 節で実測する。");
	probe.log("");
	probe.log("**利用者の図面は触らない**——測るのは、このプローブが自分で開いて自分で"
			  "閉じる空の図面の中だけである。");
	probe.log("");
	probe.log("表の読み方: 新口 style は `GetObjBeginningMarker` の `SMarkerStyle.style`、"
			  "`dSize` はインチ、`nAngle` は同じ構造体の角度。中口 angle / size は "
			  "`GetMarker` の値（size は**ポイント**）。狙いは "
			  "**大きさ " +
			  Real(kSizeInch) + " インチ（= " + Num(kSizePoints) + "ポイント）・角度 " +
			  Num(kProbeAngle) + " 度**で通してある。");
	probe.log("");

	// =====================================================================
	// 1. 中口で角度 → 新口で様式・大きさ
	// =====================================================================
	probe.log("## 1. 中口で角度を書いた後に、新口で様式・大きさを書く");
	probe.log("");
	probe.log("**角度は残るのか、`1` に潰れるのか。** 1 本ごとに新しい線を引き、"
			  "`SetMarker(line, 0, " +
			  Num(kSizePoints) + ", " + Num(kProbeAngle) +
			  ", yes, yes)` で角度を入れてから、新口で `style` と `dSize` を書く"
			  "（**読んでから差し替えて書き戻す**——#113 の「どう書くか」の形）。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 狙う様式 | 中口の後の angle | 新口の後の style | 根 | dSize | "
				  "nAngle | 中口 angle | 中口 size |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kTargetsCount; ++i)
		{
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + std::string(kTargets[i].name) +
						  " | **引けなかった（nil）** | | | | | | |");
				continue;
			}
			// ① 中口で角度を入れる（様式は塗り矢印 = 番号 0 にしておく）。
			WriteMiddle(line, 0, kSizePoints, kProbeAngle);
			const long angleAfterMiddle = static_cast<long>(ReadMiddle(line).angle);
			// ② 新口で様式と大きさを書く。
			WriteModernStyleAndSize(line, kTargets[i].style, kSizeInch);
			probe.log("| " + std::string(kTargets[i].name) + " | " + Num(angleAfterMiddle) + " | " +
					  StateCells(line) + " |");
		}
		probe.log("");
		probe.log("**読み方**: `nAngle` の欄が " + Num(kProbeAngle) +
				  " のままなら**角度は残る**（＝新口は `nAngle` を触らない）。"
				  "`1` になっていれば**新口で書いた時点で角度が消える**ので、"
				  "この順では両立しない。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 2. 新口で様式 → 中口で角度
	// =====================================================================
	probe.log("## 2. 逆順——新口で様式を書いた後に、中口で角度を書く");
	probe.log("");
	probe.log("**中口は `style` も同時に書く**（角度だけを書く口は無い）ので、"
			  "**様式が番号 0〜6 の表へ落ちてしまわないか**が焦点である。"
			  "中口へ渡す `style` を 2 通り試す:");
	probe.log("");
	probe.log("- **(a) 読み戻した `MarkerType` をそのまま渡す**"
			  "（素朴にやるとこうなる。#113 のとおりなら番号として解釈されて壊れる）");
	probe.log("- **(b) 同じ様式へ届く `EMarkerType` の番号を渡す**"
			  "（届く番号がある様式だけ。無いものは「番号なし」と出る）");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 狙う様式 | 中口へ渡した style | 新口の後の style | 最終 style | 根 | "
				  "dSize | nAngle | 中口 angle | 中口 size | 様式は保たれたか |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kTargetsCount; ++i)
		{
			for (int variant = 0; variant < 2; ++variant)
			{
				const long passed = variant == 0 ? kTargets[i].style : kTargets[i].middleNumber;
				const std::string passedLabel =
					variant == 0 ? "(a) " + Num(passed) + "（MarkerType のまま）"
								 : (passed < 0 ? "(b) **番号なし**"
											   : "(b) " + Num(passed) + "（EMarkerType の番号）");
				if (variant == 1 && passed < 0)
				{
					probe.log("| " + std::string(kTargets[i].name) + " | " + passedLabel +
							  " | — | — | — | — | — | — | — | **この様式へ届く番号は無い** |");
					continue;
				}
				MCObjectHandle line = CreateWitnessLine();
				if (line == nil)
				{
					probe.log("| " + std::string(kTargets[i].name) + " | " + passedLabel +
							  " | **引けなかった（nil）** | | | | | | | |");
					continue;
				}
				// ① 新口で様式と大きさ。
				WriteModernStyleAndSize(line, kTargets[i].style, kSizeInch);
				const long styleAfterModern = ModernStyleOf(ReadModern(line));
				// ② 中口で角度（style も一緒に書かざるを得ない）。
				WriteMiddle(line, passed, kSizePoints, kProbeAngle);
				const long finalStyle = ModernStyleOf(ReadModern(line));
				probe.log("| " + std::string(kTargets[i].name) + " | " + passedLabel + " | " +
						  Num(styleAfterModern) + " | " + StateCells(line) + " | " +
						  SameOrNot(finalStyle, kTargets[i].style) + " |");
			}
		}
		probe.log("");
		probe.log("**読み方**: 「最終 style」が狙いと同じで `nAngle` も " + Num(kProbeAngle) +
				  " なら、**この順で両立する**。様式が別の数に変わっていれば、"
				  "**中口の書き込みが様式を番号の表へ落としている**。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 3. `nAngle` の `1` は何なのか
	// =====================================================================
	probe.log("## 3. 新口の `nAngle` が `1` になるのは「無視」か「潰し」か");
	probe.log("");
	probe.log("**読んでからそのまま書き戻す**（`nAngle` を触らない）とどうなるかで分かる:");
	probe.log("");
	probe.log("- 書き戻しても角度が残る → **`nAngle` は読まれていない（無視）** のであって、"
			  "**既にある角度は壊れない**。#113 で常に `1` だったのは、"
			  "「新口で書いた線の角度の既定が `1`」というだけのことになる。");
	probe.log("- 書き戻すと `1` になる → **新口で書くたびに角度が `1` へ潰される**。"
			  "角度を入れた後で新口を叩けない（1 節の順は使えない）。");
	probe.log("");
	probe.log("あわせて**#113 が振っていない小さな値（1 / 2 / 3）と `Sint8` の両端**も"
			  "書いてみる——「`1` だけは書ける」「小さい値だけ通る」ならここで割れる。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("### 3-A. 角度 " + Num(kProbeAngle) +
				  " を中口で入れた線に、新口でいろいろな書き方をする");
		probe.log("");
		probe.log("| 新口での書き方 | 書く前の nAngle | 書いた後の nAngle | 中口 angle | "
				  "style | dSize |");
		probe.log("| --- | --- | --- | --- | --- | --- |");

		struct WriteBackCase
		{
			const char* label;
			int kind; // 0=素通し 1=dSize だけ変える 2=同じ角度を明示 3=別の角度 4=まっさら
		};
		const WriteBackCase kCases[] = {
			{"読んで**何も変えずに**書き戻す", 0},
			{"読んで **`dSize` だけ**変えて書き戻す", 1},
			{"読んで **`nAngle` に同じ値**を入れて書き戻す", 2},
			{"読んで **`nAngle` に別の値（90）**を入れて書き戻す", 3},
			{"**読まずに** `SMarkerStyle{}`（`nAngle=0`）で書く", 4},
		};
		const size_t kCasesCount = sizeof(kCases) / sizeof(kCases[0]);

		for (size_t i = 0; i < kCasesCount; ++i)
		{
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + std::string(kCases[i].label) +
						  " | **引けなかった（nil）** | | | | |");
				continue;
			}
			WriteMiddle(line, 0, kSizePoints, kProbeAngle);
			const MarkerEnd before = ReadModern(line);
			SMarkerStyle write = before.style;
			switch (kCases[i].kind)
			{
			case 1:
				write.dSize = 0.5000;
				break;
			case 2:
				write.nAngle = static_cast<Sint8>(kProbeAngle);
				break;
			case 3:
				write.nAngle = static_cast<Sint8>(90);
				break;
			case 4:
			{
				SMarkerStyle blank{};
				blank.style = static_cast<MarkerType>(kCircleMarker);
				blank.dSize = kSizeInch;
				write = blank;
				break;
			}
			default:
				break;
			}
			gSDK->SetObjBeginningMarker(line, write, static_cast<Boolean>(1));
			probe.log("| " + std::string(kCases[i].label) + " | " + Num(ModernAngleOf(before)) +
					  " | " + StateCells(line) + " |");
		}
		probe.log("");

		probe.log("### 3-B. 新口へ角度を直に書く（#113 が振っていない値を足した掃引）");
		probe.log("");
		probe.log("線は毎回引き直し、**中口で角度 " + Num(kProbeAngle) +
				  " を入れてから**新口でその値を書く（＝書き込みが効かなければ " +
				  Num(kProbeAngle) + " が残り、効けば書いた値になり、潰されれば `1` になる）。");
		probe.log("");
		probe.log("| 新口へ書いた `nAngle` | 読み戻した nAngle | どうなったか | 中口 angle |");
		probe.log("| --- | --- | --- | --- |");
		for (size_t i = 0; i < kAngleSweepCount; ++i)
		{
			const long wanted = kAngleSweep[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Num(wanted) + " | **引けなかった（nil）** | | |");
				continue;
			}
			WriteMiddle(line, 0, kSizePoints, kProbeAngle);
			MarkerEnd end = ReadModern(line);
			end.style.nAngle = static_cast<Sint8>(wanted);
			gSDK->SetObjBeginningMarker(line, end.style, static_cast<Boolean>(1));
			const MarkerEnd after = ReadModern(line);
			const long got = ModernAngleOf(after);
			const char* verdict = got == wanted
									  ? "**書けた**"
									  : (got == kProbeAngle ? "書き込みが無視された（前の値）"
															: "**別の値へ潰れた**");
			probe.log("| " + Num(wanted) + " | " + Num(got) + " | " + verdict + " | " +
					  Num(static_cast<long>(ReadMiddle(line).angle)) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 4. 3 つを同時に指定する手順はあるか
	// =====================================================================
	probe.log("## 4. 様式・大きさ・角度の 3 つを同時に指定する手順はあるか");
	probe.log("");
	probe.log("**この節が issue の結論になる。** 狙いは「様式＝その行の `MarkerType`・"
			  "大きさ＝" +
			  Real(kSizeInch) + " インチ・角度＝" + Num(kProbeAngle) +
			  " 度」の 3 つが**同時に**読み戻せること。手順を 6 通り試す:");
	probe.log("");
	probe.log("| 手順 | 何をするか |");
	probe.log("| --- | --- |");
	probe.log("| R1 | **中口だけ**（`SetMarker(番号, ポイント, 角度)`） |");
	probe.log("| R2 | 新口（様式・大きさ）→ 中口（角度。style は番号） |");
	probe.log("| R3 | 中口（角度）→ 新口（様式・大きさ） |");
	probe.log("| R4 | 新口 → 中口 → **新口で様式だけ書き直す** |");
	probe.log("| R5 | 中口 → 新口 → **中口でもう一度**（番号） |");
	probe.log("| R6 | 新口で `style` と `dSize` と `nAngle` を**まとめて 1 回**で書く |");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 狙う様式 | 手順 | 最終 style | 根 | dSize | nAngle | 中口 angle | "
				  "中口 size | **3 つとも狙いどおりか** |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kTargetsCount; ++i)
		{
			for (int recipe = 1; recipe <= 6; ++recipe)
			{
				const long middleNumber =
					kTargets[i].middleNumber >= 0 ? kTargets[i].middleNumber : 0;
				MCObjectHandle line = CreateWitnessLine();
				if (line == nil)
				{
					probe.log("| " + std::string(kTargets[i].name) + " | R" + Num(recipe) +
							  " | **引けなかった（nil）** | | | | | | |");
					continue;
				}
				switch (recipe)
				{
				case 1:
					WriteMiddle(line, middleNumber, kSizePoints, kProbeAngle);
					break;
				case 2:
					WriteModernStyleAndSize(line, kTargets[i].style, kSizeInch);
					WriteMiddle(line, middleNumber, kSizePoints, kProbeAngle);
					break;
				case 3:
					WriteMiddle(line, middleNumber, kSizePoints, kProbeAngle);
					WriteModernStyleAndSize(line, kTargets[i].style, kSizeInch);
					break;
				case 4:
					WriteModernStyleAndSize(line, kTargets[i].style, kSizeInch);
					WriteMiddle(line, middleNumber, kSizePoints, kProbeAngle);
					WriteModernStyleAndSize(line, kTargets[i].style, kSizeInch);
					break;
				case 5:
					WriteMiddle(line, middleNumber, kSizePoints, kProbeAngle);
					WriteModernStyleAndSize(line, kTargets[i].style, kSizeInch);
					WriteMiddle(line, middleNumber, kSizePoints, kProbeAngle);
					break;
				default:
				{
					MarkerEnd end = ReadModern(line);
					end.style.style = static_cast<MarkerType>(kTargets[i].style);
					end.style.dSize = kSizeInch;
					end.style.nAngle = static_cast<Sint8>(kProbeAngle);
					gSDK->SetObjBeginningMarker(line, end.style, static_cast<Boolean>(1));
					gSDK->SetObjEndMarker(line, end.style, static_cast<Boolean>(1));
					break;
				}
				}
				const MarkerEnd got = ReadModern(line);
				const bool styleOk = ModernStyleOf(got) == kTargets[i].style;
				const bool sizeOk =
					got.style.dSize > kSizeInch - 0.0005 && got.style.dSize < kSizeInch + 0.0005;
				const bool angleOk = ModernAngleOf(got) == kProbeAngle;
				const std::string verdict = styleOk && sizeOk && angleOk
												? "**○ 3 つとも入った**"
												: std::string("× ") + (styleOk ? "" : "様式 ") +
													  (sizeOk ? "" : "大きさ ") +
													  (angleOk ? "" : "角度 ") + "が狙いと違う";
				probe.log("| " + std::string(kTargets[i].name) + " | R" + Num(recipe) + " | " +
						  StateCells(line) + " | " + verdict + " |");
			}
		}
		probe.log("");
		probe.log("**読み方**: ○ が 1 つでもあれば、**その手順が答え**である。"
				  "「中口で届く番号がある様式」だけに ○ が並ぶなら、"
				  "**角度を指定できるのは中口で届く 7 つの様式だけ**と確定する。"
				  "どの行にも ○ が無ければ、**様式と角度は同時に指定できない**"
				  "（＝角度は SDK からは実質指定できない）と確定する。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 5・6・7. クラスの口
	// =====================================================================
	probe.log("## 5. クラスの新口（`Set/GetClassBeginningMarker`）");
	probe.log("");
	probe.log("**`MarkerType` がそのまま往復するか**（per-object の新口と同じか）と、"
			  "**`nAngle` は書けるか**を見る。この口は `visibility` を持たない。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		// **クラスは次の順に取る。どれで取れたかは必ずログに出す**（取れたクラスが
		// 何であれ、番号体系の測定は成り立つ）:
		//   ① 名前から引く（`ClassNameToID`。:1444）——引くだけで作られるなら、これ
		//   ② ガイドクラスを作る（`AddGuidesClass`。:1353）——**作る口はこれだけ**
		//   ③ 寸法クラス（`GetDimensionClassID`。:1368）——図面に必ずある本物のクラス
		//   ④ なし（`GetNoneClassID`。:1377）——最後の逃げ道
		InternalIndex classIndex = 0;
		std::string classHow;
		{
			const InternalIndex named = gSDK->ClassNameToID(TXString("VwSdkProbeMarker"));
			if (named != InternalIndex(-1) && gSDK->ValidClass(named))
			{
				classIndex = named;
				classHow = "`ClassNameToID(\"VwSdkProbeMarker\")` で取れた"
						   "（**名前を引くだけでクラスが用意される**）";
			}
			else
			{
				const InternalIndex guides = gSDK->AddGuidesClass();
				const InternalIndex dimension = gSDK->GetDimensionClassID();
				if (gSDK->ValidClass(guides))
				{
					classIndex = guides;
					classHow = "新しい名前では取れなかったので**ガイドクラスを作って**"
							   "（`AddGuidesClass`）使う";
				}
				else if (gSDK->ValidClass(dimension))
				{
					classIndex = dimension;
					classHow = "**寸法クラス**（`GetDimensionClassID`）を使う"
							   "——名前からもガイドからも取れなかった";
				}
				else
				{
					classIndex = gSDK->GetNoneClassID();
					classHow = "**「なし」クラス**（`GetNoneClassID`）を使う"
							   "——ほかのどれでも取れなかった";
				}
			}
		}
		probe.log("使うクラス: 番号 " + Num(static_cast<long>(classIndex)) + " ——" + classHow +
				  "。");
		{
			TXString className;
			gSDK->ClassIDToName(classIndex, className);
			probe.log("クラス名: `" + std::string(static_cast<const char*>(className)) + "`");
		}
		probe.log("");

		probe.log("### 5-A. `MarkerType` は往復するか（`nAngle` に " + Num(kProbeAngle) +
				  " を入れて書く）");
		probe.log("");
		probe.log("| 書いた style | 書き込みの戻り値 | 読めた style | 根 | dSize | nAngle | "
				  "様式は往復したか | 角度は入ったか |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kTargetsCount; ++i)
		{
			SMarkerStyle mstyle{};
			gSDK->GetClassBeginningMarker(classIndex, mstyle);
			mstyle.style = static_cast<MarkerType>(kTargets[i].style);
			mstyle.dSize = kSizeInch;
			mstyle.nAngle = static_cast<Sint8>(kProbeAngle);
			const Boolean wrote = gSDK->SetClassBeginningMarker(classIndex, mstyle);
			SMarkerStyle readBack{};
			gSDK->GetClassBeginningMarker(classIndex, readBack);
			const long gotStyle = static_cast<long>(readBack.style);
			const long gotAngle = static_cast<long>(readBack.nAngle);
			probe.log("| " + std::string(kTargets[i].name) + " | " +
					  std::string(wrote != 0 ? "true" : "false") + " | " + Num(gotStyle) + " | " +
					  RootName(gotStyle) + " | " + Real(readBack.dSize) + " | " + Num(gotAngle) +
					  " | " + SameOrNot(gotStyle, kTargets[i].style) + " | " +
					  SameOrNot(gotAngle, kProbeAngle) + " |");
		}
		probe.log("");

		probe.log("### 5-B. 角度だけを振る（per-object の新口は全点 `1` になった）");
		probe.log("");
		probe.log("| 書いた `nAngle` | 読めた `nAngle` | どうなったか |");
		probe.log("| --- | --- | --- |");
		for (size_t i = 0; i < kAngleSweepCount; ++i)
		{
			const long wanted = kAngleSweep[i];
			SMarkerStyle mstyle{};
			gSDK->GetClassBeginningMarker(classIndex, mstyle);
			mstyle.nAngle = static_cast<Sint8>(wanted);
			gSDK->SetClassBeginningMarker(classIndex, mstyle);
			SMarkerStyle readBack{};
			gSDK->GetClassBeginningMarker(classIndex, readBack);
			const long got = static_cast<long>(readBack.nAngle);
			probe.log("| " + Num(wanted) + " | " + Num(got) + " | " +
					  std::string(got == wanted ? "**書けた**" : "**書けなかった**") + " |");
		}
		probe.log("");

		// -----------------------------------------------------------------
		probe.log("## 6. クラスの中口（`Set/GetClMarker`）——同じ罠があるか");
		probe.log("");
		probe.log("per-object の中口は「**読みは `MarkerType`、書きは `EMarkerType` の番号**」"
				  "だった。同じ形のこの口でも同じか。**番号 0〜6 と `MarkerType` の定数の"
				  "両方を書いて、クラスの新口で読み戻す**——"
				  "番号 0〜6 が per-object と同じ `0` / `256` / `1280` / `2` / `130` / `259` / "
				  "`260` へ化ければ、**同じ表**である。");
		probe.log("");
		probe.log("| `SetClMarker` へ書いた style | `GetClMarker` style | angle | size | "
				  "クラス新口 style | 根 | nAngle | dSize | 書いたとおりか |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- | --- |");

		struct ClassWrite
		{
			long value;
			const char* label;
		};
		const ClassWrite kClassWrites[] = {
			{0, "0"},
			{1, "1"},
			{2, "2"},
			{3, "3"},
			{4, "4"},
			{5, "5"},
			{6, "6"},
			{7, "7"},
			{kCircleMarker, "kCircleMarker(2)"},
			{kNoFillDimSlashMarker, "kNoFillDimSlashMarker(259)"},
			{kOpenBaseNoFillMarker, "kOpenBaseNoFillMarker(1280)"},
			{kAngleBaseMarker, "kAngleBaseMarker(2048)"},
		};
		const size_t kClassWritesCount = sizeof(kClassWrites) / sizeof(kClassWrites[0]);
		for (size_t i = 0; i < kClassWritesCount; ++i)
		{
			const long wanted = kClassWrites[i].value;
			gSDK->SetClMarker(classIndex, static_cast<MarkerType>(wanted), kSizePoints,
							  kProbeAngle);
			MarkerType clStyle = 0;
			short clSize = 0;
			short clAngle = 0;
			gSDK->GetClMarker(classIndex, clStyle, clSize, clAngle);
			SMarkerStyle modern{};
			gSDK->GetClassBeginningMarker(classIndex, modern);
			const long modernStyle = static_cast<long>(modern.style);
			probe.log("| " + std::string(kClassWrites[i].label) + " | " +
					  Num(static_cast<long>(clStyle)) + " | " + Num(static_cast<long>(clAngle)) +
					  " | " + Num(static_cast<long>(clSize)) + " | " + Num(modernStyle) + " | " +
					  RootName(modernStyle) + " | " + Num(static_cast<long>(modern.nAngle)) +
					  " | " + Real(modern.dSize) + " | " + SameOrNot(modernStyle, wanted) + " |");
		}
		probe.log("");
		probe.log("**読み方**: 「クラス新口 style」の欄が、番号 0〜6 に対して per-object の"
				  "中口とまったく同じ `0` / `256` / `1280` / `2` / `130` / `259` / `260` に"
				  "なれば、**クラスの中口も `EMarkerType` の番号を受ける**（同じ罠）。"
				  "`MarkerType` の定数の行がそのまま往復するなら、**クラスの中口だけは"
				  "宣言どおり**ということになる。`angle` の欄が " +
				  Num(kProbeAngle) + " なら、**クラスにも角度を書ける口はこちら**である。");
		probe.log("");

		// -----------------------------------------------------------------
		probe.log("## 7. クラスへ書いた値は、そのクラスの線へ下りるか");
		probe.log("");
		probe.log("**クラスの口が本当にマーカーを持っているか**の物証。"
				  "5・6 節で書いたクラスへ線を入れ、`SetArrowByClass(line)` を呼んでから"
				  "per-object の口で読む（`visibility` の引数を持たない口なので、"
				  "**マーカーが見える状態になるのか**もここで分かる）。");
		probe.log("");

		// 下りてくるはずの値を、はっきりした組み合わせで置き直す。
		SMarkerStyle wanted{};
		gSDK->GetClassBeginningMarker(classIndex, wanted);
		wanted.style = static_cast<MarkerType>(kNoFillDimSlashMarker);
		wanted.dSize = kSizeInch;
		wanted.nAngle = static_cast<Sint8>(kProbeAngle);
		gSDK->SetClassBeginningMarker(classIndex, wanted);
		gSDK->SetClassEndMarker(classIndex, wanted);
		SMarkerStyle classNow{};
		gSDK->GetClassBeginningMarker(classIndex, classNow);
		probe.log("クラスへ置いた値: style=" + Num(static_cast<long>(classNow.style)) + "（" +
				  RootName(static_cast<long>(classNow.style)) + "） dSize=" + Real(classNow.dSize) +
				  " nAngle=" + Num(static_cast<long>(classNow.nAngle)));
		probe.log("");

		probe.log("| 線 | by-class の旗 | 新口の戻り値 | visible | style | 根 | dSize | "
				  "nAngle | 中口 angle |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- | --- |");
		for (int step = 0; step < 2; ++step)
		{
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log(
					"| " +
					std::string(step == 0 ? "クラスに入れただけ" : "`SetArrowByClass` も呼んだ") +
					" | **引けなかった（nil）** | | | | | | | |");
				continue;
			}
			gSDK->SetObjectClass(line, classIndex);
			if (step == 1)
				gSDK->SetArrowByClass(line);
			const MarkerEnd end = ReadModern(line);
			const MiddleMarker middle = ReadMiddle(line);
			probe.log("| " +
					  std::string(step == 0 ? "クラスに入れただけ" : "`SetArrowByClass` も呼んだ") +
					  " | " + std::string(gSDK->GetArrowByClass(line) != 0 ? "yes" : "no") + " | " +
					  std::string(end.ok != 0 ? "yes" : "no") + " | " +
					  std::string(end.visibility != 0 ? "yes" : "no") + " | " +
					  Num(ModernStyleOf(end)) + " | " + RootName(ModernStyleOf(end)) + " | " +
					  Real(end.style.dSize) + " | " + Num(ModernAngleOf(end)) + " | " +
					  Num(static_cast<long>(middle.angle)) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 8. 締め
	// =====================================================================
	probe.log("## 8. 締め");
	probe.log("");
	probe.log("開いている図面の件数: " + Num(static_cast<long>(CountOpenDocuments())) +
			  "（走らせる前と同じなら、開いた図面はすべて閉じられている）");
	probe.log("");
	probe.log("**利用者の図面には何も足していない**——クラスを触る 5〜7 節も含め、"
			  "測るのはすべてプローブが自分で開いて自分で閉じる空の図面の中である"
			  "（図面を開けなかったときは、その旨が上に出ている）。");
}
