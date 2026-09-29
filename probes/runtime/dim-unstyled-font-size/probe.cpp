//
//	probes/runtime/dim-unstyled-font-size/probe.cpp
//
//	[issue #161] **`ovDimFontSize` はいつ絵に効くのか——2 巡目。残っているのは 1 点だけ。**
//
//	1 巡目（ビルド 8af76555fd17）で、**絵と突き合わせた物差しが手に入った**。各行の
//	「中に実際に描かれている文字図形（型 10）の 1 文字目の大きさ」を `GetTextSize` で
//	測った値が、実機の絵（読めた行＝ G5 と G7 だけ）と**完全に一致**した。以後この
//	調査では「描かれている文字＝紙 6pt なら読める／紙 0.12pt なら読めない」と
//	ログだけで判定してよい。
//
//	1 巡目で決まったこと:
//
//	  ・**外す口はある。** `SetTextStyleRef(dim, 0)` で「クラス由来=false /
//	    `GetTextStyleRef`=0 / `ovDimTextStyle`=0」になり、`ResetObject` を越えて残る。
//	    `ovDimTextStyle`(1248) へ `0` を書く道も同じ結果。`-1` を書くと「番号 −1 が
//	    当たっている」という別の状態になり、外れない。クラスを Un-Styled にしてから
//	    `SetTextStyleByClass` しても by-class のまま（実効 `30`）。
//	  ・**外しても `ovDimFontSize` は絵に効かなかった。** 外して紙 6pt 相当（105.8333）
//	    を書いた行も、その 2 倍（211.6667）を書いた行も、描かれた文字は**どちらも
//	    2.1167mm（紙 0.12pt）**——作ったときに焼き付いた大きさのままで、1 ミリも
//	    動かなかった。**見立て（文字スタイルが当たっているときだけ効かない）は反証。**
//	  ・絵に効いたのは 2 つだけ。**`SetTextStyleRef(ref)` で大きさのある文字スタイルを
//	    当てた行**（105.8333mm ＝紙 6pt）と、**`CreateChainDimension` で繋いだ行**
//	    （中の直線寸法も 105.8333mm）。
//
//	**つまり `ovDimFontSize` への書き込みは捨てられてはいない**——繋ぎ直しという
//	「作り直し」を通ると絵に出た。だとすれば **`ResetObject`（単体の作り直し）でも
//	出るのではないか。** Findings「連続寸法」には #155 の実測として
//	「**対照: 単独の直線寸法は書けば効く（`ResetObject` で絵も変わる）**」とあり、
//	1 巡目はその `ResetObject` を**呼んでいなかった**。
//
//	**ここが #143 の生死を分ける。** 反映されるなら #143 の手順は「書いたあと作り直す」
//	を足せば生き、反映されないなら #143 は撤回になる。**取りに行けば取れる答えなので、
//	未確認のまま畳まない**（CLAUDE.md「PR とマージ」3）。
//
//	2 巡目で確かめること——**書いたあとに何を通せば絵に出るか**を、行ごとに 1 つずつ:
//
//	  H1  Un-Styled ＋ 紙 6pt を書く → **`ResetObject`**
//	  H2  by-class  ＋ 紙 6pt を書く → **`ResetObject`**（外す必要があるのかを分ける）
//	  H3  Un-Styled ＋ 紙 6pt を書く → 注釈へ移して **ビューポートの縮尺を往復**
//	  H4  Un-Styled ＋ **紙 12pt** を書く → `ResetObject`（効いていれば H1 の 2 倍）
//	  H5  物差し。`SetTextStyleRef` に紙 6pt の文字スタイル（**必ず読める基準の行**）
//	  H6  対照。Un-Styled ＋ 紙 6pt を書くが **`ResetObject` を呼ばない**（1 巡目の再現）
//	  H7  対照。連続寸法（1 巡目で読めた行の再現）
//
//	**新規の空図面で走らせる。** シートレイヤ 1 枚と 1/50 のビューポート 1 つ、文字
//	スタイル 1 つ、寸法を 8 本足す。走らせた後は保存しないこと。
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

} // namespace

