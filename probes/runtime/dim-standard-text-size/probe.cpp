//
//	probes/runtime/dim-standard-text-size/probe.cpp
//
//	[issue #163] **寸法規格の「紙で決めた大きさ」を変えれば、連続寸法の中の文字の
//	大きさを決められるか。**
//
//	#155 で「連続寸法の中の直線寸法に `ovDimFontSize` を書く道は無い」ことが確定した。
//	繋ぐ瞬間に入る値は
//
//	    規格が紙で決めた大きさ × **繋ぐときの**アクティブレイヤの縮尺
//
//	ただ 1 つ。#155 が確かめたのは右の因子（縮尺）だけで、**左の因子（規格が紙で決めた
//	大きさ）を動かす道は確かめていない**（Findings では【推定】）。ここを実測で決める。
//
//	**ヘッダを読んだ限りの見込み**（`Kernel/API/MiniCadCallBacks.h:1809-1860` の
//	`dimStd*` セレクタ 1〜52 を全部読んだ）:
//
//	  * **「文字の大きさ」のセレクタは無い。** 長さ系（用紙インチ）は補助線・寸法線・
//	    端記号ばかりで、文字に関わるのは `dimStdTextStyle`(51)「規格が結び付いている
//	    文字スタイルの ref number」と `dimStdTextPos`(52) と `dimStdTolSizeFac`(26)
//	    （公差文字の％）だけ。
//	  * つまり**規格の「紙の大きさ」は、規格が指している文字スタイルの大きさ
//	    （`ovTextStyleSize`。インチ）そのもの**という見込みになる。#157 の
//	    「`ovDimFontSize` ＝ `ovTextStyleSize` × 25.4 × アクティブレイヤの縮尺」と
//	    同じ式に落ちる。
//	  * 書けるのは**カスタム規格だけ**（`SetCustomDimensionStandardVariable`。組み込みの
//	    1〜9 は "cannot be changed with this function"）。
//
//	**確かめること**（issue の 3 つの問いに 1 対 1 で対応させる）:
//
//	  Q1. 規格の文字サイズを SDK から変えられるか・どの口か。
//	      → `SetCustomDimensionStandardVariable(index, dimStdTextStyle, ref)` が通るか。
//	        組み込み規格（`JIS`）へ書くと弾かれるかも一緒に測る。
//	  Q3. 変えた規格を当てた直線寸法を、**1:1 がアクティブなまま**繋いだら、狙った
//	      大きさになるか。→ 紙 6pt × 1/50 を狙って、規格の文字スタイルを
//	      **300pt（= 6pt × 50）**にしてから 1:1 で作って 1:1 で繋ぎ、中の
//	      `ovDimFontSize` が `105.833mm` になるかを見る。対照として 6pt の規格も測る。
//	      **道は 2 つあるので両方測る**——(P1) 規格を文書の既定にしてから寸法を作る／
//	      (P2) 作った寸法へ `ovDimStandardName` で当てる（#157 で「規格を替えても
//	      寸法の文字スタイルは外れない」と分かっているので、P2 は効かない見込み）。
//	  Q2. 規格を変えたとき、**既にある連続寸法**は作り直しでその大きさになるか。
//	      → 6pt で作った連続寸法に対し、①規格の文字スタイルの**大きさ**を変える
//	        ②規格を**別の文字スタイル**へ結び直す ③中の直線寸法へ規格を当て直す
//	        の 3 つを、それぞれ `ResetObject` を挟んで測る。
//
//	**新規の空図面で走らせる。** 試験用のシートレイヤ 1 枚・文字スタイル 2 つ・カスタム
//	寸法規格 2 つを足し、文書の既定の寸法規格も変える。走らせた後は保存しないこと。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// 狙い: 1/50 のビューポートの注釈で、紙の上で 6pt に見せる。
	const double kStdPaperPt = 6.0;
	const double kStdVpScale = 50.0;
	// 「素直に 6pt」の規格（1:1 で繋ぐと 6pt × 25.4/72 × 1 = 2.1167mm が焼き付く見込み）。
	const double kStdSmallPt = kStdPaperPt;
	// 「狙い × ビューポート縮尺」の規格（1:1 で繋いでも 105.833mm になる見込み）。
	const double kStdBigPt = kStdPaperPt * kStdVpScale;
	// 焼き付く値の見込み（mm）。ovDimFontSize = ovTextStyleSize(inch) × 25.4 × 縮尺。
	const double kStdSmallMM = kStdSmallPt * 25.4 / 72.0;
	const double kStdBigMM = kStdBigPt * 25.4 / 72.0;

	const char* const kStdSmallName = "VW調査163 規格6pt";
	const char* const kStdBigName = "VW調査163 規格300pt";
	const char* const kStdSmallStyleName = "VW調査163 文字6pt";
	const char* const kStdBigStyleName = "VW調査163 文字300pt";

	// -------------------------------------------------------------------------
	std::string StdNum(double value)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.6g", value);
		return std::string(buf);
	}

	std::string StdInt(long long value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%lld", value);
		return std::string(buf);
	}

	std::string StdText(const TXString& value)
	{
		return std::string(static_cast<const char*>(value));
	}

	// 「105.833mm（1/50 の紙の上で 6pt）」の形で言う。
	std::string StdPaperText(double valueMM)
	{
		return StdNum(valueMM) + "mm（1/" + StdNum(kStdVpScale) + " の紙の上で " +
			   StdNum(valueMM / kStdVpScale * 72.0 / 25.4) + "pt）";
	}

	bool StdNear(double a, double b)
	{
		const double diff = a > b ? a - b : b - a;
		return diff < 0.01;
	}

	// -------------------------------------------------------------------------
	bool StdGetReal(MCObjectHandle object, short selector, double& outValue)
	{
		TVariableBlock block;
		if (object == nil || !gSDK->GetObjectVariable(object, selector, block))
			return false;
		Real64 value = 0.0;
		if (!block.GetReal64(value))
			return false;
		outValue = value;
		return true;
	}

	bool StdSetReal(MCObjectHandle object, short selector, double value)
	{
		if (object == nil)
			return false;
		TVariableBlock block;
		block = static_cast<Real64>(value);
		return gSDK->SetObjectVariable(object, selector, block) != 0;
	}

	bool StdSetBoolean(MCObjectHandle object, short selector, bool value)
	{
		if (object == nil)
			return false;
		TVariableBlock block;
		block = static_cast<Boolean>(value ? 1 : 0);
		return gSDK->SetObjectVariable(object, selector, block) != 0;
	}

	std::string StdFontSizeText(MCObjectHandle dim)
	{
		double value = 0.0;
		if (!StdGetReal(dim, ovDimFontSize, value))
			return "(読めない)";
		return StdPaperText(value);
	}

	// 「小さい規格の値」「大きい規格の値」「どちらでもない」を名指しする。
	std::string StdVerdict(MCObjectHandle dim)
	{
		double value = 0.0;
		if (!StdGetReal(dim, ovDimFontSize, value))
			return "（読めない）";
		if (StdNear(value, kStdBigMM))
			return "★ 300pt の規格の値（" + StdNum(kStdBigMM) + "）＝1/" + StdNum(kStdVpScale) +
				   " の紙で " + StdNum(kStdPaperPt) + "pt";
		if (StdNear(value, kStdSmallMM))
			return "× 6pt の規格の値（" + StdNum(kStdSmallMM) + "）＝1/" + StdNum(kStdVpScale) +
				   " の紙で " + StdNum(kStdSmallMM / kStdVpScale * 72.0 / 25.4) + "pt（見えない）";
		return "？ どちらでもない値 " + StdNum(value);
	}

	// **その図形の中に実際に描かれている文字図形（型 10）の 1 文字目の大きさ。**
	// 読み戻した `ovDimFontSize` と絵が食い違うことがあるので（#155）、絵の側も測る。
	bool StdFindTextSize(MCObjectHandle container, int depth, double& outSizeMM)
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
			if (StdFindTextSize(member, depth + 1, outSizeMM))
				return true;
		}
		return false;
	}

	std::string StdDrawnTextText(MCObjectHandle object)
	{
		double sizeMM = 0.0;
		if (!StdFindTextSize(object, 0, sizeMM))
			return "文字図形は無い（値が描かれていない）";
		return "描かれている文字 " + StdPaperText(sizeMM);
	}

	// 容れ物の直下から、ある型の図形を集める（終端 0 で打ち切る）。
	std::vector<MCObjectHandle> StdMembersOfType(MCObjectHandle container, short wanted)
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

	// 直線寸法 1 本の素性（大きさ・規格・文字スタイル）を 1 行で書き出す。
	void StdLogDim(vwprobe::Report& probe, const std::string& indent, const std::string& label,
				   MCObjectHandle dim)
	{
		TXString standard;
		TVariableBlock block;
		if (dim != nil && gSDK->GetObjectVariable(dim, ovDimStandardName, block))
			block.GetTXString(standard);
		const InternalIndex styleRef = dim != nil ? gSDK->GetTextStyleRef(dim) : 0;
		TXString styleName;
		if (styleRef != 0)
			gSDK->InternalIndexToNameN(styleRef, styleName);
		probe.log(indent + label + " ovDimFontSize=" + StdFontSizeText(dim));
		probe.log(indent + "  規格=\"" + StdText(standard) + "\" / by-class=" +
				  (dim != nil && gSDK->GetTextStyleByClass(dim) ? "true" : "false") +
				  " / GetTextStyleRef=" + StdInt(static_cast<long long>(styleRef)) + "（\"" +
				  StdText(styleName) + "\"） / " + StdDrawnTextText(dim));
	}

	// 連続寸法の直下の直線寸法（本体）を書き出し、判定を添える。
	void StdLogChain(vwprobe::Report& probe, const std::string& label, MCObjectHandle chain)
	{
		probe.log("  [" + label + "]");
		if (chain == nil)
		{
			probe.log("    連続寸法が nil");
			return;
		}
		const std::vector<MCObjectHandle> dims = StdMembersOfType(chain, dimHeaderNode);
		if (dims.empty())
		{
			probe.log("    直下に直線寸法（型63）が無い");
			return;
		}
		for (size_t index = 0; index < dims.size(); ++index)
			StdLogDim(probe, "    ", "直下の 63[" + StdInt(static_cast<long long>(index)) + "]",
					  dims[index]);
		probe.log("    【判定】" + StdVerdict(dims[0]));
	}

	double StdLayerScale(MCObjectHandle layer)
	{
		if (layer == nil)
			return 0.0;
		double_gs scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return static_cast<double>(scale);
	}

	std::string StdActiveLayerText()
	{
		MCObjectHandle active = gSDK->GetActiveLayer();
		if (active == nil)
			return "（アクティブレイヤが無い）";
		TXString name;
		gSDK->GetObjectName(active, name);
		return "\"" + StdText(name) + "\" 縮尺=1/" + StdNum(StdLayerScale(active));
	}

	// InternalIndex から資源の手を引く。**`ISDK` に index → handle の直の口は無い**ので、
	// 名前を経由する（`InternalIndexToNameN` → `GetNamedObject`）。
	MCObjectHandle StdStyleHandle(InternalIndex styleRef)
	{
		if (styleRef == 0)
			return nil;
		TXString name;
		gSDK->InternalIndexToNameN(styleRef, name);
		if (name.IsEmpty())
			return nil;
		return gSDK->GetNamedObject(name);
	}

	// -------------------------------------------------------------------------
	// 規格 1 つの素性（名前・結び付いた文字スタイル・その大きさ）を 1 行にする。
	std::string StdStandardText(short index)
	{
		TVariableBlock block;
		if (!gSDK->GetDimensionStandardVariable(index, dimStdstandardName, block))
			return std::string();
		TXString name;
		if (!block.GetTXString(name))
			return std::string();

		std::string out = "index=" + StdInt(index) + " \"" + StdText(name) + "\"";

		TVariableBlock styleBlock;
		if (!gSDK->GetDimensionStandardVariable(index, dimStdTextStyle, styleBlock))
			return out + " dimStdTextStyle=(読めない)";
		Sint32 styleRef = 0;
		if (!styleBlock.GetSint32(styleRef))
			return out + " dimStdTextStyle=(Sint32 で受けられない)";
		out += " dimStdTextStyle=" + StdInt(styleRef);
		if (styleRef == 0)
			return out + "（文字スタイル無し）";

		TXString styleName;
		gSDK->InternalIndexToNameN(static_cast<InternalIndex>(styleRef), styleName);
		out += "（\"" + StdText(styleName) + "\"";
		MCObjectHandle style = StdStyleHandle(static_cast<InternalIndex>(styleRef));
		double sizeInch = 0.0;
		if (style != nil && StdGetReal(style, ovTextStyleSize, sizeInch))
			out +=
				" ovTextStyleSize=" + StdNum(sizeInch) + "inch＝" + StdNum(sizeInch * 72.0) + "pt";
		else
			out += " ovTextStyleSize=(読めない)";
		return out + "）";
	}

	// 名前で規格の index を引く（総当り。Findings「一覧の取り方」と同じ作法）。
	bool StdFindStandard(const std::string& wanted, short& outIndex)
	{
		for (short index = 9; index >= -8; --index)
		{
			TVariableBlock block;
			if (!gSDK->GetDimensionStandardVariable(index, dimStdstandardName, block))
				continue;
			TXString name;
			if (!block.GetTXString(name))
				continue;
			if (StdText(name) == wanted)
			{
				outIndex = index;
				return true;
			}
		}
		return false;
	}

	void StdLogAllStandards(vwprobe::Report& probe, const std::string& indent)
	{
		for (short index = 9; index >= -8; --index)
		{
			const std::string line = StdStandardText(index);
			if (!line.empty())
				probe.log(indent + line);
		}
	}

	// 文字スタイルを 1 つ作り、大きさ（pt）を書いて ref を返す。0 なら作れなかった。
	InternalIndex StdCreateTextStyle(vwprobe::Report& probe, const char* name, double pt)
	{
		MCObjectHandle style = gSDK->CreateTextStyleResource(TXString(name));
		if (style == nil)
		{
			probe.fail(std::string("CreateTextStyleResource が nil を返した: ") + name);
			return 0;
		}
		// **ovTextStyleSize の単位はインチ**（#157 で確定）。pt なら ÷ 72。
		const bool wrote = StdSetReal(style, ovTextStyleSize, pt / 72.0);
		double readBack = 0.0;
		StdGetReal(style, ovTextStyleSize, readBack);
		const InternalIndex ref = gSDK->GetObjectInternalIndex(style);
		probe.log(std::string("  文字スタイル \"") + name +
				  "\" ref=" + StdInt(static_cast<long long>(ref)) + " 狙い=" + StdNum(pt) +
				  "pt 書けたか=" + (wrote ? "true" : "false") + " 読み戻し=" + StdNum(readBack) +
				  "inch＝" + StdNum(readBack * 72.0) + "pt");
		return ref;
	}

	// カスタム規格を 1 つ作り、その index を返す（見つからなければ false）。
	bool StdCreateStandard(vwprobe::Report& probe, const char* name, short& outIndex)
	{
		const short before = gSDK->NumberCustomDimensionStandards();
		MCObjectHandle created = gSDK->CreateCustomDimensionStandard(TXString(name));
		const short after = gSDK->NumberCustomDimensionStandards();
		probe.log(std::string("  CreateCustomDimensionStandard(\"") + name +
				  "\") → handle=" + (created != nil ? "非nil" : "nil") +
				  " / NumberCustomDimensionStandards " + StdInt(before) + " → " + StdInt(after));
		if (!StdFindStandard(name, outIndex))
		{
			probe.fail(std::string("作ったはずのカスタム規格が総当りで見つからない: ") + name);
			return false;
		}
		probe.log("    → " + StdStandardText(outIndex));
		return true;
	}

	// 規格へ文字スタイルを結び付ける（Q1 の口）。戻り値と読み戻しの両方を出す。
	bool StdLinkTextStyle(vwprobe::Report& probe, short index, InternalIndex styleRef,
						  const std::string& label)
	{
		TVariableBlock block;
		block = static_cast<Sint32>(styleRef);
		const bool wrote =
			gSDK->SetCustomDimensionStandardVariable(index, dimStdTextStyle, block) != 0;
		probe.log("  " + label + ": SetCustomDimensionStandardVariable(index=" + StdInt(index) +
				  ", dimStdTextStyle, " + StdInt(static_cast<long long>(styleRef)) + ") → " +
				  (wrote ? "true" : "false"));
		probe.log("    読み戻し: " + StdStandardText(index));
		return wrote;
	}

	// 文書の既定の寸法規格（varDimStandard。short）を差し替える。
	void StdSetDocumentStandard(vwprobe::Report& probe, short index)
	{
		short value = index;
		const bool wrote = gSDK->SetProgramVariable(varDimStandard, &value) != 0;
		short readBack = -99;
		gSDK->GetProgramVariable(varDimStandard, &readBack);
		probe.log("  文書の既定の規格 varDimStandard ← " + StdInt(index) + " → " +
				  (wrote ? "true" : "false") + " / 読み戻し=" + StdInt(readBack));
	}

	// 端点を共有する水平な直線寸法 2 本（繋げる条件を満たす形）。アクティブレイヤに入る。
	bool StdCreateTwoDims(vwprobe::Report& probe, double baseY, const char* standardName,
						  MCObjectHandle& outFirst, MCObjectHandle& outSecond)
	{
		const WorldCoord offset = static_cast<WorldCoord>(500.0);
		outFirst = gSDK->CreateLinearDimension(WorldPt(0.0, baseY), WorldPt(1000.0, baseY), offset,
											   0, Vector2(0, 0), 0);
		outSecond = gSDK->CreateLinearDimension(WorldPt(1000.0, baseY), WorldPt(2000.0, baseY),
												offset, 0, Vector2(0, 0), 0);
		if (outFirst == nil || outSecond == nil)
		{
			probe.fail("CreateLinearDimension が nil を返した（y=" + StdNum(baseY) + "）");
			return false;
		}
		StdSetBoolean(outFirst, ovDimShowValue, true);
		StdSetBoolean(outSecond, ovDimShowValue, true);
		if (standardName != nullptr)
		{
			TVariableBlock block;
			block = TXString(standardName);
			const bool a = gSDK->SetObjectVariable(outFirst, ovDimStandardName, block) != 0;
			const bool b = gSDK->SetObjectVariable(outSecond, ovDimStandardName, block) != 0;
			probe.log(std::string("  作った後に ovDimStandardName ← \"") + standardName + "\" → " +
					  (a ? "true" : "false") + " / " + (b ? "true" : "false"));
		}
		return true;
	}
} // namespace

