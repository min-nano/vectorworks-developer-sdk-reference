# プラグイン間の呼び出し（ほかのプラグインの機能を名前で呼ぶ）

あるプラグインモジュール（呼ぶ側）から、**別のプラグインモジュールの機能を名前で
呼び出す**手段。引数と結果をどう受け渡すか、呼べる時機、相手が居ないときの振る舞い
（[issue #217](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/217)・
[issue #233](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/233)）。
主に macOS / VW 2026 で確認（#233 は mac / Windows）。実測の表・宣言の引用・経緯は
[計測記録](../Metrics/Plug-in%20Interop.md)。

「プラグインがいつ読み込まれるか・殻と本体に割る話」は
[プラグインモジュール](Plug-in%20Modules.md)。こちらは**読み込み済みのモジュール同士が
どう呼び合うか**である。

## 結論（先に）

| 経路 | 使えるか | 引数と結果 |
| --- | --- | --- |
| **`ISDK::CallPluginLibrary`**（プラグインライブラリルーチンを名前で呼ぶ） | **これが本命。モジュールを跨いで実際に呼べた**（実機確認済み）。相手が `kVLIBScopeUniversal` か `kVLIBScopeSDKOnly` で登録していれば呼べる | **最大 11 本＋結果 1 つ。** 文字列・数値・**VAR（出力）引数**が往復するのを実測した。型は点・ハンドル・2 次元配列まである |
| **`ISDK::DoMenuName`**（メニューコマンドを内部名で起動） | **ある**（長らく「無い」と書いていた。下記【訂正】）。**名前で引けて、コマンドが実際に走ることまで実測した** | **渡せない。** 名前と chunk 番号だけ、戻りは `short` 1 つ |
| **スクリプトエンジン**（`IVectorScriptEngine` / `IPythonScriptEngine`） | 使える（[Undo](Undo.md)「間接経路」で実機確認済み） | スクリプトの**文字列を組んで**渡す。結果は Python のロガー経由 |
| **C の ABI**（`dlsym` / `GetProcAddress`） | 技術的には可能だが**勧めない**（下記 4） | — |

**呼ぶなら `CallPluginLibrary`。** 名前で結ばれるので相手の実装やビルドに縛られず、
引数と結果が型つきで往復し、相手は `scope` で「誰に呼ばせるか」を選べる。
**OS タイマーの刻みの中からも呼べる**（下記「呼べる時機」）。
**提供側は戻り値の型を `fParams` の最後の欄で伝える**（下記【罠】）。

## 1. `ISDK::CallPluginLibrary` — プラグインライブラリルーチンを名前で呼ぶ

- 宣言は `ISDK.h:1712`（`CallPluginLibrary(routineName, PluginLibraryArgTable*, Sint32 status)`）。
  `routineName` は提供側が `SFunctionDef::fName` に書いた名前で、**プラグインのユニバーサル名でも
  ファイル名でもない**（[宣言](../Metrics/Plug-in%20Interop.md#1-isdkcallpluginlibrary--プラグインライブラリルーチンを名前で呼ぶ)）。

### 実測: 別々のモジュールの間で、名前だけで呼べた

（macOS。互いのシンボルを知らない別々の dylib の間。[実測](../Metrics/Plug-in%20Interop.md#実測-別々のモジュールの間で名前だけで呼べた)）

- **文字列・数値・真偽が双方向に通る。** 入力は `args[]`、結果は `functionResult`、
  **VAR（出力）引数は呼ぶ側が渡した `args[]` の欄に書き戻される。**
- **居ない相手に当てても落ちない**——戻り値 `false`・結果欄は `kNullArgType` のまま。
  **成否は戻り値、値は結果欄。型を見てから読む**（getter は型が合わないと `VWFC_ASSERT`）。
- 実装例: [`plugin/src/ProbeLibrary.cpp`](../plugin/src/ProbeLibrary.cpp)（登録は
  [`plugin/src/ModuleMain.cpp`](../plugin/src/ModuleMain.cpp)）。

### 呼べるかは提供側の `scope` 次第

- `kVLIBScopeUniversal`（0）は VS・SDK・VW のどこからでも、`kVLIBScopeSDKOnly`（2）は SDK だけ、
  **`kVLIBScopeVSOnly`（1）は SDK から呼べない**、`kVLIBScopeNemetschekOnly`（255）は VW 本体だけ
  【ソース根拠】（[表](../Metrics/Plug-in%20Interop.md#呼べるかは提供側の-scope-次第)）。
- VSOnly の相手に残るのは経路 3 だけ【ソース根拠。実機で試してはいない】。

### 引数テーブル（最大 11 本＋結果 1 つ）

- `kMaxPluginLibraryArgs = 11` ＋ `functionResult`。型（`EPluginLibraryArgType`）は点・ハンドル・
  色・関数参照・2 次元の動的配列まで揃い、多くに `…VarArgType` / `…InOutArgType` がある
  【ヘッダ根拠。実機で通したのは文字列・長整数・真偽と、その VAR 版】
  （[構造体](../Metrics/Plug-in%20Interop.md#引数テーブル最大-11-本結果-1-つ)）。
- **ハンドルが渡せる**（`kHandleArgType` = 25 / `kHandleVarArgType` = 26）【ヘッダ根拠】。
  255 文字を超える文字列は `kCharDynarrayArgType` に `SetArgDynArrayChar`（`SetArgString` は
  Python 側で落ちる）【ソース根拠】。

### 【罠】生の `PluginLibraryArgTable` を自分で宣言しない

- **`argType` はコンストラクタで初期化されない**——埋めなかった欄はスタックのゴミ。SDK 同梱の
  唯一の用例（`IIFCSupport.h` の `IFC_QueryInterface`）がまさにそれをやっているので**真似しない**
  （[引用](../Metrics/Plug-in%20Interop.md#罠生の-pluginlibraryargtable-を自分で宣言しない)）。
- **呼ぶ側は `VWFC::PluginSupport::VWPluginLibraryArgTable` を使う**（全欄を `kNullArgType` で埋め、
  そのまま渡せる）【ソース根拠。上の実測もこれで組んだ】。
- `IFC_QueryInterface` はこのビルドでは引けなかったが、「無い」のか IID が合わないのかは区別
  できない（範囲外。詰めていない）。

### `argType` は setter が立てる（ただし VAR の文字列だけは例外）

- `SetArgLong` / `SetArgString` / `GetArgLongVar()` が型を立てる。**VAR の文字列には setter が無い**
  ——**先に `raw->args[1].argType = kStringVarArgType;` を立ててから** `SetArgString` で書けば VAR の
  まま保たれ、実際に `"out:i217"` が返った（[表](../Metrics/Plug-in%20Interop.md#argtype-は-setter-が立てるただし-var-の文字列だけは例外)）。
- 第 3 引数 `status` の用途は SDK に書かれていない。**`0` を渡す**（SDK の用例も実測も `0`）。

### 呼ばれる側を C++ で提供する

（[VWFC の足場と `SFunctionDef`](../Metrics/Plug-in%20Interop.md#呼ばれる側を-c-で提供する)）

- 登録するのは **`IExtensionVSFunctions`**、受け口は **`IVSFunctionsEventSink`**。VWFC の
  `VWExtensionVSFunctions` / `VWVSFunctions_EventSink` / `VWPluginLibraryRoutine` と登録マクロで組む。
  関数の表 `SFunctionDef` は **`fName == nullptr` で終端**【ソース根拠】。
- **`Execute` に来る `action` は関数名ではなく表の添字**【ソース根拠】。
  **`BEGIN_LIB_DISPATCH_MAP_Ex` は添字の上限を見ないので、範囲検査は自分で書く。**
- **`Initialize()` が `kExtensionVSFunctionsInitFlag_DontOpenResource` を返せば `.vwr` の VLIB
  リソースが要らない**（この形で実機の登録が通った）。
- **登録は殻（起動時に読まれる側）に置くほかない**（VW が番地を握り続けるため）。**呼ぶ側は本体でよい。**

### 【罠】戻り値の型は `fParams` の**最後の欄**で伝える——書かないと署名がずれて書き出される

（#233 / [PR #237](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/237)。mac / Windows。
[書き出しの比較](../Metrics/Plug-in%20Interop.md#罠戻り値の型は-fparams-の最後の欄で伝える書かないと署名がずれて書き出される)）

- **`fHasReturnValue = true` の関数は、`fParams` の最後に戻り値の欄（名前と型）を足す。** 戻り値の
  型を渡す口が無く【ソース根拠】、**VW は並びの最後の欄を戻り値として読む**。

  ```cpp
  {kLibSumName, "VwSdkProbes", "2 つの整数の和を返す", 1, kVLIBScopeUniversal,
   /*fHasReturnValue*/ true,
   {{"a", kLongArgType}, {"b", kLongArgType}, {"result", kLongArgType}}},  // ← 最後が戻り値
  ```

- 足さないと、起動時に Plug-ins 直下へ書き出される `VWPluginLibraryRoutines.h` / `.p` の署名で
  **最後の引数が戻り値に化ける**。**`CallPluginLibrary` での受け渡しはどちらでも同じに通るので、
  SDK から呼んでいるだけでは気付けない。** 書き出しは起動のたびに作り直される（Windows）。
- **【訂正】ずれた書き出しが Windows の起動時の落ち（`0xe0000001`）を招く、は誤りだった。** 原因は
  殻の時計が起動時の読み込み直しで古い番地を指したまま残ること（[プラグインモジュール](Plug-in%20Modules.md)
  「【罠】Windows の VW は起動時にモジュールを一度降ろして読み込み直す」）。
- 帰結: ライブラリ関数を足したら、**書き出された `VWPluginLibraryRoutines.p` を 1 度開いて署名を
  見る**。VectorScript から呼んだときにどうなるかは確かめていない。

## 2. メニューコマンドを内部名で起動する——`ISDK::DoMenuName`

- **【訂正】ある。** 長らく 3 か所で「メニューコマンドを名前で起動する汎用 API は無い」と書いて
  いたが誤り（[Undo](Undo.md)「打ち切った調査: プラグインから `DoMenuTextByName` 相当を呼ぶ」・
  [VectorScript → SDK の対応](VectorScript%20to%20SDK%20Mapping.md)・[文書](Documents.md)）。
- `ISDK.h:1722` の `DoMenuName(const TXString& name, short chunkIndex)`。**`DoMenuTextByName` の
  SDK 版**で、指すのは**表示名ではなく内部名**（内蔵は MITM リソース名、プラグインはファイル名）
  ——ローカライズ版でもそのまま動く。chunk でなければ `chunkIndex` は 0。再帰的に呼んでよい
  （doc コメント。【ソース根拠】。[引用](../Metrics/Plug-in%20Interop.md#2-メニューコマンドを内部名で起動するisdkdomenuname)）。
- **引数と結果は渡せない**（戻りは `short` 1 つ）。**ダイアログを抑える引数も無い**【ヘッダ根拠】。
  「とにかくあのコマンドを走らせる」専用。

### 実測: 名前で引けて、コマンドが実際に走る

（macOS。[手順と表](../Metrics/Plug-in%20Interop.md#実測-名前で引けてコマンドが実際に走る)）

- **居ないコマンドは `-3`、実在する `"Undo"` は `0`**——落ちず、戻り値で失敗を知れる。
- **本当にコマンドを実行する。** スクリプトエンジンで取り消しスタックに 1 つ積んでから
  `DoMenuName("Undo", 0)` を呼ぶと、積んだレイヤが消えた（空の図面では何も起きないので、確かめる
  なら積んでから）。
- **取り消しの実行は、開いていた undo イベントを終わらせる**（`yes` → `no`）。取り消しを起こす
  コマンドを呼ぶなら、**自分の undo イベントを開いたままにしない**（[Undo](Undo.md)）。

### なぜ「無い」と確定してしまったか（取りこぼしの教訓）

- #27 の語の総当たりに `DoMenuName` が入っていなかった（[試した語](../Metrics/Plug-in%20Interop.md#なぜ無いと確定してしまったか取りこぼしの教訓)）。
- **教訓: 語の総当たりで「無い」と確定させない。** [`SDK Index/`](../SDK%20Index/README.md) で
  **「`Menu` を含む `ISDK` の宣言」を機械的に並べて目で見る**。「無い」と書く前にこれをやる。

## 3. スクリプトエンジン経由

`IVectorScriptEngine` / `IPythonScriptEngine` の `ExecuteScript` でスクリプトを走らせ、その中から
相手を呼ぶ経路。**取得の IID・落ちる組み合わせ・エラーの拾い方は [Undo](Undo.md)「間接経路:
スクリプトエンジン経由で `DoMenuTextByName` 相当を呼ぶ」で実機確認済み**（要点:
**Python から `vs.*` を使うなら `ExecuteScript` は落ちる**ので `ScriptContext_Begin` →
`ScriptContext_Run`・`VCOMError = 0` は成功を意味せず失敗は `IPythonLogger` でしか知れない・
取り消しの実行は開いている undo イベントを終わらせる）。

- **`CallUserFunction` / `CallUserProcedure` は経路 1 と同じ引数テーブルを使う**が、`functionRef` は
  **スクリプトから渡された関数参照**（`GetArgFunctionRef()`）で、プラグインから作れない【ヘッダ根拠】
  ——呼び返し用の口であって、名前で引く口ではない（[宣言](../Metrics/Plug-in%20Interop.md#3-スクリプトエンジン経由)）。
- **scope が `kVLIBScopeVSOnly` の相手に届く唯一の道がこの経路**。代償は引数をスクリプトの文字列
  として組むことと、エラーの拾いにくさ。

## 4. 勧めない経路: C の ABI（`dlsym` / `GetProcAddress`）

**技術的には可能だが設計として選ばない**（実機で試していない——試す値打ちが無いと判断した。
[理由の全文](../Metrics/Plug-in%20Interop.md#4-勧めない経路-c-の-abidlsym--getprocaddress)）。

- 相手の殻が公開しているのは `plugin_module_main` と `plugin_module_ver` だけで、それ以外は
  マングリング済み——**相手のビルドに縛られ、版が上がると静かに壊れる**。`CallPluginLibrary` なら
  **壊れ方が「戻り値 `false`」に収まる**。
- 自分の殻と本体の間の C の ABI は別の話（同じ zip で配り、`VW_PAYLOAD_ABI_VERSION` で検出できる。
  [プラグインモジュール](Plug-in%20Modules.md)）。`dlopen` 自体はできる——**できても得が無い**。

## 打ち切った調査: `ISDK::DoProgramAction`

- **使わない。** `DoProgramAction(short actionSelector, void* actionEnv)`（`ISDK.h:1723`）の doc は
  1 行だけ、選択子は `doSelector0`〜`doSelector9` という**名前だけの列挙**【ソース根拠】。用例も
  無く、`void*` を受けるので当てずっぽうで呼ぶと落ちうる。**手掛かりが無いので調査の対象に
  しない**（[宣言](../Metrics/Plug-in%20Interop.md#打ち切った調査-isdkdoprogramaction)）。

## 打ち切った調査: `ISDK::ExternalNameToID` と XCALL

- **事前確認にも、呼び出しにも使えない。** **ID を受け取って外部を呼ぶ関数（XCALL）は ISDK にも
  VWFC にも無い**【ソース根拠】。**在る名前でも `-1` が返り**（実機・macOS）、在る / 無いを区別
  できない（[実測](../Metrics/Plug-in%20Interop.md#打ち切った調査-isdkexternalnametoid-と-xcall)）。

## 相手が居ないとき・読み込み順・呼べる時機

### 事前確認は要らない——戻り値で足りる

- **`CallPluginLibrary` は居ない相手に `false` を返す**ので、呼んでみて `false` なら相手が居ない
  （か失敗した）。`DoMenuName` も居ないコマンドで `-3`。
- 事前に確かめる道具はどれも使えない: **`HasPlugin`** は VectorScript にしか無く【ヘッダ根拠】、
  しかも「いまのワークスペースに在るか」を見るだけ【ソース根拠】。**`ExternalNameToID`** は在る
  名前でも `-1`（[詳細](../Metrics/Plug-in%20Interop.md#事前確認は要らない戻り値で足りる)）。

### 読み込み順は気にしなくてよい

- コンパイル済みプラグインは起動時に全部読み込まれ登録も終わるので、起動後に呼ぶなら順は
  関係ない【推定】。実測でも、**呼ぶ側（本体）が呼ばれる側（殻）より後から `dlopen` された**順で引けた。

### 呼べる時機: OS タイマーの刻みの中からも呼べる

- **呼べる（実機確認済み。macOS）。** `CFRunLoopTimer` の刻みの中から `CallPluginLibrary` が
  `true` と結果を返した（[実測](../Metrics/Plug-in%20Interop.md#呼べる時機-os-タイマーの刻みの中からも呼べる)）。
  **外部からの要求を OS タイマーで受けて、ほかのプラグインの機能を呼ぶ**形（CLI ブリッジ）が成り立つ。
- undo の記録は経路ではなく**呼ばれた側が何をするか**で決まる。刻みが VW の undo イベント中に
  当たったときは [周期実行と通知](Timers%20and%20Notifications.md)「VW が undo イベントを開いたまま
  回している最中」（**読むのは安全・書くと混ざる**）。

## まだ実機で確かめていないこと

**issue #217 が確かめると決めた項目は、すべて埋まった。** 残っているのは**最初から
範囲外**と決めていたものだけである（必要になった調査で測る）。

| 確かめること | いまの水準 | なぜ範囲外か |
| --- | --- | --- |
| `kVLIBScopeVSOnly` の相手が本当に弾かれるか | 【ソース根拠】 | 相手の登録を変えた版のプラグインが要る |
| `IFC_QueryInterface` が VW 2026 に在るのか | 戻り値 `false` のみ。「無い」とは言えない | issue が聞いていない。参考として測っただけ |
| ハンドル・2 次元配列の引数 | 【ヘッダ根拠】 | issue が聞いていない。使うときに測る |
| Windows での挙動 | 未測定（ビルドは通る） | [プラグインモジュール](Plug-in%20Modules.md)の Windows 未測定と同じ |

## 参考

- [プラグインモジュール](Plug-in%20Modules.md) — 読み込みの時機、殻と本体、C の ABI
- [Undo](Undo.md) — スクリプトエンジン経由の実測（取得の IID・落ちる組み合わせ）
- [周期実行と通知](Timers%20and%20Notifications.md) — OS タイマーから `gSDK` を呼ぶ
- [VectorScript → SDK の対応](VectorScript%20to%20SDK%20Mapping.md)
