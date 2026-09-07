# Undo（取り消し）

長い処理（インポート等）を 1 つの undo イベントにまとめるときの実測。

- **VW は処理の開始時に undo イベントを開かない。** 実測は
  `undo: start=no` / `afterParse=no` / `afterDraw=yes`——それでも描画の終わりには
  「イベント中」になるのは、**断面ビューポートの生成のように SDK 内部が自前でイベントを
  開く呼び出しがある**ため。この半端な記録を取り消すと**図面が壊れる**（実機: ビューポート
  だけ消え、レイヤは戻らず、オブジェクトが断面を失って単線・2D 面になった）。
  `GS_EndUndoEvent` の説明にある「外部の終了時に自動で**閉じる**」は「自動で開く」ではない。
- **`SetUndoMethod(kUndoSwapObjects)` ＋ `NameUndoEvent(...)` が開始も兼ねる**（ISDK に開始
  専用の呼び出しは無い）。RAII で包み、破棄で `EndUndoEvent()` するのが安全。
- **登録するのは「この処理が新しく作ったレイヤ」だけ**で足りる。レイヤを消せば上の図形も
  消えるので図形を 1 つずつ登録する必要が無く、**二重登録で「もう無いものを消しにいく」
  事故**も避けられる。
- **登録が 1 件も無いときはイベントごと捨てる**（`EndAndRemoveUndoEvent`）。空のイベントを
  残すと、SDK 内部が開いた記録が取り消しの対象になって図面が壊れる。
- **SDK へ渡して消費させた下ごしらえは取り消しで復活する。** 例えば
  `CreateCustomObjectPath` にポリラインと空グループを渡すと、SDK はそれらを**undo 記録つきで
  削除**して PIO へ取り込む。こちらがイベントを開いていると、その削除が記録に入り、
  **取り消したときにポリラインだけが図面へ復活する**（そのせいでレイヤも消えずに残る）。
  対処は SDK の作法どおり「自分が追加したものは申告する」——`AddAfterSwapObject` は
  "that object is deleted when Undo is selected" なので、申告しておけば復活したそれが改めて
  消える。**レイヤの上に普通に置いた図形へは使わない**（レイヤごと消えるものを二重登録
  しない）。
- **戻らないもの**: クラス・ストーリ・レベルテンプレートはリソースなので残る（図面の
  見た目は処理前に戻る）。**処理前から在ったレイヤへ描いた分**も、そのレイヤごと消すわけに
  いかないので戻らない——利用者にはその旨を伝える。

## 打ち切った調査: プラグインから「もう閉じたイベント」へ Undo を掛ける

