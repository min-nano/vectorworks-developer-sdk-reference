//
//	probes/runtime/drawing-label-title-empty-redraw/probe.cpp
//
//	[issue #177] 前回（`drawing-label-first-in-annotation`）の測り直し。**「一度も描かせて
//	いないものを測った結果を信じてよいか」**を潰す。
//
//	前回の結論は「`Title` が空だとラベルは文字を 1 文字も描かない。`Title` を書けば
//	1 本目から ×縮尺で正しく描かれる」だった。ただし**あの走行のビューポートは全クラスが
//	非表示のまま**（`CreateViewport` の既定）で、**一度も絵になっていない**。VW が
//	「描くときまで計算を遅らせる」作りなら、測っていたのは未完成の状態かもしれない。
//
//	**データ自身は既に遅延を否定している**——同じ「描かれていない」ビューポートの中で、
//	`Title` を書いた腕だけは `ResetObject` の直後に完成した図形を持っていた（文字の中身
//	`調査用の図面タイトル` / 大きさ `176.38889` ＝ 容れ物の縮尺まで掛かった値）。遅延して
//	いるならこちらも空だったはずである。**それでも機械で確かめ直せるので確かめる。**
//
//	## このプローブが変えたところ
//
//	1. **クラスを全部表示へ戻し、`UpdateViewport` で実際に描かせる**
//	   （[Findings「Viewports」](../../../Findings/Viewports.md)「クラス表示」）。
//	   **利用者が絵で突き合わせられる**ようにもなる。
//	2. **同じラベルを 2 度測る**——(1) `ResetObject` の直後（＝前回と同じ条件）と
//	   (2) クラスを戻して更新した後。**値が動かなければ「遅延していた」は消える。**
//	3. **1 つのビューポートに図面ラベルは 1 本だけ**にする。前回の走行で
//	   VectorWorks 自身が警告を出した——「自動作図調整は、1 つのビューポートに複数の
//	   図面ラベルがあると正常に機能しません」。前回の結論を、**正常な置き方でも
//	   同じか**を確かめ直すことにもなる。
//	4. **`Title` を更新の後にもう一度読む。** リンク（`Link State`=1）の押し込みが
//	   描画のときに解決されるなら、ここで空でなくなるはずである。
//
//	## 腕（ビューポートは横に離して置くので、絵で見比べられる）
//
//	  VP-A（左）  `Title` を書かない → 下線だけ・文字なし のはず
//	  VP-B（右）  `Title` を書く     → 紙で 10pt の文字が出るはず
//
//	与える文字の大きさはどちらも `3.52778`（紙で 10pt）。容れ物は 1/50 なので、正しく
//	描かれた文字は `176.38889` になる。
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

	// ラベルの木を丸ごと出す。レイアウト（プロファイルグループ）には印を付ける——
	// **そこを描いた文字と取り違えたのが #177 の読み違えの正体**だった。
	void ProbeDumpTree(vwprobe::Report& probe, const std::string& tag, MCObjectHandle container,
					   int depth, MCObjectHandle hLayout)
	{
		if (container == nil || depth > 4)
			return;
		int seen = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil; m = gSDK->NextObject(m))
		{
			if (++seen > 12)
			{
				probe.log(tag + ": （同じ深さの残りは省略）");
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
				line += " 大きさ=" + ProbeNum(static_cast<double>(size)) + "(紙で" +
						ProbeNum(static_cast<double>(size) / kProbeVpScale / kProbePtToMm) + "pt)" +
						" 文字数=" + ProbeInt(gSDK->GetTextLength(m)) + " 文字=「" +
						ProbeStr(gSDK->GetTextChars(m)) + "」";
			}
			probe.log(line);
			ProbeDumpTree(probe, tag, m, depth + 1, hLayout);
		}
	}

	std::string ProbeParams(MCObjectHandle h)
	{
		VWFC::VWObjects::VWParametricObj obj(h);
		return "Title=「" + ProbeStr(obj.GetParamString("Title")) +
			   "」 LinkState=" + ProbeInt(obj.GetParamLong("Link State"));
	}

	std::string ProbeVpTitle(MCObjectHandle vp)
	{
		TVariableBlock block;
		TXString title;
		if (gSDK->GetObjectVariable(vp, ovViewportDescription, block))
			block.GetTXString(title);
		return "1032=「" + ProbeStr(title) + "」";
	}

	// クラスを全部表示へ戻す。**注釈へ図形を足した後にも呼ぶ**——後から足した図形の
	// クラスは非表示のまま残るため（Findings「Viewports」）。
	void ProbeShowAllClasses(MCObjectHandle vp)
	{
		gSDK->ForEachClass(
			true, [vp](MCObjectHandle cls)
			{ gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0); });
	}

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
} // namespace

