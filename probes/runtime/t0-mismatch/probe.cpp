//
//	probes/runtime/t0-mismatch/probe.cpp
//
//	[issue #249] #245 で「書いた直後（T0）から過去の実測と違った」3 件を切り分ける。
//	腕ごとに自分用の新しい図面を開き（空＝OpenDocumentPath(nullptr) か、既定のテンプレート
//	Default.sta か）、終わったら保存せずに閉じる。前の腕の状態を持ち越さないため。
//
//	  L … 図面ラベル（#177）。空の Title のラベルが 1032 を消すか、ラベルが 1032 を拾うか。
//	      #177 の手順（クラスを戻さない・SetPointObjectPos しない・名前を付けない・空の枚が先）
//	      と #245 の手順を 1 つずつ入れ替える。各段で Title / Link State / 1032 を出す。
//	      腕の最後に、どこにも入れないラベルを 1 本作って Title を読む（引き継ぎの有無）。
//	  D … 寸法のクラスの文字スタイル（#157）。寸法が作られたときにどのクラスへ入ったか
//	      （varCreateDimsInDimClass・寸法クラス）と、どのクラスを書き換えれば
//	      GetTextStyleRef が動くか。#157 の手順（作ったまま）と #245 の手順
//	      （SetObjectClass + SetTextStyleByClass）を並べる。
//	  A … 寸法の関連付け（#138）。空の図面で AssociateLinearDimension が成立しない件。
//	      図面の診断（レイヤ・縮尺・環境設定・寸法のクラス）と、置き方の変種。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	// ---------------------------------------------------------------------------
	// 小道具

	std::string TmNum(double v, int prec = 4)
	{
		char buf[64];
		std::snprintf(buf, sizeof buf, "%.*f", prec, v);
		return buf;
	}

	std::string TmYes(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string TmStr(const TXString& s)
	{
		const char* p = static_cast<const char*>(s);
		return p != nullptr ? std::string(p) : std::string();
	}

	std::string TmIndexName(InternalIndex index)
	{
		if (index == 0)
			return "";
		TXString name;
		gSDK->InternalIndexToNameN(index, name);
		return TmStr(name);
	}

	std::string TmRef(InternalIndex ref)
	{
		return std::to_string(static_cast<long long>(ref)) + "(" + TmIndexName(ref) + ")";
	}

	std::string TmBoolPref(short selector)
	{
		union
		{
			Boolean asBoolean;
			Sint32 asWide;
		} u;
		u.asWide = 0;
		if (!gSDK->GetProgramVariable(selector, &u))
			return "(読めない)";
		return u.asBoolean ? "1" : "0";
	}

	void TmSetBoolPref(short selector, bool on)
	{
		Boolean v = on ? 1 : 0;
		gSDK->SetProgramVariable(selector, &v);
	}

	std::string TmText(MCObjectHandle h, short selector)
	{
		if (h == nullptr)
			return "(nil)";
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(h, selector, block))
			return "(読めない)";
		TXString s;
		block.GetTXString(s);
		return "「" + TmStr(s) + "」";
	}

	bool TmSetReal(MCObjectHandle h, short selector, double value)
	{
		TVariableBlock block;
		block = static_cast<Real64>(value);
		return h != nullptr && gSDK->SetObjectVariable(h, selector, block) != 0;
	}

	std::string TmReal(MCObjectHandle h, short selector)
	{
		TVariableBlock block;
		Real64 v = 0.0;
		if (h == nullptr || !gSDK->GetObjectVariable(h, selector, block) || !block.GetReal64(v))
			return "(読めない)";
		return TmNum(v);
	}

	std::string TmClassStr(InternalIndex cls)
	{
		if (cls == 0)
			return "(0)";
		return TmIndexName(cls) + " use=" + TmYes(gSDK->GetClUseTextStyle(cls)) +
			   " ref=" + TmRef(gSDK->GetClTextStyleRef(cls));
	}

	std::string TmLayerStr(MCObjectHandle layer)
	{
		if (layer == nullptr)
			return "(nil)";
		TXString name;
		gSDK->GetObjectName(layer, name);
		double_gs scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return "「" + TmStr(name) + "」 1/" + TmNum(static_cast<double>(scale), 1);
	}

	// ---------------------------------------------------------------------------
	// 自分用の図面

	std::vector<std::pair<Sint32, bool>> TmOpenFiles()
	{
		std::vector<std::pair<Sint32, bool>> out;
		VectorWorks::TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
			out.emplace_back(files[i].fFileRef, files[i].fIsActive);
		return out;
	}

	bool TmIsOpen(Sint32 ref)
	{
		for (const auto& [r, active] : TmOpenFiles())
			if (r == ref)
				return true;
		return false;
	}

	std::string TmDefaultTemplate()
	{
#if GS_MAC
		const char* home = std::getenv("HOME");
		if (home == nullptr)
			return "";
		const std::filesystem::path p = std::filesystem::path(home) /
										"Library/Application Support/Vectorworks/2026/Libraries/"
										"Defaults/Templates/Default.sta";
#else
		const char* appdata = std::getenv("APPDATA");
		if (appdata == nullptr)
			return "";
		const std::filesystem::path p = std::filesystem::path(appdata) /
										"Nemetschek/Vectorworks/2026/Libraries/Defaults/Templates/"
										"Default.sta";
#endif
		std::error_code ec;
		return std::filesystem::exists(p, ec) ? p.string() : "";
	}

	// 開けたら fileRef、開けなければ 0。fromTemplate なのにテンプレートが無ければ開かない。
	Sint32 TmOpen(vwprobe::Report& probe, bool fromTemplate)
	{
		const size_t before = TmOpenFiles().size();
		if (fromTemplate)
		{
			const std::string tpl = TmDefaultTemplate();
			if (tpl.empty())
			{
				probe.log("  既定のテンプレート（Default.sta）が見つからない——この腕は飛ばす");
				return 0;
			}
			VectorWorks::Filing::IFileIdentifierPtr fileID(VectorWorks::Filing::IID_FileIdentifier);
			if (fileID && fileID->Set(TXString(tpl.c_str())) == kVCOMError_NoError)
				(void)gSDK->OpenDocumentPath(fileID, false);
		}
		else
		{
			(void)gSDK->OpenDocumentPath(nullptr, false);
		}
		if (TmOpenFiles().size() != before + 1)
		{
			probe.fail("自分用の新規図面を開けなかった");
			return 0;
		}
		for (const auto& [ref, active] : TmOpenFiles())
			if (active)
				return ref;
		return 0;
	}

	void TmClose(vwprobe::Report& probe, Sint32 ref)
	{
		if (ref == 0)
			return;
		(void)gSDK->SwitchToOpenFile(ref);
		(void)gSDK->EndUndoEvent();
		(void)gSDK->CloseDocument(); // 戻り値は当てにならない（Findings「Documents」）
		if (TmIsOpen(ref))
			probe.log("  ※自分の図面を閉じられなかった（ref=" + std::to_string(ref) + "）");
	}

	const char* TmOrigin(bool fromTemplate)
	{
		return fromTemplate ? "テンプレート" : "空";
	}

	// ---------------------------------------------------------------------------
	// L: 図面ラベル

	const char* const kTmLabel = "Drawing Label2";
	const double kTmScale = 50.0;
	const double kTmTenPt = 10.0 * 25.4 / 72.0; // 3.52778

	std::string TmLabelParams(MCObjectHandle h)
	{
		VWFC::VWObjects::VWParametricObj obj(h);
		RefNumber style = 0;
		const bool ok = gSDK->GetPluginObjectStyle(h, style) != 0;
		return "Title=「" + TmStr(obj.GetParamString("Title")) +
			   "」 LS=" + std::to_string(obj.GetParamLong("Link State")) +
			   " style=" + (ok ? TmRef(static_cast<InternalIndex>(style)) : std::string("(false)"));
	}

	void TmCountTexts(MCObjectHandle container, MCObjectHandle layout, int depth, int& count,
					  std::string& first)
	{
		if (container == nullptr || depth > 4)
			return;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nullptr;
			 m = gSDK->NextObject(m))
		{
			if (m == layout)
				continue;
			if (gSDK->GetObjectTypeN(m) == 10)
			{
				if (++count == 1)
					first = "「" + TmStr(gSDK->GetTextChars(m)) + "」";
			}
			TmCountTexts(m, layout, depth + 1, count, first);
		}
	}

	std::string TmDrawn(MCObjectHandle h)
	{
		int count = 0;
		std::string first;
		TmCountTexts(h, gSDK->GetCustomObjectProfileGroup(h), 0, count, first);
		return "描いた文字 " + std::to_string(count) + " " + first;
	}

	// #177 / #245 と同じ: 既定のレイアウトからテキストと線を 1 つずつ複製し、10pt を与える。
	void TmRebuildLayout(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		MCObjectHandle hOld = gSDK->GetCustomObjectProfileGroup(h);
		MCObjectHandle hGroup = (hOld != nullptr) ? gSDK->CreateGroup(false) : nullptr;
		if (hGroup == nullptr)
		{
			probe.log("    " + tag + ": レイアウトを組み直せない（元のレイアウトが nil）");
			return;
		}
		bool tookText = false;
		bool tookLine = false;
		for (MCObjectHandle m = gSDK->FirstMemberObj(hOld); m != nullptr; m = gSDK->NextObject(m))
		{
			const short type = gSDK->GetObjectTypeN(m);
			const bool wantText = (type == 10 && !tookText);
			const bool wantLine = (type == 2 && !tookLine);
			if (!wantText && !wantLine)
				continue;
			MCObjectHandle dup = gSDK->DuplicateObject(m);
			if (dup == nullptr)
				continue;
			gSDK->AddObjectToContainer(dup, hGroup);
			if (wantText)
			{
				tookText = true;
				const Sint32 len = gSDK->GetTextLength(dup);
				gSDK->SetTextSize(dup, 0, len > 0 ? len : 1, static_cast<WorldCoord>(kTmTenPt));
			}
			else
				tookLine = true;
		}
		if (!tookText)
			probe.log("    " + tag + ": 元のレイアウトにテキストが無かった");
		gSDK->SetCustomObjectProfileGroup(h, hGroup);
	}

	struct TmPlace
	{
		const char* tag;
		const char* preset; // 置く前に 1032 へ入れる値（nullptr なら入れない）
		const char* title;	// ラベルの Title へ書く値（nullptr なら書かない）
		bool showClasses; // ビューポートのクラスを全部表示へ戻す（#245 の手順）
		bool setPos;	  // 注釈へ入れた後に SetPointObjectPos(0,0)（#245 の手順）
		bool nameVp;	  // ビューポートに名前を付ける（#245 の手順）
	};

	// 1 枚のビューポートを作り、ラベルを 1 本置く。段ごとに Title / LS / 1032 を出す。
	void TmPlaceLabel(vwprobe::Report& probe, MCObjectHandle sheet, MCObjectHandle design,
					  const TmPlace& p, int index)
	{
		const std::string tag = p.tag;
		MCObjectHandle vp = gSDK->CreateViewport(sheet);
		if (vp == nullptr)
		{
			probe.fail(tag + ": CreateViewport が nil");
			return;
		}
		gSDK->SetViewportLayerVisibility(vp, design, VWFC::VWObjects::kLayerVisibilityNormal);
		TVariableBlock scaleVar;
		scaleVar = static_cast<Real64>(kTmScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
		if (p.preset != nullptr)
		{
			TVariableBlock descVar;
			descVar = TXString(p.preset);
			gSDK->SetObjectVariable(vp, ovViewportDescription, descVar);
		}
		if (p.showClasses)
			gSDK->ForEachClass(
				true, [vp](MCObjectHandle cls)
				{ gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0); });
		gSDK->UpdateViewport(vp);
		if (index > 0)
			gSDK->MoveObject(vp, static_cast<WorldCoord>(300.0 * index), 0);
		if (p.nameVp)
			(void)gSDK->SetObjectName(vp, TXString(("tm-" + tag).c_str()));
		probe.log("  [" + tag + "] VP を作った 1032=" + TmText(vp, ovViewportDescription) +
				  "（クラス表示=" + TmYes(p.showClasses) + " 位置=" + TmYes(p.setPos) +
				  " 名前=" + TmYes(p.nameVp) + "）");

		MCObjectHandle h =
			gSDK->CreateCustomObject(kTmLabel, WorldPt(0, -3000.0 * index), 0.0, false);
		if (h == nullptr)
		{
			probe.fail(tag + ": CreateCustomObject が nil");
			return;
		}
		probe.log("    作った直後 " + TmLabelParams(h));
		TmRebuildLayout(probe, tag, h);
		const bool added = gSDK->AddViewportAnnotationObject(vp, h) != 0;
		probe.log("    注釈へ入れた=" + TmYes(added) + " " + TmLabelParams(h) +
				  " / 1032=" + TmText(vp, ovViewportDescription));
		if (p.setPos)
		{
			VWFC::VWObjects::VWParametricObj(h).SetPointObjectPos(VWPoint2D(0, 0));
			probe.log("    SetPointObjectPos の後 " + TmLabelParams(h) +
					  " / 1032=" + TmText(vp, ovViewportDescription));
		}
		if (p.title != nullptr)
		{
			VWFC::VWObjects::VWParametricObj(h).SetParamValue("Title", p.title);
			probe.log("    Title を書いた " + TmLabelParams(h) +
					  " / 1032=" + TmText(vp, ovViewportDescription));
		}
		gSDK->ResetObject(h);
		probe.log("    ResetObject の後 " + TmLabelParams(h) +
				  " / 1032=" + TmText(vp, ovViewportDescription) + " / " + TmDrawn(h));
	}

	struct TmLabelArm
	{
		const char* id;
		const char* what;
		bool fromTemplate;
		std::vector<TmPlace> places;
	};

	void TmRunLabelArm(vwprobe::Report& probe, const TmLabelArm& arm)
	{
		probe.log("");
		probe.log("=== " + std::string(arm.id) + "（" + TmOrigin(arm.fromTemplate) +
				  "の図面）: " + arm.what + " ===");
		const Sint32 ref = TmOpen(probe, arm.fromTemplate);
		if (ref == 0)
			return;
		gSDK->DefineCustomObject(kTmLabel, kCustomObjectPrefNever);
		RefNumber toolStyle = 0;
		const bool okTool = gSDK->GetPluginStyleForTool(kTmLabel, toolStyle);
		probe.log("  ツールのスタイル: " +
				  (okTool ? TmRef(static_cast<InternalIndex>(toolStyle)) : std::string("(false)")));

		MCObjectHandle design = gSDK->CreateLayer("tm-design", kLayerDesign);
		MCObjectHandle sheet = gSDK->CreateLayer("tm-sheet", kLayerSheet);
		if (design == nullptr || sheet == nullptr)
		{
			probe.fail("CreateLayer が nil");
			TmClose(probe, ref);
			return;
		}
		gSDK->SetLayerScaleN(design, kTmScale);
		gSDK->SetCurrentLayer(design);
		gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 10000.0, 10000.0);
		gSDK->SetCurrentLayer(sheet);

		for (size_t i = 0; i < arm.places.size(); ++i)
			TmPlaceLabel(probe, sheet, design, arm.places[i], static_cast<int>(i));

		// どこにも入れないラベルを 1 本作り、生まれたときの Title を読む（引き継ぎの有無）。
		MCObjectHandle stray = gSDK->CreateCustomObject(kTmLabel, WorldPt(0, 9000), 0.0, false);
		if (stray != nullptr)
		{
			probe.log("  次に作ったラベル（注釈へ入れない）: " + TmLabelParams(stray));
			gSDK->DeleteObject(stray);
		}
		TmClose(probe, ref);
	}

	// ---------------------------------------------------------------------------
	// D: 寸法のクラスの文字スタイル

	std::string TmDimStyle(MCObjectHandle dim)
	{
		if (dim == nullptr)
			return "(nil)";
		return "class=" + TmIndexName(gSDK->GetObjectClass(dim)) +
			   " ref=" + TmRef(gSDK->GetTextStyleRef(dim)) +
			   (gSDK->GetTextStyleByClass(dim) ? " cls" : " 明示") +
			   " font=" + TmReal(dim, ovDimFontSize);
	}

	MCObjectHandle TmMakeDim(double y)
	{
		MCObjectHandle dim =
			gSDK->CreateLinearDimension(WorldPt(0.0, y), WorldPt(1000.0, y),
										static_cast<WorldCoord>(500.0), 0, Vector2(0, 0), 0);
		return dim;
	}

	InternalIndex TmMakeTextStyle(vwprobe::Report& probe, const char* name, double pt)
	{
		MCObjectHandle style = gSDK->CreateTextStyleResource(TXString(name));
		if (style == nullptr)
		{
			probe.fail(std::string("CreateTextStyleResource が nil: ") + name);
			return 0;
		}
		TmSetReal(style, ovTextStyleSize, pt / 72.0);
		return gSDK->GetObjectInternalIndex(style);
	}

	std::string TmStandardStr()
	{
		short stdIndex = -99;
		gSDK->GetProgramVariable(varDimStandard, &stdIndex);
		std::string out = "varDimStandard=" + std::to_string(stdIndex);
		TVariableBlock nameBlock;
		TXString name;
		if (gSDK->GetDimensionStandardVariable(stdIndex, dimStdstandardName, nameBlock))
			nameBlock.GetTXString(name);
		out += "「" + TmStr(name) + "」";
		TVariableBlock styleBlock;
		Sint32 ref = 0;
		if (gSDK->GetDimensionStandardVariable(stdIndex, dimStdTextStyle, styleBlock) &&
			styleBlock.GetSint32(ref))
			out += " 規格の文字スタイル=" + TmRef(static_cast<InternalIndex>(ref));
		return out;
	}

	void TmRunDimArm(vwprobe::Report& probe, bool fromTemplate)
	{
		probe.log("");
		probe.log("=== D（" + std::string(TmOrigin(fromTemplate)) +
				  "の図面）: 寸法が入るクラスと、GetTextStyleRef が追うクラス ===");
		const Sint32 ref = TmOpen(probe, fromTemplate);
		if (ref == 0)
			return;
		const InternalIndex active = gSDK->GetActiveClass();
		const InternalIndex dimClass = gSDK->GetDimensionClassID();
		probe.log("  varCreateDimsInDimClass(44)=" + TmBoolPref(varCreateDimsInDimClass) + " / " +
				  TmStandardStr());
		probe.log("  アクティブクラス: " + TmClassStr(active));
		probe.log("  寸法クラス（GetDimensionClassID）: " + TmClassStr(dimClass));
		probe.log("  アクティブレイヤ: " + TmLayerStr(gSDK->GetActiveLayer()));

		const InternalIndex tsA = TmMakeTextStyle(probe, "tm-ts-active", 20.0);
		const InternalIndex tsD = TmMakeTextStyle(probe, "tm-ts-dimcls", 30.0);
		const InternalIndex tsN = TmMakeTextStyle(probe, "tm-ts-new", 40.0);
		if (tsA == 0 || tsD == 0 || tsN == 0)
		{
			TmClose(probe, ref);
			return;
		}

		// D1: #157 の手順——作ったまま。アクティブクラス、続いて寸法の入ったクラスを書き換える。
		probe.log("  -- D1: #157 の手順（作ったまま）");
		MCObjectHandle d1 = TmMakeDim(0.0);
		probe.log("    作った直後 " + TmDimStyle(d1));
		const InternalIndex d1Class = d1 != nullptr ? gSDK->GetObjectClass(d1) : 0;
		probe.log("    寸法の入ったクラス: " + TmClassStr(d1Class) +
				  "（アクティブと同じ=" + TmYes(d1Class == active) +
				  " 寸法クラスと同じ=" + TmYes(d1Class == dimClass) + "）");
		if (d1Class != active)
		{
			gSDK->SetClUseTextStyle(active, true);
			gSDK->SetClTextStyleRef(active, tsA);
			probe.log("    アクティブクラスへ tm-ts-active を当てた → " + TmDimStyle(d1));
		}
		gSDK->SetClUseTextStyle(d1Class, true);
		gSDK->SetClTextStyleRef(d1Class, tsD);
		probe.log("    寸法の入ったクラスへ tm-ts-dimcls を当てた → " + TmDimStyle(d1));
		gSDK->ResetObject(d1);
		probe.log("    ResetObject の後 → " + TmDimStyle(d1));
		MCObjectHandle d1b = TmMakeDim(2000.0);
		probe.log("    その後に作った寸法 → " + TmDimStyle(d1b));

		// D2: #245 の手順——専用クラスへ入れて SetTextStyleByClass、クラスを書き換える。
		probe.log("  -- D2: #245 の手順（専用クラス + SetTextStyleByClass）");
		const InternalIndex cls2 = gSDK->AddClass(TXString("tm-cls2"));
		MCObjectHandle d2 = TmMakeDim(4000.0);
		gSDK->SetObjectClass(d2, cls2);
		probe.log("    SetObjectClass だけ → " + TmDimStyle(d2));
		gSDK->SetTextStyleByClass(d2);
		probe.log("    SetTextStyleByClass の後 → " + TmDimStyle(d2));
		gSDK->SetClUseTextStyle(cls2, true);
		gSDK->SetClTextStyleRef(cls2, tsN);
		probe.log("    クラスへ tm-ts-new を当てた → " + TmDimStyle(d2));

		// D3: 専用クラスへ入れるが SetTextStyleByClass は呼ばない。
		probe.log("  -- D3: 専用クラスへ入れるだけ（SetTextStyleByClass を呼ばない）");
		const InternalIndex cls3 = gSDK->AddClass(TXString("tm-cls3"));
		MCObjectHandle d3 = TmMakeDim(6000.0);
		gSDK->SetObjectClass(d3, cls3);
		probe.log("    SetObjectClass だけ → " + TmDimStyle(d3));
		gSDK->SetClUseTextStyle(cls3, true);
		gSDK->SetClTextStyleRef(cls3, tsN);
		probe.log("    クラスへ tm-ts-new を当てた → " + TmDimStyle(d3));

		// D4: 文字スタイルを持たないクラスへ入れた寸法は何を返すか（#245 の「前」= 0 の確認）。
		probe.log("  -- D4: 文字スタイルを持たないクラスへ入れて SetTextStyleByClass");
		const InternalIndex cls4 = gSDK->AddClass(TXString("tm-cls4"));
		MCObjectHandle d4 = TmMakeDim(8000.0);
		probe.log("    作った直後 → " + TmDimStyle(d4));
		gSDK->SetObjectClass(d4, cls4);
		gSDK->SetTextStyleByClass(d4);
		probe.log("    tm-cls4（" + TmClassStr(cls4) + "）へ入れた後 → " + TmDimStyle(d4));
		TmClose(probe, ref);
	}

	// ---------------------------------------------------------------------------
	// A: 寸法の関連付け

	enum class TmAssocWay
	{
		SelectFalse, // 線 2 本を選択して AssociateLinearDimension(dim, false)（#245 c5）
		NoSelect,	 // 選択せずに false
		SelectTrue	 // 選択して true
	};

	const char* TmWayName(TmAssocWay w)
	{
		switch (w)
		{
		case TmAssocWay::SelectFalse:
			return "選択して false";
		case TmAssocWay::NoSelect:
			return "選択せずに false";
		case TmAssocWay::SelectTrue:
			return "選択して true";
		}
		return "?";
	}

	void TmAssocOnce(vwprobe::Report& probe, const std::string& tag, double y, TmAssocWay way)
	{
		MCObjectHandle lineA = gSDK->CreateLine(WorldPt(0.0, y), WorldPt(0.0, y - 500.0));
		MCObjectHandle lineB = gSDK->CreateLine(WorldPt(3000.0, y), WorldPt(3000.0, y - 500.0));
		MCObjectHandle dim =
			gSDK->CreateLinearDimension(WorldPt(0.0, y), WorldPt(3000.0, y),
										static_cast<WorldCoord>(600.0), 0, Vector2(0, 0), 0);
		if (lineA == nullptr || lineB == nullptr || dim == nullptr)
		{
			probe.fail(tag + ": 線か寸法を作れなかった");
			return;
		}
		gSDK->DeselectAll();
		if (way != TmAssocWay::NoSelect)
		{
			gSDK->SelectObject(lineA, true);
			gSDK->SelectObject(lineB, true);
		}
		gSDK->AssociateLinearDimension(dim, way == TmAssocWay::SelectTrue);
		gSDK->DeselectAll();
		probe.log("    " + tag + " y=" + TmNum(y, 0) + " " + TmWayName(way) +
				  " → HasConstraint(寸法)=" + TmYes(gSDK->HasConstraint(dim) != 0) +
				  " (線A)=" + TmYes(gSDK->HasConstraint(lineA) != 0) + " / 寸法のレイヤ " +
				  TmLayerStr(gSDK->ParentObject(dim)) +
				  " クラス=" + TmIndexName(gSDK->GetObjectClass(dim)));
	}

	void TmAssocDiag(vwprobe::Report& probe)
	{
		probe.log("  varAssociateDims(28)=" + TmBoolPref(varAssociateDims) +
				  " varAutoAssociateDims(134)=" + TmBoolPref(varAutoAssociateDims) +
				  " varCreateDimsInDimClass(44)=" + TmBoolPref(varCreateDimsInDimClass));
		int layers = 0;
		gSDK->ForEachLayerN([&layers](MCObjectHandle) { ++layers; });
		probe.log("  アクティブレイヤ " + TmLayerStr(gSDK->GetActiveLayer()) +
				  " / レイヤ数=" + std::to_string(layers) +
				  " / アクティブクラス=" + TmIndexName(gSDK->GetActiveClass()) +
				  " / 寸法クラス=" + TmIndexName(gSDK->GetDimensionClassID()));
	}

	void TmRunAssocArm(vwprobe::Report& probe, bool fromTemplate)
	{
		probe.log("");
		probe.log("=== A（" + std::string(TmOrigin(fromTemplate)) +
				  "の図面）: AssociateLinearDimension が成立するか ===");
		const Sint32 ref = TmOpen(probe, fromTemplate);
		if (ref == 0)
			return;
		TmAssocDiag(probe);
		if (TmBoolPref(varAssociateDims) != "1")
		{
			TmSetBoolPref(varAssociateDims, true);
			probe.log("  varAssociateDims を 1 にした → " + TmBoolPref(varAssociateDims));
		}
		probe.log("  -- そのままのレイヤで");
		TmAssocOnce(probe, "A1", 60000.0, TmAssocWay::SelectFalse);
		TmAssocOnce(probe, "A2", 0.0, TmAssocWay::NoSelect);
		TmAssocOnce(probe, "A3", 4000.0, TmAssocWay::SelectTrue);

		probe.log("  -- アクティブレイヤの縮尺を 1/50 にして");
		MCObjectHandle layer = gSDK->GetActiveLayer();
		double_gs scaleBefore = 1.0;
		gSDK->GetLayerScaleN(layer, scaleBefore);
		gSDK->SetLayerScaleN(layer, 50.0);
		TmAssocOnce(probe, "A4", 8000.0, TmAssocWay::SelectFalse);
		gSDK->SetLayerScaleN(layer, scaleBefore);

		probe.log("  -- 新しく作ったデザインレイヤで");
		MCObjectHandle design = gSDK->CreateLayer("tm-assoc", kLayerDesign);
		if (design != nullptr)
		{
			gSDK->SetCurrentLayer(design);
			TmAssocOnce(probe, "A5", 12000.0, TmAssocWay::SelectFalse);
		}

		probe.log("  -- varCreateDimsInDimClass を反転して");
		const bool dimInClass = TmBoolPref(varCreateDimsInDimClass) == "1";
		TmSetBoolPref(varCreateDimsInDimClass, !dimInClass);
		probe.log("    varCreateDimsInDimClass → " + TmBoolPref(varCreateDimsInDimClass));
		TmAssocOnce(probe, "A6", 16000.0, TmAssocWay::SelectFalse);
		TmSetBoolPref(varCreateDimsInDimClass, dimInClass);
		TmClose(probe, ref);
	}
} // namespace

