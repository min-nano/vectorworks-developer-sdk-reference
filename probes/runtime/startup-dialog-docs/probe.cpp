//
//	probes/runtime/startup-dialog-docs/probe.cpp
//
//	[issue #264] **起動時の「開発元不明のプラグイン」のダイアログの最中に、OS タイマーの刻み
//	から図面を扱えるか**を測る。MCP ブリッジ（殻の `SetTimer` / `CFRunLoopTimer` の刻み）から
//	`vw_run_probe` で走らせる前提で、ダイアログが開いていないときにも同じ手順を走らせて比べる。
//
//	手順（どれも結果をログに書いてから次へ進む）:
//	  1. 前面の図面・図面の数・`IsCurrentlyBuildingAnUndoEvent()`・mac はランループのモード・プロセスの窓（ダイアログが
//	     開いているか。Windows は可視のトップレベル窓の題・クラス・有効か）を書く。
//	  2. **図形を足す**——実プラグインと同じ作法で undo イベントを開き（このプローブは
//	     「undo の記録の中で足せるか」が問いなので例外）、矩形を作って名前を付け、
//	     `AddAfterSwapObject` で申告し、`EndUndoEvent` で閉じる。
//	  3. **保存する**——一時ディレクトリの `VwSdkProbes-startup-dialog-docs.vwx` へ
//	     `SaveActiveDocumentPath`。ファイルの大きさを確かめる。
//	  4. **閉じて開く**——`CloseDocument` で閉じ、`OpenDocumentPath(path)` で開き直して、
//	     名前で矩形を引き直す（保存したものが残っているか）。開いた図面はそのまま置く
//	     （ダイアログを閉じた後に残っているかを外から確かめる）。
//
//	新規の空図面で走らせる（ブリッジの既定）。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"

#include <filesystem>
#include <string>
#include <system_error>

#if GS_WIN
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#else
#	include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
	const char* const kSddRectName = "startup-dialog-docs-rect";

	std::string SddYesNo(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string SddUtf8(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string SddSavePath()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		if (ec)
			dir = std::filesystem::path(".");
		return (dir / "VwSdkProbes-startup-dialog-docs.vwx").string();
	}

	std::string SddDocs()
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		std::string out = std::to_string((int)list.GetSize()) + " 枚";
		VectorWorks::Filing::IFileIdentifierPtr active;
		bool saved = false;
		if (gSDK->GetActiveDocument(&active, saved) && active)
		{
			TXString path;
			active->GetFileFullPath(path);
			out += "・前面 \"" + SddUtf8(path) + "\" saved=" + SddYesNo(saved);
		}
		else
			out += "・前面なし";
		return out;
	}

#if GS_WIN
	std::string SddWideToUtf8(const wchar_t* w)
	{
		if (w == nullptr || *w == 0)
			return std::string();
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		if (n <= 1)
			return std::string();
		std::string out((std::size_t)n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
		out.resize((std::size_t)n - 1);
		return out;
	}

	BOOL CALLBACK SddListWindow(HWND h, LPARAM param)
	{
		DWORD pid = 0;
		::GetWindowThreadProcessId(h, &pid);
		if (pid != ::GetCurrentProcessId() || !::IsWindowVisible(h))
			return TRUE;
		wchar_t title[256] = {0};
		wchar_t cls[128] = {0};
		::GetWindowTextW(h, title, 256);
		::GetClassNameW(h, cls, 128);
		auto* probe = reinterpret_cast<vwprobe::Report*>(param);
		probe->log("  窓: \"" + SddWideToUtf8(title) + "\" / " + SddWideToUtf8(cls) +
				   " enabled=" + SddYesNo(::IsWindowEnabled(h) != FALSE) +
				   (::GetWindow(h, GW_OWNER) != nullptr ? " owned" : ""));
		return TRUE;
	}
#endif

	// mac はランループのモード（`NSModalPanelRunLoopMode` ならモーダルの中）。Windows は窓を並べる。
	std::string SddMode()
	{
#if GS_WIN
		return std::string();
#else
		CFStringRef mode = ::CFRunLoopCopyCurrentMode(::CFRunLoopGetMain());
		if (mode == nullptr)
			return " mode=(ランループの外)";
		char buf[128] = {0};
		const bool ok = ::CFStringGetCString(mode, buf, sizeof(buf), kCFStringEncodingUTF8);
		::CFRelease(mode);
		return std::string(" mode=") + (ok ? buf : "?");
#endif
	}

	void SddState(vwprobe::Report& probe, const std::string& when)
	{
		probe.log("[" + when + "] 図面 " + SddDocs() +
				  " undo=" + SddYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()) + SddMode());
	}
} // namespace

