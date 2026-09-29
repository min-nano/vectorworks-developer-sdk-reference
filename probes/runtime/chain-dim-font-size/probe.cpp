//
//	probes/runtime/chain-dim-font-size/probe.cpp
//
//	[issue #155] **連続寸法（`CreateChainDimension`）の中の直線寸法の文字の大きさ
//	（`ovDimFontSize`）は、何が決めているのか。** 3 回目。
//
//	ここまでの実測（1 回目 7a9f576e446a / 2 回目 889ded857aab）:
//
//	  * 繋ぐと中の直線寸法は**作り直され**（手が変わる）、繋ぐ前に書いた値は消えて
//	    1:1 の値（2.1167）になった。変わる欄は `ov17`（＝`ovDimFontSize`）と、
//	    そこから計算される `ov40` の 2 つだけ。
//	  * 繋いだ後に中へ書ける（読み戻せる）が、**絵は変わらない**。注釈へ移しても
//	    `UpdateViewport` でも絵は 2.1167 のままで、**`ResetObject` を呼ぶと中が
//	    作り直されて書いた値も消えた**（注釈（1/50 のビューポート）の中で呼んでも
//	    2.1167 のまま——**容れ物の縮尺では解き直されない**）。
//	  * **対照（2 回目）: 単独の直線寸法は書けば効く。** `ovDimFontSize` を書いて
//	    `ResetObject` を呼ぶと、絵（描かれている文字図形）もその大きさになった。
//	    つまり `ResetObject` は絵を描き直す引き金ではあり、**連続寸法だけが中の値を
//	    捨てている**。
//	  * **決め手の手掛かり（1 回目）: 中の 63 が 105.833 のときに、連続寸法そのもの
//	    （型 86）の `ovDimFontSize` を読むと 2.11667 だった。** 書くと `true` が返り、
//	    読み戻しは 105.833 になった。**つまり連続寸法は自分の値を持っていて、中の
//	    直線寸法はそこから作り直される写しではないか。**
//
//	3 回目はその 1 点を決める。
//
//	  1. 【本命】1:1 で作って繋ぎ、**連続寸法そのものへ** `ovDimFontSize` を書いて
//	     `ResetObject`。中の直線寸法と絵はその大きさになるか。注釈へ移して
//	     `UpdateViewport`・もう一度 `ResetObject` でも残るか。
//	  2. **中へ書くのと連続寸法へ書くのはどちらが勝つか**（中へ別の値を書いてから
//	     連続寸法へ書いて `ResetObject`）。
//	  3. **作り直しは「そのときのアクティブレイヤの縮尺」で解き直しているのか。**
//	     1/50 のデザインレイヤをアクティブにしてから、注釈の中の連続寸法へ
//	     `ResetObject` を呼ぶ。1:1 生まれで何も書いていないものが 105.833 になるなら
//	     アクティブレイヤが効いている（＝アクティブ次第で崩れる）。2.1167 のままなら
//	     連続寸法が持っている値が本当の値である。
//
//	**新規の空図面で走らせる。** 試験用のシートレイヤ・デザインレイヤ・ビューポートを
//	足すので、走らせた後は保存しないこと。
//

