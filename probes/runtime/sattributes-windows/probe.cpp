//
//	probes/runtime/sattributes-windows/probe.cpp
//
//	[issue #93] `SAttributes` の 11 旗と `SetAttributes` が、**Windows では macOS と
//	違う答えを返すか**を Windows 実機で確かめる。
//
//	なぜ確かめるか（issue #91 / PR #92 の範囲外として切り出したもの）:
//	macOS では #91 で測り切ってある（`Findings/File and Folder Identifiers.md`）。
//
//	  * `IFolderIdentifier::SetAttributes` は `6` = `kVCOMError_NotImplemented`。
//	  * `IFileIdentifier::SetAttributes` は成功を返し、POSIX の権限ビットだけを書く。
//	  * `fbHidden` / `fbSystem` / `fbTemporary` / `fbEncrypted` / `fbArchive` /
//	    `fbDirectory` / `fbCanBrowse` は読んでも常に no、書いても捨てられる。
//
//	ただし `fbHidden` / `fbSystem` / `fbArchive` / `fbEncrypted` / `fbTemporary` は
//	**Windows のファイル属性（`FILE_ATTRIBUTE_*`）そのものの名前**で、macOS では
//	対応物が無い（または一般ユーザーでは作れない）だけかもしれない。Windows なら
//	素直に埋まる見込みがある【推定】。ヘッダ側に答えが無いことは #93 / PR #100 で
//	潰してある（Windows 版 SDK と macOS 版 SDK は同一）。**実機で測るしかない。**
//
//	ここで確かめること（PR #92 の `sattributes-write` を雛形に、OS 側の節を
//	Windows の API に差し替えたもの。**節の番号と並びは #92 と揃えてある**——
//	macOS のログと 1 行ずつ見比べられるように）:
//
//	  1) / 2) `SetAttributes` の往復。11 旗を 1 つずつ反転して書き、読み戻す
//	     （フォルダ・ファイル両方）。**macOS と違ってフォルダ側も実装されているか**が
//	     まず知りたい。書く前後で `GetFileAttributesW` を比べ、**ディスクまで届いたか**
//	     も旗ごとに出す（「読み戻しに出ない」と「書かれていない」を分ける）。
//	  3) 読み取り専用にしたファイル／フォルダへ、本当に書けなくなるか。
//	  4) 実在しない対象への `SetAttributes`。
//	  5) 対照: `SetAttributesTimeDateReference`。
//	  6) **「立つはずの標本」を OS 側で作る。** `SetFileAttributesW` で
//	     `FILE_ATTRIBUTE_READONLY` / `_HIDDEN` / `_SYSTEM` / `_ARCHIVE` / `_TEMPORARY`
//	     を 1 つずつ立て、`GetAttributes` のどの旗が動くかを見る（ファイル・フォルダ）。
//	     6b) `fbArchive` は**ふつうに書き込んだだけで立つ**はずなので、落としてから
//	         1 バイト追記して確かめる（特権は要らない）。
//	     6c) `fbCanExecute` は拡張子で決まるのかを `.exe` / `.bat` / `.cmd` などで見る。
//	     6d) `fbEncrypted` は `EncryptFileW`（EFS）で立てた標本で測る。**EFS が使えない
//	         エディション／ファイルシステムなら、その旨を出して飛ばす**（「立たなかった」
//	         と読み違えないため）。
//	  7) **システム領域の標本**（`C:\Windows\` 配下・`pagefile.sys` など）。読むだけ。
//
//	**利用者の操作は要らない**——ダイアログは 1 つも出ない。図面には触らない。
//	ディスクへ書くのは一時フォルダ 1 つ（とその中のファイル）だけで、測り終えたら消す。
//	読むだけの標本（`C:\Windows\` など）には一切書き込まない。
//
//	**Windows 専用。** macOS で走らせても何も測らない（macOS は #91 で測り終えている）。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"
#include "Interfaces/VectorWorks/Filing/IFolderIdentifier.h"

#include <string>

#if GS_WIN
#	include <cstdio>
#	pragma comment(lib, "advapi32.lib") // EncryptFileW / DecryptFileW / FileEncryptionStatusW
#endif

#if GS_WIN
namespace
{
	using namespace VectorWorks::Filing;

