//
//	probes/runtime/folder-chooser-dialog/probe.cpp
//
//	[issue #85] フォルダを 1 つ選ばせるダイアログ（IFolderChooserDialog）を
//	**本体（ペイロード）側から**開けるかを実機で確かめる。
//
//	ヘッダで既に分かっていること（sdk-grep / sdk-ls）:
//
//	  * `Interfaces/VectorWorks/Filing/IFolderChooserDialog.h` に
//	    `IID_FolderChooserDialog` と `IFolderChooserDialog` がある。口は 5 つだけ
//	    ——SetTitle / SetDescription / SetSelectedPath / GetSelectedPath / RunDialog。
//	  * `IFolderIdentifier::GetFullPath(TXString&)` が絶対パスを返す。
//	  * `IFileIdentifier::GetFolder(IFolderIdentifier**)` が親フォルダを返す。
//	  * `IFileChooserDialog` にフォルダを選ぶモードは無い（RunOpenDialog /
//	    RunSaveDialog だけ。SetCheckFolderExist は「選ばれたファイルの入る
//	    フォルダが実在するか検査する」設定で、ディレクトリを選択対象にはしない）。
//
//	ヘッダでは分からず、ここで確かめること:
//
//	  1) 本体側から `IFolderChooserDialog` を確保して `RunDialog()` を呼べるか
//	     （`IFileChooserDialog` は本体側から開けると実測済みだが、別インターフェース
//	      なので同じとは限らない。Findings/Plug-in Modules.md）。
//	  2) キャンセルしたときと選んだときで `RunDialog()` の戻り値がどう違うか
//	     （VCOMError の実値）。キャンセル後に `GetSelectedPath` が何を返すか。
//	  3) 受け取ったパスの実物——絶対パスか・末尾に区切りが付くか・非 ASCII が
//	     UTF-8 で入るか（バイト列を 16 進で出して確かめる）。
//	  4) `IFileIdentifier::GetFolder` で採った親フォルダのパスが、3 のパスと
//	     **文字列として一致するか**（一致するなら「ファイルを選ばせて親を採る」
//	     回避策と、フォルダを選ばせる本筋が同じ値を返すと言い切れる）。
//
//	利用者にお願いすること（ダイアログの見出しに書いてある。3 回開く）:
//	  【1/3】そのままキャンセル
//	  【2/3】任意のフォルダを選ぶ（**名前に日本語を含むフォルダ**だと 3 が確かめられる）
//	  【3/3】2 で選んだフォルダの**中にあるファイルを 1 つ**選ぶ
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Filing/IFileChooserDialog.h"
#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"
#include "Interfaces/VectorWorks/Filing/IFolderChooserDialog.h"
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

	// 受け取った文字列のバイト列を 16 進で出す。**UTF-8 かどうかをログだけで
	// 判定する**ための唯一の手段（目視に頼らない）。長いパスでも読めるよう、
	// 非 ASCII を含む先頭 48 バイトだけに絞る。
	std::string HexDump(const std::string& bytes, size_t maxBytes = 48)
	{
		static const char* kDigits = "0123456789abcdef";
		std::string out;
		const size_t count = bytes.size() < maxBytes ? bytes.size() : maxBytes;
		for (size_t i = 0; i < count; ++i)
		{
			const unsigned char byte = static_cast<unsigned char>(bytes[i]);
			if (!out.empty())
				out += ' ';
			out += kDigits[byte >> 4];
			out += kDigits[byte & 0x0f];
		}
		if (bytes.size() > count)
			out += " …";
		return out;
	}

	bool HasNonAscii(const std::string& bytes)
	{
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			if (static_cast<unsigned char>(bytes[i]) >= 0x80)
				return true;
		}
		return false;
	}

	// フォルダ 1 つについて、読み出せる値を全部ログへ出す。
	void LogFolder(::vwprobe::Report& probe, const char* label, IFolderIdentifierPtr& folder,
				   std::string& outFullPath)
	{
		outFullPath.clear();
		if (folder == nullptr)
		{
			probe.log(std::string(label) + ": ポインタが nullptr");
			return;
		}

		TXString fullPath;
		const VCOMError pathErr = folder->GetFullPath(fullPath);
		outFullPath = Utf8Of(fullPath);
		probe.log(std::string(label) + ": GetFullPath VCOMError=" + ErrWord(pathErr) + " path=[" +
				  outFullPath + "]");
		probe.log(std::string(label) + ": バイト数=" + std::to_string((long)outFullPath.size()) +
				  " 非ASCII=" + YesNo(HasNonAscii(outFullPath)) + " 末尾=[" +
				  (outFullPath.empty() ? std::string("（空）")
									   : std::string(1, outFullPath[outFullPath.size() - 1])) +
				  "]");
		probe.log(std::string(label) + ": 16 進=" + HexDump(outFullPath));

		TXString name;
		const VCOMError nameErr = folder->GetName(name);
		probe.log(std::string(label) + ": GetName VCOMError=" + ErrWord(nameErr) + " name=[" +
				  Utf8Of(name) + "]");

		bool exists = false;
		const VCOMError existsErr = folder->ExistsOnDisk(exists);
		probe.log(std::string(label) + ": ExistsOnDisk VCOMError=" + ErrWord(existsErr) +
				  " 実在=" + YesNo(exists));

		SAttributes attributes = {};
		const VCOMError attrErr = folder->GetAttributes(attributes);
		probe.log(std::string(label) + ": GetAttributes VCOMError=" + ErrWord(attrErr) +
				  " fbDirectory=" + YesNo(attributes.fbDirectory) + " fbCanRead=" +
				  YesNo(attributes.fbCanRead) + " fbCanWrite=" + YesNo(attributes.fbCanWrite));

		IFolderIdentifierPtr parent;
		const VCOMError parentErr = folder->GetParentFolder(&parent);
		TXString parentPath;
		if (parent != nullptr)
			parent->GetFullPath(parentPath);
		probe.log(std::string(label) + ": GetParentFolder VCOMError=" + ErrWord(parentErr) +
				  " 親=[" + Utf8Of(parentPath) + "]");
	}
} // namespace

