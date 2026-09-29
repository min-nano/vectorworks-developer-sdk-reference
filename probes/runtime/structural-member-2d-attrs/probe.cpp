//
//	probes/runtime/structural-member-2d-attrs/probe.cpp
//
//	[issue #158] 構造材（StructuralMember）の 2D / 3D 属性パラメータ。**第 4 版**。
//
//	ここまでで確定したこと（PR #159 の実行ログ 4 本）:
//	  ・索引 48〜152 は「面（_Above / _At / _Below）× パーツ × 欄」で 35 件 × 3 面。
//	    名前・欄型・既定値は採れた。センターマークは面を持たず 153〜160 に 1 組。
//	    3D 属性は 161〜168。
//	  ・**選択肢は `GetParamChoices` / `GetParamLocalizedChoices` で採れる**
//	    （`PopupGetChoicesCount` は 0 を返す。フォーマット側は `useParametric=true` なら採れる）。
//	      `…PenStyle`  5 択: 0 なし / 1 実線 / 2 ラインタイプ / 3 オブジェクト別 / **4 クラス属性**
//	      `…FillStyle` 8 択: 0 面なし / 1 カラー / 2 ハッチング / 3 タイル / 4 グラデーション /
//	                        5 オブジェクト別 / **6 クラス属性** / 7 マテリアル属性
//	      `AttributesMode` / `AttributesMode3D` 4 択: 0 オブジェクト / 1 線種 / 2 クラス / 3 マテリアル
//	      `Member/CoverAttributes_3D` 2 択: 0 オブジェクト別 / **1 クラス属性**
//	  ・**平面図で使われている面は `_Below`**（材が切断面より下にあるため）。3 面に別々の
//	    目印を入れて確かめた——断面の塗りは `_Below` の色、稜線の太さは
//	    `MemberLineWeight_Below`、中心線は `CenterlineLineWeight_Below`、端部は
//	    `CapsLineWeight_Below` の値だった。
//	  ・**クラス欄（欄型 18）は名前で書く。** `SetParamValue` / `SetParamString` に
//	    クラス名を渡せば残る。`SetParamClass`（索引）は効かず、`GetParamClass` は
//	    どちらの書き方でも空を返す（読み戻しは `GetParamValue`）。
//	  ・`ResetObject` 1 回ごとに型 90（`kUndoPlaceholderNode`）の子が 1 つ積もる。
//	    6 回までは描画は保たれた（第 2 版では 20 回超で型 84 と断面が消えた）。
//
//	この版で採るもの（前の版は 0〜3 しか振っておらず、クラス属性の値に届いていなかった）:
//	  H  `MemberFillStyle`=6（クラス属性）/ `MemberPenStyle`=4（クラス属性）を、
//	     単独と両方で。クラスの色は黄・per-part の色はマゼンタなので、**黄になれば
//	     クラススタイルが効いた**と読める
//	  I  `AttributesMode` 0〜3 を、**PIO 自身をクラスへ入れて**振る（per-part の
//	     クラス欄は空のまま）。「クラス」が PIO 自身のクラスを指すのかを見る
//	  J  取り込みで使う形をそのまま 1 本作る（構造材＝クラス属性・被覆と中心線は非表示・
//	     端部は両端）。これが答えの現物になる
//	  K  置き石が何回で描画を壊すか（5 回ごとに 30 回まで）
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kStructMemberProbePio = "StructuralMember";
	const char* const kStructMemberProbeClass = "プローブ_属性試験";

	// 選択肢の表（第 3 版で採った）から、クラス属性に当たる値。
	const char* const kStructMemberPenByClass = "4";  // …PenStyle  → クラス属性
	const char* const kStructMemberFillByClass = "6"; // …FillStyle → クラス属性

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

	bool ProbeIsDrawn(short type)
	{
		return type == 5 || type == 21 || type == 84;
	}

	std::string ProbeChildLine(MCObjectHandle child)
	{
		VWObject wrapper(child);
		const VWObjectAttr& attr = wrapper.GetObjectAttribs();
		std::string line = "型=" + ProbeWhole(gSDK->GetObjectTypeN(child));
		line += " 塗=" + ProbePatternText(attr.GetFillPattern()) + "/" +
				ProbeColorText(attr.GetFillForeColor());
		line += " 線=" + ProbePatternText(attr.GetPenPattern()) + "/" +
				ProbeColorText(attr.GetPenForeColor());
		line += " 太=" + ProbeWhole(attr.GetLineWeightInMils());
		line += std::string(" byCls(f=") + (attr.GetFillColorByClass() ? "y" : "n") +
				",p=" + (attr.GetPenColorByClass() ? "y" : "n") + ")";
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

	MCObjectHandle ProbeMakeMember(vwprobe::Report& probe, const std::string& tag, double originY)
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
			probe.fail(tag + " CreateCustomObjectPath(StructuralMember) が nil を返した");
		return member;
	}

	const char* const kProbeFaceSuffix[] = {"_Above", "_At", "_Below"};

	// 3 面すべてへ同じ文字列を書く（どの面が使われていても効くように）。
	void ProbeWriteAllFaces(vwprobe::Report& probe, VWParametricObj& pio, const std::string& base,
							const std::string& value)
	{
		for (size_t face = 0; face < 3; ++face)
		{
			const std::string name = base + kProbeFaceSuffix[face];
			const size_t index = pio.GetParamIndex(TXString(name.c_str()));
			if (index == size_t(-1) || index >= pio.GetParamsCount())
			{
				probe.log("注意: " + name + " を名前で引けなかった");
				continue;
			}
			pio.SetParamValue(index, TXString(value.c_str()));
		}
	}

	void ProbeWriteAllFacesColor(vwprobe::Report& probe, VWParametricObj& pio,
								 const std::string& base, const CRGBColor& color)
	{
		for (size_t face = 0; face < 3; ++face)
		{
			const std::string name = base + kProbeFaceSuffix[face];
			const size_t index = pio.GetParamIndex(TXString(name.c_str()));
			if (index == size_t(-1) || index >= pio.GetParamsCount())
			{
				probe.log("注意: " + name + " を名前で引けなかった");
				continue;
			}
			pio.SetParamColor(index, color.GetColorIndex());
		}
	}
} // namespace

