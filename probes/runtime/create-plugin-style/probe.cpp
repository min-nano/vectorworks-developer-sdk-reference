//
//	probes/runtime/create-plugin-style/probe.cpp
//
//	[issue #98] `gSDK->CreatePluginStyle(hObj)` が何をする呼び出しなのかを実機で確かめる。
//
//	issue #82 の調査で 4 度呼んで、次のことが実測で分かっている（VW 2026 / mac）:
//	「フォルダ選択」ダイアログが出る・戻ると渡したハンドルが無効になる・文書の
//	プラグインスタイルは 1 本も増えない・図面にあった構造材が型 86 から型 21 へ変わる
//	（**呼び出しに渡していない本まで**）。SDK のヘッダ検索で、型の正体までは分かった:
//
//	    kParametricNode = 86（PIO） / kPolylineNode = 21（ポリライン）
//	    kUndoPlaceholderNode = 90 / kTermNode = 0（オブジェクトではない）
//
//	つまり**構造材がポリラインになっている**——「PIO が消えてパスだけが残った」のか
//	「まったく別の何かが置かれた」のかは、頂点を読んで突き合わせれば機械で分かる。
//	そこでこのプローブは次の 4 つを、目視に頼らず**ログの数字だけで**決めにいく。
//
//	  Q1 型 21 になったものは何か（頂点数・頂点座標を、作ったときのパスと突き合わせる）
//	  Q2 変わるのはどれか（**渡した本 / 渡していない本 / 選択した本 / PIO でない図形**を
//	     並べて置き、どれが変わるかを見る）
//	  Q3 スタイルは本当に増えていないのか（**資源ツリーを丸ごと**——フォルダの中まで——
//	     呼ぶ前と後で数え、差分を出す）
//	  Q4 ダイアログを通らずに SDK だけでスタイルを作れるか
//	     （`CreateSymbolDefinition` ＋ `SetSymbolDefSubType` ＋ 中へ PIO ＋
//	      `SetAllPluginStyleParameters`。**Q4 は Q1〜Q3 より先に、壊れる前に走らせる**）
//
//	**ダイアログが 1 回出る**（G4）。利用者にはフォルダを 1 つ選んで OK を押して
//	もらう（キャンセルしてもよい——そのときも結果は残る）。新規の空図面で走らせること。
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

	// 作る構造材のパス（両端の座標）。**後でポリラインの頂点と突き合わせる**ので、
	// 1 本ごとに違う値にしておく。
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

	// 型番号に SDK の名前を添える（Objs.TDType.h の定数。読み違えを防ぐため）。
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
		default:
			break;
		}
		return std::to_string(static_cast<int>(type)) + "(" + name + ")";
	}

	// ---------------------------------------------------------------- 図面の中身

	// 図面に置かれたオブジェクト 1 つぶんの素性。**呼ぶ前と後で同じ形で採る**。
	struct DrawingItem
	{
		MCObjectHandle handle = nil;
		short type = 0;
		std::string pioName; // PIO のときだけ
		RefNumber styleRef = 0;
		bool selected = false;
		std::string vertices; // ポリラインのときだけ（頂点数と座標）
	};

	std::string DescribeVertices(MCObjectHandle h)
	{
		if (!VWPolygon2DObj::IsPolygon2DObject(h))
			return "";

		VWPolygon2DObj poly(h);
		const size_t count = poly.GetVertexCount();
		std::string text = " 頂点=" + std::to_string(count) + " [";
		for (size_t index = 0; index < count && index < 8; ++index)
		{
			const VWPoint2D pt = poly.GetVertexPoint(index);
			text += " (" + Num(pt.x) + "," + Num(pt.y) + ")";
		}
		return text + " ]";
	}

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
			item.vertices = DescribeVertices(h);
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
					  (item.selected ? " **選択**" : " 非選択") + item.vertices);
		}
	}

	// ---------------------------------------------------------------- 資源ツリー

	// 資源（シンボル定義・フォルダ）1 件ぶん。**プラグインスタイルだけでなく全部**
	// 並べる——「スタイルはフォルダの中へ入ったのでは」を潰すため。
	struct ResourceItem
	{
		std::string path; // フォルダを辿った名前（"親/子"）
		short type = 0;
		bool isPluginStyle = false;
		RefNumber ref = 0;
		Sint32 subType = 0; // シンボル定義のサブタイプ ＝ PIO の内部 ID（>0 でスタイル対応）
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

	// 資源ツリーの差分（パスで突き合わせる）。**増えた／消えたものを 1 行ずつ**出す。
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

VW_PROBE("create-plugin-style", "CreatePluginStyle が何をするのかを確かめる",
		 "構造材を 4 つの立場（渡す／渡さない／選択するだけ／PIO でない）で置き、"
		 "CreatePluginStyle を 1 度呼んで、図面と資源ツリーの差分を採る"
		 "（**フォルダ選択ダイアログが 1 回出る**。新規の空図面で走らせること）")
{
	gSDK->DefineCustomObject(kMemberTypeName, kCustomObjectPrefNever);

	// -------------------------------------------------------------- G1 現場
	probe.log("[G1] 構造材を 3 本と、PIO でないポリラインを 1 本置く");

	MCObjectHandle memberPassed = CreateMember(0.0);	   // 渡す本
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

	// PIO でない対照。**PIO だけが作り替えられるのか**を見るために置く。
	VWPolygon2DObj control;
	control.AddVertex(kMemberStartX, 3000.0);
	control.AddVertex(kMemberEndX, 3000.0);
	const MCObjectHandle controlHandle = static_cast<MCObjectHandle>(control);
	// 構築しただけで図面に入っていなければ、レイヤへ入れる（入っていれば何もしない）。
	if (controlHandle != nil && gSDK->ParentObject(controlHandle) == nil)
		gSDK->AddObjectToContainer(controlHandle, layer);

	// 選択は「渡す本」と「渡さないが選択する本」の 2 つだけ。
	gSDK->SelectObject(memberPassed, true);
	gSDK->SelectObject(memberSelected, true);
	gSDK->SelectObject(memberUntouched, false);
	gSDK->SelectObject(controlHandle, false);

	probe.log("[G1] 渡す本=" + HandleText(memberPassed) +
			  "（選択）"
			  " 渡さない本=" +
			  HandleText(memberUntouched) +
			  "（非選択）"
			  " 選択だけの本=" +
			  HandleText(memberSelected) +
			  "（選択）"
			  " PIO でないポリライン=" +
			  HandleText(controlHandle) + "（非選択）");
	probe.log("[G1] 作ったパスの両端は (" + Num(kMemberStartX) + ", y) 〜 (" + Num(kMemberEndX) +
			  ", y)。y は 0 / 1000 / 2000 / 3000");

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
	// **壊れる前に**試す。SDK だけでプラグインスタイルを組み立てられるか。
	probe.log("[G3] ダイアログを通らずにスタイルを作れるかを試す");

	// シンボル定義のサブタイプ ＝ その PIO の内部 ID（VWSymbolDefObj::HasPluginStyleSupport は
	// subType > 0 だけを見る。VWSymbolDefObj::PluginStyleObjectID はその値をそのまま返す）。
	// **内部 ID はインスタンスから取れる**（VWParametricObj::GetInternalID は static）。
	// 文書に元からある構造材用スタイルの subType とも突き合わせる——**一致すれば
	// 「サブタイプ＝PIO の内部 ID」が実測で裏付く**。
	const Sint32 memberSubType = static_cast<Sint32>(VWParametricObj::GetInternalID(memberPassed));
	Sint32 subTypeOfExistingStyle = 0;
	for (size_t index = 0; index < resourcesBefore.size(); ++index)
		if (resourcesBefore[index].innerPio == kMemberTypeName &&
			resourcesBefore[index].subType > 0)
			subTypeOfExistingStyle = resourcesBefore[index].subType;
	probe.log(
		"[G3] 構造材インスタンスの内部 ID = " + std::to_string(static_cast<long>(memberSubType)) +
		" / 文書にあった構造材スタイルの subType = " +
		std::to_string(static_cast<long>(subTypeOfExistingStyle)) + "（一致するか）");

	if (memberSubType > 0)
	{
		TXString newStyleName("VwSdkProbes 試作スタイル");
		MCObjectHandle hSymDef = gSDK->CreateSymbolDefinition(newStyleName);
		probe.log("[G3] CreateSymbolDefinition = " + HandleText(hSymDef) +
				  " 名前=" + Str(newStyleName));
		if (hSymDef != nil)
		{
			MCObjectHandle inner = CreateMember(4000.0);
			const bool moved = inner != nil && gSDK->AddObjectToContainer(inner, hSymDef);
			gSDK->SetSymbolDefSubType(hSymDef, memberSubType);
			gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);
			// 定義は中身を入れたら作り直す（Findings「シンボル」）。
			gSDK->ResetObject(hSymDef);

			const RefNumber newRef = gSDK->GetObjectInternalIndex(hSymDef);
			probe.log(
				"[G3] 中へ PIO を入れた=" + std::string(moved ? "true" : "false") + " subType=" +
				std::to_string(static_cast<long>(gSDK->GetSymbolDefSubType(hSymDef))) +
				" ref=" + std::to_string(static_cast<long>(newRef)) +
				" **IsPluginStyle=" + (gSDK->IsPluginStyle(hSymDef) ? "true" : "false") + "**");

			// 作ったものを本当に当てられるか（当たれば styleRef が 0 でなくなる）。
			MCObjectHandle trial = CreateMember(5000.0);
			if (trial != nil && newRef != 0)
			{
				VWParametricObj(trial).SetStyle(newRef);
				gSDK->ResetObject(trial);
				probe.log(
					"[G3] 作ったスタイルを新しい本へ当てた: styleRef=" +
					std::to_string(static_cast<long>(VWParametricObj(trial).GetStyleRefNumber())));
			}
		}
	}
	else
	{
		probe.log("[G3] 内部 ID が 0 だった（この道は試せない）");
	}

	// -------------------------------------------------------------- G4 本題
	probe.log("[G4] これから CreatePluginStyle を 1 度だけ呼ぶ。**ダイアログが出る**ので、"
			  "フォルダを 1 つ選んで OK を押してください（キャンセルでもよい）");
	probe.log("[G4] 渡すのは " + HandleText(memberPassed) + " だけ");

	gSDK->CreatePluginStyle(memberPassed);

	probe.log("[G4] 戻ってきた");

	// -------------------------------------------------------------- G5 呼んだ後
	probe.log("[G5] 呼んだ後のスナップショット");
	const std::vector<DrawingItem> drawingAfter = DumpDrawing(layer);
	LogDrawing(probe, "[G5]", drawingAfter);

	size_t visitedAfter = 0;
	const std::vector<ResourceItem> resourcesAfter = DumpResources(visitedAfter);
	probe.log("[G5] 資源 " + std::to_string(resourcesAfter.size()) +
			  " 件（うちプラグインスタイル " + std::to_string(CountPluginStyles(resourcesAfter)) +
			  " 本）");
	LogResourceDiff(probe, "[G5]", resourcesBefore, resourcesAfter);
	probe.log("[G5] GetPluginStyleForTool(\"StructuralMember\") = " + StyleForToolText());

	// 渡した本／渡さない本／選択だけの本／PIO でない図形が、それぞれどうなったか。
	// **ハンドルで突き合わせる**——同じハンドルが生きていれば作り替えではない。
	const char* const kLabels[] = {"渡した本", "渡さない本", "選択だけの本",
								   "PIO でないポリライン"};
	MCObjectHandle targets[] = {memberPassed, memberUntouched, memberSelected, controlHandle};
	for (size_t index = 0; index < 4; ++index)
	{
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
