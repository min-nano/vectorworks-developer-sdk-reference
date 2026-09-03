# 実測知見（Findings）

公式リファレンス（`Info/` / `Versions/`）に載っていない **Vectorworks SDK の実際の挙動**を、
実機（ローカルの VectorWorks）と CI（SDK ヘッダ検索）での調査結果として集めたものです。
大半は **CI では検出できず、実機でしか判明しなかった**落とし穴の記録で、同じ失敗を
二度踏まないためにあります。

初出はホームズ君 IFC 取り込みプラグイン
（[vectorworks-plugin-import-ifc-homeskz](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)）
の開発メモです。以後の調査（issue → CI 調査 → PR → 実機確認）の確定結果もここへ足します。
運用の規約は [`CLAUDE.md`](../CLAUDE.md) を参照してください。

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
- **「打ち切った調査」**の節は、実機まで行って「できない」と確かめた記録。
  状況（VW のバージョン・SDK の版）が変わらない限り**再調査しない**。
- 実装例として挙げているファイルパス（`draw/DrawUtil` など）は
  [ホームズ君 IFC 取り込みプラグイン](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz)
  のソースを指す。動く実装が要るときはそちらを読む。

## 目次

| ファイル | 中身 |
| --- | --- |
| [VectorScript to SDK Mapping](VectorScript%20to%20SDK%20Mapping.md) | VectorScript（`vs.*`）の名前と ISDK の対応・非対応の一覧。**着手前にまず引く** |
| [Parametric Objects](Parametric%20Objects.md) | PIO 全般。パスの型・ストーリバウンド・パラメータ名・設定ダイアログ抑止・スタイル・ポップアップの値・構造材の自動結合（作る API は無い）・自作 PIO の作法 |
| [Symbols](Symbols.md) | シンボル配置の 2 つの作法（レイヤへ入れ直す・非 nil を成功と見ない）・高さ合わせ・**定義を組み立てる**（中身を入れたら `ResetObject`）・用紙基準の大きさ・インスタンスの反転（負の倍率） |
| [Walls](Walls.md) | 壁の生成・高さ・結合（`JoinWalls`）・キャップ・2D 表現の更新 |
| [Slabs and Extrudes](Slabs%20and%20Extrudes.md) | スラブの構成層・`VWExtrudeObj` の平行移動・`ModifySlab` の不具合（打ち切り） |
| [Roof Faces](Roof%20Faces.md) | 屋根面オブジェクトの正しい作り方（コンストラクタは使えない・オブジェクト変数で与える） |
| [Layers and Stories](Layers%20and%20Stories.md) | デザインレイヤの重ね順（高さの降順）・並べ替えの唯一の手段・重ね順上書きは書けない（打ち切り） |
| [Viewports](Viewports.md) | ビューポートのクラス表示・2D/平面への作り直し・断面ビューポートの新規作成と範囲 |
| [Sheet Layers and Page Layout](Sheet%20Layers%20and%20Page%20Layout.md) | 用紙と印刷可能領域の読み取り（単位の癖）・用紙の位置は読めない・置いた後に測って動かす作法 |
| [Data Tags](Data%20Tags.md) | データタグのタグレイアウト・式・関連付け・配置の落とし穴 |
| [Graphic Legends](Graphic%20Legends.md) | グラフィック凡例の内部構造・ソース定義とフィルタの保存先・縮率（打ち切り） |
| [Tagged Data](Tagged%20Data.md) | タグ付きデータ（`TaggedData*`）の型・読み書きの癖・補助オブジェクトの生バイト構造 |
| [Undo](Undo.md) | undo イベントの開き方・登録の作法・戻らないもの |
| [Progress and Diagnostics](Progress%20and%20Diagnostics.md) | 進捗ダイアログ・`DoYield`・例外境界・VW バージョンは取れない |
| [Layout Dialogs](Layout%20Dialogs.md) | レイアウトダイアログ（`VWDialog`）の大きさ・コントロール・イベントの作法 |
| [TXString](TXString.md) | 文字列型。`const char*` / `std::string` からの暗黙変換・**リテラルで多重定義が曖昧になる**・UTF-8 の出し入れ |
| [Plug-in Modules](Plug-in%20Modules.md) | プラグインモジュールの読み込みと入れ替え。起動時にしか読まれない・VCOM の初期化はモジュールごと・SDK をリンクするモジュールが必ず定義する 2 つ・**本体を外部モジュールへ出せば再起動なしで入れ替えられる**（mac 実測） |
| [Investigation Techniques](Investigation%20Techniques.md) | 調査の作法。読み戻す・測る・正解と差分を取る・setter の戻り値を信じない |

## 知見を足すときの決まり

- **なぜ（意図・根拠）と実測値を書く。** 「動かない」だけでなく、何を試してどう
  切り分けたかを残す（次に同じ調査をしないため）。
- **どの水準で確認したかを明記する**（上記の印）。実機確認前の内容を確認済みのように
  書かない。
- **打ち切った調査は消さずに残す。** 「できない」は「できる」と同じ価値がある。
- 置き場所は既存のトピックファイルへ。どれにも収まらないときだけ新しいファイルを作り、
  この目次へ 1 行足す。
