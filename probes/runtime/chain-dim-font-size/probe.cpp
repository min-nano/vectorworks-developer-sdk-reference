//
//	probes/runtime/chain-dim-font-size/probe.cpp
//
//	[issue #155] **連続寸法（`CreateChainDimension`）の中の直線寸法の文字の大きさ
//	（`ovDimFontSize`）は、いつ・何から解き直されるのか。**
//
//	1 回目の実測（PR #156 / ビルド 7a9f576e446a）で分かったこと:
//
//	  * 繋ぐと中の直線寸法は**作り直される**（手が両方変わる）。繋ぐ前に書いた
//	    `ovDimFontSize`（105.833）は消え、**1:1 の値（2.1167）に戻った**。
//	    繋ぐ前と繋いだ後で変わった欄は `ov17`（＝`ovDimFontSize`）と、それから
//	    計算される `ov40` の 2 つだけ。
//	  * 繋いだ後に中へ書ける（読み戻せる）が、**連続寸法へ `ResetObject` を呼ぶと
//	    中が作り直されて消える**。
//	  * **注釈へ移す・`UpdateViewport` では書いた値は消えないが、絵（描かれている文字
//	    図形）が 2.1167mm のまま**だった——値と絵が食い違う。
//	  * **ビューポートの縮尺を変えると中が作り直され、`ovDimFontSize` は
//	    「容れ物（ビューポート）の縮尺」で解き直された**（1/100 で 211.667、
//	    1/50 へ戻して 105.833）。そのとき絵もその大きさになった。**アクティブレイヤは
//	    ずっと 1:1 のシートレイヤだったので、効いているのは容れ物の縮尺である。**
//
//	**残っているのは「正しい手順」の詰め**（issue の問い 3）。1 回目は、注釈へ移した
//	直後に `ResetObject` を呼ぶ道を測っていない（代わりに縮尺を往復させてしまった）。
//	そこで 2 回目はそこだけを測る。
//
//	  1. **1:1 で作って繋ぎ、何も書かずに注釈へ移し、連続寸法へ `ResetObject`。**
//	     中の `ovDimFontSize` はビューポートの縮尺で解き直されるか。絵もそうなるか。
//	  2. 繋いだ後に書いてから注釈へ移し、`ResetObject`（書いた値が消えることの裏取り）。
//	  3. **2D 表現のグループ（型 11）の中の写し**が作り直しで入れ替わるか
//	     （1 回目は型 11 の中の文字が古い大きさのまま残っていた）。
//	  4. **対照: 単独の直線寸法**（連続寸法ではないもの）を同じ注釈へ入れて
//	     `ResetObject`。こちらは作り直されないので焼き付いたまま——「連続寸法だけが
//	     違う」を同じ 1 回の実行で言えるようにする。
//
//	**新規の空図面で走らせる。** 試験用のシートレイヤ・ビューポートを足すので、
//	走らせた後は保存しないこと。
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

