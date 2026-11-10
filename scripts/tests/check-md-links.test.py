#!/usr/bin/env python3
"""scripts/check-md-links.py のアンカー計算が GitHub と合っているかの試験。

間違えると「リンク切れ 0 本」と言いながら GitHub 上では飛ばないリンクを通してしまう
（#308 のレビューで、`NNA#<n>_` と `.<名前>.lck` を含む見出しで実際に起きた）。
"""
import importlib.util
import os
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('cml', os.path.join(here, '..', 'check-md-links.py'))
cml = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cml)

failed = 0


def eq(name, got, want):
    global failed
    if got != want:
        failed += 1
        print(f'FAIL {name}: got {got!r} want {want!r}')


def heading(md):
    return cml.slug(cml.rendered_text(md))


# code span の中身は残す（< > は記号として落ちるが、中の文字は残る）
eq('code-angle', heading('ゲストは `NNA#<n>_` 付き'), 'ゲストは-nnan_-付き')
eq('code-angle-ja', heading('ロックファイル `.<名前>.lck`'), 'ロックファイル-名前lck')
# 強調の記号・全角の記号・ダッシュは落ちる
eq('emphasis', heading('**既定は 100 / 100**——だから'), '既定は-100--100だから')
eq('brackets', heading('「クラスに従わせる」口の一覧（ヘッダを読んだ）'), 'クラスに従わせる口の一覧ヘッダを読んだ')
# 対になっていない * / 語中の _ は文字のまま（強調ではない）
eq('literal-star', heading('2 * 3 の割り付け'), '2--3-の割り付け')
eq('intraword-underscore', heading('__NNA_DO_NOT_CHANGE の帯'), '__nna_do_not_change-の帯')
eq('single-emphasis', heading('*斜体* と _斜体_'), '斜体-と-斜体')
# 閉じの # は空白の後だけ落とす（C# は残る）
eq('closing-hashes', cml.HEADING.match('## C# の話 ##').group(2), 'C# の話')
eq('csharp', cml.HEADING.match('## C#').group(2), 'C#')
# 英字は小文字に、_ は残る
eq('lower', heading('`ForEachClass` で `kFieldPenStyle`'), 'foreachclass-で-kfieldpenstyle')
# リンクは文字だけ、HTML のタグは落ちる
eq('link', heading('[Undo](Undo.md) の <sub>注</sub>'), 'undo-の-注')

with tempfile.TemporaryDirectory() as d:
    a = os.path.join(d, 'a.md')
    b = os.path.join(d, 'b.md')
    with open(a, 'w', encoding='utf-8') as f:
        f.write('# 題\n\n## 実測\n\n## 実測\n\n```\n## コードの中は見出しではない\n```\n')
    with open(b, 'w', encoding='utf-8') as f:
        f.write('[ok](a.md#実測) [ok2](a.md#%E5%AE%9F%E6%B8%AC-1) [ng](a.md#実測-2)\n'
                '[ng2](a.md#コードの中は見出しではない) [nofile](c.md) [ext](https://example.com/#x)\n'
                '`[code](a.md#無い)` は見ない\n')
    probs = cml.check(b)
    eq('dup-and-missing', sorted(p.split(': ', 1)[1] for p in probs),
       sorted(['アンカーが無い: a.md#実測-2', 'アンカーが無い: a.md#コードの中は見出しではない',
               'リンク先が無い: c.md']))

with tempfile.TemporaryDirectory() as d:
    # github-slugger の重複の番号: 「実測」「実測」「実測-1」→ 実測 / 実測-1 / 実測-1-1
    a = os.path.join(d, 'a.md')
    with open(a, 'w', encoding='utf-8') as f:
        f.write('## 実測\n## 実測\n## 実測-1\n')
    eq('slugger-collision', sorted(cml.anchors(a)), sorted(['実測', '実測-1', '実測-1-1']))
    # 括弧を含むファイル名と、<…> で囲んだ空白入りのリンク先
    with open(os.path.join(d, 'A (B).md'), 'w', encoding='utf-8') as f:
        f.write('# 題\n## 節\n')
    b = os.path.join(d, 'b.md')
    with open(b, 'w', encoding='utf-8') as f:
        f.write('[a](A%20(B).md) [b](A%20%28B%29.md#節) [c](<A (B).md#節>) [t](A%20(B).md "題")\n'
                '[ng](<A (B).md#無い>) [ng2](A%20(C).md)\n')
    eq('parens-and-angle', sorted(p.split(': ', 1)[1] for p in cml.check(b)),
       sorted(['アンカーが無い: A (B).md#無い', 'リンク先が無い: A%20(C).md']))

print('check-md-links tests:', 'FAILED' if failed else 'ok')
sys.exit(1 if failed else 0)
