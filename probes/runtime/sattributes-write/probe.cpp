//
//	probes/runtime/sattributes-write/probe.cpp
//
//	[issue #91] `SAttributes` の**残りの旗はいつ立つのか**と、
//	**`SetAttributes(const SAttributes&)` で書き戻せるのか**を実機で確かめる。
//
//	なぜ確かめるか（issue #87 / PR #88 の積み残し）:
//	#87 で `GetAttributes` の 11 項目を macOS 実機で 34 件測り、**`fbDirectory` は
//	フォルダでもファイルでも常に false**、**`fbReadOnly` / `fbCanRead` / `fbCanWrite`
//	は値が変わる**ことまでは確定した（`Findings/File and Folder Identifiers.md`）。
//	ただし次の 2 つは**#87 の範囲外として残してある**:
//
//	  1) `fbSystem` / `fbEncrypted` / `fbArchive` は 34 件すべてで no だったが、
//	     **「立つはずの標本」を 1 つも含めていない**ので、「使えない」とまでは
//	     言えない（`fbHidden` / `fbTemporary` / `fbCanExecute` は「立つはずの標本で
//	     no だった」ところまで測ってある）。
//	  2) `SetAttributes` を**一度も呼んでいない**。書き戻せるのか、`GetAttributes`
//	     で読み戻せるのか、`fbDirectory` に true を書いたら何が起きるのかが未測定。
//
//	ヘッダで既に分かっていること（sdk-grep）:
//
//	  * `SetAttributes(const SAttributes&)` は `IFileIdentifier` と
//	    `IFolderIdentifier` の**両方**にある純粋仮想（`IFileIdentifier.h:37` /
//	    `IFolderIdentifier.h:110`）。`GetAttributes` と同じ構造体を受け取る。
//	  * 隣に `GetAttributesTimeDateReference` / `SetAttributesTimeDateReference`
//	    （`SAttributesDateTime` は年月日時分秒の Uint32 6 つ、`EAttributesTimeReference`
//	    は Created / LastAccessed / LastWritten / LastBackup）がある。**同じ
//	    「Set* の半分」**なので、ここが生きていれば「Set 系がまるごと未実装」では
//	    ないと言える——本命の対照になる。
//	  * `SAttributes` は SDK 同梱の実装ソースのどこからも使われていない（#87 で確認）。
//	    **ヘッダに答えは無く、実機で測るしかない。**
//
//	ここで確かめること:
//
//	  1) `SetAttributes` は何を返すか。成功を返すなら、`GetAttributes` で**読み戻せる**か。
//	     11 旗を**1 つずつ反転して書いては読み戻し**、どれが反映されるかを旗ごとに出す。
//	  2) `fbDirectory` に true を書いたらどうなるか（上の総当たりに含まれる）。
//	  3) 書けたように見えたとき、**ディスクは本当に変わったか**。読み戻しだけでは
//	     「SDK が覚えているだけ」と区別が付かないので、**POSIX の `stat` で権限
//	     ビット（`st_mode`）と BSD の旗（`st_flags`）を旗ごとに前後で比べ**、
//	     読み取り専用にしたファイルへ**実際に 1 バイト追記してみて拒まれるか**まで
//	     確かめる。これで「無視された旗」と「書かれたが読み戻しに出ない旗」も
//	     分かれる。
//	  4) 実在しない対象への `SetAttributes` は何を返すか。
//	  5) **「立つはずの標本」を OS 側で作って測る**（macOS）。`chmod` で権限を落とし、
//	     `chflags` で UF_HIDDEN / UF_IMMUTABLE / SF_ARCHIVED を立てて、
//	     `SAttributes` のどの旗が動くかを見る。**これが「fbHidden / fbArchive は
//	     何に対応しているのか」への一般的な答えになる**（dot ファイルで no だった、
//	     という #87 の観測より一段強い標本）。
//	  6) **システム領域の標本**（`/System/` / `/usr/bin/` / `/bin/ls` など）で
//	     `fbSystem` / `fbCanExecute` が立つか。
//
//	**利用者の操作は要らない**——ダイアログは 1 つも出ない。図面には触らない。
//	ディスクへ書くのは一時フォルダ 1 つ（とその中のファイル）だけで、測り終えたら消す。
//	読むだけの標本（`/System/` など）には一切書き込まない。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"
#include "Interfaces/VectorWorks/Filing/IFolderIdentifier.h"

