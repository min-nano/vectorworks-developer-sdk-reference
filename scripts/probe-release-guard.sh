#!/usr/bin/env bash
#
# probe-release-guard.sh — 公開されている転がりリリース（タグ `probes`）が、**いま
# open な PR の顔ぶれとずれていないか**を確かめ、ずれていたら作り直す。
# `.github/workflows/probe-release-guard.yml` から呼ばれる。
#
# なぜこれがあるか（issue #76 で開いた穴）
# ----------------------------------------
# リリースに載る本体は**群**に分かれている——main のプローブが 1 本、**その時点で open な
# PR のプローブが PR ごとに 1 本**（scripts/gather-probes.sh）。つまり
# **「リリースに何が入っているべきか」は main のツリーだけでは決まらない**。
# open な PR の集合という、**コミットを 1 つも作らずに変わる**ものが効いている。
#
# ところが作り直しの引き金は 2 つとも「ツリーが変わったこと」だった:
#
#   * probe-build.yml の push (main) … main で `paths`（`probes/runtime/**` ほか）が動いたとき
#   * probe-auto-update.yml           … PR で `probes/runtime/**` が動いたとき
#
# **プローブを持つ PR がマージされる瞬間に、この 2 つはどちらも引かない。** 規約
# （CLAUDE.md「調査のフロー」4）は「調査に使ったプローブは**マージする前に消す**」で、
# 足して消した差分は squash merge で相殺されるので、main への差分に `probes/runtime/**` が
# 1 行も現れないからである。結果、**リリースは「消したはずのプローブを含むビルド」を
# 指したまま**になる——実際に #69 / #72 / #74 の 3 回とも起きた（#76）。
#
# だから引き金を「ツリーが変わったこと」ではなく「**PR が閉じたこと**」に置き、
# 見るものを「公開されているリリースの中身」と「いま open な PR」の**突き合わせ**にする。
#
# なお **push のビルドも、いまは open な PR を全部載せる**（#79 で直した——以前は
# push 起動だけが顔ぶれを main に狭め、open な PR の群をリリースから落としていた。
# 最初にそれを見付けたのがこの点検である）。したがってここが受け持つのは
# **「PR の顔ぶれが変わったのに、ビルドを起こすものが何も無かった」**場合だけ
# ——プローブを持つ PR が閉じたときと、取りこぼしの週 1 回である。
#
# 何を突き合わせるか
# ------------------
# リリース本文の隠しメタデータ `inputs=` は、そのビルドの材料そのものである:
#
#   inputs=main=<main の sha> pr=74:<PR #74 の head sha> pr=80:<…>
#
# **`pr=` の並びだけ**を、`scripts/gather-probes.sh --prs <いま open な PR 全部>` が
# いま出す `pr=` の並びと比べる。**同じ集約コードに「何が入るべきか」を訊く**ので、
# 「プローブを消した PR」「プローブを持たない PR」「main と同じものしか無い PR」を
# どう扱うかの判断が 2 か所に分かれない。
#
# **`main=` は比べない。** main の sha は Findings の 1 行直しでも動くので、比べると
# **マージのたびに mac / Windows の実ビルドが走る**。main のツリーの次元は
# probe-build.yml の push `paths` が正しく見ている（#76 で確かめた——止まっていたのでは
# なく、引っ掛かるものが 1 つも無かった）。ここが受け持つのは**その `paths` に原理的に
# 写らない次元**＝ PR の顔ぶれだけである。
#
# 何をするか
# ----------
#   0. main の "Probe plug-in" が走っていれば**終わるまで待つ**（走行中のビルドが
#      これから公開するものを、古いリリースとして読まないため）。
#   1. リリース `probes` の `inputs=` から `pr=` の並びを読む（**公開されているもの**）。
#   2. `gather-probes.sh --prs <open な PR 全部>` を走らせて `pr=` の並びを得る（**あるべきもの**）。
#   3. 一致していれば何もしない。**ふつうはここで終わる**——プローブを持たない PR の
#      マージでは両方とも空なので、1 分ほどで緑になる。
#   4. 食い違っていれば probe-build.yml を `prs=<open な PR 全部>` で dispatch し、
#      **完了まで待って**結果をこのスクリプトの終了ステータスにする。
#
# **ビルドと公開は人が手で叩くときとまったく同じ道**を通る（このスクリプトは何も
# ビルドしないし、リリースも作らない）。
#
# 使い方
# ------
#   scripts/probe-release-guard.sh
#
#   オプション:
#     --ref R       ビルドを走らせるブランチ（既定 main。ワークフローの置き場所）
#     --dry-run     ずれを報告するだけで dispatch しない（動作確認用）
#     --no-wait     dispatch だけして待たない
#     --poll S      ポーリング間隔・秒（既定 20）
#     --timeout S   待機 1 回あたりの上限・秒（既定 4200 = 70 分）
#
# **必ず終わること**（設計の要）
# ------------------------------
# 待機の歯止め（HTTP の時間上限・締切判定・ウォッチドッグ）と待機ループ本体、run の
# 特定は `ci-common.sh` にある。**待機ループをここへ書かないこと**（CLAUDE.md
# 「CI の完了を待つ」）。走行中のビルドを待つ probe だけは `poll_until` の上に 1 つ置く。
#
# 環境変数:
#   GH_TOKEN / GITHUB_TOKEN   必須。dispatch に actions: write、PR の一覧に
#                             pull-requests: read が要る
#   VW_REPO                   owner/repo（既定は ci-common.sh）
#   GITHUB_STEP_SUMMARY       あればジョブの要約にも書く
#   その他の共通設定（HTTP の上限・生存出力の間隔など）は ci-common.sh を参照。
#
# 終了ステータス: ずれが無い / 作り直せたら 0、作り直しが失敗・見届けられなければ 1。
# 使い方の誤りや dispatch の失敗は 2。
#
set -uo pipefail

