//
//	probes/runtime/section-viewport-annotation-height/probe.cpp
//
//	[issue #147] **UI が作った断面ビューポートの注釈ではレベル基準線が高さを出すのに、
//	`ISDK::CreateSectionViewport` で作った断面ビューポートの注釈では `0` になる。その差は何か。**
//
//	#141 で「`0` になるかを決めているのはストーリではなくビューポート」まで確定している
//	（Findings「レイヤとストーリ」「レベル（標高）オブジェクト」）。**潰れている筋はもう
//	一度やらない**——ストーリの作り方・レイヤの表示（書いて読み戻して確認済み）・
//	`ovIsSectionViewport`（SDK 製も `true`）・個体の全 33 欄（#135 で残差 0）。
//
//	だからこのプローブは**ビューポートの側だけ**を潰す。段は 8 つ。**どれも数値で読める**
//	（目視を頼まない）。
//
//	  1) **同じ 1 回の実行で差を再現する。** UI 製の断面ビューポートと、その場で作った
//	     SDK 製の断面ビューポートの注釈へ、同じレベルの 3 つ組を 1 本ずつ置いて読む。
//	     ここで差が出なければ、以降の比較には意味が無い（先に確かめる）。
//	  2) **素性を総当りで突き合わせる。** オブジェクト変数を 1000〜1150 まで 1 つずつ
//	     両方から読み、**値の違う行だけ**を並べる。当たりを付けて数個だけ見るのではなく
//	     総当りにするのは、**どの変数が効くかを知らないから**である（#135 で個体の側を
//	     全欄突き合わせたのと同じやり方）。
//	     候補として名前の付いているものだけでも `ovViewportLayerHeightIgnored`(1040) /
//	     `ovIsDesignLayerSectionViewport`(1043) / `ovViewportIsHorizontalSection`(1048) /
//	     `ovSheetLayerSectionViewportViewMatrix`(1055) /
//	     `ovSectionViewportSectionViewMatrix`(1056) があり、どれも 1000 番台に居る。
//	  3) **ビューポート群を突き合わせる。** `GetViewportGroup` を 1〜15
//	     （`EViewportGroupType`）まで引いて、群があるか・中に図形が何個あるかを並べる。
//	  4) **差を丸ごと書き写す。** 段 2 で違っていた変数を UI 製の値で SDK 製へ書き、
//	     **書けたか・読み戻せたか**を出してから、新しいレベル基準線を注釈へ置いて読む。
//	     ここで数値が出れば「差はオブジェクト変数の中にある」と確定する。
//	  5) **効いた 1 つを突き止める。** 段 4 で数値が出たら、**変数を 1 つだけ書いた
//	     新しいビューポート**を差の数だけ作って、どれが効いたかを分ける。
//	  6) **ハンドル型の変数は別に扱う。** 他のオブジェクトを指している変数は、書くと
//	     図面の側へ影響が出得るので段 4・5 から外し、**新しいビューポートだけ**へ書いて試す。
//	  7) **作り方を振る。** `CreateSectionViewport` の引数（`depth`・`startHeight`・
//	     `endHeight`・断面線の位置と向き）を 4 通り振って注釈を読む。
//	  8) **複製を試す。** UI 製を `DuplicateObject` で複製して注釈を読む。数値が出るなら、
//	     素性ごと引き継ぐ逃げ道になる（表示レイヤを書き換えても残るかまで見る）。
//
//	【走らせる図面】**UI が作った断面ビューポートのある実物件の図面**（新規の空図面では
//	比較対象が無いので、段 0 で「行えない」と出して止まる）。**この調査は図面へ書き込む**
//	——シートレイヤ・断面ビューポート・レベル基準線を作って**最後に全部消す**が、
//	undo は開いていない（Findings「Undo」）ので、**走らせる前に保存**しておくこと。
//	UI が置いた図形は読むだけで、書き換えない（複製は作って消す）。
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

	// 総当りで読むオブジェクト変数の範囲。ビューポート関係は 1000 番台に固まっており
	// （`ObjectVariables.h`）、**宣言の無い欠番も含めて**読む——UI が使っている非公開の
	// 変数がそこに居る見込みがあるため（Findings「ビューポート」の打ち切った調査で
	// 1060〜1090 は読み取り専用で触っており、落ちないことは分かっている）。
	const short kSvahVarFirst = 1000;
	const short kSvahVarLast = 1150;

	// 段 5（1 つずつ書いて効いたものを突き止める）で作るビューポートの上限。
	// 断面ビューポートを作るのは安くないので、青天井にはしない。
	const size_t kSvahIsolateLimit = 12;

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

	// ブロックがオブジェクトを指しているか（段 6 で別扱いにするため）。
	bool SvahIsHandleBlock(const TVariableBlock& var)
	{
		MCObjectHandle h = nil;
		return var.GetMCObjectHandle(h) ? true : false;
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
	void SvahDeleteAll(std::vector<MCObjectHandle>& made)
	{
		for (size_t i = made.size(); i > 0; --i)
			if (made[i - 1] != nil)
				gSDK->DeleteObject(made[i - 1], false);
		made.clear();
	}

	// 段 2 で見つけた「値の違う変数」1 件。
	struct SvahDiff
	{
		short fIndex = 0;
		TVariableBlock fUiValue; // UI 製が持っていた値（段 4・5・6 で書き写す）
		std::string fUiText;
		std::string fSdkText;
		bool fIsHandle = false;
	};
} // namespace

