//
//	probes/runtime/chain-dim-font-size/probe.cpp
//
//	[issue #155] **連続寸法（`CreateChainDimension`）の中の直線寸法の文字の大きさ
//	（`ovDimFontSize`）は、何が決めているのか。** 4 回目（最後の 1 点）。
//
//	ここまでの実測（1 回目 7a9f576e446a / 2 回目 889ded857aab / 3 回目 d02b34dc3221）で、
//	**「後から直す道は無い」ことが出揃った**:
//
//	  * 繋ぐと中の直線寸法は作り直され、繋ぐ前に書いた `ovDimFontSize` は捨てられる。
//	  * 繋いだ後に中へ書けるが、`ResetObject` で捨てられる（絵は書いた時点でも変わらない）。
//	  * **連続寸法そのもの（型 86）へも書ける**（`true`・読み戻せる・`ResetObject` を
//	    越えても連続寸法自身は保っている）が、**中には効かない**（3 回目）。
//	  * 中へ 999・連続寸法へ 105.833 と別々に書いて `ResetObject` すると、中は
//	    **どちらでもない 2.1167**（＝繋いだときの値）になる。
//	  * 容れ物（1/50 のビューポートの注釈）でも、**アクティブレイヤを 1/50 にしてから**
//	    `ResetObject` しても 2.1167 のまま。つまり後からの解き直しは起きない。
//	  * 対照: **単独の直線寸法は書けば効く**（`ResetObject` で絵も変わる）。
//	  * 効いた道はひとつだけ——**1/50（＝ビューポートと同じ縮尺）がアクティブなうちに
//	    作って繋ぐ**と 105.833 になり、何も書かずに注釈へ入れて更新しても紙で 6pt だった。
//
//	**残る 1 点は「その『繋いだときの値』を決めているのはどちらか」。** これまでの回は
//	いつも「元の寸法を作ったときのアクティブレイヤ」と「繋ぐときのアクティブレイヤ」が
//	同じだったので、分けられていない。分けないと手順を書けない——元の寸法をどこで
//	作ってもよいのか、それとも寸法の作成そのものを縮尺の合ったレイヤで行う必要があるのか
//	が変わる。**双方向で測って決める。**
//
//	  A. **1:1 で作った 2 本を、1/50 がアクティブなときに繋ぐ。**
//	     105.833 なら「繋ぐときのアクティブ」が決めている。2.1167 なら「元の寸法の生まれ」。
//	  B. **1/50 で作った 2 本を、1:1 がアクティブなときに繋ぐ。**
//	     2.1167 なら「繋ぐときのアクティブ」、105.833 なら「元の寸法の生まれ」。
//	  C. 対照: 1:1 で作って 1:1 で繋ぐ（2.1167 になるはず）。
//
//	**新規の空図面で走らせる。** 試験用のシートレイヤ 2 枚・デザインレイヤ 1 枚を足すので、
//	走らせた後は保存しないこと（ビューポートは要らないので作らない）。
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

