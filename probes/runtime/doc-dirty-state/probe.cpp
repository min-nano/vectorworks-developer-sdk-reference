//
//	probes/runtime/doc-dirty-state/probe.cpp
//
//	[issue #240] 保存済みの図面を変更した状態（未保存の変更あり）を SDK から読めるか。
//
//	候補は GetActiveDocument の outSaved と、GetOpenFilesList の fIsInMemoryOnly だけ
//	（SDK Index に「変更あり」を読む宣言は無い。IDocument にも無い）。どちらが
//	「ディスクにあるか」で、どちらが「未保存の変更が無いか」なのかを、局面ごとに読む:
//
//	  新規 → 変更（undo の外 / MarkCurrentDocChanged）→ 保存 → 変更 → 上書き保存
//	  → undo イベント付きの変更 → 取り消し（保存時点へ戻る）→ 裏に回したとき
//
//	ブリッジ（vw_run_probe）から走らせれば、OS タイマーの刻みの中から呼んだことになる
//	（殻の McpBridge の CFRunLoopTimer / SetTimer の中で RunOne が走る）。
//	走り出しの図面には触れない。作業用の図面を自分で開き、最後に閉じて元へ戻る。
//	取り消し（DoMenuName("Undo")）が問いなので、自分の undo イベントを 1 つ開いて閉じる
//	（probes/runtime/README.md「図面を壊す前提で書く」の例外）。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	using namespace VectorWorks;

	std::string DirtyFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string DirtyPathOf(Filing::IFileIdentifier* pFileID)
	{
		if (pFileID == nullptr)
			return "(nullptr)";
		TXString path;
		pFileID->GetFileFullPath(path);
		return static_cast<const char*>(path);
	}

	Sint32 DirtyActiveFileRef()
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

	int DirtyCountObjects()
	{
		int n = 0;
		MCObjectHandle layer = gSDK->GetCurrentLayer();
		if (layer == nullptr)
			return -1;
		for (MCObjectHandle h = gSDK->FirstMemberObj(layer); h != nullptr; h = gSDK->NextObject(h))
			++n;
		return n;
	}

	// 局面ごとの読み。アクティブ文書の outSaved と、一覧の全項目を 1 行ずつ。
	void DirtySnap(vwprobe::Report& probe, const std::string& tag)
	{
		const auto t0 = std::chrono::steady_clock::now();
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		bool saved = false;
		const bool ok = gSDK->GetActiveDocument(&fileID, saved);
		const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		probe.log(tag + ": GetActiveDocument=" + DirtyFlag(ok) + " outSaved=" + DirtyFlag(saved) +
				  " objs=" + std::to_string(DirtyCountObjects()) + " (" + std::to_string(us) +
				  "us) path=" + DirtyPathOf(fileID));

		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			probe.log(tag + ":   [" + std::to_string(i) +
					  "] fileRef=" + std::to_string(files[i].fFileRef) +
					  " active=" + DirtyFlag(files[i].fIsActive) +
					  " inMemoryOnly=" + DirtyFlag(files[i].fIsInMemoryOnly) +
					  " path=" + DirtyPathOf(files[i].fpFileID));
		}
	}

	// まだ無い名前の保存先（既にあるファイルへの別名保存は通らなかった実測がある）。
	std::string DirtyFreshPath(int index)
	{
		std::error_code ec;
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path(ec) / "VwSdkProbes-doc-dirty-state";
		std::filesystem::create_directories(dir, ec);
		const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch())
									.count();
		return (dir / ("dirty-" + std::to_string(stamp) + "-" + std::to_string(index) + ".vwx"))
			.string();
	}

	short DirtySave(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(path.c_str())) != kVCOMError_NoError)
		{
			probe.log(tag + ": IFileIdentifier を作れなかった");
			return -999;
		}
		const auto t0 = std::chrono::steady_clock::now();
		const GSError err = gSDK->SaveActiveDocumentPath(fileID);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		probe.log(tag + ": SaveActiveDocumentPath(" + path + ") GSError=" + std::to_string(err) +
				  " (" + std::to_string(ms) + "ms)");
		return err;
	}

	void DirtyRect(vwprobe::Report& probe, const std::string& tag, double x)
	{
		const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(x, 10, x + 10, 0));
		probe.log(tag + ": CreateRectangle=" + DirtyFlag(h != nullptr) + "（undo イベントの外）");
	}
} // namespace

