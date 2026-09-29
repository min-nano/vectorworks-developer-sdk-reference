//
//	probes/runtime/drawing-label-first-in-annotation/probe.cpp
//
//	[issue #177] 作りたてのビューポートの注釈へ図面ラベル（Drawing Label2）を続けて入れると、
//	**先頭の 1〜2 本だけ**が「縮尺が掛からない」ように見える件。原因と、確実に正しく
//	描かせる手順を決める。
//
//	**#175 / PR #176 の 7 回目のログを読み直すと、「縮尺が掛からない」は読み違えの疑いが濃い。**
//	7 回目（`4b1eea4ff60e`）の実測:
//
//	  N1（注釈・1 本目）外接 幅=238.12500 高=**0.00000** / テキスト 1 件: 3.52778
//	  N2（注釈・2 本目）外接 幅=238.12500 高=**0.00000** / テキスト 1 件: 3.52778
//	  N3（注釈・3 本目）外接 幅=1298.22225 高=264.58333 / テキスト **2 件**: 3.52778 176.38889
//
//	大きさ 3.52778 のテキストが描かれていれば外接の高さは 5.29167（＝×1.5）になるはずで、
//	**高さ 0 は「そのラベルはテキストを 1 文字も描いていない」ことを意味する**。つまり
//	N1・N2 で拾った 3.52778 は**描いた文字ではなく、レイアウト（プロファイルグループ）側の
//	値**を走査が一緒に拾ってしまったもので、N3 以降で 2 件になるのは「レイアウト＋描いた
//	文字」が並んだため——という読みが立つ。**そうなら「紙で 0.2pt」という記述は存在しない
//	ものを測っていたことになる。**
//
//	ラベルのレイアウトのテキストは `#Drawing Label2#.#Title#` を描く（Findings「Drawing
//	Labels」）。**`Title` が空なら描く文字が無く、外接は線だけ＝高さ 0 になる。**
//	Findings には既に「**置いた直後の `Title` は当てにならない。自分で書くのが確実**」と
//	書いてあるので、**先頭のラベルが空なだけ**なら、既知の作法でそのまま防げる。
//
//	## このプローブが決めること
//
//	1. **何を測っていたのか。** ラベルの木を深さ・型・**文字の中身**・大きさ・外接まで
//	   すべて出し、プロファイルグループのハンドルに印を付ける。これで「レイアウトの値を
//	   拾っていたのか」「本当に小さい文字が描かれているのか」が 1 回で割れる。
//	2. **原因は `Title` が空なことか。** 3 本の新品のビューポートで振り分ける:
//	     VP1 … ビューポートの図面タイトル（1032）は空・`Title` を書かない（＝再現の腕）
//	     VP2 … **1032 に文字列を入れてから**ラベルを置く・`Title` を書かない
//	     VP3 … 1032 は空・**各ラベルに `Title` を自分で書く**
//	   VP2 / VP3 で全部描かれれば、原因は「描く文字が無い」ことだと決まる。
//	3. **ビューポートの「新しさ」が効くのか。** 最後に VP1 へ戻ってもう 1 本置く。
//	   ここで描かれれば「温まった容れ物では起きない」、描かれなければ容れ物は無関係。
//	4. **PR #176 がやっていたスタイル外し（`SetPluginObjectStyle(h, 0)`）が絡むか。**
//	   VP4 の 1 本目で同じ手順をなぞる。
//
//	与える文字の大きさは全部 3.52778（＝紙で 10pt）。容れ物が 1/50 なら描いた文字は
//	176.38889 になるのが正しい（#175 で確定済み。ここでは測り直さない）。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	const short kProbeTextNodeType = 10;
	const short kProbeLineNodeType = 2;

	const double kProbeVpScale = 50.0;
	const double kProbePtToMm = 25.4 / 72.0;
	const double kProbeTenPtMm = 10.0 * kProbePtToMm; // 3.52778

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
		const char* p = static_cast<const char*>(s);
		return p != nullptr ? std::string(p) : std::string();
	}

	std::string ProbeBoundsOf(MCObjectHandle h)
	{
		WorldRect r;
		if (h == nil || !gSDK->GetObjectBounds(h, r))
			return "外接=取れない";
		return "外接 幅=" + ProbeNum(std::fabs(r.right - r.left)) +
			   " 高=" + ProbeNum(std::fabs(r.top - r.bottom));
	}

	// ラベルの木を丸ごと出す。プロファイルグループ（レイアウト）のハンドルには印を付ける
	// ので、「拾った値がレイアウトのものか、描いた文字か」がログだけで決まる。
	void ProbeDumpTree(vwprobe::Report& probe, const std::string& tag, MCObjectHandle container,
					   int depth, MCObjectHandle hLayout, double containerScale)
	{
		if (container == nil || depth > 4)
			return;
		int seen = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil; m = gSDK->NextObject(m))
		{
			if (++seen > 12)
			{
				probe.log(tag + ": " + std::string(static_cast<size_t>(depth) * 2, ' ') +
						  "（同じ深さの残りは省略）");
				return;
			}
			const short type = gSDK->GetObjectTypeN(m);
			std::string line = tag + ": " + std::string(static_cast<size_t>(depth) * 2, ' ') +
							   "深さ" + ProbeInt(depth) + " 型=" + ProbeInt(type) + " " +
							   ProbeBoundsOf(m);
			if (hLayout != nil && m == hLayout)
				line += " ★これはレイアウト（プロファイルグループ）";
			if (type == kProbeTextNodeType)
			{
				WorldCoord size = 0;
				gSDK->GetTextSize(m, 0, size);
				line += " 大きさ=" + ProbeNum(static_cast<double>(size));
				if (containerScale > 0.0)
					line += "(紙で" +
							ProbeNum(static_cast<double>(size) / containerScale / kProbePtToMm) +
							"pt)";
				line += " 文字数=" + ProbeInt(gSDK->GetTextLength(m)) + " 文字=「" +
						ProbeStr(gSDK->GetTextChars(m)) + "」";
			}
			probe.log(line);
			ProbeDumpTree(probe, tag, m, depth + 1, hLayout, containerScale);
		}
	}

	std::string ProbeParams(MCObjectHandle h)
	{
		VWFC::VWObjects::VWParametricObj obj(h);
		return "Title=「" + ProbeStr(obj.GetParamString("Title")) +
			   "」 LinkState=" + ProbeInt(obj.GetParamLong("Link State")) + " Drawing=「" +
			   ProbeStr(obj.GetParamString("Drawing")) + "」";
	}

	std::string ProbeVpTitle(MCObjectHandle vp)
	{
		TVariableBlock block;
		TXString title;
		if (gSDK->GetObjectVariable(vp, ovViewportDescription, block))
			block.GetTXString(title);
		return "1032=「" + ProbeStr(title) + "」";
	}

	int gProbeSpot = 0;

	WorldPt ProbeNextSpot()
	{
		return WorldPt(0, -3000.0 * (gProbeSpot++));
	}

	// レイアウトを「タイトルのテキスト 1 つ＋下線 1 本」に組み直し、テキストへ
	// 3.52778（紙で 10pt）を与える（Findings「Drawing Labels」の手順どおり複製する）。
	void ProbeRebuildLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		MCObjectHandle hOld = gSDK->GetCustomObjectProfileGroup(h);
		MCObjectHandle hGroup = (hOld != nil) ? gSDK->CreateGroup(false) : nil;
		if (hGroup == nil)
		{
			probe.log(tag + ": レイアウトを組み直せない（元のレイアウトが nil）");
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
				gSDK->SetTextSize(dup, 0, len > 0 ? len : 1,
								  static_cast<WorldCoord>(kProbeTenPtMm));
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

	// 1 本置いて、置くまでの各段の値と、できあがった木を出す。
	void ProbePlaceLabel(vwprobe::Report& probe, const std::string& tag, MCObjectHandle vp,
						 bool writeTitle, bool dropStyle)
	{
		probe.log("--- " + tag + " ---");
		probe.log(tag + ": 置く前のビューポート " + ProbeVpTitle(vp));

		MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", ProbeNextSpot(), 0.0, false);
		if (h == nil)
		{
			probe.log(tag + ": CreateCustomObject が nil（ラベルを作れなかった）");
			return;
		}
		probe.log(tag + ": 作った直後 " + ProbeParams(h));

		ProbeRebuildLayout(probe, tag, h);

		const bool added = (gSDK->AddViewportAnnotationObject(vp, h) != 0);
		probe.log(tag + ": 注釈へ入れた=" + std::string(added ? "true" : "false") + " / " +
				  ProbeParams(h));

		if (writeTitle)
		{
			VWFC::VWObjects::VWParametricObj(h).SetParamValue("Title", "調査用の図面タイトル");
			probe.log(tag + ": Title を自分で書いた → " + ProbeParams(h));
		}

		gSDK->ResetObject(h);
		if (dropStyle)
		{
			gSDK->SetPluginObjectStyle(h, 0);
			gSDK->ResetObject(h);
			probe.log(tag + ": スタイルを外して作り直した（PR #176 と同じ手順）");
		}

		probe.log(tag + ": 作り直した後 " + ProbeParams(h) + " / ラベル自身の " + ProbeBoundsOf(h));
		probe.log(tag + ": 置いた後のビューポート " + ProbeVpTitle(vp));
		ProbeDumpTree(probe, tag, h, 0, gSDK->GetCustomObjectProfileGroup(h), kProbeVpScale);
	}

	MCObjectHandle ProbeMakeViewport(vwprobe::Report& probe, const std::string& tag,
									 MCObjectHandle sheet, MCObjectHandle design,
									 const char* description)
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
		if (description != nullptr)
		{
			TVariableBlock descVar;
			descVar = TXString(description);
			gSDK->SetObjectVariable(vp, ovViewportDescription, descVar);
		}
		gSDK->UpdateViewport(vp);
		probe.log(tag + ": ビューポートを作った / " + ProbeVpTitle(vp));
		return vp;
	}
} // namespace