#include <fstream>
#include <string>

#if GS_MAC
#	include <cerrno>
#	include <cstdio>
#	include <cstring>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

namespace
{
	using namespace VectorWorks::Filing;

	std::string Utf8Of(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string ErrWord(VCOMError err)
	{
		return std::to_string((long)err) + (VCOM_SUCCEEDED(err) ? "（成功）" : "（失敗）");
	}

	std::string YesNo(bool value)
	{
		return value ? "yes" : "no";
	}

	// -----------------------------------------------------------------------
	// SAttributes の 11 項目を「名前つきで 1 つずつ触れる」ようにした表。
	// 総当たり（1 つずつ反転して書く）も、差分の言語化も、ここを回して行う
	// ——**項目を 1 つも落とさない**ことがこの調査の肝なので、並びは 1 か所に持つ。
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

	// 数え上げ。**結論は「何回中何回」で言う**ので、ログを目で数えなくて済むように持つ。
	struct Tally
	{
		long setCalls = 0; // SetAttributes を呼んだ回数（旗 1 つの反転 1 回を 1 と数える）
		long setSucceeded = 0;	  // そのうち VCOM_SUCCEEDED が返った回数
		long readBackApplied = 0; // そのうち**読み戻しに反映されていた**回数
		std::string appliedNames; // 反映された旗の名前（重複可。空なら 1 つも無い）
		long osFlagCases = 0; // OS 側で状態を作って測った標本の数
		long osFlagMoved = 0; // そのうち SAttributes に差が出た標本の数
	};

	void AppendName(std::string& list, const std::string& name)
	{
		if (!list.empty())
			list += " ";
		list += name;
	}

#if GS_MAC
	// -----------------------------------------------------------------------
	// **ディスク側の見え方。** SAttributes を読み戻すだけでは「SDK が覚えている
	// だけ」と「本当にディスクが変わった」を区別できないので、POSIX の stat で
	// 権限ビット（st_mode）と BSD の旗（st_flags）を直接見る。
	std::string StatWord(const std::string& path)
	{
		struct stat info = {};
		if (::stat(path.c_str(), &info) != 0)
			return std::string("stat 失敗 errno=") + std::to_string((long)errno) + " " +
				   std::strerror(errno);

		char buffer[64] = {};
		std::snprintf(buffer, sizeof(buffer), "mode=%04o flags=0x%08x",
					  (unsigned)(info.st_mode & 07777), (unsigned)info.st_flags);
		return std::string(buffer);
	}

#endif

	// **本当に書けなくなったか。** 権限ビットが変わっていても、実際に拒まれるかは
	// 別の話なので、追記を 1 バイト試して確かめる（OS を問わない）。
	std::string TryAppendWord(const std::string& path)
	{
		std::ofstream out(path.c_str(), std::ios::binary | std::ios::app);
		if (!out.is_open())
			return std::string("拒まれた（開けない）");
		out << "x";
		out.flush();
		return out.good() ? std::string("できた") : std::string("拒まれた（書けない）");
	}

	// ディスク側の見え方を 1 行にする（OS を問わない口）。パスが空なら「見ない」。
	std::string DiskWord(const std::string& path)
	{
		if (path.empty())
			return std::string();
#if GS_MAC
		return StatWord(path);
#else
		return std::string("（この OS では見ていない）");
#endif
	}

	// 書く前後でディスク側が動いたかを 1 行書く。**動かなかったことも書く**
	// ——「無視された」と「書かれたが読み戻しに出ない」を分けるのがこの行の役目。
	void LogDiskChange(::vwprobe::Report& probe, const std::string& path, const std::string& before)
	{
		if (path.empty() || before.empty())
			return;
		const std::string after = DiskWord(path);
		probe.log("      ディスク: 前=" + before + " 後=" + after + " → " +
				  (before == after ? "変わらない" : "変わった"));
	}

	// -----------------------------------------------------------------------
	// 対象 1 つを測って 1 行書く（フォルダ用とファイル用で呼ぶ関数名が違うだけ）。
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
		return attrErr;
	}

