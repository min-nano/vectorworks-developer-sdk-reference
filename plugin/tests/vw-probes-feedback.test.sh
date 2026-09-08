#!/usr/bin/env bash
#
#	vw-probes-feedback.test.sh
#
#	同梱スクリプト（plugin/scripts/vw-probes-feedback.sh）の単体テスト——プローブの結果を
#	PR へ投稿する裏方（plugin/src/Feedback.h）。
#
#	スクリプトは**source して**（本物の main は末尾で守られている）、いちばん外側の
#	入出力だけを差し替える。だから引数の順序・応答の扱い・トークンの探索順といった
#	**本当のロジックはそのまま走る**:
#
#	  * curl      … 網の境目。-o <ファイル> と -w（HTTP コード）の使い方を解し、
#	                --data-binary で渡された本文をファイルへ控える（組み立てた JSON を
#	                検査できるように）。
#	  * plutil    … macOS 専用の JSON 読み。python3 で代用する（Linux のランナーで走る）。
#	  * security  … キーチェーン。作業ファイルで代用し、login / logout / 探索順の
#	                本物の手順を走らせる。
#	  * gh_path   … gh CLI。既定では無いものとし、必要な 1 件だけで有効にする。
#
#	**トークンが出力へ漏れないこと**もここで確かめる——漏れたら PR コメントや診断ログに
#	載りうるので、単なる整形の話ではない。
#
#	**bash 3.2 で動くこと**（＝配列を書いていないこと）も検査する。このハーネスが走るのは
#	Linux の bash 5 だが、**本番で走るのは macOS の /bin/bash 3.2** で、そちらだけ
#	`set -u` + 空配列の展開で即死する。実プラグイン側で実際に踏んだ穴なので、
#	「そもそも配列を書かない」を構文の側で押さえる。
#
#	走らせ方（CI の lint ワークフローも同じ）:
#	    bash plugin/tests/vw-probes-feedback.test.sh
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="${HERE}/../scripts/vw-probes-feedback.sh"

[ -f "$SCRIPT" ] || {
	echo "ERROR: $SCRIPT がありません。" >&2
	exit 1
}
command -v python3 >/dev/null 2>&1 || {
	echo "ERROR: plutil を代用する python3 が要ります。" >&2
	exit 1
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ---------------------------------------------------------------------------
# 小さなハーネス。
# ---------------------------------------------------------------------------
FAILURES=0
CHECKS=0

check() { # description, actual, expected
	CHECKS=$((CHECKS + 1))
	if [ "$2" = "$3" ]; then
		echo "[ PASS ] $1"
	else
		echo "[ FAIL ] $1"
		echo "         expected: $3"
		echo "         actual:   $2"
		FAILURES=$((FAILURES + 1))
	fi
}

check_contains() { # description, haystack, needle
	CHECKS=$((CHECKS + 1))
	case "$2" in
		*"$3"*) echo "[ PASS ] $1" ;;
		*)
			echo "[ FAIL ] $1"
			echo "         expected to contain: $3"
			echo "         actual:              $2"
			FAILURES=$((FAILURES + 1))
			;;
	esac
}

check_not_contains() { # description, haystack, needle
	CHECKS=$((CHECKS + 1))
	case "$2" in
		*"$3"*)
			echo "[ FAIL ] $1"
			echo "         must NOT contain: $3"
			echo "         actual:           $2"
			FAILURES=$((FAILURES + 1))
			;;
		*) echo "[ PASS ] $1" ;;
	esac
}

# ---------------------------------------------------------------------------
# 環境: 本物のトークンもキーチェーンも gh も無い。
# ---------------------------------------------------------------------------
unset VW_PROBE_FEEDBACK_TOKEN
export VW_REPO="min-nano/vectorworks-developer-sdk-reference"

# shellcheck source=../scripts/vw-probes-feedback.sh
. "$SCRIPT"

