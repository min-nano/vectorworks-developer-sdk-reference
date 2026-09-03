cmake_minimum_required(VERSION 3.20)

#
# VwSdkProbes — Vectorworks SDK の**実機確認用**プラグイン。
#
# このリポジトリは SDK リファレンス（ドキュメント）だが、Findings に「実機確認済み」と
# 書ける知見は実機（ローカルの VectorWorks）でしか取れない。そのための道具がこれで、
# 中身は**メニューコマンド 1 つ＋そこから選んで走らせるプローブ（調査コード）の束**。
# プローブは probes/runtime/<slug>/ に 1 ディレクトリ 1 件で置き、**複数の PR のものを
# 1 本のプラグインへ同居させる**（集め方は scripts/gather-probes.sh、全体像は
# plugin/README.md）。
#
# 成果物（実プラグイン vectorworks-plugin-import-ifc-homeskz と同じ形）:
#   * macOS   -> "VwSdkProbes.vwlibrary" バンドル（.vwr は Contents/Resources の中）
#   * Windows -> "VwSdkProbes.vlb"（DLL）＋ 隣に "VwSdkProbes.vwr"
#
# ビルド:
#   macOS:   cmake -S plugin -B build -DVW_SDK_DIR=/path/to/2026-NNA-eng-mac-SDK
#            cmake --build build --config Release
#   Windows: cmake -S plugin -B build -A x64 -DVW_SDK_DIR=C:/path/to/2026-NNA-eng-win-SDK
#            cmake --build build --config Release
#
# VW_SDK_DIR は "SDKLib" を**含む**フォルダを指す（環境変数でも可）。
#

# --- アーキテクチャ / 配備ターゲット（macOS のみ。project() より前に決める） --------
if(CMAKE_HOST_APPLE)
	if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
		set(CMAKE_OSX_ARCHITECTURES
			"arm64"
			CACHE STRING "Target architectures")
	endif()
	if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
		set(CMAKE_OSX_DEPLOYMENT_TARGET
			"14.0"
			CACHE STRING "Minimum macOS version")
	endif()
endif()

# macOS では SDK ヘッダを Objective-C++ としてコンパイルする（一部の mac ヘッダが
# CoreFoundation を #import するため）。Windows では素の C++。
project(VwSdkProbes LANGUAGES CXX)
if(APPLE)
	enable_language(OBJCXX)
endif()

if(NOT APPLE AND NOT WIN32)
	message(FATAL_ERROR "このプラグインは macOS か Windows でしかビルドできません "
						"（Vectorworks SDK が両者にしか無いため）。")
endif()

