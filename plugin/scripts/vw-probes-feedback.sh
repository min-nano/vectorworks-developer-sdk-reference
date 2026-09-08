#!/usr/bin/env bash
#
# vw-probes-feedback.sh — プローブの結果を、そのプローブが来た PR へ投稿する（macOS）。
#
# プラグイン本体（plugin/src/Feedback.cpp）から**非対話**で呼ばれる裏方。ダイアログは
# すべてプラグイン側が Vectorworks のネイティブダイアログで出すので、こちらは機械可読な
# 行を標準出力へ出すだけで、自分では何も表示しない（vw-probes-update.sh と同じ流儀）。
#
#   token-status                  トークンの出どころ（keychain / gh / env / none）と使えるか
#   login <token-file>            ファイルのトークンをキーチェーンへ入れて、ファイルを消す
#   logout                        キーチェーンから消す
#   find-pr <repo> <branch>       そのブランチの open な PR 番号を引く
#   post <repo> <pr> <body-file>  PR（= issue）へコメントを 1 通投稿する
#
# 成功は**素の `ok` 1 行**（post は先に url= を出す）。失敗は `error=<理由>` で、
# **終了コードは 0** のまま——何を見せるかはプラグイン側が決める。
#
# 【宛先を尋ねない】PR 番号は**プローブの出所**が持っている（ビルドのときに決まる。
# scripts/gather-probes.sh）ので、ふつうは引数として渡ってくる。`find-pr` はその出所が
# 無いときの逃げ道で、**PR のブランチでビルドしたプラグインを手で入れて確かめるとき**に
# 使う——そのビルドではプローブが群 main に入る（＝出所に PR 番号が無い）ので、
# 動いているビルドのブランチから PR を引く（plugin/src/Feedback.h「宛先の決め方」）。
#
# 【トークンをコマンドラインに乗せない】`login` が受け取るのは*ファイルのパス*で、中身は
# 読んだ直後に消す——引数はプロセス一覧（ps）から誰にでも見えるので、そこへ秘密を置いては
# ならない。表示・ログにもトークンは一切出さない。
#
# トークンの探索順（最初に見つかったものを使う）:
#   1. 環境変数 VW_PROBE_FEEDBACK_TOKEN（Vectorworks を端末から起動したとき用）
#   2. キーチェーン（login で入れたもの。GUI から使う常用の経路）
#   3. gh CLI の認証（開発機に gh が入っていれば設定は要らない）
# 必要な権限は**そのリポジトリの Pull requests への読み書き**だけ（fine-grained PAT なら
# "Pull requests: Read and write"）。それ以上を要求しない。
#
# 【配列を使わない】macOS が積んでいる /bin/bash は 3.2 で（プラグインはこれで起動する。
# plugin/src/BundledScript.cpp）、`set -u` の下で**空の配列を展開すると "unbound variable"
# で即死する**（bash 4.4 で直った古い不具合）。実プラグイン側で実際に踏んでいるので、
# **この 1 本では配列を使わない**（plugin/tests/vw-probes-feedback.test.sh がその不在を
# 検査する）。
#
# 必要なもの: macOS に最初から入っているもの（curl / plutil / awk / security）だけ。
#
# 環境変数で上書きできる:
#   VW_REPO                        owner/repo（引数の repo が空のときの既定）
#   VW_PROBE_FEEDBACK_TOKEN        トークン（探索順 1）
#   VW_PROBE_FEEDBACK_KEYCHAIN     キーチェーンの service 名（既定 VwSdkProbesFeedback）
#
set -uo pipefail

VW_REPO="${VW_REPO:-min-nano/vectorworks-developer-sdk-reference}"
VW_API="https://api.github.com"
# キーチェーンの service 名は**識別子なので据え置く**。表示名やファイル名が変わっても
# 付け替えない——付け替えた瞬間、入っているトークンが行方不明になる。
VW_PROBE_FEEDBACK_KEYCHAIN="${VW_PROBE_FEEDBACK_KEYCHAIN:-VwSdkProbesFeedback}"

# ---------------------------------------------------------------------------
# トークンの取り出し。**標準出力へは決して流さない。**
# ---------------------------------------------------------------------------

# gh CLI は GUI アプリの PATH には入っていないのが普通（Vectorworks は LaunchServices から
# 起動され、PATH は /usr/bin:/bin:/usr/sbin:/sbin だけ）。だから探す場所を並べておく。
gh_path() {
	local candidate=""
	for candidate in /opt/homebrew/bin/gh /usr/local/bin/gh /usr/bin/gh; do
		[ -x "$candidate" ] && {
			printf '%s' "$candidate"
			return 0
		}
	done
	command -v gh 2>/dev/null || return 1
}

