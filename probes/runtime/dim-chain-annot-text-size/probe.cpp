//
//	probes/runtime/dim-chain-annot-text-size/probe.cpp
//
//	[issue #164] **既に注釈へ置いてしまった連続寸法を後から直せるか——2 巡目。
//	残っているのは「直す道が 4 つのうちどれか」だけ。**
//
//	1 巡目（ビルド db8f78a82549）で 10 行を測り、issue の 3 つの問いに答えが付いた
//	（1/125 の注釈。判定は「中に描かれている文字図形（型 10）の 1 文字目」を
//	`GetTextSize` で測った値で、#161 でこの物差しが実機の絵と一致すると確かめてある）:
//
//	  ・A 失敗の再現（`寸法(6pt)`＝0.0833 インチを当てて 1:1 のまま繋ぐ）→ 描かれた文字
//	    2.1167mm ＝紙 0.048pt で **×読めない**。**症状は「値が描かれない」ではなく
//	    「描かれているが小さすぎて見えない」だった。**
//	  ・B 繋ぐ前に `ovDimFontSize` ＝ 264.5833 を書いてから繋ぐ → **★紙 6pt**。
//	  ・C 同じ書き込みを単独の型 63 へ・`ResetObject` 無し → ×。C2 `ResetObject` あり
//	    → **★紙 6pt**。**#157 の「直線寸法は文字スタイルの大きさで描かれ、
//	    `ovDimFontSize` は効かない」は #161 の法則（引き直しで反映される）に吸収された。**
//	  ・E / F 大きさを作り直した文字スタイル（10.4167 インチ）→ 連続でも単独でも ★。
//	  ・G 1/125 のデザインレイヤをアクティブにして当てて繋ぐ → ★。
//	  ・I / J 伏図（1/50）の再現 → **単独も連続も ★紙 6pt**（`ovDimFontSize` は
//	    105.8333）。**「伏図では前から出ている」は 1/50 で作ったから、で説明が付く。**
//
//	**1 巡目で取り損ねたのは「後から直す道」だけである。** H-1（注釈の中で型 86 へ
//	`SetTextStyleRef` を掛け直す）は効いた——掛けた直後は型 86 の絵だけが 264.5833 に
//	変わり、中の型 63 は 2.1167 のままで、`ResetObject(型 86)` を通して初めて中も揃った。
//	ところが **H-2（中の型 63 へ掛け直す道）は H-1 が直した後の同じ連続寸法で試した**
//	ので、単独の効き目が分からない。**取りに行けば取れる答えなので畳まない**
//	（CLAUDE.md「PR とマージ」3）。
//
//	**2 巡目は、A と同じ作りの連続寸法を 5 本別々に用意して、直し方を 1 本に 1 つだけ
//	当てる。** 加えて 1 巡目には無かった 2 つの因子を分ける:
//
//	  ・**`ovDimFontSize` を後から書く道**（#155 は by-class で「繋いだ後に中へ書いても
//	    捨てられる」と実測した。**文字スタイルを明示してあるとどうか**）。
//	  ・**掛け直すときのアクティブレイヤの縮尺**（1:1 のままか、1/125 か）。利用者が
//	    OIP で直したのは**注釈の編集中＝ビューポートの縮尺の文脈**なので、ここが効いて
//	    いるなら 1:1 のままの掛け直しでは直らないはずである。
//
//	| 行 | 長さ | 注釈へ置いた後にすること | アクティブ |
//	| --- | --- | --- | --- |
//	| A  | 2001+2002 | **何もしない**（対照） | — |
//	| K1 | 3001+3002 | 中の型 63 へ `ovDimFontSize` ＝ 264.5833 → `ResetObject(型 86)` | 1:1 |
//	| K2 | 4001+4002 | 同じ書き込み。**`ResetObject` を呼ばない**（対照） | 1:1 |
//	| H0 | 5001+5002 | 型 86 へ `SetTextStyleRef` 掛け直し → `ResetObject(型 86)` | **1:1** |
//	| H1 | 6001+6002 | 同じことを **1/125 がアクティブな文脈で** | **1/125** |
//	| H2 | 7001+7002 | **中の型 63 へ** `SetTextStyleRef` 掛け直し → `ResetObject(型 86)` | **1/125** |
//
//	**H0 と H1 の差が「縮尺の文脈が効いているか」の答え**で、**H1 と H2 の差が
//	「型 86 と中の型 63 のどちらへ掛けるべきか」の答え**である。
//
//	**新規の空図面で走らせる。** シートレイヤ 1 枚・デザインレイヤ 1 枚・ビューポート
//	1 つ・文字スタイル 1 つ・寸法 10 本を足す。走らせた後は保存しないこと。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// 利用側と同じ顔ぶれ。断面ビューポートは 1/125、伏図は 1/50。
	const double kP164VpScale = 125.0;
	const double kP164PaperPt = 6.0;

	// `寸法(6pt)` そのもの（紙の pt をそのままインチに直しただけの大きさ）。
	const double kP164SmallInch = kP164PaperPt / 72.0;
	// #157 の F 表が「読める」と確定させた大きさ（紙の pt ÷ 72 × ビューポートの縮尺）。
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

