#!/usr/bin/env bash
#
# open-prs.sh — **いま open な PR の番号**をカンマ区切りの 1 行で出す（1 件も無ければ
# 空行）。`.github/workflows/probe-build.yml` の gather ジョブが使う。
#
# なぜこれがあるか（issue #79）
# ----------------------------
# プローブのリリース（転がりタグ `probes`）に入る本体は**群**に分かれている——main の
# プローブが 1 本、**プローブを持つ open な PR ごとに 1 本**（scripts/gather-probes.sh）。
# つまり「何が入るべきか」は main のツリーだけでは決まらず、**いま open な PR の集合**が
# 効く。ところが main への push で走るビルドは `inputs.prs` を持たないので、集約に PR が
# 1 つも渡らず、**その回の公開が open な PR の群を丸ごと落としていた**（転がりタグは
# 1 つなので、落ちた群は実機のピッカーからも消える）。
#
# 直し方は「push のビルドでも、dispatch されるときと**同じ顔ぶれ**を渡す」。その
# 顔ぶれを出すのがこのスクリプトで、中身は `ci-common.sh` の `open_prs`——自動公開
# （probe-auto-update.sh）と点検（probe-release-guard.sh）が使うのと**同じ 1 つの実装**
# である（ここがばらつくと、リリースの中身が引き金によって変わる）。
#
# 使い方:
#   scripts/open-prs.sh          # 例: "77,80"（1 件も無ければ空行）
#
# 環境変数:
#   GH_TOKEN / GITHUB_TOKEN   必須（一覧には pull-requests: read が要る）
#   VW_REPO                   owner/repo（既定は ci-common.sh）
#
# 終了ステータス: 取れたら 0（1 件も無くても 0）、取れなければ 1、使い方の誤りは 2。
# **取れなかったときに空行を出して 0 で終わらない**——呼び出し側がそれを「open な PR は
# 無い」と読むと、落とすつもりのない群を落としたビルドを公開してしまう。
#
set -uo pipefail

CI_TOOL="open-prs"
OP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source-path=SCRIPTDIR
# shellcheck source=scripts/ci-common.sh
. "$OP_DIR/ci-common.sh" || {
	echo "open-prs: error: scripts/ci-common.sh を読み込めません" >&2
	exit 2
}

# usage: ヘッダのコメントブロックをそのままヘルプとして出す（説明を二重に持たない）。
usage() {
	awk 'NR > 1 { if ($0 !~ /^#/) exit; print }' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		-h | --help)
			usage
			exit 0
			;;
		*) die "未知のオプション: $1" ;;
	esac
done

[ -n "$TOKEN" ] || die "GITHUB_TOKEN / GH_TOKEN が未設定です（open な PR の一覧には pull-requests: read が要ります）"

open_prs || exit 1
