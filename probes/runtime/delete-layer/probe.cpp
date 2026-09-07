//
//	probes/runtime/delete-layer/probe.cpp
//
//	[issue #23] レイヤのハンドルを ISDK::DeleteObject で直接消したときに何が起きるかを
//	確かめる。DeleteLayer 相当の専用 API は SDK に無い（sdk-grep で確認済み）ので、
//	候補は「オブジェクト用の DeleteObject をレイヤのハンドルへ呼ぶ」の 1 通りだけ。
//
//	見たいのは 2 つ:
//	  1) 非アクティブなデザインレイヤを消したとき、その上の図形も一緒に消えるか
//	     （Findings/Undo.md はこれを undo テーブル経由の話として書いており、直接の
//	     DeleteObject でも同じかは未確認）。
//	  2) アクティブレイヤ自身を消してよいか、消した後 GetCurrentLayer がどうなるか。
//	     ここで VW が落ちる可能性があるので、危険な呼び出しの前にログを出しておく。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <string>

namespace
{
// 図面のオブジェクト列を先頭から辿り、同名のレイヤがまだ残っているかを見る。
// ISDK に「名前でレイヤを引く」呼び出しは無いので、これが唯一の手立て
// （probes/runtime/example/probe.cpp と同じ辿り方）。
bool LayerExistsByName(const TXString& name)
{
	for (MCObjectHandle h = VWDocument::GetDrawingHeaderFristMember(); h != nil;
		 h = gSDK->NextObject(h))
	{
		if (!VWLayerObj::IsLayerObject(h))
			continue;
		VWLayerObj layer(h); // SDK のラッパは const 修飾が揃っていないので非 const で持つ
		if (layer.GetObjectName() == name)
			return true;
	}
	return false;
}

std::string LayerNameOrNil(MCObjectHandle h)
{
	if (h == nil)
		return "(nil)";
	TXString name;
	gSDK->GetObjectName(h, name);
	return static_cast<const char*>(name);
}
} // namespace

VW_PROBE("delete-layer", "レイヤのハンドルを DeleteObject で直接消す",
		 "非アクティブレイヤ+図形の削除と、アクティブレイヤ自身の削除を試し、"
		 "上の図形・GetCurrentLayer の扱いを確かめる")
{
	const MCObjectHandle originalActive = gSDK->GetCurrentLayer();
	probe.log("削除前のアクティブレイヤ: " + LayerNameOrNil(originalActive));

	// --- 1) 図形が乗った非アクティブレイヤを直接消す ---
	const TXString name1 = "probe-delete-layer-1-with-rect";
	MCObjectHandle layer1 = gSDK->CreateLayer(name1, kDesignLayerType);
	if (layer1 == nil)
	{
		probe.fail("CreateLayer(1) が nil を返した");
		return;
	}
	gSDK->SetCurrentLayer(layer1);
	MCObjectHandle rect = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
	if (rect == nil)
	{
		probe.fail("CreateRectangle が nil を返した");
		return;
	}
	// 本題のアクティブレイヤ削除（下の 2)）と条件を混ぜないよう、ここで元へ戻しておく。
	if (originalActive != nil)
		gSDK->SetCurrentLayer(originalActive);

	probe.log("非アクティブレイヤ 1 枚 + 矩形 1 個を用意した（layer1 = " +
			  std::string(static_cast<const char*>(name1)) + "）");

	gSDK->DeleteObject(layer1, true);
	probe.log(std::string("layer1 を DeleteObject(useUndo=true) で削除した。"
						   "本体から見た undo: building=") +
			  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
	probe.log(std::string("削除後、同名レイヤが図面に残っているか: ") +
			  (LayerExistsByName(name1) ? "残っている（想定外）" : "消えた"));

	// --- 2) アクティブレイヤ自身を消す（ここから先で VW が落ちる可能性がある） ---
	const TXString name2 = "probe-delete-layer-2-active";
	MCObjectHandle layer2 = gSDK->CreateLayer(name2, kDesignLayerType);
	if (layer2 == nil)
	{
		probe.fail("CreateLayer(2) が nil を返した");
		return;
	}
	gSDK->SetCurrentLayer(layer2);
	probe.log("layer2 をアクティブレイヤにした（消す前の最後のログ。ここから先で"
			  "落ちてもこの行までは残る）");

	gSDK->DeleteObject(layer2, true);

	probe.log("アクティブレイヤ(layer2)を DeleteObject で削除した（ここまで来れば"
			  "VW は落ちていない）");
	probe.log("削除後の GetCurrentLayer: " + LayerNameOrNil(gSDK->GetCurrentLayer()));
	probe.log(std::string("削除後、同名レイヤ(2)が図面に残っているか: ") +
			  (LayerExistsByName(name2) ? "残っている（想定外）" : "消えた"));

	// 後始末: アクティブレイヤが変わってしまった場合に備えて、元のアクティブレイヤへ戻す
	// （元のレイヤ自体が残っていれば、というだけの気休め。無くなっていても失敗にはしない）。
	if (originalActive != nil && LayerExistsByName(LayerNameOrNil(originalActive).c_str()))
		gSDK->SetCurrentLayer(originalActive);
}