CI_TOOL="probe-release-guard"
PRG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source-path=SCRIPTDIR
# shellcheck source=scripts/ci-common.sh
. "$PRG_DIR/ci-common.sh" || {
	echo "probe-release-guard: error: scripts/ci-common.sh を読み込めません" >&2
	exit 2
}

WORKFLOW_FILE="probe-build.yml"
RELEASE_TAG="probes"

REF="main"
POLL="${PROBE_RELEASE_GUARD_POLL:-20}"
TIMEOUT="${PROBE_RELEASE_GUARD_TIMEOUT:-4200}"
DRY_RUN=0
WAIT=1

# usage: ヘッダのコメントブロックをそのままヘルプとして出す（説明を二重に持たない）。
usage() {
	awk 'NR > 1 { if ($0 !~ /^#/) exit; print }' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--ref)
			REF="${2:-}"
			shift 2
			;;
		--poll)
			POLL="${2:-}"
			shift 2
			;;
		--timeout)
			TIMEOUT="${2:-}"
			shift 2
			;;
		--dry-run)
			DRY_RUN=1
			shift
			;;
		--no-wait)
			WAIT=0
			shift
			;;
		-h | --help)
			usage
			exit 0
			;;
		*) die "未知のオプション: $1" ;;
	esac
done

[ -n "$TOKEN" ] || die "GITHUB_TOKEN / GH_TOKEN が未設定です（dispatch には actions: write が要ります）"
[ -n "$REF" ] || die "--ref が空です"
require_positive_int "$POLL" "--poll"
require_positive_int "$TIMEOUT" "--timeout"

# 待機は最大 2 回（走行中のビルド → 作り直したビルド）。その合計＋余裕を過ぎたら
# 自分を殺す。締切判定より外側で何かが固まっても、プロセスは必ず終わる。
start_watchdog "$((TIMEOUT * 2 + 600))"

# summary <行…>: ジョブの要約（GITHUB_STEP_SUMMARY）へ書く。無い環境では何もしない。
summary() {
	[ -n "${GITHUB_STEP_SUMMARY:-}" ] || return 0
	printf '%s\n' "$@" >>"$GITHUB_STEP_SUMMARY"
}

# pr_parts <ファイル>: `pr=<番号>:<sha>` の並びを**空白区切りの 1 行**にして stdout へ
# （番号順に正規化。無ければ空行）。入力は「1 行 1 件」でも「1 行に空白区切り」でも
# よい——リリースの `inputs=` は後者、gather-probes の build-id-source.txt は前者だから
# である。1 行に畳むのは、突き合わせを単なる文字列比較で済ませるため。
pr_parts() {
	tr '[:space:]' '\n' <"$1" | sed -n 's/^\(pr=[0-9][0-9]*:[0-9a-f][0-9a-f]*\)$/\1/p' |
		sort -u | tr '\n' ' ' | sed 's/ *$//'
	echo
}

# ---------------------------------------------------------------------------
# 0. main のビルドが走っていれば終わるまで待つ
# ---------------------------------------------------------------------------
#
# **走行中のビルドの「まだ公開されていない中身」を、古いリリースとして読まない**ため。
# プローブを持つ PR のマージでは push (main) のビルドとこの点検が同時に起きうるので、
# ここを飛ばすと「ずれている」と読み違えて二重にビルドしてしまう。
PROBE_ACTIVE_BODY=""

