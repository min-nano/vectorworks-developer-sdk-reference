//
//	probes/runtime/section-vp-order/probe.cpp
//
//	[issue #248] **断面ビューポートの中身が、図面で最初に作った 1 枚にしか入らないように見える**
//	のは何が順序に効いているのかを切り分ける。
//
//	これまでに分かっていること:
//	  * #202 は場面（デザインレイヤ 3 枚＋箱）を**最初に 1 度だけ**作り、その後に 16 枚の断面を
//	    同じ用紙へ作った——どれも期待どおりの中身が入った。
//	  * #245 は c0（場面 → 用紙 → 断面）の後、**腕ごとに場面を作り直して**断面を作った
//	    （c1）——c0 と同じ関数・同じ手順なのに、後の 4 枚はすべて空だった。
//	  * 違いの候補: (1) 先に作った断面の後で**新しく作ったデザインレイヤ**は映らない、
//	    (2) 新しい場面のレイヤを作るとき**アクティブなのがシートレイヤ**（前の腕の用紙）だった、
//	    (3) `CreateViewport` 系は作ったビューポートを**選択したまま**残す（#249）。
//
//	腕（1 本の新規図面の中で、この順に作る）:
//	  A  場面 1 → 用紙 1 → 断面（対照。中身が入るはず）
//	  B  場面 1 のまま、用紙 1 へもう 1 枚（#202 と同じ。入るはず）
//	  C  場面 2 を作る（**アクティブは用紙 1 のまま**）→ 用紙 2 → 断面（#245 の再現。空なら再現）
//	  D  用紙 2 へ、**場面 1** を映す断面（C の後でも古いレイヤなら入るか）
//	  E  用紙 1 へ、**場面 2** を映す断面（C の空は用紙／断面の側か、場面 2 のレイヤの側か）
//	  F  `SetCurrentLayer(場面 1 のレイヤ)` してから場面 3 → 用紙 3 → 断面（候補 2）
//	  G  `DeselectAll()` だけしてから場面 4 → 用紙 4 → 断面（候補 3。アクティブは用紙 3 のまま）
//	腕ごとに、作る前のアクティブレイヤ・選択の有無と、場面のレイヤの性質（表示・縮尺・高さ）を
//	書き、断面のキャッシュ群（3/4/5/6/7/15）の件数と中身の外接を書く。
//
//	新規の空図面で走らせる（ブリッジの既定）。**同じ図面のままもう一度走らせる**（new_document=false）と、
//	VW へ戻った後のキャッシュ群を名前（svo-A … svo-G）で引き直して読み、更新し直してもう一度読む。
//

