#!/usr/bin/env bash
#
# gather-probes.sh — 実機確認プラグイン（plugin/）へ入れるプローブを集める。
#
# なぜこれがあるか
# ----------------
# 実機確認は「PR がマージされる前」に要る（Findings に無印＝実機確認済みで書く内容の
# PR は、確認が済むまでマージしない。CLAUDE.md「PR とマージ」）。一方でビルドは
# main のワークフローが行う。つまり**まだマージされていない複数の PR の調査コードを、
# 1 本のプラグインへ同居させて**配る必要がある。それをやるのがこのスクリプト。
#
#   main の probes/runtime/*        … 既に取り込まれているプローブ
#   + 指定された PR の probes/runtime/*  … まだ open な PR のプローブ
#   ──────────────────────────────────────────
#   = build-probes/sources/<slug>/  … 集めたソース
#     build-probes/manifest.cmake   … CMake への入力（ソース一覧＋出所の表）
#     build-probes/summary.md       … リリースノート用（何が入っているか）
#
# 同居できる仕掛けは 2 つだけ:
#   1. **1 プローブ 1 ディレクトリ**（probes/runtime/<slug>/）。集約はディレクトリ単位で、
#      slug がそのまま一意な鍵になる。
#   2. **プローブのシンボルはすべて内部リンケージ**（VW_PROBE マクロが static で展開する。
#      plugin/src/Probe.h）。別々の PR が同じ名前を使っていてもリンクで衝突しない。
#
# 同じ slug がぶつかったときの扱い（PR のブランチには main のプローブもそのまま載って
# いるので、まず「中身が同じか」で引き継ぎと変更を分ける）:
#   * 中身が同じ            → 黙って飛ばす（その PR は main の版を引き継いでいるだけ）。
#   * main と PR で中身が違う → **PR 側で上書き**（マージ前の版を確かめるのが目的。ログに出す）。
#   * PR どうしで中身が違う   → **エラーで止める**（どちらを載せるべきか機械には決められない。
#     どちらかの slug を変えてから出し直す）。
#
# 使い方:
#   scripts/gather-probes.sh                    # 作業ツリーのプローブだけ
#   scripts/gather-probes.sh --pr 12 --pr 15    # ＋ PR #12 / #15 のプローブ
#   scripts/gather-probes.sh --prs 12,15        # 同上（カンマ区切りでも可）
#   scripts/gather-probes.sh --out /tmp/probes  # 出力先（既定 build-probes）
#
# 環境変数:
#   GITHUB_REPOSITORY  PR タイトルを引くのに使う（gh がある環境でのみ。無くても動く）
#
set -euo pipefail

cd "$(dirname "$0")/.."

OUT="build-probes"
PRS=()

usage() {
	sed -n '2,40p' "$0"
	exit "${1:-0}"
}

while [ $# -gt 0 ]; do
	case "$1" in
		--out)
			OUT="${2:?--out にはディレクトリが要ります}"
			shift 2
			;;
		--pr)
			PRS+=("${2:?--pr には PR 番号が要ります}")
			shift 2
			;;
		--prs)
			# カンマ／空白区切りをまとめて受ける（ワークフローの入力をそのまま渡せるように）。
			IFS=', ' read -r -a _list <<<"${2:-}"
			for n in "${_list[@]}"; do
				[ -n "$n" ] && PRS+=("$n")
			done
			shift 2
			;;
		-h | --help) usage 0 ;;
		*)
			echo "gather-probes: 知らない引数: $1" >&2
			usage 1
			;;
	esac
done

# 出力先は毎回作り直す（前回の残りが混ざると「何が入っているか」が信用できない）。
rm -rf "$OUT"
mkdir -p "$OUT/sources"

# 集めた slug → 出所（"pr|commit|branch|title"）。bash 3.2（macOS 既定）でも動くよう、
# 連想配列ではなく 2 本の平行配列で持つ。
SLUGS=()
ORIGINS=()
DIGESTS=()

slug_index() {
	local needle="$1" i
	[ "${#SLUGS[@]}" -gt 0 ] || return 1
	for i in "${!SLUGS[@]}"; do
		if [ "${SLUGS[$i]}" = "$needle" ]; then
			echo "$i"
			return 0
		fi
	done
	return 1
}

# dir_digest <ディレクトリ>: プローブ 1 件の中身を表す指紋（ファイル名と内容）。
# **PR のブランチには main のプローブもそのまま載っている**ので、「その PR が実際に
# 足した／変えたプローブ」と「main から引き継いだだけのプローブ」を区別する必要がある。
# 内容が同じなら後者と見なして黙って飛ばす（そうしないと、2 つの PR を同居させた
# 途端に、両方が持っている main のプローブが「重複」として衝突してしまう）。
dir_digest() {
	local dir="$1" file
	find "$dir" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) | sort | while read -r file; do
		printf '%s %s\n' "$(basename "$file")" "$(git hash-object "$file")"
	done
}

