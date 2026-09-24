//
//	probes/runtime/default-byclass-restore/probe.cpp
//
//	[issue #102] **文書の既定の by-class を by-instance へ戻せるか**を実測する。
//
//	#94（[Findings「描画属性とクラス」](Findings/Attributes%20and%20Classes.md)）で
//	「オブジェクトを作る前に文書の既定を立てておけば per-object の書き込みは要らない」
//	と分かった。残っているのは**後始末**で、ここが塞がっていると使えない:
//	`SetDefaultPColorsByClass()` / `SetDefaultFColorsByClass()` / `SetDefaultLWByClass()` /
//	`SetDefaultPPatByClass()` / `SetDefaultFPatByClass()` は**いずれも引数なし**で、
//	立てる一方（戻す口は `SetDefaultClass(id)` と `SetDefaultOpacityByClassN(pen, fill)`
//	だけだった。ヘッダ検索で確認済み）。
//
//	当たりは「**既定の値そのものを書けば by-class が下りる**」で、値を書く口は 4 本ある
//	（ペン色と面色は `ObjectColorType` 1 つに同居しているので、色は 1 本で両方を書く）:
//
//	  SetDefaultColors(ObjectColorType)   ← penFore/penBack/fillFore/fillBack
//	  SetDefaultLineWeight(short mils)
//	  SetDefaultPenPatN(InternalIndex)
//	  SetDefaultFillPat(InternalIndex)
//
//	ヘッダでは「下りるか」は分からない。確かめること:
//
//	  (1) **値を 1 本書くと、どの旗が下りるか。** 1 本書くたびに 5 旗（＋参考でマーカー）
//	      を読み直して、**書き込みと旗の対応を突き合わせる**。とくに `SetDefaultColors`
//	      が**ペン色と面色の両方**を下ろすのか、片方だけなのか。
//	  (2) **元の値を書き戻せば元どおりか。** 立てる前に読んだ値と、戻した後に読んだ値が
//	      一致するか（`ObjectColorType` には == がある）。
//	  (3) **旗が下りない書き込みがあったら、それは「値が変わらないから」か。**
//	      #94 で「費用は書き込みではなく**変化**に掛かる」と分かっている。旗が残った
//	      ものには「**わざと違う値 → 元の値**」の 2 段で書き直して切り分ける。
//	  (4) **戻したあとに作ったオブジェクトが by-instance で生まれるか**（矩形と
//	      構造材 PIO の両方で。#94 は「作る瞬間に既定を読む」と確定している）。
//	      per-object の旗が no で、値が元の既定と一致することまで見る。
//	  (5) 参考: **マーカー**（`SetDefaultArrowByClass()`）も引数なしで戻す口が無い。
//	      `SetDefaultArrowHeadsN` を書くと下りるか（#94 で「マーカーは既定を継がない」
//	      と分かっているので取り込みには影響しないが、立てるなら後始末は同じ話）。
//
//	新規の空図面で走らせる（プローブは undo イベントを自分では開かない）。**この
//	プローブは最後に文書の既定を走らせる前の状態へ戻す**——戻せるかどうかが調査の主題
//	なので、戻した結果もログに出す。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// issue #102 の対象——「クラスに従わせる」既定のうち、戻す口が無い 5 つ。
	// **並びは固定**で、以下の表の列に対応する。
	const int kAttrCount = 5;

	const char* const kAttrNames[kAttrCount] = {"PColors", "FColors", "LW", "PPat", "FPat"};

	const char* YesNo(Boolean value)
	{
		return value != 0 ? "yes" : "no";
	}

	Boolean GetDefaultFlag(int index)
	{
		switch (index)
		{
		case 0:
			return gSDK->GetDefaultPColorsByClass();
		case 1:
			return gSDK->GetDefaultFColorsByClass();
		case 2:
			return gSDK->GetDefaultLWByClass();
		case 3:
			return gSDK->GetDefaultPPatByClass();
		default:
			return gSDK->GetDefaultFPatByClass();
		}
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
		default:
			gSDK->SetDefaultFPatByClass();
			break;
		}
	}

	Boolean GetObjectFlag(int index, MCObjectHandle object)
	{
		switch (index)
		{
		case 0:
			return gSDK->GetPColorsByClass(object);
		case 1:
			return gSDK->GetFColorsByClass(object);
		case 2:
			return gSDK->GetLWByClass(object);
		case 3:
			return gSDK->GetPPatByClass(object);
		default:
			return gSDK->GetFPatByClass(object);
		}
	}

	// --- 既定の「値」側 --------------------------------------------------
	//
	// 戻す口の候補 4 本が書く値をひとまとめにする。ColorRef は Uint16、InternalIndex は
	// Sint32（どちらも整数）なので、そのまま数として出せる。

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

	std::string Num(long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%ld", value);
		return std::string(buffer);
	}

	std::string Real(double value)
	{
		char buffer[48];
		std::snprintf(buffer, sizeof(buffer), "%.2f", value);
		return std::string(buffer);
	}

	std::string DescribeColors(const ObjectColorType& colors)
	{
		return std::string("penFore=") + Num(static_cast<long>(colors.penFore)) +
			   " penBack=" + Num(static_cast<long>(colors.penBack)) +
			   " fillFore=" + Num(static_cast<long>(colors.fillFore)) +
			   " fillBack=" + Num(static_cast<long>(colors.fillBack));
	}

	std::string DescribeValues(const DefaultValues& values)
	{
		return DescribeColors(values.colors) +
			   " / lineWeight=" + Num(static_cast<long>(values.lineWeight)) +
			   " penPat=" + Num(static_cast<long>(values.penPat)) +
			   " fillPat=" + Num(static_cast<long>(values.fillPat));
	}

	// 5 旗（＋参考のマーカー）を 1 行の表にする。**「どの書き込みがどの旗を下ろしたか」を
	// 追うのがこの調査の山場**なので、書き込み 1 本ごとにこれを出す。
	std::string FlagRow(const std::string& label)
	{
		std::string row = "| " + label + " |";
		for (int i = 0; i < kAttrCount; ++i)
		{
			row += std::string(" ") + YesNo(GetDefaultFlag(i)) + " |";
		}
		row += std::string(" ") + YesNo(gSDK->GetDefaultArrowByClass()) + " |";
		return row;
	}

	void LogFlagHeader(vwprobe::Report& probe)
	{
		std::string header = "| 何をした後 |";
		for (int i = 0; i < kAttrCount; ++i)
		{
			header += std::string(" ") + kAttrNames[i] + " |";
		}
		header += " Arrow（参考） |";
		probe.log(header);

		std::string rule = "| --- |";
		for (int i = 0; i <= kAttrCount; ++i)
		{
			rule += " --- |";
		}
		probe.log(rule);
	}

	// --- 作って読む ------------------------------------------------------
	//
	// #94 と同じ 2 種類で確かめる（PIO は作る瞬間の作法が違うので、片方だけでは
	// 「オブジェクトの種類に依らない」と言えない）。鉛直材の作法は
	// [Findings「Parametric Objects」](Findings/Parametric%20Objects.md)。

	MCObjectHandle CreateRect(int serial)
	{
		const WorldCoord step = 1000;
		const WorldCoord x = static_cast<WorldCoord>(serial) * step;
		WorldRect bounds(x, step, x + step, 0);
		return gSDK->CreateRectangle(bounds);
	}

	MCObjectHandle CreateStructuralMember(int serial)
	{
		const WorldCoord step = 1000;
		const WorldCoord x = static_cast<WorldCoord>(serial) * step;
		MCObjectHandle path = gSDK->CreateNurbsCurve(WorldPt3(x, 0, 0), true, 3);
		if (path == nil)
		{
			return nil;
		}
		gSDK->Add3DVertex(path, WorldPt3(x, 0, 3000));
		return gSDK->CreateCustomObjectPath("StructuralMember", path, nil, true);
	}

	// オブジェクト 1 つの「5 旗」と「値」を読んでログへ出す。
	void LogObject(vwprobe::Report& probe, const char* what, MCObjectHandle object)
	{
		if (object == nil)
		{
			probe.log(std::string("- ") + what + ": 作れなかった（nil）");
			return;
		}

		std::string flags;
		for (int i = 0; i < kAttrCount; ++i)
		{
			flags += std::string(i == 0 ? "" : " ") + kAttrNames[i] + "=" +
					 YesNo(GetObjectFlag(i, object));
		}

		ObjectColorType colors{};
		gSDK->GetColor(object, colors);
		probe.log(std::string("- ") + what + " の旗: " + flags);
		probe.log(std::string("  ") + what + " の値: " + DescribeColors(colors) +
				  " / lineWeight=" + Num(static_cast<long>(gSDK->GetLineWeight(object))) +
				  " penPat=" + Num(static_cast<long>(gSDK->GetPenPatN(object))) +
				  " fillPat=" + Num(static_cast<long>(gSDK->GetFillPat(object))));
	}
} // namespace

