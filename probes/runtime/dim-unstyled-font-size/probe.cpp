//
//	probes/runtime/dim-unstyled-font-size/probe.cpp
//
//	[issue #161] **`ovDimFontSize` はいつ絵に効くのか。** #143 の手順（by-class のまま
//	`ovDimFontSize` だけ書く）が #157 の 3 回の実行で再現しなかった——出なかったのは
//	D1 / E5 / F5 で、いずれも〈クラスの文字スタイル〉のままの寸法だった。
//
//	見立ては「**文字スタイルが当たっているあいだは `ovDimFontSize` が絵に効かない**」。
//	#157 はこれを確かめようとして**届かなかった**——寸法の規格を `JIS`（文字スタイルを
//	持たない）へ替えても `GetTextStyleRef` は `30`（`寸法(6pt)`）のまま、`ovDimTextStyle`
//	も `-2` のままで、「文字スタイルがどこにも当たっていない寸法」を作れなかった。
//
//	**外す口はヘッダに書いてあった**（`Include/vs.py` の `SetTextStyleRef`）:
//
//	    Procedure SetTextStyleRef sets the text style of an object to the referenced
//	    style. **Reference 0 means Un-Styled.** This procedure will replace by-class
//	    styling.
//	    （`GetTextStyleRef` 側: "If the text object is using class text style, this
//	      returns the **effective** style." ＝ by-class の `30` は実効値であって、
//	      寸法が自分で持っている番号ではない）
//
//	つまり **`SetTextStyleRef(dim, 0)` が「文字スタイルを外す」口**である見込み。
//	これは【ヘッダ根拠】でしかないので、実機で次の 4 つを決める（issue の問いと同じ番号）:
//
//	  1. **外せるか。** `SetTextStyleRef(dim, 0)` の後、`GetTextStyleByClass` /
//	     `GetTextStyleRef` / `ovDimTextStyle` は何になるか。ほかの口（`ovDimTextStyle`
//	     へ `0` / `-1` を書く、クラスを Un-Styled にしてから `SetTextStyleByClass`）は
//	     どうか。`ResetObject` を越えて残るか。
//	  2. **外したとき `ovDimFontSize` は絵に効くか。** 効けば #143 は「文字スタイルが
//	     当たっていないときの話」として条件付きで残せる。効かなければ #143 を訂正する。
//	  3. **#143 の当時と図面の何が違ったのか。** 2 が「効く」なら、違いは「寸法に文字
//	     スタイルが当たっているかどうか」だと 1 本で決まる。
//	  4. **文字スタイルが当たっていない寸法は、そもそも注釈で値が出るのか。**
//
//	**目視は最後の 1 手にする**（CLAUDE.md「目視を頼む前にプローブへ 1 行足す」）。
//	各行について、**寸法の中に実際に描かれている文字図形（型 10）の 1 文字目の大きさ**を
//	`GetTextSize` で測って紙の pt に直し、外接矩形の高さと一緒に出す。これで「どの大きさで
//	描かれたか」はログだけで比べられる——**読める／読めないの最終判断だけを人に尋ねる。**
//
//	**新規の空図面で走らせる。** シートレイヤ 1 枚と 1/50 のビューポート 1 つ、文字
//	スタイル 1 つ、寸法を 9 本足す。走らせた後は保存しないこと。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// #143 が絵で確定させたときと同じ 1/50 のビューポートで再現する。
	const double kProbeUnstyledVpScale = 50.0;

	// 紙で P pt に見せたい ovDimFontSize（mm）。Findings「注釈へ寸法を置くときの作り方」の式。
	double ProbeUnstyledPaperPtToFontSize(double paperPt)
	{
		return paperPt * 25.4 / 72.0 * kProbeUnstyledVpScale;
	}

	// 紙で P pt に見せたい ovTextStyleSize（インチ）。Findings「文字スタイルの大きさの決め方」。
	double ProbeUnstyledPaperPtToStyleInch(double paperPt)
	{
		return paperPt / 72.0 * kProbeUnstyledVpScale;
	}

	std::string ProbeUnstyledBool(bool value)
	{
		return value ? std::string("true") : std::string("false");
	}

	std::string ProbeUnstyledNum(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		return std::string(buffer);
	}

	std::string ProbeUnstyledInt(Sint32 value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(value));
		return std::string(buffer);
	}

	// 図面上の長さ（mm）を「1/50 の紙の上で何 pt か」に直して添える。
	std::string ProbeUnstyledPaperText(double valueMM)
	{
		return ProbeUnstyledNum(valueMM) + "mm（紙で " +
			   ProbeUnstyledNum(valueMM / kProbeUnstyledVpScale * 72.0 / 25.4) + "pt）";
	}

	std::string ProbeUnstyledIndexName(InternalIndex index)
	{
		if (index == 0)
			return std::string("(なし＝Un-Styled)");
		TXString name;
		gSDK->InternalIndexToNameN(index, name);
		const std::string text(static_cast<const char*>(name));
		if (text.empty())
			return std::string("(名前なし #") + ProbeUnstyledInt(static_cast<Sint32>(index)) + ")";
		return text;
	}

	bool ProbeUnstyledGetReal(MCObjectHandle handle, short selector, double& out)
	{
		TVariableBlock block;
		if (handle == nil || !gSDK->GetObjectVariable(handle, selector, block))
			return false;
		Real64 value = 0.0;
		if (!block.GetReal64(value))
			return false;
		out = value;
		return true;
	}

	bool ProbeUnstyledGetSint32(MCObjectHandle handle, short selector, Sint32& out)
	{
		TVariableBlock block;
		if (handle == nil || !gSDK->GetObjectVariable(handle, selector, block))
			return false;
		Sint32 value = 0;
		if (!block.GetSint32(value))
			return false;
		out = value;
		return true;
	}

	bool ProbeUnstyledSetReal(MCObjectHandle handle, short selector, double value)
	{
		TVariableBlock block;
		block = static_cast<Real64>(value);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	bool ProbeUnstyledSetSint32(MCObjectHandle handle, short selector, Sint32 value)
	{
		TVariableBlock block;
		block = static_cast<Sint32>(value);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	bool ProbeUnstyledSetBoolean(MCObjectHandle handle, short selector, bool value)
	{
		// TVariableBlock に SetBoolean は無い（Findings「読み戻すときの型」）。
		TVariableBlock block;
		block = static_cast<Boolean>(value ? 1 : 0);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	// -------------------------------------------------------------------------
	// **その図形の中に実際に描かれている文字図形（型 10）の 1 文字目の大きさ。**
	// 「絵で値が見えるか・どの大きさで出たか」を目視に頼らずに測るための物差し
	// （#155 のプローブと同じ手。深さ 4 段まで）。
	bool ProbeUnstyledFindTextSize(MCObjectHandle container, int depth, double& outSizeMM)
	{
		if (container == nil || depth > 4)
			return false;
		size_t guard = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(container); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (++guard > 500)
				break;
			const short type = gSDK->GetObjectTypeN(member);
			if (type == 0) // kTermNode（walk の終端）
				break;
			if (type == kTextNode)
			{
				WorldCoord charSize = 0;
				gSDK->GetTextSize(member, 1, charSize);
				outSizeMM = static_cast<double>(charSize);
				return true;
			}
			if (ProbeUnstyledFindTextSize(member, depth + 1, outSizeMM))
				return true;
		}
		return false;
	}

	std::string ProbeUnstyledDrawnText(MCObjectHandle object)
	{
		double sizeMM = 0.0;
		if (!ProbeUnstyledFindTextSize(object, 0, sizeMM))
			return std::string("描かれている文字=無し");
		return "描かれている文字=" + ProbeUnstyledPaperText(sizeMM);
	}

	std::string ProbeUnstyledBounds(MCObjectHandle object)
	{
		WorldRect bounds;
		if (object == nil || !gSDK->GetObjectBounds(object, bounds))
			return std::string("外接矩形=(取れない)");
		const double height = static_cast<double>(bounds.top) - static_cast<double>(bounds.bottom);
		return "外接矩形の高さ=" + ProbeUnstyledNum(height);
	}

	// 1 本を 2 行で書き出す。**どの段でも同じ並びで出す**ので前後を目で突き合わせられる。
	// 1 行目＝ISDK から読める値、2 行目＝実際に描かれているもの（物差し）。
	void ProbeUnstyledDump(vwprobe::Report& probe, const std::string& tag, MCObjectHandle dim)
	{
		if (dim == nil)
		{
			probe.log("    " + tag + ": (nil)");
			return;
		}
		std::string line = "    " + tag + ": ";
		line += "型=" + ProbeUnstyledInt(static_cast<Sint32>(gSDK->GetObjectTypeN(dim)));
		line += " クラス由来=" + ProbeUnstyledBool(gSDK->GetTextStyleByClass(dim));
		const InternalIndex styleRef = gSDK->GetTextStyleRef(dim);
		line += " GetTextStyleRef=" + ProbeUnstyledInt(static_cast<Sint32>(styleRef));
		line += "(" + ProbeUnstyledIndexName(styleRef) + ")";

		Sint32 ovStyle = 0;
		line += ProbeUnstyledGetSint32(dim, ovDimTextStyle, ovStyle)
					? (" ovDimTextStyle=" + ProbeUnstyledInt(ovStyle))
					: std::string(" ovDimTextStyle=(読めず)");

		double fontSize = 0.0;
		line += ProbeUnstyledGetReal(dim, ovDimFontSize, fontSize)
					? (" ovDimFontSize=" + ProbeUnstyledNum(fontSize))
					: std::string(" ovDimFontSize=(読めず)");

		double pointSize = 0.0;
		if (ProbeUnstyledGetReal(dim, ovDimTextSizeInPoints, pointSize))
			line += " ovDimTextSizeInPoints=" + ProbeUnstyledNum(pointSize);

		probe.log(line);
		probe.log("      ↳ " + ProbeUnstyledDrawnText(dim) + " / " + ProbeUnstyledBounds(dim));
	}

	MCObjectHandle ProbeUnstyledMakeDim(double y, double width)
	{
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(0.0, y), WorldPt(width, y), 0.0,
														 0.0, Vector2(0.0, 0.0), 0);
		if (dim != nil)
			ProbeUnstyledSetBoolean(dim, ovDimShowValue, true);
		return dim;
	}

	// 中の型 63 を全部出す（連続寸法の中身を読むため）。
	void ProbeUnstyledDumpMembers(vwprobe::Report& probe, const std::string& tag,
								  MCObjectHandle chain)
	{
		int found = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(chain); member != nil;
			 member = gSDK->NextObject(member))
		{
			const short memberType = gSDK->GetObjectTypeN(member);
			if (memberType == 0) // kTermNode（終端）
				break;
			if (memberType == dimHeaderNode)
				ProbeUnstyledDump(probe, tag + " 中の直線寸法 #" + ProbeUnstyledInt(++found),
								  member);
		}
		if (found == 0)
			probe.log("    " + tag + " 中に型 63 が見つからなかった");
	}

	// 「外れたか」を 1 行で判定する。外れた＝クラス由来でなく、実効の番号も 0。
	std::string ProbeUnstyledVerdict(MCObjectHandle dim)
	{
		if (dim == nil)
			return std::string("(nil)");
		const bool byClass = gSDK->GetTextStyleByClass(dim);
		const InternalIndex ref = gSDK->GetTextStyleRef(dim);
		if (!byClass && ref == 0)
			return std::string("★外れた（Un-Styled）");
		if (byClass)
			return std::string("外れていない（クラス由来のまま）");
		return std::string("外れていない（番号 " + ProbeUnstyledInt(static_cast<Sint32>(ref)) +
						   " が当たっている）");
	}
} // namespace

