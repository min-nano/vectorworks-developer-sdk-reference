//
//	probes/runtime/story-bound-cross-story/probe.cpp
//
//	[issue #56] 階を跨ぐストーリバウンドの解決を実測する。
//
//	PR #57 は「上下端の**解決済み絶対Z**が一致するとパスが 0 長へ潰れる」を確かめたが、
//	その再現は fBound=LayerElevation / fBoundStory=0（自階）で **両端の fOffset を人手で
//	同じ値に揃えた**もので、事故の条件ではない。実務の構造材はバウンドが**階を跨ぐ**ので
//	上下端の絶対Zは本来一致しない——にもかかわらず潰れている、というのが issue #56 の
//	現象である。
//
//	ISDK.h の SStoryObjectData には次のコメントが付いている:
//
//	    Sint8 fBoundStory;  // used with fBound = eStoryObjectBound_Story
//	                        //  if fBoundStory == 0 then it is this story
//	                        //  if fBoundStory == 1 then it is the story above
//	                        //  if fBoundStory == -1 then it is the story below
//
//	**fBoundStory は fBound == eStoryObjectBound_Story のときしか使われない**と読める。
//
//	【実機 4 回目（build 15721c41da34 / 事故のモデル）で出た決定的な数字】
//	1階(612)/2階(3571) が共有するレベル種別 3 件を、下階のレイヤ "1-横架材天端"(=572) を
//	container にして GetStoryObjectDataBoundHeight で解かせたところ:
//
//	    "耐力壁"     階内相対Z 下階=-40 上階=-40 → 絶対Z 572
//	    "FL"         階内相対Z 下階=  0 上階=  0 → 絶対Z 572   ← 612 になるはず
//	    "横架材天端"  階内相対Z 下階=-40 上階=-40 → 絶対Z 572
//
//	**レベル種別が何であれ 572**——container に渡したレイヤ自身の高さである。つまり
//	`eStoryObjectBound_LayerElevation` は文字どおり「**そのオブジェクトが乗っている
//	レイヤの高さ**」で、**fLayerLevelType も fBoundStory も見ていない**。これなら
//	事故の説明が付く: 上下端とも LayerElevation で書けば、レベル種別や階の指定に
//	関わらず両端が同じレイヤ高さへ落ち、差が 0 になる。
//
//	この回のプローブは「下階での絶対Zが違うレベル種別の組」を要求していたので、
//	**全部同じ値だったせいで測定の本体（R/S）へ進めずに止まった**。今回はそこを直し、
//	**R は必ず走る**ようにしてある（R は container のレイヤ 1 枚だけで測れる）。
//	新しく R0 を足し、**container を下階の全レイヤに振って**解決Zがどう動くかを見る
//	——container のレイヤ高さに追随するなら上の見立てが確定する。
//
//	【そこへ至るまでに転んだ 3 回（同じ轍を踏まないために残す）】
//	1 回目（7fccec3b6009）: レイヤの列挙を VWDocument::GetDrawingHeaderFristMember() +
//	   NextObject でやっていて辿れなかった。**正しいのは ForEachLayerN**。
//	2 回目（e88f678c0d6f / 空図面）: CreateStory は true を返し GetNumStories も増えるが、
//	   **レイヤを 1 枚も作らない**。GetStoryOfLayer 経由でしかストーリのハンドルは
//	   取れないので、自分で組んだ階には手が届かない。併せて
//	   **GetLayerLevelTypeName / GetStoryLayerTemplateInfo の添字は 1 始まり**
//	   （0 は無効）と分かった——0 始まりで回すと毎回 1 件取りこぼす。
//	3 回目（e88f678c0d6f / 事故のモデル）: 階の選び方で外した。「一番低い階とその上」を
//	   取ったので 基礎(0) と 1階(612) になり、この 2 つは**共通のレベル種別を 1 つも
//	   持たない**。**共有するレベル種別がいちばん多い隣接ペアを選ぶ**のが正解。
//
//	【測る順】
//	  P.  隣り合う 2 階と、両階にあるレベル種別を選ぶ（何も図面に足さない）
//	  R0. container を下階の全レイヤに振って LayerElevation の解決Zを見る
//	  Q.  VW 自身が出す選択肢文字列を全部ダンプし、SStoryObjectData へ復号する
//	  R.  fBound × fBoundStory × レベル種別の総当たり
//	  S.  実在の構造材 PIO で ResetObject 後のパス長を測る
//

