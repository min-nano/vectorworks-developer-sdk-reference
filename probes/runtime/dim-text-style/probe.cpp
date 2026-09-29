//
//	probes/runtime/dim-text-style/probe.cpp
//
//	[issue #157] 寸法の文字スタイル——3 巡目。大きさの換算則と、#143 との矛盾を決着させる。
//
//	1 巡目・2 巡目で絵つきで確定したこと（Findings へ反映済み）:
//	  ・規格の文字スタイルは dimStdTextStyle(51) で読める（Sint32 の ref number）。
//	  ・注釈で値が出るのは SetTextStyleRef を呼んだときだけ。〈クラスの文字スタイル〉の
//	    ままでも、ovDimTextStyle(1248) へ番号を書いただけでも出ない（読み戻しは同じ）。
//	  ・**文字スタイルを明示すると ovDimFontSize は絵に効かなくなる**——152.4 /
//	    264.5833 / 529.1667 の 3 本が、絵ではまったく同じ大きさで出た。
//	  ・連続寸法（型 86）だけ、中身が同じ設定でも明らかに大きく出る。
//
//	ここで決めるのは 2 つ。
//
//	(A) **紙で狙った pt を出すには ovTextStyleSize をいくつにすればよいか。**
//	    仮説は「文字スタイルの大きさは 1:1 の図面 mm として扱われ、ビューポートの縮尺で
//	    割られて紙に出る」＝ #143 の焼き付きと同じ扱い。だとすれば
//	    **ovTextStyleSize（インチ）= 紙の pt / 72 × ビューポートの縮尺**。
//	    F1/F2/F3 で 0 倍・1 倍・2 倍を作って確かめる。
//
//	(B) **#143 との矛盾。** #143 は「1:1 生まれへ ovDimFontSize を書けば注釈で正しく
//	    出る」を絵で確定させている。今回それが効かなかった違いは「**当たっている寸法規格が
//	    文字スタイルを持っているかどうか**」ではないか——この図面のカスタム規格
//	    `min-nano` は 寸法(6pt) を持っている（1 巡目で実測）が、組み込み規格はどれも
//	    持たない（dimStdTextStyle = 0）。そこで F5/F6 は**規格を JIS に替えて**、
//	    文字スタイルがどこにも無い状態で ovDimFontSize が効くかを見る。
//	    ここが効けば #143 は撤回不要で、「文字スタイルが在るときだけ話が変わる」と
//	    書ける。効かなければ #143 のほうを訂正する必要がある。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	// 利用側と同じ 1/125。紙で 6pt にしたいなら ovDimFontSize = 6 × 25.4/72 × 125。
	const double kProbeDimVpScale = 125.0;

	// 仮説 (A): 紙で P pt に見せたい文字スタイルの大きさ（インチ）。
	double ProbeDimPaperPtToStyleInch(double paperPt)
	{
		return paperPt / 72.0 * kProbeDimVpScale;
	}

	double ProbeDimPaperPtToFontSize(double paperPt)
	{
		return paperPt * 25.4 / 72.0 * kProbeDimVpScale;
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

	void ProbeDimDump(vwprobe::Report& probe, const std::string& tag, MCObjectHandle dim)
	{
		std::string line = "    " + tag + ": ";
		if (dim == nil)
		{
			probe.log(line + "(nil)");
			return;
		}
		line += "型=" + ProbeDimInt(static_cast<Sint32>(gSDK->GetObjectTypeN(dim)));
		line += " クラス由来=" + ProbeDimBool(gSDK->GetTextStyleByClass(dim));
		const InternalIndex styleRef = gSDK->GetTextStyleRef(dim);
		line += " GetTextStyleRef=" + ProbeDimInt(static_cast<Sint32>(styleRef));
		line += "(" + ProbeDimIndexName(styleRef) + ")";

		Sint32 ovStyle = 0;
		line += ProbeDimGetSint32(dim, ovDimTextStyle, ovStyle)
					? (" ovDimTextStyle=" + ProbeDimInt(ovStyle))
					: std::string(" ovDimTextStyle=(読めず)");

		double fontSize = 0.0;
		line += ProbeDimGetReal(dim, ovDimFontSize, fontSize)
					? (" ovDimFontSize=" + ProbeDimNum(fontSize))
					: std::string(" ovDimFontSize=(読めず)");

		double pointSize = 0.0;
		if (ProbeDimGetReal(dim, ovDimTextSizeInPoints, pointSize))
			line += " ovDimTextSizeInPoints=" + ProbeDimNum(pointSize);

		probe.log(line);
	}

	// 大きさ（インチ）を指定した文字スタイルを 1 つ作り、その番号を返す。
	InternalIndex ProbeDimMakeStyle(vwprobe::Report& probe, const char* name, double inches)
	{
		MCObjectHandle style = gSDK->CreateTextStyleResource(name);
		if (style == nil)
		{
			probe.fail(std::string("CreateTextStyleResource が nil を返した: ") + name);
			return 0;
		}
		ProbeDimSetReal(style, ovTextStyleSize, inches);
		double readBack = 0.0;
		ProbeDimGetReal(style, ovTextStyleSize, readBack);
		const InternalIndex ref = gSDK->GetObjectInternalIndex(style);
		probe.log(std::string("    ") + name + ": 番号=" + ProbeDimInt(static_cast<Sint32>(ref)) +
				  " ovTextStyleSize=" + ProbeDimNum(readBack) +
				  " インチ（= " + ProbeDimNum(readBack * 72.0) + "pt 相当）");
		return ref;
	}

	// 寸法規格を名前で当てる（存在しない名前は false で弾かれる。Findings「寸法へ規格を当てる」）。
	bool ProbeDimSetStandard(MCObjectHandle dim, const char* standardName)
	{
		TVariableBlock block;
		block = TXString(standardName);
		return gSDK->SetObjectVariable(dim, ovDimStandardName, block) != 0;
	}

	std::string ProbeDimStandardOf(MCObjectHandle dim)
	{
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(dim, ovDimStandardName, block))
			return std::string("(読めず)");
		TXString name;
		if (!block.GetTXString(name))
			return std::string("(読めず)");
		return std::string(static_cast<const char*>(name));
	}

	MCObjectHandle ProbeDimMake(double y, double width)
	{
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(0.0, y), WorldPt(width, y), 0.0,
														 0.0, Vector2(0.0, 0.0), 0);
		if (dim != nil)
			ProbeDimSetBoolean(dim, ovDimShowValue, true);
		return dim;
	}

	// 中の型 63 を全部出す（連続寸法の中身を読むため）。
	void ProbeDimDumpMembers(vwprobe::Report& probe, const std::string& tag, MCObjectHandle chain)
	{
		int found = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(chain); member != nil;
			 member = gSDK->NextObject(member))
		{
			const short memberType = gSDK->GetObjectTypeN(member);
			if (memberType == 0) // kTermNode（終端）
				break;
			if (memberType == 63)
				ProbeDimDump(probe, tag + " 中の直線寸法 #" + ProbeDimInt(++found), member);
		}
		if (found == 0)
			probe.log("    " + tag + " 中に型 63 が見つからなかった");
	}
} // namespace