	// -----------------------------------------------------------------------
	// **本命。** 11 旗を 1 つずつ反転して SetAttributes で書き、GetAttributes で
	// 読み戻して、旗ごとに「反映されたか」を出す。書いた後は必ず元の値へ戻す
	// （戻し切れなかったときは、その差もログへ出す——後片付けが効かない可能性まで
	//  含めて知見なので、黙って進まない）。
	//
	// IFolderIdentifier と IFileIdentifier は無関係な型だが、Get/SetAttributes の
	// 綴りが同じなのでテンプレートで両方を回せる。
	template <typename TIdent>
	void RoundTripFlags(::vwprobe::Report& probe, const std::string& who, TIdent* ident,
						Tally& tally, const std::string& diskPath)
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
		if (!VCOM_SUCCEEDED(baseErr))
		{
			probe.log("  初期値が読めないので往復は測れない");
			return;
		}

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

			const std::string diskBefore = DiskWord(diskPath);

			const VCOMError setErr = ident->SetAttributes(want);

			SAttributes back = {};
			const VCOMError getErr = ident->GetAttributes(back);
			const bool applied = VCOM_SUCCEEDED(getErr) && (back.*member == want.*member);

			probe.log("      SetAttributes=" + ErrWord(setErr) + " 読み戻し=" + ErrWord(getErr) +
					  " その旗=" + YesNo(back.*member) + " → " +
					  (applied ? "反映された" : "反映されない"));
			probe.log("      初期値との差: " + DiffWord(base, back));

			// **ディスクは本当に変わったか。** 読み戻しに出ない旗（fbHidden など）が
			// 「無視された」のか「書かれたが読み戻しに出ないだけ」なのかは、
			// ここを見ないと決まらない。
			LogDiskChange(probe, diskPath, diskBefore);

			++tally.setCalls;
			if (VCOM_SUCCEEDED(setErr))
				++tally.setSucceeded;
			if (applied)
			{
				++tally.readBackApplied;
				AppendName(tally.appliedNames, name);
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
		}

		// まとめて全部 true / 全部 false を書いたらどうなるか。1 つずつでは
		// 動かなくても「揃っていないと受け付けない」実装があり得るので、
		// **総当たりとは別に 1 回ずつ試す**。
		{
			SAttributes allTrue = {};
			SAttributes allFalse = {};
			for (size_t i = 0; i < kAttributeFlagCount; ++i)
			{
				allTrue.*kAttributeFlagTable[i].fMember = true;
				allFalse.*kAttributeFlagTable[i].fMember = false;
			}

			probe.log("  [まとめて全部 true] を書く");
			const VCOMError allTrueErr = ident->SetAttributes(allTrue);
			SAttributes afterAllTrue = {};
			ident->GetAttributes(afterAllTrue);
			probe.log("      SetAttributes=" + ErrWord(allTrueErr) +
					  " 読み戻し: " + AttrWord(afterAllTrue));
			probe.log("      初期値との差: " + DiffWord(base, afterAllTrue));

			probe.log("  [まとめて全部 false] を書く");
			const VCOMError allFalseErr = ident->SetAttributes(allFalse);
			SAttributes afterAllFalse = {};
			ident->GetAttributes(afterAllFalse);
			probe.log("      SetAttributes=" + ErrWord(allFalseErr) +
					  " 読み戻し: " + AttrWord(afterAllFalse));
			probe.log("      初期値との差: " + DiffWord(base, afterAllFalse));

			// 必ず初期値へ戻してから抜ける（この後この対象を消すため）。
			ident->SetAttributes(base);
			SAttributes now = {};
			if (VCOM_SUCCEEDED(ident->GetAttributes(now)))
				probe.log("  " + who + " を初期値へ戻した: " + DiffWord(base, now));
		}
	}

