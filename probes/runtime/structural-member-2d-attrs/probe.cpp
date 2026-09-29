//
//	probes/runtime/structural-member-2d-attrs/probe.cpp
//
//	[issue #158] 構造材（StructuralMember）の 2D / 3D 属性パラメータ。**第 3 版**。
//
//	ここまでで分かったこと（PR #159 の実行ログ 3 本）:
//	  ・索引 48〜152 は「面（_Above / _At / _Below）× パーツ × 欄」で 35 件 × 3 面。
//	    名前と欄型・既定値は採れた。
//	  ・`…Display_<面>` を false にすると描画が変わる（書いた値も残る）。
//	  ・**`ResetObject` 1 回ごとに、PIO の中へ型 90（`kUndoPlaceholderNode`）の子が
//	    1 つ積もる**——プローブは undo イベントを開かないので置き石が残る。積もると
//	    やがて描画そのものが壊れた（型 84 と型 21 が消え、値を戻しても戻らなかった）。
//	    **だから「1 つの個体の値を振って比べる」測り方は成立しない。** この版は
//	    **1 値 1 個体**で測る。
//	  ・描かれた子は属性から素性が分かる: 型 21 で線種 -17・太さ 14 は中心線
//	    （`CenterlineLineStyle` / `CenterlineLineWeight` と一致）、型 5 で太さ 14 は端部、
//	    型 21 で太さ 7 は構造材の稜線、型 21 で塗り solid 白は構造材の断面、
//	    型 84（`kCSGTreeNode`）は 3D 実体。
//	  ・**`_At` を振っても何も変わらなかった**。平面図では材が切断面より下にあるので、
//	    使われている面は `_At` ではない見込み。この版で face を名指しで確かめる。
//	  ・`SetParamClass` は `MemberClass_At`（欄型 18 = `kFieldClassesPopup`）に効かず、
//	    読み戻しは空だった。書き方をこの版で確かめる。
//
//	この版で採るもの（ログは短く保つ。長いと投稿が前から切られる）:
//	  A  置き石の積もり方（`ResetObject` を 6 回。子の数と型 90 の数）
//	  B  **どの面が使われるか**。3 面の `LineWeight` / `FillColor` に別々の目印を入れて
//	     1 回だけ描き、出てきた子の太さ・色から面を名指しする
//	  C  クラスの書き方（`SetParamValue` / `SetParamString` / `SetParamClass` を別個体で）
//	  D  `MemberFillStyle`（3 面とも同じ値）を 0〜3。**1 値 1 個体。** クラスの塗りは黄、
//	     per-part の塗り色はマゼンタにして食い違わせる → 黄ならクラススタイル、
//	     マゼンタなら per-part、白なら既定のまま、無しなら塗らない
//	  E  `MemberPenStyle` を同じやり方で 0〜3
//	  F  `AttributesMode` を 0〜3（1 値 1 個体）
//	  G  選択肢を 3 経路で採る（対照付き）
//	  要約  最後に答えだけを並べる
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace
{
	const char* const kStructMemberProbePio = "StructuralMember";
	const char* const kStructMemberProbeClass = "プローブ_属性試験";

	std::string ProbeText(const TXString& src)
	{
		const char* utf8 = static_cast<const char*>(src);
		return utf8 ? std::string(utf8) : std::string();
	}

	std::string ProbeWhole(long long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%lld", value);
		return std::string(buffer);
	}

	std::string ProbeColorText(const CRGBColor& color)
	{
		return "(" + ProbeWhole(color.GetRed()) + "," + ProbeWhole(color.GetGreen()) + "," +
			   ProbeWhole(color.GetBlue()) + ")";
	}

	std::string ProbePatternText(const VWPattern& pattern)
	{
		std::string out = ProbeWhole(static_cast<InternalIndex>(pattern));
		if (pattern.IsNonePattern())
			out += "/none";
		else if (pattern.IsSolidPattern())
			out += "/solid";
		return out;
	}

	// 描かれた子だけ（型 5 多角形・21 ポリライン・84 CSG）。グループ（11）・終端（0）・
	// undo の置き石（90）は数だけ数える。
	bool ProbeIsDrawn(short type)
	{
		return type == 5 || type == 21 || type == 84;
	}

	std::string ProbeChildLine(MCObjectHandle child)
	{
		VWObject wrapper(child);
		const VWObjectAttr& attr = wrapper.GetObjectAttribs();
		std::string line = "型=" + ProbeWhole(gSDK->GetObjectTypeN(child));
		line += " 塗=" + ProbePatternText(attr.GetFillPattern());
		line += "/" + ProbeColorText(attr.GetFillForeColor());
		line += " 線=" + ProbePatternText(attr.GetPenPattern());
		line += "/" + ProbeColorText(attr.GetPenForeColor());
		line += " 太=" + ProbeWhole(attr.GetLineWeightInMils());
		TXString className;
		gSDK->ClassIDToName(gSDK->GetObjectClass(child), className);
		line += " cls=[" + ProbeText(className) + "]";
		WorldRect bounds;
		if (gSDK->GetObjectBounds(child, bounds))
		{
			line += " 外接=(" + ProbeWhole(static_cast<long long>(bounds.left)) + "," +
					ProbeWhole(static_cast<long long>(bounds.top)) + "," +
					ProbeWhole(static_cast<long long>(bounds.right)) + "," +
					ProbeWhole(static_cast<long long>(bounds.bottom)) + ")";
		}
		return line;
	}

	void ProbeDumpDrawn(vwprobe::Report& probe, const std::string& tag, MCObjectHandle pio)
	{
		size_t total = 0;
		size_t placeholders = 0;
		size_t drawn = 0;
		for (MCObjectHandle child = gSDK->FirstMemberObj(pio); child != nil;
			 child = gSDK->NextObject(child))
		{
			const short type = gSDK->GetObjectTypeN(child);
			++total;
			if (type == 90)
				++placeholders;
			if (ProbeIsDrawn(type))
			{
				++drawn;
				if (drawn <= 10)
					probe.log(tag + " " + ProbeChildLine(child));
			}
		}
		probe.log(tag + " 子: 全 " + ProbeWhole(static_cast<long long>(total)) + " / 描かれた " +
				  ProbeWhole(static_cast<long long>(drawn)) + " / 置き石(型90) " +
				  ProbeWhole(static_cast<long long>(placeholders)));
	}

	MCObjectHandle ProbeMakeMember(vwprobe::Report& probe, const std::string& tag, double originY,
								   bool doReset)
	{
		VWPolygon2DObj path({VWPoint2D(0.0, originY), VWPoint2D(3000.0, originY)});
		MCObjectHandle pathHandle = path.GetThisObject();
		if (pathHandle == nil)
		{
			probe.fail(tag + " パス（VWPolygon2DObj）を作れなかった");
			return nil;
		}
		MCObjectHandle member =
			gSDK->CreateCustomObjectPath(kStructMemberProbePio, pathHandle, nil, false);
		if (member == nil)
		{
			probe.fail(tag + " CreateCustomObjectPath(StructuralMember) が nil を返した");
			return nil;
		}
		if (doReset && !gSDK->ResetObject(member))
			probe.log(tag + " 注意: ResetObject が false を返した");
		return member;
	}

	size_t ProbeIndexOf(vwprobe::Report& probe, const VWParametricObj& pio,
						const std::string& universalName)
	{
		const size_t index = pio.GetParamIndex(TXString(universalName.c_str()));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.log("注意: " + universalName + " を名前で引けなかった");
			return size_t(-1);
		}
		return index;
	}

	const char* const kProbeFaceSuffix[] = {"_Above", "_At", "_Below"};

	// 3 面すべてへ同じ値を書く（どの面が使われていても効くように）。
	void ProbeSetAllFaces(vwprobe::Report& probe, VWParametricObj& pio, const std::string& base,
						  const std::string& value)
	{
		for (size_t face = 0; face < 3; ++face)
		{
			const size_t index = ProbeIndexOf(probe, pio, base + kProbeFaceSuffix[face]);
			if (index != size_t(-1))
				pio.SetParamValue(index, TXString(value.c_str()));
		}
	}

	void ProbeSetAllFacesColor(vwprobe::Report& probe, VWParametricObj& pio,
							   const std::string& base, const CRGBColor& color)
	{
		for (size_t face = 0; face < 3; ++face)
		{
			const size_t index = ProbeIndexOf(probe, pio, base + kProbeFaceSuffix[face]);
			if (index != size_t(-1))
				pio.SetParamColor(index, color.GetColorIndex());
		}
	}

	void ProbeSetAllFacesClass(vwprobe::Report& probe, VWParametricObj& pio,
							   const std::string& base, const std::string& className)
	{
		for (size_t face = 0; face < 3; ++face)
		{
			const size_t index = ProbeIndexOf(probe, pio, base + kProbeFaceSuffix[face]);
			if (index != size_t(-1))
				pio.SetParamValue(index, TXString(className.c_str()));
		}
	}

	// 選択肢を 3 経路で採る。
	void ProbeDumpChoices(vwprobe::Report& probe, const VWParametricObj& pio,
						  VWRecordFormatObj& format, const std::string& universalName)
	{
		const size_t index = pio.GetParamIndex(TXString(universalName.c_str()));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.log("[G] " + universalName + " を名前で引けなかった");
			return;
		}
		TXStringSTLArray keys;
		const bool gotKeys = pio.GetParamChoices(index, keys);
		TXStringSTLArray displays;
		const bool gotDisplays = pio.GetParamLocalizedChoices(index, displays);
		const size_t onInstance = pio.PopupGetChoicesCount(index);
		const size_t onFormatParametric = format.PopupGetChoicesCount(index, true);
		const size_t onFormatPlain = format.PopupGetChoicesCount(index, false);
		std::string line = "[G] " + ProbeWhole(static_cast<long long>(index)) + " " +
						   universalName + " 経路: choices=" + std::string(gotKeys ? "T" : "F") +
						   ProbeWhole(static_cast<long long>(keys.size())) +
						   " localized=" + std::string(gotDisplays ? "T" : "F") +
						   ProbeWhole(static_cast<long long>(displays.size())) +
						   " inst=" + ProbeWhole(static_cast<long long>(onInstance)) +
						   " fmt+p=" + ProbeWhole(static_cast<long long>(onFormatParametric)) +
						   " fmt=" + ProbeWhole(static_cast<long long>(onFormatPlain));
		probe.log(line);
		const size_t pairs = keys.size() > displays.size() ? keys.size() : displays.size();
		for (size_t at = 0; at < pairs && at < 20; ++at)
		{
			probe.log("[G]   [" + ProbeWhole(static_cast<long long>(at)) + "] キー=[" +
					  (at < keys.size() ? ProbeText(keys[at]) : std::string("—")) + "] 表示=[" +
					  (at < displays.size() ? ProbeText(displays[at]) : std::string("—")) + "]");
		}
		for (size_t at = 0; at < onFormatParametric && at < 20; ++at)
		{
			TXString key;
			TXString display;
			format.PopupGetChoice(index, at, key, display, true);
			probe.log("[G]   fmt[" + ProbeWhole(static_cast<long long>(at)) + "] キー=[" +
					  ProbeText(key) + "] 表示=[" + ProbeText(display) + "]");
		}
	}
} // namespace

