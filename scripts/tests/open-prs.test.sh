#!/usr/bin/env bash
#
#	open-prs.test.sh — 「いま open な PR は何番か」を出す `open_prs`（scripts/ci-common.sh）の
#	単体テスト。
#
#	ここを試験にした理由: この 1 行が**プローブのリリースに何が入るか**を決める
#	（本体は群に分かれていて、プローブを持つ open な PR ごとに 1 本入る）。しかも
#	**壊れても静かに壊れる**——落ちた群はリリースノートの表に載らないだけなので、
#	利用者からは「入れ替えたのに出てこない」に見える（issue #79 で実際に起きた）。
#	特に押さえたいのは次の 2 つ:
#
#	  * **「取れなかった」を「1 件も無い」と混同しない。** 空行＋成功で返してしまうと、
#	    呼び出し側が open な PR の群を落としたビルドを公開する。
#	  * **黙って切り捨てない。** 1 頁に収まらないときは頁送りし、上限に達したら
#	    足りない一覧を返さずに失敗する。
#
#	`ci-common.sh` を source して、**網の境目（api_json）だけ**を差し替える。だから
#	頁送りの判断・並べ替え・重複の始末といった本当のロジックはそのまま走る。
#
#	走らせ方（CI の lint ワークフローも同じ）:
#	    bash scripts/tests/open-prs.test.sh
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CI_TOOL="open-prs-test"
# shellcheck source=scripts/ci-common.sh
. "$HERE/../ci-common.sh" || {
	echo "ERROR: scripts/ci-common.sh を読み込めません。" >&2
	exit 1
}

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
# 網の境目の差し替え。頁ごとの応答（HTTP コードと本文）を fixture で仕込んでおき、
# 差し替えた api_json が、呼ばれた URL の page= を見てそれを返す。**配列を使わない**
# （macOS 既定の bash 3.2 でも走るように）——頁ごとの値はファイルに置く。
# ---------------------------------------------------------------------------
FIXTURES="$(mktemp -d)"
# ci-common.sh は source した時点で EXIT に自分の後始末（ci_cleanup）を掛けている。
# 上書きすると向こうの作業ディレクトリが残るので、**両方**呼ぶ。
trap 'rm -rf "$FIXTURES"; ci_cleanup' EXIT

# fixture <page> <code> <json>: その頁の応答を仕込む。
fixture() {
	printf '%s' "$2" >"$FIXTURES/code-$1"
	printf '%s' "$3" >"$FIXTURES/body-$1"
}

# api_json を差し替える（本物と同じ「コードを stdout、本文を $2 のファイルへ」）。
api_json() {
	local url="$1" out="$2" page
	page="$(printf '%s' "$url" | sed -n 's/.*[?&]page=\([0-9][0-9]*\).*/\1/p')"
	page="${page:-1}"
	if [ ! -f "$FIXTURES/code-$page" ]; then
		# 仕込んでいない頁を引きに行ったら試験の失敗（本物は 200 で空配列を返す）。
		printf '{"message":"page %s was not expected"}' "$page" >"$out"
		echo "500"
		return 0
	fi
	cat "$FIXTURES/body-$page" >"$out"
	cat "$FIXTURES/code-$page"
}

# run_open_prs: open_prs を 1 回走らせ、出力を OUT へ、終了ステータスを RC へ入れる
# （stderr は黙らせる）。**コマンド置換で受けない**——`$(...)` はサブシェルなので、
# その中で代入した RC は呼び出し側へ返らず、失敗が全部「0」に見えてしまう。
OUT=""
RC=0
run_open_prs() {
	open_prs >"$FIXTURES/out" 2>/dev/null
	RC="$?"
	OUT="$(cat "$FIXTURES/out")"
}

# ---------------------------------------------------------------------------
echo "--- 1 頁で収まるとき ---"
rm -f "$FIXTURES"/code-* "$FIXTURES"/body-*
OPEN_PRS_PER_PAGE=100
fixture 1 200 '[{"number":80},{"number":77},{"number":79}]'
run_open_prs
check "昇順・カンマ区切りで返す" "$OUT" "77,79,80"
check "終了ステータスは 0" "$RC" "0"

echo "--- 1 件も open が無いとき ---"
rm -f "$FIXTURES"/code-* "$FIXTURES"/body-*
fixture 1 200 '[]'
run_open_prs
check "空で返す" "$OUT" ""
check "終了ステータスは 0（「無い」は異常ではない）" "$RC" "0"

echo "--- 1 頁に収まらないとき ---"
rm -f "$FIXTURES"/code-* "$FIXTURES"/body-*
OPEN_PRS_PER_PAGE=3
fixture 1 200 '[{"number":12},{"number":15},{"number":9}]'
fixture 2 200 '[{"number":4}]'
run_open_prs
check "頁送りして全部拾う" "$OUT" "4,9,12,15"
check "終了ステータスは 0" "$RC" "0"

echo "--- 取得に失敗したとき ---"
rm -f "$FIXTURES"/code-* "$FIXTURES"/body-*
OPEN_PRS_PER_PAGE=100
fixture 1 403 '{"message":"Resource not accessible by integration"}'
run_open_prs
check "**空の一覧を返さない**（「取れなかった」を「無い」と読ませない）" "$OUT" ""
check "終了ステータスは 1" "$RC" "1"

echo "--- 2 頁目で失敗したとき ---"
rm -f "$FIXTURES"/code-* "$FIXTURES"/body-*
OPEN_PRS_PER_PAGE=2
fixture 1 200 '[{"number":1},{"number":2}]'
fixture 2 500 '{"message":"boom"}'
run_open_prs
check "途中まで拾った分も返さない（欠けた一覧は使わせない）" "$OUT" ""
check "終了ステータスは 1" "$RC" "1"

echo "--- 頁の上限に達したとき ---"
rm -f "$FIXTURES"/code-* "$FIXTURES"/body-*
OPEN_PRS_PER_PAGE=1
OPEN_PRS_MAX_PAGES=2
fixture 1 200 '[{"number":1}]'
fixture 2 200 '[{"number":2}]'
run_open_prs
check "切り捨てずに失敗する" "$OUT" ""
check "終了ステータスは 1" "$RC" "1"
OPEN_PRS_MAX_PAGES=10

echo "--- 配列でない応答が返ったとき ---"
rm -f "$FIXTURES"/code-* "$FIXTURES"/body-*
OPEN_PRS_PER_PAGE=100
fixture 1 200 '{"message":"not an array"}'
run_open_prs
check "一覧として読まない" "$OUT" ""
check "終了ステータスは 1" "$RC" "1"

echo
if [ "$FAILURES" -eq 0 ]; then
	echo "OK: $CHECKS checks passed"
	exit 0
fi
echo "NG: $FAILURES / $CHECKS checks failed"
exit 1
