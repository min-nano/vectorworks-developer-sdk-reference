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

**スタイルは勝手に当たる。** `CreateCustomObject` した直後には、もう**ツールに設定されて
いるスタイル**が当たっている。スタイル無しで置きたいなら**組み直した後に**
`gSDK->SetPluginObjectStyle(h, 0)` を呼ぶ（下記
「[スタイルは勝手に当たる](#スタイルは勝手に当たる外し方は-setpluginobjectstyleh-0-だけ)」）。

**レイアウトのテキストに与える大きさは「紙の上の mm」である。** 容れ物（シートレイヤ・
デザインレイヤ・ビューポートの注釈）の縮尺は VW が自分で掛けるので、**自分で掛けては
いけない**。紙で 10pt なら `10 * 25.4 / 72 = 3.5278` を渡す（下記
「[レイアウトの文字の大きさ](#レイアウトの文字の大きさ与えるのは紙の上の-mm)」）。
**寸法（`ovDimFontSize`）とは逆**なので取り違えないこと。

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
	MCObjectHandle dup = gSDK->DuplicateObject(m);                  // ★ 複製して入れる
	gSDK->AddObjectToContainer(dup, hGroup);
	if (type == kTextNode)
	{
		// **文字の大きさを当てるのはここ。渡すのは「紙の上の mm」**
		// （容れ物の縮尺は VW が掛ける。下記「レイアウトの文字の大きさ」）。
		gSDK->SetTextSize(dup, 0, gSDK->GetTextLength(dup), 10.0 * 25.4 / 72.0);  // 紙で 10pt
		tookText = true;
	}
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

## スタイルは勝手に当たる——外し方は `SetPluginObjectStyle(h, 0)` だけ

**実機確認済み**（VW 2026 / mac・新規の空図面。`probes/runtime/drawing-label-style-textsize/`
を 7 回走らせた。[issue #175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)）。

| 問い | 実測 |
| --- | --- |
| 新規の空図面のツールのスタイル | `GetPluginStyleForTool("Drawing Label2")` が **`ref=179`「図面ラベル - 図番」**を返した。**文書に既定として入っている**ので、プラグインが何もしなくても当たる |
| **自動適用** | **する。** `CreateCustomObject` の**直後**に `GetPluginObjectStyle` が既にそれを返す |
| 別のスタイルでも同じ | 自分で作ったスタイル（`ref=119`）を `SetPluginStyleForTool` で設定してから作ると `ref=119` が当たる。**当たるのは「ツールにいま設定されているスタイル」**である |
| 注釈へ入れて `ResetObject` | 当たったまま（変わらない） |
| **外し方①** | **`gSDK->SetPluginObjectStyle(h, 0)` は効く。** 戻り値 `true`・`ref=0` になり、**`ResetObject` も `UpdateViewport` も越えて残る** |
| **外し方②** | **`SetPluginStyleForTool(tool, 0)` にしてから作れば、最初からスタイル無しで生まれる。** 作った後にツールの値を元へ戻して `ResetObject` しても**当たり直さない** |

- **`VWParametricObj::SetStyle(0)` でスタイルは外せない**（実機で `ref` が 1 も動かない）。
  【ソース根拠】実装が `InternalIndexToHandle(styleRefNumber)` の戻りが `nullptr` でない
  ことを門にしているので、`0` を渡すと **`SetPluginObjectStyle` が 1 度も呼ばれない**
  （`SDKLib/Source/VWSDK/VWFC/VWObjects/VWParametricObj.cpp:1703`）:

  ```cpp
  void VWParametricObj::SetStyle(RefNumber styleRefNumber) const
  {
      if(fhObject != nullptr)
      {	MCObjectHandle hStyle =  gSDK->InternalIndexToHandle( styleRefNumber );
          if(hStyle != nullptr && gSDK->IsPluginStyle( hStyle ))
          {	gSDK->SetPluginObjectStyle( fhObject, styleRefNumber );
          }
      }
  }
  ```

  **外すなら `gSDK->SetPluginObjectStyle(h, 0)` を直に呼ぶ。**
- **PIO 一般向けの「スタイル無しにする」口は SDK に無い**（`ConvertToUnstyledWall` /
  `ConvertToUnstyledSlab` / `ConvertToUnstyledRoof` の 3 つだけ。`SDK Index` を総当り。
  【ヘッダ根拠】）。
- スタイルのサブタイプ（内部 ID）は **642**。スタイルを自分で用意する手順は
  [Parametric Objects](Parametric%20Objects.md)「スタイルは SDK だけで作れる」。

## **既定のレイアウトはスタイルが持っている**——だから外す順序が決まる

**スタイル無しで作ったラベルは、レイアウトが空で何も描かない。** 実測:

| | レイアウト（プロファイルグループ） | 絵 |
| --- | --- | --- |
| スタイル**有り**で作った直後 | `型 10 2 10 6 10 0` / テキスト 3 件（`6.0` `4.23333` `4.93889`） | 描く |
| スタイル**無し**で作った直後 | **`型 17 0`** / **テキスト 0 件** | **外接 0×0 ＝ 何も描かない** |
| 有りで作って、**組み直さずに**外した | `型 10 2 10 6 10 0`（**そのまま残る**） | 描く（外接 2834.9×846.9） |

**したがって「スタイル無しのラベルを SDK で置く」には順序がある:**

```cpp
// 1. **スタイルが当たった状態で作る**（既定のレイアウトはスタイルが持っているので、
//    ツールのスタイルを 0 にしてから作ると複製する元が無くなる）。
MCObjectHandle h = gSDK->CreateCustomObject("Drawing Label2", WorldPt(x, y), 0.0, false);

// 2. レイアウトを組み直す（下記「SDK から組む手順」。複製でしか組めない）。
//    ここで文字の大きさも当てる（下記「レイアウトの文字の大きさ」）。

// 3. **組み直した後に外す。** レイアウトはインスタンスに残る。
gSDK->SetPluginObjectStyle(h, 0);
gSDK->ResetObject(h);
```

- **外し方②（ツールのスタイルを 0 にしてから作る）だけで済ませてはいけない**——
  レイアウトが空のまま、何も描かないラベルになる。
- **外してもレイアウトは消えない。** 組み直したものも、既定のままのものも残った。
  スタイルは「作るときに既定のレイアウトを配る」役で、当たり続けている必要は無い。

## レイアウトの文字の大きさ——**与えるのは「紙の上の mm」**

**実機確認済み。** 同じビューポートの注釈に「紙で 6pt になると分かっている寸法」
（`ovDimFontSize` ＝ `6 × 25.4/72 × 50` ＝ `105.83333`。[Dimensions](Dimensions.md)）を
物差しとして置き、**注釈の中では紙の 1pt ＝ 世界座標 `17.63889`** と実測してから比べた。

**レイアウトのテキストへ `SetTextSize` で与えた値は、そのまま「紙の上の mm」として効く。**
描かれる世界座標は「**与えた値 × 容れ物の縮尺**」になり、紙の上では与えた値そのものになる。

| 置き場（容れ物） | 与えた値 | 描かれた文字（世界座標） | **紙の上** |
| --- | --- | --- | --- |
| ビューポートの注釈（1/50） | `3.52778` | `176.38889`（＝×50） | **10.00000pt** |
| ビューポートの注釈（1/50） | `176.38889`（縮尺を掛けてしまった） | `8819.44444` | **500pt** |
| ビューポートの注釈（**1/100** へ変更） | `3.52778` | `352.77778`（＝×100） | **10.00000pt** |
| デザインレイヤ（1/50） | `3.52778` | `176.38889`（＝×50） | **10.00000pt** |
| シートレイヤ直下（1:1） | `3.52778` | `3.52778`（＝×1） | **10.00000pt** |

```cpp
// 紙で 10pt にしたいなら、ラベルの置き場がどこであってもこれ 1 つ。
const WorldCoord paperMm = 10.0 * 25.4 / 72.0;      // 3.5278
gSDK->SetTextSize(hDupText, 0, gSDK->GetTextLength(hDupText), paperMm);
```

- **容れ物の縮尺は VW が自分で掛ける。自分で掛けてはいけない**——掛けると紙で 500pt に
  なる。**寸法とは逆である**（寸法の `ovDimFontSize` は「紙の pt × 25.4/72 × 縮尺」を
  自分で書く。[Dimensions](Dimensions.md)「注釈へ寸法を置くときの作り方」）。
  **同じ図面の中で、寸法とラベルで作法が違う**ことを忘れないこと。
- **`ISDK::SetTextSize` の第 4 引数は `WorldCoord`＝ mm であって pt ではない。**
  `10` を渡すと **10mm ＝ 28.3pt** になる。同じ図の 6pt の寸法（2.1167mm）と比べて
  4.7 倍で、[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)
  の発端（「10pt を与えたのに 4 倍前後大きい」）はこれだった。
- **アクティブレイヤは効かない。** 容れ物とアクティブレイヤを**交差させて**確かめた
  ——デザインレイヤ(1/50) に置いてアクティブ 1:1 でも紙で 10pt、シートレイヤ(1:1) に
  置いてアクティブ 1/50 でも紙で 10pt。注釈でも、アクティブ 1/50 で掛からなかった回と
  アクティブ 1:1 で掛かった回の両方がある。**掛けているのは容れ物である。**
  （**`SetTextStyleRef` で文字スタイルを当てるときだけはアクティブレイヤが効く**。すぐ下。）
- **組み直しの時機（注釈へ入れる前／後）は効かない。** 入れ替えて測った 2 つの走行で、
  読み戻した値・外接ともに**完全に同値**だった。**ただし比べたのはどちらも下記
  「未確認のまま残っているもの」の状態（縮尺が掛かっていない側）**なので、「掛かった側でも
  同じ」とまでは言えていない。**先に組んでから入れるほうを勧める**（確定している走行は
  すべてその順）。
- **ビューポートの縮尺を後から変えても紙の見え方は保たれる**（1/50 → 1/100 で描かれた
  文字が `176.389` → `352.778`）。寸法と同じで、**一度正しく作れば崩れない**。
- **`ScaleFactor`（記号の倍率）は更に掛かる**（2 にすると紙で 20pt）。
- **`World-based`=True は文字の大きさを変えなかった**（同じ容れ物で `False` と完全に同値）。
- 既定のタイトルの大きさは **`6.0`**（紙で 6mm ＝ 約 17pt）。

### 文字スタイル（`SetTextStyleRef`）を使うなら、当てるときのアクティブレイヤを 1:1 にする

レイアウトのテキストへ文字スタイルを当てると、**その場で「文字スタイルの紙の大きさ ×
当てたときのアクティブレイヤの縮尺」が焼き付く**（実測）:

| 当てたときのアクティブレイヤ | 10pt（`ovTextStyleSize` ＝ `0.13889` インチ）を当てた直後の `GetTextSize` |
| --- | --- |
| デザインレイヤ 1/50 | **`176.38889`**（＝ 3.52778 × 50） |
| シートレイヤ 1:1 | **`3.52778`** |

ラベルはそこへ**もう一度**容れ物の縮尺を掛けるので、**1/50 のレイヤをアクティブにした
まま当てると紙で 500pt になる**。1:1 のレイヤをアクティブにしてから当てれば
`SetTextSize` と同じ結果になる。

- **`SetTextSize` で直に書くほうを勧める**——アクティブレイヤという「外の状態」に
  依らないので、いつ呼んでも結果が同じになる。
- これは [Dimensions](Dimensions.md) の〈クラスの文字スタイル〉が「繋ぐときのアクティブ
  レイヤ」で焼き付くのと同じ筋である。

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

- **作りたてのビューポートの注釈では、最初の 1〜2 本が容れ物の縮尺を拾わないことがある**
  （[issue #177](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/177)
  に切り出した）。`CreateViewport` した直後の注釈へ続けてラベルを入れると、**先頭の
  1〜2 本だけ**が「与えた値のまま」（紙で 0.2pt ＝ 見えない）で描かれ、**3 本目以降は
  正しく ×縮尺になる**（走行ごとに 1 本目までか 2 本目までかが違う）。実測で潰した道:
  `ResetObject` を増やす・`UpdateViewport` を挟む・レイアウトを入れた後に組み直す・
  アクティブレイヤを変える——**どれも効かない**。**すでに在るビューポートへ置く
  使い方（プラグインの本番）では踏んでいない**ので、原因は別の issue で追う。
  **紙の pt の規則そのものは上記のとおり確定している**（3 本目以降・デザインレイヤ・
  シートレイヤのすべてで紙で 10.00000pt）。

- **ユーザーデータに式を書き込んで、新しいテキストに欄を持たせられるか。** 容れ物 ID
  （`'oidl'`）と、中に UTF-16 の式が入っていることまでは割れているが、書き込みは試して
  いない（[Tagged Data](Tagged%20Data.md) のとおり読み出し API は当てにならないので、
  書くなら生バイトの構造を先に割る必要がある）。**複製で足りている**ため追っていない。
  必要になったら別の issue で。
