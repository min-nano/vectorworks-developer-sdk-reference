//
//	probes/runtime/level-marker-story-association/probe.cpp
//
//	[issue #130] レベル基準線の「名前」と「ストーリレベルとの関連付け」を詰める（2 回目）。
//
//	前回の走行で分かったこと:
//	  * **`Datum` に動的な選択肢は無い**（`PopupGetChoices` は欄名版・欄索引版とも 0 件）。
//	    実機の OIP が見せている `FL` は、プラグインの UI が実行時に組んでいるもので、
//	    レコードには入っていない——**前回の見当は外れ**。
//	  * **ストーリ従属のデザインレイヤに置けば、何も書かなくても関連付く**
//	    （`Datum` は既定の `UserReference` のまま、描かれた文字が 〈FL-2 階〉・高さ 2800）。
//	    **関連付けは置き場所から自動で決まる**のであって、`Datum` で選ぶものではない。
//	  * 断面ビューポートの注釈では、何を書いても 〈-〉・高さ 0 のまま。
//	  * **マーカーレイアウトの実体はプロファイルグループ**（型 11 のグループ。中に
//	    `#Elev#` と `#STLT#-#STPS#` のテキスト）——**データタグのタグレイアウトと同じ作り**
//	    （Findings「Data Tags」）。
//
//	そこで今回は、実用の答えに直結する 2 つを確かめる:
//	  A. **注釈の個体でも、レイアウトのテキストを自分の文字へ差し替えれば名前を出せるか**
//	     （`#STLT#-#STPS#` が解決されないなら、そこを "GL" に置き換えればよい）。
//	     差し替えは 2 通り試す——①テキストの中身を入れ替える ②新しいテキストを作って
//	     グループごと渡し直す。
//	  B. **注釈の個体をストーリレベルへ結び付ける道が本当に無いか**——実在するストーリ名と
//	     レベル種別名を `__StoryName` / `__LevelTypeName` へ**組で**書いてみる（前回は
//	     でたらめな文字を 1 つずつ書いただけだった）。書く順番も 2 通り試す。
//	  C. 前回できなかった試験——**ストーリの高さを変えたら、関連付いた個体は追うか**。
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

