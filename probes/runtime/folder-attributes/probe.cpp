//
//	probes/runtime/folder-attributes/probe.cpp
//
//	[issue #87] `IFolderIdentifier::GetAttributes(SAttributes&)` の `fbDirectory` が、
//	**実在するフォルダに対しても false になるのか**を実機で確かめる。
//
//	なぜ確かめるか（[PR #86](../../../probes/runtime/) の拾い物）:
//	フォルダ選択ダイアログが返したフォルダと、`IFileIdentifier::GetFolder` が返した
//	親フォルダ——2 つとも `GetAttributes` は成功（VCOMError=0）を返しながら
//	`fbDirectory=no` だった。ただし測ったのは**どちらも Google ドライブの共有ドライブ
//	配下**（`~/Library/CloudStorage/GoogleDrive-…`）で、**ローカルディスク上の普通の
//	フォルダでは試していない**。「どのフォルダでも false」なのか「クラウドストレージ
//	配下だけの話」なのかが決まらないと、Findings に測った場所を断らずに書けない。
//
//	ヘッダで既に分かっていること（sdk-grep / sdk-ls）:
//
//	  * `SAttributes` は bool 11 個——fbReadOnly / fbHidden / fbSystem / fbTemporary /
//	    fbEncrypted / fbArchive / fbDirectory / fbCanRead / fbCanWrite / fbCanExecute /
//	    fbCanBrowse（`Interfaces/VectorWorks/Filing/IFolderIdentifier.h`）。
//	    `IFileIdentifier` も**同じ構造体**を `GetAttributes` で返す。
//	  * `SAttributes` は SDK 同梱のソース（`SDKLib/Source`）のどこからも使われて
//	    いない——**宣言が 2 つのヘッダにあるだけ**で、詰めるのは Vectorworks 本体側。
//	    つまりヘッダ側に答えは無く、実機で測るしかない。
//	  * ダイアログを出さずにフォルダを掴む口が 2 系統ある:
//	    `Set(EOSFolderSpecifier)`（VW2021 以降。ApplicationsDirectory /
//	    DocumentDirectory / ApplicationSupportDirectory / SystemTempDirectory の 4 値）と
//	    `Set(EFolderSpecifier, bool bUserFolder[, subFolder])`（VW の導入先基準）。
//	  * `CreateOnDisk()` / `DeleteOnDisk()` があるので、**素のローカルフォルダを
//	    その場で作って測る**ことができる（同期も何も絡まない、いちばん素朴な標本）。
//	  * `EnumerateContents(IFolderContentListener*, bool)` は、フォルダを
//	    `OnFolderContent`、ファイルを `OnFileContent` へ**SDK 自身が振り分けて**渡す。
//	    ここで受けたものを測れば、「SDK がフォルダだと知っている対象」と
//	    「SDK がファイルだと知っている対象」を**同じ実行の中で**並べられる。
//
//	ここで確かめること:
//
//	  1) ローカルディスク上の普通のフォルダで `fbDirectory` は true になるか。
//	     （掴み方を 4 通り——OS 標準フォルダ / VW 導入先 / その場で作った新品 /
//	      絶対パス文字列——に散らして、**掴み方で変わらないか**も同時に見る）
//	  2) ファイル（`IFileIdentifier`）の `fbDirectory` は何を返すか。
//	     フォルダと同じ値なら `fbDirectory` は種類の判定に一切使えない。
//	  3) `SAttributes` の他の 10 項目は埋まっているか（全部 false なら
//	     「GetAttributes は成功を返すが中身を埋めていない」と言える）。
//	  4) クラウドストレージ配下（`~/Library/CloudStorage`）とローカルで差が出るか。
//	     出なければ #86 の観測は場所固有ではない。
//	  5) 実在しない場所に対して `GetAttributes` は失敗を返すか。
//	     （成功を返すなら、戻り値も実在の判定には使えない）
//
//	**利用者の操作は要らない**——ダイアログは 1 つも出ない。新規の空図面で走らせて
//	もらうが、図面には何も触らない（ディスクへ書くのは一時フォルダ 1 つだけで、
//	測り終えたら消す）。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"
#include "Interfaces/VectorWorks/Filing/IFolderIdentifier.h"

