//
//	probes/runtime/layer-objects/probe.cpp
//
//	[issue #282] レイヤの中のオブジェクトの列挙と数え方。見本の図面を自分で作り、辿り方ごとに
//	数と並びを出す:
//
//	  1  FirstMemberObj → NextObject の順と、作った順・LastMemberObj → PrevObject の順
//	  2  列に混ざる図形でないもの（終端 0・undo の置き石 90 など）
//	  3  入れ子（グループ・シンボル・PIO・壁の建具）。ForEachObjectN の traverseWhat ごとの数
//	  4  取り消した図形（2 回目の実行で読む。下記）
//	  5  種別の番号（名前を引く口は SDK に無い【ヘッダ根拠】ので番号だけ出す）
//	  6  名前・クラス・外形（縮尺の違うレイヤ・回転・文字・3D）
//	  7  シートレイヤを辿ったときに何が出るか
//	  8  OS タイマー（SetTimer）の刻みの中で辿ったときの所要（Windows。図形 3000）
//
//	**2 回走らせる。** 1 回目は新規の空図面で走らせると見本を作って全部を測る。そのあと人が
//	（あるいは computer use で）LO-A の上の図形を消したり描いて取り消したりしてから 2 回目を
//	走らせると、見本は作らずに辿り直すだけ（各ノードの印を全部出す）。見本があるかは
//	デザインレイヤ「LO-A」の有無で決める。
//
//	プローブは undo イベントを自分では開かない（CLAUDE.md）。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWParametricObj.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#if GS_WIN
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#endif

namespace
{
	const char* const kLoA = "LO-A";	   // 見本（縮尺 1:1）
	const char* const kLoB = "LO-B";	   // 同じ矩形を縮尺 1:50 で
	const char* const kLoMany = "LO-多";  // 刻みの中の所要を測る 3000 個
	const char* const kLoSheet = "LO-S";  // シートレイヤ（ビューポート）
	const int kManyCount = 3000;

	std::string LoStr(const TXString& s)
	{
		return static_cast<const char*>(s);
	}

