//
//	probes/runtime/dim-page-lengths-and-scale/probe.cpp
//
//	[issue #145] **寸法で「作るときの縮尺に焼き付く」のは文字の大きさだけか。**
//
//	#143 で `ovDimFontSize`（文字の図面上の大きさ）は **`CreateLinearDimension` を呼んだ
//	時点のアクティブレイヤの縮尺で焼き付く**（後から縮尺を変えても追随しない）ことが
//	確定した。残った問いは、**寸法規格が「用紙インチ」で持っている他の長さ**——端記号
//	（矢印）の大きさ・補助線の長さと隙間・寸法線の出（overhang）・文字と寸法線の間隔
//	——も同じように焼き付くのか、それとも**描くときに毎回解かれている**のか。
//
//	Findings「Dimensions」はいま「**焼き付くのは文字の大きさだけ**」と【推定】で書いて
//	いる。根拠は「文字が読めない寸法でも寸法線は正常に見えた」という**見た目の印象**
//	だけで、**大きさを突き合わせて測ってはいない**。
//
//	【ヘッダ根拠（`MiniCadCallBacks.h` の `dimStd*` 選択子）】規格の長さは 2 系統ある:
//
//	  ・`double_gs` で「**page inches**」と明記されているもの（-2.0〜2.0）
//	      witGap(1) 補助線と図形の隙間 / witExtend(2) 補助線が寸法線より上へ出る長さ /
//	      minALength(3) 内向き矢印の最小長 / extALength(4) 外向き矢印の長さ /
//	      stackGap(5) / aboveGap(6) 文字が寸法線から浮く距離 / leaderLength(7) /
//	      **overHang(8) 寸法線が補助線より外へ出る量** / markLength(9) /
//	      extendLength(10) / markGap(11) / fixedWitLength(31)
//	  ・`Sint16` で単位の記載が無いもの = **端記号（矢印）の大きさ・幅**
//	      linearASize(22) / linearAWidth(36)（マーカーの大きさは 1/16384 インチの
//	      16 ビット整数。Findings「Attributes and Classes」）
//
//	**この 1 本で確かめること（どれも数値で答えが出る）:**
//
//	  1. 規格の page inch の値を実際に読む（何インチか）。
//	  2. **外接矩形（`GetObjectBounds`）を物差しにして、描かれた線の幾何を測る。**
//	     水平な寸法（測点 (0,y)-(L,y)・オフセット d）なら
//	         出（overhang）    = (幅 - L) / 2
//	         補助線の出       = |上端 - (y + d)|
//	         補助線の隙間     = |下端 - y|
//	     文字は `ovDimShowValue = false` で消してあるので、**測っているのは線だけ**。
//	  3. **同じ幾何の寸法を「1:1 のレイヤで作ったもの」と「1/50 のレイヤで作ったもの」で
//	     作り、同じレイヤ縮尺のもとで突き合わせる。** 等しければ焼き付いていない
//	     （＝描くときに解かれている）。違えば焼き付いている。
//	  4. **レイヤの縮尺を変えて読み直す。** 線の幾何が縮尺ぶん変われば「毎回解かれて
//	     いる」。変わらなければ「焼き付いている」。**`ovDimFontSize` は #143 で
//	     「変わらない」と分かっているので、同じ寸法で両方を並べれば対照になる。**
//	  5. **端記号は `GetMarkerPolys` で図形として取り出せるか**（線では取れる。
//	     Findings「Attributes and Classes」）。取れればその外接矩形が矢印の実寸で、
//	     目視なしで大きさを突き合わせられる。
//	  6. **規格を差し替えると、既にある寸法の線の幾何は変わるか。** 変われば
//	     「規格の値は描くときに引かれている」ことの裏取りになる。
//	  7. **ビューポートの注釈でも同じか。** 1:1 生まれと 1/50 生まれを同じ注釈へ入れ、
//	     線の幾何を突き合わせる（注釈はビューポートの縮尺で描かれる。#143）。
//	  8. `ovDimTextAboveLineInCurrUnits`(43) / `ovDimTextOffsetInCurrUnits`(44) /
//	     `ovDimCust*Wit*`(1236-1239) / `ovDimLeaderLineArrowSize`(1242) は
//	     縮尺で変わるか、そして**書いて直せるか**。
//
//	目視は最後の 1 つだけ（注釈に並べた 2 本の端記号と補助線が同じ大きさに見えるか）。
//	5 が効けば、それも数値で裏が取れる。
//

#include "Probe.h"

#include <cmath>
#include <string>

namespace
{
	const double kDimPglInchToMM = 25.4;

	std::string DimPglNum(double value)
	{
		std::string s = std::to_string(value);
		const std::string::size_type dot = s.find('.');
		if (dot != std::string::npos && s.size() > dot + 5)
			s.erase(dot + 5);
		return s;
	}

	std::string DimPglInt(long long value)
	{
		return std::to_string(value);
	}

