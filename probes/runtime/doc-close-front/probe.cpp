//
//	probes/runtime/doc-close-front/probe.cpp
//
//	[issue #243] **人が手で開いた**保存済みの図面を、DoMenuName("Close", 0) で閉じたとき
//	保存の確認が出るか。OpenDocumentPath で開いた図面では出た（3 回）。
//
//	前面の図面に DoMenuName("Close", 0) を 1 回だけ呼ぶ。ブリッジからは new_document=false で
//	走らせる（人が開いた図面を前面にしたまま）。無題の図面なら何もしない。
//	確認が出たら人がキャンセルを押す。
//

#include "Probe.h"

#include <chrono>
#include <string>

VW_PROBE("doc-close-front", "前面の図面を閉じる", "手で開いた図面に DoMenuName の Close を呼ぶ")
{
	TVWArray_OpenFileInformation files;
	gSDK->GetOpenFilesList(files);
	Sint32 ref = -1;
	bool inMemory = true;
	TXString path;
	for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
	{
		if (!files[i].fIsActive)
			continue;
		ref = files[i].fFileRef;
		inMemory = files[i].fIsInMemoryOnly;
		if (files[i].fpFileID != nullptr)
			files[i].fpFileID->GetFileFullPath(path);
	}
	const size_t before = files.GetSize();
	probe.log("前面: fileRef=" + std::to_string(ref) + " inMemoryOnly=" +
			  (inMemory ? "yes" : "no") + " path=" + static_cast<const char*>(path));
	if (ref < 0 || inMemory)
	{
		probe.fail("前面が保存済みの図面ではない");
		return;
	}

	probe.log("DoMenuName(\"Close\", 0) を呼ぶ（確認が出たらキャンセル）");
	const auto t0 = std::chrono::steady_clock::now();
	const short r = gSDK->DoMenuName(TXString("Close"), 0);
	const auto ms =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
			.count();
	TVWArray_OpenFileInformation after;
	gSDK->GetOpenFilesList(after);
	bool still = false;
	for (size_t i = 0, cnt = after.GetSize(); i < cnt; ++i)
		still = still || after[i].fFileRef == ref;
	probe.log("DoMenuName(\"Close\", 0)=" + std::to_string(r) + " (" + std::to_string(ms) +
			  "ms) 件数 " + std::to_string(before) + " → " + std::to_string(after.GetSize()) +
			  " まだ開いている=" + (still ? "yes" : "no"));
}
