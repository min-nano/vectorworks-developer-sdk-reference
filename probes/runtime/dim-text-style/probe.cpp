//
//	probes/runtime/dim-text-style/probe.cpp
//
//	[issue #157] 寸法の文字スタイル——2 巡目。大きさが何で決まるかだけを確かめる。
//
//	1 巡目で決まったこと（絵つきで確定済み。Findings へ反映済み）:
//	  ・規格の文字スタイルは dimStdTextStyle(51) で読める（Sint32 の ref number）。
//	  ・〈クラスの文字スタイル〉のままだと、ovDimFontSize を書いても注釈で値が出ない。
//	  ・SetTextStyleRef で明示すると出る。連続寸法へ繋いでも残る。
//	  ・ovDimTextStyle(1248) へ番号を書く道は、読み戻しが SetTextStyleRef と完全に
//	    同一になるのに絵が出ない（＝読み戻しでは見分けられない）。
//
//	残っているのは 1 つだけ——**文字スタイルを明示したあと、大きさは何が決めるか**。
//	1 巡目の絵では、ovDimFontSize が同じ 264.5833 の 2 本（直線寸法と連続寸法の中身）が
//	違う大きさで出ていた。ここを分けるため、**ovDimFontSize だけが違う 3 本**を
//	同じ注釈へならべる。
//
//	1 巡目の絵を濁らせた原因（デザインレイヤの寸法がビューポートに写り込んで注釈の
//	寸法と重なっていた）も潰してある——このプローブはデザインレイヤを表示しない。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// 利用側と同じ 1/125。紙で 6pt にしたいなら ovDimFontSize = 6 × 25.4/72 × 125。
	const double kProbeDimVpScale = 125.0;

	// 文字スタイルの大きさ。**ovTextStyleSize の単位はインチ**（1 巡目で確定）。
	// 6 インチのままにしてあるのは、これを当てただけの E1 が注釈で見える大きさ
	// （152.4 / 125 = 1.22mm ≒ 3.5pt）になり、比べる相手として使えるため。
	const double kProbeDimStyleInch = 6.0;

	double ProbeDimPaperPtToFontSize(double paperPt)
	{
		return paperPt * 25.4 / 72.0 * kProbeDimVpScale;
	}

	std::string ProbeDimBool(bool value)
	{
		return value ? std::string("true") : std::string("false");
	}

	std::string ProbeDimNum(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		return std::string(buffer);
	}

	std::string ProbeDimInt(Sint32 value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(value));
		return std::string(buffer);
	}

	std::string ProbeDimIndexName(InternalIndex index)
	{
		if (index == 0)
			return std::string("(なし)");
		TXString name;
		gSDK->InternalIndexToNameN(index, name);
		const std::string text(static_cast<const char*>(name));
		if (text.empty())
			return std::string("(名前なし #") + ProbeDimInt(static_cast<Sint32>(index)) + ")";
		return text;
	}

	bool ProbeDimGetReal(MCObjectHandle handle, short selector, double& out)
	{
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(handle, selector, block))
			return false;
		Real64 value = 0.0;
		if (!block.GetReal64(value))
			return false;
		out = value;
		return true;
	}

	bool ProbeDimGetSint32(MCObjectHandle handle, short selector, Sint32& out)
	{
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(handle, selector, block))
			return false;
		Sint32 value = 0;
		if (!block.GetSint32(value))
			return false;
		out = value;
		return true;
	}

	bool ProbeDimSetReal(MCObjectHandle handle, short selector, double value)
	{
		TVariableBlock block;
		block = static_cast<Real64>(value);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	bool ProbeDimSetSint32(MCObjectHandle handle, short selector, Sint32 value)
	{
		TVariableBlock block;
		block = static_cast<Sint32>(value);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	bool ProbeDimSetBoolean(MCObjectHandle handle, short selector, bool value)
	{
		// TVariableBlock に SetBoolean は無い（Findings「読み戻すときの型」）。
		TVariableBlock block;
		block = static_cast<Boolean>(value ? 1 : 0);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	void ProbeDimDump(vwprobe::Report& probe, const std::string& tag, MCObjectHandle dim)
	{
		std::string line = "    " + tag + ": ";
		if (dim == nil)
		{
			probe.log(line + "(nil)");
			return;
		}
		line += "型=" + ProbeDimInt(static_cast<Sint32>(gSDK->GetObjectTypeN(dim)));
		line += " クラス由来=" + ProbeDimBool(gSDK->GetTextStyleByClass(dim));
		const InternalIndex styleRef = gSDK->GetTextStyleRef(dim);
		line += " GetTextStyleRef=" + ProbeDimInt(static_cast<Sint32>(styleRef));
		line += "(" + ProbeDimIndexName(styleRef) + ")";

		Sint32 ovStyle = 0;
		line += ProbeDimGetSint32(dim, ovDimTextStyle, ovStyle)
					? (" ovDimTextStyle=" + ProbeDimInt(ovStyle))
					: std::string(" ovDimTextStyle=(読めず)");

		double fontSize = 0.0;
		line += ProbeDimGetReal(dim, ovDimFontSize, fontSize)
					? (" ovDimFontSize=" + ProbeDimNum(fontSize))
					: std::string(" ovDimFontSize=(読めず)");

		double pointSize = 0.0;
		if (ProbeDimGetReal(dim, ovDimTextSizeInPoints, pointSize))
			line += " ovDimTextSizeInPoints=" + ProbeDimNum(pointSize);

		probe.log(line);
	}

	MCObjectHandle ProbeDimMake(double y, double width)
	{
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(0.0, y), WorldPt(width, y), 0.0,
														 0.0, Vector2(0.0, 0.0), 0);
		if (dim != nil)
			ProbeDimSetBoolean(dim, ovDimShowValue, true);
		return dim;
	}

	// 中の型 63 を全部出す（連続寸法の中身を読むため）。
	void ProbeDimDumpMembers(vwprobe::Report& probe, const std::string& tag, MCObjectHandle chain)
	{
		int found = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(chain); member != nil;
			 member = gSDK->NextObject(member))
		{
			const short memberType = gSDK->GetObjectTypeN(member);
			if (memberType == 0) // kTermNode（終端）
				break;
			if (memberType == 63)
				ProbeDimDump(probe, tag + " 中の直線寸法 #" + ProbeDimInt(++found), member);
		}
		if (found == 0)
			probe.log("    " + tag + " 中に型 63 が見つからなかった");
	}
} // namespace

