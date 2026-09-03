#!/usr/bin/env bash
#
# vw-probes-update.sh — 実機確認プラグイン（VwSdkProbes）を入れ替える（macOS）。
#
# プラグイン本体（plugin/src/Update.cpp）から**非対話**で呼ばれる裏方。ダイアログは
# すべてプラグイン側が Vectorworks のネイティブダイアログで出すので、こちらは
# 機械可読な行を標準出力へ出すだけで、自分では何も表示しない。
#
#   q                  いまの状況を key=value で出す:
#                        installed=<ビルド ID|none>
#                        latest=<ビルド ID>
#                        url=<zip の URL>
#                        title=<リリース名>
#                        probes=<入っているプローブ>
#                      取れなかったときは error=<理由>（終了コードは 0）。
#   do-install <url>   その zip を落として入れ替える。"ok" か error=<理由>。
#
# 【新旧はビルド ID で比べる】コミットではない——このプラグインは同じ main の sha から、
# 同居させる PR を変えて何度もビルドされるので、コミットで比べると取りこぼす。ビルド ID は
# 「main のコミット＋各 PR の head」から計算した値で、**同じ顔ぶれで作り直しても変わらない**
# （scripts/gather-probes.sh）。公開側はリリース本文の隠しメタデータ（<!-- vw-probes … -->）
# の build=、入っている側はバンドルの Info.plist の VWBuildId。
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

# installed_build -> 入っているビルドの ID（無ければ none）
installed_build() {
	local plist="$VW_PLUGINS_DIR/$VW_NAME.vwlibrary/Contents/Info.plist"
	if [ -f "$plist" ]; then
		/usr/libexec/PlistBuddy -c "Print :VWBuildId" "$plist" 2>/dev/null || echo "none"
	else
		echo "none"
	fi
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
	local body name url
	body="$(jval "$f" body)"
	name="$(jval "$f" name)"
	url="$(asset_url "$f" "$VW_NAME.vwlibrary.zip" || true)"
	rm -f "$f"

	local latest probes
	latest="$(meta "$body" build)"
	probes="$(meta "$body" probes)"

	if [ -z "$latest" ] || [ -z "$url" ]; then
		echo "error=リリースの情報が不完全です（ビルド ID か資産が見つかりません）。"
		return 0
	fi

	echo "installed=$(installed_build)"
	echo "latest=${latest}"
	echo "url=${url}"
	[ -n "$name" ] && echo "title=${name}"
	[ -n "$probes" ] && echo "probes=${probes}"
	return 0
}

# do-install <url>: 落として展開して、読み込まれているバンドルの隣へ入れ替える。
mode_do_install() {
	local url="$1"
	if [ -z "$url" ]; then
		echo "error=引数が不足しています。"
		return 0
	fi

	local tmp work
	tmp="$(mktemp -d)"
	work="$(mktemp -d)"
	if ! download "$url" "$tmp/$VW_NAME.vwlibrary.zip"; then
		rm -rf "$tmp" "$work"
		echo "error=ダウンロードに失敗しました。"
		return 0
	fi
	if ! unzip -q "$tmp/$VW_NAME.vwlibrary.zip" -d "$work" >/dev/null 2>&1; then
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

	# Gatekeeper: ダウンロードの隔離フラグを外し、アドホック署名をかけ直す
	# （Apple Silicon は署名の無い Mach-O を読み込まない。unzip すると署名が落ちる）。
	xattr -dr com.apple.quarantine "$src" 2>/dev/null || true
	codesign --force --deep --sign - "$src" >/dev/null 2>&1 || true

	mkdir -p "$VW_PLUGINS_DIR"
	local dst="$VW_PLUGINS_DIR/$VW_NAME.vwlibrary"
	rm -rf "$dst.new"
	if ! cp -R "$src" "$dst.new"; then
		rm -rf "$tmp" "$work" "$dst.new"
		echo "error=インストール先へのコピーに失敗しました。"
		return 0
	fi
	# 差し替えは**まるごと置いてから入れ替える**（途中で失敗しても、古いほうが残る）。
	rm -rf "$dst"
	mv "$dst.new" "$dst"
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
		*) echo "error=不明なモード: '${1:-}'（q / do-install）。" ;;
	esac
}

# 直接実行されたときだけ動かす（source されたときは動かさない——将来テストから
# 個々の関数を呼べるように。scripts/gather-probes.sh と同じ作法）。
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
	main "$@"
fi
