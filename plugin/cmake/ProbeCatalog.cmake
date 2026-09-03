#
# ProbeCatalog.cmake — **カタログ**（殻が本体を読み込まずにプローブ一覧を出すための索引）
# を組み立てる、純粋な部分。
#
# なぜ切り出してあるか
# --------------------
# 本体（.vwpayload）は群ごとに分かれていて、殻は**ダイアログで選ばれた群のものだけ**を
# 読み込む（plugin/src/ProbeMenu.cpp）。つまり「どの本体に何が入っているか」を、本体を
# 読み込まずに知る手立てが要る——それがカタログ 1 枚（VwSdkProbes.probes.txt）である。
#
# ここは**SDK にもプラットフォームにも依存しない**ので、`cmake -P` でそのまま呼べる。
# plugin/tests/probe-catalog-test.cmake がそうやって確かめている（実ビルドを回さずに
# 「表示名を拾えているか」「区切り文字を持ち込ませないか」を押さえるため）。
#
# カタログの形（UTF-8・1 行 1 件・"|" 区切り。読み手は plugin/src/PayloadCatalog.h）:
#
#   version=1
#   build=<ビルド ID>          … 自動アップデートが新旧を比べる鍵
#   shell=<殻の ID>            … 再起動が要るかを決める鍵
#   group|<群>|<ファイル名>|<PR>|<commit>|<branch>|<PR タイトル>
#   probe|<群>|<slug>|<表示名>|<概要>
#
# 群の行にファイルが**在るか**は書かない。ビルドできなかった群はファイルが配られない
# だけでカタログには残り、殻が「隣に無い」ことを見て「本体が入っていません」と出す
# ——「なぜこの PR のプローブが無いのか」が実機で分かるように。
#

# vw_probe_meta(<ソース> <slug> <title 変数> <summary 変数>):
# VW_PROBE("slug", "title", "summary") の 2 番目と 3 番目をソースから読む。
# **表示名はコードにしか無い**（出所と違ってビルドでは決まらない）ので、カタログへ
# 載せるにはここで読むしかない。読めなければ slug で代用する——ローカルで書き殴った
# プローブでもピッカーに出せるようにするためで、止めるほどのことではない。
function(vw_probe_meta source slug out_title out_summary)
	set(title "${slug}")
	set(summary "")
	if(EXISTS "${source}")
		file(READ "${source}" content)
		if(content MATCHES
		   "VW_PROBE[ \t\r\n]*\\([ \t\r\n]*\"${slug}\"[ \t\r\n]*,[ \t\r\n]*\"([^\"]*)\"[ \t\r\n]*,[ \t\r\n]*\"([^\"]*)\""
		)
			set(title "${CMAKE_MATCH_1}")
			set(summary "${CMAKE_MATCH_2}")
		endif()
	endif()
	set(${out_title}
		"${title}"
		PARENT_SCOPE)
	set(${out_summary}
		"${summary}"
		PARENT_SCOPE)
endfunction()

# vw_catalog_field(<値> <出力変数>): カタログは "|" 区切りの 1 行なので、値に "|" と
# 改行を入れさせない。**PR タイトルもプローブの表示名も外から来る文字列**で、1 文字
# 混ざるだけで以降の列がずれる（ずれたまま実機のピッカーに出るのが最悪）。
function(vw_catalog_field value out)
	string(REPLACE "|" "/" value "${value}")
	string(REGEX REPLACE "[\r\n]" " " value "${value}")
	set(${out}
		"${value}"
		PARENT_SCOPE)
endfunction()

# vw_probe_catalog_lines(<群の一覧> <本体のファイル名の頭> <group 行> <probe 行> <要約> <件数>):
# 呼び出し側が持っている VW_PROBE_ORIGIN_<群> / VW_PROBE_ENTRIES_<群> /
# VW_PROBE_SOURCES_<群> から、カタログの本文（group 行と probe 行）を組み立てる。
function(vw_probe_catalog_lines groups basename out_groups out_probes out_summary out_total)
	set(group_lines "")
	set(probe_lines "")
	set(summary "")
	set(total 0)

	foreach(group IN LISTS groups)
		set(origin "${VW_PROBE_ORIGIN_${group}}")
		if(NOT origin MATCHES "^([^|]*)\\|([^|]*)\\|([^|]*)\\|(.*)$")
			message(FATAL_ERROR "VW_PROBE_ORIGIN_${group} の形式が違います"
								"（PR|commit|branch|title の 4 項目）: ${origin}")
		endif()
		set(g_pr "${CMAKE_MATCH_1}")
		set(g_commit "${CMAKE_MATCH_2}")
		set(g_branch "${CMAKE_MATCH_3}")
		set(g_title "${CMAKE_MATCH_4}")
		vw_catalog_field("${g_branch}" g_branch)
		vw_catalog_field("${g_title}" g_title)
		string(
			APPEND
			group_lines
			"group|${group}|${basename}-${group}.vwpayload|${g_pr}|${g_commit}|${g_branch}|${g_title}\n"
		)

		foreach(entry IN LISTS VW_PROBE_ENTRIES_${group})
			# 5 項目を正規表現で切り出す。list(GET) を使わないのは、**空の項目**（main 由来の
			# プローブは PR 番号が空）がリスト分割で落ちうるため（CMP0007）。
			if(NOT entry MATCHES "^([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|(.*)$")
				message(FATAL_ERROR "VW_PROBE_ENTRIES_${group} の形式が違います"
									"（slug|PR|commit|branch|title の 5 項目）: ${entry}")
			endif()
			set(e_slug "${CMAKE_MATCH_1}")
			set(e_pr "${CMAKE_MATCH_2}")

			# 表示名と概要はソースから読む。同じ slug のディレクトリに置かれたソースを探す
			# （集約はディレクトリ単位なので、ディレクトリ名＝slug）。
			set(probe_source "")
			foreach(src IN LISTS VW_PROBE_SOURCES_${group})
				get_filename_component(src_dir "${src}" DIRECTORY)
				get_filename_component(src_slug "${src_dir}" NAME)
				if(src_slug STREQUAL e_slug)
					set(probe_source "${src}")
					break()
				endif()
			endforeach()
			vw_probe_meta("${probe_source}" "${e_slug}" p_title p_summary)
			vw_catalog_field("${p_title}" p_title)
			vw_catalog_field("${p_summary}" p_summary)
			string(APPEND probe_lines "probe|${group}|${e_slug}|${p_title}|${p_summary}\n")

			math(EXPR total "${total} + 1")
			if(e_pr)
				string(APPEND summary "${e_slug}(#${e_pr}) ")
			else()
				string(APPEND summary "${e_slug} ")
			endif()
		endforeach()
	endforeach()

	string(STRIP "${summary}" summary)
	set(${out_groups}
		"${group_lines}"
		PARENT_SCOPE)
	set(${out_probes}
		"${probe_lines}"
		PARENT_SCOPE)
	set(${out_summary}
		"${summary}"
		PARENT_SCOPE)
	set(${out_total}
		"${total}"
		PARENT_SCOPE)
endfunction()
