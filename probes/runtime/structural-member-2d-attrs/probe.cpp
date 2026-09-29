//
//	probes/runtime/structural-member-2d-attrs/probe.cpp
//
//	[issue #158] 構造材（StructuralMember）の 2D / 3D 属性パラメータ。**第 2 版**。
//
//	第 1 版で採れたもの（PR #159 の 1 回目の実行ログ）:
//	  ・索引 48〜152 は「面（_Above / _At / _Below）× パーツ × 欄」で 35 件 × 3 面。
//	    パーツと欄の名前は判明した（MemberDisplay_Above … CapsLineWeight_Below）。
//	  ・`…Display_<面>` を false にして ResetObject すると**描画が変わる**
//	    （中心線の子・端部の子・構造材の面が消える）。書いた値は残る。
//	  ・**`PopupGetChoicesCount` はどのポップアップでも 0 を返した**——選択肢はインスタンスの
//	    ポップアップ表には入っていない。だから「クラススタイルにする値」も
//	    `AttributesMode` の 4 値も採れていない。
//
//	この版で採るもの（1 回目に取り損ねたところだけ）:
//	  G1  **選択肢を別の経路で採る**。`GetParamChoices` / `GetParamLocalizedChoices`
//	      （provider 経路）と、レコードフォーマットの
//	      `PopupGetChoicesCount(useParametric)` の両方を、既知の当たり
//	      （索引 2 `MemberType`・11 `StructuralUse`・17 `EndCondition` は選択肢を持つ）を
//	      対照に置いて試す。どの経路が答えを返すかを先に決める。
//	  G2  端部を切り分ける。`StartCapDisplay_<面>` だけを false にして、端部の子が
//	      片方だけ消えるかを見る（＝「端部は両端」が何を書くことかを確定する）。
//	      真偽欄の読み戻しは `GetParamBool` で比べる（1 回目は文字列 "0" と "False" を
//	      比べてしまい「戻った」と誤記録した——値は残っていた）。
//	  G3  「線の属性」「面の属性」の値の意味を、**クラスの属性と per-part の色を
//	      食い違わせて**当てる。目立つクラス（面＝塗り・青／線＝青／線の太さ 100mil）を
//	      `MemberClass_At` に入れ、`MemberFillColor_At` に赤を入れてから
//	      `MemberFillStyle_At` / `MemberPenStyle_At` を 0〜3 で振り、**描かれた子の
//	      実際の属性**（塗りパターン・塗り前景色・線色・線の太さ・by-class の旗）を読む。
//	      赤になった値＝per-part の指定、青になった値＝クラススタイル。
//	  G4  `AttributesMode` を 0〜3 で振って同じものを読む（＝4 つの選択肢の意味）。
//	  G5  `AttributesMode3D` / `MemberAttributes_3D` も同じように振る。
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

	std::string ProbeDecimal(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.3f", value);
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
		if (pattern.IsSolidPattern())
			out += "/solid";
		if (pattern.IsHatchPattern())
			out += "/hatch";
		if (pattern.IsTilePattern())
			out += "/tile";
		if (pattern.IsGradientPattern())
			out += "/grad";
		return out;
	}

	// PIO の子（＝描かれた実体）1 つ。**実際の属性**まで読むのが第 2 版の要点——
	//「クラススタイルになったか」は、子の色がクラスの色になったかで判定する。
	std::string ProbeChildLine(MCObjectHandle child)
	{
		VWObject wrapper(child);
		const VWObjectAttr& attr = wrapper.GetObjectAttribs();
		std::string line = "型=" + ProbeWhole(gSDK->GetObjectTypeN(child));
		TXString className;
		gSDK->ClassIDToName(gSDK->GetObjectClass(child), className);
		line += " cls=[" + ProbeText(className) + "]";
		line += " 塗=" + ProbePatternText(attr.GetFillPattern());
		line += " 塗前=" + ProbeColorText(attr.GetFillForeColor());
		line += " 線=" + ProbePatternText(attr.GetPenPattern());
		line += " 線前=" + ProbeColorText(attr.GetPenForeColor());
		line += " 太=" + ProbeWhole(attr.GetLineWeightInMils());
		line += std::string(" byCls(fPat=") + (attr.GetFillPatternByClass() ? "y" : "n");
		line += std::string(" pPat=") + (attr.GetPenPatternByClass() ? "y" : "n");
		line += std::string(" fCol=") + (attr.GetFillColorByClass() ? "y" : "n");
		line += std::string(" pCol=") + (attr.GetPenColorByClass() ? "y" : "n");
		line += std::string(" lw=") + (attr.GetLineWeightByClass() ? "y" : "n") + ")";
		WorldRect bounds;
		if (gSDK->GetObjectBounds(child, bounds))
		{
			line += " 外接=(" + ProbeDecimal(bounds.left) + "," + ProbeDecimal(bounds.top) + "," +
					ProbeDecimal(bounds.right) + "," + ProbeDecimal(bounds.bottom) + ")";
		}
		return line;
	}

	void ProbeDumpChildren(vwprobe::Report& probe, const std::string& tag, MCObjectHandle pio)
	{
		size_t count = 0;
		for (MCObjectHandle child = gSDK->FirstMemberObj(pio); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (count < 16)
				probe.log(tag + " 子[" + ProbeWhole(static_cast<long long>(count)) + "] " +
						  ProbeChildLine(child));
			++count;
		}
		probe.log(tag + " 子の総数=" + ProbeWhole(static_cast<long long>(count)));
	}

	// 水平の構造材 1 本（両端の Z を等しくする＝2D ポリラインで表せる。Findings の手順）。
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
		{
			probe.fail(tag + " CreateCustomObjectPath(StructuralMember) が nil を返した"
							 "（DefineCustomObject は先に呼んである）");
			return nil;
		}
		if (!gSDK->ResetObject(member))
			probe.log(tag + " 注意: 作った直後の ResetObject が false を返した");
		return member;
	}

	size_t ProbeIndexOf(vwprobe::Report& probe, const VWParametricObj& pio,
						const char* universalName)
	{
		const size_t index = pio.GetParamIndex(TXString(universalName));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.log(std::string("注意: ") + universalName + " を名前で引けなかった");
			return size_t(-1);
		}
		return index;
	}

	// 選択肢を 3 経路で採る。どれが答えを返すかを決めるのが目的。
	void ProbeDumpChoices(vwprobe::Report& probe, const VWParametricObj& pio,
						  VWRecordFormatObj& format, size_t index, const std::string& universalName)
	{
		std::string line =
			"[G1] " + ProbeWhole(static_cast<long long>(index)) + " " + universalName;

		TXStringSTLArray universalChoices;
		const bool gotUniversal = pio.GetParamChoices(index, universalChoices);
		TXStringSTLArray localizedChoices;
		const bool gotLocalized = pio.GetParamLocalizedChoices(index, localizedChoices);
		const size_t popupOnInstance = pio.PopupGetChoicesCount(index);
		const size_t popupParametric = format.PopupGetChoicesCount(index, true);
		const size_t popupPlain = format.PopupGetChoicesCount(index, false);

		line += " GetParamChoices=" + std::string(gotUniversal ? "true" : "false") + "/" +
				ProbeWhole(static_cast<long long>(universalChoices.size()));
		line += " Localized=" + std::string(gotLocalized ? "true" : "false") + "/" +
				ProbeWhole(static_cast<long long>(localizedChoices.size()));
		line += " Popup(inst)=" + ProbeWhole(static_cast<long long>(popupOnInstance));
		line += " Popup(fmt,parametric)=" + ProbeWhole(static_cast<long long>(popupParametric));
		line += " Popup(fmt,plain)=" + ProbeWhole(static_cast<long long>(popupPlain));
		probe.log(line);

		const size_t pairs = universalChoices.size() > localizedChoices.size()
								 ? universalChoices.size()
								 : localizedChoices.size();
		for (size_t at = 0; at < pairs && at < 24; ++at)
		{
			std::string entry = "[G1]   [" + ProbeWhole(static_cast<long long>(at)) + "] キー=[";
			entry +=
				at < universalChoices.size() ? ProbeText(universalChoices[at]) : std::string("—");
			entry += "] 表示=[";
			entry +=
				at < localizedChoices.size() ? ProbeText(localizedChoices[at]) : std::string("—");
			entry += "]";
			probe.log(entry);
		}
		for (size_t at = 0; at < popupParametric && at < 24; ++at)
		{
			TXString key;
			TXString display;
			format.PopupGetChoice(index, at, key, display, true);
			probe.log("[G1]   fmt[" + ProbeWhole(static_cast<long long>(at)) + "] キー=[" +
					  ProbeText(key) + "] 表示=[" + ProbeText(display) + "]");
		}
	}

	// ポップアップの値を 0〜3 で振って、描かれた子の実際の属性を読む。
	void ProbeSweepValues(vwprobe::Report& probe, MCObjectHandle member, VWParametricObj& pio,
						  const char* tag, const char* universalName)
	{
		const size_t index = ProbeIndexOf(probe, pio, universalName);
		if (index == size_t(-1))
			return;
		const std::string before = ProbeText(pio.GetParamValue(index));
		probe.log(std::string(tag) + " " + universalName +
				  " 索引=" + ProbeWhole(static_cast<long long>(index)) + " 既定=[" + before + "]");
		for (int value = 0; value <= 3; ++value)
		{
			pio.SetParamValue(index, TXString(ProbeWhole(value).c_str()));
			const bool ok = gSDK->ResetObject(member) != 0;
			const std::string readBack = ProbeText(pio.GetParamValue(index));
			probe.log(std::string(tag) + " " + universalName + "=" + ProbeWhole(value) +
					  " 読み戻し=[" + readBack + "] reset=" + (ok ? "true" : "false"));
			ProbeDumpChildren(
				probe, std::string(tag) + " " + universalName + "=" + ProbeWhole(value), member);
		}
		pio.SetParamValue(index, TXString(before.c_str()));
		gSDK->ResetObject(member);
	}
} // namespace

