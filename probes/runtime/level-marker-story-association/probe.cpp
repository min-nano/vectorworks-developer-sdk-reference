//
//	probes/runtime/level-marker-story-association/probe.cpp
//
//	[issue #130] 最後の 1 点——**注釈で「名前＝ストーリレベル名」と「高さ＝注釈の Y」を
//	両立できるか**。
//
//	ここまでの実測で、結び方が 3 つ組だと分かった:
//	  * `__StoryName` ＋ `__LevelTypeName` ＋ **`Datum`＝`StoryLevel`** の 3 つを書くと、
//	    注釈の個体でも `Datum` が `StoryLevel` で読み戻せて、名前が 〈FL-2 階〉になる。
//	  * **名前欄だけ**（`Datum` を書かない）だと `Datum` は `UserReference` のままで、
//	    名前も 〈-〉のまま。**`Datum` 単独**でも倒される（`GroundPlane`）。
//	    ——つまり `Datum` は「結果」でも「口」でもなく、**3 つ揃って初めて効く**。
//	  * 注釈では Z 基準の測定が 0 になる。高さを出すには `Axis`＝`YAxis2DMode`。
//	  * **シートレイヤへ直に置くと、名前欄だけでも高さがストーリレベルの高さ（2800）に
//	    なった**（`Datum` は `UserReference` のまま）。注釈だけが 0 のまま。
//
//	そこで、実用の形になる組み合わせを確かめる:
//	  1. 注釈 ＋ 3 つ組だけ（名前は出るか・高さは 0 か）。
//	  2. **注釈 ＋ 3 つ組 ＋ `Axis`＝`YAxis2DMode`**（名前と高さを両立できるか）。2 点の
//	     高さで見る。
//	  3. 注釈 ＋ 3 つ組 ＋ `RefElev`（基準を引けるか）。
//	  4. `Datum` 単独・名前欄単独の対照（どちらも効かないことの確認）。
//	  5. 参考: シートレイヤ直置きに 3 つ組（名前も出るか）。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kLevelMarker = "Elevation Benchmark2";

	std::string LmToStd(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	void LmCollectTexts(MCObjectHandle h, std::vector<MCObjectHandle>& out, int depth)
	{
		if (h == nil || depth > 5)
			return;
		for (MCObjectHandle child = gSDK->FirstMemberObj(h); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (gSDK->GetObjectTypeN(child) == kTextNode)
				out.push_back(child);
			else
				LmCollectTexts(child, out, depth + 1);
		}
	}

	std::string LmTextsOf(MCObjectHandle h)
	{
		std::vector<MCObjectHandle> texts;
		LmCollectTexts(h, texts, 0);
		if (texts.empty())
			return "(テキストなし)";
		std::string joined;
		for (size_t i = 0; i < texts.size(); ++i)
		{
			std::string text = LmToStd(gSDK->GetTextChars(texts[i]));
			for (size_t c = 0; c < text.size(); ++c)
				if (text[c] == '\n' || text[c] == '\r')
					text[c] = ' ';
			if (i != 0)
				joined += " | ";
			joined += "〈" + text + "〉";
		}
		return joined;
	}

	std::string LmRead(MCObjectHandle h, const char* param)
	{
		try
		{
			VWParametricObj obj(h);
			return LmToStd(obj.GetParamAsString(TXString(param)));
		}
		catch (...)
		{
			return "(例外)";
		}
	}

	void LmWrite(MCObjectHandle h, const char* param, const char* value)
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

	MCObjectHandle LmCreateOn(MCObjectHandle layer, double x, double y)
	{
		gSDK->SetCurrentLayer(layer);
		const MCObjectHandle h =
			gSDK->CreateCustomObject(TXString(kLevelMarker), WorldPt(x, y), 0.0);
		if (h != nil)
			gSDK->ResetObject(h);
		return h;
	}

	MCObjectHandle LmPlaceInAnnotation(MCObjectHandle vp, MCObjectHandle designLayer, double x,
									   double y)
	{
		const MCObjectHandle h = LmCreateOn(designLayer, x, y);
		if (h == nil || !gSDK->AddViewportAnnotationObject(vp, h))
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

	void LmDescribe(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		probe.log("    " + tag + ": Datum=〈" + LmRead(h, "Datum") + "〉 Elevation 欄=〈" +
				  LmRead(h, "Elevation") + "〉 __StoryName=〈" + LmRead(h, "__StoryName") +
				  "〉 __LevelTypeName=〈" + LmRead(h, "__LevelTypeName") +
				  "〉 文字=" + LmTextsOf(h));
	}

	// レイアウト（プロファイルグループ）の中身を 1 行で。
	void LmDescribeLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		const MCObjectHandle profile = gSDK->GetCustomObjectProfileGroup(h);
		if (profile == nil)
		{
			probe.log("    " + tag + " レイアウト: nil");
			return;
		}
		std::vector<MCObjectHandle> texts;
		LmCollectTexts(profile, texts, 0);
		std::string body;
		for (size_t i = 0; i < texts.size(); ++i)
		{
			if (!body.empty())
				body += " | ";
			body += "〈" + LmToStd(gSDK->GetTextChars(texts[i])) + "〉";
		}
		probe.log("    " + tag + " レイアウト: テキスト " + std::to_string(texts.size()) + " 件 " +
				  (body.empty() ? "(空)" : body));
	}
} // namespace

