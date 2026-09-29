# 図面ラベル（`Drawing Label2`）

シートレイヤのビューポートに図面タイトル・縮尺・図番を出す PIO。**ラベルレイアウト**
（ラベルの絵の組み方）を SDK から per-instance で組むときの実測。

調査の発端は「軸組図（断面ビューポート）ごとに、その真下へ図面タイトルだけを出したい
（図番は出さない）」という要求（[issue #149](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/149)）。

## 先に結論

**図面タイトルを出すだけなら、レイアウトに触らず `Title` を書けばよい。** ビューポートとの
リンクは注釈へ入れれば勝手に張られるが、**置いた直後の `Title` は当てにならない**ので、
自分で書くのが確実である。

```cpp
gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);  // 作る前に 1 度

MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", WorldPt(x, y), 0.0, false);
gSDK->AddViewportAnnotationObject(hViewport, h);   // 注釈へ入れる（GetViewportGroup は使わない）

// ビューポートの図面タイトルは ovViewportDescription（1032）。そこから読んで書き写す。
TVariableBlock block;
TXString        title;
gSDK->GetObjectVariable(hViewport, 1032, block);
block.GetTXString(title);

VWParametricObj(h).SetParamValue("Title", title);  // Link State が 0 でも 1 でも効く
gSDK->ResetObject(h);
```

**図番と縮尺を出したくないなら、レイアウトから落とす**——パラメータに表示を切る欄は無い。
落とし方は「**要るものだけを複製**して新しい群へ入れ、渡し直す」（下記「SDK から組む手順」）。

## どのオブジェクトか

| | |
| --- | --- |
| universal 名 | `Drawing Label2` |
| 内部 ID / スタイルのサブタイプ | **642**（`kInternalID_DrawingLabel2`。[Parametric Objects](Parametric%20Objects.md) / [Symbols](Symbols.md)） |
| 旧版 | `kInternalID_DrawingLabel = 96`（`Drawing Label`。VW 2026 の既定は 2 のほう） |

**SDK に `DrawingLabel` という名前の API は 1 つも無い**（`sdk-grep` で当たるのは上の 2 つの
内部 ID 定数と `kObjectStylesDrawingLabelFolder` だけ）。専用の口は無く、**PIO の
パラメータとプロファイルグループだけで組む**ことになる。

## パラメータ（16 件）

新規に作った直後の実測（VW 2026 / mac・日本語版）。欄型は `EFieldStyle`
（`3`=実数 / `4`=文字 / `7`=座標 / `8`=ポップアップ / `1`=整数 / `2`=真偽 / `14`=レイアウト編集）。

| universal 名 | 欄型 | 作った直後の値 | 表示名 |
| --- | --- | --- | --- |
| `ScaleFactor` | 3 | `1` | 記号の倍率 |
| **`Title`** | 4 | （ビューポートの図面タイトル） | 図面タイトル |
| **`Drawing`** | 4 | `1`（シートレイヤ内で自動採番） | 図番 |
| `Sheet` | 4 | （シートレイヤ名） | シートレイヤ番号 |
| `BackRefSheetNo` | 4 | （空） | 逆参照シートレイヤ番号 |
| `ScaleDisplay` | 8 | `Architectural` | 縮尺表示 |
| `Custom Scale` | 4 | （空） | カスタム縮尺 |
| `LineMode` | 8 | `Auto-Fit` | ラベル幅モード |
| `Length` | 7 | `130` | 印刷時の線の長さ |
| `__Version` | 1 | `2600` | （表示名なし） |
| `__Edit Layout` | 14 | （空） | 図面ラベルレイアウト |
| `ControlPointX` | 7 | （実数） | （表示名なし） |
| `ControlPointY` | 7 | `0` | （表示名なし） |
| **`Link State`** | 1 | `0` / `1` | リンク状況 |
| `Note` | 4 | （空） | 備考 |
| `World-based` | 2 | `False` | `__NNA_DO_NOT_CHANGE` |

- **「図番を出さない」「引出線を出さない」ための真偽値の欄は無い。** 出す / 出さないは
  **レイアウトに何を置くか**で決まる（下記）。`Length` はタイトルの下線の長さで、
  引出線の欄ではない。
- `Drawing`（図番）は**同じシートレイヤに置くたび 1 ずつ増える**（実測: 1 → 2 → 3 → 4）。

## ラベルレイアウトはプロファイルグループにある

**レイアウトの実体はラベル自身のプロファイルグループ**（`ISDK::GetCustomObjectProfileGroup` /
`SetCustomObjectProfileGroup`）。[Data Tags](Data%20Tags.md) のタグレイアウト・
[Level Objects](Level%20Objects.md) のマーカーレイアウトと同じ作りである。

- **`GetCustomObjectProfileGroupInAux` は `nil`。** 両方見る必要は無い。
- 既定の中身（実測。型は `Objs.TDType.h`）:

```
レイアウト[0] 型=10（テキスト）'タイトル'
レイアウト[1] 型= 2（線）          ← タイトルの下線
レイアウト[2] 型=10（テキスト）'縮尺'
レイアウト[3] 型= 6（円弧）        ← 図番を囲む丸
レイアウト[4] 型=10（テキスト）'#'
レイアウト[5] 型= 0（終端）
```

- **テキストに名前は付いていない**（`GetObjectName` は空）。**レコードも付いていない。**
- ラベルの描いた図形を自分で走査すると、**型 90（`kUndoPlaceholderNode`）が混ざる**ことが
  ある。図形として数えない。

## 動的な文字列の式——**データタグと同じ綴り**だが、**書き込む口が無い**

レイアウトのテキストが持っている式（生バイトから UTF-16 で読み出した実測値）:

| 見えている文字 | 持っている式 |
| --- | --- |
| `タイトル` | **`#Drawing Label2#.#Title#`** |
| `縮尺` | `縮尺: #Drawing Label2#.#Scale#` |
| `#` | `#Drawing Label2#.#Drawing#` |

**綴りはデータタグとまったく同じ `#レコード#.#フィールド#`** で、レコード名は PIO の
universal 名、フィールドはそのパラメータ名である。**「図面ラベルは別の書き方になるはず」
という見込みは外れだった。**

- `#Drawing Label2#.#Scale#` の `Scale` は、**上の 16 件には無い欄**である
  （レコードには在るが OIP に出ない計算欄）。
- **違うのは保存先。** 式は**テキストにぶら下がるユーザーデータ**（補助オブジェクト
  2 つ。型 **76** ＝ `kUserDataNode`。2 つ目の容れ物 ID が `'oidl'`）に UTF-16 で入って
  いる。**`IDataTagTextLinkSupport` は使えない**:

  | 呼んだもの | 結果 |
  | --- | --- |
  | `IsSupported(テキスト)` | **`false`** |
  | `GetIsLinked(テキスト)` | `false` |
  | `GetFormula(テキスト)` | **空文字列** |

  したがって **`SetFormula` で式を持たせることはできない**——データタグのレイアウトを
  組む手順（[Data Tags](Data%20Tags.md)）はそのままでは使えない。

- **`IDataTagSupport::UpdateUserDefinedTextsUIDs` は要らない。** 呼ばずに組んだレイアウトが
  正しく図面タイトルを描いた（実測）。式を持たせる口が無い以上、呼ぶ場面も無い。

## SDK から組む手順——**複製して、要らないものを落とす**

**新しく作ったテキストに式は持たせられない。** 文字列だけを真似ても効かないことを、
同じ図面で 2 通り並べて確かめた:

| 渡したレイアウト | **描かれた文字** |
| --- | --- |
| 元のテキストを **`DuplicateObject` で複製**した 1 つだけ | **`アルファ図-書き換え後`**（＝`Title` の値） |
| 同じ文字列（`タイトル`）から **`CreateTextBlock` で新しく作った**テキスト 1 つだけ | **`タイトル`**（文字どおり。置き換わらない） |
| 複製したテキストの**文字を `SetText` で潰して**から渡す | **`手で書いた図面名`**（＝`Title` の値。**文字は無視される**） |

つまり**効いているのは文字ではなく、テキストが抱えている隠れた状態**である。だから
組み方は「**要るものだけを複製して新しい群へ入れ、渡し直す**」一択になる。

```cpp
MCObjectHandle hOld   = gSDK->GetCustomObjectProfileGroup(h);
MCObjectHandle hGroup = gSDK->CreateGroup(false);

// 例: タイトルのテキストと下線だけを残す（＝縮尺と図番を落とす）
bool tookText = false, tookLine = false;
for (MCObjectHandle m = gSDK->FirstMemberObj(hOld); m != nil; m = gSDK->NextObject(m))
{
	const short type = gSDK->GetObjectTypeN(m);
	if (!((type == kTextNode && !tookText) || (type == kLineNode && !tookLine)))
		continue;
	gSDK->AddObjectToContainer(gSDK->DuplicateObject(m), hGroup);   // ★ 複製して入れる
	if (type == kTextNode)
		tookText = true;
	else
		tookLine = true;
}

gSDK->SetCustomObjectProfileGroup(h, hGroup);   // **中身を入れてから渡す**
gSDK->ResetObject(h);
```

実測（タイトル＋下線の 2 つを複製した場合）:

```
渡した後のレイアウト: テキスト'タイトル' / 線 / 終端
描かれたもの        : テキスト'後から変えた題' / 線     ← 縮尺も図番も丸も消えた
```

- **中身を入れてから渡す。** 空の群を先に渡して後から足すと迷子になる
  （[Data Tags](Data%20Tags.md)・[Level Objects](Level%20Objects.md) と同じ筋）。
- **中身を入れ替えるだけでは絵に出ない**——[Level Objects](Level%20Objects.md) と同じで、
  新しい群を作って**渡し直す**。

## ビューポートとの紐づき

**図面タイトルはビューポートが持ち、ラベルがそれを読む。** ヘッダにもそう書いてある
【ヘッダ根拠】（`Kernel/API/ObjectVariables.h`）:

```
const short ovViewportDescription = 1032;  // … corresponds to the Dwg Title field for a
                                           //   corresponding Drawing Label.
const short ovViewportLocator     = 1033;  // … corresponds to the Item field for a
                                           //   corresponding Drawing Label.
```

実測した挙動:

| 置いた場所 | `Link State` | ビューポートのタイトルを変えたとき |
| --- | --- | --- |
| **ビューポートの注釈** | **`1`** | **追随する**（ホストのビューポートの値が入る） |
| シートレイヤ直下 | `0` | 追随しない |

- **リンク先はホストのビューポートで正しい。** 図面タイトルの違う 2 枚を並べ、2 枚目の
  注釈に置いたラベルで 2 枚目のタイトルを書き換えたら、そのラベルが追随した。
- **ただし置いた直後の `Title` は当てにならない。** 2 枚目の注釈へ置いたのに 1 枚目の
  タイトルが入っていた（**その図面でいちばん最初のラベルでも同じ**だったので、直前の
  既定を引き継いだのではない）。リンクが押し込むのは**ビューポート側が変わったとき**で、
  置いただけでは解決されない。
- **`SetParamValue("Title", …)` は `Link State` が 0 でも 1 でも効く**（描く文字も変わる）。
  **置いたあと自分で書くのが確実。**
- **ただしリンクは生きたままなので、後からビューポートのタイトルが変わると上書きされる**
  （実測: 手で `手で書いた題` を書いた注釈のラベルが、ビューポートの 1032 を変えた後は
  `後から変えた題` になった）。**手で書いた値を守りたいなら、注釈ではなくシートレイヤ
  直下へ置く**（`Link State`=0）。
- **注釈へ入れる口は `ISDK::AddViewportAnnotationObject`。**
  **作りたてのビューポートでは `GetViewportGroup(vp, kViewportGroupAnnotation)` が `nil`**
  を返すので、そちらから入れようとすると置けない（1 往復無駄にした）。
  `AddViewportAnnotationObject` で入れた後は `GetViewportGroup` も取れるようになり、
  `VWViewportObj::IsViewportGroupContainedObject` も `true` を返す。

## データタグとの違い（まとめ）

| | データタグ（`Data Tag`） | 図面ラベル（`Drawing Label2`） |
| --- | --- | --- |
| レイアウトの置き場 | プロファイルグループ | **同じ**（`…InAux` は `nil`） |
| 式の綴り | `#レコード#.#フィールド#` | **同じ**（`#Drawing Label2#.#Title#`） |
| 式の読み書き | `IDataTagTextLinkSupport`（`SetIsLinked` / `SetFormula`） | **使えない**（`IsSupported`=`false`）。式はユーザーデータの中 |
| レイアウトの作り方 | テキストを**作って**式を持たせる | **既定のテキストを複製**して要らないものを落とす |
| `UpdateUserDefinedTextsUIDs` | 要る | **要らない** |
| 本文の出どころ | 関連付けた図形のレコード | ビューポートの `ovViewportDescription`(1032)。`Title` へ直接書いてもよい |

## 未確認のまま残っているもの

- **ユーザーデータに式を書き込んで、新しいテキストに欄を持たせられるか。** 容れ物 ID
  （`'oidl'`）と、中に UTF-16 の式が入っていることまでは割れているが、書き込みは試して
  いない（[Tagged Data](Tagged%20Data.md) のとおり読み出し API は当てにならないので、
  書くなら生バイトの構造を先に割る必要がある）。**複製で足りている**ため追っていない。
  必要になったら別の issue で。
