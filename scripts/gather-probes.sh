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
# **同居は「群（group）ごとに 1 本の本体（ペイロード）」で行う。** main のプローブが
# 1 本、PR のプローブが PR ごとに 1 本ずつ、別々の .vwpayload になる。
#
#   main の probes/runtime/*             → 群 main    → VwSdkProbesPayload-main.vwpayload
#   PR #12 の probes/runtime/*（main と違うものだけ） → 群 pr12 → …-pr12.vwpayload
#   PR #15 の probes/runtime/*                        → 群 pr15 → …-pr15.vwpayload
#
# **1 本にまとめない理由が 2 つある。**
#   1. **1 つの PR がコンパイルできなくても、他の PR は配れる。** まとめて 1 本に
#      していたときは、誰か 1 人のプローブが通らないだけで**全員のビルドが落ちた**。
#   2. **選んでから読み込めばよい。** 殻はダイアログで選ばれた群の本体だけを読む
#      （plugin/src/PayloadCatalog.h / ProbeMenu.cpp）。メニューを開くたびに全部を
#      読み込む必要が無くなる。
#
# 出力（既定 build-probes/）:
#   sources/<group>/<slug>/  … 集めたソース（群ごと）
#   manifest.cmake           … CMake への入力（群ごとのソース一覧・出所の表・ID）
#   groups.txt               … 群の一覧（1 行 1 群。ワークフローがこれを回してビルドする）
#   build-id.txt / shell-id.txt … **ビルド ID / 殻の ID**（下記）
#   summary.md / summary.txt / summary-line.txt … リリースノートと控え用
#
# ビルド ID —— 自動アップデートが新旧を比べる鍵（plugin/src/Update.h）:
#
#   **「何から作ったか」から計算する**（いつ作ったかではない）。材料は
#   「チェックアウトしているコミット（＝main。プラグイン本体・CMake・ワークフロー・
#   main のプローブが全部ここに乗る）」＋「同居させる各 PR の head コミット」だけで、
#   これを並べた文字列のハッシュを ID にする。したがって:
#
#     * 同じ顔ぶれで作り直しても ID は変わらない → **中身が同じなら更新を勧めない**
#       （run id のような「実行ごとに変わる値」だと、作り直すたびに誘ってしまう）。
#     * main が動けば ID が変わる → ビルド設定の変更もちゃんと拾う。
#     * PR を force push しても head が動くので ID が変わる。
#
#   PR の並び順は正規化する（"15,12" と "12,15" を同じ ID にするため）。
#
# 同居できる仕掛け:
#   1. **1 プローブ 1 ディレクトリ**（probes/runtime/<slug>/）。集約はディレクトリ単位で、
#      slug が群の中での一意な鍵になる。
#   2. **群ごとに別のモジュール**。別々の PR が同じ slug を使っていても、別の本体に入る
#      ので衝突しない（ピッカーには出所付きで 2 行並ぶ——**それが見たい**）。
#   3. **プローブのシンボルはすべて内部リンケージ**（VW_PROBE マクロが static で展開する。
#      plugin/src/payload/Probe.h）。同じ群の中で名前がぶつかってもリンクで衝突しない。
#
# **PR のブランチには main のプローブもそのまま載っている**ので、そのままだと同じものが
# 2 度出る。中身（ファイル名と内容の指紋）が main と同じものは PR の群から落とす:
#   * main と中身が同じ   → 黙って飛ばす（その PR は main の版を引き継いでいるだけ）
#   * main と中身が違う   → **PR の群へ入れる**（main の版と並んで出る。マージ前の版と
#                            いまの版を実機で見比べられる）
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
	sed -n '2,60p' "$0"
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

# 群の一覧と、群ごとの控え。**連想配列を使わない**（macOS 既定の bash 3.2 でも動くように）。
# 群ごとの情報はファイルへ書く——群が増減しても書き方が変わらず、後段（CMake・
# ワークフロー）からも同じものを読める。
# **変数名に GROUPS を使わない。** bash の特殊変数（実行者の所属グループの配列）で、
# 代入しても数値へ潰され、追加した文字列は 0 になる（set -e のもとでは関数がそこで
# 終わる）。実際にそれで「群 0 は空です」と言い出した。
PROBE_GROUPS=()
: >"$OUT/groups.txt"

# main の指紋表（slug<TAB>digest）。PR の側で「引き継いだだけ」を落とすのに使う。
MAIN_DIGESTS="$OUT/.main-digests.txt"
: >"$MAIN_DIGESTS"

# 1 行に畳めない文字を落とす。PR タイトルは外から来る文字列なので、マニフェスト
# （CMake のリスト）・カタログ・生成される C++ を壊さないよう、ここで必ず通す。
sanitize() {
	printf '%s' "$1" | tr -d '\n\r' | sed 's/[|;"\\]/ /g; s/  */ /g; s/^ //; s/ $//'
}

