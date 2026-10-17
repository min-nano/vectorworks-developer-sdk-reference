//
//	probes/runtime/deferred-datatag/probe.cpp
//
//	[issue #245] データタグと高さの「反映されない / 追随しない / 空」を、**書いた直後（T0）・
//	ランループを回した後（T1）・VW のイベントループを経た 2 回目（T2）**の 3 時点で測り直す。
//	過去の結論（#187 / #194 / #196）は、どれも**書いたのと同じ呼び出しの中で読んで**出した
//	ものなので、「後で反映されていた」可能性を潰していない。仕立ては Deferred.h。
//
//	  path: 構造材のパスを 3D（Create3DPoly）で渡すと実体が無く `#ZTBB…#` が ±1.79e308、
//	        2D（VWPolygon2DObj）なら実体がある——を、同じバウンドの 2 本で比べる。
//	        `#BX#` `#BY#` `#BZ#` が空・`#StartElevation#` / `#EndElevation#` の値・
//	        タグが描いた文字（テキストの件数）も同時に見る。
//	  dlg:  `DialogStartElevation` に書いた −872 は、部材を +1000 動かしても・バウンドの
//	        fOffset を −1872 にしても −872 のまま——が、後でも変わらないか。
//	  lay:  `AddObjectToContainer` で移した部材は、移した先のレイヤ高さ（1100 → 1300）に
//	        追随しない（絶対Z 900 のまま）——が、後でも変わらないか。
//	  sty:  ストーリ由来のレイヤへ SetElevation(5000) を掛けると、階内相対Z
//	        （GetStoryLevelElevation）が更新されない——が、後でも変わらないか。
//
//	測る側（measure）は図形を**名前で**引き直す。各タグの欄は 2 通り読む:
//	  raw = 何も触らずに GetDataTagExtractedData と描いた文字を読む（VW が自分で更新したか）
//	  upd = 旧プローブと同じ手順（SetFormula → UpdateDataTag → ResetObject）で引き直して読む
//

#include "Deferred.h"

#include "VWFC/VWObjects/VWLayerObj.h"
#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWPolygon2DObj.h"

#include <string>
#include <utility>
#include <vector>

namespace
{
	// ---------------------------------------------------------------------------
	// 舞台の名前（すべて互いに違う数値にしてある）

	struct DtStoryDef
	{
		const char* story;
		const char* suffix;
		double storyZ;
		const char* levelType;
		const char* tplName;
		double relZ;
	};

	// 階 2F（3571）・レベル FL（階内相対 0）—— path の部材と dlg のバウンド先
	const DtStoryDef kDt2F = {"DT245-2F", "DT245A", 3571.0, "DT245-FL", "DT245-TPL-FL", 0.0};
	// 階 PLAN（2699）・レベル PL（階内相対 0）—— dlg の部材が載る伏図レイヤ
	const DtStoryDef kDtPlan = {"DT245-PLAN", "DT245B", 2699.0, "DT245-PL", "DT245-TPL-PL", 0.0};
	// 階 S（6000）・レベル LV（階内相対 −40 → レイヤ 5960）—— sty
	const DtStoryDef kDtStoryS = {"DT245-S", "DT245C", 6000.0, "DT245-LV", "DT245-TPL-LV", -40.0};

	const char* const kDtFormulaDse = "#StructuralMember#.#DialogStartElevation#";

	// ---------------------------------------------------------------------------
	// 小物

	std::string DtFromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string DtOneLine(const std::string& value)
	{
		std::string out;
		for (char c : value)
		{
			if (c == '\r')
				continue;
			if (c == '\n')
				out += "\\n";
			else if (c == '\t')
				out += "\\t";
			else
				out += c;
		}
		return out;
	}

