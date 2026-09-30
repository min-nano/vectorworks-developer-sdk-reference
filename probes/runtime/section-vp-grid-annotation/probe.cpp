//
//	probes/runtime/section-vp-grid-annotation/probe.cpp
//
//	[issue #189] 断面ビューポートの注釈に出る「グリッド線」を SDK から掴む。
//
//	確かめること（issue #189 の 1〜5）:
//	  1. 注釈の中のグリッド線はどの PIO（universal 名・内部 ID）か。注釈群を降りれば
//	     見つかるか。
//	  2. いつ作られるか（CreateSectionViewport の直後か・UpdateViewport のたびか）。
//	     更新で作り直されるか（ハンドルと値が保たれるか）。
//	  3. 「水平線の長さ（始端）／（終端）」のパラメータの universal 名・欄型・単位。
//	     単位は**同じ図・同じ高さ範囲で縮尺だけ違うビューポート 2 枚**に同じ値を書き、
//	     注釈座標での外接の動き方を比べて決める（紙 mm なら縮尺に比例して動く）。
//	  4. 書いた後に ResetObject が要るか。更新で書いた値が保たれるか。
//	  5. 符号（ラベル枠）の位置の基準。**高さ範囲の上端だけ違うビューポート 2 枚**で
//	     グリッド線の外接の上端を比べる。
//
//	図面を壊す前提（新規の空図面で走らせる）。デザインレイヤ 1 枚・通り芯 4 本
//	（1 本は試験用）・押出 1 つ・シートレイヤ 1 枚・断面ビューポート 3 枚を作る。
//
//	【1〜4 巡目で確定したこと】（#189 の 1〜4 と 5 の前半。詳しくは Findings/Viewports.md）
//	  1. 注釈のグリッド線は **`GridAxis`（内部 ID 647・loc 名「グリッド線」）**——デザイン
//	     レイヤの通り芯と同じ PIO・同じ 32 件の表。注釈群を降りれば見つかる。
//	     注釈の中の個体は **`ovPositionLocked` が `true`**（デザインレイヤの個体は `false`）。
//	  2. **作るのは `UpdateViewport`**（`CreateSectionViewport` の直後は注釈群が nil）。
//	     **更新し直しても作り直さない**（ハンドル同一・書いた値も残る）。
//	  3. 欄は **`ShoulderLengthAtStart` / `ShoulderLengthAtEnd`**（欄型 7・既定 5）。
//	     **単位は用紙 mm**（1/100 で +10 → 1000・1/50 で +10 → 500）。
//	     **スタイルは握っていない**（由来 2 ＝ `_AllwaysByInstance`）。
//	  4. **書いた後に `ResetObject` が要る**（値は即読み戻せるが絵は Reset まで動かない）。
//	  5. **高さ範囲の上端は符号を動かさない**（上端 4000 と 9000 で外接が一致）。
//
//	【4 巡目（ビルド d12b01bcc821）で見つけた**こちらの 2 つの誤り**】
//	  * **レイヤを隠せていなかった。** `kLayerVisibilityInvisible` は **`-1`** で、
//	    渡していた `2` は **`kLayerVisibilityGrayed`（＝灰色で表示）**だった
//	    （`VWLayerObj.h`）。つまり A も D も**両方のモデルが映る設定**で、
//	    「A と D の外接が一致した」は**中身が同じだったから**にすぎない。
//	  * **断面が空のまま**だった（4 枚とも 2D 断面キャッシュ 0 件・外接 ±26.649）。
//	    壁の高さは書けている（読み戻し 上 3000 / 下 0）のに映らない。**1064 を書いた
//	    だけで読み戻していなかった**のが疑い（[Findings「調査の作法」](../../Findings/Investigation%20Techniques.md)
//	    の「書けたことを読み戻さずに信じない」）。切断面のどちら側に置いたかも
//	    確かめていない。
//
//	5 巡目で潰すのは「符号は**映っているモデルの上端**で決まるのか」ただ 1 つ。そのために:
//	  (a) **隠すときは `-1`**。しかも `GetViewportLayerVisibility` で**読み戻して**から進む。
//	  (b) **1064（奥）と 1065（手前）の両方を立て、4 つとも読み戻す**（1035 / 1059 も
//	      Findings の作法どおり与える）。
//	  (c) **`pt3` を反対側にした枚**も作り、どちら側が「奥」なのかを実測で決める。
//	  (d) **キャッシュ群 3 / 5 / 6 / 7 / 15 の件数**を出して、断面が本当に作られたかを見る。
//
#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	using VWFC::VWObjects::VWParametricObj;
	using VWFC::VWObjects::VWViewportObj;

	// 「水平線」の UTF-8 バイト列。**日本語リテラルを直接書かない**——narrow literal の
	// 実行時文字集合はコンパイラと OS の設定で変わるので、突き合わせに使う文字列だけは
	// バイトで書く（ログに出すだけの文は普通に書いてよい）。
	const char kProbeHorizJa[] = "\xE6\xB0\xB4\xE5\xB9\xB3\xE7\xB7\x9A";

	// -------------------------------------------------------------------- 書式
	std::string ProbeTextOf(const TXString& s)
	{
		const char* utf8 = static_cast<const char*>(s);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string ProbeReal(double value)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.3f", value);
		return std::string(buf);
	}

	std::string ProbeWhole(long long value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%lld", value);
		return std::string(buf);
	}

	std::string ProbeHandleText(MCObjectHandle h)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", reinterpret_cast<const void*>(h));
		return std::string(buf);
	}

	// ------------------------------------------------------------------ 外接
	struct ProbeBox
	{
		bool ok = false;
		double left = 0.0;
		double top = 0.0;
		double right = 0.0;
		double bottom = 0.0;
	};

	ProbeBox ProbeBoundsOf(MCObjectHandle h)
	{
		ProbeBox box;
		WorldRect rect;
		if (h != nil && gSDK->GetObjectBounds(h, rect))
		{
			box.ok = true;
			box.left = rect.left;
			box.top = rect.top;
			box.right = rect.right;
			box.bottom = rect.bottom;
		}
		return box;
	}

	std::string ProbeBoxText(const ProbeBox& box)
	{
		if (!box.ok)
			return "bounds=NG";
		return "bounds x[" + ProbeReal(box.left) + ".." + ProbeReal(box.right) + "] y[" +
			   ProbeReal(box.bottom) + ".." + ProbeReal(box.top) + "]";
	}

	// -------------------------------------------------- オブジェクト変数（何型でも出す）
	std::string ProbeVarText(MCObjectHandle h, short selector)
	{
		TVariableBlock value;
		if (h == nil || !gSDK->GetObjectVariable(h, selector, value))
			return "（読めない）";
		bool boolean = false;
		Sint16 s16 = 0;
		Sint32 s32 = 0;
		Real64 real = 0.0;
		if (value.GetBoolean(boolean))
			return boolean ? "true" : "false";
		if (value.GetSint32(s32))
			return ProbeWhole(s32);
		if (value.GetSint16(s16))
			return ProbeWhole(s16);
		if (value.GetReal64(real))
			return ProbeReal(real);
		return "（型 " + ProbeWhole(static_cast<long long>(value.GetType())) + "）";
	}

	// -------------------------------------------------------- 見つけた図形 1 件
	struct ProbeItem
	{
		MCObjectHandle h = nil;
		short type = 0;
		long long internalId = 0;
		std::string pioName;
		ProbeBox bounds;
	};

	// 容れ物を降りて、中の図形を全部書き出す。注釈群は criteria では見つからないので
	// 自分で降りる（Findings/Viewports.md）。
	void ProbeWalk(vwprobe::Report& probe, MCObjectHandle container, int depth,
				   std::vector<ProbeItem>& out)
	{
		if (container == nil || depth > 6)
			return;
		const std::string pad((size_t)depth * 2 + 2, ' ');
		for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil; h = gSDK->NextObject(h))
		{
			ProbeItem item;
			item.h = h;
			item.type = gSDK->GetObjectTypeN(h);
			item.bounds = ProbeBoundsOf(h);
			std::string extra;
			if (item.type == kParametricNode)
			{
				VWParametricObj pio(h);
				item.pioName = ProbeTextOf(pio.GetParametricName());
				item.internalId = static_cast<long long>(pio.GetInternalID());
				extra = " PIO=" + item.pioName + " 内部ID=" + ProbeWhole(item.internalId) +
						" loc名=" + ProbeTextOf(pio.GetLocalizedParametricName()) +
						" styleRef=" + ProbeWhole(static_cast<long long>(pio.GetStyleRefNumber())) +
						" 位置ロック=" + ProbeVarText(h, ovPositionLocked) +
						" パラメータ数=" + ProbeWhole(static_cast<long long>(pio.GetParamsCount()));
			}
			probe.log(pad + "型=" + ProbeWhole(item.type) + " h=" + ProbeHandleText(h) + extra +
					  " " + ProbeBoxText(item.bounds));
			out.push_back(item);
			if (item.type == kGroupNode)
				ProbeWalk(probe, h, depth + 1, out);
		}
	}

	// -------------------------------------------------------- パラメータ 1 件の素性
	struct ProbeParam
	{
		size_t index = 0;
		std::string name; // universal 名
		std::string loc;  // ローカライズ名
		int fieldStyle = 0;
		std::string value; // 文字列で読んだ値
		double real = 0.0;
	};

	std::vector<ProbeParam> ProbeDumpParams(vwprobe::Report& probe, MCObjectHandle h,
											bool logEveryRow)
	{
		std::vector<ProbeParam> params;
		if (h == nil || gSDK->GetObjectTypeN(h) != kParametricNode)
			return params;
		VWParametricObj pio(h);
		const size_t count = pio.GetParamsCount();
		for (size_t i = 0; i < count; ++i)
		{
			ProbeParam p;
			p.index = i;
			p.name = ProbeTextOf(pio.GetParamName(i));
			p.loc = ProbeTextOf(pio.GetParamLocalizedName(i));
			if (!p.name.empty())
			{
				const TXString univ(p.name.c_str());
				p.fieldStyle = static_cast<int>(pio.GetParamStyle(univ));
				p.value = ProbeTextOf(pio.GetParamValue(univ));
				p.real = pio.GetParamReal(univ);
			}
			params.push_back(p);
			if (logEveryRow)
				probe.log("    [" + ProbeWhole(static_cast<long long>(i)) + "] " + p.name +
						  " / loc=" + p.loc + " / 欄型=" + ProbeWhole(p.fieldStyle) +
						  " / 値=" + p.value + " / real=" + ProbeReal(p.real));
		}
		return params;
	}

	// 「水平線の長さ」のパラメータを選ぶ。
	//
	// **2 巡目で universal 名が確定した**（デザインレイヤの通り芯のパラメータ表 32 件）:
	//   ShoulderLengthAtStart / loc=水平線の長さ（先端） / 欄型 7 / 既定 5
	//   ShoulderLengthAtEnd   / loc=水平線の長さ（終端） / 欄型 7 / 既定 5
	// だから名前で引くのを第 1 候補にする。**注釈の中の個体が同じ表を持つとは限らない**
	// （別の PIO かもしれない）ので、当たらなかったときの筋も残す:
	//   ② ローカライズ名に「水平線」（UTF-8 バイト列）を含むもの
	//   ③ 欄型が座標（kFieldCoordDisp = 7）で値がちょうど 5 のもの
	std::vector<ProbeParam> ProbePickCandidates(vwprobe::Report& probe,
												const std::vector<ProbeParam>& params)
	{
		std::vector<ProbeParam> hits;
		const char* known[] = {"ShoulderLengthAtStart", "ShoulderLengthAtEnd"};
		for (const char* want : known)
		{
			for (const ProbeParam& p : params)
			{
				if (p.name == want)
					hits.push_back(p);
			}
		}
		if (!hits.empty())
		{
			probe.log("  候補の選び方: 2 巡目で確定した universal 名で引いた（" +
					  ProbeWhole(static_cast<long long>(hits.size())) + " 件）");
			return hits;
		}
		for (const ProbeParam& p : params)
		{
			if (p.loc.find(kProbeHorizJa) != std::string::npos)
				hits.push_back(p);
		}
		if (!hits.empty())
		{
			probe.log("  候補の選び方: ローカライズ名に「水平線」を含むもの（" +
					  ProbeWhole(static_cast<long long>(hits.size())) + " 件）");
			return hits;
		}
		for (const ProbeParam& p : params)
		{
			if (p.fieldStyle == kFieldCoordDisp && p.real > 4.999 && p.real < 5.001)
				hits.push_back(p);
		}
		probe.log("  候補の選び方: ローカライズ名で当たらなかったので、欄型 7 かつ値 5 のもの（" +
				  ProbeWhole(static_cast<long long>(hits.size())) + " 件）");
		return hits;
	}

	// ------------------------------------------------------- 断面ビューポート 1 枚
	struct ProbeViewport
	{
		std::string tag;
		std::string note; // 何が違う枚なのか
		MCObjectHandle vp = nil;
		MCObjectHandle modelLayer = nil; // 映すモデルのレイヤ（grid レイヤは常に映す）
		double endHeight = 0.0;
		double scale = 0.0;
		bool viewerOnMinusY = true; // pt3 を -Y 側に置くか（＝どちら側から見るか）
		std::vector<ProbeItem> annotation; // 更新後に注釈群で見つけたもの
	};

	// 通り芯（GridAxis）を 1 本作って、狙ったレイヤへ入れ直す。
	//
	// **作ったものはアクティブレイヤに入る。** SDK にアクティブレイヤを切り替える口は
	// 無い（`GetActiveLayer` はあるが setter が無い）ので、**入れ直す**
	// （Findings/Symbols.md「レイヤへ入れ直す」）。
	//
	// `path` が nil なら `CreateCustomObject`（点として置く）、非 nil なら
	// `CreateCustomObjectPath`（パスを与える）。**点として置いた通り芯は線を 1 本も
	// 持たない**（2 巡目の実測）ので、断面に出したいならパスで作る。
	MCObjectHandle ProbeMakeGridAxis(vwprobe::Report& probe, MCObjectHandle layer, double x,
									 double y, MCObjectHandle path, const char* label)
	{
		MCObjectHandle h = path != nil ? gSDK->CreateCustomObjectPath("GridAxis", path)
									   : gSDK->CreateCustomObject("GridAxis", WorldPt(x, y), 0.0);
		if (h == nil)
		{
			probe.fail(std::string(path != nil ? "CreateCustomObjectPath" : "CreateCustomObject") +
					   "(\"GridAxis\") が nil を返した（" + label + "）");
			return nil;
		}
		if (gSDK->ParentObject(h) != layer)
			gSDK->AddObjectToContainer(h, layer);
		gSDK->ResetObject(h);
		VWParametricObj pio(h);
		probe.log(std::string("  ") + label + ": h=" + ProbeHandleText(h) +
				  " PIO=" + ProbeTextOf(pio.GetParametricName()) +
				  " loc名=" + ProbeTextOf(pio.GetLocalizedParametricName()) +
				  " 内部ID=" + ProbeWhole(static_cast<long long>(pio.GetInternalID())) +
				  " styleRef=" + ProbeWhole(static_cast<long long>(pio.GetStyleRefNumber())) +
				  " 位置ロック=" + ProbeVarText(h, ovPositionLocked) + " パス=" +
				  ProbeHandleText(pio.GetObjectPath()) + " " + ProbeBoxText(ProbeBoundsOf(h)));
		return h;
	}

	// 2 頂点の 2D ポリラインを作る（パス PIO に渡すパス）。
	MCObjectHandle ProbeMakePath(double x, double y0, double y1)
	{
		VWFC::VWObjects::VWPolygon2DObj poly(
			{VWFC::Math::VWPoint2D(x, y0), VWFC::Math::VWPoint2D(x, y1)});
		poly.SetClosed(false);
		return poly;
	}

	// **「水平線の長さ」の単位を確かめる**（3 巡目で用紙 mm と出た回帰。縮尺を渡して倍率を出す）。
	void ProbeMeasureShoulderUnit(vwprobe::Report& probe, MCObjectHandle axis, double layerScale,
								  const char* paramName)
	{
		const TXString univ(paramName);
		VWParametricObj pio(axis);
		const double before = pio.GetParamReal(univ);
		const ProbeBox boxBefore = ProbeBoundsOf(axis);
		pio.SetParamReal(univ, before + 10.0);
		gSDK->ResetObject(axis);
		const ProbeBox boxAfter = ProbeBoundsOf(axis);
		const double dHeight =
			(boxBefore.ok && boxAfter.ok)
				? (boxAfter.top - boxAfter.bottom) - (boxBefore.top - boxBefore.bottom)
				: 0.0;
		probe.log(std::string("    縮尺 1/") + ProbeReal(layerScale) + ": " + paramName + " " +
				  ProbeReal(before) + " → " + ProbeReal(before + 10.0) + " で 高さ Δ=" +
				  ProbeReal(dHeight) + " → 書いた 10 に対する倍率=" + ProbeReal(dHeight / 10.0));
		VWParametricObj(axis).SetParamReal(univ, before);
		gSDK->ResetObject(axis);
	}

	// モデルを 1 つのレイヤへ置く。**壁は専用関数で高さを与える**
	// （Findings/Walls.md「CreateWall が建てた壁は、新規の空図面では高さ 0 になる」）。
	// 押出も並べて置き、どちらが断面に映るかをログで見分けられるようにする。
	void ProbeMakeModel(vwprobe::Report& probe, MCObjectHandle layer, double y0, double y1,
						double height, const char* label)
	{
		// 壁（高さは SetWallOverallHeights で明示する）
		MCObjectHandle wall = gSDK->CreateWall(WorldPt(0.0, y0), WorldPt(8000.0, y0), 200.0);
		if (wall != nil)
		{
			if (gSDK->ParentObject(wall) != layer)
				gSDK->AddObjectToContainer(wall, layer);
			VectorWorks::SStoryObjectData bottomData;
			bottomData.fBound = VectorWorks::eStoryObjectBound_LayerElevation;
			bottomData.fBoundStory = 0;
			bottomData.fOffset = 0.0;
			VectorWorks::SStoryObjectData topData = bottomData;
			topData.fOffset = height;
			const bool wroteHeight = gSDK->SetWallOverallHeights(wall, bottomData, topData);
			gSDK->ResetObject(wall);
			WorldCoord top = 0.0;
			WorldCoord bottom = 0.0;
			gSDK->GetWallOverallHeights(wall, top, bottom);
			probe.log(std::string("  ") + label + " 壁: h=" + ProbeHandleText(wall) +
					  " 高さ書けた=" + (wroteHeight ? "true" : "false") +
					  " 読み戻し 上=" + ProbeReal(top) + " 下=" + ProbeReal(bottom) +
					  " 親=" + ProbeHandleText(gSDK->ParentObject(wall)) + " " +
					  ProbeBoxText(ProbeBoundsOf(wall)));
		}
		else
		{
			probe.log(std::string("  ") + label + " 壁を作れなかった");
		}

		// 押出（3 巡目はこれが断面に映らなかった。並べて置いて見比べる）
		MCObjectHandle extrude = gSDK->CreateExtrude(0.0, height);
		if (extrude != nil)
		{
			WorldRect box;
			box.left = 0.0;
			box.right = 8000.0;
			box.bottom = y1;
			box.top = y1 + 2000.0;
			MCObjectHandle rect = gSDK->CreateRectangle(box);
			if (rect != nil)
				gSDK->AddObjectToContainer(rect, extrude);
			if (gSDK->ParentObject(extrude) != layer)
				gSDK->AddObjectToContainer(extrude, layer);
			gSDK->ResetObject(extrude);
			probe.log(std::string("  ") + label + " 押出: h=" + ProbeHandleText(extrude) +
					  " z=0.." + ProbeReal(height) +
					  " 親=" + ProbeHandleText(gSDK->ParentObject(extrude)) + " " +
					  ProbeBoxText(ProbeBoundsOf(extrude)));
		}
		else
		{
			probe.log(std::string("  ") + label + " 押出を作れなかった");
		}
	}
	// キャッシュ群の件数を数える。**断面が本当に作られたか**の目安
	// （Findings/Viewports.md の 2×2 の表で「キャッシュ群 有/無」を分けていたもの）。
	std::string ProbeCacheCounts(MCObjectHandle vp)
	{
		const short groups[] = {kViewportGroupCache, kViewportGroup3DSectionCache,
								kViewportGroup2DSectionCache, kViewportGroup3DSectionExCache,
								kViewportGroup2DSectionPenCache};
		std::string out;
		for (short g : groups)
		{
			size_t n = 0;
			MCObjectHandle container = gSDK->GetViewportGroup(vp, g);
			for (MCObjectHandle h = gSDK->FirstMemberObj(container); h != nil;
				 h = gSDK->NextObject(h))
				++n;
			if (!out.empty())
				out += " ";
			out += ProbeWhole(g) + "=" + ProbeWhole(static_cast<long long>(n));
		}
		return out;
	}
} // namespace

