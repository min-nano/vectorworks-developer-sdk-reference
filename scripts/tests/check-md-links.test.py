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

print('check-md-links tests:', 'FAILED' if failed else 'ok')
sys.exit(1 if failed else 0)
