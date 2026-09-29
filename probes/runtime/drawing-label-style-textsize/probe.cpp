//
//	probes/runtime/drawing-label-style-textsize/probe.cpp
//
//	[issue #175] 図面ラベル（Drawing Label2）について 3 つを実測する。
//
//	1. CreateCustomObject はツールに設定されたスタイルを自動で当てるか
//	   （作った直後／注釈へ入れて ResetObject した後）。
//	2. 当たったスタイルをインスタンスから外す道はあるか
//	   （SetPluginObjectStyle(h, 0) ／ ツールのスタイルを 0 にしてから作る）。
//	   外した後に組み直したレイアウトが保たれるか。
//	3. ラベルレイアウトの中のテキストの大きさは、どの単位・どの縮尺で解かれるか
//	   （SetTextSize ／ 文字スタイル × 注釈へ入れる前／後 × ScaleFactor / World-based）。
//
//	文書にスタイルが在るかに結果を左右されないよう、**このプローブは自分で
//	図面ラベルのスタイルを 1 本作って**ツールへ設定してから測る
//	（Findings「Parametric Objects」の「スタイルは SDK だけで作れる」の手順）。
//	ツールのスタイルは走り終わりに元の値へ戻す。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	// Objs.TDType.h の型番号（Findings「Drawing Labels」）。
	const short kProbeTextNodeType = 10;
	const short kProbeLineNodeType = 2;

	// 1/50 のビューポートで測る。紙で 10pt を狙う値を 2 通り用意して見比べる。
	const double kProbeVpScale = 50.0;
	const double kProbePtToMm = 25.4 / 72.0;		  // 1pt = 0.352778mm
	const double kProbeTenPtMm = 10.0 * kProbePtToMm; // 3.52778mm（＝紙で 10pt を素直に mm へ）
	const double kProbeTenPtInch = 10.0 / 72.0; // 0.138889 インチ（文字スタイルの単位）

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

	// スタイルの当たり具合。**戻り値ではなく ref を見る**（Findings「Parametric Objects」）。
	std::string ProbeStyleInfo(MCObjectHandle h)
	{
		RefNumber ref = 0;
		const Boolean ret = gSDK->GetPluginObjectStyle(h, ref);
		std::string s = std::string("GetPluginObjectStyle=") + (ret ? "true" : "false") +
						" ref=" + ProbeInt(ref);
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

	std::string ProbeTextInfo(MCObjectHandle t)
	{
		WorldCoord size = 0;
		gSDK->GetTextSize(t, 0, size);
		return std::string("GetTextSize(0)=") + ProbeNum(size) +
			   " 文字数=" + ProbeInt(gSDK->GetTextLength(t)) +
			   " GetTextStyleRef=" + ProbeInt(gSDK->GetTextStyleRef(t));
	}

	std::string ProbeBounds(MCObjectHandle h)
	{
		WorldRect r;
		if (!gSDK->GetObjectBounds(h, r))
			return "GetObjectBounds=false";
		return std::string("外接 幅=") + ProbeNum(std::fabs(r.right - r.left)) +
			   " 高さ=" + ProbeNum(std::fabs(r.top - r.bottom));
	}

	// 容れ物の中のテキストを全部測って並べる。
	void ProbeDumpTexts(vwprobe::Report& probe, const std::string& tag, MCObjectHandle container)
	{
		if (container == nil)
		{
			probe.log(tag + ": 容れ物が nil");
			return;
		}
		int i = 0;
		int texts = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil;
			 m = gSDK->NextObject(m), ++i)
		{
			const short type = gSDK->GetObjectTypeN(m);
			std::string line = tag + "[" + ProbeInt(i) + "] 型=" + ProbeInt(type);
			if (type == kProbeTextNodeType)
			{
				line += " " + ProbeTextInfo(m);
				++texts;
			}
			probe.log(line);
		}
		if (texts == 0)
			probe.log(tag + ": テキストは 1 つも無かった");
	}

	// ラベル 1 本の素性をまとめて測る。
	void ProbeMeasure(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		if (h == nil)
		{
			probe.log(tag + ": ハンドルが nil");
			return;
		}
		VWFC::VWObjects::VWParametricObj pio(h);
		probe.log(tag + ": " + ProbeStyleInfo(h));
		probe.log(tag + ": " + ProbeBounds(h) +
				  " ScaleFactor=" + ProbeStr(pio.GetParamValue("ScaleFactor")) +
				  " World-based=" + ProbeStr(pio.GetParamValue("World-based")) +
				  " Link State=" + ProbeStr(pio.GetParamValue("Link State")));
		ProbeDumpTexts(probe, tag + " レイアウト", gSDK->GetCustomObjectProfileGroup(h));
		ProbeDumpTexts(probe, tag + " 描いた図形", h);
	}

	// レイアウトを「タイトルのテキスト＋下線」だけに組み直す（Findings「Drawing Labels」の手順）。
	// sizeWorld > 0 なら SetTextSize を、tsRef != 0 なら SetTextStyleRef を、複製したテキストへ当てる。
	bool ProbeRebuildLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h,
							WorldCoord sizeWorld, InternalIndex tsRef)
	{
		MCObjectHandle hOld = gSDK->GetCustomObjectProfileGroup(h);
		if (hOld == nil)
		{
			probe.log(tag + ": レイアウト（プロファイルグループ）が nil で組み直せない");
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
				probe.log(tag + ": 当てた直後のレイアウトのテキスト " + ProbeTextInfo(dup));
			}
			else
			{
				tookLine = true;
			}
		}
		const Boolean ok = gSDK->SetCustomObjectProfileGroup(h, hGroup);
		probe.log(tag + ": SetCustomObjectProfileGroup=" + std::string(ok ? "true" : "false"));
		gSDK->ResetObject(h);
		return ok != 0;
	}

	// 作ったラベルを縦にずらして置く。**測るのは数値だけ**だが、重ねて置くと図面を
	// 見たときに何が何だか分からなくなるので、1 本ごとに下へずらす。
	int gProbeLabelCount = 0;

	WorldPt ProbeNextSpot()
	{
		return WorldPt(0, -1000.0 * (gProbeLabelCount++));
	}

	MCObjectHandle ProbeNewLabel(vwprobe::Report& probe, const std::string& tag)
	{
		MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
		if (h == nil)
			probe.log(tag + ": CreateCustomObject(\"Drawing Label2\") が nil を返した");
		return h;
	}
} // namespace

