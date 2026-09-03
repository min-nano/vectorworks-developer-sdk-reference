#!/usr/bin/env bash
#
# probe-auto-update.sh — PR に置かれたプローブを、実機で走らせられる形（転がりタグ
# `probes` のリリース）へ**自動で**反映する。`.github/workflows/probe-auto-update.yml`
# から呼ばれる。
#
# なぜこれがあるか
# ----------------
# 実機確認は「PR をマージする**前**」に要る（CLAUDE.md「PR とマージ」）。ところが
# これまでは、プローブを書いたあとに人（か AI）が Actions の "Probe plug-in" を
# workflow_dispatch で叩く必要があった。叩き忘れれば実機のプラグインは古いままで、
# 「プローブを書いた」と「実機で走らせられる」の間に手作業が 1 つ挟まる。
# **probes/runtime/ に置いた時点で公開まで進むなら、その手作業は要らない。**
#
# 何をするか
# ----------
#   1. main の "Probe plug-in"（probe-build.yml）を `prs=<この PR>` で dispatch する。
#      **ビルドと公開は人が手で叩くときとまったく同じ道**を通る（このスクリプトは
#      何もビルドしないし、リリースも作らない）。ビルドが main のワークフロー定義で
#      行われるので、PR 側がビルドの中身をすり替えることもできない。
#   2. 起動した run を特定して**完了まで待ち**、その結果をこのスクリプトの終了
#      ステータスにする。呼び出し元がジョブなので、**PR のチェックとして赤くなる**
#      ——「自動公開したつもりが実はコンパイルエラーだった」を PR 上で気付ける。
#   3. 成功したら PR へコメントを 1 つ置く（2 回目以降は**同じコメントを書き換える**
#      ので、push のたびに増えない）。
#
# 何を同居させるか
# ----------------
# **main のプローブ＋この PR のプローブだけ**。open な PR を全部集めることはしない
# ——他人の PR の都合（slug の衝突・巻き戻し）でこの PR のチェックが赤くなるのは
# 理不尽だから。複数の PR を 1 本に載せたいときは、これまで通り "Probe plug-in" を
# 手で dispatch する（inputs.prs にカンマ区切り）。
#
# 使い方
# ------
#   scripts/probe-auto-update.sh --pr 12
#
#   オプション:
#     --pr N        必須。プローブを載せる PR
#     --ref R       ビルドを走らせるブランチ（既定 main。ワークフローの置き場所）
#     --poll S      ポーリング間隔・秒（既定 20）
#     --timeout S   待機の上限・秒（既定 4200 = 70 分。mac と Windows の実ビルド）
#     --no-wait     dispatch だけして待たない（結果は見届けない）
#     --no-comment  PR へコメントしない
#
# **必ず終わること**（設計の要）
# ------------------------------
# 待機の歯止め（HTTP の時間上限・締切判定・ウォッチドッグ）と待機ループ本体、run の
# 特定は `ci-common.sh` にある。**待機ループをここへ書かないこと**（CLAUDE.md
# 「CI の完了を待つ」）。
#
# 環境変数:
#   GH_TOKEN / GITHUB_TOKEN   必須。dispatch に actions: write、コメントに
#                             pull-requests: write が要る（fork の PR ではどちらも
#                             付かないので、ワークフロー側で呼ばない）
#   VW_REPO                   owner/repo（既定は ci-common.sh）
#   GITHUB_STEP_SUMMARY       あればジョブの要約にも書く
#   その他の共通設定（HTTP の上限・生存出力の間隔など）は ci-common.sh を参照。
#
# 終了ステータス: ビルドと公開が成功したら 0、それ以外（失敗・締切超過・API 断念）は 1。
# 使い方の誤りや dispatch の失敗は 2。
#
set -uo pipefail

CI_TOOL="probe-auto-update"
PAU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source-path=SCRIPTDIR
# shellcheck source=scripts/ci-common.sh
. "$PAU_DIR/ci-common.sh" || {
	echo "probe-auto-update: error: scripts/ci-common.sh を読み込めません" >&2
	exit 2
}

WORKFLOW_FILE="probe-build.yml"
RELEASE_TAG="probes"
# コメントを 1 つに保つための目印。**本文の先頭に必ず入れる**（次回これで探して
# 書き換える。HTML コメントなので読む人には見えない）。
COMMENT_MARKER="<!-- probe-auto-update -->"

PR=""
REF="main"
POLL="${PROBE_AUTO_UPDATE_POLL:-20}"
TIMEOUT="${PROBE_AUTO_UPDATE_TIMEOUT:-4200}"
WAIT=1
COMMENT=1