VW_PROBE("level-marker-story-association", "注釈でレベル名と高さを両立できるかを確かめる",
		 "__StoryName ＋ __LevelTypeName ＋ Datum=StoryLevel の 3 つ組に Axis=YAxis2DMode を"
		 "足して、名前（FL-2 階）と高さ（注釈の Y）が同時に出るかを見る。対照として "
		 "Datum 単独・名前欄単独も置く")
{
	// --- 0) ストーリ 2 つ（1 階=0 / 2 階=2800）---
	probe.log("--- 0: ストーリを作る ---");
	TXString levelTypeFL("FL");
	gSDK->CreateLayerLevelType(levelTypeFL);
	TXString templateName("FL テンプレート");
	TXString templateLevelType("FL");
	short templateIndex = 0;
	const bool madeTemplate = gSDK->CreateStoryLevelTemplate(templateName, 1.0, templateLevelType,
															 0, 2800, templateIndex);
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
			continue;
		gSDK->SetStoryElevation(story, kStoryElevs[i]);
		if (madeTemplate)
			gSDK->AddStoryLevelFromTemplate(story, templateIndex);
		const MCObjectHandle layer = gSDK->GetLayerForStory(story, TXString("FL"));
		probe.log(std::string("ストーリ 〈") + kStoryNames[i] +
				  "〉 高さ=" + std::to_string(static_cast<int>(kStoryElevs[i])) +
				  " レイヤ=" + (layer != nil ? "生えた" : "生えていない"));
		if (i == 1)
			storyLayer = layer;
	}
	if (storyLayer == nil)
	{
		probe.fail("ストーリ従属レイヤを作れなかった");
		return;
	}

	gSDK->DefineCustomObject(TXString(kLevelMarker), kCustomObjectPrefNever);

	gSDK->SetCurrentLayer(storyLayer);
	gSDK->CreateWall(WorldPt(-2000, 0), WorldPt(2000, 0), 200);
	const MCObjectHandle sheet = gSDK->CreateLayer("プローブ用シート", 2 /* シートレイヤ */);
	if (sheet == nil)
	{
		probe.fail("シートレイヤを作れなかった");
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

	// **3 つ組**——これが結ぶ手順（名前 2 つ ＋ Datum）。
	auto bindAll = [](MCObjectHandle h)
	{
		LmWrite(h, "__StoryName", "2 階");
		LmWrite(h, "__LevelTypeName", "FL");
		LmWrite(h, "Datum", "StoryLevel");
		gSDK->ResetObject(h);
	};

	// --- 1) 注釈 ＋ 3 つ組だけ ---
	probe.log("--- 1: 注釈（Y=2800）＋ 3 つ組 ---");
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 1000.0, 2800.0);
		if (h != nil)
		{
			bindAll(h);
			LmDescribe(probe, "3 つ組だけ", h);
			gSDK->DeleteObject(h, true);
		}
	}

	// --- 2) 注釈 ＋ 3 つ組 ＋ Axis=YAxis2DMode（本命） ---
	probe.log("--- 2: 注釈 ＋ 3 つ組 ＋ Axis=YAxis2DMode（名前と高さの両立）---");
	for (double y : {2800.0, 5600.0})
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 1500.0, y);
		if (h == nil)
			continue;
		bindAll(h);
		LmWrite(h, "Axis", "YAxis2DMode");
		gSDK->ResetObject(h);
		LmDescribe(probe, "注釈 Y=" + std::to_string(static_cast<int>(y)), h);
		// 書く順を変えたらどうか（Axis を先に）。
		const MCObjectHandle h2 = LmPlaceInAnnotation(vp, storyLayer, 1800.0, y);
		if (h2 != nil)
		{
			LmWrite(h2, "Axis", "YAxis2DMode");
			bindAll(h2);
			LmDescribe(probe, "注釈 Y=" + std::to_string(static_cast<int>(y)) + "（Axis を先に）",
					   h2);
			gSDK->DeleteObject(h2, true);
		}
		gSDK->DeleteObject(h, true);
	}

	// --- 3) 注釈 ＋ 3 つ組 ＋ RefElev ---
	probe.log("--- 3: 注釈 ＋ 3 つ組 ＋ Axis=Y ＋ RefElev=1000 ---");
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 2000.0, 2800.0);
		if (h != nil)
		{
			bindAll(h);
			LmWrite(h, "Axis", "YAxis2DMode");
			LmWrite(h, "RefElev", "1000");
			gSDK->ResetObject(h);
			LmDescribe(probe, "RefElev=1000", h);
			gSDK->DeleteObject(h, true);
		}
	}

	// --- 4) 対照: Datum 単独 / 名前欄単独 ---
	probe.log("--- 4: 対照（どちらも効かないはず）---");
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 2500.0, 2800.0);
		if (h != nil)
		{
			LmWrite(h, "Datum", "StoryLevel");
			gSDK->ResetObject(h);
			LmDescribe(probe, "Datum 単独", h);
			gSDK->DeleteObject(h, true);
		}
		const MCObjectHandle h2 = LmPlaceInAnnotation(vp, storyLayer, 2800.0, 2800.0);
		if (h2 != nil)
		{
			LmWrite(h2, "__StoryName", "2 階");
			LmWrite(h2, "__LevelTypeName", "FL");
			gSDK->ResetObject(h2);
			LmDescribe(probe, "名前欄だけ", h2);
			gSDK->DeleteObject(h2, true);
		}
	}

	// --- 5) 参考: シートレイヤ直置きに 3 つ組 ---
	probe.log("--- 5: 参考——シートレイヤへ直に置いて 3 つ組 ---");
	{
		const MCObjectHandle h = LmCreateOn(sheet, 0, 0);
		if (h != nil)
		{
			bindAll(h);
			LmDescribe(probe, "3 つ組", h);
			gSDK->DeleteObject(h, true);
		}
	}

	gSDK->UpdateViewport(vp);
	probe.log("おわり（図面にはストーリ 2 つ・シートレイヤ・断面ビューポート・壁が残る）");
}
