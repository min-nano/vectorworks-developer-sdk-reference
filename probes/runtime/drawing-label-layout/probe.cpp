//
//	probes/runtime/drawing-label-layout/probe.cpp
//
//	[issue #149] 図面ラベル（`Drawing Label2`）を SDK から組み、ビューポートの図面
//	タイトルを出す手順を実測する。**第 1 回の結果**（PR #150）で分かったこと:
//
//	  * ラベルレイアウトは**ラベル自身のプロファイルグループ**にある
//	    （`GetCustomObjectProfileGroup`。`…InAux` は nil）。中身は
//	    〈テキスト'タイトル'／線／テキスト'縮尺'／円弧／テキスト'#'〉。
//	  * **その中のテキストは `IDataTagTextLinkSupport` に対応していない**
//	    （`IsSupported`=no・式は空）。データタグとは別の仕組みで値が入っている。
//	  * シートレイヤ直下へ置いただけのラベルでも、パラメータ `Title` に
//	    ビューポートへ書いた図面タイトルが入っていた。
//	  * 注釈群は `GetViewportGroup` では nil だった（`AddViewportAnnotationObject`
//	    を使う。[Findings「レベルオブジェクト」]）。
//
//	そこでこの回は次を確かめる。
//
//	  ① **どのビューポートのタイトルが入るか。** 図面タイトルの違う 2 枚を同じ
//	     シートレイヤに並べ、それぞれの真下（シートレイヤ直下）と、それぞれの注釈に
//	     ラベルを置いて `Title` を見比べる
//	  ② **タイトルを書き換えたら追随するか**（ビューポートの 1032 を書き換えて再作成）
//	  ③ **レイアウトのテキストは何で「タイトル」だと決まっているか。**
//	     名前・補助オブジェクト（レコード）まで降りて全部出す
//	  ④ **レイアウトを組み直して図番を外せるか。** 「元のテキストを複製した 1 つだけ」
//	     の群と「同じ文字列から新しく作った 1 つだけ」の群を並べ、
//	     **中身が同じでも出るものが違うか**を見る（＝隠れた状態を持っているか）
//
//	図面は壊す前提（新規の空図面で走らせる）。undo イベントは開かない。
//

#include "Probe.h"

#include <string>

namespace
{
	// ビューポートへ書き込む図面タイトル。どちらが入ったか目で分かる綴りにする。
	const char* const kDlTitleA = "アルファ図";
	const char* const kDlTitleB = "ベータ図";
	const char* const kDlTitleARenamed = "アルファ図-書き換え後";

