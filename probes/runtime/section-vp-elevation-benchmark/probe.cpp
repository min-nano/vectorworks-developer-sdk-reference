//
//	probes/runtime/section-vp-elevation-benchmark/probe.cpp
//
//	[issue #130] 断面ビューポートの注釈へレベルオブジェクトを置いたとき、**表示される
//	高さが何で決まるか**を確定させる。
//
//	1 回目の走行（同じ slug）で分かったこと:
//	  * 名前は 3 つある——`Elevation Benchmark`（レベル（横断面）（レガシー）・内部 ID 102）、
//	    `Elevation Benchmark2`（レベル基準線）、`Stake Object`（レベル）。
//	  * レガシーを注釈の Y=0 / 2800 / 5600 へ置いても、**描かれた高さは 3 本とも 0 のまま**
//	    ——挿入点の Y は既定では読まれない。動かしても変わらない。
//	  * ところが真偽欄 `UseY` を true にすると、Y=2800 に置いた個体の高さが **2800** に
//	    なった（`Use Control Point` は -2800）。**注釈の Y を読む口はある**。
//
//	そこで 2 回目はこう詰める:
//	  1. **レベル基準線（`Elevation Benchmark2`）も同じ battery に掛ける**（1 回目は
//	     レガシーしか見ていない）。
//	  2. 真偽欄の総当たりを **2 つの高さ（2800 と 5600）で**行う——「両方でその Y が
//	     出た欄」だけが「注釈の Y を読む口」だと言い切れる（1 点では偶然を消せない）。
//	  3. その口を入れた個体を**動かして**、表示が追うかを見る。
//	  4. **ポップアップ欄の選択肢を総当たり**する（`GetParamChoices` で universal 名を
//	     採り、1 つずつ入れて描き直す）——基準の決め方と「数値を出さない」設定はここに
//	     出るはず。
//	  5. 文字欄の総当たり（表示名 "GL" を書く口）。
//
//	目視は頼まない——PIO が吐いた図形からテキストを読み出して比べる。
//

#include "Probe.h"