#include "Probe.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
	const double kSvoScale = 100.0;
	const double kSvoLineY = 2500.0;
	const double kSvoLineX0 = 0.0;
	const double kSvoLineX1 = 3000.0;
	const double kSvoBoxX0 = 500.0;
	const double kSvoBoxX1 = 2500.0;
	const double kSvoPt3Off = 9000.0;
	const double kSvoStartH = -1000.0;
	const double kSvoEndH = 9000.0;

	std::string SvoNum(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.1f", v);
		return std::string(buf);
	}

	std::string SvoName(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		TXString name;
		gSDK->GetObjectName(h, name);
		return std::string(static_cast<const char*>(name));
	}

	bool SvoRectIsEmpty(const WorldRect& r)
	{
		return static_cast<double>(r.left) > static_cast<double>(r.right) ||
			   static_cast<double>(r.bottom) > static_cast<double>(r.top);
	}

	std::string SvoRectText(const WorldRect& r)
	{
		if (SvoRectIsEmpty(r))
			return "【空】";
		return "x " + SvoNum(static_cast<double>(r.left)) + "〜" +
			   SvoNum(static_cast<double>(r.right)) + " / y " +
			   SvoNum(static_cast<double>(r.bottom)) + "〜" + SvoNum(static_cast<double>(r.top));
	}

	void SvoWriteBool(MCObjectHandle h, short selector, bool value)
	{
		TVariableBlock block(static_cast<Boolean>(value));
		gSDK->SetObjectVariable(h, selector, block);
	}

	std::string SvoReadShortVar(MCObjectHandle h, short selector)
	{
		Sint16 scratch = 0;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return "×";
		Sint16 value = 0;
		if (!block.GetSint16(value))
			return "?";
		return std::to_string((int)value);
	}

	std::string SvoReadRealVar(MCObjectHandle h, short selector)
	{
		double scratch = 0.0;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return "×";
		double value = 0.0;
		if (!block.GetReal64(value))
			return "?";
		return SvoNum(value);
	}

	MCObjectHandle SvoMakeBox(double y0, double y1, double baseZ, double thick)
	{
		VWFC::Math::VWPolygon2D base;
		base.AddVertex(kSvoBoxX0, y0);
		base.AddVertex(kSvoBoxX1, y0);
		base.AddVertex(kSvoBoxX1, y1);
		base.AddVertex(kSvoBoxX0, y1);
		base.SetClosed(true);
		VWFC::VWObjects::VWExtrudeObj extrude(base, baseZ, thick);
		return extrude;
	}

	int SvoSelectedCount()
	{
		int count = 0;
		int* countPtr = &count;
		gSDK->ForEachLayerN(
			[countPtr](MCObjectHandle layer)
			{
				for (MCObjectHandle h = gSDK->FirstMemberObj(layer); h != nullptr;
					 h = gSDK->NextObject(h))
					if (gSDK->IsSelected(h))
						++*countPtr;
			});
		return count;
	}

	std::string SvoLayerText(MCObjectHandle layer)
	{
		double scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return "\"" + SvoName(layer) + "\" type=" + SvoReadShortVar(layer, ovLayerType) +
			   " vis=" + SvoReadShortVar(layer, ovLayerVisibility) + " scale=" + SvoNum(scale) +
			   " elev=" + SvoReadRealVar(layer, ovLayerHeightInCurrUnits) +
			   " thick=" + SvoReadRealVar(layer, ovLayerThicknessInCurrUnits);
	}

	// 切り分けのスイッチ。一時ディレクトリの VwSdkProbes-section-vp-order-mode.txt に語を
	// 空白区切りで書く: nostate（作る前後のアクティブレイヤ・選択の数を読まない）／nolayer
	// （場面のレイヤの性質を読まない）／noparent（箱の親を読まない）／withx（切断面を切る
	// X 箱を映さないレイヤへ置く）／bounds（箱を作った直後に外接を読む）／reset（箱を
	// ResetObject する）。
	std::string& SvoMode()
	{
		static std::string sMode;
		return sMode;
	}

	bool SvoOff(const char* word)
	{
		return (" " + SvoMode() + " ").find(std::string(" ") + word + " ") != std::string::npos;
	}

	void SvoLoadMode()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		std::ifstream in((dir / "VwSdkProbes-section-vp-order-mode.txt").string());
		std::string line;
		std::getline(in, line);
		SvoMode() = line;
	}

	void SvoState(vwprobe::Report& probe, const std::string& when)
	{
		if (SvoOff("nostate"))
			return;
		probe.log("  [" + when + "] アクティブ " + SvoLayerText(gSDK->GetActiveLayer()) +
				  " / 選択 " + std::to_string(SvoSelectedCount()) + " 個");
	}

	// キャッシュ群（3/4/5/6/7/15）の件数と、中身（群 4 以外）の外接。
	std::string SvoGroups(MCObjectHandle vp)
	{
		const short groups[] = {3, 4, 5, 6, 7, 15};
		std::string line;
		bool hasContent = false;
		WorldRect content;
		for (short g : groups)
		{
			line += " " + std::to_string(g) + "=";
			MCObjectHandle group = gSDK->GetViewportGroup(vp, g);
			if (group == nullptr)
			{
				line += "nil";
				continue;
			}
			int n = 0;
			for (MCObjectHandle h = gSDK->FirstMemberObj(group); h != nullptr;
				 h = gSDK->NextObject(h))
				++n;
			line += std::to_string(n);
			WorldRect r;
			if (g == 4 || !gSDK->GetObjectBounds(group, r) || SvoRectIsEmpty(r))
				continue;
			if (!hasContent)
				content = r;
			else
				content.Unite(r);
			hasContent = true;
		}
		return std::string(hasContent ? "中身あり" : "空") + " / 群" + line + " / 中身 " +
			   (hasContent ? SvoRectText(content) : std::string("無し"));
	}

	struct SvoScene
	{
		MCObjectHandle layerS = nullptr;
		MCObjectHandle layerN = nullptr;
	};

	// 場面: S 箱（−Y 側・Z 0〜3000）と N 箱（+Y 側・Z 5000〜8000）を別々のデザインレイヤへ。
	bool SvoMakeScene(vwprobe::Report& probe, const std::string& prefix, SvoScene& sc)
	{
		SvoState(probe, prefix + " を作る前");
		sc.layerS =
			gSDK->CreateLayer(TXString((prefix + "-S").c_str()), static_cast<short>(kLayerDesign));
		sc.layerN =
			gSDK->CreateLayer(TXString((prefix + "-N").c_str()), static_cast<short>(kLayerDesign));
		if (sc.layerS == nullptr || sc.layerN == nullptr)
		{
			probe.fail(prefix + ": CreateLayer が nil");
			return false;
		}
		gSDK->SetCurrentLayer(sc.layerS);
		MCObjectHandle bs = SvoMakeBox(-3000.0, -2000.0, 0.0, 3000.0);
		gSDK->SetCurrentLayer(sc.layerN);
		MCObjectHandle bn = SvoMakeBox(7000.0, 8000.0, 5000.0, 3000.0);
		if (SvoOff("bounds"))
		{
			// #202 と #245 の c0 は、箱を作った直後に外接を読んでいた（c1 は読んでいない）。
			WorldRect rs, rn;
			const bool okS = gSDK->GetObjectBounds(bs, rs) != 0;
			const bool okN = gSDK->GetObjectBounds(bn, rn) != 0;
			probe.log("  " + prefix + " 箱の外接を読んだ S " + (okS ? SvoRectText(rs) : "×") +
					  " N " + (okN ? SvoRectText(rn) : "×"));
		}
		if (SvoOff("reset"))
		{
			gSDK->ResetObject(bs);
			gSDK->ResetObject(bn);
			probe.log("  " + prefix + " 箱を ResetObject した");
		}
		if (SvoOff("withx"))
		{
			// #202 / #245 の c0 と同じく、切断面を切る X 箱を別のレイヤへ置く（断面には映さない）。
			MCObjectHandle layerX = gSDK->CreateLayer(TXString((prefix + "-X").c_str()),
													  static_cast<short>(kLayerDesign));
			gSDK->SetCurrentLayer(layerX);
			MCObjectHandle bx = SvoMakeBox(2000.0, 3000.0, 0.0, 3000.0);
			probe.log("  " + prefix + " X 箱（映さない）" +
					  std::string(bx != nullptr ? "あり" : "作れず"));
		}
		if (!SvoOff("noparent"))
			probe.log("  " + prefix + " S 箱の親 \"" + SvoName(gSDK->ParentObject(bs)) +
					  "\" N 箱の親 \"" + SvoName(gSDK->ParentObject(bn)) + "\"");
		if (!SvoOff("nolayer"))
		{
			probe.log("    S レイヤ " + SvoLayerText(sc.layerS));
			probe.log("    N レイヤ " + SvoLayerText(sc.layerN));
		}
		return bs != nullptr && bn != nullptr;
	}

	MCObjectHandle SvoMakeSheet(vwprobe::Report& probe, const std::string& name)
	{
		MCObjectHandle sheet =
			gSDK->CreateLayer(TXString(name.c_str()), static_cast<short>(kLayerSheet));
		if (sheet == nullptr)
			probe.fail(name + ": シートを作れない");
		return sheet;
	}

	// 断面 1 枚（向き +x・pt3 は −Y 側・(1064,1065)=(F,T)＝S と N の両方が出るはず）。
	void SvoSection(vwprobe::Report& probe, const std::string& arm, MCObjectHandle sheet,
					const SvoScene& show)
	{
		SvoState(probe, arm + " 断面を作る前");
		const WorldPt pt1(kSvoLineX0, kSvoLineY);
		const WorldPt pt2(kSvoLineX1, kSvoLineY);
		const WorldPt pt3((kSvoLineX0 + kSvoLineX1) * 0.5, kSvoLineY - kSvoPt3Off);
		MCObjectHandle vp =
			gSDK->CreateSectionViewport(pt1, pt2, pt3, 0.0, kSvoStartH, kSvoEndH, sheet);
		if (vp == nullptr)
		{
			probe.log("  " + arm + " → CreateSectionViewport が nil");
			return;
		}
		const std::vector<MCObjectHandle> shown = {show.layerS, show.layerN};
		const std::vector<MCObjectHandle>* shownPtr = &shown;
		gSDK->ForEachLayerN(
			[vp, shownPtr](MCObjectHandle layer)
			{
				bool visible = false;
				for (MCObjectHandle s : *shownPtr)
					visible = visible || s == layer;
				gSDK->SetViewportLayerVisibility(vp, layer, visible ? 0 : -1);
			});
		gSDK->ForEachClass(
			true, [vp](MCObjectHandle cls)
			{ gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0); });
		VWFC::VWObjects::VWViewportObj vpObj(vp);
		vpObj.SetRenderType(renderFinalHiddenLine);
		SvoWriteBool(vp, ovSectionViewportDisplayObjectsBeyondCutPlane, false);
		SvoWriteBool(vp, ovSectionViewportDisplayObjectsBeforeCutPlane, true);
		SvoWriteBool(vp, ovViewportDisplayPlanar, false);
		TVariableBlock scaleBlock(kSvoScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleBlock);
		gSDK->UpdateViewport(vp);
		// 名前は UpdateViewport の後で付ける（#245）。2 回目に名前で引き直して読む。
		gSDK->SetObjectName(vp, TXString(("svo-" + arm).c_str()));
		probe.log("  ★" + arm + " 映す=\"" + SvoName(show.layerS) + "\"+\"" + SvoName(show.layerN) +
				  "\" 用紙=\"" + SvoName(sheet) + "\" → " + SvoGroups(vp));
		SvoState(probe, arm + " 断面を作った後");
	}

	const char* const kSvoArms[] = {"A", "B", "C", "D", "E", "F", "G"};

	// 2 回目: 1 回目の図面のまま走らせると、VW へ戻った後のキャッシュ群を名前で引き直して読む。
	// 続けて UpdateViewport し直して、もう一度読む。
	bool SvoRemeasure(vwprobe::Report& probe)
	{
		if (gSDK->GetNamedObject("svo-A") == nullptr)
			return false;
		probe.log("■ 2 回目: VW へ戻った後に、1 回目の断面を名前で引き直して読む");
		for (const char* arm : kSvoArms)
		{
			MCObjectHandle vp = gSDK->GetNamedObject(TXString((std::string("svo-") + arm).c_str()));
			probe.log(std::string("  ") + arm + " 戻った後 → " +
					  (vp == nullptr ? std::string("（見つからない）") : SvoGroups(vp)));
		}
		probe.log("■ 2 回目: UpdateViewport し直して読む");
		for (const char* arm : kSvoArms)
		{
			MCObjectHandle vp = gSDK->GetNamedObject(TXString((std::string("svo-") + arm).c_str()));
			if (vp == nullptr)
				continue;
			gSDK->UpdateViewport(vp);
			probe.log(std::string("  ") + arm + " 更新し直した → " + SvoGroups(vp));
		}
		return true;
	}
} // namespace

