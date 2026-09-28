//
//	probes/runtime/dim-text-size-and-scale/probe.cpp
//
//	[issue #143] **寸法の文字の図面上の大きさ（`ovDimFontSize`）は何で決まるか。**
//	利用側（vectorworks-plugin-import-ifc-homeskz #147）で、伏図（平面ビューポート）の
//	注釈では寸法の値が出るのに、軸組図（断面ビューポート）の注釈では**寸法線は出るのに
//	値が出ない**。作った直後に読んだ値は次のとおりで、違うのは `ovDimFontSize` だけ:
//
//	  伏図   作ったときのアクティブレイヤ＝デザインレイヤ(1/50) → ovDimFontSize 105.833
//	  軸組図 作ったときのアクティブレイヤ＝シートレイヤ(1:1)    → ovDimFontSize   2.11667
//
//	どちらも `ovDimTextSizeInPoints` は 6、規格も同じ。**2.1167mm の文字は 1/50 の図の
//	中では用紙上 0.04mm** になるので、見えないのではないか——というのが利用側の疑い。
//
//	Findings「Dimensions」には「**注釈空間は用紙 1:1 なので、文字は紙のポイントのまま
//	出る**」と書いてあるが、その実測（#129）は
//
//	  ・デザインレイヤ(1:100) で作った寸法 → 211.666
//	  ・**シートレイヤがアクティブなときに作って**注釈へ移した寸法 → 2.116
//
//	という 2 本を比べたもので、**「注釈へ移したから 2.116 になった」のか「シートレイヤ
//	(1:1) で作ったから 2.116 だった」のかを分けていない**。しかも当時の平面ビューポートは
//	`CreateViewport` の既定の縮尺（＝おそらく 1:1）のままだったので、**縮尺の違う
//	ビューポートでは話が変わりうる**。ここを分けて測り直す。
//
//	**この 1 本で確かめること:**
//
//	  1. `ovDimFontSize` は**作るときのアクティブレイヤの縮尺**で決まるか。
//	     （縮尺 1 のレイヤで作った寸法と、1/50 のレイヤで作った寸法を比べる）
//	  2. **注釈へ移すと `ovDimFontSize` / `ovDimTextSizeInPoints` は変わるか。**
//	     平面ビューポートと断面ビューポートの両方で、同じ 2 種類の寸法を移して読む。
//	  3. **読み戻す値はビューポートの縮尺に追随するか**（＝注釈空間の縮尺は
//	     ビューポートの縮尺か、それとも 1:1 か）。移した後にビューポートの縮尺を
//	     1/50 → 1/100 へ変えて読み直す。
//	  4. **後から直せるか。** 移した後に `ovDimTextSizeInPoints` / `ovDimFontSize` を
//	     書けるか、書いたら他方が追随するか、`ResetObject` や規格の当て直しで
//	     元に戻らないか。
//	  5. **既にある寸法は、レイヤの縮尺を変えたときに追随するか。**
//
//	どれも数値で答えが出る。最後に 1 つだけ目視が要る（断面注釈に、大きさの違う寸法を
//	2 本並べて置いてあるので、**どちらの値が読めるか**）。
//

#include "Probe.h"

#include <string>

namespace
{
	std::string DimTxtNum(double value)
	{
		std::string s = std::to_string(value);
		const std::string::size_type dot = s.find('.');
		if (dot != std::string::npos && s.size() > dot + 5)
			s.erase(dot + 5);
		return s;
	}

	std::string DimTxtInt(long long value)
	{
		return std::to_string(value);
	}

	// 型を決め打ちしない読み口（Findings「Dimensions」: ヘッダのコメントの型と実体が
	// 一致しないものがある）。
	std::string DimTxtReadAny(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";

		Real64 r64 = 0.0;
		if (v.GetReal64(r64))
			return DimTxtNum(r64) + " [Real64]";
		Sint8 s8 = 0;
		if (v.GetSint8(s8))
			return DimTxtInt(s8) + " [Sint8]";
		Uint8 u8 = 0;
		if (v.GetUint8(u8))
			return DimTxtInt(u8) + " [Uint8]";
		Sint16 s16 = 0;
		if (v.GetSint16(s16))
			return DimTxtInt(s16) + " [Sint16]";
		Sint32 s32 = 0;
		if (v.GetSint32(s32))
			return DimTxtInt(s32) + " [Sint32]";
		bool b = false;
		if (v.GetBoolean(b))
			return std::string(b ? "true" : "false") + " [Boolean]";
		TXString str;
		if (v.GetTXString(str))
			return "\"" + std::string(static_cast<const char*>(str)) + "\" [TXString]";
		return "(既知のどの型でも読めない。型番号=" +
			   DimTxtInt(static_cast<long long>(v.GetType())) + ")";
	}

