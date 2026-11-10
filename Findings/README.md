# 実測知見（Findings）

公式リファレンス（`Info/` / `Versions/`）に載っていない **Vectorworks SDK の実際の挙動**を、
実機（ローカルの VectorWorks）と CI（SDK ヘッダ検索）での調査結果として集めたものです。
大半は **CI では検出できず、実機でしか判明しなかった**落とし穴の記録で、同じ失敗を
二度踏まないためにあります。

初出はホームズ君 IFC 取り込みプラグイン
（[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)）
の開発メモです。以後の調査（issue → CI 調査 → PR → 実機確認）の確定結果もここへ足します。
運用の規約は [`CLAUDE.md`](../CLAUDE.md) を参照してください。

**ここには結論だけを書きます。** 測った環境・見本・手順・計測用のコード・実測の表や
ダンプ・切り分けの経緯は、同じトピック名のファイルで [`Metrics/`](../Metrics/README.md)
にあります（各ファイルの冒頭と結論の「実測」リンクから辿れます）。

## 読み方の規約

- **対象は Vectorworks 2026 SDK**（実機確認も VW 2026）。別バージョンで確かめ直した
  結果は、その旨を明記して追記する。
- 断りが無ければ**実機確認済み**の挙動。確認水準が下がるものだけ印を付ける:
  - **【推定】** … 実機未確認の推測（並びからの類推など）。確認したら印を外す。
  - **【ソース根拠】** … SDK に同梱された**実装ソース**（`SDKLib/Source/VWSDK/…` の
    VWFC の `.cpp`）を読んだ結果が根拠。宣言だけの【ヘッダ根拠】より強く、「なぜそう
    なるか」まで言い切れるが、**実機での実挙動は未確認**。
  - **【ヘッダ根拠】** … SDK ヘッダの宣言・全文検索だけが根拠（CI の `sdk-grep` 等）。
    実挙動は未確認。
  - **【バイナリ根拠】** … VW 本体・同梱ファイルのバイナリ（埋め込まれた文字列・取り込む
    API 名・ファイルの形）を読んだ結果が根拠。逆アセンブルはしておらず、実挙動は未確認。
- **印は「調べ残しを畳む道具」ではない。** 実機で確かめると決めて実際に走らせた調査で、
  **取りに行けば取れる答え**を【推定】【未確認】のまま確定させない——印を付けて先へ
  進んでよいのは、**最初からその調査の範囲外だと決めていたもの**（別の issue に
  切り出したもの）だけ。詳しくは [CLAUDE.md](../CLAUDE.md)「PR とマージ」3。
- **「打ち切った調査」**の節は、実機まで行って「できない」と確かめた記録。
  状況（VW のバージョン・SDK の版）が変わらない限り**再調査しない**。
