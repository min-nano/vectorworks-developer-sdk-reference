//
//	probes/runtime/section-viewport-annotation-height/probe.cpp
//
//	[issue #147] **`CreateSectionViewport` で作った断面ビューポートの注釈でも、
//	レベル基準線に高さを出させる手順を確定させる。**
//
//	【1 回目・2 回目で確定したこと（2026-09-29。実物件の図面）】
//
//	  - **効くのは `ovViewportViewMatrix`（1050）ただ 1 つ。** 1000〜1150 の総当りで
//	    差は 14 件あったが、1 つずつ書き分けて `Elev` が `540` になったのは 1050 だけ。
//	  - **自分の `ovSheetLayerSectionViewportViewMatrix`（1055）を自分の 1050 へ写せば
//	    出る**（UI 製を 1 つも参照しない道がある）。
//	  - **`UpdateViewport` が 1050 を単位行列へ戻す。** 写した後に更新すると、次に
//	    置いた個体は `0` に戻る。1 回目に「差を全部書き写しても 0 のまま」だったのは
//	    これが理由で、**打ち消していたのは他の変数ではなかった**（他の 7 件を 1 つずつ
//	    足しても `540` のまま）。
//	  - **そもそもの差はビューの向き。** `ovViewportViewType`（1007）が
//	    UI 製 ＝ **6（`standardViewRight`）** / SDK 製 ＝ **7（`standardViewTop`）**。
//	    `CreateSectionViewport` は断面の向き（1055）は作るが、**ビューポート自体は
//	    〈上から見た〉ままで、ビュー行列も単位行列**。だから縦方向の基準が無い。
//	  - 1050 を写しても断面の素性は対照と同じ（1054 / 1056 / 群 4 の個数が一致）。
//	  - `DuplicateObject` で UI 製を複製すると `540` が出る（表示レイヤを書き換えて
//	    更新しても保たれた）。
//
//	【この 3 回目で埋めること——**運用に耐える手順か**】
//	「更新が 1050 を戻す」ので、**このままでは『図面を開いて更新ボタンを押したら `0` に
//	戻る』かもしれない**。手順として書くには、次の 4 つが要る。
//
//	  1) **置いてある個体は、更新の後も `540` のままか。** 2 回目で読んだのは
//	     「更新の後に**新しく置いた**個体」だった。**既に置いてある個体**が更新や
//	     `ResetObject` でどうなるかは、まだ測っていない。ここが手順の寿命を決める。
//	  2) **`VWViewportObj::SetViewType(standardViewRight)` なら向きごと変えられるか。**
//	     1007 は `SetObjectVariable` では書けなかった（`Set=false`）が、VWFC には
//	     専用の口がある。これが効けば**更新しても戻らない**本当の直し方になる。
//	  3) **回復の手順で足りるか。** 更新のたびに「1050 を写し直して `ResetObject`」で
//	     `540` に戻せるなら、プラグイン側はそれを最後に 1 回やればよい。
//	  4) **注釈を全部置き終えてから最後に 1050 を写す**という順番で、**置いてある個体が
//	     まとめて `540` になるか**（プラグインが実際に書く順番はこれになる）。
//
//	【走らせる図面】**UI が作った断面ビューポートのある実物件の図面**（段 5 の対照に使う。
//	段 1〜4 は新規の空図面でもストーリがあれば走る）。**この調査は図面へ書き込む**
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

	// **置いてある個体を、作り直さずにそのまま読む。** 「更新で壊れたか」を見るには、
	// `ResetObject` を挟まずに読む回と挟む回の両方が要る——挟めば PIO は欄を
	// 書き直すので、**壊れているのに直って見える／直っているのに壊れて見える**ことが
	// どちらも起こり得る。
	void SvahReadMarker(vwprobe::Report& probe, MCObjectHandle marker, const std::string& what)
	{
		if (marker == nil)
			return;
		VWFC::VWObjects::VWParametricObj obj(marker);
		probe.log("  " + what + ": Datum=〈" + SvahStr(obj.GetParamValue("Datum")) +
				  "〉 **Elev=〈" + SvahStr(obj.GetParamValue("Elev")) + "〉**");
		probe.log("    絵に出た文字: " + SvahDrawnTexts(marker));
	}

	// 置いてある個体を作り直してから読む（PIO に欄を引き直させる）。
	void SvahResetAndRead(vwprobe::Report& probe, MCObjectHandle marker, const std::string& what)
	{
		if (marker == nil)
			return;
		gSDK->ResetObject(marker);
		SvahReadMarker(probe, marker, what);
	}
} // namespace

