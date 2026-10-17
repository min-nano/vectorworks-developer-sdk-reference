//
//	probes/runtime/deferred-pio-attrs/probe.cpp
//
//	[issue #245] 構造材 PIO の 2 つの結論を、VW へ戻った後に読み直す。
//
//	A（#158 / #166）AttributesMode 0〜3 は描画を 1 か所も変えない
//	  （子は幾何 9 のまま・per-part のマゼンタが出続ける）。
//	  あわせて #158 の面の目印（平面図で使われるのは _Below・_At は効かない）。
//	B（#94 / #104）文書の既定のマーカー by-class は PIO の regen で下りる。
//	  doRegen=false の作成では下りない。他の 6 旗は全区間 yes。
//	  最後に doRegen=false のまま作り直さない PIO を 1 本置き、後から
//	  regen されて旗が下りるかを見る。
//
//	旧プローブの手順をそのまま写し、setup の中で旧い結論が期待する値を
//	並べてログに出す（T0 が旧い結論を再現しているかを先に確かめるため）。
//	子の型 90（置き石）は ResetObject 1 回ごとに 1 つ積もるので、
//	T0 と T2 の差は「その間に作り直されたか」の目印になる。
//

#include "Deferred.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kPaPio = "StructuralMember";
	const char* const kPaClass = "pa-245-yellow";
	const char* const kPaFaces[] = {"_Above", "_At", "_Below"};

	// ---------------------------------------------------------------------------
	// 小道具

	std::string PaText(const TXString& src)
	{
		const char* utf8 = static_cast<const char*>(src);
		return utf8 ? std::string(utf8) : std::string();
	}

	std::string PaHex(const CRGBColor& c)
	{
		char buf[16];
		std::snprintf(buf, sizeof buf, "%02X%02X%02X", static_cast<unsigned>(c.GetRed()),
					  static_cast<unsigned>(c.GetGreen()), static_cast<unsigned>(c.GetBlue()));
		return buf;
	}

	std::string PaPat(const VWPattern& p)
	{
		if (p.IsNonePattern())
			return "none";
		if (p.IsSolidPattern())
			return "solid";
		return std::to_string(static_cast<long>(static_cast<InternalIndex>(p)));
	}

	std::string PaClassOf(MCObjectHandle h)
	{
		TXString name;
		gSDK->ClassIDToName(gSDK->GetObjectClass(h), name);
		return PaText(name);
	}

	bool PaIsDrawn(short type)
	{
		return type == 5 || type == 21 || type == 84;
	}

	// 置き石（90）と終端（0）を除いた幾何の内訳（#166 の数え方）。
	std::string PaGeometry(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		std::map<short, int> byType;
		int n = 0;
		for (MCObjectHandle c = gSDK->FirstMemberObj(h); c != nullptr; c = gSDK->NextObject(c))
		{
			const short t = gSDK->GetObjectTypeN(c);
			if (t == 0 || t == 90)
				continue;
			++n;
			++byType[t];
		}
		std::string s = std::to_string(n) + " [";
		bool first = true;
		for (const auto& [t, k] : byType)
		{
			s += (first ? "" : " ") + std::to_string(t) + ":" + std::to_string(k);
			first = false;
		}
		return s + "]";
	}

	// 描かれた子 1 つ（#158 のプローブが目印に使った欄）。
	// 比べる表で切られないよう 90 文字前後に詰める。
	std::string PaChildLine(MCObjectHandle child)
	{
		VWObject wrapper(child);
		const VWObjectAttr& attr = wrapper.GetObjectAttribs();
		std::string s = "t" + std::to_string(gSDK->GetObjectTypeN(child));
		s += " f=" + PaPat(attr.GetFillPattern()) + "/" + PaHex(attr.GetFillForeColor());
		s += " p=" + PaPat(attr.GetPenPattern()) + "/" + PaHex(attr.GetPenForeColor());
		s += " w=" + std::to_string(attr.GetLineWeightInMils());
		s += std::string(" bc=") + (attr.GetFillColorByClass() ? "y" : "n") +
			 (attr.GetPenColorByClass() ? "y" : "n");
		s += " c=" + PaClassOf(child);
		WorldRect r;
		if (gSDK->GetObjectBounds(child, r))
			s += " " + DfNum(r.left, 0) + "," + DfNum(r.top, 0) + "," + DfNum(r.right, 0) + "," +
				 DfNum(r.bottom, 0);
		return s;
	}

	std::vector<MCObjectHandle> PaDrawnChildren(MCObjectHandle pio)
	{
		std::vector<MCObjectHandle> out;
		if (pio == nullptr)
			return out;
		for (MCObjectHandle c = gSDK->FirstMemberObj(pio); c != nullptr; c = gSDK->NextObject(c))
			if (PaIsDrawn(gSDK->GetObjectTypeN(c)))
				out.push_back(c);
		return out;
	}

	// 描かれた子の要約: 塗りの色（solid のもの）・線の色・太さ（並べ替えて）。
	std::string PaDrawnSummary(MCObjectHandle pio)
	{
		std::vector<std::string> fills;
		std::vector<std::string> pens;
		std::vector<int> weights;
		for (MCObjectHandle c : PaDrawnChildren(pio))
		{
			if (gSDK->GetObjectTypeN(c) == 84)
				continue;
			VWObject wrapper(c);
			const VWObjectAttr& attr = wrapper.GetObjectAttribs();
			if (attr.GetFillPattern().IsSolidPattern())
				fills.push_back(PaHex(attr.GetFillForeColor()));
			pens.push_back(PaHex(attr.GetPenForeColor()));
			weights.push_back(attr.GetLineWeightInMils());
		}
		std::sort(fills.begin(), fills.end());
		std::sort(pens.begin(), pens.end());
		std::sort(weights.begin(), weights.end());
		std::string s = "fill=[";
		for (size_t i = 0; i < fills.size(); ++i)
			s += (i ? "," : "") + fills[i];
		s += "] pen=[";
		for (size_t i = 0; i < pens.size(); ++i)
			s += (i ? "," : "") + pens[i];
		s += "] w=[";
		for (size_t i = 0; i < weights.size(); ++i)
			s += (i ? "," : "") + std::to_string(weights[i]);
		return s + "]";
	}

	std::string PaParam(MCObjectHandle h, const char* name)
	{
		if (h == nullptr || gSDK->GetObjectTypeN(h) != 86)
			return "(not pio)";
		VWParametricObj pio(h);
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
			return "(no param)";
		return PaText(pio.GetParamValue(index));
	}

	// 期待（旧い結論）と並べてログに出す。
	void PaExpect(vwprobe::Report& probe, const std::string& label, const std::string& actual,
				  const std::string& expected)
	{
		probe.log("    " + label + ": " + actual + "（旧: " + expected + "）" +
				  (actual == expected ? "" : " ★不一致"));
	}

	// ---------------------------------------------------------------------------
	// A: 構造材の作り方（#158 / #166 の旧プローブそのまま。
	// 2D の線をパスにして doRegen=false で作る）

	MCObjectHandle PaMakeMember(vwprobe::Report& probe, const std::string& tag, double originY)
	{
		VWPolygon2DObj path({VWPoint2D(0.0, originY), VWPoint2D(3000.0, originY)});
		MCObjectHandle pathHandle = path.GetThisObject();
		if (pathHandle == nullptr)
		{
			probe.fail(tag + " パスを作れなかった");
			return nullptr;
		}
		MCObjectHandle member = gSDK->CreateCustomObjectPath(kPaPio, pathHandle, nullptr, false);
		if (member == nullptr)
			probe.fail(tag + " CreateCustomObjectPath が nil");
		return member;
	}

	void PaWriteFaces(vwprobe::Report& probe, VWParametricObj& pio, const std::string& base,
					  const std::string& above, const std::string& at, const std::string& below)
	{
		const std::string values[] = {above, at, below};
		for (size_t f = 0; f < 3; ++f)
		{
			const std::string name = base + kPaFaces[f];
			const size_t index = pio.GetParamIndex(TXString(name.c_str()));
			if (index == size_t(-1) || index >= pio.GetParamsCount())
			{
				probe.log("注意: " + name + " を名前で引けなかった");
				continue;
			}
			pio.SetParamValue(index, TXString(values[f].c_str()));
		}
	}

	void PaWriteFacesColor(vwprobe::Report& probe, VWParametricObj& pio, const std::string& base,
						   const CRGBColor& above, const CRGBColor& at, const CRGBColor& below)
	{
		const CRGBColor* values[] = {&above, &at, &below};
		for (size_t f = 0; f < 3; ++f)
		{
			const std::string name = base + kPaFaces[f];
			const size_t index = pio.GetParamIndex(TXString(name.c_str()));
			if (index == size_t(-1) || index >= pio.GetParamsCount())
			{
				probe.log("注意: " + name + " を名前で引けなかった");
				continue;
			}
			pio.SetParamColor(index, values[f]->GetColorIndex());
		}
	}

	std::string PaFacesRead(MCObjectHandle h, const std::string& base)
	{
		std::string s;
		for (size_t f = 0; f < 3; ++f)
			s += (f ? "/" : "") + PaParam(h, (base + kPaFaces[f]).c_str());
		return s;
	}

	const char* const kPaGeomOld = "9 [5:2 11:2 21:4 84:1]";

	void PaSetupModes(vwprobe::Report& probe)
	{
		gSDK->DefineCustomObject(kPaPio, kCustomObjectPrefNever);

		// クラスは塗り・線とも黄・太さ 99（#158 第 4 版の G0 そのまま）。
		const InternalIndex probeClass = gSDK->AddClass(TXString(kPaClass));
		const CRGBColor kYellow(Uint8(255), Uint8(255), Uint8(0));
		const CRGBColor kMagenta(Uint8(255), Uint8(0), Uint8(255));
		if (probeClass == 0)
		{
			probe.fail("[am] AddClass が 0 を返した");
			return;
		}
		{
			VWClass classObj(probeClass);
			classObj.SetUseGraphics(true);
			VWClassAttr classAttr = classObj.GetClassAttribs();
			VWPattern solidFill(true);
			solidFill.SetSolidPattern();
			classAttr.SetFillPattern(solidFill);
			classAttr.SetFillForeColor(kYellow);
			classAttr.SetFillBackColor(kYellow);
			classAttr.SetPenForeColor(kYellow);
			classAttr.SetLineWeightInMils(99);
			probe.log("  クラス " + std::string(kPaClass) +
					  " 塗=" + PaPat(classAttr.GetFillPattern()) + "/" +
					  PaHex(classAttr.GetFillForeColor()) +
					  " 線=" + PaHex(classAttr.GetPenForeColor()) +
					  " 太=" + std::to_string(classAttr.GetLineWeightInMils()));
		}

		// #158 第 4 版 [I]: per-part のクラス欄は空のまま、PIO 自身をクラスへ入れ、
		// per-part の色をマゼンタにして AttributesMode を 1 値 1 個体で振る。
		for (int mode = 0; mode <= 3; ++mode)
		{
			const std::string tag = "[am] mode=" + std::to_string(mode);
			MCObjectHandle member = PaMakeMember(probe, tag, 6000.0 + 1000.0 * double(mode));
			if (member == nullptr)
				continue;
			gSDK->SetObjectClass(member, probeClass);
			VWParametricObj pio(member);
			PaWriteFacesColor(probe, pio, "MemberFillColor", kMagenta, kMagenta, kMagenta);
			PaWriteFacesColor(probe, pio, "MemberPenColor", kMagenta, kMagenta, kMagenta);
			const size_t index = pio.GetParamIndex(TXString("AttributesMode"));
			if (index != size_t(-1) && index < pio.GetParamsCount())
				pio.SetParamValue(index, TXString(std::to_string(mode).c_str()));
			else
				probe.fail(tag + " AttributesMode を名前で引けなかった");
			gSDK->ResetObject(member);
			DfSetName(probe, member, "pa-m" + std::to_string(mode));

			probe.log("  " + tag + " 子=" + DfChildren(member));
			PaExpect(probe, "AttributesMode 読み戻し", PaParam(member, "AttributesMode"),
					 std::to_string(mode));
			PaExpect(probe, "PIO のクラス", PaClassOf(member), kPaClass);
			PaExpect(probe, "幾何", PaGeometry(member), kPaGeomOld);
			const std::string sum = PaDrawnSummary(member);
			probe.log("    描かれた子: " + sum);
			const bool magenta = sum.find("FF00FF") != std::string::npos;
			const bool yellow = sum.find("FFFF00") != std::string::npos;
			PaExpect(probe, "マゼンタあり/黄あり",
					 std::string(magenta ? "yes" : "no") + "/" + (yellow ? "yes" : "no"), "yes/no");
			for (MCObjectHandle c : PaDrawnChildren(member))
				probe.log("    " + PaChildLine(c));
		}
	}

	// #158 で「平面図で使われるのは _Below」と決めた目印の 1 本。
	// 旧プローブは手元に無いので Findings の表から組み立て直した。
	// 3 面に別々の値を入れて 1 回だけ描く。
	void PaSetupFaces(vwprobe::Report& probe)
	{
		gSDK->DefineCustomObject(kPaPio, kCustomObjectPrefNever);
		MCObjectHandle member = PaMakeMember(probe, "[af]", 15000.0);
		if (member == nullptr)
			return;
		{
			VWParametricObj pio(member);
			PaWriteFaces(probe, pio, "MemberLineWeight", "55", "56", "57");
			PaWriteFaces(probe, pio, "CenterlineLineWeight", "58", "59", "60");
			PaWriteFaces(probe, pio, "CapsLineWeight", "61", "62", "63");
			PaWriteFacesColor(probe, pio, "MemberFillColor",
							  CRGBColor(Uint8(255), Uint8(0), Uint8(0)),
							  CRGBColor(Uint8(0), Uint8(255), Uint8(0)),
							  CRGBColor(Uint8(0), Uint8(0), Uint8(255)));
		}
		gSDK->ResetObject(member);
		DfSetName(probe, member, "pa-f");
		probe.log("  [af] 子=" + DfChildren(member));
		probe.log("    MemberLineWeight 読み戻し=" + PaFacesRead(member, "MemberLineWeight"));
		probe.log("    CenterlineLineWeight 読み戻し=" +
				  PaFacesRead(member, "CenterlineLineWeight"));
		probe.log("    CapsLineWeight 読み戻し=" + PaFacesRead(member, "CapsLineWeight"));
		probe.log("    MemberFillColor 読み戻し=" + PaFacesRead(member, "MemberFillColor"));
		PaExpect(probe, "幾何", PaGeometry(member), kPaGeomOld);
		const std::string sum = PaDrawnSummary(member);
		probe.log("    描かれた子: " + sum);
		probe.log("    旧: 断面の塗り 0000FF（_Below）・中心線 60・稜線 57・端部 63。"
				  "_Above/_At の値（55 56 58 59 61 62・赤・緑）は出ない");
		bool sawAboveAt = false;
		for (const char* w : {"55", "56", "58", "59", "61", "62"})
			sawAboveAt = sawAboveAt || sum.find(std::string(",") + w + ",") != std::string::npos ||
						 sum.find(std::string("[") + w + ",") != std::string::npos ||
						 sum.find(std::string(",") + w + "]") != std::string::npos ||
						 sum.find(std::string("[") + w + "]") != std::string::npos;
		const bool blue = sum.find("0000FF") != std::string::npos;
		const bool redGreen =
			sum.find("FF0000") != std::string::npos || sum.find("00FF00") != std::string::npos;
		PaExpect(probe, "青あり/赤緑あり/_Above・_At の太さあり",
				 std::string(blue ? "yes" : "no") + "/" + (redGreen ? "yes" : "no") + "/" +
					 (sawAboveAt ? "yes" : "no"),
				 "yes/no/no");
		for (MCObjectHandle c : PaDrawnChildren(member))
			probe.log("    " + PaChildLine(c));
	}

	DeferredReadings PaMeasureMember(const std::string& prefix, const std::string& name,
									 bool withMode)
	{
		DeferredReadings rs;
		MCObjectHandle h = DfFind(name);
		rs.emplace_back(prefix + ".kids", DfChildren(h));
		if (h == nullptr)
			return rs;
		if (withMode)
			rs.emplace_back(prefix + ".mode", PaParam(h, "AttributesMode"));
		rs.emplace_back(prefix + ".cls", PaClassOf(h));
		rs.emplace_back(prefix + ".bb", DfBounds(h));
		rs.emplace_back(prefix + ".sum", PaDrawnSummary(h));
		int i = 0;
		for (MCObjectHandle c : PaDrawnChildren(h))
		{
			if (i >= 10)
				break;
			rs.emplace_back(prefix + ".d" + std::to_string(i++), PaChildLine(c));
		}
		return rs;
	}

	// ---------------------------------------------------------------------------
	// B: 文書の既定の by-class 7 旗（#104 の旧プローブそのまま）

	std::string PaYn(Boolean v)
	{
		return v != 0 ? "y" : "n";
	}

	std::string PaOpacity(Boolean pen, Boolean fill)
	{
		if (pen != 0 && fill != 0)
			return "y";
		if (pen == 0 && fill == 0)
			return "n";
		return pen != 0 ? "p" : "f";
	}

	std::string PaDocFlags()
	{
		Boolean pen = 0;
		Boolean fill = 0;
		gSDK->GetDefaultOpacityByClassN(pen, fill);
		return "PC=" + PaYn(gSDK->GetDefaultPColorsByClass()) +
			   " FC=" + PaYn(gSDK->GetDefaultFColorsByClass()) +
			   " LW=" + PaYn(gSDK->GetDefaultLWByClass()) +
			   " PP=" + PaYn(gSDK->GetDefaultPPatByClass()) +
			   " FP=" + PaYn(gSDK->GetDefaultFPatByClass()) + " Op=" + PaOpacity(pen, fill) +
			   " Ar=" + PaYn(gSDK->GetDefaultArrowByClass());
	}

	std::string PaObjFlags(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		Boolean pen = 0;
		Boolean fill = 0;
		gSDK->GetOpacityByClassN(h, pen, fill);
		return "PC=" + PaYn(gSDK->GetPColorsByClass(h)) +
			   " FC=" + PaYn(gSDK->GetFColorsByClass(h)) + " LW=" + PaYn(gSDK->GetLWByClass(h)) +
			   " PP=" + PaYn(gSDK->GetPPatByClass(h)) + " FP=" + PaYn(gSDK->GetFPatByClass(h)) +
			   " Op=" + PaOpacity(pen, fill) + " Ar=" + PaYn(gSDK->GetArrowByClass(h));
	}

	const char* const kPaAllYes = "PC=y FC=y LW=y PP=y FP=y Op=y Ar=y";
	const char* const kPaArrowNo = "PC=y FC=y LW=y PP=y FP=y Op=y Ar=n";

	int gPaSerial = 0;

	MCObjectHandle PaMakeRect()
	{
		const WorldCoord x = static_cast<WorldCoord>(gPaSerial++) * 1000;
		WorldRect bounds(x, 1000, x + 1000, 0);
		return gSDK->CreateRectangle(bounds);
	}

	// #104 の作り方（鉛直材の作法: NurbsCurve ＋ Add3DVertex）。doRegen を選べる。
	MCObjectHandle PaMakeVertical(bool doRegen)
	{
		const WorldCoord x = static_cast<WorldCoord>(gPaSerial++) * 1000;
		MCObjectHandle path = gSDK->CreateNurbsCurve(WorldPt3(x, 0, 0), true, 3);
		if (path == nullptr)
			return nullptr;
		gSDK->Add3DVertex(path, WorldPt3(x, 0, 3000));
		return gSDK->CreateCustomObjectPath(kPaPio, path, nullptr, doRegen);
	}

	void PaRaiseArrow(vwprobe::Report& probe, const char* where)
	{
		gSDK->SetDefaultArrowByClass();
		if (gSDK->GetDefaultArrowByClass() == 0)
			probe.fail(std::string("[bc] SetDefaultArrowByClass() で旗が立たない（") + where +
					   "）");
	}

	void PaSetupByClass(vwprobe::Report& probe)
	{
		gSDK->DefineCustomObject(kPaPio, kCustomObjectPrefNever); // 旧は呼んでいない（A で済み）
		gPaSerial = 0;
		probe.log("  [bc] 走らせる前の文書の旗: " + PaDocFlags() +
				  "（旧: Ar=n。#94 第 1 周では 7 つとも n）");

		// 1. 7 つを立てる（旧と同じ順・同じ口。不透明度は 1 旗版で立てる）
		gSDK->SetDefaultPColorsByClass();
		gSDK->SetDefaultFColorsByClass();
		gSDK->SetDefaultLWByClass();
		gSDK->SetDefaultPPatByClass();
		gSDK->SetDefaultFPatByClass();
		gSDK->SetDefaultOpacityByClass();
		gSDK->SetDefaultArrowByClass();
		PaExpect(probe, "7 つを立てた後", PaDocFlags(), kPaAllYes);

		// 2. 1 つ作るごとに読む（矩形 A → PIO 1 → PIO 2 → 矩形 B。doRegen=true）
		MCObjectHandle rectA = PaMakeRect();
		PaExpect(probe, "矩形 A の後", PaDocFlags(), kPaAllYes);
		MCObjectHandle pio1 = PaMakeVertical(true);
		PaExpect(probe, "PIO 1(doRegen=true) の後", PaDocFlags(), kPaArrowNo);
		MCObjectHandle pio2 = PaMakeVertical(true);
		PaExpect(probe, "PIO 2(doRegen=true) の後", PaDocFlags(), kPaArrowNo);
		MCObjectHandle rectB = PaMakeRect();
		PaExpect(probe, "矩形 B の後", PaDocFlags(), kPaArrowNo);

		PaExpect(probe, "obj 矩形 A", PaObjFlags(rectA), kPaAllYes);
		PaExpect(probe, "obj PIO 1", PaObjFlags(pio1), kPaAllYes);
		PaExpect(probe, "obj PIO 2", PaObjFlags(pio2), kPaArrowNo);
		PaExpect(probe, "obj 矩形 B", PaObjFlags(rectB), kPaArrowNo);

		// 3. 作成か作り直しか（どの手も測る前に旗を立て直す）
		PaRaiseArrow(probe, "3 節の頭");
		PaExpect(probe, "立て直した", PaDocFlags(), kPaAllYes);
		MCObjectHandle pio3 = PaMakeVertical(false);
		PaExpect(probe, "PIO 3(doRegen=false) の後", PaDocFlags(), kPaAllYes);
		probe.log("    PIO 3 子(作っただけ)=" + DfChildren(pio3) + " 外接=" + DfBounds(pio3) +
				  " obj=" + PaObjFlags(pio3));
		PaRaiseArrow(probe, "PIO の ResetObject の前");
		if (pio3 != nullptr)
		{
			gSDK->ResetObject(pio3);
			PaExpect(probe, "PIO 3 へ ResetObject の後", PaDocFlags(), kPaArrowNo);
		}
		PaRaiseArrow(probe, "矩形の ResetObject の前");
		if (rectA != nullptr)
		{
			gSDK->ResetObject(rectA);
			PaExpect(probe, "矩形 A へ ResetObject の後", PaDocFlags(), kPaAllYes);
		}

		// ここまでが旧プローブ（4 節以降は写していない）。名前を付ける。
		DfSetName(probe, rectA, "pa-ra");
		DfSetName(probe, pio1, "pa-p1");
		DfSetName(probe, pio2, "pa-p2");
		DfSetName(probe, rectB, "pa-rb");
		DfSetName(probe, pio3, "pa-p3");
		PaExpect(probe, "名前を付けた後", PaDocFlags(), kPaAllYes);

		// 4（新）. 旗を立てたまま、doRegen=false で作って作り直さない
		// PIO を 1 本残す。戻った後に VW が自分で作り直すなら、
		// 旗が下りて子が生える。
		PaRaiseArrow(probe, "PIO 4 の前");
		MCObjectHandle pio4 = PaMakeVertical(false);
		DfSetName(probe, pio4, "pa-p4");
		PaExpect(probe, "PIO 4(doRegen=false・作り直さない) の後", PaDocFlags(), kPaAllYes);
		probe.log("    PIO 4 子=" + DfChildren(pio4) + " 外接=" + DfBounds(pio4) +
				  " obj=" + PaObjFlags(pio4) + "（旧に無い。PIO 3 の作っただけの値と比べる）");
		probe.log("  T0 で文書の旗が Ar=y・PIO 4 の子が増えていなければ「下りない」を再現");
	}

	DeferredReadings PaMeasureByClass()
	{
		DeferredReadings rs;
		rs.emplace_back("doc", PaDocFlags());
		for (const char* name : {"pa-ra", "pa-p1", "pa-p2", "pa-rb", "pa-p3", "pa-p4"})
		{
			MCObjectHandle h = DfFind(name);
			const std::string key = std::string(name).substr(3);
			rs.emplace_back(key + ".flags", PaObjFlags(h));
			if (h != nullptr && gSDK->GetObjectTypeN(h) == 86)
			{
				rs.emplace_back(key + ".kids", DfChildren(h));
				rs.emplace_back(key + ".bb", DfBounds(h));
			}
		}
		return rs;
	}
} // namespace

VW_PROBE("deferred-pio-attrs", "構造材の属性モードと既定のマーカー旗を戻った後に読む",
		 "AttributesMode 0〜3・面の目印・既定 by-class の旗を 4 時点で読む")
{
	std::vector<DeferredCase> cases;
	cases.push_back({"am", "AttributesMode 0〜3 を 1 値 1 個体（#158 [I]）。描画は変わらないか",
					 PaSetupModes, []()
					 {
						 DeferredReadings rs;
						 for (int mode = 0; mode <= 3; ++mode)
						 {
							 const std::string k = std::to_string(mode);
							 for (auto& kv : PaMeasureMember("m" + k, "pa-m" + k, true))
								 rs.push_back(kv);
						 }
						 return rs;
					 }});
	cases.push_back({"af", "3 面に別々の目印（#158）。平面図で出るのは _Below か", PaSetupFaces,
					 []() { return PaMeasureMember("f", "pa-f", false); }});
	cases.push_back({"bc",
					 "既定 by-class の 7 旗（#104）。doRegen=false "
					 "では下りず、regen で下りるか",
					 PaSetupByClass, PaMeasureByClass});
	RunDeferred(probe, "deferred-pio-attrs", cases, false, true);
}
