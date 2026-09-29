//
//	probes/runtime/drawing-label-layout/probe.cpp
//
//	[issue #149] 図面ラベル（`Drawing Label2`）を SDK から組み、ビューポートの図面
//	タイトルを出す手順を実測する。**第 2 回まで**（PR #150）で確定したこと:
//
//	  * レイアウトはラベル自身のプロファイルグループ。中身は
//	    〈テキスト'タイトル' / 線 / テキスト'縮尺' / 円弧 / テキスト'#'〉。
//	  * **そのテキストは `IDataTagTextLinkSupport` に対応していない**（式は空）。
//	  * **欄を決めているのは文字列ではなくテキストが持つ隠れた状態**——元のテキストを
//	    **複製**した 1 つだけの群を渡すと図面タイトルが出たが、同じ文字列から
//	    `CreateTextBlock` で作った版は文字どおり 'タイトル' としか出なかった。
//	    テキストには型 76（`kUserDataNode`）の補助オブジェクトが 2 つぶら下がっている。
//	  * 注釈へ入れたラベルは `Link State`=1 になり、ビューポートのタイトルの変更に
//	    追随した。シートレイヤ直下（`Link State`=0）は追随しなかった。
//
//	**残った問いはただ 1 つ「どのビューポートのタイトルが出るか」**——第 2 回では
//	VP-B の注釈へ入れたラベルまで VP-A のタイトルを出した。順番（直前の既定の
//	引き継ぎ）なのか、ホストのビューポートなのかが割れていない。そこでこの回は
//	**いちばん最初のラベルを VP-B の注釈へ置く**（＝引き継ぐ既定がまだ無い状態で
//	ホストが VP-B のラベルを作る）ことで、1 本で決める。
//
//	あわせて、実用上いちばん簡単な道
//	——**`SetParamValue("Title", …)` で直接書けるか**——も確かめる。
//
//	図面は壊す前提（新規の空図面で走らせる）。undo イベントは開かない。
//

#include "Probe.h"

#include <string>

