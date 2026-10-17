//
//	probes/runtime/t0-mismatch/probe.cpp
//
//	[issue #249] #245 で「書いた直後（T0）から過去の実測と違った」3 件を切り分ける。
//	腕ごとに自分用の新しい図面を開き（空＝OpenDocumentPath(nullptr) か、既定のテンプレート
//	Default.sta か）、終わったら保存せずに閉じる。前の腕の状態を持ち越さないため。
//
//	2 巡目（1 巡目の結果は PR のコメント）:
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

	void TmSetDescription(MCObjectHandle vp, const char* value)
	{
		TVariableBlock descVar;
		descVar = TXString(value);
		gSDK->SetObjectVariable(vp, ovViewportDescription, descVar);
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

	// シートレイヤ上のビューポートを並び順に出す（どれが「先頭」か）。
	std::string TmVpOrder(MCObjectHandle sheet)
	{
		std::string out;
		for (MCObjectHandle m = gSDK->FirstMemberObj(sheet); m != nullptr; m = gSDK->NextObject(m))
			if (gSDK->GetObjectTypeN(m) == kViewportNode)
				out += " " + TmText(m, ovViewportDescription);
		return out.empty() ? " (無し)" : out;
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

	// R: ラベルが生まれたときの Title はどこから来るか。
	void TmRunBorn(vwprobe::Report& probe)
	{
		const WorldPt at(0, 0);
		{
			probe.log("");
			probe.log("=== R1（テンプレート）: VP を作る前・A・B の順に作った後 ===");
			const Sint32 ref = TmOpen(probe, true);
			TmBornSet s;
			if (ref != 0 && TmBornSetup(probe, s, "tm-s1"))
			{
				TmBornTitle(probe, "VP が 1 枚も無い", at);
				MCObjectHandle a = TmMakeVp(probe, s.sheet, s.design, "A", 0.0);
				TmBornTitle(probe, "A だけ", at);
				MCObjectHandle b = TmMakeVp(probe, s.sheet, s.design, "B", 300.0);
				probe.log("    並び:" + TmVpOrder(s.sheet));
				TmBornTitle(probe, "A → B", at);
				TmBornTitle(probe, "A → B・B の上（300,0）に作る", WorldPt(300, 0));
				TmBornTitle(probe, "A → B・遠く（50000,50000）に作る", WorldPt(50000, 50000));
				TmSetDescription(a, "A2");
				TmBornTitle(probe, "A の 1032 を A2 へ書き換えた後", at);
				gSDK->DeselectAll();
				gSDK->SelectObject(b, true);
				TmBornTitle(probe, "B だけ選択した後", at);
				gSDK->DeselectAll();
				gSDK->DeleteObject(a);
				probe.log("    A を消した後の並び:" + TmVpOrder(s.sheet));
				TmBornTitle(probe, "A を消した後", at);
				MCObjectHandle s2 = gSDK->CreateLayer("tm-s2", kLayerSheet);
				if (s2 != nullptr)
				{
					gSDK->SetCurrentLayer(s2);
					TmBornTitle(probe, "VP の無い別のシートがアクティブ", at);
					(void)TmMakeVp(probe, s2, s.design, "C", 0.0);
					TmBornTitle(probe, "別のシートに C を作った後（そこがアクティブ）", at);
					gSDK->SetCurrentLayer(s.sheet);
					TmBornTitle(probe, "元のシートへ戻った後", at);
				}
				gSDK->SetCurrentLayer(s.design);
				TmBornTitle(probe, "デザインレイヤがアクティブ", at);
			}
			TmClose(probe, ref);
		}
		{
			probe.log("");
			probe.log("=== R2（テンプレート）: 空の VP の後に B ===");
			const Sint32 ref = TmOpen(probe, true);
			TmBornSet s;
			if (ref != 0 && TmBornSetup(probe, s, "tm-s1"))
			{
				(void)TmMakeVp(probe, s.sheet, s.design, nullptr, 0.0);
				(void)TmMakeVp(probe, s.sheet, s.design, "B", 300.0);
				probe.log("    並び:" + TmVpOrder(s.sheet));
				TmBornTitle(probe, "空 → B", at);
			}
			TmClose(probe, ref);
		}
		{
			probe.log("");
			probe.log("=== R3（テンプレート）: 前のラベルが Title を持つが、どの VP も空 ===");
			const Sint32 ref = TmOpen(probe, true);
			TmBornSet s;
			if (ref != 0 && TmBornSetup(probe, s, "tm-s1"))
			{
				MCObjectHandle h = gSDK->CreateCustomObject(kTmLabel, at, 0.0, false);
				if (h != nullptr)
				{
					VWFC::VWObjects::VWParametricObj(h).SetParamValue("Title", "ラベルに書いた題");
					gSDK->ResetObject(h);
					probe.log("    注釈へ入れないラベルに Title を書いた → " + TmLabelParams(h));
				}
				TmBornTitle(probe, "VP 無し・前のラベルが題を持つ", at);
				(void)TmMakeVp(probe, s.sheet, s.design, nullptr, 0.0);
				TmBornTitle(probe, "空の VP を作った後", at);
			}
			TmClose(probe, ref);
		}
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

	void TmScan(vwprobe::Report& probe, const char* label, const std::vector<double>& ys, double x)
	{
		probe.log("  -- " + std::string(label));
		TmAssocView(probe);
		for (double y : ys)
			TmAssocAt(probe, "y", x, y);
	}

	// S: 関連付けが成立する範囲。
	void TmRunScan(vwprobe::Report& probe)
	{
		const std::vector<double> ys = {0,	   2000,  4000,	 6000,	8000,  9000,
										10000, 11000, 12000, 16000, 20000, 60000};
		{
			probe.log("");
			probe.log("=== S1（空の図面・1/1）: y を振る ===");
			const Sint32 ref = TmOpen(probe, false);
			if (ref != 0)
			{
				TmScan(probe, "x=0", ys, 0.0);
				TmScan(probe, "x=-20000（左へ外す）", {0, 4000}, -20000.0);
				TmScan(probe, "x=20000（右へ外す）", {0, 4000}, 20000.0);
				gSDK->SetViewCenter(WorldPt(1500.0, 60000.0));
				TmScan(probe, "表示の中心を (1500,60000) へ動かした後", {0, 4000, 60000, 61000},
					   0.0);
			}
			TmClose(probe, ref);
		}
		{
			probe.log("");
			probe.log("=== S2（空の図面）: 先にレイヤを 1/50 にしてから y を振る ===");
			const Sint32 ref = TmOpen(probe, false);
			if (ref != 0)
			{
				gSDK->SetLayerScaleN(gSDK->GetActiveLayer(), 50.0);
				TmScan(probe, "1/50", ys, 0.0);
			}
			TmClose(probe, ref);
		}
		{
			probe.log("");
			probe.log("=== S3（空の図面・1/1）: 用紙を広げてから y を振る ===");
			const Sint32 ref = TmOpen(probe, false);
			if (ref != 0)
			{
				WorldRect page;
				gSDK->GetPageBounds(page);
				WorldRect big(WorldPt(-100000.0, 100000.0), WorldPt(100000.0, -100000.0));
				gSDK->SetPageBounds(big);
				TmScan(probe, "用紙を ±100000 に", ys, 0.0);
			}
			TmClose(probe, ref);
		}
		{
			probe.log("");
			probe.log("=== S4（テンプレート）: y を振る ===");
			const Sint32 ref = TmOpen(probe, true);
			if (ref != 0)
				TmScan(probe, "そのまま", {0, 60000, 100000, 200000, 400000, 1000000}, 0.0);
			TmClose(probe, ref);
		}
	}
} // namespace

VW_PROBE("t0-mismatch", "T0 の食い違い（#245）", "ラベル・文字スタイル・関連付けを振り分ける")
{
	TmRunBorn(probe);
	TmRunScan(probe);
	(void)gSDK->EndUndoEvent();
	probe.log("");
	probe.log("おわり");
}
