//
//	probes/runtime/level-marker-annotation-height/probe.cpp
//
//	[issue #135] 断面ビューポートの注釈に UI のツールで置いたレベル基準線
//	（`Elevation Benchmark2`）が、測定に使用する座標軸＝〈Z軸（3Dモード）〉のまま
//	`FL-1 612` のように「ストーリレベル名＋そのレベルの高さ」を出すのはなぜか。
//	SDK から同じ状態を作った個体は高さが `0` のままになる（Findings「レベル（標高）
//	オブジェクト」）。**その差がどこにあるのかを、同じ図面の中で突き合わせる。**
//
//	【このプローブは新規の空図面では答えが出ない】走らせる図面は
//	**UI のツールでレベル基準線を断面ビューポートの注釈へ置いたもの**（名前と高さが
//	両方出ているもの）。他のプローブと違って、**図面は読むだけ**で、UI が置いた個体には
//	一切書き込まない（`SetParamValue` も `ResetObject` も呼ばない）。比較のために
//	自分で作る個体（新規 1 本＋UI 製の複製 1 本）は、最後に消す。
//
//	【ログは PR コメントとして公開される】この調査だけは利用者の図面を読むので、
//	レイヤ名・ストーリ名・欄の値がそのまま載る。差し支えのある図面では走らせない
//	（UI でレベル基準線を 1 本置いた小さな図面でも同じことが確かめられる）。
//
//	確かめること:
//	  1) UI が置いた個体の**全欄**（universal 名・ローカライズ名・欄型・値）
//	  2) SDK から素で作った個体との**差のある欄だけの一覧**（＝埋めるべき欄の候補）
//	  3) `IMarkersPluginSupport::IsElevationBenchmarkConstrained` が UI 製で true か
//	  4) 補助オブジェクトの連鎖（オブジェクト変数 703）に何がぶら下がっているか
//	  5) **UI 製の全欄を写した個体が、同じ高さを出すか**（出れば「欄で足りる」、
//	     出なければ「欄の外に何かある」と分かれる）
//	  6) **UI 製を複製した個体が、複製後も高さを出すか**（出れば状態は個体に入っている）
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Extension/IMarkersPluginSupport.h"

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

	std::string LmJoinTexts(const std::vector<std::string>& texts)
	{
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

	std::string LmDrawnTexts(MCObjectHandle h)
	{
		std::vector<std::string> texts;
		LmCollectTexts(h, texts);
		return LmJoinTexts(texts);
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

	std::vector<MCObjectHandle>* gLmLayers = nullptr;

	void LmEachLayer(MCObjectHandle h, CallBackPtr /*cbp*/, void* /*env*/)
	{
		if (gLmLayers != nullptr && h != nil)
			gLmLayers->push_back(h);
	}

	// 欄を 1 行で出す。UI 製と SDK 製で同じ形にして、差を目で拾えるようにする。
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

	// 注目している欄だけを 1 行で（長い全欄ダンプの前後で見比べるため）。
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

	void LmDumpMarker(vwprobe::Report& probe, const LmFoundMarker& found, const std::string& tag)
	{
		probe.log("=== " + tag + " ===");
		probe.log("  場所: " + found.fWhere);
		probe.log("  PIO: " + found.fPioName);
		VWFC::VWObjects::VWParametricObj obj(found.fObject);
		probe.log("  内部 ID: " + std::to_string(static_cast<int>(obj.GetInternalID())));
		const VWPoint2D pos = obj.GetPointObjectPos();
		probe.log("  位置: x=" + std::to_string(pos.x) + " y=" + std::to_string(pos.y));
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
		LmDumpAuxChain(probe, found.fObject, "  ");
		LmDumpParams(probe, found.fObject, "  ");
	}
} // namespace

VW_PROBE("level-marker-annotation-height",
		 "UI が置いたレベル基準線と SDK 製の個体を、同じ図面で全欄突き合わせる",
		 "断面ビューポートの注釈で〈Z軸（3Dモード）〉のまま高さが出ている UI 製の個体を "
		 "探して全欄・拘束・補助オブジェクトを出し、SDK から作った個体との差を一覧にする。"
		 "**UI でレベル基準線を置いた図面で走らせる**（図面は読むだけ）")
{
	probe.log("この調査は図面を読む（UI が置いた個体には書き込まない）。");
	probe.log("比較のために作る個体（新規 1 本・複製 1 本）は最後に消す。");

	// --- 1: 文書の中のレベルオブジェクトを全部探す ---
	probe.log("--- 1: 文書の中のレベルオブジェクトを探す ---");

	std::vector<MCObjectHandle> layers;
	gLmLayers = &layers;
	gSDK->ForEachLayer(&LmEachLayer, nullptr);
	gLmLayers = nullptr;
	probe.log("レイヤ数: " + std::to_string(layers.size()));

	std::vector<LmFoundMarker> found;
	for (size_t i = 0; i < layers.size(); ++i)
	{
		const std::string where = "レイヤ〈" + LmObjectName(layers[i]) + "〉";
		LmWalkContainer(layers[i], nil, where, 0, found);
	}
	probe.log("自前の走査で見つかった件数: " + std::to_string(found.size()));

	// 検索条件（criteria）でも引いてみる——注釈の中まで届くかどうかは、それ自体が知見。
	size_t criteriaCount = 0;
	gSDK->ForEachObjectInCriteria(TXString("(PON='") + kLmBenchmark2 + "')",
								  [&criteriaCount](MCObjectHandle /*h*/) { ++criteriaCount; });
	probe.log("ForEachObjectInCriteria(\"(PON='Elevation Benchmark2')\") の件数: " +
			  std::to_string(criteriaCount));

	if (found.empty())
	{
		probe.fail("レベルオブジェクトが 1 つも見つからなかった。**UI のツールでレベル基準線を"
				   "断面ビューポートの注釈へ置いた図面**を開いて走らせてください");
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
	probe.log("  SDK 製（素のまま）: " + LmKeyFields(mine));
	probe.log("  SDK 製（素のまま）の文字: " + LmDrawnTexts(mine));
	probe.log("  SDK 製（素のまま）の拘束: " + LmConstrained(mine));
	LmDumpAuxChain(probe, mine, "  ");

	// 差のある欄だけを並べる——ここが「埋めるべき欄」の候補になる。
	probe.log("  --- UI 製と SDK 製（素のまま）で値が違う欄 ---");
	size_t diffCount = 0;
	for (size_t i = 0, count = from.GetParamsCount(); i < count; ++i)
	{
		const TXString name = from.GetParamName(i);
		const TXString uiValue = from.GetParamValue(name);
		const TXString sdkValue = to.GetParamValue(name);
		if (LmStr(uiValue) != LmStr(sdkValue))
		{
			++diffCount;
			probe.log("    " + LmStr(name) + "（" + LmStr(from.GetParamLocalizedName(i)) +
					  "）: UI 製=〈" + LmStr(uiValue) + "〉 SDK 製=〈" + LmStr(sdkValue) + "〉");
		}
	}
	probe.log("  差のある欄: " + std::to_string(diffCount) + " 件");

	// --- 4: UI 製の全欄を写したら、同じ高さが出るか ---
	probe.log("--- 4: UI 製の全欄を写す（欄型ごとの口で写す）---");
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
	probe.log("  写した後: " + LmKeyFields(mine));
	probe.log("  写した後の文字: " + LmDrawnTexts(mine));
	probe.log("  写した後の拘束: " + LmConstrained(mine));
	probe.log("  写した後に残った差:");
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

	// --- 5: UI 製を複製したら、複製も高さを出すか ---
	probe.log("--- 5: UI 製を複製する ---");
	MCObjectHandle dup = gSDK->DuplicateObject(target->fObject);
	if (dup == nil)
	{
		probe.log("  DuplicateObject が nil を返した");
	}
	else
	{
		if (!gSDK->AddViewportAnnotationObject(target->fViewport, dup))
			probe.log("  ※ 複製の AddViewportAnnotationObject が false を返した");
		probe.log("  複製（描き直す前）: " + LmKeyFields(dup));
		probe.log("  複製（描き直す前）の文字: " + LmDrawnTexts(dup));
		probe.log("  複製の拘束: " + LmConstrained(dup));
		LmDumpAuxChain(probe, dup, "  ");
		gSDK->ResetObject(dup);
		probe.log("  複製（描き直した後）: " + LmKeyFields(dup));
		probe.log("  複製（描き直した後）の文字: " + LmDrawnTexts(dup));
		probe.log("  複製（描き直した後）の拘束: " + LmConstrained(dup));
	}

	// --- 6: 片付け（自分で作ったものだけ消す）---
	probe.log("--- 6: 片付け ---");
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
