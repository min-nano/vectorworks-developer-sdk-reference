//
//	probes/runtime/dim-chain-annot-text-size/probe.cpp
//
//	[issue #164] **注釈の連続寸法で「紙の pt」を出す道はどれか——3 つの因子を行ごとに
//	1 つだけ変えて測り分ける。**
//
//	利用側は #157 の手順に従い、寸法規格 `min-nano` が持つ文字スタイル `寸法(6pt)`
//	（`ovTextStyleSize` = 6/72 インチ）を `SetTextStyleRef` で当て、1:1 のシートレイヤが
//	アクティブなまま連続寸法へ繋いで 1/125 の断面ビューポートの注釈へ移した。
//	**OIP の「文字 → スタイル」には `寸法(6pt)` と出るのに値が読めず、OIP で同じ
//	スタイルを選び直すと紙 6pt で読める。**
//
//	Findings に既にある材料（どれも実測）:
//
//	  * `SetTextStyleRef` は `ovDimFontSize` を **`ovTextStyleSize`（インチ）× 25.4 ×
//	    そのときのアクティブレイヤの縮尺** で焼き直す（#157）。1:1 なので 2.1167mm。
//	  * **直線寸法（型 63）は文字スタイルの大きさで描かれ、連続寸法（型 86）は中の
//	    直線寸法の `ovDimFontSize` で描かれる**（#157 の E 表・2〜3 巡目）。
//	  * **「6pt の文字スタイル」をそのまま当てても紙で 6pt にはならない**——文字スタイルの
//	    大きさは図面上の長さとして扱われ、ビューポートの縮尺で割られる（#157 の F 表。
//	    F1＝`0.0833` インチは 1/125 の紙で 0.048pt で「見えない」）。
//	  * **描かれている文字図形（型 10）の 1 文字目を `GetTextSize` で測った値は、実機の
//	    絵と完全に一致する**（#161 の 1 巡目で物差しとして確定）。だから**この調査は
//	    目視に頼らずログだけで判定できる。**
//
//	つまり見立ては「**値は描かれているが小さすぎて見えない**」（出ていないのではない）で、
//	OIP の選び直しは「ビューポートの縮尺の文脈で `SetTextStyleRef` を掛け直す」ことに
//	当たる、という筋になる。**ここを実測で決める。**
//
//	**確かめること**（issue の 3 つの問いに対応させる）:
//
//	  A  失敗の再現。`寸法(6pt)` 相当を当てて 1:1 のまま繋ぐ → 描かれる文字は何 mm か。
//	  B  【Q2 候補 a・連続】A の後に `ovDimFontSize` ＝ 6pt × 25.4/72 × 125 を書く。
//	  C  【Q2 候補 a・単独】同じ書き込みを**繋がない型 63** に。#157 の E 表では
//	     `ovDimFontSize` は型 63 の絵に効かなかった（＝効かない見込み）。
//	  C2 C に `ResetObject` を足す（#161 の 2 巡目の論点を巻き込む）。
//	  D  対照。`寸法(6pt)` 相当を当てるだけの単独（#157 の F1 の再現）。
//	  E  【正解の見込み・連続】大きさを作り直した文字スタイル（6/72 × 125 インチ）で繋ぐ。
//	  F  【正解の見込み・単独】同じ文字スタイルの単独（#157 の F2 の再現）。
//	  G  【Q2 候補 b】**1/125 のデザインレイヤをアクティブにして**から作り・当て・繋ぐ。
//	  H  【Q1】A と同じものを注釈へ置いてから、**1/125 がアクティブな文脈で
//	     `SetTextStyleRef` を掛け直す**（OIP の選び直しの機械的な模倣）。型 86 へ掛ける道と
//	     中の型 63 へ掛ける道を分けて測る。
//	  I  【Q3 の食い違い】伏図（1/50）の再現・**単独**。`寸法(6pt)` 相当を当て、1/50 の
//	     デザインレイヤがアクティブなまま作って 1/50 のビューポートの注釈へ。
//	  J  同じく伏図の再現・**連続**（1/50 がアクティブなまま繋ぐ）。
//	     **I と J のどちらが読めるかで「伏図では出ている」の正体が決まる。**
//
//	**新規の空図面で走らせる。** シートレイヤ 1 枚・デザインレイヤ 2 枚・ビューポート
//	2 つ・文字スタイル 2 つ・寸法 16 本を足す。走らせた後は保存しないこと。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// 利用側と同じ顔ぶれ。断面ビューポートは 1/125、伏図は 1/50。
	const double kP164VpScale = 125.0;
	const double kP164PlanScale = 50.0;
	const double kP164PaperPt = 6.0;

	// `寸法(6pt)` そのもの（紙の pt をそのままインチに直しただけの大きさ）。
	const double kP164SmallInch = kP164PaperPt / 72.0;
	// #157 の F 表が「読める」と確定させた大きさ（紙の pt ÷ 72 × ビューポートの縮尺）。
	const double kP164RightInch = kP164PaperPt / 72.0 * kP164VpScale;
	// 紙で 6pt に見せたい ovDimFontSize（mm）＝ 紙 pt × 25.4/72 × ビューポートの縮尺。
	const double kP164FontSize125 = kP164PaperPt * 25.4 / 72.0 * kP164VpScale;

	std::string P164Bool(bool value)
	{
		return value ? std::string("true") : std::string("false");
	}

	std::string P164Num(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		return std::string(buffer);
	}

	std::string P164Int(Sint32 value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(value));
		return std::string(buffer);
	}

	// 図面上の mm を「その縮尺の紙の上で何 pt か」に直して添える。**判定はこの数字で
	// 行う**（#161 の 1 巡目で、この物差しが実機の絵と一致することを確かめてある）。
	std::string P164PaperText(double valueMM, double scale)
	{
		return P164Num(valueMM) + "mm（1/" + P164Num(scale) + " の紙で " +
			   P164Num(valueMM / scale * 72.0 / 25.4) + "pt）";
	}

	bool P164GetReal(MCObjectHandle handle, short selector, double& out)
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

	bool P164GetSint32(MCObjectHandle handle, short selector, Sint32& out)
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

	bool P164SetReal(MCObjectHandle handle, short selector, double value)
	{
		if (handle == nil)
			return false;
		TVariableBlock block;
		block = static_cast<Real64>(value);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	bool P164SetSint32(MCObjectHandle handle, short selector, Sint32 value)
	{
		if (handle == nil)
			return false;
		TVariableBlock block;
		block = static_cast<Sint32>(value);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	bool P164SetBoolean(MCObjectHandle handle, short selector, bool value)
	{
		// TVariableBlock に SetBoolean は無い（Findings「読み戻すときの型」）。
		if (handle == nil)
			return false;
		TVariableBlock block;
		block = static_cast<Boolean>(value ? 1 : 0);
		return gSDK->SetObjectVariable(handle, selector, block) != 0;
	}

	std::string P164StyleName(InternalIndex index)
	{
		if (index == 0)
			return std::string("(なし＝Un-Styled)");
		TXString name;
		gSDK->InternalIndexToNameN(index, name);
		const std::string text(static_cast<const char*>(name));
		if (text.empty())
			return std::string("(名前なし #") + P164Int(static_cast<Sint32>(index)) + ")";
		return text;
	}

	// -------------------------------------------------------------------------
	// **その図形の中に実際に描かれている文字図形（型 10）の 1 文字目の大きさ。**
	// 目視に頼らない物差し（#155 / #161 と同じ手。深さ 4 段まで）。
	bool P164FindTextSize(MCObjectHandle container, int depth, double& outSizeMM)
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
			if (P164FindTextSize(member, depth + 1, outSizeMM))
				return true;
		}
		return false;
	}

	std::string P164DrawnText(MCObjectHandle object, double scale)
	{
		double sizeMM = 0.0;
		if (!P164FindTextSize(object, 0, sizeMM))
			return std::string("描かれている文字=無し（値そのものが描かれていない）");
		return "描かれている文字=" + P164PaperText(sizeMM, scale);
	}

	// 紙の上で何 pt かに「読める／読めない」の札を付ける。#157 の絵と #161 の物差しの
	// 突き合わせから、紙 6pt は読め、紙 0.12pt は読めなかった。
	std::string P164Verdict(MCObjectHandle object, double scale)
	{
		double sizeMM = 0.0;
		if (!P164FindTextSize(object, 0, sizeMM))
			return std::string("【判定】値が描かれていない");
		const double paperPt = sizeMM / scale * 72.0 / 25.4;
		if (paperPt >= 2.0)
			return "【判定】★読める（紙 " + P164Num(paperPt) + "pt）";
		return "【判定】×小さすぎて読めない（紙 " + P164Num(paperPt) + "pt）";
	}

	// 1 本を 2 行で書き出す。1 行目＝ISDK から読める値、2 行目＝実際に描かれているもの。
	void P164Dump(vwprobe::Report& probe, const std::string& tag, MCObjectHandle dim, double scale)
	{
		if (dim == nil)
		{
			probe.log("    " + tag + ": (nil)");
			return;
		}
		std::string line = "    " + tag + ": ";
		line += "型=" + P164Int(static_cast<Sint32>(gSDK->GetObjectTypeN(dim)));
		line += " クラス由来=" + P164Bool(gSDK->GetTextStyleByClass(dim));
		const InternalIndex styleRef = gSDK->GetTextStyleRef(dim);
		line += " GetTextStyleRef=" + P164Int(static_cast<Sint32>(styleRef));
		line += "(" + P164StyleName(styleRef) + ")";

		Sint32 ovStyle = 0;
		line += P164GetSint32(dim, ovDimTextStyle, ovStyle)
					? (" ovDimTextStyle=" + P164Int(ovStyle))
					: std::string(" ovDimTextStyle=(読めず)");

		double fontSize = 0.0;
		line += P164GetReal(dim, ovDimFontSize, fontSize) ? (" ovDimFontSize=" + P164Num(fontSize))
														  : std::string(" ovDimFontSize=(読めず)");

		double pointSize = 0.0;
		if (P164GetReal(dim, ovDimTextSizeInPoints, pointSize))
			line += " ovDimTextSizeInPoints=" + P164Num(pointSize);

		probe.log(line);
		probe.log("      ↳ " + P164DrawnText(dim, scale) + " / " + P164Verdict(dim, scale));
	}

	// 連続寸法の直下の型 63 を全部出す（作り直しのたびに手は変わるので引き直す）。
	void P164DumpMembers(vwprobe::Report& probe, const std::string& tag, MCObjectHandle chain,
						 double scale)
	{
		if (chain == nil)
			return;
		// 単独の直線寸法（型 63）を渡されたときは黙って戻る（呼び出し側で行ごとに
		// 場合分けせずに済むように）。
		if (gSDK->GetObjectTypeN(chain) == dimHeaderNode)
			return;
		int found = 0;
		size_t guard = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(chain); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (++guard > 100)
				break;
			const short memberType = gSDK->GetObjectTypeN(member);
			if (memberType == 0) // kTermNode（終端）
				break;
			if (memberType == dimHeaderNode)
				P164Dump(probe, tag + " 中の型 63 #" + P164Int(++found), member, scale);
		}
		if (found == 0)
			probe.log("    " + tag + ": 中に型 63 が見つからなかった");
	}

	std::vector<MCObjectHandle> P164Members(MCObjectHandle chain)
	{
		std::vector<MCObjectHandle> out;
		if (chain == nil)
			return out;
		size_t guard = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(chain); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (++guard > 100)
				break;
			const short memberType = gSDK->GetObjectTypeN(member);
			if (memberType == 0)
				break;
			if (memberType == dimHeaderNode)
				out.push_back(member);
		}
		return out;
	}

	// 測る長さが行の名前になる（出た数字を見れば絵の中で行が分かる）。
	MCObjectHandle P164MakeDim(double y, double x1, double x2)
	{
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(x1, y), WorldPt(x2, y), 500.0, 0.0,
														 Vector2(0.0, 0.0), 0);
		if (dim != nil)
			P164SetBoolean(dim, ovDimShowValue, true);
		return dim;
	}

	double P164LayerScale(MCObjectHandle layer)
	{
		if (layer == nil)
			return 0.0;
		double_gs scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return static_cast<double>(scale);
	}

	std::string P164ActiveLayerText()
	{
		MCObjectHandle active = gSDK->GetActiveLayer();
		if (active == nil)
			return std::string("（アクティブレイヤが無い）");
		TXString name;
		gSDK->GetObjectName(active, name);
		return "\"" + std::string(static_cast<const char*>(name)) + "\" 縮尺=1/" +
			   P164Num(P164LayerScale(active));
	}

	// 文字スタイルを 1 つ作り、大きさ（インチ）を書いて ref を返す。
	InternalIndex P164CreateStyle(vwprobe::Report& probe, const char* name, double inch)
	{
		MCObjectHandle style = gSDK->CreateTextStyleResource(TXString(name));
		if (style == nil)
		{
			probe.fail(std::string("CreateTextStyleResource が nil を返した: ") + name);
			return 0;
		}
		P164SetReal(style, ovTextStyleSize, inch);
		double readBack = 0.0;
		P164GetReal(style, ovTextStyleSize, readBack);
		const InternalIndex ref = gSDK->GetObjectInternalIndex(style);
		probe.log(std::string("    \"") + name + "\" ref=" + P164Int(static_cast<Sint32>(ref)) +
				  " ovTextStyleSize=" + P164Num(readBack) + "インチ＝" + P164Num(readBack * 72.0) +
				  "pt（図面上では " + P164Num(readBack * 25.4) + "mm × アクティブの縮尺）");
		return ref;
	}

	// ビューポートを 1 つ作り、縮尺を書いて注釈を使える状態にする
	// （Findings「注釈へ寸法を置くだけでは見えない」の作法）。
	MCObjectHandle P164CreateViewport(vwprobe::Report& probe, MCObjectHandle sheetLayer,
									  double scale)
	{
		MCObjectHandle viewport = gSDK->CreateViewport(sheetLayer);
		if (viewport == nil)
		{
			probe.fail("CreateViewport が nil を返した（縮尺 1/" + P164Num(scale) + "）");
			return nil;
		}
		P164SetSint32(viewport, ovViewportRenderType, static_cast<Sint32>(renderFinalHiddenLine));
		P164SetReal(viewport, ovViewportScale, scale);
		gSDK->UpdateViewport(viewport);
		double readBack = 0.0;
		P164GetReal(viewport, ovViewportScale, readBack);
		probe.log("    1/" + P164Num(scale) +
				  " のビューポート: 縮尺の読み戻し=" + P164Num(readBack));
		return viewport;
	}
} // namespace