#include <string>

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

	// SAttributes の **11 項目を 1 つも省かずに** 1 行へ。省くと「測ったが載せなかった」
	// のか「埋まっていなかった」のかが後から区別できない。
	std::string AttrWord(const SAttributes& a)
	{
		return std::string("fbDirectory=") + YesNo(a.fbDirectory) +
			   " fbReadOnly=" + YesNo(a.fbReadOnly) + " fbHidden=" + YesNo(a.fbHidden) +
			   " fbSystem=" + YesNo(a.fbSystem) + " fbTemporary=" + YesNo(a.fbTemporary) +
			   " fbEncrypted=" + YesNo(a.fbEncrypted) + " fbArchive=" + YesNo(a.fbArchive) +
			   " fbCanRead=" + YesNo(a.fbCanRead) + " fbCanWrite=" + YesNo(a.fbCanWrite) +
			   " fbCanExecute=" + YesNo(a.fbCanExecute) + " fbCanBrowse=" + YesNo(a.fbCanBrowse);
	}

	// 測った標本の数え上げ。**結論は「何件中何件が true だったか」で言う**ので、
	// ログを目で数えなくて済むようにここで持つ。
	struct Tally
	{
		long folderMeasured = 0;   // GetAttributes が成功したフォルダの件数
		long folderDirYes = 0;	   // そのうち fbDirectory=yes だった件数
		long folderAttrFailed = 0; // GetAttributes 自体が失敗した件数
		long fileMeasured = 0;
		long fileDirYes = 0;
		long fileAttrFailed = 0;
		long anyOtherFlagYes = 0; // fbDirectory 以外で 1 つでも true が立った標本の件数
	};

	bool AnyOtherFlag(const SAttributes& a)
	{
		return a.fbReadOnly || a.fbHidden || a.fbSystem || a.fbTemporary || a.fbEncrypted ||
			   a.fbArchive || a.fbCanRead || a.fbCanWrite || a.fbCanExecute || a.fbCanBrowse;
	}

	// フォルダ 1 つを測って 2 行書く（1 行目＝何を測ったか、2 行目＝測った値）。
	// outFullPath には絶対パスを返す（後段で掴み直すのに使う）。
	void MeasureFolder(::vwprobe::Report& probe, const std::string& label,
					   IFolderIdentifier* folder, Tally& tally, std::string* outFullPath = nullptr)
	{
		if (outFullPath != nullptr)
			outFullPath->clear();

		if (folder == nullptr)
		{
			probe.log(label + ": ポインタが nullptr（測れない）");
			return;
		}

		TXString fullPath;
		const VCOMError pathErr = folder->GetFullPath(fullPath);
		const std::string path = Utf8Of(fullPath);
		if (outFullPath != nullptr)
			*outFullPath = path;

		bool exists = false;
		const VCOMError existsErr = folder->ExistsOnDisk(exists);

		probe.log(label + ": path=[" + path + "] GetFullPath=" + ErrWord(pathErr) +
				  " ExistsOnDisk=" + ErrWord(existsErr) + " 実在=" + YesNo(exists));

		SAttributes attributes = {};
		const VCOMError attrErr = folder->GetAttributes(attributes);
		probe.log("    GetAttributes=" + ErrWord(attrErr) + " " + AttrWord(attributes));

		if (VCOM_SUCCEEDED(attrErr))
		{
			++tally.folderMeasured;
			if (attributes.fbDirectory)
				++tally.folderDirYes;
			if (AnyOtherFlag(attributes))
				++tally.anyOtherFlagYes;
		}
		else
		{
			++tally.folderAttrFailed;
		}
	}

	// ファイル 1 つを測って 2 行書く。フォルダ側と**同じ並び**で出す（見比べるため）。
	void MeasureFile(::vwprobe::Report& probe, const std::string& label, IFileIdentifier* file,
					 Tally& tally, std::string* outFullPath = nullptr)
	{
		if (outFullPath != nullptr)
			outFullPath->clear();

		if (file == nullptr)
		{
			probe.log(label + ": ポインタが nullptr（測れない）");
			return;
		}

		TXString fullPath;
		const VCOMError pathErr = file->GetFileFullPath(fullPath);
		const std::string path = Utf8Of(fullPath);
		if (outFullPath != nullptr)
			*outFullPath = path;

		bool exists = false;
		const VCOMError existsErr = file->ExistsOnDisk(exists);

		probe.log(label + ": path=[" + path + "] GetFileFullPath=" + ErrWord(pathErr) +
				  " ExistsOnDisk=" + ErrWord(existsErr) + " 実在=" + YesNo(exists));

		SAttributes attributes = {};
		const VCOMError attrErr = file->GetAttributes(attributes);
		probe.log("    GetAttributes=" + ErrWord(attrErr) + " " + AttrWord(attributes));

		if (VCOM_SUCCEEDED(attrErr))
		{
			++tally.fileMeasured;
			if (attributes.fbDirectory)
				++tally.fileDirYes;
			if (AnyOtherFlag(attributes))
				++tally.anyOtherFlagYes;
		}
		else
		{
			++tally.fileAttrFailed;
		}
	}

	// EnumerateContents の受け口。**SDK 自身がフォルダ／ファイルへ振り分けたもの**を
	// そのまま測る——「こちらがフォルダだと思っている」ではなく「SDK がフォルダだと
	// 知っている」対象だけを数えられるのが、この経路を使う理由。
	class CAttributeListener : public IFolderContentListener
	{
	public:
		CAttributeListener(::vwprobe::Report& probe, Tally& tally, long logLimit, long visitLimit)
			: fProbe(probe), fTally(tally), fLogLimit(logLimit), fVisitLimit(visitLimit)
		{
		}

		long FolderSeen() const
		{
			return fFolderSeen;
		}
		long FileSeen() const
		{
			return fFileSeen;
		}
		const std::string& FirstFolderPath() const
		{
			return fFirstFolderPath;
		}
		const std::string& FirstFilePath() const
		{
			return fFirstFilePath;
		}

	public:
		virtual EFolderContentListenerResult VCOM_CALLTYPE
		OnFolderContent(IFolderIdentifier* pFolderID)
		{
			++fFolderSeen;

			TXString name;
			if (pFolderID != nullptr)
				pFolderID->GetName(name);

			std::string path;
			if (fFolderSeen <= fLogLimit)
			{
				MeasureFolder(fProbe,
							  "  [フォルダ " + std::to_string(fFolderSeen) + "] " + Utf8Of(name),
							  pFolderID, fTally, &path);
			}
			else if (pFolderID != nullptr)
			{
				// ログには出さないが数えは続ける（標本を増やすため）。
				SAttributes attributes = {};
				const VCOMError attrErr = pFolderID->GetAttributes(attributes);
				if (VCOM_SUCCEEDED(attrErr))
				{
					++fTally.folderMeasured;
					if (attributes.fbDirectory)
						++fTally.folderDirYes;
					if (AnyOtherFlag(attributes))
						++fTally.anyOtherFlagYes;
				}
				else
				{
					++fTally.folderAttrFailed;
				}
				TXString fullPath;
				pFolderID->GetFullPath(fullPath);
				path = Utf8Of(fullPath);
			}

			if (fFirstFolderPath.empty())
				fFirstFolderPath = path;

			return Verdict();
		}

		virtual EFolderContentListenerResult VCOM_CALLTYPE OnFileContent(IFileIdentifier* pFileID)
		{
			++fFileSeen;

			TXString name;
			if (pFileID != nullptr)
				pFileID->GetFileName(name);

			std::string path;
			if (fFileSeen <= fLogLimit)
			{
				MeasureFile(fProbe,
							"  [ファイル " + std::to_string(fFileSeen) + "] " + Utf8Of(name),
							pFileID, fTally, &path);
			}
			else if (pFileID != nullptr)
			{
				SAttributes attributes = {};
				const VCOMError attrErr = pFileID->GetAttributes(attributes);
				if (VCOM_SUCCEEDED(attrErr))
				{
					++fTally.fileMeasured;
					if (attributes.fbDirectory)
						++fTally.fileDirYes;
					if (AnyOtherFlag(attributes))
						++fTally.anyOtherFlagYes;
				}
				else
				{
					++fTally.fileAttrFailed;
				}
				TXString fullPath;
				pFileID->GetFileFullPath(fullPath);
				path = Utf8Of(fullPath);
			}

			if (fFirstFilePath.empty())
				fFirstFilePath = path;

			return Verdict();
		}

	private:
		EFolderContentListenerResult Verdict() const
		{
			// 際限なく歩かない。標本さえ足りれば止める（巨大なフォルダで固まらないため）。
			if (fFolderSeen + fFileSeen >= fVisitLimit)
				return eFolderContentListenerResult_StopNoError;
			return eFolderContentListenerResult_Continue;
		}

		::vwprobe::Report& fProbe;
		Tally& fTally;
		long fLogLimit = 0;
		long fVisitLimit = 0;
		long fFolderSeen = 0;
		long fFileSeen = 0;
		std::string fFirstFolderPath;
		std::string fFirstFilePath;
	};
} // namespace