VW_PROBE("dim-unstyled-font-size", "ovDimFontSize を書いたあと何を通せば絵に出るか",
		 "書いた後に ResetObject / 縮尺の往復を通し、描かれる文字の大きさを測り分ける")
{
	probe.log("=== [#161] ovDimFontSize はいつ絵に効くのか（2 巡目） ===");
	probe.log("新規の空図面で走らせる前提。図面は壊れる。");
	probe.log("");
	probe.log("1 巡目で「描かれている文字の大きさ」が実機の絵と一致することを確かめた");
	probe.log("（読めた行 ＝ 紙 6pt の 2 行だけ）。以後この物差しで判定してよい:");
	probe.log("  紙 6.0000pt → 読める / 紙 0.1200pt → 読めない");
	probe.log("");

	const double fontSize6pt = ProbeUnstyledPaperPtToFontSize(6.0);
	const double fontSize12pt = ProbeUnstyledPaperPtToFontSize(12.0);

	// -----------------------------------------------------------------
	probe.log("【1】物差しに使う文字スタイルを 1 つ作る（ovTextStyleSize の単位はインチ）");
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
	probe.log("【2】シートレイヤと 1/50 の平面ビューポートを作る（デザインレイヤは作らない）");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("プローブ 161 の 2", kLayerSheet);
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
	probe.log("【3】注釈へ 7 行。**書いたあとに通すもの**だけが行ごとに違う");
	probe.log("    紙 6pt 相当の ovDimFontSize = " + ProbeUnstyledNum(fontSize6pt));
	probe.log("    紙 12pt 相当の ovDimFontSize = " + ProbeUnstyledNum(fontSize12pt));
	probe.log("    **測る長さを行ごとに変えてある**ので、出た数字が行の名前になる。");
	probe.log("");

	MCObjectHandle rows[7] = {nil, nil, nil, nil, nil, nil, nil};

	probe.log("  H1（長さ 1000）: 外す → ovDimFontSize = 紙 6pt → **ResetObject**");
	probe.log("    ＝ 1 巡目に足りなかったかもしれない 1 手。ここが本命");
	rows[0] = ProbeUnstyledMakeDim(0.0, 1000.0);
	gSDK->SetTextStyleRef(rows[0], 0);
	ProbeUnstyledSetReal(rows[0], ovDimFontSize, fontSize6pt);
	ProbeUnstyledDump(probe, "H1 書いた直後（ResetObject の前）", rows[0]);
	gSDK->ResetObject(rows[0]);
	ProbeUnstyledDump(probe, "H1 ResetObject の後", rows[0]);

	probe.log("  H2（長さ 2000）: **外さずに** ovDimFontSize = 紙 6pt → ResetObject");
	probe.log("    ＝ 外すことに意味があるのかを H1 と分ける");
	rows[1] = ProbeUnstyledMakeDim(-2000.0, 2000.0);
	ProbeUnstyledSetReal(rows[1], ovDimFontSize, fontSize6pt);
	ProbeUnstyledDump(probe, "H2 書いた直後", rows[1]);
	gSDK->ResetObject(rows[1]);
	ProbeUnstyledDump(probe, "H2 ResetObject の後", rows[1]);

	probe.log("  H3（長さ 3000）: 外す → ovDimFontSize = 紙 6pt（縮尺の往復はこの後の【5】で）");
	rows[2] = ProbeUnstyledMakeDim(-4000.0, 3000.0);
	gSDK->SetTextStyleRef(rows[2], 0);
	ProbeUnstyledSetReal(rows[2], ovDimFontSize, fontSize6pt);
	ProbeUnstyledDump(probe, "H3", rows[2]);

	probe.log("  H4（長さ 4000）: 外す → ovDimFontSize = 紙 12pt → ResetObject");
	probe.log("    ＝ H1 が効いたなら、この行はそのちょうど 2 倍で出るはず");
	rows[3] = ProbeUnstyledMakeDim(-6000.0, 4000.0);
	gSDK->SetTextStyleRef(rows[3], 0);
	ProbeUnstyledSetReal(rows[3], ovDimFontSize, fontSize12pt);
	gSDK->ResetObject(rows[3]);
	ProbeUnstyledDump(probe, "H4 ResetObject の後", rows[3]);

	probe.log("  H5（長さ 5000）: 物差し。SetTextStyleRef に「紙 6pt」の文字スタイルを当てる");
	probe.log("    ＝ 1 巡目に絵で読めた行。**必ず紙 6pt に出る基準**");
	rows[4] = ProbeUnstyledMakeDim(-8000.0, 5000.0);
	if (rulerStyle != 0)
		gSDK->SetTextStyleRef(rows[4], rulerStyle);
	ProbeUnstyledDump(probe, "H5", rows[4]);

	probe.log("  H6（長さ 6000）: 対照。外して ovDimFontSize = 紙 6pt、**ResetObject は呼ばない**");
	probe.log("    ＝ 1 巡目の G1 の再現（紙 0.12pt で出るはず）");
	rows[5] = ProbeUnstyledMakeDim(-10000.0, 6000.0);
	gSDK->SetTextStyleRef(rows[5], 0);
	ProbeUnstyledSetReal(rows[5], ovDimFontSize, fontSize6pt);
	ProbeUnstyledDump(probe, "H6", rows[5]);

	probe.log("  H7（長さ 7000 + 8000 の連続寸法）: 対照。1 巡目で絵に出た行の再現");
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
				ProbeUnstyledDump(probe, "H7 連続寸法そのもの", chain);
				ProbeUnstyledDumpMembers(probe, "H7", chain);
			}
		}
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【4】注釈へ移す");
	for (int i = 0; i < 7; ++i)
	{
		if (rows[i] == nil)
		{
			probe.log("    H" + ProbeUnstyledInt(i + 1) + ": 作れていないので移せない");
			continue;
		}
		const Boolean moved = gSDK->AddViewportAnnotationObject(viewport, rows[i]);
		probe.log("    H" + ProbeUnstyledInt(i + 1) +
				  ": AddViewportAnnotationObject=" + ProbeUnstyledBool(moved != 0));
	}
	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	probe.log("    クラスを全部表示へ戻して再更新した。");
	probe.log("");

	probe.log("  移して更新した直後の大きさ:");
	for (int i = 0; i < 7; ++i)
	{
		if (rows[i] == nil)
			continue;
		probe.log("    H" + ProbeUnstyledInt(i + 1) + ": " + ProbeUnstyledDrawnText(rows[i]));
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【5】ビューポートの縮尺を往復させる（1/50 → 1/100 → 1/50）");
	probe.log("    Findings「注釈はビューポートの縮尺で描かれる」によれば、VW は注釈の");
	probe.log("    中身の ovDimFontSize を比例して書き換える。**そのとき絵の側も引き直す**");
	probe.log("    なら、H3 はここで初めて紙 6pt になるはず。");
	ProbeUnstyledSetReal(viewport, ovViewportScale, 100.0);
	gSDK->UpdateViewport(viewport);
	probe.log("  1/100 にした直後:");
	for (int i = 0; i < 7; ++i)
	{
		if (rows[i] == nil)
			continue;
		ProbeUnstyledDump(probe, "H" + ProbeUnstyledInt(i + 1) + " @1/100", rows[i]);
	}
	ProbeUnstyledSetReal(viewport, ovViewportScale, kProbeUnstyledVpScale);
	gSDK->UpdateViewport(viewport);
	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	probe.log("  1/50 へ戻した後:");
	for (int i = 0; i < 7; ++i)
	{
		if (rows[i] == nil)
			continue;
		ProbeUnstyledDump(probe, "H" + ProbeUnstyledInt(i + 1) + " @1/50", rows[i]);
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【6】最後の一覧——この表がそのまま答えになる");
	probe.log("    紙 6.0000pt なら読める / 紙 0.1200pt なら読めない（1 巡目で絵と照合済み）");
	for (int i = 0; i < 7; ++i)
	{
		if (rows[i] == nil)
			continue;
		probe.log("    H" + ProbeUnstyledInt(i + 1) + ": " + ProbeUnstyledDrawnText(rows[i]) +
				  " / " + ProbeUnstyledBounds(rows[i]));
	}
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("=== 目で見ないと分からないこと（このログには写らない） ===");
	probe.log("**今回は、物差しが 1 巡目の絵と一致したので、原則としてログだけで決まります。**");
	probe.log("念のためシートレイヤ「プローブ 161 の 2」の 1/50 のビューポートを開き、");
	probe.log("  H1 = 1,000 / H2 = 2,000 / H3 = 3,000 / H4 = 4,000 / H5 = 5,000 /");
	probe.log("  H6 = 6,000 / H7 = 7,000 と 8,000（連続寸法）");
	probe.log("のうち **読める数字が上の表と食い違っていないか** だけ見ていただけると助かります。");
	probe.log("（前回と同じなら、5,000 と 7,000/8,000 は必ず読めます。）");
}
