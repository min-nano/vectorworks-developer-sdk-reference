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

## レイヤのハンドルを直接 `DeleteObject` する（実機確認待ち）

[issue #23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23) の
もう 1 つの筋——「前回このプラグインが作ったデザインレイヤを、undo に頼らず**レイヤの
ハンドルを直接消して**取り除く」——について、ヘッダ調査で分かった範囲はここまで:

- **`DeleteLayer` 相当の専用 API は無い**【ヘッダ根拠】（`sdk-grep` で `DeleteLayer` /
  `DelLayer` / `kObjectTypeLayer` を検索してもヒットせず、`RemoveStoryLevel` の
  `bDeleteLayer` 引数がストーリレベル解除のついでに消す経路として見つかっただけ）。
  レイヤも「図面のオブジェクト列に並ぶオブジェクトの 1 つ」（[Layers and
  Stories](Layers%20and%20Stories.md)）なので、消す手段があるとすれば**汎用の
  `ISDK::DeleteObject(MCObjectHandle h, Boolean useUndo = true)`**（データタグの
  後始末で既に使っているのと同じ呼び出し）をレイヤのハンドルへ呼ぶ、の 1 通りだけ
  【ヘッダ根拠】。ヘッダにレイヤ向けの注記は無く、動作は宣言から読み取れない。
- **上の図形が一緒に消えるか・アクティブレイヤを消してよいか・ビューポート絡みの副作用・
  削除の順序**は、いずれもヘッダからは判断できず実機確認が要る。検証用のプローブを
  `probes/runtime/delete-layer/` に用意した（非アクティブレイヤ+図形の削除と、アクティブ
  レイヤ自身の削除の 2 パターン）。実機で走らせた結果が出たら、この節を確認済みの記述へ
  置き換える。
