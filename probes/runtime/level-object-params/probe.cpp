//
//	probes/runtime/level-object-params/probe.cpp
//
//	[issue #130] VW 2026 で「レベル」を名乗る PIO を棚卸しする。1 回目の走行
//	（section-vp-elevation-benchmark）で、universal 名の総当たりから 3 つ見つかった:
//
//	  Elevation Benchmark   … レベル（横断面）（レガシー）  内部 ID 102
//	  Elevation Benchmark2  … レベル基準線                  内部 ID 663（と思われる）
//	  Stake Object          … レベル
//
//	1 回目はこのうち**レガシー**の 1 つしか中身を見ていない（採用の順番で先に当たった
//	ため）。ここでは 3 つとも**パラメータを丸ごと**出す——欄の universal 名・ローカライズ
//	名・欄型・値に加えて、**ポップアップ欄の選択肢**（universal / ローカライズ）まで。
//
//	1 回目は選択肢が 1 つも取れなかった（`GetLocalizedPluginChoice` は索引の取り方が
//	合っていなかった）。ここでは `VWParametricObj::GetParamChoices` /
//	`GetParamLocalizedChoices`（欄の索引で引く口）を使う。
//
//	「高さ表示（Elevation Display）に何が選べるか」は、**基準をどう決めるか**と
//	**数値を出さずに名前だけ出せるか**の両方に直結するので、ここが本題である。
//

#include "Probe.h"

#include <string>
#include <vector>

namespace
{
	std::string LvlToStd(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	// PIO が描いた図形を辿ってテキストを集める（絵に何が出たかの代わり）。
	void LvlCollectTexts(MCObjectHandle h, std::vector<std::string>& out, int depth)
	{
		if (h == nil || depth > 5)
			return;
		for (MCObjectHandle child = gSDK->FirstMemberObj(h); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (gSDK->GetObjectTypeN(child) == kTextNode)
			{
				std::string text = LvlToStd(gSDK->GetTextChars(child));
				for (size_t i = 0; i < text.size(); ++i)
					if (text[i] == '\n' || text[i] == '\r')
						text[i] = ' ';
				out.push_back(text);
			}
			else
			{
				LvlCollectTexts(child, out, depth + 1);
			}
		}
	}

	std::string LvlTextsOf(MCObjectHandle h)
	{
		std::vector<std::string> texts;
		LvlCollectTexts(h, texts, 0);
		if (texts.empty())
			return "(テキストなし)";
		std::string joined;
		for (size_t i = 0; i < texts.size(); ++i)
		{
			if (i != 0)
				joined += " | ";
			joined += "〈" + texts[i] + "〉";
		}
		return joined;
	}

	// 1 つの PIO の全欄を出す。欄型 8（ポップアップ）・9（ラジオ）は選択肢も。
	void LvlDumpOne(vwprobe::Report& probe, const std::string& pioName, double atX)
	{
		EVSPluginType type = kVSPluginMenu;
		const bool exists = gSDK->GetPluginType(TXString(pioName.c_str()), type) != 0;
		TXString localized;
		gSDK->GetLocalizedPluginName(TXString(pioName.c_str()), localized);
		probe.log("=== 〈" + pioName + "〉 実在=" + (exists ? "true" : "false") +
				  " 種別=" + std::to_string(static_cast<int>(type)) +
				  " ローカライズ名=" + LvlToStd(localized) + " ===");
		if (!exists || type != kVSPluginObject)
		{
			probe.log("  PIO ではないので飛ばす");
			return;
		}

		gSDK->DefineCustomObject(TXString(pioName.c_str()), kCustomObjectPrefNever);
		const MCObjectHandle h =
			gSDK->CreateCustomObject(TXString(pioName.c_str()), WorldPt(atX, 0), 0.0);
		if (h == nil)
		{
			probe.fail("CreateCustomObject(〈" + pioName + "〉) が nil を返した");
			return;
		}

		try
		{
			VWParametricObj obj(h);
			probe.log("  PIO 名=" + LvlToStd(obj.GetParametricName()) +
					  " 内部 ID=" + std::to_string(static_cast<int>(obj.GetInternalID())));
			const size_t count = obj.GetParamsCount();
			probe.log("  パラメータ件数: " + std::to_string(count));
			for (size_t i = 0; i < count; ++i)
			{
				TXString uname = obj.GetParamName(i);
				std::string line = "  [" + std::to_string(i) + "] " + LvlToStd(uname);
				try
				{
					line += " / " + LvlToStd(obj.GetParamLocalizedName(i));
				}
				catch (...)
				{
					line += " / (例外)";
				}
				short style = -1;
				try
				{
					style = static_cast<short>(obj.GetParamStyle(uname));
				}
				catch (...)
				{
				}
				line += " 欄型=" + std::to_string(style);
				try
				{
					line += " 値=〈" + LvlToStd(obj.GetParamAsString(uname)) + "〉";
				}
				catch (...)
				{
					line += " 値=(例外)";
				}
				probe.log(line);

				if (style != 8 && style != 9)
					continue;
				// **ここが本題**——選べる値の一覧（universal とローカライズを対にして出す）。
				try
				{
					TXStringSTLArray univ;
					TXStringSTLArray loc;
					const bool okU = obj.GetParamChoices(i, univ);
					const bool okL = obj.GetParamLocalizedChoices(i, loc);
					std::string choices;
					for (size_t c = 0; c < univ.size(); ++c)
					{
						if (!choices.empty())
							choices += " / ";
						choices += std::to_string(c) + ":〈" + LvlToStd(univ[c]) + "〉";
						if (okL && c < loc.size())
							choices += "＝〈" + LvlToStd(loc[c]) + "〉";
					}
					probe.log("      選択肢(" + std::string(okU ? "ok" : "false") + " " +
							  std::to_string(univ.size()) +
							  " 件): " + (choices.empty() ? "(空)" : choices));
				}
				catch (...)
				{
					probe.log("      選択肢: 例外");
				}
			}
		}
		catch (...)
		{
			probe.log("  VWParametricObj が例外（PIO として扱えない）");
		}

		probe.log("  作った直後に描いた文字: " + LvlTextsOf(h));
		WorldRect bounds;
		if (gSDK->GetObjectBounds(h, bounds))
			probe.log("  外形: left=" + std::to_string(bounds.left) + " top=" +
					  std::to_string(bounds.top) + " right=" + std::to_string(bounds.right) +
					  " bottom=" + std::to_string(bounds.bottom));
	}
} // namespace

VW_PROBE("level-object-params", "レベル系 PIO 3 種のパラメータと選択肢を棚卸しする",
		 "Elevation Benchmark2（レベル基準線）/ Elevation Benchmark（レガシー）/ "
		 "Stake Object（レベル）の全欄を、ポップアップの選択肢（universal ＋ "
		 "ローカライズ）まで含めて出す")
{
	static const char* const kNames[] = {"Elevation Benchmark2", "Elevation Benchmark",
										 "Stake Object"};
	double x = 0;
	for (const char* name : kNames)
	{
		LvlDumpOne(probe, name, x);
		x += 30000;
	}
	probe.log("おわり（図面にはレベル系の PIO が 3 本残る）");
}
