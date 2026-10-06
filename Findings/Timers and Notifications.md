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
| OS のタイマー（`CFRunLoopTimer` / `SetTimer`）から `gSDK` を呼んでよいか | 下記 4（実機確認） |
| CEF に隠れたページのタイマーを間引かせない口は SDK にあるか | **無い。** SDK 全体で CEF に触れる口は `WebDlgEnableConsole`（コンソールログの出力）ただ 1 つ【ソース根拠】（下記 5） |
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

## 4. OS のタイマーから `gSDK` を呼ぶ

**【実機確認待ち】** `probes/runtime/runloop-timer-sdk/` で測っている。局面ごとに刻みの
回数と間隔（`dt_ms`）、そのときのランループモード、`gSDK` の戻り値を記録する。

| 測る局面 | 確かめたいこと |
| --- | --- |
| ① 自分でランループを 1.5 秒回す | メニューコマンドの中から仕掛けられるか（mac: `CFRunLoopTimer` をメインのランループの `kCFRunLoopCommonModes` へ） |
| ①b **ランループを回さず 3 秒働く** | **こちらの同期処理に割り込まれるか**（＝自分が undo イベントを開いて描いている最中に刻みが入るか） |
| ①c **進捗ダイアログの `DoYield` を 3 秒回す** | **VW が自分でイベントを回す場面**で刻むか |
| ② モーダル（alert）が開いている間 | 刻むか。刻むなら `mode=` が `NSModalPanelRunLoopMode` になる |
| ③ ほかのアプリの裏に回っている間 | **250 ms が保たれるか**（＝ JS タイマーと違って間引かれないか） |
| ④ **メニューコマンドが戻った後** | 刻み続けるか。そこから `gSDK` を**読めるか・書けるか**（8 回目の刻みで 2D 基準点を 1 つ作る） |

④ は 1 回の実行では見えないので、プローブは**2 回走らせる**作りにしてある（1 回目が
仕掛けて一時ファイルへ書き溜め、2 回目が読んで報告する）。結果が出たらここへ実測値を
書き、この節の【実機確認待ち】を外す。

**本体（`.vwpayload`）はピン留めしてある。** 殻はプローブが終わると本体を `dlclose` する
ので（[Plug-in Modules](Plug-in%20Modules.md)）、ピン留めしないとタイマーの行き先が消えて
落ちる。**＝「コマンドが戻った後も動き続ける仕掛け」を本体（入れ替えできる側）へ置くなら、
モジュールを降ろさせない手当てが要る**（実プラグインでは殻に置けば済む話）。

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
- **したがって手当ては「パレットの外に時計を置く」しかない**（上記 4）。【ソース根拠】

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
- したがって**「文書が開いていない間はプラグインの受け付けは動かない」前提で設計する**。
  起動直後に requests を取りこぼしたくないなら、文書が開いたことを
  `kNotifyDocOpen`（上記 2）で拾って、そこから始める。【ヘッダ根拠】

## まだ分かっていないこと（別の issue に切り出したもの）

- **VW 自身が undo イベントを開いたまま自分でイベントを回している最中**（ツールのドラッグ中・
  レンダリング中など）に刻みが当たったら何が起きるか。プローブは undo イベントを自分では
  開かない決まり（[Undo](Undo.md)・`probes/runtime/README.md`）なので、その場面を狙って
  作れない。
  → [issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)
- **Windows の `SetTimer`（ウィンドウ無しのスレッドタイマー）の振る舞い。** プローブは
  両プラットフォーム向けに書いてあるが、**実機確認は macOS でしか取れていない**。
  → [issue #205](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/205)
