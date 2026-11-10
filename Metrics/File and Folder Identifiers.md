# ファイル・フォルダの識別子（`IFileIdentifier` / `IFolderIdentifier`） — 計測記録

結論は [Findings「File and Folder Identifiers」](../Findings/File%20and%20Folder%20Identifiers.md)。
ここには標本の一覧・プローブの出力・旗ごとの実測の表（macOS / Windows）・ヘッダの突き合わせを
元の文言のまま置く。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

[issue #87](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/87)・
[issue #91](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/91)・
[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93) の調査。

## 測った環境

- **macOS**: VW 2026 / macOS / Apple Silicon。プローブ `folder-attributes`（#87）・
  `sattributes-write`（#91）。
- **Windows**: Windows 11 25H2（build 26200）Pro・NTFS。プローブ `sattributes-windows`（#93）。
- どれも実機確認プラグイン（VwSdkProbes）のプローブとして、メニューコマンドの中で走らせた
  （詳細は各節）。プローブは役目を終えたので消してある。

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

`sdk-grep` で引くと、`SAttributes` は SDK 同梱の実装ソース（`SDKLib/Source`）のどこからも
使われていなかった——宣言が上のヘッダと `IFileIdentifier.h` にあるだけで、用例も実装も無い。

`ci-debug` で
両プラットフォームの SDK を引き、`Interfaces/VectorWorks/Filing/IFolderIdentifier.h`
（132 行）と `IFileIdentifier.h`（62 行）を突き合わせた
（[windows](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/35410377167) /
[mac](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/35410383253)）。

| 比べたもの | macOS 版 SDK | Windows 版 SDK |
| --- | --- | --- |
| 改行コード | LF（CR は 0 行） | **CRLF**（132 行 / 63 行が CR 付き） |
| CR を落とした md5（`IFolderIdentifier.h` / `IFileIdentifier.h`） | `d3b3c129…` / `165f1f75…` | **同じ値** |
| `GS_WIN` / `GS_MAC` / `_WINDOWS` / `__APPLE__` による分岐 | 0 件 | 0 件 |
| `SAttributes` の 11 旗の並び・`Get`/`SetAttributes` の宣言位置 | `IFolderIdentifier.h:14`／`:109,110`・`IFileIdentifier.h:36,37` | 同じ行・同じ並び |

SDK 全体（`SDKLib/` の 961 ファイル）を同じやり方で突き合わせても、**CR を落とした集約
ハッシュが両プラットフォームで完全に一致した**（`a859b31735d9572d2b7c967aa3512a19`。
ディレクトリ単位でも全 19 群が一致。[調査の作法](../Metrics/Investigation%20Techniques.md#windows-版-sdk-と-mac-版-sdk-の比較)）。

`fbHidden` / `fbSystem` / `fbArchive` / `fbEncrypted` は Windows のファイル属性
（`FILE_ATTRIBUTE_HIDDEN` / `_SYSTEM` / `_ARCHIVE` / `_ENCRYPTED`）そのものの名前だが、
その対応を書いたものは SDK の側に 1 行も無かった（[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93)。
実機で測った結果は下記「Windows で確かめたこと」）。

## 実機で確かめたこと（VW 2026 / macOS / Apple Silicon）

**プラグインのメニューコマンドの中（`DoInterface` の中）から、外部モジュールへ出した
本体（ペイロード）側で**、実機確認プラグイン（VwSdkProbes）のプローブとして走らせた
実測。プローブは 2 本（#87 の `folder-attributes`・
#91 の `sattributes-write`）で、どちらも落ちていない。以下の「標本 34 件」は #87 の
プローブのもので、`SetAttributes` と「旗が OS の何に対応しているのか」は #91 の
プローブで別に測っている（それぞれの節に書いてある）。

**標本は 34 件**（フォルダ 30・ファイル 4）。場所と掴み方を次のように散らした。

| 何を測ったか | 件数 |
| --- | --- |
| OS 標準のフォルダ（`Set(EOSFolderSpecifier)` の全 4 値。`/Applications/`・`~/Documents/`・`~/Library/Application Support/…`・`/private/var/…/T/TemporaryItems/`） | 4 |
| VW の導入先基準のフォルダ（`kApplicationFolder` / `kExternalsFolder` / `kAppDataFolder`） | 3 |
| **その場で `CreateOnDisk()` で作った新品のローカルフォルダ**（作った後・絶対パスで掴み直したもの） | 2 |
| **`EnumerateContents` が `OnFolderContent` へ配ったフォルダ**（`/Applications/VW2026/` の直下） | 17 |
| 同じものを絶対パスから掴み直したフォルダ | 1 |
| クラウドストレージ（`~/Library/CloudStorage/` 自体と、その直下の `Box-Box` / `GoogleDrive-…`） | 3 |
| ファイル（`.DS_Store` ×2・`VWVersion.txt`・絶対パスで掴み直した `.DS_Store`） | 4 |

### `fbDirectory` の実測（フォルダとファイル）

```
フォルダ: 測れた=30  そのうち fbDirectory=yes は 0 件
ファイル: 測れた=4   そのうち fbDirectory=yes は 0 件
```

**1 件も `true` にならなかった。** 場所（ローカル / VW の導入先 / 一時フォルダ /
クラウドストレージ）も、掴み方（`EOSFolderSpecifier` / `EFolderSpecifier` /
`Set(親, 名前)` / 絶対パス文字列 / 列挙が配ったポインタ / ダイアログが返したもの）も
変えたが、結果は同じだった。

- **その場で作った新品のローカルフォルダ**（`/private/var/…/T/TemporaryItems/VwSdkProbes-folder-attributes/`）でも `fbDirectory=no`。
- **`EnumerateContents` が `OnFolderContent` へ配った 17 件**でも `fbDirectory=no`。
- [File and Folder Dialogs](../Findings/File%20and%20Folder%20Dialogs.md) では Google ドライブ配下の
  フォルダについて `fbDirectory=no` が観測されていた。同じ実行の中で `~/Library/CloudStorage/`
  配下も測っており、ローカルと差は出ていない。

**Windows:** #93 のプローブ（`sattributes-windows`）で、フォルダ 10 件
（作った作業フォルダ・`C:\`・`C:\WINDOWS\`・`System32\`・`Program Files\`・
`ProgramData\`・`System Volume Information\`・`$Recycle.Bin\`・`%USERPROFILE%\AppData\` など。
OS 側では `FILE_ATTRIBUTE_DIRECTORY` が立っている）とファイル 14 件のすべてで `fbDirectory=no`。
`SetAttributes` で `true` を書いても読み戻しもディスクも動かなかった（下記「Windows で
確かめたこと」）。

### 他の 10 項目の実測（`fbReadOnly` / `fbCanRead` / `fbCanWrite` ほか）

34 件のうち 2 件で既定と違う値が出た。

| 標本 | `fbReadOnly` | `fbCanRead` | `fbCanWrite` |
| --- | --- | --- | --- |
| 上記以外の 32 件（フォルダ・ファイルとも） | `no` | `yes` | `yes` |
| `SystemTempDirectory`（`/private/var/…/T/TemporaryItems/`） | `no` | **`no`** | `yes` |
| Google ドライブのアカウント直下（`~/Library/CloudStorage/GoogleDrive-…/`） | **`yes`** | `yes` | **`no`** |

- **`fbReadOnly` と `fbCanWrite` は連動して動いた**（Google ドライブのアカウント直下は
  実際に書き込めない場所である）。
- `SystemTempDirectory` の `fbCanRead=no` は理由が付けられなかった。後に #91 で
  `chmod 0000` でも `fbCanRead=yes` が返ることを測った（下記「旗は OS の何に対応しているのか」）。

残る 7 つ——**`fbDirectory` / `fbHidden` / `fbSystem` / `fbTemporary` / `fbEncrypted` /
`fbArchive` / `fbCanExecute` / `fbCanBrowse` は、34 件すべてで `no` だった**。
うち次の 3 つは「`true` が出てよさそうな標本」が含まれていながら `no` である。

- **`fbHidden`** … `.DS_Store`（先頭が `.` の隠しファイル）でも `no`。
  #91 で **macOS が本当に「隠し」と扱う `UF_HIDDEN` を立てた標本**でも `no` だった（下記）。
- **`fbTemporary`** … 一時フォルダ（`/private/var/…/T/TemporaryItems/`）そのものでも `no`。
- **`fbCanExecute`** … `/Applications/VW2026/` でも `no`。中身を列挙できている
  （＝辿れている）フォルダなので、実際には実行（走査）権がある。#91 で、ファイルでは
  実行権ビットを立てると `yes` になった（`/bin/ls` で `yes`。下記）。

`fbSystem` / `fbEncrypted` / `fbArchive` については、#91 で**「立つはずの標本」を
用意して測り直した**（下記「旗は OS の何に対応しているのか」）。

### 実在しない場所への `GetAttributes` の実測（フォルダとファイル）

| 対象 | `ExistsOnDisk` の出力 | `GetAttributes` の戻り値 |
| --- | --- | --- |
| 実在するフォルダ / ファイル | `yes` | `0` = `kVCOMError_NoError` |
| 実在しない**フォルダ** | `no` | **`51` = `kVCOMError_FileNotFound`** |
| 実在しない**ファイル** | `no` | **`1` = `kVCOMError_Failed`** |

同じ `IFolderIdentifier` で `CreateOnDisk()` の**前後を測る**と、`51` → `0` に変わった
（作る前は `51`、`CreateOnDisk()` が `0` を返した後は `0`）。

失敗したとき、**呼び出し側が `{}` で 0 初期化した `SAttributes` は 1 つも書き換わらなかった**
（11 項目すべて `false` のまま）。

### `ExistsOnDisk` の戻り値の実測

```
実在しないフォルダ: ExistsOnDisk=0（成功） 実在=no
```

実在しなくても戻り値は `0`（成功）で、実在は出力引数（`bool&`）にだけ出た。

### `EnumerateContents` の実測

`/Applications/VW2026/` を非再帰で列挙して `VCOMError=0`、**フォルダ 17 件・ファイル 2 件**。

- フォルダは `OnFolderContent`、ファイルは `OnFileContent` へ来た（`Plant Database` /
  `Plug-Ins` / `Settings` / `VWHelp` はフォルダ側、`.DS_Store` / `VWVersion.txt` は
  ファイル側）。
- `.DS_Store` が `OnFileContent` へ配られた。先頭が `.` の項目は落とされていなかった。
- フォルダとファイルは混ざって届いた（種類ごとにまとまらない）。実測の順は
  `Plant Database` → `Plug-Ins` → `Settings` → `.DS_Store` → `VWHelp` → `VWVersion.txt`。

### `Set(EFolderSpecifier)` が返すパスの大文字小文字（ディスク上の実名との比較）

同じ 1 つのフォルダについて、2 つの経路が**綴りの違う文字列**を返した。

```
kExternalsFolder が返した    : /Applications/VW2026/Plug-ins/
EnumerateContents が返した実名: /Applications/VW2026/Plug-Ins/    ← I が大文字
```

macOS の既定のファイルシステムは大文字小文字を区別しないのでどちらでも開けるが、
パスを文字列として比較すると一致しない。

## `SetAttributes` で書いた実測（フォルダとファイル）

`SetAttributes(const SAttributes&)` は `IFileIdentifier` と `IFolderIdentifier` の
**両方**にある（`IFileIdentifier.h:37` / `IFolderIdentifier.h:110`）
（[issue #91](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/91)。
VW 2026 / macOS / Apple Silicon）。

11 旗を**1 つずつ反転して書いては読み戻し**、さらに**その前後で `stat` を見て
ディスクが動いたか**まで測った。読み戻しだけでは「SDK が覚えているだけ」と
「本当にディスクが変わった」の区別が付かないためである。

| 対象 | 戻り値 | ディスク（`st_mode` / `st_flags`） |
| --- | --- | --- |
| `IFolderIdentifier` | **`6` = `kVCOMError_NotImplemented`** | **1 ビットも動かない**（11 旗すべて・まとめて全部 `true` / 全部 `false` でも同じ） |
| `IFileIdentifier` | **`0`（成功）** | **権限ビットだけが動く**（`st_flags` は一度も動かない） |

読み取り専用にしたつもりのフォルダへ `CreateOnDisk()` すると**ふつうに作れた**。

### ファイルに書いた旗と読み戻しの実測

| 書いた旗 | ディスク | 読み戻し |
| --- | --- | --- |
| `fbReadOnly = true` | `0644` → **`0444`** | `fbReadOnly=yes` `fbCanWrite=no` |
| `fbCanWrite = false` | `0644` → **`0444`** | `fbReadOnly=yes` `fbCanWrite=no` |
| `fbCanExecute = true` | `0644` → **`0744`** | `fbCanExecute=yes` |
| **`fbCanRead = false`** | `0644` → **`0244`** | **`fbCanRead=yes`（変わらない）** |
| `fbHidden` / `fbSystem` / `fbTemporary` / `fbEncrypted` / `fbArchive` / `fbDirectory` / `fbCanBrowse` | **動かない** | 変わらない |

- まとめて全部 `true` を書くと `0444`、全部 `false` を書くとやはり `0444` になった。
- `fbReadOnly=true` を書いた後、実際に 1 バイト追記しようとすると**開くところで拒まれた**。
  初期値を書き戻すと `0644` に戻り、また追記できた。
- `fbCanRead` に `false` を書くと所有者の読み取りビットが実際に落ちた（`0644` → `0244`）が、
  `GetAttributes` は `fbCanRead=yes` を返し続けた。
- 残り 7 つは成功が返り、**ディスクも読み戻しも 1 ビットも動かなかった**。`fbDirectory` に
  `true` を書いても何も起きなかった。

### 実在しない対象への `SetAttributes`

| 対象 | `GetAttributes` | `SetAttributes` |
| --- | --- | --- |
| 実在しないフォルダ | `51` = `kVCOMError_FileNotFound` | **`6` = `kVCOMError_NotImplemented`** |
| 実在しないファイル | `1` = `kVCOMError_Failed` | **`1` = `kVCOMError_Failed`** |

フォルダは**実在に関わらず `6`**、ファイルは実在しなければ `1` だった。

### `SetAttributesTimeDateReference` の 4 つの実測

同じ識別子の「`Set*` の半分」も測った（`IFileIdentifier`。作ったばかりのファイルに
`2001-02-03 04:05:06` を書こうとした）。

| 参照 | `GetAttributesTimeDateReference` | `SetAttributesTimeDateReference` |
| --- | --- | --- |
| `eAttributesTimeReference_Created` | `0`（成功。作成時刻が返る） | **`6` = `kVCOMError_NotImplemented`** |
| `eAttributesTimeReference_LastWritten` | `0`（成功。同じ値） | **`6`** |
| `eAttributesTimeReference_LastAccessed` | **`1` = `kVCOMError_Failed`** | **`6`** |
| `eAttributesTimeReference_LastBackup` | **`1`** | **`6`** |

書くほうは 4 つとも `6`、読むほうは `LastAccessed` と `LastBackup` が失敗した。

## 旗は OS の何に対応しているのか（macOS で作った標本）

「34 件すべて `no` だった」だけでは、**立たないのか立つ標本が無かったのか**が決まらない。
そこで `chmod` / `chflags` で**「立つはずの状態」を OS 側で作って**測った
（[issue #91](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/91)。
基準は `0644`・旗なしのファイル）。

| 作った状態 | `SAttributes` の差 |
| --- | --- |
| `chmod 0444`（読み取り専用） | `fbReadOnly=yes` `fbCanWrite=no` |
| `chmod 0000`（誰も読めない） | `fbReadOnly=yes` `fbCanWrite=no`（**`fbCanRead` は `yes` のまま**） |
| `chmod 0755`（実行できる） | `fbCanExecute=yes` |
| `chflags UF_HIDDEN`（隠し） | **差なし** |
| `chflags UF_IMMUTABLE`（変更不可） | `fbReadOnly=yes` `fbCanWrite=no` |
| `chflags SF_ARCHIVED`（書庫） | **立てられない**（`errno=1` `Operation not permitted`。root が要る） |
| 先頭が `.` のフォルダ ＋ `UF_HIDDEN` | **差なし**（`fbHidden=no`） |

読み取りは `GetAttributes` 側で（上表）、書き込みは `SetAttributes` 側で（上の
「ファイルで効くのは 4 つ、読み戻せるのは 3 つ」）測った。

- 既存の実行ファイル `/bin/ls` `/usr/bin/true` は `fbCanExecute=yes` だった。
- `fbHidden` は dot 名でも `UF_HIDDEN` でも `no` だった（ファイル・フォルダとも）。
- `chmod 0000`（所有者すら読めない）でも `fbCanRead=yes` だった。#87 で
  `SystemTempDirectory` だけ `no` だった理由は依然として分からない。
- システム領域 `/System/` `/System/Library/` `/usr/bin/` `/private/var/db/` `/Library/` `/bin/ls`
  `/System/Library/CoreServices/SystemVersion.plist` `/usr/bin/true` の **8 件すべてで
  `fbSystem=no`** だった（どれも `fbReadOnly=yes` は返っている）。

## Windows で確かめたこと（Windows 11 / NTFS）

[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93) の
プローブ `sattributes-windows` で、上の macOS の測り方（#91 の `sattributes-write`）を
Windows の API に置き換えて同じ順に測った。実行環境はログの冒頭のとおり
**Windows 11 25H2（build 26200）Pro・`C:\` は NTFS（`FILE_SUPPORTS_ENCRYPTION=yes`）**
（レジストリの `ProductName` は `Windows 10 Pro` を返すが、これは Windows 11 の既知の癖）。
ディスク側は `stat` の代わりに `GetFileAttributesW` で前後を比べ、「立つはずの標本」は
`chmod` / `chflags` の代わりに `SetFileAttributesW` と `EncryptFileW`（EFS）で作った。
落ちていない（所要 2.13 秒）。

### `SetAttributes` の実測（Windows。フォルダとファイル）

| 対象 | 戻り値 | ディスク（`GetFileAttributesW`） |
| --- | --- | --- |
| `IFolderIdentifier` | **`6` = `kVCOMError_NotImplemented`**（macOS と同じ） | **1 ビットも動かない**（11 旗すべて・まとめて全部 `true` / 全部 `false` でも同じ） |
| `IFileIdentifier` | **`0`（成功）** | **`FILE_ATTRIBUTE_*` が動く** |

ファイル（`case.txt`。作った直後は `ARCHIVE` だけが立っている）で 11 旗を 1 つずつ
反転して書いた結果:

| 書いた旗 | ディスク | 読み戻し |
| --- | --- | --- |
| `fbReadOnly = true` | `ARCHIVE` → **`READONLY\|ARCHIVE`** | `fbReadOnly=yes` `fbCanWrite=no` |
| `fbCanWrite = false` | `ARCHIVE` → **`READONLY\|ARCHIVE`** | `fbReadOnly=yes` `fbCanWrite=no` |
| `fbHidden = true` | → **`HIDDEN\|ARCHIVE`** | `fbHidden=yes` |
| `fbSystem = true` | → **`SYSTEM\|ARCHIVE`** | `fbSystem=yes` |
| `fbTemporary = true` | → **`ARCHIVE\|TEMPORARY`** | `fbTemporary=yes` |
| `fbArchive = false` | `ARCHIVE` → **`NORMAL`** | `fbArchive=no` |
| `fbEncrypted = true` | **動かない** | 変わらない |
| `fbDirectory` / `fbCanRead = false` / `fbCanExecute = true` / `fbCanBrowse` | **動かない**（属性ビットは） | 変わらない |

- 6 つが書けて、6 つとも読み戻せた。
- まとめて全部 `true` を書くと `READONLY|HIDDEN|SYSTEM|ARCHIVE|TEMPORARY`、全部 `false` を
  書くと **`READONLY` だけ**が残った。
- 全部 `false` を書くと `HIDDEN` / `SYSTEM` / `ARCHIVE` / `TEMPORARY` がすべて落ちた。
- `fbReadOnly=true`（または `fbCanWrite=false` だけ）を書いた後、1 バイト追記しようとすると
  **開くところで拒まれた**（`err=5` アクセスが拒否されました）。初期値を書き戻すと `ARCHIVE` に
  戻り、また追記できた。
- 平文へ `fbEncrypted = true` を書いても、EFS で暗号化したファイルへ `false` を書いても、
  **成功が返るだけで暗号化の状態は変わらなかった**（下記 6d）。
- `fbCanRead` / `fbCanExecute` を書いたときに**ACL が変わったかは見ていない**
  （見たのは属性ビットだけ）。読み戻しは変わらない。

フォルダを読み取り専用にしようとしても `6` が返るだけで、**その中へ `CreateOnDisk()`
するとふつうに作れた**（macOS と同じ）。実在しない対象への `SetAttributes` も
macOS と同じで、フォルダ `6`・ファイル `1`（`GetAttributes` はフォルダ `51`・ファイル `1`）。

**`SetAttributesTimeDateReference` は 4 つとも `6`**（macOS と同じ）。読むほう:

| 参照 | macOS の `Get…` | Windows の `Get…` |
| --- | --- | --- |
| `Created` | `0` | `0` |
| `LastWritten` | `0` | `0` |
| `LastAccessed` | **`1`** | **`0`**（成功） |
| `LastBackup` | `1` | `1` |

### `GetAttributes` と `FILE_ATTRIBUTE_*` の突き合わせ

`SetFileAttributesW` で属性を 1 つずつ立てて読んだ（基準はファイル `NORMAL`・フォルダ
`DIRECTORY` だけ）。

| 立てた属性 | ファイルでの差 | フォルダでの差 |
| --- | --- | --- |
| `READONLY` | `fbReadOnly=yes` `fbCanWrite=no` | **差なし** |
| `HIDDEN` | `fbHidden=yes` | `fbHidden=yes` |
| `SYSTEM` | `fbSystem=yes` | `fbSystem=yes` |
| `HIDDEN\|SYSTEM` | `fbHidden=yes` `fbSystem=yes` | （測っていない） |
| `ARCHIVE` | `fbArchive=yes` | `fbArchive=yes` |
| `TEMPORARY` | `fbTemporary=yes` | （フォルダには立てられないので測っていない） |
| `NOT_CONTENT_INDEXED`（対応物の無い対照） | 差なし | 差なし |

- `ARCHIVE` を落として（`fbArchive=no`）1 バイト追記すると、OS が `ARCHIVE` を立て直し、
  `fbArchive=yes` になった（特権なしで）。
- `EncryptFileW` が成功し（`FileEncryptionStatusW=1`、OS 側は `ARCHIVE|ENCRYPTED`）、
  **差は `fbEncrypted=yes` だけ**だった。
- フォルダの `READONLY` 属性は `fbReadOnly` に出なかった（次項）。

### システム領域の標本（読むだけ）

| 標本 | OS の属性 | `SAttributes`（`yes` の旗だけ） |
| --- | --- | --- |
| `C:\` | `HIDDEN\|SYSTEM\|DIRECTORY` | `fbHidden` `fbSystem` `fbCanRead` `fbCanWrite` |
| `C:\WINDOWS\` / `System32\` | `DIRECTORY` | `fbCanRead` `fbCanWrite` |
| `C:\Program Files\` | **`READONLY`**`\|DIRECTORY` | `fbCanRead` `fbCanWrite`（`fbReadOnly=no`） |
| `C:\ProgramData\` | `HIDDEN\|DIRECTORY\|NOT_CONTENT_INDEXED` | `fbHidden` `fbCanRead` `fbCanWrite` |
| `C:\System Volume Information\` | `HIDDEN\|SYSTEM\|DIRECTORY` | **`fbReadOnly`** `fbHidden` `fbSystem` `fbCanRead`（**`fbCanWrite=no`**） |
| `C:\$Recycle.Bin\` | `HIDDEN\|SYSTEM\|DIRECTORY` | `fbHidden` `fbSystem` `fbCanRead` `fbCanWrite` |
| `%USERPROFILE%\AppData\` | `HIDDEN\|DIRECTORY` | `fbHidden` `fbCanRead` `fbCanWrite` |
| `System32\kernel32.dll` / `cmd.exe` / `drivers\etc\hosts` | `ARCHIVE` | `fbArchive` `fbCanRead` `fbCanWrite` |
| `%USERPROFILE%\NTUSER.DAT` | `HIDDEN\|ARCHIVE\|NOT_CONTENT_INDEXED` | `fbHidden` `fbArchive` `fbCanRead` `fbCanWrite` |
| `C:\pagefile.sys` | 読めない（`GetFileAttributesW` が `err=32` 使用中） | **`GetAttributes=1`（失敗）**。`ExistsOnDisk` は `yes` |

- `cmd.exe` や `kernel32.dll`、`C:\WINDOWS\` は一般ユーザーには書けない（ACL で守られて
  いる）のに `fbCanWrite=yes` だった。
- `System Volume Information` だけ、フォルダで `fbReadOnly=yes` `fbCanWrite=no` になった。
  `READONLY` 属性は立っていない。`C:\Program Files\`（`READONLY` 属性あり・一般ユーザーは
  書けない）は `no` / `yes` だった。
- `fbCanExecute` は `cmd.exe`・`kernel32.dll` でも、作った `.exe` / `.bat` / `.cmd` / `.com` /
  `.ps1` / `.vbs` / `.dll` でも `no` だった。
- `fbCanRead` は常に `yes` だった（`System Volume Information` でも）。
- `pagefile.sys` は `ExistsOnDisk=yes` なのに `GetAttributes=1`。同じ場所で
  `GetFileAttributesW` も `err=32` で失敗した。

## 打ち切った調査

### `fbArchive` / `fbEncrypted` を立てた標本を macOS で作ろうとした

「`no` を返す」ところまでは測った。「立つはずの標本」を作ろうとした道:

- **`fbArchive`** … macOS の対応物 `SF_ARCHIVED` は**スーパーユーザでないと立てられなかった**
  （実測: `chflags` が `errno=1 Operation not permitted`）。プラグインは利用者の権限で動く。
- **`fbEncrypted`** … macOS には**ファイル単位の暗号化属性が無い**（FileVault は
  ボリューム単位で、個々のファイルに旗は立たない）。

どちらも `SetAttributes` で書いても**ディスクは 1 ビットも動かなかった**。
Windows では 2 つとも OS が立てた旗を読めた（上記「Windows で確かめたこと」。#93）。