	std::string DlToUtf8(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	const char* DlNodeTypeName(short type)
	{
		switch (type)
		{
		case kTermNode:
			return "終端";
		case kLineNode:
			return "線";
		case kBoxNode:
			return "矩形";
		case kArcNode:
			return "円弧";
		case kTextNode:
			return "テキスト";
		case kGroupNode:
			return "グループ";
		case kLocusNode:
			return "ロクス";
		case kFormatNode:
			return "レコード形式";
		case kRecordNode:
			return "レコード";
		case kParametricNode:
			return "PIO";
		case kViewportNode:
			return "ビューポート";
		default:
			return "その他";
		}
	}

	std::string DlObjectName(MCObjectHandle h)
	{
		TXString name;
		gSDK->GetObjectName(h, name);
		return DlToUtf8(name);
	}

	// 付いているレコードを全部（名前＋全欄）出す。レイアウトのテキストが
	// 「どの欄を出すか」をレコードで持っているなら、ここに現れる。
	void DlDumpRecords(vwprobe::Report& probe, const std::string& indent, MCObjectHandle h)
	{
		int count = 0;
		for (MCObjectHandle rec = gSDK->FindAuxObject(h, kRecordNode); rec != nil && count < 8;
			 rec = gSDK->NextAuxObject(rec, kRecordNode))
		{
			++count;
			VWRecordObj record(rec);
			std::string line = indent + "レコード '" + DlToUtf8(record.GetRecordName()) + "'";
			const size_t fields = record.GetParamsCount();
			for (size_t i = 0; i < fields; ++i)
			{
				const TXString fieldName = record.GetParamName(i);
				line +=
					" | " + DlToUtf8(fieldName) + "=" + DlToUtf8(record.GetParamValue(fieldName));
			}
			probe.log(line);
		}
		if (count == 0)
			probe.log(indent + "レコード: 無し");
	}

	// 補助オブジェクトの並び（型だけ）。レコード以外に何がぶら下がっているかを見る。
	void DlDumpAuxTypes(vwprobe::Report& probe, const std::string& indent, MCObjectHandle h)
	{
		std::string line = indent + "補助オブジェクト:";
		int count = 0;
		for (MCObjectHandle aux = gSDK->FirstAuxObject(h); aux != nil && count < 12;
			 aux = gSDK->NextObject(aux))
		{
			const short type = gSDK->GetObjectTypeN(aux);
			line += " " + std::to_string(type) + "(" + DlNodeTypeName(type) + ")";
			++count;
		}
		if (count == 0)
			line += " 無し";
		probe.log(line);
	}

	// ラベルレイアウト（＝プロファイルグループ）の中身を、名前・レコードまで降りて出す。
	void DlDumpLayout(vwprobe::Report& probe, MCObjectHandle hGroup, bool deep)
	{
		if (hGroup == nil)
		{
			probe.log("  レイアウト: nil");
			return;
		}
		int index = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hGroup); member != nil;
			 member = gSDK->NextObject(member))
		{
			const short type = gSDK->GetObjectTypeN(member);
			std::string line = "  レイアウト[" + std::to_string(index++) +
							   "] 型=" + std::to_string(type) + "(" + DlNodeTypeName(type) +
							   ") 名前='" + DlObjectName(member) + "'";
			if (type == kTextNode)
			{
				VWTextBlockObj text(member);
				line += " 文字='" + DlToUtf8(text.GetText()) + "'";
			}
			probe.log(line);
			if (deep)
			{
				DlDumpAuxTypes(probe, "    ", member);
				DlDumpRecords(probe, "    ", member);
			}
		}
		if (index == 0)
			probe.log("  レイアウト: 空");
	}

	// ラベルが実際に描いた文字。**プロファイルグループは数えない**（第 1 回では
	// レイアウトのテキストが混ざって読みにくかった）。
	void DlDumpDrawnText(vwprobe::Report& probe, MCObjectHandle hContainer, MCObjectHandle hSkip,
						 int depth, int& count)
	{
		if (hContainer == nil || depth > 6)
			return;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hContainer); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (member == hSkip)
				continue;
			const short type = gSDK->GetObjectTypeN(member);
			if (type == kTextNode)
			{
				VWTextBlockObj text(member);
				probe.log("  描画テキスト[" + std::to_string(count++) + "] '" +
						  DlToUtf8(text.GetText()) + "'");
			}
			else if (type == kGroupNode || type == kParametricNode)
			{
				DlDumpDrawnText(probe, member, hSkip, depth + 1, count);
			}
		}
	}

	void DlDumpDrawnText(vwprobe::Report& probe, MCObjectHandle hLabel)
	{
		int count = 0;
		DlDumpDrawnText(probe, hLabel, gSDK->GetCustomObjectProfileGroup(hLabel), 0, count);
		if (count == 0)
			probe.log("  描画テキスト: 1 つも無い");
	}

	// ラベルの「どのビューポートに結びついたか」が読める欄だけを 1 行で出す。
	void DlDumpLinkFields(vwprobe::Report& probe, MCObjectHandle hLabel)
	{
		if (gSDK->GetObjectTypeN(hLabel) != kParametricNode)
		{
			probe.log("  欄: PIO ではない");
			return;
		}
		VWParametricObj pio(hLabel);
		probe.log("  Title='" + DlToUtf8(pio.GetParamValue("Title")) + "' Drawing='" +
				  DlToUtf8(pio.GetParamValue("Drawing")) + "' Sheet='" +
				  DlToUtf8(pio.GetParamValue("Sheet")) + "' BackRefSheetNo='" +
				  DlToUtf8(pio.GetParamValue("BackRefSheetNo")) + "' Link State='" +
				  DlToUtf8(pio.GetParamValue("Link State")) + "'");
	}

	void DlDumpAllParams(vwprobe::Report& probe, MCObjectHandle hLabel)
	{
		if (gSDK->GetObjectTypeN(hLabel) != kParametricNode)
			return;
		VWParametricObj pio(hLabel);
		const size_t count = pio.GetParamsCount();
		probe.log("  パラメータ " + std::to_string(count) +
				  " 件（universal 名 | 欄型 | 値 | 表示名）");
		for (size_t i = 0; i < count; ++i)
		{
			const TXString univName = pio.GetParamName(i);
			probe.log("    " + DlToUtf8(univName) + " | " +
					  std::to_string(static_cast<int>(pio.GetParamStyle(i))) + " | " +
					  DlToUtf8(pio.GetParamValue(univName)) + " | " +
					  DlToUtf8(pio.GetParamLocalizedName(i)));
		}
	}

	MCObjectHandle DlCreateLabelOnLayer(vwprobe::Report& probe, MCObjectHandle hLayer,
										const WorldPt& location, const std::string& what)
	{
		MCObjectHandle hLabel = gSDK->CreateCustomObject("Drawing Label2", location, 0.0, false);
		if (hLabel == nil)
		{
			probe.fail(what + ": CreateCustomObject が nil を返した");
			return nil;
		}
		if (!gSDK->AddObjectToContainer(hLabel, hLayer))
		{
			probe.fail(what + ": AddObjectToContainer が false を返した");
			return nil;
		}
		gSDK->ResetObject(hLabel);
		return hLabel;
	}

	MCObjectHandle DlCreateLabelInAnnotation(vwprobe::Report& probe, MCObjectHandle hViewport,
											 const WorldPt& location, const std::string& what)
	{
		MCObjectHandle hLabel = gSDK->CreateCustomObject("Drawing Label2", location, 0.0, false);
		if (hLabel == nil)
		{
			probe.fail(what + ": CreateCustomObject が nil を返した");
			return nil;
		}
		// 注釈へ入れる正しい口（GetViewportGroup は作りたてのビューポートでは nil）。
		if (!gSDK->AddViewportAnnotationObject(hViewport, hLabel))
		{
			probe.fail(what + ": AddViewportAnnotationObject が false を返した");
			return nil;
		}
		gSDK->ResetObject(hLabel);
		return hLabel;
	}

	// ビューポートを 1 枚作り、図面タイトルと位置を書いて更新する。
	MCObjectHandle DlCreateViewport(vwprobe::Report& probe, MCObjectHandle hSheet,
									MCObjectHandle hDesign, const char* title, const char* locator,
									WorldCoord dx, const std::string& what)
	{
		MCObjectHandle hViewport = gSDK->CreateViewport(hSheet);
		if (hViewport == nil)
		{
			probe.fail(what + ": CreateViewport が nil を返した");
			return nil;
		}
		gSDK->SetViewportLayerVisibility(hViewport, hDesign, 0); // 0 = 表示
		gSDK->SetObjectVariable(hViewport, 1032, TVariableBlock(TXString(title)));
		gSDK->SetObjectVariable(hViewport, 1033, TVariableBlock(TXString(locator)));
		gSDK->MoveObject(hViewport, dx, 0);
		gSDK->UpdateViewport(hViewport);

		TVariableBlock block;
		TXString readBack;
		if (gSDK->GetObjectVariable(hViewport, 1032, block) && block.GetTXString(readBack))
			probe.log("  " + what + ": 1032 読み戻し='" + DlToUtf8(readBack) + "'");
		else
			probe.log("  " + what + ": 1032 を読み戻せなかった");

		WorldRect bounds;
		if (gSDK->GetObjectBounds(hViewport, bounds))
			probe.log("  " + what + ": 外接 x=" + std::to_string(bounds.left) + "〜" +
					  std::to_string(bounds.right) + " y=" + std::to_string(bounds.bottom) + "〜" +
					  std::to_string(bounds.top));
		return hViewport;
	}

	// レイアウトを「テキスト 1 つだけ」の新しい群へ組み直す。
	// duplicate=true なら元のテキストを複製して使い、false なら同じ文字列から新しく作る。
	// 中身を入れてから渡す（Findings「データタグ」「レベルオブジェクト」）。
	void DlRebuildLayoutWithTitleOnly(vwprobe::Report& probe, MCObjectHandle hLabel, bool duplicate)
	{
		MCObjectHandle hOldGroup = gSDK->GetCustomObjectProfileGroup(hLabel);
		if (hOldGroup == nil)
		{
			probe.fail("組み直し: 元のレイアウトが nil");
			return;
		}
		// 元のレイアウトの最初のテキスト（＝タイトル）を探す。
		MCObjectHandle hOldText = nil;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hOldGroup); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) == kTextNode)
			{
				hOldText = member;
				break;
			}
		}
		if (hOldText == nil)
		{
			probe.fail("組み直し: 元のレイアウトにテキストが無い");
			return;
		}
		VWTextBlockObj oldText(hOldText);
		const TXString oldString = oldText.GetText();
		const std::string oldName = DlObjectName(hOldText);
		probe.log("  組み直し: 元のテキスト 文字='" + DlToUtf8(oldString) + "' 名前='" + oldName +
				  "'");

		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		if (hGroup == nil)
		{
			probe.fail("組み直し: CreateGroup が nil を返した");
			return;
		}
		MCObjectHandle hText = nil;
		if (duplicate)
		{
			hText = gSDK->DuplicateObject(hOldText);
			if (hText == nil)
			{
				probe.fail("組み直し: DuplicateObject が nil を返した");
				return;
			}
		}
		else
		{
			hText = gSDK->CreateTextBlock(oldString, WorldPt(0, 0), false, 0);
			if (hText == nil)
			{
				probe.fail("組み直し: CreateTextBlock が nil を返した");
				return;
			}
			if (!oldName.empty())
				gSDK->SetObjectName(hText, TXString(oldName.c_str()));
		}
		if (!gSDK->AddObjectToContainer(hText, hGroup))
			probe.log("  組み直し: AddObjectToContainer(text→group) が false");

		const Boolean setOk = gSDK->SetCustomObjectProfileGroup(hLabel, hGroup);
		probe.log(std::string("  組み直し: SetCustomObjectProfileGroup=") +
				  (setOk ? "true" : "false"));
		gSDK->ResetObject(hLabel);
	}
} // namespace

