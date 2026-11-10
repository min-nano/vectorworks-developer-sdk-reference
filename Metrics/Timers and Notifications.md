# 周期実行と通知（タイマー・アイドル・`kNotify`） — 計測記録

結論は [Findings「Timers and Notifications」](../Findings/Timers%20and%20Notifications.md)。
ここには SDK のヘッダ・ソースを引いた結果と、実機のプローブで測った環境・手順・実測の表・
ログ・切り分けの経緯を置く。節の番号は Findings と同じにしてある。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

[issue #204](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/204) の調査。
出どころは MCP ブリッジ——外からの要求を拾うために、ウェブパレットの HTML の
`setInterval`（250 ms）から C++ を呼んでいたが、**パレットを隠すと 60 秒に 1 回まで
間引かれる**ことが実機で分かった（下記 5）。
続く問いは issue #205 / #206 / #213 / #215 / #224 / #230 / #235 / #269 で測った
（各節の冒頭に環境とビルドを書く）。

## 1. ISDK に「暇なとき・周期的に」の口を探した（ヘッダ・ソースを読んだ）

`ISDK.h`（VW 2026 SDK）で「登録」「タイマー」「アイドル」に関わる宣言を**全部**並べると
8 本だった。

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

- `RegisterDialogForTimerEvents(dialogID, ms)` はダイアログの ID を取る。VWFC 側の口は
  `VWDialog::RegisterForTimerEvents(ms)` / `DeregisterForTimerEvents()` で、
  受けるのは `VWDialog::OnDialogTimer()`（`Dialog.h:355-356, 400`）。VectorScript にも
  同じものがある（`RegisterDialogForTimerEvents` / `DeregisterDialogFromTimerEvents`）。
  `VWDialog::RunDialogLayout` はモーダル（[Layout Dialogs](Layout%20Dialogs.md)）。
- ウェブパレットの中身は HTML であって `VWDialog` ではなく
  （[Layout Dialogs](../Findings/Layout%20Dialogs.md)「モードレスなパレット」）、
  レイアウトダイアログの ID を持たない。
- `TickCount()` は 1/60 秒単位の時刻を返す。
- `RegisterMenuForCallback` の `whichChange` は下記の `kNotify*`。`RegisterExternal` は
  外部モジュールの登録。

## 2. 通知（`RegisterNotificationProcedure` ＋ `kNotify*`）の宣言を読んだ

```cpp
typedef Sint32  StatusID;                              // Kernel/API/MiniCadCallBacks.h:34-36
typedef SintptrT StatusData;
typedef void (*StatusProcPtr)(StatusID, StatusData);

gSDK->RegisterNotificationProcedure(proc, kNotifyDocOpen);   // ISDK.h:1751
gSDK->UnregisterNotificationProcedure(proc, kNotifyDocOpen); // ISDK.h:1759
```

- **`whichChange` に渡す値は `Include/Kernel/Core/MCNotification.h` の `kNotify*`**（4 文字の
  OSType。VW 2026 SDK に **184 個**ある）。**公式リファレンス（`Info/` / `Versions/`）には
  1 行も載っていない**（`kNotify` で 0 件）。例:
  `kNotifyDocOpen` / `kNotifyDocClose` / `kNotifyDocPostSaved` / `kNotifySelChange` /
  `kNotifyLayerChange` / `kNotifyClassChange` / `kNotifyViewChange` /
  `kNotifyDynamicViewChange`（対話的な 1 フレームごと——コメントに
  「!!CAUTION - check routine performance!!」とある）/ `kNotifyApplicationActivate`
  （アプリが前面に来た）/ `kNotifyBeforeApplicationQuit` / `kNotifyAutosaveDone` ほか。
- `kNotifyBeforePendingUpdate` / `kNotifyAfterPendingUpdate` のコメントには
  「Many notifications are handed out in a loop (**From OnIdle**)」とある。

## 3. ツール拡張の「アイドル」（`kAction_OnIdle`）の宣言を読んだ

```
MiniCadCallBacks.h:6376   kAction_OnIdle = 112,        // ToolMessage::EAction
```

- `kAction_OnIdle` はツール拡張（`GROUPID_ExtensionTool`）の `ToolMessage::EAction` の値。
  `OnIdle` は SDK 全体でこの 1 行と、上記 `MCNotification.h` のコメント 1 行だけで、
  メニュー・PIO・ウェブパレットの拡張には同じものが無い。

## 4. OS のタイマーから `gSDK` を呼んだ実測（mac / Windows）

**実測**（VW 2026。macOS arm64 ＋ Windows の両方。`probes/runtime/runloop-timer-sdk/` を
各プラットフォームで「仕掛ける → 2 分ほど置く → 報告させる」の 2 回に分けて実行。
ビルド `7fbdbc35d939`。以下の数値は実行ログそのまま）。タイマーは mac: `CFRunLoopTimer` を
メインのランループへ / Windows: `SetTimer(nullptr, 0, ms, proc)`。

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

### 1. コマンドが戻った後の刻みと、`gSDK` の読み書き

- **読み**: 刻みから `TickCount()` / `GetOpenFilesList()` / アクティブレイヤの図形数を
  読んで、**mac で 881 回・Windows で 423 回すべて成功、例外は 0 回**。
- **書き**: **こちらのコードがスタックに 1 本も無い刻みから `CreateLocus` が通った**
  ——mac は `write ## src=cf ## locus=作れた ## objs_after=5`（4 → 5）、Windows は
  `objs_after=4`（3 → 4）。
- **mac は 2 回まわした**（③ の刻みが 367/369 回と 343/342 回、**どちらの回も書き込みが
  通った**）。最大の空きは回によって違った（1.5 秒の回と 3.6 秒の回）。
- **図面が 1 枚も開いていなくても刻みは続いた**（ログ末尾の刻みが `docs=0 ## objs=-1`）。
  `gSDK` は落ちずに「文書が無い」を返し、**例外も異常も出ていない**。

### 2. 刻みの間隔の実測

コマンドが戻った後の `dt` は**平均 252〜273 ms**、**最大で 1.5〜4.9 秒空き**
（回によって違う）、**最小は 16〜69 ms**（空いたぶんが後でまとめて来た）。

### 3. こちらの同期処理の最中の刻み（両プラットフォーム）

**ランループを回さずに 3 秒働いた間の刻みは 0 回**だった（mac / Windows とも）。

- **刻んだ場面**: モーダルダイアログが開いている間
  （mac / Windows とも 250 ms 前後）と、**mac の `DoYield` の最中**。
- **`DoYield` の最中**: mac は刻んだが間隔が荒い（平均 500 ms・最大 3 秒）。
  **Windows は 1 回も刻まなかった。**
- **刻みが VW の undo イベント中に当たった例は、この計測では 0 回**
  （`undo 中 0 回`。刻みごとに `IsCurrentlyBuildingAnUndoEvent()` を記録した。
  2 プラットフォーム・計 1304 刻み。ふつうに 2 分触っていた）。
  狙って当てた計測は下記 6（[issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)）。

### 4. プラットフォームごとの実測（スレッド・ランループの回し方）

- **macOS: メニューコマンドはメインスレッドで走った**（`thread=main` /
  `runloop=same-as-main`）。
- **macOS: `CFRunLoopRunInMode` の回し方**（[issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)
  の実機）。当初ここには「**自分で回すことはできない**——即座に戻る」と書いていたが、
  #206 で 1 回の呼び出しの長さを変えて測り直した:

  | 回し方 | 結果 |
  | --- | --- |
  | `CFRunLoopRunInMode(default, 1.5, false)` を **1 回**（上記 ①） | **0 回**・所要 0 秒（即座に戻る） |
  | `CFRunLoopRunInMode(default, 0.6, false)` を **1 回**（#206 の 1 回目） | **0 回** |
  | **`CFRunLoopRunInMode(default, 0.05, false)` を 12 回**（#206 の 2・3 回目） | **8 回届いた**。戻り値 **3 ＝ `kCFRunLoopRunTimedOut`**、実際に費やした時間 **0.611 秒**（0.6 が期待値） |

  2 回の実機で同じ結果（どちらも 8 回）。
- **macOS: 主ランループが知っているモードは 13 個**
  （`kCFRunLoopDefaultMode` / `NSEventTrackingRunLoopMode` / `NSModalPanelRunLoopMode` /
  `NSGraphicsRunLoopMode` / `NSAnimationRunLoopMode` / `CoreDragMode` /
  `com.apple.run-loop-mode.view-bridge.blocks` / `__kCFPasteboardPrivateMode` /
  `AppleEventReplies` / `IMKClient_…` / `com.apple.accessibilityServerIPC` /
  `dockmsg-mode` / `kCFRunLoopCommonModes`）。**アイドル時に実際に刻みが届いたモードは
  `kCFRunLoopDefaultMode` / `NSEventTrackingRunLoopMode` / `NSModalPanelRunLoopMode` /
  `__kCFPasteboardPrivateMode` / `view-bridge.blocks`** で、**`kCFRunLoopCommonModes`
  だけでも届いた**。全モードへ入れるとペーストボード・view-bridge のモードの分も拾えた。
- **macOS: メインキューの `dispatch_source`**（`src=dq`）はアイドルで
  369 回・平均 272 ms。
- **Windows: `SetTimer(nullptr, 0, ms, proc)`（ウィンドウ無し）で刻んだ。** `WM_TIMER` は
  VW のメッセージポンプが配った。**自分で `PeekMessage` を回すと、その場でも配られた**
  （上の表の ①）。ピン留めも `GetModuleHandleExW` の `GET_MODULE_HANDLE_EX_FLAG_PIN` で効いた。
- 殻はプローブが終わると本体を `dlclose` する（[Plug-in Modules](../Findings/Plug-in%20Modules.md)）ので、
  プローブは `dlopen(RTLD_NOLOAD)`（mac）／`GET_MODULE_HANDLE_EX_FLAG_PIN`（Windows）で参照を
  増やして本体を留めた。

### 5. 共通モードで刻むかの測り直し

この調査の途中で**「`kCFRunLoopCommonModes` に入れた `CFRunLoopTimer` は、VW が
アイドルのときには刻まない」と読んだ回がある**（`outside` 局面の刻みが 0 件）。
その回は**仕掛けてから報告させるまでに 11 時間が空いていた**。**同じ起動のうちに 2 分で
測り直したら 367 回刻んだ**（届いたモードには `kCFRunLoopDefaultMode` が入っている）。

### 6. VW が undo イベントを開いたまま回している最中の刻み

**実測**（VW 2026 / macOS。`probes/runtime/undo-event-timer-tick/` を 3 回。
ビルド `1d5b70f53506` / `782dd705b13f` / `7fbdbc35d939`。
[issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)）。

「**VW が undo イベントを開けたまま、VW 自身がイベントを回している**」場面を狙って作って測った。

**測ったのは「VW が出したモーダルダイアログの最中」である。** #206 はこの場面のほかに
**ツールのドラッグ中・レンダリング中**も挙げていたが、そちらは**コマンドが戻った後も
生きるタイマー**が要るので、この調査では作らなかった
（[issue #213](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/213)
で測った——下記 7）。

**作り方（梃子 2 つ）。プローブは 1 度も `SetUndoMethod` / `NameUndoEvent` を呼ばない:**

1. **VW にイベントを開かせる**——`DeleteObject(h, useUndo=true)` は開いていなければ
   自分で開き、**こちらが return するまで開いたまま**（[Undo](../Findings/Undo.md)）。
2. **VW にイベントループを回させる**——`ISDK::AlertQuestion`（VW 自身のモーダル確認
   ダイアログ。上記 ② の場面）。

#### 登録したモードごとの刻みの届き方

| 局面 | 札（登録したモード） | 届いた | うち `building=yes` | 読めた |
| --- | --- | --- | --- | --- |
| **VW のモーダル確認ダイアログの最中** | **`kCFRunLoopCommonModes`** | **85 / 44 / 50 回**（3 回ぶん） | **全件** | **全件** |
| VW のモーダル確認ダイアログの最中 | **`kCFRunLoopDefaultMode` だけ** | **0 / 0 / 0 回** | — | — |
| 自分でランループを回した（0.05 秒 × 12） | 両方（札ごとに 4 回） | 4 ＋ 4 回 | **回による**（下記） | 全件 |

- **モードは `NSModalPanelRunLoopMode` だった。** `kCFRunLoopDefaultMode` だけに
  登録したタイマーは 1 回も届かなかった——**同じ図面・同じ瞬間に 2 本並べて**、
  登録モードだけを変えて測った。
- **刻みの中から `gSDK` を読めた。** `IsCurrentlyBuildingAnUndoEvent()` と
  `GetNamedObject()` が全件成功し、例外も異常も出ていない。
- **刻みの中の `IsCurrentlyBuildingAnUndoEvent()`**——**同じ局面（自分で
  ランループを回した 4 ＋ 4 回）で、イベントの状態だけを変えて両方の値が出た**:

  | 回 | その局面でイベントは | 刻みの中の `building` |
  | --- | --- | --- |
  | 2 回目（`782dd705b13f`） | 開いたまま（`CreateLocus` が開いたものを放置） | **8 回すべて `yes`** |
  | 3 回目（`7fbdbc35d939`） | `EndUndoEvent` で閉じてある | **8 回すべて `no`** |

  同じ 3 回目の中で、**VW が開いている局面（モーダル中）では 50 回すべて `yes`**
  だった。

#### 刻みから書いたものと undo イベントの実測

判定は目視に頼らず、**取り消しを段ごとに掛けて何が消えたかを名前で引いて**読んだ。

| 置いたもの | いつ作ったか | 1 段目の取り消し後 | 2 段目の取り消し後 |
| --- | --- | --- | --- |
| 対照 | **作り、その直後にイベントを閉じた**（＝閉じた別の段に入る） | **残った** | 消えた |
| **刻みが作ったもの** | **VW が開いている最中**（刻みの中） | **消えた** | — |

- **1 段目で刻みが作ったものだけが消え、対照は残った。**
- **2 段目で対照が消えた。**

#### 「いま触ってよいか」を問える口を探した（`IsCurrentlyBuildingAnUndoEvent`）

```cpp
// ISDK.h:2612 —— **コメントが 1 行も付いていない**（前 6 行まで出しても無い）
virtual bool VCOM_CALLTYPE IsCurrentlyBuildingAnUndoEvent() = 0;
```

- `MCNotification.h` 全体で undo に触れる定数は**1 本だけ**（`grep -i undo` の結果が 1 行）:

  ```cpp
  // MCNotification.h:179
  const Sint32 kNotifyUndoEndEvent = 'Udee';	// Send immediatelly before ending an undo event
  ```

- ツールの点取りの両端の通知: `kNotifyBeginToolMode`（`'BTOO'`、
  "Sent when the current tool begins collecting points"）→ `kNotifyEndToolMode`
  （`'ETOO'`、"finishes collecting points"）。`kNotifyRenderModeAboutToChange` / `Changed` は
  「レンダリング方法の設定が変わった」の通知で、`kNotifyDialogDisplayImminent`（`'DlgO'`）は
  開く側だけで、閉じたことを知らせる対が無い。

### 7. ツールのドラッグ中・レンダリング中の刻み

**実測**（VW 2026 / macOS。`probes/runtime/drag-render-timer-tick/` を 7 回。
ビルド `87d374350eda` / `c905cadc9b07` / `7b140e82b39a` / `1c1c26328292` /
`6b0067dc075a` / `c415caa18616` / `7b16c7ef15be`。計 7000 刻み以上。
[issue #213](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/213)）。

上記 6 で測ったのは「VW が出したモーダル確認ダイアログの最中」だけだった。残る 2 つ
——**ツールのドラッグ中（点取りの最中）とレンダリング中**——をここで測る。ドラッグの
最中に当てるには**メニューコマンドが戻った後も生きるタイマー**が要るので、上記 4 の 4 の
ピン留めの作法で仕掛け、1 回目で仕掛け・2 回目で報告させた（同じ起動のうちに閉じた）。

**「ドラッグされたか」は機械で押さえた。** 利用者がドラッグしたかどうかはログに写らない
ので、`kNotifyBeginToolMode`（`'BTOO'`）→ `kNotifyEndToolMode`（`'ETOO'`）で**点取りの
両端を囲み**、その中で届いた刻みだけを数えた（刻み 1 行ごとに `tool=yes/no` が付く）。
囲みが 1 度も来なければ `probe.fail` して「この回はドラッグについて何も言えない」と
結果の見出しに出す作りにした。

#### ドラッグ中・レンダリング中の刻みと `building` の実測

| 場面 | 刻み | うち `building=yes` | 読めた | 届いたモード |
| --- | --- | --- | --- | --- |
| **ツールの点取りの最中**（通知の囲みの中。clean な 4 回） | **180 回** | **0 回** | **全件** | `kCFRunLoopDefaultMode` |
| **トラッキング中**（`NSEventTrackingRunLoopMode`。同じ 4 回） | **120 回** | **0 回** | **全件** | `NSEventTrackingRunLoopMode` |
| **レンダリング中**（仕上げ Renderworks。下記） | **1427 回** | **0 回** | **全件** | **`NSModalPanelRunLoopMode`** |
| clean な 4 回の全部 | **3976 回** | **0 回** | **全件・異常 0 件** | — |

- ドラッグ中・レンダリング中とも、共通モード（か全モード）へ登録した
  タイマーは **250 ms ちょうど**で刻み続けた。
- その刻みからの読み（`IsCurrentlyBuildingAnUndoEvent` / `GetNamedObject` /
  図形の数え上げ）は**全件成功・例外 0 件**。
- clean な 4 回・**3976 刻みで `building=yes` は 0 件**。

#### undo イベントが開閉した瞬間のランループのモード

`kNotifyUndoEndEvent` が**点取りの囲みの中（`tool_depth=1`）で飛んでいた**。
その瞬間に `CFRunLoopCopyCurrentMode(CFRunLoopGetMain())` を読むと、
**どの回も nil**（ログの `mode=(not-running)`）だった:

```
note ## what=undo-end ## t_ms=31653 ## tool_depth=1 ## building=yes ## mode=(not-running)
note ## what=tool-end ## t_ms=31663 ## tool_depth=0 ## building=no  ## mode=(not-running)
```

#### レンダリング中のモードと、既定モードだけのタイマーの刻み

**実測**（ビルド `7b16c7ef15be`。利用者に「ビュー > レンダリング > 仕上げ Renderworks を
選んで、描き終わるまで何も触らない」だけをしてもらった回。計 1936 刻み・444 秒）:

| 口 | 刻み | dt 最小 / 平均 / **最大** |
| --- | --- | --- |
| **共通モード ＋ 全モード** | **1635 回** | 1 / 261 / 9711 ms |
| **`kCFRunLoopDefaultMode` だけ** | **161 回** | 30 / 2625 / **351603 ms（5 分 52 秒）** |

モードごとに割ったもの:

```
NSModalPanelRunLoopMode:    1427 回（building=yes 0 回）  ← これがレンダリング中
kCFRunLoopDefaultMode:       458 回（building=yes 0 回 / 点取りの囲みの中 68 回）
NSEventTrackingRunLoopMode:   45 回（building=yes 0 回）
com.apple.run-loop-mode.view-bridge.blocks: 5 回 / __kCFPasteboardPrivateMode: 1 回
```

- 利用者がレンダリングを選んだのが `t_ms=77154`（`kNotifyRenderModeChanged`）で、
  そこから終わりまで**モーダルのモードの刻みが 1427 回（≒ 357 秒）**続いた
  （上記 6 のモーダルダイアログと同じモード）。
- **その間、共通モードのタイマーは 250 ms ちょうどで刻み続けた**
  （最後の刻みまで `dt_ms=249〜251`・`mode=NSModalPanelRunLoopMode`）。
- **既定モードだけのタイマーは、その間まるごと止まった**——1 度の間隔が **351.6 秒**。
- **読みは全件通り、`building=yes` は 1 件も無かった。**

#### ふつうの操作中の、既定モードだけのタイマーの刻み

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
  止まった**（そのぶんが後でまとめて来た）。

#### コマンドが undo イベントを開いたまま返したときの実測

狙って測ったものではなく、2 回目の計測を汚した事故から出た記録。
`DeleteObject(h, useUndo=true)` が開けたイベントを**閉じずにメニューコマンドから返した**回では:

- **コマンドが戻っても閉じなかった。** 書き溜めの `kNotifyUndoEndEvent` が来たのは **53 秒後**で、
  それまで**刻み 181 回すべてが `building=yes`** を読んだ。
- **その間に刻みが作った図形は、その置き土産のイベントに入った。**
- **戻る直前に `EndUndoEvent` で閉じるようにしたら、`building=yes` は 0 件になった**
  （上の表の clean な 4 回）。閉じるのを処理の途中に 1 度置いただけの回では、その後に
  開くものがあって残った（6 回目の回で 36 件出た）。

#### 「描き直して、終わるまで待つ」口を探した記録（#215）

**実測**（VW 2026 / macOS。`probes/runtime/render-sync-3d/` を 4 回。ビルド
`230f69515fb9` / `c6f9a2cdbc98` / `db5473d03716` / `b9d83f799061`。
[issue #215](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/215)。
プローブは役目を終えたので消してある）。

#213 では 4 つ試してどれも描かせられず、**原因は「2D（上面/平面）ビューのままだった
から」**と見ていた。#215 で 3D へ切り替えて測り直した。

**切り替わったことは機械で押さえた**——`ISDK::SetProjection(layer,
projectionOrthogonal, false, true)` ＋ VectorScript の `SetView(-60,0,30,0,0,0)` の後、
`GetProjection(layer)` が `projectionOrthogonal`(0) を返した（`projectionPlan` は **6**）。
図面には球を 196 個（画面に見えている範囲の 60% に
収まる大きさ・位置。`GetViewCenter` ＋ `ViewPt2WorldPt` で割り出す）置いてある。

##### 「戻るまでの時間」で測ってしくじった経緯

最初は所要時間で測ろうとして**2 度しくじった**。決め手は利用者のスクリーンショットで、
プローブが戻った後もステータスバーに **「Viewport-1 の更新 0:16」**（16 秒経過）が
出ていた——`UpdateViewport` は 283 ms で戻っているのに、VW はその後も描き続けていた。

道具を変えて、**ランループを短い刻みで回し、1 周が伸びるかを見た**:

| 経路（**すべて 3D のビュー・球 196 個**） | 呼び出しが戻るまで | **戻った後にランループを回して吸われた時間** |
| --- | --- | --- |
| `SetRenderMode(layer, renderFinalRenderWorks, immediate=true, doProgress=true)` | **0 ms** | **0 ms**（5002 ms 回して **1 周の最長 53 ms**・100 ms 超の周 **0 回**） |
| 中身を入れたビューポートの `UpdateViewport` | 355 ms（初回）/ 68 ms（2 度目） | **545 ms**（2563 ms 回して **1 周の最長 301 ms**・100 ms 超の周 **3 回**） |

同じ 1 回の実行・同じ道具で取った対照である。

**回数**——「0 ms で戻る」は **4 回**（32 呼び出し）の再現だが、
**「戻った後にランループを回しても何も出てこない」はこの 1 回だけ**である
（ランループで測る道具は 4 回目で入れた）。同じ実行の中で `UpdateViewport`
の側は 545 ms 拾えている。

**測っていないこと**: **コマンドが終わって VW が自分のイベントループへ戻ってから
描くのかどうか**は、プローブの中からは追えないので測っていない。
（「戻った後に VW が何をするか」は [#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)
で一部を測った——**ランループで連続寸法が作り直され、undo イベントの終わりで PIO の外接と
ビューポートの dirty の印が更新される**。断面の中身は戻った後も変わらなかった。
[Investigation Techniques](../Findings/Investigation%20Techniques.md)「いつ読むか」。）

##### 1. `SetRenderMode`（`immediate=true`）の実測

- **3D でも 2D でも 0 ms で戻った。** 4 つのモード（`renderOpenGL` / `renderFinalShaded` /
  `renderFinalHiddenLine` / `renderFinalRenderWorks`）× 2 つの投影（3D / 上面平面）を
  4 回ずつ測って、**32 回すべて 0 ms**。3D か 2D かで何ひとつ変わらなかった。
- **戻った後にランループを 5 秒ぶん回しても、1 周の最長は 53 ms**。同じ実行の中で
  `UpdateViewport` の後は 1 周が 301 ms まで伸びた。
- ヘッダのコメント（`APIBase.Legacy.Defs.h:4225`）は "If immediate
  is true, then all rendering will take place before the call returns."、続く文は
  「背景で描けるぶんはメインのイベントループへ入り直すまで先送りされる」。
- **戻り値はモードによって違った。** `renderFinalShaded`(5) だけ **`true`**、
  `renderOpenGL`(11) / `renderFinalHiddenLine`(6) / `renderFinalRenderWorks`(14) は
  **`false`**。**3D でも 2D でも、4 回とも同じ組み合わせ**だった。
  どちらでも、直後の `GetRenderMode` は狙った値を返した（#213 から変わらず）。
- **`SetProjection` も 0 ms で戻った**（ヘッダは "a long re-render … as a result of the
  projection change" と書いている）。VectorScript の `SetView` は 1〜5 ms。

##### 2. `UpdateViewport` の実測

- **中身は入った。** 表示レイヤを入れれば（`SetViewportLayerVisibility(vp, layer, 0)`）、
  更新後のキャッシュ群（`kViewportGroupCache` = **3**）に **2 件**・外接
  `左-1341.3 上1341.3 右1323.6 下-1323.6` が入った。
  段取りは [Viewports](../Findings/Viewports.md) のとおり（ヘッダ: "a **dirty** viewport, **whose render type is
  other than wireframe or sketch**, will be re-rendered."）。
  **作りたてのままの `UpdateViewport` は 0 ms で戻り、何も起きなかった**（#213 の 131 ms は
  再現しなかった）。
- 戻った後にランループを回すと **545 ms ぶん**
  仕事が出てきた（1 周が 301 ms まで伸びる）。利用者の画面では、プローブが戻った後も
  ステータスバーに「Viewport-1 の更新」が**16 秒**出ていた。
- **`IsDirty()`** は更新の直後に読むと **`no`** を返した。そのとき描画はまだ続いていた。
- **刻み（OS のタイマー）**: `SetRenderMode` の呼び出し中も `UpdateViewport` の
  呼び出し中も、250 ms のタイマーは **0 回**。利用者がメニューから描かせたときは
  `NSModalPanelRunLoopMode` で 250 ms ちょうど届いた（上記の 1427 回）。

##### 効かなかった口（測ったので残す）

- **`VWLayerObj::SetSheetPrintDPI(300)`**——書いた直後の
  `GetSheetPrintDPI()` が **72** を返した（シートレイヤに対して。2 回再現）。
- **`VWViewportObj::SetScale()`**——100 → 50 にしたら
  キャッシュ群の外接が ±2682.5 → ±1341.3 と**半分**になった。

「いま描いているか」を問う口と、レンダリングの開始・終了の通知を探した:

- `ISDK` を `Render` で引いて出るのは `GetRenderMode` / `SetRenderMode` /
  `RefreshRenderingForSelectedObjects` ほか数本だけ。
- `MCNotification.h` 全体を読んだ——`kNotifyBeforePendingUpdate` /
  `kNotifyAfterPendingUpdate` は**通知の取りまとめ用**（"help optimize pending
  notifications … (From OnIdle)"）、`kNotifyUndrawScrMods` / `kNotifyDrawScrMods` は
  **ツールの画面補助線の消し書き**、`kNotifyRenderModeAboutToChange` / `Changed` は
  **モードの変更**。
- **`kNotifyRenderModeAboutToChange` / `Changed` は、SDK からの `SetRenderMode`
  では飛ばなかった**（11 → 14 の変更で 0 件。飛んだのは**利用者がメニューで変えたときだけ**）。
- 上のレンダリング中の数字は、利用者にメニューから描かせて刻みの `mode=` で読み取ったもの。

### 8. 図面が 1 枚も開いていないときの `GetCurrentLayer()` の実測

**実測**（VW 2026 / macOS arm64。`probes/runtime/no-doc-current-layer/` をローカルの
Claude Code から MCP ブリッジで回した。ビルド `26b6337dfcc7` / `4417e0f207fd` /
`3f9588499d8c`。**Windows も実測**——利用者がメニューから同じ手順（走らせる → 全部閉じる →
新規を開く → 走らせる）で回した。ビルド `04eb2b960ca0`。
[issue #224](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/224)）。

上記 1 で「図面が無くても刻みは続き、`GetOpenFilesList()` などは落ちずに『文書が無い』を
返す」ことは測ってあったが、**`GetCurrentLayer()` を図面の無い状態で呼んだ記録は無かった**。
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

- **落ちなかった。** 件数が 0 になった最初の刻みから `nullptr` が返り、以後ずっと `nullptr`。
  例外も異常終了も無い（落ちれば最後に残るよう、件数が変わった刻みでは呼ぶ前に 1 行
  書いていた）。
- **起動直後（一度も図面を開いていない状態）も `nullptr`。** 再起動した直後、図面 0 枚の
  まま、殻のブリッジの刻み（`kCFRunLoopDefaultMode` の `CFRunLoopTimer`）から
  `GetCurrentLayer()` を呼ぶと `nullptr`、件数も 0 で、落ちなかった（`vw_ping` の
  `document_open=false` / `open_documents=0`。図面なしの `vw_run_probe` も同じ判定で
  断られた）。
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

  **どちらも食い違い 0・落ちなかった。** モジュールが読み込まれた直後の刻み——起動画面が
  出ている頃——から呼んでも `nullptr` が返るだけだった。
  （**殻の差し替えそのもので Windows の VW が起動時に落ちる**事故を、この測定の途中で
  踏んだ。当初はプラグインライブラリ関数の書き出しのせいと見ていたが、後に
  [issue #233](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/233) で、
  記録のコードではなく**起動時の読み込み直しで古い番地を指したまま残った殻の時計**と分かった
  ——[プラグインモジュール](../Findings/Plug-in%20Modules.md)
  「【罠】Windows の VW は起動時にモジュールを一度降ろして読み込み直す」。）
- **件数と `nullptr` かどうかは、刻みごとに常に一致した**（mac 1 万 2 千回余り・Windows
  3 万 5 千回余りで食い違い 0 回）。**閉じる途中・開く途中の刻みは 1 つも来なかった**——`CloseDocument` /
  `OpenDocumentPath` はブリッジの刻みの中で同期的に終わり、その間に入れ子の刻みは
  配られていない。**利用者が手で閉じて・開いた Windows の 2 周でも同じ**で、件数 1 → 0 →
  1 と切り替わる刻みのどれにも食い違いは無かった（mac の、人が UI から閉じる道は測っていない
  ——Windows で人の道・mac でブリッジの道が揃ったので、ここで止めた）。
- **別の文書のレイヤが同じアドレスで返った。** 閉じた文書のレイヤ
  `0x7904530380` と、後で新規に開いた文書のレイヤが**同じ値**だった（別の回でも再現）。
  **Windows でも同じ**——`000001FE552347F0` / `000001FE55232AB0` がそれぞれ別の文書の
  レイヤとして 2 度ずつ返った。

**ついでに見えたこと（Windows）:** 報告の実行のとき、書き溜めの心拍が
**9.4 秒・9.9 秒古かった**（ふだんの間隔は最大 1.7 秒）。図面を開いてからメニューで
プローブを選び終えるまでのどこかで、**`SetTimer` の刻みが 10 秒近く止まっていた**。
どの局面で止まったか（メニューの追跡中か・ピッカーのダイアログ中か）は記録していない。
殻の McpBridge.h は「Windows の WM_TIMER はモーダルダイアログの中でも配られる」前提で
ピッカー中の受け付けを見送っており、上記 4 の ②（`AlertQuestion`）でも刻んだ。
（後に [#235](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/235) で、
**メニューを開いてからダイアログが出るまでの間に 7〜14 秒止まる**ことを測った——下記 9
「利用者が開いたモーダルの最中」。VW 自身のダイアログ（環境設定など）の最中は止まらなかった。
ピッカー（レイアウトダイアログ）の最中は測っていない。）

### 9. 刻みから `CloseAllFilesAndQuitVectorworks(true, false)` を呼んだ実測

**実測**（VW 2026。`probes/runtime/quit-from-timer/`。**mac**（arm64）はローカルの Claude Code
から MCP ブリッジで回し、保存の確認のボタンだけ人が押した（ビルド `aeec0b4414f4` /
`ce0611a827d7` / `e0331468016c` / `4ac3163369f7`）。**Windows** は利用者がメニューから同じ
5 段を回した（ビルド `0522e32b6127`。仮想環境で、メモリの割り当てが少なかった——時間の
数値は膨らんでいるかもしれない）。
[issue #230](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/230)）。

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

- **落ちなかった。** mac で 6 回・Windows で 3 回終了させて、すべて `atexit`（C ランタイムの
  `exit()` が呼ぶ）まで届いた。
- **呼び出しはプロセスを終わらせずに戻った**（問い 3）。戻るのは**図面を閉じ終えた後**
  （`DocClose` → 戻る）で、そこから**約 4〜8 秒**後にプロセスが終わった。
- **mac では、戻った後も刻みは届き、`gSDK` を読めた**（問い 5）。終了する回では、戻った後に
  **約 2 秒空いて**（`max_gap_ms` 2128〜2754）`NSEventTrackingRunLoopMode` で刻みが
  1〜数回届き、`GetOpenFilesList()`（0 件）・`IsCurrentlyBuildingAnUndoEvent()`（`no`）・
  `GetCurrentLayer()`（`nullptr`）がどれも落ちずに返った。**最後の刻みから `atexit` まで
  1.3〜2.0 秒**は刻みが来なかった。タイマーは止めず、この 4 回では何も起きなかった。
- **保存の確認**: 刻みの中で `CreateLine` しただけでは
  **確認が出ずに終わった**（最初の版で踏んだ）。`MarkCurrentDocChanged()` を足すと出た
  （[Documents](../Findings/Documents.md)「未保存の変更ありは読めない」の #240 で、保存済みの
  図面に刻みの中から書いて「閉じる」の確認が出ないことを確かめた）。ここで使った「新規の図面」は
  **ブリッジが実行のたびに一時ファイルへ保存していた**。

#### 保存の確認の最中（問い 4）

- **観測のタイマーは刻み続けた**（17.8 秒で 353 刻み、間隔は最大 164 ms）。モードは
  **`NSModalPanelRunLoopMode`**。図面 1 枚・`GetCurrentLayer()` は読めた。
- **`IsCurrentlyBuildingAnUndoEvent()` は `no`**——`AlertQuestion` の最中
  （上記 4 の 6 は `yes`）とは違った。
- **mac では、終了を呼んだタイマー自身は呼び出しの最中に 1 度も入り直さなかった**（駆動の
  入り直し 0 回・全 5 回）。
- **Windows では入り直した。** 確認の最中、終了を呼んだ同じ `SetTimer` の刻みが
  **35〜86 回入り直した**（確認ダイアログのメッセージループが `WM_TIMER` をそのまま配る）。
  段 1 では**その入り直した刻みから 2 度目を呼べてしまった**（呼び出しの 1.5 秒後）。
- **2 度目を呼ぶと、保存の確認が 2 つ重なって出た**（mac・Windows とも人の目視）。落ちは
  せず、両方キャンセルすると内側 → 外側の順に戻った。
  `IsCurrentlyBuildingAnUndoEvent()` は確認の最中も `no`（mac・Windows とも）。

#### 利用者が開いたモーダル（環境設定・オーガナイザ・ファイル設定）の最中

**実測（Windows。mac は下記）**（VW 2026 Update 6 / Windows 11 ARM64 の仮想環境。
`probes/runtime/quit-in-user-modal/`、ビルド `776230e81891`。メニューから走らせ、
ダイアログの開閉と保存の確認のボタンは computer use で押した。
[issue #235](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/235)）。

50 ms の `SetTimer(nullptr, …)` 1 本で、刻みごとに「主窓（走らせたときの前面の窓の根の
持ち主）が `IsWindowEnabled` か」と前面の窓の題を読み、変わったときだけ書いた。主窓が無効で
前面がそのダイアログなら「モーダルの最中」と見る。段 0 は終了を呼ばずに環境設定を開閉し、
段 1 は刻みの中で `CreateLine` ＋ `MarkCurrentDocChanged` で汚してから、**モーダルを見た
3 秒後に**刻みから `CloseAllFilesAndQuitVectorworks(true, false)` を呼んだ。

| ダイアログ | 開いている間の刻み | 呼んだとき | 保存の確認 | 呼び出しの所要 | 「キャンセル」の後 |
| --- | --- | --- | --- | --- | --- |
| 環境設定（段 0。呼ばない） | **届く**（約 23.5 秒で約 355 刻み。間隔は最大 0.6 秒） | — | — | — | — |
| 環境設定 | 届く | 落ちない | **環境設定の上に重なって出る**（環境設定は閉じず、無効になって後ろに残る） | 19568 ms（人の操作込み） | **戻る。環境設定は開いたまま**操作できる。図面 1 枚 |
| オーガナイザ | 届く | 落ちない | 同上（オーガナイザの上） | 18955 ms | 同上 |
| ファイル設定（ファイル ＞ 書類設定） | 届く | 落ちない | 同上（ファイル設定の上） | 31113 ms | 同上 |

- **刻みは届いた**（問い 1）。3 種とも VW の窓のクラスは `#32770`（Win32 のダイアログ）で、
  そのメッセージループが `WM_TIMER` を配る。
- **呼ぶと、保存の確認がダイアログの上に重なって出た**（問い 2。目視——computer use の
  画面で確かめた。機械でも、確認の最中は前面が題 `Vectorworks` の `#32770` になり、
  元のダイアログは無効で残っていた）。**ダイアログを閉じもしなかったし、閉じるのを待ちも
  しなかった。** `kNotifyBeforeApplicationQuit` は呼んだ刻みと同じミリ秒に届いた（上記と同じ）。
- **「キャンセル」で呼び出しから戻り、元のダイアログはそのまま開いていた**（問い 3。
  戻った直後の刻みで前面が元のダイアログに戻り、`IsWindowEnabled` が `yes` に戻った）。
  その後キャンセルで閉じても何も起きなかった。
- **確認の最中も刻みは入り直した**（呼び出しの最中に 225 / 286 / 297 回）。
- **このダイアログの最中は `IsCurrentlyBuildingAnUndoEvent()` が `yes`**（3 種とも、
  呼ぶ前から）。保存の確認だけの最中（上記。`no`）とは違った。
- **刻みが長く来なかったのは、メニューを開いてからダイアログが出るまで**の 4 回だけ
  （6.9 / 14.4 / 7.3 / 8.8 秒。どれも空きの終わりがダイアログの最初の
  刻み）。ダイアログの最中の空きは最大 2.3 秒（確認が出る瞬間）。メニューの追跡とダイアログの
  組み立てのどちらで止まっているかは、この記録では分けられない。

**mac**（VW 2026 Update 6 / macOS 27.0.1。同じプローブ、ビルド `5e9af2e4bed5`。
2026-10-27。ローカルの Claude Code から MCP ブリッジで走らせ、ダイアログの開閉と保存の
確認のボタンは computer use の AX（`AXPress`）で押した）。タイマーは 50 ms の
`CFRunLoopTimer` 1 本を `kCFRunLoopCommonModes` に登録し、「モーダルの最中」は
`CFRunLoopCopyCurrentMode` が `NSModalPanelRunLoopMode` かで見た。

| ダイアログ | 開いている間の刻み（段 0） | 呼んだとき | 保存の確認 | 呼び出しの所要 | 「キャンセル」の後 |
| --- | --- | --- | --- | --- | --- |
| 環境設定 | **届く**（31.3 秒で 619 刻み。モードは `NSModalPanelRunLoopMode`） | 落ちない | ダイアログが開いたまま出る | 326910 ms（人の操作の待ち込み。下記） | **戻る。環境設定は開いたまま**。図面 1 枚 |
| オーガナイザ | 届く（26.3 秒で 520 刻み） | 落ちない | 同上 | 18405 ms | 同上 |
| ファイル設定（ファイル ＞ 書類設定） | 届く（26.6 秒で 525 刻み） | 落ちない | 同上 | 18603 ms | 同上 |

- **刻みは届いた**（問い 1）。3 種とも、開いている間のモードは `NSModalPanelRunLoopMode`
  （上記 4 の 6・保存の確認と同じ）。
- **呼ぶと、保存の確認がダイアログを開いたまま出た**（問い 2）。窓の一覧（AX）で、確認の
  窓（260×286）と元のダイアログが同時にあり、確認は**元のダイアログの左上から (+66, +66)**
  の位置——ダイアログの内側——に出た（3 種とも同じ）。ダイアログを閉じもせず、閉じるのを
  待ちもしなかった（Windows と同じ）。前後（確認が上か）は画面で見ていない。
  `kNotifyBeforeApplicationQuit` は呼んだ刻みと同じミリ秒。
- **「キャンセル」で呼び出しから戻り、元のダイアログはそのまま開いていた**（問い 3。
  戻った直後の刻みのモードは `NSModalPanelRunLoopMode` のまま・図面 1 枚。窓の一覧にも
  ダイアログが残り、その後キャンセルで閉じても何も起きなかった）。オーガナイザ・ファイル
  設定では戻った後の 20 秒に 401 刻み届いた（環境設定の回はプローブの 5 分の上限に先に
  当たって 1 刻みで止まった——確認を 5 分以上置いたのは、画面を見る承認を待ったため）。
- **確認の最中、呼んだタイマーは 1 度も入り直さなかった**（3 回とも 0 回）。上記「保存の確認の
  最中」の mac と同じで、Windows（225〜297 回）とは違った。
- **このダイアログの最中は `IsCurrentlyBuildingAnUndoEvent()` が `yes`**（3 種とも、呼ぶ前
  から）。閉じると `no` に戻った。Windows と同じ。
- メニューを押してから最初の刻みまでの空きは 0.4〜1.2 秒（Windows は 7〜14 秒）。ただし
  ここではメニューを **AX で押した**（メニューを追跡していない）ので、Windows の「メニューの
  追跡中に止まる」とは比べられない。

#### 終了を知らせる通知（問い 5 の後半）

- **`kNotifyBeforeApplicationQuit`** は呼び出しと**同じミリ秒**に、
  **保存の確認より前に**届き、**キャンセルされて終わらなかった回にも届いた**（2 度呼べば
  2 回届く）。SDK のコメントは「Sent immediately before the application quits」
  （`MCNotification.h:274`）。
- **`kNotifyDocClose` は図面が閉じた時点で届いた**（呼び出しの中・戻る直前）。図面 0 枚なら
  届かなかった。
- **`plugin_module_main` に `kPluginModuleDeinit`（-2）が届いた——刻みが止まった後に 1 回**
  （mac は `atexit` の約 0.1 秒前、Windows は約 20 秒前。Windows は下記の表）。殻に action を時刻付きで書かせて（`plugin/src/ModuleMain.cpp`。
  頼み札があるときだけ書く）、刻み・`atexit` と突き合わせた（ビルド `e0331468016c` /
  `4ac3163369f7`）:

  | 回 | 呼び出しから戻る → 最後の刻み | 最後の刻み → **-2** | **-2** → `atexit` |
  | --- | --- | --- | --- |
  | 保存済みの図面 1 枚 | 6.2 秒 | **1.21 秒** | **0.12 秒** |
  | 図面 0 枚 | 3.8 秒 | **1.42 秒** | **0.10 秒** |

  - **-2 の後に刻みは 1 度も来ていない。** 刻みが止まるのが先で、
    -2 はその後、プロセスが `exit()` へ入る直前に来た。-2 の直後に -5（インタフェースの解放）
    が 1 回続いた。起動では -1 が 1 回、その後に -3 / -4 / -5 が 8 回。キャンセルされた回には
    -2 は来なかった。
  - SDK 側がこの値でしていることは、`MiniCadCallBacks.cpp:256` の
    `GS_ReleaseStandardVCOMGlobals` で `gIFC` などのグローバルを手放すことだけ（ソースを
    読んだ）。**-2 の中で `gSDK` を呼べるかは測っていない**（殻は呼ばずに書くだけ）。
  - 殻が自分で読み込む本体には、VW から `plugin_module_main` は呼ばれない。

#### Windows

| 場面 | 保存の確認 | 呼び出しの所要 | 確認の最中の刻み | 戻った後の刻み | 戻る → **-2** → `atexit` |
| --- | --- | --- | --- | --- | --- |
| キャンセル | 出た | 7953 ms（人込み） | 観測 120・**駆動の入り直し 70** | 4 秒で 61 回。図面 1 枚・`gSDK` 読める | 終わらない |
| 2 度目を呼んで両方キャンセル | **2 つ出た**（目視） | 内側 7087・外側 9746 ms | 駆動の入り直し 86（そこから 2 度目） | 図面 1 枚 | 終わらない |
| 保存しない | 出た | 5304 ms（`DocClose` の **1364 ms 後**に戻る） | 観測 60・入り直し 35 | **0 回** | 9.7 秒 → 20.5 秒（計 30.2 秒） |
| 保存済み | 出ない | **1287 ms**（`DocClose` の 1245 ms 後） | — | **0 回** | 8.0 秒 → 20.0 秒（計 28.0 秒） |
| 図面 0 枚 | 出ない | **711 ms** | — | **0 回** | 8.0 秒 → 20.1 秒（計 28.1 秒） |

- **終わっていく回では、呼び出しの後に刻みが 1 回も来なかった**（観測も駆動も。最後の刻みは
  呼び出しの直前か確認ダイアログの中）。
- **呼び出しは mac より遅く戻り（0.7〜1.3 秒）、プロセスはその後 28〜30 秒生きていた。**
  -2 は戻ってから 8〜10 秒後、`atexit` は -2 の約 20 秒後（仮想環境での値）。
- **利用者が閉じる道（メニューの「終了」＝`CloseMainWindow`）でも並びは同じ**——閉じる指示の
  7〜14 秒後に -2、その 19〜21 秒後にプロセスが終わった（3 回。issue #257。
  [Plug-in Modules](Plug-in%20Modules.md)「OS タイマーを持つ常駐プラグイン…は、終了時の
  落ち方を変えない」）。タイマーを止めない常駐プラグインが入っていても、閉じたときの
  終わり方は変わらなかった（同じ節。82 回）。刻みがいつまで来るかは下の「利用者が閉じる道の
  刻み」。
- `kNotifyBeforeApplicationQuit` が確認より前に届き、キャンセルされても届いたのは mac と同じ。
- 確認の最中の `IsCurrentlyBuildingAnUndoEvent()` は `no`（mac と同じ）。
- この間、利用者の側で**起動時のクラッシュが何度かあった**。当時は「古いインスタンスが
  終わる前に起動し直した」形を疑ったが、**その後の調査（issue #253）で、殻のタイマーが
  起動時の読み込み直しで古い番地を指したまま残り、起動中に落ちることが分かった**
  （[Plug-in Modules](../Findings/Plug-in%20Modules.md)「【罠】Windows の VW は起動時にモジュールを一度
  降ろして読み込み直す」。殻を留めて直した）。この節の測定中の回は記録が無く、突き合わせられない。

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

- **`BApQ` の後に刻みは 1 度も来なかった**（11 回とも）。閉じる指示の後に来た刻みは 3 回だけで、
  どれも `BApQ` より前（閉じる指示がまだキューにあった数 ms〜63 ms）。最後の刻みから
  `BApQ` まで 11〜73 ms。
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
  落ちたのは -2 の 36 秒後。プローブの刻みは `BApQ` 以降 0 回。#257 で 82 回再現しなかった
  `cli` の終了時の落ち方と同じモジュール
  （[Plug-in Modules](Plug-in%20Modules.md)「OS タイマーを持つ常駐プラグイン…」）。
  この回は**ブリッジが図面を閉じてホーム画面へ戻った後**に閉じていた（#257 は起動して
  ホーム画面から閉じただけ）。同じ形の 0 枚の回は 5 回中 1 回。

## 5. ウェブパレットの JS タイマーを隠したときの実測

**実測**（VW 2026 / macOS arm64。[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
の PR #192 / #194 の実機確認。出所は
[issue #204](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/204)）:

- **パレットを閉じた（隠した）とき、または Vectorworks がほかのアプリの裏に回ったとき、
  JS の `setInterval`（250 ms）がちょうど 60 秒に 1 回まで間引かれた。** パレットを出し直すと
  すぐ 250 ms ごとに戻った。
- `NSProcessInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep` で
  **App Nap から外しても変わらなかった**。
- 埋め込みブラウザは **Chromium Embedded Framework**（`Vectorworks Web Helper` が
  `--type=gpu-process` 等で走る。SDK のヘルプにも
  `<user folder>/Plug-ins/**ChromiumEF**/WebBrowser_Console_Output.txt` という道が出てくる）。
  60 秒という刻みは Chromium が隠れたページのタイマーを 1 分へ寄せる
  intensive wake-up throttling と一致する。

### SDK に間引きを止める口を探した

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

### 間引きの代わりに探した口（SDK 側）

- **`IWebJavaScriptProvider::OnPaletteVisibilityChange(bool visible, IWebPaletteFrame* frame)`**
  （`IExtensionWebPalette.h:144`）。
- **`IWebPaletteFrame::ExecuteJavaScript(code)`**。`ExecuteJavaScript(code, onCompleteCallback)`
  の方は、SDK のコメントに「callback is executed **outside UI thread**」とある。

## 6. 図面が 1 枚も開いていないときのウェブパレットの実測

**実測**（VW 2026 / macOS arm64。出所は上と同じ issue #204）:

- **Vectorworks を再起動した直後、図面が 1 枚も開いていない画面ではパレットが出なかった**
  ——メニューから `gSDK->SetWebPaletteVisibility(iid, true)` を呼んでも出ず、JS タイマーは
  一度も動かない。**図面を新規作成して編集画面になると、パレットは開いた状態で現れた。**
- パレットに触れる口は
  `GetWebPaletteVisibility` / `SetWebPaletteVisibility`（`ISDK.h:3543-3544`）と
  `GetWebPaletteFrame`（`ISDK.h:3800`）の 3 つで、どれも引数は `VWIID` 1 つ
  （＋ `bool`）。文書が無いときの扱いを変える引数も設定も無い。
- メニュー拡張は `SMenuDef` の `Needs = EMenuEnableFlags::DocIsActive` を立てると、文書が
  無い間はグレーアウトした（実機確認プラグインで実測。`plugin/src/ProbeMenu.cpp`）。
- OS のタイマーは**図面が 1 枚も開いていない状態でも刻み続け**、`gSDK` は落ちずに
  「文書が無い」を返した（上記 4 の 1）。