VW_PROBE("dim-text-style", "寸法の文字スタイル 2 巡目——大きさを決めているのは何か",
		 "文字スタイルを明示した寸法を、ovDimFontSize だけ変えて 3 本ならべる")
{
	probe.log("=== [#157] 寸法の文字スタイル（2 巡目） ===");
	probe.log("新規の空図面で走らせる前提。図面は壊れる。");
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【1】文字スタイルを作る（ovTextStyleSize の単位はインチ）");
	MCObjectHandle textStyle = gSDK->CreateTextStyleResource("プローブ文字スタイル");
	if (textStyle == nil)
	{
		probe.fail("CreateTextStyleResource が nil を返した");
		return;
	}
	const InternalIndex textStyleRef = gSDK->GetObjectInternalIndex(textStyle);
	ProbeDimSetReal(textStyle, ovTextStyleSize, kProbeDimStyleInch);
	{
		double size = 0.0;
		ProbeDimGetReal(textStyle, ovTextStyleSize, size);
		probe.log("    番号=" + ProbeDimInt(static_cast<Sint32>(textStyleRef)) +
				  " 名前=" + ProbeDimIndexName(textStyleRef) +
				  " ovTextStyleSize=" + ProbeDimNum(size) + " インチ");
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【2】シートレイヤと 1/125 の平面ビューポートを作る");
	probe.log(
		"    ※ デザインレイヤは**表示しない**（1 巡目はここが写り込んで注釈の寸法と重なった）");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("プローブ 157 の 2", kLayerSheet);
	if (sheetLayer == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	MCObjectHandle viewport = gSDK->CreateViewport(sheetLayer);
	if (viewport == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	ProbeDimSetReal(viewport, ovViewportScale, kProbeDimVpScale);
	ProbeDimSetSint32(viewport, ovViewportRenderType, static_cast<Sint32>(renderFinalHiddenLine));
	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	{
		double scale = 0.0;
		ProbeDimGetReal(viewport, ovViewportScale, scale);
		probe.log("    ビューポートの縮尺（読み戻し）=" + ProbeDimNum(scale));
	}
	probe.log("");

	// -----------------------------------------------------------------
	const double fs6pt = ProbeDimPaperPtToFontSize(6.0);
	const double fs12pt = ProbeDimPaperPtToFontSize(12.0);
	probe.log(
		"【3】注釈へ 5 行。E1〜E3 は**文字スタイルを当てたあと ovDimFontSize だけを変えてある**");
	probe.log("    紙 6pt = " + ProbeDimNum(fs6pt) + " / 紙 12pt = " + ProbeDimNum(fs12pt));
	probe.log("");

	MCObjectHandle rows[5] = {nil, nil, nil, nil, nil};

	probe.log("  E1（長さ 1000）: SetTextStyleRef だけ（ovDimFontSize は触らない）");
	rows[0] = ProbeDimMake(0.0, 1000.0);
	gSDK->SetTextStyleRef(rows[0], textStyleRef);
	ProbeDimDump(probe, "E1", rows[0]);

	probe.log("  E2（長さ 2000）: SetTextStyleRef → ovDimFontSize = 紙 6pt");
	rows[1] = ProbeDimMake(-1500.0, 2000.0);
	gSDK->SetTextStyleRef(rows[1], textStyleRef);
	ProbeDimSetReal(rows[1], ovDimFontSize, fs6pt);
	ProbeDimDump(probe, "E2", rows[1]);

	probe.log("  E3（長さ 3000）: SetTextStyleRef → ovDimFontSize = 紙 12pt（E2 のちょうど 2 倍）");
	rows[2] = ProbeDimMake(-3000.0, 3000.0);
	gSDK->SetTextStyleRef(rows[2], textStyleRef);
	ProbeDimSetReal(rows[2], ovDimFontSize, fs12pt);
	ProbeDimDump(probe, "E3", rows[2]);

	probe.log("  E4（長さ 4000 + 5000 の連続寸法）: 繋ぐ前に両方へ SetTextStyleRef → 紙 6pt（E2 "
			  "と同じ）");
	{
		const double chainY = -4500.0;
		MCObjectHandle leftDim = gSDK->CreateLinearDimension(
			WorldPt(0.0, chainY), WorldPt(4000.0, chainY), 0.0, 0.0, Vector2(0.0, 0.0), 0);
		MCObjectHandle rightDim = gSDK->CreateLinearDimension(
			WorldPt(4000.0, chainY), WorldPt(9000.0, chainY), 0.0, 0.0, Vector2(0.0, 0.0), 0);
		if (leftDim == nil || rightDim == nil)
			probe.fail("連続寸法のもとになる直線寸法を作れなかった");
		else
		{
			ProbeDimSetBoolean(leftDim, ovDimShowValue, true);
			ProbeDimSetBoolean(rightDim, ovDimShowValue, true);
			gSDK->SetTextStyleRef(leftDim, textStyleRef);
			gSDK->SetTextStyleRef(rightDim, textStyleRef);
			ProbeDimSetReal(leftDim, ovDimFontSize, fs6pt);
			ProbeDimSetReal(rightDim, ovDimFontSize, fs6pt);
			MCObjectHandle chain = gSDK->CreateChainDimension(leftDim, rightDim);
			if (chain == nil)
				probe.fail("CreateChainDimension が nil を返した");
			else
			{
				rows[3] = chain;
				ProbeDimDump(probe, "E4 連続寸法そのもの", chain);
				ProbeDimDumpMembers(probe, "E4", chain);
			}
		}
	}

	probe.log("  E5（長さ 6000）: 対照。〈クラスの文字スタイル〉のまま ovDimFontSize = 紙 6pt");
	rows[4] = ProbeDimMake(-6000.0, 6000.0);
	ProbeDimSetReal(rows[4], ovDimFontSize, fs6pt);
	ProbeDimDump(probe, "E5", rows[4]);
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【4】注釈へ移し、移った後にもう一度読む（1 巡目で読み落とした連続寸法の中身も）");
	for (int i = 0; i < 5; ++i)
	{
		if (rows[i] == nil)
		{
			probe.log("    E" + ProbeDimInt(i + 1) + ": 作れていないので移せない");
			continue;
		}
		const Boolean moved = gSDK->AddViewportAnnotationObject(viewport, rows[i]);
		probe.log("    E" + ProbeDimInt(i + 1) +
				  ": AddViewportAnnotationObject=" + ProbeDimBool(moved != 0));
		ProbeDimDump(probe, "E" + ProbeDimInt(i + 1) + " 移した後", rows[i]);
		if (gSDK->GetObjectTypeN(rows[i]) == 86)
			ProbeDimDumpMembers(probe, "E" + ProbeDimInt(i + 1) + " 移した後", rows[i]);
	}

	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	probe.log("    クラスを全部表示へ戻して再更新した。");
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("=== 目で見ないと分からないこと（このログには写らない） ===");
	probe.log("シートレイヤ「プローブ 157 の 2」の 1/125 のビューポートに、上から");
	probe.log("  E1 = 1,000 / E2 = 2,000 / E3 = 3,000 / E4 = 4,000 と 5,000 / E5 = 6,000");
	probe.log("がならびます（E5 は 1 巡目と同じで、出ない見込みの対照です）。");
	probe.log("伺いたいのは **数字の大きさ** だけで、2 つあります:");
	probe.log("  (1) 1,000 → 2,000 → 3,000 と、下へ行くほど数字が大きくなっていますか？");
	probe.log("      （3 つとも同じ大きさなら「同じ」と教えてください。）");
	probe.log("  (2) E4 の 4,000 / 5,000 は、E2 の 2,000 と同じ大きさですか？ 違いますか？");
	probe.log("この 2 つで「明示した文字スタイルと ovDimFontSize "
			  "のどちらが大きさを決めるか」が決まります。");
}
