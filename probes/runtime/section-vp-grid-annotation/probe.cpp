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
//	【1 巡目（ビルド 8016526ba982）で分かったこと】
//	  * `CreateCustomObject("GridAxis", …)` は**組み込みの通り芯**を作る（内部 ID 647・
//	    パラメータ 32 件）。名前は当たっていた。
//	  * **`GridAxis` は線分 PIO ではない**——`LineLength` を持たないので
//	    `SetLinearObjectPos` は完全な no-op（Findings/Parametric Objects.md のとおり）。
//	    その結果、作りたての通り芯は**外接 y[-2000..-775]＝1225mm しか伸びていなかった**。
//	  * だから y=4000 に引いた断面線は**通り芯を 1 本も横切っていなかった**。
//	    注釈群は 3 枚とも nil で、断面群にあったのは `Section Line2`(645) 1 本だけ。
//	    ＝「注釈にグリッド線が出ない」は**通り芯が切断面に届いていなかったから**の
//	    可能性が残っており、1 巡目の失敗は結論ではない。
//	【2 巡目（ビルド 7dc0e49e2116）で分かったこと】
//	  * **`GridAxis` のローカライズ名は「グリッド線」**——OIP の種類がそう出ていたものと
//	    同じ PIO である。スタイル（`styleRef=51`）はツールのものが勝手に当たる。
//	  * **パラメータ表は 32 件で、座標欄（欄型 7）は 2 つだけ**:
//	      `ShoulderLengthAtStart`（loc=水平線の長さ（先端）・既定 5）
//	      `ShoulderLengthAtEnd`  （loc=水平線の長さ（終端）・既定 5）
//	    ＝**issue #189 が探していた欄はこの 2 つ**。「長さ」のパラメータは無い。
//	  * **単位は用紙 mm らしい。** `ShoulderLengthAtStart` を 5 → 12000 にしたら外接が
//	    **1,199,500** 伸びた——書いた差 11,995 の**ちょうど 100 倍＝そのレイヤの縮尺**。
//	    `World-based` が `False` なのもこれと合う。3 巡目で縮尺 1/50 のレイヤでも測って
//	    確かめる。
//	  * **`ShoulderLengthAtEnd` を 12000 にしても外接は 1mm も動かなかった**
//	    （`ShowBubbleAt` が `Start Point` だから、終端側には何も描かれていないと読める）。
//	  * **点として置いた通り芯は「線」を 1 本も持たない**（外接は 750 × 1225 ＝ラベル枠と
//	    水平線だけ）。そして 2 巡目はその外接の真ん中を断面線にしたので、**水平線を
//	    12000 に伸ばしたぶん断面線が y=600362 まで飛び、断面が空（53mm 角）になった**。
//	    ＝2 巡目も「グリッド線は注釈に現れない」の証拠にはならない。
//	  * 注釈群は `AddViewportAnnotationObject` で作れた（`true`・群が nil から実体に）。
//	    置いた矩形は入ったが、**VW がグリッド線を足すことはなかった**——ただし断面が
//	    空だったので、これも決め手にはならない。
//	3 巡目で潰すのはそこ: (a) 通り芯を**パス PIO**（`CreateCustomObjectPath` ＋ 2 頂点の
//	2D ポリライン）として作って本当の「線」を持たせ、(b) 水平線の長さは既定のままにして
//	断面線を通り芯のパスの中ほどに引き、(c) 単位は縮尺 1/100 と 1/50 の 2 枚で 5 → 15 を
//	測って決める。(d) 注釈群が nil のときに矩形で作らせる筋は残す。
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
		MCObjectHandle vp = nil;
		double endHeight = 0.0;
		double scale = 0.0;
		std::vector<ProbeItem> annotation; // 更新後に注釈群で見つけたもの
	};

	// 通り芯（GridAxis）を 1 本作って、狙ったレイヤへ入れ直す。
	//
	// **作ったものはアクティブレイヤに入る。** SDK にアクティブレイヤを切り替える口は
	// 無い（`GetActiveLayer` はあるが setter が無い）ので、**入れ直す**
	// （Findings/Symbols.md「レイヤへ入れ直す」）。
	//
	// `path` が nil なら `CreateCustomObject`（点として置く）、非 nil なら
	// `CreateCustomObjectPath`（パスを与える）。**2 巡目で、点として置いた通り芯は
	// 線を 1 本も持たないと分かった**ので、3 巡目の本番はパスで作る。
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
				  " パラメータ数=" + ProbeWhole(static_cast<long long>(pio.GetParamsCount())) +
				  " 親=" + ProbeHandleText(gSDK->ParentObject(h)) + " パス=" +
				  ProbeHandleText(pio.GetObjectPath()) + " " + ProbeBoxText(ProbeBoundsOf(h)));
		return h;
	}

	// 2 頂点の 2D ポリラインを作る（パス PIO に渡すパス）。
	// **パスの型を間違えると「生成できるのに何も描かれない」**（Findings/Parametric
	// Objects.md）ので、2D ポリラインで渡す。
	MCObjectHandle ProbeMakePath(double x, double y0, double y1)
	{
		VWFC::VWObjects::VWPolygon2DObj poly(
			{VWFC::Math::VWPoint2D(x, y0), VWFC::Math::VWPoint2D(x, y1)});
		poly.SetClosed(false);
		return poly;
	}

	// **「水平線の長さ」の単位を決める。** 同じ 5 → 15（issue #189 が OIP で観察した値）を
	// **縮尺だけが違う 2 枚のレイヤ**の個体に書き、外接がどれだけ動くかを比べる。
	//   用紙 mm なら 動き = 10 × 縮尺（1/100 なら 1000・1/50 なら 500）
	//   図面（世界）の mm なら 動き = 10（縮尺に依らない）
	void ProbeMeasureShoulderUnit(vwprobe::Report& probe, MCObjectHandle axis, double layerScale,
								  const char* paramName)
	{
		const TXString univ(paramName);
		VWParametricObj pio(axis);
		const double before = pio.GetParamReal(univ);
		const ProbeBox boxBefore = ProbeBoundsOf(axis);
		pio.SetParamReal(univ, before + 10.0);
		gSDK->ResetObject(axis);
		const double readBack = VWParametricObj(axis).GetParamReal(univ);
		const ProbeBox boxAfter = ProbeBoundsOf(axis);
		const double dTop = (boxBefore.ok && boxAfter.ok) ? boxAfter.top - boxBefore.top : 0.0;
		const double dHeight =
			(boxBefore.ok && boxAfter.ok)
				? (boxAfter.top - boxAfter.bottom) - (boxBefore.top - boxBefore.bottom)
				: 0.0;
		probe.log(std::string("    縮尺 1/") + ProbeReal(layerScale) + " のレイヤ: " + paramName +
				  " " + ProbeReal(before) + " → " + ProbeReal(before + 10.0) + "（読み戻し " +
				  ProbeReal(readBack) + "）で 外接の上端 Δ=" + ProbeReal(dTop) + " 高さ Δ=" +
				  ProbeReal(dHeight) + " → 書いた 10 に対する倍率=" + ProbeReal(dHeight / 10.0));
		probe.log("      前: " + ProbeBoxText(boxBefore));
		probe.log("      後: " + ProbeBoxText(boxAfter));
		// 戻す
		VWParametricObj(axis).SetParamReal(univ, before);
		gSDK->ResetObject(axis);
	}
} // namespace

