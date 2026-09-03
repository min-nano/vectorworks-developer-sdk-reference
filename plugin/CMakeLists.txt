cmake_minimum_required(VERSION 3.20)

#
# VwSdkProbes — Vectorworks SDK の**実機確認用**プラグイン。
#
# このリポジトリは SDK リファレンス（ドキュメント）だが、Findings に「実機確認済み」と
# 書ける知見は実機（ローカルの VectorWorks）でしか取れない。そのための道具がこれで、
# 中身は**メニューコマンド 1 つ＋そこから選んで走らせるプローブ（調査コード）の束**。
# プローブは probes/runtime/<slug>/ に 1 ディレクトリ 1 件で置き、**複数の PR のものを
# 1 つのプラグインへ同居させる**（集め方は scripts/gather-probes.sh、全体像は
# plugin/README.md）。同居は**群（main / PR ごと）に 1 本の本体**で行う——1 つの PR が
# コンパイルできなくても他の PR は配れるように。
#
# 成果物（実プラグイン vectorworks-plugin-import-ifc-homeskz と同じ形）:
#   * macOS   -> "VwSdkProbes.vwlibrary" バンドル（.vwr は Contents/Resources の中）
#   * Windows -> "VwSdkProbes.vlb"（DLL）＋ 隣に "VwSdkProbes.vwr"
#   * 両方    -> 隣に "VwSdkProbesPayload-<群>.vwpayload"（本体）と
#                "VwSdkProbes.probes.txt"（カタログ＝殻が読む索引）
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

# --- どのプローブを同居させるか（群ごと）--------------------------------------
# 正の入力は「マニフェスト」1 本。**群（group）ごとに**次の 3 つを受け取る:
#
#   VW_PROBE_GROUPS             … 群の一覧（"main" "pr12" …）
#   VW_PROBE_SOURCES_<group>    … その群でコンパイルするプローブのソース（絶対パス）
#   VW_PROBE_ENTRIES_<group>    … 出所の表。1 件 "slug|PR番号|短縮sha|ブランチ|PRタイトル"
#   VW_PROBE_ORIGIN_<group>     … 群そのものの出所 "PR番号|短縮sha|ブランチ|PRタイトル"
#
# **群 1 つにつき本体（.vwpayload）が 1 本できる。** main のプローブが 1 本、PR の
# プローブが PR ごとに 1 本。こう割るのは、
#   * **1 つの PR がコンパイルできなくても他の PR は配れる**ようにするため
#     （1 本にまとめていたときは、誰か 1 人が通らないだけで全員のビルドが落ちた）、
#   * **選ばれた群の本体だけを読み込めば済む**ようにするため（plugin/src/ProbeMenu.cpp）。
#
# CI では scripts/gather-probes.sh が main と指定 PR から集めてこの .cmake を書く
# （-DVW_PROBE_MANIFEST=... で読ませる）。マニフェストが無いローカルビルドでは、
# 作業ツリーの probes/runtime/ を群 "main" として拾う（出所は "local"）。
if(VW_PROBE_MANIFEST)
	if(NOT EXISTS "${VW_PROBE_MANIFEST}")
		message(FATAL_ERROR "VW_PROBE_MANIFEST が指すファイルがありません: ${VW_PROBE_MANIFEST}")
	endif()
	include("${VW_PROBE_MANIFEST}")
	message(STATUS "プローブのマニフェスト: ${VW_PROBE_MANIFEST}")
else()
	set(VW_PROBE_GROUPS "main")
	set(VW_PROBE_ORIGIN_main "|local|local|")
	set(VW_PROBE_SOURCES_main "")
	set(VW_PROBE_ENTRIES_main "")
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
				list(APPEND VW_PROBE_SOURCES_main ${probe_sources})
				list(APPEND VW_PROBE_ENTRIES_main "${slug}||local|local|")
			endif()
		endif()
	endforeach()
endif()

