//
//	probes/runtime/create-pio-with-params/probe.cpp
//
//	[issue #122] 組み込み PIO（ドア・窓）を「パラメータを指定した状態で、再生成 1 回で」
//	作れるか——その 2 回目。1 回目で次が分かった:
//
//	  - CreateCustomObject は作成の時点で再生成する（bInsert=false でも同じ）。
//	  - 書式の既定値を書き換えてから作れば、その値で 1 回だけ描かれる。
//	  - 点で置く PIO も CreateCustomObjectPath(name, nil, nil, doRegen=false) で
//	    再生成なしに作れ、書いてから ResetObject すれば 1 回で正しい外形になる。
//
//	残った問い（この回で確かめる）:
//
//	  A) 初めて使う文書で、Path の入口（doRegen=false）でも「オブジェクトの設定」
//	     ダイアログが出るか。DefineCustomObject(kCustomObjectPrefNever) で止まるか。
//	     ダイアログは人を待つので、所要時間（秒単位になるか）で判定する。
//	  B) Path の入口で作ったものを「位置・角度つき」で置けるか（SetEntityMatrix）。
//	     置いただけで再生成が走るか。
//	  C) できあがったものが、CreateCustomObject で作ったものと同じか
//	     （位置・向き・外形・子の型の並び・パスの有無・全パラメータの値）。
//
//	**新規の空図面で走らせる**（ドア・窓を一度も使っていない文書でないと A が測れない）。
//

#include "Probe.h"

