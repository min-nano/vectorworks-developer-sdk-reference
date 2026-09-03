#!/usr/bin/env bash
#
# probe-release-notes.sh — 転がりリリース（タグ probes）の本文を組み立てる。
#
# なぜ切り出してあるか
# --------------------
# 本文には**機械が読む隠しメタデータ**と、**人が読む「何が入っているか」の表**の両方が
# 入る。前者は自動アップデータ（plugin/scripts/vw-probes-update.*）と PR のチェック
# （scripts/probe-auto-update.sh）が読むので、綴りがずれると「更新が来ない」「PR が
# 理由も分からず赤い」になる。ワークフローの中に埋めたままだと shellcheck も掛からず、
# 手元で確かめることもできないので、1 本のスクリプトにしてある。
#
# 特に payloads= の行が要る理由:
#
#   本体（.vwpayload）は**群ごとに 1 本**で、PR の群は**ビルドが落ちても他を止めない**
#   （scripts/build-payloads.sh）。つまり「公開はされたが、この PR のプローブは入って
#   いない」が起こりうる。それを PR のチェックが見て赤くするための行である。
#
# 入力（既定のパス。--in / --dist で変えられる）:
#   build-probes/build-id.txt / shell-id.txt / summary.md / summary-line.txt
#   build-probes/groups.txt / build-id-source.txt   … 集約が書いたもの
#   dist/payload-status-mac.txt / payload-status-win.txt … 群ごとのビルド結果
#
# 使い方:
#   scripts/probe-release-notes.sh --commit <sha> [--in build-probes] [--dist dist] \
#       [--repo owner/repo] > notes.md
#
set -euo pipefail

IN="build-probes"
DIST="dist"
COMMIT=""
REPO="${GITHUB_REPOSITORY:-min-nano/vectorworks-developer-sdk-reference}"

usage() {
	awk 'NR > 1 { if ($0 !~ /^#/) exit; print }' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--in)
			IN="${2:?}"
			shift 2
			;;
		--dist)
			DIST="${2:?}"
			shift 2
			;;
		--commit)
			COMMIT="${2:?}"
			shift 2
			;;
		--repo)
			REPO="${2:?}"
			shift 2
			;;
		-h | --help)
			usage
			exit 0
			;;
		*)
			echo "probe-release-notes: 知らない引数: $1" >&2
			exit 2
			;;
	esac
done

[ -n "$COMMIT" ] || {
	echo "probe-release-notes: --commit が要ります" >&2
	exit 2
}

short="${COMMIT:0:7}"
now="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# status_of <群> <status ファイル> -> ok / failed / -（記録が無い）
status_of() {
	local group="$1" file="$2"
	[ -f "$file" ] || {
		echo "-"
		return 0
	}
	local v
	v="$(sed -n "s/^${group}=//p" "$file" | head -n 1)"
	echo "${v:--}"
}

# 群ごとの表と、機械が読む 1 行（payloads=）を作る。
groups_table=""
payloads_line=""
failed_any=""
if [ -f "$IN/groups.txt" ]; then
	while IFS='|' read -r group pr commit branch title; do
		[ -n "$group" ] || continue
		mac="$(status_of "$group" "$DIST/payload-status-mac.txt")"
		win="$(status_of "$group" "$DIST/payload-status-win.txt")"
		origin="main"
		if [ -n "$pr" ]; then
			origin="PR #$pr"
		elif [ -n "$branch" ]; then
			origin="$branch"
		fi
		mark_mac="✅"
		mark_win="✅"
		[ "$mac" = "ok" ] || mark_mac="❌"
		[ "$win" = "ok" ] || mark_win="❌"
		groups_table="${groups_table}| \`${group}\` | ${origin} | \`${commit}\` | ${mark_mac} | ${mark_win} | ${title} |
