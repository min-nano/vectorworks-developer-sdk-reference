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
#                              url=<zip の URL>
#                              title=<リリース名>
#                              probes=<入っているプローブ>
#                            取れなかったときは error=<理由>（終了コードは 0）。
#                            **理由は具体的に書く**——curl の終了コードか HTTP の
#                            コードと、それが何を意味するか（回数制限・DNS・証明書・
#                            プロキシ）。「ネットワークを確認してください」だけでは、
#                            網が生きているときに手掛かりが残らない。
#   do-install <url>         まるごと入れ替える（殻＋本体）。"ok" か error=<理由>。
#   do-install-payload <url> **本体だけ**入れ替える。"ok" か error=<理由>。
#
# **zip は 1 つしか無い**（殻＋本体一式）。本体だけの入れ替えも同じ zip を落として、中から
# 本体のファイルだけを取り出して置く——配る zip を 2 つに分けると、人が手で入れるときの
# 展開の手間が増えるだけで、得られるのは数百 KB の節約でしかない。
#
# **本体は 1 本ではなく「群ごとに 1 本」**（main のプローブが 1 本、PR のプローブが PR
# ごとに 1 本。VwSdkProbesPayload-<群>.vwpayload）。入れ替えでは
#   * zip に入っている .vwpayload を**全部**置き、
#   * 前の版にあって今度の版に無い .vwpayload を**消す**（消さないと、ビルドが落ちた
#     PR のプローブが古いまま残り、カタログに無いのにファイルだけある状態になる）、
#   * カタログ（VwSdkProbes.probes.txt）を置き換える
# の 3 つを行う。カタログが「いま入っているビルド」の真実（build= を読む）。
#
# 【なぜ 2 通りの入れ替えがあるか】このプラグインは「殻（Vectorworks が起動時に読み込む
# モジュール）」と「本体（殻が自分で読み込む .vwpayload）」に割れている。**本体だけなら
# Vectorworks を動かしたまま置き換えられ、次にメニューを開いたときから新しいプローブが
# 動く**（再起動が要らない）。殻まで変わったときだけ、まるごと入れ替えて再起動する。
# 判断はプラグイン側（plugin/src/UpdateParse.h の Evaluate）が 2 つの ID を見て行う。
#
# 【ID の出どころ】
#   公開側   … リリース本文の隠しメタデータ（<!-- vw-probes … -->）の build= と shell=
#   入っている側 … 本体はカタログ VwSdkProbes.probes.txt の build=
#                  殻はバンドルの Info.plist の VWShellId
#
# 【API が使えないときの逃げ道】q は最初に GitHub の API（api.github.com）を叩くが、
# **そこが駄目でも諦めない**。資産の URL はタグと名前から決まる
# （https://github.com/<repo>/releases/download/<タグ>/<名前>）ので、リリースの素性だけを
# 書いた小さなテキスト <名前>.release-info.txt を直接落として読む（中身はリリース本文の
# 隠しメタデータと同じ key=value で、scripts/probe-release-notes.sh --info が作る）。
# これで **API の呼び出し上限（未認証は 1 時間あたり 60 回）や、api.github.com だけが
# 塞がれている網でも入れ替えられる**。両方駄目なときだけ、両方の理由を並べて返す。
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
# 名乗り（GitHub は User-Agent を見る）と、**API を通さない取得先**。資産の URL は
# タグと名前から決まるので、api.github.com が使えないときはこちらから引く（下の mode_q）。
VW_UA="vw-probes-update"
VW_DL="https://github.com/${VW_REPO}/releases/download/${VW_TAG}"
VW_INFO="${VW_NAME}.release-info.txt"
# 本体のファイル名の頭（実体は "<この頭>-<群>.vwpayload"）と、殻が読む索引。
# plugin/src/PayloadHost.h の payload::FileNameFor / CatalogFileName と対。
VW_PAYLOAD_PREFIX="VwSdkProbesPayload-"
VW_CATALOG="VwSdkProbes.probes.txt"

# ---------------------------------------------------------------------------
# 取得の下請け。JSON は plutil で読む（macOS に最初から入っていて JSON を解せる）。
#
# **失敗したら理由を残す。** 「ネットワークを確認してください」しか出さないと、網は
# 生きているのに落ちたとき（API の回数制限・プロキシ・証明書・社内フィルタ）に、
# 利用者にも直す側にも手掛かりが無い。curl の終了コードと HTTP のコードは、
# **そのまま原因の名前**になるので捨てない。
# ---------------------------------------------------------------------------

# 直近の取得が失敗した理由（fetch_to が入れる。**成功したら空**）。
# **fetch_to を $(...) の中で呼ばないこと**——サブシェルではここへ書いた値が消える。
VW_FETCH_REASON=""

