#!/usr/bin/env bash
#
# issue-webhook.sh — issue が立ったことを外部の webhook へ POST する（Claude の
# ルーティンを HTTP で起こすため）。
#
# なぜこれがあるか
# ----------------
# 調査は issue 単位で回す（CLAUDE.md「調査のフロー」）。issue を立ててから調査が
# 始まるまでの間を人が繋ぐ（Claude を開いて「この issue を調べて」と伝える）と、
# 立てたことを忘れた分だけ調査が止まる。**issue が立った瞬間に、その内容を
# Claude のルーティンの入口（webhook）へ投げる**ところまでを CI にやらせる。
#
# 送り先（URL）と認証（トークン）はリポジトリごと・人ごとに違うので**コードに
# 書かない**。シークレット（推奨）か variables から環境変数で受け取り、URL が
# 設定されていなければ黙って何もしない（REQUIRE_URL=yes のときだけ失敗する）。
#
# 何を送るか
# ----------
# `Content-Type: application/json` で、次の形の 1 オブジェクトを POST する。
# `text` はルーティンのプロンプトからそのまま読める要約で、構造化された値は
# `issue` の下にある。
#
#   {
#     "source": "github", "event": "issues", "repository": "owner/repo",
#     "sent_at": "2026-01-02T03:04:05Z", "run_url": "https://github.com/...",
#     "issue": { "number": 12, "title": "...", "url": "...", "state": "open",
#                "author": "...", "labels": ["investigation"],
#                "created_at": "...", "body": "...", "body_truncated": false },
#     "text": "GitHub の issue が立ちました: owner/repo#12 調査: ...\nhttps://..."
#   }
#
# 認証は既定で `Authorization: Bearer <トークン>`。ヘッダ名と接頭辞は変えられる
# （WEBHOOK_TOKEN_HEADER / WEBHOOK_TOKEN_SCHEME）。トークンを URL に埋める形式の
# webhook なら、トークンを空のままにしておけばヘッダは付かない。API キー方式の
# 送り先には WEBHOOK_TOKEN_HEADER=x-api-key / WEBHOOK_TOKEN_SCHEME=none を使う。
#
# 送り先が要求する他のヘッダは WEBHOOK_HEADERS に 1 行 1 つ（`名前: 値`）で足せる。
# **api.anthropic.com は `anthropic-version` を必須にしている**（無いと 400 で
# `anthropic-version: header is required` が返る）ので、そこ宛てで明示が無いときは
# 既定の版（ANTHROPIC_VERSION、既定 2023-06-01）を自動で足す。
#
# 使い方
# ------
#   WEBHOOK_URL=... WEBHOOK_TOKEN=... scripts/issue-webhook.sh   # イベントから送る
#   ISSUE_NUMBER=12 ... scripts/issue-webhook.sh                 # 番号を指定して送る
#   DRY_RUN=yes ... scripts/issue-webhook.sh                     # 送らず中身だけ出す
#
# 環境変数:
#   WEBHOOK_URL           送り先。https:// のみ。空なら何もしない（下の REQUIRE_URL）
#   WEBHOOK_TOKEN         認証トークン。空ならヘッダを付けない
#   WEBHOOK_TOKEN_HEADER  トークンを載せるヘッダ名（既定 Authorization）
#   WEBHOOK_TOKEN_SCHEME  トークンの接頭辞（既定 Bearer。`none` なら素のトークン）
#   WEBHOOK_HEADERS       追加のヘッダ。1 行 1 つ、`名前: 値`。空行と # 始まりは無視
#   ANTHROPIC_VERSION     api.anthropic.com 宛てに足す版（既定 2023-06-01）
#   WEBHOOK_BODY_LIMIT    issue 本文の送信上限（文字数。既定 4000）
#   WEBHOOK_EVENT_PATH    イベント JSON のパス（既定 $GITHUB_EVENT_PATH）
#   ISSUE_NUMBER          指定するとイベントではなく API から issue を取る（gh が要る）
#   REQUIRE_URL           yes なら WEBHOOK_URL が無いときに失敗する（既定 no）
#   DRY_RUN               yes なら POST しない（既定 no）
#   GH_TOKEN              ISSUE_NUMBER を使うときの gh 用トークン（読み取りで足りる）
#   GITHUB_REPOSITORY     owner/repo（Actions が自動で入れる）
#
set -euo pipefail