"
		# **どちらかで落ちていれば failed** とする（片方でも入っていなければ、その
		# プラットフォームの人は実機で走らせられない）。
		state="ok"
		if [ "$mac" != "ok" ] || [ "$win" != "ok" ]; then
			state="failed"
			failed_any="yes"
		fi
		payloads_line="${payloads_line}${payloads_line:+,}${group}:${state}"
	done <"$IN/groups.txt"
fi

# 隠しメタデータ。**自動アップデータが読むのはここ**で、build= が**本体**のビルドを
# 一意に指す ID、shell= が**殻**の ID。2 つあるのは「入れ替えに再起動が要るか」を決める
# ため——本体だけが新しければ Vectorworks を動かしたまま置き換えられる
# （plugin/src/UpdateParse.h の Evaluate）。値は集約が「main のコミット＋各 PR の head」
# から計算したもので、プラグインへ焼いた値と同じファイル由来だからずれようがない。
# **実行ごとに変わる値（run id）にはしない**——同じ顔ぶれで作り直したときに「新しい
# ビルドがあります」と誘ってしまうため。inputs= はその材料（更新が来ない・来すぎる
# ときに最初に見る行）。payloads= は群ごとのビルド結果（PR のチェックが読む）。
# HTML コメントなのでリリースのページには出ないが、API の body には入る。
echo "<!-- vw-probes"
echo "build=$(cat "$IN/build-id.txt")"
echo "shell=$(cat "$IN/shell-id.txt")"
echo "commit=${COMMIT}"
echo "built=${now}"
echo "probes=$(cat "$IN/summary-line.txt" 2>/dev/null || true)"
echo "payloads=${payloads_line}"
echo "inputs=$(tr '\n' ' ' <"$IN/build-id-source.txt")"
echo "-->"
echo
echo "実機確認プラグイン **VwSdkProbes**（${short} / ${now}）。"
echo
echo "## 入っているプローブ"
echo
cat "$IN/summary.md"
echo
echo "## 本体（プローブはこちらに入る）"
echo
echo "**本体は群ごとに 1 本**（main のプローブが 1 本、PR のプローブが PR ごとに 1 本）。"
echo "1 つの群がコンパイルできなくても、**他の群はそのまま配る**。入っていない群は"
echo "プラグインのピッカーに「※本体なし」と出る。"
echo
echo "| 群 | 出所 | コミット | macOS | Windows | PR タイトル |"
echo "| --- | --- | --- | --- | --- | --- |"
printf '%s' "$groups_table"
if [ -n "$failed_any" ]; then
	echo
	echo "> ❌ の群は、この回のビルドに**入っていない**（その群のプローブは実機で選べない）。"
	echo "> 原因はその PR のプローブのコンパイルエラーで、直して push すれば作り直される。"
fi
echo
echo "## 使い方"
echo
echo "**すでに入れてある場合は何もしなくてよい**——次に Vectorworks を起動したときに"
echo "プラグイン自身が「入れ替えますか？」と尋ねる（メニューの先頭項目からも確認できる）。"
echo
echo "**プローブだけの入れ替えなら Vectorworks の再起動は要らない**"
echo "（新しい本体を読み込むのは、次にメニューでプローブを選んだとき）。"
echo "殻（プラグイン本体）まで変わったときだけ再起動を尋ねる。"
echo "どちらの場合も落とすのは下の zip 1 つで、必要なファイルだけが置き換わる。"
echo
echo "初回だけ手で入れる:"
echo
echo "1. \`VwSdkProbes.*.zip\` を展開し、**中身をすべて**"
echo "   Vectorworks の Plug-ins フォルダへ置く（プラグイン・\`.vwpayload\`・カタログは隣同士）"
echo "2. Vectorworks を起動し直す（ワークスペース編集でメニューへ追加する）"
echo "3. メニュー「SDK 実機プローブ…」→ 一覧から選んで実行"
echo
echo "詳細は [plugin/README.md](https://github.com/${REPO}/blob/main/plugin/README.md)。"
