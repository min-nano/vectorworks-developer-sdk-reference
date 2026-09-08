//
//	probes/runtime/open-document/probe.cpp
//
//	[issue #34] コマンド実行中に ISDK::OpenDocumentPath でもう 1 つ文書を開いたとき、
//	  1) カレント文書・カレントレイヤがどう変わるか
//	  2) 開く前のレイヤハンドルはどうなるか（別文書のハンドルを読みにいく）
//	  3) undo イベントの状態がどうなるか
//	  4) GetOpenFilesList / SwitchToOpenFile で前の文書へ戻れるか
//	  5) 変更を加えた新規文書を CloseDocument() で閉じたとき、保存確認ダイアログが
//	     出るか（bShowErrorMessages に相当する引数が無いため、目視で確認する）
//	を実機で確かめる。
//
//	OpenDocumentPath に nullptr を渡すと「空の新規文書を開く」ことは、公式ドキュメント
//	（Info/Writing automated tests.md）に明記されている用法。テンプレート（.sta）から
//	作る専用 API は SDK に無いことをヘッダ検索で確認済みなので、ここでは唯一実在する
//	「新規文書を開く」経路（nullptr 渡し）で、コマンド実行中に呼んでよいかどうかの
//	挙動を見る。
//

#include "Probe.h"

#include <string>

namespace
{
	std::string LayerNameOrNil(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		TXString name;
		gSDK->GetObjectName(h, name);
		return static_cast<const char*>(name);
	}

	// IFileIdentifier の指すパスを読む。パスが設定されていない（in-memory only の
	// 新規文書など）場合は空文字列のままになる——それ自体が観測結果になる。
	std::string FullPathOf(VectorWorks::Filing::IFileIdentifier* pFileID)
	{
		if (pFileID == nullptr)
			return "(nullptr)";
		TXString path;
		pFileID->GetFileFullPath(path);
		return static_cast<const char*>(path);
	}
} // namespace

