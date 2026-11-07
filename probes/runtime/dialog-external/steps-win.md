# #302 Windows の手順

mac の手順と同じ問いを Windows で測る。ダイアログの最中はブリッジの呼び出しが戻らないので、
`helper-win.ps1 watch` を**別のプロセスとして先に裏で起動してから** `vw_run_probe probe=dialog-external` を呼ぶ。
指示ファイルは `%TEMP%\VwSdkProbes-dialog-external.do`、印は `%TEMP%\VwSdkProbes-dialog-external\in-call.txt`。

共通の起動（押すボタンだけ変える。**ラベルを初めて見るダイアログは先に `-Title` なしの `list` で読む**）:

```powershell
$m = "$env:TEMP\VwSdkProbes-dialog-external\in-call.txt"
Start-Process powershell -ArgumentList '-File','probes\runtime\dialog-external\helper-win.ps1','watch','-Title','キャンセル','-Marker',$m -RedirectStandardOutput out.txt
```

## 普通の権限（VW も CLI も昇格なし）

| 名前 | 指示ファイル | 押す | 見るもの |
| --- | --- | --- | --- |
| A1 | `event a` / `find a` / `close` / `docs` / `menu` / `doundo` / `find a` | キャンセル | ID・呼び出しが戻るまでの所要・印・`Dialog[OPEN]` の有無・件数が変わらない・取り消しが効く |
| A2 | `close` | いいえ | 図面が閉じる |
| B1 | `mkdir $T\p302\b1` / `rect a` / `saveto $T\p302\b1\f.vwx` / 親のフォルダの `FILE_ADD_FILE` を拒む（#293 の `icacls`）/ `rect b` / `save` / `fs` | OK（枚数を見て `-Count`） | 枚数・文言・`GSError` |
| C1 | `rect a` / `saveto $T\p302\c1\held.vwx` / `new` / `saveto $T\p302\c1\held.vwx` | OK | `GSError=50331649` |
| D1 | 読み取り専用のファイルを人の入口で開く（`Vectorworks2026.exe <P>`。呼び出しの外） | いいえ | 印が無い・`list` で 2 枚目（進み具合）が別の窓に見えるか |
| E1 | メニュー「閉じる」を別のプロセスから起こす（呼び出しの外）→ `list` | キャンセル | 印が無い |
| N1 | `nest $T\p302\c1\held.vwx` / `close`（`rect a` などで変更を作っておく） | 上から順に | 2 枚が重なったときの `list` の並びと、**下を先に押すと何が起きるか** |
| G1 | `busy 8`（その間に `list` を 2 回） | — | VW が塞がっているときの `SendMessageTimeout` の答え（-1 ＝ `読めない err=…`）と所要 |
| U1 | 開発元不明のプラグイン（再起動して起動の途中に `list`） | 続ける | 読める中身。`有効化` は無効のまま |

押した後に `docs` を足し、人が押したときとの比較が要るところは `computer use` のクリックと並べる。

## 昇格（UIPI）

管理者の `Vectorworks2026.exe`（昇格）と、普通の権限の PowerShell（CLI の代わり）の組み合わせで A1 を繰り返す。

| 組み合わせ | 見るもの |
| --- | --- |
| VW 昇格・ヘルパー昇格なし | `trust` の整合性レベル。`list` で `#32770` が見えるか・`WM_GETTEXT` の答え（`読めない err=…`）・`BM_CLICK` の答え（`lastError`）・ダイアログが残るか |
| VW 昇格なし・ヘルパー昇格 | 読めて押せること |
| どちらも昇格 | 読めて押せること |