VW_PROBE("folder-chooser-dialog", "フォルダ選択ダイアログ（IFolderChooserDialog）を実機で開く",
		 "本体側から開けるか・キャンセルと選択の戻り値・受け取るパスの中身・"
		 "IFileIdentifier::GetFolder との一致を確かめる（ダイアログが 3 回出ます）")
{
	// -----------------------------------------------------------------------
	// 0) 本体側で確保できるか。
	probe.log("0) IFolderChooserDialog を確保する");
	IFolderChooserDialogPtr folderDlg(IID_FolderChooserDialog);
	probe.log(std::string("   確保できたか: ") + YesNo(folderDlg != nullptr));
	if (folderDlg == nullptr)
	{
		probe.fail("IFolderChooserDialog を確保できなかった（本体側からは使えない）");
		return;
	}

	// -----------------------------------------------------------------------
	// 1) キャンセルしたときの戻り値。
	probe.log("1) キャンセルしたときの RunDialog の戻り値を見る");
	probe.log("   SetTitle VCOMError=" +
			  ErrWord(folderDlg->SetTitle("【1/3】そのままキャンセルしてください")));
	probe.log("   SetDescription VCOMError=" +
			  ErrWord(folderDlg->SetDescription(
				  "調査 #85: キャンセルしたときの戻り値を測っています。選ばずに閉じてください")));

	const VCOMError cancelErr = folderDlg->RunDialog();
	probe.log("   RunDialog VCOMError=" + ErrWord(cancelErr));

	IFolderIdentifierPtr afterCancel;
	const VCOMError afterCancelErr = folderDlg->GetSelectedPath(&afterCancel);
	probe.log("   キャンセル後の GetSelectedPath VCOMError=" + ErrWord(afterCancelErr) +
			  " ポインタ=" + (afterCancel == nullptr ? "nullptr" : "非 nullptr"));
	if (afterCancel != nullptr)
	{
		TXString strayPath;
		const VCOMError strayErr = afterCancel->GetFullPath(strayPath);
		probe.log("   キャンセル後の GetFullPath VCOMError=" + ErrWord(strayErr) + " path=[" +
				  Utf8Of(strayPath) + "]");
	}

	// -----------------------------------------------------------------------
	// 2) 選んだときの戻り値と、受け取るパスの中身。
	//    **同じインスタンスを使い回す**（作り直さない）——1 回目の結果が残るかも
	//    ここで分かる。
	probe.log("2) フォルダを選んでもらい、受け取る値を全部読み出す");
	probe.log("   SetTitle VCOMError=" +
			  ErrWord(folderDlg->SetTitle("【2/3】フォルダを 1 つ選んでください")));
	probe.log("   SetDescription VCOMError=" +
			  ErrWord(folderDlg->SetDescription("調査 #85: 名前に日本語を含むフォルダだと助かります"
												"（UTF-8 で受け取れるかを測っています）")));

	const VCOMError chooseErr = folderDlg->RunDialog();
	probe.log("   RunDialog VCOMError=" + ErrWord(chooseErr));
	if (!VCOM_SUCCEEDED(chooseErr))
	{
		probe.fail("【2/3】でフォルダが選ばれなかった（RunDialog が失敗を返した）。"
				   "選んだうえでもう一度走らせてください");
		return;
	}

	IFolderIdentifierPtr chosen;
	const VCOMError chosenErr = folderDlg->GetSelectedPath(&chosen);
	probe.log("   GetSelectedPath VCOMError=" + ErrWord(chosenErr) +
			  " ポインタ=" + (chosen == nullptr ? "nullptr" : "非 nullptr"));

	std::string chosenPath;
	LogFolder(probe, "   選ばれたフォルダ", chosen, chosenPath);
	if (chosenPath.empty())
	{
		probe.fail("選ばれたフォルダの絶対パスを取り出せなかった");
		return;
	}

	// -----------------------------------------------------------------------
	// 3) 同じフォルダの中のファイルを選ばせ、IFileIdentifier::GetFolder で採った
	//    親フォルダのパスと突き合わせる（回避策と本筋が同じ値になるか）。
	probe.log("3) 同じフォルダの中のファイルを選んでもらい、GetFolder と突き合わせる");
	IFileChooserDialogPtr fileDlg(IID_FileChooserDialog);
	probe.log(std::string("   IFileChooserDialog を確保できたか: ") + YesNo(fileDlg != nullptr));
	if (fileDlg == nullptr)
	{
		probe.fail("IFileChooserDialog を確保できなかった");
		return;
	}

	probe.log("   SetTitle VCOMError=" +
			  ErrWord(fileDlg->SetTitle("【3/3】いま選んだフォルダの中のファイルを 1 つ")));
	// 初期位置を 2 で選んだフォルダにする。**ここが効いているかどうかも、
	// 選ばれたファイルのパスを見れば分かる**（利用者が動かなければ同じ場所になる）。
	probe.log("   SetInitialFolder VCOMError=" + ErrWord(fileDlg->SetInitialFolder(chosen)));
	probe.log("   AddFilterAllFiles VCOMError=" + ErrWord(fileDlg->AddFilterAllFiles()));

	const VCOMError openErr = fileDlg->RunOpenDialog();
	probe.log("   RunOpenDialog VCOMError=" + ErrWord(openErr));
	if (!VCOM_SUCCEEDED(openErr))
	{
		probe.fail("【3/3】でファイルが選ばれなかった。選んだうえでもう一度走らせてください");
		return;
	}

	Uint32 count = 0;
	probe.log("   GetSelectedFileNamesCount VCOMError=" +
			  ErrWord(fileDlg->GetSelectedFileNamesCount(count)) +
			  " 件数=" + std::to_string((long)count));

	IFileIdentifierPtr file;
	const VCOMError fileErr = fileDlg->GetSelectedFileName(0, &file);
	probe.log("   GetSelectedFileName VCOMError=" + ErrWord(fileErr) +
			  " ポインタ=" + (file == nullptr ? "nullptr" : "非 nullptr"));
	if (file == nullptr)
	{
		probe.fail("選ばれたファイルの IFileIdentifier を取り出せなかった");
		return;
	}

	TXString filePath;
	probe.log("   GetFileFullPath VCOMError=" + ErrWord(file->GetFileFullPath(filePath)) +
			  " path=[" + Utf8Of(filePath) + "]");

	IFolderIdentifierPtr containing;
	const VCOMError containingErr = file->GetFolder(&containing);
	probe.log("   GetFolder VCOMError=" + ErrWord(containingErr) +
			  " ポインタ=" + (containing == nullptr ? "nullptr" : "非 nullptr"));

	std::string containingPath;
	LogFolder(probe, "   ファイルの親フォルダ", containing, containingPath);

	// -----------------------------------------------------------------------
	// 4) 突き合わせ。**文字列として一致するか**が知りたいこと（末尾の区切りの
	//    有無まで含めて）。
	probe.log("4) 突き合わせ");
	probe.log("   フォルダ選択の結果 =[" + chosenPath + "]");
	probe.log("   ファイルの親       =[" + containingPath + "]");
	probe.log(std::string("   文字列として一致: ") + YesNo(chosenPath == containingPath));
	if (chosenPath != containingPath)
	{
		probe.log("   ※ 一致しなかった。上の 16 進と末尾の文字を見比べれば、"
				  "区切りの有無か、利用者が別のフォルダへ移ったかが分かる");
	}
}