# --- ビルド ID -----------------------------------------------------------------
# **自動アップデートが新旧を比べる鍵**（plugin/src/Update.h）。コミットでは比べない
# ——同じ main の sha から、同居させる PR を変えて何度もビルドされるため。
#
# 値は**集約が決める**（scripts/gather-probes.sh が「main のコミット＋各 PR の head」から
# 計算し、マニフェストの VW_PROBE_BUILD_ID として渡す）。ビルドへ焼く値とリリースに書く
# 値が同じファイル由来になるので、**ずれようがない**のが要点。
#
# 明示的に -DVW_BUILD_ID= を渡せばそちらが勝つ（逃げ道）。マニフェストの無いローカル
# ビルドは "local" で、公開されている ID と一致しないので常に「新しいものがある」と
# 出る（それでよい——手元のビルドを最新扱いするほうが危ない）。
if(NOT DEFINED VW_BUILD_ID)
	if(VW_PROBE_BUILD_ID)
		set(VW_BUILD_ID "${VW_PROBE_BUILD_ID}")
	else()
		set(VW_BUILD_ID "local")
	endif()
endif()
message(STATUS "ビルド ID: ${VW_BUILD_ID}")

# --- 殻の ID -------------------------------------------------------------------
# **「再起動が要るか」を決める鍵**（plugin/src/UpdateParse.h の Evaluate）。本体
# （.vwpayload）だけが新しいなら置き換えて読み直すだけで済み、殻まで変わっていれば
# 再起動が要る。
#
# 値は**殻に入るものだけ**から計算する（plugin/src・plugin/resources・
# plugin/CMakeLists.txt・plugin/scripts のツリー。scripts/gather-probes.sh）。
# つまり**プローブが変わっても動かず、境界（plugin/src/PayloadAbi.h）を変えれば必ず動く**
# ——後者が肝で、版の食い違ったまま動かさないための歯止めになっている。
if(NOT DEFINED VW_SHELL_ID)
	if(VW_PROBE_SHELL_ID)
		set(VW_SHELL_ID "${VW_PROBE_SHELL_ID}")
	else()
		set(VW_SHELL_ID "local")
	endif()
endif()
message(STATUS "殻の ID: ${VW_SHELL_ID}")

# --- 名前 ---------------------------------------------------------------------
# 本体のファイル名は "<basename>-<group>.vwpayload"。**群ごとに 1 本**なので、名前に
# 群が入る（plugin/src/PayloadHost.h の payload::FileNameFor と対）。
set(VW_PAYLOAD_BASENAME "VwSdkProbesPayload")
# 殻が読む索引。**本体を読み込まずに一覧を出すため**のテキスト（plugin/src/PayloadCatalog.h）。
set(VW_CATALOG_FILENAME "VwSdkProbes.probes.txt")

# --- 群ごとの下ごしらえ（出所表・カタログの材料）--------------------------------
# 群 1 つにつき本体（.vwpayload）を 1 本作る。ここではその材料を作る:
#
#   * 出所表 ProbeProvenance-<group>.cpp … その群のプローブの PR / コミット / ブランチ
#     （本体へコンパイルされ、ピッカーと結果ダイアログの「出所」になる）
#   * カタログの行                       … **殻が本体を読み込まずに一覧を出すため**の索引
#
# カタログの組み立ては cmake/ProbeCatalog.cmake（SDK にもプラットフォームにも依存しない
# ので、plugin/tests/probe-catalog-test.cmake が `cmake -P` で確かめている）。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProbeCatalog.cmake")

vw_probe_catalog_lines("${VW_PROBE_GROUPS}" "${VW_PAYLOAD_BASENAME}" VW_CATALOG_GROUP_LINES
					   VW_CATALOG_PROBE_LINES VW_PROBE_SUMMARY VW_PROBE_TOTAL)

foreach(group IN LISTS VW_PROBE_GROUPS)
	# 出所表（この群のぶん）。C++ の文字列リテラルへ入るので、外から来る文字は
	# 必ずエスケープする。
	set(VW_PROBE_PROVENANCE_ENTRIES "")
	foreach(entry IN LISTS VW_PROBE_ENTRIES_${group})
		if(NOT entry MATCHES "^([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|(.*)$")
			message(FATAL_ERROR "VW_PROBE_ENTRIES_${group} の形式が違います"
								"（slug|PR|commit|branch|title の 5 項目）: ${entry}")
		endif()
		set(e_slug "${CMAKE_MATCH_1}")
		set(e_pr "${CMAKE_MATCH_2}")
		set(e_commit "${CMAKE_MATCH_3}")
		set(e_branch "${CMAKE_MATCH_4}")
		set(e_title "${CMAKE_MATCH_5}")

		set(escaped "")
		foreach(field IN ITEMS e_slug e_pr e_commit e_branch e_title)
			set(value "${${field}}")
			string(REPLACE "\\" "\\\\" value "${value}")
			string(REPLACE "\"" "\\\"" value "${value}")
			list(APPEND escaped "\"${value}\"")
		endforeach()
		string(REPLACE ";" ", " escaped "${escaped}")
		string(APPEND VW_PROBE_PROVENANCE_ENTRIES "\t\t{${escaped}},\n")
	endforeach()

	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/payload/ProbeProvenance.cpp.in"
				   "${CMAKE_CURRENT_BINARY_DIR}/ProbeProvenance-${group}.cpp" @ONLY)
