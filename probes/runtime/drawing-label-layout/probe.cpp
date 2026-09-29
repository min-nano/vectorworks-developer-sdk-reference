//
//	probes/runtime/drawing-label-layout/probe.cpp
//
//	[issue #149] 図面ラベル（`Drawing Label2`）のラベルレイアウト。**第 4 回まで**
//	（PR #150）で次が確定した。
//
//	  * 式の綴りは**データタグと同じ `#レコード#.#フィールド#`**。レイアウトの
//	    3 つのテキストが持っていたのは `#Drawing Label2#.#Title#` /
//	    `縮尺: #Drawing Label2#.#Scale#` / `#Drawing Label2#.#Drawing#`。
//	  * ただし**保存先が違う**——式はテキストにぶら下がるユーザーデータ（型 76）に
//	    UTF-16 で入っており、`IDataTagTextLinkSupport` では読み書きできない
//	    （`IsSupported`=no）。だから**新しく作ったテキストに式は持たせられず、
//	    既定レイアウトのテキストを複製するしかない**。
//	  * `SetParamValue("Title", …)` は `Link State` が 0 でも 1 でも効く。
//	  * 注釈へ入れたラベルはホストのビューポートにリンクし、ビューポートの
//	    `ovViewportDescription`(1032) の変更に追随する。
//
//	**残りは 2 つだけ**（どちらも取りに行けば取れるので、未確認のままにしない）。
//
//	  ① **欲しいものを複数複製したレイアウトでも成り立つか。** 第 3 回で確かめたのは
//	     「タイトルのテキスト 1 つだけ」。**タイトル＋下線の 2 つ**を複製して渡し、
//	     図番（テキストと丸）だけが消えるかを見る。
//	  ② **手で書いた `Title` は、後からビューポートのタイトルを変えると上書きされるか。**
//	     リンクは生きているので押し込まれるはずだが、実測していない。
//
//	図面は壊す前提（新規の空図面で走らせる）。undo イベントは開かない。
//

#include "Probe.h"

#include <string>

namespace
{
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
		default:
			return "その他";
		}
	}

	// ラベルが実際に描いた図形を、型と（テキストなら）文字ごと出す。
	// プロファイルグループ（＝レイアウト）は数えない。
	void DlDumpDrawn(vwprobe::Report& probe, MCObjectHandle hContainer, MCObjectHandle hSkip,
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
			if (type == kGroupNode)
			{
				DlDumpDrawn(probe, member, hSkip, depth + 1, count);
				continue;
			}
			if (type == kTermNode)
				continue;
			std::string line = "  描画[" + std::to_string(count++) +
							   "] 型=" + std::to_string(type) + "(" + DlNodeTypeName(type) + ")";
			if (type == kTextNode)
			{
				VWTextBlockObj text(member);
				line += " '" + DlToUtf8(text.GetText()) + "'";
			}
			probe.log(line);
		}
	}

	void DlDumpDrawn(vwprobe::Report& probe, MCObjectHandle hLabel)
	{
		if (hLabel == nil)
			return;
		int count = 0;
		DlDumpDrawn(probe, hLabel, gSDK->GetCustomObjectProfileGroup(hLabel), 0, count);
		if (count == 0)
			probe.log("  描画: 1 つも無い");
	}

	void DlDumpLayout(vwprobe::Report& probe, MCObjectHandle hLabel)
	{
		MCObjectHandle hGroup = gSDK->GetCustomObjectProfileGroup(hLabel);
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
							   "] 型=" + std::to_string(type) + "(" + DlNodeTypeName(type) + ")";
			if (type == kTextNode)
			{
				VWTextBlockObj text(member);
				line += " '" + DlToUtf8(text.GetText()) + "'";
			}
			probe.log(line);
		}
	}

	void DlDumpTitle(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hLabel)
	{
		if (hLabel == nil || gSDK->GetObjectTypeN(hLabel) != kParametricNode)
		{
			probe.log("  " + tag + ": PIO ではない");
			return;
		}
		VWParametricObj pio(hLabel);
		probe.log("  " + tag + " Title='" + DlToUtf8(pio.GetParamValue("Title")) +
				  "' Link State='" + DlToUtf8(pio.GetParamValue("Link State")) + "'");
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
} // namespace

