//
//	probes/runtime/t0-mismatch/probe.cpp
//
//	[issue #249] #245 で「書いた直後（T0）から過去の実測と違った」3 件を切り分ける。
//	腕ごとに自分用の新しい図面を開き（空＝OpenDocumentPath(nullptr) か、既定のテンプレート
//	Default.sta か）、終わったら保存せずに閉じる。前の腕の状態を持ち越さないため。
//
//	3 巡目（1・2 巡目の結果は PR のコメント）:
//	  V … ラベルが写すのは選択されている VP の 1032 か。作った VP は選択されたまま残るか。
//	  W … 関連付けは表示範囲で決まる。表示を寄せる・縮小すれば成立するか。
//	2 巡目:
//	  R … 図面ラベルは Title が空で生まれるとは限らない。生まれたときの Title がどこから来るか
//	      （どの VP の 1032 か・前のラベルか・アクティブレイヤ・選択・置く位置）を振る。
//	  S … 空の図面で関連付けが成立しないのは置いた座標による（1 巡目で y=0〜8000 は成立、
//	      12000 以上は不成立）。y・x・レイヤの縮尺・用紙・表示の中心を振り、境目を探す。
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

	std::string TmLabelParams(MCObjectHandle h)
	{
		VWFC::VWObjects::VWParametricObj obj(h);
		RefNumber style = 0;
		const bool ok = gSDK->GetPluginObjectStyle(h, style) != 0;
		return "Title=「" + TmStr(obj.GetParamString("Title")) +
			   "」 LS=" + std::to_string(obj.GetParamLong("Link State")) +
			   " style=" + (ok ? TmRef(static_cast<InternalIndex>(style)) : std::string("(false)"));
	}

	// ラベルを置かずに平面ビューポートだけ作る（1032 を preset で入れる）。
	MCObjectHandle TmMakeVp(vwprobe::Report& probe, MCObjectHandle sheet, MCObjectHandle design,
							const char* preset, double moveX)
	{
		MCObjectHandle vp = gSDK->CreateViewport(sheet);
		if (vp == nullptr)
		{
			probe.fail("CreateViewport が nil");
			return nullptr;
		}
		gSDK->SetViewportLayerVisibility(vp, design, VWFC::VWObjects::kLayerVisibilityNormal);
		TVariableBlock scaleVar;
		scaleVar = static_cast<Real64>(kTmScale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleVar);
		if (preset != nullptr)
		{
			TVariableBlock descVar;
			descVar = TXString(preset);
			gSDK->SetObjectVariable(vp, ovViewportDescription, descVar);
		}
		gSDK->UpdateViewport(vp);
		if (moveX != 0.0)
			gSDK->MoveObject(vp, static_cast<WorldCoord>(moveX), 0);
		return vp;
	}

	// ラベルを 1 本作って（どこにも入れない）、生まれたときの Title を出し、消す。
	void TmBornTitle(vwprobe::Report& probe, const std::string& what, const WorldPt& at)
	{
		MCObjectHandle h = gSDK->CreateCustomObject(kTmLabel, at, 0.0, false);
		if (h == nullptr)
		{
			probe.fail(what + ": CreateCustomObject が nil");
			return;
		}
		probe.log("    " + what + " → " + TmLabelParams(h));
		gSDK->DeleteObject(h);
	}

	struct TmBornSet
	{
		MCObjectHandle design = nullptr;
		MCObjectHandle sheet = nullptr;
	};

	bool TmBornSetup(vwprobe::Report& probe, TmBornSet& s, const char* sheetName)
	{
		gSDK->DefineCustomObject(kTmLabel, kCustomObjectPrefNever);
		s.design = gSDK->CreateLayer("tm-design", kLayerDesign);
		s.sheet = gSDK->CreateLayer(TXString(sheetName), kLayerSheet);
		if (s.design == nullptr || s.sheet == nullptr)
		{
			probe.fail("CreateLayer が nil");
			return false;
		}
		gSDK->SetLayerScaleN(s.design, kTmScale);
		gSDK->SetCurrentLayer(s.design);
		gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 10000.0, 10000.0);
		gSDK->SetCurrentLayer(s.sheet);
		return true;
	}

	// ---------------------------------------------------------------------------
	// A: 寸法の関連付け

	void TmAssocAt(vwprobe::Report& probe, const std::string& tag, double x, double y)
	{
		MCObjectHandle lineA = gSDK->CreateLine(WorldPt(x, y), WorldPt(x, y - 500.0));
		MCObjectHandle lineB =
			gSDK->CreateLine(WorldPt(x + 3000.0, y), WorldPt(x + 3000.0, y - 500.0));
		MCObjectHandle dim =
			gSDK->CreateLinearDimension(WorldPt(x, y), WorldPt(x + 3000.0, y),
										static_cast<WorldCoord>(600.0), 0, Vector2(0, 0), 0);
		if (lineA == nullptr || lineB == nullptr || dim == nullptr)
		{
			probe.fail(tag + ": 線か寸法を作れなかった");
			return;
		}
		gSDK->DeselectAll();
		gSDK->AssociateLinearDimension(dim, false);
		probe.log("    " + tag + " (" + TmNum(x, 0) + "," + TmNum(y, 0) +
				  ") → HasConstraint=" + TmYes(gSDK->HasConstraint(dim) != 0));
	}

	std::string TmRectStr(const WorldRect& r)
	{
		return "L" + TmNum(r.left, 0) + " B" + TmNum(r.bottom, 0) + " R" + TmNum(r.right, 0) +
			   " T" + TmNum(r.top, 0);
	}

	void TmAssocView(vwprobe::Report& probe)
	{
		WorldRect page;
		gSDK->GetPageBounds(page);
		WorldPt center;
		gSDK->GetViewCenter(center);
		probe.log("  用紙 " + TmRectStr(page) + " / 表示の中心 (" + TmNum(center.x, 0) + "," +
				  TmNum(center.y, 0) + ") / アクティブレイヤ " +
				  TmLayerStr(gSDK->GetActiveLayer()));
	}

	std::string TmSel(MCObjectHandle h)
	{
		return h != nullptr && gSDK->IsSelected(h) ? "選択" : "非選択";
	}

	// V: ラベルが写すのは「選択されている VP」の 1032 か。
	void TmRunSel(vwprobe::Report& probe)
	{
		probe.log("");
		probe.log("=== V（テンプレート）: 作った VP は選択されたまま残るか・どの VP を写すか ===");
		const Sint32 ref = TmOpen(probe, true);
		TmBornSet s;
		if (ref != 0 && TmBornSetup(probe, s, "tm-s1"))
		{
			gSDK->DeselectAll();
			MCObjectHandle a = TmMakeVp(probe, s.sheet, s.design, "A", 0.0);
			probe.log("    A を作った直後: A=" + TmSel(a));
			MCObjectHandle b = TmMakeVp(probe, s.sheet, s.design, "B", 300.0);
			probe.log("    B を作った直後: A=" + TmSel(a) + " B=" + TmSel(b));
			gSDK->DeselectAll();
			TmBornTitle(probe, "全部の選択を外した", WorldPt(0, 0));
			gSDK->SelectObject(a, true);
			TmBornTitle(probe, "A だけ選択", WorldPt(0, 0));
			gSDK->SelectObject(b, true);
			TmBornTitle(probe, "A と B を選択（A を先に）", WorldPt(0, 0));
			gSDK->DeselectAll();
			gSDK->SelectObject(b, true);
			gSDK->SelectObject(a, true);
			TmBornTitle(probe, "A と B を選択（B を先に）", WorldPt(0, 0));
			gSDK->DeselectAll();
			MCObjectHandle rect =
				gSDK->CreateRectangleN(WorldPt(0, 0), Vector2(1, 0), 100.0, 100.0);
			gSDK->DeselectAll();
			gSDK->SelectObject(rect, true);
			TmBornTitle(probe, "VP ではない矩形だけ選択", WorldPt(0, 0));
			gSDK->DeselectAll();
			gSDK->SelectObject(b, true);
			gSDK->SetCurrentLayer(s.design);
			TmBornTitle(probe, "B を選択したままデザインレイヤをアクティブに", WorldPt(0, 0));
			gSDK->SetCurrentLayer(s.sheet);
			gSDK->DeselectAll();
			// 置いたラベルの Title が、注釈へ入れて ResetObject したときに VP へ押し込まれるのを
			// 選択で防げるか（#177 の pre の再現と、その手当て）。
			MCObjectHandle c = TmMakeVp(probe, s.sheet, s.design, "C の題", 600.0);
			gSDK->DeselectAll();
			MCObjectHandle d = TmMakeVp(probe, s.sheet, s.design, nullptr, 900.0);
			MCObjectHandle e = TmMakeVp(probe, s.sheet, s.design, "E の題", 1200.0);
			gSDK->DeselectAll();
			gSDK->SelectObject(d, true);
			MCObjectHandle h = gSDK->CreateCustomObject(kTmLabel, WorldPt(0, 0), 0.0, false);
			probe.log("    空の VP（d）を選択して作った → " + TmLabelParams(h));
			gSDK->DeselectAll();
			gSDK->AddViewportAnnotationObject(e, h);
			gSDK->ResetObject(h);
			probe.log("    E の題の VP へ入れて ResetObject → " + TmLabelParams(h) +
					  " / E の 1032=" + TmText(e, ovViewportDescription));
			gSDK->DeselectAll();
			MCObjectHandle h2 = gSDK->CreateCustomObject(kTmLabel, WorldPt(0, 0), 0.0, false);
			probe.log("    選択を外して作った → " + TmLabelParams(h2));
			gSDK->AddViewportAnnotationObject(c, h2);
			gSDK->ResetObject(h2);
			probe.log("    C の題の VP へ入れて ResetObject → " + TmLabelParams(h2) +
					  " / C の 1032=" + TmText(c, ovViewportDescription));
		}
		TmClose(probe, ref);
	}

	// W: 関連付けは表示範囲で決まる。表示を寄せれば（または縮小すれば）成立するか。
	void TmRunView(vwprobe::Report& probe)
	{
		probe.log("");
		probe.log("=== W（空の図面・1/1）: 表示を寄せる・縮小する ===");
		const Sint32 ref = TmOpen(probe, false);
		if (ref != 0)
		{
			TmAssocView(probe);
			TmAssocAt(probe, "そのまま", 0.0, 60000.0);
			WorldPt keep;
			gSDK->GetViewCenter(keep);
			gSDK->SetViewCenter(WorldPt(1500.0, 60000.0 + 2000.0));
			TmAssocAt(probe, "表示の中心を寸法へ寄せて", 0.0, 62000.0);
			gSDK->SetViewCenter(keep);
			TmAssocView(probe);
			TmAssocAt(probe, "戻した表示で 66000（対照）", 0.0, 66000.0);
			gSDK->SetZoomFactorN(0.01);
			TmAssocView(probe);
			TmAssocAt(probe, "SetZoomFactorN(0.01) の後", 0.0, 70000.0);
			TmAssocAt(probe, "SetZoomFactorN(0.01) の後・遠く", 0.0, 400000.0);
			gSDK->SetZoomFactorN(1.0);
			TmAssocAt(probe, "SetZoomFactorN(1) に戻した後", 0.0, 74000.0);
		}
		TmClose(probe, ref);
	}
} // namespace

VW_PROBE("t0-mismatch", "T0 の食い違い（#245）", "ラベル・文字スタイル・関連付けを振り分ける")
{
	TmRunSel(probe);
	TmRunView(probe);
	(void)gSDK->EndUndoEvent();
	probe.log("");
	probe.log("おわり");
}
