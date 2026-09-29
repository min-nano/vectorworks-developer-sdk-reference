//
//	probes/runtime/sdk-made-story-level-height/probe.cpp
//
//	[issue #141] **SDK で作ったストーリのレベルは「高さ」を持っているか。**
//
//	同じ 3 つ組（`__StoryName` ＋ `__LevelTypeName` ＋ `Datum`＝`StoryLevel`）を書いた
//	レベル基準線が、文書によって数値を出す（UI で作ったストーリの図面＝`576` / `3176` /
//	`6776`）／`0` になる（SDK だけで組み立てた文書＝#133）。個体の側は #135 で全欄まで
//	突き合わせて同じだと確定したので、**差は文書の側**——「SDK で生やしたストーリレベル」が
//	高さを持っていない疑いがある。
//
//	この調査は次の 3 段で切り分ける。**どの段も数値だけで読めるようにしてある**
//	（目視を頼まない）。
//
//	  1) **文書にあるストーリを読む（読み取りのみ）。** 階の高さ（`GetStoryElevation`）・
//	     レベルの高さ（`GetStoryLevelElevation`）・レベルのレイヤ・そして
//	     **バウンドを解かせた絶対Z**（`GetStoryObjectDataBoundHeight`）を並べる。
//	     UI で作ったストーリの図面で走らせれば、これが比較対象の実測値になる。
//	  2) **SDK でストーリを 4 通りの作り方で作り、同じ 4 つの口で読み戻す。**
//	     変種は「Findings の手順そのまま」「`SetStoryLevelElevation` を足す」
//	     「`SetStoryElevation` をレベルの後に呼ぶ」「テンプレートの `elevationOffset` を
//	     0 以外にする」。**どの作り方なら高さを持つか**が 1 回で分かる。
//	  3) **断面ビューポートを作り、注釈へ 3 つ組のレベル基準線を変種ごとに置いて
//	     `Elev` を読む。** 対照として同じ 3 つ組をそのレベルのデザインレイヤにも置く
//	     （#133 ではそちらだけ `2800` を出していた）。
//	  4) **段 3 で注釈だけが `0` になったときに、その理由をビューポートの側で探す。**
//
//	【1 回目の実測で分かったこと（2026-09-28。新規の空図面）】**段 2 の 4 変種すべてで
//	ストーリレベルは高さを持っていた**（解決Z ＝ 2800 / 5900 / 8400 / 11300）。
//	`GetStoryLevelElevation` は階内の相対Z、`SetStoryLevelElevation` も相対Z、
//	テンプレートの `elevationOffset` がその相対Z になる。`SetStoryElevation` を
//	レベルの前に呼ぶか後に呼ぶかは高さに効かない。**つまり issue #141 の見当
//	（「SDK で作ったレベルは高さを持っていない」）は外れ**だった。
//	段 3 では**デザインレイヤもシートレイヤも解決Z をそのまま描いた**（シートレイヤは
//	ストーリに属さないので、**レイヤの Z ではなく解決Z を読んでいる**ことも確定）。
//	**注釈の中だけが 4 変種とも `0`**（名前は出た）。残っているのは「注釈の中で
//	解決させる条件」だけなので、段 4 でビューポートの側を潰す:
//	  4-1) このビューポートはストーリのレイヤを表示しているか（`GetViewportLayerVisibility`）
//	  4-2) **全レイヤを表示にして更新してから作り直す**と解決するか
//	  4-3) そのうえで**新しい 1 本**を注釈へ置いたら解決するか
//	  4-4) **はじめから全レイヤを表示にして作った 2 つ目の断面ビューポート**ならどうか
//
//	【走らせる図面】**新規の空図面**。段 2・3 はストーリが 1 つも無い図面でしか走らない
//	（実物件の図面へ試験用のストーリを生やさないため）。ストーリのある図面で走らせた
//	ときは段 1 だけを行う——**UI 製の実測値を取るのはそちら**なので、
//	**「実物件の図面」と「新規の空図面」で 1 回ずつ走らせてもらう**のが揃った形になる。
//
//	【ログは PR コメントとして公開される】段 1 は開いている図面のストーリ名・レイヤ名・
//	レベル種別名をそのまま載せる。差し支えのある図面では走らせない。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kSlhBenchmark2 = "Elevation Benchmark2";
	// 段 2 で登録するレベル種別。実物件と同じ 1 種別を全ストーリで共有する形にする。
	const char* const kSlhLevelType = "SLH-FL";

	std::string SlhStr(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string SlhNum(double value)
	{
		// 図面の単位（mm）で読める桁に丸める。高さの比較しかしないので 3 桁で足りる。
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.3f", value);
		return std::string(buffer);
	}

	std::string SlhName(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		TXString name;
		gSDK->GetObjectName(h, name);
		return SlhStr(name);
	}

	std::string SlhBool(bool value)
	{
		return value ? "true" : "false";
	}

	// 図形の中のテキスト（PIO が吐いた絵の文字）を再帰で全部集める。
	// レベル基準線では、レイアウトのトークン（`#Elev#`）と**解決後の値**の両方が拾える
	// ——「絵に何が出たか」を目視に頼らず読む唯一の道（Findings「レベル（標高）オブジェクト」）。
	void SlhCollectTexts(MCObjectHandle container, std::vector<std::string>& out, int depth = 0)
	{
		if (container == nil || depth > 6)
			return;
		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil; h = gSDK->NextObject(h))
		{
			const short type = gSDK->GetObjectTypeN(h);
			if (type == kTextNode)
				out.push_back(SlhStr(gSDK->GetTextChars(h)));
			else
				SlhCollectTexts(h, out, depth + 1);
		}
	}

	std::string SlhDrawnTexts(MCObjectHandle h)
	{
		std::vector<std::string> texts;
		SlhCollectTexts(h, texts);
		if (texts.empty())
			return "(テキストなし)";
		std::string line;
		for (size_t i = 0; i < texts.size(); ++i)
		{
			if (i != 0)
				line += " | ";
			line += "〈" + texts[i] + "〉";
		}
		return line;
	}

	// **オブジェクトを作らずに「そのストーリのそのレベルは絶対Z で何処か」を SDK に解かせる。**
	// `fBound = eStoryObjectBound_Story` ＋ `fBoundStory = 0`（自階）＋ レベル種別で、
	// 入れ物（そのストーリのレイヤ）を基準に解決される
	// （Findings「パラメトリックオブジェクト」の「階やレベルを指すバウンド」）。
	std::string SlhResolvedLevelZ(MCObjectHandle container, const TXString& levelType)
	{
		if (container == nil)
			return "(入れ物が無くて解けない)";
		VectorWorks::SStoryObjectData data;
		data.fBound = VectorWorks::eStoryObjectBound_Story;
		data.fBoundStory = 0;
		data.fLayerLevelType = levelType;
		data.fOffset = 0;
		return SlhNum(gSDK->GetStoryObjectDataBoundHeight(data, container));
	}

	// **レイヤそのものの高さ**を読む。VS の `GetLayerElevation` に当たる ISDK の口は
	// 無いが、`eStoryObjectBound_LayerElevation`（＝そのオブジェクトが乗っている
	// レイヤの高さ）のバウンドを、そのレイヤを入れ物にして解かせれば読める。
	//
	// **これはストーリレベルの絶対Z とは別物になり得る。** 1 回目の実測（実物件の図面）で
	// レベル基準線が描いた数値（576 / 3176 / 6776）は、ストーリレベルの絶対Z
	// （612 / 3571 / 6374）と一致しなかった——**マーカーが読んでいるのはレベルの高さでは
	// なくレイヤの高さではないか**という筋を、この 1 行で切り分ける。
	std::string SlhLayerZ(MCObjectHandle layer)
	{
		if (layer == nil)
			return "(レイヤが無い)";
		VectorWorks::SStoryObjectData data;
		data.fBound = VectorWorks::eStoryObjectBound_LayerElevation;
		data.fBoundStory = 0;
		data.fLayerLevelType = "";
		data.fOffset = 0;
		return SlhNum(gSDK->GetStoryObjectDataBoundHeight(data, layer));
	}

	// 登録されているレベル種別の名前（**添字は 1 始まり**。0 始まりで回すと末尾を落とす）。
	std::vector<TXString> SlhLevelTypeNames()
	{
		std::vector<TXString> names;
		const short count = gSDK->GetNumLayerLevelTypes();
		for (short i = 1; i <= count; ++i)
			names.push_back(gSDK->GetLayerLevelTypeName(i));
		return names;
	}

	// 下で定義している（この関数より後ろに置いてあるので前方宣言する）。
	std::vector<MCObjectHandle> SlhAllLayers();

	// 真偽のオブジェクト変数を 1 つ読む（読めなかったことと false を区別して出す）。
	std::string SlhVarBool(MCObjectHandle h, short index)
	{
		TVariableBlock var;
		if (!gSDK->GetObjectVariable(h, index, var))
			return "読めず";
		bool value = false;
		if (!var.GetBoolean(value))
			return "真偽でない";
		return value ? "true" : "false";
	}

	// オブジェクト変数が読めるか＋中身の型番号だけを出す（構造体の中身には踏み込まない）。
	// **断面の見え方を持っている行列**（1055 / 1056）が、UI 製と SDK 製で同じ形かを見る。
	std::string SlhVarType(MCObjectHandle h, short index)
	{
		TVariableBlock var;
		if (!gSDK->GetObjectVariable(h, index, var))
			return "読めず";
		return "型" + std::to_string(static_cast<int>(var.GetType()));
	}

	// 下で定義している（この関数より後ろに置いてあるので前方宣言する）。
	std::vector<MCObjectHandle> SlhAllLayers();

	// ビューポートの素性を 1 行で。**UI 製で数値が出るものと、SDK 製で `0` になるものを
	// この 1 行で見比べて、違っている欄を探す**のがこの関数の役目。
	//
	// 分かっていること: SDK の `CreateSectionViewport` で作ったものも
	// `ovIsSectionViewport`（1054）は **true**（＝断面ビューポートとしては成立している）で、
	// レイヤを表示にしても `0` のままだった。だから差は別の欄にある。
	//   1043 `ovIsDesignLayerSectionViewport` / 1039 `ovViewportCropVisible`
	//   1055 `ovSheetLayerSectionViewportViewMatrix` / 1056 `ovSectionViewportSectionViewMatrix`
	//   1064 切断面より奥を表示 / 1065 切断面より前を表示
	std::string SlhViewportKind(MCObjectHandle vp)
	{
		int shown = 0;
		int readable = 0;
		const std::vector<MCObjectHandle> layers = SlhAllLayers();
		for (size_t i = 0; i < layers.size(); ++i)
		{
			short visibility = -99;
			if (!gSDK->GetViewportLayerVisibility(vp, layers[i], visibility))
				continue;
			++readable;
			if (visibility == 0)
				++shown;
		}
		return "断面VP=" + SlhVarBool(vp, 1054) + " 設計層断面VP=" + SlhVarBool(vp, 1043) +
			   " 奥=" + SlhVarBool(vp, 1064) + " 前=" + SlhVarBool(vp, 1065) +
			   " クロップ=" + SlhVarBool(vp, 1039) + " 行列1055=" + SlhVarType(vp, 1055) +
			   " 行列1056=" + SlhVarType(vp, 1056) + " 表示レイヤ=" + std::to_string(shown) + "/" +
			   std::to_string(readable);
	}

	// 1 つのストーリを、階の高さ・レベルの高さ・レイヤ・解決Z の 4 つの口で読む。
	// **段 1（UI 製）と段 2（SDK 製）で同じ関数を通す**ので、並べたときに比較できる。
	void SlhDumpStory(vwprobe::Report& probe, MCObjectHandle story, const std::string& indent)
	{
		if (story == nil)
		{
			probe.log(indent + "(nil のストーリ)");
			return;
		}
		probe.log(indent + "ストーリ〈" + SlhName(story) +
				  "〉 GetStoryElevation=" + SlhNum(gSDK->GetStoryElevation(story)));

		TXStringArray levels;
		gSDK->GetStoryLevels(story, levels);
		std::string levelLine =
			indent + "  GetStoryLevels（" + std::to_string(levels.GetSize()) + " 件）:";
		for (size_t i = 0; i < levels.GetSize(); ++i)
			levelLine += " 〈" + SlhStr(levels.GetAt(i)) + "〉";
		probe.log(levelLine);

		const std::vector<TXString> types = SlhLevelTypeNames();
		for (size_t i = 0; i < types.size(); ++i)
		{
			MCObjectHandle layer = gSDK->GetLayerForStory(story, types[i]);
			// **レベルが生えていないレベル種別は飛ばす**（全種別 × 全階を出すと読めなくなる）。
			if (layer == nil)
				continue;
			probe.log(indent + "  レベル種別〈" + SlhStr(types[i]) +
					  "〉 相対Z=" + SlhNum(gSDK->GetStoryLevelElevation(story, types[i])) +
					  " レベルの絶対Z=" + SlhResolvedLevelZ(layer, types[i]) +
					  " レイヤの高さ=" + SlhLayerZ(layer) + " レイヤ=〈" + SlhName(layer) + "〉");
		}
	}

	// 文書にあるストーリを、レイヤ経由で集める（`GetStoryAt` に当たる口が無いため）。
	std::vector<MCObjectHandle> SlhCollectStories()
	{
		std::vector<MCObjectHandle> stories;
		std::vector<MCObjectHandle> layers;
		gSDK->ForEachLayerN(
			[&layers](MCObjectHandle h)
			{
				if (h != nil)
					layers.push_back(h);
			});
		for (size_t i = 0; i < layers.size(); ++i)
		{
			MCObjectHandle story = gSDK->GetStoryOfLayer(layers[i]);
			if (story == nil)
				continue;
			bool seen = false;
			for (size_t j = 0; j < stories.size(); ++j)
				if (stories[j] == story)
					seen = true;
			if (!seen)
				stories.push_back(story);
		}
		return stories;
	}

	// 段 2 で作る 1 変種の素性。
	struct SlhVariant
	{
		const char* fTag;			 // ログの見出し（何が違う作り方か）
		const char* fStoryName;		 // ストーリ名（＝`__StoryName` へ書く値）
		const char* fSuffix;		 // `CreateStory` の接尾語
		double fStoryElevation;		 // `SetStoryElevation` に渡す値
		double fTemplateOffset;		 // `CreateStoryLevelTemplate` の `elevationOffset`
		bool fElevationAfterLevel;	 // 真なら `SetStoryElevation` をレベルの後に呼ぶ
		double fSetLevelElevation;	 // 0 以外なら `SetStoryLevelElevation` も呼ぶ
		MCObjectHandle fStory = nil; // できたストーリ
		MCObjectHandle fLayer = nil; // できたレベルのレイヤ
	};

	// 変種 1 つぶんのストーリを作る。**戻り値ではなく読み戻しで判定する**
	// （`AddStoryLevel` 系は生えなくても true を返す。Findings「レイヤとストーリ」）。
	void SlhBuildVariant(vwprobe::Report& probe, SlhVariant& v, TXString& levelType)
	{
		probe.log("--- 変種〈" + std::string(v.fTag) + "〉を作る ---");
		TXString storyName(v.fStoryName);
		TXString suffix(v.fSuffix);
		const bool created = gSDK->CreateStory(storyName, suffix);
		v.fStory = gSDK->GetNamedObject(storyName);
		probe.log("  CreateStory(〈" + SlhStr(storyName) + "〉, 〈" + SlhStr(suffix) + "〉)=" +
				  SlhBool(created) + " / GetNamedObject=" + (v.fStory == nil ? "nil" : "取れた"));
		if (v.fStory == nil)
		{
			probe.fail(std::string("変種〈") + v.fTag + "〉のストーリを作れなかった");
			return;
		}

		if (!v.fElevationAfterLevel)
			probe.log("  SetStoryElevation(" + SlhNum(v.fStoryElevation) +
					  ")=" + SlhBool(gSDK->SetStoryElevation(v.fStory, v.fStoryElevation)) +
					  "（レベルを足す前に呼ぶ）");

		// `CreateStoryLevelTemplate` の name / layerLevelType は**非 const の
		// `TXString&`** なので、名前付き lvalue を渡す（Findings「レイヤとストーリ」）。
		TXString templateName(storyName);
		templateName += "-";
		templateName += levelType;
		short index = -1;
		const bool madeTemplate = gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelType,
																 v.fTemplateOffset, 2800, index);
		const bool added = gSDK->AddStoryLevelFromTemplate(v.fStory, index);
		v.fLayer = gSDK->GetLayerForStory(v.fStory, levelType);
		probe.log("  CreateStoryLevelTemplate(elevationOffset=" + SlhNum(v.fTemplateOffset) +
				  ")=" + SlhBool(madeTemplate) + " index=" + std::to_string(index) +
				  " / AddStoryLevelFromTemplate=" + SlhBool(added) + " / GetLayerForStory=" +
				  (v.fLayer == nil ? "nil（生えていない）" : "〈" + SlhName(v.fLayer) + "〉"));

		if (v.fElevationAfterLevel)
			probe.log("  SetStoryElevation(" + SlhNum(v.fStoryElevation) +
					  ")=" + SlhBool(gSDK->SetStoryElevation(v.fStory, v.fStoryElevation)) +
					  "（レベルを足した後に呼ぶ）");

		probe.log("  作った直後の読み戻し:");
		SlhDumpStory(probe, v.fStory, "    ");

		if (v.fSetLevelElevation != 0.0)
		{
			// **`SetStoryLevelElevation` が相対Z を取るのか絶対Z を取るのかを、ここで決める。**
			// 渡した値がそのまま `GetStoryLevelElevation` に出て、解決Z が
			//「階の高さ＋渡した値」なら相対。解決Z が渡した値そのものなら絶対。
			probe.log(
				"  SetStoryLevelElevation(〈" + SlhStr(levelType) + "〉, " +
				SlhNum(v.fSetLevelElevation) + ")=" +
				SlhBool(gSDK->SetStoryLevelElevation(v.fStory, levelType, v.fSetLevelElevation)));
			probe.log("  その後の読み戻し:");
			SlhDumpStory(probe, v.fStory, "    ");
		}
	}

	// レベル基準線を 1 本置いて 3 つ組を書き、**絵に出た数値と名前**まで読む。
	// `viewport` が nil なら「いまのレイヤ」へ、非 nil ならその注釈へ入れる。
	// 置いた個体を返す（段 4 で作り直して読み直すため）。
	MCObjectHandle SlhPlaceMarker(vwprobe::Report& probe, MCObjectHandle viewport,
								  const char* storyName, const TXString& levelType, double y,
								  const std::string& what)
	{
		MCObjectHandle marker = gSDK->CreateCustomObject(kSlhBenchmark2, WorldPt(0, y), 0.0);
		if (marker == nil)
		{
			probe.fail(std::string("CreateCustomObject(") + kSlhBenchmark2 + ") が nil を返した（" +
					   what + "）");
			return nil;
		}
		if (viewport != nil)
			gSDK->AddViewportAnnotationObject(viewport, marker);
		VWFC::VWObjects::VWParametricObj obj(marker);
		// **注釈へ移したら座標を書き直す**（AddViewportAnnotationObject は VW が決めた
		// 位置へ落とす。Findings「レベル（標高）オブジェクト」）。
		obj.SetPointObjectPos(VWPoint2D(0, y));
		// 3 つ組。**3 つ揃って初めて効く**（`Datum` 単独では `GroundPlane` へ倒される）。
		obj.SetParamValue("__StoryName", storyName);
		obj.SetParamValue("__LevelTypeName", levelType);
		obj.SetParamValue("Datum", "StoryLevel");
		gSDK->ResetObject(marker);
		probe.log("  " + what + ": Datum=〈" + SlhStr(obj.GetParamValue("Datum")) +
				  "〉 Elevation 欄=〈" + SlhStr(obj.GetParamValue("Elevation")) + "〉 Elev 欄=〈" +
				  SlhStr(obj.GetParamValue("Elev")) + "〉 __StoryName=〈" +
				  SlhStr(obj.GetParamValue("__StoryName")) + "〉 __LevelTypeName=〈" +
				  SlhStr(obj.GetParamValue("__LevelTypeName")) + "〉");
		probe.log("    絵に出た文字: " + SlhDrawnTexts(marker));
		return marker;
	}

	// 置いてある個体を作り直して、同じ形でもう一度読む（段 4 で「条件を変えたら
	// 解決するか」を見るため）。**作り直さないと欄は更新されない。**
	void SlhResetAndLog(vwprobe::Report& probe, MCObjectHandle marker, const std::string& what)
	{
		if (marker == nil)
			return;
		gSDK->ResetObject(marker);
		VWFC::VWObjects::VWParametricObj obj(marker);
		probe.log("  " + what + ": Datum=〈" + SlhStr(obj.GetParamValue("Datum")) +
				  "〉 Elevation 欄=〈" + SlhStr(obj.GetParamValue("Elevation")) + "〉 Elev 欄=〈" +
				  SlhStr(obj.GetParamValue("Elev")) + "〉");
		probe.log("    絵に出た文字: " + SlhDrawnTexts(marker));
	}

	// 文書にあるビューポートを全部集める（シートレイヤの中を 1 段見れば足りる）。
	// 型 122 ＝ `kViewportNode`（`Objs.TDType.h`）。
	std::vector<MCObjectHandle> SlhCollectViewports()
	{
		std::vector<MCObjectHandle> viewports;
		std::vector<MCObjectHandle> layers;
		gSDK->ForEachLayerN(
			[&layers](MCObjectHandle h)
			{
				if (h != nil)
					layers.push_back(h);
			});
		for (size_t i = 0; i < layers.size(); ++i)
			for (MCObjectHandle h = gSDK->FirstMemberObj(layers[i]); h != nil;
				 h = gSDK->NextObject(h))
				if (gSDK->GetObjectTypeN(h) == kViewportNode)
					viewports.push_back(h);
		return viewports;
	}

	// **比較のために作った個体を消す。** undo イベントは開いていないので
	// `useUndo = false` で消す（Findings「Undo」——半端な記録を残さない）。
	void SlhDeleteAll(std::vector<MCObjectHandle>& made)
	{
		// 後ろから消す（注釈の個体 → ビューポート → シートレイヤ の順になるように積む）。
		for (size_t i = made.size(); i > 0; --i)
			if (made[i - 1] != nil)
				gSDK->DeleteObject(made[i - 1], false);
		made.clear();
	}

	// 文書の全レイヤ（`ForEachLayerN` が返すもの）。
	std::vector<MCObjectHandle> SlhAllLayers()
	{
		std::vector<MCObjectHandle> layers;
		gSDK->ForEachLayerN(
			[&layers](MCObjectHandle h)
			{
				if (h != nil)
					layers.push_back(h);
			});
		return layers;
	}
} // namespace

