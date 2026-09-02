#!/usr/bin/env bash
#
# upstream-sync.sh — フォーク元（公式リファレンス）の更新を取り込む PR を作る。
#
# なぜこれがあるか
# ----------------
# このリポジトリは Vectorworks 公式リファレンス（Vectorworks/developer-sdk）の
# フォークで、`Info/` と `Versions/` は**上流由来のまま保つ**という約束になっている
# （CLAUDE.md「このリポジトリについて」）。約束を守るには上流の更新をこちらへ流し
# 込み続ける必要があるが、上流の更新は年に数回（Vectorworks の版が上がるとき）で、
# 気付いた頃には何十コミットも離れている、という壊れ方をする。だから週次で見張り、
# **差分があったら PR にして人の目に掛ける**。
#
# 何をするか
# ----------
#   1. 上流のデフォルトブランチを取ってきて、こちらの main に入っていない
#      コミットがあるか調べる。無ければ何もしない（PR も作らない）。
#   2. あれば main から作業ブランチ（既定 `upstream-sync`）を作り、上流を merge する。
#   3. そのブランチを push し、PR を作る（既に開いていれば中身を更新する）。
#
# 作業ブランチは**毎回作り直して force push する**。PR を 1 本に保ち、上流が更に
# 進んだときも同じ PR が最新の差分を示すようにするため。ただし**ブランチに bot 以外の
# コミットがあるときは触らない**——競合を人（や AI）が解消した内容を消さないため。
#
# 競合したときも PR は作る（draft + タイトルに [競合あり]）。競合マーカーを含んだまま
# コミットして push し、run 自体は失敗させる。**「PR が立たないまま静かに失敗する」の
# だけは避ける**という方針で、競合の解消は PR 上で行う。
#
# 使い方
# ------
#   scripts/upstream-sync.sh              取り込み PR を作る／更新する
#   DRY_RUN=yes scripts/upstream-sync.sh  push も PR 作成もせず、何をするかだけ出す
#
# 環境変数:
#   GH_TOKEN         gh 用のトークン（contents: write / pull-requests: write）
#   UPSTREAM_REPO    上流の owner/repo（既定 Vectorworks/developer-sdk）
#   UPSTREAM_BRANCH  上流のブランチ（既定: 上流の HEAD が指すブランチを自動判定）
#   BASE_BRANCH      取り込み先（既定 main）
#   SYNC_BRANCH      作業ブランチ名（既定 upstream-sync）
#   DRY_RUN          yes なら push / PR 作成をしない（既定 no）
#   GITHUB_REPOSITORY  自分の owner/repo（Actions が自動で入れる）
#
set -euo pipefail

UPSTREAM_REPO="${UPSTREAM_REPO:-Vectorworks/developer-sdk}"
UPSTREAM_BRANCH="${UPSTREAM_BRANCH:-}"
BASE_BRANCH="${BASE_BRANCH:-main}"
SYNC_BRANCH="${SYNC_BRANCH:-upstream-sync}"
DRY_RUN="${DRY_RUN:-no}"
REPO="${GITHUB_REPOSITORY:-min-nano/vectorworks-developer-sdk-reference}"

# 作業ブランチを作り直してよいかの判定に使う（このコミッタ以外が触っていたら触らない）。
BOT_NAME="github-actions[bot]"
BOT_EMAIL="41898282+github-actions[bot]@users.noreply.github.com"

UPSTREAM_URL="https://github.com/${UPSTREAM_REPO}.git"

die() {
	printf '::error::upstream-sync: %s\n' "$*" >&2
	exit 1
}

# GitHub の run サマリ（人が後から読む用）。ローカル実行では捨てる。
summary() {
	printf '%s\n' "$*"
	if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
		printf '%s\n' "$*" >> "$GITHUB_STEP_SUMMARY"
	fi
}

