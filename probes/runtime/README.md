# probes/runtime — 実機で走らせる調査（プローブ）

**実機（ローカルの VectorWorks）でしか答えの出ない問い**を確かめるコード置き場。
ここに置いたものは[実機確認プラグイン](../../plugin/README.md)（VwSdkProbes）へ
まとめて入り、メニューコマンドのピッカーから選んで走らせられる。

`probes/*.cpp`（1 階層上）との違い:

| | 何をする | どこで動く |
| --- | --- | --- |
| `probes/<名前>.cpp` | **構文チェックだけ**（`ci-debug` の `compile` モード。`-fsyntax-only`） | CI |
| `probes/runtime/<slug>/probe.cpp` | **実際に走らせて図面を触る** | 実機の VectorWorks |

「この呼び出しはコンパイルが通るか」だけなら 1 階層上で足りる（速い）。
「呼んだら何が起きるか」はここ。

## 書き方

1. `example/` を写して `probes/runtime/<slug>/` にする。**slug は小文字英数字と
   ハイフンだけ**（ディレクトリ名がそのまま一意な鍵になる）。
2. `VW_PROBE` の**第 1 引数をディレクトリ名と同じ slug にする**（違うと集約が止まる）。
3. 本体を書く。`probe`（`vwprobe::Report&`）へ書き出す。

```cpp
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
  集約の単位と一致している。補助のヘッダ（`.h`）や追加の `.cpp` を同じディレクトリへ
  置くのは自由（**すべてコンパイル対象になる**ので、そちらの名前は衝突しないよう
  無名名前空間か `static` に入れる）。
- **PR 番号やコミットをコードへ書かない。** 出所はビルドのときに決まり、
  [`scripts/gather-probes.sh`](../../scripts/gather-probes.sh) が表を生成して
  ピッカーに出す。
- **図面を壊す前提で書く。** プローブは undo イベントを自分では開かない
  （[Findings「Undo」](../../Findings/Undo.md)：半端な記録を取り消すと図面が壊れる）。
  利用者には「新規の空図面で走らせる」と案内してある。
- **落ちてもよいが、落ちる前に書く。** `probe.log()` は 1 行ごとにファイルへ flush
  するので、VectorWorks ごと落ちてもそこまでの行は残る。**知りたいことは、それを
  引き起こす呼び出しの前にログへ出しておく。**
- **例外を投げてよい。** メニュー側（`plugin/src/ProbeMenu.cpp`）が受け止めて
  「例外で中断」として見せる。VectorWorks は落ちない。
- **役目を終えたら消す。** 結論は `Findings/` に文章で残る（CLAUDE.md「調査のフロー」4）。
  `example/` だけは雛形かつ煙試験として残す。

## 確かめ方

```
# ① 構文チェックだけ先に（数十秒。mac 専用）
mcp__github__actions_run_trigger  ci-debug.yml
  inputs: {mode: compile, platform: mac, label: <一意>, args: probes/runtime/<slug>/probe.cpp}
scripts/ci-debug.sh wait --label <一意>     ← Bash(run_in_background: true)

# ② PR を出す（mac / Windows の実ビルドが自動で走る。公開はされない）

# ③ 実機用のビルドを作る
Actions の "Probe plug-in" を workflow_dispatch、inputs.prs に PR 番号（例 "12,15"）
→ リリース（タグ probes）から zip を落として実機へ
```
