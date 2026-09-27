//
//	probes/runtime/level-marker-annotation-height/probe.cpp
//
//	[issue #135] 断面ビューポートの注釈に UI のツールで置いたレベル基準線
//	（`Elevation Benchmark2`）が、測定に使用する座標軸＝〈Z軸（3Dモード）〉のまま
//	`FL-1 612` のように「ストーリレベル名＋そのレベルの高さ」を出すのはなぜか。
//	SDK から同じ状態を作った個体は高さが `0` のままになる（Findings「レベル（標高）
//	オブジェクト」）。**その差がどこにあるのかを、同じ図面の中で突き合わせる。**
//
//	【この調査は新規の空図面では答えが出ない】走らせる図面は
//	**UI のツールでレベル基準線を断面ビューポートの注釈へ置いたもの**（名前と高さが
//	両方出ているもの）。他のプローブと違って、**UI が置いた個体には書き込まない**
//	（`SetParamValue` も `ResetObject` も呼ばない）。比較のために自分で作る個体
//	（新規 1 本＋UI 製の複製 1 本）は、最後に消す。
//
//	【ログは PR コメントとして公開される】この調査だけは利用者の図面を読むので、
//	レイヤ名・ストーリ名・欄の値がそのまま載る。差し支えのある図面では走らせない
//	（UI でレベル基準線を 1 本置いた小さな図面でも同じことが確かめられる）。
//
//	本命の見当は**ストーリバウンド**（`ISDK::SetObjectStoryBound` の一群）。ツールが
//	個体にバウンドを付けていれば、高さは注釈の Z ではなく**バウンドを解決した絶対 Z**
//	から出るので、注釈の中でも数値が出る——という筋。だから欄の差だけでなく、
//	バウンドの中身（`SStoryObjectData`）と解決結果（`GetObjectBoundElevation`）も見る。
//	（構造材 PIO でのバウンドの作法は Findings「パラメトリックオブジェクト」にある。）
//
//	確かめること:
//	  1) UI が置いた個体の**全欄**（universal 名・ローカライズ名・欄型・値）
//	  2) **ストーリバウンド**の有無・ID・中身・解決Z（UI 製 / SDK 製の両方）
//	  3) SDK から素で作った個体との**差のある欄だけの一覧**
//	  4) `IMarkersPluginSupport::IsElevationBenchmarkConstrained` が UI 製で true か
//	  5) 補助オブジェクトの連鎖（オブジェクト変数 703）に何がぶら下がっているか
//	  6) **1 つずつ足していく梯子**——3 つ組 → ストーリバウンドを写す → 全欄を写す
//	     → 複製。どこで高さが出るかで、必要なものが 1 回の実行で切り分かる
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Extension/IMarkersPluginSupport.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kLmBenchmark2 = "Elevation Benchmark2";
	const char* const kLmBenchmarkLegacy = "Elevation Benchmark";

	// 見つけたレベルオブジェクト 1 件。
	struct LmFoundMarker
	{
		MCObjectHandle fObject = nil;
		MCObjectHandle fViewport = nil; // 注釈の中なら、その親のビューポート
		MCObjectHandle fContainer = nil; // 直接の入れ物（注釈群・グループ・レイヤ）
		std::string fWhere;				 // どこで見つけたか（人が読む文）
		std::string fPioName;
	};

	std::string LmStr(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string LmNum(double value)
	{
		// 図面の単位（mm）で読める桁に丸める。高さの比較しかしないので 3 桁で足りる。
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.3f", value);
		return std::string(buffer);
	}

	std::string LmObjectName(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		TXString name;
		gSDK->GetObjectName(h, name);
		return LmStr(name);
	}

	// 図形の中のテキスト（PIO が吐いた絵の文字）を再帰で全部集める。
	void LmCollectTexts(MCObjectHandle container, std::vector<std::string>& out, int depth = 0)
	{
		if (container == nil || depth > 6)
			return;
		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil; h = gSDK->NextObject(h))
		{
			const short type = gSDK->GetObjectTypeN(h);
			if (type == kTextNode)
				out.push_back(LmStr(gSDK->GetTextChars(h)));
			else
				LmCollectTexts(h, out, depth + 1);
		}
	}

	std::string LmDrawnTexts(MCObjectHandle h)
	{
		std::vector<std::string> texts;
		LmCollectTexts(h, texts);
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

	// レベルオブジェクトを、レイヤ直下・グループの中・ビューポートの注釈の中から探す。
	void LmWalkContainer(MCObjectHandle container, MCObjectHandle viewport,
						 const std::string& where, int depth, std::vector<LmFoundMarker>& out)
	{
		if (container == nil || depth > 8)
			return;
		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil; h = gSDK->NextObject(h))
		{
			const short type = gSDK->GetObjectTypeN(h);
			if (type == kParametricNode)
			{
				VWFC::VWObjects::VWParametricObj obj(h);
				const std::string pio = LmStr(obj.GetParametricName());
				if (pio == kLmBenchmark2 || pio == kLmBenchmarkLegacy)
				{
					LmFoundMarker found;
					found.fObject = h;
					found.fViewport = viewport;
					found.fContainer = container;
					found.fWhere = where;
					found.fPioName = pio;
					out.push_back(found);
				}
			}
			else if (type == kGroupNode)
			{
				LmWalkContainer(h, viewport, where + " > グループ", depth + 1, out);
			}
			else if (type == kViewportNode)
			{
				MCObjectHandle annot = gSDK->GetViewportGroup(h, kViewportGroupAnnotation);
				LmWalkContainer(annot, h,
								where + " > ビューポート〈" + LmObjectName(h) + "〉の注釈",
								depth + 1, out);
			}
		}
	}

	// 欄を 1 行ずつ出す。UI 製と SDK 製で同じ形にして、差を目で拾えるようにする。
	void LmDumpParams(vwprobe::Report& probe, MCObjectHandle h, const std::string& indent)
	{
		VWFC::VWObjects::VWParametricObj obj(h);
		const size_t count = obj.GetParamsCount();
		probe.log(indent + "欄数: " + std::to_string(count));
		for (size_t i = 0; i < count; ++i)
		{
			const TXString name = obj.GetParamName(i);
			probe.log(indent + "  [" + std::to_string(i) + "] " + LmStr(name) + " / " +
					  LmStr(obj.GetParamLocalizedName(i)) +
					  " / 欄型=" + std::to_string(static_cast<int>(obj.GetParamStyle(name))) +
					  " / 値=〈" + LmStr(obj.GetParamValue(name)) + "〉");
		}
	}

	// **本命**: ストーリバウンド（オブジェクトを階／レベルへ結ぶ仕組み）を読む。
	void LmDumpStoryBounds(vwprobe::Report& probe, MCObjectHandle h, const std::string& indent)
	{
		const size_t count = gSDK->GetObjectStoryBoundsCount(h);
		probe.log(indent + "ストーリバウンド: HasObjectStoryBounds=" +
				  (gSDK->HasObjectStoryBounds(h) ? "true" : "false") +
				  " 件数=" + std::to_string(count));
		for (size_t i = 0; i < count; ++i)
		{
			const Sint32 id = gSDK->GetObjectStoryBoundsAt(h, i);
			VectorWorks::SStoryObjectData data;
			const bool got = gSDK->GetObjectStoryBound(h, id, data);
			TXString choice;
			gSDK->GetChoiceStringFromStoryBoundData(data, choice);
			probe.log(indent + "  [" + std::to_string(i) + "] id=" +
					  std::to_string(static_cast<int>(id)) + " 読めた=" + (got ? "true" : "false") +
					  " fBound=" + std::to_string(static_cast<int>(data.fBound)) + " fBoundStory=" +
					  std::to_string(static_cast<int>(data.fBoundStory)) + " fLayerLevelType=〈" +
					  LmStr(data.fLayerLevelType) + "〉 fOffset=" + LmNum(data.fOffset) +
					  " 解決Z=" + LmNum(gSDK->GetObjectBoundElevation(h, id)) + " 選択肢文字列=〈" +
					  LmStr(choice) + "〉");
		}
		// 並んでいなくても、既知の ID を名指しで問い合わせてみる（-3 は PIO 用の汎用 ID）。
		// TObjectBoundID は Sint32 の typedef（`ISDK.h`）。名前空間を跨がずに書くため素の型で持つ。
		const Sint32 knownIDs[] = {-3, 0, 1};
		std::string line = indent + "  名指しの HasObjectStoryBound:";
		for (size_t i = 0; i < sizeof(knownIDs) / sizeof(knownIDs[0]); ++i)
			line += " id=" + std::to_string(static_cast<int>(knownIDs[i])) + "→" +
					(gSDK->HasObjectStoryBound(h, knownIDs[i]) ? "true" : "false");
		probe.log(line);
	}

	// 補助オブジェクトの連鎖（オブジェクト変数 703 = ovFirstAuxObject）を辿る。
	void LmDumpAuxChain(vwprobe::Report& probe, MCObjectHandle h, const std::string& indent)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, 703, v))
		{
			probe.log(indent + "補助オブジェクト: GetObjectVariable(703) が false");
			return;
		}
		MCObjectHandle aux = nil;
		if (!v.GetMCObjectHandle(aux))
		{
			probe.log(indent + "補助オブジェクト: 703 の中身をハンドルとして読めない（型=" +
					  std::to_string(static_cast<int>(v.GetType())) + "）");
			return;
		}
		if (aux == nil)
		{
			probe.log(indent + "補助オブジェクト: 無し");
			return;
		}
		int index = 0;
		for (; aux != nil && index < 20; aux = gSDK->NextObject(aux), ++index)
		{
			size_t size = 0;
			gSDK->GSGetHandleSize(reinterpret_cast<GSHandle>(aux), size);
			std::string line = indent + "  補助[" + std::to_string(index) + "] 型=" +
							   std::to_string(static_cast<int>(gSDK->GetObjectTypeN(aux))) +
							   " 大きさ=" + std::to_string(size);
			// 生バイトの頭（タグ・容れ物 ID が載る辺り）を 16 進で出す。
			const unsigned char* bytes =
				reinterpret_cast<const unsigned char*>(*reinterpret_cast<GSHandle>(aux));
			if (bytes != nullptr && size > 0)
			{
				static const char* const kLmHexDigits = "0123456789abcdef";
				const size_t shown = size < 128 ? size : 128;
				std::string hex;
				for (size_t i = 0; i < shown; ++i)
				{
					hex += kLmHexDigits[(bytes[i] >> 4) & 0x0f];
					hex += kLmHexDigits[bytes[i] & 0x0f];
					hex += ((i % 16) == 15) ? '\n' : ' ';
				}
				line += " 先頭 " + std::to_string(shown) + " バイト:\n" + hex;
			}
			probe.log(line);
		}
		probe.log(indent + "補助オブジェクトの件数: " + std::to_string(index) +
				  (index >= 20 ? "（打ち切り）" : ""));
	}

	std::string LmConstrained(MCObjectHandle h)
	{
		using namespace VectorWorks::Extension;
		IMarkersPluginSupportPtr support(IID_MarkersPluginSupport);
		if (!support)
			return "(IMarkersPluginSupport を取れない)";
		return support->IsElevationBenchmarkConstrained(h) ? "true" : "false";
	}

	// 注目している欄だけを 1 行で（梯子の各段で見比べるため）。
	std::string LmKeyFields(MCObjectHandle h)
	{
		VWFC::VWObjects::VWParametricObj obj(h);
		return "Axis=〈" + LmStr(obj.GetParamValue("Axis")) + "〉 Datum=〈" +
			   LmStr(obj.GetParamValue("Datum")) + "〉 Elevation 欄=〈" +
			   LmStr(obj.GetParamValue("Elevation")) + "〉 RefElev=〈" +
			   LmStr(obj.GetParamValue("RefElev")) + "〉 __StoryName=〈" +
			   LmStr(obj.GetParamValue("__StoryName")) + "〉 __LevelTypeName=〈" +
			   LmStr(obj.GetParamValue("__LevelTypeName")) + "〉";
	}

	// 梯子の 1 段ぶんの観測（何をした直後かを添えて、同じ形で出す）。
	void LmLogStep(vwprobe::Report& probe, MCObjectHandle h, const std::string& what)
	{
		probe.log("  " + what + ": " + LmKeyFields(h));
		probe.log("    文字: " + LmDrawnTexts(h));
		probe.log("    拘束: " + LmConstrained(h) +
				  " / バウンド件数=" + std::to_string(gSDK->GetObjectStoryBoundsCount(h)));
	}

	// レベルオブジェクトが 1 つも無い図面（＝新規の空図面）で走らせたときに、
	// **UI でレベル基準線を 1 本置ける状態まで用意する**。ここまでは SDK で作れるので、
	// 利用者に頼むのは「ツールで 1 本置いて、もう一度走らせる」だけで済む。
	MCObjectHandle LmBuildScaffolding(vwprobe::Report& probe)
	{
		probe.log("--- 下ごしらえ: ストーリ 2 つ・壁・断面ビューポートを作る ---");

		// 1) レベル種別を先に登録する（Findings「レイヤとストーリ」の手順）。
		TXString levelType("FL");
		probe.log("  CreateLayerLevelType(FL)=" +
				  std::string(gSDK->CreateLayerLevelType(levelType) ? "true" : "false"));

		MCObjectHandle firstFloorLayer = nil;
		const char* const storyNames[] = {"1 階", "2 階"};
		const double storyElevations[] = {0.0, 2800.0};
		for (size_t i = 0; i < 2; ++i)
		{
			TXString storyName(storyNames[i]);
			TXString suffix("");
			const bool made = gSDK->CreateStory(storyName, suffix);
			MCObjectHandle story = gSDK->GetNamedObject(storyName);
			if (story == nil)
			{
				probe.log(std::string("  ストーリ〈") + storyNames[i] +
						  "〉を作れなかった（CreateStory=" + (made ? "true" : "false") +
						  " / GetNamedObject=nil）");
				continue;
			}
			// **レベルを足す前に高さを入れる**（後回しにすると次の CreateStory が衝突し得る）。
			gSDK->SetStoryElevation(story, storyElevations[i]);
			TXString templateName = storyName + "-FL";
			short index = -1;
			gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelType, 0, 2800, index);
			gSDK->AddStoryLevelFromTemplate(story, index);
			// **戻り値ではなく読み戻しで判定する**（AddStoryLevel 系は true を返しても生えない）。
			MCObjectHandle layer = gSDK->GetLayerForStory(story, levelType);
			probe.log(std::string("  ストーリ〈") + storyNames[i] +
					  "〉 高さ=" + LmNum(storyElevations[i]) + " レイヤ=" +
					  (layer == nil ? "生えなかった" : "〈" + LmObjectName(layer) + "〉"));
			if (i == 0)
				firstFloorLayer = layer;
		}

		// 2) 断面に写るものを 1 つ（空の断面だと、どこを狙えばよいか分からないため）。
		if (firstFloorLayer != nil)
		{
			gSDK->SetCurrentLayer(firstFloorLayer);
			MCObjectHandle wall = gSDK->CreateWall(WorldPt(0, 0), WorldPt(6000, 0), 200);
			if (wall != nil)
			{
				gSDK->SetWallCornerHeights(wall, 5600, 0, 5600, 0);
				gSDK->ResetObject(wall);
				probe.log("  壁を 1 枚作った（0,0）→（6000,0）高さ 0〜5600");
			}
		}

		// 3) シートレイヤと断面ビューポート。断面線は壁の手前に引いて奥を見る形にするので、
		//    **「切断面より奥を表示」（オブジェクト変数 1064）を true にしないと空になる**
		//    （Findings「ビューポート」）。
		MCObjectHandle sheet = gSDK->CreateLayer("断面（#135 の調査用）", kLayerSheet);
		if (sheet == nil)
		{
			probe.log("  シートレイヤを作れなかった");
			return nil;
		}
		MCObjectHandle viewport = gSDK->CreateSectionViewport(
			WorldPt(-1000, -1500), WorldPt(7000, -1500), WorldPt(0, 3000), 0, -1000, 7000, sheet);
		if (viewport == nil)
		{
			probe.log("  断面ビューポートを作れなかった");
			return nil;
		}
		TVariableBlock beyond;
		beyond = static_cast<Boolean>(true); // TVariableBlock に setter は無い（operator= で書く）
		gSDK->SetObjectVariable(viewport, 1064, beyond);
		VWFC::VWObjects::VWViewportObj(viewport).SetRenderType(renderFinalHiddenLine);
		gSDK->UpdateViewport(viewport);
		gSDK->SetCurrentLayer(sheet);
		probe.log("  断面ビューポートを作った（シートレイヤ〈" + LmObjectName(sheet) + "〉の上）");

		return viewport;
	}

	// SDK だけで作った文書（上の下ごしらえ）で、**3 つ組が注釈の高さを解決するか**を見る。
	// 実機の図面（UI が作った断面ビューポート）では解決したので、ここで解決しなければ
	// 差はビューポートの側にある——という切り分けのための実験。
	void LmRunSdkOnlyExperiment(vwprobe::Report& probe, MCObjectHandle viewport)
	{
		if (viewport == nil)
			return;

		gSDK->DefineCustomObject(kLmBenchmark2, kCustomObjectPrefNever);

		// --- A: SDK が作った断面ビューポートの注釈へ置いて 3 つ組 ---
		probe.log("--- A: SDK 製の断面ビューポートの注釈へ置いて 3 つ組を書く ---");
		MCObjectHandle marker = gSDK->CreateCustomObject(kLmBenchmark2, WorldPt(0, 0), 0.0);
		if (marker == nil)
		{
			probe.fail("CreateCustomObject が nil を返した");
			return;
		}
		gSDK->AddViewportAnnotationObject(viewport, marker);
		VWFC::VWObjects::VWParametricObj(marker).SetPointObjectPos(VWPoint2D(0, 2800));
		VWFC::VWObjects::VWParametricObj(marker).SetParamValue("__StoryName", "2 階");
		VWFC::VWObjects::VWParametricObj(marker).SetParamValue("__LevelTypeName", "FL");
		VWFC::VWObjects::VWParametricObj(marker).SetParamValue("Datum", "StoryLevel");
		gSDK->ResetObject(marker);
		LmLogStep(probe, marker, "注釈（Y=2800）＋ 3 つ組");

		// --- B: このビューポートがストーリのレイヤを表示しているか ---
		probe.log("--- B: ビューポートの表示レイヤ ---");
		std::vector<MCObjectHandle> layers;
		gSDK->ForEachLayerN(
			[&layers](MCObjectHandle h)
			{
				if (h != nil)
					layers.push_back(h);
			});
		for (size_t i = 0; i < layers.size(); ++i)
		{
			short visibility = -1;
			const bool got = gSDK->GetViewportLayerVisibility(viewport, layers[i], visibility);
			probe.log("  レイヤ〈" + LmObjectName(layers[i]) + "〉 読めた=" +
					  (got ? "true" : "false") + " 表示=" + std::to_string(visibility) +
					  " ストーリ=" + (gSDK->GetStoryOfLayer(layers[i]) == nil ? "無し" : "有り"));
		}

		// --- C: 全レイヤを表示にして更新してから、もう一度読む ---
		probe.log("--- C: 全レイヤを表示にして更新してから読み直す ---");
		for (size_t i = 0; i < layers.size(); ++i)
			gSDK->SetViewportLayerVisibility(viewport, layers[i], 0); // 0 = 表示
		gSDK->UpdateViewport(viewport);
		gSDK->ResetObject(marker);
		LmLogStep(probe, marker, "全レイヤ表示＋更新の後");

		// --- D: 参考——同じ 3 つ組を、注釈の外（シートレイヤ・ストーリ従属レイヤ）で ---
		probe.log("--- D: 参考（注釈の外）---");
		MCObjectHandle sheet = gSDK->GetViewportGroupParent(viewport);
		if (sheet == nil)
			sheet = gSDK->GetCurrentLayer();
		MCObjectHandle onSheet = gSDK->CreateCustomObject(kLmBenchmark2, WorldPt(0, 0), 0.0);
		if (onSheet != nil)
		{
			VWFC::VWObjects::VWParametricObj(onSheet).SetParamValue("__StoryName", "2 階");
			VWFC::VWObjects::VWParametricObj(onSheet).SetParamValue("__LevelTypeName", "FL");
			VWFC::VWObjects::VWParametricObj(onSheet).SetParamValue("Datum", "StoryLevel");
			gSDK->ResetObject(onSheet);
			LmLogStep(probe, onSheet, "いまのレイヤへ直に置いて 3 つ組");
		}

		probe.log("");
		probe.log("この後、**UI のツールでも 1 本置いて、もう一度このプローブを走らせる**と、"
				  "UI 製と SDK 製の全欄を突き合わせます"
				  "（断面ビューポートを右クリック →〈注釈を編集〉→ レベル基準線ツール）。");
	}

	void LmDumpMarker(vwprobe::Report& probe, const LmFoundMarker& found, const std::string& tag)
	{
		probe.log("=== " + tag + " ===");
		probe.log("  場所: " + found.fWhere);
		probe.log("  PIO: " + found.fPioName);
		VWFC::VWObjects::VWParametricObj obj(found.fObject);
		probe.log("  内部 ID: " + std::to_string(static_cast<int>(obj.GetInternalID())));
		const VWPoint2D pos = obj.GetPointObjectPos();
		const VWPoint3D pos3 = obj.GetObjectModelPos();
		VWFC::Math::VWTransformMatrix matrix;
		obj.GetObjectToWorldTransform(matrix);
		const VWPoint3D offset = matrix.GetOffset();
		// **注釈の個体が Z を持っているか**を見る（持っていれば「注釈に Z が無い」が崩れる）。
		probe.log("  位置: 2D=(" + LmNum(pos.x) + ", " + LmNum(pos.y) + ") 3D=(" + LmNum(pos3.x) +
				  ", " + LmNum(pos3.y) + ", " + LmNum(pos3.z) + ") 変換行列の原点=(" +
				  LmNum(offset.x) + ", " + LmNum(offset.y) + ", " + LmNum(offset.z) + ")");
		probe.log("  注釈の中か（IsViewportGroupContainedObject）: " +
				  std::string(
					  gSDK->IsViewportGroupContainedObject(found.fObject, kViewportGroupAnnotation)
						  ? "yes"
						  : "no"));
		probe.log("  拘束（IsElevationBenchmarkConstrained）: " + LmConstrained(found.fObject));
		probe.log("  注目欄: " + LmKeyFields(found.fObject));
		probe.log("  描かれた文字: " + LmDrawnTexts(found.fObject));
		MCObjectHandle profile = obj.GetObjectProfileGroup();
		probe.log("  マーカーレイアウト（プロファイルグループ）: " +
				  (profile == nil
					   ? std::string("nil")
					   : "型=" + std::to_string(static_cast<int>(gSDK->GetObjectTypeN(profile))) +
							 " 中の文字=" + LmDrawnTexts(profile)));
		LmDumpStoryBounds(probe, found.fObject, "  ");
		LmDumpAuxChain(probe, found.fObject, "  ");
		LmDumpParams(probe, found.fObject, "  ");
	}
} // namespace

