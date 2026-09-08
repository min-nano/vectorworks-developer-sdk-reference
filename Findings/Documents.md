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
  **人の作りかけを消しうる**。呼ぶ前に `GetActiveDocument` の `outSaved` を見るなり、
  自分が開いた文書だけを閉じるなりの判断は**呼び出し側の責任**になる。
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
  するか、`GetActiveDocument` の `outSaved` を見て判断する。
- **成否を戻り値で判定しない。** `CloseDocument()` も `SetObjectName()` も、成功しながら
  `false` を返す。**`GetOpenFilesList` の件数**や**読み戻した値**で確かめる。
- **「保存 → そのパスを開く」は続けて書かない。** 保存した時点でその文書自身がその
  ファイルなので、開き直しは `false` になる。閉じてから開く。