- 実装例として挙げているファイルパス（`draw/DrawUtil` など）は
  [ホームズ君 IFC 取り込みプラグイン](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
  のソースを指す。動く実装が要るときはそちらを読む。

## 目次

| ファイル | 結論 | 計測記録 |
| --- | --- | --- |
| [VectorScript to SDK Mapping](VectorScript%20to%20SDK%20Mapping.md) | VectorScript（`vs.*`）の名前と ISDK の対応・非対応の一覧。**着手前にまず引く** | — |
| [Parametric Objects](Parametric%20Objects.md) | PIO 全般——パスとストーリバウンド（`ResetObject` の作り直し・0 長の死角）・パラメータ名と構造材の属性欄・断面（`MemberType`）・スタイル・`doRegen=false` で再生成を半分に・自作 PIO の作法 | [Metrics](../Metrics/Parametric%20Objects.md) |
| [Attributes and Classes](Attributes%20and%20Classes.md) | 描画属性を「クラスに従わせる」口と費用（書き込みは無料、高いのは PIO の作り直し）・既定の退避と復元・マーカーの有無 / 大きさ / 様式の罠・`ForEachClass` | [Metrics](../Metrics/Attributes%20and%20Classes.md) |
| [Symbols](Symbols.md) | シンボル配置の 2 つの作法（レイヤへ入れ直す・非 nil を成功と見ない）・高さ合わせ・**定義を組み立てる**（中身を入れたら `ResetObject`）・用紙基準の大きさ・インスタンスの反転（負の倍率） | [Metrics](../Metrics/Symbols.md) |
| [Walls](Walls.md) | 壁の生成・高さ・結合（`JoinWalls`）・キャップ・2D 表現の更新 | — |
| [Slabs and Extrudes](Slabs%20and%20Extrudes.md) | スラブの構成層・`VWExtrudeObj` の平行移動・`ModifySlab` の不具合（打ち切り） | — |
| [Roof Faces](Roof%20Faces.md) | 屋根面オブジェクトの正しい作り方（コンストラクタは使えない・オブジェクト変数で与える） | — |
| [Layers and Stories](Layers%20and%20Stories.md) | レイヤの列挙と重ね順（高さの降順）・レイヤ高さの読み書きと絶対 Z・縮尺・階とレベルを SDK で作る / レベル無しのレイヤを階に結ぶ | [Metrics](../Metrics/Layers%20and%20Stories.md) |
| [Layer Objects](Layer%20Objects.md) | レイヤの中の図形の辿り方と数え方。**情報パレットの「N 図形」はレイヤ直下の数**・型 `0`（終端）と `90`（undo の置き石）を除く・`traverseWhat` の違い・外形は縮尺を掛けない mm | [Metrics](../Metrics/Layer%20Objects.md) |
| [Viewports](Viewports.md) | クラス表示・2D 作り直し・**`UpdateViewport` は描き終わりを知らせない**・断面の 1064/1065 の倒し方・注釈の座標と通り芯の符号・`parentHandle`・**【罠】作ったばかりの押し出しは外接を読むか `ResetObject` するまで断面に映らない** | [Metrics](../Metrics/Viewports.md) |
| [Sheet Layers and Page Layout](Sheet%20Layers%20and%20Page%20Layout.md) | 用紙と印刷可能領域の読み取り（単位の癖）・用紙の位置は読めない・置いた後に測って動かす作法 | — |
| [Level Objects](Level%20Objects.md) | レベル基準線 / レガシーを断面ビューポートの注釈へ置く（GL・FL・軒高）。**名前＋高さは 3 つ組で出る**・`0` になるのはビューポート次第 | [Metrics](../Metrics/Level%20Objects.md) |
| [Data Tags](Data%20Tags.md) | データタグのタグレイアウトの組み立て・**`#…#` 式の文法**（連結 / 修飾子 / 条件式）・高さの綴り（末尾 G/L/S）と連動する注記・配置の落とし穴・**【訂正】`#thsep#` は修飾子の後ろでだけ効く**・**【訂正】`#StartElevation#` / `#EndElevation#` は「常に 0」ではない** | [Metrics](../Metrics/Data%20Tags.md) |
| [Drawing Labels](Drawing%20Labels.md) | 図面ラベル。レイアウトは既定テキストの複製で組む・**`Title` は必ず自分で書く**（空だと文字を描かず VP の 1032 を消す）・図番の重複・文字の大きさは紙の上の mm | [Metrics](../Metrics/Drawing%20Labels.md) |
| [Dimensions](Dimensions.md) | 寸法の作り方（`CreateLinearDimension`）・規格・文字スタイル（大きさは引き直しで反映）・連続寸法・関連付け（解く段）・注釈への置き方 | [Metrics](../Metrics/Dimensions.md) |
| [Graphic Legends](Graphic%20Legends.md) | グラフィック凡例の内部構造・ソース定義とフィルタの保存先・縮率（打ち切り） | [Metrics](../Metrics/Graphic%20Legends.md) |
| [Tagged Data](Tagged%20Data.md) | タグ付きデータ（`TaggedData*`）の型・読み書きの癖・補助オブジェクトの生バイト構造 | [Metrics](../Metrics/Tagged%20Data.md) |
| [Undo](Undo.md) | 自前の undo イベントの作法・SDK が勝手に開くイベント・保存が積む段・スクリプト / `DoMenuName` 経由の取り消し・レイヤの直接削除・**【訂正】自分のイベントで作った階・レベル種別・レベルテンプレート・クラスは取り消しで消える** | [Metrics](../Metrics/Undo.md) |
| [Progress and Diagnostics](Progress%20and%20Diagnostics.md) | 進捗ダイアログ・`DoYield`・例外境界・VW バージョンは取れない | — |
| [Layout Dialogs](Layout%20Dialogs.md) | レイアウトダイアログ（`VWDialog`）の大きさ・コントロール・イベントの作法・**モードレスなパレット（`IExtensionWebPalette`。未確認）** | [Metrics](../Metrics/Layout%20Dialogs.md) |
| [Timers and Notifications](Timers%20and%20Notifications.md) | **周期実行の口は ISDK に無い**・OS タイマーから gSDK を呼べる（共通モード必須・undo 中の書き込みは混ざる）・描画完了は待てない・刻みからの終了・パレットの間引き・mac の `CFRunLoopRunInMode` は短く刻めば回る（【訂正】） | [Metrics](../Metrics/Timers%20and%20Notifications.md) |
| [File and Folder Dialogs](File%20and%20Folder%20Dialogs.md) | OS のファイル／フォルダ選択ダイアログ。**フォルダ選択は `IFolderChooserDialog` で開ける**・キャンセルの戻り値・受け取るパスは末尾に区切りが付く UTF-8 の絶対パス・`IFileIdentifier::GetFolder` は同じ文字列を返す | [Metrics](../Metrics/File%20and%20Folder%20Dialogs.md) |
| [File and Folder Identifiers](File%20and%20Folder%20Identifiers.md) | `IFileIdentifier` / `IFolderIdentifier`。**`fbDirectory` は両 OS で常に `false`**・`ExistsOnDisk` は出力引数で見る・`SetAttributes` はファイルだけで書ける旗が OS で違う | [Metrics](../Metrics/File%20and%20Folder%20Identifiers.md) |
| [TXString](TXString.md) | 文字列型。`const char*` / `std::string` からの暗黙変換・**リテラルで多重定義が曖昧になる**・UTF-8 の出し入れ | — |
| [Plug-in Interop](Plug-in%20Interop.md) | **プラグイン間の呼び出し**。ほかのプラグインの機能を名前で呼ぶ 4 つの経路（`CallPluginLibrary` / `DoMenuName` / スクリプトエンジン / C の ABI）・引数と結果の受け渡し・呼ばれる側の作り方。**`DoMenuName` の【訂正】を含む**・**`DoMenuName` の【訂正】を含む** | [Metrics](../Metrics/Plug-in%20Interop.md) |
| [Plug-in Modules](Plug-in%20Modules.md) | モジュールの読み込みは起動時のみ・Windows は起動時に読み込み直す（OS に預ける前に PIN）・開発元不明ダイアログはフルパスで記憶・殻と本体に割れば再起動なしで入れ替え・**【罠】Windows の VW は起動時にモジュールを読み込み直す**（mac は読み込み直さない。【訂正】を含む） | [Metrics](../Metrics/Plug-in%20Modules.md) |
| [Documents](Documents.md) | 図面を開く・作る・保存・閉じる。テンプレートは `.sta` を `OpenDocumentPath` で開く・**`CloseDocument()` は確認なしに捨て、戻り値は当てにならない**・未保存の変更は読めない・上書き保存のダイアログの事前判定（mac / Windows） | [Metrics](../Metrics/Documents.md) |
| [Investigation Techniques](Investigation%20Techniques.md) | 調査の作法。読み戻す・描かれた図形を測る・正解と差分を取る・setter の戻り値を信用しない・`APP_API_*` は呼べない・**Windows 版 SDK は mac 版と同一**・同じ呼び出しの中の値は後で変わりうる | [Metrics](../Metrics/Investigation%20Techniques.md) |

## 知見を足すときの決まり

- **結論はここ、測った記録は [`Metrics/`](../Metrics/README.md)。** ここには何が言えるか
  （結論・使い方・落とし穴）を箇条書きで短く書き、根拠は Metrics の同じトピックの節へ
  リンクする。結論そのものである値（件数・所要の桁・enum の値）は残してよい。
- **なぜ（意図・根拠）と実測値を書く。** 「動かない」だけでなく、何を試してどう
  切り分けたかを Metrics に残す（次に同じ調査をしないため）。
- **どの水準で確認したかを明記する**（上記の印）。実機確認前の内容を確認済みのように
  書かない。
- **打ち切った調査は消さずに残す。** 「できない」は「できる」と同じ価値がある
  （結論と教訓はここ、潰した道の詳細は Metrics）。
- 置き場所は既存のトピックファイルへ。どれにも収まらないときだけ新しいファイルを作り、
  この目次へ 1 行足す。計測の記録があるなら `Metrics/` に同じ名前のファイルを作り、
  [`Metrics/README.md`](../Metrics/README.md) の目次にも 1 行足す（記録の無いトピックは載せない）。
