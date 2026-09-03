//
//	PluginPrefix.h
//
//	実機確認プラグイン（VwSdkProbes）の共通プリフィックスヘッダ。Vectorworks SDK の
//	アンブレラヘッダを引き込み、SDK のサンプルが前提にしている名前空間を開く。
//	SDK を使う実装ファイルは必ずこれを最初に include する。
//
//	（実プラグイン vectorworks-plugin-import-ifc-homeskz の src/PluginPrefix.h と同じ形。
//	  SDK の作法に属する部分なので、あちらと揃えてある。）
//

#pragma once

#ifdef _WINDOWS
#	include <Windows.h>
#endif

// Vectorworks SDK の本体。プラットフォーム（GS_MAC / GS_WIN）は __APPLE__ / _WINDOWS から
// 自動判別されるので、mac では _WINDOWS を定義してはならない。
#include "VectorworksSDK.h"

using namespace VWFC::Math;
using namespace VWFC::VWObjects;
using namespace VWFC::Tools;
using namespace VWFC::VWUI;