	// Real64 として読めたときだけ数値を返す（比較・計算に使う）。読めなければ false。
	bool DimTxtReadReal(MCObjectHandle h, short selector, double& out)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return false;
		Real64 r64 = 0.0;
		if (!v.GetReal64(r64))
			return false;
		out = r64;
		return true;
	}

	std::string DimTxtWriteReal(MCObjectHandle h, short selector, double value)
	{
		TVariableBlock v;
		v = static_cast<Real64>(value);
		const bool ok = gSDK->SetObjectVariable(h, selector, v) != 0;
		return std::string(ok ? "true" : "false");
	}

	std::string DimTxtWriteBool(MCObjectHandle h, short selector, bool value)
	{
		TVariableBlock v;
		v = static_cast<Boolean>(value);
		const bool ok = gSDK->SetObjectVariable(h, selector, v) != 0;
		return std::string(ok ? "true" : "false");
	}

	double DimTxtLayerScale(MCObjectHandle layer)
	{
		double_gs scale = 0.0;
		if (layer == nil)
			return 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return static_cast<double>(scale);
	}

	// **この調査の主計器。** 1 本の寸法について、紙の上の大きさ・図面上の大きさ・
	// その比（＝その寸法が前提にしている縮尺）を 1 行で出す。
	void DimTxtLogDim(vwprobe::Report& probe, const std::string& label, MCObjectHandle h)
	{
		if (h == nil)
		{
			probe.log("  " + label + ": (nil)");
			return;
		}
		double fontSize = 0.0;
		double points = 0.0;
		const bool haveFont = DimTxtReadReal(h, ovDimFontSize, fontSize);
		const bool havePoints = DimTxtReadReal(h, ovDimTextSizeInPoints, points);

		std::string line = "  " + label + ": ovDimFontSize=" + DimTxtReadAny(h, ovDimFontSize) +
						   " / ovDimTextSizeInPoints=" + DimTxtReadAny(h, ovDimTextSizeInPoints);
		// 6pt = 6 * 25.4 / 72 = 2.11667mm。fontSize をこの紙寸法で割ると、
		// その寸法が「いくつの縮尺の中に居るつもりか」が出る。
		if (haveFont && havePoints && points != 0.0)
		{
			const double paperMM = points * 25.4 / 72.0;
			if (paperMM != 0.0)
				line += " → fontSize ÷ (points の紙 mm) = " + DimTxtNum(fontSize / paperMM) +
						"（＝この寸法が前提にしている縮尺）";
		}
		probe.log(line);
		probe.log("     ovDimShowValue=" + DimTxtReadAny(h, ovDimShowValue) +
				  " / ovDimStandardName=" + DimTxtReadAny(h, ovDimStandardName));
	}

	MCObjectHandle DimTxtMakeDim(WorldPt p1, WorldPt p2, WorldCoord startOffset)
	{
		MCObjectHandle h = gSDK->CreateLinearDimension(p1, p2, startOffset, 0, Vector2(0, 0), 0);
		if (h != nil)
		{
			// 値を出す設定は明示しておく（利用側もそうしている）。
			TVariableBlock v;
			v = static_cast<Boolean>(true);
			gSDK->SetObjectVariable(h, ovDimShowValue, v);
		}
		return h;
	}

	// ビューポートのクラスをすべて表示へ戻す（既定では全部消えている。
	// Findings「Viewports」）。
	int DimTxtShowAllClasses(MCObjectHandle viewport)
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

	// オブジェクト変数へ「いま入っている型と同じ型で」書く（型がヘッダに書かれて
	// いないビューポートの設定用。Findings「Dimensions」の作法）。
	std::string DimTxtWriteLikeCurrent(MCObjectHandle h, short selector, long long value)
	{
		TVariableBlock current;
		if (!gSDK->GetObjectVariable(h, selector, current))
			return "(読めないので書かなかった)";

		TVariableBlock next;
		Sint8 s8 = 0;
		Uint8 u8 = 0;
		Sint16 s16 = 0;
		Sint32 s32 = 0;
		bool b = false;
		if (current.GetSint8(s8))
			next = static_cast<Sint8>(value);
		else if (current.GetUint8(u8))
			next.SetUint8(static_cast<Uint8>(value));
		else if (current.GetSint16(s16))
			next = static_cast<Sint16>(value);
		else if (current.GetSint32(s32))
			next = static_cast<Sint32>(value);
		else if (current.GetBoolean(b))
			next = static_cast<Boolean>(value != 0);
		else
			return "(型が分からないので書かなかった)";

		const bool ok = gSDK->SetObjectVariable(h, selector, next) != 0;
		return std::string(ok ? "true" : "false") + " → " + DimTxtReadAny(h, selector);
	}
} // namespace