VW_PROBE("dim-standard-text-size", "寸法規格の紙の大きさで連続寸法の文字を決められるか",
		 "規格の文字スタイルを差し替え、1:1 で繋いだ連続寸法の大きさを測る")
{
	probe.log("【新規の空図面で走らせる】試験用のシートレイヤ 1 枚・文字スタイル 2 つ・");
	probe.log("カスタム寸法規格 2 つを足し、文書の既定の寸法規格も変える。保存しないこと。");
	probe.log("狙い: 1/" + StdNum(kStdVpScale) + " のビューポートの注釈で紙の上 " +
			  StdNum(kStdPaperPt) + "pt。");
	probe.log("  6pt の規格を 1:1 で繋ぐと焼き付く見込み=" + StdNum(kStdSmallMM) + "mm");
	probe.log("  " + StdNum(kStdBigPt) + "pt の規格を 1:1 で繋ぐと焼き付く見込み=" +
			  StdNum(kStdBigMM) + "mm（＝紙で " + StdNum(kStdPaperPt) + "pt）");
	probe.log("走り出しのアクティブレイヤ: " + StdActiveLayerText());

	// =====================================================================
	probe.log("");
	probe.log("== 0. 走り出しの寸法規格の顔ぶれ（index 9〜−8 を総当り）");
	{
		short docStandard = -99;
		const bool read = gSDK->GetProgramVariable(varDimStandard, &docStandard) != 0;
		probe.log("  varDimStandard（文書の既定）=" + StdInt(docStandard) +
				  "（GetProgramVariable=" + (read ? "true" : "false") + "）");
		probe.log("  NumberCustomDimensionStandards()=" +
				  StdInt(gSDK->NumberCustomDimensionStandards()));
		StdLogAllStandards(probe, "  ");
	}

	// =====================================================================
	probe.log("");
	probe.log("== 1. 文字スタイルを 2 つ作る（6pt と " + StdNum(kStdBigPt) + "pt）");
	const InternalIndex smallStyle = StdCreateTextStyle(probe, kStdSmallStyleName, kStdSmallPt);
	const InternalIndex bigStyle = StdCreateTextStyle(probe, kStdBigStyleName, kStdBigPt);
	if (smallStyle == 0 || bigStyle == 0)
		return;

	// =====================================================================
	probe.log("");
	probe.log("== 2. カスタム寸法規格を 2 つ作る");
	short smallIndex = 0;
	short bigIndex = 0;
	if (!StdCreateStandard(probe, kStdSmallName, smallIndex))
		return;
	if (!StdCreateStandard(probe, kStdBigName, bigIndex))
		return;

	// =====================================================================
	probe.log("");
	probe.log("== 3.【Q1】規格へ文字スタイルを結び付ける（SetCustomDimensionStandardVariable）");
	StdLinkTextStyle(probe, smallIndex, smallStyle, "6pt の規格");
	StdLinkTextStyle(probe, bigIndex, bigStyle, StdNum(kStdBigPt) + "pt の規格");
	probe.log("  対照: 組み込み規格（JIS）へ同じ書き込みを試す（弾かれる見込み）");
	{
		short jisIndex = 0;
		if (StdFindStandard("JIS", jisIndex))
			StdLinkTextStyle(probe, jisIndex, bigStyle, "組み込みの JIS");
		else
			probe.log("    JIS が総当りで見つからなかった（この図面には無い）");
	}

	// =====================================================================
	probe.log("");
	probe.log("== 4. 1:1 のシートレイヤをアクティブにする（以後ずっと 1:1 のまま繋ぐ）");
	if (gSDK->CreateLayer("VW調査163 試験シート", kLayerSheet) == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	probe.log("  アクティブ: " + StdActiveLayerText());

	// =====================================================================
	probe.log("");
	probe.log("== 5.【Q3・P1】規格を**文書の既定にしてから**寸法を作り、1:1 のまま繋ぐ");
	probe.log("  5a. 既定を 6pt の規格にして作る（対照）");
	StdSetDocumentStandard(probe, smallIndex);
	MCObjectHandle smallChain = nil;
	{
		MCObjectHandle first = nil;
		MCObjectHandle second = nil;
		if (!StdCreateTwoDims(probe, 0.0, nullptr, first, second))
			return;
		StdLogDim(probe, "  ", "繋ぐ前 1 本目", first);
		smallChain = gSDK->CreateChainDimension(first, second);
		StdLogChain(probe, "P1・6pt の規格で作って 1:1 で繋いだ", smallChain);
	}

	probe.log("");
	probe.log("  5b. 既定を " + StdNum(kStdBigPt) + "pt の規格にして作る（★これが本題）");
	StdSetDocumentStandard(probe, bigIndex);
	{
		MCObjectHandle first = nil;
		MCObjectHandle second = nil;
		if (!StdCreateTwoDims(probe, 4000.0, nullptr, first, second))
			return;
		StdLogDim(probe, "  ", "繋ぐ前 1 本目", first);
		MCObjectHandle chain = gSDK->CreateChainDimension(first, second);
		StdLogChain(probe, "P1・" + StdNum(kStdBigPt) + "pt の規格で作って 1:1 で繋いだ", chain);
	}

	// =====================================================================
	probe.log("");
	probe.log("== 6.【Q3・P2】6pt の規格で作った寸法へ、後から ovDimStandardName で");
	probe.log("   " + StdNum(kStdBigPt) + "pt の規格を当ててから繋ぐ（#157 から効かない見込み）");
	StdSetDocumentStandard(probe, smallIndex);
	{
		MCObjectHandle first = nil;
		MCObjectHandle second = nil;
		if (!StdCreateTwoDims(probe, 8000.0, kStdBigName, first, second))
			return;
		StdLogDim(probe, "  ", "規格を当てた直後 1 本目", first);
		MCObjectHandle chain = gSDK->CreateChainDimension(first, second);
		StdLogChain(probe, "P2・作った後に規格を当ててから 1:1 で繋いだ", chain);
	}

	// =====================================================================
	probe.log("");
	probe.log("== 7.【Q2】既にある連続寸法（5a の 6pt のもの）は、規格を変えると");
	probe.log("   作り直しでその大きさになるか。3 つの道を順に試す。");
	if (smallChain == nil)
	{
		probe.fail("5a の連続寸法が nil なので Q2 を測れない");
		return;
	}
	StdLogChain(probe, "Q2 の出発点（いまの状態）", smallChain);

	probe.log("");
	probe.log("  7a. 規格が指している文字スタイルの**大きさ**を " + StdNum(kStdSmallPt) + "pt → " +
			  StdNum(kStdBigPt) + "pt に変える");
	{
		MCObjectHandle style = StdStyleHandle(smallStyle);
		if (style == nil)
			probe.log("    文字スタイルの手が引けない（名前から GetNamedObject が nil）");
		else
		{
			const bool wrote = StdSetReal(style, ovTextStyleSize, kStdBigPt / 72.0);
			double readBack = 0.0;
			StdGetReal(style, ovTextStyleSize, readBack);
			probe.log("    書けたか=" + std::string(wrote ? "true" : "false") +
					  " 読み戻し=" + StdNum(readBack) + "inch＝" + StdNum(readBack * 72.0) + "pt");
			probe.log("    → " + StdStandardText(smallIndex));
		}
		StdLogChain(probe, "7a: 大きさを変えた直後（ResetObject の前）", smallChain);
		gSDK->ResetObject(smallChain);
		StdLogChain(probe, "7a: 連続寸法を ResetObject した後", smallChain);
	}

	probe.log("");
	probe.log("  7b. 規格を**別の文字スタイル**（" + StdNum(kStdBigPt) + "pt）へ結び直す");
	{
		StdLinkTextStyle(probe, smallIndex, bigStyle, "6pt の規格を結び直す");
		StdLogChain(probe, "7b: 結び直した直後（ResetObject の前）", smallChain);
		gSDK->ResetObject(smallChain);
		StdLogChain(probe, "7b: 連続寸法を ResetObject した後", smallChain);
	}

	probe.log("");
	probe.log("  7c. 中の直線寸法へ ovDimStandardName で " + StdNum(kStdBigPt) +
			  "pt の規格を当ててから ResetObject");
	{
		const std::vector<MCObjectHandle> dims = StdMembersOfType(smallChain, dimHeaderNode);
		TVariableBlock block;
		block = TXString(kStdBigName);
		for (size_t index = 0; index < dims.size(); ++index)
			probe.log("    63[" + StdInt(static_cast<long long>(index)) +
					  "] へ ovDimStandardName ← \"" + kStdBigName + "\" → " +
					  (gSDK->SetObjectVariable(dims[index], ovDimStandardName, block) ? "true"
																					  : "false"));
		StdLogChain(probe, "7c: 当てた直後（ResetObject の前）", smallChain);
		gSDK->ResetObject(smallChain);
		StdLogChain(probe, "7c: 連続寸法を ResetObject した後", smallChain);
	}

	probe.log("");
	probe.log("おわり。**この図面は保存しないこと**（試験用のレイヤ・文字スタイル・規格が");
	probe.log("残っており、文書の既定の寸法規格も書き換わっている）。");
}
