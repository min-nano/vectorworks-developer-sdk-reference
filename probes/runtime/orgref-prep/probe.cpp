//
//	probes/runtime/orgref-prep/probe.cpp
//
//	[issue #310] オーガナイザの「ファイル参照」で参照する図面を用意する（読むのは orgref-inspect）。
//
//	新規の空図面に次を作り、一時フォルダへ別名保存する。
//	  - クラス `OR-cls`
//	  - デザインレイヤ `1F`（1/50）: 矩形・線・点の 3 つ（矩形と線は `OR-cls`）
//	  - デザインレイヤ `2F`（1/100）: 円 2 つ
//	  - 既定のレイヤ（レイヤ-1）は空のまま
//	保存するファイル（どれも走らせる前に消す）:
//	  1. `VwSdkProbes-orgref-src.vwx`   … 参照元
//	  2. `VwSdkProbes-orgref-src2.vwx`  … 参照元と同じ中身の別ファイル（同じ名前のレイヤを 2 つの参照で持つ）
//	  3. `VwSdkProbes-orgref-dup.vwx`   … 参照先（自分も 1F・2F を持つ。同じ名前のとき）
//	  4. `VwSdkProbes-orgref-host.vwx`  … 参照先（1F・2F を消し、レイヤ-1 に矩形を 1 つ置く）
//	最後は 4 の図面が前面に残る。
//

#include "Probe.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace
{
	std::string OrpPath(const char* leaf)
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return (dir / leaf).string();
	}

	std::string OrpPtr(const void* p)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", p);
		return buf;
	}

	std::string OrpActivePath()
	{
		IFileIdentifierPtr id;
		bool saved = false;
		if (!gSDK->GetActiveDocument(&id, saved) || id == nullptr)
			return "(GetActiveDocument=false)";
		TXString path;
		id->GetFileFullPath(path);
		std::string out(static_cast<const char*>(path));
		return out + (saved ? " saved=yes" : " saved=no");
	}

	bool OrpSaveAs(vwprobe::Report& probe, const std::string& path)
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
		IFileIdentifierPtr id(IID_FileIdentifier);
		id->Set(TXString(path.c_str()));
		const GSError err = gSDK->SaveActiveDocumentPath(id);
		std::string line = "  SaveActiveDocumentPath(" + path + ")";
		line += " → " + std::to_string((long long)err);
		probe.log(line + " 図面=" + OrpActivePath());
		return err == 0;
	}

	MCObjectHandle OrpLayer(vwprobe::Report& probe, const char* name, double scale)
	{
		MCObjectHandle h = gSDK->CreateLayer(TXString(name), kLayerDesign);
		probe.log(std::string("  CreateLayer(") + name + ") → " + OrpPtr(h));
		if (h != nullptr)
		{
			gSDK->SetLayerScaleN(h, scale);
			double_gs got = -1;
			gSDK->GetLayerScaleN(h, got);
			probe.log("    scale → " + std::to_string((double)got));
		}
		return h;
	}

	MCObjectHandle OrpFirstOther(MCObjectHandle a, MCObjectHandle b)
	{
		MCObjectHandle first = gSDK->FirstMemberObj(gSDK->GetDrawingHeader());
		for (MCObjectHandle h = first; h != nullptr; h = gSDK->NextObject(h))
		{
			if (h != a && h != b && gSDK->GetObjectTypeN(h) == kLayerNode)
				return h;
		}
		return nullptr;
	}
} // namespace

VW_PROBE("orgref-prep", "参照元を用意する", "1F/2F を持つ参照元と参照先を一時フォルダへ保存")
{
	probe.log("図面=" + OrpActivePath());
	const InternalIndex cls = gSDK->AddClass("OR-cls");
	probe.log("  AddClass(OR-cls) → " + std::to_string((long long)cls));

	MCObjectHandle f1 = OrpLayer(probe, "1F", 50);
	MCObjectHandle f2 = OrpLayer(probe, "2F", 100);
	if (f1 == nullptr || f2 == nullptr)
	{
		probe.fail("1F / 2F を作れなかった");
		return;
	}

	gSDK->SetCurrentLayer(f1);
	MCObjectHandle rect = gSDK->CreateRectangle(WorldRect(WorldPt(0, 1000), WorldPt(2000, 0)));
	MCObjectHandle line = gSDK->CreateLine(WorldPt(0, 0), WorldPt(3000, 1500));
	MCObjectHandle locus = gSDK->CreateLocus(WorldPt(500, 500));
	gSDK->SetObjectClass(rect, cls);
	gSDK->SetObjectClass(line, cls);
	probe.log("  1F: rect=" + OrpPtr(rect) + " line=" + OrpPtr(line) + " locus=" + OrpPtr(locus));

	gSDK->SetCurrentLayer(f2);
	MCObjectHandle c1 = gSDK->CreateOval(WorldRect(WorldPt(0, 400), WorldPt(400, 0)));
	MCObjectHandle c2 = gSDK->CreateOval(WorldRect(WorldPt(1000, 1400), WorldPt(1400, 1000)));
	probe.log("  2F: oval=" + OrpPtr(c1) + " oval=" + OrpPtr(c2));
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		gSDK->EndUndoEvent();

	probe.log("■ 保存");
	if (!OrpSaveAs(probe, OrpPath("VwSdkProbes-orgref-src.vwx")) ||
		!OrpSaveAs(probe, OrpPath("VwSdkProbes-orgref-src2.vwx")) ||
		!OrpSaveAs(probe, OrpPath("VwSdkProbes-orgref-dup.vwx")))
	{
		probe.fail("保存できなかった");
		return;
	}

	probe.log("■ 参照先: 1F・2F を消し、レイヤ-1 に矩形を置く");
	MCObjectHandle own = OrpFirstOther(f1, f2);
	if (own == nullptr)
	{
		probe.fail("残すレイヤが無い");
		return;
	}
	gSDK->SetCurrentLayer(own);
	gSDK->DeleteObject(f1, false);
	gSDK->DeleteObject(f2, false);
	MCObjectHandle mine =
		gSDK->CreateRectangle(WorldRect(WorldPt(-500, -500), WorldPt(-100, -900)));
	probe.log("  own rect=" + OrpPtr(mine));
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		gSDK->EndUndoEvent();
	if (!OrpSaveAs(probe, OrpPath("VwSdkProbes-orgref-host.vwx")))
	{
		probe.fail("参照先を保存できなかった");
		return;
	}
	probe.log("前面=" + OrpActivePath());
}
