//
//	probes/runtime/drawing-label-layout/probe.cpp
//
//	[issue #149] 図面ラベル（`Drawing Label2`）を SDK から作り、次の 4 つを実測する。
//
//	  ① パラメータの universal 名・欄型・値（図番や引出線の表示を切るのはどれか）
//	  ② ラベルレイアウトの在り処。データタグと同じプロファイルグループ
//	     （`GetCustomObjectProfileGroup` / `…InAux`）に入っているか。入っている
//	     テキストが `IDataTagTextLinkSupport` の式を持っているか
//	  ③ 図面タイトルがどう紐づくか。**ビューポートの注釈に置いたとき**と
//	     **シートレイヤ直下に置いたとき**で描かれる文字を見比べる
//	     （ビューポート側は `ovViewportDescription`＝1032。ヘッダに
//	      「対応する図面ラベルの Dwg Title 欄に対応する」とある）
//	  ④ ② で読み取った式で**自前のレイアウトを組み直せるか**。
//	     `UpdateUserDefinedTextsUIDs` を呼ぶ版と呼ばない版を並べる
//
//	図面は壊す前提（新規の空図面で走らせる）。undo イベントは開かない。
//

#include "Probe.h"

#include "VectorWorks/Extension/IDataTagSupport.h"

#include <string>

namespace
{
	using VectorWorks::Extension::IDataTagSupportPtr;
	using VectorWorks::Extension::IDataTagTextLinkSupportPtr;
	using VectorWorks::Extension::IID_DataTagSupport;
	using VectorWorks::Extension::IID_DataTagTextLinkSupport;

	// 図面タイトルとして書き込む文字列。描画テキストの中から目で拾えるよう、
	// 図面の既定値とぶつからない綴りにする。
	const char* const kDrawingLabelProbeTitle = "軸組図タイトル試験";
	const char* const kDrawingLabelProbeLocator = "試験位置";

