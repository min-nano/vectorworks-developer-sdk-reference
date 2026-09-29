//
//	probes/runtime/drawing-label-style-textsize/probe.cpp
//
//	[issue #175] 図面ラベル（Drawing Label2）のレイアウトの文字の大きさ——**残っているのは
//	「何が縮尺を掛けるのか」** だけ。容れ物か、アクティブレイヤか、その両方かを決める。
//
//	確定していること（測り直さない）:
//	  * ツールのスタイルは CreateCustomObject で自動的に当たる。外すのは
//	    gSDK->SetPluginObjectStyle(h, 0)。VWParametricObj::SetStyle(0) は無反応。
//	  * 既定のレイアウトはスタイルが持っている。スタイル無しで作ると空で何も描かない。
//	    組み直してから外せばレイアウトは残る。
//	  * 文字スタイルを SetTextStyleRef で当てると、**当てた時点のアクティブレイヤの
//	    縮尺が焼き付く**（1/50 で 10pt が 176.38889、1:1 で 3.52778）。
//	  * ビューポートの注釈は CreateViewport(sheet) ＋ SetViewportLayerVisibility で作る
//	    （parentHandle は「置く容れ物」）。
//
//	**まだ決まっていないこと。** レイアウトへ 3.52778（紙で 10pt）を与えたラベルが、
//	「描いた文字 3.52778（＝掛からない）」になる回と「176.38889（＝×50）」になる回がある。
//
//	| 走行 | アクティブレイヤの扱い | 注釈の中の結果 |
//	| --- | --- | --- |
//	| 2 回目 | 終始 1/50（デザインレイヤ） | 全部 ×50 |
//	| 3・4 回目 | 終始 1:1（シートレイヤ） | 全部 掛からない |
//	| 5 回目 | 1 本ごとに切り替え | 1 本目（1:1）だけ掛からず、**1/50 を 1 度使った後は
//	          1:1 に戻しても掛かった** |
//	| 6 回目 | 終始 1:1 | 全部 掛からない（ResetObject の回数・UpdateViewport・
//	          組み直しの時機を変えても動かない） |
//
//	つまり**容れ物ではなくアクティブレイヤが効いている見込み**が濃い（5 回目の
//	「一度 1/50 を使うと後も掛かる」は要確認）。ここを決めないと「何を渡せばよいか」が
//	決まらない——間違えると紙で 0.2pt（見えない）か 500pt になる。
//
//	測ること（与える値は全部 3.52778 ＝ 紙の 10pt に固定）:
//	  N1 注釈・ResetObject のときのアクティブ 1:1
//	  N2 注釈・アクティブ 1/50
//	  N3 注釈・アクティブ 1:1（N2 の後。「一度 1/50 を使うと後も掛かる」の確認）
//	  N4 注釈・アクティブ 1:1（もう 1 本。N3 の裏取り）
//	  N5 注釈・アクティブ 1/50
//	  **N6 デザインレイヤ(1/50) へ置く・アクティブ 1:1**  ← 容れ物と切り離す対
//	  **N7 シートレイヤ(1:1) へ置く・アクティブ 1/50**    ← その逆
//	アクティブレイヤは ResetObject の直前と直後に読んで記録する。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const short kProbeTextNodeType = 10;
	const short kProbeLineNodeType = 2;

	const double kProbeVpScale = 50.0;
	const double kProbePtToMm = 25.4 / 72.0;
	const double kProbeTenPtMm = 10.0 * kProbePtToMm; // 3.52778
	const double kProbeRulerPt = 6.0;

	double gProbeWorldPerPtSize = 0.0;

	std::string ProbeNum(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.5f", v);
		return buf;
	}

	std::string ProbeInt(Sint32 v)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%ld", static_cast<long>(v));
		return buf;
	}

	std::string ProbeBoolStr(bool b)
	{
		return b ? "true" : "false";
	}

	double ProbeHeightOf(MCObjectHandle h)
	{
		WorldRect r;
		if (h == nil || !gSDK->GetObjectBounds(h, r))
			return -1.0;
		return std::fabs(r.top - r.bottom);
	}

	double ProbeWidthOf(MCObjectHandle h)
	{
		WorldRect r;
		if (h == nil || !gSDK->GetObjectBounds(h, r))
			return -1.0;
		return std::fabs(r.right - r.left);
	}

	// いまのアクティブレイヤ（名前と縮尺）。
	std::string ProbeActive()
	{
		MCObjectHandle layer = gSDK->GetActiveLayer();
		if (layer == nil)
			return "アクティブ=nil";
		TXString nm;
		gSDK->GetObjectName(layer, nm);
		double_gs scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return std::string("アクティブ=") + static_cast<const char*>(nm) + "(縮尺 " +
			   ProbeNum(scale) + ")";
	}

	void ProbeCollectTextSizes(MCObjectHandle container, int depth, std::vector<double>& out)
	{
		if (container == nil || depth > 6 || out.size() >= 8)
			return;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil; m = gSDK->NextObject(m))
		{
			if (gSDK->GetObjectTypeN(m) == kProbeTextNodeType)
			{
				WorldCoord size = 0;
				gSDK->GetTextSize(m, 0, size);
				out.push_back(static_cast<double>(size));
				if (out.size() >= 8)
					return;
			}
			ProbeCollectTextSizes(m, depth + 1, out);
		}
	}

	// 紙の pt は「描いた文字の大きさ ÷ 容れ物の縮尺 ÷ 0.352778」。容れ物の縮尺を渡す。
	std::string ProbeDrawn(MCObjectHandle h, double containerScale)
	{
		std::vector<double> sizes;
		ProbeCollectTextSizes(h, 0, sizes);
		if (sizes.empty())
			return "テキスト 0 件";
		std::string s = "テキスト " + ProbeInt(static_cast<Sint32>(sizes.size())) + " 件:";
		for (size_t i = 0; i < sizes.size(); ++i)
		{
			s += " " + ProbeNum(sizes[i]);
			if (containerScale > 0.0)
				s += "(紙で" + ProbeNum(sizes[i] / containerScale / kProbePtToMm) + "pt)";
		}
		return s;
	}

	int gProbeLabelCount = 0;

	WorldPt ProbeNextSpot()
	{
		return WorldPt(0, -3000.0 * (gProbeLabelCount++));
	}

	void ProbeRebuildLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h,
							WorldCoord sizeWorld)
	{
		MCObjectHandle hOld = gSDK->GetCustomObjectProfileGroup(h);
		MCObjectHandle hGroup = (hOld != nil) ? gSDK->CreateGroup(false) : nil;
		if (hGroup == nil)
		{
			probe.log(tag + ": レイアウトを組み直せない");
			return;
		}
		bool tookText = false;
		bool tookLine = false;
		for (MCObjectHandle m = gSDK->FirstMemberObj(hOld); m != nil; m = gSDK->NextObject(m))
		{
			const short type = gSDK->GetObjectTypeN(m);
			const bool wantText = (type == kProbeTextNodeType && !tookText);
			const bool wantLine = (type == kProbeLineNodeType && !tookLine);
			if (!wantText && !wantLine)
				continue;
			MCObjectHandle dup = gSDK->DuplicateObject(m);
			if (dup == nil)
				continue;
			gSDK->AddObjectToContainer(dup, hGroup);
			if (wantText)
			{
				tookText = true;
				const Sint32 len = gSDK->GetTextLength(dup);
				if (sizeWorld > 0)
					gSDK->SetTextSize(dup, 0, len > 0 ? len : 1, sizeWorld);
			}
			else
			{
				tookLine = true;
			}
		}
		if (!tookText)
			probe.log(tag + ": **元のレイアウトにテキストが無かった**");
		gSDK->SetCustomObjectProfileGroup(h, hGroup);
	}
} // namespace