#include "Probe.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{
	// 紙で見せたい大きさと、注釈を置くビューポートの縮尺。
	const double kProbePaperPt = 6.0;
	const double kProbeVpScale = 50.0;
	// 6pt × 25.4/72 × 50 = 105.8333mm（Findings「Dimensions」の式）。
	const double kProbeTargetFontSize = kProbePaperPt * 25.4 / 72.0 * kProbeVpScale;
	// 1:1 で作ったときに焼き付く値（＝紙の 6pt そのまま）。
	const double kProbeFontSizeAt1To1 = kProbePaperPt * 25.4 / 72.0;

	// -------------------------------------------------------------------------
	std::string ProbeFormatNumber(double value)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.6g", value);
		return std::string(buf);
	}

	std::string ProbeFormatInt(long long value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%lld", value);
		return std::string(buf);
	}

	// 手（ハンドル）の同一性だけを言う。**中身は触らない**（作り直された後の古い手は
	// 無効になっているかもしれないので、逆参照してはいけない）。
	std::string ProbeHandleDigest(MCObjectHandle handle)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", static_cast<const void*>(handle));
		return std::string(buf);
	}

	std::string ProbeDescribeBlock(const TVariableBlock& block)
	{
		std::string out = "t" + ProbeFormatInt(static_cast<long long>(block.GetType())) + ":";

		Real64 real = 0.0;
		if (block.GetReal64(real))
			return out + ProbeFormatNumber(real);

		Sint32 s32 = 0;
		if (block.GetSint32(s32))
			return out + ProbeFormatInt(s32);

		Sint16 s16 = 0;
		if (block.GetSint16(s16))
			return out + ProbeFormatInt(s16);

		bool flag = false;
		if (block.GetBoolean(flag))
			return out + (flag ? "true" : "false");

		TXString text;
		if (block.GetTXString(text))
			return out + "\"" + std::string(static_cast<const char*>(text)) + "\"";

		return out + "?";
	}

	std::string ProbeReadVariable(MCObjectHandle object, short selector)
	{
		TVariableBlock block;
		if (object == nil || !gSDK->GetObjectVariable(object, selector, block))
			return std::string("(読めない)");
		return ProbeDescribeBlock(block);
	}

	// -------------------------------------------------------------------------
	bool ProbeGetFontSize(MCObjectHandle object, double& outValue)
	{
		TVariableBlock block;
		if (object == nil || !gSDK->GetObjectVariable(object, ovDimFontSize, block))
			return false;
		Real64 value = 0.0;
		if (!block.GetReal64(value))
			return false;
		outValue = value;
		return true;
	}

	// 「105.833mm（紙の上で 6pt）」の形で言う。紙換算はビューポートの縮尺で割る。
	std::string ProbePaperText(double value)
	{
		return ProbeFormatNumber(value) + "mm（1/" + ProbeFormatNumber(kProbeVpScale) +
			   " の紙の上で " + ProbeFormatNumber(value / kProbeVpScale * 72.0 / 25.4) + "pt）";
	}

	std::string ProbeFontSizeText(MCObjectHandle object)
	{
		double value = 0.0;
		if (!ProbeGetFontSize(object, value))
			return "(読めない)";
		return ProbePaperText(value);
	}

	bool ProbeSetFontSize(MCObjectHandle object, double value)
	{
		if (object == nil)
			return false;
		TVariableBlock block;
		block = static_cast<Real64>(value);
		return gSDK->SetObjectVariable(object, ovDimFontSize, block) != 0;
	}

	bool ProbeSetBoolean(MCObjectHandle object, short selector, bool value)
	{
		if (object == nil)
			return false;
		TVariableBlock block;
		block = static_cast<Boolean>(value ? 1 : 0);
		return gSDK->SetObjectVariable(object, selector, block) != 0;
	}

	std::string ProbeBoundsText(MCObjectHandle object)
	{
		WorldRect bounds;
		if (object == nil || !gSDK->GetObjectBounds(object, bounds))
			return "(外接矩形が取れない)";
		const double width = static_cast<double>(bounds.right) - static_cast<double>(bounds.left);
		const double height = static_cast<double>(bounds.top) - static_cast<double>(bounds.bottom);
		return "幅=" + ProbeFormatNumber(width) + " 高さ=" + ProbeFormatNumber(height);
	}

	std::string ProbeTypeText(MCObjectHandle object)
	{
		if (object == nil)
			return "nil";
		const short type = gSDK->GetObjectTypeN(object);
		std::string out = "型" + ProbeFormatInt(type);
		if (type == kParametricNode)
		{
			VWParametricObj parametric(object);
			out +=
				"(" + std::string(static_cast<const char*>(parametric.GetParametricName())) + ")";
		}
		return out;
	}

	// -------------------------------------------------------------------------
	// **その図形の中に実際に描かれている文字図形（型 10）の 1 文字目の大きさ。**
	// 「絵で値が見えるか」を目視に頼らずに測るための物差し。深さ 4 段まで。
	bool ProbeFindTextSize(MCObjectHandle container, int depth, double& outSizeMM)
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
			if (type == 0)
				break; // kTermNode（walk の終端）
			if (type == kTextNode)
			{
				WorldCoord charSize = 0;
				gSDK->GetTextSize(member, 1, charSize);
				outSizeMM = static_cast<double>(charSize);
				return true;
			}
			if (ProbeFindTextSize(member, depth + 1, outSizeMM))
				return true;
		}
		return false;
	}

	std::string ProbeDrawnTextText(MCObjectHandle object)
	{
		double sizeMM = 0.0;
		if (!ProbeFindTextSize(object, 0, sizeMM))
			return "文字図形は無い（値が描かれていない）";
		return "描かれている文字 " + ProbePaperText(sizeMM);
	}

	// 直線寸法 1 本を 2 行で書き出す（どの段でも同じ並びで出す）。
	void ProbeLogDim(vwprobe::Report& probe, const std::string& indent, const std::string& label,
					 MCObjectHandle dim)
	{
		probe.log(indent + label + " " + ProbeHandleDigest(dim) +
				  " ovDimFontSize=" + ProbeFontSizeText(dim));
		probe.log(indent + "  外接矩形: " + ProbeBoundsText(dim) + " / " + ProbeDrawnTextText(dim) +
				  " / 規格名=" + ProbeReadVariable(dim, ovDimStandardName));
	}

	// 容れ物の直下から、ある型の図形を集める（終端 0 で打ち切る）。
	std::vector<MCObjectHandle> ProbeMembersOfType(MCObjectHandle container, short wanted)
	{
		std::vector<MCObjectHandle> out;
		if (container == nil)
			return out;
		size_t guard = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(container); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (++guard > 100)
				break;
			const short type = gSDK->GetObjectTypeN(member);
			if (type == 0)
				break;
			if (type == wanted)
				out.push_back(member);
		}
		return out;
	}

	// **連続寸法の今の状態。** 直下の直線寸法（本体）と、2D 表現のグループ（型 11）の
	// 中の写しを**分けて**出す——1 回目の実測で、型 11 の中だけ古い大きさの文字が
	// 残っていたため。
	void ProbeLogChain(vwprobe::Report& probe, const std::string& label, MCObjectHandle chain)
	{
		probe.log("  [" + label + "]");
		probe.log("    連続寸法: " + ProbeTypeText(chain) + " " + ProbeHandleDigest(chain) + " " +
				  ProbeBoundsText(chain));
		const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chain, dimHeaderNode);
		for (size_t index = 0; index < dims.size(); ++index)
			ProbeLogDim(probe, "    ",
						"直下の 63[" + ProbeFormatInt(static_cast<long long>(index)) + "]",
						dims[index]);
		const std::vector<MCObjectHandle> groups = ProbeMembersOfType(chain, kGroupNode);
		for (size_t g = 0; g < groups.size(); ++g)
		{
			probe.log("    2D 表現のグループ 型11[" + ProbeFormatInt(static_cast<long long>(g)) +
					  "] " + ProbeHandleDigest(groups[g]) + " " + ProbeBoundsText(groups[g]));
			const std::vector<MCObjectHandle> inner = ProbeMembersOfType(groups[g], dimHeaderNode);
			for (size_t index = 0; index < inner.size(); ++index)
				ProbeLogDim(probe, "      ",
							"型11 の中の 63[" + ProbeFormatInt(static_cast<long long>(index)) + "]",
							inner[index]);
			if (inner.empty())
				probe.log("      型11 の中に直線寸法は無い / " + ProbeDrawnTextText(groups[g]));
		}
	}

	double ProbeLayerScale(MCObjectHandle layer)
	{
		if (layer == nil)
			return 0.0;
		double_gs scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return static_cast<double>(scale);
	}

	std::string ProbeActiveLayerText()
	{
		MCObjectHandle active = gSDK->GetActiveLayer();
		if (active == nil)
			return "（アクティブレイヤが無い）";
		TXString name;
		gSDK->GetObjectName(active, name);
		return "\"" + std::string(static_cast<const char*>(name)) + "\" 縮尺=1/" +
			   ProbeFormatNumber(ProbeLayerScale(active));
	}

	// 端点を共有する水平な直線寸法 2 本（繋げる条件を満たす形）。アクティブレイヤに入る。
	bool ProbeCreateTwoDims(vwprobe::Report& probe, double baseY, MCObjectHandle& outFirst,
							MCObjectHandle& outSecond)
	{
		const WorldCoord offset = static_cast<WorldCoord>(500.0);
		outFirst = gSDK->CreateLinearDimension(WorldPt(0.0, baseY), WorldPt(1000.0, baseY), offset,
											   0, Vector2(0, 0), 0);
		outSecond = gSDK->CreateLinearDimension(WorldPt(1000.0, baseY), WorldPt(2000.0, baseY),
												offset, 0, Vector2(0, 0), 0);
		if (outFirst == nil || outSecond == nil)
		{
			probe.fail("CreateLinearDimension が nil を返した（y=" + ProbeFormatNumber(baseY) +
					   "）");
			return false;
		}
		ProbeSetBoolean(outFirst, ovDimShowValue, true);
		ProbeSetBoolean(outSecond, ovDimShowValue, true);
		return true;
	}

	// 「いま入っている容れ物の縮尺で解いた値」と読み比べるための判定。
	std::string ProbeVerdict(MCObjectHandle dim)
	{
		double value = 0.0;
		if (!ProbeGetFontSize(dim, value))
			return "（読めない）";
		const double diffTarget = value > kProbeTargetFontSize ? value - kProbeTargetFontSize
															   : kProbeTargetFontSize - value;
		const double diff1To1 = value > kProbeFontSizeAt1To1 ? value - kProbeFontSizeAt1To1
															 : kProbeFontSizeAt1To1 - value;
		if (diffTarget < 0.01)
			return "ビューポートの縮尺で解いた値（" + ProbeFormatNumber(kProbeTargetFontSize) +
				   "）＝紙で 6pt";
		if (diff1To1 < 0.01)
			return "1:1 の値（" + ProbeFormatNumber(kProbeFontSizeAt1To1) +
				   "）＝紙で 0.12pt（絵に値が出ない大きさ）";
		return "どちらでもない値";
	}
} // namespace

