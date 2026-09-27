//
//	probes/runtime/section-vp-elevation-benchmark/probe.cpp
//
//	[issue #130] 断面ビューポートの注釈へレベルオブジェクトを置くときの、**表示される
//	高さの決まり方**と**名前の出し方**を確定させる（3 回目）。
//
//	ここまでに実機で分かったこと:
//	  * レベル系の PIO は 3 つ——`Elevation Benchmark2`（レベル基準線・内部 ID 663）、
//	    `Elevation Benchmark`（レベル（横断面）（レガシー）・102）、`Stake Object`
//	    （レベル・370）。
//	  * **どちらも既定では注釈の Y を読まない**（高さは 0 のまま）。
//	  * レベル基準線は `Axis`＝〈YAxis2DMode〉にすると、Y=2800 の注釈で **2800** が出た。
//	  * レガシーは真偽欄 `UseY` で Y=2800→2800 / Y=5600→5600、**動かすと追う**。
//	    ただし `UseY` は `__NNA_DO_NOT_CHANGE` の内部欄で、**表の顔は `Elevation Display`
//	    の 5 択**（Custom / ground plane / reference elevation / Y value / control point）。
//	    2 回目は内部欄を先に倒してしまったので、**この 5 択を素の個体へ書いたときに
//	    入るかは未確認**のまま。
//	  * レベル基準線の `Datum`（測定基準）は 6 択で〈StoryLevel〉を持つが、**ストーリの
//	    無い文書では書いても GroundPlane に戻された**（試験した文書にストーリが無かった）。
//
//	そこで 3 回目は:
//	  0. **ストーリを 2 つ作ってから**試す（〈StoryLevel〉を正しい土俵で確かめるため）。
//	  A. レベル基準線: `Axis`＝Y を 2 つの高さ＋移動で確かめ、`Datum` の 6 択・`RefElev`・
//	     `Offset`・`CustElev`・`Note`・前後記号・単位記号が**絵にどう出るか**を 1 つずつ。
//	  B. レガシー: `Elevation Display` の 5 択を**素の個体へ**書いて、入るか・Y を読むか・
//	     動かすと追うか。`DatumY` の引き算と、`Custom`＋`Elevation`＋`Title` で
//	     **数値を出さずに名前だけ**出せるかも。
//
//	目視は頼まない——PIO が吐いた図形からテキストを読み出して比べる。
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

	std::string BmNum(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.0f", v);
		return std::string(buf);
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

	bool BmWrite(MCObjectHandle h, const char* param, const char* value)
	{
		try
		{
			VWParametricObj obj(h);
			obj.SetParamValue(TXString(param), TXString(value));
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	MCObjectHandle BmPlace(const char* pioName, MCObjectHandle vp, MCObjectHandle designLayer,
						   double x, double y)
	{
		gSDK->SetCurrentLayer(designLayer);
		const MCObjectHandle h = gSDK->CreateCustomObject(TXString(pioName), WorldPt(x, y), 0.0);
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

	// ストーリを 2 つ作る（Findings「レイヤ・ストーリ・重ね順」の手順どおり）。
	void BmMakeStories(vwprobe::Report& probe)
	{
		TXString levelType("FL");
		probe.log(std::string("CreateLayerLevelType(FL)=") +
				  (gSDK->CreateLayerLevelType(levelType) ? "true" : "false"));

		TXString templateName("FL テンプレート");
		TXString templateLevelType("FL");
		short templateIndex = 0;
		const bool madeTemplate = gSDK->CreateStoryLevelTemplate(
			templateName, 1.0, templateLevelType, 0, 2800, templateIndex);
		probe.log(std::string("CreateStoryLevelTemplate=") + (madeTemplate ? "true" : "false") +
				  " index=" + std::to_string(static_cast<int>(templateIndex)));

		static const char* const kStoryNames[] = {"1 階", "2 階"};
		static const double kStoryElevs[] = {0.0, 2800.0};
		for (int i = 0; i < 2; ++i)
		{
			TXString name(kStoryNames[i]);
			TXString suffix(kStoryNames[i]);
			const bool made = gSDK->CreateStory(name, suffix);
			const MCObjectHandle story = gSDK->GetNamedObject(TXString(kStoryNames[i]));
			if (story == nil)
			{
				probe.log(std::string("ストーリ 〈") + kStoryNames[i] + "〉: CreateStory=" +
						  (made ? "true" : "false") + " だがハンドルを引けなかった");
				continue;
			}
			// **レベルを足す前に**高さを入れる（足した後では衝突し得る）。
			const bool setElev = gSDK->SetStoryElevation(story, kStoryElevs[i]);
			const bool added =
				madeTemplate && gSDK->AddStoryLevelFromTemplate(story, templateIndex);
			const MCObjectHandle layer = gSDK->GetLayerForStory(story, TXString("FL"));
			probe.log(std::string("ストーリ 〈") + kStoryNames[i] + "〉: 高さ=" +
					  BmNum(kStoryElevs[i]) + " SetStoryElevation=" + (setElev ? "true" : "false") +
					  " AddStoryLevelFromTemplate=" + (added ? "true" : "false") +
					  " レイヤ=" + (layer != nil ? "生えた" : "生えていない"));
		}
	}

	// レベル基準線（Elevation Benchmark2）を詰める。
	void BmRunBenchmark2(vwprobe::Report& probe, MCObjectHandle vp, MCObjectHandle designLayer)
	{
		static const char* const kName = "Elevation Benchmark2";
		probe.log("");
		probe.log("========== 〈Elevation Benchmark2〉（レベル基準線）==========");
		gSDK->DefineCustomObject(TXString(kName), kCustomObjectPrefNever);

		using namespace VectorWorks::Extension;
		IMarkersPluginSupportPtr markers(IID_MarkersPluginSupport);

		// A1: Axis=YAxis2DMode を入れて 3 つの高さへ置く。
		probe.log("--- A1: Axis=YAxis2DMode で注釈の 3 つの高さへ置く ---");
		static const double kYs[] = {0.0, 2800.0, 5600.0};
		for (int i = 0; i < 3; ++i)
		{
			const MCObjectHandle h = BmPlace(kName, vp, designLayer, 1000.0, kYs[i]);
			if (h == nil)
			{
				probe.fail("レベル基準線を注釈へ置けなかった（Y=" + BmNum(kYs[i]) + "）");
				continue;
			}
			BmWrite(h, "Axis", "YAxis2DMode");
			gSDK->ResetObject(h);
			probe.log("  Y=" + BmNum(kYs[i]) + ": Axis 読み戻し=〈" + BmRead(h, "Axis") +
					  "〉 Elevation 欄=〈" + BmRead(h, "Elevation") + "〉 文字=" + BmTextsOf(h));
			gSDK->DeleteObject(h, true);
		}

		// A2: 動かしたら追うか。
		probe.log("--- A2: Axis=Y の 1 本を動かす ---");
		{
			const MCObjectHandle h = BmPlace(kName, vp, designLayer, 1500.0, 2800.0);
			if (h != nil)
			{
				BmWrite(h, "Axis", "YAxis2DMode");
				gSDK->ResetObject(h);
				probe.log("  置いた直後（Y=2800）: " + BmTextsOf(h));
				gSDK->MoveObject(h, 0, 2800);
				gSDK->ResetObject(h);
				probe.log("  Y=5600 へ動かして描き直し: " + BmTextsOf(h));
				gSDK->DeleteObject(h, true);
			}
		}

		// A3: Datum の 6 択（Axis=Y・Y=2800・ストーリのある文書で）。
		probe.log("--- A3: Datum の 6 択（Axis=Y / 注釈 Y=2800）---");
		static const char* const kDatums[] = {"GroundPlane",   "DesignLayerZ", "ControlPoint",
											  "UserReference", "Custom",	   "StoryLevel"};
		for (const char* datum : kDatums)
		{
			const MCObjectHandle h = BmPlace(kName, vp, designLayer, 2000.0, 2800.0);
			if (h == nil)
				continue;
			BmWrite(h, "Axis", "YAxis2DMode");
			BmWrite(h, "Datum", datum);
			gSDK->ResetObject(h);
			probe.log(std::string("  〈") + datum + "〉→ 読み戻し=〈" + BmRead(h, "Datum") +
					  "〉 Elevation 欄=〈" + BmRead(h, "Elevation") + "〉 __StoryName=〈" +
					  BmRead(h, "__StoryName") + "〉 __LevelTypeName=〈" +
					  BmRead(h, "__LevelTypeName") + "〉 拘束=" +
					  (markers ? (markers->IsElevationBenchmarkConstrained(h) ? "true" : "false")
							   : "(口なし)") +
					  " 文字=" + BmTextsOf(h));
			gSDK->DeleteObject(h, true);
		}

		// A4〜A8: 1 欄ずつ（すべて Axis=Y・注釈 Y=2800）。
		struct BmTweak
		{
			const char* label;
			const char* datum; // 空なら既定のまま
			const char* param1;
			const char* value1;
			const char* param2;
			const char* value2;
		};
		static const BmTweak kTweaks[] = {
			{"A4 UserReference ＋ RefElev=1000", "UserReference", "RefElev", "1000", "", ""},
			{"A5 Offset=500", "", "Offset", "500", "", ""},
			{"A6 Custom ＋ CustElev=GL", "Custom", "CustElev", "GL", "", ""},
			{"A6' Custom ＋ CustElev=空", "Custom", "CustElev", "", "", ""},
			{"A7 Note=軒高", "", "Note", "軒高", "", ""},
			{"A7' 前記号=GL＋後記号=まで", "", "EPfx", "GL ", "ESfx", " まで"},
			{"A8 単位=Millimeters ＋ 単位記号なし", "", "PrimaryUnits", "Millimeters",
			 "ShowUnitMark", "False"},
		};
		probe.log("--- A4〜A8: 1 欄ずつ（Axis=Y / 注釈 Y=2800）---");
		for (const BmTweak& tweak : kTweaks)
		{
			const MCObjectHandle h = BmPlace(kName, vp, designLayer, 2500.0, 2800.0);
			if (h == nil)
				continue;
			BmWrite(h, "Axis", "YAxis2DMode");
			if (tweak.datum[0] != '\0')
				BmWrite(h, "Datum", tweak.datum);
			BmWrite(h, tweak.param1, tweak.value1);
			if (tweak.param2[0] != '\0')
				BmWrite(h, tweak.param2, tweak.value2);
			gSDK->ResetObject(h);
			probe.log(std::string("  ") + tweak.label + ": " + tweak.param1 + " 読み戻し=〈" +
					  BmRead(h, tweak.param1) + "〉 Elevation 欄=〈" + BmRead(h, "Elevation") +
					  "〉 文字=" + BmTextsOf(h));
			gSDK->DeleteObject(h, true);
		}
	}

	// レガシー（Elevation Benchmark）を詰める。
	void BmRunLegacy(vwprobe::Report& probe, MCObjectHandle vp, MCObjectHandle designLayer)
	{
		static const char* const kName = "Elevation Benchmark";
		probe.log("");
		probe.log("========== 〈Elevation Benchmark〉（レガシー）==========");
		gSDK->DefineCustomObject(TXString(kName), kCustomObjectPrefNever);

		// B1: Elevation Display の 5 択を**素の個体へ**書く（内部の真偽欄は触らない）。
		probe.log("--- B1: Elevation Display の 5 択を素の個体へ書く（注釈 Y=2800）---");
		static const char* const kDisplays[] = {
			"Custom", "Z value relative to ground plane", "Z value relative to reference elevation",
			"Y value relative to reference elevation", "Distance from control point"};
		for (const char* display : kDisplays)
		{
			const MCObjectHandle h = BmPlace(kName, vp, designLayer, 3000.0, 2800.0);
			if (h == nil)
				continue;
			BmWrite(h, "Elevation Display", display);
			gSDK->ResetObject(h);
			probe.log(std::string("  〈") + display + "〉→ 読み戻し=〈" +
					  BmRead(h, "Elevation Display") + "〉 UseY=〈" + BmRead(h, "UseY") +
					  "〉 文字=" + BmTextsOf(h));
			gSDK->DeleteObject(h, true);
		}

		// B2: 「Y value relative to reference elevation」を 2 つの高さ＋移動で。
		probe.log("--- B2: Y value relative to reference elevation を 2 つの高さ＋移動で ---");
		static const double kYs[] = {0.0, 2800.0, 5600.0};
		for (int i = 0; i < 3; ++i)
		{
			const MCObjectHandle h = BmPlace(kName, vp, designLayer, 3500.0, kYs[i]);
			if (h == nil)
				continue;
			BmWrite(h, "Elevation Display", "Y value relative to reference elevation");
			gSDK->ResetObject(h);
			probe.log("  Y=" + BmNum(kYs[i]) + ": 文字=" + BmTextsOf(h));
			if (i == 2)
			{
				gSDK->MoveObject(h, 0, 1400);
				gSDK->ResetObject(h);
				probe.log("  Y=7000 へ動かして描き直し: " + BmTextsOf(h));
			}
			gSDK->DeleteObject(h, true);
		}

		// B3: DatumY で基準をずらす。
		probe.log("--- B3: DatumY=1000（注釈 Y=2800）---");
		{
			const MCObjectHandle h = BmPlace(kName, vp, designLayer, 4000.0, 2800.0);
			if (h != nil)
			{
				BmWrite(h, "Elevation Display", "Y value relative to reference elevation");
				BmWrite(h, "DatumY", "1000");
				gSDK->ResetObject(h);
				probe.log("  DatumY 読み戻し=〈" + BmRead(h, "DatumY") + "〉 文字=" + BmTextsOf(h));
				gSDK->DeleteObject(h, true);
			}
		}

		// B4: 名前だけ出す（Custom ＋ Elevation ＋ Title）。
		probe.log("--- B4: 数値を出さずに名前だけ出せるか ---");
		struct BmNameCase
		{
			const char* label;
			const char* elevation;
			const char* title;
		};
		static const BmNameCase kCases[] = {
			{"Custom ＋ Elevation=空 ＋ Title=1FL", "", "1FL"},
			{"Custom ＋ Elevation=GL ＋ Title=空", "GL", ""},
			{"Custom ＋ Elevation=+2800 ＋ Title=2FL", "+2800", "2FL"},
		};
		for (const BmNameCase& nameCase : kCases)
		{
			const MCObjectHandle h = BmPlace(kName, vp, designLayer, 4500.0, 2800.0);
			if (h == nil)
				continue;
			BmWrite(h, "Elevation Display", "Custom");
			BmWrite(h, "Elevation", nameCase.elevation);
			BmWrite(h, "Title", nameCase.title);
			gSDK->ResetObject(h);
			probe.log(std::string("  ") + nameCase.label + ": Elevation Display 読み戻し=〈" +
					  BmRead(h, "Elevation Display") + "〉 文字=" + BmTextsOf(h));
			gSDK->DeleteObject(h, true);
		}
	}
} // namespace

VW_PROBE("section-vp-elevation-benchmark",
		 "断面ビューポートの注釈でレベルの高さと名前の出し方を確定する",
		 "ストーリを 2 つ作った文書で、レベル基準線の Axis / Datum / 基準高さ / "
		 "カスタム文字と、レガシーの Elevation Display 5 択・DatumY・タイトルが、"
		 "注釈の Y（＝Z）に対して絵にどう出るかを 1 欄ずつ実測する")
{
	// 0) ストーリを 2 つ作る（Datum=StoryLevel を正しい土俵で試すため）。
	probe.log("--- 0: ストーリを 2 つ作る ---");
	BmMakeStories(probe);

	const MCObjectHandle designLayer = gSDK->GetCurrentLayer();
	const MCObjectHandle wall = gSDK->CreateWall(WorldPt(-2000, 0), WorldPt(2000, 0), 200);
	probe.log(std::string("試験用の壁: ") + (wall != nil ? "作れた" : "作れなかった"));

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
	probe.log("断面ビューポートを作れた: 型=" + std::to_string(gSDK->GetObjectTypeN(vp)));
	gSDK->UpdateViewport(vp);

	BmRunBenchmark2(probe, vp, designLayer);
	BmRunLegacy(probe, vp, designLayer);

	gSDK->UpdateViewport(vp);
	probe.log("");
	probe.log("おわり（図面にはストーリ 2 つ・試験用のシートレイヤ・断面ビューポート・壁が残る）");
}