VW_PROBE("section-vp-grid-annotation", "断面ビューポートの注釈のグリッド線を掴む",
		 "通り芯を 3 本引いて断面ビューポートを 3 枚作り、注釈のグリッド線を探して書く")
{
	// ---------------------------------------------------------------- 前置き
	probe.log("== 0. 前置き ==");
	probe.log("  アクティブレイヤ（走り出し）= " + ProbeHandleText(gSDK->GetActiveLayer()));
	// 「オブジェクトの設定」ダイアログで止まらないように（probes/runtime/README.md）。
	gSDK->DefineCustomObject("GridAxis", kCustomObjectPrefNever);

	// デザインレイヤを 2 枚作る。**中身が同じで縮尺だけが違う 2 枚**にして、
	// 「水平線の長さ」の単位を縮尺で割り出す（下記 1c）。
	MCObjectHandle design = gSDK->CreateLayer("i189-model-100", kLayerDesign);
	MCObjectHandle design50 = gSDK->CreateLayer("i189-model-50", kLayerDesign);
	if (design == nil || design50 == nil)
	{
		probe.fail("CreateLayer(kLayerDesign) が nil を返した");
		return;
	}
	gSDK->SetLayerScaleN(design, 100.0);
	gSDK->SetLayerScaleN(design50, 50.0);
	probe.log("  デザインレイヤ 1/100 h=" + ProbeHandleText(design) +
			  " / 1/50 h=" + ProbeHandleText(design50) +
			  " / 作った後のアクティブレイヤ=" + ProbeHandleText(gSDK->GetActiveLayer()));

	// ------------------------------------------- 1. 通り芯の素性とパラメータ表
	probe.log("== 1. 通り芯（GridAxis）の素性とパラメータ表 ==");
	probe.log("  1a. 点として置いた 1 本（断面線の届かない遠く）:");
	MCObjectHandle pointAxis =
		ProbeMakeGridAxis(probe, design, -50000.0, 0.0, nil, "点として置いた通り芯");
	if (pointAxis == nil)
		return;
	probe.log("  パラメータ表（全件）:");
	const std::vector<ProbeParam> axisParams = ProbeDumpParams(probe, pointAxis, true);

	// スタイルが「水平線の長さ」を握っていないか（#189 の 3 の後半）。
	probe.log("  1b. スタイルとの関係（styleRef と、パラメータごとの由来）:");
	{
		VWParametricObj pio(pointAxis);
		const RefNumber styleRef = pio.GetStyleRefNumber();
		probe.log("    styleRef=" + ProbeWhole(static_cast<long long>(styleRef)) +
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

	// 1c. 単位。**2 巡目で ShoulderLengthAtStart を 5 → 12000 にしたら外接が 1199500 伸びた**
	// ——ちょうど 100 倍（＝そのレイヤの縮尺）。用紙 mm だという読みを、縮尺の違う 2 枚で
	// 確かめる。
	probe.log("  1c. 「水平線の長さ（先端）」の単位（5 → 15 を縮尺の違う 2 枚で）:");
	ProbeMeasureShoulderUnit(probe, pointAxis, 100.0, "ShoulderLengthAtStart");
	MCObjectHandle pointAxis50 =
		ProbeMakeGridAxis(probe, design50, -60000.0, 0.0, nil, "1/50 のレイヤの通り芯");
	if (pointAxis50 != nil)
		ProbeMeasureShoulderUnit(probe, pointAxis50, 50.0, "ShoulderLengthAtStart");

	// ------------------------------- 2. 本番の通り芯 3 本を**パス**として作る
	//
	// 2 巡目の肝: **点として置いた通り芯は線を 1 本も持たない**（外接は
	// ラベル枠＋水平線だけの 750 × 1225）。長さのパラメータは 32 件のどこにも無く、
	// 座標欄は `ShoulderLengthAtStart` / `AtEnd` の 2 つだけだった。
	// ＝**グリッド線の「線」はパスで決まる**とみて、パス PIO として作り直す。
	probe.log("== 2. 通り芯 3 本を**パス PIO** として作る ==");
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
		MCObjectHandle axis = ProbeMakeGridAxis(probe, design, kGridX[i], 0.0, path, label.c_str());
		if (axis == nil)
			return;
		axes.push_back(axis);
	}

	// **断面線の y は、通り芯が実際に占めている範囲の真ん中に取る。**
	// ただし 2 巡目のように「水平線の長さ」で外接が km 単位に膨らんでいると、
	// 真ん中が建物から遠く離れてしまう（2 巡目は y=600362 になって断面が空だった）。
	// パスで作れているなら外接はパスの範囲（-2000〜10000）に収まるので、ここは素直に
	// 使える。**膨らんでいたら気付けるように、パスの y 範囲と突き合わせて出す。**
	const ProbeBox axisBox = ProbeBoundsOf(axes[0]);
	if (!axisBox.ok)
	{
		probe.fail("通り芯の外接が読めないので、断面線をどこへ引けばよいか決められない");
		return;
	}
	double cutY = (axisBox.top + axisBox.bottom) * 0.5;
	probe.log("  通り芯 1 の外接 " + ProbeBoxText(axisBox) + " → 真ん中の y = " + ProbeReal(cutY));
	if (cutY < -2000.0 || cutY > 10000.0)
	{
		cutY = 4000.0;
		probe.log("  外接の真ん中がパスの範囲（-2000〜10000）の外にある——外接が水平線の長さで"
				  "膨らんでいる。断面線はパスの真ん中 y = 4000 に引く");
	}
	probe.log("  → 断面線の y = " + ProbeReal(cutY));

	// 断面に何か映るように、切断面の奥（y > cutY）へ押出を 1 つ置く。
	MCObjectHandle extrude = gSDK->CreateExtrude(0.0, 3000.0);
	if (extrude != nil)
	{
		WorldRect box;
		box.left = 0.0;
		box.right = 8000.0;
		box.bottom = cutY + 1000.0;
		box.top = cutY + 3000.0;
		MCObjectHandle rect = gSDK->CreateRectangle(box);
		if (rect != nil)
			gSDK->AddObjectToContainer(rect, extrude);
		if (gSDK->ParentObject(extrude) != design)
			gSDK->AddObjectToContainer(extrude, design);
		gSDK->ResetObject(extrude);
		probe.log("  押出 h=" + ProbeHandleText(extrude) +
				  " 親=" + ProbeHandleText(gSDK->ParentObject(extrude)) + " " +
				  ProbeBoxText(ProbeBoundsOf(extrude)));
	}
	else
	{
		probe.log("  押出を作れなかった（断面が空でも注釈のグリッド線は出るかを見る）");
	}

	// ---------------------------------------------------- 3. シートレイヤと 3 枚
	probe.log("== 3. シートレイヤと断面ビューポート 3 枚 ==");
	MCObjectHandle sheet = gSDK->CreateLayer("i189-sheet", kLayerSheet);
	if (sheet == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}

	// A: 高さ範囲の上端 4000・縮尺 1/100（本命。ここへ書く）
	// B: 高さ範囲の上端 8000・縮尺 1/100（A との差は上端だけ → 符号の位置の基準・Q5）
	// C: 高さ範囲の上端 4000・縮尺 1/50 （A との差は縮尺だけ → 単位・Q3）
	std::vector<ProbeViewport> vps;
	{
		ProbeViewport a;
		a.tag = "A";
		a.endHeight = 4000.0;
		a.scale = 100.0;
		vps.push_back(a);
		ProbeViewport b;
		b.tag = "B";
		b.endHeight = 8000.0;
		b.scale = 100.0;
		vps.push_back(b);
		ProbeViewport c;
		c.tag = "C";
		c.endHeight = 4000.0;
		c.scale = 50.0;
		vps.push_back(c);
	}

	for (size_t i = 0; i < vps.size(); ++i)
	{
		ProbeViewport& v = vps[i];
		// 断面線は y = cutY を x = -2000 〜 10000 に引く。pt3（見る側）は -Y 側。
		v.vp = gSDK->CreateSectionViewport(WorldPt(-2000.0, cutY), WorldPt(10000.0, cutY),
										   WorldPt(4000.0, cutY - 8000.0), 0.0, -500.0, v.endHeight,
										   sheet);
		if (v.vp == nil)
		{
			probe.fail("CreateSectionViewport が nil を返した（" + v.tag + "）");
			return;
		}
		probe.log("  [" + v.tag + "] vp=" + ProbeHandleText(v.vp) +
				  " 上端=" + ProbeReal(v.endHeight) + " 縮尺=1/" + ProbeReal(v.scale));
		// **更新の前に注釈群が取れるか**（Q2 の前半）。
		probe.log("    更新前の注釈群 = " +
				  ProbeHandleText(gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation)));

		// 表示レイヤ・クラスを表示へ、レンダは隠線消去、切断面より奥を表示
		// （Findings/Viewports.md）。
		gSDK->ForEachLayerN(
			[&v, sheet](MCObjectHandle layer)
			{
				if (layer != sheet)
					gSDK->SetViewportLayerVisibility(v.vp, layer, 0 /* 表示 */);
			});
		gSDK->ForEachClass(
			true, [&v](MCObjectHandle cls)
			{ gSDK->SetViewportClassVisibility(v.vp, gSDK->GetObjectInternalIndex(cls), 0); });
		VWViewportObj(v.vp).SetRenderType(renderFinalHiddenLine);
		TVariableBlock beyond;
		beyond = static_cast<Boolean>(1);
		gSDK->SetObjectVariable(v.vp, ovSectionViewportDisplayObjectsBeyondCutPlane, beyond);
		TVariableBlock scale;
		scale = static_cast<Real64>(v.scale);
		gSDK->SetObjectVariable(v.vp, ovViewportScale, scale);

		gSDK->UpdateViewport(v.vp);
		// **ビューポートの外接は「断面に何か映ったか」の目安になる。** 2 巡目は
		// 53mm 角＝空っぽで、そのせいで「グリッド線が出ない」を確かめきれなかった。
		probe.log("    更新後: 縮尺=" + ProbeVarText(v.vp, ovViewportScale) + " 注釈群=" +
				  ProbeHandleText(gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation)) + " " +
				  ProbeBoxText(ProbeBoundsOf(v.vp)) + "（小さすぎるなら断面は空）");
		probe.log("    注釈群の中身:");
		ProbeWalk(probe, gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation), 0, v.annotation);

		// **注釈群がまだ無いなら、1 つ図形を置いて作らせてから更新し直す。**
		// 作りたてのビューポートの注釈群は nil で（Findings/Drawing Labels.md）、
		// VW がグリッド線の個体を入れる先が無いのかもしれない——という筋を潰す。
		if (gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation) == nil)
		{
			WorldRect seedBox;
			seedBox.left = 0.0;
			seedBox.right = 100.0;
			seedBox.bottom = 0.0;
			seedBox.top = 100.0;
			MCObjectHandle seed = gSDK->CreateRectangle(seedBox);
			const bool added = seed != nil && gSDK->AddViewportAnnotationObject(v.vp, seed) != 0;
			probe.log("    注釈群が無いので矩形を 1 つ置いてみた: AddViewportAnnotationObject=" +
					  std::string(added ? "true" : "false") + " 注釈群=" +
					  ProbeHandleText(gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation)));
			gSDK->UpdateViewport(v.vp);
			v.annotation.clear();
			probe.log("    置いた後に更新した注釈群の中身:");
			ProbeWalk(probe, gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation), 0,
					  v.annotation);
		}

		probe.log("    断面群（kViewportGroupSection）の中身:");
		std::vector<ProbeItem> section;
		ProbeWalk(probe, gSDK->GetViewportGroup(v.vp, kViewportGroupSection), 0, section);
	}
}
// ------------------------------------- 4. A の注釈で見つかった PIO を洗い出す
probe.log("== 4. A の注釈にいる PIO ==");
std::vector<ProbeItem> aPios;
for (const ProbeItem& item : vps[0].annotation)
{
	if (item.type == kParametricNode)
		aPios.push_back(item);
}
probe.log("  A の注釈の PIO は " + ProbeWhole(static_cast<long long>(aPios.size())) + " 件");
if (aPios.empty())
{
	probe.fail("A の注釈に PIO が 1 件も無い——グリッド線は注釈群には現れなかった");
	return;
}

