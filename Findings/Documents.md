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
| `bool GetActiveDocument(IFileIdentifier** ppOutFileID, bool& outSaved)` | 現在アクティブな文書のパスと、**ディスクに保存されたことがあるか**を返す（**未保存の変更があるかではない**。下記「[未保存の変更ありは読めない](#未保存の変更ありは読めない)」） |
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

## テンプレート（`.sta`）から新規文書は作れる——**専用 API ではなく `OpenDocumentPath` で**

**結論: できる。** ただし「テンプレートから新規作成する」という名前の API は無く、
**`.sta` のパスを `OpenDocumentPath` へ渡す**という形になる。渡すと、**その `.sta` 自体では
なく、それを元にした無題の新規文書が開く**（実機確認済み。下記「`.sta` を渡すとテンプレート
として扱われる」）。

以下は「専用 API を探して見つからなかった」という経緯である——**探し方が見当違いだった**
（`New`/`Create` 系を探していたが、実際は `Open` 側にテンプレートの扱いが入っていた）。

SDK 全体（`Include` + `Source` の実装ソース）を次のパターンで検索したが、**該当なし**。

| 検索パターン | 結果 |
| --- | --- |
| `(New\|Open\|Create)[A-Za-z]*(Document\|Drawing\|File)` | ヒットは `OpenDocumentPath` 系・`OpenPDFDocument`・`CreateNewXMLDocument`（XML の話）など、いずれも文書「作成」とは無関係 |
| `GS_(New\|Create)[A-Za-z]*(Doc\|File\|Draw)` | 0 件 |
| `ISDK.{0,5}New[A-Za-z]*\(` | 0 件 |
| `NewFromTemplate\|CreateFromTemplate\|kFileTypeVWTemplate\|SaveAsTemplate\|NewDrawing` | 0 件 |

「テンプレートから新規作成する」専用の ISDK / VWFC の呼び出しは無い。

**【訂正】ただし「メニューコマンドを名前で起動する汎用 API 自体が無い」というかつての
根拠は誤りだった**——`ISDK::DoMenuName(name, chunkIndex)` が実在する
（[Plug-in Interop](Plug-in%20Interop.md) の 2。
[issue #217](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/217)）。
`DoMenuName` が実際にコマンドを実行することは実機で確かめてあるので、
**「新規」に当たるメニューコマンドの内部名が分かれば、そこから起こせる見込みがある**
（内部名そのものは未調査）。**ただし `DoMenuName` には引数を渡す口が無い**ので、「このテンプレートから」を
指定する手段はこの経路には無く、下記の `OpenDocumentPath` のほうが扱いやすい。

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

これは**テンプレートを選べない「空の」新規文書**である。**テンプレート由来の初期状態
（通り芯レイヤ・クラス・シンボル定義・スタイル）が要るなら、`nullptr` ではなく
`.sta` のパスを渡す**（同じ `OpenDocumentPath` で、下記のとおりテンプレート扱いになる）。
上の用例は自動テストの `setUp`/`tearDown`（`System Test Manager` から走らせる文脈）の
ものだが、**プラグインのメニューコマンド実行中（`DoInterface` の中）から呼んでも動く**
ことは実機で確かめた（下記「実機で確かめたこと」）。

## 既存ファイル（`.vwx` / `.sta`）をパス指定で開ける（実機確認済み）

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

**実機でも同じ形で開けた**（下記「実機で確かめたこと」）。`.sta` を渡したときの扱いも
実機で確かめてある（テンプレートとして扱われる。同じく下記）。

## 実機で確かめたこと（VW 2026 / macOS）

以下は**プラグインのメニューコマンドの中（`DoInterface` の中）から**実機確認プラグイン
（VwSdkProbes）でプローブを走らせた実測。**印の無い記述は実機確認済み。**
プローブは 3 度作り直し、3 回走らせている（`open-document` 1 回、`close-document` 2 回。
いずれも役目を終えたので削除済み）。**一度も落ちていない。**

**戻り値を信じないこと。** この節でいちばん値の張る教訓がこれである——
`CloseDocument()` も `SetObjectName()` も、**成功しているのに `false` を返す**
（下記）。文書まわりの成否は、**`GetOpenFilesList` の件数や、書いた値を読み戻して**
確かめる。

### コマンド実行中に呼んでよい

`gSDK->OpenDocumentPath(nullptr, false)` をコマンド実行中に呼んで **`true`**。落ちない。
`SwitchToOpenFile` / `CloseDocument` も同じ文脈で呼べた。下記
「[`CloseAllFilesAndQuitVectorworks`](#closeallfilesandquitvectorworks終了)」の
**プラグイン読み込み中には呼べない**のような「呼べる場面」の制約は、**少なくとも
コマンド実行中については無い**。

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

- **図面が 1 枚も無ければ `GetCurrentLayer()` は `nullptr`**（落ちない。全部閉じた後も起動
  直後も。件数と常に一致した）。**閉じた文書のレイヤと後で開いた文書のレイヤが同じアドレスで
  返った**ので、ハンドルの値で文書を同定しない（[Timers and Notifications](Timers%20and%20Notifications.md)
  の 4 の 8。mac / Windows で実測）。

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

**書くこともできる。** 別の文書 Y をアクティブにした状態で、文書 X のレイヤハンドルへ
`SetObjectName(layerX, "probe-g4-renamed")` を呼ぶと、**落ちずに名前が変わった**——
Y のまま読み戻しても、X へ切り替えてから読んでも `probe-g4-renamed` だった。

- **ただし `SetObjectName` は `false` を返した**（`CloseDocument` と同じ）。
  **成否を戻り値で判定しない。書いた値を読み戻して確かめる。**
- 確かめたのは**名前の書き換え 1 種類**だけである。図形を足す・消すのような、
  カレント文書やカレントレイヤを暗黙に見る呼び出しが同じように振る舞うかは別問題なので、
  **描くときは `SwitchToOpenFile` でその文書をアクティブにしてから**にするのが安全。

### 文書を開く・切り替える操作は undo イベントを開かない。**開いているイベントも壊さない**

`IsCurrentlyBuildingAnUndoEvent()` は開く前・開いた直後・`SwitchToOpenFile` で戻った後の
いずれも `no`（自分でイベントを開いていない場合）。

**自分で undo イベントを開いたまま文書を開いても、そのイベントは生きている。**
`IsCurrentlyBuildingAnUndoEvent()` は**文書ごとの状態**だった:

| 見た場所 | `building` |
| --- | --- |
| 文書 A でイベントを開き、中でレイヤを 1 枚作った直後 | `yes` |
| そこから `OpenDocumentPath(nullptr, false)` で開いた文書 B | **`no`** |
| **B から `SwitchToOpenFile` で A へ戻った直後** | **`yes`** |

A へ戻ればイベントは開いたままで、**その中で作ったレイヤも残っている**。つまり
B で見た `no` は「イベントが終わった」ではなく「**B にはイベントが無い**」という意味。

- **[Undo](Undo.md)「間接経路: スクリプトエンジン経由…」の事例とは別物**である。
  あちらでイベントを終わらせていたのは**取り消しの実行**であって、文書を開く操作では
  なかった（同じ形でスクリプトだけを差し替えて切り分け済み）。
- とはいえ、**イベントを開いたまま別の文書へ行って描く**のは、記録がどちらの文書に
  積まれるのかが曖昧になる。**イベントは開いた文書の中で閉じる**のが素直。

### `SwitchToOpenFile` で行き来できる

`SwitchToOpenFile(2)`（開く前の `fileRef`）は `true` を返し、**カレントレイヤは開く前と
同じハンドル**に戻った。「今の文書を閉じてから次を開く」だけでなく、
**開いたまま裏に残して行き来する**が実際に動く。

### `CloseDocument()` は **`false` を返しながら閉じている**——未保存の変更も黙って捨てる

**この節は一度書き換えている。** 最初は「未保存の変更がある文書は `CloseDocument()` では
閉じられない」と書いた。戻り値が `false` だったからである。しかし**開いている文書の件数を
前後で数えたら、ちゃんと減っていた**。

| 呼んだ場面 | 戻り値 | 所要 | `GetOpenFilesList` の件数 |
| --- | --- | --- | --- |
| 未保存の変更（矩形を 1 つ作った）がある無題文書 | `false` | 74ms | **3 → 2** |
| `SaveActiveDocumentPath` で保存済みの文書 | `false` | 135ms | **2 → 1** |
| 同上（別の走行、3 例） | `false` | 73 / 237 / 201ms | **いずれも 1 件減った** |

- **戻り値は当てにならない。** 5 回とも `false` を返しながら 5 回とも閉じている。
  **判定は `GetOpenFilesList` の件数でする。**
- **保存確認ダイアログは出ない。** どの回も 73〜237ms で戻っており、**人を待っていない**
  （モーダルが出れば秒単位になる。[Undo](Undo.md)「間接経路…」で実際に 18108ms を
  観測している）。**未保存の変更は黙って捨てられる。**
- したがって **`bShowErrorMessages` に相当する引数が無いのは困らない**——ダイアログが
  そもそも出ない。逆に言えば、**利用者に「保存しますか」を尋ねる機会も無い**ので、
  **人の作りかけを消しうる**。自分が開いた文書だけを閉じるなりの判断は**呼び出し側の
  責任**になる（`GetActiveDocument` の `outSaved` は**未保存の変更を表さない**ので、
  この判断には使えない。下記「未保存の変更ありは読めない」）。
- 「保存してから閉じる」も通る（`SaveActiveDocumentPath` が `GSError=0`、その後
  件数が減る）。ただし**保存しなくても閉じられる**ので、捨ててよい文書のためにわざわざ
  保存する必要は無い。

### `.sta` を渡すとテンプレートとして扱われる——**無題の新規文書が開く**

**この節が issue #34 の本題への答えである。**

`.sta` の拡張子を持つファイルのパスを `OpenDocumentPath` へ渡すと、**そのファイル自体では
なく、それを元にした無題の新規文書が開いた**:

```
G3: OpenDocumentPath(.sta, false) = true elapsed=467ms
G3: 開いた後のアクティブ文書: /Applications/VW2026/名称未設定 7（saved=no）
```

パスが `.sta` ではなく**無題**（`名称未設定 7` / `saved=no`）になっている。
**Finder で `.sta` をダブルクリックしたときと同じ扱い**である。

- **中身も引き継がれている。** この `.sta` は、直前まで編集していた文書を
  `SaveActiveDocumentPath` でその名前に保存したもので、中にレイヤ `probe-g1-inner` が
  あった。開いた無題文書のカレントレイヤが**その `probe-g1-inner`** だった——
  **テンプレートの中身を持った、まっさらな（未保存の）文書**が手に入る。
- したがって「テンプレートからの新規作成専用 API が無いので、テンプレート由来の初期状態は
  再現できない」という以前の結論は**誤り**だった。**`OpenDocumentPath` にテンプレートの
  パスを渡せばよい。**
- **`.vwx` を渡したときは無題にならない**（アクティブ文書のパスが渡したパスそのものに
  なり、`saved=yes`）。**扱いを分けているのは拡張子**だと読める【推定】——
  中身の形式まで見ているのかは確かめていない。

### 既に開いているファイルは、もう一度は開けない

いま開いている文書のファイルを `OpenDocumentPath` へ渡すと **`false`**（20ms、件数も
変わらない）。**閉じてから同じパスを渡すと `true`**（529ms）。

- これは**引っ掛かりやすい**。`SaveActiveDocumentPath` で保存すると**その文書自身が
  保存先のファイルになる**ので、「保存 → そのパスを開く」と続けて書くと必ず失敗する。
  （実際この調査の 1 度目で踏んで、`.sta` の判定を 1 周やり直した。）
- **失敗は速い**（20ms）。時間が掛かっていないことも、ダイアログではなく即座の拒否で
  あることの裏づけになる。

## 未保存の変更ありは読めない

**問い**（[issue #240](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/240)）:
開いている図面のうち、**保存済みの図面を変更した状態（未保存の変更あり）**を SDK から
取れるか。[vectorworks-plugin-command-line-interface](https://github.com/min-nano/vectorworks-plugin-command-line-interface)
の `quit` が、終了を頼む前にダイアログなしで「未保存の変更がある図面」を見つけたい。

**答え: 取れない。** 読める手掛かりは `GetActiveDocument` の `outSaved` と
`GetOpenFilesList` の `fIsInMemoryOnly` の 2 つだけで、**どちらも「ディスクに保存された
ことがあるか」を表す**。保存済みの図面に保存の確認が出る状態になっても値は変わらない。
macOS の窓の「編集済み」（`NSWindow` の `isDocumentEdited`）も、保存の確認とは食い違う。

### 読む口は 2 つしか無く、どちらも「ディスクにあるか」【ヘッダ根拠】

- **SDK に「変更あり」を読む宣言は無い。** `SDK Index/` を `Modified` / `Dirty` / `Changed` /
  `Saved` で引いて、文書の状態として見つかるのは**変更ありに「する」側**の
  `ISDK::MarkCurrentDocChanged()`（`ISDK.h:1386`。`kcbMarkCurrentDocChanged`）と、
  メニューの有効条件 `EMenuEnableFlags::DocChanged`（`MiniCadCallBacks.h:4950`）だけ。
  `IApplication::GetActiveDocument(IDocument**)` で取れる `IDocument` は設定ダイアログを
  出す 4 つの関数しか持たない（`IDocument.h:16-19`）。
- `outSaved` の意味は旧 API の説明文に書いてある（`APIBase.Legacy.Defs.h`、
  `GS_GetActiveDocumentN`）: 「The saved parameter indicates whether the document has been
  **saved to disk**. If it has not, then activeDocumentPath is only a file name.」
  ——**ディスクにあるか**であって、変更の有無ではない。

### 実測: 正解（「閉じる」で保存の確認が出るか）と突き合わせた

**実測**（VW 2026 / macOS arm64。`probes/runtime/doc-dirty-staged/` をローカルの Claude Code
から MCP ブリッジで回した＝**OS タイマーの刻みの中**（殻の `CFRunLoopTimer`）。ビルド
`3ba4ea7d79bb`。プローブは役目を終えたので削除済み）。

**測り方が肝なので先に書く。** 最初の測定（#241）は 1 回の実行の中で「変更 → 読む」を
していたので、VW が変更ありの印をコマンドや undo イベントの終わりに付けるなら読み損ねて
いる疑いがあった（利用者の指摘）。そこで:

- **1 回の実行で 1 段だけ進める。** ケースごとに「設定の実行（図面を開き、保存済みの
  ケースは別名で保存し、操作を 1 つする）→（取り消しのケースだけ）取り消す実行 →
  確かめる実行」と分け、**読むのは前の実行が返った後**にした。どの段の冒頭でも
  `IsCurrentlyBuildingAnUndoEvent()` は `no`（undo イベントは閉じている）。
- **正解を機械で取る。** 確かめる実行で `DoMenuName("Close", 0)`（メニューの「閉じる」。
  内部名 `Close` で引けた）を呼び、`kCFRunLoopCommonModes` のタイマーから
  `NSApp.modalWindow` を見て、保存の確認が出たかとその文言を記録した（出たら
  `abortModal` で閉じた。VW はこれを「保存しない」として扱い、図面は閉じた）。

| ケース | `outSaved` | `fIsInMemoryOnly` | 窓の「編集済み」 | 「閉じる」で保存の確認 |
| --- | --- | --- | --- | --- |
| 保存済み・無変更 | `yes` | `no` | `no` | **出ない**（91 ms で閉じた） |
| 保存済み・undo イベント付きで矩形 | `yes` | `no` | `yes` | **出た** |
| 保存済み・undo イベント付きで矩形 → 次の実行で `DoMenuName("Undo")` | `yes` | `no` | **`no`** | **出た** |
| 保存済み・`MarkCurrentDocChanged()` | `yes` | `no` | **`no`** | **出た** |
| 保存済み・undo イベントの外で矩形 | `yes` | `no` | `no` | **出ない**（90 ms。矩形は黙って捨てられた） |
| 無題（`OpenDocumentPath(nullptr)`）・無変更 | `no` | `yes` | `yes` | **出た** |
| 無題・undo イベント付きで矩形 | `no` | `yes` | `yes` | 出た |
| 無題・`MarkCurrentDocChanged()` | `no` | `yes` | `yes` | 出た |

確認の文言はどれも「ファイル「…」で行った変更を保存しますか? / 保存しない場合、変更は
失われます。 / [保存] / [保存しない] / [キャンセル]」（呼んでから 91〜145 ms で出た）。

- **`outSaved` と `fIsInMemoryOnly` は、全ケース・全段で `outSaved == !fIsInMemoryOnly`。**
  保存済みの 5 ケースはどれも `outSaved=yes` で、**確認が出る 3 ケースと出ない 2 ケースを
  見分けられない**。実行が返った後に読んでも同じ（同じ実行の中で読んだ #241 の 14 局面とも
  同じ）。**値が変わるのは保存した瞬間だけ**。
- **窓の「編集済み」も正解ではない。** 取り消した後と `MarkCurrentDocChanged()` の後は
  `no` なのに確認が出る。**この印は undo の段の位置を追っているだけ**と読める
  （undo イベント付きの変更で `yes`、取り消すと `no`、`MarkCurrentDocChanged()` では
  動かない）【推定】。無題の図面は開いた瞬間から `yes`。
- **VW の「変更あり」は取り消しでは消えない。** 変更してから取り消して保存時点の中身に
  戻しても、確認は出る。**消えるのは保存したときだけ**。
- **undo イベントの外の書き込みは「変更あり」にしない**（刻みの中で。[Timers and
  Notifications](Timers%20and%20Notifications.md) の 4 の 9 の【推定】をここで確かめた）。
  確認が出ないので、**閉じれば黙って捨てられる**。
- **無題の図面は、何もしていなくても確認が出る**（「閉じる」で）。
  - #230 で「新規の図面に刻みの中で線を引いただけでは、終了の確認が出なかった」のは、
    **その図面がブリッジによって既に一時ファイルへ保存されていた**ため——つまり上の表の
    「保存済み・undo の外の変更」と同じ形だった——と読める【推定】（ブリッジは
    実行のたびに自分が開いた図面を保存する。#220 から）。
- **取り消された図形は、保存するまで一覧に残る。** 取り消した後も `FirstMemberObj` /
  `NextObject` で数えた図形数は減らず、上書き保存した瞬間に減った（#242 の段ごとの測定）。
  **図形数で「取り消しが効いたか」を判定しない。** #241 の測定で「取り消しで戻らない」と
  見えたのはこれで、取り消し自体は効いていた（窓の「編集済み」が `no` に戻っていた）。
- **刻みの中から呼んでよい。** 上の測定はすべて OS タイマーの刻みの中で、落ちず、
  `GetActiveDocument` は 1 回 7〜94 µs（#241）。**図面 0 枚・起動直後の刻みでも
  `GetOpenFilesList()` は落ちない**（[Timers and Notifications](Timers%20and%20Notifications.md)
  の 4 の 1・4 の 8）。
- **同じパスへの上書き保存は通る**（`GSError=0`・パスも変わらない。#241・#242）。殻のブリッジの
  「既にあるファイルへの**別名**保存は通らなかった」（`plugin/src/McpBridge.cpp`）とは別の
  場面で、**その文書自身のファイルへの保存**は通る。
- 正解に使ったのは**「閉じる」の確認**で、**終了（`CloseAllFilesAndQuitVectorworks(true)`）の
  確認ではない**。保存済みの「無変更」「`MarkCurrentDocChanged`」「undo の外の変更」の 3 つは
  #230 の終了の実測と一致する（出ない／出る／出ない）ので、同じ判定だと見ている【推定】。
- Windows では測っていない。`outSaved` の意味は SDK の説明文どおりなので変わらないと見るが
  【推定】。窓の「編集済み」は macOS だけの口。

### `quit` の事前確認への帰結

- **ダイアログなしで分かるのは「無題の図面がある」までである。** `GetOpenFilesList` の
  `fIsInMemoryOnly=yes`（無題）を拾えば、その図面は**変更が無くても**保存の確認を出す
  （「閉じる」で実測。終了でも同じと見る【推定】）。**保存済みの図面に未保存の変更が
  あるかは、事前に知る口が無い。**
- **未保存の変更が無いときの `CloseAllFilesAndQuitVectorworks(true, false)` は、確認なしで
  終わる。** 確認があるときは**キャンセルされても呼び出しから戻り、図面は残る**（件数で
  分かる）。どちらも #230 で実測済み（下記「[呼んだ後の振る舞い](#呼んだ後の振る舞いmac--windows-で実測)」）。
  **issue #240 の 5・6 はこれで答えが出ている**——確認は呼び出しの時点の状態で決まるので、
  「確かめてから呼ぶまでの間に変更ができた」場合も、ふつうに確認が出て、キャンセルなら戻る。
- したがって「確認を出さない」を保証する手は、事前確認ではなく次のどれかになる:
  1. **保存してから終える**——開いている保存済みの図面を順に `SwitchToOpenFile` →
     `SaveActiveDocumentPath(<その図面自身のパス>)` で上書き保存すれば、変更の有無に
     関わらず確認の種は消える（上書き保存が通ること、保存で「変更あり」が消えることは
     上記のとおり実測）。**人の変更を黙ってディスクへ書く**ので、明示の指定（`--save`
     など）を要する設計になる。無題の図面には保存先が無いので、この手では消せない。
  2. **捨てて終える**——`CloseAllFilesAndQuitVectorworks(false, …)`（確認なしで閉じる）。
     **人の変更を黙って捨てる**ので、同じく明示の指定（`--force` など）でだけ使う。
  3. **確認を出す前提で呼ぶ**——ダイアログが出うることを受け入れ、キャンセルされたら
     （戻った後も図面が残っていたら）エラーで応える。
  事前確認で拾えるのは無題の図面だけなので、それを「未保存の一覧」として返すなら
  **「保存済みの図面の変更は含まれない」**ことを利用者に言わなければならない。
- **自分で数えて持つ【推定・未検証】。** `kNotifyDocPostSaved` / `kNotifyDocOpen` で「保存
  時点」を記録し、`kNotifyUndoEndEvent` で「変更あり」を立てる作りは考えられる。上の表で
  VW の「変更あり」が立ったのは **undo イベント付きの変更**と **`MarkCurrentDocChanged()`** で、
  **取り消しでは消えない**ので、「保存以降に undo イベントが 1 つでも閉じたか」は表と合う。
  ただし **`MarkCurrentDocChanged()` は通知なしに変更ありにする**ので（他のプラグインが
  呼べば見えない）、**正確な代わりにはならない**。

## 指定した図面を保存する・閉じる（前面に依存せず）

**問い**（[issue #243](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/243)）:
[vectorworks-plugin-command-line-interface](https://github.com/min-nano/vectorworks-plugin-command-line-interface)
の `save` / `close` は、前面の図面ではなく**引数（パス）で指定した図面**を扱う。その図面を
新しいパスへ保存できるか、保存・閉じるの後に前面を元に戻せるか、「閉じる」で保存の確認が
いつ出るか。

**実測**（VW 2026 / macOS arm64。`probes/runtime/doc-save-close/`・`doc-close-confirm/`・
`doc-close-next-tick/` をローカルの Claude Code から MCP ブリッジで回した＝**OS タイマーの
刻みの中**から呼んだ。ビルド `f9a1aa1bf5a2` / `f94a82d23e07` / `4063818092fa`。保存の確認と
警告のボタンだけ人が押した。プローブは役目を終えたので削除済み）。

### 手順——`GetOpenFilesList` で引き、`SwitchToOpenFile` で前へ出し、前面を戻す

```cpp
// 1. 前面を覚える（fileRef とカレントレイヤ）
// 2. GetOpenFilesList から、パスが一致する項目の fFileRef を引く
gSDK->SwitchToOpenFile(targetRef);
gSDK->SaveActiveDocumentPath(fileID);   // 保存（新しいパス / 自分のパス）
gSDK->DoMenuName("Close", 0);           // 閉じる（確認が出うる。下記）
// 3. 閉じた後の前面は「元の前面」とは限らない → 覚えた fileRef へ戻す
gSDK->SwitchToOpenFile(originRef);
```

- **前面は戻せる。** 裏の図面へ切り替えて保存し、元の図面へ `SwitchToOpenFile` で戻すと、
  アクティブの `fileRef` も**カレントレイヤのハンドルも**元どおりだった。裏の図面を閉じた後に
  戻しても同じ（カレントレイヤは同じハンドル）。
- **閉じた後に前へ出るのは、元の前面ではない。** 前面の図面を「閉じる」と、アクティブは
  **別の図面**（この実測ではいちばん最近開いた図面）になった。元の前面へ戻すのは呼ぶ側の仕事。
- **図面は「パス」で一意に決まる。** 同じファイル名 `same.vwx` を別のフォルダに保存した
  2 枚は、`GetOpenFilesList` のパスで見分けられた（名前は同じ）。**無題の図面のパスは
  「アプリケーションのフォルダ＋名称未設定 N」**（`/Applications/VW2026/名称未設定 15` /
  `16` …）で、N はセッション全体の連番（上記「カレント文書と…」のとおり）。開いている間は
  重ならないが、**名前はファイルとしては存在しない**ので、無題の図面を指すなら `fileRef`
  のほうが確か。

### `SaveActiveDocumentPath`——新しいパスへは通る。既存のファイルへは通らない

| 保存する図面 | 保存先 | `GSError` | 所要 | ダイアログ | 保存後 |
| --- | --- | --- | --- | --- | --- |
| 無題（未保存） | **まだ無いパス** | **0** | 34 / 25 / 27 / 28 ms | なし | 図面のパスが保存先になり、`fIsInMemoryOnly=no`・`outSaved=yes` |
| 保存済み | **まだ無いパス**（別名保存） | **0** | 21 / 68 ms | なし | **図面のパスが保存先へ移る**。元のファイルはそのまま（大きさ・更新時刻とも不変） |
| 保存済み | 自分のパス（上書き保存） | 0 | 43 ms | なし | パスは変わらない（#240 と同じ） |
| 保存済み | **ディスクにあるが開いていない**ファイル | **50331649**（`0x03000001`） | 5 ms | **なし** | 何も起きない（保存先も図面も不変） |
| 無題 | 同上 | **50331649** | 3 ms | **なし** | 同上 |
| 無題 | **別の図面が開いている**ファイル | **50331649** | （人待ち） | **警告「このファイルはすでに開かれています。」**（OK だけ） | 何も起きない |

- **別名保存は「名前を付けて保存」と同じく、図面ごと保存先へ移る。** 元のファイルは
  保存した時点の中身のまま残り、**別の図面として開き直せた**（図形数も保存した時点の 2 つ。
  開き直した図面には新しい `fileRef` が付く）。
- **既存のファイルを上書きする別名保存はできない。** 開いていないファイルが相手なら、**確認も
  警告も出さずに**エラーを返す（ファイルには触れない）。開いているファイルが相手だと
  **警告のダイアログが出て、人が OK を押すまで戻らない**——SDK の説明文の「ダイアログを
  出さない（エラー時のアラートを除く）」の「アラート」がこれ。**呼ぶ前に `GetOpenFilesList`
  のパスと突き合わせて断る**のが安全（ダイアログなしで避けられるのはこちら側だけ）。
  上書きしたいなら、**呼ぶ前に呼ぶ側がファイルを消す**しかない【推定：消してから保存は試していない】。
- 殻のブリッジの「既にあるファイルへの別名保存は通らなかった」（`plugin/src/McpBridge.cpp`）は
  この 50331649 の場面である。
- 保存先が一時フォルダ（`$TMPDIR` の下）でも通った。

### 「閉じる」（`DoMenuName("Close", 0)`）——**保存した直後なら確認なしに閉じる**。確認の有無は「保存済みか」では決まらない

閉じたかは `GetOpenFilesList` の件数と、対象の `fileRef` が残っているかで見た。戻り値は
**どの回も 0**（閉じても、確認をキャンセルしても）なので、**戻り値で成否は分からない**。

| 閉じた図面 | 保存の確認 | 所要 | 結果 |
| --- | --- | --- | --- |
| 新しいパスへ保存した直後（別名保存した図面・無題から保存した図面） | **出ない** | 154 / 110 ms | 閉じた（件数 −1） |
| 裏から呼び出して保存した直後 | **出ない** | 73 ms | 閉じた |
| **開き直して**、自分のパスへ上書き保存した直後 | **出ない** | 81 ms | 閉じた |
| **保存済みのファイルを `OpenDocumentPath` で開いただけ**（何もしていない） | **出た**（「ファイル『q1-untitled.vwx』で行った変更を保存しますか?」） | （人待ち） | キャンセル → **戻る。図面は残る** |
| 同上（別のファイルで再現） | **出た** | （人待ち） | キャンセル → 戻る。図面は残る |
| 無題・無変更 | **出た** | （人待ち） | キャンセル → 戻る。図面は残る |
| **保存した後に矩形を足した**（undo イベントの外） | **出ない** | **73 ms** | **閉じた——足した矩形は黙って捨てられた** |
| **前の実行で**保存して矩形を足し、制御を返してから閉じた | **出ない** | 75 ms | 閉じた（矩形は捨てられた。#242 と同じ） |
| **前の実行で**開き直し、制御を返してから閉じた | **出た** | （人待ち） | キャンセル → 戻る。図面は残る |
| **前の実行で**保存し、制御を返してから閉じた（対照） | 出ない | 83 ms | 閉じた |

- **刻みの中から呼べる。** 落ちない。確認が出たときは、**人が押すまで呼び出しから戻らない**
  （刻みの中の処理がその間止まる）。**キャンセルされても呼び出しは戻り、図面は残る**
  ——件数が減っていないことで分かる。
- **`OpenDocumentPath` で開いただけの保存済みの図面でも、確認が出る。** 3 回とも再現した
  （別のファイル・別の走行。**開いた実行が返り、Vectorworks に制御が戻った後に閉じても出た**
  ——同じ実行の中で閉じたせいではない）。開いた後に**同じパスへ上書き保存してから**閉じると
  出ない。何が「変更あり」にしているのかは分からない。
- **undo イベントの外で足した図形は「変更あり」にしない——そして「閉じる」で黙って消える。**
  保存した後に `CreateRectangle` した図面は、同じ実行の中で閉じても、**制御を返した後の実行で
  閉じても**確認なしに閉じた。上記「[未保存の変更ありは読めない](#未保存の変更ありは読めない)」
  （#242）の「保存済み・undo イベントの外で矩形」と同じ結果である。**「確認が出ないから変更は
  無い」とは読めない**ので、図面に書く道具を持つプラグインは、閉じる前に**呼ぶ側が保存する**か、
  undo イベント付きで書く（または `MarkCurrentDocChanged()` で変更ありにしておく）。後者なら
  人の「閉じる」でも確認が出る（#242）。
- 前面の図面を閉じると、次に前へ出るのは別の図面（上記）。

### CLI の `save` / `close` への帰結

- **`save <パス> --to <新しいパス>`（無題の図面の保存・別名保存）は成り立つ。** 保存先が既に
  あるなら、呼ぶ前に断る（開いているファイルなら警告のダイアログが出てしまうため）。
- **`close` は「保存した直後」でないと確認が出うる**——無題・開いただけ・`MarkCurrentDocChanged()`
  の図面では出る。逆に、**刻みの中で描いただけの変更は確認なしに捨てられる**。確認が出て
  キャンセルされたら、件数が減っていないことで知り、エラーで応える（`quit` と同じ。上記
  「`quit` の事前確認への帰結」）。
- **「保存してから閉じる」の 2 手なら、確認は出ない**（3 例。開き直した図面でも出なかった）。
  呼ぶ側に保存の責任を持たせる設計（#243 の背景）と合う。

## `CloseAllFilesAndQuitVectorworks`（終了）

```cpp
// ISDK.h:3799
virtual void CloseAllFilesAndQuitVectorworks(bool bAskForSave = true, bool bRestart = false) = 0;
```

SDK の説明文は「Closes all files and then gracefully quits Vectorworks」。`bAskForSave` が
偽なら確認せずに閉じてそのまま終了、`bRestart` が真なら終了後に起動し直す。**戻り値は無く、
キャンセルされたときどうなるかも書かれていない**。SDK 自身のシステムテストは
`CloseAllFilesAndQuitVectorworks(false)` で呼んでいる（`VWFC/Tools/CatchSystemTest.cpp:87`）。

### プラグインの読み込み中には呼べない（import-ifc の実測・macOS）

[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
の [#48](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz/pull/48)
（アップデート後の再起動）で踏んだ。更新の確認が**起動中——スプラッシュの表示中、
`plugin_module_main` の中**——に走っていた頃、そこから呼ぶと **`bRestart` が真でも偽でも
「サポートファイルの読み込みに失敗しました。」が出て異常終了した**。**このリポジトリでは
再現させていない**（出所は上記のとおり実測だが、ここの記述はその転記）。

- 同じ呼び出しは**メニューコマンドの中**からも、**パレットの時計（ウェブパレットの JS
  タイマーから呼ばれる C++）の中**からも機能した（import-ifc の M23 / M38）。
- **OS のタイマーの刻みの中からも呼べる**（mac で実測。[Timers and
  Notifications](Timers%20and%20Notifications.md) の 4 の 9）。
- 以前このページは、この制約の出典を「Plug-in Modules」としていたが、**Plug-in Modules に
  その節があったことは一度も無い**（`git log -S` で確認）。

### 呼んだ後の振る舞い（mac / Windows で実測）

詳細と数値は [Timers and Notifications](Timers%20and%20Notifications.md) の 4 の 9。ここには
文書の側から見た要点だけを置く。

- **呼び出しは戻る。** 図面を閉じ終えてから戻り（保存済み・図面 0 枚なら mac 99〜196 ms・
  Windows 711〜1287 ms）、**プロセスはその後に正常に終わる**（`atexit` まで届く。mac は
  4〜8 秒後・Windows は 28〜30 秒後）。
- **保存の確認は「未保存の変更」がある文書にだけ出る**（`bAskForSave=true`）。
  保存済みの文書・図面 0 枚なら確認なしで終わる。
- **確認で「キャンセル」すると、呼び出しはそのまま戻り、図面は開いたまま残る。**
  Vectorworks は動き続け、`gSDK` もふつうに呼べる。**戻り値が無いので、キャンセルされたことは
  `GetOpenFilesList()` の件数（減っていない）か、プロセスが生きていることで知る。**
- **確認の最中にもう一度呼ぶと、確認が 2 つ重なって出る。** 落ちはしない。Windows では
  呼んだタイマー自身が確認の最中に入り直すので、「呼び出しの最中」を旗で持って見送る。
- **`kNotifyBeforeApplicationQuit` は確認の前に届き、キャンセルされても届く。**
  「本当に終わる」合図にはならない。本当に終わるときは、最後に `plugin_module_main` へ
  `kPluginModuleDeinit`（-2）が届く（`atexit` の約 0.1 秒前）。

## 結論（プラグイン開発への示唆）

- **「周ごとにテンプレートから新しい図面を開いて、そこへ描く」は実機で成立する。**
  コマンド実行中に **`OpenDocumentPath(<テンプレートの .sta>, false)`** を呼べばよい。
  **テンプレートの中身を持った無題の新規文書**が開き、カレント文書も描画先も即座に移る。
  当初この筋は「テンプレート専用 API が無いので不可」と結論していたが、**それは誤り**
  だった——`Open` 側に `.sta` の扱いが入っている。
- **前の文書は開いたままにしても、閉じてもよい。** ハンドルは生きていて
  `SwitchToOpenFile` で行き来でき、読みも（名前の書き換えという範囲では）書きもできる。
  閉じたいなら `CloseDocument()` で閉じられる——**未保存でも閉じられる**。
- **【危険】`CloseDocument()` は確認せずに捨てる。** 保存確認ダイアログは出ず、未保存の
  変更は黙って消える。**人が編集中の文書を閉じない**こと——自分が開いた文書だけを対象に
  する（`outSaved` は「ディスクにあるか」でしかなく、**保存済みの文書の変更は見えない**）。
- **「未保存の変更があるか」は SDK から読めない。** 分かるのは「一度もディスクへ保存されて
  いない」（`fIsInMemoryOnly` / `outSaved`）までで、保存済みの文書を変更した状態は
  どちらにも現れない（上記「未保存の変更ありは読めない」）。
- **成否を戻り値で判定しない。** `CloseDocument()` も `SetObjectName()` も、成功しながら
  `false` を返す。**`GetOpenFilesList` の件数**や**読み戻した値**で確かめる。
- **「保存 → そのパスを開く」は続けて書かない。** 保存した時点でその文書自身がその
  ファイルなので、開き直しは `false` になる。閉じてから開く。