VW_PROBE("chain-dim-font-size", "連続寸法と寸法の文字の大きさ（3 回目）",
		 "連続寸法そのものへ書く道と、アクティブレイヤで解き直すのかを決める")
{
	probe.log("【新規の空図面で走らせる】試験用のレイヤとビューポートを足すので、");
	probe.log("走らせた後は保存しないこと。");
	probe.log("目標: 紙で " + ProbeFormatNumber(kProbePaperPt) + "pt ＝ 1/" +
			  ProbeFormatNumber(kProbeVpScale) + " のビューポートでは " +
			  ProbeFormatNumber(kProbeTargetFontSize) + "mm（1:1 で作ると " +
			  ProbeFormatNumber(kProbeFontSizeAt1To1) + "mm）");
	probe.log("走り出しのアクティブレイヤ: " + ProbeActiveLayerText());

	// =====================================================================
	probe.log("");
	probe.log("== 0. 試験用のシートレイヤ（1:1 がアクティブになる）と 1/" +
			  ProbeFormatNumber(kProbeVpScale) + " のビューポート");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("VW調査155 試験シート3", kLayerSheet);
	if (sheetLayer == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	probe.log("アクティブ: " + ProbeActiveLayerText());
	MCObjectHandle viewport = gSDK->CreateViewport(sheetLayer);
	if (viewport == nil)
	{
		probe.fail("CreateViewport が nil を返した（この調査は注釈が要る）");
		return;
	}
	{
		TVariableBlock scaleBlock;
		scaleBlock = static_cast<Real64>(kProbeVpScale);
		probe.log("ビューポートの縮尺を 1/" + ProbeFormatNumber(kProbeVpScale) + " へ書いた=" +
				  (gSDK->SetObjectVariable(viewport, ovViewportScale, scaleBlock) != 0 ? "成功"
																					   : "失敗") +
				  " 読み戻し=" + ProbeReadVariable(viewport, ovViewportScale));
	}

	// =====================================================================
	probe.log("");
	probe.log("== 1. 【本命】連続寸法そのものへ ovDimFontSize を書いて ResetObject");
	MCObjectHandle dimA = nil;
	MCObjectHandle dimB = nil;
	if (!ProbeCreateTwoDims(probe, 0.0, dimA, dimB))
		return;
	MCObjectHandle chainA = gSDK->CreateChainDimension(dimA, dimB);
	if (chainA == nil)
	{
		probe.fail("CreateChainDimension が nil を返した");
		return;
	}
	probe.log("  繋いだ直後の**連続寸法そのもの**の ovDimFontSize=" + ProbeFontSizeText(chainA));
	ProbeLogChain(probe, "繋いだ直後（まだシートレイヤの上）", chainA);

	probe.log("  連続寸法そのものへ " + ProbeFormatNumber(kProbeTargetFontSize) + "mm を書けた=" +
			  std::string(ProbeSetFontSize(chainA, kProbeTargetFontSize) ? "true" : "false") +
			  " 読み戻し=" + ProbeFontSizeText(chainA));
	ProbeLogChain(probe, "連続寸法へ書いた直後（ResetObject はまだ）", chainA);

	probe.log("  連続寸法へ ResetObject を呼ぶ ← ここが今回の主役");
	gSDK->ResetObject(chainA);
	probe.log("  ResetObject 後の連続寸法そのものの ovDimFontSize=" + ProbeFontSizeText(chainA));
	ProbeLogChain(probe, "連続寸法へ書いて ResetObject した後", chainA);
	{
		const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chainA, dimHeaderNode);
		if (!dims.empty())
			probe.log("  【答え 1】連続寸法へ書いて ResetObject した後の中の 63[0] は " +
					  ProbeVerdict(dims[0]) + " ／ " + ProbeDrawnTextText(dims[0]) +
					  " ← 両方が 105.833 なら**連続寸法そのものへ書くのが正しい手順**");
	}

	probe.log(
		"  AddViewportAnnotationObject(連続寸法)=" +
		std::string(gSDK->AddViewportAnnotationObject(viewport, chainA) != 0 ? "true" : "false"));
	gSDK->UpdateViewport(viewport);
	probe.log("  注釈へ移して UpdateViewport した");
	ProbeLogChain(probe, "注釈へ移して更新した後", chainA);
	gSDK->ResetObject(chainA);
	probe.log("  注釈の中でもう一度 ResetObject した");
	ProbeLogChain(probe, "注釈の中で ResetObject した後", chainA);

	// =====================================================================
	probe.log("");
	probe.log("== 2. 中へ書くのと連続寸法へ書くのはどちらが勝つか");
	MCObjectHandle chainB = nil;
	{
		MCObjectHandle dimC = nil;
		MCObjectHandle dimD = nil;
		if (ProbeCreateTwoDims(probe, 4000.0, dimC, dimD))
		{
			chainB = gSDK->CreateChainDimension(dimC, dimD);
			if (chainB == nil)
			{
				probe.log("  CreateChainDimension が nil");
			}
			else
			{
				const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chainB, dimHeaderNode);
				for (size_t index = 0; index < dims.size(); ++index)
					probe.log("  中の 63[" + ProbeFormatInt(static_cast<long long>(index)) +
							  "] へ 999mm を書けた=" +
							  (ProbeSetFontSize(dims[index], 999.0) ? "true" : "false") +
							  " 読み戻し=" + ProbeFontSizeText(dims[index]));
				probe.log(
					"  連続寸法そのものへ " + ProbeFormatNumber(kProbeTargetFontSize) +
					"mm を書けた=" +
					std::string(ProbeSetFontSize(chainB, kProbeTargetFontSize) ? "true" : "false") +
					" 読み戻し=" + ProbeFontSizeText(chainB));
				gSDK->ResetObject(chainB);
				ProbeLogChain(probe, "999 と 105.833 を書いて ResetObject した後", chainB);
				const std::vector<MCObjectHandle> after = ProbeMembersOfType(chainB, dimHeaderNode);
				if (!after.empty())
					probe.log("  【答え 2】中の 63[0] は " + ProbeFontSizeText(after[0]) +
							  " ← 105.833 なら連続寸法の値が勝つ（中へ書くのは無駄）、"
							  "999 なら中の値が残る");
				probe.log("  AddViewportAnnotationObject=" +
						  std::string(gSDK->AddViewportAnnotationObject(viewport, chainB) != 0
										  ? "true"
										  : "false"));
				gSDK->UpdateViewport(viewport);
			}
		}
	}

	// =====================================================================
	probe.log("");
	probe.log("== 3. 何も書いていない連続寸法を、1:1 のまま注釈へ入れておく（対照の下ごしらえ）");
	MCObjectHandle chainC = nil;
	{
		MCObjectHandle dimE = nil;
		MCObjectHandle dimF = nil;
		if (ProbeCreateTwoDims(probe, 8000.0, dimE, dimF))
		{
			chainC = gSDK->CreateChainDimension(dimE, dimF);
			if (chainC == nil)
			{
				probe.log("  CreateChainDimension が nil");
			}
			else
			{
				probe.log("  何も書かずに注釈へ移す: AddViewportAnnotationObject=" +
						  std::string(gSDK->AddViewportAnnotationObject(viewport, chainC) != 0
										  ? "true"
										  : "false"));
				gSDK->UpdateViewport(viewport);
				probe.log("  連続寸法そのものの ovDimFontSize=" + ProbeFontSizeText(chainC));
				ProbeLogChain(probe, "1:1 生まれ・何も書かず・注釈の中", chainC);
			}
		}
	}

	// =====================================================================
	probe.log("");
	probe.log("== 4. 【切り分け】1/" + ProbeFormatNumber(kProbeVpScale) +
			  " のデザインレイヤをアクティブにしてから ResetObject");
	{
		MCObjectHandle designLayer = gSDK->CreateLayer("VW調査155 試験デザイン3", kLayerDesign);
		if (designLayer == nil)
		{
			probe.log("  CreateLayer(kLayerDesign) が nil。この切り分けは測れない");
		}
		else
		{
			gSDK->SetLayerScaleN(designLayer, kProbeVpScale);
			probe.log("  アクティブ: " + ProbeActiveLayerText() +
					  " ← これで「作り直しがアクティブレイヤの縮尺で解き直すのか」が分かる");
			if (chainC != nil)
			{
				gSDK->ResetObject(chainC);
				ProbeLogChain(probe,
							  "1:1 生まれ・何も書いていないものを 1/50 アクティブで ResetObject",
							  chainC);
				const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chainC, dimHeaderNode);
				if (!dims.empty())
					probe.log("  【答え 4】" + ProbeVerdict(dims[0]) + " ／ " +
							  ProbeDrawnTextText(dims[0]) +
							  " ← 105.833 になったならアクティブレイヤの縮尺で毎回解き直している"
							  "（＝アクティブ次第で崩れる）。2.1167 のままなら連続寸法が持って"
							  "いる値が本当の値である");
				probe.log("  連続寸法そのものの ovDimFontSize=" + ProbeFontSizeText(chainC));
			}
			if (chainA != nil)
			{
				gSDK->ResetObject(chainA);
				ProbeLogChain(probe,
							  "連続寸法へ 105.833 を書いたものを 1/50 アクティブで ResetObject",
							  chainA);
			}
			gSDK->UpdateViewport(viewport);
			probe.log("  UpdateViewport も呼んだ");
			if (chainA != nil)
				ProbeLogChain(probe, "最後（連続寸法へ書いたもの）", chainA);
			if (chainC != nil)
				ProbeLogChain(probe, "最後（何も書いていないもの）", chainC);
		}
	}

	probe.log("");
	probe.log("おわり。**この図面は保存しないこと**（試験用のレイヤ 2 枚とビューポートが残る）。");
}
