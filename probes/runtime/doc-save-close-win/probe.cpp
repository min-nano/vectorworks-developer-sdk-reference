//
//	probes/runtime/doc-save-close-win/probe.cpp
//
//	[issue #283] 指定した図面の一覧・保存・「閉じる」が、Windows でも mac（#243）と同じに
//	振る舞うか。ブリッジ（vw_run_probe）から走らせれば OS タイマー（`SetTimer`）の刻みの中:
//
//	  A  GetOpenFilesList のパスの形（無題の名前と N・保存済みの区切り文字・ドライブ文字・
//	     8.3 の短い名前・`/` 区切り・小文字のドライブ・260 文字を超えるパス）
//	  B  無題を新しいパスへ保存（GSError・保存後のパス・fIsInMemoryOnly）。裏の図面を保存して
//	     前面とカレントレイヤを戻せるか
//	  C  保存先が既にある（開いていない／別の図面が開いている）。大文字と小文字だけ変えた保存先
//	  D  DoMenuName("Close", 0)：保存した直後・無題・OpenDocumentPath で開いただけ。確認を
//	     取り消すと戻り図面が残るか。**確認の最中に SetTimer の刻みが入り直すか**
//	  E  閉じたあと、元の前面へ SwitchToOpenFile で戻せるか
//
//	**ダイアログは人を待たない。** 呼び出しの間だけ「見張り」の SetTimer（100 ms）を仕掛け、
//	その刻みが新しく出た窓の文言（タイトル・子の class / ID / 文字）をログへ書き、1 秒見せて
//	からボタンを押す（保存の確認は「キャンセル」だけ。見つからなければ押さずに人を待つ）。
//	見張りが刻んだ回数が、そのまま「呼び出しの最中に WM_TIMER が配られたか」の答えになる。
//	D の 1 件は「駆動」の SetTimer の刻みから Close を呼び、**同じタイマーが入り直すか**を数える
//	（そのためだけにプローブの中で短いメッセージループを回す）。
//
//	Windows 専用（mac は #243 で測り済み）。走り出しの図面には触れない。作業用の図面を自分で
//	開き、最後に CloseDocument で閉じる。
//

#include "Probe.h"

#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#if GS_WIN
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#endif

#if GS_WIN
namespace
{
	using namespace VectorWorks;

