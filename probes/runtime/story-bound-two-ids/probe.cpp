//
//	probes/runtime/story-bound-two-ids/probe.cpp
//
//	[issue #59] **階だけが違う 2 つの `eStoryObjectBound_Story` バウンドを、1 つの
//	オブジェクトの 2 つのバウンド ID へ書いたとき、両方が保持されて独立に解決されるか。**
//
//	#56（PR #58）は「上下端とも `eStoryObjectBound_LayerElevation` で書くと、レベル種別も
//	階も見てもらえず両端が同じレイヤ高さへ落ちる」を確定させた。これは実測として正しいが、
//	**事故を起こしたプラグインはその経路を通っていない**——あちらは最初から
//	`fBound = eStoryObjectBound_Story` で書いている。#58 の実オブジェクト再現は 4 通りとも
//	下端が `LayerElevation` で、**「上下端とも `Story`」という実務でいちばん普通の
//	組み合わせが 1 行も無い**。そこだけを埋めるのがこのプローブである。
//
//	知りたいことは 3 つ:
//	  1. `{Story, 自階, L, 0}` と `{Story, 上階, L, 0}`（**`fBoundStory` 以外まったく同一**）を
//	     2 つの ID へ書くと、`GetObjectBoundElevation` は **自階Z / 上階Z** と出るか、
//	     それとも **同じ値が 2 つ**（＝片方が消えている・同じ 1 つとして扱われている）か。
//	  2. 構造材 PIO が実際に使うバウンド ID は何番か。`ISDK.h` には
//	     `kPIOGenericStoryLevelBoundID = -3` という別の ID もある。
//	     `GetObjectStoryBoundsCount` ＋ `GetObjectStoryBoundsAt` で**実際に並ぶ ID を数える**。
//	     併せて、`ResetObject` 後のパスの下端Z・上端Zがどの ID の解決Zと一致するかで
//	     **「どちらの ID が下端か」**も決まる。
//	  3. バウンドが独立に解決されているのにパスが 0 長になることがあるか
//	     （＝潰しているのがバウンドではなくパス側か）。
//
//	【測り方の方針】
//	* **図面には何も足さない**（オブジェクトは作るが、階やレイヤは作らない）。階とレベルは
//	  既にあるものを使う。#58 で分かったとおり `CreateStory` はレイヤの無い階しか作れず、
//	  階のハンドルは `GetStoryOfLayer` 経由でしか取れない。
//	* **高さを決め打ちにしない。** 「共有するレベル種別がいちばん多い隣接ペア」を選び、
//	  そこで測る。事故のモデルでも、別のモデルでも同じように読める。
//	* **1 ケース 1 オブジェクト。** 同じオブジェクトを書き換えて回すと、前のケースの
//	  残りが効いているのかどうかが読めなくなる。
//	* 比較の軸は #59 が名指しした 3 つ:
//	  A（本命。`fBoundStory` だけが違う）/ B（`fOffset` も違う）/ C（レベル種別も違う）。
//	  実機の事故では **A の形の柱だけが 46 本すべて潰れ、B・C の形は無事**だった。
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

	// そのオブジェクトが**いま実際に持っているバウンド**を、ID ごとに全部出す。
	// ここが issue #59 の問い 2（構造材 PIO が使う ID は 0/1 か −3 か）の答えになる。
	void DumpBounds(vwprobe::Report& probe, const std::string& prefix, MCObjectHandle pio)
	{
		probe.log(prefix + " HasObjectStoryBounds=" +
				  (gSDK->HasObjectStoryBounds(pio) ? "true" : "false") + " / 件数=" +
				  std::to_string(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(pio))));
		const size_t count = gSDK->GetObjectStoryBoundsCount(pio);
		for (size_t i = 0; i < count; ++i)
		{
			const MockUp::TObjectBoundID id = gSDK->GetObjectStoryBoundsAt(pio, i);
			MockUp::SStoryObjectData data;
			const bool got = gSDK->GetObjectStoryBound(pio, id, data);
			probe.log(prefix + "   [" + std::to_string(i) +
					  "] ID=" + std::to_string(static_cast<long long>(id)) + " " +
					  (got ? Describe(data) : std::string("GetObjectStoryBound が false")) +
					  " 解決Z=" + Num(gSDK->GetObjectBoundElevation(pio, id)));
		}
		// 名指しの ID も個別に当たる（一覧に並ばない ID が読めるかどうかを見る）。
		const MockUp::TObjectBoundID probedIDs[] = {0, 1, MockUp::kPIOGenericStoryLevelBoundID};
		for (size_t i = 0; i < sizeof(probedIDs) / sizeof(probedIDs[0]); ++i)
		{
			const MockUp::TObjectBoundID id = probedIDs[i];
			MockUp::SStoryObjectData data;
			const bool has = gSDK->HasObjectStoryBound(pio, id);
			const bool got = gSDK->GetObjectStoryBound(pio, id, data);
			probe.log(prefix + "   ID=" + std::to_string(static_cast<long long>(id)) +
					  ": Has=" + (has ? "true" : "false") + " Get=" + (got ? "true" : "false") +
					  (got ? " " + Describe(data) : std::string()) +
					  " 解決Z=" + Num(gSDK->GetObjectBoundElevation(pio, id)));
		}
	}

	bool ReadPathEndpoints(vwprobe::Report& probe, MCObjectHandle pio, WorldPt3& outP0,
						   WorldPt3& outP1)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
		{
			probe.log("    GetCustomObjectPath が nil を返した");
			return false;
		}
		Boolean ok0 = gSDK->NurbsGetPt3D(path, 0, 0, outP0);
		Boolean ok1 = gSDK->NurbsGetPt3D(path, 0, 1, outP1);
		if (!ok0 || !ok1)
		{
			probe.log("    NurbsGetPt3D が false を返した");
			return false;
		}
		return true;
	}

	void LogPath(vwprobe::Report& probe, const std::string& prefix, MCObjectHandle pio)
	{
		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(probe, pio, p0, p1))
			return;
		probe.log(prefix + " パス[0]=(" + Num(p0.x) + ", " + Num(p0.y) + ", " + Num(p0.z) +
				  ") パス[1]=(" + Num(p1.x) + ", " + Num(p1.y) + ", " + Num(p1.z) +
				  ") z1-z0=" + Num(p1.z - p0.z));
	}

	MCObjectHandle MakeVerticalPath(vwprobe::Report& probe, WorldCoord z0, WorldCoord z1)
	{
		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(0, 0, z0), false, 1);
		if (curve == nullptr)
		{
			probe.log("  CreateNurbsCurve が nil を返した");
			return nullptr;
		}
		gSDK->Add3DVertex(curve, WorldPt3(0, 0, z1), true);
		return curve;
	}

	struct StoryInfo
	{
		MCObjectHandle handle = nullptr;
		TXString name;
		WorldCoord elevation = 0;
		MCObjectHandle anyLayer = nullptr;
	};

	// **レイヤの列挙は ForEachLayerN**（#58 で確定。GetDrawingHeaderFristMember +
	// NextObject では辿れない）。
	std::vector<StoryInfo> CollectStories(vwprobe::Report& probe)
	{
		std::vector<StoryInfo> stories;
		gSDK->ForEachLayerN(
			[&](MCObjectHandle layer)
			{
				MCObjectHandle story = gSDK->GetStoryOfLayer(layer);
				if (story == nullptr)
					return;
				for (size_t i = 0; i < stories.size(); ++i)
					if (stories[i].handle == story)
						return;
				StoryInfo info;
				info.handle = story;
				gSDK->GetObjectName(story, info.name);
				info.elevation = gSDK->GetStoryElevation(story);
				info.anyLayer = layer;
				stories.push_back(info);
			});
		probe.log("P. ストーリ " + std::to_string(stories.size()) +
				  " 件（GetNumStories = " + std::to_string(gSDK->GetNumStories()) + "）");
		return stories;
	}

	struct LevelInfo
	{
		TXString type;
		WorldCoord lowerRelative = 0;
		WorldCoord upperRelative = 0;
	};
} // namespace

