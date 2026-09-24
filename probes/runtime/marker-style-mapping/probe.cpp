//
//	probes/runtime/marker-style-mapping/probe.cpp
//
//	[issue #113] **旧口（`SetArrowHeadsN` の `ArrowType`）に書いた数値と、新口
//	（`GetObjBeginningMarker` の `SMarkerStyle.style` ＝ `MarkerType`）で読める数値が
//	一致しない。** PR #110 の実測（7 節）では 4 点とも別の数になった:
//
//	  | 書いた `ArrowType` | 読めた `MarkerType` |
//	  | --- | --- |
//	  | 0 | 0 |
//	  | 2 | **1280** |
//	  | 3 | **2** |
//	  | 1280 | **2048** |
//
//	## 先に `sdk-grep` で分かったこと（実機へ持っていく前に潰した筋）
//
//	**`ArrowType` と `MarkerType` は別々の enum である。** `Kernel/API/MiniCadCallBacks.h`
//	は 756 行目から**2 つの体系を続けて**定義しており、Findings が
//	「`ArrowType` の値は `MarkerType` の体系である【ヘッダ根拠】」と書いていたのは、
//	**隣の enum を読み違えていた**ということになる:
//
//	    // ArrowType - for specifying arrowheads using the old style calls
//	    enum { arArrow = 0, arTightArrow = 1, arBall = 2, arSlash = 3, arCross = 4 };
//	    typedef Sint32 ArrowType;
//
//	    // MarkerType - for specifying marker styles using the new marker calls
//	    enum { … kArrowMarker = 0, kConcaveCurvedArrowMarker = 1, kCircleMarker = 2,
//	           kDimSlashMarker = 3, … kOpenBaseNoFillMarker = 1280, … };
//
//	**ところが実測は `arArrow/arBall/arSlash` の並びとも合わない**——`arBall = 2` を
//	書いたのに丸（`kCircleMarker = 2`）ではなく `1280`（開いた矢印）が読めている。
//	合うのは**もう 1 つ別の並び**、VWFC の `EMarkerType`（`VWFC/VWFCLibrary.h:62`）である:
//
//	    enum EMarkerType { kMarkerFilledArrow = 0, kMarkerEmptyArrow = 1,
//	                       kMarkerOpenArrow = 2, kMarkerFilledBall = 3,
//	                       kMarkerEmptyBall = 4, kMarkerSlash = 5, kMarkerCross = 6 };
//
//	  * 書いた `0`（塗り矢印）→ `kArrowMarker = 0`（矢印・線色塗り）——合う
//	  * 書いた `2`（**開いた**矢印）→ `kOpenBaseNoFillMarker = 1280`（開いた台・塗り無し）——合う
//	  * 書いた `3`（塗り丸）→ `kCircleMarker = 2`（丸）——合う
//
//	**つまり有力な仮説は「旧口の `style` は `EMarkerType` の番号（0〜6）であって、
//	`ArrowType` の enum でも `MarkerType` の値でもない」。** この調査はそれを
//	**0 から順に掃引して確かめ、対応表そのものを作る**。仮説が外れても、表は残る。
//
//	## もう 1 つ、ヘッダで見つけた口
//
//	**`MarkerType` をそのまま書ける「中口」がある**（`Interfaces/VectorWorks/ISDK.h:941`）:
//
//	    void GetMarker(MCObjectHandle obj, MarkerType& style, short& size, short& angle,
//	                   Boolean& start, Boolean& end);
//	    void SetMarker(MCObjectHandle obj, MarkerType style, short size, short angle,
//	                   Boolean start, Boolean end);
//
//	**`angle` を引数に持つのはこの口だけ**なので、issue の「`nAngle` は旧口から書けるのか」
//	はここで答えが出る見込みが高い（旧口 `…ArrowHeadsN` に角度の引数は無い）。
//	4・5 節で確かめる。
//
//	**器の大きさも 2 つの体系で違う**——`typedef Sint32 ArrowType;`（756 行目付近）に対し
//	`typedef Uint16 MarkerType;`（`MiniCadCallBacks.h:834`）。**`MarkerType` は符号無し
//	16 ビット**なので、`MarkerType` の側は 0〜65535 しか表せない。2 節で `32768` や
//	`65536` や負の値を書くのは、この境目で何が起きるかを見るためでもある。
//
//	## 確かめること（issue の切り分け項目に 1 節ずつ対応させてある）
//
//	  | 節 | 何を見るか | issue の項目 |
//	  | --- | --- | --- |
//	  | 1 | 旧口へ 0〜19 を順に書き、3 つの口で読む**対応表** | 小さな連番か |
//	  | 2 | **範囲外**の値（20〜65536・負）を書いたら何になるか | 飽和か下位ビットか |
//	  | 3 | **逆向き**（新口で `MarkerType` を書き、旧口で読む） | 逆向きはどうか |
//	  | 4 | **中口**（`SetMarker`。`MarkerType` ＋ `angle`）で書く | — |
//	  | 5 | `nAngle` を書けるか（新口・中口） | `nAngle` は書けるか |
//	  | 6 | **文書の既定**でも同じ対応か | 一般性 |
//
//	マスク（`kMarkerRootTypeMask` ほか）は独立した節にせず、**1〜4 節の表の中で
//	そのつど分解して出す**——「新口で読めた値に対してだけ意味があるのか」は、
//	旧口で読めた値を同じマスクで割った欄と見比べれば、その場で分かる。
//
//	**利用者の図面は触らない**——測るのはすべて、このプローブが自分で開いて自分で閉じる
//	空の図面の中である（`OpenDocumentPath(nullptr, false)` / `CloseDocument()`）。
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

	// --- `MarkerType` の分解と命名 ---------------------------------------
	//
	// ヘッダのマスク（`MiniCadCallBacks.h:811-815`）でそのまま割る。
	// **旧口で読めた値も同じマスクで割って並べる**ので、「マスクは新口の値にだけ
	// 意味があるのか」は表を見比べればその場で分かる。

	long RootOf(long style)
	{
		return style & static_cast<long>(kMarkerRootTypeMask);
	}

	long FillOf(long style)
	{
		return style & static_cast<long>(kMarkerFillMask);
	}

	long BaseOf(long style)
	{
		return style & static_cast<long>(kMarkerBaseMask);
	}

	long HalfTickOf(long style)
	{
		return style & static_cast<long>(kMarkerHalfTickMask);
	}

	long TailOf(long style)
	{
		return style & static_cast<long>(kMarkerTailMask);
	}

	// 根種別の定数名。**表に名前が出ると、対応表がそのまま読める**。
	const char* RootName(long style)
	{
		switch (RootOf(style))
		{
		case kArrowMarker:
			return "kArrowMarker(矢印)";
		case kConcaveCurvedArrowMarker:
			return "kConcaveCurvedArrowMarker(反り矢印)";
		case kCircleMarker:
			return "kCircleMarker(丸)";
		case kDimSlashMarker:
			return "kDimSlashMarker(スラッシュ)";
		case kDimCrossMarker:
			return "kDimCrossMarker(十字)";
		case kLassoMarker:
			return "kLassoMarker(投げ縄)";
		case kHexagonMarker:
			return "kHexagonMarker(六角)";
		case kVShapedMarker:
			return "kVShapedMarker(V)";
		case kConeMarker:
			return "kConeMarker(円錐)";
		case kTaperedVShapedMarker:
			return "kTaperedVShapedMarker(先細 V)";
		case kSShapedMarker:
			return "kSShapedMarker(S)";
		case kRectangleMarker:
			return "kRectangleMarker(矩形)";
		case kDoubleLineMarker:
			return "kDoubleLineMarker(二重線)";
		default:
			return "（未知）";
		}
	}

	// 塗り・台の名前。複合定数を読み解くのに要る。
	const char* FillName(long style)
	{
		switch (FillOf(style))
		{
		case kLineColorFillMarker:
			return "線色";
		case kWhiteFillMarker:
			return "白";
		case kNoFillMarker:
			return "無し";
		default:
			return "?";
		}
	}

	const char* BaseName(long style)
	{
		switch (BaseOf(style))
		{
		case kFlatBaseMarker:
			return "平";
		case kOpenBaseMarker:
			return "開";
		case kAngleBaseMarker:
			return "角";
		case kArcBaseMarker:
			return "弧";
		default:
			return "?";
		}
	}

	// 分解した 5 欄をまとめて 1 行のセル群にする（表の右半分）。
	std::string DecomposeCells(long style)
	{
		return Num(RootOf(style)) + " | " + Num(FillOf(style)) + " | " + Num(BaseOf(style)) +
			   " | " + Num(HalfTickOf(style)) + " | " + Num(TailOf(style)) + " | " +
			   RootName(style) + " / 塗り=" + FillName(style) + " / 台=" + BaseName(style);
	}

	// --- 旧口（`…ArrowHeadsN`。style は `ArrowType`） ---------------------

	struct LegacyArrow
	{
		Boolean starting = 0;
		Boolean ending = 0;
		ArrowType style = 0;
		double_gs size = 0;
	};

	LegacyArrow ReadLegacyObject(MCObjectHandle object)
	{
		LegacyArrow value;
		gSDK->GetArrowHeadsN(object, value.starting, value.ending, value.style, value.size);
		return value;
	}

	LegacyArrow ReadLegacyDefault()
	{
		LegacyArrow value;
		gSDK->GetDefaultArrowHeadsN(value.starting, value.ending, value.style, value.size);
		return value;
	}

	// --- 中口（`Get/SetMarker`。style は `MarkerType`、angle を持つ） -----

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
		Boolean ok = 0;			// 呼び出しの戻り値（参考。判断には使わない）
		Boolean visibility = 0; // マーカーが付いているか
		SMarkerStyle style{};
	};

	MarkerEnd ReadModernBeginning(MCObjectHandle object)
	{
		MarkerEnd end;
		end.ok = gSDK->GetObjBeginningMarker(object, end.style, end.visibility);
		return end;
	}

	MarkerEnd ReadModernEnd(MCObjectHandle object)
	{
		MarkerEnd end;
		end.ok = gSDK->GetObjEndMarker(object, end.style, end.visibility);
		return end;
	}

	MarkerEnd ReadModernDefaultBeginning()
	{
		MarkerEnd end;
		end.ok = gSDK->GetDefaultBeginningMarker(end.style, end.visibility);
		return end;
	}

	long ModernStyleOf(const MarkerEnd& end)
	{
		return static_cast<long>(end.style.style);
	}

	// --- 証人の線 --------------------------------------------------------

	int gSerial = 0;

	MCObjectHandle CreateWitnessLine()
	{
		const WorldCoord x = static_cast<WorldCoord>(gSerial++) * 1000;
		return gSDK->CreateLine(WorldPt(x, 0), WorldPt(x, 1000));
	}

	// --- 図面を開く・閉じる（#108 / PR #110 と同じ作法） ------------------

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
					  "**文書の既定を書く節では利用者の図面の既定を書き換えてしまう**ので、"
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

	// 1 節。`EMarkerType` は 0〜6 の 7 つなので、**その倍以上まで**回して
	// 「どこまでが有効な連番か」を見る。
	const long kLegacySweep[] = {0,	 1,	 2,	 3,	 4,	 5,	 6,	 7,	 8,	 9,
								 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
	const size_t kLegacySweepCount = sizeof(kLegacySweep) / sizeof(kLegacySweep[0]);

	// 2 節。**範囲外の振る舞いを 3 つの仮説で切り分ける値**を選んである。
	//   飽和なら   → どれも 1 節の最後の有効値と同じものになる
	//   下位ビット → 127 / 128 / 255 / 256 の組で割れる（マスクの境目だから）
	//   剰余       → 7 の倍数まわり（7 / 14 / 21）が 0 に戻る
	// `1280` と `2048` は issue が実際に踏んだ値、`32767` / `65536` は
	// short / Uint16 の境目、負は符号の扱いを見るため。
	const long kOutOfRangeSweep[] = {20,   21,	 25,	50,	   100,	  126,	 127,	128,  129,
									 255,  256,	 259,	260,   261,	  512,	 1024,	1280, 2048,
									 3072, 4096, 16384, 32768, 32767, 65535, 65536, -1,	  -2};
	const size_t kOutOfRangeSweepCount = sizeof(kOutOfRangeSweep) / sizeof(kOutOfRangeSweep[0]);

	// 3・4 節。**`MarkerType` の側の値**（単純定数と複合定数の両方）。
	struct NamedMarker
	{
		long value;
		const char* name;
	};

	const NamedMarker kMarkerValues[] = {
		{kArrowMarker, "kArrowMarker"},
		{kConcaveCurvedArrowMarker, "kConcaveCurvedArrowMarker"},
		{kCircleMarker, "kCircleMarker"},
		{kDimSlashMarker, "kDimSlashMarker"},
		{kDimCrossMarker, "kDimCrossMarker"},
		{kLassoMarker, "kLassoMarker"},
		{kHexagonMarker, "kHexagonMarker"},
		{kVShapedMarker, "kVShapedMarker"},
		{kRectangleMarker, "kRectangleMarker"},
		{kDoubleLineMarker, "kDoubleLineMarker"},
		{kWhiteFillMarker, "kWhiteFillMarker(矢印＋白)"},
		{kNoFillMarker, "kNoFillMarker(矢印＋塗り無し)"},
		{kNoFillDimSlashMarker, "kNoFillDimSlashMarker"},
		{kNoFillDimCrossMarker, "kNoFillDimCrossMarker"},
		{kNoFillLassoMarker, "kNoFillLassoMarker"},
		{kOpenBaseNoFillMarker, "kOpenBaseNoFillMarker"},
		{kAngleBaseMarker, "kAngleBaseMarker"},
		{kArcBaseMarker, "kArcBaseMarker"},
		{kCircleMarker | kNoFillMarker, "kCircleMarker|kNoFillMarker"},
		{kTailMarker, "kTailMarker(矢印＋尾)"},
		{kLeftHalfTickMarker, "kLeftHalfTickMarker"},
	};
	const size_t kMarkerValuesCount = sizeof(kMarkerValues) / sizeof(kMarkerValues[0]);

	// 5 節。角度。`SMarkerStyle::nAngle` は実測で全区間 `15` だった（#108）。
	const long kAngleSweep[] = {0, 5, 15, 30, 45, 60, 90, 120, 180};
	const size_t kAngleSweepCount = sizeof(kAngleSweep) / sizeof(kAngleSweep[0]);

	// 掃引で使う大きさ（この調査では大きさは主役ではない。往復する値を選ぶ——#108）。
	const double kProbeSize = 0.5000;
} // namespace

