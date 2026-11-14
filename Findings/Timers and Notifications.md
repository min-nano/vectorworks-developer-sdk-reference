# 周期実行と通知（タイマー・アイドル・`kNotify`）

「**暇なときに・周期的にプラグインのコードを呼んでもらう**口はあるか」の調査
（[issue #204](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/204)）。
出どころは MCP ブリッジ——ウェブパレットの `setInterval`（250 ms）から C++ を呼んでいたが、
**パレットを隠すと 60 秒に 1 回まで間引かれる**（下記 5）。VW 2026 の mac / Windows。
測った環境・ビルド・手順・実測の表とログは [計測記録](../Metrics/Timers%20and%20Notifications.md)。

## 結論（先に）

| 問い | 答え |
| --- | --- |
| ISDK に「暇なとき／周期的に」呼ばれる口はあるか | **無い。** ISDK 全体で「登録」できるのは**メニューのコールバック**・**通知手続き**・**ダイアログのタイマー**の 3 つだけで、常駐の時計になるものは 1 つも無い【ソース根拠】（下記 1） |
| 代わりに使えるもの | ① **ツール拡張の `kAction_OnIdle`**——ただし**自分のツールが選ばれている間だけ**（下記 3）／② **`RegisterNotificationProcedure` ＋ `kNotify*`**——**出来事に反応するだけで周期ではない**（下記 2）／③ **OS のタイマー**（下記 4） |
| OS のタイマー（mac: `CFRunLoopTimer` / `dispatch` / Windows: `SetTimer`）から `gSDK` を呼んでよいか | **できる（実測・mac / Windows とも）。** コマンドが戻った後も刻み、その刻みから `gSDK` を**読めるし図面に書ける**（読み 881＋423 回で例外 0・`CreateLocus` も通った）。**裏に回っても間引かれず、図面が 1 枚も開いていなくても刻む。** ただし**間隔は保証されない**（平均 265〜273 ms・**最大 3〜5 秒空く**）。**こちらの同期処理には割り込まない**（下記 4） |
| 刻みを**どのモードへ登録すべきか**（mac） | **`kCFRunLoopCommonModes`（か全モード）。既定モードだけで組んではいけない。** 同じ瞬間に 2 本並べて 7 回測ると、既定モードだけの 1 本は**十数秒〜30 秒止まり、レンダリング中は 5 分 52 秒まるごと止まった**（共通モードの 1 本は同じ回で最大 9.7 秒）（下記 4 の 7） |
| **利用者がドラッグしている／レンダリングしている最中**に刻みが当たったら | **届く。`gSDK` も読める**（clean な 4 回・3976 刻みで異常 0 件）。**そのとき VW は undo イベントを開いていない**（`building=yes` が 0 件）——VW がイベントを開閉するのは**ランループが回っていない区間**だから。**＝「書くと混ざる」（下記 6）が当てはまるのは、VW のモーダルダイアログの最中だけ**（下記 7） |
| **プラグインから「描き直して、終わるまで待つ」ことはできるか** | **できない。** `SetRenderMode(…, immediate=true, doProgress=true)` は **3D のビューでも 0 ms で戻り、戻った後にイベントループを回しても何も起きない**（＝描いていない。32 回とも）。描かせられるのは**ビューポートの `UpdateViewport`** だけだが、それも**戻った時点では描き終わっていない**（戻った後にランループが 545 ms 吸われる）。**終わりを知る口も通知も無く、`IsDirty()` も戻り値も完了を表さない**（下記 7 の「道具について」） |
| CEF に隠れたページのタイマーを間引かせない口は SDK にあるか | **無い。** SDK 全体で CEF に触れる口は `WebDlgEnableConsole`（コンソールログの出力）ただ 1 つ【ソース根拠】（下記 5） |
| 刻みから **`CloseAllFilesAndQuitVectorworks(true, false)`** を呼ぶと | **呼べる・落ちない（mac / Windows とも実測）。呼び出しは戻り、プロセスはその後に正常終了（`atexit` まで届く。mac 4〜8 秒後・Windows 28〜30 秒後）。** 確認をキャンセルすれば何も変わらず戻る。**確認の最中、呼んだタイマー自身は mac では刻まないが Windows では入り直す**——そこから 2 度目を呼ぶと確認が 2 つ重なるので、「呼び出しの最中」を旗で持って見送る。**`kNotifyBeforeApplicationQuit` はキャンセルされても届く**。終了の回は、刻みが止まった後・`atexit` の直前に `plugin_module_main` へ `kPluginModuleDeinit` が届く（下記 4 の 9） |
| **起動の途中**（ホーム画面の前）の刻みから終了を呼ぶと（mac） | **終わらない。** 起動画面の前半には刻みが来ず、届くのは Cineware の起動待ちと「開発元不明のプラグイン」のダイアログの間だけ。どちらも呼び出しは戻り `kNotifyBeforeApplicationQuit` も届くが、**Cineware の待ちでは起動画面のまま止まり SIGKILL でしか止まらない**。ダイアログの間なら頼みは捨てられ、「続ける」でそのまま起動する（下記 4 の 9。Windows は未測定——[#316](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/316)） |
| **起動を終えた（ホーム画面が出た）こと**を刻みの側から知るには（mac） | **最初の `kNotifyBeforePendingUpdate`（`'BEFO'`）を目印にする。** VW のアイドルから来る通知で、起動画面・Cineware の起動待ち・「開発元不明のプラグイン」のダイアログの間には届かず、ホーム画面の後に初めて届く。その後の刻みから終了を呼ぶとふつうに終わる。`kNotifyNewWrkSpaceLoaded` / `kNotifyApplicationActivate` は起動では届かず、ワークスペース名はダイアログの間にもう読めるダイアログで「続ける」を押した後も同じ（下記 4 の 9「起動を終えた目印」。Windows は未測定） |
| 終了の呼び出しが**戻った後**の刻みから、もう一度呼ぶと（mac） | **何も起きない。** 0〜1 ms で戻り、終わり方も変わらない（8 回続けても同じ）。2 度目以降に `kNotifyBeforeApplicationQuit` は届かない（下記 4 の 9。Windows は未測定——[#316](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/316)） |
| **利用者が閉じる**（メニューの「終了」）とき、刻みはいつまで来るか（Windows） | **`kNotifyBeforeApplicationQuit` の後には 1 度も来ない**（11 回）。閉じる指示から BApQ までの数 ms〜63 ms に 0〜2 回来るだけ。その後 -2（約 7〜9 秒後）・`atexit`（約 27〜29 秒後）まで刻みは無い。**＝閉じていく途中に刻みから `gSDK` を呼ぶ場面は無い**（下記 4 の 9「Windows」） |
| **図面が 1 枚も開いていない**とき、刻みから `GetCurrentLayer()` を呼ぶと | **`nullptr` が返り、落ちない**（閉じた後も起動直後も）。`GetOpenFilesList()` の件数と**刻みごとに常に一致**（mac 1 万 2 千回余り・Windows 3 万 5 千回余りで食い違い 0）。ただし**閉じた文書のレイヤと別の文書のレイヤが同じアドレスで返る**ので、ハンドルは刻みごとに引き直す（mac / Windows とも。下記 4 の 8） |
| 図面が開いていないときにウェブパレットを出せるか | **出す口は無い**（`SetWebPaletteVisibility` を呼んでも出ないのは実測）。**「文書が無い間は動かない」前提で設計する**（下記 6） |

## 1. ISDK に「暇なとき・周期的に」の口は無い

- **`ISDK.h` で登録・タイマー・アイドルに関わる宣言は 8 本だけ**で、周期実行に使えるのは
  **ダイアログのタイマー（`RegisterDialogForTimerEvents`）1 種類だけ**
  （[一覧](../Metrics/Timers%20and%20Notifications.md#1-isdk-に暇なとき周期的にの口を探したヘッダソースを読んだ)）。
- **`RegisterDialogForTimerEvents(dialogID, ms)` は「ダイアログが開いている間」の時計。**
  VWFC は `VWDialog::RegisterForTimerEvents` → `OnDialogTimer()`。**`RunDialogLayout` は
  モーダル**（[Layout Dialogs](Layout%20Dialogs.md)）なので常駐の受け付けには使えない——使うと利用者は
  図面を触れない。
- **ウェブパレットには渡せる `dialogID` が無い**（中身は HTML で `VWDialog` ではない）【ヘッダ根拠】。
- `TickCount()` は時刻を読む口（1/60 秒単位）。`RegisterMenuForCallback` は通知の受け取り、
  `RegisterExternal` は外部モジュールの登録で、どちらも周期ではない。
- **「SDK に常時開けておく口（アイドルコールバック）は無い」という前提は正しい**
  （[Layers and Stories](Layers%20and%20Stories.md) の「SDK に遅延実行の口は無い」も同じ話）。

## 2. 通知（`RegisterNotificationProcedure` ＋ `kNotify*`）は「出来事」用で、周期ではない

```cpp
gSDK->RegisterNotificationProcedure(proc, kNotifyDocOpen);   // ISDK.h:1751
gSDK->UnregisterNotificationProcedure(proc, kNotifyDocOpen); // ISDK.h:1759
```

- **`whichChange` は `Include/Kernel/Core/MCNotification.h` の `kNotify*`**（VW 2026 SDK に
  **184 個**）。**公式リファレンス（`Info/` / `Versions/`）には 1 行も載っていない**
  （[型と例](../Metrics/Timers%20and%20Notifications.md#2-通知registernotificationprocedure--knotifyの宣言を読んだ)）。
- **出来事が起きなければ 1 度も呼ばれない**ので、外からの要求を拾う時計にはならない（利用者が
  何も触らずにいれば永久に呼ばれない）。
- **`kNotifyApplicationActivate` は「前面に戻った瞬間に拾い直す」のに使える**——間引かれる
  JS タイマーの取りこぼしだけは埋められる【ヘッダ根拠】。
- VW 本体にはアイドルのループがある（`kNotifyBeforePendingUpdate` のコメント「From OnIdle」）が、
  **そこへ差し込む口は公開されていない**。

## 3. ツール拡張だけは「アイドル」を受け取れる（が、選ばれている間だけ）

- **ツール拡張（`GROUPID_ExtensionTool`）には `kAction_OnIdle`（112）が来る**。メニュー・PIO・
  ウェブパレットの拡張には無い。
- **届くのは自分のツールがアクティブな間だけ**——利用者が別のツールを選んだ瞬間に止まるので
  常駐の受け付けには使えず、「ツールを選んで
  いる間だけ動く監視」（カーソル追従など）向き【ヘッダ根拠】。

## 4. OS のタイマーから `gSDK` を呼ぶ——**できる**（mac / Windows とも実測）

**ウェブパレットの外に時計を置ける。** OS のタイマー（mac: `CFRunLoopTimer` をメインの
ランループへ / Windows: `SetTimer(nullptr, 0, ms, proc)`）は、**メニューコマンドが戻った後も
刻み続け、その刻みから `gSDK` を読むことも図面に書くこともできた**。JS タイマーのような
間引きも起きない。**ただし間隔は保証されない**。

### 局面ごとの実測

局面ごとの刻みの回数と間隔は [計測記録の表](../Metrics/Timers%20and%20Notifications.md#局面ごとの実測)。

- **刻みが届くのは VW がイベントを回す瞬間だけ**: コマンドが戻った後・モーダル（alert）の最中は
  両プラットフォームで届く。**ランループを回さず働いている間は 0 回**（両方）。
- **進捗ダイアログの `DoYield` は mac だけ刻み（間隔は荒い）、Windows は 0 回。**
- **自分でイベントを回す**: Windows は `PeekMessage` で届く。mac の `CFRunLoopRunInMode` は
  1 回の長い呼び出しでは即座に戻る（下記 4 の 4）。

### 1. コマンドが戻った後も刻み、`gSDK` を読める・**書ける**

- **読み**は mac 881 回・Windows 423 回すべて成功・例外 0。
- **書き**: **こちらのコードがスタックに 1 本も無い刻みから `CreateLocus` が通った**（mac・
  Windows とも。[実測](../Metrics/Timers%20and%20Notifications.md#1-コマンドが戻った後の刻みとgsdk-の読み書き)）。
- mac は 2 回まわして再現した。間隔の最大は回によって違い、**上限は決まっていない**。
- **図面が 1 枚も開いていなくても刻みは続き**、`gSDK` は落ちずに「文書が無い」を返す
  （ウェブパレットと違い、文書の有無に関わらず動く。下記 6）。

### 2. ただし**間隔は保証されない**（「250 ms ごと」と当てにしてはいけない）

- コマンドが戻った後の `dt` は**平均 252〜273 ms・最大 1.5〜4.9 秒・最小 16〜69 ms**
  （空いたぶんが後でまとめて来る）。配り手は OS ではなく **Vectorworks のイベント処理**なので、
  VW が同期処理で詰まっている間は刻みが止まり、明いてから続く（下記 3 がその裏返し）。
  最大値は「その回で観測した最悪値」として読む。
- **タイムアウトや一定周期のポーリングは、刻みの回数ではなく時刻で測ること。**

### 3. こちらの同期処理には割り込まれない（両プラットフォーム）

- **ランループを回さず働いている間の刻みは 0 回**——**自分が undo イベントを開いて描いている
  最中に刻みが割り込むことはない。**
- VW が自分でイベントを回す場面（モーダルダイアログ・mac の `DoYield`）には挟まる。
  **`DoYield` で受け付けを回す設計は Windows では成立しない。**
- ふつうに 2 分触って、刻みが VW の undo イベント中に当たったのは **0 回**（2 プラットフォーム・
  計 1304 刻み）。**「当たらなかった」だけで「当たらない」ではない**——狙って当てた結果は下記 6
  （[issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)）。

### 4. プラットフォームごとの作法

- **macOS: メニューコマンドはメインスレッドで走る。**
- **macOS: `CFRunLoopRunInMode` は「短く刻んで何度も呼べば」回る**【訂正】。1 回で 0.6〜1.5 秒を
  取ると即座に戻り刻みは 0 回だが、**0.05 秒 × 12 回なら 8 回届き `kCFRunLoopRunTimedOut` で戻る**
  （[実測](../Metrics/Timers%20and%20Notifications.md#4-プラットフォームごとの実測スレッドランループの回し方)）——短い刻みで呼べば、
  ちゃんと時間を使って刻みを配ってから `TimedOut` で戻る。待ちたいなら 0.05 秒ずつ回す。当初の「自分で回すことはできない」は誤り。
- **macOS: 登録は `kCFRunLoopCommonModes` で足りる**（既定モードが共通モードに入っている）。
  全モードへ入れるとペーストボード・view-bridge のモードの分も拾える。主ランループのモードは 13 個。
- **macOS: メインキューの `dispatch_source` も `CFRunLoopTimer` とほぼ同じ振る舞い**なので、
  どちらでもよい。
- **Windows: `SetTimer(nullptr, 0, ms, proc)`（ウィンドウ無し）で足りる。** `WM_TIMER` は VW の
  メッセージポンプが配り、自分で `PeekMessage` を回してもその場で配られる。
- **【罠】Windows では、起動時に読み込まれる殻でも、タイマーを作る前に
  `GET_MODULE_HANDLE_EX_FLAG_PIN` で自分を留める。** VW は起動時にモジュールを一度降ろして
  読み込み直すので、留めないと**もう無い番地を呼び続けて起動中に落ちる**
  （[Plug-in Modules](Plug-in%20Modules.md)「【罠】Windows の VW は起動時にモジュールを一度降ろして
  読み込み直す」。issue #253）。
- **入れ替えできるモジュール（本体）へ置くなら、降ろさせない手当てが要る**——
  `dlopen(RTLD_NOLOAD)`（mac）／`GET_MODULE_HANDLE_EX_FLAG_PIN`（Windows）で参照を増やす
  （[Plug-in Modules](Plug-in%20Modules.md)）。**実プラグインでは殻に置けば済む。**

### 5. 【訂正】「共通モードでは刻まない」は誤りだった

- 一度「`kCFRunLoopCommonModes` のタイマーはアイドルで刻まない」と読んだが、**仕掛けてから
  報告させるまでに 11 時間空き、その間に VW が終わっていた**のが原因。同じ起動のうちに測り
  直したら 367 回刻んだ（[経緯](../Metrics/Timers%20and%20Notifications.md#5-共通モードで刻むかの測り直し)）。
- **教訓: 「仕掛けて、後で報告させる」計測は同じ起動のうちに閉じる。** 刻み 0 件は「刻まない」
  ではなく「もう居ない」かもしれない。

### 6. **VW が undo イベントを開いたまま回している最中**に当たったら——読むのは安全、**書くと混ざる**

（mac。[issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)。
測ったのは **VW のモーダル確認ダイアログ（`AlertQuestion`）の最中**。プローブは undo イベントを
自分では開かず、`DeleteObject(h, useUndo=true)` で VW に開かせた。
[作り方と実測](../Metrics/Timers%20and%20Notifications.md#6-vw-が-undo-イベントを開いたまま回している最中の刻み)）

ドラッグ中・レンダリング中は下記 7——**そこでは VW はイベントを開いていない**ので、
**「書くと混ざる」が当てはまるのはモーダルダイアログの最中だけ**。

#### 刻みは届く。ただし **`kCFRunLoopCommonModes` に登録した 1 本だけ**

- **モーダル中のモードは `NSModalPanelRunLoopMode`。`kCFRunLoopDefaultMode` だけのタイマーは
  1 回も届かない**（3 回とも 0 回。同じ瞬間に 2 本並べて比べた）。
- **刻みの中から `gSDK` を読める**（全件成功）。
- **刻みの中の `IsCurrentlyBuildingAnUndoEvent()` は信用できる**——イベントの状態だけを変えると
  `yes` / `no` が入れ替わった（[実測](../Metrics/Timers%20and%20Notifications.md#登録したモードごとの刻みの届き方)）。

#### **書くと、そのとき開いていた undo イベントに混ざる**

- **刻みの中で作ったものは、そのとき VW が開いていた undo イベントに入る。** 1 段目の取り消しで
  それだけが消え、別の段の対照は 2 段目で消えた（段の境目の観測。[表](../Metrics/Timers%20and%20Notifications.md#刻みから書いたものと-undo-イベントの実測)）。
- **利用者が 1 回取り消すだけで、刻みが作ったものが無関係な作業と一緒に消える。** 逆に、刻みが
  作ったものだけを狙って残すことはできない。

#### 線引き——刻みの中で何をしてよいか

- **読むのはよい。書くのは避ける**（混ざる先をこちらが決められない）。
- **書くなら「`IsCurrentlyBuildingAnUndoEvent()` が `no` のときだけ」。** ふつうに触っている
  限り刻みがイベント中に当たることはほとんど無い（1304 刻みで 0 件）ので、`no` を待つのは現実的。
  ただし `yes` の意味は広い（次項）。

#### 「いま触ってよいか」を問える口は 1 つだけ。しかも `yes` の意味が広い【ヘッダ根拠】

- 口は `IsCurrentlyBuildingAnUndoEvent()`（`ISDK.h:2612`。コメント無し）だけ。
- **`yes` は「利用者が操作中」を意味しない。** SDK 内部が勝手に開き（PIO ＋ `ResetObject`・
  ビューポートの生成と更新・`DeleteObject(useUndo=true)`・`CreateLocus` 1 つでも）、
  スクリプトエンジンはコマンドをまたいで居座らせ、文書ごとの状態である（[Undo](Undo.md)・
  [Documents](Documents.md)）。**`yes` を「触るな」の旗にすると永久に何もできなくなりうる。
  信用できるのは `no` の側。**
- **通知に「undo イベントが開いた」は無い。** undo に触れるのは `kNotifyUndoEndEvent`
  （`MCNotification.h:179`、閉じる直前）だけ——「開いた瞬間に旗を立てる」作りは `kNotify*` では
  原理的に書けない（立てられるのは下ろす側だけ）。**刻みの中で毎回問い直すしかない。**
- 囲める通知はツールの点取り（`kNotifyBeginToolMode` / `kNotifyEndToolMode`）だけ。
  **レンダリング中と VW のダイアログは囲めない**（`kNotifyRenderModeAboutToChange` / `Changed` は
  設定の変更、`kNotifyDialogDisplayImminent` は開く側だけ）。

### 7. **ツールのドラッグ中・レンダリング中**に当たったら——届いて読めるが、**VW はイベントを開いていない**

（mac。[issue #213](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/213)。
ドラッグは `kNotifyBeginToolMode` → `kNotifyEndToolMode` の囲みで機械的に押さえた。
[実測](../Metrics/Timers%20and%20Notifications.md#7-ツールのドラッグ中レンダリング中の刻み)）

#### 答え——**どちらの場面でも刻みは届き、`gSDK` を読める。そのとき VW のイベントは開いていない**

- 共通モード（か全モード）のタイマーは**ドラッグ中もレンダリング中も 250 ms ちょうどで刻む**。
- **読みは全件成功・例外 0。`building=yes` は clean な 4 回・3976 刻みで 0 件**——「書くと混ざる」
  場面はドラッグ中にもレンダリング中にも現れない。

#### なぜ開いていないか——**VW はランループが回っていない区間でイベントを開閉している**

- `kNotifyUndoEndEvent` は点取りの中で飛ぶが、**その瞬間の主ランループのモードは毎回 nil**
  （回っていない）。**刻みはランループが回る瞬間にしか配られないので、その窓に入れない**
  （観測。[ログ](../Metrics/Timers%20and%20Notifications.md#undo-イベントが開閉した瞬間のランループのモード)）。
- 上記 4 の 3「こちらの同期処理には割り込まれない」の裏返しが、**VW 側の同期処理についても
  成り立つ**（推論ではなく、通知が飛んだ時点のモードを読んだ観測）。
- **モーダル確認ダイアログ（上記 6）だけが、「VW がイベントを開けたまま入れ子のランループを回す」
  唯一の場面**。

#### レンダリング中は **`NSModalPanelRunLoopMode`**——既定モードだけのタイマーは**6 分止まる**

- 利用者がメニューで描かせたレンダリングの間、**VW は入れ子の `NSModalPanelRunLoopMode` で
  イベントを回す**（上記 6 と同じモード）。共通モードのタイマーは 250 ms ちょうどで刻み続け、
  **既定モードだけのタイマーは 351.6 秒（5 分 52 秒）まるごと止まった**
  （[実測](../Metrics/Timers%20and%20Notifications.md#レンダリング中のモードと既定モードだけのタイマーの刻み)）。
- ＝レンダリング中は既定モードが 1 度も回らない。レンダリングのために VW が undo イベントを
  開くことはない。

#### 既定モードだけのタイマーは、ふつうの操作でも十数秒から数分止まる

- 7 回とも、共通モードの 1 本は 1〜2 秒（重い回で 9.7 秒）で戻り、**既定モードだけの 1 本は
  十数秒〜30 秒（最大 62.6 秒）止まった**（[表](../Metrics/Timers%20and%20Notifications.md#ふつうの操作中の既定モードだけのタイマーの刻み)）。
- **既定モードだけで組むと、受け付けが分単位で止まる。共通モード（か全モード）へ登録する。**

#### 【落とし穴】**コマンドが undo イベントを開いたまま返すと、数十秒開いたままになる**

- `DeleteObject(h, useUndo=true)` が開けたイベントを閉じずにコマンドから返すと、**53 秒後まで
  閉じず**、その間の刻みの書き込みはそのイベントに入った。
- **戻る直前に自分で `EndUndoEvent` で閉じる**と 0 件になった。処理の途中に 1 度閉じるだけでは
  足りない（後から開くものが残る）。[Undo](Undo.md) の「外部の終了時に自動で閉じる」は、
  **少なくともコマンドが戻った時点では閉じない**（[実測](../Metrics/Timers%20and%20Notifications.md#コマンドが-undo-イベントを開いたまま返したときの実測)）。

#### 【道具について】**SDK から「描き直して、終わるまで待つ」ことはできない**（#215 で決着）

（mac。[issue #215](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/215)。
3D のビュー（`GetProjection(layer) != 6` で機械的に確認）・球 196 個で測った。
[実測](../Metrics/Timers%20and%20Notifications.md#描き直して終わるまで待つ口を探した記録215)）

#213 で「2D ビューのままだったから描けない」と見ていたのは誤りで、**3D でも描かない**。
`GetProjection(layer) != 6` が「上面/平面を出た」の唯一の機械判定で、#213 に欠けていたのは
この 1 行だった。コマンドが終わって VW が自分のイベントループへ戻ってから描くのかは測って
いないが、プラグインの用途（描き直して、終わるまで待つ）には、どちらでも答えは変わらない。

##### 「戻るまでの時間」で測ってはいけない

- `UpdateViewport` が戻った後もステータスバーに「Viewport-1 の更新 0:16」が出ていた——**戻るまでの
  時間は描画の量を測っていない。** 測るなら**戻った後にランループを短く回し、1 周が伸びるか**を見る。

##### 1. `SetRenderMode` は **`immediate=true` でも描かない**——先送りですらない

- **3D でも 2D でも 0 ms で戻り（32 回）、戻った後にランループを 5 秒回しても何も起きない。**
  ヘッダのコメント（`APIBase.Legacy.Defs.h:4225`）と合わない。**モードを書くだけの口と思ってよい。**
- **戻り値は当てにならない**: `renderFinalShaded`(5) だけ `true`、`renderOpenGL`(11) /
  `renderFinalHiddenLine`(6) / `renderFinalRenderWorks`(14) は `false` だが、どれもモードは変わる。
- `SetProjection` も 0 ms で戻る（描画は起きない）。

##### 2. `UpdateViewport` は描く——が、**戻った時点では描き終わっていない**

- **中身は入る**（表示レイヤを入れ、`SetDirty(true)` とワイヤフレーム／スケッチ以外の描画種別が
  要る。[Viewports](Viewports.md)）。作りたてのままの `UpdateViewport` は 0 ms で何も起きない。
- **戻った後にランループへ 545 ms ぶんの仕事が出てくる。** **`IsDirty()` は更新直後に `no` を返す
  が完了の合図ではない。** 戻り値（`void`）も当てにできない。

##### 3. 帰結——プラグインから「描き直して、終わるまで待つ」はできない

- **描かせる口は `UpdateViewport` だけ**で、レイヤの画面描画を起こす道は無い。
- **終わりを知る口・通知が無い。** 近道は「ランループを回して 1 周が伸びなくなるまで待つ」だけで、
  **信号ではなく当て推量**——締切を必ず持つ。
- SDK から起こした描画の呼び出し中は 250 ms のタイマーは 0 回（VW は入れ子のランループを回さない）。
  **利用者がメニューから描かせたとき**だけ `NSModalPanelRunLoopMode` で届く——SDK から起こした
  描画とは別物。`UpdateViewport` の先送りされたぶんは普通のランループで走るので、そこでは届く。
- 戻った後に VW が何をするかは [#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)
  で一部を測った（[Investigation Techniques](Investigation%20Techniques.md)「いつ読むか」）。

##### 効かなかった口（測ったので残す）

- **`VWLayerObj::SetSheetPrintDPI(300)` は効かない**（直後の `GetSheetPrintDPI()` が 72）。
- **`VWViewportObj::SetScale()` は数が大きいほど絵が大きくなる**（縮尺の分母ではない）。
- **「いま描いているか」を問う口と、レンダリングの開始・終了の通知は無い。**
  **`kNotifyRenderModeAboutToChange` / `Changed` は SDK からの `SetRenderMode` では飛ばない**
  （利用者がメニューで変えたときだけ）——プログラムからの変更の目印には使えない。レンダリング中を測るなら、利用者にメニューから描かせて
  刻みの `mode=`（`NSModalPanelRunLoopMode`）で読む。

### 8. **図面が 1 枚も開いていないとき**の `GetCurrentLayer()`——**nullptr が返り、落ちない**（件数と常に一致）

（mac / Windows。[issue #224](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/224)。
[実測](../Metrics/Timers%20and%20Notifications.md#8-図面が-1-枚も開いていないときの-getcurrentlayer-の実測)）

- **全部閉じた後も、起動直後（殻の時計の最初の刻み・起動画面の頃）も `nullptr` が返り、落ちない**
  （mac / Windows）。
- **件数（`GetOpenFilesList()`）と `nullptr` かどうかは刻みごとに常に一致**（mac 1 万 2 千回余り・
  Windows 3 万 5 千回余りで食い違い 0）。閉じる途中・開く途中の刻みは来なかった（ブリッジの道でも、
  Windows の人が閉じて開く道でも）——「中途半端な状態」を刻みが見る場面は、少なくともこの道では
  現れない。
- **【注意】閉じた文書のレイヤと後で開いた別の文書のレイヤが同じアドレスで返る**（mac / Windows）。
  **ハンドルを刻みをまたいで持ち越さず、刻みのたびに引き直す。**
- **帰結**: 「図面が開いているか」は `GetCurrentLayer() != nullptr` で判定してよく（安上がり。
  殻の `plugin/src/McpBridge.cpp` の `PrepareRun` / `vw_ping` もそうしている）、**図面が無いときに呼ぶのを避ける必要は無い**。
- 測定中に踏んだ Windows の起動時クラッシュは殻の時計の古い番地が原因だった
  （[issue #233](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/233)。
  [プラグインモジュール](Plug-in%20Modules.md)「【罠】Windows の VW は起動時にモジュールを一度降ろして
  読み込み直す」）。
- Windows で `SetTimer` の刻みが 10 秒近く止まった回があり、メニューの追跡中だった見込みが高い
  【推定】（[#235](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/235) で
  メニューを開いてからダイアログが出るまで 7〜14 秒止まることを実測。下記 9）。

### 9. 刻みから **`CloseAllFilesAndQuitVectorworks(true, false)`** を呼ぶ——**呼べる。呼び出しは戻り、終了はその後のイベントループで起きる**

（mac / Windows。[issue #230](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/230)。
[実測](../Metrics/Timers%20and%20Notifications.md#9-刻みから-closeallfilesandquitvectorworkstrue-false-を呼んだ実測)）
**mac と Windows で刻みの振る舞いがはっきり違う。**

- **落ちない。** mac 6 回・Windows 3 回とも `atexit` まで届く正常終了。
- **呼び出しは図面を閉じ終えてから戻り、終了はその後のイベントループで起きる**（mac は戻って
  約 4〜8 秒後）。**応答を書いてから呼んでも、戻った後に書いても間に合う。**
- **キャンセルされれば何も変わらず戻る**（図面もそのまま、刻みも続く）。
- **mac では戻った後も刻みが届き、`gSDK` を読めた**（0 件・`no`・`nullptr`）。タイマーを止めなくても
  何も起きなかった。
- **保存の確認は「未保存の変更」があるときだけ出る。刻みの中で（undo イベントの外で）図面に
  書いても文書は「変更あり」にならない**（`MarkCurrentDocChanged()` で出る。[Documents](Documents.md)
  「未保存の変更ありは読めない」。#240）。ここで使った新規の図面は実際には保存済みだった【推定】
  ——無題のままの図面は、何もしていなくても確認を出す（同上）。

#### 保存の確認の最中（問い 4）

- **観測のタイマーは刻み続ける**（mac。モードは `NSModalPanelRunLoopMode`）。
  **`IsCurrentlyBuildingAnUndoEvent()` は `no`**（mac・Windows。`AlertQuestion` の最中とは違う）
  ——保存の確認は undo イベントを開いたまま出るものではない。
- **呼んだタイマー自身は、mac では入り直さない**（`CFRunLoopTimer` は自分の callout の最中に発火
  しない）。**Windows では入り直す**（35〜86 回）——そこから 2 度目を呼べてしまう。＝「1 本の
  タイマーの中で呼ぶから入れ子は起きない」は mac でしか成り立たない。
- **2 度目を呼ぶと保存の確認が 2 つ重なる**（落ちない）。**「終了の呼び出しの最中」を旗で持って
  入れ子の要求を見送る**（Windows では必須）。`IsCurrentlyBuildingAnUndoEvent()` はこの判定に
  使えない。

#### 利用者が開いたモーダル（環境設定・オーガナイザ・ファイル設定）の最中

（[issue #235](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/235)。
[Windows の表](../Metrics/Timers%20and%20Notifications.md#利用者が開いたモーダル環境設定オーガナイザファイル設定の最中)と mac の表）

- **刻みは届く**（Windows は `#32770` のメッセージループが配り、mac は `NSModalPanelRunLoopMode`。
  **既定モードだけのタイマーは mac では 0 回**）。
- **呼ぶと、保存の確認がダイアログの上に重なって出る。ダイアログを閉じもせず、待ちもしない**
  （mac は確認がダイアログの内側に出た。前後は画面で見ていない【推定】）。
- **「キャンセル」で戻り、元のダイアログは開いたまま操作できる**（mac・Windows）。
- **確認の最中の入り直しは Windows だけ**（225〜297 回）。mac は 0 回。
- **このダイアログの最中は `IsCurrentlyBuildingAnUndoEvent()` が `yes`**（3 種とも、mac・Windows）
  ——**この間に刻みから図面へ書くと、その undo イベントに混ざる**（上記 6）。終了を呼ぶだけなら
  書いていないので関係しない。
- **Windows で刻みが止まるのはメニューの追跡中**（メニューを開いてからダイアログが出るまで
  6.9〜14.4 秒）。ダイアログの最中は止まらない。
- **線引き**: ダイアログを開いたまま `quit` が来ても落ちず、キャンセルすれば元どおり。**避ける
  手段を探す必要は無い**（保存しない／保存を選んだときにダイアログごと終わるかは測っていない）。
  「終了の呼び出しの最中」を旗で持つ作りなら mac・Windows で同じに書ける。

#### 終了を知らせる通知（問い 5 の後半）

- **`kNotifyBeforeApplicationQuit` は「終わる」合図にならない**——呼び出しと同じミリ秒に、保存の
  確認より前に、**キャンセルされた回にも届く**（コメントの「immediately before the application
  quits」とは違い「終了の手続きに入った」）。**これでタイマーを止めると、キャンセル後に止まった
  ままになる。**
- **`kNotifyDocClose` は図面が閉じた時点で届く**（図面 0 枚なら届かない）。
- **`plugin_module_main` に `kPluginModuleDeinit`（-2）が、刻みが止まった後に 1 回届く**（mac は
  `atexit` の約 0.1 秒前、Windows は約 20 秒前）。**-2 の後に刻みは来ない**ので、止めるなら -2 で
  止める（止めなくても何も起きなかった）。-2 は「タイマーを止める入口」として遅すぎも早すぎも
  しない——キャンセルされた回には来ず、来た時点ではもう刻みは来ない。キャンセルされた回には来ない。直後に -5 が 1 回続く。
- SDK が -2 でしているのは `GS_ReleaseStandardVCOMGlobals`（`MiniCadCallBacks.cpp:256`）で
  グローバルを手放すことだけ【ソース根拠】。**-2 の中で `gSDK` を呼べるかは測っていない。**
- **-2 は VW が読み込んだモジュール（殻）にしか届かない**（殻が読み込む本体には来ない）。

#### Windows

（[表](../Metrics/Timers%20and%20Notifications.md#windows)）

- **終わっていく回では、呼び出しの後に刻みが 1 回も来ない**——終了の途中で `gSDK` を呼ぶ心配は無い。
- **呼び出しは mac より遅く戻り（0.7〜1.3 秒）、プロセスはその後 28〜30 秒生きている**（-2 は戻って
  8〜10 秒後、`atexit` はその約 20 秒後）。**プロセスの終了で見届けるなら 30 秒以上待つ**
  （仮想環境での値）。
- **利用者が閉じる道（メニューの「終了」）でも並びは同じ**で、タイマーを止めない常駐プラグインが
  入っていても終わり方は変わらない（[Plug-in Modules](Plug-in%20Modules.md)「OS タイマーを持つ
  常駐プラグイン…は、終了時の落ち方を変えない」。82 回）。
- `kNotifyBeforeApplicationQuit` の届き方・確認の最中の `no` は mac と同じ。
- 測定中の起動時クラッシュは、殻の時計が起動時の読み込み直しで古い番地を指したまま残った
  ためと分かった（issue #253。[Plug-in Modules](Plug-in%20Modules.md)「【罠】Windows の VW は
  起動時にモジュールを一度降ろして読み込み直す」）。この節の測定中のものも同じ【推定】。

#### 利用者が閉じる道の刻み（Windows）

（[issue #269](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/269)。
[11 回の表](../Metrics/Timers%20and%20Notifications.md#利用者が閉じる道の刻みwindows)）

- **`kNotifyBeforeApplicationQuit`（`BApQ`）の後に刻みは 1 度も来ない**（11 回とも）。閉じる指示の
  後に来た刻みは `BApQ` より前の数 ms〜63 ms だけ。**閉じていく途中に刻みから `gSDK` を呼ぶ場面は
  無く、タイマーを止めておく必要も無い。**
- 並びは **閉じる指示 → `BApQ` → `DocClose` → -2（+7.3〜9.5 秒）→ -5 → `atexit`（-2 の約 19〜20 秒後）
  → 終了**。
- 11 回中 1 回（図面を閉じてホーム画面へ戻した後に閉じた回）、**終了の途中に `libcef.dll_unloaded`
  の `0xC0000005` で落ちた**。刻みは `BApQ` 以降 0 回なので刻みが落としたものではない
  （[Plug-in Modules](Plug-in%20Modules.md)「OS タイマーを持つ常駐プラグイン…」と同じモジュール）。

#### 起動の途中の刻みから（issue #312）

（[issue #312](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/312)。
[実測](../Metrics/Timers%20and%20Notifications.md#起動の途中の刻みから呼んだ実測)。
**mac だけ。Windows はまだ測っていない**——下記は Windows には当てはめない。Windows の仮想環境で
DirectX を有効にできず保留中で、[#316](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/316) で測る。）

- **起動画面の前半には刻みが来ない**（mac）。殻が `plugin_module_main` で時計を動かしても、
  最初の刻みは**モジュールの読み込みから 10.7〜15.2 秒後**。その後も、起動を終える前の
  **約 13 秒は刻みが 1 度も来ない**（ワークスペースの読み込み〜起動の終わり）。
  起動の途中で刻みが届くのは、次の 2 つの間だけだった。
  - **Cinema4D（Cineware）の起動を待つ間**——`applicationDidFinishLaunching` の中の
    `CMaxonRenderDevice::Startup()` → `WaitUntilCinewareIsRunning` が `runUntilDate:` を回す。
    起動画面は出たまま。
  - **「開発元不明のプラグイン」のダイアログの間**（`NSModalPanelRunLoopMode`。共通モードの時計だけ）。
- **そこから呼ぶと、呼び出しは 17〜35 ms で戻り、`kNotifyBeforeApplicationQuit` も届くが、
  Vectorworks は終わらない。** 落ちもせず、「サポートファイルの読み込みに失敗しました。」も
  出なかった。終わり方は場面で違う:

  | 呼んだ場面 | その後 |
  | --- | --- |
  | Cineware の起動を待つ間（3 回） | **起動画面のまま止まる**（目視）。刻みは 50 ms ごとに来続け `gSDK` も読めるが、起動は先へ進まず、`atexit` も -2 も来ない。**SIGTERM でも終わらず、SIGKILL で止めるしかなかった** |
  | ダイアログの間（2 回） | ダイアログが開いたまま残る（目視）。**利用者が「続ける」を押すと、そのまま起動を続けてホーム画面まで進み、ふつうに使えた**（目視）——**終了の頼みは黙って捨てられた** |
  | 起動を終えた後（2 回。比べるため） | 4 の 9 と同じ。155〜187 ms で戻り、5〜6 秒後に `atexit` |

- **止まる仕組み【推定】**（止まったプロセスのスタックから）: 呼び出しは終了の AppleEvent を
  積むだけで戻り、その AppleEvent が **Cineware の起動待ちの `runUntilDate:` の中で**処理される
  （`_handleAEQuit` → `terminate:`）。VW の終了処理が `CMaxonRenderDevice::IsRunning()` →
  `IsCinewareInitialized` で Cineware を待ち、その待ちが同じスタックの外側の起動待ちの上に
  入れ子になって、どちらも明けない。ダイアログの間は、モーダルのループが AppleEvent を
  処理しないので `terminate:` に入らない。
- **帰結**: mac では、**起動を終える前（ホーム画面が出る前）に終了を頼んではいけない**。
  頼んでも戻り値は無く、`kNotifyBeforeApplicationQuit` も届くので、呼んだ側からは失敗が
  見えない。そのうえ Cineware の待ちに当たると、**殺す以外に止められない**。
  「起動を終えたか」を確かめる目印は、下記「起動を終えた目印」（#317）。

#### 戻った後の刻みから、もう一度呼ぶ（issue #313）

（[issue #313](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/313)。
[実測](../Metrics/Timers%20and%20Notifications.md#戻った後の刻みから-2-度目を呼んだ実測)。
**mac だけ。Windows はまだ測っていない**（[#316](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/316) で測る）。）

- **何も起きない**（mac。図面 0 枚・2 周）。戻った後に届いた**最初の刻み**から 2 度目を呼んでも、
  **届いた刻みのたびに**呼んでも（8 回。最後の刻みを含む）、**落ちず、どれも 0〜1 ms で戻った**。
- **終わり方は 1 度だけ呼んだ回と変わらない。** 1 度目が戻ってから `atexit` まで 4.4〜9.5 秒、
  -2 は `atexit` の約 0.35 秒前（1 度だけ呼んだ回は 4.7〜6.0 秒）。
- **2 度目以降には `kNotifyBeforeApplicationQuit` が届かない**（全部で 1 回）——保存の確認の
  **最中**に 2 度目を呼んだ回（上記。2 度呼べば 2 回届く）とは違う。
- **帰結**: 「終了の呼び出しの最中」の旗は**戻ったら下ろしてよい**（mac）。戻った後の刻みから
  呼び直されても害は無い。

#### 起動を終えた目印（issue #317）

（[issue #317](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/317)。
[実測](../Metrics/Timers%20and%20Notifications.md#起動を終えた目印を探した実測)。
**mac だけ。Windows はまだ測っていない**（[#316](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/316) で測る）。）

- **SDK の中の目印は「最初の `kNotifyBeforePendingUpdate`（`'BEFO'`）」**（mac）。
  `RegisterNotificationProcedure(proc, kNotifyBeforePendingUpdate)` を `plugin_module_main`
  か最初の刻みで登録しておけば、**ホーム画面が出た後に初めて届く**（起動画面・Cineware の起動待ち・
  「開発元不明のプラグイン」のダイアログの間には 1 度も届かない）。ヘッダのコメントどおり
  VW のアイドル（`OnIdle`）から来るので、**起動を終えてアイドルに入った**しるしと読める。
  - ダイアログを出さない 6 回: ホーム画面の行（`VW User Log.txt`）の 2.9〜8.1 秒後に届いた。
  - ダイアログを出した 3 回: ダイアログが開いている 4〜7 分のあいだ 1 度も届かなかった。
  - ダイアログで人が「続ける」を押した 1 回: 押した後、ホーム画面の行の 2.6 秒後に初めて届いた。
  - その後は 20 ms ほどの間隔で何度も届く。目印に使うのは**最初の 1 回**で、旗を立てるだけにする
    （通知の中から終了を呼ばない。呼ぶのは次の刻みで）。
- **目印の後の刻みから `CloseAllFilesAndQuitVectorworks(true, false)` を呼ぶと、ふつうに終わる**
  （ダイアログを出さない回。`BEFO` の後 2 回・ホーム画面の行の後 1 回・ワークスペース名の後 1 回。
  **ダイアログで「続ける」を押した後の `BEFO` からも 1 回**）。195〜219 ms で戻り、
  `kNotifyBeforeApplicationQuit` が 1 回届き、4.3〜4.6 秒後に `atexit`。
  - **ダイアログの間は待つだけでよい。** 「続ける」が押されるまで `BEFO` は来ず、押されれば起動を
    続けてホーム画面の後に来る。押されないまま放っておかれれば来ない（4〜7 分待って来なかった）
    ので、目印を待つ側に締切を設けるなら、ダイアログが出うることを見込んで長めに取る。
- **目印に使えないもの**:

  | 候補 | 結果 |
  | --- | --- |
  | `kNotifyNewWrkSpace`（`'NWKS'`）・`kNotifyNewWrkSpaceLoaded`（`'WKSL'`） | **起動では届かない**（9 回とも。`plugin_module_main` で登録しても）。名前に反して、起動時のワークスペースの読み込みでは送られない |
  | `kNotifyApplicationActivate`（`'AppA'`） | **起動では届かない**（9 回とも） |
  | `kNotifyDialogDisplayImminent`（`'DlgO'`） | 「開発元不明のプラグイン」のダイアログの直前に届く。ホーム画面では届かない |
  | `GetCurrentWorkspaceFileName` が空でなくなる | **ダイアログの間にもう名前を返す**（起動の途中でワークスペースを読み終える）。ダイアログが出ないときは、刻みの空きのせいでホーム画面の後に初めて見えるだけ |
  | `IsDocumentReady()` | 図面が 0 枚の間ずっと偽。起動とは関係しない |
  | `VW User Log.txt` の `Section = HomeScreen` の行 | mac にも書かれ、ホーム画面の後にしか現れない（ダイアログの間は起動の行が 1 行も無い）。**ただし SDK の外**——ファイルの場所が版で変わり、起動の行は差し替えたファイルにまとめて書かれる。プラグインの中では `BEFO` で足りる |
  | NSApp の窓に `NNAApplicationWindow` が見える | ホーム画面の後にしか見えない（ダイアログの間は無い）が、VW の内部のクラス名に頼るので使わない |

- **刻みは起動を終える前に 11.6〜21.3 秒空く**（#312 の約 13 秒と同じ区間）。ワークスペースの読み込み〜
  ホーム画面の間で、空きの後の最初の刻みが来たときにはもうホーム画面が出ている。
- **ダイアログの間は `osascript` の終了の頼みにも応えない**（`runModalForWindow:` の中。#312 の
  「頼みは捨てられる」と同じ）。

**まとめ（mac / Windows）**: 刻みから終了を呼んでよい。**「終了の呼び出しの最中」を旗で持ち、その間の
要求は見送る**（Windows では必須）。タイマーを止める必要は無く、止めたいなら `kPluginModuleDeinit`
で止める——`BApQ` で止めるのは誤り。呼ぶ側はプロセスの終了を **30 秒以上**待つ（Windows）。

## 5. ウェブパレットの JS タイマーは、隠れると 60 秒に 1 回まで間引かれる

（mac。[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
の PR #192 / #194 の実機確認。[実測](../Metrics/Timers%20and%20Notifications.md#5-ウェブパレットの-js-タイマーを隠したときの実測)）

- **パレットを閉じた（隠した）とき、または VW が裏に回ったとき、JS の `setInterval`（250 ms）が
  ちょうど 60 秒に 1 回まで間引かれる。** 出し直すとすぐ戻る。
- **App Nap から外しても変わらない**（＝ OS の省電力ではない）——埋め込みブラウザ（**Chromium Embedded Framework**）の
  intensive wake-up throttling と見られる。

### SDK に間引きを止める口は無い

- `IWebPaletteFrame`（`Reload` / `LoadURL` / `ExecuteJavaScript` / `SetTitle` / `Focus`）にも
  `IWebCallbacksProvider` / `IWebJavaScriptProvider` にも、間引き・省電力の設定は無い。
- **SDK 全体で CEF に触れる口は `WebDlgEnableConsole(enable)` ただ 1 つ**（コンソールログを出す
  だけ）。CEF のスイッチを足す口も無い。
- **手当ては「パレットの外に時計を置く」しかなく、それは成り立つ**（上記 4）【ソース根拠】。

### 代わりに使える 2 つの口（どちらも SDK 側にある）

- **`IWebJavaScriptProvider::OnPaletteVisibilityChange(bool visible, IWebPaletteFrame* frame)`**
  ——隠れた／出たことが C++ 側で分かる（`IExtensionWebPalette.h:144`）。「隠れている間は JS を
  当てにしない」と切り替える足場になる【ヘッダ根拠】。
- **`IWebPaletteFrame::ExecuteJavaScript(code)`**——時計を C++ 側へ移し、間引かれたページを C++ から
  起こせる（`ExecuteJavaScript(code, onCompleteCallback)` のコールバックは「**outside UI thread**」
  なので、そこから `gSDK` を触らない）【ヘッダ根拠】。

## 6. 図面が 1 枚も開いていないと、ウェブパレットは出ない

（mac。[実測](../Metrics/Timers%20and%20Notifications.md#6-図面が-1-枚も開いていないときのウェブパレットの実測)）

- **再起動直後、図面が 1 枚も無い画面では `SetWebPaletteVisibility(iid, true)` を呼んでも出ず、
  JS タイマーも動かない。** 図面を新規作成すると開いた状態で現れる。
- **SDK 側に出す手立ては無い**（パレットに触れる口 3 つはどれも `VWIID` 1 つ（＋ `bool`）を取る
  だけ）【ヘッダ根拠】。メニュー拡張も `EMenuEnableFlags::DocIsActive` で文書が無い間はグレーアウト
  する（実測）。VW がパレットを文書の窓に紐付けていると見るのが自然だが、仕様の明記は無い。
- **「文書が開いていない間はウェブパレットは動かない」前提で設計する。** 文書が開いたことは
  `kNotifyDocOpen` で拾う【ヘッダ根拠】。
- **受け付けそのものは文書が無くても動かせる**——OS のタイマーへ移せば、起動直後の取りこぼしも
  無くなる（上記 4 の 1。実測）。

## まだ分かっていないこと（別の issue に切り出したもの）

- （**VW が undo イベントを開いたまま回している最中に当たったら何が起きるか**は
  [issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)
  で**実機確認が取れたので、ここからは外した**。答えは上記 6。「プローブは undo イベントを
  自分では開かない決まりがあるので狙って作れない」と見ていたが、**VW に開かせる梃子が
  あった**——詳しくは上記 6。）
- （**ツールのドラッグ中・レンダリング中に当たったらどうなるか**は
  [issue #213](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/213)
  で**実機確認が取れたので、ここからは外した**。答えは上記 7——**どちらも刻みは届いて
  読めるが、VW はそのとき undo イベントを開いていない**。「undo の記録に混ざるかどうかは
  場面に依らず同じと見ている」と書いていたが、**そもそもその場面が現れない**のだった。）
- （**3D のビューに切り替えてから `SetRenderMode(…, immediate=true)` を呼べばその場で
  描くのか**は [issue #215](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/215)
  で**実機確認が取れたので、ここからは外した**。答えは上記 7 の「道具について」——
  **3D でも描かない。原因は 2D ビューではなかった。** 先送りですらなく、こちらが
  イベントループを回してやっても何も出てこない。）
- （**Windows の `SetTimer`** は上記 4 で実機確認が取れたので、ここからは外した。
  [issue #205](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/205)
  はそのための issue だったが、**この調査の実機確認が mac と Windows の両方で取れた**
  ため、答えは上記 4 に入っている。）