VW_PROBE("default-byclass-restore", "文書の既定の by-class を戻せるかを実測する",
		 "5 つの SetDefault*ByClass を立ててから既定の値を書き戻し、旗が下りるか・"
		 "値が元どおりか・その後に作ったものが by-instance で生まれるかを確かめる")
{
	// =====================================================================
	// 0. 触る前の状態を退避する（＝「退避・復元」の退避そのもの）。
	// =====================================================================
	probe.log("## 0. 触る前の状態");
	probe.log("");
	LogFlagHeader(probe);
	probe.log(FlagRow("走らせる前"));
	probe.log("");

	// **旗も配列で退避する**（この後で書き換えるので、締めの節では読み直せない）。
	Boolean originalFlags[kAttrCount] = {};
	for (int i = 0; i < kAttrCount; ++i)
	{
		originalFlags[i] = GetDefaultFlag(i);
	}

	const DefaultValues original = ReadDefaultValues();
	probe.log(std::string("退避した既定の値: ") + DescribeValues(original));

	Boolean originalArrowByClass = gSDK->GetDefaultArrowByClass();
	Boolean arrowStart = 0;
	Boolean arrowEnd = 0;
	ArrowType arrowStyle = 0;
	double_gs arrowSize = 0;
	gSDK->GetDefaultArrowHeadsN(arrowStart, arrowEnd, arrowStyle, arrowSize);
	probe.log(std::string("退避したマーカー（参考）: start=") + YesNo(arrowStart) +
			  " end=" + YesNo(arrowEnd) + " style=" + Num(static_cast<long>(arrowStyle)) +
			  " size=" + Real(static_cast<double>(arrowSize)));

	{
		bool alreadyByClass = false;
		for (int i = 0; i < kAttrCount; ++i)
		{
			if (originalFlags[i] != 0)
			{
				alreadyByClass = true;
			}
		}
		if (alreadyByClass)
		{
			probe.log("※ 走らせる前から by-class の旗が立っている（新規の空図面ではない）。"
					  "『元へ戻す』の元がこの状態になる。");
		}
	}
	probe.log("");

	// =====================================================================
	// 1. 5 つを立てる（取り込みの高速化が実際にすること）。
	// =====================================================================
	probe.log("## 1. 5 つの SetDefault*ByClass を立てる");
	probe.log("");
	for (int i = 0; i < kAttrCount; ++i)
	{
		SetDefaultFlag(i);
	}
	gSDK->SetDefaultArrowByClass(); // 参考（取り込み側は 7 つ立てる）
	LogFlagHeader(probe);
	probe.log(FlagRow("5 つ＋マーカーを立てた後"));
	probe.log("");
	probe.log(std::string("立てた後に読んだ既定の値: ") + DescribeValues(ReadDefaultValues()));
	probe.log("");

	probe.log("立てた状態で作ったもの（既定が効いていることの確認）:");
	MCObjectHandle bornByClass = CreateRect(0);
	if (bornByClass == nil)
	{
		probe.fail("CreateRectangle が nil を返した（以降の比較ができない）");
		return;
	}
	LogObject(probe, "矩形 A（by-class の既定で生まれた）", bornByClass);
	probe.log("");

	// =====================================================================
	// 2. **山場**: 値を 1 本ずつ書き戻して、どの旗が下りるかを見る。
	// =====================================================================
	probe.log("## 2. 退避した値を 1 本ずつ書き戻す（どの書き込みがどの旗を下ろすか）");
	probe.log("");
	LogFlagHeader(probe);
	probe.log(FlagRow("書き戻す前"));

	gSDK->SetDefaultColors(original.colors);
	probe.log(FlagRow("SetDefaultColors"));

	gSDK->SetDefaultLineWeight(original.lineWeight);
	probe.log(FlagRow("SetDefaultLineWeight"));

	gSDK->SetDefaultPenPatN(original.penPat);
	probe.log(FlagRow("SetDefaultPenPatN"));

	gSDK->SetDefaultFillPat(original.fillPat);
	probe.log(FlagRow("SetDefaultFillPat"));
	probe.log("");

	{
		const DefaultValues afterRestore = ReadDefaultValues();
		probe.log(std::string("書き戻した後の値: ") + DescribeValues(afterRestore));
		const bool colorsSame = (afterRestore.colors == original.colors);
		const bool restSame = afterRestore.lineWeight == original.lineWeight &&
							  afterRestore.penPat == original.penPat &&
							  afterRestore.fillPat == original.fillPat;
		probe.log(std::string("退避した値と一致するか: 色=") + (colorsSame ? "一致" : "不一致") +
				  " それ以外=" + (restSame ? "一致" : "不一致"));
	}
	probe.log("");

	// =====================================================================
	// 3. 下りなかった旗があれば「わざと違う値 → 元の値」で切り分ける。
	//    #94 の「費用は変化に掛かる」に倣い、**同じ値の書き込みは無視される**のか
	//    どうかを見る。
	// =====================================================================
	probe.log("## 3. 残った旗を「違う値 → 元の値」で切り分ける");
	probe.log("");
	{
		bool anyLeft = false;
		for (int i = 0; i < kAttrCount; ++i)
		{
			if (GetDefaultFlag(i) != 0)
			{
				anyLeft = true;
			}
		}
		if (!anyLeft)
		{
			probe.log("5 旗とも下りているので、この切り分けは要らない。");
		}
		else
		{
			LogFlagHeader(probe);
			probe.log(FlagRow("切り分けの前"));

			// 色: ペンと面を入れ替えた値（新規図面ならペン=黒・面前景=白で必ず違う）。
			ObjectColorType swapped = original.colors;
			swapped.penFore = original.colors.fillFore;
			swapped.penBack = original.colors.fillBack;
			swapped.fillFore = original.colors.penFore;
			swapped.fillBack = original.colors.penBack;
			probe.log(std::string("（違う値として使う色: ") + DescribeColors(swapped) + "）");
			gSDK->SetDefaultColors(swapped);
			probe.log(FlagRow("SetDefaultColors（違う値）"));
			gSDK->SetDefaultColors(original.colors);
			probe.log(FlagRow("SetDefaultColors（元の値へ）"));

			gSDK->SetDefaultLineWeight(static_cast<short>(original.lineWeight + 1));
			probe.log(FlagRow("SetDefaultLineWeight（＋1）"));
			gSDK->SetDefaultLineWeight(original.lineWeight);
			probe.log(FlagRow("SetDefaultLineWeight（元の値へ）"));

			const InternalIndex otherPenPat = (original.penPat == 2) ? 3 : 2;
			gSDK->SetDefaultPenPatN(otherPenPat);
			probe.log(FlagRow("SetDefaultPenPatN（違う値）"));
			gSDK->SetDefaultPenPatN(original.penPat);
			probe.log(FlagRow("SetDefaultPenPatN（元の値へ）"));

			const InternalIndex otherFillPat = (original.fillPat == 2) ? 3 : 2;
			gSDK->SetDefaultFillPat(otherFillPat);
			probe.log(FlagRow("SetDefaultFillPat（違う値）"));
			gSDK->SetDefaultFillPat(original.fillPat);
			probe.log(FlagRow("SetDefaultFillPat（元の値へ）"));
			probe.log("");

			const DefaultValues afterRetry = ReadDefaultValues();
			probe.log(std::string("切り分けの後の値: ") + DescribeValues(afterRetry));
			probe.log(
				std::string("退避した値と一致するか: 色=") +
				((afterRetry.colors == original.colors) ? "一致" : "不一致") + " それ以外=" +
				((afterRetry.lineWeight == original.lineWeight &&
				  afterRetry.penPat == original.penPat && afterRetry.fillPat == original.fillPat)
					 ? "一致"
					 : "不一致"));
		}
	}
	probe.log("");

	// =====================================================================
	// 4. 戻した後に作ったものが by-instance で生まれるか（矩形と PIO の両方）。
	// =====================================================================
	probe.log("## 4. 戻した後に作ったもの");
	probe.log("");
	probe.log("期待: 5 旗とも no で、値が退避した既定と一致する。");
	probe.log("");
	LogObject(probe, "矩形 B（戻した後に生まれた）", CreateRect(2));
	MCObjectHandle pio = CreateStructuralMember(4);
	LogObject(probe, "構造材 PIO（戻した後に生まれた）", pio);
	probe.log("");
	probe.log(std::string("（比較用）退避した既定の値: ") + DescribeValues(original));
	probe.log("");

	// =====================================================================
	// 5. 参考: マーカーの旗を下ろす口はあるか。
	// =====================================================================
	probe.log("## 5. 参考: マーカー（SetDefaultArrowByClass）を下ろせるか");
	probe.log("");
	LogFlagHeader(probe);
	probe.log(FlagRow("マーカーを書き戻す前"));
	gSDK->SetDefaultArrowHeadsN(arrowStart, arrowEnd, arrowStyle, arrowSize);
	probe.log(FlagRow("SetDefaultArrowHeadsN（退避した値）"));
	{
		// 「同じ値だから無視された」を潰すため、旗が残っていたら違う値でも試す。
		if (gSDK->GetDefaultArrowByClass() != 0)
		{
			gSDK->SetDefaultArrowHeadsN(arrowStart == 0 ? 1 : 0, arrowEnd, arrowStyle,
										arrowSize > 0 ? arrowSize : 3);
			probe.log(FlagRow("SetDefaultArrowHeadsN（違う値）"));
			gSDK->SetDefaultArrowHeadsN(arrowStart, arrowEnd, arrowStyle, arrowSize);
			probe.log(FlagRow("SetDefaultArrowHeadsN（元の値へ）"));
		}
	}
	probe.log("");

	// =====================================================================
	// 6. 締め: 走らせる前の状態へ戻す（戻せたかどうかもログに出す）。
	// =====================================================================
	probe.log("## 6. 締め: 走らせる前の状態へ戻す");
	probe.log("");
	probe.log("0 節で退避した旗のうち、**走らせる前に立っていたものだけ**を立て直す"
			  "（値は 2〜3 節で既に書き戻してある）。");
	probe.log("");
	for (int i = 0; i < kAttrCount; ++i)
	{
		if (originalFlags[i] != 0)
		{
			SetDefaultFlag(i);
		}
	}
	if (originalArrowByClass != 0)
	{
		gSDK->SetDefaultArrowByClass();
	}
	LogFlagHeader(probe);
	probe.log(FlagRow("締めの後"));
	probe.log("");
	probe.log(std::string("締めの後の値: ") + DescribeValues(ReadDefaultValues()));
	probe.log(std::string("（比較用）退避した既定の値: ") + DescribeValues(original));
}
