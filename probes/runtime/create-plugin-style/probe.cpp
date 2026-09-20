//
//	probes/runtime/create-plugin-style/probe.cpp
//
//	[issue #98] `gSDK->CreatePluginStyle(hObj)` が何をする呼び出しなのかを実機で確かめる。
//
//	**これは 2 回目の版。** 1 回目（PR #99 のコメント）で次まで決着した:
//
//	  * 呼ぶと **PIO が全部ポリライン（型 21 = `kPolylineNode`）に作り替えられる**。
//	    渡した本も、渡していない本も、選択していない本も区別なく。PIO でない図形
//	    （型 5 の多角形）は**同じハンドルのまま無傷**。＝**選択は関係ない**。
//	  * **資源は 1 つも増えない。** 資源ツリーを丸ごと数えて 535 → 537 件、増えた 2 件は
//	    どちらもプローブ自身が作ったシンボル定義とその空レコード。プラグインスタイルは
//	    9 本のまま、`GetPluginStyleForTool` も `ref=0` のまま。
//	  * **シンボル定義のサブタイプ ＝ PIO の内部 ID** は裏付いた（構造材インスタンスの
//	    内部 ID 537 ＝ 文書にあった構造材スタイル 2 本の subType 537）。
//
//	**取りに行けば取れるのに取れなかったものが 3 つ**残ったので、この版で取りに行く。
//
//	  A ポリラインの中身が読めなかった。`VWPolygon2DObj::IsPolygon2DObject` は
//	    `kPolygonNode` / `kBoxNode` にしか true を返さず、型 21 は素通りしていた。
//	    → **`gSDK->CountVertices` / `gSDK->GetVertex` を直に使い、外接も併せて出す**。
//	    「PIO のパスが残ったのか、描かれていた輪郭なのか」をここで決める。
//	  B 置いた PIO が構造材だけだったので、**「同じ種別だけ」か「PIO 全部」か**が割れない。
//	    → **別種別の PIO を 1 つ混ぜる**。
//	  C ダイアログを通らない道（G3）が失敗した理由が曖昧だった。`AddObjectToContainer` は
//	    true を返したのに、最後にシンボル定義は空で、`SetSymbolDefSubType` の読み戻しは 0。
//	    「そもそも入らなかった」のか「入った後 `CreatePluginStyle` に抜かれた」のかが
//	    区別できていない。→ **1 手ごとに中身を数え、G3 の直後にも図面を撮る**。
//
//	**ダイアログが 1 回出る**（G5。「フォルダの指定」——文書の資源フォルダを選ばせる画面）。
//	フォルダを 1 つ選んで OK を押してください（キャンセルでも結果は残ります）。
//	**新規の空図面で走らせること**（図面は壊れます。それが調べたいことです）。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWPolygon2DObj.h"
#include "VWFC/VWObjects/VWSymbolDefObj.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kMemberTypeName = "StructuralMember";

	// 別種別の PIO（B）。上から順に作ってみて、最初に作れたものを使う。
	const char* const kOtherPioCandidates[] = {"Data Tag", "GridAxis", "Drawing Label2"};

	// 作る構造材のパス（両端の座標）。**後でポリラインの外接・頂点と突き合わせる**ので、
	// 1 本ごとに y を分けておく。
	const double kMemberStartX = 0.0;
	const double kMemberEndX = 3000.0;

	std::string Str(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string HandleText(MCObjectHandle h)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", static_cast<const void*>(h));
		return std::string(buf);
	}

	std::string Num(double value)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.3f", value);
		return std::string(buf);
	}

	// 型番号に SDK の名前を添える（`Objs.TDType.h` の定数。読み違えを防ぐため）。
	std::string TypeText(short type)
	{
		const char* name = "?";
		switch (type)
		{
		case kTermNode:
			name = "kTermNode/オブジェクトではない";
			break;
		case kGroupNode:
			name = "kGroupNode/グループ";
			break;
		case kSymDefNode:
			name = "kSymDefNode/シンボル定義";
			break;
		case kPolygonNode:
			name = "kPolygonNode/多角形";
			break;
		case kPolylineNode:
			name = "kPolylineNode/ポリライン";
			break;
		case kParametricNode:
			name = "kParametricNode/PIO";
			break;
		case kUndoPlaceholderNode:
			name = "kUndoPlaceholderNode/undo の置き石";
			break;
		case kFolderNode:
			name = "kFolderNode/フォルダ";
			break;
		case kSymbolNode:
			name = "kSymbolNode/シンボル";
			break;
		default:
			break;
		}
		return std::to_string(static_cast<int>(type)) + "(" + name + ")";
	}

	// ---------------------------------------------------------------- 形を読む

	// 外接。**どの本だったか**を y で言い当てるために出す（作った y は 0/1000/…）。
	std::string BoundsText(MCObjectHandle h)
	{
		WorldRect bounds;
		if (!gSDK->GetObjectBounds(h, bounds))
			return " 外接=読めず";
		return " 外接=[x " + Num(bounds.left) + "〜" + Num(bounds.right) + " / y " +
			   Num(bounds.bottom) + "〜" + Num(bounds.top) + "]";
	}

	// 頂点。**`VWPolygon2DObj` の判定は型 21 を弾く**ので、ISDK の口を直に叩く
	// （`CountVertices` / `GetVertex` は多角形にもポリラインにも効く）。
	std::string VertexText(MCObjectHandle h)
	{
		const short count = gSDK->CountVertices(h);
		if (count <= 0)
			return "";

		std::string text = " 頂点=" + std::to_string(static_cast<int>(count)) + " [";
		for (short index = 1; index <= count && index <= 10; ++index)
		{
			WorldPt pt;
			VertexType vertexType = vtCorner;
			WorldCoord arcRadius = 0;
			gSDK->GetVertex(h, index, pt, vertexType, arcRadius);
			text += " (" + Num(pt.x) + "," + Num(pt.y) + ")";
		}
		if (count > 10)
			text += " …";
		return text + " ]";
	}

	// ---------------------------------------------------------------- 図面の中身

	struct DrawingItem
	{
		MCObjectHandle handle = nil;
		short type = 0;
		std::string pioName;
		RefNumber styleRef = 0;
		bool selected = false;
		std::string shape;
	};

	std::vector<DrawingItem> DumpDrawing(MCObjectHandle container)
	{
		std::vector<DrawingItem> items;
		if (container == nil)
			return items;

		size_t guard = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil && guard < 200;
			 h = gSDK->NextObject(h))
		{
			++guard;
			DrawingItem item;
			item.handle = h;
			item.type = gSDK->GetObjectTypeN(h);
			item.selected = gSDK->IsSelected(h);
			if (VWParametricObj::IsParametricObject(h))
			{
				VWParametricObj obj(h);
				item.pioName = Str(obj.GetParametricName());
				item.styleRef = obj.GetStyleRefNumber();
			}
			if (item.type != kTermNode && item.type != kUndoPlaceholderNode)
				item.shape = BoundsText(h) + VertexText(h);
			items.push_back(item);
		}
		return items;
	}

	void LogDrawing(vwprobe::Report& probe, const std::string& tag,
					const std::vector<DrawingItem>& items)
	{
		probe.log(tag + " 図面のオブジェクト " + std::to_string(items.size()) + " 件");
		for (size_t index = 0; index < items.size(); ++index)
		{
			const DrawingItem& item = items[index];
			probe.log(tag + " [" + std::to_string(index) + "] " + HandleText(item.handle) + " 型=" +
					  TypeText(item.type) + (item.pioName.empty() ? "" : " PIO=" + item.pioName) +
					  " styleRef=" + std::to_string(static_cast<long>(item.styleRef)) +
					  (item.selected ? " **選択**" : " 非選択") + item.shape);
		}
	}

	// シンボル定義の中身を型番号で並べる（空の定義は型 0 のレコードを 1 つ持つ
	// ——Findings「シンボル」。**「非 nil だから入った」と読まないため**）。
	std::string MembersText(MCObjectHandle container)
	{
		std::string text;
		size_t guard = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil && guard < 30;
			 m = gSDK->NextObject(m))
		{
			++guard;
			text += " " + TypeText(gSDK->GetObjectTypeN(m));
			if (VWParametricObj::IsParametricObject(m))
				text += "=" + Str(VWParametricObj(m).GetParametricName());
		}
		return text.empty() ? " （空）" : text;
	}

	// ---------------------------------------------------------------- 資源ツリー

	struct ResourceItem
	{
		std::string path;
		short type = 0;
		bool isPluginStyle = false;
		RefNumber ref = 0;
		Sint32 subType = 0;
		std::string innerPio;
	};

	std::string InnerPioName(MCObjectHandle hSymDef)
	{
		size_t guard = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(hSymDef); m != nil && guard < 30;
			 m = gSDK->NextObject(m))
		{
			++guard;
			if (VWParametricObj::IsParametricObject(m))
				return Str(VWParametricObj(m).GetParametricName());
		}
		return "";
	}

	void CollectResources(MCObjectHandle container, const std::string& prefix, int depth,
						  size_t& visited, std::vector<ResourceItem>& out)
	{
		if (container == nil || depth > 6 || visited > 4000)
			return;

		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil && visited < 4000;
			 h = gSDK->NextObject(h))
		{
			++visited;
			TXString name;
			gSDK->GetObjectName(h, name);

			ResourceItem item;
			item.path = prefix + Str(name);
			item.type = gSDK->GetObjectTypeN(h);
			item.isPluginStyle = gSDK->IsPluginStyle(h);
			item.ref = gSDK->GetObjectInternalIndex(h);
			if (VWSymbolDefObj::IsSymbolDefObject(h))
			{
				item.subType = gSDK->GetSymbolDefSubType(h);
				item.innerPio = InnerPioName(h);
			}
			out.push_back(item);

			CollectResources(h, item.path + "/", depth + 1, visited, out);
		}
	}

	std::vector<ResourceItem> DumpResources(size_t& visited)
	{
		std::vector<ResourceItem> items;
		visited = 0;
		CollectResources(gSDK->GetSymbolLibraryHeader(), "", 0, visited, items);
		return items;
	}

	size_t CountPluginStyles(const std::vector<ResourceItem>& items)
	{
		size_t count = 0;
		for (size_t index = 0; index < items.size(); ++index)
			if (items[index].isPluginStyle)
				++count;
		return count;
	}

	void LogResourceDiff(vwprobe::Report& probe, const std::string& tag,
						 const std::vector<ResourceItem>& before,
						 const std::vector<ResourceItem>& after)
	{
		size_t added = 0;
		for (size_t a = 0; a < after.size(); ++a)
		{
			bool found = false;
			for (size_t b = 0; b < before.size(); ++b)
				if (before[b].path == after[a].path && before[b].ref == after[a].ref)
					found = true;
			if (found)
				continue;
			++added;
			probe.log(tag + " **増えた**: " + after[a].path + " 型=" + TypeText(after[a].type) +
					  " ref=" + std::to_string(static_cast<long>(after[a].ref)) +
					  " subType=" + std::to_string(static_cast<long>(after[a].subType)) +
					  " プラグインスタイル=" + (after[a].isPluginStyle ? "はい" : "いいえ") +
					  (after[a].innerPio.empty() ? "" : " 中の PIO=" + after[a].innerPio));
		}
		size_t removed = 0;
		for (size_t b = 0; b < before.size(); ++b)
		{
			bool found = false;
			for (size_t a = 0; a < after.size(); ++a)
				if (before[b].path == after[a].path && before[b].ref == after[a].ref)
					found = true;
			if (!found)
			{
				++removed;
				probe.log(tag + " **消えた**: " + before[b].path +
						  " ref=" + std::to_string(static_cast<long>(before[b].ref)));
			}
		}
		probe.log(tag + " 資源の差分: 増えた " + std::to_string(added) + " 件・消えた " +
				  std::to_string(removed) + " 件");
	}

	// ---------------------------------------------------------------- 現場を作る

	MCObjectHandle CreateMember(double offsetY)
	{
		VWPolygon2DObj path;
		path.AddVertex(kMemberStartX, offsetY);
		path.AddVertex(kMemberEndX, offsetY);
		return gSDK->CreateCustomObjectPath(kMemberTypeName, static_cast<MCObjectHandle>(path), nil,
											true);
	}

	std::string StyleForToolText()
	{
		RefNumber ref = 0;
		const bool ok = gSDK->GetPluginStyleForTool(kMemberTypeName, ref);
		return std::string(ok ? "true" : "false") +
			   " ref=" + std::to_string(static_cast<long>(ref));
	}
} // namespace