set(VW_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# --- Vectorworks SDK の場所 --------------------------------------------------
if(NOT DEFINED VW_SDK_DIR AND DEFINED ENV{VW_SDK_DIR})
	set(VW_SDK_DIR "$ENV{VW_SDK_DIR}")
endif()
if(NOT DEFINED VW_SDK_DIR)
	message(FATAL_ERROR "VW_SDK_DIR が未設定です。'SDKLib' を含むフォルダを指してください:\n"
						"  cmake -S plugin -B build -DVW_SDK_DIR=/path/to/2026-NNA-eng-<mac|win>-SDK")
endif()

set(VW_SDK_INCLUDE "${VW_SDK_DIR}/SDKLib/Include")
if(NOT EXISTS "${VW_SDK_INCLUDE}/VectorworksSDK.h")
	message(FATAL_ERROR "'${VW_SDK_INCLUDE}' に VectorworksSDK.h がありません "
						"（VW_SDK_DIR='${VW_SDK_DIR}'）。")
endif()

if(APPLE)
	set(VW_SDK_LIB "${VW_SDK_DIR}/SDKLib/LibMac/Release")
	if(NOT EXISTS "${VW_SDK_LIB}/libVWSDK.a")
		message(FATAL_ERROR "'${VW_SDK_LIB}' に libVWSDK.a がありません。")
	endif()
	set(VW_BUILDVWR "${VW_SDK_DIR}/SDKLib/ToolsMac/BuildVWR/BuildVWR")
else()
	# Windows は x64 のみ（SDK が x64 のライブラリしか持たず、Vectorworks 本体も x64）。
	if(CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM MATCHES "^([xX]64)$")
		message(WARNING "platform='${CMAKE_GENERATOR_PLATFORM}' ですが、Vectorworks SDK は "
						"x64 のライブラリしか持ちません。-A x64 で configure してください。")
	endif()
	set(VW_SDK_LIB "${VW_SDK_DIR}/SDKLib/LibWin/Release")
	if(NOT EXISTS "${VW_SDK_LIB}/VWSDK.lib")
		message(FATAL_ERROR "'${VW_SDK_LIB}' に VWSDK.lib がありません。")
	endif()
	set(VW_BUILDVWR "${VW_SDK_DIR}/SDKLib/ToolsWin/BuildVWR/buildvwr.exe")
	set(VW_BUILDVWR_7Z "${VW_SDK_DIR}/SDKLib/ToolsWin/BuildVWR/7z")
endif()

if(NOT EXISTS "${VW_BUILDVWR}")
	message(WARNING "BuildVWR が '${VW_BUILDVWR}' にありません。ビルドは通りますが、"
					"メニューコマンドの表示名が空になります。")
endif()

# --- ビルドスタンプ -----------------------------------------------------------
# どのビルドを実機へ入れたのかを、プラグイン側（ピッカーの見出し）と Info.plist の
# 両方から言えるようにする。CI が -D で渡す。
if(NOT DEFINED VW_BUILD_VERSION)
	set(VW_BUILD_VERSION
		"local"
		CACHE STRING "Short build identifier (git commit)")
endif()
if(NOT DEFINED VW_BUILD_BRANCH)
	set(VW_BUILD_BRANCH
		"local"
		CACHE STRING "Git branch this build came from")
endif()
if(NOT DEFINED VW_BUILD_TIME)
	string(TIMESTAMP VW_BUILD_TIME "%Y-%m-%dT%H:%M:%SZ" UTC)
endif()

# --- どのプローブを同居させるか ------------------------------------------------
# 正の入力は「マニフェスト」1 本:
#
#   VW_PROBE_SOURCES  … コンパイルするプローブのソース（絶対パス）
#   VW_PROBE_ENTRIES  … 出所の表。1 件 "slug|PR番号|短縮sha|ブランチ|PRタイトル"
#
# CI では scripts/gather-probes.sh が main と指定 PR からプローブを集めて、この 2 つを
# set する .cmake を書く（-DVW_PROBE_MANIFEST=... で読ませる）。マニフェストが無い
# ローカルビルドでは、作業ツリーの probes/runtime/ をそのまま拾う（出所は "local"）。
if(VW_PROBE_MANIFEST)
	if(NOT EXISTS "${VW_PROBE_MANIFEST}")
		message(FATAL_ERROR "VW_PROBE_MANIFEST が指すファイルがありません: ${VW_PROBE_MANIFEST}")
	endif()
	include("${VW_PROBE_MANIFEST}")
	message(STATUS "プローブのマニフェスト: ${VW_PROBE_MANIFEST}")
else()
	set(VW_PROBE_SOURCES "")
	set(VW_PROBE_ENTRIES "")
	file(
		GLOB probe_dirs
		LIST_DIRECTORIES true
		"${VW_REPO_ROOT}/probes/runtime/*")
	# glob の戻りは環境によって順序が揺れるので、明示的に並べて決定性を保つ。
	list(SORT probe_dirs)
	foreach(dir IN LISTS probe_dirs)
		if(IS_DIRECTORY "${dir}")
			get_filename_component(slug "${dir}" NAME)
			file(GLOB probe_sources "${dir}/*.cpp")
			list(SORT probe_sources)
			if(probe_sources)
				list(APPEND VW_PROBE_SOURCES ${probe_sources})
				list(APPEND VW_PROBE_ENTRIES "${slug}||local|local|")
			endif()
		endif()
	endforeach()
endif()

if(NOT VW_PROBE_SOURCES)
	# 1 件も無いビルドは作れてしまうが、実機では「プローブが入っていません」と出るだけで
	# 何の役にも立たない。取り違え（マニフェストの指定ミス等）を早く気付けるよう警告する。
	message(WARNING "プローブが 1 件もありません（probes/runtime/ が空、"
					"またはマニフェストが空）。")
endif()

# 出所表（ProbeProvenance.cpp）を生成する。C++ の文字列リテラルへ入るので、
# PR タイトルのような外から来る文字は必ずエスケープする。
set(VW_PROBE_PROVENANCE_ENTRIES "")
set(VW_PROBE_SUMMARY "")
foreach(entry IN LISTS VW_PROBE_ENTRIES)
	# 5 項目を正規表現で切り出す。list(GET) を使わないのは、**空の項目**（main 由来の
	# プローブは PR 番号が空）がリスト分割で落ちうるため（CMP0007）。
	if(NOT entry MATCHES "^([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|(.*)$")
		message(FATAL_ERROR "VW_PROBE_ENTRIES の形式が違います"
							"（slug|PR|commit|branch|title の 5 項目）: ${entry}")
	endif()
	set(e_slug "${CMAKE_MATCH_1}")
	set(e_pr "${CMAKE_MATCH_2}")
	set(e_commit "${CMAKE_MATCH_3}")
	set(e_branch "${CMAKE_MATCH_4}")
	set(e_title "${CMAKE_MATCH_5}")

	# C++ の文字列リテラルへ入れる前にエスケープする（PR タイトルは外から来る文字列）。
	set(escaped "")
	foreach(field IN ITEMS e_slug e_pr e_commit e_branch e_title)
		set(value "${${field}}")
		string(REPLACE "\\" "\\\\" value "${value}")
		string(REPLACE "\"" "\\\"" value "${value}")
		list(APPEND escaped "\"${value}\"")
	endforeach()
	string(REPLACE ";" ", " escaped "${escaped}")
	string(APPEND VW_PROBE_PROVENANCE_ENTRIES "\t\t{${escaped}},\n")
	if(e_pr)
		string(APPEND VW_PROBE_SUMMARY "${e_slug}(#${e_pr}) ")
	else()
		string(APPEND VW_PROBE_SUMMARY "${e_slug} ")
	endif()
endforeach()
string(STRIP "${VW_PROBE_SUMMARY}" VW_PROBE_SUMMARY)
message(STATUS "同居するプローブ: ${VW_PROBE_SUMMARY}")

set(generated_provenance "${CMAKE_CURRENT_BINARY_DIR}/ProbeProvenance.cpp")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/ProbeProvenance.cpp.in" "${generated_provenance}"
			   @ONLY)

