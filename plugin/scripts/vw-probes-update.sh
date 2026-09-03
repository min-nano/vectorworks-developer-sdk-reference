#!/usr/bin/env bash
#
# vw-probes-update.sh — 実機確認プラグイン（VwSdkProbes）を入れ替える（macOS）。
#
# プラグイン本体（plugin/src/Update.cpp）から**非対話**で呼ばれる裏方。ダイアログは
# すべてプラグイン側が Vectorworks のネイティブダイアログで出すので、こちらは
# 機械可読な行を標準出力へ出すだけで、自分では何も表示しない。
#
#   q                        いまの状況を key=value で出す:
#                              installed=<本体のビルド ID|none>
#                              latest=<公開されている本体のビルド ID>
#                              installedShell=<入っている殻の ID|none>
#                              latestShell=<公開されている殻の ID>
#                              url=<まるごとの zip の URL>
#                              payloadUrl=<本体だけの zip の URL>
#                              title=<リリース名>
#                              probes=<入っているプローブ>
#                            取れなかったときは error=<理由>（終了コードは 0）。
#   do-install <url>         まるごと入れ替える（殻＋本体）。"ok" か error=<理由>。
#   do-install-payload <url> **本体だけ**入れ替える。"ok" か error=<理由>。
#
# 【なぜ 2 通りの入れ替えがあるか】このプラグインは「殻（Vectorworks が起動時に読み込む
# モジュール）」と「本体（殻が自分で読み込む .vwpayload）」に割れている。**本体だけなら
# Vectorworks を動かしたまま置き換えられ、次にメニューを開いたときから新しいプローブが
# 動く**（再起動が要らない）。殻まで変わったときだけ、まるごと入れ替えて再起動する。
# 判断はプラグイン側（plugin/src/UpdateParse.h の Evaluate）が 2 つの ID を見て行う。
#
# 【ID の出どころ】
#   公開側   … リリース本文の隠しメタデータ（<!-- vw-probes … -->）の build= と shell=
#   入っている側 … 本体は VwSdkProbesPayload.build-info.txt の build=
#                  殻はバンドルの Info.plist の VWShellId
#
# 手で叩いて確かめることもできる:
#   ./vw-probes-update.sh q
#
# 必要なもの: macOS に最初から入っているもの（curl / plutil / unzip / codesign / xattr）
# だけ。リポジトリは public なので認証も要らない。
#
# 環境変数で上書きできる:
#   VW_REPO         owner/repo           （既定は下）
#   VW_PLUGINS_DIR  Plug-Ins フォルダ    （既定は VW 2026 のユーザフォルダ。プラグインは
#                                          **実際に読み込まれたフォルダ**を必ず渡す）
#
set -euo pipefail

VW_REPO="${VW_REPO:-min-nano/vectorworks-developer-sdk-reference}"
VW_PLUGINS_DIR="${VW_PLUGINS_DIR:-$HOME/Library/Application Support/Vectorworks/2026/Plug-Ins}"
VW_API="https://api.github.com/repos/${VW_REPO}"
VW_TAG="${VW_TAG:-probes}"
VW_NAME="VwSdkProbes"
VW_PAYLOAD="VwSdkProbesPayload"
VW_PAYLOAD_ASSET="${VW_PAYLOAD}-mac.zip"

# ---------------------------------------------------------------------------
# GitHub REST の下請け。JSON は plutil で読む（macOS に最初から入っていて JSON を解せる）。
# ---------------------------------------------------------------------------

# api_get <サブパス> -> JSON を入れた一時ファイルのパス（失敗したら非 0）
api_get() {
	# --max-time で頭打ちにする。**起動時チェックが Vectorworks を止めないため**に必須。
	local f
	f="$(mktemp)"
	if curl -fsSL --max-time 20 --retry 2 -H "Accept: application/vnd.github+json" \
		"${VW_API}/$1" -o "$f"; then
		printf '%s' "$f"
	else
		rm -f "$f"
		return 1
	fi
}

# jval <json ファイル> <キーパス> -> 値（無ければ空）
jval() {
	plutil -extract "$2" raw -o - "$1" 2>/dev/null || true
}

