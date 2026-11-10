# プラグインモジュールの読み込みと入れ替え — 計測記録

結論は [Findings「Plug-in Modules」](../Findings/Plug-in%20Modules.md)。ここには測った環境・
被験体・手順・実測の表・ダンプの読み方・切り分けの経緯を置く。節の見出しは Findings と同じに
してある。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

コンパイル済みプラグイン（`.vwlibrary` / `.vlb`）が**いつ読み込まれ、いつなら入れ替えられるか**。
そして「Vectorworks を起動したまま中身を差し替えられないか」という問い（issue #15）の調査。

## 拡張機能を 1 つも登録しないモジュールの常駐を確かめた（Windows・macOS）

（[issue #252](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/252)。
被験体は [vectorworks-plugin-command-line-interface](https://github.com/min-nano/vectorworks-plugin-command-line-interface)
の `cli`。実測の記録はあちらの [Windows の実機での実測](https://github.com/min-nano/vectorworks-plugin-command-line-interface/blob/98bfbb1e5745405cb208981271a2d8b8240ad224/docs/plugin/windows-measurements.md)
と [SDK の調査 0](https://github.com/min-nano/vectorworks-plugin-command-line-interface/blob/main/docs/sdk-research.md#0-拡張機能を-1-つも登録しないプラグインの読み込み)。）

被験体の `cli` はメニューも PIO もプラグインライブラリ関数も登録しない（`REGISTER_Extension` を
1 度も呼ばない）。`plugin_module_main` は `GS_InitializeVCOM(cbp)` のあと、`kPluginModuleDeinit` 以外の
action で 1 度だけ OS タイマーを始めて 0 を返す（`plugin_module_ver` と
`DefaultPluginVWRIdentifier` は定義する。下の「SDK の静的ライブラリをリンクするモジュールで足りなかった定義」）。

| | Windows（VW 2026 Update 6 x64・Windows 11 ARM64 の VM の x64 エミュレーション） | macOS（VW 2026 `31.6.0 (872399)`・macOS 27.0.1 arm64） |
| --- | --- | --- |
| ビルド | `0683df0` / `296a797`（`SetTimer(nullptr, 0, 250, proc)` の前に `GET_MODULE_HANDLE_EX_FLAG_PIN`） | `7b80014`（`CFRunLoopTimer` を `kCFRunLoopCommonModes` に。留めない） |
| 登録 | 無し（ソースに `REGISTER_Extension` が無い） | 同じ |
| 常駐 | 起動から 10〜50 秒前後で `cli.vlb` がモジュール一覧に載り、刻みが受け付ける `ping` が応えた（`running:true` から 0.2〜2.4 秒） | 有効化した後の起動で、ダイアログが開いている間から `ping` が応えた |
| 有効化の前 | 「無効」と示され、モジュール一覧に `cli.vlb` が無く、最初の刻みで作るスプールも作られなかった | 「無効」と示され、130 秒待ってもスプールは作られなかった |

- 新しく置いた最初の起動では「開発元不明のプラグイン」で「無効」にされ、プロセスのモジュール
  一覧に居らず、`plugin_module_main` も呼ばれなかった。
- **プラグインマネージャの「開発元」タブで、拡張なしのモジュールは「不明な開発元」の下に
  ファイル名だけが載った**（下に項目が並ばない）。
- 当初の「Windows では拡張なしだと常駐しない」という観測（留めない版）は、上の表の
  「有効化の前」とそっくり同じだった。
- **留めない「拡張なし」のモジュールが Windows の読み込み直しの対象になるかは確かめていない**
  （#253 で読み込み直しを見たのは、メニューとプラグインライブラリ関数を登録した殻）。

## Windows の VW の起動時のモジュールの読み込み直しと、OS に預けたコールバック

（[issue #253](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/253)。
Windows 11 ARM64 の VM（UTM）・VW 2026 は x64 のエミュレーション。）

### 実測

| 殻 | 起動 | 結果 |
| --- | --- | --- |
| `a8af333`（留めない・ウィンドウなしタイマー） | 10/17 に 4 回・10/18 朝に 5 回 | **9 回とも落ちた**（10/18 朝の 1 回目は CPU に負荷を掛けた回） |
| 同上 | 10/18 朝、上の前に 3 回 | 3 回とも起動した |
| 診断ビルド（留めない。`f6dd983`）・刻みごとにファイルへ書く | 2 | 2 回とも起動 |
| 同・元と同じ動き | 1 | **落ちた**（`0xc0150014`） |
| 診断ビルド（留めない。`8061038`）・タイマーなし | 2 | 2 回とも起動 |
| 同・ウィンドウなしタイマーで何もしない | 2 | 2 回とも起動 |
| 同・元と同じ動き | 2 | 2 回とも起動 |
| 同・メッセージ専用ウィンドウのタイマー | 2 | **2 回とも落ちた** |
| `5e969b7`（**留める**） | 6 | **6 回とも起動**（下記） |

落ちた回は **Windows の異常終了記録（Application の Event 1000）に残らなかった**。VW 同梱の
Sentry（sentry-native / crashpad）が先に掴み、ダンプを
`%APPDATA%\Nemetschek\Vectorworks\2026\Sentry\reports\*.dmp` に書いた。`VW User Log.txt` にも
その回は 1 行も残らなかった。**例外は下の `0xe0000001` の形**で、
こちらは Event 1000 に残り（`KERNELBASE.dll`）、Sentry のダンプは出なかった（WER の
`LocalDumps` を設定してあればそちらに出る）。

**読み込み直しの跡（3 つ）:**

- **殻の時計を作る関数が 1 プロセスで 2 回走った。** 関数は static の旗で 1 度しか動かない
  作りなのに、診断の記録には 0.6〜1.0 秒あけて 2 行ずつ残った（記録を取った 6 回とも）。
- **action の記録**（頼まれたときだけ書く。`plugin/src/ModuleMain.cpp`）は、留めない殻で
  `-1`・`-3` → 0.8 秒後に `-4`・`-5` で、**`-1` は 1 回きり・`-2` は無し**だった。
- **落ちた番地が「前の読み込みの番地 ＋ 自分の関数の RVA」になっている。** メッセージ専用
  ウィンドウの回は、USER32 がこちらのウィンドウへ `WM_TIMER`（`0x113`）を配ろうとして
  `AlignDistribute.vlb+0x30c50` を呼んで落ちた。ウィンドウ手続きの RVA は `0x40c50` で、
  差し引くと `0x7ffe5c0f0000`（64 KB 境界）——そこに今は `AlignDistribute.vlb`
  （`0x7ffe5c100000`〜）が入っており、今の殻は `0x7ffe42680000` と別の所にいた。
  2 件目も同じ形（`0x7ffe5cdd0c50` − `0x40c50` = `0x7ffe5cd90000`。今の殻は `0x7ffe43420000`）。

**留めない殻の落ち方（ダンプ 12 件）:**

- ウィンドウなしタイマー（9 件）: 主スレッドで Cineware（`cineware.dll`。
  `MaxonRenderDevice.vlb` から呼ばれる）が自前の `PeekMessageW` → `TranslateMessage` →
  `DispatchMessageW` のループを回している最中、こちらの `WM_TIMER`（`hwnd=0`・`lParam` は殻の
  `OnClock`）を配ろうとして、**`DispatchMessageW` を持っていたはずの `rdi` が `-1` になって
  いた**（`call rdi` で `0xffffffffffffffff` を実行）。`rdi` はループの前に 1 度だけ読み込まれる
  非揮発レジスタ。その前の周はダンプに残っていない。起動から 22〜35 秒。
- 同（1 件）: `0xc0150014`（`STATUS_SXS_CORRUPT_ACTIVATION_STACK`）。USER32 のコールバックの中。
- メッセージ専用ウィンドウ（2 件）: 上記の `AlignDistribute.vlb` 行き。

**留めた殻の実測（`5e969b7`。6 回起動して各 120 秒）:** 6 回とも起動し、閉じれば正常終了
（終了コード 0）した。action の記録は**すべての action が同じ番地で届いた**（例:
`-1 -3 -4 -4 -5 -4 -5 -4 -5 -2 -5` がすべて `0x7ffeba150000`。`-2` は閉じたとき）。

### #233 の `0xe0000001` との突き合わせ

（[issue #233](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/233)。
2026-10-24 に同じ Windows の VM で測った。殻はどれも Actions の成果物で、Plug-ins 直下に置いた。）

2026-10-15 に「殻を `4b2e59f` → `b125451` へ替えたら起動画面で毎回落ちる・Plug-ins 直下の
`VWPluginLibraryRoutines.h` / `.p` / `WorksheetFunctionOptionsRegistry.txt` を退けると起動する」
を踏み、プラグインライブラリ関数の定義のずれた書き出しが原因と見ていた。2026-10-24 に
3 ファイルと殻を組み替えて測り直した（`KERNELBASE.dll`・`0xe0000001`・オフセット `0x1f79e8`）:

| # | 3 ファイル | 殻 | 結果 |
| --- | --- | --- | --- |
| T2 | `4b2e59f` が書いた、ずれたもの | `b125451`（留めない） | **`0xe0000001`**（147 秒） |
| T2b | 同上 | 同上 | **`0xe0000001`**（53 秒） |
| T5 | **無し** | 同上 | **`0xe0000001`**（56 秒） |
| T3 | ずれたもの | `4b2e59f`（留めない。書いた本人） | `0xc0000005`（番地 `0xf90013e4f9400110`。93 秒） |
| T3b | 同上 | 同上 | 起動した |
| T4 | ずれたもの | `fd379cf`（**留める**・定義を直した） | 起動した（3 ファイルは今の定義で書き直された） |
| T6 | T4 が書いたもの | `1aa43cc`（留める） | 起動した |

- **3 ファイルが無くても落ちた**（T5）。ずれた 3 ファイルのまま留めた殻へ替えると起動した（T4）。
- **T2 のダンプ**（WER の `LocalDumps`。Sentry のダンプは出なかった）で、`RaiseException` の
  手前のスタックは上の「ウィンドウなしタイマー」と同じ形だった: `MaxonRenderDevice.vlb` →
  `cineware.dll` のメッセージループ → `user32` → **`3DS Library.vlb+0x53390`** → `ucrtbase` →
  `Vectorworks2026.exe+0x20058a3` → `KERNELBASE!RaiseException`。
- **番地の勘定。** `b125451` の殻で `SetTimer` に渡しているタイマー手続きの RVA は
  `0x43390`（`.vlb` の `SetTimer` の呼び出しの手前の `lea r9, [rip+…]` から読んだ）。呼ばれた
  番地 `0x7ffe894e3390`（＝ `3DS Library.vlb` の `0x7ffe89490000` ＋ `0x53390`）から引くと
  `0x7ffe894a0000`——**64 KB 境界**。今の殻は `0x7ffe6fdc0000` にいた。
- `b125451`（手続きの RVA `0x43390`）は 3 回とも `0xe0000001`、`4b2e59f`（`0x42fb0`）は 1 回落ち
  1 回起動した。ダンプを読んだのは T2 の 1 回だけで、ほかの回の番地は見ていない。2026-10-15 の
  「3 ファイルを退けたら起動した」回の番地は残っていない。

### 結果が食い違って見えたところの切り分け

- 置いた・退けたで結果が変わって見えたが、同じ中身でも落ちたり落ちなかったりした
  （issue の 5 と 6 の食い違い）。
- **起動中に VM ごと落ちたこともある**（10/16 18:45・10/18 11:52。バグチェック `0xD1` が 2 回とも
  `viogpudo.sys+0x180b8`——UTM の virtio GPU ドライバ）。どちらも VW が落ちた・終わった前後
  だった。上の直しとの関係は確かめていない。
- **macOS（読み込み直しの有無）:** mac の殻は `CFRunLoopTimer` を留めずに作っている。action の
  記録（殻の番地つき。殻 `3889ee91a052`＝`fd379cf`・macOS 27.0.1 arm64・VW 2026 `31.6.0 (872399)`）で、
  **5 回起動して 5 回とも、起動から終了までのすべての action が同じ番地で届いた**:
  `-1 -3 -4 -4 -5 -4 -5 -4 -5`、閉じたとき `-2 -5`。`-3` から `-4` までは 0.07〜0.1 秒
  （Windows で読み込み直したときは 0.8 秒あき、番地が変わった）。番地は起動ごとに違った
  （`0x715dec0000` / `0x7160b18000` / `0x7155338000` / `0x715eac0000` / `0x715ec8c000`）。
  起動後の `vmmap` でも殻は 1 つだけ読み込まれていた。
- **閉じたときは `-2` の後にもう 1 度 `-5` が来た**（mac で 4 回。Windows の留めた殻の記録も
  同じ `… -2 -5`）。
- 同じ `Plug-ins` に入れていた別のプラグイン（`cli`。`SetTimer` を使う）だけで落ちた回
  （issue の 5）はダンプが残っておらず、確かめていない。

## OS タイマーを持つ常駐プラグイン（留めたモジュール）の有無と終了時の落ち方（Windows）

（[issue #257](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/257)。
VW 2026 Update 6（Build 872399）x64・Windows 11 Pro ARM64 の VM（UTM）の x64 エミュレーション・
描画は WARP。2026-10-26 に測った。）

**`SetTimer` を持ち `GET_MODULE_HANDLE_EX_FLAG_PIN` で留めた常駐プラグインを入れても、
利用者が閉じたときの終わり方は入れないときと変わらなかった。** ホーム画面まで進んでから
閉じた 82 回（プラグインあり 63・なし 19）がすべて終了コード 0 で終わり、閉じる指示から
プロセスが終わるまでの時間にも差は無かった。

以前 `cli`（`296a797`）で 11 回中 3 回（27%）出た終了時の `0xC0000005`（すでに外された
`libcef.dll` の中を実行していたスレッドで落ちた）は、**同じ版を 40 回閉じても再現しなかった**。
当時と違うのは、起動からホーム画面までが
36〜308 秒とばらついていたこと（今回は 45〜91 秒）と、閉じる時機をホーム画面ではなく
起動からの決め打ち（240 / 360 秒）で決めていたこと（当時の記録は cli の側の
[Windows の実機での実測](https://github.com/min-nano/vectorworks-plugin-command-line-interface/blob/98bfbb1e5745405cb208981271a2d8b8240ad224/docs/plugin/windows-measurements.md)）。

### 実測

起動し、`VW User Log.txt` にホーム画面の表示（`Dialog opened, Section = HomeScreen`）が出るのを
待って、メインウィンドウへ `CloseMainWindow`（メニューの「終了」と同じ）を送った。終了コードは
起動直後に開いておいたハンドルで読んだ。構成は 1 回ずつ交互に回した（時間帯の偏りを避けるため）。
プラグインは `Plug-ins` の外へ退けて出し入れした。

| 構成 | 閉じる時機 | 回数 | 終了コード 0 | 閉じる → 終わる |
| --- | --- | --- | --- | --- |
| なし | ホーム画面の 90 秒後 | 10 | 9（残る 1 回は VM ごと止まった。下記） | 26.5〜30.6 秒 |
| VwSdkProbes の殻だけ | 同 | 10 | 10 | 26.8〜32.6 秒 |
| `cli`（`296a797`） | 同 | 10 | 10 | 25.6〜33.3 秒 |
| `cli`（`0683df0`） | 同 | 10 | 10 | 25.2〜35.1 秒 |
| なし | ホーム画面が出た直後 | 10 | 10 | 28.4〜49.5 秒 |
| `cli`（`296a797`） | 同 | 10 | 10 | 27.8〜47.1 秒 |
| `cli`（`296a797`）・3 秒ごとに `vw2026 call ping` | 起動から 360 秒 | 10 | 10 | 27.8〜28.6 秒 |
| `cli`（`296a797`）・要求なし | 同 | 10 | 10 | 27.1〜28.6 秒 |
| VwSdkProbes の殻だけ（下の action の記録） | ホーム画面の 90 秒後 | 3 | 3 | 26.1〜33.6 秒 |

- 常駐プラグインはどの回も読み込まれていた（ホーム画面の前、起動から 6〜14 秒でモジュール
  一覧に載った）。ping の回は 1 回あたり 74〜79 回応え、刻みの中でスプールを読み書きしていた。
- **Sentry のダンプも WER のダンプも 1 件も出なかった。**
- **ホーム画面が出た直後に閉じると、タイトルの無いダイアログ（`#32770`）が開いていることがあり、
  そのときは終わるまでに 38〜50 秒かかった**（20 回中 5 回。プラグインあり 2・なし 3）。
  無いときは 28〜37 秒。
- **そのうち 1 回（`cli` あり・47 秒）は、本体が終了コード 0 で終わった後に CEF の renderer
  （`Vectorworks2026.exe --type=renderer`）が残り、殺せなくなった**——スレッドが 1 本だけ
  `WaitReason=Executive` で止まり、`Stop-Process` も `taskkill /F` も効かない（終了の途中で
  カーネルの中から戻ってこない）。VM を再起動するまで残った。次の起動の邪魔にはならなかった
  （以後の 27 回は、下記の打ち切った 2 回を除いて変わらず終わった）。
- **起動の途中（ホーム画面の前。起動から 40 秒）に送った `CloseMainWindow` は捨てられた。**
  ホーム画面まで進んだ後も終わらず、240 秒待って打ち切った（2 回）。
- **VM ごと止まった 1 回**は、プラグインなしでホーム画面に置いていた最中だった（ログは起動から
  240 秒で途切れ、Windows は「予期しないシャットダウン」を記録。バグチェックの記録は無い）。

**利用者が閉じる道の action の記録。** 殻に action を
書かせた（頼み札のある間だけ。`plugin/src/ModuleMain.cpp`）3 回で、`CloseMainWindow` の
7〜14 秒後に -2、その 0.5 秒後に -5、-2 の 19〜21 秒後にプロセスが終わった。刻みから
`CloseAllFilesAndQuitVectorworks` を呼んだ道（[Timers and Notifications](Timers%20and%20Notifications.md)
4 の 9。戻ってから 8〜10 秒で -2、その約 20 秒後に `atexit`）と同じ並び。番地は -1 から -2 まで
同じだった。この道の刻みは [Timers and Notifications](Timers%20and%20Notifications.md) 4 の 9「利用者が
閉じる道の刻み」——`kNotifyBeforeApplicationQuit` の後には来なかった（11 回。#269）。

**その #269 の 11 回のうち 1 回は、終了の途中に `libcef.dll_unloaded` の `0xC0000005` で
落ちた**（-2・-5 の後、`atexit` の前。閉じる指示の 44.5 秒後。WER のフルダンプが残った）。
上の 82 回で再現しなかった `cli` の落ち方と同じモジュールである。この回は、ブリッジが図面を
閉じて**ホーム画面へ戻った後**に閉じていた（上の 82 回は、起動してホーム画面から閉じた
だけ）。同じ形の回は 5 回中 1 回で、入っていたのは `VwSdkProbes` だけ。

### 計測でつまずいたところの記録

- **「開発元不明のプラグイン」のダイアログが出たまま閉じると、`CloseMainWindow` は効かなかった。**
  最初に組んだ計測ではプラグインありの 5 回がすべてこれで止まった（全部有効でも前回から
  約 24 時間たつと出る。下の「開発元不明のプラグイン」）。計測では、VW を止めた状態で
  `SavedSettingsUser.xml` の `<UnknownPluginsDialog><Date>` を今にし、有効化の記録
  （`ExceptionsMissingCredentials.bin`）を全部入りの写しから戻してから起動した（プラグインを
  出し入れしながらダイアログを閉じると、居ないものの有効化が消える。同じ節）。
- **`Get-Process Vectorworks2026` は CEF の子プロセスも拾った。** 子（`--type=renderer` /
  `gpu-process` など）も同じ `Vectorworks2026.exe` で、先頭の 1 つを取ると主窓の無い子に
  当たることがあり、`CloseMainWindow` は黙って何もしなかった（主窓のハンドルが 0。
  #269 で 4 回踏んだ）。
- **`VW User Log.txt` の `After SetUpCOM` の後にも、ライブラリの準備が 20 秒ほど続いた。**
- **`VW User Log.txt` は前の起動の行を持ち越し、途中で短く書き直されることがあった**（ログの
  送信の後）。「起動より後に書き換わった」「起動前の長さより後ろ」のどちらで絞っても、
  前の起動の `HomeScreen` を拾うか、今回の行を読み落とした（#270 で両方踏んだ）。また VW は
  書き込みのために開いたままで、`File.ReadAllLines` は共有違反で失敗した。

### 新しい図面の最初の描画を 30 回測った（Windows）

（[issue #270](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/270)。
上と同じ VM・VW。2026-10-27 に測った。）

**ホーム画面の「新規用紙」から新しい図面を開き、最初の描画を待ってから閉じる、を 30 回
（プラグインなし・`cli`（`296a797`）・`VwSdkProbes` の殻だけ、を 1 回ずつ交互に 10 巡）
回して、描画中の落ち（`MaxonDocumentBuilder.vlb` の番地 0 の読み出し）は 1 度も出なかった。**
閉じたときもすべて終了コード 0、Sentry・WER のダンプは 0 件。cli の側で出た 1 回（2 回中 1 回）は、
どの構成でも再現しなかった。

| 構成 | 回数 | 落ち | ホーム画面まで | クリック → `PaintArea begin` | 閉じる → 終わる |
| --- | --- | --- | --- | --- | --- |
| なし | 10 | 0 | 73〜98 秒 | 8〜22 秒 | 27.3〜36.4 秒 |
| `cli`（`296a797`） | 10 | 0 | 74〜130 秒 | 13〜21 秒 | 28.4〜34.2 秒 |
| `VwSdkProbes` の殻だけ | 10 | 0 | 78〜134 秒 | 9〜16 秒 | 28.1〜35.7 秒 |

- 手順: 起動 → ログの `Section = HomeScreen` の 10 秒後に、主窓を前へ出して「新規用紙」の
  位置をクリック（`SetCursorPos` ＋ `mouse_event`）→ 60 秒見張る（プロセスが消えるか・ダンプが
  増えるか）→ `CloseMainWindow`。無題の図面は何もしていなければ保存の確認を出さなかった。
- cli の側で落ちた当時は描画が WARP（VM のソフトウェア描画）だった。この VM はほかでも描画の
  スレッドで落ちている（[Viewports](Viewports.md) の #248。`d3d10warp.dll` の番地 0）。
- **「新規用紙」の最初のクリックは、30 回中 21 回で効かなかった**（ホーム画面の表示から
  10 秒後でも、図面が開かない）。20 秒待って 2 度目を押すと開いた。ホーム画面（CEF）は
  `HomeScreen` のログの後もしばらく入力を受け付けていなかった。
- この計測の外で 1 回、**「新規用紙」で開いてから閉じたときに `libcef.dll_unloaded` の
  `0xC0000005` で落ちた**（`VwSdkProbes` の殻だけ。手で同じ操作をした回。WER のフルダンプ）。
  上の #269 の 1 回とオフセットまで同じ（`+0x3699c7e`）。計測の 30 回では出ていない。

## 「開発元不明のプラグイン」のダイアログが出る条件と有効化の記録の実測（Windows・macOS）

（[issue #258](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/258)。
VW 2026 Update 6（Build 872399）・Windows 11 ARM64 の VM（UTM）・x64 エミュレーション。
被験体は `%APPDATA%\Nemetschek\Vectorworks\2026\Plug-ins\cli\cli.vlb`（署名なし。
[vectorworks-plugin-command-line-interface](https://github.com/min-nano/vectorworks-plugin-command-line-interface)）。
2026-10-21 に 12 回起動して確かめた。macOS も 2026-10-24 に同じ手順で測った——下の「macOS」。）

### 実測（起動ごと）

「出た」は `VW User Log.txt` の `Dialog[OPEN]: ID-10002 (UnknownDeveloperPlugIns)` と画面で、
「読み込み」はホーム画面が出た後に VW のプロセスのモジュール一覧に `cli.vlb` があるかで見た。
`<Date>` は下記の `SavedSettingsUser.xml` の値（VW を止めて書き換えたものを含む）。

| # | 前提 | ダイアログ | cli の表示 | 読み込み |
| --- | --- | --- | --- | --- |
| 1 | 何も変えない（記録あり。`<Date>` は 3 日前の 10/18 21:19） | **出た** | 有効 | された |
| 2 | 1 の直後にそのまま | 出ない | — | された |
| 3 | `<Date>` を 24 時間 20 分前に | **出た** | 有効 | された |
| 4 | `<Date>` を 6 時間前（同じ日）に | 出ない | — | された |
| 5 | `<Date>` を前日の 23:59（約 18 時間前）に | 出ない | — | された |
| 6 | `<Date>` を 23 時間前に | 出ない | — | された |
| 7 | 記録を退ける（`<Date>` は今） | **出た** | **無効** | **されない** |
| 7' | 7 で「有効化」→「再起動」した後の起動 | 出ない | — | された |
| 8 | フォルダを `cli` → `cli2`（記録は `cli` のもの） | **出た** | **無効** | **されない** |
| 9 | 8 で有効化せず「続ける」→ `cli` に戻す | **出た** | **無効** | **されない** |
| 10 | 9 の後、VW を止めて 7 で退けた記録を戻す | 出ない | — | された |
| 11 | `cli.vlb` の末尾に 16 バイト足す（SHA-256・サイズ・日時が変わる） | 出ない | — | された |
| 12 | `cli2` にして有効化 →「再起動」した後の起動 | 出ない | — | された（`cli2\cli.vlb`） |

- `<Date>` を 6 時間前・約 18 時間前・23 時間前にした 4〜6 は出ず、24 時間 20 分前にした 3 は
  出た。5 は前日の日付だが出なかった。
- 7〜9 は、どれも前回から 24 時間以内の `<Date>` で出た（無効のものがあった）。
- ボタンごとの記録の書き方:

  | 操作 | `ExceptionsMissingCredentials.bin` | `<Date>` | その後 |
  | --- | --- | --- | --- |
  | 「続ける」（全部有効のとき。1・3） | 書き直す（同じバイト列） | 押した時刻になる | そのまま起動を続ける |
  | 「有効化」 | まだ書かない | — | 「続ける」が「再起動」に変わる |
  | 「再起動」（7・12） | 書き直す | 変わらなかった | ホーム画面まで進んでから終了し、自動で起動し直す |
  | 「続ける」（無効が残るとき。8） | 書き直す（**8 バイト＝空**） | 変わらなかった | そのまま起動を続ける（無効のものは読まれない） |
  | 「終了」（9） | 書き直す（8 バイト） | 変わらなかった | ホーム画面まで進んでから VW を終了する（ログは `Return=2`） |

### ダイアログの最中に刻みから図面を扱った実測（Windows）

（[issue #264](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/264)。
VW 2026 Update 6・Windows 11 ARM64 の VM。2026-10-26。`Plug-ins` には有効化済みの
`VwSdkProbes` だけを置き、`<Date>` を 2 日前にして起動した——全部有効でも出る回。
`probes/runtime/startup-dialog-docs/`、ビルド `b0a3ea33ef37`。macOS は下の「mac で共通モード…」。）

**全部有効で出たダイアログの最中、OS タイマー（`SetTimer(nullptr, …)`）の刻みは届き、
そこから図面を作る・足す・保存する・閉じる・開くがすべて通った。** ダイアログを閉じた後も、
作った図面と保存したファイルは残っていた。

- 刻みは届いた。殻の MCP ブリッジ（250 ms の `SetTimer`）の生存の印は 2 秒ごとに更新され続け、
  `vw_ping` は 1 秒で応えた（その時点の図面は 0 枚）。
- `vw_run_probe` が刻みの中で `OpenDocumentPath(nullptr, false)` で新規の図面を開き、
  プローブが続けて次を行った——どれも成功し、落ちなかった。
  | 手順 | 結果 |
  | --- | --- |
  | 始め | 図面 1 枚（ブリッジが開いた無題）・`IsCurrentlyBuildingAnUndoEvent()` は `no` |
  | `SetUndoMethod(kUndoSwapObjects)` ＋ `NameUndoEvent` → 矩形 → `AddAfterSwapObject` → `EndUndoEvent` | 開いた後 `yes`、閉じた後 `no`。どれも成功 |
  | `SaveActiveDocumentPath`（一時ディレクトリの `.vwx`） | 0（成功）・33255 バイト。前面の図面がそのパスになり `saved=yes` |
  | `CloseDocument` → `OpenDocumentPath(そのファイル)` | 0 枚 → 1 枚。名前で矩形を引き直せた |

  ダイアログの無い起動で同じ手順を走らせた結果と、ファイルの大きさ（32607 / 33255 バイト）
  以外は同じだった。所要はダイアログの最中が 38 秒、無いときが 7 秒（起動の途中で新規の
  図面を開くのが重い）。
- そのときのプロセスの窓（可視のトップレベル）は、ダイアログ「開発元不明のプラグイン」
  （`#32770`・有効）、題の無い `#32770`（無効。起動画面）、主窓（**無効**）の 3 つ。主窓の
  リボンと開いた図面のタブは、起動画面とダイアログの後ろに既に描かれていた。
- **ダイアログを閉じた後**: 開き直した図面は開いたまま、保存したファイルもそのまま。VW は
  その上にホーム画面を出して起動を終えた（図面を開いた状態でも出る）。ブリッジはそのまま
  応え続けた。

**ボタンは UI Automation では押せなかった。Win32 の `BM_CLICK` なら押せた。**

- UI Automation では、ダイアログのボタンが **`Button` ではなく `Pane`** として見え、
  **パターンを 1 つも持たない**（`InvokePattern` も無い）。
- Win32 では普通の子窓: 「続ける」＝`Button`・ID 1、「終了」＝ID 2、「有効化」＝ID 7、
  「ファイルの場所を開く...」＝ID 8。**「続ける」へ `BM_CLICK` を送ると、3 秒以内に
  ダイアログが閉じ**、記録はそのとき一覧にあったものだけで書き直された（上の表の「続ける」と
  同じ。`<Date>` は VW を終えるまでファイルへ書かれない）。ダイアログは、VW のプロセスの
  可視の `#32770` で題が「開発元不明のプラグイン」のものとして見つけた。
- この回は、起動を待つハーネス
  （「書き換え時刻が起動より新しく、`Section = HomeScreen` を含む」で判定）が**ダイアログの
  最中に「ホーム画面」と誤判定した**。直後のファイルは、ダイアログの最中にログが送信されて
  （`Log uploading complete`）**10 行に縮んでおり**、`HomeScreen` の行は無かった。

### mac で共通モード・既定のモードの刻みから図面を扱った実測

（[issue #264](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/264)。
VW 2026 Update 6（Build 872399）・macOS 27.0.1。2026-10-27。`Plug-ins` には有効化済みの
`cli`（`0b206b8`）と `VwSdkProbes` を置き、VW を止めて `<Date>` を 2 日前にして起動した。
ローカルの Claude Code から MCP ブリッジで走らせた。ビルド `442411571f73`。）

**ダイアログは `applicationDidFinishLaunching` の中の `-[NSApplication runModalForWindow:]`
で回っていた**（`sample` で採ったメインスレッドのスタック。開く道は AppleEvent の open →
`_postDidFinishNotification` → VW → `runModalForWindow:` → `_doModalLoop:peek:`）。ランループの
モードは **`NSModalPanelRunLoopMode`**（下のプローブの `CFRunLoopCopyCurrentMode`）。

- **既定のモードにだけ登録したタイマーは、ダイアログの間 1 度も刻まなかった。** 殻の MCP
  ブリッジ（`kCFRunLoopDefaultMode`）の生存の印は、ダイアログが出る 5 秒前の値のまま
  **639 秒**更新されなかった。「続ける」で閉じた直後から刻み直した。
- **共通モードのタイマーは刻んだ。** 同じプロセス（同じ pid）の `cli`（`CFRunLoopAddTimer(…,
  kCFRunLoopCommonModes)`）は、同じダイアログの最中に `ping` へ 0.07〜0.17 秒で応えた。
- 殻のブリッジを共通モードに登録し直して（スプールの旗 `common-modes`。
  [#276](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/276)）、
  ダイアログの最中に `vw_run_probe` で `startup-dialog-docs` を走らせた——**Windows と同じ
  手順がすべて通り、落ちなかった**（所要 3.1 秒）。
  | 手順 | 結果 |
  | --- | --- |
  | 始め | 図面 1 枚（ブリッジが `OpenDocumentPath(nullptr, false)` で開いた無題）・`undo=no`・モード `NSModalPanelRunLoopMode` |
  | `SetUndoMethod` ＋ `NameUndoEvent` → 矩形 → `AddAfterSwapObject` → `EndUndoEvent` | 開いた後 `yes`、閉じた後 `no`。どれも成功 |
  | `SaveActiveDocumentPath`（一時ディレクトリの `.vwx`） | 0（成功）・34722 バイト・`saved=yes` |
  | `CloseDocument` → `OpenDocumentPath(そのファイル)` | 0 枚 → 1 枚。名前で矩形を引き直せた |

  どの時点も `IsCurrentlyBuildingAnUndoEvent()` は（自分で開いた間を除き）`no`——利用者が
  開いた環境設定などの最中（`yes`。Timers and Notifications の 9）とは違い、Windows と同じ。
  ダイアログの最中は、開いた図面の窓は窓の一覧（AX）に出ていなかった（ダイアログだけ）。
- **ダイアログを閉じた後**（約 2 分後に `AXPress` で「続ける」）: 開き直した図面は開いたまま
  （`GetOpenFilesList` 1 枚・`GetCurrentLayer()` が「レイヤ-1」）、保存したファイルも同じ
  大きさで残った。VW はその上にホーム画面を出して起動を終え、ブリッジはそのまま応え続けた。
  Windows と同じ。
- **ボタンは AX の `AXPress` で押せた**（#258 と同じ）。押した後の記録はバイト単位で
  押す前と同じに書き直され、`<Date>` は押した時刻（分まで）が VW の終了時に書かれた——
  手で押したときと同じ（上記の表）。
- **ダイアログが出たことは `VW User Log.txt` の `Dialog[OPEN]: ID-10002 (UnknownDeveloperPlugIns)`
  の行に出た**（日本語の表示でしか見ていない）。Windows と同じく、ダイアログの最中にログが
  送信されてファイルが縮んだ（この回は 10 行）。

### 置き場所と形

- **有効化の記録**: `%APPDATA%\Nemetschek\Vectorworks\2026\Settings\ExceptionsMissingCredentials.bin`。
  先頭 8 バイトが平文の長さ（リトルエンディアン）、続いて 16 バイト単位の暗号文。
  - `cli` 1 本のとき長さ 82（0x52）＝ `C:\Users\…\Plug-ins\cli\cli.vlb` の 81 文字＋終端。
    `cli2` にすると 83（0x53）になった。
  - `cli` と `cli2` の記録は**暗号文の先頭 64 バイトが一致**し、パスが食い違う文字（73 文字目）を
    含むブロックから先が違った。7 で作り直した記録は、退けたものとバイト単位で同一だった。
  - 何も有効でなければ 8 バイト（長さ 0）。**プラグインが 2 本以上のときの並べ方は見ていない**
    （有効にしたのは 1 本ずつ。[#260](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/260)）。
- **間引きの基準**: `Settings\SavedSettingsUser.xml` の
  `<Vectorworks><UnknownPluginsDialog><Date>21;10;2026;17;14</Date>`（日;月;年;時;分。
  ローカル時刻。同じ要素にダイアログの位置と列幅も入る）。
- **レジストリ**（`HKCU\Software\Nemetschek\Vectorworks 31` 以下）には何も無かった。

### 「開発元」とは何か

- VW 同梱の `Plug-Ins\` に**クレデンシャル**が置いてある: `CredentialsVectorworks.vlb`・
  `CredentialsComputerWorks.vlb`・`CredentialsExtragroup.vlb`・`CredentialsMaxon.vlb`・
  `CredentialsVectorworksJapanJW.vst`。中身は DLL ではなく、魔法の数 `de 4c 34 97`・
  長さ・**ECDSA P-384 の署名**（DER）・長さ・JSON の順で、JSON は
  `{"developer":{"contact":…,"issueDate":…,"name":"Vectorworks Inc.","web":…},"files":["3DS Library",…,"Vectorworks Remote/Vectorworks Remote",…]}`。
  `files` は **`Plug-Ins` からの相対パス（拡張子なし）**の一覧。
- `Vectorworks2026.exe` には P-384 の公開鍵（PEM）、`developer` / `contact` / `issueDate`、
  `IExtendedPropsSupport::AddCredentials`、`_AddCredentialsLog.txt` と
  `ADDED CREDENTIALS TO` / `FILE CONTAINS SIGNATURE` などの文字列がある。
- ダイアログの文言は「開発者は Vectorworks パートナーシッププログラムの一員ではありません」、
  同梱ヘルプ（`VWHelp\…\Start\Third-party_plug-in_security.htm`「サードパーティプラグインの
  セキュリティ」）は「パートナーシップを申請しプラグインの審査を受けて…認証を得る」。
  有効にした開発者のプラグインは「プラグインマネージャ」の「開発元」タブで管理するとある。
- `Vectorworks2026.exe` と同梱の `.vlb` / `.dll` の
  どれも `WinVerifyTrust` を含まない（含むのは Cineware・CEF・PhysX・Redshift・
  インストーラだけ）。`CryptQueryObject` / `CertGetNameStringA` は exe にあり、同じ所に
  curl の schannel の文字列が並んでいる。**署名して試してはいない**（[#260](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/260)）。

### SDK

`SDK Index/` に `credential` / `UnknownPlugin` / `UnknownDeveloper` / `ExtendedPropsSupport` /
`AddCredentials` / `partner` は 1 件も無い。

### macOS

（VW 2026 31.6.0（Build 872399）・macOS 27.0.1・arm64。被験体は
`~/Library/Application Support/Vectorworks/2026/Plug-ins/cli/cli.vwlibrary`（アドホック署名）。
2026-10-24 に 11 回起動して確かめた。見方は Windows と同じで、「読み込み」は `lsof -p` に
`cli.vwlibrary/Contents/MacOS/cli` が出るか。）

結果は Windows と同じ形だった。違ったのは置き場所と、空の記録の先頭 8 バイトだけ。

| # | 前提 | ダイアログ | cli の表示 | 読み込み |
| --- | --- | --- | --- | --- |
| m1 | `<Date>` を 1 時間前に（記録あり） | 出ない | — | された |
| m2 | **`install` の手順で入れ替え**（下記）。`<Date>` は 1 時間前 | **出ない** | — | **された** |
| m3 | `<Date>` を 23 時間前に | 出ない | — | された |
| m4 | `<Date>` を 24 時間 20 分前に | **出た** | 有効 | された（開いている間も） |
| m5 | `<Date>` を 23 時間 55 分前に | 出ない | — | された |
| m6 | `<Date>` を 24 時間 5 分前に | **出た** | 有効 | された |
| m7 | フォルダを `cli` → `cli2` | **出た** | **無効** | **されない** |
| m8 | m7 で有効化せず「続ける」→ `cli` に戻す | **出た** | **無効** | **されない** |
| m9 | m8 で「終了」→ VW を止めて m6 の後の記録を戻す | 出ない | — | された |
| m10 | `cli2` にして有効化 →「再起動」した後の起動 | 出ない | — | された（`cli2/cli.vwlibrary`） |

- **m2 の入れ替え**は cli の `install` と同じ手順で、VW を止めてから
  `<Plug-ins>/cli` を `~/Library/Application Support/vectorworks2026-cli/old/cli` へ退け、
  `…/staging/cli`（CI の別ビルド。`5fcf0f3` → `0b206b8`）を `<Plug-ins>/cli` へ置き、
  `old/cli` を消した。フォルダの inode・実行ファイルの inode・作成日時・SHA-256 が
  すべて変わり、パスだけが同じ。**有効のまま、ダイアログも出なかった。**
- m5（23 時間 55 分前）は出ず、m6（24 時間 5 分前）は出た。
- **記録**: `~/Library/Application Support/Vectorworks/2026/Settings/ExceptionsMissingCredentials.bin`。
  cli 1 本のとき 104 バイトで、先頭 8 バイトが 91＝`/Users/…/Plug-ins/cli/cli.vwlibrary`
  の 90 文字＋終端、続いて 96 バイト（16 の倍数）。長さはバンドルのパスに合い、中の
  実行ファイル（`Contents/MacOS/cli`）のパスには合わない。
- ボタンごとの書き方も Windows と同じだった:
  - 「続ける」（全部有効。m4・m6）は記録を**同じバイト列で**書き直し、`<Date>` を押した
    時刻にした（ファイルに出るのは終了時の保存で、押した直後の XML はまだ古い値）。
  - 「続ける」（無効が残る。m7）と「終了」（m8）は記録を **8 バイト**に書き直し、`<Date>` は
    変えなかった。**ただし先頭 8 バイトは 0 ではなく**、`0x7b84ce7960`・`0x76d47c15a0` と
    書くたびに違う値だった（Windows は長さ 0）。m8 の起動で cli は「無効」だった。
  - 「有効化」（m10）は押した時点では記録を書かず（mtime もバイト列もそのまま）、
    「続ける」が「再起動」に変わった。「再起動」は記録を書き直し（長さ 92＝`cli2` のパス
    ＋終端）、`<Date>` は変えず、ホーム画面まで進んでから自動で起動し直した。
- **「有効化」ボタンは実際のクリックで行を選んだときだけ押せるようになった。**
  アクセシビリティ API で行を選んでも（`AXSelected`）灰色のまま（[cli の実測](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/258#issuecomment-6263987581)
  と同じ）。「続ける」「終了」は `AXPress` で押せた。
- [cli の実測](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/258#issuecomment-6263987581)
  では、入れたばかりの VW で「有効化」→「再起動」の 2 回目も**ダイアログが出た**（cli は
  「有効」で読み込まれた）。「再起動」は `<Date>` を書かなかった。m10 は `<Date>` が 9 分前で、
  出なかった。
- 同じ VW の 10/19〜10/23 のログ: 10/19 16:04 に「続ける」で閉じた
  時刻がそのまま `<Date>` に残り、約 3 日後の 10/22 の起動で出たダイアログを「終了」
  （`Return=2`）で閉じたときは、記録だけ書き直されて `<Date>` は変わらなかった。

## プラグインのフォルダに「モジュール以外のもの」を置いた実測と、モジュールを読む場所（Windows・macOS）

（[issue #277](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/277)。
プローブ `plugin-folder-extras`。macOS は VW 2026 `31.6.0 (872399)`・macOS 27.0.1 arm64・
2026-10-28・ローカルの Claude Code から MCP ブリッジで、本体のビルド `1544564efe53`。
Windows は同じ VW・Windows 11 ARM64（x64 エミュレーション）の VM・2026-10-29・
ローカルの Claude Code が画面操作でメニューから、本体のビルド `230da8d0a5d5`。）

`<Plug-ins>/cli/` のようにモジュールをフォルダへ入れ、その隣に CLI の実行ファイル・フック・
JSON・スプールを置く形（cli の段 3）を確かめた。

### macOS

- **警告は出なかった。** 起動ごとの `VW User Log.txt` / `VW User Log Sent.txt` で、そのセッションの
  `Dialog` の行はホーム画面の開閉だけだった。INFO 以外の行（`NNAExceptionHandlerDelegate`
  10 件・`RefNameTableHandle::IsValid failed` 2 件）は**試料を置く前の起動にも出ていたもの**
  である——前者（修飾キーの `NSEvent` の例外）は直前の 5 回の起動すべてで 10 件ずつ、後者は
  5 回のうち 2 回で 4 件ずつ出ていた。
  起動直後の VW の窓もホーム画面だけだった（3 回目の起動で窓の一覧を見た）。
- **書き換え・移し・消しは無かった。** 試料を作ってから**起動を 2 回またいで**、17 件
  （試料 14・cli の実物 3）の inode・大きさ・更新時刻（ns）・中身のハッシュ・mode が
  すべて同じだった。
- **開いたままのものは無かった。** 起動後のプロセスのファイル記述子（`PROC_PIDLISTFDS`）のうち
  Plug-ins の下のものは、**cli 自身が持つ `cli/spool/bridge.lock` の 1 つだけ**（3 回とも）。
  写像（`PROC_PIDREGIONPATHINFO` で全領域を舐めた）も、Plug-ins の下は 2 つのモジュールの
  実行ファイルだけだった。
- **読み込まれた場所:** `_dyld_get_image_name`・
  `dladdr(イメージの先頭番地).dli_fname`・`realpath` がどれも
  `…/Plug-ins/cli/cli.vwlibrary/Contents/MacOS/cli` を返し、写像されているファイルの inode
  （615865）がそのパスを `stat` した inode と一致した（殻 `VwSdkProbes` も同じ）。段 3 の cli
  （`39edfc9`）は `dladdr` で求めた `<Plug-ins>/cli/spool/bridge.lock` を開いていた。

置いた試料（`<Plug-ins>/cli/p277/` の下。cli の `spool/` に入れなかったのは、cli が
`*.req.json` を処理してしまうため）:

| 種類 | 試料 |
| --- | --- |
| 実行ファイル | `bin/vw2026`（拡張子なしの Mach-O。`/bin/echo` の写し・755） |
| フック | `hooks/post-install.sh`（755）・`hooks/post-install.ps1` |
| JSON | `build.json`・`vw2026-install.json` |
| スプール | `spool/bridge.lock`（空）・`p277-0001.req.json`・`p277-0001.res.json`・`p277-0002.wait`（空）・`p277-0002.res.json.tmp`（JSON の書きかけ） |
| cli の実物（深さ 1） | `bin/vw2026`（7.8 MB の Mach-O）・`build.json` |

| 起動 | pid | 台帳との違い | Plug-ins の下の fd | 警告 |
| --- | --- | --- | --- | --- |
| 試料を作った回（19:45） | 4506 | —（台帳を作った） | `cli/spool/bridge.lock` | 無し |
| 再起動 1（19:47） | 4717 | 17 件とも同じ | `cli/spool/bridge.lock` | 無し |
| 再起動 2（20:26） | 5593 | 17 件とも同じ | `cli/spool/bridge.lock` | 無し |

- **見ていないこと: 起動時の走査で中身を読んだか**（開いて閉じたか）。fd は起動後の
  一瞬を写したもので、走査の最中は見ていない（`fs_usage` は root が要る）。

### Windows

試料は mac と同じ形で、実行ファイルだけ `bin/vw2026.exe`（`C:\Windows\System32\whoami.exe` の
写し、82944 バイトの PE）に替えた。この機械の cli は段 2 のもの（`cli/bin/`（空）・
`cli/build.json`・`cli.vlb`・`cli.vwr`）で、`cli/spool/` は無い。台帳には試料 14 件と
cli の実物 2 件の、計 16 件を載せた。

- **警告は出なかった。** 「開発元不明のプラグイン」のダイアログは、**試料を置いた状態でも
  `cli` と `VwSdkProbes` の 2 行だけ**だった（試料を置く前の起動と同じ）。`vw2026.exe`・
  `.ps1`・`.sh`・JSON は載らない。試料を置いたあとの最初の 2 回の起動ではこのダイアログは
  出なかった（前回から 24 時間以内。上の「『開発元不明のプラグイン』のダイアログ」）ので、
  VW を止めて `<Date>` を 2 日前へ戻し、**出る回を 1 回わざと作って**確かめた。それ以外の
  起動では、メインの窓が操作できるようになるまでモーダルの窓は 1 つも出なかった（窓の一覧で見た）。
  `VW User Log.txt` のそのセッションの `Dialog` の行は、ホーム画面・ピッカー・上の
  `UnknownDeveloperPlugIns` だけだった。INFO 以外の行（`RefNameTableHandle::IsValid failed`
  2 件・`GRAPHICS ADAPTERS CONFIGURATION ISSUE ALERT!` 1 件）は、**試料を置く前の起動にも
  出ていたもの**である。10/26 以降の大半のセッションで同じ 3 件が出ており、試料を置く前の
  当日の最初の起動（22:31 UTC）にも出ていた。
- **書き換え・移し・消しは無かった。** 試料を作ってから**起動を 2 回またいで**、16 件の
  うち 15 件はファイル ID・大きさ・更新時刻（100 ns）・中身のハッシュがすべて同じだった。
  ダイアログを出した回（別の試料。起動 1 回）も同じく 15 件が同じだった。違った 1 件は
  毎回、試料のフォルダ `cli/p277` そのものの更新時刻である。その値は**直前にプローブを
  走らせた時刻**（07:54:04・08:00:47・08:09:24。どれも次の起動より前）で、プローブが
  自分で中のフォルダの名前を変えて戻したときに付いたもの（NTFS では子の名前を変えると
  親のフォルダの更新時刻が変わる）。台帳はその前に取っているので差に出た。
- **開いたままのものは無かった。** どの回も、起動後に試料のファイルとフォルダ 14 個と、cli の
  `bin/`・`build.json`・`cli/` 自体を**共有なし（share 0）で開けた**。VW が 1 つでも
  ハンドルを持っていれば、ここで共有違反（32）になる。試料の `spool/`・`bin/`・`hooks/`・
  `p277/` と cli の `bin/` は、**名前を変えて戻せた**。
- **`cli/` 自体の名前は変えられなかった**（`MoveFileExW` が `ERROR_ACCESS_DENIED`＝5。
  4 回とも）。`cli/` を共有なしで開くことはできた。中の `cli.vlb` を書き込みで開くと 32 だった。
- **読み込まれた場所:** `cli.vlb` と `VwSdkProbes.vlb`
  のどちらについても、次の 3 つがどれも `…\Plug-ins\cli\cli.vlb`（と
  `…\Plug-ins\VwSdkProbes\VwSdkProbes.vlb`）を返した: `K32EnumProcessModules` の
  `GetModuleFileNameW`、モジュールの中の番地からの
  `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)`（同じ `HMODULE` が返った）→
  `GetModuleFileNameW`、`GetMappedFileNameW`（`\Device\HarddiskVolume3\Users\…\Plug-ins\cli\cli.vlb`）。
  そのパスを書き込みで開くと共有違反（32）になった。
  - これは**起動時の降ろし直し（上の「Windows の VW の起動時のモジュールの読み込み直しと、
    OS に預けたコールバック」）より後**の値である（プローブはメニューから走らせた）。
    4 回の起動とも同じパスで、`cli.vlb` の `HMODULE` は 4 回とも `00007ffe8f7a0000`
    だった（殻の番地は起動ごとに違った）。降ろす前の最初の読み込みで求めた値は見ていない。
- 実機確認プラグインの本体（`.vwpayload`）は、殻が `%TEMP%` へ写してから読み込む
  （`…\Temp\VwSdkProbes-1-VwSdkProbesPayload-pr279.vwpayload`）。

| 起動 | pid | 台帳との違い | 共有なしで開けない付属物 | `cli/` の名前 | 警告 |
| --- | --- | --- | --- | --- | --- |
| 試料を作った回（07:43） | 9588 | —（台帳を作った） | 無し | 変えられない（5） | 無し |
| 再起動 1（07:55） | 5684 | 15 件同じ・`p277` の更新時刻だけ（上記） | 無し | 変えられない（5） | 無し |
| 再起動 2（08:02） | 9864 | 15 件同じ・`p277` の更新時刻だけ（上記） | 無し | 変えられない（5） | 無し |
| `<Date>` を戻して再起動（08:11。08:09 に作り直した試料） | 4580 | 15 件同じ・`p277` の更新時刻だけ（上記） | 無し | 変えられない（5） | 開発元不明のダイアログ（`cli`・`VwSdkProbes` の 2 行だけ） |

- **見ていないこと: 起動時の走査で中身を読んだか**（開いて閉じたか）。mac と同じく、
  起動後を写したもので、走査の最中は見ていない（Process Monitor は使わなかった）。

## VCOM の初期化の範囲を SDK の宣言と定義で読んだ

SDK の宣言と定義（`sdk-grep` で読んだ）:

```
Include/Kernel/API/MiniCadHookIntf.h:2032  extern CallBackPtr        gCBP;
Include/Kernel/API/MiniCadHookIntf.h:2041  extern VectorWorks::ISDK* gSDK;
Source/VWSDK/Kernel/API/MiniCadCallBacks.cpp:150  CallBackPtr gCBP = nil;
Source/VWSDK/Kernel/API/MiniCadCallBacks.cpp:54   VectorWorks::ISDK* gSDK = NULL;
```

`GS_InitializeVCOM(void* cbpParam)`（`Source/VWSDK/Kernel/API/MiniCadCallBacks.cpp:219`）の中で
していること:

| すること | 何を埋めるか |
| --- | --- |
| `GS_Kludge(cbp, 499, &ver, &gVCOMCallback)` | VCOM のコールバック |
| `VWMM::Partition::GetPartitionByIdentifier("VWMainPartition", true)` | `gVWMM` |
| `gCBP = (CallBackPtr)cbpParam` | 旧 `GS_` 系の呼び出し口 |
| `VCOMPtr<ISDK>(IID_SDK)` | `gSDK` |

### SDK の静的ライブラリをリンクするモジュールで足りなかった定義（リンクエラー）

CI の実ビルドで、Vectorworks にプラグインとして登録されないモジュールでも、
`plugin_module_ver` と `DefaultPluginVWRIdentifier` が無いとリンクが通らなかった。
`DefaultPluginVWRIdentifier` を落としたときのリンカの出力（参照元は `_GS_GetLayoutFromRsrc` と
`TXResStr::TXResStr`。`TXString` を使うと `TXResStr` 経由で入ってくる）:

```
mac : Undefined symbols: "DefaultPluginVWRIdentifier()", referenced from:
        _GS_GetLayoutFromRsrc in libVWSDK.a(APIBase.Legacy.Defs.o)
        TXResStr::TXResStr(char const*, char const*, EEmptyHandling) in libVWSDK.a(TypesString.o)
win : VWSDK.lib(TypesString.obj) : error LNK2019: unresolved external symbol
        "char const * __cdecl DefaultPluginVWRIdentifier(void)"
```

## 本体を外部モジュールへ出して再起動なしで入れ替えた実測

（macOS・VW 2026 / Apple Silicon。殻と本体の境界の置き場所は Findings の同じ節。）

### 実測（macOS。中身の違う 2 つのモジュールを順に読んだ）

`.vwlibrary` の `Contents/Resources` に置いた**アドホック署名済み**の `MH_BUNDLE`
（拡張子 `.vwpayload`）を、一時ディレクトリへ複製してから読み込んだ。

| 確かめたこと | 結果 |
| --- | --- |
| Vectorworks のプロセス内で `dlopen` できるか | **できた。** アドホック署名で足りる（ホストの library validation に弾かれない） |
| そのモジュールから SDK が使えるか | **使えた。** 殻の `gCBP` を渡して `GS_InitializeVCOM` → `gSDK` が埋まり、レイヤを読めた |
| `dlclose` で本当に消えるか | **消えた**（`dlopen(RTLD_NOLOAD)` が nullptr を返す） |
| 読み込み中のファイルを消せるか | **消せた**（macOS の作法どおり。Windows は消せないはず＝未確認） |
| 降ろした後に置き換えて読み直せるか | **できた。変種 A → B で、走ったコードが実際に入れ替わった** |

かかった時間（プローブ全体で 0.76 秒）:

| | 世代 1（変種 A） | 世代 2（変種 B） |
| --- | --- | --- |
| 読み込み（`dlopen` ＋ シンボル解決） | 373 ms | 342 ms |
| アンロード（`dlclose`） | 19 ms | 14 ms |

**降ろした後、次のモジュールが同じ番地へ載った**（両世代とも、モジュール内の関数の
番地が `0xb1f798760`）。

### 殻と本体の `gSDK` の値の実測

実測では殻とペイロードの `gSDK` が同じ値（`0x1069a2818`）だった。ペイロードは静的ライブラリを
自分でリンクし、`RTLD_LOCAL`（二階層名前空間）で読み込んでいる。

### 本体を複数置いた形（PR ごとに分けて、選ばれた 1 本だけ読む）

（VW 2026。実機確認プラグインの形——main のプローブが 1 本、PR のプローブが PR ごとに 1 本
（`VwSdkProbesPayload-<群>.vwpayload`）。）

| 確かめたこと | 結果 |
| --- | --- |
| 一覧を出すのに本体を読み込まなくてよいか | **よい。** 殻の隣に置いたテキストの索引（`VwSdkProbes.probes.txt`）だけを読んでピッカーを組める。表示名と概要はビルドのときにソースから読み出して索引へ書いておく |
| 選ばれてから読み込んで走らせられるか | **できた。** ダイアログで選んだ 1 本だけを `dlopen` → 実行 → 降ろす |
| 1 本が欠けていても他は使えるか | **使えた。** 索引に載っているのにファイルが無い本体は「※本体なし」と出て、他の本体のプローブはそのまま選んで走らせられる |

1 本にまとめていたときは、**同居させた誰か 1 つの調査コードがコンパイルできないだけで、
全部が配れなくなった**。

### 殻の記憶域を本体に持たせて落ちた実測

最初の実装は、殻が渡す `VwPayloadHost`（コールバック・ログの受け口）を本体が
**ポインタのまま**保持していた。殻の側はそれを `load()` の**ローカル変数**に置いていた。
プローブがログの 1 行目を書いたところで、Vectorworks ごと落ちた。

**コンパイルもリンクも CI の実ビルドも通っていた**（型はすべて合っている）。

#### クラッシュレポートから犯人を特定する読み方

実際に読んだ macOS の crash report:

```
Exception Type:    EXC_BAD_ACCESS (SIGSEGV)
Exception Subtype: KERN_PROTECTION_FAILURE at 0x000000016b699ad0
  far: 0x16b699ad0  esr: 0x8200000f (Instruction Abort) Permission fault
VM Region Info: 0x16b699ad0 is in 0x16aea0000-0x16b69c000 ---> Stack (thread 0)

  x0: 0x000000016b699600   x1: 0x00000008db0797a0   x2: 0x000000016b699ad0
   lr: 0x00000005234007ec   pc: 0x000000016b699ad0

0  ???                          0x16b699ad0 ???
1  …VwSdkProbesPayload.vwpayload 0x5233fcbf8 vw_payload_run + 1812
2  VwSdkProbes                   0x4385e58dc vwprobe::Payload::run(...) + 232
```

| 読めること | どこから |
| --- | --- |
| **命令フェッチで落ちている**（データ参照ではない） | `esr` の `Instruction Abort` |
| **飛び先がスタック**（＝関数ポインタがゴミ） | `far`＝`pc` が `VM Region Info` でスタックに入る |
| **その値は `x2` から来た**（`blr x2`） | `x2` が `pc` と同じ値 |
| **引数はゴミと実データが混じる** | `x0` はスタックの番地（＝腐った `logCtx`）、`x1` はヒープ（＝生きている `line`） |
| **呼んだのは本体のログ経路** | `lr` が本体の中、frame 1 が `vw_payload_run` |

切り分けの決め手は「x0 だけがゴミで x1 は正しい」だった——構造体から `ctx` と関数ポインタを読み、
実データは別経路で来る `host->log(host->logCtx, line)` の形に合う。腐っていたのは `host` の指す先
だった。

`lr`（本体 +0x87ec）と frame 1（`vw_payload_run + 1812`）が食い違っていたのは、
受け口を呼ぶ小さな関数が**末尾呼び出し**にされてフレームを作らなかったため。

単体テスト `plugin/tests/PayloadHostHolderTests.cpp` は、渡した記憶域を後から塗り潰し、
受け口がまだ生きているかを見る（SDK も実機も要らない）。

直したうえで**実機で確かめた**（VW 2026 / mac。build `95bb8334fd18`）。ログの受け口が生きていて、
プローブの各行が殻の結果ダイアログへ流れ、最後まで走り切った:

```
=== 煙試験: 図面のレイヤを数える [example] ===
本体: … id=95bb8334fd18
undo: before building=no
--- ここからプローブ本体 ---
本体から見た undo: building=no
  デザイン: 共通
デザインレイヤ 1 枚 / シートレイヤ 0 枚
--- プローブ本体ここまで ---
結果: 成功
```

これで**本番の形**（メニュー → 本体を読む → プローブが SDK と VWFC を使う →
ログが境界を越えて殻へ流れる → 結果ダイアログ → 降ろす）が一周した。

### 本体側からダイアログを開いた実測（実プラグイン）

実プラグイン
（[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)）
を同じ形へ割ったとき（macOS / VW 2026 / Apple Silicon）に、**本体側から開いて
正常に動いたもの**:

| 開いたもの | 使ったもの |
| --- | --- |
| 取り込み設定ダイアログ（一覧・チェック・シンボルの絵） | `VWFC::VWUI::VWDialog`（`GS_CreateLayout` で組む。`.vwr` のレイアウトは使わない） |
| 進捗ダイアログ | 同上 |
| 完了ダイアログ（折り畳んだログ欄付き） | 同上 |
| ファイル選択 | VCOM の `IFileChooserDialog` / `IFileIdentifier` |
| フォルダ選択 | VCOM の `IFolderChooserDialog` / `IFolderIdentifier`（[File and Folder Dialogs](../Findings/File%20and%20Folder%20Dialogs.md)。実機確認プラグインのプローブで実測） |

- 本体は `DefaultPluginVWRIdentifier` を定義するだけでリソースを引かない。`.vwr` は殻の側にだけ
  置いた（メニュー名・PIO 名・パラメータ名の登録は殻に残した）。
- 実プラグインは**入れ子の深さが 0（本体のコードがスタックに 1 つも無い）のときだけ降ろす**
  作りにしてある。

### PIO のイベントを殻から本体へ取り次いだ実測（実プラグイン）

PIO の**登録**——`SParametricDef`・パラメータ定義・UUID・`IMPLEMENT_VWParametricExtension`——は
殻に残し、`Recalculate()` の**中身を本体へ出した**。殻の event sink は「本体を確保して、自分の
ハンドル（`fhObject`）を C の ABI 越しに渡す」だけにした。

実測（記号 PIO 12 個・耐力壁 PIO 53 枚を含む図面。取り込みの診断ログから、リセット
1 回につき 1 行出る記録を数えた）:

| 確かめたこと | 結果 |
| --- | --- |
| 取り込みが作った PIO のリセットが本体側で走るか | **走った**（耐力壁の作成中に 159 回 / 83 ms） |
| **VW が自発的に起こすリセット**でも走るか | **走った**（伏図のビューポート作成中に 53 回 / 52 ms） |
| 速さ | 合わせて **212 回 / 135 ms**＝**1 回あたり 0.6 ms 前後** |

取り込み全体の所要は 1 分 15 秒。

### 実運用の入れ替えを実機で回した（再起動なし）

実プラグイン（IFC を読んで 1174 個のオブジェクトを描く取り込み）で:

1. ビルド A（`03a175c`）を入れて取り込みを実行。
2. **`.vwpayload` だけ**をビルド B（`4363d43`）のものへ差し替え（`.vwlibrary` は触らない）。
   **Vectorworks は起動したまま。**
3. もう一度取り込みを実行。

結果:

- 取り込みログの見出しが名乗るコミットが `03a175c` → `4363d43` へ変わった
  （この行は**本体が自分で名乗る値**）。
- **ビルド B でしか直っていない挙動が実際に現れた**（ログのパス表示の修正）。
- 描いたものは差し替えの前後で **1174 件が完全に一致**、所要も 1 分 14 秒 → 1 分 15 秒。

本体はバンドルの外（殻の隣）に置いており、`codesign --deep` には含まれない。署名が無いと
Apple Silicon の library validation に弾かれて `dlopen` が失敗する。
