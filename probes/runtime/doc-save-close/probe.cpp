//
//	probes/runtime/doc-save-close/probe.cpp
//
//	[issue #243] 指定した図面の保存（新しいパス）と「閉じる」を、前面の図面に依存せず
//	行えるか。保存の確認が**出ないはずの**局面だけを並べる（確認が出る局面は
//	doc-close-confirm に分けてある——人が応えないと先へ進まないため）:
//
//	  Q5a 無題の図面を 2 枚開き、名前（パス）の振られ方を見る
//	  Q1  無題の図面を新しいパスへ保存する（GSError・保存後のパス・fIsInMemoryOnly）
//	  Q2  保存済みの図面を別のパスへ保存する（図面のパスは変わるか・元のファイルは残るか）
//	  Q4  保存済みで無変更の図面を DoMenuName("Close", 0) で閉じる（所要・件数）
//	  Q3  前面でない図面を SwitchToOpenFile → 保存 → 閉じる → 元の前面へ戻す
//	  Q1b 既にあるファイル（開いていない / 別の図面が開いている）への保存
//	  Q5b 同じファイル名・別のフォルダの図面を 2 枚
//
//	ブリッジ（vw_run_probe）から走らせれば、OS タイマーの刻みの中から呼んだことになる。
//	走り出しの図面には触れない。作業用の図面を自分で開き、最後に CloseDocument で閉じる。
//	**確認が出るまで秒単位で待てば所要に出る**（モーダルなら人を待つ）ので、所要を必ず書く。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	using namespace VectorWorks;

	std::string SaveCloseFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string SaveClosePathOf(Filing::IFileIdentifier* pFileID)
	{
		if (pFileID == nullptr)
			return "(nullptr)";
		TXString path;
		pFileID->GetFileFullPath(path);
		return static_cast<const char*>(path);
	}

	struct SaveCloseDoc
	{
		Sint32 ref = -1;
		bool active = false;
		bool inMemoryOnly = false;
		std::string path;
	};

	std::vector<SaveCloseDoc> SaveCloseList()
	{
		std::vector<SaveCloseDoc> out;
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			SaveCloseDoc d;
			d.ref = files[i].fFileRef;
			d.active = files[i].fIsActive;
			d.inMemoryOnly = files[i].fIsInMemoryOnly;
			d.path = SaveClosePathOf(files[i].fpFileID);
			out.push_back(d);
		}
		return out;
	}

	Sint32 SaveCloseActiveRef()
	{
		for (const SaveCloseDoc& d : SaveCloseList())
		{
			if (d.active)
				return d.ref;
		}
		return -1;
	}

	bool SaveCloseIsOpen(Sint32 ref)
	{
		for (const SaveCloseDoc& d : SaveCloseList())
		{
			if (d.ref == ref)
				return true;
		}
		return false;
	}

	std::string SaveClosePathOfRef(Sint32 ref)
	{
		for (const SaveCloseDoc& d : SaveCloseList())
		{
			if (d.ref == ref)
				return d.path;
		}
		return "(開いていない)";
	}

	void SaveCloseSnap(vwprobe::Report& probe, const std::string& tag)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		bool saved = false;
		const bool ok = gSDK->GetActiveDocument(&fileID, saved);
		probe.log(tag + ": GetActiveDocument=" + SaveCloseFlag(ok) +
				  " outSaved=" + SaveCloseFlag(saved) + " path=" + SaveClosePathOf(fileID));
		const std::vector<SaveCloseDoc> docs = SaveCloseList();
		probe.log(tag + ":   件数=" + std::to_string(docs.size()));
		for (size_t i = 0; i < docs.size(); ++i)
		{
			const std::filesystem::path p(docs[i].path);
			probe.log(tag + ":   [" + std::to_string(i) + "] fileRef=" +
					  std::to_string(docs[i].ref) + " active=" + SaveCloseFlag(docs[i].active) +
					  " inMemoryOnly=" + SaveCloseFlag(docs[i].inMemoryOnly) +
					  " name=" + p.filename().string() + " path=" + docs[i].path);
		}
	}

	std::filesystem::path SaveCloseDir()
	{
		std::error_code ec;
		const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch())
									.count();
		const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) /
										  ("VwSdkProbes-doc-save-close-" + std::to_string(stamp));
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	std::string SaveCloseFileState(const std::string& path)
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return "無い";
		const auto size = std::filesystem::file_size(path, ec);
		const auto mtime = std::filesystem::last_write_time(path, ec);
		const long long ticks = static_cast<long long>(
			std::chrono::duration_cast<std::chrono::milliseconds>(mtime.time_since_epoch())
				.count());
		return "ある size=" + std::to_string(size) + " mtime=" + std::to_string(ticks);
	}

	GSError SaveCloseSave(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(path.c_str())) != kVCOMError_NoError)
		{
			probe.log(tag + ": IFileIdentifier を作れなかった");
			return -999;
		}
		probe.log(tag + ": SaveActiveDocumentPath を呼ぶ（保存先 " + SaveCloseFileState(path) +
				  "）");
		const auto t0 = std::chrono::steady_clock::now();
		const GSError err = gSDK->SaveActiveDocumentPath(fileID);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		probe.log(tag + ": SaveActiveDocumentPath(" + path + ") GSError=" + std::to_string(err) +
				  " (" + std::to_string(ms) + "ms) 保存先は " + SaveCloseFileState(path));
		return err;
	}

	// アクティブな図面を「閉じる」メニューで閉じる。閉じたかは件数と fileRef で見る。
	void SaveCloseMenuClose(vwprobe::Report& probe, const std::string& tag, Sint32 expectRef)
	{
		const Sint32 activeRef = SaveCloseActiveRef();
		const size_t before = SaveCloseList().size();
		probe.log(tag +
				  ": DoMenuName(\"Close\", 0) を呼ぶ（active fileRef=" + std::to_string(activeRef) +
				  " 期待=" + std::to_string(expectRef) + " 件数=" + std::to_string(before) + "）");
		const auto t0 = std::chrono::steady_clock::now();
		const short r = gSDK->DoMenuName(TXString("Close"), 0);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		probe.log(tag + ": DoMenuName(\"Close\", 0)=" + std::to_string(r) + " (" +
				  std::to_string(ms) + "ms) 件数 " + std::to_string(before) + " → " +
				  std::to_string(SaveCloseList().size()) +
				  " 対象はまだ開いている=" + SaveCloseFlag(SaveCloseIsOpen(expectRef)) +
				  " 閉じた後の active fileRef=" + std::to_string(SaveCloseActiveRef()));
	}

	Sint32 SaveCloseOpenNew(vwprobe::Report& probe, const std::string& tag)
	{
		const bool ok = gSDK->OpenDocumentPath(nullptr, false);
		const Sint32 ref = SaveCloseActiveRef();
		probe.log(tag + ": OpenDocumentPath(nullptr)=" + SaveCloseFlag(ok) +
				  " fileRef=" + std::to_string(ref) + " path=" + SaveClosePathOfRef(ref));
		return ref;
	}

	void SaveCloseRect(vwprobe::Report& probe, const std::string& tag, double x)
	{
		const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(x, 10, x + 10, 0));
		probe.log(tag + ": CreateRectangle=" + SaveCloseFlag(h != nullptr));
	}

	int SaveCloseCountObjects()
	{
		MCObjectHandle layer = gSDK->GetCurrentLayer();
		if (layer == nullptr)
			return -1;
		int n = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(layer); h != nullptr; h = gSDK->NextObject(h))
			++n;
		return n;
	}
} // namespace

