//
//	probes/runtime/orgref-sdk/probe.cpp
//
//	[issue #310] 前面の図面から `ISDK::CreateReference(<orgref-src>)` を呼ぶ（mac での比べ物）。
//	参照元は orgref-prep が一時フォルダに作った `VwSdkProbes-orgref-src.vwx`。
//	読み取りは orgref-inspect を new_document=false で続けて走らせる。
//

#include "Probe.h"

#include <filesystem>
#include <string>
#include <system_error>

using namespace VectorWorks;
using namespace VectorWorks::Filing;

VW_PROBE("orgref-sdk", "CreateReference で参照", "前面の図面から orgref-src を CreateReference")
{
	std::error_code ec;
	std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
	const std::string src = (dir / "VwSdkProbes-orgref-src.vwx").string();
	const bool exists = std::filesystem::exists(src, ec);
	probe.log("src=" + src + (exists ? " (ある)" : " (無い)"));
	if (!exists)
	{
		probe.fail("先に orgref-prep を走らせる");
		return;
	}
	IFileIdentifierPtr id(IID_FileIdentifier);
	id->Set(TXString(src.c_str()));
	const bool made = gSDK->CreateReference(id);
	probe.log(std::string("CreateReference → ") + (made ? "true" : "false"));
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
	{
		gSDK->EndUndoEvent();
		probe.log("開いていた undo イベントを閉じた");
	}
}