# shellcheck disable=SC2317,SC2329 # poll_until から名前で間接的に呼ばれる。
probe_no_active_build() {
	local code n
	code="$(api_json "$VW_API/actions/workflows/$WORKFLOW_FILE/runs?branch=$REF&per_page=30" "$PROBE_ACTIVE_BODY")"
	if [ "$code" != "200" ]; then
		echo "$CI_TOOL: run 一覧の取得に失敗（HTTP $code, ${POLL_ELAPSED}s）" >&2
		if fatal_http "$code"; then
			echo "$CI_TOOL: 回復しないエラーなので待機を打ち切ります: $(api_message "$PROBE_ACTIVE_BODY")" >&2
			return 2
		fi
		return 3
	fi
	n="$(jq -r '[.workflow_runs[] | select(.status != "completed")] | length' "$PROBE_ACTIVE_BODY" 2>/dev/null)"
	n="${n:-0}"
	POLL_STATUS="走行中の ${WORKFLOW_FILE}=${n} 本"
	[ "$n" -eq 0 ] && return 0
	return 1
}

# **見届けられなくても止めない。** ここは読み違いを減らすための前置きで、判断そのもの
# ではない（判断の材料はこの後リリースから取り直す）。待ち切れなかったら、いまの
# リリースで判断して先へ進む——最悪でも「余分に 1 本ビルドする」で済む。
PROBE_ACTIVE_BODY="$(workfile)"
poll_until probe_no_active_build
case "$?" in
	0) ;;
	1) echo "::warning::${WORKFLOW_FILE} の走行中のビルドが ${TIMEOUT}s 待っても終わりませんでした。いまのリリースで判断します。" ;;
	*) echo "::warning::走行中のビルドを確かめられませんでした。いまのリリースで判断します。" ;;
esac

# ---------------------------------------------------------------------------
# 1. 公開されているリリースの中身を読む
# ---------------------------------------------------------------------------
#
# 突き合わせられない（リリースが無い・古い形式）ときは**ずれている扱い**にする。
# 1 回作り直せば以後は比べられる形になるので、黙って見逃すより安い。
FORCE=0
PUBLISHED_PRS=""
published_desc=""

rel="$(workfile)"
code="$(api_json "$VW_API/releases/tags/$RELEASE_TAG" "$rel")"
case "$code" in
	200)
		body="$(jq -r '.body // empty' "$rel" 2>/dev/null)"
		build_id="$(printf '%s\n' "$body" | sed -n 's/^build=//p' | head -n 1 | tr -d '\r')"
		inputs_line="$(printf '%s\n' "$body" | sed -n 's/^inputs=//p' | head -n 1 | tr -d '\r')"
		probes_line="$(printf '%s\n' "$body" | sed -n 's/^probes=//p' | head -n 1 | tr -d '\r')"
		if [ -z "$inputs_line" ]; then
			echo "::warning::リリース $RELEASE_TAG に inputs= がありません（古いビルド）。作り直します。"
			FORCE=1
			published_desc="inputs= を持たない古いビルド"
		else
			inputs_file="$(workfile)"
			printf '%s\n' "$inputs_line" >"$inputs_file"
			PUBLISHED_PRS="$(pr_parts "$inputs_file")"
			published_desc="build=${build_id:-unknown} probes=${probes_line:-（なし）}"
		fi
		;;
	404)
		echo "リリース $RELEASE_TAG がまだありません。作り直します。"
		FORCE=1
		published_desc="リリースなし"
		;;
	*)
		die "リリース $RELEASE_TAG を取得できませんでした（HTTP ${code}）: $(api_message "$rel")"
		;;
esac

# ---------------------------------------------------------------------------
# 2. いま open な PR で「何が入るべきか」を集約に計算させる
# ---------------------------------------------------------------------------
# 一覧の取り方は ci-common.sh の open_prs 1 か所に置いてある（push のビルドと自動公開も
# 同じものを使う。**ここがばらつくと、リリースの中身が引き金によって変わる**。issue #79）。
# **ここは黙って続けてはいけない。** 一覧が取れないまま「open な PR は無い」と読むと、
# 「ずれている」と判じて open な PR のプローブを落としたビルドを公開してしまう。
PRS_LIST="$(open_prs)" ||
	die "open な PR の一覧を取得できませんでした（pull-requests: read がありますか）"
echo "open な PR: ${PRS_LIST:-（なし）}"

gather_out="$WORKDIR/expected"
gather_log="$(workfile)"
gather_args=(--out "$gather_out")
[ -n "$PRS_LIST" ] && gather_args+=(--prs "$PRS_LIST")
if ! "$PRG_DIR/gather-probes.sh" "${gather_args[@]}" >"$gather_log" 2>&1; then
	sed 's/^/  /' "$gather_log" >&2
	die "集約（gather-probes.sh）が失敗しました。プローブのコンパイル以前の問題（slug とディレクトリ名の不一致など）が出ています"
fi
sed 's/^/  /' "$gather_log"

EXPECTED_PRS="$(pr_parts "$gather_out/build-id-source.txt")"

