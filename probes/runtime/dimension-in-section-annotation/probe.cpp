//
//	probes/runtime/dimension-in-section-annotation/probe.cpp
//
//	[issue #129] 断面ビューポートの注釈空間へ直線寸法を置いたとき、データタグと同じ
//	座標の約束（横＝断面線の始点からの距離・縦＝Z）で狙った場所に出るかを実機で見る。
//
//	**2 巡目。** 1 巡目はビューポートが真っ白（×印の空枠）で何も描かれず、寸法が
//	見えるかどうか以前の状態だった。原因はプローブ側で、Findings「Viewports」に
//	書いてある断面ビューポートの作法を踏んでいなかったこと:
//
//	  - **レンダリングを隠線消去にする**（これが先に要る。シェイドのままでは
//	    2D コンポーネントの指定が入らない）
//	  - **クラスをすべて表示へ戻す**（ビューポートは既定でクラスが全部消えている）
//	  - 断面の表示の作法（1064 / 1035 / 1059）をすべて**更新より前**に設定する
//	  - **注釈へ後から足した図形のクラスも非表示のまま**なので、足した後にもう一度
//	    全クラスを表示へ戻して再更新する
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

	std::string SecDimReadAny(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		Sint8 s8 = 0;
		if (v.GetSint8(s8))
			return std::to_string(static_cast<long long>(s8)) + " [Sint8]";
		Uint8 u8 = 0;
		if (v.GetUint8(u8))
			return std::to_string(static_cast<long long>(u8)) + " [Uint8]";
		Sint16 s16 = 0;
		if (v.GetSint16(s16))
			return std::to_string(static_cast<long long>(s16)) + " [Sint16]";
		Sint32 s32 = 0;
		if (v.GetSint32(s32))
			return std::to_string(static_cast<long long>(s32)) + " [Sint32]";
		bool b = false;
		if (v.GetBoolean(b))
			return std::string(b ? "true" : "false") + " [Boolean]";
		Real64 r64 = 0.0;
		if (v.GetReal64(r64))
			return SecDimNum(r64) + " [Real64]";
		return "(既知のどの型でも読めない。型番号=" +
			   std::to_string(static_cast<long long>(v.GetType())) + ")";
	}

	// オブジェクト変数へ「いま入っている型と同じ型で」書く。ovViewportRenderType の
	// ように型がヘッダに書かれていないものを、決め打ちで外さないため。
	std::string SecDimWriteLikeCurrent(MCObjectHandle h, short selector, long long value)
	{
		TVariableBlock probeCurrent;
		if (!gSDK->GetObjectVariable(h, selector, probeCurrent))
			return "(読めないので書かなかった)";

		TVariableBlock next;
		Sint8 s8 = 0;
		Uint8 u8 = 0;
		Sint16 s16 = 0;
		Sint32 s32 = 0;
		bool b = false;
		if (probeCurrent.GetSint8(s8))
			next = static_cast<Sint8>(value);
		else if (probeCurrent.GetUint8(u8))
			next.SetUint8(static_cast<Uint8>(value));
		else if (probeCurrent.GetSint16(s16))
			next = static_cast<Sint16>(value);
		else if (probeCurrent.GetSint32(s32))
			next = static_cast<Sint32>(value);
		else if (probeCurrent.GetBoolean(b))
			next = static_cast<Boolean>(value != 0);
		else
			return "(型が分からないので書かなかった。型番号=" +
				   std::to_string(static_cast<long long>(probeCurrent.GetType())) + ")";

		const bool ok = gSDK->SetObjectVariable(h, selector, next) != 0;
		return std::string(ok ? "true" : "false") + " → 読み戻し " + SecDimReadAny(h, selector);
	}

	// ビューポートのクラスをすべて表示へ戻す（既定では全部消えている。
	// Findings「Viewports」）。ゲスト（参照ファイル由来）も含める。
	int SecDimShowAllClasses(MCObjectHandle viewport)
	{
		int shown = 0;
		gSDK->ForEachClass(true,
						   [&](MCObjectHandle cls)
						   {
							   if (cls == nil)
								   return;
							   if (gSDK->SetViewportClassVisibility(
									   viewport, gSDK->GetObjectInternalIndex(cls), 0))
								   ++shown;
						   });
		return shown;
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

VW_PROBE("dimension-in-section-annotation", "断面ビューポートの注釈へ寸法を置く（2 巡目）",
		 "1 巡目はビューポートが真っ白だった。Findings「Viewports」の作法（隠線消去・"
		 "全クラス表示・1064/1035/1059）を踏んでから、注釈へ寸法を 2 本置いて見え方を見る")
{
	const WorldCoord kSecDimWallEndX = 4000;
	const WorldCoord kSecDimWallThickness = 120;
	const WorldCoord kSecDimSectionY = -1500;

	probe.log("== 図面を作る ==");
	MCObjectHandle layer = gSDK->GetActiveLayer();
	if (layer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した");
		return;
	}

	MCObjectHandle wall =
		gSDK->CreateWall(WorldPt(0, 0), WorldPt(kSecDimWallEndX, 0), kSecDimWallThickness);
	if (wall == nil)
	{
		probe.fail("CreateWall が nil を返した");
		return;
	}
	probe.log("壁を 1 枚建てた: (0,0)-(4000,0) 厚さ 120mm");
	SecDimLogBounds(probe, "壁", wall);
	probe.log("  壁の高さ（既定）を ovWallHeight ではなく外接立体で測らない——高さは");
	probe.log("  断面の見え方で確かめる。");

	probe.log("");
	probe.log("== 断面ビューポートを作る ==");
	MCObjectHandle sheet = gSDK->CreateLayer("断面寸法調査シート", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("シートレイヤを作れなかった");
		return;
	}
	MCObjectHandle section =
		gSDK->CreateSectionViewport(WorldPt(-500, kSecDimSectionY), WorldPt(4500, kSecDimSectionY),
									WorldPt(2000, 1500), 0, -1000, 5000, sheet);
	if (section == nil)
	{
		probe.fail("CreateSectionViewport が nil を返した");
		return;
	}
	probe.log("断面線 (-500,-1500)-(4500,-1500) / 見る側 (2000,1500) / depth=0 高さ -1000〜5000");

	probe.log("");
	probe.log("== 描かれるようにする（1 巡目はここを飛ばして真っ白だった） ==");
	// **順番が効く。** レンダリングを先に隠線消去にしないと 1059 が入らない
	//（Findings「Viewports」）。いずれも更新より前。
	probe.log("  ovViewportRenderType(1001) ← renderFinalHiddenLine(6): " +
			  SecDimWriteLikeCurrent(section, ovViewportRenderType, renderFinalHiddenLine));
	probe.log("  ovSectionViewportDisplayObjectsBeyondCutPlane(1064) ← false: " +
			  SecDimWriteLikeCurrent(section, ovSectionViewportDisplayObjectsBeyondCutPlane, 0));
	probe.log("  ovViewportDisplayPlanar(1035) ← false: " +
			  SecDimWriteLikeCurrent(section, ovViewportDisplayPlanar, 0));
	probe.log("  ovViewportDisplay2DComponents(1059) ← true: " +
			  SecDimWriteLikeCurrent(section, ovViewportDisplay2DComponents, 1));
	probe.log("  表示レイヤ: SetViewportLayerVisibility(デザインレイヤ, 0) = " +
			  std::string(gSDK->SetViewportLayerVisibility(section, layer, 0) ? "true" : "false"));
	probe.log("  クラスを全部表示へ戻した件数 = " + std::to_string(SecDimShowAllClasses(section)));
	gSDK->UpdateViewport(section);
	SecDimLogBounds(probe, "更新後の断面ビューポート", section);
	probe.log("  ※ 1 巡目はここが 53.3mm 角の空枠（左-26.64 上26.64 右26.64 下-26.64）だった。");
	probe.log("  　 今回それより大きくなっていれば、中身が描かれたということ。");

	probe.log("");
	probe.log("== 注釈へ寸法を 2 本置く ==");
	probe.log("データタグで実機確認済みの約束（Findings「Data Tags」）に合わせて置く:");
	probe.log("  横 = 断面線の**始点**からの距離 / 縦 = Z");
	probe.log("断面線は (-500,-1500) から (4500,-1500) なので、壁の左端 x=0 は始点から");
	probe.log("500mm、右端 x=4000 は 4500mm の位置にあたる。");

	MCObjectHandle horiz =
		gSDK->CreateLinearDimension(WorldPt(500, 0), WorldPt(4500, 0), -500, 0, Vector2(0, 0), 0);
	if (horiz == nil)
		probe.fail("(A) 横方向の寸法を作れなかった");
	else
	{
		probe.log(
			"(A) 横 500→4500・Z=0・startOffset=-500 … AddViewportAnnotationObject = " +
			std::string(gSDK->AddViewportAnnotationObject(section, horiz) ? "true" : "false"));
		SecDimLogBounds(probe, "(A)", horiz);
	}

	MCObjectHandle vert =
		gSDK->CreateLinearDimension(WorldPt(500, 0), WorldPt(500, 2400), -500, 0, Vector2(0, 0), 0);
	if (vert == nil)
		probe.fail("(B) 縦方向の寸法を作れなかった");
	else
	{
		probe.log("(B) 横 500 固定・Z 0→2400・startOffset=-500 … AddViewportAnnotationObject = " +
				  std::string(gSDK->AddViewportAnnotationObject(section, vert) ? "true" : "false"));
		SecDimLogBounds(probe, "(B)", vert);
	}

	// **注釈へ後から足した図形のクラスは非表示のまま**（Findings「Viewports」）。
	// 足した後にもう一度全クラスを表示へ戻し、更新し直す。
	probe.log("");
	probe.log("注釈へ足した後、クラスを全部表示へ戻し直した件数 = " +
			  std::to_string(SecDimShowAllClasses(section)));
	gSDK->UpdateViewport(section);
	SecDimLogBounds(probe, "注釈を足して再更新した後の断面ビューポート", section);

	probe.log("");
	probe.log("== 目で見ないと分からないこと（利用者へ） ==");
	probe.log("シートレイヤ「断面寸法調査シート」を開いて、断面ビューポートを見てほしい。");
	probe.log("(1) 壁の断面が描かれているか（1 巡目は×印の空枠だった）。");
	probe.log("(2) 寸法が 2 本（横と縦）見えているか。");
	probe.log("(3) 横の寸法 (A) は、断面に写った壁の左端から右端までに掛かっているか");
	probe.log("    （＝「4000」と読めるか）。ずれているなら、どちらへ何 mm ぶんか。");
	probe.log("(4) 縦の寸法 (B) は、壁の足元（Z=0）から Z=2400 までに掛かっているか。");
}
