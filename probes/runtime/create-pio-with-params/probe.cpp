//
//	probes/runtime/create-pio-with-params/probe.cpp
//
//	[issue #122] 組み込み PIO（ドア・窓）を「パラメータを指定した状態で、再生成 1 回で」
//	作れる経路があるかを実測する。ヘッダだけでは分からない:
//
//	  1) CreateCustomObject は作成の時点で再生成（既定値で描く）するか。
//	     → 作る → 書く → ResetObject が「2 回描く」になっているか。
//	  2) CreateCustomObject / CreateCustomObjectByMatrixEx の bInsert=false は何をするか
//	     （図面に入れない＝再生成しない、なのか。後から AddObjectToContainer できるか）。
//	  3) パラメトリックレコード書式の既定値を書き換えてから作ると、その値で生まれるか。
//	  4) 点で置く PIO に CreateCustomObjectPath(name, nil, nil, doRegen=false) が使えるか。
//
//	「再生成が走ったか」は、PIO の中身（子オブジェクトの数）と外形で判定する
//	（再生成前の PIO は中身を持たない）。所要時間も添える。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWRecordFormatObj.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace
{
	using ProbeClock = std::chrono::steady_clock;

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

	std::string HandleWord(MCObjectHandle h)
	{
		return h == nil ? "nil" : "非 nil";
	}

	// PIO の中身（直下の子）の数。再生成前の PIO は 0 のはず。
	int CountMembersOfPio(MCObjectHandle h)
	{
		int n = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(h); m != nil; m = gSDK->NextObject(m))
			++n;
		return n;
	}

	// 1 行の状態: 子の数・外形・パラメータの値・親。
	std::string DescribePio(MCObjectHandle h, const TXString& paramName)
	{
		if (h == nil)
			return "(nil)";
		std::string s = "子=" + std::to_string(CountMembersOfPio(h));
		WorldRect r;
		if (gSDK->GetObjectBounds(h, r))
			s += " 外形=" + FormatNum(r.Width()) + "x" + FormatNum(r.Height());
		else
			s += " 外形=(取れない)";
		if (!paramName.IsEmpty())
		{
			VWParametricObj pio(h);
			s += " " + std::string(paramName.GetStdString()) + "=" +
				 FormatNum(pio.GetParamReal(paramName));
		}
		MCObjectHandle parent = gSDK->ParentObject(h);
		s += " 親=" + HandleWord(parent);
		if (parent != nil)
			s += "(type=" + std::to_string(gSDK->GetObjectTypeN(parent)) + ")";
		return s;
	}

	// 幅を表す実数パラメータを探す。見つからなければ全欄を並べて空を返す。
	TXString FindWidthParam(::vwprobe::Report& probe, MCObjectHandle h)
	{
		VWParametricObj pio(h);
		const char* candidates[] = {"Width", "OverallWidth", "Door Width", "Window Width",
									"W",	 "Length"};
		for (const char* c : candidates)
		{
			if (pio.GetParamIndex(c) != size_t(-1))
				return c;
		}
		probe.log("  幅らしい欄が見つからない。欄を並べる:");
		const size_t n = pio.GetParamsCount();
		for (size_t i = 0; i < n && i < 60; ++i)
			probe.log("    [" + std::to_string(i) + "] " +
					  std::string(pio.GetParamName(i).GetStdString()));
		return TXString();
	}

	void RunForPio(::vwprobe::Report& probe, const TXString& pioName, double baseY)
	{
		const std::string name = pioName.GetStdString();
		probe.log("");
		probe.log("===== " + name + " =====");

		// 0) 下見。書式（パラメトリックレコード）は最初の生成で文書に入る。
		probe.log("[0] 下見: CreateCustomObject(bInsert=true)");
		auto t0 = ProbeClock::now();
		MCObjectHandle h0 = gSDK->CreateCustomObject(pioName, WorldPt(0, baseY), 0.0, true);
		double ms = ElapsedMsSince(t0);
		if (h0 == nil)
		{
			probe.log("  nil（この PIO は使えない。飛ばす）");
			return;
		}
		const TXString widthParam = FindWidthParam(probe, h0);
		probe.log("  作成 " + FormatMs(ms) + " / " + DescribePio(h0, widthParam));
		if (widthParam.IsEmpty())
			return;
		const double defaultWidth = VWParametricObj(h0).GetParamReal(widthParam);
		const double wantWidth = defaultWidth + 500.0;
		probe.log("  欄=" + std::string(widthParam.GetStdString()) +
				  " 既定=" + FormatNum(defaultWidth) + " 書く値=" + FormatNum(wantWidth));
		// 基準: 既定値のまま ResetObject した外形（再生成済みの姿）
		t0 = ProbeClock::now();
		gSDK->ResetObject(h0);
		probe.log("  ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " +
				  DescribePio(h0, widthParam));

		MCObjectHandle layer = gSDK->GetActiveLayer();

		// 1) 今のやり方
		probe.log("[1] CreateCustomObject(bInsert=true) → 書く → ResetObject");
		t0 = ProbeClock::now();
		MCObjectHandle h1 = gSDK->CreateCustomObject(pioName, WorldPt(3000, baseY), 0.0, true);
		probe.log("  作成 " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePio(h1, widthParam));
		if (h1 != nil)
		{
			VWParametricObj(h1).SetParamReal(widthParam, wantWidth);
			probe.log("  書いた直後 / " + DescribePio(h1, widthParam));
			t0 = ProbeClock::now();
			gSDK->ResetObject(h1);
			probe.log("  ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " +
					  DescribePio(h1, widthParam));
		}

		// 2) bInsert=false
		probe.log("[2] CreateCustomObject(bInsert=false) → 書く → AddObjectToContainer → "
				  "ResetObject");
		t0 = ProbeClock::now();
		MCObjectHandle h2 = gSDK->CreateCustomObject(pioName, WorldPt(6000, baseY), 0.0, false);
		probe.log("  作成 " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePio(h2, widthParam));
		if (h2 != nil)
		{
			VWParametricObj(h2).SetParamReal(widthParam, wantWidth);
			probe.log("  書いた直後 / " + DescribePio(h2, widthParam));
			if (gSDK->ParentObject(h2) == nil)
			{
				const bool added = gSDK->AddObjectToContainer(h2, layer);
				probe.log(std::string("  AddObjectToContainer=") + (added ? "true" : "false") +
						  " / " + DescribePio(h2, widthParam));
			}
			t0 = ProbeClock::now();
			gSDK->ResetObject(h2);
			probe.log("  ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " +
					  DescribePio(h2, widthParam));
		}

		// 3) ByMatrixEx(bInsert=false)
		probe.log("[3] CreateCustomObjectByMatrixEx(bInsert=false) → 書く → "
				  "AddObjectToContainer → ResetObject");
		TransformMatrix mat;
		mat.SetToIdentity();
		mat.SetTrans(WorldPt3(9000, baseY, 0));
		t0 = ProbeClock::now();
		MCObjectHandle h3 = gSDK->CreateCustomObjectByMatrixEx(pioName, mat, false);
		probe.log("  作成 " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePio(h3, widthParam));
		if (h3 != nil)
		{
			VWParametricObj(h3).SetParamReal(widthParam, wantWidth);
			probe.log("  書いた直後 / " + DescribePio(h3, widthParam));
			if (gSDK->ParentObject(h3) == nil)
			{
				const bool added = gSDK->AddObjectToContainer(h3, layer);
				probe.log(std::string("  AddObjectToContainer=") + (added ? "true" : "false") +
						  " / " + DescribePio(h3, widthParam));
			}
			t0 = ProbeClock::now();
			gSDK->ResetObject(h3);
			probe.log("  ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " +
					  DescribePio(h3, widthParam));
		}

		// 4) 書式の既定値を書き換えてから作る（終わったら戻す）
		probe.log("[4] 書式の既定値を書き換える → CreateCustomObject(bInsert=true) → 戻す");
		{
			VWRecordFormatObj format = VWParametricObj(h0).GetRecordFormat();
			probe.log("  書式の既定（前）=" + FormatNum(format.GetParamReal(widthParam)));
			format.SetParamReal(widthParam, wantWidth);
			probe.log("  書式の既定（書いた後）=" + FormatNum(format.GetParamReal(widthParam)) +
					  " / 下見の個体 " + DescribePio(h0, widthParam));
			t0 = ProbeClock::now();
			MCObjectHandle h4 = gSDK->CreateCustomObject(pioName, WorldPt(12000, baseY), 0.0, true);
			probe.log("  作成 " + FormatMs(ElapsedMsSince(t0)) + " / " +
					  DescribePio(h4, widthParam));
			format.SetParamReal(widthParam, defaultWidth);
			probe.log("  書式の既定（戻した後）=" + FormatNum(format.GetParamReal(widthParam)) +
					  " / 作った個体 " + DescribePio(h4, widthParam));
			if (h4 != nil)
			{
				t0 = ProbeClock::now();
				gSDK->ResetObject(h4);
				probe.log("  （参考）ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " +
						  DescribePio(h4, widthParam));
			}
		}

		// 5) 点の PIO を Path の入口（doRegen=false）で作る
		probe.log("[5] CreateCustomObjectPath(name, nil, nil, doRegen=false) → 書く → "
				  "ResetObject");
		t0 = ProbeClock::now();
		MCObjectHandle h5 = gSDK->CreateCustomObjectPath(pioName, nil, nil, false);
		probe.log("  作成 " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribePio(h5, widthParam));
		if (h5 != nil)
		{
			VWParametricObj(h5).SetParamReal(widthParam, wantWidth);
			probe.log("  書いた直後 / " + DescribePio(h5, widthParam));
			t0 = ProbeClock::now();
			gSDK->ResetObject(h5);
			probe.log("  ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " +
					  DescribePio(h5, widthParam));
		}
	}
} // namespace

VW_PROBE("create-pio-with-params", "PIO をパラメータ指定・再生成 1 回で作れるか",
		 "ドア・窓を 5 経路（bInsert=false・書式の既定値・Path の doRegen=false 等）で作り、"
		 "作成時に再生成されたかを子の数と外形で測る")
{
	RunForPio(probe, "Door", 0);
	RunForPio(probe, "Window", 5000);
}
