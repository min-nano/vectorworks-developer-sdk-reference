//
//	probes/runtime/drawing-label-style-textsize/probe.cpp
//
//	[issue #175] 図面ラベル（Drawing Label2）について 3 つを実測する。
//
//	1. CreateCustomObject はツールに設定されたスタイルを自動で当てるか。
//	2. 当たったスタイルをインスタンスから外す道はあるか。
//	3. ラベルレイアウトの中のテキストの大きさは、どの単位・どの縮尺で解かれるか。
//
//	1 と 2 は 1 回目の走行（build b990edcef722）で割れた。**2 回目のここは 3 を測り直す。**
//	1 回目が測れなかった理由は 2 つで、どちらもこの版で直してある:
//
//	  * **既定のレイアウトはスタイルが持っている。** ツールのスタイルを 0 にしてから
//	    作ったラベルのプロファイルグループは**空**で、複製する元が無かった。だから
//	    ここでは**スタイルが当たった状態で作り、レイアウトを組み直してから
//	    SetPluginObjectStyle(h, 0) で外す**（1 回目でこの順なら保たれると確かめてある）。
//	  * **ラベルが描いた図形は入れ子**（型 11 の群の中）なので、1 階層だけ舐めても
//	    テキストに当たらなかった。ここでは**再帰**で数える。
//
//	絶対の物差しとして、**紙で 6pt になると分かっている寸法**（Findings「Dimensions」
//	#143/#161: ovDimFontSize ＝ 紙の pt × 25.4/72 × ビューポートの縮尺）を同じ注釈へ
//	1 本置いて測る。ラベルの文字はこれとの比で紙の pt に直せる——目視は要らない。
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

	const double kProbeVpScale = 50.0;		 // ビューポート／デザインレイヤの縮尺
	const double kProbePtToMm = 25.4 / 72.0; // 1pt = 0.352778mm
	const double kProbeTenPtMm = 10.0 * kProbePtToMm; // 3.52778
	const double kProbeTenPtInch = 10.0 / 72.0; // 文字スタイルの ovTextStyleSize はインチ
	const double kProbeRulerPt = 6.0;			// 物差しの寸法の紙の大きさ

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

	// スタイルの当たり具合。**戻り値ではなく ref を見る**（Findings「Parametric Objects」）。
	std::string ProbeStyleInfo(MCObjectHandle h)
	{
		RefNumber ref = 0;
		const Boolean ret = gSDK->GetPluginObjectStyle(h, ref);
		std::string s = "GetPluginObjectStyle=" + ProbeBoolStr(ret != 0) + " ref=" + ProbeInt(ref);
		if (ref != 0)
		{
			TXString nm;
			gSDK->InternalIndexToNameN(static_cast<InternalIndex>(ref), nm);
			s += " 名前=" + ProbeStr(nm);
		}
		else
		{
			s += "（スタイル無し）";
		}
		return s;
	}

	std::string ProbeBounds(MCObjectHandle h)
	{
		WorldRect r;
		if (!gSDK->GetObjectBounds(h, r))
			return "GetObjectBounds=false";
		return "外接 幅=" + ProbeNum(std::fabs(r.right - r.left)) +
			   " 高さ=" + ProbeNum(std::fabs(r.top - r.bottom));
	}

	// **再帰で**テキストを集める。ラベルが描いた図形は群（型 11）の入れ子になっていて、
	// 1 階層だけ舐めてもテキストに当たらない（1 回目の走行で踏んだ）。
	void ProbeCollectTextSizes(MCObjectHandle container, int depth, std::vector<double>& out)
	{
		if (container == nil || depth > 6 || out.size() >= 24)
			return;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil; m = gSDK->NextObject(m))
		{
			if (gSDK->GetObjectTypeN(m) == kProbeTextNodeType)
			{
				WorldCoord size = 0;
				gSDK->GetTextSize(m, 0, size);
				out.push_back(static_cast<double>(size));
				if (out.size() >= 24)
					return;
			}
			ProbeCollectTextSizes(m, depth + 1, out);
		}
	}

	std::string ProbeTextSummary(MCObjectHandle container)
	{
		std::vector<double> sizes;
		ProbeCollectTextSizes(container, 0, sizes);
		if (sizes.empty())
			return "テキスト 0 件";
		std::string s = "テキスト " + ProbeInt(static_cast<Sint32>(sizes.size())) + " 件:";
		for (size_t i = 0; i < sizes.size(); ++i)
			s += " " + ProbeNum(sizes[i]);
		return s;
	}

	// 1 階層の顔ぶれ（型の並び）。構造を見たいときだけ使う。
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

	// 作ったラベルを縦にずらして置く（図を見たときに見分けが付くように）。
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

	// レイアウトを「タイトルのテキスト＋下線」だけに組み直す（Findings「Drawing Labels」の手順）。
	// sizeWorld > 0 なら SetTextSize を、tsRef != 0 なら SetTextStyleRef を、複製したテキストへ当てる。
	bool ProbeRebuildLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h,
							WorldCoord sizeWorld, InternalIndex tsRef)
	{
		MCObjectHandle hOld = gSDK->GetCustomObjectProfileGroup(h);
		if (hOld == nil)
		{
			probe.log(tag + ": レイアウトが nil で組み直せない");
			return false;
		}
		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		if (hGroup == nil)
		{
			probe.log(tag + ": CreateGroup が nil を返した");
			return false;
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
			{
				probe.log(tag + ": DuplicateObject が nil を返した（型=" + ProbeInt(type) + "）");
				continue;
			}
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
				probe.log(tag + ": 複製したテキストへ当てた直後 GetTextSize=" + ProbeNum(back) +
						  " GetTextStyleRef=" + ProbeInt(gSDK->GetTextStyleRef(dup)) +
						  " 文字数=" + ProbeInt(len));
			}
			else
			{
				tookLine = true;
			}
		}
		if (!tookText)
			probe.log(tag + ": **元のレイアウトにテキストが無かった**（複製する元が無い）");
		const Boolean ok = gSDK->SetCustomObjectProfileGroup(h, hGroup);
		gSDK->ResetObject(h);
		return ok != 0;
	}

	// 1 本ぶんの測り。紙の pt は「描いた文字の大きさ ÷ 1pt あたりの世界座標」で出す。
	void ProbeMeasure(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h,
					  double worldPerPt)
	{
		if (h == nil)
		{
			probe.log(tag + ": ハンドルが nil");
			return;
		}
		VWFC::VWObjects::VWParametricObj pio(h);
		probe.log(tag + ": " + ProbeStyleInfo(h) + " / " + ProbeBounds(h));
		probe.log(tag + ": ScaleFactor=" + ProbeStr(pio.GetParamValue("ScaleFactor")) +
				  " World-based=" + ProbeStr(pio.GetParamValue("World-based")) +
				  " Link State=" + ProbeStr(pio.GetParamValue("Link State")));
		probe.log(tag + " レイアウト: " + ProbeTextSummary(gSDK->GetCustomObjectProfileGroup(h)));

		std::vector<double> drawn;
		ProbeCollectTextSizes(h, 0, drawn);
		if (drawn.empty())
		{
			probe.log(tag + " 描いた図形: テキスト 0 件 / " + ProbeShapeSummary(h));
		}
		else
		{
			std::string line = tag + " 描いた図形: テキスト " +
							   ProbeInt(static_cast<Sint32>(drawn.size())) + " 件:";
			for (size_t i = 0; i < drawn.size(); ++i)
			{
				line += " " + ProbeNum(drawn[i]);
				if (worldPerPt > 0.0)
					line += "（紙で " + ProbeNum(drawn[i] / worldPerPt) + "pt）";
			}
			probe.log(line);
		}
	}
} // namespace