# 1 行に畳めない文字を落とす。PR タイトルは外から来る文字列なので、マニフェスト
# （CMake のリスト）と生成される C++ を壊さないよう、ここで必ず通す。
sanitize() {
	printf '%s' "$1" | tr -d '\n\r' | sed 's/[|;"\\]/ /g; s/  */ /g; s/^ //; s/ $//'
}

# add_probe <ソースのディレクトリ> <slug> <pr> <commit> <branch> <title> <出どころの説明>
add_probe() {
	local dir="$1" slug="$2" pr="$3" commit="$4" branch="$5" title="$6" from="$7"

	# slug は「小文字英数字とハイフン」に限る。ディレクトリ名がそのままリリースノート・
	# ログファイル名・grep のパターンになるので、変な文字を持ち込ませない。
	if ! printf '%s' "$slug" | grep -qE '^[a-z0-9][a-z0-9-]*$'; then
		echo "::error::プローブのディレクトリ名 '$slug'（$from）は小文字英数字とハイフンだけにしてください。" >&2
		exit 1
	fi

	# **slug はディレクトリ名と一致させる。** プラグイン側は VW_PROBE の第 1 引数を
	# 鍵に出所表と突き合わせるので、ここがずれると「出所不明」で表示される。
	if ! grep -qE "VW_PROBE\(\"$slug\"" "$dir"/*.cpp 2>/dev/null; then
		echo "::error::プローブ '$slug'（$from）の VW_PROBE(\"$slug\", …) が見つかりません。" \
			"ディレクトリ名と VW_PROBE の第 1 引数を一致させてください。" >&2
		exit 1
	fi

	local digest
	digest="$(dir_digest "$dir")"

	local existing
	if existing="$(slug_index "$slug")"; then
		# 同じ中身なら「その PR が main から引き継いだだけ」。黙って飛ばす。
		if [ "$digest" = "${DIGESTS[$existing]}" ]; then
			return 0
		fi
		local previous="${ORIGINS[$existing]}"
		local previous_pr="${previous%%|*}"
		if [ -n "$previous_pr" ] && [ -n "$pr" ]; then
			echo "::error::プローブ '$slug' を PR #$previous_pr と PR #$pr が別々に変えています。" \
				"どちらを載せるかは機械には決められません。どちらかの slug" \
				"（ディレクトリ名と VW_PROBE の第 1 引数）を変えてください。" >&2
			exit 1
		fi
		# main 由来と PR 由来がぶつかった場合。**PR 側を採る**——マージ前の版を実機で
		# 確かめるのがこの仕組みの目的だから。PR が main より古いときも PR 側になるので、
		# 意図しない巻き戻しに気付けるよう必ずログへ出す。
		if [ -n "$pr" ]; then
			echo "  (note) '$slug' は main にもありますが、PR #$pr の版で上書きします。"
			rm -rf "${OUT:?}/sources/$slug"
		else
			echo "  (note) '$slug' は PR 側の版を採用済みです。main の版は使いません。"
			return 0
		fi
		SLUGS[existing]=""
		ORIGINS[existing]=""
		DIGESTS[existing]=""
	fi

	mkdir -p "$OUT/sources/$slug"
	# コンパイル対象は *.cpp / *.h だけ（README や試験データは持ち込まない）。
	find "$dir" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) -exec cp {} "$OUT/sources/$slug/" \;

	SLUGS+=("$slug")
	ORIGINS+=("$pr|$commit|$branch|$(sanitize "$title")")
	DIGESTS+=("$digest")
	echo "  + $slug  ($from)"
}

# --- main（＝いまチェックアウトしている作業ツリー）のプローブ ------------------
head_commit="$(git rev-parse --short HEAD 2>/dev/null || echo local)"
# ブランチ名は CI が知っている値を優先する。**Actions のチェックアウトは detached HEAD**
# なので、git に訊くと "HEAD" が返ってしまう（GITHUB_HEAD_REF は PR の head ブランチ、
# GITHUB_REF_NAME は push されたブランチ）。
head_branch="${GITHUB_HEAD_REF:-}"
if [ -z "$head_branch" ]; then
	head_branch="${GITHUB_REF_NAME:-}"
fi
if [ -z "$head_branch" ] || [ "$head_branch" = "HEAD" ]; then
	head_branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo local)"
fi

echo "作業ツリー（$head_branch $head_commit）のプローブ:"
if [ -d probes/runtime ]; then
	for dir in probes/runtime/*/; do
		[ -d "$dir" ] || continue
		slug="$(basename "$dir")"
		add_probe "$dir" "$slug" "" "$head_commit" "$head_branch" "" "$head_branch"
	done
fi

# --- 指定された PR のプローブ ---------------------------------------------------
# PR の head を fetch し、その木から probes/runtime だけを取り出す（作業ツリーは
# 汚さない。git archive → tar で一時ディレクトリへ展開する）。
for pr in ${PRS[@]+"${PRS[@]}"}; do
	echo "PR #$pr のプローブ:"
	# 浅いクローン（CI の actions/checkout は既定で fetch-depth 1）では、PR の head を
	# 普通に fetch すると履歴を深く引きに行く。木さえ取れればよいので、浅いときだけ
	# --depth=1 を足す（手元の完全なクローンを浅くしてしまわないよう、条件付きにする）。
	# 参照は "+" で上書き（作り直しのたびに non-fast-forward で落ちないように）。
	depth_args=()
	if [ "$(git rev-parse --is-shallow-repository 2>/dev/null)" = "true" ]; then
		depth_args=(--depth=1)
	fi
	if ! git fetch --no-tags --quiet ${depth_args[@]+"${depth_args[@]}"} origin \
		"+pull/$pr/head:refs/remotes/probe-pr/$pr" 2>/dev/null; then
		# 既に取得済みのときは fetch が失敗しうるので、参照が解決できれば続行する。
		if ! git rev-parse --verify --quiet "refs/remotes/probe-pr/$pr" >/dev/null; then
			echo "::error::PR #$pr の head を取得できませんでした（番号は正しいですか）。" >&2
			exit 1
		fi
	fi
	pr_sha="$(git rev-parse "refs/remotes/probe-pr/$pr")"
	pr_short="$(git rev-parse --short "refs/remotes/probe-pr/$pr")"

	pr_branch=""
	pr_title=""
	if command -v gh >/dev/null 2>&1 && [ -n "${GITHUB_REPOSITORY:-}" ]; then
		# 取れなくても構わない（表示が少し寂しくなるだけ）。
		pr_branch="$(gh api "repos/$GITHUB_REPOSITORY/pulls/$pr" --jq .head.ref 2>/dev/null || true)"
		pr_title="$(gh api "repos/$GITHUB_REPOSITORY/pulls/$pr" --jq .title 2>/dev/null || true)"
	fi

	if ! git ls-tree -d --name-only "$pr_sha" probes/runtime | grep -q .; then
		echo "::error::PR #$pr（$pr_short）に probes/runtime/ がありません。" >&2
		exit 1
	fi

	work="$(mktemp -d)"
	git archive "$pr_sha" probes/runtime | tar -x -C "$work"
	found=0
	for dir in "$work"/probes/runtime/*/; do
		[ -d "$dir" ] || continue
		slug="$(basename "$dir")"
		add_probe "$dir" "$slug" "$pr" "$pr_short" "$pr_branch" "$pr_title" "PR #$pr $pr_short"
		found=1
	done
	rm -rf "$work"
	if [ "$found" -eq 0 ]; then
		echo "::error::PR #$pr（$pr_short）の probes/runtime/ にプローブがありません。" >&2
		exit 1
	fi
done

# --- マニフェストと要約を書く ---------------------------------------------------
manifest="$OUT/manifest.cmake"
{
	echo "# 自動生成（scripts/gather-probes.sh）。編集しない。"
	echo "# plugin/CMakeLists.txt が -DVW_PROBE_MANIFEST=... で読む。"
	echo "set(VW_PROBE_SOURCES"
} >"$manifest"

summary_md="$OUT/summary.md"
summary_txt="$OUT/summary.txt"
: >"$summary_md"
{
	echo "| プローブ | 出所 | コミット | ブランチ | PR タイトル |"
	echo "| --- | --- | --- | --- | --- |"
} >>"$summary_md"
: >"$summary_txt"

entries=()
for i in ${SLUGS[@]+"${!SLUGS[@]}"}; do
	slug="${SLUGS[$i]}"
	[ -n "$slug" ] || continue
	origin="${ORIGINS[$i]}"
	pr="$(echo "$origin" | cut -d'|' -f1)"
	commit="$(echo "$origin" | cut -d'|' -f2)"
	branch="$(echo "$origin" | cut -d'|' -f3)"
	title="$(echo "$origin" | cut -d'|' -f4-)"

	for src in "$OUT/sources/$slug"/*.cpp; do
		[ -f "$src" ] || continue
		# マニフェストからの相対で書く（集約したランナーと、ビルドするランナーが
		# 別でもよいように。CMake の CMAKE_CURRENT_LIST_DIR はこの .cmake の場所）。
		echo "	\"\${CMAKE_CURRENT_LIST_DIR}/sources/$slug/$(basename "$src")\"" >>"$manifest"
	done

	entries+=("$slug|$pr|$commit|$branch|$title")
	if [ -n "$pr" ]; then
		echo "| \`$slug\` | PR #$pr | \`$commit\` | $branch | $title |" >>"$summary_md"
		echo "$slug <- PR #$pr ($commit)" >>"$summary_txt"
	else
		echo "| \`$slug\` | ${branch:-main} | \`$commit\` | $branch | |" >>"$summary_md"
		echo "$slug <- ${branch:-main} ($commit)" >>"$summary_txt"
	fi
done

{
	echo ")"
	echo "set(VW_PROBE_ENTRIES"
	for entry in ${entries[@]+"${entries[@]}"}; do
		echo "	\"$entry\""
	done
	echo ")"
} >>"$manifest"

echo
echo "集めたプローブ: ${#entries[@]} 件"
cat "$summary_txt"
echo "マニフェスト: $manifest"
