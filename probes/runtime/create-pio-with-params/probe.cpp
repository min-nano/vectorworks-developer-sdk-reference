//
//	probes/runtime/create-pio-with-params/probe.cpp
//
//	[issue #122] 組み込み PIO（ドア・窓）を「パラメータを指定した状態で、再生成 1 回で」
//	作れるか——その 3 回目。
//
//	2 回目で、ダイアログを DefineCustomObject(kCustomObjectPrefNever) で止めた窓では、
//	CreateCustomObjectPath(nil, nil, doRegen=false) で作ったもの（B）が
//	CreateCustomObject で作ったもの（A）と 618 欄中 76 欄違った（B は書いた幅も外形に
//	出なかった）。ダイアログを OK で通したドアでは 672 欄すべて一致した。
//	この回はダイアログを一切出さない条件（プラグインが実際に使う条件）で、差の出どころを分ける:
//
//	  1) 書式（パラメトリックレコード）の既定値 F と、何も書かずに作った A0・B0 を
//	     それぞれ突き合わせる。どちらが F のままで、どちらが F から離れるか。
//	  2) B0 に A0 の全欄を写してから書いて ResetObject すると、A と同じになるか（C）。
//	  3) 書式の既定値を書き換えてから CreateCustomObject（1 回目の [4]）は、A と同じになるか（D）。
//	  4) ResetObject の後に SetEntityMatrix で動かすと外形が古いまま残った（2 回目の B'）。
//	     もう一度 ResetObject すれば追い付くか。
//
//	**新規の空図面で走らせる。**ダイアログは出ない見込み（出たら OK で閉じる）。
//

#include "Probe.h"

