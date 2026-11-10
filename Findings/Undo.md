# Undo（取り消し）

長い処理（インポート等）を 1 つの undo イベントにまとめるときの実測。特記の無い限り
VW 2026 / mac。実測の値・表・プローブ・潰した道は [計測記録](../Metrics/Undo.md)。

- **VW は処理の開始時に undo イベントを開かない**（`undo: start=no` / `afterParse=no` /
  `afterDraw=yes`）。描画の終わりに「イベント中」になるのは、**SDK 内部が自前でイベントを開く
  呼び出しがある**ため。**この半端な記録を取り消すと図面が壊れる**（ビューポートだけ消え、
  レイヤは戻らず、オブジェクトが断面を失った）。`GS_EndUndoEvent` の説明の「外部の終了時に
  自動で**閉じる**」は「自動で開く」ではない
  （[実測](../Metrics/Undo.md#vw-は処理の開始時に-undo-イベントを開かない)）。
  - **自前で開く呼び出しは断面ビューポートだけではない**——PIO の作成＋`ResetObject`
    （[#166](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/166)）、
    `CreateLocus`（mac。[#206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)）、
    `DeleteObject(h, true)`、平面ビューポートの作成・更新でも開いた。**#247 では `CreateLine` /
    `CreateLocus` では開かなかった**（Windows・mac。下記【追記】）。**図形を作る呼び出しは開く
    かもしれないと構え、呼んだ後に旗を読んで確かめる。**
  - **だから「どの undo イベントにも属さない図形」は作れない**——対照実験で足をすくわれる
    （#206 で実機を 2 回無駄にした）。**自分で開いた覚えが無くても `EndUndoEvent()` で閉じられる**
    （`true`、閉じた後は `no`）ので、対照は作った直後に閉じて別の段へ送り出す。
- **`kUndoPlaceholderNode`（型 90）は `ResetObject` 1 回ごとに PIO の子へ 1 つ積もるが、描画には
  影響しない**（11 個積んでも子・外接・立方は既定のまま。[Parametric Objects](Parametric%20Objects.md)
  の「構造材 PIO の子を消すのは…」）。イベントが開いていても積もり、**消せない**
  （`DeleteObject(h, useUndo=false)`・`ClearUndoTableDueToUnsupportedAction()`・イベントを閉じる・
  VW へ戻る、のどれでも減らない。[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）。
  **数が増えること自体を異常と読まない**
  （[実測](../Metrics/Undo.md#kundoplaceholdernode型-90は積もるが描画に影響しない)）。
- **`SetUndoMethod(kUndoSwapObjects)` ＋ `NameUndoEvent(...)` が開始も兼ねる**（ISDK に開始
  専用の呼び出しは無い）。RAII で包み、破棄で `EndUndoEvent()` するのが安全。
- **`IsCurrentlyBuildingAnUndoEvent()` を「いま触ってよいか」の判定に使うなら、
  本ファイルの実測をそのまま当てにしてはいけない**——`yes` は「利用者が操作中」ではなく
  「この文書にイベントが開いている」でしかなく、**SDK 内部の置き土産でも立つ**。周期実行の
  刻みの中での読み方と、`kNotify…` で囲めるもの／囲めないものは
  [周期実行と通知](Timers%20and%20Notifications.md)。
- **登録するのは「この処理が新しく作ったレイヤ」だけ**で足りる。レイヤを消せば上の図形も
  消えるので図形を 1 つずつ登録する必要が無く、**二重登録で「もう無いものを消しにいく」
  事故**も避けられる。
- **登録が 1 件も無いときはイベントごと捨てる**（`EndAndRemoveUndoEvent`）。空のイベントを
  残すと、SDK 内部が開いた記録が取り消しの対象になって図面が壊れる。
- **SDK へ渡して消費させた下ごしらえは取り消しで復活する。** 例えば
  `CreateCustomObjectPath` にポリラインと空グループを渡すと、SDK はそれらを**undo 記録つきで
  削除**して PIO へ取り込む。こちらがイベントを開いていると、その削除が記録に入り、
  **取り消したときにポリラインだけが図面へ復活する**（そのせいでレイヤも消えずに残る）。
  対処は SDK の作法どおり「自分が追加したものは申告する」——`AddAfterSwapObject` は
  "that object is deleted when Undo is selected" なので、申告しておけば復活したそれが改めて
  消える。**レイヤの上に普通に置いた図形へは使わない**（レイヤごと消えるものを二重登録
  しない）。
- **undo イベントの外で作ったものは、取り消しスタックに載らない**（イベントを開かずに
  `CreateLayer` したレイヤは、スクリプト経由の取り消しでも残る）。**戻したいものは自分で
  イベントを開いて登録しておく**——人がメニューから取り消しても、登録していないものは戻らない
  （[実測](../Metrics/Undo.md#undo-イベントの外で作ったもの取り消しの実行スクリプトエンジンが残すイベント)）。
- **取り消しを実行すると、開きかけのイベントごと終わる**（自分のイベントを開いたまま
  `DoMenuTextByName('Undo', 0)` を呼ぶと、戻ったときには `no`）。**RAII で `EndUndoEvent` を呼ぶ
  作りは「もう無いイベントを閉じにいく」ことになる。** 犯人は取り消しの実行で、無害な
  スクリプトや失敗するスクリプト（`VCOMError=1`）では `yes` のまま残った。
- **スクリプトエンジンは undo イベントを開き、開いたまま返す**（図形を作る VectorScript /
  Python で。**コマンドをまたいで残る**）。冒頭の「SDK 内部が自前でイベントを開く」の一例。
- **戻らないもの**: **処理前から在ったレイヤへ描いた分**は戻らない——利用者にその旨を伝える。
  - **【訂正】クラス・ストーリ・レベルテンプレートは「リソースなので残る」のではなかった。**
    **自分のイベントの中で作ったものは、登録していなくても取り消しで一緒に消える**
    （`AddClass` / `CreateStory` / `CreateLayerLevelType` / `CreateStoryLevelTemplate` の 4 つとも、
    件数まで戻った。[issue #223](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/223)、
    [Layers and Stories](Layers%20and%20Stories.md)「プラグイン自身の undo イベントの中で回すと…」）。
    **後始末は要らないが、取り消しの後で残っていると当てにしない。**
  - **登録しなかったレイヤは残る**（階に結んでいたレイヤは階を失って残る）。その上の構造材は
    消えた（[実測](../Metrics/Undo.md#戻らないものクラスストーリレベルテンプレートの測り直し)）。
- **【訂正】保存はいつも段を積むわけではない。積むのは次の 2 つの場面の 1 回だけ**
  （[issue #288](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/288)。
  [手順ごとの表](../Metrics/Undo.md#保存が積む取り消しの段288)）。**どの場合も履歴は消えず、
  もう 1 回押せば戻る。** 別名保存と上書き保存の差も、`DoMenuName("Undo", 0)` とメニューの
  「取り消し」の差も無い。以前の「別名保存は段を 1 つ積む」（#223）は、構造材を足した直後の
  最初の保存を測ったものだった。
  1. **図面に構造材を初めて足したあとの、最初の保存**（mac と Windows で同じ）。2 つ目の構造材・
     2 回目の保存では積まない。構造材のある保存済みの図面を開き直して編集・保存した場合は
     積まない。図面に構造材の定義（`DefineCustomObject` が作る様式）が新しく入ったかが効いて
     いる【推定】。
  2. **前の保存のあと、プラグインのイベントだけで図面が変わった図面の、最初のメニューの
     「保存」**（mac のみ。刻みからの `SaveActiveDocumentPath` では構造材が無ければ積まない）。
  - **利用者の操作に当てはめると**: 既に構造材のある図面を開いて編集し保存する、ふつうの場面では
    1 回で戻る。無題の図面に初めて入れて初めて保存した直後は、1 回目で何も戻らないことがある。
  - 空の段が何なのかは分からない。**メニューの項目の文字はいつも「取り消し」**（mac）で、
    1 回目で何が戻るかを知らせられない。
  - **MCP ブリッジの `vw_run_probe` は走らせた後に図面を別名保存する**ので、構造材を足した図面で
    「コマンドをまたいだ取り消し」を測ると 1 段多く見える
    （[plugin/README.md](../plugin/README.md)「ローカルの Claude Code から操作する」）。
  - **Windows の 2 の場面は測れていない**（VM が描画で落ち、`DoMenuName` は `Save` を **-3** で
    受け付けない。[#291](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/291)
    で打ち切り。[Documents](Documents.md)「Windows——ほかの Vectorworks が開くファイルと、測れなかった残り」）。
    **mac から類推しないこと。**

## 打ち切った調査: プラグインから「もう閉じたイベント」へ Undo を掛ける

**結論: できない（ヘッダの記述から確定。[issue #23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23)）。**

- Undo を**実行する** API は `ISDK::SupportUndoAndRemove` / `ISDK::UndoAndRemove` の組だけで、
  対象は**まだ `EndUndoEvent` で閉じていない、組み立て中のイベントだけ**（`SupportUndoAndRemove()`
  をその回の `SetUndoMethod()` より前に呼ぶ必要もある）【ヘッダ根拠】
  （[説明文](../Metrics/Undo.md#打ち切った調査-プラグインからもう閉じたイベントへ-undo-を掛ける)）。
- 「n 個前のイベントを実行する」汎用の呼び出しは `ISDK` に無い（`Info/Undo Best Practices.md`
  の 9 個はどれも開始・登録・終了・警告ダイアログ・このペア）。
- **前回の取り込み結果を戻す役はプラグイン自身では担えない**（この API では）。ただし
  `DoMenuName` / スクリプトエンジン経由で取り消しを起こす道が後で見つかった（下記）。

## 打ち切った調査: undo イベントを閉じずにコマンドを終えて、次のコマンドから `UndoAndRemove` する

**結論: できない（ヘッダの記述から確定。[issue #31](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/31)）。**

- `GS_EndUndoEvent` の説明文の "**VectorWorks will automatically end the event when an external
  is completed.**" により、呼ばずに返しても**イベントは次のコマンドまで開いたまま残らない**
  【ヘッダ根拠】。次のコマンドから見れば「もう閉じたイベント」と同じで、`UndoAndRemove` は
  届かない。問い 2〜6 はこの 1 文からの帰結で、**実機では個別に確かめていない**
  （[ヘッダの説明文](../Metrics/Undo.md#打ち切った調査-undo-イベントを閉じずにコマンドを終えて次のコマンドから-undoandremove-する)）。
  問いごとの答え（★ = ヘッダの記述を素直に読んだ帰結で、実機で個別に確かめていない）:
  - 問い 1: `EndUndoEvent` を呼ばずに返っても、VW が完了時点で自動的にイベントを終了する。
    「開いたまま残る」でも「捨てられる」でもなく、**正常にクローズされる**【ヘッダ根拠】。
  - 問い 2（★）: 直後のコマンド B の `IsCurrentlyBuildingAnUndoEvent()` は、A のイベントに
    ついては `false` と考えられる（A の完了時点で既に構築中ではないため）。
    `IsCurrentlyBuildingAnUndoEvent()` 自体には説明文が無く、ヘッダ根拠を持たない帰結である。
  - 問い 3（★）: A の `SetUndoMethod()` より前に `SupportUndoAndRemove()` を呼んであっても、
    B から `UndoAndRemove()` を呼んだ時点では対象が既に無い——「もう閉じたイベント」と同じ。
    **戻せない。**
  - 問い 4〜6（★）: 「イベントが開いたまま人へ操作を返る」前提が成り立たないので、人が
    取り消しを選ぶ・別の編集をする・保存/終了する、のいずれの時点でも「半端な記録」は
    存在しないはず。危険シナリオが実機で再現されるかは未検証のまま。
- この方向でも「往復の周ごとに前回の描画を undo で丸ごと戻す」筋は成立しない。プラグイン側は
  「レイヤのハンドルを直接 `DeleteObject` する」（下記）を代替案とし、テンプレート由来の
  レイヤに描いた分は戻らない（レイヤの直接削除は部分復元にしかならない）と利用者に伝える。
- **【注意】後の実測では、コマンドが戻った時点では閉じなかった**（下記「レイヤのハンドルを直接
  `DeleteObject` する」の【追記】。閉じるのは利用者の次の取り消せる操作）。この節の帰結は
  ヘッダの記述からのもので、その実測を踏まえて見直してはいない。

## 打ち切った調査: プラグインから `DoMenuTextByName` 相当（メニューコマンドを名前で起動）を呼ぶ

> **【訂正】この節の結論は誤りだった。** `ISDK::DoMenuName(const TXString& name,
> short chunkIndex)`（`ISDK.h:1722`、旧 `GS_DoMenuName`）が `DoMenuTextByName` の SDK 版として
> **実在し、実機でコマンドが実行される**
> （[issue #217](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/217)）。
> 内部名を指し、居ないコマンドなら `-3`、実在すれば `0`。**`DoMenuName("Undo", 0)` で、
> スクリプトエンジンを介さずに取り消しを起こせ、開いていた undo イベントも終わった。**
> 詳しくは[プラグイン間の呼び出し](Plug-in%20Interop.md)の 2。

- 当時（[issue #27](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/27)）
  「無い」と書いたのは、**`DoMenuName` という綴りを語の総当たりに入れ忘れた**ため
  （[取りこぼしの記録](../Metrics/Undo.md#打ち切った調査-プラグインから-domenutextbyname-相当メニューコマンドを名前で起動を呼ぶ)）。
- そのまま有効な結論【ヘッダ根拠】: `IWorkspaceMenuItem` は実行の口を持たない（ワークスペースの
  編集用）、`GetMyMenuCommandIndex` は逆方向（自分がどこから呼ばれたか）、`Kludge` にもメニュー
  実行・取り消しの ID は無い（`kKludgeGetMenuItemUserFriendlyName` は表示名を読むだけ）。
- **教訓: 「無い」と書く前に、綴りの揺れ（`Text` の有無など）まで総当たりする。**

## 間接経路: スクリプトエンジン経由で `DoMenuTextByName` 相当を呼ぶ

**結論: 呼び出す口はあり、VectorScript 経由なら取り消しも実際に効く。効くのは
スクリプト自身が直前にした操作と、呼び出し元（C++）が自分の undo イベントに登録して
`EndUndoEvent()` で閉じたものまでで、undo イベントの外で作った（登録していない）
呼び出し元の操作には届かない**（実機。
[issue #39](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/39)・
[issue #54](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/54)）。
**登録して閉じたイベントなら、別のコマンド実行からの呼び出しでも戻る。**
**Python から `vs.*` を使うなら `ExecuteScript` を呼んではいけない——落ちる。
`ScriptContext_Begin` → `ScriptContext_Run` を使う。** 要点は次の 6 つ。

- **VectorScript 経由の `DoMenuTextByName('Undo', 0)` は到達し、取り消しが掛かる。**
  連続で呼べば**複数段戻る**（実測で 3 段。上限は見ていない）。
- **取り消せるのは取り消しスタックに載っているものだけ**——登録していない呼び出し元の操作は
  戻らない。**`AddAfterSwapObject` で登録して `EndUndoEvent()` で閉じたものは、別のコマンド
  実行からでも戻る**（次節）。
- **`VCOMError=0` は「実行時エラーが起きなかった」を意味しない。**
- **Python 経由（`ScriptContext`）でも挙動は同じ**——足跡は `pre=有 / m1=無 / m2=有`。
- **【危険】`IPythonScriptEngine::ExecuteScript` から `vs.*` を呼ぶと VectorWorks ごと
  落ちる**（ロガーの有無に関わらず。3 通り試して 3 通りとも）。**同じことを
  `ScriptContext_Begin` → `ScriptContext_Run` でやると通る。**
- **Python のロガーは実行時エラーの traceback を拾える**（`VCOMError` は 0 のまま）。
  スクリプトの失敗を**呼び出し側で**知る手段は、**この経路にしか無い**。
- **取り消しの実行は、呼び出し側が開いている undo イベントを終わらせる。**
  終わらせるのは取り消しであって、`ExecuteScript` でも失敗の後始末でもない。
- **【罠】`CompileScript` は `showDialogs=false` でも成功のダイアログを出す**（毎回）。
  構文を確かめたいだけでも人を止めるので、**呼ばずに `ExecuteScript` の戻り値で足りる**。

スクリプトを実行させる口は `IVectorScriptEngine::ExecuteScript(const TXString&)` と
`IPythonScriptEngine::ExecuteScript(const TXString&, IPythonLogger* = NULL)`（どちらも
`Interfaces/VectorWorks/Scripting/`。`script` はソーステキストそのもの）と、Python だけにある
`ScriptContext_Begin` / `ScriptContext_Run`（`RunEx(logger)`）【ヘッダ根拠】。保存済みの
スクリプトを直接指す引数は無く、リソースを読んでテキストにして渡す
（[宣言と実装例](../Metrics/Undo.md#間接経路-スクリプトエンジン経由で-domenutextbyname-相当を呼ぶ)）。

```cpp
// Python から vs.* を使う道（ExecuteScript は vs を触らないスクリプト専用）
pyEngine->ScriptContext_Begin(script /*, IPythonLogger* = nullptr */);
pyEngine->ScriptContext_Run();          // ロガーを使うなら ScriptContext_RunEx(logger)
```

### 実機で走らせた結果と、その読み方（VW 2026 / macOS）

**この節の結論は二度書き換えている**（「スクリプトエラーが出ていた」という指摘と、足跡で
誤りが割れた経緯）。プローブを 7 度作り直し、実機で 10 回走らせ、うち 4 回は VectorWorks
ごと落ちた。足跡・実測値・落ちたときのスタック・所要時間の表は
[計測記録](../Metrics/Undo.md#実機で走らせた結果とその読み方vw-2026--macos)。

#### 確定していること（無印＝実機確認済み）

- **VectorScript 経由の取り消しは効く**（`pre=有 / m1=無 / m2=有`——スクリプトは最後まで走り、
  直前の 1 段が消え、C++ が作ったものは消えない）。**連続で 3 回呼べば 3 段戻る**
  （上限は見ていない。深さは program variable `varMaxUndos`（セレクタの番号が 59。段数では
  ない）で決まるらしい【推定】——必要になったら `GetProgramVariable(varMaxUndos)` で読める
  見込み。深さは利用者の環境設定で SDK の性質ではないので測っていない）。
- **`ExecuteScript` は同期的**（戻った直後にスクリプトの作ったものがある）。
- **両エンジンともコマンド実行中に取得できる。`CompileScript` は両エンジンとも通る。**
- **構文エラーは戻り値で分かる**（`CompileScript` は `ok=no`、`ExecuteScript` は `VCOMError=1`）
  が、`outErrorText` は空・行番号は -1 で、**内容も場所も取れない**。**実行時エラーは
  `VCOMError=0` のまま素通りする。**
- **VectorScript の失敗はモーダルの「スクリプトエラー」ダイアログで人を止める**（抑える引数が
  無い）——**無人で回す処理から呼ばない。**
- **Python のロガーは安全で、`stdout`（`fOutput`）・実行時エラーの traceback（`fErrors`）が取れる。**
  `ScriptContext` 経由では出力を受けるのは `RunEx` のほう。
- **`ExecuteScript` から `vs.*` で必ず落ちる**（`EXC_BAD_ACCESS`。`vs` の C 関数の直後）。
  **`ScriptContext_Begin` → `ScriptContext_Run` なら通る**（SDK 同梱の
  `ImageComparisonTesting.cpp` もこちら）。Python の Undo も VectorScript とまったく同じ。
- **開いている undo イベントを終わらせるのは「取り消しの実行」だけ。** イベントの中で作った
  レイヤは残る（イベントだけが終わらされ、取り消しスタックへ積まれる）。図面は壊れなかった。
- **`CompileScript` の `inShouldDisplayDialogs=false` は成功のダイアログを抑えない**（毎回
  秒単位で止まる）。抑えるのはエラーのダイアログだけらしい【推定】。**`CompileScript` を
  呼ばない**——成否は `ExecuteScript` の戻り値で足りる。

#### 教訓（プローブの作り方）

- **成否を戻り値だけで判定しない。** スクリプトの中の出来事は戻り値に出ない。
- **スクリプトには足跡を残させる。** 呼び出しの前後でレイヤを作らせれば、どこまで到達したか・
  何が取り消されたかが目視なしで分かる。
- **足跡は「誰が作ったか」で分ける。** C++ が作ったマーカーでスクリプトの取り消しを測ろうと
  したのが 1 度目の誤りの正体。
- **拾える口があるなら必ず拾う**（Python のロガーを `nullptr` で捨てたのがやり直しを生んだ）。
- **落ちる見込みのある呼び出しは後ろへ回し、直前に印を出す。** 1 回の走行で試せる
  「落ちるかもしれないもの」は 1 つだけ。
- **落ちた節を次の走行で飛ばす。** 節の出入りをファイルへ書けば、頼むのは「もう一度
  走らせてください」だけで切り分けが進む。
- **人に「どの行で出ましたか」と訊かない。** ダイアログの出所は所要時間で測れる（失敗した
  `ExecuteScript` は 18108ms、他は 0〜17ms）。**目視を頼む前に、機械で測れないかを考える。**
- **時間で測るなら「初回」を疑う**——同じ呼び出しを 3 回続けて所要を並べる。
- **同じ形にして 1 か所だけ変える**（スクリプトだけを 3 通りに差し替えて犯人が割れた）。

**#23（閉じたイベントには `UndoAndRemove` が効かない）・#31（閉じずに返して次の実行から戻す）
は塞がったまま。** #27（メニューを名前で起動する API が無い）は #217 で【訂正】——`DoMenuName`
がある。取り消しを起こせるのはこの間接経路と `DoMenuName` で、**「登録して閉じておけば、
別のコマンド実行からでも戻せる」**形で当初の用途に届く（次節）。

## 間接経路（続き）: 登録して閉じたイベントの cross-command 復元

**結論: 自分の undo イベントに `AddAfterSwapObject` で登録して `EndUndoEvent()` で
閉じたレイヤは、別のコマンド実行（＝別の `DoInterface` 呼び出し）からの
`DoMenuTextByName('Undo', 0)` で戻る**（実機。
[issue #54](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/54)）。
前節の「呼び出し元がしたことは届かない」は**未登録の場合の観測**である。

- 実プラグインでは 1 段の取り消しで前回の 1319 件・レイヤ 23 枚・シート 14 枚がすべて消え、
  取り込み前から在ったレイヤへ描いた分まで戻った。最小構成（1 レイヤ・1 登録・新規の空図面）
  でも `before=残っている / after=消えた`
  （[経緯と、同名レイヤで判定が汚れた回](../Metrics/Undo.md#間接経路続き-登録して閉じたイベントの-cross-command-復元)）。
- **教訓: 判定に同名のものを使い回さない**（最初の 1 回は「戻らなかった」に見えたが、同名の
  マーカーレイヤが複数残る図面で走らせており、`LayerExistsByName` が同名の別レイヤを拾って
  誤判定した可能性が高い——取り消しは登録済みの別のレイヤを正しく消したと考えられる。
  その回は根拠にしていない）。
- **実務上の作法**（issue #54 の提案どおり）:
  - **呼ぶ前に、戻したいものが自分のイベントに登録されているかを確かめる。**
    登録していないものは、何段掛けても残る（前節）。
  - **押してよい場面を絞る。** 取り消しは「誰の操作か」を選べない——押した時点で
    取り消しスタックの一番上にあるものが戻る。ホームズ君側は (1) いま開いているのが
    自分が作った作業ファイルであること、(2) 前回作ったレイヤが実際に残っていること、
    の 2 つを満たすときだけ実行している。利用者の図面で押せば、消えるのは利用者自身の
    直前の編集になる。
  - **効いたかは読み戻しで判定する。** `ExecuteScript` の `VCOMError=0` は「実行時
    エラーが起きなかった」しか意味しない（前節）。目視でしか分からないこと
    （保存確認ダイアログが出たか等）は、機械で確かめ直せないかを先に考える——
    このプローブでは「文書が閉じたか」ではなく「レイヤ名が読み戻せるか」で足りた。

## レイヤのハンドルを直接 `DeleteObject` する

undo に頼らず、前回作ったデザインレイヤのハンドルを直接消す筋
（[issue #23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23)。
[実測](../Metrics/Undo.md#レイヤのハンドルを直接-deleteobject-する)）。

- **`DeleteLayer` 相当の専用 API は無い**【ヘッダ根拠】。汎用の
  `ISDK::DeleteObject(MCObjectHandle h, Boolean useUndo = true)` をレイヤのハンドルへ呼ぶ
  （[Layers and Stories](Layers%20and%20Stories.md)）。
- **レイヤごと（上の図形ごと）図面から消える。** VW は落ちない。
- **アクティブレイヤ自身を消してもクラッシュしない**（VW が別のレイヤへ切り替える）。
  **全レイヤを消して 1 枚も残らない状態は試していない**（未確認。最後の 1 枚は消せないか、
  消しても自動生成されると見ているが【推定】、裏は取れていない）。
- **落とし穴: `DeleteObject(handle, true)` は、undo イベントが開いていなければ自分で開く。**
  レイヤ削除にも、**自分で `SetUndoMethod` ＋ `NameUndoEvent` で開き、終わったら
  `EndUndoEvent`（登録が無ければ `EndAndRemoveUndoEvent`）で閉じる**——冒頭の RAII の作法を適用する。
  - **【追記】そのまま返すと、コマンドが戻っても閉じない**
    （[issue #213](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/213)。
    `kNotifyUndoEndEvent` は 53 秒後で、その間に作った図形は置き土産のイベントに入った）。
    **戻る直前に `EndUndoEvent` で閉じれば `yes` は 0 件。** `GS_EndUndoEvent` の「外部の終了時に
    自動で閉じる」は、**少なくとも「コマンドが戻った時点では閉じない」**。周期実行との組み合わせは
    [周期実行と通知 7 節](Timers%20and%20Notifications.md)。
  - **【追記】閉じるのは「利用者の次の、取り消せる操作」——時間でも、クリックでも、図面の切り替え
    でもない**（Windows・mac とも、メニューコマンドの中で開いても刻みの中で開いても同じ。
    [issue #247](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/247)。
    [操作ごとの表](../Metrics/Undo.md#レイヤのハンドルを直接-deleteobject-する)）。矩形を描き終えた
    時点で `no` になり、`kNotifyUndoEndEvent` が 2 回来る（置き土産のイベントと矩形のイベントが
    それぞれ閉じたものと見ている【推定】）。**何もしなければいつまでも閉じない**
    （#245 の 7 分）。#213 の 53 秒はその間に人が何か描いたと見るのが自然【推定】。
    - **新規の図面を開いても閉じない**——`IsCurrentlyBuildingAnUndoEvent()` は**前面の図面の**旗で、
      元の図面へ戻れば `yes` に戻る。#245 で ⌘N の直後にブリッジが戻ったのは、閉じたのではなく
      前面が替わったから。
    - 矩形は置き土産と同じイベントに入ったと読める（mac で 1 回の取り消しで両方が戻った）【推定】。
    - **ブリッジは走らせた後に自分で閉じる**（[#266](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/266)。
      `plugin/src/McpBridge.cpp` の `RunTargets`）。プローブの作法は変わらない——**自動で開いた
      イベントは、戻る前に `EndUndoEvent()` で閉じる**（[probes/runtime/README.md](../probes/runtime/README.md)）。
  - **【追記】`CreateLine` / `CreateLocus` だけでは開かなかった——Windows も mac も**（続けて
    `DeleteObject(h, useUndo=true)` を呼んだ時点で `yes`）。#206 も mac の実測なので、
    プラットフォームの違いではない。#206 との違いは呼んだ場面にあると
    見ている【推定】が、切り分けていない。**「作る呼び出しは開くかもしれない」と構え、旗を読んで
    確かめる**のは変わらない。
- **ビューポートの載ったシートレイヤも `DeleteObject(sheetLayer, true)` で丸ごと消せる**
  （[issue #29](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/29)）。
  ビューポートも `AddViewportAnnotationObject` の注釈も取り残されない。
  - `ISDK::CreateViewport(parentHandle)` の `parentHandle` は「どの容れ物（レイヤ／レイヤ内の
    グループ）に置くか」で、表示するデザインレイヤは `ISDK::SetViewportLayerVisibility(viewport,
    designLayer, kLayerVisibilityNormal)` で決まる【ヘッダ根拠】。
- **参照されていたデザインレイヤ側に副作用は無い**（シートを消してもデザインレイヤと図形は残る）。
- **削除順序に決まりは無い。** ビューポートが参照しているデザインレイヤを先に消しても落ちず、
  `UpdateViewport` も落ちず、その後シートも消せる（間はビューポートが半端な状態で残る）。
- **ビューポートの作成・更新（`CreateViewport` / `SetViewportLayerVisibility` / `UpdateViewport` /
  `AddViewportAnnotationObject`）だけで undo イベントが開く**（平面ビューポートでも）。
  ビューポートを扱う処理を自前のイベントで包むなら、包む**前**に
  `IsCurrentlyBuildingAnUndoEvent()` を確かめる。