VW_PROBE("drawing-label-layout", "図面ラベル: 残り 2 つ（複数の複製・Title の上書き）",
		 "タイトル＋下線の 2 つを複製したレイアウトで図番だけが消えるか、"
		 "手で書いた Title が後からのビューポートのタイトル変更で上書きされるかを確かめる")
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
	MCObjectHandle hViewport = gSDK->CreateViewport(hSheet);
	if (hViewport == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	gSDK->SetViewportLayerVisibility(hViewport, hDesign, 0);
	gSDK->SetObjectVariable(hViewport, 1032, TVariableBlock(TXString("最初の題")));
	gSDK->UpdateViewport(hViewport);

	// --- ② 手で書いた Title は後から上書きされるか -------------------------
	probe.log("");
	probe.log(
		"■ 実験 1: 手で書いた Title は、後からビューポートのタイトルを変えると上書きされるか");
	MCObjectHandle hLabel1 =
		DlCreateLabelInAnnotation(probe, hViewport, WorldPt(0, -2000), "実験 1");
	if (hLabel1 != nil)
	{
		VWParametricObj pio(hLabel1);
		pio.SetParamValue("Title", TXString("手で書いた題"));
		gSDK->ResetObject(hLabel1);
		DlDumpTitle(probe, "書いた直後", hLabel1);
		DlDumpDrawn(probe, hLabel1);

		probe.log("  → ビューポートの 1032 を '後から変えた題' にする");
		gSDK->SetObjectVariable(hViewport, 1032, TVariableBlock(TXString("後から変えた題")));
		gSDK->UpdateViewport(hViewport);
		gSDK->ResetObject(hLabel1);
		DlDumpTitle(probe, "変えた後", hLabel1);
		DlDumpDrawn(probe, hLabel1);
	}

	// --- ① 欲しいものを複数複製したレイアウト ------------------------------
	probe.log("");
	probe.log("■ 実験 2: タイトルのテキストと下線の 2 つを複製したレイアウトを渡す");
	MCObjectHandle hLabel2 =
		DlCreateLabelInAnnotation(probe, hViewport, WorldPt(0, -6000), "実験 2");
	if (hLabel2 != nil)
	{
		probe.log("  渡す前のレイアウト:");
		DlDumpLayout(probe, hLabel2);

		MCObjectHandle hOld = gSDK->GetCustomObjectProfileGroup(hLabel2);
		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		if (hOld == nil || hGroup == nil)
		{
			probe.fail("実験 2: 複製の用意ができなかった");
		}
		else
		{
			// 先頭のテキスト（タイトル）と、最初の線（下線）だけを複製する。
			int copied = 0;
			bool tookText = false;
			bool tookLine = false;
			for (MCObjectHandle member = gSDK->FirstMemberObj(hOld); member != nil;
				 member = gSDK->NextObject(member))
			{
				const short type = gSDK->GetObjectTypeN(member);
				const bool want =
					(type == kTextNode && !tookText) || (type == kLineNode && !tookLine);
				if (!want)
					continue;
				MCObjectHandle hCopy = gSDK->DuplicateObject(member);
				if (hCopy == nil)
				{
					probe.log("  実験 2: DuplicateObject が nil（型=" + std::to_string(type) +
							  "）");
					continue;
				}
				if (!gSDK->AddObjectToContainer(hCopy, hGroup))
					probe.log("  実験 2: AddObjectToContainer が false（型=" +
							  std::to_string(type) + "）");
				else
					++copied;
				if (type == kTextNode)
					tookText = true;
				else
					tookLine = true;
			}
			probe.log("  複製した数=" + std::to_string(copied));
			probe.log(std::string("  SetCustomObjectProfileGroup=") +
					  (gSDK->SetCustomObjectProfileGroup(hLabel2, hGroup) ? "true" : "false"));
			gSDK->ResetObject(hLabel2);
			gSDK->UpdateViewport(hViewport);
			probe.log("  渡した後のレイアウト:");
			DlDumpLayout(probe, hLabel2);
			probe.log("  描かれたもの:");
			DlDumpDrawn(probe, hLabel2);
		}
	}

	probe.log("");
	probe.log("■ おわり");
}
