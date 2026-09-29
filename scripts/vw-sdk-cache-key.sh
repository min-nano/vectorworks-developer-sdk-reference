#!/usr/bin/env bash
#
# vw-sdk-cache-key.sh — SDK キャッシュのキーを「いま配布されている SDK の版」から決める。
#
# なぜこれがあるか
# ----------------
# SDK の URL は ".../latest/..." で、Vectorworks が SDK を差し替えると**同じ URL の
# 中身が黙って変わる**。以前はキャッシュのキーが固定（`vw-sdk-2026-NNA-mac-headers-v1`
# など）だったので、一度温まったキャッシュが永久にヒットし続け、**SDK が更新されても
# ci-debug は古いヘッダを検索し、probe-build は古いライブラリでビルドし続ける**という
# 静かな壊れ方をしていた（SDK の宣言索引は週次でキャッシュ無しに取り直すので、
# 索引だけが新しくなって ci-debug と食い違う）。
#
# そこでダウンロードの前に HEAD を 1 本打ち、応答の ETag（無ければ Last-Modified）を
# キーに混ぜる。SDK が差し替わればキーが変わってキャッシュを外し、取り直す。
#
# HEAD が失敗したとき（配布元の一時的な不調など）は run を落とさない。キーを
# `<prefix>-unknown` にして restore-keys（`<prefix>-`）を渡し、**直近のキャッシュを
# 使う**——古いかもしれないが、何も調べられないよりよい。キャッシュも無ければ
# fetch-vw-sdk.sh がダウンロードを試み、そちらで落ちる。
#
# 使い方（ワークフローから）
# --------------------------
#   - id: sdk-key
#     run: scripts/vw-sdk-cache-key.sh vw-sdk-2026-NNA-mac-headers
#   - uses: actions/cache@...
#     with:
#       key: ${{ steps.sdk-key.outputs.key }}
#       restore-keys: ${{ steps.sdk-key.outputs.restore-keys }}
#
# 環境変数:
#   VW_SDK_URL      SDK zip の URL（fetch-vw-sdk.sh と同じもの）
#   GITHUB_OUTPUT   Actions が設定する。無ければ stdout に出すだけ（ローカルでの確認用）
#
set -euo pipefail

prefix="${1:?usage: vw-sdk-cache-key.sh <key-prefix>}"
url="${VW_SDK_URL:?VW_SDK_URL is not set}"

# 応答ヘッダから 1 つ取り出す（リダイレクトの分も重なって入るので最後のものを採る）。
header_value() {
	printf '%s\n' "$headers" | tr -d '\r' | awk -v k="$1" '
		tolower($0) ~ "^" tolower(k) ":" { sub(/^[^:]*:[ \t]*/, ""); v = $0 } END { print v }'
}

version=""
if headers="$(curl -fsSIL --max-time 30 --retry 2 --retry-delay 3 "$url" 2>/dev/null)"; then
	version="$(header_value etag)"
	if [ -z "$version" ]; then
		version="$(header_value last-modified)"
	fi
fi

# キーに使えない文字（引用符・空白・カンマ等）を落とし、長さも抑える。
version="$(printf '%s' "$version" | tr -c 'A-Za-z0-9._-' '-' | sed 's/--*/-/g; s/^-//; s/-$//' | cut -c1-64)"
if [ -z "$version" ]; then
	echo "::warning::vw-sdk-cache-key: SDK の版を確かめられませんでした（HEAD $url）。直近のキャッシュを使います。"
	version="unknown"
fi

# restore-keys（前方一致での復元）は**版が分からないときだけ**渡す。版が分かっているのに
# 渡すと、新しい版のキャッシュがまだ無い初回に**古い版のキャッシュが前方一致で復元され**、
# fetch-vw-sdk.sh はそれを「揃っている」と見てダウンロードしない——直したい壊れ方そのものに
# なる。
key="${prefix}-${version}"
restore=""
if [ "$version" = "unknown" ]; then
	restore="${prefix}-"
fi
echo "cache key: $key${restore:+ (restore-keys: $restore)}"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
	{
		echo "key=$key"
		echo "restore-keys=$restore"
	} >>"$GITHUB_OUTPUT"
fi