VW_PROBE("chain-dim-font-size", "連続寸法と寸法の文字の大きさ（4 回目）",
		 "繋ぐときのアクティブレイヤが決めるのか、元の寸法の生まれが決めるのか")
{
	probe.log("【新規の空図面で走らせる】試験用のレイヤを 3 枚足すので、走らせた後は保存しない");
	probe.log("こと。1:1 生まれの値=" + ProbeFormatNumber(kProbeFontSizeAt1To1) + "mm／1/" +
			  ProbeFormatNumber(kProbeVpScale) +
			  " 生まれの値=" + ProbeFormatNumber(kProbeTargetFontSize) + "mm（規格が紙で " +
			  ProbeFormatNumber(kProbePaperPt) + "pt のとき）");
	probe.log("走り出しのアクティブレイヤ: " + ProbeActiveLayerText());

	// =====================================================================
	probe.log("");
	probe.log(
		"== 0. シートレイヤ（1:1）をアクティブにして、1:1 生まれの 2 本を作る（まだ繋がない）");
	if (gSDK->CreateLayer("VW調査155 試験シート4a", kLayerSheet) == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	probe.log("  アクティブ: " + ProbeActiveLayerText());
	MCObjectHandle bornAt1To1First = nil;
	MCObjectHandle bornAt1To1Second = nil;
	if (!ProbeCreateTwoDims(probe, 0.0, bornAt1To1First, bornAt1To1Second))
		return;
	ProbeLogDim(probe, "  ", "1:1 生まれ 1 本目", bornAt1To1First);
	MCObjectHandle controlFirst = nil;
	MCObjectHandle controlSecond = nil;
	if (!ProbeCreateTwoDims(probe, 12000.0, controlFirst, controlSecond))
		return;
	probe.log("  （対照 C 用の 2 本も 1:1 のうちに作っておく）");

	// =====================================================================
	probe.log("");
	probe.log("== 1. 対照 C: 1:1 で作って 1:1 のまま繋ぐ");
	{
		MCObjectHandle chainC = gSDK->CreateChainDimension(controlFirst, controlSecond);
		if (chainC == nil)
			probe.log("  CreateChainDimension が nil");
		else
		{
			ProbeLogChain(probe, "1:1 で作って 1:1 で繋いだ", chainC);
			const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chainC, dimHeaderNode);
			if (!dims.empty())
				probe.log("  【対照 C】" + ProbeVerdict(dims[0]));
		}
	}

	// =====================================================================
	probe.log("");
	probe.log("== 2. デザインレイヤ（1/" + ProbeFormatNumber(kProbeVpScale) +
			  "）をアクティブにして、そこで 1/" + ProbeFormatNumber(kProbeVpScale) +
			  " 生まれの 2 本を作る");
	MCObjectHandle designLayer = gSDK->CreateLayer("VW調査155 試験デザイン4", kLayerDesign);
	if (designLayer == nil)
	{
		probe.fail("CreateLayer(kLayerDesign) が nil を返した");
		return;
	}
	gSDK->SetLayerScaleN(designLayer, kProbeVpScale);
	probe.log("  アクティブ: " + ProbeActiveLayerText());
	MCObjectHandle bornAt50First = nil;
	MCObjectHandle bornAt50Second = nil;
	if (!ProbeCreateTwoDims(probe, 4000.0, bornAt50First, bornAt50Second))
		return;
	ProbeLogDim(probe, "  ", "1/50 生まれ 1 本目", bornAt50First);

	// =====================================================================
	probe.log("");
	probe.log("== 3. 【A】1:1 で作った 2 本を、1/" + ProbeFormatNumber(kProbeVpScale) +
			  " がアクティブなときに繋ぐ");
	probe.log("  いまのアクティブ: " + ProbeActiveLayerText());
	{
		MCObjectHandle chainA = gSDK->CreateChainDimension(bornAt1To1First, bornAt1To1Second);
		if (chainA == nil)
			probe.log("  CreateChainDimension が nil（1:1 生まれの 2 本が繋がらなかった）");
		else
		{
			ProbeLogChain(probe, "A: 1:1 生まれ × 1/50 アクティブで繋いだ", chainA);
			const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chainA, dimHeaderNode);
			if (!dims.empty())
				probe.log("  【答え A】" + ProbeVerdict(dims[0]) +
						  " ← 105.833 なら**繋ぐときのアクティブレイヤ**が決めている。"
						  "2.1167 なら**元の寸法を作ったときの縮尺**が決めている");
		}
	}

	// =====================================================================
	probe.log("");
	probe.log("== 4. 【B】1/" + ProbeFormatNumber(kProbeVpScale) +
			  " で作った 2 本を、1:1 がアクティブなときに繋ぐ（逆向きの確認）");
	if (gSDK->CreateLayer("VW調査155 試験シート4b", kLayerSheet) == nil)
	{
		probe.log("  CreateLayer(kLayerSheet) が nil。B は測れない");
	}
	else
	{
		probe.log("  いまのアクティブ: " + ProbeActiveLayerText());
		MCObjectHandle chainB = gSDK->CreateChainDimension(bornAt50First, bornAt50Second);
		if (chainB == nil)
			probe.log("  CreateChainDimension が nil（1/50 生まれの 2 本が繋がらなかった）");
		else
		{
			ProbeLogChain(probe, "B: 1/50 生まれ × 1:1 アクティブで繋いだ", chainB);
			const std::vector<MCObjectHandle> dims = ProbeMembersOfType(chainB, dimHeaderNode);
			if (!dims.empty())
				probe.log("  【答え B】" + ProbeVerdict(dims[0]) +
						  " ← 2.1167 なら**繋ぐときのアクティブレイヤ**が決めている。"
						  "105.833 なら**元の寸法を作ったときの縮尺**が決めている");
			gSDK->ResetObject(chainB);
			ProbeLogChain(probe, "B を ResetObject した後（値が動くかの念押し）", chainB);
		}
	}

	probe.log("");
	probe.log("おわり。**この図面は保存しないこと**（試験用のレイヤ 3 枚が残っている）。");
}
