#!/usr/bin/env bash
#
#	vw-probes-update.test.sh
#
#	同梱スクリプト（plugin/scripts/vw-probes-update.sh）の単体テスト——実機確認プラグインの
#	入れ替え（plugin/src/Update.h）の裏方のうち、**取得と、失敗したときの理由**を押さえる。
#
#	ここを試験にした理由: 「ネットワークは生きているのにリリースを取得できませんでした」と
#	だけ出て、原因が分からない状態が実際に起きた。**理由を具体的に言うこと**と、
#	**API が駄目でも決まった URL から引けること**は、この道具が自分で直せるかどうかを
#	決めるので、振る舞いとして固定する。
#
#	スクリプトは**source して**（本物の main は末尾で守られている）、いちばん外側の
#	入出力だけを差し替える。だから引数の組み立て・応答の扱い・逃げ道の順序といった
#	**本当のロジックはそのまま走る**:
#
#	  * curl   … 網の境目。-o / -D / -w（HTTP コード）の使い方を解し、URL の宛先ごとに
#	             用意した終了コード・HTTP コード・本文・応答ヘッダを返す。
#	  * plutil … macOS 専用の JSON 読み。python3 で代用する（Linux のランナーで走る）。
#
#	**bash 3.2 で動くこと**（＝配列を書いていないこと）も検査する。本番で走るのは
#	macOS の /bin/bash 3.2 で、そちらだけ `set -u` + 空配列の展開で即死する。
#
#	走らせ方（CI の lint ワークフローも同じ）:
#	    bash plugin/tests/vw-probes-update.test.sh
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="${HERE}/../scripts/vw-probes-update.sh"

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
# 環境: 入れ替え先は作業ディレクトリ（本物の Plug-Ins には触らない）。
# ---------------------------------------------------------------------------
PLUGINS="$WORK/plugins"
mkdir -p "$PLUGINS"
export VW_REPO="min-nano/vectorworks-developer-sdk-reference"
export VW_PLUGINS_DIR="$PLUGINS"
export VW_TAG="probes"

# shellcheck source=../scripts/vw-probes-update.sh
. "$SCRIPT"

DL_BASE="https://github.com/min-nano/vectorworks-developer-sdk-reference/releases/download/probes"

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

# curl の代用。宛先（api.github.com か、決まった資産の URL か）で応答を選ぶ。
API_EXIT=0
API_CODE="200"
API_BODY=""
API_HDR=""
DL_EXIT=0
DL_CODE="200"
DL_BODY=""
DL_HDR=""
curl() {
	local out="" hdr="" wants_code=0 url="" arg
	while [ "$#" -gt 0 ]; do
		arg="$1"
		case "$arg" in
			-o)
				out="${2:-}"
				shift
				;;
			-D)
				hdr="${2:-}"
				shift
				;;
			-w) wants_code=1 ;;
			-A | -H | --max-time | --retry) shift ;;
			https://*) url="$arg" ;;
		esac
		shift
	done

	local rc code body headers
	case "$url" in
		https://api.github.com/*)
			rc="$API_EXIT"
			code="$API_CODE"
			body="$API_BODY"
			headers="$API_HDR"
			;;
		*)
			rc="$DL_EXIT"
			code="$DL_CODE"
			body="$DL_BODY"
			headers="$DL_HDR"
			;;
	esac

	if [ -n "$hdr" ]; then
		printf '%s' "$headers" >"$hdr"
	fi
	if [ "$rc" -ne 0 ]; then
		# 本物と同じく、警告が先に出て**最後の行**が理由になる形にする。
		echo "Warning: Transient problem: timeout Will retry in 1 seconds." >&2
		echo "curl: (${rc}) 模擬エラー" >&2
		if [ "$wants_code" -eq 1 ]; then
			printf '000'
		fi
		return "$rc"
	fi
	if [ -n "$out" ]; then
		printf '%s' "$body" >"$out"
	fi
	if [ "$wants_code" -eq 1 ]; then
		printf '%s' "$code"
	fi
	return 0
}

# ---------------------------------------------------------------------------
# q: API が答えるとき（ふだんの道）。
# ---------------------------------------------------------------------------
printf 'build=INSTALLED1\n' >"$PLUGINS/VwSdkProbes.probes.txt"
API_BODY='{"name":"Probes (abc1234)","body":"<!-- vw-probes\nbuild=BUILD123\nshell=SHELL123\nprobes=alpha,beta\n-->\n","assets":[{"name":"VwSdkProbes.vlb.zip","browser_download_url":"https://example.invalid/vlb.zip"},{"name":"VwSdkProbes.vwlibrary.zip","browser_download_url":"https://example.invalid/vwlibrary.zip"}]}'
out="$(mode_q)"
check "q: API から本体・殻・資産を読む" "$out" "installed=INSTALLED1
latest=BUILD123
installedShell=none
latestShell=SHELL123
url=https://example.invalid/vwlibrary.zip
title=Probes (abc1234)
probes=alpha,beta"

