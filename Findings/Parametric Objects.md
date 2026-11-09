# プラグインオブジェクト（PIO）

構造材ツール（`StructuralMember`）・通り芯（`GridAxis`）・データタグ・グラフィック凡例など
VW 標準のツールは PIO として実装されており、SDK から生成するときに共通の落とし穴がある。
自作 PIO を足すときの作法も末尾にまとめる。断りが無ければ VW 2026 / mac・新規の空図面での
実機確認。測った環境・プローブ・実測の表とログ・切り分けの経緯は
[計測記録](../Metrics/Parametric%20Objects.md)。

## パスの型を間違えると「生成できるのに何も描かれない」

- **水平材は 2D ポリライン**（`VWPolygon2DObj`）を渡す。2 頂点の 3D ポリラインに絶対 Z を
  持たせると、構造材は生成されるのに**長さ 0 で何も出ない**（OIP は「スパン 0 / 長さ 0」）。
  構造材では 3D ポリゴンのパスはバウンドも無視される（下記「構造材のパスは **2D で渡す**」）。
- **鉛直材（柱・小屋束）は `CreateNurbsCurve` ＋ `Add3DVertex`** で下端 → 上端の 2 点。
  `Add3DVertex` が VS の `AddVertex3D` に当たる（`Insert3DVertex` は別物で頂点が増えない）。
  `VWNURBSCurve` は評価専用で制御点から構築できない。

## `VWPolygon2DObj` は既定で「開いた」折れ線

`SetClosed(true)` を呼ばないと**最後の頂点から最初の頂点へ戻る辺が描かれない**（直角三角形の
斜辺だけが出ない形で現れる）。閉じ／開きは呼び出しごとに明示する。

## 高さ・実体を最終的に決めるのは「解決済みストーリバウンドから `ResetObject` が作り直すパス」

