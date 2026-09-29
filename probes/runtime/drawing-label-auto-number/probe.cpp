//
//	probes/runtime/drawing-label-auto-number/probe.cpp
//
//	[issue #179] 図面ラベル（`Drawing Label2`）の図番（`Drawing`）の自動採番の規則を決める。
//
//	## 2 巡目（ビルド 00849e667e22）までに割れたこと
//
//	**(1) 採番の規則。** 作った瞬間の図番は「**そのシートレイヤで、いま図番として使われて
//	いる正整数のうち、使われていない最小のもの**」。`max + 1` ではない（消した番号は埋まる）。
//	数として読めない図番（`A-01` など）は「使われている」に入らない。
//
//	**(2) なぜ番号が戻るのか。** **ビューポートの注釈の「先頭のラベル」は、その注釈へ
//	最後に入れたラベルの図番を映し続ける。** 2 巡目の P で 1 本足すたびに全員を読んだら、
//	先頭が毎回いちばん新しい本と同じ値になっていた:
//
//	    1 本目 [1] → 2 本目 [2,2] → 3 本目 [1,2,1] → 4 本目 [3,2,1,3] → 5 本目 [4,2,1,3,4]
//
//	先頭が新しい値へ移るたびに**それまでの値が空く**ので、次の 1 本がその空きへ落ちる。
//	これで #177 の 11 本（1,2,1,3,4,5,4,6,4,7,4）が 1 本の例外も無く再現できる。
//
//	**(3) 先頭のラベルは自分の図番を持てない。** Q（文字列 `A-01`）でも R（数字 `7`）でも、
//	**同じ注釈へ 2 本目を入れた瞬間に書いた値が消えた**。S では 3 本すべてに書いたのに
//	`[S-3, S-2, S-3]`——先頭が 3 本目の値に乗っ取られ、さらに 4 本目を足すと `[1, S-2, S-3, 1]`。
//
//	## この 3 巡目が決めること
//
//	**U: 1 つのビューポートに 1 本だけ置くなら重複しないか。** (2)(3) は「同じ注釈に 2 本
//	以上」入れたときの話で、**先頭＝最後なら鏡は自分自身**になる。実際の使い方
//	（ビューポートごとに 1 本、その真下へ）がこれなので、**「自分で書かなくてよい場面」が
//	あるかどうかはここで決まる**。ビューポートを 5 枚作って 1 本ずつ入れ、毎回全員を読む。
//	そのうえで 5 本すべてへ自分の値を書き、引き直して守られるかを見る。
//
//	**V: 「鏡」は並び順の先頭か、最初に入れた 1 本か。** 3 本入れてから先頭を消すと、
//	2 本目が鏡になるのか（位置で決まる）、誰も鏡にならないのか（最初の 1 本に紐づく）。
//	どちらでも「2 本以上入れるな」という結論は変わらないが、**先頭を消して直せるのか**が
//	決まる。
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

	std::string ProbeStateOf(MCObjectHandle h)
	{
		if (h == nil)
			return "(消した)";
		VWFC::VWObjects::VWParametricObj obj(h);
		return "図番=「" + ProbeStr(obj.GetParamString("Drawing")) +
			   "」/リンク=" + ProbeInt(obj.GetParamLong("Link State"));
	}

	void ProbeDumpAll(vwprobe::Report& probe, const std::string& tag,
					  const std::vector<MCObjectHandle>& labels)
	{
		std::string line = tag + ": 【全員】";
		for (size_t i = 0; i < labels.size(); ++i)
			line += " " + ProbeInt(static_cast<Sint32>(i) + 1) + ":" + ProbeStateOf(labels[i]);
		probe.log(line);
	}

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
					const std::string& value)
	{
		if (h == nil)
			return;
		VWFC::VWObjects::VWParametricObj(h).SetParamValue("Drawing", value.c_str());
		gSDK->ResetObject(h);
		probe.log(tag + ": 図番へ「" + value + "」を書いて ResetObject → " + ProbeStateOf(h));
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
} // namespace

