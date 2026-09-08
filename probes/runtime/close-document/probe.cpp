//
//	probes/runtime/close-document/probe.cpp
//
//	[issue #34] 文書を開く／閉じるまわりの残り。**2 度目である。**
//
//	【1 度目で取れたもの】
//
//	  ・**`CloseDocument()` は `false` を返しながら文書を閉じている。** 未保存の変更が
//	    あっても閉じ、**保存確認ダイアログも出ない**（74ms / 135ms）。`GetOpenFilesList`
//	    の件数が 3→2、2→1 と減った。**戻り値は当てにならない。**
//	  ・**実在するファイルをパス指定で開ける**（`OpenDocumentPath(fileID, false)` = `true`、
//	    683ms。アクティブ文書が保存したパスになった）。
//	  ・`SaveActiveDocumentPath` は `GSError=0` で通り、**アクティブ文書がその保存先になる**。
//
//	【1 度目で取れなかったもの——プローブの組み立てが悪かった】
//
//	  ・**`.sta`**: `.sta` へ保存した直後、その `.sta` が**アクティブな文書そのもの**に
//	    なっていた。そこへ `OpenDocumentPath` を掛けて `false` だったので、理由が
//	    「`.sta` だから」なのか「**既に開いているから**」なのか切り分いていない。
//	  ・**アクティブでない文書へ書く**: 手前の節で文書を閉じてしまい、戻る先が無くなって
//	    飛ばされた。
//	  ・**undo イベント**: 文書を開いた後 `building=no` になったが、「イベントが終わった」
//	    のか「**フラグが文書ごと**」なのか分かれていない。**元の文書へ戻って読めば付く。**
//
//	【この版の作り】上の 3 つだけを、**節ごとに状態を整えてから**測る。
//
//	  G1 undo イベントは文書ごとか   … 開く → 別文書で読む → **戻って読む**
//	  G2 既に開いているファイルを開けるか … `.vwx` を閉じてから開き、**もう一度**開く
//	  G3 `.sta` を（開いていない状態で）開く … 判定はアクティブ文書のパス
//	  G4 アクティブでない文書へ書く … 落ちる見込みがあるので**最後**
//
//	【走らせる人へ】**新規の空図面で 1 回走らせるだけ**です。ダイアログが出たら閉じて
//	ください（どこで出たかはログの `elapsed=` に出ます）。文書をいくつか開き、
//	**プローブは開いたものを閉じません**——終わったら手で閉じてください。一時ファイルは
//	最後に消します。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <chrono>
#include <string>

namespace
{
	using namespace VectorWorks;

	// モーダルダイアログは人が閉じるまで戻らないので、そこだけ秒単位になる。
	class Stopwatch
	{
	public:
		Stopwatch() : fStart(std::chrono::steady_clock::now()) {}

		long ms() const
		{
			return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::steady_clock::now() - fStart)
				.count();
		}