#if GS_MAC
	// -----------------------------------------------------------------------
	// OS 側で「立つはずの状態」を作って測る（macOS 固有）。SDK ではなく POSIX で
	// 状態を作るのは、**SAttributes の旗が OS の何に対応しているのか**を決めたいため
	// ——対応が分かれば「Windows ではどうか」も推測ではなく設計から言える。
	std::string PosixResult(int rc)
	{
		if (rc == 0)
			return "0（成功）";
		return std::to_string((long)rc) + "（失敗 errno=" + std::to_string((long)errno) + " " +
			   std::strerror(errno) + "）";
	}
#endif
} // namespace

VW_PROBE("sattributes-write", "SetAttributes で書き戻せるか・残りの旗はいつ立つか",
		 "一時フォルダとファイルを作り、SAttributes の 11 旗を 1 つずつ書いては読み戻す。"
		 "OS 側で権限・隠し属性を作った標本も測る（ダイアログは出ません）")
{
	Tally tally;

	// -----------------------------------------------------------------------
	// 0) 素材を作る。一時フォルダ 1 つと、その中のファイル 1 つ。
	probe.log("0) 素材（一時フォルダ＋その中のファイル）を作る");

	IFolderIdentifierPtr tempRoot(IID_FolderIdentifier);
	IFolderIdentifierPtr workFolder(IID_FolderIdentifier);
	IFileIdentifierPtr workFile(IID_FileIdentifier);
	std::string workFolderPath;
	std::string workFilePath;
	bool haveFolder = false;
	bool haveFile = false;

	{
		const VCOMError tempErr = tempRoot->Set(EOSFolderSpecifier::SystemTempDirectory);
		probe.log("  一時フォルダ: Set=" + ErrWord(tempErr));
		if (!VCOM_SUCCEEDED(tempErr))
		{
			probe.fail("一時フォルダを掴めなかった（この先は測れない）");
			return;
		}

		const VCOMError setErr = workFolder->Set(tempRoot, "VwSdkProbes-sattributes-write");
		const VCOMError createErr = VCOM_SUCCEEDED(setErr) ? workFolder->CreateOnDisk() : setErr;
		TXString folderPath;
		workFolder->GetFullPath(folderPath);
		workFolderPath = Utf8Of(folderPath);
		bool folderExists = false;
		workFolder->ExistsOnDisk(folderExists);
		haveFolder = VCOM_SUCCEEDED(createErr) && folderExists;
		probe.log("  作業フォルダ: Set=" + ErrWord(setErr) + " CreateOnDisk=" + ErrWord(createErr) +
				  " 実在=" + YesNo(folderExists) + " path=[" + workFolderPath + "]");

		if (!haveFolder)
		{
			probe.fail("作業フォルダを作れなかった（この先は測れない）");
			return;
		}

		// ファイルは SDK に「作る」口が無い（IFileIdentifier には CreateOnDisk が
		// 無く、DeleteOnDisk / RenameOnDisk / DuplicateOnDisk しかない）ので、
		// **素の C++ で 1 バイト書いて**作る。測るのは作った後の SDK 側の値。
		workFilePath = workFolderPath + "case.txt";
		{
			std::ofstream out(workFilePath.c_str(), std::ios::binary | std::ios::trunc);
			out << "vw-sdk-probe";
		}
		const VCOMError fileSetErr = workFile->Set(TXString(workFilePath.c_str()));
		bool fileExists = false;
		workFile->ExistsOnDisk(fileExists);
		haveFile = VCOM_SUCCEEDED(fileSetErr) && fileExists;
		probe.log("  作業ファイル: Set=" + ErrWord(fileSetErr) + " 実在=" + YesNo(fileExists) +
				  " path=[" + workFilePath + "]");
	}

	// -----------------------------------------------------------------------
	// 1) 本命その 1——フォルダの SetAttributes 往復。
	probe.log("1) SetAttributes の往復（フォルダ。11 旗を 1 つずつ反転して書く）");
	IFolderIdentifier* const workFolderRaw = workFolder;
	RoundTripFlags(probe, "作業フォルダ", workFolderRaw, tally, workFolderPath);

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
	//    ここが本命の詰め——`SetAttributes` が成功を返し、`GetAttributes` にも
	//    出たとして、それが「SDK が覚えているだけ」なら実務では使えない。
	//    **実際に 1 バイト追記してみて拒まれるか**で決める。
	probe.log("3) 読み取り専用にしたファイルへ、本当に書けなくなるか");
	if (haveFile)
	{
		SAttributes base = {};
		const VCOMError baseErr = workFile->GetAttributes(base);

		probe.log("  書く前: " + DiskWord(workFilePath) + " 追記=" + (TryAppendWord(workFilePath)));

		SAttributes readOnly = base;
		readOnly.fbReadOnly = true;
		readOnly.fbCanWrite = false;
		const VCOMError setErr = workFile->SetAttributes(readOnly);

		SAttributes back = {};
		workFile->GetAttributes(back);
		probe.log("  fbReadOnly=yes / fbCanWrite=no を書いた: SetAttributes=" + ErrWord(setErr) +
				  " 読み戻し fbReadOnly=" + YesNo(back.fbReadOnly) +
				  " fbCanWrite=" + YesNo(back.fbCanWrite));
		probe.log("  書いた後: " + DiskWord(workFilePath) +
				  " 追記=" + (TryAppendWord(workFilePath)));

		// 元へ戻して、書けるように戻るかまで見る（戻らなければ後片付けが効かない）。
		if (VCOM_SUCCEEDED(baseErr))
			workFile->SetAttributes(base);
		probe.log("  初期値へ戻した後: " + DiskWord(workFilePath) +
				  " 追記=" + (TryAppendWord(workFilePath)));
	}
	else
	{
		probe.log("  作業ファイルを作れなかったので飛ばす");
	}

	// -----------------------------------------------------------------------
	// 3b) 同じことをフォルダでも（子を作れるかで見る）。
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
				  " fbCanWrite=" + YesNo(back.fbCanWrite));

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

		// 元へ戻す（この後このフォルダを消すため）。
		if (VCOM_SUCCEEDED(baseErr))
			workFolder->SetAttributes(base);
	}

	// -----------------------------------------------------------------------
	// 4) 実在しない対象への SetAttributes は何を返すか。
	//    （GetAttributes はフォルダ 51 / ファイル 1 を返すと #87 で測ってある）
	probe.log("4) 実在しない対象への SetAttributes");
	{
		SAttributes attributes = {};

		IFolderIdentifierPtr ghostFolder(IID_FolderIdentifier);
		if (VCOM_SUCCEEDED(ghostFolder->Set(tempRoot, "VwSdkProbes-no-such-folder-91")))
		{
			const VCOMError getErr = ghostFolder->GetAttributes(attributes);
			const VCOMError setErr = ghostFolder->SetAttributes(attributes);
			probe.log("  実在しないフォルダ: GetAttributes=" + ErrWord(getErr) +
					  " SetAttributes=" + ErrWord(setErr));
		}

		IFileIdentifierPtr ghostFile(IID_FileIdentifier);
		if (VCOM_SUCCEEDED(ghostFile->Set(tempRoot, "VwSdkProbes-no-such-file-91.txt")))
		{
			const VCOMError getErr = ghostFile->GetAttributes(attributes);
			const VCOMError setErr = ghostFile->SetAttributes(attributes);
			probe.log("  実在しないファイル: GetAttributes=" + ErrWord(getErr) +
					  " SetAttributes=" + ErrWord(setErr));
		}
	}

	// -----------------------------------------------------------------------
	// 5) **対照**——同じ識別子の「Set* の半分」である
	//    SetAttributesTimeDateReference は書き戻せるか。
	//    ここが生きていれば「Set 系がまるごと未実装」ではないと言い切れる。
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

			// 2001-02-03 04:05:06 を書いて、そのまま読み戻せるかを見る。
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
	// 6) **「立つはずの標本」を OS 側で作って測る**（macOS）。
	//    #87 では「dot ファイルで fbHidden=no」までしか言えていない。ここでは
	//    macOS が本当に「隠し」と扱う UF_HIDDEN を立てた標本で測るので、
	//    「fbHidden は何にも対応していない」と言い切れるところまで行ける。
	probe.log("6) OS 側で状態を作った標本（macOS の chmod / chflags）");