**結論: できない（ヘッダの記述から確定。[issue #23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23)）。**
ISDK に「人がメニューの『取り消し』を選んだのと同じことを、閉じた undo イベントに対して
プログラムから起こす」呼び出しは無い。

- Undo を**実行する**側の API は `ISDK::SupportUndoAndRemove` /
  `ISDK::UndoAndRemove` の組だけ【ヘッダ根拠】。`APIBase.Legacy.Defs.h` の説明文そのまま:

  > `GS_UndoAndRemove`: Undo a partially built (unended) undo event and remove it from
  > the undo table. The SetUndoMethod() call of the event that is being undone and
  > removed must have been preceded with a SupportUndoAndRemove() call.

  つまり対象にできるのは**まだ `EndUndoEvent` で閉じていない、今まさに自分が組み立てている
  最中のイベントだけ**。`SupportUndoAndRemove()` を、その回の `SetUndoMethod()` より**前に**
  呼んでおく必要もある（後追いでは効かない）。
- この回のプラグイン開発が本当に欲しかったのは「**前回のメニュー実行で、すでに
  `EndUndoEvent`（`NameUndoEvent` 経由）まで閉じてしまったイベント**」を、次のメニュー実行
  （＝別のタイミングで開いた別の undo イベント）から戻すこと。`UndoAndRemove` が扱えるのは
  「閉じていない」イベントに限られるので、**この用途には最初から使えない**——世代をまたいで
  「n 個前の undo イベントを実行する」ような汎用の呼び出しは、`ISDK` 全体を見ても存在しない
  （`Info/Undo Best Practices.md` に載っている Undo 関連の呼び出しは全部で 9 個。開始・登録・
  終了・警告ダイアログ・このペアのどれかに分類できる）。
- したがって、**2 周目以降に「前回の取り込み結果を戻す」役はプラグイン自身では担えない。**
  引き続き利用者（またはそれに代わる操作）に「取り消し」を実行してもらう前提で設計する。
  レイヤを直接消す代替案は次節。

## レイヤのハンドルを直接 `DeleteObject` する

[issue #23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23) の
もう 1 つの筋——「前回このプラグインが作ったデザインレイヤを、undo に頼らず**レイヤの
ハンドルを直接消して**取り除く」——を実機（`probes/runtime/delete-layer/`）で確かめた。

- **`DeleteLayer` 相当の専用 API は無い**【ヘッダ根拠】（`sdk-grep` で `DeleteLayer` /
  `DelLayer` / `kObjectTypeLayer` を検索してもヒットせず、`RemoveStoryLevel` の
  `bDeleteLayer` 引数がストーリレベル解除のついでに消す経路として見つかっただけ）。
  レイヤも「図面のオブジェクト列に並ぶオブジェクトの 1 つ」（[Layers and
  Stories](Layers%20and%20Stories.md)）なので、消す手段は**汎用の
  `ISDK::DeleteObject(MCObjectHandle h, Boolean useUndo = true)`**（データタグの
  後始末で既に使っているのと同じ呼び出し）をレイヤのハンドルへ呼ぶ、の 1 通りだけ。
- **非アクティブなデザインレイヤ＋その上の矩形を用意して `DeleteObject(layer, true)` を
  呼ぶと、レイヤごと図面から消える。** 削除後に図面を辿っても同名レイヤは見つからず、
  VW も落ちない。図形だけがレイヤを失って残るような中途半端な状態は観測されなかった
  （矩形のハンドル自体が無効化されたかまでは個別に確認していないが、レイヤが完全に
  図面から消えている以上、宙に浮いた図形が残っているとは考えにくい）。
- **アクティブレイヤ自身を `DeleteObject` で消してもクラッシュしない。** 消す前に
  他のレイヤへ退避する必要は無い——削除直後に `GetCurrentLayer()` を呼ぶと、VW が
  自動的に別の既存レイヤへ切り替えていた（今回は、そのセッションで元々アクティブ
  だった「共通」レイヤに戻った）。**ただし「全レイヤを消して 1 枚も残らない」状態は
  試していない**（未確認。恐らく最後の 1 枚は消せない/消しても自動生成されるはずだが、
  裏は取れていない）。
- **落とし穴: `DeleteObject(handle, true)` は、呼び出し時点で undo イベントが開いて
  いなければ自分で開く。** 実測: プローブ開始時点では `IsCurrentlyBuildingAnUndoEvent()`
  が `false` だったが、1 回目の `DeleteObject(layer1, true)` の直後には `true` に
  変わり、その後プローブ関数が `return` するまで（＝ `EndUndoEvent` の類を 1 度も
  呼ばないまま）ずっと `true` のままだった。これは「登録が 1 件も無い空のイベントや
  半端な記録を取り消すと図面が壊れる」という上記の一般的な注意点に、**`DeleteObject`
  を useUndo 任せで呼ぶだけで自分から踏み込んでしまう経路がある**ということ。
  プラグインからレイヤ削除に使うときは、`DeleteObject` の `useUndo` に開始から終了まで
  任せきりにせず、**自分で `SetUndoMethod` ＋ `NameUndoEvent` を呼んでイベントを明示的に
  開始し、削除が終わったら `EndUndoEvent`（登録するものが無ければ
  `EndAndRemoveUndoEvent`）まで自分で閉じる**——本ファイル冒頭の RAII の作法をそのまま
  レイヤ削除にも適用する。
- **未確認のまま残っているもの**: シートレイヤを消したときのビューポートへの副作用、
  ビューポートが参照する側のデザインレイヤを先に消した場合の影響、削除順序の決まり。
  今回のプローブはデザインレイヤ単体の削除しか見ていない。ホームズ君 IFC 取り込み
  プラグインの用途（前回自分が作ったデザインレイヤを消す）はこれで足りるため、
  シートレイヤ／ビューポート絡みは必要になった時点で別途 issue を立てて調べる。