# push はネットワークで落ちうるので数回だけ待って粘る（CI の再実行より安い）。
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
if [ "$DRY_RUN" != "yes" ]; then
	command -v gh >/dev/null 2>&1 || die "gh がありません"
	[ -n "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ] || die "GH_TOKEN が設定されていません"
fi

# ---------------------------------------------------------------------------
# 上流と自分の現在地を確かめる
# ---------------------------------------------------------------------------

if [ -z "$UPSTREAM_BRANCH" ]; then
	UPSTREAM_BRANCH="$(git ls-remote --symref "$UPSTREAM_URL" HEAD |
		sed -n 's#^ref: refs/heads/\([^[:space:]]*\).*#\1#p')"
	[ -n "$UPSTREAM_BRANCH" ] || die "上流 ($UPSTREAM_REPO) のデフォルトブランチを判定できません"
fi

git config user.name "$BOT_NAME"
git config user.email "$BOT_EMAIL"

git remote remove upstream >/dev/null 2>&1 || true
git remote add upstream "$UPSTREAM_URL"

git fetch --no-tags origin "+refs/heads/${BASE_BRANCH}:refs/remotes/origin/${BASE_BRANCH}" ||
	die "origin/$BASE_BRANCH を取得できません"
git fetch --no-tags upstream "+refs/heads/${UPSTREAM_BRANCH}:refs/remotes/upstream/${UPSTREAM_BRANCH}" ||
	die "$UPSTREAM_REPO の $UPSTREAM_BRANCH を取得できません"

base_head="$(git rev-parse "refs/remotes/origin/${BASE_BRANCH}")"
upstream_head="$(git rev-parse "refs/remotes/upstream/${UPSTREAM_BRANCH}")"

echo "base    : $REPO@$BASE_BRANCH = $base_head"
echo "upstream: $UPSTREAM_REPO@$UPSTREAM_BRANCH = $upstream_head"

if git merge-base --is-ancestor "$upstream_head" "$base_head"; then
	summary "上流に新しいコミットはありません（$UPSTREAM_REPO@$UPSTREAM_BRANCH = ${upstream_head:0:12} は既に $BASE_BRANCH に入っています）。"
	exit 0
fi

# 共通祖先。上流側の compare リンクと「新しいコミット」の列挙に使う。
merge_base="$(git merge-base "$base_head" "$upstream_head")"
new_count="$(git rev-list --count "${merge_base}..${upstream_head}")"
echo "上流に $new_count 件の新しいコミットがあります（共通祖先 ${merge_base:0:12}）。"

# ---------------------------------------------------------------------------
# 既にある作業ブランチを尊重する（人が競合を解消した内容を force push で消さない）
# ---------------------------------------------------------------------------

sync_head=""
if [ -n "$(git ls-remote --heads origin "$SYNC_BRANCH")" ]; then
	git fetch --no-tags origin "+refs/heads/${SYNC_BRANCH}:refs/remotes/origin/${SYNC_BRANCH}" ||
		die "origin/$SYNC_BRANCH を取得できません"
	sync_head="$(git rev-parse "refs/remotes/origin/${SYNC_BRANCH}")"

	# ブランチ独自のコミット（main にも上流にも無いもの＝このスクリプトが作った merge か、
	# 人が足した解消コミット）に bot 以外の作者がいるか。上流由来のコミットを数えて
	# しまうと、上流の作者名で必ず引っかかってしまうので --not で除く。
	foreign="$(git log --format='%ae' "$sync_head" --not "$base_head" "$upstream_head" |
		grep -v -F -x "$BOT_EMAIL" || true)"
	if [ -n "$foreign" ]; then
		summary "$SYNC_BRANCH に $BOT_NAME 以外のコミットがあるため、このブランチには触りません（競合の解消などを消さないため）。取り込みを進めるにはそちらの PR を片付けてください。"
		exit 0
	fi
fi

# ---------------------------------------------------------------------------
# main から作り直して上流を merge する
# ---------------------------------------------------------------------------

