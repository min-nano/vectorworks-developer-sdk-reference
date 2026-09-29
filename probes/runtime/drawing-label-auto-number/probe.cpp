//
//	probes/runtime/drawing-label-auto-number/probe.cpp
//
//	[issue #179] 図面ラベル（`Drawing Label2`）の図番（`Drawing`）の自動採番の規則を決める。
//
//	## 1 巡目（ビルド 805aaf940f89）で割れたこと
//
//	作った瞬間の図番は「**そのシートレイヤで、いま図番として使われている正整数のうち、
//	使われていない最小のもの**」である。1 巡目の A〜H に加えて、**#177 の 11 本
//	（1,2,1,3,4,5,4,6,4,7,4）が 1 本の例外も無くこの規則で説明できた**ので、規則そのものは
//	確定してよい。
//
//	  * シートレイヤ直下に置きっぱなしなら 1,2,3,… と素直に増える（A=1..6 / H=1..8）。
//	  * 消して空いた番号は埋める（F: 2 を消したら次が 2）。max+1 ではない。
//	  * `bInsert=false` でどこへも入れないと、その本は誰からも見えないので**常に 1**（B）。
//	  * `varUseAutoDrawCoord`（544）は無関係（H: 切っても 1,2,3,4 のまま）。
//
//	戻る回がある理由も同じ規則で説明が付く——**ビューポートの注釈へ 2 本目を入れると、
//	その注釈の先頭のラベルが「使われている番号」から外れる**（番号が 1 つ空く）ため、
//	次の 1 本がその空きへ落ちる。最後に読み直すと、先頭のラベルは**その注釈へ最後に
//	入れたラベルと同じ値**になっている（C: 1 本目が 5 ＝ 6 本目と同じ／E: VP2 は 3 が 2 本・
//	VP3 は 4 が 2 本）。
//
//	## この 2 巡目が決めること
//
//	1. **先頭のラベルは、番号を手放してから何を持っているのか**（P）。1 巡目は「作る →
//	   入れる → reset」の 3 点しか読んでいないので、**他の本を足したときに先頭が何に
//	   変わるか**を見ていない。1 本足すたびに**全員を読み直す**。
//	2. **自分で書いた図番は、あとから同じ注釈へ 2 本目・3 本目を入れても残るか**（Q・R）。
//	   ここが実用の要——「SDK 任せでは重複するから自分で書け」と言えるかどうかが懸かる。
//	   1 巡目の G は**その注釈にラベルが 1 本しか無い**状態しか見ていない。文字列
//	   （`A-01`）と数字（`7`）で分ける。
//	3. **全部に自分で書けば安全か**（S）。実際に使う手順をそのままなぞる。
//
//	`Link State`（リンク状況）も併せて読む。先頭だけが書き換わるなら、そこに差が出る。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const double kProbeVpScale = 50.0;

	std::string ProbeInt(Sint32 v)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%ld", static_cast<long>(v));
		return buf;
	}

	std::string ProbeStr(const TXString& s)
	{
		const char* p = static_cast<const char*>(s);
		return p != nullptr ? std::string(p) : std::string();
	}

	int gProbeSpot = 0;

	WorldPt ProbeNextSpot()
	{
		return WorldPt(0, -2000.0 * (gProbeSpot++));
	}

	std::string ProbeDrawingOf(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		return ProbeStr(VWFC::VWObjects::VWParametricObj(h).GetParamString("Drawing"));
	}

	// 「図番＋リンク状況」を 1 本ぶん。手放した先頭が何を持っているかは、この 2 つで見る。
	std::string ProbeStateOf(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		VWFC::VWObjects::VWParametricObj obj(h);
		return "図番=「" + ProbeStr(obj.GetParamString("Drawing")) +
			   "」/リンク=" + ProbeInt(obj.GetParamLong("Link State"));
	}

	// そのフェーズで作った全部を、入れた順に 1 行で並べ直す。
	void ProbeDumpAll(vwprobe::Report& probe, const std::string& tag,
					  const std::vector<MCObjectHandle>& labels)
	{
		std::string line = tag + ": 【全員】";
		for (size_t i = 0; i < labels.size(); ++i)
			line += " " + ProbeInt(static_cast<Sint32>(i) + 1) + ":" + ProbeStateOf(labels[i]);
		probe.log(line);
	}

	// ラベルを 1 本作って、vp の注釈へ入れる（vp が nil なら入れない）。
	MCObjectHandle ProbeMakeLabel(vwprobe::Report& probe, const std::string& tag, MCObjectHandle vp)
	{
		MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, true);
		if (h == nil)
		{
			probe.fail(tag + ": CreateCustomObject が nil");
			return nil;
		}
		std::string line = tag + ": 作った直後 " + ProbeStateOf(h);
		if (vp != nil)
		{
			const bool added = (gSDK->AddViewportAnnotationObject(vp, h) != 0);
			line +=
				" → 注釈へ入れた=" + std::string(added ? "true" : "false") + " " + ProbeStateOf(h);
			gSDK->ResetObject(h);
			line += " → ResetObject 後 " + ProbeStateOf(h);
		}
		probe.log(line);
		return h;
	}

	void ProbeWrite(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h,
					const char* value)
	{
		if (h == nil)
			return;
		VWFC::VWObjects::VWParametricObj(h).SetParamValue("Drawing", value);
		gSDK->ResetObject(h);
		probe.log(tag + ": 図番へ「" + std::string(value) + "」を書いて ResetObject → " +
				  ProbeStateOf(h));
	}

	MCObjectHandle ProbeMakeSheet(vwprobe::Report& probe, const char* name)
	{
		MCObjectHandle sheet = gSDK->CreateLayer(name, kLayerSheet);
		if (sheet == nil)
		{
			probe.fail(std::string("CreateLayer(シート ") + name + ") が nil");
			return nil;
		}
		gSDK->SetCurrentLayer(sheet);
		return sheet;
	}

	MCObjectHandle ProbeMakeViewport(vwprobe::Report& probe, const std::string& tag,
									 MCObjectHandle sheet, MCObjectHandle design)
	{
		MCObjectHandle vp = gSDK->CreateViewport(sheet);
		if (vp == nil)
		{
			probe.fail(tag + ": CreateViewport が nil");
			return nil;
		}
		gSDK->SetViewportLayerVisibility(vp, design, VWFC::VWObjects::kLayerVisibilityNormal);
		TVariableBlock scaleVar;
		scaleVar = static_cast<Real64>(kProbeVpScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
		gSDK->UpdateViewport(vp);
		return vp;
	}

	// 「1 本足すたびに全員を読み直す」を count 本ぶん繰り返す。
	void ProbeAddAndDump(vwprobe::Report& probe, const std::string& phase, MCObjectHandle vp,
						 std::vector<MCObjectHandle>& labels, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			const std::string tag =
				phase + ProbeInt(static_cast<Sint32>(labels.size()) + 1) + " 本目";
			MCObjectHandle h = ProbeMakeLabel(probe, tag, vp);
			if (h == nil)
				return;
			labels.push_back(h);
			ProbeDumpAll(probe, tag + "の直後", labels);
		}
	}
} // namespace

