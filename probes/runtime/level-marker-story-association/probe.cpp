//
//	probes/runtime/level-marker-story-association/probe.cpp
//
//	[issue #130] 残り 1 点——**注釈でストーリレベルへ結んだとき、高さもレベルの高さに
//	できるか**。
//
//	ここまでに確定したこと:
//	  * **注釈でもストーリレベルへ結べる**——`__StoryName` と `__LevelTypeName` を
//	    組で書くと `Datum` が `StoryLevel` になり、描かれる名前が 〈FL-2 階〉になる
//	    （書く順番は問わない）。**ただし高さは 0 のまま**。
//	  * ストーリ従属のデザインレイヤに置いた個体は、何も書かなくても関連付き、
//	    **ストーリの高さを変えると追う**（2800 → 3500）。
//	  * **マーカーレイアウトは差し替えられる**——新しいテキストを作ってグループへ入れ、
//	    **`SetObjectProfileGroup` で渡し直す**と絵に出る。中身を入れ替えるだけでは
//	    出ない（データタグの「中身を入れてから渡す」と同じ筋）。
//
//	利用者の実機では、注釈の中のレベル基準線が **612**（＝ストーリレベルの高さ）を
//	出している。こちらの再現では 0 のままなので、その差を潰す:
//	  1. 結んだ個体を `ResetObject` 2 度＋`UpdateViewport` の後に読み直す
//	     （遅れて埋まるのか）。
//	  2. `Axis` を `YAxis2DMode` にしたら（名前はレベル名・高さは注釈の Y という組み
//	     合わせになるか）。
//	  3. `RefElev` にストーリレベルの高さを書いたら。
//	  4. 参考として、シートレイヤへ直に置いた個体（注釈ではない）はどうか。
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

VW_PROBE("level-marker-story-association", "注釈でストーリレベルへ結んだとき高さも出せるかを詰める",
		 "__StoryName ＋ __LevelTypeName で結んだ注釈の個体の高さが、描き直しの回数・"
		 "Axis・RefElev・置き場所でどう変わるかを 1 つずつ見る")
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

	// ストーリレベルへ結ぶ（前回ここまでは確定している）。
	auto bind = [](MCObjectHandle h)
	{
		LmWrite(h, "__StoryName", "2 階");
		LmWrite(h, "__LevelTypeName", "FL");
		gSDK->ResetObject(h);
	};

	// --- 1) 描き直しを重ねたら遅れて埋まるか ---
	probe.log("--- 1: 結んだ注釈の個体を、描き直しを重ねて読み直す ---");
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 1000.0, 2800.0);
		if (h != nil)
		{
			bind(h);
			LmDescribe(probe, "結んだ直後", h);
			gSDK->ResetObject(h);
			LmDescribe(probe, "もう一度描き直して", h);
			gSDK->UpdateViewport(vp);
			LmDescribe(probe, "ビューポートを更新して", h);
			gSDK->DeleteObject(h, true);
		}
	}

	// --- 2) Axis を YAxis2DMode にしたら（名前はレベル名・高さは注釈の Y か）---
	probe.log("--- 2: 結んだうえで Axis=YAxis2DMode ---");
	for (double y : {2800.0, 5600.0})
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 1500.0, y);
		if (h == nil)
			continue;
		bind(h);
		LmWrite(h, "Axis", "YAxis2DMode");
		gSDK->ResetObject(h);
		LmDescribe(probe, "注釈 Y=" + std::to_string(static_cast<int>(y)), h);
		gSDK->DeleteObject(h, true);
	}

	// --- 3) RefElev にストーリレベルの高さを書いたら ---
	probe.log("--- 3: 結んだうえで RefElev=2800 ---");
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 2000.0, 2800.0);
		if (h != nil)
		{
			bind(h);
			LmWrite(h, "RefElev", "2800");
			gSDK->ResetObject(h);
			LmDescribe(probe, "RefElev=2800", h);
			gSDK->DeleteObject(h, true);
		}
	}

	// --- 4) 参考: シートレイヤへ直に置いた個体（注釈ではない）---
	probe.log("--- 4: 参考——シートレイヤへ直に置く ---");
	{
		const MCObjectHandle h = LmCreateOn(sheet, 0, 0);
		if (h != nil)
		{
			LmDescribe(probe, "素のまま", h);
			bind(h);
			LmDescribe(probe, "結んだ後", h);
			gSDK->DeleteObject(h, true);
		}
	}

	// --- 5) 参考: ストーリ従属レイヤの個体（高さはレベルの高さになる）---
	probe.log("--- 5: 参考——ストーリ従属のデザインレイヤ ---");
	{
		const MCObjectHandle h = LmCreateOn(storyLayer, 0, 0);
		if (h != nil)
		{
			LmDescribe(probe, "素のまま", h);
			gSDK->DeleteObject(h, true);
		}
	}

	gSDK->UpdateViewport(vp);
	probe.log("おわり（図面にはストーリ 2 つ・シートレイヤ・断面ビューポート・壁が残る）");
}