VW_PROBE("drawing-label-first-in-annotation", "注釈の先頭の図面ラベル",
		 "先頭のラベルが描かれない原因と、確実に描かせる手順を決める")
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
	probe.log("正しい答え: レイアウトへ " + ProbeNum(kProbeTenPtMm) +
			  " を与えたので、1/50 の"
			  "注釈で描かれる文字は 176.38889（紙で 10pt）");

	// -----------------------------------------------------------------------
	probe.log("=== VP1: 図面タイトル（1032）は空・Title を書かない（再現の腕）===");
	MCObjectHandle vp1 = ProbeMakeViewport(probe, "VP1", sheet, design, nullptr);
	if (vp1 == nil)
		return;
	ProbePlaceLabel(probe, "A1 VP1 の 1 本目", vp1, false, false);
	ProbePlaceLabel(probe, "A2 VP1 の 2 本目", vp1, false, false);
	ProbePlaceLabel(probe, "A3 VP1 の 3 本目", vp1, false, false);
	ProbePlaceLabel(probe, "A4 VP1 の 4 本目", vp1, false, false);

	// -----------------------------------------------------------------------
	probe.log("=== VP2: 1032 に文字列を入れてから置く・Title は書かない ===");
	MCObjectHandle vp2 = ProbeMakeViewport(probe, "VP2", sheet, design, "VP2 の図面タイトル");
	if (vp2 != nil)
	{
		ProbePlaceLabel(probe, "B1 VP2 の 1 本目", vp2, false, false);
		ProbePlaceLabel(probe, "B2 VP2 の 2 本目", vp2, false, false);
	}

	// -----------------------------------------------------------------------
	probe.log("=== VP3: 1032 は空・各ラベルに Title を自分で書く ===");
	MCObjectHandle vp3 = ProbeMakeViewport(probe, "VP3", sheet, design, nullptr);
	if (vp3 != nil)
	{
		ProbePlaceLabel(probe, "C1 VP3 の 1 本目", vp3, true, false);
		ProbePlaceLabel(probe, "C2 VP3 の 2 本目", vp3, true, false);
	}

	// -----------------------------------------------------------------------
	probe.log("=== VP4: PR #176 と同じ手順（スタイルを外す）の 1 本目 ===");
	MCObjectHandle vp4 = ProbeMakeViewport(probe, "VP4", sheet, design, nullptr);
	if (vp4 != nil)
	{
		ProbePlaceLabel(probe, "E1 VP4 の 1 本目・スタイル外し", vp4, false, true);
		ProbePlaceLabel(probe, "E2 VP4 の 2 本目・スタイル外し＋Title を書く", vp4, true, true);
	}

	// -----------------------------------------------------------------------
	probe.log("=== VP1 へ戻る: 容れ物の「新しさ」が効くのかを見る ===");
	ProbePlaceLabel(probe, "D1 VP1 の 5 本目（他を全部置いた後）", vp1, false, false);

	probe.log("おわり");
}
