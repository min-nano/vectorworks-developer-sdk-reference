//
//	probes/runtime/drawing-label-style-textsize/probe.cpp
//
//	[issue #175] 図面ラベル（Drawing Label2）のレイアウトの文字が、**ビューポートの
//	注釈の中で紙の上いくつになるか**を、同じ注釈に置いた物差しとの比で確定させる。
//
//	ここまでに割れたこと（測り直さない）:
//	  * ツールのスタイルは CreateCustomObject で自動的に当たる。外すのは
//	    gSDK->SetPluginObjectStyle(h, 0)（ResetObject を越えて残る）。
//	    VWParametricObj::SetStyle(0) は無反応。
//	  * **既定のレイアウトはスタイルが持っている**——スタイル無しで作ったラベルの
//	    プロファイルグループは空で何も描かない。外すとレイアウトはインスタンスへ残る。
//	  * **文字スタイルは当てたときのアクティブレイヤの縮尺で焼き付く**
//	    （1/50 で当てると 10pt が 176.38889、1:1 なら 3.52778）。
//	  * デザインレイヤ 1/50 では、レイアウトへ与えた 3.52778 が 176.38889 で描かれ、
//	    外接の高さが紙で 15pt になった（＝与えるのは紙の mm）。
//	  * `CreateViewport(parentHandle)` の parentHandle は「どの容れ物に置くか」。
//	    シートレイヤを渡し、表示するデザインレイヤは SetViewportLayerVisibility で決める。
//
//	4 回目で注釈へは入るようになったが、**注釈の中のラベルは外接の高さが 0** で、
//	描いたテキストも 1 件（レイアウトと同じ値）しか出ず、紙の pt を決められなかった。
//	そこでこの版は測り方を変える:
//	  1. **同じ注釈に物差しの寸法**（Findings「Dimensions」で紙 6pt と確定している
//	     ovDimFontSize ＝ 6 × 25.4/72 × 50 ＝ 105.83333）を置き、その中のテキストを
//	     GetTextSize と **GetObjectBounds の高さ**の両方で測る。
//	  2. ラベルのテキストも同じ 2 つで測り、**物差しとの比**から紙の pt を出す。
//	  3. 外接が 0 になる件を追うため、ラベルの子の群ごとの外接も出す。
//	  4. ResetObject のときのアクティブレイヤ（1:1 / 1/50）を変えた対を取る。
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
	const double kProbeRulerPt = 6.0;				  // 物差しの寸法の紙の大きさ

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

	std::string ProbeStr(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
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

	// テキスト 1 件の測り。**GetTextSize と外接の高さの両方**を取る（4 回目は
	// GetTextSize だけを見ていて、注釈の中では読み違えるおそれがあった）。
	struct ProbeTextHit
	{
		double size;   // GetTextSize
		double height; // 外接の高さ
		int depth;
	};

	void ProbeCollectTexts(MCObjectHandle container, int depth, std::vector<ProbeTextHit>& out)
	{
		if (container == nil || depth > 6 || out.size() >= 12)
			return;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil; m = gSDK->NextObject(m))
		{
			if (gSDK->GetObjectTypeN(m) == kProbeTextNodeType)
			{
				WorldCoord size = 0;
				gSDK->GetTextSize(m, 0, size);
				out.push_back(ProbeTextHit{static_cast<double>(size), ProbeHeightOf(m), depth});
				if (out.size() >= 12)
					return;
			}
			ProbeCollectTexts(m, depth + 1, out);
		}
	}

	// 物差し（紙 1pt あたりの世界座標）を渡すと紙の pt も添える。
	std::string ProbeTextSummary(MCObjectHandle container, double worldPerPt)
	{
		std::vector<ProbeTextHit> hits;
		ProbeCollectTexts(container, 0, hits);
		if (hits.empty())
			return "テキスト 0 件";
		std::string s = "テキスト " + ProbeInt(static_cast<Sint32>(hits.size())) + " 件:";
		for (size_t i = 0; i < hits.size(); ++i)
		{
			s += " [深さ" + ProbeInt(hits[i].depth) + "]大きさ=" + ProbeNum(hits[i].size) +
				 " 外接高=" + ProbeNum(hits[i].height);
			if (worldPerPt > 0.0 && hits[i].height >= 0.0)
				s += "(外接は紙で" + ProbeNum(hits[i].height / worldPerPt) + "pt)";
		}
		return s;
	}

	// 子の群ごとの外接も出す（ラベル自身の外接が 0 になる件を追うため）。
	std::string ProbeChildBounds(MCObjectHandle h)
	{
		if (h == nil)
			return "nil";
		std::string s;
		int n = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(h); m != nil && n < 8;
			 m = gSDK->NextObject(m), ++n)
			s += " [型" + ProbeInt(gSDK->GetObjectTypeN(m)) + " 幅=" + ProbeNum(ProbeWidthOf(m)) +
				 " 高=" + ProbeNum(ProbeHeightOf(m)) + "]";
		return s.empty() ? std::string("（子なし）") : s;
	}

	std::string ProbeLayerInfo(MCObjectHandle layer)
	{
		if (layer == nil)
			return "レイヤが nil";
		TXString nm;
		gSDK->GetObjectName(layer, nm);
		double_gs scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return ProbeStr(nm) + "（縮尺 " + ProbeNum(scale) + "）";
	}

	int gProbeLabelCount = 0;

	WorldPt ProbeNextSpot()
	{
		return WorldPt(0, -3000.0 * (gProbeLabelCount++));
	}

	MCObjectHandle ProbeNewLabel(vwprobe::Report& probe, const std::string& tag)
	{
		MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
		if (h == nil)
			probe.log(tag + ": CreateCustomObject が nil を返した");
		return h;
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
				WorldCoord back = 0;
				gSDK->GetTextSize(dup, 0, back);
				probe.log(tag + ": レイアウトへ当てた直後 大きさ=" + ProbeNum(back) +
						  " 外接高=" + ProbeNum(ProbeHeightOf(dup)));
			}
			else
			{
				tookLine = true;
			}
		}
		gSDK->SetCustomObjectProfileGroup(h, hGroup);
		gSDK->ResetObject(h);
	}
} // namespace