	std::string DswFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	long long DswMsSince(std::chrono::steady_clock::time_point t0)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - t0)
			.count();
	}

	std::string DswPathOf(Filing::IFileIdentifier* pFileID)
	{
		if (pFileID == nullptr)
			return "(nullptr)";
		TXString path;
		pFileID->GetFileFullPath(path);
		return static_cast<const char*>(path);
	}

	struct DswDoc
	{
		Sint32 ref = -1;
		bool active = false;
		bool inMemoryOnly = false;
		std::string path;
	};

	std::vector<DswDoc> DswList()
	{
		std::vector<DswDoc> out;
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			DswDoc d;
			d.ref = files[i].fFileRef;
			d.active = files[i].fIsActive;
			d.inMemoryOnly = files[i].fIsInMemoryOnly;
			d.path = DswPathOf(files[i].fpFileID);
			out.push_back(d);
		}
		return out;
	}

	Sint32 DswActiveRef()
	{
		for (const DswDoc& d : DswList())
		{
			if (d.active)
				return d.ref;
		}
		return -1;
	}

	bool DswIsOpen(Sint32 ref)
	{
		for (const DswDoc& d : DswList())
		{
			if (d.ref == ref)
				return true;
		}
		return false;
	}

	std::string DswPathOfRef(Sint32 ref)
	{
		for (const DswDoc& d : DswList())
		{
			if (d.ref == ref)
				return d.path;
		}
		return "(開いていない)";
	}

	void DswSnap(vwprobe::Report& probe, const std::string& tag)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		bool saved = false;
		const bool ok = gSDK->GetActiveDocument(&fileID, saved);
		probe.log(tag + ": GetActiveDocument=" + DswFlag(ok) + " outSaved=" + DswFlag(saved) +
				  " path=" + DswPathOf(fileID));
		const std::vector<DswDoc> docs = DswList();
		probe.log(tag + ":   件数=" + std::to_string(docs.size()));
		for (size_t i = 0; i < docs.size(); ++i)
		{
			probe.log(tag + ":   [" + std::to_string(i) + "] fileRef=" +
					  std::to_string(docs[i].ref) + " active=" + DswFlag(docs[i].active) +
					  " inMemoryOnly=" + DswFlag(docs[i].inMemoryOnly) +
					  " len=" + std::to_string(docs[i].path.size()) + " path=" + docs[i].path);
		}
	}

	std::wstring DswToWide(const std::string& s)
	{
		if (s.empty())
			return std::wstring();
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
		std::wstring out((std::size_t)n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
		out.resize((std::size_t)n - 1);
		return out;
	}

	// 260 文字を超えても見えるように \\?\ を付けて見る（'/' 区切りは \ に直す）
	std::string DswFileState(const std::string& path)
	{
		std::wstring w = DswToWide(path);
		for (wchar_t& c : w)
		{
			if (c == L'/')
				c = L'\\';
		}
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (!::GetFileAttributesExW((L"\\\\?\\" + w).c_str(), GetFileExInfoStandard, &data))
			return "無い";
		return "ある size=" + std::to_string(data.nFileSizeLow);
	}

	Sint32 DswOpenNew(vwprobe::Report& probe, const std::string& tag)
	{
		const bool ok = gSDK->OpenDocumentPath(nullptr, false);
		const Sint32 ref = DswActiveRef();
		probe.log(tag + ": OpenDocumentPath(nullptr)=" + DswFlag(ok) +
				  " fileRef=" + std::to_string(ref) + " path=" + DswPathOfRef(ref));
		return ref;
	}

	void DswRect(vwprobe::Report& probe, const std::string& tag, double x)
	{
		const MCObjectHandle h = gSDK->CreateRectangle(WorldRect(x, 10, x + 10, 0));
		probe.log(tag + ": CreateRectangle=" + DswFlag(h != nullptr));
	}

	std::string DswLayerName(MCObjectHandle layer)
	{
		TXString name;
		if (layer != nullptr)
			gSDK->GetObjectName(layer, name);
		return static_cast<const char*>(name);
	}

	// -----------------------------------------------------------------------
	// 見張り（呼び出しの最中に出た窓を書き、ボタンを押す）と駆動

	enum class DswPress
	{
		Cancel, // 保存の確認。「キャンセル」以外は押さない
		Ok,		// 警告（OK だけのはず）
	};

	struct DswWatch
	{
		vwprobe::Report* probe = nullptr;
		std::string tag;
		DswPress press = DswPress::Cancel;
		std::chrono::steady_clock::time_point t0;
		std::set<HWND> before; // 呼ぶ前からあった窓
		std::set<HWND> logged;
		HWND dialog = nullptr;
		long long seenAt = -1;
		bool pressed = false;
		bool gaveUp = false;
		long long ticks = 0; // 呼び出しの最中に見張りが刻んだ回数
		long long firstTickMs = -1;
		long long lastTickMs = -1;
		// 駆動
		bool driveInCall = false;
		bool driveDone = false;
		long long driveReentries = 0;
		long long driveFirstReentryMs = -1;
		short driveResult = -99;
		long long driveCallMs = -1;
	};

	DswWatch* gDswWatch = nullptr;

	std::string DswWide(const wchar_t* w)
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

	BOOL CALLBACK DswCollectTop(HWND h, LPARAM param)
	{
		DWORD pid = 0;
		::GetWindowThreadProcessId(h, &pid);
		if (pid == ::GetCurrentProcessId() && ::IsWindowVisible(h))
			reinterpret_cast<std::set<HWND>*>(param)->insert(h);
		return TRUE;
	}

	std::set<HWND> DswTopWindows()
	{
		std::set<HWND> out;
		::EnumWindows(&DswCollectTop, reinterpret_cast<LPARAM>(&out));
		return out;
	}

	struct DswChild
	{
		HWND h = nullptr;
		int id = 0;
		std::string cls;
		std::string text;
		bool visible = false;
	};

	BOOL CALLBACK DswCollectChild(HWND h, LPARAM param)
	{
		DswChild c;
		c.h = h;
		c.id = ::GetDlgCtrlID(h);
		wchar_t cls[128] = {0};
		wchar_t text[512] = {0};
		::GetClassNameW(h, cls, 128);
		::GetWindowTextW(h, text, 512);
		c.cls = DswWide(cls);
		c.text = DswWide(text);
		c.visible = ::IsWindowVisible(h) != FALSE;
		reinterpret_cast<std::vector<DswChild>*>(param)->push_back(c);
		return TRUE;
	}

	std::vector<DswChild> DswChildren(HWND h)
	{
		std::vector<DswChild> out;
		::EnumChildWindows(h, &DswCollectChild, reinterpret_cast<LPARAM>(&out));
		return out;
	}

	bool DswHas(const std::string& s, const char* needle)
	{
		return s.find(needle) != std::string::npos;
	}

	void DswLogWindow(DswWatch& w, HWND h)
	{
		wchar_t title[256] = {0};
		wchar_t cls[128] = {0};
		::GetWindowTextW(h, title, 256);
		::GetClassNameW(h, cls, 128);
		w.probe->log(w.tag + ":     [" + std::to_string(DswMsSince(w.t0)) + "ms] 新しい窓 \"" +
					 DswWide(title) + "\" class=" + DswWide(cls) +
					 " enabled=" + DswFlag(::IsWindowEnabled(h) != FALSE) +
					 (::GetWindow(h, GW_OWNER) != nullptr ? " owned" : ""));
		for (const DswChild& c : DswChildren(h))
		{
			if (!c.visible)
				continue;
			w.probe->log(w.tag + ":       子 id=" + std::to_string(c.id) + " class=" + c.cls +
						 " text=\"" + c.text + "\"");
		}
	}

	// 押すボタンを決める。保存の確認では「キャンセル」以外を決して押さない。
	HWND DswPickButton(DswWatch& w, HWND dlg, std::string& why)
	{
		const std::vector<DswChild> kids = DswChildren(dlg);
		for (const DswChild& c : kids)
		{
			if (!c.visible)
				continue;
			const bool isCancel = DswHas(c.text, "キャンセル") || DswHas(c.text, "Cancel");
			const bool isOk = c.text == "OK" || c.text == "&OK" || c.text == "ＯＫ";
			if (w.press == DswPress::Cancel && isCancel)
			{
				why = "文字「" + c.text + "」";
				return c.h;
			}
			if (w.press == DswPress::Ok && isOk)
			{
				why = "文字「" + c.text + "」";
				return c.h;
			}
		}
		// 文字が読めない（VW の独自の部品）なら ID で。キャンセル＝IDCANCEL(2)、OK＝IDOK(1)
		const int wantId = w.press == DswPress::Cancel ? IDCANCEL : IDOK;
		for (const DswChild& c : kids)
		{
			if (c.visible && c.id == wantId)
			{
				why = "ID " + std::to_string(wantId) + "（" + c.cls + " \"" + c.text + "\"）";
				return c.h;
			}
		}
		return nullptr;
	}

	void DswWatchTick()
	{
		DswWatch* w = gDswWatch;
		if (w == nullptr)
			return;
		const long long now = DswMsSince(w->t0);
		++w->ticks;
		if (w->firstTickMs < 0)
			w->firstTickMs = now;
		w->lastTickMs = now;
		if (w->pressed || w->gaveUp)
			return;
		for (HWND h : DswTopWindows())
		{
			if (w->before.count(h) != 0 || w->logged.count(h) != 0)
				continue;
			w->logged.insert(h);
			DswLogWindow(*w, h);
			if (w->dialog == nullptr)
			{
				w->dialog = h;
				w->seenAt = now;
			}
		}
		if (w->dialog == nullptr || now - w->seenAt < 1000)
			return;
		if (!::IsWindow(w->dialog))
		{
			w->dialog = nullptr;
			return;
		}
		std::string why;
		HWND button = DswPickButton(*w, w->dialog, why);
		if (button != nullptr)
		{
			w->pressed = true;
			w->probe->log(w->tag + ":     [" + std::to_string(now) + "ms] 見張りが押す: " + why);
			::SendMessageW(button, BM_CLICK, 0, 0);
			return;
		}
		if (w->press == DswPress::Ok)
		{
			w->pressed = true;
			w->probe->log(w->tag + ":     [" + std::to_string(now) +
						  "ms] ボタンが見つからない → WM_COMMAND(IDOK) を窓へ送る");
			::PostMessageW(w->dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
			return;
		}
		w->gaveUp = true;
		w->probe->log(w->tag + ":     [" + std::to_string(now) +
					  "ms] 「キャンセル」が見つからない → 押さない（人が押すのを待つ）");
	}

	VOID CALLBACK DswWatchTimer(HWND, UINT, UINT_PTR, DWORD)
	{
		DswWatchTick();
	}

	void DswWatchSummary(vwprobe::Report& probe, const DswWatch& w, long long callMs)
	{
		probe.log(w.tag + ":     見張り: 呼び出しの最中に刻んだ回数=" + std::to_string(w.ticks) +
				  "（初回 " + std::to_string(w.firstTickMs) + "ms・最後 " +
				  std::to_string(w.lastTickMs) + "ms・呼び出し " + std::to_string(callMs) +
				  "ms） 新しい窓=" + std::to_string(w.logged.size()) +
				  " 押した=" + DswFlag(w.pressed));
	}

	// 見張りを仕掛けて fn を呼ぶ。
	template <class Fn>
	long long DswWatched(vwprobe::Report& probe, const std::string& tag, DswPress press, Fn fn)
	{
		DswWatch w;
		w.probe = &probe;
		w.tag = tag;
		w.press = press;
		w.before = DswTopWindows();
		w.t0 = std::chrono::steady_clock::now();
		gDswWatch = &w;
		const UINT_PTR timer = ::SetTimer(nullptr, 0, 100, &DswWatchTimer);
		fn();
		const long long ms = DswMsSince(w.t0);
		if (timer != 0)
			::KillTimer(nullptr, timer);
		gDswWatch = nullptr;
		DswWatchSummary(probe, w, ms);
		return ms;
	}

	GSError DswSave(vwprobe::Report& probe, const std::string& tag, const std::string& path,
					DswPress press = DswPress::Ok)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		if (!fileID || fileID->Set(TXString(path.c_str())) != kVCOMError_NoError)
		{
			probe.log(tag + ": IFileIdentifier を作れなかった");
			return -999;
		}
		probe.log(tag + ": SaveActiveDocumentPath を呼ぶ（保存先 " + DswFileState(path) +
				  " 渡したパス=" + path + " IFileIdentifier の読み戻し=" + DswPathOf(fileID) +
				  "）");
		GSError err = -998;
		const long long ms =
			DswWatched(probe, tag, press, [&] { err = gSDK->SaveActiveDocumentPath(fileID); });
		probe.log(tag + ": SaveActiveDocumentPath GSError=" + std::to_string(err) + " (" +
				  std::to_string(ms) + "ms) 保存先は " + DswFileState(path) +
				  " 前面のパス=" + DswPathOfRef(DswActiveRef()));
		return err;
	}

	void DswClose(vwprobe::Report& probe, const std::string& tag, Sint32 expectRef)
	{
		const Sint32 activeRef = DswActiveRef();
		const size_t before = DswList().size();
		probe.log(tag +
				  ": DoMenuName(\"Close\", 0) を呼ぶ（active fileRef=" + std::to_string(activeRef) +
				  " 期待=" + std::to_string(expectRef) + " 件数=" + std::to_string(before) + "）");
		short r = -99;
		const long long ms = DswWatched(probe, tag, DswPress::Cancel,
										[&] { r = gSDK->DoMenuName(TXString("Close"), 0); });
		probe.log(tag + ": DoMenuName(\"Close\", 0)=" + std::to_string(r) + " (" +
				  std::to_string(ms) + "ms) 件数 " + std::to_string(before) + " → " +
				  std::to_string(DswList().size()) +
				  " 対象はまだ開いている=" + DswFlag(DswIsOpen(expectRef)) +
				  " 閉じた後の active fileRef=" + std::to_string(DswActiveRef()));
	}

	// 駆動の SetTimer の刻みから Close を呼ぶ。プローブの中でメッセージループを回して待つ。
	VOID CALLBACK DswDriveTimer(HWND, UINT, UINT_PTR, DWORD)
	{
		DswWatch* w = gDswWatch;
		if (w == nullptr || w->driveDone)
			return;
		if (w->driveInCall)
		{
			++w->driveReentries;
			if (w->driveFirstReentryMs < 0)
				w->driveFirstReentryMs = DswMsSince(w->t0);
			return;
		}
		w->driveInCall = true;
		const auto t0 = std::chrono::steady_clock::now();
		w->driveResult = gSDK->DoMenuName(TXString("Close"), 0);
		w->driveCallMs = DswMsSince(t0);
		w->driveInCall = false;
		w->driveDone = true;
	}

	void DswCloseFromDrive(vwprobe::Report& probe, const std::string& tag, Sint32 expectRef)
	{
		const size_t before = DswList().size();
		probe.log(tag + ": 駆動の刻みから DoMenuName(\"Close\", 0) を呼ぶ（active fileRef=" +
				  std::to_string(DswActiveRef()) + " 件数=" + std::to_string(before) + "）");
		DswWatch w;
		w.probe = &probe;
		w.tag = tag;
		w.press = DswPress::Cancel;
		w.before = DswTopWindows();
		w.t0 = std::chrono::steady_clock::now();
		gDswWatch = &w;
		const UINT_PTR watch = ::SetTimer(nullptr, 0, 100, &DswWatchTimer);
		const UINT_PTR drive = ::SetTimer(nullptr, 0, 50, &DswDriveTimer);
		bool quit = false;
		while (!w.driveDone && !quit && DswMsSince(w.t0) < 120000)
		{
			::MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					::PostQuitMessage((int)msg.wParam);
					quit = true;
					break;
				}
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}
		}
		const long long ms = DswMsSince(w.t0);
		if (drive != 0)
			::KillTimer(nullptr, drive);
		if (watch != 0)
			::KillTimer(nullptr, watch);
		gDswWatch = nullptr;
		DswWatchSummary(probe, w, ms);
		probe.log(tag + ": 駆動: 終えた=" + DswFlag(w.driveDone) + " WM_QUIT=" + DswFlag(quit) +
				  " DoMenuName=" + std::to_string(w.driveResult) +
				  " 呼び出しの所要=" + std::to_string(w.driveCallMs) +
				  "ms **駆動の入り直し=" + std::to_string(w.driveReentries) + "**（初回 " +
				  std::to_string(w.driveFirstReentryMs) + "ms）");
		probe.log(tag + ": 件数 " + std::to_string(before) + " → " +
				  std::to_string(DswList().size()) +
				  " 対象はまだ開いている=" + DswFlag(DswIsOpen(expectRef)) +
				  " active fileRef=" + std::to_string(DswActiveRef()));
	}

	bool DswOpenPath(vwprobe::Report& probe, const std::string& tag, const std::string& path,
					 Sint32& outRef)
	{
		Filing::IFileIdentifierPtr fileID(Filing::IID_FileIdentifier);
		fileID->Set(TXString(path.c_str()));
		bool opened = false;
		const long long ms = DswWatched(probe, tag, DswPress::Ok,
										[&] { opened = gSDK->OpenDocumentPath(fileID, false); });
		outRef = DswActiveRef();
		probe.log(tag + ": OpenDocumentPath(" + path + ")=" + DswFlag(opened) + " (" +
				  std::to_string(ms) + "ms) active fileRef=" + std::to_string(outRef) +
				  " path=" + DswPathOfRef(outRef));
		return opened;
	}

	std::string DswUpperName(const std::string& path)
	{
		// ファイル名の部分だけ大文字にする（ASCII）
		std::string out = path;
		const size_t slash = out.find_last_of("\\/");
		for (size_t i = (slash == std::string::npos ? 0 : slash + 1); i < out.size(); ++i)
		{
			if (out[i] >= 'a' && out[i] <= 'z')
				out[i] = (char)(out[i] - 'a' + 'A');
		}
		return out;
	}

	std::string DswReplaceAll(std::string s, char from, char to)
	{
		for (char& c : s)
		{
			if (c == from)
				c = to;
		}
		return s;
	}
} // namespace
#endif

