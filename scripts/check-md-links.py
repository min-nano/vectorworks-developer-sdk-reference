#!/usr/bin/env python3
"""Markdown の相対リンクとアンカーを、GitHub と同じ規則で点検する。

    scripts/check-md-links.py [ファイルかディレクトリ ...]   # 既定は Findings/ Metrics/

Findings/ の結論から Metrics/ の節へは `（[実測](../Metrics/X.md#見出し)）` で数百本の
アンカーを張っている。見出しを 1 つ言い換えるとリンクが黙って切れるので、lint で
点検する（Metrics/README.md「見出しも記録の言い方にする」）。

アンカーの作り方は GitHub（github-slugger）に合わせる:
  * 見出しを描画したときの文字だけを使う（`code` の中身は残す。強調の記号・リンクの
    URL・HTML のタグは落とす）。
  * 小文字にし、文字・数字・結合記号・`-`・`_`・空白以外を消し、空白を `-` にする。
  * 同じアンカーが 2 度目に出たら `-1`、3 度目は `-2`…を付ける。
外部 URL（`http:` など）は見ない。切れたリンクが 1 本でもあれば exit 1。
"""
import os
import re
import sys
import unicodedata
import urllib.parse

FENCE = re.compile(r'^\s*(```|~~~)')
HEADING = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')
LINK = re.compile(r'\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)')


def rendered_text(md):
    """見出しの Markdown から、描画したときの文字列を作る。"""
    out, i = [], 0
    for m in re.finditer(r'(`+)(.+?)\1', md):
        out.append(_plain(md[i:m.start()]))
        out.append(m.group(2).strip())  # code span: 中身はそのまま
        i = m.end()
    out.append(_plain(md[i:]))
    return ''.join(out)


def _plain(s):
    s = re.sub(r'!?\[([^\]]*)\]\([^)]*\)', r'\1', s)  # リンク・画像は文字だけ
    s = re.sub(r'<[A-Za-z/][^>]*>', '', s)  # HTML のタグ
    s = re.sub(r'(\*\*|__|\*|~~)', '', s)  # 強調の記号
    s = s.replace('\\', '')
    return s


def slug(text):
    keep = []
    for ch in text.lower():
        cat = unicodedata.category(ch)
        if ch in ' -_' or cat[0] in 'LNM':
            keep.append('-' if ch == ' ' else ch)
    return ''.join(keep)


_anchor_cache = {}


def anchors(path):
    if path in _anchor_cache:
        return _anchor_cache[path]
    seen, result, fence = {}, set(), False
    with open(path, encoding='utf-8') as f:
        for line in f:
            if FENCE.match(line):
                fence = not fence
                continue
            if fence:
                continue
            m = HEADING.match(line)
            if m:
                s = slug(rendered_text(m.group(2)))
                n = seen.get(s, 0)
                seen[s] = n + 1
                result.add(s if n == 0 else f'{s}-{n}')
            for a in re.findall(r'<a\s+(?:name|id)="([^"]+)"', line):
                result.add(a)
    _anchor_cache[path] = result
    return result


def check(path):
    problems, fence = [], False
    with open(path, encoding='utf-8') as f:
        for lineno, line in enumerate(f, 1):
            if FENCE.match(line):
                fence = not fence
                continue
            if fence:
                continue
            text = re.sub(r'`[^`]*`', '', line)
            for target in LINK.findall(text):
                if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', target):
                    continue  # http: / mailto: など
                file_part, _, frag = target.partition('#')
                dest = path
                if file_part:
                    dest = os.path.normpath(
                        os.path.join(os.path.dirname(path), urllib.parse.unquote(file_part)))
                if not os.path.exists(dest):
                    problems.append(f'{path}:{lineno}: リンク先が無い: {target}')
                    continue
                if frag and dest.endswith('.md'):
                    want = urllib.parse.unquote(frag).lower()
                    if want not in anchors(dest):
                        problems.append(f'{path}:{lineno}: アンカーが無い: {target}')
    return problems


def main(argv):
    roots = argv or ['Findings', 'Metrics']
    files = []
    for r in roots:
        if os.path.isdir(r):
            for d, _, names in os.walk(r):
                files += [os.path.join(d, n) for n in sorted(names) if n.endswith('.md')]
        else:
            files.append(r)
    problems = []
    for f in sorted(files):
        problems += check(f)
    for p in problems:
        print(p)
    print(f'check-md-links: {len(files)} ファイル・切れたリンク {len(problems)} 本')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
