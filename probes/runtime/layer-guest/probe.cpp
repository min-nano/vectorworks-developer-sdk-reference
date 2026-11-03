//
//	probes/runtime/layer-guest/probe.cpp
//
//	[issue #281] 参照で入ってきたレイヤ（`ELayerType::kGuestLayerDesign` = 3）が、
//	レイヤの列（`GetDrawingHeader` → `FirstMemberObj` → `NextObject`）と
//	`ForEachLayer` にどう現れるか。
//
//	  1. 参照元を作る: 新規の空図面にデザインレイヤ `LG-src` と点を 1 つ置き、
//	     一時フォルダの `VwSdkProbes-layer-guest-src.vwx` へ別名保存する。
//	  2. 続けて `VwSdkProbes-layer-guest-host.vwx` へ別名保存して、図面を参照先へ移す。
//	     `LG-src` は消す（参照で入ってきたものと見分けるため）。
//	  3. `ISDK::CreateReference(src)` を呼び、列・`ForEachLayer`・各レイヤの種類
//	     （`ovLayerType` / `GetLayerType`）・`IsObjectFromReferencedFile`・
//	     `ovGuestLayerHasRelativePath`(163)・直下のオブジェクトの種類を記録する。
//
//	保存先のファイルは走らせる前に消す（既存のファイルへの別名保存は通らないため）。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWLayerObj.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifndef VW_PAYLOAD_BUILD_ID
#	define VW_PAYLOAD_BUILD_ID "local"
#endif

namespace
{
	using VectorWorks::Filing::IFileIdentifierPtr;
	using VectorWorks::Filing::IID_FileIdentifier;

	std::string LgPath(const char* leaf)
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return (dir / leaf).string();
	}

	std::string LgPtr(const void* p)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", p);
		return buf;
	}

	std::string LgName(MCObjectHandle h)
	{
		TXString name;
		gSDK->GetObjectName(h, name);
		return std::string(static_cast<const char*>(name));
	}

	// オブジェクト変数を、受け取れた型の名前つきで文字にする（型が分からないため総当たり）。
	std::string LgVar(MCObjectHandle h, short sel)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, sel, v))
			return "(GetObjectVariable=false)";
		Sint16 s16 = 0;
		if (v.GetSint16(s16))
			return "s16:" + std::to_string(s16);
		Sint32 s32 = 0;
		if (v.GetSint32(s32))
			return "s32:" + std::to_string(s32);
		bool b = false;
		if (v.GetBoolean(b))
			return std::string("bool:") + (b ? "true" : "false");
		TXString s;
		if (v.GetTXString(s))
			return "str:\"" + std::string(static_cast<const char*>(s)) + "\"";
		return "(型が読めない)";
	}

	// 直下のオブジェクトの種類を並べる（多ければ頭だけ）。
	std::string LgMembers(MCObjectHandle h)
	{
		std::string out;
		int n = 0;
		MCObjectHandle first = gSDK->FirstMemberObj(h);
		for (MCObjectHandle m = first; m != nullptr && n < 100000; m = gSDK->NextObject(m))
		{
			if (n < 8)
			{
				out += n ? "," : "";
				out += std::to_string(gSDK->GetObjectTypeN(m));
			}
			++n;
		}
		if (n > 8)
			out += ",…";
		return std::to_string(n) + "[" + out + "]";
	}

	std::string LgDescribe(MCObjectHandle h, MCObjectHandle current)
	{
		const short type = gSDK->GetObjectTypeN(h);
		std::string line = "type=" + std::to_string(type);
		line += " name=\"" + LgName(h) + "\"";
		const bool fromRef = gSDK->IsObjectFromReferencedFile(h);
		line += std::string(" fromRef=") + (fromRef ? "yes" : "no");
		if (type != kLayerNode)
			return line + " （レイヤではない）";
		line += " ovLayerType=" + LgVar(h, ovLayerType);
		line += " GetLayerType=" + std::to_string((int)VWLayerObj(h).GetLayerType());
		line += " rel(163)=" + LgVar(h, ovGuestLayerHasRelativePath);
		line += " members=" + LgMembers(h);
		line += std::string(" current==") + (h == current ? "yes" : "no");
		return line;
	}

	void LgCb(MCObjectHandle h, CallBackPtr, void* env)
	{
		static_cast<std::vector<MCObjectHandle>*>(env)->push_back(h);
	}

	void LgWalk(vwprobe::Report& probe, const std::string& title)
	{
		probe.log("■ " + title);
		MCObjectHandle header = gSDK->GetDrawingHeader();
		MCObjectHandle current = gSDK->GetCurrentLayer();
		MCObjectHandle first = header ? gSDK->FirstMemberObj(header) : nullptr;
		int i = 0;
		for (MCObjectHandle h = first; h != nullptr && i < 1000; h = gSDK->NextObject(h), ++i)
		{
			std::string row = "  [" + std::to_string(i) + "] " + LgPtr(h);
			probe.log(row + " " + LgDescribe(h, current));
		}
		probe.log("  件数=" + std::to_string(i));
		std::vector<MCObjectHandle> hs;
		gSDK->ForEachLayer(&LgCb, &hs);
		for (std::size_t k = 0; k < hs.size(); ++k)
		{
			std::string row = "  (" + std::to_string(k) + ") " + LgPtr(hs[k]);
			row += " type=" + std::to_string(gSDK->GetObjectTypeN(hs[k]));
			probe.log(row + " \"" + LgName(hs[k]) + "\"");
		}
		probe.log("  ForEachLayer 件数=" + std::to_string(hs.size()));
	}

	std::string LgActivePath()
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

	bool LgSaveAs(vwprobe::Report& probe, const std::string& path)
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
		IFileIdentifierPtr id(IID_FileIdentifier);
		id->Set(TXString(path.c_str()));
		const GSError err = gSDK->SaveActiveDocumentPath(id);
		std::string line = "  SaveActiveDocumentPath(" + path + ")";
		line += " → " + std::to_string((long long)err);
		probe.log(line + " 図面=" + LgActivePath());
		return err == 0;
	}
} // namespace