	// -----------------------------------------------------------------------
	// 文字列。**パスは wchar_t で持つ**（Windows の API は W 版で叩く。日本語の
	// ユーザー名を含む一時フォルダで A 版を使うと化けるため）。ログは UTF-8。
	std::string Utf8OfWide(const std::wstring& w)
	{
		if (w.empty())
			return std::string();
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0,
											nullptr, nullptr);
		std::string out(n > 0 ? (size_t)n : 0, '\0');
		if (n > 0)
			::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, nullptr,
								  nullptr);
		return out;
	}

	std::string Utf8Of(const TXString& s)
	{
		return s.GetStdString();
	}

	std::wstring WideOf(const TXString& s)
	{
		return s.GetStdWString();
	}

	std::string ErrWord(VCOMError err)
	{
		return std::to_string((long)err) + (VCOM_SUCCEEDED(err) ? "（成功）" : "（失敗）");
	}

	std::string YesNo(bool value)
	{
		return value ? "yes" : "no";
	}

	// GetLastError の値を「番号＋OS の説明文」で出す。
	std::string WinErrWord(DWORD code)
	{
		wchar_t* text = nullptr;
		const DWORD n =
			::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
								 FORMAT_MESSAGE_IGNORE_INSERTS,
							 nullptr, code, 0, (LPWSTR)&text, 0, nullptr);
		std::wstring message = (n > 0 && text != nullptr) ? std::wstring(text, n) : std::wstring();
		if (text != nullptr)
			::LocalFree(text);
		while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' ||
									message.back() == L' ' || message.back() == L'.'))
			message.pop_back();
		return "err=" + std::to_string((unsigned long)code) +
			   (message.empty() ? std::string() : " " + Utf8OfWide(message));
	}

	// HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion の文字列値を 1 つ読む。
	std::string RegistryWord(const wchar_t* name)
	{
		wchar_t buffer[256] = {};
		DWORD size = sizeof(buffer);
		const LSTATUS rc =
			::RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
						   name, RRF_RT_REG_SZ, nullptr, buffer, &size);
		return rc == ERROR_SUCCESS ? Utf8OfWide(buffer) : std::string("?");
	}

	std::string BoolCallWord(BOOL ok, DWORD lastError)
	{
		return ok ? std::string("成功") : std::string("失敗 ") + WinErrWord(lastError);
	}

	// -----------------------------------------------------------------------
	// SAttributes の 11 項目を「名前つきで 1 つずつ触れる」ようにした表
	// （#92 と同じ並び。**項目を 1 つも落とさない**ことがこの調査の肝なので 1 か所に持つ）。
	struct SFlagRef
	{
		const char* fName;
		bool SAttributes::*fMember;
	};

	const SFlagRef kAttributeFlagTable[] = {
		{"fbReadOnly", &SAttributes::fbReadOnly},	{"fbHidden", &SAttributes::fbHidden},
		{"fbSystem", &SAttributes::fbSystem},		{"fbTemporary", &SAttributes::fbTemporary},
		{"fbEncrypted", &SAttributes::fbEncrypted}, {"fbArchive", &SAttributes::fbArchive},
		{"fbDirectory", &SAttributes::fbDirectory}, {"fbCanRead", &SAttributes::fbCanRead},
		{"fbCanWrite", &SAttributes::fbCanWrite},	{"fbCanExecute", &SAttributes::fbCanExecute},
		{"fbCanBrowse", &SAttributes::fbCanBrowse},
	};

	const size_t kAttributeFlagCount = sizeof(kAttributeFlagTable) / sizeof(kAttributeFlagTable[0]);

	std::string AttrWord(const SAttributes& a)
	{
		std::string out;
		for (size_t i = 0; i < kAttributeFlagCount; ++i)
		{
			if (!out.empty())
				out += " ";
			out += std::string(kAttributeFlagTable[i].fName) + "=" +
				   YesNo(a.*kAttributeFlagTable[i].fMember);
		}
		return out;
	}

	// 2 つの SAttributes の差を「動いた旗の名前＝後の値」で並べる。
	// **差が無いことを「（差なし）」と明示する**——空行だと「測っていない」と
	// 見分けが付かないため。
	std::string DiffWord(const SAttributes& before, const SAttributes& after)
	{
		std::string out;
		for (size_t i = 0; i < kAttributeFlagCount; ++i)
		{
			bool SAttributes::*member = kAttributeFlagTable[i].fMember;
			if (before.*member != after.*member)
			{
				if (!out.empty())
					out += " ";
				out += std::string(kAttributeFlagTable[i].fName) + "=" + YesNo(after.*member);
			}
		}
		return out.empty() ? std::string("（差なし）") : out;
	}

	// -----------------------------------------------------------------------
	// **ディスク側の見え方。** Windows のファイル属性を GetFileAttributesW で直接見る
	// （macOS 版の stat に当たる）。値は 16 進と名前の両方で出す。マクロの有無が
	// SDK のバージョンで揺れないよう、ビットは数値で持つ。
	struct SWinAttrName
	{
		DWORD fBit;
		const char* fName;
	};

	const SWinAttrName kWinAttrNames[] = {
		{0x00000001, "READONLY"},
		{0x00000002, "HIDDEN"},
		{0x00000004, "SYSTEM"},
		{0x00000010, "DIRECTORY"},
		{0x00000020, "ARCHIVE"},
		{0x00000080, "NORMAL"},
		{0x00000100, "TEMPORARY"},
		{0x00000200, "SPARSE_FILE"},
		{0x00000400, "REPARSE_POINT"},
		{0x00000800, "COMPRESSED"},
		{0x00001000, "OFFLINE"},
		{0x00002000, "NOT_CONTENT_INDEXED"},
		{0x00004000, "ENCRYPTED"},
		{0x00008000, "INTEGRITY_STREAM"},
		{0x00020000, "NO_SCRUB_DATA"},
		{0x00040000, "RECALL_ON_OPEN"},
		{0x00080000, "PINNED"},
		{0x00100000, "UNPINNED"},
		{0x00400000, "RECALL_ON_DATA_ACCESS"},
	};

	std::string WinAttrWord(const std::wstring& path)
	{
		if (path.empty())
			return std::string();
		const DWORD attr = ::GetFileAttributesW(path.c_str());
		if (attr == INVALID_FILE_ATTRIBUTES)
			return "GetFileAttributesW 失敗 " + WinErrWord(::GetLastError());

		char hex[16] = {};
		std::snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)attr);
		std::string names;
		DWORD known = 0;
		for (size_t i = 0; i < sizeof(kWinAttrNames) / sizeof(kWinAttrNames[0]); ++i)
		{
			known |= kWinAttrNames[i].fBit;
			if ((attr & kWinAttrNames[i].fBit) != 0)
			{
				if (!names.empty())
					names += "|";
				names += kWinAttrNames[i].fName;
			}
		}
		if ((attr & ~known) != 0)
		{
			char rest[24] = {};
			std::snprintf(rest, sizeof(rest), "0x%08lx", (unsigned long)(attr & ~known));
			names += (names.empty() ? std::string() : std::string("|")) + "他" + rest;
		}
		return std::string("attr=") + hex + "[" + (names.empty() ? std::string("-") : names) + "]";
	}

	// 書く前後でディスク側が動いたかを 1 行書く。**動かなかったことも書く**
	// ——「無視された」と「書かれたが読み戻しに出ない」を分けるのがこの行の役目。
	void LogDiskChange(::vwprobe::Report& probe, const std::wstring& path,
					   const std::string& before)
	{
		if (path.empty() || before.empty())
			return;
		const std::string after = WinAttrWord(path);
		probe.log("      ディスク: 前=" + before + " 後=" + after + " → " +
				  (before == after ? "変わらない" : "変わった"));
	}

	// **本当に書けなくなったか。** 属性が変わっていても、実際に拒まれるかは別の話なので、
	// 追記を 1 バイト試して確かめる。拒まれた理由（GetLastError）まで出す。
	std::string TryAppendWord(const std::wstring& path)
	{
		HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA,
								 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
								 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return "拒まれた（開けない " + WinErrWord(::GetLastError()) + "）";
		DWORD written = 0;
		const BOOL ok = ::WriteFile(h, "x", 1, &written, nullptr);
		const DWORD err = ::GetLastError();
		::CloseHandle(h);
		return (ok && written == 1) ? std::string("できた")
									: "拒まれた（書けない " + WinErrWord(err) + "）";
	}

	// 素の Win32 でファイルを作る（SDK の IFileIdentifier には「作る」口が無い）。
	bool WriteNewFile(const std::wstring& path, const char* content, std::string& why)
	{
		HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
								 FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			why = "CreateFileW 失敗 " + WinErrWord(::GetLastError());
			return false;
		}
		DWORD written = 0;
		const DWORD size = (DWORD)std::char_traits<char>::length(content);
		const BOOL ok = ::WriteFile(h, content, size, &written, nullptr);
		const DWORD err = ::GetLastError();
		::CloseHandle(h);
		if (!ok || written != size)
		{
			why = "WriteFile 失敗 " + WinErrWord(err);
			return false;
		}
		return true;
	}

	// 後片付けの前に属性を落とす（READONLY / SYSTEM が立ったままだと消せない）。
	void ClearAttributes(const std::wstring& path)
	{
		::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
	}

	// フォルダのパスは末尾に \ を付けて持つ（SDK の GetFullPath の形に揃えるため。
	// 付いていなければ足す）。
	std::wstring WithTrailingSlash(std::wstring path)
	{
		if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
			path += L'\\';
		return path;
	}

	// GetFileAttributesW などはフォルダの末尾の \ があっても通るが、念のため外した形も持つ。
	std::wstring WithoutTrailingSlash(std::wstring path)
	{
		while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
			path.pop_back();
		return path;
	}

	// 数え上げ。**結論は「何回中何回」で言う**ので、ログを目で数えなくて済むように持つ。
	struct Tally
	{
		long setCalls = 0; // SetAttributes を呼んだ回数（旗 1 つの反転 1 回を 1 と数える）
		long setSucceeded = 0;	  // そのうち VCOM_SUCCEEDED が返った回数
		long readBackApplied = 0; // そのうち**読み戻しに反映されていた**回数
		long diskChanged = 0;	  // そのうち**ディスク側の属性が動いた**回数
		std::string appliedNames; // 反映された旗（「対象:旗」。空なら 1 つも無い）
		std::string diskNames; // ディスクまで届いた旗（「対象:旗」）
		long osFlagCases = 0;  // OS 側で属性を立てて測った標本の数
		long osFlagExpected = 0; // そのうち、狙った SAttributes の旗が立った標本の数
		std::string osMatrix;	 // 「FILE_ATTRIBUTE_x→fbY=出た/出ない」の一覧
	};

	void AppendName(std::string& list, const std::string& name)
	{
		if (!list.empty())
			list += " ";
		list += name;
	}

	// -----------------------------------------------------------------------
	// 対象 1 つを測って書く（フォルダ用とファイル用で呼ぶ関数名が違うだけ）。
	// OS 側の属性も同じ行の下へ出す。
	VCOMError LogFolder(::vwprobe::Report& probe, const std::string& label,
						IFolderIdentifier* folder)
	{
		if (folder == nullptr)
		{
			probe.log(label + ": ポインタが nullptr（測れない）");
			return kVCOMError_Failed;
		}

		TXString fullPath;
		folder->GetFullPath(fullPath);
		bool exists = false;
		folder->ExistsOnDisk(exists);

		SAttributes attributes = {};
		const VCOMError attrErr = folder->GetAttributes(attributes);
		probe.log(label + ": path=[" + Utf8Of(fullPath) + "] 実在=" + YesNo(exists));
		probe.log("    GetAttributes=" + ErrWord(attrErr) + " " + AttrWord(attributes));
		probe.log("    OS: " + WinAttrWord(WithoutTrailingSlash(WideOf(fullPath))));
		return attrErr;
	}

	VCOMError LogFile(::vwprobe::Report& probe, const std::string& label, IFileIdentifier* file)
	{
		if (file == nullptr)
		{
			probe.log(label + ": ポインタが nullptr（測れない）");
			return kVCOMError_Failed;
		}

		TXString fullPath;
		file->GetFileFullPath(fullPath);
		bool exists = false;
		file->ExistsOnDisk(exists);

		SAttributes attributes = {};
		const VCOMError attrErr = file->GetAttributes(attributes);
		probe.log(label + ": path=[" + Utf8Of(fullPath) + "] 実在=" + YesNo(exists));
		probe.log("    GetAttributes=" + ErrWord(attrErr) + " " + AttrWord(attributes));
		probe.log("    OS: " + WinAttrWord(WideOf(fullPath)));
		return attrErr;
	}

	// -----------------------------------------------------------------------
	// **本命。** 11 旗を 1 つずつ反転して SetAttributes で書き、GetAttributes で
	// 読み戻して、旗ごとに「反映されたか」「ディスクまで届いたか」を出す。書いた後は
	// 必ず元の値へ戻す（戻し切れなかったときは、その差もログへ出す）。
	template <typename TIdent>
	void RoundTripFlags(::vwprobe::Report& probe, const std::string& who, TIdent* ident,
						Tally& tally, const std::wstring& diskPath)
	{
		if (ident == nullptr)
		{
			probe.log("  " + who + ": ポインタが nullptr（測れない）");
			return;
		}

		SAttributes base = {};
		const VCOMError baseErr = ident->GetAttributes(base);
		probe.log("  " + who + " の初期値: GetAttributes=" + ErrWord(baseErr) + " " +
				  AttrWord(base));
		probe.log("  " + who + " の初期値（OS）: " + WinAttrWord(diskPath));
		if (!VCOM_SUCCEEDED(baseErr))
		{
			probe.log("  初期値が読めないので往復は測れない");
			return;
		}
		const std::string diskBase = WinAttrWord(diskPath);

		for (size_t i = 0; i < kAttributeFlagCount; ++i)
		{
			const std::string name = kAttributeFlagTable[i].fName;
			bool SAttributes::*member = kAttributeFlagTable[i].fMember;

			SAttributes want = base;
			want.*member = !(base.*member);

			// **書く前にログへ出す。** ここで落ちる可能性があるなら、落ちた場所が
			// そのまま最後の行として残る。
			probe.log("  [" + name + "] " + YesNo(base.*member) + " → " + YesNo(want.*member) +
					  " を書く");

			const std::string diskBefore = WinAttrWord(diskPath);

			const VCOMError setErr = ident->SetAttributes(want);

			SAttributes back = {};
			const VCOMError getErr = ident->GetAttributes(back);
			const bool applied = VCOM_SUCCEEDED(getErr) && (back.*member == want.*member);
			const std::string diskAfter = WinAttrWord(diskPath);

			probe.log("      SetAttributes=" + ErrWord(setErr) + " 読み戻し=" + ErrWord(getErr) +
					  " その旗=" + YesNo(back.*member) + " → " +
					  (applied ? "反映された" : "反映されない"));
			probe.log("      初期値との差: " + DiffWord(base, back));
			LogDiskChange(probe, diskPath, diskBefore);

			++tally.setCalls;
			if (VCOM_SUCCEEDED(setErr))
				++tally.setSucceeded;
			if (applied)
			{
				++tally.readBackApplied;
				AppendName(tally.appliedNames, who + ":" + name);
			}
			if (diskBefore != diskAfter)
			{
				++tally.diskChanged;
				AppendName(tally.diskNames, who + ":" + name);
			}

			// 元へ戻す。戻らなければ、その事実を書く（後片付けが効かない＝知見）。
			const VCOMError restoreErr = ident->SetAttributes(base);
			SAttributes now = {};
			if (VCOM_SUCCEEDED(ident->GetAttributes(now)))
			{
				const std::string restDiff = DiffWord(base, now);
				if (restDiff != "（差なし）")
					probe.log("      戻し: SetAttributes=" + ErrWord(restoreErr) +
							  " まだ初期値と差がある: " + restDiff);
			}
			const std::string diskRestored = WinAttrWord(diskPath);
			if (diskRestored != diskBase)
				probe.log("      戻し（OS）: まだ初期値と差がある: 初期=" + diskBase +
						  " 今=" + diskRestored);
		}

		// まとめて全部 true / 全部 false。1 つずつでは動かなくても「揃っていないと
		// 受け付けない」実装があり得るので、**総当たりとは別に 1 回ずつ試す**。
		{
			SAttributes allTrue = {};
			SAttributes allFalse = {};
			for (size_t i = 0; i < kAttributeFlagCount; ++i)
			{
				allTrue.*kAttributeFlagTable[i].fMember = true;
				allFalse.*kAttributeFlagTable[i].fMember = false;
			}

			probe.log("  [まとめて全部 true] を書く");
			std::string diskBefore = WinAttrWord(diskPath);
			const VCOMError allTrueErr = ident->SetAttributes(allTrue);
			SAttributes afterAllTrue = {};
			ident->GetAttributes(afterAllTrue);
			probe.log("      SetAttributes=" + ErrWord(allTrueErr) +
					  " 読み戻し: " + AttrWord(afterAllTrue));
			probe.log("      初期値との差: " + DiffWord(base, afterAllTrue));
			LogDiskChange(probe, diskPath, diskBefore);

			probe.log("  [まとめて全部 false] を書く");
			diskBefore = WinAttrWord(diskPath);
			const VCOMError allFalseErr = ident->SetAttributes(allFalse);
			SAttributes afterAllFalse = {};
			ident->GetAttributes(afterAllFalse);
			probe.log("      SetAttributes=" + ErrWord(allFalseErr) +
					  " 読み戻し: " + AttrWord(afterAllFalse));
			probe.log("      初期値との差: " + DiffWord(base, afterAllFalse));
			LogDiskChange(probe, diskPath, diskBefore);

			// 必ず初期値へ戻してから抜ける（この後この対象を使い、最後に消すため）。
			ident->SetAttributes(base);
			SAttributes now = {};
			if (VCOM_SUCCEEDED(ident->GetAttributes(now)))
				probe.log("  " + who + " を初期値へ戻した: " + DiffWord(base, now) +
						  " OS=" + WinAttrWord(diskPath));
		}
	}

	// -----------------------------------------------------------------------
	// OS 側で属性を 1 つ立て、SAttributes のどの旗が動くかを測る（6 節の 1 件）。
	// 基準は FILE_ATTRIBUTE_NORMAL（フォルダでは DIRECTORY だけ）にしてから読む。
	struct SOsCase
	{
		const char* fName;
		DWORD fAttr;				// SetFileAttributesW に渡す値
		bool SAttributes::*fExpect; // 立つと見込む旗（nullptr なら対応物なし＝対照）
	};

	template <typename TIdent>
	void MeasureOsCases(::vwprobe::Report& probe, const std::string& who, TIdent* ident,
						const std::wstring& path, const SOsCase* cases, size_t count, Tally& tally)
	{
		ClearAttributes(path);
		SAttributes baseline = {};
		const VCOMError baselineErr = ident->GetAttributes(baseline);
		probe.log("  " + who + " 基準（NORMAL）: GetAttributes=" + ErrWord(baselineErr) + " " +
				  AttrWord(baseline));
		probe.log("      OS: " + WinAttrWord(path));

		for (size_t i = 0; i < count; ++i)
		{
			// 状態を作る。**作れたかどうか（GetLastError）も必ず出す**——立てられなかった
			// 属性について「SAttributes が写さなかった」と読み違えないため。
			const BOOL ok = ::SetFileAttributesW(path.c_str(), cases[i].fAttr);
			const DWORD err = ::GetLastError();
			probe.log("  " + who + " に " + cases[i].fName +
					  " を立てる: SetFileAttributesW=" + BoolCallWord(ok, err));
			probe.log("      OS: " + WinAttrWord(path));

			SAttributes measured = {};
			const VCOMError measuredErr = ident->GetAttributes(measured);
			probe.log("      GetAttributes=" + ErrWord(measuredErr) + " " + AttrWord(measured));
			const bool comparable = VCOM_SUCCEEDED(measuredErr) && VCOM_SUCCEEDED(baselineErr);
			const std::string diff =
				comparable ? DiffWord(baseline, measured) : std::string("（測れない）");
			probe.log("      基準との差: " + diff);

			++tally.osFlagCases;
			if (cases[i].fExpect != nullptr)
			{
				std::string expectName = "?";
				for (size_t k = 0; k < kAttributeFlagCount; ++k)
					if (kAttributeFlagTable[k].fMember == cases[i].fExpect)
						expectName = kAttributeFlagTable[k].fName;
				const bool moved =
					comparable && ok && (measured.*cases[i].fExpect != baseline.*cases[i].fExpect);
				if (moved)
					++tally.osFlagExpected;
				AppendName(tally.osMatrix,
						   who + ":" + cases[i].fName + "→" + expectName + "=" +
							   (!ok ? std::string("（立てられず）")
									: (moved ? std::string("出た") : std::string("出ない"))));
			}

			ClearAttributes(path);
		}
	}

	// 読むだけの標本 1 件（7 節）。
	void LogSystemFolder(::vwprobe::Report& probe, const std::wstring& path)
	{
		IFolderIdentifierPtr folder(IID_FolderIdentifier);
		const VCOMError setErr = folder->Set(TXString(path));
		if (VCOM_SUCCEEDED(setErr))
			LogFolder(probe, "  " + Utf8OfWide(path), folder);
		else
			probe.log("  " + Utf8OfWide(path) + ": Set=" + ErrWord(setErr) +
					  " OS: " + WinAttrWord(WithoutTrailingSlash(path)));
	}

	void LogSystemFile(::vwprobe::Report& probe, const std::wstring& path)
	{
		IFileIdentifierPtr file(IID_FileIdentifier);
		const VCOMError setErr = file->Set(TXString(path));
		if (VCOM_SUCCEEDED(setErr))
			LogFile(probe, "  " + Utf8OfWide(path), file);
		else
			probe.log("  " + Utf8OfWide(path) + ": Set=" + ErrWord(setErr) +
					  " OS: " + WinAttrWord(path));
	}
} // namespace
#endif // GS_WIN