VW_PROBE("folder-attributes", "GetAttributes の fbDirectory を実機で測る",
		 "ローカルのフォルダ・ファイル・クラウドストレージ配下を並べて、"
		 "SAttributes の 11 項目が何で埋まるかを確かめる（ダイアログは出ません）")
{
	Tally tally;

	// -----------------------------------------------------------------------
	// 1) OS 標準のフォルダ 4 つ（EOSFolderSpecifier のすべての値）。
	//    **ダイアログを通さずに掴んだ、ローカルディスク上の普通のフォルダ**。
	probe.log("1) OS 標準のフォルダ（Set(EOSFolderSpecifier)）");
	{
		struct SOSCase
		{
			EOSFolderSpecifier spec;
			const char* label;
		};
		const SOSCase osCases[] = {
			{EOSFolderSpecifier::ApplicationsDirectory, "  ApplicationsDirectory"},
			{EOSFolderSpecifier::DocumentDirectory, "  DocumentDirectory"},
			{EOSFolderSpecifier::ApplicationSupportDirectory, "  ApplicationSupportDirectory"},
			{EOSFolderSpecifier::SystemTempDirectory, "  SystemTempDirectory"},
		};

		for (size_t i = 0; i < sizeof(osCases) / sizeof(osCases[0]); ++i)
		{
			IFolderIdentifierPtr folder(IID_FolderIdentifier);
			if (folder == nullptr)
			{
				probe.fail("IFolderIdentifier を確保できなかった");
				return;
			}
			const VCOMError setErr = folder->Set(osCases[i].spec);
			probe.log(std::string(osCases[i].label) + ": Set=" + ErrWord(setErr));
			if (VCOM_SUCCEEDED(setErr))
				MeasureFolder(probe, std::string(osCases[i].label), folder, tally);
		}
	}

	// -----------------------------------------------------------------------
	// 2) VW の導入先基準のフォルダ（EFolderSpecifier）。掴み方が変われば答えも
	//    変わるのか——変わらなければ「掴み方の問題ではない」と言える。
	probe.log("2) VW の導入先基準のフォルダ（Set(EFolderSpecifier, bUserFolder)）");
	{
		struct SVWCase
		{
			EFolderSpecifier spec;
			bool userFolder;
			const char* label;
		};
		const SVWCase vwCases[] = {
			{kApplicationFolder, false, "  kApplicationFolder(false)"},
			{kExternalsFolder, false, "  kExternalsFolder(false)"},
			{kAppDataFolder, true, "  kAppDataFolder(true)"},
		};

		for (size_t i = 0; i < sizeof(vwCases) / sizeof(vwCases[0]); ++i)
		{
			IFolderIdentifierPtr folder(IID_FolderIdentifier);
			const VCOMError setErr = folder->Set(vwCases[i].spec, vwCases[i].userFolder);
			probe.log(std::string(vwCases[i].label) + ": Set=" + ErrWord(setErr));
			if (VCOM_SUCCEEDED(setErr))
				MeasureFolder(probe, std::string(vwCases[i].label), folder, tally);
		}
	}

	// -----------------------------------------------------------------------
	// 3) **その場で作った新品のローカルフォルダ。** 同期も特別扱いも絡まない、
	//    いちばん素朴な標本。ここで fbDirectory=no なら「どのフォルダでも no」に
	//    ぐっと近づく（逆に yes なら #86 の観測は場所固有だったことになる）。
	probe.log("3) その場で作る新品のローカルフォルダ（一時フォルダの下）");
	std::string madePath;
	{
		IFolderIdentifierPtr tempRoot(IID_FolderIdentifier);
		const VCOMError tempErr = tempRoot->Set(EOSFolderSpecifier::SystemTempDirectory);
		probe.log("  一時フォルダ: Set=" + ErrWord(tempErr));

		if (VCOM_SUCCEEDED(tempErr))
		{
			IFolderIdentifierPtr made(IID_FolderIdentifier);
			const VCOMError madeSetErr = made->Set(tempRoot, "VwSdkProbes-folder-attributes");
			probe.log("  新品: Set(親, 名前)=" + ErrWord(madeSetErr));

			if (VCOM_SUCCEEDED(madeSetErr))
			{
				// 作る前に測る——**実在しないフォルダに対して GetAttributes が
				// 何を返すか**が、ここで一緒に分かる（下の 6 でも改めて確かめる）。
				MeasureFolder(probe, "  新品（作る前）", made, tally, &madePath);

				const VCOMError createErr = made->CreateOnDisk();
				probe.log("  CreateOnDisk=" + ErrWord(createErr));
				MeasureFolder(probe, "  新品（作った後）", made, tally, &madePath);

				// 同じ場所を**絶対パス文字列から掴み直して**測る。掴み方の違いで
				// 答えが変わらないかの確認。
				if (!madePath.empty())
				{
					IFolderIdentifierPtr byPath(IID_FolderIdentifier);
					const VCOMError byPathErr = byPath->Set(TXString(madePath.c_str()));
					probe.log("  新品（絶対パスで掴み直し）: Set=" + ErrWord(byPathErr));
					if (VCOM_SUCCEEDED(byPathErr))
						MeasureFolder(probe, "  新品（絶対パスで掴み直し）", byPath, tally);
				}

				const VCOMError deleteErr = made->DeleteOnDisk();
				probe.log("  DeleteOnDisk=" + ErrWord(deleteErr) + "（後片付け）");
			}
		}
	}

	// -----------------------------------------------------------------------
	// 4) **SDK 自身がフォルダ／ファイルへ振り分けたもの**を並べて測る（決定打）。
	//    同じ親フォルダの中身なので、fbDirectory がフォルダとファイルを
	//    区別できるかどうかが、そのまま 1 か所で見える。
	probe.log("4) VW の導入先を列挙して、フォルダとファイルを並べて測る");
	std::string enumeratedFolderPath;
	std::string enumeratedFilePath;
	{
		IFolderIdentifierPtr appFolder(IID_FolderIdentifier);
		const VCOMError setErr = appFolder->Set(kApplicationFolder, false);
		probe.log("  列挙する親: Set=" + ErrWord(setErr));

		if (VCOM_SUCCEEDED(setErr))
		{
			TXString parentPath;
			appFolder->GetFullPath(parentPath);
			probe.log("  列挙する親: path=[" + Utf8Of(parentPath) + "]");

			CAttributeListener listener(probe, tally, /*logLimit=*/4, /*visitLimit=*/120);
			const VCOMError enumErr = appFolder->EnumerateContents(&listener, false);
			probe.log("  EnumerateContents=" + ErrWord(enumErr) +
					  " 見たフォルダ=" + std::to_string(listener.FolderSeen()) +
					  " 見たファイル=" + std::to_string(listener.FileSeen()) +
					  "（ログに出したのは各先頭 4 件。残りも数えには入っている）");
			enumeratedFolderPath = listener.FirstFolderPath();
			enumeratedFilePath = listener.FirstFilePath();
		}
	}

	// -----------------------------------------------------------------------
	// 5) 列挙で見つけた**同じ対象**を、絶対パスから自分で掴み直して測る。
	//    列挙が配ったポインタと、自分で作った識別子とで答えが違えば、
	//    「掴み方で変わる」という別の話になる。
	probe.log("5) 列挙で見つけた対象を、絶対パスから掴み直して測る");
	if (!enumeratedFolderPath.empty())
	{
		IFolderIdentifierPtr folder(IID_FolderIdentifier);
		const VCOMError setErr = folder->Set(TXString(enumeratedFolderPath.c_str()));
		probe.log("  フォルダ: Set(絶対パス)=" + ErrWord(setErr));
		if (VCOM_SUCCEEDED(setErr))
			MeasureFolder(probe, "  フォルダ（掴み直し）", folder, tally);
	}
	else
	{
		probe.log("  フォルダ: 列挙でフォルダを 1 つも見つけられなかった");
	}

	if (!enumeratedFilePath.empty())
	{
		IFileIdentifierPtr file(IID_FileIdentifier);
		const VCOMError setErr = file->Set(TXString(enumeratedFilePath.c_str()));
		probe.log("  ファイル: Set(絶対パス)=" + ErrWord(setErr));
		if (VCOM_SUCCEEDED(setErr))
			MeasureFile(probe, "  ファイル（掴み直し）", file, tally);
	}
	else
	{
		probe.log("  ファイル: 列挙でファイルを 1 つも見つけられなかった");
	}

	// -----------------------------------------------------------------------
	// 6) 実在しない場所。GetAttributes の**戻り値**が実在の判定に使えるか。
	probe.log("6) 実在しない場所に対する GetAttributes の戻り値");
	{
		IFolderIdentifierPtr ghostFolder(IID_FolderIdentifier);
		IFolderIdentifierPtr tempRoot(IID_FolderIdentifier);
		if (VCOM_SUCCEEDED(tempRoot->Set(EOSFolderSpecifier::SystemTempDirectory)) &&
			VCOM_SUCCEEDED(ghostFolder->Set(tempRoot, "VwSdkProbes-no-such-folder-87")))
		{
			MeasureFolder(probe, "  実在しないフォルダ", ghostFolder, tally);
		}

		IFileIdentifierPtr ghostFile(IID_FileIdentifier);
		IFolderIdentifierPtr tempRoot2(IID_FolderIdentifier);
		if (VCOM_SUCCEEDED(tempRoot2->Set(EOSFolderSpecifier::SystemTempDirectory)) &&
			VCOM_SUCCEEDED(ghostFile->Set(tempRoot2, "VwSdkProbes-no-such-file-87.txt")))
		{
			MeasureFile(probe, "  実在しないファイル", ghostFile, tally);
		}
	}

	// -----------------------------------------------------------------------
	// 7) クラウドストレージ配下（`~/Library/CloudStorage`）。#86 で測ったのと
	//    同じ種類の場所を、**同じ実行の中で**ローカルと並べる。
	//    ホームは DocumentDirectory の親から採る（利用者名を埋め込まないため）。
	probe.log("7) クラウドストレージ配下（~/Library/CloudStorage）とローカルの対比");
	{
		IFolderIdentifierPtr documents(IID_FolderIdentifier);
		IFolderIdentifierPtr home;
		bool haveHome = false;
		if (VCOM_SUCCEEDED(documents->Set(EOSFolderSpecifier::DocumentDirectory)))
			haveHome = VCOM_SUCCEEDED(documents->GetParentFolder(&home)) && home != nullptr;

		if (!haveHome)
		{
			probe.log("  ホームフォルダを採れなかった（この節は飛ばす）");
		}
		else
		{
			IFolderIdentifierPtr library(IID_FolderIdentifier);
			IFolderIdentifierPtr cloud(IID_FolderIdentifier);
			bool haveCloud = VCOM_SUCCEEDED(library->Set(home, "Library")) &&
							 VCOM_SUCCEEDED(cloud->Set(library, "CloudStorage"));

			bool cloudExists = false;
			if (haveCloud)
				cloud->ExistsOnDisk(cloudExists);

			if (!haveCloud || !cloudExists)
			{
				probe.log("  ~/Library/CloudStorage が無い環境（この節は飛ばす。"
						  "ローカルだけの結果として読むこと）");
			}
			else
			{
				MeasureFolder(probe, "  CloudStorage 自体", cloud, tally);

				// 中を 1 階層だけ列挙する（GoogleDrive-… などの同期先が並ぶ）。
				CAttributeListener listener(probe, tally, /*logLimit=*/3, /*visitLimit=*/12);
				const VCOMError enumErr = cloud->EnumerateContents(&listener, false);
				probe.log("  EnumerateContents=" + ErrWord(enumErr) +
						  " 見たフォルダ=" + std::to_string(listener.FolderSeen()) +
						  " 見たファイル=" + std::to_string(listener.FileSeen()));
			}
		}
	}

	// -----------------------------------------------------------------------
	// 8) 数え上げ。**ログを目で数えなくても結論が読める 1 行**を残す。
	probe.log("8) 数え上げ（GetAttributes が成功した標本だけを数えている）");
	probe.log("  フォルダ: 測れた=" + std::to_string(tally.folderMeasured) +
			  " そのうち fbDirectory=yes は " + std::to_string(tally.folderDirYes) +
			  " 件 / GetAttributes 自体が失敗=" + std::to_string(tally.folderAttrFailed) + " 件");
	probe.log("  ファイル: 測れた=" + std::to_string(tally.fileMeasured) +
			  " そのうち fbDirectory=yes は " + std::to_string(tally.fileDirYes) +
			  " 件 / GetAttributes 自体が失敗=" + std::to_string(tally.fileAttrFailed) + " 件");
	probe.log(
		"  fbDirectory 以外に 1 つでも true が立った標本=" + std::to_string(tally.anyOtherFlagYes) +
		" 件（0 なら SAttributes は"
		"まるごと埋まっていないことになる）");

	if (tally.folderMeasured == 0)
		probe.fail("フォルダを 1 つも測れなかった（GetAttributes がすべて失敗した）");
}
