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
| **`ISDK::CallPluginLibrary`**（プラグインライブラリルーチンを名前で呼ぶ） | **これが本命。** 相手が `kVLIBScopeUniversal` か `kVLIBScopeSDKOnly` で登録していれば呼べる | **最大 11 本＋結果 1 つ。** 型つき（文字列・数値・点・**ハンドル**・2 次元配列ほか）。`…Var` / `…InOut` の出力・入出力版もある |
| **`ISDK::DoMenuName`**（メニューコマンドを内部名で起動） | **ある**（長らく「無い」と書いていた。下記【訂正】） | **渡せない。** 名前と chunk 番号だけ、戻りは `short` 1 つ |
| **スクリプトエンジン**（`IVectorScriptEngine` / `IPythonScriptEngine`） | 使える（[Undo](Undo.md)「間接経路」で実機確認済み） | スクリプトの**文字列を組んで**渡す。結果は Python のロガー経由 |
| **C の ABI**（`dlsym` / `GetProcAddress`） | 技術的には可能だが**勧めない**（下記 4） | — |

**呼ぶなら `CallPluginLibrary`。** 名前で結ばれるので相手の実装やビルドに縛られず、
引数と結果が型つきで往復し、相手は `scope` で「誰に呼ばせるか」を選べる。

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
ユニバーサル名でもファイル名でもない**【ソース根拠】。

### 呼べるかは提供側の `scope` 次第

VectorScript のライブラリ関数（関数ライブラリの拡張機能）を SDK から呼べるかは、
**相手がどの scope で登録したか**で決まる（`Kernel/API/MiniCadCallBacks.h:1224-1227`）
【ソース根拠】:

| 定数 | 値 | 誰が呼べるか |
| --- | --- | --- |
| `kVLIBScopeUniversal` | 0 | **VS・SDK・VW のどこからでも** |
| `kVLIBScopeVSOnly` | 1 | **VectorScript だけ。SDK からは呼べない** |
| `kVLIBScopeSDKOnly` | 2 | SDK プラグインだけ |
| `kVLIBScopeNemetschekOnly` | 255 | VW 本体だけ（VS も SDK も不可） |

**相手が `kVLIBScopeVSOnly` で登録していたら、`CallPluginLibrary` では届かない**——
そのときに残るのは経路 3（スクリプトエンジンに `vs.` から呼ばせる）だけである。

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
`…InOutArgType`（入出力）がある【ソース根拠】。

**ハンドルが渡せる**（`kHandleArgType` = 25 / `kHandleVarArgType` = 26）のが要点で、
図面のオブジェクトをそのまま相手へ渡せる。文字列は 255 文字を超えるなら
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
（なお、このコードは `#ifdef _VWFC_FOR_VW125x` の中——VW 12.5 時代の互換層で、
SDK の中で `_VWFC_FOR_VW125x` を定義している場所は無い【ヘッダ根拠】。）

**呼ぶ側は `VWFC::PluginSupport::VWPluginLibraryArgTable` を使う。** 既定
コンストラクタが 11 本＋結果を `kNullArgType` で埋め、`operator PluginLibraryArgTable*()`
でそのまま `CallPluginLibrary` へ渡せる（`VWPluginLibraryArgTable.cpp:17`）
【ソース根拠】。

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
VAR のまま保たれる【ソース根拠】。

### 結果は**型を見てから**読む

失敗した呼び出しでは `functionResult.argType` が `kNullArgType` のまま残る。
`VWPluginLibraryArgument` の getter は型が合わないと `VWFC_ASSERT` を踏むので、
**`argType` を見てから読む**こと【ソース根拠】。`GS_CallPluginLibrary` の戻り値
（`Boolean`）と結果欄は別物で、SDK 自身の用例も**戻り値を見てから、さらに結果が
nil でないかを確かめている**（上記 `IIFCSupport.h`）。

第 3 引数 `status` の用途は**SDK のどこにも書かれていない**（doc コメントは 1 行だけ、
`MockSDK.h` の実装も素通し）。**SDK 自身の用例はどれも `0` を渡している**ので、
`0` を渡す【ソース根拠】。

### 呼ばれる側を C++ で提供する

登録するのは **`IExtensionVSFunctions`**（グループ `GROUPID_ExtensionVSFunctions`。
`Interfaces/VectorWorks/Extension/IExtensionVSFunctions.h`）で、呼び出しの受け口は
**`IVSFunctionsEventSink`**（`IID_VSFunctionsEventSink`。`IExtension.h:187`）
【ソース根拠】。VWFC の足場は 3 つ:

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
  想定外の添字が来たら表の外を読む。**範囲検査は自分で書く**。
- **`Initialize()` が `kExtensionVSFunctionsInitFlag_DontOpenResource` を返せば
  `.vwr` の VLIB リソースが要らない。** 関数の定義はコードの表が持っているので、
  リソース側に同じものを置かずに済む（既定の `_None` だと VW がリソースを探しにいく）
  【ソース根拠】。

**最小の実装例**（このリポジトリの実機確認プラグインに置いてある）:

