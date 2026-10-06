# 周期実行と通知（Timers and Notifications）

**プラグインのコードを「自分が呼ばれていないとき」に動かすための口**と、その刻みの中で
何をしてよいかの実測。

いまここにあるのは、[issue #206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)
で調べた「**OS のタイマーの刻みが、VW が undo イベントを開いたままイベントループを
回している最中に当たったら何が起きるか**」の分だけである。
「そもそも SDK に周期実行・アイドルの口はあるか」「ウェブパレットの JS タイマーが
隠れると間引かれるのを避けられるか」は
[issue #204](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/204)
の調査で、まだ入っていない。

関連:

- [Undo](Undo.md) — undo イベントの開き方・誰が勝手に開くか・何が取り消しスタックに載るか。
  **本ファイルの前提はほぼすべてあちらで実測済み**なので、先にあちらを読む。
- [Layers and Stories](Layers%20and%20Stories.md) — 「SDK に遅延実行の口は無い（`ISDK` に
  あるのは `RegisterDialogForTimerEvents` ＝**ダイアログの**タイマーだけ）」の実測。
- [Progress and Diagnostics](Progress%20and%20Diagnostics.md) — 進捗ダイアログの `DoYield`。

## 刻みの中で「いま触ってよいか」を判定する口

結論を先に: **「VW がいま何かの最中か」を問える口は無い。** 問えるのは
「**この文書に undo イベントが開いているか**」の 1 つだけで、それは**同じことではない**。

### `ISDK::IsCurrentlyBuildingAnUndoEvent()` が唯一の問い合わせ口。説明文は 1 行も無い

```cpp
// ISDK.h:2612
virtual bool VCOM_CALLTYPE IsCurrentlyBuildingAnUndoEvent() = 0;
```

**宣言だけで、コメントが付いていない**（前後の行にも無い。`ci-debug` の `shell` で
前 6 行まで出して確認。[run](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/37436354417)）。
つまりこの関数の意味は**実測でしか決まらない**。幸い実測は既にある——
[Undo](Undo.md) と [Documents](Documents.md) に散っているものを、**刻みの中で使う道具
として読み直す**と次のようになる。

| 既知の実測 | 刻みの中で使うとき何を意味するか |
| --- | --- |
| **文書ごとの状態**（[Documents](Documents.md)「文書を開く・切り替える操作は…」）。文書 A でイベントを開いたまま B へ行くと `no`、A へ戻ると `yes` | **「いまアクティブな文書」の話しか聞けない。** 刻みが見るのはそのときのカレント文書である |
| **SDK 内部が勝手に開く**（[Undo](Undo.md) 冒頭）。PIO を作って `ResetObject` するだけ・（断面でない）ビューポートの生成や更新だけ・`DeleteObject(h, useUndo=true)` だけで開く | **`yes` は「利用者が操作中」を意味しない。** こちらのコードが直前に通った呼び出しの置き土産でも `yes` になる |
| **スクリプトエンジンはイベントを開いたまま返し、コマンドをまたいで残る**（[Undo](Undo.md)） | **`yes` が居座る。** 次に走ったコマンドの冒頭から `yes` で始まる |
| **取り消しの実行がイベントを終わらせる**（[Undo](Undo.md)） | 自分が開いた覚えのあるイベントでも、いつの間にか `no` に戻っている |

**だから `yes` を「触るな」の旗にしてはいけない**——前のコマンドの置き土産ひとつで
旗が立ったまま居座り、以後の刻みが永久に何もできなくなる。使える向きは逆で、
**`no` は「この文書にはいま開いているイベントが無い」として信用できる**。
「開いていないときだけ書く」という安全側の作りには足りるが、
**「誰が開けたのか」「VW が操作中なのか」は問えない。**

### 通知に「undo イベントが開いた」は無い——あるのは「**閉じる直前**」だけ

`MCNotification.h` 全体で undo に触れる定数は**1 本しか無い**（`grep -i undo` で 1 行。
[run](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/37436354417)）。

```cpp
// MCNotification.h:179
const Sint32 kNotifyUndoEndEvent = 'Udee';	// Send immediatelly before ending an undo event
```

**これは決定的な制約である**【ヘッダ根拠】。

- **「開いた」を知る通知が無い**ので、`kNotify…` を購読して「イベントが開いた瞬間に
  旗を立てる」作りは**原理的に書けない**。立てられるのは「**閉じる直前に旗を下ろす**」
  側だけで、片側しか取れない。
- したがって「いまイベントが開いているか」を通知で追うことはできず、
  **`IsCurrentlyBuildingAnUndoEvent()` を刻みの中で毎回問い直すしかない**
  （その値の読み方は上表）。

購読の口は `ISDK` にある【ヘッダ根拠】:

```cpp
// ISDK.h:1751 / 1759
virtual Boolean VCOM_CALLTYPE RegisterNotificationProcedure(StatusProcPtr proc, OSType whichChange) = 0;
virtual void    VCOM_CALLTYPE UnregisterNotificationProcedure(StatusProcPtr proc, OSType whichChange) = 0;
```

### 「VW が操作中か」を囲める通知は、**ツールの点取りだけ**

`MCNotification.h` から、刻みの可否を判定する材料になりそうなものを拾うと【ヘッダ根拠】:

| 定数 | 値 | ヘッダのコメント（原文） | 両端が取れるか |
| --- | --- | --- | --- |
| `kNotifyBeginToolMode` | `'BTOO'` | Sent when the current tool begins collecting points | **取れる**（`ETOO` と対） |
| `kNotifyEndToolMode` | `'ETOO'` | Sent when the current tool finishes collecting points | 同上 |
| `kNotifyToolChange` | `'TLCh'` | Sent when the current tool has changed. | 片側（切り替わっただけ） |
| `kNotifyToolModeChanged` | `'TmCh'` | Sent when the current mode tool has changed. | 片側 |
| `kNotifyDialogDisplayImminent` | `'DlgO'` | Sent when a dialog display/"open" operation is in progress. | **片側だけ**（閉じたことは来ない） |
| `kNotifyInteractiveDrawCleared` | `'InOC'` | Sent when interactive or adorner objects draw lists are cleared. | 片側 |
| `kNotifyRenderModeAboutToChange` | `'RndC'` | — | **レンダリング中ではない**（モードの変更） |
| `kNotifyRenderModeChanged` | `'RnMC'` | — | 同上 |

- **ツールのドラッグ（点取り）は `BTOO` → `ETOO` で囲める**ので、「点取りの最中は
  触らない」という旗は**自分で立てられる**。
- **レンダリング中を囲める通知は無い。** `kNotifyRenderMode*` は「レンダリング方法の
  設定が変わった」で、「いま描いている最中」ではない。
- **VW が出したダイアログも囲めない**——`kNotifyDialogDisplayImminent` は開く側だけで、
  閉じたことを知らせる対が無い。

**＝ 通知だけで「触ってよいか」を完全に判定することはできない**【ヘッダ根拠】。
ツールの点取りという一番ありがちな一区間は囲めるが、レンダリングと VW のダイアログは
囲めないので、そこは `IsCurrentlyBuildingAnUndoEvent()`（＝上表の読み方）と
併せて安全側に倒すしかない。
