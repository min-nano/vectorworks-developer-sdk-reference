//
//	probes/runtime/drawing-label-style-textsize/probe.cpp
//
//	[issue #175] 図面ラベル（Drawing Label2）のレイアウトの中の文字の大きさを、
//	**ビューポートの注釈の中で**測る。
//
//	1・2 回目で割れたこと（ここでは測り直さない）:
//	  * ツールのスタイルは CreateCustomObject で自動的に当たる。
//	  * gSDK->SetPluginObjectStyle(h, 0) で外せる（ResetObject を越えて残る）。
//	    VWParametricObj::SetStyle(0) は無反応。
//	  * **既定のレイアウトはスタイルが持っている**——スタイル無しで作ったラベルの
//	    プロファイルグループは空で、何も描かない。外すと**レイアウトはインスタンスへ残る**。
//	  * 置き場の縮尺が掛かる: レイアウトへ与えた大きさ × その容れ物の縮尺が描かれる
//	    （シートレイヤ 1:1 で ×1、デザインレイヤ 1/50 で ×50）。
//
//	2 回目で測れなかったのは**注釈**だけで、原因は段取りにあった:
//	**CreateLayer(…, kLayerSheet) はアクティブレイヤを切り替えない**（文字スタイルが
//	1/50 で解けたこと、置かないラベルが ×50 で描かれたことから分かる）。そのため
//	ビューポートがシートレイヤに載らず、AddViewportAnnotationObject が常に false だった。
//	**ISDK には SetCurrentLayer がある**ので、ここではそれで明示的に切り替える。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// Objs.TDType.h の型番号（Findings「Drawing Labels」）。
	const short kProbeTextNodeType = 10;
	const short kProbeLineNodeType = 2;

	const double kProbeVpScale = 50.0;
	const double kProbePtToMm = 25.4 / 72.0;		  // 1pt = 0.352778mm
	const double kProbeTenPtMm = 10.0 * kProbePtToMm; // 3.52778
	const double kProbeTenPtInch = 10.0 / 72.0; // 文字スタイルの ovTextStyleSize はインチ

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

	std::string ProbeStyleInfo(MCObjectHandle h)
	{
		RefNumber ref = 0;
		gSDK->GetPluginObjectStyle(h, ref);
		return "styleRef=" + ProbeInt(ref);
	}

	// 深さ付きでテキストを集める（ラベルが描いた図形は群の入れ子）。
	struct ProbeTextHit
	{
		double size;
		int depth;
	};

	void ProbeCollectTexts(MCObjectHandle container, int depth, std::vector<ProbeTextHit>& out)
	{
		if (container == nil || depth > 6 || out.size() >= 16)
			return;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil; m = gSDK->NextObject(m))
		{
			if (gSDK->GetObjectTypeN(m) == kProbeTextNodeType)
			{
				WorldCoord size = 0;
				gSDK->GetTextSize(m, 0, size);
				out.push_back(ProbeTextHit{static_cast<double>(size), depth});
				if (out.size() >= 16)
					return;
			}
			ProbeCollectTexts(m, depth + 1, out);
		}
	}

	std::string ProbeTextSummary(MCObjectHandle container, double worldPerPt)
	{
		std::vector<ProbeTextHit> hits;
		ProbeCollectTexts(container, 0, hits);
		if (hits.empty())
			return "テキスト 0 件";
		std::string s = "テキスト " + ProbeInt(static_cast<Sint32>(hits.size())) + " 件:";
		for (size_t i = 0; i < hits.size(); ++i)
		{
			s += " [深さ" + ProbeInt(hits[i].depth) + "]" + ProbeNum(hits[i].size);
			if (worldPerPt > 0.0)
				s += "(紙で" + ProbeNum(hits[i].size / worldPerPt) + "pt)";
		}
		return s;
	}

	// 外接の高さも「紙で何 pt か」に直して出す（描かれているのがどちらのテキストかの裏取り）。
	std::string ProbeBounds(MCObjectHandle h, double worldPerPt)
	{
		WorldRect r;
		if (!gSDK->GetObjectBounds(h, r))
			return "GetObjectBounds=false";
		const double hh = std::fabs(r.top - r.bottom);
		std::string s =
			"外接 幅=" + ProbeNum(std::fabs(r.right - r.left)) + " 高さ=" + ProbeNum(hh);
		if (worldPerPt > 0.0)
			s += "（高さは紙で " + ProbeNum(hh / worldPerPt) + "pt）";
		return s;
	}

	std::string ProbeShapeSummary(MCObjectHandle container)
	{
		if (container == nil)
			return "容れ物が nil";
		std::string s;
		int n = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil && n < 16;
			 m = gSDK->NextObject(m), ++n)
			s += " " + ProbeInt(gSDK->GetObjectTypeN(m));
		return "型の並び:" + (s.empty() ? std::string(" （空）") : s);
	}

	std::string ProbeLayerInfo(MCObjectHandle layer)
	{
		if (layer == nil)
			return "レイヤが nil";
		TXString nm;
		gSDK->GetObjectName(layer, nm);
		double_gs scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		TVariableBlock typeVar;
		Sint32 type = -1;
		if (gSDK->GetObjectVariable(layer, ovLayerType, typeVar))
		{
			short s16 = 0;
			if (typeVar.GetShort(s16))
				type = s16;
		}
		return "名前=" + ProbeStr(nm) + " 縮尺=" + ProbeNum(scale) +
			   " ovLayerType=" + ProbeInt(type);
	}

	int gProbeLabelCount = 0;

	WorldPt ProbeNextSpot()
	{
		return WorldPt(0, -2000.0 * (gProbeLabelCount++));
	}

	MCObjectHandle ProbeNewLabel(vwprobe::Report& probe, const std::string& tag)
	{
		MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
		if (h == nil)
			probe.log(tag + ": CreateCustomObject(\"Drawing Label2\") が nil を返した");
		return h;
	}

	// レイアウトを「タイトルのテキスト＋下線」だけに組み直す。
	double ProbeRebuildLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h,
							  WorldCoord sizeWorld, InternalIndex tsRef)
	{
		double applied = -1.0;
		MCObjectHandle hOld = gSDK->GetCustomObjectProfileGroup(h);
		if (hOld == nil)
		{
			probe.log(tag + ": レイアウトが nil で組み直せない");
			return applied;
		}
		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		if (hGroup == nil)
		{
			probe.log(tag + ": CreateGroup が nil を返した");
			return applied;
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
				if (tsRef != 0)
					gSDK->SetTextStyleRef(dup, tsRef);
				WorldCoord back = 0;
				gSDK->GetTextSize(dup, 0, back);
				applied = static_cast<double>(back);
				probe.log(tag +
						  ": レイアウトのテキストへ当てた直後 GetTextSize=" + ProbeNum(applied) +
						  " GetTextStyleRef=" + ProbeInt(gSDK->GetTextStyleRef(dup)));
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
		return applied;
	}
} // namespace

