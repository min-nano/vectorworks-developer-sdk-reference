//
//	probes/runtime/orgref-inspect/probe.cpp
//
//	[issue #310] 前面の図面のレイヤの列を、参照で入ったものを見分ける材料ごと読む。
//	**図面を変えない**（読むだけ）。new_document=false で、参照を作った図面に対して走らせる。
//
//	  - `GetReferencedFilesInfo` の全項目
//	  - レイヤの列（`GetDrawingHeader` → `FirstMemberObj` → `NextObject`）の各行:
//	    型・名前・`IsObjectFromReferencedFile`・`ovLayerType`・`GetLayerScaleN`・
//	    レイヤのオブジェクト変数 149〜168・補助リストの型
//	  - 各レイヤの直下: 型・名前・クラス・外接・`IsObjectFromReferencedFile`・親
//	  - `ForEachLayer` の件数、`GetNamedLayer` の引き当て
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	std::string OriPtr(const void* p)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", p);
		return buf;
	}

	std::string OriStr(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string OriName(MCObjectHandle h)
	{
		TXString name;
		gSDK->GetObjectName(h, name);
		return OriStr(name);
	}

	std::string OriNum(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%g", v);
		return buf;
	}

	std::string OriVar(MCObjectHandle h, short sel)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, sel, v))
			return "-";
		Sint16 s16 = 0;
		if (v.GetSint16(s16))
			return "s16:" + std::to_string(s16);
		Sint32 s32 = 0;
		if (v.GetSint32(s32))
			return "s32:" + std::to_string(s32);
		bool b = false;
		if (v.GetBoolean(b))
			return std::string("b:") + (b ? "1" : "0");
		Real64 d = 0;
		if (v.GetReal64(d))
			return "r:" + OriNum(d);
		TXString s;
		if (v.GetTXString(s))
			return "str:\"" + OriStr(s) + "\"";
		return "?";
	}

	std::string OriClass(MCObjectHandle h)
	{
		TXString name;
		gSDK->ClassIDToName(gSDK->GetObjectClass(h), name);
		return OriStr(name);
	}

	std::string OriBounds(MCObjectHandle h)
	{
		WorldRect r;
		if (!gSDK->GetObjectBounds(h, r))
			return "-";
		return "(" + OriNum(r.Left()) + "," + OriNum(r.Bottom()) + ")-(" + OriNum(r.Right()) + "," +
			   OriNum(r.Top()) + ")";
	}

	void OriMembers(vwprobe::Report& probe, MCObjectHandle layer)
	{
		int n = 0;
		int fromRef = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(layer); m != nullptr && n < 100000;
			 m = gSDK->NextObject(m), ++n)
		{
			const bool ref = gSDK->IsObjectFromReferencedFile(m);
			fromRef += ref ? 1 : 0;
			if (n >= 40)
				continue;
			std::string row = "      - type=" + std::to_string(gSDK->GetObjectTypeN(m));
			row += " name=\"" + OriName(m) + "\" class=\"" + OriClass(m) + "\"";
			row += " bounds=" + OriBounds(m);
			row += std::string(" fromRef=") + (ref ? "yes" : "no");
			MCObjectHandle parent = gSDK->ParentObject(m);
			row += std::string(" parent==layer:") + (parent == layer ? "yes" : "no");
			probe.log(row);
		}
		probe.log("      直下の件数=" + std::to_string(n) +
				  " うち fromRef=" + std::to_string(fromRef));
	}

	void OriAux(vwprobe::Report& probe, MCObjectHandle layer)
	{
		std::string out;
		int n = 0;
		for (MCObjectHandle a = gSDK->FirstAuxObject(layer); a != nullptr && n < 1000;
			 a = gSDK->NextObject(a), ++n)
		{
			if (n < 12)
				out += (n ? "," : "") + std::to_string(gSDK->GetObjectTypeN(a)) + "\"" +
					   OriName(a) + "\"";
		}
		probe.log("    aux=" + std::to_string(n) + "[" + out + "]");
	}

	void OriCb(MCObjectHandle h, CallBackPtr, void* env)
	{
		static_cast<std::vector<MCObjectHandle>*>(env)->push_back(h);
	}
} // namespace