VW_PROBE("drawing-label-title-empty-redraw", "空 Title のラベルを描かせて測り直す",
		 "描画まで計算が遅れている可能性を潰し、絵でも見比べられるようにする")
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

	struct ProbeArm
	{
		const char* tag;
		bool writeTitle;
		double moveX;
	};
	const ProbeArm arms[] = {
		{"VP-A（左）Title を書かない", false, 0.0},
		{"VP-B（右）Title を書く", true, 300.0},
	};

	MCObjectHandle vps[2] = {nil, nil};
	MCObjectHandle labels[2] = {nil, nil};

	for (size_t i = 0; i < 2; ++i)
	{
		const ProbeArm& arm = arms[i];
		probe.log(std::string("=== ") + arm.tag + " ===");

		MCObjectHandle vp = gSDK->CreateViewport(sheet);
		if (vp == nil)
		{
			probe.fail(std::string(arm.tag) + ": CreateViewport が nil");
			return;
		}
		vps[i] = vp;
		gSDK->SetViewportLayerVisibility(vp, design, VWFC::VWObjects::kLayerVisibilityNormal);
		TVariableBlock scaleVar;
		scaleVar = static_cast<Real64>(kProbeVpScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
		ProbeShowAllClasses(vp); // ← 前回はここを省いていたので、絵が空のままだった
		gSDK->UpdateViewport(vp);
		if (arm.moveX != 0.0)
			gSDK->MoveObject(vp, static_cast<WorldCoord>(arm.moveX), 0);
		probe.log(std::string(arm.tag) + ": ビューポートを作った / " + ProbeVpTitle(vp) + " / " +
				  ProbeBoundsOf(vp));

		MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", WorldPt(0, 0), 0.0, false);
		if (h == nil)
		{
			probe.fail(std::string(arm.tag) + ": CreateCustomObject が nil");
			return;
		}
		labels[i] = h;
		ProbeRebuildLayout(probe, arm.tag, h);
		probe.log(std::string(arm.tag) + ": 注釈へ入れた=" +
				  std::string(gSDK->AddViewportAnnotationObject(vp, h) != 0 ? "true" : "false"));
		VWFC::VWObjects::VWParametricObj(h).SetPointObjectPos(VWPoint2D(0, 0));
		if (arm.writeTitle)
			VWFC::VWObjects::VWParametricObj(h).SetParamValue("Title", "調査用の図面タイトル");
		gSDK->ResetObject(h);

		probe.log(std::string(arm.tag) + ": 【1 段目】ResetObject の直後（まだ描かせていない）" +
				  ProbeParams(h) + " / " + ProbeBoundsOf(h));
		ProbeDumpTree(probe, std::string(arm.tag) + " 1 段目", h, 0,
					  gSDK->GetCustomObjectProfileGroup(h));
	}

	// -----------------------------------------------------------------------
	// **ここで実際に描かせる。** 注釈へ後から足した図形のクラスは非表示のまま残るので、
	// クラスを戻してから更新する（Findings「Viewports」）。
	probe.log("=== 描かせる: クラスを全部表示へ戻して UpdateViewport ===");
	for (size_t i = 0; i < 2; ++i)
	{
		ProbeShowAllClasses(vps[i]);
		gSDK->UpdateViewport(vps[i]);
	}

	for (size_t i = 0; i < 2; ++i)
	{
		const ProbeArm& arm = arms[i];
		probe.log(std::string("--- ") + arm.tag + " の 2 段目 ---");
		probe.log(std::string(arm.tag) + ": 【2 段目】描かせた後 " + ProbeParams(labels[i]) +
				  " / " + ProbeBoundsOf(labels[i]) + " / ビューポートの " + ProbeVpTitle(vps[i]));
		ProbeDumpTree(probe, std::string(arm.tag) + " 2 段目", labels[i], 0,
					  gSDK->GetCustomObjectProfileGroup(labels[i]));
	}

	probe.log("=== 読み方 ===");
	probe.log("1 段目と 2 段目で値が動かなければ、「描くまで計算が遅れていた」は消える。");
	probe.log("絵での見分け: 左のビューポートのラベルは**下線だけ**、右のラベルには");
	probe.log("**紙で 10pt の文字『調査用の図面タイトル』**が出ているはず。");
	probe.log("おわり");
}
