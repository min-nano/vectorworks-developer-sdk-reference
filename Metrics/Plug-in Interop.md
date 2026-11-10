# プラグイン間の呼び出し（ほかのプラグインの機能を名前で呼ぶ） — 計測記録

結論は [Findings「Plug-in Interop」](../Findings/Plug-in%20Interop.md)。ここには実測の表・
宣言とソースの引用・登録のコード・#233 の書き出しのずれと訂正の経緯・`DoMenuName` の
確かめ方・打ち切った経路の実測を元の文言のまま置く。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

[issue #217](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/217)・
[issue #233](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/233) の調査。

## 測った環境

- **#217**: macOS / VW 2026 / Apple Silicon。実機確認プラグイン（VwSdkProbes）の、呼ぶ側を
  入れ替わる本体（`.vwpayload`）・呼ばれる側を殻（`.vwlibrary`）に置いた（プローブ全体で 0.19 秒）。
- **#233**: VW 2026。mac / Windows（[PR #237](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/237)）。

## 1. `ISDK::CallPluginLibrary` — プラグインライブラリルーチンを名前で呼ぶ

```cpp
// Include/Interfaces/VectorWorks/ISDK.h:1712
virtual Boolean CallPluginLibrary(const TXString& routineName,
                                  PluginLibraryArgTable* argumentTable,
                                  Sint32 status) = 0;
// 旧: APIBase.Legacy.Defs.h:6564 / GS_CallPluginLibrary（kcbCallPluginLibrary = 667）
//   /* Call an SDK Plug-in Library Routine. */   ← doc コメントはこの 1 行だけ
```

### 実測: 別々のモジュールの間で、名前だけで呼べた

呼ぶ側を**入れ替わる本体モジュール**（`.vwpayload`）に、呼ばれる側を
**殻**（`.vwlibrary`）に置いて測った。**2 つは別々の dylib で、互いのシンボルを一切
知らない**（macOS / VW 2026 / Apple Silicon。プローブ全体で 0.19 秒）。

| 呼んだもの | 戻り値 | 結果欄（`functionResult`） | 出力引数 |
| --- | --- | --- | --- |
| `VwSdkProbes_NoSuchRoutine_217`（**居ない相手**） | **`false`** | `argType=0`（`kNullArgType`。**何も書かれない**） | — |
| `VwSdkProbes_Echo("i217")` | `true` | `argType=18`（`kStringArgType`）= `"echo:i217"` | — |
| `VwSdkProbes_Sum(40, 2)` | `true` | `argType=3`（`kLongArgType`）= `42` | — |
| `VwSdkProbes_Out("i217", VAR, VAR)` | `true` | `argType=16`（`kBooleanArgType`）= `true` | `args[1]`: `argType=19`（`kStringVarArgType`）= `"out:i217"` / `args[2]`: `argType=4`（`kLongVarArgType`）= `4` |

実装例（このリポジトリ）: 呼ばれる側が
[`plugin/src/ProbeLibrary.h`](../plugin/src/ProbeLibrary.h) /
[`plugin/src/ProbeLibrary.cpp`](../plugin/src/ProbeLibrary.cpp)（登録は
[`plugin/src/ModuleMain.cpp`](../plugin/src/ModuleMain.cpp)）。

### 呼べるかは提供側の `scope` 次第

`Kernel/API/MiniCadCallBacks.h:1224-1227` の定義:

| 定数 | 値 | 誰が呼べるか |
| --- | --- | --- |
| `kVLIBScopeUniversal` | 0 | **VS・SDK・VW のどこからでも**（上の実測はこれで登録した） |
| `kVLIBScopeVSOnly` | 1 | **VectorScript だけ。SDK からは呼べない** |
| `kVLIBScopeSDKOnly` | 2 | SDK プラグインだけ |
| `kVLIBScopeNemetschekOnly` | 255 | VW 本体だけ（VS も SDK も不可） |

`kVLIBScopeVSOnly` の相手は実機で試していない。

### 引数テーブル（最大 11 本＋結果 1 つ）

```cpp
// Include/Kernel/API/MiniCadCallBacks.h:1217
const short kMaxPluginLibraryArgs = 11;
struct PluginLibraryArgTable {
    PluginLibraryArg args[kMaxPluginLibraryArgs];
    PluginLibraryArg functionResult;      // 結果はここ 1 つ
};

struct PluginLibraryArg {
    EPluginLibraryArgType argType;        // この引数の型
    union {
        double_gs realValue;  Boolean  boolValue;  UCChar   charValue;
        short     intValue;   Sint32   longValue;  GSHandle handleValue;
        _WorldPt  ptValue;    _WorldPt3 pt3Value;  _WorldPt3 vecValue;
        unsigned char styleValue;  void* voidData;  void* ptrValue;
        struct { Sint32 redValue, greenValue, blueValue; } colorVar;
        struct { EPluginLibraryArgType valueType; Sint32 cntRows, cntCols;
                 void* buffer; size_t bufferSize; } arrayVar;
    };
    TXString strValue;                    // 文字列は union の外
};
```

型（`EPluginLibraryArgType`。`MiniCadCallBacks.h:1121` 以降）には
**整数・長整数・実数・角度・距離・点・グローバル点・3D 点・ベクトル・真偽・文字列・
文字・文字の動的配列・ハンドル・色・スタイル・`void*`・関数参照・手続き参照・ポインタ・
2 次元の動的配列**が並び、多くに `…VarArgType`（出力）と `…InOutArgType`（入出力）がある。
実機で通したのは文字列・長整数・真偽と、その VAR 版。

ハンドルは `kHandleArgType` = 25 / `kHandleVarArgType` = 26。SDK の実装コメントは
「`kCharDynarray*` の欄に `SetArgString` を使うと Python 側で落ちる。
`SetArgDynArrayChar` を使え」と書いている（`VWPluginLibraryArgTable.cpp:298-310`）。

### 引数テーブルを自分で宣言したときの実測

`PluginLibraryArg::argType` はコンストラクタで初期化されない。

SDK に同梱されている唯一の用例 `Include/Interfaces/VectorWorks/Extension/IIFCSupport.h` は、
IFC プラグインからインターフェースを引くのに次のように書いている:

```cpp
PluginLibraryArgTable callTable;                       // ← 初期化していない
callTable.args[0].argType  = kVoidPtr;
callTable.args[0].voidData = (void*) gCBP;
callTable.args[1].argType  = kVoidPtr;
callTable.args[1].voidData = (void*) & iid;
if ( ::GS_CallPluginLibrary( gCBP, TXString("IFC_QueryInterface"),
                             (PluginLibraryArgTable*) & callTable, 0 ) ) {
    fPtr = (T*) callTable.functionResult.voidData;
}
```

`VWFC::PluginSupport::VWPluginLibraryArgTable` の既定コンストラクタは 11 本＋結果を
`kNullArgType` で埋め、`operator PluginLibraryArgTable*()` でそのまま `CallPluginLibrary` へ
渡せる（`VWPluginLibraryArgTable.cpp:17`）。上の実測はこれで組んだ。

**参考: `IFC_QueryInterface` はこのビルドでは引けなかった**（戻り値 `false` /
結果欄 `kNullArgType`）。あのコードは `#ifdef _VWFC_FOR_VW125x` の中で、SDK の中で
`_VWFC_FOR_VW125x` を定義している場所は無かった。渡した IID が何にも一致しないときも戻り値は
`false` なので、この走行では両者を区別していない（issue #217 の範囲外）。

### `argType` は setter が立てる（ただし VAR の文字列だけは例外）

| したいこと | 書き方 | 立つ `argType` |
| --- | --- | --- |
| 数値を渡す | `table.GetArgument(0).SetArgLong(42)` | `kLongArgType` |
| 文字列を渡す | `table.GetArgument(0).SetArgString("x")` | `kStringArgType` |
| **VAR の数値**を用意する | `table.GetArgument(1).GetArgLongVar() = 0` | `kLongVarArgType`（`kNullArgType` のときだけ立つ） |
| **VAR の文字列**を用意する | **setter が無い。** 生の `argType` を自分で立てる | `raw->args[1].argType = kStringVarArgType;` |

`SetArgString` は「既に `kStringVarArgType` / `kStringInOutArgType` でなければ
`kStringArgType` にする」作り（`VWPluginLibraryArgTable.cpp:311`）。先に型を立ててから書いて、
実際に `"out:i217"` が返ってきた（上の実測の 4 行目）。

第 3 引数 `status` の用途は SDK のどこにも書かれていなかった（doc コメントは 1 行だけ、
`MockSDK.h` の実装も素通し）。SDK 自身の用例はどれも `0` を渡しており、上の実測も全部 `0`。

### 呼ばれる側を C++ で提供する

登録するのは **`IExtensionVSFunctions`**（グループ `GROUPID_ExtensionVSFunctions`。
`Interfaces/VectorWorks/Extension/IExtensionVSFunctions.h`）で、呼び出しの受け口は
**`IVSFunctionsEventSink`**（`IID_VSFunctionsEventSink`。`IExtension.h:187`）。
VWFC の足場は 3 つ:

| 要るもの | VWFC |
| --- | --- |
| 拡張機能そのもの | `VWExtensionVSFunctions(cbp, const SFunctionDef* arrFunctions)` |
| 受け口 | `VWVSFunctions_EventSink`（`AddRoutine(routine, isLocalMemory)`） |
| 実装の受け皿 | `VWPluginLibraryRoutine::DispatchRoutine(selector, argTable)` |
| 登録マクロ | `DEFINE_VWVSFunctionsExtension` / `IMPLEMENT_VWVSFunctionsExtension` |

関数の表は `SFunctionDef`。`GetFunctionsCount()` は `fName` が nullptr になるまで数える
（`VWExtensionVSFunctions.cpp:102`）。

```cpp
struct SFunctionDef {
    const char*       fName;            // ← CallPluginLibrary / vs.* から指す名前
    const char*       fCategory;
    const char*       fDescription;
    Sint32            fVersion;
    Sint8             fScope;           // kVLIBScope…（上記の表）
    bool              fHasReturnValue;
    SFunctionParamDef fParams[11];      // { 名前, 型 }。こちらも nullptr 終端。
                                        // fHasReturnValue なら最後の欄が戻り値（下記【罠】）
};
```

`VWVSFunctions_EventSink::Execute` は登録済みのルーチン全部に
`DispatchRoutine(action, argTable)` を回すだけで、名前への引き直しをしていない
（SDK の `ADD_LIB_FUNCTION_Ex` は `TArr[routineSelector].fName` を `strcmp` する）。

- `BEGIN_LIB_DISPATCH_MAP_Ex` が見ているのは `routineSelector < 0` だけで、上限を見ていない。
- `Initialize()` の既定 `_None` では VW が `.vwr` の VLIB リソースを探しにいく。
  `kExtensionVSFunctionsInitFlag_DontOpenResource` を返す形で、リソースを 1 行も足さずに
  実機の登録が通った（上の実測）。
- 登録は殻に置き、呼ぶ側は本体に置いて呼べた（上の実測）。

### 戻り値の型の伝え方と、書き出される署名の実測

**実測**（VW 2026。mac / Windows。[issue #233](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/233) /
[PR #237](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/237)）。

`IExtensionVSFunctions` が VW に渡すのは「戻り値があるか」（`GetFunctionHasReturnValue`）と
「引数の並び」（`GetFunctionParamsCount` / `Name` / `Type`）だけだった
（`IExtensionVSFunctions.h` / `VWExtensionVSFunctions.cpp:164-205`。SDK に使用例は無い）。
直した後の表:

```cpp
{kLibSumName, "VwSdkProbes", "2 つの整数の和を返す", 1, kVLIBScopeUniversal,
 /*fHasReturnValue*/ true,
 {{"a", kLongArgType}, {"b", kLongArgType}, {"result", kLongArgType}}},  // ← 最後が戻り値
```

- VW は起動時に Plug-ins 直下へ `VWPluginLibraryRoutines.h` / `.p`（と
  `WorksheetFunctionOptionsRegistry`）を書き出す。そこに載った定義:

  | 実際の署名 | 欄を足す前の書き出し（`.p`） | 足した後 |
  | --- | --- | --- |
  | `Echo(input: STRING): STRING` | `VwSdkProbes_Echo() : STRING` | `VwSdkProbes_Echo(input: STRING) : STRING` |
  | `Sum(a, b: LONGINT): LONGINT` | `VwSdkProbes_Sum(a: LONGINT) : LONGINT` | `VwSdkProbes_Sum(a: LONGINT; b: LONGINT) : LONGINT` |
  | `Out(input; VAR outText; VAR outCount): BOOLEAN` | `VwSdkProbes_Out(input: STRING; VAR outText: STRING) VAR : LONGINT` | `VwSdkProbes_Out(input: STRING; VAR outText: STRING; VAR outCount: LONGINT) : BOOLEAN` |

- `ISDK::CallPluginLibrary` での受け渡しは、欄を足す前の #217 の実測も、足した後の
  mac / Windows の実測も同じで、`Echo` → `"echo:i233"`・`Sum(40, 2)` → `42`・`Out` → `true` と
  VAR 引数の書き戻しが通った。結果は `GetResult()` の欄に入り、実装が読む `GetArgument(i)` の
  添字は変わらなかった。
- 書き出しは起動のたびに作り直された（Windows。殻を替えずに起動しても、ずれた 3 ファイルを
  置いたまま直した殻へ替えても、その起動で今の定義どおりに書き直された）。
- **【訂正の記録】** 以前はここに「ずれたまま `.vlb` を差し替えると起動画面で毎回落ちる
  （`0xe0000001`）・3 ファイルを退けると起動する」と書いていた。後の切り分け（issue #233）:
  - 当時落ちた殻（`b125451`）は、**3 ファイルが無くても**同じ `0xe0000001` で落ちた。
  - ずれた 3 ファイルを置いたまま、直して留めた殻（`fd379cf`）へ替えると起動した。
  - 原因は殻の時計が起動時の読み込み直しで古い番地を指したまま残ることだった
    （[プラグインモジュール](../Findings/Plug-in%20Modules.md)「【罠】Windows の VW は起動時に
    モジュールを一度降ろして読み込み直す」の「#233 の `0xe0000001` も同じもの」）。
- VectorScript から呼んだときにどうなるかは確かめていない。

## 2. メニューコマンドを内部名で起動する——`ISDK::DoMenuName`

**【訂正の記録】** このリポジトリは長らく 3 か所で「メニューコマンドを名前で起動する
汎用 API は ISDK / VWFC に無い」と書いていた（[Undo](../Findings/Undo.md)
「打ち切った調査: プラグインから `DoMenuTextByName` 相当を呼ぶ」・
[VectorScript → SDK の対応](../Findings/VectorScript%20to%20SDK%20Mapping.md)・
[文書](../Findings/Documents.md)）。#217 で次の宣言を見つけた。

```cpp
// Include/Interfaces/VectorWorks/ISDK.h:1722
virtual short DoMenuName(const TXString& name, short chunkIndex) = 0;
// 旧: APIBase.Legacy.Defs.h:6631 / GS_DoMenuName（kcbDoMenuName = 199）
```

doc コメント（`APIBase.Legacy.Defs.h:6632-6639`）:

> Executes the menu specified by name. Calls menu items by the name of the external
> menu file or the name of the internal MITM resource. Using the file and resource
> names allows externals to work on any localized version of MiniCad. If the name is
> a chunk, then the chunkIndex should be the number of the item in the chunk to
> execute. If the item is not a chunk then 0 must be passed in chunkIndex.
> DoMenuName can be called recursively.

`vs.py` の `DoMenuTextByName` の注記は「when calling VS plug-ins, you have to use the
filename」と書いている。

### 実測: 名前で引けて、コマンドが実際に走る

macOS / VW 2026。メニューコマンドの中（実機確認プラグインの本体）から呼んだ:

| 呼んだもの | 戻り値 |
| --- | --- |
| `DoMenuName("VwSdkProbes_NoSuchCommand_217", 0)`（**居ないコマンド**） | **`-3`** |
| `DoMenuName("Undo", 0)`（実在する内蔵コマンドの内部名） | **`0`** |

1 回目は新規の空図面で測ったので取り消すものが無く、`DoMenuName("Undo", 0)` が `0` を返しても
**何も起きなかった**——直前に undo イベントの外で作った locus も残り（[Undo](../Findings/Undo.md)
「undo イベントの外で作ったものは取り消しスタックに載らない」どおり）、
`IsCurrentlyBuildingAnUndoEvent()` も前後とも `no` のままだった。これでは「名前は引けるが
実行はされない」と区別できなかった。

そこで**取り消しスタックに確実に 1 つ積んでから**測り直した。積む役はスクリプト
エンジン——`ExecuteScript` で図形を作ると**スクリプトエンジンが undo イベントを開いて
積み、開いたまま返す**（同上で実測済み）:

| 手順 | 結果 |
| --- | --- |
| `ExecuteScript("Layer('i217-undo-target');")` | `VCOMError=0`・**レイヤができた** |
| 積んだ後の `IsCurrentlyBuildingAnUndoEvent()` | **`yes`**（スクリプトエンジンが開いたまま返した） |
| `DoMenuName("Undo", 0)` | **`0`** |
| 呼んだ後、そのレイヤは在るか | **無い（消えた）** |
| 呼んだ後の `IsCurrentlyBuildingAnUndoEvent()` | **`no`** |

積んだレイヤが消えた（所要 0.01 秒）。開いていた undo イベントは `yes` → `no` になった
——[Undo](../Findings/Undo.md) でスクリプト経由の `DoMenuTextByName('Undo', 0)` について測った
挙動と同じ。

### なぜ「無い」と確定してしまったか（取りこぼしの教訓）

[issue #27](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/27)
は「メニュー項目を実行する」に当たりそうな語を総当たりした——`ExecuteMenuItem` /
`SelectMenuItem` / `PerformMenuCommand` / `PostMenuCommand` / `SendMenuCommand` /
`CallMenuHandler` / `InvokeCommand` / `ExecuteCommand` / `DoMenuText` /
`DoMenuTextByName`。**そこに `DoMenuName` が無かった**。VectorScript 側の名前
（`DoMenuTextByName`）から「`Text` が落ちる」形を試していなかった。

## 3. スクリプトエンジン経由

スクリプトエンジン（`IVectorScriptEngine::ExecuteScript` / `IPythonScriptEngine::ExecuteScript`）の
実測は [Undo](../Findings/Undo.md)「間接経路: スクリプトエンジン経由で `DoMenuTextByName` 相当を呼ぶ」
にある。この調査ではヘッダだけを読んだ:

- `IVectorScriptEngine::CallUserFunction(void* functionRef, PluginLibraryArgTable*, size_t)`
  ／ `CallUserProcedure` は経路 1 と同じ引数テーブルを取る
  （`IVectorScriptEngine.h:37-38` / `IPythonScriptEngine.h:40-41`）。`functionRef` の出どころは
  `VWPluginLibraryArgument::GetArgFunctionRef()`（`kFunctionkArgType` = 37）。

## 4. C の ABI（`dlsym` / `GetProcAddress`）の経路

この経路は実機で試していない。関連する実測は
[プラグインモジュール](../Findings/Plug-in%20Modules.md) にある——mac で VW のプロセス内に
`dlopen` できること（アドホック署名で足りる）と、自分の殻と本体の間の
`VW_PAYLOAD_ABI_VERSION` による食い違いの検出。

## 打ち切った調査: `ISDK::DoProgramAction`

`virtual Sint32 DoProgramAction(short actionSelector, void* actionEnv)`
（`ISDK.h:1723`）の doc コメントは **"Does program action." の 1 行だけ**で、
選択子は `MiniCadCallBacks.h:423` の

```cpp
// Selectors for DoProgramAction
enum { doSelector0 = 0, doSelector1 = 1, ..., doSelector9 = 9 };
```

だった。どの番号が何をするのか、`actionEnv` にどの型を渡すのかは SDK のどこにも書かれて
おらず、同梱の実装ソースにも用例が無かった。issue #217 の候補として挙がっていたが、実機では
呼んでいない。

## 打ち切った調査: `ISDK::ExternalNameToID` と XCALL

`ExternalNameToID` の doc コメント（`APIBase.Legacy.Defs.h:6664`）:

> This returns the ID of an external library file, allowing it to be called by the
> **XCALL interface** which takes an ID. It is faster than calling an external by
> file name.

`XCALL` を SDK 全体で検索して出てきたのは、SDK 自身の内部マクロのコメント行 2 つだけ
（`Source/VWSDK/Kernel/API/APIBase.Legacy.Defs.cpp:25` の `// #define XCALL(fn) …`
——コールバック表を引くための、この doc とは無関係な同名マクロ）。ID を受け取って外部を
呼ぶ関数は ISDK にも VWFC にも見つからなかった。

実機（macOS / VW 2026）:

| 呼んだもの | 戻り値 |
| --- | --- |
| `ExternalNameToID("VwSdkProbes")`（**実際に読み込まれているプラグイン**） | **`-1`** |
| `ExternalNameToID("NoSuchExternal217")` | **`-1`** |

## 相手が居ないとき・読み込み順・呼べる時機

### 事前確認は要らない——戻り値で足りる

- 居ない相手への `CallPluginLibrary` は `false`、居ないコマンドへの `DoMenuName` は `-3`
  （上の実測）。
- **`HasPlugin`** は `Include/vs.py:21432` にあり、ISDK に相当する宣言は無かった。説明は
  > Returns whether tool item or menu command is **in current workspace**.
- `ExternalNameToID` は在る名前でも `-1`（上記）。

### 読み込み順は気にしなくてよい

上の実測では、**呼ぶ側（本体モジュール）は呼ばれる側（殻）より後から `dlopen` された**
——その順でも名前で引けた。

### 呼べる時機: OS タイマーの刻みの中からも呼べる

`CFRunLoopTimer` を `kCFRunLoopCommonModes` に仕掛け、その刻みの中から
`CallPluginLibrary("VwSdkProbes_Echo", …)` を呼んだところ、**戻り値 `true`・結果欄に
`"echo:from-timer"`** が返った（macOS / VW 2026）。
