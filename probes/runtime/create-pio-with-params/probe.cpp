//
//	probes/runtime/create-pio-with-params/probe.cpp
//
//	[issue #122] 組み込み PIO（ドア・窓）を「パラメータを指定した状態で、再生成 1 回で」
//	作れるか——その 4 回目。
//
//	3 回目で、ダイアログを出さない文書（DefineCustomObject(kCustomObjectPrefNever)）では
//	「CreateCustomObjectPath(nil, nil, doRegen=false) → 見本（CreateCustomObject で作った A0）の
//	全欄を SetParamValue で写す → 幅を書く → ResetObject」で外形は A と一致したが、線の種類・
//	太さの欄だけ 17 / 18 欄違った。SDK の実装（VWParametricObj.cpp）では線の種類・太さは
//	専用の欄型（kFieldPenStyle=26 / kFieldPenWeight=27）で、専用の口
//	（SetParamPenStyle / SetParamPenWeight）がある。
//
//	この回は欄型ごとの口で写し（型付きの写し）、次を確かめる:
//
//	  1) 写した直後（ResetObject の前）に A0 と全欄一致するか。違う欄は欄型つきで出す。
//	  2) 幅を書いて ResetObject した後、A（CreateCustomObject → 幅 → ResetObject）と
//	     全欄一致するか。外形・子の型の並びも比べる。
//	  3) 比べるために、文字列（SetParamValue）だけで写した場合も同じ手順で並べる。
//
//	**新規の空図面で走らせる。**ダイアログは出ない見込み（出たら OK で閉じる）。
//

#include "Probe.h"

