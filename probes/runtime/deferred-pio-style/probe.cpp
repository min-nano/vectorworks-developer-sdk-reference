//
//	probes/runtime/deferred-pio-style/probe.cpp
//
//	[issue #245] PIO・プラグインスタイルの「書いた直後に読んで効かなかった」結論を、
//	Vectorworks へ制御を返した後に読み直す（2 回走らせる。仕立ては Deferred.h）。
//
//	読み直す結論（どれも同じ呼び出しの中で読んで出したもの。Findings「Parametric Objects」）:
//
//	  p1  バウンドを書いて ResetObject しない構造材は作り直されない（#81 の物差し）
//	  p2  RedrawRect は 1 本も作り直さない（#81。「この 2 つはもう試さない」）
//	  p3  ResetObject(レイヤ) は 1 本も作り直さない（#81。同上）
//	  p4  doRegen=false で作ると最初の ResetObject が「span ＋ 渡した長さ」に狂う（#81 / #109）
//	  p5  UpdateStyledObjects だけで作り直すと終端が「スタイルの span」だけずれる（#125）
//	  p6  UpdateStyledObjects を 2 度流すとずれが累積する（#125 F）
//	  p7  SetPluginObjectStyle だけではバウンドの記録も解決も変わらない（#112）
//	  p8  ResetObject ではスタイルの値が配られない（#118）
//	  p9  ResetObject の後に SetEntityMatrix で動かすと外接が古い位置のまま（#122）
//	  p10 doRegen=false で作ったドアは子が 1 つのまま（描かれない。#122）
//	  p11 undo の置き石（型 90）は ResetObject のたびに積もり、DeleteObject で消えない（#158/#166）
//
//	部材はケースごとに X をずらして置く（p2 の RedrawRect が他のケースへ掛からないように）。
//	p3 は専用のレイヤに置く（ResetObject(レイヤ) が他のケースへ掛からないように）。
//

#include "Deferred.h"