endforeach()

message(STATUS "群: ${VW_PROBE_GROUPS}")
message(STATUS "同居するプローブ: ${VW_PROBE_SUMMARY}")

if(VW_PROBE_TOTAL EQUAL 0)
	# 1 件も無いビルドは作れてしまうが、実機では「プローブが入っていません」と出るだけで
	# 何の役にも立たない。取り違え（マニフェストの指定ミス等）を早く気付けるよう警告する。
	message(WARNING "プローブが 1 件もありません（probes/runtime/ が空、"
					"またはマニフェストが空）。")
endif()

# --- プラグイン本体 -----------------------------------------------------------
set(VW_PLUGIN_NAME "VwSdkProbes")
set(VW_BUNDLE_ID "io.github.min-nano.VwSdkProbes")

# **殻にプローブは入らない。** プローブとレジストリは本体（ペイロード）側にあり、
# 殻はそれを読み込んで呼ぶだけ（plugin/src/PayloadAbi.h）。ここが 2 つに割れているのが
# 「Vectorworks を再起動せずに入れ替えられる」の全部である。
set(VW_PLUGIN_SOURCES src/ModuleMain.cpp src/ProbeMenu.cpp src/Update.cpp src/PayloadHost.cpp)

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
			"VW_BUILD_BRANCH=\"${VW_BUILD_BRANCH}\"" "VW_BUILD_TIME=\"${VW_BUILD_TIME}\""
			"VW_BUILD_ID=\"${VW_BUILD_ID}\"" "VW_SHELL_ID=\"${VW_SHELL_ID}\"")

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

# --- 本体（ペイロード）: 群ごとに 1 本 --------------------------------------------
# **プローブはここに入る。** Vectorworks が読み込むのは殻だけで、こちらは殻が自分で
# dlopen / LoadLibrary する——だから**Vectorworks を再起動せずに入れ替えられる**
# （Findings「プラグインモジュールの読み込みと入れ替え」で実測済み。境界は
# plugin/src/PayloadAbi.h）。
#
# **群ごとに分けるのが肝。** main のプローブが 1 本、PR のプローブが PR ごとに 1 本。
#   * 1 つの PR がコンパイルできなくても、**他の PR は配れる**（ワークフローは本体を
#     1 本ずつビルドし、落ちた群だけを外して公開する。.github/workflows/probe-build.yml）。
#   * 殻は**選ばれた群の本体だけ**を読む（メニューを開くたびに全部読む必要が無い）。
#
# 置き場所は**殻の隣**（mac もバンドルの中ではない）。mac のバンドルは署名がリソースまで
# 封をするので、中のファイルを差し替えると署名が壊れる。隣に置けば、本体を何度置き換えても
# 殻の署名に触れない。拡張子 .vwpayload は Vectorworks にプラグインとして拾わせないため。