# asset_url <json ファイル> <資産名> -> browser_download_url（無ければ非 0）
asset_url() {
	local f="$1" want="$2" j=0 nm
	while [ "$j" -lt 30 ]; do
		nm="$(jval "$f" "assets.${j}.name")"
		[ -n "$nm" ] || break
		if [ "$nm" = "$want" ]; then
			jval "$f" "assets.${j}.browser_download_url"
			return 0
		fi
		j=$((j + 1))
	done
	return 1
}

# meta <本文> <キー> -> リリース本文の隠しメタデータから "キー=値" の値を取り出す
meta() {
	printf '%s\n' "$1" | sed -n "s/^$2=//p" | head -n 1
}

download() {
	curl -fL --retry 3 --max-time 300 "$1" -o "$2"
}

# installed_build -> 入っている**本体**のビルド ID（無ければ none）
installed_build() {
	local info="$VW_PLUGINS_DIR/$VW_PAYLOAD.build-info.txt"
	if [ -f "$info" ]; then
		local v
		v="$(sed -n 's/^build=//p' "$info" | head -n 1)"
		if [ -n "$v" ]; then
			echo "$v"
			return 0
		fi
	fi
	echo "none"
}

# installed_shell -> 入っている**殻**の ID（無ければ none）
installed_shell() {
	local plist="$VW_PLUGINS_DIR/$VW_NAME.vwlibrary/Contents/Info.plist"
	if [ -f "$plist" ]; then
		/usr/libexec/PlistBuddy -c "Print :VWShellId" "$plist" 2>/dev/null || echo "none"
	else
		echo "none"
	fi
}

# ダウンロードした Mach-O を読み込める形にする（隔離フラグを外し、アドホック署名を
# かけ直す）。**Apple Silicon は署名の無い Mach-O を読み込まない**うえ、unzip すると
# 署名が落ちる。バンドルには --deep、単体のファイルにはそのまま。
sanitize() {
	local path="$1"
	xattr -dr com.apple.quarantine "$path" 2>/dev/null || true
	if [ -d "$path" ]; then
		codesign --force --deep --sign - "$path" >/dev/null 2>&1 || true
	else
		codesign --force --sign - "$path" >/dev/null 2>&1 || true
	fi
}

# install_one <src> <dst>: まるごと置いてから入れ替える（途中で失敗しても古いほうが残る）。
install_one() {
	local src="$1" dst="$2"
	rm -rf "$dst.new"
	cp -R "$src" "$dst.new" || return 1
	rm -rf "$dst"
	mv "$dst.new" "$dst"
}

# ---------------------------------------------------------------------------
# モード
# ---------------------------------------------------------------------------

mode_q() {
	local f
	if ! f="$(api_get "releases/tags/${VW_TAG}")"; then
		echo "error=リリース（${VW_TAG}）を取得できませんでした。ネットワークを確認してください。"
		return 0
	fi
	local body name url payload_url
	body="$(jval "$f" body)"
	name="$(jval "$f" name)"
	url="$(asset_url "$f" "$VW_NAME.vwlibrary.zip" || true)"
	payload_url="$(asset_url "$f" "$VW_PAYLOAD_ASSET" || true)"
	rm -f "$f"

	local latest latest_shell probes
	latest="$(meta "$body" build)"
	latest_shell="$(meta "$body" shell)"
	probes="$(meta "$body" probes)"

	if [ -z "$latest" ] || [ -z "$url" ]; then
		echo "error=リリースの情報が不完全です（ビルド ID か資産が見つかりません）。"
		return 0
	fi

	# **`x && echo` で書かない。** set -e のもとでは、条件が偽になった時点で関数ごと
	# 抜けてしまい、以降の行が出なくなる（値が空になりうる行が増えたので顕在化する）。
	echo "installed=$(installed_build)"
	echo "latest=${latest}"
	echo "installedShell=$(installed_shell)"
	if [ -n "$latest_shell" ]; then
		echo "latestShell=${latest_shell}"
	fi
	echo "url=${url}"
	if [ -n "$payload_url" ]; then
		echo "payloadUrl=${payload_url}"
	fi
	if [ -n "$name" ]; then
		echo "title=${name}"
	fi
	if [ -n "$probes" ]; then
		echo "probes=${probes}"
	fi
	return 0
}

