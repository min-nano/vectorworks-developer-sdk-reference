# 周期実行と通知（タイマー・アイドル・`kNotify`）

「**暇なときに・周期的にプラグインのコードを呼んでもらう**口はあるか」の調査
（[issue #204](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/204)）。
出どころは MCP ブリッジ——外からの要求を拾うために、ウェブパレットの HTML の
`setInterval`（250 ms）から C++ を呼んでいたが、**パレットを隠すと 60 秒に 1 回まで
間引かれる**ことが実機で分かった（下記 5）。

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
| **利用者が閉じる**（メニューの「終了」）とき、刻みはいつまで来るか（Windows） | **`kNotifyBeforeApplicationQuit` の後には 1 度も来ない**（11 回）。閉じる指示から BApQ までの数 ms〜63 ms に 0〜2 回来るだけ。その後 -2（約 7〜9 秒後）・`atexit`（約 27〜29 秒後）まで刻みは無い。**＝閉じていく途中に刻みから `gSDK` を呼ぶ場面は無い**（下記 4 の 9「Windows」） |
| **図面が 1 枚も開いていない**とき、刻みから `GetCurrentLayer()` を呼ぶと | **`nullptr` が返り、落ちない**（閉じた後も起動直後も）。`GetOpenFilesList()` の件数と**刻みごとに常に一致**（mac 1 万 2 千回余り・Windows 3 万 5 千回余りで食い違い 0）。ただし**閉じた文書のレイヤと別の文書のレイヤが同じアドレスで返る**ので、ハンドルは刻みごとに引き直す（mac / Windows とも。下記 4 の 8） |
| 図面が開いていないときにウェブパレットを出せるか | **出す口は無い**（`SetWebPaletteVisibility` を呼んでも出ないのは実測）。**「文書が無い間は動かない」前提で設計する**（下記 6） |

## 1. ISDK に「暇なとき・周期的に」の口は無い

`ISDK.h`（VW 2026 SDK）で「登録」「タイマー」「アイドル」に関わる宣言を**全部**並べると
8 本しかなく、周期実行に使えるものは**ダイアログのタイマー 1 種類だけ**である。

```
$ grep -nIE 'virtual .*(Register|Unregister|Timer|Idle|Periodic|Tick|Yield)' ISDK.h
1433:  Boolean RegisterMenuForCallback(short menuIndex, OSType whichChange)
1435:  void    UnregisterMenuForCallback(short menuIndex, OSType whichChange)
1750:  Sint32  RegisterExternal(const TXString& fileName, short codeResID, OSType codeResType)
1751:  Boolean RegisterNotificationProcedure(StatusProcPtr proc, OSType whichChange)
1759:  void    UnregisterNotificationProcedure(StatusProcPtr proc, OSType whichChange)
2006:  bool    RegisterDialogForTimerEvents(Sint32 dialogID, Uint32 numberOfMilliseconds)
2007:  void    DeregisterDialogFromTimerEvents(Sint32 dialogID)
2524:  Uint32  TickCount()   // returns 1/60th of a second. Approx: mili_sec = TickCount() * 17;
```

- **`RegisterDialogForTimerEvents(dialogID, ms)` は「ダイアログが開いている間」の時計である。**
  VWFC 側の口は `VWDialog::RegisterForTimerEvents(ms)` / `DeregisterForTimerEvents()` で、
  受けるのは `VWDialog::OnDialogTimer()`（`Dialog.h:355-356, 400`）。VectorScript にも
  同じものがある（`RegisterDialogForTimerEvents` / `DeregisterDialogFromTimerEvents`）。
  **`VWDialog::RunDialogLayout` はモーダル**（[Layout Dialogs](Layout%20Dialogs.md)）なので、
  これを常駐の受け付けには使えない——使うと利用者は図面を触れない。
- **ウェブパレットには渡せる `dialogID` が無い。** パレットの中身は HTML であって
  `VWDialog` ではなく（[Layout Dialogs](Layout%20Dialogs.md)「モードレスなパレット」）、
  レイアウトダイアログの ID を持たない。【ヘッダ根拠】
- **`TickCount()` は「いま何時か」を読む口で、呼んでもらう口ではない**（1/60 秒単位）。
- `RegisterMenuForCallback` はメニュー項目が**通知を受け取る**ための登録（`whichChange` は
  下記の `kNotify*`）で、周期ではない。`RegisterExternal` は外部モジュールの登録。

**つまり「SDK に常時開けておく口（アイドルコールバック）は無い」という前提は正しい。**
（[Layers and Stories](Layers%20and%20Stories.md) の「SDK に遅延実行の口は無い」も同じ話で、
あちらはビューポートの描き直しを遅らせようとして突き当たったもの。）

## 2. 通知（`RegisterNotificationProcedure` ＋ `kNotify*`）は「出来事」用で、周期ではない

```cpp
typedef Sint32  StatusID;                              // Kernel/API/MiniCadCallBacks.h:34-36
typedef SintptrT StatusData;
typedef void (*StatusProcPtr)(StatusID, StatusData);

gSDK->RegisterNotificationProcedure(proc, kNotifyDocOpen);   // ISDK.h:1751
gSDK->UnregisterNotificationProcedure(proc, kNotifyDocOpen); // ISDK.h:1759
```

- **`whichChange` に渡す値は `Include/Kernel/Core/MCNotification.h` の `kNotify*`**（4 文字の
  OSType。VW 2026 SDK に **184 個**ある）。**公式リファレンス（`Info/` / `Versions/`）には
  1 行も載っていない**（`kNotify` で 0 件）。拾えるものの例:
  `kNotifyDocOpen` / `kNotifyDocClose` / `kNotifyDocPostSaved` / `kNotifySelChange` /
  `kNotifyLayerChange` / `kNotifyClassChange` / `kNotifyViewChange` /
  `kNotifyDynamicViewChange`（対話的な 1 フレームごと——コメントに
  「!!CAUTION - check routine performance!!」とある）/ `kNotifyApplicationActivate`
  （アプリが前面に来た）/ `kNotifyBeforeApplicationQuit` / `kNotifyAutosaveDone` ほか。
- **出来事が起きなければ 1 度も呼ばれない。** 「外からの要求を定期的に拾う」用途の時計には
  ならない（利用者が何も触らずにいれば永久に呼ばれない）。
- **ただし `kNotifyApplicationActivate` は「戻ってきたら拾い直す」のに使える**——間引かれる
  JS タイマーと組み合わせると、**前面に戻った瞬間の取りこぼしだけは埋められる**【ヘッダ根拠】。
- `kNotifyBeforePendingUpdate` / `kNotifyAfterPendingUpdate` のコメントには
  「Many notifications are handed out in a loop (**From OnIdle**)」とあり、**VW 本体には
  アイドルのループがある**ことが読み取れる。しかし**そこへ自分のコードを差し込む口は
  公開されていない**（上記 1 の 8 本しかない）。

## 3. ツール拡張だけは「アイドル」を受け取れる（が、選ばれている間だけ）

```
MiniCadCallBacks.h:6376   kAction_OnIdle = 112,        // ToolMessage::EAction
```

- **ツール拡張（`GROUPID_ExtensionTool`）には `kAction_OnIdle` が来る。** メニュー・PIO・
  ウェブパレットの拡張には同じものが無い（`OnIdle` は SDK 全体でこの 1 行と、上記
  `MCNotification.h` のコメント 1 行だけ）。
- **届くのは自分のツールがアクティブな間だけ**なので、**常駐の受け付けには使えない**
  ——利用者が別のツールを選んだ瞬間に止まる。「ツールを選んでいる間だけ動く監視」
  （カーソル追従など）には使える。【ヘッダ根拠】

## 4. OS のタイマーから `gSDK` を呼ぶ——**できる**（mac / Windows とも実測）

**実測**（VW 2026。macOS arm64 ＋ Windows の両方。`probes/runtime/runloop-timer-sdk/` を
各プラットフォームで「仕掛ける → 2 分ほど置く → 報告させる」の 2 回に分けて実行。
ビルド `7fbdbc35d939`。以下の数値は実行ログそのまま）。

**ウェブパレットの外に時計を置ける。** OS のタイマー（mac: `CFRunLoopTimer` をメインの
ランループへ / Windows: `SetTimer(nullptr, 0, ms, proc)`）は、**メニューコマンドが戻った
後も刻み続け、その刻みから `gSDK` を読むことも図面に書くこともできた**。JS タイマーの
ような間引きも起きない。**ただし間隔は保証されない**（下記）。

### 局面ごとの実測

| 局面 | macOS `CFRunLoopTimer`（`src=cf`） | macOS メインキュー（`src=dq`） | Windows `SetTimer`（`src=win`） |
| --- | --- | --- | --- |
| ① 自分でイベントを 1.5 秒回す | **0 回**（`CFRunLoopRunInMode` が即座に戻る） | **0 回** | **5 回**（228 / 252 / 273 ms） |
| ①b ランループを回さず 3 秒働く | **0 回** | **0 回** | **0 回** |
| ①c 進捗ダイアログの `DoYield` を 3 秒回す | 12 回（200 / 504 / **3019**） | 13 回（200 / 465 / **3047**） | **0 回** |
| ② モーダル（alert）が開いている間 | 60 回（196 / **249** / 250） | 60 回（197 / **249** / 256） | 61 回（227 / 356 / **6579**） |
| ③ **コマンドが戻った後**（約 2 分） | **367 回**（40 / **273** / **3610**）<br>別の回: 343 回（40 / 252 / 1476） | **369 回**（16 / **272** / **3024**）<br>別の回: 342 回（236 / 253 / 1512） | **357 回**（69 / **265** / **4944**） |
| 別の回: ② の alert を開いたまま裏のアプリへ回る | 214 回・522 回（249 / **249** / 251） | — | — |

（`dt` は「最小 / 平均 / 最大 ms」。仕掛けた間隔は 250 ms。）

### 1. コマンドが戻った後も刻み、`gSDK` を読める・**書ける**

- **読み**: 刻みから `TickCount()` / `GetOpenFilesList()` / アクティブレイヤの図形数を
  読んで、**mac で 881 回・Windows で 423 回すべて成功、例外は 0 回**。
- **書き**: **こちらのコードがスタックに 1 本も無い刻みから `CreateLocus` が通った**
  ——mac は `write ## src=cf ## locus=作れた ## objs_after=5`（4 → 5）、Windows は
  `objs_after=4`（3 → 4）。**図面に図形を作れている。**
- **mac は 2 回まわして同じ結果になった**（③ の刻みが 367/369 回と 343/342 回、
  **どちらの回も書き込みが通った**）。**つまり「コマンドが戻った後も刻む」は再現する。**
  ばらつきの最大値は回によって違う（1.5 秒の回と 3.6 秒の回）ので、**下記 2 の
  「最大」は「その回で観測した最悪値」として読むこと**——**上限は決まっていない。**
- **図面が 1 枚も開いていなくても刻みは続く**（ログ末尾の刻みが `docs=0 ## objs=-1`）。
  `gSDK` は落ちずに「文書が無い」を返しただけで、**例外も異常も出ていない**。
  **＝ウェブパレット（文書が無いと出ない。下記 6）と違い、OS のタイマーは文書の有無に
  関わらず動く。**

### 2. ただし**間隔は保証されない**（「250 ms ごと」と当てにしてはいけない）

コマンドが戻った後の `dt` は**平均 252〜273 ms** だが、**最大で 1.5〜4.9 秒空き**
（回によって違う。上限は決まっていない）、**最小は 16〜69 ms**
（空いたぶんが後でまとめて来る）。配り手は OS ではなく
**Vectorworks のイベント処理**なので、VW が同期処理で詰まっている間は刻みが止まり、
明いてから続く（下記 3 がその裏返し）。**受け付けの間隔に意味のある設計
（タイムアウト・一定周期のポーリング）は、刻みの回数ではなく時刻で測ること。**

### 3. こちらの同期処理には割り込まれない（両プラットフォーム）

**ランループを回さずに 3 秒働いた間の刻みは 0 回**だった（mac / Windows とも）。
つまり**プラグインが自分の処理をしている最中——自分が undo イベントを開いて図面を
描いている最中——に刻みが割り込むことはない。** 刻みは「VW がイベントを回す瞬間」に
挟まるだけである。

- **VW が自分でイベントを回す場面では挟まる**: モーダルダイアログが開いている間
  （mac / Windows とも 250 ms 前後で正確）と、**mac の `DoYield` の最中**。
- **`DoYield` はプラットフォームで違う。** mac は刻むが**間隔が荒い**（平均 500 ms・
  最大 3 秒）。**Windows は 1 回も刻まない。** 進捗を出しながら受け付けを回す設計は、
  Windows では成立しない。
- **刻みが VW の undo イベント中に当たった例は、この計測では 0 回**
  （`undo 中 0 回`。刻みごとに `IsCurrentlyBuildingAnUndoEvent()` を記録した。
  2 プラットフォーム・計 1304 刻み）。**「当たらなかった」だけで「当たらない」ではない**
  ——ふつうに 2 分触っている限りでは当たらない、ということである。
  **狙って当てたときに何が起きるかは下記 6**（[issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)。**答えが出ている**）。

### 4. プラットフォームごとの作法

- **macOS: メニューコマンドはメインスレッドで走る**（`thread=main` /
  `runloop=same-as-main`）。
- **macOS: `CFRunLoopRunInMode` は「短く刻んで何度も呼べば」回る**（【訂正】。
  [issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)
  の実機で判明）。当初ここには「**自分で回すことはできない**——即座に戻る」と書いて
  いたが、それは**1 回の呼び出しを長く取ったときの話**だった。実測:

  | 回し方 | 結果 |
  | --- | --- |
  | `CFRunLoopRunInMode(default, 1.5, false)` を **1 回**（上記 ①） | **0 回**・所要 0 秒（即座に戻る） |
  | `CFRunLoopRunInMode(default, 0.6, false)` を **1 回**（#206 の 1 回目） | **0 回** |
  | **`CFRunLoopRunInMode(default, 0.05, false)` を 12 回**（#206 の 2・3 回目） | **8 回届いた**。戻り値 **3 ＝ `kCFRunLoopRunTimedOut`**、実際に費やした時間 **0.611 秒**（0.6 が期待値） |

  **＝短い刻みで呼べば、ちゃんと時間を使って刻みを配ってから `TimedOut` で戻る。**
  2 回の実機で同じ結果（どちらも 8 回）。**「待ちたいなら 0.05 秒ずつ回す」が使える。**
  ただし**これはこちらがランループを回している間の話**で、上記 3 のとおり
  **自分の同期処理が割り込まれるわけではない**（回さなければ 0 回のまま）。
- **macOS: 主ランループが知っているモードは 13 個**
  （`kCFRunLoopDefaultMode` / `NSEventTrackingRunLoopMode` / `NSModalPanelRunLoopMode` /
  `NSGraphicsRunLoopMode` / `NSAnimationRunLoopMode` / `CoreDragMode` /
  `com.apple.run-loop-mode.view-bridge.blocks` / `__kCFPasteboardPrivateMode` /
  `AppleEventReplies` / `IMKClient_…` / `com.apple.accessibilityServerIPC` /
  `dockmsg-mode` / `kCFRunLoopCommonModes`）。**アイドル時に実際に刻みが届いたモードは
  `kCFRunLoopDefaultMode` / `NSEventTrackingRunLoopMode` / `NSModalPanelRunLoopMode` /
  `__kCFPasteboardPrivateMode` / `view-bridge.blocks`** で、**`kCFRunLoopCommonModes`
  だけでも届く**（既定モードが共通モードに入っているため）。全モードへ入れると
  取りこぼしが減る（ペーストボード・view-bridge のモードはそれで拾えた）。
- **macOS: メインキューの `dispatch_source` も同じように使える**（`src=dq`。アイドルで
  369 回・平均 272 ms）。**CFRunLoopTimer とほぼ同じ振る舞い**なので、どちらでもよい。
- **Windows: `SetTimer(nullptr, 0, ms, proc)`（ウィンドウ無し）で足りる。** `WM_TIMER` は
  VW のメッセージポンプが配る。**自分で `PeekMessage` を回せば、その場でも配られる**
  （mac と違ってこちらは効く）。ピン留めも `GetModuleHandleExW` の
  `GET_MODULE_HANDLE_EX_FLAG_PIN` で効いた。
- **【罠】Windows では、VW が起動時に読み込む殻でも留める。** VW は起動時にモジュールを
  一度降ろして読み込み直すので（-1・-3 の後に -2 無しで降ろす）、最初の呼び出しで作った
  タイマーが**もう無い番地を呼び続けて起動中に落ちる**。作る前に
  `GET_MODULE_HANDLE_EX_FLAG_PIN` で自分を留める（[Plug-in Modules](Plug-in%20Modules.md)
  「【罠】Windows の VW は起動時にモジュールを一度降ろして読み込み直す」。issue #253）。
- **入れ替えできるモジュールへ置くなら、降ろさせない手当てが要る。** 殻はプローブが
  終わると本体を `dlclose` する（[Plug-in Modules](Plug-in%20Modules.md)）ので、
  `dlopen(RTLD_NOLOAD)`（mac）／`GET_MODULE_HANDLE_EX_FLAG_PIN`（Windows）で参照を
  増やして留めた。**実プラグインでは殻（起動時に読まれる側）に置けば済む。**

### 5. 【訂正】「共通モードでは刻まない」は誤りだった

この調査の途中で**「`kCFRunLoopCommonModes` に入れた `CFRunLoopTimer` は、VW が
アイドルのときには刻まない」と読んだ回がある**（`outside` 局面の刻みが 0 件）。
**これは誤りで、原因は仕掛けてから報告させるまでに 11 時間が空いたこと**——その間に
Vectorworks が終了していれば、ピン留めした本体ごとタイマーが消えるので、書き溜めは
増えない。**同じ起動のうちに 2 分で測り直したら 367 回刻んだ**（しかも届いたモードには
`kCFRunLoopDefaultMode` が入っている＝共通モードで足りる）。

**教訓: 「仕掛けて、後で報告させる」形の計測は、同じ起動のうちに閉じること。**
プロセスが終われば仕掛けも消えるので、**刻みが 0 件なのは「刻まない」ではなく
「もう居ない」**かもしれない。

### 6. **VW が undo イベントを開いたまま回している最中**に当たったら——読むのは安全、**書くと混ざる**

**実測**（VW 2026 / macOS。`probes/runtime/undo-event-timer-tick/` を 3 回。
ビルド `1d5b70f53506` / `782dd705b13f` / `7fbdbc35d939`。
[issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)）。

上記 3 で分かったとおり、危ないのは「**VW が undo イベントを開けたまま、VW 自身が
イベントを回している**」場面だけである。そこを狙って作って測った。

**測ったのは「VW が出したモーダルダイアログの最中」である。** #206 はこの場面のほかに
**ツールのドラッグ中・レンダリング中**も挙げていたが、そちらは**コマンドが戻った後も
生きるタイマー**が要るので、この調査では作らなかった。
**そちらは [issue #213](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/213)
で測り終えている——下記 7。** 結論を先に書くと、**ドラッグ中もレンダリング中も刻みは
届いて `gSDK` を読めるが、そのとき VW は undo イベントを開いていない**（clean な 4 回・
3976 刻みで `building=yes` は 0 件）。**だから「書くと混ざる」が当てはまるのは、
ここで測ったモーダルダイアログの最中だけである**——VW がイベントを開けたまま入れ子の
ランループを回すのは、その場面だけだから（機構は下記 7）。

**作り方（梃子 2 つ）。プローブは 1 度も `SetUndoMethod` / `NameUndoEvent` を呼ばない:**

1. **VW にイベントを開かせる**——`DeleteObject(h, useUndo=true)` は開いていなければ
   自分で開き、**こちらが return するまで開いたまま**（[Undo](Undo.md)）。
2. **VW にイベントループを回させる**——`ISDK::AlertQuestion`（VW 自身のモーダル確認
   ダイアログ）。上記 3 のとおり、両プラットフォームで効く唯一の場面である。

#### 刻みは届く。ただし **`kCFRunLoopCommonModes` に登録した 1 本だけ**

| 局面 | 札（登録したモード） | 届いた | うち `building=yes` | 読めた |
| --- | --- | --- | --- | --- |
| **VW のモーダル確認ダイアログの最中** | **`kCFRunLoopCommonModes`** | **85 / 44 / 50 回**（3 回ぶん） | **全件** | **全件** |
| VW のモーダル確認ダイアログの最中 | **`kCFRunLoopDefaultMode` だけ** | **0 / 0 / 0 回** | — | — |
| 自分でランループを回した（0.05 秒 × 12） | 両方（札ごとに 4 回） | 4 ＋ 4 回 | **回による**（下記） | 全件 |

- **モードは `NSModalPanelRunLoopMode`。** だから**`kCFRunLoopDefaultMode` だけに
  登録したタイマーは 1 回も届かない**——**同じ図面・同じ瞬間に 2 本並べて**、
  登録モードだけを変えて測ったので、ここは取り違えようがない。
  **刻みを VW のループへ当てたいなら `kCFRunLoopCommonModes`（か全モード）が要る。**
- **刻みの中から `gSDK` を読める。** `IsCurrentlyBuildingAnUndoEvent()` と
  `GetNamedObject()` が全件成功し、例外も異常も出ていない。
- **刻みの中の `IsCurrentlyBuildingAnUndoEvent()` は信用できる**——**同じ局面（自分で
  ランループを回した 4 ＋ 4 回）で、イベントの状態だけを変えて両方の値が出た**:

  | 回 | その局面でイベントは | 刻みの中の `building` |
  | --- | --- | --- |
  | 2 回目（`782dd705b13f`） | 開いたまま（`CreateLocus` が開いたものを放置） | **8 回すべて `yes`** |
  | 3 回目（`7fbdbc35d939`） | `EndUndoEvent` で閉じてある | **8 回すべて `no`** |

  そして同じ 3 回目の中で、**VW が開いている局面（モーダル中）では 50 回すべて `yes`**
  に戻った。**＝刻みの中で「いま開いているか」を問うことは、ちゃんとできる。**

#### **書くと、そのとき開いていた undo イベントに混ざる**

判定は目視に頼らず、**取り消しを段ごとに掛けて何が消えたかを名前で引いて**読んだ。

| 置いたもの | いつ作ったか | 1 段目の取り消し後 | 2 段目の取り消し後 |
| --- | --- | --- | --- |
| 対照 | **作り、その直後にイベントを閉じた**（＝閉じた別の段に入る） | **残った** | 消えた |
| **刻みが作ったもの** | **VW が開いている最中**（刻みの中） | **消えた** | — |

- **1 段目で刻みが作ったものだけが消え、対照は残った。** → 刻みの中での書き込みは
  **そのとき開いていた undo イベントに入る**。
- **2 段目で対照が消えた。** → 2 つは**本当に別の段に居た**。つまり 1 段目の結果は
  「取り消しが広く効いた」のではない。**推論ではなく段の境目の観測である。**
- **帰結: 利用者が 1 回取り消すだけで、刻みが作ったものが、刻みとまったく無関係な
  作業と一緒に消える。** 逆に、刻みが作ったものだけを狙って残すことはできない。

#### 線引き——刻みの中で何をしてよいか

- **読むのはよい。** VW が undo イベントを開いて回している最中でも、`gSDK` の読みは
  全件通った。
- **書くのは避ける。** 書けてしまうが、**そのとき開いている記録に混ざる**ので、
  利用者の 1 回の取り消しが何を持っていくかを**こちらが決められない**。
- **安全側に倒すなら「`IsCurrentlyBuildingAnUndoEvent()` が `no` のときだけ書く」。**
  刻みの中でこの判定ができることは上で確かめた。ただし**`yes` の意味は広い**
  （次項）。
- **`no` を待つのは現実的である**——上記 3 のとおり、ふつうに触っている限り刻みが
  イベント中に当たることはほとんど無い（2 プラットフォーム・1304 刻みで 0 件）。

#### 「いま触ってよいか」を問える口は 1 つだけ。しかも `yes` の意味が広い【ヘッダ根拠】

```cpp
// ISDK.h:2612 —— **コメントが 1 行も付いていない**（前 6 行まで出しても無い）
virtual bool VCOM_CALLTYPE IsCurrentlyBuildingAnUndoEvent() = 0;
```

- **`yes` は「利用者が操作中」を意味しない。** [Undo](Undo.md) の実測どおり、
  **SDK 内部が勝手に開く**（PIO ＋ `ResetObject`・ビューポートの生成と更新・
  `DeleteObject(useUndo=true)`・**`CreateLocus` 1 つでも**）し、
  **スクリプトエンジンはコマンドをまたいで `yes` を居座らせる**。しかも
  **文書ごとの状態**である（[Documents](Documents.md)）。
  → **`yes` を「触るな」の旗にすると、置き土産ひとつで旗が立ったまま居座り、
    以後の刻みが永久に何もできなくなる。** 信用できるのは `no` の側である。
- **通知に「undo イベントが開いた」は無い。** `MCNotification.h` 全体で undo に
  触れる定数は**1 本だけ**（`grep -i undo` の結果が 1 行）:

  ```cpp
  // MCNotification.h:179
  const Sint32 kNotifyUndoEndEvent = 'Udee';	// Send immediatelly before ending an undo event
  ```

  **閉じる直前しか来ない。** だから「開いた瞬間に旗を立てる」作りは `kNotify*` では
  **原理的に書けない**（立てられるのは下ろす側だけ）。**刻みの中で毎回問い直すしかない。**
- **囲める通知があるのはツールの点取りだけ**: `kNotifyBeginToolMode`（`'BTOO'`、
  "Sent when the current tool begins collecting points"）→ `kNotifyEndToolMode`
  （`'ETOO'`、"finishes collecting points"）。**レンダリング中と VW のダイアログは
  囲めない**——`kNotifyRenderModeAboutToChange` / `Changed` は「レンダリング方法の設定が
  変わった」で「いま描いている最中」ではなく、`kNotifyDialogDisplayImminent`（`'DlgO'`）は
  開く側だけで閉じたことを知らせる対が無い。

### 7. **ツールのドラッグ中・レンダリング中**に当たったら——届いて読めるが、**VW はイベントを開いていない**

**実測**（VW 2026 / macOS。`probes/runtime/drag-render-timer-tick/` を 7 回。
ビルド `87d374350eda` / `c905cadc9b07` / `7b140e82b39a` / `1c1c26328292` /
`6b0067dc075a` / `c415caa18616` / `7b16c7ef15be`。計 7000 刻み以上。
[issue #213](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/213)）。

上記 6 で測ったのは「VW が出したモーダル確認ダイアログの最中」だけだった。残る 2 つ
——**ツールのドラッグ中（点取りの最中）とレンダリング中**——をここで測る。ドラッグの
最中に当てるには**メニューコマンドが戻った後も生きるタイマー**が要るので、上記 4 の 4 の
ピン留めの作法で仕掛け、1 回目で仕掛け・2 回目で報告させた（**同じ起動のうちに閉じる**）。

**「ドラッグされたか」は機械で押さえた。** 利用者がドラッグしたかどうかはログに写らない
ので、`kNotifyBeginToolMode`（`'BTOO'`）→ `kNotifyEndToolMode`（`'ETOO'`）で**点取りの
両端を囲み**、その中で届いた刻みだけを数えた（刻み 1 行ごとに `tool=yes/no` が付く）。
囲みが 1 度も来なければ `probe.fail` して「この回はドラッグについて何も言えない」と
結果の見出しに出す作りにした。

#### 答え——**どちらの場面でも刻みは届き、`gSDK` を読める。そのとき VW のイベントは開いていない**

| 場面 | 刻み | うち `building=yes` | 読めた | 届いたモード |
| --- | --- | --- | --- | --- |
| **ツールの点取りの最中**（通知の囲みの中。clean な 4 回） | **180 回** | **0 回** | **全件** | `kCFRunLoopDefaultMode` |
| **トラッキング中**（`NSEventTrackingRunLoopMode`。同じ 4 回） | **120 回** | **0 回** | **全件** | `NSEventTrackingRunLoopMode` |
| **レンダリング中**（仕上げ Renderworks。下記） | **1427 回** | **0 回** | **全件** | **`NSModalPanelRunLoopMode`** |
| clean な 4 回の全部 | **3976 回** | **0 回** | **全件・異常 0 件** | — |

- **刻みは届く。** ドラッグ中・レンダリング中とも、共通モード（か全モード）へ登録した
  タイマーは **250 ms ちょうど**で刻み続けた。
- **その刻みから `gSDK` を読める**（`IsCurrentlyBuildingAnUndoEvent` / `GetNamedObject` /
  図形の数え上げが**全件成功・例外 0 件**）。
- **そのとき VW の undo イベントは開いていない**——clean な 4 回・**3976 刻みで
  `building=yes` は 0 件**。だから**「書くと混ざる」（上記 6）が当てはまる場面が、
  ドラッグ中にもレンダリング中にも現れない。**

#### なぜ開いていないか——**VW はランループが回っていない区間でイベントを開閉している**

機構は通知そのものに写っていた。**VW はドラッグの最中にイベントを開いて閉じている**
——`kNotifyUndoEndEvent` が**点取りの囲みの中（`tool_depth=1`）で飛んでいる**。
ところがその瞬間に `CFRunLoopCopyCurrentMode(CFRunLoopGetMain())` を読むと、
**どの回も nil**（ログの `mode=(not-running)`）である:

```
note ## what=undo-end ## t_ms=31653 ## tool_depth=1 ## building=yes ## mode=(not-running)
note ## what=tool-end ## t_ms=31663 ## tool_depth=0 ## building=no  ## mode=(not-running)
```

- **VW が undo イベントを開いてから閉じるまでの区間は、主ランループが回っていない**
  ——VW 自身の同期処理の中である。
- **ランループタイマーの刻みは、ランループが回る瞬間にしか配られない。**
- → **だから刻みはその窓に入れない。** 上記 4 の 3「こちらの同期処理には割り込まれない」
  の裏返しが、**VW 側の同期処理についても成り立つ**ということ。**推論ではなく観測である**
  （通知が飛んだ時点のモードを読んでいる）。

**上記 6（モーダル確認ダイアログの最中）だけが例外なのは、そこが「VW がイベントを
開けたまま、入れ子のランループを回す」唯一の場面だから**である。

#### レンダリング中は **`NSModalPanelRunLoopMode`**——既定モードだけのタイマーは**6 分止まる**

**実測**（ビルド `7b16c7ef15be`。利用者に「ビュー > レンダリング > 仕上げ Renderworks を
選んで、描き終わるまで何も触らない」だけをしてもらった回。計 1936 刻み・444 秒）:

| 口 | 刻み | dt 最小 / 平均 / **最大** |
| --- | --- | --- |
| **共通モード ＋ 全モード** | **1635 回** | 1 / 261 / 9711 ms |
| **`kCFRunLoopDefaultMode` だけ** | **161 回** | 30 / 2625 / **351603 ms（5 分 52 秒）** |

モードごとに割ると答えが出る:

```
NSModalPanelRunLoopMode:    1427 回（building=yes 0 回）  ← これがレンダリング中
kCFRunLoopDefaultMode:       458 回（building=yes 0 回 / 点取りの囲みの中 68 回）
NSEventTrackingRunLoopMode:   45 回（building=yes 0 回）
com.apple.run-loop-mode.view-bridge.blocks: 5 回 / __kCFPasteboardPrivateMode: 1 回
```

- **VW は進捗を出しながら、入れ子の `NSModalPanelRunLoopMode` でイベントを回す。**
  利用者がレンダリングを選んだのが `t_ms=77154`（`kNotifyRenderModeChanged`）で、
  そこから終わりまで**モーダルのモードの刻みが 1427 回（≒ 357 秒）**続いた。
  **＝上記 6 のモーダルダイアログと同じモード**である。
- **その間、共通モードのタイマーは 250 ms ちょうどで刻み続けた**
  （最後の刻みまで `dt_ms=249〜251`・`mode=NSModalPanelRunLoopMode`）。
- **既定モードだけのタイマーは、その間まるごと止まった**——1 度の間隔が **351.6 秒**。
  **＝レンダリング中は既定モードが 1 度も回らない。**
- **読みは全件通り、`building=yes` は 1 件も無い**（レンダリングのために VW が undo
  イベントを開くことはない）。

#### 既定モードだけのタイマーは、ふつうの操作でも十数秒から数分止まる

登録するモードだけを変えた 2 本を**同じ瞬間に**並べて 7 回測った（上記 6 と同じ作法）。

| 回（ビルド） | 共通モード ＋ 全モードの dt 最大 | `kCFRunLoopDefaultMode` だけの dt 最大 |
| --- | --- | --- |
| 1（`87d374350eda`） | 2201 ms | **30402 ms** |
| 2（`c905cadc9b07`） | 1227 ms | **21530 ms** |
| 3（`7b140e82b39a`） | 1403 ms | **11344 ms** |
| 4（`1c1c26328292`） | 1044 ms | **62559 ms** |
| 5（`6b0067dc075a`） | 1434 ms | **19099 ms** |
| 6（`c415caa18616`） | 1498 ms | **8899 ms** |
| **7（`7b16c7ef15be`。レンダリングあり）** | 9711 ms | **351603 ms** |

- **7 回とも同じ形。** 共通モードの 1 本は 1〜2 秒（重い回で 9.7 秒）で戻ってくるのに、
  **既定モードだけの 1 本は十数秒〜30 秒止まり、レンダリングが入った回は 5 分 52 秒
  止まった**（そのぶんが後でまとめて来る）。
- 上記 6 の「モーダル中は共通モードの 1 本だけが届く」に加えて、**ふつうに触っている
  間でも共通モード（か全モード）への登録が要る**。**既定モードだけで組むと、受け付けが
  分単位で止まる。**

#### 【落とし穴】**コマンドが undo イベントを開いたまま返すと、数十秒開いたままになる**

狙って測ったものではなく、2 回目の計測を汚した事故から出たものだが、**実装に効く**ので残す。
`DeleteObject(h, useUndo=true)` が開けたイベントを**閉じずにメニューコマンドから返した**回では:

- **コマンドが戻っても閉じない。** 書き溜めの `kNotifyUndoEndEvent` が来たのは **53 秒後**で、
  それまで**刻み 181 回すべてが `building=yes`** を読んだ。
- **その間に刻みが作った図形は、その置き土産のイベントに入った**（利用者が 1 回取り消せば
  一緒に消える関係になる）。
- **戻る直前に `EndUndoEvent` で閉じるようにしたら、`building=yes` は 0 件になった**
  （上の表の clean な 4 回）。**閉じるのを処理の途中に 1 度置くだけでは足りない**
  ——その後に開くものがあると残る（6 回目の回で 36 件出た）。

→ [Undo](Undo.md) の「`EndUndoEvent` を呼ばなくても外部の終了時に自動で閉じる」は、
**少なくとも「コマンドが戻った時点では閉じない」**。OS のタイマーを併用する実装では、
**自分が開けたイベントは戻る直前に自分で閉じる**こと。閉じ忘れると、以後の刻みの
書き込みがそのイベントへ流れ込む。

#### 【道具について】**SDK から「描き直して、終わるまで待つ」ことはできない**（#215 で決着）

**実測**（VW 2026 / macOS。`probes/runtime/render-sync-3d/` を 4 回。ビルド
`230f69515fb9` / `c6f9a2cdbc98` / `db5473d03716` / `b9d83f799061`。
[issue #215](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/215)。
プローブは役目を終えたので消してある）。

#213 では 4 つ試してどれも描かせられず、**原因は「2D（上面/平面）ビューのままだった
から」**と見ていた。**3D へ切り替えて測り直した結果、原因はそこではなかった。**

**切り替わったことは機械で押さえてある**——`ISDK::SetProjection(layer,
projectionOrthogonal, false, true)` ＋ VectorScript の `SetView(-60,0,30,0,0,0)` の後、
`GetProjection(layer)` が `projectionOrthogonal`(0) を返す（`projectionPlan` は **6**）。
**`GetProjection(layer) != 6` が「上面/平面を出た」の唯一の機械判定**で、#213 に
欠けていたのはこの 1 行である。図面には球を 196 個（画面に見えている範囲の 60% に
収まる大きさ・位置。`GetViewCenter` ＋ `ViewPt2WorldPt` で割り出す）置いてある。

##### 「戻るまでの時間」で測ってはいけない

最初は所要時間で測ろうとして**2 度しくじった**。決め手は利用者のスクリーンショットで、
プローブが戻った後もステータスバーに **「Viewport-1 の更新 0:16」**（16 秒経過）が
出ていた——`UpdateViewport` は 283 ms で戻っているのに、**VW はその後も描き続けて
いた**。**＝「戻るまでの時間」は描画の量を測っていない。**

正しい道具は上記 4 節と同じ——**ランループを短い刻みで回し、1 周が伸びるかを見る**。
伸びれば、その仕事は**呼び出しの後、イベントループで**行われている。

| 経路（**すべて 3D のビュー・球 196 個**） | 呼び出しが戻るまで | **戻った後にランループを回して吸われた時間** |
| --- | --- | --- |
| `SetRenderMode(layer, renderFinalRenderWorks, immediate=true, doProgress=true)` | **0 ms** | **0 ms**（5002 ms 回して **1 周の最長 53 ms**・100 ms 超の周 **0 回**） |
| 中身を入れたビューポートの `UpdateViewport` | 355 ms（初回）/ 68 ms（2 度目） | **545 ms**（2563 ms 回して **1 周の最長 301 ms**・100 ms 超の周 **3 回**） |

**同じ 1 回の実行・同じ道具で取った対照なので、取り違えようがない。**

**回数は正直に書いておく**——「0 ms で戻る」は **4 回**（32 呼び出し）の再現だが、
**「戻った後にランループを回しても何も出てこない」はこの 1 回だけ**である
（ランループで測る道具は 4 回目で入れた）。ただし**同じ実行の中で `UpdateViewport`
の側は 545 ms 拾えている**ので、「道具が仕事を拾えないのでは」という疑いは潰れている。

**測っていないこと**: **コマンドが終わって VW が自分のイベントループへ戻ってから
描くのかどうか**は、プローブの中からは追えないので測っていない。
**プラグインの用途——「描き直して、終わるまで待つ」——には、どちらでも答えは変わらない。**
（「戻った後に VW が何をするか」は [#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)
で一部を測った——**ランループで連続寸法が作り直され、undo イベントの終わりで PIO の外接と
ビューポートの dirty の印が更新される**。断面の中身は戻った後も変わらなかった。
[Investigation Techniques](Investigation%20Techniques.md)「いつ読むか」。）

##### 1. `SetRenderMode` は **`immediate=true` でも描かない**——先送りですらない

- **3D でも 2D でも 0 ms で戻る。** 4 つのモード（`renderOpenGL` / `renderFinalShaded` /
  `renderFinalHiddenLine` / `renderFinalRenderWorks`）× 2 つの投影（3D / 上面平面）を
  4 回ずつ測って、**32 回すべて 0 ms**。**3D か 2D かで何ひとつ変わらない。**
- **戻った後にランループを 5 秒ぶん回しても、VW は何もしない**（1 周の最長 53 ms）。
  **同じ実行の中で `UpdateViewport` の後は 1 周が 301 ms まで伸びている**ので、
  道具が仕事を拾えないのではない。**この経路では、そもそも描画が起きていない。**
- → **ヘッダのコメントは実際と合わない**（`APIBase.Legacy.Defs.h:4225`。"If immediate
  is true, then all rendering will take place before the call returns."）。かといって
  次の文の「背景で描けるぶんはメインのイベントループへ入り直すまで先送りされる」でも
  ない——**こちらがイベントループを回してやっても、何も出てこない。**
  **この口がするのはモードを書くことだけだと思ってよい。**
- **戻り値はモードによって違う。** `renderFinalShaded`(5) だけ **`true`**、
  `renderOpenGL`(11) / `renderFinalHiddenLine`(6) / `renderFinalRenderWorks`(14) は
  **`false`**。**3D でも 2D でも、4 回とも同じ組み合わせ**だった。
  **どちらでもモードは変わっている**（直後の `GetRenderMode` が狙った値を返す）ので、
  **`false` を失敗と読んではいけない**（#213 から変わらず）。`true` を成功と読むのも
  同じく誤りで、**戻り値からは何も分からない**。
- **`SetProjection` も 0 ms で戻る**（ヘッダは "a long re-render … as a result of the
  projection change" と書いているが、描画は起きない）。VectorScript の `SetView` は
  1〜5 ms。

##### 2. `UpdateViewport` は描く——が、**戻った時点では描き終わっていない**

- **中身は入る。** 表示レイヤを入れれば（`SetViewportLayerVisibility(vp, layer, 0)`）、
  更新後のキャッシュ群（`kViewportGroupCache` = **3**）に **2 件**・外接
  `左-1341.3 上1341.3 右1323.6 下-1323.6` が入る。**空枠を描いていたのではない。**
  段取りは [Viewports](Viewports.md) のとおりで、**`SetDirty(true)` と「ワイヤフレーム／
  スケッチ以外の描画種別」が要る**（ヘッダ: "a **dirty** viewport, **whose render type is
  other than wireframe or sketch**, will be re-rendered."）。
  **作りたてのままの `UpdateViewport` は 0 ms で戻り、何も起きない**（#213 の 131 ms は
  再現しなかった）。
- **ただし戻っても終わっていない。** 戻った後にランループを回すと **545 ms ぶん**
  仕事が出てくる（1 周が 301 ms まで伸びる）。利用者の画面では、プローブが戻った後も
  ステータスバーに「Viewport-1 の更新」が**16 秒**出ていた。
- **`IsDirty()` は完了の合図ではない。** 更新の直後に読むと **`no`** を返すのに、
  描画はまだ続いている。**戻り値（`void`）も `IsDirty()` も当てにできない。**

##### 3. 帰結——プラグインから「描き直して、終わるまで待つ」はできない

- **描かせる口は `UpdateViewport`（ビューポート）だけ**で、レイヤの画面描画を
  プログラムから起こす道は無い。
- **終わりを知る道が無い。** 「いま描いているか」を問う口も、レンダリングの開始・終了の
  通知も無い（下記）。`IsDirty()` も戻り値も完了を表さない。
- **唯一の近道は「ランループを回して、1 周が伸びなくなるまで待つ」**——このプローブが
  使った道具そのものである。**信号ではなく当て推量**なので、「静かになったら終わった
  ことにする」以上のことは言えない。待つ側は締切を必ず持つこと。
- **刻み（OS のタイマー）との関係**: `SetRenderMode` の呼び出し中も `UpdateViewport` の
  呼び出し中も、250 ms のタイマーは **0 回**（VW は入れ子のランループを回さない）。
  **利用者がメニューから描かせたとき**は `NSModalPanelRunLoopMode` で 250 ms ちょうど
  届く（上記の 1427 回）——**SDK から起こした描画とは別物である。**
  `UpdateViewport` の**先送りされたぶん**は普通のランループで走るので、そこでは届く。

##### 効かなかった口（測ったので残す）

- **`VWLayerObj::SetSheetPrintDPI(300)` は効かない**——書いた直後の
  `GetSheetPrintDPI()` が **72** を返す（シートレイヤに対して。2 回再現）。
- **`VWViewportObj::SetScale()` の数が大きいほど絵は大きくなる。** 100 → 50 にしたら
  キャッシュ群の外接が ±2682.5 → ±1341.3 と**半分**になった（縮尺の分母のつもりで
  小さくすると、絵も小さくなる）。

**「いま描いているか」を問う口と、レンダリングの開始・終了の通知は無い**:

- `ISDK` を `Render` で引いて出るのは `GetRenderMode` / `SetRenderMode` /
  `RefreshRenderingForSelectedObjects` ほか数本だけ。
- `MCNotification.h` 全体を読んだ——`kNotifyBeforePendingUpdate` /
  `kNotifyAfterPendingUpdate` は**通知の取りまとめ用**（"help optimize pending
  notifications … (From OnIdle)"）、`kNotifyUndrawScrMods` / `kNotifyDrawScrMods` は
  **ツールの画面補助線の消し書き**、`kNotifyRenderModeAboutToChange` / `Changed` は
  **モードの変更**。**描画そのものを囲む通知は無い。**
- しかも **`kNotifyRenderModeAboutToChange` / `Changed` は、SDK からの `SetRenderMode`
  では飛ばない**（11 → 14 の変更で 0 件。飛ぶのは**利用者がメニューで変えたときだけ**）。
  **プログラムからの変更の目印には使えない。**
- → **レンダリング中を測るなら、利用者にメニューから描かせて刻みの `mode=` で読み取る**
  （`NSModalPanelRunLoopMode` が印になる）。上の数字はその道で取ったものである。

### 8. **図面が 1 枚も開いていないとき**の `GetCurrentLayer()`——**nullptr が返り、落ちない**（件数と常に一致）

**実測**（VW 2026 / macOS arm64。`probes/runtime/no-doc-current-layer/` をローカルの
Claude Code から MCP ブリッジで回した。ビルド `26b6337dfcc7` / `4417e0f207fd` /
`3f9588499d8c`。**Windows も実測**——利用者がメニューから同じ手順（走らせる → 全部閉じる →
新規を開く → 走らせる）で回した。ビルド `04eb2b960ca0`。
[issue #224](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/224)）。

上記 1 で「図面が無くても刻みは続き、`GetOpenFilesList()` などは落ちずに『文書が無い』を
返す」ことは分かっていたが、**`GetCurrentLayer()` を図面の無い状態で呼んだ記録は無かった**。
20 ms の刻み（`kCFRunLoopCommonModes`）から、毎回 `GetOpenFilesList()` の件数 →
`GetCurrentLayer()` →（ハンドルがあれば）`GetObjectName` / `GetObjectTypeN` を読み、
図面あり → 全部閉じる → 新規を開く を通して並べた。

| 局面 | 件数 | `GetCurrentLayer()` | mac の刻み（3 回） | Windows の刻み（3 回） |
| --- | --- | --- | --- | --- |
| 図面あり | 1 | ハンドル（名前「レイヤ-1」・型 31 ＝レイヤ） | 22 ＋ 11167 ＋ 402 | 714 ＋ 686 ＋ 33474 |
| **全部閉じた後** | **0** | **`nullptr`** | 278 ＋ 283 ＋ 498 | 336 ＋ 314 ＋ 0 |
| 新規を開いた後 | 1 | ハンドル（同上） | （上の「図面あり」に含む） | （同左） |
| 件数 0 なのにハンドル／件数 1 以上なのに `nullptr` | — | — | **0 回** | **0 回** |

（Windows の 3 回目は閉じずに 20 分置いて上限で止まった回。）

- **落ちない。** 件数が 0 になった最初の刻みから `nullptr` が返り、以後ずっと `nullptr`。
  例外も異常終了も無い（落ちれば最後に残るよう、件数が変わった刻みでは呼ぶ前に 1 行
  書いていた）。
- **起動直後（一度も図面を開いていない状態）も `nullptr`。** 再起動した直後、図面 0 枚の
  まま、殻のブリッジの刻み（`kCFRunLoopDefaultMode` の `CFRunLoopTimer`）から
  `GetCurrentLayer()` を呼ぶと `nullptr`、件数も 0 で、落ちなかった（`vw_ping` の
  `document_open=false` / `open_documents=0`。図面なしの `vw_run_probe` も同じ判定で
  断られた）。**閉じた後と起動直後で振る舞いは変わらない。**
  **殻の時計の最初の刻みからも測った（mac / Windows）。** プローブは図面が無いと走らせ
  られず（メニューは `DocIsActive`）、ブリッジ（MCP サーバ）は mac のローカルにしか無いので、
  殻に「頼み札があるときだけ、起動直後の刻みから件数と `GetCurrentLayer()` を書く」記録を
  足して測った（[PR #228](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/228)。
  測るための道具で、**後述の Windows の起動時クラッシュの切り分けのため #231 で戻した**——
  Windows は同じ記録を持つ診断ビルド
  [#232](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/232) で測った）。

  | | 最初の刻み | 図面 0 枚の間 | 図面を開いた後 |
  | --- | --- | --- | --- |
  | mac（ビルド `b11ecfd3c565`） | t=0 ms | **58 刻み（30 秒）すべて `nullptr`** | 20 刻みすべてハンドル（「レイヤ-1」・型 31） |
  | Windows（殻 `701945345747`） | t=2 ms | **560 刻み（3 分）すべて `nullptr`** | （開く前に記録の上限 3 分で終わった） |

  **どちらも食い違い 0・落ちない。** モジュールが読み込まれた直後の刻み——起動画面が
  出ている頃——から呼んでも `nullptr` が返るだけだった。
  （**殻の差し替えそのもので Windows の VW が起動時に落ちる**事故を、この測定の途中で
  踏んだ。原因は記録のコードではなく、**起動時の読み込み直しで古い番地を指したまま残った
  殻の時計**だった——[issue #233](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/233)。
  当初はプラグインライブラリ関数の書き出しのせいと見ていたが誤り。[プラグインモジュール](Plug-in%20Modules.md)
  「【罠】Windows の VW は起動時にモジュールを一度降ろして読み込み直す」。）
- **件数と `nullptr` かどうかは、刻みごとに常に一致した**（mac 1 万 2 千回余り・Windows
  3 万 5 千回余りで食い違い 0 回）。**閉じる途中・開く途中の刻みは 1 つも来なかった**——`CloseDocument` /
  `OpenDocumentPath` はブリッジの刻みの中で同期的に終わり、その間に入れ子の刻みは
  配られていない。**だから「中途半端な状態」を刻みが見る場面は、少なくともこの道では
  現れない。** **利用者が手で閉じて・開いた Windows の 2 周でも同じ**で、件数 1 → 0 →
  1 と切り替わる刻みのどれにも食い違いは無かった（mac の、人が UI から閉じる道は測っていない
  が、Windows で人の道・mac でブリッジの道が揃ったので、ここで止める）。
- **【注意】別の文書のレイヤが同じアドレスで返る。** 閉じた文書のレイヤ
  `0x7904530380` と、後で新規に開いた文書のレイヤが**同じ値**だった（別の回でも再現）。
  **Windows でも同じ**——`000001FE552347F0` / `000001FE55232AB0` がそれぞれ別の文書の
  レイヤとして 2 度ずつ返った。
  **ハンドルを刻みをまたいで持ち越すと、閉じた後に別の文書のレイヤを指しうる**——
  刻みのたびに `GetCurrentLayer()` を引き直すこと。

**帰結（使い道）:** 刻みの中で「図面が開いているか」を判定するのに、
`GetCurrentLayer() != nullptr` と `GetOpenFilesList()` の件数のどちらを使っても答えは
同じで、**図面が無いときに `GetCurrentLayer()` を呼ぶのを避ける必要は無い**（mac / Windows）。
殻のブリッジ（`plugin/src/McpBridge.cpp` の `PrepareRun` / `vw_ping`）も前者で判定して
いる。安上がりなのは `GetCurrentLayer()`（配列を作らない）。

**ついでに見えたこと（Windows）【推定】:** 報告の実行のとき、書き溜めの心拍が
**9.4 秒・9.9 秒古かった**（ふだんの間隔は最大 1.7 秒）。図面を開いてからメニューで
プローブを選び終えるまでのどこかで、**`SetTimer` の刻みが 10 秒近く止まっていた**ことになる。
どの局面で止まったか（メニューの追跡中か・ピッカーのダイアログ中か）は記録していない。
殻の McpBridge.h は「Windows の WM_TIMER はモーダルダイアログの中でも配られる」前提で
ピッカー中の受け付けを見送っており、上記 4 の ②（`AlertQuestion`）でも刻んだので、
**止まった場所は未特定**のまま置く。

### 9. 刻みから **`CloseAllFilesAndQuitVectorworks(true, false)`** を呼ぶ——**呼べる。呼び出しは戻り、終了はその後のイベントループで起きる**

**実測**（VW 2026。`probes/runtime/quit-from-timer/`。**mac**（arm64）はローカルの Claude Code
から MCP ブリッジで回し、保存の確認のボタンだけ人が押した（ビルド `aeec0b4414f4` /
`ce0611a827d7` / `e0331468016c` / `4ac3163369f7`）。**Windows** は利用者がメニューから同じ
5 段を回した（ビルド `0522e32b6127`。仮想環境で、メモリの割り当てが少なかった——時間の
数値は膨らんでいるかもしれない）。
[issue #230](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/230)）。
**mac と Windows で、刻みの振る舞いがはっきり違う**（下記「Windows」）。

Vectorworks が終わるので、報告は返らない。**刻みが 1 行ずつ一時ファイルへ書き（書くたびに
閉じる）、起動し直した後に読んだ。** タイマーは 2 本——終了を呼ぶ「駆動」（100 ms）と、
状態を読む「観測」（50 ms）。どちらも `kCFRunLoopCommonModes`。あわせて
`kNotifyBeforeApplicationQuit`（`'BApQ'`）・`kNotifyDocClose`（`'DCLS'`）・`std::atexit` を
記録した。図面は新規の 1 枚だけ。

| 場面 | 保存の確認 | 呼び出しの所要 | 戻った後 | 終了 |
| --- | --- | --- | --- | --- |
| 未保存の変更あり → 人が「キャンセル」 | **出た** | 17787 ms（人の操作込み） | **戻る。図面 1 枚はそのまま**、刻みは続き `gSDK` も読める（4 秒で 78 刻み） | しない |
| 同上 → 確認の最中にもう一度呼び、両方「キャンセル」 | **2 つ重なって出た**（人の目視） | 内側 172558 ms・外側 178267 ms | 両方戻る。図面はそのまま | しない |
| 未保存の変更あり → 人が「保存しない」 | 出た | 22635 ms（人の操作込み。`DocClose` の 158 ms 後に戻る） | 戻る。図面 0 枚・`GetCurrentLayer()` は `nullptr` | **戻ってから 4.7 秒後に `atexit`** |
| 保存済み（未保存の変更なし） | **出ない** | **129 / 132 / 196 ms**（3 回） | 同上 | 戻ってから 3.9 / 7.6 / 4.3 秒後に `atexit` |
| 図面 0 枚（保存して `CloseDocument()` で閉じた後） | 出ない | **115 / 99 ms**（2 回） | 同上 | 戻ってから 4.3 / 5.3 秒後に `atexit` |

（上は mac。Windows は下記の表。）

- **落ちない。** mac で 6 回・Windows で 3 回終了させて、すべて `atexit` まで届いた——**C ランタイムの `exit()`
  を通る正常な終了**である。
- **呼び出しはプロセスを終わらせずに戻る。終了は、戻った後のイベントループで起きる**
  （問い 3）。戻るのは**図面を閉じ終えた後**（`DocClose` → 戻る）で、そこから
  **約 4〜8 秒**後にプロセスが終わる。**＝「応答を書いてから呼ぶ」順序で足りる**し、
  呼び出しから戻った後に何かを書いても間に合う。
- **mac では、戻った後も刻みは届き、`gSDK` を読めた**（問い 5）。終了する回では、戻った後に
  **約 2 秒空いて**（`max_gap_ms` 2128〜2754）`NSEventTrackingRunLoopMode` で刻みが
  1〜数回届き、`GetOpenFilesList()`（0 件）・`IsCurrentlyBuildingAnUndoEvent()`（`no`）・
  `GetCurrentLayer()`（`nullptr`）がどれも落ちずに返った。**最後の刻みから `atexit` まで
  1.3〜2.0 秒**は刻みが来ない。**タイマーを止めなくても、この 4 回では何も起きなかった。**
- **保存の確認は「未保存の変更」があるときだけ出る。** 刻みの中で `CreateLine` しただけでは
  **確認が出ずに終わった**（最初の版で踏んだ）。`MarkCurrentDocChanged()` を足すと出た。
  **刻みの中で（undo イベントの外で）図面に書いても、文書は「変更あり」にならない**
  ——上記 4 の 1 の「書ける」は、
  保存の確認に引っ掛からないという意味でもある（[Documents](Documents.md)「未保存の変更ありは
  読めない」で、保存済みの図面に刻みの中から書いて「閉じる」の確認が出ないことを確かめた。#240）。
  なお、ここで使った「新規の図面」は**ブリッジが実行のたびに一時ファイルへ保存していた**ので、
  実際には保存済みの図面だった【推定】——**無題のままの図面は、何もしていなくても確認を出す**
  （同上）。

#### 保存の確認の最中（問い 4）

- **観測のタイマーは刻み続けた**（17.8 秒で 353 刻み、間隔は最大 164 ms）。モードは
  **`NSModalPanelRunLoopMode`**。図面 1 枚・`GetCurrentLayer()` は読めた。
- **`IsCurrentlyBuildingAnUndoEvent()` は `no`**——`AlertQuestion` の最中
  （上記 4 の 6 は `yes`）とは違う。保存の確認は undo イベントを開いたまま出るものではない。
- **mac では、終了を呼んだタイマー自身は呼び出しの最中に 1 度も入り直さなかった**（駆動の
  入り直し 0 回・全 5 回）。**`CFRunLoopTimer` は自分の callout の最中に同じタイマーを
  発火しない**。
- **Windows では入り直す。** 確認の最中、終了を呼んだ同じ `SetTimer` の刻みが
  **35〜86 回入り直した**（確認ダイアログのメッセージループが `WM_TIMER` をそのまま配る）。
  段 1 では**その入り直した刻みから 2 度目を呼べてしまった**（呼び出しの 1.5 秒後）。
  ＝**「1 本のタイマーの中で呼ぶから入れ子は起きない」は mac でしか成り立たない。**
- **2 度目を呼ぶと、保存の確認が 2 つ重なって出る**（mac・Windows とも人の目視）。落ちは
  せず、両方キャンセルすると内側 → 外側の順に戻った。**終了を頼まれる作りなら、
  「終了の呼び出しの最中」を旗で持って、入れ子の要求を見送る**（Windows では必須）。
  `IsCurrentlyBuildingAnUndoEvent()` は確認の最中も `no`（mac・Windows とも）なので、
  この判定には使えない。

#### 終了を知らせる通知（問い 5 の後半）

- **`kNotifyBeforeApplicationQuit` は「終わる」合図にならない。** 呼び出しと**同じミリ秒**に、
  **保存の確認より前に**届き、**キャンセルされて終わらなかった回にも届いた**（2 度呼べば
  2 回届く）。SDK のコメントは「Sent immediately before the application quits」
  （`MCNotification.h:274`）だが、実際は「終了の手続きに入った」である。これを見て
  タイマーを止めると、キャンセルされたときに止まったままになる。
- **`kNotifyDocClose` は図面が閉じた時点で届く**（呼び出しの中・戻る直前）。図面 0 枚なら
  届かない。
- **`plugin_module_main` に `kPluginModuleDeinit`（-2）が届く——刻みが止まった後に 1 回**
  （mac は `atexit` の約 0.1 秒前、Windows は約 20 秒前。Windows は下記の表）。殻に action を時刻付きで書かせて（`plugin/src/ModuleMain.cpp`。
  頼み札があるときだけ書く）、刻み・`atexit` と突き合わせた（ビルド `e0331468016c` /
  `4ac3163369f7`）:

  | 回 | 呼び出しから戻る → 最後の刻み | 最後の刻み → **-2** | **-2** → `atexit` |
  | --- | --- | --- | --- |
  | 保存済みの図面 1 枚 | 6.2 秒 | **1.21 秒** | **0.12 秒** |
  | 図面 0 枚 | 3.8 秒 | **1.42 秒** | **0.10 秒** |

  - **-2 の後に刻みは 1 度も来ていない。** 刻みが止まるのが先（イベントループを抜けた）で、
    -2 はその後、プロセスが `exit()` へ入る直前に来る。-2 の直後に -5（インタフェースの解放）
    が 1 回続く。起動では -1 が 1 回、その後に -3 / -4 / -5 が 8 回。
  - **だから -2 は「タイマーを止める入口」として遅すぎも早すぎもしない**——キャンセルされた
    回には来ず（終わらないので）、来た時点ではもう刻みは来ない。止めるなら -2 で止めればよいが、
    **止めなくても何も起きなかった**（上記）。
  - SDK 側がこの値でしていることは、`MiniCadCallBacks.cpp:256` の
    `GS_ReleaseStandardVCOMGlobals` で `gIFC` などのグローバルを手放すことだけ
    【ソース根拠】。**-2 の中で `gSDK` を呼べるかは測っていない**（殻は呼ばずに書くだけ）。
  - **-2 は VW が読み込んだモジュール（殻）にしか届かない。** 殻が自分で読み込む本体には
    VW から `plugin_module_main` は呼ばれない。

#### Windows

| 場面 | 保存の確認 | 呼び出しの所要 | 確認の最中の刻み | 戻った後の刻み | 戻る → **-2** → `atexit` |
| --- | --- | --- | --- | --- | --- |
| キャンセル | 出た | 7953 ms（人込み） | 観測 120・**駆動の入り直し 70** | 4 秒で 61 回。図面 1 枚・`gSDK` 読める | 終わらない |
| 2 度目を呼んで両方キャンセル | **2 つ出た**（目視） | 内側 7087・外側 9746 ms | 駆動の入り直し 86（そこから 2 度目） | 図面 1 枚 | 終わらない |
| 保存しない | 出た | 5304 ms（`DocClose` の **1364 ms 後**に戻る） | 観測 60・入り直し 35 | **0 回** | 9.7 秒 → 20.5 秒（計 30.2 秒） |
| 保存済み | 出ない | **1287 ms**（`DocClose` の 1245 ms 後） | — | **0 回** | 8.0 秒 → 20.0 秒（計 28.0 秒） |
| 図面 0 枚 | 出ない | **711 ms** | — | **0 回** | 8.0 秒 → 20.1 秒（計 28.1 秒） |

- **終わっていく回では、呼び出しの後に刻みが 1 回も来ない**（観測も駆動も。最後の刻みは
  呼び出しの直前か確認ダイアログの中）。mac のように「戻った後に刻みが来て `gSDK` を読む」
  場面そのものが無い。**＝ Windows では終了の途中で `gSDK` を呼ぶ心配は無い。**
- **呼び出しは mac より遅く戻り（0.7〜1.3 秒）、プロセスはその後 28〜30 秒生きている。**
  -2 は戻ってから 8〜10 秒後、`atexit` は -2 の約 20 秒後。**呼ぶ側が「プロセスが終わって
  ロックが放される」ことで見届けるなら、30 秒以上待つ前提にする**（仮想環境での値）。
- **利用者が閉じる道（メニューの「終了」＝`CloseMainWindow`）でも並びは同じ**——閉じる指示の
  7〜14 秒後に -2、その 19〜21 秒後にプロセスが終わる（3 回。issue #257。
  [Plug-in Modules](Plug-in%20Modules.md)「OS タイマーを持つ常駐プラグイン…は、終了時の
  落ち方を変えない」）。タイマーを止めない常駐プラグインが入っていても、閉じたときの
  終わり方は変わらなかった（同じ節。82 回）。刻みがいつまで来るかは下の「利用者が閉じる道の
  刻み」。
- `kNotifyBeforeApplicationQuit` が確認より前に届き、キャンセルされても届くのは mac と同じ。
- 確認の最中の `IsCurrentlyBuildingAnUndoEvent()` は `no`（mac と同じ）。
- この間、利用者の側で**起動時のクラッシュが何度かあった**。当時は「古いインスタンスが
  終わる前に起動し直した」形を疑ったが、**その後の調査（issue #253）で、殻のタイマーが
  起動時の読み込み直しで古い番地を指したまま残り、起動中に落ちることが分かった**
  （[Plug-in Modules](Plug-in%20Modules.md)「【罠】Windows の VW は起動時にモジュールを一度
  降ろして読み込み直す」。殻を留めて直した）。【推定】この節の測定中のクラッシュも同じもの
  （当時の回は記録が無く、突き合わせられない）。

#### 利用者が閉じる道の刻み（Windows）

**実測**（VW 2026 Update 6・Windows 11 ARM64 の VM の x64 エミュレーション。
`probes/runtime/user-quit-ticks/`。ビルド `f829e3869070`。
[issue #269](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/269)）。
プローブが `SetTimer(nullptr, 0, 50, …)` を仕掛け（本体は留める）、刻みごとに壁時計の
ミリ秒を 1 行書いて閉じる。`kNotifyBeforeApplicationQuit`・`kNotifyDocClose`・`atexit` と、
殻の action の記録（-2・-5）も同じ時計で書く。走らせたのは MCP ブリッジ（殻の刻み）から。
ハーネスが VW の主窓へ `WM_CLOSE` を送り（メニューの「終了」・`CloseMainWindow` と同じ）、
プロセスが終わるのを 100 ms ごとに見た。刻みの中で `gSDK` を読む回（`GetOpenFilesList`・
`IsCurrentlyBuildingAnUndoEvent`）と読まない回、図面 1 枚（ブリッジが保存済み）と 0 枚
（ブリッジの図面を閉じてホーム画面へ戻した後）を振った。時刻は閉じる指示からの ms。

| 回 | 図面 | `gSDK` | 閉じた後の刻み | `BApQ` | -2 | `atexit` | 終了 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 1 枚 | 読む | 2 回（+63 まで） | +74 | +58573 | +92568 | （測れず） |
| 2 | 1 枚 | 読む | 0（※） | +185 | +40895 | +86424 | +86424 |
| 3 | 1 枚 | 読む | 0 | +8 | +9465 | +29249 | +29277 |
| 4 | 1 枚 | 読む | 0 | +8 | +7572 | +27219 | +27295 |
| 5 | 1 枚 | 読まない | 0 | +7 | +9226 | +29059 | +29068 |
| 6 | 1 枚 | 読まない | 1 回（+4） | +15 | +8593 | +28392 | +28423 |
| 7 | 0 枚 | 読む | 0 | +17 | +8454 | **無し** | +44529（**`0xC0000005`**。下記） |
| 8 | 0 枚 | 読む | 0 | +7 | +7315 | +26829 | +26853 |
| 9 | 0 枚 | 読む | 0 | +11 | +7828 | +27480 | +27561 |
| 10 | 0 枚 | 読まない | 0 | +10 | +7304 | +26855 | +26922 |
| 11 | 0 枚 | 読まない | 0 | +3 | +7391 | +26668 | +26661 |

- **`BApQ` の後に刻みは 1 度も来ない**（11 回とも）。閉じる指示の後に来た刻みは 3 回だけで、
  どれも `BApQ` より前（閉じる指示がまだキューにあった数 ms〜63 ms）。最後の刻みから
  `BApQ` まで 11〜73 ms。**刻みから終了を呼ぶ道（上の表）と同じく、閉じていく途中に刻みが
  当たって `gSDK` を呼ぶ場面は無い**——タイマーを止めておく必要も無い。
- 並びは **閉じる指示 → （数 ms）`BApQ` → `DocClose`（図面があれば +46〜+5921 ms）→
  -2（+7.3〜9.5 秒）→ -5（-2 の約 0.6 秒後）→ `atexit`（-2 の約 19〜20 秒後）→ 終了
  （`atexit` の 0〜80 ms 後）**。1・2 回目だけ -2 が 41〜59 秒と遅いのは、VM の起動が
  遅かった回（ホーム画面まで 287〜320 秒。ほかは 68〜105 秒）。
- 図面 0 枚の回は、`BApQ` の時点で図面はもう無く `DocClose` は来ない（表の時刻はブリッジが
  閉じたときのもの）。刻みの中の `gSDK` は 0 枚でも落ちずに読めた（`docs=0`）。
- ※ 2 回目は、仕掛けてから閉じるまでの 19 秒に**プローブの刻みが 1 度も来なかった**
  （`SetTimer` は成功し、`BApQ` 以降の記録は残った）。ほかの 10 回は仕掛けて 1.3〜9.9 秒で
  刻み始めた（0 枚の回はブリッジが図面を閉じる間を挟むので 4.4 秒以上）。原因は分かって
  いない（起動が遅かった回）。
- **7 回目は `atexit` に届かず、`libcef.dll_unloaded` の中の `0xC0000005` で落ちた**
  （Windows の異常終了記録・WER のフルダンプ。Sentry には無し）。-2・-5 は届いており、
  落ちたのは -2 の 36 秒後。プローブの刻みは `BApQ` 以降 0 回なので、刻みが落としたもの
  ではない。#257 で 82 回再現しなかった `cli` の終了時の落ち方と同じモジュール
  （[Plug-in Modules](Plug-in%20Modules.md)「OS タイマーを持つ常駐プラグイン…」）。
  この回は**ブリッジが図面を閉じてホーム画面へ戻った後**に閉じていた（#257 は起動して
  ホーム画面から閉じただけ）。同じ形の 0 枚の回は 5 回中 1 回。

**線引き（mac / Windows）**: 刻みから終了を呼んでよい。呼び出しは戻るので、応答はその前でも
後でも書ける。キャンセルされたら何も変わらず動き続ける。**「終了の呼び出しの最中」を旗で
持ち、その間の要求は見送る**（Windows では同じタイマーが入り直すので必須）。終了の途中で
`gSDK` を読んでも落ちなかった（mac）か、そもそも刻みが来なかった（Windows）ので、タイマーを
止める必要は無い。止めたいなら `plugin_module_main` の `kPluginModuleDeinit` で止める
——`BApQ` で止めるのは誤り。呼ぶ側はプロセスの終了を **30 秒以上**待つ（Windows）。

## 5. ウェブパレットの JS タイマーは、隠れると 60 秒に 1 回まで間引かれる

**実測**（VW 2026 / macOS arm64。[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
の PR #192 / #194 の実機確認。出所は
[issue #204](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/204)）:

- **パレットを閉じた（隠した）とき、または Vectorworks がほかのアプリの裏に回ったとき、
  JS の `setInterval`（250 ms）がちょうど 60 秒に 1 回まで間引かれる。** パレットを出し直すと
  すぐ 250 ms ごとに戻る。
- `NSProcessInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep` で
  **App Nap から外しても変わらない**（＝ OS の省電力ではなく、埋め込みブラウザ側の間引き）。
- 埋め込みブラウザは **Chromium Embedded Framework**（`Vectorworks Web Helper` が
  `--type=gpu-process` 等で走る。SDK のヘルプにも
  `<user folder>/Plug-ins/**ChromiumEF**/WebBrowser_Console_Output.txt` という道が出てくる）。
  60 秒という刻みは Chromium が隠れたページのタイマーを 1 分へ寄せる
  intensive wake-up throttling と一致する。

### SDK に間引きを止める口は無い

- **`IWebPaletteFrame` が持つのは `Reload` / `LoadURL` / `ExecuteJavaScript` / `SetTitle` /
  `Focus` の 5 つだけ**（`IExtensionWebPalette.h:37-42`）。間引き・省電力・バックグラウンド
  動作に関わる設定は 1 つも無い。
- `IWebCallbacksProvider`（`OnInit` / `OnGetRequestHeaders` / `OnBeforeOpenLink` /
  `OnBeforeDownload` / `OnRequestResponse` / `OnKeyDown` / `OnDragFiles` /
  `DisableContextMenu`）にも、`IWebJavaScriptProvider`（`AddVariable` / `AddFunction*` /
  `AddExecute` / `OnFunction`）にも無い。
- **SDK 全体（ヘッダ＋同梱の実装ソース＋`vs.py`）で CEF に触れる口は
  `WebDlgEnableConsole(enable)` ただ 1 つ**で、これはコンソールログをファイルへ出すだけ。
  CEF のコマンドラインスイッチ（`--disable-background-timer-throttling` など）を
  プラグインから足す口は無い。`varTestWebPalette`(6793) は「Internal」と注記された
  書き込み専用の Boolean。
- **したがって手当ては「パレットの外に時計を置く」しかない。そしてそれは成り立つ**
  ——OS のタイマーなら間引かれず、文書が無くても刻む（上記 4 で実測）。【ソース根拠】

### 代わりに使える 2 つの口（どちらも SDK 側にある）

- **`IWebJavaScriptProvider::OnPaletteVisibilityChange(bool visible, IWebPaletteFrame* frame)`**
  ——**隠れた／出たことが C++ 側で分かる**（`IExtensionWebPalette.h:144`）。
  「隠れている間は JS を当てにしない」と切り替える足場になる。【ヘッダ根拠】
- **`IWebPaletteFrame::ExecuteJavaScript(code)`** ——C++ から JS を叩ける。
  **時計を C++ 側へ移せば、間引かれたページを C++ から起こす形にできる**
  （`ExecuteJavaScript(code, onCompleteCallback)` の方は、SDK のコメントに
  「callback is executed **outside UI thread**」とあるので、そこから `gSDK` を触らない）。【ヘッダ根拠】

## 6. 図面が 1 枚も開いていないと、ウェブパレットは出ない

**実測**（VW 2026 / macOS arm64。出所は上と同じ issue #204）:

- **Vectorworks を再起動した直後、図面が 1 枚も開いていない画面ではパレットが出ない**
  ——メニューから `gSDK->SetWebPaletteVisibility(iid, true)` を呼んでも出ず、JS タイマーは
  一度も動かない。**図面を新規作成して編集画面になると、パレットは開いた状態で現れる。**
- **SDK 側に出す手立ては無い。** パレットに触れる口は
  `GetWebPaletteVisibility` / `SetWebPaletteVisibility`（`ISDK.h:3543-3544`）と
  `GetWebPaletteFrame`（`ISDK.h:3800`）の 3 つで、どれも引数は `VWIID` 1 つ
  （＋ `bool`）。文書が無いときの扱いを変える引数も設定も無い。【ヘッダ根拠】
- **拡張は「文書があること」を前提にしている**——メニュー拡張も `SMenuDef` の
  `Needs = EMenuEnableFlags::DocIsActive` を立てれば文書が無い間はグレーアウトする
  （実機確認プラグインで実測。`plugin/src/ProbeMenu.cpp`）。VW 側がパレットを
  文書の窓に紐付けていると見るのが自然だが、**それが仕様として明記された文書は無い**。
- したがって**「文書が開いていない間はウェブパレットは動かない」前提で設計する**。
  文書が開いたことを知りたいなら `kNotifyDocOpen`（上記 2）で拾う。【ヘッダ根拠】
- **ただし受け付けそのものは、文書が無くても動かせる。** OS のタイマーは**図面が 1 枚も
  開いていない状態でも刻み続け**、`gSDK` は落ちずに「文書が無い」を返した（上記 4 の 1。
  実測）。**＝起動直後の取りこぼしを無くしたいなら、時計をパレットから OS のタイマーへ
  移すのがそのまま答えになる。**

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
