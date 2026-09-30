# 調査の作法

**CI が全て緑でも、実描画（高さ・傾き・スタイル・PIO の挙動）は検証できない。**
実機（ローカルの VectorWorks）で確かめるしかない領域を、最少の往復で切り分けるための作法。
どれも実際の調査で確立し、外したときに往復を無駄にしたものばかり。

## 書く・読む

- **書いたパラメータは読み戻す。** 名前が違えば setter は黙って無視する
  （[Parametric Objects](Parametric%20Objects.md)）。
- **組み込み PIO の universal 名は、当てる前に `GetPluginType` で確かめる。**
  `gSDK->GetPluginType(名前, 種別)` は図形を 1 つも作らずに「その綴りのプラグインが在るか」と
  「オブジェクト（`kVSPluginObject` ＝ 2）か」を返すので、**綴りの候補を並べて総当たりする
  のが安い**（[#181](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/181)
  では 18 候補のうち 10 件が当たり、8 件が `false`）。綴りは英語の UI 名そのままで、
  **空白を詰めると当たらない**（`Break Line` は在るが `BreakLine` は無い）。候補の起こし方は
  `SDKLib/Include/Kernel/API/MiniCadHookIntf.h` の `kInternalID_*`（定数名がほぼ綴りになって
  いる）。
  - **プラグインフォルダを舐めても組み込みは出てこない。**
    `ForEachFilePathInPluginFolderN` は 2450 ファイルを返したが、`.vso` / `.vst` の該当は
    **0 件**だった（組み込み PIO はフォルダ内の単体ファイルとして置かれていない）。
    **名前探しにこの口を当てにしない。**
- **setter の戻り値を信用しない。** 書けなくても true を返す族がある
  （`SetViewportLayerStackingOverride`・`SetUseDocumentClassVis`）。**読み戻しで確かめる**
  しかない。同じ形の setter に当たったら、まず GUI で作った実物を読んで「読める形式」と
  「書けるか」を切り分ける（[Layers and Stories](Layers%20and%20Stories.md)）。
- **`SetObjectName` は `0`（成功）を返しながら 63 文字で黙って切る。** 図形の名前を
  「小さな記憶」に使う（走行をまたいで値を残す）ときは、**書いた後に読み戻して長さまで
  確かめる**。実測では 64 文字目以降が落ち、`GetObjectName` はちょうど 63 文字を返した
  （[#181](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/181) で
  実際に踏み、プローブが 1 巡まるごと無駄になった）。**載せるのは短い指紋
  （ハッシュ）にして、値そのものはログへ出す**のが確実である。
- **「書けたのに絵が変わらない」＝「その欄は効かない」ではない。** PIO の欄は作り直しの
  ときに読まれるので、まず `ResetObject` を挟んだかを疑う。
- **`ResetObject` を挟んでも変わらないなら、次は「どこから値を採るか」を切り替える欄を
  探す。** 同じ種別の中に**モード欄**があって、いま選ばれていないモードの欄は
  **黙って読まれない**ことがある。**その欄は死んでいるのではなく、眠っている。**
  探すときは**ポップアップの選択肢を全部採って読む**のが速い（`GetParamChoices` /
  `GetParamLocalizedChoices`。[Parametric Objects](Parametric%20Objects.md)
  「選択肢を採る経路」）——項目名がそのままモードの名前になっている。
  **実例**: 構造材の `MajorDepth` は書けて読み戻せるのに断面が 1mm も動かなかったが、
  効かない理由は `MemberType`（材質）が「スチール」で、そこでは断面がカタログ
  （`ProfileShape` ＋ `ProfileSize` の綴り）から来ていたためだった。
  「コンクリート」「木」へ倒すと**同じ欄がそのまま断面になる**（#169）。
  **4 択を採るまで 16 個体を無駄に振った**——モード欄を先に疑えば 1 往復で済んだ。
- **読み戻しの一致は「効いた」の証拠にならない。** さらに悪いことに、**書いた値を
  写すだけの読み取り専用の表示欄**が隣に居ることがあり、そこが追随するのを見て
  「効いた」と誤読しうる（実例: 構造材の `B` / `B1` / `D` / `D1` は寸法 4 欄の鏡で、
  絵が 1mm も動いていなくても書いた値を返す。#169）。
  **効いたかどうかは、描かれた図形か生成物を測って決める**（PIO なら
  `GetCustomObjectProfileGroup` の外接のように、**パスの長さが混ざらない出力**を選ぶ）。
- **順序を疑う。** 半分だけ直らないときは、置き場所を探し回る前に**書く順番を並べ替えて
  みる**（実例: 凡例の枠が合わなかった原因は「中身を変えるのが作り直しより後だった」という
  ただそれだけだった）。

## 測る・比べる

- **数えるより測る。** 「指定した値」と「図面の実測値」を突き合わせれば、ずれが平行移動
  なのかスケールなのか回転なのかが 1 本のサンプルで分かる。
- **何も描いていない PIO の `GetObjectBounds` は `true` を返して「反転した空矩形」を返す。**
  `left` = `bottom` = `+DBL_MAX`（1.7976931348623157e308）、`right` = `top` = `-DBL_MAX` が
  入るので、**幅・高さを計算すると `-inf` になる**。**戻り値では空を判別できない**ので、
  外接を測る前に `right > left` を確かめる。実測はパスを与えずに作った
  `StructuralMember` と `Linear Material`（[#181](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/181)）。
  「大きさ 0」と「外接が無い」を取り違えると、**存在しない巨大な図形を追いかけることに
  なる**。
- **症状が同じまま対処を重ねない。** 同じ症状に対処を 3 回続けて外したら、対処の方向では
  なく**前提**（＝入力と図面のどちらがずれているか）を実測で確かめる。1 本の両端の座標と
  長さを測るだけで、長さが一致して位置だけ違えば描画側、長さも違えば入力側、と確定する。
- **外接は「形」を教えてくれない——輪郭は頂点で読む。** 外接（`GetObjectBounds`）が
  指定どおりでも、中身がその形だとは限らない。構造材の断面は外接が
  「主幅 × 主高さ」に一致するので**矩形だと読んでしまったが、実際は I 形**で、
  断面群の子の**12 頂点を 1 点ずつ出して初めて**「効かないと思っていた 2 欄は板厚だった」
  と分かった（[Findings「副幅・副高さは板厚」](Parametric%20Objects.md)。#169 → #173）。
  **「この欄は効かない」と書く前に、外接ではなく頂点を見る。**
  ポリゴン・ポリラインなら `VWPolygon2DObj(handle)` の `GetVertexCount` /
  `GetVertexPoint` で引ける。
- **3D があることを 2D の測り値で判定しない。** 同じ構造材で、断面群の輪郭も外接も
  正しいのに**型 84（`kCSGTree`）だけが作られていない**——つまり平面図には出るのに
  3D が無い——状態が起きた。`ResetObject` は `true`、読み戻しも一致、群の外接も正しい。
  **立体が要るものを調べるなら `GetObjectCube` の高さか、子の型を数える。**
- **「壊れた」と「意図と違う」を分けて記録する。** 同じ入力の範囲外でも、3D が
  作られない個体と、作られるが寸法が違う個体があった。**前者だけを見て「壊れる条件」を
  書くと、後者（黙って別物ができる）を見落とす。** 表には必ず「実体の有無」と
  「実測寸法」の両方を並べる。
- **同じ文書に「正解」を並べて差分を取る。** UI で手作業したものと SDK が作ったものを
  並べて全欄を比較すると、「欄が足りない」のか「書き込み経路が違う」のかが分かる。
  **推測を重ねるより、正解を 1 つ用意して比べるほうが速い。**
- **API の無い設定は「UI で手作業したもの」と見比べて突き止める。** 読み取り専用の
  ダンプ（パラメトリックレコード・付いているレコード・オブジェクト変数・補助オブジェクト）
  を設定の前と後で 2 回取り、diff を見る。`ModifySlab` の噛み合わせも断面ビューポートの
  範囲も凡例のフィルタも、この見比べで決着した（実装例: ホームズ君プラグインの
  `scripts/vw-dump-pio-fields.py`）。
- **「絵で読める大きさか」は目視に頼らず測れる——中に描かれている文字図形を引く。**
  寸法や PIO は、描いた結果を**子の図形として持っている**。深さ数段まで潜って最初の
  型 `10`（`kTextNode`）を拾い、`GetTextSize(text, 1, charSize)` で 1 文字目の大きさを
  読めば、**紙の上で何 pt で出るかが数値で分かる**（容れ物の縮尺で割る）。

  ```cpp
  // container の中から最初の文字図形を探し、その大きさ（図面上の mm）を返す。
  bool FindDrawnTextSize(MCObjectHandle container, int depth, double& outMM)
  {
      if (container == nil || depth > 4) return false;
      for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nil;
           m = gSDK->NextObject(m))
      {
          const short type = gSDK->GetObjectTypeN(m);
          if (type == 0) break;                      // kTermNode（walk の終端）
          if (type == kTextNode)
          {
              WorldCoord size = 0;
              gSDK->GetTextSize(m, 1, size);
              outMM = static_cast<double>(size);
              return true;
          }
          if (FindDrawnTextSize(m, depth + 1, outMM)) return true;
      }
      return false;
  }
  ```

  **実機の絵と突き合わせて確かめてある**（[issue #161](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/161)）。
  1/50 のビューポートの注釈へ作り方を変えた寸法を 7 行ならべ、この物差しが「紙 6pt」と
  言った行だけが絵で読め、「紙 0.12pt」と言った行は寸法線だけだった——**7 行とも一致**。
  2 巡目では「紙 12pt」と言った 1 行だけが絵でも 2 倍に見えた。
  - **これが効くのは、`ISDK` の読み戻しが当てにならない場面**である。#157 は
    「`GetTextStyleByClass` / `GetTextStyleRef` / `ovDimTextStyle` / `ovDimFontSize` の
    4 つとも同じなのに絵が違う」で行き止まりになったが、**描かれている文字を測れば
    その場で割れた**（[Dimensions](Dimensions.md)）。
  - **落とし穴: レイアウト（プロファイルグループ）が混ざる。** データタグ・レベル
    オブジェクト・図面ラベルのように**レイアウトをプロファイルグループで持つ PIO** では、
    `FirstMemberObj` の再帰が**描いた図形だけでなくレイアウトそのものも拾う**。
    そこに入っているのは「**レイアウトへ与えた値**」（縮尺を掛ける前の、紙の上の値）
    なので、**文字を 1 つも描いていない個体を測ると、レイアウト側の値が「描かれた文字」
    として返ってくる**。[#177](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/177)
    では、これで「縮尺が掛からない」という**存在しない現象**を 7 回の走行にわたって
    追いかけた（実際には `Title` が空で文字が描かれていなかっただけ。
    [Drawing Labels](Drawing%20Labels.md)）。
    - **外接（`GetObjectBounds`）を必ず併せて読む。** 大きさ `s` の文字が本当に描かれて
      いれば**外接の高さは `s × 1.5` になる**（実測。フォント依存だが、この比は安定して
      いる）。**高さ `0` は「文字が無い」**であって「小さい文字がある」ではない。
    - **レイアウトのハンドル（`GetCustomObjectProfileGroup`）と突き合わせて、拾った
      テキストがどちら側のものかを印で出す。** 走査の結果を「最初の 1 件」で代表させず、
      **件数と出どころごと**ログへ出せば、取り違えはその場で目に入る。
  - **落とし穴: 古い写しを拾うことがある。** 連続寸法（型 86）は中に 2D 表現のグループ
    （型 11）を持ち、**そこだけ引き直される前の文字が残っていることがある**
    （[issue #156](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/156)
    で実際に踏んだ）。**直線寸法では信用してよいが、PIO では外接矩形（`GetObjectBounds`）
    と併せて読む**——両方が同じ向きに動いていれば確か。
- **オブジェクトの中身を型で数えるのがいちばん早い。** コンテナの中の節点型
  （`GetObjectTypeN`）を数える 1 行を診断へ出すだけで、正体が割れることがある（実例:
  凡例イメージがビューポートだと分かった）。**中を見ずにヘッダだけで推測していた間は
  ずっと外していた。**
- **ただし、数える型を決め打ちしない。** 「描かれた子」を型 5 / 21 / 84 だけと決めて
  数えたせいで、網から漏れた型 11（`kGroupNode`）の子が**「消えた」と読めてしまった**
  ことがある（[#166](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/166)。
  実際には何も消えていなかった）。**置き石以外を全部数え、型の内訳ごと出す。**
  決め打ちの網は、漏れたときに**いちばん間違えやすい形**で嘘をつく。
- **1 個体に複数の欄を順に書いて測ると、原因が 1 段前へずれる。** 「A を書く → 測る →
  B を書く → 測る」で B のときに変化が出ても、それは A の遅れて出た結果かもしれない。
  **欄ごとに個体を分ける**か、少なくとも**1 段ごとに内訳を出して差分で読む**
  （同じ [#166](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/166)
  で、表示欄が消した子を、直前に振っていた `AttributesMode` のせいだと読んだ）。
- **「ヘッダに書いていないから分からない」で止めない——SDK は実装ソースを同梱している。**
  `SDKLib/Source/VWSDK/` に VWFC（`VWFC::VWUI` / `VWFC::Tools` …）の `.cpp` が丸ごと入って
  いるので、`sdk-grep` / `sdk-ls` はそこも見る。「この関数が false を返す条件は？」は
  たいていその場で確定する（実例: `VWImagePopupCtrl::CreateControl` が **`return false`
  のスタブ**だと分かり、実機での条件出しが丸ごと不要になった。
  [Layout Dialogs](Layout%20Dialogs.md)）。**宣言だけを読んで推測を重ねる前に、実装を
  探す。**
- **`SDK Index/` で見つからなくても「無い」と決めない——マクロの中の宣言は索引に載らない。**
  旧 API の一部は `Include/Kernel/API/APIBase.Legacy.h` の中で `APP_API_FUNCTION(...)` /
  `APP_API_PROCEDURE(...)` の**引数として**書かれており、索引を作るパーサはそれを宣言として
  読み取れない。**実例**: 断面ビューポートの作り方を調べていて、索引には 1 行も無い
  `GS_CreateSectionLineInstance(CallBackPtr, MCObjectHandle inSectionView)` と
  `GS_IsSectionLineLinkedToViewport(CallBackPtr, MCObjectHandle inSectionLine)` が
  `sdk-grep` で出た（[issue #151](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/151)）。
  **索引で当たりが無かったら、諦める前に `sdk-grep` で同じ語を引き直す**——
  索引は「速い入口」であって「SDK の全部」ではない。
- **ただし `APP_API_FUNCTION` / `APP_API_PROCEDURE` の 12 本は、宣言があっても呼べない**
  （「未公開の API」＝ヘッダのコメントいわく *SDK APIs waiting to be published for
  external use*）。マクロが作るのは `CB_<名前>` を呼ぶ**インライン**の `GS_<名前>` だけで、
  **その `CB_` シンボルは `SDKLib/LibMac/Release/libVWSDK.a` に入っていない**——
  `libVWSDK.a` にある `CB_` は**メモリ確保系の 8 本だけ**（`CB_NewHandle` 等）で、
  残りは実体が無い。だから呼ぶと**リンクで落ちる**:

  ```
  Undefined symbols for architecture arm64:
    "_CB_CreateSectionLineInstance", referenced from: …
  ld: symbol(s) not found for architecture arm64
  ```

  **`compile`（`-fsyntax-only`）では通ってしまう**ので、構文チェックだけでは気付けない
  （実際に、プローブの構文チェックが緑のままプラグインのビルドで落ちた。
  [run](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/36519158213)）。
  該当するのは次の 12 本で、**この名前を見かけたら実装の当てにしない**:
  `GS_CreateSectionLineInstance` / `GS_IsSectionLineLinkedToViewport` /
  `GS_IsDetailCalloutLinkedToViewport` / `GS_DisplayImagePopup` /
  `GS_{Begin,Cancel,Draw,End,Update}FreehandInteractive` /
  `GS_GetFreehandInteractivePoly` / `GS_SetFreehandInteractivePen{,Style}`。
  （`GS_DisplayImagePopup` が呼べないことは、`VWImagePopupCtrl::CreateControl` が
  `return false` のスタブだったこと——[Layout Dialogs](Layout%20Dialogs.md)——と
  同じ話の裏表である。）
  **見分け方**: 宣言が `APIBase.Legacy.h` の `APP_API_*` の中にあるなら呼べない。
  `APIBase.Legacy.Defs.h` に `extern "C" … GS_…(CallBackPtr, …);` として並んでいる
  ふつうの旧 API は、`libVWSDK.a` に実体があるので呼べる。
- **「絵に出ているか」は目視に頼らず、描かれている図形を測る。** オブジェクト変数を
  読み戻して「書けた」と確かめても、**絵が古いまま**のことがある（実例: 連続寸法の中の
  直線寸法は `ovDimFontSize` が書き換わっても文字の大きさが変わらなかった。
  [Dimensions](Dimensions.md)）。**中を歩いて文字図形（型 `10`）を見つけ
  `GetTextSize` で 1 文字目を測る**・**外接矩形の高さを見る**——このどちらかを
  プローブに 1 行足せば、「値は入ったが絵は変わっていない」をログで判別できる。
  ユーザーへ目視を頼む前に、これを試す。
- **2 つの要因がいつも一致しているなら、入れ替えて双方向で測る。** 「作ったときの
  アクティブレイヤ」と「操作したときのアクティブレイヤ」のように、毎回同じ値になる要因は
  **どちらが効いているかを分けられない**。A（片方だけ変える）と B（もう片方だけ変える）を
  同じ 1 回の実行に並べると、1 往復で決着する（実例: 連続寸法の文字の大きさは
  **繋ぐときの**アクティブレイヤだけで決まると確定した。[Dimensions](Dimensions.md)）。
- **信用できるのはハンドルの生バイト。** タグ付きデータの読み出し API は当てにならない
  ので、探索は 16 進ダンプでやる（[Tagged Data](Tagged%20Data.md)）。
- **1 ULP を追う調査では、プローブの中で予測式を計算しない。** 浮動小数の式は
  **コンパイラが FMA へ縮約して別の値になる**（clang の既定は `-ffp-contract=on`）。
  `v / 25.4 * 25.4 - v` は `fma(v / 25.4, 25.4, -v)` になり、丸めが 1 回減るぶん
  **答えが変わる**——実例: 3531 で `-2.885e-13` と出たが、本当の往復誤差は `-4.547e-13`
  だった（[issue #67](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/67)。
  1 往復を無駄にした）。予測はプローブの外（手元の Python など）で計算して**値を
  書き込む**か、どうしても中で計算するなら中間値を `volatile` の `double` へ落として
  縮約を止める。**プローブが出すべきは実測値だけ**である。
- **仮説に寄せた値ばかりで試さない。** 「予測が当たった値」を並べても仮説は強くならない。
  **予測が割れる値**（2 つの説で答えが違う値）と、**予測が外れるなら外れるはずの値**を
  必ず混ぜる。同じ #67 で、8 点では当たって見えた仮説が、値を散らした 18 点の走査で
  落ちた。
- **数えるだけの走査は「1 ケース 1 文字」にする。** 結果が数通り（残差の ULP 数・
  成否・分類）に収まる調査なら、1 ケースを 1 行ではなく**1 文字**にして 50 文字ずつ
  並べると、**数百ケースが数行に収まる**。全文の明細は「0 でなかったケース」だけに
  絞れば、**規則を目で探せる地図**になる（実例:
  [issue #71](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/71) の
  491 ケース・11.3 秒を 10 行ほどの地図にした。`0000000001111000000000000` のような
  並びを見て初めて「隣り合う値がまとまって出る」が分かった——1 ケース 1 行では
  埋もれていた）。**ログが長いと読まれない**ので、走査の規模は文字数ではなく
  「1 ケースあたりの文字数」で稼ぐ。
- **「何で決まるか」は、その量を厳密に揃えた組で 1 発で割る。** 「A で決まるのか B で
  決まるのか」を数百点の走査から読み取ろうとすると、たいてい読み取れない。**A だけを
  厳密に同じにして B を変えた組**を数家族通せば、**残差が食い違った時点で「A では
  決まらない」が確定する**。実例:
  [issue #73](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/73) で
  「残差は 3 次元長で決まるのか、成分の組で決まるのか」を、**ピタゴラス数**
  （`(1600, 0, 3000)` / `(3000, 0, 1600)` / `(0, 0, 3400)` は 3 次元長が厳密に 3400）で
  割った——**5 家族 15 ケースで済み**、うち 4 家族で食い違って決着した。
  **浮動小数の調査では「厳密に揃う」組を作れるかどうかが要**（整数の三つ組・2 の冪倍など）。
- **1 次元の走査で決まらないものは、格子（2 次元の地図）にする。** 行と列のどちらで
  文字が変わるかが**面として見える**ので、「片方だけが効いている」「両方が効いている」が
  一目で分かる。#73 では `dz` × `dx` の 21 × 21 を 21 行で出し、**`dx = 0` の列（＝鉛直）に
  だけ負符号が無い**ことが目で見えた。
- **26 点で式が当たらないなら、点を増やすより先に「何が決まっているか」を絞る。**
  #71 では、走らせる前に手元で 26 点を整理して (1) 残差は必ず `k × ULP` (2) `k` は仮数で
  決まる (3) 指数には依らないらしい——の 3 つに畳み、**その 3 つを正面から試す群**を
  プローブに置いた。結果、(3) が確定して「危ない値の話は仮数だけの話」に縮み、
  走査の意味がはっきりした。**闇雲に点を増やす走査は、点が増えるだけで終わる。**

- **Windows 版 SDK を引き直しても同じものしか出ない。** `ci-debug` の
  `--platform windows` と `--platform mac` が用意する SDK は、**`SDKLib/` の 961
  ファイルすべてが改行コードを除いて同一**である（CR を落とした集約ハッシュが
  `a859b31735d9572d2b7c967aa3512a19` で一致。ディレクトリ単位でも全 19 群が一致。
  [windows](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/35883156877) /
  [mac](https://github.com/min-nano/vectorworks-developer-sdk-reference/actions/runs/35883177907)）。
  **`Include/OnlyWin` も `Include/OnlyMac` も、どちらの SDK にも両方入っている**
  （13 ファイル / 27 ファイル）。したがって「Windows ではどうなっているか」を
  `sdk-grep` / `sdk-ls` で引き直しても、**mac で引いたのと 1 文字も違わない答えが返る**
  ——`--platform windows` は**中身を変えない**。プラットフォーム差を知りたいなら
  実機で測るしかない（実例: [ファイル・フォルダの識別子](File%20and%20Folder%20Identifiers.md)
  の `SAttributes`）。
  - **比べるときは `tr -d '\r'` を使わない。** macOS の BSD `tr` は UTF-8 でない
    バイトを含むファイルで `Illegal byte sequence` を出して**そのファイルだけ空に
    してしまう**ので、GNU `tr` の Windows 側と比べると**差が無いのに差が出る**
    （実際に 961 ファイル中 6 ファイルで踏み、4 つのディレクトリが「違う」と出た）。
    `perl -0777` で読んで `s/\r//g` すれば両方で同じ挙動になる。

- **PIO が描いた図形は入れ子になっている。** 図面ラベルでは `FirstMemberObj(pio)` の
  直下は群（型 11）で、テキストはその中にある。**1 階層だけ舐めて「テキストが無い」と
  読むと、そこから先の推論が全部ずれる**（[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)
  で 1 往復無駄にした）。走査は再帰で書き、深さも記録する。
- **`GetTextSize` だけでは「紙の上で何 pt か」は決まらない。** 大きさの値は容れ物の
  縮尺が掛かる前のものかもしれず、掛かった後のものかもしれない。**同じ容れ物に
  「紙で何 pt か分かっているもの」を 1 つ置いて、比で測る**——図面ラベルの調査では
  ビューポートの注釈へ「紙で 6pt の寸法」（`ovDimFontSize` ＝ 紙の pt × 25.4/72 × 縮尺。
  [Dimensions](Dimensions.md) で確定済み）を置いて物差しにした。
  - **物差しは「大きさ用」と「外接用」を別に取る。** テキストの外接の高さは大きさの
    1.4〜1.5 倍あり、**書体によってその比が違う**（寸法の文字で 1.44、ラベルの文字で
    1.5）。大きさ同士・外接同士で比べないと、10pt を `10.4167pt` と読む（実際に読んだ）。
- **2 つの要因がいつも一致していると、切り分けたつもりになる。** 図面ラベルの文字の
  大きさでは「容れ物の縮尺」と「アクティブレイヤの縮尺」が 5 走行にわたって常に一致して
  いたため、**どちらが効いているのか決まっていないのに「容れ物だ」と書いてしまった**
  （[#175](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/175)。
  翌走行で否定され、取り消した）。**交差させた対を 1 回の走行で取る**——1/50 の容れ物 ×
  アクティブ 1:1 と、1:1 の容れ物 × アクティブ 1/50。これで 1 回で決まった。
  （同じ教訓は [Dimensions](Dimensions.md) の「双方向で測る」にもある。）
- **同じプローブを作り替えて何度も走らせるときは、先頭コメントに「ここまでに確定した
  こと」と「今回測ること」を書く。** 走行が 5 回を越えると、何が確定して何が仮説なのかが
  自分でも混ざる。確定した項目は測り直さず、コメントへ結論だけ残して落とす。
- **「作った直後」だけを読むと、既に在るものが書き換わったことに気づけない。**
  図面ラベルの図番の採番は、作った直後の値を並べると `1 2 1 3 4 5` と乱数のように見えた
  （[#179](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/179)）。
  **1 本足すたびに「いま在るもの全部」を読み直したら、先頭のラベルが毎回書き換わって
  いることが 1 回の走行で出た**——乱れて見えたのは、**測っていない場所が動いていた**
  からである。**1 つ足すたびに全員を読む**のは行数が増えるだけで、往復は増えない。
- **規則の候補が立ったら、独立した過去の走行に当てて検算する。** #179 で立てた規則
  （「使われていない最小の正整数」＋「注釈の先頭は最後に入れた本を映す」）を、
  [#177](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/177) の
  11 本（`1 2 1 3 4 5 4 6 4 7 4`）へ当てたら**1 本の例外も無く一致した**。
  **別の条件で取った古いログは、ただで手に入る検算用のデータである**——消す前に当てる。
- **「乱れている」で終わらせない。** 同じ手順を繰り返して違う値が出るなら、**繰り返し
  回数を増やして並べる**（#179 では 6〜8 本を 1 フェーズにした）。3 本では乱数に見える
  ものが、6 本並べると規則になる。
- **ワークシート式は、ワークシートを作らずにハンドルへ直接評価できる**
  ——`ISDK::CompileCriteriaExpression(式, 誤りのコールバック, 文脈)` で
  コンパイルし、`ISDK::ExecWSExpression(h, compiled, outResult)` で実行する
  （ラッパーは `VWFC::Tools::WSCriteriaExpression`。`Compile` / `ExecWSExpression` /
  `GetLastError` / `GetLastErrorOffset` が生えている）。**`=` は付けない**
  ——SDK のコメントの例が `"Area()/2"` である。文脈は `eLocal`（ローカライズされた綴り）と
  `eUniversal` を選ぶ。【ヘッダ根拠】
  - **これが「識別子の総当たり」を当て推量から実測へ変える。** コンパイルの失敗は
    `ECriteriaExpressionError` の enum（`InvalIdent` / `InvalRecordRef` / `InvalTypes` /
    `LeftParenExpected` …）で返るので、**「そんな名前は無い」と「名前は在るが値が空」を
    機械的に区別できる**。候補名を 50 本並べて 1 回の走行で仕分けられる。
  - **対照を 1 本入れる。** 存在しないはずの識別子（`T187NOSUCHFUNCTION` のような）を
    必ず混ぜて、「無いときに何が返るか」を同じ走行の中に持っておく。これが無いと
    `InvalIdent` が本当に「無い」の意味なのかを言い切れない。
  - 戻り値は `VWVariant` なので、**型（`GetType`）も一緒にログへ出す**
    ——`eVWVariantType_Empty` と「空文字列」は別物である。
  - 量（長さ・面積・体積など）だけなら `ISDK::ExecQTOFunction` ＋ `EQTOFunction`
    （`Angle` / `Count` / `Length` / `Perimeter` / `Width` / `Height` / `Depth` / `Weight` /
    `Area` / `SurfaceArea` / `ProjectedArea` / `FootPrintArea` / `CrossSectionArea` /
    `SpecialArea` / `Volume` / `ObjectData` / `Thickness` の 17 個）という**列挙できる口**も
    ある。【ヘッダ根拠】

## 運用

- **実機の出力にはビルドの素性を添える。** 「その出力がどのビルドのものか」が分からないと
  切り分けが崩れる——実際に**前のビルドの出力を新しいビルドのものと読んで 1 往復無駄に
  した**。診断ログの先頭にブランチとコミットを書く。
- **役目を終えた計装は消す。** 規約を詰めるための実測コード・一時診断は、確定と同時に
  外す（履歴に残っているので必要なら作り直せる）。
- **事前ガードを置かない。** 本命の操作の前に自前の判定で弾くと、その判定が外れたとき
  **1 つも動かないうえ原因を誤って指す**。本命の操作を唯一の門にし、失敗したときだけ
  理由を引き分ける（[Symbols](Symbols.md)）。
- **平常時に鳴る診断は外す。** 切り分けに役立った検査でも、仕様どおりの動きで鳴るものを
  残すと警報として死ぬ。役目が終わった診断は消し、必要になったら作り直す。
