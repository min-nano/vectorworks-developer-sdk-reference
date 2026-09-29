//
//	probes/runtime/section-viewport-annotation-height/probe.cpp
//
//	[issue #147] **UI が作った断面ビューポートの注釈ではレベル基準線が高さを出すのに、
//	`ISDK::CreateSectionViewport` で作った断面ビューポートの注釈では `0` になる。その差は何か。**
//
//	#141 で「`0` になるかを決めているのはストーリではなくビューポート」まで確定した
//	（[Findings「レベル（標高）オブジェクト」](Findings/Level Objects.md)）。潰れている筋は
//	もう一度やらない——ストーリの作り方・レイヤの表示（書いて読み戻して確認済み）・
//	`ovIsSectionViewport`（SDK 製も `true`）・個体の全 33 欄（#135 で残差 0）。
//
//	だからこのプローブは**ビューポートの側だけ**を、次の 4 段で潰す。**どれも数値で読める**
//	（目視を頼まない）。
//
//	  1) **同じ 1 回の実行で差を再現する。** UI 製の断面ビューポートと、その場で作った
//	     SDK 製の断面ビューポートの注釈へ、同じレベルの 3 つ組を 1 本ずつ置いて読む。
//	     ここで差が出なければ、以降の比較には意味が無い（先に確かめる）。
//	  2) **素性を総当りで突き合わせる。** オブジェクト変数を 1000〜1150 まで 1 つずつ
//	     両方から読み、**値の違う行だけ**を並べる。当たりを付けて数個だけ見るのではなく
//	     総当りにするのは、**「どの変数が効くか」を知らないから**である（#135 で個体の側を
//	     全欄突き合わせたのと同じやり方）。`MCObjectHandle` 型の変数は、**指している先の
//	     種類と名前まで**出す——UI 製だけが何かを指しているなら、それが探していた差になる。
//	  3) **ビューポート群を突き合わせる。** `GetViewportGroup` を 1〜15（`EViewportGroupType`）
//	     まで引いて、群があるか・中に図形が何個あるかを両方で並べる。断面の素性を持って
//	     いるのは `kViewportGroupSection`(4) なので、SDK 製で空なら筋が通る。
//	  4) **作り方を振る／複製を試す。** `CreateSectionViewport` の引数（`depth`・
//	     `startHeight`・`endHeight`・断面線の位置）を 4 通り振って注釈を読む。そのうえで
//	     **UI 製を `DuplicateObject` で複製**して注釈を読む——複製で数値が出るなら、
//	     それが（素性ごと引き継ぐ）逃げ道になる。
//
//	【走らせる図面】**UI が作った断面ビューポートのある実物件の図面**（新規の空図面では
//	比較対象が無いので、段 0 で「行えない」と出して止まる）。**この調査は図面へ書き込む**
//	——シートレイヤ・断面ビューポート・レベル基準線を作って**最後に全部消す**が、
//	undo は開いていない（Findings「Undo」）ので、**走らせる前に保存**しておくこと。
//	UI が置いた図形には触らない（複製は作って消すだけで、元は読むだけ）。
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
	// 変数がここに居る見込みがあるため（Findings「ビューポート」の打ち切った調査で
	// 1060〜1090 は読み取り専用で触っており、落ちないことは分かっている）。
	const short kSvahVarFirst = 1000;
	const short kSvahVarLast = 1150;

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

	// オブジェクト変数を 1 つ読んで、**型と値を文字列に畳む**。型が分からないときも
	// 「型N（種類を読めない）」と出して、比較そのものは成立させる。
	// `TVariableBlock` の取り出し口は型ごとに分かれていて、型が違えば false を返すので、
	// **上から順に当ててゆけば型番号の一覧が無くても読める**（`ObjectVariables.h`）。
	std::string SvahVarValue(MCObjectHandle h, short index, bool& readable)
	{
		TVariableBlock var;
		readable = gSDK->GetObjectVariable(h, index, var) ? true : false;
		if (!readable)
			return "(読めず)";
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
			// **ここが本命のひとつ**——1055 / 1056 は断面の見え方（視線の向きと原点）を
			// 持っている。12 個の数をそのまま出して、UI 製と SDK 製で見比べる。
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
			// **UI 製だけが何かを指しているなら、それが探していた差。** 種類（型番号）と
			// 名前まで出す（`kParametricNode`=86 なら PIO＝断面線の見込み）。
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
				  "〉 Elev 欄=〈" + SvahStr(obj.GetParamValue("Elev")) + "〉");
		probe.log("    絵に出た文字: " + SvahDrawnTexts(marker));
		return marker;
	}

	// SDK 製の断面ビューポートを 1 本、**いまできる最善の作法で**作る
	// （Findings「ビューポート」——1064 を立て、隠線消去にし、全レイヤを表示にして更新）。
	MCObjectHandle SvahMakeSectionViewport(MCObjectHandle sheet, const WorldPt& pt1,
										   const WorldPt& pt2, const WorldPt& pt3, double depth,
										   double startHeight, double endHeight)
	{
		MCObjectHandle vp =
			gSDK->CreateSectionViewport(pt1, pt2, pt3, depth, startHeight, endHeight, sheet);
		if (vp == nil)
			return nil;
		TVariableBlock beyond;
		beyond = static_cast<Boolean>(true); // TVariableBlock に setter は無い（operator= で書く）
		gSDK->SetObjectVariable(vp, 1064, beyond);
		VWFC::VWObjects::VWViewportObj(vp).SetRenderType(renderFinalHiddenLine);
		const std::vector<MCObjectHandle> layers = SvahAllLayers();
		for (size_t i = 0; i < layers.size(); ++i)
			gSDK->SetViewportLayerVisibility(vp, layers[i], 0); // 0 = 表示
		gSDK->UpdateViewport(vp);
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
} // namespace

