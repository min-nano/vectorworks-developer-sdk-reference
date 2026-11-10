#!/usr/bin/env python3
"""Markdown の相対リンクとアンカーを、GitHub と同じ規則で点検する。

    scripts/check-md-links.py [ファイルかディレクトリ ...]
        # 既定は Findings/ Metrics/ と、そこへリンクする読み物（CLAUDE.md・plugin/README.md・
        # probes/README.md・probes/runtime/README.md・README.md の「このフォークについて」の節）

Findings/ の結論から Metrics/ の節へは `（[実測](../Metrics/X.md#見出し)）` で数百本の
アンカーを張っている。見出しを 1 つ言い換えるとリンクが黙って切れるので、lint で
点検する（Metrics/README.md「見出しも記録の言い方にする」）。

アンカーの作り方は GitHub（github-slugger）に合わせる:
  * 見出しを描画したときの文字だけを使う（`code` の中身は残す。強調の記号・リンクの
    URL・HTML のタグは落とす）。
  * 小文字にし、文字・数字・結合記号・`-`・`_`・空白以外を消し、空白を `-` にする。
  * 同じアンカーが重なったら `-1`・`-2`…を付ける。付けた結果がまた既にあるアンカーと
    重なるときは番号を進める（github-slugger と同じ）。
リンク先は CommonMark のとおりに読む（`<…>` で囲めば空白を含められる。囲まないときは
対になった括弧を含められる——`Info/VCOM%20(…).md` のような名前のため）。
リンク先の `\\(` などのエスケープは外して読む。アンカーは大文字小文字まで一致を求める
（GitHub が作るアンカーは小文字だけなので、大文字の混じったアンカーは切れている）。
外部 URL（`http:` など）は見ない。切れたリンクが 1 本でもあれば exit 1。

README.md は上流（Vectorworks/developer-sdk）の文書で、上流の取り込みは lint を
起こさずに main へ入る。上流が切ったリンクで関係ない PR が落ちないよう、README.md は
このフォークで足した節（SECTION_ONLY）だけを点検する。
"""
import os
import re
import sys
import unicodedata
import urllib.parse

FENCE = re.compile(r'^\s*(`{3,}|~{3,})(.*)$')
HEADING = re.compile(r'^(#{1,6})\s+(.*?)(?:\s+#+)?\s*$')

DEFAULT_ROOTS = ['Findings', 'Metrics', 'CLAUDE.md', 'README.md', 'plugin/README.md',
                 'probes/README.md', 'probes/runtime/README.md']

# 一部の節だけを点検するファイル（{パス: 見出しの書き出し}）。その見出しから、同じか
# 上の階層の次の見出しまでを見る。
SECTION_ONLY = {'README.md': 'このフォークについて'}

_ESCAPE = re.compile(r'\\([!-/:-@\[-`{-~])')  # CommonMark のエスケープ（ASCII の記号）


class Fence:
    """``` / ~~~ の囲みを追う。閉じるのは同じ記号で、開きと同じか長い列だけ。"""

    def __init__(self):
        self.open = None

    def step(self, line):
        """この行が囲みの中（または区切り）なら True。"""
        m = FENCE.match(line)
        if self.open is None:
            if m and not (m.group(1)[0] == '`' and '`' in m.group(2)):
                self.open = m.group(1)
                return True
            return False
        if m and m.group(1)[0] == self.open[0] and len(m.group(1)) >= len(self.open) \
                and not m.group(2).strip():
            self.open = None
        return True


def link_targets(text):
    """`](` に続くリンク先を CommonMark のとおりに取り出す。"""
    i = 0
    while True:
        i = text.find('](', i)
        if i < 0:
            return
        j = i + 2
        while j < len(text) and text[j] in ' \t':
            j += 1
        if j < len(text) and text[j] == '<':
            k = text.find('>', j + 1)
            if k > 0:
                yield _ESCAPE.sub(r'\1', text[j + 1:k])
            i = j + 1
            continue
        depth, k = 0, j
        while k < len(text):
            ch = text[k]
            if ch == '\\':
                k += 2
                continue
            if ch.isspace():
                break
            if ch == '(':
                depth += 1
            elif ch == ')':
                if depth == 0:
                    break
                depth -= 1
            k += 1
        if k > j:
            yield _ESCAPE.sub(r'\1', text[j:k])
        i = k


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
    # 強調の記号は対になっているときだけ落とす（`2 * 3` や語中の `_` は文字のまま残る）
    for _ in range(3):
        s = re.sub(r'(\*\*|__)(?=\S)(.+?)(?<=\S)\1', r'\2', s)
        s = re.sub(r'\*(?=\S)(.+?)(?<=\S)\*', r'\1', s)
        s = re.sub(r'(?<![\w])_(?=\S)(.+?)(?<=\S)_(?![\w])', r'\1', s)
        s = re.sub(r'~~(?=\S)(.+?)(?<=\S)~~', r'\1', s)
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
    occurrences, fence = {}, Fence()
    with open(path, encoding='utf-8') as f:
        for line in f:
            if fence.step(line):
                continue
            m = HEADING.match(line)
            if m:
                base = slug(rendered_text(m.group(2)))
                s = base
                while s in occurrences:  # github-slugger と同じ番号の進め方
                    occurrences[base] += 1
                    s = f'{base}-{occurrences[base]}'
                occurrences[s] = 0
            for a in re.findall(r'<a\s+(?:name|id)="([^"]+)"', line):
                occurrences.setdefault(a, 0)
    result = set(occurrences)
    _anchor_cache[path] = result
    return result


def check(path):
    problems, fence = [], Fence()
    section = SECTION_ONLY.get(os.path.normpath(path))
    level = None if section else 0  # 0 = ファイル全体を見る
    with open(path, encoding='utf-8') as f:
        for lineno, line in enumerate(f, 1):
            if fence.step(line):
                continue
            if section:
                h = HEADING.match(line)
                if h and h.group(2).startswith(section):
                    level = len(h.group(1))
                elif h and level and len(h.group(1)) <= level:
                    level = None
                if level is None:
                    continue
            text = re.sub(r'`[^`]*`', '', line)
            for target in link_targets(text):
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
                    want = urllib.parse.unquote(frag)
                    if want not in anchors(dest):
                        problems.append(f'{path}:{lineno}: アンカーが無い: {target}')
    return problems


def main(argv):
    roots = argv or DEFAULT_ROOTS
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