VW_PROBE("sattributes-windows", "SAttributes を Windows で測る",
		 "Windows 専用。11 旗の往復と OS 属性の写り方")
{
#if !GS_WIN
	probe.log("このプローブは Windows 専用です。macOS では何も測りません"
			  "（macOS は issue #91 / sattributes-write で測り終えています）。");
#else
	Tally tally;

	// -----------------------------------------------------------------------
	// 0) 素材を作る。一時フォルダ 1 つと、その中のファイル 1 つ。
	probe.log("0) 素材（一時フォルダ＋その中のファイル）を作る");

	IFolderIdentifierPtr tempRoot(IID_FolderIdentifier);
	IFolderIdentifierPtr workFolder(IID_FolderIdentifier);
	IFileIdentifierPtr workFile(IID_FileIdentifier);
	std::wstring workFolderPath; // 末尾に \ を付けた形
	std::wstring workFolderDisk; // 末尾の \ を外した形（Win32 へ渡す用）
	std::wstring workFilePath;
	bool haveFolder = false;
	bool haveFile = false;

	{
		const VCOMError tempErr = tempRoot->Set(EOSFolderSpecifier::SystemTempDirectory);
		TXString tempPath;
		tempRoot->GetFullPath(tempPath);
		probe.log("  一時フォルダ: Set=" + ErrWord(tempErr) + " path=[" + Utf8Of(tempPath) + "]");
		if (!VCOM_SUCCEEDED(tempErr))
		{
			probe.fail("一時フォルダを掴めなかった（この先は測れない）");
			return;
		}

		// ボリュームの素性。**EFS（fbEncrypted の標本）が作れるかはここで決まる**ので先に出す。
		{
			const std::wstring tempWide = WideOf(tempPath);
			wchar_t root[MAX_PATH] = {};
			wchar_t fsName[64] = {};
			DWORD fsFlags = 0;
			const BOOL rootOk = ::GetVolumePathNameW(tempWide.c_str(), root, MAX_PATH);
			const BOOL volOk = rootOk && ::GetVolumeInformationW(
											 root, nullptr, 0, nullptr, nullptr, &fsFlags, fsName,
											 (DWORD)(sizeof(fsName) / sizeof(fsName[0])));
			char flagsHex[16] = {};
			std::snprintf(flagsHex, sizeof(flagsHex), "0x%08lx", (unsigned long)fsFlags);
			probe.log("  ボリューム: root=[" + Utf8OfWide(root) + "] fs=" + Utf8OfWide(fsName) +
					  " flags=" + flagsHex +
					  " 暗号化(FILE_SUPPORTS_ENCRYPTION)=" + YesNo((fsFlags & 0x00020000) != 0) +
					  (volOk ? std::string() : " ※GetVolumeInformationW 失敗"));

			// OS の版とエディション。**Home エディションには EFS が無い**ので、6d が
			// 測れなかったときの理由をここで読めるようにする。
			probe.log("  OS: " + RegistryWord(L"ProductName") + " / " + RegistryWord(L"EditionID") +
					  " / " + RegistryWord(L"DisplayVersion") + " / build " +
					  RegistryWord(L"CurrentBuild"));
		}

		const VCOMError setErr = workFolder->Set(tempRoot, "VwSdkProbes-sattributes-windows");
		bool already = false;
		workFolder->ExistsOnDisk(already);
		if (already)
			probe.log("  作業フォルダが前回から残っていた（そのまま使う）");
		const VCOMError createErr =
			!VCOM_SUCCEEDED(setErr) ? setErr
									: (already ? kVCOMError_NoError : workFolder->CreateOnDisk());
		TXString folderPath;
		workFolder->GetFullPath(folderPath);
		workFolderPath = WithTrailingSlash(WideOf(folderPath));
		workFolderDisk = WithoutTrailingSlash(workFolderPath);
		bool folderExists = false;
		workFolder->ExistsOnDisk(folderExists);
		haveFolder = VCOM_SUCCEEDED(createErr) && folderExists;
		probe.log("  作業フォルダ: Set=" + ErrWord(setErr) + " CreateOnDisk=" + ErrWord(createErr) +
				  " 実在=" + YesNo(folderExists) + " path=[" + Utf8Of(folderPath) + "]");

		if (!haveFolder)
		{
			probe.fail("作業フォルダを作れなかった（この先は測れない）");
			return;
		}
		ClearAttributes(workFolderDisk);

		// ファイルは SDK に「作る」口が無いので、**素の Win32 で書いて**作る。
		// パスは SDK に組ませる（区切りや末尾の \ の扱いを推測しないため）。
		const VCOMError fileSetErr = workFile->Set(workFolder, "case.txt");
		TXString filePath;
		workFile->GetFileFullPath(filePath);
		workFilePath = WideOf(filePath);
		std::string why;
		ClearAttributes(workFilePath);
		const bool written =
			VCOM_SUCCEEDED(fileSetErr) && WriteNewFile(workFilePath, "vw-sdk-probe", why);
		bool fileExists = false;
		workFile->ExistsOnDisk(fileExists);
		haveFile = written && fileExists;
		probe.log("  作業ファイル: Set=" + ErrWord(fileSetErr) + " 書いた=" + YesNo(written) +
				  (why.empty() ? std::string() : "（" + why + "）") + " 実在=" + YesNo(fileExists) +
				  " path=[" + Utf8Of(filePath) + "]");
		probe.log("  作業フォルダ OS: " + WinAttrWord(workFolderDisk));
		if (haveFile)
			probe.log("  作業ファイル OS: " + WinAttrWord(workFilePath) +
					  "（新しく作ったファイルには ARCHIVE が立つのが Windows の既定）");
	}

	// -----------------------------------------------------------------------
	// 1) 本命その 1——フォルダの SetAttributes 往復。
	probe.log("1) SetAttributes の往復（フォルダ。11 旗を 1 つずつ反転して書く）");
	IFolderIdentifier* const workFolderRaw = workFolder;
	RoundTripFlags(probe, "作業フォルダ", workFolderRaw, tally, workFolderDisk);

	// -----------------------------------------------------------------------
	// 2) 本命その 2——ファイルの SetAttributes 往復。フォルダと**同じ並び**で出す。
	probe.log("2) SetAttributes の往復（ファイル。11 旗を 1 つずつ反転して書く）");
	if (haveFile)
	{
		IFileIdentifier* const workFileRaw = workFile;
		RoundTripFlags(probe, "作業ファイル", workFileRaw, tally, workFilePath);
	}
	else
		probe.log("  作業ファイルを作れなかったので飛ばす");

	// -----------------------------------------------------------------------
	// 3) 「書けた」ように見えたとき、**本当に書けなくなるか**。
	probe.log("3) 読み取り専用にしたファイルへ、本当に書けなくなるか");
	if (haveFile)
	{
		SAttributes base = {};
		const VCOMError baseErr = workFile->GetAttributes(base);

		probe.log("  書く前: " + WinAttrWord(workFilePath) +
				  " 追記=" + TryAppendWord(workFilePath));

		SAttributes readOnly = base;
		readOnly.fbReadOnly = true;
		readOnly.fbCanWrite = false;
		const VCOMError setErr = workFile->SetAttributes(readOnly);

		SAttributes back = {};
		workFile->GetAttributes(back);
		probe.log("  fbReadOnly=yes / fbCanWrite=no を書いた: SetAttributes=" + ErrWord(setErr) +
				  " 読み戻し fbReadOnly=" + YesNo(back.fbReadOnly) +
				  " fbCanWrite=" + YesNo(back.fbCanWrite));
		probe.log("  書いた後: " + WinAttrWord(workFilePath) +
				  " 追記=" + TryAppendWord(workFilePath));

		// fbCanWrite だけを落としたら？（macOS では POSIX の w ビットに当たった）
		SAttributes cannotWrite = base;
		cannotWrite.fbCanWrite = false;
		workFile->SetAttributes(base);
		const VCOMError cwErr = workFile->SetAttributes(cannotWrite);
		SAttributes cwBack = {};
		workFile->GetAttributes(cwBack);
		probe.log("  fbCanWrite=no だけを書いた: SetAttributes=" + ErrWord(cwErr) +
				  " 読み戻し fbReadOnly=" + YesNo(cwBack.fbReadOnly) +
				  " fbCanWrite=" + YesNo(cwBack.fbCanWrite) + " OS=" + WinAttrWord(workFilePath) +
				  " 追記=" + TryAppendWord(workFilePath));

		// 元へ戻して、書けるように戻るかまで見る（戻らなければ後片付けが効かない）。
		if (VCOM_SUCCEEDED(baseErr))
			workFile->SetAttributes(base);
		probe.log("  初期値へ戻した後: " + WinAttrWord(workFilePath) +
				  " 追記=" + TryAppendWord(workFilePath));
		ClearAttributes(workFilePath);
	}
	else
	{
		probe.log("  作業ファイルを作れなかったので飛ばす");
	}

	// -----------------------------------------------------------------------
	// 3b) 同じことをフォルダでも（子を作れるかで見る）。
	//     Windows ではフォルダの READONLY は「中へ書けない」を意味しない（Explorer の
	//     カスタマイズ用の印）ので、**作れてしまう**のが OS の素の挙動の見込み。
	probe.log("3b) 読み取り専用にしたフォルダの中へ、本当に作れなくなるか");
	{
		SAttributes base = {};
		const VCOMError baseErr = workFolder->GetAttributes(base);

		SAttributes readOnly = base;
		readOnly.fbReadOnly = true;
		readOnly.fbCanWrite = false;
		const VCOMError setErr = workFolder->SetAttributes(readOnly);

		SAttributes back = {};
		workFolder->GetAttributes(back);
		probe.log("  fbReadOnly=yes / fbCanWrite=no を書いた: SetAttributes=" + ErrWord(setErr) +
				  " 読み戻し fbReadOnly=" + YesNo(back.fbReadOnly) +
				  " fbCanWrite=" + YesNo(back.fbCanWrite) + " OS=" + WinAttrWord(workFolderDisk));

		IFolderIdentifierPtr child(IID_FolderIdentifier);
		const VCOMError childSetErr = child->Set(workFolder, "child-after-readonly");
		const VCOMError childCreateErr =
			VCOM_SUCCEEDED(childSetErr) ? child->CreateOnDisk() : childSetErr;
		bool childExists = false;
		child->ExistsOnDisk(childExists);
		probe.log("  その中へフォルダを作ってみる: CreateOnDisk=" + ErrWord(childCreateErr) +
				  " 実在=" + YesNo(childExists) + " → " +
				  (childExists ? "作れた（＝書き込みは止まっていない）"
							   : "作れなかった（＝本当に読み取り専用になった）"));

		if (childExists)
			child->DeleteOnDisk();

		if (VCOM_SUCCEEDED(baseErr))
			workFolder->SetAttributes(base);
		ClearAttributes(workFolderDisk);
	}

	// -----------------------------------------------------------------------
	// 4) 実在しない対象への SetAttributes は何を返すか。
	probe.log("4) 実在しない対象への SetAttributes");
	{
		SAttributes attributes = {};

		IFolderIdentifierPtr ghostFolder(IID_FolderIdentifier);
		if (VCOM_SUCCEEDED(ghostFolder->Set(tempRoot, "VwSdkProbes-no-such-folder-93")))
		{
			const VCOMError getErr = ghostFolder->GetAttributes(attributes);
			const VCOMError setErr = ghostFolder->SetAttributes(attributes);
			probe.log("  実在しないフォルダ: GetAttributes=" + ErrWord(getErr) +
					  " SetAttributes=" + ErrWord(setErr));
		}

		IFileIdentifierPtr ghostFile(IID_FileIdentifier);
		if (VCOM_SUCCEEDED(ghostFile->Set(tempRoot, "VwSdkProbes-no-such-file-93.txt")))
		{
			const VCOMError getErr = ghostFile->GetAttributes(attributes);
			const VCOMError setErr = ghostFile->SetAttributes(attributes);
			probe.log("  実在しないファイル: GetAttributes=" + ErrWord(getErr) +
					  " SetAttributes=" + ErrWord(setErr));
		}
	}

	// -----------------------------------------------------------------------
	// 5) **対照**——同じ識別子の「Set* の半分」である SetAttributesTimeDateReference。
	probe.log("5) 対照: SetAttributesTimeDateReference は書き戻せるか（作業ファイル）");
	if (haveFile)
	{
		struct STimeCase
		{
			EAttributesTimeReference fRef;
			const char* fName;
		};
		const STimeCase timeCases[] = {
			{eAttributesTimeReference_Created, "Created"},
			{eAttributesTimeReference_LastAccessed, "LastAccessed"},
			{eAttributesTimeReference_LastWritten, "LastWritten"},
			{eAttributesTimeReference_LastBackup, "LastBackup"},
		};

		for (size_t i = 0; i < sizeof(timeCases) / sizeof(timeCases[0]); ++i)
		{
			SAttributesDateTime now = {};
			const VCOMError getErr =
				workFile->GetAttributesTimeDateReference(timeCases[i].fRef, now);
			probe.log(std::string("  ") + timeCases[i].fName + ": GetAttributesTimeDateReference=" +
					  ErrWord(getErr) + " " + std::to_string((long)now.fYear) + "-" +
					  std::to_string((long)now.fMonth) + "-" + std::to_string((long)now.fDay) +
					  " " + std::to_string((long)now.fHour) + ":" +
					  std::to_string((long)now.fMinute) + ":" + std::to_string((long)now.fSecond));

			SAttributesDateTime want = {};
			want.fYear = 2001;
			want.fMonth = 2;
			want.fDay = 3;
			want.fHour = 4;
			want.fMinute = 5;
			want.fSecond = 6;
			const VCOMError setErr =
				workFile->SetAttributesTimeDateReference(timeCases[i].fRef, want);

			SAttributesDateTime back = {};
			const VCOMError backErr =
				workFile->GetAttributesTimeDateReference(timeCases[i].fRef, back);
			const bool applied = VCOM_SUCCEEDED(backErr) && back.fYear == want.fYear &&
								 back.fMonth == want.fMonth && back.fDay == want.fDay;
			probe.log(std::string("      2001-02-03 04:05:06 を書く: Set=") + ErrWord(setErr) +
					  " 読み戻し=" + ErrWord(backErr) + " " + std::to_string((long)back.fYear) +
					  "-" + std::to_string((long)back.fMonth) + "-" +
					  std::to_string((long)back.fDay) + " " + std::to_string((long)back.fHour) +
					  ":" + std::to_string((long)back.fMinute) + ":" +
					  std::to_string((long)back.fSecond) + " → " +
					  (applied ? "反映された" : "反映されない"));
		}
	}
	else
	{
		probe.log("  作業ファイルを作れなかったので飛ばす");
	}

	// -----------------------------------------------------------------------
	// 6) **「立つはずの標本」を OS 側で作って測る**（SetFileAttributesW）。
	//    macOS の chmod / chflags に当たる節。ここで SAttributes の旗と
	//    FILE_ATTRIBUTE_* の対応が決まる。NOT_CONTENT_INDEXED は SAttributes に
	//    対応物が無い**対照**（これで何かが動いたら、写し方を疑う）。
	probe.log("6) OS 側で属性を立てた標本（SetFileAttributesW）");
	{
		const SOsCase fileCases[] = {
			{"READONLY", FILE_ATTRIBUTE_READONLY, &SAttributes::fbReadOnly},
			{"HIDDEN", FILE_ATTRIBUTE_HIDDEN, &SAttributes::fbHidden},
			{"SYSTEM", FILE_ATTRIBUTE_SYSTEM, &SAttributes::fbSystem},
			{"ARCHIVE", FILE_ATTRIBUTE_ARCHIVE, &SAttributes::fbArchive},
			{"TEMPORARY", FILE_ATTRIBUTE_TEMPORARY, &SAttributes::fbTemporary},
			{"HIDDEN|SYSTEM", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM,
			 &SAttributes::fbSystem},
			{"NOT_CONTENT_INDEXED（対照）", FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr},
		};
		// フォルダには TEMPORARY は立てられない（SetFileAttributesW が黙って捨てる）ので外す。
		const SOsCase folderCases[] = {
			{"READONLY", FILE_ATTRIBUTE_READONLY, &SAttributes::fbReadOnly},
			{"HIDDEN", FILE_ATTRIBUTE_HIDDEN, &SAttributes::fbHidden},
			{"SYSTEM", FILE_ATTRIBUTE_SYSTEM, &SAttributes::fbSystem},
			{"ARCHIVE", FILE_ATTRIBUTE_ARCHIVE, &SAttributes::fbArchive},
			{"NOT_CONTENT_INDEXED（対照）", FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr},
		};

		if (haveFile)
		{
			IFileIdentifier* const workFileRaw = workFile;
			MeasureOsCases(probe, "ファイル", workFileRaw, workFilePath, fileCases,
						   sizeof(fileCases) / sizeof(fileCases[0]), tally);
		}
		else
			probe.log("  作業ファイルを作れなかったのでファイル側は飛ばす");

		IFolderIdentifierPtr sampleFolder(IID_FolderIdentifier);
		if (VCOM_SUCCEEDED(sampleFolder->Set(workFolder, "os-sample")) &&
			VCOM_SUCCEEDED(sampleFolder->CreateOnDisk()))
		{
			TXString samplePath;
			sampleFolder->GetFullPath(samplePath);
			const std::wstring sampleDisk = WithoutTrailingSlash(WideOf(samplePath));
			IFolderIdentifier* const sampleRaw = sampleFolder;
			MeasureOsCases(probe, "フォルダ", sampleRaw, sampleDisk, folderCases,
						   sizeof(folderCases) / sizeof(folderCases[0]), tally);
			ClearAttributes(sampleDisk);
			sampleFolder->DeleteOnDisk();
		}
		else
			probe.log("  標本フォルダ os-sample を作れなかったのでフォルダ側は飛ばす");
	}

	// -----------------------------------------------------------------------
	// 6b) fbArchive は**ふつうに書き込んだだけで立つ**か。ARCHIVE を落としてから
	//     1 バイト追記し、OS と SAttributes の両方を見る。
	probe.log("6b) ARCHIVE を落としてから書き込むと fbArchive が立つか");
	if (haveFile)
	{
		ClearAttributes(workFilePath);
		SAttributes before = {};
		const VCOMError beforeErr = workFile->GetAttributes(before);
		probe.log("  落とした後: OS=" + WinAttrWord(workFilePath) +
				  " GetAttributes=" + ErrWord(beforeErr) + " fbArchive=" + YesNo(before.fbArchive));
		const std::string appendWord = TryAppendWord(workFilePath);
		SAttributes after = {};
		const VCOMError afterErr = workFile->GetAttributes(after);
		probe.log("  1 バイト追記（" + appendWord + "）した後: OS=" + WinAttrWord(workFilePath) +
				  " GetAttributes=" + ErrWord(afterErr) + " fbArchive=" + YesNo(after.fbArchive));
		probe.log("      差: " + DiffWord(before, after));
		ClearAttributes(workFilePath);
	}
	else
		probe.log("  作業ファイルを作れなかったので飛ばす");

	// -----------------------------------------------------------------------
	// 6c) fbCanExecute は拡張子で決まるか。Windows に実行ビットは無いので、
	//     立つとすれば拡張子（PATHEXT）か ACL の実行権しか材料が無い。
	probe.log("6c) 拡張子で fbCanExecute が変わるか");
	{
		const char* names[] = {"exec.exe", "exec.bat", "exec.cmd", "exec.com",
							   "exec.ps1", "exec.vbs", "exec.dll", "exec.txt"};
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
		{
			IFileIdentifierPtr file(IID_FileIdentifier);
			if (!VCOM_SUCCEEDED(file->Set(workFolder, names[i])))
			{
				probe.log(std::string("  ") + names[i] + ": Set が失敗した");
				continue;
			}
			TXString path;
			file->GetFileFullPath(path);
			std::string why;
			if (!WriteNewFile(WideOf(path), "rem vw-sdk-probe\r\n", why))
			{
				probe.log(std::string("  ") + names[i] + ": 作れなかった（" + why + "）");
				continue;
			}
			LogFile(probe, std::string("  ") + names[i], file);
			file->DeleteOnDisk();
		}
	}

	// -----------------------------------------------------------------------
	// 6d) fbEncrypted——EFS（EncryptFileW）で暗号化した標本。
	//     EFS は Home エディションや FAT/exFAT では使えない。**使えないときは
	//     その理由を出して飛ばす**——「fbEncrypted が立たなかった」と読み違えないため。
	probe.log("6d) EFS で暗号化した標本で fbEncrypted が立つか");
	{
		IFileIdentifierPtr encFile(IID_FileIdentifier);
		if (VCOM_SUCCEEDED(encFile->Set(workFolder, "encrypted.txt")))
		{
			TXString encPathTx;
			encFile->GetFileFullPath(encPathTx);
			const std::wstring encPath = WideOf(encPathTx);
			std::string why;
			if (WriteNewFile(encPath, "vw-sdk-probe-efs", why))
			{
				SAttributes plain = {};
				const VCOMError plainErr = encFile->GetAttributes(plain);
				probe.log("  暗号化する前: OS=" + WinAttrWord(encPath) +
						  " GetAttributes=" + ErrWord(plainErr) + " " + AttrWord(plain));

				// **書く前にログへ出す**（EncryptFileW は EFS の証明書を初回に作るので、
				// 環境によっては時間がかかる）。
				probe.log("  EncryptFileW を呼ぶ");
				const BOOL encOk = ::EncryptFileW(encPath.c_str());
				const DWORD encErr = ::GetLastError();
				DWORD status = 0xFFFFFFFF;
				::FileEncryptionStatusW(encPath.c_str(), &status);
				probe.log("  EncryptFileW=" + BoolCallWord(encOk, encErr) +
						  " FileEncryptionStatusW=" + std::to_string((unsigned long)status) +
						  "（1=暗号化済み 0=暗号化できる 2=システムファイル 3=ルート 4=システム"
						  "フォルダ 5=EFS 無効 6=FS 非対応 8=読み取り専用）");
				probe.log("  OS: " + WinAttrWord(encPath));

				const DWORD osAttr = ::GetFileAttributesW(encPath.c_str());
				const bool reallyEncrypted =
					osAttr != INVALID_FILE_ATTRIBUTES && (osAttr & FILE_ATTRIBUTE_ENCRYPTED) != 0;
				if (!reallyEncrypted)
				{
					probe.log(
						"  → **EFS で暗号化できなかった**。fbEncrypted の標本は作れていない"
						"（「SAttributes が写さなかった」のではない）。この節は測れずに終わる");
				}
				else
				{
					SAttributes enc = {};
					const VCOMError encGetErr = encFile->GetAttributes(enc);
					probe.log("  暗号化した後: GetAttributes=" + ErrWord(encGetErr) + " " +
							  AttrWord(enc));
					probe.log("      暗号化前との差: " + DiffWord(plain, enc));
					probe.log(std::string("  → fbEncrypted は ") +
							  (enc.fbEncrypted ? "**立った**" : "**立たない**（EFS の標本で no）"));

					// 逆向き: fbEncrypted=false を SetAttributes で書いたら復号されるか。
					SAttributes clear = enc;
					clear.fbEncrypted = false;
					const VCOMError clearErr = encFile->SetAttributes(clear);
					SAttributes clearBack = {};
					encFile->GetAttributes(clearBack);
					probe.log("  fbEncrypted=no を SetAttributes で書く: " + ErrWord(clearErr) +
							  " 読み戻し fbEncrypted=" + YesNo(clearBack.fbEncrypted) +
							  " OS=" + WinAttrWord(encPath));

					const BOOL decOk = ::DecryptFileW(encPath.c_str(), 0);
					probe.log(
						"  DecryptFileW（後片付け）=" + BoolCallWord(decOk, ::GetLastError()) +
						" OS=" + WinAttrWord(encPath));
				}

				// 平文のファイルへ fbEncrypted=true を書いたら暗号化されるか
				// （2 節の総当たりにも含まれるが、ここで OS の答えと並べて出す）。
				{
					SAttributes now = {};
					encFile->GetAttributes(now);
					if (!now.fbEncrypted)
					{
						SAttributes want = now;
						want.fbEncrypted = true;
						const VCOMError setErr = encFile->SetAttributes(want);
						SAttributes back = {};
						encFile->GetAttributes(back);
						probe.log(
							"  平文へ fbEncrypted=yes を SetAttributes で書く: " + ErrWord(setErr) +
							" 読み戻し fbEncrypted=" + YesNo(back.fbEncrypted) +
							" OS=" + WinAttrWord(encPath));
						if ((::GetFileAttributesW(encPath.c_str()) & FILE_ATTRIBUTE_ENCRYPTED) != 0)
							::DecryptFileW(encPath.c_str(), 0);
					}
				}
			}
			else
				probe.log("  標本を作れなかった（" + why + "）");

			ClearAttributes(encPath);
			encFile->DeleteOnDisk();
		}
		else
			probe.log("  Set が失敗した");
	}

	// -----------------------------------------------------------------------
	// 7) システム領域の標本。fbSystem / fbHidden / fbCanExecute が立つ場所があるか。
	//    **読むだけ**（1 バイトも書かない）。ドライブ文字は決め打ちせず、
	//    GetWindowsDirectoryW から引く。
	probe.log("7) システム領域の標本（読むだけ。fbSystem / fbHidden / fbCanExecute 狙い）");
	{
		wchar_t winDirBuf[MAX_PATH] = {};
		::GetWindowsDirectoryW(winDirBuf, MAX_PATH);
		// winDir は「C:\Windows\」の形、drive は「C:\」の形（どちらも末尾に区切り）。
		const std::wstring winDir = WithTrailingSlash(winDirBuf);
		const std::wstring drive = winDir.size() >= 3 ? winDir.substr(0, 3) : L"C:\\";

		wchar_t profileBuf[MAX_PATH] = {};
		const DWORD profileLen = ::GetEnvironmentVariableW(L"USERPROFILE", profileBuf, MAX_PATH);
		const std::wstring profile = (profileLen > 0 && profileLen < MAX_PATH)
										 ? WithTrailingSlash(profileBuf)
										 : std::wstring();

		const std::wstring folders[] = {
			drive,
			winDir,
			winDir + L"System32\\",
			drive + L"Program Files\\",
			drive + L"ProgramData\\",				// 既定で HIDDEN
			drive + L"System Volume Information\\", // 既定で HIDDEN|SYSTEM（読めない）
			drive + L"$Recycle.Bin\\",				// 既定で HIDDEN|SYSTEM
			profile.empty() ? std::wstring() : profile + L"AppData\\", // 既定で HIDDEN
		};
		for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); ++i)
			if (!folders[i].empty())
				LogSystemFolder(probe, folders[i]);

		const std::wstring files[] = {
			winDir + L"System32\\kernel32.dll",
			winDir + L"System32\\cmd.exe",
			winDir + L"System32\\drivers\\etc\\hosts",
			winDir + L"System32\\desktop.ini", // 既定で HIDDEN|SYSTEM のことが多い
			drive + L"pagefile.sys",		   // HIDDEN|SYSTEM。使用中で開けない
			profile.empty() ? std::wstring() : profile + L"NTUSER.DAT", // HIDDEN|SYSTEM・使用中
		};
		for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
			if (!files[i].empty())
				LogSystemFile(probe, files[i]);
	}

	// -----------------------------------------------------------------------
	// 8) 後片付け。作ったものは全部消す。属性を落としてから SDK で消し、
	//    消えなければ Win32 で消す（消え残りも知見として出す）。
	probe.log("8) 後片付け");
	{
		if (haveFile)
		{
			ClearAttributes(workFilePath);
			const VCOMError delErr = workFile->DeleteOnDisk();
			bool stillThere = false;
			workFile->ExistsOnDisk(stillThere);
			probe.log("  作業ファイル: DeleteOnDisk=" + ErrWord(delErr) +
					  " まだ残っている=" + YesNo(stillThere));
			if (stillThere)
				probe.log("    DeleteFileW=" +
						  BoolCallWord(::DeleteFileW(workFilePath.c_str()), ::GetLastError()));
		}
		ClearAttributes(workFolderDisk);
		const VCOMError delErr = workFolder->DeleteOnDisk();
		bool stillThere = false;
		workFolder->ExistsOnDisk(stillThere);
		probe.log("  作業フォルダ: DeleteOnDisk=" + ErrWord(delErr) +
				  " まだ残っている=" + YesNo(stillThere));
		if (stillThere)
			probe.log("    RemoveDirectoryW=" +
					  BoolCallWord(::RemoveDirectoryW(workFolderDisk.c_str()), ::GetLastError()));
	}

	// -----------------------------------------------------------------------
	// 9) 数え上げと**結論の 1 行**。ログを目で追わなくても答えが読めるようにする。
	probe.log("9) 数え上げ");
	probe.log("  SetAttributes（旗 1 つの反転）: 呼んだ=" + std::to_string(tally.setCalls) +
			  " 成功が返った=" + std::to_string(tally.setSucceeded) +
			  " 読み戻しに反映された=" + std::to_string(tally.readBackApplied) +
			  " ディスクの属性が動いた=" + std::to_string(tally.diskChanged));
	probe.log("  反映された旗: " +
			  (tally.appliedNames.empty() ? std::string("（1 つも無い）") : tally.appliedNames));
	probe.log("  ディスクまで届いた旗: " +
			  (tally.diskNames.empty() ? std::string("（1 つも無い）") : tally.diskNames));
	probe.log("  OS 側で属性を立てた標本: " + std::to_string(tally.osFlagCases) +
			  " 件 / そのうち狙った旗が立った標本=" + std::to_string(tally.osFlagExpected) + " 件");
	probe.log("  対応表: " +
			  (tally.osMatrix.empty() ? std::string("（測れていない）") : tally.osMatrix));

	if (tally.setCalls == 0)
		probe.log("  結論: SetAttributes を 1 度も呼べなかった（素材を作れていない）");
	else if (tally.setSucceeded == 0)
		probe.log("  結論: SetAttributes は**一度も成功を返さなかった**（書き戻す口は無い）");
	else if (tally.readBackApplied == 0)
		probe.log("  結論: SetAttributes は**成功を返すが、読み戻しに 1 つも反映されない**");
	else
		probe.log(
			"  結論: SetAttributes で**反映された旗がある**（上の「反映された旗」の一覧が答え）");

	if (tally.setCalls == 0)
		probe.fail("SetAttributes を 1 度も呼べなかった");
#endif // GS_WIN
}