merge_title="chore: 上流 ${UPSTREAM_REPO}@${UPSTREAM_BRANCH} を取り込む (${upstream_head:0:12})"

git checkout -B "$SYNC_BRANCH" "refs/remotes/origin/${BASE_BRANCH}"

conflicted=no
conflict_files=""
if ! git merge --no-ff --no-edit -m "$merge_title" "$upstream_head"; then
	conflicted=yes
	conflict_files="$(git diff --name-only --diff-filter=U)"
	# 競合マーカーを含んだままコミットする。**PR を必ず立てる**ためで、この PR は
	# そのままマージしてはいけない（draft にし、run も失敗させる）。
	git add -A
	git commit --no-verify -m "$merge_title

競合したため、競合マーカーを含んだままコミットしています。このままマージしないこと。"
fi

new_head="$(git rev-parse HEAD)"

# 前回と同じ merge（親も木も同じ）なら push も PR 更新もしない。無駄な force push で
# PR に通知が飛ぶのを防ぐ。
push_needed=yes
if [ -n "$sync_head" ]; then
	old_parents="$(git rev-list --parents -n 1 "$sync_head" | cut -d' ' -f2-)"
	new_parents="$(git rev-list --parents -n 1 "$new_head" | cut -d' ' -f2-)"
	if [ "$old_parents" = "$new_parents" ] &&
		[ "$(git rev-parse "${sync_head}^{tree}")" = "$(git rev-parse "${new_head}^{tree}")" ]; then
		push_needed=no
	fi
fi

# ---------------------------------------------------------------------------
# PR 本文
# ---------------------------------------------------------------------------

body="$(mktemp)"
trap 'rm -f "$body"' EXIT

{
	echo "上流 [\`${UPSTREAM_REPO}\`](https://github.com/${UPSTREAM_REPO}) の \`${UPSTREAM_BRANCH}\` が更新されたので取り込みます（\`scripts/upstream-sync.sh\` による自動生成）。"
	echo
	echo "| | |"
	echo "| --- | --- |"
	echo "| 上流 | \`${UPSTREAM_REPO}@${UPSTREAM_BRANCH}\` |"
	echo "| 取り込む先 | \`${BASE_BRANCH}\` |"
	echo "| 新しいコミット | ${new_count} 件 |"
	echo "| 差分 | https://github.com/${UPSTREAM_REPO}/compare/${merge_base}...${upstream_head} |"
	echo
	echo "## 上流の新しいコミット"
	echo
	echo '```'
	git log --no-merges --format='%h %ad %an  %s' --date=short "${merge_base}..${upstream_head}" | head -n 50
	if [ "$new_count" -gt 50 ]; then
		echo "... (以下略。全 ${new_count} 件は上の差分リンクで見られます)"
	fi
	echo '```'
	echo

	if [ "$conflicted" = "yes" ]; then
		echo "## ⚠ 競合しています — **このままマージしないこと**"
		echo
		echo "自動 merge が競合したため、**競合マーカーを含んだまま**コミットしてあります。"
		echo "手で解消して push し直してください（解消コミットが入るとこのブランチは"
		echo "自動更新の対象から外れるので、force push で消される心配はありません）。"
		echo
		echo "競合したファイル:"
		echo
		# バッククォート。単一引用符の中に直接書くと shellcheck が誤検知するので、
		# 8 進エスケープで持って printf の %b で戻す。
		bt='\140'
		printf '%s\n' "$conflict_files" | while IFS= read -r f; do
			printf -- '- %b%s%b\n' "$bt" "$f" "$bt"
		done
		echo
	fi

	echo "## 確認すること"
	echo
	echo "- \`Info/\` \`Versions/\` は上流由来のリファレンスなので、**上流の内容をそのまま**取り込む（CLAUDE.md「このリポジトリについて」）。誤りを見つけても直すのは \`Findings/\` 側。"
	echo "- \`README.md\` はフォーク側でも編集している（「このフォークについて」の節）。上流の変更と衝突していないか、追加分の節が残っているかを見る。"
	echo "- 上流の更新で \`Findings/\` の記述が古くなっていないか（SDK の版が上がったときは特に）。気になる点は **Findings 側に追記する**か、[調査 issue](https://github.com/${REPO}/issues/new?template=investigation.md) を立てる。"
	echo
	echo "> [!NOTE]"
	echo "> この PR は \`GITHUB_TOKEN\` で作られているため、**lint の CI は自動では走りません**（GitHub の仕様）。走らせたい場合は PR を close → reopen してください。"
} > "$body"