# usage: ヘッダのコメントブロックをそのままヘルプとして出す（説明を二重に持たない）。
usage() {
	awk 'NR > 1 { if ($0 !~ /^#/) exit; print }' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--pr) PR="${2:-}" ; shift 2 ;;
		--ref) REF="${2:-}" ; shift 2 ;;
		--poll) POLL="${2:-}" ; shift 2 ;;
		--timeout) TIMEOUT="${2:-}" ; shift 2 ;;
		--no-wait) WAIT=0 ; shift ;;
		--no-comment) COMMENT=0 ; shift ;;
		-h | --help) usage ; exit 0 ;;
		*) die "未知のオプション: $1" ;;
	esac
done

[ -n "$TOKEN" ] || die "GITHUB_TOKEN / GH_TOKEN が未設定です（dispatch には actions: write が要ります）"
case "$PR" in
	'' | *[!0-9]*) die "--pr には PR 番号（整数）が要ります: '$PR'" ;;
esac
[ -n "$REF" ] || die "--ref が空です"
require_positive_int "$POLL" "--poll"
require_positive_int "$TIMEOUT" "--timeout"

# 締切＋余裕（run の特定とコメント投稿ぶん）を過ぎたら自分を殺す。締切判定より外側で
# 何かが固まっても、プロセスは必ず終わる。
start_watchdog "$((TIMEOUT + 600))"

# summary <行…>: ジョブの要約（GITHUB_STEP_SUMMARY）へ書く。無い環境では何もしない。
summary() {
	[ -n "${GITHUB_STEP_SUMMARY:-}" ] || return 0
	printf '%s\n' "$@" >>"$GITHUB_STEP_SUMMARY"
}

# ---------------------------------------------------------------------------
# 1. main の "Probe plug-in" を dispatch する
# ---------------------------------------------------------------------------
#
# 起動前に「今いちばん新しい dispatch の run id」を控える。dispatch API は run の id を
# 返さないので、**これより大きい id** を自分の run を掴む条件に使う（run 名だけで
# 突き合わせると、同じ PR で少し前に走った run を掴んでしまう）。
last_body="$(workfile)"
LAST_RUN_ID="0"
if [ "$(api_json "$VW_API/actions/workflows/$WORKFLOW_FILE/runs?event=workflow_dispatch&per_page=1" "$last_body")" = "200" ]; then
	LAST_RUN_ID="$(jq -r '.workflow_runs[0].id // 0' "$last_body" 2>/dev/null)"
fi
rm -f "$last_body"

payload="$(jq -n --arg ref "$REF" --arg prs "$PR" '{ref: $ref, inputs: {prs: $prs}}')"
resp="$(workfile)"
code="$(api -o "$resp" -w '%{http_code}' -X POST -d "$payload" \
	"$VW_API/actions/workflows/$WORKFLOW_FILE/dispatches")"
if [ "$code" = "403" ]; then
	die "dispatch が権限で拒否されました（HTTP 403）。このトークンに actions: write がありません（fork の PR では付きません）。Actions の \"Probe plug-in\" を手で dispatch してください（inputs.prs=$PR）"
fi
[ "$code" = "204" ] ||
	die "dispatch に失敗しました（HTTP $code）: $(api_message "$resp")。probe-build.yml が $REF にあるか確認してください"
rm -f "$resp"

echo "dispatched: $WORKFLOW_FILE (ref=$REF prs=$PR)"

# ---------------------------------------------------------------------------
# 2. その run を特定して完了まで待つ
# ---------------------------------------------------------------------------
#
# 突き合わせは run 名（probe-build.yml の run-name が "probe build (PR 12 + main)" を
# 作る）＋「控えた id より新しいこと」。前者だけだと過去の run を、後者だけだと
# 他の PR の run を掴みうるので、両方要る。
RESOLVE_RUN_WHAT="PR #$PR のビルド"
# shellcheck disable=SC2016 # $last / $t は jq の変数（--arg で渡す）。シェルに展開させない。
run_id="$(resolve_run "$WORKFLOW_FILE" \
	'(.id > ($last | tonumber)) and (.display_title | contains($t))' \
	--arg last "$LAST_RUN_ID" --arg t "(PR $PR + ")" ||
	die "起動したビルドの run を特定できませんでした（PR #$PR）"

run_url="https://github.com/$VW_REPO/actions/runs/$run_id"
echo "run: $run_url"

if [ "$WAIT" -eq 0 ]; then
	echo "(--no-wait: 結果は見届けません)"
	summary "プローブの自動更新: ビルドを起動しました（[run]($run_url)）。"
	exit 0
fi

conclusion="$(wait_run "$run_id")"
echo "conclusion=$conclusion"

if [ "$conclusion" != "success" ]; then
	# 見届けられなかった（timed-out-waiting / api-error）のか、ビルドが落ちたのかを
	# 呼び出し側が区別できるよう、文言を分ける。どちらも赤にする——「公開できたか
	# 分からない」を緑にすると、古いプラグインのまま実機で走らせてしまう。
	case "$conclusion" in
		timed-out-waiting | api-error)
			echo "::error::プローブの自動更新: ビルドの結果を見届けられませんでした（$conclusion）。$run_url を見てください。"
			summary "プローブの自動更新: **結果を見届けられませんでした**（$conclusion）。[run]($run_url)"
			;;
		*)
			echo "::error::プローブの自動更新: ビルドが $conclusion で終わりました。$run_url を見てください（プローブがコンパイルできていない可能性が高い）。"
			summary "プローブの自動更新: **ビルドが $conclusion**。[run]($run_url)"
			;;
	esac
	exit 1
