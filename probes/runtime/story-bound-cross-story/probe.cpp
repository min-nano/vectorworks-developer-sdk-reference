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
//	つまり **fBoundStory は fBound == eStoryObjectBound_Story のときしか使われない**と
//	読める。もしそうなら、fBound=LayerElevation のまま fBoundStory=1 を書いても階は
//	跨がず、**自階の同名レベル**へ解決される。自階と上階でレベルの階内相対Zが等しい
//	モデルでは、上端も下端も自階の同じ高さへ落ちて差が 0 になる——issue #56 の
//	「各階レイヤの相対Zが一致したときだけ潰れる」と一致する。
//
//	【初回（build 7fccec3b6009）の失敗と、その直し】
//	P で「作ったはずのストーリをレイヤ経由で見つけられなかった」で止まった。ログには
//	`GetNumStories = 2`（＝ストーリは確かにできている）と出ているのに、レイヤの列挙が
//	"共通" と名前の空のもの 2 つしか拾えていなかった。**列挙の仕方が間違っていた**
//	——`VWDocument::GetDrawingHeaderFristMember()` ＋ `NextObject` ではレイヤ列を辿れて
//	いない。ISDK には `ForEachLayerN(std::function<void(MCObjectHandle)>)` があるので
//	そちらへ替え、**両方を出して見比べられる**ようにした。
//
//	併せて、初回のような「準備で転んで何も測れない」を繰り返さないために:
//	  * レベル種別とストーリレイヤテンプレートの一覧を**作る前と後に**ダンプする
//	    （`CreateStoryLayerTemplate` が 2 回とも index=1 を返していた件の切り分け）。
//	  * 自分で組んだストーリが使えなければ、**図面に既にあるストーリで測る**へ落ちる
//	    （実際の事故モデルで走らせればそのまま再現できる）。
//	  * 高さは決め打ちにせず `GetStoryObjectDataBoundHeight` で**測ってから**使う
//	    （Findings「レイヤ高さを取得する呼び出しが無い」の回避にもなる）。
//

#include "Probe.h"

#include <functional>
#include <string>
#include <vector>

namespace
{
	// ---- 自分で組む 2 階建ての諸元（既存のストーリが使えないときだけ使う）--------
	// 階内相対Zは **両階で同じ** にする（issue #56 のモデルと同じ形）。
	const char* const kLevelFloor = "VwProbeFloor";		// 階内相対Z = 0
	const char* const kLevelBeamTop = "VwProbeBeamTop"; // 階内相対Z = -40
	const WorldCoord kBeamTopRelative = -40;
	const WorldCoord kStory1Elevation = 612;
	const WorldCoord kStory2Elevation = 3571;
	const char* const kStory1Name = "VwProbeStoryLower";
	const char* const kStory2Name = "VwProbeStoryUpper";

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
		const short levelTypeCount = gSDK->GetNumLayerLevelTypes();
		std::string levelTypes;
		for (short i = 0; i < levelTypeCount; ++i)
			levelTypes += (i != 0 ? ", " : "") + Str(gSDK->GetLayerLevelTypeName(i));
		probe.log(std::string(whenLabel) + " レベル種別 " + std::to_string(levelTypeCount) +
				  " 件: " + levelTypes);

		const short templateCount = gSDK->GetNumStoryLayerTemplates();
		probe.log(std::string(whenLabel) + " ストーリレイヤテンプレート " +
				  std::to_string(templateCount) + " 件:");
		for (short i = 0; i < templateCount; ++i)
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
	probe.log("=== P. 測るための 2 階建てを用意する ===");
	DumpRegistries(probe, "P. 作る前:");

	probe.log("P. まず自分で組んでみる（階内相対Zは両階とも 0 / -40）");
	TXString levelFloor(kLevelFloor);
	TXString levelBeamTop(kLevelBeamTop);
	probe.log(std::string("P.   CreateLayerLevelType(\"") + kLevelFloor +
			  "\") = " + (gSDK->CreateLayerLevelType(levelFloor) ? "true" : "false"));
	probe.log(std::string("P.   CreateLayerLevelType(\"") + kLevelBeamTop +
			  "\") = " + (gSDK->CreateLayerLevelType(levelBeamTop) ? "true" : "false"));

	short templateIndexFloor = -1;
	short templateIndexBeamTop = -1;
	TXString templateFloor("VwProbeTemplateFloor");
	TXString templateBeamTop("VwProbeTemplateBeamTop");
	probe.log(
		std::string("P.   CreateStoryLayerTemplate(Floor, 階内相対Z=0) = ") +
		(gSDK->CreateStoryLayerTemplate(templateFloor, 1.0, levelFloor, 0, 2400, templateIndexFloor)
			 ? "true"
			 : "false") +
		" index=" + std::to_string(templateIndexFloor));
	probe.log(std::string("P.   CreateStoryLayerTemplate(BeamTop, 階内相対Z=") +
			  Num(kBeamTopRelative) + ") = " +
			  (gSDK->CreateStoryLayerTemplate(templateBeamTop, 1.0, levelBeamTop, kBeamTopRelative,
											  2400, templateIndexBeamTop)
				   ? "true"
				   : "false") +
			  " index=" + std::to_string(templateIndexBeamTop));

	TXString story1Name(kStory1Name);
	TXString story1Suffix("-L");
	TXString story2Name(kStory2Name);
	TXString story2Suffix("-U");
	probe.log(std::string("P.   CreateStory(lower) = ") +
			  (gSDK->CreateStory(story1Name, story1Suffix) ? "true" : "false"));
	probe.log(std::string("P.   CreateStory(upper) = ") +
			  (gSDK->CreateStory(story2Name, story2Suffix) ? "true" : "false"));

