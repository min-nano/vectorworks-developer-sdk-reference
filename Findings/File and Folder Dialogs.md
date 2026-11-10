# ファイル・フォルダを選ばせるダイアログ

OS のファイル選択／フォルダ選択ダイアログを SDK から開き、選ばれたものの**絶対パスを
受け取る**まで（[issue #85](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/85)）。
「取り込む IFC はどれか」「どのフォルダを対象にするか」を利用者に指してもらう経路そのもの。
ヘッダの抜き書きと実測の値は [計測記録](../Metrics/File%20and%20Folder%20Dialogs.md)。

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

- **「フォルダの中のファイルを選ばせて親を採る」回避策は要らない。** 中に対象のファイルが
  1 つも無いフォルダも指せる（【推定】——空のフォルダを指す操作そのものは実機で試して
  いない）。既にその形のコードも、`GetFolder` を使えば
  `std::filesystem::path::parent_path()` を自前で持たなくて済む（両者は同じ文字列を返す）。

## `IFolderChooserDialog` の全体（口は 5 つだけ）

- 口は `SetTitle` / `SetDescription` / `GetSelectedPath` / `SetSelectedPath` / `RunDialog` の
  5 つ（[ヘッダの抜き書き](../Metrics/File%20and%20Folder%20Dialogs.md#ifolderchooserdialog-のヘッダ)）。
- **`IFileChooserDialog` にあるフィルタ・複数選択・既定の拡張子・初期フォルダ
  （`SetInitialFolder`）は、フォルダ側には無い。** 初期位置は `SetSelectedPath` だけ。

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

メニューコマンドの中（`DoInterface`）から、外部モジュールへ出した本体（ペイロード）側で
VwSdkProbes のプローブとして走らせた。**印の無い記述は実機確認済み**（macOS）。

### 本体（ペイロード）側から開ける

- **`IFolderChooserDialogPtr folderDlg(IID_FolderChooserDialog);` は本体側でも非 nullptr を
  返し、`RunDialog()` が OS のフォルダ選択ダイアログを出す**（[Plug-in
  Modules](Plug-in%20Modules.md)「本体側からダイアログを開ける」と同じ）。日本語の UTF-8 の
  見出しを渡して `VCOMError=0`（見え方は目視の話なので言わない）
  （[実測](../Metrics/File%20and%20Folder%20Dialogs.md#本体ペイロード側から開いた実測)）。

### 戻り値: **キャンセルは `RunDialog` の失敗として返る**

- **キャンセルは `kVCOMError_Failed`（`1`）で返り、`kVCOMError_Canceled`（`16`）ではない。**
  「キャンセル」と「出せなかった」は戻り値で見分けられない。判定は
  `VCOM_SUCCEEDED(RunDialog())`（0 との厳密な一致）の真偽だけでよい。
- **「ポインタが取れたか」で分岐してはいけない。** キャンセルでも `GetSelectedPath` は
  非 nullptr を返し、`GetFullPath` が空文字列と `50`（`kVCOMError_BadPathSpecified`）を返す
  ——空文字列を掴まされるのが最悪の壊れ方で、黙って前回の値が残るようなことは無い
  （[戻り値の表](../Metrics/File%20and%20Folder%20Dialogs.md#戻り値の実測選んだときキャンセルしたとき)）。

### 受け取るパス: 絶対パス・**末尾に区切りが付く**・UTF-8

- **絶対パス**（`/` 始まり）で、**末尾に `/` が付く**（`GetName` には付かない）。
  **UTF-8** である（日本語のパスのバイト数で確かめた）。
- **継ぎ足しは自前でやらず `IFileIdentifier::Set(IFolderIdentifier*, const TXString&
  fileName)` を使う**（自前で `/` を足すと `//` になる。[Documents](Documents.md)）
  （[実測](../Metrics/File%20and%20Folder%20Dialogs.md#受け取るパスの実測)）。
- **`TXString` の `operator const char*` は UTF-8 を返す**という [TXString](TXString.md) の
  前提がここでも保たれる。

### `IFileIdentifier::GetFolder` は**フォルダ選択とまったく同じ文字列**を返す

- **末尾の区切りまで含めて 1 バイトも違わない**ので、「フォルダを選ばせる」と「ファイルを
  選ばせて親を採る」は受け取るパスとしては等価——差は利用者の操作（空のフォルダを
  指せるか・見出しで断りが要るか）だけ。
- フォルダ選択で得た `IFolderIdentifier` を `IFileChooserDialog::SetInitialFolder` に渡して
  `VCOMError=0`（初期位置として効いたかは目視の話なので言い切らない）
  （[実測](../Metrics/File%20and%20Folder%20Dialogs.md#ifileidentifiergetfolder-とフォルダ選択の文字列の突き合わせ)）。

### `GetAttributes` の `fbDirectory` は**フォルダでも `no` を返した**

- **「フォルダかどうか」の判定に `fbDirectory` を使ってはいけない**——ダイアログに固有の
  話でも測った場所に固有の話でもなく、フォルダ 30 件・ファイル 4 件のすべてで `false`（[issue #87](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/87)。
  macOS 実測。Windows は未確認）。
- 実在の確認は `ExistsOnDisk` で、**戻り値ではなく出力引数を見る**
  （[File and Folder Identifiers](File%20and%20Folder%20Identifiers.md)。
  [実測](../Metrics/File%20and%20Folder%20Dialogs.md#getattributes-の-fbdirectory-はフォルダでも-no-を返した)）。

## 使い分け

| やりたいこと | 使うもの |
| --- | --- |
| フォルダを 1 つ選ばせる | `IFolderChooserDialog`（`RunDialog` → `GetSelectedPath`） |
| ファイルを選ばせる（開く） | `IFileChooserDialog`（`RunOpenDialog` → `GetSelectedFileName`） |
| 保存先を選ばせる | `IFileChooserDialog`（`RunSaveDialog`） |
| 選ばせたファイルの入っているフォルダが欲しい | `IFileIdentifier::GetFolder` |
| フォルダ＋ファイル名からパスを組む | `IFileIdentifier::Set(IFolderIdentifier*, const TXString&)` |
| ダイアログを出さずに既定の場所を引く | `IFolderIdentifier::Set(EFolderSpecifier, bool)` / `Set(EOSFolderSpecifier)`（`ApplicationsDirectory` / `DocumentDirectory` / `ApplicationSupportDirectory` / `SystemTempDirectory`） |
