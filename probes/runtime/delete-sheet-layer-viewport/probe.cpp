//
//	probes/runtime/delete-sheet-layer-viewport/probe.cpp
//
//	[issue #29] issue #25「レイヤのハンドルを直接 DeleteObject する」が未確認のまま残した
//	領域を埋める。デザインレイヤ単体の削除は #25 で確認済み（Findings/Undo.md）。今回見たいのは
//	「ビューポートが載ったシートレイヤ」を消したときの副作用。
//
//	  1) ビューポートが 1 つ以上載ったシートレイヤを DeleteObject(sheetLayer, true) で
//	     消すと、レイヤごと消えるか・ビューポートだけ残るか・落ちるか。
//	  2) そのビューポートが表示していたデザインレイヤ側に副作用が出るか。
//	  3) 「シートを先に消す」（安全とされる順序）と「デザインレイヤを先に消す」（ビュー
//	     ポートがまだ参照している状態で参照先を消す、逆順）とで違いが出るか。
//	  4) ビューポートの注釈空間に置いたオブジェクト（データタグ・グラフィック凡例の代用
//	     として単純なロケータ点を使う——それ自体の作り方は Findings/Data Tags.md /
//	     Graphic Legends.md が別に持っている）が、シートレイヤと一緒に消えるか。
//
//	【ヘッダ根拠で構成】シート上のビューポートが「どのデザインレイヤを表示するか」は
//	ISDK::CreateViewport の引数では決まらない——そちらは「どの容れ物（レイヤ／レイヤ内の
//	グループ）に置くか」だけを取る（GS_CreateViewport の説明: "The specified parent handle
//	may only be a layer or a group contained within a layer, nested or otherwise"）。表示する
//	デザインレイヤは作成後に ISDK::SetViewportLayerVisibility(viewport, designLayer,
//	kLayerVisibilityNormal) で個別に可視性を設定して初めて決まる。
//
//	シナリオ A（issue の本題・安全とされる順序）: 表示用の非アクティブデザインレイヤ + 矩形、
//	それを表示するビューポート 2 つを載せたシートレイヤ、ビューポートの注釈空間に置いた
//	ロケータ点、を用意して**シートレイヤを先に**消す。
//
//	シナリオ B（issue が知りたい逆順）: 同じ組み合わせを用意し、**デザインレイヤを先に**
//	消す（ビューポートがまだ参照している状態）。ここから先で VW が落ちる可能性があるので、
//	危険な呼び出しの前後でログを厚くする。最後にシートレイヤも消して後始末する。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"
#include "VWFC/VWObjects/VWViewportObj.h"

#include <string>
#include <vector>

namespace
{
	// 図面のオブジェクト列を先頭から辿り、同名のレイヤがまだ残っているかを見る
	// （probes/runtime/delete-layer/probe.cpp と同じ辿り方。ISDK に「名前でレイヤを
	// 引く」呼び出しは無い）。
	bool LayerExistsByName(const TXString& name)
	{
		for (MCObjectHandle h = VWDocument::GetDrawingHeaderFristMember(); h != nil;
			 h = gSDK->NextObject(h))
		{
			if (!VWLayerObj::IsLayerObject(h))
				continue;
			VWLayerObj layer(h);
			if (layer.GetObjectName() == name)
				return true;
		}
		return false;
	}

	// 残っている各レイヤの直下のメンバを 1 段だけ見て、名前が prefix で始まるものを拾う。
	// 「取り残されていないか」を判定ではなくログとして残すための道具
	// （probes/runtime/README「落ちてもよいが、落ちる前に書く」）。
	std::vector<std::string> FindObjectsByNamePrefix(const std::string& prefix)
	{
		std::vector<std::string> hits;
		for (MCObjectHandle hLayer = VWDocument::GetDrawingHeaderFristMember(); hLayer != nil;
			 hLayer = gSDK->NextObject(hLayer))
		{
			if (!VWLayerObj::IsLayerObject(hLayer))
				continue;
			TXString layerNameTx;
			gSDK->GetObjectName(hLayer, layerNameTx);
			const std::string layerName = layerNameTx.GetStdString();

			for (MCObjectHandle hObj = gSDK->FirstMemberObj(hLayer); hObj != nil;
				 hObj = gSDK->NextObject(hObj))
			{
				TXString objNameTx;
				gSDK->GetObjectName(hObj, objNameTx);
				const std::string objName = objNameTx.GetStdString();
				if (objName.rfind(prefix, 0) == 0) // starts with prefix
					hits.push_back(layerName + "/" + objName);
			}
		}
		return hits;
	}

	// デザインレイヤの直下メンバの中に、名前が一致する図形があるかを見る
	// （デザインレイヤ自体は生きている前提。GetLayerType の確認は呼び出し側で行う）。
	bool LayerHasMemberNamed(MCObjectHandle hLayer, const TXString& name)
	{
		for (MCObjectHandle hObj = gSDK->FirstMemberObj(hLayer); hObj != nil;
			 hObj = gSDK->NextObject(hObj))
		{
			TXString objName;
			gSDK->GetObjectName(hObj, objName);
			if (objName == name)
				return true;
		}
		return false;
	}
} // namespace