VW_PROBE("level-marker-annotation-height",
		 "UI が置いたレベル基準線と SDK 製の個体を、同じ図面で全欄突き合わせる",
		 "断面ビューポートの注釈で〈Z軸（3Dモード）〉のまま高さが出ている UI 製の個体を "
		 "探して全欄・ストーリバウンド・拘束・補助オブジェクトを出し、SDK から作った個体へ "
		 "1 つずつ足して（3 つ組 → バウンド → 全欄 → 複製）どこで高さが出るかを見る。"
		 "**UI でレベル基準線を置いた図面で走らせる**（UI 製の個体には書き込まない）")
{
	probe.log("この調査は図面を読む（UI が置いた個体には書き込まない）。");
	probe.log("比較のために作る個体（新規 1 本・複製 1 本）は最後に消す。");

	// --- 1: 文書の中のレベルオブジェクトを全部探す ---
	probe.log("--- 1: 文書の中のレベルオブジェクトを探す ---");

	std::vector<MCObjectHandle> layers;
	gSDK->ForEachLayerN(
		[&layers](MCObjectHandle h)
		{
			if (h != nil)
				layers.push_back(h);
		});
	probe.log("レイヤ数: " + std::to_string(layers.size()));

	std::vector<LmFoundMarker> found;
	for (size_t i = 0; i < layers.size(); ++i)
		LmWalkContainer(layers[i], nil, "レイヤ〈" + LmObjectName(layers[i]) + "〉", 0, found);
	probe.log("自前の走査で見つかった件数: " + std::to_string(found.size()));

	// 検索条件（criteria）でも引いてみる——注釈の中まで届くかどうかは、それ自体が知見。
	size_t criteriaCount = 0;
	gSDK->ForEachObjectInCriteria(TXString("(PON='") + kLmBenchmark2 + "')",
								  [&criteriaCount](MCObjectHandle /*h*/) { ++criteriaCount; });
	probe.log("ForEachObjectInCriteria(\"(PON='Elevation Benchmark2')\") の件数: " +
			  std::to_string(criteriaCount));

	if (found.empty())
	{
		// 新規の空図面で走らせたとき。**失敗にはしない**——ここで下ごしらえまで済ませて、
		// 「ツールで 1 本置いて、もう一度走らせる」だけにする。
		probe.log("レベルオブジェクトが 1 つも無い図面だった"
				  "（UI でレベル基準線を置いた図面を開いて走らせても、そのまま突き合わせる）。");
		LmRunSdkOnlyExperiment(probe, LmBuildScaffolding(probe));
		return;
	}

	for (size_t i = 0; i < found.size(); ++i)
		probe.log("  [" + std::to_string(i) + "] " + found[i].fPioName + " @ " + found[i].fWhere +
				  " / 注目欄: " + LmKeyFields(found[i].fObject) +
				  " / 文字: " + LmDrawnTexts(found[i].fObject));

	// --- 2: 見つけた個体を全欄ダンプする（注釈の中のものを優先し、最大 3 件）---
	probe.log("--- 2: 全欄ダンプ ---");
	size_t dumped = 0;
	for (size_t i = 0; i < found.size() && dumped < 3; ++i)
	{
		if (found[i].fViewport == nil)
			continue;
		LmDumpMarker(probe, found[i], "注釈の中の個体 [" + std::to_string(i) + "]");
		++dumped;
	}
	for (size_t i = 0; i < found.size() && dumped < 4; ++i)
	{
		if (found[i].fViewport != nil)
			continue;
		LmDumpMarker(probe, found[i], "注釈の外の個体 [" + std::to_string(i) + "]（比較用）");
		++dumped;
	}

	// --- 3: 同じ注釈へ SDK から作って突き合わせる ---
	const LmFoundMarker* target = nullptr;
	for (size_t i = 0; i < found.size(); ++i)
	{
		if (found[i].fViewport != nil && found[i].fPioName == kLmBenchmark2)
		{
			target = &found[i];
			break;
		}
	}
	if (target == nullptr)
	{
		probe.fail("断面ビューポートの注釈の中に `Elevation Benchmark2` が無かったので、"
				   "突き合わせは行えない（見つかった個体は上のとおり）");
		return;
	}

	probe.log("--- 3: 同じ注釈へ SDK から作って突き合わせる ---");
	probe.log("突き合わせる相手: " + target->fWhere);

	gSDK->DefineCustomObject(kLmBenchmark2, kCustomObjectPrefNever);
	MCObjectHandle mine = gSDK->CreateCustomObject(kLmBenchmark2, WorldPt(0, 0), 0.0);
	if (mine == nil)
	{
		probe.fail("CreateCustomObject(\"Elevation Benchmark2\") が nil を返した");
		return;
	}
	if (!gSDK->AddViewportAnnotationObject(target->fViewport, mine))
		probe.log("  ※ AddViewportAnnotationObject が false を返した（そのまま続ける）");

	VWFC::VWObjects::VWParametricObj from(target->fObject);
	VWFC::VWObjects::VWParametricObj to(mine);
	to.SetPointObjectPos(from.GetPointObjectPos());
	gSDK->ResetObject(mine);
	LmLogStep(probe, mine, "SDK 製（素のまま。位置だけ UI 製に合わせた）");
	LmDumpStoryBounds(probe, mine, "  ");
	LmDumpAuxChain(probe, mine, "  ");

	// 差のある欄だけを並べる——ここが「埋めるべき欄」の候補になる。
	probe.log("  --- UI 製と SDK 製（素のまま）で値が違う欄 ---");
	size_t diffCount = 0;
	for (size_t i = 0, count = from.GetParamsCount(); i < count; ++i)
	{
		const TXString name = from.GetParamName(i);
		const std::string uiValue = LmStr(from.GetParamValue(name));
		const std::string sdkValue = LmStr(to.GetParamValue(name));
		if (uiValue != sdkValue)
		{
			++diffCount;
			probe.log("    " + LmStr(name) + "（" + LmStr(from.GetParamLocalizedName(i)) +
					  "）: UI 製=〈" + uiValue + "〉 SDK 製=〈" + sdkValue + "〉");
		}
	}
	probe.log("  差のある欄: " + std::to_string(diffCount) + " 件");

	// --- 4: 梯子 その 1——#130 で分かっている 3 つ組を書く ---
	probe.log("--- 4: 梯子 1／3 つ組（__StoryName ＋ __LevelTypeName ＋ Datum=StoryLevel）---");
	to.SetParamValue("__StoryName", from.GetParamValue("__StoryName"));
	to.SetParamValue("__LevelTypeName", from.GetParamValue("__LevelTypeName"));
	to.SetParamValue("Datum", "StoryLevel");
	gSDK->ResetObject(mine);
	LmLogStep(probe, mine, "3 つ組の後");

	// --- 5: 梯子 その 2——UI 製のストーリバウンドを写す ---
	probe.log("--- 5: 梯子 2／ストーリバウンドを写す ---");
	const size_t uiBoundCount = gSDK->GetObjectStoryBoundsCount(target->fObject);
	if (uiBoundCount == 0)
	{
		probe.log("  UI 製にストーリバウンドが 1 つも無いので、写すものが無い"
				  "（＝この筋では説明できない）");
	}
	else
	{
		for (size_t i = 0; i < uiBoundCount; ++i)
		{
			const Sint32 id = gSDK->GetObjectStoryBoundsAt(target->fObject, i);
			VectorWorks::SStoryObjectData data;
			if (!gSDK->GetObjectStoryBound(target->fObject, id, data))
			{
				probe.log("  id=" + std::to_string(static_cast<int>(id)) +
						  " の中身が読めなかった（写せない）");
				continue;
			}
			const bool set = gSDK->SetObjectStoryBound(mine, id, data);
			probe.log("  SetObjectStoryBound(id=" + std::to_string(static_cast<int>(id)) +
					  ") = " + (set ? "true" : "false"));
		}
		LmDumpStoryBounds(probe, mine, "  写した直後 ");
		gSDK->ResetObject(mine);
		LmLogStep(probe, mine, "バウンドを写して描き直した後");
		LmDumpStoryBounds(probe, mine, "  描き直した後 ");
	}

	// --- 6: 梯子 その 3——全欄を写す（欄型ごとの口で写す）---
	probe.log("--- 6: 梯子 3／全欄を写す ---");
	for (size_t i = 0, count = from.GetParamsCount(); i < count; ++i)
	{
		const TXString name = from.GetParamName(i);
		switch (from.GetParamStyle(name))
		{
		case kFieldPenStyle:
			to.SetParamPenStyle(name, from.GetParamPenStyle(name));
			break;
		case kFieldPenWeight:
			to.SetParamPenWeight(name, from.GetParamPenWeight(name));
			break;
		case kFieldFill:
			to.SetParamFill(name, from.GetParamFill(name));
			break;
		case kFieldColor:
			to.SetParamColor(name, from.GetParamColor(name));
			break;
		case kFieldClass:
			to.SetParamClass(name, from.GetParamClass(name));
			break;
		case kFieldBuildingMaterial:
			to.SetParamBuildingMaterial(name, from.GetParamBuildingMaterial(name));
			break;
		case kFieldTexture:
			to.SetParamTexture(name, from.GetParamTexture(name));
			break;
		case kFieldSymDef:
			to.SetParamSymDef(name, from.GetParamSymDef(name));
			break;
		default:
			to.SetParamValue(name, from.GetParamValue(name));
			break;
		}
	}
	gSDK->ResetObject(mine);
	LmLogStep(probe, mine, "全欄を写した後");
	probe.log("  写しても残った差:");
	size_t leftCount = 0;
	for (size_t i = 0, count = from.GetParamsCount(); i < count; ++i)
	{
		const TXString name = from.GetParamName(i);
		if (LmStr(from.GetParamValue(name)) != LmStr(to.GetParamValue(name)))
		{
			++leftCount;
			probe.log("    " + LmStr(name) + ": UI 製=〈" + LmStr(from.GetParamValue(name)) +
					  "〉 写した側=〈" + LmStr(to.GetParamValue(name)) + "〉");
		}
	}
	probe.log("  残った差: " + std::to_string(leftCount) + " 件");

	// --- 7: 梯子 その 4——UI 製を複製する（状態が個体に入っているかを見る）---
	probe.log("--- 7: 梯子 4／UI 製を複製する ---");
	MCObjectHandle dup = gSDK->DuplicateObject(target->fObject);
	if (dup == nil)
	{
		probe.log("  DuplicateObject が nil を返した");
	}
	else
	{
		if (!gSDK->AddViewportAnnotationObject(target->fViewport, dup))
			probe.log("  ※ 複製の AddViewportAnnotationObject が false を返した");
		LmLogStep(probe, dup, "複製（描き直す前）");
		LmDumpStoryBounds(probe, dup, "  ");
		LmDumpAuxChain(probe, dup, "  ");
		gSDK->ResetObject(dup);
		LmLogStep(probe, dup, "複製（描き直した後）");
	}

	// --- 8: 片付け（自分で作ったものだけ消す）---
	probe.log("--- 8: 片付け ---");
	gSDK->DeleteObject(mine, false);
	probe.log("  SDK 製の個体を消した");
	if (dup != nil)
	{
		gSDK->DeleteObject(dup, false);
		probe.log("  複製を消した");
	}
	probe.log("おわり（UI が置いた個体には触っていない。ビューポートは"
			  "「更新が必要」の表示になっているかもしれない——更新すれば元に戻る）");
}