	// 型を決め打ちしない読み口（Findings「Dimensions」: ヘッダのコメントの型と実体が
	// 一致しないものがある）。
	std::string DimPglBlockText(const TVariableBlock& v)
	{
		TVariableBlock copy = v;
		Real64 r64 = 0.0;
		if (copy.GetReal64(r64))
			return DimPglNum(r64) + " [Real64]";
		Sint8 s8 = 0;
		if (copy.GetSint8(s8))
			return DimPglInt(s8) + " [Sint8]";
		Uint8 u8 = 0;
		if (copy.GetUint8(u8))
			return DimPglInt(u8) + " [Uint8]";
		Sint16 s16 = 0;
		if (copy.GetSint16(s16))
			return DimPglInt(s16) + " [Sint16]";
		Sint32 s32 = 0;
		if (copy.GetSint32(s32))
			return DimPglInt(s32) + " [Sint32]";
		bool b = false;
		if (copy.GetBoolean(b))
			return std::string(b ? "true" : "false") + " [Boolean]";
		TXString str;
		if (copy.GetTXString(str))
			return "\"" + std::string(static_cast<const char*>(str)) + "\" [TXString]";
		return "(既知のどの型でも読めない。型番号=" +
			   DimPglInt(static_cast<long long>(copy.GetType())) + ")";
	}

	std::string DimPglReadAny(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		return DimPglBlockText(v);
	}