- 呼ばれる側: [`plugin/src/ProbeLibrary.h`](../plugin/src/ProbeLibrary.h) /
  [`plugin/src/ProbeLibrary.cpp`](../plugin/src/ProbeLibrary.cpp)（登録は
  [`plugin/src/ModuleMain.cpp`](../plugin/src/ModuleMain.cpp)）
- 呼ぶ側: `probes/runtime/plugin-call-by-name/probe.cpp`

**登録は殻（起動時に読まれる側）に置くほかない。** VW は `plugin_module_main` で
受け取った番地を握り続けるので、**入れ替わる本体（`.vwpayload`）には置けない**
（[プラグインモジュール](Plug-in%20Modules.md)「まだ確かめていないこと」の
「本体側からイベントを登録する」と同じ話）。呼ぶ側は本体でよい。

## 2. メニューコマンドを内部名で起動する——`ISDK::DoMenuName`

**【訂正】ある。** このリポジトリは長らく 3 か所で「汎用のメニュー起動 API は
ISDK / VWFC に無い」と書いていたが、**誤りだった**（[Undo](Undo.md)「打ち切った調査:
プラグインから `DoMenuTextByName` 相当を呼ぶ」・
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
コマンドはダイアログを出す。

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

**技術的には同じプロセス内なので可能だが、設計として選ばない。**【推定】を含む判断で、
実機で試していない（試す値打ちが無いと判断した。理由は下記）。

- **相手の殻が公開しているのは `plugin_module_main` と `plugin_module_ver` だけ**
  （SDK の作法。[プラグインモジュール](Plug-in%20Modules.md)）。それ以外の名前は
  C++ のマングリング済みで、**相手のコンパイラ・標準ライブラリ・ビルド設定に縛られる**。
- **相手の版が上がった瞬間に静かに壊れる。** 名前解決は実行時なので、コンパイルも
  リンクも通る。`CallPluginLibrary` は VW が名前で仲介するので、相手の実装が変わっても
  署名が同じなら壊れない——**壊れ方が「呼べなかった（戻り値 false）」に収まる**のが
  決定的な違いである。
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

**「ID を取って呼ぶ」経路は SDK に公開されていない。** `ExternalNameToID` の doc
コメント（`APIBase.Legacy.Defs.h:6664`）は

> This returns the ID of an external library file, allowing it to be called by the
> **XCALL interface** which takes an ID. It is faster than calling an external by
> file name.

と言うが、**`XCALL` を SDK 全体で検索して出てくるのは、SDK 自身の内部マクロの
コメント行 2 つだけ**（`Source/VWSDK/Kernel/API/APIBase.Legacy.Defs.cpp:25` の
`// #define XCALL(fn) …`——コールバック表を引くための、この doc とは無関係な同名マクロ）
【ソース根拠】。**ID を受け取って外部を呼ぶ関数は ISDK にも VWFC にも無い。**

したがって `ExternalNameToID` の使い道は「名前から ID が引けるか＝その外部が
在るか」の判定に限られる。

## 相手が居ないとき・読み込み順・呼べる時機

### 事前確認に `HasPlugin` は使えない

`HasPlugin(itemUniversalName, VAR PaletteName)` は **VectorScript にしか無い**
（`Include/vs.py:21432`。ISDK に相当する宣言は無い）【ヘッダ根拠】。しかも
説明は

> Returns whether tool item or menu command is **in current workspace**.

——**「いまのワークスペースに在るか」**を見るだけで、「そのプラグインが入っているか・
読み込めたか」ではない【ソース根拠】。ワークスペースに並べていないコマンドを持つ
プラグインは、入っていても `false` になる。**事前確認の道具としては向かない。**

### 読み込み順は気にしなくてよい

コンパイル済みプラグインは**起動時に全部読み込まれ、そこで拡張機能の登録も終わる**
（[プラグインモジュール](Plug-in%20Modules.md)）。呼ぶのが起動後の要求時だけなら、
呼ぶ側と呼ばれる側のどちらが先に読み込まれていても関係ない【推定】。

## まだ実機で確かめていないこと

**この節は issue #217 の範囲内**である（プローブ `plugin-call-by-name` で測る）。
埋まったらこの節を消して、本文の印を外す。

| 確かめること | いまの水準 |
| --- | --- |
| 殻が登録したルーチンを、別モジュール（本体）から名前で呼べるか | 【ソース根拠】 |
| 文字列・数値・VAR 引数が実際に往復するか | 【ソース根拠】 |
| 実在しないルーチン名のときの戻り値（落ちずに失敗を知れるか） | 【推定】 |
| `DoMenuName` が実際にコマンドを起動するか・戻り値の意味 | 【ソース根拠】 |
| `DoMenuName` と undo の記録の関係 | 未確認 |
| `ExternalNameToID` が「在る / 無い」で分かれるか | 未確認 |
| **OS タイマーの刻みの中から `CallPluginLibrary` を呼べるか** | 未確認 |

## 参考

- [プラグインモジュール](Plug-in%20Modules.md) — 読み込みの時機、殻と本体、C の ABI
- [Undo](Undo.md) — スクリプトエンジン経由の実測（取得の IID・落ちる組み合わせ）
- [周期実行と通知](Timers%20and%20Notifications.md) — OS タイマーから `gSDK` を呼ぶ
- [VectorScript → SDK の対応](VectorScript%20to%20SDK%20Mapping.md)