VW_PROBE("dim-chain-annot-text-size", "注釈の連続寸法を後から直せる道はどれか",
		 "注釈へ置いた連続寸法へ直しを 1 本に 1 つだけ当てて、描かれる文字を測る")
{
	probe.log("=== [#164] 既に注釈へ置いた連続寸法を後から直せるか（2 巡目） ===");
	probe.log("**新規の空図面で走らせる。図面は壊れる（保存しないこと）。**");
	probe.log("");
	probe.log("判定は目視に頼らない——**中に描かれている文字図形（型 10）の 1 文字目**を");
	probe.log("GetTextSize で測り、紙の上で何 pt かに直す（#161 で実機の絵と一致を確認済み）。");
	probe.log("");
	probe.log("1 巡目で決まったこと: A の作り方（`寸法(6pt)`＝" + P164Num(kP164SmallInch) +
			  "インチを 1:1 で当てて繋ぐ）は");
	probe.log("紙 0.048pt で見えない。繋ぐ前に ovDimFontSize を書けば（B）紙 6pt で出る。");
	probe.log("**残っているのは「既に注釈へ置いた後から直せるか」だけ。**");
	probe.log("");
	probe.log("狙い: 1/" + P164Num(kP164VpScale) + " の注釈で紙の上 " + P164Num(kP164PaperPt) +
			  "pt ＝ ovDimFontSize " + P164Num(kP164FontSize125) + "mm");
	probe.log("走り出しのアクティブレイヤ: " + P164ActiveLayerText());
	probe.log("");

	// =====================================================================
	probe.log("【1】`寸法(6pt)` と同じ大きさの文字スタイルを 1 つ作る");
	const InternalIndex smallStyle =
		P164CreateStyle(probe, "プローブ164 寸法6pt そのまま", kP164SmallInch);
	if (smallStyle == 0)
		return;
	probe.log("");

	// =====================================================================
	probe.log("【2】シートレイヤ（1:1）と 1/125 のビューポートを作る");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("プローブ164 シート", kLayerSheet);
	if (sheetLayer == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	MCObjectHandle vp125 = P164CreateViewport(probe, sheetLayer, kP164VpScale);
	if (vp125 == nil)
		return;
	probe.log("    アクティブ: " + P164ActiveLayerText() +
			  "（＝利用側と同じ「1:1 がアクティブ」の状態）");
	probe.log("");

	// =====================================================================
	probe.log("【3】A と同じ作りの連続寸法を 5 本、1:1 がアクティブなまま作る");
	probe.log("    **測る長さが行の名前**——出た数字を見れば絵の中で行が分かる。");
	const char* const kRowNames[5] = {"A", "K1", "K2", "H0", "H1H2"};
	const double kRowSpans[5] = {2001.0, 3001.0, 4001.0, 5001.0, 6001.0};
	MCObjectHandle chains[6] = {nil, nil, nil, nil, nil, nil};
	for (int index = 0; index < 5; ++index)
	{
		const double y = 3000.0 * index;
		const double span = kRowSpans[index];
		MCObjectHandle left = P164MakeDim(y, 0.0, span);
		MCObjectHandle right = P164MakeDim(y, span, span * 2.0);
		if (left == nil || right == nil)
		{
			probe.fail(std::string("直線寸法を作れなかった: ") + kRowNames[index]);
			return;
		}
		gSDK->SetTextStyleRef(left, smallStyle);
		gSDK->SetTextStyleRef(right, smallStyle);
		chains[index] = gSDK->CreateChainDimension(left, right);
		if (chains[index] == nil)
		{
			probe.fail(std::string("CreateChainDimension が nil を返した: ") + kRowNames[index]);
			return;
		}
	}
	// H2 は H1 と同じ作りの別の 1 本（7001+7002）。掛け直す相手を変えて比べる。
	{
		const double y = 15000.0;
		MCObjectHandle left = P164MakeDim(y, 0.0, 7001.0);
		MCObjectHandle right = P164MakeDim(y, 7001.0, 14002.0);
		if (left == nil || right == nil)
		{
			probe.fail("H2 の直線寸法を作れなかった");
			return;
		}
		gSDK->SetTextStyleRef(left, smallStyle);
		gSDK->SetTextStyleRef(right, smallStyle);
		chains[5] = gSDK->CreateChainDimension(left, right);
		if (chains[5] == nil)
		{
			probe.fail("H2 の CreateChainDimension が nil を返した");
			return;
		}
	}
	MCObjectHandle chainA = chains[0];
	MCObjectHandle chainK1 = chains[1];
	MCObjectHandle chainK2 = chains[2];
	MCObjectHandle chainH0 = chains[3];
	MCObjectHandle chainH1 = chains[4];
	MCObjectHandle chainH2 = chains[5];
	probe.log("    6 本とも作れた（A / K1 / K2 / H0 / H1 / H2）");
	probe.log("");

	// =====================================================================
	probe.log("【4】6 本とも 1/125 の注釈へ移す（移動は引き直しではない＝#161）");
	{
		MCObjectHandle moved[6] = {chainA, chainK1, chainK2, chainH0, chainH1, chainH2};
		const char* names[6] = {"A", "K1", "K2", "H0", "H1", "H2"};
		for (int index = 0; index < 6; ++index)
			gSDK->AddViewportAnnotationObject(vp125, moved[index]);
		gSDK->UpdateViewport(vp125);
		probe.log("    移した後（直す前の出発点。6 本とも同じ値のはず）:");
		for (int index = 0; index < 6; ++index)
		{
			P164Dump(probe, std::string(names[index]) + " 出発点", moved[index], kP164VpScale);
			P164DumpMembers(probe, std::string(names[index]) + " 出発点", moved[index],
							kP164VpScale);
		}
	}
	probe.log("");

	// =====================================================================
	probe.log("【5】1:1 がアクティブなままできる直し（K1 / K2 / H0）");
	probe.log("    アクティブ: " + P164ActiveLayerText());
	probe.log("");

	probe.log("  K1: 中の型 63 へ ovDimFontSize ＝ " + P164Num(kP164FontSize125) +
			  " → **ResetObject(型 86)**");
	probe.log("     ＝#155 は by-class で「捨てられる」と実測した道。明示ありならどうか");
	{
		const std::vector<MCObjectHandle> members = P164Members(chainK1);
		for (size_t index = 0; index < members.size(); ++index)
			probe.log("    K1 中の型 63 #" + P164Int(static_cast<Sint32>(index + 1)) +
					  " へ書けたか=" +
					  P164Bool(P164SetReal(members[index], ovDimFontSize, kP164FontSize125)));
		P164Dump(probe, "K1 書いた直後（ResetObject の前）", chainK1, kP164VpScale);
		P164DumpMembers(probe, "K1 書いた直後", chainK1, kP164VpScale);
		gSDK->ResetObject(chainK1);
		P164Dump(probe, "K1 ResetObject の後", chainK1, kP164VpScale);
		P164DumpMembers(probe, "K1 ResetObject の後", chainK1, kP164VpScale);
	}
	probe.log("");

	probe.log("  K2: 同じ書き込み。**ResetObject を呼ばない**（対照）");
	{
		const std::vector<MCObjectHandle> members = P164Members(chainK2);
		for (size_t index = 0; index < members.size(); ++index)
			P164SetReal(members[index], ovDimFontSize, kP164FontSize125);
		P164Dump(probe, "K2 書いただけ", chainK2, kP164VpScale);
		P164DumpMembers(probe, "K2 書いただけ", chainK2, kP164VpScale);
	}
	probe.log("");

	probe.log("  H0: 型 86 へ SetTextStyleRef を掛け直す（**1:1 がアクティブなまま**）");
	probe.log("     ＝「掛け直し」だけで直るのか、「1/125 の文脈」が要るのかを分ける");
	{
		gSDK->SetTextStyleRef(chainH0, smallStyle);
		P164Dump(probe, "H0 掛け直した直後", chainH0, kP164VpScale);
		P164DumpMembers(probe, "H0 掛け直した直後", chainH0, kP164VpScale);
		gSDK->ResetObject(chainH0);
		P164Dump(probe, "H0 ResetObject の後", chainH0, kP164VpScale);
		P164DumpMembers(probe, "H0 ResetObject の後", chainH0, kP164VpScale);
	}
	probe.log("");

	// =====================================================================
	probe.log("【6】1/125 のデザインレイヤをアクティブにしてから H1 / H2 を掛け直す");
	probe.log("   ＝OIP の選び直し（注釈の編集中＝ビューポートの縮尺の文脈）の機械的な模倣");
	{
		MCObjectHandle workLayer = gSDK->CreateLayer("プローブ164 作業 1-125", kLayerDesign);
		if (workLayer == nil)
		{
			probe.fail("CreateLayer(kLayerDesign) が nil を返した");
			return;
		}
		gSDK->SetLayerScaleN(workLayer, kP164VpScale);
		probe.log("    アクティブ: " + P164ActiveLayerText());
		probe.log("");

		probe.log("  H1: **型 86 へ** SetTextStyleRef を掛け直す（H0 と同じ手・文脈だけ違う）");
		gSDK->SetTextStyleRef(chainH1, smallStyle);
		P164Dump(probe, "H1 掛け直した直後", chainH1, kP164VpScale);
		P164DumpMembers(probe, "H1 掛け直した直後", chainH1, kP164VpScale);
		gSDK->ResetObject(chainH1);
		P164Dump(probe, "H1 ResetObject の後", chainH1, kP164VpScale);
		P164DumpMembers(probe, "H1 ResetObject の後", chainH1, kP164VpScale);
		probe.log("");

		probe.log("  H2: **中の型 63 へ** SetTextStyleRef を掛け直す（別の 1 本なので");
		probe.log("     H1 の直しに巻き込まれない——1 巡目の取りこぼしはここ）");
		const std::vector<MCObjectHandle> members = P164Members(chainH2);
		probe.log("    H2 中の型 63 は " + P164Int(static_cast<Sint32>(members.size())) + " 本");
		for (size_t index = 0; index < members.size(); ++index)
			gSDK->SetTextStyleRef(members[index], smallStyle);
		P164Dump(probe, "H2 掛け直した直後", chainH2, kP164VpScale);
		P164DumpMembers(probe, "H2 掛け直した直後", chainH2, kP164VpScale);
		gSDK->ResetObject(chainH2);
		P164Dump(probe, "H2 ResetObject の後", chainH2, kP164VpScale);
		P164DumpMembers(probe, "H2 ResetObject の後", chainH2, kP164VpScale);
	}
	probe.log("");

	// =====================================================================
	probe.log("【7】最後に UpdateViewport を通して 6 本とも測り直す");
	gSDK->UpdateViewport(vp125);
	{
		MCObjectHandle all[6] = {chainA, chainK1, chainK2, chainH0, chainH1, chainH2};
		const char* names[6] = {"A", "K1", "K2", "H0", "H1", "H2"};
		for (int index = 0; index < 6; ++index)
		{
			P164Dump(probe, std::string(names[index]) + " 最終", all[index], kP164VpScale);
			P164DumpMembers(probe, std::string(names[index]) + " 最終", all[index], kP164VpScale);
		}
	}
	probe.log("");

	// =====================================================================
	probe.log("【8】読み方");
	probe.log("  * A は最後まで **×** のはず（何もしていない対照）。**★になったら、");
	probe.log("    どれかの直しが他の行へ漏れているということなので、まずそこを疑う。**");
	probe.log("  * **K1 が ★ なら、後から直す道がある**——中の型 63 へ ovDimFontSize を");
	probe.log("    書いて連続寸法を ResetObject すればよい（#155 の「捨てられる」は");
	probe.log("    by-class 限定だったことになる）。K2（引き直し無し）は × のはず。");
	probe.log("  * **H0 と H1 の差が「縮尺の文脈」の答え**——H0 が × で H1 が ★ なら、");
	probe.log("    掛け直しはビューポートの縮尺の文脈でしか効かない（＝SDK からは K1 か、");
	probe.log("    作るときに正しく作る道を使うしかない）。両方 ★ なら掛け直しそのものが");
	probe.log("    効いていて、アクティブレイヤの縮尺は関係が無い。");
	probe.log("  * **H1 と H2 の差が「どこへ掛けるか」の答え。** 1 巡目は型 86 へ掛けた");
	probe.log("    直後に中の型 63 が古いままで、ResetObject(型 86) で揃った。H2 は");
	probe.log("    中へ直に掛けるので、掛けた直後から揃うはず。");
	probe.log("");
	probe.log("おわり。**この図面は保存しないこと。**");
}