VW_PROBE("orgref-inspect", "レイヤを読む", "前面の図面の参照レイヤを読む（変更なし）")
{
	IFileIdentifierPtr active;
	bool saved = false;
	if (gSDK->GetActiveDocument(&active, saved) && active != nullptr)
	{
		TXString path;
		active->GetFileFullPath(path);
		probe.log("図面=" + OriStr(path) + (saved ? " saved=yes" : " saved=no"));
	}

	probe.log("■ GetReferencedFilesInfo");
	TVWArray_ReferencedFileInfo refs;
	gSDK->GetReferencedFilesInfo(refs);
	for (size_t i = 0; i < refs.GetSize(); ++i)
	{
		const SReferencedFileInfo& r = refs.GetAt(i);
		std::string row = "  [" + std::to_string(i) + "] id=" + std::to_string(r.id);
		row += " type=" + std::to_string((int)r.type);
		row += " mac=\"" + OriStr(r.macFilePathName) + "\"";
		row += " win=\"" + OriStr(r.winFilePathName) + "\"";
		probe.log(row);
		std::string f = "      updateOnOpen=" + std::to_string((int)r.bUpdateOnOpen);
		f += " ignoreUserOrigin=" + std::to_string((int)r.bIgnoreUserOrigin);
		f += " updateClasses=" + std::to_string((int)r.bUpdateClasses);
		f += " createLayerLinks=" + std::to_string((int)r.bCreateLayerLinks);
		f += " saveCache=" + std::to_string((int)r.bSaveCache);
		f += " useLayerColors=" + std::to_string((int)r.bUseLayerColors);
		f += " lastUpdate=" + std::to_string(r.lastUpdateTimeStamp);
		f += " lastModified=" + std::to_string(r.lastModifiedTimeStamp);
		probe.log(f);
	}
	probe.log("  件数=" + std::to_string(refs.GetSize()));

	probe.log("■ レイヤの列");
	MCObjectHandle header = gSDK->GetDrawingHeader();
	MCObjectHandle current = gSDK->GetCurrentLayer();
	int i = 0;
	for (MCObjectHandle h = header ? gSDK->FirstMemberObj(header) : nullptr;
		 h != nullptr && i < 1000; h = gSDK->NextObject(h), ++i)
	{
		const short type = gSDK->GetObjectTypeN(h);
		std::string row = "  [" + std::to_string(i) + "] " + OriPtr(h);
		row += " type=" + std::to_string(type) + " name=\"" + OriName(h) + "\"";
		row += std::string(" fromRef=") + (gSDK->IsObjectFromReferencedFile(h) ? "yes" : "no");
		if (type != kLayerNode)
		{
			probe.log(row + " （レイヤではない）");
			continue;
		}
		row += " ovLayerType=" + OriVar(h, ovLayerType);
		double_gs scale = -1;
		gSDK->GetLayerScaleN(h, scale);
		row += " scale=" + OriNum(scale);
		row += std::string(" current=") + (h == current ? "yes" : "no");
		probe.log(row);
		std::string ov = "    ov";
		for (short sel = 149; sel <= 168; ++sel)
			ov += " " + std::to_string(sel) + "=" + OriVar(h, sel);
		probe.log(ov);
		OriAux(probe, h);
		OriMembers(probe, h);
	}
	probe.log("  件数=" + std::to_string(i));

	std::vector<MCObjectHandle> hs;
	gSDK->ForEachLayer(&OriCb, &hs);
	probe.log("ForEachLayer 件数=" + std::to_string(hs.size()));

	const char* names[] = {"1F", "2F", "NNA#1_1F", "NNA#1_2F", "NNA#2_1F", "NNA#1_レイヤ-1"};
	for (const char* nm : names)
		probe.log(std::string("GetNamedLayer(\"") + nm + "\") → " +
				  OriPtr(gSDK->GetNamedLayer(nm)));
}
