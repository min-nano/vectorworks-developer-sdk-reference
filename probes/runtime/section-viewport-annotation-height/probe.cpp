//
//	probes/runtime/section-viewport-annotation-height/probe.cpp
//
//	[issue #147] **UI が作った断面ビューポートの注釈ではレベル基準線が高さを出すのに、
//	`ISDK::CreateSectionViewport` で作った断面ビューポートの注釈では `0` になる。その差は何か。**
//
//	【1 回目（2026-09-29。実物件の図面）で分かったこと——**犯人は見つかった**】
//	オブジェクト変数 1000〜1150 を総当りで突き合わせ、差を 1 つずつ書き写したところ、
//	**`ovViewportViewMatrix`（1050）だけを書いたビューポートで `Elev` が `540` になった**
//	（他の 11 件はすべて `0` のまま）。値はこうだった:
//
//	  - UI 製 `Viewport-7`: 1050 ＝ `[0 0 1 | 1 0 0 | 0 1 0 | -5820 0 3640]`
//	    **＝その 1055（`ovSheetLayerSectionViewportViewMatrix`）と完全に同じ**
//	  - SDK 製: 1050 ＝ **単位行列**（＝〈上から見た〉向きのまま）。1055 のほうは
//	    断面の向き（`[1 0 0 | 0 0 -1 | 0 1 0 | …]`）がちゃんと入っている
//
//	つまり **`CreateSectionViewport` は断面の向き（1055）を作るが、ビューポートの
//	ビュー行列（1050）を単位行列のまま残す**——縦方向の基準が無いのはこれが理由、という筋。
//	ハンドル型の差は 0 件だったので「断面線オブジェクトとの結び付き」の筋は消えた。
//	作り方の引数（`depth` / 高さの範囲 / 断面線の位置と向き）は 4 通りとも `0` のまま。
//	**`DuplicateObject` で UI 製を複製すると `540` が出る**（表示レイヤを書き換えて
//	更新しても保たれた）——逃げ道としては成立する。
//
//	【この 2 回目で確かめること——**手順として使えるか**】1 回目で書いたのは
//	「UI 製の 1050」という**他人の値**で、しかも**差を全部書き写した段では `0` のまま**
//	だった（そちらは更新を挟んでいる）。だから手順として言い切るには、次の 5 つが要る。
//
//	  1) **自分の 1055 を自分の 1050 へ書けば済むのか。** UI 製を参照しない道があるかは、
//	     プラグインから使えるかどうかを分ける（UI 製の無い文書でも効く手順が要る）。
//	  2) **`UpdateViewport` は 1050 を消すか。** 実用では必ず更新するので、
//	     消えるなら「更新の後に書く」が手順になる。
//	  3) **実用の順番（表示レイヤ・1064・レンダ・更新を済ませてから最後に 1050）**で効くか。
//	  4) **1 回目の段 4（差を全部書き写した）が `0` だったのはなぜか。** 1050 を書いた
//	     ビューポートへ他の変数を 1 つずつ足して、**打ち消したものを突き止める。**
//	  5) **1050 を書いたビューポートは断面として壊れていないか。** 1054 / 1055 / 1056 と
//	     ビュー種別（1007）を読み、**断面のキャッシュ群（5 / 6 / 7 / 15）に図形が
//	     入ったか**を数える——「絵が出ているか」を目視に頼らず見るための代わりになる。
//
//	【走らせる図面】**UI が作った断面ビューポートのある実物件の図面**（新規の空図面では
//	段 4・6 の対照が取れない。段 1〜3 だけは空図面でも走る）。**この調査は図面へ書き込む**
//	——シートレイヤ・断面ビューポート・レベル基準線を作って**最後に全部消す**が、
//	undo は開いていない（Findings「Undo」）ので、**走らせる前に保存**しておくこと。
//	UI が置いた図形は読むだけで、書き換えない。
//
//	【ログは PR コメントとして公開される】ビューポート名・レイヤ名・ストーリ名・
//	レベル種別名がそのまま載る。差し支えのある図面では走らせない。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kSvahBenchmark2 = "Elevation Benchmark2";

	std::string SvahStr(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string SvahNum(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.3f", value);
		return std::string(buffer);
	}

	std::string SvahName(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		TXString name;
		gSDK->GetObjectName(h, name);
		const std::string plain = SvahStr(name);
		return plain.empty() ? "(名前なし)" : plain;
	}

	std::string SvahBool(bool value)
	{
		return value ? "true" : "false";
	}

	// 図形の中のテキスト（PIO が吐いた絵の文字）を再帰で全部集める。レベル基準線では
	// レイアウトのトークン（`#Elev#`）と**解決後の値**の両方が拾えるので、「絵に何が
	// 出たか」を目視に頼らず読める（Findings「レベル（標高）オブジェクト」）。
	void SvahCollectTexts(MCObjectHandle container, std::vector<std::string>& out, int depth = 0)
	{
		if (container == nil || depth > 6)
			return;
		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil; h = gSDK->NextObject(h))
		{
			if (gSDK->GetObjectTypeN(h) == kTextNode)
				out.push_back(SvahStr(gSDK->GetTextChars(h)));
			else
				SvahCollectTexts(h, out, depth + 1);
		}
	}

	std::string SvahDrawnTexts(MCObjectHandle h)
	{
		std::vector<std::string> texts;
		SvahCollectTexts(h, texts);
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

	// オブジェクトを作らずに「そのストーリのそのレベルは絶対Z で何処か」を SDK に解かせる
	// （Findings「パラメトリックオブジェクト」の「階やレベルを指すバウンド」）。
	std::string SvahResolvedLevelZ(MCObjectHandle container, const TXString& levelType)
	{
		if (container == nil)
			return "(入れ物が無くて解けない)";
		VectorWorks::SStoryObjectData data;
		data.fBound = VectorWorks::eStoryObjectBound_Story;
		data.fBoundStory = 0;
		data.fLayerLevelType = levelType;
		data.fOffset = 0;
		return SvahNum(gSDK->GetStoryObjectDataBoundHeight(data, container));
	}

	std::vector<MCObjectHandle> SvahAllLayers()
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

	// 文書にあるビューポートを全部集める（シートレイヤの中を 1 段見れば足りる）。
	// 型 122 ＝ `kViewportNode`（`Objs.TDType.h`）。
	std::vector<MCObjectHandle> SvahCollectViewports()
	{
		std::vector<MCObjectHandle> viewports;
		const std::vector<MCObjectHandle> layers = SvahAllLayers();
		for (size_t i = 0; i < layers.size(); ++i)
			for (MCObjectHandle h = gSDK->FirstMemberObj(layers[i]); h != nil;
				 h = gSDK->NextObject(h))
				if (gSDK->GetObjectTypeN(h) == kViewportNode)
					viewports.push_back(h);
		return viewports;
	}

	// 真偽のオブジェクト変数を 1 つ読む（読めなかったことと false を区別する）。
	bool SvahReadBool(MCObjectHandle h, short index, bool fallback)
	{
		TVariableBlock var;
		if (!gSDK->GetObjectVariable(h, index, var))
			return fallback;
		bool value = fallback;
		if (!var.GetBoolean(value))
			return fallback;
		return value;
	}

	// オブジェクト変数の中身を**型と値の文字列**に畳む。型が分からないときも
	// 「型N」と出して、比較そのものは成立させる。`TVariableBlock` の取り出し口は
	// 型ごとに分かれていて、型が違えば false を返すので、**上から順に当ててゆけば
	// 型番号の一覧が無くても読める**（`ObjectVariables.h`）。
	std::string SvahBlockText(const TVariableBlock& var)
	{
		const std::string head = "型" + std::to_string(static_cast<int>(var.GetType())) + ":";

		bool asBool = false;
		if (var.GetBoolean(asBool))
			return head + SvahBool(asBool);
		Uint8 asUint8 = 0;
		if (var.GetUint8(asUint8))
			return head + std::to_string(static_cast<int>(asUint8));
		Sint8 asSint8 = 0;
		if (var.GetSint8(asSint8))
			return head + std::to_string(static_cast<int>(asSint8));
		Sint16 asSint16 = 0;
		if (var.GetSint16(asSint16))
			return head + std::to_string(static_cast<int>(asSint16));
		Sint32 asSint32 = 0;
		if (var.GetSint32(asSint32))
			return head + std::to_string(static_cast<long>(asSint32));
		Uint32 asUint32 = 0;
		if (var.GetUint32(asUint32))
			return head + std::to_string(static_cast<unsigned long>(asUint32));
		Real64 asReal = 0;
		if (var.GetReal64(asReal))
			return head + SvahNum(asReal);
		WorldPt asPt;
		if (var.GetWorldPt(asPt))
			return head + "(" + SvahNum(asPt.x) + "," + SvahNum(asPt.y) + ")";
		WorldPt3 asPt3;
		if (var.GetWorldPt3(asPt3))
			return head + "(" + SvahNum(asPt3.x) + "," + SvahNum(asPt3.y) + "," + SvahNum(asPt3.z) +
				   ")";
		WorldRect asRect;
		if (var.GetWorldRect(asRect))
			return head + "(l=" + SvahNum(asRect.left) + " t=" + SvahNum(asRect.top) +
				   " r=" + SvahNum(asRect.right) + " b=" + SvahNum(asRect.bottom) + ")";
		TransformMatrix asMatrix;
		if (var.GetTransformMatrix(asMatrix))
		{
			// **候補のひとつ**——1055 / 1056 は断面の見え方（視線の向きと原点）を持って
			// いる。12 個の数をそのまま出して、UI 製と SDK 製で見比べる。
			std::string body = head + "[";
			for (int row = 0; row < 4; ++row)
				for (int col = 0; col < 3; ++col)
				{
					if (row != 0 || col != 0)
						body += " ";
					body += SvahNum(asMatrix.mat[row][col]);
				}
			return body + "]";
		}
		TXString asText;
		if (var.GetTXString(asText))
			return head + "〈" + SvahStr(asText) + "〉";
		MCObjectHandle asHandle = nil;
		if (var.GetMCObjectHandle(asHandle))
		{
			// **UI 製だけが何かを指しているなら、それが探していた差。** 指している先の
			// 種類（型番号）と名前まで出す（`kParametricNode`=86 なら PIO＝断面線の見込み）。
			if (asHandle == nil)
				return head + "ハンドル nil";
			return head + "ハンドル 種類=" +
				   std::to_string(static_cast<int>(gSDK->GetObjectTypeN(asHandle))) + " 名前=〈" +
				   SvahName(asHandle) + "〉";
		}
		return head + "(この型は文字にできない)";
	}

	// レベル基準線を 1 本置いて 3 つ組を書き、**絵に出た数値と名前**まで読む。
	// `viewport` が nil なら「いまのレイヤ」へ、非 nil ならその注釈へ入れる。
	MCObjectHandle SvahPlaceMarker(vwprobe::Report& probe, MCObjectHandle viewport,
								   const TXString& storyName, const TXString& levelType,
								   const std::string& what)
	{
		MCObjectHandle marker = gSDK->CreateCustomObject(kSvahBenchmark2, WorldPt(0, 0), 0.0);
		if (marker == nil)
		{
			probe.fail(std::string("CreateCustomObject(") + kSvahBenchmark2 +
					   ") が nil を返した（" + what + "）");
			return nil;
		}
		if (viewport != nil)
			gSDK->AddViewportAnnotationObject(viewport, marker);
		VWFC::VWObjects::VWParametricObj obj(marker);
		// 注釈へ移したら座標を書き直す（Findings「レベル（標高）オブジェクト」）。
		obj.SetPointObjectPos(VWPoint2D(0, 0));
		// 3 つ組。**3 つ揃って初めて効く**（`Datum` 単独では `GroundPlane` へ倒される）。
		obj.SetParamValue("__StoryName", storyName);
		obj.SetParamValue("__LevelTypeName", levelType);
		obj.SetParamValue("Datum", "StoryLevel");
		gSDK->ResetObject(marker);
		probe.log("  " + what + ": Datum=〈" + SvahStr(obj.GetParamValue("Datum")) +
				  "〉 **Elev=〈" + SvahStr(obj.GetParamValue("Elev")) + "〉**");
		probe.log("    絵に出た文字: " + SvahDrawnTexts(marker));
		return marker;
	}

	// SDK 製の断面ビューポートを 1 本、**いまできる最善の作法で**作る
	// （Findings「ビューポート」——1064 を立て、隠線消去にする）。`showAllLayers` を
	// 立てると全レイヤを表示にして更新まで行う。**段 5 のように何本も作るときは
	// 立てない**——実物件の図面で断面を描き直すのは安くないうえ、レイヤの表示が
	// 条件でないことは #141 で確定しているため。
	MCObjectHandle SvahMakeSectionViewport(MCObjectHandle sheet, const WorldPt& pt1,
										   const WorldPt& pt2, const WorldPt& pt3, double depth,
										   double startHeight, double endHeight, bool showAllLayers)
	{
		MCObjectHandle vp =
			gSDK->CreateSectionViewport(pt1, pt2, pt3, depth, startHeight, endHeight, sheet);
		if (vp == nil)
			return nil;
		TVariableBlock beyond;
		beyond = static_cast<Boolean>(true); // TVariableBlock に setter は無い（operator= で書く）
		gSDK->SetObjectVariable(vp, 1064, beyond);
		VWFC::VWObjects::VWViewportObj(vp).SetRenderType(renderFinalHiddenLine);
		if (showAllLayers)
		{
			const std::vector<MCObjectHandle> layers = SvahAllLayers();
			for (size_t i = 0; i < layers.size(); ++i)
				gSDK->SetViewportLayerVisibility(vp, layers[i], 0); // 0 = 表示
			gSDK->UpdateViewport(vp);
		}
		return vp;
	}

	// ビューポートが表示しているレイヤの枚数（読めた枚数ぶんの / つき）。
	std::string SvahShownLayers(MCObjectHandle vp)
	{
		int shown = 0;
		int readable = 0;
		const std::vector<MCObjectHandle> layers = SvahAllLayers();
		for (size_t i = 0; i < layers.size(); ++i)
		{
			short visibility = -99;
			if (!gSDK->GetViewportLayerVisibility(vp, layers[i], visibility))
				continue;
			++readable;
			if (visibility == 0)
				++shown;
		}
		return std::to_string(shown) + "/" + std::to_string(readable);
	}

	// 群（`EViewportGroupType` 1〜15）の中の図形を数える。
	int SvahCountMembers(MCObjectHandle container)
	{
		int count = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil; h = gSDK->NextObject(h))
			++count;
		return count;
	}

	// 作った個体を消す（undo イベントは開いていないので `useUndo = false`。Findings「Undo」）。
	//
	// **消す順は「中の figure → ビューポート → シートレイヤ」でなければならない。**
	// シートレイヤを先に消すと、その中のビューポートと注釈も道連れに消え、**残った
	// ハンドルは宙に浮く**（それを `DeleteObject` へ渡すのは踏んではいけない道）。
	// だから積む先を 3 本に分け、この関数はそれぞれを 1 本ずつ消す。
	void SvahDeleteAll(std::vector<MCObjectHandle>& made)
	{
		for (size_t i = 0; i < made.size(); ++i)
			if (made[i] != nil)
				gSDK->DeleteObject(made[i], false);
		made.clear();
	}

	// 行列のオブジェクト変数を 1 つ読む（読めなければ空を返す）。
	std::string SvahMatrixText(MCObjectHandle h, short index)
	{
		TVariableBlock var;
		if (!gSDK->GetObjectVariable(h, index, var))
			return "(読めず)";
		return SvahBlockText(var);
	}

	// **1050 と 1055 を 1 行で並べる。** UI 製ではこの 2 つが完全に同じで、SDK 製では
	// 1050 だけが単位行列のまま——1 回目の実測で分かった、いちばん効く 1 行。
	std::string SvahMatrixLine(MCObjectHandle vp)
	{
		return "1050=" + SvahMatrixText(vp, 1050) + " / 1055=" + SvahMatrixText(vp, 1055);
	}

	// その行列が単位行列（＝〈上から見た〉向きのまま）か。
	bool SvahIsIdentity(MCObjectHandle vp, short index)
	{
		TVariableBlock var;
		if (!gSDK->GetObjectVariable(vp, index, var))
			return false;
		TransformMatrix matrix;
		if (!var.GetTransformMatrix(matrix))
			return false;
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 3; ++col)
			{
				const double expected = (row == col) ? 1.0 : 0.0;
				const double diff = matrix.mat[row][col] - expected;
				if (diff > 1e-9 || diff < -1e-9)
					return false;
			}
		return true;
	}

	// **本命の手順そのもの**——そのビューポートの 1055（断面の向き）を、同じ
	// ビューポートの 1050（ビュー行列）へ写す。UI 製を 1 つも参照しない。
	std::string SvahCopy1055To1050(MCObjectHandle vp)
	{
		TVariableBlock sectionMatrix;
		if (!gSDK->GetObjectVariable(vp, 1055, sectionMatrix))
			return "1055 が読めず（写せない）";
		const bool wrote = gSDK->SetObjectVariable(vp, 1050, sectionMatrix) ? true : false;
		return "Set(1050)=" + SvahBool(wrote) + " → " + SvahMatrixLine(vp);
	}

	// 断面として壊れていないかを**数えられるものだけ**で見る。群 4 は断面、
	// 5 / 6 / 7 / 15 はキャッシュ——描き終えていれば図形が入る。
	std::string SvahSectionHealth(MCObjectHandle vp)
	{
		std::string line = "1054=" + SvahBool(SvahReadBool(vp, 1054, false)) +
						   " ビュー種別(1007)=" + SvahMatrixText(vp, 1007) +
						   " 1056=" + SvahMatrixText(vp, 1056) +
						   " 表示レイヤ=" + SvahShownLayers(vp) + " 群";
		const short groups[] = {4, 5, 6, 7, 15};
		for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i)
		{
			MCObjectHandle group = gSDK->GetViewportGroup(vp, groups[i]);
			line += " " + std::to_string(static_cast<int>(groups[i])) + "=" +
					(group == nil ? std::string("無") : std::to_string(SvahCountMembers(group)));
		}
		return line;
	}
} // namespace

