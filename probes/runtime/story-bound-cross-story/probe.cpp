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
//	（＝どちらも FL−40 のような）モデルでは、上端も下端も自階の同じ高さへ落ちて差が 0 に
//	なる——issue #56 の「各階レイヤの相対Zが一致したときだけ潰れる」と一致する。
//
//	これを実機で確かめる。測る順は
//	  P. 2 階建てを新規の空図面に組む（階内相対Zが**両階で等しい**レベルを 2 種類作る）
//	  Q. VW 自身が出す選択肢文字列（OIP のポップアップ）を全部ダンプし、
//	     GetStoryBoundDataFromChoiceString で SStoryObjectData へ戻して中身を見る
//	     ——「上階のレベルを指す」正しい書き方を VW の口から読み取る
//	  R. GetStoryObjectDataBoundHeight で総当たりの解決結果を測る（オブジェクトを
//	     作らずに解決だけ見る。fBound × fBoundStory × レベル種別）
//	  S. 実在の構造材 PIO で、相対Z一致／不一致の 2 通りのパス長を測る（再現と対照）
//

#include "Probe.h"

#include <string>
#include <vector>

namespace
{
	// ---- 図面に組む2階建ての諸元 ------------------------------------------------
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

	std::string Describe(const MockUp::SStoryObjectData& data)
	{
		return std::string("{fBound=") + BoundKindName(data.fBound) +
			   ", fBoundStory=" + std::to_string(static_cast<int>(data.fBoundStory)) +
			   ", fLayerLevelType=\"" + static_cast<const char*>(data.fLayerLevelType) +
			   "\", fOffset=" + Num(data.fOffset) + "}";
	}

	MockUp::SStoryObjectData MakeBound(MockUp::EStoryObjectBound bound, int story,
									   const char* levelType, WorldCoord offset)
	{
		MockUp::SStoryObjectData data;
		data.fBound = bound;
		data.fBoundStory = static_cast<Sint8>(story);
		data.fLayerLevelType = levelType;
		data.fOffset = offset;
		return data;
	}

	void LogPoint(vwprobe::Report& probe, const std::string& label, const WorldPt3& pt)
	{
		probe.log(label + ": (" + Num(pt.x) + ", " + Num(pt.y) + ", " + Num(pt.z) + ")");
	}

	// PIO が内部に持つパスの両端点を読み戻す。
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

	// 2 点の鉛直パス（NURBS）を世界座標で作る。
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
} // namespace

