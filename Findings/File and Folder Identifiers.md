# ファイル・フォルダの識別子（`IFileIdentifier` / `IFolderIdentifier`）

ディスク上の場所を指す 2 つの識別子そのものの話——**属性（`GetAttributes` / `SetAttributes`）・
実在の確認（`ExistsOnDisk`）・中身の列挙（`EnumerateContents`）が、実機で何を返すか**
（[issue #87](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/87)、
[issue #91](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/91)）。

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
| `SetAttributes` で属性を書き戻せるか | **フォルダは不可**（`kVCOMError_NotImplemented`）。**ファイルは「権限だけ」書ける**——`fbReadOnly` / `fbCanWrite` / `fbCanExecute` / `fbCanRead` が POSIX の権限ビットへ届き、**残り 7 つは成功を返して完全に捨てられる**（下記） |
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

> **【未確認】Windows では測っていない。** 上はすべて macOS の実測である
> （[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93)）。

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

// ✗ fbReadOnly と fbCanWrite を同時に指定しない（衝突して「書けない」側が勝つ）
attrs.fbReadOnly = false;
attrs.fbCanWrite = false;              // ← こちらが効いて読み取り専用になる
file->SetAttributes(attrs);

// ○ ファイルを読み取り専用にする（触るのは 1 つだけ）
SAttributes attrs = {};
if (VCOM_SUCCEEDED(file->GetAttributes(attrs)))
{
    attrs.fbReadOnly = true;           // 実際に chmod 0444 相当まで届く
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
無い**ので、この 2 つだけは「読めない」と言い切らずに止めてある。

- **`fbArchive`** … macOS の対応物 `SF_ARCHIVED` は**スーパーユーザでないと立てられない**
  （実測: `chflags` が `errno=1 Operation not permitted`）。プラグインは利用者の権限で
  動くので、**標本を用意できない**。
- **`fbEncrypted`** … macOS には**ファイル単位の暗号化属性が無い**（FileVault は
  ボリューム単位で、個々のファイルに旗は立たない）。**そもそも作れない。**

どちらも `SetAttributes` で書いても**ディスクは 1 ビットも動かなかった**ので、
「Vectorworks 側がこの 2 つを扱っていない」ことまでは言える。残っているのは
**「OS が立てた旗なら読めるのか」**だけで、それは Windows でなければ確かめられない
（次項）。

### Windows では測っていない

上はすべて macOS の実測である。**`fbArchive` / `fbSystem` / `fbHidden` / `fbEncrypted`
は、どれも Windows のファイル属性そのものの名前**（`FILE_ATTRIBUTE_ARCHIVE` /
`_SYSTEM` / `_HIDDEN` / `_ENCRYPTED`）なので、**あちらでは埋まる見込みがある**【推定】。
Windows 実機が要るため
[issue #93](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/93)
へ切り出した（**Windows 実機が手に入るまで着手できない**）。

ここが確かめられると、「この 4 つは Windows 専用の概念で、macOS では死んでいる」と
**プラットフォーム差として言い切れる**ようになる。