VW_PROBE("create-plugin-style", "CreatePluginStyle が何をするのかを確かめる（2 回目）",
		 "構造材 3 本と別種別の PIO 1 つと PIO でない多角形を置き、CreatePluginStyle を"
		 "1 度呼んで、できたポリラインの頂点・外接と資源ツリーの差分を採る"
		 "（**「フォルダの指定」ダイアログが 1 回出る**。新規の空図面で走らせること）")
{
	gSDK->DefineCustomObject(kMemberTypeName, kCustomObjectPrefNever);

	// -------------------------------------------------------------- G1 現場
	probe.log("[G1] 構造材 3 本・別種別の PIO 1 つ・PIO でない多角形 1 つを置く");

	MCObjectHandle memberPassed = CreateMember(0.0);	   // 渡す本（選択）
	MCObjectHandle memberUntouched = CreateMember(1000.0); // 渡さない・選択しない本
	MCObjectHandle memberSelected = CreateMember(2000.0);  // 渡さないが選択する本
	if (memberPassed == nil || memberUntouched == nil || memberSelected == nil)
	{
		probe.fail("CreateCustomObjectPath が nil を返した（StructuralMember を作れない）");
		return;
	}

	// 図面の器（レイヤ）は**呼ぶ前に**取っておく——呼んだ後はハンドルが当てにならない。
	MCObjectHandle layer = gSDK->ParentObject(memberPassed);
	if (layer == nil)
		layer = gSDK->GetActiveLayer();

	// B: 別種別の PIO。**「同じ種別だけ」か「PIO 全部」か**を割るために置く。
	MCObjectHandle otherPio = nil;
	std::string otherPioName;
	for (size_t index = 0; index < sizeof(kOtherPioCandidates) / sizeof(kOtherPioCandidates[0]);
		 ++index)
	{
		const char* const candidate = kOtherPioCandidates[index];
		// 設定ダイアログが出ないように、作る前に定義しておく
		// （Findings「生成時に『オブジェクトの設定』ダイアログが出る」）。
		gSDK->DefineCustomObject(candidate, kCustomObjectPrefNever);
		otherPio = gSDK->CreateCustomObject(candidate, WorldPt(6000.0, 0.0), 0.0, true);
		if (otherPio != nil)
		{
			otherPioName = candidate;
			break;
		}
	}
	probe.log("[G1] 別種別の PIO = " + (otherPio == nil
											? std::string("**作れなかった**（候補を全部試した）")
											: otherPioName + " " + HandleText(otherPio)));

	// PIO でない対照（型 5 の多角形）。
	VWPolygon2DObj control;
	control.AddVertex(kMemberStartX, 3000.0);
	control.AddVertex(kMemberEndX, 3000.0);
	const MCObjectHandle controlHandle = static_cast<MCObjectHandle>(control);
	if (controlHandle != nil && gSDK->ParentObject(controlHandle) == nil)
		gSDK->AddObjectToContainer(controlHandle, layer);

	gSDK->SelectObject(memberPassed, true);
	gSDK->SelectObject(memberSelected, true);
	gSDK->SelectObject(memberUntouched, false);
	gSDK->SelectObject(controlHandle, false);
	if (otherPio != nil)
		gSDK->SelectObject(otherPio, false);

	probe.log("[G1] 渡す本=" + HandleText(memberPassed) +
			  "（選択・y=0）"
			  " 渡さない本=" +
			  HandleText(memberUntouched) +
			  "（非選択・y=1000）"
			  " 選択だけの本=" +
			  HandleText(memberSelected) +
			  "（選択・y=2000）"
			  " PIO でない多角形=" +
			  HandleText(controlHandle) + "（非選択・y=3000）");
	probe.log("[G1] 構造材のパスは (0, y) 〜 (3000, y) の 2 頂点。**作り替えられたものの"
			  "頂点がこれと一致すればパスが残ったということ**");

	// -------------------------------------------------------------- G2 呼ぶ前
	probe.log("[G2] 呼ぶ前のスナップショット");
	const std::vector<DrawingItem> drawingBefore = DumpDrawing(layer);
	LogDrawing(probe, "[G2]", drawingBefore);

	size_t visitedBefore = 0;
	const std::vector<ResourceItem> resourcesBefore = DumpResources(visitedBefore);
	probe.log("[G2] 資源 " + std::to_string(resourcesBefore.size()) +
			  " 件（うちプラグインスタイル " + std::to_string(CountPluginStyles(resourcesBefore)) +
			  " 本）");
	for (size_t index = 0; index < resourcesBefore.size(); ++index)
	{
		const ResourceItem& item = resourcesBefore[index];
		if (!item.isPluginStyle)
			continue;
		probe.log("[G2] スタイル: " + item.path +
				  " ref=" + std::to_string(static_cast<long>(item.ref)) +
				  " subType=" + std::to_string(static_cast<long>(item.subType)) +
				  (item.innerPio.empty() ? "" : " 中の PIO=" + item.innerPio));
	}
	probe.log("[G2] GetPluginStyleForTool(\"StructuralMember\") = " + StyleForToolText());

	// -------------------------------------------------------------- G3 ダイアログを通らない道
	// **壊れる前に**試す。1 手ごとに読み戻して、どこで落ちるのかを名指しする（C）。
	probe.log("[G3] ダイアログを通らずにスタイルを作れるかを試す（1 手ごとに読み戻す）");

	const Sint32 memberSubType = static_cast<Sint32>(VWParametricObj::GetInternalID(memberPassed));
	Sint32 subTypeOfExistingStyle = 0;
	for (size_t index = 0; index < resourcesBefore.size(); ++index)
		if (resourcesBefore[index].innerPio == kMemberTypeName &&
			resourcesBefore[index].subType > 0)
			subTypeOfExistingStyle = resourcesBefore[index].subType;
	probe.log(
		"[G3] 構造材インスタンスの内部 ID = " + std::to_string(static_cast<long>(memberSubType)) +
		" / 文書にあった構造材スタイルの subType = " +
		std::to_string(static_cast<long>(subTypeOfExistingStyle)));

	MCObjectHandle hSymDef = nil;
	if (memberSubType > 0)
	{
		TXString newStyleName("VwSdkProbes 試作スタイル");
		hSymDef = gSDK->CreateSymbolDefinition(newStyleName);
		probe.log("[G3] ① CreateSymbolDefinition = " + HandleText(hSymDef) + " 名前=" +
				  Str(newStyleName) + " 中身:" + (hSymDef == nil ? " —" : MembersText(hSymDef)));
	}
	if (hSymDef != nil)
	{
		MCObjectHandle inner = CreateMember(4000.0);
		const bool moved = inner != nil && gSDK->AddObjectToContainer(inner, hSymDef);
		probe.log("[G3] ② AddObjectToContainer=" + std::string(moved ? "true" : "false") +
				  " 定義の中身:" + MembersText(hSymDef) + " / 入れた PIO の親=" +
				  HandleText(inner == nil ? nil : gSDK->ParentObject(inner)) +
				  "（定義=" + HandleText(hSymDef) + " と同じか）");

		gSDK->ResetObject(hSymDef);
		probe.log("[G3] ③ ResetObject の後 定義の中身:" + MembersText(hSymDef) +
				  BoundsText(hSymDef));

		// **ここが本命**——サブタイプ（＝PIO の内部 ID）を書いて読み戻す。
		gSDK->SetSymbolDefSubType(hSymDef, memberSubType);
		probe.log("[G3] ④ SetSymbolDefSubType(" + std::to_string(static_cast<long>(memberSubType)) +
				  ") → 読み戻し=" +
				  std::to_string(static_cast<long>(gSDK->GetSymbolDefSubType(hSymDef))) +
				  " **IsPluginStyle=" + (gSDK->IsPluginStyle(hSymDef) ? "true" : "false") + "**");

		gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);
		const RefNumber newRef = gSDK->GetObjectInternalIndex(hSymDef);
		probe.log("[G3] ⑤ SetAllPluginStyleParameters の後 subType=" +
				  std::to_string(static_cast<long>(gSDK->GetSymbolDefSubType(hSymDef))) +
				  " ref=" + std::to_string(static_cast<long>(newRef)) +
				  " **IsPluginStyle=" + (gSDK->IsPluginStyle(hSymDef) ? "true" : "false") + "**");

		MCObjectHandle trial = CreateMember(5000.0);
		if (trial != nil && newRef != 0)
		{
			VWParametricObj(trial).SetStyle(newRef);
			gSDK->ResetObject(trial);
			probe.log(
				"[G3] ⑥ 作ったものを新しい本へ当てた: styleRef=" +
				std::to_string(static_cast<long>(VWParametricObj(trial).GetStyleRefNumber())));
		}
	}
	else if (memberSubType > 0)
	{
		probe.log("[G3] シンボル定義を作れなかった（名前が使われている？）");
	}
	else
	{
		probe.log("[G3] 内部 ID が 0 だった（この道は試せない）");
	}

	// **G3 の直後にも図面を撮る**——「定義へ入れた本が図面から消えたか」を、
	// CreatePluginStyle を呼ぶ前に確かめておく（C）。
	probe.log("[G3] ここまでの図面（CreatePluginStyle を呼ぶ前）");
	const std::vector<DrawingItem> drawingAfterBuild = DumpDrawing(layer);
	LogDrawing(probe, "[G3]", drawingAfterBuild);

	// -------------------------------------------------------------- G4 本題
	probe.log("[G4] これから CreatePluginStyle を 1 度だけ呼ぶ。**「フォルダの指定」"
			  "ダイアログが出る**ので、フォルダを 1 つ選んで OK を押してください");
	probe.log("[G4] 渡すのは " + HandleText(memberPassed) + " だけ");

	gSDK->CreatePluginStyle(memberPassed);

	probe.log("[G4] 戻ってきた");

	// -------------------------------------------------------------- G5 呼んだ後
	probe.log("[G5] 呼んだ後のスナップショット");
	const std::vector<DrawingItem> drawingAfter = DumpDrawing(layer);
	LogDrawing(probe, "[G5]", drawingAfter);

	if (hSymDef != nil)
		probe.log("[G5] G3 で作ったシンボル定義の中身:" + MembersText(hSymDef) + " subType=" +
				  std::to_string(static_cast<long>(gSDK->GetSymbolDefSubType(hSymDef))));

	size_t visitedAfter = 0;
	const std::vector<ResourceItem> resourcesAfter = DumpResources(visitedAfter);
	probe.log("[G5] 資源 " + std::to_string(resourcesAfter.size()) +
			  " 件（うちプラグインスタイル " + std::to_string(CountPluginStyles(resourcesAfter)) +
			  " 本）");
	LogResourceDiff(probe, "[G5]", resourcesBefore, resourcesAfter);
	probe.log("[G5] GetPluginStyleForTool(\"StructuralMember\") = " + StyleForToolText());

	// 立場ごとの結末を 1 行ずつ。**ハンドルで突き合わせる**。
	const char* const kLabels[] = {"渡した本（選択）", "渡さない本（非選択）",
								   "選択だけの本（選択）", "別種別の PIO（非選択）",
								   "PIO でない多角形（非選択）"};
	MCObjectHandle targets[] = {memberPassed, memberUntouched, memberSelected, otherPio,
								controlHandle};
	for (size_t index = 0; index < 5; ++index)
	{
		if (targets[index] == nil)
		{
			probe.log("[G5] " + std::string(kLabels[index]) + ": 置けなかったので判定なし");
			continue;
		}
		bool stillThere = false;
		for (size_t a = 0; a < drawingAfter.size(); ++a)
			if (drawingAfter[a].handle == targets[index])
				stillThere = true;
		probe.log("[G5] " + std::string(kLabels[index]) + " " + HandleText(targets[index]) +
				  ": 呼ぶ前と同じハンドルが図面に**" +
				  (stillThere ? "ある（作り替えられていない）" : "無い（作り替えられた）") + "**");
	}

	probe.log("おわり");
}