VW_PROBE("structural-member-2d-attrs", "構造材の 2D / 3D 属性パラメータを採る（第 4 版）",
		 "クラス属性の値（PenStyle=4 / FillStyle=6）が描画に効くかを確かめ、"
		 "AttributesMode が何を切り替えるかを見て、取り込みで使う形を 1 本作る")
{
	gSDK->DefineCustomObject(kStructMemberProbePio, kCustomObjectPrefNever);

	// クラスは塗り・線とも**黄**・太さ 99。per-part の色は**マゼンタ**。
	const InternalIndex probeClass = gSDK->AddClass(TXString(kStructMemberProbeClass));
	const CRGBColor kYellow(Uint8(255), Uint8(255), Uint8(0));
	const CRGBColor kMagenta(Uint8(255), Uint8(0), Uint8(255));
	if (probeClass == 0)
	{
		probe.fail("[G0] AddClass が 0 を返した");
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
		probe.log("[G0] クラス [" + std::string(kStructMemberProbeClass) +
				  "] 塗=" + ProbePatternText(classAttr.GetFillPattern()) + "/" +
				  ProbeColorText(classAttr.GetFillForeColor()) +
				  " 線=" + ProbeColorText(classAttr.GetPenForeColor()) +
				  " 太=" + ProbeWhole(classAttr.GetLineWeightInMils()));
	}

	// ---- H: クラス属性の値は描画に効くか --------------------------------
	// 面の属性 6（クラス属性）・線の属性 4（クラス属性）を、単独と両方で。
	struct ProbeClassCase
	{
		const char* tag;
		const char* fillStyle; // nullptr なら書かない（既定のまま）
		const char* penStyle;
	};
	const ProbeClassCase kClassCases[] = {
		{"[H] 面だけクラス属性(Fill=6)", kStructMemberFillByClass, nullptr},
		{"[H] 線だけクラス属性(Pen=4)", nullptr, kStructMemberPenByClass},
		{"[H] 両方クラス属性(Fill=6,Pen=4)", kStructMemberFillByClass, kStructMemberPenByClass},
		{"[H] どちらもオブジェクト別(Fill=5,Pen=3)", "5", "3"},
	};
	for (size_t at = 0; at < sizeof(kClassCases) / sizeof(kClassCases[0]); ++at)
	{
		MCObjectHandle member = ProbeMakeMember(probe, kClassCases[at].tag, 1000.0 * double(at));
		if (member == nil)
			continue;
		VWParametricObj pio(member);
		ProbeWriteAllFaces(probe, pio, "MemberClass", kStructMemberProbeClass);
		ProbeWriteAllFacesColor(probe, pio, "MemberFillColor", kMagenta);
		ProbeWriteAllFacesColor(probe, pio, "MemberPenColor", kMagenta);
		if (kClassCases[at].fillStyle != nullptr)
			ProbeWriteAllFaces(probe, pio, "MemberFillStyle", kClassCases[at].fillStyle);
		if (kClassCases[at].penStyle != nullptr)
			ProbeWriteAllFaces(probe, pio, "MemberPenStyle", kClassCases[at].penStyle);
		gSDK->ResetObject(member);
		ProbeDumpDrawn(probe, kClassCases[at].tag, member);
	}

	// ---- I: AttributesMode は何を切り替えるか ---------------------------
	// per-part のクラス欄は**空のまま**にして、**PIO 自身**をクラスへ入れる。
	// 「クラス」が PIO 自身のクラスを指すのなら、2 で黄になるはず。
	for (int mode = 0; mode <= 3; ++mode)
	{
		MCObjectHandle member = ProbeMakeMember(probe, "[I]", 6000.0 + 1000.0 * double(mode));
		if (member == nil)
			continue;
		gSDK->SetObjectClass(member, probeClass);
		VWParametricObj pio(member);
		ProbeWriteAllFacesColor(probe, pio, "MemberFillColor", kMagenta);
		ProbeWriteAllFacesColor(probe, pio, "MemberPenColor", kMagenta);
		const size_t index = pio.GetParamIndex(TXString("AttributesMode"));
		if (index != size_t(-1) && index < pio.GetParamsCount())
			pio.SetParamValue(index, TXString(ProbeWhole(mode).c_str()));
		gSDK->ResetObject(member);
		TXString pioClass;
		gSDK->ClassIDToName(gSDK->GetObjectClass(member), pioClass);
		probe.log("[I] AttributesMode=" + ProbeWhole(mode) + " PIO のクラス=[" +
				  ProbeText(pioClass) + "] 読み戻し=[" +
				  (index != size_t(-1) ? ProbeText(pio.GetParamValue(index)) : std::string("—")) +
				  "]");
		ProbeDumpDrawn(probe, "[I] AttributesMode=" + ProbeWhole(mode), member);
	}

	// ---- J: 取り込みで使う形をそのまま 1 本 -----------------------------
	// 構造材＝クラス属性（面 6 / 線 4）・被覆と中心線は非表示・端部は両端。
	{
		MCObjectHandle member = ProbeMakeMember(probe, "[J]", 12000.0);
		if (member != nil)
		{
			VWParametricObj pio(member);
			ProbeWriteAllFaces(probe, pio, "MemberClass", kStructMemberProbeClass);
			ProbeWriteAllFaces(probe, pio, "MemberFillStyle", kStructMemberFillByClass);
			ProbeWriteAllFaces(probe, pio, "MemberPenStyle", kStructMemberPenByClass);
			ProbeWriteAllFaces(probe, pio, "MemberDisplay", "True");
			ProbeWriteAllFaces(probe, pio, "CoverDisplay", "False");
			ProbeWriteAllFaces(probe, pio, "CenterlineDisplay", "False");
			ProbeWriteAllFaces(probe, pio, "StartCapDisplay", "True");
			ProbeWriteAllFaces(probe, pio, "EndCapDisplay", "True");
			gSDK->ResetObject(member);
			// 書いた値がそのまま残っているか（真偽欄は GetParamBool で比べる）。
			const char* const kChecks[] = {"MemberDisplay", "CoverDisplay", "CenterlineDisplay",
										   "StartCapDisplay", "EndCapDisplay"};
			for (size_t at = 0; at < sizeof(kChecks) / sizeof(kChecks[0]); ++at)
			{
				std::string line = std::string("[J] ") + kChecks[at] + ":";
				for (size_t face = 0; face < 3; ++face)
				{
					const std::string name = std::string(kChecks[at]) + kProbeFaceSuffix[face];
					const size_t index = pio.GetParamIndex(TXString(name.c_str()));
					line += std::string(" ") + kProbeFaceSuffix[face] + "=" +
							(index != size_t(-1) && index < pio.GetParamsCount()
								 ? (pio.GetParamBool(index) ? "true" : "false")
								 : "?");
				}
				probe.log(line);
			}
			const char* const kStyleChecks[] = {"MemberFillStyle", "MemberPenStyle", "MemberClass"};
			for (size_t at = 0; at < sizeof(kStyleChecks) / sizeof(kStyleChecks[0]); ++at)
			{
				std::string line = std::string("[J] ") + kStyleChecks[at] + ":";
				for (size_t face = 0; face < 3; ++face)
				{
					const std::string name = std::string(kStyleChecks[at]) + kProbeFaceSuffix[face];
					const size_t index = pio.GetParamIndex(TXString(name.c_str()));
					line += std::string(" ") + kProbeFaceSuffix[face] + "=[" +
							(index != size_t(-1) && index < pio.GetParamsCount()
								 ? ProbeText(pio.GetParamValue(index))
								 : std::string("?")) +
							"]";
				}
				probe.log(line);
			}
			ProbeDumpDrawn(probe, "[J] 取り込みで使う形", member);
		}
	}

	// ---- K: 置き石は何回で描画を壊すか ----------------------------------
	{
		MCObjectHandle member = ProbeMakeMember(probe, "[K]", 20000.0);
		if (member != nil)
		{
			for (int round = 1; round <= 30; ++round)
			{
				gSDK->ResetObject(member);
				if (round % 5 != 0)
					continue;
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
				probe.log("[K] ResetObject " + ProbeWhole(round) + " 回: 子 全 " +
						  ProbeWhole(static_cast<long long>(total)) + " / 描かれた " +
						  ProbeWhole(static_cast<long long>(drawn)) + " / 置き石(型90) " +
						  ProbeWhole(static_cast<long long>(placeholders)));
			}
		}
	}

	probe.log("要約: 黄(255,255,0)＝クラスの属性が効いた / マゼンタ(255,0,255)＝per-part の"
			  "指定が効いた / 白(255,255,255)＝どちらも効いていない。");
	probe.log("おわり");
}
