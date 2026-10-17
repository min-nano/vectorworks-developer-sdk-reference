//
//	probes/runtime/doc-close-next-tick/probe.cpp
//
//	[issue #243] 「閉じる」の保存の確認が、**Vectorworks に制御を一度返した後**でも同じか。
//	doc-close-confirm では、同じ実行の中で
//	  ・保存 → 矩形を足す → 閉じる …… 確認が出ずに閉じた（矩形は捨てられた）
//	  ・保存済みのファイルを開く → 閉じる …… 確認が出た
//	となった。変更ありの印が、制御が戻ったとき（イベントループ）に付く・外れるのではないか、
//	を切り分ける。**2 回走らせる**（状態は一時フォルダのファイルに持つ）:
//
//	  1 回目（用意）: A = 保存してから矩形を足した図面 / B = 保存したファイルを開き直した図面
//	                 / C = 保存しただけの図面（対照）を作って、そのまま戻る
//	  2 回目（閉じる）: A・B・C の順に DoMenuName("Close", 0)。確認が出たら人がキャンセル
//
//	2 回目はブリッジの new_document=false で走らせる（1 回目の図面を閉じさせないため）。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	using namespace VectorWorks;

	std::string NextTickFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	Sint32 NextTickActiveRef()
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

	size_t NextTickCount()
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		return files.GetSize();
	}

	bool NextTickIsOpen(Sint32 ref)
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

	std::filesystem::path NextTickDir()
	{
		std::error_code ec;
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path(ec) / "VwSdkProbes-doc-close-next-tick";
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	GSError NextTickSave(vwprobe::Report& probe, const std::string& tag, const std::string& path)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(path.c_str())) != kVCOMError_NoError)
			return -999;
		const GSError err = gSDK->SaveActiveDocumentPath(fileID);
		probe.log(tag + ": SaveActiveDocumentPath(" + path + ") GSError=" + std::to_string(err));
		return err;
	}

	void NextTickClose(vwprobe::Report& probe, const std::string& tag, Sint32 ref)
	{
		if (!NextTickIsOpen(ref))
		{
			probe.log(tag + ": 開いていない（fileRef=" + std::to_string(ref) + "）");
			return;
		}
		probe.log(tag + ": SwitchToOpenFile=" + NextTickFlag(gSDK->SwitchToOpenFile(ref)));
		const size_t before = NextTickCount();
		probe.log(tag + ": DoMenuName(\"Close\", 0) を呼ぶ（確認が出たらキャンセル）");
		const auto t0 = std::chrono::steady_clock::now();
		const short r = gSDK->DoMenuName(TXString("Close"), 0);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		probe.log(tag + ": DoMenuName(\"Close\", 0)=" + std::to_string(r) + " (" +
				  std::to_string(ms) + "ms) 件数 " + std::to_string(before) + " → " +
				  std::to_string(NextTickCount()) +
				  " まだ開いている=" + NextTickFlag(NextTickIsOpen(ref)));
	}
} // namespace

VW_PROBE("doc-close-next-tick", "閉じるの確認（制御を返した後）", "2 回走らせる。用意と閉じる")
{
	const std::filesystem::path dir = NextTickDir();
	const std::filesystem::path state = dir / "state.txt";
	const Sint32 originRef = NextTickActiveRef();
	std::error_code ec;

	Sint32 refA = -1, refB = -1, refC = -1;
	{
		std::ifstream in(state);
		in >> refA >> refB >> refC;
	}
	const bool stage2 = refA >= 0 && refB >= 0 && refC >= 0 && NextTickIsOpen(refA) &&
						NextTickIsOpen(refB) && NextTickIsOpen(refC);

	if (!stage2)
	{
		probe.log("1 回目（用意）。走り出し active fileRef=" + std::to_string(originRef));
		const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch())
									.count();
		const std::string tag = std::to_string(stamp);

		// A: 保存してから矩形を足す
		(void)gSDK->OpenDocumentPath(nullptr, false);
		refA = NextTickActiveRef();
		NextTickSave(probe, "A", (dir / ("a-" + tag + ".vwx")).string());
		const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		probe.log("A: CreateRectangle=" + NextTickFlag(h != nullptr));

		// B: 保存して閉じたファイルを開き直す
		const std::string pB = (dir / ("b-" + tag + ".vwx")).string();
		(void)gSDK->OpenDocumentPath(nullptr, false);
		const Sint32 tmp = NextTickActiveRef();
		NextTickSave(probe, "B", pB);
		if (gSDK->SwitchToOpenFile(tmp) && NextTickActiveRef() == tmp)
			(void)gSDK->CloseDocument();
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(pB.c_str()));
		probe.log("B: OpenDocumentPath(開き直し)=" +
				  NextTickFlag(gSDK->OpenDocumentPath(fileID, false)));
		refB = NextTickActiveRef();

		// C: 保存しただけ（対照）
		(void)gSDK->OpenDocumentPath(nullptr, false);
		refC = NextTickActiveRef();
		NextTickSave(probe, "C", (dir / ("c-" + tag + ".vwx")).string());

		(void)gSDK->SwitchToOpenFile(originRef);
		std::ofstream out(state, std::ios::trunc);
		out << refA << " " << refB << " " << refC << "\n";
		probe.log("用意した: A=" + std::to_string(refA) + " B=" + std::to_string(refB) +
				  " C=" + std::to_string(refC) + "。new_document=false でもう一度走らせる");
		return;
	}

	probe.log("2 回目（閉じる）。A=" + std::to_string(refA) + " B=" + std::to_string(refB) +
			  " C=" + std::to_string(refC));
	NextTickClose(probe, "A 保存後に矩形を足した（前の実行で）", refA);
	NextTickClose(probe, "B 開き直しただけ（前の実行で）", refB);
	NextTickClose(probe, "C 保存しただけ（対照）", refC);

	// 片付け: 残ったものは捨てて閉じる
	for (const Sint32 ref : {refA, refB, refC})
	{
		if (NextTickIsOpen(ref) && gSDK->SwitchToOpenFile(ref) && NextTickActiveRef() == ref)
			(void)gSDK->CloseDocument();
	}
	if (NextTickIsOpen(originRef))
		(void)gSDK->SwitchToOpenFile(originRef);
	std::filesystem::remove_all(dir, ec);
	probe.log("片付けた。件数=" + std::to_string(NextTickCount()));
}
