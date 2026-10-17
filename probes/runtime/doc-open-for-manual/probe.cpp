//
//	probes/runtime/doc-open-for-manual/probe.cpp
//
//	[issue #243] OpenDocumentPath で開いただけの保存済みの図面を、**人が手で**閉じたとき
//	保存の確認が出るか。DoMenuName("Close", 0) では出た（3 回）。手で開いた図面を手で
//	閉じると出ない（利用者の目視）。
//
//	新規の図面を一時フォルダへ保存して CloseDocument で閉じ、そのファイルを
//	OpenDocumentPath で開き直して、**前面に置いたまま戻る**。閉じるのは人。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
	using namespace VectorWorks;

	Sint32 ManualOpenActiveRef()
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			if (files[i].fIsActive)
				return files[i].fFileRef;
		}
		return -1;
	}
} // namespace

VW_PROBE("doc-open-for-manual", "開いて残す（手で閉じる）", "OpenDocumentPath で開いて前面に置く")
{
	std::error_code ec;
	const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::system_clock::now().time_since_epoch())
								.count();
	const std::filesystem::path dir =
		std::filesystem::temp_directory_path(ec) / "VwSdkProbes-doc-open-for-manual";
	std::filesystem::create_directories(dir, ec);
	const std::string path = (dir / ("manual-" + std::to_string(stamp) + ".vwx")).string();

	(void)gSDK->OpenDocumentPath(nullptr, false);
	const Sint32 tmp = ManualOpenActiveRef();
	Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
	fileID->Set(TXString(path.c_str()));
	probe.log("保存 GSError=" + std::to_string(gSDK->SaveActiveDocumentPath(fileID)) + " " + path);
	if (gSDK->SwitchToOpenFile(tmp) && ManualOpenActiveRef() == tmp)
		(void)gSDK->CloseDocument();

	const bool opened = gSDK->OpenDocumentPath(fileID, false);
	probe.log(std::string("OpenDocumentPath(開き直し)=") + (opened ? "yes" : "no") +
			  " fileRef=" + std::to_string(ManualOpenActiveRef()));
	probe.log("前面に manual-" + std::to_string(stamp) +
			  ".vwx を置いた。人が「閉じる」（⌘W）で閉じ、確認が出たかを見る");
}
