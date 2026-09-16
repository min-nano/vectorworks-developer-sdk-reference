# プラグインオブジェクト（PIO）

構造材ツール（`StructuralMember`）・通り芯（`GridAxis`）・データタグ・グラフィック凡例など
VW 標準のツールは PIO として実装されており、SDK から生成するときに共通の落とし穴がある。
自作 PIO を足すときの作法も末尾にまとめる。

## パスの型を間違えると「生成できるのに何も描かれない」

- **水平材は 2D ポリライン**（`VWPolygon2DObj`）を渡す。2 頂点の 3D ポリラインに絶対 Z を
  持たせたところ、構造材オブジェクトは数百本生成されるのに**長さ 0 で画面に何も出なかった**
  （OIP は「スパン 0 / 長さ 0」。PIO がパスを挿入点としてしか読んでいない）。
- **鉛直材（柱・小屋束）は `CreateNurbsCurve` ＋ `Add3DVertex`** で下端 → 上端の 2 点。
  平面へ落とすと 1 点に潰れるので 2D ポリラインでは表せない。`Add3DVertex` が VS の
  `AddVertex3D` に当たる（`Insert3DVertex` は別物で頂点が増えない）。`VWNURBSCurve` は
  評価専用で制御点から構築できないが、`ISDK` 側に `Add3DVertex` がある。

## `VWPolygon2DObj` は既定で「開いた」折れ線

頂点を一周ぶん足しても `SetClosed(true)` を呼ばないと、**最後の頂点から最初の頂点へ戻る辺が
描かれない**。実機では直角三角形の**斜辺だけが出ない**という形で現れ、塗りが要らない図形
ほど気付きにくい（3 辺のうち 2 辺は出ているので「描けている」ように見える）。閉じ／開きは
呼び出しごとに明示する。

## 高さ・実体を最終的に決めるのは「解決済みストーリバウンドから `ResetObject` が作り直すパス」