VW_PROBE("section-viewport-annotation-height",
		 "SDK 製の断面ビューポートの注釈で高さが 0 になる理由（#147）",
		 "UI 製と SDK 製の断面ビューポートを同じ文書で総当り比較し、作り方を振り、複製を試す")
{
	probe.log("**この調査は図面へ書き込む**（作ったものは最後に全部消すが、undo は開いて");
	probe.log("いない）。走らせる前に保存しておくこと。UI が置いた図形には触らない。");
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
	{
		bool readable = false;
		TVariableBlock var;
		bool isSection = false;
		if (gSDK->GetObjectVariable(viewports[i], 1054, var) && var.GetBoolean(isSection) &&
			isSection)
		{
			++sectionCount;
			if (uiViewport == nil)
				uiViewport = viewports[i];
		}
		(void)readable;
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
									WorldPt(0, 100000), 0, -100000, 100000);
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
	probe.log("**違う行だけ**を出す。落ちたときに何番で落ちたか分かるよう、10 件ごとに");
	probe.log("読み進めた印を残す。");
	int bothReadable = 0;
	int differing = 0;
	std::vector<std::string> diffs;
	for (short index = kSvahVarFirst; index <= kSvahVarLast; ++index)
	{
		bool uiReadable = false;
		bool sdkReadable = false;
		const std::string uiValue = SvahVarValue(uiViewport, index, uiReadable);
		const std::string sdkValue = SvahVarValue(sdkViewport, index, sdkReadable);
		if (uiReadable && sdkReadable)
			++bothReadable;
		if (!uiReadable && !sdkReadable)
			; // どちらでも読めない番号は黙って飛ばす（欠番）
		else if (uiValue != sdkValue)
		{
			++differing;
			diffs.push_back("  " + std::to_string(static_cast<int>(index)) + ": UI=" + uiValue +
							" / SDK=" + sdkValue);
		}
		if ((index - kSvahVarFirst) % 10 == 9)
			probe.log("  …" + std::to_string(static_cast<int>(index)) + " まで読んだ");
	}
	probe.log("両方で読めた番号=" + std::to_string(bothReadable) +
			  " / 値の違った番号=" + std::to_string(differing));
	for (size_t i = 0; i < diffs.size(); ++i)
		probe.log(diffs[i]);
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
	probe.log("=== 段 4: 作り方を振る ===");
	probe.log("  `depth` は 0 が〈切断面より奥: 無限〉（Findings「ビューポート」）。0 以外に");
	probe.log("  したとき・高さの範囲を建物に合わせたとき・断面線を建物の中に引いたときで");
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
		const std::string sheetName = "#147 の調査用シート " + std::to_string(r + 1);
		MCObjectHandle sheetR = gSDK->CreateLayer(TXString(sheetName.c_str()), kLayerSheet);
		if (sheetR == nil)
		{
			probe.log(std::string("  ") + recipes[r].fTag + ": シートレイヤを作れなかった");
			continue;
		}
		MCObjectHandle vpR =
			SvahMakeSectionViewport(sheetR, recipes[r].fP1, recipes[r].fP2, recipes[r].fP3,
									recipes[r].fDepth, recipes[r].fStart, recipes[r].fEnd);
		if (vpR == nil)
		{
			probe.log(std::string("  ") + recipes[r].fTag + ": 断面ビューポートを作れなかった");
			made.push_back(sheetR);
			continue;
		}
		bool isSection = false;
		TVariableBlock sectionVar;
		gSDK->GetObjectVariable(vpR, 1054, sectionVar);
		sectionVar.GetBoolean(isSection);
		probe.log(std::string("  ") + recipes[r].fTag + ": 断面VP=" + SvahBool(isSection) +
				  " 表示レイヤ=" + SvahShownLayers(vpR));
		made.push_back(SvahPlaceMarker(probe, vpR, targetStoryName, targetType,
									   std::string("  → 注釈〈") + recipes[r].fTag + "〉"));
		gSDK->UpdateViewport(vpR);
		made.push_back(vpR);
		made.push_back(sheetR);
	}

	// ---------------------------------------------------------------- 段 5
	probe.log("");
	probe.log("=== 段 5: UI 製を複製して使い回せるか（逃げ道）===");
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
		bool copyIsSection = false;
		TVariableBlock copyVar;
		gSDK->GetObjectVariable(copy, 1054, copyVar);
		copyVar.GetBoolean(copyIsSection);
		probe.log("  複製の素性: 断面VP=" + SvahBool(copyIsSection) +
				  " 表示レイヤ=" + SvahShownLayers(copy));
		made.push_back(
			SvahPlaceMarker(probe, copy, targetStoryName, targetType, "5a 複製そのままの注釈"));

		// 5b: **表示レイヤを書き換えても素性が残るか。** プラグインは複製をそのまま
		// 使うのではなく、見せたいレイヤへ絞りたい——そこで壊れないかを見る。
		for (size_t i = 0; i < allLayers.size(); ++i)
			gSDK->SetViewportLayerVisibility(copy, allLayers[i], 0);
		gSDK->UpdateViewport(copy);
		probe.log("  5b の下ごしらえ: 全レイヤを表示にして更新した（表示レイヤ=" +
				  SvahShownLayers(copy) + "）");
		made.push_back(SvahPlaceMarker(probe, copy, targetStoryName, targetType,
									   "5b 表示レイヤを書き換えた後の注釈"));
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
	probe.log("  段 2 に違う行があれば、それが素性の差——特に `1055` / `1056`（断面の");
	probe.log("  見え方の行列）と、ハンドルを持っている番号（UI 製だけが何かを指している");
	probe.log("  なら、それが断面線オブジェクトの見込み）。");
	probe.log("  段 3 で群 4（断面）が UI 製にだけあるなら、それも同じ筋の裏付けになる。");
	probe.log("  段 4 のどれかで数値が出れば、それが探していた作り方。");
	probe.log("  段 5 で数値が出れば、複製が逃げ道になる（出ないなら、素性はビューポートの");
	probe.log("  中ではなく、断面線オブジェクトとの結び付きに載っている）。");
}
