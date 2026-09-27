//
//	probes/runtime/dimension-in-section-annotation/probe.cpp
//
//	[issue #129] 断面ビューポートの注釈空間へ直線寸法を置いたとき、データタグと同じ
//	座標の約束（横＝断面線の終点からの距離・縦＝Z）で狙った場所に出るかを実機で見る。
//
//	dimension-and-standards のほうと分けてあるのは、こちらが「壁を建てて断面
//	ビューポートを作る」ぶん失敗しうる工程が多く、**片方が転んでももう片方の結果は
//	取りたい**ため（プローブは 1 ディレクトリ 1 本）。
//

#include "Probe.h"

#include <string>

namespace
{
	std::string SecDimNum(double value)
	{
		std::string s = std::to_string(value);
		const std::string::size_type dot = s.find('.');
		if (dot != std::string::npos && s.size() > dot + 3)
			s.erase(dot + 3);
		return s;
	}

	void SecDimLogBounds(vwprobe::Report& probe, const char* label, MCObjectHandle h)
	{
		WorldRect box;
		if (gSDK->GetObjectBounds(h, box))
			probe.log(std::string("  ") + label + " の外接矩形(mm) = 左" + SecDimNum(box.left) +
					  " 上" + SecDimNum(box.top) + " 右" + SecDimNum(box.right) + " 下" +
					  SecDimNum(box.bottom));
		else
			probe.log(std::string("  ") + label + ": GetObjectBounds が false");
	}
} // namespace

VW_PROBE("dimension-in-section-annotation", "断面ビューポートの注釈へ寸法を置く",
		 "壁を 1 枚建てて断面ビューポートを作り、その注釈へ直線寸法を 2 本（横＝断面線の"
		 "終点からの距離・縦＝Z）置いて、狙った位置に出るかを見る")
{
	// 図面の作りは実測値の読み方に直結するので、使った数値を先にすべて出しておく。
	const WorldCoord kSecDimWallStartX = 0;
	const WorldCoord kSecDimWallEndX = 4000; // 壁の長さ 4000mm
	const WorldCoord kSecDimWallThickness = 120;
	const WorldCoord kSecDimSectionY = -1500; // 断面線を引く Y（壁より手前）
	const WorldCoord kSecDimLookAtY = 1500; // 「見る側」を示す点の Y（壁の向こう）

	probe.log("== 図面を作る ==");
	MCObjectHandle layer = gSDK->GetActiveLayer();
	if (layer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した");
		return;
	}

	MCObjectHandle wall = gSDK->CreateWall(WorldPt(kSecDimWallStartX, 0),
										   WorldPt(kSecDimWallEndX, 0), kSecDimWallThickness);
	if (wall == nil)
	{
		probe.fail("CreateWall が nil を返した（壁を建てられないと断面に何も写らない）");
		return;
	}
	probe.log("壁を 1 枚建てた: (0,0)-(4000,0) 厚さ 120mm");
	SecDimLogBounds(probe, "壁", wall);

	probe.log("");
	probe.log("== 断面ビューポートを作る ==");
	MCObjectHandle sheet = gSDK->CreateLayer("断面寸法調査シート", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("シートレイヤを作れなかった（CreateLayer が nil）");
		return;
	}
	// 断面線は壁の手前を左から右へ。第 3 点が「見る側」（Findings「Viewports」）。
	// 範囲は Findings「Viewports」に従い depth=0（＝奥は無限）、高さは実寸＋余白。
	MCObjectHandle section =
		gSDK->CreateSectionViewport(WorldPt(-500, kSecDimSectionY), WorldPt(4500, kSecDimSectionY),
									WorldPt(2000, kSecDimLookAtY), 0, -1000, 5000, sheet);
	if (section == nil)
	{
		probe.fail("CreateSectionViewport が nil を返した");
		return;
	}
	probe.log("断面ビューポートを作った: 断面線 (-500,-1500)-(4500,-1500) / 見る側 (2000,1500)");
	probe.log("  depth=0 startHeight=-1000 endHeight=5000");
	gSDK->SetViewportLayerVisibility(section, layer, 0); // 0 = 表示
	gSDK->UpdateViewport(section);
	SecDimLogBounds(probe, "断面ビューポート", section);

	probe.log("");
	probe.log("== 注釈へ寸法を 2 本置く ==");
	probe.log("データタグで実機確認済みの約束（Findings「Data Tags」）に合わせて置く:");
	probe.log("  横 = 断面線の終点からの距離 / 縦 = Z");
	probe.log("断面線は (-500,-1500) から (4500,-1500) なので、壁の左端 x=0 は断面線の");
	probe.log("始点から 500mm の位置にあたる。壁の右端 x=4000 は 4500mm。");

	// (A) 横方向: 壁の左端から右端まで（Z = 0 の高さに寸法線を置く）。
	//     約束どおりなら、断面図の中で壁の幅ぴったりに掛かるはず。
	MCObjectHandle horiz =
		gSDK->CreateLinearDimension(WorldPt(500, 0), WorldPt(4500, 0), -500, 0, Vector2(0, 0), 0);
	if (horiz == nil)
	{
		probe.fail("(A) 横方向の寸法を作れなかった");
	}
	else
	{
		const bool added = gSDK->AddViewportAnnotationObject(section, horiz) != 0;
		probe.log("(A) 横 500→4500・Z=0・startOffset=-500 … AddViewportAnnotationObject = " +
				  std::string(added ? "true" : "false"));
		SecDimLogBounds(probe, "(A)", horiz);
	}

	// (B) 縦方向: Z = 0 から Z = 2400（壁の高さのあたり）まで。
	//     約束どおりなら、断面図の中で壁の足元から頭までに掛かるはず。
	MCObjectHandle vert =
		gSDK->CreateLinearDimension(WorldPt(500, 0), WorldPt(500, 2400), -500, 0, Vector2(0, 0), 0);
	if (vert == nil)
	{
		probe.fail("(B) 縦方向の寸法を作れなかった");
	}
	else
	{
		const bool added = gSDK->AddViewportAnnotationObject(section, vert) != 0;
		probe.log("(B) 横 500 固定・Z 0→2400・startOffset=-500 … AddViewportAnnotationObject = " +
				  std::string(added ? "true" : "false"));
		SecDimLogBounds(probe, "(B)", vert);
	}

	// 注釈へ後から足した図形のクラスは非表示のまま（Findings「Viewports」）。
	// ここでは新規の空図面なのでクラスは 1 つだけだが、作法として再更新しておく。
	gSDK->UpdateViewport(section);
	probe.log("");
	probe.log("UpdateViewport を掛け直した。");

	probe.log("");
	probe.log("== 目で見ないと分からないこと（利用者へ） ==");
	probe.log("シートレイヤ「断面寸法調査シート」を開いて、断面ビューポートを見てほしい。");
	probe.log("(1) 寸法が 2 本（横と縦）見えているか。");
	probe.log("(2) 横の寸法 (A) は、断面に写った壁の左端から右端までに掛かっているか");
	probe.log("    （＝「4000」と読めるか）。ずれているなら、どちらへ何 mm ぶんずれて");
	probe.log("    見えるか。");
	probe.log("(3) 縦の寸法 (B) は、壁の足元（Z=0）から Z=2400 までに掛かっているか。");
	probe.log("(4) 寸法の文字の大きさは、ビューポートの縮尺に関係なく用紙上で一定に");
	probe.log("    見えるか（＝図面を拡大縮小しても文字が同じ大きさか）。");
}
