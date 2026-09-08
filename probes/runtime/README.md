# probes/runtime — 実機で走らせる調査（プローブ）

**実機（ローカルの VectorWorks）でしか答えの出ない問い**を確かめるコード置き場。
ここに置いたものは[実機確認プラグイン](../../plugin/README.md)（VwSdkProbes）の**本体**へ
まとめて入り、メニューコマンドのピッカーから選んで走らせられる。

`probes/*.cpp`（1 階層上）との違い:

| | 何をする | どこで動く |
| --- | --- | --- |
| `probes/<名前>.cpp` | **構文チェックだけ**（`ci-debug` の `compile` モード。`-fsyntax-only`） | CI |
| `probes/runtime/<slug>/probe.cpp` | **実際に走らせて図面を触る** | 実機の VectorWorks |

「この呼び出しはコンパイルが通るか」だけなら 1 階層上で足りる（速い）。
「呼んだら何が起きるか」はここ。

## 書き方

1. `probes/runtime/<slug>/probe.cpp` を作る。**slug は小文字英数字とハイフンだけ**
   （ディレクトリ名がそのまま一意な鍵になる）。下の雛形を写して中身を置き換える。
2. **先頭コメントに `[issue #<番号>]` と書く。** この issue はこの調査の id で、
   [`scripts/gather-probes.sh`](../../scripts/gather-probes.sh) が集約のときにここから
   読み取り、出所表とカタログへ載せる。**これが結果の投稿先の 2 番目の候補になる**——
   宛先はふつう「そのプローブが来た PR」だが、PR が無い（main に入った後、または
   main から集めたプローブ）ときは、この issue 番号（開いているものに限る）へ投稿される
   （`plugin/README.md`「結果を PR / issue へ自動で返す」）。
   **書き忘れても止まらない**——単に投稿先が無いプローブになるだけで、実害は無い。
3. `VW_PROBE` の**第 1 引数をディレクトリ名と同じ slug にする**（違うと集約が止まる）。
4. 本体を書く。`probe`（`vwprobe::Report&`）へ書き出す。

```cpp
//
//	probes/runtime/layer-order/probe.cpp
//
//	[issue #34] レイヤの重ね順が、作った順と並べ替えの操作でどう変わるかを実測する。
//

#include "Probe.h"

VW_PROBE("layer-order", "レイヤの重ね順を実測する",
		 "レイヤを 3 枚作り、並べ替えてから読み戻す")
{
	probe.log("レイヤを作る");
	MCObjectHandle layer = gSDK->CreateLayer("試験 1", kDesignLayerType);
	if (layer == nil)
	{
		probe.fail("CreateLayer が nil を返した");
		return;
	}
	// GetObjectName は**戻り値ではなく出力引数**で返す（void GetObjectName(h, TXString&)）。
	TXString name;
	gSDK->GetObjectName(layer, name);
	probe.log(std::string("できた: ") + static_cast<const char*>(name));
}
```

## 決まり

- **1 ファイルに 1 つ。** `VW_PROBE` が展開する名前は固定なので、1 つの翻訳単位に
  2 つ書くと衝突する。これは意図した制約で、「1 プローブ 1 ディレクトリ」という
  集約の単位と一致している。
- **表示名と概要は `VW_PROBE` の引数に、文字列リテラルで書く。** ビルドのときに
  ソースから読み出してカタログ（ピッカーの一覧）へ載せるので、変数や連結で書くと
  読み取れず、**表示名が slug のまま**になる。
- **他の PR と slug がぶつかっても構わない。** 本体は PR ごとに分かれているので
  衝突しない（ピッカーには出所付きで 2 行並ぶ）。main にあるものと同じ slug で
  中身を変えれば、**main の版と PR の版が並んで出る**——見比べるのが目的なら、それでよい。補助のヘッダ（`.h`）や追加の `.cpp` を同じディレクトリへ
  置くのは自由（**すべてコンパイル対象になる**ので、そちらの名前は衝突しないよう
  無名名前空間か `static` に入れる）。