# rate_reset_hint <応答ヘッダのファイル> -> 「あと N 分で戻ります」（分からなければ空）
rate_reset_hint() {
	local reset now mins
	reset="$(sed -n 's/^[Xx]-[Rr]ate[Ll]imit-[Rr]eset:[[:space:]]*//p' "$1" | tr -d '\r' | tail -n 1)"
	case "$reset" in
		'' | *[!0-9]*) return 0 ;;
	esac
	now="$(date +%s)"
	mins=$(((reset - now + 59) / 60))
	[ "$mins" -gt 0 ] || return 0
	printf 'あと %s 分で戻ります' "$mins"
}

# fetch_reason <curl の終了コード> <HTTP コード> <curl のメッセージ> <応答ヘッダのファイル>
#   -> **1 行**の理由（そのままダイアログに出る。error= は 1 行しか読まれない）
fetch_reason() {
	local code="$1" http="$2" msg="$3" hdr="$4" why hint
	if [ "$code" -ne 0 ]; then
		case "$code" in
			5) why="プロキシに接続できません" ;;
			6) why="名前解決に失敗しました（DNS）" ;;
			7) why="接続できません（遮断・プロキシ設定の可能性）" ;;
			28) why="時間内に応答がありません（打ち切り）" ;;
			35 | 58 | 59 | 60 | 77 | 91) why="TLS の検証に失敗しました（証明書・SSL の設定）" ;;
			*) why="curl が失敗しました" ;;
		esac
		printf 'curl 終了コード %s（%s）%s' "$code" "$why" "${msg:+: ${msg}}"
		return 0
	fi
	case "$http" in
		401 | 403 | 429)
			if grep -qi '^x-ratelimit-remaining:[[:space:]]*0' "$hdr" 2>/dev/null; then
				hint="$(rate_reset_hint "$hdr")"
				printf 'HTTP %s: GitHub API の呼び出し上限に達しました（未認証は 1 時間あたり 60 回）%s' \
					"$http" "${hint:+。${hint}}"
			else
				printf 'HTTP %s: 拒否されました（プロキシや社内フィルタの可能性）' "$http"
			fi
			;;
		404) printf 'HTTP 404: 見つかりません（リリースか資産がまだありません）' ;;
		5??) printf 'HTTP %s: GitHub 側の一時的な障害' "$http" ;;
		000) printf '応答がありません' ;;
		*) printf 'HTTP %s' "$http" ;;
	esac
}

# fetch_to <URL> <出力先> <Accept> [秒] : 取れたら 0。取れなければ VW_FETCH_REASON に
# 理由を入れて非 0。**-f を付けない**——付けると HTTP のコードもろとも消えて、
# 「取れなかった」以上のことが言えなくなる。
fetch_to() {
	local url="$1" out="$2" accept="$3" maxtime="${4:-20}"
	local hdr err http code msg
	hdr="$(mktemp)"
	err="$(mktemp)"
	# --max-time で頭打ちにする。**この呼び出しの間 Vectorworks は止まる**（メニューから
	# 同期に叩かれる）ので、応答が返らないネットワークで待ち続けさせないために必須。
	if http="$(curl -sSL --max-time "$maxtime" --retry 2 -A "$VW_UA" -H "Accept: ${accept}" \
		-D "$hdr" -w '%{http_code}' "$url" -o "$out" 2>"$err")"; then
		code=0
	else
		code=$?
	fi
	case "$http" in
		'' | *[!0-9]*) http="000" ;;
	esac
	# curl のメッセージは**最後の行**を採る（--retry の警告が先に出るため）。
	msg="$(grep -v '^[[:space:]]*$' "$err" 2>/dev/null | tail -n 1 | tr -d '\r\n' || true)"
	if [ "$code" -eq 0 ] && [ "$http" -lt 400 ]; then
		VW_FETCH_REASON=""
		rm -f "$hdr" "$err"
		return 0
	fi
	VW_FETCH_REASON="$(fetch_reason "$code" "$http" "$msg" "$hdr")"
	rm -f "$hdr" "$err"
	return 1
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

# download <URL> <出力先>: zip を落とす。**理由の残し方は取得と同じ**（fetch_to）で、
# 時間の頭打ちだけ緩める（1 MB 強あるので 20 秒では足りない網がある）。
download() {
	fetch_to "$1" "$2" "*/*" 300
}