#if GS_MAC
	if (haveFile)
	{
		SAttributes baseline = {};
		const VCOMError baselineErr = workFile->GetAttributes(baseline);
		probe.log("  基準（0644・旗なし）: GetAttributes=" + ErrWord(baselineErr) + " " +
				  AttrWord(baseline));

		struct SPosixCase
		{
			const char* fName;
			int fMode;			  // chmod する権限（-1 なら触らない）
			unsigned long fFlags; // chflags する旗
			const char* fWhy;
		};
		const SPosixCase posixCases[] = {
			{"chmod 0444（読み取り専用）", 0444, 0, "fbReadOnly / fbCanWrite が動くはず"},
			{"chmod 0000（読めない）", 0000, 0, "fbCanRead が動くはず"},
			{"chmod 0755（実行できる）", 0755, 0, "fbCanExecute が動くはず"},
			{"chflags UF_HIDDEN（隠し）", 0644, UF_HIDDEN, "fbHidden が動くはず"},
			{"chflags UF_IMMUTABLE（変更不可）", 0644, UF_IMMUTABLE,
			 "fbReadOnly が動くかもしれない"},
			{"chflags SF_ARCHIVED（書庫）", 0644, SF_ARCHIVED,
			 "fbArchive が動くはず（root でないと立てられない見込み）"},
		};

		for (size_t i = 0; i < sizeof(posixCases) / sizeof(posixCases[0]); ++i)
		{
			// 状態を作る。**作れたかどうか（errno）も必ず出す**——立てられなかった
			// 旗について「SAttributes が写さなかった」と読み違えないため。
			const int chmodRc = ::chmod(workFilePath.c_str(), (mode_t)posixCases[i].fMode);
			const int chflagsRc =
				::chflags(workFilePath.c_str(), (unsigned int)posixCases[i].fFlags);
			probe.log(std::string("  ") + posixCases[i].fName + "（" + posixCases[i].fWhy +
					  "）: chmod=" + PosixResult(chmodRc) + " chflags=" + PosixResult(chflagsRc));

			SAttributes measured = {};
			const VCOMError measuredErr = workFile->GetAttributes(measured);
			probe.log("      GetAttributes=" + ErrWord(measuredErr) + " " + AttrWord(measured));
			const std::string diff = VCOM_SUCCEEDED(measuredErr) && VCOM_SUCCEEDED(baselineErr)
										 ? DiffWord(baseline, measured)
										 : std::string("（測れない）");
			probe.log("      基準との差: " + diff);

			++tally.osFlagCases;
			if (diff != "（差なし）" && diff != "（測れない）")
				++tally.osFlagMoved;

			// すぐ元へ戻す（UF_IMMUTABLE を立てたまま進むと消せなくなる）。
			::chflags(workFilePath.c_str(), 0);
			::chmod(workFilePath.c_str(), (mode_t)0644);
		}

		// 隠しフォルダ（先頭が . のフォルダ）でも fbHidden が動かないかを 1 件。
		IFolderIdentifierPtr dotFolder(IID_FolderIdentifier);
		if (VCOM_SUCCEEDED(dotFolder->Set(workFolder, ".hidden-by-name")))
		{
			const VCOMError createErr = dotFolder->CreateOnDisk();
			::chflags((workFolderPath + ".hidden-by-name").c_str(), UF_HIDDEN);
			probe.log("  先頭が . のフォルダ＋UF_HIDDEN: CreateOnDisk=" + ErrWord(createErr));
			LogFolder(probe, "    隠しフォルダ", dotFolder);
			::chflags((workFolderPath + ".hidden-by-name").c_str(), 0);
			dotFolder->DeleteOnDisk();
		}
	}
	else
	{
		probe.log("  作業ファイルを作れなかったので飛ばす");
	}