VW_PROBE("layer-guest", "参照で入るレイヤ", "CreateReference 後の列・種類 3")
{
	probe.log("build=" + std::string(VW_PAYLOAD_BUILD_ID) + " 図面=" + LgActivePath());
	probe.log("kGuestLayerDesign=" + std::to_string((int)kGuestLayerDesign));
	probe.log("kLayerRefNode=" + std::to_string((int)kLayerRefNode));
	LgWalk(probe, "走らせる前の列");

	probe.log("■ 参照元を作る");
	MCObjectHandle src = gSDK->GetNamedLayer("LG-src");
	if (src == nullptr)
		src = gSDK->CreateLayer("LG-src", kLayerDesign);
	probe.log("  LG-src → " + LgPtr(src));
	if (src == nullptr)
	{
		probe.fail("LG-src を作れなかった");
		return;
	}
	gSDK->SetCurrentLayer(src);
	MCObjectHandle locus = gSDK->CreateLocus(WorldPt(100, 200));
	probe.log("  CreateLocus → " + LgPtr(locus));
	probe.log("  LG-src の直下=" + LgMembers(src));
	const std::string srcPath = LgPath("VwSdkProbes-layer-guest-src.vwx");
	const std::string hostPath = LgPath("VwSdkProbes-layer-guest-host.vwx");
	if (!LgSaveAs(probe, srcPath))
	{
		probe.fail("参照元を保存できなかった");
		return;
	}
	if (!LgSaveAs(probe, hostPath))
	{
		probe.fail("参照先へ別名保存できなかった");
		return;
	}

	probe.log("■ 参照先から LG-src を消す");
	MCObjectHandle other = nullptr;
	MCObjectHandle first = gSDK->FirstMemberObj(gSDK->GetDrawingHeader());
	for (MCObjectHandle h = first; h != nullptr && other == nullptr; h = gSDK->NextObject(h))
	{
		if (h != src && gSDK->GetObjectTypeN(h) == kLayerNode)
			other = h;
	}
	if (other != nullptr)
		gSDK->SetCurrentLayer(other);
	gSDK->DeleteObject(src, false);
	LgWalk(probe, "LG-src を消した後の列");

	probe.log("■ CreateReference(src) を呼ぶ");
	IFileIdentifierPtr srcId(IID_FileIdentifier);
	srcId->Set(TXString(srcPath.c_str()));
	const bool made = gSDK->CreateReference(srcId);
	probe.log(std::string("  → ") + (made ? "true" : "false") + " 図面=" + LgActivePath());
	LgWalk(probe, "CreateReference の後の列");

	probe.log("  GetNamedLayer(\"LG-src\") → " + LgPtr(gSDK->GetNamedLayer("LG-src")));
	MCObjectHandle guest = nullptr;
	first = gSDK->FirstMemberObj(gSDK->GetDrawingHeader());
	for (MCObjectHandle h = first; h != nullptr; h = gSDK->NextObject(h))
	{
		if (gSDK->GetObjectTypeN(h) == kLayerNode && VWLayerObj(h).GetLayerType() == 3)
			guest = h; // 最後の 1 枚（LG-src 由来）を取る
	}
	probe.log("  種類 3 の最後のレイヤ → " + LgPtr(guest));
	if (guest != nullptr)
	{
		const std::string name = LgName(guest);
		MCObjectHandle byName = gSDK->GetNamedLayer(name.c_str());
		probe.log("  GetNamedLayer(\"" + name + "\") → " + LgPtr(byName));
		probe.log("■ 参照で入ったレイヤを現在にする");
		gSDK->SetCurrentLayer(guest);
		MCObjectHandle cur = gSDK->GetCurrentLayer();
		probe.log("  GetCurrentLayer → " + LgPtr(cur) + (cur == guest ? " 一致" : " 不一致"));
		double_gs scale = -1;
		gSDK->GetLayerScaleN(guest, scale);
		probe.log("  GetLayerScaleN → " + std::to_string((double)scale));
		LgWalk(probe, "参照で入ったレイヤを現在にした後の列");
	}

	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
	{
		gSDK->EndUndoEvent();
		probe.log("開いていた undo イベントを閉じた");
	}
}
