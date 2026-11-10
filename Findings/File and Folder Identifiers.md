# ファイル・フォルダの識別子（`IFileIdentifier` / `IFolderIdentifier`）

ディスク上の場所を指す 2 つの識別子そのものの話——**属性（`GetAttributes` / `SetAttributes`）・
実在の確認（`ExistsOnDisk`）・中身の列挙（`EnumerateContents`）が、実機で何を返すか**
（[issue #87](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/87)、
[issue #91](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/91)、
[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93)）。
VW 2026 の macOS（Apple Silicon）と Windows 11（NTFS）で確認。標本・実測の表は
[計測記録](../Metrics/File%20and%20Folder%20Identifiers.md)。

識別子の作り方・`Set` の使い分け・相対パスの組み立ては公式リファレンスにある
（[`Info/Working with File Identifiers.md`](../Info/Working%20with%20File%20Identifiers.md)、
[`Info/Enumerating folder contents.md`](../Info/Enumerating%20folder%20contents.md)）。
**ここに足すのは、公式に書かれていない「呼んだら何が返るか」だけ**である。
ダイアログで選ばせる話は [File and Folder Dialogs](File%20and%20Folder%20Dialogs.md)。

## 結論

| 問い | 答え |
| --- | --- |
| `fbDirectory` で「フォルダかファイルか」を判定できるか | **できない。フォルダでもファイルでも常に `false`。** macOS で掴み方・場所を変えた 34 件、Windows でもフォルダ 10 件・ファイル 14 件のすべてで `false` だった（下記） |
| `SAttributes` は埋まっていないのか | **埋まってはいる。** `fbReadOnly` / `fbCanRead` / `fbCanWrite` の 3 つは標本の中で実際に値が変わった。使えないのは `fbDirectory` である |
| `GetAttributes` の戻り値は実在の判定に使えるか | **使える**（実在しない場所では失敗する）。ただし**フォルダとファイルで返るエラー番号が違う**ので、特定の定数と比べず `VCOM_SUCCEEDED` で見る |
| `ExistsOnDisk` の戻り値で実在が分かるか | **分からない。** 実在しなくても戻り値は `0`（成功）で、**答えは出力引数のほうにある** |
| `SetAttributes` で属性を書き戻せるか | **フォルダは両 OS とも不可**（`kVCOMError_NotImplemented`）。**ファイルは書けるが、書ける旗が OS で違う**——macOS では `fbReadOnly` / `fbCanWrite` / `fbCanExecute` / `fbCanRead` が POSIX の権限ビットへ届き、Windows では `fbReadOnly` / `fbCanWrite` / `fbHidden` / `fbSystem` / `fbTemporary` / `fbArchive` がファイル属性（`FILE_ATTRIBUTE_*`）へ届く。**それ以外は成功を返して捨てられる**（下記） |
| `fbHidden` / `fbSystem` / `fbArchive` / `fbTemporary` / `fbEncrypted` は使えるか | **Windows でだけ生きている。** Windows では `FILE_ATTRIBUTE_*` をそのまま映し（`fbEncrypted` は読むだけ）、macOS では常に `no`。**プラットフォーム差として確定した**（下記「macOS と Windows の比較」） |
| 書いた `fbCanRead` を読み戻せるか | **読み戻せない。書けるのに読めない。** 権限ビットは実際に変わるのに、`GetAttributes` は `yes` を返し続ける（下記） |
| では種類の判定はどうするか | **判定しなくて済むように書く。** `IFolderIdentifier` を持っている時点でフォルダ、`IFileIdentifier` を持っている時点でファイルである。どうしても混在を捌くなら `EnumerateContents` の振り分け（`OnFolderContent` / `OnFileContent`）を使う——**SDK 自身の分類はそちらに出ている** |

## `SAttributes` の中身（ヘッダ）

- `bool` 11 個（`fbReadOnly` `fbHidden` `fbSystem` `fbTemporary` `fbEncrypted` `fbArchive`
  `fbDirectory` `fbCanRead` `fbCanWrite` `fbCanExecute` `fbCanBrowse`）で、**`IFolderIdentifier` と
  `IFileIdentifier` で共有**（`IFolderIdentifier.h`）。【ヘッダ根拠】
- **SDK 同梱の実装ソースのどこからも使われていない**——中身を詰めるのは VW 本体なので、
  **ヘッダを読んでも何が入るかは分からない**。以下はすべて実機の値。
- **Windows 版 SDK のヘッダも macOS 版と改行コードしか違わない**（OS 分岐 0 件。SDK 全体の
  961 ファイルでも同じ——[調査の作法](Investigation%20Techniques.md)「Windows 版 SDK を引き直しても
  同じものしか出ない」）。**プラットフォーム差は実機で測るしかない**（`sdk-grep` / `sdk-ls` を
  引き直しても答えは出ない）
  （[突き合わせ](../Metrics/File%20and%20Folder%20Identifiers.md#sattributes-の中身ヘッダ)）。

## 実機で確かめたこと（VW 2026 / macOS / Apple Silicon）

**印の無い記述は実機確認済み。** 標本は 34 件（フォルダ 30・ファイル 4。場所と掴み方を散らした）
（[標本と環境](../Metrics/File%20and%20Folder%20Identifiers.md#実機で確かめたことvw-2026--macos--apple-silicon)）。

### `fbDirectory` は**フォルダでもファイルでも常に `false`**

- **34 件で 1 件も `true` にならない。** その場で作った新品のローカルフォルダでも、
  **`EnumerateContents` が `OnFolderContent` へ配ったフォルダ**でも `no`——掴み方や場所の
  問題ではない（[File and Folder Dialogs](File%20and%20Folder%20Dialogs.md) の Google ドライブの
  観測もこれ）。
- **Windows でも同じ**（フォルダ 10・ファイル 14 のすべて。`true` を書いても動かない）。
  **`fbDirectory` は両 OS で死んでいる**（[実測](../Metrics/File%20and%20Folder%20Identifiers.md#fbdirectory-の実測フォルダとファイル)）。

### 他の 10 項目——`fbReadOnly` / `fbCanRead` / `fbCanWrite` は生きている

- **`GetAttributes` は中身を埋めている**——34 件のうち 2 件（`SystemTempDirectory`・Google
  ドライブのアカウント直下）で既定と違う値が出た（[実測](../Metrics/File%20and%20Folder%20Identifiers.md#他の-10-項目の実測fbreadonly--fbcanread--fbcanwrite-ほか)）。
- **`fbReadOnly` と `fbCanWrite` は連動し、書けるかの当たりに使える。** `fbCanRead` は単独で
  信じない（読み取り権限を映していない。下記「旗は OS の何に対応しているのか」）。
- 残りは 34 件すべて `no`。**`fbHidden` は dot ファイルでも `no`**（dot ファイルの検出に使えない）、
  `fbTemporary` は一時フォルダでも `no`、**`fbCanExecute` はフォルダでは死んでいる**（ファイルは生きている）。

### `GetAttributes` は**実在しない場所では失敗する**——番号はフォルダとファイルで違う

- 実在しないフォルダは **`51` = `kVCOMError_FileNotFound`**、ファイルは **`1` = `kVCOMError_Failed`**。
  `CreateOnDisk()` の前後で `51` → `0` に変わり、実在に追随する。**判定は
  `VCOM_SUCCEEDED(GetAttributes(attrs))` で**（[実測](../Metrics/File%20and%20Folder%20Identifiers.md#実在しない場所への-getattributes-の実測フォルダとファイル)）。
- **失敗したとき構造体は書き換わらない。失敗時に受け取った `SAttributes` を読まない**
  （「全部 `false`」と見分けが付かない）。

### `ExistsOnDisk` の戻り値は「実在するか」ではない

- **実在しなくても戻り値は `0`（成功）。** 実在は出力引数（`bool&`）にしか出ない。
  `if (VCOM_SUCCEEDED(folder->ExistsOnDisk(exists)))` だけで分岐すると、無い場所を在るものと
  して扱う。**必ず `exists` のほうを見る**（[Investigation Techniques](Investigation%20Techniques.md)「setter の
  戻り値を信用しない」と同じ形の罠）。

### `EnumerateContents` の実測

- **振り分けは正しい**（フォルダは `OnFolderContent`、ファイルは `OnFileContent`）——**種類の判定は
  こちらを信用する**。
- **隠しファイルも来る**（`.DS_Store`）。**フォルダとファイルは混ざって届く**——順序に依存しない
  （[実測](../Metrics/File%20and%20Folder%20Identifiers.md#enumeratecontents-の実測)）。

### 【拾い物】`Set(EFolderSpecifier)` が返すパスは、ディスク上の実名と大文字小文字が違うことがある

- `kExternalsFolder` は `…/Plug-ins/`、列挙の実名は `…/Plug-Ins/`。**`GetFullPath` を文字列で
  比較すると経路によって答えが変わる**（macOS）。

## `SetAttributes` は「ファイルの権限」だけを書く（フォルダは未実装）

（#91・macOS。11 旗を 1 つずつ反転し、前後の `stat` でディスクまで見た。
[実測](../Metrics/File%20and%20Folder%20Identifiers.md#setattributes-で書いた実測フォルダとファイル)）

- **フォルダは `6` = `kVCOMError_NotImplemented`** でディスクは動かない（読み取り専用にした
  つもりのフォルダへも `CreateOnDisk()` で作れる）。**ファイルは `0` で、権限ビットだけが動く**。

### ファイルで効くのは 4 つ、読み戻せるのは 3 つ

- 効くのは `fbReadOnly` / `fbCanWrite`（`0444`）・`fbCanExecute`（`0744`）・`fbCanRead`（`0244`）。
  **本当に書けなくなる**（ディスクの権限そのもの）（[表](../Metrics/File%20and%20Folder%20Identifiers.md#ファイルに書いた旗と読み戻しの実測)）。
- **書き込み権は `fbCanWrite == true && fbReadOnly == false` の AND。** 同時に立てると衝突する
  ので片方だけ触る。
- **`fbCanRead` は書けるのに読み戻せない**（`yes` のまま）。**残り 7 つは成功を返して捨てられる。**

### 実在しない対象への `SetAttributes`

- フォルダは実在に関わらず `6`、ファイルは `1`。番号を当てにせず `VCOM_SUCCEEDED` で見る。

### 【対照】`SetAttributesTimeDateReference` は 4 つとも未実装

- **タイムスタンプは読めても書けない**（4 つとも `6`）。読むほうも `LastAccessed`・`LastBackup` は
  macOS で失敗する（Windows は `LastAccessed` が成功）（[表](../Metrics/File%20and%20Folder%20Identifiers.md#setattributestimedatereference-の-4-つの実測)）。
- **`Set*` がまるごと死んでいるわけではない**——`IFileIdentifier::SetAttributes` は生きている——ので、
  この 2 つは別々に考える。

## 旗は OS の何に対応しているのか（macOS で作った標本）

（`chmod` / `chflags` で「立つはずの状態」を作って測った。[実測](../Metrics/File%20and%20Folder%20Identifiers.md#旗は-os-の何に対応しているのかmacos-で作った標本)）

- **`fbReadOnly` / `fbCanWrite`** は権限ビットと `UF_IMMUTABLE` の両方を映す（読み書きとも生きている）。
- **`fbCanExecute`** はファイルの実行権ビットを映す（フォルダは常に `no`）。
- **`fbHidden` は macOS では絶対に立たない**（dot 名でも `UF_HIDDEN` でも）。**隠し項目の判定に使わない。**
- **`fbCanRead` は読み取り権限ではない**（`chmod 0000` でも `yes`）。
- **`fbSystem` は macOS のシステム領域でも立たない**（8 件すべて `no`）。

## Windows で確かめたこと（Windows 11 / NTFS）

（#93 のプローブ `sattributes-windows`。#91 の測り方を Windows の API に置き換えた。
[環境と実測](../Metrics/File%20and%20Folder%20Identifiers.md#windows-で確かめたことwindows-11--ntfs)）

### `SetAttributes`——フォルダは未実装のまま、ファイルは「ファイル属性」を書く

- フォルダは macOS と同じく `6`。ファイルは **`FILE_ATTRIBUTE_*` が動く**——`fbReadOnly` /
  `fbCanWrite` / `fbHidden` / `fbSystem` / `fbTemporary` / `fbArchive` の **6 つが書けて読み戻せる**。
  `fbEncrypted` は書けない（成功が返るだけ）（[表](../Metrics/File%20and%20Folder%20Identifiers.md#setattributes-の実測windowsフォルダとファイル)）。
- **書き込みの可否は macOS と同じく AND。** **構造体まるごとで置き換わる**ので、1 つだけ変える
  ときも**必ず `GetAttributes` で読んだ値を書き換えて渡す**（`{}` から作ると立っていた属性を消す）。
- `fbCanRead` / `fbCanExecute` を書いたときに ACL が変わったかは見ていない。

### `GetAttributes`——`FILE_ATTRIBUTE_*` をそのまま映す

- `HIDDEN` / `SYSTEM` / `ARCHIVE` / `TEMPORARY` / EFS の `ENCRYPTED` がそのまま旗に出る。
  `fbArchive` はふつうに書き込んだだけで立つ（[表](../Metrics/File%20and%20Folder%20Identifiers.md#getattributes-と-file_attribute_-の突き合わせ)）。
- **フォルダの `READONLY` 属性は `fbReadOnly` に出ない。** Windows ではフォルダの `READONLY` は
  「中へ書けない」という意味ではない（Explorer の表示用の印）ので、映さないほうが正しいとも
  言える。ただ、フォルダの `fbReadOnly` が何を見ているのかは分からなかった。

### システム領域の標本（読むだけ）

（[表](../Metrics/File%20and%20Folder%20Identifiers.md#システム領域の標本読むだけ)）

- **`fbReadOnly` / `fbCanWrite` は ACL を見ていない**（`cmd.exe` でも `fbCanWrite=yes`）。書けるかの
  当たりとしては macOS より弱い。**フォルダの `fbReadOnly` / `fbCanWrite` は Windows では当てに
  しない**（`System Volume Information` だけ `fbReadOnly=yes` になった。`READONLY` 属性は
  立っていないので、属性ビットではない何か（このフォルダは管理者でも開けない）を見ている
  らしいが【推定】、`C:\Program Files\` が `no` / `yes` なので規則は分からない）。
- **`fbCanExecute` は Windows では常に `no`**（拡張子でも決まらない）。**`fbCanRead` も常に `yes`。**
- **使用中のファイル（`pagefile.sys`）では `GetAttributes` が失敗する**（`ExistsOnDisk` は `yes`。
  中で同じ口を使っているのだろう【推定】）。「失敗したら実在しない」とは読まない。

## macOS と Windows の比較

| 旗 | macOS 読む | macOS 書く | Windows 読む | Windows 書く |
| --- | --- | --- | --- | --- |
| `fbReadOnly` / `fbCanWrite` | **生きている**（権限ビット・`UF_IMMUTABLE`） | **生きている**（権限ビット） | **ファイルは生きている**（`READONLY` 属性だけ。ACL は見ない）。フォルダは不明瞭 | **生きている**（`READONLY` 属性） |
| `fbHidden` | 死んでいる | 死んでいる | **生きている**（`HIDDEN`） | **生きている** |
| `fbSystem` | 死んでいる | 死んでいる | **生きている**（`SYSTEM`） | **生きている** |
| `fbArchive` | 死んでいる（標本を作れない） | 死んでいる | **生きている**（`ARCHIVE`） | **生きている** |
| `fbTemporary` | 死んでいる | 死んでいる | **生きている**（`TEMPORARY`） | **生きている** |
| `fbEncrypted` | 死んでいる（標本を作れない） | 死んでいる | **生きている**（EFS の `ENCRYPTED`） | **死んでいる**（成功が返るだけ） |
| `fbCanExecute` | **ファイルでは生きている**（実行権ビット）。フォルダは常に `no` | **生きている** | 死んでいる（常に `no`） | 死んでいる（属性ビットは動かない） |
| `fbCanRead` | 死んでいる（常に `yes`） | **生きている**（読み取りビットが落ちる） | 死んでいる（常に `yes`） | 死んでいる（属性ビットは動かない） |
| `fbDirectory` / `fbCanBrowse` | 死んでいる | 死んでいる | 死んでいる | 死んでいる |

書くのは**ファイルだけ**（フォルダは両 OS とも `kVCOMError_NotImplemented`）。
**両 OS で同じ意味に使えるのは `fbReadOnly` / `fbCanWrite` だけ**で、それも macOS は
権限ビット、Windows は `READONLY` 属性という**別のもの**を見ている。

## 実務上どう書くか

```cpp
using namespace VectorWorks::Filing;
bool exists = false;                     // 実在は出力引数で見る（戻り値は実在しなくても 0）
if (VCOM_SUCCEEDED(folder->ExistsOnDisk(exists)) && exists) { /* 実在する */ }

SAttributes attrs = {};                  // 書けるかの当たり（当たりまで。実際は書いてみる）
bool writable = VCOM_SUCCEEDED(folder->GetAttributes(attrs)) && !attrs.fbReadOnly && attrs.fbCanWrite;

if (VCOM_SUCCEEDED(file->GetAttributes(attrs)))   // 読み取り専用にする: 読んでから 1 つだけ変える
{
    attrs.fbReadOnly = true;             // macOS: chmod 0444 相当 / Windows: READONLY 属性
    file->SetAttributes(attrs);
}
```

- **`fbDirectory` で種類を判定しない**（フォルダでもファイルでも常に `false`）。フォルダとファイルの
  混在は `EnumerateContents` に渡す `IFolderContentListener` の `OnFolderContent` /
  `OnFileContent` の振り分けで捌く。
- **`ExistsOnDisk` は戻り値だけで分岐しない**——実在しなくても成功（0）が返る。
- **フォルダの属性は書き戻せない**（`SetAttributes` は `kVCOMError_NotImplemented` が返るだけ）。
- **`SAttributes` を `{}` から作って書かない**——Windows では構造体まるごとで属性が置き換わり、
  立っていた `HIDDEN` / `SYSTEM` / `ARCHIVE` / `TEMPORARY` が落ちる（`READONLY` だけが残る）。
- **`fbReadOnly` と `fbCanWrite` を同時に指定しない**（`fbReadOnly = false` と `fbCanWrite = false`
  を一緒に書くと、「書けない」側が勝って読み取り専用になる）。
- **書いた `fbCanRead` を読み戻して確かめない**（権限は変わるのに `yes` が返り続ける）。
- **`fbHidden` / `fbSystem` / `fbArchive` / `fbTemporary` / `fbEncrypted` / `fbCanExecute` を
  両 OS 共通の判定に使わない**（前の 5 つは Windows でだけ、`fbCanExecute` は macOS でだけ生きている）。

## 打ち切った調査

### `fbArchive` / `fbEncrypted` を立てた標本は、macOS では作れない

- macOS の `SF_ARCHIVED` は root でないと立てられず、ファイル単位の暗号化属性はそもそも無い
  ので、**立つはずの標本を作れない**（[潰した道](../Metrics/File%20and%20Folder%20Identifiers.md#fbarchive--fbencrypted-を立てた標本を-macos-で作ろうとした)）。
- `SetAttributes` で書いてもディスクは動かなかったので、**macOS の VW はこの 2 つを扱っていない**
  ことまでは言える。**Windows では 2 つとも読めた**（#93 で解決）。特権を取ってまで測り直さない。