#else
	probe.log("  この節は macOS でのみ測る（Windows では別の作り方が要る）");
#endif

	// -----------------------------------------------------------------------
	// 7) システム領域の標本。fbSystem / fbCanExecute が立つ場所があるか。
	//    **読むだけ**（1 バイトも書かない）。macOS のパスなので、他の OS では
	//    「実在しない」として失敗が並ぶ——それも含めてログに残る。
	probe.log("7) システム領域の標本（読むだけ。fbSystem / fbCanExecute 狙い）");
	{
		const char* systemFolders[] = {
			"/System/", "/System/Library/", "/usr/bin/", "/private/var/db/", "/Library/",
		};
		for (size_t i = 0; i < sizeof(systemFolders) / sizeof(systemFolders[0]); ++i)
		{
			IFolderIdentifierPtr folder(IID_FolderIdentifier);
			if (VCOM_SUCCEEDED(folder->Set(TXString(systemFolders[i]))))
				LogFolder(probe, std::string("  ") + systemFolders[i], folder);
			else
				probe.log(std::string("  ") + systemFolders[i] + ": Set が失敗した");
		}

		const char* systemFiles[] = {
			"/bin/ls",
			"/System/Library/CoreServices/SystemVersion.plist",
			"/usr/bin/true",
		};
		for (size_t i = 0; i < sizeof(systemFiles) / sizeof(systemFiles[0]); ++i)
		{
			IFileIdentifierPtr file(IID_FileIdentifier);
			if (VCOM_SUCCEEDED(file->Set(TXString(systemFiles[i]))))
				LogFile(probe, std::string("  ") + systemFiles[i], file);
			else
				probe.log(std::string("  ") + systemFiles[i] + ": Set が失敗した");
		}
	}

	// -----------------------------------------------------------------------
	// 8) 後片付け。作ったものは全部消す。
	probe.log("8) 後片付け");
	{
		if (haveFile)
		{
			const VCOMError delErr = workFile->DeleteOnDisk();
			bool stillThere = false;
			workFile->ExistsOnDisk(stillThere);
			probe.log("  作業ファイル: DeleteOnDisk=" + ErrWord(delErr) +
					  " まだ残っている=" + YesNo(stillThere));
		}
		const VCOMError delErr = workFolder->DeleteOnDisk();
		bool stillThere = false;
		workFolder->ExistsOnDisk(stillThere);
		probe.log("  作業フォルダ: DeleteOnDisk=" + ErrWord(delErr) +
				  " まだ残っている=" + YesNo(stillThere));
	}

	// -----------------------------------------------------------------------
	// 9) 数え上げと**結論の 1 行**。ログを目で追わなくても答えが読めるようにする。
	probe.log("9) 数え上げ");
	probe.log("  SetAttributes（旗 1 つの反転）: 呼んだ=" + std::to_string(tally.setCalls) +
			  " 成功が返った=" + std::to_string(tally.setSucceeded) +
			  " 読み戻しに反映された=" + std::to_string(tally.readBackApplied));
	probe.log("  反映された旗: " +
			  (tally.appliedNames.empty() ? std::string("（1 つも無い）") : tally.appliedNames));
	probe.log("  OS 側で状態を作った標本: " + std::to_string(tally.osFlagCases) +
			  " 件 / そのうち SAttributes に差が出た標本=" + std::to_string(tally.osFlagMoved) +
			  " 件");

	if (tally.setCalls == 0)
		probe.log("  結論: SetAttributes を 1 度も呼べなかった（素材を作れていない）");
	else if (tally.setSucceeded == 0)
		probe.log("  結論: SetAttributes は**一度も成功を返さなかった**（書き戻す口は無い）");
	else if (tally.readBackApplied == 0)
		probe.log("  結論: SetAttributes は**成功を返すが、読み戻しに 1 つも反映されない**"
				  "（＝呼んでも何も起きない）");
	else
		probe.log(
			"  結論: SetAttributes で**反映された旗がある**（上の「反映された旗」の一覧が答え）");

	if (tally.setCalls == 0)
		probe.fail("SetAttributes を 1 度も呼べなかった");
}
