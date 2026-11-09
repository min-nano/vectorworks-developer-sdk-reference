# プラグインモジュールの読み込みと入れ替え

コンパイル済みプラグイン（`.vwlibrary` / `.vlb`）が**いつ読み込まれ、いつなら入れ替えられるか**。
そして「Vectorworks を起動したまま中身を差し替えられないか」という問い（issue #15）の調査。
測った環境・被験体・手順・実測の表・ダンプは [計測記録](../Metrics/Plug-in%20Modules.md)。

## 読み込みは起動時の 1 度きり

- **VW はコンパイル済みプラグインを起動時に走査して読み込み、そのプロセスの間は差し替えられない**
  ——ファイルを置き換えても**次に起動するまで反映されない**。実機確認プラグインの自動アップデートが
  殻の入れ替えで再起動を尋ねるのはこのため（[`plugin/README.md`](../plugin/README.md)「自動アップデート」）。
- macOS は**読み込み中のバンドルのファイルを置き換えること自体はできる**が、動くコードは古いまま。
- Windows は**読み込み中の DLL を消すことも上書きすることもできない**（共有違反）。入れ替えは VW を
  終了させてから。

## 拡張機能を 1 つも登録しないモジュールも、有効なら起動時から常駐する（Windows・macOS）

（[issue #252](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/252)。
被験体は [vectorworks-plugin-command-line-interface](https://github.com/min-nano/vectorworks-plugin-command-line-interface)
の `cli`。[実測](../Metrics/Plug-in%20Modules.md#拡張機能を-1-つも登録しないモジュールも有効なら起動時から常駐するwindowsmacos)）

- **`REGISTER_Extension` を 1 度も呼ばないモジュールでも、VW は起動時に読み込んで
  `plugin_module_main` を呼び、常駐させる。** そこで始めた OS タイマーは刻み続ける。目に触れない
  メニューを登録するといった細工は要らない。条件は 2 つだけ:
  - **「開発元不明のプラグイン」で有効にしてあること**（新しく置いた最初の起動では無効にされ、
    読み込まれない。下記）。
  - **Windows では、OS にコールバックを預ける前に自分を留めること**（下記「【罠】」）。
- プラグインマネージャの「開発元」タブでは、拡張なしのモジュールは「不明な開発元」の下に
  ファイル名だけが載る（読み込まれていない印ではない）。
- 当初の「Windows では拡張なしだと常駐しない」は、有効化の前に見ていたため。
- 留めない拡張なしのモジュールが Windows の読み込み直しの対象になるかは確かめていない（留めれば
  困らない）【推定】。

## 【罠】Windows の VW は起動時にモジュールを一度降ろして読み込み直す——OS に預けたコールバックは留めておく

（[issue #253](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/253)。Windows 11 ARM64 の
VM・VW 2026 は x64 のエミュレーション。[実測](../Metrics/Plug-in%20Modules.md#実測)）

- **Windows の VW は起動時、`plugin_module_main` に -1（`kPluginModuleInit`）・-3
  （`kVCOMRegisterInterfaces`）を送ったあと、-2 を送らずに一度降ろし、別の番地へ読み込み直して
  -4（`kVCOMQueryInterface`）以降を送る。** 読み込み直した側には -1 は来ず、static は初期化し直される。
- **最初の呼び出しで OS に預けたコールバックの番地**（`SetTimer(nullptr, 0, ms, proc)`・ウィンドウ
  手続き・フックなど）は降ろされた後も残り、**もう無い番地を呼び続ける**。そこに別の `.vlb` のコードが
  入っていれば**起動の途中で落ちる**。同じビルドが落ちたり落ちなかったりする。
- **避け方: 預ける前に自分を留める。**

  ```cpp
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                     自分の関数の番地, &h);
  ```

  留めれば VW の `FreeLibrary` では降りず、読み込み直しも同じモジュールが返る（殻は
  `plugin/src/McpBridge.cpp` の `StartMcpBridgeClock` でこうしている）。留めた殻は 6 回とも起動し、
  すべての action が同じ番地で届いた。
- 落ちた回は **Windows の異常終了記録（Event 1000）に残らず、VW 同梱の Sentry がダンプを
  `%APPDATA%\Nemetschek\Vectorworks\2026\Sentry\reports\*.dmp` に書く**（`VW User Log.txt` にも残らない）。
  `0xe0000001` の形は Event 1000 に残り（`KERNELBASE.dll`）、Sentry のダンプは出ない。
- 読み込み直していることの根拠（関数が 2 回走る・-1 が 1 回で -2 が無い・落ちた番地が「前の読み込みの
  番地＋RVA」）と、留めない殻のダンプ 12 件の落ち方は [計測記録](../Metrics/Plug-in%20Modules.md#実測)。

### 実測

[計測記録](../Metrics/Plug-in%20Modules.md#実測)——留めない殻（`a8af333`）は 12 回中 9 回落ち、
**留めた殻（`5e969b7`）は 6 回とも起動した**。**落ちなかった回数だけでは直った証拠にならない**
（留めない殻でも続けて起動した回がある）。直ったと言える根拠は番地のほう。

### #233 の `0xe0000001` も同じもの

（[issue #233](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/233)。
[実測](../Metrics/Plug-in%20Modules.md#233-の-0xe0000001-も同じもの)）

- 「Plug-ins 直下の `VWPluginLibraryRoutines.h` / `.p` / `WorksheetFunctionOptionsRegistry.txt` の
  ずれた書き出しで起動時に落ちる」と見ていたのは**誤り**。`KERNELBASE.dll`・`0xe0000001` も、
  **古い番地を指したまま残った殻の時計**である（3 ファイルが無くても落ち、留めた殻なら起動した）。
- 【推定】手続きの RVA が着地する先の命令で落ち方が決まる。

### 読み違えやすいところ

- **「Plug-ins の中身で決まる」ように見える**のは、起動ごとに番地の巡り合わせが変わるため。
- 起動中に VM ごと落ちたこと（バグチェック `0xD1`・`viogpudo.sys`）は UTM の仮想 GPU ドライバの
  不具合で、上の直しとの関係は確かめていない。
- **macOS の VW は読み込み直さない——mac の殻は留めなくてよい**（5 回起動して、すべての action が
  同じ番地で届いた。[実測](../Metrics/Plug-in%20Modules.md#読み違えやすいところ)）。
- **閉じたときは `-2` の後にもう 1 度 `-5` が来る**（mac・Windows）。**`-2` を「もう呼ばれない」合図に
  して後片付けすると、続く `-5` で片付けたものを踏む。**
- 【推定】同じ `Plug-ins` の別のプラグイン（`cli`。`SetTimer` を使う）も同じ形で落ちうる。

## OS タイマーを持つ常駐プラグイン（留めたモジュール）は、終了時の落ち方を変えない（Windows）

（[issue #257](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/257)。
[実測](../Metrics/Plug-in%20Modules.md#os-タイマーを持つ常駐プラグイン留めたモジュールは終了時の落ち方を変えないwindows)）

- **`SetTimer` を持ち `GET_MODULE_HANDLE_EX_FLAG_PIN` で留めた常駐プラグインを入れても、利用者が
  閉じたときの終わり方は変わらない**（82 回すべて終了コード 0、所要にも差が無い）。**タイマーを
  止める・留め方を変えるといった作法は要らない。**
- 以前 `cli` で 11 回中 3 回出た終了時の `0xC0000005`（外された `libcef.dll` の中）は 40 回で再現
  しなかった。`cli` の性質ではなく当時の状況の側と読めるが、**何が効いていたかは分かっていない**
  （当時の記録は cli の側の [Windows の実機での実測](https://github.com/min-nano/vectorworks-plugin-command-line-interface/blob/98bfbb1e5745405cb208981271a2d8b8240ad224/docs/plugin/windows-measurements.md)）。

### 実測

[計測記録](../Metrics/Plug-in%20Modules.md#実測-1)。要点:

- **利用者が閉じる道でも `kPluginModuleDeinit`（-2）は終わる前に 1 回届く**（閉じて 7〜14 秒後、
  その 0.5 秒後に -5、-2 の 19〜21 秒後に終了）。刻みから `CloseAllFilesAndQuitVectorworks` を呼んだ道
  （[Timers and Notifications](Timers%20and%20Notifications.md) 4 の 9）と同じ並び。刻みは
  **`kNotifyBeforeApplicationQuit` の後には来ない**（同 4 の 9「利用者が閉じる道の刻み」。#269）。
- ホーム画面が出た直後に閉じるとタイトルの無いダイアログが開いていることがあり、終わるまで
  38〜50 秒かかる。**起動の途中（ホーム画面の前）に送った `CloseMainWindow` は捨てられる。**
- 1 回、本体が終わった後に CEF の renderer が殺せずに残った【推定】（仮想 GPU のドライバ）。
- #269 の 11 回中 1 回、図面を閉じてホーム画面へ戻った後に閉じた回で `libcef.dll_unloaded` の
  `0xC0000005` で落ちた。【推定】図面を開いて閉じ、ホーム画面（CEF）へ戻ったことが効いている
  ——確かめていない。

### 読み違えやすいところ

外から VW を起動・終了させる道具を書くときの落とし穴（[詳細](../Metrics/Plug-in%20Modules.md#読み違えやすいところ-1)）:

- **「開発元不明のプラグイン」のダイアログが出たままだと `CloseMainWindow` は効かない。**
- **`Get-Process Vectorworks2026` は CEF の子プロセスも拾う**——主は `--type=` の無いもの
  （`Win32_Process`）か `Start-Process -PassThru` の戻り値を使う。
- **`After SetUpCOM` はホーム画面ではない。** 起動し終えた目印は `Section = HomeScreen`。
- **`VW User Log.txt` は前の起動の行を持ち越し、途中で縮む**ことがある——**各行の `ts`（UTC）が
  起動より後のものだけを見る**。`FileShare.ReadWrite | Delete` で開く。

### 新しい図面の最初の描画——30 回で 1 度も落ちなかった（Windows）

（[issue #270](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/270)。
[実測](../Metrics/Plug-in%20Modules.md#新しい図面の最初の描画30-回で-1-度も落ちなかったwindows)）

- **「新規用紙」から開いて最初の描画を待って閉じる、を 30 回回して、描画中の落ちは 0 回。常駐
  プラグインの有無で差は見えない**（1 回あたり約 10% を超えるとは考えにくい）。cli 側の 2 回中 1 回は
  偶然の偏りと読むのが自然で、【推定】プラグインではなく VM の描画（WARP）の側
  （[Viewports](Viewports.md) の #248）。
- **ホーム画面の「新規用紙」の最初のクリックは効かないことが多い**（30 回中 21 回）——開いたかを
  窓の題（`[名称未設定 1]` など）で確かめて押し直す。

## 「開発元不明のプラグイン」のダイアログ——有効化は**フルパス**で覚え、出るのは無効があるときと約 24 時間おき（Windows・macOS）

（[issue #258](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/258)。Windows・macOS の
VW 2026。被験体は `cli`（署名なし／アドホック署名）。[起動ごとの実測](../Metrics/Plug-in%20Modules.md#実測起動ごと)）

**まとめ（`install` のように外からプラグインを置く側が知っておくこと）:**

- **有効化の記録は `Settings\ExceptionsMissingCredentials.bin` に、プラグインの
  フルパスで残る**（レジストリには無い）。**中身は見ない**——同じパスのまま別のビルドへ
  入れ替えても、末尾にバイトを足しても有効のまま読み込まれた。**パスが変われば別物**
  （フォルダ名を変えただけで「無効」に戻った）。
- **記録はダイアログを閉じるたびに、そのとき一覧にあったものだけで書き直される。**
  プラグインが居ない起動でダイアログを閉じると、そのプラグインの有効化は消える
  （戻しても「無効」から）。**入れ替えはパスを変えず、プラグインが居ない状態で VW を
  起動させない。**
- **前もって有効にしておける——ただし作れるのは同じ記録の複製だけ。** VW を止めた状態で
  以前の `ExceptionsMissingCredentials.bin` を戻すと、ダイアログ無しで読み込まれた。記録は
  固定の鍵による暗号文なので、新しいパスの記録を外から作ることはできない（鍵は取り出して
  いない）。**別の機械で作った記録の複製が通るかは未確認**（[#260](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/260)）。
- **ダイアログは「無効のものがあれば毎回」「全部有効なら前回から約 24 時間おき」に出る。**
  同梱ヘルプは「各セッションの開始時に開く」と書くが、実測では間引かれる。全部有効なら
  開いている間もプラグインは読み込み済み（[cli の実測](https://github.com/min-nano/vectorworks-plugin-command-line-interface/blob/98bfbb1e5745405cb208981271a2d8b8240ad224/docs/plugin/windows-measurements.md)と同じ）。
- **全部有効で出たダイアログの最中も、プラグインは OS タイマーの刻みから図面を作る・足す
  （undo の記録の中で）・保存する・閉じる・開くができ、閉じた後も残る**（Windows。下の
  「ダイアログの最中も…」。#264）。ボタンは UI Automation では押せず、Win32 の `BM_CLICK`
  なら押せる。**mac は `kCFRunLoopCommonModes` に登録したタイマーからなら同じことができる**
  （ダイアログは `NSModalPanelRunLoopMode` のモーダルで、既定のモードのタイマーは刻まない。
  下の「mac も…」）。ボタンは `AXPress` で押せる。
- **SDK に有効にする口は無い【ヘッダ根拠】。Authenticode の署名では消えない見込み
  【バイナリ根拠】**——「開発元」は Vectorworks のパートナープログラムが発行する
  クレデンシャルで、Windows の署名ではない（下記）。署名して試すのは [#260](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/260)。
- **macOS も同じ。** 記録は `~/Library/Application Support/Vectorworks/2026/Settings/ExceptionsMissingCredentials.bin`
  に**バンドル（`cli.vwlibrary`）のフルパス**で残り、`install` の手順（フォルダごと
  付け替え・中身も別ビルド）でも有効のまま、閾値はほぼ 24 時間ちょうど（下の「macOS」）。

### 実測（起動ごと）

- **閾値は暦の日付ではなく経過時間**（Windows で 23 時間〜24 時間 20 分の間、mac で 23 時間 55 分〜
  24 時間 5 分の間）。**無効のものがあると `<Date>` が新しくても出る。**
- ボタンごとの書き方: **「続ける」（全部有効）は記録を同じバイト列で書き直し `<Date>` を押した時刻に**、
  **「有効化」は押した時点では書かず「続ける」が「再起動」に変わる**、「再起動」は記録を書き直し
  自動で起動し直す、**無効が残るときの「続ける」「終了」は記録を空に書き直す**
  （[表](../Metrics/Plug-in%20Modules.md#実測起動ごと)）。

### ダイアログの最中も、プラグインは刻みから図面を扱える（Windows）

（[issue #264](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/264)。
[実測](../Metrics/Plug-in%20Modules.md#ダイアログの最中もプラグインは刻みから図面を扱えるwindows)）

- **全部有効で出たダイアログの最中、`SetTimer(nullptr, …)` の刻みは届き、図面を作る・undo の記録の
  中で足す・保存する・閉じる・開くがすべて通った。閉じた後も残った。** ＝誰も見ていない起動で
  ダイアログに止まっても、プラグイン経由の操作は待たずに進められる。
- **ボタンは UI Automation では押せない**（`Pane` に見え、パターンを持たない）。**Win32 の `BM_CLICK`
  なら押せる**（「続ける」＝ID 1、「終了」＝ID 2、「有効化」＝ID 7、「ファイルの場所を開く...」＝ID 8。
  題は表示言語で変わるはずなので ID で押す）。
- ダイアログが開いているかは `VW User Log.txt` より**窓**で見るほうが確か（ログがダイアログの
  最中に縮み、前の起動の `HomeScreen` を拾った【推定】）。

### mac も、共通モードの刻みからなら扱える（既定のモードだけでは刻まない）

（[issue #264](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/264)。
[実測](../Metrics/Plug-in%20Modules.md#mac-も共通モードの刻みからなら扱える既定のモードだけでは刻まない)）

- **ダイアログは `applicationDidFinishLaunching` の中の `runModalForWindow:` で回り、モードは
  `NSModalPanelRunLoopMode`**——**刻むのは `kCFRunLoopCommonModes` に登録したタイマーだけ**
  （[Timers and Notifications](Timers%20and%20Notifications.md) の 4 の 6 と同じ）。
- **既定のモードだけのタイマーはダイアログの間 1 度も刻まない**（639 秒止まった）。共通モードなら
  Windows と同じ手順がすべて通った（[#276](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/276)
  で殻のブリッジを共通モードに登録し直して確かめた）。**既定のモードだけのプラグインは、
  ダイアログが閉じるまで何もできない。**
- ダイアログの最中の `IsCurrentlyBuildingAnUndoEvent()` は `no`（利用者が開いた環境設定などの
  最中の `yes` とは違う）。
- **ボタンは AX の `AXPress` で押せる。** ダイアログが出たことは `VW User Log.txt` の
  `Dialog[OPEN]: ID-10002 (UnknownDeveloperPlugIns)` で分かる（表示言語に依らない【推定】）が、
  縮む前の行を拾わないよう時刻で確かめる。

### 置き場所と形

- **有効化の記録**: `%APPDATA%\Nemetschek\Vectorworks\2026\Settings\ExceptionsMissingCredentials.bin`。
  先頭 8 バイトが平文の長さ、続いて 16 バイト単位の暗号文——**フルパスそのものを鍵も IV も固定の
  ブロック暗号で暗号化している**（[詳細](../Metrics/Plug-in%20Modules.md#置き場所と形)）。何も有効で
  なければ 8 バイト。**プラグインが 2 本以上のときの並べ方は見ていない**
  （[#260](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/260)）。
- **間引きの基準**: `Settings\SavedSettingsUser.xml` の `<Vectorworks><UnknownPluginsDialog><Date>`
  （日;月;年;時;分。ローカル時刻）。
- **レジストリ**（`HKCU\Software\Nemetschek\Vectorworks 31` 以下）には何も無い。

### 「開発元」とは何か【バイナリ根拠】

- 「開発元」は**VW 同梱の `Plug-Ins\Credentials*.vlb` のクレデンシャル**（ECDSA P-384 で署名した JSON。
  `files` は `Plug-Ins` からの相対パスの一覧）で、Vectorworks パートナーシッププログラムが審査して
  発行する（[中身](../Metrics/Plug-in%20Modules.md#開発元とは何かバイナリ根拠)）。
- **Authenticode は使っていない見込み**（本体と同梱の `.vlb` / `.dll` に `WinVerifyTrust` が無い）。
  **署名して試してはいない**（[#260](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/260)）。

### SDK【ヘッダ根拠】

`SDK Index/` に `credential` / `UnknownPlugin` / `UnknownDeveloper` / `ExtendedPropsSupport` /
`AddCredentials` / `partner` は 1 件も無い。プラグインから有効・無効を操作する口も、
クレデンシャルを扱う口も公開されていない。

### macOS

（[実測](../Metrics/Plug-in%20Modules.md#macos)）

- **規則は Windows と同じ。** 記録は `~/Library/Application Support/Vectorworks/2026/Settings/ExceptionsMissingCredentials.bin`
  で、**鍵はバンドル（`cli.vwlibrary`）のパス**（中の実行ファイルのパスではない）。
- **cli の `install` の手順（VW を止めてフォルダごと付け替え・中身も別ビルド）でも、パスが同じなら
  有効のまま、ダイアログも出ない。**
- 空の記録の先頭 8 バイトは 0 ではなく書くたびに違う値（Windows は長さ 0）【推定】初期化していない値。
- **「有効化」ボタンは実際のクリックで行を選んだときだけ押せる**（`AXSelected` では灰色のまま）。
  「続ける」「終了」は `AXPress` で押せる。
- 入れたばかりの VW で「有効化」→「再起動」の 2 回目にもダイアログが出たのは、【推定】`<Date>` が
  まだ無かったため（「再起動」は `<Date>` を書かない）。

## プラグインのフォルダの「モジュール以外のもの」は無視され、モジュールは置いた場所から読まれる（Windows・macOS）

（[issue #277](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/277)。プローブ
`plugin-folder-extras`。[実測](../Metrics/Plug-in%20Modules.md#プラグインのフォルダのモジュール以外のものは無視されモジュールは置いた場所から読まれるwindowsmacos)）

`<Plug-ins>/cli/` にモジュールと並べて CLI の実行ファイル・フック・JSON・スプールを置く形を
確かめた。**Windows でも mac でも、VW はそれらに何もしない。**

### macOS

- **警告は出ない・書き換えも移しも消しもしない・開いたままにしない**（起動を 2 回またいで 17 件が
  inode・大きさ・更新時刻・ハッシュまで同じ。[実測](../Metrics/Plug-in%20Modules.md#macos-1)）。
- **モジュールは置いた場所のファイルそのものから読まれる**——モジュールの中で `dladdr` で自分の場所を
  求めれば `<Plug-ins>/cli/` が得られる（mac は読み込み直さないので起動の途中でも同じ値）。
- **mac はフォルダの名前をいつでも変えられる**（読み込み中でも）。
- 起動時の走査で中身を読んだかは見ていない（読んでいたとしても害は無い）。

### Windows

- **警告は出ない**（「開発元不明のプラグイン」にも付属物は載らない）。**書き換えも移しも消しもせず、
  ハンドルも残さない**（付属物は共有なしで開け、名前も変えられる。[実測](../Metrics/Plug-in%20Modules.md#windows)）。
- **ただし `cli/`（モジュールを入れたフォルダ）自体の名前は VW が動いている間は変えられない**
  （`MoveFileExW` が `ERROR_ACCESS_DENIED`＝5。中の `cli.vlb` が写像されているため）。**`<Plug-ins>/cli/`
  をフォルダごと付け替える `install` は VW を止めてから行う。**
- **モジュールは置いた場所から読まれる**——モジュールの中で `GetModuleHandleExW(FROM_ADDRESS)` →
  `GetModuleFileNameW` で自分の場所を求めれば `<Plug-ins>\cli\` が得られる（起動時の降ろし直しより後の
  値。降ろす前も【推定】同じパス）。
- 実機確認プラグインの本体（`.vwpayload`）が `%TEMP%` の写しから読まれるのは殻の作りで、VW が
  そうしているのではない。

## VCOM の初期化はモジュールごとに閉じている【ソース根拠】

- **`gSDK` / `gCBP` / `gVWMM` / `gVCOMCallback` は、SDK の静的ライブラリ（`libVWSDK.a` / `VWSDK.lib`）が
  持つそのモジュール内のグローバル変数。** 埋めるのは `GS_InitializeVCOM(void* cbpParam)` で、
  **`cbpParam` を材料にした自モジュール内の代入だけ**をする（[宣言と代入の一覧](../Metrics/Plug-in%20Modules.md#vcom-の初期化はモジュールごとに閉じているソース根拠)）。
- **同じプロセスに SDK の複製を持つモジュールが 2 つあってよい**（はず）。受け取った `CallBackPtr` を
  渡して `GS_InitializeVCOM` を呼べば、そちらの `gSDK` も埋まる【推定】（実機での確認は下記
  「本体を外部モジュールへ出せば…」）。

### SDK の静的ライブラリをリンクするモジュールは、この 2 つを定義しなければならない

**Vectorworks にプラグインとして登録されないモジュールでも要る**（CI の実ビルドで確認。
無いとリンクが通らない）。

```cpp
// ① GS_InitializeVCOM がこれを呼ぶ（Include/VectorworksSDK.h:56-61 の注記どおり）。
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_ver() { return SDK_VERSION; }

// ② リソース（.vwr）の識別子。**リソースを 1 つも持たず、引きもしないモジュールでも要る。**
const char* DefaultPluginVWRIdentifier() { return "<vwr の基底名>"; }
```

- ② を落とすと、**呼んでいる覚えのない場所（`TXResStr` など）から参照される未解決**になる
  （[リンカの出力](../Metrics/Plug-in%20Modules.md#sdk-の静的ライブラリをリンクするモジュールはこの-2-つを定義しなければならない)）。
- `DefaultPluginVWRIdentifier` は **C++ リンケージ**（`extern "C"` を付けない）。

## 本体を外部モジュールへ出せば、再起動なしで入れ替えられる

**できる。macOS で実測した**（VW 2026 / Apple Silicon）。

```
Vectorworks ──読み込む──▶ 殻（メニュー・ダイアログ・更新）      … 起動時に 1 度きり
                              │ dlopen / LoadLibrary
                              ▼
                          本体（調査コード）                     … 好きなときに降ろして読み直せる
```

VW は本体の存在を知らないので、**その入れ替えに VW の再起動は要らない**。実機確認プラグイン
（`plugin/`）はこの形で作られている（[`plugin/README.md`](../plugin/README.md)「殻と本体」）。境界:

| 置き場所 | 役割 |
| --- | --- |
| [`plugin/src/PayloadAbi.h`](../plugin/src/PayloadAbi.h) | 殻と本体の間の **C の ABI**（例外・C++ の型・vtable を跨がせない） |
| [`plugin/src/PayloadHost.{h,cpp}`](../plugin/src/PayloadHost.h) | 殻の側（読み込み・解決・アンロード・複製） |
| [`plugin/src/PayloadHostHolder.h`](../plugin/src/PayloadHostHolder.h) | 本体の側。**殻から渡されたものを写して持つ**（下記「落とし穴」） |
| [`plugin/src/payload/PayloadMain.cpp`](../plugin/src/payload/PayloadMain.cpp) | 本体の側（`GS_InitializeVCOM` を自分で呼ぶ） |

境界を C にしてあるのは**降ろすため**——降ろした瞬間にそのモジュールのコードと静的データは
消えるので、殻の側に残してよいのは「値を写したもの」だけ。

### 実測（macOS。中身の違う 2 つのモジュールを順に読んだ）

- **アドホック署名の `MH_BUNDLE` を VW のプロセス内で `dlopen` でき、殻の `gCBP` を渡して
  `GS_InitializeVCOM` すれば SDK が使え、`dlclose` で本当に消え、置き換えて読み直すと走るコードが
  入れ替わった**（[表](../Metrics/Plug-in%20Modules.md#実測macos中身の違う-2-つのモジュールを順に読んだ)）。
- **読み込みは 0.3〜0.4 秒**（SDK の静的ライブラリを丸ごと抱えているため）、アンロードは十数 ms。
- 読み込み中のファイルは mac では消せる（Windows は消せないはず＝未確認）。

### 読み違えやすいところ: `gSDK` の**値**は殻と同じになる

- 殻と本体の `gSDK` が同じ値なのは**共有の証拠ではない**（VW 側の同じ `ISDK` を指すため）。**複製か
  どうかは `&gSDK`（変数の番地）で比べる。** 本体は `RTLD_LOCAL` で読むので変数はモジュールごとに別。

### 実運用でこうした（この形にするときの決めごと）

| 決めたこと | なぜ |
| --- | --- |
| 境界は **C の ABI**（`VW_PAYLOAD_ABI_VERSION` 付き） | 降ろす前提なので、例外・C++ のオブジェクト・vtable を跨がせない。殻と本体は別々にビルドされ別々に配られるので、版の食い違いは実行時にしか気付けない |
| 本体は**必ず一時ディレクトリへ複製してから読む** | Windows は読み込み中の DLL を置き換えられない。同梱物を直接読むと、動かしたまま入れ替えられなくなる＝仕組みが死ぬ |
| 本体は**バンドルの中に置かない**（mac も殻の隣） | バンドルの署名はリソースまで封をするので、中を差し替えると署名が壊れる |
| **選ばれたときに読み、終わったら降ろす** | 入れ替えが自動で効く（「読み直す」を押させない）。降ろすときにコードがスタックに無いことが自明になる。**一覧を出すだけなら 1 つも読み込まない**（下記「本体は複数置ける」） |
| 境界を越えて渡した構造体は**受け取った側がその場で写す** | 相手の記憶域がどこにあるか（ローカルかメンバか）は分からない。持ち続けると腐る——**実際にこれで Vectorworks ごと落とした**（下記「落とし穴」） |
| 再起動の要否は**殻の ID の一致**で決める | 本体だけなら要らない。判断できないときは必ず「要る」へ倒す——版が食い違ったまま動かすほうが危ない |

### 本体は複数置ける（PR ごとに分けて、選ばれた 1 本だけ読む）

- **できる（実機）。** 一覧は殻の隣のテキストの索引（`VwSdkProbes.probes.txt`）だけで組め、選ばれた
  1 本だけを読んで走らせ、欠けた本体は「※本体なし」と出て他は使える
  （[表](../Metrics/Plug-in%20Modules.md#本体は複数置けるpr-ごとに分けて選ばれた-1-本だけ読む)）。
- **分ける値打ちは「壊れ方が 1 本の中に閉じる」こと**——1 つの調査コードがコンパイルできなくても、
  残りは配れる。
- 索引と本体は**同じビルドから出し、置くのは本体 → 索引の順**。殻は選んだ id が本体にあるかを
  確かめてから呼ぶ。

### 落とし穴: 殻の記憶域を本体に持たせると落ちる（実機で落とした）

- **境界を越えて渡した構造体のポインタを、受け取った側が持ち続けてはいけない。** 殻の `load()` の
  ローカルに置いた `VwPayloadHost` を本体がポインタのまま持ち、腐った構造体から関数ポインタを
  読んで呼んで VW ごと落ちた。**コンパイルもリンクも CI の実ビルドも通る**——実機でしか出ない。

#### クラッシュレポートから犯人を特定する読み方

- macOS の crash report で、**`esr` の `Instruction Abort`（命令フェッチ）・`pc` がスタック・引数の
  一部だけがゴミ**なら、構造体から読んだ関数ポインタが腐っている
  （[実物と読み方](../Metrics/Plug-in%20Modules.md#クラッシュレポートから犯人を特定する読み方)）。
- **crash report のフレームは末尾呼び出しで省略されうる**ので、`lr` と突き合わせて読む。

#### 決めごと（両側に置く）

| どちら | すること |
| --- | --- |
| **本体** | 受け取った `VwPayloadHost` を**その場で写す**（`plugin/src/PayloadHostHolder.h`）。以後、殻の記憶域には触らない |
| **殻** | 渡した実体を**降ろすまで生かす**（`Payload` のメンバに置く。`load()` のローカルにしない） |

- **両方やる**（殻と本体は別々に配られるので、片方だけ古い組み合わせが起きうる）。逆向きの
  `VwPayloadInfo` / `VwPayloadProbe` / `VwPayloadResult` は**殻が写す**約束。
- **この壊れ方は単体テストで押さえられる**（`plugin/tests/PayloadHostHolderTests.cpp`）。直した後、
  本番の形（メニュー → 本体 → SDK と VWFC → ログが殻へ → 結果ダイアログ → 降ろす）が実機で一周した。

### 本体側からダイアログを開ける（実プラグインで実測）

**開ける**（[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
を割ったとき。macOS。[開いたもの](../Metrics/Plug-in%20Modules.md#本体側からダイアログを開ける実プラグインで実測)）。

- `VWFC::VWUI::VWDialog`（`GS_CreateLayout` で組む）・VCOM の `IFileChooserDialog` /
  `IFolderChooserDialog`（[File and Folder Dialogs](File%20and%20Folder%20Dialogs.md)）が本体側から動いた。
- **`.vwr` は殻の側にしか無くてよい**（`.vwr` の文字列は登録で使い、登録は殻に残る）。
- **危ないのは「開いたまま降ろす」こと。** **降ろすのは入れ子の深さが 0（本体のコードがスタックに
  無い）ときだけ**——降ろす契機を呼び出しの外に置く。

### PIO のイベントも殻から本体へ取り次げる（実プラグインで実測）

- **取り次げる。** PIO の登録（`SParametricDef`・`IMPLEMENT_VWParametricExtension` など）は殻に残し、
  `Recalculate()` の中身を本体へ出せる。VW が自発的に起こすリセットでも走り、**1 回あたり 0.6 ms
  前後**（[実測](../Metrics/Plug-in%20Modules.md#pio-のイベントも殻から本体へ取り次げる実プラグインで実測)）。
- **この形では本体を載せっぱなしにする**（読み込み 0.3〜0.4 秒をリセットごとに払えない）。入れ替えの
  判定（同梱ファイルの大きさ・更新時刻）だけを入口ごとに行い、降ろすのは「変わった」かつ入れ子の
  深さが 0 のときだけ。**入口がメニュー 1 つなら「選ばれたら読み、終わったら降ろす」でよい。**

### 実運用の入れ替えが実機で通った（再起動なし）

- **`.vwpayload` だけを差し替えて、VW を再起動せずに走るコードが入れ替わった**（実プラグインの
  取り込み。描いた 1174 件は前後で一致。[手順と結果](../Metrics/Plug-in%20Modules.md#実運用の入れ替えが実機で通った再起動なし)）。
- **mac では本体にも個別のアドホック署名が要る**（殻の隣に置くので `codesign --deep` に含まれず、
  無いと library validation で `dlopen` が失敗する）。配布 zip を作るときも展開するときも署名し直す。

### まだ確かめていないこと

- **Windows**（`LoadLibraryEx` / `FreeLibrary`）。設計上は同じはずだが未実測
  （本体を複数置く形も含めて、Windows での実測はこのリポジトリにまだ記録が無い）。
- 本体側から**イベントを登録する**（`RegisterNotificationProcedure` 等、VW にモジュール内の
  番地を**持ち越させる**操作）。ダイアログと PIO のリセットは呼び出しの中で完結するので
  上記のとおり通ったが、**登録は呼び出しが終わっても番地が残る**ので話が別で、降ろす前に
  必ず手放す設計が要る。**モードレスなパレット（`IExtensionWebPalette`）の登録も同じ話**
  だと見ている（[Layout Dialogs「モードレス（非モーダル）なパレット」](Layout%20Dialogs.md)。
  実機未確認）。
- 長時間・多数回の読み直しでリークしないか。

## 参考

- [`plugin/README.md`](../plugin/README.md) — 実機確認プラグインの全体像と自動アップデート
- [Progress and Diagnostics](Progress%20and%20Diagnostics.md) — 例外境界・VW のバージョンは取れない
