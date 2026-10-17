//
//	probes/runtime/doc-close-confirm/probe.cpp
//
//	[issue #243] DoMenuName("Close", 0) で保存の確認が**出る**局面と、取り消したときに
//	図面が残るか。**確認が出たら人が「キャンセル」を押す**前提で走らせる（押すまで
//	呼び出しから戻らないはず。所要に出る）:
//
//	  C0 保存して閉じたファイルを OpenDocumentPath で開き直し、そのまま「閉じる」
//	     （doc-save-close では開いただけの図面で確認が出た）→ キャンセル →
//	     自分のパスへ上書き保存してから「閉じる」→ 確認なしに閉じるか
//	  C1 無題・無変更の図面を「閉じる」→ 確認 → キャンセル → 図面は残るか
//	  C2 保存済みの図面に矩形を足して「閉じる」→ 確認 → キャンセル → 図面は残るか
//	  C3 その図面を自分のパスへ上書き保存してから「閉じる」→ 確認なしに閉じるか
//	  C4 無題の図面を、ディスクにあるが開いていないファイルへ保存する（確認は出ない見込み）
//
//	ブリッジ（vw_run_probe）から走らせれば、OS タイマーの刻みの中から呼んだことになる。
//	走り出しの図面には触れない。作業用の図面を自分で開き、最後に CloseDocument で閉じる。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
	using namespace VectorWorks;

	std::string ConfirmFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	Sint32 ConfirmActiveRef()
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

	size_t ConfirmCount()
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		return files.GetSize();
	}

	bool ConfirmIsOpen(Sint32 ref)
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			if (files[i].fFileRef == ref)
				return true;
		}
		return false;
	}

	void ConfirmClose(vwprobe::Report& probe, const std::string& tag, Sint32 ref)
	{
		const size_t before = ConfirmCount();
		probe.log(tag + ": DoMenuName(\"Close\", 0) を呼ぶ（active fileRef=" +
				  std::to_string(ConfirmActiveRef()) + " 対象=" + std::to_string(ref) +
				  " 件数=" + std::to_string(before) + "）。確認が出たらキャンセルを押す");
		const auto t0 = std::chrono::steady_clock::now();
		const short r = gSDK->DoMenuName(TXString("Close"), 0);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		probe.log(tag + ": DoMenuName(\"Close\", 0)=" + std::to_string(r) + " (" +
				  std::to_string(ms) + "ms) 件数 " + std::to_string(before) + " → " +
				  std::to_string(ConfirmCount()) +
				  " 対象はまだ開いている=" + ConfirmFlag(ConfirmIsOpen(ref)) +
				  " active fileRef=" + std::to_string(ConfirmActiveRef()));
	}

	GSError ConfirmSave(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(path.c_str())) != kVCOMError_NoError)
			return -999;
		const auto t0 = std::chrono::steady_clock::now();
		const GSError err = gSDK->SaveActiveDocumentPath(fileID);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		probe.log(tag + ": SaveActiveDocumentPath GSError=" + std::to_string(err) + " (" +
				  std::to_string(ms) + "ms)");
		return err;
	}
} // namespace

VW_PROBE("doc-close-confirm", "閉じるの確認と取り消し", "確認が出たらキャンセルを押す")
{
	const Sint32 originRef = ConfirmActiveRef();
	probe.log("走り出し: active fileRef=" + std::to_string(originRef) +
			  " 件数=" + std::to_string(ConfirmCount()));

	std::error_code ec;
	const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::system_clock::now().time_since_epoch())
								.count();
	const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) /
									  ("VwSdkProbes-doc-close-confirm-" + std::to_string(stamp));
	std::filesystem::create_directories(dir, ec);

	// --- C0 開き直しただけの図面 ---
	{
		const std::string p0 = (dir / "c0.vwx").string();
		(void)gSDK->OpenDocumentPath(nullptr, false);
		const Sint32 r0 = ConfirmActiveRef();
		ConfirmSave(probe, "C0-0 新規を保存", p0);
		ConfirmClose(probe, "C0-1 保存した直後（確認は出ない見込み）", r0);
		if (ConfirmIsOpen(r0) && gSDK->SwitchToOpenFile(r0))
			(void)gSDK->CloseDocument();
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(p0.c_str()));
		const bool opened = gSDK->OpenDocumentPath(fileID, false);
		const Sint32 r1 = ConfirmActiveRef();
		probe.log("C0-2: OpenDocumentPath(c0.vwx)=" + ConfirmFlag(opened) +
				  " fileRef=" + std::to_string(r1));
		if (opened && r1 != originRef)
		{
			ConfirmClose(probe, "C0-3 開き直しただけ", r1);
			if (ConfirmIsOpen(r1) && gSDK->SwitchToOpenFile(r1))
			{
				ConfirmSave(probe, "C0-4 自分のパスへ上書き保存", p0);
				ConfirmClose(probe, "C0-5 開き直して上書き保存した直後", r1);
			}
			if (ConfirmIsOpen(r1) && gSDK->SwitchToOpenFile(r1))
				(void)gSDK->CloseDocument();
		}
		(void)gSDK->SwitchToOpenFile(originRef);
	}

	// --- C1 無題・無変更 ---
	(void)gSDK->OpenDocumentPath(nullptr, false);
	const Sint32 work = ConfirmActiveRef();
	if (work < 0 || work == originRef)
	{
		probe.fail("作業用の図面を開けなかった");
		return;
	}
	ConfirmClose(probe, "C1 無題・無変更", work);

	// --- C2 保存済みに変更 ---
	if (ConfirmIsOpen(work))
	{
		(void)gSDK->SwitchToOpenFile(work);
		const std::string path = (dir / "c2.vwx").string();
		ConfirmSave(probe, "C2-0 保存", path);
		const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		probe.log("C2-1: CreateRectangle=" + ConfirmFlag(h != nullptr));
		ConfirmClose(probe, "C2 保存済みに矩形を足した", work);

		// --- C3 上書き保存してから閉じる ---
		if (ConfirmIsOpen(work))
		{
			(void)gSDK->SwitchToOpenFile(work);
			ConfirmSave(probe, "C3-0 上書き保存", path);
			ConfirmClose(probe, "C3 上書き保存した直後", work);
		}
	}

	// --- C4 無題 → 開いていない既存ファイル ---
	{
		const std::string p4 = (dir / "c0.vwx").string(); // C0 で閉じた（開いていない）
		probe.log(std::string("C4: 保存先はディスクに") +
				  ConfirmFlag(std::filesystem::exists(p4, ec)));
		(void)gSDK->OpenDocumentPath(nullptr, false);
		const Sint32 r4 = ConfirmActiveRef();
		const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		probe.log("C4: CreateRectangle=" + ConfirmFlag(h != nullptr));
		ConfirmSave(probe, "C4 無題を開いていない既存ファイルへ", p4);
		if (r4 != originRef && ConfirmIsOpen(r4) && gSDK->SwitchToOpenFile(r4) &&
			ConfirmActiveRef() == r4)
			(void)gSDK->CloseDocument();
	}

	// --- 片付け ---
	if (ConfirmIsOpen(work) && gSDK->SwitchToOpenFile(work) && ConfirmActiveRef() == work)
		(void)gSDK->CloseDocument();
	(void)gSDK->SwitchToOpenFile(originRef);
	probe.log("Z 片付けた後: active fileRef=" + std::to_string(ConfirmActiveRef()) +
			  " 件数=" + std::to_string(ConfirmCount()));
	std::filesystem::remove_all(dir, ec);
}
