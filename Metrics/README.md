# 計測記録（Metrics）

[`Findings/`](../Findings/README.md) の各トピックの**根拠**——測った環境・見本・手順・
調査に使ったコード・実測の表やダンプ・切り分けの経緯——を置く場所です。

`Findings/` は「**何が言えるか**」（結論・使い方・落とし穴）だけを短く書き、
ここは「**どう測って何が出たか**」——計測の手順と結果——だけを省略せずに残します。
**結論・解釈・指針はここに書きません。** 結論を引きたいときは `Findings/` を、
結論を疑う・測り直す・数字を確かめたいときはここを読みます。

## Findings との分け方

| 置き場所 | 書くもの |
| --- | --- |
| `Findings/<トピック>.md` | 結論（箇条書き）・確認水準の印・どの環境で成り立つか（mac / Windows）・推奨する呼び方（数行のコード例まで）・打ち切った調査の結論と教訓・**根拠への短いリンク** |
| `Metrics/<トピック>.md` | 測った環境（VW の版・ビルド・OS・日付）・使ったプローブ（パス・PR）・見本（何を作ったか）・手順・**実測の表・ログ・ダンプ（省略しない）**・計測用のコード・ヘッダ / ソースの抜き書き・切り分けの経緯（何を変えて何が出たか）・落ちた記録 |
| **Metrics に書かないもの** | 結論・まとめ・「だから〜する」「〜は使わない」の指針・推奨する書き方・なぜそうなるかの読み（解釈）・確認水準の判断。これらは Findings へ |

- **ファイル名はトピックごとに同じ**にする（`Findings/Layer Objects.md` ↔
  `Metrics/Layer Objects.md`）。どちらも冒頭で相手へリンクする。
- **Findings の結論から、根拠の節へリンクする**（`（[実測](../Metrics/Layer%20Objects.md#3-入れ子と-foreachobjectn-の-traversewhat)）`）。
  節の見出しは Findings と同じにしておくと、リンクを組み立てやすい。
- **数値の答えそのものは Findings に残してよい**（「`LO-A` は 11」「3000 件で 10 ms に届かない」
  のように結論の一部になる値）。その値を出した表は Metrics に置く。
- **結果は「何をして何が出たか」の形で書く**（「`X` を呼ぶと `false` が返った」「画面に
  ダイアログが出た（目視）」）。「だから `X` は使えない」は Findings に書く。
- **測り直して結果が変わったら、古い記録は消さずに新しい記録を足す**（日付・版を添えて）。
  結論の更新は Findings で行う。Metrics と Findings が食い違って見えたら、Findings が正。
- 確認水準の印（【推定】【ヘッダ根拠】など）は結論に付けるものなので、Findings 側に付ける。
  Metrics では、**どう確かめたか**（実機のログ・目視・ヘッダを読んだ・ソースを読んだ）を
  記録そのものとして書く。
- **新しい調査では、測った記録をまず Metrics に書き、結論を Findings に足す。**
  プローブはマージ前に消す決まり（[CLAUDE.md](../CLAUDE.md)「調査のフロー」4）なので、
  何をどの順で呼んだか（コードの要点）は消す前にここへ写す。

## 目次

| ファイル | 結論 |
| --- | --- |
| [Parametric Objects](Parametric%20Objects.md) | [Findings](../Findings/Parametric%20Objects.md) |
| [Attributes and Classes](Attributes%20and%20Classes.md) | [Findings](../Findings/Attributes%20and%20Classes.md) |
| [Symbols](Symbols.md) | [Findings](../Findings/Symbols.md) |
| [Layers and Stories](Layers%20and%20Stories.md) | [Findings](../Findings/Layers%20and%20Stories.md) |
| [Layer Objects](Layer%20Objects.md) | [Findings](../Findings/Layer%20Objects.md) |
| [Viewports](Viewports.md) | [Findings](../Findings/Viewports.md) |
| [Level Objects](Level%20Objects.md) | [Findings](../Findings/Level%20Objects.md) |
| [Data Tags](Data%20Tags.md) | [Findings](../Findings/Data%20Tags.md) |
| [Drawing Labels](Drawing%20Labels.md) | [Findings](../Findings/Drawing%20Labels.md) |
| [Dimensions](Dimensions.md) | [Findings](../Findings/Dimensions.md) |
| [Graphic Legends](Graphic%20Legends.md) | [Findings](../Findings/Graphic%20Legends.md) |
| [Tagged Data](Tagged%20Data.md) | [Findings](../Findings/Tagged%20Data.md) |
| [Undo](Undo.md) | [Findings](../Findings/Undo.md) |
| [Layout Dialogs](Layout%20Dialogs.md) | [Findings](../Findings/Layout%20Dialogs.md) |
| [Timers and Notifications](Timers%20and%20Notifications.md) | [Findings](../Findings/Timers%20and%20Notifications.md) |
| [File and Folder Dialogs](File%20and%20Folder%20Dialogs.md) | [Findings](../Findings/File%20and%20Folder%20Dialogs.md) |
| [File and Folder Identifiers](File%20and%20Folder%20Identifiers.md) | [Findings](../Findings/File%20and%20Folder%20Identifiers.md) |
| [Plug-in Interop](Plug-in%20Interop.md) | [Findings](../Findings/Plug-in%20Interop.md) |
| [Plug-in Modules](Plug-in%20Modules.md) | [Findings](../Findings/Plug-in%20Modules.md) |
| [Documents](Documents.md) | [Findings](../Findings/Documents.md) |
| [Investigation Techniques](Investigation%20Techniques.md) | [Findings](../Findings/Investigation%20Techniques.md) |

計測の記録が無いトピック（ヘッダ根拠だけのもの・短いもの）は、Metrics 側のファイルを
作らずにこの表から外す。
