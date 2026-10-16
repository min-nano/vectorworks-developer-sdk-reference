//
//	probes/runtime/doc-dirty-staged/probe.cpp
//
//	[issue #240] 「未保存の変更あり」を、**操作した実行が返った後**に読み、**正解（メニューの
//	「閉じる」で保存の確認が出るか）**と突き合わせる。
//
//	1 回目の測定（#241 の doc-dirty-state）は 1 回の実行の中で「変更 → 読む」をしていたので、
//	VW が変更ありの印をコマンドや undo イベントの終わりに付けるなら読み損ねている。そこで
//	**1 回の実行で 1 段だけ進め**、段の進み具合は一時ディレクトリの状態ファイルで持ち越す。
//
//	ケースごとに:
//	  設定の実行   … 作業用の図面 W を開き（保存済みのケースは別名で保存し）、操作を 1 つする
//	  途中の実行   … （取り消しのケースだけ）DoMenuName("Undo")
//	  確かめる実行 … outSaved・fIsInMemoryOnly・窓の「編集済み」を読み、DoMenuName で
//	                 「閉じる」を呼んで、保存の確認が出たかを機械で記録する（cocoa_windows.cpp。
//	                 出たら abortModal で閉じる）。W が残っていれば CloseDocument で捨てる。
//
//	取り消しが問いなので自分の undo イベントを開いて閉じる（README「図面を壊す前提で書く」の例外）。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// cocoa_windows.cpp
std::string DirtyStagedCocoaWindows();
void DirtyStagedWatchStart();
std::string DirtyStagedWatchStop();

namespace
{
	using namespace VectorWorks;

	enum class StagedAction
	{
		kNothing,
		kUndoRect,	  // undo イベント付きで矩形
		kPlainRect,	  // undo イベントの外で矩形
		kMarkChanged, // MarkCurrentDocChanged()
		kUndo,		  // DoMenuName("Undo")
	};

	struct StagedCase
	{
		const char* name;
		bool saveFirst;
		StagedAction setup;
		StagedAction mid;
	};

	const StagedCase kStagedCases[] = {
		{"保存済み・無変更", true, StagedAction::kNothing, StagedAction::kNothing},
		{"保存済み・undo 付きの変更", true, StagedAction::kUndoRect, StagedAction::kNothing},
		{"保存済み・undo 付きの変更 → 取り消し", true, StagedAction::kUndoRect,
		 StagedAction::kUndo},
		{"保存済み・MarkCurrentDocChanged", true, StagedAction::kMarkChanged,
		 StagedAction::kNothing},
		{"保存済み・undo の外の変更", true, StagedAction::kPlainRect, StagedAction::kNothing},
		{"無題・無変更", false, StagedAction::kNothing, StagedAction::kNothing},
		{"無題・undo 付きの変更", false, StagedAction::kUndoRect, StagedAction::kNothing},
		{"無題・MarkCurrentDocChanged", false, StagedAction::kMarkChanged, StagedAction::kNothing},
	};
	const int kStagedCaseCount = static_cast<int>(sizeof(kStagedCases) / sizeof(kStagedCases[0]));

	// 「閉じる」の内部名の候補（DoMenuName は居ない名前に -3 を返す）。
	const char* const kStagedCloseNames[] = {"Close", "CloseDoc", "Close Document", "CloseFile"};

	struct StagedState
	{
		int caseIndex = 0;
		int phase = 0; // 0=設定 1=途中 2=確かめる
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
			if (line.rfind("case=", 0) == 0)
				state.caseIndex = std::stoi(line.substr(5));
			else if (line.rfind("phase=", 0) == 0)
				state.phase = std::stoi(line.substr(6));
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
		out << "case=" << state.caseIndex << "\nphase=" << state.phase << "\nref=" << state.workRef
			<< "\npath=" << state.path << "\n";
	}

	std::string StagedPathOf(Filing::IFileIdentifier* pFileID)
	{
		if (pFileID == nullptr)
			return "(nullptr)";
		TXString path;
		pFileID->GetFileFullPath(path);
		return static_cast<const char*>(path);
	}

	Sint32 StagedActiveRef(std::string* outPath = nullptr)
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			if (files[i].fIsActive)
			{
				if (outPath != nullptr)
					*outPath = StagedPathOf(files[i].fpFileID);
				return files[i].fFileRef;
			}
		}
		return -1;
	}

	size_t StagedOpenCount()
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		return files.GetSize();
	}

	bool StagedWorkOpen(const StagedState& state)
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

	void StagedSnap(vwprobe::Report& probe, const std::string& tag)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		bool saved = false;
		(void)gSDK->GetActiveDocument(&fileID, saved);
		bool inMemoryOnly = false;
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			if (files[i].fIsActive)
				inMemoryOnly = files[i].fIsInMemoryOnly;
		}
		probe.log(tag + ": outSaved=" + StagedFlag(saved) +
				  " inMemoryOnly=" + StagedFlag(inMemoryOnly) +
				  " building=" + StagedFlag(gSDK->IsCurrentlyBuildingAnUndoEvent()) +
				  " 図面=" + std::to_string(files.GetSize()) + " 枚 path=" + StagedPathOf(fileID));
		probe.log(tag + ":   窓: " + DirtyStagedCocoaWindows());
	}

	void StagedDo(vwprobe::Report& probe, StagedAction action)
	{
		switch (action)
		{
		case StagedAction::kNothing:
			probe.log("操作: なし");
			break;
		case StagedAction::kUndoRect:
		{
			gSDK->SetUndoMethod(kUndoSwapObjects);
			gSDK->NameUndoEvent(TXString("試験 #240 矩形"));
			const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
			const bool recorded = h != nullptr && gSDK->AddAfterSwapObject(h);
			gSDK->EndUndoEvent();
			probe.log("操作: undo イベント付きで矩形=" + StagedFlag(h != nullptr) +
					  " AddAfterSwapObject=" + StagedFlag(recorded) + "（EndUndoEvent 済み）");
			break;
		}
		case StagedAction::kPlainRect:
		{
			const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(20, 10, 30, 0));
			probe.log("操作: undo イベントの外で矩形=" + StagedFlag(h != nullptr));
			break;
		}
		case StagedAction::kMarkChanged:
			gSDK->MarkCurrentDocChanged();
			probe.log("操作: MarkCurrentDocChanged()");
			break;
		case StagedAction::kUndo:
			probe.log("操作: DoMenuName(\"Undo\", 0)=" +
					  std::to_string(gSDK->DoMenuName(TXString("Undo"), 0)));
			break;
		}
	}
} // namespace

