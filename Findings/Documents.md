# 図面（ドキュメント）を開く・作る

プラグインの実行中に**別の図面（文書）を開いて、以後の描画をそちらへ向けられるか**の
調査（[issue #34](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/34)）。
背景は [issue #23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23) /
[#27](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/27) /
[#31](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/31)（[Undo](Undo.md)）
の続き——「前回の実行結果を戻す」手段が undo にもレイヤ削除にも無い場面で、
「テンプレートから新しい図面を開き直す」が使えないかという問い。

## 存在する API【ヘッダ根拠】

ISDK（`Interfaces/VectorWorks/ISDK.h`）に、複数の図面（文書）を開いて切り替えるための
一群の呼び出しがある。**すべて宣言と、実装ソース側の薄い転送コード（`gCBP` 経由で
Vectorworks 本体へ渡すだけ）までしか確認できていない**——実際に何が起きるかは
Vectorworks 本体側（SDK に同梱されていない）の実装に依存する。

| API | 効果（ヘッダのコメントより） |
| --- | --- |
| `bool OpenDocumentPath(IFileIdentifier* pFileID, bool bShowErrorMessages)` | `pFileID` の指すパスの文書を開く。`GS_OpenDocumentPath` の説明: 「Opens the document specified by inPath. Set inShowErrorMessages to false if you want to make sure no dialogs come up.」 |
| `bool CloseDocument()` | 現在の文書を閉じる。引数は無く、**保存の要否を指定する口が無い**（後述） |
| `bool GetActiveDocument(IFileIdentifier** ppOutFileID, bool& outSaved)` | 現在アクティブな文書のパスと、保存済みかどうかを返す |
| `void GetOpenFilesList(TVWArray_OpenFileInformation& outInformation)` | 現在開いている**すべての**文書の一覧を返す（後述） |
| `bool SwitchToOpenFile(Sint32 fileRef)` | 開いている別の文書へアクティブを切り替える。`fileRef` は `GetOpenFilesList` から取る |
| `GSError SaveActiveDocumentPath(IFileIdentifier* pFileID)` | アクティブな文書を、ダイアログを出さずに指定パスへ保存する（エラー時のアラートを除く） |

`GetOpenFilesList` が返す `SOpenFileInformation`（`ISDK.h`）は 1 文書につき

```cpp
struct SOpenFileInformation
{
    IFileIdentifierPtr  fpFileID;
    Sint32              fFileRef;         // SwitchToOpenFile へ渡す鍵
    bool                fIsActive;        // これがカレント文書か
    bool                fIsInMemoryOnly;  // true = まだディスクに保存されていない
};
```

を持つ。**Vectorworks は元から複数の文書を同時に開いた状態を扱える**（`sdk-grep` で
見つかった `VWResourceListCategorized.cpp` のコメント: "List resources from open
documents other than the current document"）。「今の文書を閉じてから次を開く」以外に、
「開いたまま裏に残し、`SwitchToOpenFile` で行き来する」という選択肢も API 上は存在する。

## テンプレート（`.sta`）から新規文書を作る専用 API は無い【ヘッダ根拠】

SDK 全体（`Include` + `Source` の実装ソース）を次のパターンで検索したが、**該当なし**。

| 検索パターン | 結果 |
| --- | --- |
| `(New\|Open\|Create)[A-Za-z]*(Document\|Drawing\|File)` | ヒットは `OpenDocumentPath` 系・`OpenPDFDocument`・`CreateNewXMLDocument`（XML の話）など、いずれも文書「作成」とは無関係 |
| `GS_(New\|Create)[A-Za-z]*(Doc\|File\|Draw)` | 0 件 |
| `ISDK.{0,5}New[A-Za-z]*\(` | 0 件 |
| `NewFromTemplate\|CreateFromTemplate\|kFileTypeVWTemplate\|SaveAsTemplate\|NewDrawing` | 0 件 |

VectorScript の `DoMenuTextByName('New From Template', ...)` に相当する ISDK / VWFC の
呼び出しは無い。[issue #27](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/27)
で確定した「`DoMenuTextByName` 相当の汎用メニュー起動 API 自体が SDK に無い」という結論と
整合する——テンプレートからの新規作成が Finder / エクスプローラでの `.sta` ダブルクリックや
ファイルメニューの「新規」相当だとすれば、そもそもそれを起動する手段が無い。

**唯一実在する「新規文書を開く」経路は `OpenDocumentPath(nullptr, false)`。** 公式ドキュメント
（[`Info/Writing automated tests.md`](../Info/Writing%20automated%20tests.md)）に
用例として明記されている:

```cpp
void SystemTests::YourTest::setUp()
{
    // This opens a blank new document
    CPPUNIT_ASSERT(gSDK->OpenDocumentPath(nullptr, false));
}
```

ただしこれは**テンプレートを選べない「空の」新規文書**であり、issue が求める
「テンプレート由来の初期状態（通り芯レイヤ・クラス・シンボル定義・スタイル）」を
再現する手段にはならない。この用例も自動テストの `setUp`/`tearDown`（`System Test
Manager` から走らせる文脈）のものであり、**プラグインのメニューコマンド実行中
（`DoInterface` の中）から呼んだ場合の挙動は別途確認が要る**（次節）。

## 既存ファイル（`.vwx` / `.sta`）をパス指定で開くことはできる【ヘッダ根拠】

`OpenDocumentPath(IFileIdentifier*, bool)` に実在するファイルの `IFileIdentifier` を渡せば
開ける。SDK 自身の自動テスト基盤（`VWFC/Tools/CatchSystemTest.cpp` の
`TEST_OpenDocument`）が実際にこの形で使っている:

```cpp
IFolderIdentifierPtr testFolder(IID_FolderIdentifier);
IFileIdentifierPtr testFile(IID_FileIdentifier);
gTestEnv->GetTestFileDirectory(testFolder);
testFile->Set(testFolder, relativeFilePath);
fOpened = gSDK->OpenDocumentPath(testFile, inbShowErrorMessages);
// ...
gSDK->CloseDocument();
```

**`.sta`（テンプレート）ファイルそのものを `OpenDocumentPath` へ渡した場合に何が起きるかは
未確認。**`.sta` を Finder でダブルクリックしたときのような「テンプレートを元にした
無題の新規文書が開く」特別扱いがあるのか、それとも `.sta` ファイル自体を（上書き保存可能な
既存文書として）開いてしまうのかは、ヘッダのコメントからは判断できない。実機確認が要る。

## 未確認のまま残る点（実機確認が必要）

issue #34 が求める「呼んでよい場面・カレント文書の変化・前の文書のハンドル・undo
イベント・保存ダイアログの抑止可否」は、いずれも Vectorworks 本体側の実装に依存し、
ヘッダの宣言・コメントだけでは判断できない。次を確かめるプローブ
（[`probes/runtime/open-document/probe.cpp`](../probes/runtime/open-document/probe.cpp)）を
実機確認プラグインへ足した。実機で走らせて結果が得られ次第、本節を更新して確定させる。

- **コマンド実行中（`DoInterface` の中）に `OpenDocumentPath` / `CloseDocument` /
  `SwitchToOpenFile` を呼んでよいか。** [Plug-in Modules](Plug-in%20Modules.md) には
  `CloseAllFilesAndQuitVectorworks` が「Vectorworks が完全に動いている最中にしか呼べない
  （プラグイン読み込み中に呼ぶと落ちる）」という類例があり、文書を開く系にも同種の
  制約が無いとは言い切れない。
- **開いた後、カレント文書がどう変わるか。** `GetCurrentLayer()` が新しい文書のレイヤを
  指すようになるか、`GetActiveDocument` が返すパスが切り替わるか。
- **開く前の文書のハンドル（`MCObjectHandle`）はどうなるか。** 別文書のハンドルを、
  切り替え後のアクティブ文書のコンテキストで読みにいったときに何が起きるか
  （正しい値が読める／無効値が返る／クラッシュする、のどれか）。
- **開く前に開いていた undo イベントの扱い。** `IsCurrentlyBuildingAnUndoEvent()` が
  開く前後・`SwitchToOpenFile` での復帰後でどう変わるか。
- **前の図面を保存せずに閉じられるか。** `CloseDocument()` に `bShowErrorMessages`
  相当の引数は無い（ヘッダ上、引数なし）。未保存の変更がある文書を閉じたときに
  「保存しますか」ダイアログを抑止する手段がヘッダから見つからない——これも実機で
  確かめるまでは「抑止できない」とみなして設計するのが安全。

## 現時点の結論（プラグイン開発への示唆）

- **既存ファイルを開く／複数文書を切り替えるための API は存在する**
  （`OpenDocumentPath` / `GetOpenFilesList` / `SwitchToOpenFile` / `CloseDocument`）。
- **テンプレート専用の新規作成 API は無い。** 使えるのは「空の新規文書を開く」
  （`OpenDocumentPath(nullptr, false)`）だけで、テンプレートの初期状態
  （通り芯レイヤ等）は再現できない。
- 上記いずれも**実機での挙動（コマンド実行中に呼んでよいか、保存ダイアログを
  抑止できるか等）が未確認**なので、実機確認が取れるまでは「周ごとに新しい図面を
  開いて完全にまっさらな状態から始める」方式には切り替えず、既存の
  「前回作ったレイヤを `DeleteObject` で消す」方式（[Undo](Undo.md)）を使い続け、
  テンプレート由来の「共通」レイヤのように消せない土台へ描いた分はクラスで見分けて
  個別に消す方向で設計するのが妥当。