[issue #56](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/56) で
実機確認済み（VW 2026 / mac。`probes/runtime/custom-object-path-coordinate-system/` で
実在の構造材 PIO を対象に検証。以下の値は同プローブの実行ログそのまま）。

ホームズ君 IFC 取り込みプラグインで、構造材 PIO の柱 46 本が「オブジェクトとしては在るのに
長さ 0（実体無し）で図に出ない」事故が起きた
（[あちらの PR #113](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz/pull/113)）。
その原因と、下記「実機で3周かけて切り分けた結果」の関係が、この節でようやく判明した。

**結論**:

1. **`CreateCustomObjectPath` は世界座標で渡した2点のうち最初の点を挿入点にし、パスは
   その挿入点からの相対（ローカル）で保持し直す。** 例: `(0,0,572)→(0,0,3531)` の世界座標
   パスを渡すと、挿入点は `(0,0,572)`、読み戻したパスは `(0,0,0)→(0,0,2959)`
   （渡した差 `2959` と一致）。
2. **`SetCustomObjectPath` は変換を一切しない。** 渡した曲線をそのまま保持するだけで、
   世界座標っぽい値を渡せばそのまま、相対っぽい値を渡せばそのまま読み戻る。挿入点は
   変わらないので、**世界座標のつもりで渡すと、挿入点のぶんだけ余計にずれて描かれる**
   （実機の別報告: 挿入点Zが572のオブジェクトへ世界座標`572→3531`のパスを`SetCustomObjectPath`
   したら、実測は`1144→4103`——572だけ余計に足された）。**Create と Set は非対称。**
   `SetCustomObjectPath` を使うときは、呼び出し側が「今の挿入点からの相対」を用意すること。
3. **`SetObjectStoryBound` は record を記録するだけでなく、`ResetObject` が解決済みの
   バウンドからパスを作り直す。** 両端に `eStoryObjectBound_LayerElevation`（自階基準、
   offset=572/3531）を設定して `ResetObject` を呼ぶと、**挿入点が上端の解決済み絶対Z
   （3531）へ移り**、パスは `(0,0,0)→(0,0,-2959)` に書き換わった（符号が反転）。
   つまり「バウンドは受け渡し用データに過ぎない」は誤りで、**`ResetObject` はバウンドから
   ジオメトリを再構築する**。
4. **上下端の解決済み絶対Zが一致すると、パスが正確に0長へ潰れる。** 両方のバウンドを
   同じ絶対Z（3531）へ解決させて `ResetObject` すると、パスは
   `(0,0,0)→(0,0,-0.000000)` になった（`z1-z0 = 0`）。ただし**これを「issue #56 の柱46本の
   事故そのもの」と読んではいけない**——この 4 は両端の `fOffset` を人手で同じ値へ揃えて
   作った状況で、事故のほうは**バウンドが階を跨いでいて絶対Zは本来一致しない**。
   なぜ一致してしまうのかは下記「階を跨ぐバウンドは `fBound` で書き分ける」を読むこと。
5. **`ResetObject` が呼ばれる前提では、パスの絶対Zは意味を持たない。** わざと大きく
   外れた絶対Z（`0→1`）のパスで新しいオブジェクトを作り、3 と同じバウンド（572/3531）を
   掛けて `ResetObject` すると、結果は 3 と寸分違わず一致した（挿入点 `(0,0,3531)`、
   パス `(0,0,0)→(0,0,-2959)`）。**つまりバウンドを両端とも設定して `ResetObject` を
   呼ぶなら、パスに正しい絶対Zを持たせる意味は無い**（最終的な実体はバウンドだけで
   決まり、パスの元の値は上書きされる）。

**実機で 3 周かけて切り分けた結果**（旧知見。鉛直材の場合。上記の仕組みが分かった今は、
「バウンドの差が高さを決める」という当時の解釈より、**上記 1〜5 の機構で説明するほうが
正確**だが、実測の値自体は生きている）:

| 鉛直パス | バウンドの差 | 結果 |
| --- | --- | --- |
| 1 点 | 0 | スパン/長さ/高さ 0。オブジェクトはレイヤ原点に置かれ、offset まで VW に上書きされる |
| 1 点 | 部材高さ | OIP の値は指定どおりになるが、**長さ 0 で描かれない** |
| **2 点** | 部材高さ | **正しく描かれる** |

**実務上の指針**（構造材のように両端のストーリバウンドを必ず設定して `ResetObject` まで
呼ぶ PIO の場合）:

- **パスとバウンドの両方に絶対Zを持たせる現行の二重指定は不要**（上記 5）。パスは
  「潰れない・退化しない2点」であれば足り、値そのものはバウンド解決後に上書きされる。
  ただし `ResetObject` を呼ばない・呼べない経路（バウンドを設定しないまま使う、
  他の PIO で `ResetObject` を省く等）ではパスの絶対Zがそのまま実体になる（上記 1）ので、
  **「バウンドを必ず設定して `ResetObject` する」経路を外れない設計であることが前提**。
- **上下端のバウンドが同じ絶対Zへ解決されないことを、書き込み側で保証する**（上記 4）。
  `SetObjectStoryBound` の戻り値が `true` でも、`GetObjectBoundElevation` で両端の
  解決結果を読み比べ、一致していたら手前で弾く・別の基準へ振り替えるなどの対処が要る
  ——**戻り値やレコードの読み戻しだけでは潰れを検知できない**（record は書いたとおりに
  読み戻る。実際に潰れるかは解決結果次第）。
- **`SetCustomObjectPath` で差し替えるときは、挿入点を読んでから相対座標を計算する**
  （上記 2）。挿入点は `GetObjectModelPos`（VWFC）で読める。

**検証範囲の限界**: 上記はすべて**鉛直材**（X=Y=0 の2点。`eStoryObjectBound_LayerElevation`、
自階基準）で確認したもの。水平材（両端が異なるX/Yを持ち、Zの差はバウンドoffsetの差だけで
表す設計。`draw/Member.cpp`）や、`eStoryObjectBound_Story`（他階基準）・
`fLayerLevelType`（横架材天端・軒高等の名前付きレベル）を使う場合に同じ機構が働くかは、
本調査では確認していない（同じ `ResetObject` 起点の再構築である可能性は高いが、未確認）。

**ヘッダ／同梱実装ソースからの補足**（VW 2026 SDK / mac。上記の実機確認とは独立に、
`sdk-grep` で確認できる事実）:

- `CreateCustomObjectPath` には `CreateCustomObjectPathNoOffset` という別の入口がある
  （`ISDK.h`）。名前どおり、既定の入口（上記 1 の「挿入点への変換」）を飛ばす入口と読める。
- VWFC のラッパー（`VWParametricObj` のパス付きコンストラクタ・`GetObjectPath`・
  `SetObjectPath`）は `GS_CreateCustomObjectPath` 等の `extern "C"` callback を素通しで
  呼ぶだけで、座標変換のコードは SDK に同梱されていない——上記 1〜5 の機構は VW 本体側の
  クローズドな実装であり、これ以上の詳細（挿入点の決め方・再構築のアルゴリズムそのもの）
  はヘッダからは分からない。
- `SetObjectStoryBound` / `GetObjectStoryBound` が扱う `SStoryObjectData` はヘッダ上は
  受け渡し用のレコード（`fBound` / `fBoundStory` / `fLayerLevelType` / `fOffset`）にしか
  見えない。ジオメトリを再構築するのは `ResetObject` 側の実装（同じくクローズド）。
  バウンドを解決した絶対Zだけを読み取れる `GetObjectBoundElevation(hObject, id)` がある。

### 階を跨ぐバウンドは `fBound` で書き分ける（`fBoundStory` だけでは跨がない）

**【ヘッダ根拠】**（VW 2026 SDK。`ISDK.h` の `SStoryObjectData` の宣言に付いたコメント。
**実機未確認**——確かめるプローブは
[issue #56](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/56) で走らせる）:

```cpp
struct SStoryObjectData
{
    EStoryObjectBound   fBound;
    Sint8               fBoundStory;    // used with fBound = eStoryObjectBound_Story
                                        //  if fBoundStory == 0 then it is this story
                                        //  if fBoundStory == 1 then it is the story above
                                        //  if fBoundStory == -1 then it is the story below
    TXString            fLayerLevelType;
    double              fOffset;
};
```

**`fBoundStory` が効くのは `fBound == eStoryObjectBound_Story` のときだけ**とコメントは
言っている。`eStoryObjectBound_LayerElevation`（レイヤ高さ基準）のまま `fBoundStory = 1` を
書いても**階は跨がず、自階の同名レベルへ解決される**と読める。

これが効いてくるのは、**同じ名前のレベル種別が全部の階に居る**——ストーリレイヤ
テンプレート（`CreateStoryLayerTemplate` の `elevationOffset`＝**階内の相対Z**）から
階を作るので、ふつうはそうなる——モデルで、**上下端のレベルの階内相対Zが等しい**ときである。
跨げていないぶん上端も下端も自階の同じ高さへ落ち、差が 0 になってパスが潰れる。
`Findings` の上記 4 が「絶対Zが一致すると潰れる」と言っているのはその先の話で、
**一致する理由がここにある**（という仮説。実機で確認中）。

ストーリ周りで使える口（`ISDK.h`。**プログラムから階を組める**）:

| 呼び出し | 何をするか |
| --- | --- |
| `CreateLayerLevelType(name)` | レベル種別（FL・横架材天端…）を足す |
| `CreateStoryLayerTemplate(name, scale, levelType, elevationOffset, wallHeight, index)` | 階を作るときの雛形。**`elevationOffset` が階内の相対Z** |
| `CreateStory(name, suffix)` / `GetNumStories()` | 階を作る。**ハンドルは返らない**（`GetStoryAt` に当たる口が無い） |
| `GetStoryOfLayer(layer)` / `GetLayerForStory(story, levelType)` | 階とレイヤの行き来。**階のハンドルはレイヤ経由でしか取れない** |
| `GetStoryElevation(story)` / `SetStoryElevation(story, z)` | 階の絶対Z |
| `GetStoryAbove(story)` / `GetStoryBelow(story)` | 上下の階 |
| `GetStoryBoundChoiceStrings(story, topBound, strings)` | **OIP のポップアップに出る選択肢**の一覧 |
| `GetStoryBoundDataFromChoiceString(string, data)` / `GetChoiceStringFromStoryBoundData(data, string)` | 選択肢文字列 ⇄ `SStoryObjectData`。**VW 自身の書き方を読み出せる** |
| `GetStoryObjectDataBoundHeight(data, hContainer)` | **オブジェクトを作らずに**バウンド指定の解決先の絶対Zを得る |

最後の 2 つは調査の道具として強い。**「上階のこのレベル」を正しく書く方法が分からないときは、
`GetStoryBoundChoiceStrings` で VW が出す選択肢を全部もらい、
`GetStoryBoundDataFromChoiceString` で構造体へ戻して中身を見ればよい**——推測でフィールドを
埋めるより確実で、`GetStoryObjectDataBoundHeight` を使えば解決先だけを先に検算できる。

## パラメータ名は実機の PIO 登録から採る

VectorScript のエクスポートから推測した名前（`pitch` / `label` / 先頭大文字の
`StructuralUse`）では **setter が黙って無視された**。書いたら**読み戻して確かめる**
（実装例: ホームズ君プラグインの `draw/DrawUtil` にある `ResolveParamName` /
`SetParamRealChecked`——名前を解決してから書き、読み戻して一致を確かめるラッパー）。

## パラメータの既定値は「文書」に記録される

自作 PIO のパラメータ既定値（`SParametricParamDef` に書いた値）は、**その PIO を初めて
使ったときに文書へ焼き込まれる**。あとからコードの既定値を変えても、**その PIO を一度でも
使った文書では古い既定のまま**で、新しい文書でしか新しい既定は出てこない。

実機での症状: 記号の離れの既定を 4 → 200 に変えたのに、同じ文書で取り込み直しても
ログはずっと `記号の離れ 4.0mm` のままで、記号が横架材の下へ潜って見えなかった。
コードを読む限り新しい値が入るはずなので、原因を PIO の外（レイヤ・重ね順）に探して
遠回りした。

**見た目に効く値は、既定値に頼らず書き手から毎回明示的に書く。** 生成側が値を持つ経路を
用意しておけば、既定値の食い違いに悩まされない（実装例: ホームズ君プラグインの
`draw/ShearWall` の `PlaceOne` は、解析結果から来ない見た目の値も含めて全パラメータを
毎回書く）。

## 生成時に「オブジェクトの設定」ダイアログが出る

`CreateCustomObject` は、その名前の PIO が文書に未定義なら定義を作り、その `prefWhen` の
**既定が `kCustomObjectPrefAlways`**。そのため**最初の 1 個だけ**ダイアログが出て処理が
止まる。PIO 側の `OnInitXProperties` で `kObjXPropShowPrefDialogWhen` を宣言しても
**定義が作られる過程で走るので 1 回目に間に合わない**。描き始める前に
`DefineCustomObject(name, kCustomObjectPrefNever)` を 1 度呼んで先に定義しておく。

## プラグインスタイル

- **当てただけでは描画属性が流れない。** `SetPluginObjectStyle` は関連付けまでしか
  行わないので、対象を全部置いてから **`UpdateStyledObjects` を 1 回**呼ぶ。
  スタイルを当てない PIO（データタグ・グラフィック凡例）にはそもそも要らない。
- **スタイル名 → RefNumber を名前で引く呼び出しは無い。** `GetNamedObject` ＋
  `GetObjectInternalIndex` で引く。

## プロファイル（断面）グループは空でないことを確かめる

空の断面は「オブジェクトはあるのに描かれない」を招く。生成後に読み戻して数え、
異常なら診断に出す。

## ポップアップの値は表示文字ではなくキー

ポップアップ（種別 8）のパラメータ値は英語または数値文字列のキーで、表示だけが
ローカライズされる。数値キーは**その項目の並び順（0 始まりの索引）**で、`<自動>` のような
先頭項目も 1 つ数える。

**構造材ツール（`StructuralMember`）の「構造用途」の全項目**（実機で「構造材設定」の
ドロップダウンを開いて確認。値は上から 0, 1, 2, …）:

| 値 | 項目 | 値 | 項目 | 値 | 項目 |
| --- | --- | --- | --- | --- | --- |
| `0` | `<自動>` | `6` | コレクタ | `12` | 筋かい |
| `1` | 梁 | `7` | 弦材 | `13` | 棟木 |
| `2` | 桁 | `8` | 垂木 | `14` | 隅木 |
| `3` | 根太 | `9` | 母屋梁 | `15` | 頭つなぎ |
| `4` | 柱 | `10` | 角材 | `16` | まぐさ |
| `5` | 小屋束 | `11` | 胴差し | `17` | その他 |

そのほか構造材ツールのポップアップ:

| パラメータ | キー | 意味 |
| --- | --- | --- |
| `AxisAlign`（断面基準点） | `1` / `4` / `7` | 天端中央 / 中央 / 中下。3×3 グリッドを 0 始まり・行優先で数えたもの（天端中央と中央は実機確認済み、**中下 `7` はその並びからの推定【推定】**） |
| `MemberType`（部材種別） | `2` | 構造材（種別の違いは構造用途の方に出る） |
| `StartCondition` / `EndCondition` | `3` | 直切り |

## 構造材ツールは軒の出・差し込みを持たない

軸組ツール（`FramingMember`）は挿入点＝支持点から軒側へ 差し込み＋軒の出 だけ材を伸ばすが、
構造材ツール（`StructuralMember`）は**パスがそのまま材の範囲**。垂木などを構造材ツールで
描くなら、パスの始端を支持点ではなく**軒先**にし、その位置・高さ・バウンド offset を
自分で計算する必要がある。

## 構造材同士の「自動結合」を作る API は無い【ヘッダ根拠】

構造材ツールには **自動結合（Auto Join Members）** モードがあり、この状態で置いた構造材同士は
**関連付け（association）**を持って、片方を動かすともう片方が長さを変えて追随する
（[VW ヘルプ](https://app-help.vectorworks.net/2023/eng/VW2023_Guide/Structural/Creating_structural_members.htm)）。
これを SDK から作る／読む口があるかを、VW 2026 SDK（mac）の `SDKLib/Include` 全体
（ヘッダ 456 本 ＋ VectorScript の宣言集 `vs.py`）の全数検索で確かめた。**専用の API は
1 つも無い。**

**構造材について SDK が持っているもの（`Structural` を含む識別子はこれで全部）**:

| 場所 | 識別子 | 中身 |
| --- | --- | --- |
| `Kernel/API/MiniCadHookIntf.h:1798-1799` | `kInternalID_StructuralMember = 537` / `kInternalID_StructuralComponent = 538` | PIO の内部 ID |
| `Kernel/Core/FolderSpecifiers.h:92` | `kDefaultStructuralShapesFolder = 142` | 断面形状の既定フォルダ |
| `Kernel/Core/FolderSpecifiers.h:326` | `kObjectStylesStructuralMemberFolder = 362` | オブジェクトスタイルのフォルダ |
| `Kernel/API/ObjectVariables.h:901` | `ovIsStructural = 702` | 「構造用」印の Boolean。結合とは無関係 |
| `vs.py:41643` / `vs.py:41657` | `SM_FromShape(hObj)` / `SM_Preferences()` | **VectorScript/Python のみ**。図形から構造材を作る／設定ダイアログを出す。どちらも結合に触れない |

**「結合」と名の付く API は全部よそのもの**:

| API | 対象 |
| --- | --- |
| `ISDK::JoinWalls`（`ISDK.h:1789`）と `kTWallJoin` / `kLWallJoin` / `kXWallJoin` / `kAutoWallJoin` / `kAutoLWallJoin`（`Kernel/API/MiniCadCallBacks.h:151-155`） | **壁だけ**（[Walls](Walls.md)） |
| `Get/SetComponentAlwaysAutoJoinInCappedJoinMode`（`ISDK.h:2777-2778`）・`varWallAutoJoin`（`ProgramVariables.h:48`） | 壁の構成要素 |
| `IPoly2DMath::JoinPolylines` / `JoinSinglePolyline` | 2D ポリライン |
| `IAssemblyUnitObject::OnJoinAssembly` | Braceworks のアセンブリ（建具・トラス側。構造材とは別系統） |

**汎用の関連付け API は「読む・消す」しか無い**:

| 用途 | ISDK | VectorScript/Python |
| --- | --- | --- |
| 数を数える | `GetNumAssociations(h)`（`ISDK.h:2323`） | `GetNumAssociations` |
| 1 件読む | `GetAssociation(h, index, associationKind, value)`（`ISDK.h:2324`） | `GetAssociation` |
| 消す | `DeleteAssociations(h, associationKind)`（`ISDK.h:2408`） | `RemoveAssociation` |
| **足す** | **無い** | `AddAssociation(owner, kind, target)` |

- ISDK の関連付け 3 メソッドには**コメントが 1 行も付いておらず**、しかも
  **`associationKind` の定数が SDK ヘッダのどこにも定義されていない**（`kAssociation*` の
  ヒットは文書ノード種別の `kAssociationNode = 124` だけ。`Kernel/API/Objs.TDType.h:190`）。
  読めても整数の意味は当てものになる。VW 開発者 wiki 側には `kOnDeleteDelete = 4` /
  `kOnDeleteReset = 5`（所有側を消したとき相手を消す／リセットする）しか出ておらず、
  **これは寿命の連動であって構造材の自動結合とは別物**の可能性が高い。
- `Interfaces/Base/ExtendedProperties.h:70-71` に `kKludgeAddAssociation = 19` /
  `kKludgeRemoveAssociation = 20` があるが、"DO NOT USE THOSE SELECTORS !!!" と明記された
  private API で、渡す `fData` の構造体すら公開されていない。**逃げ道にならない。**
- **VectorScript を SDK から流す道はある**（`IVectorScriptEngine::ExecuteScript(const TXString&)`
  ／ `IPythonScriptEngine::ExecuteScript`。`Interfaces/VectorWorks/Scripting/`）。
  したがって `AddAssociation` を呼ぶこと自体は SDK 側から可能だが、**それで構造材ツールの
  自動結合が作れるとは限らない**（`AddAssociation` は汎用の関連付けで、自動結合と同じ
  仕組みかどうかがそもそも未確認）。

現時点の結論:

- **作成**——構造材同士の自動結合を作る公開 API は SDK にも VectorScript にも無い。
  取り合いの**見た目**だけが要るなら、構造材はパスがそのまま材の範囲なので（上記）、
  呼び出し側でパスを詰めて突き付ける方が確実。ただしそれは関連付けではないので、
  後から VW 側で片方を動かしても追随しない。
- **読み取り**——構造材専用の口は無い。汎用の `GetNumAssociations` / `GetAssociation` が
  自動結合を返すかどうかは**実機で確かめる**しかない（下記）。

### 実機で確かめる手順（未実施）

1. VW で構造材を 2 本、**自動結合モードで**繋いで置く。
2. 両方のハンドルに `GetNumAssociations` を呼ぶ。0 なら、自動結合はこの口からは読めない。
3. 0 でなければ `GetAssociation` を index 全部について回し、返る
   `(handle, associationKind, value)` を全部ログに出す。返ったハンドルが
   もう 1 本の構造材かを `GetObjectTypeN` と PIO 名で確かめる。
4. **同じ 2 本を結合せずに置いた場合と差分を取る**（[Investigation Techniques](Investigation%20Techniques.md)）。
   差が出た `associationKind` の値が自動結合の印。
5. 併せて構造材の PIO レコードのフィールドを全数ダンプし、結合相手を指す欄が無いかを見る。

## 自作 PIO を足すときの 3 点

CI が全て緑でも次の 3 つは通ってしまい、実機で初めて出る。**新しい PIO を足すときは
最初から入れる**（実装例: ホームズ君プラグインの `Extensions/ExtColumnMark`）。

1. 上記の**設定ダイアログ抑止**（`DefineCustomObject(..., kCustomObjectPrefNever)`）。
2. **PIO のジオメトリは PIO 自身のローカル座標で持たれる。** 走査して見つけた対象の
   ワールド座標へそのまま描くと、PIO を動かした量だけ絵がずれ、リセットしても同じ相対位置に
   描き直すので直らない。`GetObjectToWorldTransform` ＋ `InversePointTransform` でローカルへ
   落としてから描く（回転も戻るので PIO を回しても絵は対象の上に残る）。
3. **リセットの契機は自分で宣言する。** 既定では何も設定されておらず、印刷でも再オープンでも
   描き直されない。`ResetOnMove` / `ResetOnRotate` と `kObjXPropResetBeforeExport`
   （印刷・書き出しの直前）を立てる。

**【限界】他の図形が変わったときに再計算する仕組みは VW に無い。** `kObjXProp*` を全数
確認したが、リセット系はすべて PIO 自身に関するもの（自分のレコード・自分のパス／
プロファイル・ビューポート倍率・書類単位・書き出し前）だけで、association にも追加の公開
API が無い。したがって参照先の図形を動かした瞬間には PIO が追随しない。実用上は
`kObjXPropResetBeforeExport` が効く——**図面として外へ出る瞬間には必ず実物と一致する**。

なお **PIO を同梱する代償は小さい**ことを実機で確認した。プラグインを外して過去の図面を
開いても、**PIO が描いたジオメトリは保存されていて表示できる**（更新ができないだけ）。

## パラメータ変更を PIO へ伝える口は無い

`kParameterChangedReset`（`ObjectStateData_ParamChanged`）は **PIO 側が受け取る**
メッセージ（`IObjUpdateSupport::OnState`）で、外からこれを送る API は `ISDK` に無い。
外からパラメータを書いた後に絵を変えたいなら `ResetObject` を呼ぶ（PIO の欄は
作り直しのときに読まれる。[Investigation Techniques](Investigation%20Techniques.md)）。