#include "VWFC/Math/VWTransformMatrix.h"
#include "VWFC/VWObjects/VWParametricObj.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
	using namespace VWFC::Math;
	using namespace VWFC::VWObjects;

	// バウンド（LayerElevation の offset）。span 3000 なら「作り直された」。
	const double kPsLow = 2500;
	const double kPsHigh = 5500;
	const double kPsSeed = 1; // 作るときに渡すパスの長さ（作り直されなければこのまま）

	// スタイル側とインスタンス側で見分けの付く値（#118 / #125 と同じ）。
	const char* const kPsStyleID = "STYLE-GAWA";
	const char* const kPsInstID = "HON-GOTO";
	const double kPsStyleBreadth = 300;
	const double kPsInstBreadth = 120;
	const char* const kPsBoundParam = "DialogStartElevationReference";

	std::string PsStr(const TXString& s)
	{
		return static_cast<const char*>(s);
	}

	bool PsBound(MCObjectHandle pio, short id, double offset)
	{
		MockUp::SStoryObjectData data;
		data.fBound = MockUp::eStoryObjectBound_LayerElevation;
		data.fBoundStory = 0;
		data.fLayerLevelType = "";
		data.fOffset = offset;
		return gSDK->SetObjectStoryBound(pio, static_cast<MockUp::TObjectBoundID>(id), data);
	}

	// 構造材 1 本。パスは (x,y,0)→(x+dx, y, dz)。
	MCObjectHandle PsMember(double x, double y, double dx, double dz, bool doRegen)
	{
		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(x, y, 0), false, 1);
		if (curve == nullptr)
			return nullptr;
		gSDK->Add3DVertex(curve, WorldPt3(x + dx, y, dz), true);
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, WorldPt3(x, y, 0));
			gSDK->NurbsSetPt3D(curve, 0, 1, WorldPt3(x + dx, y, dz));
		}
		MCObjectHandle noProfile = nullptr;
		return gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile, doRegen);
	}

	// 構造材の読み。dz（パスの Z 差）・挿入点 Z・記録・解決・スタイル・値・子・外接。
	DeferredReadings PsReadMember(const std::string& name)
	{
		MCObjectHandle pio = DfFind(name);
		if (pio == nullptr)
			return {{name, "(nil)"}};
		DeferredReadings rs;
		std::string dz = "(パス無し)";
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path != nullptr)
		{
			WorldPt3 p0, p1;
			if (gSDK->NurbsGetPt3D(path, 0, 0, p0) && gSDK->NurbsGetPt3D(path, 0, 1, p1))
				dz = DfNum(p1.z - p0.z, 6);
		}
		TransformMatrix m;
		gSDK->GetEntityMatrix(pio, m);
		rs.emplace_back(name + ".dz", dz);
		rs.emplace_back(name + ".z0", DfNum(m.P().z, 3));

		std::string rec, res;
		for (int id = 0; id < 2; ++id)
		{
			MockUp::SStoryObjectData data;
			data.fBound = MockUp::eStoryObjectBound_LayerElevation;
			data.fBoundStory = 0;
			data.fLayerLevelType = "";
			data.fOffset = 0;
			const auto bid = static_cast<MockUp::TObjectBoundID>(id);
			rec += (id ? "/" : "") + (gSDK->GetObjectStoryBound(pio, bid, data)
										  ? DfNum(data.fOffset, 3)
										  : std::string("-"));
			res += (id ? "/" : "") + DfNum(gSDK->GetObjectBoundElevation(pio, bid), 3);
		}
		rs.emplace_back(name + ".rec", rec);
		rs.emplace_back(name + ".res", res);

		RefNumber styleRef = 0;
		gSDK->GetPluginObjectStyle(pio, styleRef);
		VWParametricObj obj(pio);
		rs.emplace_back(name + ".val", "style=" + std::to_string(styleRef) +
										   " id=" + PsStr(obj.GetParamValue("MemberID")) +
										   " br=" + DfNum(obj.GetParamReal("MajorBreadth"), 1));
		rs.emplace_back(name + ".kids", DfChildren(pio));
		rs.emplace_back(name + ".bnd", DfBounds(pio));
		return rs;
	}

	DeferredReadings PsReadAll(const std::vector<std::string>& names)
	{
		DeferredReadings out;
		for (const std::string& n : names)
			for (auto& kv : PsReadMember(n))
				out.push_back(kv);
		return out;
	}

	// プラグインスタイル 1 本（#98 / #125 の手順。CreatePluginStyle は呼ばない）。
	RefNumber PsMakeStyle(vwprobe::Report& probe, const std::string& label, bool boundByInstance)
	{
		MCObjectHandle seed = PsMember(-20000, 0, 2000, 0, true);
		if (seed == nullptr)
		{
			probe.fail(label + ": スタイルの元の部材を作れない");
			return 0;
		}
		PsBound(seed, 0, kPsLow);
		PsBound(seed, 1, kPsHigh);
		VWParametricObj seedObj(seed);
		seedObj.SetParamString("MemberID", kPsStyleID);
		seedObj.SetParamReal("MajorBreadth", kPsStyleBreadth);
		gSDK->ResetObject(seed);

		MCObjectHandle symDef = nullptr;
		for (int attempt = 1; attempt <= 60 && symDef == nullptr; ++attempt)
		{
			TXString name(("df245 " + label + " " + std::to_string(attempt)).c_str());
			symDef = gSDK->CreateSymbolDefinition(name);
		}
		if (symDef == nullptr)
		{
			probe.fail(label + ": シンボル定義を作れない");
			return 0;
		}
		gSDK->AddObjectToContainer(seed, symDef);
		gSDK->ResetObject(symDef); // サブタイプを書く前に通す（#98）
		gSDK->SetSymbolDefSubType(symDef,
								  static_cast<Sint32>(VWParametricObj::GetInternalID(seed)));
		gSDK->SetAllPluginStyleParameters(symDef, kPluginStyleParameter_ByStyle);
		if (boundByInstance)
			gSDK->SetPluginStyleParameterType(symDef, kPsBoundParam,
											  kPluginStyleParameter_ByInstance);
		const RefNumber ref = static_cast<RefNumber>(gSDK->GetObjectInternalIndex(symDef));
		probe.log("  スタイル " + label + ": IsPluginStyle=" + DfBool(gSDK->IsPluginStyle(symDef)) +
				  " ref=" + std::to_string(ref) + " バウンド " +
				  (boundByInstance ? "by-instance" : "by-style"));
		return gSDK->IsPluginStyle(symDef) ? ref : 0;
	}

	// スタイルを当てる本（#125 の MakeBar）。ResetObject はしない。
	MCObjectHandle PsBar(double x, double low, double high, RefNumber style)
	{
		MCObjectHandle pio = PsMember(x, 0, 2000, 0, true);
		if (pio == nullptr)
			return nullptr;
		PsBound(pio, 0, low);
		PsBound(pio, 1, high);
		VWParametricObj obj(pio);
		obj.SetParamString("MemberID", kPsInstID);
		obj.SetParamReal("MajorBreadth", kPsInstBreadth);
		gSDK->SetPluginObjectStyle(pio, style);
		return pio;
	}

	std::string PsMade(vwprobe::Report& probe, MCObjectHandle h, const std::string& name)
	{
		if (h == nullptr)
		{
			probe.fail(name + " を作れない（nil）");
			return name;
		}
		DfSetName(probe, h, name);
		return name;
	}
} // namespace

