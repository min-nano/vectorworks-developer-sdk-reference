//
//	probes/runtime/chain-dim-font-size/probe.cpp
//
//	[issue #155] **`CreateChainDimension` で繋ぐと、中の直線寸法の文字の大きさ
//	（`ovDimFontSize`）はどうなるのか。** 利用側（プラグイン）では、シートレイヤ（1:1）が
//	アクティブなうちに直線寸法へ `ovDimFontSize = 6pt × 25.4/72 × ビューポート縮尺`
//	（＝ 1/125 で 264.583mm）を書き、読み戻しで入ったことを確かめたうえで連続寸法へ繋ぎ、
//	注釈へ移したのに、**実機の絵では値が出なかった**（寸法線は出ている）。
//
//	見立ては「繋ぐときに中の直線寸法が作り直され、そのときのアクティブレイヤの縮尺で
//	`ovDimFontSize` が焼き直されている」。それを次の順で測る。
//
//	  1. 1:1 がアクティブなまま、**繋ぐ前に書いた値**が繋いだ後の中の直線寸法に残るか
//	     （残らなければ、いくつになるか＝どの縮尺で焼き直されたか）。
//	  2. **繋いだ後に中の直線寸法へ書けるか。** 書いた値が
//	     `ResetObject`・注釈へ移す・`UpdateViewport`・ビューポート縮尺の変更を越えて残るか。
//	  3. 連続寸法そのもの（型 86）／中の 2D 表現のグループ（型 11）へ書けるか。
//	  4. 対照: **ビューポートと同じ縮尺（1/50）のデザインレイヤがアクティブなまま**
//	     作って繋いだら、中の値はいくつになるか。
//
//	**「絵で値が見えるか」は目視だが、ここでは機械で代わりに測る**——寸法の外接矩形の
//	高さ（文字が大きいほど高くなる。Findings「Dimensions」の #145 の物差し）と、
//	寸法の中に実際に描かれている文字図形（型 10）の文字の大きさを読む。
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

	// オブジェクト変数の総当りの範囲（寸法の ov* はこの辺に固まっている）。
	const short kProbeOvFirst = 0;
	const short kProbeOvLast = 60;

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

	// 手（ハンドル）の同一性だけを言う。**中身は触らない**（繋いだ後の古い手は
	// 無効になっているかもしれないので、逆参照してはいけない）。
	std::string ProbeHandleDigest(MCObjectHandle handle)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", static_cast<const void*>(handle));
		return std::string(buf);
	}

	// ov を 1 つ読んで文字列にする（型ごとの getter は型が合わないと false を返すので、
	// 順に試せば型名の列挙子に依存せずに済む。#151 のプローブと同じ手）。
	std::string ProbeDescribeBlock(const TVariableBlock& block)
	{
		std::string out = "t" + ProbeFormatInt(static_cast<long long>(block.GetType())) + ":";

		WorldPt pt;
		if (block.GetWorldPt(pt))
			return out + "pt(" + ProbeFormatNumber(pt.x) + "," + ProbeFormatNumber(pt.y) + ")";

		Real64 real = 0.0;
		if (block.GetReal64(real))
			return out + ProbeFormatNumber(real);

		Sint32 s32 = 0;
		if (block.GetSint32(s32))
			return out + ProbeFormatInt(s32);

		Sint16 s16 = 0;
		if (block.GetSint16(s16))
			return out + ProbeFormatInt(s16);

		Uint8 u8 = 0;
		if (block.GetUint8(u8))
			return out + ProbeFormatInt(static_cast<long long>(u8));

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
			return std::string();
		return ProbeDescribeBlock(block);
	}

	std::vector<std::string> ProbeSnapshotVariables(MCObjectHandle object)
	{
		std::vector<std::string> out;
		for (short selector = kProbeOvFirst; selector <= kProbeOvLast; ++selector)
			out.push_back(ProbeReadVariable(object, selector));
		return out;
	}

	void ProbeLogVariableDiff(vwprobe::Report& probe, const std::string& what,
							  const std::vector<std::string>& before,
							  const std::vector<std::string>& after)
	{
		size_t changed = 0;
		for (size_t index = 0; index < before.size() && index < after.size(); ++index)
		{
			if (before[index] == after[index])
				continue;
			++changed;
			const short selector = static_cast<short>(kProbeOvFirst + index);
			probe.log("    " + what + " ov" + ProbeFormatInt(selector) + ": " +
					  (before[index].empty() ? std::string("(読めない)") : before[index]) + " -> " +
					  (after[index].empty() ? std::string("(読めない)") : after[index]));
		}
		probe.log("    " + what +
				  " 変わった欄の数 = " + ProbeFormatInt(static_cast<long long>(changed)));
	}

	// -------------------------------------------------------------------------
	// 文字の大きさ（ovDimFontSize）を読む。読めなければ「(読めない)」。
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

	// 「105.8333mm（紙の上で 6pt）」の形で言う。紙換算は kProbeVpScale で割る。
	std::string ProbeFontSizeText(MCObjectHandle object)
	{
		double value = 0.0;
		if (!ProbeGetFontSize(object, value))
			return "(読めない)";
		const double paperPt = value / kProbeVpScale * 72.0 / 25.4;
		return ProbeFormatNumber(value) + "mm（1/" + ProbeFormatNumber(kProbeVpScale) +
			   " の紙の上で " + ProbeFormatNumber(paperPt) + "pt）";
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
			out += "(" + std::string(static_cast<const char*>(parametric.GetParametricName())) +
				   " id=" + ProbeFormatInt(static_cast<long long>(parametric.GetInternalID())) +
				   ")";
		}
		return out;
	}

	// -------------------------------------------------------------------------
	// **中に実際に描かれている文字図形を探す。** 「絵で値が見えるか」を目視に頼らずに
	// 測るための代わりの物差し: 文字図形（型 10）があるか・その文字の大きさは何 mm か。
	// 深さは 4 段まで（寸法 → 2D 表現のグループ → …）。
	void ProbeCollectDrawnText(MCObjectHandle container, int depth, std::vector<std::string>& out,
							   std::map<std::string, size_t>& typeCounts)
	{
		if (container == nil || depth > 4)
			return;
		size_t guard = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(container); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (++guard > 500)
				break;
			const short type = gSDK->GetObjectTypeN(member);
			if (type == 0)
				break; // kTermNode（walk の終端。Findings「Dimensions」）
			++typeCounts[ProbeTypeText(member)];
			if (type == kTextNode)
			{
				WorldCoord charSize = 0;
				gSDK->GetTextSize(member, 1, charSize);
				const double sizeMM = static_cast<double>(charSize);
				out.push_back("文字図形: 1 文字目の大きさ=" + ProbeFormatNumber(sizeMM) + "mm（1/" +
							  ProbeFormatNumber(kProbeVpScale) + " の紙の上で " +
							  ProbeFormatNumber(sizeMM / kProbeVpScale * 72.0 / 25.4) + "pt） " +
							  ProbeBoundsText(member));
			}
			ProbeCollectDrawnText(member, depth + 1, out, typeCounts);
		}
	}

	void ProbeLogDrawnText(vwprobe::Report& probe, const std::string& indent,
						   MCObjectHandle container)
	{
		std::vector<std::string> texts;
		std::map<std::string, size_t> typeCounts;
		ProbeCollectDrawnText(container, 0, texts, typeCounts);
		std::string census;
		for (std::map<std::string, size_t>::const_iterator it = typeCounts.begin();
			 it != typeCounts.end(); ++it)
		{
			if (!census.empty())
				census += ", ";
			census += it->first + " x" + ProbeFormatInt(static_cast<long long>(it->second));
		}
		probe.log(indent + "中に入っている図形: " + (census.empty() ? "（無）" : census));
		for (size_t index = 0; index < texts.size(); ++index)
			probe.log(indent + texts[index]);
	}

	// -------------------------------------------------------------------------
	// 連続寸法の中の直線寸法（型 63 = dimHeaderNode）を集める。
	std::vector<MCObjectHandle> ProbeChainMembers(MCObjectHandle chain)
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
			const short type = gSDK->GetObjectTypeN(member);
			if (type == 0)
				break; // 終端
			if (type == dimHeaderNode)
				out.push_back(member);
		}
		return out;
	}

	// 連続寸法の顔ぶれ（型の並び）を 1 行で。
	std::string ProbeChainShape(MCObjectHandle chain)
	{
		if (chain == nil)
			return "nil";
		std::string out;
		size_t guard = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(chain); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (++guard > 100)
				break;
			if (!out.empty())
				out += " / ";
			out += ProbeTypeText(member) + " " + ProbeHandleDigest(member);
			if (gSDK->GetObjectTypeN(member) == 0)
				break;
		}
		return out.empty() ? "（空）" : out;
	}

	// **連続寸法の今の状態を一通り書き出す。** ここが調査の主役なので、どの段でも同じ
	// 並びで出す（前後を目で突き合わせられるように）。
	void ProbeLogChain(vwprobe::Report& probe, const std::string& label, MCObjectHandle chain)
	{
		probe.log("  [" + label + "]");
		probe.log("    連続寸法そのもの: " + ProbeTypeText(chain) + " " + ProbeBoundsText(chain));
		probe.log("    顔ぶれ: " + ProbeChainShape(chain));
		const std::vector<MCObjectHandle> members = ProbeChainMembers(chain);
		probe.log("    中の直線寸法（型 63）= " +
				  ProbeFormatInt(static_cast<long long>(members.size())) + " 本");
		for (size_t index = 0; index < members.size(); ++index)
		{
			MCObjectHandle member = members[index];
			probe.log("    ・63[" + ProbeFormatInt(static_cast<long long>(index)) + "] " +
					  ProbeHandleDigest(member) + " ovDimFontSize=" + ProbeFontSizeText(member));
			probe.log(
				"      値を出すか(ovDimShowValue)=" + ProbeReadVariable(member, ovDimShowValue) +
				" ovDimTextSizeInPoints=" + ProbeReadVariable(member, ovDimTextSizeInPoints) +
				" 規格名=" + ProbeReadVariable(member, ovDimStandardName));
			probe.log("      外接矩形: " + ProbeBoundsText(member));
			ProbeLogDrawnText(probe, "      ", member);
		}
		ProbeLogDrawnText(probe, "    連続寸法の直下ふくみ: ", chain);
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

	// 水平に並んだ、端点を共有する直線寸法 2 本を作る（繋げる条件を満たす形）。
	// 作った寸法はアクティブレイヤに入る（Findings「Dimensions」）。
	void ProbeCreateTwoDims(vwprobe::Report& probe, double baseY, MCObjectHandle& outFirst,
							MCObjectHandle& outSecond)
	{
		const WorldCoord offset = static_cast<WorldCoord>(500.0);
		outFirst = gSDK->CreateLinearDimension(WorldPt(0.0, baseY), WorldPt(1000.0, baseY), offset,
											   0, Vector2(0, 0), 0);
		outSecond = gSDK->CreateLinearDimension(WorldPt(1000.0, baseY), WorldPt(2000.0, baseY),
												offset, 0, Vector2(0, 0), 0);
		if (outFirst == nil || outSecond == nil)
		{
			probe.fail("CreateLinearDimension が nil を返した（1 本目=" +
					   std::string(outFirst == nil ? "nil" : "有") +
					   " 2 本目=" + std::string(outSecond == nil ? "nil" : "有") + "）");
			return;
		}
		// 値を出す設定にしておく（既定でも true のはずだが、測る前提を揃える）。
		ProbeSetBoolean(outFirst, ovDimShowValue, true);
		ProbeSetBoolean(outSecond, ovDimShowValue, true);
	}
} // namespace

