//
//	probes/runtime/drawing-label-style-textsize/probe.cpp
//
//	[issue #175] 図面ラベル（Drawing Label2）——**残りは「注釈へ入れた 1 本目が
//	描き直らない」件だけ**。ここはそれを詰める。
//
//	ここまでに確定したこと（測り直さない）:
//	  * ツールのスタイルは CreateCustomObject で自動的に当たる。外すのは
//	    gSDK->SetPluginObjectStyle(h, 0)。VWParametricObj::SetStyle(0) は無反応。
//	  * **既定のレイアウトはスタイルが持っている**。スタイル無しで作ったラベルの
//	    プロファイルグループは空で何も描かない。外してもレイアウトはインスタンスに残る。
//	  * **レイアウトのテキストへ与える大きさは「紙の上の mm」。** 描かれる世界座標は
//	    「与えた値 × 容れ物の縮尺」になり、紙の上では与えた値そのものになる
//	    （5 回目: シートレイヤ 1:1 / デザインレイヤ 1/50 / 注釈 1/50・1/100 で確認）。
//	    **アクティブレイヤは効かない**（同じ値を A=1:1 と A=1/50 で入れて同じ結果）。
//	  * ビューポートの縮尺を変えても紙の見え方は保たれる（1/50→1/100 で 176.389→352.778）。
//	  * 文字スタイルを `SetTextStyleRef` で当てると、**当てた時点のアクティブレイヤの
//	    縮尺が焼き付く**ので、そのぶん二重に掛かる。`SetTextSize` で直に書くほうがよい。
//
//	**残った 1 つ。** 5 回目で、注釈へ**最初に**入れたラベル（L1）だけが
//	「描いたテキストが 1 件・外接の高さ 0」＝**縮尺の掛かった絵になっていなかった**。
//	後で（段 V で）もう一度 ResetObject したら正しくなった。4 回目は注釈の全部が
//	その状態だった。**プラグインがこれを踏むと、タイトルが紙で 0.2pt ＝ 見えない**ので、
//	「何回・どのタイミングで ResetObject すればよいか」を決めておく必要がある。
//
//	測ること:
//	  M1 注釈へ入れて ResetObject 1 回だけ                   → 描き直るか
//	  M2 同じことを 2 本目でやる                             → 「1 本目だけ」なのか
//	  M3 注釈へ入れて UpdateViewport → ResetObject           → これで決まるか
//	  M4 注釈へ入れてから**レイアウトを組み直す**（時機の対照）→ 前後で差が出るか
//	  最後に M1 を ResetObject し直して、後追いで直るかを見る（縮尺は変えない）。
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

	struct ProbeTextHit
	{
		double size;
		double height;
	};

	void ProbeCollectTexts(MCObjectHandle container, int depth, std::vector<ProbeTextHit>& out)
	{
		if (container == nil || depth > 6 || out.size() >= 8)
			return;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil; m = gSDK->NextObject(m))
		{
			if (gSDK->GetObjectTypeN(m) == kProbeTextNodeType)
			{
				WorldCoord size = 0;
				gSDK->GetTextSize(m, 0, size);
				out.push_back(ProbeTextHit{static_cast<double>(size), ProbeHeightOf(m)});
				if (out.size() >= 8)
					return;
			}
			ProbeCollectTexts(m, depth + 1, out);
		}
	}

	// 物差しは 2 本取る（大きさ用と外接用。テキストの外接は大きさの 1.4〜1.5 倍あるので、
	// 大きさ同士・外接同士で比べないと紙の pt を読み違える）。
	double gProbeWorldPerPtSize = 0.0;
	double gProbeWorldPerPtBounds = 0.0;

	std::string ProbeDrawnSummary(MCObjectHandle h)
	{
		std::vector<ProbeTextHit> hits;
		ProbeCollectTexts(h, 0, hits);
		if (hits.empty())
			return "テキスト 0 件";
		std::string s = "テキスト " + ProbeInt(static_cast<Sint32>(hits.size())) + " 件:";
		for (size_t i = 0; i < hits.size(); ++i)
		{
			s += " 大きさ=" + ProbeNum(hits[i].size);
			if (gProbeWorldPerPtSize > 0.0)
				s += "(紙で" + ProbeNum(hits[i].size / gProbeWorldPerPtSize) + "pt)";
			s += " 外接高=" + ProbeNum(hits[i].height);
			if (gProbeWorldPerPtBounds > 0.0 && hits[i].height >= 0.0)
				s += "(紙で" + ProbeNum(hits[i].height / gProbeWorldPerPtBounds) + "pt)";
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
		gSDK->ResetObject(h);
	}

	void ProbeReport(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		probe.log(tag + ": 外接 幅=" + ProbeNum(ProbeWidthOf(h)) +
				  " 高=" + ProbeNum(ProbeHeightOf(h)) + " / " + ProbeDrawnSummary(h));
	}
} // namespace