# vw_add_payload(<group>): その群の本体ターゲットを 1 つ作る。
# **殻とは独立にビルドできる**ようにしておく（add_dependencies で結ばない）——結ぶと、
# 1 つの群が通らないだけで殻まで作られなくなり、「他の PR は配れる」が成り立たない。
function(vw_add_payload group)
	set(target "${VW_PAYLOAD_BASENAME}-${group}")
	set(sources src/payload/PayloadMain.cpp src/payload/Probe.cpp
				"${CMAKE_CURRENT_BINARY_DIR}/ProbeProvenance-${group}.cpp"
				${VW_PROBE_SOURCES_${group}})

	add_library(${target} MODULE ${sources})

	set_target_properties(
		${target}
		PROPERTIES CXX_STANDARD 20
				   CXX_STANDARD_REQUIRED ON
				   BUNDLE FALSE
				   PREFIX ""
				   SUFFIX ".vwpayload"
				   OUTPUT_NAME "${target}")

	target_include_directories(
		${target}
		PRIVATE "${VW_SDK_INCLUDE}"
				"${VW_SDK_INCLUDE}/Kernel"
				"${VW_SDK_INCLUDE}/Interfaces"
				"${VW_SDK_INCLUDE}/VWMM"
				"${CMAKE_CURRENT_SOURCE_DIR}/src"
				"${CMAKE_CURRENT_SOURCE_DIR}/src/payload")

	# 本体は**自分の素性を自分で名乗る**（殻とは別に入れ替わるので、殻の値では言えない）。
	# 群の名前も焼く——実機のログで「どの本体が動いたか」を人が確かめられるように。
	target_compile_definitions(
		${target}
		PRIVATE RELEASE_BLD=1
				"VW_PAYLOAD_BUILD_ID=\"${VW_BUILD_ID}\""
				"VW_PAYLOAD_GROUP=\"${group}\""
				"VW_PAYLOAD_COMMIT=\"${VW_BUILD_VERSION}\""
				"VW_PAYLOAD_BRANCH=\"${VW_BUILD_BRANCH}\""
				"VW_PAYLOAD_BUILD_TIME=\"${VW_BUILD_TIME}\"")

	# SDK まわりの設定は殻と同じものを与える（本体も libVWSDK.a を自分でリンクし、
	# GS_InitializeVCOM を自分で呼ぶ）。
	if(APPLE)
		set_source_files_properties(${sources} TARGET_DIRECTORY ${target}
									PROPERTIES LANGUAGE OBJCXX)
		set_target_properties(${target} PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
		target_include_directories(${target} PRIVATE "${VW_SDK_INCLUDE}/OnlyMac")
		target_compile_options(${target} PRIVATE -stdlib=libc++ -Wno-deprecated-declarations)
		target_link_libraries(
			${target}
			PRIVATE "${VW_SDK_LIB}/libVWSDK.a"
					"-F${VW_SDK_LIB}"
					"-framework VWMM"
					"-framework Carbon"
					"-framework Cocoa")
	else()
		target_include_directories(${target} PRIVATE "${VW_SDK_INCLUDE}/OnlyWin")
		set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
		target_compile_definitions(
			${target}
			PRIVATE _WINDOWS
					WIN32
					_USRDLL
					_WINDLL
					_WIN_EXTERNAL_
					UNICODE
					_UNICODE
					NDEBUG
					_CRT_SECURE_NO_WARNINGS)
		target_compile_options(${target} PRIVATE /EHsc /bigobj /wd4996)
		if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
			target_compile_options(${target} PRIVATE /MP)
		endif()
		target_link_libraries(${target} PRIVATE "${VW_SDK_LIB}/VWSDK.lib" "${VW_SDK_LIB}/VWMM.lib"
												USP10.lib Rpcrt4.lib)
	endif()

	# SDK アンブレラヘッダのプリコンパイル（本体も SDK を丸ごと引くので効く）。
	target_precompile_headers(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/PluginPrefix.h")
endfunction()

foreach(group IN LISTS VW_PROBE_GROUPS)
	vw_add_payload("${group}")
endforeach()

# --- カタログ（殻が本体を読み込まずに一覧を出すための索引）-------------------------
# **これが無いと、メニューを開くたびに全部の本体を読み込むことになる。** 中身は
# 「どの群がどのファイルか」と「どの群にどのプローブが入っているか」だけ。読み手は
# plugin/src/PayloadCatalog.h（純粋な部分なので plugin/tests から SDK 抜きで確かめられる）。
#
# **列挙にファイルの存在は書かない。** 群の本体がビルドできなかったときは、その
# .vwpayload が配られないだけで、カタログには残る——殻は隣にファイルが無いことを見て
# 「（本体が入っていません）」と出す。こうすると「なぜこの PR のプローブが無いのか」が
# 実機で分かる（黙って消えるのが一番たちが悪い）。
set(catalog_file "${CMAKE_CURRENT_BINARY_DIR}/${VW_CATALOG_FILENAME}")
file(
	WRITE "${catalog_file}"
	"# VwSdkProbes probe catalog — 自動生成（plugin/CMakeLists.txt）。編集しない。\nversion=1\nbuild=${VW_BUILD_ID}\nshell=${VW_SHELL_ID}\nbranch=${VW_BUILD_BRANCH}\ncommit=${VW_BUILD_VERSION}\nbuilt=${VW_BUILD_TIME}\nprobes=${VW_PROBE_SUMMARY}\n${VW_CATALOG_GROUP_LINES}${VW_CATALOG_PROBE_LINES}"
)

# 出力先（本体とカタログが並ぶ場所＝殻の隣）。mac はバンドルの親、Windows は .vlb の隣
# （多構成ジェネレータでは Release/ の中）。
if(APPLE)
	set(VW_OUTPUT_DIR "$<TARGET_BUNDLE_DIR:${VW_PLUGIN_NAME}>/..")
else()
	set(VW_OUTPUT_DIR "$<TARGET_FILE_DIR:${VW_PLUGIN_NAME}>")
endif()

add_custom_command(
	TARGET ${VW_PLUGIN_NAME}
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy "${catalog_file}" "${VW_OUTPUT_DIR}/${VW_CATALOG_FILENAME}"
	VERBATIM
	COMMENT "Writing the probe catalog next to ${VW_PLUGIN_NAME}")

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

# --- 自動アップデータ（同梱スクリプト）を一緒に配る --------------------------------
# プラグイン本体（src/Update.cpp）がこれを非対話で叩いて、リリースの確認と入れ替えを行う。
# macOS はバンドルの Contents/Resources、Windows は .vlb の隣（SDK の作法と同じ置き方）。
if(APPLE)
	add_custom_command(
		TARGET ${VW_PLUGIN_NAME}
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory
				"$<TARGET_BUNDLE_CONTENT_DIR:${VW_PLUGIN_NAME}>/Resources"
		COMMAND
			"${CMAKE_COMMAND}" -E copy
			"${CMAKE_CURRENT_SOURCE_DIR}/scripts/vw-probes-update.sh"
			"$<TARGET_BUNDLE_CONTENT_DIR:${VW_PLUGIN_NAME}>/Resources/vw-probes-update.sh"
		VERBATIM
		COMMENT "Bundling vw-probes-update.sh into ${VW_PLUGIN_NAME}")
else()
	add_custom_command(
		TARGET ${VW_PLUGIN_NAME}
		POST_BUILD
		COMMAND
			"${CMAKE_COMMAND}" -E copy
			"${CMAKE_CURRENT_SOURCE_DIR}/scripts/vw-probes-update.ps1"
			"$<TARGET_FILE_DIR:${VW_PLUGIN_NAME}>/vw-probes-update.ps1"
		VERBATIM
		COMMENT "Bundling vw-probes-update.ps1 next to ${VW_PLUGIN_NAME}")
endif()

# Windows には Info.plist が無いので、ビルドの素性は隣のテキストへ書き出す
# （どのビルドを入れたかを、プラグインを起動せずに確かめられるように）。
if(NOT APPLE)
	set(stamp_file "${CMAKE_CURRENT_BINARY_DIR}/${VW_PLUGIN_NAME}.build-info.txt")
	# shell= は**自動アップデートが読む行**（入っている殻の ID。mac 側は Info.plist の
	# VWShellId）。plugin/scripts/vw-probes-update.ps1 の Get-InstalledShell と対で、
	# 「再起動が要るか」がここで決まる。いま入っている**本体**のほうは、隣の
	# カタログ（VwSdkProbes.probes.txt）の build= が真実（入れ替えで動くのは本体だけ）。
	file(
		WRITE "${stamp_file}"
		"shell=${VW_SHELL_ID}\nbuild=${VW_BUILD_ID}\nbranch=${VW_BUILD_BRANCH}\ncommit=${VW_BUILD_VERSION}\nbuilt=${VW_BUILD_TIME}\nprobes=${VW_PROBE_SUMMARY}\n"
	)
	add_custom_command(
		TARGET ${VW_PLUGIN_NAME}
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy "${stamp_file}"
				"$<TARGET_FILE_DIR:${VW_PLUGIN_NAME}>/${VW_PLUGIN_NAME}.build-info.txt"
		VERBATIM
		COMMENT "Writing the build stamp next to ${VW_PLUGIN_NAME}")
endif()
