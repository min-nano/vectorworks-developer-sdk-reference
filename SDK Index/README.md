# SDK の宣言索引（`SDK Index/`）

Vectorworks SDK（`SDKLib/`）に含まれる **C++ の宣言の一覧**と、`vs.py` の
**VectorScript / Python 関数の書式**です。リモートセッションには SDK が無いので、
「このシグネチャは」「この enum にどんな値があるか」「このクラスはどこに宣言があるか」を
**ci-debug を往復させずに `Grep` で引く**ためにあります。

- **生成物なので手で直さない。** 週 1 回、
  [`.github/workflows/sdk-index.yml`](../.github/workflows/sdk-index.yml) が最新の SDK から
  作り直し、変わっていれば PR を立てる（実体は
  [`scripts/sdk-index-sync.sh`](../scripts/sdk-index-sync.sh) /
  [`scripts/sdk-index.py`](../scripts/sdk-index.py)）。形を変えたいときはスクリプトを直す。
- **元にした SDK の版と件数**は [`INDEX.md`](INDEX.md)（これも生成物）。
- **載っているのは宣言だけ。** コメント（説明文）と関数本体は載せていない（SDK をそのまま
  再配布しないため）。**説明や実装を読みたいときは、ここで場所を突き止めてから
  ci-debug の `sdk-ls` / `sdk-grep` で原本を引く**（CLAUDE.md「CI デバッグ」）。
- **宣言があることは「動く」ことを意味しない。** ここは【ヘッダ根拠】の水準の情報しか
  持たない。実挙動は [`Findings/`](../Findings/README.md) を先に読む。

## 置き場所

索引は **SDK のディレクトリ 1 つにつき 1 枚**。`SDKLib/` からの相対パスをそのまま写して
あり、`Include/Interfaces/VectorWorks/` の宣言は
`SDK Index/Include/Interfaces/VectorWorks.md` に、原本のファイルごとの見出しで並ぶ。

| 索引 | 中身 |
| --- | --- |
| `Include/Interfaces/VectorWorks.md` | `ISDK`（`gSDK->…`）ほか VCOM インタフェース |
| `Include/Interfaces/VectorWorks/<分野>.md` | 分野別の VCOM インタフェース（`Extension` / `Filing` / `UI` …） |
| `Include/Kernel/API.md` | 旧 API（`GS_…`・`kcb…`）・`MiniCadCallBacks.h` の構造体と定数・オブジェクト変数 |
| `Include/VWFC/<分野>.md` | VWFC（`VWFC::VWUI` のダイアログ部品・`VWFC::VWObjects` ほか） |
| `Source/VWSDK/…` | 同梱の**実装ソース**（`.cpp`）にある関数の定義位置。本体は載せない |
| `VectorScript.md` | `vs.py` の関数名・VectorScript / Python の書式・分類 |

サードパーティ（`glm` / `json`）と、`ISDK` の写しでしかない `MockUp` は載せていない。

## 1 行の読み方

```
- `ISDK.h:1376` prototype `VectorWorks::ISDK::GetNamedLayer` — `virtual MCObjectHandle GetNamedLayer(const TXString& layerName) = 0;`
```

| 部分 | 意味 |
| --- | --- |
| `ISDK.h:1376` | 原本のファイル名と行。ディレクトリは索引ファイルの場所（ここでは `Include/Interfaces/VectorWorks/`） |
| `prototype` | 種類。`prototype`（宣言）/ `function`（定義。`.cpp` やインライン）/ `class` / `struct` / `enum` / `enumerator` / `typedef` / `macro` / `member` / `variable` / `namespace` / `union` |
| `VectorWorks::ISDK::GetNamedLayer` | 修飾名。無名の `enum` / `union` は `(anonymous)` |
| 最後のコード | **原本から写した宣言文**（コメントを除き空白を詰めたもの）。既定引数・`virtual`・`= 0`・`const`、列挙子の値、`#define` の 1 行目が残る。400 字で切る |

## 引き方の例

リモートセッションの `Grep`（ripgrep）で、`path` を `SDK Index` にして引く。

| 知りたいこと | パターンの例 |
| --- | --- |
| ISDK のメソッドのシグネチャ | `ISDK::GetNamedLayer\b` |
| 名前の一部から探す | `prototype .*ISDK::.*Layer` |
| enum の値の一覧 | `EButtonImagePos::`（列挙子は修飾名が enum 名で始まる） |
| クラスの宣言位置と基底 | `class .*VWDialog\b` |
| VWFC の実装がどの `.cpp` にあるか | `function .*VWImagePopupCtrl::CreateControl` |
| 定数・`#define` の値 | `` `kWallNode` ``・`` `kcbGetNamedLayer` `` |
| VectorScript 関数の書式 | `\| GetLayerByName \|`（`VectorScript.md`） |

**ここに無い名前は SDK に無い**と読んでよい（ヘッダと同梱ソースの全体を読んでいる）。ただし
マクロで組み立てられる名前（`enum_def(...)` など）は、展開前の形でしか載っていない。
