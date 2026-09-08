# 図面（ドキュメント）を開く・作る・閉じる

プラグインのコードから**別の図面を開く／新しい空の図面を作る／いま開いている図面を
閉じる**ための口。「毎回まっさらな状態から処理を走らせたい」（自動テスト・実機テストの
往復など）ときに要る。

## 口はある（`ISDK`）

**【ヘッダ根拠 ＋ ソース根拠】**（`Include/Interfaces/VectorWorks/ISDK.h`。行番号は VW 2026 SDK）

```cpp
// 1934: パスを渡せばそのファイルを開く。**nullptr を渡すと新規の空図面**（下記）。
virtual bool    OpenDocumentPath(IFileIdentifier* pFileID, bool bShowErrorMessages) = 0;
// 1935: アクティブな図面を指定パスへ保存する。
virtual GSError SaveActiveDocumentPath(IFileIdentifier* pFileID) = 0;
// 2201: アクティブな図面を閉じる。
virtual bool    CloseDocument() = 0;
// 2719 / 2722: 開いている図面の一覧と、その中への切り替え。
virtual void    GetOpenFilesList(TVWArray_OpenFileInformation& outInformation) = 0;
virtual bool    SwitchToOpenFile(Sint32 fileRef) = 0;
```

一覧の要素は `SOpenFileInformation`（同 111 行）で、`fpFileID` / `fFileRef` /
`fIsActive` / `fIsInMemoryOnly` を持つ。`fFileRef` は開いているファイルなら必ず非負。

`Set` で作るファイル指定子（`Include/Interfaces/VectorWorks/Filing/IFileIdentifier.h`）は
**絶対パス 1 本から作れる**:

```cpp
IFileIdentifierPtr fileID(IID_FileIdentifier);
fileID->Set(TXString(absolutePath));            // 32 行: Set(const TXString& fullPath)
```

（ほかに `Set(EFolderSpecifier, bool bUserFolder, fileName)` と
`Set(IFolderIdentifier*, fileName)` がある。）

## SDK 自身が「開く → 作業 → 閉じる」で使っている

**【ソース根拠】** SDK 同梱の実装 `SDKLib/Source/VWSDK/VWFC/Tools/CatchSystemTest.cpp`
が、まさにこの 3 つで自動テスト用の図面を出し入れしている:

```cpp
TEST_OpenDocument::TEST_OpenDocument()
{
    fOpened = gSDK->OpenDocumentPath(nullptr, false /*inbShowErrorMessages*/);
}

TEST_OpenDocument::TEST_OpenDocument(const std::string& relativeFilePath, bool showErrors)
{
    IFolderIdentifierPtr testFolder(IID_FolderIdentifier);
    IFileIdentifierPtr   testFile(IID_FileIdentifier);
    gTestEnv->GetTestFileDirectory(testFolder);
    testFile->Set(testFolder, relativeFilePath);
    fOpened = gSDK->OpenDocumentPath(testFile, showErrors);
}

TEST_OpenDocument::~TEST_OpenDocument()
{
    if (fOpened)
        gSDK->CloseDocument();
}
```

ここから読み取れること:

- **`OpenDocumentPath(nullptr, …)` は「新規の空図面」**。引数なしのコンストラクタが
  これを呼んでいて、テストは「まっさらな図面で走る」ことを期待している。
- **戻り値の `bool` を成否として見る**（`fOpened`）。閉じるのは開けたときだけ。
- **`CloseDocument()` は引数を取らない**——閉じるのは**アクティブな図面**である。
  複数開いているときにどれが閉じるかは、`GetOpenFilesList` / `SwitchToOpenFile` で
  自分でアクティブを決めてから呼ぶこと。
- 同じファイルに `TEST_QuitVectorworksNoFileSave()` があり、そちらは
  `CloseAllFilesAndQuitVectorworks(false)` と**「保存しない」を明示できる**。
  `CloseDocument()` にはその引数が無い（下記の未確認事項）。

## まだ実機で確かめていないこと

**【未確認】** 下は宣言と SDK 同梱ソースから読めるところまでで、**実挙動は未確認**。

- **コマンドの実行中（`DoInterface` の中）に呼んでよいか。** SDK 自身のシステムテストは
  プラグインのモジュールから呼んでいるので見込みは高いが、
  [`Plug-in Modules`](Plug-in%20Modules.md) の `CloseAllFilesAndQuitVectorworks` は
  「VectorWorks が完全に動いている最中」でないと落ちた実績がある。同種の制約を疑うこと。
- **変更のある図面で `CloseDocument()` を呼ぶと保存ダイアログが出るか。** 出るなら
  `SaveActiveDocumentPath` で一時ファイルへ保存してから閉じる（＝変更なしにしてから
  閉じる）方法が要る。
- **開く前に取っていた `MCObjectHandle` と、開いていた undo イベントがどうなるか。**
  文書が変わればハンドルは当然無効になるはずだが、undo イベントを開いたまま別の文書へ
  移った場合の挙動は不明（[`Undo`](Undo.md) の「外部の終了時に自動で閉じる」と組み合わせて
  考える必要がある）。
- **`OpenDocumentPath` にテンプレート（`.sta`）を渡したとき**、テンプレートから作った
  無題の新規図面になるのか、テンプレートそのものを編集用に開くのか。

## 参考

- 元の調査: [issue #34](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/34)
- 関連: [`Undo`](Undo.md)（プログラムから「取り込み前」へ戻す 3 経路はすべて塞がっている）。
  丸ごと戻せない以上、「新しい図面を開いてそこへ描く」が現実的な代替になる。