// ------------------------------------------ 4. パラメータ表を全件ダンプ（Q3）
probe.log("== 5. A の注釈の PIO[0] のパラメータ表（全件） ==");
probe.log("  PIO=" + aPios[0].pioName + " 内部ID=" + ProbeWhole(aPios[0].internalId));
const std::vector<ProbeParam> params = ProbeDumpParams(probe, aPios[0].h, true);

probe.log("== 6. 「水平線の長さ」らしいパラメータを選ぶ ==");
const std::vector<ProbeParam> candidates = ProbePickCandidates(probe, params);
for (const ProbeParam& p : candidates)
{
	probe.log("  候補: [" + ProbeWhole(static_cast<long long>(p.index)) + "] " + p.name +
			  " / loc=" + p.loc + " / 欄型=" + ProbeWhole(p.fieldStyle) + " / 値=" + p.value +
			  " / スタイル由来=" +
			  ProbeWhole(static_cast<long long>(
				  gSDK->GetPluginStyleParameterType(aPios[0].h, TXString(p.name.c_str())))));
}
if (candidates.empty())
{
	probe.fail("「水平線の長さ」に当たるパラメータを選べなかった（上の全件表から選び直す）");
	return;
}

// --------------------------------- 6. 書いてみる（A と C の同じ位置の個体へ）
// 対照のため PIO[0] には何も書かない。PIO[1] に候補 0、PIO[2] に候補 1 を書く。
probe.log("== 7. 候補を書いて読み戻す（A と C。PIO[0] は対照で触らない） ==");
const size_t kWriteTargets[2] = {1, 2};
for (size_t vi = 0; vi < vps.size(); ++vi)
{
	if (vps[vi].tag == "B")
		continue; // B は Q5 用の対照——何も書かない
	ProbeViewport& v = vps[vi];
	std::vector<ProbeItem> pios;
	for (const ProbeItem& item : v.annotation)
	{
		if (item.type == kParametricNode)
			pios.push_back(item);
	}
	probe.log("  [" + v.tag + "] 注釈の PIO は " + ProbeWhole(static_cast<long long>(pios.size())) +
			  " 件");
	for (size_t ci = 0; ci < candidates.size() && ci < 2; ++ci)
	{
		const size_t target = kWriteTargets[ci];
		if (target >= pios.size())
		{
			probe.log("    PIO[" + ProbeWhole(static_cast<long long>(target)) +
					  "] が無いので候補 " + ProbeWhole(static_cast<long long>(ci)) + " は書けない");
			continue;
		}
		MCObjectHandle h = pios[target].h;
		const TXString univ(candidates[ci].name.c_str());
		VWParametricObj pio(h);
		const double before = pio.GetParamReal(univ);
		const ProbeBox boxBefore = ProbeBoundsOf(h);
		const double want = before + 10.0;
		pio.SetParamReal(univ, want);
		const double afterWrite = pio.GetParamReal(univ);
		const ProbeBox boxAfterWrite = ProbeBoundsOf(h);
		const bool reset = gSDK->ResetObject(h) != 0;
		const double afterReset = VWParametricObj(h).GetParamReal(univ);
		const ProbeBox boxAfterReset = ProbeBoundsOf(h);
		probe.log("    [" + v.tag + "] PIO[" + ProbeWhole(static_cast<long long>(target)) + "] " +
				  candidates[ci].name + ": 前=" + ProbeReal(before) + " 書いた=" + ProbeReal(want) +
				  " 直後=" + ProbeReal(afterWrite) + " ResetObject=" + (reset ? "true" : "false") +
				  " 後=" + ProbeReal(afterReset));
		probe.log("      外接 前: " + ProbeBoxText(boxBefore));
		probe.log("      外接 書いた直後: " + ProbeBoxText(boxAfterWrite));
		probe.log("      外接 Reset 後: " + ProbeBoxText(boxAfterReset));
		if (boxBefore.ok && boxAfterReset.ok)
			probe.log("      外接の動き: 上端 Δ=" + ProbeReal(boxAfterReset.top - boxBefore.top) +
					  " 下端 Δ=" + ProbeReal(boxAfterReset.bottom - boxBefore.bottom) +
					  " 左 Δ=" + ProbeReal(boxAfterReset.left - boxBefore.left) +
					  " 右 Δ=" + ProbeReal(boxAfterReset.right - boxBefore.right));
	}
}