VW_PROBE("drawing-label-style-textsize", "図面ラベルのスタイルと文字の大きさ",
		 "既定レイアウトの出どころと、レイアウトの文字の大きさを実測する")
{
	// 作る前に 1 度。これが無いと最初の 1 個で「オブジェクトの設定」ダイアログが出る。
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
	// **ビューポートの元になる図形を置く。** 1 回目は空のデザインレイヤから
	// ビューポートを作っており、注釈へ入れられなかった原因の候補だった。
	MCObjectHandle seedRect =
		gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 10000.0, 10000.0);
	probe.log(std::string("デザインレイヤの目印の矩形: ") + (seedRect != nil ? "作れた" : "nil"));

	MCObjectHandle sheet = gSDK->CreateLayer("調査用シート", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("CreateLayer(シート) が nil を返した");
		return;
	}

	MCObjectHandle vp = gSDK->CreateViewport(design);
	if (vp == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	TVariableBlock scaleVar;
	scaleVar = static_cast<Real64>(kProbeVpScale);
	gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
	gSDK->UpdateViewport(vp);
	{
		TVariableBlock readBack;
		Real64 s = 0.0;
		if (gSDK->GetObjectVariable(vp, ovViewportScale, readBack) && readBack.GetReal64(s))
			probe.log("ビューポートの縮尺 ovViewportScale=" + ProbeNum(s));
		probe.log(std::string("注釈群 GetViewportGroup(kViewportGroupAnnotation)=") +
				  (gSDK->GetViewportGroup(vp, kViewportGroupAnnotation) != nil ? "取れる" : "nil"));
	}

	// -----------------------------------------------------------------------
	// 段 A: 注釈へ入れる道の切り分け。1 回目は AddViewportAnnotationObject が false を返した。
	probe.log("=== 段 A: 注釈へ入れられるか（3 通り）===");
	{
		MCObjectHandle r1 = gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 100.0, 100.0);
		probe.log(std::string("A1 矩形（作れば文書に入る）→ AddViewportAnnotationObject=") +
				  ProbeBoolStr(r1 != nil && gSDK->AddViewportAnnotationObject(vp, r1) != 0));

		MCObjectHandle l2 = gSDK->CreateCustomObject("Drawing Label2", WorldPt(0, 0), 0.0, false);
		probe.log(std::string("A2 ラベル bInsert=false → AddViewportAnnotationObject=") +
				  ProbeBoolStr(l2 != nil && gSDK->AddViewportAnnotationObject(vp, l2) != 0) +
				  " Link State=" +
				  (l2 != nil
					   ? ProbeStr(VWFC::VWObjects::VWParametricObj(l2).GetParamValue("Link State"))
					   : "—"));

		MCObjectHandle l3 = gSDK->CreateCustomObject("Drawing Label2", WorldPt(0, 0), 0.0, true);
		probe.log(std::string("A3 ラベル bInsert=true → AddViewportAnnotationObject=") +
				  ProbeBoolStr(l3 != nil && gSDK->AddViewportAnnotationObject(vp, l3) != 0) +
				  " Link State=" +
				  (l3 != nil
					   ? ProbeStr(VWFC::VWObjects::VWParametricObj(l3).GetParamValue("Link State"))
					   : "—"));
		probe.log(std::string("A 後の注釈群=") +
				  (gSDK->GetViewportGroup(vp, kViewportGroupAnnotation) != nil ? "取れる" : "nil"));
	}

	// 以降「注釈へ入れる」はこの 1 本にまとめる（戻り値も Link State も毎回出す）。
	auto putInAnnotation = [&probe, vp](const std::string& tag, MCObjectHandle h)
	{
		const Boolean ok = gSDK->AddViewportAnnotationObject(vp, h);
		gSDK->ResetObject(h);
		probe.log(tag + ": 注釈へ AddViewportAnnotationObject=" + ProbeBoolStr(ok != 0) +
				  " Link State=" +
				  ProbeStr(VWFC::VWObjects::VWParametricObj(h).GetParamValue("Link State")));
		return ok != 0;
	};

	// -----------------------------------------------------------------------
	// 段 B: 物差し——紙で 6pt になると分かっている寸法を同じ注釈へ 1 本置く。
	probe.log("=== 段 B: 物差しの寸法（紙で 6pt）===");
	double worldPerPt = 0.0;
	{
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(0, 3000), WorldPt(5000, 3000), 0,
														 0, Vector2(0, 0), 0);
		if (dim == nil)
		{
			probe.log("段 B: CreateLinearDimension が nil を返した（物差しは無し）");
		}
		else
		{
			TVariableBlock showVar;
			showVar = true;
			gSDK->SetObjectVariable(dim, ovDimShowValue, showVar);
			TVariableBlock fontVar;
			fontVar = static_cast<Real64>(kProbeRulerPt * kProbePtToMm * kProbeVpScale);
			gSDK->SetObjectVariable(dim, ovDimFontSize, fontVar);
			gSDK->ResetObject(dim);
			putInAnnotation("段 B 寸法", dim);
			gSDK->ResetObject(dim);
			std::vector<double> sizes;
			ProbeCollectTextSizes(dim, 0, sizes);
			probe.log("段 B 書いた ovDimFontSize=" +
					  ProbeNum(kProbeRulerPt * kProbePtToMm * kProbeVpScale) +
					  " / 寸法の中のテキスト " + ProbeTextSummary(dim));
			if (!sizes.empty() && sizes[0] > 0.0)
			{
				worldPerPt = sizes[0] / kProbeRulerPt;
				probe.log("段 B ★ 物差し: 紙の 1pt ＝ 世界座標 " + ProbeNum(worldPerPt));
			}
			else
			{
				probe.log("段 B: 寸法の中にテキストが見つからず、物差しを作れなかった");
			}
		}
		if (worldPerPt <= 0.0)
		{
			// 計算値で代用する（Findings「Dimensions」: 紙の pt × 25.4/72 × 縮尺）。
			worldPerPt = kProbePtToMm * kProbeVpScale;
			probe.log("段 B: 計算値で代用する 紙の 1pt ＝ 世界座標 " + ProbeNum(worldPerPt));
		}
	}

	// -----------------------------------------------------------------------
	// 段 C: スタイルを 1 本作る（Findings「Parametric Objects」の手順）。
	probe.log("=== 段 C: 調査用のスタイルを作る ===");
	RefNumber toolRefOriginal = 0;
	gSDK->GetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	probe.log("ツールの元のスタイル ref=" + ProbeInt(toolRefOriginal));

	TXString styleName("調査用_図面ラベルスタイル");
	MCObjectHandle hSymDef = gSDK->CreateSymbolDefinition(styleName);
	RefNumber styleRef = 0;
	if (hSymDef == nil)
	{
		probe.log(
			"段 C: CreateSymbolDefinition が nil（同名が既に在る？）→ 元のツールのスタイルで進む");
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
		gSDK->ResetObject(hSymDef); // ★ サブタイプを書く**前に**通す
		gSDK->SetSymbolDefSubType(
			hSymDef, static_cast<Sint32>(VWFC::VWObjects::VWParametricObj::GetInternalID(seed)));
		gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);
		styleRef = gSDK->GetObjectInternalIndex(hSymDef);
		probe.log("段 C subType の読み戻し=" + ProbeInt(gSDK->GetSymbolDefSubType(hSymDef)) +
				  " IsPluginStyle=" + ProbeBoolStr(gSDK->IsPluginStyle(hSymDef)) +
				  " ref=" + ProbeInt(styleRef));
	}
	if (styleRef == 0)
	{
		probe.fail("段 C: 当てられるスタイルが用意できなかった");
		return;
	}

	// -----------------------------------------------------------------------
	// 段 D: **既定のレイアウトはどこから来るか。** スタイル有り／無しで作った直後を見比べる。
	probe.log("=== 段 D: 既定のレイアウトの出どころ ===");
	gSDK->SetPluginStyleForTool("Drawing Label2", styleRef);
	MCObjectHandle dStyled = ProbeNewLabel(probe, "D1");
	if (dStyled != nil)
		probe.log("D1 スタイル有りで作った直後: " + ProbeStyleInfo(dStyled) + " / レイアウト " +
				  ProbeShapeSummary(gSDK->GetCustomObjectProfileGroup(dStyled)) + " / " +
				  ProbeTextSummary(gSDK->GetCustomObjectProfileGroup(dStyled)));

	gSDK->SetPluginStyleForTool("Drawing Label2", 0);
	MCObjectHandle dPlain = ProbeNewLabel(probe, "D2");
	if (dPlain != nil)
	{
		probe.log("D2 スタイル無しで作った直後: " + ProbeStyleInfo(dPlain) + " / レイアウト " +
				  ProbeShapeSummary(gSDK->GetCustomObjectProfileGroup(dPlain)) + " / " +
				  ProbeTextSummary(gSDK->GetCustomObjectProfileGroup(dPlain)));
		gSDK->ResetObject(dPlain);
		probe.log("D2 ResetObject の後: レイアウト " +
				  ProbeShapeSummary(gSDK->GetCustomObjectProfileGroup(dPlain)) + " / 描いた図形 " +
				  ProbeShapeSummary(dPlain) + " / " + ProbeBounds(dPlain));
	}

	// D3: スタイル有りで作った個体を、レイアウトに触らずに外したらレイアウトはどうなるか。
	gSDK->SetPluginStyleForTool("Drawing Label2", styleRef);
	MCObjectHandle dStrip = ProbeNewLabel(probe, "D3");
	if (dStrip != nil)
	{
		probe.log("D3 外す前: レイアウト " +
				  ProbeShapeSummary(gSDK->GetCustomObjectProfileGroup(dStrip)));
		gSDK->SetPluginObjectStyle(dStrip, 0);
		gSDK->ResetObject(dStrip);
		probe.log("D3 ★ 組み直さずに外した後: " + ProbeStyleInfo(dStrip) + " / レイアウト " +
				  ProbeShapeSummary(gSDK->GetCustomObjectProfileGroup(dStrip)) + " / " +
				  ProbeTextSummary(gSDK->GetCustomObjectProfileGroup(dStrip)) + " / " +
				  ProbeBounds(dStrip));
	}

	// -----------------------------------------------------------------------
	// 段 E: レイアウトの中の文字の大きさ。
	//
	//	**作るときはスタイル有り**（既定レイアウトが要る）→ 組み直す → 外す、の順で揃える。
	probe.log("=== 段 E: レイアウトの中の文字の大きさ ===");
	InternalIndex tsRef = 0;
	{
		MCObjectHandle hTS = gSDK->CreateTextStyleResource("調査用(10pt)");
		if (hTS == nil)
		{
			probe.log("段 E: CreateTextStyleResource が nil（文字スタイルの枝は測れない）");
		}
		else
		{
			TVariableBlock sizeVar;
			sizeVar = static_cast<Real64>(kProbeTenPtInch);
			gSDK->SetObjectVariable(hTS, ovTextStyleSize, sizeVar);
			tsRef = gSDK->GetObjectInternalIndex(hTS);
			TVariableBlock back;
			Real64 inches = 0.0;
			gSDK->GetObjectVariable(hTS, ovTextStyleSize, back);
			back.GetReal64(inches);
			probe.log("段 E 文字スタイル ref=" + ProbeInt(tsRef) +
					  " ovTextStyleSize（インチ）=" + ProbeNum(inches));
		}
	}
	probe.log("段 E 与える値: 10pt を素直に mm へ=" + ProbeNum(kProbeTenPtMm) +
			  " / 縮尺を掛けた値=" + ProbeNum(kProbeTenPtMm * kProbeVpScale));

	// 置き場: 0=ビューポートの注釈（1/50） 1=シートレイヤ直下（1:1） 2=デザインレイヤ（1/50）
	struct ProbeCase
	{
		const char* tag;
		WorldCoord size;
		bool useTextStyle;
		bool afterPlacing; // 組み直しを置いた後にやるか
		int place;
		const char* scaleFactor; // nullptr なら触らない
		const char* worldBased;	 // nullptr なら触らない
	};
	const ProbeCase cases[] = {
		{"E1 SetTextSize(10pt mm)・注釈・入れる前", kProbeTenPtMm, false, false, 0, nullptr,
		 nullptr},
		{"E2 SetTextSize(10pt mm)・注釈・入れた後", kProbeTenPtMm, false, true, 0, nullptr,
		 nullptr},
		{"E3 SetTextSize(10pt mm × 縮尺)・注釈・入れる前", kProbeTenPtMm * kProbeVpScale, false,
		 false, 0, nullptr, nullptr},
		{"E4 文字スタイル(10pt)・注釈・入れる前", 0, true, false, 0, nullptr, nullptr},
		{"E5 文字スタイル(10pt)・注釈・入れた後", 0, true, true, 0, nullptr, nullptr},
		{"E6 対照 大きさに触らない・注釈", 0, false, false, 0, nullptr, nullptr},
		{"E7 SetTextSize(10pt mm)・シートレイヤ直下(1:1)", kProbeTenPtMm, false, false, 1, nullptr,
		 nullptr},
		{"E8 SetTextSize(10pt mm)・デザインレイヤ(1/50)", kProbeTenPtMm, false, false, 2, nullptr,
		 nullptr},
		{"E9 SetTextSize(10pt mm) ＋ ScaleFactor=2・注釈", kProbeTenPtMm, false, false, 0, "2",
		 nullptr},
		{"E10 SetTextSize(10pt mm) ＋ World-based=True・注釈", kProbeTenPtMm, false, false, 0,
		 nullptr, "True"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const ProbeCase& c = cases[i];
		if (c.useTextStyle && tsRef == 0)
			continue;
		probe.log(std::string("--- ") + c.tag + " ---");
		gSDK->SetPluginStyleForTool("Drawing Label2",
									styleRef); // 既定レイアウトが要るので有りで作る
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
			putInAnnotation(c.tag, h);
		else if (c.place == 1)
			gSDK->AddObjectToContainer(h, sheet);
		else
			gSDK->AddObjectToContainer(h, design);
		gSDK->ResetObject(h);

		if (c.afterPlacing)
			ProbeRebuildLayout(probe, c.tag, h, c.size, useTS);

		// **ここで外す。** 組み直したレイアウトは外しても保たれる（1 回目の走行で確定）。
		gSDK->SetPluginObjectStyle(h, 0);
		gSDK->ResetObject(h);
		// 置き場が 1:1 のときは、紙の pt は世界座標そのもの（÷1）で読む。
		ProbeMeasure(probe, c.tag, h, c.place == 1 ? kProbePtToMm : worldPerPt);
	}

	gSDK->UpdateViewport(vp);

	// -----------------------------------------------------------------------
	probe.log("=== 後片付け ===");
	probe.log("SetPluginStyleForTool(元の値 " + ProbeInt(toolRefOriginal) +
			  ")=" + ProbeBoolStr(gSDK->SetPluginStyleForTool("Drawing Label2", toolRefOriginal)));
	{
		RefNumber back = 0;
		gSDK->GetPluginStyleForTool("Drawing Label2", back);
		probe.log("読み戻し ref=" + ProbeInt(back));
	}
	probe.log("おわり");
}