VW_PROBE("structural-member-2d-attrs", "構造材の 2D / 3D 属性パラメータを採る（第 3 版）",
		 "1 値 1 個体で測り直す。どの面が使われるか・クラスの書き方・FillStyle / PenStyle / "
		 "AttributesMode の値の意味・選択肢の採り方・undo の置き石の積もり方")
{
	gSDK->DefineCustomObject(kStructMemberProbePio, kCustomObjectPrefNever);

	// 目立つクラス（塗り・線とも**黄**）。per-part の色は**マゼンタ**にして食い違わせる。
	const InternalIndex probeClass = gSDK->AddClass(TXString(kStructMemberProbeClass));
	const CRGBColor kYellow(Uint8(255), Uint8(255), Uint8(0));
	const CRGBColor kMagenta(Uint8(255), Uint8(0), Uint8(255));
	if (probeClass != 0)
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
		probe.log("[G0] クラス [" + std::string(kStructMemberProbeClass) +
				  "] 塗=" + ProbePatternText(classAttr.GetFillPattern()) + "/" +
				  ProbeColorText(classAttr.GetFillForeColor()) +
				  " 線=" + ProbeColorText(classAttr.GetPenForeColor()) +
				  " 太=" + ProbeWhole(classAttr.GetLineWeightInMils()));
	}
	else
	{
		probe.fail("[G0] AddClass が 0 を返した");
	}

	// ---- A: undo の置き石はどう積もるか --------------------------------
	{
		MCObjectHandle member = ProbeMakeMember(probe, "[A]", 0.0, true);
		if (member != nil)
		{
			for (int round = 1; round <= 5; ++round)
			{
				gSDK->ResetObject(member);
				size_t total = 0;
				size_t placeholders = 0;
				size_t drawn = 0;
				for (MCObjectHandle child = gSDK->FirstMemberObj(member); child != nil;
					 child = gSDK->NextObject(child))
				{
					const short type = gSDK->GetObjectTypeN(child);
					++total;
					if (type == 90)
						++placeholders;
					if (ProbeIsDrawn(type))
						++drawn;
				}
				probe.log("[A] ResetObject " + ProbeWhole(round + 1) + " 回目まで: 子 全 " +
						  ProbeWhole(static_cast<long long>(total)) + " / 描かれた " +
						  ProbeWhole(static_cast<long long>(drawn)) + " / 置き石(型90) " +
						  ProbeWhole(static_cast<long long>(placeholders)));
			}
		}
	}

	// ---- B: どの面が使われるか ------------------------------------------
	// 3 面の太さと塗り色に別々の目印を入れて 1 回だけ描く。出てきた子の値が、どの面の
	// 設定から来たかをそのまま名指しする。
	{
		MCObjectHandle member = ProbeMakeMember(probe, "[B]", 5000.0, false);
		if (member != nil)
		{
			VWParametricObj pio(member);
			const char* const kWeightBases[] = {"MemberLineWeight", "CenterlineLineWeight",
												"CapsLineWeight"};
			const long kWeights[3][3] = {{55, 56, 57}, {58, 59, 60}, {61, 62, 63}};
			for (size_t base = 0; base < 3; ++base)
			{
				for (size_t face = 0; face < 3; ++face)
				{
					const size_t index = ProbeIndexOf(
						probe, pio, std::string(kWeightBases[base]) + kProbeFaceSuffix[face]);
					if (index != size_t(-1))
						pio.SetParamValue(index,
										  TXString(ProbeWhole(kWeights[base][face]).c_str()));
				}
				probe.log(std::string("[B] ") + kWeightBases[base] + " ← Above=" +
						  ProbeWhole(kWeights[base][0]) + " At=" + ProbeWhole(kWeights[base][1]) +
						  " Below=" + ProbeWhole(kWeights[base][2]));
			}
			// 塗り色: Above=赤 / At=緑 / Below=青。
			const CRGBColor faceColors[3] = {CRGBColor(Uint8(255), Uint8(0), Uint8(0)),
											 CRGBColor(Uint8(0), Uint8(255), Uint8(0)),
											 CRGBColor(Uint8(0), Uint8(0), Uint8(255))};
			for (size_t face = 0; face < 3; ++face)
			{
				const size_t index = ProbeIndexOf(
					probe, pio, std::string("MemberFillColor") + kProbeFaceSuffix[face]);
				if (index != size_t(-1))
					pio.SetParamColor(index, faceColors[face].GetColorIndex());
			}
			probe.log("[B] MemberFillColor ← Above=赤 At=緑 Below=青");
			gSDK->ResetObject(member);
			ProbeDumpDrawn(probe, "[B]", member);
		}
	}

	// ---- C: クラスをどう書くか（欄型 18 = kFieldClassesPopup） -----------
	if (probeClass != 0)
	{
		const char* const kWays[] = {"SetParamValue(名前)", "SetParamString(名前)",
									 "SetParamClass(索引)"};
		for (size_t way = 0; way < 3; ++way)
		{
			MCObjectHandle member =
				ProbeMakeMember(probe, "[C]", 10000.0 + 1000.0 * double(way), false);
			if (member == nil)
				continue;
			VWParametricObj pio(member);
			const size_t index = ProbeIndexOf(probe, pio, "MemberClass_Below");
			if (index == size_t(-1))
				continue;
			if (way == 0)
				pio.SetParamValue(index, TXString(kStructMemberProbeClass));
			else if (way == 1)
				pio.SetParamString(index, TXString(kStructMemberProbeClass));
			else
				pio.SetParamClass(index, probeClass);
			TXString byClassCall;
			gSDK->ClassIDToName(pio.GetParamClass(index), byClassCall);
			probe.log(std::string("[C] ") + kWays[way] + " → GetParamValue=[" +
					  ProbeText(pio.GetParamValue(index)) + "] GetParamClass=[" +
					  ProbeText(byClassCall) + "]");
			gSDK->ResetObject(member);
			probe.log(std::string("[C] ") + kWays[way] + " ResetObject 後 GetParamValue=[" +
					  ProbeText(pio.GetParamValue(index)) + "]");
		}
	}

	// ---- D / E / F: 値の意味（1 値 1 個体） ------------------------------
	struct ProbeSweepSpec
	{
		const char* tag;
		const char* base;  // 3 面へ書く欄。face を持たないものは nullptr
		const char* plain; // face を持たない欄（AttributesMode）
	};
	const ProbeSweepSpec kSweeps[] = {
		{"[D] MemberFillStyle", "MemberFillStyle", nullptr},
		{"[E] MemberPenStyle", "MemberPenStyle", nullptr},
		{"[F] AttributesMode", nullptr, "AttributesMode"},
	};
	for (size_t spec = 0; spec < sizeof(kSweeps) / sizeof(kSweeps[0]); ++spec)
	{
		for (int value = 0; value <= 3; ++value)
		{
			MCObjectHandle member =
				ProbeMakeMember(probe, kSweeps[spec].tag,
								20000.0 + 1000.0 * double(spec * 4 + size_t(value)), false);
			if (member == nil)
				continue;
			VWParametricObj pio(member);
			// どの経路でも同じ土台にする: クラス（黄）と per-part の色（マゼンタ）。
			ProbeSetAllFacesClass(probe, pio, "MemberClass", kStructMemberProbeClass);
			ProbeSetAllFacesColor(probe, pio, "MemberFillColor", kMagenta);
			ProbeSetAllFacesColor(probe, pio, "MemberPenColor", kMagenta);
			if (kSweeps[spec].base != nullptr)
			{
				ProbeSetAllFaces(probe, pio, kSweeps[spec].base, ProbeWhole(value));
			}
			else
			{
				const size_t index = ProbeIndexOf(probe, pio, kSweeps[spec].plain);
				if (index != size_t(-1))
					pio.SetParamValue(index, TXString(ProbeWhole(value).c_str()));
			}
			gSDK->ResetObject(member);
			ProbeDumpDrawn(probe, std::string(kSweeps[spec].tag) + "=" + ProbeWhole(value), member);
		}
	}

	// ---- G: 選択肢を 3 経路で採る（対照付き。ここは最後に置く） ----------
	{
		MCObjectHandle member = ProbeMakeMember(probe, "[G]", 40000.0, true);
		if (member != nil)
		{
			VWParametricObj pio(member);
			VWRecordFormatObj format = pio.GetRecordFormat();
			probe.log("[G] 対照（選択肢を持つと分かっているもの）");
			const char* const kControls[] = {"MemberType", "StructuralUse", "EndCondition"};
			for (size_t at = 0; at < sizeof(kControls) / sizeof(kControls[0]); ++at)
				ProbeDumpChoices(probe, pio, format, kControls[at]);
			probe.log("[G] 本題");
			const char* const kTargets[] = {"MemberPenStyle_Below", "MemberFillStyle_Below",
											"AttributesMode", "AttributesMode3D",
											"MemberAttributes_3D"};
			for (size_t at = 0; at < sizeof(kTargets) / sizeof(kTargets[0]); ++at)
				ProbeDumpChoices(probe, pio, format, kTargets[at]);
		}
	}

	probe.log("要約: [A] 置き石の積もり方 / [B] 使われる面 / [C] クラスの書き方 / "
			  "[D] FillStyle / [E] PenStyle / [F] AttributesMode / [G] 選択肢 の各行を読む。"
			  "クラスの黄=クラススタイル、マゼンタ=per-part の指定、白=既定のまま。");
	probe.log("おわり");
}
