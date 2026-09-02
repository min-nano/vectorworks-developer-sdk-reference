#!/usr/bin/env bash
#
# ci-debug-job.sh — `.github/workflows/ci-debug.yml`（CI debug）のランナー側本体。
#
# ワークフローは「チェックアウト → SDK 用意 → このスクリプトを 1 回実行」だけを行い、
# 実際の調査コマンドはすべてここに集約する。こうしている理由は 2 つ:
#
#   1. workflow_dispatch は「デフォルトブランチに存在するワークフロー」しか起動でき
#      ないため、ワークフロー本体を頻繁に触ると毎回 main へマージする必要が出る。
#      モードの追加・修正をこのスクリプト側に閉じ込めれば、作業ブランチに push する
#      だけで（dispatch の ref がそのブランチなので）すぐ試せる。
#   2. インライン YAML の run: と違い、独立したシェルスクリプトなので shellcheck に
#      そのままかけられる。
#
# 入力はすべて環境変数（ワークフローが inputs から詰める）:
#
#   MODE        sdk-grep | sdk-ls | compile | shell
#   PLATFORM    mac | windows | linux
#   ARGS        モードごとの引数（grep パターン / パス / ソースのパス）
#   SCRIPT      MODE=shell のときに実行する bash スクリプト本文
#   VW_SDK_DIR  トリミング済み SDK の場所（SDK を使うモードのみ）
#
# 出力は「ペイロードマーカー」で挟んだ 1 ブロックとして stdout に出す:
#
#   ===== BEGIN PAYLOAD (mode=... platform=...) =====
#   ...
#   ===== END PAYLOAD (exit=N lines_total=N truncated=yes|no) =====
#
# 呼び出し側（scripts/ci-debug.sh）はジョブログからこのマーカー間だけを抜き出すので、
# セットアップ手順のノイズを読まずに済む。生の全出力は debug-out/ に残し、ワーク
# フローがアーティファクトとしてアップロードする（人間用の保険。AI は GitHub MCP で
# アーティファクトを取得できないため、必要な情報は必ずログ側に出すこと）。
#
# 終了ステータスは調査コマンドのものをそのまま返す（＝run の conclusion になる）。
#
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

MODE="${MODE:-}"
PLATFORM="${PLATFORM:-mac}"
ARGS="${ARGS:-}"
SCRIPT="${SCRIPT:-}"
SDK="${VW_SDK_DIR:-}"
OUT_DIR="${OUT_DIR:-debug-out}"

# ペイロードに載せる最大行数。これを超えたぶんは切り捨て、END マーカーの
# truncated=yes で「全部は見ていない」ことを呼び出し側に明示する（AI が「該当なし」
# と誤読しないための最重要ポイント）。全文は debug-out/raw.txt に残る。
MAX_LINES="${PAYLOAD_MAX_LINES:-400}"

mkdir -p "$OUT_DIR"
RAW="$OUT_DIR/raw.txt"
PAYLOAD="$OUT_DIR/payload.txt"

# コンパイルのように「全文は長いが、欲しいのは診断行と末尾」という出力は、単純な
# head ではなくダイジェスト（診断行の抜粋＋末尾）にする。モード実装はサブシェルで
# 動く（後述）ので変数を書き戻せない。ここでモードから静的に決める。
case "$MODE" in
	compile | shell) DIGEST="log" ;;
	*) DIGEST="head" ;;
esac

# ヘッダ全文を読む sdk-ls だけは上限を上げる（SDK のヘッダは 1000 行級のものがある）。
if [ "$MODE" = "sdk-ls" ] && [ -z "${PAYLOAD_MAX_LINES:-}" ]; then
	MAX_LINES=1200
fi

# ---------------------------------------------------------------------------
# 小さなヘルパー
# ---------------------------------------------------------------------------

# die <message>: 使い方の誤り。モード実装はサブシェル内で走るので、この exit は
# スクリプト全体ではなくサブシェルだけを終わらせる。メッセージは（stderr ごと）
# RAW に入り、通常どおりペイロードとして出力される — つまり失敗しても呼び出し側は
# 必ずマーカー付きの理由を受け取れる。
die() {
	echo "ci-debug-job: error: $1" >&2
	exit 2
}

# need_sdk: SDK が用意されていることを確かめる（用意はワークフロー側の仕事）。
need_sdk() {
	[ -n "$SDK" ] || die "このモードには SDK が必要です（platform=linux では使えません）"
	[ -d "$SDK/SDKLib/Include" ] || die "SDK が見つかりません: $SDK/SDKLib/Include"
}

# need_args <説明>: ARGS 必須のモード用。
need_args() {
	[ -n "$ARGS" ] || die "このモードには args が必要です（$1）"
}