# ---------------------------------------------------------------------------
# q: API が呼び出し上限で断ってきたとき——**決まった URL から引き直して先へ進む**。
# ---------------------------------------------------------------------------
API_CODE="403"
API_HDR="HTTP/2 403
x-ratelimit-remaining: 0
x-ratelimit-reset: $(($(date +%s) + 600))
"
DL_BODY='build=BUILD777
shell=SHELL777
probes=gamma
title=Probes (zzz + PR 9)
'
out="$(mode_q)"
check "q: API が駄目でも資産の URL から素性を読む" "$out" "installed=INSTALLED1
latest=BUILD777
installedShell=none
latestShell=SHELL777
url=${DL_BASE}/VwSdkProbes.vwlibrary.zip
title=Probes (zzz + PR 9)
probes=gamma"
check_not_contains "逃げ道が通ったときは error を出さない" "$out" "error="

# ---------------------------------------------------------------------------
# q: 両方駄目なとき——**両方の理由を 1 行で**言う（error= は 1 行しか読まれない）。
# ---------------------------------------------------------------------------
DL_CODE="404"
DL_BODY=""
out="$(mode_q)"
check "両方駄目なら error= の 1 行だけ" "$(printf '%s' "$out" | wc -l | tr -d ' ')" "0"
check_contains "API の HTTP コードを言う" "$out" "GitHub API: HTTP 403"
check_contains "呼び出し上限だと分かるように言う" "$out" "呼び出し上限に達しました"
check_contains "いつ戻るかも言う" "$out" "あと 10 分で戻ります"
check_contains "直接取得の HTTP コードも言う" "$out" "直接取得: HTTP 404"
check_not_contains "「ネットワークを確認してください」で終わらせない" "$out" \
	"ネットワークを確認してください"

# 上限ではない 403（プロキシ・社内フィルタ）は別の理由にする。
API_HDR="HTTP/2 403
x-ratelimit-remaining: 57
"
out="$(mode_q)"
check_contains "上限でない 403 は拒否として言う" "$out" "HTTP 403: 拒否されました"

# ---------------------------------------------------------------------------
# q: curl そのものが失敗したとき——**終了コードと、それが何かを言う**。
# ---------------------------------------------------------------------------
API_EXIT=6
DL_EXIT=6
out="$(mode_q)"
check_contains "curl の終了コードを言う" "$out" "curl 終了コード 6"
check_contains "終了コードの意味も言う" "$out" "名前解決に失敗しました（DNS）"
check_contains "curl のメッセージは最後の行を採る" "$out" "curl: (6) 模擬エラー"
check_not_contains "--retry の警告は理由にしない" "$out" "Transient problem"

API_EXIT=60
DL_EXIT=60
out="$(mode_q)"
check_contains "証明書の失敗は TLS だと言う" "$out" "TLS の検証に失敗しました"

API_EXIT=28
DL_EXIT=28
out="$(mode_q)"
check_contains "打ち切りは時間切れだと言う" "$out" "時間内に応答がありません"

API_EXIT=0
DL_EXIT=0

# ---------------------------------------------------------------------------
# do-install: **落とせなかったときも理由を言う**（入れ替えは網の上でも起きる）。
# ---------------------------------------------------------------------------
DL_CODE="404"
out="$(mode_do_install "https://example.invalid/VwSdkProbes.vwlibrary.zip")"
check "落とせなかった理由も言う" "$out" \
	"error=ダウンロードに失敗しました（HTTP 404: 見つかりません（リリースか資産がまだありません））。"

DL_EXIT=7
out="$(mode_do_install_payload "https://example.invalid/VwSdkProbes.vwlibrary.zip")"
check_contains "本体だけの入れ替えでも理由を言う" "$out" "curl 終了コード 7"
DL_EXIT=0

# ---------------------------------------------------------------------------
# fetch_reason 単体（理由の文言は 1 行であること）。
# ---------------------------------------------------------------------------
: >"$WORK/empty-hdr"
check "404 は「見つかりません」" \
	"$(fetch_reason 0 404 "" "$WORK/empty-hdr")" \
	"HTTP 404: 見つかりません（リリースか資産がまだありません）"
check "5xx は GitHub 側の障害" \
	"$(fetch_reason 0 503 "" "$WORK/empty-hdr")" \
	"HTTP 503: GitHub 側の一時的な障害"
check "応答が無ければそう言う" \
	"$(fetch_reason 0 000 "" "$WORK/empty-hdr")" "応答がありません"
check "知らない終了コードでも黙らない" \
	"$(fetch_reason 47 000 "curl: (47) too many redirects" "$WORK/empty-hdr")" \
	"curl 終了コード 47（curl が失敗しました）: curl: (47) too many redirects"

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
echo
if [ "$FAILURES" -eq 0 ]; then
	echo "すべて通りました（${CHECKS} 件）。"
	exit 0
fi
echo "${FAILURES} 件失敗しました（${CHECKS} 件中）。"
exit 1
