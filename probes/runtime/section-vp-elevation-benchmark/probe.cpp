//
//	probes/runtime/section-vp-elevation-benchmark/probe.cpp
//
//	[issue #130] 4 回目。残っているのは**ストーリレベルとの関連付け**だけ。
//
//	ここまでに実機で確定したこと:
//	  * レガシー（`Elevation Benchmark`）は `Elevation Display`＝
//	    〈Y value relative to reference elevation〉で注釈の Y がそのまま高さになり、
//	    動かすと追う。`DatumY` で基準を引ける。〈Custom〉＋`Elevation` で数値を消せ、
//	    `Title` に名前を書ける。
//	  * レベル基準線（`Elevation Benchmark2`）は `Axis`＝〈YAxis2DMode〉で注釈の Y が
//	    出て、動かすと追う。`RefElev` で基準を引ける。
//	  * ただし `Axis`＝Y にすると `Datum` は〈UserReference〉へ倒され、Z 基準の 3 つ
//	    （GroundPlane / DesignLayerZ / **StoryLevel**）は書いても戻された。
//
//	つまり「**Axis を Z（既定）のまま**なら、ストーリのある文書で `Datum`＝
//	〈StoryLevel〉が入るのか」が未確認のまま残っている。ここを:
//	  1. **ストーリ従属のデザインレイヤ**（土俵として正しい場所）と
//	  2. **断面ビューポートの注釈**（実際に置きたい場所）
//	の両方で試して切り分ける。「注釈だから入らない」のか「SDK からは書けない」のかは、
//	この 2 つを並べないと言い分けられない。
//
//	併せて、レベル基準線の〈Custom〉＋`CustElev` が**欄には入るのに絵に出なかった**件を
//	Axis の両方で確かめ直す（描き直しを 2 度掛けても出ないか）。
//

#include "Probe.h"

#include "VectorWorks/Extension/IMarkersPluginSupport.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	std::string BmToStd(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	void BmCollectTexts(MCObjectHandle h, std::vector<std::string>& out, int depth)
	{
		if (h == nil || depth > 5)
			return;
		for (MCObjectHandle child = gSDK->FirstMemberObj(h); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (gSDK->GetObjectTypeN(child) == kTextNode)
			{
				std::string text = BmToStd(gSDK->GetTextChars(child));
				for (size_t i = 0; i < text.size(); ++i)
					if (text[i] == '\n' || text[i] == '\r')
						text[i] = ' ';
				out.push_back(text);
			}
			else
			{
				BmCollectTexts(child, out, depth + 1);
			}
		}
	}

	std::string BmTextsOf(MCObjectHandle h)
	{
		std::vector<std::string> texts;
		BmCollectTexts(h, texts, 0);
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

	std::string BmRead(MCObjectHandle h, const char* param)
	{
		try
		{
			VWParametricObj obj(h);
			return BmToStd(obj.GetParamAsString(TXString(param)));
		}
		catch (...)
		{
			return "(例外)";
		}
	}

	void BmWrite(MCObjectHandle h, const char* param, const char* value)
	{
		try
		{
			VWParametricObj obj(h);
			obj.SetParamValue(TXString(param), TXString(value));
		}
		catch (...)
		{
		}
	}

	// 1 本の素性を 1 行で出す。
	void BmDescribe(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		using namespace VectorWorks::Extension;
		IMarkersPluginSupportPtr markers(IID_MarkersPluginSupport);
		probe.log("  " + tag + ": Axis=〈" + BmRead(h, "Axis") + "〉 Datum=〈" +
				  BmRead(h, "Datum") + "〉 Elevation 欄=〈" + BmRead(h, "Elevation") +
				  "〉 RefElev=〈" + BmRead(h, "RefElev") + "〉 __StoryName=〈" +
				  BmRead(h, "__StoryName") + "〉 __LevelTypeName=〈" +
				  BmRead(h, "__LevelTypeName") + "〉 拘束=" +
				  (markers ? (markers->IsElevationBenchmarkConstrained(h) ? "true" : "false")
						   : "(口なし)") +
				  " 文字=" + BmTextsOf(h));
	}

	MCObjectHandle BmCreateOn(MCObjectHandle layer, double x, double y)
	{
		gSDK->SetCurrentLayer(layer);
		const MCObjectHandle h =
			gSDK->CreateCustomObject(TXString("Elevation Benchmark2"), WorldPt(x, y), 0.0);
		if (h != nil)
			gSDK->ResetObject(h);
		return h;
	}

	MCObjectHandle BmPlaceInAnnotation(MCObjectHandle vp, MCObjectHandle designLayer, double x,
									   double y)
	{
		const MCObjectHandle h = BmCreateOn(designLayer, x, y);
		if (h == nil)
			return nil;
		if (!gSDK->AddViewportAnnotationObject(vp, h))
			return nil;
		try
		{
			VWParametricObj obj(h);
			obj.SetPointObjectPos(VWPoint2D(x, y));
		}
		catch (...)
		{
		}
		gSDK->ResetObject(h);
		return h;
	}
} // namespace