# ---------------------------------------------------------------------------
# モード実装。すべて stdout/stderr に出し、呼び出し元が RAW へリダイレクトする。
# ---------------------------------------------------------------------------

# sdk-grep: SDK ヘッダを拡張正規表現で検索する。「この API は SDK にあるか」を
# 確かめる設計調査用で、ローカル（リモートセッションのコンテナ）に SDK が無い以上
# CI 経由でしか答えられない問いに答えるための最重要モード。
mode_sdk_grep() {
	need_sdk
	need_args "検索する拡張正規表現"
	echo "# grep -rnIE '$ARGS' in SDKLib/Include (paths are relative to it)"
	echo
	local status=0
	( cd "$SDK/SDKLib/Include" && grep -rnIE -- "$ARGS" . ) | sed 's#^\./##' || status=$?
	# grep はヒット 0 件で 1 を返す。「見つからなかった」は調査結果であって失敗では
	# ないので run を赤くしない（本当のエラーは 2 以上なのでそれだけ伝播させる）。
	if [ "$status" -eq 1 ]; then
		echo "(no matches)"
		return 0
	fi
	return "$status"
}

# sdk-ls: ARGS がヘッダの実ファイルを指していればその全文、そうでなければパスの
# 部分一致で一覧を出す。grep で当たりを付けてから宣言の前後を読む、という流れ。
mode_sdk_ls() {
	need_sdk
	need_args "ヘッダのパス、またはパスの部分一致文字列"
	local root="$SDK/SDKLib/Include"
	if [ -f "$root/$ARGS" ]; then
		echo "# cat SDKLib/Include/$ARGS"
		echo
		cat "$root/$ARGS"
	else
		echo "# find SDKLib/Include -ipath '*$ARGS*' (paths are relative to it)"
		echo
		( cd "$root" && find . -type f -ipath "*$ARGS*" ) | sed 's#^\./##' | sort
	fi
}

# compile: リポジトリ内の調査スニペット 1 ファイルを SDK ヘッダに対して構文チェック
# する（-fsyntax-only。リンクはしない — このリポジトリの SDK はヘッダしか持たない）。
# 「この API はこの引数で呼べるか」を数十秒で確かめる用途。スニペットは probes/ に
# 置き、先頭で #include "VectorworksSDK.h" する（probes/README.md 参照）。
#
# インクルードパスと定義は、実プラグイン（vectorworks-plugin-import-ifc-homeskz）の
# CMake が SDK 依存ターゲットへ与えているものの写し。プラットフォーム判別
# （GS_MAC / GS_WIN）は VectorworksSDK.h が __APPLE__ / _WINDOWS から自動で行うので、
# mac では何も定義しない。mac 専用: Windows の cl.exe 環境構成は git-bash からは
# 複雑すぎて割に合わない（Windows 固有の疑問は実プラグイン側の mode=build で確かめる）。
mode_compile() {
	[ "$PLATFORM" = "mac" ] || die "compile は mac 専用です（--platform mac）"
	need_sdk
	need_args "コンパイルするソースのパス（リポジトリルートからの相対。例 probes/example.cpp）"
	[ -f "$ARGS" ] || die "ソースが見つかりません: $ARGS"
	echo "# clang++ -fsyntax-only $ARGS (against SDKLib/Include)"
	echo
	# plugin/src も -I に入れる。実機確認プラグイン（plugin/）のソースと、その API を
	# include する実機プローブ（probes/runtime/<slug>/probe.cpp）を、ビルドを回さずに
	# 構文チェックできるようにするため。
	clang++ -std=c++20 -stdlib=libc++ -x objective-c++ -fsyntax-only \
		-I "$SDK/SDKLib/Include" \
		-I "$SDK/SDKLib/Include/Kernel" \
		-I "$SDK/SDKLib/Include/Interfaces" \
		-I "$SDK/SDKLib/Include/VWMM" \
		-I "$SDK/SDKLib/Include/OnlyMac" \
		-I plugin/src \
		-DRELEASE_BLD=1 \
		-Wno-deprecated-declarations \
		"$ARGS" && echo "OK: syntax check passed"
}

# shell: 逃げ道。固定モードで表現できない一発調査を bash でそのまま流す。
# SCRIPT は環境変数で渡ってくる（YAML へ展開しないのでクォート事故が起きない）。
mode_shell() {
	[ -n "$SCRIPT" ] || die "mode=shell には script が必要です"
	local f="$OUT_DIR/script.sh"
	printf '%s\n' "$SCRIPT" >"$f"
	echo "# bash $f"
	echo
	bash "$f"
}

# ---------------------------------------------------------------------------
# ペイロード出力
# ---------------------------------------------------------------------------