VW_PROBE("delete-sheet-layer-viewport", "ビューポート付きシートレイヤの DeleteObject",
		 "ビューポート(+注釈オブジェクト)が載ったシートレイヤを消したときのビューポート・"
		 "デザインレイヤへの副作用と、削除順序による違いを確かめる")
{
	// ------------------------------------------------------------------
	// シナリオ A: シートレイヤを先に消す（issue が「安全なはず」とする順序）
	// ------------------------------------------------------------------
	probe.log("=== シナリオ A: ビューポート付きシートレイヤを先に消す ===");

	const TXString designNameA = "probe-dslvp-design-a";
	MCObjectHandle designA = gSDK->CreateLayer(designNameA, kLayerDesign);
	if (designA == nil)
	{
		probe.fail("シナリオA: CreateLayer(design) が nil を返した");
		return;
	}
	gSDK->SetCurrentLayer(designA);
	const TXString rectName = "probe-dslvp-rect-a";
	MCObjectHandle rectA = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
	if (rectA == nil)
	{
		probe.fail("シナリオA: CreateRectangle が nil を返した");
		return;
	}
	gSDK->SetObjectName(rectA, rectName);
	probe.log("デザインレイヤ(" + designNameA.GetStdString() + ") + 矩形(" +
			  rectName.GetStdString() + ") を用意した");

	const TXString sheetNameA = "probe-dslvp-sheet-a";
	MCObjectHandle sheetA = gSDK->CreateLayer(sheetNameA, kLayerSheet);
	if (sheetA == nil)
	{
		probe.fail("シナリオA: CreateLayer(sheet) が nil を返した");
		return;
	}
	probe.log("シートレイヤ(" + sheetNameA.GetStdString() + ") を用意した");

	// ビューポートを 2 つ、同じデザインレイヤを表示する形でシートへ載せる
	// （issue の「ビューポートが 1 つ以上」を汲んで複数枚で試す）。
	MCObjectHandle vpA1 = gSDK->CreateViewport(sheetA);
	MCObjectHandle vpA2 = gSDK->CreateViewport(sheetA);
	if (vpA1 == nil || vpA2 == nil)
	{
		probe.fail("シナリオA: CreateViewport が nil を返した");
		return;
	}
	gSDK->SetViewportLayerVisibility(vpA1, designA, VWFC::VWObjects::kLayerVisibilityNormal);
	gSDK->SetViewportLayerVisibility(vpA2, designA, VWFC::VWObjects::kLayerVisibilityNormal);
	gSDK->UpdateViewport(vpA1);
	gSDK->UpdateViewport(vpA2);
	probe.log("シートA上にビューポートを 2 つ作り、どちらもデザインレイヤAを表示するよう "
			  "SetViewportLayerVisibility(kLayerVisibilityNormal) で設定・更新した");

	// 注釈空間のオブジェクト（データタグ・グラフィック凡例の代用としてロケータ点）。
	// 一度シートへ普通に置いてから AddViewportAnnotationObject で注釈へ移す。
	gSDK->SetCurrentLayer(sheetA);
	const TXString annoNameA = "probe-dslvp-anno-a";
	MCObjectHandle annoA = gSDK->CreateLocus(WorldPt(0, 0));
	if (annoA == nil)
	{
		probe.fail("シナリオA: CreateLocus(注釈用) が nil を返した");
		return;
	}
	gSDK->SetObjectName(annoA, annoNameA);
	const bool addedToAnno = gSDK->AddViewportAnnotationObject(vpA1, annoA);
	const bool isInAnnoGroup = VWFC::VWObjects::VWViewportObj::IsViewportGroupContainedObject(
		annoA, kViewportGroupAnnotation);
	probe.log("注釈オブジェクト(" + annoNameA.GetStdString() +
			  ") を vpA1 の注釈空間へ AddViewportAnnotationObject で追加した: added=" +
			  std::string(addedToAnno ? "yes" : "no") +
			  " / 追加後に注釈グループの一員か=" + std::string(isInAnnoGroup ? "yes" : "no"));

	probe.log(std::string("削除前の undo: building=") +
			  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
	probe.log("ここから DeleteObject(sheetA, true) を呼ぶ。ここまでは落ちていない");

	gSDK->DeleteObject(sheetA, true);

	probe.log(
		std::string("DeleteObject(sheetA, true) から戻った（落ちていない）。undo: building=") +
		(gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
	probe.log(std::string("削除後、シートA(" + sheetNameA.GetStdString() + ")は残っているか: ") +
			  (LayerExistsByName(sheetNameA) ? "残っている（想定外）" : "消えた"));
	probe.log(std::string("削除後、デザインA(" + designNameA.GetStdString() + ")は残っているか: ") +
			  (LayerExistsByName(designNameA) ? "残っている" : "消えた（想定外）"));
	if (LayerExistsByName(designNameA))
	{
		probe.log(
			std::string("デザインAの上の矩形(" + rectName.GetStdString() + ")は残っているか: ") +
			(LayerHasMemberNamed(designA, rectName) ? "残っている" : "消えた（想定外）"));
	}
	{
		const std::vector<std::string> stray = FindObjectsByNamePrefix("probe-dslvp-");
		probe.log("削除後、図面上の各レイヤ直下に残っている probe-dslvp-* 系オブジェクト: " +
				  (stray.empty() ? std::string("なし")
								 : [&stray]() {
									   std::string s;
									   for (const auto& h : stray)
										   s += h + " / ";
									   return s;
								   }()));
	}

	// ------------------------------------------------------------------
	// シナリオ B: 逆順——ビューポートが参照しているデザインレイヤを先に消す
	// ------------------------------------------------------------------
	probe.log("=== シナリオ B: ビューポートが参照するデザインレイヤを先に消す（逆順） ===");

	const TXString designNameB = "probe-dslvp-design-b";
	MCObjectHandle designB = gSDK->CreateLayer(designNameB, kLayerDesign);
	if (designB == nil)
	{
		probe.fail("シナリオB: CreateLayer(design) が nil を返した");
		return;
	}
	gSDK->SetCurrentLayer(designB);
	MCObjectHandle rectB = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
	if (rectB == nil)
	{
		probe.fail("シナリオB: CreateRectangle が nil を返した");
		return;
	}

	const TXString sheetNameB = "probe-dslvp-sheet-b";
	MCObjectHandle sheetB = gSDK->CreateLayer(sheetNameB, kLayerSheet);
	if (sheetB == nil)
	{
		probe.fail("シナリオB: CreateLayer(sheet) が nil を返した");
		return;
	}
	MCObjectHandle vpB = gSDK->CreateViewport(sheetB);
	if (vpB == nil)
	{
		probe.fail("シナリオB: CreateViewport が nil を返した");
		return;
	}
	gSDK->SetViewportLayerVisibility(vpB, designB, VWFC::VWObjects::kLayerVisibilityNormal);
	gSDK->UpdateViewport(vpB);
	probe.log("デザインB(" + designNameB.GetStdString() + ") + 矩形、シートB(" +
			  sheetNameB.GetStdString() + ") + それを表示するビューポート vpB を用意した");

	probe.log("ここから DeleteObject(designB, true) を呼ぶ（vpB がまだ参照している状態で"
			  "参照先を消す、危険な呼び出し）。ここまでは落ちていない");

	gSDK->DeleteObject(designB, true);

	probe.log("DeleteObject(designB, true) から戻った（ここまで来ればVWは落ちていない）");
	probe.log(std::string("削除後、デザインB(" + designNameB.GetStdString() + ")は残っているか: ") +
			  (LayerExistsByName(designNameB) ? "残っている（想定外）" : "消えた"));
	probe.log(std::string("削除後、シートB(" + sheetNameB.GetStdString() + ")は残っているか: ") +
			  (LayerExistsByName(sheetNameB) ? "残っている" : "消えた（想定外）"));

	if (LayerExistsByName(sheetNameB))
	{
		// vpB がまだシートBの直下メンバとして辿れるか（参照先を失った後も器としては残るか）。
		bool vpFoundAsMember = false;
		for (MCObjectHandle hObj = gSDK->FirstMemberObj(sheetB); hObj != nil;
			 hObj = gSDK->NextObject(hObj))
		{
			if (hObj == vpB)
			{
				vpFoundAsMember = true;
				break;
			}
		}
		probe.log(std::string("参照先を失った後、vpB はシートBの直下メンバとして辿れるか: ") +
				  (vpFoundAsMember ? "辿れる" : "辿れない"));

		if (vpFoundAsMember)
		{
			probe.log("ここから vpB を UpdateViewport で更新してみる（参照先が無い状態での"
					  "更新。ここまでは落ちていない）");
			gSDK->UpdateViewport(vpB);
			probe.log("UpdateViewport(vpB) から戻った（落ちていない）");
		}
	}

	// 後始末: 参照先を失ったシートBも消す（逆順でもシート側の削除自体が壊れないかを見る）。
	if (LayerExistsByName(sheetNameB))
	{
		probe.log("後始末: 参照先を失ったシートBを DeleteObject(sheetB, true) で消す");
		gSDK->DeleteObject(sheetB, true);
		probe.log(
			std::string("削除後、シートB(" + sheetNameB.GetStdString() + ")は残っているか: ") +
			(LayerExistsByName(sheetNameB) ? "残っている（想定外）" : "消えた"));
	}

	probe.log(std::string("プローブ終了時点の undo: building=") +
			  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
}