# plutil の代用（JSON を読む）。
jval() { # json-file, keypath -> raw scalar
	python3 - "$1" "$2" <<'PY' 2>/dev/null || true
import json, sys
try:
	with open(sys.argv[1], encoding="utf-8") as handle:
		data = json.load(handle)
except Exception:
	sys.exit(0)
for part in sys.argv[2].split("."):
	if isinstance(data, list):
		try:
			data = data[int(part)]
		except (ValueError, IndexError):
			sys.exit(0)
	elif isinstance(data, dict):
		if part not in data:
			sys.exit(0)
		data = data[part]
	else:
		sys.exit(0)
if isinstance(data, (dict, list)):
	sys.exit(0)
print(data)
PY
}

# キーチェーンの代用。作業ファイルを置き場にして、本物の add / find / delete の
# 呼び分けをそのまま走らせる。
KEYCHAIN="$WORK/keychain"
security() {
	case "${1:-}" in
		find-generic-password)
			[ -f "$KEYCHAIN" ] || return 1
			cat "$KEYCHAIN"
			;;
		add-generic-password)
			local value=""
			while [ "$#" -gt 0 ]; do
				[ "$1" = "-w" ] && {
					value="${2:-}"
					break
				}
				shift
			done
			printf '%s' "$value" >"$KEYCHAIN"
			;;
		delete-generic-password) rm -f "$KEYCHAIN" ;;
		*) return 1 ;;
	esac
}

# gh CLI: 既定では無い。
GH_STUB=""
gh_path() {
	[ -n "$GH_STUB" ] || return 1
	printf '%s' "$GH_STUB"
}

# curl の代用。CURL_BODY を -o のファイルへ書き、-w があれば CURL_CODE を出す。
# --data-binary で渡された本文は控える（組み立てた JSON を検査するため）。
CURL_BODY=""
CURL_CODE="201"
CURL_FAIL=0
CURL_URL_FILE="$WORK/url.txt"
CURL_PAYLOAD="$WORK/payload.json"
curl() {
	# 呼ばれるのは $(...) の中（＝サブシェル）なので、覗きたい値は変数ではなく
	# **ファイルへ**残す。変数に入れても呼び出し側からは見えない。
	local out="" wants_code=0 arg
	rm -f "$CURL_PAYLOAD" "$CURL_URL_FILE"
	while [ "$#" -gt 0 ]; do
		arg="$1"
		case "$arg" in
			-o)
				out="${2:-}"
				shift
				;;
			-w)
				wants_code=1
				shift
				;;
			--data-binary)
				cp "${2#@}" "$CURL_PAYLOAD" 2>/dev/null || true
				shift
				;;
			https://*) printf '%s' "$arg" >"$CURL_URL_FILE" ;;
		esac
		shift
	done
	if [ "$CURL_FAIL" -eq 1 ]; then
		[ "$wants_code" -eq 1 ] && printf '000'
		return 1
	fi
	[ -n "$out" ] && printf '%s' "$CURL_BODY" >"$out"
	[ "$wants_code" -eq 1 ] && printf '%s' "$CURL_CODE"
	return 0
}

# ---------------------------------------------------------------------------
# token-status — 探索順と、**トークンが出力に出ないこと**。
# ---------------------------------------------------------------------------
rm -f "$KEYCHAIN"
out="$(mode_token_status)"
check "token-status: 何も無ければ none" "$out" "source=none
ok=no"

printf '%s' "keychain-secret-value" >"$KEYCHAIN"
out="$(mode_token_status)"
check "token-status: 環境変数が無ければキーチェーン" "$out" "source=keychain
ok=yes"
check_not_contains "token-status はトークンを出さない" "$out" "keychain-secret-value"

VW_PROBE_FEEDBACK_TOKEN="env-secret-value"
export VW_PROBE_FEEDBACK_TOKEN
out="$(mode_token_status)"
check "token-status: 環境変数が最優先" "$out" "source=env
ok=yes"
check_not_contains "token-status は環境変数のトークンも出さない" "$out" "env-secret-value"
check "resolve_token は環境変数のトークンを返す" "$(resolve_token)" "env-secret-value"
unset VW_PROBE_FEEDBACK_TOKEN