fi

# ---------------------------------------------------------------------------
# 3. 何が公開されたかを読み、PR へ 1 つだけ置いたコメントを書き換える
# ---------------------------------------------------------------------------
#
# ビルド ID と「入っているプローブ」は**リリース本文の隠しメタデータ**が真実
# （probe-build.yml が build= / probes= として書く。プラグインの自動アップデートも
# 同じ行を読む）。ここで作り直さない。
build_id=""
probes_line=""
rel="$(workfile)"
if [ "$(api_json "$VW_API/releases/tags/$RELEASE_TAG" "$rel")" = "200" ]; then
	body="$(jq -r '.body // empty' "$rel" 2>/dev/null)"
	build_id="$(printf '%s\n' "$body" | sed -n 's/^build=//p' | head -n 1 | tr -d '\r')"
	probes_line="$(printf '%s\n' "$body" | sed -n 's/^probes=//p' | head -n 1 | tr -d '\r')"
fi
rm -f "$rel"

release_url="https://github.com/$VW_REPO/releases/tag/$RELEASE_TAG"
echo "published: build=${build_id:-unknown} probes=${probes_line:-?}"
summary \
	"プローブの自動更新: 公開しました（ビルド ID \`${build_id:-unknown}\`）。" \
	"" \
	"- 入っているプローブ: ${probes_line:-（不明）}" \
	"- [ビルドの run]($run_url) / [リリース]($release_url)"

[ "$COMMENT" -eq 1 ] || exit 0

comment_body="$(
	cat <<EOF
$COMMENT_MARKER
**実機プローブを公開しました。** この PR のプローブを main のものと同居させた
ビルドが、リリース（タグ [\`$RELEASE_TAG\`]($release_url)）に載っています。

| | |
| --- | --- |
| ビルド ID | \`${build_id:-unknown}\` |
| 入っているプローブ | ${probes_line:-（不明）} |
| ビルド | [run]($run_url) |

**すでにプラグインを入れてあるなら何もしなくてよい**——次に Vectorworks を起動した
ときにプラグイン自身が「入れ替えますか？」と尋ねます（メニューの先頭項目からも
確認できます）。初回だけの入れ方は
[plugin/README.md](https://github.com/$VW_REPO/blob/main/plugin/README.md)。

<sub>このコメントは push のたびに書き換わります（増えません）。</sub>
EOF
)"

# 既にこの PR へ置いたコメントがあれば書き換える。push のたびに新しいコメントが
# 増えると、PR のレビューが読めなくなる。
comments="$(workfile)"
comment_id=""
if [ "$(api_json "$VW_API/issues/$PR/comments?per_page=100" "$comments")" = "200" ]; then
	comment_id="$(jq -r --arg m "$COMMENT_MARKER" \
		'first(.[] | select(.body | startswith($m)) | .id) // empty' "$comments" 2>/dev/null)"
fi
rm -f "$comments"

payload="$(jq -n --arg body "$comment_body" '{body: $body}')"
if [ -n "$comment_id" ]; then
	code="$(api -o /dev/null -w '%{http_code}' -X PATCH -d "$payload" \
		"$VW_API/issues/comments/$comment_id")"
else
	code="$(api -o /dev/null -w '%{http_code}' -X POST -d "$payload" \
		"$VW_API/issues/$PR/comments")"
fi
case "$code" in
	200 | 201) echo "commented on PR #$PR" ;;
	# 公開そのものは済んでいるので、コメントできなくても成功にする（要約とログには残る）。
	*) echo "::warning::PR #$PR へコメントできませんでした（HTTP $code）。公開そのものは成功しています。" ;;
esac

exit 0
