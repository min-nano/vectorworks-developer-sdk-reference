//
//	probes/runtime/doc-dirty-staged/probe.cpp
//
//	[issue #240] 「未保存の変更あり」を、**操作した実行の後（コマンドが返った後）**に読む。
//
//	1 回目の測定（doc-dirty-state）は 1 回の実行の中で「変更 → 読む」をしていたので、
//	VW が変更ありの印を付けるのがコマンドや undo イベントの終わりなら、その前に読んで
//	いたことになる。そこで **1 回の実行で 1 段だけ進め、その結果は次の実行の冒頭で読む**。
//	段の進み具合は一時ディレクトリの状態ファイルで持ち越す（同じプローブを 8 回走らせる）。
//
//	  段 0: 作業用の図面 W を開き、別名で保存する
//	  段 1: 読む → undo イベント付きで矩形を足す
//	  段 2: 読む → DoMenuName("Undo")（保存時点へ戻す）
//	  段 3: 読む → MarkCurrentDocChanged()
//	  段 4: 読む → 同じパスへ上書き保存
//	  段 5: 読む → undo イベントの外で矩形を足す
//	  段 6: 読む → ユーザーの操作の代わりに DoMenuName("Undo") をもう 1 度（何が戻るかを見る）
//	  段 7: 読む → W を閉じて片付ける（状態ファイルを消す。次はまた段 0 から）
//
//	読むのは GetActiveDocument の outSaved・GetOpenFilesList の fIsInMemoryOnly と、
//	**別の口として** macOS の窓の「編集済み」（NSWindow の isDocumentEdited。cocoa_windows.cpp）。
//	W は各段の終わりにアクティブのまま残す（取り消しが W に効くように）。
//	取り消しが問いなので自分の undo イベントを開いて閉じる（README「図面を壊す前提で書く」の例外）。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

std::string DirtyStagedCocoaWindows(); // cocoa_windows.cpp

namespace
{
	using namespace VectorWorks;

	struct StagedState
	{
		int stage = 0;
		Sint32 workRef = -1;
		std::string path;
	};

	std::string StagedFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	std::filesystem::path StagedDir()
	{
		std::error_code ec;
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path(ec) / "VwSdkProbes-doc-dirty-staged";
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	std::filesystem::path StagedStateFile()
	{
		return StagedDir() / "state.txt";
	}

	StagedState StagedLoad()
	{
		StagedState state;
		std::ifstream in(StagedStateFile());
		std::string line;
		while (std::getline(in, line))
		{
			if (line.rfind("stage=", 0) == 0)
				state.stage = std::stoi(line.substr(6));
			else if (line.rfind("ref=", 0) == 0)
				state.workRef = static_cast<Sint32>(std::stol(line.substr(4)));
			else if (line.rfind("path=", 0) == 0)
				state.path = line.substr(5);
		}
		return state;
	}

	void StagedStore(const StagedState& state)
	{
		std::ofstream out(StagedStateFile(), std::ios::trunc);
		out << "stage=" << state.stage << "\nref=" << state.workRef << "\npath=" << state.path
			<< "\n";
	}

	void StagedReset()
	{
		std::error_code ec;
		std::filesystem::remove(StagedStateFile(), ec);
	}

	std::string StagedPathOf(Filing::IFileIdentifier* pFileID)
	{
		if (pFileID == nullptr)
			return "(nullptr)";
		TXString path;
		pFileID->GetFileFullPath(path);
		return static_cast<const char*>(path);
	}

	Sint32 StagedActiveRef()
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

	// 作業用の図面を探す（fileRef が一致し、パスも一致するもの）。
	bool StagedFindWork(const StagedState& state)
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			if (files[i].fFileRef == state.workRef && StagedPathOf(files[i].fpFileID) == state.path)
				return true;
		}
		return false;
	}

	int StagedCountObjects()
	{
		const MCObjectHandle layer = gSDK->GetCurrentLayer();
		if (layer == nullptr)
			return -1;
		int n = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(layer); h != nullptr; h = gSDK->NextObject(h))
			++n;
		return n;
	}

	void StagedSnap(vwprobe::Report& probe, const std::string& tag)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		bool saved = false;
		const bool ok = gSDK->GetActiveDocument(&fileID, saved);
		probe.log(tag + ": outSaved=" + StagedFlag(saved) + "（GetActiveDocument=" +
				  StagedFlag(ok) + "） objs=" + std::to_string(StagedCountObjects()) +
				  " building=" + StagedFlag(gSDK->IsCurrentlyBuildingAnUndoEvent()) +
				  " path=" + StagedPathOf(fileID));
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			probe.log(tag + ":   [" + std::to_string(i) +
					  "] fileRef=" + std::to_string(files[i].fFileRef) +
					  " active=" + StagedFlag(files[i].fIsActive) +
					  " inMemoryOnly=" + StagedFlag(files[i].fIsInMemoryOnly) +
					  " path=" + StagedPathOf(files[i].fpFileID));
		}
		probe.log(tag + ":   窓: " + DirtyStagedCocoaWindows());
	}

	short StagedSave(vwprobe::Report& probe, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(path.c_str())) != kVCOMError_NoError)
		{
			probe.log("IFileIdentifier を作れなかった");
			return -999;
		}
		const GSError err = gSDK->SaveActiveDocumentPath(fileID);
		probe.log("SaveActiveDocumentPath(" + path + ") GSError=" + std::to_string(err));
		return err;
	}

	void StagedUndo(vwprobe::Report& probe)
	{
		const int before = StagedCountObjects();
		const short result = gSDK->DoMenuName(TXString("Undo"), 0);
		probe.log("DoMenuName(\"Undo\", 0)=" + std::to_string(result) + " objs: " +
				  std::to_string(before) + " → " + std::to_string(StagedCountObjects()));
	}
} // namespace