	std::string DlToUtf8(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	const char* DlNodeTypeName(short type)
	{
		switch (type)
		{
		case 0:
			return "終端";
		case kTextNode:
			return "テキスト";
		case kGroupNode:
			return "グループ";
		case kLocusNode:
			return "ロクス";
		case kParametricNode:
			return "PIO";
		case kViewportNode:
			return "ビューポート";
		default:
			return "その他";
		}
	}

	// パラメータの一覧（universal 名・欄型・値・ローカライズ名）を全部出す。
	void DlDumpParams(vwprobe::Report& probe, MCObjectHandle hLabel)
	{
		if (gSDK->GetObjectTypeN(hLabel) != kParametricNode)
		{
			probe.log("  パラメータ: PIO ではない");
			return;
		}
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

	// プロファイルグループ（＝ラベルレイアウトの入れ物かどうかを確かめる先）の中身。
	// テキストには IDataTagTextLinkSupport の式が入っているはずなので、それも出す。
	void DlDumpLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hGroup)
	{
		if (hGroup == nil)
		{
			probe.log("  " + tag + ": nil");
			return;
		}
		IDataTagTextLinkSupportPtr textLink(IID_DataTagTextLinkSupport);
		if (!textLink)
			probe.log("  " + tag + ": IDataTagTextLinkSupport を取れなかった");

		int index = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hGroup); member != nil;
			 member = gSDK->NextObject(member))
		{
			const short type = gSDK->GetObjectTypeN(member);
			std::string line = "  " + tag + "[" + std::to_string(index++) +
							   "] 型=" + std::to_string(type) + "(" + DlNodeTypeName(type) + ")";
			if (type == kTextNode)
			{
				VWTextBlockObj text(member);
				line += " 文字='" + DlToUtf8(text.GetText()) + "'";
				if (textLink)
				{
					line += " 対応=" + std::string(textLink->IsSupported(member) ? "yes" : "no");
					line += " 連動=" + std::string(textLink->GetIsLinked(member) ? "yes" : "no");
					line += " 式='" + DlToUtf8(textLink->GetFormula(member)) + "'";
					line += " 既定値='" + DlToUtf8(textLink->GetDefaultValue(member)) + "'";
				}
			}
			probe.log(line);
		}
		if (index == 0)
			probe.log("  " + tag + ": 空");
	}

	// PIO が実際に描いた図形の中からテキストだけを拾う（＝画面に出ている文字）。
	void DlDumpDrawnText(vwprobe::Report& probe, MCObjectHandle hContainer, int depth, int& count)
	{
		if (hContainer == nil || depth > 6)
			return;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hContainer); member != nil;
			 member = gSDK->NextObject(member))
		{
			const short type = gSDK->GetObjectTypeN(member);
			if (type == kTextNode)
			{
				VWTextBlockObj text(member);
				probe.log("  描画テキスト[" + std::to_string(count++) + "] '" +
						  DlToUtf8(text.GetText()) + "'");
			}
			else if (type == kGroupNode || type == kParametricNode)
			{
				DlDumpDrawnText(probe, member, depth + 1, count);
			}
		}
	}

	void DlDumpDrawnText(vwprobe::Report& probe, MCObjectHandle hLabel)
	{
		int count = 0;
		DlDumpDrawnText(probe, hLabel, 0, count);
		if (count == 0)
			probe.log("  描画テキスト: 1 つも無い");
	}

	// 図面ラベルを 1 本作り、コンテナへ入れて作り直す。bInsert=false で作ってから
	// 入れる（アクティブレイヤへ落ちるのを避ける）。
	MCObjectHandle DlCreateLabel(vwprobe::Report& probe, MCObjectHandle hContainer,
								 const WorldPt& location, const std::string& what)
	{
		MCObjectHandle hLabel = gSDK->CreateCustomObject("Drawing Label2", location, 0.0, false);
		if (hLabel == nil)
		{
			probe.fail(what + ": CreateCustomObject(\"Drawing Label2\") が nil を返した");
			return nil;
		}
		if (!gSDK->AddObjectToContainer(hLabel, hContainer))
		{
			probe.fail(what + ": AddObjectToContainer が false を返した");
			return nil;
		}
		gSDK->ResetObject(hLabel);
		return hLabel;
	}

	// ラベルの現在のレイアウトを、同じ式を持つテキスト 1 つだけの自前グループへ
	// 組み直す（④）。戻り値は組み直しに使った式。
	std::string DlRebuildLayout(vwprobe::Report& probe, MCObjectHandle hLabel,
								const std::string& formula, bool callUpdateUIDs)
	{
		IDataTagTextLinkSupportPtr textLink(IID_DataTagTextLinkSupport);
		if (!textLink)
		{
			probe.fail("組み直し: IDataTagTextLinkSupport を取れなかった");
			return std::string();
		}

		// **中身を入れてから渡す**（Findings「データタグ」）。空のグループを先に渡すと
		// VW が複製した場合に後から足したテキストが迷子になる。
		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		if (hGroup == nil)
		{
			probe.fail("組み直し: CreateGroup が nil を返した");
			return std::string();
		}
		MCObjectHandle hText = gSDK->CreateTextBlock(TXString("組み直し"), WorldPt(0, 0), false, 0);
		if (hText == nil)
		{
			probe.fail("組み直し: CreateTextBlock が nil を返した");
			return std::string();
		}
		if (!gSDK->AddObjectToContainer(hText, hGroup))
			probe.log("  組み直し: AddObjectToContainer(text→group) が false");

		textLink->SetIsLinked(hText, true);
		textLink->SetFormula(hText, TXString(formula.c_str()), false);
		probe.log("  組み直し: 書いた式='" + formula + "' 読み戻し='" +
				  DlToUtf8(textLink->GetFormula(hText)) +
				  "' 連動=" + std::string(textLink->GetIsLinked(hText) ? "yes" : "no"));

		const Boolean setOk = gSDK->SetCustomObjectProfileGroup(hLabel, hGroup);
		probe.log(std::string("  組み直し: SetCustomObjectProfileGroup=") +
				  (setOk ? "true" : "false"));

		if (callUpdateUIDs)
		{
			IDataTagSupportPtr tagSupport(IID_DataTagSupport);
			if (tagSupport)
			{
				tagSupport->UpdateUserDefinedTextsUIDs(hLabel);
				probe.log("  組み直し: UpdateUserDefinedTextsUIDs を呼んだ");
			}
			else
			{
				probe.fail("組み直し: IDataTagSupport を取れなかった");
			}
		}
		else
		{
			probe.log("  組み直し: UpdateUserDefinedTextsUIDs は呼んでいない");
		}

		gSDK->ResetObject(hLabel);
		return formula;
	}

	// プロファイルグループの中の最初のテキストが持つ式を返す（④ の入力）。
	std::string DlFirstFormula(MCObjectHandle hGroup)
	{
		if (hGroup == nil)
			return std::string();
		IDataTagTextLinkSupportPtr textLink(IID_DataTagTextLinkSupport);
		if (!textLink)
			return std::string();
		for (MCObjectHandle member = gSDK->FirstMemberObj(hGroup); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) != kTextNode)
				continue;
			const std::string formula = DlToUtf8(textLink->GetFormula(member));
			if (!formula.empty())
				return formula;
		}
		return std::string();
	}
} // namespace