VW_PROBE("chain-dim-font-size", "連続寸法と寸法の文字の大きさ（2 回目）",
		 "注釈へ移して ResetObject する道を測る・単独の寸法と見比べる")
{
	probe.log("【新規の空図面で走らせる】試験用のシートレイヤとビューポートを足すので、");
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
	MCObjectHandle sheetLayer = gSDK->CreateLayer("VW調査155 試験シート2", kLayerSheet);
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
	probe.log("== 1. 【本命】1:1 で作って繋ぎ、**何も書かずに**注釈へ移して ResetObject");
	MCObjectHandle dimA = nil;
	MCObjectHandle dimB = nil;
	if (!ProbeCreateTwoDims(probe, 0.0, dimA, dimB))
		return;
	probe.log("  1:1 生まれ（何も書いていない）:");
	ProbeLogDim(probe, "  ", "1 本目", dimA);
	MCObjectHandle chain = gSDK->CreateChainDimension(dimA, dimB);
	if (chain == nil)
	{
		probe.fail("CreateChainDimension が nil を返した");
		return;
	}
	ProbeLogChain(probe, "繋いだ直後（まだシートレイヤの上）", chain);

	probe.log(
		"  AddViewportAnnotationObject(連続寸法)=" +
		std::string(gSDK->AddViewportAnnotationObject(viewport, chain) != 0 ? "true" : "false"));
	ProbeLogChain(probe, "注釈へ移した直後（ResetObject はまだ呼んでいない）", chain);

	probe.log("  連続寸法へ ResetObject を呼ぶ ← ここが今回の主役");
	gSDK->ResetObject(chain);
	ProbeLogChain(probe, "注釈の中で ResetObject した後", chain);
	{
		const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chain, dimHeaderNode);
		if (!dims.empty())
		{
			probe.log("  【答え 1】注釈で ResetObject した後の直下の 63[0] は " +
					  ProbeVerdict(dims[0]));
			probe.log("  　　　　　 絵は: " + ProbeDrawnTextText(dims[0]));
			probe.log("  ↑ 両方が「ビューポートの縮尺で解いた値」なら、**1:1 で作って繋いでも、"
					  "注釈へ移して ResetObject するだけで紙の上で正しい大きさになる**");
		}
	}
	gSDK->UpdateViewport(viewport);
	probe.log("  UpdateViewport も呼んだ（絵の作り直しが更新に依るのかを見る）");
	ProbeLogChain(probe, "ResetObject → UpdateViewport の後", chain);

	// =====================================================================
	probe.log("");
	probe.log("== 2. 繋いだ後に 105.833 を書いてから注釈へ移し、ResetObject（裏取り）");
	MCObjectHandle dimC = nil;
	MCObjectHandle dimD = nil;
	if (ProbeCreateTwoDims(probe, 4000.0, dimC, dimD))
	{
		MCObjectHandle chain2 = gSDK->CreateChainDimension(dimC, dimD);
		if (chain2 == nil)
		{
			probe.log("  CreateChainDimension が nil");
		}
		else
		{
			const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chain2, dimHeaderNode);
			for (size_t index = 0; index < dims.size(); ++index)
				probe.log("  直下の 63[" + ProbeFormatInt(static_cast<long long>(index)) + "] へ " +
						  ProbeFormatNumber(kProbeTargetFontSize) + "mm を書けた=" +
						  (ProbeSetFontSize(dims[index], kProbeTargetFontSize) ? "true" : "false") +
						  " 読み戻し=" + ProbeFontSizeText(dims[index]));
			ProbeLogChain(probe, "書いた直後（まだシートレイヤの上）", chain2);
			probe.log("  AddViewportAnnotationObject=" +
					  std::string(gSDK->AddViewportAnnotationObject(viewport, chain2) != 0
									  ? "true"
									  : "false"));
			ProbeLogChain(probe, "注釈へ移した直後", chain2);
			gSDK->ResetObject(chain2);
			ProbeLogChain(probe, "注釈の中で ResetObject した後", chain2);
			const std::vector<MCObjectHandle> after = ProbeMembersOfType(chain2, dimHeaderNode);
			if (!after.empty())
				probe.log("  【答え 2】書いてから注釈で ResetObject した後の 63[0] は " +
						  ProbeVerdict(after[0]) + " ／ " + ProbeDrawnTextText(after[0]) +
						  " ← 書いた値が残るのではなく容れ物の縮尺で解き直されるなら、"
						  "**中へ書くのは無駄**（1 回目の結果と合わせて確定する）");
		}
	}

	// =====================================================================
	probe.log("");
	probe.log("== 3. 対照: 単独の直線寸法（連続寸法ではない）を同じ注釈へ入れて ResetObject");
	{
		const WorldCoord offset = static_cast<WorldCoord>(500.0);
		MCObjectHandle plain = gSDK->CreateLinearDimension(
			WorldPt(0.0, 8000.0), WorldPt(1000.0, 8000.0), offset, 0, Vector2(0, 0), 0);
		MCObjectHandle written = gSDK->CreateLinearDimension(
			WorldPt(0.0, 9000.0), WorldPt(1000.0, 9000.0), offset, 0, Vector2(0, 0), 0);
		if (plain == nil || written == nil)
		{
			probe.log("  CreateLinearDimension が nil。この対照は測れない");
		}
		else
		{
			ProbeSetBoolean(plain, ovDimShowValue, true);
			ProbeSetBoolean(written, ovDimShowValue, true);
			probe.log("  ①何も書かない 1 本 / ②105.833 を書く 1 本（どちらも 1:1 生まれ）");
			probe.log("  ②へ書けた=" + std::string(ProbeSetFontSize(written, kProbeTargetFontSize)
													   ? "true"
													   : "false"));
			gSDK->AddViewportAnnotationObject(viewport, plain);
			gSDK->AddViewportAnnotationObject(viewport, written);
			ProbeLogDim(probe, "  ", "①注釈へ移した直後", plain);
			ProbeLogDim(probe, "  ", "②注釈へ移した直後", written);
			gSDK->ResetObject(plain);
			gSDK->ResetObject(written);
			ProbeLogDim(probe, "  ", "①ResetObject した後", plain);
			ProbeLogDim(probe, "  ", "②ResetObject した後", written);
			gSDK->UpdateViewport(viewport);
			ProbeLogDim(probe, "  ", "①UpdateViewport の後", plain);
			ProbeLogDim(probe, "  ", "②UpdateViewport の後", written);
			probe.log("  【答え 3】単独の寸法は " + ProbeVerdict(plain) + "（①）／" +
					  ProbeVerdict(written) +
					  "（②）——①が 1:1 の値のままなら「単独の寸法は作るときの縮尺に焼き付いた"
					  "まま容れ物に従わない」、②が書いた値のままなら「単独なら書けば効く」で、"
					  "**連続寸法だけが違う**ことになる");
		}
	}

	probe.log("");
	probe.log("おわり。**この図面は保存しないこと**（試験用のシートレイヤとビューポートが残る）。");
}