token_from_keychain() {
	security find-generic-password -s "$VW_PROBE_FEEDBACK_KEYCHAIN" -w 2>/dev/null || return 1
}

token_from_gh() {
	local gh
	gh="$(gh_path)" || return 1
	"$gh" auth token 2>/dev/null || return 1
}

# token_source: どこから取れるか（取れなければ "none"）。トークン自体は出さない。
token_source() {
	[ -n "${VW_PROBE_FEEDBACK_TOKEN:-}" ] && {
		echo "env"
		return 0
	}
	[ -n "$(token_from_keychain)" ] && {
		echo "keychain"
		return 0
	}
	[ -n "$(token_from_gh)" ] && {
		echo "gh"
		return 0
	}
	echo "none"
}

# resolve_token: 実際のトークン（無ければ空で 1 を返す）。
resolve_token() {
	local t
	if [ -n "${VW_PROBE_FEEDBACK_TOKEN:-}" ]; then
		printf '%s' "$VW_PROBE_FEEDBACK_TOKEN"
		return 0
	fi
	t="$(token_from_keychain)" && [ -n "$t" ] && {
		printf '%s' "$t"
		return 0
	}
	t="$(token_from_gh)" && [ -n "$t" ] && {
		printf '%s' "$t"
		return 0
	}
	return 1
}

# ---------------------------------------------------------------------------
# JSON。読むのは plutil（macOS 同梱で JSON をそのまま読める。vw-probes-update.sh と同じ）、
# 書くのは awk——**本文は任意のテキスト**（プローブのログ）なので、素朴な文字列連結では
# 必ず壊れる。エスケープを 1 か所に閉じ込める。
# ---------------------------------------------------------------------------

jval() { # json-file, keypath -> raw scalar value (empty if missing)
	plutil -extract "$2" raw -o - "$1" 2>/dev/null || true
}

# json_string_from_file: ファイルの中身を JSON 文字列リテラル（引用符つき）にする。
# LC_ALL=C はバイト単位で扱わせるため——UTF-8 の各バイトは 0x80 以上なので、下の
# 制御文字クラスに巻き込まれない（日本語の本文がそのまま通る）。
json_string_from_file() { # file
	LC_ALL=C awk '
		BEGIN { printf "\"" }
		{
			s = $0
			gsub(/\\/, "\\\\", s)
			gsub(/"/, "\\\"", s)
			gsub(/\t/, "\\t", s)
			gsub(/\r/, "", s)
			gsub(/[\001-\010\013\014\016-\037]/, "", s)
			printf "%s\\n", s
		}
		END { printf "\"" }
	' "$1"
}

# ---------------------------------------------------------------------------
# モード
# ---------------------------------------------------------------------------

# token-status: 使えるトークンがあるか。**トークンは出さない。**
mode_token_status() {
	local src
	src="$(token_source)"
	echo "source=${src}"
	if [ "$src" = "none" ]; then
		echo "ok=no"
	else
		echo "ok=yes"
	fi
}

# login <token-file>: ファイルのトークンをキーチェーンへ。**読んだファイルは必ず消す**
# （プラグインが一時ファイルへ書いて渡す。引数に秘密を乗せないための経路）。
mode_login() {
	local file="${1:-}"
	if [ -z "$file" ] || [ ! -f "$file" ]; then
		echo "error=トークンのファイルが見つかりません。"
		return 0
	fi
	local token
	token="$(tr -d '\r\n' <"$file")"
	rm -f "$file"
	if [ -z "$token" ]; then
		echo "error=トークンが空です。"
		return 0
	fi
	# 既存の項目があれば置き換える（-U）。-a（アカウント名）は表示のためだけのもので、
	# **$USER が無い環境でも落とさない**（Vectorworks は LaunchServices から起動される
	# ので、環境変数は端末より痩せている）。
	local account="${USER:-vectorworks}"
	if security add-generic-password -U -s "$VW_PROBE_FEEDBACK_KEYCHAIN" \
		-a "$account" -w "$token" >/dev/null 2>&1; then
		echo "ok"
	else
		echo "error=キーチェーンへ保存できませんでした。"
	fi
}

mode_logout() {
	security delete-generic-password -s "$VW_PROBE_FEEDBACK_KEYCHAIN" >/dev/null 2>&1 || true
	echo "ok"
}