#include "VWFC/Math/VWTransformMatrix.h"
#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWRecordFormatObj.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace
{
	using ProbeClock = std::chrono::steady_clock;

	const double kWantWidth = 1410.0;

	double ElapsedMsSince(ProbeClock::time_point t0)
	{
		return std::chrono::duration<double, std::milli>(ProbeClock::now() - t0).count();
	}

	std::string FormatMs(double ms)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.2fms", ms);
		return buf;
	}

	std::string FormatNum(double v)
	{
		char buf[48];
		std::snprintf(buf, sizeof(buf), "%.3f", v);
		return buf;
	}

	std::string Utf8(const TXString& s)
	{
		return std::string(s.GetStdString());
	}

	// 外形（中心・大きさ）と子の数。
	std::string DescribeShape(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		std::string s;
		WorldRect r;
		if (gSDK->GetObjectBounds(h, r))
		{
			const WorldPt c = r.Center();
			s += "外形中心=(" + FormatNum(c.x) + "," + FormatNum(c.y) +
				 ") 外形=" + FormatNum(r.Width()) + "x" + FormatNum(r.Height());
		}
		int n = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(h); m != nil; m = gSDK->NextObject(m))
			++n;
		s += " 子=" + std::to_string(n);
		return s;
	}

	// 値の出どころ（書式 or 個体）を 1 本の関数で比べるための薄い口。
	struct ProbeParamSource
	{
		const char* label;
		VWParametricObj* pio;
		VWRecordFormatObj* format;

		TXString Value(const TXString& name) const
		{
			return pio != nullptr ? pio->GetParamValue(name) : format->GetParamValue(name);
		}
	};

	// 全欄を文字列で突き合わせ、違う欄を最大 maxList 件まで並べる。欄名は a から採る。
	size_t CompareParamSources(::vwprobe::Report& probe, VWParametricObj& names,
							   const ProbeParamSource& a, const ProbeParamSource& b, size_t maxList)
	{
		const size_t n = names.GetParamsCount();
		size_t diff = 0;
		for (size_t i = 0; i < n; ++i)
		{
			const TXString name = names.GetParamName(i);
			const TXString va = a.Value(name);
			const TXString vb = b.Value(name);
			if (va == vb)
				continue;
			++diff;
			if (diff <= maxList)
				probe.log("      " + Utf8(name) + ": " + a.label + "=" + Utf8(va) + " " + b.label +
						  "=" + Utf8(vb));
		}
		probe.log("    " + std::string(a.label) + " と " + b.label + ": 全 " + std::to_string(n) +
				  " 欄のうち違うもの " + std::to_string(diff) + " 欄");
		return diff;
	}

	MCObjectHandle CreateByPathAt(const TXString& pioName, double x, double y)
	{
		MCObjectHandle h = gSDK->CreateCustomObjectPath(pioName, nil, nil, false);
		if (h == nil)
			return nil;
		VWTransformMatrix place;
		place.SetOffset(VWPoint3D(x, y, 0));
		gSDK->SetEntityMatrix(h, place);
		return h;
	}

	void RunForPio(::vwprobe::Report& probe, const TXString& pioName, double baseY)
	{
		probe.log("");
		probe.log("===== " + Utf8(pioName) + " =====");

		auto t0 = ProbeClock::now();
		MCObjectHandle def = gSDK->DefineCustomObject(pioName, kCustomObjectPrefNever);
		double ms = ElapsedMsSince(t0);
		probe.log("DefineCustomObject(kCustomObjectPrefNever) " + FormatMs(ms) +
				  " 戻り=" + (def != nil ? "非 nil" : "nil"));

		// 1) 何も書かずに作る
		t0 = ProbeClock::now();
		MCObjectHandle a0 = gSDK->CreateCustomObject(pioName, WorldPt(0, baseY), 0.0, true);
		ms = ElapsedMsSince(t0);
		probe.log("[A0] CreateCustomObject（何も書かない） " + FormatMs(ms) + " / " +
				  DescribeShape(a0));
		MCObjectHandle b0 = CreateByPathAt(pioName, 3000, baseY);
		probe.log("[B0] CreateCustomObjectPath(doRegen=false)＋置く（何も書かない） / " +
				  DescribeShape(b0));
		if (a0 == nil || b0 == nil)
		{
			probe.log("  作れなかったので飛ばす");
			return;
		}
		gSDK->ResetObject(b0);
		probe.log("  B0 を ResetObject / " + DescribeShape(b0));

		VWParametricObj pa0(a0);
		VWParametricObj pb0(b0);
		VWRecordFormatObj format = pa0.GetRecordFormat();
		const ProbeParamSource srcF{"F", nullptr, &format};
		const ProbeParamSource srcA0{"A0", &pa0, nullptr};
		const ProbeParamSource srcB0{"B0", &pb0, nullptr};
		probe.log("[1] 書式の既定値 F と突き合わせる");
		CompareParamSources(probe, pa0, srcF, srcA0, 25);
		CompareParamSources(probe, pa0, srcF, srcB0, 25);
		CompareParamSources(probe, pa0, srcA0, srcB0, 5);

		// 基準 A: いつもの経路で幅を書く
		MCObjectHandle a = gSDK->CreateCustomObject(pioName, WorldPt(6000, baseY), 0.0, true);
		VWParametricObj pa(a);
		pa.SetParamReal("Width", kWantWidth);
		gSDK->ResetObject(a);
		probe.log("[A] CreateCustomObject → 幅 → ResetObject / " + DescribeShape(a));
		const ProbeParamSource srcA{"A", &pa, nullptr};

		// 2) C: Path の入口で作り、A0 の全欄を写してから幅を書く
		MCObjectHandle c = CreateByPathAt(pioName, 9000, baseY);
		VWParametricObj pc(c);
		const size_t n = pa0.GetParamsCount();
		for (size_t i = 0; i < n; ++i)
		{
			const TXString name = pa0.GetParamName(i);
			pc.SetParamValue(name, pa0.GetParamValue(name));
		}
		pc.SetParamReal("Width", kWantWidth);
		t0 = ProbeClock::now();
		gSDK->ResetObject(c);
		probe.log("[C] Path(doRegen=false) → A0 の全欄を写す → 幅 → ResetObject " +
				  FormatMs(ElapsedMsSince(t0)) + " / " + DescribeShape(c));
		const ProbeParamSource srcC{"C", &pc, nullptr};
		CompareParamSources(probe, pa, srcA, srcC, 15);

		// 3) D: 書式の既定値を書き換えてから CreateCustomObject（終わったら戻す）
		const TXString oldWidth = format.GetParamValue("Width");
		format.SetParamReal("Width", kWantWidth);
		MCObjectHandle d = gSDK->CreateCustomObject(pioName, WorldPt(12000, baseY), 0.0, true);
		format.SetParamValue("Width", oldWidth);
		probe.log("[D] 書式の幅を書き換え → CreateCustomObject → 書式を戻す（戻した値=" +
				  Utf8(format.GetParamValue("Width")) + "） / " + DescribeShape(d));
		if (d != nil)
		{
			VWParametricObj pd(d);
			const ProbeParamSource srcD{"D", &pd, nullptr};
			CompareParamSources(probe, pa, srcA, srcD, 15);
		}

		// 4) ResetObject の後で動かすと外形が古いまま残るか、もう一度 ResetObject で追い付くか
		VWTransformMatrix moved;
		moved.SetOffset(VWPoint3D(15000, baseY, 0));
		gSDK->SetEntityMatrix(c, moved);
		probe.log("[4] C を (15000, y) へ SetEntityMatrix / " + DescribeShape(c));
		gSDK->ResetObject(c);
		probe.log("    もう一度 ResetObject / " + DescribeShape(c));
	}
} // namespace

VW_PROBE("create-pio-with-params", "PIO をパラメータ指定・再生成 1 回で作れるか（3 回目）",
		 "新規の空図面で走らせる。ダイアログを出さない条件で、Path の入口で作った PIO が"
		 "いつもの経路と違う値になる出どころを分け、揃える手を試す")
{
	RunForPio(probe, "Door", 0);
	RunForPio(probe, "Window", 5000);
}