VW_PROBE("deferred-pio-style", "PIO・スタイルを戻った後に読み直す",
		 "2 回走らせて T0/T1/T2 を比べる")
{
	std::vector<DeferredCase> cases;

	cases.push_back({"p1", "バウンドを書いて ResetObject しない（作られた後は何も呼ばない）",
					 [](vwprobe::Report& probe)
					 {
						 // 定義は図面ごと。ハーネスが開いた自分の図面で、何かを作る前に 1 度
						 gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
						 gSDK->DefineCustomObject("Door", kCustomObjectPrefNever);
						 MCObjectHandle m = PsMember(0, 0, 0, kPsSeed, true);
						 if (m != nullptr)
						 {
							 PsBound(m, 0, kPsLow);
							 PsBound(m, 1, kPsHigh);
						 }
						 PsMade(probe, m, "ps-p1");
					 },
					 []() { return PsReadAll({"ps-p1"}); }});

	cases.push_back({"p2", "置いただけの 2 本へ RedrawRect（その範囲だけ）",
					 [](vwprobe::Report& probe)
					 {
						 for (int i = 0; i < 2; ++i)
						 {
							 MCObjectHandle m = PsMember(3000 + i * 500, 0, 0, kPsSeed, true);
							 if (m != nullptr)
							 {
								 PsBound(m, 0, kPsLow);
								 PsBound(m, 1, kPsHigh);
							 }
							 PsMade(probe, m, "ps-p2-" + std::to_string(i));
						 }
						 gSDK->RedrawRect(WorldRect(2500, 1000, 4000, -1000));
					 },
					 []() { return PsReadAll({"ps-p2-0", "ps-p2-1"}); }});

	cases.push_back({"p3", "専用のレイヤに置いただけの 2 本へ ResetObject(レイヤ)",
					 [](vwprobe::Report& probe)
					 {
						 MCObjectHandle home = gSDK->GetCurrentLayer();
						 MCObjectHandle layer = gSDK->CreateLayer("df245-p3", kLayerDesign);
						 if (layer == nullptr)
						 {
							 probe.fail("p3: レイヤを作れない");
							 return;
						 }
						 gSDK->SetCurrentLayerN(layer, true); // 以降の生成はこのレイヤへ
						 for (int i = 0; i < 2; ++i)
						 {
							 MCObjectHandle m = PsMember(6000 + i * 500, 0, 0, kPsSeed, true);
							 if (m != nullptr)
							 {
								 PsBound(m, 0, kPsLow);
								 PsBound(m, 1, kPsHigh);
							 }
							 PsMade(probe, m, "ps-p3-" + std::to_string(i));
						 }
						 gSDK->ResetObject(layer);
						 if (home != nullptr)
							 gSDK->SetCurrentLayerN(home, true);
					 },
					 []() { return PsReadAll({"ps-p3-0", "ps-p3-1"}); }});

	cases.push_back({"p4", "doRegen=false で作り、バウンドを書いて ResetObject 1 回",
					 [](vwprobe::Report& probe)
					 {
						 MCObjectHandle m = PsMember(9000, 0, 0, kPsSeed, false);
						 if (m != nullptr)
						 {
							 PsBound(m, 0, kPsLow);
							 PsBound(m, 1, kPsHigh);
							 gSDK->ResetObject(m);
						 }
						 PsMade(probe, m, "ps-p4");
						 // 何も呼ばずに放っておく doRegen=false の 1 本（後から作り直されるか）
						 MCObjectHandle n = PsMember(9500, 0, 0, kPsSeed, false);
						 if (n != nullptr)
						 {
							 PsBound(n, 0, kPsLow);
							 PsBound(n, 1, kPsHigh);
						 }
						 PsMade(probe, n, "ps-p4n");
					 },
					 []() { return PsReadAll({"ps-p4", "ps-p4n"}); }});

	cases.push_back(
		{"p5", "バウンド by-instance のスタイル。ResetObject せず UpdateStyledObjects を 1 回",
		 [](vwprobe::Report& probe)
		 {
			 const RefNumber style = PsMakeStyle(probe, "p5", true);
			 if (style == 0)
				 return;
			 PsMade(probe, PsBar(12000, 3000, 3000, style), "ps-p5-u1");
			 PsMade(probe, PsBar(12500, 1000, 6500, style), "ps-p5-u3");
			 MCObjectHandle r = PsBar(13000, 1000, 6500, style);
			 if (r != nullptr)
				 gSDK->ResetObject(r);
			 PsMade(probe, r, "ps-p5-r3");
			 gSDK->UpdateStyledObjects(style);
			 probe.log(
				 "  予測（#125）: u1 dz=-3000・u3 dz=+2500（差分仮説）／ r3 dz=5500（正しい）");
		 },
		 []() { return PsReadAll({"ps-p5-u1", "ps-p5-u3", "ps-p5-r3"}); }});

	cases.push_back({"p6", "同じ形で UpdateStyledObjects を 2 度流す（累積）",
					 [](vwprobe::Report& probe)
					 {
						 const RefNumber style = PsMakeStyle(probe, "p6", true);
						 if (style == 0)
							 return;
						 PsMade(probe, PsBar(15000, 1000, 6500, style), "ps-p6-u3");
						 gSDK->UpdateStyledObjects(style);
						 gSDK->UpdateStyledObjects(style);
						 probe.log("  予測（#125）: 累積なら dz=5000（2500＋2500）");
					 },
					 []() { return PsReadAll({"ps-p6-u3"}); }});

	cases.push_back({"p7", "バウンド by-style のスタイルを SetPluginObjectStyle で当てるだけ",
					 [](vwprobe::Report& probe)
					 {
						 const RefNumber style = PsMakeStyle(probe, "p7", false);
						 if (style == 0)
							 return;
						 PsMade(probe, PsBar(18000, 1000, 6500, style), "ps-p7");
						 probe.log(
							 "  予測（#112）: 記録 1000/6500 のまま・値は HON-GOTO/120 のまま");
					 },
					 []() { return PsReadAll({"ps-p7"}); }});

	cases.push_back({"p8", "スタイルを当てて ResetObject だけ（UpdateStyledObjects なし）",
					 [](vwprobe::Report& probe)
					 {
						 const RefNumber style = PsMakeStyle(probe, "p8", true);
						 if (style == 0)
							 return;
						 MCObjectHandle b = PsBar(21000, 1000, 6500, style);
						 if (b != nullptr)
							 gSDK->ResetObject(b);
						 PsMade(probe, b, "ps-p8");
						 probe.log("  予測（#118）: 値は HON-GOTO/120 のまま（配られない）");
					 },
					 []() { return PsReadAll({"ps-p8"}); }});

	cases.push_back({"p9", "ドアを ResetObject の後に SetEntityMatrix で +5000 動かす（外接）",
					 [](vwprobe::Report& probe)
					 {
						 MCObjectHandle d =
							 gSDK->CreateCustomObject("Door", WorldPt(24000, 0), 0.0, true);
						 if (d != nullptr)
						 {
							 gSDK->ResetObject(d);
							 probe.log("  動かす前の外接: " + DfBounds(d));
							 VWTransformMatrix place;
							 place.SetOffset(VWPoint3D(29000, 0, 0));
							 gSDK->SetEntityMatrix(d, place);
						 }
						 PsMade(probe, d, "ps-p9");
						 probe.log("  予測（#122）: 外接は古い位置（X≈24000）のまま");
					 },
					 []()
					 {
						 MCObjectHandle d = DfFind("ps-p9");
						 TransformMatrix m;
						 if (d != nullptr)
							 gSDK->GetEntityMatrix(d, m);
						 return DeferredReadings{{"ps-p9.bnd", DfBounds(d)},
												 {"ps-p9.x", d ? DfNum(m.P().x, 1) : "(nil)"},
												 {"ps-p9.kids", DfChildren(d)}};
					 }});

	cases.push_back(
		{"p10", "ドアを CreateCustomObjectPath(doRegen=false) で作って位置だけ置く",
		 [](vwprobe::Report& probe)
		 {
			 MCObjectHandle d = gSDK->CreateCustomObjectPath("Door", nullptr, nullptr, false);
			 if (d != nullptr)
			 {
				 VWTransformMatrix place;
				 place.SetOffset(VWPoint3D(33000, 0, 0));
				 gSDK->SetEntityMatrix(d, place);
			 }
			 PsMade(probe, d, "ps-p10");
			 probe.log("  予測（#122）: 子は 1 つのまま（描き直されない）");
		 },
		 []()
		 {
			 MCObjectHandle d = DfFind("ps-p10");
			 return DeferredReadings{{"ps-p10.kids", DfChildren(d)}, {"ps-p10.bnd", DfBounds(d)}};
		 }});

	cases.push_back(
		{"p11", "構造材へ ResetObject を 5 回 → 型 90 を 1 つ DeleteObject(useUndo=false)",
		 [](vwprobe::Report& probe)
		 {
			 MCObjectHandle m = PsMember(36000, 0, 2000, 0, true);
			 if (m != nullptr)
			 {
				 PsBound(m, 0, kPsLow);
				 PsBound(m, 1, kPsLow);
				 for (int i = 0; i < 5; ++i)
					 gSDK->ResetObject(m);
				 probe.log("  ResetObject 5 回の後: " + DfChildren(m));
				 for (MCObjectHandle c = gSDK->FirstMemberObj(m); c != nullptr;
					  c = gSDK->NextObject(c))
				 {
					 if (gSDK->GetObjectTypeN(c) == 90)
					 {
						 gSDK->DeleteObject(c, false);
						 break;
					 }
				 }
			 }
			 PsMade(probe, m, "ps-p11");
			 probe.log("  予測（#166）: 型 90 は減らない");
		 },
		 []() { return DeferredReadings{{"ps-p11.kids", DfChildren(DfFind("ps-p11"))}}; }});

	RunDeferred(probe, "deferred-pio-style", cases);
}