VW_PROBE("section-vp-grid-annotation", "断面ビューポートの注釈のグリッド線を掴む",
		 "通り芯とモデルを置いて断面ビューポートを 6 枚作り、符号の位置の基準を決める")
{
	probe.log("== 0. 前置き ==");
	probe.log("  アクティブレイヤ（走り出し）= " + ProbeHandleText(gSDK->GetActiveLayer()));
	gSDK->DefineCustomObject("GridAxis", kCustomObjectPrefNever);

	// レイヤを 4 枚。**通り芯は専用のレイヤへ置き、どのビューポートでも必ず映す。**
	// モデルは高さ 3000 と 6000 の 2 枚に分け、**表示レイヤの切り替えだけで
	// 「映っているモデル」を変える**（＝符号の位置の基準を割り出すため）。
	MCObjectHandle gridLayer = gSDK->CreateLayer("i189-grid", kLayerDesign);
	MCObjectHandle lowLayer = gSDK->CreateLayer("i189-low", kLayerDesign);
	MCObjectHandle highLayer = gSDK->CreateLayer("i189-high", kLayerDesign);
	MCObjectHandle layer50 = gSDK->CreateLayer("i189-grid-50", kLayerDesign);
	if (gridLayer == nil || lowLayer == nil || highLayer == nil || layer50 == nil)
	{
		probe.fail("CreateLayer(kLayerDesign) が nil を返した");
		return;
	}
	gSDK->SetLayerScaleN(gridLayer, 100.0);
	gSDK->SetLayerScaleN(lowLayer, 100.0);
	gSDK->SetLayerScaleN(highLayer, 100.0);
	gSDK->SetLayerScaleN(layer50, 50.0);
	probe.log("  grid=" + ProbeHandleText(gridLayer) + " low=" + ProbeHandleText(lowLayer) +
			  " high=" + ProbeHandleText(highLayer) + " 1/50=" + ProbeHandleText(layer50));

	// ------------------------------------------- 1. 通り芯の素性とパラメータ表（回帰）
	probe.log("== 1. 通り芯（GridAxis）の素性とパラメータ表 ==");
	MCObjectHandle pointAxis =
		ProbeMakeGridAxis(probe, gridLayer, -50000.0, 0.0, nil, "点として置いた通り芯（遠く）");
	if (pointAxis == nil)
		return;
	probe.log("  パラメータ表（全件）:");
	const std::vector<ProbeParam> axisParams = ProbeDumpParams(probe, pointAxis, true);

	probe.log("  スタイルとの関係（由来 0=ByInstance / 1=ByStyle / 2=AllwaysByInstance）:");
	{
		VWParametricObj pio(pointAxis);
		probe.log("    styleRef=" + ProbeWhole(static_cast<long long>(pio.GetStyleRefNumber())) +
				  " スタイルの実体=" + ProbeHandleText(pio.GetStyleHandle()));
		const char* watch[] = {"ShoulderLengthAtStart", "ShoulderLengthAtEnd", "ShowBubbleAt",
							   "BubbleScaleFactor",		"AddElbowToSholder",   "World-based"};
		for (const char* name : watch)
		{
			const TXString univ(name);
			probe.log(std::string("    ") + name + ": 由来=" +
					  ProbeWhole(static_cast<long long>(
						  gSDK->GetPluginStyleParameterType(pointAxis, univ))) +
					  " 値=" + ProbeTextOf(VWParametricObj(pointAxis).GetParamValue(univ)));
		}
	}

	probe.log("  単位（5 → 15 を縮尺の違う 2 枚で。3 巡目の回帰）:");
	ProbeMeasureShoulderUnit(probe, pointAxis, 100.0, "ShoulderLengthAtStart");
	MCObjectHandle pointAxis50 =
		ProbeMakeGridAxis(probe, layer50, -60000.0, 0.0, nil, "1/50 のレイヤの通り芯");
	if (pointAxis50 != nil)
		ProbeMeasureShoulderUnit(probe, pointAxis50, 50.0, "ShoulderLengthAtStart");

	// --------------------------------- 2. 通り芯 3 本（パス）とモデル 2 段
	probe.log("== 2. 通り芯 3 本（パス）と、高さの違うモデル 2 段 ==");
	const double kGridX[3] = {0.0, 4000.0, 8000.0};
	std::vector<MCObjectHandle> axes;
	for (int i = 0; i < 3; ++i)
	{
		const std::string label = "通り芯 " + ProbeWhole(i + 1) + "（パス）";
		MCObjectHandle path = ProbeMakePath(kGridX[i], -2000.0, 10000.0);
		if (path == nil)
		{
			probe.fail("2 頂点の 2D ポリライン（パス）を作れなかった");
			return;
		}
		MCObjectHandle axis =
			ProbeMakeGridAxis(probe, gridLayer, kGridX[i], 0.0, path, label.c_str());
		if (axis == nil)
			return;
		axes.push_back(axis);
	}

	// 断面線はパスの中ほど（y=4000）へ引く。**モデルは切断面の奥（y > 4000）へ置く。**
	const double cutY = 4000.0;
	ProbeMakeModel(probe, lowLayer, 5000.0, 6000.0, 3000.0, "低い方（3000）");
	ProbeMakeModel(probe, highLayer, 5500.0, 8000.0, 6000.0, "高い方（6000）");
	probe.log("  断面線の y = " + ProbeReal(cutY) + "（通り芯のパス -2000〜10000 の中ほど）");
	probe.log("  通り芯 1 の外接 " + ProbeBoxText(ProbeBoundsOf(axes[0])));

	// ---------------------------------------------------- 3. シートレイヤと 4 枚
	probe.log("== 3. シートレイヤと断面ビューポート 6 枚 ==");
	MCObjectHandle sheet = gSDK->CreateLayer("i189-sheet", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}

	std::vector<ProbeViewport> vps;
	{
		ProbeViewport a;
		a.tag = "A";
		a.note = "低いモデル（3000）・上端 9000・1/100・視点 -Y（本命。ここへ書く）";
		a.modelLayer = lowLayer;
		a.endHeight = 9000.0;
		a.scale = 100.0;
		vps.push_back(a);
		ProbeViewport b;
		b.tag = "B";
		b.note = "A との差は高さ範囲の上端だけ（4000）";
		b.modelLayer = lowLayer;
		b.endHeight = 4000.0;
		b.scale = 100.0;
		vps.push_back(b);
		ProbeViewport c;
		c.tag = "C";
		c.note = "A との差は縮尺だけ（1/50）";
		c.modelLayer = lowLayer;
		c.endHeight = 9000.0;
		c.scale = 50.0;
		vps.push_back(c);
		ProbeViewport d;
		d.tag = "D";
		d.note = "A との差は**映っているモデルの高さだけ**（3000 → 6000）";
		d.modelLayer = highLayer;
		d.endHeight = 9000.0;
		d.scale = 100.0;
		vps.push_back(d);
		// **4 巡目は 6 枚とも断面が空だった。** どちら側が「奥」なのかを実測で決めるため、
		// A / D と同じ設定で **pt3 だけを反対側**に置いた 2 枚を足す。
		ProbeViewport e;
		e.tag = "E";
		e.note = "A との差は **pt3（見る側）だけ**——+Y 側から見る";
		e.modelLayer = lowLayer;
		e.endHeight = 9000.0;
		e.scale = 100.0;
		e.viewerOnMinusY = false;
		vps.push_back(e);
		ProbeViewport f;
		f.tag = "F";
		f.note = "E との差は**映っているモデルの高さだけ**（3000 → 6000）";
		f.modelLayer = highLayer;
		f.endHeight = 9000.0;
		f.scale = 100.0;
		f.viewerOnMinusY = false;
		vps.push_back(f);
	}

	for (size_t i = 0; i < vps.size(); ++i)
	{
		ProbeViewport& v = vps[i];
		const double pt3Y = v.viewerOnMinusY ? cutY - 8000.0 : cutY + 14000.0;
		v.vp = gSDK->CreateSectionViewport(WorldPt(-2000.0, cutY), WorldPt(10000.0, cutY),
										   WorldPt(4000.0, pt3Y), 0.0, -500.0, v.endHeight, sheet);
		if (v.vp == nil)
		{
			probe.fail("CreateSectionViewport が nil を返した（" + v.tag + "）");
			return;
		}
		probe.log("  [" + v.tag + "] " + v.note);
		probe.log("    vp=" + ProbeHandleText(v.vp) + " 上端=" + ProbeReal(v.endHeight) +
				  " 縮尺=1/" + ProbeReal(v.scale) + " 映すモデル=" + ProbeHandleText(v.modelLayer) +
				  " 更新前の注釈群=" +
				  ProbeHandleText(gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation)));

		// **通り芯のレイヤと、その枚が映すモデルのレイヤだけを表示にする。**
		// 高さの違う 2 段のうち片方だけを映すのが、この巡の肝。
		// **隠すのは `-1`（`kLayerVisibilityInvisible`）。** 4 巡目は `2` を渡していたが、
		// それは `kLayerVisibilityGrayed`（＝灰色で**表示**）で、隠せていなかった
		// ——だから「A と D の外接が一致した」は中身が同じだっただけである。
		// 書いたら **読み戻して**確かめる（Findings「調査の作法」）。
		std::string visLog;
		gSDK->ForEachLayerN(
			[&v, sheet, gridLayer, layer50, &visLog](MCObjectHandle layer)
			{
				if (layer == sheet)
					return;
				const bool show =
					(layer == gridLayer) || (layer == v.modelLayer) || (layer == layer50);
				gSDK->SetViewportLayerVisibility(v.vp, layer, show ? 0 : -1);
				short readBack = 99;
				gSDK->GetViewportLayerVisibility(v.vp, layer, readBack);
				TXString name;
				gSDK->GetObjectName(layer, name);
				if (!visLog.empty())
					visLog += " / ";
				visLog += ProbeTextOf(name) + "=" + ProbeWhole(readBack) +
						  (show ? "(表示のつもり)" : "(隠すつもり)");
			});
		probe.log("    表示レイヤの読み戻し（0=表示 / -1=非表示 / 2=灰色）: " + visLog);
		gSDK->ForEachClass(
			true, [&v](MCObjectHandle cls)
			{ gSDK->SetViewportClassVisibility(v.vp, gSDK->GetObjectInternalIndex(cls), 0); });
		VWViewportObj(v.vp).SetRenderType(renderFinalHiddenLine);
		// **奥（1064）と手前（1065）の両方を立てる。** 4 巡目は 1064 だけを書いて
		// 読み戻しもしておらず、モデルが切断面のどちら側にいるのかも確かめていなかった。
		// 1035（プレイナーは表示しない）・1059（2D コンポーネントを表示する）も
		// Findings/Viewports.md の作法どおり与える。**4 つとも読み戻す。**
		const short kFlagSel[] = {ovSectionViewportDisplayObjectsBeyondCutPlane,
								  ovSectionViewportDisplayObjectsBeforeCutPlane,
								  ovViewportDisplayPlanar, ovViewportDisplay2DComponents};
		const Boolean kFlagWant[] = {1, 1, 0, 1};
		std::string flagLog;
		for (size_t fi = 0; fi < 4; ++fi)
		{
			TVariableBlock value;
			value = static_cast<Boolean>(kFlagWant[fi]);
			const bool wrote = gSDK->SetObjectVariable(v.vp, kFlagSel[fi], value) != 0;
			if (!flagLog.empty())
				flagLog += " / ";
			flagLog += ProbeWhole(kFlagSel[fi]) + ": 書けた=" + (wrote ? "true" : "false") +
					   " 読み戻し=" + ProbeVarText(v.vp, kFlagSel[fi]);
		}
		probe.log("    表示の欄（1064 奥 / 1065 手前 / 1035 プレイナー / 1059 2D）: " + flagLog);
		TVariableBlock scale;
		scale = static_cast<Real64>(v.scale);
		gSDK->SetObjectVariable(v.vp, ovViewportScale, scale);

		gSDK->UpdateViewport(v.vp);
		// **ビューポートの外接が「断面に何か映ったか」の目安。** 3 巡目は 53mm 角の
		// 空き箱で、そのせいで「映っているモデルの上端か」を確かめられなかった。
		probe.log("    更新後: 注釈群=" +
				  ProbeHandleText(gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation)) + " " +
				  ProbeBoxText(ProbeBoundsOf(v.vp)) + "（±26.649 のままなら断面は空）");
		probe.log("    注釈群の中身:");
		ProbeWalk(probe, gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation), 0, v.annotation);
		probe.log("    断面群（kViewportGroupSection）の中身:");
		std::vector<ProbeItem> section;
		ProbeWalk(probe, gSDK->GetViewportGroup(v.vp, kViewportGroupSection), 0, section);
		probe.log("    キャッシュ群の件数（3/5/6/7/15。すべて 0 なら断面は作られていない）: " +
				  ProbeCacheCounts(v.vp));
	}

	// ------------------------- 4. 符号の位置の基準（この巡の本題。#189 の 5 の後半）
	probe.log("== 4. 符号の位置の基準——A 対 D と E 対 F が「映っているモデルの高さ」だけの差 ==");
	for (size_t vi = 0; vi < vps.size(); ++vi)
	{
		const ProbeViewport& v = vps[vi];
		probe.log("  [" + v.tag + "] " + v.note);
		size_t n = 0;
		for (const ProbeItem& item : v.annotation)
		{
			if (item.type != kParametricNode)
				continue;
			probe.log("    グリッド線[" + ProbeWhole(static_cast<long long>(n)) + "] " +
					  item.pioName + " " + ProbeBoxText(ProbeBoundsOf(item.h)));
			++n;
		}
		probe.log("    ビューポート自身の外接: " + ProbeBoxText(ProbeBoundsOf(v.vp)));
	}

	// --------------------------------- 5. 書いて読み戻す（A と C。3 巡目の回帰）
	probe.log("== 5. 候補を書いて読み戻す（A と C。グリッド線[0] は対照で触らない） ==");
	std::vector<ProbeParam> aParams;
	for (const ProbeItem& item : vps[0].annotation)
	{
		if (item.type == kParametricNode)
		{
			aParams = ProbeDumpParams(probe, item.h, false);
			break;
		}
	}
	if (aParams.empty())
	{
		probe.fail("A の注釈に PIO が 1 件も無い——グリッド線は注釈群には現れなかった");
		return;
	}
	const std::vector<ProbeParam> candidates = ProbePickCandidates(probe, aParams);
	const size_t kWriteTargets[2] = {1, 2};
	for (size_t vi = 0; vi < vps.size(); ++vi)
	{
		if (vps[vi].tag != "A" && vps[vi].tag != "C")
			continue;
		ProbeViewport& v = vps[vi];
		std::vector<ProbeItem> pios;
		for (const ProbeItem& item : v.annotation)
		{
			if (item.type == kParametricNode)
				pios.push_back(item);
		}
		for (size_t ci = 0; ci < candidates.size() && ci < 2; ++ci)
		{
			const size_t target = kWriteTargets[ci];
			if (target >= pios.size())
				continue;
			MCObjectHandle h = pios[target].h;
			const TXString univ(candidates[ci].name.c_str());
			VWParametricObj pio(h);
			const double before = pio.GetParamReal(univ);
			const ProbeBox boxBefore = ProbeBoundsOf(h);
			pio.SetParamReal(univ, before + 10.0);
			const ProbeBox boxAfterWrite = ProbeBoundsOf(h);
			const bool reset = gSDK->ResetObject(h) != 0;
			const ProbeBox boxAfterReset = ProbeBoundsOf(h);
			probe.log("    [" + v.tag + "] グリッド線[" +
					  ProbeWhole(static_cast<long long>(target)) + "] " + candidates[ci].name +
					  ": 前=" + ProbeReal(before) + " → " + ProbeReal(before + 10.0) +
					  " 読み戻し=" + ProbeReal(VWParametricObj(h).GetParamReal(univ)) +
					  " ResetObject=" + (reset ? "true" : "false"));
			probe.log(
				"      外接 書いた直後の上端 Δ=" + ProbeReal(boxAfterWrite.top - boxBefore.top) +
				" / Reset 後の上端 Δ=" + ProbeReal(boxAfterReset.top - boxBefore.top));
		}
	}

	// ------------------------------ 6. 更新で保たれるか（3 巡目の回帰）
	probe.log("== 6. A を更新して、書いた値とハンドルが残るか ==");
	{
		ProbeViewport& v = vps[0];
		std::vector<MCObjectHandle> beforeHandles;
		for (const ProbeItem& item : v.annotation)
		{
			if (item.type == kParametricNode)
				beforeHandles.push_back(item.h);
		}
		gSDK->UpdateViewport(v.vp);
		std::vector<ProbeItem> after;
		ProbeWalk(probe, gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation), 0, after);
		std::vector<MCObjectHandle> afterHandles;
		for (const ProbeItem& item : after)
		{
			if (item.type == kParametricNode)
				afterHandles.push_back(item.h);
		}
		bool same = beforeHandles.size() == afterHandles.size();
		for (size_t i = 0; same && i < beforeHandles.size(); ++i)
			same = beforeHandles[i] == afterHandles[i];
		probe.log("  件数 更新前=" + ProbeWhole(static_cast<long long>(beforeHandles.size())) +
				  " 更新後=" + ProbeWhole(static_cast<long long>(afterHandles.size())) +
				  " ハンドルは " + std::string(same ? "同一（作り直されていない）" : "違う"));
		for (size_t ci = 0; ci < candidates.size() && ci < 2; ++ci)
		{
			const size_t target = kWriteTargets[ci];
			if (target >= afterHandles.size())
				continue;
			probe.log("  更新後の値 グリッド線[" + ProbeWhole(static_cast<long long>(target)) +
					  "] " + candidates[ci].name + " = " +
					  ProbeReal(VWParametricObj(afterHandles[target])
									.GetParamReal(TXString(candidates[ci].name.c_str()))));
		}
	}

	probe.log("== おわり ==");
}
