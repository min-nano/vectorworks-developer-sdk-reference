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
//	整形が 1 通りに決まるよう、式は 1 行に収め、ラムダは使わない（手元に clang-format が無い）。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWParametricObj.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
	// 見本（縮尺 1:1）
	const char* const kLoA = "LO-A";
	// 同じ矩形を縮尺 1:50 で
	const char* const kLoB = "LO-B";
	// 刻みの中の所要を測る 3000 個
	const char* const kLoMany = "LO-多";
	// シートレイヤ（ビューポート）
	const char* const kLoSheet = "LO-S";
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

	std::string LoInt(long long v)
	{
		return std::to_string(v);
	}

	short LoType(MCObjectHandle h)
	{
		if (h == nullptr)
			return -1;
		return gSDK->GetObjectTypeN(h);
	}

	long long LoUs(std::chrono::steady_clock::time_point t0)
	{
		const auto d = std::chrono::steady_clock::now() - t0;
		return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
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

	int LoCountMembers(MCObjectHandle h)
	{
		int kids = 0;
		MCObjectHandle k = gSDK->FirstMemberObj(h);
		while (k != nullptr && kids < 10000)
		{
			++kids;
			k = gSDK->NextObject(k);
		}
		return kids;
	}

	// 1 ノードの読み（名前・クラス・外形・親・選択・PIO 名・3D の外形）。
	std::string LoDescribe(MCObjectHandle h)
	{
		const short t = LoType(h);
		std::string s = "type=" + LoInt(t);
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
		s += ok ? " bounds=[" : " bounds(false)=[";
		s += LoNum(r.Left()) + "," + LoNum(r.Bottom()) + " → ";
		s += LoNum(r.Right()) + "," + LoNum(r.Top()) + "]";
		s += " parent=" + LoInt(LoType(gSDK->ParentObject(h)));
		if (gSDK->IsSelected(h))
			s += " selected";
		if (t == kParametricNode)
		{
			VWFC::VWObjects::VWParametricObj pio(h);
			s += " pio=\"" + LoStr(pio.GetParametricName()) + "\"";
		}
		const bool solid = t == kExtrudeNode || t == kParametricNode;
		if (solid || t == kWallNode || t == kSymbolNode)
		{
			WorldCube c;
			gSDK->GetObjectCube(h, c);
			s += " cube.z=[" + LoNum(c.MinZ()) + " → " + LoNum(c.MaxZ()) + "]";
		}
		const int kids = LoCountMembers(h);
		if (kids > 0)
			s += " members=" + LoInt(kids);
		return s;
	}

	// 型ごとの数（"型:数 型:数 …"）。
	std::string LoHisto(const std::map<short, int>& m)
	{
		std::string s;
		for (const auto& kv : m)
		{
			if (!s.empty())
				s += " ";
			s += LoInt(kv.first) + ":" + LoInt(kv.second);
		}
		if (s.empty())
			return "(なし)";
		return s;
	}

	// FirstMemberObj → NextObject で辿る。full なら 1 ノードずつ書く。
	std::vector<MCObjectHandle> LoWalk(vwprobe::Report& probe, MCObjectHandle layer, bool full)
	{
		std::vector<MCObjectHandle> seq;
		std::map<short, int> histo;
		MCObjectHandle h = gSDK->FirstMemberObj(layer);
		while (h != nullptr && seq.size() < 100000)
		{
			seq.push_back(h);
			histo[LoType(h)]++;
			h = gSDK->NextObject(h);
		}
		std::string s = "  walk(First→Next) " + LoLayerName(layer) + ": ";
		s += LoInt(seq.size()) + " ノード  型の内訳 " + LoHisto(histo);
		probe.log(s);
		if (!full)
			return seq;
		for (size_t i = 0; i < seq.size(); ++i)
			probe.log("    [" + LoInt(i) + "] " + LoDescribe(seq[i]));
		return seq;
	}

	// Last → Prev の順が First → Next のちょうど逆か。
	void LoReverseCheck(vwprobe::Report& probe, MCObjectHandle layer,
						const std::vector<MCObjectHandle>& fwd)
	{
		std::vector<MCObjectHandle> rev;
		MCObjectHandle h = gSDK->LastMemberObj(layer);
		while (h != nullptr && rev.size() < 100000)
		{
			rev.push_back(h);
			h = gSDK->PrevObject(h);
		}
		bool mirror = rev.size() == fwd.size();
		for (size_t i = 0; mirror && i < rev.size(); ++i)
			mirror = rev[i] == fwd[fwd.size() - 1 - i];
		std::string head = "-";
		if (!rev.empty())
			head = LoInt(LoType(rev.front()));
		std::string s = "  walk(Last→Prev) " + LoLayerName(layer) + ": ";
		s += LoInt(rev.size()) + " ノード  先頭の型=" + head;
		s += std::string("  First→Next の逆と一致=") + (mirror ? "yes" : "no");
		probe.log(s);
	}

	// ForEachObjectN の 1 回分の集計。
	struct LoCount
	{
		MCObjectHandle layerA = nullptr;
		int total = 0;
		std::map<std::string, std::map<short, int>> perLayer;
		// layerA の直下のものだけ、出てきた順
		std::vector<MCObjectHandle> topA;
	};

	struct LoCounter
	{
		LoCount* st;

		void operator()(MCObjectHandle h) const
		{
			st->total++;
			MCObjectHandle layer = LoLayerOf(h);
			st->perLayer[LoLayerName(layer)][LoType(h)]++;
			if (gSDK->ParentObject(h) == st->layerA)
				st->topA.push_back(h);
		}
	};

	bool LoContains(const std::vector<MCObjectHandle>& v, MCObjectHandle h)
	{
		for (MCObjectHandle k : v)
		{
			if (k == h)
				return true;
		}
		return false;
	}

	// 直下の並びが walk（終端を除く）と同じ順か、逆順か。
	void LoCompareOrder(vwprobe::Report& probe, const std::vector<MCObjectHandle>& topA,
						const std::vector<MCObjectHandle>& walkA)
	{
		std::vector<MCObjectHandle> objs;
		for (MCObjectHandle h : walkA)
		{
			if (LoType(h) != kTermNode)
				objs.push_back(h);
		}
		bool same = topA.size() == objs.size();
		bool reverse = same;
		for (size_t i = 0; same && i < topA.size(); ++i)
			same = topA[i] == objs[i];
		for (size_t i = 0; reverse && i < topA.size(); ++i)
			reverse = topA[i] == objs[objs.size() - 1 - i];
		std::string missing;
		for (MCObjectHandle h : objs)
		{
			if (!LoContains(topA, h))
				missing += " " + LoInt(LoType(h)) + "@" + LoPtr(h);
		}
		std::string s = "    " + std::string(kLoA) + " 直下: " + LoInt(topA.size());
		s += "（walk から終端を除いた数 " + LoInt(objs.size()) + "）";
		s += std::string("  同順=") + (same ? "yes" : "no");
		s += std::string(" 逆順=") + (reverse ? "yes" : "no");
		if (!missing.empty())
			s += "  walk にあって出てこない:" + missing;
		probe.log(s);
	}

	// ForEachObjectN を 1 回、レイヤごとの数・型の内訳で出す。順序は layerA の直下で比べる。
	void LoForEachOne(vwprobe::Report& probe, MCObjectHandle layerA,
					  const std::vector<MCObjectHandle>& walkA, const char* label, short what)
	{
		LoCount st;
		st.layerA = layerA;
		std::function<void(MCObjectHandle)> fn = LoCounter{&st};
		const auto t0 = std::chrono::steady_clock::now();
		gSDK->ForEachObjectN(what, fn);
		const long long us = LoUs(t0);
		std::string s = "  ForEachObjectN(" + std::string(label) + "=";
		s += LoInt(what) + "): 計 " + LoInt(st.total) + "  " + LoInt(us) + " µs";
		probe.log(s);
		for (const auto& kv : st.perLayer)
		{
			int n = 0;
			for (const auto& tv : kv.second)
				n += tv.second;
			probe.log("    " + kv.first + ": " + LoInt(n) + "  " + LoHisto(kv.second));
		}
		LoCompareOrder(probe, st.topA, walkA);
	}

	void LoForEach(vwprobe::Report& probe, MCObjectHandle a, const std::vector<MCObjectHandle>& w)
	{
		const short objAll = allObjects + descendIntoAll;
		LoForEachOne(probe, a, w, "allSelected", allSelected);
		LoForEachOne(probe, a, w, "allEditable", allEditable);
		LoForEachOne(probe, a, w, "allVisible", allVisible);
		LoForEachOne(probe, a, w, "allDrawing", allDrawing);
		LoForEachOne(probe, a, w, "allObjects", allObjects);
		LoForEachOne(probe, a, w, "allObjectsAndResources", allObjectsAndResources);
		LoForEachOne(probe, a, w, "allDrawing+Groups", allDrawing + descendIntoGroups);
		LoForEachOne(probe, a, w, "allDrawing+All", allDrawing + descendIntoAll);
		LoForEachOne(probe, a, w, "allObjects+Groups", allObjects + descendIntoGroups);
		LoForEachOne(probe, a, w, "allObjects+All", objAll);
		LoForEachOne(probe, a, w, "allObjects+All+Viewports", objAll + descendIntoViewports);
		LoForEachOne(probe, a, w, "allObjects+AuxLists", allObjects + descendIntoAuxLists);
		LoForEachOne(probe, a, w, "allObjects+All+AuxLists", objAll + descendIntoAuxLists);
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
		if (gSDK->ParentObject(h) == layer)
			return h;
		const bool moved = gSDK->AddObjectToContainer(h, layer);
		std::string s = std::string("  ") + label + " を " + LoLayerName(layer);
		s += std::string(" へ移した: ") + (moved ? "ok" : "失敗");
		probe.log(s);
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
		MCObjectHandle active = gSDK->GetActiveLayer();
		MCObjectHandle l = gSDK->FirstMemberObj(header);
		for (int i = 0; l != nullptr && i < 1000; ++i)
		{
			if (LoType(l) == kLayerNode)
			{
				double_gs scale = 0;
				gSDK->GetLayerScaleN(l, scale);
				std::string s = "  [" + LoInt(i) + "] " + LoLayerName(l);
				s += "  scale=" + LoNum(scale);
				if (l == active)
					s += "  (アクティブ)";
				probe.log(s);
			}
			l = gSDK->NextObject(l);
		}
	}

	// 名前・外形・クラスを読みながら辿る（刻みの中の所要を測る本体）。
	int LoWalkRead(MCObjectHandle layer)
	{
		int n = 0;
		MCObjectHandle h = gSDK->FirstMemberObj(layer);
		while (h != nullptr)
		{
			TXString name;
			gSDK->GetObjectName(h, name);
			WorldRect r;
			gSDK->GetObjectBounds(h, r);
			gSDK->GetObjectClass(h);
			++n;
			h = gSDK->NextObject(h);
		}
		return n;
	}

	struct LoCountOnly
	{
		int* n;

		void operator()(MCObjectHandle) const
		{
			(*n)++;
		}
	};

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
		const auto t0 = std::chrono::steady_clock::now();
		gLoTick.walkN = LoWalkRead(gLoTick.layer);
		gLoTick.walkUs = LoUs(t0);
		int m = 0;
		std::function<void(MCObjectHandle)> fn = LoCountOnly{&m};
		const auto t1 = std::chrono::steady_clock::now();
		gSDK->ForEachObjectN(allObjects, fn);
		gLoTick.forEachUs = LoUs(t1);
		gLoTick.forEachN = m;
		gLoTick.done = true;
	}

	void LoTimingInTick(vwprobe::Report& probe, MCObjectHandle many)
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
			return;
		}
		std::string s = "  刻みの中 walk＋名前・外形・クラス: " + LoInt(gLoTick.walkN);
		s += " ノード " + LoInt(gLoTick.walkUs) + " µs";
		s += " / ForEachObjectN(allObjects): " + LoInt(gLoTick.forEachN);
		s += " 個 " + LoInt(gLoTick.forEachUs) + " µs";
		probe.log(s);
	}
