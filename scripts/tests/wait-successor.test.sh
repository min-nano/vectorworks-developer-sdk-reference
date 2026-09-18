#!/usr/bin/env bash
#
#	wait-successor.test.sh — 「待ち行列を奪われたら後続の run を待ち直す」
#	（scripts/ci-common.sh の wait_run_or_successor と、後続を選ぶ条件）の単体テスト。
#
#	ここを試験にした理由: probe-build.yml の dispatch はどれも ref=main で同じ
#	concurrency グループに入り、GitHub はそこに「走行中 1 本＋待機 1 本」しか置かない。
#	**プローブを持つ PR が 3 本同時に動くと、待機していた run が cancelled になる**
#	（issue #89 で 2 度踏んだ）。奪われたのは待ち行列だけで、奪った側のビルドには
#	この PR のプローブも入っているので、正しい振る舞いは「後続を待ち直す」こと。
#	特に押さえたいのは次の 3 つ:
#
#	  * **cancelled をビルドの失敗として報告しない。** 「プローブがコンパイルできて
#	    いない可能性が高い」と出ると、読んだ人（や AI）はまず自分のプローブを疑う。
#	  * **自分のプローブが載らない run へ乗り換えない。** 顔ぶれを絞って手で叩かれた
#	    dispatch を待っても、この PR のプローブは公開されない。番号の一部に一致させる
#	    （84 を探して 8 や 884 を掴む）のも同じ事故になる。
#	  * **必ず有限時間で返る。** 待ち直しが無限に続くと、待機のぶら下がり——この
#	    仕組みがいちばん避けたかった壊れ方——に戻る。
#
#	`ci-common.sh` を source して、**網の境目（api_json）だけ**を差し替える。だから
#	乗り換えの判断・後続の選び方・打ち切りといった本当のロジックはそのまま走る。
#
#	走らせ方（CI の lint ワークフローも同じ）:
#	    bash scripts/tests/wait-successor.test.sh
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CI_TOOL="wait-successor-test"
# shellcheck source=scripts/ci-common.sh
. "$HERE/../ci-common.sh" || {
	echo "ERROR: scripts/ci-common.sh を読み込めません。" >&2
	exit 1
}

POLL=1
TIMEOUT=60

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

# ---------------------------------------------------------------------------
# 網の境目の差し替え。run の状態は `$FIXTURES/run-<id>`、run 一覧は `$FIXTURES/list`
# に置き、差し替えた api_json が URL を見てそれを返す。**配列を使わない**（macOS 既定の
# bash 3.2 でも走るように）。
# ---------------------------------------------------------------------------
FIXTURES="$(mktemp -d)"
# ci-common.sh は source した時点で EXIT に自分の後始末（ci_cleanup）を掛けている。
# 上書きすると向こうの作業ディレクトリが残るので、**両方**呼ぶ。
trap 'rm -rf "$FIXTURES"; ci_cleanup' EXIT

# run <id> <status> <conclusion>: その run の状態を仕込む。
run() {
	printf '{"status":"%s","conclusion":"%s"}' "$2" "$3" >"$FIXTURES/run-$1"
}

# list <JSON>: run 一覧（.workflow_runs の中身）を仕込む。
list() {
	printf '{"workflow_runs":%s}' "$1" >"$FIXTURES/list"
}

# 一覧を引きに行った回数（＝後続を探した回数）。「success のときは探しに行かない」
# 「打ち切りが効いている」を確かめるのに使う。**ファイルに数える**——api_json は
# コマンド置換（サブシェル）から呼ばれるので、変数に数えても呼び出し側へ返らない。
LIST_CALLS_FILE="$FIXTURES/list-calls"
: >"$LIST_CALLS_FILE"

list_calls() {
	wc -l <"$LIST_CALLS_FILE" | tr -d ' '
}

