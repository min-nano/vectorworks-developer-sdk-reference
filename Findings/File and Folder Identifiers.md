# ファイル・フォルダの識別子（`IFileIdentifier` / `IFolderIdentifier`）

ディスク上の場所を指す 2 つの識別子そのものの話——**属性（`GetAttributes`）・実在の確認
（`ExistsOnDisk`）・中身の列挙（`EnumerateContents`）が、実機で何を返すか**
（[issue #87](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/87)）。

識別子の作り方・`Set` の使い分け・相対パスの組み立ては公式リファレンスにある
（[`Info/Working with File Identifiers.md`](../Info/Working%20with%20File%20Identifiers.md)、
[`Info/Enumerating folder contents.md`](../Info/Enumerating%20folder%20contents.md)）。
**ここに足すのは、公式に書かれていない「呼んだら何が返るか」だけ**である。
ダイアログで選ばせる話は [File and Folder Dialogs](File%20and%20Folder%20Dialogs.md)。

## 結論

| 問い | 答え |
| --- | --- |
| `fbDirectory` で「フォルダかファイルか」を判定できるか | **できない。フォルダでもファイルでも常に `false`。** 掴み方・場所を変えた 34 件すべてで `false` だった（下記） |
| `SAttributes` は埋まっていないのか | **埋まってはいる。** `fbReadOnly` / `fbCanRead` / `fbCanWrite` の 3 つは標本の中で実際に値が変わった。使えないのは `fbDirectory` である |
| `GetAttributes` の戻り値は実在の判定に使えるか | **使える**（実在しない場所では失敗する）。ただし**フォルダとファイルで返るエラー番号が違う**ので、特定の定数と比べず `VCOM_SUCCEEDED` で見る |
| `ExistsOnDisk` の戻り値で実在が分かるか | **分からない。** 実在しなくても戻り値は `0`（成功）で、**答えは出力引数のほうにある** |
| では種類の判定はどうするか | **判定しなくて済むように書く。** `IFolderIdentifier` を持っている時点でフォルダ、`IFileIdentifier` を持っている時点でファイルである。どうしても混在を捌くなら `EnumerateContents` の振り分け（`OnFolderContent` / `OnFileContent`）を使う——**SDK 自身の分類はそちらに出ている** |

## `SAttributes` の中身（ヘッダ）

`GetAttributes` / `SetAttributes` が使う構造体は `bool` 11 個で、**`IFolderIdentifier` と
`IFileIdentifier` で同じものを共有する**（`Interfaces/VectorWorks/Filing/IFolderIdentifier.h`）。

```cpp
struct SAttributes
{
    bool fbReadOnly;
    bool fbHidden;
    bool fbSystem;
    bool fbTemporary;
    bool fbEncrypted;
    bool fbArchive;
    bool fbDirectory;
    bool fbCanRead;
    bool fbCanWrite;
    bool fbCanExecute;
    bool fbCanBrowse;
};
```

**`SAttributes` は SDK 同梱の実装ソース（`SDKLib/Source`）のどこからも使われていない**
——宣言が上のヘッダと `IFileIdentifier.h` にあるだけで、用例も実装も付いてこない
（`sdk-grep` で確認）。中身を詰めるのは Vectorworks 本体側なので、**ヘッダを読んでも
何が入るかは分からない**。以下はすべて実機で測った値である。

## 実機で確かめたこと（VW 2026 / macOS / Apple Silicon）

**プラグインのメニューコマンドの中（`DoInterface` の中）から、外部モジュールへ出した
本体（ペイロード）側で**、実機確認プラグイン（VwSdkProbes）のプローブとして走らせた
実測。**印の無い記述は実機確認済み。** プローブは 1 回走らせ、落ちていない。

**標本は 34 件**（フォルダ 30・ファイル 4）。`fbDirectory` が場所や掴み方に依存しないことを
言い切るために、次のように散らしてある。

| 何を測ったか | 件数 |
| --- | --- |
| OS 標準のフォルダ（`Set(EOSFolderSpecifier)` の全 4 値。`/Applications/`・`~/Documents/`・`~/Library/Application Support/…`・`/private/var/…/T/TemporaryItems/`） | 4 |
| VW の導入先基準のフォルダ（`kApplicationFolder` / `kExternalsFolder` / `kAppDataFolder`） | 3 |
| **その場で `CreateOnDisk()` で作った新品のローカルフォルダ**（作った後・絶対パスで掴み直したもの） | 2 |
| **`EnumerateContents` が `OnFolderContent` へ配ったフォルダ**（`/Applications/VW2026/` の直下） | 17 |
| 同じものを絶対パスから掴み直したフォルダ | 1 |
| クラウドストレージ（`~/Library/CloudStorage/` 自体と、その直下の `Box-Box` / `GoogleDrive-…`） | 3 |
| ファイル（`.DS_Store` ×2・`VWVersion.txt`・絶対パスで掴み直した `.DS_Store`） | 4 |

### `fbDirectory` は**フォルダでもファイルでも常に `false`**

```
フォルダ: 測れた=30  そのうち fbDirectory=yes は 0 件
ファイル: 測れた=4   そのうち fbDirectory=yes は 0 件
```

**1 件も `true` にならなかった。** 場所（ローカル / VW の導入先 / 一時フォルダ /
クラウドストレージ）も、掴み方（`EOSFolderSpecifier` / `EFolderSpecifier` /
`Set(親, 名前)` / 絶対パス文字列 / 列挙が配ったポインタ / ダイアログが返したもの）も
変えたが、結果は同じである。

とくに効くのが次の 2 つ。

- **その場で作った新品のローカルフォルダ**（`/private/var/…/T/TemporaryItems/VwSdkProbes-folder-attributes/`）でも `fbDirectory=no`。同期も特別扱いも絡まない、いちばん素朴なフォルダである。
- **`EnumerateContents` が `OnFolderContent` へ配った 17 件**でも `fbDirectory=no`。
  **SDK 自身が「これはフォルダだ」と分類して渡してきた対象**が `false` を返すのだから、
  これは「掴み方が悪い」でも「その場所が変わっている」でもない。

したがって [File and Folder Dialogs](File%20and%20Folder%20Dialogs.md) で
Google ドライブ配下のフォルダについて観測された `fbDirectory=no` は、**その場所に固有の
話ではなかった**。同じ実行の中で `~/Library/CloudStorage/` 配下も測っており、ローカルと
差は出ていない。

> **【未確認】Windows では測っていない。** 上はすべて macOS の実測である。

### 他の 10 項目——`fbReadOnly` / `fbCanRead` / `fbCanWrite` は生きている

**「`GetAttributes` は成功を返すだけで中身を埋めていない」わけではない。** 34 件のうち
2 件で既定と違う値が出ており、そこが「埋めている」証拠になる。

| 標本 | `fbReadOnly` | `fbCanRead` | `fbCanWrite` |
| --- | --- | --- | --- |
| 上記以外の 32 件（フォルダ・ファイルとも） | `no` | `yes` | `yes` |
| `SystemTempDirectory`（`/private/var/…/T/TemporaryItems/`） | `no` | **`no`** | `yes` |
| Google ドライブのアカウント直下（`~/Library/CloudStorage/GoogleDrive-…/`） | **`yes`** | `yes` | **`no`** |

- **`fbReadOnly` と `fbCanWrite` は連動して動いた**（Google ドライブのアカウント直下は
  実際に書き込めない場所である）。**書けるかどうかの当たりを付ける用途には使える。**
- `SystemTempDirectory` の `fbCanRead=no` は理由が付けられない（同じ場所へフォルダを
  作って消すことは成功している）。**`fbCanRead` を単独で信じない**。

残る 7 つ——**`fbDirectory` / `fbHidden` / `fbSystem` / `fbTemporary` / `fbEncrypted` /
`fbArchive` / `fbCanExecute` / `fbCanBrowse` は、34 件すべてで `no` だった**。
うち次の 3 つは「`true` が出てよさそうな標本」が含まれていながら `no` である。

- **`fbHidden`** … `.DS_Store`（先頭が `.` の隠しファイル）でも `no`。
  **dot ファイルの検出には使えない。**
- **`fbTemporary`** … 一時フォルダ（`/private/var/…/T/TemporaryItems/`）そのものでも `no`。
- **`fbCanExecute`** … `/Applications/VW2026/` でも `no`。中身を列挙できている
  （＝辿れている）フォルダなので、実際には実行（走査）権がある。

`fbSystem` / `fbEncrypted` / `fbArchive` は、**`true` になるはずの標本を用意していない**
ので「使えない」とまでは言えない（この調査の範囲外。測った 34 件で一度も立たなかった、
という事実だけを記録しておく）。

### `GetAttributes` は**実在しない場所では失敗する**——番号はフォルダとファイルで違う

| 対象 | `ExistsOnDisk` の出力 | `GetAttributes` の戻り値 |
| --- | --- | --- |
| 実在するフォルダ / ファイル | `yes` | `0` = `kVCOMError_NoError` |
| 実在しない**フォルダ** | `no` | **`51` = `kVCOMError_FileNotFound`** |
| 実在しない**ファイル** | `no` | **`1` = `kVCOMError_Failed`** |

同じ `IFolderIdentifier` で `CreateOnDisk()` の**前後を測る**と、`51` → `0` に変わる
（作る前は `51`、`CreateOnDisk()` が `0` を返した後は `0`）。つまり戻り値は実在に追随する。

**ただし番号を当てにしないこと**——同じ「無い」でもフォルダは `51`、ファイルは `1` が
返る。判定は `VCOM_SUCCEEDED(GetAttributes(attrs))` の真偽で行う。

失敗したとき、**呼び出し側が `{}` で 0 初期化した `SAttributes` は 1 つも書き換わらなかった**
（11 項目すべて `false` のまま）。**失敗時に受け取った構造体を読んではいけない**
——「全部 `false`」は「そういう属性だった」と見分けが付かない。

### `ExistsOnDisk` の戻り値は「実在するか」ではない

```
実在しないフォルダ: ExistsOnDisk=0（成功） 実在=no
```

**実在しなくても戻り値は `0`（成功）。** 「呼び出しが成功したか」と「実在したか」は
別物で、後者は出力引数（`bool&`）にしか出ない。**`if (VCOM_SUCCEEDED(folder->ExistsOnDisk(exists)))`
だけで分岐すると、無い場所を在るものとして扱う。** 必ず `exists` のほうを見る。
（[Investigation Techniques](Investigation%20Techniques.md)「戻り値を信じない」と同じ形の罠。）

### `EnumerateContents` の実測

`/Applications/VW2026/` を非再帰で列挙して `VCOMError=0`、**フォルダ 17 件・ファイル 2 件**。

- **振り分けは正しい。** フォルダは `OnFolderContent`、ファイルは `OnFileContent` へ来る
  ——`fbDirectory` が死んでいるのに対し、**こちらの分類は信用できる**（`Plant Database` /
  `Plug-Ins` / `Settings` / `VWHelp` はフォルダ側、`.DS_Store` / `VWVersion.txt` は
  ファイル側へ来た）。
- **隠しファイルも来る。** `.DS_Store` が `OnFileContent` へ配られた。先頭が `.` の項目を
  SDK 側では落としていない。
- **フォルダとファイルは混ざって届く**（種類ごとにまとまらない）。実測の順は
  `Plant Database` → `Plug-Ins` → `Settings` → `.DS_Store` → `VWHelp` → `VWVersion.txt`。
  **順序に依存した書き方をしない。**

### 【拾い物】`Set(EFolderSpecifier)` が返すパスは、ディスク上の実名と大文字小文字が違うことがある

同じ 1 つのフォルダについて、2 つの経路が**綴りの違う文字列**を返した。

```
kExternalsFolder が返した    : /Applications/VW2026/Plug-ins/
EnumerateContents が返した実名: /Applications/VW2026/Plug-Ins/    ← I が大文字
```

macOS の既定のファイルシステムは大文字小文字を区別しないので**どちらでも開けるが、
パスを文字列として比較すると一致しない**。「このフォルダは Plug-ins 配下か」を
`GetFullPath` の文字列比較で判定すると、経路によって答えが変わる。

## 実務上どう書くか

```cpp
using namespace VectorWorks::Filing;

// ✗ 種類の判定に使わない（フォルダでもファイルでも常に false）
SAttributes attrs = {};
folder->GetAttributes(attrs);
if (attrs.fbDirectory) { /* ここへは決して来ない */ }

// ✗ 戻り値だけ見ない（実在しなくても 0 が返る）
bool exists = false;
if (VCOM_SUCCEEDED(folder->ExistsOnDisk(exists))) { /* 実在の保証にならない */ }

// ○ 実在は出力引数で見る
bool exists = false;
if (VCOM_SUCCEEDED(folder->ExistsOnDisk(exists)) && exists) { /* ここは実在する */ }

// ○ 書けるかどうかの当たりは fbReadOnly / fbCanWrite で付けられる
//    （ただし当たりまで。実際に書けるかは書いてみないと分からない）
SAttributes attrs = {};
if (VCOM_SUCCEEDED(folder->GetAttributes(attrs)) && !attrs.fbReadOnly && attrs.fbCanWrite)
{
    // …
}

// ○ フォルダとファイルの混在は EnumerateContents の振り分けで捌く
class CListener : public IFolderContentListener
{
public:
    virtual EFolderContentListenerResult VCOM_CALLTYPE OnFolderContent(IFolderIdentifier*) override
    { /* ここへ来たものはフォルダ */ return eFolderContentListenerResult_Continue; }
    virtual EFolderContentListenerResult VCOM_CALLTYPE OnFileContent(IFileIdentifier*) override
    { /* ここへ来たものはファイル */ return eFolderContentListenerResult_Continue; }
};
```

## 未確認のまま残っているもの

いずれも **issue #87 の範囲外と決めていたもの**で、
[issue #91](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/91)
へ切り出してある。**分からなくても実務では困らない**——要るのは「フォルダかファイルか」
（＝判定しなくて済む書き方で回避できる）と「書けるか」（＝`fbReadOnly` / `fbCanWrite`
で当たりが付く）の 2 つだけである。

- **`fbSystem` / `fbEncrypted` / `fbArchive` が `true` になる標本。** 上の 34 件には
  含まれていないので、「立たない」のか「立つ標本が無かった」のかが区別できていない。
- **`SetAttributes(const SAttributes&)` の挙動。** 一度も呼んでいない。
- **Windows での `SAttributes`。** 上はすべて macOS の実測。`fbArchive` / `fbSystem` /
  `fbHidden` は Windows のファイル属性そのものの名前なので、**あちらでは埋まる見込みが
  ある**【推定】。