VW_PROBE("level-marker-story-association",
		 "レベル基準線の名前をレイアウトから出せるか・注釈でストーリへ結べるか",
		 "マーカーレイアウト（＝プロファイルグループ）のテキストを自分の文字へ差し替えて"
		 "名前が出るかを 2 通りの手で試し、実在するストーリ名とレベル種別名を組で書いて"
		 "注釈の個体が関連付くかを見る。ストーリの高さを変えたら追うかも確かめる")
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
	MCObjectHandle story2 = nil;
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
		{
			storyLayer = layer;
			story2 = story;
		}
	}
	if (storyLayer == nil)
	{
		probe.fail("ストーリ従属レイヤを作れなかった");
		return;
	}

	gSDK->DefineCustomObject(TXString(kLevelMarker), kCustomObjectPrefNever);

	// --- 断面ビューポート（**ストーリ従属レイヤに壁を置いて**から作る）---
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

	// =========================================================================
	// A) レイアウトのテキストを自分の文字へ差し替える
	// =========================================================================
	probe.log("--- A: マーカーレイアウトのテキストを差し替える（注釈 Y=2800）---");

	// A-1: テキストの中身を入れ替える（DeleteText ＋ AddTextFromBuffer）。
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 1000.0, 2800.0);
		if (h != nil)
		{
			LmWrite(h, "Axis", "YAxis2DMode"); // 高さは注釈の Y から出させる
			gSDK->ResetObject(h);
			LmDescribeLayout(probe, "差し替える前", h);
			const MCObjectHandle profile = gSDK->GetCustomObjectProfileGroup(h);
			std::vector<MCObjectHandle> texts;
			LmCollectTexts(profile, texts, 0);
			bool touched = false;
			for (MCObjectHandle text : texts)
			{
				const std::string body = LmToStd(gSDK->GetTextChars(text));
				if (body.find("#STLT#") == std::string::npos)
					continue;
				const Sint32 length = gSDK->GetTextLength(text);
				gSDK->DeleteText(text, 0, length);
				static const UCChar kGL[] = {'G', 'L'};
				const Boolean added = gSDK->AddTextFromBuffer(text, 0, kGL, 2);
				probe.log(std::string("  ①中身を入れ替える: 元の長さ=") + std::to_string(length) +
						  " AddTextFromBuffer=" + (added ? "true" : "false") +
						  " 入れ替え後のテキスト=〈" + LmToStd(gSDK->GetTextChars(text)) + "〉");
				touched = true;
			}
			if (!touched)
				probe.log("  ①#STLT# を含むテキストが見つからなかった");
			gSDK->ResetObject(h);
			LmDescribeLayout(probe, "差し替えた後", h);
			probe.log("  ①描き直した後の絵の文字: " + LmTextsOf(h));
			gSDK->DeleteObject(h, true);
		}
	}

	// A-2: 新しいテキストを作って、グループごと渡し直す。
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 1500.0, 2800.0);
		if (h != nil)
		{
			LmWrite(h, "Axis", "YAxis2DMode");
			gSDK->ResetObject(h);
			const MCObjectHandle profile = gSDK->GetCustomObjectProfileGroup(h);
			if (profile == nil)
			{
				probe.log("  ②プロファイルグループが nil");
			}
			else
			{
				// 中のテキストのうち #STLT# のものを消し、同じ位置へ "GL" を作って入れる。
				std::vector<MCObjectHandle> texts;
				LmCollectTexts(profile, texts, 0);
				for (MCObjectHandle text : texts)
				{
					if (LmToStd(gSDK->GetTextChars(text)).find("#STLT#") == std::string::npos)
						continue;
					WorldRect bounds;
					const bool gotBounds = gSDK->GetObjectBounds(text, bounds);
					const WorldPt origin =
						gotBounds ? WorldPt(bounds.left, bounds.top) : WorldPt(0, 0);
					gSDK->DeleteObject(text, false);
					const MCObjectHandle fresh =
						gSDK->CreateTextBlock(TXString("GL"), origin, false, 0);
					const bool moved = fresh != nil && gSDK->AddObjectToContainer(fresh, profile);
					probe.log(std::string("  ②新しいテキストを作って入れる: 作成=") +
							  (fresh != nil ? "ok" : "nil") +
							  " グループへ=" + (moved ? "true" : "false"));
				}
				try
				{
					VWParametricObj obj(h);
					obj.SetObjectProfileGroup(profile);
				}
				catch (...)
				{
					probe.log("  ②SetObjectProfileGroup で例外");
				}
				gSDK->ResetObject(h);
				LmDescribeLayout(probe, "渡し直した後", h);
				probe.log("  ②描き直した後の絵の文字: " + LmTextsOf(h));
			}
			gSDK->DeleteObject(h, true);
		}
	}

	// =========================================================================
	// B) 実在するストーリ名とレベル種別名を「組で」書く
	// =========================================================================
	probe.log("--- B: __StoryName ＋ __LevelTypeName を組で書く（注釈 Y=2800）---");
	struct LmOrder
	{
		const char* label;
		bool datumFirst;
	};
	static const LmOrder kOrders[] = {{"名前を先に書く", false}, {"Datum を先に書く", true}};
	MCObjectHandle associated = nil;
	for (const LmOrder& order : kOrders)
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, storyLayer, 2000.0, 2800.0);
		if (h == nil)
			continue;
		if (order.datumFirst)
			LmWrite(h, "Datum", "StoryLevel");
		LmWrite(h, "__StoryName", "2 階");
		LmWrite(h, "__LevelTypeName", "FL");
		if (!order.datumFirst)
			LmWrite(h, "Datum", "StoryLevel");
		gSDK->ResetObject(h);
		LmDescribe(probe, order.label, h);
		if (associated == nil && LmTextsOf(h).find("FL-") != std::string::npos)
			associated = h;
		else
			gSDK->DeleteObject(h, true);
	}

	// デザインレイヤの個体（前回、何も書かずに関連付いた）を比較用に 1 本残す。
	const MCObjectHandle onLayer = LmCreateOn(storyLayer, 0, 0);
	if (onLayer != nil)
		LmDescribe(probe, "比較: ストーリ従属レイヤに素のまま", onLayer);

	// =========================================================================
	// C) ストーリの高さを変えたら追うか
	// =========================================================================
	probe.log("--- C: ストーリの高さを 2800 → 3500 へ変える ---");
	if (story2 == nil)
	{
		probe.log("  ストーリのハンドルが無い");
	}
	else
	{
		const bool changed = gSDK->SetStoryElevation(story2, 3500);
		probe.log(std::string("  SetStoryElevation=") + (changed ? "true" : "false"));
		if (onLayer != nil)
		{
			gSDK->ResetObject(onLayer);
			LmDescribe(probe, "変えた後: レイヤの個体", onLayer);
		}
		if (associated != nil)
		{
			gSDK->ResetObject(associated);
			gSDK->UpdateViewport(vp);
			LmDescribe(probe, "変えた後: 注釈の個体", associated);
		}
		else
		{
			probe.log("  注釈で関連付いた個体は無かった");
		}
	}

	gSDK->UpdateViewport(vp);
	probe.log("おわり（図面にはストーリ 2 つ・シートレイヤ・断面ビューポート・壁が残る）");
}