# --- プラグイン本体 -----------------------------------------------------------
set(VW_PLUGIN_NAME "VwSdkProbes")
set(VW_BUNDLE_ID "io.github.min-nano.VwSdkProbes")

set(VW_PLUGIN_SOURCES
	src/ModuleMain.cpp
	src/Probe.cpp
	src/ProbeMenu.cpp
	"${generated_provenance}"
	${VW_PROBE_SOURCES})

# MODULE: 実行時に読み込まれるプラグイン（リンク対象ではない）。macOS ではバンドル、
# Windows では DLL（下で .vlb にリネームする）。
add_library(${VW_PLUGIN_NAME} MODULE ${VW_PLUGIN_SOURCES})

set_target_properties(
	${VW_PLUGIN_NAME}
	PROPERTIES CXX_STANDARD 20
			   CXX_STANDARD_REQUIRED ON
			   OUTPUT_NAME "${VW_PLUGIN_NAME}")

target_include_directories(
	${VW_PLUGIN_NAME}
	PRIVATE "${VW_SDK_INCLUDE}"
			"${VW_SDK_INCLUDE}/Kernel"
			"${VW_SDK_INCLUDE}/Interfaces"
			"${VW_SDK_INCLUDE}/VWMM"
			"${CMAKE_CURRENT_SOURCE_DIR}/src")

# RELEASE_BLD: SDK の Release ライブラリに合わせる。あとはビルドスタンプ。
target_compile_definitions(
	${VW_PLUGIN_NAME}
	PRIVATE RELEASE_BLD=1 "VW_BUILD_VERSION=\"${VW_BUILD_VERSION}\""
			"VW_BUILD_BRANCH=\"${VW_BUILD_BRANCH}\"" "VW_BUILD_TIME=\"${VW_BUILD_TIME}\"")