	std::string LoNum(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof buf, "%.3f", v);
		return buf;
	}

	std::string LoPtr(const void* p)
	{
		char buf[32];
		std::snprintf(buf, sizeof buf, "%p", p);
		return buf;
	}

	short LoType(MCObjectHandle h)
	{
		return h == nullptr ? -1 : gSDK->GetObjectTypeN(h);
	}

	// 図形の下にある容れ物（レイヤ＝31）まで ParentObject を辿る。
	MCObjectHandle LoLayerOf(MCObjectHandle h)
	{
		MCObjectHandle p = h;
		for (int guard = 0; p != nullptr && guard < 64; ++guard)
		{
			if (gSDK->GetObjectTypeN(p) == kLayerNode)
				return p;
			p = gSDK->ParentObject(p);
		}
		return nullptr;
	}

	std::string LoLayerName(MCObjectHandle layer)
	{
		if (layer == nullptr)
			return "(レイヤ外)";
		TXString n;
		gSDK->GetObjectName(layer, n);
		return LoStr(n);
	}

	// 1 ノードの読み（名前・クラス・外形・親・選択・PIO 名・3D の外形）。
	std::string LoDescribe(MCObjectHandle h)
	{
		const short t = LoType(h);
		std::string s = "type=" + std::to_string(t);
		if (t == kTermNode)
			return s + "（終端）";
		TXString name;
		gSDK->GetObjectName(h, name);
		s += " name=\"" + LoStr(name) + "\"";
		TXString cls;
		gSDK->ClassIDToName(gSDK->GetObjectClass(h), cls);
		s += " class=\"" + LoStr(cls) + "\"";
		WorldRect r;
		const Boolean ok = gSDK->GetObjectBounds(h, r);
		s += " bounds" + std::string(ok ? "" : "(false)") + "=[" + LoNum(r.Left()) + "," +
			 LoNum(r.Bottom()) + " → " + LoNum(r.Right()) + "," + LoNum(r.Top()) + "]";
		MCObjectHandle parent = gSDK->ParentObject(h);
		s += " parent=" + std::to_string(LoType(parent));
		if (gSDK->IsSelected(h))
			s += " selected";
		if (t == kParametricNode)
		{
			VWFC::VWObjects::VWParametricObj pio(h);
			s += " pio=\"" + LoStr(pio.GetParametricName()) + "\"";
		}
		if (t == kExtrudeNode || t == kParametricNode || t == kWallNode || t == kSymbolNode)
		{
			WorldCube c;
			gSDK->GetObjectCube(h, c);
			s += " cube.z=[" + LoNum(c.MinZ()) + " → " + LoNum(c.MaxZ()) + "]";
		}
		int kids = 0;
		for (MCObjectHandle k = gSDK->FirstMemberObj(h); k != nullptr && kids < 10000;
			 k = gSDK->NextObject(k))
			++kids;
		if (kids > 0)
			s += " members=" + std::to_string(kids);
		return s;
	}

	// 型ごとの数（"型:数 型:数 …"）。
	std::string LoHisto(const std::map<short, int>& m)
	{
		std::string s;
		for (const auto& kv : m)
			s += (s.empty() ? "" : " ") + std::to_string(kv.first) + ":" + std::to_string(kv.second);
		return s.empty() ? "(なし)" : s;
	}

	// FirstMemberObj → NextObject で辿る。full なら 1 ノードずつ書く。
	std::vector<MCObjectHandle> LoWalk(vwprobe::Report& probe, MCObjectHandle layer, bool full)
	{
		std::vector<MCObjectHandle> seq;
		std::map<short, int> histo;
		for (MCObjectHandle h = gSDK->FirstMemberObj(layer); h != nullptr && seq.size() < 100000;
			 h = gSDK->NextObject(h))
		{
			seq.push_back(h);
			histo[LoType(h)]++;
		}
		probe.log("  walk(First→Next) " + LoLayerName(layer) + ": " + std::to_string(seq.size()) +
				  " ノード  型の内訳 " + LoHisto(histo));
		if (full)
		{
			for (size_t i = 0; i < seq.size(); ++i)
				probe.log("    [" + std::to_string(i) + "] " + LoDescribe(seq[i]));
		}
		return seq;
	}

	// Last → Prev の順が First → Next のちょうど逆か。
	void LoReverseCheck(vwprobe::Report& probe, MCObjectHandle layer,
						const std::vector<MCObjectHandle>& fwd)
	{
		std::vector<MCObjectHandle> rev;
		for (MCObjectHandle h = gSDK->LastMemberObj(layer); h != nullptr && rev.size() < 100000;
			 h = gSDK->PrevObject(h))
			rev.push_back(h);
		bool mirror = rev.size() == fwd.size();
		for (size_t i = 0; mirror && i < rev.size(); ++i)
			mirror = rev[i] == fwd[fwd.size() - 1 - i];
		std::string head = rev.empty() ? "-" : std::to_string(LoType(rev.front()));
		probe.log("  walk(Last→Prev) " + LoLayerName(layer) + ": " + std::to_string(rev.size()) +
				  " ノード  先頭の型=" + head + "  First→Next の逆と一致=" + (mirror ? "yes" : "no"));
	}

	struct LoTraverse
	{
		const char* label;
		short what;
	};

	const LoTraverse kTraverses[] = {
		{"allSelected", allSelected},
		{"allEditable", allEditable},
		{"allVisible", allVisible},
		{"allDrawing", allDrawing},
		{"allObjects", allObjects},
		{"allObjectsAndResources", allObjectsAndResources},
		{"allDrawing+descendIntoGroups", allDrawing + descendIntoGroups},
		{"allDrawing+descendIntoAll", allDrawing + descendIntoAll},
		{"allObjects+descendIntoGroups", allObjects + descendIntoGroups},
		{"allObjects+descendIntoAll", allObjects + descendIntoAll},
		{"allObjects+descendIntoAll+Viewports", allObjects + descendIntoAll + descendIntoViewports},
		{"allObjects+descendIntoAuxLists", allObjects + descendIntoAuxLists},
		{"allObjects+descendIntoAll+AuxLists", allObjects + descendIntoAll + descendIntoAuxLists},
	};

	// ForEachObjectN をレイヤごとの数・型の内訳で出す。順序は layerA の top レベルと比べる。
	void LoForEach(vwprobe::Report& probe, MCObjectHandle layerA,
				   const std::vector<MCObjectHandle>& walkA)
	{
		for (const LoTraverse& tr : kTraverses)
		{
			std::map<std::string, std::map<short, int>> perLayer;
			std::vector<MCObjectHandle> topA; // layerA の直下のものだけ、出てきた順
			int total = 0;
			const auto t0 = std::chrono::steady_clock::now();
			gSDK->ForEachObjectN(tr.what, [&](MCObjectHandle h) {
				++total;
				MCObjectHandle layer = LoLayerOf(h);
				perLayer[LoLayerName(layer)][LoType(h)]++;
				if (gSDK->ParentObject(h) == layerA)
					topA.push_back(h);
			});
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
								std::chrono::steady_clock::now() - t0)
								.count();
			probe.log("  ForEachObjectN(" + std::string(tr.label) + "=" + std::to_string(tr.what) +
					  "): 計 " + std::to_string(total) + "  " + std::to_string(us) + " µs");
			for (const auto& kv : perLayer)
			{
				int n = 0;
				for (const auto& tv : kv.second)
					n += tv.second;
				probe.log("    " + kv.first + ": " + std::to_string(n) + "  " + LoHisto(kv.second));
			}
			// 直下の並びが walk（終端・置き石を除く）と同じ順か、逆順か。
			std::vector<MCObjectHandle> walkObjs;
			for (MCObjectHandle h : walkA)
				if (LoType(h) != kTermNode)
					walkObjs.push_back(h);
			bool same = topA.size() == walkObjs.size();
			bool reverse = same;
			for (size_t i = 0; i < topA.size() && (same || reverse); ++i)
			{
				if (i >= walkObjs.size())
				{
					same = reverse = false;
					break;
				}
				same = same && topA[i] == walkObjs[i];
				reverse = reverse && topA[i] == walkObjs[walkObjs.size() - 1 - i];
			}
			std::string missing;
			for (MCObjectHandle h : walkObjs)
			{
				bool found = false;
				for (MCObjectHandle k : topA)
					found = found || k == h;
				if (!found)
					missing += " " + std::to_string(LoType(h)) + "@" + LoPtr(h);
			}
			probe.log("    " + std::string(kLoA) + " 直下: " + std::to_string(topA.size()) +
					  "（walk から終端を除いた数 " + std::to_string(walkObjs.size()) + "）  同順=" +
					  (same ? "yes" : "no") + " 逆順=" + (reverse ? "yes" : "no") +
					  (missing.empty() ? "" : "  walk にあって出てこない:" + missing));
		}
	}

	// 作ったものを指定のレイヤへ置く（作成はアクティブなレイヤに入るので、違えば移す）。
	MCObjectHandle LoPut(vwprobe::Report& probe, const char* label, MCObjectHandle h,
						 MCObjectHandle layer)
	{
		if (h == nullptr)
		{
			probe.log(std::string("  作れなかった: ") + label);
			return nullptr;
		}
		if (gSDK->ParentObject(h) != layer)
		{
			const bool moved = gSDK->AddObjectToContainer(h, layer);
			probe.log(std::string("  ") + label + " を " + LoLayerName(layer) + " へ移した: " +
					  (moved ? "ok" : "失敗"));
		}
		return h;
	}

	MCObjectHandle LoRect(double l, double b, double r, double t)
	{
		return gSDK->CreateRectangle(WorldRect(WorldPt(l, t), WorldPt(r, b)));
	}

	void LoDumpLayers(vwprobe::Report& probe)
	{
		probe.log("--- レイヤの並び（GetDrawingHeader の子） ---");
		MCObjectHandle header = gSDK->GetDrawingHeader();
		int i = 0;
		for (MCObjectHandle l = gSDK->FirstMemberObj(header); l != nullptr && i < 1000;
			 l = gSDK->NextObject(l), ++i)
		{
			if (LoType(l) != kLayerNode)
				continue;
			double_gs scale = 0;
			gSDK->GetLayerScaleN(l, scale);
			probe.log("  [" + std::to_string(i) + "] " + LoLayerName(l) + "  scale=" + LoNum(scale) +
					  (l == gSDK->GetActiveLayer() ? "  (アクティブ)" : ""));
		}
	}