VW_PROBE("section-vp-elevation-benchmark",
		 "レベル基準線をストーリレベルへ関連付けられるかを確かめる",
		 "ストーリのある文書で、レベル基準線の Datum＝StoryLevel が「ストーリ従属の"
		 "デザインレイヤ」と「断面ビューポートの注釈」でそれぞれ入るかを並べて見る。"
		 "カスタム文字が絵に出ない件も Axis の両方で確かめ直す")
{
	// --- 0) ストーリを 2 つ作る（Findings「レイヤ・ストーリ・重ね順」の手順） ---
	probe.log("--- 0: ストーリを 2 つ作る ---");
	TXString levelType("FL");
	probe.log(std::string("CreateLayerLevelType(FL)=") +
			  (gSDK->CreateLayerLevelType(levelType) ? "true" : "false"));
	TXString templateName("FL テンプレート");
	TXString templateLevelType("FL");
	short templateIndex = 0;
	const bool madeTemplate = gSDK->CreateStoryLevelTemplate(templateName, 1.0, templateLevelType,
															 0, 2800, templateIndex);
	probe.log(std::string("CreateStoryLevelTemplate=") + (madeTemplate ? "true" : "false"));

	MCObjectHandle storyLayer = nil;
	static const char* const kStoryNames[] = {"1 階", "2 階"};
	static const double kStoryElevs[] = {0.0, 2800.0};
	for (int i = 0; i < 2; ++i)
	{
		TXString name(kStoryNames[i]);
		TXString suffix(kStoryNames[i]);
		gSDK->CreateStory(name, suffix);
		const MCObjectHandle story = gSDK->GetNamedObject(TXString(kStoryNames[i]));
		if (story == nil)
		{
			probe.log(std::string("ストーリ 〈") + kStoryNames[i] + "〉: ハンドルを引けなかった");
			continue;
		}
		gSDK->SetStoryElevation(story, kStoryElevs[i]);
		const bool added = madeTemplate && gSDK->AddStoryLevelFromTemplate(story, templateIndex);
		const MCObjectHandle layer = gSDK->GetLayerForStory(story, TXString("FL"));
		probe.log(std::string("ストーリ 〈") + kStoryNames[i] +
				  "〉 高さ=" + std::to_string(static_cast<int>(kStoryElevs[i])) +
				  " AddStoryLevelFromTemplate=" + (added ? "true" : "false") +
				  " レイヤ=" + (layer != nil ? "生えた" : "生えていない"));
		if (i == 1 && layer != nil)
			storyLayer = layer; // 高さ 2800 の「2 階」のレイヤを使う
	}
	if (storyLayer == nil)
	{
		probe.fail("ストーリ従属レイヤを作れなかった（デザインレイヤ側の比較ができない）");
	}

	gSDK->DefineCustomObject(TXString("Elevation Benchmark2"), kCustomObjectPrefNever);

	// --- 1) ストーリ従属のデザインレイヤ（高さ 2800 の「2 階」）で ---
	probe.log("--- 1: ストーリ従属のデザインレイヤ（2 階・高さ 2800）に置く ---");
	static const char* const kDatums[] = {"", "StoryLevel", "GroundPlane", "DesignLayerZ",
										  "UserReference"};
	if (storyLayer != nil)
	{
		for (const char* datum : kDatums)
		{
			const MCObjectHandle h = BmCreateOn(storyLayer, 0, 0);
			if (h == nil)
				continue;
			if (datum[0] != '\0')
				BmWrite(h, "Datum", datum);
			gSDK->ResetObject(h);
			BmDescribe(probe,
					   std::string("Datum 書き込み=〈") +
						   (datum[0] == '\0' ? "（書かない）" : datum) + "〉",
					   h);
			gSDK->DeleteObject(h, true);
		}
	}

	// --- 断面ビューポートを用意する ---
	const MCObjectHandle designLayer = gSDK->GetCurrentLayer();
	gSDK->CreateWall(WorldPt(-2000, 0), WorldPt(2000, 0), 200);
	const MCObjectHandle sheet = gSDK->CreateLayer("プローブ用シート", 2 /* シートレイヤ */);
	if (sheet == nil)
	{
		probe.fail("CreateLayer(…, 2) が nil を返した（シートレイヤを作れない）");
		return;
	}
	const MCObjectHandle vp = gSDK->CreateSectionViewport(WorldPt(-3000, 0), WorldPt(3000, 0),
														  WorldPt(0, 3000), 0, -1000, 10000, sheet);
	if (vp == nil)
	{
		probe.fail("CreateSectionViewport が nil を返した");
		return;
	}
	gSDK->UpdateViewport(vp);

	// --- 2) 断面ビューポートの注釈で同じことを（Axis は既定の Z のまま） ---
	probe.log("--- 2: 断面ビューポートの注釈（Axis は既定の ZAxis3DMode のまま・Y=2800）---");
	for (const char* datum : kDatums)
	{
		const MCObjectHandle h = BmPlaceInAnnotation(vp, designLayer, 1000.0, 2800.0);
		if (h == nil)
			continue;
		if (datum[0] != '\0')
			BmWrite(h, "Datum", datum);
		gSDK->ResetObject(h);
		BmDescribe(probe,
				   std::string("Datum 書き込み=〈") + (datum[0] == '\0' ? "（書かない）" : datum) +
					   "〉",
				   h);
		gSDK->DeleteObject(h, true);
	}

	// --- 3) カスタム文字が絵に出ないか（Axis の両方・描き直しを 2 度） ---
	probe.log("--- 3: Datum=Custom ＋ CustElev=GL（Axis の両方・ResetObject を 2 度）---");
	static const char* const kAxes[] = {"YAxis2DMode", "ZAxis3DMode"};
	for (const char* axis : kAxes)
	{
		const MCObjectHandle h = BmPlaceInAnnotation(vp, designLayer, 2000.0, 2800.0);
		if (h == nil)
			continue;
		BmWrite(h, "Axis", axis);
		BmWrite(h, "Datum", "Custom");
		BmWrite(h, "CustElev", "GL");
		gSDK->ResetObject(h);
		const std::string once = BmTextsOf(h);
		gSDK->ResetObject(h);
		probe.log(std::string("  Axis=") + axis + ": CustElev=〈" + BmRead(h, "CustElev") +
				  "〉 Elevation 欄=〈" + BmRead(h, "Elevation") + "〉 1 度目=" + once +
				  " 2 度目=" + BmTextsOf(h));
		gSDK->DeleteObject(h, true);
	}

	gSDK->UpdateViewport(vp);
	probe.log("おわり（図面にはストーリ 2 つ・試験用のシートレイヤ・断面ビューポート・壁が残る）");
}
