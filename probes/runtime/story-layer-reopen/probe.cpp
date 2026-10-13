//
//	probes/runtime/story-layer-reopen/probe.cpp
//
//	[issue #221] `story-layer-recipe` で作った図面を**保存して開き直してから**走らせ、
//	「ストーリ＝その階・レベル＝なし」のレイヤが開き直しても保たれるかを読む。
//	**図面を作らない・変えない**（構造材の ResetObject だけは掛ける）。
//	新規の図面ではなく、開き直した図面を前面にして走らせる（vw_run_probe なら
//	new_document=false）。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	std::string ProbeNum(double value)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.4f", value);
		std::string text(buf);
		while (text.size() > 1 && text[text.size() - 1] == '0')
			text.erase(text.size() - 1);
		if (!text.empty() && text[text.size() - 1] == '.')
			text.erase(text.size() - 1);
		return text;
	}

	std::string ProbeText(const TXString& value)
	{
		return std::string(static_cast<const char*>(value));
	}

	std::string ProbeObjName(MCObjectHandle hObject)
	{
		if (hObject == nil)
			return "nil";
		TXString name;
		gSDK->GetObjectName(hObject, name);
		return ProbeText(name);
	}

	void LogMembers(vwprobe::Report& probe, MCObjectHandle hLayer, bool reset)
	{
		for (MCObjectHandle hObject = gSDK->FirstMemberObj(hLayer); hObject != nil;
			 hObject = gSDK->NextObject(hObject))
		{
			if (gSDK->GetObjectStoryBoundsCount(hObject) == 0)
				continue;
			if (reset)
				gSDK->ResetObject(hObject);
			VectorWorks::SStoryObjectData top;
			bool readTop = gSDK->GetObjectStoryBound(hObject, 1, top);
			double bottomZ = gSDK->GetObjectBoundElevation(hObject, 0);
			double topZ = gSDK->GetObjectBoundElevation(hObject, 1);
			probe.log(std::string("    構造材") +
					  (reset ? "（ResetObject 後）" : "（開いたまま）") +
					  ": 解決Z 下端=" + ProbeNum(bottomZ) + " 上端=" + ProbeNum(topZ) +
					  " 差=" + ProbeNum(topZ - bottomZ) + " / ID1 の読み戻し=" +
					  (readTop ? "true" : "false") + " fBound=" + std::to_string((int)top.fBound) +
					  " fBoundStory=" + std::to_string((int)top.fBoundStory) + " 種別='" +
					  ProbeText(top.fLayerLevelType) + "'");
		}
	}
} // namespace

VW_PROBE("story-layer-reopen", "開き直した図面で階の結び付きを読む",
		 "story-layer-recipe の図面を開き直して読む")
{
	const char* storyNames[] = {"試験1階", "試験2階", "試験3階"};
	for (const char* storyName : storyNames)
	{
		MCObjectHandle hStory = gSDK->GetNamedObject(storyName);
		probe.log(
			std::string(storyName) + ": " +
			(hStory == nil ? "見つからない" : "高さ=" + ProbeNum(gSDK->GetStoryElevation(hStory))));
	}
	// GetNamedObject でレイヤが引けるかも記録する（1 回目はここで nil だった）。
	const char* layerNames[] = {"試験-柱X", "試験-柱Y", "試験-柱Z", "試験天端雛形-t1"};
	for (const char* layerName : layerNames)
		probe.log(std::string("GetNamedObject(") + layerName +
				  ")=" + (gSDK->GetNamedObject(layerName) == nil ? "nil" : "有"));
	probe.log("デザインレイヤの並び（前面から）");
	int position = 0;
	for (MCObjectHandle hObject = VWFC::VWObjects::VWDocument::GetDrawingHeaderFristMember();
		 hObject != nil; hObject = gSDK->NextObject(hObject))
	{
		if (gSDK->GetObjectTypeN(hObject) != 31 /* kLayerNode */)
			continue;
		VWFC::VWObjects::VWLayerObj layerObj(hObject);
		if (layerObj.GetLayerType() != kLayerDesign)
			continue;
		++position;
		probe.log("  " + std::to_string(position) + ". " + ProbeObjName(hObject) +
				  " 高さ=" + ProbeNum(layerObj.GetElevation()) +
				  " 階=" + ProbeObjName(gSDK->GetStoryOfLayer(hObject)) + " レベル種別='" +
				  ProbeText(gSDK->GetLayerLevelType(hObject)) + "'");
		LogMembers(probe, hObject, false);
		LogMembers(probe, hObject, true);
	}
}
