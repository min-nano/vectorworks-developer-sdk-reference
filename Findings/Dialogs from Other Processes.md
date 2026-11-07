# ダイアログを別のプロセスから見つけ、読み、押す

Vectorworks がモーダルなダイアログを出して呼び出しから戻らない間に、**別のプロセス**（CLI の
`vw2026` など）がそれを扱えるか。[#302](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/302)。
プロセスの中の見張りから押した記録は [Documents](Documents.md) と [Plug-in Modules](Plug-in%20Modules.md) にある。

**確認の範囲: mac だけ。Windows は未測定で、昇格（UIPI）の答えも無い。** 下の「まだ測っていないこと」を参照。

## 結論（mac）

- **対象の 5 種類はすべて、別のプロセスから AX で見つけ、読み、`AXPress` で押せる。** 押すと呼び出しは戻り、
  図面の件数・取り消し・戻り値は本物のマウスのクリックと同じだった。**違いは前面だけ**で、`AXPress` は VW を前面にしない。
- **見つける目印は `AXModal=true`。** `AXSubrole=AXDialog` はパレットも名乗るので使えない。
- **`Modal=true` はダイアログとは限らない。** 図面を開く進み具合の窓も `Modal=true` で同時に出る。ボタンの有無で分ける。
- **`AXPress` の `success` は「押せた」を意味しない。** 無効なボタンでも `success` を返して何も起きない。閉じたかは窓で確かめる。
- **`cannotComplete(-25204)` は「閉じた」でも「無い」でもない。** VW のメインスレッドが忙しいという答え。
- **重なっているときは一番上だけを押す。** 下の窓のボタンを押すと、**下は閉じず、上のダイアログが違うコードで閉じる。**
- **呼び出しの最中に出たものかは、窓からもログからも分からない。** プラグインが印を置く必要がある。
- **許可が無ければ AX はすべて `apiDisabled(-25211)` を返す。** `AXIsProcessTrusted()` で事前に分かる（画面は出ない）。
- **許可は起動元（責任プロセス）で数えられる。** ターミナルから起こす CLI はターミナルの許可で通り、CLI 自身の許可は効かない。
  ad-hoc 署名の CLI に付けた許可は、作り直して cdhash が変わると黙って切れる。

## 環境

VW 2026（31.6.0）。macOS 27.0.1（仮想マシン）。表示言語は日本語だけ。本体のビルド `598348282e54` / `a86fcb6698dd`。
ヘルパーは Swift の CLI（`probes/runtime/dialog-external/helper-mac.swift`。ad-hoc 署名）で、VW とは別のプロセス。
プローブがダイアログを出す呼び出しをし、**プローブの見張りは見るだけで押していない**（安全弁で 1 回だけ押した場面は N2 に書いた）。

## ダイアログごとの表（mac）

「部品」の `action-button-N` は `AXIdentifier` の値。VW 独自の窓のボタンには識別子が無く、`AXDefaultButton` と題だけで引く。

| ダイアログ | 出し方 | 窓のクラス（プロセスの中で読んだ） | ログの名前 | ボタン（題・目印） | 押した結果 |
| --- | --- | --- | --- | --- | --- |
| **閉じる確認** | `DoMenuName("Close", 0)` | `NSAlertPanel` | **出ない** | 保存 `action-button-1`（default）/ 保存しない `action-button-3` / キャンセル `action-button-2`（cancel） | 保存: 件数 2→1、ファイルのハッシュが変わる。保存しない: 件数 1→0、ファイル不変。キャンセル: 件数不変、図面は残る |
| 閉じる確認から出る保存パネル（無題で「保存」） | 上の続き | `id=save-panel`、題 `保存:Vectorworks` | — | 保存 `OKButton` / キャンセル `CancelButton`。名前は `saveAsNameTextField` | キャンセルで元の図面に戻る。パネルの実体は `openAndSavePanelService` だが、VW の AX の窓として読める |
| **保存の失敗**（親のフォルダに足せない） | `SaveActiveDocumentPath` | `NNADialogWindow` | `NetworkAlertCritical` が **2 枚** | OK（default） | 2 枚押すと戻る。`GSError=0`（失敗なのに 0。[#293](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/293)）。ファイル不変 |
| **すでに開かれています** | 別の図面が開いているパスへ保存 | `NNADialogWindow` | `NetworkAlertInform` | OK（default） | `GSError=50331649`。件数不変 |
| **読み取り専用** | `open -a`（呼び出しの外） | （`NNADialogWindow` と同じ形【推定】） | `NetworkAlertQuestion` | はい(Y)（default）/ いいえ(N) | はい: `ro コピー 1` が開く。いいえ: 開かない |
| **使用中** | 2 つ目の VW（`open -n`）で同じファイルを開く | （同上【推定】） | `NetworkAlertQuestionWithDetails` | はい(Y)（default）/ いいえ(N) / 詳細... | いいえ: 開かず、4.1 秒後に閉じた（その間 `cannotComplete`） |
| **開発元不明のプラグイン** | 起動 | — | `UnknownDeveloperPlugIns` | 続ける（default）/ 終了 / 有効化・ファイルの場所を開く・元に戻す（無効） | 続ける: 起動が進む。**閉じるまで 10 秒**（2 つ目の VW。その間 `cannotComplete` が 6 回） |

- 文言は `AXStaticText` の `AXValue` で読める。**警告と確認の題は空**で、文言の先頭に `" "` だけの部品が来る窓がある。
- 開発元不明の一覧の行（プラグイン名など）は `AXStaticText` に値が無く、この道では読めなかった。
- 閉じるまでの時間は同じ確認でも 0.2〜1.3 秒とばらつく（`AXPress` 自体は 2〜100 ms で戻る）。

### 窓の見分け

| 窓 | `AXSubrole` | `AXModal` | 備考 |
| --- | --- | --- | --- |
| ダイアログ | `AXDialog` | **true** | |
| **進み具合**（題 `Vectorworks`。「ファイルを開いています: …」） | `AXDialog` | **true** | **ボタンが無い**。`AXProgressIndicator` がある。読み取り専用・使用中の確認の裏に同時に出る |
| パレット | **`AXDialog`** | false | 題が空で `id=_NS:14` |
| ツールチップ | — | — | `AXRole=AXHelpTag` |
| 図面の窓 / ホーム画面 | `AXStandardWindow` | false | **ホーム画面は主窓そのもの**（題が空）で、混ざらない |

- **`AXWindows` の先頭が一番上だった**（重なった場面と進み具合の場面。どちらも `AXFocusedWindow` と一致）。
- `AXFocused` 属性は当てにならない（`AXFocusedWindow` が指す窓でも `false` のことがある。開発元不明・保存パネル）。
- **`NSRunningApplication.isActive` は当てにならない**（ランループを回さない CLI では古い値のまま）。前面は `AXFrontmost` で読む。

### 表示言語に依らない目印

- ログの **`Dialog[OPEN]: ID-10003 (NetworkAlertCritical)` の ID は種類ではなく、その起動の中の通し番号**である（10002 が開発元不明、以後 10003, 10004, … と増えた）。
  **種類の目印になるのは括弧の中の名前だけで、それも「危険な通知」「質問」程度の粒度**（保存の失敗もほかの警告も同じ名前になりうる【推定】）。
- **`NSAlertPanel`（閉じる確認）はログに出ない。** ログだけではダイアログの有無を判定できない。
- `Dialog[CLOSE]: ID-… Return=` は押したボタンを表さない。はい・いいえ・OK・続けるがどれも `Return=1`、閉じるボタンが `Return=2` だった。
- ボタンの言語に依らない目印は **`AXDefaultButton` / `AXCancelButton`**（`NSAlert` はさらに `action-button-N`）。題の `はい(Y)` は言語で変わるはず。
  **日本語でしか見ていないので、言語に依らないことは【推定】。**
- 窓とログを直接結ぶ値は無い。時刻で並べるしかない。

## 押し方の落とし穴（mac）

### 無効なボタンの `AXPress` は `success`

開発元不明の「有効化」（`AXEnabled=false`）へ `AXPress` すると **`success`（0 ms）が返り、何も起きない**。押す前に `AXEnabled` を読み、押した後は窓が消えたかで確かめる。

### 閉じたかの判定で `cannotComplete` を読み違えない

押した後に窓へ問い合わせた答えは 3 通りある。

| 答え | 意味 |
| --- | --- |
| `success` | まだある |
| `invalidUIElement(-25202)` | **閉じた** |
| `cannotComplete(-25204)` | **VW が忙しくて答えない。閉じたかは分からない** |

最初の版は `success` 以外を「閉じた」とみなして、1.5 秒後に閉じたと誤認した。

### メインスレッドが塞がっているとき（G2）

プローブがメインスレッドを 14 秒塞いでいる間に問い合わせた。

| 上限 | 答え | 所要 |
| --- | --- | --- |
| `AXUIElementSetMessagingTimeout(1)` | `cannotComplete` | 1022 ms |
| `AXUIElementSetMessagingTimeout(2)` | `cannotComplete` | 2021 ms |
| 既定 | `cannotComplete` | 1518 ms（別の属性では約 3 秒） |

塞ぎ終えた後は普通に読めた（35 ms）。**ダイアログの最中（モーダルのループが回っている間）は AX は答える**ので、`cannotComplete` はダイアログではなく、処理の最中を指す。
`CGWindowListCopyWindowInfo` はウィンドウサーバへの問い合わせなので塞がっていても答えるはずだが、この回は塞ぎ終えた直後に読んだので確かめられていない【推定】。

### 重なったダイアログ（N2）

閉じる確認の最中に、見張りの刻みから「すでに開かれています」を出して 2 枚重ねた。

- `AXWindows` は `[すでに開かれています（上）, 閉じる確認（下）]` の順。`AXFocusedWindow` は上。どちらも `Modal=true`。
- **下の「キャンセル」を `AXPress` すると `success`（2 ms）で、下の窓は閉じない。**
- **同じ時刻に上のダイアログが閉じた**（`Dialog[CLOSE]: ID-10008 Return=1001`。OK で閉じたときは `Return=1`）。重ねた保存は `GSError=50331649` で戻った。
- 下の窓は残り、57 秒後に安全弁（見張りのキャンセル）で閉じた。
- 読み: `NSAlert` のボタンが、自分のモーダルではなく**一番上のモーダルをそのコードで止める**【推定】。結果はこの 1 回の観測。

したがって **`dialog press` は `AXFocusedWindow`（`AXWindows` の先頭のモーダル）だけを相手にし、それ以外を指定されたら断る。**

- 閉じる確認の最中に、前面の図面自身のパスへの保存は通った（`GSError=0`・33 ms。N1）。

### 閉じるボタン（`AXCloseButton`）

`Modal=true` の警告にも有効な閉じるボタンがあり、`AXPress` で閉じる（`Return=2`。呼び出しの結果は OK と同じ `GSError=50331649`）。OK しかないダイアログを言語に依らず閉じる手にもなるが、ボタンが複数ある確認で何にあたるかは測っていない。

### 本物のクリックとの比較（A1〜A3）

| | `AXPress`（A3） | CGEvent のクリック（A2） |
| --- | --- | --- |
| 閉じる確認のキャンセルの後の件数 | 変わらない | 変わらない |
| 「取り消し」の項目 | 有効 | 有効 |
| `DoMenuName("Undo")` で図形が消える | 消える | 消える |
| 前面 | **変わらない**（A1 は Finder のまま押せた） | VW が前面になる（ヘルパーが `activate` してから押した） |

VW が前面でなくても `AXPress` は効く。クリックは必ず前面にしてから押したので、前面でないときに届くかは測っていない。
ここでの比較相手は CGEvent のクリックで、人の手では押していない。

## 呼び出しの最中に出たものか（mac）

- **窓だけでは見分けられない。** メニューから開いた閉じる確認と呼び出しの閉じる確認は、属性も `id=_NS:87` も同じだった。
- **ログでも見分けられない。** `Dialog[OPEN]` に呼び出しの情報は無く、`NSAlert` はそもそも出ない。
- **プラグインが「呼び出しの最中」の印を置けば見分けられる。** プローブは呼び出しの間だけ `$T/in-call.txt`（タグ・開始のエポック ms・pid）を置いた。
  別のプロセスから、呼び出しの閉じる確認の最中には読めて、`open -a` の確認とメニューの閉じる確認の最中には無かった。押して呼び出しが戻ると消えた。
- **印があっても、一番上がその呼び出しのものとは限らない。** 利用者のダイアログの最中に呼び出しが入ることがあるので（下）、呼び出しの開始時の窓の一覧を印へ入れておくと分かる【推定】。VW が落ちると印が残るので、pid の生死も見る【推定】。
- `Dialog[OPEN]` の `session` は起動ごとに違い、2 つの VW が同じログへ書いていた。ログを読むなら `session` で絞る。

**ブリッジの答えについて:** 開発元不明と、メニューから開いた閉じる確認の最中は、`vw_bridge_status` が `running:false`、`vw_run_probe` は「橋が架かりませんでした」だった（閉じると戻った）。
殻のブリッジはこの最中に受け付けなかった。#264 で確かめた「共通モードのタイマーからなら図面を扱える」と矛盾はしないが（そちらはプロセスの中のタイマー）、**ブリッジで走らせられた #264 の記録とは違う**ので、理由は分かっていない。

## 権限（mac）

`AXIsProcessTrusted` と `AXIsProcessTrustedWithOptions(prompt=false)` は同じ答えで、2〜36 ms で返った。
**許可の無い経路で何度呼んでも、許可を求める画面は出なかった**（目視）。

同じ実行ファイル（`~/vwdlg-302/vwdlg`、ad-hoc 署名）を 2 つの経路から起こし、システム設定の「アクセシビリティ」の
スイッチを入れ替えて測った（どの行が入っているかは `/Library/Application Support/com.apple.TCC/TCC.db` の
`kTCCServiceAccessibility` の `auth_value` で確かめた）。

| 許可（Terminal / vwdlg） | Terminal.app の `.command`（責任プロセス = **Terminal**） | launchd の `launchctl submit`（責任プロセス = **vwdlg 自身**） |
| --- | --- | --- |
| 切 / 切 | false・`apiDisabled(-25211)` | false・`apiDisabled` |
| 切 / **入** | **false**・`apiDisabled` | **true**・読める |
| **入** / 切 | **true**・読める | **false**・`apiDisabled` |
| 入 / 入 | true | true |

`claude` の子として起こしたとき（責任プロセス = `claude`。許可あり）は、vwdlg の許可に依らず true だった。

- **許可は責任プロセス（`responsibility_get_pid_responsible_for_pid`）の 1 つでだけ数えられる。** 実行ファイルに許可を付けても、
  Terminal から起こせば Terminal の許可しか見られない。**どちらか一方で足り、両方は要らない。**
- **スイッチの変更は、走っている Terminal にもすぐ効いた**（Terminal を起動し直さずに、次に起こした子から答えが変わった）。
  システム設定でスイッチを切っても、**TCC.db に書かれないことが 1 度あった**（入れ直して切ったら書かれた）。答えは DB の値どおりだった。
- **ad-hoc 署名の実行ファイルへの許可は cdhash に結び付く。** TCC.db の `csreq` は `cdhash H"d554…"` の要件だった。
  - 同じソースを同じ `-O` で作り直すと cdhash は変わらず（`d5542e1e…`）、true のまま。
  - `-Onone` で作り直すと cdhash が変わり（`9ce3913c…`）、**設定のスイッチは入ったままなのに false**。元の `-O` に戻すと true に戻った。
  - つまり**CLI を更新するたびに、許可を付け直す必要がある**（設定画面では入っているように見える）。開発者 ID で署名すれば要件は
    識別子とチームになるので、更新しても切れない見込み【推定】。
- 許可が無いと属性の読み取りはどれも `apiDisabled` で、`AXWindows` も読めない（空の一覧ではない）。
- **許可が無くても `CGWindowListCopyWindowInfo` は答える。** VW の窓の枚数・位置・`layer` が読める。題は読めない（画面収録の許可が無いと `kCGWindowName` が無い）。
- `layer` はダイアログの目印に足りない。VW が前面のとき、ダイアログも進み具合の窓も `layer=8` だった。**前面でないときの閉じる確認は `layer=0`**（A1。この 1 回）。パレットは 3、ツールチップは 103。
  「VW に `layer=8` の窓があれば、何か出ているらしい」程度の手掛かりにはなるが、無いことの証拠にはならない。

## CLI への帰結（mac）

`dialog list`:

1. `AXIsProcessTrusted()` を見る（画面は出ない）。false なら「許可が要る」と答え、**責任プロセス**（`responsibility_get_pid_responsible_for_pid` の
   パス。ふつうは使っているターミナル）に許可を与える案内を出す。CLI の実行ファイルに付けても、ターミナルから起こす限り効かない。
   ad-hoc 署名の CLI を更新したなら「設定に入っていても付け直す」と添える。CGWindowList で `layer=8` の窓があれば「何か出ているらしい」と添える。
2. `AXWindows` のうち `AXModal=true` を、**配列の順のまま**返す。先頭が押せる相手。
3. 有効な `AXButton` が 1 つも無い窓（進み具合）は押せない窓として別に示す。ウィンドウの制御ボタン（`AXCloseButton` など）はボタンから除く。
4. `cannotComplete` は「忙しい」と答える。「ダイアログは無い」と答えない。
5. 文言は `AXStaticText`。ボタンは題・`AXIdentifier`・default / cancel の印・`AXEnabled` を返す。
6. 印があれば「呼び出しの最中」を添える。

`dialog press`:

1. **先頭のモーダルだけを相手にする。** ほかを指定されたら断る。
2. 指定は題より **`default` / `cancel`** を勧める（言語に依らない【推定】）。
3. `AXEnabled=false` は押さずに断る。
4. `AXPress` の `success` で終わらず、`invalidUIElement` になるまで待って報告する。`cannotComplete` の間は待ち続ける（開発元不明で 10 秒かかった）。時間切れは「押したが閉じたか分からない」と答える。
5. 次のダイアログが続くことがある（保存の失敗は 2 枚）。呼び出しが戻ったかは印で見る。
6. **本物のクリックは使わない**（前面を奪う）。

プラグイン側: 呼び出しの間だけ印を置く。`save`・`close` の戻り値は押された後に返るが、`GSError=0` のまま失敗していることがある（保存の失敗）ので、押したダイアログの文言を結果に添えられると呼ぶ側が判断できる。

## まだ測っていないこと

この調査の範囲内で、**PR を open のまま答えを待っている**項目（[CLAUDE.md](../CLAUDE.md)「PR とマージ」3）。

- **Windows のすべて**: `#32770` の見つけ方・読める中身・`BM_CLICK`・印・重なったとき・`SendMessageTimeout` の答えと、**昇格（UIPI）の組み合わせ**。手順とヘルパーは `probes/runtime/dialog-external/`（`steps-win.md`・`helper-win.ps1`）。ヘルパーは書いただけで、一度も走らせていない。
- 表示言語を変えたときの題と目印。
- 閉じる確認で閉じるボタンにあたるもの（`AXCloseButton` の有無を含む）。