WEBHOOK_URL="${WEBHOOK_URL:-}"
WEBHOOK_TOKEN="${WEBHOOK_TOKEN:-}"
WEBHOOK_TOKEN_HEADER="${WEBHOOK_TOKEN_HEADER:-Authorization}"
WEBHOOK_TOKEN_SCHEME="${WEBHOOK_TOKEN_SCHEME:-Bearer}"
WEBHOOK_HEADERS="${WEBHOOK_HEADERS:-}"
ANTHROPIC_VERSION="${ANTHROPIC_VERSION:-2023-06-01}"
BODY_LIMIT="${WEBHOOK_BODY_LIMIT:-4000}"
EVENT_PATH="${WEBHOOK_EVENT_PATH:-${GITHUB_EVENT_PATH:-}}"
ISSUE_NUMBER="${ISSUE_NUMBER:-}"
REQUIRE_URL="${REQUIRE_URL:-no}"
DRY_RUN="${DRY_RUN:-no}"
REPO="${GITHUB_REPOSITORY:-min-nano/vectorworks-developer-sdk-reference}"

# 送信の粘り。webhook 側の一時的な失敗（5xx / 429 / 接続断）だけを対象にする。
MAX_ATTEMPTS=4
CONNECT_TIMEOUT=10
MAX_TIME=30

die() {
	printf '::error::issue-webhook: %s\n' "$*" >&2
	exit 1
}

warn() {
	printf '::warning::issue-webhook: %s\n' "$*" >&2
}

# GitHub の run サマリ（人が後から読む用）。ローカル実行では捨てる。
summary() {
	printf '%s\n' "$*"
	if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
		printf '%s\n' "$*" >> "$GITHUB_STEP_SUMMARY"
	fi
}

# curl の設定ファイルに書く値のエスケープ（" と \ だけ）。URL とトークンを
# コマンドライン引数に置かないためにこの経路を使う（ps から見えてしまうため）。
conf_escape() {
	printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

command -v jq >/dev/null 2>&1 || die "jq がありません"
command -v curl >/dev/null 2>&1 || die "curl がありません"

# ---------------------------------------------------------------------------
# 送り先を確かめる
# ---------------------------------------------------------------------------

if [ -z "$WEBHOOK_URL" ]; then
	if [ "$REQUIRE_URL" = "yes" ]; then
		die "WEBHOOK_URL が空です（シークレット CLAUDE_ROUTINE_WEBHOOK_URL を設定してください）。"
	fi
	summary "webhook の URL が設定されていないので何もしません（シークレット CLAUDE_ROUTINE_WEBHOOK_URL）。"
	exit 0
fi

case "$WEBHOOK_URL" in
	https://*) ;;
	*) die "WEBHOOK_URL は https:// で始まる必要があります（トークンを平文で流さないため）。" ;;
esac

case "$BODY_LIMIT" in
	'' | *[!0-9]*) die "WEBHOOK_BODY_LIMIT は数値である必要があります（今の値: $BODY_LIMIT）。" ;;
esac

# Actions はシークレット由来の値を自動で伏せるが、variables から来た場合は伏せない。
# ログへ出す気は無いが、万一混ざったときのために自分でも伏せておく。
if [ -n "${GITHUB_ACTIONS:-}" ]; then
	printf '::add-mask::%s\n' "$WEBHOOK_URL"
	if [ -n "$WEBHOOK_TOKEN" ]; then
		printf '::add-mask::%s\n' "$WEBHOOK_TOKEN"
	fi
fi

if [ -z "$WEBHOOK_TOKEN" ]; then
	warn "トークンが空です。認証ヘッダを付けずに送ります（URL にトークンを含む形式ならこれで正しい）。"
