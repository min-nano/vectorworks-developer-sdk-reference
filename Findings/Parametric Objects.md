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

## 高さはストーリバウンドが支配する（パスとバウンドの両方が要る）

実機で 3 周かけて切り分けた結果（鉛直材の場合）:

| 鉛直パス | バウンドの差 | 結果 |
| --- | --- | --- |
| 1 点 | 0 | スパン/長さ/高さ 0。オブジェクトはレイヤ原点に置かれ、offset まで VW に上書きされる |
| 1 点 | 部材高さ | OIP の値は指定どおりになるが、**長さ 0 で描かれない** |
| **2 点** | 部材高さ | **正しく描かれる** |

つまり**バウンドの差が高さを決め**、鉛直パスはその高さで実体を作るために要る。上端 offset を
下端と同値にする（＝差 0）と高さ 0 になるので、**「差＝部材高さ」**にする。
逆に水平材のパスに傾斜を持たせてはいけない（バウンドの高さ差と二重に効く）。

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
