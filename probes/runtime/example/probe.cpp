//
//	probes/runtime/example/probe.cpp
//
//	**雛形かつ煙試験。** 新しい調査を書くときはこのディレクトリを写して
//	probes/runtime/<調査の slug>/ にし、中身を置き換える（書き方は
//	probes/runtime/README.md）。
//
//	このプローブ自体は図面を**読むだけ**で、何も作らず・何も変えない。実機で
//	「メニュー → プローブを選ぶ → 走る → 結果が出る」の 1 周が回ることを確かめる
//	ための最小の題材として、常にビルドへ入れてある。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWLayerObj.h"

#include <string>

VW_PROBE("example", "煙試験: 図面のレイヤを数える",
		 "図面を読むだけ（何も作らない）。プラグインの導線が通っているかの確認用")
{
	// undo イベントの状態。**プローブの前後は ProbeMenu が記録する**ので、ここでは
	// 「本体の中から見た状態」だけを 1 行。
	probe.log(std::string("本体から見た undo: building=") +
			  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));

	const MCObjectHandle current = gSDK->GetCurrentLayer();
	if (current == nil)
	{
		probe.fail("カレントレイヤが取れない（文書が開いていない？）");
		return;
	}

	// 図面のオブジェクト列の先頭＝最初のレイヤ。以降は NextObject でたどる
	// （ISDK に「レイヤだけを列挙する」呼び出しは無いので、これが唯一の手立て）。
	// 綴りは SDK ママ（GetDrawingHeaderFristMember）。
	size_t design = 0;
	size_t sheet = 0;
	for (MCObjectHandle h = VWDocument::GetDrawingHeaderFristMember(); h != nil;
		 h = gSDK->NextObject(h))
	{
		if (!VWLayerObj::IsLayerObject(h))
			continue;
		VWLayerObj layer(h); // SDK のラッパは const 修飾が揃っていないので非 const で持つ
		const bool isSheet = (layer.GetLayerType() == kLayerSheet);
		if (isSheet)
			++sheet;
		else
			++design;
		// TXString の operator const char*() は UTF-8 を返す（std::string へはこれで写す）。
		probe.log(std::string(isSheet ? "  シート: " : "  デザイン: ") +
				  static_cast<const char*>(layer.GetObjectName()));
	}

	probe.log("デザインレイヤ " + std::to_string(design) + " 枚 / シートレイヤ " +
			  std::to_string(sheet) + " 枚");

	if (design == 0 && sheet == 0)
		probe.fail("レイヤを 1 枚も数えられなかった（走査の前提が崩れている）");
}