# digest_log: コンパイルログ向けの抜粋。診断行（error/warning/FAILED …）を先に、
# その後 末尾の数十行を出す。エラーが末尾に来るとは限らないので、単純な tail では
# なく両方を出している。
digest_log() {
	local hits
	hits="$(grep -nE -- '(^|[^A-Za-z])([Ee]rror|ERROR|FAILED|fatal|undefined (reference|symbols)|error C[0-9]{4}|warning C[0-9]{4})' "$RAW" | head -n 300)"
	if [ -n "$hits" ]; then
		echo "--- diagnostics (max 300 lines, prefixed with the line number in raw.txt) ---"
		printf '%s\n' "$hits"
		echo
	fi
	echo "--- tail of the log (last 80 lines) ---"
	tail -n 80 "$RAW"
}

# emit_payload <exit-status>: マーカーで挟んだ 1 ブロックを stdout と payload.txt へ。
emit_payload() {
	local status="$1" total truncated="no"
	total="$(wc -l <"$RAW" | tr -d ' ')"

	{
		echo "===== BEGIN PAYLOAD (mode=$MODE platform=$PLATFORM) ====="
		if [ "$DIGEST" = "log" ]; then
			digest_log
		else
			head -n "$MAX_LINES" "$RAW"
			if [ "$total" -gt "$MAX_LINES" ]; then
				truncated="yes"
			fi
		fi
		echo "===== END PAYLOAD (exit=$status lines_total=$total truncated=$truncated) ====="
	} >"$PAYLOAD"

	cat "$PAYLOAD"
	emit_annotation
}

# emit_annotation: ペイロードを **チェックラン注釈** としても出す。
#
# なぜ二重に出すか: 呼び出し側がペイロードを取る経路は本来ジョブログだが、ログ API は
# 署名付きの Azure Blob Storage へ 302 で飛ぶ。組織の egress ポリシーがそのホストを
# 拒否している環境（Claude Code のリモートセッションなど）では、コンテナからログ本文を
# 取得できない。一方、注釈は
#
#   GET /repos/{owner}/{repo}/check-runs/{check_run_id}/annotations
#
# つまり api.github.com だけで読めるうえ、ログのノイズ（セットアップ手順・アーティファクト
# アップロード・ポストジョブ後始末）が混ざらない。ワークフローコマンドの仕様で改行は
# %0A へエスケープする必要がある（% と CR も同様）。
#
# **GitHub は注釈のメッセージを 4096 文字ちょうどで切る**（実測）。しかも切り方は
# 単語の途中でも構わない乱暴なもので、そのままだと END マーカーごと消えて「これで
# 全部だ」と誤読される。そこで自前でバイト予算に収め、切り詰めた旨の 1 行と END
# マーカー行を**必ず**収まる形で残す。全文はジョブログとアーティファクトにある。
#
# END 行には lines_total が入っているので、注釈側が切られていても「本当は何行あった
# のか」は読み手に伝わる。
emit_annotation() {
	local budget="${ANNOTATION_MAX_BYTES:-3800}" total kept body tail_line notice
	total="$(wc -l <"$PAYLOAD" | tr -d ' ')"
	tail_line="$(tail -n 1 "$PAYLOAD")"
	notice="... (annotation truncated by GitHub's 4096-char limit — the full payload is in the job log and the run artifact)"

	# 予算から「切り詰め通知＋END 行」ぶんを引いた範囲まで、行単位で詰める。
	# 文字数ではなくバイト数で数えるため LC_ALL=C（日本語のエラーメッセージ対策）。
	body="$(LC_ALL=C awk -v limit="$((budget - ${#notice} - ${#tail_line} - 4))" '
		{
			len += length($0) + 1
			if (len > limit) { exit }
			print
		}' "$PAYLOAD")"

	kept="$(printf '%s\n' "$body" | wc -l | tr -d ' ')"
	if [ "$kept" -lt "$total" ]; then
		body="$(printf '%s\n%s\n%s' "$body" "$notice" "$tail_line")"
	fi

	body="$(printf '%s\n' "$body" |
		sed -e 's/%/%25/g' -e 's/\r/%0D/g' |
		awk '{printf "%s%%0A", $0}')"
	echo "::notice title=ci-debug payload::${body}"
}

# ---------------------------------------------------------------------------
# 本体
# ---------------------------------------------------------------------------

# モード実装はサブシェルで動かす。die の exit がここで止まるので、使い方の誤りでも
# 必ず emit_payload まで到達する（＝呼び出し側は理由をマーカー付きで受け取れる）。
(
	case "$MODE" in
		sdk-grep) mode_sdk_grep ;;
		sdk-ls) mode_sdk_ls ;;
		compile) mode_compile ;;
		shell) mode_shell ;;
		*) die "未知の mode: '$MODE'（sdk-grep / sdk-ls / compile / shell）" ;;
	esac
) >"$RAW" 2>&1
STATUS=$?

emit_payload "$STATUS"
exit "$STATUS"