VW_PROBE("dim-text-style", "寸法の文字スタイル 3 巡目——大きさの換算則と #143 との矛盾",
		 "文字スタイルの大きさを 0/1/2 倍で作り分け、規格を替えて ovDimFontSize が効く条件も見る")
{
	probe.log("=== [#157] 寸法の文字スタイル（3 巡目） ===");
	probe.log("新規の空図面で走らせる前提。図面は壊れる。");
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【1】大きさの違う文字スタイルを 3 つ作る（ovTextStyleSize の単位はインチ）");
	probe.log("    仮説: 紙で P pt に見せたいなら ovTextStyleSize = P / 72 × ビューポートの縮尺");
	const InternalIndex styleTooSmall = ProbeDimMakeStyle(probe, "プローブ 素の6pt", 6.0 / 72.0);
	const InternalIndex style6pt =
		ProbeDimMakeStyle(probe, "プローブ 紙6pt", ProbeDimPaperPtToStyleInch(6.0));
	const InternalIndex style12pt =
		ProbeDimMakeStyle(probe, "プローブ 紙12pt", ProbeDimPaperPtToStyleInch(12.0));
	if (styleTooSmall == 0 || style6pt == 0 || style12pt == 0)
		return;
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【2】シートレイヤと 1/125 の平面ビューポートを作る（デザインレイヤは表示しない）");
	MCObjectHandle sheetLayer = gSDK->CreateLayer("プローブ 157 の 3", kLayerSheet);
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
	ProbeDimSetReal(viewport, ovViewportScale, kProbeDimVpScale);
	ProbeDimSetSint32(viewport, ovViewportRenderType, static_cast<Sint32>(renderFinalHiddenLine));
	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	probe.log("");

	// -----------------------------------------------------------------
	const double fs6pt = ProbeDimPaperPtToFontSize(6.0);
	probe.log("【3】注釈へ 6 行");
	probe.log("    紙 6pt 相当の ovDimFontSize = " + ProbeDimNum(fs6pt));
	probe.log("");

	MCObjectHandle rows[6] = {nil, nil, nil, nil, nil, nil};

	// --- (A) 換算則を決める 3 本。違うのは文字スタイルの大きさだけ ---
	probe.log("  F1（長さ 1000）: SetTextStyleRef に「素の 6pt」（6/72 インチ）を当てる");
	probe.log("    → 仮説どおりなら紙で 6/125 = 0.048pt になり **見えない**");
	rows[0] = ProbeDimMake(0.0, 1000.0);
	gSDK->SetTextStyleRef(rows[0], styleTooSmall);
	ProbeDimDump(probe, "F1", rows[0]);

	probe.log(
		"  F2（長さ 2000）: SetTextStyleRef に「紙 6pt」を当てる → 仮説どおりなら **紙で 6pt**");
	rows[1] = ProbeDimMake(-1500.0, 2000.0);
	gSDK->SetTextStyleRef(rows[1], style6pt);
	ProbeDimDump(probe, "F2", rows[1]);

	probe.log("  F3（長さ 3000）: SetTextStyleRef に「紙 12pt」を当てる → 仮説どおりなら **F2 "
			  "のちょうど 2 倍**");
	rows[2] = ProbeDimMake(-3000.0, 3000.0);
	gSDK->SetTextStyleRef(rows[2], style12pt);
	ProbeDimDump(probe, "F3", rows[2]);

	// --- 連続寸法が本当に別扱いなのか ---
	probe.log("  F4（長さ 4000 + 5000 の連続寸法）: 両方へ「紙 6pt」→ F2 と同じ大きさになるはず");
	{
		const double chainY = -4500.0;
		MCObjectHandle leftDim = gSDK->CreateLinearDimension(
			WorldPt(0.0, chainY), WorldPt(4000.0, chainY), 0.0, 0.0, Vector2(0.0, 0.0), 0);
		MCObjectHandle rightDim = gSDK->CreateLinearDimension(
			WorldPt(4000.0, chainY), WorldPt(9000.0, chainY), 0.0, 0.0, Vector2(0.0, 0.0), 0);
		if (leftDim == nil || rightDim == nil)
			probe.fail("連続寸法のもとになる直線寸法を作れなかった");
		else
		{
			ProbeDimSetBoolean(leftDim, ovDimShowValue, true);
			ProbeDimSetBoolean(rightDim, ovDimShowValue, true);
			gSDK->SetTextStyleRef(leftDim, style6pt);
			gSDK->SetTextStyleRef(rightDim, style6pt);
			MCObjectHandle chain = gSDK->CreateChainDimension(leftDim, rightDim);
			if (chain == nil)
				probe.fail("CreateChainDimension が nil を返した");
			else
			{
				rows[3] = chain;
				ProbeDimDump(probe, "F4 連続寸法そのもの", chain);
				ProbeDimDumpMembers(probe, "F4", chain);
			}
		}
	}

	// --- (B) #143 との矛盾。文字スタイルがどこにも無ければ ovDimFontSize は効くか ---
	probe.log("  F5（長さ 6000）: **規格を JIS（文字スタイルを持たない）に替え**、");
	probe.log("    〈クラスの文字スタイル〉のまま ovDimFontSize = 紙 6pt を書く（#143 の再現）");
	rows[4] = ProbeDimMake(-6000.0, 6000.0);
	probe.log("    規格を JIS にした=" + ProbeDimBool(ProbeDimSetStandard(rows[4], "JIS")) +
			  " → 読み戻し=" + ProbeDimStandardOf(rows[4]));
	ProbeDimDump(probe, "F5 規格を替えた直後", rows[4]);
	ProbeDimSetReal(rows[4], ovDimFontSize, fs6pt);
	ProbeDimDump(probe, "F5 ovDimFontSize を書いた後", rows[4]);

	probe.log("  F6（長さ 7000）: 対照。規格を JIS に替えるだけで ovDimFontSize は書かない");
	rows[5] = ProbeDimMake(-7500.0, 7000.0);
	ProbeDimSetStandard(rows[5], "JIS");
	ProbeDimDump(probe, "F6", rows[5]);
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("【4】注釈へ移し、移った後にもう一度読む");
	for (int i = 0; i < 6; ++i)
	{
		if (rows[i] == nil)
		{
			probe.log("    F" + ProbeDimInt(i + 1) + ": 作れていないので移せない");
			continue;
		}
		const Boolean moved = gSDK->AddViewportAnnotationObject(viewport, rows[i]);
		probe.log("    F" + ProbeDimInt(i + 1) +
				  ": AddViewportAnnotationObject=" + ProbeDimBool(moved != 0));
		ProbeDimDump(probe, "F" + ProbeDimInt(i + 1) + " 移した後", rows[i]);
		if (gSDK->GetObjectTypeN(rows[i]) == 86)
			ProbeDimDumpMembers(probe, "F" + ProbeDimInt(i + 1) + " 移した後", rows[i]);
	}

	gSDK->ForEachClass(
		true, [viewport](MCObjectHandle cls)
		{ gSDK->SetViewportClassVisibility(viewport, gSDK->GetObjectInternalIndex(cls), 0); });
	gSDK->UpdateViewport(viewport);
	probe.log("    クラスを全部表示へ戻して再更新した。");
	probe.log("");

	// -----------------------------------------------------------------
	probe.log("=== 目で見ないと分からないこと（このログには写らない） ===");
	probe.log("シートレイヤ「プローブ 157 の 3」の 1/125 のビューポートに、上から");
	probe.log("  F1 = 1,000 / F2 = 2,000 / F3 = 3,000 / F4 = 4,000 と 5,000 /");
	probe.log("  F5 = 6,000 / F6 = 7,000");
	probe.log("がならびます。伺いたいのは **どれが読める大きさで出ているか** だけです。");
	probe.log("  (1) 読める数字はどれですか（見えない行があれば、それも教えてください）。");
	probe.log("  (2) 3,000 は 2,000 のちょうど 2 倍くらいの大きさですか。");
	probe.log("  (3) 4,000 / 5,000 は 2,000 と同じ大きさですか、違いますか。");
	probe.log("仮説どおりなら: F1 見えない / F2 読める / F3 は F2 の 2 倍 / F4 は F2 と同じ /");
	probe.log("F5 読める（#143 は撤回不要）/ F6 見えない。**違っていたら、そう教えてください。**");
}
