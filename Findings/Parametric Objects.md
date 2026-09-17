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
この節は、その調査の過程で分かった**「高さと実体が何で決まるか」の機構**である
（[issue #59](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/59) で
「階だけが違う 2 本の `_Story` バウンド」を潰した結果は下記のとおり**白**だった。
**あの 46 本の原因は
[issue #61](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/61) で
確定した**——下記「`ResetObject` がバウンドから作り直すのは…」）。

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
   `(0,0,0)→(0,0,-0.000000)` になった（`z1-z0 = 0`）。両端が同じ絶対Zへ解決されてしまう
   書き方の代表が `eStoryObjectBound_LayerElevation` で、これで書くと階もレベル種別も
   見てもらえず両端が「乗っているレイヤの高さ」へ落ちる（下記「階やレベルを指すバウンドは
   `eStoryObjectBound_Story` で書く」）。**0 長へ潰れる条件はもう 1 つあり、
   バウンドを 1 本も持たないまま `ResetObject` を呼んでも潰れる**（下記
   「同じオブジェクトに 2 本のバウンドを書く」）。
5. **`ResetObject` が呼ばれる前提では、パスの絶対Zは意味を持たない。** わざと大きく
   外れた絶対Z（`0→1`）のパスで新しいオブジェクトを作り、3 と同じバウンド（572/3531）を
   掛けて `ResetObject` すると、結果は 3 と寸分違わず一致した（挿入点 `(0,0,3531)`、
   パス `(0,0,0)→(0,0,-2959)`）。**つまりバウンドを両端とも設定して `ResetObject` を
   呼ぶなら、パスに正しい絶対Zを持たせる意味は無い**（最終的な実体はバウンドだけで
   決まり、パスの元の値は上書きされる）。
   ——**ただし作り直してもらえるのは「長さ 0」か「長さ 1e-7 以上」のパスだけ**で、
   その間の長さで渡すと作り直されない（#61。下記「`ResetObject` がバウンドから
   作り直すのは…」）。

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
  **「厳密に 0 長」か「長さ 1e-7 以上」**であれば足り、値そのものはバウンド解決後に
  上書きされる（**その間の長さで渡すと作り直してもらえない**——下記
  「`ResetObject` がバウンドから作り直すのは…」）。
  ただし `ResetObject` を呼ばない・呼べない経路（バウンドを設定しないまま使う、
  他の PIO で `ResetObject` を省く等）ではパスの絶対Zがそのまま実体になる（上記 1）ので、
  **「バウンドを必ず設定して `ResetObject` する」経路を外れない設計であることが前提**。
- **上下端のバウンドが同じ絶対Zへ解決されないことを、書き込み側で保証する**（上記 4）。
  `SetObjectStoryBound` の戻り値が `true` でも、`GetObjectBoundElevation` で両端の
  解決結果を読み比べ、一致していたら手前で弾く・別の基準へ振り替えるなどの対処が要る
  ——**戻り値やレコードの読み戻しだけでは潰れを検知できない**（record は書いたとおりに
  読み戻る。実際に潰れるかは解決結果次第）。
- **`SetCustomObjectPath` で差し替えるときは、挿入点を読んでから相対座標を計算する**
  （上記 2）。挿入点は `GetObjectModelPos`（VWFC）で読める。**差し替えたパスは以後
  `ResetObject` で作り直されず、逆にバウンドのほうが書き換わる**ので、差し替えるなら
  渡す形が最終形であること（下記「`SetCustomObjectPath` で差し替えたパスには…」）。

**検証範囲の限界**: 上記はすべて**鉛直材**（X=Y=0 の2点）で確認したもの。水平材
（両端が異なるX/Yを持ち、Zの差はバウンドoffsetの差だけで表す設計。`draw/Member.cpp`）で
同じ機構が働くかは未確認（同じ `ResetObject` 起点の再構築である可能性は高い）。
`eStoryObjectBound_Story`（他階基準）と `fLayerLevelType`（横架材天端等の名前付きレベル）
については下記の節で実機確認済み。

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

### `ResetObject` がバウンドから作り直すのは「長さ 0」か「長さ 1e-7 以上」のパスだけ（その間に死角がある）

[issue #61](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/61) で
**実機確認済み**（VW 2026 / mac。`probes/runtime/reset-object-path-rebuild-threshold/` を
**2 つの図面**——事故のモデル（`_Story` バウンド。解決Z 572 / 3531）と新規の空図面
（`LayerElevation` バウンド。解決Z 2500 / 5500）——で走らせ、事故のモデルでは 3 回とも
同じ結果が出た。以下の値は実行ログそのまま）。

#### 結論: 判定しているのは**パスの長さ**で、`(0, 1e-7)` の帯では作り直されない

`CreateCustomObjectPath` でオブジェクトを作り、バウンドを 2 本書いて `ResetObject` を
呼んだとき、**解決済みバウンドからパスを作り直すかどうかは、渡したパスの長さだけで決まる**。

| 渡したパスの長さ | `ResetObject` は | 実体は |
| --- | --- | --- |
| **厳密に 0**（両端がビット一致） | **作り直す** | バウンドどおりに出る |
| **0 より大きく 1e-7 未満** | **作り直さない** | **0 長のまま（図に出ない）** |
| **1e-7 以上** | **作り直す** | バウンドどおりに出る |

実測（基準Z 2500 / バウンドは 2500 と 5500 へ解決 ＝ 作り直されたときの長さ 3000）:

| 渡した Z の差 | `ResetObject` 後の `z1−z0` | |
| --- | --- | --- |
| `0` | `3000` | 作り直された |
| `4.5474735088646412e-13`（1 ULP） | `4.5474735150684958e-13` | **作り直されない** |
| `1.8189894035458565e-12`（4 ULP） | `1.8189894134720238e-12` | **作り直されない** |
| `1.0004441719502211e-11` | `1.0004442019768773e-11` | **作り直されない** |
| `9.999894245993346e-10` | `9.9999242453588253e-10` | **作り直されない** |
| `9.9999851954635233e-08` | `1.0002985186580708e-07` | **作り直されない**（温存の最大） |
| `3.0000001061125658e-07` | `3000` | 作り直された |
| `9.9999988378840499e-07` / `3.0000001061125658e-06` / `1.0000000202126103e-05` | `3000` | 作り直された |
| `0.001` / `0.1` / `1` / `100` | `3000` | 作り直された |

- **閾値は 1e-7（図面の単位）。** 温存された最大が `9.9999851954635233e-08`、作り直された
  最小が `1.0011717677116394e-07`——**1e-7 を 1.2e-10 の幅で挟んだ**。
- **閾値は絶対値で、座標の大きさに依らない。** 基準Zを 572 / 2500 / 1,572,000 / 3,500,000 と
  変えても切り替わる位置は動かなかった（**相対ではない**）。
- **見ているのは Z の差ではなく 3 次元の長さ。** Z の差が 1 ULP でも `dx` を 1000 や
  `1e-5` にすると作り直された。逆に `dx = 9.9999999999999995e-08` ／
  `dz = 9.9999851954635233e-08`（**どの成分も 1e-7 未満**だが長さは `1.41e-7`）も
  作り直された——**成分ごとの比較では説明が付かず、長さで見ていると読むほかない**。
- **作り直されなくても、挿入点は ID 0 の解決Zへ動く。** 基準Z 3,500,000 で作った個体が、
  パスは温存されたまま挿入点だけ 2500 へ移った。**位置はバウンドに従い、長さだけが
  従わない**という半端な状態になる（画面上は「その場に潰れた線」）。
- **温存された値は厳密には入力と一致しない。** `L` → `L + H·L²`（`H` は作り直したときの
  長さ）になる。例: `9.9999851954635233e-08` → `1.0002985186580708e-07`
  （差 `3.00e-11` ＝ `3000 × (1e-7)²`。事故のモデル側でも `1e-7` → `1.0002955562113858e-07`
  で差 `2.96e-11` ＝ `2959 × (1e-7)²` と、同じ形になる）。実用上は「そのまま」だが、
  **ビット比較で「触っていない」と判定してはいけない**。

#### これが「柱 46 本が長さ 0」の原因だった

[#56](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/56)（パスの型・
バウンドの書き方）でも
[#59](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/59)（2 本の
バウンドの扱い）でもなく、**残っていたのはこれ**である。事故の取り込みは両端の世界座標Zを
別々に計算しており、**ビット一致した個体（0 長）は作り直され、1 ULP だけ残った個体
（`4.54747e-13`）は作り直されなかった**。197 本中 46 本という内訳は、その丸めの当たり外れ
そのもの。実機で採った 3 地点の読み戻し（`4.54747e-13` が ①②③ とも変わらない）は、
上の表の 2 行目と同じ現象である。

**実務上の指針**:

- **両端の Z は同じ値（同じ変数）で渡し、厳密に退化させる。** 「ほぼ同じ」では駄目で、
  **1 ULP 残るだけで再構築が止まる**。別々の式で計算した結果を突き合わせない。
- **作った直後に検算できる。** `GetCustomObjectPath` → `NurbsGetPt3D` で長さを読み、
  **0 でも 1e-7 以上でもなければ危険**（`ResetObject` を呼んでも実体が出ない）。
- 潰れてしまった個体を後から直すなら `SetCustomObjectPath` で差し替えるしかないが、
  **差し替えるとバウンドのほうが書き換わる**（下記）。作るときに退化させて渡すほうが
  副作用が無い。

#### `SetCustomObjectPath` で差し替えたパスには、この判定が掛からない（バウンドのほうが書き換わる）

一度 `ResetObject` を通したオブジェクトのパスを `SetCustomObjectPath` で差し替えると、
**その後の `ResetObject` は長さに関わらずパスを作り直さない**（0 長 / 1 ULP / 100 の
3 通りとも、渡したパスがそのまま残った）。さらに**その `ResetObject` は ID 1 のバウンドの
レコードを、差し替えたパスの上端に合うよう書き換える**（実測。`LayerElevation` バウンドで
`fOffset` が書き換わった）:

| 差し替えたパス | `ResetObject` 後のパス | ID 1 の `fOffset`（差し替え前は 5500） |
| --- | --- | --- |
| 0 長 | 0 長のまま | **2500**（解決Z 2500 ＝ 挿入点） |
| 1 ULP | 1 ULP のまま | **2500.0000000000005** |
| 100 | 100 のまま | **2600** |

**つまり差し替え経路では、パスが正で、バウンドが従う**（作るときの経路と逆向き）。
バウンドに追従させたい部材を `SetCustomObjectPath` で直すと、**その部材は以後その階に
追従しなくなる**。

これは「作り直しは最初の `ResetObject` のときだけ」だからではない——**パスに触らず
バウンドだけを書き換えて 2 度目の `ResetObject` を呼ぶと、新しい解決Zで作り直される**
（ID 1 の `fOffset` を 5500 → 4500 にしたら、パスは `3000` → `2000` になった）。
温存させているのは**差し替え経路そのもの**である。

- 【別 issue】**`_Story` バウンドでも同じ書き換えが起きるか**は未確認（上の表は
  `LayerElevation` バウンドでの実測）。
  [issue #63](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/63) へ
  切り出した。

#### 2 つの経路を、最初の `ResetObject` より前に混ぜない

**まだ一度も `ResetObject` を通していないオブジェクト**のパスを `SetCustomObjectPath` で
差し替えてから `ResetObject` を呼ぶと、作り直しはされるが**長さが足りない**:

| 作るときに渡した長さ | 差し替えた長さ | `ResetObject` 後 | 解決済みの長さは 3000 |
| --- | --- | --- | --- |
| 100 | 0 | **2900** | 100 足りない |
| 500 | 0 | **2500** | 500 足りない |
| 100 | 100 | 3000 | 合っている |

3 点とも **`解決済みの長さ − (作るときに渡した長さ − 差し替えた長さ)`** で説明が付く
（バウンドのレコードはこのとき書き換わっておらず、ID 1 は 5500 のままだった。
にもかかわらず端点の絶対Zは 5400 / 5000 になる＝**バウンドと実体がずれる**）。
**作る → バウンドを書く → `ResetObject`、の間に差し替えを挟まないこと。**

### 階やレベルを指すバウンドは `eStoryObjectBound_Story` で書く（`LayerElevation` では跨がらない）

[issue #56](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/56) で
**実機確認済み**（VW 2026 / mac。実際に事故が起きたモデルで
`probes/runtime/story-bound-cross-story/` を走らせた。以下の値は実行ログそのまま）。

> **帰属についての訂正**（[issue #59](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/59)）:
> 当初ここには「**これが**柱 46 本が長さ 0 になる事故の原因である」と書いていたが、
> **事故を起こしたプラグインは `LayerElevation` で書く経路を持っていない**（最初から
> `fBound = eStoryObjectBound_Story`）。下記の機構は**実測として正しく、再現もする**が、
> **あの 46 本の原因がこれだとは言えない**。以下は「`LayerElevation` で書くとこうなる」
> という機構の記述として読むこと。

#### `EStoryObjectBound` の 3 つの値は「何を見るか」が違う

| `fBound` | 何に解決されるか | `fLayerLevelType` | `fBoundStory` |
| --- | --- | --- | --- |
| `eStoryObjectBound_LayerElevation` | **そのオブジェクトが乗っているレイヤの高さ** | **見ない** | **見ない** |
| `eStoryObjectBound_LayerWallHeight` | 同上（レイヤ設定の壁高） | **見ない** | **見ない** |
| `eStoryObjectBound_Story` | **`fBoundStory` で選んだ階の、`fLayerLevelType` のレベル** | 見る | 見る（0=自階 / 1=上階 / −1=下階） |

どの場合も `fOffset` は最後に足される。

`LayerElevation` が `fLayerLevelType` を見ないことは、**同じレベル種別の指定を container の
レイヤだけ変えて解かせる**と一目で分かる（`GetStoryObjectDataBoundHeight`。1階=612 /
2階=3571、`FL` の階内相対Z=0、`耐力壁`・`横架材天端`=−40 のモデル）:

| container のレイヤ | 種別 `耐力壁` | 種別 `FL` | 種別 `横架材天端` |
| --- | --- | --- | --- |
| `1-耐力壁`（572） | 572 | **572** | 572 |
| `1-FL`（612） | 612 | 612 | **612** |
| `1-横架材天端`（572） | 572 | **572** | 572 |

**行（レイヤ）を変えると全部動き、列（レベル種別）を変えても何も動かない。**
同じ container のまま `fBound` を `Story` に替えると、逆に**レイヤに依らず**
`耐力壁=572 / FL=612 / 横架材天端=572` で一定になる。

`fBoundStory` も同じで、`LayerElevation` のままでは 0 / 1 / −1 のどれを書いても
解決結果は変わらない（実測。下表は container=`1-耐力壁` での総当たり）:

| `fBound` | `fBoundStory` = 0 | = 1 | = −1 |
| --- | --- | --- | --- |
| `LayerElevation`（種別 `FL` / `耐力壁`） | 572 / 572 | **572 / 572** | **572 / 572** |
| `LayerWallHeight`（同上） | 572 / 572 | 572 / 572 | 572 / 572 |
| `Story`（種別 `FL` / `耐力壁`） | 612 / 572 | **3571 / 3531** | 572 / 572 |

#### 正しい書き方は VW 自身に聞ける

`GetStoryBoundChoiceStrings`（OIP のポップアップに出る選択肢）を
`GetStoryBoundDataFromChoiceString` で構造体へ戻すと、**VW がその選択肢をどう表現して
いるか**がそのまま読める。実測（下階＝1階）:

| 選択肢の文字列 | 復号した `SStoryObjectData` | 解決Z |
| --- | --- | --- |
| `レイヤの高さ` | `{LayerElevation, 0, "", 0}` | 572 |
| `壁の高さ（レイヤ設定）` | `{LayerWallHeight, 0, "", 0}` | 572 |
| `FL` | `{Story, 0, "FL", 0}` | 612 |
| `横架材天端` | `{Story, 0, "横架材天端", 0}` | 572 |
| `横架材天端 [上階]` | `{Story, **1**, "横架材天端", 0}` | 3531 |
| `FL [上階]` | `{Story, **1**, "FL", 0}` | 3571 |
| `基礎天端 [下階]` | `{Story, **−1**, "基礎天端", 0}` | 400 |

**階やレベルを指す選択肢はすべて `fBound = eStoryObjectBound_Story`。**
`LayerElevation` の選択肢は「レイヤの高さ」ただ 1 つで、`fLayerLevelType` は**空文字**
——「レベル種別を書いた `LayerElevation`」という組み合わせは、VW 自身は一度も作らない。

> **迷ったらここから引く。** フィールドを推測で埋めるより、
> `GetStoryBoundChoiceStrings` → `GetStoryBoundDataFromChoiceString` で正解を取り出し、
> `GetStoryObjectDataBoundHeight(data, hContainer)` で**オブジェクトを作らずに**
> 解決先を検算するほうが速くて確実。

#### 上下端とも `LayerElevation` で書くと 0 長へ潰れる（機構）

上下端とも `LayerElevation` で書くと、レベル種別にも階にも関わらず**両端が同じ
「レイヤの高さ」へ解決される**。残るのは `fOffset` の差だけなので、
**offset が同じなら長さがちょうど 0 になる**。実測（レイヤ `1-耐力壁`＝572 に置いた
構造材 PIO。`ResetObject` 後のパスを読み戻した）:

| 下端 | 上端 | 解決Z（下/上） | `z1−z0` |
| --- | --- | --- | --- |
| `{LayerElevation, 0, FL, 0}` | `{LayerElevation, **1**, 耐力壁, 0}` | 572 / 572 | **0（潰れる）** |
| `{LayerElevation, 0, FL, 0}` | `{LayerElevation, **0**, 耐力壁, 0}` | 572 / 572 | **0（潰れる）** |
| `{LayerElevation, 0, FL, −40}` | `{LayerElevation, 1, 耐力壁, 0}` | 532 / 572 | −40 |
| `{LayerElevation, 0, FL, −40}` | `{**Story**, 1, 耐力壁, 0}` | 532 / **3531** | **−2999（正常）** |

上の 2 行は `fBoundStory` だけが違うのに**結果が 1 ビットも違わない**——
`fBoundStory` が `LayerElevation` では死んでいることの直接の証拠である。
4 行目は上端の `fBound` を `Story` に替えただけで、正しく階を跨いだ。

**したがって直し方は「階やレベルを指す指定は `fBound = eStoryObjectBound_Story` で
書く」**。`SetObjectStoryBound` は `LayerElevation` ＋ レベル種別という組み合わせも
**`true` を返して受け取り、`GetObjectStoryBound` で書いたとおりに読み戻せる**ので、
戻り値でも読み戻しでも誤りに気付けない。**気付けるのは `GetObjectBoundElevation`
（または `GetStoryObjectDataBoundHeight`）で解決結果を読んだときだけ**。

- **書いた後は必ず解決結果を読む。** 上下端の `GetObjectBoundElevation` が一致していたら
  その部材は 0 長になる。
- **「階内相対Zが各階で一致している」モデルほど当たりやすい**が、原因は相対Zの一致では
  ない。ストーリレイヤテンプレートから階を作れば同名レベルの階内相対Zは揃うので、
  そういうモデルで目立って見えるだけで、**`LayerElevation` で書いた時点で階は跨げて
  いない**（相対Zが揃っていなくても、offset が同じなら同じように潰れる）。
- **【推定】解決できないレベル種別を指すと「レイヤの高さ」へ落ちる。** `{Story, −1, FL}`
  は 572（＝container のレイヤ高さ）になった——下階（基礎）に `FL` が無いため。
  データ点が 1 つなので推定に留める。

### 同じオブジェクトに 2 本のバウンドを書く（ID 0 / 1。階だけが違っても独立に解決される）

[issue #59](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/59) で
**実機確認済み**（VW 2026 / mac。`probes/runtime/story-bound-two-ids/` を事故のモデルで
走らせた。1階=612 / 2階=3571、`耐力壁` の階内相対Z=−40、`FL`=0。以下の値は実行ログそのまま）。

#### 結論: 消えない・混ざらない・書く順も効かない

レイヤ `1-耐力壁`（572）に置いた構造材 PIO へ、**バウンド ID 0 と 1 に 1 本ずつ**
`eStoryObjectBound_Story` を書き、`GetObjectBoundElevation` と `ResetObject` 後のパスを読んだ:

| | ID 0 に書いたもの | ID 1 に書いたもの | 解決Z（ID0 / ID1） | `ResetObject` 後の `z1−z0` |
| --- | --- | --- | --- | --- |
| A | `{Story, 0, 耐力壁, 0}` | `{Story, **1**, 耐力壁, 0}`（**階だけが違う**） | **572 / 3531** | **2959（正常）** |
| B | `{Story, 0, 耐力壁, 0}` | `{Story, 1, 耐力壁, **−739.75**}` | 572 / 2791.25 | 2219.25 |
| C | `{Story, 0, 耐力壁, 0}` | `{Story, 1, **FL**, 0}` | 572 / 3571 | 2999 |
| D | `{Story, 0, 耐力壁, 0}` | `{Story, 0, 耐力壁, 0}`（**完全に同一**） | 572 / 572 | **0（潰れる）** |
| E | A と同じ形で、**ID 1 を先に**書く | | 572 / 3531 | 2959 |

- **`fBoundStory` だけが違う 2 本の `_Story` バウンドは、両方そのまま保持され、
  それぞれ独立に解決される**（A）。`GetObjectStoryBoundsCount` は 2 を返し、
  `GetObjectStoryBoundsAt` は 0 と 1 を返し、`GetObjectStoryBound` は書いたとおりに
  読み戻る。**片方が消える・2 本が「同じ 1 つ」として畳まれる、ということは起きない。**
- **書く順も効かない**（E。先に ID 1 を書いても結果は A と同じ）。
- 完全に同一のレコードを 2 本書いても**2 件として保持される**（D。件数=2、ID も 0 と 1 で
  並ぶ）。ただし解決結果が同じ 572 なので**パスは 0 長へ潰れる**——潰れているのは
  「バウンドが 1 本に畳まれたから」ではなく、**上下端が同じ絶対Zへ解決されたから**である
  （上記「高さ・実体を最終的に決めるのは…」4）。
- **したがって「2 本の `_Story` バウンドを書くと片方が消える」という筋で長さ 0 を
  説明することはできない。** この経路でのバウンドの扱いは**白**である。

#### バウンド ID は 0 と 1。`ID 0 がパスの始点`、`ID 1 が終点`

- **新規に作った構造材 PIO はバウンドを 1 つも持たない。** `CreateCustomObjectPath` 直後は
  `HasObjectStoryBounds=false` / 件数 0 で、`ResetObject` を挟んでも増えない
  ——**VW が勝手に既定のバウンドを作ることはない**。並ぶのは `SetObjectStoryBound` で
  書いた ID だけ。
- **`ResetObject` はパスの始点を ID 0 の解決Zに、終点を ID 1 の解決Zに置く。** A では
  挿入点が `(0,0,572)`（＝ID 0 の解決Z）になり、端点の絶対Z（挿入点＋パスZ）は
  `[0]=572`（ID 0）/ `[1]=3531`（ID 1）だった。**下から上へ描くパスなら ID 0 が下端**。
  - この対応は #56 の実行ログとも合う。あちらのプローブは逆に「ID 0 ＝上端」のつもりで
    書いており、そのとき `ResetObject` 後のパスが `−2999`（＝始点のほうが高い）に
    なっていた。**2 回の実行が同じ規則で説明できる**ので、規則は「上端/下端」ではなく
    **「ID 0 → 始点 / ID 1 → 終点」**と読むのが正しい。
- **`kPIOGenericStoryLevelBoundID`（−3）へは書けない。** `{Story, 1, 耐力壁, 0}` を
  この ID へ `SetObjectStoryBound` すると **`false` を返し**、`HasObjectStoryBounds` も
  false のまま・件数 0 のままだった。`ISDK.h` のコメント（「2017 年に足されたストーリ
  レベル対応を使う parametric 用の Story boundID」）に釣られてこの ID を使わないこと。
  **構造材 PIO のバウンドは 0 と 1 で扱う。**

#### `ResetObject` 後のパスが 0 長になるのは 2 通り（実測）

1. **上下端の解決済み絶対Zが一致したとき**（上表 D。`572 / 572` → `z1−z0 = 0`）。
2. **バウンドが 1 本も無いまま `ResetObject` を呼んだとき。** バウンドを書かずに
   `(0,0,0)→(0,0,2959)` のパスで作った構造材 PIO を `ResetObject` すると、パスは
   `(0,0,0)→(0,0,0)` になった（`−3` への書き込みが `false` で弾かれた回でも同じ）。
   **つまり `SetObjectStoryBound` が効いていないだけでも、実体は 0 長になる。**

> **書いたら数えて読む。** `SetObjectStoryBound` の戻り値は `true` でも、**その ID が
> 実際に並んでいるか**は `GetObjectStoryBoundsCount` / `GetObjectStoryBoundsAt` で、
> **解決結果が上下で違うか**は `GetObjectBoundElevation` で確かめる。この 2 つを見れば、
> 上の 1 と 2 のどちらで潰れるのかを `ResetObject` の前に弾ける。

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
| 【ヘッダ根拠】`AssociateLayerWithStory(layer, story)` / `SetLayerLevelType(layer, levelType)` | **既にあるレイヤを階へ結び付ける / レベル種別を付け替える。** `AddStoryLevel` で生やす以外の道がここにある（`ISDK.h`。実機未確認） |
| `HasObjectStoryBound(h, id)` / `GetObjectStoryBound` / `SetObjectStoryBound` / `DelObjectStoryBound(h, id)` / `DelObjectStoryBounds(h)` | オブジェクトのバウンドを ID 単位で読み書き・削除する |
| `GetObjectStoryBoundsCount(h)` / `GetObjectStoryBoundsAt(h, index)` | **実際に並んでいるバウンド ID を数えて引く。** 書けたかどうかはこれで確かめる（上記） |
| `GetObjectBoundElevation(h, id)` | その ID のバウンドを**解決した絶対Z**。潰れの検知はここ |

落とし穴が 3 つ（すべて実測）:

- **`CreateStory` はレイヤを 1 枚も作らない。** `true` を返し `GetNumStories()` も増えるが、
  できるのは**レベルの無い空の階**で、`ForEachLayerN` からは見えない。階のハンドルは
  `GetStoryOfLayer` 経由でしか取れないので、**`CreateStory` だけで作った階には手が届かない**。
  レベルを足すには `AddStoryLevel`（`Story Level` 系）を使う。
- **`Story Layer Template` と `Story Level` は別系統。** `GetNumStoryLayerTemplates` /
  `CreateStoryLayerTemplate` の一群と、`GetNumStoryLevelTemplates` /
  `CreateStoryLevelTemplate` / `AddStoryLevel` … の一群がある。階へレベルを生やすのは後者。
- **`GetLayerLevelTypeName` / `GetStoryLayerTemplateInfo` / `GetStoryLevelTemplateInfo` の
  添字は 1 始まり。** 添字 0 は無効（前者は空文字、後者は `false`）で、件数 N に対して
  有効なのは 1〜N。0 始まりで回すと**末尾の 1 件を毎回取りこぼす**。
  一覧は**名前順**に並ぶので、`Create*Template` が返す `index` は挿入時点での整列位置に
  過ぎない（2 本続けて足すと 1 本目が押し下げられ、どちらも `index=1` を返しうる）。

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