VW_PROBE("dim-chain-annot-text-size", "注釈の連続寸法で紙の pt を出す道を測り分ける",
		 "文字スタイルの大きさ・ovDimFontSize・アクティブ縮尺を行ごとに変えて測る")
{
	probe.log("=== [#164] 注釈の連続寸法で「紙の pt」を出す道はどれか ===");
	probe.log("**新規の空図面で走らせる。図面は壊れる（保存しないこと）。**");
	probe.log("");
	probe.log("判定は目視に頼らない——**寸法の中に描かれている文字図形（型 10）の 1 文字目**を");
	probe.log("GetTextSize で測り、その縮尺の紙の上で何 pt かに直す（#161 の 1 巡目で、この");
	probe.log("物差しが実機の絵と一致することを確かめてある）。");
	probe.log("");
	probe.log("狙い: 1/" + P164Num(kP164VpScale) + " の注釈で紙の上 " + P164Num(kP164PaperPt) +
			  "pt。");
	probe.log("  `寸法(6pt)` 相当の文字スタイル = " + P164Num(kP164SmallInch) + "インチ");
	probe.log("  #157 の F 表が「読める」とした大きさ = " + P164Num(kP164RightInch) + "インチ");
	probe.log("  紙 6pt 相当の ovDimFontSize（1/125） = " + P164Num(kP164FontSize125) + "mm");
	probe.log("走り出しのアクティブレイヤ: " + P164ActiveLayerText());
	probe.log("");

	// =====================================================================
	probe.log("【1】文字スタイルを 2 つ作る");
	const InternalIndex smallStyle =
		P164CreateStyle(probe, "プローブ164 寸法6pt そのまま", kP164SmallInch);
	const InternalIndex rightStyle =
		P164CreateStyle(probe, "プローブ164 寸法6pt 1-125用", kP164RightInch);
	if (smallStyle == 0 || rightStyle == 0)
		return;
	probe.log("");

	// =====================================================================
	probe.log("【2】シートレイヤ（1:1）と、1/125・1/50 のビューポートを作る");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("プローブ164 シート", kLayerSheet);
	if (sheetLayer == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	MCObjectHandle vp125 = P164CreateViewport(probe, sheetLayer, kP164VpScale);
	MCObjectHandle vp50 = P164CreateViewport(probe, sheetLayer, kP164PlanScale);
	if (vp125 == nil || vp50 == nil)
		return;
	probe.log("    アクティブ: " + P164ActiveLayerText() +
			  "（＝利用側と同じ「1:1 がアクティブ」の状態）");
	probe.log("");

	// =====================================================================
	probe.log("【3】1:1 がアクティブなまま A〜F・H を作る（利用側の作り方の再現）");
	probe.log("    **測る長さが行の名前**——出た数字を見れば絵の中で行が分かる。");
	probe.log("");

	// --- A: 失敗の再現 --------------------------------------------------
	probe.log("  A（2001+2002 の連続寸法）: `寸法(6pt)` 相当を当てて 1:1 のまま繋ぐ");
	probe.log("     ＝利用側の作り方そのまま。**値が読めない側の再現**");
	MCObjectHandle chainA = nil;
	{
		MCObjectHandle left = P164MakeDim(0.0, 0.0, 2001.0);
		MCObjectHandle right = P164MakeDim(0.0, 2001.0, 4003.0);
		if (left == nil || right == nil)
		{
			probe.fail("A の直線寸法を作れなかった");
			return;
		}
		gSDK->SetTextStyleRef(left, smallStyle);
		gSDK->SetTextStyleRef(right, smallStyle);
		P164Dump(probe, "A 繋ぐ前の左（1 本目）", left, kP164VpScale);
		chainA = gSDK->CreateChainDimension(left, right);
		P164Dump(probe, "A 連続寸法そのもの（型 86）", chainA, kP164VpScale);
		P164DumpMembers(probe, "A", chainA, kP164VpScale);
	}
	probe.log("");

	// --- B: 候補 a・連続 ------------------------------------------------
	probe.log("  B（3001+3002 の連続寸法）:【Q2 候補 a】A の後に ovDimFontSize ＝ " +
			  P164Num(kP164FontSize125));
	probe.log("     ＝繋ぐ前の型 63 へ書いてから繋ぐ（#157「順番は文字スタイル → "
			  "ovDimFontSize」）");
	MCObjectHandle chainB = nil;
	{
		MCObjectHandle left = P164MakeDim(3000.0, 0.0, 3001.0);
		MCObjectHandle right = P164MakeDim(3000.0, 3001.0, 6003.0);
		gSDK->SetTextStyleRef(left, smallStyle);
		gSDK->SetTextStyleRef(right, smallStyle);
		P164SetReal(left, ovDimFontSize, kP164FontSize125);
		P164SetReal(right, ovDimFontSize, kP164FontSize125);
		P164Dump(probe, "B 繋ぐ前の左", left, kP164VpScale);
		chainB = gSDK->CreateChainDimension(left, right);
		P164Dump(probe, "B 連続寸法そのもの（型 86）", chainB, kP164VpScale);
		P164DumpMembers(probe, "B", chainB, kP164VpScale);
	}
	probe.log("");

	// --- C / C2: 候補 a・単独（Q3） -------------------------------------
	probe.log("  C（4001 の単独・型 63）:【Q3】候補 a を**繋がない寸法**に");
	probe.log("     ＝#157 の E 表では ovDimFontSize は型 63 の絵に効かなかった");
	MCObjectHandle dimC = P164MakeDim(6000.0, 0.0, 4001.0);
	gSDK->SetTextStyleRef(dimC, smallStyle);
	P164SetReal(dimC, ovDimFontSize, kP164FontSize125);
	P164Dump(probe, "C", dimC, kP164VpScale);

	probe.log("  C2（4501 の単独・型 63）: C に ResetObject を足す（#161 の 2 巡目の論点）");
	MCObjectHandle dimC2 = P164MakeDim(9000.0, 0.0, 4501.0);
	gSDK->SetTextStyleRef(dimC2, smallStyle);
	P164SetReal(dimC2, ovDimFontSize, kP164FontSize125);
	P164Dump(probe, "C2 ResetObject の前", dimC2, kP164VpScale);
	gSDK->ResetObject(dimC2);
	P164Dump(probe, "C2 ResetObject の後", dimC2, kP164VpScale);
	probe.log("");

	// --- D: 対照 --------------------------------------------------------
	probe.log("  D（5001 の単独・型 63）: 対照。`寸法(6pt)` 相当を当てるだけ（#157 F1 の再現）");
	MCObjectHandle dimD = P164MakeDim(12000.0, 0.0, 5001.0);
	gSDK->SetTextStyleRef(dimD, smallStyle);
	P164Dump(probe, "D", dimD, kP164VpScale);
	probe.log("");

	// --- E / F: 正解の見込み --------------------------------------------
	probe.log("  E（6001+6002 の連続寸法）: 大きさを作り直した文字スタイル（" +
			  P164Num(kP164RightInch) + "インチ）で繋ぐ");
	probe.log("     ＝#157 の F2（読めた行）の連続寸法版。**ovDimFontSize は触らない**");
	MCObjectHandle chainE = nil;
	{
		MCObjectHandle left = P164MakeDim(15000.0, 0.0, 6001.0);
		MCObjectHandle right = P164MakeDim(15000.0, 6001.0, 12003.0);
		gSDK->SetTextStyleRef(left, rightStyle);
		gSDK->SetTextStyleRef(right, rightStyle);
		P164Dump(probe, "E 繋ぐ前の左", left, kP164VpScale);
		chainE = gSDK->CreateChainDimension(left, right);
		P164Dump(probe, "E 連続寸法そのもの（型 86）", chainE, kP164VpScale);
		P164DumpMembers(probe, "E", chainE, kP164VpScale);
	}

	probe.log("  F（7001 の単独・型 63）: 同じ文字スタイルの単独（#157 F2 の再現）");
	MCObjectHandle dimF = P164MakeDim(18000.0, 0.0, 7001.0);
	gSDK->SetTextStyleRef(dimF, rightStyle);
	P164Dump(probe, "F", dimF, kP164VpScale);
	probe.log("");

	// --- H: Q1 の材料（掛け直しは【6】で） ------------------------------
	probe.log("  H（9001+9002 の連続寸法）: A と同じ作り。**注釈へ移してから掛け直す**用");
	MCObjectHandle chainH = nil;
	{
		MCObjectHandle left = P164MakeDim(21000.0, 0.0, 9001.0);
		MCObjectHandle right = P164MakeDim(21000.0, 9001.0, 18003.0);
		gSDK->SetTextStyleRef(left, smallStyle);
		gSDK->SetTextStyleRef(right, smallStyle);
		chainH = gSDK->CreateChainDimension(left, right);
		P164Dump(probe, "H 連続寸法そのもの（型 86）", chainH, kP164VpScale);
		P164DumpMembers(probe, "H", chainH, kP164VpScale);
	}
	probe.log("");

	// =====================================================================
	probe.log("【4】A〜F・H を 1/125 の注釈へ移す（#157: 移しても値は 1 つも動かない）");
	{
		MCObjectHandle moved[8] = {chainA, chainB, dimC, dimC2, dimD, chainE, dimF, chainH};
		const char* names[8] = {"A", "B", "C", "C2", "D", "E", "F", "H"};
		for (int index = 0; index < 8; ++index)
		{
			if (moved[index] == nil)
			{
				probe.log("    " + std::string(names[index]) + ": 作れていないので移せない");
				continue;
			}
			gSDK->AddViewportAnnotationObject(vp125, moved[index]);
		}
		gSDK->UpdateViewport(vp125);
		probe.log("    移した後（1/125 の注釈の中）:");
		for (int index = 0; index < 8; ++index)
		{
			if (moved[index] == nil)
				continue;
			P164Dump(probe, std::string(names[index]) + " 注釈の中", moved[index], kP164VpScale);
			P164DumpMembers(probe, std::string(names[index]) + " 注釈の中", moved[index],
							kP164VpScale);
		}
	}
	probe.log("");

	// =====================================================================
	probe.log("【5】伏図（1/50）の再現——I（単独）と J（連続）。**どちらが読めるかで、");
	probe.log("   「伏図では前から値が出ている」の正体が決まる**（issue の 3 つ目の食い違い）");
	{
		MCObjectHandle planLayer = gSDK->CreateLayer("プローブ164 伏図 1-50", kLayerDesign);
		if (planLayer == nil)
		{
			probe.fail("CreateLayer(kLayerDesign) が nil を返した（1/50）");
			return;
		}
		gSDK->SetLayerScaleN(planLayer, kP164PlanScale);
		probe.log("    アクティブ: " + P164ActiveLayerText());

		probe.log("  I（1001 の単独・型 63）: `寸法(6pt)` 相当を当てる。1/50 がアクティブ");
		MCObjectHandle dimI = P164MakeDim(0.0, 0.0, 1001.0);
		gSDK->SetTextStyleRef(dimI, smallStyle);
		P164Dump(probe, "I", dimI, kP164PlanScale);

		probe.log("  J（1501+1502 の連続寸法）: 同じものを 1/50 がアクティブなまま繋ぐ");
		MCObjectHandle left = P164MakeDim(3000.0, 0.0, 1501.0);
		MCObjectHandle right = P164MakeDim(3000.0, 1501.0, 3003.0);
		gSDK->SetTextStyleRef(left, smallStyle);
		gSDK->SetTextStyleRef(right, smallStyle);
		P164Dump(probe, "J 繋ぐ前の左", left, kP164PlanScale);
		MCObjectHandle chainJ = gSDK->CreateChainDimension(left, right);
		P164Dump(probe, "J 連続寸法そのもの（型 86）", chainJ, kP164PlanScale);
		P164DumpMembers(probe, "J", chainJ, kP164PlanScale);

		probe.log("    1/50 の注釈へ移す");
		if (dimI != nil)
			gSDK->AddViewportAnnotationObject(vp50, dimI);
		if (chainJ != nil)
			gSDK->AddViewportAnnotationObject(vp50, chainJ);
		gSDK->UpdateViewport(vp50);
		P164Dump(probe, "I 注釈の中", dimI, kP164PlanScale);
		P164Dump(probe, "J 注釈の中", chainJ, kP164PlanScale);
		P164DumpMembers(probe, "J 注釈の中", chainJ, kP164PlanScale);
	}
	probe.log("");

	// =====================================================================
	probe.log("【6】1/125 のデザインレイヤをアクティブにして G（候補 b）と H の掛け直し（Q1）");
	{
		MCObjectHandle workLayer = gSDK->CreateLayer("プローブ164 作業 1-125", kLayerDesign);
		if (workLayer == nil)
		{
			probe.fail("CreateLayer(kLayerDesign) が nil を返した（1/125）");
			return;
		}
		gSDK->SetLayerScaleN(workLayer, kP164VpScale);
		probe.log("    アクティブ: " + P164ActiveLayerText());
		probe.log("");

		probe.log("  G（8001+8002 の連続寸法）:【Q2 候補 b】1/125 がアクティブな文脈で");
		probe.log("     作り・`寸法(6pt)` 相当を当て・繋ぐ。**ovDimFontSize は触らない**");
		MCObjectHandle left = P164MakeDim(0.0, 0.0, 8001.0);
		MCObjectHandle right = P164MakeDim(0.0, 8001.0, 16003.0);
		gSDK->SetTextStyleRef(left, smallStyle);
		gSDK->SetTextStyleRef(right, smallStyle);
		P164Dump(probe, "G 繋ぐ前の左", left, kP164VpScale);
		MCObjectHandle chainG = gSDK->CreateChainDimension(left, right);
		P164Dump(probe, "G 連続寸法そのもの（型 86）", chainG, kP164VpScale);
		P164DumpMembers(probe, "G", chainG, kP164VpScale);
		if (chainG != nil)
		{
			gSDK->AddViewportAnnotationObject(vp125, chainG);
			gSDK->UpdateViewport(vp125);
			P164Dump(probe, "G 注釈の中", chainG, kP164VpScale);
			P164DumpMembers(probe, "G 注釈の中", chainG, kP164VpScale);
		}
		probe.log("");

		// --- H の掛け直し（OIP の選び直しの機械的な模倣） -----------------
		probe.log("  H の掛け直し:【Q1】OIP で同じ文字スタイルを選び直すのを、");
		probe.log("     「1/125 がアクティブな文脈で SetTextStyleRef を掛け直す」で模倣する。");
		probe.log("     **型 86 へ掛ける道と、中の型 63 へ掛ける道を分けて測る。**");
		if (chainH == nil)
			probe.fail("H の連続寸法が nil なので Q1 を測れない");
		else
		{
			probe.log("  H-1: 連続寸法そのもの（型 86）へ SetTextStyleRef を掛け直す");
			gSDK->SetTextStyleRef(chainH, smallStyle);
			P164Dump(probe, "H-1 型 86（掛け直した直後）", chainH, kP164VpScale);
			P164DumpMembers(probe, "H-1", chainH, kP164VpScale);
			gSDK->ResetObject(chainH);
			P164Dump(probe, "H-1 型 86（ResetObject の後）", chainH, kP164VpScale);
			P164DumpMembers(probe, "H-1 ResetObject の後", chainH, kP164VpScale);

			probe.log("  H-2: 中の型 63 へ SetTextStyleRef を掛け直す（手は引き直す）");
			const std::vector<MCObjectHandle> members = P164Members(chainH);
			for (size_t index = 0; index < members.size(); ++index)
				gSDK->SetTextStyleRef(members[index], smallStyle);
			P164Dump(probe, "H-2 型 86（掛け直した直後）", chainH, kP164VpScale);
			P164DumpMembers(probe, "H-2", chainH, kP164VpScale);
			gSDK->ResetObject(chainH);
			P164Dump(probe, "H-2 型 86（ResetObject の後）", chainH, kP164VpScale);
			P164DumpMembers(probe, "H-2 ResetObject の後", chainH, kP164VpScale);

			gSDK->UpdateViewport(vp125);
			probe.log("  H-3: UpdateViewport の後（絵を描き直させてから測り直す）");
			P164Dump(probe, "H-3 型 86", chainH, kP164VpScale);
			P164DumpMembers(probe, "H-3", chainH, kP164VpScale);
		}
	}
	probe.log("");

	// =====================================================================
	probe.log("【7】読み方");
	probe.log("  * 1/125 の注釈（A〜H）で **★読める** と出た行が、SDK から作れる道である。");
	probe.log("  * A と D が **×小さすぎて読めない** なら、利用側の症状は「値が描かれない」");
	probe.log("    ではなく「**描かれているが紙 0.048pt で見えない**」ことになる。");
	probe.log("  * B が読めて C が読めないなら、候補 a は**連続寸法にしか効かない**。");
	probe.log("  * G が読めるなら候補 b は通る（ただしアクティブレイヤを作る副作用が要る）。");
	probe.log("  * E と F が両方読めるなら、**縮尺ごとに文字スタイルを作り直す道だけが");
	probe.log("    連続寸法と単独寸法の両方で通る**ことになる（#157 の結論の裏取り）。");
	probe.log("  * I が読めず J が読めるなら、「伏図では出ている」のは**連続寸法だけ**で、");
	probe.log("    E 表との食い違いは消える。");
	probe.log("");
	probe.log("おわり。**この図面は保存しないこと。**");
}
