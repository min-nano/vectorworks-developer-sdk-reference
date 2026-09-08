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
- **他所が開いているイベントを閉じることがある。** スクリプトエンジンの
  `ExecuteScript` は、こちらが開いたままの undo イベントを**勝手に終わらせる**（実測。
  下記「間接経路: スクリプトエンジン経由…」）。RAII で `EndUndoEvent` を呼ぶ作りは
  「もう無いイベントを閉じる」ことになるので、**イベントの中で他所の重い呼び出しを
  しない**。
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
  レイヤを直接消す代替案は後述する。

## 打ち切った調査: undo イベントを閉じずにコマンドを終えて、次のコマンドから `UndoAndRemove` する

**結論: できない（ヘッダの記述から確定。[issue #31](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/31)）。**
前節の制約（`UndoAndRemove` が対象にできるのは「まだ `EndUndoEvent` で閉じていない」イベント
だけ）に対し、「**イベントを閉じているのはプラグイン自身のコードなのだから、意図的に
`EndUndoEvent` を呼ばずにコマンドから返れば、そのイベントは次のコマンド実行まで
クローズされずに残るのでは**」という回避策を確かめた。しかしこれは `GS_EndUndoEvent`
自身の説明文（`APIBase.Legacy.Defs.h`）で否定される。

> `GS_EndUndoEvent`: Ends the creation of an undo event. A new event must have been
> initiated (with the SetUndoEvent() callback) before EndUndoEvent() can be called.
> EndUndoEvent() saves the view at the end of the user action and performs general
> undo cleanup. **The use of this procedure is not required; VectorWorks will
> automatically end the event when an external is completed.**

最後の 1 文がそのまま今回の問いへの答え。**`EndUndoEvent()` を呼ぶこと自体が必須ではなく、
呼ばなくても VW が「外部（＝そのコマンドの実行）が完了した時点」で自動的にイベントを
終了（クローズ）する。** つまり「呼ばずに返す」を選んでも、**イベントは開いたまま残らない**
——コマンド A の `DoInterface()` が戻った時点で、VW が代わりに `EndUndoEvent()` 相当の
処理（「ビューの保存」と「一般的な undo の後始末」）を行って閉じてしまう【ヘッダ根拠】。
本ファイル冒頭で「`GS_EndUndoEvent` の説明にある『外部の終了時に自動で閉じる』は
『自動で開くではない』」として同じ一文を既に引用しているが、今回の issue はその**同じ一文の
別の含意**（＝「呼ばなくても自動で閉じてしまう以上、次のコマンドまで開いたままにする策には
使えない」）を確かめるものだった。

issue #31 の問い 1〜6 への答えは、すべてこの 1 文からの帰結になる（★ = ヘッダの記述を
素直に読んだ場合の帰結であり、実機で個別に再現・確認したわけではない）:

- **問い1**: コマンド A が `EndUndoEvent` を呼ばずに返っても、VW が完了時点で自動的に
  イベントを終了する。「開いたまま残る」でも「捨てられる」でもなく、**正常にクローズされる**。
- **問い2**（★）: 直後のコマンド B で `IsCurrentlyBuildingAnUndoEvent()` を呼んでも、
  A が開いたイベントについては `false` が返ると考えられる——A の完了時点で既に
  「構築中」ではなくなっているため。ただし `IsCurrentlyBuildingAnUndoEvent()`
  （`Interfaces/VectorWorks/ISDK.h` 2612 行目）には対応する `GS_` 系コールバックが無く、
  それ自体の説明文も無い（宣言のみ）。この項目は上記のヘッダ根拠からの帰結であり、
  それ自体はヘッダ根拠を持たない。
- **問い3**（★）: `UndoAndRemove` の対象は「まだ閉じていないイベント」だけ（前節の
  ヘッダ根拠）。A のイベントは A の完了時点で VW により閉じられている以上、
  `SupportUndoAndRemove()` を A の `SetUndoMethod()` より前に呼んであったとしても、
  コマンド B から `UndoAndRemove()` を呼んだ時点では**対象が既に無い**——前節「もう閉じた
  イベントへ Undo を掛ける」と同じ状況に帰着する。**戻せない。**
- **問い4〜6**（★）: 「イベントが開いたまま人へ操作を返る」という前提自体が成立しない
  （コマンドが完了した時点で VW がクローズしている）ため、**人が取り消しを選ぶ・別の編集を
  する・保存/終了する、のいずれの時点でも「半端な記録」は存在しない**はず——コマンド A の
  完了までに、VW が（呼んだ場合と同じ）`EndUndoEvent()` 相当の後始末（ビューの保存・一般的な
  undo の後始末）を行ってから制御を返すため。**この帰結は実機で個別に確かめていない**——
  今回はここで調査を打ち切ったため、上記の危険シナリオ自体が実機プローブで再現されるかは
  未検証のまま残る。

**結論として、#23 の「もう閉じたイベントには `UndoAndRemove` が効かない」という制約に対し、
「意図的に閉じずに返す」という回避策も塞がっている**——VW がコマンド完了時に代わりに閉じて
しまうため、次のコマンド実行時点では結局「もう閉じたイベント」と同じ状態になる。往復の周
ごとに前回の描画を undo で丸ごと戻す、という筋は、この方向でも成立しない。プラグイン側は、
次節「レイヤのハンドルを直接 `DeleteObject` する」を代替案として使う前提に戻り、テンプレート
由来のレイヤに描いた分は undo と違って戻らない（レイヤの直接削除は部分復元にしかならない）
という制約を利用者に伝える運用とする。

## 打ち切った調査: プラグインから `DoMenuTextByName` 相当（メニューコマンドを名前で起動）を呼ぶ

**結論: できない（ヘッダの記述から確定。[issue #27](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/27)）。**
VectorScript の `DoMenuTextByName('Undo', 0)` のように、**メニューコマンドを名前・識別子・
ID のいずれで指定しても起動できる汎用 API は ISDK / VWFC に存在しない**。

- `DoMenuText` / `DoMenuTextByName` という名前が SDK 内に現れるのは
  `Include/vs.py`（VectorScript / Python バインディングの宣言だけを集めた、C++ の
  コンパイル対象には入らないスタブファイル）**のみ**【ヘッダ根拠】。ISDK・VWFC の
  ヘッダにも実装ソース（`SDKLib/Source`）にも同名の呼び出しは無い。`vs.py` 側の
  注釈でも `DoMenuText` は "Obsolete procedure"（廃止済み）とされている。
- 「メニュー項目を実行する」に相当しうる語（`ExecuteMenuItem` / `SelectMenuItem` /
  `PerformMenuCommand` / `PostMenuCommand` / `SendMenuCommand` / `CallMenuHandler` /
  `InvokeCommand` / `ExecuteCommand` ／ `Perform*Command`）を SDK 全体
  （ヘッダ＋実装ソース）で検索しても**1 件もヒットしない**【ヘッダ根拠】。
- メニューを扱うインターフェースとして `IWorkspaceMenuItem`
  （`Interfaces/VectorWorks/Workspaces/IWorkspaces.h`）が存在するが、これは
  **ワークスペース（メニュー構成そのもの）を編集するための API**——識別子・表示名・
  ショートカットキー・サブメニュー構成の get/set しか持たず、「このメニュー項目を
  今すぐ実行しろ」に当たる `Execute` / `Invoke` / `Perform` 系のメソッドは無い
  【ヘッダ根拠】。
- `ISDK::GetMyMenuCommandIndex`（`GS_GetMyMenuCommandIndex`）は逆方向の情報——
  **いま実行中の自分のプラグインコマンドが、メニューのどの位置から呼ばれたか**を
  返すだけで、他のコマンドを呼び出す手段ではない【ヘッダ根拠】。
- SDK の汎用エスケープハッチである `Kludge` 経由でメニュー実行や取り消しを行える ID
  も無い。`kKludge` 系の定数を `Menu` / `Undo` / `Command` / `Execute` で検索して
  唯一ヒットしたのは `kKludgeGetMenuItemUserFriendlyName`（4136、
  `VWExtensionMenu.cpp` が使用）——メニュー項目の表示名を読むだけで、実行とは
  無関係【ヘッダ根拠】。

したがって issue #27 の問い 2〜6（取り消しの対象になるイベント・呼べる文脈・複数段
戻せるか・失敗判定・半端な記録を取り消したときに図面が壊れるか）は、**そもそも
呼び出す手段が無いため検証の対象にならない**。上記「プラグインから『もう閉じた
イベント』へ Undo を掛ける」（`SupportUndoAndRemove` / `UndoAndRemove` は未クローズの
イベントしか扱えない）を、「メニューコマンドを名前で起動する」という別経路で回避
できないか確かめた形になるが、**その経路自体が SDK に存在しない**ため結論は変わらない。
前回自分が作ったデザインレイヤを取り除く用途では、引き続き次節「レイヤのハンドルを
直接 `DeleteObject` する」が唯一の代替案になる。

## 間接経路: スクリプトエンジン経由で `DoMenuTextByName` 相当を呼ぶ

**結論: 呼び出す口はある（ヘッダ根拠）が、この経路で取り消しは 1 段も掛からない
（実機確認済み。[issue #39](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/39)）。
しかも呼ぶと、呼び出し側が開いている undo イベントが勝手に終わらされる**——
`ExecuteScript` は undo イベントを開いている最中に呼んではいけない。

前節「プラグインから `DoMenuTextByName` 相当を呼ぶ」で確定した「メニューコマンドを
名前で**直接**起動する汎用 API は無い」に対し、「SDK から VectorScript / Python の
**スクリプト実行そのもの**を起動し、そのスクリプトの中で `DoMenuTextByName('Undo', 0)`
を呼ぶ」という間接経路を確かめた。

- **スクリプトを実行させる API 自体は存在する**【ヘッダ根拠】。
  `Interfaces/VectorWorks/Scripting/` にある 2 つのシングルトンが持つ:

  ```cpp
  // IVectorScriptEngine.h（namespace VectorWorks::Scripting）
  class IVectorScriptEngine : public IVWSingletonUnknown {
  public:
      // ...
      virtual VCOMError VCOM_CALLTYPE ExecuteScript(const TXString& script) = 0;
      // ...
  };
  // 取得: VCOMPtr<IVectorScriptEngine> engine(IID_VectorScriptEngine);
  ```

  ```cpp
  // IPythonScriptEngine.h（同じ namespace。IVectorScriptEngine を include）
  class IPythonScriptEngine : public IVWSingletonUnknown {
  public:
      // ...
      virtual VCOMError VCOM_CALLTYPE ExecuteScript(const TXString& script, IPythonLogger* logger = NULL) = 0;
      // ...
  };
  // 取得: VCOMPtr<IPythonScriptEngine> engine(IID_PythonScriptEngine);
  ```

  どちらも `script` は**ソーステキストそのもの**で、リソースとして保存済みの
  スクリプトを直接指す引数は無い。SDK 同梱の実装ソース
  （`SDKLib/Source/VWSDK/VWFC/Tools/ImageComparisonTesting.cpp`）でも、
  `TXResource("Vectorworks/Scripts/….py")` でリソースからテキストを読んでから
  別の口（`ScriptContext_Begin` / `ScriptContext_Run`。`IPythonScriptEngine` 固有で
  `IVectorScriptEngine` には無い）へ渡している。したがって issue の問い1
  「リソースとして保存済みのスクリプトを実行させる API」という形そのものは無く、
  呼び出し側でリソースを読んでテキスト化する一手間が要る【ヘッダ根拠】。

### 実機で確かめたこと（VW 2026 / macOS）

実機確認プラグイン（VwSdkProbes、ビルド `3a60d15e81fb`）でプローブ
`probes/runtime/script-engine-undo/` を、**メニューコマンドの中（`DoInterface` の中）から**
走らせた（役目を終えたのでプローブ自体は削除済み）。マーカーには**直前に作った
デザインレイヤ**を使い、「`DoMenuTextByName('Undo', 0)` が効いたならレイヤが消えるはず」を
判定にしている。渡したスクリプトは

```
（VectorScript）PROCEDURE __ProbeScriptEngineUndo;
                BEGIN  DoMenuTextByName('Undo', 0);  END;
                Run(__ProbeScriptEngineUndo);
（Python）      import vs
                vs.DoMenuTextByName('Undo', 0)
```

の 2 つ。**印の無い記述は実機確認済み。**

- **両エンジンともコマンド実行中に取得でき、スクリプトは実際に走る。**
  `IVectorScriptEngine` / `IPythonScriptEngine` の `VCOMPtr` はコマンド実行中に取れた。
  `CompileScript` は VectorScript 版・Python 版とも `ok=yes`、`ExecuteScript` は
  `VCOMError=0`（成功）。**落ちない。**
- **しかし取り消しは 1 段も掛からない。** 直前に作ったマーカーレイヤは、VectorScript
  版でも Python 版でも `ExecuteScript` から戻った時点で**残っている**。マーカーを 2 枚
  作って**連続 2 回**呼んでも**両方残る**——「複数段戻せるか」以前に **1 段も戻らない**。
- **`VCOMError=0` は「スクリプトを実行できた」であって「Undo が効いた」ではない。**
  戻り値が見ているのはスクリプトの実行可否だけで、その中の
  `DoMenuTextByName('Undo', 0)` が何もしなかったことは**呼び出し側へ伝わらない**。
- **【危険】自分の undo イベントを開いたまま呼ぶと、そのイベントが消える。**
  `SetUndoMethod(kUndoSwapObjects)` ＋ `NameUndoEvent(...)` で開き
  （`IsCurrentlyBuildingAnUndoEvent()` = `yes`）、その中でレイヤを 1 枚作ってから
  `ExecuteScript` を呼ぶと、**戻ったときには `no` になっていた**——こちらは
  `EndUndoEvent` も `EndAndRemoveUndoEvent` も呼んでいない。しかも**イベントの中で
  作ったレイヤは残っている**ので、「取り消されて消えた」のではなく
  **イベントだけが勝手に終わらされている**。
  - 本ファイル冒頭の「半端な記録を取り消すと図面が壊れる」（ビューポートだけ消える）
    事態は、**この場面では起きなかった**。
  - それでも危ない。**RAII で `EndUndoEvent` を呼ぶ作りだと、もう無いイベントを
    閉じにいく**ことになる。**`ExecuteScript` は undo イベントの中で呼ばない。**
- **エラーは判別できる。ただし「どこが」は分からない。** 壊れた VectorScript
  （`ThisIsNotAValidCall(;`）を渡すと `CompileScript` は `ok=no`、`ExecuteScript` は
  `VCOMError=1` / `succeeded=no`。**`ExecuteScript` 単体でも成否は分かる**（ヘッダから
  「`VCOMError` はインターフェース自体のエラーしか示さないかもしれない」と懸念して
  いた点は、実測で否定された）。一方 `CompileScript` の `outErrorText` は**空**、
  `outLineNumberOfSelectedError` は **-1** で、**エラーの内容も場所も取れなかった**
  （`showDialogs=false` で呼んでいるのでダイアログにも出ない）。
- **同期らしい**【推定】。`ExecuteScript` から戻った時点で「開いていた undo イベントが
  終わっている」という**副作用が既に観測できる**ので、スクリプトは呼び出しの中で
  走っているとみなせる。ただし「Undo だけが遅れて効く」可能性は、この実測では
  否定しきれない（**少なくとも C の 2 回目の呼び出しが戻った時点までは 1 枚も
  消えていない**ところまでしか見ていない。プローブは後片付けをしないので、
  走らせた図面を見ればその先も確かめられる）。参考: `ExecuteScript` 6 回を含む
  プローブ全体の所要は 13.8 秒（Python エンジンの初回起動を含む）。

**なぜ効かないのかは分かっていない。** `DoMenuTextByName('Undo', 0)` がプラグインの
コマンド実行中には無視される／後回しにされるのか、この経路からはメニュー相当の取り消しが
そもそも起動しないのか、どちらかまでは切り分けていない（スクリプトの実行自体は成功して
いるので、少なくとも「スクリプトが走らなかった」ではない）。用途にとって必要なのは
**この経路では取り消せない**という事実の方なので、ここで打ち切る。

**結論**: [#23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23)
（閉じたイベントには効かない）・[#27](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/27)
（メニューを名前で起動する API が無い）・[#31](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/31)
（イベントを閉じずに返して次の実行から戻す）に続き、**この間接経路も塞がった**——
「プログラムから取り消しを掛ける」道は**これで 4 本とも無い**。ホームズ君 IFC 取り込み
プラグインの「前の周の図を消す」用途では、引き続き次節「レイヤのハンドルを直接
`DeleteObject` する」を使う。あわせて **`ExecuteScript` を undo イベントの中で呼ばない**
（開いているイベントが消える）を守ること。

なお、スクリプトエンジン自体は「実行できる・成否が分かる・落ちない」ことが実機で
確かめられた。**Undo 以外の用途**（SDK に口が無い VectorScript 関数を借りる等）で
使う余地はある——その場合も上の「undo イベントの中で呼ばない」は同じく効く。

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
- **ビューポートの載ったシートレイヤも `DeleteObject(sheetLayer, true)` で丸ごと消せる**
  （実機。[issue #29](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/29)、
  `probes/runtime/delete-sheet-layer-viewport/`）。ビューポート 2 枚＋注釈空間へ
  `ISDK::AddViewportAnnotationObject` で足したオブジェクト（データタグ・グラフィック
  凡例の代用としてロケータ点）を載せたシートレイヤを削除したところ、シートは削除後に
  同名レイヤとして辿れなくなり（消えた）、VW も落ちなかった。**ビューポート・注釈
  オブジェクトのどちらも、他のレイヤへ取り残されることなくシートと一緒に消えた**
  （削除後に図面上の全レイヤ直下を走査しても痕跡が残っていない）。
  なお `ISDK::CreateViewport(parentHandle)` の `parentHandle` は「どのデザインレイヤを
  表示するか」ではなく「どの容れ物（レイヤ／レイヤ内のグループ）に置くか」を指定する
  引数【ヘッダ根拠】（`GS_CreateViewport` の説明: "The specified parent handle may only
  be a layer or a group contained within a layer, nested or otherwise"）。表示する
  デザインレイヤは作成後に `ISDK::SetViewportLayerVisibility(viewport, designLayer,
  kLayerVisibilityNormal)` で個別に可視性を設定して初めて決まる。
- **参照されていたデザインレイヤ側に副作用は無い。** シートレイヤを消しても、
  ビューポートが表示していたデザインレイヤとその上の図形（矩形）はそのまま図面に
  残った——ビューポート経由の参照は片方向で、シート側を消してもデザインレイヤは無傷。
- **削除順序に決まりは無い。** 「シートを先に消す」のが安全という前提で調べたが、
  逆に**ビューポートがまだ参照しているデザインレイヤを先に `DeleteObject` で消しても
  VW は落ちない。** 参照先を失ったビューポートはシートレイヤの直下メンバとしてそのまま
  辿れ、その状態で `UpdateViewport` を呼んでも落ちなかった（表示すべきデザインレイヤが
  無いだけで、オブジェクトとしては壊れない）。**その後もシートレイヤ自体を
  `DeleteObject` で問題なく消せる**——参照先を失っていても削除処理は通常どおり完了した。
  したがって**どちらの順序で消しても最終的に同じ状態（両方消える）に落ち着く**
  （ただし逆順では、デザインレイヤ削除後からシートレイヤ削除までの間、ビューポートが
  「表示するものが無い」半端な状態のまま図面に残る点には留意する）。
- 今回も落とし穴を踏んだ: **ビューポートの作成・更新（`CreateViewport` /
  `SetViewportLayerVisibility` / `UpdateViewport` / `AddViewportAnnotationObject`）
  だけで、`DeleteObject` を呼ぶ**前**から undo イベントが自動的に開いていた**
  （実測: 最初の `DeleteObject` を呼ぶ前の時点で `IsCurrentlyBuildingAnUndoEvent()` が
  既に `true`）。これは本ファイル冒頭の「断面ビューポートの生成のように SDK 内部が
  自前で undo イベントを開く呼び出しがある」の一例で、**通常の（断面ではない）平面
  ビューポートの生成・更新でも同じことが起きる**。レイヤ削除に限らず、ビューポートを
  扱う処理を自前の undo イベントで包む場合は、包む**前**に
  `IsCurrentlyBuildingAnUndoEvent()` を確認すること。