#include "VWFC/Math/VWTransformMatrix.h"
#include "VWFC/VWObjects/VWParametricObj.h"

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

	// 外形（中心・大きさ）と子の型の並び。
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
		std::string types;
		for (MCObjectHandle m = gSDK->FirstMemberObj(h); m != nil; m = gSDK->NextObject(m))
		{
			if (!types.empty())
				types += ",";
			types += std::to_string(gSDK->GetObjectTypeN(m));
			++n;
		}
		s += " 子=" + std::to_string(n) + "[" + types + "]";
		return s;
	}

	// 全欄を文字列で突き合わせ、違う欄を欄型つきで最大 maxList 件まで並べる。
	size_t CompareInstances(::vwprobe::Report& probe, const char* la, VWParametricObj& a,
							const char* lb, VWParametricObj& b, size_t maxList)
	{
		const size_t n = a.GetParamsCount();
		size_t diff = 0;
		for (size_t i = 0; i < n; ++i)
		{
			const TXString name = a.GetParamName(i);
			const TXString va = a.GetParamValue(name);
			const TXString vb = b.GetParamValue(name);
			if (va == vb)
				continue;
			++diff;
			if (diff <= maxList)
				probe.log("      " + Utf8(name) + "（欄型 " +
						  std::to_string(static_cast<int>(a.GetParamStyle(name))) + "）: " + la +
						  "=" + Utf8(va) + " " + lb + "=" + Utf8(vb));
		}
		probe.log("    " + std::string(la) + " と " + lb + ": 全 " + std::to_string(n) +
				  " 欄のうち違うもの " + std::to_string(diff) + " 欄");
		return diff;
	}

	// 欄型ごとの口で写す。typed=false なら全欄を文字列（SetParamValue）で写す。
	void CopyAllParams(VWParametricObj& from, VWParametricObj& to, bool typed)
	{
		const size_t n = from.GetParamsCount();
		for (size_t i = 0; i < n; ++i)
		{
			const TXString name = from.GetParamName(i);
			const EFieldStyle style = typed ? from.GetParamStyle(name) : kFieldText;
			switch (style)
			{
			case kFieldPenStyle:
				to.SetParamPenStyle(name, from.GetParamPenStyle(name));
				break;
			case kFieldPenWeight:
				to.SetParamPenWeight(name, from.GetParamPenWeight(name));
				break;
			case kFieldFill:
				to.SetParamFill(name, from.GetParamFill(name));
				break;
			case kFieldColor:
				to.SetParamColor(name, from.GetParamColor(name));
				break;
			case kFieldClass:
				to.SetParamClass(name, from.GetParamClass(name));
				break;
			case kFieldBuildingMaterial:
				to.SetParamBuildingMaterial(name, from.GetParamBuildingMaterial(name));
				break;
			case kFieldTexture:
				to.SetParamTexture(name, from.GetParamTexture(name));
				break;
			case kFieldSymDef:
				to.SetParamSymDef(name, from.GetParamSymDef(name));
				break;
			default:
				to.SetParamValue(name, from.GetParamValue(name));
				break;
			}
		}
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

	void RunCopyCase(::vwprobe::Report& probe, const TXString& pioName, double x, double y,
					 bool typed, VWParametricObj& pa0, VWParametricObj& pa)
	{
		const char* label = typed ? "T" : "S";
		probe.log(std::string("[") + label + "] Path(doRegen=false) → A0 を" +
				  (typed ? "欄型ごとの口で" : "文字列（SetParamValue）で") +
				  "写す → 幅 → ResetObject");
		MCObjectHandle h = CreateByPathAt(pioName, x, y);
		if (h == nil)
		{
			probe.log("  作れなかった");
			return;
		}
		VWParametricObj pc(h);
		CopyAllParams(pa0, pc, typed);
		probe.log("  写した直後（ResetObject の前）");
		CompareInstances(probe, "A0", pa0, label, pc, 20);
		pc.SetParamReal("Width", kWantWidth);
		const auto t0 = ProbeClock::now();
		gSDK->ResetObject(h);
		probe.log("  ResetObject " + FormatMs(ElapsedMsSince(t0)) + " / " + DescribeShape(h));
		CompareInstances(probe, "A", pa, label, pc, 20);
	}

	void RunForPio(::vwprobe::Report& probe, const TXString& pioName, double baseY)
	{
		probe.log("");
		probe.log("===== " + Utf8(pioName) + " =====");

		auto t0 = ProbeClock::now();
		MCObjectHandle def = gSDK->DefineCustomObject(pioName, kCustomObjectPrefNever);
		probe.log("DefineCustomObject(kCustomObjectPrefNever) " + FormatMs(ElapsedMsSince(t0)) +
				  " 戻り=" + (def != nil ? "非 nil" : "nil"));

		// 見本 A0（何も書かない）と、基準 A（幅を書いて ResetObject）
		MCObjectHandle a0 = gSDK->CreateCustomObject(pioName, WorldPt(0, baseY), 0.0, true);
		MCObjectHandle a = gSDK->CreateCustomObject(pioName, WorldPt(3000, baseY), 0.0, true);
		if (a0 == nil || a == nil)
		{
			probe.log("  作れなかったので飛ばす");
			return;
		}
		VWParametricObj pa0(a0);
		VWParametricObj pa(a);
		pa.SetParamReal("Width", kWantWidth);
		gSDK->ResetObject(a);
		probe.log("[A0] CreateCustomObject（何も書かない） / " + DescribeShape(a0));
		probe.log("[A] CreateCustomObject → 幅 → ResetObject / " + DescribeShape(a));

		RunCopyCase(probe, pioName, 6000, baseY, true, pa0, pa);
		RunCopyCase(probe, pioName, 9000, baseY, false, pa0, pa);
	}
} // namespace

VW_PROBE("create-pio-with-params", "PIO をパラメータ指定・再生成 1 回で作れるか（4 回目）",
		 "新規の空図面で走らせる。ダイアログを出さない条件で、Path の入口で作った PIO へ"
		 "見本の全欄を欄型ごとの口で写すと、いつもの経路と全欄一致するかを測る")
{
	RunForPio(probe, "Door", 0);
	RunForPio(probe, "Window", 5000);
}
