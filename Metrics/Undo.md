# Undo（取り消し） — 計測記録

結論は [Findings「Undo」](../Findings/Undo.md)。ここには実測の値・表・プローブ・切り分けの経緯（潰した道と
落ちた記録）を置く。プローブ（`probes/runtime/<名前>/`）は役目を終えて消してある。

> ここには計測の手順と結果だけを置く。結論・解釈は Findings にある。

## VW は処理の開始時に undo イベントを開かない

- `IsCurrentlyBuildingAnUndoEvent()` の実測は `undo: start=no` / `afterParse=no` /
  `afterDraw=yes` だった（その処理は描画で断面ビューポートを生成していた）。この記録を
  取り消すと、ビューポートだけ消え、レイヤは戻らず、オブジェクトが断面を失って単線・2D 面に
  なった（実機）。
  - 走り出しに `IsCurrentlyBuildingAnUndoEvent()` が `no` だったプローブが、構造材 PIO を
    いくつか作って `ResetObject` を重ねた後には `yes` になっていた
    （[#166](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/166)。
    そのプローブはイベントを一度も開いていない）。
  - 走り出しに `no` だったプローブが、**ロケータ点を 1 つ `CreateLocus` した直後に `yes`**
    になった（[#206](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/206)。
    そのプローブはイベントを一度も開いていない）。
    （**#247 のプローブでは `CreateLine` / `CreateLocus` では開かず、`DeleteObject(h, true)` で
    開いた**——Windows・mac とも。下記「レイヤのハンドルを直接 `DeleteObject` する」の【追記】）
    - #206 の調査では、対照として置いた図形が測定対象と同じ段に入ってしまい、実機を 2 回
      無駄にした。
    - 自分で開いていないイベントに `EndUndoEvent()` を呼ぶと、戻り値 `true`、閉じた後の
      `IsCurrentlyBuildingAnUndoEvent()` は `no` だった。

## `kUndoPlaceholderNode`（型 90）は積もるが描画に影響しない

- `kUndoPlaceholderNode`（型 90）は `ResetObject` 1 回ごとに PIO の子へ 1 つ積もった。
  11 個積んだ構造材でも、描かれた子の数・型の内訳・外接・立方は既定のままだった
  （[Parametric Objects](../Findings/Parametric%20Objects.md) の「構造材 PIO の子を消すのは…」）。
  - undo イベントが開いていても積もった。
  - 型 90 の子へ `DeleteObject(h, useUndo=false)` を呼んでも 1 つも減らず、
    `ClearUndoTableDueToUnsupportedAction()` でも減らなかった。undo イベントを閉じても、VW へ
    戻った後も減らなかった（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）。

## undo イベントの外で作ったもの・取り消しの実行・スクリプトエンジンが残すイベント

- イベントを開かずに `CreateLayer` したレイヤは、そのあと（スクリプト経由で）取り消しを
  実行しても**残った**（下記「間接経路: スクリプトエンジン経由…」）。
- 自分の undo イベントを開いたまま（`IsCurrentlyBuildingAnUndoEvent()` = `yes`）、スクリプト
  経由で `DoMenuTextByName('Undo', 0)` を呼ぶと、**戻ったときには `no`** になっていた——
  こちらは `EndUndoEvent` も `EndAndRemoveUndoEvent` も呼んでいない（下記
  「間接経路: スクリプトエンジン経由…」）。
  - 無害なスクリプトを同じ形で走らせてもイベントは `yes` のまま残り、**失敗するスクリプト**
    （コンパイルエラーで `VCOMError=1`）でも `yes` のまま残った。`Undo` を呼ぶスクリプト
    だけが `no` にした。
- 図形を作る VectorScript / Python を走らせると、戻った時点で
  `IsCurrentlyBuildingAnUndoEvent()` が `yes` になった（VectorScript / Python の
  `ScriptContext` 経由とも）。次に走らせたプローブの冒頭も `building=yes` から始まった。

## 戻らないもの——クラス・ストーリ・レベルテンプレートの【訂正】

- **経緯:** この節は長らく「クラス・ストーリ・レベルテンプレートはリソースなので残る」と
  書いていた（初回の移植から。実測の記録は見当たらない）。
  [issue #223](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/223) で
  測った: 作ったレイヤを `AddAfterSwapObject` で登録して `EndUndoEvent` で閉じるイベントの
  中で、`AddClass` のクラス・`CreateStory` の階・`CreateLayerLevelType` のレベル種別・
  `CreateStoryLevelTemplate` のテンプレートを作り（どれも登録はしていない）、別のコマンドから
  取り消したところ、**4 つとも消え、件数まで作る前に戻った**（詳細は
  [Layers and Stories](../Findings/Layers%20and%20Stories.md)「プラグイン自身の undo イベントの
  中で回すと…」）。
  - 登録しなかったレイヤは残った（階に結んでいたレイヤは階を失って残った）。
    その上の構造材は、登録していなくても消えた。

## 保存が積む取り消しの段（#288）

- **経緯:** 以前は「別名保存は取り消しの段を 1 つ積む」（#223）と書いていた。
  [issue #288](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/288) で
  手順を変えて測り直した。空の段が出たのは次の 2 つの場面の 1 回だけで、どの場合も
  もう 1 回押せば戻った。別名保存と上書き保存、`DoMenuName("Undo", 0)` とメニューの
  「取り消し」で結果は変わらなかった。

  1. **図面に構造材を初めて足したあとの、最初の保存**（mac と Windows で同じ）。
  2. **前の保存のあと、プラグインのイベントだけで図面が変わった図面の、最初のメニューの「保存」**（mac のみ。
     Windows はメニューを操作できず測っていない）。

  **1 の実測**（取り消しで戻った順。`・` は 1 回。**空**は何も戻らない。手順を決めた 7 通りは
  mac / Windows とも同じ結果。ビルド `017577ba80d6` / `c618d7104b26`）

  | 手順（左から順に） | 戻った順 | 測った OS |
  | --- | --- | --- |
  | 矩形 A・矩形 B・別名保存 | B・A（空の段なし） | 両方 |
  | 矩形 A・別名保存・矩形 B・上書き保存 | B（空の段なし） | 両方 |
  | 矩形 A・階 S・別名保存 | S（空の段なし） | 両方 |
  | 構造材 M・矩形 B・保存なし | B・M（空の段なし） | Windows（mac は矩形 A・構造材 M で M・空の段なし） |
  | 構造材 M・矩形 B・別名保存 | **空**・B | 両方 |
  | 構造材 M・矩形 B・別名保存・上書き保存・上書き保存 | **空**・B（空は 1 つ） | Windows（mac は矩形なしで**空**・M） |
  | 構造材 M・別名保存・矩形 B（以後保存なし） | B・**空**・M | 両方 |
  | 構造材 M・別名保存・矩形 B・上書き保存 | B・**空**・M（2 回目の保存は積まない） | 両方 |
  | 構造材 M・矩形 B・別名保存・矩形 C・上書き保存 | C・**空**・B・M | Windows |
  | 構造材 M・別名保存・構造材 N・上書き保存 | N・**空**・M（**2 つ目の構造材の保存は積まない**） | 両方 |
  | 矩形 A・別名保存・構造材 N・上書き保存 | **空**・N（最初の保存の時点で構造材が無かった） | 両方 |
  | **構造材のある保存済みの図面を開き直し**、矩形 C・上書き保存 | C（空の段なし） | 両方 |
  | 同上、構造材 X・上書き保存 | X（空の段なし） | 両方 |
  | **構造材の無い保存済みの図面を開き直し**、構造材 X・上書き保存 | **空**・X | 両方 |
  | 構造材 M・矩形 B・（ブリッジが別名保存して戻る）→ 次の実行で取り消し | **空**・B・M（コマンドをまたいでも同じ） | Windows（mac は矩形なしで**空**・M） |
  | 構造材 M・**メニューの「複製」**・別名保存 | **空**・複製・M（**人の操作でも積む**） | mac |
  | 構造材 M・矩形 A・別名保存・**メニューの「複製」**・別名保存 | 複製（空の段なし。最初の保存で使い切っている） | mac |

  - 「矩形 A・別名保存・構造材 N・上書き保存」では N の下にもう 1 段あり、3 回目でも A は
    戻らなかった。
  - #288 の途中で「人の操作の後なら積まない」と書いたが、人の操作の前に 1 度保存していた
    （上の最後の 2 行）。取り下げた。

  **2 の実測**（mac。「保存」「複製」は画面からメニューを押した。人の手ではない）

  | 手順（左から順に。図面は無題から） | 戻った順 |
  | --- | --- |
  | 別名保存・矩形 A・矩形 B・**メニューの「保存」** | **空**・B（2 回確かめた） |
  | 別名保存・矩形 A・矩形 B・メニューの「すべてを選択」・メニューの「保存」 | **空**・B・A |
  | 別名保存・矩形 A・メニューの「保存」・矩形 B・メニューの「保存」 | B・**空**・A（2 回目は積まない） |
  | 矩形 A・矩形 B・別名保存・メニューの「保存」（保存のあと何も変えていない） | B・A（空の段なし） |
  | 別名保存・矩形 A・メニューの「すべてを選択」・メニューの「複製」・メニューの「保存」 | 複製・A（空の段なし） |
  | 矩形 A・別名保存・メニューの「複製」・メニューの「別名で保存」（同じ図面で複製と取り消しを繰り返したあと） | 複製（空の段なし） |
  | 矩形 A・矩形 B（保存なし）・メニューの「すべてを選択」 | B・A（選択は段にならない） |

  - 刻みからの保存（`SaveActiveDocumentPath`）で「別名保存・矩形 A・矩形 B・上書き保存」は
    B・A だった（構造材なし）。
  - 空の段が何なのかは、どちらの場面も分からなかった。
  - メニューの項目の文字はいつも「取り消し」だった（`NSMenu` の題と画面の両方。mac）。
  - 描く操作の後は測っていない（人の操作はメニューの「複製」で測った）。
  - MCP ブリッジの `vw_run_probe` は走らせた後に図面を別名保存する
    （[plugin/README.md](../plugin/README.md)「ローカルの Claude Code から操作する」）。
    上の表の「ブリッジが別名保存して戻る」行がそれ。
  - **Windows では測れていない**: メニューの「取り消し」と「保存」、人の操作の後の取り消し、「取り消し」の
    文字（メニューが `HMENU` ではなくプローブから読めない）。[#291](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/291) で
    試し直したが、この VM は 3D アクセラレーションが無く、メニューを開く・図面のタブを切り替えると落ち、Ctrl+S はホーム画面に
    遮られて図面に届かなかった。`DoMenuName` は `Save` を **-3** で受け付けないのでプローブからも起こせない。描画で落ちない
    Windows が無いので打ち切った（[Documents](../Findings/Documents.md)「Windows——ほかの Vectorworks が開くファイルと、測れなかった残り」）。

## 打ち切った調査: プラグインから「もう閉じたイベント」へ Undo を掛ける

[issue #23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23)。
問い: 前回のメニュー実行で `EndUndoEvent`（`NameUndoEvent` 経由）まで閉じたイベントを、
次のメニュー実行（＝別のタイミングで開いた別の undo イベント）から戻せるか。ヘッダを読んだ。

- Undo を**実行する**側の API として見つかったのは `ISDK::SupportUndoAndRemove` /
  `ISDK::UndoAndRemove` の組だけ。`APIBase.Legacy.Defs.h` の説明文そのまま:

  > `GS_UndoAndRemove`: Undo a partially built (unended) undo event and remove it from
  > the undo table. The SetUndoMethod() call of the event that is being undone and
  > removed must have been preceded with a SupportUndoAndRemove() call.

- `Info/Undo Best Practices.md` に載っている Undo 関連の呼び出しは全部で 9 個で、開始・登録・
  終了・警告ダイアログ・このペアのどれかに分類できた。「n 個前の undo イベントを実行する」
  ような呼び出しは `ISDK` に見当たらなかった。
- 実機では走らせていない。

## 打ち切った調査: undo イベントを閉じずにコマンドを終えて、次のコマンドから `UndoAndRemove` する

[issue #31](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/31)。
確かめた回避策: 「意図的に `EndUndoEvent` を呼ばずにコマンドから返れば、そのイベントは
次のコマンド実行までクローズされずに残るのでは」。ヘッダの `GS_EndUndoEvent` の説明文
（`APIBase.Legacy.Defs.h`）を読んだ:

> `GS_EndUndoEvent`: Ends the creation of an undo event. A new event must have been
> initiated (with the SetUndoEvent() callback) before EndUndoEvent() can be called.
> EndUndoEvent() saves the view at the end of the user action and performs general
> undo cleanup. **The use of this procedure is not required; VectorWorks will
> automatically end the event when an external is completed.**

- `IsCurrentlyBuildingAnUndoEvent()`（`Interfaces/VectorWorks/ISDK.h` 2612 行目）には対応する
  `GS_` 系コールバックが無く、それ自体の説明文も無い（宣言のみ）。
- issue #31 の問い 1〜6 は、この 1 文から答えを導いて打ち切った。**実機で個別に再現・確認は
  していない**（導いた答えは Findings）。後の #213 / #247 の実測（下記「レイヤのハンドルを
  直接 `DeleteObject` する」の【追記】）では、コマンドが戻った時点ではイベントは閉じなかった。

## 打ち切った調査: プラグインから `DoMenuTextByName` 相当（メニューコマンドを名前で起動）を呼ぶ

> **経緯:** 当時（[issue #27](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/27)）は
> 「メニューコマンドを名前・識別子・ID のいずれで指定しても起動できる汎用 API は ISDK / VWFC に
> 存在しない」と書いたが、`DoMenuName` という綴りを語の総当たりに入れ忘れていた。
> [issue #217](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/217) で
> `ISDK::DoMenuName(const TXString& name, short chunkIndex)`（`ISDK.h:1722`、旧 `GS_DoMenuName`）
> を見つけ、実機でコマンドが実行されることを確かめた。指すのは内部名（外部メニューファイル名 /
> MITM リソース名）で、居ないコマンドなら `-3`、実在すれば `0` が返った。スクリプトエンジンに
> 取り消しスタックへレイヤを 1 枚積ませてから `DoMenuName("Undo", 0)` を呼ぶと、そのレイヤは
> 消え、開いていた undo イベントも終わった（`building` が `yes` → `no`）。詳細は
> [プラグイン間の呼び出し](../Findings/Plug-in%20Interop.md)の 2。

**（以下、#27 の検索の記録）**

- `DoMenuText` / `DoMenuTextByName` という名前が SDK 内に現れるのは
  `Include/vs.py`（VectorScript / Python バインディングの宣言だけを集めた、C++ の
  コンパイル対象には入らないスタブファイル）**のみ**だった。ISDK・VWFC の
  ヘッダにも実装ソース（`SDKLib/Source`）にも同名の呼び出しは無い。`vs.py` 側の
  注釈でも `DoMenuText` は "Obsolete procedure"（廃止済み）とされている。
- 「メニュー項目を実行する」に相当しうる語（`ExecuteMenuItem` / `SelectMenuItem` /
  `PerformMenuCommand` / `PostMenuCommand` / `SendMenuCommand` / `CallMenuHandler` /
  `InvokeCommand` / `ExecuteCommand` ／ `Perform*Command`）を SDK 全体
  （ヘッダ＋実装ソース）で検索しても**1 件もヒットしなかった**。
- メニューを扱うインターフェースとして `IWorkspaceMenuItem`
  （`Interfaces/VectorWorks/Workspaces/IWorkspaces.h`）は
  識別子・表示名・ショートカットキー・サブメニュー構成の get/set しか持たず、
  `Execute` / `Invoke` / `Perform` 系のメソッドは無かった。
- `ISDK::GetMyMenuCommandIndex`（`GS_GetMyMenuCommandIndex`）の説明は、いま実行中の自分の
  プラグインコマンドがメニューのどの位置から呼ばれたかを返す、だった。
- `kKludge` 系の定数を `Menu` / `Undo` / `Command` / `Execute` で検索して
  唯一ヒットしたのは `kKludgeGetMenuItemUserFriendlyName`（4136、
  `VWExtensionMenu.cpp` が使用）——メニュー項目の表示名を読むもの。

issue #27 の問い 2〜6（取り消しの対象になるイベント・呼べる文脈・複数段戻せるか・
失敗判定・半端な記録を取り消したときに図面が壊れるか）は、当時は検証しなかった。

## 間接経路: スクリプトエンジン経由で `DoMenuTextByName` 相当を呼ぶ

（要点の 6 つは [Findings](../Findings/Undo.md#間接経路-スクリプトエンジン経由で-domenutextbyname-相当を呼ぶ)。）

「SDK から VectorScript / Python の**スクリプト実行そのもの**を起動し、そのスクリプトの中で
`DoMenuTextByName('Undo', 0)` を呼ぶ」という間接経路を確かめた（当時は前節の #27 の
「直接起動する API は無い」を前提にしていた）。

- スクリプトを実行させる宣言（`Interfaces/VectorWorks/Scripting/` にある 2 つのシングルトン）:

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
  スクリプトを直接指す引数は無かった。SDK 同梱の実装ソース
  （`SDKLib/Source/VWSDK/VWFC/Tools/ImageComparisonTesting.cpp`）は、
  `TXResource("Vectorworks/Scripts/….py")` でリソースからテキストを読んでから
  別の口（`ScriptContext_Begin` / `ScriptContext_Run`。`IPythonScriptEngine` 固有で
  `IVectorScriptEngine` には無い）へ渡していた。

### 実機で走らせた結果と、その読み方（VW 2026 / macOS）

**経緯:** この節の記述は二度書き換えた。

1. 1 度目は「スクリプトは走ったが取り消しが効かない」と無印（＝実機確認済み）で書いた。
   走らせた利用者から「**スクリプトエラーが出ていた**」という指摘があった。
2. 2 度目にスクリプト自身へ**足跡**を残させたところ、スクリプトが作ったものは取り消しで
   消え、C++ が作ったものは消えなかった（下記）。

**プローブを 7 度作り直し、実機で 10 回走らせた**（うち 4 回は VectorWorks ごと落ちた）。
以下はすべて**メニューコマンドの中（`DoInterface` の中）から**走らせた結果である。

**判定に使った足跡**——取り消しが効いたなら、そのレイヤは消える。

| 印 | 誰が作るか | いつ |
| --- | --- | --- |
| `pre` | **C++（プローブ本体）** | スクリプトを呼ぶ前 |
| `m1` | **スクリプト自身** | `DoMenuTextByName('Undo', 0)` の直前 |
| `m2` | **スクリプト自身** | 同・直後 |

```
（VectorScript）PROCEDURE __ProbeSeUndo;
                BEGIN
                  Layer('m1');  DoMenuTextByName('Undo', 0);  Layer('m2');
                END;
                Run(__ProbeSeUndo);
```

呼び出しの形は `Include/vs.py` の宣言に合わせた——
`Python: vs.DoMenuTextByName(subMenu, index)` /
`VectorScript: PROCEDURE DoMenuTextByName(subMenu:STRING; index:INTEGER);` とある。

#### 実測

- VectorScript 経由の `DoMenuTextByName('Undo', 0)`: 実測は **`pre=有 / m1=無 / m2=有`**
  （2 度目・3 度目とも同じ）。消えたのはスクリプト自身が直前に作った `Layer('m1')`
  （取り消しスタックのいちばん上の 1 段）。1 度目のマーカーは C++ が作っていた。
- 連続で呼んだ: スクリプトに足跡を 3 つ作らせ、
  そのあと `DoMenuTextByName('Undo', 0)` を **3 回**呼ばせて、最後にもう 1 つ作らせた:

  ```
  Layer('a'); Layer('b'); Layer('c');
  DoMenuTextByName('Undo', 0);  ×3
  Layer('d');
  ```

  実測は **`a=無 b=無 c=無 d=有`**——**3 つとも消えた**。上限は見ていない（3 回しか
  呼んでいない）。
  - `pre=有` のままだった。
  - Python（`ScriptContext` 経由）も同じ 3 段だった。
  - `Kernel/API/ProgramVariables.h` に `varMaxUndos = 59, // short - Public for VS` という
    program variable がある（ヘッダを読んだ）。深さはこの調査では測っていない——#39 が
    確かめると決めたのは「複数段戻るか」まで。
- 呼び出しから戻った**直後に、待たずに**読むと、スクリプトが作ったレイヤが**もう存在して
  いた**（`probe-sync=有`、`elapsed=3ms`）。失敗するスクリプトを渡した回は**人がダイアログを
  閉じるまで戻ってこなかった**（`elapsed=18108ms`）。
- スクリプトに**何も作らせず** `DoMenuTextByName('Undo', 0)` だけを呼ばせても、直前に
  C++ が（undo イベントの外で）`CreateLayer` したレイヤは**残ったまま**だった（`pre=有`）。
  登録した場合は [issue #54](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/54)
  （下記「間接経路（続き）: 登録して閉じたイベントの cross-command 復元」）。
- `IVectorScriptEngine` / `IPythonScriptEngine` の `VCOMPtr` はコマンド実行中
  （`DoInterface` の中）に取れた。
- `CompileScript` は両エンジンとも通った（`ok=yes`）。
- 1 度目は
  `ExecuteScript` が `VCOMError=0`（成功）を返したのに、画面にはスクリプトエラーが
  出ていた（走らせた利用者の報告）。
- 壊れた VectorScript（`ThisIsNotAValidCall(;`）を
  渡すと `CompileScript` は `ok=no`、`ExecuteScript` は `VCOMError=1` / `succeeded=no`。
  ただし `CompileScript` の `outErrorText` は**空**、`outLineNumberOfSelectedError` は
  **-1** で、**エラーの内容も場所も取れなかった**（`showDialogs=false` で呼んでいるので
  ダイアログにも出ない）。
- `vs` を一切触らない `print('...')` だけのスクリプトを
  `ExecuteScript(script, &CDefaultPythonLogger)` で走らせると通り、`fOutput` に
  `probe: hello from python` が入った。
- `print('probe: before')` / `1 / 0` / `print('probe: after')` を渡すと、
  **`VCOMError=0`（成功）が返る**のに `fErrors` は

  ```
  Traceback (most recent call last):
    File "<string>", line 2, in <module>
  ZeroDivisionError: division by zero
  ```

  で、`fOutput` は `probe: before` だけ（`after` は無い）だった。VectorScript のエンジンには
  ロガーを渡す引数が無い。
- `IPythonScriptEngine::ExecuteScript` に `import vs` ＋ `vs.Layer('...')` だけの最小の
  スクリプトを渡すと、**VectorWorks ごと落ちた**。試した 3 通り——**ロガー無し / ロガーあり /
  `DoMenuTextByName` つき**——の**すべて**で落ちた。
  - `EXC_BAD_ACCESS`（`KERN_INVALID_ADDRESS` at `0x232`、byte write）でプロセスごと終了。
    クラッシュレポートのスタックは `PyRun_SimpleStringFlags → PyRun_StringFlags → … →
    cfunction_call → VectorWorks 内部で 2 段`——**`vs` モジュールの C 関数が呼ばれた直後**。
  - 同じ実行の中で `print` だけの Python は通っていた。
- **まったく同じスクリプト**（`import vs` ＋ `vs.Layer('probe-c4-py-layer')`）を

  ```cpp
  pyEngine->ScriptContext_Begin(script /*, IPythonLogger* = nullptr */);
  pyEngine->ScriptContext_Run();          // ロガーを使うなら ScriptContext_RunEx(logger)
  ```

  で走らせると、**`VCOMError=0` で戻り、レイヤが実際に作られた**（落ちない）。
  `IPythonScriptEngine` にだけある口で、SDK 同梱の実装ソース
  （`SDKLib/Source/VWSDK/VWFC/Tools/ImageComparisonTesting.cpp`）もこちらを使っている。
- `ScriptContext_RunEx(logger)` に `print('...')` を渡すと `fOutput` に出た
  （`ScriptContext_Begin` の第 2 引数にもロガーを取るが、出力を受けたのは `RunEx` のほう）。
- Python 経由（`ScriptContext`）で同じ足跡（`vs.Layer(m1)` → `vs.DoMenuTextByName('Undo', 0)` →
  `vs.Layer(m2)`）を走らせると **`pre=有 / m1=無 / m2=有`** だった。
- 同じ形（イベントを開く → 中でレイヤを 1 枚作る → `ExecuteScript` → `building` を読む）で
  スクリプトだけを差し替えて切り分けた:

  | 走らせたスクリプト | 戻ったときの `building` |
  | --- | --- |
  | `Layer('...')` だけ（無害） | **`yes`**（残る） |
  | `ThisIsNotAValidCall(;`（失敗する。`VCOMError=1`） | **`yes`**（残る） |
  | `DoMenuTextByName('Undo', 0)` | **`no`**（終わっている） |

  - 1 度目は「`ExecuteScript` が勝手に終わらせる」と書いたが、そのとき走らせていた
    スクリプトは `Undo` を呼んでいた。
  - イベントの中で作ったレイヤは、どの行でも残っていた。
  - 図面が壊れる（本ファイル冒頭の「ビューポートだけ消える」）事態は、いずれの回にも
    起きていない。
- `ThisIsNotAValidCall(;` を `ExecuteScript` へ渡すと、**モーダルの「スクリプトエラー」
  ダイアログ**が出て、そこには行番号も理由も載っている:

  ```
  Line #1:  ThisIsNotAValidCall(;
                                |
                    { Error: Identifier not declared. }
  ```

  一方 `CompileScript(script, false, ok, &line, &errorText)` は `ok=no` を返しながら
  **`errorText` は空、`line` は -1** だった。`ExecuteScript` の宣言にはダイアログを抑える
  引数が無い（`CompileScript` の `inShouldDisplayDialogs` に当たるものが無い）。
- `CompileScript` に `inShouldDisplayDialogs=false` を渡しているのに、**成功するたびに「コンパイルに成功しました」の
  モーダルダイアログが出る**。同じスクリプトで `CompileScript` と `ExecuteScript` を
  3 回ずつ呼んで所要時間を並べると、はっきり分かれた:

  | 呼び出し | 1 回目 | 2 回目 | 3 回目 |
  | --- | --- | --- | --- |
  | `CompileScript(showDialogs=false)` | **2123ms** | **2159ms** | **1503ms** |
  | `ExecuteScript` | 12ms | 0ms | 0ms |

  秒単位で掛かった間は、人がダイアログを閉じるのを待っていた（3 回とも掛かった）。
  `ExecuteScript` は成功時に何も出さなかった。
  - 後日、`CompileScript` を**一度も呼ばない**プローブを走らせたところ、**ダイアログは
    1 度も出なかった**（`ExecuteScript` は 3 回呼んでいる。所要は 3〜24ms）。
  - `false` で呼んだとき、エラーの内容は `errorText` にも来なかった（上記）。

#### プローブの作り直しの記録

- 1 度目は `VCOMError=0` を「走った」と読み、C++ が作ったマーカーでスクリプトの取り消しを
  測っていた。スクリプト自身に足跡（レイヤ）を作らせた 2 度目で結果が変わった。
- Python のロガーを最初は `nullptr` で渡しており、やり直しになった。
- 落ちる見込みのある呼び出しは後ろへ回し、直前に印を出した。節に入る前と出た後をログの
  隣のファイルへ書き、前回落ちた節（入ったのに出ていない節）を次の走行で飛ばした。
  4 度目は 4 回の走行で 4 つの節を通し切った（利用者に頼んだのは「もう一度走らせて
  ください」だけ）。
- ダイアログの出所は所要時間で測った（失敗するスクリプトの `ExecuteScript` は
  **18108ms**、他は 0〜17ms）。
- 1 秒級の呼び出しが走行で最初のエンジン呼び出しだったため、同じ呼び出しを 3 回続けて
  所要を並べた（`CompileScript` は 2123 / 2159 / 1503ms、`ExecuteScript` は 12 / 0 / 0ms）。
- イベントを閉じた犯人は、「イベントを開く → レイヤを作る → `ExecuteScript` →
  `building` を読む」を固定し、スクリプトだけを 3 通りに差し替えて割った（1 回の走行で
  3 行の表）。

## 間接経路（続き）: 登録して閉じたイベントの cross-command 復元

[issue #54](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/54)、
`probes/runtime/undo-registered-cross-command/`。前節の `pre` マーカー（issue #39）は
undo イベントの外で作られていた。ここでは登録して閉じた場合を測った。

- **報告の経緯**: ホームズ君 IFC 取り込みプラグインの実機フィードバック（round 18・
  VW 2026/macOS）で、`SetUndoMethod(kUndoSwapObjects)` ＋ `NameUndoEvent` でイベントを
  開き、作ったレイヤを `AddAfterSwapObject` で登録し、コマンドの終わりに
  `EndUndoEvent()` で閉じる——という Findings 冒頭どおりの作法を通した実装で、
  **1 段の `DoMenuTextByName('Undo', 0)` により、前回のコマンド実行が描いた
  1319 件・レイヤ 23 枚・シート 14 枚がすべて消え、取り込み前から在ったレイヤへ
  描いた分まで戻った**という報告があった（issue #54）。
- **最小構成で確かめ直した**（1 レイヤ・1 登録。新規の空図面）。1 回目でレイヤを作って
  登録・close し、**別のコマンド実行**（同じプローブをもう一度選ぶ）から 2 回目で
  `DoMenuTextByName('Undo', 0)` を呼び、読み戻しで判定する
  （`probes/runtime/undo-registered-cross-command/probe.cpp`）。
  - **1 回目の実測は `before=残っている / after=残っている`** だった。この回は、直前まで
    走らせていた（static 変数で前回状態を覚える設計だった）旧版のプローブが、バグで
    **毎回「1 回目」を実行し続けて同名のマーカーレイヤを複数枚作っていた**同じ図面の上で
    走らせたもので、判定は `LayerExistsByName` で名前を引いていた（同名レイヤが複数残る
    ことを許すかどうか自体は未確認）。
  - **直後に新規の空図面で撮り直したところ、`before=残っている / after=消えた`** だった
    （`ExecuteScript` の `VCOMError=0 / succeeded=yes` で、失敗の形跡も無い）。
- 実務上の作法は [Findings](../Findings/Undo.md#間接経路続き-登録して閉じたイベントの-cross-command-復元)。

## レイヤのハンドルを直接 `DeleteObject` する

[issue #23](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/23) の
もう 1 つの筋——「前回このプラグインが作ったデザインレイヤを、undo に頼らず**レイヤの
ハンドルを直接消して**取り除く」——を実機（`probes/runtime/delete-layer/`）で確かめた。

- `sdk-grep` で `DeleteLayer` / `DelLayer` / `kObjectTypeLayer` を検索してもヒットせず、
  `RemoveStoryLevel` の `bDeleteLayer` 引数がストーリレベル解除のついでに消す経路として
  見つかっただけだった。プローブはレイヤのハンドルへ汎用の
  `ISDK::DeleteObject(MCObjectHandle h, Boolean useUndo = true)`（データタグの後始末で
  既に使っているのと同じ呼び出し）を呼んだ。
- 非アクティブなデザインレイヤ＋その上の矩形を用意して `DeleteObject(layer, true)` を
  呼ぶと、削除後に図面を辿っても同名レイヤは見つからず、VW も落ちなかった。図形だけが
  レイヤを失って残るような状態は観測されなかった（矩形のハンドル自体が無効化されたか
  までは個別に確認していない）。
- アクティブレイヤ自身を `DeleteObject` で消してもクラッシュしなかった。削除直後に
  `GetCurrentLayer()` を呼ぶと、別の既存レイヤへ切り替わっていた（今回は、そのセッションで
  元々アクティブだった「共通」レイヤに戻った）。「全レイヤを消して 1 枚も残らない」状態は
  試していない。
- プローブ開始時点では `IsCurrentlyBuildingAnUndoEvent()` が `false` だったが、1 回目の
  `DeleteObject(layer1, true)` の直後には `true` に変わり、その後プローブ関数が `return`
  するまで（＝ `EndUndoEvent` の類を 1 度も呼ばないまま）ずっと `true` のままだった。
  - **【追記】そのまま返した場合**（[issue #213](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/213)。
    OS のタイマーを併用したプローブ）: `DeleteObject(h, useUndo=true)` が開けた
    イベントを閉じずにメニューコマンドから返した回では、**`kNotifyUndoEndEvent` が
    来たのは 53 秒後**で、それまで**刻み 181 回すべてが `IsCurrentlyBuildingAnUndoEvent()`
    を `yes` と読み**、**その間に作った図形はその置き土産のイベントに入った**。
    **戻る直前に `EndUndoEvent` で閉じるようにしたら `yes` は 0 件になった**（3976 刻み）。
    周期実行との組み合わせは [周期実行と通知 7 節](../Findings/Timers%20and%20Notifications.md)。
  - **【追記】いつ閉じるかを操作ごとに測った**（Windows。mac は下記。
    [issue #247](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/247)、
    `probes/runtime/undo-auto-close/`、ビルド `4a75f9c807a9`。VW 2026 Update 6 /
    Windows 11 ARM64 の仮想環境。操作は computer use で行い、操作の時刻を記録へ書き込んで
    並べた）。
    `DeleteObject(h, useUndo=true)` で開いたイベントを閉じずに置き、50 ms の刻みで
    アクティブな図面と `IsCurrentlyBuildingAnUndoEvent()`、`kNotifyUndoEndEvent` を記録した。
    開いた場所はメニューコマンドの中（段 0）と OS タイマーの刻みの中（段 1。ブリッジの形）の
    2 通りで、どちらも同じ結果だった:

    | 操作 | `building` | `kNotifyUndoEndEvent` |
    | --- | --- | --- |
    | 何も触らずに 2 分（段 0） | `yes` のまま | 来ない |
    | 図面の上でマウスを動かす | `yes` のまま | 来ない |
    | 図面の空いた所を 1 回クリック（セレクションツール） | `yes` のまま | 来ない |
    | メニューを開いて Esc で閉じる | `yes` のまま | 来ない |
    | **矩形ツールで矩形を描く**（段 0・段 1 とも） | **`no` になる**（次の刻みから） | **2 回**（3〜5 ms 差。描き終えたクリックの時刻） |
    | ファイル ＞ 新規 で 2 枚目を開く（段 1） | 新しい図面は `no` | 来ない |
    | **タブで元の図面へ戻る** | **`yes` のまま**（閉じていなかった） | 来ない |

    - 矩形が置き土産と同じイベントに入ったか——1 回の取り消しで一緒に戻るか——は
      Windows では確かめていない。
    - 2 枚目の図面が前面にある間、`IsCurrentlyBuildingAnUndoEvent()` は `no` と読め、元の
      図面へ戻ると `yes` だった。#245 では ⌘N の直後にブリッジが戻った。
    - #245 では何もせず 7 分閉じなかった。#213 の 53 秒の間の操作の記録は無い。
    - この後、ブリッジは走らせた後に自分で閉じるようにした（[#266](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/266)。
      `plugin/src/McpBridge.cpp` の `RunTargets`）。
    - **mac でも同じ——段 0・段 1 とも**（VW 2026 Update 6 / macOS 27.0.1。同じプローブ。
      2026-10-27。段 1（刻みの中で開く。ブリッジの形）はビルド `5e9af2e4bed5` で MCP ブリッジ
      から、段 0（メニューコマンドの中で開いて閉じずに返す。#213 の形）はビルド
      `6459bc6b895a` でメニュー「SDK 実機プローブ…」から仕掛けた。段 0 をブリッジから
      走らせないのは、#266 のブリッジが走らせた直後に閉じてしまうため。操作は computer use
      で行って `mark` 行を記録へ足した。マウスを動かす・メニューを開くは実際のマウスで、
      それ以外は AX か背景からの合成クリック）。

      | 操作 | 段 0（メニューコマンド） | 段 1（刻み） | `kNotifyUndoEndEvent` |
      | --- | --- | --- | --- |
      | 結果ダイアログを閉じてコマンドから戻る | `yes` のまま（ダイアログの間は `NSModalPanelRunLoopMode`） | — | 来ない |
      | 何も触らずに 2 分 | `yes` のまま | `yes` のまま | 来ない |
      | 図面の上でマウスを動かす | `yes` のまま | 試していない | 来ない |
      | 図面の空いた所を 1 回クリック（セレクションツール） | `yes` のまま（クリックの間は `NSEventTrackingRunLoopMode`） | `yes` のまま | 来ない |
      | メニュー「加工」を開いて Esc で閉じる | `yes` のまま（開いている間は `NSEventTrackingRunLoopMode`） | 試していない | 来ない |
      | 基本パレットで矩形ツールを選ぶ | `yes` のまま | `yes` のまま | 来ない |
      | ファイル ＞ 新規 で 2 枚目を開く | 「用紙の作成」の最中は `yes`、開いた新しい図面は `no` | 同じ | 来ない |
      | **ウインドウ メニューで元の図面へ戻る** | **`yes` のまま** | **`yes` のまま** | 来ない |
      | **矩形ツールで矩形を描き終える**（2 点目のクリック） | **`no` になる** | **`no` になる** | **2 回**（同じミリ秒。ランループの外＝クリックの処理の中） |

      - 矩形は、ドラッグを素早く送ると「2 点クリックの 1 点目」として扱われ（対角コーナー
        モード）、2 点目をクリックしたときに描き終わる。閉じたのはこの 2 点目のクリック。
      - 段 0 の後に「編集 ＞ 取り消し」を 1 回掛けると、矩形が消え、オブジェクト情報に**長さ 500・角度 0° の直線**が選ばれて
        出た——置き土産のイベントが `DeleteObject(useUndo=true)` で消した線
        （(0, 500)–(500, 500)）と同じ寸法だった。線そのものは画面の外で、図面の上では
        見ていない。
  - **【追記】`CreateLine` / `CreateLocus` だけでは開かなかった——Windows も mac も**（同じ
    プローブ。メニューコマンドの中でも刻みの中でも、呼んだ直後の旗は `no`。続けて
    `DeleteObject(h, useUndo=true)` を呼んだ時点で `yes`）。冒頭の
    「素の 2D 図形を 1 つ作るだけでも開く——`CreateLocus` で確かめた」は #206 の **mac** の
    実測で、#247 では mac のメニューコマンドの中でも刻みの中でも開かなかった。#206 と #247 で
    呼ぶ前に何をしていたかの違いは切り分けていない。
- **ビューポートの載ったシートレイヤも `DeleteObject(sheetLayer, true)` で丸ごと消せる**
  （実機。[issue #29](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/29)、
  `probes/runtime/delete-sheet-layer-viewport/`）。ビューポート 2 枚＋注釈空間へ
  `ISDK::AddViewportAnnotationObject` で足したオブジェクト（データタグ・グラフィック
  凡例の代用としてロケータ点）を載せたシートレイヤを削除したところ、シートは削除後に
  同名レイヤとして辿れなくなり、VW も落ちなかった。削除後に図面上の全レイヤ直下を
  走査しても、ビューポート・注釈オブジェクトの痕跡は残っていなかった。
  `GS_CreateViewport` の説明: "The specified parent handle may only be a layer or a group
  contained within a layer, nested or otherwise"。表示するデザインレイヤは作成後に
  `ISDK::SetViewportLayerVisibility(viewport, designLayer, kLayerVisibilityNormal)` で設定した。
- シートレイヤを消しても、ビューポートが表示していたデザインレイヤとその上の図形（矩形）は
  そのまま図面に残った。
- 逆順: ビューポートがまだ参照しているデザインレイヤを先に `DeleteObject` で消しても
  VW は落ちなかった。参照先を失ったビューポートはシートレイヤの直下メンバとしてそのまま
  辿れ、その状態で `UpdateViewport` を呼んでも落ちなかった。その後シートレイヤ自体も
  `DeleteObject` で消せ、両方消えた。
- ビューポートの作成・更新（`CreateViewport` / `SetViewportLayerVisibility` /
  `UpdateViewport` / `AddViewportAnnotationObject`。断面ではない平面ビューポート）の後、
  最初の `DeleteObject` を呼ぶ前の時点で `IsCurrentlyBuildingAnUndoEvent()` が既に
  `true` だった。
