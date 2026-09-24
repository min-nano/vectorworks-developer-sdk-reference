//
//	probes/runtime/marker-angle-order/probe.cpp
//
//	[issue #116] **様式・大きさ・角度を同時に指定する手順はあるか**——1 周目
//	（[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/117#issuecomment-5822405859)）で
//	issue の 6 項目には答えが出た。要点:
//
//	  * **新口 1 回で 3 つとも書ける**（`SetObjBeginningMarker` に `style` / `dSize` /
//	    `nAngle` をまとめて渡す）。中口を挟む必要は無い。
//	  * **ただし `kCircleMarker`（丸）の様式だけは、新口で書くと `nAngle` が `1` になる。**
//	    丸以外（矢印・スラッシュ・六角・角台）は `45` がそのまま入った。
//	  * **#113 の「新口では角度を書けない」は、丸を選んで測ったことによる見かけだった**
//	    ——あのプローブの 5 節は `style` に `kCircleMarker` を固定していた
//	    （`marker-style-mapping/probe.cpp` の該当箇所）。矢印で掃引すると
//	    `1` / `2` / `3` / `15` / `45` / `90` / `127` / `-1` / `-128` の **9 点すべてが往復した**。
//	  * **クラスの口は per-object と同じ**——新口型（`Set/GetClassBeginningMarker`）は
//	    `MarkerType` が往復し、中口型（`Set/GetClMarker`）は書きだけ `EMarkerType` の
//	    番号（0→0 / 1→256 / 2→1280 / 3→2 / 4→130 / 5→259 / 6→260、他は 0）。
//
//	## この 2 周目で閉じる 3 つ
//
//	1 周目が**新しく開けてしまった穴**を塞ぐ。どれも実機のログで機械的に答えが出る
//	（目視は要らない）。
//
//	  | 節 | 何を確かめるか | なぜ要るか |
//	  | --- | --- | --- |
//	  | 1 | **角度が入る様式・入らない様式の総当たり** | 「丸だけ駄目」なのか、他にもあるのかが分からないと**使う側が手順を選べない** |
//	  | 2 | 角度の掃引を**丸と矢印で並べて**、中口からも書く | 「丸は角度を持てない」のか「新口の書き込みだけが潰す」のかを分ける |
//	  | 3 | **中口の `size` の単位** | 1 周目で **18 を書いたら 0.0011 インチ**、`dSize=0.2500` を書いたら **4096** と読めた。**ポイント（72 分の 1）ではない**——`0.2500 × 16384 = 4096` なので **1/16384 インチ**に見えるが、3 点しか無い |
//	  | 4 | **クラスのマーカーは線へ下りるのか**（`GetMarkerPolys` で物証を取る） | 1 周目の 7 節は per-object の口で読んで「戻り値 no」だった——**その出力引数は当てにならない**（Findings「`GetObjBeginningMarker` が `false` を返したら…」）ので、**何も分かっていない** |
//
//	`GetMarkerPolys(object, startPoly&, endPoly&)`（`ISDK.h:851`）は**実際に描かれる
//	マーカーの図形**を返す。**描かれているかどうかが目視なしで分かる**唯一の口なので、
//	4 節はこれと `GetObjectBounds`（:854）で測る。
//
//	**利用者の図面は触らない**——クラスを触る 4 節も含め、測るのはすべて、このプローブが
//	自分で開いて自分で閉じる空の図面の中である。節ごとに開き直すので前の節を引きずらない。
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

	std::string Real6(double value)
	{
		char buffer[48];
		std::snprintf(buffer, sizeof(buffer), "%.6f", value);
		return std::string(buffer);
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

	// --- 中口（`Get/SetMarker`） -----------------------------------------

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
		Boolean ok = 0;
		Boolean visibility = 0;
		SMarkerStyle style{};
	};

	MarkerEnd ReadModern(MCObjectHandle object)
	{
		MarkerEnd end;
		end.ok = gSDK->GetObjBeginningMarker(object, end.style, end.visibility);
		return end;
	}

	// 新口で**様式・大きさ・角度を 1 回で**書く（1 周目の R6。両端に同じものを書く）。
	void WriteModernAll(MCObjectHandle object, long style, double sizeInInch, long angle)
	{
		MarkerEnd end = ReadModern(object);
		end.style.style = static_cast<MarkerType>(style);
		end.style.dSize = sizeInInch;
		end.style.nAngle = static_cast<Sint8>(angle);
		gSDK->SetObjBeginningMarker(object, end.style, static_cast<Boolean>(1));
		gSDK->SetObjEndMarker(object, end.style, static_cast<Boolean>(1));
	}

	void WriteMiddle(MCObjectHandle object, long style, short size, short angle)
	{
		gSDK->SetMarker(object, static_cast<MarkerType>(style), size, angle,
						static_cast<Boolean>(1), static_cast<Boolean>(1));
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

	struct NamedMarker
	{
		long value;
		const char* name;
	};

	// 1 節。**根を全部（ヘッダにある 0〜12）と、塗り・台・尾・半の変種**を並べる。
	// 丸の変種を 3 つ（`2` / `130` / `258`）入れてあるのは、「丸の根なら塗りに依らず
	// 駄目なのか」をここで割るため。
	const NamedMarker kStyleSweep[] = {
		{kArrowMarker, "kArrowMarker(0)"},
		{kConcaveCurvedArrowMarker, "kConcaveCurvedArrowMarker(1)"},
		{kCircleMarker, "kCircleMarker(2)"},
		{kDimSlashMarker, "kDimSlashMarker(3)"},
		{kDimCrossMarker, "kDimCrossMarker(4)"},
		{kLassoMarker, "kLassoMarker(5)"},
		{kHexagonMarker, "kHexagonMarker(6)"},
		{kVShapedMarker, "kVShapedMarker(7)"},
		{8, "8（名前の無い根）"},
		{9, "9（名前の無い根）"},
		{10, "10（名前の無い根）"},
		{kRectangleMarker, "kRectangleMarker(11)"},
		{kDoubleLineMarker, "kDoubleLineMarker(12)"},
		{kWhiteFillMarker, "kWhiteFillMarker(128。矢印＋白)"},
		{kNoFillMarker, "kNoFillMarker(256。矢印＋塗り無し)"},
		{kWhiteFillMarker | kCircleMarker, "kWhiteFillMarker|kCircleMarker(130)"},
		{kCircleMarker | kNoFillMarker, "kCircleMarker|kNoFillMarker(258)"},
		{kNoFillDimSlashMarker, "kNoFillDimSlashMarker(259)"},
		{kNoFillDimCrossMarker, "kNoFillDimCrossMarker(260)"},
		{kNoFillLassoMarker, "kNoFillLassoMarker(261)"},
		{kOpenBaseNoFillMarker, "kOpenBaseNoFillMarker(1280)"},
		{kAngleBaseMarker, "kAngleBaseMarker(2048)"},
		{kArcBaseMarker, "kArcBaseMarker(3072)"},
		{kLeftHalfTickMarker, "kLeftHalfTickMarker(16384)"},
		{kTailMarker, "kTailMarker(32768。矢印＋尾)"},
	};
	const size_t kStyleSweepCount = sizeof(kStyleSweep) / sizeof(kStyleSweep[0]);

	// 2 節。角度の掃引（`Sint8` の両端と、#113 が振った値を含む）。
	const long kAngleSweep[] = {0, 1, 2, 3, 15, 30, 45, 60, 90, 120, 127, -1, -45, -128};
	const size_t kAngleSweepCount = sizeof(kAngleSweep) / sizeof(kAngleSweep[0]);

	// 3 節。中口へ書く `size`（`short`）。**1/16384 インチ説**なら
	// `16384` = 1 インチ、`32767` ≒ 2 インチ（上限。#108 で 1.9999 インチ）になる。
	// ポイント説（72 分の 1）なら `72` = 1 インチ。**両説が割れる点を選んである。**
	const long kMiddleSizeSweep[] = {1, 18, 72, 144, 512, 1024, 4096, 8192, 16384, 32767, -1};
	const size_t kMiddleSizeSweepCount = sizeof(kMiddleSizeSweep) / sizeof(kMiddleSizeSweep[0]);

	// 3 節の逆向き。新口へ書くインチを、中口がどう読むか。
	const double kInchSweep[] = {0.0100, 0.0625, 0.1250, 0.2500, 0.5000, 1.0000, 1.9999, 2.0000};
	const size_t kInchSweepCount = sizeof(kInchSweep) / sizeof(kInchSweep[0]);

	const double kSizeInch = 0.2500;
	const long kProbeAngle = 45;

	// 4 節。マーカーの図形（`GetMarkerPolys`）を 1 行のセルにする。
	// **nil かどうかが「描かれているか」の物証**で、大きさは境界で見る。
	std::string MarkerPolyCells(MCObjectHandle object)
	{
		MCObjectHandle startPoly = nil;
		MCObjectHandle endPoly = nil;
		gSDK->GetMarkerPolys(object, startPoly, endPoly);
		std::string cells = std::string(startPoly != nil ? "**あり**" : "なし") + " | " +
							std::string(endPoly != nil ? "**あり**" : "なし") + " | ";
		if (startPoly == nil)
			return cells + "— | —";
		cells += Num(static_cast<long>(gSDK->GetObjectTypeN(startPoly))) + " | ";
		WorldRect bounds;
		if (gSDK->GetObjectBounds(startPoly, bounds) != 0)
		{
			const double width =
				static_cast<double>(bounds.right) - static_cast<double>(bounds.left);
			const double height =
				static_cast<double>(bounds.top) - static_cast<double>(bounds.bottom);
			cells += Real(width < 0 ? -width : width) + " × " + Real(height < 0 ? -height : height);
		}
		else
		{
			cells += "**境界を読めない**";
		}
		return cells;
	}
} // namespace