VW_PROBE("drawing-label-layout", "図面ラベルのラベルレイアウトと図面タイトル",
		 "図面タイトルの違うビューポート 2 枚で、ラベルがどちらのタイトルを出すかを決め、"
		 "レイアウトのテキストが何で「タイトル」だと決まっているかを名前・レコードまで降りて出し、"
		 "レイアウトを組み直して図番を外せるかを見る")
{
	// 「オブジェクトの設定」ダイアログで止まらないようにする（probes/runtime/README.md）。
	gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);

	// --- 図面を組む --------------------------------------------------------
	probe.log("■ 図面を組む");
	MCObjectHandle hDesign = gSDK->CreateLayer(TXString("調査-デザイン"), kLayerDesign);
	if (hDesign == nil)
	{
		probe.fail("CreateLayer(デザイン) が nil を返した");
		return;
	}
	gSDK->CreateRectangle(WorldRect(0, 3000, 5000, 0));

	MCObjectHandle hSheet = gSDK->CreateLayer(TXString("調査-シート"), kLayerSheet);
	if (hSheet == nil)
	{
		probe.fail("CreateLayer(シート) が nil を返した");
		return;
	}

	MCObjectHandle hVpA = DlCreateViewport(probe, hSheet, hDesign, kDlTitleA, "A-1", 0, "VP-A");
	MCObjectHandle hVpB =
		DlCreateViewport(probe, hSheet, hDesign, kDlTitleB, "B-2", 200000, "VP-B");
	if (hVpA == nil || hVpB == nil)
		return;

	// --- ① どのビューポートのタイトルが入るか ------------------------------
	probe.log("");
	probe.log("■ 実験 1: シートレイヤ直下・VP-A の真下");
	MCObjectHandle hLabel1 = DlCreateLabelOnLayer(probe, hSheet, WorldPt(0, -50000), "実験 1");
	if (hLabel1 != nil)
	{
		DlDumpLinkFields(probe, hLabel1);
		DlDumpDrawnText(probe, hLabel1);
	}

	probe.log("");
	probe.log("■ 実験 2: シートレイヤ直下・VP-B の真下");
	MCObjectHandle hLabel2 = DlCreateLabelOnLayer(probe, hSheet, WorldPt(200000, -50000), "実験 2");
	if (hLabel2 != nil)
	{
		DlDumpLinkFields(probe, hLabel2);
		DlDumpDrawnText(probe, hLabel2);
	}

	probe.log("");
	probe.log("■ 実験 3: VP-A の注釈");
	MCObjectHandle hLabel3 = DlCreateLabelInAnnotation(probe, hVpA, WorldPt(0, -2000), "実験 3");
	if (hLabel3 != nil)
	{
		gSDK->UpdateViewport(hVpA);
		gSDK->ResetObject(hLabel3);
		probe.log(std::string("  注釈の中にいるか=") +
				  (VWViewportObj::IsViewportGroupContainedObject(hLabel3, kViewportGroupAnnotation)
					   ? "yes"
					   : "no"));
		probe.log(
			std::string("  入れた後に GetViewportGroup=") +
			(gSDK->GetViewportGroup(hVpA, kViewportGroupAnnotation) != nil ? "取れた" : "nil"));
		DlDumpLinkFields(probe, hLabel3);
		DlDumpDrawnText(probe, hLabel3);
	}

	probe.log("");
	probe.log("■ 実験 4: VP-B の注釈");
	MCObjectHandle hLabel4 = DlCreateLabelInAnnotation(probe, hVpB, WorldPt(0, -2000), "実験 4");
	if (hLabel4 != nil)
	{
		gSDK->UpdateViewport(hVpB);
		gSDK->ResetObject(hLabel4);
		DlDumpLinkFields(probe, hLabel4);
		DlDumpDrawnText(probe, hLabel4);
	}

	// --- ③ レイアウトのテキストは何で決まっているか ------------------------
	probe.log("");
	probe.log("■ 実験 5: レイアウトの中身を名前・レコードまで降りて出す（実験 3 のラベル）");
	if (hLabel3 != nil)
	{
		DlDumpAllParams(probe, hLabel3);
		DlDumpLayout(probe, gSDK->GetCustomObjectProfileGroup(hLabel3), true);
	}

	// --- ② タイトルを書き換えたら追随するか --------------------------------
	probe.log("");
	probe.log("■ 実験 6: VP-A の 1032 を書き換えて、ラベルが追随するか");
	gSDK->SetObjectVariable(hVpA, 1032, TVariableBlock(TXString(kDlTitleARenamed)));
	gSDK->UpdateViewport(hVpA);
	if (hLabel3 != nil)
	{
		gSDK->ResetObject(hLabel3);
		probe.log("  注釈のラベル（ResetObject 後）");
		DlDumpLinkFields(probe, hLabel3);
		DlDumpDrawnText(probe, hLabel3);
	}
	if (hLabel1 != nil)
	{
		gSDK->ResetObject(hLabel1);
		probe.log("  シートレイヤ直下のラベル（ResetObject 後）");
		DlDumpLinkFields(probe, hLabel1);
		DlDumpDrawnText(probe, hLabel1);
	}

	// --- ④ レイアウトを組み直して図番を外せるか ----------------------------
	probe.log("");
	probe.log("■ 実験 7: レイアウトを「元のテキストを複製した 1 つだけ」に組み直す");
	MCObjectHandle hLabel7 = DlCreateLabelInAnnotation(probe, hVpA, WorldPt(0, -6000), "実験 7");
	if (hLabel7 != nil)
	{
		DlRebuildLayoutWithTitleOnly(probe, hLabel7, true);
		gSDK->UpdateViewport(hVpA);
		DlDumpLayout(probe, gSDK->GetCustomObjectProfileGroup(hLabel7), false);
		DlDumpDrawnText(probe, hLabel7);
	}

	probe.log("");
	probe.log("■ 実験 8: レイアウトを「同じ文字列から新しく作ったテキスト 1 つだけ」に組み直す");
	MCObjectHandle hLabel8 = DlCreateLabelInAnnotation(probe, hVpA, WorldPt(0, -10000), "実験 8");
	if (hLabel8 != nil)
	{
		DlRebuildLayoutWithTitleOnly(probe, hLabel8, false);
		gSDK->UpdateViewport(hVpA);
		DlDumpLayout(probe, gSDK->GetCustomObjectProfileGroup(hLabel8), false);
		DlDumpDrawnText(probe, hLabel8);
	}

	probe.log("");
	probe.log("■ おわり");
}