VW_PROBE("drawing-label-layout", "図面ラベルのラベルレイアウトと図面タイトル",
		 "Drawing Label2 のパラメータ・既定レイアウト・式を読み、ビューポートの注釈と"
		 "シートレイヤ直下で描かれる文字を見比べ、読み取った式で組み直せるかまで見る")
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
	// ビューポートに何か写るように 1 つ描く（アクティブレイヤ＝作ったばかりの層）。
	gSDK->CreateRectangle(WorldRect(0, 3000, 5000, 0));

	MCObjectHandle hSheet = gSDK->CreateLayer(TXString("調査-シート"), kLayerSheet);
	if (hSheet == nil)
	{
		probe.fail("CreateLayer(シート) が nil を返した");
		return;
	}

	MCObjectHandle hViewport = gSDK->CreateViewport(hSheet);
	if (hViewport == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	gSDK->SetViewportLayerVisibility(hViewport, hDesign, 0); // 0 = 表示

	// 図面タイトル（ovViewportDescription=1032）と位置（ovViewportLocator=1033）を書く。
	// ヘッダに「対応する図面ラベルの Dwg Title / Item 欄に対応する」とある。
	gSDK->SetObjectVariable(hViewport, 1032, TVariableBlock(TXString(kDrawingLabelProbeTitle)));
	gSDK->SetObjectVariable(hViewport, 1033, TVariableBlock(TXString(kDrawingLabelProbeLocator)));
	{
		TVariableBlock readBack;
		TXString title;
		if (gSDK->GetObjectVariable(hViewport, 1032, readBack) && readBack.GetTXString(title))
			probe.log("  ビューポート 1032（図面タイトル）読み戻し='" + DlToUtf8(title) + "'");
		else
			probe.log("  ビューポート 1032 を読み戻せなかった");

		TVariableBlock readBack2;
		TXString locator;
		if (gSDK->GetObjectVariable(hViewport, 1033, readBack2) && readBack2.GetTXString(locator))
			probe.log("  ビューポート 1033（位置）読み戻し='" + DlToUtf8(locator) + "'");
		else
			probe.log("  ビューポート 1033 を読み戻せなかった");
	}
	gSDK->UpdateViewport(hViewport);

	MCObjectHandle hAnnotation = gSDK->GetViewportGroup(hViewport, kViewportGroupAnnotation);
	probe.log(std::string("  注釈群=") + (hAnnotation != nil ? "取れた" : "nil"));

	// --- ① / ② シートレイヤ直下の素の図面ラベル ---------------------------
	probe.log("");
	probe.log("■ 実験 1: シートレイヤ直下に置いた素の図面ラベル");
	MCObjectHandle hLabelSheet = DlCreateLabel(probe, hSheet, WorldPt(0, -2000), "実験 1");
	if (hLabelSheet != nil)
	{
		DlDumpParams(probe, hLabelSheet);
		DlDumpLayout(probe, "プロファイルグループ", gSDK->GetCustomObjectProfileGroup(hLabelSheet));
		DlDumpLayout(probe, "プロファイルグループ(InAux)",
					 gSDK->GetCustomObjectProfileGroupInAux(hLabelSheet));
		DlDumpDrawnText(probe, hLabelSheet);
	}

	// --- ③ ビューポートの注釈に置いた図面ラベル ----------------------------
	probe.log("");
	probe.log("■ 実験 2: ビューポートの注釈に置いた図面ラベル");
	MCObjectHandle hLabelAnno = nil;
	if (hAnnotation == nil)
	{
		probe.fail("実験 2: 注釈群が取れなかったので置けない");
	}
	else
	{
		hLabelAnno = DlCreateLabel(probe, hAnnotation, WorldPt(0, -2000), "実験 2");
		if (hLabelAnno != nil)
		{
			gSDK->UpdateViewport(hViewport);
			gSDK->ResetObject(hLabelAnno);
			DlDumpParams(probe, hLabelAnno);
			DlDumpLayout(probe, "プロファイルグループ",
						 gSDK->GetCustomObjectProfileGroup(hLabelAnno));
			DlDumpDrawnText(probe, hLabelAnno);
			probe.log(
				std::string("  注釈の中にいるか=") +
				(VWViewportObj::IsViewportGroupContainedObject(hLabelAnno, kViewportGroupAnnotation)
					 ? "yes"
					 : "no"));
		}
	}

	// --- ④ 読み取った式で組み直す ------------------------------------------
	probe.log("");
	probe.log("■ 実験 3 / 4: 既定レイアウトから読んだ式で組み直す");
	const std::string formula =
		DlFirstFormula(hLabelAnno != nil ? gSDK->GetCustomObjectProfileGroup(hLabelAnno)
										 : gSDK->GetCustomObjectProfileGroup(hLabelSheet));
	if (formula.empty())
	{
		probe.fail("既定レイアウトから式を 1 つも読めなかったので、組み直しは試せない");
	}
	else if (hAnnotation != nil)
	{
		probe.log("  読み取った式='" + formula + "'");

		probe.log("  -- 実験 3: UpdateUserDefinedTextsUIDs を呼ばない --");
		MCObjectHandle hLabelNoUids =
			DlCreateLabel(probe, hAnnotation, WorldPt(6000, -2000), "実験 3");
		if (hLabelNoUids != nil)
		{
			DlRebuildLayout(probe, hLabelNoUids, formula, false);
			gSDK->UpdateViewport(hViewport);
			DlDumpLayout(probe, "組み直し後のプロファイルグループ",
						 gSDK->GetCustomObjectProfileGroup(hLabelNoUids));
			DlDumpDrawnText(probe, hLabelNoUids);
		}

		probe.log("  -- 実験 4: UpdateUserDefinedTextsUIDs を呼ぶ --");
		MCObjectHandle hLabelUids =
			DlCreateLabel(probe, hAnnotation, WorldPt(12000, -2000), "実験 4");
		if (hLabelUids != nil)
		{
			DlRebuildLayout(probe, hLabelUids, formula, true);
			gSDK->UpdateViewport(hViewport);
			DlDumpLayout(probe, "組み直し後のプロファイルグループ",
						 gSDK->GetCustomObjectProfileGroup(hLabelUids));
			DlDumpDrawnText(probe, hLabelUids);
		}
	}

	probe.log("");
	probe.log("■ おわり");
}