VW_PROBE("marker-angle-order", "角度が入る様式・中口の size の単位・クラスの下り方を確かめる",
		 "1 周目で開いた 3 つの穴（丸だけ駄目なのか・size の単位・クラスの物証）を塞ぐ")
{
	probe.log("**これは 2 周目である。** 1 周目"
			  "（[実測ログ](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/"
			  "117#issuecomment-5822405859)）"
			  "で issue #116 の 6 項目には答えが出た:");
	probe.log("");
	probe.log("- **新口 1 回で 3 つとも書ける**——`SetObjBeginningMarker` に `style` / "
			  "`dSize` / `nAngle` をまとめて渡せばよい。中口を挟む必要は無い。");
	probe.log("- **ただし丸（`kCircleMarker`）だけは、新口で書くと `nAngle` が `1` になった。**");
	probe.log("- **#113 の「新口では角度を書けない」は、丸を選んで測ったことによる見かけ**"
			  "だった（あのプローブの 5 節は `style` に `kCircleMarker` を固定していた）。"
			  "矢印で掃引すると 9 点すべてが往復した。");
	probe.log("- **クラスの口は per-object と同じ**（新口型は `MarkerType` が往復、"
			  "中口型は書きだけ `EMarkerType` の番号）。");
	probe.log("");
	probe.log("**2 周目は、1 周目が新しく開けた 3 つの穴を塞ぐ**——どれも機械で答えが出る:");
	probe.log("");
	probe.log("1. **角度が入る様式・入らない様式の総当たり**（「丸だけ駄目」なのか）");
	probe.log("2. **丸と矢印を並べた角度の掃引**（丸が角度を持てないのか、"
			  "新口の書き込みだけが潰すのか）");
	probe.log("3. **中口の `size` の単位**——1 周目で `18` を書いたら **0.0011 インチ**、"
			  "`dSize=0.2500` を書いたら **4096** と読めた。**ポイントではない**"
			  "（`0.2500 × 16384 = 4096`）が、3 点しか無い");
	probe.log("4. **クラスのマーカーは線へ下りるのか**——1 周目の 7 節は per-object の口で"
			  "読んで「戻り値 no」だった。**その出力引数は当てにならない**ので何も分かって"
			  "いない。`GetMarkerPolys`（`ISDK.h:851`）で**描かれる図形そのもの**を見る");
	probe.log("");
	probe.log("**利用者の図面は触らない**——測るのは、このプローブが自分で開いて自分で"
			  "閉じる空の図面の中だけである。");
	probe.log("");

	// =====================================================================
	// 1. 角度が入る様式・入らない様式
	// =====================================================================
	probe.log("## 1. 新口で角度が入る様式・入らない様式（総当たり）");
	probe.log("");
	probe.log("1 本ごとに新しい線を引き、**新口 1 回**で `style` ＋ `dSize=" + Real(kSizeInch) +
			  "` ＋ `nAngle=" + Num(kProbeAngle) + "` を書いて読み戻す。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 書いた style | 読めた style | 根 | dSize | nAngle | 中口 angle | "
				  "**角度は入ったか** |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kStyleSweepCount; ++i)
		{
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + std::string(kStyleSweep[i].name) +
						  " | **引けなかった（nil）** | | | | | |");
				continue;
			}
			WriteModernAll(line, kStyleSweep[i].value, kSizeInch, kProbeAngle);
			const MarkerEnd got = ReadModern(line);
			const long gotStyle = static_cast<long>(got.style.style);
			const long gotAngle = static_cast<long>(got.style.nAngle);
			probe.log("| " + std::string(kStyleSweep[i].name) + " | " + Num(gotStyle) + " | " +
					  RootName(gotStyle) + " | " + Real(got.style.dSize) + " | " + Num(gotAngle) +
					  " | " + Num(static_cast<long>(ReadMiddle(line).angle)) + " | " +
					  std::string(gotAngle == kProbeAngle ? "**入った**" : "**違う**") + " |");
		}
		probe.log("");
		probe.log("**読み方**: 「違う」が丸の根（`2` / `130` / `258`）の行だけに並べば、"
				  "**角度を持てないのは丸だけ**と確定する。他の根にも並ぶなら、"
				  "**その一覧がそのまま「角度を指定できない様式」の表**になる。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 2. 丸と矢印を並べた角度の掃引
	// =====================================================================
	probe.log("## 2. 角度の掃引——丸と矢印を、新口と中口の両方で");
	probe.log("");
	probe.log("**丸が角度を持てないのか、新口の書き込みだけが潰すのか**を分ける。"
			  "中口（`SetMarker`）で丸に角度を書いて入るなら、**潰しているのは新口の"
			  "書き込みだけ**である（1 周目のクラス 6 節では、中口から丸へ `45` が入った）。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 書いた角度 | 新口→矢印 | 新口→丸 | 中口→矢印(番号 0) | 中口→丸(番号 3) |");
		probe.log("| --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kAngleSweepCount; ++i)
		{
			const long wanted = kAngleSweep[i];
			std::string cells;

			// 新口 → 矢印 / 丸
			const long modernStyles[2] = {kArrowMarker, kCircleMarker};
			for (int k = 0; k < 2; ++k)
			{
				MCObjectHandle line = CreateWitnessLine();
				if (line == nil)
				{
					cells += " **nil** |";
					continue;
				}
				WriteModernAll(line, modernStyles[k], kSizeInch, wanted);
				cells += " " + Num(static_cast<long>(ReadModern(line).style.nAngle)) + " |";
			}

			// 中口 → 矢印（番号 0）/ 丸（番号 3）
			const long middleNumbers[2] = {0, 3};
			for (int k = 0; k < 2; ++k)
			{
				MCObjectHandle line = CreateWitnessLine();
				if (line == nil)
				{
					cells += " **nil** |";
					continue;
				}
				WriteMiddle(line, middleNumbers[k], static_cast<short>(4096),
							static_cast<short>(wanted));
				cells += " " + Num(static_cast<long>(ReadModern(line).style.nAngle)) + " |";
			}
			probe.log("| " + Num(wanted) + " |" + cells);
		}
		probe.log("");
		probe.log("**読み方**: 「新口→丸」の列だけが `1` で埋まり、「中口→丸」が書いた値を"
				  "返すなら、**丸も角度は持てる。潰しているのは新口の書き込みである**。"
				  "両方 `1` なら、**丸は角度を持てない**（口の問題ではない）。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 3. 中口の `size` の単位
	// =====================================================================
	probe.log("## 3. 中口（`Get/SetMarker`）の `size` の単位");
	probe.log("");
	probe.log("**1 周目で「ポイントではない」ことが分かった**——`18` を書いたら "
			  "`0.0011` インチ、`dSize=0.2500` を書いたら `4096` と読めた。"
			  "`0.2500 × 16384 = 4096` なので **1/16384 インチ**に見えるが、"
			  "3 点では決められないので掃引する。");
	probe.log("");
	probe.log("**ポイント説なら `72` が 1 インチ、1/16384 インチ説なら `16384` が 1 インチ**"
			  "になる。`32767`（`short` の上限）は 1.9999 インチ"
			  "（= マーカーの大きさの上限。#108）にぴったり当たるはずである。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("### 3-A. 中口へ書いた `size` を、新口のインチで読む");
		probe.log("");
		probe.log("| 中口へ書いた size | 新口 dSize（インチ） | 中口で読み直した size | "
				  "size ÷ 16384 | size ÷ 72 |");
		probe.log("| --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kMiddleSizeSweepCount; ++i)
		{
			const long wanted = kMiddleSizeSweep[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Num(wanted) + " | **引けなかった（nil）** | | | |");
				continue;
			}
			WriteMiddle(line, 0, static_cast<short>(wanted), static_cast<short>(kProbeAngle));
			const MarkerEnd got = ReadModern(line);
			probe.log("| " + Num(wanted) + " | " + Real6(got.style.dSize) + " | " +
					  Num(static_cast<long>(ReadMiddle(line).size)) + " | " +
					  Real6(static_cast<double>(wanted) / 16384.0) + " | " +
					  Real6(static_cast<double>(wanted) / 72.0) + " |");
		}
		probe.log("");
		probe.log("**読み方**: 「新口 dSize」が「size ÷ 16384」の列と一致すれば"
				  "**1/16384 インチ**、「size ÷ 72」と一致すれば**ポイント**である。");
		probe.log("");

		probe.log("### 3-B. 逆向き——新口へ書いたインチを、中口の `size` で読む");
		probe.log("");
		probe.log("| 新口へ書いた dSize | 新口で読めた dSize | 中口 size | "
				  "dSize × 16384 | dSize × 72 |");
		probe.log("| --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kInchSweepCount; ++i)
		{
			const double wanted = kInchSweep[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Real(wanted) + " | **引けなかった（nil）** | | | |");
				continue;
			}
			WriteModernAll(line, kArrowMarker, wanted, kProbeAngle);
			const MarkerEnd got = ReadModern(line);
			probe.log("| " + Real(wanted) + " | " + Real6(got.style.dSize) + " | " +
					  Num(static_cast<long>(ReadMiddle(line).size)) + " | " +
					  Real(got.style.dSize * 16384.0) + " | " + Real(got.style.dSize * 72.0) +
					  " |");
		}
		probe.log("");
		probe.log("**読み方**: 「中口 size」が「dSize × 16384」と一致すれば 1/16384 インチ。"
				  "`short` は 32767 までなので、**2 インチ（= 32768）を超えると読めなくなる"
				  "はず**——`2.0000` の行がそれを踏む（上限 1.9999 で止まるなら 32766 前後）。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 4. クラスのマーカーは線へ下りるか（物証）
	// =====================================================================
	probe.log("## 4. クラスのマーカーは線へ下りるか——`GetMarkerPolys` で物証を取る");
	probe.log("");
	probe.log("**1 周目の 7 節は何も分かっていなかった**——per-object の新口で読んで"
			  "「戻り値 no」だったが、**`false` のときの出力引数は当てにならない**"
			  "（Findings「余録: `GetObjBeginningMarker` が `false` を返したら、"
			  "出力引数を読まない」）。**描かれているか**は "
			  "`GetMarkerPolys`（`ISDK.h:851`）が返す図形で分かる。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		InternalIndex classIndex = 0;
		std::string classHow;
		{
			const InternalIndex named = gSDK->ClassNameToID(TXString("VwSdkProbeMarker"));
			if (named != InternalIndex(-1) && gSDK->ValidClass(named))
			{
				classIndex = named;
				classHow = "`ClassNameToID(\"VwSdkProbeMarker\")` で取れた";
			}
			else
			{
				const InternalIndex guides = gSDK->AddGuidesClass();
				if (gSDK->ValidClass(guides))
				{
					classIndex = guides;
					classHow = "**ガイドクラスを作って**（`AddGuidesClass`）使う";
				}
				else
				{
					classIndex = gSDK->GetNoneClassID();
					classHow = "**「なし」クラス**（`GetNoneClassID`）を使う";
				}
			}
		}
		TXString className;
		gSDK->ClassIDToName(classIndex, className);
		probe.log("使うクラス: 番号 " + Num(static_cast<long>(classIndex)) + "（`" +
				  std::string(static_cast<const char*>(className)) + "`）——" + classHow + "。");
		probe.log("");

		// クラスへはっきりした値を置く（1 周目で往復することは確かめてある）。
		SMarkerStyle classStyle{};
		gSDK->GetClassBeginningMarker(classIndex, classStyle);
		classStyle.style = static_cast<MarkerType>(kNoFillDimSlashMarker);
		classStyle.dSize = kSizeInch;
		classStyle.nAngle = static_cast<Sint8>(kProbeAngle);
		gSDK->SetClassBeginningMarker(classIndex, classStyle);
		gSDK->SetClassEndMarker(classIndex, classStyle);
		SMarkerStyle classNow{};
		gSDK->GetClassBeginningMarker(classIndex, classNow);
		probe.log("クラスへ置いた値: style=" + Num(static_cast<long>(classNow.style)) + "（" +
				  RootName(static_cast<long>(classNow.style)) + "） dSize=" + Real(classNow.dSize) +
				  " nAngle=" + Num(static_cast<long>(classNow.nAngle)));
		probe.log("");

		probe.log("| 線 | by-class の旗 | 始点の図形 | 終点の図形 | 図形の種別 | "
				  "図形の境界（幅 × 高さ） |");
		probe.log("| --- | --- | --- | --- | --- | --- |");

		struct LineCase
		{
			const char* label;
			int kind; // 0=素の線 1=クラスに入れただけ 2=by-class も立てる 3=per-object で直に
		};
		const LineCase kLineCases[] = {
			{"**対照**: 何も書いていない素の線", 0},
			{"クラスに入れただけ", 1},
			{"クラスに入れて `SetArrowByClass` も呼んだ", 2},
			{"**対照**: per-object で直に書いた（クラスとは無関係）", 3},
		};
		const size_t kLineCasesCount = sizeof(kLineCases) / sizeof(kLineCases[0]);

		for (size_t i = 0; i < kLineCasesCount; ++i)
		{
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + std::string(kLineCases[i].label) +
						  " | **引けなかった（nil）** | | | | |");
				continue;
			}
			if (kLineCases[i].kind == 1 || kLineCases[i].kind == 2)
				gSDK->SetObjectClass(line, classIndex);
			if (kLineCases[i].kind == 2)
				gSDK->SetArrowByClass(line);
			if (kLineCases[i].kind == 3)
				WriteModernAll(line, kNoFillDimSlashMarker, kSizeInch, kProbeAngle);
			probe.log("| " + std::string(kLineCases[i].label) + " | " +
					  std::string(gSDK->GetArrowByClass(line) != 0 ? "yes" : "no") + " | " +
					  MarkerPolyCells(line) + " |");
		}
		probe.log("");
		probe.log("**読み方**: 「クラスに入れて `SetArrowByClass` も呼んだ」行に図形が"
				  "**あり**、素の線に**なし**なら、**クラスのマーカーは本当に線へ下りている**"
				  "（per-object の口で読めないだけ）。by-class を立てても図形が出ないなら、"
				  "**クラスのマーカーは絵に出ていない**——`visibility` の引数が無い口なので、"
				  "点ける道が別に要ることになる。");
		probe.log("");
		probe.log("**対照の 2 本が効く**——素の線に図形が出ないこと（＝この口が"
				  "「マーカーの有無」を映すこと）と、per-object で直に書いた線に図形が出ること"
				  "（＝この口が壊れていないこと）を、同じ走りの中で確かめている。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 5. 締め
	// =====================================================================
	probe.log("## 5. 締め");
	probe.log("");
	probe.log("開いている図面の件数: " + Num(static_cast<long>(CountOpenDocuments())) +
			  "（走らせる前と同じなら、開いた図面はすべて閉じられている）");
	probe.log("");
	probe.log("**利用者の図面には何も足していない**——クラスを触る 4 節も含め、"
			  "測るのはすべてプローブが自分で開いて自分で閉じる空の図面の中である。");
}