VW_PROBE("open-document", "コマンド実行中に別の文書を OpenDocumentPath で開く",
		 "ISDK::OpenDocumentPath(nullptr) で新規文書を開いたときの、カレント文書・"
		 "レイヤ・前の文書のハンドル・undo イベント・GetOpenFilesList の変化と、"
		 "SwitchToOpenFile での復帰・CloseDocument の保存ダイアログを確かめる")
{
	using namespace VectorWorks::Filing;

	// --- 開く前の状態を記録する ---
	const MCObjectHandle beforeLayer = gSDK->GetCurrentLayer();
	const std::string beforeLayerName = LayerNameOrNil(beforeLayer);
	probe.log("開く前のカレントレイヤ: " + beforeLayerName);

	IFileIdentifierPtr beforeFileID(IID_FileIdentifier);
	bool beforeSaved = false;
	gSDK->GetActiveDocument(&beforeFileID, beforeSaved);
	probe.log("開く前のアクティブ文書: " + FullPathOf(beforeFileID) +
			  "（saved=" + (beforeSaved ? "yes" : "no") + "）");

	probe.log(std::string("開く前の undo: building=") +
			  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));

	TVWArray_OpenFileInformation before;
	gSDK->GetOpenFilesList(before);
	probe.log("開く前の GetOpenFilesList 件数: " + std::to_string(before.GetSize()));
	Sint32 beforeFileRef = -1;
	for (size_t i = 0, cnt = before.GetSize(); i < cnt; ++i)
	{
		if (before[i].fIsActive)
			beforeFileRef = before[i].fFileRef;
	}
	probe.log("開く前のアクティブ fileRef: " + std::to_string(beforeFileRef));

	// --- ここから先で VW が落ちる可能性がある。落ちてもここまでのログは残る ---
	probe.log("ここから gSDK->OpenDocumentPath(nullptr, false) を呼ぶ"
			  "（コマンド実行中＝DoInterface の中から呼んでいる）");

	const bool opened = gSDK->OpenDocumentPath(nullptr, false);
	probe.log(std::string("OpenDocumentPath(nullptr) の戻り値: ") + (opened ? "true" : "false"));
	if (!opened)
	{
		probe.fail("OpenDocumentPath(nullptr) が false を返した（新規文書を開けなかった）");
		return;
	}

	// --- 開いた直後の状態 ---
	const MCObjectHandle afterLayer = gSDK->GetCurrentLayer();
	probe.log("開いた直後のカレントレイヤ: " + LayerNameOrNil(afterLayer));
	probe.log(std::string("カレントレイヤが変わったか: ") +
			  (afterLayer != beforeLayer ? "変わった" : "変わっていない"));

	IFileIdentifierPtr afterFileID(IID_FileIdentifier);
	bool afterSaved = false;
	gSDK->GetActiveDocument(&afterFileID, afterSaved);
	probe.log("開いた直後のアクティブ文書: " + FullPathOf(afterFileID) +
			  "（saved=" + (afterSaved ? "yes" : "no") + "）");

	probe.log(std::string("開いた直後の undo: building=") +
			  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));

	TVWArray_OpenFileInformation after;
	gSDK->GetOpenFilesList(after);
	probe.log("開いた直後の GetOpenFilesList 件数: " + std::to_string(after.GetSize()));
	Sint32 newFileRef = -1;
	for (size_t i = 0, cnt = after.GetSize(); i < cnt; ++i)
	{
		probe.log("  [" + std::to_string(i) + "] fileRef=" + std::to_string(after[i].fFileRef) +
				  " active=" + (after[i].fIsActive ? "yes" : "no") +
				  " inMemoryOnly=" + (after[i].fIsInMemoryOnly ? "yes" : "no") +
				  " path=" + FullPathOf(after[i].fpFileID));
		if (after[i].fFileRef != beforeFileRef)
			newFileRef = after[i].fFileRef;
	}

	// --- 前の文書のレイヤハンドルへ、今のアクティブ文書のもとで触れてみる ---
	probe.log("ここから、開く前に取ったレイヤハンドル（前の文書のもの）へ"
			  "GetObjectName を呼ぶ（別文書のハンドルを、今アクティブな別の文書の"
			  "コンテキストで読む。ここまでのログは落ちても残る）");
	const std::string beforeLayerNameNow = LayerNameOrNil(beforeLayer);
	probe.log("前の文書のレイヤハンドルから今読めた名前: " + beforeLayerNameNow +
			  "（開く前は「" + beforeLayerName + "」だった）");

	// --- 前の文書へ SwitchToOpenFile で戻れるか ---
	if (beforeFileRef >= 0)
	{
		const bool switched = gSDK->SwitchToOpenFile(beforeFileRef);
		probe.log(std::string("SwitchToOpenFile(") + std::to_string(beforeFileRef) +
				  ") の戻り値: " + (switched ? "true" : "false"));
		if (switched)
		{
			const MCObjectHandle backLayer = gSDK->GetCurrentLayer();
			probe.log("戻った後のカレントレイヤ: " + LayerNameOrNil(backLayer) +
					  "（開く前と同じハンドルか: " +
					  (backLayer == beforeLayer ? "同じ" : "違う") + "）");
			probe.log(std::string("戻った後の undo: building=") +
					  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));
		}
	}
	else
	{
		probe.log("開く前のアクティブ fileRef が取れなかったので"
				  "SwitchToOpenFile での復帰は試さない");
	}

	// --- 保存ダイアログの抑止可否（目視で確認する） ---
	if (newFileRef >= 0 && gSDK->SwitchToOpenFile(newFileRef))
	{
		probe.log("新しく開いた文書へ SwitchToOpenFile で切り替えた");
		const MCObjectHandle rect = gSDK->CreateRectangle(WorldRect(0, 10, 10, 0));
		probe.log(std::string("矩形を作って文書を変更した: ") + (rect != nil ? "できた" : "失敗"));
		probe.log("★ここから CloseDocument() を呼ぶ。保存確認ダイアログが出るかを"
				  "目視で確認してください（CloseDocument() に bShowErrorMessages に"
				  "相当する引数は無い）");
		const bool closeResult = gSDK->CloseDocument();
		probe.log(std::string("CloseDocument() の戻り値: ") + (closeResult ? "true" : "false"));
	}
	else
	{
		probe.log("新しく開いた文書の fileRef が特定できなかったので、"
				  "保存ダイアログの確認は省略した");
	}

	probe.log("プローブ終了");
}