namespace
{
	const char* const kDlTitleA = "アルファ図";
	const char* const kDlTitleB = "ベータ図";
	const char* const kDlTitleBRenamed = "ベータ図-書き換え後";
	const char* const kDlTitleWritten = "手で書いた図面名";

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
		case kArcNode:
			return "円弧";
		case kTextNode:
			return "テキスト";
		case kGroupNode:
			return "グループ";
		case kUserDataNode:
			return "ユーザーデータ";
		case kAuxMarkerNode:
			return "補助マーカー";
		case kParametricNode:
			return "PIO";
		default:
			return "その他";
		}
	}

	void DlDumpLinkFields(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hLabel)
	{
		if (hLabel == nil || gSDK->GetObjectTypeN(hLabel) != kParametricNode)
		{
			probe.log("  " + tag + ": PIO ではない");
			return;
		}
		VWParametricObj pio(hLabel);
		probe.log("  " + tag + " Title='" + DlToUtf8(pio.GetParamValue("Title")) + "' Drawing='" +
				  DlToUtf8(pio.GetParamValue("Drawing")) + "' Link State='" +
				  DlToUtf8(pio.GetParamValue("Link State")) + "'");
	}

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
		if (hLabel == nil)
			return;
		int count = 0;
		DlDumpDrawnText(probe, hLabel, gSDK->GetCustomObjectProfileGroup(hLabel), 0, count);
		if (count == 0)
			probe.log("  描画テキスト: 1 つも無い");
	}

	MCObjectHandle DlCreateLabelOnLayer(vwprobe::Report& probe, MCObjectHandle hLayer,
										const WorldPt& location, const std::string& what)
	{
		MCObjectHandle hLabel = gSDK->CreateCustomObject("Drawing Label2", location, 0.0, false);
		if (hLabel == nil || !gSDK->AddObjectToContainer(hLabel, hLayer))
		{
			probe.fail(what + ": シートレイヤへ置けなかった");
			return nil;
		}
		gSDK->ResetObject(hLabel);
		return hLabel;
	}

	MCObjectHandle DlCreateLabelInAnnotation(vwprobe::Report& probe, MCObjectHandle hViewport,
											 const WorldPt& location, const std::string& what)
	{
		MCObjectHandle hLabel = gSDK->CreateCustomObject("Drawing Label2", location, 0.0, false);
		if (hLabel == nil || !gSDK->AddViewportAnnotationObject(hViewport, hLabel))
		{
			probe.fail(what + ": 注釈へ置けなかった");
			return nil;
		}
		gSDK->ResetObject(hLabel);
		return hLabel;
	}

	MCObjectHandle DlCreateViewport(vwprobe::Report& probe, MCObjectHandle hSheet,
									MCObjectHandle hDesign, const char* title, WorldCoord dx,
									const std::string& what)
	{
		MCObjectHandle hViewport = gSDK->CreateViewport(hSheet);
		if (hViewport == nil)
		{
			probe.fail(what + ": CreateViewport が nil を返した");
			return nil;
		}
		gSDK->SetViewportLayerVisibility(hViewport, hDesign, 0);
		gSDK->SetObjectVariable(hViewport, 1032, TVariableBlock(TXString(title)));
		gSDK->MoveObject(hViewport, dx, 0);
		gSDK->UpdateViewport(hViewport);

		TVariableBlock block;
		TXString readBack;
		if (gSDK->GetObjectVariable(hViewport, 1032, block) && block.GetTXString(readBack))
			probe.log("  " + what + ": 1032 読み戻し='" + DlToUtf8(readBack) + "'");
		return hViewport;
	}

	// レイアウトの最初のテキスト（＝タイトル）を返す。
	MCObjectHandle DlFirstLayoutText(MCObjectHandle hLabel)
	{
		MCObjectHandle hGroup = gSDK->GetCustomObjectProfileGroup(hLabel);
		if (hGroup == nil)
			return nil;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hGroup); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) == kTextNode)
				return member;
		}
		return nil;
	}

	// n 番目のテキスト（0 始まり）。0=タイトル / 1=縮尺 / 2=図番。
	MCObjectHandle DlLayoutTextAt(MCObjectHandle hLabel, int wanted)
	{
		MCObjectHandle hGroup = gSDK->GetCustomObjectProfileGroup(hLabel);
		if (hGroup == nil)
			return nil;
		int seen = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hGroup); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) != kTextNode)
				continue;
			if (seen++ == wanted)
				return member;
		}
		return nil;
	}

	// ハンドルの生バイトを 16 進で出す（[Findings「タグ付きデータ」]の作法）。
	void DlDumpHandleBytes(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h,
						   size_t from, size_t howMany)
	{
		if (h == nil)
		{
			probe.log("  " + tag + ": nil");
			return;
		}
		size_t size = 0;
		gSDK->GSGetHandleSize(h, size);
		const char* base = *reinterpret_cast<char* const*>(h);
		probe.log("  " + tag + " 大きさ=" + std::to_string(size));
		if (base == nullptr)
			return;
		static const char* const kHexDigits = "0123456789abcdef";
		for (size_t offset = from; offset < from + howMany && offset < size; offset += 16)
		{
			std::string hex;
			std::string ascii;
			for (size_t i = offset; i < offset + 16 && i < size; ++i)
			{
				const unsigned char byte = static_cast<unsigned char>(base[i]);
				hex += kHexDigits[byte >> 4];
				hex += kHexDigits[byte & 0x0F];
				hex += ' ';
				ascii += (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
			}
			probe.log("    +" + std::to_string(offset) + " " + hex + "|" + ascii + "|");
		}
	}

	// テキストがぶら下げている補助オブジェクトを、生バイトごと出す。
	void DlDumpTextAux(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hText)
	{
		if (hText == nil)
		{
			probe.log("  " + tag + ": nil");
			return;
		}
		VWTextBlockObj text(hText);
		probe.log("  " + tag + " 文字='" + DlToUtf8(text.GetText()) + "'");
		int index = 0;
		for (MCObjectHandle aux = gSDK->FirstAuxObject(hText); aux != nil && index < 4;
			 aux = gSDK->NextObject(aux))
		{
			const short type = gSDK->GetObjectTypeN(aux);
			DlDumpHandleBytes(probe,
							  tag + " 補助[" + std::to_string(index++) +
								  "] 型=" + std::to_string(type) + "(" + DlNodeTypeName(type) + ")",
							  aux, 64, 64);
		}
		if (index == 0)
			probe.log("  " + tag + ": 補助オブジェクト無し");
	}
} // namespace