	bool DimPglReadReal(MCObjectHandle h, short selector, double& out)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return false;
		Real64 r64 = 0.0;
		if (!v.GetReal64(r64))
			return false;
		out = r64;
		return true;
	}

	std::string DimPglWriteReal(MCObjectHandle h, short selector, double value)
	{
		TVariableBlock v;
		v = static_cast<Real64>(value);
		const bool ok = gSDK->SetObjectVariable(h, selector, v) != 0;
		return std::string(ok ? "true" : "false") + " → 読み戻し " + DimPglReadAny(h, selector);
	}

	std::string DimPglWriteBool(MCObjectHandle h, short selector, bool value)
	{
		TVariableBlock v;
		v.SetBoolean(value);
		const bool ok = gSDK->SetObjectVariable(h, selector, v) != 0;
		return std::string(ok ? "true" : "false");
	}

	std::string DimPglWriteString(MCObjectHandle h, short selector, const char* value)
	{
		TVariableBlock v;
		v = TXString(value);
		const bool ok = gSDK->SetObjectVariable(h, selector, v) != 0;
		return std::string(ok ? "true" : "false") + " → 読み戻し " + DimPglReadAny(h, selector);
	}

	double DimPglLayerScale(MCObjectHandle layer)
	{
		double_gs scale = 0.0;
		if (layer == nil)
			return 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return static_cast<double>(scale);
	}

	// ------------------------------------------------------------------
	// この調査の主計器: 水平な寸法 1 本の「線の幾何」を外接矩形から割り出す。
	// nominalSpan = 測点間の距離（L）/ nominalY = 測点の y / nominalOffset = d。
	struct DimPglGeometry
	{
		bool ok = false;
		double left = 0.0;
		double top = 0.0;
		double right = 0.0;
		double bottom = 0.0;
		double overHang = 0.0;	// 寸法線が補助線より外へ出る量（片側）
		double witExtend = 0.0; // 補助線が寸法線より先へ出る長さ
		double witGap = 0.0;	// 補助線と測点の隙間
	};

	DimPglGeometry DimPglMeasure(MCObjectHandle h, double nominalSpan, double nominalY,
								 double nominalOffset)
	{
		DimPglGeometry g;
		if (h == nil)
			return g;
		WorldRect bounds;
		if (!gSDK->GetObjectBounds(h, bounds))
			return g;
		g.ok = true;
		g.left = static_cast<double>(bounds.left);
		g.top = static_cast<double>(bounds.top);
		g.right = static_cast<double>(bounds.right);
		g.bottom = static_cast<double>(bounds.bottom);
		// オフセットが正なら寸法線は測点より上（+y）にある。
		const double dimLineY = nominalY + nominalOffset;
		g.overHang = ((g.right - g.left) - nominalSpan) / 2.0;
		if (nominalOffset >= 0.0)
		{
			g.witExtend = g.top - dimLineY;
			g.witGap = g.bottom - nominalY;
		}
		else
		{
			g.witExtend = dimLineY - g.bottom;
			g.witGap = nominalY - g.top;
		}
		return g;
	}

	// 端記号（矢印）の図形を取り出して、その外接矩形の大きさを返す。
	std::string DimPglMarkerText(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		MCObjectHandle startPoly = nil;
		MCObjectHandle endPoly = nil;
		gSDK->GetMarkerPolys(h, startPoly, endPoly);
		std::string out;
		const MCObjectHandle polys[2] = {startPoly, endPoly};
		const char* names[2] = {"始", "終"};
		for (int i = 0; i < 2; ++i)
		{
			out += std::string(i == 0 ? "" : " / ") + names[i] + "=";
			if (polys[i] == nil)
			{
				out += "なし";
				continue;
			}
			WorldRect r;
			if (!gSDK->GetObjectBounds(polys[i], r))
			{
				out += "図形あり（外接矩形が読めない）";
				continue;
			}
			out += "型" + DimPglInt(gSDK->GetObjectTypeN(polys[i])) + " " +
				   DimPglNum(static_cast<double>(r.right) - static_cast<double>(r.left)) + "×" +
				   DimPglNum(static_cast<double>(r.top) - static_cast<double>(r.bottom));
		}
		return out;
	}

	// **1 本の寸法の「いま」を 1 枚のカードとして出す。** 比べるのはこのカード同士。
	void DimPglLogDim(vwprobe::Report& probe, const std::string& label, MCObjectHandle h,
					  double nominalSpan, double nominalY, double nominalOffset)
	{
		if (h == nil)
		{
			probe.log("  " + label + ": (nil)");
			return;
		}
		const DimPglGeometry g = DimPglMeasure(h, nominalSpan, nominalY, nominalOffset);
		probe.log("  " + label + ":");
		if (!g.ok)
		{
			probe.log("     外接矩形が読めない");
		}
		else
		{
			probe.log("     外接矩形 左" + DimPglNum(g.left) + " 上" + DimPglNum(g.top) + " 右" +
					  DimPglNum(g.right) + " 下" + DimPglNum(g.bottom) + "（幅" +
					  DimPglNum(g.right - g.left) + " 高" + DimPglNum(g.top - g.bottom) + "）");
			probe.log("     ★線の幾何: 出(overhang)=" + DimPglNum(g.overHang) + " / 補助線の出=" +
					  DimPglNum(g.witExtend) + " / 補助線の隙間=" + DimPglNum(g.witGap));
		}
		probe.log("     ovDimFontSize=" + DimPglReadAny(h, ovDimFontSize) +
				  "（#143 で「焼き付く」と分かっている対照）");
		probe.log("     端記号の図形(GetMarkerPolys): " + DimPglMarkerText(h));
		probe.log(
			"     ovDimTextAboveLineInCurrUnits(43)=" +
			DimPglReadAny(h, ovDimTextAboveLineInCurrUnits) +
			" / ovDimTextOffsetInCurrUnits(44)=" + DimPglReadAny(h, ovDimTextOffsetInCurrUnits));
		probe.log("     ovDimStartOffsetInCurrUnits(45)=" +
				  DimPglReadAny(h, ovDimStartOffsetInCurrUnits) +
				  " / ovDimWitnessOverride(1235)=" + DimPglReadAny(h, ovDimWitnessOverride));
		probe.log(
			"     ovDimCustStartWitLength(1236)=" + DimPglReadAny(h, ovDimCustStartWitLength) +
			" / CustEndWitLength(1237)=" + DimPglReadAny(h, ovDimCustEndWitLength));
		probe.log(
			"     ovDimCustStartWitOffset(1238)=" + DimPglReadAny(h, ovDimCustStartWitOffset) +
			" / CustEndWitOffset(1239)=" + DimPglReadAny(h, ovDimCustEndWitOffset));
		probe.log(
			"     ovDimLeaderLineArrowSize(1242)=" + DimPglReadAny(h, ovDimLeaderLineArrowSize) +
			" / ArrowWidth(1247)=" + DimPglReadAny(h, ovDimLeaderLineArrowWidth) +
			" / ArrowType(1241)=" + DimPglReadAny(h, ovDimLeaderLineArrowType));
		probe.log("     ovDimStandardName=" + DimPglReadAny(h, ovDimStandardName) +
				  " / ovDimStandard=" + DimPglReadAny(h, ovDimStandard));
	}

	// 2 枚のカードを突き合わせて、答えを 1 行で言い切る。
	void DimPglCompare(vwprobe::Report& probe, const std::string& what, MCObjectHandle a,
					   const std::string& aLabel, MCObjectHandle b, const std::string& bLabel,
					   double nominalSpan, double aY, double bY, double nominalOffset)
	{
		const DimPglGeometry ga = DimPglMeasure(a, nominalSpan, aY, nominalOffset);
		const DimPglGeometry gb = DimPglMeasure(b, nominalSpan, bY, nominalOffset);
		if (!ga.ok || !gb.ok)
		{
			probe.log("★ " + what + ": どちらかの外接矩形が読めないので比べられない");
			return;
		}
		const double dOver = gb.overHang - ga.overHang;
		const double dExt = gb.witExtend - ga.witExtend;
		const double dGap = gb.witGap - ga.witGap;
		const bool same =
			std::fabs(dOver) < 0.001 && std::fabs(dExt) < 0.001 && std::fabs(dGap) < 0.001;
		probe.log("★ " + what + ":");
		probe.log("   " + aLabel + " 出=" + DimPglNum(ga.overHang) +
				  " 補助線の出=" + DimPglNum(ga.witExtend) + " 隙間=" + DimPglNum(ga.witGap));
		probe.log("   " + bLabel + " 出=" + DimPglNum(gb.overHang) +
				  " 補助線の出=" + DimPglNum(gb.witExtend) + " 隙間=" + DimPglNum(gb.witGap));
		if (same)
		{
			probe.log("   → **完全に同じ**（線の幾何は作ったときの縮尺に依らない）");
		}
		else
		{
			probe.log("   → **違う**（差 出=" + DimPglNum(dOver) +
					  " 補助線の出=" + DimPglNum(dExt) + " 隙間=" + DimPglNum(dGap) + "）");
			if (ga.witExtend != 0.0)
				probe.log("      補助線の出の比 = " + DimPglNum(gb.witExtend / ga.witExtend));
			if (ga.overHang != 0.0)
				probe.log("      出の比 = " + DimPglNum(gb.overHang / ga.overHang));
		}
	}

	MCObjectHandle DimPglMakeDim(double x0, double x1, double y, double startOffset, bool showValue)
	{
		MCObjectHandle h = gSDK->CreateLinearDimension(
			WorldPt(static_cast<WorldCoord>(x0), static_cast<WorldCoord>(y)),
			WorldPt(static_cast<WorldCoord>(x1), static_cast<WorldCoord>(y)),
			static_cast<WorldCoord>(startOffset), 0, Vector2(0, 0), 0);
		if (h != nil)
		{
			TVariableBlock v;
			v.SetBoolean(showValue);
			gSDK->SetObjectVariable(h, ovDimShowValue, v);
		}
		return h;
	}

	// 規格 1 つぶんの page inch の値を並べる（何インチか＋その縮尺での図面上の長さ）。
	void DimPglLogStandard(vwprobe::Report& probe, short index, double scale)
	{
		struct Entry
		{
			short selector;
			const char* name;
		};
		const Entry pageInches[] = {
			{dimStdWitGap, "witGap(1) 補助線と図形の隙間"},
			{dimStdWitExtend, "witExtend(2) 補助線が寸法線より出る長さ"},
			{dimStdMinALength, "minALength(3) 内向き矢印の最小長"},
			{dimStdExtALength, "extALength(4) 外向き矢印の長さ"},
			{dimStdAboveGap, "aboveGap(6) 文字が寸法線から浮く距離"},
			{dimStdOverHang, "overHang(8) 寸法線が補助線より外へ出る量"},
			{dimStdFixedWitLength, "fixedWitLength(31) 固定補助線の長さ"},
		};
		const Entry others[] = {
			{dimStdstandardName, "standardName(25)"},
			{dimStdLinearArrowType, "linearArrowType(16) 端記号の様式"},
			{dimStdLinearASize, "linearASize(22) 端記号の大きさ [Sint16]"},
			{dimStdLinearAWidth, "linearAWidth(36) 端記号の幅 [Sint16]"},
			{dimStdLinearArrowVis, "linearArrowVis(33) 端記号の可視"},
			{dimStdWitnessFixed, "witnessFixed(21) 固定補助線の旗"},
			{dimStdOptions, "options(15) ビット旗(128:補助線)"},
		};
		for (const Entry& e : pageInches)
		{
			TVariableBlock v;
			if (!gSDK->GetDimensionStandardVariable(index, e.selector, v))
			{
				probe.log(std::string("     ") + e.name + " = (false)");
				continue;
			}
			Real64 inches = 0.0;
			if (v.GetReal64(inches))
			{
				probe.log(std::string("     ") + e.name + " = " + DimPglNum(inches) +
						  " page inch = 紙の上 " + DimPglNum(inches * kDimPglInchToMM) +
						  "mm → 縮尺 1/" + DimPglNum(scale) + " の図面上では " +
						  DimPglNum(inches * kDimPglInchToMM * scale) + "mm のはず");
			}
			else
			{
				probe.log(std::string("     ") + e.name + " = " + DimPglBlockText(v) +
						  "（Real64 で読めなかった）");
			}
		}
		for (const Entry& e : others)
		{
			TVariableBlock v;
			if (!gSDK->GetDimensionStandardVariable(index, e.selector, v))
			{
				probe.log(std::string("     ") + e.name + " = (false)");
				continue;
			}
			std::string line = std::string("     ") + e.name + " = " + DimPglBlockText(v);
			Sint16 s16 = 0;
			if ((e.selector == dimStdLinearASize || e.selector == dimStdLinearAWidth) &&
				v.GetSint16(s16))
			{
				// マーカーの大きさは 1/16384 インチの 16 ビット整数（Findings
				// 「Attributes and Classes」）。その読みが正しいかをここで確かめる。
				const double asInch = static_cast<double>(s16) / 16384.0;
				line += "（1/16384 インチとして読むと " + DimPglNum(asInch) +
						" インチ = " + DimPglNum(asInch * kDimPglInchToMM) + "mm）";
			}
			probe.log(line);
		}
	}

	// ビューポートのクラスをすべて表示へ戻す（既定では全部消えている。
	// Findings「Viewports」）。
	int DimPglShowAllClasses(MCObjectHandle viewport)
	{
		int shown = 0;
		gSDK->ForEachClass(true,
						   [&](MCObjectHandle cls)
						   {
							   if (cls == nil)
								   return;
							   if (gSDK->SetViewportClassVisibility(
									   viewport, gSDK->GetObjectInternalIndex(cls), 0))
								   ++shown;
						   });
		return shown;
	}
} // namespace