VW_PROBE("doc-dirty-staged", "変更ありを次の実行で読む", "1 回 1 段。8 回走らせる")
{
	StagedState state = StagedLoad();
	probe.log("段 " + std::to_string(state.stage) +
			  "（走り出し: building=" + StagedFlag(gSDK->IsCurrentlyBuildingAnUndoEvent()) + "）");

	if (state.stage == 0)
	{
		StagedSnap(probe, "段0 走り出し");
		(void)gSDK->OpenDocumentPath(nullptr, false);
		const Sint32 ref = StagedActiveRef();
		StagedSnap(probe, "段0 作業用を開いた直後（新規・無変更）");
		const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch())
									.count();
		const std::string path =
			(StagedDir() / ("work-" + std::to_string(stamp) + ".vwx")).string();
		if (StagedSave(probe, path) != 0)
		{
			probe.fail("作業用の図面を保存できなかった");
			return;
		}
		StagedSnap(probe, "段0 保存した直後");
		state.workRef = ref;
		state.path = path;
		state.stage = 1;
		StagedStore(state);
		probe.log("次の実行で段 1（保存の後を読む → undo 付きで変更）");
		return;
	}

	if (!StagedFindWork(state))
	{
		StagedReset();
		probe.fail("作業用の図面が見つからない（閉じられた？）。状態を消したので次は段 0 から");
		return;
	}
	probe.log("SwitchToOpenFile(作業用)=" + StagedFlag(gSDK->SwitchToOpenFile(state.workRef)));
	StagedSnap(probe, "段" + std::to_string(state.stage) + " 冒頭（前の実行が返った後）");

	switch (state.stage)
	{
	case 1:
	{
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent(TXString("試験 #240 矩形"));
		const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		const bool recorded = h != nullptr && gSDK->AddAfterSwapObject(h);
		probe.log("undo イベントの中で矩形=" + StagedFlag(h != nullptr) +
				  " AddAfterSwapObject=" + StagedFlag(recorded));
		gSDK->EndUndoEvent();
		StagedSnap(probe, "段1 EndUndoEvent の直後");
		probe.log("次の実行で段 2（変更の後を読む → 取り消す）");
		break;
	}
	case 2:
		StagedUndo(probe);
		StagedSnap(probe, "段2 取り消した直後");
		probe.log("次の実行で段 3（取り消しの後を読む → MarkCurrentDocChanged）");
		break;
	case 3:
		gSDK->MarkCurrentDocChanged();
		StagedSnap(probe, "段3 MarkCurrentDocChanged の直後");
		probe.log("次の実行で段 4（その後を読む → 上書き保存）");
		break;
	case 4:
		(void)StagedSave(probe, state.path);
		StagedSnap(probe, "段4 上書き保存の直後");
		probe.log("次の実行で段 5（保存の後を読む → undo の外で変更）");
		break;
	case 5:
	{
		const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(20, 10, 30, 0));
		probe.log("undo イベントの外で矩形=" + StagedFlag(h != nullptr));
		StagedSnap(probe, "段5 矩形の直後");
		probe.log("次の実行で段 6（その後を読む → もう 1 度取り消す）");
		break;
	}
	case 6:
		StagedUndo(probe);
		StagedSnap(probe, "段6 取り消した直後");
		probe.log("次の実行で段 7（その後を読む → 片付け）");
		break;
	default:
	{
		if (StagedActiveRef() == state.workRef)
			(void)gSDK->CloseDocument(); // 戻り値は当てにならない（Findings「Documents」）
		std::error_code ec;
		std::filesystem::remove(state.path, ec);
		StagedReset();
		StagedSnap(probe, "片付けた後");
		probe.log("作業用の図面を閉じた。状態を消したので次は段 0 から");
		return;
	}
	}
	state.stage += 1;
	StagedStore(state);
}
