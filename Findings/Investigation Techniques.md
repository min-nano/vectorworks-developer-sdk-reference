# 調査の作法

**CI が全て緑でも、実描画（高さ・傾き・スタイル・PIO の挙動）は検証できない。**
実機（ローカルの VectorWorks）で確かめるしかない領域を、最少の往復で切り分けるための作法。
どれも実際の調査で確立し、外したときに往復を無駄にしたもの。各作法を確立した実例
（どの issue で何を踏み、何が出たか）は [計測記録](../Metrics/Investigation%20Techniques.md)。

## 書く・読む

- **書いたパラメータは読み戻す。** 名前が違えば setter は黙って無視する
  （[Parametric Objects](Parametric%20Objects.md)）。
- **組み込み PIO の universal 名は、当てる前に `GetPluginType` で確かめる。** 図形を作らずに
  在るか・オブジェクト（`kVSPluginObject` ＝ 2）かを返すので、**綴りの候補を総当たりするのが
  安い**。綴りは英語の UI 名そのままで、**空白を詰めると当たらない**。候補は
  `MiniCadHookIntf.h` の `kInternalID_*` から起こす。**`ForEachFilePathInPluginFolderN` で
  プラグインフォルダを舐めても組み込みは出てこない**（[実測](../Metrics/Investigation%20Techniques.md#組み込み-pio-の-universal-名の総当たり181)）。
- **setter の戻り値を信用しない。** 書けなくても true を返す族がある（`SetViewportLayerStackingOverride`・
  `SetUseDocumentClassVis`）。読み戻しで確かめ、GUI で作った実物を読んで「読める形式」と
  「書けるか」を切り分ける（[Layers and Stories](Layers%20and%20Stories.md)）。
- **`SetObjectName` は `0`（成功）を返しながら 63 文字で黙って切る。** 名前を走行をまたぐ記憶に
  使うなら長さまで読み戻し、**載せるのは短い指紋にして値はログへ出す**（[実測](../Metrics/Investigation%20Techniques.md#setter-の戻り値と名前の切り詰め181)）。
- **「書けたのに絵が変わらない」＝「その欄は効かない」ではない。** まず `ResetObject` を
  挟んだかを疑い、次に**モード欄**を探す——選ばれていないモードの欄は黙って読まれない
  （**死んでいるのではなく眠っている**）。探すときは**ポップアップの選択肢を全部採る**
  （`GetParamChoices` / `GetParamLocalizedChoices`。[Parametric Objects](Parametric%20Objects.md)
  「選択肢を採る経路」）（[実例](../Metrics/Investigation%20Techniques.md#モード欄と読み取り専用の鏡169)）。
- **読み戻しの一致は「効いた」の証拠にならない。** 書いた値を写すだけの読み取り専用の欄が
  隣に居ることがある。**効いたかは描かれた図形か生成物を測って決める**（PIO なら
  `GetCustomObjectProfileGroup` の外接のように、パスの長さが混ざらない出力）。
- **順序を疑う。** 半分だけ直らないときは、置き場所を探す前に**書く順番を並べ替える**。

## 測る・比べる

- **数えるより測る。** 指定した値と図面の実測値を突き合わせれば、平行移動・スケール・回転の
  どれかが 1 本で分かる。
- **何も描いていない PIO の `GetObjectBounds` は `true` を返して「反転した空矩形」を返す**
  （`left` = `bottom` = `+DBL_MAX`、`right` = `top` = `-DBL_MAX`）。**戻り値で空を判別できない**
  ので、測る前に `right > left` を確かめる（[実測](../Metrics/Investigation%20Techniques.md#何も描いていない-pio-の外接181)）。
- **症状が同じまま対処を重ねない。** 3 回外したら**前提**（入力と図面のどちらがずれているか）を
  実測する——1 本の両端と長さを測れば決まる。
- **外接は「形」を教えてくれない——輪郭は頂点で読む**（`VWPolygon2DObj` の `GetVertexCount` /
  `GetVertexPoint`）。**3D があることを 2D の測り値で判定しない**（`GetObjectCube` の高さか子の型）。
  **「壊れた」と「意図と違う」を分けて**、実体の有無と実測寸法を両方並べる
  （[実例](../Metrics/Investigation%20Techniques.md#前提を測る輪郭は頂点で3d-の有無169173)）。
- **同じ文書に「正解」を並べて差分を取る。** API の無い設定は、UI で手作業したものの
  ダンプを設定の前後で取り diff を見る（[実例](../Metrics/Investigation%20Techniques.md#正解と並べて差分を取る)）。
- **描かれている文字を測る**——「絵で読める大きさか」は目視に頼らず、PIO・寸法の中を再帰で
  潜って最初の型 `10` の `GetTextSize` を読む（容れ物の縮尺で割れば紙の pt）。**実機の絵と
  一致を確かめてある**（#161。コードは [計測記録](../Metrics/Investigation%20Techniques.md#描かれている文字を測る161177156)）。
  - **ISDK の読み戻しが当てにならない場面で効く**（#157 の行き止まりがその場で割れた）。
  - **落とし穴: レイアウト（プロファイルグループ）が混ざる。** レイアウトを持つ PIO では再帰が
    レイアウトの値（紙の上の値）も拾い、**文字を描いていない個体ではそれが「描かれた文字」に
    見える**（#177 で存在しない現象を追った）。**外接を必ず併せて読む**（文字が在れば高さは
    大きさの約 1.5 倍、**高さ `0` は文字が無い**）。拾ったテキストの出どころと件数をログへ出す。
  - **落とし穴: 古い写しを拾う。** 連続寸法（型 86）の中の 2D 表現の群には引き直し前の文字が
    残ることがある。**PIO では外接と併せて読む。**
- **中身を型で数えるのがいちばん早い。ただし数える型を決め打ちしない**——置き石以外を全部
  数え、型の内訳ごと出す。**1 個体に複数の欄を順に書いて測ると原因が 1 段前へずれる**ので、
  欄ごとに個体を分けるか 1 段ごとに差分で読む（[実例](../Metrics/Investigation%20Techniques.md#型で数える欄ごとに個体を分ける166)）。
- **「ヘッダに書いていないから分からない」で止めない——SDK は VWFC の実装ソースを同梱している**
  （`SDKLib/Source/VWSDK/`。`sdk-grep` / `sdk-ls` で見る）。**`SDK Index/` に無くても「無い」と
  決めない**——`APIBase.Legacy.h` の `APP_API_*` マクロの中の宣言は索引に載らないので
  `sdk-grep` で引き直す（[実例](../Metrics/Investigation%20Techniques.md#実装ソースと索引の外151)）。
- **ただし `APP_API_FUNCTION` / `APP_API_PROCEDURE` の 12 本は、宣言があっても呼べない**
  （未公開の API。`CB_` の実体が `libVWSDK.a` に無く**リンクで落ちる**。`compile` は通る）。
  **宣言が `APIBase.Legacy.h` の `APP_API_*` の中なら呼べない**、`APIBase.Legacy.Defs.h` の
  ふつうの旧 API は呼べる（[12 本の一覧とリンクエラー](../Metrics/Investigation%20Techniques.md#呼べない-app_api_-の-12-本)）。
- **「絵に出ているか」も描かれている図形を測って判別する**（文字の `GetTextSize`・外接の高さ）。
  目視を頼む前に試す。**2 つの要因がいつも一致しているなら、入れ替えて双方向で測る**
  （[実例](../Metrics/Investigation%20Techniques.md#絵に出ているか双方向で測る)）。
- **信用できるのはハンドルの生バイト**（タグ付きデータは 16 進ダンプで。[Tagged Data](Tagged%20Data.md)）。
- **浮動小数の走査**（[実例](../Metrics/Investigation%20Techniques.md#生バイトと浮動小数の走査677173)）:
  - **1 ULP を追うならプローブの中で予測式を計算しない**——clang は FMA へ縮約する
    （`-ffp-contract=on`）。予測は外で計算して値を書き込むか、`volatile` で止める。
    **プローブが出すのは実測値だけ。**
  - **予測が割れる値・外れるはずの値を必ず混ぜる。** **数えるだけの走査は 1 ケース 1 文字**で
    地図にする。**「何で決まるか」は厳密に揃えた組（ピタゴラス数など）で 1 発で割る。**
    1 次元で決まらなければ**格子**にする。点を増やす前に**何が決まっているかを絞る**。
- **Windows 版 SDK を引き直しても同じものしか出ない。** `ci-debug` の `--platform windows` と
  `mac` の SDK は、**`SDKLib/` の 961 ファイルすべてが改行コードを除いて同一**で、
  **`Include/OnlyWin` も `Include/OnlyMac` も両方に入っている**。プラットフォーム差は実機で
  測るしかない（[File and Folder Identifiers](File%20and%20Folder%20Identifiers.md) の
  `SAttributes`）。比べるときは BSD の `tr -d '\r'` を使わず `perl` で
  （[実測](../Metrics/Investigation%20Techniques.md#windows-版-sdk-は-mac-版と同一)）。
- **図面ラベルの調査で踏んだもの**（[実例](../Metrics/Investigation%20Techniques.md#図面ラベルの調査で踏んだもの175177179)）:
  - **PIO が描いた図形は入れ子になっている**——走査は再帰で書き、深さも記録する。
  - **`GetTextSize` だけでは紙の pt は決まらない**——同じ容れ物に紙の pt が分かっている物差しを
    置いて比で測る。**物差しは大きさ用と外接用を別に取る**（外接/大きさの比は書体で違う）。
  - **2 つの要因が常に一致していると切り分けたつもりになる**——交差させた対を 1 走行で取る。
  - **作り替えて何度も走らせるプローブは、先頭コメントに確定したことと今回測ることを書く。**
  - **「作った直後」だけを読まず、1 つ足すたびに全員を読む**（動いていたのは測っていない場所）。
  - **規則の候補は独立した過去の走行で検算する**（古いログはただの検算データ）。
  - **「乱れている」で終わらせず、繰り返し回数を増やして並べる。**
- **ワークシート式は、ワークシートを作らずにハンドルへ直接評価できる**——
  `ISDK::CompileCriteriaExpression` ＋ `ISDK::ExecWSExpression`（ラッパーは
  `VWFC::Tools::WSCriteriaExpression`）。**`=` は付けない**。文脈は `eLocal` / `eUniversal`。【ヘッダ根拠】
  - **知らない識別子もコンパイルは通り、「自分自身の文字列」に評価される**【実測】（#187）。
    **判定は戻り値**で、入れた綴りと同一の文字列なら**その関数は無い**。
  - **【訂正】当初の「`InvalIdent` で無いと判定できる」は誤り**（1 度も返らなかった）。
  - **存在しない識別子の対照を必ず 1 本入れる。** 戻り値は `VWVariant` なので**型も出す**
    （`eVWVariantType_Empty` と空文字列は別物）。
  - 量だけなら `ISDK::ExecQTOFunction` ＋ `EQTOFunction`（17 個）という列挙できる口もある。【ヘッダ根拠】
  （[実測と判定表](../Metrics/Investigation%20Techniques.md#ワークシート式の評価187)）

## いつ読むか——同じ呼び出しの中・undo イベントの後・VW へ戻った後

**同じ呼び出しの中で読んだ値は、利用者の図面に残る値と同じとは限らない**
（[#245](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/245)。4 時点
T0・T1・Te・T2 の読み比べは [計測記録](../Metrics/Investigation%20Techniques.md#いつ読むか同じ呼び出しの中undo-イベントの後vw-へ戻った後)）。

- **後から変わるものは 2 種類**: **ランループで作り直されるもの**（連続寸法の中の寸法の大きさ。
  [Dimensions](Dimensions.md)）と、**undo イベントの終わりで更新されるもの**（`ResetObject` 後に
  `SetEntityMatrix` で動かした PIO の外接・`UpdateViewport` 直後の `IsDirty()`）。
- **それ以外の「効かない」は、戻った後も同じだった**（各ファイルに注記）。`doRegen=false` の
  まま `ResetObject` しない PIO を VW が後から作り直すことも無い。**逆に「`SetDirty(true)` が
  立たない」は再現せず、旧結論の誤り**だった（[Layers and Stories](Layers%20and%20Stories.md)）。
- **「効かない」と書く前に、undo イベントを閉じた後と、VW へ戻った後にも読む**（外接・dirty の
  印・PIO の中身）。仕立ては [PR #246](https://github.com/min-nano/vectorworks-developer-sdk-reference/pull/246)
  の `RunDeferred`（消したが履歴にある）。
- **自動で開いた undo イベントを閉じずに戻らない**（[Undo](Undo.md)。外接も `IsDirty` も
  古いまま、ブリッジが止まる。時間でもアイドルでも追い付かない）。
- **戻った後に呼ばれるもの（タイマー・`dispatch_after`・通知）をプローブに仕掛けない**——
  VW ごと落ちる（[probes/runtime/README.md](../probes/runtime/README.md)）。
- **`OpenDocumentPath(nullptr)` の図面はテンプレートの資源を持たない**（図面ラベルのスタイル・
  「寸法」クラスの文字スタイルが無い。縮尺 1/1・用紙が小さい）。`vw_run_probe` の新規図面も
  これ。旧調査の再現は**同じ出どころの図面**で（[Documents](Documents.md)・
  [Dimensions](Dimensions.md)「関連付くのは表示範囲の中だけ」）。
- **SDK の呼び出しが「画面の状態」（選択・表示・アクティブレイヤ）を黙って読むことがある**
  （#249。[Drawing Labels](Drawing%20Labels.md)・[Dimensions](Dimensions.md)）。食い違ったら
  **選択と表示**を疑い、`DeselectAll()` と表示の中心・縮尺をログに出す。
- **旧調査を測り直すときは、まず T0 が旧調査と同じ値になることを確かめる**（#248）。
- **所要時間（ms）の表は「呼び出しの中の時間」だけ**を測っている。

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