VW_PROBE("story-bound-cross-story", "階を跨ぐストーリバウンドの解決を実測する",
		 "2階建てを組み、上階のレベルを指すバウンドの正しい書き方を VW 自身の選択肢文字列から"
		 "読み取り、fBound × fBoundStory × レベル種別の総当たりで解決結果とパス長を測る")
{
	// ===========================================================================
	probe.log("=== P. 2 階建てを組む（階内相対Zは両階とも 0 / -40） ===");

	TXString levelFloor(kLevelFloor);
	TXString levelBeamTop(kLevelBeamTop);
	probe.log(std::string("P. CreateLayerLevelType(\"") + kLevelFloor +
			  "\") = " + (gSDK->CreateLayerLevelType(levelFloor) ? "true" : "false"));
	probe.log(std::string("P. CreateLayerLevelType(\"") + kLevelBeamTop +
			  "\") = " + (gSDK->CreateLayerLevelType(levelBeamTop) ? "true" : "false"));

	// テンプレートの elevationOffset が「階内の相対Z」。ここを両階で共有させる。
	short templateIndexFloor = -1;
	short templateIndexBeamTop = -1;
	TXString templateFloor("VwProbeTemplateFloor");
	TXString templateBeamTop("VwProbeTemplateBeamTop");
	probe.log(
		std::string("P. CreateStoryLayerTemplate(Floor, 相対Z=0) = ") +
		(gSDK->CreateStoryLayerTemplate(templateFloor, 1.0, levelFloor, 0, 2400, templateIndexFloor)
			 ? "true"
			 : "false") +
		" index=" + std::to_string(templateIndexFloor));
	probe.log(std::string("P. CreateStoryLayerTemplate(BeamTop, 相対Z=") + Num(kBeamTopRelative) +
			  ") = " +
			  (gSDK->CreateStoryLayerTemplate(templateBeamTop, 1.0, levelBeamTop, kBeamTopRelative,
											  2400, templateIndexBeamTop)
				   ? "true"
				   : "false") +
			  " index=" + std::to_string(templateIndexBeamTop));

	TXString story1Name(kStory1Name);
	TXString story1Suffix("-L");
	TXString story2Name(kStory2Name);
	TXString story2Suffix("-U");
	probe.log(std::string("P. CreateStory(lower) = ") +
			  (gSDK->CreateStory(story1Name, story1Suffix) ? "true" : "false"));
	probe.log(std::string("P. CreateStory(upper) = ") +
			  (gSDK->CreateStory(story2Name, story2Suffix) ? "true" : "false"));
	probe.log("P. GetNumStories = " + std::to_string(gSDK->GetNumStories()));

	// ストーリのハンドルを直接取る口（GetStoryAt 相当）は ISDK に無い。レイヤを辿って
	// GetStoryOfLayer で拾い、名前で見分ける。
	MCObjectHandle story1 = nullptr;
	MCObjectHandle story2 = nullptr;
	{
		std::vector<MCObjectHandle> seen;
		MCObjectHandle layer = VWDocument::GetDrawingHeaderFristMember();
		for (; layer != nullptr; layer = gSDK->NextObject(layer))
		{
			TXString layerName;
			gSDK->GetObjectName(layer, layerName);
			MCObjectHandle story = gSDK->GetStoryOfLayer(layer);
			TXString storyName;
			if (story != nullptr)
				gSDK->GetObjectName(story, storyName);
			probe.log(std::string("P. レイヤ \"") + static_cast<const char*>(layerName) +
					  "\" レベル種別=\"" +
					  static_cast<const char*>(gSDK->GetLayerLevelType(layer)) + "\" ストーリ=\"" +
					  static_cast<const char*>(storyName) + "\"" +
					  (story != nullptr ? " ストーリ高さ=" + Num(gSDK->GetStoryElevation(story))
										: std::string()));
			if (story == nullptr)
				continue;
			if (std::string(static_cast<const char*>(storyName)) == kStory1Name)
				story1 = story;
			else if (std::string(static_cast<const char*>(storyName)) == kStory2Name)
				story2 = story;
		}
	}

	if (story1 == nullptr || story2 == nullptr)
	{
		probe.fail("P. 作ったはずのストーリをレイヤ経由で見つけられなかった（上のレイヤ一覧を"
				   "見ること）。以降の測定はできない。");
		return;
	}

	probe.log(std::string("P. SetStoryElevation(lower, ") + Num(kStory1Elevation) +
			  ") = " + (gSDK->SetStoryElevation(story1, kStory1Elevation) ? "true" : "false"));
	probe.log(std::string("P. SetStoryElevation(upper, ") + Num(kStory2Elevation) +
			  ") = " + (gSDK->SetStoryElevation(story2, kStory2Elevation) ? "true" : "false"));
	probe.log("P. 読み戻し: lower=" + Num(gSDK->GetStoryElevation(story1)) +
			  " upper=" + Num(gSDK->GetStoryElevation(story2)));

	MCObjectHandle layerLowerFloor = gSDK->GetLayerForStory(story1, levelFloor);
	MCObjectHandle layerLowerBeamTop = gSDK->GetLayerForStory(story1, levelBeamTop);
	MCObjectHandle layerUpperBeamTop = gSDK->GetLayerForStory(story2, levelBeamTop);
	probe.log(std::string("P. GetLayerForStory: lower/Floor=") +
			  (layerLowerFloor != nullptr ? "有" : "nil") +
			  " lower/BeamTop=" + (layerLowerBeamTop != nullptr ? "有" : "nil") +
			  " upper/BeamTop=" + (layerUpperBeamTop != nullptr ? "有" : "nil"));
	probe.log(std::string("P. GetStoryAbove(lower) は upper か: ") +
			  (gSDK->GetStoryAbove(story1) == story2 ? "はい" : "いいえ"));

	if (layerLowerFloor == nullptr)
	{
		probe.fail("P. 下階の基準レイヤが取れなかった。以降の測定はできない。");
		return;
	}
	gSDK->SetCurrentLayer(layerLowerFloor);

	// ===========================================================================
	probe.log("=== Q. VW 自身が出す選択肢（OIP ポップアップ）を全部ダンプして復号する ===");
	// 「上階のレベルを指す」正しい書き方を、VW の口から読み取るのがねらい。
	for (int isTop = 0; isTop <= 1; ++isTop)
	{
		TXStringArray choices;
		gSDK->GetStoryBoundChoiceStrings(story1, isTop != 0, choices);
		probe.log(std::string("Q. ") + (isTop != 0 ? "上端" : "下端") +
				  "の選択肢 件数=" + std::to_string(static_cast<int>(choices.GetSize())));
		for (size_t i = 0; i < choices.GetSize(); ++i)
		{
			const TXString& choice = choices[i];
			MockUp::SStoryObjectData decoded;
			gSDK->GetStoryBoundDataFromChoiceString(choice, decoded);
			probe.log(std::string("Q.   \"") + static_cast<const char*>(choice) + "\" -> " +
					  Describe(decoded) + " 解決Z=" +
					  Num(gSDK->GetStoryObjectDataBoundHeight(decoded, layerLowerFloor)));
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
		const int stories[] = {0, 1, -1};
		const char* const levels[] = {kLevelFloor, kLevelBeamTop};
		for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k)
		{
			for (size_t s = 0; s < sizeof(stories) / sizeof(stories[0]); ++s)
			{
				for (size_t l = 0; l < sizeof(levels) / sizeof(levels[0]); ++l)
				{
					MockUp::SStoryObjectData data = MakeBound(kinds[k], stories[s], levels[l], 0);
					TXString roundTrip;
					gSDK->GetChoiceStringFromStoryBoundData(data, roundTrip);
					probe.log(std::string("R. ") + Describe(data) + " -> 解決Z=" +
							  Num(gSDK->GetStoryObjectDataBoundHeight(data, layerLowerFloor)) +
							  " 選択肢名=\"" + static_cast<const char*>(roundTrip) + "\"");
				}
			}
		}
	}
	probe.log("R. 参考: 下階=" + Num(kStory1Elevation) + " 上階=" + Num(kStory2Elevation) +
			  "。BeamTop の階内相対Zは " + Num(kBeamTopRelative) +
			  " なので、下階 BeamTop=" + Num(kStory1Elevation + kBeamTopRelative) +
			  " 上階 BeamTop=" + Num(kStory2Elevation + kBeamTopRelative) + " が正しい解決先。");

	// ===========================================================================
	probe.log("=== S. 実在の構造材 PIO で、相対Z一致／不一致のパス長を測る ===");
	// 事故の形をそのまま作る。柱 46 本の下端は `{自階, FL, offset -40}`、上端は
	// `{上階, 横架材天端, offset 0}` で、**自階の横架材天端も FL-40**——だから上端が
	// 階を跨げていなければ、上下端はぴったり同じ絶対Zへ落ちて 0 長になる。
	// ここでも下階 Floor-40 (=572) と下階 BeamTop (=572) が一致するようにしてある。
	struct Case
	{
		const char* label;
		// 下端
		MockUp::EStoryObjectBound bottomBound;
		int bottomStory;
		const char* bottomLevel;
		WorldCoord bottomOffset;
		// 上端
		MockUp::EStoryObjectBound topBound;
		int topStory;
		const char* topLevel;
		WorldCoord topOffset;
		const char* expectation;
	};
	const Case cases[] = {
		// (1) 事故の再現。下端を Floor-40 (=572) にして、自階 BeamTop (=572) と一致させる。
		//     上端が階を跨げていなければ両端 572 でパスが 0 長へ潰れるはず。
		{"S1 事故の再現: 下端=自階Floor-40 / 上端=LayerElevation・上階・BeamTop",
		 MockUp::eStoryObjectBound_LayerElevation, 0, kLevelFloor, kBeamTopRelative,
		 MockUp::eStoryObjectBound_LayerElevation, 1, kLevelBeamTop, 0,
		 "跨げていなければ上端も 572 に解決され、z1-z0 = 0（潰れる）"},
		// (2) 同じ下端で、上端だけヘッダのコメントどおり fBound=Story にする。
		{"S2 ヘッダどおり: 下端=自階Floor-40 / 上端=Story・上階・BeamTop",
		 MockUp::eStoryObjectBound_LayerElevation, 0, kLevelFloor, kBeamTopRelative,
		 MockUp::eStoryObjectBound_Story, 1, kLevelBeamTop, 0,
		 "跨げていれば上端 3531、z1-z0 = 2959（潰れない）"},
		// (3)(4) fBoundStory が無視されているかの直接の対照。上端の指定は
		//        fBoundStory だけが違う。**両者の結果が同じなら無視されている。**
		{"S3 対照(上階指定): 下端=自階Floor / 上端=LayerElevation・上階・BeamTop",
		 MockUp::eStoryObjectBound_LayerElevation, 0, kLevelFloor, 0,
		 MockUp::eStoryObjectBound_LayerElevation, 1, kLevelBeamTop, 0,
		 "S4 と同じ結果なら fBoundStory は無視されている"},
		{"S4 対照(自階指定): 下端=自階Floor / 上端=LayerElevation・自階・BeamTop",
		 MockUp::eStoryObjectBound_LayerElevation, 0, kLevelFloor, 0,
		 MockUp::eStoryObjectBound_LayerElevation, 0, kLevelBeamTop, 0,
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
		// パスの絶対Zは上書きされる）。ここでは正しい高さを入れておく。
		MCObjectHandle path =
			MakeVerticalPath(probe, testCase.label, kStory1Elevation + kBeamTopRelative,
							 kStory2Elevation + kBeamTopRelative);
		if (path == nullptr)
			continue;
		MCObjectHandle pio = gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr);
		if (pio == nullptr)
		{
			probe.log(std::string(testCase.label) + ": CreateCustomObjectPath が nil を返した");
			continue;
		}

		MockUp::SStoryObjectData bottom = MakeBound(testCase.bottomBound, testCase.bottomStory,
													testCase.bottomLevel, testCase.bottomOffset);
		MockUp::SStoryObjectData top =
			MakeBound(testCase.topBound, testCase.topStory, testCase.topLevel, testCase.topOffset);
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
	probe.log("下階 Floor=" + Num(kStory1Elevation) +
			  " 下階 BeamTop=" + Num(kStory1Elevation + kBeamTopRelative) +
			  " 上階 BeamTop=" + Num(kStory2Elevation + kBeamTopRelative) + "。");
	probe.log("R で fBound=LayerElevation の行が fBoundStory によらず同じ解決Zなら、"
			  "fBoundStory は LayerElevation では無視されている＝ヘッダのコメントどおり。");
	probe.log("S1 が潰れ（z1-z0=0）、S2 が潰れず、S3 と S4 が同じ結果なら、issue #56 の"
			  "「各階レイヤの相対Zが一致したときだけ潰れる」の機構が確定する。");
}