fi

# 送り先のホストだけはログに出す（設定を間違えたときに気付けるように）。
webhook_host="$(printf '%s' "$WEBHOOK_URL" | sed -e 's#^https://##' -e 's#[/?].*$##')"

# api.anthropic.com は anthropic-version を必須にしている（無いと 400）。明示されて
# いなければ既定の版を足す——ここで足さないと、送り先を入れ替えるたびに variable の
# 設定を思い出す羽目になる。
if [ "${webhook_host%%:*}" = "api.anthropic.com" ] &&
	! printf '%s\n' "$WEBHOOK_HEADERS" | grep -qi '^[[:space:]]*anthropic-version[[:space:]]*:'; then
	WEBHOOK_HEADERS="$(printf '%s\nanthropic-version: %s\n' "$WEBHOOK_HEADERS" "$ANTHROPIC_VERSION")"
fi

# 追加ヘッダは組み立てる前に検めておく（送ってから 400 で気付くのは遅い）。値は
# 伏せ、名前だけをログに出す。
header_names=""
while IFS= read -r header_line; do
	header_line="${header_line%$'\r'}"
	case "$header_line" in
		'' | '#'*) continue ;;
		*:*) ;;
		*) die "WEBHOOK_HEADERS の行に : がありません: $header_line" ;;
	esac
	header_names="${header_names}${header_names:+, }${header_line%%:*}"
done <<< "$WEBHOOK_HEADERS"
if [ -n "$header_names" ]; then
	echo "追加ヘッダ: $header_names"
fi

# ---------------------------------------------------------------------------
# issue を取る（イベント JSON、または番号指定で API から）
# ---------------------------------------------------------------------------

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
issue_file="$workdir/issue.json"

if [ -n "$ISSUE_NUMBER" ]; then
	command -v gh >/dev/null 2>&1 || die "gh がありません（ISSUE_NUMBER を使うときに要る）"
	case "$ISSUE_NUMBER" in
		'' | *[!0-9]*) die "ISSUE_NUMBER は数値である必要があります（今の値: $ISSUE_NUMBER）。" ;;
	esac
	gh api "repos/${REPO}/issues/${ISSUE_NUMBER}" > "$issue_file" ||
		die "issue #${ISSUE_NUMBER} を取得できません（$REPO）。"
else
	[ -n "$EVENT_PATH" ] || die "イベント JSON のパスが分かりません（GITHUB_EVENT_PATH も ISSUE_NUMBER も無い）。"
	[ -f "$EVENT_PATH" ] || die "イベント JSON がありません: $EVENT_PATH"
	jq -e '.issue' "$EVENT_PATH" > "$issue_file" ||
		die "イベント JSON に .issue がありません（issues イベント以外から呼ばれた？）: $EVENT_PATH"
fi

jq -e '.number' "$issue_file" >/dev/null || die "issue の番号が読めません。"

# ---------------------------------------------------------------------------
# 送る中身を組み立てる
# ---------------------------------------------------------------------------