api_json() {
	local url="$1" out="$2" id
	case "$url" in
		*/actions/runs/*)
			id="${url##*/actions/runs/}"
			if [ ! -f "$FIXTURES/run-$id" ]; then
				printf '{"message":"run %s was not expected"}' "$id" >"$out"
				echo "500"
				return 0
			fi
			cat "$FIXTURES/run-$id" >"$out"
			echo "200"
			;;
		*/runs\?branch=*)
			echo "." >>"$LIST_CALLS_FILE"
			if [ ! -f "$FIXTURES/list" ]; then
				printf '{"message":"the run list was not expected"}' >"$out"
				echo "500"
				return 0
			fi
			cat "$FIXTURES/list" >"$out"
			echo "200"
			;;
		*)
			printf '{"message":"unexpected url: %s"}' "$url" >"$out"
			echo "500"
			;;
	esac
}

# wait_pr <最初の run id> <PR 番号>: probe-auto-update.sh とまったく同じ条件で
# wait_run_or_successor を呼ぶ（RC / WAIT_CONCLUSION / WAIT_RUN_ID を見る）。
# **コマンド置換で受けない**——サブシェルでは WAIT_RUN_ID が呼び出し側へ返らない。
RC=0
wait_pr() {
	wait_run_or_successor "$1" "probe-build.yml" "main" "$SUCCESSOR_CARRIES_PR" \
		--arg re "$(successor_title_re "$2")" 2>/dev/null
	RC="$?"
}

reset() {
	rm -f "$FIXTURES"/run-* "$FIXTURES"/list
	: >"$LIST_CALLS_FILE"
	TIMEOUT=60
	MAX_HANDOFFS=4
}

# ---------------------------------------------------------------------------
echo "--- ふつうに終わったとき ---"
reset
run 100 completed success
wait_pr 100 84
check "conclusion をそのまま返す" "$WAIT_CONCLUSION" "success"
check "待った run は変わらない" "$WAIT_RUN_ID" "100"
check "終了ステータスは 0" "$RC" "0"
check "後続を探しに行かない" "$(list_calls)" "0"

echo "--- ビルドが落ちたとき（failure）---"
reset
run 100 completed failure
wait_pr 100 84
check "failure は待ち直さない（これは本当の失敗）" "$WAIT_CONCLUSION" "failure"
check "後続を探しに行かない" "$(list_calls)" "0"
check "終了ステータスは 0（conclusion は見届けた）" "$RC" "0"

echo "--- 待ち行列を奪われたとき（後続は他の PR も載せる dispatch）---"
reset
run 100 completed cancelled
run 101 completed success
list '[{"id":101,"event":"workflow_dispatch","display_title":"probe build (PR 83,84,88 + main)"},
       {"id":100,"event":"workflow_dispatch","display_title":"probe build (PR 83,84 + main)"}]'
wait_pr 100 84
check "後続へ乗り換える" "$WAIT_RUN_ID" "101"
check "後続の conclusion を返す" "$WAIT_CONCLUSION" "success"
check "終了ステータスは 0" "$RC" "0"

echo "--- 奪ったのが push のビルドだったとき ---"
reset
run 100 completed cancelled
run 102 completed success
list '[{"id":102,"event":"push","display_title":"probe build (open PR + main)"}]'
wait_pr 100 84
check "push のビルドも後続として認める（open な PR を全部載せる）" "$WAIT_RUN_ID" "102"
check "後続の conclusion を返す" "$WAIT_CONCLUSION" "success"

echo "--- 後続が 2 本あるとき ---"
reset
run 100 completed cancelled
run 105 completed success
list '[{"id":105,"event":"workflow_dispatch","display_title":"probe build (PR 84 + main)"},
       {"id":103,"event":"workflow_dispatch","display_title":"probe build (PR 84 + main)"}]'
wait_pr 100 84
check "いちばん新しい後続を待つ（最後に公開するのはそれ）" "$WAIT_RUN_ID" "105"

echo "--- 後続がこの PR を載せないとき ---"
reset
run 100 completed cancelled
list '[{"id":101,"event":"workflow_dispatch","display_title":"probe build (PR 83,88 + main)"}]'
wait_pr 100 84
check "**乗り換えない**（待っても自分のプローブは載らない）" "$WAIT_RUN_ID" "100"
check "cancelled のまま返す" "$WAIT_CONCLUSION" "cancelled"
check "終了ステータスは 0（conclusion は見届けた）" "$RC" "0"