VW_PROBE("sdk-made-story-level-height", "SDK で作ったストーリのレベルは高さを持つか",
		 "階とレベルの高さを 4 つの口で読み戻し、断面注釈のレベル基準線が数値を出すかまで見る")
{
	probe.log("この調査は 3 段。段 1 は開いている図面を読むだけ（書き込まない）。");
	probe.log("段 2・3 は**ストーリが 1 つも無い図面**（＝新規の空図面）でしか走らない。");
	probe.log("");

	// ---------------------------------------------------------------- 段 1
	probe.log("=== 段 1: いまの文書のストーリとレベルの高さ（読み取りのみ）===");
	probe.log("GetNumStories=" + std::to_string(gSDK->GetNumStories()) +
			  " / GetNumLayerLevelTypes=" + std::to_string(gSDK->GetNumLayerLevelTypes()));
	const std::vector<TXString> existingTypes = SlhLevelTypeNames();
	std::string typeLine = "登録されているレベル種別:";
	for (size_t i = 0; i < existingTypes.size(); ++i)
		typeLine += " 〈" + SlhStr(existingTypes[i]) + "〉";
	probe.log(existingTypes.empty() ? std::string("登録されているレベル種別: (無し)") : typeLine);

	const std::vector<MCObjectHandle> existingStories = SlhCollectStories();
	probe.log("レイヤ経由で見つかったストーリ: " + std::to_string(existingStories.size()) + " 件");
	for (size_t i = 0; i < existingStories.size(); ++i)
		SlhDumpStory(probe, existingStories[i], "  ");

	if (!existingStories.empty())
	{
		probe.log("");
		probe.log("この図面には既にストーリがあるので、段 2・3 は行わない（試験用のストーリを");
		probe.log("生やさないため）。**上の数値が「UI で作ったストーリ」側の実測値**になる。");
		probe.log("**新規の空図面でもう一度走らせる**と、SDK で作った側が同じ形で並ぶ。");
		probe.log("");
		probe.log("**見るところ: 「レベルの絶対Z」と「レイヤの高さ」が一致しているか。**");
		probe.log("1 回目の実測では、この図面のレベル基準線が描いた数値（576 / 3176 / 6776）が");
		probe.log("レベルの絶対Z（612 / 3571 / 6374）と一致しなかった。レイヤの高さのほうが");
		probe.log("描かれた数値と一致するなら、マーカーが読んでいるのはレイヤの高さである。");

		// ------------------------------------------------------------ 段 1B
		// **読むだけでは足りなかった。** 描かれた数値（#135 の実測）と絶対Z が一致しない
		// 件は、**同じ 1 回の実行で両方を測らないと**決まらない（別の日の別の図面と
		// 突き合わせている疑いが残る）。だから**比較用の個体を自分で作って読み、必ず消す**。
		// UI が置いた個体には一切触らない。
		probe.log("");
		probe.log("=== 段 1B: 同じレベルへ 3 つ組で結んだ個体が何を描くか（作って消す）===");
		probe.log("**UI が置いた個体には触らない。** ここで作る個体は、この段の最後に全部消す。");
		probe.log("ビューポートが「更新が必要」になるかもしれない（更新すれば元に戻る）。");
		gSDK->DefineCustomObject(kSlhBenchmark2, kCustomObjectPrefNever);
		std::vector<MCObjectHandle> made;

		// 各ストーリの「レイヤが生えている最初のレベル種別」を 1 つずつ拾う。
		std::vector<MCObjectHandle> probeStories;
		std::vector<TXString> probeTypes;
		for (size_t i = 0; i < existingStories.size(); ++i)
			for (size_t j = 0; j < existingTypes.size(); ++j)
				if (gSDK->GetLayerForStory(existingStories[i], existingTypes[j]) != nil)
				{
					probeStories.push_back(existingStories[i]);
					probeTypes.push_back(existingTypes[j]);
					j = existingTypes.size();
				}

		probe.log("--- 1B-1: そのレベルのデザインレイヤへ置く（注釈の外）---");
		for (size_t i = 0; i < probeStories.size(); ++i)
		{
			MCObjectHandle layer = gSDK->GetLayerForStory(probeStories[i], probeTypes[i]);
			if (layer == nil)
				continue;
			gSDK->SetCurrentLayer(layer);
			const std::string label =
				"〈" + SlhName(probeStories[i]) + " / " + SlhStr(probeTypes[i]) +
				"〉 レベルの絶対Z=" + SlhResolvedLevelZ(layer, probeTypes[i]) +
				" レイヤの高さ=" + SlhLayerZ(layer);
			TXString storyName;
			gSDK->GetObjectName(probeStories[i], storyName);
			made.push_back(SlhPlaceMarker(probe, nil, static_cast<const char*>(storyName),
										  probeTypes[i], 0.0, label));
		}

		// **UI が作ったビューポートの注釈**と**SDK が作った断面ビューポートの注釈**を、
		// 同じ文書の中で並べる。片方だけ数値が出れば、条件はビューポートの出自である。
		const std::vector<MCObjectHandle> existingViewports = SlhCollectViewports();
		probe.log("--- 1B-2: 既にあるビューポート（UI 製）の注釈へ置く ---");
		probe.log("  この図面のビューポート: " + std::to_string(existingViewports.size()) + " 件");
		if (existingViewports.empty())
		{
			probe.log("  ビューポートが 1 件も無いので、この段は行えない");
		}
		else if (!probeStories.empty())
		{
			TXString storyName;
			gSDK->GetObjectName(probeStories[0], storyName);
			MCObjectHandle layer0 = gSDK->GetLayerForStory(probeStories[0], probeTypes[0]);
			for (size_t i = 0; i < existingViewports.size(); ++i)
			{
				const std::string label =
					"ビューポート〈" + SlhName(existingViewports[i]) + "〉[" +
					SlhViewportKind(existingViewports[i]) + "] の注釈（" +
					SlhName(probeStories[0]) + " / " + SlhStr(probeTypes[0]) +
					"。レベルの絶対Z=" + SlhResolvedLevelZ(layer0, probeTypes[0]) + "）";
				made.push_back(SlhPlaceMarker(probe, existingViewports[i],
											  static_cast<const char*>(storyName), probeTypes[0],
											  0.0, label));
			}
		}

		probe.log("--- 1B-3: SDK で作った断面ビューポートの注釈へ置く（同じ文書で）---");
		if (probeStories.empty())
		{
			probe.log("  レイヤの生えているレベルが無いので、この段は行えない");
		}
		else
		{
			MCObjectHandle sheetB = gSDK->CreateLayer("断面（#141 の突き合わせ用）", kLayerSheet);
			MCObjectHandle vpB = nil;
			if (sheetB != nil)
				vpB = gSDK->CreateSectionViewport(WorldPt(-1000, -1500), WorldPt(7000, -1500),
												  WorldPt(0, 3000), 0, -10000, 30000, sheetB);
			if (vpB == nil)
			{
				probe.log("  SDK で断面ビューポートを作れなかった（この段は行えない）");
				if (sheetB != nil)
					made.push_back(sheetB);
			}
			else
			{
				TVariableBlock beyondB;
				beyondB = static_cast<Boolean>(true);
				gSDK->SetObjectVariable(vpB, 1064, beyondB);
				VWFC::VWObjects::VWViewportObj(vpB).SetRenderType(renderFinalHiddenLine);
				gSDK->UpdateViewport(vpB);
				TXString storyName;
				gSDK->GetObjectName(probeStories[0], storyName);
				MCObjectHandle layer0 = gSDK->GetLayerForStory(probeStories[0], probeTypes[0]);
				const std::string zInfo =
					"。レベルの絶対Z=" + SlhResolvedLevelZ(layer0, probeTypes[0]) + "）";
				const std::string who =
					"（" + SlhName(probeStories[0]) + " / " + SlhStr(probeTypes[0]);
				// 3a: 作ったまま（表示レイヤは VW が決めた既定のまま）。
				made.push_back(SlhPlaceMarker(
					probe, vpB, static_cast<const char*>(storyName), probeTypes[0], 0.0,
					"3a 作ったまま [" + SlhViewportKind(vpB) + "] " + who + zInfo));

				// 3b: **全レイヤを表示にしてから**もう 1 本。1 回目の段 4 では書いたまま
				// 読み戻していなかったので、ここは 1 枚ずつ読み戻して効いたかを出す。
				probe.log("  3b の下ごしらえ: 全レイヤを表示にする（書いて読み戻す）");
				const std::vector<MCObjectHandle> allLayers = SlhAllLayers();
				int wroteOk = 0;
				int nowShown = 0;
				for (size_t k = 0; k < allLayers.size(); ++k)
				{
					if (gSDK->SetViewportLayerVisibility(vpB, allLayers[k], 0))
						++wroteOk;
					short after = -99;
					if (gSDK->GetViewportLayerVisibility(vpB, allLayers[k], after) && after == 0)
						++nowShown;
				}
				probe.log("    Set が true を返した数=" + std::to_string(wroteOk) +
						  " / 読み戻しで表示になっていた数=" + std::to_string(nowShown) +
						  " / レイヤ数=" + std::to_string(allLayers.size()));
				gSDK->UpdateViewport(vpB);
				made.push_back(SlhPlaceMarker(
					probe, vpB, static_cast<const char*>(storyName), probeTypes[0], 0.0,
					"3b 全レイヤ表示の後 [" + SlhViewportKind(vpB) + "] " + who + zInfo));
				// 消す順（後ろから消えるので、先に積んだものが後に消える）。
				made.push_back(vpB);
				made.push_back(sheetB);
			}
		}

		probe.log("--- 1B-4: 片付け ---");
		const size_t madeCount = made.size();
		SlhDeleteAll(made);
		probe.log("  作った " + std::to_string(madeCount) + " 個を消した");
		probe.log("");
		probe.log("**読みどころ 1**: 1B-2 で数値が出たビューポートと `0` のものを、行頭の");
		probe.log("[断面VP/平面VP 表示レイヤ=n/m] で見分ける。1 回目は 37 件が 2 つに割れた");
		probe.log("（断面図らしい名前は数値、伏図らしい名前は 0）——**名前ではなくこの 2 つの");
		probe.log("値で割れているのか**がここで分かる。");
		probe.log(
			"**読みどころ 2**: 3a が `0` で 3b が数値なら、**SDK で作った断面ビューポートでも");
		probe.log("「表示レイヤを入れてから置けば」注釈に高さが出る**——それが求めていた手順。");
		probe.log(
			"3b も `0` なら、上の「読み戻しで表示になっていた数」を見る——効いていて 0 なら、");
		probe.log("条件はレイヤの表示ではない。");
		return;
	}

	// ---------------------------------------------------------------- 段 2
	probe.log("");
	probe.log("=== 段 2: SDK で 4 通りの作り方でストーリを作り、同じ口で読み戻す ===");
	TXString levelType(kSlhLevelType);
	probe.log("CreateLayerLevelType(〈" + SlhStr(levelType) +
			  "〉)=" + SlhBool(gSDK->CreateLayerLevelType(levelType)));

	SlhVariant variants[] = {
		// Findings「レイヤとストーリ」の手順そのまま（＝#133 が踏んだ形）。
		{"A: Findings の手順そのまま", "SLH-A", "A", 2800.0, 0.0, false, 0.0},
		// A に `SetStoryLevelElevation` を足す（issue #141 の見当）。
		{"B: A ＋ SetStoryLevelElevation(300)", "SLH-B", "B", 5600.0, 0.0, false, 300.0},
		// `SetStoryElevation` をレベルの後に呼ぶ（順序が効くのかを見る）。
		{"C: SetStoryElevation をレベルの後に", "SLH-C", "C", 8400.0, 0.0, true, 0.0},
		// テンプレートの `elevationOffset` を 0 以外にする（オフセットが効くのかを見る）。
		{"D: elevationOffset=100", "SLH-D", "D", 11200.0, 100.0, false, 0.0},
	};
	const size_t variantCount = sizeof(variants) / sizeof(variants[0]);
	for (size_t i = 0; i < variantCount; ++i)
		SlhBuildVariant(probe, variants[i], levelType);

	probe.log("");
	probe.log("--- 段 2 のまとめ（階の高さ / レベルの高さ / 解決Z）---");
	for (size_t i = 0; i < variantCount; ++i)
	{
		const SlhVariant& v = variants[i];
		if (v.fStory == nil)
		{
			probe.log(std::string("  ") + v.fTag + ": ストーリを作れなかった");
			continue;
		}
		probe.log(std::string("  ") + v.fTag + ": 階=" + SlhNum(gSDK->GetStoryElevation(v.fStory)) +
				  " 相対Z=" + SlhNum(gSDK->GetStoryLevelElevation(v.fStory, levelType)) +
				  " レベルの絶対Z=" + SlhResolvedLevelZ(v.fLayer, levelType) +
				  " レイヤの高さ=" + SlhLayerZ(v.fLayer) + " レイヤ=" +
				  (v.fLayer == nil ? "生えていない" : "〈" + SlhName(v.fLayer) + "〉"));
	}

	// ---------------------------------------------------------------- 段 3
	probe.log("");
	probe.log("=== 段 3: 断面ビューポートの注釈へ 3 つ組のレベル基準線を置く ===");
	gSDK->DefineCustomObject(kSlhBenchmark2, kCustomObjectPrefNever);

	// 断面に写るものを 1 つ（空の断面だと何も見えず、更新の成否も分からない）。
	if (variants[0].fLayer != nil)
	{
		gSDK->SetCurrentLayer(variants[0].fLayer);
		MCObjectHandle wall = gSDK->CreateWall(WorldPt(0, 0), WorldPt(6000, 0), 200);
		if (wall != nil)
		{
			gSDK->SetWallCornerHeights(wall, 12000, 0, 12000, 0);
			gSDK->ResetObject(wall);
			probe.log("壁を 1 枚作った（0,0）→（6000,0）");
		}
	}

	MCObjectHandle sheet = gSDK->CreateLayer("断面（#141 の調査用）", kLayerSheet);
	MCObjectHandle viewport = nil;
	if (sheet != nil)
	{
		viewport = gSDK->CreateSectionViewport(WorldPt(-1000, -1500), WorldPt(7000, -1500),
											   WorldPt(0, 3000), 0, -1000, 13000, sheet);
	}
	if (viewport == nil)
	{
		probe.fail("断面ビューポートを作れなかった（段 3 は行えない）");
		return;
	}
	// 断面線は壁の手前に引いて奥を見る形なので、「切断面より奥を表示」（オブジェクト変数
	// 1064）を立てないと空になる（Findings「ビューポート」）。
	TVariableBlock beyond;
	beyond = static_cast<Boolean>(true); // TVariableBlock に setter は無い（operator= で書く）
	gSDK->SetObjectVariable(viewport, 1064, beyond);
	VWFC::VWObjects::VWViewportObj(viewport).SetRenderType(renderFinalHiddenLine);
	gSDK->UpdateViewport(viewport);
	gSDK->SetCurrentLayer(sheet);
	probe.log("断面ビューポートを作った（シートレイヤ〈" + SlhName(sheet) + "〉の上）");

	probe.log("--- 3-1: 注釈の中（各変種のレベルへ 3 つ組で結ぶ）---");
	std::vector<MCObjectHandle> annotationMarkers(variantCount, nil);
	for (size_t i = 0; i < variantCount; ++i)
	{
		if (variants[i].fStory == nil)
			continue;
		annotationMarkers[i] = SlhPlaceMarker(probe, viewport, variants[i].fStoryName, levelType,
											  gSDK->GetStoryElevation(variants[i].fStory),
											  std::string("注釈〈") + variants[i].fTag + "〉");
	}

	probe.log("--- 3-2: 対照（そのレベルのデザインレイヤへ直に置く）---");
	for (size_t i = 0; i < variantCount; ++i)
	{
		if (variants[i].fStory == nil || variants[i].fLayer == nil)
			continue;
		gSDK->SetCurrentLayer(variants[i].fLayer);
		SlhPlaceMarker(probe, nil, variants[i].fStoryName, levelType, 0.0,
					   std::string("デザインレイヤ〈") + variants[i].fTag + "〉");
	}

	probe.log("--- 3-3: 対照（シートレイヤへ直に置く）---");
	gSDK->SetCurrentLayer(sheet);
	for (size_t i = 0; i < variantCount; ++i)
	{
		if (variants[i].fStory == nil)
			continue;
		SlhPlaceMarker(probe, nil, variants[i].fStoryName, levelType, 0.0,
					   std::string("シートレイヤ〈") + variants[i].fTag + "〉");
	}

	gSDK->UpdateViewport(viewport);

	// ---------------------------------------------------------------- 段 4
	// 1 回目の実測で「ストーリの側は正しく、注釈の中だけが 0」まで絞れた。
	// **残りはビューポートの側**なので、ここで潰す。
	probe.log("");
	probe.log("=== 段 4: 注釈だけが 0 になる理由を、ビューポートの側で探す ===");

	// **ここが決め手になり得る**——UI が作った断面ビューポートの注釈では数値が出るのに、
	// `CreateSectionViewport` で作ったものでは出ない。**そもそも断面ビューポートとして
	// 成立しているのか**を `ovIsSectionViewport`（1054）で読む。平面VP と出るなら、
	// 「UI 製の伏図の注釈でも 0 だった」ことと合わせて筋が通る。
	probe.log("--- 4-0: この SDK 製ビューポートの素性 ---");
	probe.log("  " + SlhViewportKind(viewport) +
			  "（UI 製の断面図では数値が出て、UI 製の"
			  "伏図では 0 だった——ここが 平面VP なら説明がつく）");

	probe.log("--- 4-1: このビューポートが表示しているレイヤ ---");
	const std::vector<MCObjectHandle> layers = SlhAllLayers();
	for (size_t i = 0; i < layers.size(); ++i)
	{
		short visibility = -1;
		const bool got = gSDK->GetViewportLayerVisibility(viewport, layers[i], visibility);
		probe.log("  レイヤ〈" + SlhName(layers[i]) + "〉 読めた=" + SlhBool(got) +
				  " 表示=" + std::to_string(visibility) +
				  " ストーリ=" + (gSDK->GetStoryOfLayer(layers[i]) == nil ? "無し" : "有り"));
	}

	probe.log("--- 4-2: 全レイヤを表示にして更新し、注釈の個体を作り直す ---");
	// **書いたら読み戻す**（1 回目は読み戻さなかったので「表示にできたのか」が分からず、
	// レイヤの表示を潰し切れていなかった。Findings「調査の作法」）。
	for (size_t i = 0; i < layers.size(); ++i)
	{
		const bool wrote = gSDK->SetViewportLayerVisibility(viewport, layers[i], 0); // 0 = 表示
		short after = -99;
		const bool readBack = gSDK->GetViewportLayerVisibility(viewport, layers[i], after);
		probe.log("  レイヤ〈" + SlhName(layers[i]) + "〉 Set=" + SlhBool(wrote) +
				  " 読み戻し=" + SlhBool(readBack) + " 表示=" + std::to_string(after));
	}
	gSDK->UpdateViewport(viewport);
	for (size_t i = 0; i < variantCount; ++i)
		SlhResetAndLog(probe, annotationMarkers[i],
					   std::string("全レイヤ表示＋更新の後〈") + variants[i].fTag + "〉");

	probe.log("--- 4-3: そのうえで注釈へ新しい 1 本を置く ---");
	for (size_t i = 0; i < variantCount; ++i)
	{
		if (variants[i].fStory == nil)
			continue;
		SlhPlaceMarker(probe, viewport, variants[i].fStoryName, levelType,
					   gSDK->GetStoryElevation(variants[i].fStory),
					   std::string("全レイヤ表示の後に新しい 1 本〈") + variants[i].fTag + "〉");
	}

	probe.log("--- 4-4: はじめから全レイヤを表示にして作った 2 つ目の断面ビューポート ---");
	MCObjectHandle sheet2 = gSDK->CreateLayer("断面 2（#141 の調査用）", kLayerSheet);
	MCObjectHandle viewport2 = nil;
	if (sheet2 != nil)
		viewport2 = gSDK->CreateSectionViewport(WorldPt(-1000, -1500), WorldPt(7000, -1500),
												WorldPt(0, 3000), 0, -1000, 13000, sheet2);
	if (viewport2 == nil)
	{
		probe.log("  2 つ目の断面ビューポートを作れなかった（4-4 は行えない）");
	}
	else
	{
		TVariableBlock beyond2;
		beyond2 = static_cast<Boolean>(true);
		gSDK->SetObjectVariable(viewport2, 1064, beyond2);
		VWFC::VWObjects::VWViewportObj(viewport2).SetRenderType(renderFinalHiddenLine);
		for (size_t i = 0; i < layers.size(); ++i)
			gSDK->SetViewportLayerVisibility(viewport2, layers[i], 0);
		gSDK->UpdateViewport(viewport2);
		gSDK->SetCurrentLayer(sheet2);
		probe.log("  2 つ目の素性: " + SlhViewportKind(viewport2));
		for (size_t i = 0; i < variantCount; ++i)
		{
			if (variants[i].fStory == nil)
				continue;
			SlhPlaceMarker(probe, viewport2, variants[i].fStoryName, levelType,
						   gSDK->GetStoryElevation(variants[i].fStory),
						   std::string("2 つ目の注釈〈") + variants[i].fTag + "〉");
		}
		gSDK->UpdateViewport(viewport2);
	}

	// --- 4-5: 作り方を変えた断面ビューポートを何通りか試す -------------------
	// 4-0 で「断面VP としては成立している」と分かったので、残るのは**作り方の引数**か、
	// UI が作るときに一緒に用意する何か。ここでは引数を振って当たりを探す。
	probe.log("--- 4-5: 作り方（depth / 高さの範囲 / 断面線の位置）を振ってみる ---");
	struct SlhVpRecipe
	{
		const char* fTag;
		WorldPt fP1;
		WorldPt fP2;
		WorldPt fP3;
		double fDepth;
		double fStart;
		double fEnd;
	};
	const SlhVpRecipe recipes[] = {
		{"depth=3000（0 以外）", WorldPt(-1000, -1500), WorldPt(7000, -1500), WorldPt(0, 3000),
		 3000, -1000, 13000},
		{"高さの範囲を階に合わせる", WorldPt(-1000, -1500), WorldPt(7000, -1500), WorldPt(0, 3000),
		 0, 0, 12000},
		{"断面線を壁の上に載せる", WorldPt(-1000, 0), WorldPt(7000, 0), WorldPt(0, 3000), 3000,
		 -1000, 13000},
	};
	for (size_t r = 0; r < sizeof(recipes) / sizeof(recipes[0]); ++r)
	{
		MCObjectHandle sheetR = gSDK->CreateLayer(
			TXString("断面 4-5-") + TXString(static_cast<Sint32>(r + 1)), kLayerSheet);
		if (sheetR == nil)
		{
			probe.log(std::string("  ") + recipes[r].fTag + ": シートレイヤを作れなかった");
			continue;
		}
		MCObjectHandle vpR = gSDK->CreateSectionViewport(
			recipes[r].fP1, recipes[r].fP2, recipes[r].fP3, recipes[r].fDepth, recipes[r].fStart,
			recipes[r].fEnd, sheetR);
		if (vpR == nil)
		{
			probe.log(std::string("  ") + recipes[r].fTag + ": 断面ビューポートを作れなかった");
			continue;
		}
		TVariableBlock beyondR;
		beyondR = static_cast<Boolean>(true);
		gSDK->SetObjectVariable(vpR, 1064, beyondR);
		VWFC::VWObjects::VWViewportObj(vpR).SetRenderType(renderFinalHiddenLine);
		for (size_t i = 0; i < layers.size(); ++i)
			gSDK->SetViewportLayerVisibility(vpR, layers[i], 0);
		gSDK->UpdateViewport(vpR);
		gSDK->SetCurrentLayer(sheetR);
		probe.log(std::string("  ") + recipes[r].fTag + " の素性: " + SlhViewportKind(vpR));
		SlhPlaceMarker(probe, vpR, variants[0].fStoryName, levelType,
					   gSDK->GetStoryElevation(variants[0].fStory),
					   std::string("  → 注釈〈") + recipes[r].fTag + "〉");
		gSDK->UpdateViewport(vpR);
	}

	probe.log("");
	probe.log("読み方: 段 2 でストーリレベルが高さを持っているか、段 3 でその値が絵に出たかを");
	probe.log("場所ごとに見る。ここまでで分かっているのは——段 2 は 4 変種とも噛み合い、");
	probe.log("デザインレイヤとシートレイヤは絶対Z を描き、**注釈の中だけが 0**。そして");
	probe.log("4-0 で SDK 製も〈断面VP=true〉（断面としては成立している）、4-2 でレイヤを");
	probe.log("表示にしても（読み戻しで確認）0 のまま。**だから残るのは作り方の引数か、");
	probe.log("UI が一緒に用意している何か**で、そこを 4-5 で振っている。");
	probe.log("4-5 のどれかで数値が出れば、それが探していた手順。全部 0 なら、素性の行");
	probe.log("（断面VP / 設計層断面VP / 奥 / 前 / クロップ / 行列1055 / 行列1056）を");
	probe.log("**実物件の図面の 1B-2（UI 製で数値が出るもの）と見比べて**、違う欄を探す。");
}
