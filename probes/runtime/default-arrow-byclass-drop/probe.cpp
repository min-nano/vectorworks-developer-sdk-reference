//
//	probes/runtime/default-arrow-byclass-drop/probe.cpp
//
//	[issue #104] **文書の既定のマーカー by-class（`SetDefaultArrowByClass()`）を、
//	誰が下ろしているか**を実測する。あわせて**意図して下ろす口があるか**を確かめる。
//
//	出どころは 2 つの観測である。
//
//	  (a) #94（[Findings「描画属性とクラス」](Findings/Attributes%20and%20Classes.md)）——
//	      7 つの既定を立ててから 20 個ずつ作ると、6 つは 20/20 が継ぐのに
//	      **マーカーだけが PIO で 1/20・矩形で 0/20** しか継がない。この半端な数の
//	      理由が分かっていない（#94 の表は PIO → 矩形の順で走らせている）。
//	  (b) #102 のプローブ（`default-byclass-restore`）の実測ログ——
//	      `GetDefaultArrowByClass()` が **yes → no** に変わった区間があり、
//	      その区間でしたことは「矩形を 1 つと構造材 PIO を 1 つ作る」だけだった。
//	      同じログの前半では**矩形を作っても yes のまま**だったので、残る候補は
//	      **構造材 PIO の作成**。
//
//	この 2 つが同じ 1 つの現象なら、**「最初の PIO がマーカーを継いだ、その作成で旗が
//	下り、以降は誰も継げない」**ことになり、(a) の 1/20 も、その後に作った矩形の 0/20 も
//	説明が付く。確かめること:
//
//	  (1) **旗を下ろしているのは何か。** 矩形の作成では下りないこと、PIO の作成で
//	      下りることを、1 つ作るごとに 7 旗を読んで突き合わせる。
//	  (2) **下ろしているのは「作成」か「作り直し（regen）」か。** ここが**汎用性の要**で、
//	      regen が犯人なら構造材 PIO に限らず**あらゆる PIO の作成・`ResetObject`・
//	      `UpdateStyledObjects`** が同じことをする。切り分けは 3 手:
//	        - `CreateCustomObjectPath(..., doRegen=false)` で作る（regen を伴わない作成）
//	        - その PIO へ `ResetObject`（作成を伴わない regen）
//	        - 矩形へ `ResetObject`（PIO でないものの作り直し）
//	  (3) **巻き添えは無いか。** マーカー以外の 6 旗（ペン色・面色・線の太さ・線種・
//	      面パターン・不透明度）も**毎回同時に読む**。PIO の作成が 6 旗まで下ろすなら、
//	      #94 の高速化（既定を先に立てて大量に作る）が**取り込みの途中で崩れる**ので、
//	      そこが本当の危険である（#94 では 20/20 が継いでいたので崩れない見込みだが、
//	      **旗そのものを読んだ記録が無い**）。
//	  (4) **立て直せば毎回継げるか**（回避の道があるか）。3 回続けて
//	      「立てる → PIO を作る → その PIO の旗を読む」を回す。
//	  (5) **意図して下ろす口はあるか。** #102 で 5 つは「既定の**値**を書けば旗が下りる」
//	      と確定した。マーカーで同じ筋（`SetDefaultArrowHeadsN` を書く）が効くか——
//	      #102 では試した時点で旗が既に no だったため**判定できていない**。
//	      「同じ値だから無視された」を潰すため、同じ値と違う値の両方で書く。
//
//	新規の空図面で走らせる（プローブは undo イベントを自分では開かない）。
//	**このプローブは最後に文書の既定を走らせる前の状態へ戻す**（プラグインは利用者の
//	図面の設定を書き換えない方針）。戻せたかどうかもログに出す。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// 「クラスに従わせる」文書の既定 7 つ。**並びは固定**で、以下の表の列に対応する。
	// 不透明度はペンと面の 2 旗を持つので、読み出しは 2 旗版を使う
	// （1 旗版 GetDefaultOpacityByClass は読み戻しに使えない。#94）。
	const int kFlagCount = 7;

	const char* const kFlagNames[kFlagCount] = {"PColors", "FColors", "LW",	  "PPat",
												"FPat",	   "Opacity", "Arrow"};

	const char* YesNo(Boolean value)
	{
		return value != 0 ? "yes" : "no";
	}

	// 既定の旗 1 つを読む。不透明度だけは 2 旗あるので、片方だけ立っている状態を
	// "pen" / "fill" として区別できるよう、文字列で返す。
	std::string GetDefaultFlagText(int index)
	{
		switch (index)
		{
		case 0:
			return YesNo(gSDK->GetDefaultPColorsByClass());
		case 1:
			return YesNo(gSDK->GetDefaultFColorsByClass());
		case 2:
			return YesNo(gSDK->GetDefaultLWByClass());
		case 3:
			return YesNo(gSDK->GetDefaultPPatByClass());
		case 4:
			return YesNo(gSDK->GetDefaultFPatByClass());
		case 5:
		{
			Boolean pen = 0;
			Boolean fill = 0;
			gSDK->GetDefaultOpacityByClassN(pen, fill);
			if (pen != 0 && fill != 0)
			{
				return "yes";
			}
			if (pen == 0 && fill == 0)
			{
				return "no";
			}
			return pen != 0 ? "pen のみ" : "fill のみ";
		}
		default:
			return YesNo(gSDK->GetDefaultArrowByClass());
		}
	}

	bool IsDefaultFlagUp(int index)
	{
		return GetDefaultFlagText(index) != "no";
	}

	void SetDefaultFlag(int index)
	{
		switch (index)
		{
		case 0:
			gSDK->SetDefaultPColorsByClass();
			break;
		case 1:
			gSDK->SetDefaultFColorsByClass();
			break;
		case 2:
			gSDK->SetDefaultLWByClass();
			break;
		case 3:
			gSDK->SetDefaultPPatByClass();
			break;
		case 4:
			gSDK->SetDefaultFPatByClass();
			break;
		case 5:
			gSDK->SetDefaultOpacityByClass();
			break;
		default:
			gSDK->SetDefaultArrowByClass();
			break;
		}
	}

	// オブジェクト側の同じ 7 旗。
	std::string GetObjectFlagText(int index, MCObjectHandle object)
	{
		switch (index)
		{
		case 0:
			return YesNo(gSDK->GetPColorsByClass(object));
		case 1:
			return YesNo(gSDK->GetFColorsByClass(object));
		case 2:
			return YesNo(gSDK->GetLWByClass(object));
		case 3:
			return YesNo(gSDK->GetPPatByClass(object));
		case 4:
			return YesNo(gSDK->GetFPatByClass(object));
		case 5:
		{
			Boolean pen = 0;
			Boolean fill = 0;
			gSDK->GetOpacityByClassN(object, pen, fill);
			if (pen != 0 && fill != 0)
			{
				return "yes";
			}
			if (pen == 0 && fill == 0)
			{
				return "no";
			}
			return pen != 0 ? "pen のみ" : "fill のみ";
		}
		default:
			return YesNo(gSDK->GetArrowByClass(object));
		}
	}

	// --- 表 --------------------------------------------------------------
	//
	// 「どの操作がどの旗を下ろしたか」を追うのがこの調査の山場なので、**操作 1 つごとに
	// 7 旗を 1 行にして出す**。マーカーは最後の列（主役）。

	void LogFlagHeader(vwprobe::Report& probe)
	{
		std::string header = "| 何をした後 |";
		std::string rule = "| --- |";
		for (int i = 0; i < kFlagCount; ++i)
		{
			header += std::string(" ") + kFlagNames[i] + " |";
			rule += " --- |";
		}
		probe.log(header);
		probe.log(rule);
	}

	void LogFlagRow(vwprobe::Report& probe, const std::string& label)
	{
		std::string row = "| " + label + " |";
		for (int i = 0; i < kFlagCount; ++i)
		{
			row += " " + GetDefaultFlagText(i) + " |";
		}
		probe.log(row);
	}

	void LogObjectRow(vwprobe::Report& probe, const std::string& label, MCObjectHandle object)
	{
		if (object == nil)
		{
			probe.log("| " + label + " | **作れなかった（nil）** | | | | | | |");
			return;
		}
		std::string row = "| " + label + " |";
		for (int i = 0; i < kFlagCount; ++i)
		{
			row += " " + GetObjectFlagText(i, object) + " |";
		}
		probe.log(row);
	}

	// --- 作る ------------------------------------------------------------

	int gSerial = 0;

	WorldCoord NextX()
	{
		const WorldCoord step = 1000;
		return static_cast<WorldCoord>(gSerial++) * step;
	}

	MCObjectHandle CreateRect()
	{
		const WorldCoord x = NextX();
		WorldRect bounds(x, 1000, x + 1000, 0);
		return gSDK->CreateRectangle(bounds);
	}

	// 構造材 PIO。作法は [Findings「Parametric Objects」](Findings/Parametric%20Objects.md)。
	// **doRegen を呼び手が選べる**のがこの調査の肝（作成と作り直しを切り分けるため）。
	MCObjectHandle CreateStructuralMember(bool doRegen)
	{
		const WorldCoord x = NextX();
		MCObjectHandle path = gSDK->CreateNurbsCurve(WorldPt3(x, 0, 0), true, 3);
		if (path == nil)
		{
			return nil;
		}
		gSDK->Add3DVertex(path, WorldPt3(x, 0, 3000));
		return gSDK->CreateCustomObjectPath("StructuralMember", path, nil, doRegen);
	}

	// --- 既定の「値」側（締めで元へ戻すために退避する） --------------------

	struct DefaultValues
	{
		ObjectColorType colors{};
		short lineWeight = 0;
		InternalIndex penPat = 0;
		InternalIndex fillPat = 0;
	};

	DefaultValues ReadDefaultValues()
	{
		DefaultValues values;
		gSDK->GetDefaultColors(values.colors);
		values.lineWeight = gSDK->GetDefaultLineWeight();
		values.penPat = gSDK->GetDefaultPenPatN();
		values.fillPat = gSDK->GetDefaultFillPat();
		return values;
	}

	void WriteDefaultValues(const DefaultValues& values)
	{
		gSDK->SetDefaultColors(values.colors);
		gSDK->SetDefaultLineWeight(values.lineWeight);
		gSDK->SetDefaultPenPatN(values.penPat);
		gSDK->SetDefaultFillPat(values.fillPat);
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

	std::string DescribeValues(const DefaultValues& values)
	{
		return std::string("penFore=") + Num(static_cast<long>(values.colors.penFore)) +
			   " penBack=" + Num(static_cast<long>(values.colors.penBack)) +
			   " fillFore=" + Num(static_cast<long>(values.colors.fillFore)) +
			   " fillBack=" + Num(static_cast<long>(values.colors.fillBack)) +
			   " / lineWeight=" + Num(static_cast<long>(values.lineWeight)) +
			   " penPat=" + Num(static_cast<long>(values.penPat)) +
			   " fillPat=" + Num(static_cast<long>(values.fillPat));
	}

	struct ArrowValues
	{
		Boolean starting = 0;
		Boolean ending = 0;
		ArrowType style = 0;
		double_gs size = 0;
	};

	ArrowValues ReadArrowValues()
	{
		ArrowValues values;
		gSDK->GetDefaultArrowHeadsN(values.starting, values.ending, values.style, values.size);
		return values;
	}

	std::string DescribeArrow(const ArrowValues& values)
	{
		return std::string("start=") + YesNo(values.starting) + " end=" + YesNo(values.ending) +
			   " style=" + Num(static_cast<long>(values.style)) +
			   " size=" + Real(static_cast<double>(values.size));
	}

	// マーカーの旗を立て直す。**立っていなければ立て、立っていればそのまま**——
	// 各節が「旗が立った状態から始まる」ことを保証するための道具。
	void RaiseArrowFlag(vwprobe::Report& probe, const char* where)
	{
		gSDK->SetDefaultArrowByClass();
		if (gSDK->GetDefaultArrowByClass() == 0)
		{
			probe.fail(std::string("SetDefaultArrowByClass() を呼んでも旗が立たなかった（") +
					   where + "）");
		}
	}
} // namespace