VW_PROBE("dim-unstyled-font-size", "寸法から文字スタイルを外すと ovDimFontSize は効くか",
		 "SetTextStyleRef(dim, 0) で外し、1/50 の注釈で #143 の手順が再現するかを見る")
{
	probe.log("=== [#161] ovDimFontSize はいつ絵に効くのか ===");
	probe.log("新規の空図面で走らせる前提。図面は壊れる。");
	probe.log("");

	const double fontSize6pt = ProbeUnstyledPaperPtToFontSize(6.0);
	const double fontSize12pt = ProbeUnstyledPaperPtToFontSize(12.0);

	// -----------------------------------------------------------------
	probe.log("【0】この図面の素性——#143 の当時と何が違うのかの材料（問い 3）");
	{
		// 寸法規格が文字スタイルを持っているか（#157 の 1 巡目と同じ並び）。
		probe.log("  寸法規格 index / 名前 / dimStdTextStyle / その番号が指す名前");
		const short kProbeStandardIndexes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
		for (size_t i = 0; i < sizeof(kProbeStandardIndexes) / sizeof(kProbeStandardIndexes[0]);
			 ++i)
		{
			const short index = kProbeStandardIndexes[i];
			TVariableBlock nameBlock;
			if (!gSDK->GetDimensionStandardVariable(index, dimStdstandardName, nameBlock))
			{
				probe.log("    " + ProbeUnstyledInt(static_cast<Sint32>(index)) +
						  ": （この index は無い）");
				continue;
			}
			TXString standardName;
			nameBlock.GetTXString(standardName);
			TVariableBlock styleBlock;
			Sint32 styleRef = 0;
			if (gSDK->GetDimensionStandardVariable(index, dimStdTextStyle, styleBlock))
				styleBlock.GetSint32(styleRef);
			probe.log("    " + ProbeUnstyledInt(static_cast<Sint32>(index)) + ": \"" +
					  std::string(static_cast<const char*>(standardName)) +
					  "\" / dimStdTextStyle=" + ProbeUnstyledInt(styleRef) + " / " +
					  ProbeUnstyledIndexName(static_cast<InternalIndex>(styleRef)));
		}
		// アクティブクラスが文字スタイルを持っているか。
		const InternalIndex activeClass = gSDK->GetActiveClass();
		probe.log("  アクティブクラス: 番号=" + ProbeUnstyledInt(static_cast<Sint32>(activeClass)) +
				  "(" + ProbeUnstyledIndexName(activeClass) + ") 文字スタイルを使う=" +
				  ProbeUnstyledBool(gSDK->GetClUseTextStyle(activeClass)) + " 番号=" +
				  ProbeUnstyledInt(static_cast<Sint32>(gSDK->GetClTextStyleRef(activeClass))));
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【1】文字スタイルを外す口を探す（問い 1）");
	probe.log("  ヘッダ（vs.py の SetTextStyleRef）が言う「Reference 0 means Un-Styled」を");
	probe.log("  含めて 4 つの道を、それぞれ別の寸法で試す。");
	probe.log("  ★が付いた道が「外れた（クラス由来でなく実効の番号も 0）」道である。");
	{
		// (a) SetTextStyleRef(dim, 0)
		MCObjectHandle dimA = ProbeUnstyledMakeDim(20000.0, 1000.0);
		ProbeUnstyledDump(probe, "(a) 作った直後", dimA);
		gSDK->SetTextStyleRef(dimA, 0);
		ProbeUnstyledDump(probe, "(a) SetTextStyleRef(dim, 0) の後", dimA);
		probe.log("      判定: " + ProbeUnstyledVerdict(dimA));
		gSDK->ResetObject(dimA);
		ProbeUnstyledDump(probe, "(a) さらに ResetObject した後", dimA);
		probe.log("      判定: " + ProbeUnstyledVerdict(dimA));

		// (b) ovDimTextStyle(1248) へ 0 を書く
		MCObjectHandle dimB = ProbeUnstyledMakeDim(21000.0, 1000.0);
		probe.log("    (b) ovDimTextStyle へ 0 を書いた=" +
				  ProbeUnstyledBool(ProbeUnstyledSetSint32(dimB, ovDimTextStyle, 0)));
		ProbeUnstyledDump(probe, "(b) 書いた後", dimB);
		probe.log("      判定: " + ProbeUnstyledVerdict(dimB));

		// (c) ovDimTextStyle へ -1 を書く（-2 が「クラス由来」の番兵なので、隣を試す）
		MCObjectHandle dimC = ProbeUnstyledMakeDim(22000.0, 1000.0);
		probe.log("    (c) ovDimTextStyle へ -1 を書いた=" +
				  ProbeUnstyledBool(ProbeUnstyledSetSint32(dimC, ovDimTextStyle, -1)));
		ProbeUnstyledDump(probe, "(c) 書いた後", dimC);
		probe.log("      判定: " + ProbeUnstyledVerdict(dimC));

		// (d) クラスを Un-Styled にしてから SetTextStyleByClass で引き直す
		MCObjectHandle dimD = ProbeUnstyledMakeDim(23000.0, 1000.0);
		const InternalIndex activeClass = gSDK->GetActiveClass();
		gSDK->SetClUseTextStyle(activeClass, false);
		gSDK->SetClTextStyleRef(activeClass, 0);
		gSDK->SetTextStyleByClass(dimD);
		gSDK->ResetObject(dimD);
		ProbeUnstyledDump(probe, "(d) クラスを Un-Styled にして引き直した後", dimD);
		probe.log("      判定: " + ProbeUnstyledVerdict(dimD));
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【2】物差しに使う文字スタイルを 1 つ作る（ovTextStyleSize の単位はインチ）");
	InternalIndex rulerStyle = 0;
	{
		MCObjectHandle style = gSDK->CreateTextStyleResource("プローブ161 紙6pt 1-50");
		if (style == nil)
			probe.fail("CreateTextStyleResource が nil を返した");
		else
		{
			ProbeUnstyledSetReal(style, ovTextStyleSize, ProbeUnstyledPaperPtToStyleInch(6.0));
			double readBack = 0.0;
			ProbeUnstyledGetReal(style, ovTextStyleSize, readBack);
			rulerStyle = gSDK->GetObjectInternalIndex(style);
			probe.log("    番号=" + ProbeUnstyledInt(static_cast<Sint32>(rulerStyle)) +
					  " ovTextStyleSize=" + ProbeUnstyledNum(readBack) + " インチ（紙で 6pt）");
		}
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【3】シートレイヤと 1/50 の平面ビューポートを作る（デザインレイヤは作らない）");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("プローブ 161", kLayerSheet);
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
	// ビューポートの作法（Findings「注釈へ寸法を置くだけでは見えない」）。
	ProbeUnstyledSetSint32(viewport, ovViewportRenderType,
						   static_cast<Sint32>(renderFinalHiddenLine));
	ProbeUnstyledSetReal(viewport, ovViewportScale, kProbeUnstyledVpScale);
	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	{
		double vpScale = 0.0;
		ProbeUnstyledGetReal(viewport, ovViewportScale, vpScale);
		probe.log("    ビューポートの縮尺（読み戻し）=" + ProbeUnstyledNum(vpScale));
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【4】注釈へ 7 行。**測る長さを行ごとに変えてある**ので、出た数字が行の名前になる");
	probe.log("    紙 6pt 相当の ovDimFontSize = " + ProbeUnstyledNum(fontSize6pt));
	probe.log("    紙 12pt 相当の ovDimFontSize = " + ProbeUnstyledNum(fontSize12pt));
	probe.log("");

	MCObjectHandle rows[7] = {nil, nil, nil, nil, nil, nil, nil};

	probe.log("  G1（長さ 1000）: SetTextStyleRef(dim, 0) で外す → ovDimFontSize = 紙 6pt");
	probe.log("    ＝ #143 の手順を「文字スタイルを外した寸法」に対して行う本命の行");
	rows[0] = ProbeUnstyledMakeDim(0.0, 1000.0);
	gSDK->SetTextStyleRef(rows[0], 0);
	ProbeUnstyledDump(probe, "G1 外した直後", rows[0]);
	probe.log("    ovDimFontSize を書いた=" +
			  ProbeUnstyledBool(ProbeUnstyledSetReal(rows[0], ovDimFontSize, fontSize6pt)));
	ProbeUnstyledDump(probe, "G1 書いた後", rows[0]);

	probe.log("  G2（長さ 2000）: 同じく外して ovDimFontSize = 紙 12pt（G1 のちょうど 2 倍）");
	rows[1] = ProbeUnstyledMakeDim(-2000.0, 2000.0);
	gSDK->SetTextStyleRef(rows[1], 0);
	ProbeUnstyledSetReal(rows[1], ovDimFontSize, fontSize12pt);
	ProbeUnstyledDump(probe, "G2", rows[1]);

	probe.log("  G3（長さ 3000）: 外すだけ（ovDimFontSize は触らない）＝外した直後の素の大きさ");
	rows[2] = ProbeUnstyledMakeDim(-4000.0, 3000.0);
	gSDK->SetTextStyleRef(rows[2], 0);
	ProbeUnstyledDump(probe, "G3", rows[2]);

	probe.log("  G4（長さ 4000）: 対照。〈クラスの文字スタイル〉のまま ovDimFontSize = 紙 6pt");
	probe.log("    ＝ #157 の D1 / E5 / F5 の再現（3 回とも値が出なかった作り方）");
	rows[3] = ProbeUnstyledMakeDim(-6000.0, 4000.0);
	ProbeUnstyledSetReal(rows[3], ovDimFontSize, fontSize6pt);
	ProbeUnstyledDump(probe, "G4", rows[3]);

	probe.log("  G5（長さ 5000）: 物差し。SetTextStyleRef に「紙 6pt」の文字スタイルを当てる");
	probe.log("    ＝ #157 で確定した作り方。**必ず紙で 6pt に出る行**なので G1 と見比べる");
	rows[4] = ProbeUnstyledMakeDim(-8000.0, 5000.0);
	if (rulerStyle != 0)
		gSDK->SetTextStyleRef(rows[4], rulerStyle);
	ProbeUnstyledDump(probe, "G5", rows[4]);

	probe.log("  G6（長さ 6000）: 【1】の (b) の道。ovDimTextStyle へ 0 → ovDimFontSize = 紙 6pt");
	rows[5] = ProbeUnstyledMakeDim(-10000.0, 6000.0);
	ProbeUnstyledSetSint32(rows[5], ovDimTextStyle, 0);
	ProbeUnstyledSetReal(rows[5], ovDimFontSize, fontSize6pt);
	ProbeUnstyledDump(probe, "G6", rows[5]);

	probe.log("  G7（長さ 7000 + 8000 の連続寸法）: 繋ぐ前に両方を外して ovDimFontSize = 紙 6pt");
	{
		const double chainY = -12000.0;
		MCObjectHandle leftDim = gSDK->CreateLinearDimension(
			WorldPt(0.0, chainY), WorldPt(7000.0, chainY), 0.0, 0.0, Vector2(0.0, 0.0), 0);
		MCObjectHandle rightDim = gSDK->CreateLinearDimension(
			WorldPt(7000.0, chainY), WorldPt(15000.0, chainY), 0.0, 0.0, Vector2(0.0, 0.0), 0);
		if (leftDim == nil || rightDim == nil)
			probe.fail("連続寸法のもとになる直線寸法を作れなかった");
		else
		{
			ProbeUnstyledSetBoolean(leftDim, ovDimShowValue, true);
			ProbeUnstyledSetBoolean(rightDim, ovDimShowValue, true);
			gSDK->SetTextStyleRef(leftDim, 0);
			gSDK->SetTextStyleRef(rightDim, 0);
			ProbeUnstyledSetReal(leftDim, ovDimFontSize, fontSize6pt);
			ProbeUnstyledSetReal(rightDim, ovDimFontSize, fontSize6pt);
			MCObjectHandle chain = gSDK->CreateChainDimension(leftDim, rightDim);
			if (chain == nil)
				probe.fail("CreateChainDimension が nil を返した");
			else
			{
				rows[6] = chain;
				ProbeUnstyledDump(probe, "G7 連続寸法そのもの", chain);
				ProbeUnstyledDumpMembers(probe, "G7", chain);
			}
		}
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【5】注釈へ移し、移った後にもう一度読む（外した状態が移動を越えて残るか）");
	for (int i = 0; i < 7; ++i)
	{
		if (rows[i] == nil)
		{
			probe.log("    G" + ProbeUnstyledInt(i + 1) + ": 作れていないので移せない");
			continue;
		}
		const Boolean moved = gSDK->AddViewportAnnotationObject(viewport, rows[i]);
		probe.log("    G" + ProbeUnstyledInt(i + 1) +
				  ": AddViewportAnnotationObject=" + ProbeUnstyledBool(moved != 0));
		ProbeUnstyledDump(probe, "G" + ProbeUnstyledInt(i + 1) + " 移した後", rows[i]);
		if (gSDK->GetObjectTypeN(rows[i]) == 86)
			ProbeUnstyledDumpMembers(probe, "G" + ProbeUnstyledInt(i + 1) + " 移した後", rows[i]);
	}
	// 注釈へ後から足した図形のクラスは非表示のままなので、もう一度戻して再更新する。
	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	probe.log("    クラスを全部表示へ戻して再更新した。");
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【6】更新の後に、描かれている文字の大きさをもう一度だけ測る（物差しの決定版）");
	probe.log("    ここが行ごとに違っていれば、**どの大きさで描かれたかはログだけで決まる。**");
	for (int i = 0; i < 7; ++i)
	{
		if (rows[i] == nil)
			continue;
		probe.log("    G" + ProbeUnstyledInt(i + 1) + ": " + ProbeUnstyledDrawnText(rows[i]) +
				  " / " + ProbeUnstyledBounds(rows[i]));
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("=== 目で見ないと分からないこと（このログには写らない） ===");
	probe.log("シートレイヤ「プローブ 161」の 1/50 のビューポートに、上から");
	probe.log("  G1 = 1,000 / G2 = 2,000 / G3 = 3,000 / G4 = 4,000 / G5 = 5,000 /");
	probe.log("  G6 = 6,000 / G7 = 7,000 と 8,000（連続寸法）");
	probe.log("がならびます。**出ている数字そのものが行の名前**です（行を数えなくて結構です）。");
	probe.log("伺いたいのは 2 つだけ:");
	probe.log("  (1) 読める数字はどれですか（寸法線だけで数字が無い行も教えてください）。");
	probe.log("  (2) **1,000 と 5,000 は同じ大きさですか、違いますか。**");
	probe.log("      5,000 は確定済みの作り方で必ず紙 6pt に出る物差しなので、1,000 が");
	probe.log("      それと同じなら「外せば ovDimFontSize が効く」と決まります。");
	probe.log("見込み: G1 と G5 が同じ大きさで読める / G2 はその 2 倍 / G4 は出ない。");
	probe.log("**違っていたら、そう教えてください。**");
}