	private:
		std::chrono::steady_clock::time_point fStart;
	};

	std::string Elapsed(long ms)
	{
		std::string s = " elapsed=" + std::to_string(ms) + "ms";
		if (ms >= 1000)
			s += " ← **ここで人を待っていた＝ダイアログが出た**";
		return s;
	}

	// ISDK に名前でレイヤを引く呼び出しは無いので、図面の頭から辿る
	// （probes/runtime/script-engine-undo/ で使っていたものと同じ）。
	bool LayerExistsByName(const TXString& name)
	{
		for (MCObjectHandle h = VWFC::VWObjects::VWDocument::GetDrawingHeaderFristMember();
			 h != nil; h = gSDK->NextObject(h))
		{
			if (!VWFC::VWObjects::VWLayerObj::IsLayerObject(h))
				continue;
			VWFC::VWObjects::VWLayerObj layer(h);
			if (layer.GetObjectName() == name)
				return true;
		}
		return false;
	}

	std::string PathOf(IFileIdentifier* pFileID)
	{
		if (pFileID == nullptr)
			return "(nullptr)";
		TXString path;
		pFileID->GetFileFullPath(path);
		return static_cast<const char*>(path);
	}

	std::string ActiveDocument()
	{
		IFileIdentifierPtr fid(IID_FileIdentifier);
		bool saved = false;
		gSDK->GetActiveDocument(&fid, saved);
		return PathOf(fid) + "（saved=" + (saved ? "yes" : "no") + "）";
	}

	std::string Building()
	{
		return gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no";
	}

	size_t OpenFileCount()
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		return files.GetSize();
	}

	size_t LogOpenFiles(::vwprobe::Report& probe, const char* label)
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		const size_t count = files.GetSize();
		probe.log(std::string(label) + ": 開いている文書 " + std::to_string(count) + " 件");
		for (size_t i = 0; i < count; ++i)
			probe.log(std::string("  [") + std::to_string(i) +
					  "] fileRef=" + std::to_string((long)files[i].fFileRef) + " active=" +
					  (files[i].fIsActive ? "yes" : "no") + " path=" + PathOf(files[i].fpFileID));
		return count;
	}

	Sint32 ActiveFileRef()
	{
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, n = files.GetSize(); i < n; ++i)
			if (files[i].fIsActive)
				return files[i].fFileRef;
		return -1;
	}

	// **閉じたかどうかは件数で見る**（戻り値は 1 度目に当てにならないと分かった）。
	bool CloseActiveDocument(::vwprobe::Report& probe, const char* label)
	{
		const size_t before = OpenFileCount();
		const Stopwatch watch;
		const bool ret = gSDK->CloseDocument();
		const size_t after = OpenFileCount();
		probe.log(std::string(label) + ": CloseDocument() = " + (ret ? "true" : "false") +
				  Elapsed(watch.ms()) + " / 件数 " + std::to_string(before) + " → " +
				  std::to_string(after) + (after < before ? "（閉じた）" : "（閉じていない）"));
		return after < before;
	}

	std::string TempPath(const std::string& logPath, const char* name)
	{
		const size_t slash = logPath.find_last_of('/');
		const std::string dir =
			(slash == std::string::npos) ? std::string("/tmp") : logPath.substr(0, slash);
		return dir + "/" + name;
	}

	bool MakeFileID(IFileIdentifierPtr& fid, const std::string& path)
	{
		return fid && VCOM_SUCCEEDED(fid->Set(TXString(path.c_str())));
	}

	std::string ExistsOnDisk(IFileIdentifierPtr& fid)
	{
		bool exists = false;
		if (!fid || !VCOM_SUCCEEDED(fid->ExistsOnDisk(exists)))
			return "(読めない)";
		return exists ? "有" : "無";
	}
} // namespace