# ---------------------------------------------------------------------------
# push と PR
# ---------------------------------------------------------------------------

title="chore: 上流 (${UPSTREAM_REPO}) の更新を取り込む"
if [ "$conflicted" = "yes" ]; then
	title="[競合あり] ${title}"
fi

if [ "$DRY_RUN" = "yes" ]; then
	summary "DRY_RUN=yes のため push も PR 作成もしません。push_needed=$push_needed conflicted=$conflicted"
	echo "--- PR タイトル ---"
	echo "$title"
	echo "--- PR 本文 ---"
	cat "$body"
	# 競合の判定だけは dry run でも本番と同じに扱う（最後の exit まで落ちる）。
elif [ "$push_needed" = "yes" ]; then
	# 作り直したブランチなので force が要る。ただし「取得したときのまま」であることを
	# 明示的に条件にして、待っている間に誰かが積んだコミットを踏み潰さないようにする
	# （ブランチがまだ無いときは通常の push）。
	push_args=(push origin "HEAD:refs/heads/${SYNC_BRANCH}")
	if [ -n "$sync_head" ]; then
		push_args=(push "--force-with-lease=refs/heads/${SYNC_BRANCH}:${sync_head}"
			origin "HEAD:refs/heads/${SYNC_BRANCH}")
	fi
	push_with_retry "${push_args[@]}" ||
		die "$SYNC_BRANCH の push に失敗しました"
else
	echo "$SYNC_BRANCH は既に同じ内容なので push しません。"
fi

# PR の作成・更新。dry run では触らない。
if [ "$DRY_RUN" != "yes" ]; then
	pr_info="$(gh pr list --repo "$REPO" --head "$SYNC_BRANCH" --base "$BASE_BRANCH" --state open \
		--json number,isDraft --jq '.[0] | select(.) | "\(.number) \(.isDraft)"' 2>/dev/null || true)"
	pr_number="${pr_info%% *}"
	pr_draft="${pr_info##* }"

	if [ -n "$pr_number" ]; then
		gh pr edit "$pr_number" --repo "$REPO" --title "$title" --body-file "$body"
		# 競合の有無が前回から変わっていたら draft 状態も合わせる。
		if [ "$conflicted" = "yes" ] && [ "$pr_draft" != "true" ]; then
			gh pr ready "$pr_number" --repo "$REPO" --undo || true
		elif [ "$conflicted" = "no" ] && [ "$pr_draft" = "true" ]; then
			gh pr ready "$pr_number" --repo "$REPO" || true
		fi
		summary "PR #${pr_number} を更新しました。"
	else
		create_args=(--repo "$REPO" --base "$BASE_BRANCH" --head "$SYNC_BRANCH"
			--title "$title" --body-file "$body")
		if [ "$conflicted" = "yes" ]; then
			create_args+=(--draft)
		fi
		pr_url="$(gh pr create "${create_args[@]}")"
		summary "PR を作成しました: ${pr_url}"
	fi
fi

if [ "$conflicted" = "yes" ]; then
	die "上流の取り込みが競合しました。PR 上で手で解消してください（競合: $(printf '%s' "$conflict_files" | tr '\n' ' '))"
fi
