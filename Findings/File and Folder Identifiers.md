# ファイル・フォルダの識別子（`IFileIdentifier` / `IFolderIdentifier`）

ディスク上の場所を指す 2 つの識別子そのものの話——**属性（`GetAttributes` / `SetAttributes`）・
実在の確認（`ExistsOnDisk`）・中身の列挙（`EnumerateContents`）が、実機で何を返すか**
（[issue #87](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/87)、
[issue #91](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/91)、
[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93)）。

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

**Windows 版 SDK のヘッダも、macOS 版と改行コードしか違わない。** `ci-debug` で
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

**これは この 2 ファイルに限った話ではない。** SDK 全体（`SDKLib/` の 961 ファイル）を
同じやり方で突き合わせると、**CR を落とした集約ハッシュが両プラットフォームで完全に一致する**
（`a859b31735d9572d2b7c967aa3512a19`。ディレクトリ単位でも全 19 群が一致）——
つまり**Windows 版 SDK と macOS 版 SDK は、配られるファイルとしては同じものである**
（詳細は [調査の作法](Investigation%20Techniques.md)「Windows 版 SDK を引き直しても同じものしか出ない」）。

したがって**「Windows では旗の意味が違うのか」も、ヘッダからは一切分からない**。
`fbHidden` / `fbSystem` / `fbArchive` / `fbEncrypted` は Windows のファイル属性
（`FILE_ATTRIBUTE_HIDDEN` / `_SYSTEM` / `_ARCHIVE` / `_ENCRYPTED`）そのものの名前だが、
**その対応を書いたものは SDK の側に 1 行も無い**——値を詰めるのは Vectorworks 本体で、
本体は SDK に付いてこないからである。**プラットフォーム差は Windows 実機で測るしかなく、
`sdk-grep` / `sdk-ls` を引き直しても答えは出ない**（[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93)。
実機で測った結果は下記「Windows で確かめたこと」）。

## 実機で確かめたこと（VW 2026 / macOS / Apple Silicon）

**プラグインのメニューコマンドの中（`DoInterface` の中）から、外部モジュールへ出した
本体（ペイロード）側で**、実機確認プラグイン（VwSdkProbes）のプローブとして走らせた
実測。**印の無い記述は実機確認済み。** プローブは 2 本（#87 の `folder-attributes`・
#91 の `sattributes-write`）で、どちらも落ちていない。以下の「標本 34 件」は #87 の
プローブのもので、`SetAttributes` と「旗が OS の何に対応しているのか」は #91 の
プローブで別に測っている（それぞれの節に書いてある）。

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

**Windows でも同じ。** #93 のプローブ（`sattributes-windows`）で、フォルダ 10 件
（作った作業フォルダ・`C:\`・`C:\WINDOWS\`・`System32\`・`Program Files\`・
`ProgramData\`・`System Volume Information\`・`$Recycle.Bin\`・`%USERPROFILE%\AppData\` など。
OS 側では `FILE_ATTRIBUTE_DIRECTORY` が立っている）とファイル 14 件のすべてで `fbDirectory=no`。
`SetAttributes` で `true` を書いても読み戻しもディスクも動かない（下記「Windows で
確かめたこと」）。**`fbDirectory` は両 OS で死んでいる。**

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
- `SystemTempDirectory` の `fbCanRead=no` は理由が付けられない。**`fbCanRead` を単独で
  信じない**——後に #91 で、**`fbCanRead` は読み取り権限を映していない**ことが確定した
  （下記「旗は OS の何に対応しているのか」）。

残る 7 つ——**`fbDirectory` / `fbHidden` / `fbSystem` / `fbTemporary` / `fbEncrypted` /
`fbArchive` / `fbCanExecute` / `fbCanBrowse` は、34 件すべてで `no` だった**。
うち次の 3 つは「`true` が出てよさそうな標本」が含まれていながら `no` である。

- **`fbHidden`** … `.DS_Store`（先頭が `.` の隠しファイル）でも `no`。
  **dot ファイルの検出には使えない。** #91 で **macOS が本当に「隠し」と扱う
  `UF_HIDDEN` を立てた標本**でも `no` だと確かめた（下記）。
- **`fbTemporary`** … 一時フォルダ（`/private/var/…/T/TemporaryItems/`）そのものでも `no`。
- **`fbCanExecute`** … `/Applications/VW2026/` でも `no`。中身を列挙できている
  （＝辿れている）フォルダなので、実際には実行（走査）権がある。#91 で
  **ファイルなら実行権ビットをそのまま映す**（`/bin/ls` で `yes`）と分かったので、
  これは「**フォルダ側の `fbCanExecute` だけが死んでいる**」という話である（下記）。

`fbSystem` / `fbEncrypted` / `fbArchive` については、#91 で**「立つはずの標本」を
用意して測り直した**（下記「旗は OS の何に対応しているのか」）。

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

## `SetAttributes` は「ファイルの権限」だけを書く（フォルダは未実装）

`SetAttributes(const SAttributes&)` は `IFileIdentifier` と `IFolderIdentifier` の
**両方**にある（`IFileIdentifier.h:37` / `IFolderIdentifier.h:110`）。**が、生きているのは
ファイル側だけである**（[issue #91](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/91)。
VW 2026 / macOS / Apple Silicon）。

11 旗を**1 つずつ反転して書いては読み戻し**、さらに**その前後で `stat` を見て
ディスクが動いたか**まで測った。読み戻しだけでは「SDK が覚えているだけ」と
「本当にディスクが変わった」の区別が付かないためである。

| 対象 | 戻り値 | ディスク（`st_mode` / `st_flags`） |
| --- | --- | --- |
| `IFolderIdentifier` | **`6` = `kVCOMError_NotImplemented`** | **1 ビットも動かない**（11 旗すべて・まとめて全部 `true` / 全部 `false` でも同じ） |
| `IFileIdentifier` | **`0`（成功）** | **権限ビットだけが動く**（`st_flags` は一度も動かない） |

**フォルダには書き戻す口が無い。** 読み取り専用にしたつもりのフォルダへ
`CreateOnDisk()` すると**ふつうに作れる**（実測）。`kVCOMError_NotImplemented` を
返しているので、`VCOM_SUCCEEDED` で見ていれば取り違えることはない。

### ファイルで効くのは 4 つ、読み戻せるのは 3 つ

| 書いた旗 | ディスク | 読み戻し |
| --- | --- | --- |
| `fbReadOnly = true` | `0644` → **`0444`** | `fbReadOnly=yes` `fbCanWrite=no` |
| `fbCanWrite = false` | `0644` → **`0444`** | `fbReadOnly=yes` `fbCanWrite=no` |
| `fbCanExecute = true` | `0644` → **`0744`** | `fbCanExecute=yes` |
| **`fbCanRead = false`** | `0644` → **`0244`** | **`fbCanRead=yes`（変わらない）** |
| `fbHidden` / `fbSystem` / `fbTemporary` / `fbEncrypted` / `fbArchive` / `fbDirectory` / `fbCanBrowse` | **動かない** | 変わらない |

- **書き込み権は 2 つの旗の AND で決まる。** 書けるのは
  `fbCanWrite == true && fbReadOnly == false` のときだけで、**どちらか一方でも
  「書けない」側なら書けなくなる**。まとめて全部 `true` を書くと（`fbReadOnly=true` が
  効いて）`0444`、全部 `false` を書くと（`fbCanWrite=false` が効いて）やはり `0444` に
  なった。**`fbReadOnly` と `fbCanWrite` を同時に立てると意図が衝突する**ので、
  片方だけ触ること。
- **本当に書けなくなる。** `fbReadOnly=true` を書いた後、実際に 1 バイト追記しようと
  すると**開くところで拒まれる**。初期値を書き戻すと `0644` に戻り、また追記できる。
  ——**「SDK が覚えているだけ」ではなく、ディスクの権限そのものを変えている。**
- **`fbCanRead` は「書けるのに読めない」。** `false` を書くと所有者の読み取りビットが
  実際に落ちる（`0644` → `0244`）のに、`GetAttributes` は `fbCanRead=yes` を返し続ける。
  **書いた値を読み戻して確かめる、という書き方ができない。**
- **残り 7 つは完全に捨てられる。** 成功が返り、**ディスクも読み戻しも 1 ビットも
  動かない**。`fbDirectory` に `true` を書いても何も起きない（「フォルダに変える」
  ような操作ではない）。

### 実在しない対象への `SetAttributes`

| 対象 | `GetAttributes` | `SetAttributes` |
| --- | --- | --- |
| 実在しないフォルダ | `51` = `kVCOMError_FileNotFound` | **`6` = `kVCOMError_NotImplemented`** |
| 実在しないファイル | `1` = `kVCOMError_Failed` | **`1` = `kVCOMError_Failed`** |

フォルダは**実在に関わらず `6`**（そもそも実装が無いため）、ファイルは実在しなければ
`1`。ここでも番号を当てにせず `VCOM_SUCCEEDED` で見る。

### 【対照】`SetAttributesTimeDateReference` は 4 つとも未実装

同じ識別子の「`Set*` の半分」も測った（`IFileIdentifier`。作ったばかりのファイルに
`2001-02-03 04:05:06` を書こうとした）。

| 参照 | `GetAttributesTimeDateReference` | `SetAttributesTimeDateReference` |
| --- | --- | --- |
| `eAttributesTimeReference_Created` | `0`（成功。作成時刻が返る） | **`6` = `kVCOMError_NotImplemented`** |
| `eAttributesTimeReference_LastWritten` | `0`（成功。同じ値） | **`6`** |
| `eAttributesTimeReference_LastAccessed` | **`1` = `kVCOMError_Failed`** | **`6`** |
| `eAttributesTimeReference_LastBackup` | **`1`** | **`6`** |

**タイムスタンプは読めても書けない**（4 つとも `6`）。読むほうも半分で、
`LastAccessed` と `LastBackup` は失敗する。**`Set*` がまるごと死んでいるわけではない**
——`IFileIdentifier::SetAttributes` は生きている——ので、この 2 つは別々に考える。

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

読み取りは `GetAttributes` 側、書き込みは `SetAttributes` 側で測っているので、
**旗ごとに「読めるか」「書けるか」が別々に決まる**。まとめると次のようになる。

| 旗 | 読む（`GetAttributes`） | 書く（`SetAttributes`。ファイルのみ） |
| --- | --- | --- |
| `fbReadOnly` / `fbCanWrite` | **生きている**（権限ビット・`UF_IMMUTABLE` の両方を映す） | **生きている** |
| `fbCanExecute` | **ファイルでは生きている**（`/bin/ls` `/usr/bin/true` で `yes`）。**フォルダでは常に `no`** | **生きている** |
| `fbCanRead` | **死んでいる**（`chmod 0000` でも `yes`） | **生きている**（読み取りビットが実際に落ちる） |
| `fbHidden` / `fbSystem` / `fbTemporary` / `fbEncrypted` / `fbArchive` / `fbDirectory` / `fbCanBrowse` | **死んでいる**（常に `no`） | **死んでいる**（黙って捨てられる） |

とくに次の 3 つは言い切ってよい。

- **`fbHidden` は macOS では絶対に立たない。** dot 名でも、macOS が本当に「隠し」と
  扱う `UF_HIDDEN` でも `no` である（ファイル・フォルダとも）。
  **隠し項目の判定に使ってはいけない。**
- **`fbCanRead` は読み取り権限ではない。** `chmod 0000`（所有者すら読めない）でも
  `yes` を返す。#87 で `SystemTempDirectory` だけ `no` だった理由は依然として
  分からないが、**どちらにせよ当てにできない**ことは確定した。
- **`fbSystem` は macOS のシステム領域でも立たない。** `/System/` `/System/Library/`
  `/usr/bin/` `/private/var/db/` `/Library/` `/bin/ls`
  `/System/Library/CoreServices/SystemVersion.plist` `/usr/bin/true` の **8 件すべてで
  `no`**（どれも `fbReadOnly=yes` は正しく返っている＝属性自体は読めている）。

## Windows で確かめたこと（Windows 11 / NTFS）

[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93) の
プローブ `sattributes-windows` で、上の macOS の測り方（#91 の `sattributes-write`）を
Windows の API に置き換えて同じ順に測った。実行環境はログの冒頭のとおり
**Windows 11 25H2（build 26200）Pro・`C:\` は NTFS（`FILE_SUPPORTS_ENCRYPTION=yes`）**
（レジストリの `ProductName` は `Windows 10 Pro` を返すが、これは Windows 11 の既知の癖）。
ディスク側は `stat` の代わりに `GetFileAttributesW` で前後を比べ、「立つはずの標本」は
`chmod` / `chflags` の代わりに `SetFileAttributesW` と `EncryptFileW`（EFS）で作った。
落ちていない（所要 2.13 秒）。

### `SetAttributes`——フォルダは未実装のまま、ファイルは「ファイル属性」を書く

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

- **6 つが書けて、6 つとも読み戻せる。** macOS（書けるのは権限の 4 つ、読み戻せるのは 3 つ）
  とは**書ける旗の顔ぶれがまるで違う**。
- **書き込みの可否は macOS と同じく AND で決まる。** まとめて全部 `true` を書くと
  `READONLY|HIDDEN|SYSTEM|ARCHIVE|TEMPORARY`（`fbReadOnly=true` が勝つ）、全部 `false` を
  書くと **`READONLY` だけ**が残る（`fbCanWrite=false` が勝つ）。
- **構造体まるごとで置き換わる。** 全部 `false` を書くと `HIDDEN` / `SYSTEM` / `ARCHIVE` /
  `TEMPORARY` がすべて落ちた。1 つだけ変えたいときも、**必ず `GetAttributes` で読んだ値を
  書き換えて渡す**（`{}` から作って渡すと、立っていた属性を消す）。
- **本当に書けなくなる。** `fbReadOnly=true`（または `fbCanWrite=false` だけ）を書いた後、
  1 バイト追記しようとすると**開くところで拒まれる**（`err=5` アクセスが拒否されました）。
  初期値を書き戻すと `ARCHIVE` に戻り、また追記できる。
- **`fbEncrypted` は書けない。** 平文へ `true` を書いても、EFS で暗号化したファイルへ
  `false` を書いても、**成功が返るだけで暗号化の状態は変わらない**（下記 6d）。
- `fbCanRead` / `fbCanExecute` を書いたときに**ACL が変わったかは見ていない**
  （見たのは属性ビットだけ）。読み戻しは変わらない。

フォルダを読み取り専用にしようとしても `6` が返るだけで、**その中へ `CreateOnDisk()`
するとふつうに作れる**（macOS と同じ）。実在しない対象への `SetAttributes` も
macOS と同じで、フォルダ `6`・ファイル `1`（`GetAttributes` はフォルダ `51`・ファイル `1`）。

**`SetAttributesTimeDateReference` は 4 つとも `6`**（macOS と同じ）。読むほうは
**macOS と食い違う**:

| 参照 | macOS の `Get…` | Windows の `Get…` |
| --- | --- | --- |
| `Created` | `0` | `0` |
| `LastWritten` | `0` | `0` |
| `LastAccessed` | **`1`** | **`0`**（成功） |
| `LastBackup` | `1` | `1` |

### `GetAttributes`——`FILE_ATTRIBUTE_*` をそのまま映す

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

- **`fbArchive` はふつうに書き込んだだけで立つ。** `ARCHIVE` を落として（`fbArchive=no`）
  1 バイト追記すると、OS が `ARCHIVE` を立て直し、`fbArchive=yes` になった。特権は要らない。
- **`fbEncrypted` は EFS の標本で立つ。** `EncryptFileW` が成功し（`FileEncryptionStatusW=1`、
  OS 側は `ARCHIVE|ENCRYPTED`）、**差は `fbEncrypted=yes` だけ**だった。
- **フォルダの `READONLY` 属性は `fbReadOnly` に出ない。** Windows ではフォルダの `READONLY` は
  「中へ書けない」という意味ではない（Explorer の表示用の印）ので、映さないほうが正しい
  とも言える。ただ、フォルダの `fbReadOnly` が何を見ているのかは分からなかった（次項）。

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

- **`fbReadOnly` / `fbCanWrite` は ACL を見ていない。** `cmd.exe` や `kernel32.dll`、
  `C:\WINDOWS\` は一般ユーザーには書けない（ACL で守られている）のに `fbCanWrite=yes`。
  ファイルについては「`READONLY` 属性が立っているか」だけである。**書けるかどうかの
  当たりとしては、macOS（権限ビットを映す）より弱い。**
- **`System Volume Information` だけフォルダで `fbReadOnly=yes` `fbCanWrite=no` になった。**
  `READONLY` 属性は立っていないので、属性ビットではない何か（このフォルダは管理者でも
  開けない）を見ているらしいが【推定】、`C:\Program Files\`（`READONLY` 属性あり・
  一般ユーザーは書けない）が `no` / `yes` なので、規則は分からない。**フォルダの
  `fbReadOnly` / `fbCanWrite` は Windows では当てにしない。**
- **`fbCanExecute` は Windows では常に `no`。** `cmd.exe`・`kernel32.dll` でも、作った
  `.exe` / `.bat` / `.cmd` / `.com` / `.ps1` / `.vbs` / `.dll` でも `no`（拡張子では決まらない）。
  macOS ではファイルの実行権ビットを映していたので、**これもプラットフォーム差**である。
- **`fbCanRead` は Windows でも常に `yes`**（`System Volume Information` でも）。
- **使用中のファイルでは `GetAttributes` が失敗する。** `pagefile.sys` は `ExistsOnDisk=yes`
  なのに `GetAttributes=1`。同じ場所で `GetFileAttributesW` も `err=32` で失敗しているので、
  中で同じ口を使っているのだろう【推定】。「`GetAttributes` が失敗したら実在しない」とは
  読まない。

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

// ✗ フォルダの属性は書き戻せない（kVCOMError_NotImplemented が返るだけ）
folder->SetAttributes(attrs);          // 何も起きない

// ✗ SAttributes を {} から作って書かない（Windows では構造体まるごとで属性が置き換わり、
//    立っていた HIDDEN / SYSTEM / ARCHIVE / TEMPORARY を消す）
SAttributes fresh = {};
fresh.fbReadOnly = true;
file->SetAttributes(fresh);            // Windows: READONLY だけが残り、ARCHIVE などが落ちる

// ✗ fbHidden / fbSystem / fbArchive / fbTemporary / fbEncrypted / fbCanExecute を
//    両 OS 共通の判定に使わない（前の 5 つは Windows でだけ、fbCanExecute は macOS でだけ生きている）

// ✗ fbReadOnly と fbCanWrite を同時に指定しない（衝突して「書けない」側が勝つ）
attrs.fbReadOnly = false;
attrs.fbCanWrite = false;              // ← こちらが効いて読み取り専用になる
file->SetAttributes(attrs);

// ○ ファイルを読み取り専用にする（触るのは 1 つだけ）
SAttributes attrs = {};
if (VCOM_SUCCEEDED(file->GetAttributes(attrs)))
{
    attrs.fbReadOnly = true;           // macOS: chmod 0444 相当 / Windows: READONLY 属性
    file->SetAttributes(attrs);
}

// ✗ 書いた fbCanRead を読み戻して確かめない（権限は変わるのに yes が返り続ける）
attrs.fbCanRead = false;
file->SetAttributes(attrs);
file->GetAttributes(attrs);
if (!attrs.fbCanRead) { /* ここへは決して来ない */ }

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

## 打ち切った調査

### `fbArchive` / `fbEncrypted` を立てた標本は、macOS では作れない

「`no` を返す」ところまでは測ってあるが、**「立つはずの標本」を作る手段が macOS に
無い**ので、この 2 つは macOS では「読めない」と言い切らずに止めてある。

- **`fbArchive`** … macOS の対応物 `SF_ARCHIVED` は**スーパーユーザでないと立てられない**
  （実測: `chflags` が `errno=1 Operation not permitted`）。プラグインは利用者の権限で
  動くので、**標本を用意できない**。
- **`fbEncrypted`** … macOS には**ファイル単位の暗号化属性が無い**（FileVault は
  ボリューム単位で、個々のファイルに旗は立たない）。**そもそも作れない。**

どちらも `SetAttributes` で書いても**ディスクは 1 ビットも動かなかった**ので、
「macOS の Vectorworks はこの 2 つを扱っていない」ことまでは言える。
**Windows では 2 つとも OS が立てた旗を読めた**（上記「Windows で確かめたこと」。
#93 で解決）。macOS で特権を取ってまで測り直す意味は無いので、ここで打ち切る。