VW_PROBE("doc-dirty-staged", "変更ありを次の実行で読む", "1 回 1 段。閉じて確認が出るかと比べる")
{
	StagedState state = StagedLoad();
	if (state.caseIndex >= kStagedCaseCount)
	{
		std::error_code ec;
		std::filesystem::remove(StagedStateFile(), ec);
		probe.log("全ケース終わり。状態を消したので、次はケース 0 から");
		return;
	}
	const StagedCase& c = kStagedCases[state.caseIndex];
	probe.log("ケース " + std::to_string(state.caseIndex) + "「" + c.name + "」 段 " +
			  std::to_string(state.phase) +
			  "（走り出し: building=" + StagedFlag(gSDK->IsCurrentlyBuildingAnUndoEvent()) + "）");

	if (state.phase == 0)
	{
		(void)gSDK->OpenDocumentPath(nullptr, false);
		std::string path;
		const Sint32 ref = StagedActiveRef(&path);
		if (c.saveFirst)
		{
			const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
										std::chrono::system_clock::now().time_since_epoch())
										.count();
			path = (StagedDir() / ("work-" + std::to_string(stamp) + ".vwx")).string();
			Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
			const GSError err =
				(fileID && fileID->Set(TXString(path.c_str())) == kVCOMError_NoError)
					? gSDK->SaveActiveDocumentPath(fileID)
					: -999;
			probe.log("SaveActiveDocumentPath GSError=" + std::to_string(err));
			if (err != 0)
			{
				probe.fail("作業用の図面を保存できなかった");
				return;
			}
		}
		StagedSnap(probe, "設定 操作の前");
		StagedDo(probe, c.setup);
		StagedSnap(probe, "設定 操作の直後（同じ実行の中）");
		state.workRef = ref;
		state.path = path;
		state.phase = (c.mid != StagedAction::kNothing) ? 1 : 2;
		StagedStore(state);
		probe.log("作業用の図面をアクティブのまま返す。次の実行で段 " +
				  std::to_string(state.phase));
		return;
	}

	if (!StagedWorkOpen(state))
	{
		probe.log("作業用の図面が見つからない。このケースを飛ばす");
		state.caseIndex += 1;
		state.phase = 0;
		StagedStore(state);
		return;
	}
	probe.log("SwitchToOpenFile(作業用)=" + StagedFlag(gSDK->SwitchToOpenFile(state.workRef)));

	if (state.phase == 1)
	{
		StagedSnap(probe, "途中 冒頭（前の実行が返った後）");
		StagedDo(probe, c.mid);
		StagedSnap(probe, "途中 操作の直後");
		state.phase = 2;
		StagedStore(state);
		probe.log("作業用の図面をアクティブのまま返す。次の実行で段 2");
		return;
	}

	// --- 確かめる ---
	StagedSnap(probe, "確かめる 冒頭（前の実行が返った後）");
	const size_t before = StagedOpenCount();
	for (const char* name : kStagedCloseNames)
	{
		DirtyStagedWatchStart();
		const auto t0 = std::chrono::steady_clock::now();
		const short result = gSDK->DoMenuName(TXString(name), 0);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		const std::string seen = DirtyStagedWatchStop();
		probe.log(std::string("DoMenuName(\"") + name + "\", 0)=" + std::to_string(result) + " (" +
				  std::to_string(ms) + "ms) " + seen);
		if (result != -3)
			break;
	}
	const bool stillOpen = StagedWorkOpen(state);
	probe.log("閉じる の後: 図面 " + std::to_string(before) + " → " +
			  std::to_string(StagedOpenCount()) + " 枚・作業用は" +
			  (stillOpen ? "残っている" : "閉じた"));
	if (stillOpen && gSDK->SwitchToOpenFile(state.workRef))
	{
		(void)gSDK->CloseDocument(); // 確認なしで捨てる（Findings「Documents」）
		probe.log("作業用を CloseDocument で捨てた: 図面 " + std::to_string(StagedOpenCount()) +
				  " 枚");
	}
	if (c.saveFirst)
	{
		std::error_code ec;
		std::filesystem::remove(state.path, ec);
	}
	state.caseIndex += 1;
	state.phase = 0;
	StagedStore(state);
	probe.log("次の実行はケース " + std::to_string(state.caseIndex));
}
