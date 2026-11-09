# ファイル・フォルダを選ばせるダイアログ — 計測記録

結論は [Findings「File and Folder Dialogs」](../Findings/File%20and%20Folder%20Dialogs.md)。
ここにはヘッダの抜き書きと、実機で測った戻り値・受け取ったパスの実物を置く
（[issue #85](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/85)）。

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

## 実機で確かめたこと（VW 2026 / macOS / Apple Silicon）

**プラグインのメニューコマンドの中（`DoInterface` の中）から、しかも外部モジュールへ
出した本体（ペイロード）側で**、実機確認プラグイン（VwSdkProbes）のプローブとして
走らせた実測。**印の無い記述は実機確認済み。** プローブは 1 回走らせ、落ちていない。

### 本体（ペイロード）側から開ける

`IFolderChooserDialogPtr folderDlg(IID_FolderChooserDialog);` が**非 nullptr**を返し、
`RunDialog()` が実際に OS のフォルダ選択ダイアログを出した。[Plug-in
Modules](../Findings/Plug-in%20Modules.md)「本体側からダイアログを開ける」に `IFileChooserDialog` で
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
  TXString& fileName)` を使う**のが作法（[Documents](../Findings/Documents.md) の `CatchSystemTest.cpp`
  の用例と同じ形）。
- **UTF-8 である。** この 160 という数字がその根拠——同じパスを UTF-8 で数えると、
  ASCII 部分 78 バイト＋`共有ドライブ/` 19＋`02_アーカイブ/` 19＋
  `20260529_遠山信夫アトリエ_安藤邸/` 44 で**ちょうど 160 バイト**になる（日本語 1 文字＝
  3 バイト）。UTF-16 でも Shift_JIS でもこの数にはならない。`GetName` の値もそのまま
  日本語として読める。**`TXString` の `operator const char*` は UTF-8 を返す**という
  [TXString](../Findings/TXString.md) の前提が、ここでも保たれている。

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
（実在しなくても戻り値は `0` を返す。[File and Folder Identifiers](../Findings/File%20and%20Folder%20Identifiers.md)）。

**これはダイアログに固有の話でも、測った場所に固有の話でもない。**
[issue #87](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/87) で
掴み方と場所を散らした 34 件（ローカル・VW の導入先・その場で作った新品・
`EnumerateContents` が配ったもの・クラウドストレージ配下）を測り直したところ、
**フォルダ 30 件・ファイル 4 件のすべてで `fbDirectory=false`** だった。
`fbDirectory` は**常に `false`** だと思ってよい（macOS 実測。Windows は未確認）。
識別子そのものの話は
[File and Folder Identifiers](../Findings/File%20and%20Folder%20Identifiers.md) にまとめてある。