#include "Probe.h"

#include <functional>
#include <string>
#include <vector>

namespace
{
	std::string Num(double v)
	{
		return std::to_string(v);
	}

	std::string Str(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	const char* BoundKindName(MockUp::EStoryObjectBound bound)
	{
		switch (bound)
		{
		case MockUp::eStoryObjectBound_LayerElevation:
			return "LayerElevation";
		case MockUp::eStoryObjectBound_LayerWallHeight:
			return "LayerWallHeight";
		case MockUp::eStoryObjectBound_Story:
			return "Story";
		}
		return "?";
	}

	MockUp::SStoryObjectData MakeBound(MockUp::EStoryObjectBound bound, int story,
									   const TXString& levelType, WorldCoord offset)
	{
		MockUp::SStoryObjectData data;
		data.fBound = bound;
		data.fBoundStory = static_cast<Sint8>(story);
		data.fLayerLevelType = levelType;
		data.fOffset = offset;
		return data;
	}

	std::string Describe(const MockUp::SStoryObjectData& data)
	{
		return std::string("{fBound=") + BoundKindName(data.fBound) +
			   ", fBoundStory=" + std::to_string(static_cast<int>(data.fBoundStory)) +
			   ", fLayerLevelType=\"" + Str(data.fLayerLevelType) +
			   "\", fOffset=" + Num(data.fOffset) + "}";
	}

	void LogPoint(vwprobe::Report& probe, const std::string& label, const WorldPt3& pt)
	{
		probe.log(label + ": (" + Num(pt.x) + ", " + Num(pt.y) + ", " + Num(pt.z) + ")");
	}

	bool ReadPathEndpoints(vwprobe::Report& probe, MCObjectHandle pio, WorldPt3& outP0,
						   WorldPt3& outP1)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
		{
			probe.log("  GetCustomObjectPath が nil を返した");
			return false;
		}
		Boolean ok0 = gSDK->NurbsGetPt3D(path, 0, 0, outP0);
		Boolean ok1 = gSDK->NurbsGetPt3D(path, 0, 1, outP1);
		if (!ok0 || !ok1)
		{
			probe.log("  NurbsGetPt3D が false を返した");
			return false;
		}
		return true;
	}