VW_PROBE(
	"chain-dim-font-size", "連続寸法へ繋ぐと中の寸法の文字の大きさは作り直されるか",
	"繋ぐ前に書いた ovDimFontSize が残るか・繋いだ後に書けるか・注釈と更新を越えて残るかを測る")
{
	probe.log("【新規の空図面で走らせる】試験用のシートレイヤ・デザインレイヤ・ビューポートを");
	probe.log("足すので、走らせた後は保存しないこと。");
	probe.log("目標の文字の大きさ: 紙で " + ProbeFormatNumber(kProbePaperPt) + "pt ＝ 1/" +
			  ProbeFormatNumber(kProbeVpScale) + " のビューポートでは " +
			  ProbeFormatNumber(kProbeTargetFontSize) + "mm");
	probe.log("1:1 で作ったときに焼き付く値（対照）: " + ProbeFormatNumber(kProbeFontSizeAt1To1) +
			  "mm");
	probe.log("走り出しのアクティブレイヤ: " + ProbeActiveLayerText());

	// =====================================================================
	probe.log("");
	probe.log("== 0. 試験用のシートレイヤとビューポート（縮尺 1/" +
			  ProbeFormatNumber(kProbeVpScale) + "）を作る");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("VW調査155 試験シート", kLayerSheet);
	if (sheetLayer == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	probe.log("シートレイヤを作った（これでアクティブが 1:1 になる）: " + ProbeActiveLayerText());

	MCObjectHandle viewport = gSDK->CreateViewport(sheetLayer);
	if (viewport == nil)
	{
		probe.log("CreateViewport が nil。注釈の段（3 以降）は測れない");
	}
	else
	{
		TVariableBlock scaleBlock;
		scaleBlock = static_cast<Real64>(kProbeVpScale);
		const bool wrote = gSDK->SetObjectVariable(viewport, ovViewportScale, scaleBlock) != 0;
		probe.log("ビューポートを作った。縮尺を 1/" + ProbeFormatNumber(kProbeVpScale) +
				  " へ書いた=" + (wrote ? "成功" : "失敗") +
				  " 読み戻し=" + ProbeReadVariable(viewport, ovViewportScale));
	}

	// =====================================================================
	probe.log("");
	probe.log("== 1. 【本題】1:1 がアクティブなまま、繋ぐ前に書いた値は残るか");
	MCObjectHandle dimA = nil;
	MCObjectHandle dimB = nil;
	ProbeCreateTwoDims(probe, 0.0, dimA, dimB);
	if (dimA == nil || dimB == nil)
		return;
	probe.log("作った直後（1:1 生まれ）:");
	probe.log("  1 本目 " + ProbeHandleDigest(dimA) + " ovDimFontSize=" + ProbeFontSizeText(dimA) +
			  " 外接矩形: " + ProbeBoundsText(dimA));
	probe.log("  2 本目 " + ProbeHandleDigest(dimB) + " ovDimFontSize=" + ProbeFontSizeText(dimB));

	probe.log("繋ぐ前に " + ProbeFormatNumber(kProbeTargetFontSize) + "mm を書く:");
	probe.log("  1 本目へ書けた=" +
			  std::string(ProbeSetFontSize(dimA, kProbeTargetFontSize) ? "true" : "false") +
			  " 読み戻し=" + ProbeFontSizeText(dimA));
	probe.log("  2 本目へ書けた=" +
			  std::string(ProbeSetFontSize(dimB, kProbeTargetFontSize) ? "true" : "false") +
			  " 読み戻し=" + ProbeFontSizeText(dimB));
	probe.log("  1 本目の外接矩形（文字が大きくなったぶん高くなる）: " + ProbeBoundsText(dimA));
	ProbeLogDrawnText(probe, "  1 本目の中身: ", dimA);

	const std::vector<std::string> beforeChain = ProbeSnapshotVariables(dimA);
	const std::string handleA = ProbeHandleDigest(dimA);
	const std::string handleB = ProbeHandleDigest(dimB);

	probe.log("CreateChainDimension(1 本目, 2 本目) を呼ぶ");
	MCObjectHandle chain = gSDK->CreateChainDimension(dimA, dimB);
	if (chain == nil)
	{
		probe.fail("CreateChainDimension が nil を返した（2 本が繋がる条件を満たしていない）");
		return;
	}
	probe.log("繋げた。**以後、繋ぐ前の手（" + handleA + " / " + handleB +
			  "）は逆参照しない**（無効になっているかもしれないので、同一性の比較だけに使う）");
	ProbeLogChain(probe, "繋いだ直後", chain);

	const std::vector<MCObjectHandle> firstMembers = ProbeChainMembers(chain);
	{
		std::string identity = "中の 63 の手と、繋ぐ前の手の同一性: ";
		for (size_t index = 0; index < firstMembers.size(); ++index)
		{
			const std::string handle = ProbeHandleDigest(firstMembers[index]);
			identity += "63[" + ProbeFormatInt(static_cast<long long>(index)) + "]=" + handle +
						(handle == handleA ? "（＝繋ぐ前の 1 本目）"
										   : (handle == handleB ? "（＝繋ぐ前の 2 本目）"
																: "（繋ぐ前のどちらとも違う）")) +
						" ";
		}
		probe.log("  " + identity);
		probe.log("  ↑ どちらとも違えば「作り直されている」、同じなら「そのまま取り込まれた」");
	}
	if (!firstMembers.empty())
	{
		probe.log("  繋ぐ前の 1 本目と、繋いだ後の 63[0] の ov 0〜60 の差:");
		ProbeLogVariableDiff(probe, "繋ぐ前 -> 繋いだ後", beforeChain,
							 ProbeSnapshotVariables(firstMembers[0]));
	}
	{
		double value = 0.0;
		if (!firstMembers.empty() && ProbeGetFontSize(firstMembers[0], value))
		{
			const double diffTarget = value > kProbeTargetFontSize ? value - kProbeTargetFontSize
																   : kProbeTargetFontSize - value;
			const double diff1To1 = value > kProbeFontSizeAt1To1 ? value - kProbeFontSizeAt1To1
																 : kProbeFontSizeAt1To1 - value;
			probe.log(
				std::string("  【答え 1】繋いだ後の 63[0] の ovDimFontSize は ") +
				ProbeFormatNumber(value) + "mm ——" +
				(diffTarget < 0.01
					 ? "書いた値（" + ProbeFormatNumber(kProbeTargetFontSize) +
						   "）がそのまま残っている＝焼き直されていない"
					 : (diff1To1 < 0.01 ? "1:1 の値（" + ProbeFormatNumber(kProbeFontSizeAt1To1) +
											  "）へ焼き直されている＝見立てのとおり"
										: "書いた値でも 1:1 の値でもない（別の由来）")));
		}
	}

	// =====================================================================
	probe.log("");
	probe.log("== 2. 繋いだ後に、中の直線寸法へ書けるか");
	for (size_t index = 0; index < firstMembers.size(); ++index)
	{
		const bool wrote = ProbeSetFontSize(firstMembers[index], kProbeTargetFontSize);
		probe.log("  63[" + ProbeFormatInt(static_cast<long long>(index)) + "] へ " +
				  ProbeFormatNumber(kProbeTargetFontSize) +
				  "mm を書けた=" + (wrote ? "true" : "false") +
				  " 読み戻し=" + ProbeFontSizeText(firstMembers[index]));
	}
	ProbeLogChain(probe, "中へ書いた直後", chain);

	probe.log("  連続寸法へ ResetObject を呼ぶ（中が作り直されるか）");
	gSDK->ResetObject(chain);
	ProbeLogChain(probe, "連続寸法を ResetObject した後", chain);

	// =====================================================================
	probe.log("");
	probe.log("== 3. 連続寸法そのもの（型 86）と中の 2D 表現のグループ（型 11）へ書けるか");
	probe.log("  連続寸法の ovDimFontSize 読み=" +
			  std::string(ProbeReadVariable(chain, ovDimFontSize).empty()
							  ? "(読めない)"
							  : ProbeReadVariable(chain, ovDimFontSize)));
	probe.log("  連続寸法へ書けた=" +
			  std::string(ProbeSetFontSize(chain, kProbeTargetFontSize) ? "true" : "false") +
			  " 書いた後の読み=" +
			  std::string(ProbeReadVariable(chain, ovDimFontSize).empty()
							  ? "(読めない)"
							  : ProbeReadVariable(chain, ovDimFontSize)));
	{
		MCObjectHandle group = nil;
		for (MCObjectHandle member = gSDK->FirstMemberObj(chain); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) == kGroupNode)
			{
				group = member;
				break;
			}
			if (gSDK->GetObjectTypeN(member) == 0)
				break;
		}
		if (group == nil)
			probe.log("  中に 2D 表現のグループ（型 11）は無い");
		else
			probe.log(
				"  型 11 へ書けた=" +
				std::string(ProbeSetFontSize(group, kProbeTargetFontSize) ? "true" : "false") +
				" 読み=" +
				std::string(ProbeReadVariable(group, ovDimFontSize).empty()
								? "(読めない)"
								: ProbeReadVariable(group, ovDimFontSize)));
	}
	probe.log("  連続寸法のパラメータ（PIO の欄）を並べる:");
	{
		VWParametricObj parametric(chain);
		const size_t count = parametric.GetParamsCount();
		probe.log("    欄 " + ProbeFormatInt(static_cast<long long>(count)) + " 件");
		for (size_t index = 0; index < count && index < 60; ++index)
			probe.log(
				"    " + std::string(static_cast<const char*>(parametric.GetParamName(index))) +
				" = " + std::string(static_cast<const char*>(parametric.GetParamValue(index))));
	}

	// =====================================================================
	probe.log("");
	probe.log("== 4. 注釈へ移す・更新する・縮尺を変える——書いた値は残るか");
	if (viewport == nil)
	{
		probe.log("  ビューポートが無いので測れない");
	}
	else
	{
		// 直前に中へ書き直してから移す（3 の書き込みで壊れていても揃えるため）。
		const std::vector<MCObjectHandle> members = ProbeChainMembers(chain);
		for (size_t index = 0; index < members.size(); ++index)
			ProbeSetFontSize(members[index], kProbeTargetFontSize);
		ProbeLogChain(probe, "注釈へ移す直前（中へ書き直した）", chain);

		const bool added = gSDK->AddViewportAnnotationObject(viewport, chain) != 0;
		probe.log("  AddViewportAnnotationObject(連続寸法)=" +
				  std::string(added ? "true" : "false"));
		ProbeLogChain(probe, "注釈へ移した直後", chain);

		gSDK->UpdateViewport(viewport);
		probe.log("  UpdateViewport を呼んだ");
		ProbeLogChain(probe, "ビューポートを更新した後", chain);

		TVariableBlock scaleBlock;
		scaleBlock = static_cast<Real64>(kProbeVpScale * 2.0);
		gSDK->SetObjectVariable(viewport, ovViewportScale, scaleBlock);
		gSDK->UpdateViewport(viewport);
		probe.log("  ビューポートの縮尺を 1/" + ProbeFormatNumber(kProbeVpScale * 2.0) +
				  " へ変えて更新した（比例して書き換わるなら倍になる）");
		ProbeLogChain(probe, "縮尺を倍にした後", chain);

		scaleBlock = static_cast<Real64>(kProbeVpScale);
		gSDK->SetObjectVariable(viewport, ovViewportScale, scaleBlock);
		gSDK->UpdateViewport(viewport);
		probe.log("  縮尺を 1/" + ProbeFormatNumber(kProbeVpScale) + " へ戻して更新した");
		ProbeLogChain(probe, "縮尺を戻した後", chain);

		gSDK->ResetObject(chain);
		probe.log("  注釈の中で連続寸法へ ResetObject を呼んだ");
		ProbeLogChain(probe, "注釈の中で ResetObject した後", chain);
	}

	// =====================================================================
	probe.log("");
	probe.log("== 5. 対照: ビューポートと同じ縮尺（1/" + ProbeFormatNumber(kProbeVpScale) +
			  "）のデザインレイヤがアクティブなまま作って繋ぐ");
	MCObjectHandle designLayer = gSDK->CreateLayer("VW調査155 試験デザイン", kLayerDesign);
	if (designLayer == nil)
	{
		probe.log("  CreateLayer(kLayerDesign) が nil。この対照は測れない");
	}
	else
	{
		gSDK->SetLayerScaleN(designLayer, kProbeVpScale);
		probe.log("  デザインレイヤを作って縮尺を 1/" + ProbeFormatNumber(kProbeVpScale) +
				  " にした。アクティブ: " + ProbeActiveLayerText());
		MCObjectHandle dimC = nil;
		MCObjectHandle dimD = nil;
		ProbeCreateTwoDims(probe, 5000.0, dimC, dimD);
		if (dimC != nil && dimD != nil)
		{
			probe.log("  作った直後（1/" + ProbeFormatNumber(kProbeVpScale) +
					  " 生まれ。何も書いて"
					  "いない）: 1 本目 ovDimFontSize=" +
					  ProbeFontSizeText(dimC));
			MCObjectHandle chain2 = gSDK->CreateChainDimension(dimC, dimD);
			if (chain2 == nil)
			{
				probe.log("  CreateChainDimension が nil");
			}
			else
			{
				ProbeLogChain(
					probe, "1/" + ProbeFormatNumber(kProbeVpScale) + " で作って繋いだ直後", chain2);
				if (viewport != nil)
				{
					probe.log("  これを何も書かずに注釈へ移して更新する（推奨手順の候補）");
					probe.log("  AddViewportAnnotationObject=" +
							  std::string(gSDK->AddViewportAnnotationObject(viewport, chain2) != 0
											  ? "true"
											  : "false"));
					gSDK->UpdateViewport(viewport);
					ProbeLogChain(probe, "注釈へ移して更新した後", chain2);
				}
			}
		}
	}

	probe.log("");
	probe.log("おわり。**この図面は保存しないこと**（試験用のレイヤ 2 枚とビューポートが残る）。");
}