rm -f "$KEYCHAIN"
GH_STUB="$WORK/gh"
cat >"$GH_STUB" <<'GH'
#!/usr/bin/env bash
[ "$1" = "auth" ] && [ "$2" = "token" ] && echo "gh-secret-value"
GH
chmod +x "$GH_STUB"
out="$(mode_token_status)"
check "token-status: 最後は gh CLI へ落ちる" "$out" "source=gh
ok=yes"
GH_STUB=""

# ---------------------------------------------------------------------------
# login / logout
# ---------------------------------------------------------------------------
rm -f "$KEYCHAIN"
TOKEN_FILE="$WORK/token.txt"
printf 'ghp_exampletoken\n' >"$TOKEN_FILE"
out="$(mode_login "$TOKEN_FILE")"
check "login: 保存できる" "$out" "ok"
check "login: 保存先へ届いている" "$(cat "$KEYCHAIN")" "ghp_exampletoken"
CHECKS=$((CHECKS + 1))
if [ -f "$TOKEN_FILE" ]; then
	echo "[ FAIL ] login: 受け渡しのファイルは消さなければならない"
	FAILURES=$((FAILURES + 1))
else
	echo "[ PASS ] login: 受け渡しのファイルを消す"
fi

printf '' >"$WORK/empty.txt"
check "login: 空のトークンは断る" "$(mode_login "$WORK/empty.txt")" \
	"error=トークンが空です。"
check "login: ファイルが無ければ断る" "$(mode_login "$WORK/nope.txt")" \
	"error=トークンのファイルが見つかりません。"

check "logout: 成功する" "$(mode_logout)" "ok"
check "logout: 保存が消えている" "$(mode_token_status)" "source=none
ok=no"

# ---------------------------------------------------------------------------
# find-pr — 出所に PR 番号が無いビルド（PR のブランチでビルドしたものを手で入れたとき）
# の逃げ道。**トークン無しでも引けなければならない**——登録の前に宛先が分かる必要が
# あるためで、実プラグイン側ではここが死んで番号を手入力させてしまった。
# ---------------------------------------------------------------------------
rm -f "$KEYCHAIN"
GH_STUB=""
CURL_BODY='[{"number":42,"title":"プローブの結果を PR コメントへ自動で投稿する"}]'
CURL_CODE="200"
out="$(mode_find_pr "min-nano/vectorworks-developer-sdk-reference" "claude/probe-results")"
check "find-pr: トークンが無くても open な PR を返す" "$out" "pr=42
title=プローブの結果を PR コメントへ自動で投稿する
ok"
check_contains "find-pr: head=<owner>:<branch> で引く" "$(cat "$CURL_URL_FILE")" \
	"head=min-nano:claude/probe-results"

out="$(mode_find_pr "" "claude/probe-results")"
check_contains "find-pr: repo が空なら既定のリポジトリ" "$(cat "$CURL_URL_FILE")" \
	"repos/min-nano/vectorworks-developer-sdk-reference/pulls"

CURL_BODY='[]'
out="$(mode_find_pr "o/r" "no-such-branch")"
check "find-pr: open な PR が無いとき" "$out" \
	"error=ブランチ no-such-branch に open な PR がありません。"

check "find-pr: ブランチが無ければ断る" "$(mode_find_pr "o/r" "")" \
	"error=ブランチが指定されていません。"

CURL_FAIL=1
out="$(mode_find_pr "o/r" "b")"
CURL_FAIL=0
check "find-pr: 網の失敗は理由を返すだけ（落とさない）" "$out" \
	"error=PR を検索できませんでした（ネットワークか権限）。"

# ---------------------------------------------------------------------------
# post — 送り先・本文・成功と失敗の文言。
# ---------------------------------------------------------------------------
printf '%s' "keychain-secret-value" >"$KEYCHAIN"
BODY_FILE="$WORK/body.md"
cat >"$BODY_FILE" <<'BODY'
<!-- vw-probes-result v1 probe=layer-order group=pr12 pr=12 build=abc result=ok -->
## 実機プローブ: "レイヤの重ね順" `[layer-order]`
バックスラッシュ \ と "引用符"
BODY