#if GS_WIN
	// 刻みの中で辿る（SetTimer の刻みからだけ呼ばれる）。
	struct LoTick
	{
		MCObjectHandle layer = nullptr;
		bool done = false;
		long long walkUs = -1;
		long long forEachUs = -1;
		int walkN = 0;
		int forEachN = 0;
	};
	LoTick gLoTick;

	void CALLBACK LoTimerProc(HWND, UINT, UINT_PTR id, DWORD)
	{
		::KillTimer(nullptr, id);
		if (gLoTick.done)
			return;
		auto t0 = std::chrono::steady_clock::now();
		int n = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(gLoTick.layer); h != nullptr;
			 h = gSDK->NextObject(h))
		{
			TXString name;
			gSDK->GetObjectName(h, name);
			WorldRect r;
			gSDK->GetObjectBounds(h, r);
			gSDK->GetObjectClass(h);
			++n;
		}
		auto t1 = std::chrono::steady_clock::now();
		int m = 0;
		gSDK->ForEachObjectN(allObjects, [&](MCObjectHandle) { ++m; });
		auto t2 = std::chrono::steady_clock::now();
		gLoTick.walkUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
		gLoTick.forEachUs = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
		gLoTick.walkN = n;
		gLoTick.forEachN = m;
		gLoTick.done = true;
	}