VW_PROBE("story-bound-two-ids", "階だけが違う 2 つの Story バウンドが独立に解決されるかを測る",
		 "1 つの構造材 PIO の 2 つのバウンド ID へ、fBoundStory だけが違う "
		 "eStoryObjectBound_Story を書き、両方が保持されて自階Z / 上階Z へ独立に解決されるかを"
		 "見る。実際に並ぶバウンド ID も数える")
{
	// =======================================================================
	probe.log("=== P. 測る場所を選ぶ（図面には階もレイヤも足さない） ===");
	std::vector<StoryInfo> stories = CollectStories(probe);

	std::vector<TXString> allLevelTypes;
	{
		// **添字は 1 始まり**（0 は空文字。#58 で確定）。
		const short levelTypeCount = gSDK->GetNumLayerLevelTypes();
		for (short i = 1; i <= levelTypeCount; ++i)
		{
			const TXString type = gSDK->GetLayerLevelTypeName(i);
			if (!Str(type).empty())
				allLevelTypes.push_back(type);
		}
		probe.log("P. レベル種別 " + std::to_string(allLevelTypes.size()) + " 件");
	}

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
			found.push_back(info);
		}
		return found;
	};

	// **共有するレベル種別がいちばん多い隣接ペア**を選ぶ（#58 で外した点。一番低い階と
	// その上、では共有 0 件になりうる）。
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
					  ") の共有レベル種別: " + std::to_string(shared.size()) + " 件");
			if (shared.size() > levels.size())
			{
				levels = shared;
				lower = stories[i];
				upper = stories[j];
			}
		}
	}

	if (lower.handle == nullptr || levels.empty())
	{
		probe.fail("P. 「同じレベル種別を両方が持つ隣り合う 2 階」が図面に無い。2 階以上あって"
				   "同名のレベルが両階に居るモデル（事故の起きた図面がまさにそれ）で走らせ直して"
				   "ほしい。");
		return;
	}

	probe.log("P. 使う階: 下=\"" + Str(lower.name) + "\"(" + Num(lower.elevation) + ") 上=\"" +
			  Str(upper.name) + "\"(" + Num(upper.elevation) + ")");
	for (size_t i = 0; i < levels.size(); ++i)
		probe.log("P.   共有レベル種別 \"" + Str(levels[i].type) + "\" 階内相対Z: 下階=" +
				  Num(levels[i].lowerRelative) + " 上階=" + Num(levels[i].upperRelative) +
				  " → あるべき絶対Z: 下階=" + Num(lower.elevation + levels[i].lowerRelative) +
				  " 上階=" + Num(upper.elevation + levels[i].upperRelative));

	// レベル種別 L は本命 A/B で使う 1 つ。L2 は C（レベル種別違い）の対照で、
	// **L と階内相対Zが違う**ものを選ぶ（同じだと C が A と区別できない）。
	const LevelInfo levelL = levels[0];
	LevelInfo levelL2;
	for (size_t i = 1; i < levels.size(); ++i)
		if (levels[i].lowerRelative != levelL.lowerRelative)
		{
			levelL2 = levels[i];
			break;
		}
	const bool haveSecondLevel = !Str(levelL2.type).empty();

	// B（offset 違い）で使うずらし幅。**モデルから採る**（決め打ちの数値を持ち込まない）。
	// 階高の 1/4 だけ下げれば、上階のどのレベルとも一致しない位置になる。
	WorldCoord offsetNudge = -(upper.elevation - lower.elevation) / 4;
	if (offsetNudge == 0)
		offsetNudge = -1;

	probe.log("P. L=\"" + Str(levelL.type) + "\" / L2=" +
			  (haveSecondLevel
				   ? "\"" + Str(levelL2.type) + "\"（階内相対Z " + Num(levelL2.lowerRelative) + "）"
				   : std::string("（無し。C は測れない）")) +
			  " / B の offset=" + Num(offsetNudge));

	// オブジェクトを置くレイヤは**下階の L のレイヤ**。#58 で LayerElevation が
	// container のレイヤ高さを返していたので、どこに置いたかを必ずログへ残す。
	MCObjectHandle container = gSDK->GetLayerForStory(lower.handle, levelL.type);
	if (container == nullptr)
		container = lower.anyLayer;
	{
		TXString layerName;
		gSDK->GetObjectName(container, layerName);
		probe.log("P. オブジェクトを置くレイヤ: \"" + Str(layerName) + "\"");
	}
	gSDK->SetCurrentLayer(container);

	// **オブジェクトを作らずに解ける値**を先に控えておく（物差し）。実オブジェクトの
	// 読みがこれと食い違ったら、食い違いそのものが答えになる。
	const WorldCoord pureSelfZ = gSDK->GetStoryObjectDataBoundHeight(
		MakeBound(MockUp::eStoryObjectBound_Story, 0, levelL.type, 0), container);
	const WorldCoord pureUpperZ = gSDK->GetStoryObjectDataBoundHeight(
		MakeBound(MockUp::eStoryObjectBound_Story, 1, levelL.type, 0), container);
	probe.log("P. GetStoryObjectDataBoundHeight（オブジェクト抜きの解決）: {Story,0,L,0}=" +
			  Num(pureSelfZ) + " / {Story,1,L,0}=" + Num(pureUpperZ));
	probe.log("P. **この 2 つが実オブジェクトでも両方読めれば「独立に解決される」、"
			  "同じ値が 2 つ返れば「片方が消えている」。**");

	// =======================================================================
	probe.log("=== T. 生の構造材 PIO が最初から持っているバウンド ID を数える（問い 2） ===");
	{
		MCObjectHandle path = MakeVerticalPath(probe, lower.elevation + levelL.lowerRelative,
											   upper.elevation + levelL.upperRelative);
		MCObjectHandle pio = path != nullptr
								 ? gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr)
								 : nullptr;
		if (pio == nullptr)
			probe.fail("T. CreateCustomObjectPath(\"StructuralMember\") が nil を返した"
					   "（構造材 PIO がこの図面で作れない）");
		else
		{
			DumpBounds(probe, "T. 作った直後:", pio);
			LogPath(probe, "T. 作った直後:", pio);
			gSDK->ResetObject(pio);
			DumpBounds(probe, "T. ResetObject 後:", pio);
			LogPath(probe, "T. ResetObject 後:", pio);
		}
	}

	// =======================================================================
	probe.log("=== U. 2 つの ID へ Story バウンドを 2 本書く（本命） ===");

	struct Case
	{
		const char* label;
		int idA;					 // 先に書く ID
		int idB;					 // 後に書く ID
		int storyA;					 // ID=idA の fBoundStory
		int storyB;					 // ID=idB の fBoundStory
		bool useSecondLevelForB;	 // ID=idB のレベル種別を L2 にするか
		bool useOffsetForB;			 // ID=idB に offset を付けるか
		const char* whatItSeparates; // 何を切り分けるケースか
	};

	const Case cases[] = {
		{"U-A 本命: ID0={Story,自階,L,0} / ID1={Story,上階,L,0}（fBoundStory だけが違う）", 0, 1, 0,
		 1, false, false,
		 "両方が独立に解決されるなら 自階Z / 上階Z。同じ値が 2 つなら片方が消えている"},
		{"U-B 対照: ID1 の fOffset も違う（実機で無事だった形）", 0, 1, 0, 1, false, true,
		 "A が潰れて B が潰れないなら「レコードが完全同一に近いほど消える」が立つ"},
		{"U-C 対照: ID1 のレベル種別も違う（実機で無事だった形）", 0, 1, 0, 1, true, false,
		 "同上。L2 が無ければこのケースは飛ばす"},
		{"U-D 対照: 2 本とも完全に同一 {Story,自階,L,0}", 0, 1, 0, 0, false, false,
		 "完全同一でも 2 件として並ぶなら、ID ごとに独立して保持されている"},
		{"U-E 書き順を逆にした A（ID1 を先に書く）", 1, 0, 1, 0, false, false,
		 "A で片方が消えるなら、消えるのは「先に書いた方」か「後に書いた方」か"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const Case& testCase = cases[i];
		if (testCase.useSecondLevelForB && !haveSecondLevel)
		{
			probe.log(std::string("--- ") + testCase.label + " → 飛ばす（L2 が無い） ---");
			continue;
		}
		probe.log(std::string("--- ") + testCase.label + " ---");
		probe.log(std::string("  切り分け: ") + testCase.whatItSeparates);

		// パスは「潰れていない 2 点」であればよい（#58 の 5: ResetObject を呼ぶなら
		// パスの絶対Zはバウンド解決結果で上書きされる）。それでも**バウンドを書く前の
		// パス**を必ず読んでおく——潰れるのがバウンドのせいか ResetObject のせいかは、
		// 前後を並べないと分けられない（問い 3）。
		MCObjectHandle path = MakeVerticalPath(probe, lower.elevation + levelL.lowerRelative,
											   upper.elevation + levelL.upperRelative);
		if (path == nullptr)
			continue;
		MCObjectHandle pio = gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr);
		if (pio == nullptr)
		{
			probe.log("  CreateCustomObjectPath が nil を返した");
			continue;
		}
		LogPath(probe, "  バウンドを書く前:", pio);

		const MockUp::SStoryObjectData dataA =
			MakeBound(MockUp::eStoryObjectBound_Story, testCase.storyA, levelL.type, 0);
		const MockUp::SStoryObjectData dataB =
			MakeBound(MockUp::eStoryObjectBound_Story, testCase.storyB,
					  testCase.useSecondLevelForB ? levelL2.type : levelL.type,
					  testCase.useOffsetForB ? offsetNudge : 0);

		probe.log("  ID=" + std::to_string(testCase.idA) + " へ書く: " + Describe(dataA));
		const bool setA = gSDK->SetObjectStoryBound(pio, testCase.idA, dataA);
		probe.log(std::string("  → SetObjectStoryBound=") + (setA ? "true" : "false"));
		DumpBounds(probe, "  1 本目を書いた直後:", pio);

		probe.log("  ID=" + std::to_string(testCase.idB) + " へ書く: " + Describe(dataB));
		const bool setB = gSDK->SetObjectStoryBound(pio, testCase.idB, dataB);
		probe.log(std::string("  → SetObjectStoryBound=") + (setB ? "true" : "false"));
		DumpBounds(probe, "  2 本目を書いた直後:", pio);

		const WorldCoord z0 = gSDK->GetObjectBoundElevation(pio, 0);
		const WorldCoord z1 = gSDK->GetObjectBoundElevation(pio, 1);
		probe.log("  **GetObjectBoundElevation: ID0=" + Num(z0) + " ID1=" + Num(z1) +
				  " 差=" + Num(z1 - z0) + "**");

		gSDK->ResetObject(pio);
		DumpBounds(probe, "  ResetObject 後:", pio);
		LogPath(probe, "  ResetObject 後:", pio);
		{
			VWParametricObj obj(pio);
			const WorldPt3 pos = obj.GetObjectModelPos();
			probe.log("  ResetObject 後の挿入点: (" + Num(pos.x) + ", " + Num(pos.y) + ", " +
					  Num(pos.z) + ")");
			WorldPt3 p0, p1;
			if (ReadPathEndpoints(probe, pio, p0, p1))
			{
				// **どちらの ID が下端か**は、パスの端点がどちらの解決Zと一致するかで決まる
				// （問い 2 の後半）。挿入点ぶんの平行移動が入りうるので、絶対Zと
				// 「挿入点 + パスZ」の両方を並べて出す。
				probe.log("  端点の絶対Z（挿入点 + パスZ）: [0]=" + Num(pos.z + p0.z) + " [1]=" +
						  Num(pos.z + p1.z) + " ／ 解決Z: ID0=" + Num(z0) + " ID1=" + Num(z1));
			}
		}
	}

	// =======================================================================
	probe.log("=== V. kPIOGenericStoryLevelBoundID（−3）に書けるか ===");
	// ISDK.h の「2017 年に足されたストーリレベル対応を使う parametric 用の Story boundID」。
	// 構造材 PIO がこちらを見ているなら、0/1 へ書いたものは効かないことになる。
	{
		MCObjectHandle path = MakeVerticalPath(probe, lower.elevation + levelL.lowerRelative,
											   upper.elevation + levelL.upperRelative);
		MCObjectHandle pio = path != nullptr
								 ? gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr)
								 : nullptr;
		if (pio == nullptr)
			probe.log("V. CreateCustomObjectPath が nil を返した");
		else
		{
			const MockUp::SStoryObjectData data =
				MakeBound(MockUp::eStoryObjectBound_Story, 1, levelL.type, 0);
			probe.log("V. ID=" +
					  std::to_string(static_cast<long long>(MockUp::kPIOGenericStoryLevelBoundID)) +
					  " へ書く: " + Describe(data));
			const bool ok =
				gSDK->SetObjectStoryBound(pio, MockUp::kPIOGenericStoryLevelBoundID, data);
			probe.log(std::string("V. → SetObjectStoryBound=") + (ok ? "true" : "false"));
			DumpBounds(probe, "V. 書いた直後:", pio);
			gSDK->ResetObject(pio);
			DumpBounds(probe, "V. ResetObject 後:", pio);
			LogPath(probe, "V. ResetObject 後:", pio);
		}
	}

	// =======================================================================
	probe.log("=== 読み方 ===");
	probe.log("U-A の「GetObjectBoundElevation: ID0=… ID1=…」がこのプローブの結論そのもの。"
			  "P で出した {Story,0,L,0} / {Story,1,L,0} の 2 つの値が両方読めていれば"
			  "**階だけが違う 2 本の Story バウンドは独立に解決される**（＝バウンドは白で、"
			  "事故の原因はパス側）。同じ値が 2 つ返っていれば**片方が消えている**"
			  "（＝これが #56 の事故の原因）。");
	probe.log("U-B / U-C が A と違う結果なら、消える条件は「レコードがどれだけ似ているか」。"
			  "U-D（完全同一）と U-E（書き順逆）は、消えるのが先／後のどちらかを決める。");
	probe.log("T と各ケースの「件数」「ID=…」の行が問い 2 の答え。並ぶ ID が 0/1 なら"
			  "構造材 PIO のバウンドは 0/1 で扱う。−3 しか並ばないなら 0/1 への書き込みは"
			  "そもそも見られていない。");
	probe.log("バウンドが 2 つとも正しく解決されているのに ResetObject 後のパスが 0 長なら、"
			  "潰しているのはバウンドではなくパスの作り直し側（問い 3）。");
}