CURL_BODY='{"html_url":"https://github.com/o/r/pull/12#issuecomment-1"}'
CURL_CODE="201"
out="$(mode_post "o/r" "12" "$BODY_FILE")"
check "post: コメントの URL を返す" "$out" "url=https://github.com/o/r/pull/12#issuecomment-1
ok"
check_contains "post: issue コメントの口へ投げる" "$(cat "$CURL_URL_FILE")" \
	"https://api.github.com/repos/o/r/issues/12/comments"

# **repo が空なら既定（このリポジトリ）へ。** 殻は設定が空のまま渡してくる。
out="$(mode_post "" "12" "$BODY_FILE")"
check_contains "post: repo が空なら既定のリポジトリ" "$(cat "$CURL_URL_FILE")" \
	"repos/min-nano/vectorworks-developer-sdk-reference/issues/12/comments"

# 組み立てた JSON は本文をそのまま往復しなければならない——本文はプローブのログを
# 含むので、引用符もバックスラッシュも日本語も通ること。
decoded="$(python3 -c '
import json, sys
with open(sys.argv[1], encoding="utf-8") as handle:
    print(json.load(handle)["body"], end="")
' "$CURL_PAYLOAD")"
check "post: 本文が JSON を往復する" "$decoded" "$(cat "$BODY_FILE")"

CURL_CODE="403"
CURL_BODY='{"message":"Resource not accessible by integration"}'
out="$(mode_post "o/r" "12" "$BODY_FILE")"
check "post: GitHub の言い分をそのまま渡す" "$out" \
	"error=コメントを投稿できませんでした（Resource not accessible by integration）。"

CURL_CODE="201"
rm -f "$KEYCHAIN"
out="$(mode_post "o/r" "12" "$BODY_FILE")"
check "post: トークンが無いとき" "$out" \
	"error=GitHub のトークンがありません（先に登録してください）。"

printf '%s' "keychain-secret-value" >"$KEYCHAIN"
check "post: 本文のファイルが無ければ断る" "$(mode_post "o/r" "12" "$WORK/none.md")" \
	"error=引数が不足しています。"
check "post: PR 番号が無ければ断る" "$(mode_post "o/r" "" "$BODY_FILE")" \
	"error=引数が不足しています。"

check "不明なモードは名乗って終わる" "$(main "no-such-mode")" \
	"error=不明なモード: 'no-such-mode'（token-status / login / logout / find-pr / post）。"

# ---------------------------------------------------------------------------
# bash 3.2（macOS の /bin/bash）で動くこと。
#
# **配列を 1 つも書いていないこと**を構文の側で押さえる。`set -u` の下で空の配列を
# 展開すると bash 3.2 は "unbound variable" で即死するが、このハーネスが走る bash 5 では
# 起きない——だから「振る舞いを試す」では守れず、**書かないことを検査する**しかない。
# ---------------------------------------------------------------------------
CHECKS=$((CHECKS + 1))
if grep -nE '\$\{[A-Za-z_][A-Za-z0-9_]*\[@\]\}' "$SCRIPT" >/dev/null 2>&1; then
	echo "[ FAIL ] 配列を展開してはいけない（macOS の bash 3.2 は空配列で即死する）"
	grep -nE '\$\{[A-Za-z_][A-Za-z0-9_]*\[@\]\}' "$SCRIPT" | sed 's/^/         /'
	FAILURES=$((FAILURES + 1))
else
	echo "[ PASS ] 配列を 1 つも展開していない（macOS の bash 3.2 で安全）"
fi

# ---------------------------------------------------------------------------
# JSON のエスケープ（唯一の手書きの符号化）。
# ---------------------------------------------------------------------------
printf 'a"b\\c\td\n' >"$WORK/esc.txt"
check "json_string_from_file は引用符・バックスラッシュ・タブを逃がす" \
	"$(json_string_from_file "$WORK/esc.txt")" '"a\"b\\c\td\n"'

# ---------------------------------------------------------------------------
echo
if [ "$FAILURES" -eq 0 ]; then
	echo "$CHECKS check(s) passed."
	exit 0
fi
echo "$FAILURES of $CHECKS check(s) FAILED."
exit 1