- **PR 番号やコミットをコードへ書かない。** 出所はビルドのときに決まり、
  [`scripts/gather-probes.sh`](../../scripts/gather-probes.sh) が表を生成して
  ピッカーに出す。**issue 番号だけは例外**——調査そのものの id なので、先頭コメントの
  `[issue #<番号>]` に書く（上記「書き方」2）。
- **図面を壊す前提で書く。** プローブは undo イベントを自分では開かない
  （[Findings「Undo」](../../Findings/Undo.md)：半端な記録を取り消すと図面が壊れる）。
  利用者には「新規の空図面で走らせる」と案内してある。
- **落ちてもよいが、落ちる前に書く。** `probe.log()` は 1 行ごとにファイルへ flush
  するので、VectorWorks ごと落ちてもそこまでの行は残る。**知りたいことは、それを
  引き起こす呼び出しの前にログへ出しておく。** 落ちた回のログも、次にメニューを
  開いたときに「結末不明」として PR へ投稿される（plugin/README.md）。
- **ログは PR コメントとして公開される。** 出すのは SDK から読み戻した値だけにする
  （プローブが触るのは新規の空図面なので、ふつうは何も気にしなくてよい）。
- **例外を投げてよい。** メニュー側（`plugin/src/ProbeMenu.cpp`）が受け止めて
  「例外で中断」として見せる。VectorWorks は落ちない。
- **役目を終えたら消す。** 結論は `Findings/` に文章で残る（CLAUDE.md「調査のフロー」4）。
  **常駐の煙試験は置かない**——ピッカーに「調べているわけではないプローブ」が並ぶと、
  いま確かめたいものと見分けが付かない。導線（メニュー → 選ぶ → 走る → 結果）が
  通っているかは、そのとき調べているプローブを走らせれば分かる。**「すべて順に実行」
  （ピッカーの 3 番目）に毎回混ざる**ぶん、残しておく害はいっそう大きい。
- **1 件も無い状態は正常。** すべてのプローブが役目を終えれば `probes/runtime/` は
  README だけになる。そのときのピッカーは「プローブがありません。新しいビルドを
  取り込めます:」と出て、入れ替えだけが選べる（`plugin/src/ProbeMenu.cpp`）。

## 確かめ方

```
# ① 構文チェックだけ先に（数十秒。mac 専用）
mcp__github__actions_run_trigger  ci-debug.yml
  inputs: {mode: compile, platform: mac, label: <一意>, args: probes/runtime/<slug>/probe.cpp}
scripts/ci-debug.sh wait --label <一意>     ← Bash(run_in_background: true)

# ② PR を出す（あとは自動）
probe-auto-update.yml が "Probe plug-in" を起動し（open な PR を全部載せる）、完了まで待つ。
  * その待機がそのまま PR のチェック。**自分の群がコンパイルできなければ、その PR だけが
    赤くなる**（本体は PR ごとに分かれているので、他人の PR は止まらない）
  * 成功すると転がりタグ probes のリリースが更新され、PR にコメントが 1 つ付く
  * **すでにプラグインを入れてあるなら、次に Vectorworks を起動すると
    「入れ替えますか？」と尋ねてくる**（初回だけ手で入れる。plugin/README.md）

# ③ 実機で走らせてもらう（結果は自動で PR / issue へ返る）
利用者がすることは「入れ替える」と「走らせる」の 2 つだけ。走らせ終えると、**その
プローブの出所（PR。無ければ先頭コメントの issue 番号）へ結果（結末・所要・出所・
ログ全文）がコメントとして投稿される**（plugin/README.md「結果を PR / issue へ自動で
返す」）。目印 `<!-- vw-probes-result v1 … -->` で始まるコメントがそれで、**人は 1 文字も
書いていない**（絵や所見はチャットへ来る）。PR がマージされた後（main のプローブに
なった後）でも、issue 番号さえ書いてあれば結果は返ってくる。

# ③ 載せる顔ぶれを絞りたいときだけ、手で叩く
Actions の "Probe plug-in" を workflow_dispatch、inputs.prs に PR 番号（例 "12,15"）
```
