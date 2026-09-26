#!/usr/bin/env bash
#
# sdk-index-sync.sh — SDK の宣言索引（`SDK Index/`）を作り直し、変わっていれば PR を立てる。
#
# なぜこれがあるか
# ----------------
# リモートセッションには SDK が無いので、宣言（シグネチャ・enum の値・クラスの形）を
# 確かめるたびに ci-debug を往復させていた。宣言の一覧をリポジトリに置けば `Grep` で
# その場で引ける（CLAUDE.md「SDK の宣言索引」）。ただし SDK の URL は "latest" で、
# 版が上がると中身が黙って差し替わる。だから週次で作り直し、**差分があったら PR に
# して人の目に掛ける**（＝ SDK の版上げに気付く仕組みも兼ねる）。
#
# 何をするか
# ----------
#   1. main から作業ブランチ（既定 `sdk-index`）を作り、scripts/sdk-index.py で
#      `SDK Index/` を作り直す。
#   2. main と比べて変わっていなければ何もしない（PR も作らない）。
#   3. 変わっていればコミットして push し、PR を作る（既に開いていれば更新する）。
#
# 作業ブランチは毎回作り直して force push する（upstream-sync.sh と同じ流儀）。ただし
# **ブランチに bot 以外のコミットがあるときは触らない**——人が足したものを消さないため。
#
# 使い方
# ------
#   VW_SDK_DIR=... scripts/sdk-index-sync.sh              PR を作る／更新する
#   DRY_RUN=yes VW_SDK_DIR=... scripts/sdk-index-sync.sh  push も PR 作成もせず差分だけ出す
#
# 環境変数:
#   VW_SDK_DIR       scripts/fetch-vw-sdk.sh が用意した SDK（必須）
#   GH_TOKEN         gh 用のトークン（contents: write / pull-requests: write）
#   BASE_BRANCH      取り込み先（既定 main）
#   INDEX_BRANCH     作業ブランチ名（既定 sdk-index）
#   DRY_RUN          yes なら push / PR 作成をしない（既定 no）
#   GITHUB_REPOSITORY  自分の owner/repo（Actions が自動で入れる）
#
set -euo pipefail

SDK_DIR="${VW_SDK_DIR:?VW_SDK_DIR is not set}"
BASE_BRANCH="${BASE_BRANCH:-main}"
INDEX_BRANCH="${INDEX_BRANCH:-sdk-index}"
DRY_RUN="${DRY_RUN:-no}"
REPO="${GITHUB_REPOSITORY:-min-nano/vectorworks-developer-sdk-reference}"
INDEX_DIR="SDK Index"

BOT_NAME="github-actions[bot]"
BOT_EMAIL="41898282+github-actions[bot]@users.noreply.github.com"

cd "$(dirname "$0")/.."

die() {
	printf '::error::sdk-index-sync: %s\n' "$*" >&2
	exit 1
}

summary() {
	printf '%s\n' "$*"
	if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
		printf '%s\n' "$*" >>"$GITHUB_STEP_SUMMARY"
	fi
}

push_with_retry() {
	local delay=2 attempt
	for attempt in 1 2 3 4; do
		if git "$@"; then
			return 0
		fi
		if [ "$attempt" -eq 4 ]; then
			return 1
		fi
		echo "push に失敗した。${delay} 秒待って再試行する（$attempt/3）。" >&2
		sleep "$delay"
		delay=$((delay * 2))
	done
}

command -v git >/dev/null 2>&1 || die "git がありません"
command -v python3 >/dev/null 2>&1 || die "python3 がありません"
if [ "$DRY_RUN" != "yes" ]; then
	command -v gh >/dev/null 2>&1 || die "gh がありません"
	[ -n "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ] || die "GH_TOKEN が設定されていません"
fi

git config user.name "$BOT_NAME"
git config user.email "$BOT_EMAIL"

git fetch --no-tags origin "+refs/heads/${BASE_BRANCH}:refs/remotes/origin/${BASE_BRANCH}" ||
	die "origin/$BASE_BRANCH を取得できません"
base_head="$(git rev-parse "refs/remotes/origin/${BASE_BRANCH}")"

# ---------------------------------------------------------------------------
# 既にある作業ブランチを尊重する
# ---------------------------------------------------------------------------

index_head=""
if [ -n "$(git ls-remote --heads origin "$INDEX_BRANCH")" ]; then
	git fetch --no-tags origin "+refs/heads/${INDEX_BRANCH}:refs/remotes/origin/${INDEX_BRANCH}" ||
		die "origin/$INDEX_BRANCH を取得できません"
	index_head="$(git rev-parse "refs/remotes/origin/${INDEX_BRANCH}")"
	foreign="$(git log --format='%ae' "$index_head" --not "$base_head" |
		grep -v -F -x "$BOT_EMAIL" || true)"
	if [ -n "$foreign" ]; then
		summary "$INDEX_BRANCH に $BOT_NAME 以外のコミットがあるため、このブランチには触りません。そちらの PR を片付けてください。"
		exit 0
	fi