# do-install <url>: まるごと（殻＋本体）落として、読み込まれているバンドルの隣へ入れ替える。
mode_do_install() {
	local url="$1"
	if [ -z "$url" ]; then
		echo "error=引数が不足しています。"
		return 0
	fi

	local tmp work
	tmp="$(mktemp -d)"
	work="$(mktemp -d)"

	if ! download "$url" "$tmp/bundle.zip"; then
		rm -rf "$tmp" "$work"
		echo "error=ダウンロードに失敗しました。"
		return 0
	fi
	if ! unzip -q "$tmp/bundle.zip" -d "$work" >/dev/null 2>&1; then
		rm -rf "$tmp" "$work"
		echo "error=アーカイブの展開に失敗しました。"
		return 0
	fi
	local src="$work/$VW_NAME.vwlibrary"
	if [ ! -d "$src" ]; then
		rm -rf "$tmp" "$work"
		echo "error=$VW_NAME.vwlibrary が zip 内に見つかりません。"
		return 0
	fi

	sanitize "$src"
	if [ -f "$work/$VW_PAYLOAD.vwpayload" ]; then
		sanitize "$work/$VW_PAYLOAD.vwpayload"
	fi

	mkdir -p "$VW_PLUGINS_DIR"
	if ! install_one "$src" "$VW_PLUGINS_DIR/$VW_NAME.vwlibrary"; then
		rm -rf "$tmp" "$work"
		echo "error=インストール先へのコピーに失敗しました。"
		return 0
	fi
	# 本体と控えも一緒に（殻と本体の版は揃っていなければならない）。
	local f
	for f in "$VW_PAYLOAD.vwpayload" "$VW_PAYLOAD.build-info.txt"; do
		if [ -e "$work/$f" ] && ! install_one "$work/$f" "$VW_PLUGINS_DIR/$f"; then
			rm -rf "$tmp" "$work"
			echo "error=本体（$f）のコピーに失敗しました。"
			return 0
		fi
	done
	rm -rf "$tmp" "$work"
	echo "ok"
	return 0
}

# do-install-payload <url>: **本体だけ**入れ替える（Vectorworks を動かしたままでよい）。
# 殻は読み込まれたまま触らないので、次にメニューを開いた時点で新しい本体が読まれる。
# **殻はこのファイルを直接は読んでいない**（一時ディレクトリへ写した複製を読んでいる。
# plugin/src/PayloadHost.h）ので、いつ置き換えても衝突しない。
mode_do_install_payload() {
	local url="$1"
	if [ -z "$url" ]; then
		echo "error=引数が不足しています。"
		return 0
	fi

	local tmp work
	tmp="$(mktemp -d)"
	work="$(mktemp -d)"

	if ! download "$url" "$tmp/payload.zip"; then
		rm -rf "$tmp" "$work"
		echo "error=ダウンロードに失敗しました。"
		return 0
	fi
	if ! unzip -q "$tmp/payload.zip" -d "$work" >/dev/null 2>&1; then
		rm -rf "$tmp" "$work"
		echo "error=アーカイブの展開に失敗しました。"
		return 0
	fi
	if [ ! -f "$work/$VW_PAYLOAD.vwpayload" ]; then
		rm -rf "$tmp" "$work"
		echo "error=$VW_PAYLOAD.vwpayload が zip 内に見つかりません。"
		return 0
	fi

	sanitize "$work/$VW_PAYLOAD.vwpayload"

	mkdir -p "$VW_PLUGINS_DIR"
	local f
	for f in "$VW_PAYLOAD.vwpayload" "$VW_PAYLOAD.build-info.txt"; do
		if [ -e "$work/$f" ] && ! install_one "$work/$f" "$VW_PLUGINS_DIR/$f"; then
			rm -rf "$tmp" "$work"
			echo "error=本体（$f）のコピーに失敗しました。"
			return 0
		fi
	done
	rm -rf "$tmp" "$work"
	echo "ok"
	return 0
}

main() {
	if ! command -v curl >/dev/null 2>&1; then
		echo "error=curl が見つかりません（macOS で実行してください）。"
		return 0
	fi
	case "${1:-}" in
		q) mode_q ;;
		do-install) mode_do_install "${2:-}" ;;
		do-install-payload) mode_do_install_payload "${2:-}" ;;
		*) echo "error=不明なモード: '${1:-}'（q / do-install / do-install-payload）。" ;;
	esac
}

# 直接実行されたときだけ動かす（source されたときは動かさない——将来テストから
# 個々の関数を呼べるように。scripts/gather-probes.sh と同じ作法）。
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
	main "$@"
fi