VW_PROBE("doc-dirty-state", "変更ありを読めるか", "outSaved と inMemoryOnly を読む")
{
	probe.log(std::string("走り出し: IsCurrentlyBuildingAnUndoEvent=") +
			  DirtyFlag(gSDK->IsCurrentlyBuildingAnUndoEvent()));
	const Sint32 originRef = DirtyActiveFileRef();
	DirtySnap(probe, "S0 走り出しの図面");

	// --- 作業用の図面を開く ---
	(void)gSDK->OpenDocumentPath(nullptr, false);
	const Sint32 workRef = DirtyActiveFileRef();
	if (workRef == originRef || workRef < 0)
	{
		probe.fail("作業用の図面を開けなかった");
		return;
	}
	std::vector<std::string> written;

	DirtySnap(probe, "A0 新規（無変更）");
	DirtyRect(probe, "A1", 0);
	DirtySnap(probe, "A1 新規に矩形");
	gSDK->MarkCurrentDocChanged();
	DirtySnap(probe, "A2 新規に MarkCurrentDocChanged");

	const std::string p1 = DirtyFreshPath(1);
	if (DirtySave(probe, "A3", p1) == 0)
		written.push_back(p1);
	DirtySnap(probe, "A3 保存した直後");

	DirtyRect(probe, "A4", 20);
	DirtySnap(probe, "A4 保存済みに矩形（undo の外）");
	gSDK->MarkCurrentDocChanged();
	DirtySnap(probe, "A5 保存済みに MarkCurrentDocChanged");

	// 同じパスへの上書き保存（＝「保存」）。通らなければ別名で保存する。
	if (DirtySave(probe, "A6", p1) != 0)
	{
		const std::string p2 = DirtyFreshPath(2);
		if (DirtySave(probe, "A6b", p2) == 0)
			written.push_back(p2);
	}
	DirtySnap(probe, "A6 上書き保存した直後");

	// --- undo イベント付きの変更（ふつうのプラグインの書き方）と、その取り消し ---
	gSDK->SetUndoMethod(kUndoSwapObjects);
	gSDK->NameUndoEvent(TXString("試験 #240"));
	const MCObjectHandle undoRect = gSDK->CreateRectangle(WorldRect(40, 10, 50, 0));
	const bool recorded = undoRect != nullptr && gSDK->AddAfterSwapObject(undoRect);
	probe.log(std::string("A7: undo イベントの中で矩形=") + DirtyFlag(undoRect != nullptr) +
			  " AddAfterSwapObject=" + DirtyFlag(recorded) +
			  " building=" + DirtyFlag(gSDK->IsCurrentlyBuildingAnUndoEvent()));
	DirtySnap(probe, "A7a undo イベントの中（閉じる前）");
	gSDK->EndUndoEvent();
	DirtySnap(probe, "A7 undo イベント付きで矩形（閉じた後）");

	const short undoResult = gSDK->DoMenuName(TXString("Undo"), 0);
	probe.log("A8: DoMenuName(\"Undo\", 0)=" + std::to_string(undoResult));
	DirtySnap(probe, "A8 取り消した直後（保存時点へ戻ったはず）");

	const short redoResult = gSDK->DoMenuName(TXString("Redo"), 0);
	probe.log("A9: DoMenuName(\"Redo\", 0)=" + std::to_string(redoResult));
	DirtySnap(probe, "A9 やり直した直後");

	// --- 変更ありのまま裏に回したとき ---
	gSDK->MarkCurrentDocChanged();
	DirtySnap(probe, "B0 作業用に MarkCurrentDocChanged");
	probe.log("B1: SwitchToOpenFile(走り出しの図面)=" +
			  DirtyFlag(gSDK->SwitchToOpenFile(originRef)));
	DirtySnap(probe, "B1 走り出しの図面へ切り替えた（作業用は裏）");
	probe.log("B2: SwitchToOpenFile(作業用)=" + DirtyFlag(gSDK->SwitchToOpenFile(workRef)));
	DirtySnap(probe, "B2 作業用へ戻した");

	// --- 片付け: 作業用を閉じて走り出しの図面へ戻る ---
	if (DirtyActiveFileRef() == workRef)
		(void)gSDK->CloseDocument(); // 戻り値は当てにならない（Findings「Documents」）
	(void)gSDK->SwitchToOpenFile(originRef);
	DirtySnap(probe, "Z 片付けた後");
	for (const std::string& path : written)
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
}
