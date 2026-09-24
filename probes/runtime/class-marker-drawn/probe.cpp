//
//	probes/runtime/class-marker-drawn/probe.cpp
//
//	[issue #120] **クラスへ置いたマーカーは、線の絵に本当に出るのか。**
//
//	#116（PR #117）の 2 周目 4 節で、**by-class の旗が立っている線には
//	`GetMarkerPolys` が図形を返さない**と測れた——素の線（図形なし）と per-object 直書き
//	（図形あり・種別 21・境界 4.4901 × 4.4901）の対照が両方効いていたので、
//	**`GetMarkerPolys` そのものは壊れていない**。それでも by-class の線では出なかった。
//
//	`Findings/Attributes and Classes.md` は「**マーカーが要るなら per-object の
//	`SetArrowByClass` を呼ぶ**（無料）」と案内している。**その案内で本当に絵に出るのか**を
//	ここで確定させる。
//
//	## 実機へ持っていく前に SDK で潰したこと（ヘッダ・同梱ソース）
//
//	1. **クラスのマーカーには visibility（有無）の口がどこにも無い。**【ヘッダ根拠】
//	   per-object の新口は `SetObjBeginningMarker(h, mstyle, visibility)` と
//	   有無を引数に持つのに、クラス側の 2 つは持たない:
//	     * `SetClassBeginningMarker(InternalIndex, SMarkerStyle)`（`ISDK.h`）
//	     * `SetClMarker(InternalIndex, MarkerType style, short size, short angle)`
//	       ——**per-object の中口 `SetMarker` にはある `start` / `end` が無い**
//	   VectorScript 側も同じで（`vs.py`）、`SetClassBeginningMarker` /
//	   `SetClassEndMarker` / `SetClassArrow` のどれにも有無の引数は無く、戻り値の
//	   `BOOLEAN` は「Return TRUE if operation was successful」＝成否である。
//	   **クラス属性の口を全数列挙**しても、有無に当たるものは無い（`GetClColor` /
//	   `GetClFillPat` / `GetClLineWeight` / `GetClMarker` / `GetClUseGraphic` /
//	   `GetClVisibility` / `GetClPenPatN` / テクスチャとテキストスタイル）。
//	   `GetClVisibility` は**クラス自体の表示・非表示**で、マーカーとは無関係。
//
//	2. **SDK 自身の参照実装は、クラスのマーカーを per-object へ書き写している。**【ソース根拠】
//	   データタグの引き出し線を作る `VWTaggedObj::CreateLeaderLine`
//	   （`Source/VWSDK/VWFC/Tools/VWTaggedObj.cpp:660` 付近）:
//
//	     SMarkerStyle mstyle;
//	     GS_GetClassEndMarker(gCBP, classID, mstyle);        // ← クラスから読む
//	     GS_SetObjectClass(gCBP, leaderLine, classID);
//	     gSDK->SetObjEndMarker( leaderLine, mstyle, useMarker );  // ← per-object へ書く
//	     VWClass theClass( classID );
//	     if ( theClass.GetUseGraphics() ) {
//	         … SetFColorsByClass / SetFPatByClass / SetLWByClass /
//	           SetPColorsByClass / SetPPatByClass …
//	         gSDK->SetArrowByClass( leaderLine );            // ← その後で by-class 化
//	     }
//
//	   **`SetArrowByClass` だけでは済ませていない**——有無（`useMarker`）を持てるのは
//	   per-object の口だけなので、**クラスの値を読んで per-object へ書き写してから**
//	   by-class にしている。しかも `SetArrowByClass` は**クラスの「グラフィック属性を
//	   使用」（`GetClUseGraphic`）が立っているときだけ**呼ばれている。
//
//	つまり見込みは「**`SetArrowByClass` だけでは描かれない。クラスの値を読んで
//	per-object へ書き写す（visibility=true）のが唯一の道**」だが、**ヘッダとソースでは
//	ここまでしか言えない**（VW 本体が visibility をクラス側に隠し持っていて、
//	`SetArrowByClass` で点く余地は残る）。だから実機で測る。
//
//	## このプローブが測るもの
//
//	  | 節 | 何を確かめるか | issue の項目 |
//	  | --- | --- | --- |
//	  | 1 | **物差しの校正**。`GetMarkerPolys` と**線そのものの境界**が、per-object の
//	        visibility=true / false でどう変わるか | 2 |
//	  | 2 | **クラス経由の道を総当たり**（`SetArrowByClass` 単独・`ResetObject` つき・
//	        `SetClUseGraphic` つき・中口で書く・順序違い・**VW 自身の作法**） | 1・2・4 |
//	  | 3 | **クラスの値を後から変えたら、線は追随するか** | 1 |
//	  | 4 | **文書の既定から生まれた線**（既定は per-object の値として入るはず） | 3 |
//	  | 5 | **目視用の対照図面**——1〜4 の物差しで割れなかったときの保険 | 1 |
//
//	**1 節が効く**——per-object で **visibility=false** の線（値はあるが消えている）に
//	図形が出ないなら、`GetMarkerPolys` は「値の有無」ではなく**実際に描かれるか**を
//	映していることになり、2 節の「図形なし」は「絵に出ていない」と読める。
//	逆に visibility=false でも図形が出るなら、この口は絵を映していないと分かる
//	——そのときは 5 節の図面を見てもらう。
//
//	**利用者の図面は触らない。** 1〜4 節は、プローブが自分で開いて自分で閉じる空の図面の
//	中だけで測る（クラスを作るのも文書の既定を触るのもその中）。**5 節だけは、見るための
//	図面を開いたまま残す**（見終わったら保存せず閉じてよい）。
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

	std::string YesNo(bool value)
	{
		return value ? "yes" : "no";
	}

	// --- 測る値（#113 / #116 のプローブと同じ割り方） ---------------------

	const char* RootName(long style)
	{
		switch (style & static_cast<long>(kMarkerRootTypeMask))
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
		default:
			return "（その他）";
		}
	}

	// 測る対象の様式。#116 の 4 節と同じものを使う（境界 4.4901 × 4.4901 と比べられる）。
	const long kProbeStyle = kNoFillDimSlashMarker; // 259。スラッシュ・塗り無し
	const double kProbeSize = 0.2500;				// インチ（#108: 上限は約 2.0）
	const long kProbeAngle = 45;

	// --- per-object の新口 -----------------------------------------------

	struct ModernRead
	{
		Boolean ok = 0;
		Boolean visibility = 0;
		SMarkerStyle style{};
	};

	ModernRead ReadModernBegin(MCObjectHandle object)
	{
		ModernRead read;
		read.ok = gSDK->GetObjBeginningMarker(object, read.style, read.visibility);
		return read;
	}

	SMarkerStyle MakeStyle(long style, double sizeInInch, long angle)
	{
		SMarkerStyle mstyle{};
		mstyle.style = static_cast<MarkerType>(style);
		mstyle.dSize = sizeInInch;
		mstyle.nAngle = static_cast<Sint8>(angle);
		return mstyle;
	}

	// per-object へ直に書く（両端・有無つき）。
	void WritePerObject(MCObjectHandle object, long style, double sizeInInch, long angle,
						bool visible)
	{
		SMarkerStyle mstyle = MakeStyle(style, sizeInInch, angle);
		gSDK->SetObjBeginningMarker(object, mstyle, static_cast<Boolean>(visible ? 1 : 0));
		gSDK->SetObjEndMarker(object, mstyle, static_cast<Boolean>(visible ? 1 : 0));
	}

	// --- 物差し 1: 描かれるマーカーの図形（`GetMarkerPolys`） -------------
	// **nil かどうかが「描かれているか」の物証**で、大きさは図形の境界で見る。

	struct PolyRead
	{
		bool startThere = false;
		bool endThere = false;
		long type = 0;
		bool boundsOk = false;
		double width = 0.0;
		double height = 0.0;
	};

	double ProbeAbs(double value)
	{
		return value < 0 ? -value : value;
	}

	PolyRead ReadPolys(MCObjectHandle object)
	{
		PolyRead read;
		MCObjectHandle startPoly = nil;
		MCObjectHandle endPoly = nil;
		gSDK->GetMarkerPolys(object, startPoly, endPoly);
		read.startThere = (startPoly != nil);
		read.endThere = (endPoly != nil);
		if (startPoly == nil)
			return read;
		read.type = static_cast<long>(gSDK->GetObjectTypeN(startPoly));
		WorldRect bounds;
		if (gSDK->GetObjectBounds(startPoly, bounds) != 0)
		{
			read.boundsOk = true;
			read.width =
				ProbeAbs(static_cast<double>(bounds.right) - static_cast<double>(bounds.left));
			read.height =
				ProbeAbs(static_cast<double>(bounds.top) - static_cast<double>(bounds.bottom));
		}
		return read;
	}

	// --- 物差し 2: 線そのものの境界（`GetObjectBounds`） ------------------
	// per-object のマーカーが線の境界を広げるなら、by-class でも広がるはずである
	// （広がらないなら、この物差しでは分けられないと分かる——それも結論になる）。

	struct BoundsRead
	{
		bool ok = false;
		double width = 0.0;
		double height = 0.0;
	};

	BoundsRead ReadBounds(MCObjectHandle object)
	{
		BoundsRead read;
		WorldRect bounds;
		if (gSDK->GetObjectBounds(object, bounds) != 0)
		{
			read.ok = true;
			read.width =
				ProbeAbs(static_cast<double>(bounds.right) - static_cast<double>(bounds.left));
			read.height =
				ProbeAbs(static_cast<double>(bounds.top) - static_cast<double>(bounds.bottom));
		}
		return read;
	}

	// --- 1 行にまとめる ---------------------------------------------------

	std::string RowCells(MCObjectHandle object)
	{
		const ModernRead modern = ReadModernBegin(object);
		const PolyRead poly = ReadPolys(object);
		const BoundsRead bounds = ReadBounds(object);

		std::string cells;
		cells += std::string(gSDK->GetArrowByClass(object) != 0 ? "**yes**" : "no") + " | ";
		cells +=
			std::string("ok=") + YesNo(modern.ok != 0) + " vis=" + YesNo(modern.visibility != 0);
		if (modern.ok != 0)
			cells += " style=" + Num(static_cast<long>(modern.style.style)) + "（" +
					 RootName(static_cast<long>(modern.style.style)) +
					 "） dSize=" + Real(modern.style.dSize);
		cells += " | ";
		cells += std::string(poly.startThere ? "**あり**" : "なし") + " / " +
				 std::string(poly.endThere ? "**あり**" : "なし") + " | ";
		if (!poly.startThere)
			cells += "— | ";
		else if (poly.boundsOk)
			cells += "種別 " + Num(poly.type) + "・" + Real(poly.width) + " × " +
					 Real(poly.height) + " | ";
		else
			cells += "種別 " + Num(poly.type) + "・**境界を読めない** | ";
		if (bounds.ok)
			cells += Real(bounds.width) + " × " + Real(bounds.height);
		else
			cells += "**読めない**";
		return cells;
	}

	const char* kHeader = "| 線 | by-class の旗 | per-object の新口の読み | 図形（始 / 終） | "
						  "図形の境界（幅 × 高さ） | **線そのものの境界** |";
	const char* kHeaderRule = "| --- | --- | --- | --- | --- | --- |";

	// --- 証人の線 --------------------------------------------------------

	int gSerial = 0;

	MCObjectHandle CreateWitnessLine()
	{
		const WorldCoord x = static_cast<WorldCoord>(gSerial++) * 1000;
		return gSDK->CreateLine(WorldPt(x, 0), WorldPt(x, 1000));
	}

	// --- 図面を開く・閉じる（#108 / #113 / #116 と同じ作法） --------------

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
					  "）。この節はいまの図面で測る——**利用者の図面にクラスを作り、"
					  "文書の既定を書き換えてしまう**ので、そのつもりで読むこと。");
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

	// --- 測る先のクラス（節ごとに、その節の図面の中で作る） ---------------

	InternalIndex PrepareClass(vwprobe::Report& probe, long style, double sizeInInch, long angle,
							   bool announce)
	{
		InternalIndex index = gSDK->AddClass(TXString("VwSdkProbe120"));
		if (!gSDK->ValidClass(index))
		{
			index = gSDK->ClassNameToID(TXString("VwSdkProbe120"));
			if (!gSDK->ValidClass(index))
			{
				probe.fail("クラスを作れなかった（`AddClass` も `ClassNameToID` も無効な番号を"
						   "返した）。この節は測れない");
				return 0;
			}
		}

		SMarkerStyle mstyle = MakeStyle(style, sizeInInch, angle);
		const Boolean setBegin = gSDK->SetClassBeginningMarker(index, mstyle);
		const Boolean setEnd = gSDK->SetClassEndMarker(index, mstyle);
		SMarkerStyle readBack{};
		const Boolean getBegin = gSDK->GetClassBeginningMarker(index, readBack);

		if (announce)
		{
			TXString className;
			gSDK->ClassIDToName(index, className);
			probe.log("使うクラス: 番号 " + Num(static_cast<long>(index)) + "（`" +
					  std::string(static_cast<const char*>(className)) +
					  "`）。`GetClUseGraphic`（グラフィック属性を使用）の初期値 = " +
					  YesNo(gSDK->GetClUseGraphic(index) != 0));
			probe.log("");
			probe.log("クラスへ書いた結果: `SetClassBeginningMarker`=" + YesNo(setBegin != 0) +
					  " `SetClassEndMarker`=" + YesNo(setEnd != 0) +
					  " / 読み戻し `GetClassBeginningMarker`=" + YesNo(getBegin != 0) +
					  " style=" + Num(static_cast<long>(readBack.style)) + "（" +
					  RootName(static_cast<long>(readBack.style)) + "） dSize=" +
					  Real(readBack.dSize) + " nAngle=" + Num(static_cast<long>(readBack.nAngle)));
			MarkerType midStyle = 0;
			short midSize = 0;
			short midAngle = 0;
			gSDK->GetClMarker(index, midStyle, midSize, midAngle);
			probe.log("");
			probe.log("同じクラスを中口（`GetClMarker`）で読むと: style=" +
					  Num(static_cast<long>(midStyle)) +
					  " size=" + Num(static_cast<long>(midSize)) +
					  " angle=" + Num(static_cast<long>(midAngle)) +
					  "。**どちらの口にも有無（visibility）の引数は無い**"
					  "——これはヘッダで確かめてある（このファイル冒頭）。");
			probe.log("");
		}
		return index;
	}
} // namespace