VW_PROBE("marker-style-mapping", "旧口の ArrowType と新口の MarkerType の対応表を作る",
		 "0 から順に掃引して対応を出し、範囲外・逆向き・中口・角度まで確かめる")
{
	probe.log("**旧口（`SetArrowHeadsN` の `ArrowType`）へ書いた数値と、新口"
			  "（`GetObjBeginningMarker` の `SMarkerStyle.style` ＝ `MarkerType`）で読める"
			  "数値が一致しない**（#110 の 7 節: 0→0 / 2→1280 / 3→2 / 1280→2048）。"
			  "**その対応表を作る**のがこの調査である。");
	probe.log("");
	probe.log("**先に `sdk-grep` で分かっていること**——`ArrowType` と `MarkerType` は"
			  "`MiniCadCallBacks.h` の中で**別々に定義された 2 つの体系**である"
			  "（756 行目から順に `enum { arArrow=0, arTightArrow=1, arBall=2, arSlash=3, "
			  "arCross=4 }; typedef Sint32 ArrowType;` と、`kArrowMarker=0 … "
			  "kOpenBaseNoFillMarker=1280 …` の `MarkerType`）。Findings の"
			  "「`ArrowType` の値は `MarkerType` の体系である【ヘッダ根拠】」は"
			  "**隣の enum を読み違えていた**ことになる。");
	probe.log("");
	probe.log("**ところが実測は `arArrow/arBall/arSlash` の並びとも合わない。** 合うのは"
			  "VWFC の `EMarkerType`（`VWFC/VWFCLibrary.h:62`: 塗り矢印=0 / 白矢印=1 / "
			  "開矢印=2 / 塗り丸=3 / 白丸=4 / スラッシュ=5 / 十字=6）である——"
			  "書いた `2`（開矢印）が `kOpenBaseNoFillMarker=1280`、書いた `3`（塗り丸）が"
			  "`kCircleMarker=2` になったのはこれで説明が付く。**この仮説を掃引で確かめ、"
			  "対応表そのものを作る**（外れても表は残る）。");
	probe.log("");
	probe.log("**もう 1 つ、ヘッダで見つけた口**——`GetMarker` / `SetMarker`"
			  "（`ISDK.h:941`）は `MarkerType` をそのまま読み書きし、**`angle` の引数を"
			  "持つ唯一の口**である。4・5 節でこれを使う。");
	probe.log("");
	probe.log("**利用者の図面は触らない**——測るのは、このプローブが自分で開いて自分で"
			  "閉じる空の図面の中だけである。");
	probe.log("");

	// =====================================================================
	// 1. 旧口へ 0 から順に書き、3 つの口で読む
	// =====================================================================
	probe.log("## 1. 旧口へ 0〜19 を順に書き、3 つの口で読む（対応表）");
	probe.log("");
	probe.log("**1 つの値につき新しい線を 1 本引いて**書く（前の値の残りを引きずらないため）。"
			  "`SetArrowHeadsN(line, yes, yes, <値>, " +
			  Real(kProbeSize) + ")`。");
	probe.log("");
	probe.log("**読み方**——`EMarkerType` の並び（塗り矢印 / 白矢印 / 開矢印 / 塗り丸 / "
			  "白丸 / スラッシュ / 十字）が 0〜6 に並べば仮説は当たり。"
			  "7 以降が同じ値で埋まれば、そこが**有効な範囲の終わり**である。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("### 1-A. 3 つの口が何を返すか");
		probe.log("");
		probe.log("| 書いた `ArrowType` | 旧口の読み | 中口 `GetMarker` style | 中口 angle | "
				  "新口・始点 style | 新口・終点 style | 新口 `nAngle` |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- |");

		// 分解の表は 2 周目に出す（1 周目で線を引き直さないよう、値を控えておく）。
		long modernStyles[kLegacySweepCount];
		long legacyStyles[kLegacySweepCount];

		for (size_t i = 0; i < kLegacySweepCount; ++i)
		{
			modernStyles[i] = 0;
			legacyStyles[i] = 0;
			const long wanted = kLegacySweep[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Num(wanted) + " | **引けなかった（nil）** | | | | | |");
				continue;
			}
			gSDK->SetArrowHeadsN(line, static_cast<Boolean>(1), static_cast<Boolean>(1),
								 static_cast<ArrowType>(wanted), kProbeSize);
			const LegacyArrow legacy = ReadLegacyObject(line);
			const MiddleMarker middle = ReadMiddle(line);
			const MarkerEnd beginning = ReadModernBeginning(line);
			const MarkerEnd ending = ReadModernEnd(line);
			legacyStyles[i] = static_cast<long>(legacy.style);
			modernStyles[i] = ModernStyleOf(beginning);
			probe.log("| " + Num(wanted) + " | " + Num(legacyStyles[i]) + " | " +
					  Num(static_cast<long>(middle.style)) + " | " +
					  Num(static_cast<long>(middle.angle)) + " | " + Num(modernStyles[i]) + " | " +
					  Num(ModernStyleOf(ending)) + " | " +
					  Num(static_cast<long>(beginning.style.nAngle)) + " |");
		}
		probe.log("");

		probe.log("### 1-B. 読めた値をマスクで分解する");
		probe.log("");
		probe.log("**マスクは新口で読めた値にだけ意味があるのか**を見るため、"
				  "**旧口で読めた値も同じマスクで割って**並べる（`kMarkerRootTypeMask=127` / "
				  "`kMarkerFillMask=896` / `kMarkerBaseMask=7168` / "
				  "`kMarkerHalfTickMask=24576` / `kMarkerTailMask=32768`）。");
		probe.log("");
		probe.log("| 書いた | 新口 style | 根 | 塗り | 台 | 半 | 尾 | 名前 |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kLegacySweepCount; ++i)
		{
			probe.log("| " + Num(kLegacySweep[i]) + " | " + Num(modernStyles[i]) + " | " +
					  DecomposeCells(modernStyles[i]) + " |");
		}
		probe.log("");
		probe.log("| 書いた | **旧口** style | 根 | 塗り | 台 | 半 | 尾 | 名前 |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kLegacySweepCount; ++i)
		{
			probe.log("| " + Num(kLegacySweep[i]) + " | " + Num(legacyStyles[i]) + " | " +
					  DecomposeCells(legacyStyles[i]) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 2. 範囲外の値
	// =====================================================================
	probe.log("## 2. 範囲外の値を書いたら何になるか");
	probe.log("");
	probe.log("**飽和か、下位ビットだけか、剰余か、無意味な値か**を分ける。"
			  "1 節で見つかった「有効な範囲の終わり」と見比べて読む:");
	probe.log("");
	probe.log("- どれも**1 節の最後の有効値と同じ**になる → **飽和（クランプ）**");
	probe.log("- `127` / `128` / `255` / `256` の組で割れる → **下位ビットだけが使われている**"
			  "（マスクの境目だから、ここで割れる）");
	probe.log("- `7` の倍数まわりで 0 に戻る → **剰余**");
	probe.log("- 負の値が巨大な正の値として読める → **符号なしとして扱われている**");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 書いた `ArrowType` | 旧口の読み | 中口 style | 新口・始点 style | "
				  "根 | 塗り | 台 | 半 | 尾 | 名前 |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kOutOfRangeSweepCount; ++i)
		{
			const long wanted = kOutOfRangeSweep[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Num(wanted) + " | **引けなかった（nil）** | | | | | | | | |");
				continue;
			}
			gSDK->SetArrowHeadsN(line, static_cast<Boolean>(1), static_cast<Boolean>(1),
								 static_cast<ArrowType>(wanted), kProbeSize);
			const LegacyArrow legacy = ReadLegacyObject(line);
			const MiddleMarker middle = ReadMiddle(line);
			const MarkerEnd beginning = ReadModernBeginning(line);
			const long style = ModernStyleOf(beginning);
			probe.log("| " + Num(wanted) + " | " + Num(static_cast<long>(legacy.style)) + " | " +
					  Num(static_cast<long>(middle.style)) + " | " + Num(style) + " | " +
					  DecomposeCells(style) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 3. 逆向き（新口で MarkerType を書いて、旧口で読む）
	// =====================================================================
	probe.log("## 3. 逆向き——新口で `MarkerType` を書き、旧口で読む");
	probe.log("");
	probe.log("**新口へ書いた値がそのまま往復するか**（＝新口が「様式を指定する道」になるか）が"
			  "この節の主役である。あわせて、そのとき旧口が何を返すかも見る"
			  "（旧口の読みが `EMarkerType` の番号へ戻るなら、対応は**両向きの変換表**である）。");
	probe.log("");
	probe.log("書き方は「読んでから `style` だけ差し替えて書き戻す」——`SMarkerStyle` の"
			  "他の欄（大きさなど）を壊さないため。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 新口へ書いた style | 定数 | 戻り値 | 新口の読み（往復したか） | "
				  "中口 style | 旧口の読み | 根 | 塗り | 台 | 半 | 尾 |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kMarkerValuesCount; ++i)
		{
			const long wanted = kMarkerValues[i].value;
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Num(wanted) + " | " + kMarkerValues[i].name +
						  " | **引けなかった（nil）** | | | | | | | | |");
				continue;
			}
			MarkerEnd before = ReadModernBeginning(line);
			SMarkerStyle writing = before.style;
			writing.style = static_cast<decltype(writing.style)>(wanted);
			writing.dSize = kProbeSize;
			const Boolean ret = gSDK->SetObjBeginningMarker(line, writing, static_cast<Boolean>(1));
			const MarkerEnd after = ReadModernBeginning(line);
			const MiddleMarker middle = ReadMiddle(line);
			const LegacyArrow legacy = ReadLegacyObject(line);
			const long style = ModernStyleOf(after);
			probe.log("| " + Num(wanted) + " | `" + kMarkerValues[i].name + "` | " + YesNo(ret) +
					  " | " + Num(style) + "（" + (style == wanted ? "**往復した**" : "**違う**") +
					  "） | " + Num(static_cast<long>(middle.style)) + " | " +
					  Num(static_cast<long>(legacy.style)) + " | " + Num(RootOf(style)) + " | " +
					  Num(FillOf(style)) + " | " + Num(BaseOf(style)) + " | " +
					  Num(HalfTickOf(style)) + " | " + Num(TailOf(style)) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 4. 中口（SetMarker）で MarkerType を書く
	// =====================================================================
	probe.log("## 4. 中口（`SetMarker`）で `MarkerType` を書く");
	probe.log("");
	probe.log("`SetMarker(h, style, size, angle, start, end)` は **`MarkerType` を"
			  "そのまま受ける**と宣言されている（`ISDK.h`）。**本当にそうなら、"
			  "「様式を指定する道」が新口以外にもある**ことになる。`size` はポイント、"
			  "`angle` は度（いずれも `short`）と見て 36 ポイント / 45 度を書く。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 中口へ書いた style | 定数 | 中口の読み style | 往復したか | "
				  "中口 angle | 新口 style | 新口 `nAngle` | 旧口の読み |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kMarkerValuesCount; ++i)
		{
			const long wanted = kMarkerValues[i].value;
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Num(wanted) + " | " + kMarkerValues[i].name +
						  " | **引けなかった（nil）** | | | | | |");
				continue;
			}
			gSDK->SetMarker(line, static_cast<MarkerType>(wanted), static_cast<short>(36),
							static_cast<short>(45), static_cast<Boolean>(1),
							static_cast<Boolean>(1));
			const MiddleMarker middle = ReadMiddle(line);
			const MarkerEnd beginning = ReadModernBeginning(line);
			const LegacyArrow legacy = ReadLegacyObject(line);
			const long readMiddle = static_cast<long>(middle.style);
			probe.log("| " + Num(wanted) + " | `" + kMarkerValues[i].name + "` | " +
					  Num(readMiddle) + " | " +
					  std::string(readMiddle == wanted ? "**往復した**" : "**違う**") + " | " +
					  Num(static_cast<long>(middle.angle)) + " | " + Num(ModernStyleOf(beginning)) +
					  " | " + Num(static_cast<long>(beginning.style.nAngle)) + " | " +
					  Num(static_cast<long>(legacy.style)) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 5. 角度（nAngle）は書けるか
	// =====================================================================
	probe.log("## 5. 角度（`nAngle`）は書けるか");
	probe.log("");
	probe.log("#108 の実測では `nAngle` は**全区間 `15`** だった（読むだけで、書いていない）。"
			  "**書ける口があるのか**を、新口（`SMarkerStyle.nAngle`）と中口"
			  "（`SetMarker` の `angle`）の両方で確かめる。旧口には角度の引数が無いので、"
			  "**旧口で書いた線の `nAngle` が何になるか**もあわせて見る。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 書いた角度 | 新口で書く → 新口の `nAngle` | 往復したか | "
				  "新口で書く → 中口 angle | 中口で書く → 中口 angle | 往復したか | "
				  "中口で書く → 新口 `nAngle` |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kAngleSweepCount; ++i)
		{
			const long wanted = kAngleSweep[i];

			long modernAngle = -999, modernToMiddle = -999;
			MCObjectHandle lineModern = CreateWitnessLine();
			if (lineModern != nil)
			{
				MarkerEnd before = ReadModernBeginning(lineModern);
				SMarkerStyle writing = before.style;
				writing.style = static_cast<decltype(writing.style)>(kCircleMarker);
				writing.dSize = kProbeSize;
				writing.nAngle = static_cast<decltype(writing.nAngle)>(wanted);
				gSDK->SetObjBeginningMarker(lineModern, writing, static_cast<Boolean>(1));
				const MarkerEnd after = ReadModernBeginning(lineModern);
				modernAngle = static_cast<long>(after.style.nAngle);
				modernToMiddle = static_cast<long>(ReadMiddle(lineModern).angle);
			}

			long middleAngle = -999, middleToModern = -999;
			MCObjectHandle lineMiddle = CreateWitnessLine();
			if (lineMiddle != nil)
			{
				gSDK->SetMarker(lineMiddle, static_cast<MarkerType>(kCircleMarker),
								static_cast<short>(36), static_cast<short>(wanted),
								static_cast<Boolean>(1), static_cast<Boolean>(1));
				middleAngle = static_cast<long>(ReadMiddle(lineMiddle).angle);
				middleToModern = static_cast<long>(ReadModernBeginning(lineMiddle).style.nAngle);
			}

			probe.log("| " + Num(wanted) + " | " + Num(modernAngle) + " | " +
					  std::string(modernAngle == wanted ? "**往復した**" : "**違う**") + " | " +
					  Num(modernToMiddle) + " | " + Num(middleAngle) + " | " +
					  std::string(middleAngle == wanted ? "**往復した**" : "**違う**") + " | " +
					  Num(middleToModern) + " |");
		}
		probe.log("");

		probe.log("旧口（`SetArrowHeadsN`。角度の引数を持たない）で書いた線の角度:");
		probe.log("");
		MCObjectHandle lineLegacy = CreateWitnessLine();
		if (lineLegacy != nil)
		{
			gSDK->SetArrowHeadsN(lineLegacy, static_cast<Boolean>(1), static_cast<Boolean>(1),
								 static_cast<ArrowType>(3), kProbeSize);
			const MarkerEnd beginning = ReadModernBeginning(lineLegacy);
			probe.log("- 新口 `nAngle` = " + Num(static_cast<long>(beginning.style.nAngle)) +
					  " / 中口 angle = " + Num(static_cast<long>(ReadMiddle(lineLegacy).angle)));
		}
		else
		{
			probe.log("- **線を引けなかった（nil）**");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 6. 文書の既定でも同じ対応か
	// =====================================================================
	probe.log("## 6. 文書の既定でも同じ対応か");
	probe.log("");
	probe.log("**1 節の対応が per-object だけの話なのか、口そのものの性質なのか**を分ける。"
			  "`SetDefaultArrowHeadsN` へ同じ値を書き、`GetDefaultBeginningMarker` で読む。"
			  "**同じ表になれば、対応は「旧口 ⇄ 新口」の変換そのもの**である。");
	probe.log("");
	probe.log("あわせて**書いた直後に線を 1 本引いて**、そこから生まれたオブジェクトが"
			  "何を持つかも読む（既定はオブジェクトが生まれる瞬間に読まれる——#94）。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 既定へ書いた `ArrowType` | 既定・旧口の読み | 既定・新口 style | "
				  "生まれた線・新口 style | 名前 |");
		probe.log("| --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kLegacySweepCount && kLegacySweep[i] <= 9; ++i)
		{
			const long wanted = kLegacySweep[i];
			gSDK->SetDefaultArrowHeadsN(static_cast<Boolean>(1), static_cast<Boolean>(1),
										static_cast<ArrowType>(wanted), kProbeSize);
			const LegacyArrow legacy = ReadLegacyDefault();
			const MarkerEnd beginning = ReadModernDefaultBeginning();
			const long style = ModernStyleOf(beginning);
			MCObjectHandle born = CreateWitnessLine();
			const std::string bornStyle =
				born != nil ? Num(ModernStyleOf(ReadModernBeginning(born))) : "（引けなかった）";
			probe.log("| " + Num(wanted) + " | " + Num(static_cast<long>(legacy.style)) + " | " +
					  Num(style) + " | " + bornStyle + " | " + RootName(style) +
					  " / 塗り=" + FillName(style) + " / 台=" + BaseName(style) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 締め
	// =====================================================================
	probe.log("## 7. 締め");
	probe.log("");
	probe.log("開いている図面の件数: " + Num(static_cast<long>(CountOpenDocuments())) +
			  "（走らせる前と同じなら、開いた図面はすべて閉じられている）");
	probe.log("");
	probe.log("**利用者の図面には何も足していない**——文書の既定を書き換える 6 節も含め、"
			  "測るのはすべてプローブが自分で開いて自分で閉じる空の図面の中である"
			  "（図面を開けなかったときは、その旨が上に出ている）。");
	probe.log("");
	probe.log("### 読み方");
	probe.log("");
	probe.log("- **1-A の「新口・始点 style」の欄が 0〜6 で `EMarkerType` の並びになる** → "
			  "**旧口の `style` は `EMarkerType` の番号**であり、`MarkerType` の定数を"
			  "そのまま渡してはいけない、と確定する。");
	probe.log("- **1-A の「旧口の読み」が書いた値をそのまま返す** → 旧口は**自分の体系の"
			  "ままで往復する**（＝旧口だけで閉じているぶんには食い違わない）。"
			  "違う値を返すなら、旧口の読みも変換を通っていることになる。");
	probe.log("- **1-B で、旧口で読めた値をマスクで割った欄が意味を成さない** → "
			  "**マスクは新口（`MarkerType`）の値にだけ意味がある**。");
	probe.log("- **2 節で範囲外がすべて同じ値になる** → 飽和。`127`/`128`/`255`/`256` で"
			  "割れるなら下位ビット。`1280` が `2048` になった件も、ここで説明が付く。");
	probe.log("- **3 節で新口へ書いた値がそのまま読める** → **様式を指定する道は新口**"
			  "（大きさが新口だったのと同じ結論。#108）。往復しない値があれば、"
			  "それは「その組み合わせが存在しない」ということである。");
	probe.log("- **4 節で中口が `MarkerType` をそのまま往復させる** → "
			  "**`SetMarker` も様式を指定する道**であり、`SMarkerStyle` を組まずに"
			  "様式・大きさ・角度を 1 呼び出しで書ける。");
	probe.log("- **5 節で `nAngle` が往復する口がある** → 角度は**その口から**書ける。"
			  "どちらも往復しないなら、角度は SDK からは書けない。");
	probe.log("- **6 節が 1 節と同じ表になる** → 対応は per-object の話ではなく、"
			  "**旧口という口そのものの性質**である。");
}
