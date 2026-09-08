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
再現する手段にはならない。この用例は自動テストの `setUp`/`tearDown`（`System Test
Manager` から走らせる文脈）のものだが、**プラグインのメニューコマンド実行中
（`DoInterface` の中）から呼んでも動く**ことは実機で確かめた（下記「実機で確かめたこと」）。

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
未確認のまま。**`.sta` を Finder でダブルクリックしたときのような「テンプレートを元にした
無題の新規文書が開く」特別扱いがあるのか、それとも `.sta` ファイル自体を（上書き保存可能な
既存文書として）開いてしまうのかは、ヘッダのコメントからは判断できない。実機確認が要る。

## 実機で確かめたこと（VW 2026 / macOS）

以下は**プラグインのメニューコマンドの中（`DoInterface` の中）から**
実機確認プラグイン（VwSdkProbes、ビルド `3a60d15e81fb`）でプローブ
`probes/runtime/open-document/` を走らせた実測（役目を終えたのでプローブ自体は削除済み）。
**印の無い記述は実機確認済み。** プローブ全体は 0.73 秒で完了し、落ちなかった。

### コマンド実行中に呼んでよい

`gSDK->OpenDocumentPath(nullptr, false)` をコマンド実行中に呼んで **`true`**。落ちない。
`SwitchToOpenFile` / `CloseDocument` も同じ文脈で呼べた。[Plug-in
Modules](Plug-in%20Modules.md) の `CloseAllFilesAndQuitVectorworks`（プラグイン読み込み中に
呼ぶと落ちる）のような「呼べる場面」の制約は、**少なくともコマンド実行中については無い**。

### カレント文書とカレントレイヤは即座に新しい文書へ移る

| | 開く前 | `OpenDocumentPath(nullptr, false)` の直後 |
| --- | --- | --- |
| `GetCurrentLayer()` の名前 | `7` | `レイヤ-1`（ハンドルも別物） |
| `GetActiveDocument()` のパス | `/Applications/VW2026/名称未設定 1` | `/Applications/VW2026/名称未設定 3` |
| 同 `outSaved` | `no` | `no` |
| `GetOpenFilesList()` の件数 | 1 | 2 |

**呼んだ瞬間に描画先が新しい文書へ移る**（切り替えてから `CreateRectangle` が新しい文書に
入ることを確認）。ついでに分かったこと 2 つ:

- **未保存の文書でも `GetActiveDocument` はパスを返す。** `/Applications/VW2026/名称未設定 1`
  のように「アプリケーションのあるディレクトリ＋無題の文書名」が入るので、**保存済みか
  どうかはパスの有無ではなく `outSaved` で見る**。
- 新規文書の名前は「名称未設定 2」ではなく**「名称未設定 3」**だった。無題の連番は VW の
  セッション全体で進むので、**名前から文書を同定しない**。

### `GetOpenFilesList` と `fileRef`

```
[0] fileRef=2 active=no  inMemoryOnly=yes path=/Applications/VW2026/名称未設定 1
[1] fileRef=9 active=yes inMemoryOnly=yes path=/Applications/VW2026/名称未設定 3
```

- **`fileRef` は連番ではない**（2 の次が 9）。**不透明な鍵**として `GetOpenFilesList` から
  取り、そのまま `SwitchToOpenFile` へ渡す（値を自分で組み立てない）。
- 一度も保存していない文書は `fIsInMemoryOnly=yes`。

### 前の文書のハンドルは、別の文書がアクティブでも読める

開く前に取ったレイヤハンドルへ、**新しい文書がアクティブな状態で** `GetObjectName` を
呼ぶと、開く前と同じ「7」が返った。落ちないし、無効値にもならない。

ただし確かめたのは**読み**だけである。アクティブでない文書のハンドルへ**書く**
（図形を足す・消す・属性を変える）のが安全かは未確認なので、**描くときは
`SwitchToOpenFile` でその文書をアクティブにしてから**にするのが安全。