VW_PROBE("close-document", "文書を開く・閉じるの残り 3 つ（#34、2 度目）",
		 "undo イベントのフラグは文書ごとか・既に開いているファイルは開けるか・"
		 ".sta はテンプレート扱いか・アクティブでない文書へ書けるかを確かめる")
{
	const std::string vwxPath = TempPath(probe.logPath(), "VwSdkProbes-doc2.vwx");
	const std::string staPath = TempPath(probe.logPath(), "VwSdkProbes-doc2.sta");

	probe.log("**ダイアログが出たら閉じてください。** どこで出たかは覚えなくて結構です"
			  "——ログの elapsed= が秒単位になっている行がそれです");
	probe.log("一時ファイル: " + vwxPath + " / " + staPath + "（最後に消します）");
	probe.log("開始時 " + ActiveDocument() + " / undo building=" + Building());
	LogOpenFiles(probe, "開始時");

	// =====================================================================
	// G1) **undo イベントのフラグは文書ごとか。** 1 度目は「文書を開いたら building が
	//     no になった」までしか取れていない。**元の文書へ戻って読めば**、
	//     「イベントが終わった」のか「フラグが文書ごと」のかが付く。
	// =====================================================================
	probe.log("=== G1) undo イベントのフラグは文書ごとか ===");
	{
		const Sint32 docA = ActiveFileRef();
		probe.log("G1: 文書 A の fileRef=" + std::to_string((long)docA) +
				  " / 開始前 building=" + Building());

		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent("probe-g1-open-event");
		gSDK->CreateLayer(TXString("probe-g1-inner"), kLayerDesign);
		probe.log("G1: A でイベントを開いてレイヤを作った。building=" + Building());

		const Stopwatch watch;
		const bool opened = gSDK->OpenDocumentPath(nullptr, false);
		probe.log(std::string("G1: OpenDocumentPath(nullptr) = ") + (opened ? "true" : "false") +
				  Elapsed(watch.ms()));
		if (!opened)
		{
			probe.fail("G1: 新規文書を開けなかった（以降の節も同じ口を使うので中断する）");
			return;
		}
		const Sint32 docB = ActiveFileRef();
		probe.log("G1: 文書 B（fileRef=" + std::to_string((long)docB) +
				  "）で building=" + Building() + " ← 1 度目はここまでしか見ていない");

		if (docA >= 0 && gSDK->SwitchToOpenFile(docA))
		{
			probe.log("G1: **A へ戻って** building=" + Building() +
					  "（yes なら**フラグは文書ごと**でイベントは生きている。"
					  "no なら**イベントそのものが終わった**）");
			probe.log(std::string("G1: A のイベント中に作ったレイヤ=") +
					  (LayerExistsByName("probe-g1-inner") ? "有" : "無"));
			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
			{
				const Boolean removed = gSDK->EndAndRemoveUndoEvent();
				probe.log(std::string("G1: 後始末 EndAndRemoveUndoEvent() = ") +
						  (removed ? "true" : "false"));
			}
		}
		else
		{
			probe.log("G1: A へ戻れなかったので判定できない");
		}

		// B は用済み。閉じて片付ける（閉じられることは 1 度目に分かっている）。
		if (docB >= 0 && gSDK->SwitchToOpenFile(docB))
			CloseActiveDocument(probe, "G1: 用済みの B を閉じる");
	}

	// =====================================================================
	// G2) **既に開いているファイルをもう一度開けるか。** 1 度目の `.sta` の `false` が
	//     これで説明できるなら、`.sta` の判定はやり直しになる。
	// =====================================================================
	probe.log("=== G2) 既に開いているファイルをもう一度開けるか ===");
	{
		IFileIdentifierPtr fid(IID_FileIdentifier);
		if (!MakeFileID(fid, vwxPath))
		{
			probe.fail("G2: IFileIdentifier にパスを設定できなかった");
		}
		else
		{
			const GSError saved = gSDK->SaveActiveDocumentPath(fid);
			probe.log(std::string("G2: SaveActiveDocumentPath = ") + std::to_string((long)saved) +
					  " / ディスク上の存在=" + ExistsOnDisk(fid));
			probe.log("G2: 保存後のアクティブ文書: " + ActiveDocument() +
					  "（保存すると、その文書自身が保存先のファイルになる）");

			// **ここが要点**——保存したファイルは「いま開いている文書」そのものである。
			const Stopwatch again;
			const bool reopened = gSDK->OpenDocumentPath(fid, false);
			probe.log(std::string("G2: 開いたままの同じファイルを OpenDocumentPath = ") +
					  (reopened ? "true" : "false") + Elapsed(again.ms()) +
					  "（false なら、**既に開いているファイルは開けない**——1 度目の "
					  ".sta の false はこれで説明できる）");
			LogOpenFiles(probe, "G2 もう一度開いた後");

			// 閉じてから開き直せることを確かめる（これで G3 の前提も整う）。
			CloseActiveDocument(probe, "G2: いったん閉じる");
			const Stopwatch fresh;
			const bool openedAgain = gSDK->OpenDocumentPath(fid, false);
			probe.log(std::string("G2: 閉じてから同じファイルを開く = ") +
					  (openedAgain ? "true" : "false") + Elapsed(fresh.ms()));
			probe.log("G2: いまのアクティブ文書: " + ActiveDocument());
		}
	}

	// =====================================================================
	// G3) **`.sta` を、開いていない状態で開く。** 判定はアクティブ文書のパス。
	//     パスが `.sta` そのもの → ファイルを開いた。無題 → テンプレート扱い。
	// =====================================================================
	probe.log("=== G3) .sta を（開いていない状態で）開く ===");
	{
		IFileIdentifierPtr staID(IID_FileIdentifier);
		if (!MakeFileID(staID, staPath))
		{
			probe.log("G3: .sta のパスを設定できなかったので飛ばす");
		}
		else
		{
			const GSError saved = gSDK->SaveActiveDocumentPath(staID);
			probe.log(std::string("G3: SaveActiveDocumentPath(.sta) = ") +
					  std::to_string((long)saved) + " / ディスク上の存在=" + ExistsOnDisk(staID));
			probe.log("G3: 保存直後のアクティブ文書: " + ActiveDocument());

			// **1 度目の取りこぼしはここ。** 開く前に閉じておく。
			const bool closed = CloseActiveDocument(probe, "G3: .sta を閉じてから開き直す");
			if (!closed)
			{
				probe.log("G3: .sta を閉じられなかったので、開き直しの判定はできない");
			}
			else
			{
				probe.log("G3: 閉じた後のアクティブ文書: " + ActiveDocument());
				const Stopwatch watch;
				const bool opened = gSDK->OpenDocumentPath(staID, false);
				probe.log(std::string("G3: OpenDocumentPath(.sta, false) = ") +
						  (opened ? "true" : "false") + Elapsed(watch.ms()));
				probe.log("G3: 開いた後のアクティブ文書: " + ActiveDocument());
				probe.log("G3: **判定** パスが .sta そのもの → ファイルを開いた / "
						  "無題（名称未設定…）→ テンプレートとして扱われた");
				LogOpenFiles(probe, "G3 開いた後");
			}
		}
	}

	// =====================================================================
	// G4) **アクティブでない文書のハンドルへ書く。** 読みは PR #46 で確認済み。
	//     **落ちる見込みがあるので最後**（ここまでの結果はもう取れている）。
	//     1 度目は手前の節で戻る先を閉じてしまい飛ばされた——ここでは**この節の中で
	//     使う 2 つの文書を用意する**ので、手前の節の状態に依存しない。
	// =====================================================================
	probe.log("=== G4) アクティブでない文書のハンドルへ書く（落ちる見込みがあるので最後） ===");
	{
		const Sint32 docX = ActiveFileRef();
		const MCObjectHandle layerX = gSDK->GetCurrentLayer();
		TXString nameX;
		if (layerX != nil)
			gSDK->GetObjectName(layerX, nameX);
		probe.log("G4: 文書 X の fileRef=" + std::to_string((long)docX) +
				  " カレントレイヤ=" + static_cast<const char*>(nameX));

		if (docX < 0 || layerX == nil)
		{
			probe.log("G4: 書き込み元のレイヤハンドルが取れないので飛ばす");
		}
		else if (!gSDK->OpenDocumentPath(nullptr, false))
		{
			probe.log("G4: 別の文書を開けなかったので飛ばす");
		}
		else
		{
			probe.log("G4: 別の文書 Y をアクティブにした: " + ActiveDocument());
			probe.log("★ G4: これから**アクティブでない文書 X のレイヤハンドルへ "
					  "SetObjectName を呼ぶ**。落ちたらこの行が最後に残る");
			const Boolean ok = gSDK->SetObjectName(layerX, "probe-g4-renamed");
			probe.log(std::string("G4: SetObjectName = ") + (ok ? "true" : "false") +
					  "（落ちなかった）");

			TXString readBack;
			gSDK->GetObjectName(layerX, readBack);
			probe.log(std::string("G4: Y のまま読み戻した名前: ") +
					  static_cast<const char*>(readBack));

			if (gSDK->SwitchToOpenFile(docX))
			{
				TXString afterSwitch;
				gSDK->GetObjectName(layerX, afterSwitch);
				probe.log(std::string("G4: X へ戻って読んだ名前: ") +
						  static_cast<const char*>(afterSwitch) +
						  "（probe-g4-renamed なら **書けている**）");
			}
			else
			{
				probe.log("G4: X へ戻れなかったので、書けたかの確認まではできない");
			}
		}
	}

	// =====================================================================
	// 後片付け: 一時ファイルを消す。文書は閉じない（閉じ方がこの調査の主題）。
	// =====================================================================
	probe.log("=== 後片付け ===");
	for (const std::string& path : {vwxPath, staPath})
	{
		IFileIdentifierPtr fid(IID_FileIdentifier);
		if (MakeFileID(fid, path) && ExistsOnDisk(fid) == "有")
		{
			const VCOMError err = fid->DeleteOnDisk();
			probe.log("一時ファイルを消した: " + path +
					  "（DeleteOnDisk=" + std::to_string((long)err) + "）");
		}
	}
	LogOpenFiles(probe, "終了時");
	probe.log("=== 完了。開いた文書はそのままです（勝手に閉じません）。手で閉じてください。 ===");
}