VW_PROBE("dim-text-size-and-scale", "寸法の文字の大きさは何で決まるか（注釈・縮尺）",
		 "縮尺 1 と 1/50 のレイヤで作った寸法を、平面／断面ビューポートの注釈へ移して "
		 "ovDimFontSize と ovDimTextSizeInPoints を読み比べる。移した後に書き直せるかも見る")
{
	const double kDimTxtDesignScale = 50.0;	  // デザインレイヤの縮尺 1/50
	const double kDimTxtViewportScale = 50.0; // ビューポートの縮尺 1/50
	const WorldCoord kDimTxtWallEndX = 4000;
	const WorldCoord kDimTxtWallThickness = 120;
	const WorldCoord kDimTxtSectionY = -1500;
	const WorldCoord kDimTxtSectionEndX = 4500; // 断面線の終点（注釈の横座標の原点）

	probe.log("== 0. 前提 ==");
	MCObjectHandle designLayer = gSDK->GetActiveLayer();
	if (designLayer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した");
		return;
	}
	probe.log("アクティブ（デザイン）レイヤの縮尺 = 1/" + DimTxtNum(DimTxtLayerScale(designLayer)));

	probe.log("");
	probe.log("== 1. 縮尺 1:1 のうちに 1 本作る（対照） ==");
	MCObjectHandle dimScale1OnDesign = DimTxtMakeDim(WorldPt(0, -6000), WorldPt(2000, -6000), 300);
	DimTxtLogDim(probe, "(S1) デザインレイヤが 1:1 のときに作った寸法", dimScale1OnDesign);

	probe.log("");
	probe.log("== 2. デザインレイヤの縮尺を 1/" + DimTxtNum(kDimTxtDesignScale) + " にする ==");
	gSDK->SetLayerScaleN(designLayer, kDimTxtDesignScale);
	probe.log("SetLayerScaleN 後の縮尺 = 1/" + DimTxtNum(DimTxtLayerScale(designLayer)));
	probe.log("★ 既にある寸法は縮尺の変更に追随するか:");
	DimTxtLogDim(probe, "(S1) 縮尺を変えた後に読み直す", dimScale1OnDesign);

	probe.log("");
	probe.log("== 3. 1/" + DimTxtNum(kDimTxtDesignScale) +
			  " のデザインレイヤで寸法を作る（A 群） ==");
	probe.log("期待: 6pt = 2.11667mm を 1/" + DimTxtNum(kDimTxtDesignScale) +
			  " で割り戻して ovDimFontSize = 105.833");
	// (A1) 平面ビューポートの注釈へ移すぶん。
	MCObjectHandle dimA1 = DimTxtMakeDim(WorldPt(0, 0), WorldPt(kDimTxtWallEndX, 0), 800);
	// (A2) 断面ビューポートの注釈へ移すぶん（横＝断面線の終点からの距離・縦＝Z）。
	MCObjectHandle dimA2 = DimTxtMakeDim(WorldPt(-kDimTxtSectionEndX, 0),
										 WorldPt(kDimTxtWallEndX - kDimTxtSectionEndX, 0), -500);
	// (A3) どこへも移さない対照。
	MCObjectHandle dimA3 = DimTxtMakeDim(WorldPt(0, -8000), WorldPt(2000, -8000), 300);
	DimTxtLogDim(probe, "(A1) 平面注釈へ移す予定", dimA1);
	DimTxtLogDim(probe, "(A2) 断面注釈へ移す予定", dimA2);
	DimTxtLogDim(probe, "(A3) レイヤに残す対照", dimA3);

	probe.log("");
	probe.log("== 4. 断面に写すものを用意する（壁） ==");
	MCObjectHandle wall =
		gSDK->CreateWall(WorldPt(0, 0), WorldPt(kDimTxtWallEndX, 0), kDimTxtWallThickness);
	if (wall == nil)
	{
		probe.fail("CreateWall が nil を返した");
	}
	else
	{
		// CreateWall は壁厚しか取らず、新規の空図面では高さ 0 になる（Findings「Walls」）。
		VectorWorks::SStoryObjectData bottomData;
		bottomData.fBound = VectorWorks::eStoryObjectBound_LayerElevation;
		bottomData.fBoundStory = 0;
		bottomData.fOffset = 0.0;
		VectorWorks::SStoryObjectData topData = bottomData;
		topData.fOffset = 2800.0;
		gSDK->SetWallOverallHeights(wall, bottomData, topData);
		gSDK->ResetObject(wall);
		WorldCoord top = 0;
		WorldCoord bottom = 0;
		gSDK->GetWallOverallHeights(wall, top, bottom);
		probe.log("壁 (0,0)-(4000,0) 厚 120mm / 高さ " + DimTxtNum(bottom) + "〜" + DimTxtNum(top));
	}

	probe.log("");
	probe.log("== 5. シートレイヤを作る（アクティブレイヤがそちらへ移る） ==");
	MCObjectHandle sheet = gSDK->CreateLayer("寸法の文字調査シート", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("シートレイヤを作れなかった");
		return;
	}
	MCObjectHandle activeNow = gSDK->GetActiveLayer();
	probe.log("いまのアクティブレイヤはシートレイヤか = " +
			  std::string(activeNow == sheet ? "はい" : "いいえ") + " / その縮尺 = 1/" +
			  DimTxtNum(DimTxtLayerScale(activeNow)));

	probe.log("");
	probe.log("== 6. シートレイヤ（1:1）で寸法を作る（B 群） ==");
	probe.log("期待: 6pt = 2.11667mm がそのまま ovDimFontSize になる");
	MCObjectHandle dimB1 = DimTxtMakeDim(WorldPt(0, 0), WorldPt(kDimTxtWallEndX, 0), 1600);
	MCObjectHandle dimB2 = DimTxtMakeDim(WorldPt(-kDimTxtSectionEndX, 0),
										 WorldPt(kDimTxtWallEndX - kDimTxtSectionEndX, 0), -1200);
	MCObjectHandle dimB3 = DimTxtMakeDim(WorldPt(0, 8000), WorldPt(2000, 8000), 300);
	DimTxtLogDim(probe, "(B1) 平面注釈へ移す予定", dimB1);
	DimTxtLogDim(probe, "(B2) 断面注釈へ移す予定", dimB2);
	DimTxtLogDim(probe, "(B3) シートレイヤに残す対照", dimB3);

	probe.log("");
	probe.log("== 7. 平面ビューポート（縮尺 1/" + DimTxtNum(kDimTxtViewportScale) + "） ==");
	MCObjectHandle planVP = gSDK->CreateViewport(sheet);
	if (planVP == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	probe.log("作った直後の ovViewportScale(1003) = " + DimTxtReadAny(planVP, ovViewportScale));
	probe.log("ovViewportScale ← " + DimTxtNum(kDimTxtViewportScale) + ": " +
			  DimTxtWriteReal(planVP, ovViewportScale, kDimTxtViewportScale) + " → 読み戻し " +
			  DimTxtReadAny(planVP, ovViewportScale));
	gSDK->SetViewportLayerVisibility(planVP, designLayer, 0);
	probe.log("クラスを全部表示へ戻した件数 = " + DimTxtInt(DimTxtShowAllClasses(planVP)));
	gSDK->UpdateViewport(planVP);

	probe.log("★ 注釈へ移す（ここが本題）:");
	probe.log("  (A1) AddViewportAnnotationObject = " +
			  std::string(gSDK->AddViewportAnnotationObject(planVP, dimA1) ? "true" : "false"));
	probe.log("  (B1) AddViewportAnnotationObject = " +
			  std::string(gSDK->AddViewportAnnotationObject(planVP, dimB1) ? "true" : "false"));
	DimTxtLogDim(probe, "(A1) 平面注釈へ移した後", dimA1);
	DimTxtLogDim(probe, "(B1) 平面注釈へ移した後", dimB1);
	probe.log("  ※ 移す前の値と比べる。変わっていなければ「大きさは作るときに焼き付く」。");
	probe.log("クラスを全部表示へ戻し直した件数 = " + DimTxtInt(DimTxtShowAllClasses(planVP)));
	gSDK->UpdateViewport(planVP);

	probe.log("");
	probe.log("== 8. 断面ビューポート（縮尺 1/" + DimTxtNum(kDimTxtViewportScale) + "） ==");
	MCObjectHandle sectionVP = gSDK->CreateSectionViewport(
		WorldPt(-500, kDimTxtSectionY), WorldPt(kDimTxtSectionEndX, kDimTxtSectionY),
		WorldPt(2000, 1500), 0, -1000, 5000, sheet);
	if (sectionVP == nil)
	{
		probe.fail("CreateSectionViewport が nil を返した");
		return;
	}
	probe.log("作った直後の ovViewportScale(1003) = " + DimTxtReadAny(sectionVP, ovViewportScale));
	probe.log("ovViewportScale ← " + DimTxtNum(kDimTxtViewportScale) + ": " +
			  DimTxtWriteReal(sectionVP, ovViewportScale, kDimTxtViewportScale) + " → 読み戻し " +
			  DimTxtReadAny(sectionVP, ovViewportScale));
	// 描かれるようにする作法（Findings「Viewports」/「Dimensions」）。順番が効く。
	probe.log("  ovViewportRenderType(1001) ← renderFinalHiddenLine(6): " +
			  DimTxtWriteLikeCurrent(sectionVP, ovViewportRenderType, renderFinalHiddenLine));
	// 断面線は壁の 1500mm 手前なので、壁は丸ごと「切断面より奥」にある → true が要る。
	probe.log("  ovSectionViewportDisplayObjectsBeyondCutPlane(1064) ← true: " +
			  DimTxtWriteLikeCurrent(sectionVP, ovSectionViewportDisplayObjectsBeyondCutPlane, 1));
	probe.log("  ovViewportDisplayPlanar(1035) ← false: " +
			  DimTxtWriteLikeCurrent(sectionVP, ovViewportDisplayPlanar, 0));
	probe.log("  ovViewportDisplay2DComponents(1059) ← true: " +
			  DimTxtWriteLikeCurrent(sectionVP, ovViewportDisplay2DComponents, 1));
	gSDK->SetViewportLayerVisibility(sectionVP, designLayer, 0);
	probe.log("  クラスを全部表示へ戻した件数 = " + DimTxtInt(DimTxtShowAllClasses(sectionVP)));
	gSDK->UpdateViewport(sectionVP);

	probe.log("★ 注釈へ移す（ここが本題）:");
	probe.log("  (A2) AddViewportAnnotationObject = " +
			  std::string(gSDK->AddViewportAnnotationObject(sectionVP, dimA2) ? "true" : "false"));
	probe.log("  (B2) AddViewportAnnotationObject = " +
			  std::string(gSDK->AddViewportAnnotationObject(sectionVP, dimB2) ? "true" : "false"));
	DimTxtLogDim(probe, "(A2) 断面注釈へ移した後", dimA2);
	DimTxtLogDim(probe, "(B2) 断面注釈へ移した後", dimB2);
	probe.log("クラスを全部表示へ戻し直した件数 = " + DimTxtInt(DimTxtShowAllClasses(sectionVP)));
	gSDK->UpdateViewport(sectionVP);

	probe.log("");
	probe.log("== 9. 注釈群そのものに縮尺はあるか ==");
	{
		MCObjectHandle annotGroup =
			gSDK->GetViewportGroup(sectionVP, static_cast<short>(kViewportGroupAnnotation));
		if (annotGroup == nil)
		{
			probe.log("GetViewportGroup(断面VP, 注釈) = nil");
		}
		else
		{
			probe.log("注釈群: 型=" + DimTxtInt(gSDK->GetObjectTypeN(annotGroup)) +
					  " / GetLayerScaleN を当ててみる = 1/" +
					  DimTxtNum(DimTxtLayerScale(annotGroup)));
			probe.log("  ※ 注釈群はレイヤではないので、意味のある値が返る保証は無い。");
			probe.log("  ※ 0 や 1 が返ったら「読めなかった」と解釈する。");
		}
	}

	probe.log("");
	probe.log("== 10. ビューポートの縮尺を変えたら、注釈の寸法の読みは動くか ==");
	probe.log("平面ビューポートの縮尺を 1/" + DimTxtNum(kDimTxtViewportScale) + " → 1/100 へ:");
	probe.log("  ovViewportScale ← 100: " + DimTxtWriteReal(planVP, ovViewportScale, 100.0) +
			  " → 読み戻し " + DimTxtReadAny(planVP, ovViewportScale));
	gSDK->UpdateViewport(planVP);
	DimTxtLogDim(probe, "(A1) VP 縮尺 1/100 にした後", dimA1);
	DimTxtLogDim(probe, "(B1) VP 縮尺 1/100 にした後", dimB1);
	probe.log("  ※ 値が動けば「読み戻す値は入れ物の縮尺に追随する」。動かなければ");
	probe.log("  　 「寸法が自分で持っている図面上の大きさ」であって入れ物に依らない。");
	probe.log("元に戻す: ovViewportScale ← " + DimTxtNum(kDimTxtViewportScale) + ": " +
			  DimTxtWriteReal(planVP, ovViewportScale, kDimTxtViewportScale) + " → 読み戻し " +
			  DimTxtReadAny(planVP, ovViewportScale));
	gSDK->UpdateViewport(planVP);
	DimTxtLogDim(probe, "(A1) 戻した後", dimA1);
	DimTxtLogDim(probe, "(B1) 戻した後", dimB1);

	probe.log("");
	probe.log("== 11. 移した後から直せるか（(B2) 断面注釈の小さい寸法で試す） ==");
	if (dimB2 != nil)
	{
		probe.log("(a) ovDimTextSizeInPoints ← 6（紙の大きさを書く）: " +
				  DimTxtWriteReal(dimB2, ovDimTextSizeInPoints, 6.0));
		DimTxtLogDim(probe, "  書いた直後", dimB2);

		const double kDimTxtWanted = 6.0 * 25.4 / 72.0 * kDimTxtViewportScale; // 105.833
		probe.log("(b) ovDimFontSize ← " + DimTxtNum(kDimTxtWanted) +
				  "（図面上の大きさを直に書く）: " +
				  DimTxtWriteReal(dimB2, ovDimFontSize, kDimTxtWanted));
		DimTxtLogDim(probe, "  書いた直後", dimB2);

		probe.log("(c) ResetObject の後（書いた値が残るか）:");
		gSDK->ResetObject(dimB2);
		DimTxtLogDim(probe, "  ResetObject の後", dimB2);

		probe.log("(d) 規格を当て直した後（規格が大きさを巻き戻さないか）:");
		{
			TVariableBlock v;
			if (gSDK->GetObjectVariable(dimB2, ovDimStandardName, v))
			{
				const bool ok = gSDK->SetObjectVariable(dimB2, ovDimStandardName, v) != 0;
				probe.log("  ovDimStandardName を同じ値で書き戻す = " +
						  std::string(ok ? "true" : "false"));
			}
			else
			{
				probe.log("  ovDimStandardName が読めなかった");
			}
		}
		DimTxtLogDim(probe, "  規格の当て直しの後", dimB2);

		probe.log("(e) ovDimShowValue を明示的に立て直す: " +
				  DimTxtWriteBool(dimB2, ovDimShowValue, true));
		DimTxtLogDim(probe, "  立て直した後", dimB2);

		gSDK->UpdateViewport(sectionVP);
	}

	probe.log("");
	probe.log("== 12. 対照（どこへも移していないもの） ==");
	DimTxtLogDim(probe, "(A3) デザインレイヤ 1/" + DimTxtNum(DimTxtLayerScale(designLayer)), dimA3);
	DimTxtLogDim(probe, "(B3) シートレイヤ 1/" + DimTxtNum(DimTxtLayerScale(sheet)), dimB3);

	probe.log("");
	probe.log("== 目で見ないと分からないこと（利用者へ） ==");
	probe.log("シートレイヤ「寸法の文字調査シート」を開いて、**断面ビューポート**を見てほしい。");
	probe.log("壁の下に横向きの寸法が 2 本並んでいる（上が (A2)、下が (B2)）。");
	probe.log("(1) **(A2) の寸法値（4000 のはず）は読める大きさで出ているか。**");
	probe.log("(2) **(B2) の寸法値は出ているか**（11 で大きさを書き直した後の姿）。");
	probe.log("    出ていないなら、書き直しでは直らないということ。");
	probe.log("(3) 2 本の文字の大きさが違って見えるか（同じに見えるか）。");
	probe.log("平面ビューポートのほうにも同じ 2 種類（(A1)/(B1)）が入っている。");
	probe.log("(4) そちらでも「大きい方だけ読める」になっているか。");
}