#include "VWFC/Math/VWTransformMatrix.h"
#include "VWFC/VWObjects/VWParametricObj.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	using ProbeClock = std::chrono::steady_clock;

	const double kProbeAngleDeg = 30.0;

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

	// 1 秒を超えたら人を待った（＝ダイアログが出た）とみなす。作成そのものは 20ms 前後
	// （1 回目の実測）なので、取り違えは起きない。
	std::string DialogWord(double ms)
	{
		return ms > 1000.0 ? "ダイアログが出た見込み" : "ダイアログ無し";
	}

	// 直下の子の型を並べる（再生成前後・経路間の比較用）。
	std::string MemberTypesOfPio(MCObjectHandle h, int& count)
	{
		count = 0;
		std::string s;
		for (MCObjectHandle m = gSDK->FirstMemberObj(h); m != nil; m = gSDK->NextObject(m))
		{
			if (!s.empty())
				s += ",";
			s += std::to_string(gSDK->GetObjectTypeN(m));
			++count;
		}
		return s;
	}

	// 位置（行列の原点）・向き（U ベクトルの角度）・外形の中心と大きさ・子・パスの有無。
	std::string DescribePlaced(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		TransformMatrix raw;
		gSDK->GetEntityMatrix(h, raw);
		VWTransformMatrix mat(raw);
		const VWPoint3D off = mat.GetOffset();
		const VWPoint3D u = mat.GetUVector();
		const double angle = std::atan2(u.y, u.x) * 180.0 / 3.14159265358979323846;
		std::string s = "原点=(" + FormatNum(off.x) + "," + FormatNum(off.y) + "," +
						FormatNum(off.z) + ") 角度=" + FormatNum(angle);
		WorldRect r;
		if (gSDK->GetObjectBounds(h, r))
		{
			const WorldPt c = r.Center();
			s += " 外形中心=(" + FormatNum(c.x) + "," + FormatNum(c.y) +
				 ") 外形=" + FormatNum(r.Width()) + "x" + FormatNum(r.Height());
		}
		int n = 0;
		const std::string types = MemberTypesOfPio(h, n);
		s += " 子=" + std::to_string(n) + "[" + types + "]";
		s += std::string(" パス=") + (gSDK->GetCustomObjectPath(h) != nil ? "有" : "無");
		return s;
	}

	// 全パラメータを文字列で突き合わせ、違う欄を並べる。
	void CompareAllParams(::vwprobe::Report& probe, MCObjectHandle a, MCObjectHandle b)
	{
		VWParametricObj pa(a);
		VWParametricObj pb(b);
		const size_t n = pa.GetParamsCount();
		size_t diff = 0;
		for (size_t i = 0; i < n; ++i)
		{
			const TXString name = pa.GetParamName(i);
			const TXString va = pa.GetParamValue(name);
			const TXString vb = pb.GetParamValue(name);
			if (va == vb)
				continue;
			++diff;
			if (diff <= 20)
				probe.log("    違う: " + std::string(name.GetStdString()) + " A=" +
						  std::string(va.GetStdString()) + " B=" + std::string(vb.GetStdString()));
		}
		probe.log("  全 " + std::to_string(n) + " 欄のうち違うもの " + std::to_string(diff) +
				  " 欄（B の欄数 " + std::to_string(pb.GetParamsCount()) + "）");
	}

	void ComparePlacement(::vwprobe::Report& probe, const TXString& pioName, double baseY)
	{
		const std::string name = pioName.GetStdString();
		probe.log("");
		probe.log("===== B/C: " + name + "（位置・角度つきで置き、経路間で比べる） =====");
		const double wantWidth = 1410.0;

		// A: いつもの経路
		probe.log("[A] CreateCustomObject(pt, 30°) → 幅を書く → ResetObject");
		auto t0 = ProbeClock::now();
		MCObjectHandle ha =
			gSDK->CreateCustomObject(pioName, WorldPt(0, baseY), kProbeAngleDeg, true);
		probe.log("  作成 " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePlaced(ha));
		if (ha == nil)
			return;
		VWParametricObj(ha).SetParamReal("Width", wantWidth);
		t0 = ProbeClock::now();
		gSDK->ResetObject(ha);
		probe.log("  ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePlaced(ha));

		// B: Path の入口（doRegen=false）→ 行列で置く → 書く → ResetObject
		probe.log("[B] CreateCustomObjectPath(nil, nil, doRegen=false) → SetEntityMatrix(pt, "
				  "30°) → 幅を書く → ResetObject");
		t0 = ProbeClock::now();
		MCObjectHandle hb = gSDK->CreateCustomObjectPath(pioName, nil, nil, false);
		probe.log("  作成 " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePlaced(hb));
		if (hb == nil)
			return;
		VWTransformMatrix place;
		place.RotateZAfter(kProbeAngleDeg);
		place.SetOffset(VWPoint3D(6000, baseY, 0));
		t0 = ProbeClock::now();
		gSDK->SetEntityMatrix(hb, place);
		probe.log("  SetEntityMatrix " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePlaced(hb));
		VWParametricObj(hb).SetParamReal("Width", wantWidth);
		t0 = ProbeClock::now();
		gSDK->ResetObject(hb);
		probe.log("  ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePlaced(hb));

		// B': 書いて ResetObject した後で置き直す（置き直しに再生成が要るか）
		probe.log("[B'] 同じ B を ResetObject の後で (12000, y) へ置き直す");
		VWTransformMatrix again;
		again.RotateZAfter(kProbeAngleDeg);
		again.SetOffset(VWPoint3D(12000, baseY, 0));
		gSDK->SetEntityMatrix(hb, again);
		probe.log("  置き直した直後 / " + DescribePlaced(hb));

		probe.log("[C] A と B の全パラメータを突き合わせる");
		CompareAllParams(probe, ha, hb);
	}
} // namespace

VW_PROBE("create-pio-with-params", "PIO をパラメータ指定・再生成 1 回で作れるか（2 回目）",
		 "新規の空図面で走らせる。Path の入口で作った点の PIO を位置・角度つきで置けるか、"
		 "いつもの経路と同じものになるか、初回のダイアログが出るかを測る")
{
	// A) 初めて使うときのダイアログ
	probe.log("===== A: 初めて使うときのダイアログ =====");
	probe.log("[A1] Door を初めて CreateCustomObjectPath(nil, nil, doRegen=false) で作る");
	auto t0 = ProbeClock::now();
	MCObjectHandle firstDoor = gSDK->CreateCustomObjectPath("Door", nil, nil, false);
	double ms = ElapsedMsSince(t0);
	probe.log("  作成 " + FormatMs(ms) + "（" + DialogWord(ms) + "） / " +
			  DescribePlaced(firstDoor));

	probe.log("[A2] Window を DefineCustomObject(kCustomObjectPrefNever) してから "
			  "CreateCustomObject で初めて作る");
	t0 = ProbeClock::now();
	MCObjectHandle def = gSDK->DefineCustomObject("Window", kCustomObjectPrefNever);
	ms = ElapsedMsSince(t0);
	probe.log("  DefineCustomObject " + FormatMs(ms) + "（" + DialogWord(ms) +
			  "） 戻り=" + (def != nil ? "非 nil" : "nil"));
	t0 = ProbeClock::now();
	MCObjectHandle firstWindow = gSDK->CreateCustomObject("Window", WorldPt(0, -5000), 0.0, true);
	ms = ElapsedMsSince(t0);
	probe.log("  作成 " + FormatMs(ms) + "（" + DialogWord(ms) + "） / " +
			  DescribePlaced(firstWindow));

	// B / C
	ComparePlacement(probe, "Door", 5000);
	ComparePlacement(probe, "Window", 10000);
}