	MCObjectHandle MakeVerticalPath(vwprobe::Report& probe, const std::string& label, WorldCoord z0,
									WorldCoord z1)
	{
		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(0, 0, z0), false, 1);
		if (curve == nullptr)
		{
			probe.log(label + ": CreateNurbsCurve が nil を返した");
			return nullptr;
		}
		gSDK->Add3DVertex(curve, WorldPt3(0, 0, z1), true);
		return curve;
	}

	// レベル種別とストーリレイヤテンプレートの一覧。CreateStoryLayerTemplate が
	// 2 回とも index=1 を返した件を切り分けるため、作る前と後の両方で出す。
	void DumpRegistries(vwprobe::Report& probe, const char* whenLabel)
	{
		// **添字は 1 始まり**（実機 build e88f678c0d6f で確定: index 0 は
		// GetStoryLayerTemplateInfo が false を返し、件数 N に対して有効なのは 1..N）。
		// 0 も出して「0 は無効」が毎回ログに残るようにしてある。
		const short levelTypeCount = gSDK->GetNumLayerLevelTypes();
		std::string levelTypes;
		for (short i = 0; i <= levelTypeCount; ++i)
			levelTypes += (i != 0 ? ", " : "") + ("[" + std::to_string(i) + "]\"" +
												  Str(gSDK->GetLayerLevelTypeName(i)) + "\"");
		probe.log(std::string(whenLabel) + " レベル種別 " + std::to_string(levelTypeCount) +
				  " 件（添字 0 も確認用に出す）: " + levelTypes);

		const short templateCount = gSDK->GetNumStoryLayerTemplates();
		probe.log(std::string(whenLabel) + " ストーリレイヤテンプレート " +
				  std::to_string(templateCount) + " 件:");
		for (short i = 0; i <= templateCount; ++i)
		{
			TXString name, levelType;
			double scaleFactor = 0, elevationOffset = 0, defaultWallHeight = 0;
			if (gSDK->GetStoryLayerTemplateInfo(i, name, scaleFactor, levelType, elevationOffset,
												defaultWallHeight))
				probe.log("    [" + std::to_string(i) + "] \"" + Str(name) + "\" レベル種別=\"" +
						  Str(levelType) + "\" 階内相対Z=" + Num(elevationOffset) +
						  " 既定壁高=" + Num(defaultWallHeight));
			else
				probe.log("    [" + std::to_string(i) + "] GetStoryLayerTemplateInfo が false");
		}

		// **こちらが本命。** ISDK には Story *Layer* Template（上）とは別に
		// Story *Level* Template の一群があり、階へレベル（＝レイヤ）を生やすのは
		// こちら側（AddStoryLevel / AddStoryLevelFromTemplate / ResetDefaultStoryLevels）。
		// 実機 2 回目で CreateStory がレイヤを 1 枚も作らなかったのは、Layer 側の
		// テンプレートしか登録していなかったからではないか、というのがここの見立て。
		const short levelTemplateCount = gSDK->GetNumStoryLevelTemplates();
		probe.log(std::string(whenLabel) + " ストーリ**レベル**テンプレート " +
				  std::to_string(levelTemplateCount) + " 件:");
		for (short i = 0; i <= levelTemplateCount; ++i)
		{
			TXString name, levelType;
			double scaleFactor = 0, elevationOffset = 0, defaultWallHeight = 0;
			if (gSDK->GetStoryLevelTemplateInfo(i, name, scaleFactor, levelType, elevationOffset,
												defaultWallHeight))
				probe.log("    [" + std::to_string(i) + "] \"" + Str(name) + "\" レベル種別=\"" +
						  Str(levelType) + "\" 階内相対Z=" + Num(elevationOffset) +
						  " 既定壁高=" + Num(defaultWallHeight));
			else
				probe.log("    [" + std::to_string(i) + "] GetStoryLevelTemplateInfo が false");
		}
	}

	struct StoryInfo
	{
		MCObjectHandle handle = nullptr;
		TXString name;
		WorldCoord elevation = 0;
		MCObjectHandle anyLayer = nullptr; // その階のレイヤ 1 枚（container に使う）
	};

	// **レイヤの正しい列挙は ForEachLayerN。** 併せて、初回に使って外した
	// GetDrawingHeaderFristMember + NextObject の結果も出して見比べる。
	std::vector<StoryInfo> CollectStories(vwprobe::Report& probe)
	{
		std::vector<StoryInfo> stories;

		probe.log("  --- ForEachLayerN で列挙 ---");
		gSDK->ForEachLayerN(
			[&](MCObjectHandle layer)
			{
				TXString layerName;
				gSDK->GetObjectName(layer, layerName);
				const TXString levelType = gSDK->GetLayerLevelType(layer);
				MCObjectHandle story = gSDK->GetStoryOfLayer(layer);
				TXString storyName;
				if (story != nullptr)
					gSDK->GetObjectName(story, storyName);
				probe.log("    レイヤ \"" + Str(layerName) + "\" レベル種別=\"" + Str(levelType) +
						  "\" ストーリ=\"" + Str(storyName) + "\"" +
						  (story != nullptr ? " ストーリ高さ=" + Num(gSDK->GetStoryElevation(story))
											: std::string()));
				if (story == nullptr)
					return;
				for (size_t i = 0; i < stories.size(); ++i)
					if (stories[i].handle == story)
						return;
				StoryInfo info;
				info.handle = story;
				info.name = storyName;
				info.elevation = gSDK->GetStoryElevation(story);
				info.anyLayer = layer;
				stories.push_back(info);
			});

		probe.log("  --- 参考: GetDrawingHeaderFristMember + NextObject で列挙（初回に使って"
				  "外した経路。ここが短ければ「レイヤ列はこれでは辿れない」が確定する） ---");
		int walked = 0;
		for (MCObjectHandle h = VWDocument::GetDrawingHeaderFristMember(); h != nullptr;
			 h = gSDK->NextObject(h))
		{
			TXString objectName;
			gSDK->GetObjectName(h, objectName);
			probe.log("    [" + std::to_string(walked) + "] \"" + Str(objectName) + "\"");
			if (++walked >= 40)
			{
				probe.log("    （40 件で打ち切り）");
				break;
			}
		}
		probe.log("  ForEachLayerN が見つけたストーリ: " + std::to_string(stories.size()) +
				  " 件 / GetNumStories = " + std::to_string(gSDK->GetNumStories()));
		return stories;
	}
} // namespace

