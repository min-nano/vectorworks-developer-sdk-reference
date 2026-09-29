//
//	probes/runtime/drawing-label-auto-number/probe.cpp
//
//	[issue #179] 図面ラベル（`Drawing Label2`）の図番（`Drawing`）の自動採番の規則を決める。
//
//	Findings「Drawing Labels」は #149 の実測から「**同じシートレイヤに置くたび 1 ずつ増える**」と
//	書いているが、#177 / PR #178 の走行では **1,2,1,3,4,5,4,6,4,7,4** と戻る回があり、
//	**同じシートレイヤに同じ図番が 4 本並んだ**。「どういう条件なら 1 ずつ増えるのか」が
//	決まっていないので、効きそうな因子を 1 つずつ振り分けて測る。
//
//	## 振り分ける因子
//
//	1. **`CreateCustomObject` の `bInsert`。** #149 も #177 も `false` で呼んでいる。
//	   `false` だと**どの容れ物にも入らないまま生まれる**見込みで、そうなら「同じ
//	   シートレイヤに何本あるか」を数えようがない。ここが本命の疑い。
//	2. **注釈へ入れるか。** 作ってすぐ `AddViewportAnnotationObject` で
//	   ビューポートの注釈へ移すと、シートレイヤの直下からは消える。
//	3. **ビューポートを跨ぐか。** #177 では跨いだ直後に戻る回があった。
//	4. **シートレイヤが変わると番号は戻るか**（採番の単位が文書か、シートレイヤか）。
//	   →**1 フェーズ 1 シートレイヤ**にしてあるので、各フェーズの 1 本目を見れば分かる。
//	5. **空いた番号を埋めるか**（`max + 1` なのか「一番小さい空き」なのか）。
//	6. **自分で書いた番号は次の採番に効くか。また、書いた値は残るか。**
//	7. **`varUseAutoDrawCoord`（プログラム変数 544）が効くか。** SDK のヘッダに
//	   `whether to coordinate sheet and drawing numbers for various items` とある
//	   read/write の真偽値で、**SDK で「図番の自動採番」に触れている唯一の口**
//	   （`sdk-grep` で `DrawingNumber|DrawingNo|NextDrawing|drawing number|DrawingLabel`
//	   を引いて出たのは、内部 ID 2 つ・スタイルのフォルダ・これだけ）。切って作れば
//	   採番が止まるのか、止まるとしたら何が入るのかを見る。
//
//	## 読み方
//
//	各フェーズの終わりに【いまのシート】として、**シートレイヤの直下に居るラベル**と
//	**ビューポートの注釈の中に居るラベル**を図番付きで並べる。採番が「何を見ているか」は、
//	次の 1 本の番号とこの並びを突き合わせれば決まる。
//	最後に全部のラベルをもう一度読み直し、**後から番号が動くか**も見る。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <utility>
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

	// ラベルの型番号。1 本目を作った時点で引いて、以後の走査の目印にする
	// （型番号をコードに書き込まないので、版が変わっても外れない）。
	short gProbeLabelType = 0;

	// 作ったラベル全部（最後にもう一度読み直すため）。消したものは外す。
	std::vector<std::pair<std::string, MCObjectHandle>> gProbeMade;

	std::string ProbeDrawingOf(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		return ProbeStr(VWFC::VWObjects::VWParametricObj(h).GetParamString("Drawing"));
	}

	// ラベルを 1 本作る。vp が nil でなければ、作った直後に注釈へ移す。
	// 各段（作った直後 / 注釈へ入れた後 / ResetObject の後）の図番をすべて出す。
	MCObjectHandle ProbeMakeLabel(vwprobe::Report& probe, const std::string& tag, bool insert,
								  MCObjectHandle vp)
	{
		MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, insert);
		if (h == nil)
		{
			probe.fail(tag + ": CreateCustomObject が nil（bInsert=" +
					   std::string(insert ? "true" : "false") + "）");
			return nil;
		}
		if (gProbeLabelType == 0)
			gProbeLabelType = gSDK->GetObjectTypeN(h);

		std::string line = tag + ": bInsert=" + std::string(insert ? "true " : "false") +
						   " 作った直後 図番=「" + ProbeDrawingOf(h) + "」";
		if (vp != nil)
		{
			const bool added = (gSDK->AddViewportAnnotationObject(vp, h) != 0);
			line += " → 注釈へ入れた=" + std::string(added ? "true" : "false") + " 図番=「" +
					ProbeDrawingOf(h) + "」";
			gSDK->ResetObject(h);
			line += " → ResetObject 後 図番=「" + ProbeDrawingOf(h) + "」";
		}
		probe.log(line);
		gProbeMade.push_back(std::make_pair(tag, h));
		return h;
	}

	// そのシートレイヤに「いま」居るラベルを、図番付きで並べる。
	// 直下（レイヤの直接の子）と、注釈の中（渡されたビューポートの注釈群）を分けて数える。
	void ProbeCensus(vwprobe::Report& probe, const std::string& tag, MCObjectHandle sheet,
					 const std::vector<MCObjectHandle>& vps)
	{
		if (gProbeLabelType == 0)
		{
			probe.log(tag + ": 【いまのシート】ラベルの型がまだ分からない（1 本も作れていない）");
			return;
		}
		int nDirect = 0;
		std::string direct;
		for (MCObjectHandle m = gSDK->FirstMemberObj(sheet); m != nil; m = gSDK->NextObject(m))
		{
			if (gSDK->GetObjectTypeN(m) != gProbeLabelType)
				continue;
			++nDirect;
			direct += (direct.empty() ? "" : " ") + ProbeDrawingOf(m);
		}
		int nAnno = 0;
		std::string anno;
		for (size_t i = 0; i < vps.size(); ++i)
		{
			MCObjectHandle group = gSDK->GetViewportGroup(vps[i], kViewportGroupAnnotation);
			if (group == nil)
				continue;
			for (MCObjectHandle a = gSDK->FirstMemberObj(group); a != nil; a = gSDK->NextObject(a))
			{
				if (gSDK->GetObjectTypeN(a) != gProbeLabelType)
					continue;
				++nAnno;
				anno += (anno.empty() ? "" : " ") + ProbeDrawingOf(a);
			}
		}
		probe.log(tag + ": 【いまのシート】直下 " + ProbeInt(nDirect) + " 本 [" + direct +
				  "] / 注釈の中（ビューポート " + ProbeInt(static_cast<Sint32>(vps.size())) +
				  " 枚） " + ProbeInt(nAnno) + " 本 [" + anno + "]");
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

VW_PROBE("drawing-label-auto-number", "図面ラベルの図番の自動採番",
		 "図番が 1 ずつ増える条件と、増えない条件を振り分ける")
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
	probe.log("フェーズごとに別のシートレイヤを使う（採番の単位がシートレイヤなら、"
			  "各フェーズの 1 本目が 1 に戻る）");

	std::vector<MCObjectHandle> noVp;

	// -----------------------------------------------------------------------
	probe.log("=== A: bInsert=true・シートレイヤ直下に置きっぱなし（6 本）===");
	MCObjectHandle sheetA = ProbeMakeSheet(probe, "調査シート A");
	if (sheetA == nil)
		return;
	for (int i = 1; i <= 6; ++i)
		ProbeMakeLabel(probe, "A" + ProbeInt(i), true, nil);
	ProbeCensus(probe, "A 終わり", sheetA, noVp);

	// -----------------------------------------------------------------------
	probe.log("=== B: bInsert=false・どこへも入れない（6 本）===");
	MCObjectHandle sheetB = ProbeMakeSheet(probe, "調査シート B");
	if (sheetB == nil)
		return;
	for (int i = 1; i <= 6; ++i)
		ProbeMakeLabel(probe, "B" + ProbeInt(i), false, nil);
	ProbeCensus(probe, "B 終わり", sheetB, noVp);

	// -----------------------------------------------------------------------
	probe.log("=== C: bInsert=true・作ってすぐ 1 枚のビューポートの注釈へ（6 本）===");
	MCObjectHandle sheetC = ProbeMakeSheet(probe, "調査シート C");
	if (sheetC == nil)
		return;
	MCObjectHandle vpC = ProbeMakeViewport(probe, "C", sheetC, design);
	std::vector<MCObjectHandle> vpsC;
	if (vpC != nil)
		vpsC.push_back(vpC);
	for (int i = 1; i <= 6; ++i)
		ProbeMakeLabel(probe, "C" + ProbeInt(i), true, vpC);
	ProbeCensus(probe, "C 終わり", sheetC, vpsC);

	// -----------------------------------------------------------------------
	probe.log("=== D: bInsert=false・作ってすぐ注釈へ（6 本。#177 と同じ条件）===");
	MCObjectHandle sheetD = ProbeMakeSheet(probe, "調査シート D");
	if (sheetD == nil)
		return;
	MCObjectHandle vpD = ProbeMakeViewport(probe, "D", sheetD, design);
	std::vector<MCObjectHandle> vpsD;
	if (vpD != nil)
		vpsD.push_back(vpD);
	for (int i = 1; i <= 6; ++i)
		ProbeMakeLabel(probe, "D" + ProbeInt(i), false, vpD);
	ProbeCensus(probe, "D 終わり", sheetD, vpsD);

	// -----------------------------------------------------------------------
	probe.log("=== E: bInsert=true・ビューポートを 3 枚跨ぐ（2 本ずつ＋最後に 1 枚目へ戻る）===");
	MCObjectHandle sheetE = ProbeMakeSheet(probe, "調査シート E");
	if (sheetE == nil)
		return;
	std::vector<MCObjectHandle> vpsE;
	for (int i = 1; i <= 3; ++i)
	{
		MCObjectHandle vp = ProbeMakeViewport(probe, "E の VP" + ProbeInt(i), sheetE, design);
		if (vp != nil)
			vpsE.push_back(vp);
	}
	for (size_t i = 0; i < vpsE.size(); ++i)
	{
		const std::string vpTag = "VP" + ProbeInt(static_cast<Sint32>(i) + 1);
		ProbeMakeLabel(probe, "E " + vpTag + " の 1 本目", true, vpsE[i]);
		ProbeMakeLabel(probe, "E " + vpTag + " の 2 本目", true, vpsE[i]);
	}
	if (!vpsE.empty())
		ProbeMakeLabel(probe, "E VP1 へ戻って 3 本目", true, vpsE[0]);
	ProbeCensus(probe, "E 終わり", sheetE, vpsE);

	// -----------------------------------------------------------------------
	probe.log("=== F: 空いた番号を埋めるか / 自分で書いた番号は効くか ===");
	MCObjectHandle sheetF = ProbeMakeSheet(probe, "調査シート F");
	if (sheetF == nil)
		return;
	MCObjectHandle f1 = ProbeMakeLabel(probe, "F1", true, nil);
	MCObjectHandle f2 = ProbeMakeLabel(probe, "F2", true, nil);
	ProbeMakeLabel(probe, "F3", true, nil);
	ProbeMakeLabel(probe, "F4", true, nil);
	ProbeCensus(probe, "F 4 本を置いた", sheetF, noVp);

	if (f2 != nil)
	{
		probe.log("F: 2 本目（図番「" + ProbeDrawingOf(f2) + "」）を消す");
		for (size_t i = 0; i < gProbeMade.size(); ++i)
		{
			if (gProbeMade[i].second == f2)
			{
				gProbeMade.erase(gProbeMade.begin() + static_cast<long>(i));
				break;
			}
		}
		gSDK->DeleteObject(f2, false);
		f2 = nil;
		ProbeCensus(probe, "F 2 本目を消した", sheetF, noVp);
	}
	ProbeMakeLabel(probe, "F5（穴を埋めるか）", true, nil);

	if (f1 != nil)
	{
		VWFC::VWObjects::VWParametricObj(f1).SetParamValue("Drawing", "100");
		probe.log("F: 1 本目へ「100」を書いた → 図番=「" + ProbeDrawingOf(f1) + "」");
		gSDK->ResetObject(f1);
		probe.log("F: ResetObject 後 → 図番=「" + ProbeDrawingOf(f1) + "」");
	}
	ProbeMakeLabel(probe, "F6（書いた 100 の次になるか）", true, nil);
	ProbeCensus(probe, "F 終わり", sheetF, noVp);

	// -----------------------------------------------------------------------
	probe.log("=== G: 自分で書いた図番は、注釈へ入れても ResetObject しても残るか ===");
	MCObjectHandle sheetG = ProbeMakeSheet(probe, "調査シート G");
	if (sheetG == nil)
		return;
	MCObjectHandle vpG = ProbeMakeViewport(probe, "G", sheetG, design);
	std::vector<MCObjectHandle> vpsG;
	if (vpG != nil)
		vpsG.push_back(vpG);
	MCObjectHandle g1 = ProbeMakeLabel(probe, "G1", true, nil);
	if (g1 != nil)
	{
		VWFC::VWObjects::VWParametricObj(g1).SetParamValue("Drawing", "A-01");
		probe.log("G1: 「A-01」を書いた → 図番=「" + ProbeDrawingOf(g1) + "」");
		const bool added = (gSDK->AddViewportAnnotationObject(vpG, g1) != 0);
		probe.log("G1: 注釈へ入れた=" + std::string(added ? "true" : "false") + " → 図番=「" +
				  ProbeDrawingOf(g1) + "」");
		gSDK->ResetObject(g1);
		probe.log("G1: ResetObject 後 → 図番=「" + ProbeDrawingOf(g1) + "」");
		if (vpG != nil)
		{
			gSDK->UpdateViewport(vpG);
			probe.log("G1: UpdateViewport 後 → 図番=「" + ProbeDrawingOf(g1) + "」");
		}
	}
	ProbeMakeLabel(probe, "G2（文字の図番の次はどうなるか）", true, nil);
	ProbeCensus(probe, "G 終わり", sheetG, vpsG);

	// -----------------------------------------------------------------------
	probe.log("=== H: varUseAutoDrawCoord（544）を切ると採番は止まるか ===");
	MCObjectHandle sheetH = ProbeMakeSheet(probe, "調査シート H");
	if (sheetH == nil)
		return;
	Boolean coordWas = false;
	const bool coordRead = (gSDK->GetProgramVariable(varUseAutoDrawCoord, &coordWas) != 0);
	probe.log("H: varUseAutoDrawCoord を読めた=" + std::string(coordRead ? "true" : "false") +
			  " 値=" + std::string(coordWas ? "true" : "false"));
	Boolean coordOff = false;
	const bool coordSet = (gSDK->SetProgramVariable(varUseAutoDrawCoord, &coordOff) != 0);
	probe.log("H: false を書けた=" + std::string(coordSet ? "true" : "false"));
	for (int i = 1; i <= 4; ++i)
		ProbeMakeLabel(probe, "H" + ProbeInt(i) + "（採番の協調を切って）", true, nil);
	Boolean coordOn = true;
	gSDK->SetProgramVariable(varUseAutoDrawCoord, &coordOn);
	probe.log("H: true へ戻した");
	for (int i = 5; i <= 8; ++i)
		ProbeMakeLabel(probe, "H" + ProbeInt(i) + "（戻してから）", true, nil);
	gSDK->SetProgramVariable(varUseAutoDrawCoord, &coordWas);
	ProbeCensus(probe, "H 終わり", sheetH, noVp);

	// -----------------------------------------------------------------------
	probe.log("=== Z: 全部のビューポートを引き直してから、全部のラベルを読み直す ===");
	for (size_t i = 0; i < vpsC.size(); ++i)
		gSDK->UpdateViewport(vpsC[i]);
	for (size_t i = 0; i < vpsD.size(); ++i)
		gSDK->UpdateViewport(vpsD[i]);
	for (size_t i = 0; i < vpsE.size(); ++i)
		gSDK->UpdateViewport(vpsE[i]);
	for (size_t i = 0; i < vpsG.size(); ++i)
		gSDK->UpdateViewport(vpsG[i]);
	for (size_t i = 0; i < gProbeMade.size(); ++i)
		probe.log("Z: " + gProbeMade[i].first + " の図番 = 「" +
				  ProbeDrawingOf(gProbeMade[i].second) + "」");

	probe.log("おわり");
}