VW_PROBE("doc-save-close-win", "図面の保存と閉じる（Windows）", "Windows で一覧・保存・閉じる")
{
#if !GS_WIN
	probe.fail("Windows 専用（mac は #243 で測り済み）");
#else
	const Sint32 originRef = DswActiveRef();
	const MCObjectHandle originLayer = gSDK->GetCurrentLayer();
	probe.log("走り出し: active fileRef=" + std::to_string(originRef) +
			  " カレントレイヤ=" + DswLayerName(originLayer));
	DswSnap(probe, "S0 走り出し");
	{
		DWORD v = 0;
		DWORD sz = sizeof(v);
		const LSTATUS s =
			::RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
						   L"LongPathsEnabled", RRF_RT_REG_DWORD, nullptr, &v, &sz);
		probe.log("S0: LongPathsEnabled=" +
				  (s == ERROR_SUCCESS ? std::to_string(v) : "(読めない)"));
		wchar_t exe[MAX_PATH] = {0};
		::GetModuleFileNameW(nullptr, exe, MAX_PATH);
		wchar_t cwd[MAX_PATH] = {0};
		::GetCurrentDirectoryW(MAX_PATH, cwd);
		probe.log("S0: 実行ファイル=" + DswWide(exe) + " カレントディレクトリ=" + DswWide(cwd));
	}

	// 作業フォルダ。GetTempPathW は 8.3 の短い名前を返しうるので、長い名前に直したものも持つ
	wchar_t tmpW[MAX_PATH] = {0};
	::GetTempPathW(MAX_PATH, tmpW);
	wchar_t tmpLongW[MAX_PATH] = {0};
	::GetLongPathNameW(tmpW, tmpLongW, MAX_PATH);
	const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::system_clock::now().time_since_epoch())
								.count();
	const std::string dirName = "VwSdkProbes-dsw-" + std::to_string(stamp);
	std::string dir = DswWide(tmpLongW);
	if (dir.empty())
		dir = DswWide(tmpW);
	if (!dir.empty() && dir.back() != '\\')
		dir += "\\";
	dir += dirName;
	::CreateDirectoryW(DswToWide(dir).c_str(), nullptr);
	wchar_t shortW[MAX_PATH] = {0};
	::GetShortPathNameW(DswToWide(dir).c_str(), shortW, MAX_PATH);
	const std::string dirShort = DswWide(shortW);
	probe.log("作業フォルダ: GetTempPathW=" + DswWide(tmpW) + " 長い名前=" + dir +
			  " 短い名前=" + dirShort);
	std::vector<Sint32> owned;

	// ===== A 無題の図面の名前 =====
	const Sint32 u1 = DswOpenNew(probe, "A-1");
	const Sint32 u2 = DswOpenNew(probe, "A-2");
	owned.push_back(u1);
	owned.push_back(u2);
	DswSnap(probe, "A 無題を 2 枚開いた");
	if (u1 < 0 || u2 < 0 || u1 == u2 || u1 == originRef)
	{
		probe.fail("作業用の図面を開けなかった");
		return;
	}
	probe.log("A: 無題の名前はファイルとして " + DswFileState(DswPathOfRef(u1)));

	// ===== B 無題を新しいパスへ（＋A の保存済みのパスの形） =====
	probe.log("B-1: SwitchToOpenFile(u1)=" + DswFlag(gSDK->SwitchToOpenFile(u1)));
	DswRect(probe, "B-1", 0);
	const std::string p1 = dir + "\\q1-untitled.vwx";
	DswSave(probe, "B-1 無題を新しいパスへ", p1);
	DswSnap(probe, "B-1 の後");

	// 保存の形を変えて渡す: 8.3 の短い名前・'/' 区切り・小文字のドライブ
	const Sint32 sShort = DswOpenNew(probe, "A-3");
	owned.push_back(sShort);
	if (!dirShort.empty())
		DswSave(probe, "A-3 8.3 の短い名前で渡す", dirShort + "\\q-short.vwx");
	const Sint32 sSlash = DswOpenNew(probe, "A-4");
	owned.push_back(sSlash);
	DswSave(probe, "A-4 '/' 区切りで渡す", DswReplaceAll(dir + "\\q-slash.vwx", '\\', '/'));
	const Sint32 sLower = DswOpenNew(probe, "A-5");
	owned.push_back(sLower);
	{
		std::string lower = dir + "\\Q-Lower.vwx";
		if (lower.size() > 1 && lower[1] == ':' && lower[0] >= 'A' && lower[0] <= 'Z')
			lower[0] = (char)(lower[0] - 'A' + 'a');
		DswSave(probe, "A-5 小文字のドライブ・大文字混じりの名前で渡す", lower);
	}
	// 260 文字を超えるパス
	{
		std::string longDir = dir;
		for (int i = 0; i < 3 && longDir.size() < 280; ++i)
		{
			longDir += "\\" + std::string(90, (char)('a' + i));
			const BOOL ok = ::CreateDirectoryW(DswToWide("\\\\?\\" + longDir).c_str(), nullptr);
			if (!ok && ::GetLastError() != ERROR_ALREADY_EXISTS)
				probe.log("A-6: CreateDirectoryW 失敗 GetLastError=" +
						  std::to_string(::GetLastError()));
		}
		const std::string pLong = longDir + "\\q-long.vwx";
		probe.log("A-6: 長いパスの長さ=" + std::to_string(pLong.size()));
		const Sint32 sLong = DswOpenNew(probe, "A-6");
		owned.push_back(sLong);
		DswSave(probe, "A-6 260 文字を超えるパス", pLong);
		const DWORD attr = ::GetFileAttributesW(DswToWide("\\\\?\\" + pLong).c_str());
		probe.log("A-6: \\\\?\\ 経由で見たファイル=" + DswFlag(attr != INVALID_FILE_ATTRIBUTES));
	}
	DswSnap(probe, "A 保存済みのパスの形");

	// ===== B-2 裏の図面を保存して前面とカレントレイヤを戻す =====
	probe.log("B-2: SwitchToOpenFile(origin)=" + DswFlag(gSDK->SwitchToOpenFile(originRef)));
	probe.log("B-2: SwitchToOpenFile(u2)=" + DswFlag(gSDK->SwitchToOpenFile(u2)));
	DswRect(probe, "B-2", 40);
	const std::string p3 = dir + "\\q3-background.vwx";
	DswSave(probe, "B-2 裏の図面を保存", p3);
	probe.log("B-2: SwitchToOpenFile(origin)=" + DswFlag(gSDK->SwitchToOpenFile(originRef)));
	{
		const MCObjectHandle layer = gSDK->GetCurrentLayer();
		probe.log("B-2: 戻した後 active fileRef=" + std::to_string(DswActiveRef()) + "（走り出し " +
				  std::to_string(originRef) + "） カレントレイヤ同じハンドル=" +
				  DswFlag(layer == originLayer) + " 名前=" + DswLayerName(layer));
	}

	// ===== D 閉じる（確認が出ない局面）と E 前面を戻す =====
	probe.log("D-1: SwitchToOpenFile(u2)=" + DswFlag(gSDK->SwitchToOpenFile(u2)));
	DswClose(probe, "D-1 裏から呼び出して保存した直後", u2);
	probe.log("E-1: SwitchToOpenFile(origin)=" + DswFlag(gSDK->SwitchToOpenFile(originRef)));
	{
		const MCObjectHandle layer = gSDK->GetCurrentLayer();
		probe.log("E-1: 閉じた後に戻した active fileRef=" + std::to_string(DswActiveRef()) +
				  " カレントレイヤ同じハンドル=" + DswFlag(layer == originLayer));
	}
	DswSnap(probe, "E-1 の後");

	// ===== C 保存先が既にある =====
	// u1 は p1 を開いている。sSlash / sLower も開いている。p3 は閉じた（ディスクにだけある）
	const Sint32 e1 = DswOpenNew(probe, "C-1");
	owned.push_back(e1);
	DswRect(probe, "C-1", 60);
	DswSave(probe, "C-1 開いていない既存ファイル", p3);
	DswSave(probe, "C-2 開いていない既存ファイル（名前を大文字に）", DswUpperName(p3));
	DswSave(probe, "C-3 別の図面が開いているファイル", p1);
	DswSave(probe, "C-4 別の図面が開いているファイル（名前を大文字に）", DswUpperName(p1));
	DswSnap(probe, "C-4 の後");
	// 自分のパスを大文字と小文字だけ変えて渡す（u1 は p1 を開いている）
	probe.log("C-5: SwitchToOpenFile(u1)=" + DswFlag(gSDK->SwitchToOpenFile(u1)));
	DswSave(probe, "C-5 自分のパス（名前を大文字に）", DswUpperName(p1));
	probe.log("C-5: u1 のパス=" + DswPathOfRef(u1));
	DswSave(probe, "C-6 自分のパス（そのまま）", p1);
	probe.log("C-6: u1 のパス=" + DswPathOfRef(u1));

	// ===== D 閉じる =====
	// D-2 新しいパスへ保存した直後
	DswClose(probe, "D-2 自分のパスへ保存した直後", u1);
	// D-3 無題・無変更（確認 → 見張りがキャンセル）
	const Sint32 d3 = DswOpenNew(probe, "D-3");
	owned.push_back(d3);
	DswClose(probe, "D-3 無題・無変更", d3);
	// D-4 OpenDocumentPath で開いただけ
	Sint32 d4 = -1;
	if (DswOpenPath(probe, "D-4 開く", p3, d4) && d4 != originRef)
	{
		owned.push_back(d4);
		DswClose(probe, "D-4 OpenDocumentPath で開いただけ", d4);
		// D-5 自分のパスへ上書き保存してから
		if (DswIsOpen(d4) && gSDK->SwitchToOpenFile(d4))
		{
			DswSave(probe, "D-5 上書き保存", p3);
			DswClose(probe, "D-5 開き直して上書き保存した直後", d4);
		}
	}
	// D-6 駆動の刻みから（無題・無変更 → 確認 → キャンセル）。同じタイマーが入り直すか
	if (DswIsOpen(d3) && gSDK->SwitchToOpenFile(d3))
		DswCloseFromDrive(probe, "D-6 駆動の刻みから・無題", d3);
	// E-2 前面（d3）は残っているはず。元の前面へ戻す
	probe.log("E-2: SwitchToOpenFile(origin)=" + DswFlag(gSDK->SwitchToOpenFile(originRef)));
	{
		const MCObjectHandle layer = gSDK->GetCurrentLayer();
		probe.log("E-2: active fileRef=" + std::to_string(DswActiveRef()) +
				  " カレントレイヤ同じハンドル=" + DswFlag(layer == originLayer));
	}
	// E-3 前面の図面を閉じた後、何が前へ出るか → 元へ戻す
	const Sint32 e3 = DswOpenNew(probe, "E-3");
	owned.push_back(e3);
	DswSave(probe, "E-3 保存", dir + "\\e3.vwx");
	DswClose(probe, "E-3 前面を閉じる", e3);
	probe.log("E-3: SwitchToOpenFile(origin)=" + DswFlag(gSDK->SwitchToOpenFile(originRef)) +
			  " active fileRef=" + std::to_string(DswActiveRef()) +
			  " カレントレイヤ同じハンドル=" + DswFlag(gSDK->GetCurrentLayer() == originLayer));

	// ===== 片付け =====
	for (const Sint32 ref : owned)
	{
		if (ref == originRef || !DswIsOpen(ref))
			continue;
		if (gSDK->SwitchToOpenFile(ref) && DswActiveRef() == ref)
			(void)gSDK->CloseDocument(); // 戻り値は当てにならない（Findings「Documents」）
	}
	(void)gSDK->SwitchToOpenFile(originRef);
	DswSnap(probe, "Z 片付けた後");
	std::error_code ec;
	std::filesystem::remove_all(std::filesystem::path(L"\\\\?\\" + DswToWide(dir)), ec);
	probe.log("Z: 作業フォルダを消した=" + DswFlag(!ec) + (ec ? " " + ec.message() : ""));
#endif
}
