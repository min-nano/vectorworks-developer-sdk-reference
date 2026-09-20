# ファイル・フォルダを選ばせるダイアログ

OS のファイル選択／フォルダ選択ダイアログを SDK から開き、選ばれたものの**絶対パスを
受け取る**まで（[issue #85](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/85)）。
「取り込む IFC はどれか」「どのフォルダを対象にするか」を利用者に指してもらう経路そのもの。

**`VWFC::VWUI::VWDialog` で自前に組むダイアログ（[Layout Dialogs](Layout%20Dialogs.md)）とは
別系統**である——こちらは VCOM のインターフェースを確保して **OS のダイアログ**を出す。
組み立てる部品は無く、見出しと説明文を渡して走らせるだけ。

公式リファレンスにも節がある（[`Info/Working with File-Folder Choose
Dialogs.md`](../Info/Working%20with%20File-Folder%20Choose%20Dialogs.md)）。**そこに載っている
呼び出しの綴りが現行の SDK（VW 2026）のヘッダと一致することは確認した**ので、あちらの
用例はそのまま使ってよい。ここに足すのは、公式に書かれていない**戻り値・受け取る文字列の
実物・本体（ペイロード）側から開けるか**である。

## 結論

**フォルダを 1 つ選ばせるダイアログは在る。** 専用のインターフェース
`IFolderChooserDialog` がそれで、`IFileChooserDialog` とは**別物**である。

| 問い | 答え |
| --- | --- |
| フォルダ専用のインターフェースはあるか | **ある**。`IFolderChooserDialog` / `IID_FolderChooserDialog`（`{A162E405-E859-4e0c-B5C8-959C9BEC0565}`）。ヘッダは `Interfaces/VectorWorks/Filing/IFolderChooserDialog.h` |
| `IFileChooserDialog` でフォルダを選べるか | **選べない**。実行する口は `RunOpenDialog` / `RunSaveDialog` の 2 つだけで、`SetCanChooseDirectories` 相当の設定も無い |
| 選ばれたフォルダの絶対パスの取り出し方 | `GetSelectedPath(IFolderIdentifier**)` → `IFolderIdentifier::GetFullPath(TXString&)` |
| ファイルから親フォルダを取れるか | **取れる**。`IFileIdentifier::GetFolder(IFolderIdentifier**)` |

**つまり「フォルダの中のファイルを選ばせて親を採る」回避策は要らない。** 見出しで
「フォルダではなくその中のファイルを選んでください」と断る必要が無くなり、**中に対象の
ファイルが 1 つも無いフォルダも指せる**（フォルダそのものを選ぶダイアログで、中身を条件に
する設定は上記のとおり存在しない。【推定】——空のフォルダを指す操作そのものは実機で
試していない）。既にその形で書いてあるコードも、`GetFolder` を使えば
`std::filesystem::path::parent_path()` を自前で持たなくて済む（下記のとおり、**両者は
同じ文字列を返す**ことを実測した）。

## `IFolderChooserDialog` の全体（口は 5 つだけ）

```cpp
// Interfaces/VectorWorks/Filing/IFolderChooserDialog.h（全文に近い）
static const VWIID IID_FolderChooserDialog = { 0xa162e405, 0xe859, 0x4e0c,
                                               { 0xb5, 0xc8, 0x95, 0x9c, 0x9b, 0xec, 0x5, 0x65 } };

class DYNAMIC_ATTRIBUTE IFolderChooserDialog : public IVWUnknown
{
public:
    virtual VCOMError VCOM_CALLTYPE	SetTitle(const TXString& title) = 0;
    virtual VCOMError VCOM_CALLTYPE	SetDescription(const TXString& desc) = 0;

    virtual VCOMError VCOM_CALLTYPE	GetSelectedPath(IFolderIdentifier** ppOutFolderID) = 0;
    virtual VCOMError VCOM_CALLTYPE	SetSelectedPath(IFolderIdentifier* pFolderID) = 0;

    virtual VCOMError VCOM_CALLTYPE	RunDialog() = 0;
};

typedef VCOMPtr<IFolderChooserDialog>		IFolderChooserDialogPtr;
```

`IFileChooserDialog` にある**フィルタ・複数選択・既定の拡張子・初期フォルダ
（`SetInitialFolder`）は、フォルダ側には無い**。初期位置を与える口は
`SetSelectedPath` の 1 つだけである。

使い方（実機で動かした形そのまま）:

```cpp
using namespace VectorWorks::Filing;

IFolderChooserDialogPtr folderDlg(IID_FolderChooserDialog);
folderDlg->SetTitle("取り込む物件の入ったフォルダを選んでください");
folderDlg->SetDescription("選んだフォルダの中の IFC を順に取り込みます");

if (VCOM_SUCCEEDED(folderDlg->RunDialog()))
{
    IFolderIdentifierPtr folder;
    if (VCOM_SUCCEEDED(folderDlg->GetSelectedPath(&folder)))
    {
        TXString fullPath;   // UTF-8 の絶対パス。末尾に区切り（/）が付く
        folder->GetFullPath(fullPath);
    }
}
```

## 綴りが `IFileIdentifier` と揃っていない

同じ `VectorWorks::Filing` に並んでいて、**同じ意味の口の名前が違う**。取り違えると
コンパイルが通らないだけなので実害は小さいが、当たりを付けるときに引っかかる。

| 意味 | `IFileIdentifier` | `IFolderIdentifier` |
| --- | --- | --- |
| 絶対パスを返す | `GetFileFullPath(TXString&)` | **`GetFullPath(TXString&)`**（`File` が入らない） |
| 名前だけを返す | `GetFileName(TXString&)` | **`GetName(TXString&)`** |
| 親フォルダを返す | `GetFolder(IFolderIdentifier**)` | `GetParentFolder(IFolderIdentifier**)` |

## 実機で確かめたこと（VW 2026 / macOS / Apple Silicon）

**プラグインのメニューコマンドの中（`DoInterface` の中）から、しかも外部モジュールへ
出した本体（ペイロード）側で**、実機確認プラグイン（VwSdkProbes）のプローブとして
走らせた実測。**印の無い記述は実機確認済み。** プローブは 1 回走らせ、落ちていない。

### 本体（ペイロード）側から開ける

`IFolderChooserDialogPtr folderDlg(IID_FolderChooserDialog);` が**非 nullptr**を返し、
`RunDialog()` が実際に OS のフォルダ選択ダイアログを出した。[Plug-in
Modules](Plug-in%20Modules.md)「本体側からダイアログを開ける」に `IFileChooserDialog` で
確かめてある話と同じことが、**別インターフェースの `IFolderChooserDialog` でも成り立つ**。

`SetTitle` / `SetDescription` に**日本語の UTF-8 リテラルを渡して `VCOMError=0`**。
（見出しが実際にどう出たかは目視の話なので、ここでは戻り値だけを言う。）

### 戻り値: **キャンセルは `RunDialog` の失敗として返る**

| 呼び出し | 選ばれたとき | キャンセルしたとき |
| --- | --- | --- |
| `RunDialog()` | `0` = `kVCOMError_NoError` | **`1` = `kVCOMError_Failed`** |
| `GetSelectedPath(&folder)` | `0`、ポインタは非 nullptr | **`15` = `kVCOMError_InvalidArg`。ただしポインタは非 nullptr** |
| `folder->GetFullPath(path)` | `0`、下記のパス | **`50` = `kVCOMError_BadPathSpecified`**、`path` は**空文字列** |

**キャンセルは `kVCOMError_Canceled`（`16`）では返ってこない。** その定数は
`Kernel/API/MiniCadHookIntf.h` に実在するが、ここで返るのは汎用の `kVCOMError_Failed`
なので、**「キャンセルされた」と「ダイアログを出せなかった」を戻り値で見分ける手段は
無い**。判定は `VCOM_SUCCEEDED(RunDialog())` の真偽だけでよい（マクロの定義は
`#define VCOM_SUCCEEDED(x) (x == kVCOMError_NoError)` で、**0 との厳密な一致**である）。

**「ポインタが取れたか」で分岐してはいけない。** キャンセルされても `GetSelectedPath` は
**非 nullptr のポインタを返してくる**ので、そこで分岐すると素通りする。取れた
`IFolderIdentifier` は中身が空で、`GetFullPath` が空文字列と `50` を返すだけである
（つまり**空文字列を掴まされるのが最悪の壊れ方**で、黙って前回の値が残るようなことは無い）。

### 受け取るパス: 絶対パス・**末尾に区切りが付く**・UTF-8

選ばれたフォルダを Google ドライブ上の日本語名のフォルダにして測った実測値:

```
path = [/Users/…/共有ドライブ/02_アーカイブ/20260529_遠山信夫アトリエ_安藤邸/]
バイト数 = 160   非ASCII = yes   末尾の 1 バイト = [/]
GetName  = [20260529_遠山信夫アトリエ_安藤邸]      ← こちらは区切り無し
```

- **絶対パス**である（`/` 始まり）。
- **末尾に区切り（`/`）が付く。** ファイル名を継ぎ足すときに `/` を足すと `//` になる。
  そもそも**継ぎ足しは自前でやらず `IFileIdentifier::Set(IFolderIdentifier*, const
  TXString& fileName)` を使う**のが作法（[Documents](Documents.md) の `CatchSystemTest.cpp`
  の用例と同じ形）。
- **UTF-8 である。** この 160 という数字がその根拠——同じパスを UTF-8 で数えると、
  ASCII 部分 78 バイト＋`共有ドライブ/` 19＋`02_アーカイブ/` 19＋
  `20260529_遠山信夫アトリエ_安藤邸/` 44 で**ちょうど 160 バイト**になる（日本語 1 文字＝
  3 バイト）。UTF-16 でも Shift_JIS でもこの数にはならない。`GetName` の値もそのまま
  日本語として読める。**`TXString` の `operator const char*` は UTF-8 を返す**という
  [TXString](TXString.md) の前提が、ここでも保たれている。

### `IFileIdentifier::GetFolder` は**フォルダ選択とまったく同じ文字列**を返す

同じフォルダの中のファイル（`…/安藤邸.vwx`）を `IFileChooserDialog` で選ばせ、その
`IFileIdentifier::GetFolder()` から採った親フォルダのパスと、上のフォルダ選択で得た
パスを突き合わせた:

```
フォルダ選択の結果 = [/Users/…/20260529_遠山信夫アトリエ_安藤邸/]   160 バイト・末尾 [/]
ファイルの親       = [/Users/…/20260529_遠山信夫アトリエ_安藤邸/]   160 バイト・末尾 [/]
文字列として一致: yes
```

**末尾の区切りまで含めて 1 バイトも違わない。** だから「フォルダを選ばせる」と
「ファイルを選ばせて親を採る」は、**受け取るパスとしては等価**である——差は
利用者の操作（空のフォルダを指せるか・見出しで断りが要るか）だけ。

なお `IFileChooserDialog::SetInitialFolder(IFolderIdentifier*)` に、フォルダ選択で得た
`IFolderIdentifier` をそのまま渡して `VCOMError=0`。**フォルダ選択の結果を、続けて開く
ファイル選択の初期位置として渡せる**（初期位置として実際に効いたかは目視の話なので
言い切らない）。

### `GetAttributes` の `fbDirectory` は**フォルダでも `no` を返した**

上の 2 つのフォルダ（フォルダ選択の結果・ファイルの親）どちらも、
`GetAttributes` は `VCOMError=0` を返しながら **`fbDirectory=no`**（`fbCanRead=yes` /
`fbCanWrite=yes`）だった。**「フォルダかどうか」の判定に `fbDirectory` を使ってはいけない。**
実在の確認は `ExistsOnDisk` で行う——ただし**戻り値ではなく出力引数のほうを見る**
（実在しなくても戻り値は `0` を返す。[File and Folder Identifiers](File%20and%20Folder%20Identifiers.md)）。

**これはダイアログに固有の話でも、測った場所に固有の話でもない。**
[issue #87](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/87) で
掴み方と場所を散らした 34 件（ローカル・VW の導入先・その場で作った新品・
`EnumerateContents` が配ったもの・クラウドストレージ配下）を測り直したところ、
**フォルダ 30 件・ファイル 4 件のすべてで `fbDirectory=false`** だった。
`fbDirectory` は**常に `false`** だと思ってよい（macOS 実測。Windows は未確認）。
識別子そのものの話は
[File and Folder Identifiers](File%20and%20Folder%20Identifiers.md) にまとめてある。

## 使い分け

| やりたいこと | 使うもの |
| --- | --- |
| フォルダを 1 つ選ばせる | `IFolderChooserDialog`（`RunDialog` → `GetSelectedPath`） |
| ファイルを選ばせる（開く） | `IFileChooserDialog`（`RunOpenDialog` → `GetSelectedFileName`） |
| 保存先を選ばせる | `IFileChooserDialog`（`RunSaveDialog`） |
| 選ばせたファイルの入っているフォルダが欲しい | `IFileIdentifier::GetFolder` |
| フォルダ＋ファイル名からパスを組む | `IFileIdentifier::Set(IFolderIdentifier*, const TXString&)` |
| ダイアログを出さずに既定の場所を引く | `IFolderIdentifier::Set(EFolderSpecifier, bool)` / `Set(EOSFolderSpecifier)`（`ApplicationsDirectory` / `DocumentDirectory` / `ApplicationSupportDirectory` / `SystemTempDirectory`） |
