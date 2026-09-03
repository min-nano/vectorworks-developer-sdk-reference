//
//	BuildConfig.h
//
//	プラグインの素性（ビルド時に決まるもの）を 1 か所へ集める。実プラグイン
//	（vectorworks-plugin-import-ifc-homeskz）と違い、ここには stable / dev の 2 系統は
//	無い——このプラグインは**実機確認のための 1 本**で、その中に「どの PR のプローブが
//	入っているか」が並ぶ（同居のさせ方は plugin/README.md）。
//

#pragma once

// .vwr リソースを引くときの識別子。**パッケージされる .vwr の基底名と一致させる**
// （resources/VwSdkProbes.vwr → "VwSdkProbes"）。ModuleMain.cpp の
// DefaultPluginVWRIdentifier() が返す値でもある。
#define PLUGIN_VWR_ID "VwSdkProbes"

// VCOM のユニバーサル名（拡張機能を一意に指す名前）。
#define PLUGIN_UNIVERSAL_NAME "CExtMenuVwSdkProbes"

// ビルドの短い識別子（CI では main の短縮 sha、ローカルでは "local"）。CMake が
// -DVW_BUILD_VERSION=... で渡す。ピッカーの見出しに出して「いま動かしているのが
// どのビルドか」を言えるようにする。
#ifndef VW_BUILD_VERSION
#	define VW_BUILD_VERSION "local"
#endif

// ビルド元のブランチ（CI では通常 "main"、ローカルでは "local"）。
#ifndef VW_BUILD_BRANCH
#	define VW_BUILD_BRANCH "local"
#endif

// ビルドした時刻（UTC の ISO 8601）。同じ main の sha でも「プローブの顔ぶれ」は
// ディスパッチのたびに変わりうるので、**どの実行のビルドか**を言えるように時刻も持つ。
#ifndef VW_BUILD_TIME
#	define VW_BUILD_TIME "unknown"
#endif

// ビルドを一意に指す ID（ローカルビルドでは "local"）。**自動アップデートが新旧を
// 比べるのはこれ**——コミットではない（同じ sha から、同居させる PR を変えて何度も
// ビルドされるため）。値は「main のコミット＋各 PR の head」から計算したもので、
// **同じ顔ぶれで作り直しても変わらない**（scripts/gather-probes.sh。plugin/src/Update.h）。
#ifndef VW_BUILD_ID
#	define VW_BUILD_ID "local"
#endif