# SDK アンブレラヘッダのプリコンパイル（コンパイル時間のいちばん大きな削減）。
target_precompile_headers(${VW_PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/PluginPrefix.h")

if(APPLE)
	# Objective-C++ としてコンパイルする（project() のコメント参照）。**プローブの
	# ソースも同じ**——SDK ヘッダを include するため。
	set_source_files_properties(${VW_PLUGIN_SOURCES} TARGET_DIRECTORY ${VW_PLUGIN_NAME}
							    PROPERTIES LANGUAGE OBJCXX)

	target_include_directories(${VW_PLUGIN_NAME} PRIVATE "${VW_SDK_INCLUDE}/OnlyMac")

	set(VW_BUNDLE_NAME "${VW_PLUGIN_NAME}")
	set(plist "${CMAKE_CURRENT_BINARY_DIR}/${VW_PLUGIN_NAME}-Info.plist")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/Module-Info.plist.in" "${plist}" @ONLY)

	set_target_properties(
		${VW_PLUGIN_NAME}
		PROPERTIES OBJCXX_STANDARD 20
				   OBJCXX_STANDARD_REQUIRED ON
				   BUNDLE TRUE
				   BUNDLE_EXTENSION "vwlibrary"
				   MACOSX_BUNDLE_INFO_PLIST "${plist}")

	target_compile_options(${VW_PLUGIN_NAME} PRIVATE -stdlib=libc++ -Wno-deprecated-declarations)

	target_link_libraries(
		${VW_PLUGIN_NAME}
		PRIVATE "${VW_SDK_LIB}/libVWSDK.a"
				"-F${VW_SDK_LIB}"
				"-framework VWMM"
				"-framework Carbon"
				"-framework Cocoa")
else()
	target_include_directories(${VW_PLUGIN_NAME} PRIVATE "${VW_SDK_INCLUDE}/OnlyWin")

	set_target_properties(
		${VW_PLUGIN_NAME}
		PROPERTIES PREFIX ""
				   SUFFIX ".vlb"
				   MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" # /MD（SDK のライブラリに合わせる）
	)

	# _WINDOWS が SDK ヘッダを GS_WIN 構成に切り替える（MSVC は自分では定義しない）。
	target_compile_definitions(
		${VW_PLUGIN_NAME}
		PRIVATE _WINDOWS
				WIN32
				_USRDLL
				_WINDLL
				_WIN_EXTERNAL_
				UNICODE
				_UNICODE
				NDEBUG
				_CRT_SECURE_NO_WARNINGS)

	# /bigobj: テンプレートの多い SDK ヘッダが既定のセクション上限を超える。
	target_compile_options(${VW_PLUGIN_NAME} PRIVATE /EHsc /bigobj /wd4996)
	if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
		target_compile_options(${VW_PLUGIN_NAME} PRIVATE /MP)
	endif()

	target_link_libraries(${VW_PLUGIN_NAME} PRIVATE "${VW_SDK_LIB}/VWSDK.lib"
													"${VW_SDK_LIB}/VWMM.lib" USP10.lib Rpcrt4.lib)
endif()

# --- "<name>.vwr" のパッケージ --------------------------------------------------
# SDK の BuildVWR が resources/<name>.vwr を無圧縮 zip に固める。macOS ではバンドルの
# Contents/Resources、Windows では .vlb の隣。
set(vwr_src "${CMAKE_CURRENT_SOURCE_DIR}/resources/${VW_PLUGIN_NAME}.vwr")
if(EXISTS "${VW_BUILDVWR}")
	if(APPLE)
		add_custom_command(
			TARGET ${VW_PLUGIN_NAME}
			POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E make_directory
					"$<TARGET_BUNDLE_CONTENT_DIR:${VW_PLUGIN_NAME}>/Resources"
			COMMAND
				"${CMAKE_COMMAND}" -E env "VWR_INPUT=${vwr_src}"
				"VWR_OUTPUT=$<TARGET_BUNDLE_CONTENT_DIR:${VW_PLUGIN_NAME}>/Resources/${VW_PLUGIN_NAME}.vwr"
				"VWR_MODE=Release" "${VW_BUILDVWR}"
			VERBATIM
			COMMENT "Packaging ${VW_PLUGIN_NAME}.vwr resource into the bundle")
	else()
		add_custom_command(
			TARGET ${VW_PLUGIN_NAME}
			POST_BUILD
			COMMAND
				"${CMAKE_COMMAND}" -E env "VWR_INPUT=${vwr_src}"
				"VWR_OUTPUT=$<TARGET_FILE_DIR:${VW_PLUGIN_NAME}>/${VW_PLUGIN_NAME}.vwr"
				"VWR_MODE=Release" "VWR_7ZCMD=${VW_BUILDVWR_7Z}" "${VW_BUILDVWR}"
			VERBATIM
			COMMENT "Packaging ${VW_PLUGIN_NAME}.vwr resource next to the module")
	endif()
endif()

# Windows には Info.plist が無いので、ビルドの素性は隣のテキストへ書き出す
# （どのビルドを入れたかを、プラグインを起動せずに確かめられるように）。
if(NOT APPLE)
	set(stamp_file "${CMAKE_CURRENT_BINARY_DIR}/${VW_PLUGIN_NAME}.build-info.txt")
	file(
		WRITE "${stamp_file}"
		"branch=${VW_BUILD_BRANCH}\ncommit=${VW_BUILD_VERSION}\nbuilt=${VW_BUILD_TIME}\nprobes=${VW_PROBE_SUMMARY}\n"
	)
	add_custom_command(
		TARGET ${VW_PLUGIN_NAME}
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy "${stamp_file}"
				"$<TARGET_FILE_DIR:${VW_PLUGIN_NAME}>/${VW_PLUGIN_NAME}.build-info.txt"
		VERBATIM
		COMMENT "Writing the build stamp next to ${VW_PLUGIN_NAME}")
endif()