VW_PROBE("section-vp-order", "断面VPの順序", "後から作った場面の断面が空になる条件を切り分ける")
{
	if (SvoRemeasure(probe))
		return;
	SvoLoadMode();
	probe.log("切り分けのスイッチ: \"" + SvoMode() + "\"");
	probe.log("■ A 場面 1 → 用紙 1 → 断面（対照）");
	SvoScene s1;
	if (!SvoMakeScene(probe, "s1", s1))
		return;
	MCObjectHandle sheet1 = SvoMakeSheet(probe, "s1-用紙");
	if (sheet1 == nullptr)
		return;
	SvoSection(probe, "A", sheet1, s1);

	probe.log("■ B 用紙 1 へもう 1 枚（場面 1）");
	SvoSection(probe, "B", sheet1, s1);

	probe.log("■ C 場面 2（アクティブはそのまま）→ 用紙 2 → 断面（#245 の再現）");
	SvoScene s2;
	if (!SvoMakeScene(probe, "s2", s2))
		return;
	MCObjectHandle sheet2 = SvoMakeSheet(probe, "s2-用紙");
	if (sheet2 == nullptr)
		return;
	SvoSection(probe, "C", sheet2, s2);

	probe.log("■ D 用紙 2 へ、場面 1 を映す断面");
	SvoSection(probe, "D", sheet2, s1);

	probe.log("■ E 用紙 1 へ、場面 2 を映す断面");
	SvoSection(probe, "E", sheet1, s2);

	probe.log("■ F SetCurrentLayer(場面 1 の S) してから場面 3 → 用紙 3 → 断面");
	gSDK->SetCurrentLayer(s1.layerS);
	SvoScene s3;
	if (!SvoMakeScene(probe, "s3", s3))
		return;
	MCObjectHandle sheet3 = SvoMakeSheet(probe, "s3-用紙");
	if (sheet3 == nullptr)
		return;
	SvoSection(probe, "F", sheet3, s3);

	probe.log("■ G DeselectAll だけしてから場面 4 → 用紙 4 → 断面");
	gSDK->DeselectAll();
	SvoScene s4;
	if (!SvoMakeScene(probe, "s4", s4))
		return;
	MCObjectHandle sheet4 = SvoMakeSheet(probe, "s4-用紙");
	if (sheet4 == nullptr)
		return;
	SvoSection(probe, "G", sheet4, s4);

	probe.log("■ 終わり。図面は壊れたままでよい（新規の空図面で走らせる前提）");
}