// ---------------------------- 7. 更新で保たれるか・作り直されるか（Q2 / Q4）
probe.log("== 8. A を更新して、書いた値とハンドルが残るか ==");
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
	probe.log("  更新後の注釈群の中身:");
	ProbeWalk(probe, gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation), 0, after);
	std::vector<MCObjectHandle> afterHandles;
	for (const ProbeItem& item : after)
	{
		if (item.type == kParametricNode)
			afterHandles.push_back(item.h);
	}
	probe.log("  PIO の件数 更新前=" + ProbeWhole(static_cast<long long>(beforeHandles.size())) +
			  " 更新後=" + ProbeWhole(static_cast<long long>(afterHandles.size())));
	bool same = beforeHandles.size() == afterHandles.size();
	for (size_t i = 0; same && i < beforeHandles.size(); ++i)
		same = beforeHandles[i] == afterHandles[i];
	probe.log("  ハンドルは " +
			  std::string(same ? "同一（作り直されていない）" : "違う（作り直された）"));
	for (size_t ci = 0; ci < candidates.size() && ci < 2; ++ci)
	{
		const size_t target = kWriteTargets[ci];
		if (target >= afterHandles.size())
			continue;
		const TXString univ(candidates[ci].name.c_str());
		probe.log("  更新後の値 PIO[" + ProbeWhole(static_cast<long long>(target)) + "] " +
				  candidates[ci].name + " = " +
				  ProbeReal(VWParametricObj(afterHandles[target]).GetParamReal(univ)));
	}

	// 1053（注釈だけ変えたときの更新）を立ててもう一度更新する。
	TVariableBlock only;
	only = static_cast<Boolean>(1);
	const bool wrote1053 =
		gSDK->SetObjectVariable(v.vp, ovViewportResetForOnlyAnnotationsChange, only) != 0;
	gSDK->UpdateViewport(v.vp);
	std::vector<ProbeItem> after2;
	ProbeWalk(probe, gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation), 0, after2);
	std::vector<MCObjectHandle> after2Handles;
	for (const ProbeItem& item : after2)
	{
		if (item.type == kParametricNode)
			after2Handles.push_back(item.h);
	}
	probe.log(
		"  1053 を書いて（" + std::string(wrote1053 ? "true" : "false") +
		"）もう一度更新: PIO の件数=" + ProbeWhole(static_cast<long long>(after2Handles.size())));
	for (size_t ci = 0; ci < candidates.size() && ci < 2; ++ci)
	{
		const size_t target = kWriteTargets[ci];
		if (target >= after2Handles.size())
			continue;
		const TXString univ(candidates[ci].name.c_str());
		probe.log("  1053 後の値 PIO[" + ProbeWhole(static_cast<long long>(target)) + "] " +
				  candidates[ci].name + " = " +
				  ProbeReal(VWParametricObj(after2Handles[target]).GetParamReal(univ)));
	}
}

// -------------------- 8. 符号の位置の基準（A と B。上端だけが違う）（Q5）
probe.log("== 9. 高さ範囲の上端を上げると符号は動くか（A: 上端 4000 / B: 上端 8000） ==");
for (size_t vi = 0; vi < vps.size(); ++vi)
{
	const ProbeViewport& v = vps[vi];
	probe.log("  [" + v.tag + "] 上端=" + ProbeReal(v.endHeight) + " 縮尺=1/" + ProbeReal(v.scale) +
			  " 注釈群=" + ProbeHandleText(gSDK->GetViewportGroup(v.vp, kViewportGroupAnnotation)));
	size_t n = 0;
	for (const ProbeItem& item : v.annotation)
	{
		if (item.type != kParametricNode)
			continue;
		probe.log("    PIO[" + ProbeWhole(static_cast<long long>(n)) + "] " + item.pioName + " " +
				  ProbeBoxText(ProbeBoundsOf(item.h)));
		++n;
	}
	probe.log("    ビューポート自身の外接: " + ProbeBoxText(ProbeBoundsOf(v.vp)));
}

probe.log("== おわり ==");
}