# installed_build -> 入っている**本体一式**のビルド ID（無ければ none）。
# 出どころはカタログ 1 枚——本体は群ごとに分かれているが、**どれも同じビルドから出る**
# ので、ビルド ID はカタログが持っていれば足りる（plugin/cmake/ProbeCatalog.cmake）。
installed_build() {
	local info="$VW_PLUGINS_DIR/$VW_CATALOG"
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

# install_payloads <展開先>: **本体一式**（群ごとの .vwpayload）とカタログを入れ替える。
# 新しい版に無い本体は消す——残すと、カタログに載っていないファイルだけが古いまま
# 居座る（実機で「消したはずの PR のプローブが動く」の元になる）。
install_payloads() {
	local work="$1" f name
	mkdir -p "$VW_PLUGINS_DIR"

	# まず新しいものを置く（1 本でも失敗したら、そこで止めて理由を返す）。
	for f in "$work/$VW_PAYLOAD_PREFIX"*.vwpayload; do
		[ -e "$f" ] || continue
		sanitize "$f"
		name="$(basename "$f")"
		if ! install_one "$f" "$VW_PLUGINS_DIR/$name"; then
			echo "本体（${name}）のコピーに失敗しました。"
			return 1
		fi
	done

	# **カタログは本体の後**。先に置くと、本体のコピーが途中で落ちたときに
	# 「カタログには載っているのにファイルが無い」状態が残る。
	if [ -e "$work/$VW_CATALOG" ] && ! install_one "$work/$VW_CATALOG" "$VW_PLUGINS_DIR/$VW_CATALOG"; then
		echo "カタログのコピーに失敗しました。"
		return 1
	fi

	# 今度の版に無い本体を消す。
	for f in "$VW_PLUGINS_DIR/$VW_PAYLOAD_PREFIX"*.vwpayload; do
		[ -e "$f" ] || continue
		name="$(basename "$f")"
		if [ ! -e "$work/$name" ]; then
			rm -rf "$f"
		fi
	done

	# 旧版（本体が 1 本だった頃）の置き土産も片付ける。
	rm -f "$VW_PLUGINS_DIR/VwSdkProbesPayload.vwpayload" \
		"$VW_PLUGINS_DIR/VwSdkProbesPayload.build-info.txt"
	return 0
}

# ---------------------------------------------------------------------------
# モード
# ---------------------------------------------------------------------------

mode_q() {
	local f body name url api_reason latest latest_shell probes
	f="$(mktemp)"
	body=""
	name=""
	url=""
	if fetch_to "${VW_API}/releases/tags/${VW_TAG}" "$f" "application/vnd.github+json"; then
		body="$(jval "$f" body)"
		name="$(jval "$f" name)"
		url="$(asset_url "$f" "$VW_NAME.vwlibrary.zip" || true)"
	else
		# **API が使えなくても更新はできる。** 資産の URL はタグと名前から決まる
		# （…/releases/download/<タグ>/<名前>）ので、リリースの素性だけを書いた小さな
		# テキスト（VW_INFO）を直接落として読む。API の呼び出し上限（未認証は 1 時間
		# あたり 60 回）や、api.github.com だけが塞がれている網でもここで通る。
		# **両方の理由を持ったまま**進む——どちらも駄目だったときに 1 行で言うため。
		api_reason="$VW_FETCH_REASON"
		if fetch_to "${VW_DL}/${VW_INFO}" "$f" "*/*"; then
			body="$(cat "$f")"
			name="$(meta "$body" title)"
			url="${VW_DL}/${VW_NAME}.vwlibrary.zip"
		else
			rm -f "$f"
			echo "error=リリース（${VW_TAG}）を取得できませんでした。GitHub API: ${api_reason}／直接取得: ${VW_FETCH_REASON}"
			return 0
		fi
	fi
	rm -f "$f"

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
		echo "error=ダウンロードに失敗しました（${VW_FETCH_REASON}）。"
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

	mkdir -p "$VW_PLUGINS_DIR"
	if ! install_one "$src" "$VW_PLUGINS_DIR/$VW_NAME.vwlibrary"; then
		rm -rf "$tmp" "$work"
		echo "error=インストール先へのコピーに失敗しました。"
		return 0
	fi
	# 本体一式とカタログも一緒に（殻と本体の版は揃っていなければならない）。
	local why
	if ! why="$(install_payloads "$work")"; then
		rm -rf "$tmp" "$work"
		echo "error=${why:-本体のコピーに失敗しました。}"
		return 0
	fi
	rm -rf "$tmp" "$work"
	echo "ok"
	return 0
}

# do-install-payload <url>: **本体一式だけ**入れ替える（Vectorworks を動かしたままでよい）。
# 落とすのは do-install と**同じ zip**で、中から本体のファイルだけを取り出して置く。
# 殻は読み込まれたまま触らないので、次にメニューを開いた時点で新しい本体が読まれる。
# **殻は置き換えるファイルを直接は読んでいない**（一時ディレクトリへ写した複製を読んで
# いる。plugin/src/PayloadHost.h）ので、いつ置き換えても衝突しない。
mode_do_install_payload() {
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
		echo "error=ダウンロードに失敗しました（${VW_FETCH_REASON}）。"
		return 0
	fi
	if ! unzip -q "$tmp/bundle.zip" -d "$work" >/dev/null 2>&1; then
		rm -rf "$tmp" "$work"
		echo "error=アーカイブの展開に失敗しました。"
		return 0
	fi
	if [ ! -e "$work/$VW_CATALOG" ]; then
		rm -rf "$tmp" "$work"
		echo "error=$VW_CATALOG が zip 内に見つかりません。"
		return 0
	fi

	local why
	if ! why="$(install_payloads "$work")"; then
		rm -rf "$tmp" "$work"
		echo "error=${why:-本体のコピーに失敗しました。}"
		return 0
	fi
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
