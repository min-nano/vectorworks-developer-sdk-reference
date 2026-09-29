# 調査の作法

**CI が全て緑でも、実描画（高さ・傾き・スタイル・PIO の挙動）は検証できない。**
実機（ローカルの VectorWorks）で確かめるしかない領域を、最少の往復で切り分けるための作法。
どれも実際の調査で確立し、外したときに往復を無駄にしたものばかり。

## 書く・読む

- **書いたパラメータは読み戻す。** 名前が違えば setter は黙って無視する
  （[Parametric Objects](Parametric%20Objects.md)）。
- **setter の戻り値を信用しない。** 書けなくても true を返す族がある
  （`SetViewportLayerStackingOverride`・`SetUseDocumentClassVis`）。**読み戻しで確かめる**
  しかない。同じ形の setter に当たったら、まず GUI で作った実物を読んで「読める形式」と
  「書けるか」を切り分ける（[Layers and Stories](Layers%20and%20Stories.md)）。
- **「書けたのに絵が変わらない」＝「その欄は効かない」ではない。** PIO の欄は作り直しの
  ときに読まれるので、まず `ResetObject` を挟んだかを疑う。
- **順序を疑う。** 半分だけ直らないときは、置き場所を探し回る前に**書く順番を並べ替えて
  みる**（実例: 凡例の枠が合わなかった原因は「中身を変えるのが作り直しより後だった」という
  ただそれだけだった）。

## 測る・比べる

- **数えるより測る。** 「指定した値」と「図面の実測値」を突き合わせれば、ずれが平行移動
  なのかスケールなのか回転なのかが 1 本のサンプルで分かる。
- **症状が同じまま対処を重ねない。** 同じ症状に対処を 3 回続けて外したら、対処の方向では
  なく**前提**（＝入力と図面のどちらがずれているか）を実測で確かめる。1 本の両端の座標と
  長さを測るだけで、長さが一致して位置だけ違えば描画側、長さも違えば入力側、と確定する。
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
  - **落とし穴: 古い写しを拾うことがある。** 連続寸法（型 86）は中に 2D 表現のグループ
    （型 11）を持ち、**そこだけ引き直される前の文字が残っていることがある**
    （[issue #156](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/156)
    で実際に踏んだ）。**直線寸法では信用してよいが、PIO では外接矩形（`GetObjectBounds`）
    と併せて読む**——両方が同じ向きに動いていれば確か。
- **オブジェクトの中身を型で数えるのがいちばん早い。** コンテナの中の節点型
  （`GetObjectTypeN`）を数える 1 行を診断へ出すだけで、正体が割れることがある（実例:
  凡例イメージがビューポートだと分かった）。**中を見ずにヘッダだけで推測していた間は
  ずっと外していた。**
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
