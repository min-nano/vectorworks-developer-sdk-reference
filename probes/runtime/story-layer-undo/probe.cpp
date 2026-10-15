//
//	probes/runtime/story-layer-undo/probe.cpp
//
//	[issue #223] #221 で分かった「仮のレベル種別を与えて `AssociateLayerWithStory` →
//	`SetLayerLevelType(layer, "")` で外す」（＝ストーリ＝その階・レベル＝なしのレイヤ）を、
//	**プラグイン自身が開いた undo イベントの中で**回し、閉じてから取り消したときに何が
//	戻るかを測る。
//
//	**このプローブは決まりの例外として、自分で undo イベントを開く**（それが問いそのもの）。
//	開き方は import-ifc の `draw::ImportUndoScope` と同じ——`SetUndoMethod(kUndoSwapObjects)`
//	＋ `NameUndoEvent` で開き、**作ったレイヤを `AddAfterSwapObject` で登録**し、
//	`EndUndoEvent` で閉じる。構造材へ渡すパスも登録する（渡したパスは SDK が undo 記録つきで
//	消費するので、登録しないと取り消しで復活する。Findings「Undo」）。
//	閉じたあと `DoMenuName("Undo", 0)`（⌘Z と同じコマンド）で取り消し、名前で引き直して読む
//	（取り消しの後は手元のハンドルを使わない——消えたものを指しているかもしれないため）。
//
//	  V1 作ったレイヤを全部登録する（import-ifc の作法どおり。レベル無しのレイヤは
//	     `CreateLayer` の直後に登録）
//	  V2 レベル無しのレイヤ**だけ**登録しない（レイヤが残る場合、階・種別がどう戻るか）
//	  V4 V1 と同じものを作って閉じ、**取り消す前に別名保存する**（`SaveActiveDocumentPath`。
//	     MCP ブリッジは走らせた後に図面をこれで一時ファイルへ保存する。別のコマンドから
//	     取り消した `story-layer-undo-later` で何も戻らなかったので、保存が取り消しの履歴を
//	     消すのかを同じコマンドの中で切り分ける）
//
//	取り消しは、階 1 が消えるまで最大 3 段掛け、段ごとに読む（間に別の段が挟まっていれば
//	2 段目で消える。何段掛けても消えなければ、履歴そのものが無い）。
//
//	各回とも、階 2 つ（1000 / 4000）・レベル種別 2 つ（天端・仮）・レベルテンプレート 1 つを
//	**イベントの中で**作り、レベル付きのレイヤ（テンプレート経由）とレベル無しのレイヤ X
//	（上の手順）を 1 階に置いて、それぞれに構造材を 1 本ずつ載せる。名前は回ごとに分ける。
//
//	**新規の空図面で走らせる。**
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
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

	// 一時ディレクトリへ別名保存する（まだ無い名前で）。戻り値は報告の 1 行。
	std::string ProbeSaveAsTemp()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			return "一時ディレクトリが引けない";
		std::filesystem::path target =
			dir / ("VwSdkProbes-223-" + std::to_string((long long)std::time(nullptr)) + ".vwx");
		const VectorWorks::Filing::IFileIdentifierPtr fileID(
			VectorWorks::Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(target.string().c_str())) != kVCOMError_NoError)
			return "IFileIdentifier を作れない";
		short saved = gSDK->SaveActiveDocumentPath(fileID);
		return "SaveActiveDocumentPath=" + ProbeInt(saved) +
			   "（0 が成功） 保存先に在る=" + ProbeFlag(std::filesystem::exists(target, ec));
	}

	// 1 回分。registerLayerX が false なら、レベル無しのレイヤ X だけ登録しない。
	void RunVariant(vwprobe::Report& probe, const std::string& key, const std::string& suffixHead,
					bool registerLayerX, bool saveBeforeUndo)
	{
		ProbeNames names;
		names.key = key;
		names.story1 = key + "-1階";
		names.story2 = key + "-2階";
		names.baseType = key + "天端";
		names.tempType = key + "仮";
		names.templateName = key + "天端雛形";
		names.layerX = key + "-柱X";

		probe.log("===== " + key + "（レベル無しのレイヤ X を" +
				  (registerLayerX ? "登録する" : "登録しない") +
				  (saveBeforeUndo ? "・取り消す前に別名保存" : "") + "） =====");
		LogResources(probe, key + " 開く前", names);

		// ---- イベントを開く（import-ifc の ImportUndoScope と同じ）
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent(TXString(("試験 #223 " + key).c_str()));
		probe.log(key + ": SetUndoMethod + NameUndoEvent の後 building=" + ProbeBuilding());

		TXString baseType(names.baseType.c_str());
		TXString tempType(names.tempType.c_str());
		probe.log(
			"CreateLayerLevelType 天端/仮=" + ProbeFlag(gSDK->CreateLayerLevelType(baseType)) +
			"/" + ProbeFlag(gSDK->CreateLayerLevelType(tempType)));
		MCObjectHandle hStory1 = MakeStory(probe, names.story1, suffixHead + "1", 1000.0);
		MCObjectHandle hStory2 = MakeStory(probe, names.story2, suffixHead + "2", 4000.0);
		if (hStory1 == nil || hStory2 == nil)
		{
			probe.fail(key + ": 階のハンドルが取れなかった。イベントを閉じて打ち切る");
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
		names.levelLayer1 = ProbeObjName(hLevelLayer1);
		names.levelLayer2 = ProbeObjName(hLevelLayer2);
		probe.log("レベル付きのレイヤ 1階=" + names.levelLayer1 + " 2階=" + names.levelLayer2 +
				  " AddAfterSwapObject 1階/2階=" +
				  ProbeFlag(hLevelLayer1 != nil && gSDK->AddAfterSwapObject(hLevelLayer1)) + "/" +
				  ProbeFlag(hLevelLayer2 != nil && gSDK->AddAfterSwapObject(hLevelLayer2)));

		// レベル無しのレイヤ X: 作る → （登録）→ 仮の種別 → 結ぶ → 外す
		MCObjectHandle hLayerX = gSDK->CreateLayer(names.layerX.c_str(), kLayerDesign);
		if (hLayerX == nil)
		{
			probe.fail(key + ": CreateLayer(X) が nil を返した。イベントを閉じて打ち切る");
			gSDK->EndUndoEvent();
			return;
		}
		std::string recordX = "登録しない";
		if (registerLayerX)
			recordX = "AddAfterSwapObject=" + ProbeFlag(gSDK->AddAfterSwapObject(hLayerX));
		TXString tempArg(names.tempType.c_str());
		bool setTemp = gSDK->SetLayerLevelType(hLayerX, tempArg);
		bool associated = gSDK->AssociateLayerWithStory(hLayerX, hStory1);
		TXString emptyType;
		bool cleared = gSDK->SetLayerLevelType(hLayerX, emptyType);
		probe.log("X: " + recordX + " SetLayerLevelType(仮)=" + ProbeFlag(setTemp) +
				  " AssociateLayerWithStory(1階)=" + ProbeFlag(associated) +
				  " SetLayerLevelType('')=" + ProbeFlag(cleared));

		MakeMember(probe, "構造材(レベル付き 1階)", hLevelLayer1, baseType, 0.0);
		MakeMember(probe, "構造材(X)", hLayerX, baseType, 2000.0);

		LogAll(probe, key + " 閉じる前", names);

		// ---- イベントを閉じる
		bool ended = gSDK->EndUndoEvent();
		probe.log(key + ": EndUndoEvent=" + ProbeFlag(ended) +
				  " 閉じた後 building=" + ProbeBuilding());

		if (saveBeforeUndo)
			probe.log(key + ": 取り消す前に別名保存: " + ProbeSaveAsTemp() +
					  " 後 building=" + ProbeBuilding());

		// ---- 取り消す（⌘Z と同じコマンド）。階 1 が消えるまで最大 3 段。
		for (int step = 1; step <= 3; ++step)
		{
			short undoResult = gSDK->DoMenuName("Undo", 0);
			probe.log(key + ": " + ProbeInt(step) + " 段目 DoMenuName(Undo)=" +
					  ProbeInt(undoResult) + " 後 building=" + ProbeBuilding());
			LogAll(probe, key + " 取り消し " + ProbeInt(step) + " 段目の後", names);
			if (gSDK->GetNamedObject(names.story1.c_str()) == nil)
				break;
		}
	}
} // namespace

VW_PROBE("story-layer-undo", "レベル無しのレイヤと undo", "自分の undo イベントで作って取り消す")
{
	probe.log("走り出し building=" + ProbeBuilding());
	gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever); // 設定ダイアログ抑止
	probe.log("DefineCustomObject の後 building=" + ProbeBuilding());
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
	{
		// 自分の取り消しが別の記録を巻き込まないよう、開いていれば先に閉じて別の段へ送る。
		probe.log("開いていたので EndUndoEvent=" + ProbeFlag(gSDK->EndUndoEvent()) +
				  " 後 building=" + ProbeBuilding());
	}

	RunVariant(probe, "V1", "a", true, false);
	RunVariant(probe, "V2", "b", false, false);
	RunVariant(probe, "V4", "d", true, true);

	probe.log("===== おわり（最後に building=" + ProbeBuilding() + "） =====");
}