VW_PROBE("dim-page-lengths-and-scale", "寸法の端記号・補助線・間隔は作るときの縮尺で焼き付くか",
		 "外接矩形を物差しに、1:1 生まれと 1/50 生まれの寸法の線の幾何を突き合わせる。"
		 "レイヤ縮尺の変更・規格の差し替え・ビューポートの注釈でも測り直す")
{
	const double kDimPglScale = 50.0;	  // 比べるための縮尺 1/50
	const double kDimPglSpan = 4000.0;	  // 測点間の距離（L）
	const double kDimPglOffset = 800.0;	  // 寸法線のオフセット（d）
	const double kDimPglShortSpan = 30.0; // 矢印が外へ出るほど短い寸法
	const double kDimPglWallEndX = 4000.0;
	const double kDimPglWallThickness = 120.0;

	probe.log("== 0. 前提 ==");
	MCObjectHandle designLayer = gSDK->GetActiveLayer();
	if (designLayer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した");
		return;
	}
	const double scale0 = DimPglLayerScale(designLayer);
	probe.log("アクティブ（デザイン）レイヤの縮尺 = 1/" + DimPglNum(scale0));
	probe.log("測るのは「線だけ」——どの寸法も ovDimShowValue=false で文字を消してある");
	probe.log("（文字を出す 2 本だけは段 6 で作る。目視用）");

	probe.log("");
	probe.log("== 1. いま当たっている規格の page inch の値を読む ==");
	MCObjectHandle probeDim = DimPglMakeDim(0.0, kDimPglSpan, -20000.0, kDimPglOffset, false);
	if (probeDim == nil)
	{
		probe.fail("CreateLinearDimension が nil を返した");
		return;
	}
	short stdIndex = 0;
	{
		TVariableBlock v;
		if (gSDK->GetObjectVariable(probeDim, ovDimStandard, v))
		{
			Sint8 s8 = 0;
			Sint16 s16 = 0;
			if (v.GetSint8(s8))
				stdIndex = static_cast<short>(s8);
			else if (v.GetSint16(s16))
				stdIndex = s16;
		}
	}
	probe.log("作った寸法の規格 index = " + DimPglInt(stdIndex) +
			  " / 名前 = " + DimPglReadAny(probeDim, ovDimStandardName));
	probe.log("   規格 " + DimPglInt(stdIndex) + " の中身:");
	DimPglLogStandard(probe, stdIndex, scale0);

	probe.log("");
	probe.log("== 2. 縮尺 1/" + DimPglNum(scale0) + " のまま 1 組作る（P 群） ==");
	MCObjectHandle dimP = DimPglMakeDim(0.0, kDimPglSpan, 0.0, kDimPglOffset, false);
	MCObjectHandle dimPShort = DimPglMakeDim(0.0, kDimPglShortSpan, -3000.0, kDimPglOffset, false);
	DimPglLogDim(probe, "(P) 長い寸法 L=" + DimPglNum(kDimPglSpan), dimP, kDimPglSpan, 0.0,
				 kDimPglOffset);
	DimPglLogDim(probe,
				 "(P短) 短い寸法 L=" + DimPglNum(kDimPglShortSpan) + "（矢印が外へ出るはず）",
				 dimPShort, kDimPglShortSpan, -3000.0, kDimPglOffset);

	probe.log("");
	probe.log("== 3. レイヤの縮尺を 1/" + DimPglNum(kDimPglScale) + " へ変える ==");
	probe.log("★ ここが第 1 の答え——既にある寸法の線の幾何は縮尺に追随するか");
	gSDK->SetLayerScaleN(designLayer, static_cast<double_param>(kDimPglScale));
	probe.log("SetLayerScaleN 後の縮尺 = 1/" + DimPglNum(DimPglLayerScale(designLayer)));
	probe.log("(a) ResetObject を呼ばずに読み直す:");
	DimPglLogDim(probe, "(P) 縮尺変更後", dimP, kDimPglSpan, 0.0, kDimPglOffset);
	probe.log("(b) ResetObject を呼んでから読み直す:");
	gSDK->ResetObject(dimP);
	gSDK->ResetObject(dimPShort);
	DimPglLogDim(probe, "(P) ResetObject 後", dimP, kDimPglSpan, 0.0, kDimPglOffset);
	DimPglLogDim(probe, "(P短) ResetObject 後", dimPShort, kDimPglShortSpan, -3000.0,
				 kDimPglOffset);
	probe.log("   ※ 段 2 の値と比べる。縮尺ぶん（×" + DimPglNum(kDimPglScale / scale0) +
			  "）大きくなっていれば「描くときに解かれている」。");
	probe.log("   ※ 変わっていなければ「作るときに焼き付いている」。");
	probe.log("   ※ ovDimFontSize は #143 で「変わらない」と分かっている——この 1 本の中で");
	probe.log("   　 「文字は変わらないが線は変わった」が見えれば、それが答えそのもの。");
	probe.log("   ※ 期待値（規格の page inch × 25.4 × 50）は段 1 の各行の末尾にある。");

	probe.log("");
	probe.log("== 4. 1/" + DimPglNum(kDimPglScale) +
			  " のレイヤで新しく作る（Q 群）——生まれの違いを突き合わせる ==");
	MCObjectHandle dimQ = DimPglMakeDim(0.0, kDimPglSpan, 6000.0, kDimPglOffset, false);
	MCObjectHandle dimQShort = DimPglMakeDim(0.0, kDimPglShortSpan, 9000.0, kDimPglOffset, false);
	DimPglLogDim(probe, "(Q) 1/" + DimPglNum(kDimPglScale) + " 生まれ", dimQ, kDimPglSpan, 6000.0,
				 kDimPglOffset);
	DimPglLogDim(probe, "(Q短) 1/" + DimPglNum(kDimPglScale) + " 生まれ", dimQShort,
				 kDimPglShortSpan, 9000.0, kDimPglOffset);
	DimPglCompare(probe,
				  "同じレイヤ（1/" + DimPglNum(kDimPglScale) +
					  "）に居る 2 本: 1:1 生まれ(P) と 1/" + DimPglNum(kDimPglScale) + " 生まれ(Q)",
				  dimP, "(P) 1:1 生まれ  ", dimQ, "(Q) 1/50 生まれ", kDimPglSpan, 0.0, 6000.0,
				  kDimPglOffset);
	DimPglCompare(probe, "短い寸法でも同じか（矢印が外へ出る場合）", dimPShort,
				  "(P短) 1:1 生まれ  ", dimQShort, "(Q短) 1/50 生まれ", kDimPglShortSpan, -3000.0,
				  9000.0, kDimPglOffset);

	probe.log("");
	probe.log("== 5. レイヤの縮尺を 1:1 へ戻して、両方を読み直す ==");
	gSDK->SetLayerScaleN(designLayer, static_cast<double_param>(1.0));
	gSDK->ResetObject(dimP);
	gSDK->ResetObject(dimQ);
	probe.log("いまの縮尺 = 1/" + DimPglNum(DimPglLayerScale(designLayer)));
	DimPglLogDim(probe, "(P) 1:1 に戻した後", dimP, kDimPglSpan, 0.0, kDimPglOffset);
	DimPglLogDim(probe, "(Q) 1:1 に戻した後", dimQ, kDimPglSpan, 6000.0, kDimPglOffset);
	DimPglCompare(probe, "1:1 のもとでの 2 本", dimP, "(P) 1:1 生まれ  ", dimQ, "(Q) 1/50 生まれ",
				  kDimPglSpan, 0.0, 6000.0, kDimPglOffset);
	probe.log("   ※ 段 2 の (P) と同じ値に戻れば「入れ物の縮尺で毎回解かれている」。");
	probe.log("レイヤを 1/" + DimPglNum(kDimPglScale) + " へ戻す（以後の段のため）");
	gSDK->SetLayerScaleN(designLayer, static_cast<double_param>(kDimPglScale));
	gSDK->ResetObject(dimP);
	gSDK->ResetObject(dimQ);

	probe.log("");
	probe.log("== 6. 規格を差し替えたら、既にある寸法の線の幾何は変わるか ==");
	probe.log("★ 変われば「規格の値は寸法の中に写し取られておらず、描くときに引かれている」");
	{
		const char* kDimPglStandards[] = {"Arch", "DIN", "ISO", "JIS"};
		for (const char* name : kDimPglStandards)
		{
			probe.log("");
			probe.log(" 規格 ← \"" + std::string(name) +
					  "\": " + DimPglWriteString(dimQ, ovDimStandardName, name));
			short idx = 0;
			TVariableBlock v;
			if (gSDK->GetObjectVariable(dimQ, ovDimStandard, v))
			{
				Sint8 s8 = 0;
				Sint16 s16 = 0;
				if (v.GetSint8(s8))
					idx = static_cast<short>(s8);
				else if (v.GetSint16(s16))
					idx = s16;
			}
			probe.log("   その規格の page inch（期待値つき）:");
			DimPglLogStandard(probe, idx, kDimPglScale);
			gSDK->ResetObject(dimQ);
			DimPglLogDim(probe, "(Q) 規格 " + std::string(name) + " を当てた後", dimQ, kDimPglSpan,
						 6000.0, kDimPglOffset);
		}
	}

	probe.log("");
	probe.log("== 7. per-object の上書き（ovDimWitnessOverride ＋ ovDimCust*）は効くか ==");
	probe.log("★ もし焼き付いていた場合の「直し方」の候補。単位も同時に分かる");
	if (dimQShort != nil)
	{
		MCObjectHandle dimR = DimPglMakeDim(0.0, kDimPglSpan, 12000.0, kDimPglOffset, false);
		DimPglLogDim(probe, "(R) 素の状態", dimR, kDimPglSpan, 12000.0, kDimPglOffset);
		probe.log(" ovDimWitnessOverride(1235) ← 1（単一のカスタム長）:");
		{
			TVariableBlock v;
			v = static_cast<Sint16>(1);
			const bool ok = gSDK->SetObjectVariable(dimR, ovDimWitnessOverride, v) != 0;
			probe.log("   Sint16 で書く = " + std::string(ok ? "true" : "false") + " → 読み戻し " +
					  DimPglReadAny(dimR, ovDimWitnessOverride));
			if (!ok)
			{
				TVariableBlock v8;
				v8.SetUint8(1);
				probe.log("   Uint8 で書き直す = " +
						  std::string(gSDK->SetObjectVariable(dimR, ovDimWitnessOverride, v8)
										  ? "true"
										  : "false") +
						  " → 読み戻し " + DimPglReadAny(dimR, ovDimWitnessOverride));
			}
		}
		probe.log(
			" ovDimCustStartWitLength(1236) ← 2000（図面上の 2000 か、紙の 2000 かを見る）: " +
			DimPglWriteReal(dimR, ovDimCustStartWitLength, 2000.0));
		gSDK->ResetObject(dimR);
		DimPglLogDim(probe, "(R) 上書きを書いた後", dimR, kDimPglSpan, 12000.0, kDimPglOffset);
		probe.log(" ovDimTextAboveLineInCurrUnits(43) ← 500: " +
				  DimPglWriteReal(dimR, ovDimTextAboveLineInCurrUnits, 500.0));
		gSDK->ResetObject(dimR);
		DimPglLogDim(probe, "(R) 文字の浮きを書いた後", dimR, kDimPglSpan, 12000.0, kDimPglOffset);
	}

	probe.log("");
	probe.log("== 8. 断面に写すものを用意する（壁） ==");
	{
		MCObjectHandle wall = gSDK->CreateWall(
			WorldPt(static_cast<WorldCoord>(0), static_cast<WorldCoord>(0)),
			WorldPt(static_cast<WorldCoord>(kDimPglWallEndX), static_cast<WorldCoord>(0)),
			static_cast<WorldCoord>(kDimPglWallThickness));
		if (wall == nil)
		{
			probe.log("CreateWall が nil を返した（絵の当て先が無いだけ。続ける）");
		}
		else
		{
			// CreateWall は壁厚しか取らず、新規の空図面では高さ 0（Findings「Walls」）。
			VectorWorks::SStoryObjectData bottomData;
			bottomData.fBound = VectorWorks::eStoryObjectBound_LayerElevation;
			bottomData.fBoundStory = 0;
			bottomData.fOffset = 0.0;
			VectorWorks::SStoryObjectData topData = bottomData;
			topData.fOffset = 2800.0;
			gSDK->SetWallOverallHeights(wall, bottomData, topData);
			gSDK->ResetObject(wall);
			probe.log("壁 (0,0)-(4000,0) 厚 120mm・高さ 2800mm を置いた");
		}
	}

	probe.log("");
	probe.log("== 9. 文字を出す 2 本を用意する（目視用。大きさは紙の 6pt に揃える） ==");
	const double kDimPglWantedFont = 6.0 * 25.4 / 72.0 * kDimPglScale; // 105.833
	MCObjectHandle dimVis50 = DimPglMakeDim(0.0, kDimPglSpan, 0.0, 2400.0, true);
	probe.log("(V50) 1/" + DimPglNum(kDimPglScale) +
			  " 生まれ・文字あり: ovDimFontSize=" + DimPglReadAny(dimVis50, ovDimFontSize));

	probe.log("");
	probe.log("== 10. シートレイヤを作る（アクティブが 1:1 へ移る） ==");
	MCObjectHandle sheet = gSDK->CreateLayer("寸法の用紙長さ調査シート", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("シートレイヤを作れなかった");
		return;
	}
	MCObjectHandle activeNow = gSDK->GetActiveLayer();
	probe.log("いまのアクティブはシートレイヤか = " +
			  std::string(activeNow == sheet ? "はい" : "いいえ") + " / 縮尺 = 1/" +
			  DimPglNum(DimPglLayerScale(activeNow)));

	probe.log("");
	probe.log("== 11. 1:1（シートレイヤ）で作る（S 群。注釈へ入れるぶん） ==");
	MCObjectHandle dimS = DimPglMakeDim(0.0, kDimPglSpan, -6000.0, kDimPglOffset, false);
	MCObjectHandle dimVis1 = DimPglMakeDim(0.0, kDimPglSpan, 0.0, 4000.0, true);
	DimPglLogDim(probe, "(S) 1:1 生まれ・文字なし", dimS, kDimPglSpan, -6000.0, kDimPglOffset);
	probe.log("(V1) 1:1 生まれ・文字あり: ovDimFontSize=" + DimPglReadAny(dimVis1, ovDimFontSize));
	probe.log("(V1) の文字だけ直す（#143 の指針）: ovDimFontSize ← " +
			  DimPglNum(kDimPglWantedFont) + ": " +
			  DimPglWriteReal(dimVis1, ovDimFontSize, kDimPglWantedFont));
	probe.log("   ※ これで 2 本の**文字**の大きさは揃った。揃わないものが残るかが問い。");

	probe.log("");
	probe.log("== 12. 1/" + DimPglNum(kDimPglScale) + " の平面ビューポートの注釈へ入れる ==");
	MCObjectHandle planVP = gSDK->CreateViewport(sheet);
	if (planVP == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	probe.log("作った直後の ovViewportScale(1003) = " + DimPglReadAny(planVP, ovViewportScale));
	probe.log("ovViewportScale ← " + DimPglNum(kDimPglScale) + ": " +
			  DimPglWriteReal(planVP, ovViewportScale, kDimPglScale));
	gSDK->SetViewportLayerVisibility(planVP, designLayer, 0);
	probe.log("クラスを全部表示へ戻した件数 = " + DimPglInt(DimPglShowAllClasses(planVP)));
	gSDK->UpdateViewport(planVP);

	probe.log("注釈へ移す:");
	probe.log("  (Q) 1/50 生まれ・文字なし = " +
			  std::string(gSDK->AddViewportAnnotationObject(planVP, dimQ) ? "true" : "false"));
	probe.log("  (S) 1:1 生まれ・文字なし  = " +
			  std::string(gSDK->AddViewportAnnotationObject(planVP, dimS) ? "true" : "false"));
	probe.log("  (V50) 1/50 生まれ・文字あり = " +
			  std::string(gSDK->AddViewportAnnotationObject(planVP, dimVis50) ? "true" : "false"));
	probe.log("  (V1) 1:1 生まれ・文字あり（文字だけ直したもの）= " +
			  std::string(gSDK->AddViewportAnnotationObject(planVP, dimVis1) ? "true" : "false"));
	probe.log("クラスを全部表示へ戻し直した件数 = " + DimPglInt(DimPglShowAllClasses(planVP)));
	gSDK->UpdateViewport(planVP);

	DimPglLogDim(probe, "(Q) 注釈へ移した後", dimQ, kDimPglSpan, 6000.0, kDimPglOffset);
	DimPglLogDim(probe, "(S) 注釈へ移した後", dimS, kDimPglSpan, -6000.0, kDimPglOffset);
	DimPglCompare(probe,
				  "同じ注釈（ビューポート 1/" + DimPglNum(kDimPglScale) +
					  "）の中の 2 本: 1/50 生まれ(Q) と 1:1 生まれ(S)",
				  dimQ, "(Q) 1/50 生まれ", dimS, "(S) 1:1 生まれ  ", kDimPglSpan, 6000.0, -6000.0,
				  kDimPglOffset);
	probe.log("   ※ 同じなら、注釈でも線の幾何はビューポートの縮尺で解かれている");
	probe.log("   　 （＝直すのは ovDimFontSize だけでよい）。");

	probe.log("");
	probe.log("== 13. ビューポートの縮尺を変えたら、注釈の中の線の幾何は動くか ==");
	probe.log("ovViewportScale ← 100: " + DimPglWriteReal(planVP, ovViewportScale, 100.0));
	gSDK->UpdateViewport(planVP);
	DimPglLogDim(probe, "(Q) VP 1/100 にした後", dimQ, kDimPglSpan, 6000.0, kDimPglOffset);
	DimPglLogDim(probe, "(S) VP 1/100 にした後", dimS, kDimPglSpan, -6000.0, kDimPglOffset);
	probe.log("   ※ #143 では ovDimFontSize がここで 2 倍になった。線の幾何も 2 倍か。");
	probe.log("元に戻す: ovViewportScale ← " + DimPglNum(kDimPglScale) + ": " +
			  DimPglWriteReal(planVP, ovViewportScale, kDimPglScale));
	gSDK->UpdateViewport(planVP);
	DimPglLogDim(probe, "(Q) 戻した後", dimQ, kDimPglSpan, 6000.0, kDimPglOffset);
	DimPglLogDim(probe, "(S) 戻した後", dimS, kDimPglSpan, -6000.0, kDimPglOffset);

	probe.log("");
	probe.log("== まとめて読むための控え ==");
	probe.log("答えの読み方（この順に見れば決まる）:");
	probe.log(" 1) 段 3: レイヤの縮尺を変えたら線の幾何は動いたか（ovDimFontSize は動かない）");
	probe.log(" 2) 段 4: 同じレイヤに居る「1:1 生まれ」と「1/50 生まれ」は同じか");
	probe.log(" 3) 段 6: 規格を差し替えたら線の幾何は変わったか");
	probe.log(" 4) 段 12: 注釈の中でも 2 本は同じか");
	probe.log(" どれも「動く／同じ／変わる」なら、**焼き付いているのは文字の大きさだけ**。");

	probe.log("");
	probe.log("== 目で見ないと分からないこと（利用者へ。1 つだけ） ==");
	probe.log("シートレイヤ「寸法の用紙長さ調査シート」のビューポートを見てほしい。");
	probe.log("壁の上のほうに、文字付きの寸法が 2 本ある（どちらも 4000 と出るはず）。");
	probe.log("**その 2 本の「端記号（矢印やスラッシュ）」と「補助線」の大きさは、");
	probe.log("  同じに見えるか、それとも片方だけ極端に小さい（大きい）か。**");
	probe.log("（文字の大きさはこちらで揃えてあるので、揃っていないものがあればそれが答え）");
}
