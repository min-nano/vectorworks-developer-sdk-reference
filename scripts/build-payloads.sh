#!/usr/bin/env bash
#
# build-payloads.sh — 本体（.vwpayload）を**群ごとに 1 本ずつ**ビルドする。
#
# なぜこれがあるか
# ----------------
# プローブは群（main と、PR ごと）に分かれた本体へコンパイルされる
# （plugin/CMakeLists.txt）。**まとめて 1 本にしていたときは、誰か 1 人のプローブが
# コンパイルできないだけで全員のビルドが落ちた**——実機で確かめたい他の PR まで、
# 巻き添えで配れなくなる。
#
# そこで 1 本ずつ作り、**落ちた群だけを外して**先へ進む:
#
#   * main の群が落ちたら**失敗**（＝マージ済みのコードが壊れている。見逃さない）
#   * PR の群が落ちたら**記録して続行**（その PR のプローブだけが配られない）
#
# 結果は <status ファイル> へ "<群>=ok" / "<群>=failed" の形で 1 行ずつ書く。
# 呼び出し側（.github/workflows/probe-build.yml）はこれを見て、zip に入れるものを
# 決め、リリースノートの表と隠しメタデータ（payloads=）を作る。
#
# **落ちた群のことは必ずどこかに出す。** 黙って消えると、実機で「なぜ自分のプローブが
# 無いのか」が分からない（カタログには残るので、殻は「※本体なし」と出す）。
#
# 使い方:
#   scripts/build-payloads.sh --build <ビルドディレクトリ> --groups <groups.txt> \
#       --status <出力ファイル> [--config Release] [--parallel 4]
#
# 【macOS の bash 3.2 で踏んだ落とし穴】**変数のすぐ後ろに日本語を書くときは
# `${var}` と括る。** `"$label（…）"` のように書くと、macOS 既定の bash 3.2 は
# 全角括弧の先頭バイトを変数名の一部として読み、`set -u` のもとで
# 「label…: unbound variable」で落ちる（実際に mac のビルドだけが落ちた）。
# ubuntu の bash 5 では起きないので、CI の mac ジョブでしか顕在化しない。
#
set -uo pipefail

BUILD_DIR="build"
GROUPS_FILE="build-probes/groups.txt"
STATUS_FILE="payload-status.txt"
CONFIG="Release"
PARALLEL=""

usage() {
	awk 'NR > 1 { if ($0 !~ /^#/) exit; print }' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--build)
			BUILD_DIR="${2:?--build にはディレクトリが要ります}"
			shift 2
			;;
		--groups)
			GROUPS_FILE="${2:?--groups にはファイルが要ります}"
			shift 2
			;;
		--status)
			STATUS_FILE="${2:?--status にはファイルが要ります}"
			shift 2
			;;
		--config)
			CONFIG="${2:?--config には構成が要ります}"
			shift 2
			;;
		--parallel)
			PARALLEL="${2:?--parallel には数が要ります}"
			shift 2
			;;
		-h | --help)
			usage
			exit 0
			;;
		*)
			echo "build-payloads: 知らない引数: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [ ! -f "$GROUPS_FILE" ]; then
	echo "::error::群の一覧がありません: ${GROUPS_FILE}（集約が走っていますか）" >&2
	exit 2
fi

: >"$STATUS_FILE"
failed_main=0
failed_prs=""

while IFS='|' read -r group pr commit branch title; do
	[ -n "$group" ] || continue
	target="VwSdkProbesPayload-$group"
	label="群 $group"
	if [ -n "$pr" ]; then
		label="${label}（PR #$pr $commit${title:+ / $title}）"
	else
		label="${label}（$branch ${commit}）"
	fi

	echo "::group::$target をビルド（${label}）"
	args=(--build "$BUILD_DIR" --config "$CONFIG" --target "$target")
	if [ -n "$PARALLEL" ]; then
		args+=(--parallel "$PARALLEL")
	fi
	if cmake "${args[@]}"; then
		echo "$group=ok" >>"$STATUS_FILE"
		echo "::endgroup::"
		continue
	fi
	echo "::endgroup::"

	echo "$group=failed" >>"$STATUS_FILE"
	if [ -z "$pr" ]; then
		# main の群が通らない＝マージ済みのコードが壊れている。**必ず落とす。**
		echo "::error::$label の本体をビルドできませんでした（main のプローブは常に" \
			"コンパイルできなければなりません）。" >&2
		failed_main=1
	else
		# PR の群。**他の群は配る**（それがこの分割の目的）。PR 側のチェックは
		# scripts/probe-auto-update.sh が payloads= を見て赤くする。
		echo "::warning::$label の本体をビルドできませんでした。この PR のプローブは" \
			"今回のビルドに入りません（他の群はそのまま配ります）。" >&2
		failed_prs="$failed_prs #$pr"
	fi
done <"$GROUPS_FILE"

echo "---- 本体のビルド結果 ----"
cat "$STATUS_FILE"
if [ -n "$failed_prs" ]; then
	echo "ビルドできなかった PR:$failed_prs"
fi

exit "$failed_main"