	DumpRegistries(probe, "P. 作った後:");

	std::vector<StoryInfo> stories = CollectStories(probe);

	// 自分で組んだ階に高さを入れる（見つかっていれば）。
	for (size_t i = 0; i < stories.size(); ++i)
	{
		const std::string name = Str(stories[i].name);
		if (name == kStory1Name)
			probe.log(
				std::string("P. SetStoryElevation(lower, ") + Num(kStory1Elevation) + ") = " +
				(gSDK->SetStoryElevation(stories[i].handle, kStory1Elevation) ? "true" : "false"));
		else if (name == kStory2Name)
			probe.log(
				std::string("P. SetStoryElevation(upper, ") + Num(kStory2Elevation) + ") = " +
				(gSDK->SetStoryElevation(stories[i].handle, kStory2Elevation) ? "true" : "false"));
	}
	// 高さを変えたので拾い直す（並べ替えの基準に使う）。
	for (size_t i = 0; i < stories.size(); ++i)
		stories[i].elevation = gSDK->GetStoryElevation(stories[i].handle);

	if (stories.size() < 2)
	{
		probe.fail(
			"P. ストーリが 2 つ見つからない（見つかった数=" + std::to_string(stories.size()) +
			"）。自分で組めなかったので、**2 階以上あるモデル**（事故の起きた図面でよい）"
			"で走らせ直してほしい。上のレイヤ一覧とテンプレート一覧が手掛かり。");
		return;
	}

	// 下から 2 つ隣り合う階を選ぶ。GetStoryAbove で隣接を確かめる。
	StoryInfo lower = stories[0];
	StoryInfo upper;
	for (size_t i = 0; i < stories.size(); ++i)
		if (stories[i].elevation < lower.elevation)
			lower = stories[i];
	{
		MCObjectHandle above = gSDK->GetStoryAbove(lower.handle);
		for (size_t i = 0; i < stories.size(); ++i)
			if (stories[i].handle == above)
				upper = stories[i];
	}
	if (upper.handle == nullptr)
	{
		probe.fail("P. 下階の GetStoryAbove がどの階とも一致しない（隣り合う 2 階が要る）。");
		return;
	}
	probe.log("P. 使う階: 下=\"" + Str(lower.name) + "\"(" + Num(lower.elevation) + ") 上=\"" +
			  Str(upper.name) + "\"(" + Num(upper.elevation) + ")");

	MCObjectHandle container = lower.anyLayer;
	gSDK->SetCurrentLayer(container);

	// 両方の階にあるレベル種別を、自階での解決Zつきで集める。**高さは決め打ちにせず測る。**
	struct LevelInfo
	{
		TXString type;
		WorldCoord selfZ = 0; // 自階（下階）でのこのレベルの絶対Z
	};
	std::vector<LevelInfo> levels;
	{
		const short levelTypeCount = gSDK->GetNumLayerLevelTypes();
		for (short i = 0; i < levelTypeCount; ++i)
		{
			const TXString type = gSDK->GetLayerLevelTypeName(i);
			if (gSDK->GetLayerForStory(lower.handle, type) == nullptr)
				continue;
			if (gSDK->GetLayerForStory(upper.handle, type) == nullptr)
				continue;
			LevelInfo info;
			info.type = type;
			info.selfZ = gSDK->GetStoryObjectDataBoundHeight(
				MakeBound(MockUp::eStoryObjectBound_LayerElevation, 0, type, 0), container);
			levels.push_back(info);
			probe.log("P. 両階にあるレベル種別 \"" + Str(type) +
					  "\" 下階での解決Z=" + Num(info.selfZ));
		}
	}
	if (levels.size() < 2)
	{
		probe.fail("P. 両方の階にあるレベル種別が 2 つ未満（見つかった数=" +
				   std::to_string(levels.size()) + "）。2 種類ないと相対Z一致／不一致を作れない。");
		return;
	}
	// 解決Zが違う 2 つを選ぶ（同じでは対照が作れない）。
	LevelInfo levelA = levels[0];
	LevelInfo levelB;
	for (size_t i = 1; i < levels.size(); ++i)
		if (levels[i].selfZ != levelA.selfZ)
		{
			levelB = levels[i];
			break;
		}
	if (Str(levelB.type).empty())
	{
		probe.fail("P. 自階での解決Zが違うレベル種別の組が作れない（全部同じ高さ）。");
		return;
	}
	probe.log("P. 使うレベル種別: A=\"" + Str(levelA.type) + "\"(" + Num(levelA.selfZ) + ") B=\"" +
			  Str(levelB.type) + "\"(" + Num(levelB.selfZ) + ")");

	// ===========================================================================
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
	// ここでは下端を「レベルA ＋ (B の自階Z − A の自階Z)」にして、自階 B と一致させる。
	const WorldCoord kBottomOffsetToMatchB = levelB.selfZ - levelA.selfZ;
	probe.log("S. 下端は \"" + Str(levelA.type) + "\" + " + Num(kBottomOffsetToMatchB) +
			  " で、自階の \"" + Str(levelB.type) + "\"(" + Num(levelB.selfZ) + ") と一致させる。");

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
		MCObjectHandle path = MakeVerticalPath(probe, testCase.label, levelB.selfZ,
											   levelB.selfZ + (upper.elevation - lower.elevation));
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
	probe.log("R で fBound=LayerElevation の行が fBoundStory によらず同じ解決Zなら、"
			  "fBoundStory は LayerElevation では無視されている＝ヘッダのコメントどおり。");
	probe.log("S1 が潰れ（z1-z0=0）、S2 が潰れず、S3 と S4 が同じ結果なら、issue #56 の"
			  "「各階レイヤの相対Zが一致したときだけ潰れる」の機構が確定する。");
}
