#
# probe-catalog-test.cmake — カタログ組み立て（plugin/cmake/ProbeCatalog.cmake）の試験。
#
#   cmake -P plugin/tests/probe-catalog-test.cmake
#
# **SDK もコンパイラも要らない**（純粋な文字列処理なので、CI の lint で毎回走る）。
# ここが壊れると、実機のピッカーに「表示名が slug のまま」「列がずれた行」が出る
# ——どちらもビルドは通るので、確かめるならここしかない。
#

get_filename_component(here "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
include("${here}/../cmake/ProbeCatalog.cmake")

set(failures 0)

function(expect_equal what actual expected)
	if(NOT actual STREQUAL expected)
		message(WARNING "NG ${what}\n  実際  : [${actual}]\n  期待  : [${expected}]")
		math(EXPR failures "${failures} + 1")
		set(failures
			"${failures}"
			PARENT_SCOPE)
	endif()
endfunction()

function(expect_contains what actual needle)
	string(FIND "${actual}" "${needle}" at)
	if(at EQUAL -1)
		message(WARNING "NG ${what}\n  [${needle}] が入っていません:\n${actual}")
		math(EXPR failures "${failures} + 1")
		set(failures
			"${failures}"
			PARENT_SCOPE)
	endif()
endfunction()

# --- 材料（集約が書くマニフェストと同じ形）を作る ---------------------------------
set(work "${CMAKE_CURRENT_LIST_DIR}/../../build-probes-test")
file(REMOVE_RECURSE "${work}")
file(
	WRITE "${work}/sources/main/example/probe.cpp"
	"#include \"Probe.h\"\n\nVW_PROBE(\"example\", \"煙試験: レイヤを数える\",\n\t\t \"図面のレイヤ数を読む\")\n{\n}\n"
)
# 表示名に区切り文字（|）が入っているプローブ。**行を壊させない**ことを確かめる。
file(WRITE "${work}/sources/pr7/pipe/probe.cpp"
	 "VW_PROBE(\"pipe\", \"a|b の実測\", \"概要|付き\")\n{\n}\n")
# VW_PROBE を読み取れないもの（書き殴り）。slug で代用されること。
file(WRITE "${work}/sources/pr7/odd/probe.cpp" "// まだ書いていない\n")

set(VW_PROBE_ORIGIN_main "|abc1234|main|")
set(VW_PROBE_SOURCES_main "${work}/sources/main/example/probe.cpp")
set(VW_PROBE_ENTRIES_main "example||abc1234|main|")

set(VW_PROBE_ORIGIN_pr7 "7|def5678|feature/x|PR の題 | 付き")
set(VW_PROBE_SOURCES_pr7 "${work}/sources/pr7/pipe/probe.cpp" "${work}/sources/pr7/odd/probe.cpp")
set(VW_PROBE_ENTRIES_pr7 "pipe|7|def5678|feature/x|PR の題 | 付き"
						 "odd|7|def5678|feature/x|PR の題 | 付き")

vw_probe_catalog_lines("main;pr7" "VwSdkProbesPayload" group_lines probe_lines summary total)

# --- 群の行 -----------------------------------------------------------------------
expect_contains("main の群" "${group_lines}"
				"group|main|VwSdkProbesPayload-main.vwpayload||abc1234|main|")
# PR タイトルの "|" は落ちていること（列がずれないこと）。
expect_contains("PR の群" "${group_lines}"
				"group|pr7|VwSdkProbesPayload-pr7.vwpayload|7|def5678|feature/x|PR の題 / 付き")

# --- プローブの行 -------------------------------------------------------------------
expect_contains("表示名と概要を拾う" "${probe_lines}" "probe|main|example|煙試験: レイヤを数える|図面のレイヤ数を読む")
expect_contains("表示名の | は落とす" "${probe_lines}" "probe|pr7|pipe|a/b の実測|概要/付き")
expect_contains("読めなければ slug で代用" "${probe_lines}" "probe|pr7|odd|odd|")

# --- 要約と件数 ---------------------------------------------------------------------
expect_equal("件数" "${total}" "3")
expect_equal("要約" "${summary}" "example pipe(#7) odd(#7)")

file(REMOVE_RECURSE "${work}")

if(failures GREATER 0)
	message(FATAL_ERROR "probe-catalog-test: ${failures} 件失敗しました。")
endif()
message(STATUS "probe-catalog-test: すべて通りました。")