VW_PROBE("startup-dialog-docs", "起動時ダイアログ中の図面", "刻みから図形を足し保存し開き直す")
{
	SddState(probe, "始め");
#if GS_WIN
	::EnumWindows(&SddListWindow, reinterpret_cast<LPARAM>(&probe));
#endif

	// 2. 図形を足す（undo イベントの中で）。
	probe.log("2. undo イベントを開いて矩形を足す");
	gSDK->SetUndoMethod(kUndoSwapObjects);
	gSDK->NameUndoEvent("startup-dialog-docs");
	probe.log("  開いた後 undo=" + SddYesNo(gSDK->IsCurrentlyBuildingAnUndoEvent()));
	MCObjectHandle rect = gSDK->CreateRectangle(WorldRect(WorldPt(0, 1000), WorldPt(2000, 0)));
	if (rect == nullptr)
	{
		probe.fail("CreateRectangle が nullptr を返した");
		gSDK->EndUndoEvent();
		return;
	}
	const GSError named = gSDK->SetObjectName(rect, kSddRectName);
	const Boolean added = gSDK->AddAfterSwapObject(rect);
	const Boolean ended = gSDK->EndUndoEvent();
	probe.log("  SetObjectName=" + std::to_string((int)named) + " AddAfterSwapObject=" +
			  SddYesNo(added != 0) + " EndUndoEvent=" + SddYesNo(ended != 0));
	gSDK->MarkCurrentDocChanged();
	SddState(probe, "足した後");

	// 3. 保存する。
	const std::string path = SddSavePath();
	std::error_code ec;
	std::filesystem::remove(path, ec);
	probe.log("3. 保存する: " + path);
	const VectorWorks::Filing::IFileIdentifierPtr fileID(VectorWorks::Filing::IID_FileIdentifier);
	if (!fileID || fileID->Set(TXString(path.c_str())) != kVCOMError_NoError)
	{
		probe.fail("IFileIdentifier を作れなかった");
		return;
	}
	const GSError saveErr = gSDK->SaveActiveDocumentPath(fileID);
	const bool exists = std::filesystem::exists(path, ec);
	const long long size = exists ? (long long)std::filesystem::file_size(path, ec) : -1;
	probe.log("  SaveActiveDocumentPath=" + std::to_string((int)saveErr) +
			  " exists=" + SddYesNo(exists) + " bytes=" + std::to_string(size));
	SddState(probe, "保存した後");
	if (!exists)
	{
		probe.fail("保存したはずのファイルが無い");
		return;
	}

	// 4. 閉じて開き直す。
	probe.log("4. CloseDocument で閉じる");
	(void)gSDK->CloseDocument(); // 戻り値は当てにならない（Findings「Documents」）
	SddState(probe, "閉じた後");
	probe.log("  OpenDocumentPath で開き直す");
	const bool opened = gSDK->OpenDocumentPath(fileID, false);
	SddState(probe, "開いた後");
	MCObjectHandle again = gSDK->GetNamedObject(kSddRectName);
	probe.log("  OpenDocumentPath=" + SddYesNo(opened) +
			  " 名前で引いた矩形=" + std::string(again != nullptr ? "ある" : "無い"));
	if (again == nullptr)
		probe.fail("開き直した図面に矩形が無い");
}