[issue #56](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/56) で実機確認
（鉛直材。水平材・斜め材は #73 / #75 で確認。下記）。取り込みで構造材の柱 46 本が長さ 0 になった
事故（[あちらの PR #113](https://github.com/min-nano/vectorworks-plugin-import-ifc-homeskz/pull/113)）
の調査から分かった機構。46 本の原因そのものは #61 で確定した（下記「`ResetObject` がバウンドから
作り直すのは…」）（[実測](../Metrics/Parametric%20Objects.md#高さ実体を最終的に決めるのは解決済みストーリバウンドから-resetobject-が作り直すパス)）。

1. **`CreateCustomObjectPath` は渡した世界座標の最初の点を挿入点にし、パスは挿入点からの
   相対で持ち直す**（`(0,0,572)→(0,0,3531)` → 挿入点 `(0,0,572)`・パス `(0,0,0)→(0,0,2959)`）。
2. **`SetCustomObjectPath` は変換を一切しない。** 挿入点は変わらないので、世界座標のつもりで
   渡すと挿入点のぶんだけずれる（`572→3531` を渡して `1144→4103`）。**Create と Set は非対称**で、
   Set には「今の挿入点からの相対」を渡す（挿入点は `GetObjectModelPos`）。
3. **`ResetObject` は解決済みのバウンドからパスを作り直す**（バウンドは受け渡し用データでは
   ない）。挿入点・パスの値はバウンドで上書きされる。
4. **上下端の解決済み絶対 Z が一致するとパスは正確に 0 長へ潰れる。** 代表は両端を
   `eStoryObjectBound_LayerElevation` で書いたとき。**バウンドを 1 本も持たずに `ResetObject`
   しても潰れる**。
5. **バウンドを両端とも書いて `ResetObject` するなら、パスの絶対 Z は意味を持たない**
   （`0→1` で作っても結果は同じ）。**ただしパスが「長さ 0」か「長さ 1e-7 以上」のときだけ**
   （#61。下記）。

**実務上の指針**:

- **パスとバウンドの二重指定は不要。** パスは「厳密に 0 長」か「長さ 1e-7 以上」であればよい。
  ただし `ResetObject` を呼ばない経路ではパスの絶対 Z がそのまま実体になる。
- **`doRegen=false` で作るなら、パスの両端の Z を等しくする**（下記「`doRegen=false` は速い…」）。
- **上下端が同じ絶対 Z へ解決されないことを書き込み側で保証する。** `SetObjectStoryBound` の
  戻り値も読み戻しも潰れを示さないので、`GetObjectBoundElevation` で両端を読み比べる。
- **スタイルを当てるなら、その検査は作り直しを通した後に行う**——バウンドはスタイルのものへ
  置き換わる（「[スタイルを当てると書いたバウンドはスタイルのものへ置き換わる](#スタイルを当てると書いたバウンドはスタイルのものへ置き換わる)」）。
  置き換えを避けるなら `DialogStartElevationReference` だけを by-instance にする
  （「[バウンドだけ by-instance のスタイルは作れる](#バウンドだけ-by-instance-のスタイルは作れる握っているのは-dialogstartelevationreference-1-件)」）。
- **`SetCustomObjectPath` で差し替えたパスは以後作り直されず、バウンドのほうが書き換わる**
  （下記）。差し替えるなら渡す形が最終形であること。

**検証範囲**: 水平材・斜め材でも同じ機構が働くが、**作り直しが触るのは Z の差だけ**で、水平成分は
ビット一致で残る（#73 / #75）。**バウンドの差がそのまま高さ＝長さになるのは鉛直材だけ**。

**ヘッダ／同梱ソースからの補足**【ヘッダ根拠】: 入口には別に `CreateCustomObjectPathNoOffset`
がある。VWFC のラッパー（`VWParametricObj` のパス付きコンストラクタ・`GetObjectPath`・
`SetObjectPath`）は `GS_CreateCustomObjectPath` 等を素通しで呼ぶだけで、変換・再構築は VW 本体の
クローズドな実装。`SStoryObjectData` は `fBound` / `fBoundStory` / `fLayerLevelType` / `fOffset`
の受け渡しレコードで、解決した絶対 Z は `GetObjectBoundElevation(hObject, id)` で読める。

### `ResetObject` がバウンドから作り直すのは「長さ 0」か「長さ 1e-7 以上」のパスだけ（その間に死角がある）

[issue #61](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/61)
（[実測](../Metrics/Parametric%20Objects.md#resetobject-がバウンドから作り直すのは長さ-0か長さ-1e-7-以上のパスだけその間に死角がある)）。

#### 結論: 判定しているのは**パスの長さ**で、`(0, 1e-7)` の帯では作り直されない

| 渡したパスの長さ | `ResetObject` は | 実体は |
| --- | --- | --- |
| **厳密に 0**（両端がビット一致） | **作り直す** | バウンドどおりに出る |
| **0 より大きく 1e-7 未満** | **作り直さない** | **0 長のまま（図に出ない）** |
| **1e-7 以上** | **作り直す** | バウンドどおりに出る |

- **閾値は 1e-7（図面の単位）で絶対値。** 座標の大きさに依らない。
- **見ているのは Z の差ではなく 3 次元の長さ**（どの成分も 1e-7 未満でも長さが超えれば作り直す）。
- **作り直されなくても挿入点は ID 0 の解決 Z へ動く**（「その場に潰れた線」になる）。
- **温存された値は `L` → `L + H·L²`（`H` は作り直したときの長さ）になる。** ビット比較で
  「触っていない」と判定しない。非鉛直でも同じ式（`L` は 3 次元長）。

#### これが「柱 46 本が長さ 0」の原因だった

事故の取り込みが渡したパスは長さ 2959 で、**作り直しに掛けると 1 ULP（`4.54747e-13`）が残る
長さだった**——帯に落ちて作り直されなかった。46 本の内訳は柱の高さごとの当たり外れ
（実測したのは 2959 の分だけ）。

> **【訂正】帰属は 2 回訂正した**（[issue #67](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/67) で決着）。
> 「呼び出し側の算術の丸め」も「mm ↔ インチの往復」（[#68](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/68)）も
> **誤り**——呼び出し側は厳密な値を渡しており、**残差を作っているのは VW の作り直し**である
> （[経緯](../Metrics/Parametric%20Objects.md#これが柱-46-本が長さ-0の原因だった)）。

**実務上の指針**:

- **両端の Z は同じ値（同じ変数）で渡し、厳密に退化させる。** `f(z) − f(z)` は厳密に 0 なので、
  残差の出どころが何であれ避けられる。別々の式で計算した結果を突き合わせない。
- **検算は「作り直しの後」に読む。** `GetCustomObjectPath` → `NurbsGetPt3D` で長さを読み、
  0 でも 1e-7 以上でもなければ危険。生成直後に読んでも危険は見えない。鉛直材なら読み戻した長さを
  `round(m) × ULP(L)` とビット比較すれば「渡した長さの取りこぼし」かが分かる。
- 潰れた個体を `SetCustomObjectPath` で直すとバウンドが書き換わる（下記）。作るときに退化させる。

#### その残差はどこで生まれたのか——**作り直しが「渡した長さの 1〜2 ULP」を残す**

[issue #67](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/67)
（[実測](../Metrics/Parametric%20Objects.md#その残差はどこで生まれたのか作り直しが渡した長さの-12-ulpを残す)）。

1. **生成の段では変換されない。** `CreateCustomObjectPath`（`NoOffset` も）は渡した世界座標を
   ビット一致で保つ（65 ケース全件）。
2. **残差は作り直し（regen）で生まれる。** バウンド無しの `ResetObject` で 0 長へ潰すとき、
   長さによっては L の 1〜2 ULP が残る（符号は L と同じ）。その大きさは `(0, 1e-7)` の帯に入る。
3. **決めているのは L だけ**で端点の絶対 Z ではない。**決定的**（順番・レイヤ・実行に依らない）。
4. **mm ↔ インチの往復（`v / 25.4 * 25.4 != v`）では予測できない。**
5. **L = 0 に退化させて渡せば必ず 0 のまま。**

- 測った作り直しは `ResetObject`。事故は `SetPluginObjectStyle` を挟んだ後だったが、同じ長さ
  2959 からビット一致で同じ残差が出た。
- 【ヘッダ根拠】`WorldCoord` は `double` の mm 固定（`kWorldCoordsPerMM = 1` /
  `kWorldCoordsPerInch = 25.4`）。`kNearlyEqualEpsilonForDoubles = 1e-13` /
  `kWorldCoordBumpEpsilon = 1e-11` は 1e-7 の閾値とは別物。

#### どの長さ L が残差を残すのか——**大きさは式で言える。出るかどうかは言えない**

[issue #71](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/71)（鉛直 506 ケース。
[実測](../Metrics/Parametric%20Objects.md#どの長さ-l-が残差を残すのか大きさは式で言える出るかどうかは言えない)）。

- **大きさは `round(m) × ULP(L)`**（`m` は L の仮数。m < 1.5 なら 1 ULP、m > 1.5 なら 2 ULP）。
  残差の出た 116 件すべてがビット一致。**`|L| < 3e8 mm` なら残差は必ず 1e-7 未満**——
  「残差が出た」と「作り直しが止まる」は現実の図面では同じこと。
- **出るかどうかは仮数だけで決まる**（`L` と `2L` と `L/2` は同じ結果）。単位を変えて逃げられない。
- **出る L は飛び飛びで密度 14〜23%**。整数 mm 特有でも、切りのいい値が安全でもない
  （例: 2959・3428 は出る、3000・4000 は出ない。一覧は計測記録）。
- **当てはめ（単位往復・定数倍・等分加算・数値積分）は全滅。危ない L の一覧は式で作れない。**
  検出は「作り直しの後に長さを読んで `round(m) × ULP(L)` と比べる」、避け方は L = 0 に退化。
- **以上は鉛直（`dx = dy = 0`）の話。** 水平成分があると別の規則（下記）。

#### 鉛直でないパスでは、潰れるのは Z の差だけ——**水平成分は残り、残差の規則は別物になる**

[issue #73](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/73)（1119 ケース。
[実測](../Metrics/Parametric%20Objects.md#鉛直でないパスでは潰れるのは-z-の差だけ水平成分は残り残差の規則は別物になる)）。

- **作り直しが触るのは Z の差だけ。** `dx` / `dy` はビット一致で残り（1119 / 1119）、0 長へ潰すと
  斜め材は「水平材に倒れる」（`(dx, dy, 残差)`）。
- **残差は `k × ULP(dz)`（`k` は整数・`|k| ≤ 3`）。** `k` は鉛直の式から外れ、**符号が L と逆にも
  なる**（負は水平成分があるときだけ）。**3 次元長では決まらない**（同じ 3 次元長で食い違う）。
- **`dx = 0.001 mm` の水平成分でも結果が変わる。** 水平成分が入ると残差の出る確率は 56〜65% へ
  上がる。X と Y は完全に同じ効き方。始点の絶対座標には依らない。
- **実務**: 避け方は鉛直と同じ（両端に同じ Z）。**検出は「Z の差が厳密に 0 か」だけを見る**
  （`round(m)` の式は斜めでは使えない）。潰れた斜め材は水平成分があるので死角に落ちない（下記）。

#### 水平成分が 1e-7 以上ある部材は「0 長の死角」に落ちない——**事故は鉛直材に固有**

[issue #75](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/75)（72 ケース。
[実測](../Metrics/Parametric%20Objects.md#水平成分が-1e-7-以上ある部材は0-長の死角に落ちない事故は鉛直材に固有)）。

- **判定は「バウンド無しで潰した後の 3 次元長」で、閾値 1e-7。** 水平成分が 1e-7 以上なら必ず
  作り直される。**「柱 46 本」の事故は鉛直材でしか起きない。**
- **作り直された斜め材は「水平成分そのまま・Z の差だけバウンドどおり」**——傾きは保たれず、
  3 次元長は `√(dx² + dy² + span²)`。**斜め材の傾きと長さは渡すパスの水平成分が決める。**
- **水平材（`dz = 0`）も同じ機構**で、Z の差が span へ書き換わる（水平のままではいられない）。
- **作り直された長さも `span ± 1〜2 ULP`。** 作り直されたかは `==` ではなく、`|z1 − z0|` が死角
  （0 より大きく 1e-7 未満）に入っているかで見る。
- 温存されたパスは `z + H·L²`（`L` は 3 次元長）——判定も補正も 3 次元長を見ている。
- **取り込みの検算は `dx = dy = 0` の部材だけを疑えばよい。** 退化させる避け方は鉛直材では必須、
  斜め材・水平材では不要（害も無い）。

#### `SetCustomObjectPath` で差し替えたパスには、この判定が掛からない（バウンドの**両方**が書き換わる）

`LayerElevation`（[#61](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/61)）と
`_Story`（[#63](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/63)）の両方で確認
（[実測](../Metrics/Parametric%20Objects.md#setcustomobjectpath-で差し替えたパスにはこの判定が掛からないバウンドの両方が書き換わる)）。

- 一度 `ResetObject` を通したオブジェクトのパスを差し替えると、**以後の `ResetObject` は長さに
  関わらずパスを作り直さず、バウンドのレコードをパスへ合わせて書き換える**——ID 0 の解決 Z が
  始点、ID 1 が終点の絶対 Z に厳密に一致する。
- 書き換わるのは `fOffset` だけで、**`fBound` / `fBoundStory` / `fLayerLevelType` は変わらない**。
  ID 0 も対象（始点をずらせば挿入点も動く）。符号も階の境も越える。
- **差し替え経路ではパスが正で、バウンドが従う**（作る経路と逆）。部材は階に追従し続けるが、
  **その時点の階間隔が `fOffset` へ焼き込まれる**。
- **差し替えを対症療法に使わない**——図は直っても嘘の `fOffset` が残る（下記）。
- パスに触らずバウンドだけを書き換えて 2 度目の `ResetObject` を呼べば、新しい解決 Z で作り直される。

#### 差し替えで `fOffset` を書き換えられた部材は、階を動かすと長さが変わる（＝潰れた部材が生えてくる）

[issue #65](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/65)
（[実測](../Metrics/Parametric%20Objects.md#差し替えで-foffset-を書き換えられた部材は階を動かすと長さが変わる潰れた部材が生えてくる)）。

- **長さ ＝ 現在の階間隔 − 差し替え時点の階間隔 ＋ 差し替えたパスの長さ。** 0 長へ差し替えた
  部材は、階高を 100 上げると 100 の部材として生え、自階を上げると符号が反転する。
- **`SetStoryElevation` の後、`ResetObject` を呼ぶ前から変わっている**（引き金が
  `SetStoryElevation` か読み出しかは切り分けていない）。バウンドのレコードは二度と書き換わらず、
  動くのは解決 Z だけ。**可逆**（階を戻せば完全に元どおり）。
- **直し方**: バウンドを正しい値（`fOffset = 0` など）へ書き直して `ResetObject` に作り直させるか、
  部材ごと作り直す。

#### 2 つの経路を、最初の `ResetObject` より前に混ぜない

まだ一度も `ResetObject` を通していないオブジェクトのパスを差し替えてから `ResetObject` すると、
**長さが「解決済みの長さ − (作るときに渡した長さ − 差し替えた長さ)」になり、バウンドと実体が
ずれる**。**作る → バウンドを書く → `ResetObject` の間に差し替えを挟まない**
（[実測](../Metrics/Parametric%20Objects.md#2-つの経路を最初の-resetobject-より前に混ぜない)）。

### 階やレベルを指すバウンドは `eStoryObjectBound_Story` で書く（`LayerElevation` では跨がらない）

[issue #56](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/56)
（[実測](../Metrics/Parametric%20Objects.md#階やレベルを指すバウンドは-estoryobjectbound_story-で書くlayerelevation-では跨がらない)）。

> **【訂正】**（[issue #59](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/59)）:
> 当初「これが柱 46 本の原因」と書いたが、**事故のプラグインは `LayerElevation` で書く経路を
> 持っていない**。以下は「`LayerElevation` で書くとこうなる」という機構の記述である。

#### `EStoryObjectBound` の 3 つの値は「何を見るか」が違う

| `fBound` | 何に解決されるか | `fLayerLevelType` | `fBoundStory` |
| --- | --- | --- | --- |
| `eStoryObjectBound_LayerElevation` | **そのオブジェクトが乗っているレイヤの高さ** | **見ない** | **見ない** |
| `eStoryObjectBound_LayerWallHeight` | 同上（レイヤ設定の壁高） | **見ない** | **見ない** |
| `eStoryObjectBound_Story` | **`fBoundStory` で選んだ階の、`fLayerLevelType` のレベル** | 見る | 見る（0=自階 / 1=上階 / −1=下階） |

どの場合も `fOffset` は最後に足される（総当たりの表は
[計測記録](../Metrics/Parametric%20Objects.md#estoryobjectbound-の-3-つの値は何を見るかが違う)）。

#### 正しい書き方は VW 自身に聞ける

**`GetStoryBoundChoiceStrings` → `GetStoryBoundDataFromChoiceString` で正解の
`SStoryObjectData` を取り出し、`GetStoryObjectDataBoundHeight(data, hContainer)` でオブジェクトを
作らずに解決先を検算する。** 階やレベルを指す選択肢はすべて `fBound = eStoryObjectBound_Story`
で、`LayerElevation` の選択肢は「レイヤの高さ」1 つ（`fLayerLevelType` は空）だけ
（[選択肢と復号結果](../Metrics/Parametric%20Objects.md#正しい書き方は-vw-自身に聞ける)）。

#### 上下端とも `LayerElevation` で書くと 0 長へ潰れる（機構）

- 両端が同じ「レイヤの高さ」へ解決され、残るのは `fOffset` の差だけ——**offset が同じなら 0 長**
  （[実測](../Metrics/Parametric%20Objects.md#上下端とも-layerelevation-で書くと-0-長へ潰れる機構)）。
- `SetObjectStoryBound` は `LayerElevation` ＋ レベル種別も `true` で受け取り、書いたとおりに
  読み戻る。**気付けるのは `GetObjectBoundElevation`（または `GetStoryObjectDataBoundHeight`）で
  解決結果を読んだときだけ。** 書いた後は必ず解決結果を読む。
- 階内相対 Z が各階で揃ったモデルほど当たりやすいが、原因は相対 Z の一致ではない。
- **【推定】解決できないレベル種別を指すと「レイヤの高さ」へ落ちる**（`{Story, −1, FL}` が
  572。データ点 1 つ）。

### 同じオブジェクトに 2 本のバウンドを書く（ID 0 / 1。階だけが違っても独立に解決される）

[issue #59](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/59)
（[実測](../Metrics/Parametric%20Objects.md#同じオブジェクトに-2-本のバウンドを書くid-0--1階だけが違っても独立に解決される)）。

#### 結論: 消えない・混ざらない・書く順も効かない

- **`fBoundStory` だけが違う 2 本の `_Story` バウンドは両方保持され、独立に解決される**
  （`GetObjectStoryBoundsCount` = 2）。書く順も効かない。
- 完全に同一のレコードを 2 本書いても 2 件として保持される（解決 Z が同じなので 0 長に潰れる）。
- **「2 本書くと片方が消える」で長さ 0 は説明できない**——この経路は白。

#### バウンド ID は 0 と 1。`ID 0 がパスの始点`、`ID 1 が終点`

- **新規に作った構造材 PIO はバウンドを持たない**（`HasObjectStoryBounds=false`。VW が既定を
  作ることはない）。
- **`ResetObject` はパスの始点を ID 0、終点を ID 1 の解決 Z に置く。** 下から上へ描くなら ID 0 が
  下端（規則は「上端／下端」ではなく「ID 0 → 始点 / ID 1 → 終点」）。
- **`kPIOGenericStoryLevelBoundID`（−3）へは書けない**（`SetObjectStoryBound` が `false`）。
  構造材 PIO のバウンドは 0 と 1 で扱う。

#### `ResetObject` 後のパスが 0 長になるのは 2 通り（実測）

1. **上下端の解決済み絶対 Z が一致したとき。**
2. **バウンドが 1 本も無いまま `ResetObject` を呼んだとき**（`SetObjectStoryBound` が効いて
   いないだけでも 0 長になる）。

> **書いたら数えて読む。** ID が並んでいるかは `GetObjectStoryBoundsCount` /
> `GetObjectStoryBoundsAt`、上下で違うかは `GetObjectBoundElevation` で、`ResetObject` の前に弾く。

#### 構造材のパスは **2D で渡す**——3D ポリゴンで渡すとバウンドが黙って無視される

[issue #196](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/196)
（[実測](../Metrics/Parametric%20Objects.md#構造材のパスは-2d-で渡す3d-ポリゴンで渡すとバウンドが黙って無視される)）。

- **`Create3DPoly` ＋ `Add3DVertex` のパスで作った構造材はストーリバウンドを黙って無視する**——
  `SetObjectStoryBound` は `true`、`GetObjectBoundElevation` も正しいのに、挿入点はパスの Z のまま
  で断面（型 84）も作られない（外接は `±DBL_MAX`）。**件数も解決 Z も正常に見える**ので、気付けるのは
  幾何（`GetObjectCube` の高さ・子の型 84）だけ。
- [#187](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/187) で `#ZTBB…#` が
  `±1.79e308` になった本当の理由（[Data Tags](Data%20Tags.md)「傾斜材の両端の天端」）。
- **パスは `VWPolygon2DObj`（平面の 2 点）で渡し、高さはバウンドに与える。** 水平材も傾斜材も
  平面のパス ＋ 両端のバウンド（傾斜は両端の offset の差）で作る。

#### ストーリを触る API（`ISDK.h`）

| 呼び出し | 何をするか |
| --- | --- |
| `ForEachLayerN(std::function<void(MCObjectHandle)>)` | **レイヤ列挙はこれ。** `VWDocument::GetDrawingHeaderFristMember` ＋ `NextObject` では辿れない（実測） |
| `GetStoryOfLayer(layer)` / `GetLayerForStory(story, levelType)` | 階とレイヤの行き来。**階のハンドルはレイヤ経由でしか取れない**（`GetStoryAt` に当たる口が無い） |
| `GetStoryElevation(story)` / `SetStoryElevation(story, z)` | 階の絶対Z |
| `GetStoryLevelElevation(story, levelType)` | **階内の相対Z**。レイヤ高さを読む口が無い問題の回避にも使える |
| `GetStoryAbove(story)` / `GetStoryBelow(story)` / `GetNumStories()` | 階の並び |
| `GetStoryBoundChoiceStrings` / `GetStoryBoundDataFromChoiceString` / `GetChoiceStringFromStoryBoundData` | 選択肢文字列 ⇄ `SStoryObjectData`（上記） |
| `GetStoryObjectDataBoundHeight(data, hContainer)` | **オブジェクトを作らずに**解決先の絶対Zを得る |
| `AddStoryLevel(story, levelType, 階内相対Z, layerName)` / `AddStoryLevelFromTemplate` / `RemoveStoryLevel` / `SetStoryLevelElevation` / `ResetDefaultStoryLevels` | 階へレベル（＝レイヤ）を足す |
| `CreateLayerLevelType(name)` / `CreateStoryLevelTemplate(...)` / `CreateStoryLayerTemplate(...)` | レベル種別と雛形の登録 |
| `AssociateLayerWithStory(layer, story)` / `SetLayerLevelType(layer, levelType)` | **既にあるレイヤを階へ結び付ける / レベル種別を付け替える。** 結べるのは**その階で未使用のレベル種別を持つレイヤだけ**（素のレイヤ＝種別 `''` は `false`）。**仮の種別を与えて結び、`SetLayerLevelType(layer, "")` で外すと「階あり・レベルなし」になる**（実機確認済み。[Layers and Stories](Layers%20and%20Stories.md)「レベルを持たないレイヤを階に結ぶ」） |
| `HasObjectStoryBound(h, id)` / `GetObjectStoryBound` / `SetObjectStoryBound` / `DelObjectStoryBound(h, id)` / `DelObjectStoryBounds(h)` | オブジェクトのバウンドを ID 単位で読み書き・削除する |
| `GetObjectStoryBoundsCount(h)` / `GetObjectStoryBoundsAt(h, index)` | **実際に並んでいるバウンド ID を数えて引く。** 書けたかどうかはこれで確かめる（上記） |
| `GetObjectBoundElevation(h, id)` | その ID のバウンドを**解決した絶対Z**。潰れの検知はここ |

落とし穴が 3 つ（すべて実測）:

- **`CreateStory` はレイヤを 1 枚も作らない**（レベルの無い空の階。`ForEachLayerN` から見えない）。
  階のハンドルは `GetNamedObject(階の名前)` で引け、レベルは `CreateStoryLevelTemplate` ＋
  `AddStoryLevelFromTemplate` で生やす（`AddStoryLevel` では紐付かない。[Layers and Stories](Layers%20and%20Stories.md)「階（ストーリ）を SDK から作る」）。
- **`Story Layer Template`（`GetNumStoryLayerTemplates` / `CreateStoryLayerTemplate`）と
  `Story Level`（`GetNumStoryLevelTemplates` / `CreateStoryLevelTemplate` …）は別系統。**
  前者は `true` が返るだけでレイヤは生えない。
- **`GetLayerLevelTypeName` / `GetStoryLayerTemplateInfo` / `GetStoryLevelTemplateInfo` の添字は
  1 始まり**（0 始まりで回すと末尾を取りこぼす）。一覧は名前順で、`Create*Template` が返す
  `index` は挿入時点の整列位置に過ぎない。

## 線分 PIO の両端と長さ——`LineLength` を持つ PIO にだけ `Set/GetLinearObjectPos` が効く

[`Info/Parametric Object Types.md`](../Info/Parametric%20Object%20Types.md) は「線分 PIO
（`kParametricSubType_Linear`）は行列＋隠しパラメータ `LineLength`（`kFieldCoordDisp`）」と言うが、
**SDK には `LineLength` という綴りもサブタイプを実行時に問い合わせる API も無い**（`kLineLike = 10`
は登録時の資源 `PExt` の値）【ヘッダ根拠】。
[issue #181](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/181) で組み込み PIO
10 件を同じ手順に掛けて決着させた。

**結論**:

1. **`LineLength` は隠しではなく、ふつうのパラメータとして表に並ぶ**（欄型 `7`）。`GetParamReal` /
   `SetParamReal` で読み書きでき、`ResetObject` を挟めば絵もその長さになる。
2. **`VWParametricObj::Set/GetLinearObjectPos`（＝`ISDK::Set/GetEndPoints`）は `LineLength` を持つ
   PIO にだけ効く。** `Set` は行列の i 軸と `LineLength` を同時に書く。
3. **持たない PIO では `Get` が `(0, 0), (0, 0)`、`Set` は完全な no-op**（例外も戻り値も無い）。
   `(0, 0), (0, 0)` は「この PIO は線分ではない」の意味。
4. **`LineLength` の有無が、線分として登録されているかの唯一の判定手段**:

   ```cpp
   VWParametricObj pio(h);
   const bool isLinear = pio.GetParamIndex("LineLength") != (size_t) -1;
   ```

5. **名前に「Line」が入っていても線分とは限らない**（`Leader Line` / `Center Line Marker` /
   `Linear Material` は持たない）。**構造材も線分ではない**（長さはパスの両端で測る）。

### 実測（組み込み PIO 10 件。`LineLength` の有無で 3 対 7 にきれいに割れた）

- `LineLength` を持つのは `Break Line` / `Joist` / `Clothes Rod` の 3 件
  （[表](../Metrics/Parametric%20Objects.md#実測組み込み-pio-10-件linelength-の有無で-3-対-7-にきれいに割れた)）。
- **作成直後の終端は `LineLength` の既定値そのもの**——既定が `1`（1mm）の線分 PIO もあるので
  「作った直後は長さ 0」と思わない。
- **内部 ID は `MiniCadHookIntf.h` の `kInternalID_*` と一致する。** `GetObjectVariable(h, 1165
  /* ovParametricInternalID */, …)` で読み、種別をヘッダの定数と突き合わせられる。
- 型（`GetObjectTypeN`）は 10 件すべて 86 で、線分の区別には使えない。

### `Set…` が効く PIO では、行列の i 軸と `LineLength` が同時に書かれる

- **与えた方向がそのまま行列の i 軸になる。** `Set…` の効き目は `ResetObject` を待たずに読み戻せる。
  ただし**絵（外接）を測るなら `ResetObject` の後**。
- **`LineLength` を書いても行列は動かない。** 長さだけなら `LineLength`、向きも変えるなら
  `SetLinearObjectPos`。
- **`gSDK->SetEndPoints` と `SetLinearObjectPos` は同じもの**【ソース根拠】（VWFC は
  `GS_SetEndPoints` を素通し）。「例外が出ないのに効かない」はカーネルが線分でない PIO を黙って
  無視するため（[実測](../Metrics/Parametric%20Objects.md#set-が効く-pio-では行列の-i-軸と-linelength-が同時に書かれる)）。

### 両端の正体は「始端＝行列の offset」「終端＝offset ＋ `LineLength` × i 軸」

- **`SetLinearObjectPos(A, B)` は A を行列の offset へ書く——オブジェクトを動かす操作でもある。**
- `LineLength` を縮めても始端は動かず、終端だけが i 軸方向に詰まる
  （[実測](../Metrics/Parametric%20Objects.md#両端の正体は始端行列の-offset終端offset--linelength--i-軸)）。
- **取り込みで線分 PIO を置くなら「`CreateCustomObject` で始端と角度を与え、長さは `LineLength`
  で書く」で足りる**（位置がそのまま offset になる）。

### OIP でパラメータを編集しても、VW は行列も長さも触らない

- 利用者が OIP で 1 つ編集した前後で、行列・`LineLength`・両端・外接が 1 桁も変わらなかった
  （パラメータの指紋は変わり、幾何の指紋だけが一致。[実測](../Metrics/Parametric%20Objects.md#oip-でパラメータを編集してもvw-は行列も長さも触らない)）。
- **「OIP で編集したら絵がずれる」なら、疑うのは `Recalculate` が自分以外から読んでいるもの。**
  `Recalculate` の中から見えるものは下記（[issue #183](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/183)）。

## パラメータ名は実機の PIO 登録から採る

VectorScript のエクスポートから推測した名前（`pitch` / `label` / 先頭大文字の
`StructuralUse`）では **setter が黙って無視された**。書いたら**読み戻して確かめる**
（実装例: ホームズ君プラグインの `draw/DrawUtil` の `ResolveParamName` / `SetParamRealChecked`）。

### 名前の解決は「種別 × 文書」ごとに 1 度でよい——表はインスタンスに依らない

[issue #82](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/82)。
**`GetParamsCount` / `GetParamName(i)` / `GetParamLocalizedName(i)` の表は、同じ種別・同じ文書の
インスタンスなら常に同一**——値・ポップアップ・スタイル・`ResetObject` で変わらない。
名前の解決は種別ごとに 1 度でキャッシュしてよい（鍵は「種別 × 文書」）。

#### 何を変えても表は動かなかった（実測）

- 構造材のパラメータは **181 件**。値・ポップアップ・別文書・スタイルのどれでも件数・並び・名前・
  選択肢の数が一致（[比べたもの](../Metrics/Parametric%20Objects.md#何を変えても表は動かなかった実測)）。
- 【ソース根拠】表はパラメータレコードのフォーマット（種別ごとに 1 つ）から来る
  （`GetNameOfField` は 1 始まり）。`GetParamLocalizedName(i)` は `IParametricParamsProvider` →
  `GetLocalizedPluginParameter` → `GetParamName(i)` の順に落ち、引数は索引と種別だけ。

#### スタイルを当てても表は変わらない

- **スタイルの素性は、そのシンボル定義を `FirstMemberObj` で辿れば分かる**（その種別の PIO が
  1 つ入っている）。`SetStyle` / `SetPluginObjectStyle` は `IsPluginStyle` しか見ないので**別種別の
  スタイルでも当たってしまう**——「当てられた」を「その種別だった」と読み替えない。
- 当てただけではパラメータの値は流れない（値は `UpdateStyledObjects` で流れる）。
- **`CreatePluginStyle` は使えない**（下記「**`CreatePluginStyle` を呼んではいけない**」）。

#### ローカライズ名はアプリの資源から来る（文書ではない）

- `GetParamLocalizedName(i)` は `GetLocalizedPluginParameter` と 179 / 181 件一致・相違 0。
  左右するのは索引・種別・UI の言語だけ。
- **資源に無い 2 件（`MemberMergeInSections` / `CoverMergeInSections`）は空文字が返る。**
  ローカライズ名で引き当てる実装は空を一致と数えない。

#### キャッシュはいつ捨てるか

- **文書が変わったら捨てる**（古い文書は古いフォーマットを持ちうる）。**UI の言語が変わったら
  捨てる**（ローカライズ名を鍵にしているなら）。コマンド 1 回に閉じたキャッシュなら考えなくてよい。

#### コスト（VW 2026 / mac。181 件の構造材で実測）

- **ローカライズ名 1 件 0.435us、フルスキャン 0.079ms**——「描画時間の 51〜68% がここ」という
  見立て（#82）はこの経路では説明が付かない。
- **高いのは universal 名で引く呼び出し（`GetParamIndex` 4.236us。線形探索）。** 名前で読み書き
  する呼び出しは毎回これを払う——**キャッシュすべきは解決した索引**
  （[表](../Metrics/Parametric%20Objects.md#コストvw-2026--mac181-件の構造材で実測)）。

#### 構造材（`StructuralMember`）のパラメータ表（VW 2026 / mac・日本語 UI）

181 件の索引・universal 名・ローカライズ名・選択肢の数は
[計測記録の表](../Metrics/Parametric%20Objects.md#構造材structuralmemberのパラメータ表vw-2026--mac日本語-ui)。
主なもの: `MemberType`（2）・`ProfileShape`/`ProfileSeries`/`ProfileSize`/`ProfileSymbol`（3〜6）・
`MajorBreadth`/`MinorBreadth`/`MajorDepth`/`MinorDepth`（7〜10）・`StructuralUse`（11・18 択）・
`AxisAlign`（25）・`DialogStartElevationReference`（28）・`AttributesMode`/`AttributesMode3D`（179/180）。
索引 0 と 48〜152・154〜158・161〜168・171〜176 はローカライズ名が `__NNA_DO_NOT_CHANGE`
（中身は「[構造材の 2D / 3D 属性パラメータ（索引 48〜168）](#構造材の-2d--3d-属性パラメータ索引-48168面--パーツ--欄)」）。
`B` / `D` / `StartOffset` は universal 名として実在する。

## 構造材の 2D / 3D 属性パラメータ（索引 48〜168）——「面 × パーツ × 欄」

[issue #158](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/158)
（[実測](../Metrics/Parametric%20Objects.md#構造材の-2d--3d-属性パラメータ索引-48168面--パーツ--欄)）。

- **パラメータの型を返すのは `GetParamStyle`（→ `EFieldStyle`）だけ**（`GetParamType` は無い）。
- **ポップアップの選択肢は `PopupGetChoicesCount` では採れない**（下記）。

### 構造

- 2D 属性は **48〜152 の 105 件 ＝ 3 面 × 35 件**。面は接尾辞 `_Above`（48〜82）/ `_At`（83〜117）/
  `_Below`（118〜152）。universal 名は `<パーツ><欄>_<面>`。
- パーツは `Member`（11 欄）・`Cover`（11 欄）・`Centerline`（6 欄）・端部（7 欄。表示だけ
  `StartCapDisplay` / `EndCapDisplay` に割れ、残りは `Caps…`）。欄は `Display` / `Class` /
  `PenStyle` / `PenColor` / `LineStyle` / `LineWeight` / `FillStyle` / `FillColor` / `FillHatch` /
  `FillTile` / `FillGradient`（後ろ 3 つは資源の索引【推定】）。
- **センターマーク（153〜160）と 3D 属性（161〜168。`MemberClass_3D` / `MemberAttributes_3D` /
  `MemberTextureByClass_3D` / `MemberTexture_3D` と `Cover…` の 4 欄）は面を持たない。**
- **171〜176 の `B` / `B1` / `D` / `D1` は欄型 14 の読み取り専用表示欄**——寸法欄の鏡で、絵は
  動かない（#169 で確定）
  （[一覧](../Metrics/Parametric%20Objects.md#構造)）。

### 新規に作った 1 本の既定値

面で違うのは `PenStyle` / `LineStyle` / `LineWeight` / `FillStyle` の 4 欄だけ。`LineStyle` の負の
値はラインタイプの資源索引。**色は 16 bit で返る**（`65535,65535,65535`）。書くときは
`SetParamColor(索引, ColorRef)`（`CRGBColor(255, 0, 0).GetColorIndex()`）
（[既定値の表](../Metrics/Parametric%20Objects.md#新規に作った-1-本の既定値)）。

### 選択肢を採る経路——`PopupGetChoicesCount` は 0 を返す

**採れるのは `VWParametricObj::GetParamChoices`（キー）/ `GetParamLocalizedChoices`（表示）と
`VWRecordFormatObj::PopupGetChoicesCount(索引, useParametric=true)` ＋ `PopupGetChoice`。**
`VWParametricObj::PopupGetChoicesCount` と `useParametric=false` は常に 0——「選択肢が無い」ではなく
「その経路では見えない」（[実測](../Metrics/Parametric%20Objects.md#選択肢を採る経路popupgetchoicescount-は-0-を返す)）。

### 「線の属性」「面の属性」「使用する属性設定」の選択肢

| パラメータ | 選択肢（キー＝表示） |
| --- | --- |
| `…PenStyle_<面>`（線の属性。4 パーツとも） | 0＝なし / 1＝実線 / 2＝ラインタイプ / 3＝オブジェクト別 / **4＝クラス属性** |
| `…FillStyle_<面>`（面の属性。構造材・被覆のみ） | 0＝面なし / 1＝カラー / 2＝ハッチング / 3＝タイル / 4＝グラデーション / 5＝オブジェクト別 / **6＝クラス属性** / 7＝マテリアル属性 |
| `AttributesMode`（179）/ `AttributesMode3D`（180） | 0＝オブジェクト / 1＝線種 / 2＝クラス / 3＝マテリアル |
| `Member/CoverAttributes_3D`（162 / 166） | 0＝オブジェクト別 / **1＝クラス属性** |

**「クラススタイルにする値」は、線が 4・面が 6。**

### クラス属性は効く——`FillStyle`=6 / `PenStyle`=4 で描画がクラスのものになる

- **その子は指定したクラスに入り、by-class の旗も立つ**——絵を見なくても
  `GetFillColorByClass` / `GetPenColorByClass` と `GetObjectClass` で判定できる。
- **「オブジェクト別」（Fill=5 / Pen=3）は per-part の色欄を見ない**（PIO 自身の属性を使う【推定】）。
- 効くのは書いた面・書いたパーツの欄だけ
  （[実測](../Metrics/Parametric%20Objects.md#クラス属性は効くfillstyle6--penstyle4-で描画がクラスのものになる)）。

### `AttributesMode` は 4 択だが、`ResetObject` 後の描画には効かなかった

- 0〜3 を振っても描画は 1 か所も変わらない（書けて読み戻せる）。VW へ戻った後も同じ
  （[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)）。
- **属性の出どころを決めるのは per-part の `PenStyle` / `FillStyle`**。`AttributesMode` は設定
  ダイアログで出す欄を握る値と見られる【推定】。**クラススタイルにしたいときに触る必要は無い。**

### 平面図で使われるのは `_Below`——3 面のどれが描かれるかは切断面との関係で決まる

- Z=0 に置いて平面図で描いた材では、**描かれた子の属性はすべて `_Below` から来た**（材が切断面より
  下にあるため）。`_At` を振って何も変わらないのを「効かない」と読まない。
- **面を選ばずに効かせたいなら 3 面とも同じ値を書く。** 書いた値は `ResetObject` を通ってそのまま
  描画に効く（[実測](../Metrics/Parametric%20Objects.md#平面図で使われるのは-_below3-面のどれが描かれるかは切断面との関係で決まる)）。

### 子（描かれた実体）の素性

| 型 | 定数 | 構造材では何か（既定の 1 本で） |
| --- | --- | --- |
| 11 | `kGroupNode` | パスのグループと断面のグループ（2 つ） |
| 84 | `kCSGTreeNode` | 3D 実体 |
| 21 | `kPolylineNode` | 断面の面（塗りあり・全面）／中心線（線種 -17・太さ 14）／稜線（`y=±奥行/2`・太さ 7） |
| 5 | `kPolygonNode` | 端部（始端 `x=0`・終端 `x=長さ` の 2 つ・太さ 14） |
| 90 | `kUndoPlaceholderNode` | **undo の置き石**（下記） |
| 0 | `kTermNode` | 終端 |

### クラス欄（欄型 18 = `kFieldClassesPopup`）は**名前**で書く

- **`SetParamValue` / `SetParamString` でクラス名を書く。** `SetParamClass`（`kFieldClass` = 欄型 23
  用）は効かない。
- **`GetParamClass` は正しく書けていても空を返す。** 読み戻しは `GetParamValue`。
- クラスは先に文書へ在ること（`AddClass`）。無い名前を書いたときの挙動は【未確認】
  （[実測](../Metrics/Parametric%20Objects.md#クラス欄欄型-18--kfieldclassespopupは名前で書く)）。

### 真偽欄は `GetParamBool` で読み戻す（文字列は `True` / `False`）

`SetParamBool(索引, false)` の後、`GetParamValue` は **`False`**（`0` ではない）を返す。
書いた `"0"` と突き合わせて「戻された」と読み違えない。

### `ResetObject` 1 回ごとに undo の置き石（型 90）が 1 つ積もる

- 構造材へ `ResetObject` を繰り返すと、PIO の子に `kUndoPlaceholderNode` が 1 回につき 1 つ増える。
  undo イベントを閉じても、VW へ戻った後も減らない（#245）。**置き石は描画を壊さない**
  （[実測](../Metrics/Parametric%20Objects.md#resetobject-1-回ごとに-undo-の置き石型-90が-1-つ積もる)）。
- この調査で「`AttributesMode` を振った個体で 3D 実体が消えた」と見えたものは、
  [issue #166](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/166) で
  **表示欄のせいだった**と決着した（下記「構造材 PIO の子を消すのは『表示欄』で…」）。
- **調査では 1 値 1 個体で比べる。**

### 取り込みで使う形（構造材＝クラススタイル・被覆と中心線は非表示・端部は両端）

**面は 3 面とも書く。**

```cpp
VWParametricObj pio(member);                 // CreateCustomObjectPath で作った構造材
const char* const kFaces[] = { "_Above", "_At", "_Below" };
for (const char* face : kFaces) {
    // 構造材＝クラススタイル（面 6 / 線 4）。クラス欄は**名前**で書く。
    pio.SetParamValue(TXString("MemberClass")     + face, TXString("木構造_柱"));
    pio.SetParamValue(TXString("MemberFillStyle") + face, TXString("6"));  // 面の属性＝クラス属性
    pio.SetParamValue(TXString("MemberPenStyle")  + face, TXString("4"));  // 線の属性＝クラス属性
    pio.SetParamBool (TXString("MemberDisplay")   + face, true);
    // 被覆と中心線は非表示。
    pio.SetParamBool(TXString("CoverDisplay")      + face, false);
    pio.SetParamBool(TXString("CenterlineDisplay") + face, false);
    // 端部は両端（＝始端と終端の両方を表示。これは既定でもある）。
    pio.SetParamBool(TXString("StartCapDisplay") + face, true);
    pio.SetParamBool(TXString("EndCapDisplay")   + face, true);
}
gSDK->ResetObject(member);                   // 1 回で足りる
```

- 書いた値は全部残り、中心線の子が消え、断面と稜線はクラスの色・by-class になった。端部は 2 つとも
  残る。被覆では子が減らない（既定では被覆が描かれていないため【推定】）——「書けていない」と
  読まない（[実測](../Metrics/Parametric%20Objects.md#取り込みで使う形構造材クラススタイル被覆と中心線は非表示端部は両端)）。
- **「端部は両端」はポップアップではなく、`StartCapDisplay_<面>` と `EndCapDisplay_<面>` の真偽 2 つ。**

## 構造材の断面は `MemberType`（材質）が決める——`MajorDepth` が効くかどうかもここで決まる

[issue #169](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/169)
（1 値 1 個体で計 40 個体。[実測](../Metrics/Parametric%20Objects.md#構造材の断面は-membertype材質が決めるmajordepth-が効くかどうかもここで決まる)）。

| `MemberType` | ローカライズ名 | **断面を決めるもの** | 効かない欄 |
| --- | --- | --- | --- |
| `0` | スチール | **`ProfileShape` ＋ `ProfileSize`**（カタログの綴り） | 寸法 4 欄・`ProfileSymbol`・`ProfileSeries` |
| `1` | コンクリート | **寸法 4 欄**（**I 形断面**。`Major` が外形・`Minor` が板厚） | `ProfileShape` / `ProfileSize` / `ProfileSymbol` |
| `2` | 木 | **寸法 4 欄**（同上） | 同上 |
| `3` | カスタム | **`ProfileSymbol`**（シンボル定義の 2D 図形） | 寸法 4 欄・`ProfileShape` / `ProfileSize` |

- **新規に作った 1 本は `0`（スチール）**（この文書では。既定値は文書に記録される）。だから
  **「`MajorDepth` を書いても断面が変わらない」は既定の挙動**で、`1` か `2` にすれば効く。
- **`ProfileShape` / `ProfileSeries` / `ProfileSize` は欄型 14（静的テキスト）だが、
  `SetParamValue` で書けて、`0` では実際に断面を決めている。**

### `0`（スチール）——カタログの綴りが断面を決める

- 既定は AISC (Inch) の `W12 X 30`。`ProfileSize` に同じ系列の項目を書けばその寸法になる。
- **綴りは厳密（空白の有無を問う）、大文字小文字は問わない。**
- **解けなかったときは黙って `W44 X 335`（405.1 × 1118.1）になる**——この寸法が出たら
  「綴りが解けなかった」の合図。
- `ProfileShape` は**ローカライズされた表示文字列**で書く（日本語 UI で `W Shape` は解けない）。
  **形状と寸法は対で書く**（形状だけ変えると落ち先になる【推定】）。
- `ProfileSeries`・`ProfileSymbol`・寸法 4 欄は効かない
  （[実測](../Metrics/Parametric%20Objects.md#0スチールカタログの綴りが断面を決める)）。

### `1`（コンクリート）/ `2`（木）——寸法 4 欄がそのまま断面になる

- 切り替えた直後は `MajorBreadth` / `MajorDepth` の既定（300 × 600）、書けばその寸法。書く順序に
  依らない。
- **【訂正】断面は矩形ではなく `Major` を外形とする I 形**（以前「矩形」と書いていた。#173）。
- **`ProfileShape` / `ProfileSymbol` は完全に無視される。** `Minor` の 2 欄は板厚を決める（下記）
  （[実測](../Metrics/Parametric%20Objects.md#1コンクリート-2木寸法-4-欄がそのまま断面になる)）。

### 副幅・副高さは板厚——断面は矩形ではなく I 形である

[issue #173](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/173)（計 54 個体。
[実測](../Metrics/Parametric%20Objects.md#副幅副高さは板厚断面は矩形ではなく-i-形である)）。
断面群の子は常に 12 頂点の `Polygon`:

| 記号 | 欄 | 既定 | **何の寸法か** |
| --- | --- | --- | --- |
| `B` | `MajorBreadth`（主幅） | 300 | **フランジ幅**（断面の全幅） |
| `D` | `MajorDepth`（主高さ） | 600 | **全せい**（断面の全高） |
| `b` | **`MinorBreadth`（副幅）** | **100** | **ウェブ厚**（中心振り分け。`±b/2`） |
| `d` | **`MinorDepth`（副高さ）** | **100** | **フランジ厚**（上下 2 枚に 1 回ずつ） |

#### 3D 実体が消えるのは輪郭が自己交差したとき——境界は `MajorDepth` の半分

- **`2d > D` で上下のフランジが交差すると型 84 が作られない**（`GetObjectCube` の高さ 0・子が
  9 → 8。平面図には出るのに 3D が無い）。境界は `D/2` で、絶対値ではない。
- **ただし `b ≥ B` なら `2d > D` でも実体はできる**（張り出しが無くなるため【推定】）。
- **安全域は `0 < b ≤ B` かつ `0 ≤ d ≤ D/2`。** 範囲外は「作られない」か「寸法が違う」で、
  **どちらも `ResetObject` は `true`**
  （[壊し方の全体](../Metrics/Parametric%20Objects.md#3d-実体が消えるのは輪郭が自己交差したとき境界は-majordepth-の半分)）。

#### **副を 1 文字も書かなくても壊れる**——既定が 100 / 100 だから

**せいが 200 未満の部材（土台・母屋・胴縁・根太など）は `Minor` を触らなくても 3D を失い、
幅が 100 未満の部材は指定より太くなる。** 「`Minor` を触らない」は安全な指針ではない。

#### 無垢の矩形断面が欲しいなら `MinorBreadth` に `MajorBreadth` と同じ値を書く

`b = B` で輪郭は `B × D` の矩形へ退化し、`d > D/2` でも壊れない。**角材を置くならこれが正しい
書き方。**

#### 壊れたことを書き手が知る道——**3D を数えるしかない**

`ResetObject` の戻り値・読み戻し・`B`/`B1`/`D`/`D1`・断面群の外接はどれも正常に見える。
**指標になるのは `GetObjectCube` の高さ（0.0）と子の型 84 の有無だけ。** 壊しても安全域へ
書き戻して `ResetObject` すれば直る（個体に焼き付かない）。

### `3`（カスタム）——`ProfileSymbol` が断面になる

- **与えたシンボル定義の 2D 図形がそのまま断面になる**——任意の断面を与えられるのはこのモードだけ。
  シンボル無しの既定は 60 × 60。寸法 4 欄は効かない。
- 書き方は `SetParamValue(索引, シンボル名)` でも `SetParamSymDef(索引, 参照)` でもよい。
- シンボル定義は `CreateSymbolDefinition` → `AddObjectToContainer` → `ResetObject` で作る。
  **`CreateSymbolDefinition` は名前を参照で受けて書き換える**ので、実際の名前を読み戻して使う
  （[実測](../Metrics/Parametric%20Objects.md#3カスタムprofilesymbol-が断面になる)）。

### プロファイル（断面）グループは**生成物**——外から書いても消える

- `GetCustomObjectProfileGroup` の群は**描かれた断面とぴったり一致する**（いまの断面はここで測れる）。
- **`SetCustomObjectProfileGroup` は `true` を返すが、`ResetObject` で元に戻る。** 群の子を消しても
  復活する。`GetCustomObjectSecondProfileGroup` / `GetCustomObjectProfileGroupInAux` は `nil`。
- **群は入力ではなく出力。** 断面を変えたいなら欄を書く。

### 効かない欄を書いたときに手がかりは残るか——**残らない**

`0` の 1 本へ寸法 4 欄を書くと、`ResetObject` は `true`、読み戻しは一致、`B`/`B1`/`D`/`D1` は
追随するが、外接は不変。**気付ける道は幾何を測ることだけ**（安いのは
`GetCustomObjectProfileGroup(h)` の外接）
（[実測](../Metrics/Parametric%20Objects.md#効かない欄を書いたときに手がかりは残るか残らない)）。

### 取り込みでどう書くか

```cpp
VWParametricObj pio(member);                  // CreateCustomObjectPath で作った構造材
pio.SetParamValue(TXString("MemberType"), TXString("2"));   // 2＝木（1＝コンクリートも同じ）
pio.SetParamReal (TXString("MajorBreadth"), breadth);       // 断面の幅（フランジ幅）
pio.SetParamReal (TXString("MajorDepth"),   depth);         // 断面の高さ（全せい）
// **この 2 行を省かない。** 既定のまま（100 / 100）だと断面が I 形になり、
// せいが 200 未満の部材では 3D 実体そのものが作られない（上記「副幅・副高さは板厚」）。
pio.SetParamReal (TXString("MinorBreadth"), breadth);       // ウェブ厚＝幅 ⇒ 無垢の矩形
pio.SetParamReal (TXString("MinorDepth"),   depth / 2.0);   // フランジ厚＝せいの半分
gSDK->ResetObject(member);                    // 1 回で足りる
```

- **`MemberType` を書き忘れると、全部書けて読み戻せるのに太さだけ既定（W12 X 30）のまま。**
- **確かめるなら `GetObjectCube` の高さ（または子の型 84）。** 群の外接は `MemberType` の
  書き忘れには効くが `Minor` の壊れ方には効かない。
- 規格断面は `MemberType`=`0` で `ProfileShape` と `ProfileSize` を対で書き、群の外接で確かめる
  （解けなければ黙って `W44 X 335`）。任意の形は `MemberType`=`3` ＋ `ProfileSymbol`。

## パラメータの既定値は「文書」に記録される

自作 PIO のパラメータ既定値（`SParametricParamDef`）は、**その PIO を初めて使ったときに文書へ
焼き込まれる**。あとからコードの既定を変えても、一度でも使った文書では古い既定のまま
（実機で記号の離れの既定を 4 → 200 に変えても `記号の離れ 4.0mm` のままだった）。
**見た目に効く値は既定値に頼らず毎回明示的に書く**（実装例: ホームズ君プラグインの
`draw/ShearWall` の `PlaceOne`）。

## 生成時に「オブジェクトの設定」ダイアログが出る

- `CreateCustomObject` は未定義の PIO の定義を `prefWhen` = **`kCustomObjectPrefAlways`** で作る
  ので、**最初の 1 個だけ**ダイアログが出て止まる。PIO 側の `OnInitXProperties` で
  `kObjXPropShowPrefDialogWhen` を宣言しても間に合わない。**描く前に
  `DefineCustomObject(name, kCustomObjectPrefNever)` を 1 度呼ぶ。**
- **組み込みのドア・窓でも同じで、入口を選ばない**——`CreateCustomObjectPath(…, doRegen=false)`
  でも出る（[issue #122](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/122)。
  [実測](../Metrics/Parametric%20Objects.md#生成時にオブジェクトの設定ダイアログが出る)）。
- **ダイアログを飛ばすと、できる個体の値が変わる**（下記）。

## パラメータを指定して作る口は無い——点の PIO も作成の時点で 1 回描く

[issue #122](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/122)（組み込みの
ドア / 窓。描かれたかは子の数と外形で判定。
[実測](../Metrics/Parametric%20Objects.md#パラメータを指定して作る口は無い点の-pio-も作成の時点で-1-回描く)）。

1. **パラメータの値を受け取る生成 API は無い**【ヘッダ根拠】。入口は `CreateCustomObject` /
   `CreateCustomObjectByMatrix` / `CreateCustomObjectByMatrixEx` / `CreateCustomObjectPath` /
   `CreateCustomObjectPathNoOffset` / `CreateCustomObjectDoubleClick` だけで、再生成を止める旗は
   `CreateCustomObjectPath` の `doRegen` だけ。
2. **`CreateCustomObject` は作成の時点で描く**（書いて `ResetObject` するともう 1 回描く）。
3. **`bInsert=false` は再生成を止めない**（図面に入れないだけ）。
4. **「見本を 1 本普通に作り、残りは描かずに作って見本の全欄を欄型ごとの口で写す」なら、1 本 1 回で
   いつもの経路と全欄一致する個体ができる**（ダイアログを出さない条件で、ドア 672 欄・窓 618 欄とも
   差 0）。
5. それ以外の 1 回で済ませる経路は、ダイアログを出さない条件では同じものにならない（下記）。

### 1 回で済ませる経路と、いつもの経路（A）との差

- **書式の既定値を書き換えてから作る経路は、ダイアログを飛ばした文書では効かない。**
  `CreateCustomObject` は作るときに書式の値へ「初期化」を掛けており、書式の既定値は作られる値の
  出どころではない（ダイアログを通した文書で効いたのはダイアログの値が書式へ書かれるため【推定】）。
- **`CreateCustomObjectPath(…, doRegen=false)` はこの初期化を通らず別物になる**（何も書かずに描かせた
  ドアは袖 FIX 付き）。初期化の中身はダイアログの値に由来すると読める【推定】。
- **線の種類の欄（`kFieldPenStyle` = 26）は `SetParamValue` の文字列では写らない**（`2` を書いても
  `0`）。`SetParamPenStyle` なら写る。理由は突き止めていない
  （[経路ごとの差](../Metrics/Parametric%20Objects.md#1-回で済ませる経路といつもの経路aとの差)）。

### 手順（1 本 1 回で、いつもの経路と同じ個体を作る）

```cpp
// 0) 文書ごとに 1 度: ダイアログを止める
gSDK->DefineCustomObject(name, kCustomObjectPrefNever);

// 1) 文書ごと・種類ごとに 1 度: 見本を普通に作る（この 1 本は作成時に描かれる）
MCObjectHandle sample = gSDK->CreateCustomObject(name, WorldPt(0, 0), 0.0, true);
VWParametricObj from(sample);

// 2) 1 本ごと: 描かずに作る → 置く → 見本を欄型ごとの口で写す → 値を書く → 1 回描く
MCObjectHandle h = gSDK->CreateCustomObjectPath(name, nil, nil, false);
gSDK->SetEntityMatrix(h, placement);          // 位置・角度（描き直さない）
VWParametricObj to(h);
for (size_t i = 0; i < from.GetParamsCount(); ++i) {
    const TXString p = from.GetParamName(i);
    switch (from.GetParamStyle(p)) {
    case kFieldPenStyle:  to.SetParamPenStyle(p, from.GetParamPenStyle(p)); break;
    case kFieldPenWeight: to.SetParamPenWeight(p, from.GetParamPenWeight(p)); break;
    case kFieldFill:      to.SetParamFill(p, from.GetParamFill(p)); break;
    case kFieldColor:     to.SetParamColor(p, from.GetParamColor(p)); break;
    case kFieldClass:     to.SetParamClass(p, from.GetParamClass(p)); break;
    case kFieldBuildingMaterial: to.SetParamBuildingMaterial(p, from.GetParamBuildingMaterial(p)); break;
    case kFieldTexture:   to.SetParamTexture(p, from.GetParamTexture(p)); break;
    case kFieldSymDef:    to.SetParamSymDef(p, from.GetParamSymDef(p)); break;
    default:              to.SetParamValue(p, from.GetParamValue(p)); break;
    }
}
to.SetParamReal("Width", width);              // 欲しい値を書く
gSDK->ResetObject(h);                         // ここで 1 回だけ描く
```

- 実測でずれが出たのは線の種類（欄型 26）だけ。他の枝は同じ理由で落ちうるから足したもの。
- 見本は図面に 1 本残る（消してよいかは確かめていない）。文書をまたいで使い回さない【推定】。
- 減るのは作成時の 1 回ぶん（1 回 10〜20ms）。

### 位置と角度は `SetEntityMatrix` で与える（描き直しを起こさない）

- `CreateCustomObjectPath(…, doRegen=false)` は原点・角度 0 に置く。`SetEntityMatrix` で位置・角度を
  与えても描き直さず、その後の `ResetObject` で `CreateCustomObject(pt, 30°)` と同じ個体になる。
- **`ResetObject` の後で `SetEntityMatrix` で動かすと、外形（`GetObjectBounds`）は undo イベントが
  閉じるまで古い位置のまま**（原点は動いている）。もう一度 `ResetObject` すれば追い付く。
  **置き場所は `ResetObject` の前に決める。**
- 【訂正】以前「古い位置のまま残る」と書いたのは同じ呼び出しの中で読んでいたため。引き金は時間でも
  アイドルでもなく **undo イベントの終わり**で、利用者の図面では新しい位置になっている
  （[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。
  [実測](../Metrics/Parametric%20Objects.md#位置と角度は-setentitymatrix-で与える描き直しを起こさない)）。

## プラグインスタイル

- **当てただけでは何も流れない。** `SetPluginObjectStyle` は関連付けまで。対象を全部置いてから
  **`UpdateStyledObjects` を 1 回**呼ぶ（スタイルを当てない PIO には要らない）。
- **`UpdateStyledObjects` はジオメトリまで触る。** バウンドまで by-style のスタイルなら 1 本ごとの
  `ResetObject` を省ける（速さは変わらない。「リセット（再生成）をまとめられるか」）。
- **バウンドを by-instance にしたスタイルでは省けない。** `UpdateStyledObjects` はいまの形へ
  `自分の span − スタイルの span` を足すだけで、流した回数ぶん累積する。`ResetObject` を 1 度通せば
  戻る（「[バウンドだけ by-instance のスタイルは作れる](#バウンドだけ-by-instance-のスタイルは作れる握っているのは-dialogstartelevationreference-1-件)」）。
- **パラメータの値を配るのは `UpdateStyledObjects` だけ**（`ResetObject` では配られない）。
- **スタイル名 → RefNumber は `GetNamedObject` ＋ `GetObjectInternalIndex` で引く**（専用の口は無い）。
- **スタイルはストーリバウンドも配る**——構造材では横架材の高さを丸ごと変える（下記）。

### スタイルを当てると書いたバウンドはスタイルのものへ置き換わる

[issue #112](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/112)
（2 度走らせて同じ値。[実測](../Metrics/Parametric%20Objects.md#スタイルを当てると書いたバウンドはスタイルのものへ置き換わる)）。

1. **書き換えるのは `SetPluginObjectStyle` ではなく、その後の `ResetObject` /
   `UpdateStyledObjects`。** 当てただけで放っておいても VW が後から作り直すことはない（#245）。
2. **置き換わるのはレコードごと**（`GetObjectStoryBound` の `fOffset` もスタイルの値）——
   「書いて、読み戻して、合っていた」は保証にならない。
3. **順序では避けられない**（スタイルの後に書き直しても効かない）。
4. **配られるのはスタイルの持つバウンドそのもの**（バウンドを持たないスタイルなら 0 / 0 になる）。
5. **`UpdateStyledObjects` でも同じ。**
6. **逃げ道は `kPluginStyleParameter_ByInstance`**——バウンドはパラメータ表に名前が無いのに
   by-style / by-instance の切り替えに従う（理由は下記 #118 で判明）。

**実務上の指針**:

- **バウンドの検査はスタイルを当てて作り直しを通した後に行う。**
- **本ごとに違う高さを持たせるなら `DialogStartElevationReference` だけを by-instance にする**（下記）。
- **そのスタイルでは 1 本ごとの `ResetObject` を省かない**（バウンドを書き換えたときもそのたび）。
- **バウンドは必ず自分で両端とも書く。** バウンドを持たない材にスタイルを当てて `ResetObject`
  すると、実体はスタイルどおりに建つのに、**生えた 2 本の `fBound` が `EStoryObjectBound` のどの値
  でもない値で返り、`GetObjectBoundElevation` が実体と食い違う**（壊れる ID も値も実行ごとに違い、
  VW が書いた値である）——そうなった材は検査できない
  （[2 回の記録](../Metrics/Parametric%20Objects.md#スタイルを当てると書いたバウンドはスタイルのものへ置き換わる)）。
- スタイルを当てない PIO は何も変わらない。

### バウンドだけ by-instance のスタイルは作れる——握っているのは `DialogStartElevationReference` 1 件

[issue #118](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/118)
（[実測](../Metrics/Parametric%20Objects.md#バウンドだけ-by-instance-のスタイルは作れる握っているのは-dialogstartelevationreference-1-件)）。

```cpp
gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);
gSDK->SetPluginStyleParameterType(hSymDef, "DialogStartElevationReference",
                                  kPluginStyleParameter_ByInstance);
```

1. **バウンドの持ち主は `DialogStartElevationReference`。** 由来表（`'PSMP'`）の鍵はパラメータ名
   だけ【ソース根拠】で、181 件を両方向に総当たりして動いたのはこの 1 件だけ。
2. **この 1 件で ID 0 も ID 1 も握る**（`DialogEndElevationReference` を裏返す必要は無い）。
3. **`SetAllPluginStyleParameters(ByInstance)` が効いていたのはこの 1 件も裏返していた副作用。**
4. `SetPluginStyleParameterType` / `GetPluginStyleParameterType` で 1 つずつ読み書きできる。
   **ただし表に無い名前は `by-instance`（0）が返る**——綴りを間違えても静かに 0。

これで「断面や材種はスタイルで揃え、高さは本ごと」になる（6 本で確認）。
**手順**: 1. バウンドを両端とも書く → 2. `SetPluginObjectStyle` → 3. **1 本ごとに
`ResetObject`** → 4. 最後に `UpdateStyledObjects` を 1 回。

#### `UpdateStyledObjects` はバウンドから作り直さない——差分を足すだけで、流した回数ぶん累積する

[issue #125](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/125)（水平材 17 本。
[実測](../Metrics/Parametric%20Objects.md#updatestyledobjects-はバウンドから作り直さない差分を足すだけで流した回数ぶん累積する)）。
バウンドが by-instance のとき:

```
バウンドが、最後に ResetObject で作り直した時点から変わっている本:
    Δz（後） ＝ Δz（前） ＋ (自分のバウンドの span) − (スタイルのバウンドの span)

変わっていない本:
    Δz（後） ＝ Δz（前）（＝正しいまま。作り直し前の値が保たれる）
```

- **終端だけがずれる**（始端＝挿入点はバウンドどおり）。ずれ幅はスタイルの span に追随する。
- **2 度流すと 2 回ぶん足される**（収束しない）。途中で何度も呼ぶのは正しさの問題。
- **一度 `ResetObject` した本でも、バウンドを書き換えて Update だけを流すとずれる。**
- **`ResetObject` を 1 度通せば直る**（レコードは正しい）。ずれは VW へ戻った後もそのまま残る（#245）。
- **バウンドが by-style なら起きない**（本当に作り直している。#81 の 5 本が Update だけで建ったのは
  この経路だったから）。**【訂正】** #118 が書いた「span が同じなので差が 0」という説明は誤り。
- 【推定】`ResetObject` は「いまの形はこのバウンドのもの」と覚え、Update は覚えずに差分を足す
  （規則そのものは実測）。
- **検査では検知できない**（`GetObjectStoryBound` も `GetObjectBoundElevation` も正しい値を返し、
  図だけが違う）。**速さのために `ResetObject` を省く改変を入れない。**

#### パラメータの値を配るのは `UpdateStyledObjects` だけ（`ResetObject` では配られない）

by-style にしたパラメータは、`ResetObject` の後もインスタンスの値のままで、`UpdateStyledObjects`
の後にスタイルの値になる（VW へ戻った後も同じ。#245）。バウンドとパラメータの値は別の経路で
配られる——上の手順の 4 も省けない（[実測](../Metrics/Parametric%20Objects.md#パラメータの値を配るのは-updatestyledobjects-だけresetobject-では配られない)）。

### **`CreatePluginStyle` を呼んではいけない**——スタイルは作られず、文書中の PIO が全滅する

[issue #98](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/98)
（[実測](../Metrics/Parametric%20Objects.md#createpluginstyle-を呼んではいけないスタイルは作られず文書中の-pio-が全滅する)）。

- **「フォルダの指定」ダイアログを出して人を待ち、スタイルは 1 本も作らない。**
- **文書の PIO が全部壊れる**——渡していない本・非選択の本・別種別の PIO・**シンボル定義の中の
  PIO まで**。パスを持つ PIO は「パスのポリライン」（型 21）だけが残り、点の PIO（`Data Tag`）は
  跡形も無い。PIO でない図形は無傷。渡したハンドルは無効になる。
- undo で戻せるとは限らない。**実装で呼んでよい場面は無い。**
- 【ソース根拠】宣言は `virtual void CreatePluginStyle(MCObjectHandle hObj) = 0;`（戻り値無し）。
  PIO へ `ParametricCreatePluginStyle`（`kAction = 61`）と `kAction_FinalizeCreateStyle`（63）を
  投げる UI 経路で、VWFC の既定の `OnCreatePluginStyle` は `kObjectEventNotImplemented`。

### スタイルは SDK だけで作れる——**シンボル定義のサブタイプに PIO の内部 ID を書く**

同 issue #98。作ったスタイルを別の本へ当てて `styleRef` が付くまで確認
（[実測](../Metrics/Parametric%20Objects.md#スタイルは-sdk-だけで作れるシンボル定義のサブタイプに-pio-の内部-id-を書く)）。

```cpp
TXString        name("新しいスタイル");
MCObjectHandle  hSymDef = gSDK->CreateSymbolDefinition(name);  // 名前が使われていれば nil
MCObjectHandle  seed    = /* その種別の PIO を 1 つ作る */;

gSDK->AddObjectToContainer(seed, hSymDef);   // 中へ「その種別の PIO を 1 つ」入れる
gSDK->ResetObject(hSymDef);                  // ★ サブタイプを書く**前に**通す（下記）
gSDK->SetSymbolDefSubType(hSymDef, VWParametricObj::GetInternalID(seed));
gSDK->SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByStyle);

const RefNumber ref = gSDK->GetObjectInternalIndex(hSymDef);
VWParametricObj(target).SetStyle(ref);       // 当たる（styleRef が ref になる）
```

- **`ResetObject` はサブタイプを書く前に通す。** 逆順では `SetSymbolDefSubType` の読み戻しが `0` で
  当たらない（[調査の作法](Investigation%20Techniques.md)・[シンボル](Symbols.md)「定義を組み立てる」）。
- **サブタイプ ＝ その PIO の内部 ID**【ソース根拠】（`StructuralMember` 537・`Data Tag` 599・
  `GridAxis` 647 など。一覧は計測記録）。
- **`IsPluginStyle` は中身も見る**——中の PIO が消えた定義は `subType` が残っていても `false`。
- **`GetPluginStyleForTool` の戻り値を「スタイルがあるか」と読まない**（`true` で `ref=0` が返る。
  返すのはそのツールにいま設定されているスタイル。`ref` を見る）。
- フォルダは見た目の整理で、効くかには関係しない（【ソース根拠】`VWStyleSupport::GetStylesFolder` /
  `MoveStyleToFolder`）。1 つずつの由来は `SetPluginStyleParameterType` / `AddItemToPluginStyle`
  （`kPluginStyleParameter_ByInstance` / `_ByStyle` / `_AllwaysByInstance` / `_ByCatalog` /
  `_ByMixed`）。由来表は定義のタグ付きデータ `'PSMP'`【ソース根拠】。

## プロファイル（断面）グループは空でないことを確かめる

空の断面は「オブジェクトはあるのに描かれない」を招く。生成後に読み戻して数える。
**ただし群が空でないことは 3D があることを意味しない**（輪郭が自己交差すると型 84 が無い。
上記「副幅・副高さは板厚」）。**3D は `GetObjectCube` の高さか子の型 84 で見る。**

## ポップアップの値は表示文字ではなくキー

ポップアップ（種別 8）の値は英語または数値文字列のキーで、表示だけがローカライズされる。数値キーは
**項目の並び順（0 始まり）**で、`<自動>` のような先頭項目も 1 つ数える。

**構造材ツールの「構造用途」（`StructuralUse`）の全項目**（実機のドロップダウンで確認）:

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
| `MemberType`（**材質**。索引 2） | `0` / `1` / `2` / `3` | **スチール / コンクリート / 木 / カスタム**。**断面をどこから採るかを握る欄**で、`MajorDepth` が効くかどうかもここで決まる（上記「[構造材の断面は `MemberType`（材質）が決める](#構造材の断面は-membertype材質が決めるmajordepth-が効くかどうかもここで決まる)」）。**以前ここには「`2`＝構造材」と書いてあったが誤り**——`2` は木である（#169 で 4 択の全項目を採った）。新規に作った 1 本は `0` だった（既定値は文書に記録される） |
| `StartCondition` / `EndCondition` | `3` | 直切り |

## 構造材ツールは軒の出・差し込みを持たない

軸組ツール（`FramingMember`）は挿入点＝支持点から差し込み＋軒の出だけ材を伸ばすが、構造材ツール
（`StructuralMember`）は**パスがそのまま材の範囲**。垂木などは、パスの始端を**軒先**にし、その位置・
高さ・バウンド offset を自分で計算する。

## 打ち切った調査: 構造材 PIO から「スパン」「部材長」をパラメータで読む

**結論: 構造材 PIO のパラメータ表に部材長にもスパンにも当たるものは 1 件も無く、パラメータから
部材の長さを読む道は無い**（[issue #95](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/95)）。
実体の長さは **`GetCustomObjectPath` で読み戻したパスの両端の距離**で測る。鉛直材なら
`StartElevation` / `EndElevation` の差でも測れるが、水平材・斜め材で使えるのはパスの読み戻しだけ。

### 「無い」と言い切れる根拠（3 つが独立に同じ答えを出している）

1. **パラメータ表の全数列挙（実機）**——181 件に `Span` も軸方向の長さも無い（長さらしいのは断面の
   寸法と高さ方向のオフセットだけ）。表はインスタンスに依らない。
2. **SDK の全数検索【ヘッダ根拠】**——`[Ss]pan` で構造材に関わるものは無く、部材長を返すオブジェクト
   変数も無い。
3. **実運用（実機）**——取り込みで `"Span"` / `"スパン"` が横架材 266 本・垂木 54 本の全数で
   解決しなかった（[根拠の詳細](../Metrics/Parametric%20Objects.md#無いと言い切れる根拠3-つが独立に同じ答えを出している)）。

### `CenterPointLength(長さ)` は部材長ではない——**センターマークの長さである**【推定】

「長さ」で引き当たる唯一のパラメータだが、実長 5333mm の柱で **100**（既定も 100）。表の並び
（153〜160 のセンターマーク群）からセンターマークの線の長さ【推定】。**教訓: ローカライズ名だけで
当てない。引き当てたら値を実測と突き合わせる。**

### では OIP の「スパン」欄は何か——**パラメータではなく計算値**【推定】

パスから毎回計算して表示しているだけで、読み書きできる値としては存在しないと見るのが整合的
【推定】。目視でしか見えず、読む道が無いことは確定しているので**これ以上取りに行く必要も無い**。

### 代わりにどう測るか

```
MCObjectHandle path = gSDK->GetCustomObjectPath(pio);   // 作り直しの「後」に読む
// 鉛直材（NURBS）: NurbsCurveGetNumPieces / NurbsGetNumPts / NurbsGetPt3D で両端を取る
// 水平材（2D ポリライン）: VWPolygon2DObj の GetVertexCount / GetVertexPoint(i)
```

- **読むのは `ResetObject` の後。** 水平成分はビット一致で残るので、両端の距離は水平材・斜め材でも
  部材長として読める。「実体があるか」を数えるなら鉛直材だけ見ればよい。

## 構造材同士の「自動結合」を作る API は無い【ヘッダ根拠】

構造材ツールの**自動結合（Auto Join Members）**は、置いた構造材同士に関連付け（association）を
持たせて追随させる（[VW ヘルプ](https://app-help.vectorworks.net/2023/eng/VW2023_Guide/Structural/Creating_structural_members.htm)）。
VW 2026 SDK（mac）の `SDKLib/Include` 全体（ヘッダ 456 本 ＋ `vs.py`）を全数検索して、**専用の
API は 1 つも無い**（[検索結果の表](../Metrics/Parametric%20Objects.md#構造材同士の自動結合を作る-api-は無いヘッダ根拠)）。

- 構造材について SDK が持つのは内部 ID（`kInternalID_StructuralMember = 537`）・フォルダ定数・
  `ovIsStructural`・VS 専用の `SM_FromShape` / `SM_Preferences` だけ。「結合」と名の付く API
  （`JoinWalls` など）は全部よそのもの。
- **汎用の関連付けは ISDK では「読む・消す」しか無い**（`GetNumAssociations` / `GetAssociation` /
  `DeleteAssociations`。**足す**口は VS の `AddAssociation` だけ）。`associationKind` の定数は
  SDK に無い。`kKludgeAddAssociation` は使用禁止の private API。
- VS を SDK から流す道（`IVectorScriptEngine::ExecuteScript`）はあるが、`AddAssociation` で自動結合が
  作れるかは未確認。
- **作成**: 公開 API は無い。見た目だけならパスを詰めて突き付ける（関連付けではないので追随しない）。
  **読み取り**: 汎用の口が自動結合を返すかは実機で確かめるしかない。

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

CI が緑でも実機で初めて出る。**新しい PIO を足すときは最初から入れる**（実装例: ホームズ君
プラグインの `Extensions/ExtColumnMark`）。

1. **設定ダイアログ抑止**（`DefineCustomObject(..., kCustomObjectPrefNever)`）。
2. **PIO のジオメトリは PIO 自身のローカル座標で持たれる。** 対象のワールド座標は
   `GetObjectToWorldTransform` ＋ `InversePointTransform` でローカルへ落としてから描く。
3. **リセットの契機は自分で宣言する**（既定では印刷でも再オープンでも描き直されない）。
   `ResetOnMove` / `ResetOnRotate` と `kObjXPropResetBeforeExport` を立てる。

**【限界】他の図形が変わったときに再計算する仕組みは VW に無い**（`kObjXProp*` のリセット系は PIO
自身に関するものだけ）。`kObjXPropResetBeforeExport` で、外へ出る瞬間には必ず実物と一致する。
**PIO を同梱する代償は小さい**——プラグインを外しても PIO が描いたジオメトリは保存されていて表示できる。

## `Recalculate` の中から見える環境は「取り込み時のリセット」と「OIP 編集」で**同じ**

[issue #183](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/183)。自前の PIO を
登録して測った 11 項目がすべて一致し、違ったのは編集したパラメータの値だけ
（[表](../Metrics/Parametric%20Objects.md#recalculate-の中から見える環境は取り込み時のリセットとoip-編集で同じ)）。

- **`Recalculate` の中で自分以外の図形を探して測れる**——`GetNamedObject`・レイヤの走査
  （`FirstMemberObj` → `NextObject`）・`GetNamedLayer` がどちらの文脈でも効く。
- 行列はこの測り方（原点・無回転）では足りなかったので測り直した（「[PIO の行列を読む口](#pio-の行列を読む口名前は-4-つ実体は-2-つ)」）。

### リセットの理由は `OnAddState` で分かる（`kObjXPropAcceptStates` が要る）

`OnInitXProperties` で `kObjXPropAcceptStates` を立てると `Recalculate` の直前に
`ObjectState::fSpecifier` が届く:

| 起こしたこと | 届いた `fSpecifier` |
| --- | --- |
| `CreateCustomObject` | `13` `kObjectCreated` → `0` `kFirstRegenReset` → `Recalculate` |
| `gSDK->ResetObject` | `16` `kObjectExternalReset` → `Recalculate` |
| OIP でパラメータを 1 つ編集 | `3` `kParameterChangedReset` → `Recalculate` |

立てなければ `ObjectState::kAction` 自体が来ない（[`Info/Parametric Extended Properties.md`](../Info/Parametric%20Extended%20Properties.md)）。
OIP の編集 1 回で `Recalculate` は 1 回。

### 最初の `Recalculate` では**自分の外接が読めない**

`kFirstRegenReset` の `Recalculate` で `GetObjectBounds(自分)` は **`true` を返すのに空の矩形**
（`left` = `bottom` = `DBL_MAX`、幅・高さ `-inf`）。2 回目以降は前回の絵の外接が返る。
**自分の外接を足場にする PIO は初回だけ壊れる**——`DBL_MAX` を疑うか、外接に頼らない作りにする。

### SDK で登録した線分 PIO に `LineLength` は**無い**

`kParametricSubType_Linear` で登録した PIO の表には登録した 3 つしか出ず、`GetParamIndex("LineLength")`
は `(size_t)-1`。**`SetLinearObjectPos` も効かない**（`A=(0,0) B=(0,0)` のまま）。**SDK で登録した
線分 PIO の長さを与える口はこの経路には無い。**

### 測り方（この節の数値の出どころ）

実機確認プラグインの殻へ調査用の PIO を登録して測った（[PR #184](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/184)）。
プローブの突き合わせの最終行「違いがある」は鍵の切り出しの読み違えで、実質の差は 0
（[詳細](../Metrics/Parametric%20Objects.md#測り方この節の数値の出どころ)）。

## PIO の行列を読む口——名前は 4 つ、実体は 2 つ

【ヘッダ根拠】（同梱の VWFC 実装を読んだ結果。[表](../Metrics/Parametric%20Objects.md#pio-の行列を読む口名前は-4-つ実体は-2-つ)）:

- **`VWParametricObj::GetObjectToWorldTransform` と `VWObject::GetObjectMatrix` は完全に同じ**
  （どちらも `GS_GetEntityMatrix`）。片方が駄目でももう片方に変えても変わらない。
- `VWObject::GetObjectModelMatrix` は `gSDK->GetEntityMatrix`（底）。違いは旧 callback が
  **`bUseLegacyZ = true`** を渡す 1 点で、**差が出うるのは Z だけ**。
- **`GetObjectToWorldTransform` の「world」は「レイヤ座標」の意味**——シンボル内・グループ内で
  親を遡って積む処理は無い。

### **どの口も、どの文脈でも、実際の配置を返す**——行列は化けない

[issue #185](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/185)。原点から離し
30° 回した PIO で、4 口 × 3 文脈（`kFirstRegenReset` / `kObjectExternalReset` /
`kParameterChangedReset`）× 入口・出口の 20 件がすべて外から読んだ真値と一致
（[実測](../Metrics/Parametric%20Objects.md#どの口もどの文脈でも実際の配置を返す行列は化けない)）。

- **行列は最初の `Recalculate` から正しい**（外接と対照的）。描く前後でも変わらない。
- `InversePointTransform` も毎回同じ値を返す。`CreateCustomObject(名前, 位置, 角度)` の位置と角度
  （度）はそのまま行列になる（線分サブタイプでも）。
- **「OIP 編集時にどの口で読むか」は前提が成り立たない——どれでも正しい。**

### **編集時だけローカル座標がずれたなら、疑うのは行列ではない**

変わったのは入力のほう。当たる順に【推定】: ①自分の外接や自分の絵を足場にしていないか（外接は
文脈で化ける）②相手の図形の世界座標の出し方（相手も PIO なら同じ波に巻き込まれる）③探し方で
拾う顔ぶれが変わっていないか（同じ顔ぶれが返ることは確かめていない）。

### 測り方（この節の数値の出どころ）

[PR #186](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/186)。同じ測り方を
するときに必ず要る 2 点（教訓）:

1. **被験 PIO を原点から離し、軸に乗らない角度で置く**（単位行列に化けても同じ値が出ないように）。
2. **`Recalculate` の外から読んだ値を真値として突き合わせる**（中と中を比べても正しさは決まらない）。

書き溜めの区切りは値に現れない `鍵 ## 値` にする。殻を触る調査は公開ビルド（転がりタグ `probes`）
では確かめられない——PR の Actions の成果物を手で入れる
（[`plugin/README.md`](../plugin/README.md)「PR のビルドを手で入れて確かめるとき」。
[詳細](../Metrics/Parametric%20Objects.md#測り方この節の数値の出どころ-1)）。

## パラメータ変更を PIO へ伝える口は無い

`kParameterChangedReset`（`ObjectStateData_ParamChanged`）は **PIO 側が受け取る**メッセージ
（`IObjUpdateSupport::OnState`）で、外から送る API は `ISDK` に無い。外から書いた後は
`ResetObject` を呼ぶ（[Investigation Techniques](Investigation%20Techniques.md)）。

## リセット（再生成）をまとめられるか——**まとめても速くならない。減らせるのは「回数」だけ**

構造材 200〜400 本の取り込みで 1 本 93〜130ms
（[issue #81](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/81)）。
構造材 PIO 30 本で実測（[実測](../Metrics/Parametric%20Objects.md#リセット再生成をまとめられるかまとめても速くならない減らせるのは回数だけ)）。

1. **止める口も、まとめて 1 度で全部を作り直す口も無い。**
2. **`UpdateStyledObjects` はジオメトリの作り直しまで行うが、所要はほぼ同じ**（12.4ms/本 対
   11.5ms/本）——作り直しの費用は 1 本ごとに掛かる。
3. **効くのは作り直しを 2 回から 1 回に減らすこと**——`CreateCustomObjectPath(…, doRegen=false)`
   ＋ **Z の差が 0 のパス**で、鉛直・水平・斜めのどれでもおよそ半分になる。

**`SetPColorsByClass` 等や `SetObjectClass` も PIO では 1 回ごとに作り直しを起こす**（構造材で 1 回
9ms）——文書の既定を先に立てて避ける（[Attributes and Classes](Attributes%20and%20Classes.md)）。

### 再生成・再描画を止めておく口は SDK に無い【ヘッダ根拠】

`Batch` / `Defer` / `Suspend` / `Freeze` / `DisableDrawing` / `SuspendRedraw` / `LockDrawing` /
`BulkUpdate`、`Redraw`（`RedrawRect` は描き直す口）、プログラム変数のセレクタまで探して、止める・
遅らせる口は無い（[探した先](../Metrics/Parametric%20Objects.md#再生成再描画を止めておく口は-sdk-に無いヘッダ根拠)）。
呼び出し側が `ResetObject` の回数と順序を決めるしかない。

### ただし `CreateCustomObjectPath` だけは再生成を省ける（`doRegen`）【ヘッダ根拠】

```cpp
virtual MCObjectHandle CreateCustomObjectPath(
    const TXString& name, MCObjectHandle pathHand = NULL,
    MCObjectHandle profileGroupHand = NULL, bool doRegen = true) = 0;
```

SDK でこの引数を持つのはこれだけ。既定 `true` なので**何も指定しなければ作成時と `ResetObject` で
計 2 回作り直している**（実機の内訳が裏づけた）。

### まとめて全部を作り直す口も無い（レイヤへの `ResetObject` も再描画も効かない）

`ResetObject(レイヤのハンドル)` も `RedrawRect`（図面全体）も中の PIO を 0 本も作り直さない。
VW へ戻った後も作り直されない（#245）。**この 2 つはもう試さない**
（[実測](../Metrics/Parametric%20Objects.md#まとめて全部を作り直す口も無いレイヤへの-resetobject-も再描画も効かない)）。

### `UpdateStyledObjects` はジオメトリの作り直しまで行う（issue #81 の (b)）——**バウンドが by-style のときは**

- **バウンドまで by-style のスタイル**なら、`ResetObject` 抜きで置いた PIO が
  `UpdateStyledObjects` 1 回でバウンドどおりに作り直される（「[プラグインスタイル](#プラグインスタイル)」の
  「最後に 1 回」は実体まで作る）。by-instance なら差分を足すだけ
  （「[`UpdateStyledObjects` はバウンドから作り直さない](#updatestyledobjects-はバウンドから作り直さない差分を足すだけで流した回数ぶん累積する)」）。
- 書いたバウンドが残ったのはスタイルの span と同じだったからで、食い違えばスタイル側が勝つ
  （「[スタイルを当てると書いたバウンドはスタイルのものへ置き換わる](#スタイルを当てると書いたバウンドはスタイルのものへ置き換わる)」）。
- **速くはならない**（1 本ごとの費用）。対象はそのスタイルを当てた全オブジェクトなので、
  **最後に 1 回だけ呼ぶ**。

### `doRegen=false` は速い。ただし**Z の差が 0 のパスで作る**こと（さもないと span + 渡した Z の差になる）

[#109](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/109)。
**`doRegen=false` で作ると、最初の `ResetObject` の Z の差が「span ＋ 渡した Z の差」になる。**
狂いの源は渡した Z の差だけで、水平成分は関わらない。

> **鉛直材・水平材・斜め材のどれであっても、両端の Z を等しくしたパスで作れば、
> 1 回目の `ResetObject` から正しい形になる**（部材の向きを問わない）。

#81 が書いた「0 長のパスで作れ」は、**鉛直材ではたまたま「Z の差 0 ＝ 0 長」だった**から
そう見えただけで、一般の規則は**「Z の差を 0 にする」**である。

#### 実測——同じケースを `doRegen` の true / false で 2 本ずつ作り、1 回目の `ResetObject` を読み比べた

決め手は斜め・Z の差 3000 のケースで、ずれは 3 次元長（3605.55）ではなく **3000 ちょうど ＝ 渡した
Z の差**。平面長のずれは 0。狂っても 2 度目の `ResetObject` で必ず正しく収まるが、それでは速くならない
（[表](../Metrics/Parametric%20Objects.md#実測同じケースを-doregen-の-true--false-で-2-本ずつ作り1-回目の-resetobject-を読み比べた)）。

#### 端部オフセットとプラグインスタイルを当てても、結論は変わらない

`StartOffset` / `EndOffset` とプラグインスタイルは `doRegen` の判断に関係しない。副産物として見つけた
「スタイルを当てるとバウンドが書き換わる」は #112 で決着した（上記「スタイルを当てると…」）。
**バウンドの検査はスタイルを当てた後に行う。**

#### 自己修復（`SetCustomObjectPath` → もう一度 `ResetObject`）は `doRegen=false` でも効く

正しい最終形のパスへ差し替えて `ResetObject` すれば直り、渡したパスがバウンドの解決形と同じなら
嘘の `fOffset` も入らない。**ただし保険であって手順ではない**——走ったら呼び出し側が規則
（Z の差 0）を破っている合図。

### 実測（構造材 PIO 30 本 / VW 2026 / mac / 新規の空図面）

- **鉛直材**（#81）: 23.6ms/本 → **`doRegen=false` ＋ 0 長で 11.9ms/本**。`doRegen=false`（長さ 1）
  で置いてからまとめて `ResetObject` すると全件 3001 に狂う（VW へ戻った後も同じ。#245）。
- **水平材・斜め材**（#109。Z の差 0）: 27.1 → **14.6ms**、28.7 → **15.4ms**（30 / 30 が 1 回で正しい）。
- **作り直し 1 回がおよそ 11.5〜14.8ms で費用のほぼ全部**。絶対値は日で 1〜2 割ぶれるので比で読む
  ——**`doRegen=false` はおよそ半分**（消えるのは「作る」側）。第 2 パスへ寄せても速くならない
  （[内訳の表](../Metrics/Parametric%20Objects.md#実測構造材-pio-30-本--vw-2026--mac--新規の空図面)）。
- 所要は呼び出しの中の時間だけ（[Investigation Techniques](Investigation%20Techniques.md)「いつ読むか」）。

### 実装の手順（構造材 PIO を大量に置くとき）

**向きで分ける必要は無い。**

1. パスを `CreateNurbsCurve` ＋ `Add3DVertex` の 2 点で作る。**両端の Z は必ず等しくする**（0 で
   よい）。平面上の位置は実際の位置そのまま。
2. `CreateCustomObjectPath(name, path, nullptr, false)` で作る。
3. クラス・属性は**作る前に文書の既定として立てておく**（[Attributes and Classes](Attributes%20and%20Classes.md)）。
4. スタイルを当てる → **バウンドを両端とも書く** → パラメータを書く。**この順で書いたバウンドが
   残るかは確かめていない【推定】**——バウンドの解決結果の検査はスタイルを当てた後に行う
   （置き換えの機構は上記 #112）。
5. `ResetObject` を **1 回だけ**呼ぶ。
6. 念のため長さを読み戻し、狂っていたら自己修復。**ここが走ったら 1 を疑う。**

※ 構造材のパスの作り方は上記「構造材のパスは **2D で渡す**」（#196）も併せて読む。

## 構造材 PIO の子を消すのは「表示欄」で、属性モード（`AttributesMode`）ではない——しかも可逆

[issue #166](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/166)。「`AttributesMode`
を 0 → 3 と振ると描画が消え、戻しても戻らない」という観測は**起きない**
（[実測](../Metrics/Parametric%20Objects.md#構造材-pio-の子を消すのは表示欄で属性モードattributesmodeではないしかも可逆)）。

1. **`AttributesMode` / `AttributesMode3D` / `MemberAttributes_3D` は子を 1 つも増減させない。**
2. **子を消すのは表示欄**（`MemberDisplay` / `CenterlineDisplay` / `StartCapDisplay` /
   `EndCapDisplay` の 3 面）で、欄と消える子は 1 対 1。
3. **可逆**——`true` へ戻して `ResetObject` すれば既定どおりに戻る。

### どの欄がどの子を消すか（実測）

素の構造材の子は置き石と終端を除いて **9 つ**（`11(Group)×2` `84(CSGTreeNode)×1`
`21(Polyline)×4` `5(Polygon)×2`）。`MemberDisplay` と `CenterlineDisplay` が Polyline を 1 本ずつ、
`StartCapDisplay` と `EndCapDisplay` が Polygon を 1 つずつ消す（`CoverDisplay` は既定では何も消さない）。
**型 84（3D 実体）はどの欄でも消えない**——2D の表示欄で 3D は消せない
（[累積の表](../Metrics/Parametric%20Objects.md#どの欄がどの子を消すか実測)）。

### 属性モードは何をしても子を動かさない（実測の網）

1 個体での 0→3→0、1 値 1 個体、同じ値の繰り返し、置き石を積んだ後、undo イベント中、クラス属性を
先に書いた後——どれも 9 のまま。新しい個体も既定どおりで、壊れが文書に及ぶことも無い
（[網の表](../Metrics/Parametric%20Objects.md#属性モードは何をしても子を動かさない実測の網)）。

### なぜ「消えた」と読めてしまったか——**子の型を決め打ちして数えない**

元の観測は子を「型 5 / 21 / 84」と決め打ちして数え（型 11 が漏れる）、直前の段で表示欄を `false`
にしていた。**教訓**（[Investigation Techniques](Investigation%20Techniques.md) にも置いた）:

- **型を決め打ちせず、置き石以外の子を全部数えて型の内訳ごと出す。**
- **1 個体に複数の欄を順に書くと原因が前の段へずれる。** 欄ごとに個体を分けるか、1 段ごとの差分で読む。

### ついでに確定したこと

- **置き石（型 90）は何個積もっても描画に影響しない。undo イベントが開いていても積もる**
  （「undo イベントを開かないから残る」は誤り）。**消せない**（`DeleteObject(h, useUndo=false)` でも
  `ClearUndoTableDueToUnsupportedAction()` でも減らない）。
- **`DeleteRegenerableSubObjects(hParametric)` は PIO の外から呼んでも効く**（子が Group×2 と終端
  だけになり、次の `ResetObject` で戻る）——外から子を作り直させる口としても使える。
- **子には常に型 0（`kTermNode`【ヘッダ根拠】）が 1 つ在る。** 描かれた子として数えない。
- **`ResetObject` を通す前の `GetObjectBounds` / `GetObjectCube` は `true` を返すのに ±`DBL_MAX` の
  反転した矩形。** 1 回通してから読む。
- `MajorDepth` が効かなかったのは `MemberType`=`0` だったから（#169）。**「書けて読み戻せる」は
  「効く」ではない。**

### 実務上の指針

- **同じ個体へ値を書き直して描き直す流れで壊れる心配は要らない。** `ResetObject` の回数を絞るのは
  所要のため。
- **絵を消したい／出したいときに触るのは表示欄。** `AttributesMode` は「どの属性で描くか」の欄。