# dir_digest <ディレクトリ>: プローブ 1 件の中身を表す指紋（ファイル名と内容）。
# **PR のブランチには main のプローブもそのまま載っている**ので、「その PR が実際に
# 足した／変えたプローブ」と「main から引き継いだだけのプローブ」を区別する必要がある。
dir_digest() {
	local dir="$1" file
	find "$dir" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) | sort | while read -r file; do
		printf '%s %s\n' "$(basename "$file")" "$(git hash-object "$file")"
	done
}

# check_slug <slug> <ディレクトリ> <出どころの説明>: 名前と VW_PROBE の突き合わせ。
check_slug() {
	local slug="$1" dir="$2" from="$3"

	# slug は「小文字英数字とハイフン」に限る。ディレクトリ名がそのままリリースノート・
	# ログファイル名・grep のパターンになるので、変な文字を持ち込ませない。
	if ! printf '%s' "$slug" | grep -qE '^[a-z0-9][a-z0-9-]*$'; then
		echo "::error::プローブのディレクトリ名 '$slug'（${from}）は小文字英数字とハイフンだけにしてください。" >&2
		exit 1
	fi

	# **slug はディレクトリ名と一致させる。** プラグイン側は VW_PROBE の第 1 引数を
	# 鍵に出所表と突き合わせるので、ここがずれると「出所不明」で表示される。
	if ! grep -qE "VW_PROBE\(\"$slug\"" "$dir"/*.cpp 2>/dev/null; then
		echo "::error::プローブ '$slug'（${from}）の VW_PROBE(\"$slug\", …) が見つかりません。" \
			"ディレクトリ名と VW_PROBE の第 1 引数を一致させてください。" >&2
		exit 1
	fi
}

# add_group <group> <pr> <commit> <branch> <title>: 群を 1 つ開く。
add_group() {
	local group="$1"
	PROBE_GROUPS+=("$group")
	mkdir -p "$OUT/sources/$group"
	: >"$OUT/entries-$group.txt"
	printf '%s|%s|%s|%s|%s\n' "$group" "$2" "$3" "$4" "$(sanitize "$5")" >>"$OUT/groups.txt"
}

# add_probe <ソースのディレクトリ> <slug> <group>: 群へプローブを 1 件入れる。
add_probe() {
	local dir="$1" slug="$2" group="$3"
	mkdir -p "$OUT/sources/$group/$slug"
	# コンパイル対象は *.cpp / *.h だけ（README や試験データは持ち込まない）。
	find "$dir" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) \
		-exec cp {} "$OUT/sources/$group/$slug/" \;
	echo "$slug" >>"$OUT/entries-$group.txt"
	echo "  + $slug  ($group)"
}

# --- main（＝いまチェックアウトしている作業ツリー）のプローブ ------------------
head_commit="$(git rev-parse --short HEAD 2>/dev/null || echo local)"
head_full="$(git rev-parse HEAD 2>/dev/null || echo local)"

# ビルド ID の材料。main の行が先頭で、PR の行は後で番号順に並べ替える（上記「ビルド ID」）。
ID_PR_PARTS=()
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

echo "作業ツリー（$head_branch ${head_commit}）のプローブ → 群 main:"
add_group "main" "" "$head_commit" "$head_branch" ""
if [ -d probes/runtime ]; then
	for dir in probes/runtime/*/; do
		[ -d "$dir" ] || continue
		slug="$(basename "$dir")"
		check_slug "$slug" "$dir" "$head_branch"
		add_probe "$dir" "$slug" "main"
		printf '%s\t%s\n' "$slug" "$(dir_digest "$dir")" >>"$MAIN_DIGESTS"
	done
fi

# --- 指定された PR のプローブ ---------------------------------------------------
# PR の head を fetch し、その木から probes/runtime だけを取り出す（作業ツリーは
# 汚さない。git archive → tar で一時ディレクトリへ展開する）。
for pr in ${PRS[@]+"${PRS[@]}"}; do
	echo "PR #$pr のプローブ → 群 pr$pr:"
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

	# probes/runtime/ が無い PR は**黙って飛ばす**（エラーにしない）。自動公開は
	# open な PR を一括で渡してくるので、プローブと無関係な PR も混ざる
	# （scripts/probe-auto-update.sh）。
	if ! git ls-tree -d --name-only "$pr_sha" probes/runtime | grep -q .; then
		echo "  (note) PR #${pr}（${pr_short}）に probes/runtime/ がありません。飛ばします。"
		continue
	fi

	work="$(mktemp -d)"
	git archive "$pr_sha" probes/runtime | tar -x -C "$work"
	group="pr$pr"
	add_group "$group" "$pr" "$pr_short" "$pr_branch" "$pr_title"
	found=0
	for dir in "$work"/probes/runtime/*/; do
		[ -d "$dir" ] || continue
		slug="$(basename "$dir")"
		check_slug "$slug" "$dir" "PR #$pr $pr_short"
		# main と中身が同じなら「引き継いだだけ」。**群には入れない**（同じものが
		# ピッカーに 2 度並ぶのを避ける。上記「PR のブランチには main のプローブも…」）。
		digest="$(dir_digest "$dir")"
		main_digest="$(awk -F'\t' -v s="$slug" '$1 == s { $1 = ""; sub(/^\t/, ""); print }' \
			"$MAIN_DIGESTS")"
		if [ -n "$main_digest" ] && [ "$digest" = "$main_digest" ]; then
			continue
		fi
		add_probe "$dir" "$slug" "$group"
		found=1
	done
	rm -rf "$work"
	if [ "$found" -eq 0 ]; then
		# **エラーにしない。** main と同じものしか無い（プローブを消した PR・プローブ以外
		# を直した PR）は普通に起こる。空の群は下で落とす。
		echo "  (note) PR #$pr に main と違うプローブはありませんでした。"
		continue
	fi
	# **ビルド ID の材料に入れるのは、実際にプローブを載せた PR だけ。** 自動公開は
	# open な PR を一括で渡してくるので、無関係な PR の push まで材料に入れると、
	# 中身が同じなのに ID が動いて「新しいビルドがあります」と誘ってしまう。
	ID_PR_PARTS+=("pr=${pr}:${pr_sha}")
done

# 空の群は落とす（中身の無い本体を作っても、ピッカーに空の行が出るだけ）。
kept_groups=()
for group in ${PROBE_GROUPS[@]+"${PROBE_GROUPS[@]}"}; do
	if [ -s "$OUT/entries-$group.txt" ]; then
		kept_groups+=("$group")
	else
		echo "  (note) 群 $group は空なので落とします。"
		rm -rf "${OUT:?}/sources/$group"
		grep -v "^$group|" "$OUT/groups.txt" >"$OUT/groups.txt.tmp" || true
		mv "$OUT/groups.txt.tmp" "$OUT/groups.txt"
	fi
done
PROBE_GROUPS=(${kept_groups[@]+"${kept_groups[@]}"})

# --- ビルド ID を決める -----------------------------------------------------------
# 材料は「main のコミット」＋「各 PR の head コミット」。PR の行は**番号順に正規化**して
# から並べる（引数の順序で ID が変わらないように）。ハッシュは git hash-object で取る
# ——このスクリプトは常に git リポジトリの中で走るので、shasum / sha256sum の
# プラットフォーム差を気にしなくてよい。
buildid_source="$OUT/build-id-source.txt"
{
	echo "main=${head_full}"
	if [ "${#ID_PR_PARTS[@]}" -gt 0 ]; then
		printf '%s\n' "${ID_PR_PARTS[@]}" | sort -t= -k2 -V
	fi
} >"$buildid_source"

# 12 桁に切る（人が読み比べられる長さで、衝突は実用上考えなくてよい）。
build_id="$(git hash-object "$buildid_source" | cut -c1-12)"
printf '%s' "$build_id" >"$OUT/build-id.txt"

# --- 殻の ID を決める -------------------------------------------------------------
# **「入れ替えに再起動が要るか」を決める鍵**（plugin/src/UpdateParse.h の Evaluate）。
# プラグインは「殻（Vectorworks が起動時に読み込む）」と「本体（殻が自分で読み込む
# .vwpayload）」に割れていて、**本体だけなら Vectorworks を動かしたまま置き換えられる**。
# 殻まで変わったときだけ再起動が要る——それをこの ID の一致で見分ける。
#
# 材料は**殻に入るものだけ**（下のパスのツリーハッシュ）。だから:
#   * プローブを足しても消しても動かない  → 日常の入れ替えで再起動を求めない
#   * 境界（plugin/src/PayloadAbi.h）を変えれば必ず動く → 版の食い違いを持ち越さない
# ツリーハッシュは git がすでに持っている値なので、内容が同じなら必ず同じになる。
shellid_source="$OUT/shell-id-source.txt"
: >"$shellid_source"
for shell_path in plugin/src plugin/resources plugin/scripts plugin/CMakeLists.txt; do
	echo "${shell_path}=$(git rev-parse "${head_full}:${shell_path}")" >>"$shellid_source"
done
shell_id="$(git hash-object "$shellid_source" | cut -c1-12)"
printf '%s' "$shell_id" >"$OUT/shell-id.txt"

# --- マニフェストと要約を書く ---------------------------------------------------
# マニフェストは CMake が include する（-DVW_PROBE_MANIFEST=…）。**群ごとに**
#   VW_PROBE_SOURCES_<group>  … コンパイルするソース
#   VW_PROBE_ENTRIES_<group>  … 出所の表（slug|PR|commit|branch|PRタイトル）
#   VW_PROBE_ORIGIN_<group>   … その群自体の出所（PR|commit|branch|PRタイトル）
# を渡す。
manifest="$OUT/manifest.cmake"
{
	echo "# 自動生成（scripts/gather-probes.sh）。編集しない。"
	echo "# plugin/CMakeLists.txt が -DVW_PROBE_MANIFEST=... で読む。"
	echo "# ビルド ID（自動アップデートが新旧を比べる鍵）。**ここから配るので、"
	echo "# ビルドへ焼く値とリリースに書く値がずれようがない。**"
	echo "set(VW_PROBE_BUILD_ID \"$build_id\")"
	echo "# 殻の ID（入れ替えに再起動が要るかを決める鍵）。同上。"
	echo "set(VW_PROBE_SHELL_ID \"$shell_id\")"
	printf 'set(VW_PROBE_GROUPS'
	for group in ${PROBE_GROUPS[@]+"${PROBE_GROUPS[@]}"}; do
		printf ' "%s"' "$group"
	done
	echo ")"
} >"$manifest"

summary_md="$OUT/summary.md"
summary_txt="$OUT/summary.txt"
# 1 行版。リリース本文の隠しメタデータ（probes=）と、更新ダイアログの
# 「入っているプローブ」に出す（plugin/src/Update.cpp）。
summary_line="$OUT/summary-line.txt"
{
	echo "| プローブ | 出所 | コミット | ブランチ | PR タイトル |"
	echo "| --- | --- | --- | --- | --- |"
} >"$summary_md"
: >"$summary_txt"
: >"$summary_line"
line_parts=()
total=0

for group in ${PROBE_GROUPS[@]+"${PROBE_GROUPS[@]}"}; do
	origin="$(grep "^$group|" "$OUT/groups.txt")"
	pr="$(echo "$origin" | cut -d'|' -f2)"
	commit="$(echo "$origin" | cut -d'|' -f3)"
	branch="$(echo "$origin" | cut -d'|' -f4)"
	title="$(echo "$origin" | cut -d'|' -f5-)"

	{
		echo ""
		echo "# 群 ${group}（$([ -n "$pr" ] && echo "PR #$pr" || echo "main") ${commit}）"
		echo "set(VW_PROBE_ORIGIN_$group \"$pr|$commit|$branch|$title\")"
		echo "set(VW_PROBE_SOURCES_$group"
	} >>"$manifest"

	while read -r slug; do
		[ -n "$slug" ] || continue
		for src in "$OUT/sources/$group/$slug"/*.cpp; do
			[ -f "$src" ] || continue
			# マニフェストからの相対で書く（集約したランナーと、ビルドするランナーが
			# 別でもよいように。CMake の CMAKE_CURRENT_LIST_DIR はこの .cmake の場所）。
			echo "	\"\${CMAKE_CURRENT_LIST_DIR}/sources/$group/$slug/$(basename "$src")\"" \
				>>"$manifest"
		done
	done <"$OUT/entries-$group.txt"

	{
		echo ")"
		echo "set(VW_PROBE_ENTRIES_$group"
	} >>"$manifest"

	while read -r slug; do
		[ -n "$slug" ] || continue
		echo "	\"$slug|$pr|$commit|$branch|$title\"" >>"$manifest"
		total=$((total + 1))
		if [ -n "$pr" ]; then
			echo "| \`$slug\` | PR #$pr | \`$commit\` | $branch | $title |" >>"$summary_md"
			echo "$slug <- PR #$pr ($commit)" >>"$summary_txt"
			line_parts+=("$slug(#$pr)")
		else
			echo "| \`$slug\` | ${branch:-main} | \`$commit\` | $branch | |" >>"$summary_md"
			echo "$slug <- ${branch:-main} ($commit)" >>"$summary_txt"
			line_parts+=("$slug")
		fi
	done <"$OUT/entries-$group.txt"

	echo ")" >>"$manifest"
done

# 1 行版（", " 区切り）。空なら空ファイルのまま。
if [ "${#line_parts[@]}" -gt 0 ]; then
	printf '%s' "$(
		IFS=', '
		echo "${line_parts[*]}"
	)" >"$summary_line"
fi

rm -f "$MAIN_DIGESTS"

echo
echo "ビルド ID: $build_id"
echo "殻の ID: $shell_id"
sed 's/^/  /' "$buildid_source"
echo "群: ${PROBE_GROUPS[*]-（なし）}"
echo "集めたプローブ: $total 件"
cat "$summary_txt"
echo "マニフェスト: $manifest"