#endif

	void LoTiming(vwprobe::Report& probe, MCObjectHandle many)
	{
		probe.log("--- 8 刻みの中の所要（" + std::string(kLoMany) + "） ---");
		// 刻みの外（対照）
		for (int rep = 0; rep < 3; ++rep)
		{
			const auto t0 = std::chrono::steady_clock::now();
			const int n = LoWalkRead(many);
			const long long us = LoUs(t0);
			std::string s = "  刻みの外 walk＋名前・外形・クラス: " + LoInt(n);
			s += " ノード " + LoInt(us) + " µs";
			probe.log(s);
		}
#if GS_WIN
		for (int rep = 0; rep < 3; ++rep)
			LoTimingInTick(probe, many);
#else
		probe.log("  （刻みの中の測定は Windows だけ）");
#endif
	}

	// 作った順（重ね順の確かめ）。
	struct LoMade
	{
		std::string label;
		MCObjectHandle h;
	};

	struct LoBuilder
	{
		vwprobe::Report& probe;
		MCObjectHandle layerA;
		std::vector<LoMade> made;

		MCObjectHandle Add(const char* label, MCObjectHandle h, const char* name)
		{
			h = LoPut(probe, label, h, layerA);
			if (h != nullptr && name != nullptr)
				gSDK->SetObjectName(h, name);
			made.push_back({label, h});
			return h;
		}
	};

	MCObjectHandle LoMakeGroup()
	{
		MCObjectHandle inner = gSDK->CreateGroup();
		if (inner != nullptr)
			gSDK->AddObjectToContainer(LoRect(4000, 100, 4300, 400), inner);
		MCObjectHandle group = gSDK->CreateGroup();
		if (group == nullptr)
			return nullptr;
		MCObjectHandle l1 = gSDK->CreateLine(WorldPt(4000, 0), WorldPt(5000, 0));
		MCObjectHandle l2 = gSDK->CreateLine(WorldPt(4000, 600), WorldPt(5000, 600));
		gSDK->AddObjectToContainer(l1, group);
		gSDK->AddObjectToContainer(l2, group);
		if (inner != nullptr)
			gSDK->AddObjectToContainer(inner, group);
		gSDK->ResetObject(group);
		return group;
	}

	MCObjectHandle LoMakeSymbolDef(vwprobe::Report& probe)
	{
		TXString symName = "LO-シンボル";
		MCObjectHandle def = gSDK->CreateSymbolDefinition(symName);
		if (def != nullptr)
		{
			gSDK->AddObjectToContainer(LoRect(0, 0, 200, 200), def);
			gSDK->AddObjectToContainer(LoRect(300, 0, 500, 200), def);
			gSDK->ResetObject(def);
		}
		probe.log("  シンボル定義: " + LoPtr(def) + " name=\"" + LoStr(symName) + "\"");
		return def;
	}

	void LoMakeWall(LoBuilder& b)
	{
		MCObjectHandle w = gSDK->CreateWall(WorldPt(0, 2000), WorldPt(5000, 2000), 200);
		MCObjectHandle wall = b.Add("09 壁", w, "LO-壁");
		// 壁の上に置けば CreateCustomObject が自分で壁へ入れる見込み
		// （varCreateCustomObjectDontInsertInWall があるため）。#282 の 1 回目は、この後に
		// AddObjectToContainer(窓, 壁) を重ねて、窓の親が nil になった。
		MCObjectHandle win = gSDK->CreateCustomObject("Window", WorldPt(2500, 2000), 0);
		if (wall == nullptr || win == nullptr)
			return;
		MCObjectHandle parent = gSDK->ParentObject(win);
		std::string s = "  窓を壁の上に CreateCustomObject: 親の型=" + LoInt(LoType(parent));
		s += parent == wall ? "（その壁）" : "（壁ではない）";
		b.probe.log(s);
		if (parent == wall)
			return;
		if (parent == nullptr)
		{
			// 行き場の無い PIO を図面に残さない
			gSDK->DeleteObject(win, false);
			b.probe.log("  親の無い窓を DeleteObject(useUndo=false) で消した");
			return;
		}
		b.made.push_back({"09b 窓（壁に入らなかった）", win});
	}

	MCObjectHandle LoMakeExtrude()
	{
		MCObjectHandle ex = gSDK->CreateExtrude(0, 1000);
		if (ex == nullptr)
			return nullptr;
		gSDK->AddObjectToContainer(LoRect(6000, 2000, 7000, 3000), ex);
		gSDK->ResetObject(ex);
		return ex;
	}

	MCObjectHandle LoMakeViewport(vwprobe::Report& probe, MCObjectHandle sheet,
								  MCObjectHandle layerA)
	{
		MCObjectHandle vp = gSDK->CreateViewport(sheet);
		if (vp != nullptr)
		{
			// 0 = kLayerVisibilityNormal
			gSDK->SetViewportLayerVisibility(vp, layerA, 0);
			gSDK->UpdateViewport(vp);
		}
		MCObjectHandle parent = gSDK->ParentObject(vp);
		probe.log("  ビューポート: " + LoPtr(vp) + " 親の型=" + LoInt(LoType(parent)));
		return vp;
	}

	void LoMakeMany(MCObjectHandle many)
	{
		for (int i = 0; i < kManyCount; ++i)
		{
			const double x = (i % 60) * 200.0;
			const double y = (i / 60) * 200.0;
			MCObjectHandle h = LoRect(x, y, x + 100, y + 100);
			if (h != nullptr && gSDK->ParentObject(h) != many)
				gSDK->AddObjectToContainer(h, many);
		}
	}

	struct LoLayers
	{
		MCObjectHandle a = nullptr;
		MCObjectHandle b = nullptr;
		MCObjectHandle many = nullptr;
		MCObjectHandle sheet = nullptr;
	};

	void LoMakeLayers(vwprobe::Report& probe, LoLayers& L)
	{
		L.a = gSDK->CreateLayerN(kLoA, 1.0);
		L.b = gSDK->CreateLayerN(kLoB, 50.0);
		double_gs sb = 0;
		gSDK->GetLayerScaleN(L.b, sb);
		if (std::fabs(sb - 50.0) > 1e-9)
		{
			gSDK->SetLayerScaleN(L.b, 50.0);
			gSDK->GetLayerScaleN(L.b, sb);
		}
		probe.log("  " + std::string(kLoB) + " の縮尺（読み戻し）= " + LoNum(sb));
		L.many = gSDK->CreateLayerN(kLoMany, 1.0);
		L.sheet = gSDK->CreateLayer(kLoSheet, kLayerSheet);
		std::string s = "  レイヤ: A=" + LoPtr(L.a) + " B=" + LoPtr(L.b);
		s += " 多=" + LoPtr(L.many) + " S=" + LoPtr(L.sheet);
		s += "  作った直後のアクティブ=" + LoLayerName(gSDK->GetActiveLayer());
		probe.log(s);
	}

	// 作るものを絞る（落ちの切り分け用）。%TEMP%\VwSdkProbes-layer-objects.mode に
	// 語を空白区切りで並べると、その語の群だけ作る。ファイルが無ければ全部作る。
	// 語: 2d symbol sym0 sym45 symdef door wall extrude viewport many b nosel
	struct LoMode
	{
		std::string words;
		bool all = true;

		bool Has(const char* w) const
		{
			if (all)
				return true;
			return (" " + words + " ").find(std::string(" ") + w + " ") != std::string::npos;
		}
	};

	LoMode LoReadMode(vwprobe::Report& probe)
	{
		LoMode m;
		const char* tmp = std::getenv("TEMP");
		if (tmp == nullptr)
			return m;
		const std::string path = std::string(tmp) + "\\VwSdkProbes-layer-objects.mode";
		std::FILE* f = std::fopen(path.c_str(), "r");
		if (f == nullptr)
		{
			probe.log("  mode: ファイル無し → 全部作る");
			return m;
		}
		char buf[256] = {};
		const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
		std::fclose(f);
		for (size_t i = 0; i < n; ++i)
		{
			const char c = buf[i];
			m.words += (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
		}
		m.all = false;
		probe.log("  mode: \"" + m.words + "\" の群だけ作る");
		return m;
	}

	void LoBuild2D(LoBuilder& b, InternalIndex cls)
	{
		MCObjectHandle line = gSDK->CreateLine(WorldPt(0, 0), WorldPt(1000, 0));
		b.Add("01 直線（名前あり）", line, "LO-直線");

		MCObjectHandle rect = b.Add("02 矩形（名前なし）", LoRect(0, 100, 1000, 600), nullptr);
		if (rect != nullptr)
			gSDK->SetObjectClass(rect, cls);

		const double kPi = 3.14159265358979323846;
		const Vector2 dir(std::cos(kPi / 6), std::sin(kPi / 6));
		MCObjectHandle rot = gSDK->CreateRectangleN(WorldPt(1500, 100), dir, 1000, 500);
		b.Add("03 回転した矩形 30°（原点 1500,100・1000×500）", rot, "LO-回転");

		const WorldRect ovalBox(WorldPt(2500, 600), WorldPt(3500, 100));
		b.Add("04 楕円", gSDK->CreateOval(ovalBox), nullptr);

		MCObjectHandle text = gSDK->CreateTextBlock("文字の外形", WorldPt(0, -500), false, 0);
		b.Add("05 文字", text, "LO-文字");

		MCObjectHandle group = LoMakeGroup();
		b.Add("06 グループ（直線 2・入れ子 1）", group, "LO-グループ");
	}

	// symbol は 0° と 45° の両方を置く。sym0 / sym45 はどちらか一方、symdef は定義だけ。
	void LoBuildSymbols(LoBuilder& b, const LoMode& mode)
	{
		MCObjectHandle def = LoMakeSymbolDef(b.probe);
		if (def == nullptr)
			return;
		const bool both = mode.Has("symbol");
		if (both || mode.Has("sym0"))
			b.Add("07a シンボル", gSDK->PlaceSymbolN(def, WorldPt(6000, 0), 0), nullptr);
		if (both || mode.Has("sym45"))
		{
			MCObjectHandle s2 = gSDK->PlaceSymbolN(def, WorldPt(7000, 0), 45);
			b.Add("07b シンボル（名前あり・45°）", s2, "LO-シンボル2");
		}
	}

	// 作ったものの選択を外す（nosel。選択の強調の描画が引き金かを分ける）。
	void LoDeselect(vwprobe::Report& probe, const LoLayers& L)
	{
		int n = 0;
		const MCObjectHandle layers[] = {L.a, L.b, L.many, L.sheet};
		for (MCObjectHandle layer : layers)
		{
			if (layer == nullptr)
				continue;
			MCObjectHandle h = gSDK->FirstMemberObj(layer);
			while (h != nullptr)
			{
				if (gSDK->IsSelected(h))
				{
					gSDK->SelectObject(h, false);
					++n;
				}
				h = gSDK->NextObject(h);
			}
		}
		probe.log("  nosel: 選択を外した数 " + LoInt(n));
	}

	void LoBuild(vwprobe::Report& probe, LoLayers& L)
	{
		probe.log("--- 見本を作る ---");
		const LoMode mode = LoReadMode(probe);
		LoMakeLayers(probe, L);
		if (L.a == nullptr)
			return;
		const InternalIndex cls = gSDK->AddClass("LO-クラス");
		LoBuilder b{probe, L.a, {}};

		if (mode.Has("2d"))
			LoBuild2D(b, cls);
		const bool anySym = mode.Has("symbol") || mode.Has("sym0") || mode.Has("sym45");
		if (anySym || mode.Has("symdef"))
			LoBuildSymbols(b, mode);

		// 設定ダイアログを出さないため先に定義する（Findings「Parametric Objects」）。
		gSDK->DefineCustomObject("Door", kCustomObjectPrefNever);
		gSDK->DefineCustomObject("Window", kCustomObjectPrefNever);
		if (mode.Has("door"))
		{
			MCObjectHandle door = gSDK->CreateCustomObject("Door", WorldPt(8000, 0), 0);
			b.Add("08 PIO ドア", door, nullptr);
		}
		if (mode.Has("wall"))
			LoMakeWall(b);
		if (mode.Has("extrude"))
			b.Add("10 押し出し（1000×1000×高さ 1000）", LoMakeExtrude(), nullptr);

		// 同じ矩形を LO-B（1:50）へ
		if (mode.Has("b"))
		{
			MCObjectHandle rb = LoPut(probe, "B 矩形", LoRect(0, 100, 1000, 600), L.b);
			if (rb != nullptr)
				gSDK->SetObjectName(rb, "LO-B矩形");
		}
		if (L.sheet != nullptr && mode.Has("viewport"))
			LoMakeViewport(probe, L.sheet, L.a);
		if (L.many != nullptr && mode.Has("many"))
			LoMakeMany(L.many);
		if (!mode.all && mode.Has("nosel"))
			LoDeselect(probe, L);

		probe.log("--- 作った順（1 の答え合わせ） ---");
		for (const LoMade& m : b.made)
		{
			std::string s = "  " + m.label + " → " + LoPtr(m.h);
			s += " type=" + LoInt(LoType(m.h));
			probe.log(s);
		}
	}

	void LoNested(vwprobe::Report& probe, const std::vector<MCObjectHandle>& walkA)
	{
		probe.log("--- 3 入れ子の中身（グループ・シンボル定義・壁・PIO の子） ---");
		for (MCObjectHandle h : walkA)
		{
			const short t = LoType(h);
			const bool box = t == kGroupNode || t == kWallNode;
			if (box || t == kParametricNode || t == kExtrudeNode)
			{
				probe.log("  " + LoDescribe(h));
				MCObjectHandle k = gSDK->FirstMemberObj(h);
				while (k != nullptr)
				{
					probe.log("      子 " + LoDescribe(k));
					k = gSDK->NextObject(k);
				}
			}
			if (t != kSymbolNode)
				continue;
			MCObjectHandle def = gSDK->GetDefinition(h);
			TXString dn;
			gSDK->GetObjectName(def, dn);
			std::string s = "  シンボル → 定義 \"" + LoStr(dn) + "\"";
			s += " type=" + LoInt(LoType(def));
			s += "  インスタンスの子の先頭の型=" + LoInt(LoType(gSDK->FirstMemberObj(h)));
			probe.log(s);
		}
	}

	void LoNamedOne(vwprobe::Report& probe, const char* n)
	{
		MCObjectHandle h = gSDK->GetNamedObject(n);
		std::string s = std::string("  ") + n + " → " + LoPtr(h);
		s += " type=" + LoInt(LoType(h));
		s += " layer=" + LoLayerName(LoLayerOf(h));
		probe.log(s);
	}

	void LoNamed(vwprobe::Report& probe)
	{
		probe.log("--- 名前で引けるか（GetNamedObject） ---");
		LoNamedOne(probe, "LO-直線");
		LoNamedOne(probe, "LO-回転");
		LoNamedOne(probe, "LO-グループ");
		LoNamedOne(probe, "LO-壁");
		LoNamedOne(probe, "LO-シンボル");
		LoNamedOne(probe, "LO-B矩形");
	}
} // namespace