#endif

	void LoTiming(vwprobe::Report& probe, MCObjectHandle many)
	{
		probe.log("--- 8 刻みの中の所要（" + std::string(kLoMany) + " に " +
				  std::to_string(kManyCount) + " 個） ---");
		// 刻みの外（対照）
		for (int rep = 0; rep < 3; ++rep)
		{
			auto t0 = std::chrono::steady_clock::now();
			int n = 0;
			for (MCObjectHandle h = gSDK->FirstMemberObj(many); h != nullptr; h = gSDK->NextObject(h))
			{
				TXString name;
				gSDK->GetObjectName(h, name);
				WorldRect r;
				gSDK->GetObjectBounds(h, r);
				gSDK->GetObjectClass(h);
				++n;
			}
			auto us = std::chrono::duration_cast<std::chrono::microseconds>(
						  std::chrono::steady_clock::now() - t0)
						  .count();
			probe.log("  刻みの外 walk＋名前・外形・クラス: " + std::to_string(n) + " ノード " +
					  std::to_string(us) + " µs");
		}
#if GS_WIN
		for (int rep = 0; rep < 3; ++rep)
		{
			gLoTick = LoTick();
			gLoTick.layer = many;
			const UINT_PTR timer = ::SetTimer(nullptr, 0, 20, &LoTimerProc);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			MSG msg;
			while (!gLoTick.done && std::chrono::steady_clock::now() < deadline)
			{
				while (::PeekMessageW(&msg, nullptr, WM_TIMER, WM_TIMER, PM_REMOVE))
					::DispatchMessageW(&msg);
				::Sleep(1);
			}
			if (!gLoTick.done)
			{
				::KillTimer(nullptr, timer);
				probe.log("  刻みが 10 秒来なかった");
				continue;
			}
			probe.log("  刻みの中 walk＋名前・外形・クラス: " + std::to_string(gLoTick.walkN) +
					  " ノード " + std::to_string(gLoTick.walkUs) + " µs / ForEachObjectN(allObjects): " +
					  std::to_string(gLoTick.forEachN) + " 個 " + std::to_string(gLoTick.forEachUs) +
					  " µs");
		}
#else
		probe.log("  （刻みの中の測定は Windows だけ）");
#endif
	}

	void LoBuild(vwprobe::Report& probe, MCObjectHandle& layerA, MCObjectHandle& layerB,
				 MCObjectHandle& many, MCObjectHandle& sheet)
	{
		probe.log("--- 見本を作る ---");
		layerA = gSDK->CreateLayerN(kLoA, 1.0);
		layerB = gSDK->CreateLayerN(kLoB, 50.0);
		double_gs sb = 0;
		gSDK->GetLayerScaleN(layerB, sb);
		if (std::fabs(sb - 50.0) > 1e-9)
		{
			gSDK->SetLayerScaleN(layerB, 50.0);
			gSDK->GetLayerScaleN(layerB, sb);
		}
		probe.log("  " + std::string(kLoB) + " の縮尺（読み戻し）= " + LoNum(sb));
		many = gSDK->CreateLayerN(kLoMany, 1.0);
		sheet = gSDK->CreateLayer(kLoSheet, kLayerSheet);
		probe.log("  レイヤ: A=" + LoPtr(layerA) + " B=" + LoPtr(layerB) + " 多=" + LoPtr(many) +
				  " S=" + LoPtr(sheet) + "  作った直後のアクティブ=" +
				  LoLayerName(gSDK->GetActiveLayer()));

		const InternalIndex cls = gSDK->AddClass("LO-クラス");

		// 作った順（重ね順の確かめ）。名前は付けたり付けなかったり。
		struct Made
		{
			const char* label;
			MCObjectHandle h;
		};
		std::vector<Made> made;
		auto add = [&](const char* label, MCObjectHandle h, const char* name) {
			h = LoPut(probe, label, h, layerA);
			if (h != nullptr && name != nullptr)
				gSDK->SetObjectName(h, name);
			made.push_back({label, h});
			return h;
		};

		add("01 直線（名前あり）", gSDK->CreateLine(WorldPt(0, 0), WorldPt(1000, 0)), "LO-直線");
		MCObjectHandle rect = add("02 矩形（名前なし・クラス LO-クラス）", LoRect(0, 100, 1000, 600),
								  nullptr);
		if (rect != nullptr)
			gSDK->SetObjectClass(rect, cls);
		const double kPi = 3.14159265358979323846;
		add("03 回転した矩形 30°（原点 1500,100 ・1000×500）",
			gSDK->CreateRectangleN(WorldPt(1500, 100), Vector2(std::cos(kPi / 6), std::sin(kPi / 6)),
								   1000, 500),
			"LO-回転");
		add("04 楕円", gSDK->CreateOval(WorldRect(WorldPt(2500, 600), WorldPt(3500, 100))), nullptr);
		add("05 文字", gSDK->CreateTextBlock("文字の外形", WorldPt(0, -500), false, 0), "LO-文字");

		// 06 グループ（直線 2 本＋入れ子のグループに矩形 1 つ）
		MCObjectHandle inner = gSDK->CreateGroup();
		if (inner != nullptr)
			gSDK->AddObjectToContainer(LoRect(4000, 100, 4300, 400), inner);
		MCObjectHandle group = gSDK->CreateGroup();
		if (group != nullptr)
		{
			gSDK->AddObjectToContainer(gSDK->CreateLine(WorldPt(4000, 0), WorldPt(5000, 0)), group);
			gSDK->AddObjectToContainer(gSDK->CreateLine(WorldPt(4000, 600), WorldPt(5000, 600)),
									   group);
			if (inner != nullptr)
				gSDK->AddObjectToContainer(inner, group);
			gSDK->ResetObject(group);
		}
		add("06 グループ（直線 2・入れ子のグループ 1（矩形 1））", group, "LO-グループ");

		// 07 シンボル（定義に矩形 2）× 2
		TXString symName = "LO-シンボル";
		MCObjectHandle def = gSDK->CreateSymbolDefinition(symName);
		if (def != nullptr)
		{
			gSDK->AddObjectToContainer(LoRect(0, 0, 200, 200), def);
			gSDK->AddObjectToContainer(LoRect(300, 0, 500, 200), def);
			gSDK->ResetObject(def);
		}
		probe.log("  シンボル定義: " + LoPtr(def) + " name=\"" + LoStr(symName) + "\"");
		add("07a シンボル", def ? gSDK->PlaceSymbolN(def, WorldPt(6000, 0), 0) : nullptr, nullptr);
		add("07b シンボル（名前あり・45°）", def ? gSDK->PlaceSymbolN(def, WorldPt(7000, 0), 45) : nullptr,
			"LO-シンボル2");

		// 08 PIO（ドア単体）。設定ダイアログを出さないため先に定義する（Findings「Parametric Objects」）。
		gSDK->DefineCustomObject("Door", kCustomObjectPrefNever);
		gSDK->DefineCustomObject("Window", kCustomObjectPrefNever);
		add("08 PIO ドア", gSDK->CreateCustomObject("Door", WorldPt(8000, 0), 0), nullptr);

		// 09 壁＋窓（AddObjectToContainer で壁へ入れてみる）
		MCObjectHandle wall = add("09 壁", gSDK->CreateWall(WorldPt(0, 2000), WorldPt(5000, 2000), 200),
								  "LO-壁");
		MCObjectHandle win = gSDK->CreateCustomObject("Window", WorldPt(2500, 2000), 0);
		if (wall != nullptr && win != nullptr)
		{
			const bool ok = gSDK->AddObjectToContainer(win, wall);
			gSDK->ResetObject(wall);
			probe.log(std::string("  窓を壁へ AddObjectToContainer: ") + (ok ? "ok" : "失敗") +
					  "  窓の親の型=" + std::to_string(LoType(gSDK->ParentObject(win))));
			if (gSDK->ParentObject(win) != wall)
				made.push_back({"09b 窓（壁に入らなかった）", win});
		}

		// 10 押し出し（矩形を入れる）
		MCObjectHandle ex = gSDK->CreateExtrude(0, 1000);
		if (ex != nullptr)
		{
			gSDK->AddObjectToContainer(LoRect(6000, 2000, 7000, 3000), ex);
			gSDK->ResetObject(ex);
		}
		add("10 押し出し（1000×1000×高さ 1000）", ex, nullptr);

		// 11 同じ矩形を LO-B（1:50）へ
		MCObjectHandle rb = LoPut(probe, "B 矩形", LoRect(0, 100, 1000, 600), layerB);
		if (rb != nullptr)
			gSDK->SetObjectName(rb, "LO-B矩形");

		// 12 シートレイヤにビューポート（LO-A を映す）
		MCObjectHandle vp = gSDK->CreateViewport(sheet);
		if (vp != nullptr)
		{
			gSDK->SetViewportLayerVisibility(vp, layerA, 0 /* kLayerVisibilityNormal */);
			gSDK->UpdateViewport(vp);
		}
		probe.log("  ビューポート: " + LoPtr(vp) + " 親の型=" + std::to_string(LoType(gSDK->ParentObject(vp))));

		// 13 3000 個
		for (int i = 0; i < kManyCount; ++i)
		{
			const double x = (i % 60) * 200.0;
			const double y = (i / 60) * 200.0;
			MCObjectHandle h = LoRect(x, y, x + 100, y + 100);
			if (h != nullptr && gSDK->ParentObject(h) != many)
				gSDK->AddObjectToContainer(h, many);
		}

		probe.log("--- 作った順（1 の答え合わせ） ---");
		for (const Made& m : made)
			probe.log(std::string("  ") + m.label + " → " + LoPtr(m.h) + " type=" +
					  std::to_string(LoType(m.h)));
	}
} // namespace