VW_PROBE("doc-save-close", "図面の保存と閉じる", "指定した図面を新しいパスへ保存し、閉じる")
{
	const Sint32 originRef = SaveCloseActiveRef();
	const MCObjectHandle originLayer = gSDK->GetCurrentLayer();
	TXString originLayerName;
	if (originLayer != nullptr)
		gSDK->GetObjectName(originLayer, originLayerName);
	probe.log("走り出し: active fileRef=" + std::to_string(originRef) +
			  " カレントレイヤ=" + static_cast<const char*>(originLayerName));
	SaveCloseSnap(probe, "S0 走り出し");

	const std::filesystem::path dir = SaveCloseDir();
	probe.log("作業フォルダ: " + dir.string());
	std::vector<Sint32> owned;

	// --- Q5a 無題の図面を 2 枚 ---
	const Sint32 u1 = SaveCloseOpenNew(probe, "Q5a-1");
	const Sint32 u2 = SaveCloseOpenNew(probe, "Q5a-2");
	owned.push_back(u1);
	owned.push_back(u2);
	SaveCloseSnap(probe, "Q5a 無題を 2 枚開いた");
	if (u1 < 0 || u2 < 0 || u1 == u2 || u1 == originRef)
	{
		probe.fail("作業用の図面を開けなかった");
		return;
	}

	// --- Q1 無題の図面を新しいパスへ ---
	probe.log("Q1: SwitchToOpenFile(u1)=" + SaveCloseFlag(gSDK->SwitchToOpenFile(u1)));
	SaveCloseRect(probe, "Q1", 0);
	const std::string p1 = (dir / "q1-untitled.vwx").string();
	SaveCloseSave(probe, "Q1", p1);
	SaveCloseSnap(probe, "Q1 無題を保存した直後");

	// --- Q2 保存済みの図面を別のパスへ ---
	const std::string p1Before = SaveCloseFileState(p1);
	SaveCloseRect(probe, "Q2", 20); // p1 と中身を変える（p1 は図形 2・p2 は図形 3 のはず）
	const std::string p2 = (dir / "q2-saveas.vwx").string();
	SaveCloseSave(probe, "Q2", p2);
	SaveCloseSnap(probe, "Q2 別のパスへ保存した直後");
	probe.log("Q2: 元のファイル p1 は 保存前 " + p1Before + " / 保存後 " + SaveCloseFileState(p1));
	probe.log("Q2: u1 のパス=" + SaveClosePathOfRef(u1));

	// 元のファイル p1 を開いてみる（u1 が p1 から離れたなら別の図面として開けるはず）
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(p1.c_str()));
		const auto t0 = std::chrono::steady_clock::now();
		const bool opened = gSDK->OpenDocumentPath(fileID, false);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		const Sint32 r1 = SaveCloseActiveRef();
		probe.log("Q2: OpenDocumentPath(p1)=" + SaveCloseFlag(opened) + " (" + std::to_string(ms) +
				  "ms) active fileRef=" + std::to_string(r1) +
				  " 図形数=" + std::to_string(SaveCloseCountObjects()));
		if (opened && r1 != u1)
		{
			owned.push_back(r1);
			SaveCloseSnap(probe, "Q2 p1 を開いた");
			// --- Q4 保存済み・無変更を「閉じる」で ---
			SaveCloseMenuClose(probe, "Q4a p1（開いただけ・無変更）", r1);
		}
	}

	// u1 は p2 へ保存した直後で無変更のはず
	probe.log("Q4b: SwitchToOpenFile(u1)=" + SaveCloseFlag(gSDK->SwitchToOpenFile(u1)) +
			  " 図形数=" + std::to_string(SaveCloseCountObjects()));
	SaveCloseMenuClose(probe, "Q4b u1（保存した直後・無変更）", u1);
	SaveCloseSnap(probe, "Q4 閉じた後");

	// --- Q3 前面でない図面を指定して保存・閉じる ---
	probe.log("Q3: SwitchToOpenFile(origin)=" + SaveCloseFlag(gSDK->SwitchToOpenFile(originRef)));
	SaveCloseSnap(probe, "Q3-0 前面を走り出しの図面にした（u2 は裏）");
	probe.log("Q3-1: SwitchToOpenFile(u2)=" + SaveCloseFlag(gSDK->SwitchToOpenFile(u2)));
	SaveCloseRect(probe, "Q3-1", 40);
	const std::string p3 = (dir / "q3-background.vwx").string();
	SaveCloseSave(probe, "Q3-2", p3);
	probe.log("Q3-3: SwitchToOpenFile(origin)=" + SaveCloseFlag(gSDK->SwitchToOpenFile(originRef)));
	{
		const MCObjectHandle layer = gSDK->GetCurrentLayer();
		TXString name;
		if (layer != nullptr)
			gSDK->GetObjectName(layer, name);
		probe.log("Q3-3: 戻した後 active fileRef=" + std::to_string(SaveCloseActiveRef()) +
				  "（走り出し " + std::to_string(originRef) + "） カレントレイヤ同じハンドル=" +
				  SaveCloseFlag(layer == originLayer) + " 名前=" + static_cast<const char*>(name));
	}
	SaveCloseSnap(probe, "Q3-3 保存の後、前面を戻した");
	probe.log("Q3-4: SwitchToOpenFile(u2)=" + SaveCloseFlag(gSDK->SwitchToOpenFile(u2)));
	SaveCloseMenuClose(probe, "Q3-4 u2（裏から呼び出して保存済み・無変更）", u2);
	probe.log("Q3-5: SwitchToOpenFile(origin)=" + SaveCloseFlag(gSDK->SwitchToOpenFile(originRef)));
	{
		const MCObjectHandle layer = gSDK->GetCurrentLayer();
		probe.log("Q3-5: 閉じた後に戻した active fileRef=" + std::to_string(SaveCloseActiveRef()) +
				  " カレントレイヤ同じハンドル=" + SaveCloseFlag(layer == originLayer));
	}
	SaveCloseSnap(probe, "Q3-5 閉じて前面を戻した");

	// --- Q5b 同じファイル名・別のフォルダ ---
	std::error_code ec;
	std::filesystem::create_directories(dir / "a", ec);
	std::filesystem::create_directories(dir / "b", ec);
	const Sint32 s1 = SaveCloseOpenNew(probe, "Q5b-1");
	owned.push_back(s1);
	SaveCloseSave(probe, "Q5b-1", (dir / "a" / "same.vwx").string());
	const Sint32 s2 = SaveCloseOpenNew(probe, "Q5b-2");
	owned.push_back(s2);
	SaveCloseSave(probe, "Q5b-2", (dir / "b" / "same.vwx").string());
	SaveCloseSnap(probe, "Q5b 同じ名前の図面を 2 枚");

	// --- Q1b 既にあるファイルへの保存（最後に。確認が出れば所要に出る） ---
	// (1) ディスクにあるが開いていない p1（Q4a で閉じた）へ、無題の図面を保存する
	const Sint32 e1 = SaveCloseOpenNew(probe, "Q1b-1");
	owned.push_back(e1);
	SaveCloseRect(probe, "Q1b-1", 60);
	SaveCloseSave(probe, "Q1b-1 開いていない既存ファイル", p1);
	SaveCloseSnap(probe, "Q1b-1 の後");
	// (2) 保存済みの図面 s2 を、開いていない既存ファイル p2（Q4b で閉じた）へ
	probe.log("Q1b-2: SwitchToOpenFile(s2)=" + SaveCloseFlag(gSDK->SwitchToOpenFile(s2)));
	SaveCloseSave(probe, "Q1b-2 保存済みの図面を開いていない既存ファイルへ", p2);
	SaveCloseSnap(probe, "Q1b-2 の後");
	// (3) 別の図面 s1 が開いているファイル（a/same.vwx）へ、無題の図面を保存する
	const Sint32 e2 = SaveCloseOpenNew(probe, "Q1b-3");
	owned.push_back(e2);
	SaveCloseSave(probe, "Q1b-3 別の図面が開いているファイル", (dir / "a" / "same.vwx").string());
	SaveCloseSnap(probe, "Q1b-3 の後");

	// --- 片付け: 自分で開いた図面を捨てて閉じる ---
	for (const Sint32 ref : owned)
	{
		if (ref == originRef || !SaveCloseIsOpen(ref))
			continue;
		if (gSDK->SwitchToOpenFile(ref) && SaveCloseActiveRef() == ref)
			(void)gSDK->CloseDocument(); // 戻り値は当てにならない（Findings「Documents」）
	}
	(void)gSDK->SwitchToOpenFile(originRef);
	SaveCloseSnap(probe, "Z 片付けた後");
	std::filesystem::remove_all(dir, ec);
}
