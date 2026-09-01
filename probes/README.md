# probes — 調査用のコンパイルスニペット置き場

「この API はこの引数で呼べるか」「この型はこう使えるか」を SDK ヘッダに対して
確かめるための **1 ファイル完結のスニペット**を置く。CI の `compile` モードが
`-fsyntax-only` で構文チェックする（リンクはしない——このリポジトリの SDK は
ヘッダしか持たない。実際に動かす確認は実機か、実プラグイン側のビルドで行う）。

```
1. スニペットを probes/<調査名>.cpp として作業ブランチへ push
2. mcp__github__actions_run_trigger で ci-debug.yml をディスパッチ
     inputs: {mode: compile, platform: mac, label: <一意な文字列>, args: probes/<調査名>.cpp}
3. Bash(run_in_background: true): scripts/ci-debug.sh wait --label <label>
```

書き方は [`example.cpp`](example.cpp) を雛形にする。要点:

- 先頭で `#include "VectorworksSDK.h"`（プラットフォームは `__APPLE__` から自動判別
  されるので、マクロ定義は要らない）。
- `gSDK->...` の呼び出しは、実行はしないので**関数の中に書くだけ**でよい。
  「コンパイルが通るか」だけが答え。
- **役目を終えたスニペットは消す**（調査 PR をマージする前に。結論は Findings/ に
  文章で残る。[`CLAUDE.md`](../CLAUDE.md)）。`example.cpp` だけは雛形として残す。