	// PIO の定義は文書ごと（RunDeferred が新規図面を開くので、setup の中で毎回呼ぶ）。
	void DtDefinePios()
	{
		gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
		gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);
	}

	// ---------------------------------------------------------------------------
	// 階とレベル（旧プローブの手順そのまま。**出力引数の index を信用せず種別で引き直す**）

	MCObjectHandle DtFindStoryLayer(const DtStoryDef& def)
	{
		MCObjectHandle hStory = DfFind(def.story);
		if (hStory == nullptr)
			return nullptr;
		return gSDK->GetLayerForStory(hStory, TXString(def.levelType));
	}

	MCObjectHandle DtMakeStoryLayer(vwprobe::Report& probe, const DtStoryDef& def)
	{
		if (MCObjectHandle hExisting = DtFindStoryLayer(def))
			return hExisting;

		TXString levelType(def.levelType);
		gSDK->CreateLayerLevelType(levelType);
		TXString storyName(def.story);
		TXString storySuffix(def.suffix);
		gSDK->CreateStory(storyName, storySuffix);
		MCObjectHandle hStory = DfFind(def.story);
		if (hStory == nullptr)
		{
			probe.fail(std::string("階 ") + def.story + " を作れなかった");
			return nullptr;
		}
		gSDK->SetStoryElevation(hStory, def.storyZ);

		short index = -1;
		TXString templateName(def.tplName);
		gSDK->CreateStoryLevelTemplate(templateName, 1.0, levelType, def.relZ, 2400.0, index);
		short byType = -1;
		const short count = gSDK->GetNumStoryLevelTemplates();
		for (short i = 0; i <= count && byType < 0; ++i)
		{
			TXString name;
			TXString type;
			double scale = 0.0;
			double offset = 0.0;
			double wallHeight = 0.0;
			if (gSDK->GetStoryLevelTemplateInfo(i, name, scale, type, offset, wallHeight) &&
				DtFromTX(type) == def.levelType)
				byType = i;
		}
		MCObjectHandle hLayer = nullptr;
		if (byType >= 0)
		{
			gSDK->AddStoryLevelFromTemplate(hStory, byType);
			hLayer = gSDK->GetLayerForStory(hStory, levelType);
		}
		if (hLayer == nullptr)
		{
			probe.fail(std::string("階 ") + def.story + " のレベルからレイヤを取れなかった");
			return nullptr;
		}
		probe.log(std::string("  階 ") + def.story +
				  ": GetStoryElevation=" + DfNum(gSDK->GetStoryElevation(hStory), 2) +
				  " 階内相対Z=" + DfNum(gSDK->GetStoryLevelElevation(hStory, levelType), 2) +
				  " レイヤ='" + DfName(hLayer) +
				  "' 高さ=" + DfNum(VWFC::VWObjects::VWLayerObj(hLayer).GetElevation(), 2));
		return hLayer;
	}

	// ---------------------------------------------------------------------------
	// 構造材（旧プローブと同じ欄。path3D なら Create3DPoly、そうでなければ VWPolygon2DObj）

	MCObjectHandle DtMakeMember(vwprobe::Report& probe, const std::string& name, bool path3D,
								double y, double pathZ)
	{
		MCObjectHandle hMember = nullptr;
		if (path3D)
		{
			MCObjectHandle hPath = gSDK->Create3DPoly();
			if (hPath != nullptr)
			{
				gSDK->Add3DVertex(hPath, WorldPt3(0.0, y, pathZ));
				gSDK->Add3DVertex(hPath, WorldPt3(4000.0, y, pathZ));
			}
			hMember = gSDK->CreateCustomObjectPath("StructuralMember", hPath, nullptr, true);
		}
		else
		{
			VWFC::VWObjects::VWPolygon2DObj path(
				{VWFC::Math::VWPoint2D(0.0, y), VWFC::Math::VWPoint2D(4000.0, y)});
			path.SetClosed(false);
			hMember = gSDK->CreateCustomObjectPath("StructuralMember", path.GetThisObject(),
												   nullptr, true);
		}
		if (hMember == nullptr)
		{
			probe.fail("部材 " + name + " を作れなかった（CreateCustomObjectPath が nil）");
			return nullptr;
		}
		VWFC::VWObjects::VWParametricObj pio(hMember);
		pio.SetParamValue("MemberType", "2"); // 2＝木
		pio.SetParamReal("MajorBreadth", 120.0);
		pio.SetParamReal("MajorDepth", 600.0);
		pio.SetParamReal("MinorBreadth", 120.0);
		pio.SetParamReal("MinorDepth", 300.0);
		gSDK->ResetObject(hMember);
		DfSetName(probe, hMember, name);
		return hMember;
	}

	void DtSetBounds(MCObjectHandle hMember, short boundStory, const char* levelType, double off0,
					 double off1)
	{
		VectorWorks::SStoryObjectData bound;
		bound.fBound = VectorWorks::eStoryObjectBound_Story;
		bound.fBoundStory = boundStory;
		bound.fLayerLevelType = levelType;
		bound.fOffset = off0;
		gSDK->SetObjectStoryBound(hMember, 0, bound);
		bound.fOffset = off1;
		gSDK->SetObjectStoryBound(hMember, 1, bound);
	}

	// ---------------------------------------------------------------------------
	// 部材の読み取り

	std::string DtPosZ(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		try
		{
			const VWFC::VWObjects::VWParametricObj pio(h);
			return DfNum(pio.GetObjectModelPos().z, 2);
		}
		catch (...)
		{
			return "(例外)";
		}
	}

	std::string DtParam(MCObjectHandle h, const char* param)
	{
		if (h == nullptr)
			return "(nil)";
		try
		{
			const VWFC::VWObjects::VWParametricObj pio(h);
			return "'" + DtOneLine(DtFromTX(pio.GetParamValue(param))) + "'";
		}
		catch (...)
		{
			return "(例外)";
		}
	}

	std::string DtCube(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		WorldCube cube;
		gSDK->GetObjectCube(h, cube);
		return "Z " + DfNum(cube.MinZ(), 2) + ".." + DfNum(cube.MaxZ(), 2) + " X " +
			   DfNum(cube.Left(), 1) + ".." + DfNum(cube.Right(), 1);
	}

	std::string DtBoundZ(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		std::string s = "n=" + std::to_string(gSDK->GetObjectStoryBoundsCount(h)) + " 解決Z " +
						DfNum(gSDK->GetObjectBoundElevation(h, 0), 2) + " / " +
						DfNum(gSDK->GetObjectBoundElevation(h, 1), 2);
		VectorWorks::SStoryObjectData data;
		if (gSDK->GetObjectStoryBound(h, 0, data))
			s += " ID0.fOffset=" + DfNum(data.fOffset, 2);
		return s;
	}

	std::string DtParentLayer(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		try
		{
			const VWFC::VWObjects::VWParametricObj pio(h);
			MCObjectHandle hLayer = pio.GetParentLayer();
			if (hLayer == nullptr)
				return "(親レイヤ nil)";
			return "'" + DfName(hLayer) +
				   "' 高さ=" + DfNum(VWFC::VWObjects::VWLayerObj(hLayer).GetElevation(), 2);
		}
		catch (...)
		{
			return "(例外)";
		}
	}

	// ---------------------------------------------------------------------------
	// データタグ

	// タグ（PIO）が描いた図形を辿って、テキストの件数と中身を出す（群の中も 3 段まで）。
	void DtCollectTexts(MCObjectHandle parent, std::vector<std::string>& out, int depth)
	{
		for (MCObjectHandle c = gSDK->FirstMemberObj(parent); c != nullptr; c = gSDK->NextObject(c))
		{
			const short type = gSDK->GetObjectTypeN(c);
			if (type == kTextNode)
				out.push_back(DtOneLine(DtFromTX(gSDK->GetTextChars(c))));
			else if (type == kGroupNode && depth < 3)
				DtCollectTexts(c, out, depth + 1);
		}
	}

	std::string DtDrawn(MCObjectHandle hTag)
	{
		if (hTag == nullptr)
			return "(nil)";
		std::vector<std::string> texts;
		DtCollectTexts(hTag, texts, 0);
		std::string s = "(テキスト " + std::to_string(texts.size()) + " 件)";
		for (const std::string& t : texts)
			s += " '" + (t.size() > 40 ? t.substr(0, 40) + "…" : t) + "'";
		return s;
	}

	std::string DtExtracted(MCObjectHandle hTag)
	{
		VectorWorks::Extension::IDataTagSupportPtr tagSupport(
			VectorWorks::Extension::IID_DataTagSupport);
		if (!tagSupport)
			return "(IDataTagSupport 無し)";
		VectorWorks::Extension::TXStringSTLPairArray extracted;
		tagSupport->GetDataTagExtractedData(hTag, extracted);
		if (extracted.empty())
			return "(0 件)";
		std::string out;
		for (const auto& pair : extracted)
			out += "'" + DtOneLine(DtFromTX(pair.second)) + "'";
		return out;
	}

	// 渡した後のレイアウトから、式を持たせたテキストを取り直す。
	MCObjectHandle DtTagText(MCObjectHandle hTag)
	{
		MCObjectHandle hLiveGroup = gSDK->GetCustomObjectProfileGroup(hTag);
		if (hLiveGroup == nullptr)
			return nullptr;
		for (MCObjectHandle m = gSDK->FirstMemberObj(hLiveGroup); m != nullptr;
			 m = gSDK->NextObject(m))
			if (gSDK->GetObjectTypeN(m) == kTextNode)
				return m;
		return nullptr;
	}

	// 旧プローブと同じ手順で式を持たせて評価する。
	bool DtApplyFormula(MCObjectHandle hTag, const char* formula)
	{
		VectorWorks::Extension::IDataTagSupportPtr tagSupport(
			VectorWorks::Extension::IID_DataTagSupport);
		VectorWorks::Extension::IDataTagTextLinkSupportPtr linkSupport(
			VectorWorks::Extension::IID_DataTagTextLinkSupport);
		MCObjectHandle hText = DtTagText(hTag);
		if (!tagSupport || !linkSupport || hText == nullptr)
			return false;
		linkSupport->SetIsLinked(hText, true);
		linkSupport->SetFormula(hText, TXString(formula), false);
		tagSupport->UpdateUserDefinedTextsUIDs(hTag);
		tagSupport->UpdateDataTag(hTag);
		gSDK->ResetObject(hTag);
		return true;
	}

	// タグを作って部材へ関連付け、式を持たせる（旧プローブの BuildTag ＋ Eval）。
	void DtMakeTag(vwprobe::Report& probe, MCObjectHandle hMember, MCObjectHandle hLayer,
				   const std::string& name, const char* formula, double x, double y)
	{
		VectorWorks::Extension::IDataTagSupportPtr tagSupport(
			VectorWorks::Extension::IID_DataTagSupport);
		if (!tagSupport || hMember == nullptr)
		{
			probe.fail("タグ " + name + ": IDataTagSupport か部材が無い");
			return;
		}
		MCObjectHandle hTag = gSDK->CreateCustomObject("Data Tag", WorldPt(x, y), 0.0, true);
		if (hTag == nullptr)
		{
			probe.fail("タグ " + name + " を作れなかった（CreateCustomObject が nil）");
			return;
		}
		if (hLayer != nullptr)
			gSDK->AddObjectToContainer(hTag, hLayer);
		const bool associated = tagSupport->AssociateWithObject(hTag, hMember);

		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		MCObjectHandle hText = gSDK->CreateTextBlock("T245", WorldPt(0.0, 0.0), false, 0.0);
		if (hGroup == nullptr || hText == nullptr)
		{
			probe.fail("タグ " + name + ": レイアウトを組めなかった");
			return;
		}
		gSDK->AddObjectToContainer(hText, hGroup);
		gSDK->SetCustomObjectProfileGroup(hTag, hGroup); // 中身を入れてから渡す
		DfSetName(probe, hTag, name);
		if (!DtApplyFormula(hTag, formula))
			probe.fail("タグ " + name + ": レイアウトにテキストが無い（式を持たせられない）");
		probe.log("  タグ " + name + " '" + DtOneLine(formula) +
				  "' Associate=" + DfBool(associated) + " → " + DtExtracted(hTag));
	}

	// タグを 2 通りに読む（raw: 触らずに / upd: 引き直して）。
	void DtReadTag(DeferredReadings& rs, const std::string& key, const std::string& tagName,
				   const char* formula)
	{
		MCObjectHandle hTag = DfFind(tagName);
		if (hTag == nullptr)
		{
			rs.emplace_back(key + ".raw", "(nil)");
			return;
		}
		rs.emplace_back(key + ".raw", "ext=" + DtExtracted(hTag) + " drawn=" + DtDrawn(hTag));
		// 掛け直しは T2 でだけ（T0 / T1 で書くと、T2 の raw が「VW が自分で変えたか」を
		// 表さなくなる）。T0 相当（変更の後に掛け直した値）は setup で取ってある。
		if (DfPhase() != 2)
			return;
		const bool applied = DtApplyFormula(hTag, formula);
		rs.emplace_back(key + ".upd", std::string(applied ? "" : "(式を持たせられない) ") +
										  "ext=" + DtExtracted(hTag) + " drawn=" + DtDrawn(hTag));
	}

	// ---------------------------------------------------------------------------
	// path: 3D パス と 2D パス

	const std::vector<std::pair<const char*, const char*>>& DtPathTokens()
	{
		static const std::vector<std::pair<const char*, const char*>> kTokens = {
			{"zts", "#ZTBBS#"},
			{"ztg", "#ZTBBG#"},
			{"bx", "#BX#"},
			{"by", "#BY#"},
			{"bz", "#BZ#"},
			{"se", "#StructuralMember#.#StartElevation#"},
			{"ee", "#StructuralMember#.#EndElevation#"}};
		return kTokens;
	}

	void DtSetupPath(vwprobe::Report& probe)
	{
		DtDefinePios();
		MCObjectHandle hLayer = DtMakeStoryLayer(probe, kDt2F);
		if (hLayer == nullptr)
			return;
		gSDK->SetCurrentLayer(hLayer);
		const struct
		{
			const char* name;
			bool path3D;
			double y;
		} kMembers[] = {{"dt-m3", true, 0.0}, {"dt-m2", false, 3000.0}};
		for (const auto& m : kMembers)
		{
			MCObjectHandle hMember = DtMakeMember(probe, m.name, m.path3D, m.y, 0.0);
			if (hMember == nullptr)
				continue;
			DtSetBounds(hMember, 0, kDt2F.levelType, -872.0, -40.0);
			gSDK->ResetObject(hMember);
			probe.log(std::string("  部材 ") + m.name + (m.path3D ? "（3D パス）" : "（2D パス）") +
					  ": バウンド {Story,0,FL,-872}/{Story,0,FL,-40} → 挿入点Z=" + DtPosZ(hMember) +
					  " " + DtCube(hMember) + " " + DtBoundZ(hMember));
			double x = 6000.0;
			for (const auto& [key, formula] : DtPathTokens())
			{
				DtMakeTag(probe, hMember, hLayer, std::string(m.name) + "-" + key, formula, x, m.y);
				x += 1500.0;
			}
		}
	}

	DeferredReadings DtMeasurePath()
	{
		DeferredReadings rs;
		for (const char* name : {"dt-m3", "dt-m2"})
		{
			const std::string k = std::string(name).substr(3); // "m3" / "m2"
			MCObjectHandle h = DfFind(name);
			rs.emplace_back(k + ".posZ", DtPosZ(h));
			rs.emplace_back(k + ".cube", DtCube(h));
			rs.emplace_back(k + ".bounds", DfBounds(h));
			rs.emplace_back(k + ".kids", DfChildren(h));
			rs.emplace_back(k + ".bound", DtBoundZ(h));
			for (const auto& [key, formula] : DtPathTokens())
				DtReadTag(rs, k + "." + key, std::string(name) + "-" + key, formula);
		}
		return rs;
	}

	// ---------------------------------------------------------------------------
	// dlg: DialogStartElevation に書いた値は追随するか

	void DtSetupDialog(vwprobe::Report& probe)
	{
		DtDefinePios();
		MCObjectHandle hLayer2F = DtMakeStoryLayer(probe, kDt2F);
		MCObjectHandle hLayerPlan = DtMakeStoryLayer(probe, kDtPlan);
		if (hLayer2F == nullptr || hLayerPlan == nullptr)
			return;
		// 旧プローブ（datatag-formula-grammar）は高さ 0 のレイヤで 3D パスの部材を作り、
		// 伏図レイヤへ AddObjectToContainer していた。その形を再現する。
		MCObjectHandle hLayer0 = gSDK->CreateLayer("DT245-L0", kLayerDesign);
		if (hLayer0 != nullptr)
			gSDK->SetCurrentLayer(hLayer0);

		// d1: 3D パス・バウンド＋パラメータ → +1000 動かす
		// d2: 3D パス・バウンド＋パラメータ → ID0 の fOffset を −1872 に
		// d3: 2D パス（実体あり）・バウンド＋パラメータ → ID0 の fOffset を −1872 に
		const struct
		{
			const char* name;
			bool path3D;
			bool move;
			double y;
		} kMembers[] = {{"dt-d1", true, true, 6000.0},
						{"dt-d2", true, false, 7500.0},
						{"dt-d3", false, false, 9000.0}};
		for (const auto& m : kMembers)
		{
			if (!m.path3D)
				gSDK->SetCurrentLayer(hLayerPlan);
			MCObjectHandle hMember = DtMakeMember(probe, m.name, m.path3D, m.y, 0.0);
			if (hMember == nullptr)
				continue;
			if (m.path3D)
				gSDK->AddObjectToContainer(hMember, hLayerPlan);
			DtSetBounds(hMember, 1, kDt2F.levelType, -872.0, -302.0); // 1 つ上の階＝2F
			VWFC::VWObjects::VWParametricObj pio(hMember);
			pio.SetParamReal("DialogStartElevation", -872.0);
			pio.SetParamReal("DialogEndElevation", -302.0);
			gSDK->ResetObject(hMember);
			probe.log(
				std::string("  部材 ") + m.name + (m.path3D ? "（3D パス）" : "（2D パス）") +
				": バウンド {Story,+1,FL,-872/-302} ＋ Dialog*Elevation=-872/-302 → 挿入点Z=" +
				DtPosZ(hMember) + " " + DtBoundZ(hMember) +
				" param=" + DtParam(hMember, "DialogStartElevation"));
			DtMakeTag(probe, hMember, hLayerPlan, std::string(m.name) + "-dse", kDtFormulaDse,
					  6000.0, m.y);

			if (m.move)
			{
				gSDK->MoveObject3D(hMember, 0.0, 0.0, 1000.0);
				gSDK->ResetObject(hMember);
				probe.log(std::string("  部材 ") + m.name +
						  ": MoveObject3D(+1000) → 挿入点Z=" + DtPosZ(hMember));
			}
			else
			{
				VectorWorks::SStoryObjectData bound;
				if (gSDK->GetObjectStoryBound(hMember, 0, bound))
				{
					bound.fOffset = -1872.0;
					gSDK->SetObjectStoryBound(hMember, 0, bound);
					gSDK->ResetObject(hMember);
				}
				probe.log(std::string("  部材 ") + m.name + ": ID0.fOffset を -1872 に → " +
						  DtBoundZ(hMember) + " 挿入点Z=" + DtPosZ(hMember));
			}
			// 旧プローブと同じく、変えた後に更新手順を掛け直して読む（＝これまでの T0）
			if (MCObjectHandle hTag = DfFind(std::string(m.name) + "-dse"))
			{
				DtApplyFormula(hTag, kDtFormulaDse);
				probe.log(std::string("  タグ ") + m.name + "-dse 掛け直し → " + DtExtracted(hTag));
			}
		}
	}

	DeferredReadings DtMeasureDialog()
	{
		DeferredReadings rs;
		for (const char* name : {"dt-d1", "dt-d2", "dt-d3"})
		{
			const std::string k = std::string(name).substr(3); // "d1" …
			MCObjectHandle h = DfFind(name);
			rs.emplace_back(k + ".param", DtParam(h, "DialogStartElevation"));
			rs.emplace_back(k + ".posZ", DtPosZ(h));
			rs.emplace_back(k + ".bound", DtBoundZ(h));
			rs.emplace_back(k + ".cube", DtCube(h));
			DtReadTag(rs, k + ".dse", std::string(name) + "-dse", kDtFormulaDse);
		}
		return rs;
	}

	// ---------------------------------------------------------------------------
	// lay: 移した部材は、移した先のレイヤの高さ変更に追随するか（#194 の段 R）

	void DtSetupLayer(vwprobe::Report& probe)
	{
		DtDefinePios();
		MCObjectHandle hL2 = gSDK->CreateLayer("DT245-L2", kLayerDesign);
		if (hL2 != nullptr)
			VWFC::VWObjects::VWLayerObj(hL2).SetElevation(900.0);
		MCObjectHandle hL3 = gSDK->CreateLayer("DT245-L3", kLayerDesign);
		if (hL3 != nullptr)
			VWFC::VWObjects::VWLayerObj(hL3).SetElevation(1100.0);
		if (hL2 == nullptr || hL3 == nullptr)
		{
			probe.fail("レイヤ DT245-L2 / L3 を作れなかった");
			return;
		}
		gSDK->SetCurrentLayer(hL2); // CreateLayer は作ったレイヤをアクティブにするので戻す
		MCObjectHandle hMember = DtMakeMember(probe, "dt-r", true, 12000.0, 0.0);
		if (hMember == nullptr)
			return;
		probe.log("  部材 dt-r: L2(900) で生まれた → 親=" + DtParentLayer(hMember) +
				  " 挿入点Z=" + DtPosZ(hMember));
		DtMakeTag(probe, hMember, hL2, "dt-r-ipz", "#IPZ#", 6000.0, 12000.0);
		DtMakeTag(probe, hMember, hL2, "dt-r-ipzl", "#IPZL#", 7500.0, 12000.0);

		gSDK->AddObjectToContainer(hMember, hL3);
		for (const char* tag : {"dt-r-ipz", "dt-r-ipzl"})
			if (MCObjectHandle hTag = DfFind(tag))
				gSDK->AddObjectToContainer(hTag, hL3);
		gSDK->ResetObject(hMember);
		probe.log("  AddObjectToContainer(L3=1100) → 親=" + DtParentLayer(hMember) +
				  " 挿入点Z=" + DtPosZ(hMember));
		VWFC::VWObjects::VWLayerObj(hL3).SetElevation(1300.0);
		probe.log("  L3.SetElevation(1300) → 親=" + DtParentLayer(hMember) +
				  " 挿入点Z=" + DtPosZ(hMember));
		for (const auto& [tag, formula] :
			 {std::pair<const char*, const char*>{"dt-r-ipz", "#IPZ#"}, {"dt-r-ipzl", "#IPZL#"}})
			if (MCObjectHandle hTag = DfFind(tag))
			{
				DtApplyFormula(hTag, formula);
				probe.log(std::string("  タグ ") + tag + " 掛け直し → " + DtExtracted(hTag));
			}
	}

	DeferredReadings DtMeasureLayer()
	{
		DeferredReadings rs;
		MCObjectHandle h = DfFind("dt-r");
		rs.emplace_back("r.parent", DtParentLayer(h));
		rs.emplace_back("r.posZ", DtPosZ(h));
		rs.emplace_back("r.cube", DtCube(h));
		DtReadTag(rs, "r.ipz", "dt-r-ipz", "#IPZ#");
		DtReadTag(rs, "r.ipzl", "dt-r-ipzl", "#IPZL#");
		return rs;
	}

	// ---------------------------------------------------------------------------
	// sty: ストーリ由来のレイヤへ SetElevation(5000)

	void DtSetupStory(vwprobe::Report& probe)
	{
		DtDefinePios();
		MCObjectHandle hLayer = DtMakeStoryLayer(probe, kDtStoryS);
		if (hLayer == nullptr)
			return;
		gSDK->SetCurrentLayer(hLayer);
		MCObjectHandle hMember = DtMakeMember(probe, "dt-t", true, 15000.0, 0.0);
		if (hMember == nullptr)
			return;
		probe.log("  部材 dt-t: ストーリ由来レイヤで生まれた → 親=" + DtParentLayer(hMember) +
				  " 挿入点Z=" + DtPosZ(hMember));
		DtMakeTag(probe, hMember, hLayer, "dt-t-ipz", "#IPZ#", 6000.0, 15000.0);
		DtMakeTag(probe, hMember, hLayer, "dt-t-ipzs", "#IPZS#", 7500.0, 15000.0);

		VWFC::VWObjects::VWLayerObj(hLayer).SetElevation(5000.0);
		for (const auto& [tag, formula] :
			 {std::pair<const char*, const char*>{"dt-t-ipz", "#IPZ#"}, {"dt-t-ipzs", "#IPZS#"}})
			if (MCObjectHandle hTag = DfFind(tag))
			{
				DtApplyFormula(hTag, formula);
				probe.log(std::string("  タグ ") + tag + " 掛け直し → " + DtExtracted(hTag));
			}
		MCObjectHandle hStory = DfFind(kDtStoryS.story);
		probe.log(
			"  レイヤへ SetElevation(5000) → レイヤ高さ=" +
			DfNum(VWFC::VWObjects::VWLayerObj(hLayer).GetElevation(), 2) +
			" 階=" + (hStory != nullptr ? DfNum(gSDK->GetStoryElevation(hStory), 2) : "(nil)") +
			" 階内相対Z=" +
			(hStory != nullptr
				 ? DfNum(gSDK->GetStoryLevelElevation(hStory, TXString(kDtStoryS.levelType)), 2)
				 : "(nil)"));
	}

	DeferredReadings DtMeasureStory()
	{
		DeferredReadings rs;
		MCObjectHandle hStory = DfFind(kDtStoryS.story);
		MCObjectHandle hLayer = DtFindStoryLayer(kDtStoryS);
		rs.emplace_back("storyZ",
						hStory != nullptr ? DfNum(gSDK->GetStoryElevation(hStory), 2) : "(nil)");
		rs.emplace_back(
			"levelZ",
			hStory != nullptr
				? DfNum(gSDK->GetStoryLevelElevation(hStory, TXString(kDtStoryS.levelType)), 2)
				: "(nil)");
		rs.emplace_back("layerZ", hLayer != nullptr
									  ? DfNum(VWFC::VWObjects::VWLayerObj(hLayer).GetElevation(), 2)
									  : "(nil)");
		MCObjectHandle h = DfFind("dt-t");
		rs.emplace_back("t.posZ", DtPosZ(h));
		DtReadTag(rs, "t.ipz", "dt-t-ipz", "#IPZ#");
		DtReadTag(rs, "t.ipzs", "dt-t-ipzs", "#IPZS#");
		return rs;
	}
} // namespace

VW_PROBE("deferred-datatag", "タグと高さを後で読み直す", "タグの高さの結論を後で測り直す")
{
	const std::vector<DeferredCase> cases = {
		{"path", "構造材 3D パス vs 2D パス: #ZTBB#・#BX/BY/BZ#・Start/EndElevation・描いた文字",
		 &DtSetupPath, &DtMeasurePath},
		{"dlg", "DialogStartElevation(-872) は +1000 移動 / fOffset -1872 に追随するか",
		 &DtSetupDialog, &DtMeasureDialog},
		{"lay", "L2(900)→L3(1100) へ移した部材は L3=1300 に追随するか（#IPZ#/#IPZL#）",
		 &DtSetupLayer, &DtMeasureLayer},
		{"sty", "ストーリ由来レイヤへ SetElevation(5000): 階内相対Z と #IPZS#", &DtSetupStory,
		 &DtMeasureStory}};
	RunDeferred(probe, "deferred-datatag", cases);
}