VW_PROBE("class-marker-drawn", "クラスへ置いたマーカーが線の絵に出るかを確かめる",
		 "by-class で図形が生まれない件（#120）。物差しを校正してからクラス経由の道を総当たりする")
{
	probe.log("**問い: `SetClassBeginningMarker` ＋ `SetArrowByClass` で、マーカーは"
			  "本当に線の絵に出るのか。**");
	probe.log("");
	probe.log("#116（PR #117）の 2 周目で、**by-class の線には `GetMarkerPolys` が図形を"
			  "返さない**と測れた。素の線（図形なし）と per-object 直書き（図形あり・"
			  "境界 4.4901 × 4.4901）の**対照が両方効いていた**ので、この口が壊れている"
			  "わけではない。");
	probe.log("");
	probe.log("**実機へ持ってくる前に SDK で分かっていること**（ヘッダと同梱ソース）:");
	probe.log("");
	probe.log("1. **クラスのマーカーには有無（visibility）の口がどこにも無い。** "
			  "per-object の新口は `SetObjBeginningMarker(h, mstyle, visibility)` と"
			  "引数に持つのに、`SetClassBeginningMarker(index, mstyle)` も "
			  "`SetClMarker(index, style, size, angle)` も持たない"
			  "（中口 `SetMarker` にはある `start` / `end` すら無い）。"
			  "VectorScript 側も同じで、戻り値の `BOOLEAN` は成否である。");
	probe.log("2. **SDK 自身の参照実装は、クラスの値を per-object へ書き写している。** "
			  "`VWTaggedObj::CreateLeaderLine` は `GetClassEndMarker` で読んでから "
			  "`SetObjEndMarker(line, mstyle, useMarker)` で**有無つきで書き**、"
			  "その後 `GetClUseGraphic` が立っているときだけ `SetArrowByClass` を呼ぶ。");
	probe.log("");
	probe.log("**見込みは「`SetArrowByClass` だけでは描かれない」だが、ヘッダではそこまで"
			  "しか言えない**（VW 本体がクラス側に有無を隠し持っている余地が残る）。"
			  "だから測る。");
	probe.log("");

	// =====================================================================
	// 1. 物差しの校正
	// =====================================================================
	probe.log("## 1. 物差しの校正——`GetMarkerPolys` と線の境界は「描かれるか」を映すか");
	probe.log("");
	probe.log("**per-object で visibility=false の線が効く**（値はあるが消えている）。"
			  "そこに図形が出ないなら、`GetMarkerPolys` は「値の有無」ではなく"
			  "**実際に描かれるか**を映していることになり、2 節の「図形なし」は"
			  "**絵に出ていない**と読める。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log(kHeader);
		probe.log(kHeaderRule);

		struct Calib
		{
			const char* label;
			int kind; // 0=素の線 1=vis=true 2=vis=false 3=vis=true で大きく 4=矢印で vis=true
		};
		const Calib kCalib[] = {
			{"**対照**: 何も書いていない素の線", 0},
			{"per-object 直書き・visibility=**true**", 1},
			{"per-object 直書き・visibility=**false**（値はある）", 2},
			{"per-object 直書き・visibility=true・dSize=**2.0000**（上限）", 3},
			{"per-object 直書き・visibility=true・**矢印**（style=0）", 4},
		};
		const size_t kCalibCount = sizeof(kCalib) / sizeof(kCalib[0]);

		for (size_t i = 0; i < kCalibCount; ++i)
		{
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + std::string(kCalib[i].label) +
						  " | **引けなかった（nil）** | | | | |");
				continue;
			}
			switch (kCalib[i].kind)
			{
			case 1:
				WritePerObject(line, kProbeStyle, kProbeSize, kProbeAngle, true);
				break;
			case 2:
				WritePerObject(line, kProbeStyle, kProbeSize, kProbeAngle, false);
				break;
			case 3:
				WritePerObject(line, kProbeStyle, 2.0, kProbeAngle, true);
				break;
			case 4:
				WritePerObject(line, kArrowMarker, kProbeSize, kProbeAngle, true);
				break;
			default:
				break;
			}
			probe.log("| " + std::string(kCalib[i].label) + " | " + RowCells(line) + " |");
		}
		probe.log("");
		probe.log("**読み方**: 線はすべて (x, 0)–(x, 1000) の縦線なので、"
				  "**素の線の境界は 幅 0**である。per-object の行で幅が広がっていれば、"
				  "**線の境界もマーカーを映す物差しとして使える**。"
				  "visibility=false の行が素の線と同じなら、どちらの物差しも"
				  "**「描かれるか」を映している**と確定する。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 2. クラス経由の道を総当たり
	// =====================================================================
	probe.log("## 2. クラス経由の道を総当たり");
	probe.log("");
	probe.log("**クラスには 1 節と同じ値**（style=" + Num(kProbeStyle) +
			  " / dSize=" + Real(kProbeSize) + " / nAngle=" + Num(kProbeAngle) +
			  "）**を置く**。per-object 直書きの対照と見比べられるようにするためである。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);
		const InternalIndex classIndex =
			PrepareClass(probe, kProbeStyle, kProbeSize, kProbeAngle, true);

		probe.log(kHeader);
		probe.log(kHeaderRule);

		// 0=素の線 1=クラスだけ 2=+SetArrowByClass 3=+ResetObject
		// 4=UseGraphic を立ててから 5=クラスを中口で書く 6=順序を逆に
		// 7=VW の作法（書き写し＋by-class） 8=書き写しだけ 9=by-class の後に書き写す
		// 10=per-object 直書きの対照
		struct ClassCase
		{
			const char* label;
			int kind;
		};
		const ClassCase kCases[] = {
			{"**対照**: 何も書いていない素の線", 0},
			{"クラスに入れただけ", 1},
			{"クラスに入れて `SetArrowByClass`（**いまの案内**）", 2},
			{"同じもの＋`ResetObject`", 3},
			{"`SetClUseGraphic(true)` の後にクラス＋`SetArrowByClass`", 4},
			{"クラスの値を**中口**（`SetClMarker`）で書いて＋`SetArrowByClass`", 5},
			{"`SetArrowByClass` を**先**に、`SetObjectClass` を**後**に", 6},
			{"**VW の作法**: クラスから読んで per-object へ書き写す＋`SetArrowByClass`", 7},
			{"書き写すだけ（`SetArrowByClass` を呼ばない）", 8},
			{"`SetArrowByClass` の**後**に per-object へ書き写す", 9},
			{"**対照**: per-object で直に書いた（クラスとは無関係）", 10},
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
			const int kind = kCases[i].kind;

			if (kind == 4)
				gSDK->SetClUseGraphic(classIndex, static_cast<Boolean>(1));
			if (kind == 5)
			{
				// 中口はクラス側も `EMarkerType` の番号で受ける（#116 で確定）。
				// 5 = `kMarkerSlash` → `MarkerType` の 259 になる。
				gSDK->SetClMarker(classIndex, static_cast<MarkerType>(5), static_cast<short>(4096),
								  static_cast<short>(kProbeAngle));
			}

			if (kind == 6)
			{
				gSDK->SetArrowByClass(line);
				gSDK->SetObjectClass(line, classIndex);
			}
			else if (kind >= 1 && kind <= 9)
			{
				gSDK->SetObjectClass(line, classIndex);
			}

			if (kind == 2 || kind == 3 || kind == 4 || kind == 5)
				gSDK->SetArrowByClass(line);

			if (kind == 3)
				gSDK->ResetObject(line);

			if (kind == 7 || kind == 8 || kind == 9)
			{
				if (kind == 9)
					gSDK->SetArrowByClass(line);
				SMarkerStyle fromClass{};
				const Boolean gotBegin = gSDK->GetClassBeginningMarker(classIndex, fromClass);
				SMarkerStyle fromClassEnd{};
				const Boolean gotEnd = gSDK->GetClassEndMarker(classIndex, fromClassEnd);
				gSDK->SetObjBeginningMarker(line, fromClass, static_cast<Boolean>(1));
				gSDK->SetObjEndMarker(line, fromClassEnd, static_cast<Boolean>(1));
				if (gotBegin == 0 || gotEnd == 0)
					probe.log("※ `GetClassBeginningMarker`=" + YesNo(gotBegin != 0) +
							  " / `GetClassEndMarker`=" + YesNo(gotEnd != 0) +
							  " だった（書き写す値が読めていない）");
				if (kind == 7)
					gSDK->SetArrowByClass(line);
			}

			if (kind == 10)
				WritePerObject(line, kProbeStyle, kProbeSize, kProbeAngle, true);

			probe.log("| " + std::string(kCases[i].label) + " | " + RowCells(line) + " |");
		}
		probe.log("");
		probe.log("**読み方**（1 節で物差しが校正できている前提で）:");
		probe.log("");
		probe.log("- 「クラスに入れて `SetArrowByClass`」の行に**図形が出れば**、"
				  "いまの案内は正しい（per-object の口で読めないだけ）。");
		probe.log("- **出なければ、その案内はマーカーを描かない**。そのとき「VW の作法」の行に"
				  "図形が出ていれば、**それが唯一の道**である"
				  "（クラスの値を読んで per-object へ visibility=true で書き写す）。");
		probe.log("- 「`SetArrowByClass` の**後**に書き写す」の行は、**後から by-class 化"
				  "すると per-object の有無が消えるのか**を見る（VW の作法が"
				  "「書き写し → by-class」の順である理由の裏取り）。");
		probe.log("- **`GetArrowByClass` が yes なのに新口の読みが `ok=no`** なら、"
				  "**by-class の値は per-object の口からは読めない**（issue の 4 つ目）。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 3. クラスの値を後から変えたら追随するか
	// =====================================================================
	probe.log("## 3. クラスの値を後から変えたら、線は追随するか");
	probe.log("");
	probe.log("**by-class の値打ちはここにある**——クラスを直せば線が直るなら、"
			  "書き写しでは代わりにならない。クラスを "
			  "style=" +
			  Num(static_cast<long>(kNoFillDimCrossMarker)) + "（十字）・dSize=" + Real(1.0) +
			  " へ**後から**変えて、両方の線を測り直す。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);
		const InternalIndex classIndex =
			PrepareClass(probe, kProbeStyle, kProbeSize, kProbeAngle, false);

		MCObjectHandle byClassLine = CreateWitnessLine();
		if (byClassLine != nil)
		{
			gSDK->SetObjectClass(byClassLine, classIndex);
			gSDK->SetArrowByClass(byClassLine);
		}
		MCObjectHandle copiedLine = CreateWitnessLine();
		if (copiedLine != nil)
		{
			gSDK->SetObjectClass(copiedLine, classIndex);
			SMarkerStyle fromClass{};
			gSDK->GetClassBeginningMarker(classIndex, fromClass);
			gSDK->SetObjBeginningMarker(copiedLine, fromClass, static_cast<Boolean>(1));
			gSDK->SetObjEndMarker(copiedLine, fromClass, static_cast<Boolean>(1));
			gSDK->SetArrowByClass(copiedLine);
		}

		probe.log("**クラスを変える前**:");
		probe.log("");
		probe.log(kHeader);
		probe.log(kHeaderRule);
		if (byClassLine != nil)
			probe.log("| `SetArrowByClass` だけの線 | " + RowCells(byClassLine) + " |");
		if (copiedLine != nil)
			probe.log("| 書き写した線 | " + RowCells(copiedLine) + " |");
		probe.log("");

		SMarkerStyle changed = MakeStyle(kNoFillDimCrossMarker, 1.0, kProbeAngle);
		const Boolean rewroteBegin = gSDK->SetClassBeginningMarker(classIndex, changed);
		const Boolean rewroteEnd = gSDK->SetClassEndMarker(classIndex, changed);
		probe.log("クラスを書き換えた（`SetClassBeginningMarker`=" + YesNo(rewroteBegin != 0) +
				  " `SetClassEndMarker`=" + YesNo(rewroteEnd != 0) + "）。**変えた後**:");
		probe.log("");
		probe.log(kHeader);
		probe.log(kHeaderRule);
		if (byClassLine != nil)
			probe.log("| `SetArrowByClass` だけの線 | " + RowCells(byClassLine) + " |");
		if (copiedLine != nil)
			probe.log("| 書き写した線 | " + RowCells(copiedLine) + " |");
		probe.log("");
		probe.log("**読み方**: 書き写した線が変わらないのは当たり前（値は自分で持っている）。"
				  "見どころは `SetArrowByClass` だけの線で、**図形が生まれたなら"
				  "「クラスの値が線へ届く経路はある」**ことになる。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 4. 文書の既定から生まれた線
	// =====================================================================
	probe.log("## 4. 文書の既定から生まれた線はどうか");
	probe.log("");
	probe.log("既定のマーカーは**線が生まれる瞬間に読まれる**（#94）ので、"
			  "そちらは**per-object の値として**入っているはずである"
			  "——**クラス経由だけが特別なのか**の見当が付く。");
	probe.log("");
	probe.log("**`SetDefaultArrowByClass()` は呼ばない**"
			  "（[Findings「マーカーの既定 by-class は、意図して下ろせない」]"
			  "(../blob/main/Findings/Attributes%20and%20Classes.md) の通り、"
			  "立てると戻せない）。ここで触るのは**既定の値**だけで、"
			  "走らせる前の値へ書き戻す。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		SMarkerStyle savedBegin{};
		Boolean savedBeginVis = 0;
		const Boolean savedBeginOk = gSDK->GetDefaultBeginningMarker(savedBegin, savedBeginVis);
		SMarkerStyle savedEnd{};
		Boolean savedEndVis = 0;
		const Boolean savedEndOk = gSDK->GetDefaultEndMarker(savedEnd, savedEndVis);
		probe.log("走らせる前の既定: 始点 ok=" + YesNo(savedBeginOk != 0) + " vis=" +
				  YesNo(savedBeginVis != 0) + " style=" + Num(static_cast<long>(savedBegin.style)) +
				  " dSize=" + Real(savedBegin.dSize) + " / 終点 ok=" + YesNo(savedEndOk != 0) +
				  " vis=" + YesNo(savedEndVis != 0));
		probe.log("");

		SMarkerStyle defStyle = MakeStyle(kProbeStyle, kProbeSize, kProbeAngle);
		const Boolean setBegin = gSDK->SetDefaultBeginningMarker(defStyle, static_cast<Boolean>(1));
		const Boolean setEnd = gSDK->SetDefaultEndMarker(defStyle, static_cast<Boolean>(1));
		probe.log("既定へ書いた（`SetDefaultBeginningMarker`=" + YesNo(setBegin != 0) +
				  " `SetDefaultEndMarker`=" + YesNo(setEnd != 0) + "）。");
		probe.log("");

		probe.log(kHeader);
		probe.log(kHeaderRule);
		MCObjectHandle bornLine = CreateWitnessLine();
		if (bornLine != nil)
			probe.log("| 既定を立ててから生まれた線 | " + RowCells(bornLine) + " |");
		else
			probe.log("| 既定を立ててから生まれた線 | **引けなかった（nil）** | | | | |");

		// 既定を走らせる前へ戻し、その後に生まれた線も対照として測る。
		gSDK->SetDefaultBeginningMarker(savedBegin, savedBeginVis);
		gSDK->SetDefaultEndMarker(savedEnd, savedEndVis);
		MCObjectHandle afterRestore = CreateWitnessLine();
		if (afterRestore != nil)
			probe.log("| **対照**: 既定を戻した後に生まれた線 | " + RowCells(afterRestore) + " |");
		probe.log("");

		SMarkerStyle nowBegin{};
		Boolean nowBeginVis = 0;
		const Boolean nowOk = gSDK->GetDefaultBeginningMarker(nowBegin, nowBeginVis);
		probe.log(
			"既定を戻した後の読み: ok=" + YesNo(nowOk != 0) + " vis=" + YesNo(nowBeginVis != 0) +
			" style=" + Num(static_cast<long>(nowBegin.style)) + " dSize=" + Real(nowBegin.dSize) +
			"（走らせる前と同じなら、既定は元へ戻っている）");
		probe.log("");
		probe.log("**読み方**: 既定から生まれた線に図形が出て、新口の読みが `ok=yes vis=yes` "
				  "なら、**既定は per-object の値として入る**——つまり"
				  "**クラス経由だけが特別**だと分かる。");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 5. 目視用の対照図面（保険）
	// =====================================================================
	probe.log("## 5. 目視用の対照図面——1〜4 で割れなかったときの保険");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);
		const InternalIndex classIndex = PrepareClass(probe, kArrowMarker, 1.0, 0, false);

		// 見やすさを優先する: 横線・長さ 300mm・間隔 120mm・矢印（塗りあり）1 インチ。
		struct EyeCase
		{
			const char* label;
			int kind; // 0=素の線 1=クラス＋SetArrowByClass 2=VW の作法 3=per-object 直書き
		};
		const EyeCase kEyeCases[] = {
			{"1 素の線（対照・印は出ないはず）", 0},
			{"2 クラス＋SetArrowByClass", 1},
			{"3 クラスから書き写し＋SetArrowByClass", 2},
			{"4 per-object 直書き（対照・印は出るはず）", 3},
		};
		const size_t kEyeCount = sizeof(kEyeCases) / sizeof(kEyeCases[0]);

		for (size_t i = 0; i < kEyeCount; ++i)
		{
			const WorldCoord y = static_cast<WorldCoord>(kEyeCount - i) * 120;
			MCObjectHandle line = gSDK->CreateLine(WorldPt(0, y), WorldPt(300, y));
			if (line == nil)
			{
				probe.log("※ " + std::string(kEyeCases[i].label) + " の線を引けなかった");
				continue;
			}
			switch (kEyeCases[i].kind)
			{
			case 1:
				gSDK->SetObjectClass(line, classIndex);
				gSDK->SetArrowByClass(line);
				break;
			case 2:
			{
				gSDK->SetObjectClass(line, classIndex);
				SMarkerStyle fromClass{};
				gSDK->GetClassBeginningMarker(classIndex, fromClass);
				gSDK->SetObjBeginningMarker(line, fromClass, static_cast<Boolean>(1));
				gSDK->SetObjEndMarker(line, fromClass, static_cast<Boolean>(1));
				gSDK->SetArrowByClass(line);
				break;
			}
			case 3:
				WritePerObject(line, kArrowMarker, 1.0, 0, true);
				break;
			default:
				break;
			}
			if (gSDK->CreateTextBlock(TXString(kEyeCases[i].label), WorldPt(320, y),
									  static_cast<Boolean>(0), static_cast<WorldCoord>(0)) == nil)
			{
				probe.log("※ 見出しの文字を置けなかった（" + std::string(kEyeCases[i].label) +
						  "）。上から順に " + Num(static_cast<long>(i + 1)) +
						  " 番目の線がこれである");
			}
		}

		if (fresh)
		{
			probe.log("**この図面は開いたまま残してある**（他の節の図面はすべて閉じた）。"
					  "上から順に、素の線 / クラス＋`SetArrowByClass` / 書き写し＋"
					  "`SetArrowByClass` / per-object 直書きの 4 本で、マーカーは"
					  "**塗りのある矢印・1 インチ**（見落としようのない大きさ）である。");
			probe.log("");
			probe.log("**1〜4 節で割れていれば、この図面は見なくてよい**（保存せず閉じて"
					  "構わない）。割れていなかったときだけ、**どの線に矢印が付いているか**を"
					  "チャットで教えてください——それが最後の物差しになる。");
		}
		else
		{
			probe.log("※ 新しい図面を開けなかったので、**この 4 本は利用者の図面へ描いて"
					  "しまった**。取り消して構わない。");
		}
		probe.log("");
	}

	probe.log("## 6. 締め");
	probe.log("");
	probe.log("開いている図面の件数: " + Num(static_cast<long>(CountOpenDocuments())) +
			  "（**走らせる前より 1 多いのが正しい**——5 節の図面だけを残している）");
	probe.log("");
	probe.log("**1〜4 節は利用者の図面を触っていない**。クラスを作ったのも文書の既定を"
			  "書き換えたのも、プローブが自分で開いて自分で閉じた空の図面の中である。");
}