VW_PROBE("drawing-label-auto-number", "図面ラベルの図番の自動採番（3 巡目）",
		 "1 ビューポートに 1 本だけなら重複しないか／鏡は並び順の先頭か")
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
	probe.log("=== U: ビューポート 5 枚へ 1 本ずつ（実際の使い方）===");
	probe.log("U: 先頭＝最後なので鏡は自分自身のはず。1,2,3,4,5 と増えて重複しないかを見る");
	MCObjectHandle sheetU = ProbeMakeSheet(probe, "調査シート U");
	if (sheetU == nil)
		return;
	std::vector<MCObjectHandle> vpsU;
	for (int i = 1; i <= 5; ++i)
	{
		MCObjectHandle vp = ProbeMakeViewport(probe, "U の VP" + ProbeInt(i), sheetU, design);
		if (vp != nil)
			vpsU.push_back(vp);
	}
	std::vector<MCObjectHandle> labelsU;
	for (size_t i = 0; i < vpsU.size(); ++i)
	{
		const std::string tag = "U VP" + ProbeInt(static_cast<Sint32>(i) + 1) + " の 1 本目";
		MCObjectHandle h = ProbeMakeLabel(probe, tag, vpsU[i]);
		if (h == nil)
			return;
		labelsU.push_back(h);
		ProbeDumpAll(probe, tag + "の直後", labelsU);
	}
	probe.log("U: ここまでで重複が無ければ、1 ビューポート 1 本なら SDK 任せでよいことになる");

	probe.log("--- U: そのうえで 5 本すべてへ自分の値を書く ---");
	for (size_t i = 0; i < labelsU.size(); ++i)
		ProbeWrite(probe, "U" + ProbeInt(static_cast<Sint32>(i) + 1), labelsU[i],
				   "U-" + ProbeInt(static_cast<Sint32>(i) + 1));
	ProbeDumpAll(probe, "U 5 本とも書いた", labelsU);
	for (size_t i = 0; i < vpsU.size(); ++i)
		gSDK->UpdateViewport(vpsU[i]);
	ProbeDumpAll(probe, "U 全部を引き直した後", labelsU);

	// -----------------------------------------------------------------------
	probe.log("=== V: 3 本入れてから先頭を消す——鏡は並び順の先頭へ移るか ===");
	MCObjectHandle sheetV = ProbeMakeSheet(probe, "調査シート V");
	if (sheetV == nil)
		return;
	MCObjectHandle vpV = ProbeMakeViewport(probe, "V", sheetV, design);
	std::vector<MCObjectHandle> labelsV;
	for (int i = 1; i <= 3; ++i)
	{
		const std::string tag = "V " + ProbeInt(i) + " 本目";
		MCObjectHandle h = ProbeMakeLabel(probe, tag, vpV);
		if (h == nil)
			return;
		labelsV.push_back(h);
		ProbeDumpAll(probe, tag + "の直後", labelsV);
	}
	probe.log("V: 2 本目・3 本目へ自分の値を書いてから、先頭を消す");
	ProbeWrite(probe, "V2", labelsV[1], "V-2");
	ProbeWrite(probe, "V3", labelsV[2], "V-3");
	ProbeDumpAll(probe, "V 2・3 へ書いた", labelsV);

	gSDK->DeleteObject(labelsV[0], false);
	labelsV[0] = nil;
	probe.log("V: 先頭（1 本目）を消した");
	ProbeDumpAll(probe, "V 先頭を消した直後", labelsV);
	if (vpV != nil)
	{
		gSDK->UpdateViewport(vpV);
		ProbeDumpAll(probe, "V 引き直した後", labelsV);
	}

	probe.log("V: さらに 1 本足す——いま誰が鏡になっているかが出る");
	MCObjectHandle v4 = ProbeMakeLabel(probe, "V 4 本目", vpV);
	labelsV.push_back(v4);
	ProbeDumpAll(probe, "V 4 本目の直後", labelsV);
	if (vpV != nil)
	{
		gSDK->UpdateViewport(vpV);
		ProbeDumpAll(probe, "V 最後に引き直した", labelsV);
	}

	probe.log("おわり");
}
