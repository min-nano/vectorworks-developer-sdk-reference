//
//	probes/runtime/story-layer-undo-later/probe.cpp
//
//	[issue #223] `story-layer-undo` は「閉じた直後に、同じコマンドの中で」取り消した。
//	実際の取り込みは**コマンドが終わってから**利用者が ⌘Z を押すので、それと同じ形で測る。
//	**同じ図面で 2 回走らせる。**
//
//	  1 回目（図面に階 V3-1階 が無いとき）: `story-layer-undo` の V1 と同じもの（作った
//	        レイヤを全部登録）に**クラスを 1 つ**（import-ifc と同じ `AddClass`）足して作り、
//	        `EndUndoEvent` で閉じて返る。
//	  2 回目（階 V3-1階 が在るとき）: 読んでから `DoMenuName("Undo", 0)` で取り消し、
//	        名前で引き直して読む。
//
//	**このプローブは決まりの例外として、自分で undo イベントを開く**（`story-layer-undo` と同じ）。
//	1 回目は**新規の空図面で**、2 回目は**その図面のまま**走らせる。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// ---------------------------------------------------------------- 書式
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

	std::string ProbeInt(long long value)
	{
		return std::to_string(value);
	}

	std::string ProbeText(const TXString& value)
	{
		return std::string(static_cast<const char*>(value));
	}

	std::string ProbeFlag(bool value)
	{
		return value ? "true" : "false";
	}

	std::string ProbeObjName(MCObjectHandle hObject)
	{
		if (hObject == nil)
			return "nil";
		TXString name;
		gSDK->GetObjectName(hObject, name);
		return ProbeText(name);
	}

	std::string ProbeBuilding()
	{
		return gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no";
	}

	// ---------------------------------------------------------------- 引き直し
	// 開き直した図面では GetNamedObject でデザインレイヤが引けなかった（Findings「Layers and
	// Stories」）ので、レイヤは ForEachLayerN で名前を突き合わせて引く。
	MCObjectHandle FindLayerByName(const std::string& name)
	{
		MCObjectHandle hFound = nil;
		gSDK->ForEachLayerN(
			[&](MCObjectHandle hLayer)
			{
				if (hFound == nil && ProbeObjName(hLayer) == name)
					hFound = hLayer;
			});
		return hFound;
	}

	// 名前の一覧（GetLayerLevelTypeName / GetStoryLevelTemplateName）は索引の基点が
	// 書かれていないので、0〜件数 を全部読んで並べる。
	std::string ListLevelTypes(bool& hasBase, bool& hasTemp, const std::string& baseName,
							   const std::string& tempName)
	{
		short count = gSDK->GetNumLayerLevelTypes();
		std::string names;
		hasBase = false;
		hasTemp = false;
		for (short index = 0; index <= count; ++index)
		{
			std::string name = ProbeText(gSDK->GetLayerLevelTypeName(index));
			if (name.empty())
				continue;
			hasBase = hasBase || name == baseName;
			hasTemp = hasTemp || name == tempName;
			names += (names.empty() ? "" : " / ") + ProbeInt(index) + ":" + name;
		}
		return "件数=" + ProbeInt(count) + " [" + names + "]";
	}

	std::string ListLevelTemplates(bool& hasTemplate, const std::string& templateName)
	{
		short count = gSDK->GetNumStoryLevelTemplates();
		std::string names;
		hasTemplate = false;
		for (short index = 0; index <= count; ++index)
		{
			std::string name = ProbeText(gSDK->GetStoryLevelTemplateName(index));
			if (name.empty())
				continue;
			hasTemplate = hasTemplate || name == templateName;
			names += (names.empty() ? "" : " / ") + ProbeInt(index) + ":" + name;
		}
		return "件数=" + ProbeInt(count) + " [" + names + "]";
	}

	// ---------------------------------------------------------------- 読み戻し
	struct ProbeNames
	{
		std::string key;
		std::string story1;
		std::string story2;
		std::string baseType;
		std::string tempType;
		std::string templateName;
		std::string layerX;
		std::string levelLayer1; // テンプレートが付けた名前（作った後に読む）
		std::string levelLayer2;
	};

	void LogResources(vwprobe::Report& probe, const std::string& tag, const ProbeNames& names)
	{
		bool hasBase = false;
		bool hasTemp = false;
		bool hasTemplate = false;
		std::string types = ListLevelTypes(hasBase, hasTemp, names.baseType, names.tempType);
		std::string templates = ListLevelTemplates(hasTemplate, names.templateName);
		probe.log(tag + ": building=" + ProbeBuilding() +
				  " GetNumStories=" + ProbeInt(gSDK->GetNumStories()));
		probe.log(tag + ": 階 " + names.story1 + "=" +
				  (gSDK->GetNamedObject(names.story1.c_str()) == nil ? "無い" : "在る") + " " +
				  names.story2 + "=" +
				  (gSDK->GetNamedObject(names.story2.c_str()) == nil ? "無い" : "在る"));
		probe.log(tag + ": レベル種別 " + types + " 天端=" + ProbeFlag(hasBase) +
				  " 仮=" + ProbeFlag(hasTemp));
		probe.log(tag + ": レベルテンプレート " + templates + " 自分の=" + ProbeFlag(hasTemplate));
	}

	void LogLayerByName(vwprobe::Report& probe, const std::string& tag, const std::string& name)
	{
		MCObjectHandle hLayer = FindLayerByName(name);
		if (hLayer == nil)
		{
			probe.log(tag + " '" + name + "': 無い");
			return;
		}
		int children = 0;
		int members = 0; // kParametricNode = 86
		std::string kinds;
		for (MCObjectHandle hChild = gSDK->FirstMemberObj(hLayer); hChild != nil;
			 hChild = gSDK->NextObject(hChild))
		{
			short type = gSDK->GetObjectTypeN(hChild);
			++children;
			if (type == 86)
				++members;
			kinds += (kinds.empty() ? "" : ",") + ProbeInt(type);
		}
		probe.log(tag + " '" + name + "': 在る 階=" + ProbeObjName(gSDK->GetStoryOfLayer(hLayer)) +
				  " レベル種別='" + ProbeText(gSDK->GetLayerLevelType(hLayer)) +
				  "' 高さ=" + ProbeNum(VWFC::VWObjects::VWLayerObj(hLayer).GetElevation()) +
				  " 子=" + ProbeInt(children) + "（型 " + kinds + "）PIO=" + ProbeInt(members));
	}

	void LogStoryLayers(vwprobe::Report& probe, const std::string& tag, const ProbeNames& names)
	{
		const std::string stories[] = {names.story1, names.story2};
		for (const std::string& storyName : stories)
		{
			MCObjectHandle hStory = gSDK->GetNamedObject(storyName.c_str());
			if (hStory == nil)
				continue;
			TXString baseType(names.baseType.c_str());
			TXString tempType(names.tempType.c_str());
			probe.log(tag + ": " + storyName + " 高さ=" +
					  ProbeNum(gSDK->GetStoryElevation(hStory)) + " GetLayerForStory(天端)=" +
					  ProbeObjName(gSDK->GetLayerForStory(hStory, baseType)) +
					  " (仮)=" + ProbeObjName(gSDK->GetLayerForStory(hStory, tempType)));
		}
	}

	void LogAll(vwprobe::Report& probe, const std::string& tag, const ProbeNames& names)
	{
		LogResources(probe, tag, names);
		LogStoryLayers(probe, tag, names);
		LogLayerByName(probe, tag + " レベル付き(1階)", names.levelLayer1);
		LogLayerByName(probe, tag + " レベル付き(2階)", names.levelLayer2);
		LogLayerByName(probe, tag + " レベル無し X", names.layerX);
	}

	// ---------------------------------------------------------------- 作る
	VectorWorks::SStoryObjectData MakeStoryBound(Sint8 boundStory, const TXString& levelType)
	{
		VectorWorks::SStoryObjectData data;
		data.fBound = VectorWorks::eStoryObjectBound_Story;
		data.fBoundStory = boundStory;
		data.fLayerLevelType = levelType;
		data.fOffset = 0.0;
		return data;
	}

	// 構造材 1 本（下端＝自階の天端・上端＝上階の天端）。渡すパスは先に登録しておく。
	void MakeMember(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hLayer,
					const TXString& baseType, double x0)
	{
		if (hLayer == nil)
		{
			probe.log(tag + ": レイヤが nil なので構造材は作らない");
			return;
		}
		gSDK->SetCurrentLayer(hLayer);
		VWFC::VWObjects::VWPolygon2DObj path(
			{VWFC::Math::VWPoint2D(x0, 0), VWFC::Math::VWPoint2D(x0 + 1000, 0)});
		bool pathRecorded = gSDK->AddAfterSwapObject(path.GetThisObject());
		MCObjectHandle hMember =
			gSDK->CreateCustomObjectPath("StructuralMember", path.GetThisObject(), nil, true);
		if (hMember == nil)
		{
			probe.fail(tag + ": CreateCustomObjectPath(StructuralMember) が nil を返した");
			return;
		}
		bool wroteBottom = gSDK->SetObjectStoryBound(hMember, 0, MakeStoryBound(0, baseType));
		bool wroteTop = gSDK->SetObjectStoryBound(hMember, 1, MakeStoryBound(1, baseType));
		gSDK->ResetObject(hMember);
		probe.log(tag + ": パスの AddAfterSwapObject=" + ProbeFlag(pathRecorded) +
				  " SetObjectStoryBound 下/上=" + ProbeFlag(wroteBottom) + "/" +
				  ProbeFlag(wroteTop) +
				  " 解決Z=" + ProbeNum(gSDK->GetObjectBoundElevation(hMember, 0)) + "/" +
				  ProbeNum(gSDK->GetObjectBoundElevation(hMember, 1)));
	}

	MCObjectHandle MakeStory(vwprobe::Report& probe, const std::string& name,
							 const std::string& suffix, double elevation)
	{
		TXString storyName(name.c_str());
		TXString storySuffix(suffix.c_str());
		bool created = gSDK->CreateStory(storyName, storySuffix);
		MCObjectHandle hStory = gSDK->GetNamedObject(name.c_str());
		bool setElevation = (hStory != nil) && gSDK->SetStoryElevation(hStory, elevation);
		probe.log("CreateStory(" + name + ")=" + ProbeFlag(created) + " ハンドル=" +
				  (hStory == nil ? "nil" : "有") + " SetStoryElevation=" + ProbeFlag(setElevation));
		return hStory;
	}

	const char* const kProbeClassName = "V3試験クラス";

	bool ProbeClassExists()
	{
		bool found = false;
		gSDK->ForEachClass(true,
						   [&](MCObjectHandle hClass)
						   {
							   if (ProbeObjName(hClass) == kProbeClassName)
								   found = true;
						   });
		return found;
	}

	ProbeNames MakeNames()
	{
		ProbeNames names;
		names.key = "V3";
		names.story1 = "V3-1階";
		names.story2 = "V3-2階";
		names.baseType = "V3天端";
		names.tempType = "V3仮";
		names.templateName = "V3天端雛形";
		names.layerX = "V3-柱X";
		names.levelLayer1 = "V3天端雛形-c1"; // AddStoryLevelFromTemplate が付ける名前
		names.levelLayer2 = "V3天端雛形-c2";
		return names;
	}

	void LogAllWithClass(vwprobe::Report& probe, const std::string& tag, const ProbeNames& names)
	{
		LogAll(probe, tag, names);
		probe.log(tag + ": クラス " + kProbeClassName + "=" +
				  (ProbeClassExists() ? "在る" : "無い"));
	}

	// 1 回目: 作って閉じて返る（取り消さない）。
	void BuildAndClose(vwprobe::Report& probe)
	{
		ProbeNames names = MakeNames();
		probe.log("===== 1 回目: 作って閉じる（取り消しは次の実行で） =====");
		gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever); // 設定ダイアログ抑止
		probe.log("DefineCustomObject の後 building=" + ProbeBuilding());
		if (gSDK->IsCurrentlyBuildingAnUndoEvent())
			probe.log("開いていたので EndUndoEvent=" + ProbeFlag(gSDK->EndUndoEvent()) +
					  " 後 building=" + ProbeBuilding());
		LogResources(probe, "開く前", names);
		probe.log("開く前: クラス " + std::string(kProbeClassName) + "=" +
				  (ProbeClassExists() ? "在る" : "無い"));

		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent(TXString("試験 #223 V3"));
		probe.log("SetUndoMethod + NameUndoEvent の後 building=" + ProbeBuilding());

		InternalIndex classIndex = gSDK->AddClass(TXString(kProbeClassName));
		probe.log("AddClass(" + std::string(kProbeClassName) + ")=" + ProbeInt(classIndex));

		TXString baseType(names.baseType.c_str());
		TXString tempType(names.tempType.c_str());
		probe.log(
			"CreateLayerLevelType 天端/仮=" + ProbeFlag(gSDK->CreateLayerLevelType(baseType)) +
			"/" + ProbeFlag(gSDK->CreateLayerLevelType(tempType)));
		MCObjectHandle hStory1 = MakeStory(probe, names.story1, "c1", 1000.0);
		MCObjectHandle hStory2 = MakeStory(probe, names.story2, "c2", 4000.0);
		if (hStory1 == nil || hStory2 == nil)
		{
			probe.fail("階のハンドルが取れなかった。イベントを閉じて打ち切る");
			gSDK->EndUndoEvent();
			return;
		}
		TXString templateName(names.templateName.c_str());
		TXString templateType(names.baseType.c_str());
		short templateIndex = 0;
		probe.log("CreateStoryLevelTemplate(相対Z=-40)=" +
				  ProbeFlag(gSDK->CreateStoryLevelTemplate(templateName, 1.0, templateType, -40.0,
														   0.0, templateIndex)) +
				  " index=" + ProbeInt(templateIndex));
		probe.log("AddStoryLevelFromTemplate 1階/2階=" +
				  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory1, templateIndex)) + "/" +
				  ProbeFlag(gSDK->AddStoryLevelFromTemplate(hStory2, templateIndex)));
		MCObjectHandle hLevelLayer1 = gSDK->GetLayerForStory(hStory1, baseType);
		MCObjectHandle hLevelLayer2 = gSDK->GetLayerForStory(hStory2, baseType);
		probe.log("レベル付きのレイヤ 1階=" + ProbeObjName(hLevelLayer1) +
				  " 2階=" + ProbeObjName(hLevelLayer2) + " AddAfterSwapObject 1階/2階=" +
				  ProbeFlag(hLevelLayer1 != nil && gSDK->AddAfterSwapObject(hLevelLayer1)) + "/" +
				  ProbeFlag(hLevelLayer2 != nil && gSDK->AddAfterSwapObject(hLevelLayer2)));

		MCObjectHandle hLayerX = gSDK->CreateLayer(names.layerX.c_str(), kLayerDesign);
		if (hLayerX == nil)
		{
			probe.fail("CreateLayer(X) が nil を返した。イベントを閉じて打ち切る");
			gSDK->EndUndoEvent();
			return;
		}
		bool recordedX = gSDK->AddAfterSwapObject(hLayerX);
		TXString tempArg(names.tempType.c_str());
		bool setTemp = gSDK->SetLayerLevelType(hLayerX, tempArg);
		bool associated = gSDK->AssociateLayerWithStory(hLayerX, hStory1);
		TXString emptyType;
		bool cleared = gSDK->SetLayerLevelType(hLayerX, emptyType);
		probe.log("X: AddAfterSwapObject=" + ProbeFlag(recordedX) + " SetLayerLevelType(仮)=" +
				  ProbeFlag(setTemp) + " AssociateLayerWithStory(1階)=" + ProbeFlag(associated) +
				  " SetLayerLevelType('')=" + ProbeFlag(cleared));

		MakeMember(probe, "構造材(レベル付き 1階)", hLevelLayer1, baseType, 0.0);
		MakeMember(probe, "構造材(X)", hLayerX, baseType, 2000.0);

		LogAllWithClass(probe, "閉じる前", names);
		probe.log("EndUndoEvent=" + ProbeFlag(gSDK->EndUndoEvent()) +
				  " 閉じた後 building=" + ProbeBuilding());
		probe.log("このまま返る。**同じ図面のまま、もう一度走らせると取り消す。**");
	}

	// 2 回目: 別のコマンドから取り消して読む。
	void UndoAndRead(vwprobe::Report& probe)
	{
		ProbeNames names = MakeNames();
		probe.log("===== 2 回目: 別のコマンドから取り消す =====");
		LogAllWithClass(probe, "取り消す前", names);
		// 階 1 が消えるまで最大 3 段。間に別の段が挟まっていれば 2 段目以降で消える。
		for (int step = 1; step <= 3; ++step)
		{
			short undoResult = gSDK->DoMenuName("Undo", 0);
			probe.log(ProbeInt(step) + " 段目 DoMenuName(Undo)=" + ProbeInt(undoResult) +
					  " 後 building=" + ProbeBuilding());
			LogAllWithClass(probe, "取り消し " + ProbeInt(step) + " 段目の後", names);
			if (gSDK->GetNamedObject(names.story1.c_str()) == nil)
				break;
		}
	}
} // namespace

VW_PROBE("story-layer-undo-later", "undo を後で掛ける", "2 回走らせる（作る→取り消す）")
{
	probe.log("走り出し building=" + ProbeBuilding());
	if (gSDK->GetNamedObject("V3-1階") == nil)
		BuildAndClose(probe);
	else
		UndoAndRead(probe);
	probe.log("===== おわり（最後に building=" + ProbeBuilding() + "） =====");
}