VW_PROBE("story-bound-cross-story", "階を跨ぐストーリバウンドの解決を実測する",
		 "2階建てを組み、上階のレベルを指すバウンドの正しい書き方を VW 自身の選択肢文字列から"
		 "読み取り、fBound × fBoundStory × レベル種別の総当たりで解決結果とパス長を測る")
{
	// ===========================================================================
	probe.log("=== P. 測るための「隣り合う 2 階」と「両階にあるレベル種別 2 つ」を選ぶ ===");
	DumpRegistries(probe, "P. 触る前:");

	// **既にある階を優先して使い、図面には何も足さない。** 実機 3 回目（事故のモデル、
	// build e88f678c0d6f）で分かったこと:
	//   * CreateStory は**レイヤの無い階**を作る（GetNumStories は数えるが
	//     ForEachLayerN からは見えない）。足すだけ図面が散らかる。
	//   * 「一番低い階とその上」では駄目だった——基礎(0) と 1階(612) は共通のレベル
	//     種別を 1 つも持たない。欲しいのは 1階(612) と 2階(3571) で、こちらは
	//     横架材天端・耐力壁・FL を共有している。
	// なので**共有するレベル種別がいちばん多い隣接ペア**を選ぶ。
	std::vector<StoryInfo> stories = CollectStories(probe);

	// 図面のレベル種別の一覧（添字は 1 始まり）。
	std::vector<TXString> allLevelTypes;
	{
		const short levelTypeCount = gSDK->GetNumLayerLevelTypes();
		for (short i = 1; i <= levelTypeCount; ++i)
		{
			const TXString type = gSDK->GetLayerLevelTypeName(i);
			if (!Str(type).empty())
				allLevelTypes.push_back(type);
		}
	}

	struct LevelInfo
	{
		TXString type;
		WorldCoord selfZ = 0;		  // 下階でのこのレベルの**絶対**Z
		WorldCoord lowerRelative = 0; // 下階での**階内相対**Z
		WorldCoord upperRelative = 0; // 上階での**階内相対**Z
	};

	// ある隣接ペアが共有するレベル種別を集める。
	auto sharedLevels = [&](const StoryInfo& a, const StoryInfo& b)
	{
		std::vector<LevelInfo> found;
		for (size_t i = 0; i < allLevelTypes.size(); ++i)
		{
			const TXString& type = allLevelTypes[i];
			if (gSDK->GetLayerForStory(a.handle, type) == nullptr)
				continue;
			if (gSDK->GetLayerForStory(b.handle, type) == nullptr)
				continue;
			LevelInfo info;
			info.type = type;
			info.lowerRelative = gSDK->GetStoryLevelElevation(a.handle, type);
			info.upperRelative = gSDK->GetStoryLevelElevation(b.handle, type);
			info.selfZ = gSDK->GetStoryObjectDataBoundHeight(
				MakeBound(MockUp::eStoryObjectBound_LayerElevation, 0, type, 0), a.anyLayer);
			found.push_back(info);
		}
		return found;
	};

	StoryInfo lower;
	StoryInfo upper;
	std::vector<LevelInfo> levels;
	for (size_t i = 0; i < stories.size(); ++i)
	{
		MCObjectHandle above = gSDK->GetStoryAbove(stories[i].handle);
		if (above == nullptr)
			continue;
		for (size_t j = 0; j < stories.size(); ++j)
		{
			if (stories[j].handle != above)
				continue;
			std::vector<LevelInfo> shared = sharedLevels(stories[i], stories[j]);
			probe.log("P. 隣接ペア \"" + Str(stories[i].name) + "\"(" + Num(stories[i].elevation) +
					  ") → \"" + Str(stories[j].name) + "\"(" + Num(stories[j].elevation) +
					  ") が共有するレベル種別: " + std::to_string(shared.size()) + " 件");
			for (size_t k = 0; k < shared.size(); ++k)
				probe.log("P.   \"" + Str(shared[k].type) + "\" 階内相対Z: 下階=" +
						  Num(shared[k].lowerRelative) + " 上階=" + Num(shared[k].upperRelative) +
						  " / 下階での絶対Z=" + Num(shared[k].selfZ));
			if (shared.size() > levels.size())
			{
				levels = shared;
				lower = stories[i];
				upper = stories[j];
			}
		}
	}

	if (lower.handle == nullptr || levels.size() < 2)
	{
		probe.fail(
			"P. 「共有するレベル種別を 2 つ以上持つ隣り合う 2 階」が図面に無い（最良のペアの"
			"共有数=" +
			std::to_string(levels.size()) +
			"）。**2 階以上あって、同じレベル種別が両方の階に居るモデル**（事故の起きた図面が"
			"まさにそれ）で走らせ直してほしい。上のレイヤ一覧が手掛かり。");
		return;
	}

	probe.log("P. 使う階: 下=\"" + Str(lower.name) + "\"(" + Num(lower.elevation) + ") 上=\"" +
			  Str(upper.name) + "\"(" + Num(upper.elevation) + ")");

	MCObjectHandle container = lower.anyLayer;
	gSDK->SetCurrentLayer(container);

	// レベル種別 A / B は**階内相対Z**で選ぶ（絶対Zで選ぶと、4 回目のように
	// 「全部同じ値」で組が作れず測定の手前で止まる）。A は相対Z 0、B は相対Z が
	// A と違うもの。どちらも両階にあることは上で確かめてある。
	LevelInfo levelA = levels[0];
	LevelInfo levelB;
	for (size_t i = 0; i < levels.size(); ++i)
		if (levels[i].lowerRelative == 0)
		{
			levelA = levels[i];
			break;
		}
	for (size_t i = 0; i < levels.size(); ++i)
		if (levels[i].lowerRelative != levelA.lowerRelative)
		{
			levelB = levels[i];
			break;
		}
	const bool haveLevelPair = !Str(levelB.type).empty();
	if (!haveLevelPair)
		probe.log("P. 階内相対Zが違うレベル種別の組が作れなかった。S 系列は A だけで組む。");
	if (!haveLevelPair)
		levelB = levelA;
	probe.log("P. 使うレベル種別: A=\"" + Str(levelA.type) + "\"(階内相対Z " +
			  Num(levelA.lowerRelative) + "/" + Num(levelA.upperRelative) + ", 下階での絶対Z " +
			  Num(levelA.selfZ) + ") B=\"" + Str(levelB.type) + "\"(階内相対Z " +
			  Num(levelB.lowerRelative) + "/" + Num(levelB.upperRelative) + ", 下階での絶対Z " +
			  Num(levelB.selfZ) + ")");
	probe.log(std::string("P. B の階内相対Zは両階で") +
			  (levelB.lowerRelative == levelB.upperRelative
				   ? "**一致している**（事故と同じ条件）"
				   : "一致していない（事故の条件ではない）"));

	// 下階の各レベルのレイヤと、その「あるべき絶対Z」を控えておく。
	// あるべき絶対Z = ストーリ高さ + 階内相対Z。実測と突き合わせるための物差し。
	probe.log("P. 下階の各レベルのあるべき絶対Z（ストーリ高さ " + Num(lower.elevation) +
			  " + 相対Z）:");
	for (size_t i = 0; i < levels.size(); ++i)
		probe.log("P.   \"" + Str(levels[i].type) +
				  "\" 下階=" + Num(lower.elevation + levels[i].lowerRelative) +
				  " 上階=" + Num(upper.elevation + levels[i].upperRelative));

	// =======================================================================
	probe.log("=== R0. container を下階の全レイヤに振って LayerElevation の解決Zを見る ===");
	// **ここが 4 回目の見立ての検算。** レベル種別が何であれ container のレイヤ高さが
	// 返るなら、eStoryObjectBound_LayerElevation は「自分が乗っているレイヤの高さ」で
	// あって fLayerLevelType を見ていない、と言い切れる。
	probe.log("R0. 読み方: 行（container のレイヤ）が変われば値も変わり、列（レベル種別）を"
			  "変えても値が動かないなら、fLayerLevelType は無視されている。");
	for (size_t i = 0; i < levels.size(); ++i)
	{
		MCObjectHandle layer = gSDK->GetLayerForStory(lower.handle, levels[i].type);
		if (layer == nullptr)
			continue;
		TXString layerName;
		gSDK->GetObjectName(layer, layerName);
		std::string row;
		for (size_t j = 0; j < levels.size(); ++j)
			row += (j != 0 ? " | " : "") + Str(levels[j].type) + "=" +
				   Num(gSDK->GetStoryObjectDataBoundHeight(
					   MakeBound(MockUp::eStoryObjectBound_LayerElevation, 0, levels[j].type, 0),
					   layer));
		probe.log("R0. container=\"" + Str(layerName) + "\"（あるべき絶対Z " +
				  Num(lower.elevation + levels[i].lowerRelative) + "）: " + row);

		// 同じ container で fBound=Story に替えるとどうなるか（対照）。
		std::string rowStory;
		for (size_t j = 0; j < levels.size(); ++j)
			rowStory +=
				(j != 0 ? " | " : "") + Str(levels[j].type) + "=" +
				Num(gSDK->GetStoryObjectDataBoundHeight(
					MakeBound(MockUp::eStoryObjectBound_Story, 0, levels[j].type, 0), layer));
		probe.log("R0.   同じ container で fBound=Story（自階）: " + rowStory);
	}

	probe.log("=== Q. VW 自身が出す選択肢（OIP ポップアップ）を全部ダンプして復号する ===");
	// 「上階のレベルを指す」正しい書き方を、VW の口から読み取るのがねらい。
	for (int isTop = 0; isTop <= 1; ++isTop)
	{
		TXStringArray choices;
		gSDK->GetStoryBoundChoiceStrings(lower.handle, isTop != 0, choices);
		probe.log(std::string("Q. ") + (isTop != 0 ? "上端" : "下端") +
				  "の選択肢 件数=" + std::to_string(static_cast<int>(choices.GetSize())));
		for (size_t i = 0; i < choices.GetSize(); ++i)
		{
			const TXString& choice = choices[i];
			MockUp::SStoryObjectData decoded;
			gSDK->GetStoryBoundDataFromChoiceString(choice, decoded);
			probe.log("Q.   \"" + Str(choice) + "\" -> " + Describe(decoded) +
					  " 解決Z=" + Num(gSDK->GetStoryObjectDataBoundHeight(decoded, container)));
		}
	}

	// ===========================================================================
	probe.log("=== R. 総当たりで解決結果だけを測る（オブジェクトを作らない） ===");
	// 期待（ヘッダのコメントからの仮説）:
	//   fBound=LayerElevation では fBoundStory が無視され、どの値でも自階へ解決される。
	//   fBound=Story のときだけ fBoundStory=1 が上階を指す。
	{
		const MockUp::EStoryObjectBound kinds[] = {MockUp::eStoryObjectBound_LayerElevation,
												   MockUp::eStoryObjectBound_LayerWallHeight,
												   MockUp::eStoryObjectBound_Story};
		const int storyOffsets[] = {0, 1, -1};
		const TXString types[] = {levelA.type, levelB.type};
		for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k)
			for (size_t s = 0; s < sizeof(storyOffsets) / sizeof(storyOffsets[0]); ++s)
				for (size_t l = 0; l < sizeof(types) / sizeof(types[0]); ++l)
				{
					MockUp::SStoryObjectData data =
						MakeBound(kinds[k], storyOffsets[s], types[l], 0);
					TXString roundTrip;
					gSDK->GetChoiceStringFromStoryBoundData(data, roundTrip);
					probe.log("R. " + Describe(data) + " -> 解決Z=" +
							  Num(gSDK->GetStoryObjectDataBoundHeight(data, container)) +
							  " 選択肢名=\"" + Str(roundTrip) + "\"");
				}
	}

	// ===========================================================================
	probe.log("=== S. 実在の構造材 PIO で、相対Z一致／不一致のパス長を測る ===");
	// 事故の形をそのまま作る。柱 46 本の下端は `{自階, FL, offset -40}`、上端は
	// `{上階, 横架材天端, offset 0}` で、**自階の横架材天端もちょうど FL-40** だった
	// ——だから上端が階を跨げていなければ両端がぴったり同じ絶対Zへ落ちる。
	//
	// オブジェクトは**下階の B のレイヤ**（事故で言えば 1-横架材天端 と同じ高さの層）に
	// 置く。4 回目で LayerElevation が container のレイヤ高さを返していたので、
	// **どのレイヤに置いたかが効く**という前提で読めるようにしておく。
	if (MCObjectHandle layerB = gSDK->GetLayerForStory(lower.handle, levelB.type))
	{
		container = layerB;
		gSDK->SetCurrentLayer(container);
		TXString layerName;
		gSDK->GetObjectName(container, layerName);
		probe.log("S. オブジェクトを置くレイヤ: \"" + Str(layerName) + "\"（あるべき絶対Z " +
				  Num(lower.elevation + levelB.lowerRelative) + "）");
	}

	// 下端の offset は「A のあるべき絶対Z」と「B のあるべき絶対Z」の差。A 基準で書いて
	// B の高さへ落とす、という事故と同じ書き方になる。
	const WorldCoord kBottomOffsetToMatchB =
		(lower.elevation + levelB.lowerRelative) - (lower.elevation + levelA.lowerRelative);
	probe.log("S. 下端は \"" + Str(levelA.type) + "\" + " + Num(kBottomOffsetToMatchB) +
			  " で、下階の \"" + Str(levelB.type) + "\"(あるべき絶対Z " +
			  Num(lower.elevation + levelB.lowerRelative) + ") と一致させる。");

	struct Case
	{
		const char* label;
		MockUp::EStoryObjectBound topBound;
		int topStory;
		bool bottomMatchesSelfB; // 下端を自階Bと一致させるか
		const char* expectation;
	};
	const Case cases[] = {
		{"S1 事故の再現: 下端=自階A+差 / 上端=LayerElevation・上階・B",
		 MockUp::eStoryObjectBound_LayerElevation, 1, true,
		 "跨げていなければ上端も自階Bに解決され、z1-z0 = 0（潰れる）"},
		{"S2 ヘッダどおり: 下端=自階A+差 / 上端=Story・上階・B", MockUp::eStoryObjectBound_Story, 1,
		 true, "跨げていれば上端は上階Bに解決され、潰れない"},
		{"S3 対照(上階指定): 下端=自階A / 上端=LayerElevation・上階・B",
		 MockUp::eStoryObjectBound_LayerElevation, 1, false,
		 "S4 と同じ結果なら fBoundStory は無視されている"},
		{"S4 対照(自階指定): 下端=自階A / 上端=LayerElevation・自階・B",
		 MockUp::eStoryObjectBound_LayerElevation, 0, false,
		 "S3 と同じ結果なら fBoundStory は無視されている"},
	};

	const MockUp::TObjectBoundID kTopBoundID = 0;
	const MockUp::TObjectBoundID kBottomBoundID = 1;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const Case& testCase = cases[i];
		probe.log(std::string("--- ") + testCase.label + " ---");
		probe.log(std::string("  見込み: ") + testCase.expectation);

		// パスは「潰れていない 2 点」であればよい（PR #57 の 5: ResetObject を呼ぶなら
		// パスの絶対Zはバウンド解決結果で上書きされる）。
		MCObjectHandle path =
			MakeVerticalPath(probe, testCase.label, lower.elevation + levelB.lowerRelative,
							 upper.elevation + levelB.upperRelative);
		if (path == nullptr)
			continue;
		MCObjectHandle pio = gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr);
		if (pio == nullptr)
		{
			probe.log(std::string(testCase.label) + ": CreateCustomObjectPath が nil を返した");
			continue;
		}

		MockUp::SStoryObjectData bottom =
			MakeBound(MockUp::eStoryObjectBound_LayerElevation, 0, levelA.type,
					  testCase.bottomMatchesSelfB ? kBottomOffsetToMatchB : 0);
		MockUp::SStoryObjectData top =
			MakeBound(testCase.topBound, testCase.topStory, levelB.type, 0);
		bool setBottom = gSDK->SetObjectStoryBound(pio, kBottomBoundID, bottom);
		bool setTop = gSDK->SetObjectStoryBound(pio, kTopBoundID, top);
		probe.log(std::string("  SetObjectStoryBound: bottom=") + (setBottom ? "true" : "false") +
				  " top=" + (setTop ? "true" : "false"));
		probe.log("  書いた bottom = " + Describe(bottom));
		probe.log("  書いた top    = " + Describe(top));

		MockUp::SStoryObjectData readBottom;
		MockUp::SStoryObjectData readTop;
		if (gSDK->GetObjectStoryBound(pio, kBottomBoundID, readBottom))
			probe.log("  読み戻し bottom = " + Describe(readBottom));
		if (gSDK->GetObjectStoryBound(pio, kTopBoundID, readTop))
			probe.log("  読み戻し top    = " + Describe(readTop));

		const WorldCoord bottomZ = gSDK->GetObjectBoundElevation(pio, kBottomBoundID);
		const WorldCoord topZ = gSDK->GetObjectBoundElevation(pio, kTopBoundID);
		probe.log("  GetObjectBoundElevation: bottom=" + Num(bottomZ) + " top=" + Num(topZ) +
				  " 差=" + Num(topZ - bottomZ));

		gSDK->ResetObject(pio);

		VWParametricObj obj(pio);
		LogPoint(probe, "  ResetObject 後の挿入点", obj.GetObjectModelPos());
		WorldPt3 p0, p1;
		if (ReadPathEndpoints(probe, pio, p0, p1))
		{
			LogPoint(probe, "  ResetObject 後のパス[0]", p0);
			LogPoint(probe, "  ResetObject 後のパス[1]", p1);
			probe.log("  z1-z0 = " + Num(p1.z - p0.z) + "（0 なら潰れている）");
		}
	}

	probe.log("=== 読み方 ===");
	probe.log("R0: 行（container のレイヤ）で値が動き、列（レベル種別）で動かないなら、"
			  "eStoryObjectBound_LayerElevation は「自分が乗っているレイヤの高さ」であり "
			  "fLayerLevelType を見ていない——これが issue #56 の潰れの正体。");
	probe.log("R: fBound=LayerElevation の行が fBoundStory によらず同じ解決Zなら、"
			  "fBoundStory も見ていない（ヘッダのコメントどおり）。fBound=Story の行だけが "
			  "fBoundStory とレベル種別に反応するはず。");
	probe.log("S1 が潰れ（z1-z0=0）、S2（上端だけ fBound=Story）が潰れなければ、"
			  "**直し方は「階を跨ぐ指定は fBound=eStoryObjectBound_Story で書く」**に決まる。");
	probe.log("S3 と S4 が同じ結果なら、fBoundStory を変えても何も変わらないことの"
			  "直接の証拠になる。");
}
