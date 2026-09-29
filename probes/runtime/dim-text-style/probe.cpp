//
//	probes/runtime/dim-text-style/probe.cpp
//
//	[issue #157] 寸法の文字スタイルを実測する。
//
//	利用側（断面ビューポートの注釈に置いた連続寸法）で、ovDimFontSize を
//	ビューポートの縮尺（1/125 → 264.583mm）で書いたのに値が出なかった。OIP の
//	「文字 → スタイル」は〈クラスの文字スタイル〉で、そこで明示的に文字スタイルを
//	選ぶと値が出た。確かめるのは 4 つ:
//
//	  1. 寸法規格が持つ文字スタイルを読めるか（dimStdTextStyle = 51）。
//	  2. 寸法オブジェクト（型 63）へ文字スタイルを当てられるか
//	     （SetTextStyleRef / ovDimTextStyle = 1248）。連続寸法へ繋いだ後も残るか。
//	  3. 〈クラスの文字スタイル〉は、いつ・何を基準に解かれるか。
//	  4. 文字スタイルを明示したとき ovDimFontSize はどうなるか。
//
//	1/125 の平面ビューポートの注釈へ、作り方の違う寸法を 6 行ならべて置く。
//	どの行に値が出たかは目視でしか分からないので、利用者に尋ねる。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// 紙で見せたい大きさと、利用側と同じビューポートの縮尺。
	const double kProbeDimPaperPt = 6.0;
	const double kProbeDimVpScale = 125.0;
	const double kProbeDimDesignScale = 50.0;

	// 紙で 6pt に見せるための ovDimFontSize（Findings「注釈へ寸法を置くときの作り方」）。
	double ProbeDimWantedFontSize()
	{
		return kProbeDimPaperPt * 25.4 / 72.0 * kProbeDimVpScale;
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

	// 名前付きリソースの番号 → 名前。0 は「無し」。
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

	// TVariableBlock を「型を決め打ちせずに」読んで文字列にする
	// （Findings「読み戻すときの型」——ヘッダのコメントと実体が食い違うものがある）。
	std::string ProbeDimVarText(const TVariableBlock& block)
	{
		std::string out =
			std::string("type=") + ProbeDimInt(static_cast<Sint32>(block.GetType())) + " ";
		TXString asString;
		Sint32 asSint32 = 0;
		Real64 asReal = 0.0;
		Sint16 asSint16 = 0;
		Sint8 asSint8 = 0;
		if (block.GetTXString(asString))
			out += std::string("\"") + static_cast<const char*>(asString) + "\"";
		else if (block.GetSint32(asSint32))
			out += ProbeDimInt(asSint32);
		else if (block.GetReal64(asReal))
			out += ProbeDimNum(asReal);
		else if (block.GetSint16(asSint16))
			out += ProbeDimInt(static_cast<Sint32>(asSint16));
		else if (block.GetSint8(asSint8))
			out += ProbeDimInt(static_cast<Sint32>(asSint8));
		else
			out += "(読めず)";
		return out;
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

	// 寸法 1 本の「文字まわり」の全部を 1 行に出す。
	void ProbeDimDump(vwprobe::Report& probe, const std::string& tag, MCObjectHandle dim)
	{
		std::string line = "    " + tag + ": ";
		if (dim == nil)
		{
			probe.log(line + "(nil)");
			return;
		}
		line += "型=" + ProbeDimInt(static_cast<Sint32>(gSDK->GetObjectTypeN(dim)));
		line += " 文字スタイルはクラス由来=" + ProbeDimBool(gSDK->GetTextStyleByClass(dim));
		const InternalIndex styleRef = gSDK->GetTextStyleRef(dim);
		line += " GetTextStyleRef=" + ProbeDimInt(static_cast<Sint32>(styleRef));
		line += "(" + ProbeDimIndexName(styleRef) + ")";

		Sint32 ovStyle = 0;
		if (ProbeDimGetSint32(dim, ovDimTextStyle, ovStyle))
			line += " ovDimTextStyle=" + ProbeDimInt(ovStyle);
		else
			line += " ovDimTextStyle=(読めず)";

		double fontSize = 0.0;
		if (ProbeDimGetReal(dim, ovDimFontSize, fontSize))
			line += " ovDimFontSize=" + ProbeDimNum(fontSize);
		else
			line += " ovDimFontSize=(読めず)";

		double pointSize = 0.0;
		if (ProbeDimGetReal(dim, ovDimTextSizeInPoints, pointSize))
			line += " ovDimTextSizeInPoints=" + ProbeDimNum(pointSize);

		probe.log(line);
	}

	// 測る寸法を 1 本作る（値を出す設定まで）。
	MCObjectHandle ProbeDimMake(double y, double width)
	{
		const WorldPt first(0.0, y);
		const WorldPt second(width, y);
		MCObjectHandle dim =
			gSDK->CreateLinearDimension(first, second, 0.0, 0.0, Vector2(0.0, 0.0), 0);
		if (dim != nil)
			ProbeDimSetBoolean(dim, ovDimShowValue, true);
		return dim;
	}
} // namespace

VW_PROBE("dim-text-style", "寸法の文字スタイル（規格・クラス・明示指定）を実測する",
		 "規格の文字スタイルを読み、寸法へ当て、1/125 の注釈へ 6 行ならべて値が出るかを見る")
{
	probe.log("=== [#157] 寸法の文字スタイル ===");
	probe.log("新規の空図面で走らせる前提。図面は壊れる。");
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【1】寸法規格が持つ文字スタイルを読む（dimStdTextStyle = 51）");
	probe.log("  index / 規格名 / dimStdTextStyle / その番号が指す名前");
	{
		const short kProbeDimStdIndices[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, -1, -2, -3, -4, -5};
		for (size_t i = 0; i < sizeof(kProbeDimStdIndices) / sizeof(kProbeDimStdIndices[0]); ++i)
		{
			const short index = kProbeDimStdIndices[i];
			std::string line = "    " + ProbeDimInt(static_cast<Sint32>(index)) + ": ";

			TVariableBlock nameBlock;
			if (gSDK->GetDimensionStandardVariable(index, dimStdstandardName, nameBlock))
				line += "名前=" + ProbeDimVarText(nameBlock);
			else
			{
				probe.log(line + "GetDimensionStandardVariable(dimStdstandardName) が false（この "
								 "index は無い）");
				continue;
			}

			TVariableBlock styleBlock;
			if (gSDK->GetDimensionStandardVariable(index, dimStdTextStyle, styleBlock))
			{
				line += " / 文字スタイル=" + ProbeDimVarText(styleBlock);
				Sint32 styleRef = 0;
				if (styleBlock.GetSint32(styleRef))
					line +=
						" / 指す名前=" + ProbeDimIndexName(static_cast<InternalIndex>(styleRef));
			}
			else
				line += " / 文字スタイル=(GetDimensionStandardVariable が false)";

			probe.log(line);
		}
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【2】文字スタイルのリソースを作る（CreateTextStyleResource）");
	MCObjectHandle textStyle = gSDK->CreateTextStyleResource("プローブ 6pt");
	if (textStyle == nil)
	{
		probe.fail("CreateTextStyleResource が nil を返した——以降の当て込みが確かめられない");
		return;
	}
	const InternalIndex textStyleRef = gSDK->GetObjectInternalIndex(textStyle);
	{
		double sizeBefore = 0.0;
		const bool okBefore = ProbeDimGetReal(textStyle, ovTextStyleSize, sizeBefore);
		const bool wrote = ProbeDimSetReal(textStyle, ovTextStyleSize, kProbeDimPaperPt);
		double sizeAfter = 0.0;
		const bool okAfter = ProbeDimGetReal(textStyle, ovTextStyleSize, sizeAfter);
		probe.log("    番号=" + ProbeDimInt(static_cast<Sint32>(textStyleRef)) +
				  " 名前=" + ProbeDimIndexName(textStyleRef));
		probe.log("    ovTextStyleSize: 作った直後=" +
				  (okBefore ? ProbeDimNum(sizeBefore) : std::string("(読めず)")) +
				  " / 6 を書いた=" + ProbeDimBool(wrote) +
				  " / 読み戻し=" + (okAfter ? ProbeDimNum(sizeAfter) : std::string("(読めず)")));
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【3】クラスの文字スタイル（GetClUseTextStyle / GetClTextStyleRef）");
	const InternalIndex activeClass = gSDK->GetActiveClass();
	probe.log("    アクティブクラス=" + ProbeDimInt(static_cast<Sint32>(activeClass)) + "(" +
			  ProbeDimIndexName(activeClass) + ")");
	probe.log("    はじめ: use=" + ProbeDimBool(gSDK->GetClUseTextStyle(activeClass)) +
			  " ref=" + ProbeDimInt(static_cast<Sint32>(gSDK->GetClTextStyleRef(activeClass))));
	// まず「クラスに文字スタイルが無い」状態を作る。
	gSDK->SetClUseTextStyle(activeClass, false);
	probe.log("    use=false にした: use=" + ProbeDimBool(gSDK->GetClUseTextStyle(activeClass)));
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【4】デザインレイヤ（1/50）で、クラスに文字スタイルが無い状態の寸法を作る");
	MCObjectHandle designLayer = gSDK->GetActiveLayer();
	if (designLayer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した");
		return;
	}
	gSDK->SetLayerScaleN(designLayer, kProbeDimDesignScale);
	{
		double scale = 0.0;
		gSDK->GetLayerScaleN(designLayer, scale);
		probe.log("    デザインレイヤの縮尺=" + ProbeDimNum(scale));
	}
	MCObjectHandle dimLive = ProbeDimMake(0.0, 3000.0);
	ProbeDimDump(probe, "クラスに文字スタイル無しで作った直後", dimLive);

	probe.log("  クラスへ文字スタイルを与える（SetClUseTextStyle / SetClTextStyleRef）");
	gSDK->SetClUseTextStyle(activeClass, true);
	gSDK->SetClTextStyleRef(activeClass, textStyleRef);
	probe.log("    読み戻し: use=" + ProbeDimBool(gSDK->GetClUseTextStyle(activeClass)) +
			  " ref=" + ProbeDimInt(static_cast<Sint32>(gSDK->GetClTextStyleRef(activeClass))) +
			  "(" + ProbeDimIndexName(gSDK->GetClTextStyleRef(activeClass)) + ")");
	ProbeDimDump(probe, "同じ寸法を、クラスへ与えた後にもう一度読む", dimLive);
	gSDK->ResetObject(dimLive);
	ProbeDimDump(probe, "さらに ResetObject した後", dimLive);
	probe.log(
		"  ↑ ここが変わらなければ「クラスの文字スタイルは寸法側に写らない＝描くたびに解かれる」。");
	probe.log("");

	MCObjectHandle dimDesign = ProbeDimMake(-3000.0, 3000.0);
	ProbeDimDump(probe, "1/50 生まれ（クラスに文字スタイルがある状態で作った）", dimDesign);
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【5】シートレイヤ（1:1）を作る——ここでアクティブレイヤが移る");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("プローブ 157", kLayerSheet);
	if (sheetLayer == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	{
		double scale = 0.0;
		gSDK->GetLayerScaleN(gSDK->GetActiveLayer(), scale);
		probe.log("    いまのアクティブレイヤの縮尺=" + ProbeDimNum(scale) +
				  "（1 ならシートレイヤ）");
	}
	MCObjectHandle dimSheet = ProbeDimMake(-6000.0, 3000.0);
	ProbeDimDump(probe, "1:1 生まれ（何も書いていない）", dimSheet);
	probe.log("  同じ 1 本で「明示 → クラスへ戻す」を往復する（ovDimFontSize が動くかを見る）");
	gSDK->SetTextStyleRef(dimSheet, textStyleRef);
	ProbeDimDump(probe, "SetTextStyleRef の後", dimSheet);
	gSDK->SetTextStyleByClass(dimSheet);
	ProbeDimDump(probe, "SetTextStyleByClass で戻した後", dimSheet);
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【6】1/125 の平面ビューポートを作り、下ごしらえまで済ませる");
	MCObjectHandle viewport = gSDK->CreateViewport(sheetLayer);
	if (viewport == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	ProbeDimSetReal(viewport, ovViewportScale, kProbeDimVpScale);
	ProbeDimSetSint32(viewport, ovViewportRenderType, static_cast<Sint32>(renderFinalHiddenLine));
	gSDK->SetViewportLayerVisibility(viewport, designLayer, 0);
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
	const double wanted = ProbeDimWantedFontSize();
	probe.log("【7】作り方の違う寸法を 6 行、注釈へ置く");
	probe.log("  紙で " + ProbeDimNum(kProbeDimPaperPt) +
			  "pt にしたい → ovDimFontSize = " + ProbeDimNum(wanted) + "（= 6 × 25.4/72 × 125）");
	probe.log("  上から順に D1〜D6。**測る長さを行ごとに変えてある**ので、"
			  "値が出た行は数字そのもので見分けられる。");
	probe.log("");

	MCObjectHandle rows[6] = {nil, nil, nil, nil, nil, nil};

	// --- D1: いまの利用側と同じ。by-class のまま ovDimFontSize だけ書く ---
	probe.log(
		"  D1（長さ 1000）: 〈クラスの文字スタイル〉のまま ovDimFontSize だけ書く＝利用側の現状");
	rows[0] = ProbeDimMake(0.0, 1000.0);
	ProbeDimDump(probe, "作った直後", rows[0]);
	probe.log("    ovDimFontSize を書いた=" +
			  ProbeDimBool(ProbeDimSetReal(rows[0], ovDimFontSize, wanted)));
	ProbeDimDump(probe, "書いた後", rows[0]);

	// --- D2: SetTextStyleRef で明示してから ovDimFontSize ---
	probe.log("  D2（長さ 2000）: SetTextStyleRef で明示 → そのあと ovDimFontSize を書く");
	rows[1] = ProbeDimMake(-1500.0, 2000.0);
	gSDK->SetTextStyleRef(rows[1], textStyleRef);
	ProbeDimDump(probe, "SetTextStyleRef の直後", rows[1]);
	probe.log("    ovDimFontSize を書いた=" +
			  ProbeDimBool(ProbeDimSetReal(rows[1], ovDimFontSize, wanted)));
	ProbeDimDump(probe, "書いた後", rows[1]);

	// --- D3: SetTextStyleRef だけ（大きさは触らない） ---
	probe.log("  D3（長さ 3000）: SetTextStyleRef で明示するだけ（ovDimFontSize は触らない）");
	rows[2] = ProbeDimMake(-3000.0, 3000.0);
	ProbeDimDump(probe, "作った直後", rows[2]);
	gSDK->SetTextStyleRef(rows[2], textStyleRef);
	ProbeDimDump(probe, "SetTextStyleRef の直後", rows[2]);

	// --- D4: ovDimTextStyle（オブジェクト変数）で明示してから ovDimFontSize ---
	probe.log("  D4（長さ 4000）: ovDimTextStyle(1248) へ番号を書く → そのあと ovDimFontSize");
	rows[3] = ProbeDimMake(-4500.0, 4000.0);
	probe.log("    ovDimTextStyle を書いた=" +
			  ProbeDimBool(
				  ProbeDimSetSint32(rows[3], ovDimTextStyle, static_cast<Sint32>(textStyleRef))));
	ProbeDimDump(probe, "書いた直後", rows[3]);
	probe.log("    ovDimFontSize を書いた=" +
			  ProbeDimBool(ProbeDimSetReal(rows[3], ovDimFontSize, wanted)));
	ProbeDimDump(probe, "書いた後", rows[3]);

	// --- D5: 何もしない対照（1:1 生まれのまま） ---
	probe.log("  D5（長さ 5000）: 対照。1:1 生まれのまま何も書かない");
	rows[4] = ProbeDimMake(-6000.0, 5000.0);
	ProbeDimDump(probe, "そのまま", rows[4]);

	// --- D6: 連続寸法。繋ぐ前に文字スタイルと大きさを当てる ---
	probe.log("  D6（長さ 6000 + 7000 の連続寸法）: 繋ぐ前に両方へ SetTextStyleRef と "
			  "ovDimFontSize を当てる");
	{
		const double chainY = -7500.0;
		MCObjectHandle leftDim = gSDK->CreateLinearDimension(
			WorldPt(0.0, chainY), WorldPt(3000.0, chainY), 0.0, 0.0, Vector2(0.0, 0.0), 0);
		MCObjectHandle rightDim = gSDK->CreateLinearDimension(
			WorldPt(3000.0, chainY), WorldPt(6000.0, chainY), 0.0, 0.0, Vector2(0.0, 0.0), 0);
		if (leftDim == nil || rightDim == nil)
			probe.fail(
				"連続寸法のもとになる直線寸法を作れなかった（CreateLinearDimension が nil）");
		else
		{
			ProbeDimSetBoolean(leftDim, ovDimShowValue, true);
			ProbeDimSetBoolean(rightDim, ovDimShowValue, true);
			gSDK->SetTextStyleRef(leftDim, textStyleRef);
			gSDK->SetTextStyleRef(rightDim, textStyleRef);
			ProbeDimSetReal(leftDim, ovDimFontSize, wanted);
			ProbeDimSetReal(rightDim, ovDimFontSize, wanted);
			ProbeDimDump(probe, "繋ぐ前 左", leftDim);
			ProbeDimDump(probe, "繋ぐ前 右", rightDim);

			MCObjectHandle chain = gSDK->CreateChainDimension(leftDim, rightDim);
			if (chain == nil)
				probe.fail(
					"CreateChainDimension が nil を返した——連続寸法での残り方が確かめられない");
			else
			{
				rows[5] = chain;
				probe.log("    連続寸法の型=" +
						  ProbeDimInt(static_cast<Sint32>(gSDK->GetObjectTypeN(chain))));
				ProbeDimDump(probe, "連続寸法そのもの", chain);
				probe.log("    中身を歩いて、型 63 の直線寸法を読み直す:");
				int memberIndex = 0;
				for (MCObjectHandle member = gSDK->FirstMemberObj(chain); member != nil;
					 member = gSDK->NextObject(member))
				{
					const short memberType = gSDK->GetObjectTypeN(member);
					if (memberType == 0) // kTermNode（終端）は数えない
						break;
					if (memberType == 63)
						ProbeDimDump(probe, "中の直線寸法 #" + ProbeDimInt(++memberIndex), member);
					else
						probe.log("      （型 " + ProbeDimInt(static_cast<Sint32>(memberType)) +
								  " は読み飛ばす）");
				}
				if (memberIndex == 0)
					probe.log("      型 63 の要素が見つからなかった");
			}
		}
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【8】6 行を注釈へ移し、移った後にもう一度読む");
	for (int i = 0; i < 6; ++i)
	{
		if (rows[i] == nil)
		{
			probe.log("    D" + ProbeDimInt(i + 1) + ": 作れていないので移せない");
			continue;
		}
		const Boolean moved = gSDK->AddViewportAnnotationObject(viewport, rows[i]);
		probe.log("    D" + ProbeDimInt(i + 1) +
				  ": AddViewportAnnotationObject=" + ProbeDimBool(moved != 0));
		ProbeDimDump(probe, "D" + ProbeDimInt(i + 1) + " 注釈へ移した後", rows[i]);
	}

	// 注釈へ後から足した図形のクラスは非表示のまま（Findings「ビューポート」）。
	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	probe.log("    クラスを全部表示へ戻して再更新した。");
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("=== 目で見ないと分からないこと（このログには写らない） ===");
	probe.log("シートレイヤ「プローブ 157」の 1/125 のビューポートを開いて、");
	probe.log(
		"上から順に 6 行ならんだ寸法のうち **どの行に値（数字）が出ているか** を教えてください。");
	probe.log("  D1 = 1000 / D2 = 2000 / D3 = 3000 / D4 = 4000 / D5 = 5000 /");
	probe.log("  D6 = 6000 と 7000 が横に 2 つ（連続寸法）");
	probe.log("行を数えなくても、**出ている数字そのものがその行の名前**になっています。");
	probe.log("（数字がまったく出ず寸法線だけの行は「出ていない」です。）");
}