// レベルオブジェクトが「拘束されているか」を見る口。**このヘッダは VectorworksSDK.h
// からは引き込まれない**ので名指しで include する（-I に SDKLib/Include/Interfaces が
// 入っている前提。plugin/CMakeLists.txt）。
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
		std::snprintf(buf, sizeof(buf), "%.1f", v);
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

	struct BmParam
	{
		size_t index = 0;
		std::string name;
		std::string localized;
		short style = 0;
		std::string value;
	};

	std::vector<BmParam> BmReadParams(MCObjectHandle h)
	{
		std::vector<BmParam> rows;
		if (h == nil)
			return rows;
		size_t count = 0;
		try
		{
			VWParametricObj probeObj(h);
			count = probeObj.GetParamsCount();
		}
		catch (...)
		{
			return rows;
		}
		VWParametricObj obj(h);
		for (size_t i = 0; i < count; ++i)
		{
			BmParam row;
			row.index = i;
			TXString uname;
			try
			{
				uname = obj.GetParamName(i);
				row.name = BmToStd(uname);
			}
			catch (...)
			{
				continue;
			}
			try
			{
				row.localized = BmToStd(obj.GetParamLocalizedName(i));
			}
			catch (...)
			{
				row.localized = "(例外)";
			}
			try
			{
				row.style = static_cast<short>(obj.GetParamStyle(uname));
			}
			catch (...)
			{
				row.style = -1;
			}
			try
			{
				row.value = BmToStd(obj.GetParamAsString(uname));
			}
			catch (...)
			{
				row.value = "(例外)";
			}
			rows.push_back(row);
		}
		return rows;
	}

	// 注釈へ 1 本置く（作る → 注釈へ移す → 注釈空間の座標を書き直す → 描き直す）。
	MCObjectHandle BmPlaceInAnnotation(const std::string& pioName, MCObjectHandle vp,
									   MCObjectHandle designLayer, double x, double y)
	{
		gSDK->SetCurrentLayer(designLayer);
		const MCObjectHandle h =
			gSDK->CreateCustomObject(TXString(pioName.c_str()), WorldPt(x, y), 0.0);
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

	bool BmContains(const std::string& haystack, const std::string& needle)
	{
		return haystack.find(needle) != std::string::npos;
	}

	// 1 つの PIO を、断面ビューポートの注釈の中で総当たりに掛ける。
	void BmRunBattery(vwprobe::Report& probe, const std::string& pioName, MCObjectHandle vp,
					  MCObjectHandle designLayer)
	{
		probe.log("");
		probe.log("========== 〈" + pioName + "〉 ==========");

		EVSPluginType type = kVSPluginMenu;
		if (!gSDK->GetPluginType(TXString(pioName.c_str()), type) || type != kVSPluginObject)
		{
			probe.log("PIO として見つからない（種別=" + std::to_string(static_cast<int>(type)) +
					  "）ので飛ばす");
			return;
		}
		gSDK->DefineCustomObject(TXString(pioName.c_str()), kCustomObjectPrefNever);

		TXString localized;
		gSDK->GetLocalizedPluginName(TXString(pioName.c_str()), localized);
		probe.log("ローカライズ名=" + BmToStd(localized));

		using namespace VectorWorks::Extension;
		IMarkersPluginSupportPtr markers(IID_MarkersPluginSupport);

		// --- 1) 注釈の 3 つの高さへ素のまま置く（1 回目の追試） ---
		static const double kYs[] = {0.0, 2800.0, 5600.0};
		std::vector<BmParam> baseParams;
		for (int i = 0; i < 3; ++i)
		{
			const MCObjectHandle h = BmPlaceInAnnotation(pioName, vp, designLayer, 1000.0, kYs[i]);
			if (h == nil)
			{
				probe.fail("注釈へ置けなかった（Y=" + BmNum(kYs[i]) + "・" + pioName + "）");
				continue;
			}
			if (i == 0)
				baseParams = BmReadParams(h);
			probe.log("素のまま Y=" + BmNum(kYs[i]) + ": 文字=" + BmTextsOf(h) +
					  (markers
						   ? std::string(" 拘束=") +
								 (markers->IsElevationBenchmarkConstrained(h) ? "true" : "false")
						   : std::string("")));
		}
		if (baseParams.empty())
		{
			probe.fail("パラメータ表を読めなかった（" + pioName + "）");
			return;
		}
		probe.log("欄の件数: " + std::to_string(baseParams.size()));

		// --- 2) 真偽欄の総当たりを 2 つの高さで行う ---
		//     「2800 に置いたら 2800、5600 に置いたら 5600 が出た」欄だけが、
		//     注釈の Y（＝Z）を読む口だと言い切れる。
		probe.log("--- 真偽欄の総当たり（Y=2800 と Y=5600 の 2 点で）---");
		std::vector<std::string> readsY;
		for (const BmParam& row : baseParams)
		{
			if (row.style != 2)
				continue;
			std::string texts[2];
			for (int k = 0; k < 2; ++k)
			{
				const double y = (k == 0) ? 2800.0 : 5600.0;
				const MCObjectHandle h = BmPlaceInAnnotation(pioName, vp, designLayer, 2000.0, y);
				if (h == nil)
				{
					texts[k] = "(置けず)";
					continue;
				}
				try
				{
					VWParametricObj obj(h);
					const TXString uname(row.name.c_str());
					obj.SetParamBool(uname, !obj.GetParamBool(uname));
					gSDK->ResetObject(h);
					texts[k] = BmTextsOf(h);
				}
				catch (...)
				{
					texts[k] = "(例外)";
				}
				gSDK->DeleteObject(h, true);
			}
			const bool follows = BmContains(texts[0], "2800") && BmContains(texts[1], "5600");
			probe.log("  " + row.name + " (" + row.localized + ") 反転: Y=2800→" + texts[0] +
					  " / Y=5600→" + texts[1] + (follows ? "　★ Y を読んでいる" : ""));
			if (follows)
				readsY.push_back(row.name);
		}
		if (readsY.empty())
			probe.log("  → 注釈の Y を読む真偽欄は見つからなかった");

		// --- 3) その口を入れた個体を動かす（表示が追うか） ---
		for (const std::string& name : readsY)
		{
			const MCObjectHandle h = BmPlaceInAnnotation(pioName, vp, designLayer, 2500.0, 2800.0);
			if (h == nil)
				continue;
			try
			{
				VWParametricObj obj(h);
				const TXString uname(name.c_str());
				obj.SetParamBool(uname, !obj.GetParamBool(uname));
				gSDK->ResetObject(h);
				probe.log("  " + name + " を入れた個体: 置いた直後（Y=2800）=" + BmTextsOf(h));
				gSDK->MoveObject(h, 0, 2800 /* 2800 → 5600 */);
				gSDK->ResetObject(h);
				probe.log("  " + name + " を入れた個体: Y=5600 へ動かして描き直し=" + BmTextsOf(h));
				gSDK->MoveObject(h, 0, 1400 /* 5600 → 7000。描き直さずに読む */);
				probe.log(
					"  " + name +
					" を入れた個体: さらに Y=7000 へ動かして**描き直さずに**読む=" + BmTextsOf(h));
				gSDK->ResetObject(h);
				probe.log("  " + name + " を入れた個体: 描き直した後=" + BmTextsOf(h));
			}
			catch (...)
			{
				probe.log("  " + name + ": 移動の試験で例外");
			}
			gSDK->DeleteObject(h, true);
		}

		// --- 4) ポップアップ欄の総当たり（選択肢を 1 つずつ入れて描き直す） ---
		//     基準の決め方と「数値を出さない」設定はここに出るはず。Y を読む口は
		//     入れた状態で見る（そうでないと全部 0 になって見分けが付かない）。
		probe.log("--- ポップアップ欄の選択肢を総当たり（Y=2800 の注釈で）---");
		int popupTrials = 0;
		for (const BmParam& row : baseParams)
		{
			if (row.style != 8 && row.style != 9)
				continue;
			std::vector<std::string> choices;
			{
				const MCObjectHandle probeHandle =
					BmPlaceInAnnotation(pioName, vp, designLayer, 3000.0, 2800.0);
				if (probeHandle == nil)
					continue;
				try
				{
					VWParametricObj obj(probeHandle);
					TXStringSTLArray univ;
					if (obj.GetParamChoices(row.index, univ))
						for (size_t c = 0; c < univ.size(); ++c)
							choices.push_back(BmToStd(univ[c]));
				}
				catch (...)
				{
				}
				gSDK->DeleteObject(probeHandle, true);
			}
			if (choices.empty())
			{
				probe.log("  " + row.name + " (" + row.localized + "): 選択肢を取れなかった");
				continue;
			}
			probe.log("  " + row.name + " (" + row.localized + "): 選択肢 " +
					  std::to_string(choices.size()) + " 件");
			for (const std::string& choice : choices)
			{
				if (++popupTrials > 60)
					break;
				const MCObjectHandle h =
					BmPlaceInAnnotation(pioName, vp, designLayer, 3000.0, 2800.0);
				if (h == nil)
					continue;
				try
				{
					VWParametricObj obj(h);
					for (const std::string& yName : readsY)
					{
						const TXString yUname(yName.c_str());
						obj.SetParamBool(yUname, !obj.GetParamBool(yUname));
					}
					const TXString uname(row.name.c_str());
					obj.SetParamValue(uname, TXString(choice.c_str()));
					gSDK->ResetObject(h);
					probe.log("      〈" + choice + "〉→ 読み戻し=〈" +
							  BmToStd(obj.GetParamAsString(uname)) + "〉 文字=" + BmTextsOf(h));
				}
				catch (...)
				{
					probe.log("      〈" + choice + "〉→ 例外");
				}
				gSDK->DeleteObject(h, true);
			}
			if (popupTrials > 60)
			{
				probe.log("  （総当たりが 60 回を超えたので打ち切る）");
				break;
			}
		}

		// --- 5) 文字欄の総当たり（表示名を書く口） ---
		probe.log("--- 文字欄の総当たり（目印を書いて、絵に出るかを見る）---");
		int textTried = 0;
		for (const BmParam& row : baseParams)
		{
			if (row.style != 4)
				continue;
			if (++textTried > 12)
			{
				probe.log("  （文字欄が 12 件を超えたので打ち切る）");
				break;
			}
			const MCObjectHandle h = BmPlaceInAnnotation(pioName, vp, designLayer, 4000.0, 2800.0);
			if (h == nil)
				continue;
			try
			{
				VWParametricObj obj(h);
				const TXString uname(row.name.c_str());
				obj.SetParamString(uname, TXString("GLしるし"));
				gSDK->ResetObject(h);
				probe.log("  " + row.name + " (" + row.localized + "): 読み戻し=〈" +
						  BmToStd(obj.GetParamAsString(uname)) + "〉 文字=" + BmTextsOf(h));
			}
			catch (...)
			{
				probe.log("  " + row.name + ": 例外");
			}
			gSDK->DeleteObject(h, true);
		}

		gSDK->UpdateViewport(vp);
	}
} // namespace

VW_PROBE("section-vp-elevation-benchmark",
		 "断面ビューポートの注釈でレベルの高さが何で決まるかを確定する",
		 "レベル基準線（Elevation Benchmark2）とレガシー（Elevation Benchmark）を断面"
		 "ビューポートの注釈へ置き、真偽欄を 2 つの高さで総当たりして「注釈の Y を読む口」"
		 "を特定し、動かして追うかを見る。ポップアップの選択肢と文字欄も総当たりする")
{
	const MCObjectHandle designLayer = gSDK->GetCurrentLayer();

	// 断面に何か映るように壁を 1 枚置く。
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

	BmRunBattery(probe, "Elevation Benchmark2", vp, designLayer);
	BmRunBattery(probe, "Elevation Benchmark", vp, designLayer);

	probe.log("");
	probe.log("おわり（図面には試験用のシートレイヤ・断面ビューポート・壁が残る）");
}