VW_PROBE("drawing-label-auto-number", "図面ラベルの図番の自動採番（2 巡目）",
		 "先頭のラベルが番号を手放したあと何を持つか、自分で書いた図番は守られるか")
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

	// -----------------------------------------------------------------------
	probe.log("=== P: 1 枚の注釈へ 6 本。1 本足すたびに全員を読み直す ===");
	probe.log("P: 見たいのは「先頭が番号を手放す瞬間」と、手放してから何を持っているか");
	MCObjectHandle sheetP = ProbeMakeSheet(probe, "調査シート P");
	if (sheetP == nil)
		return;
	MCObjectHandle vpP = ProbeMakeViewport(probe, "P", sheetP, design);
	std::vector<MCObjectHandle> labelsP;
	ProbeAddAndDump(probe, "P ", vpP, labelsP, 6);
	if (vpP != nil)
	{
		gSDK->UpdateViewport(vpP);
		ProbeDumpAll(probe, "P UpdateViewport 後", labelsP);
	}

	// -----------------------------------------------------------------------
	probe.log("=== Q: 先頭へ文字列「A-01」を書いてから、2 本目・3 本目を足す ===");
	probe.log("Q: 書いた値が守られるかどうかで「自分で書けば確実」と言えるかが決まる");
	MCObjectHandle sheetQ = ProbeMakeSheet(probe, "調査シート Q");
	if (sheetQ == nil)
		return;
	MCObjectHandle vpQ = ProbeMakeViewport(probe, "Q", sheetQ, design);
	std::vector<MCObjectHandle> labelsQ;
	ProbeAddAndDump(probe, "Q ", vpQ, labelsQ, 1);
	if (!labelsQ.empty())
		ProbeWrite(probe, "Q1", labelsQ[0], "A-01");
	ProbeAddAndDump(probe, "Q ", vpQ, labelsQ, 2);
	if (vpQ != nil)
	{
		gSDK->UpdateViewport(vpQ);
		ProbeDumpAll(probe, "Q UpdateViewport 後", labelsQ);
	}

	// -----------------------------------------------------------------------
	probe.log("=== R: 先頭へ数字「7」を書いてから、2 本目・3 本目を足す ===");
	probe.log("R: 数字なら乗っ取られる／文字列なら守られる、という分かれ方をするかを見る");
	MCObjectHandle sheetR = ProbeMakeSheet(probe, "調査シート R");
	if (sheetR == nil)
		return;
	MCObjectHandle vpR = ProbeMakeViewport(probe, "R", sheetR, design);
	std::vector<MCObjectHandle> labelsR;
	ProbeAddAndDump(probe, "R ", vpR, labelsR, 1);
	if (!labelsR.empty())
		ProbeWrite(probe, "R1", labelsR[0], "7");
	ProbeAddAndDump(probe, "R ", vpR, labelsR, 2);
	if (vpR != nil)
	{
		gSDK->UpdateViewport(vpR);
		ProbeDumpAll(probe, "R UpdateViewport 後", labelsR);
	}

	// -----------------------------------------------------------------------
	probe.log("=== S: 実用の手順——3 本すべてに自分で書く ===");
	probe.log("S: 入れてから書く。最後に引き直して、3 本とも書いたままかを見る");
	MCObjectHandle sheetS = ProbeMakeSheet(probe, "調査シート S");
	if (sheetS == nil)
		return;
	MCObjectHandle vpS = ProbeMakeViewport(probe, "S", sheetS, design);
	std::vector<MCObjectHandle> labelsS;
	ProbeAddAndDump(probe, "S ", vpS, labelsS, 3);
	const char* kProbeSheetSValues[] = {"S-1", "S-2", "S-3"};
	for (size_t i = 0; i < labelsS.size() && i < 3; ++i)
		ProbeWrite(probe, "S" + ProbeInt(static_cast<Sint32>(i) + 1), labelsS[i],
				   kProbeSheetSValues[i]);
	ProbeDumpAll(probe, "S 3 本とも書いた", labelsS);
	if (vpS != nil)
	{
		gSDK->UpdateViewport(vpS);
		ProbeDumpAll(probe, "S UpdateViewport 後", labelsS);
	}
	probe.log("=== T: そのうえで、もう 1 本足したら何番になるか ===");
	ProbeAddAndDump(probe, "S ", vpS, labelsS, 1);
	if (vpS != nil)
	{
		gSDK->UpdateViewport(vpS);
		ProbeDumpAll(probe, "S 最後に引き直した", labelsS);
	}

	probe.log("おわり");
}