VW_PROBE("layer-objects", "レイヤの中の図形の列挙", "見本を作り、辿り方ごとに数と並びを出す")
{
	MCObjectHandle layerA = gSDK->GetNamedLayer(kLoA);
	MCObjectHandle layerB = gSDK->GetNamedLayer(kLoB);
	MCObjectHandle many = gSDK->GetNamedLayer(kLoMany);
	MCObjectHandle sheet = gSDK->GetNamedLayer(kLoSheet);
	const bool second = layerA != nullptr;
	probe.log(second ? "見本あり → 辿り直すだけ（2 回目）" : "見本なし → 作って測る（1 回目）");

	if (!second)
		LoBuild(probe, layerA, layerB, many, sheet);
	if (layerA == nullptr)
	{
		probe.fail("LO-A が作れなかった");
		return;
	}

	LoDumpLayers(probe);

	probe.log("--- 1・2・5・6 LO-A を First→Next で（1 ノードずつ） ---");
	const std::vector<MCObjectHandle> walkA = LoWalk(probe, layerA, true);
	LoReverseCheck(probe, layerA, walkA);

	probe.log("--- 3 入れ子の中身（グループ・シンボル定義・壁・PIO の子） ---");
	for (MCObjectHandle h : walkA)
	{
		const short t = LoType(h);
		if (t == kGroupNode || t == kWallNode || t == kParametricNode || t == kExtrudeNode)
		{
			probe.log("  " + LoDescribe(h));
			for (MCObjectHandle k = gSDK->FirstMemberObj(h); k != nullptr; k = gSDK->NextObject(k))
				probe.log("      子 " + LoDescribe(k));
		}
		if (t == kSymbolNode)
		{
			MCObjectHandle def = gSDK->GetDefinition(h);
			TXString dn;
			gSDK->GetObjectName(def, dn);
			probe.log("  シンボル → 定義 \"" + LoStr(dn) + "\" type=" + std::to_string(LoType(def)) +
					  "  インスタンス自身の子の先頭の型=" +
					  std::to_string(LoType(gSDK->FirstMemberObj(h))));
		}
	}

	if (layerB != nullptr)
	{
		probe.log("--- 6 縮尺 1:50 のレイヤ（LO-A の 02 と同じ座標で作った矩形） ---");
		LoWalk(probe, layerB, true);
	}
	if (sheet != nullptr)
	{
		probe.log("--- 7 シートレイヤ ---");
		LoWalk(probe, sheet, true);
	}
	if (many != nullptr)
		LoWalk(probe, many, false);

	probe.log("--- 3・7 ForEachObjectN の traverseWhat ごと ---");
	LoForEach(probe, layerA, walkA);

	if (many != nullptr)
		LoTiming(probe, many);

	probe.log("--- 名前で引けるか（GetNamedObject） ---");
	for (const char* n : {"LO-直線", "LO-回転", "LO-グループ", "LO-壁", "LO-シンボル", "LO-B矩形"})
	{
		MCObjectHandle h = gSDK->GetNamedObject(n);
		probe.log(std::string("  ") + n + " → " + LoPtr(h) + " type=" + std::to_string(LoType(h)) +
				  " layer=" + LoLayerName(LoLayerOf(h)));
	}
}