VW_PROBE("section-viewport-annotation-height",
		 "SDK 製の断面ビューポートでも注釈に高さを出す手順（#147）",
		 "1050（ovViewportViewMatrix）を書けば出る、を手順として使えるところまで詰める")
{
	probe.log("**この調査は図面へ書き込む**（作ったものは最後に全部消すが、undo は開いて");
	probe.log("いない）。走らせる前に保存しておくこと。UI が置いた図形は読むだけ。");
	probe.log("");
	probe.log("1 回目で分かったこと: **1050（ovViewportViewMatrix）だけを書いたビューポートで");
	probe.log("Elev が 540 になった**。UI 製ではその 1050 が 1055 と完全に同じ値で、SDK 製では");
	probe.log("1050 が単位行列のまま残っている。ここではそれを**手順**にできるかを見る。");
	probe.log("");

	// ---------------------------------------------------------------- 段 0
	probe.log("=== 段 0: 基準にするレベルと、対照にする UI 製の断面ビューポート ===");
	std::vector<MCObjectHandle> stories;
	const std::vector<MCObjectHandle> allLayers = SvahAllLayers();
	for (size_t i = 0; i < allLayers.size(); ++i)
	{
		MCObjectHandle story = gSDK->GetStoryOfLayer(allLayers[i]);
		if (story == nil)
			continue;
		bool seen = false;
		for (size_t j = 0; j < stories.size(); ++j)
			if (stories[j] == story)
				seen = true;
		if (!seen)
			stories.push_back(story);
	}

	MCObjectHandle targetStory = nil;
	TXString targetType;
	MCObjectHandle targetLayer = nil;
	const short typeCount = gSDK->GetNumLayerLevelTypes();
	for (size_t i = 0; targetStory == nil && i < stories.size(); ++i)
		for (short t = 1; targetStory == nil && t <= typeCount; ++t)
		{
			TXString name = gSDK->GetLayerLevelTypeName(t);
			MCObjectHandle layer = gSDK->GetLayerForStory(stories[i], name);
			if (layer == nil)
				continue;
			targetStory = stories[i];
			targetType = name;
			targetLayer = layer;
		}
	if (targetStory == nil)
	{
		probe.fail("レイヤの生えているストーリレベルが 1 つも無い"
				   "（この調査は UI で作った実物件の図面で走らせる）");
		return;
	}
	TXString targetStoryName;
	gSDK->GetObjectName(targetStory, targetStoryName);
	const std::string targetZ = SvahResolvedLevelZ(targetLayer, targetType);
	probe.log("基準にするレベル: ストーリ〈" + SvahStr(targetStoryName) + "〉 / レベル種別〈" +
			  SvahStr(targetType) + "〉 **絶対Z=" + targetZ + "**");

	const std::vector<MCObjectHandle> viewports = SvahCollectViewports();
	MCObjectHandle uiViewport = nil;
	for (size_t i = 0; uiViewport == nil && i < viewports.size(); ++i)
		if (SvahReadBool(viewports[i], 1054, false))
			uiViewport = viewports[i];
	if (uiViewport == nil)
		probe.log("UI 製の断面ビューポートは無い（段 4 の対照と段 6 は行えない）");
	else
		probe.log("UI 製の対照: ビューポート〈" + SvahName(uiViewport) + "〉");

	gSDK->DefineCustomObject(kSvahBenchmark2, kCustomObjectPrefNever);
	std::vector<MCObjectHandle> madeMarkers;
	std::vector<MCObjectHandle> madeViewports;
	std::vector<MCObjectHandle> madeLayers;

	// ---------------------------------------------------------------- 段 1
	probe.log("");
	probe.log("=== 段 1: 本命の手順——**自分の 1055 を自分の 1050 へ書く** ===");
	probe.log("  UI 製を 1 つも参照しない道。これが効けば、UI 製の無い文書でも使える。");
	MCObjectHandle sheet1 = gSDK->CreateLayer("#147-2 本命", kLayerSheet);
	MCObjectHandle vp1 = nil;
	if (sheet1 != nil)
		vp1 = SvahMakeSectionViewport(sheet1, WorldPt(-100000, -100000), WorldPt(100000, -100000),
									  WorldPt(0, 100000), 0, -100000, 100000, false);
	if (vp1 == nil)
	{
		if (sheet1 != nil)
			madeLayers.push_back(sheet1);
		SvahDeleteAll(madeLayers);
		probe.fail("CreateSectionViewport が nil を返した（この調査は行えない）");
		return;
	}
	madeLayers.push_back(sheet1);
	madeViewports.push_back(vp1);
	probe.log("  1-a 作った直後: " + SvahMatrixLine(vp1));
	probe.log("  1-b 1055 を 1050 へ写す: " + SvahCopy1055To1050(vp1));
	madeMarkers.push_back(SvahPlaceMarker(probe, vp1, targetStoryName, targetType,
										  "1-c 写した直後の注釈（ここが本命）"));

	// ---------------------------------------------------------------- 段 2
	probe.log("");
	probe.log("=== 段 2: `UpdateViewport` は 1050 を消すか ===");
	probe.log("  実用では必ず更新するので、ここが消えるなら「更新の後に書く」が手順になる。");
	gSDK->UpdateViewport(vp1);
	probe.log("  2-a 更新の後: " + SvahMatrixLine(vp1));
	madeMarkers.push_back(SvahPlaceMarker(probe, vp1, targetStoryName, targetType,
										  "2-b 更新の後に置いた新しい 1 本"));

	// ---------------------------------------------------------------- 段 3
	probe.log("");
	probe.log("=== 段 3: 実用の順番（下ごしらえを全部済ませてから最後に 1050 を書く）===");
	MCObjectHandle sheet3 = gSDK->CreateLayer("#147-2 実用の順番", kLayerSheet);
	MCObjectHandle vp3 = nil;
	if (sheet3 != nil)
		vp3 = SvahMakeSectionViewport(sheet3, WorldPt(-100000, -100000), WorldPt(100000, -100000),
									  WorldPt(0, 100000), 0, -100000, 100000, true);
	if (vp3 == nil)
	{
		probe.log("  ビューポートを作れなかった（この段は行えない）");
		if (sheet3 != nil)
			madeLayers.push_back(sheet3);
	}
	else
	{
		madeLayers.push_back(sheet3);
		madeViewports.push_back(vp3);
		probe.log("  3-a 表示レイヤ・1064・レンダ・更新まで済ませた後: " + SvahMatrixLine(vp3));
		probe.log("  3-b 1055 を 1050 へ写す: " + SvahCopy1055To1050(vp3));
		madeMarkers.push_back(
			SvahPlaceMarker(probe, vp3, targetStoryName, targetType, "3-c 写した直後の注釈"));
		gSDK->UpdateViewport(vp3);
		probe.log("  3-d もう一度更新した後: " + SvahMatrixLine(vp3));
		madeMarkers.push_back(SvahPlaceMarker(probe, vp3, targetStoryName, targetType,
											  "3-e もう一度更新した後の注釈"));
	}

	// ---------------------------------------------------------------- 段 4
	probe.log("");
	probe.log("=== 段 4: 1 回目に「差を全部書き写したら 0 のまま」だったのはなぜか ===");
	probe.log("  1050 を写したビューポートへ、**他の変数を 1 つだけ足して**読む。");
	probe.log("  `0` に戻る行があれば、それが打ち消していたもの。");
	const short kSvahOthers[] = {1003, 1004, 1032, 1033, 1035, 1049, 1064};
	const size_t otherCount = sizeof(kSvahOthers) / sizeof(kSvahOthers[0]);
	if (uiViewport == nil)
	{
		probe.log("  UI 製が無いので、足す値が取れない（この段は行えない）");
	}
	else
	{
		for (size_t i = 0; i < otherCount; ++i)
		{
			const std::string sheetName =
				"#147-2 打ち消し " + std::to_string(static_cast<int>(kSvahOthers[i]));
			MCObjectHandle sheetN = gSDK->CreateLayer(TXString(sheetName.c_str()), kLayerSheet);
			if (sheetN == nil)
				continue;
			madeLayers.push_back(sheetN);
			MCObjectHandle vpN =
				SvahMakeSectionViewport(sheetN, WorldPt(-100000, -100000), WorldPt(100000, -100000),
										WorldPt(0, 100000), 0, -100000, 100000, false);
			if (vpN == nil)
			{
				probe.log("  " + std::to_string(static_cast<int>(kSvahOthers[i])) +
						  ": 断面ビューポートを作れなかった");
				continue;
			}
			madeViewports.push_back(vpN);
			SvahCopy1055To1050(vpN);
			TVariableBlock other;
			const bool got =
				gSDK->GetObjectVariable(uiViewport, kSvahOthers[i], other) ? true : false;
			const bool wrote =
				got && gSDK->SetObjectVariable(vpN, kSvahOthers[i], other) ? true : false;
			madeMarkers.push_back(SvahPlaceMarker(
				probe, vpN, targetStoryName, targetType,
				"1050 ＋ " + std::to_string(static_cast<int>(kSvahOthers[i])) +
					" を足した（Set=" + SvahBool(wrote) + "）1050 は" +
					(SvahIsIdentity(vpN, 1050) ? "**単位行列に戻った**" : "保たれている")));
		}
	}

	// ---------------------------------------------------------------- 段 5
	probe.log("");
	probe.log("=== 段 5: 1050 を書いたビューポートは断面として壊れていないか（機械で見る）===");
	probe.log(
		"  「絵が出ているか」は目視だが、**断面のキャッシュ群に図形が入ったか**は数えられる。");
	probe.log("  群 4=断面 / 5・6・7・15=キャッシュ。UI 製と並べて読む。");
	if (uiViewport != nil)
		probe.log("  UI 製〈" + SvahName(uiViewport) + "〉: " + SvahSectionHealth(uiViewport));
	probe.log("  段 1 の 1 本（1050 を写した。更新済み）: " + SvahSectionHealth(vp1));
	if (vp3 != nil)
		probe.log("  段 3 の 1 本（下ごしらえ → 1050 → 更新）: " + SvahSectionHealth(vp3));
	MCObjectHandle sheet5 = gSDK->CreateLayer("#147-2 素のまま", kLayerSheet);
	MCObjectHandle vp5 = nil;
	if (sheet5 != nil)
		vp5 = SvahMakeSectionViewport(sheet5, WorldPt(-100000, -100000), WorldPt(100000, -100000),
									  WorldPt(0, 100000), 0, -100000, 100000, true);
	if (vp5 != nil)
	{
		madeLayers.push_back(sheet5);
		madeViewports.push_back(vp5);
		probe.log("  対照（1050 を書かない。更新済み）: " + SvahSectionHealth(vp5));
		madeMarkers.push_back(
			SvahPlaceMarker(probe, vp5, targetStoryName, targetType, "対照の注釈"));
	}
	else if (sheet5 != nil)
	{
		madeLayers.push_back(sheet5);
	}

	// ---------------------------------------------------------------- 段 6
	probe.log("");
	probe.log("=== 段 6: 1 回目の再現（UI 製の 1050 を書く）===");
	if (uiViewport == nil)
	{
		probe.log("  UI 製が無いので行えない");
	}
	else
	{
		MCObjectHandle sheet6 = gSDK->CreateLayer("#147-2 UI の 1050", kLayerSheet);
		MCObjectHandle vp6 = nil;
		if (sheet6 != nil)
			vp6 =
				SvahMakeSectionViewport(sheet6, WorldPt(-100000, -100000), WorldPt(100000, -100000),
										WorldPt(0, 100000), 0, -100000, 100000, false);
		if (vp6 == nil)
		{
			probe.log("  ビューポートを作れなかった");
			if (sheet6 != nil)
				madeLayers.push_back(sheet6);
		}
		else
		{
			madeLayers.push_back(sheet6);
			madeViewports.push_back(vp6);
			TVariableBlock uiMatrix;
			const bool got = gSDK->GetObjectVariable(uiViewport, 1050, uiMatrix) ? true : false;
			const bool wrote = got && gSDK->SetObjectVariable(vp6, 1050, uiMatrix) ? true : false;
			madeMarkers.push_back(
				SvahPlaceMarker(probe, vp6, targetStoryName, targetType,
								"UI 製の 1050 を書いた（Set=" + SvahBool(wrote) + "）"));
		}
	}

	// ---------------------------------------------------------------- 片付け
	probe.log("");
	probe.log("=== 片付け ===");
	const size_t madeCount = madeMarkers.size() + madeViewports.size() + madeLayers.size();
	// **この順でなければならない**（注釈の個体 → ビューポート → シートレイヤ）。
	SvahDeleteAll(madeMarkers);
	SvahDeleteAll(madeViewports);
	SvahDeleteAll(madeLayers);
	probe.log("  作った " + std::to_string(madeCount) + " 個を消した（UI 製の元は触っていない）");

	probe.log("");
	probe.log("読み方:");
	probe.log("  段 1-c が " + targetZ + " なら、**手順は『1055 を 1050 へ写す』で済む**");
	probe.log("  （UI 製を参照しなくてよい）。`0` なら、効いていたのは UI 製の値そのもの。");
	probe.log("  段 2-a で 1050 が単位行列に戻っていれば「更新が消す」——段 3 の順番が手順。");
	probe.log("  段 4 で `0` に戻った行が、1 回目に打ち消していたもの。");
	probe.log("  段 5 でキャッシュ群の個数が対照と同じなら、1050 を書いても断面は壊れていない。");
}