VW_PROBE("drawing-label-style-textsize", "図面ラベルのスタイルと文字の大きさ",
		 "注釈の中の文字を物差しとの比で紙の pt に直す")
{
	gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);

	// -----------------------------------------------------------------------
	probe.log("=== 段取り ===");
	MCObjectHandle design = gSDK->CreateLayer("調査用デザイン", kLayerDesign);
	MCObjectHandle sheet = nil;
	if (design == nil)
	{
		probe.fail("CreateLayer(デザイン) が nil");
		return;
	}
	gSDK->SetLayerScaleN(design, kProbeVpScale);
	gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 10000.0, 10000.0);

	sheet = gSDK->CreateLayer("調査用シート", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("CreateLayer(シート) が nil");
		return;
	}
	gSDK->SetCurrentLayer(sheet);

	// parentHandle は「置く容れ物」。表示するデザインレイヤは後から決める。
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
	probe.log("ビューポートの親: " + ProbeLayerInfo(gSDK->ParentObject(vp)) + " / 縮尺 1/" +
			  ProbeNum(kProbeVpScale));

	// -----------------------------------------------------------------------
	// 段 R: **物差し。** 紙で 6pt になると確定している寸法を、この注釈へ 1 本置く。
	probe.log("=== 段 R: 物差しの寸法（紙で 6pt）を注釈へ置く ===");
	double worldPerPt = 0.0;
	{
		const double fontSize = kProbeRulerPt * kProbePtToMm * kProbeVpScale; // 105.83333
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(0, 5000), WorldPt(5000, 5000), 0,
														 0, Vector2(0, 0), 0);
		if (dim == nil)
		{
			probe.log("段 R: CreateLinearDimension が nil");
		}
		else
		{
			TVariableBlock showVar;
			showVar = true;
			gSDK->SetObjectVariable(dim, ovDimShowValue, showVar);
			TVariableBlock fontVar;
			fontVar = static_cast<Real64>(fontSize);
			gSDK->SetObjectVariable(dim, ovDimFontSize, fontVar);
			gSDK->ResetObject(dim);
			const Boolean ok = gSDK->AddViewportAnnotationObject(vp, dim);
			gSDK->ResetObject(dim);
			probe.log("段 R 注釈へ入れた=" + ProbeBoolStr(ok != 0) +
					  " 書いた ovDimFontSize=" + ProbeNum(fontSize));
			std::vector<ProbeTextHit> hits;
			ProbeCollectTexts(dim, 0, hits);
			probe.log("段 R 寸法の中: " + ProbeTextSummary(dim, 0.0));
			probe.log("段 R 寸法そのものの外接 高=" + ProbeNum(ProbeHeightOf(dim)));
			for (size_t i = 0; i < hits.size(); ++i)
			{
				if (hits[i].height > 0.0)
				{
					worldPerPt = hits[i].height / kProbeRulerPt;
					break;
				}
			}
			if (worldPerPt > 0.0)
				probe.log("段 R ★ 物差し（外接から）: 紙の 1pt ＝ 世界座標 " +
						  ProbeNum(worldPerPt));
		}
		if (worldPerPt <= 0.0)
		{
			worldPerPt = kProbePtToMm * kProbeVpScale;
			probe.log("段 R 物差しを外接から取れなかったので計算値を使う: " + ProbeNum(worldPerPt));
		}
	}

	// -----------------------------------------------------------------------
	probe.log("=== 段 C: 調査用のスタイル（既定レイアウトの出どころ）===");
	RefNumber toolRefOriginal = 0;
	gSDK->GetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	TXString styleName("調査用_図面ラベルスタイル");
	MCObjectHandle hSymDef = gSDK->CreateSymbolDefinition(styleName);
	RefNumber styleRef = toolRefOriginal;
	if (hSymDef != nil)
	{
		MCObjectHandle seed = ProbeNewLabel(probe, "段 C 種");
		if (seed == nil)
		{
			probe.fail("段 C: 種のラベルを作れなかった");
			return;
		}
		gSDK->AddObjectToContainer(seed, hSymDef);
		gSDK->ResetObject(hSymDef);
		gSDK->SetSymbolDefSubType(
			hSymDef, static_cast<Sint32>(VWFC::VWObjects::VWParametricObj::GetInternalID(seed)));
		gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);
		styleRef = gSDK->GetObjectInternalIndex(hSymDef);
	}
	probe.log("使うスタイル ref=" + ProbeInt(styleRef));
	if (styleRef == 0)
	{
		probe.fail("段 C: スタイルを用意できなかった");
		return;
	}

	// -----------------------------------------------------------------------
	// 段 L: ラベルを 6 通り置いて、物差しとの比で紙の pt を出す。
	probe.log("=== 段 L: ラベル ===");
	probe.log("与える値: 紙の 10pt ＝ " + ProbeNum(kProbeTenPtMm) +
			  "mm / 縮尺を掛けた値=" + ProbeNum(kProbeTenPtMm * kProbeVpScale));

	struct ProbeCase
	{
		const char* tag;
		WorldCoord size;
		int place; // 0=注釈 1=シートレイヤ直下(1:1) 2=デザインレイヤ(1/50)
		bool activeIsDesign; // ResetObject のときのアクティブレイヤ
	};
	const ProbeCase cases[] = {
		{"L1 注釈・与えた値=紙の 10pt mm・アクティブ 1:1", kProbeTenPtMm, 0, false},
		{"L2 注釈・与えた値=紙の 10pt mm・アクティブ 1/50", kProbeTenPtMm, 0, true},
		{"L3 注釈・与えた値=10pt mm × 縮尺・アクティブ 1:1", kProbeTenPtMm * kProbeVpScale, 0,
		 false},
		{"L4 シートレイヤ直下(1:1)・与えた値=紙の 10pt mm", kProbeTenPtMm, 1, false},
		{"L5 デザインレイヤ(1/50)・与えた値=紙の 10pt mm", kProbeTenPtMm, 2, true},
		{"L6 注釈・対照 大きさに触らない・アクティブ 1/50", 0, 0, true},
	};

	MCObjectHandle firstInAnnotation = nil;
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const ProbeCase& c = cases[i];
		probe.log(std::string("--- ") + c.tag + " ---");
		gSDK->SetCurrentLayer(c.activeIsDesign ? design : sheet);
		gSDK->SetPluginStyleForTool("Drawing Label2", styleRef);
		MCObjectHandle h = ProbeNewLabel(probe, c.tag);
		if (h == nil)
			continue;
		ProbeRebuildLayout(probe, c.tag, h, c.size);

		if (c.place == 0)
		{
			const Boolean ok = gSDK->AddViewportAnnotationObject(vp, h);
			probe.log(std::string(c.tag) + ": 注釈へ入れた=" + ProbeBoolStr(ok != 0));
			if (firstInAnnotation == nil)
				firstInAnnotation = h;
		}
		else
		{
			gSDK->AddObjectToContainer(h, c.place == 1 ? sheet : design);
		}
		gSDK->SetCurrentLayer(c.activeIsDesign ? design : sheet);
		gSDK->ResetObject(h);
		gSDK->SetPluginObjectStyle(h, 0); // スタイル無しにする
		gSDK->ResetObject(h);

		const double wpp = (c.place == 1) ? kProbePtToMm : worldPerPt;
		probe.log(std::string(c.tag) + ": 外接 幅=" + ProbeNum(ProbeWidthOf(h)) +
				  " 高=" + ProbeNum(ProbeHeightOf(h)) + " Link State=" +
				  ProbeStr(VWFC::VWObjects::VWParametricObj(h).GetParamValue("Link State")));
		probe.log(std::string(c.tag) +
				  " レイアウト: " + ProbeTextSummary(gSDK->GetCustomObjectProfileGroup(h), 0.0));
		probe.log(std::string(c.tag) + " 描いた図形: " + ProbeTextSummary(h, wpp));
		probe.log(std::string(c.tag) + " 子の外接:" + ProbeChildBounds(h));
	}

	// -----------------------------------------------------------------------
	// 段 V: ビューポートの縮尺を 1/50 → 1/100 にしたら、紙の見え方は保たれるか。
	if (firstInAnnotation != nil)
	{
		probe.log("=== 段 V: ビューポートの縮尺を 1/50 → 1/100 ===");
		TVariableBlock v;
		v = static_cast<Real64>(100.0);
		gSDK->SetObjectVariable(vp, ovViewportScale, v);
		gSDK->UpdateViewport(vp);
		gSDK->ResetObject(firstInAnnotation);
		probe.log("V L1 の外接 幅=" + ProbeNum(ProbeWidthOf(firstInAnnotation)) +
				  " 高=" + ProbeNum(ProbeHeightOf(firstInAnnotation)));
		probe.log("V L1 描いた図形: " + ProbeTextSummary(firstInAnnotation, worldPerPt * 2.0));
		v = static_cast<Real64>(kProbeVpScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, v);
		gSDK->UpdateViewport(vp);
	}

	// -----------------------------------------------------------------------
	probe.log("=== 後片付け ===");
	gSDK->SetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	probe.log("ツールのスタイルを戻した ref=" + ProbeInt(toolRefOriginal));
	probe.log("おわり");
}
