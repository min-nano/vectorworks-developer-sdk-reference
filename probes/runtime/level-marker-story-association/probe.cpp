//
//	probes/runtime/level-marker-story-association/probe.cpp
//
//	[issue #130] **前回の結論を覆す材料が出たので、取り直す。**
//
//	#131 では「レベル基準線（`Elevation Benchmark2`）はストーリレベルへ関連付けられない
//	（`Datum`＝`StoryLevel` は書いても `GroundPlane` へ戻る）」「名前を書く口が無い」と
//	書いたが、利用者の実機の画面では**断面ビューポートの注釈に置いたレベル基準線が、
//	測定に使用する座標軸＝〈Z軸（3Dモード）〉のまま `FL-1 612` / `GL-F 0` /
//	`FL-2 3571` を出していた**——つまり**関連付いていて、高さもストーリレベルの高さ**で
//	ある。OIP の「測定基準」に入っていたのは `FL`、すなわち**レベル種別の名前**だった。
//
//	見当: `Datum` の選択肢は**インスタンスごとの動的な一覧**（レコードに持つ選択肢＝
//	`VWParametricObj::PopupGetChoices` 系）で、前回引いていた**プラグイン定義側の静的な
//	6 択**（`GetParamChoices`）とは別物ではないか。だとすれば、書くべき値は
//	`StoryLevel` ではなく**レベル種別名（`FL`）**である。
//
//	確かめること:
//	  1. `Datum` の**静的な選択肢と動的な選択肢を並べて出す**（欄名版・欄索引版の両方）。
//	     置き場所はストーリ従属のデザインレイヤと断面注釈の 2 つ。
//	  2. 動的な選択肢の値を 1 つずつ `Datum` へ書き、読み戻し・`__StoryName` /
//	     `__LevelTypeName`・高さ・**描かれた文字**がどうなるかを見る。
//	  3. 関連付いた個体で**ストーリの高さを変えたら追うか**。
//	  4. **マーカーレイアウトの実体**を突き止める——データタグのタグレイアウトは
//	     「PIO 自身のプロファイルグループ」だった（Findings「Data Tags」）ので、
//	     レベル基準線も同じか（`GetObjectProfileGroup` を辿って中身を出す）。
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

	void LmCollectTexts(MCObjectHandle h, std::vector<std::string>& out, int depth)
	{
		if (h == nil || depth > 5)
			return;
		for (MCObjectHandle child = gSDK->FirstMemberObj(h); child != nil;
			 child = gSDK->NextObject(child))
		{
			if (gSDK->GetObjectTypeN(child) == kTextNode)
			{
				std::string text = LmToStd(gSDK->GetTextChars(child));
				for (size_t i = 0; i < text.size(); ++i)
					if (text[i] == '\n' || text[i] == '\r')
						text[i] = ' ';
				out.push_back(text);
			}
			else
			{
				LmCollectTexts(child, out, depth + 1);
			}
		}
	}

	std::string LmTextsOf(MCObjectHandle h)
	{
		std::vector<std::string> texts;
		LmCollectTexts(h, texts, 0);
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

	void LmWrite(MCObjectHandle h, const char* param, const std::string& value)
	{
		try
		{
			VWParametricObj obj(h);
			obj.SetParamValue(TXString(param), TXString(value.c_str()));
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

	// `Datum` の選択肢を、静的（プラグイン定義）と動的（レコードに持つ）の両方で出す。
	// **動的なほうに実機の OIP が見せている FL などが並ぶはず**、というのがこの調査の要。
	std::vector<std::string> LmDumpChoices(vwprobe::Report& probe, const std::string& where,
										   MCObjectHandle h)
	{
		std::vector<std::string> dynamicKeys;
		if (h == nil)
			return dynamicKeys;
		size_t datumIndex = 0;
		try
		{
			VWParametricObj obj(h);
			datumIndex = obj.GetParamIndex(TXString("Datum"));
			probe.log("  " + where + ": Datum の欄索引=" + std::to_string(datumIndex));

			TXStringSTLArray staticChoices;
			const bool okStatic = obj.GetParamChoices(datumIndex, staticChoices);
			std::string line;
			for (size_t i = 0; i < staticChoices.size(); ++i)
			{
				if (!line.empty())
					line += " / ";
				line += LmToStd(staticChoices[i]);
			}
			probe.log("  " + where + ": 静的な選択肢(" + (okStatic ? "ok" : "false") + " " +
					  std::to_string(staticChoices.size()) +
					  " 件): " + (line.empty() ? "(空)" : line));
		}
		catch (...)
		{
			probe.log("  " + where + ": 静的な選択肢の取得で例外");
		}

		// 動的（レコードに持つ）選択肢——欄名版。
		try
		{
			VWParametricObj obj(h);
			const size_t count = obj.PopupGetChoicesCount(TXString("Datum"));
			std::string line;
			for (size_t i = 0; i < count; ++i)
			{
				TXString key;
				TXString value;
				obj.PopupGetChoice(TXString("Datum"), i, key, value);
				if (!line.empty())
					line += " / ";
				line +=
					std::to_string(i) + ":鍵〈" + LmToStd(key) + "〉値〈" + LmToStd(value) + "〉";
				dynamicKeys.push_back(LmToStd(key));
			}
			probe.log("  " + where + ": 動的な選択肢（欄名版）" + std::to_string(count) +
					  " 件: " + (line.empty() ? "(空)" : line));
		}
		catch (...)
		{
			probe.log("  " + where + ": 動的な選択肢（欄名版）で例外");
		}

		// 動的（レコードに持つ）選択肢——欄索引版。食い違わないかを見る。
		try
		{
			VWParametricObj obj(h);
			const size_t count = obj.PopupGetChoicesCount(datumIndex);
			std::string line;
			for (size_t i = 0; i < count; ++i)
			{
				TXString key;
				TXString value;
				obj.PopupGetChoice(datumIndex, i, key, value);
				if (!line.empty())
					line += " / ";
				line +=
					std::to_string(i) + ":鍵〈" + LmToStd(key) + "〉値〈" + LmToStd(value) + "〉";
			}
			probe.log("  " + where + ": 動的な選択肢（欄索引版）" + std::to_string(count) +
					  " 件: " + (line.empty() ? "(空)" : line));
		}
		catch (...)
		{
			probe.log("  " + where + ": 動的な選択肢（欄索引版）で例外");
		}
		return dynamicKeys;
	}

	void LmDescribe(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		probe.log("    " + tag + ": Datum=〈" + LmRead(h, "Datum") + "〉 Axis=〈" +
				  LmRead(h, "Axis") + "〉 Elevation 欄=〈" + LmRead(h, "Elevation") +
				  "〉 __StoryName=〈" + LmRead(h, "__StoryName") + "〉 __LevelTypeName=〈" +
				  LmRead(h, "__LevelTypeName") + "〉 文字=" + LmTextsOf(h));
	}

	// マーカーレイアウトの実体を探す。データタグのタグレイアウトは「PIO 自身の
	// プロファイルグループ」だった（Findings「Data Tags」）ので、まずそこを見る。
	void LmDumpLayout(vwprobe::Report& probe, MCObjectHandle h)
	{
		try
		{
			VWParametricObj obj(h);
			const MCObjectHandle profile = obj.GetObjectProfileGroup();
			if (profile == nil)
			{
				probe.log("  プロファイルグループ: nil（レイアウトはここには無い）");
			}
			else
			{
				probe.log(
					"  プロファイルグループ: 型=" + std::to_string(gSDK->GetObjectTypeN(profile)) +
					" 中の文字=" + LmTextsOf(profile));
				std::string kinds;
				for (MCObjectHandle child = gSDK->FirstMemberObj(profile); child != nil;
					 child = gSDK->NextObject(child))
				{
					if (!kinds.empty())
						kinds += " / ";
					kinds += std::to_string(gSDK->GetObjectTypeN(child));
				}
				probe.log("  プロファイルグループの中身の型: " + (kinds.empty() ? "(空)" : kinds));
			}
			const MCObjectHandle path = obj.GetObjectPath();
			probe.log(std::string("  パス: ") +
					  (path == nil ? "nil" : "型=" + std::to_string(gSDK->GetObjectTypeN(path))));
		}
		catch (...)
		{
			probe.log("  レイアウトの取得で例外");
		}
	}
} // namespace

VW_PROBE("level-marker-story-association",
		 "レベル基準線をストーリレベルへ関連付ける（#131 の結論の取り直し）",
		 "Datum の選択肢を静的・動的の両方で出し、動的な値（レベル種別名）を書いて"
		 "関連付くかを見る。ストーリの高さを変えたら追うかと、マーカーレイアウトの"
		 "実体がプロファイルグループかどうかも確かめる")
{
	// --- 0) ストーリ 2 つ＋レベル種別 2 つ ---
	probe.log("--- 0: ストーリとレベル種別を作る ---");
	TXString levelTypeFL("FL");
	TXString levelTypeGL("GL");
	probe.log(std::string("CreateLayerLevelType(FL)=") +
			  (gSDK->CreateLayerLevelType(levelTypeFL) ? "true" : "false") +
			  " CreateLayerLevelType(GL)=" +
			  (gSDK->CreateLayerLevelType(levelTypeGL) ? "true" : "false"));

	TXString templateName("FL テンプレート");
	TXString templateLevelType("FL");
	short templateIndex = 0;
	const bool madeTemplate = gSDK->CreateStoryLevelTemplate(templateName, 1.0, templateLevelType,
															 0, 2800, templateIndex);
	probe.log(std::string("CreateStoryLevelTemplate=") + (madeTemplate ? "true" : "false"));

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
		const bool added = madeTemplate && gSDK->AddStoryLevelFromTemplate(story, templateIndex);
		const MCObjectHandle layer = gSDK->GetLayerForStory(story, TXString("FL"));
		probe.log(std::string("ストーリ 〈") + kStoryNames[i] +
				  "〉 高さ=" + std::to_string(static_cast<int>(kStoryElevs[i])) +
				  " レベル追加=" + (added ? "true" : "false") +
				  " レイヤ=" + (layer != nil ? "生えた" : "生えていない"));
		if (i == 1)
		{
			storyLayer = layer;
			story2 = story;
		}
	}

	gSDK->DefineCustomObject(TXString(kLevelMarker), kCustomObjectPrefNever);

	// --- 断面ビューポートを用意する ---
	const MCObjectHandle designLayer = gSDK->GetCurrentLayer();
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

	// --- 1) 選択肢を静的・動的の両方で出す（2 つの置き場所で） ---
	probe.log("--- 1: Datum の選択肢（静的 / 動的）---");
	std::vector<std::string> keysOnLayer;
	std::vector<std::string> keysInAnnotation;
	if (storyLayer != nil)
	{
		const MCObjectHandle h = LmCreateOn(storyLayer, 0, 0);
		if (h != nil)
		{
			keysOnLayer = LmDumpChoices(probe, "デザインレイヤ（2 階）", h);
			LmDescribe(probe, "素のまま", h);
			LmDumpLayout(probe, h);
			gSDK->DeleteObject(h, true);
		}
	}
	{
		const MCObjectHandle h = LmPlaceInAnnotation(vp, designLayer, 1000.0, 2800.0);
		if (h != nil)
		{
			keysInAnnotation = LmDumpChoices(probe, "断面注釈（Y=2800）", h);
			LmDescribe(probe, "素のまま", h);
			LmDumpLayout(probe, h);
			gSDK->DeleteObject(h, true);
		}
	}

	// --- 2) 動的な選択肢の値を 1 つずつ書く ---
	probe.log("--- 2: 動的な選択肢の鍵を Datum へ書く ---");
	// 動的な一覧が空でも、実機の OIP が見せていた値（レベル種別名）は試す。
	std::vector<std::string> candidates = keysInAnnotation;
	if (candidates.empty())
		candidates = keysOnLayer;
	for (const char* extra : {"FL", "GL", "StoryLevel"})
	{
		bool known = false;
		for (const std::string& key : candidates)
			if (key == extra)
				known = true;
		if (!known)
			candidates.push_back(extra);
	}

	MCObjectHandle associated = nil;
	for (const std::string& key : candidates)
	{
		if (storyLayer != nil)
		{
			const MCObjectHandle h = LmCreateOn(storyLayer, 0, 0);
			if (h != nil)
			{
				LmWrite(h, "Datum", key);
				gSDK->ResetObject(h);
				LmDescribe(probe, "デザインレイヤ 〈" + key + "〉", h);
				gSDK->DeleteObject(h, true);
			}
		}
		const MCObjectHandle h = LmPlaceInAnnotation(vp, designLayer, 2000.0, 2800.0);
		if (h == nil)
			continue;
		LmWrite(h, "Datum", key);
		gSDK->ResetObject(h);
		LmDescribe(probe, "断面注釈 〈" + key + "〉", h);
		// 「FL-2 階」のようにレベル名が出た個体を、3) の試験に残す。
		if (associated == nil && LmTextsOf(h).find("FL") != std::string::npos)
			associated = h;
		else
			gSDK->DeleteObject(h, true);
	}

	// --- 3) ストーリの高さを変えたら追うか ---
	probe.log("--- 3: ストーリの高さを 2800 → 3500 へ変える ---");
	if (associated == nil)
	{
		probe.log("  関連付いた個体が無いので、この試験は行えない");
	}
	else if (story2 == nil)
	{
		probe.log("  ストーリのハンドルが無いので、この試験は行えない");
	}
	else
	{
		probe.log("  変える前: " + LmTextsOf(associated));
		const bool changed = gSDK->SetStoryElevation(story2, 3500);
		gSDK->ResetObject(associated);
		gSDK->UpdateViewport(vp);
		probe.log(std::string("  SetStoryElevation=") + (changed ? "true" : "false") +
				  " 変えた後: " + LmTextsOf(associated) + " Elevation 欄=〈" +
				  LmRead(associated, "Elevation") + "〉");
	}

	gSDK->UpdateViewport(vp);
	probe.log("おわり（図面にはストーリ 2 つ・シートレイヤ・断面ビューポート・壁が残る）");
}