VW_PROBE("drawing-label-style-textsize", "図面ラベルのスタイルと文字の大きさ",
		 "スタイルの自動適用と外し方、レイアウトの文字の大きさを実測する")
{
	// 作る前に 1 度。これが無いと最初の 1 個で「オブジェクトの設定」ダイアログが出る。
	gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);

	// -----------------------------------------------------------------------
	// 段取り: 1/50 のデザインレイヤ → シートレイヤ（ここでアクティブになる）→ 1/50 のビューポート
	probe.log("=== 段取り ===");
	MCObjectHandle design = gSDK->CreateLayer("調査用デザイン", kLayerDesign);
	if (design == nil)
	{
		probe.fail("CreateLayer(デザイン) が nil を返した");
		return;
	}
	gSDK->SetLayerScaleN(design, kProbeVpScale);

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
			probe.log(std::string("ビューポートの縮尺 ovViewportScale=") + ProbeNum(s));
		else
			probe.log("ビューポートの縮尺を読み戻せなかった");
	}

	// -----------------------------------------------------------------------
	// 段 1: ツールにいま設定されているスタイル（元の値。最後に戻す）
	probe.log("=== 段 1: ツールのスタイル（元の値）===");
	RefNumber toolRefOriginal = 0;
	const bool gotTool = gSDK->GetPluginStyleForTool("Drawing Label2", toolRefOriginal);
	{
		std::string line = std::string("GetPluginStyleForTool 戻り値=") +
						   (gotTool ? "true" : "false") + " ref=" + ProbeInt(toolRefOriginal);
		if (toolRefOriginal != 0)
		{
			TXString nm;
			gSDK->InternalIndexToNameN(static_cast<InternalIndex>(toolRefOriginal), nm);
			line += " 名前=" + ProbeStr(nm);
		}
		probe.log(line);
	}

	// -----------------------------------------------------------------------
	// 段 2: そのまま作ったラベル（文書のツール設定のまま）
	probe.log("=== 段 2: ツール設定に触らず作る ===");
	MCObjectHandle labelA = ProbeNewLabel(probe, "段 2");
	if (labelA == nil)
	{
		probe.fail("段 2: 図面ラベルを作れなかった");
		return;
	}
	probe.log(std::string("段 2 作った直後: ") + ProbeStyleInfo(labelA));
	probe.log(std::string("段 2 AddViewportAnnotationObject=") +
			  (gSDK->AddViewportAnnotationObject(vp, labelA) ? "true" : "false"));
	gSDK->ResetObject(labelA);
	probe.log(std::string("段 2 注釈へ入れて ResetObject した後: ") + ProbeStyleInfo(labelA));

	// -----------------------------------------------------------------------
	// 段 3: 図面ラベルのスタイルを自分で 1 本作り、ツールへ設定して作り直す
	probe.log("=== 段 3: スタイルを作ってツールへ設定し、作った直後を見る ===");
	TXString styleName("調査用_図面ラベルスタイル");
	MCObjectHandle hSymDef = gSDK->CreateSymbolDefinition(styleName);
	if (hSymDef == nil)
	{
		probe.fail("段 3: CreateSymbolDefinition が nil を返した（同名が既に在る？）");
		return;
	}
	MCObjectHandle seed = ProbeNewLabel(probe, "段 3 種");
	if (seed == nil)
	{
		probe.fail("段 3: 種の図面ラベルを作れなかった");
		return;
	}
	probe.log(std::string("段 3 AddObjectToContainer=") +
			  (gSDK->AddObjectToContainer(seed, hSymDef) ? "true" : "false"));
	gSDK->ResetObject(hSymDef); // ★ サブタイプを書く**前に**通す
	const TInternalID seedID = VWFC::VWObjects::VWParametricObj::GetInternalID(seed);
	gSDK->SetSymbolDefSubType(hSymDef, static_cast<Sint32>(seedID));
	gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);
	const RefNumber styleRef = gSDK->GetObjectInternalIndex(hSymDef);
	probe.log(std::string("段 3 種の内部 ID=") + ProbeInt(static_cast<Sint32>(seedID)) +
			  " SetSymbolDefSubType の読み戻し=" + ProbeInt(gSDK->GetSymbolDefSubType(hSymDef)) +
			  " IsPluginStyle=" + (gSDK->IsPluginStyle(hSymDef) ? "true" : "false") +
			  " ref=" + ProbeInt(styleRef));
	if (styleRef == 0 || !gSDK->IsPluginStyle(hSymDef))
	{
		probe.fail("段 3: 図面ラベルのスタイルを用意できなかった（以降の段は当てにならない）");
	}

	probe.log(std::string("段 3 SetPluginStyleForTool(styleRef)=") +
			  (gSDK->SetPluginStyleForTool("Drawing Label2", styleRef) ? "true" : "false"));
	{
		RefNumber back = 0;
		const bool ret = gSDK->GetPluginStyleForTool("Drawing Label2", back);
		probe.log(std::string("段 3 読み戻し 戻り値=") + (ret ? "true" : "false") +
				  " ref=" + ProbeInt(back));
	}

	MCObjectHandle labelB = ProbeNewLabel(probe, "段 3");
	if (labelB == nil)
	{
		probe.fail("段 3: 図面ラベルを作れなかった");
		return;
	}
	probe.log(std::string("段 3 ★ ツールにスタイルを設定して作った直後: ") +
			  ProbeStyleInfo(labelB));
	gSDK->AddViewportAnnotationObject(vp, labelB);
	gSDK->ResetObject(labelB);
	probe.log(std::string("段 3 ★ 注釈へ入れて ResetObject した後: ") + ProbeStyleInfo(labelB));

	// -----------------------------------------------------------------------
	// 段 4: 外し方① SetPluginObjectStyle(h, 0)。レイアウトが保たれるかも見る。
	probe.log("=== 段 4: 外し方① SetPluginObjectStyle(h, 0) ===");
	ProbeRebuildLayout(probe, "段 4 組み直し", labelB, 0, 0);
	probe.log(std::string("段 4 組み直した直後: ") + ProbeStyleInfo(labelB));
	ProbeDumpTexts(probe, "段 4 組み直した直後 レイアウト",
				   gSDK->GetCustomObjectProfileGroup(labelB));
	probe.log(std::string("段 4 SetPluginObjectStyle(h, 0) の戻り値=") +
			  (gSDK->SetPluginObjectStyle(labelB, 0) ? "true" : "false"));
	probe.log(std::string("段 4 呼んだ直後: ") + ProbeStyleInfo(labelB));
	gSDK->ResetObject(labelB);
	probe.log(std::string("段 4 ResetObject の後: ") + ProbeStyleInfo(labelB));
	ProbeDumpTexts(probe, "段 4 外した後 レイアウト", gSDK->GetCustomObjectProfileGroup(labelB));
	ProbeDumpTexts(probe, "段 4 外した後 描いた図形", labelB);
	gSDK->UpdateViewport(vp);
	probe.log(std::string("段 4 UpdateViewport の後: ") + ProbeStyleInfo(labelB));

	// 対照: VWFC の VWParametricObj::SetStyle(0) は何もしない（ソース根拠の裏取り）。
	probe.log("=== 段 4b: 対照 VWParametricObj::SetStyle(0) ===");
	MCObjectHandle labelC = ProbeNewLabel(probe, "段 4b");
	if (labelC != nil)
	{
		gSDK->AddViewportAnnotationObject(vp, labelC);
		gSDK->ResetObject(labelC);
		probe.log(std::string("段 4b 呼ぶ前: ") + ProbeStyleInfo(labelC));
		const RefNumber zero = 0;
		VWFC::VWObjects::VWParametricObj(labelC).SetStyle(zero);
		probe.log(std::string("段 4b SetStyle(0) の後: ") + ProbeStyleInfo(labelC));
		gSDK->ResetObject(labelC);
		probe.log(std::string("段 4b ResetObject の後: ") + ProbeStyleInfo(labelC));
	}

	// -----------------------------------------------------------------------
	// 段 5: 外し方② ツールのスタイルを 0 にしてから作り、元へ戻す
	probe.log("=== 段 5: 外し方② ツールのスタイルを 0 にしてから作る ===");
	probe.log(std::string("段 5 SetPluginStyleForTool(0)=") +
			  (gSDK->SetPluginStyleForTool("Drawing Label2", 0) ? "true" : "false"));
	{
		RefNumber back = 0;
		gSDK->GetPluginStyleForTool("Drawing Label2", back);
		probe.log(std::string("段 5 読み戻し ref=") + ProbeInt(back));
	}
	MCObjectHandle labelD = ProbeNewLabel(probe, "段 5");
	if (labelD != nil)
	{
		probe.log(std::string("段 5 ★ ツールのスタイルが 0 のときに作った直後: ") +
				  ProbeStyleInfo(labelD));
		gSDK->AddViewportAnnotationObject(vp, labelD);
		gSDK->ResetObject(labelD);
		probe.log(std::string("段 5 注釈へ入れて ResetObject した後: ") + ProbeStyleInfo(labelD));
		gSDK->SetPluginStyleForTool("Drawing Label2", styleRef); // ツールのスタイルを戻す
		gSDK->ResetObject(labelD);
		probe.log(std::string("段 5 ★ ツールのスタイルを戻して ResetObject した後: ") +
				  ProbeStyleInfo(labelD));
	}

	// -----------------------------------------------------------------------
	// 段 6: レイアウトの中の文字の大きさ
	//
	//	ここから先は**スタイルの影響を切るため、ツールのスタイルを 0 にしたまま**測る。
	probe.log("=== 段 6: レイアウトの中の文字の大きさ ===");
	gSDK->SetPluginStyleForTool("Drawing Label2", 0);

	// 文字スタイル「調査用(10pt)」を 1 本作る。ovTextStyleSize の単位は**インチ**。
	InternalIndex tsRef = 0;
	{
		MCObjectHandle hTS = gSDK->CreateTextStyleResource("調査用(10pt)");
		if (hTS == nil)
		{
			probe.log(
				"段 6: CreateTextStyleResource が nil を返した（文字スタイルの枝は測れない）");
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
			probe.log(std::string("段 6 文字スタイル ref=") + ProbeInt(tsRef) +
					  " ovTextStyleSize（インチ）=" + ProbeNum(inches));
		}
	}

	probe.log(std::string("段 6 与える値: 10pt をそのまま mm へ=") + ProbeNum(kProbeTenPtMm) +
			  " / 縮尺を掛けた値=" + ProbeNum(kProbeTenPtMm * kProbeVpScale));

	// F0: 対照。文字の大きさに何も触らず、レイアウトだけ組み直す（注釈の中）。
	probe.log("--- F0 対照: 大きさに触らない（注釈の中・1/50） ---");
	MCObjectHandle f0 = ProbeNewLabel(probe, "F0");
	if (f0 != nil)
	{
		gSDK->AddViewportAnnotationObject(vp, f0);
		gSDK->ResetObject(f0);
		ProbeRebuildLayout(probe, "F0", f0, 0, 0);
		ProbeMeasure(probe, "F0", f0);
	}

	// F1: SetTextSize(3.52778mm) を**注釈へ入れる前**に当てる。
	probe.log("--- F1 SetTextSize(10pt 相当 mm)・注釈へ入れる前 ---");
	MCObjectHandle f1 = ProbeNewLabel(probe, "F1");
	if (f1 != nil)
	{
		ProbeRebuildLayout(probe, "F1", f1, kProbeTenPtMm, 0);
		probe.log("F1 組み直し済み・注釈へ入れる前の測り:");
		ProbeMeasure(probe, "F1 入れる前", f1);
		gSDK->AddViewportAnnotationObject(vp, f1);
		gSDK->ResetObject(f1);
		ProbeMeasure(probe, "F1 入れた後", f1);
	}

	// F2: 同じ値を**注釈へ入れた後**に当てる。
	probe.log("--- F2 SetTextSize(10pt 相当 mm)・注釈へ入れた後 ---");
	MCObjectHandle f2 = ProbeNewLabel(probe, "F2");
	if (f2 != nil)
	{
		gSDK->AddViewportAnnotationObject(vp, f2);
		gSDK->ResetObject(f2);
		ProbeRebuildLayout(probe, "F2", f2, kProbeTenPtMm, 0);
		ProbeMeasure(probe, "F2", f2);
	}

	// F3: 縮尺を掛けた値（10pt × 50）を**注釈へ入れる前**に当てる。
	probe.log("--- F3 SetTextSize(10pt × 縮尺)・注釈へ入れる前 ---");
	MCObjectHandle f3 = ProbeNewLabel(probe, "F3");
	if (f3 != nil)
	{
		ProbeRebuildLayout(probe, "F3", f3, kProbeTenPtMm * kProbeVpScale, 0);
		gSDK->AddViewportAnnotationObject(vp, f3);
		gSDK->ResetObject(f3);
		ProbeMeasure(probe, "F3", f3);
	}

	// F4 / F5: 文字スタイル（10pt）を注釈へ入れる前／後に当てる。
	if (tsRef != 0)
	{
		probe.log("--- F4 文字スタイル(10pt)・注釈へ入れる前 ---");
		MCObjectHandle f4 = ProbeNewLabel(probe, "F4");
		if (f4 != nil)
		{
			ProbeRebuildLayout(probe, "F4", f4, 0, tsRef);
			gSDK->AddViewportAnnotationObject(vp, f4);
			gSDK->ResetObject(f4);
			ProbeMeasure(probe, "F4", f4);
		}

		probe.log("--- F5 文字スタイル(10pt)・注釈へ入れた後 ---");
		MCObjectHandle f5 = ProbeNewLabel(probe, "F5");
		if (f5 != nil)
		{
			gSDK->AddViewportAnnotationObject(vp, f5);
			gSDK->ResetObject(f5);
			ProbeRebuildLayout(probe, "F5", f5, 0, tsRef);
			ProbeMeasure(probe, "F5", f5);
		}
	}

	// F6: 同じ値を**シートレイヤ直下**（1:1）へ置いて測る。縮尺の効き目を切り分ける対照。
	probe.log("--- F6 SetTextSize(10pt 相当 mm)・シートレイヤ直下（1:1） ---");
	MCObjectHandle f6 = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, true);
	if (f6 != nil)
	{
		ProbeRebuildLayout(probe, "F6", f6, kProbeTenPtMm, 0);
		ProbeMeasure(probe, "F6", f6);
	}
	else
	{
		probe.log("F6: CreateCustomObject が nil を返した");
	}

	// F7: ScaleFactor（記号の倍率）を 2 にしたとき、レイアウトの文字も倍になるか。
	probe.log("--- F7 SetTextSize(10pt 相当 mm) ＋ ScaleFactor=2（注釈の中） ---");
	MCObjectHandle f7 = ProbeNewLabel(probe, "F7");
	if (f7 != nil)
	{
		VWFC::VWObjects::VWParametricObj(f7).SetParamValue("ScaleFactor", "2");
		ProbeRebuildLayout(probe, "F7", f7, kProbeTenPtMm, 0);
		gSDK->AddViewportAnnotationObject(vp, f7);
		gSDK->ResetObject(f7);
		ProbeMeasure(probe, "F7", f7);
	}

	// F8: World-based を True にしたとき。
	probe.log("--- F8 SetTextSize(10pt 相当 mm) ＋ World-based=True（注釈の中） ---");
	MCObjectHandle f8 = ProbeNewLabel(probe, "F8");
	if (f8 != nil)
	{
		VWFC::VWObjects::VWParametricObj(f8).SetParamValue("World-based", "True");
		ProbeRebuildLayout(probe, "F8", f8, kProbeTenPtMm, 0);
		gSDK->AddViewportAnnotationObject(vp, f8);
		gSDK->ResetObject(f8);
		ProbeMeasure(probe, "F8", f8);
	}

	gSDK->UpdateViewport(vp);

	// -----------------------------------------------------------------------
	// 後片付け: ツールのスタイルを元の値へ戻す。
	probe.log("=== 後片付け ===");
	probe.log(std::string("SetPluginStyleForTool(元の値 ") + ProbeInt(toolRefOriginal) + ")=" +
			  (gSDK->SetPluginStyleForTool("Drawing Label2", toolRefOriginal) ? "true" : "false"));
	{
		RefNumber back = 0;
		gSDK->GetPluginStyleForTool("Drawing Label2", back);
		probe.log(std::string("読み戻し ref=") + ProbeInt(back));
	}
	probe.log("おわり");
}