VW_PROBE("drawing-label-layout", "図面ラベル: どのビューポートのタイトルが出るか",
		 "いちばん最初のラベルを 2 枚目のビューポートの注釈へ置いて紐づきの決まりを 1 本で決め、"
		 "Title を直接書けるか・レイアウトの隠れた状態が文字より強いかを確かめる")
{
	gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);

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
	MCObjectHandle hVpA = DlCreateViewport(probe, hSheet, hDesign, kDlTitleA, 0, "VP-A");
	MCObjectHandle hVpB = DlCreateViewport(probe, hSheet, hDesign, kDlTitleB, 200000, "VP-B");
	if (hVpA == nil || hVpB == nil)
		return;

	// --- 決定打: いちばん最初のラベルを **2 枚目** の注釈へ ------------------
	probe.log("");
	probe.log("■ 実験 1: この図面でいちばん最初のラベルを VP-B の注釈へ置く");
	probe.log("  （'ベータ図' ならホストのビューポートから来る。'アルファ図' なら別の決まり）");
	MCObjectHandle hLabelB = DlCreateLabelInAnnotation(probe, hVpB, WorldPt(0, -2000), "実験 1");
	DlDumpLinkFields(probe, "実験 1", hLabelB);
	DlDumpDrawnText(probe, hLabelB);

	probe.log("");
	probe.log("■ 実験 2: VP-B のタイトルを書き換えて、実験 1 のラベルが追随するか");
	gSDK->SetObjectVariable(hVpB, 1032, TVariableBlock(TXString(kDlTitleBRenamed)));
	gSDK->UpdateViewport(hVpB);
	if (hLabelB != nil)
	{
		gSDK->ResetObject(hLabelB);
		DlDumpLinkFields(probe, "実験 2", hLabelB);
		DlDumpDrawnText(probe, hLabelB);
	}

	probe.log("");
	probe.log("■ 実験 3: 次のラベルを VP-A の注釈へ置く");
	probe.log("  （直前の既定は 'ベータ図…'。'アルファ図' が出ればホスト由来で確定）");
	MCObjectHandle hLabelA = DlCreateLabelInAnnotation(probe, hVpA, WorldPt(0, -2000), "実験 3");
	DlDumpLinkFields(probe, "実験 3", hLabelA);
	DlDumpDrawnText(probe, hLabelA);

	probe.log("");
	probe.log("■ 実験 4: シートレイヤ直下へ置く（どのビューポートにも属さない）");
	MCObjectHandle hLabelSheet = DlCreateLabelOnLayer(probe, hSheet, WorldPt(0, -50000), "実験 4");
	DlDumpLinkFields(probe, "実験 4", hLabelSheet);
	DlDumpDrawnText(probe, hLabelSheet);

	// --- 実用上いちばん簡単な道: Title を直接書く --------------------------
	probe.log("");
	probe.log("■ 実験 5: シートレイヤ直下のラベルへ Title を直接書く");
	if (hLabelSheet != nil)
	{
		VWParametricObj pio(hLabelSheet);
		pio.SetParamValue("Title", TXString(kDlTitleWritten));
		gSDK->ResetObject(hLabelSheet);
		DlDumpLinkFields(probe, "実験 5", hLabelSheet);
		DlDumpDrawnText(probe, hLabelSheet);
	}

	probe.log("");
	probe.log("■ 実験 6: 注釈のラベル（Link State=1）へ Title を直接書く");
	if (hLabelA != nil)
	{
		VWParametricObj pio(hLabelA);
		pio.SetParamValue("Title", TXString(kDlTitleWritten));
		gSDK->ResetObject(hLabelA);
		gSDK->UpdateViewport(hVpA);
		gSDK->ResetObject(hLabelA);
		DlDumpLinkFields(probe, "実験 6", hLabelA);
		DlDumpDrawnText(probe, hLabelA);
	}

	// --- 隠れた状態は文字より強いか ----------------------------------------
	probe.log("");
	probe.log("■ 実験 7: 複製したタイトルのテキストの文字を潰してから渡す");
	MCObjectHandle hLabel7 = DlCreateLabelInAnnotation(probe, hVpA, WorldPt(0, -6000), "実験 7");
	if (hLabel7 != nil)
	{
		MCObjectHandle hOldText = DlFirstLayoutText(hLabel7);
		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		MCObjectHandle hCopy = (hOldText != nil) ? gSDK->DuplicateObject(hOldText) : nil;
		if (hOldText == nil || hGroup == nil || hCopy == nil)
		{
			probe.fail("実験 7: 複製の用意ができなかった");
		}
		else
		{
			VWTextBlockObj copy(hCopy);
			copy.SetText(TXString("ここは無視されるはず"));
			gSDK->AddObjectToContainer(hCopy, hGroup);
			probe.log(std::string("  SetCustomObjectProfileGroup=") +
					  (gSDK->SetCustomObjectProfileGroup(hLabel7, hGroup) ? "true" : "false"));
			gSDK->ResetObject(hLabel7);
			gSDK->UpdateViewport(hVpA);
			MCObjectHandle hNowText = DlFirstLayoutText(hLabel7);
			if (hNowText != nil)
			{
				VWTextBlockObj now(hNowText);
				probe.log("  渡した後のレイアウトの文字='" + DlToUtf8(now.GetText()) + "'");
			}
			DlDumpDrawnText(probe, hLabel7);
		}
	}

	// --- 欄を決めている隠れた状態の中身 ------------------------------------
	probe.log("");
	probe.log("■ 実験 8: レイアウトのテキストが持つ補助オブジェクトの生バイト");
	MCObjectHandle hProbeLabel =
		DlCreateLabelInAnnotation(probe, hVpA, WorldPt(0, -10000), "実験 8");
	if (hProbeLabel != nil)
	{
		DlDumpTextAux(probe, "タイトル", DlLayoutTextAt(hProbeLabel, 0));
		DlDumpTextAux(probe, "図番", DlLayoutTextAt(hProbeLabel, 2));
	}

	probe.log("");
	probe.log("■ おわり");
}
