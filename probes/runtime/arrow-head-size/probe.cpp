//
//	probes/runtime/arrow-head-size/probe.cpp
//
//	[issue #108] **per-object のマーカーの `size` に書いた値が、そのまま読み戻せない。**
//	`SetArrowHeadsN(h, …, 3.0000)` を書いたのに、旧口は `2.0000`、新口の `dSize` は
//	`1.8000` を返した（PR #107 の 4 節）。**書けていない**のか、**単位が違う**のか、
//	**上限で頭打ち（飽和）している**のか、**読みだけが嘘**なのかを切り分ける。
//
//	## 出どころ（#106 / PR #107 の実測ログ・4 節）
//
//	  | どこへ書いたか | 書いた `size` | 旧口の読み | 新口の `dSize` |
//	  | --- | --- | --- | --- |
//	  | 文書の既定 | 0.0945 | 0.0945（一致） | 0.0945（一致） |
//	  | per-object（線） | 3.0000 | **2.0000** | **1.8000** |
//
//	**旧口と新口で読みが割れている**のが手掛かりで、「単に書けていない」では説明が付か
//	ない。なお #107 は per-object で **3.0000 の 1 点しか**書いていない（`born.size` が
//	0 だったので `otherSize` が定数 3 になった）。**1 点では「飽和」と「係数」を区別
//	できない**——この調査の中心は、そこを**掃引**して形を出すことである。
//
//	## 先に `sdk-grep` で潰したこと（実機へ持っていく前に分かったもの）
//
//	1. **`…ArrowHeadsN` の `size` はインチである。** VWFC の実装が明示している
//	   （`SDKLib/Source/VWSDK/VWFC/VWObjects/VWObjectAttributes.cpp:568-605`）:
//
//	       // getter
//	       gSDK->GetArrowHeadsN( fhObject, innerStart, innerEnd, style, sizeInInch );
//	       sizeInMilimeters = sizeInInch * 25.4;
//	       // setter
//	       double_gs sizeInInch = sizeInMilimeters / 25.4;
//	       gSDK->SetArrowHeadsN( fhObject, starting, ending, type, sizeInInch );
//
//	   **だから「per-object だけポイント⇄ミリの換算が挟まっている」という issue の筋は、
//	   SDK 側の換算としては存在しない**——VWFC は per-object と文書の既定で**同じ**換算を
//	   掛けており（`fhObject` の有無で呼び先が変わるだけ）、素の `gSDK->…ArrowHeadsN` は
//	   どちらもインチをそのまま渡す。換算が挟まっているとすれば **VW の中**である。
//	   ついでに `N` の付かない旧口（`short size`）は**ポイント**（`sizeInPoints`）なので、
//	   **同じ大きさを 2 つの単位で読める**——3 節でその一致を突き合わせる。
//	2. **`SMarkerStyle` は 6 つの欄を持つ**（`Kernel/API/MiniCadCallBacks.h:839`）:
//	   `style` / `nAngle` / `dSize` / `dWidth` / `nThicknessBasis` / `dThickness`。
//	   ヘッダのコメントは `dSize` に `/* Length */`、`dWidth` に `/* Width */` と書く。
//	   **#107 は前の 4 つしか読んでいない**ので、この調査では 6 つ全部を読む。
//	3. `nThicknessBasis` は**太さの基準と単位**を畳んだ欄で、単位に
//	   `kMarkerThicknessUnitsMils=0` / `…Points=16` / `…Millimeters=32` を持つ。
//	   **つまり「マーカーの寸法の単位」という概念が SDK に実在する**——`dSize` にも
//	   同じ話が効いていないかを、5 節で 6 欄まとめて見る。
//
//	## この調査の設計
//
//	- **1 節: 掃引する。** 0.01 から 12 まで 16 点を、**1 点につき新しい線 1 本**へ
//	  書いて読み戻す。**書いた値との比を同じ表に出す**ので、
//	  「どこまでは一致し、どこから頭打ちになるか」が 1 目で読める。
//	    * 比が全区間で一定 → **単位（係数）**
//	    * 小さい値では 1.0 で、ある点から比が落ちる → **上限（飽和）**。折れ点が上限
//	    * どちらでもない → 量子化・丸めを疑う（4 桁で出しているので刻みが見える）
//	- **2 節: 同じ掃引を文書の既定でも回す。** #107 は既定側を 0.0945 の 1 点でしか
//	  見ていない。**既定も 2.0 で頭打ちになるなら「per-object だけの穴」ではない**。
//	  あわせて**書いた直後に線を引いて**、生まれたオブジェクトが何を持つかも読む
//	  （既定はオブジェクトが生まれる瞬間に読まれる——#94）。**既定が 3.0 を保ったまま、
//	  そこから生まれた線が 2.0 を返すなら、頭打ちはオブジェクト側**だと分かる。
//	- **3 節: ポイントの口と突き合わせる。** `SetArrowHeads(h, …, short sizeInPoints)` /
//	  `GetArrowHeads(h, …, short&)` は同じ大きさをポイントで読み書きする。
//	  N 口で書いた線をポイントで読めば（またはその逆）、**インチという前提そのもの**を
//	  実機で確かめられる（1.0 インチ = 72 ポイント）。**上限がポイント側でいくつに
//	  見えるか**も、飽和なら同じ物理量を指すはずである。
//	- **4 節: 新口で書いてみる。** `SetObjBeginningMarker(h, SMarkerStyle, visibility)` で
//	  同じ掃引を書く。**新口なら通るなら、それが「大きさを指定する道」になる**。
//	- **5 節: 6 欄を全部読む。** `dWidth` が全区間 0.0000 だった件を含め、
//	  `nThicknessBasis` / `dThickness` まで出す。`dWidth` を書いたら往復するかも見る。
//	- **6 節: `size=0.0000` は「未設定」か「本当に 0」か。** 引いた直後の線は旧口で
//	  0.0000 を返し、新口は **ret=no**（#107 の 4 節）。明示的に 0 を書いた線と、
//	  新口で有無だけを立てた線を並べれば、**ret が「持っているか」の印**なのかが分かる。
//	- **0 節: 図面の素性（単位・縮尺）を控える。** 「既定側が一致したのは、たまたま単位が
//	  同じだから」という筋を潰すため。
//	- **8 節: 縮尺を変えて同じ値を書く。** マーカーの大きさが**紙の上の寸法**か
//	  **図面の寸法**かで、上限の意味が変わる。
//	- **7 節: 様式で変わるか。** 根種別（矢印・丸・スラッシュ）を変えて同じ 2 点を書く。
//	  上限が様式ごとなら、ここで割れる。
//
//	**利用者の図面は触らない**——測るのはすべて、このプローブが自分で開いて自分で閉じる
//	空の図面の中である（`OpenDocumentPath(nullptr, false)` / `CloseDocument()`。
//	[Findings「図面（ドキュメント）を開く・作る」](Findings/Documents.md)）。
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

	// 書いた値に対する比。**この欄がこの調査の主役**——全区間で一定なら係数（単位）、
	// 途中から落ちるなら上限（飽和）である。書いた値が 0 のときは比を出さない。
	std::string Ratio(double written, double readBack)
	{
		if (written == 0)
			return "—";
		return Real(readBack / written);
	}

	// --- 旧口（`…ArrowHeadsN`。size はインチ） ----------------------------

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

	std::string Describe(const LegacyArrow& value)
	{
		return std::string("start=") + YesNo(value.starting) + " end=" + YesNo(value.ending) +
			   " style=" + Num(static_cast<long>(value.style)) +
			   " size=" + Real(static_cast<double>(value.size));
	}

	// --- 新口（`SMarkerStyle` ＋ `visibility`） ---------------------------
	//
	// **6 欄すべてを読む**（#107 は前の 4 つしか読んでいない）。

	struct MarkerEnd
	{
		Boolean ok = 0;			// 呼び出しの戻り値（参考。判断には使わない）
		Boolean visibility = 0; // マーカーが付いているか
		SMarkerStyle style{};
	};

	std::string DescribeSize(const MarkerEnd& end)
	{
		return Real(end.style.dSize);
	}

	// 6 欄すべて。5 節で使う。
	std::string DescribeFull(const MarkerEnd& end)
	{
		return std::string("visible=") + YesNo(end.visibility) +
			   " style=" + Num(static_cast<long>(end.style.style)) +
			   " nAngle=" + Num(static_cast<long>(end.style.nAngle)) +
			   " dSize=" + Real(end.style.dSize) + " dWidth=" + Real(end.style.dWidth) +
			   " nThicknessBasis=" + Num(static_cast<long>(end.style.nThicknessBasis)) +
			   " dThickness=" + Real(end.style.dThickness) + " (ret=" + YesNo(end.ok) + ")";
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

	MarkerEnd ReadModernDefaultBeginning()
	{
		MarkerEnd end;
		end.ok = gSDK->GetDefaultBeginningMarker(end.style, end.visibility);
		return end;
	}

	// --- 証人の線 --------------------------------------------------------

	int gSerial = 0;

	MCObjectHandle CreateWitnessLine()
	{
		const WorldCoord x = static_cast<WorldCoord>(gSerial++) * 1000;
		return gSDK->CreateLine(WorldPt(x, 0), WorldPt(x, 1000));
	}

	// --- 図面を開く・閉じる（p107 と同じ作法） ---------------------------

	size_t CountOpenDocuments()
	{
		// `MockUp::` が要る（`TVWArray_OpenFileInformation` はその名前空間の中）。
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
	//
	// **0.01 から 12 まで 16 点。** 2.0 の前後（1.75 / 1.8 / 1.9 / 2.0 / 2.2）を
	// 細かく刻んであるのは、#107 が返した 2.0000 / 1.8000 が**折れ点**なのかを
	// その場で見分けるため。0.0472（1.2mm）と 0.1250（1/8 インチ）は、
	// #107 で実際に読めた既定の値である。

	const double kSweep[] = {0.0100, 0.0472, 0.1250, 0.2500, 0.5000, 0.7500, 1.0000, 1.2500,
							 1.5000, 1.7500, 1.8000, 1.9000, 2.0000, 2.2000, 3.0000, 12.0000};
	const size_t kSweepCount = sizeof(kSweep) / sizeof(kSweep[0]);

	// 根種別。`kArrowMarker` は 0（＝「マーカー無し」ではない。#107 で確定）。
	const ArrowType kStyleArrow = static_cast<ArrowType>(kArrowMarker);
} // namespace

VW_PROBE("arrow-head-size", "per-object のマーカーの大きさが往復しない理由を突き止める",
		 "書いた size を掃引して、飽和か単位か丸めかを分ける（旧口・新口・ポイント口）")
{
	probe.log("**per-object の `SetArrowHeadsN(h, …, 3.0000)` が `2.0000`（旧口） / "
			  "`1.8000`（新口 `dSize`）で返ってくる**のはなぜかを切り分ける。#107 は "
			  "per-object で **3.0000 の 1 点しか**書いていないので、**掃引して形を出す**"
			  "のがこの調査の中心である。");
	probe.log("");
	probe.log("**先に `sdk-grep` で分かっていること**——`…ArrowHeadsN` の `size` は"
			  "**インチ**である（VWFC が `sizeInMilimeters = sizeInInch * 25.4` と換算して"
			  "いる。`VWObjectAttributes.cpp:568-605`）。VWFC は per-object と文書の既定に"
			  "**同じ**換算を掛けるので、**「per-object だけポイント⇄ミリの換算が挟まって"
			  "いる」という筋は、少なくとも SDK 側には無い**。");
	probe.log("");
	probe.log("**利用者の図面は触らない**——測るのは、このプローブが自分で開いて自分で"
			  "閉じる空の図面の中だけである。");
	probe.log("");

	// =====================================================================
	// 0. 図面の素性（単位・縮尺）
	// =====================================================================
	probe.log("## 0. 走らせている図面の素性（単位・縮尺）");
	probe.log("");
	probe.log("**「既定側が一致したのは、たまたま単位が同じだからでは」という筋を潰す**ため、"
			  "測る前に図面の素性を控える。`UnitsType` は **`unitsPerInch`（1 インチあたりの"
			  "表示単位）と `unitMark`（記号）を持つ構造体**（`MiniCadCallBacks.h:1260`）"
			  "なので、単位の筋はここの数値と突き合わせて潰せる。`CoordLengthToUnitsLengthN` は"
			  "**内部座標 1 に相当する表示単位の長さ**で、これが単位系の実効的な物差しになる。");
	probe.log("");

	{
		UnitsType units{};
		gSDK->GetUnits(units);
		MCObjectHandle activeLayer = gSDK->GetActiveLayer();
		double_gs scale = 0;
		if (activeLayer != nil)
			gSDK->GetLayerScaleN(activeLayer, scale);

		probe.log("| 何 | 値 |");
		probe.log("| --- | --- |");
		// `UnitsType` は構造体（`MiniCadCallBacks.h:1260`）。**`unitsPerInch` を持つ**
		// ので、「単位が違うから合わない」筋はここの数値と突き合わせて潰せる。
		probe.log("| `GetUnits`→`unitsPerInch`（1 インチあたりの表示単位） | " +
				  Real(static_cast<double>(units.unitsPerInch)) + " |");
		probe.log("| `GetUnits`→`unitMark`（単位の記号） | " +
				  std::string(static_cast<const char*>(units.unitMark)) + " |");
		probe.log("| `GetUnits`→`storedAccuracy`（内部座標/単位） | " +
				  Num(static_cast<long>(units.storedAccuracy)) + " |");
		probe.log("| `GetUnits`→`format`（表示書式） | " + Num(static_cast<long>(units.format)) +
				  " |");
		probe.log("| アクティブレイヤの縮尺 `GetLayerScaleN` | " +
				  (activeLayer != nil ? Real(static_cast<double>(scale))
									  : std::string("（レイヤを取れなかった）")) +
				  " |");
		probe.log(
			"| `CoordLengthToUnitsLengthN(1)`（内部座標 1 の表示単位長） | " +
			Real(static_cast<double>(gSDK->CoordLengthToUnitsLengthN(static_cast<WorldCoord>(1)))) +
			" |");
		probe.log("| `CoordLengthToUnitsLengthN(1000)` | " +
				  Real(static_cast<double>(
					  gSDK->CoordLengthToUnitsLengthN(static_cast<WorldCoord>(1000)))) +
				  " |");
		probe.log("| 文書の既定・旧口（読むだけ） | " + Describe(ReadLegacyDefault()) + " |");
		probe.log("");
	}

	// =====================================================================
	// 1. per-object を掃引する（この調査の本丸）
	// =====================================================================
	probe.log("## 1. per-object を掃引する（`SetArrowHeadsN(h, …, size)`）");
	probe.log("");
	probe.log("**1 点につき新しい線を 1 本引いて**書き、旧口と新口で読み戻す（前の点の"
			  "残りを引きずらないため）。**比の欄が主役**——全区間で一定なら単位（係数）、"
			  "途中から落ちるなら上限（飽和）、どちらでもないなら丸め・量子化である。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 書いた size（インチ） | 旧口 `GetArrowHeadsN` | 旧口/書いた | "
				  "新口・始点 `dSize` | 新口/書いた | 新口・終点 `dSize` |");
		probe.log("| --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kSweepCount; ++i)
		{
			const double wanted = kSweep[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Real(wanted) + " | **引けなかった（nil）** | | | | |");
				continue;
			}
			gSDK->SetArrowHeadsN(line, static_cast<Boolean>(1), static_cast<Boolean>(1),
								 kStyleArrow, wanted);
			const LegacyArrow legacy = ReadLegacyObject(line);
			const MarkerEnd beginning = ReadModernObjectBeginning(line);
			const MarkerEnd ending = ReadModernObjectEnd(line);
			probe.log("| " + Real(wanted) + " | " + Real(static_cast<double>(legacy.size)) + " | " +
					  Ratio(wanted, static_cast<double>(legacy.size)) + " | " +
					  DescribeSize(beginning) + " | " + Ratio(wanted, beginning.style.dSize) +
					  " | " + DescribeSize(ending) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 2. 同じ掃引を文書の既定でも回す
	// =====================================================================
	probe.log("## 2. 文書の既定でも同じ掃引を回す（`SetDefaultArrowHeadsN`）");
	probe.log("");
	probe.log("#107 は既定側を **0.0945 の 1 点**でしか見ていない。**既定も同じところで"
			  "頭打ちになるなら「per-object だけの穴」ではない**。あわせて**書いた直後に"
			  "線を 1 本引いて**、そこから生まれたオブジェクトが何を持つかも読む"
			  "（既定はオブジェクトが生まれる瞬間に読まれる——#94）。"
			  "**既定が書いたとおりのまま、生まれた線だけが落ちるなら、頭打ちは"
			  "「オブジェクトに入れるとき」に掛かっている**。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 書いた size | 既定・旧口 | 既定/書いた | 既定・新口 `dSize` | "
				  "生まれた線・旧口 | 生まれた線/書いた | 生まれた線・新口 `dSize` |");
		probe.log("| --- | --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kSweepCount; ++i)
		{
			const double wanted = kSweep[i];
			gSDK->SetDefaultArrowHeadsN(static_cast<Boolean>(1), static_cast<Boolean>(1),
										kStyleArrow, wanted);
			const LegacyArrow legacy = ReadLegacyDefault();
			const MarkerEnd beginning = ReadModernDefaultBeginning();

			MCObjectHandle line = CreateWitnessLine();
			std::string bornLegacy = "**引けなかった（nil）**";
			std::string bornRatio = "—";
			std::string bornModern = "—";
			if (line != nil)
			{
				const LegacyArrow born = ReadLegacyObject(line);
				bornLegacy = Real(static_cast<double>(born.size));
				bornRatio = Ratio(wanted, static_cast<double>(born.size));
				bornModern = DescribeSize(ReadModernObjectBeginning(line));
			}
			probe.log("| " + Real(wanted) + " | " + Real(static_cast<double>(legacy.size)) + " | " +
					  Ratio(wanted, static_cast<double>(legacy.size)) + " | " +
					  DescribeSize(beginning) + " | " + bornLegacy + " | " + bornRatio + " | " +
					  bornModern + " |");
		}
		probe.log("");
		probe.log("既定の by-class の旗（この調査では一度も立てない）: " +
				  std::string(YesNo(gSDK->GetDefaultArrowByClass())));
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 3. ポイントの口と突き合わせる（単位の確定）
	// =====================================================================
	probe.log("## 3. ポイントの口と突き合わせる（`SetArrowHeads(h, …, short sizeInPoints)`）");
	probe.log("");
	probe.log("`N` の付かない旧口は**ポイント**で読み書きする（VWFC の引数名 "
			  "`sizeInPoints`）。**1 インチ = 72 ポイント**なので、同じ線を 2 つの口で"
			  "読めば「`…N` の単位はインチ」を実機で確かめられる。**飽和しているなら、"
			  "ポイント側でも同じ物理量で頭打ちになる**はずである。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("**① `…N`（インチ）で書いて、両方の口で読む**");
		probe.log("");
		probe.log("| 書いた size（インチ） | 旧口 `…N`（インチ） | ポイント口 `short` | "
				  "ポイント÷72（インチ換算） |");
		probe.log("| --- | --- | --- | --- |");
		const double kCross[] = {0.1250, 0.5000, 1.0000, 2.0000, 3.0000};
		for (size_t i = 0; i < sizeof(kCross) / sizeof(kCross[0]); ++i)
		{
			const double wanted = kCross[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Real(wanted) + " | **引けなかった（nil）** | | |");
				continue;
			}
			gSDK->SetArrowHeadsN(line, static_cast<Boolean>(1), static_cast<Boolean>(1),
								 kStyleArrow, wanted);
			const LegacyArrow legacy = ReadLegacyObject(line);
			Boolean pointStart = 0, pointEnd = 0;
			ArrowType pointStyle = 0;
			short pointSize = 0;
			gSDK->GetArrowHeads(line, pointStart, pointEnd, pointStyle, pointSize);
			probe.log("| " + Real(wanted) + " | " + Real(static_cast<double>(legacy.size)) + " | " +
					  Num(static_cast<long>(pointSize)) + " | " +
					  Real(static_cast<double>(pointSize) / 72.0) + " |");
		}
		probe.log("");

		probe.log("**② ポイント口で書いて、両方の口で読む**");
		probe.log("");
		probe.log("| 書いた size（ポイント） | 書いた値÷72（インチ換算） | "
				  "ポイント口 `short` | 旧口 `…N`（インチ） |");
		probe.log("| --- | --- | --- | --- |");
		const short kCrossPoints[] = {9, 36, 72, 144, 216};
		for (size_t i = 0; i < sizeof(kCrossPoints) / sizeof(kCrossPoints[0]); ++i)
		{
			const short wanted = kCrossPoints[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Num(static_cast<long>(wanted)) +
						  " | **引けなかった（nil）** | | |");
				continue;
			}
			gSDK->SetArrowHeads(line, static_cast<Boolean>(1), static_cast<Boolean>(1), kStyleArrow,
								wanted);
			Boolean pointStart = 0, pointEnd = 0;
			ArrowType pointStyle = 0;
			short pointSize = 0;
			gSDK->GetArrowHeads(line, pointStart, pointEnd, pointStyle, pointSize);
			const LegacyArrow legacy = ReadLegacyObject(line);
			probe.log("| " + Num(static_cast<long>(wanted)) + " | " +
					  Real(static_cast<double>(wanted) / 72.0) + " | " +
					  Num(static_cast<long>(pointSize)) + " | " +
					  Real(static_cast<double>(legacy.size)) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 4. 新口で書いたら往復するか
	// =====================================================================
	probe.log("## 4. 新口で書いたら往復するか（`SetObjBeginningMarker(h, SMarkerStyle, …)`）");
	probe.log("");
	probe.log("旧口で往復しないなら、**新口が「大きさを指定する道」になるか**が実務上の"
			  "答えになる。同じ掃引を新口で書いて、両方の口で読み戻す。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 書いた `dSize` | 新口の戻り値 | 新口・始点 `dSize` | 新口/書いた | "
				  "旧口 `…N` | 旧口/書いた |");
		probe.log("| --- | --- | --- | --- | --- | --- |");
		for (size_t i = 0; i < kSweepCount; ++i)
		{
			const double wanted = kSweep[i];
			MCObjectHandle line = CreateWitnessLine();
			if (line == nil)
			{
				probe.log("| " + Real(wanted) + " | **引けなかった（nil）** | | | | |");
				continue;
			}
			// 読んでから欲しい欄だけ差し替える（他の欄を勝手な値で上書きしないため）。
			MarkerEnd seed = ReadModernObjectBeginning(line);
			seed.style.style = static_cast<MarkerType>(kArrowMarker);
			seed.style.dSize = wanted;
			const Boolean ret =
				gSDK->SetObjBeginningMarker(line, seed.style, static_cast<Boolean>(1));
			const MarkerEnd beginning = ReadModernObjectBeginning(line);
			const LegacyArrow legacy = ReadLegacyObject(line);
			probe.log("| " + Real(wanted) + " | " + std::string(YesNo(ret)) + " | " +
					  DescribeSize(beginning) + " | " + Ratio(wanted, beginning.style.dSize) +
					  " | " + Real(static_cast<double>(legacy.size)) + " | " +
					  Ratio(wanted, static_cast<double>(legacy.size)) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 5. `SMarkerStyle` の 6 欄を全部読む
	// =====================================================================
	probe.log("## 5. `SMarkerStyle` の 6 欄を全部読む（`dWidth` / `nThicknessBasis` / "
			  "`dThickness`）");
	probe.log("");
	probe.log("#107 は前の 4 欄しか読んでいない。`dWidth` が全区間 `0.0000` だった件も"
			  "ここで見る（**書いたら往復するのか、そもそも読めない欄なのか**）。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 何をした線 | 新口・始点の 6 欄 |");
		probe.log("| --- | --- |");

		MCObjectHandle fresh1 = CreateWitnessLine();
		if (fresh1 != nil)
			probe.log("| 引いた直後（何も書いていない） | " +
					  DescribeFull(ReadModernObjectBeginning(fresh1)) + " |");

		MCObjectHandle byLegacy = CreateWitnessLine();
		if (byLegacy != nil)
		{
			gSDK->SetArrowHeadsN(byLegacy, static_cast<Boolean>(1), static_cast<Boolean>(1),
								 kStyleArrow, 0.5);
			probe.log("| 旧口で `size=0.5000` を書いた | " +
					  DescribeFull(ReadModernObjectBeginning(byLegacy)) + " |");
		}

		MCObjectHandle byModern = CreateWitnessLine();
		if (byModern != nil)
		{
			MarkerEnd seed = ReadModernObjectBeginning(byModern);
			seed.style.style = static_cast<MarkerType>(kArrowMarker);
			seed.style.dSize = 0.5;
			seed.style.dWidth = 0.2500;
			gSDK->SetObjBeginningMarker(byModern, seed.style, static_cast<Boolean>(1));
			probe.log("| 新口で `dSize=0.5000` `dWidth=0.2500` を書いた | " +
					  DescribeFull(ReadModernObjectBeginning(byModern)) + " |");
		}

		probe.log("| 文書の既定（この図面は触っていない） | " +
				  DescribeFull(ReadModernDefaultBeginning()) + " |");
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 6. `size=0.0000` は「未設定」か「本当に 0」か
	// =====================================================================
	probe.log("## 6. `size=0.0000` は「未設定」か「本当に 0」か");
	probe.log("");
	probe.log("引いた直後の線は旧口で `size=0.0000` を返し、新口は **ret=no** だった"
			  "（#107 の 4 節）。**明示的に 0 を書いた線**と、**新口で有無だけを立てた線**を"
			  "並べれば、`ret` が「この線がマーカーを持っているかの印」なのかが分かる。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 線 | 旧口 `…N` | 新口・始点（6 欄） |");
		probe.log("| --- | --- | --- |");

		MCObjectHandle untouched = CreateWitnessLine();
		if (untouched != nil)
			probe.log("| ① 引いた直後（何も書いていない） | " +
					  Describe(ReadLegacyObject(untouched)) + " | " +
					  DescribeFull(ReadModernObjectBeginning(untouched)) + " |");

		MCObjectHandle wroteZero = CreateWitnessLine();
		if (wroteZero != nil)
		{
			gSDK->SetArrowHeadsN(wroteZero, static_cast<Boolean>(1), static_cast<Boolean>(1),
								 kStyleArrow, 0.0);
			probe.log("| ② 旧口で `size=0.0000` を明示的に書いた | " +
					  Describe(ReadLegacyObject(wroteZero)) + " | " +
					  DescribeFull(ReadModernObjectBeginning(wroteZero)) + " |");
		}

		MCObjectHandle visibleOnly = CreateWitnessLine();
		if (visibleOnly != nil)
		{
			MarkerEnd seed = ReadModernObjectBeginning(visibleOnly);
			gSDK->SetObjBeginningMarker(visibleOnly, seed.style, static_cast<Boolean>(1));
			probe.log("| ③ 新口で有無だけを立てた（`dSize` は読んだ値のまま） | " +
					  Describe(ReadLegacyObject(visibleOnly)) + " | " +
					  DescribeFull(ReadModernObjectBeginning(visibleOnly)) + " |");
		}

		MCObjectHandle legacyThenRead = CreateWitnessLine();
		if (legacyThenRead != nil)
		{
			gSDK->SetArrowHeadsN(legacyThenRead, static_cast<Boolean>(1), static_cast<Boolean>(1),
								 kStyleArrow, 0.1250);
			probe.log("| ④ 旧口で `size=0.1250` を書いた（比較用） | " +
					  Describe(ReadLegacyObject(legacyThenRead)) + " | " +
					  DescribeFull(ReadModernObjectBeginning(legacyThenRead)) + " |");
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 7. 様式（根種別）で変わるか
	// =====================================================================
	probe.log("## 7. 様式（根種別）で変わるか");
	probe.log("");
	probe.log("上限や係数が**様式ごと**なら、ここで割れる。根種別は "
			  "`kArrowMarker=0` / `kCircleMarker=2` / `kDimSlashMarker=3`、"
			  "それに複合定数 `kOpenBaseNoFillMarker=1280` を使う。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 様式 | 書いた size | 旧口 `…N` | 新口・始点 `dSize` | 読めた style |");
		probe.log("| --- | --- | --- | --- | --- |");
		const ArrowType kStyles[] = {
			static_cast<ArrowType>(kArrowMarker), static_cast<ArrowType>(kCircleMarker),
			static_cast<ArrowType>(kDimSlashMarker), static_cast<ArrowType>(kOpenBaseNoFillMarker)};
		const double kTwoPoints[] = {0.5000, 3.0000};
		for (size_t s = 0; s < sizeof(kStyles) / sizeof(kStyles[0]); ++s)
		{
			for (size_t v = 0; v < sizeof(kTwoPoints) / sizeof(kTwoPoints[0]); ++v)
			{
				const double wanted = kTwoPoints[v];
				MCObjectHandle line = CreateWitnessLine();
				if (line == nil)
				{
					probe.log("| " + Num(static_cast<long>(kStyles[s])) + " | " + Real(wanted) +
							  " | **引けなかった（nil）** | | |");
					continue;
				}
				gSDK->SetArrowHeadsN(line, static_cast<Boolean>(1), static_cast<Boolean>(1),
									 kStyles[s], wanted);
				const LegacyArrow legacy = ReadLegacyObject(line);
				const MarkerEnd beginning = ReadModernObjectBeginning(line);
				probe.log("| " + Num(static_cast<long>(kStyles[s])) + " | " + Real(wanted) + " | " +
						  Real(static_cast<double>(legacy.size)) + " | " + DescribeSize(beginning) +
						  " | " + Num(static_cast<long>(beginning.style.style)) + " |");
			}
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 8. レイヤの縮尺で変わるか
	// =====================================================================
	probe.log("## 8. レイヤの縮尺で変わるか（`SetLayerScaleN`）");
	probe.log("");
	probe.log("マーカーの大きさは**紙の上の寸法**（縮尺に依らない）なのか、"
			  "**図面の寸法**（縮尺で変わる）なのか。同じ値を、縮尺 1:1 のレイヤと"
			  "1:50 のレイヤで書いて読み比べる。**読めた値が縮尺で動くなら、"
			  "1 節の頭打ちは「縮尺込みの上限」だということになる**。");
	probe.log("");

	{
		size_t countBefore = 0;
		const bool fresh = OpenFreshDocument(probe, countBefore);

		probe.log("| 縮尺 | 書いた size | 旧口 `…N` | 旧口/書いた | 新口・始点 `dSize` |");
		probe.log("| --- | --- | --- | --- | --- |");
		const double kScales[] = {1.0, 50.0};
		const double kScalePoints[] = {0.5000, 3.0000};
		for (size_t sc = 0; sc < sizeof(kScales) / sizeof(kScales[0]); ++sc)
		{
			MCObjectHandle activeLayer = gSDK->GetActiveLayer();
			if (activeLayer == nil)
			{
				probe.log("| " + Real(kScales[sc]) +
						  " | — | **アクティブレイヤを取れなかった** | | |");
				continue;
			}
			gSDK->SetLayerScaleN(activeLayer, kScales[sc]);
			double_gs readBackScale = 0;
			gSDK->GetLayerScaleN(activeLayer, readBackScale);

			for (size_t v = 0; v < sizeof(kScalePoints) / sizeof(kScalePoints[0]); ++v)
			{
				const double wanted = kScalePoints[v];
				MCObjectHandle line = CreateWitnessLine();
				if (line == nil)
				{
					probe.log("| " + Real(static_cast<double>(readBackScale)) + " | " +
							  Real(wanted) + " | **引けなかった（nil）** | | |");
					continue;
				}
				gSDK->SetArrowHeadsN(line, static_cast<Boolean>(1), static_cast<Boolean>(1),
									 kStyleArrow, wanted);
				const LegacyArrow legacy = ReadLegacyObject(line);
				probe.log("| " + Real(static_cast<double>(readBackScale)) +
						  "（書いた値: " + Real(kScales[sc]) + "） | " + Real(wanted) + " | " +
						  Real(static_cast<double>(legacy.size)) + " | " +
						  Ratio(wanted, static_cast<double>(legacy.size)) + " | " +
						  DescribeSize(ReadModernObjectBeginning(line)) + " |");
			}
		}
		probe.log("");
		CloseFreshDocument(probe, countBefore, fresh);
	}

	// =====================================================================
	// 締め
	// =====================================================================
	probe.log("## 9. 締め");
	probe.log("");
	probe.log("開いている図面の件数: " + Num(static_cast<long>(CountOpenDocuments())) +
			  "（走らせる前と同じなら、開いた図面はすべて閉じられている）");
	probe.log("");
	probe.log("**利用者の図面には何も足していない**——既定を書き換えないだけでなく、"
			  "証人の線もプローブが自分で開いた図面の中だけで引いている"
			  "（図面を開けなかったときは、その旨が上に出ている）。");
	probe.log("");
	probe.log("### 読み方");
	probe.log("");
	probe.log("**1 節の比の欄**が主役である。");
	probe.log("");
	probe.log("- **比が全区間で一定**（例: どの点でも 0.667） → **単位（係数）**の話。"
			  "その係数が何インチ分かを 3 節のポイント口で裏取りする。");
	probe.log("- **小さい値では 1.0000 で、ある点から落ちる** → **上限（飽和）**。"
			  "落ち始める直前の値が上限で、**読めた値が一定になったところ**がその上限の"
			  "実寸である（旧口と新口で違う値に見えるなら、上限は 1 つでも"
			  "**読みの単位が 2 つある**ということになる）。");
	probe.log("- **2 節で既定だけが書いたとおりに保たれる** → 頭打ちは"
			  "**「オブジェクトに入れるとき」**に掛かっている（既定の器はもっと広い）。");
	probe.log("- **4 節の新口で往復する** → **大きさを指定する道は新口**である"
			  "（旧口は書ける範囲が狭い口だということになる）。");
	probe.log("- **8 節で縮尺を変えると読めた値が動く** → マーカーの大きさは"
			  "**図面の寸法**であり、1 節の頭打ちも縮尺込みで効いている"
			  "（動かなければ、紙の上の寸法である）。");
	probe.log("- **6 節の ① と ② で新口の `ret` が割れる** → `ret` は"
			  "「この線がマーカーを持っているか」の印で、`size=0.0000` は"
			  "**未設定**を意味する。割れなければ `0` は本当に 0 である。");
}