VW_PROBE("section-viewport-annotation-height",
		 "SDK 製の断面ビューポートの注釈で高さが 0 になる理由（#147）",
		 "UI 製と SDK 製の断面ビューポートを総当りで突き合わせ、差を書き写し、作り方を振る")
{
	probe.log("**この調査は図面へ書き込む**（作ったものは最後に全部消すが、undo は開いて");
	probe.log("いない）。走らせる前に保存しておくこと。UI が置いた図形は読むだけ。");
	probe.log("");

	// ---------------------------------------------------------------- 段 0
	probe.log("=== 段 0: 比較する 2 本と、基準にするレベルを選ぶ ===");

	// レイヤ経由でストーリを集め、**レイヤの生えているレベル種別**を 1 つ選ぶ。
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
	probe.log("レイヤ=" + std::to_string(allLayers.size()) +
			  " 枚 / ストーリ=" + std::to_string(stories.size()) +
			  " 件 / レベル種別=" + std::to_string(gSDK->GetNumLayerLevelTypes()) + " 個");

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
			  SvahStr(targetType) + "〉 **絶対Z=" + targetZ + "** レイヤ〈" +
			  SvahName(targetLayer) + "〉");

	// UI 製の断面ビューポートを 1 本選ぶ（`ovIsSectionViewport`＝1054 が true のもの）。
	const std::vector<MCObjectHandle> viewports = SvahCollectViewports();
	MCObjectHandle uiViewport = nil;
	int sectionCount = 0;
	for (size_t i = 0; i < viewports.size(); ++i)
		if (SvahReadBool(viewports[i], 1054, false))
		{
			++sectionCount;
			if (uiViewport == nil)
				uiViewport = viewports[i];
		}
	probe.log("この文書のビューポート=" + std::to_string(viewports.size()) +
			  " 件 / そのうち断面（1054=true）=" + std::to_string(sectionCount) + " 件");
	if (uiViewport == nil)
	{
		probe.fail("UI が作った断面ビューポートが 1 本も無い"
				   "（比較対象が無いので、この調査は行えない）");
		return;
	}
	probe.log("UI 製の比較対象: ビューポート〈" + SvahName(uiViewport) +
			  "〉 表示レイヤ=" + SvahShownLayers(uiViewport));

	gSDK->DefineCustomObject(kSvahBenchmark2, kCustomObjectPrefNever);
	std::vector<MCObjectHandle> made;

	// SDK 製を 1 本作る。断面線は建物を挟むように長く引き、高さの範囲も広く取る
	// （空の断面にしないため。Findings「ビューポート」）。
	MCObjectHandle sheet = gSDK->CreateLayer("#147 の調査用シート", kLayerSheet);
	MCObjectHandle sdkViewport = nil;
	if (sheet != nil)
		sdkViewport =
			SvahMakeSectionViewport(sheet, WorldPt(-100000, -100000), WorldPt(100000, -100000),
									WorldPt(0, 100000), 0, -100000, 100000, true);
	if (sdkViewport == nil)
	{
		if (sheet != nil)
			made.push_back(sheet);
		SvahDeleteAll(made);
		probe.fail("CreateSectionViewport が nil を返した（この調査は行えない）");
		return;
	}
	probe.log("SDK 製の比較対象を作った: シートレイヤ〈" + SvahName(sheet) +
			  "〉の上 / 表示レイヤ=" + SvahShownLayers(sdkViewport));

	// ---------------------------------------------------------------- 段 1
	probe.log("");
	probe.log("=== 段 1: 同じ 1 回の実行で差が出ることを確かめる ===");
	probe.log("**ここで差が出なければ、段 2 以降の突き合わせには意味が無い。**");
	made.push_back(SvahPlaceMarker(probe, uiViewport, targetStoryName, targetType,
								   "UI 製〈" + SvahName(uiViewport) + "〉の注釈（絶対Z=" + targetZ +
									   " が出るはず）"));
	made.push_back(SvahPlaceMarker(probe, sdkViewport, targetStoryName, targetType,
								   "SDK 製の注釈（いまは 0 になるはず）"));

	// ---------------------------------------------------------------- 段 2
	probe.log("");
	probe.log("=== 段 2: オブジェクト変数 " + std::to_string(kSvahVarFirst) + "〜" +
			  std::to_string(kSvahVarLast) + " を総当りで突き合わせる ===");
	probe.log("**違う行だけ**を出す。落ちたときに何番まで進んだか分かるよう、20 件ごとに");
	probe.log("印を残す。");
	int bothReadable = 0;
	std::vector<SvahDiff> diffs;
	for (short index = kSvahVarFirst; index <= kSvahVarLast; ++index)
	{
		TVariableBlock uiVar;
		TVariableBlock sdkVar;
		const bool uiReadable = gSDK->GetObjectVariable(uiViewport, index, uiVar) ? true : false;
		const bool sdkReadable = gSDK->GetObjectVariable(sdkViewport, index, sdkVar) ? true : false;
		if (uiReadable && sdkReadable)
			++bothReadable;
		if (uiReadable || sdkReadable)
		{
			const std::string uiText = uiReadable ? SvahBlockText(uiVar) : "(読めず)";
			const std::string sdkText = sdkReadable ? SvahBlockText(sdkVar) : "(読めず)";
			if (uiText != sdkText)
			{
				SvahDiff diff;
				diff.fIndex = index;
				diff.fUiValue = uiVar;
				diff.fUiText = uiText;
				diff.fSdkText = sdkText;
				diff.fIsHandle = uiReadable && SvahIsHandleBlock(uiVar);
				diffs.push_back(diff);
			}
		}
		if ((index - kSvahVarFirst) % 20 == 19)
			probe.log("  …" + std::to_string(static_cast<int>(index)) + " まで読んだ");
	}
	probe.log("両方で読めた番号=" + std::to_string(bothReadable) +
			  " / 値の違った番号=" + std::to_string(diffs.size()));
	for (size_t i = 0; i < diffs.size(); ++i)
		probe.log("  " + std::to_string(static_cast<int>(diffs[i].fIndex)) +
				  (diffs[i].fIsHandle ? "（ハンドル）" : "") + ": UI=" + diffs[i].fUiText +
				  " / SDK=" + diffs[i].fSdkText);
	if (diffs.empty())
		probe.log("  （違う行は 1 つも無かった——素性はオブジェクト変数の外にある）");

	// ---------------------------------------------------------------- 段 3
	probe.log("");
	probe.log("=== 段 3: ビューポート群（EViewportGroupType 1〜15）を突き合わせる ===");
	probe.log("  群: 1=クロップ 2=注釈 3=キャッシュ 4=**断面** 5〜9=各種キャッシュ");
	probe.log("      10=カメラ 11=詳細 12=内部立面 13=切断面の上書き 14/15=キャッシュ");
	for (short groupType = 1; groupType <= 15; ++groupType)
	{
		MCObjectHandle uiGroup = gSDK->GetViewportGroup(uiViewport, groupType);
		MCObjectHandle sdkGroup = gSDK->GetViewportGroup(sdkViewport, groupType);
		if (uiGroup == nil && sdkGroup == nil)
			continue;
		probe.log("  群 " + std::to_string(static_cast<int>(groupType)) + ": UI=" +
				  (uiGroup == nil ? std::string("無し")
								  : "有り(" + std::to_string(SvahCountMembers(uiGroup)) + " 個)") +
				  " / SDK=" +
				  (sdkGroup == nil
					   ? std::string("無し")
					   : "有り(" + std::to_string(SvahCountMembers(sdkGroup)) + " 個)"));
	}

	// ---------------------------------------------------------------- 段 4
	probe.log("");
	probe.log("=== 段 4: 差を丸ごと書き写して、注釈を読み直す ===");
	probe.log("  **ハンドル型は外す**（他のオブジェクトを指している変数は段 6 で別に試す）。");
	std::vector<SvahDiff> writable;
	for (size_t i = 0; i < diffs.size(); ++i)
		if (!diffs[i].fIsHandle)
			writable.push_back(diffs[i]);
	if (writable.empty())
	{
		probe.log("  書き写せる差が無いので、この段は行えない");
	}
	else
	{
		int wroteOk = 0;
		int tookEffect = 0;
		for (size_t i = 0; i < writable.size(); ++i)
		{
			const bool wrote =
				gSDK->SetObjectVariable(sdkViewport, writable[i].fIndex, writable[i].fUiValue)
					? true
					: false;
			TVariableBlock after;
			const bool readBack =
				gSDK->GetObjectVariable(sdkViewport, writable[i].fIndex, after) ? true : false;
			const std::string afterText = readBack ? SvahBlockText(after) : "(読めず)";
			if (wrote)
				++wroteOk;
			if (afterText == writable[i].fUiText)
				++tookEffect;
			probe.log("  " + std::to_string(static_cast<int>(writable[i].fIndex)) +
					  ": Set=" + SvahBool(wrote) + " 読み戻し=" + afterText +
					  (afterText == writable[i].fUiText ? "（入った）" : "（**入らなかった**）"));
		}
		probe.log("  書けた=" + std::to_string(wroteOk) + " / 入った=" +
				  std::to_string(tookEffect) + " / 試した=" + std::to_string(writable.size()));
		gSDK->UpdateViewport(sdkViewport);
		made.push_back(SvahPlaceMarker(probe, sdkViewport, targetStoryName, targetType,
									   "差を全部書き写した後の SDK 製の注釈"));
	}

	// ---------------------------------------------------------------- 段 5
	probe.log("");
	probe.log("=== 段 5: 変数を 1 つだけ書いたビューポートを作って、効いたものを分ける ===");
	probe.log("  段 4 で数値が出ていなければ、ここも全部 0 になるはず（それも情報になる）。");
	probe.log("  **ここで作るビューポートは表示レイヤを触らず更新もしない**——レイヤの表示は");
	probe.log("  条件でないと #141 で確定しており、実物件の断面を何枚も描き直さないため。");
	if (writable.empty())
	{
		probe.log("  書き写せる差が無いので、この段は行えない");
	}
	else
	{
		// 対照（何も書かないビューポート）。**これが 0 であることを先に見せる**
		// ——更新しない作りでも 0 になることを確かめないと、段 5 の 0 が読めない。
		MCObjectHandle controlSheet = gSDK->CreateLayer("#147 の対照シート", kLayerSheet);
		MCObjectHandle controlVp = nil;
		if (controlSheet != nil)
			controlVp = SvahMakeSectionViewport(controlSheet, WorldPt(-100000, -100000),
												WorldPt(100000, -100000), WorldPt(0, 100000), 0,
												-100000, 100000, false);
		if (controlVp == nil)
		{
			probe.log("  対照のビューポートを作れなかった");
			if (controlSheet != nil)
				made.push_back(controlSheet);
		}
		else
		{
			made.push_back(SvahPlaceMarker(probe, controlVp, targetStoryName, targetType,
										   "対照（何も書かない）"));
			made.push_back(controlVp);
			made.push_back(controlSheet);
		}

		const size_t limit =
			writable.size() < kSvahIsolateLimit ? writable.size() : kSvahIsolateLimit;
		if (limit < writable.size())
			probe.log("  差が " + std::to_string(writable.size()) + " 件あるので、先頭 " +
					  std::to_string(limit) + " 件だけ試す");
		for (size_t i = 0; i < limit; ++i)
		{
			const std::string sheetName =
				"#147 の単独シート " + std::to_string(static_cast<int>(writable[i].fIndex));
			MCObjectHandle sheetOne = gSDK->CreateLayer(TXString(sheetName.c_str()), kLayerSheet);
			if (sheetOne == nil)
			{
				probe.log("  " + std::to_string(static_cast<int>(writable[i].fIndex)) +
						  ": シートレイヤを作れなかった");
				continue;
			}
			MCObjectHandle vpOne = SvahMakeSectionViewport(
				sheetOne, WorldPt(-100000, -100000), WorldPt(100000, -100000), WorldPt(0, 100000),
				0, -100000, 100000, false);
			if (vpOne == nil)
			{
				probe.log("  " + std::to_string(static_cast<int>(writable[i].fIndex)) +
						  ": 断面ビューポートを作れなかった");
				made.push_back(sheetOne);
				continue;
			}
			const bool wrote =
				gSDK->SetObjectVariable(vpOne, writable[i].fIndex, writable[i].fUiValue) ? true
																						 : false;
			made.push_back(SvahPlaceMarker(probe, vpOne, targetStoryName, targetType,
										   std::to_string(static_cast<int>(writable[i].fIndex)) +
											   " だけ書いた（Set=" + SvahBool(wrote) +
											   " 値=" + writable[i].fUiText + "）"));
			made.push_back(vpOne);
			made.push_back(sheetOne);
		}
	}

	// ---------------------------------------------------------------- 段 6
	probe.log("");
	probe.log("=== 段 6: ハンドル型の差を、新しいビューポートへ書いて試す ===");
	probe.log("  UI 製が何かを指していて SDK 製が指していないなら、**それが断面線");
	probe.log("  オブジェクトとの結び付き**である見込みが高い（UI の断面ビューポートは");
	probe.log("  設計レイヤ上の断面線から作られる）。指し先は読むだけで書き換えない。");
	std::vector<SvahDiff> handles;
	for (size_t i = 0; i < diffs.size(); ++i)
		if (diffs[i].fIsHandle)
			handles.push_back(diffs[i]);
	if (handles.empty())
	{
		probe.log("  ハンドル型の差は 1 つも無かった（この筋は消える）");
	}
	else
	{
		for (size_t i = 0; i < handles.size(); ++i)
		{
			const std::string sheetName =
				"#147 のハンドルシート " + std::to_string(static_cast<int>(handles[i].fIndex));
			MCObjectHandle sheetH = gSDK->CreateLayer(TXString(sheetName.c_str()), kLayerSheet);
			if (sheetH == nil)
			{
				probe.log("  " + std::to_string(static_cast<int>(handles[i].fIndex)) +
						  ": シートレイヤを作れなかった");
				continue;
			}
			MCObjectHandle vpH =
				SvahMakeSectionViewport(sheetH, WorldPt(-100000, -100000), WorldPt(100000, -100000),
										WorldPt(0, 100000), 0, -100000, 100000, false);
			if (vpH == nil)
			{
				probe.log("  " + std::to_string(static_cast<int>(handles[i].fIndex)) +
						  ": 断面ビューポートを作れなかった");
				made.push_back(sheetH);
				continue;
			}
			const bool wrote =
				gSDK->SetObjectVariable(vpH, handles[i].fIndex, handles[i].fUiValue) ? true : false;
			made.push_back(SvahPlaceMarker(probe, vpH, targetStoryName, targetType,
										   std::to_string(static_cast<int>(handles[i].fIndex)) +
											   " のハンドルを書いた（Set=" + SvahBool(wrote) +
											   " 指し先=" + handles[i].fUiText + "）"));
			made.push_back(vpH);
			made.push_back(sheetH);
		}
	}

	// ---------------------------------------------------------------- 段 7
	probe.log("");
	probe.log("=== 段 7: 作り方を振る ===");
	probe.log("  `depth` は 0 が〈切断面より奥: 無限〉（Findings「ビューポート」）。0 以外に");
	probe.log("  したとき・高さの範囲を建物に合わせたとき・断面線の位置と向きを変えたときで");
	probe.log("  違いが出るかを見る。");
	struct SvahRecipe
	{
		const char* fTag;
		WorldPt fP1;
		WorldPt fP2;
		WorldPt fP3;
		double fDepth;
		double fStart;
		double fEnd;
	};
	const SvahRecipe recipes[] = {
		{"depth=3000（0 以外）", WorldPt(-100000, -100000), WorldPt(100000, -100000),
		 WorldPt(0, 100000), 3000, -100000, 100000},
		{"高さの範囲を建物に合わせる", WorldPt(-100000, -100000), WorldPt(100000, -100000),
		 WorldPt(0, 100000), 0, -1000, 12000},
		{"断面線を原点の近くに短く引く", WorldPt(-10000, 0), WorldPt(10000, 0), WorldPt(0, 10000),
		 0, -1000, 12000},
		{"断面線の向きを逆にする（pt3 を反対側へ）", WorldPt(-100000, -100000),
		 WorldPt(100000, -100000), WorldPt(0, -200000), 0, -100000, 100000},
	};
	for (size_t r = 0; r < sizeof(recipes) / sizeof(recipes[0]); ++r)
	{
		const std::string sheetName = "#147 の作り方シート " + std::to_string(r + 1);
		MCObjectHandle sheetR = gSDK->CreateLayer(TXString(sheetName.c_str()), kLayerSheet);
		if (sheetR == nil)
		{
			probe.log(std::string("  ") + recipes[r].fTag + ": シートレイヤを作れなかった");
			continue;
		}
		MCObjectHandle vpR =
			SvahMakeSectionViewport(sheetR, recipes[r].fP1, recipes[r].fP2, recipes[r].fP3,
									recipes[r].fDepth, recipes[r].fStart, recipes[r].fEnd, true);
		if (vpR == nil)
		{
			probe.log(std::string("  ") + recipes[r].fTag + ": 断面ビューポートを作れなかった");
			made.push_back(sheetR);
			continue;
		}
		probe.log(std::string("  ") + recipes[r].fTag + ": 断面VP=" +
				  SvahBool(SvahReadBool(vpR, 1054, false)) + " 表示レイヤ=" + SvahShownLayers(vpR));
		made.push_back(SvahPlaceMarker(probe, vpR, targetStoryName, targetType,
									   std::string("  → 注釈〈") + recipes[r].fTag + "〉"));
		made.push_back(vpR);
		made.push_back(sheetR);
	}

	// ---------------------------------------------------------------- 段 8
	probe.log("");
	probe.log("=== 段 8: UI 製を複製して使い回せるか（逃げ道）===");
	probe.log("  **元には触らない**（複製を作って読み、最後に消す）。複製で数値が出るなら、");
	probe.log("  「UI で 1 本作っておいて複製する」がプラグインから使える道になる。");
	MCObjectHandle copy = gSDK->DuplicateObject(uiViewport);
	if (copy == nil)
	{
		probe.log("  DuplicateObject が nil を返した（この段は行えない）");
	}
	else
	{
		gSDK->SetObjectName(copy, "#147 の複製");
		probe.log("  複製の素性: 断面VP=" + SvahBool(SvahReadBool(copy, 1054, false)) +
				  " 表示レイヤ=" + SvahShownLayers(copy));
		made.push_back(
			SvahPlaceMarker(probe, copy, targetStoryName, targetType, "8a 複製そのままの注釈"));

		// 8b: **表示レイヤを書き換えても素性が残るか。** プラグインは複製をそのまま
		// 使うのではなく、見せたいレイヤへ絞りたい——そこで壊れないかを見る。
		for (size_t i = 0; i < allLayers.size(); ++i)
			gSDK->SetViewportLayerVisibility(copy, allLayers[i], 0);
		gSDK->UpdateViewport(copy);
		probe.log("  8b の下ごしらえ: 全レイヤを表示にして更新した（表示レイヤ=" +
				  SvahShownLayers(copy) + "）");
		made.push_back(SvahPlaceMarker(probe, copy, targetStoryName, targetType,
									   "8b 表示レイヤを書き換えた後の注釈"));
		made.push_back(copy);
	}

	// ---------------------------------------------------------------- 片付け
	probe.log("");
	probe.log("=== 片付け ===");
	made.push_back(sdkViewport);
	made.push_back(sheet);
	const size_t madeCount = made.size();
	SvahDeleteAll(made);
	probe.log("  作った " + std::to_string(madeCount) + " 個を消した（UI 製の元は触っていない）");

	probe.log("");
	probe.log("読み方:");
	probe.log("  段 1 で UI 製＝" + targetZ + " / SDK 製＝0 が再現していることを先に確かめる。");
	probe.log("  段 2 の差の一覧が突き合わせの全部（1000〜1150 に差が無ければ、素性は");
	probe.log("  オブジェクト変数の外にある）。");
	probe.log("  段 4 で数値が出れば「差は書き写せる」——段 5 がどの 1 つかを言う。");
	probe.log("  段 6 で数値が出れば、断面線オブジェクトとの結び付きが素性だったことになる。");
	probe.log("  段 7 のどれかで数値が出れば、それが探していた作り方。");
	probe.log("  段 8 で数値が出れば、複製が逃げ道になる。");
}