# ---------------------------------------------------------------------------
# 3. 突き合わせる
# ---------------------------------------------------------------------------
echo "公開されている PR の群: ${PUBLISHED_PRS:-（なし）}"
echo "あるべき PR の群:       ${EXPECTED_PRS:-（なし）}"

if [ "$FORCE" -eq 0 ] && [ "$PUBLISHED_PRS" = "$EXPECTED_PRS" ]; then
	echo "ずれなし（${published_desc}）。"
	summary "プローブのリリース点検: **ずれなし**（${published_desc}）。"
	exit 0
fi

drift_note="公開: ${PUBLISHED_PRS:-（なし）} → あるべき: ${EXPECTED_PRS:-（なし）}"
[ "$FORCE" -eq 1 ] && drift_note="${published_desc} → あるべき: ${EXPECTED_PRS:-（なし）}"
echo "::warning::リリース $RELEASE_TAG がいま open な PR とずれています（${drift_note}）。"

if [ "$DRY_RUN" -eq 1 ]; then
	echo "(--dry-run: 作り直しません)"
	summary "プローブのリリース点検: **ずれあり**（${drift_note}）。\`--dry-run\` なので作り直していません。"
	exit 0
fi

# ---------------------------------------------------------------------------
# 4. 作り直す（人が手で叩くときと同じ道）
# ---------------------------------------------------------------------------
#
# 起動前に「今いちばん新しい dispatch の run id」を控える。dispatch API は run の id を
# 返さないので、**これより大きい id** を自分の run を掴む条件に使う。
last_body="$(workfile)"
LAST_RUN_ID="0"
if [ "$(api_json "$VW_API/actions/workflows/$WORKFLOW_FILE/runs?event=workflow_dispatch&per_page=1" "$last_body")" = "200" ]; then
	LAST_RUN_ID="$(jq -r '.workflow_runs[0].id // 0' "$last_body" 2>/dev/null)"
fi

payload="$(jq -n --arg ref "$REF" --arg prs "$PRS_LIST" '{ref: $ref, inputs: {prs: $prs}}')"
resp="$(workfile)"
code="$(api -o "$resp" -w '%{http_code}' -X POST -d "$payload" \
	"$VW_API/actions/workflows/$WORKFLOW_FILE/dispatches")"
if [ "$code" = "403" ]; then
	die "dispatch が権限で拒否されました（HTTP 403）。このトークンに actions: write がありません。Actions の \"Probe plug-in\" を手で dispatch してください（inputs.prs=${PRS_LIST}）"
fi
[ "$code" = "204" ] ||
	die "dispatch に失敗しました（HTTP ${code}）: $(api_message "$resp")。$WORKFLOW_FILE が $REF にあるか確認してください"

echo "dispatched: $WORKFLOW_FILE (ref=$REF prs=${PRS_LIST:-（なし）})"

# run 名は probe-build.yml の run-name が作る（prs が空なら "probe build (main)"）。
if [ -n "$PRS_LIST" ]; then
	run_title="probe build (PR $PRS_LIST + $REF)"
else
	run_title="probe build ($REF)"
fi
RESOLVE_RUN_WHAT="作り直しのビルド（$run_title）"
# shellcheck disable=SC2016 # $last / $t は jq の変数（--arg で渡す）。シェルに展開させない。
run_id="$(resolve_run "$WORKFLOW_FILE" \
	'(.id > ($last | tonumber)) and (.display_title == $t)' \
	--arg last "$LAST_RUN_ID" --arg t "$run_title")" ||
	die "起動したビルドの run を特定できませんでした（$run_title）"

run_url="https://github.com/$VW_REPO/actions/runs/$run_id"
release_url="https://github.com/$VW_REPO/releases/tag/$RELEASE_TAG"
echo "run: $run_url"

if [ "$WAIT" -eq 0 ]; then
	echo "(--no-wait: 結果は見届けません)"
	summary "プローブのリリース点検: ずれ（${drift_note}）を直すビルドを起動しました（[run]($run_url)）。"
	exit 0
fi

conclusion="$(wait_run "$run_id")"
echo "conclusion=$conclusion"

if [ "$conclusion" != "success" ]; then
	# **緑にしない。** 「消したはずのプローブが載ったままのリリース」を誰も知らないまま
	# 実機で走らせることになるのが、この仕組みが防ぎたかったことそのものである。
	echo "::error::リリースの作り直しが $conclusion で終わりました。$run_url を見てください。"
	summary \
		"プローブのリリース点検: **作り直しが $conclusion**（${drift_note}）。" \
		"[run]($run_url) / [リリース]($release_url)"
	exit 1
fi

echo "リリースを作り直しました: $release_url"
summary \
	"プローブのリリース点検: ずれを直しました（${drift_note}）。" \
	"" \
	"- [ビルドの run]($run_url) / [リリース]($release_url)"
exit 0