VW_PROBE("layer-objects", "レイヤの中の図形の列挙", "見本を作り、辿り方ごとに数と並びを出す")
{
	LoLayers L;
	L.a = gSDK->GetNamedLayer(kLoA);
	L.b = gSDK->GetNamedLayer(kLoB);
	L.many = gSDK->GetNamedLayer(kLoMany);
	L.sheet = gSDK->GetNamedLayer(kLoSheet);
	const bool second = L.a != nullptr;
	if (second)
		probe.log("見本あり → 辿り直すだけ（2 回目）");
	else
		probe.log("見本なし → 作って測る（1 回目）");

	if (!second)
		LoBuild(probe, L);
	if (L.a == nullptr)
	{
		probe.fail("LO-A が作れなかった");
		return;
	}

	LoDumpLayers(probe);

	probe.log("--- 1・2・5・6 LO-A を First→Next で（1 ノードずつ） ---");
	const std::vector<MCObjectHandle> walkA = LoWalk(probe, L.a, true);
	LoReverseCheck(probe, L.a, walkA);
	LoNested(probe, walkA);

	if (L.b != nullptr)
	{
		probe.log("--- 6 縮尺 1:50 のレイヤ（LO-A の 02 と同じ座標の矩形） ---");
		LoWalk(probe, L.b, true);
	}
	if (L.sheet != nullptr)
	{
		probe.log("--- 7 シートレイヤ ---");
		LoWalk(probe, L.sheet, true);
	}
	if (L.many != nullptr)
		LoWalk(probe, L.many, false);

	probe.log("--- 3・7 ForEachObjectN の traverseWhat ごと ---");
	LoForEach(probe, L.a, walkA);

	if (L.many != nullptr)
		LoTiming(probe, L.many);

	LoNamed(probe);
}