VW_PROBE("t0-mismatch", "T0 の食い違い（#245）", "ラベル・文字スタイル・関連付けを振り分ける")
{
	// #177 の手順: クラスを戻さない・SetPointObjectPos しない・名前を付けない。
	const TmPlace e177{"E", nullptr, nullptr, false, false, false};
	const TmPlace pre177{"pre", "VP の図面タイトル", nullptr, false, false, false};
	const TmPlace t177{"T", nullptr, "調査用の図面タイトル", false, false, false};
	// #245 の手順: クラスを全部表示・SetPointObjectPos(0,0)・名前を付ける。
	const TmPlace e245{"E", nullptr, nullptr, true, true, true};
	const TmPlace pre245{"pre", "VP の図面タイトル", nullptr, true, true, true};
	const TmPlace t245{"T", nullptr, "調査用の図面タイトル", true, true, true};
	const TmPlace hand245{"H", nullptr, "手で書いた題", true, true, true};

	const std::vector<TmLabelArm> labelArms = {
		{"L1", "#245 の並び（pre → E → T）と手順", true, {pre245, e245, t245}},
		{"L2", "#177 の並び（E → pre → T）と手順", true, {e177, pre177, t177}},
		{"L3", "pre だけ・#177 の手順", true, {pre177}},
		{"L4", "pre だけ・#245 の手順", true, {pre245}},
		{"L5",
		 "pre だけ・#245 からクラス表示を外す",
		 true,
		 {{"pre", "VP の図面タイトル", nullptr, false, true, true}}},
		{"L6",
		 "pre だけ・#245 から SetPointObjectPos を外す",
		 true,
		 {{"pre", "VP の図面タイトル", nullptr, true, false, true}}},
		{"L7",
		 "pre だけ・#245 から名前を外す",
		 true,
		 {{"pre", "VP の図面タイトル", nullptr, true, true, false}}},
		{"L8", "pre だけ・#245 の手順", false, {pre245}},
		{"L9", "pre だけ・#177 の手順", false, {pre177}},
		{"L10", "Title を書いたラベルの後に E（引き継ぐか）", true, {hand245, e245}},
		{"L11", "E だけ・#245 の手順", true, {e245}},
	};
	for (const TmLabelArm& arm : labelArms)
		TmRunLabelArm(probe, arm);

	TmRunDimArm(probe, true);
	TmRunDimArm(probe, false);

	TmRunAssocArm(probe, false);
	TmRunAssocArm(probe, true);

	(void)gSDK->EndUndoEvent();
	probe.log("");
	probe.log("おわり");
}
