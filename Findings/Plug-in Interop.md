# プラグイン間の呼び出し（ほかのプラグインの機能を名前で呼ぶ）

あるプラグインモジュール（呼ぶ側）から、**別のプラグインモジュールの機能を名前で
呼び出す**手段。引数と結果をどう受け渡すか、呼べる時機、相手が居ないときの振る舞い
（[issue #217](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/217)）。

「プラグインがいつ読み込まれるか・殻と本体に割る話」は
[プラグインモジュール](Plug-in%20Modules.md)。こちらは**読み込み済みのモジュール同士が
どう呼び合うか**である。

## 結論（先に）

| 経路 | 使えるか | 引数と結果 |
| --- | --- | --- |
| **`ISDK::CallPluginLibrary`**（プラグインライブラリルーチンを名前で呼ぶ） | **これが本命。モジュールを跨いで実際に呼べた**（実機確認済み）。相手が `kVLIBScopeUniversal` か `kVLIBScopeSDKOnly` で登録していれば呼べる | **最大 11 本＋結果 1 つ。** 文字列・数値・**VAR（出力）引数**が往復するのを実測した。型は点・ハンドル・2 次元配列まである |
| **`ISDK::DoMenuName`**（メニューコマンドを内部名で起動） | **ある**（長らく「無い」と書いていた。下記【訂正】）。**名前解決が働くことは実測した**（在る名前と無い名前で戻り値が分かれる） | **渡せない。** 名前と chunk 番号だけ、戻りは `short` 1 つ |
| **スクリプトエンジン**（`IVectorScriptEngine` / `IPythonScriptEngine`） | 使える（[Undo](Undo.md)「間接経路」で実機確認済み） | スクリプトの**文字列を組んで**渡す。結果は Python のロガー経由 |
| **C の ABI**（`dlsym` / `GetProcAddress`） | 技術的には可能だが**勧めない**（下記 4） | — |

**呼ぶなら `CallPluginLibrary`。** 名前で結ばれるので相手の実装やビルドに縛られず、
引数と結果が型つきで往復し、相手は `scope` で「誰に呼ばせるか」を選べる。
**OS タイマーの刻みの中からも呼べる**（下記「呼べる時機」）。

## 1. `ISDK::CallPluginLibrary` — プラグインライブラリルーチンを名前で呼ぶ

```cpp
// Include/Interfaces/VectorWorks/ISDK.h:1712
virtual Boolean CallPluginLibrary(const TXString& routineName,
                                  PluginLibraryArgTable* argumentTable,
                                  Sint32 status) = 0;
// 旧: APIBase.Legacy.Defs.h:6564 / GS_CallPluginLibrary（kcbCallPluginLibrary = 667）
//   /* Call an SDK Plug-in Library Routine. */   ← doc コメントはこの 1 行だけ
```

`routineName` は**プラグインライブラリルーチンの名前**である——提供側が
`SFunctionDef::fName` に書いた名前（または VLIB リソースの名前）で、**プラグインの
ユニバーサル名でもファイル名でもない**。

### 実測: 別々のモジュールの間で、名前だけで呼べた

**呼べる。** 呼ぶ側を**入れ替わる本体モジュール**（`.vwpayload`）に、呼ばれる側を
**殻**（`.vwlibrary`）に置いて測った。**2 つは別々の dylib で、互いのシンボルを一切
知らない**——間を取り持つのは VW の名前解決だけである（macOS / VW 2026 /
Apple Silicon。プローブ全体で 0.19 秒）。

| 呼んだもの | 戻り値 | 結果欄（`functionResult`） | 出力引数 |
| --- | --- | --- | --- |
| `VwSdkProbes_NoSuchRoutine_217`（**居ない相手**） | **`false`** | `argType=0`（`kNullArgType`。**何も書かれない**） | — |
| `VwSdkProbes_Echo("i217")` | `true` | `argType=18`（`kStringArgType`）= `"echo:i217"` | — |
| `VwSdkProbes_Sum(40, 2)` | `true` | `argType=3`（`kLongArgType`）= `42` | — |
| `VwSdkProbes_Out("i217", VAR, VAR)` | `true` | `argType=16`（`kBooleanArgType`）= `true` | `args[1]`: `argType=19`（`kStringVarArgType`）= `"out:i217"` / `args[2]`: `argType=4`（`kLongVarArgType`）= `4` |

読み取れること:

- **文字列・数値・真偽が双方向に通る。** 入力は `args[]`、結果は `functionResult`、
  **VAR（出力）引数は呼ぶ側が渡した `args[]` の欄に書き戻される。**
- **居ない相手に当てても落ちない。** 戻り値が `false` になり、`functionResult.argType`
  は `kNullArgType` のまま残る。**呼ぶ側は戻り値だけで安全に失敗を知れる**
  （事前確認は要らない。下記「相手が居ないとき」）。
- **成否は戻り値、値は結果欄。** 失敗した呼び出しの結果欄は `kNullArgType` なので、
  **型を見てから読む**（getter は型が合わないと `VWFC_ASSERT` を踏む）。

実装例（このリポジトリ）: 呼ばれる側が
[`plugin/src/ProbeLibrary.h`](../plugin/src/ProbeLibrary.h) /
[`plugin/src/ProbeLibrary.cpp`](../plugin/src/ProbeLibrary.cpp)（登録は
[`plugin/src/ModuleMain.cpp`](../plugin/src/ModuleMain.cpp)）。

### 呼べるかは提供側の `scope` 次第

VectorScript のライブラリ関数（関数ライブラリの拡張機能）を SDK から呼べるかは、
**相手がどの scope で登録したか**で決まる（`Kernel/API/MiniCadCallBacks.h:1224-1227`）
【ソース根拠】:

| 定数 | 値 | 誰が呼べるか |
| --- | --- | --- |
| `kVLIBScopeUniversal` | 0 | **VS・SDK・VW のどこからでも**（上の実測はこれで登録した） |
| `kVLIBScopeVSOnly` | 1 | **VectorScript だけ。SDK からは呼べない** |
| `kVLIBScopeSDKOnly` | 2 | SDK プラグインだけ |
| `kVLIBScopeNemetschekOnly` | 255 | VW 本体だけ（VS も SDK も不可） |

**相手が `kVLIBScopeVSOnly` で登録していたら、`CallPluginLibrary` では届かない**——
そのときに残るのは経路 3（スクリプトエンジンに `vs.` から呼ばせる）だけである
【ソース根拠。VSOnly の相手を実機で試してはいない】。

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

型（`EPluginLibraryArgType`。`MiniCadCallBacks.h:1121` 以降）は
**整数・長整数・実数・角度・距離・点・グローバル点・3D 点・ベクトル・真偽・文字列・
文字・文字の動的配列・ハンドル・色・スタイル・`void*`・関数参照・手続き参照・ポインタ・
2 次元の動的配列**が揃っていて、多くに `…VarArgType`（出力）と
`…InOutArgType`（入出力）がある【ヘッダ根拠。実機で通したのは文字列・長整数・真偽と、
その VAR 版】。

**ハンドルが渡せる**（`kHandleArgType` = 25 / `kHandleVarArgType` = 26）のが要点で、
図面のオブジェクトをそのまま相手へ渡せる【ヘッダ根拠】。文字列は 255 文字を超えるなら
`kCharDynarrayArgType` を使う——SDK の実装コメントが
「`kCharDynarray*` の欄に `SetArgString` を使うと Python 側で落ちる。
`SetArgDynArrayChar` を使え」と明示している（`VWPluginLibraryArgTable.cpp:298-310`）
【ソース根拠】。

### 【罠】生の `PluginLibraryArgTable` を自分で宣言しない

`PluginLibraryArg::argType` は**コンストラクタで初期化されない**。11 本のうち 2 本だけ
埋めて渡せば、**残り 9 本の `argType` はスタックのゴミ**になる。

**SDK に同梱されている唯一の用例が、まさにそれをやっている**——
`Include/Interfaces/VectorWorks/Extension/IIFCSupport.h` は IFC プラグインから
インターフェースを引くのに

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

と書いている。**仕掛けの読み方としては最良の実例だが、この初期化の仕方は真似しない。**

**呼ぶ側は `VWFC::PluginSupport::VWPluginLibraryArgTable` を使う。** 既定
コンストラクタが 11 本＋結果を `kNullArgType` で埋め、`operator PluginLibraryArgTable*()`
でそのまま `CallPluginLibrary` へ渡せる（`VWPluginLibraryArgTable.cpp:17`）
【ソース根拠。上の実測もこれで組んだ】。

**参考: `IFC_QueryInterface` はこのビルドでは引けなかった**（戻り値 `false` /
結果欄 `kNullArgType`）。あのコードは `#ifdef _VWFC_FOR_VW125x` の中——VW 12.5 時代の
互換層で、SDK の中で `_VWFC_FOR_VW125x` を定義している場所は無い【ヘッダ根拠】。
**ただし「ルーチンが無い」のか「何にも一致しない IID を渡したから `false` が返った」のかは
区別できない**（どちらも戻り値 `false`）。issue #217 の範囲外なので、ここは詰めていない。

### `argType` は setter が立てる（ただし VAR の文字列だけは例外）

| したいこと | 書き方 | 立つ `argType` |
| --- | --- | --- |
| 数値を渡す | `table.GetArgument(0).SetArgLong(42)` | `kLongArgType` |
| 文字列を渡す | `table.GetArgument(0).SetArgString("x")` | `kStringArgType` |
| **VAR の数値**を用意する | `table.GetArgument(1).GetArgLongVar() = 0` | `kLongVarArgType`（`kNullArgType` のときだけ立つ） |
| **VAR の文字列**を用意する | **setter が無い。** 生の `argType` を自分で立てる | `raw->args[1].argType = kStringVarArgType;` |

VAR の文字列に setter が無いのは、`SetArgString` が
「既に `kStringVarArgType` / `kStringInOutArgType` でなければ `kStringArgType` にする」
作りだから（`VWPluginLibraryArgTable.cpp:311`）——つまり**先に型を立ててから**書けば
VAR のまま保たれる。**この手で実際に `"out:i217"` が返ってきた**（上の実測の 4 行目）。

第 3 引数 `status` の用途は**SDK のどこにも書かれていない**（doc コメントは 1 行だけ、
`MockSDK.h` の実装も素通し）。**SDK 自身の用例はどれも `0` を渡している**ので、
`0` を渡す（上の実測も全部 `0`）。

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

関数の表は `SFunctionDef`。**`fName == nullptr` で終端**する約束で、
`GetFunctionsCount()` が nullptr まで数える（`VWExtensionVSFunctions.cpp:102`）
【ソース根拠】。

```cpp
struct SFunctionDef {
    const char*       fName;            // ← CallPluginLibrary / vs.* から指す名前
    const char*       fCategory;
    const char*       fDescription;
    Sint32            fVersion;
    Sint8             fScope;           // kVLIBScope…（上記の表）
    bool              fHasReturnValue;
    SFunctionParamDef fParams[11];      // { 名前, 型 }。こちらも nullptr 終端
};
```

**`Execute` に来る `action` は関数名ではなく「表の添字」である**【ソース根拠】。
`VWVSFunctions_EventSink::Execute` は登録済みのルーチン全部に
`DispatchRoutine(action, argTable)` を回すだけで、名前への引き直しは実装側の仕事
（SDK の `ADD_LIB_FUNCTION_Ex` は `TArr[routineSelector].fName` を `strcmp` する）。

- **`BEGIN_LIB_DISPATCH_MAP_Ex` は添字の上限を見ない**（`routineSelector < 0` だけ）。
  想定外の添字が来たら表の外を読む。**範囲検査は自分で書く。**
- **`Initialize()` が `kExtensionVSFunctionsInitFlag_DontOpenResource` を返せば
  `.vwr` の VLIB リソースが要らない。** 関数の定義はコードの表が持っているので、
  リソース側に同じものを置かずに済む（既定の `_None` だと VW がリソースを探しにいく）。
  **この形で実機の登録が通った**（上の実測はリソースを 1 行も足していない）。

**登録は殻（起動時に読まれる側）に置くほかない。** VW は `plugin_module_main` で
受け取った番地を握り続けるので、**入れ替わる本体（`.vwpayload`）には置けない**
（[プラグインモジュール](Plug-in%20Modules.md)「まだ確かめていないこと」の
「本体側からイベントを登録する」と同じ話）。**呼ぶ側は本体でよい**（上の実測がそれ）。

## 2. メニューコマンドを内部名で起動する——`ISDK::DoMenuName`

**【訂正】ある。** このリポジトリは長らく 3 か所で「メニューコマンドを名前で起動する
汎用 API は ISDK / VWFC に無い」と書いていたが、**誤りだった**（[Undo](Undo.md)
「打ち切った調査: プラグインから `DoMenuTextByName` 相当を呼ぶ」・
[VectorScript → SDK の対応](VectorScript%20to%20SDK%20Mapping.md)・
[文書](Documents.md)）。

```cpp
// Include/Interfaces/VectorWorks/ISDK.h:1722
virtual short DoMenuName(const TXString& name, short chunkIndex) = 0;
// 旧: APIBase.Legacy.Defs.h:6631 / GS_DoMenuName（kcbDoMenuName = 199）
```

doc コメント（`APIBase.Legacy.Defs.h:6632-6639`）【ソース根拠】:

> Executes the menu specified by name. Calls menu items by the name of the external
> menu file or the name of the internal MITM resource. Using the file and resource
> names allows externals to work on any localized version of MiniCad. If the name is
> a chunk, then the chunkIndex should be the number of the item in the chunk to
> execute. If the item is not a chunk then 0 must be passed in chunkIndex.
> DoMenuName can be called recursively.

つまり:

- **`DoMenuTextByName(subMenu, index)` の SDK 版**である。指すのは**表示名ではなく
  内部名**——VW 内蔵のコマンドなら MITM リソースの名前、**プラグインのコマンドなら
  そのファイル名**（`vs.py` の `DoMenuTextByName` の注記も
  「when calling VS plug-ins, you have to use the filename」と同じことを言っている）。
  だから**ローカライズ版でもそのまま動く**。
- **chunk でなければ `chunkIndex` は 0**（chunk なら chunk 内の位置）。
- **再帰的に呼んでよい**と明記されている。

**引数と結果は渡せない。** `name` と `chunkIndex` だけで、戻りは `short` 1 つ。
引数と結果を往復させたいなら経路 1 を使い、この経路は「とにかくあのコマンドを
走らせる」専用と考える。**ダイアログを抑える引数も無い**ので、ダイアログを出す
コマンドはダイアログを出す【ヘッダ根拠】。

### 実測: 名前解決は働く（在る名前と無い名前で戻り値が分かれる）

macOS / VW 2026。メニューコマンドの中（実機確認プラグインの本体）から呼んだ:

| 呼んだもの | 戻り値 |
| --- | --- |
| `DoMenuName("VwSdkProbes_NoSuchCommand_217", 0)`（**居ないコマンド**） | **`-3`** |
| `DoMenuName("Undo", 0)`（実在する内蔵コマンドの内部名） | **`0`** |

- **居ないコマンドでも落ちない。** 固有の負値（`-3`）が返るので、**呼ぶ側は戻り値で
  失敗を知れる**。
- **`0` と `-3` が分かれた＝名前で引く仕組みは働いている。**
- undo の記録は動かなかった（`IsCurrentlyBuildingAnUndoEvent()` は前後とも `no`、
  直前に undo イベントの外で作った locus も残った）。これは
  [Undo](Undo.md)「undo イベントの外で作ったものは取り消しスタックに載らない」と
  **整合する**——が、空図面で取り消すものが無かっただけの可能性もあるので、
  **「コマンドが実際に走った」ことの確証にはならない**（下記「まだ実機で確かめて
  いないこと」）。

### なぜ「無い」と確定してしまったか（取りこぼしの教訓）

[issue #27](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/27)
は「メニュー項目を実行する」に当たりそうな語を総当たりした——`ExecuteMenuItem` /
`SelectMenuItem` / `PerformMenuCommand` / `PostMenuCommand` / `SendMenuCommand` /
`CallMenuHandler` / `InvokeCommand` / `ExecuteCommand` / `DoMenuText` /
`DoMenuTextByName`。**そこに `DoMenuName` が無かった**。VectorScript 側の名前
（`DoMenuTextByName`）から素直に縮めた綴りが正解だったのに、「`Text` が落ちる」形を
試していない。

**教訓: 語の総当たりで「無い」と確定させない。** `ISDK` の宣言は
[`SDK Index/`](../SDK%20Index/README.md) に全部載っているので、
**「`Menu` を含む `ISDK` の宣言」を機械的に並べて目で見る**ほうが速く、取りこぼさない。
[VectorScript → SDK の対応](VectorScript%20to%20SDK%20Mapping.md)に「無い」と書く前には
これをやること。

## 3. スクリプトエンジン経由

`IVectorScriptEngine::ExecuteScript` / `IPythonScriptEngine::ExecuteScript` で
スクリプトを走らせ、その中から相手のライブラリ関数やメニューコマンドを呼ぶ経路。
**取得の IID・落ちる組み合わせ・エラーの拾い方は
[Undo](Undo.md)「間接経路: スクリプトエンジン経由で `DoMenuTextByName` 相当を呼ぶ」で
実機確認済み**なので、そちらを読む。要点だけ:

- **Python から `vs.*` を使うなら `ExecuteScript` を呼んではいけない——落ちる。**
  `ScriptContext_Begin` → `ScriptContext_Run` を使う。
- `VCOMError = 0` は「実行時エラーが起きなかった」を意味しない。失敗を**呼び出し側で**
  知る手段は Python のロガー（`IPythonLogger`）だけ。
- 取り消しの実行は、呼び出し側が開いている undo イベントを終わらせる。

この調査で足せるのは 1 点だけ:
**`IVectorScriptEngine::CallUserFunction(void* functionRef, PluginLibraryArgTable*, size_t)`
／ `CallUserProcedure` は、経路 1 と同じ引数テーブルを使う**
（`IVectorScriptEngine.h:37-38` / `IPythonScriptEngine.h:40-41`）。ただし
`functionRef` の出どころは `VWPluginLibraryArgument::GetArgFunctionRef()`
（`kFunctionkArgType` = 37）——**スクリプトから引数として渡された関数参照**なので、
**プラグインから勝手に作ることはできない**【ヘッダ根拠】。スクリプトがコールバックを
渡してきたときに呼び返すための口であって、「相手の関数を名前で引く」口ではない。

**scope が `kVLIBScopeVSOnly` の相手に届く唯一の道がこの経路**である（経路 1 は弾かれる）。
代償は、引数を**スクリプトの文字列として組む**ことと、エラーの拾いにくさ。

## 4. 勧めない経路: C の ABI（`dlsym` / `GetProcAddress`）

**技術的には同じプロセス内なので可能だが、設計として選ばない。** 実機で試していない
（試す値打ちが無いと判断した。理由は下記）。

- **相手の殻が公開しているのは `plugin_module_main` と `plugin_module_ver` だけ**
  （SDK の作法。[プラグインモジュール](Plug-in%20Modules.md)）。それ以外の名前は
  C++ のマングリング済みで、**相手のコンパイラ・標準ライブラリ・ビルド設定に縛られる**。
- **相手の版が上がった瞬間に静かに壊れる。** 名前解決は実行時なので、コンパイルも
  リンクも通る。`CallPluginLibrary` は VW が名前で仲介するので、相手の実装が変わっても
  署名が同じなら壊れない——**壊れ方が「呼べなかった（戻り値 `false`）」に収まる**のが
  決定的な違いである（上の実測で、居ない相手でも落ちないことを確かめた）。
- **自分の殻と自分の本体の間で C の ABI を使うのは別の話。** あれは
  **版を揃えて同じ zip で配れる**からで（しかも `VW_PAYLOAD_ABI_VERSION` で食い違いを
  実行時に検出している。[プラグインモジュール](Plug-in%20Modules.md)）、他人の
  プラグインとは揃えられない。
- mac では VW のプロセス内で `dlopen` できること自体は実測済み（アドホック署名で足りる。
  同上）。だから「できない」のではなく、**できても得が無い**。

## 打ち切った調査: `ISDK::DoProgramAction`

**使わない。** `virtual Sint32 DoProgramAction(short actionSelector, void* actionEnv)`
（`ISDK.h:1723`）の doc コメントは **"Does program action." の 1 行だけ**で、
選択子は `MiniCadCallBacks.h:423` の

```cpp
// Selectors for DoProgramAction
enum { doSelector0 = 0, doSelector1 = 1, ..., doSelector9 = 9 };
```

——**名前だけの列挙**である【ソース根拠】。どの番号が何をするのか、`actionEnv` に
どの型を渡すのかは SDK のどこにも書かれておらず、同梱の実装ソースにも用例が無い。
**当てずっぽうで呼ぶと何が起きるか分からない**（`void*` を受けるので、型を外せば落ちる）。
issue #217 の候補として挙がっていたが、**手掛かりが無いので調査の対象にしない**。

## 打ち切った調査: `ISDK::ExternalNameToID` と XCALL

**事前確認にも、呼び出しにも使えない。**

`ExternalNameToID` の doc コメント（`APIBase.Legacy.Defs.h:6664`）は

> This returns the ID of an external library file, allowing it to be called by the
> **XCALL interface** which takes an ID. It is faster than calling an external by
> file name.

と言うが、**`XCALL` を SDK 全体で検索して出てくるのは、SDK 自身の内部マクロの
コメント行 2 つだけ**（`Source/VWSDK/Kernel/API/APIBase.Legacy.Defs.cpp:25` の
`// #define XCALL(fn) …`——コールバック表を引くための、この doc とは無関係な同名マクロ）
【ソース根拠】。**ID を受け取って外部を呼ぶ関数は ISDK にも VWFC にも無い。**

**「在るかどうかの判定」にも使えなかった**（実機。macOS / VW 2026）:

| 呼んだもの | 戻り値 |
| --- | --- |
| `ExternalNameToID("VwSdkProbes")`（**実際に読み込まれているプラグイン**） | **`-1`** |
| `ExternalNameToID("NoSuchExternal217")` | **`-1`** |

**在る名前でも `-1` が返るので、在る / 無いを区別できない。** 相手の有無は
`CallPluginLibrary` の戻り値で判断する（下記）。

## 相手が居ないとき・読み込み順・呼べる時機

### 事前確認は要らない——戻り値で足りる

**`CallPluginLibrary` は居ない相手に当てても落ちず、`false` を返す**（上の実測）。
だから「呼んでみて `false` なら相手が居ない（か、相手が失敗した）」で足りる。
事前に確かめる道具は、次のとおりどれも使えない:

- **`HasPlugin`** は **VectorScript にしか無い**（`Include/vs.py:21432`。ISDK に相当する
  宣言は無い）【ヘッダ根拠】。しかも説明は
  > Returns whether tool item or menu command is **in current workspace**.

  ——**「いまのワークスペースに在るか」**を見るだけで、「そのプラグインが入っているか・
  読み込めたか」ではない【ソース根拠】。ワークスペースに並べていないコマンドを持つ
  プラグインは、入っていても `false` になる。
- **`ExternalNameToID`** は在る名前でも `-1`（上記）。

**`DoMenuName` も同じ**——居ないコマンドでは `-3` が返る（上の実測）。

### 読み込み順は気にしなくてよい

コンパイル済みプラグインは**起動時に全部読み込まれ、そこで拡張機能の登録も終わる**
（[プラグインモジュール](Plug-in%20Modules.md)）。呼ぶのが起動後の要求時だけなら、
呼ぶ側と呼ばれる側のどちらが先に読み込まれていても関係ない【推定】。
上の実測では、**呼ぶ側（本体モジュール）は呼ばれる側（殻）より後から `dlopen` された**
——その順でも名前で引けた。

### 呼べる時機: OS タイマーの刻みの中からも呼べる

**呼べる（実機確認済み）。** `CFRunLoopTimer` を `kCFRunLoopCommonModes` に仕掛け、
その刻みの中から `CallPluginLibrary("VwSdkProbes_Echo", …)` を呼んだところ、
**戻り値 `true`・結果欄に `"echo:from-timer"`** が返った（macOS / VW 2026）。

つまり**メニューコマンドの中でなくても呼べる**——[周期実行と通知](Timers%20and%20Notifications.md)
で確かめた「OS のタイマーから `gSDK` を読める・書ける」の延長線上にある。
**外部からの要求を OS タイマーで受けて、そこからほかのプラグインの機能を呼ぶ**という
形（CLI ブリッジ）が成り立つ。

undo の記録との関係は経路ごとの話ではなく、**呼ばれた側が何をするか**で決まる。
刻みが VW の undo イベント中に当たったときの作法は
[周期実行と通知](Timers%20and%20Notifications.md)「VW が undo イベントを開いたまま
回している最中」を読む（**読むのは安全・書くと混ざる**）。

## まだ実機で確かめていないこと

| 確かめること | いまの水準 | 扱い |
| --- | --- | --- |
| **`DoMenuName` が実際にコマンドを「実行」するか** | 名前解決が働くこと（`0` と `-3` の分岐）までは実測。**走ったことの確証は無い** | **issue #217 の範囲内。測り直す**（スクリプトエンジンに取り消しスタックへ積ませてから `DoMenuName("Undo", 0)` を呼び、消えるかを数える） |
| `kVLIBScopeVSOnly` の相手が本当に弾かれるか | 【ソース根拠】 | 範囲外（相手の登録を変えた版が要る） |
| `IFC_QueryInterface` が VW 2026 に在るのか | 戻り値 `false` のみ。「無い」とは言えない | 範囲外（参考として測っただけ） |
| ハンドル・2 次元配列の引数 | 【ヘッダ根拠】 | 範囲外（必要になったときに測る） |
| Windows での挙動 | 未測定（ビルドは通る） | 範囲外（[プラグインモジュール](Plug-in%20Modules.md)の Windows 未測定と同じ） |

## 参考

- [プラグインモジュール](Plug-in%20Modules.md) — 読み込みの時機、殻と本体、C の ABI
- [Undo](Undo.md) — スクリプトエンジン経由の実測（取得の IID・落ちる組み合わせ）
- [周期実行と通知](Timers%20and%20Notifications.md) — OS タイマーから `gSDK` を呼ぶ
- [VectorScript → SDK の対応](VectorScript%20to%20SDK%20Mapping.md)