fi

# ---------------------------------------------------------------------------
# main から作り直して索引を生成する
# ---------------------------------------------------------------------------

git checkout -B "$INDEX_BRANCH" "refs/remotes/origin/${BASE_BRANCH}"
python3 scripts/sdk-index.py "$SDK_DIR" "$INDEX_DIR"
git add -A -- "$INDEX_DIR"

if git diff --cached --quiet; then
	summary "SDK の宣言索引は main と同じです（変更なし）。"
	exit 0
fi

stat="$(git diff --cached --shortstat)"
origin_folder="$(sed -n 's/^folder=//p' "$SDK_DIR/sdk-origin.txt" 2>/dev/null || true)"
origin_modified="$(sed -n 's/^last_modified=//p' "$SDK_DIR/sdk-origin.txt" 2>/dev/null || true)"

git commit --no-verify -q -m "chore: SDK の宣言索引を作り直す (${origin_folder:-SDK})

scripts/sdk-index-sync.sh による自動生成。"
new_head="$(git rev-parse HEAD)"

# 前回と同じ内容（親も木も同じ）なら push も PR 更新もしない。
push_needed=yes
if [ -n "$index_head" ] &&
	[ "$(git rev-parse "${index_head}^")" = "$base_head" ] &&
	[ "$(git rev-parse "${index_head}^{tree}")" = "$(git rev-parse "${new_head}^{tree}")" ]; then
	push_needed=no
fi

# ---------------------------------------------------------------------------
# PR 本文
# ---------------------------------------------------------------------------

body="$(mktemp)"
trap 'rm -f "$body"' EXIT

{
	echo "Vectorworks SDK の宣言索引（\`${INDEX_DIR}/\`）を作り直しました（\`scripts/sdk-index-sync.sh\` による自動生成）。"
	echo
	echo "| | |"
	echo "| --- | --- |"
	echo "| SDK の版（zip 内のフォルダ名） | \`${origin_folder:-(不明)}\` |"
	echo "| Last-Modified | ${origin_modified:-(不明)} |"
	echo "| 差分 | ${stat} |"
	echo
	echo "## 変わったファイル"
	echo
	echo '```'
	git diff --stat=200 "${base_head}" "${new_head}" -- "$INDEX_DIR" | head -n 80
	echo '```'
	echo
	echo "## 確認すること"
	echo
	echo "- 生成物なので手で直さない。形を変えたいときは \`scripts/sdk-index.py\` を直す。"
	echo "- **SDK の版が変わっている**（\`INDEX.md\` のフォルダ名・Last-Modified が動いている）なら、\`Findings/\` の記述が古くなっていないかを見る。気になる点は [調査 issue](https://github.com/${REPO}/issues/new?template=investigation.md) を立てる。"
	echo "- 実機確認の要らない変更なので、CI が green ならマージしてよい（CLAUDE.md「PR とマージ」4）。"
	echo
	echo "> [!NOTE]"
	echo "> この PR は \`GITHUB_TOKEN\` で作られているため、**lint の CI は自動では走りません**（GitHub の仕様）。走らせたい場合は PR を close → reopen してください。"
} >"$body"

title="chore: SDK の宣言索引を更新する (${origin_folder:-SDK})"

if [ "$DRY_RUN" = "yes" ]; then
	summary "DRY_RUN=yes のため push も PR 作成もしません。push_needed=$push_needed ($stat)"
	echo "--- PR タイトル ---"
	echo "$title"
	echo "--- PR 本文 ---"
	cat "$body"
	exit 0
fi

if [ "$push_needed" = "yes" ]; then
	push_args=(push origin "HEAD:refs/heads/${INDEX_BRANCH}")
	if [ -n "$index_head" ]; then
		push_args=(push "--force-with-lease=refs/heads/${INDEX_BRANCH}:${index_head}"
			origin "HEAD:refs/heads/${INDEX_BRANCH}")
	fi
	push_with_retry "${push_args[@]}" || die "$INDEX_BRANCH の push に失敗しました"
else
	echo "$INDEX_BRANCH は既に同じ内容なので push しません。"
fi

pr_number="$(gh pr list --repo "$REPO" --head "$INDEX_BRANCH" --base "$BASE_BRANCH" --state open \
	--json number --jq '.[0].number // empty' 2>/dev/null || true)"
if [ -n "$pr_number" ]; then
	gh pr edit "$pr_number" --repo "$REPO" --title "$title" --body-file "$body"
	summary "PR #${pr_number} を更新しました。"
else
	pr_url="$(gh pr create --repo "$REPO" --base "$BASE_BRANCH" --head "$INDEX_BRANCH" \
		--title "$title" --body-file "$body")"
	summary "PR を作成しました: ${pr_url}"
fi