VW_PROBE("section-viewport-annotation-height",
		 "SDK 製の断面ビューポートでも注釈に高さを出す手順（#147）",
		 "1050 を写す手順が更新に耐えるか、SetViewType で向きごと直せるかを詰める")
{
	probe.log("**この調査は図面へ書き込む**（作ったものは最後に全部消すが、undo は開いて");
	probe.log("いない）。走らせる前に保存しておくこと。UI が置いた図形は読むだけ。");
	probe.log("");
	probe.log("ここまでに確定: 効くのは 1050（ovViewportViewMatrix）ただ 1 つ。自分の 1055 を");
	probe.log("自分の 1050 へ写せば出る。**ただし UpdateViewport が 1050 を単位行列へ戻す。**");
	probe.log("そもそもの差はビューの向き——1007 が UI 製=6（右）/ SDK 製=7（上）。");
	probe.log("この回で見るのは「**更新に耐える手順があるか**」。");
	probe.log("");

	// ---------------------------------------------------------------- 段 0
	probe.log("=== 段 0: 基準にするレベル ===");
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
				   "（ストーリのある図面で走らせる）");
		return;
	}
	TXString targetStoryName;
	gSDK->GetObjectName(targetStory, targetStoryName);
	const std::string targetZ = SvahResolvedLevelZ(targetLayer, targetType);
	probe.log("基準にするレベル: ストーリ〈" + SvahStr(targetStoryName) + "〉 / レベル種別〈" +
			  SvahStr(targetType) + "〉 **絶対Z=" + targetZ + "**");

	gSDK->DefineCustomObject(kSvahBenchmark2, kCustomObjectPrefNever);
	std::vector<MCObjectHandle> madeMarkers;
	std::vector<MCObjectHandle> madeViewports;
	std::vector<MCObjectHandle> madeLayers;

	// ---------------------------------------------------------------- 段 1
	probe.log("");
	probe.log("=== 段 1: 置いてある個体は、更新の後も 540 のままか ===");
	probe.log("  **ここが手順の寿命を決める。** 2 回目に読んだのは「更新の後に新しく置いた");
	probe.log("  個体」だった。既に置いてある個体が、更新や ResetObject でどうなるかを見る。");
	MCObjectHandle sheet1 = gSDK->CreateLayer("#147-3 寿命", kLayerSheet);
	MCObjectHandle vp1 = nil;
	if (sheet1 != nil)
		vp1 = SvahMakeSectionViewport(sheet1, WorldPt(-100000, -100000), WorldPt(100000, -100000),
									  WorldPt(0, 100000), 0, -100000, 100000, true);
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
	probe.log("  1-a 1055 を 1050 へ写す: " + SvahCopy1055To1050(vp1));
	MCObjectHandle marker1 = SvahPlaceMarker(probe, vp1, targetStoryName, targetType,
											 "1-b 写した直後に置いた（540 のはず）");
	madeMarkers.push_back(marker1);
	gSDK->UpdateViewport(vp1);
	probe.log("  1-c 更新した（1050 は" +
			  std::string(SvahIsIdentity(vp1, 1050) ? "**単位行列へ戻った**" : "保たれている") +
			  "）");
	SvahReadMarker(probe, marker1, "1-d 更新の後、**触らずに読む**");
	SvahResetAndRead(probe, marker1, "1-e 更新の後、ResetObject してから読む");

	// ---------------------------------------------------------------- 段 2
	probe.log("");
	probe.log("=== 段 2: 更新の後に写し直して ResetObject すれば戻るか（回復の手順）===");
	probe.log("  戻るなら、プラグインは「最後に 1 回だけ写して作り直す」で済む。");
	probe.log("  2-a 写し直す: " + SvahCopy1055To1050(vp1));
	SvahResetAndRead(probe, marker1, "2-b 写し直して ResetObject した後");

	// ---------------------------------------------------------------- 段 3
	probe.log("");
	probe.log("=== 段 3: 注釈を全部置き終えてから、最後に 1050 を写す（実際に書く順番）===");
	MCObjectHandle sheet3 = gSDK->CreateLayer("#147-3 最後に写す", kLayerSheet);
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
		std::vector<MCObjectHandle> three;
		for (int n = 1; n <= 3; ++n)
		{
			MCObjectHandle m =
				SvahPlaceMarker(probe, vp3, targetStoryName, targetType,
								"3-a 先に置いた " + std::to_string(n) + " 本目（まだ 0 のはず）");
			three.push_back(m);
			madeMarkers.push_back(m);
		}
		probe.log("  3-b 全部置いてから 1055 を 1050 へ写す: " + SvahCopy1055To1050(vp3));
		for (size_t n = 0; n < three.size(); ++n)
			SvahResetAndRead(probe, three[n],
							 "3-c 写した後に ResetObject した " + std::to_string(n + 1) + " 本目");
	}

	// ---------------------------------------------------------------- 段 4
	probe.log("");
	probe.log("=== 段 4: `VWViewportObj::SetViewType` で向きごと変えられるか ===");
	probe.log("  1007（ovViewportViewType）は SetObjectVariable では書けなかったが、VWFC には");
	probe.log("  専用の口がある。UI 製は 6（standardViewRight）、SDK 製は 7（standardViewTop）。");
	probe.log("  **これが効けば、更新しても戻らない本当の直し方**になる。");
	MCObjectHandle sheet4 = gSDK->CreateLayer("#147-3 SetViewType", kLayerSheet);
	MCObjectHandle vp4 = nil;
	if (sheet4 != nil)
		vp4 = SvahMakeSectionViewport(sheet4, WorldPt(-100000, -100000), WorldPt(100000, -100000),
									  WorldPt(0, 100000), 0, -100000, 100000, true);
	if (vp4 == nil)
	{
		probe.log("  ビューポートを作れなかった（この段は行えない）");
		if (sheet4 != nil)
			madeLayers.push_back(sheet4);
	}
	else
	{
		madeLayers.push_back(sheet4);
		madeViewports.push_back(vp4);
		VWFC::VWObjects::VWViewportObj obj4(vp4);
		probe.log("  4-a 変える前: ビュー種別(GetViewType)=" +
				  std::to_string(static_cast<int>(obj4.GetViewType())) + " / " +
				  SvahMatrixLine(vp4));
		obj4.SetViewType(standardViewRight);
		probe.log("  4-b SetViewType(standardViewRight=6) の後: ビュー種別=" +
				  std::to_string(static_cast<int>(obj4.GetViewType())) + " / " +
				  SvahMatrixLine(vp4));
		madeMarkers.push_back(
			SvahPlaceMarker(probe, vp4, targetStoryName, targetType, "4-c 向きを変えた直後の注釈"));
		gSDK->UpdateViewport(vp4);
		probe.log(
			"  4-d 更新の後: ビュー種別=" + std::to_string(static_cast<int>(obj4.GetViewType())) +
			" / " + SvahMatrixLine(vp4));
		madeMarkers.push_back(
			SvahPlaceMarker(probe, vp4, targetStoryName, targetType, "4-e 更新の後に置いた注釈"));
		probe.log("  4-f 断面として: " + SvahSectionHealth(vp4));

		// 4-g: 向きを変えたうえで 1050 も写すと、更新に耐えるか（合わせ技）。
		probe.log("  4-g 向きを変えたうえで 1055 を 1050 へ写す: " + SvahCopy1055To1050(vp4));
		MCObjectHandle marker4 =
			SvahPlaceMarker(probe, vp4, targetStoryName, targetType, "4-h 合わせ技の直後の注釈");
		madeMarkers.push_back(marker4);
		gSDK->UpdateViewport(vp4);
		probe.log("  4-i もう一度更新した（1050 は" +
				  std::string(SvahIsIdentity(vp4, 1050) ? "**単位行列へ戻った**" : "保たれている") +
				  "）");
		SvahResetAndRead(probe, marker4, "4-j 更新の後に ResetObject して読む");
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
	probe.log("読み方（期待する絶対Z は " + targetZ + "）:");
	probe.log("  段 1-d が 540 のままなら、**置いた個体は更新で壊れない**（書き込まれた値が");
	probe.log("  残る）。1-e で 0 に戻るなら、**作り直すと消える**＝図面を触ると壊れる。");
	probe.log("  段 2-b で戻るなら、回復の手順がある。");
	probe.log("  段 3-c が 3 本とも 540 なら、「全部置いてから最後に写す」が実際の手順。");
	probe.log("  段 4-e が 540 なら、**SetViewType が本当の直し方**（更新に耐える）。");
	probe.log("  段 4-j は合わせ技が更新に耐えるか。");
}