### 文書を開く・切り替える操作は undo イベントを開かない

`IsCurrentlyBuildingAnUndoEvent()` は開く前・開いた直後・`SwitchToOpenFile` で戻った後の
いずれも `no`。**自分で開いた undo イベントを持ったまま文書を開いた場合**にどうなるかは
確かめていない。[Undo](Undo.md)「間接経路: スクリプトエンジン経由…」に**開いたままの
イベントが他所の呼び出しで勝手に終わらされる**実例があるので、**その場面を作らない**
（イベントを閉じてから文書を開く）のが安全。

### `SwitchToOpenFile` で行き来できる

`SwitchToOpenFile(2)`（開く前の `fileRef`）は `true` を返し、**カレントレイヤは開く前と
同じハンドル**に戻った。「今の文書を閉じてから次を開く」だけでなく、
**開いたまま裏に残して行き来する**が実際に動く。

### 未保存の変更がある文書は `CloseDocument()` では閉じられない

新しい文書へ切り替え、矩形を 1 つ作って（＝未保存の変更を作って）から `CloseDocument()` を
呼ぶと **`false`** が返った。**閉じられない。**

- **保存確認ダイアログが出たかどうかは【未確認】**（目視で確かめる項目）。ただし
  **プローブ全体が 0.73 秒で終わっている**ので、入力を待つモーダルは出ていないと
  考えられる【推定】。つまりこの `false` は「ダイアログで『キャンセル』が選ばれた」
  ではなく、**呼び出しがそのまま拒否された**という読みになる。
- ヘッダにも保存の要否を指定する引数は無い（引数なし）。**「保存せずに閉じる」手段は
  現時点で見つかっていない。** どうしても閉じたいなら `SaveActiveDocumentPath` で
  捨ててよい場所へ保存してから閉じる、という遠回りになる【未確認】。

## 未確認のまま残っているもの

- **`.sta`（テンプレート）ファイルそのものを `OpenDocumentPath` へ渡したときの挙動。**
  「テンプレートを元にした無題の新規文書が開く」のか「`.sta` 自体を開いてしまう」のか。
- **実在するファイルを `OpenDocumentPath` で開いたときの挙動。** 上記の実測は
  `nullptr`（空の新規文書）だけで、パスを渡す経路は SDK 自身の自動テストの用例
  （【ヘッダ根拠】）までしか確かめていない。
- **保存確認ダイアログの有無**（前節）。
- **アクティブでない文書のハンドルへの書き込み**（前節）。
- **自分で undo イベントを開いたまま文書を開いた場合**（前節）。

## 結論（プラグイン開発への示唆）

- **「周ごとに新しい空の図面を開いて、そこへ描く」は実機で成立する。**
  `OpenDocumentPath(nullptr, false)` をコマンド実行中に呼べばよく、カレント文書も
  描画先も即座に移る。前の文書のハンドルも生きたままで、`SwitchToOpenFile` で戻れる。
- **ただしテンプレートの初期状態は再現できない**（テンプレート専用の新規作成 API が
  無く、開けるのは「空の」新規文書だけ）。通り芯用の「共通」レイヤのような
  **テンプレート由来の土台が要る取り込みでは、この方式は代わりにならない**。
- **後始末（前の文書を閉じる）は当てにしない。** 未保存の変更がある文書に
  `CloseDocument()` は効かない（`false`）。**開いたまま裏に残して `SwitchToOpenFile` で
  行き来する**か、文書を閉じる判断は利用者に委ねる。
- したがってホームズ君 IFC 取り込みプラグインの「前の周の図を消す」用途では、
  **引き続き「前回作ったレイヤを `DeleteObject` で消す」方式**（[Undo](Undo.md)）を使い、
  テンプレート由来の消せない土台へ描いた分はクラスで見分けて個別に消す。
  「新しい図面を開く」はその代替にはならないが、**取り込み先を汚さずに試したい**
  といった場面では使える。