payload_file="$workdir/payload.json"
jq -n \
	--slurpfile issue "$issue_file" \
	--arg repo "$REPO" \
	--arg event "${GITHUB_EVENT_NAME:-local}" \
	--arg action "$(jq -r '.action // ""' "${EVENT_PATH:-/dev/null}" 2>/dev/null || true)" \
	--arg run_url "${GITHUB_SERVER_URL:-https://github.com}/${REPO}/actions/runs/${GITHUB_RUN_ID:-0}" \
	--arg sent_at "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" \
	--argjson body_limit "$BODY_LIMIT" '
	($issue[0]) as $i
	| ($i.body // "") as $body
	| ($body | length > $body_limit) as $cut
	| {
		source: "github",
		event: $event,
		action: $action,
		repository: $repo,
		sent_at: $sent_at,
		run_url: $run_url,
		issue: {
			number: $i.number,
			title: ($i.title // ""),
			url: ($i.html_url // ""),
			state: ($i.state // ""),
			author: ($i.user.login // ""),
			labels: [(($i.labels // [])[] | if type == "object" then (.name // "") else . end)],
			created_at: ($i.created_at // ""),
			body: (if $cut then ($body[0:$body_limit] + "\n…（以下省略）") else $body end),
			body_truncated: $cut
		},
		text: "GitHub の issue が立ちました: \($repo)#\($i.number) \($i.title // "")\n\($i.html_url // "")"
	}' > "$payload_file" || die "送る中身を組み立てられません。"

issue_number="$(jq -r '.issue.number' "$payload_file")"
issue_title="$(jq -r '.issue.title' "$payload_file")"

echo "送り先: https://${webhook_host}/…（URL は伏せています）"
echo "送る中身（本文は省略）:"
jq 'del(.issue.body)' "$payload_file"

if [ "$DRY_RUN" = "yes" ]; then
	summary "DRY_RUN のため送信しませんでした（${REPO}#${issue_number} ${issue_title}）。"
	exit 0
fi

# ---------------------------------------------------------------------------
# 送る
# ---------------------------------------------------------------------------

conf_file="$workdir/curl.conf"
(
	umask 077
	{
		printf 'url = "%s"\n' "$(conf_escape "$WEBHOOK_URL")"
		printf 'header = "Content-Type: application/json"\n'
		printf 'header = "User-Agent: vectorworks-developer-sdk-reference/issue-webhook"\n'
		while IFS= read -r header_line; do
			header_line="${header_line%$'\r'}"
			case "$header_line" in
				'' | '#'*) continue ;;
			esac
			printf 'header = "%s"\n' "$(conf_escape "$header_line")"
		done <<< "$WEBHOOK_HEADERS"
		if [ -n "$WEBHOOK_TOKEN" ]; then
			if [ "$WEBHOOK_TOKEN_SCHEME" = "none" ]; then
				auth_value="$WEBHOOK_TOKEN"
			else
				auth_value="${WEBHOOK_TOKEN_SCHEME} ${WEBHOOK_TOKEN}"
			fi
			printf 'header = "%s: %s"\n' \
				"$(conf_escape "$WEBHOOK_TOKEN_HEADER")" "$(conf_escape "$auth_value")"
		fi
	} > "$conf_file"
)

resp_file="$workdir/response.txt"
attempt=1
delay=2
while :; do
	if ! http_code="$(curl --silent --show-error \
		--config "$conf_file" \
		--request POST \
		--data-binary "@$payload_file" \
		--connect-timeout "$CONNECT_TIMEOUT" \
		--max-time "$MAX_TIME" \
		--output "$resp_file" \
		--write-out '%{http_code}')"; then
		http_code="000"
	fi
	http_code="${http_code:-000}"

	case "$http_code" in
		2??)
			summary "webhook へ送りました（HTTP ${http_code}・${REPO}#${issue_number} ${issue_title}）。"
			echo "応答（先頭 500 文字）:"
			head -c 500 "$resp_file" 2>/dev/null || true
			echo
			exit 0
			;;
	esac

	# 一時的な失敗（接続断・混雑・サーバ側の障害）だけ粘る。4xx は設定か中身の
	# 誤りなので、何度送っても同じ。
	retryable=no
	case "$http_code" in
		000 | 408 | 429 | 5??) retryable=yes ;;
	esac

	if [ "$retryable" = "no" ] || [ "$attempt" -ge "$MAX_ATTEMPTS" ]; then
		echo "応答（先頭 500 文字）:" >&2
		head -c 500 "$resp_file" >&2 2>/dev/null || true
		echo >&2
		die "webhook への送信に失敗しました（HTTP ${http_code}・${attempt} 回目で打ち切り）。"
	fi

	echo "送信に失敗した（HTTP ${http_code}）。${delay} 秒待って再試行する（${attempt}/$((MAX_ATTEMPTS - 1))）。" >&2
	sleep "$delay"
	delay=$((delay * 2))
	attempt=$((attempt + 1))
done