VW_PROBE("drawing-label-style-textsize", "図面ラベルのスタイルと文字の大きさ",
		 "注釈の中でレイアウトの文字の大きさを実測する")
{
	gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);

	// -----------------------------------------------------------------------
	probe.log("=== 段取り ===");
	MCObjectHandle design = gSDK->CreateLayer("調査用デザイン", kLayerDesign);
	if (design == nil)
	{
		probe.fail("CreateLayer(デザイン) が nil を返した");
		return;
	}
	gSDK->SetLayerScaleN(design, kProbeVpScale);
	gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 10000.0, 10000.0);

	MCObjectHandle sheet = gSDK->CreateLayer("調査用シート", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("CreateLayer(シート) が nil を返した");
		return;
	}
	probe.log("シートレイヤ: " + ProbeLayerInfo(sheet));
	probe.log("★ CreateLayer(kLayerSheet) の直後のアクティブレイヤ: " +
			  ProbeLayerInfo(gSDK->GetActiveLayer()));

	// **ここが 2 回目との違い。** ISDK::SetCurrentLayer で明示的にシートレイヤへ移る。
	gSDK->SetCurrentLayer(sheet);
	probe.log("★ SetCurrentLayer(sheet) の後のアクティブレイヤ: " +
			  ProbeLayerInfo(gSDK->GetActiveLayer()));

	MCObjectHandle vp = gSDK->CreateViewport(design);
	if (vp == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	probe.log(std::string("ビューポートの親はシートレイヤか: ") +
			  ProbeBoolStr(gSDK->ParentObject(vp) == sheet) + " / 親 " +
			  ProbeLayerInfo(gSDK->ParentObject(vp)));
	TVariableBlock scaleVar;
	scaleVar = static_cast<Real64>(kProbeVpScale);
	gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
	gSDK->UpdateViewport(vp);

	// 注釈の中では「紙の 1pt ＝ ビューポートの縮尺 × 0.352778mm」（Findings「Dimensions」）。
	const double worldPerPtVp = kProbePtToMm * kProbeVpScale; // 17.63889
	const double worldPerPtSheet = kProbePtToMm;			  // 0.352778

	// -----------------------------------------------------------------------
	probe.log("=== 段 A: 注釈へ入れられるか ===");
	{
		MCObjectHandle r1 = gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 100.0, 100.0);
		const bool ok = (r1 != nil) && (gSDK->AddViewportAnnotationObject(vp, r1) != 0);
		probe.log(std::string("A1 矩形 → AddViewportAnnotationObject=") + ProbeBoolStr(ok) +
				  " IsViewportGroupContainedObject=" +
				  (r1 != nil ? ProbeBoolStr(gSDK->IsViewportGroupContainedObject(
												r1, kViewportGroupAnnotation) != 0)
							 : "—"));
		probe.log(std::string("A1 後の注釈群=") +
				  (gSDK->GetViewportGroup(vp, kViewportGroupAnnotation) != nil ? "取れる" : "nil"));
	}

	auto putInAnnotation = [&probe, vp](const std::string& tag, MCObjectHandle h)
	{
		const Boolean ok = gSDK->AddViewportAnnotationObject(vp, h);
		gSDK->ResetObject(h);
		probe.log(
			tag + ": 注釈へ入れた=" + ProbeBoolStr(ok != 0) + " 注釈の中か=" +
			ProbeBoolStr(gSDK->IsViewportGroupContainedObject(h, kViewportGroupAnnotation) != 0) +
			" Link State=" +
			ProbeStr(VWFC::VWObjects::VWParametricObj(h).GetParamValue("Link State")));
	};

	// -----------------------------------------------------------------------
	probe.log("=== 段 C: 調査用のスタイルを作る（既定レイアウトの出どころ）===");
	RefNumber toolRefOriginal = 0;
	gSDK->GetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	probe.log("ツールの元のスタイル ref=" + ProbeInt(toolRefOriginal));

	TXString styleName("調査用_図面ラベルスタイル");
	MCObjectHandle hSymDef = gSDK->CreateSymbolDefinition(styleName);
	RefNumber styleRef = 0;
	if (hSymDef == nil)
	{
		probe.log("CreateSymbolDefinition が nil（同名が在る？）→ 元のツールのスタイルで進む");
		styleRef = toolRefOriginal;
	}
	else
	{
		MCObjectHandle seed = ProbeNewLabel(probe, "段 C 種");
		if (seed == nil)
		{
			probe.fail("段 C: 種の図面ラベルを作れなかった");
			return;
		}
		gSDK->AddObjectToContainer(seed, hSymDef);
		gSDK->ResetObject(hSymDef);
		gSDK->SetSymbolDefSubType(
			hSymDef, static_cast<Sint32>(VWFC::VWObjects::VWParametricObj::GetInternalID(seed)));
		gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);
		styleRef = gSDK->GetObjectInternalIndex(hSymDef);
		probe.log("段 C IsPluginStyle=" + ProbeBoolStr(gSDK->IsPluginStyle(hSymDef)) +
				  " ref=" + ProbeInt(styleRef));
	}
	if (styleRef == 0)
	{
		probe.fail("段 C: 当てられるスタイルが用意できなかった");
		return;
	}
	gSDK->SetPluginStyleForTool("Drawing Label2", styleRef);

	// -----------------------------------------------------------------------
	// 段 T: 文字スタイルは「当てたときのアクティブレイヤの縮尺」で解けるのか。
	probe.log("=== 段 T: 文字スタイルはいつの縮尺で解けるか ===");
	InternalIndex tsRef = 0;
	{
		MCObjectHandle hTS = gSDK->CreateTextStyleResource("調査用(10pt)");
		if (hTS != nil)
		{
			TVariableBlock sizeVar;
			sizeVar = static_cast<Real64>(kProbeTenPtInch);
			gSDK->SetObjectVariable(hTS, ovTextStyleSize, sizeVar);
			tsRef = gSDK->GetObjectInternalIndex(hTS);
			probe.log("文字スタイル ref=" + ProbeInt(tsRef) + " ovTextStyleSize（インチ）=" +
					  ProbeNum(kProbeTenPtInch) + "（＝紙で 10pt）");
		}
		else
		{
			probe.log("CreateTextStyleResource が nil（文字スタイルの枝は測れない）");
		}
	}
	if (tsRef != 0)
	{
		gSDK->SetCurrentLayer(design); // 1/50
		MCObjectHandle t1 = ProbeNewLabel(probe, "T1");
		if (t1 != nil)
			probe.log("T1 アクティブ=1/50 のとき当てた値: " +
					  ProbeNum(ProbeRebuildLayout(probe, "T1", t1, 0, tsRef)));
		gSDK->SetCurrentLayer(sheet); // 1:1
		MCObjectHandle t2 = ProbeNewLabel(probe, "T2");
		if (t2 != nil)
			probe.log("T2 アクティブ=1:1 のとき当てた値: " +
					  ProbeNum(ProbeRebuildLayout(probe, "T2", t2, 0, tsRef)));
	}

	// -----------------------------------------------------------------------
	probe.log("=== 段 E: レイアウトの中の文字の大きさ ===");
	probe.log("与える値: 10pt を紙の mm へ=" + ProbeNum(kProbeTenPtMm) +
			  " / 縮尺を掛けた値=" + ProbeNum(kProbeTenPtMm * kProbeVpScale));
	probe.log("物差し: 注釈（1/50）では紙の 1pt ＝ 世界座標 " + ProbeNum(worldPerPtVp) +
			  " / シートレイヤ（1:1）では " + ProbeNum(worldPerPtSheet));

	struct ProbeCase
	{
		const char* tag;
		WorldCoord size;
		bool useTextStyle;
		bool afterPlacing;
		int place; // 0=注釈 1=シートレイヤ直下 2=デザインレイヤ
		const char* scaleFactor;
		const char* worldBased;
	};
	const ProbeCase cases[] = {
		{"E1 SetTextSize(紙の 10pt mm)・注釈・入れる前", kProbeTenPtMm, false, false, 0, nullptr,
		 nullptr},
		{"E2 SetTextSize(紙の 10pt mm)・注釈・入れた後", kProbeTenPtMm, false, true, 0, nullptr,
		 nullptr},
		{"E3 SetTextSize(10pt mm × 縮尺)・注釈・入れる前", kProbeTenPtMm * kProbeVpScale, false,
		 false, 0, nullptr, nullptr},
		{"E4 文字スタイル(10pt)・注釈・入れる前", 0, true, false, 0, nullptr, nullptr},
		{"E5 文字スタイル(10pt)・注釈・入れた後", 0, true, true, 0, nullptr, nullptr},
		{"E6 対照 大きさに触らない・注釈", 0, false, false, 0, nullptr, nullptr},
		{"E7 SetTextSize(紙の 10pt mm)・シートレイヤ直下(1:1)", kProbeTenPtMm, false, false, 1,
		 nullptr, nullptr},
		{"E8 SetTextSize(紙の 10pt mm)・デザインレイヤ(1/50)", kProbeTenPtMm, false, false, 2,
		 nullptr, nullptr},
		{"E9 SetTextSize(紙の 10pt mm) ＋ ScaleFactor=2・注釈", kProbeTenPtMm, false, false, 0, "2",
		 nullptr},
		{"E10 SetTextSize(紙の 10pt mm) ＋ World-based=True・注釈", kProbeTenPtMm, false, false, 0,
		 nullptr, "True"},
	};

	MCObjectHandle firstInAnnotation = nil;
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const ProbeCase& c = cases[i];
		if (c.useTextStyle && tsRef == 0)
			continue;
		probe.log(std::string("--- ") + c.tag + " ---");
		// **当てるときのアクティブレイヤを置き場に合わせる**（段 T のとおり文字スタイルが
		// そこで解けるため）。注釈はシートレイヤの上なので 1:1 側に合わせる。
		gSDK->SetCurrentLayer(c.place == 2 ? design : sheet);
		gSDK->SetPluginStyleForTool("Drawing Label2", styleRef); // 既定レイアウトが要る
		MCObjectHandle h = ProbeNewLabel(probe, c.tag);
		if (h == nil)
			continue;
		VWFC::VWObjects::VWParametricObj pio(h);
		if (c.scaleFactor != nullptr)
			pio.SetParamValue("ScaleFactor", c.scaleFactor);
		if (c.worldBased != nullptr)
			pio.SetParamValue("World-based", c.worldBased);

		const InternalIndex useTS = c.useTextStyle ? tsRef : 0;
		if (!c.afterPlacing)
			ProbeRebuildLayout(probe, c.tag, h, c.size, useTS);

		if (c.place == 0)
		{
			putInAnnotation(c.tag, h);
			if (firstInAnnotation == nil)
				firstInAnnotation = h;
		}
		else
		{
			gSDK->AddObjectToContainer(h, c.place == 1 ? sheet : design);
			gSDK->ResetObject(h);
		}

		if (c.afterPlacing)
			ProbeRebuildLayout(probe, c.tag, h, c.size, useTS);

		gSDK->SetPluginObjectStyle(h, 0); // スタイル無しにする（レイアウトは残る）
		gSDK->ResetObject(h);

		const double wpp = (c.place == 1) ? worldPerPtSheet : worldPerPtVp;
		probe.log(std::string(c.tag) + ": " + ProbeStyleInfo(h) +
				  " ScaleFactor=" + ProbeStr(pio.GetParamValue("ScaleFactor")) + " World-based=" +
				  ProbeStr(pio.GetParamValue("World-based")) + " / " + ProbeBounds(h, wpp));
		probe.log(std::string(c.tag) +
				  " レイアウト: " + ProbeTextSummary(gSDK->GetCustomObjectProfileGroup(h), 0.0));
		probe.log(std::string(c.tag) + " 描いた図形: " + ProbeTextSummary(h, wpp) + " / " +
				  ProbeShapeSummary(h));
	}

	// -----------------------------------------------------------------------
	// 段 V: ビューポートの縮尺を後から変えたら、注釈の中のラベルはどうなるか。
	//	（寸法は「紙の見え方が変わらないように」書き換わる——Findings「Dimensions」）
	if (firstInAnnotation != nil)
	{
		probe.log("=== 段 V: ビューポートの縮尺を 1/50 → 1/100 に変える ===");
		TVariableBlock v;
		v = static_cast<Real64>(100.0);
		gSDK->SetObjectVariable(vp, ovViewportScale, v);
		gSDK->UpdateViewport(vp);
		gSDK->ResetObject(firstInAnnotation);
		probe.log("V 縮尺 1/100 での E1: " + ProbeBounds(firstInAnnotation, kProbePtToMm * 100.0));
		probe.log("V 縮尺 1/100 での E1 描いた図形: " +
				  ProbeTextSummary(firstInAnnotation, kProbePtToMm * 100.0));
		v = static_cast<Real64>(kProbeVpScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, v);
		gSDK->UpdateViewport(vp);
	}

	// -----------------------------------------------------------------------
	probe.log("=== 後片付け ===");
	gSDK->SetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	{
		RefNumber back = 0;
		gSDK->GetPluginStyleForTool("Drawing Label2", back);
		probe.log("ツールのスタイルを戻した: ref=" + ProbeInt(back));
	}
	probe.log("おわり");
}