VW_PROBE("drawing-label-style-textsize", "図面ラベルのスタイルと文字の大きさ",
		 "注釈の 1 本目が描き直らない件を詰める")
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

	// -----------------------------------------------------------------------
	// スタイル（既定レイアウトの出どころなので要る）
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
	gSDK->SetPluginStyleForTool("Drawing Label2", styleRef);
	probe.log("使うスタイル ref=" + ProbeInt(styleRef));

	// -----------------------------------------------------------------------
	// **M1 を先に置く。** 物差しの寸法は後から入れる——5 回目は寸法を先に入れており、
	// それが 1 本目の扱いを変えていた見込みを潰すため。
	probe.log("=== M1: 注釈へ入れて ResetObject 1 回だけ（注釈の 1 本目）===");
	MCObjectHandle m1 = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
	if (m1 == nil)
	{
		probe.fail("M1 のラベルを作れなかった");
		return;
	}
	ProbeRebuildLayout(probe, "M1", m1, kProbeTenPtMm);
	probe.log(std::string("M1 注釈へ入れた=") +
			  ProbeBoolStr(gSDK->AddViewportAnnotationObject(vp, m1) != 0));
	gSDK->ResetObject(m1);
	gSDK->SetPluginObjectStyle(m1, 0);
	gSDK->ResetObject(m1);
	probe.log("M1 ★ ResetObject までの時点（物差しはまだ無い。数値は生の世界座標）:");
	ProbeReport(probe, "M1", m1);

	// -----------------------------------------------------------------------
	probe.log("=== 段 R: 物差しの寸法（紙で 6pt）を注釈へ置く ===");
	{
		const double fontSize = kProbeRulerPt * kProbePtToMm * kProbeVpScale; // 105.83333
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(0, 8000), WorldPt(5000, 8000), 0,
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
			gSDK->AddViewportAnnotationObject(vp, dim);
			gSDK->ResetObject(dim);
			std::vector<ProbeTextHit> hits;
			ProbeCollectTexts(dim, 0, hits);
			if (!hits.empty() && hits[0].size > 0.0)
			{
				gProbeWorldPerPtSize = hits[0].size / kProbeRulerPt;
				gProbeWorldPerPtBounds = hits[0].height / kProbeRulerPt;
				probe.log("段 R ★ 物差し: 紙の 1pt ＝ 大きさで " + ProbeNum(gProbeWorldPerPtSize) +
						  " / 外接で " + ProbeNum(gProbeWorldPerPtBounds));
			}
			else
			{
				probe.log("段 R: 寸法の中にテキストが見つからなかった");
			}
		}
	}
	probe.log("M1 を物差し付きで読み直す（触っていない）:");
	ProbeReport(probe, "M1 再読み", m1);

	// -----------------------------------------------------------------------
	probe.log("=== M2: 2 本目を同じ手順で（ResetObject 1 回だけ）===");
	MCObjectHandle m2 = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
	if (m2 != nil)
	{
		gSDK->SetPluginStyleForTool("Drawing Label2", styleRef);
		ProbeRebuildLayout(probe, "M2", m2, kProbeTenPtMm);
		probe.log(std::string("M2 注釈へ入れた=") +
				  ProbeBoolStr(gSDK->AddViewportAnnotationObject(vp, m2) != 0));
		gSDK->ResetObject(m2);
		gSDK->SetPluginObjectStyle(m2, 0);
		gSDK->ResetObject(m2);
		ProbeReport(probe, "M2", m2);
	}

	// -----------------------------------------------------------------------
	probe.log("=== M3: 注釈へ入れて UpdateViewport → ResetObject ===");
	MCObjectHandle m3 = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
	if (m3 != nil)
	{
		ProbeRebuildLayout(probe, "M3", m3, kProbeTenPtMm);
		probe.log(std::string("M3 注釈へ入れた=") +
				  ProbeBoolStr(gSDK->AddViewportAnnotationObject(vp, m3) != 0));
		gSDK->UpdateViewport(vp);
		gSDK->ResetObject(m3);
		gSDK->SetPluginObjectStyle(m3, 0);
		gSDK->ResetObject(m3);
		ProbeReport(probe, "M3", m3);
	}

	// -----------------------------------------------------------------------
	probe.log("=== M4: 注釈へ入れてから**レイアウトを組み直す**（時機の対照）===");
	MCObjectHandle m4 = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
	if (m4 != nil)
	{
		probe.log(std::string("M4 注釈へ入れた=") +
				  ProbeBoolStr(gSDK->AddViewportAnnotationObject(vp, m4) != 0));
		gSDK->ResetObject(m4);
		ProbeRebuildLayout(probe, "M4", m4, kProbeTenPtMm);
		gSDK->SetPluginObjectStyle(m4, 0);
		gSDK->ResetObject(m4);
		ProbeReport(probe, "M4", m4);
	}

	// -----------------------------------------------------------------------
	// **M1 を後追いで直せるか。** 縮尺は変えない（5 回目は縮尺を変えたついでだった）。
	probe.log("=== M5: M1 をもう一度 ResetObject するだけ（縮尺は変えない）===");
	gSDK->ResetObject(m1);
	ProbeReport(probe, "M5 ResetObject 1 回追加", m1);
	gSDK->UpdateViewport(vp);
	gSDK->ResetObject(m1);
	ProbeReport(probe, "M5 UpdateViewport ＋ ResetObject", m1);

	probe.log("=== 後片付け ===");
	gSDK->SetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	probe.log("ツールのスタイルを戻した ref=" + ProbeInt(toolRefOriginal));
	probe.log("おわり");
}