VW_PROBE("drawing-label-style-textsize", "図面ラベルのスタイルと文字の大きさ",
		 "縮尺を掛けるのは容れ物かアクティブレイヤかを決める")
{
	gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);

	probe.log("=== 段取り ===");
	MCObjectHandle design = gSDK->CreateLayer("調査用デザイン", kLayerDesign);
	if (design == nil)
	{
		probe.fail("CreateLayer(デザイン) が nil");
		return;
	}
	gSDK->SetLayerScaleN(design, kProbeVpScale);
	gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 10000.0, 10000.0);

	MCObjectHandle sheet = gSDK->CreateLayer("調査用シート", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("CreateLayer(シート) が nil");
		return;
	}
	gSDK->SetCurrentLayer(sheet);
	probe.log(std::string("段取りの終わり: ") + ProbeActive());

	MCObjectHandle vp = gSDK->CreateViewport(sheet);
	if (vp == nil)
	{
		probe.fail("CreateViewport が nil");
		return;
	}
	gSDK->SetViewportLayerVisibility(vp, design, VWFC::VWObjects::kLayerVisibilityNormal);
	TVariableBlock scaleVar;
	scaleVar = static_cast<Real64>(kProbeVpScale);
	gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
	gSDK->UpdateViewport(vp);

	// 物差し（紙で 6pt の寸法）。5・6 回目で置く順は結果に影響しないと分かっている。
	{
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(0, 8000), WorldPt(5000, 8000), 0,
														 0, Vector2(0, 0), 0);
		if (dim != nil)
		{
			TVariableBlock showVar;
			showVar = true;
			gSDK->SetObjectVariable(dim, ovDimShowValue, showVar);
			TVariableBlock fontVar;
			fontVar = static_cast<Real64>(kProbeRulerPt * kProbePtToMm * kProbeVpScale);
			gSDK->SetObjectVariable(dim, ovDimFontSize, fontVar);
			gSDK->ResetObject(dim);
			gSDK->AddViewportAnnotationObject(vp, dim);
			gSDK->ResetObject(dim);
			std::vector<double> sizes;
			ProbeCollectTextSizes(dim, 0, sizes);
			if (!sizes.empty() && sizes[0] > 0.0)
			{
				gProbeWorldPerPtSize = sizes[0] / kProbeRulerPt;
				probe.log("物差し: 紙の 1pt ＝ 大きさで " + ProbeNum(gProbeWorldPerPtSize));
			}
		}
	}

	// スタイル（既定レイアウトの出どころ）
	RefNumber toolRefOriginal = 0;
	gSDK->GetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	TXString styleName("調査用_図面ラベルスタイル");
	MCObjectHandle hSymDef = gSDK->CreateSymbolDefinition(styleName);
	RefNumber styleRef = toolRefOriginal;
	if (hSymDef != nil)
	{
		MCObjectHandle seed =
			gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
		if (seed == nil)
		{
			probe.fail("種のラベルを作れなかった");
			return;
		}
		gSDK->AddObjectToContainer(seed, hSymDef);
		gSDK->ResetObject(hSymDef);
		gSDK->SetSymbolDefSubType(
			hSymDef, static_cast<Sint32>(VWFC::VWObjects::VWParametricObj::GetInternalID(seed)));
		gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);
		styleRef = gSDK->GetObjectInternalIndex(hSymDef);
	}
	if (styleRef == 0)
	{
		probe.fail("スタイルを用意できなかった");
		return;
	}
	probe.log("使うスタイル ref=" + ProbeInt(styleRef));

	// -----------------------------------------------------------------------
	probe.log("=== 段 N: 与える値は全部 3.52778（紙で 10pt）===");
	struct ProbeCase
	{
		const char* tag;
		int place;			 // 0=注釈 1=シートレイヤ(1:1) 2=デザインレイヤ(1/50)
		bool activeIsDesign; // ResetObject のときのアクティブレイヤ
		double containerScale;
	};
	const ProbeCase cases[] = {
		{"N1 注釈・アクティブ 1:1", 0, false, kProbeVpScale},
		{"N2 注釈・アクティブ 1/50", 0, true, kProbeVpScale},
		{"N3 注釈・アクティブ 1:1（N2 の後）", 0, false, kProbeVpScale},
		{"N4 注釈・アクティブ 1:1（もう 1 本）", 0, false, kProbeVpScale},
		{"N5 注釈・アクティブ 1/50", 0, true, kProbeVpScale},
		{"N6 デザインレイヤ(1/50)・アクティブ 1:1", 2, false, kProbeVpScale},
		{"N7 シートレイヤ(1:1)・アクティブ 1/50", 1, true, 1.0},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const ProbeCase& c = cases[i];
		probe.log(std::string("--- ") + c.tag + " ---");
		gSDK->SetPluginStyleForTool("Drawing Label2", styleRef);
		MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
		if (h == nil)
		{
			probe.log(std::string(c.tag) + ": ラベルを作れなかった");
			continue;
		}
		// レイアウトを組み直す（ResetObject はまだしない——縮尺を掛けるのが何かを
		// 見たいので、アクティブレイヤを決めてから 1 度だけ通す）。
		ProbeRebuildLayout(probe, c.tag, h, kProbeTenPtMm);

		if (c.place == 0)
			probe.log(std::string(c.tag) + ": 注釈へ入れた=" +
					  ProbeBoolStr(gSDK->AddViewportAnnotationObject(vp, h) != 0));
		else
			gSDK->AddObjectToContainer(h, c.place == 1 ? sheet : design);

		gSDK->SetCurrentLayer(c.activeIsDesign ? design : sheet);
		probe.log(std::string(c.tag) + ": ResetObject の直前 " + ProbeActive());
		gSDK->ResetObject(h);
		gSDK->SetPluginObjectStyle(h, 0);
		gSDK->ResetObject(h);
		probe.log(std::string(c.tag) + ": ResetObject の直後 " + ProbeActive());
		probe.log(std::string(c.tag) + ": 外接 幅=" + ProbeNum(ProbeWidthOf(h)) + " 高=" +
				  ProbeNum(ProbeHeightOf(h)) + " / 描いた図形 " + ProbeDrawn(h, c.containerScale));
	}

	probe.log("=== 後片付け ===");
	gSDK->SetCurrentLayer(sheet);
	gSDK->SetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	probe.log("ツールのスタイルを戻した ref=" + ProbeInt(toolRefOriginal));
	probe.log("おわり");
}