VW_PROBE("structural-member-2d-attrs", "構造材の 2D / 3D 属性パラメータを採る（第 2 版）",
		 "1 回目で取り損ねた「ポップアップの選択肢」「クラススタイルにする値」"
		 "「AttributesMode の 4 値の意味」「端部の切り分け」を採る")
{
	probe.log("[G0] DefineCustomObject(StructuralMember, kCustomObjectPrefNever)");
	gSDK->DefineCustomObject(kStructMemberProbePio, kCustomObjectPrefNever);

	// 目立つクラスを 1 つ用意する（クラススタイルになったかを色で見分けるため）。
	const InternalIndex probeClass = gSDK->AddClass(TXString(kStructMemberProbeClass));
	probe.log("[G0] クラス [" + std::string(kStructMemberProbeClass) +
			  "] index=" + ProbeWhole(static_cast<long long>(probeClass)));
	if (probeClass != 0)
	{
		VWClass classObj(probeClass);
		classObj.SetUseGraphics(true);
		// VWClassAttr のコンストラクタは protected。クラス側の口から取る。
		VWClassAttr classAttr = classObj.GetClassAttribs();
		VWPattern solidFill(true);
		solidFill.SetSolidPattern();
		classAttr.SetFillPattern(solidFill);
		classAttr.SetFillForeColor(CRGBColor(Uint8(0), Uint8(0), Uint8(255))); // 青
		classAttr.SetFillBackColor(CRGBColor(Uint8(0), Uint8(0), Uint8(255)));
		classAttr.SetPenForeColor(CRGBColor(Uint8(0), Uint8(0), Uint8(255))); // 青
		classAttr.SetLineWeightInMils(100);
		probe.log("[G0] クラスの属性: 塗=" + ProbePatternText(classAttr.GetFillPattern()) +
				  " 塗前=" + ProbeColorText(classAttr.GetFillForeColor()) +
				  " 線前=" + ProbeColorText(classAttr.GetPenForeColor()) +
				  " 太=" + ProbeWhole(classAttr.GetLineWeightInMils()));
	}
	else
	{
		probe.fail("[G0] AddClass が 0 を返した（クラススタイルの見分けが付かなくなる）");
	}

	MCObjectHandle memberA = ProbeMakeMember(probe, "[G0] A", 0.0);
	if (memberA == nil)
		return;
	VWParametricObj pioA(memberA);
	VWRecordFormatObj formatA = pioA.GetRecordFormat();
	probe.log("[G1] パラメータ総数=" + ProbeWhole(static_cast<long long>(pioA.GetParamsCount())));

	// ---- G1: 選択肢を 3 経路で採る ---------------------------------------
	// 対照（選択肢を持つと分かっている索引）を先に出す。ここが 0 なら経路そのものが
	// 使えないということなので、残りの 0 を「選択肢が無い」と読み違えずに済む。
	const char* const kControls[] = {"MemberType", "StructuralUse", "EndCondition", "AxisAlign"};
	probe.log("[G1] --- 対照（選択肢を持つと分かっているもの） ---");
	for (size_t at = 0; at < sizeof(kControls) / sizeof(kControls[0]); ++at)
	{
		const size_t index = ProbeIndexOf(probe, pioA, kControls[at]);
		if (index != size_t(-1))
			ProbeDumpChoices(probe, pioA, formatA, index, kControls[at]);
	}

	probe.log("[G1] --- 本題（2D / 3D 属性のポップアップ） ---");
	const char* const kTargets[] = {
		"MemberPenStyle_Above",	 "MemberFillStyle_Above", "MemberPenStyle_At",
		"MemberFillStyle_At",	 "CoverPenStyle_At",	  "CoverFillStyle_At",
		"CenterlinePenStyle_At", "CapsPenStyle_At",		  "MemberAttributes_3D",
		"CoverAttributes_3D",	 "AttributesMode",		  "AttributesMode3D",
	};
	for (size_t at = 0; at < sizeof(kTargets) / sizeof(kTargets[0]); ++at)
	{
		const size_t index = ProbeIndexOf(probe, pioA, kTargets[at]);
		if (index != size_t(-1))
			ProbeDumpChoices(probe, pioA, formatA, index, kTargets[at]);
	}

	// ---- G2: 端部を切り分ける -------------------------------------------
	// 1 回目は「全部の Display を false」にしたので、端部の子が消えたのが端部の欄の
	// せいだと言い切れなかった。ここでは **始端だけ** を落とす。
	MCObjectHandle memberB = ProbeMakeMember(probe, "[G2] B", 5000.0);
	if (memberB != nil)
	{
		VWParametricObj pioB(memberB);
		ProbeDumpChildren(probe, "[G2] B（既定）", memberB);
		const char* const kStartCaps[] = {"StartCapDisplay_Above", "StartCapDisplay_At",
										  "StartCapDisplay_Below"};
		for (size_t at = 0; at < sizeof(kStartCaps) / sizeof(kStartCaps[0]); ++at)
		{
			const size_t index = ProbeIndexOf(probe, pioB, kStartCaps[at]);
			if (index == size_t(-1))
				continue;
			pioB.SetParamBool(index, false);
			// 真偽欄は GetParamBool で比べる（文字列は "True" / "False" で返る）。
			probe.log(std::string("[G2] ") + kStartCaps[at] +
					  " ← false / 読み戻し(bool)=" + (pioB.GetParamBool(index) ? "true" : "false") +
					  " 読み戻し(文字列)=[" + ProbeText(pioB.GetParamValue(index)) + "]");
		}
		const bool ok = gSDK->ResetObject(memberB) != 0;
		probe.log(std::string("[G2] ResetObject=") + (ok ? "true" : "false"));
		for (size_t at = 0; at < sizeof(kStartCaps) / sizeof(kStartCaps[0]); ++at)
		{
			const size_t index = ProbeIndexOf(probe, pioB, kStartCaps[at]);
			if (index != size_t(-1))
			{
				probe.log(std::string("[G2] ResetObject 後 ") + kStartCaps[at] +
						  " (bool)=" + (pioB.GetParamBool(index) ? "true" : "false"));
			}
		}
		ProbeDumpChildren(probe, "[G2] B（始端だけ落とした後）", memberB);

		// 177 / 178（始端部 / 終端部）は別の欄。こちらも切り分けておく。
		const size_t startCapIndex = ProbeIndexOf(probe, pioB, "StartCap");
		if (startCapIndex != size_t(-1))
		{
			pioB.SetParamBool(startCapIndex, false);
			gSDK->ResetObject(memberB);
			probe.log("[G2] StartCap（177）← false / 読み戻し(bool)=" +
					  std::string(pioB.GetParamBool(startCapIndex) ? "true" : "false"));
			ProbeDumpChildren(probe, "[G2] B（StartCap も落とした後）", memberB);
		}
	}

	// ---- G3 / G4 / G5: 値の意味を色で当てる ------------------------------
	MCObjectHandle memberC = ProbeMakeMember(probe, "[G3] C", 10000.0);
	if (memberC != nil)
	{
		VWParametricObj pioC(memberC);
		// クラスは「切断面（_At）の構造材」に入れる。per-part の色は赤にして、
		// クラスの青と食い違わせる。
		const size_t classIndex = ProbeIndexOf(probe, pioC, "MemberClass_At");
		if (classIndex != size_t(-1) && probeClass != 0)
		{
			pioC.SetParamClass(classIndex, probeClass);
			TXString wroteClass;
			gSDK->ClassIDToName(pioC.GetParamClass(classIndex), wroteClass);
			probe.log("[G3] MemberClass_At ← [" + std::string(kStructMemberProbeClass) +
					  "] / 読み戻し=[" + ProbeText(wroteClass) + "]");
		}
		const size_t fillColorIndex = ProbeIndexOf(probe, pioC, "MemberFillColor_At");
		if (fillColorIndex != size_t(-1))
		{
			const CRGBColor red(Uint8(255), Uint8(0), Uint8(0));
			pioC.SetParamColor(fillColorIndex, red.GetColorIndex());
			probe.log("[G3] MemberFillColor_At ← 赤 / 読み戻し=[" +
					  ProbeText(pioC.GetParamValue(fillColorIndex)) + "]");
		}
		const size_t penColorIndex = ProbeIndexOf(probe, pioC, "MemberPenColor_At");
		if (penColorIndex != size_t(-1))
		{
			const CRGBColor red(Uint8(255), Uint8(0), Uint8(0));
			pioC.SetParamColor(penColorIndex, red.GetColorIndex());
			probe.log("[G3] MemberPenColor_At ← 赤 / 読み戻し=[" +
					  ProbeText(pioC.GetParamValue(penColorIndex)) + "]");
		}
		gSDK->ResetObject(memberC);
		ProbeDumpChildren(probe, "[G3] C（クラス＋赤を入れた既定の値のまま）", memberC);

		ProbeSweepValues(probe, memberC, pioC, "[G3]", "MemberFillStyle_At");
		ProbeSweepValues(probe, memberC, pioC, "[G3]", "MemberPenStyle_At");
		ProbeSweepValues(probe, memberC, pioC, "[G4]", "AttributesMode");
		ProbeSweepValues(probe, memberC, pioC, "[G5]", "AttributesMode3D");
		ProbeSweepValues(probe, memberC, pioC, "[G5]", "MemberAttributes_3D");
	}

	probe.log("おわり");
}