VW_PROBE("default-arrow-byclass-drop", "既定のマーカー by-class を誰が下ろすかを実測する",
		 "7 つの既定を立ててから矩形と構造材 PIO を作り、旗がどこで下りるかを 1 つ作るごとに"
		 "読む。作成と作り直し（regen）を切り分け、意図して下ろす口があるかも確かめる")
{
	// =====================================================================
	// 0. 触る前の状態を退避する（締めで戻すため）。
	// =====================================================================
	probe.log("## 0. 触る前の状態");
	probe.log("");
	LogFlagHeader(probe);
	LogFlagRow(probe, "走らせる前");
	probe.log("");

	bool originalFlags[kFlagCount] = {};
	for (int i = 0; i < kFlagCount; ++i)
	{
		originalFlags[i] = IsDefaultFlagUp(i);
	}
	const DefaultValues originalValues = ReadDefaultValues();
	const ArrowValues originalArrow = ReadArrowValues();
	probe.log(std::string("退避した既定の値: ") + DescribeValues(originalValues));
	probe.log(std::string("退避したマーカーの値: ") + DescribeArrow(originalArrow));

	{
		bool anyUp = false;
		for (int i = 0; i < kFlagCount; ++i)
		{
			if (originalFlags[i])
			{
				anyUp = true;
			}
		}
		if (anyUp)
		{
			probe.log("※ 走らせる前から by-class の旗が立っている（新規の空図面ではない）。"
					  "『元へ戻す』の元がこの状態になる。");
		}
	}
	probe.log("");

	// =====================================================================
	// 1. 7 つを立てる（#94 の高速化が取り込みの初めにすること）。
	// =====================================================================
	probe.log("## 1. 7 つの既定を立てる（取り込みと同じ）");
	probe.log("");
	for (int i = 0; i < kFlagCount; ++i)
	{
		SetDefaultFlag(i);
	}
	LogFlagHeader(probe);
	LogFlagRow(probe, "7 つを立てた後");
	probe.log("");
	if (gSDK->GetDefaultArrowByClass() == 0)
	{
		probe.fail("SetDefaultArrowByClass() を呼んでも旗が立たなかった"
				   "（この調査の前提が崩れている。以降の読みは当てにならない）");
	}

	// =====================================================================
	// 2〜4. 1 つ作るごとに「文書の既定の 7 旗」と「そのオブジェクトの 7 旗」を読む。
	//       **#94 の 1/20・0/20 がここで再現するはず**（PIO → 矩形の順）。
	// =====================================================================
	probe.log("## 2. 1 つ作るごとに読む（本題）");
	probe.log("");
	probe.log("**文書の既定の旗**（作った直後に読み直したもの）:");
	probe.log("");
	LogFlagHeader(probe);
	LogFlagRow(probe, "何も作っていない");

	MCObjectHandle rectA = CreateRect();
	LogFlagRow(probe, "矩形 A を作った後");

	MCObjectHandle pio1 = CreateStructuralMember(true);
	LogFlagRow(probe, "**構造材 PIO 1 を作った後**");

	MCObjectHandle pio2 = CreateStructuralMember(true);
	LogFlagRow(probe, "構造材 PIO 2 を作った後");

	MCObjectHandle rectB = CreateRect();
	LogFlagRow(probe, "矩形 B を作った後");
	probe.log("");

	probe.log("**生まれたオブジェクトの旗**（per-object の書き込みは 1 度もしていない）:");
	probe.log("");
	LogFlagHeader(probe);
	LogObjectRow(probe, "矩形 A", rectA);
	LogObjectRow(probe, "構造材 PIO 1", pio1);
	LogObjectRow(probe, "構造材 PIO 2", pio2);
	LogObjectRow(probe, "矩形 B", rectB);
	probe.log("");
	probe.log("読み方: #94 の「マーカーだけ PIO 20 個中 1 個・矩形 0 個」が正しければ、"
			  "**PIO 1 だけが Arrow=yes**・PIO 2 と矩形 B は no になる（矩形 A は PIO の前に"
			  "作られているので yes のはず）。");
	probe.log("");

	if (pio1 == nil || pio2 == nil)
	{
		probe.fail("構造材 PIO を作れなかった（CreateCustomObjectPath が nil）。"
				   "以降の節は当てにならない");
	}

	// =====================================================================
	// 5. 下ろしているのは「作成」か「作り直し（regen）」か。
	//    **ここが汎用性の要**——regen が犯人なら、構造材 PIO に限らず
	//    あらゆる PIO の作成・ResetObject・UpdateStyledObjects が同じことをする。
	// =====================================================================
	probe.log("## 3. 下ろしているのは「作成」か「作り直し（regen）」か");
	probe.log("");
	LogFlagHeader(probe);

	RaiseArrowFlag(probe, "3 節の頭");
	LogFlagRow(probe, "マーカーを立て直した");

	// (a) regen を伴わない作成。
	MCObjectHandle pio3 = CreateStructuralMember(false);
	LogFlagRow(probe, "**PIO 3 を doRegen=false で作った後**");

	// (b) 作成を伴わない regen。旗が (a) で下りていたら立て直してから測る
	//     ——「下りているものは下りない」を読み違えないため。
	RaiseArrowFlag(probe, "ResetObject（PIO）の前");
	LogFlagRow(probe, "（測る前に立て直した）");
	if (pio3 != nil)
	{
		gSDK->ResetObject(pio3);
		LogFlagRow(probe, "**PIO 3 へ ResetObject した後**");
	}
	else
	{
		probe.log("| PIO 3 へ ResetObject した後 | （PIO 3 を作れなかったので測れない） | | | "
				  "| | | |");
	}

	// (c) PIO でないものの作り直し。
	RaiseArrowFlag(probe, "ResetObject（矩形）の前");
	LogFlagRow(probe, "（測る前に立て直した）");
	if (rectA != nil)
	{
		gSDK->ResetObject(rectA);
		LogFlagRow(probe, "矩形 A へ ResetObject した後");
	}
	probe.log("");
	probe.log("読み方: **doRegen=false で下りず、ResetObject で下りる**なら犯人は"
			  "**作り直し（regen）**で、構造材 PIO に限らない話になる。"
			  "**doRegen=false でも下りる**なら犯人は**作成そのもの**。"
			  "矩形の ResetObject でも下りるなら、PIO ですらなく**作り直し一般**。");
	probe.log("");

	// =====================================================================
	// 6. 立て直せば毎回継げるか（回避の道があるか）。
	// =====================================================================
	probe.log("## 4. 立て直せば毎回継げるか（1 本ごとに立て直す道）");
	probe.log("");
	probe.log("**PIO を作る直前に毎回 `SetDefaultArrowByClass()` を呼ぶ**と、"
			  "その PIO は毎回マーカーを継ぐか。3 本続けて試す。");
	probe.log("");
	probe.log("| 回 | 作る直前の既定の Arrow | 作った後の既定の Arrow | その PIO の Arrow |");
	probe.log("| --- | --- | --- | --- |");
	for (int round = 1; round <= 3; ++round)
	{
		gSDK->SetDefaultArrowByClass();
		const std::string before = YesNo(gSDK->GetDefaultArrowByClass());
		MCObjectHandle member = CreateStructuralMember(true);
		const std::string after = YesNo(gSDK->GetDefaultArrowByClass());
		const std::string onObject =
			member != nil ? YesNo(gSDK->GetArrowByClass(member)) : std::string("（作れず）");
		probe.log("| " + Num(static_cast<long>(round)) + " | " + before + " | " + after + " | " +
				  onObject + " |");
	}
	probe.log("");

	// =====================================================================
	// 7. 意図して下ろす口はあるか（#102 と同じ筋がマーカーで効くか）。
	// =====================================================================
	probe.log("## 5. 意図して下ろす口はあるか（`SetDefaultArrowHeadsN`）");
	probe.log("");
	probe.log("#102 で 5 つは「既定の**値**を書けば旗が下りる」と確定した。"
			  "マーカーで同じ筋が効くかを、**同じ値**と**違う値**の両方で試す"
			  "（「同じ値だから無視された」を潰すため）。");
	probe.log("");
	LogFlagHeader(probe);

	RaiseArrowFlag(probe, "5 節の頭");
	LogFlagRow(probe, "マーカーを立て直した");

	const ArrowValues current = ReadArrowValues();
	probe.log("");
	probe.log(std::string("（いま読めるマーカーの値: ") + DescribeArrow(current) + "）");
	probe.log("");
	LogFlagHeader(probe);

	gSDK->SetDefaultArrowHeadsN(current.starting, current.ending, current.style, current.size);
	LogFlagRow(probe, "`SetDefaultArrowHeadsN`（**同じ値**）");

	if (gSDK->GetDefaultArrowByClass() != 0)
	{
		// わざと違う値。start を反転し、大きさも変える（size=0 のときに 0 のままだと
		// 「何も変わっていない」になりかねないため）。
		const Boolean otherStarting = static_cast<Boolean>(current.starting != 0 ? 0 : 1);
		const double_gs otherSize = current.size > 0 ? current.size * 2 : 3;
		gSDK->SetDefaultArrowHeadsN(otherStarting, current.ending, current.style, otherSize);
		LogFlagRow(probe, "`SetDefaultArrowHeadsN`（**違う値**）");
		probe.log(std::string("（書いた違う値: start=") + YesNo(otherStarting) +
				  " size=" + Real(static_cast<double>(otherSize)) + "）");
	}
	probe.log("");
	probe.log(std::string("この節の後のマーカーの値: ") + DescribeArrow(ReadArrowValues()));
	probe.log("");
	probe.log("読み方: **同じ値で下りる**なら後始末は 1 行で済む。"
			  "**違う値でしか下りない**なら「退避した値 → 違う値 → 退避した値」の 3 手が要る。"
			  "**どちらでも下りない**なら、マーカーの旗を意図して下ろす口は無い"
			  "（残る道は PIO を 1 つ作ることだけで、それは後始末として使えない）。");
	probe.log("");

	// =====================================================================
	// 8. 締め: 走らせる前の状態へ戻す。
	// =====================================================================
	probe.log("## 6. 締め: 走らせる前の状態へ戻す");
	probe.log("");
	probe.log("既定の**値**を書き戻し（#102 の確定——これで 5 旗が下りる）、"
			  "不透明度は 2 旗版で明示的に下ろし、"
			  "**走らせる前に立っていた旗だけ**を立て直す。");
	probe.log("");
	WriteDefaultValues(originalValues);
	gSDK->SetDefaultOpacityByClassN(static_cast<Boolean>(0), static_cast<Boolean>(0));
	gSDK->SetDefaultArrowHeadsN(originalArrow.starting, originalArrow.ending, originalArrow.style,
								originalArrow.size);
	LogFlagHeader(probe);
	LogFlagRow(probe, "値を書き戻した後（旗を立て直す前）");

	for (int i = 0; i < kFlagCount; ++i)
	{
		if (originalFlags[i])
		{
			SetDefaultFlag(i);
		}
	}
	LogFlagRow(probe, "締めの後");
	probe.log("");

	{
		const DefaultValues finalValues = ReadDefaultValues();
		const ArrowValues finalArrow = ReadArrowValues();
		probe.log(std::string("締めの後の値: ") + DescribeValues(finalValues));
		probe.log(std::string("（比較用）退避した値: ") + DescribeValues(originalValues));
		probe.log(std::string("締めの後のマーカーの値: ") + DescribeArrow(finalArrow));
		probe.log(std::string("（比較用）退避したマーカーの値: ") + DescribeArrow(originalArrow));

		const bool valuesSame = (finalValues.colors == originalValues.colors) &&
								finalValues.lineWeight == originalValues.lineWeight &&
								finalValues.penPat == originalValues.penPat &&
								finalValues.fillPat == originalValues.fillPat;
		const bool arrowSame = finalArrow.starting == originalArrow.starting &&
							   finalArrow.ending == originalArrow.ending &&
							   finalArrow.style == originalArrow.style &&
							   finalArrow.size == originalArrow.size;
		probe.log(std::string("値は元どおりか: 描画属性=") + (valuesSame ? "一致" : "**不一致**") +
				  " マーカー=" + (arrowSame ? "一致" : "**不一致**"));

		bool flagsSame = true;
		for (int i = 0; i < kFlagCount; ++i)
		{
			if (IsDefaultFlagUp(i) != originalFlags[i])
			{
				flagsSame = false;
				probe.log(std::string("旗が戻っていない: ") + kFlagNames[i] +
						  "（走らせる前=" + (originalFlags[i] ? "yes" : "no") +
						  " いま=" + GetDefaultFlagText(i) + "）");
			}
		}
		if (flagsSame)
		{
			probe.log("旗は 7 つとも走らせる前と同じ。");
		}
	}
}