# find-pr <repo> <branch>: そのブランチの open な PR。**番号を人に打たせないため**の口で、
# 見つからなければ error= を返す（呼び出し側は投稿しないだけ）。
#
# **トークンは要らない**（公開リポジトリの open な PR を引くだけ）。あれば付けるが、
# 無くても引けなければならない——トークンを登録する前に宛先が分かる必要があるためで、
# 実プラグイン側では round 1 でここが死に、番号を手入力させてしまった。
mode_find_pr() {
	local repo="${1:-}" branch="${2:-}"
	[ -n "$repo" ] || repo="$VW_REPO"
	if [ -z "$branch" ]; then
		echo "error=ブランチが指定されていません。"
		return 0
	fi
	local owner="${repo%%/*}"
	local token
	token="$(resolve_token || true)"
	local url="${VW_API}/repos/${repo}/pulls?state=open&head=${owner}:${branch}"

	local f got
	f="$(mktemp)"
	got=1
	if [ -n "$token" ]; then
		curl -fsSL --max-time 20 --retry 2 \
			-H "Authorization: Bearer ${token}" \
			-H "Accept: application/vnd.github+json" "$url" -o "$f" && got=0
	else
		curl -fsSL --max-time 20 --retry 2 \
			-H "Accept: application/vnd.github+json" "$url" -o "$f" && got=0
	fi
	if [ "$got" -ne 0 ]; then
		rm -f "$f"
		echo "error=PR を検索できませんでした（ネットワークか権限）。"
		return 0
	fi
	local number title
	number="$(jval "$f" "0.number")"
	title="$(jval "$f" "0.title")"
	rm -f "$f"
	if [ -z "$number" ]; then
		echo "error=ブランチ ${branch} に open な PR がありません。"
		return 0
	fi
	echo "pr=${number}"
	[ -n "$title" ] && echo "title=${title}"
	echo "ok"
}

# post <repo> <pr> <body-file>: PR へコメントを 1 通。
#   url=<コメントの URL>
#   ok
mode_post() {
	local repo="${1:-}" number="${2:-}" body="${3:-}"
	[ -n "$repo" ] || repo="$VW_REPO"
	if [ -z "$number" ] || [ -z "$body" ] || [ ! -f "$body" ]; then
		echo "error=引数が不足しています。"
		return 0
	fi
	local token
	token="$(resolve_token || true)"
	if [ -z "$token" ]; then
		echo "error=GitHub のトークンがありません（先に登録してください）。"
		return 0
	fi

	local payload
	payload="$(mktemp)"
	{
		printf '{"body":'
		json_string_from_file "$body"
		printf '}'
	} >"$payload"

	local out code
	out="$(mktemp)"
	code="$(curl -sS --max-time 60 --retry 2 -o "$out" -w '%{http_code}' \
		-X POST \
		-H "Authorization: Bearer ${token}" \
		-H "Accept: application/vnd.github+json" \
		-H "Content-Type: application/json" \
		--data-binary "@${payload}" \
		"${VW_API}/repos/${repo}/issues/${number}/comments" 2>/dev/null)"
	rm -f "$payload"

	if [ "$code" != "201" ]; then
		# GitHub の言い分をそのまま渡す（権限不足か PR 違いかが、これで切り分けられる）。
		local message
		message="$(jval "$out" "message")"
		rm -f "$out"
		[ -n "$message" ] || message="HTTP ${code}"
		echo "error=コメントを投稿できませんでした（${message}）。"
		return 0
	fi
	local url
	url="$(jval "$out" "html_url")"
	rm -f "$out"
	[ -n "$url" ] && echo "url=${url}"
	echo "ok"
}

# ---------------------------------------------------------------------------
main() {
	command -v curl >/dev/null 2>&1 || {
		echo "error=curl が見つかりません（macOS で実行してください）。"
		exit 0
	}

	local mode="${1:-}"
	shift || true
	case "$mode" in
		token-status) mode_token_status ;;
		login) mode_login "${1:-}" ;;
		logout) mode_logout ;;
		find-pr) mode_find_pr "${1:-}" "${2:-}" ;;
		post) mode_post "${1:-}" "${2:-}" "${3:-}" ;;
		*) echo "error=不明なモード: '${mode}'（token-status / login / logout / find-pr / post）。" ;;
	esac
}

# 直接実行されたときだけ main を走らせる（テストはこのファイルを source して、curl /
# plutil / security を差し替えたまま各モードを叩く）。
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
	main "$@"
fi