echo "--- 番号の一部に一致させないこと ---"
reset
run 100 completed cancelled
list '[{"id":101,"event":"workflow_dispatch","display_title":"probe build (PR 84,884 + main)"}]'
wait_pr 100 8
check "8 を探して 84 / 884 を掴まない" "$WAIT_RUN_ID" "100"
reset
run 100 completed cancelled
run 101 completed success
list '[{"id":101,"event":"workflow_dispatch","display_title":"probe build (PR 8,84 + main)"}]'
wait_pr 100 8
check "1 つの要素として入っていれば掴む" "$WAIT_RUN_ID" "101"

echo "--- 古い run を後続と読まないこと ---"
reset
run 100 completed cancelled
list '[{"id":99,"event":"workflow_dispatch","display_title":"probe build (PR 84 + main)"}]'
wait_pr 100 84
check "自分より古い run へは乗り換えない" "$WAIT_RUN_ID" "100"

echo "--- 後続が 1 本も無いとき（人が止めた見込み）---"
reset
run 100 completed cancelled
list '[]'
wait_pr 100 84
check "cancelled のまま返す" "$WAIT_CONCLUSION" "cancelled"
check "待った run は変わらない" "$WAIT_RUN_ID" "100"

echo "--- 一覧が引けなかったとき ---"
reset
run 100 completed cancelled
wait_pr 100 84
check "cancelled のまま返す（作り話をしない）" "$WAIT_CONCLUSION" "cancelled"
check "待った run は変わらない" "$WAIT_RUN_ID" "100"

echo "--- 奪われ続けたとき（必ず有限回で打ち切る）---"
reset
MAX_HANDOFFS=2
run 100 completed cancelled
run 101 completed cancelled
run 102 completed cancelled
run 103 completed cancelled
list '[{"id":103,"event":"push","display_title":"probe build (open PR + main)"},
       {"id":102,"event":"push","display_title":"probe build (open PR + main)"},
       {"id":101,"event":"push","display_title":"probe build (open PR + main)"}]'
wait_pr 100 84
check "MAX_HANDOFFS 回で打ち切る" "$WAIT_RUN_ID" "103"
check "cancelled のまま返す" "$WAIT_CONCLUSION" "cancelled"
check "後続を探した回数も上限どおり" "$(list_calls)" "2"

echo "--- 残り時間が無いとき ---"
reset
TIMEOUT=30
run 100 completed cancelled
run 101 completed success
list '[{"id":101,"event":"push","display_title":"probe build (open PR + main)"}]'
wait_pr 100 84
check "**総待機は最初の TIMEOUT を越えない**（越えるとウォッチドッグが先に発火する）" \
	"$WAIT_CONCLUSION" "timed-out-waiting"
check "終了ステータスは 1（見届けられなかった）" "$RC" "1"
check "TIMEOUT を元へ戻す" "$TIMEOUT" "30"

echo "--- 同じ顔ぶれを選ぶ条件（probe-release-guard.sh が使う）---"
reset
run 100 completed cancelled
run 101 completed success
list '[{"id":101,"event":"workflow_dispatch","display_title":"probe build (PR 83,84 + main)"}]'
wait_run_or_successor 100 "probe-build.yml" "main" "$SUCCESSOR_SAME_TITLE" \
	--arg t "probe build (PR 83,84 + main)" 2>/dev/null
check "run 名が一致する後続へ乗り換える" "$WAIT_RUN_ID" "101"
reset
run 100 completed cancelled
list '[{"id":101,"event":"workflow_dispatch","display_title":"probe build (PR 83 + main)"}]'
wait_run_or_successor 100 "probe-build.yml" "main" "$SUCCESSOR_SAME_TITLE" \
	--arg t "probe build (PR 83,84 + main)" 2>/dev/null
check "顔ぶれが違う後続へは乗り換えない" "$WAIT_RUN_ID" "100"

echo
if [ "$FAILURES" -eq 0 ]; then
	echo "OK: $CHECKS checks passed"
	exit 0
fi
echo "NG: $FAILURES / $CHECKS checks failed"
exit 1
