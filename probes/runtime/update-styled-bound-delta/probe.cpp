//
//	probes/runtime/update-styled-bound-delta/probe.cpp
//
//	[issue #125] `ResetObject` を 1 度も通していない材に `UpdateStyledObjects` を流すと、
//	バウンドと実体がずれる——その機構を確定させ、後から救えるのかを確かめる。
//
//	## 何を見て立った問いか（#118 / PR #119 の実機実測。VW 2026 / mac）
//
//	「バウンドだけ by-instance、他は by-style」のスタイル（元にした材のバウンドは
//	2500 / 5500 ＝ **span 3000**）を、本ごとに違うバウンドを書いた水平材 6 本へ当てた。
//	**1 本ごとに `ResetObject` を通した R1〜R3 は最後まで正しく、`ResetObject` を
//	省いて `UpdateStyledObjects` だけを流した U1〜U3 は終端だけがずれた**
//	（始端＝挿入点は正しい）:
//
//	    U1: 書いた 3000/3000（span 0）    → 期待 Δz 0    ／ 実測 Δz **−3000**
//	    U2: 書いた 3200/4000（span 800）  → 期待 Δz 800  ／ 実測 Δz **−2200**
//	    U3: 書いた 1000/6500（span 5500） → 期待 Δz 5500 ／ 実測 Δz **+2500**
//
//	3 本とも `実測 Δz ＝ (自分のバウンドの span) − (スタイルのバウンドの span)` が
//	ぴったり成り立つ。**レコードも解決結果も書いたとおり**なので、狂っているのは実体だけ。
//
//	## このプローブが取りに行くもの（issue #125 の 1〜4）
//
//	  1. **後から `ResetObject` を通せば直るか**（直らなければ、一度この状態にした材は
//	     救えないことになる）。→ E 群
//	  2. **ずれ幅はスタイルの span に追随するか**（＝上の式が機構として正しいか）。
//	     span 2000 のスタイルを 2 本目に用意して同じことをする。→ D 群
//	  3. **バウンドが by-style のときはどうか。** [#81](../../../Findings/Parametric%20Objects.md)
//	     では `UpdateStyledObjects` だけで 5 本とも正しく建っているが、**そのときは
//	     スタイルの span と自分の span が同じだったので式でも差が 0 になり区別が付かない**。
//	     **書く span をスタイルと違えて（0 と 5500）測り直す**。→ G 群
//	  4. **`UpdateStyledObjects` を 2 度流すとどうなるか**（累積か収束か）。→ F 群
//
//	  併せて、**実務でいちばん踏みやすい形**も測る——「一度 `ResetObject` で正しく建てた本の
//	  バウンドを書き換えて `UpdateStyledObjects` だけを流す」。ここが差分で動いているなら、
//	  **作り直し済みの材でも書き換えのたびに狂う**ことになる。→ H 群
//
//	## 群ごとにスタイルを分けてある（ここを崩すと測れない）
//
//	**`UpdateStyledObjects(ref)` はそのスタイルを当てた全オブジェクトを舐める**ので、
//	1 本のスタイルを群で共有すると、後の群の `UpdateStyledObjects` が前の群の材を
//	もう一度作り直してしまう。**`UpdateStyledObjects` を呼ぶ群にはそれぞれ別のスタイルを
//	立てる**（B/C 群だけは #118 の形をそのまま再現するため 1 本を共有する）。
//
//	## 読み方
//
//	各本の最後の行に「→」で判定を出す。物差しは **`ResetObject` / `UpdateStyledObjects`
//	の後のパスの Z 差（Δz）**で、次のどれと一致したかを機械的に並べる:
//
//	  * **解決バウンドどおり** … `GetObjectBoundElevation` の差と一致＝正しく作り直している
//	  * **差分仮説どおり** … 「自分の span − スタイルの span」と一致＝#118 の読みが正しい
//	  * **どの説明にも当たらない** … 数字をそのまま読む（新しい説明が要る）
//
//	新規の空図面で走らせる前提。undo イベントは開かない。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// -----------------------------------------------------------------------
	// 形の定数。**新規の空図面（レイヤ高さ 0）では `LayerElevation` の offset が
	// そのまま解決Zになる**（#81 / #109 / #112 / #118 と同じ置き方）。
	const double kPlanar = 2000; // 水平材の平面上の長さ（Z 差 0 のパスで作る）

	// スタイルが持つバウンド。**span を 2 通り用意する**のがこの調査の主軸
	// （ずれ幅がスタイルの span に追随するかを見るため）。
	const double kStyleLow = 2500;
	const double kStyleHighWide = 5500;	  // span 3000（#118 と同じスタイル）
	const double kStyleHighNarrow = 4500; // span 2000（機構の検証用）

	// 一致と見なす窓。作り直しは 1〜2 ULP の残差を残す（#67 / #71）ので 1e-6 で見る。
	const double kEpsilon = 1e-6;

	// スタイル側とインスタンス側で見分けの付く値。**スタイルが本当に効いているか**を
	// 目視なしで確かめるために置く（バウンドだけが自由になっているのかの対照）。
	const char* const kStyleMemberID = "STYLE-GAWA";
	const char* const kInstanceMemberID = "HON-GOTO";
	const double kStyleBreadth = 300;
	const double kInstanceBreadth = 120;

	// **バウンドを握っているパラメータ名。** #118 の掃引で、構造材の 181 件を両方向に
	// 総当たりして動いたのはこの 1 件だけだった（始端・終端の両方をこれが握る）。
	const char* const kBoundParam = "DialogStartElevationReference";

	// 本ごとに書くバウンド（#118 の U1〜U3 と同じ値。span は 0 / 800 / 5500）。
	const double kBarLow[3] = {3000, 3200, 1000};
	const double kBarHigh[3] = {3000, 4000, 6500};

	std::string Num(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.17g", value);
		return std::string(buffer);
	}

	std::string Count(long long value)
	{
		return std::to_string(value);
	}

	bool Near(double a, double b)
	{
		return std::fabs(a - b) < kEpsilon;
	}

	std::string YesNo(bool value)
	{
		return value ? "true" : "false";
	}

	std::string Str(const TXString& value)
	{
		return std::string(static_cast<const char*>(value));
	}

	TXString Tx(const std::string& value)
	{
		return TXString(value.c_str());
	}

	// 由来の種別を読める形に。**`EPluginStyleParameter` は `short` の typedef**
	// （`MiniCadCallBacks.h:1338`）なので、知らない値もそのまま数字で出す。
	std::string DescribeStyleType(EPluginStyleParameter type)
	{
		switch (type)
		{
		case kPluginStyleParameter_ByInstance:
			return "by-instance(0)";
		case kPluginStyleParameter_ByStyle:
			return "by-style(1)";
		case kPluginStyleParameter_AllwaysByInstance:
			return "always-by-instance(2)";
		case kPluginStyleParameter_ByCatalog:
			return "by-catalog(3)";
		case kPluginStyleParameter_ByMixed:
			return "by-mixed(4)";
		default:
			return "不明(" + Count(static_cast<long long>(type)) + ")";
		}
	}

	// 構造材のパラメータ名（A 群で 1 度だけ引く。由来表を数えるのに使う）。
	std::vector<std::string> gParamNames;

	// 由来表を全件読み、種別ごとに数える。
	std::string TallyMap(MCObjectHandle target)
	{
		long long counts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		long long other = 0;
		for (size_t i = 0; i < gParamNames.size(); ++i)
		{
			const EPluginStyleParameter type =
				gSDK->GetPluginStyleParameterType(target, Tx(gParamNames[i]));
			if (type >= 0 && type < 8)
				++counts[type];
			else
				++other;
		}
		std::string text;
		for (int type = 0; type < 8; ++type)
		{
			if (counts[type] == 0)
				continue;
			if (!text.empty())
				text += " ／ ";
			text += DescribeStyleType(static_cast<EPluginStyleParameter>(type)) + ":" +
					Count(counts[type]);
		}
		if (other != 0)
			text += (text.empty() ? "" : " ／ ") + std::string("範囲外:") + Count(other);
		return text.empty() ? "（1 件も読めなかった）" : text;
	}

	// -----------------------------------------------------------------------
	// 読み戻し。**パスの Z 差（実体）とバウンドのレコード・解決結果**をひと揃いで取る。
	struct Snap
	{
		bool pathOk = false;
		double dz = 0;
		double insertZ = 0;
		long long boundCount = 0;
		double recOffset[2] = {0, 0};
		double resolved[2] = {0, 0};
		RefNumber styleRef = 0;
		std::string memberID;
		double breadth = 0;
	};

	Snap Read(MCObjectHandle pio)
	{
		Snap snap;
		if (pio == nullptr)
			return snap;

		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path != nullptr)
		{
			WorldPt3 p0, p1;
			if (gSDK->NurbsGetPt3D(path, 0, 0, p0) && gSDK->NurbsGetPt3D(path, 0, 1, p1))
			{
				snap.pathOk = true;
				snap.dz = p1.z - p0.z;
			}
		}

		TransformMatrix matrix;
		gSDK->GetEntityMatrix(pio, matrix);
		snap.insertZ = matrix.P().z;

		snap.boundCount = static_cast<long long>(gSDK->GetObjectStoryBoundsCount(pio));
		for (int id = 0; id < 2; ++id)
		{
			const MockUp::TObjectBoundID boundID = static_cast<MockUp::TObjectBoundID>(id);
			MockUp::SStoryObjectData data;
			data.fBound = MockUp::eStoryObjectBound_LayerElevation;
			data.fBoundStory = 0;
			data.fLayerLevelType = "";
			data.fOffset = 0;
			if (gSDK->GetObjectStoryBound(pio, boundID, data))
				snap.recOffset[id] = data.fOffset;
			snap.resolved[id] = gSDK->GetObjectBoundElevation(pio, boundID);
		}

		gSDK->GetPluginObjectStyle(pio, snap.styleRef);

		VWParametricObj obj(pio);
		snap.memberID = Str(obj.GetParamValue("MemberID"));
		snap.breadth = obj.GetParamReal("MajorBreadth");
		return snap;
	}

	// -----------------------------------------------------------------------
	// 作る。

	bool WriteLayerElevationBound(MCObjectHandle pio, short id, double offset)
	{
		MockUp::SStoryObjectData data;
		data.fBound = MockUp::eStoryObjectBound_LayerElevation;
		data.fBoundStory = 0;
		data.fLayerLevelType = "";
		data.fOffset = offset;
		return gSDK->SetObjectStoryBound(pio, static_cast<MockUp::TObjectBoundID>(id), data);
	}

	// 2 点の NURBS 曲線。**座標を明示的に入れ直す**（#67 / #74 / #75 と同じ作法）。
	MCObjectHandle MakePath(double dx, double dy, double dz)
	{
		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(0, 0, 0), false, 1);
		if (curve == nullptr)
			return nullptr;
		gSDK->Add3DVertex(curve, WorldPt3(dx, dy, dz), true);
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, WorldPt3(0, 0, 0));
			gSDK->NurbsSetPt3D(curve, 0, 1, WorldPt3(dx, dy, dz));
		}
		return curve;
	}

	// **PIO の定義を先に作って「オブジェクトの設定」ダイアログを抑止する。**
	// これを呼ばないと、構造材が未定義の文書では最初の 1 個でダイアログが出て止まる
	// （#118 の 2 度目の実行で実際に踏んだ。`probes/runtime/README.md`）。
	void DefineStructuralMember(vwprobe::Report& probe)
	{
		MCObjectHandle definition =
			gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
		probe.log(std::string("`DefineCustomObject(\"StructuralMember\", "
							  "kCustomObjectPrefNever)`: ") +
				  (definition != nullptr ? "定義のハンドルが返った"
										 : "**nil が返った**（既に定義済みの文書かもしれない）"));
	}

	// `CreateBare` が nil を返した直近の理由（失敗メッセージへ乗せる）。
	std::string gCreateFailure;

	MCObjectHandle CreateBare(double dx, double dy, double dz)
	{
		gCreateFailure.clear();
		MCObjectHandle curve = MakePath(dx, dy, dz);
		if (curve == nullptr)
		{
			gCreateFailure = "`CreateNurbsCurve` が nil を返した";
			return nullptr;
		}
		MCObjectHandle noProfile = nullptr;
		MCObjectHandle pio =
			gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile, true);
		if (pio == nullptr)
			gCreateFailure = "`CreateCustomObjectPath(\"StructuralMember\", …)` が nil を返した"
							 "（「オブジェクトの設定」ダイアログが出てキャンセルされた可能性）";
		return pio;
	}

	// -----------------------------------------------------------------------
	// プラグインスタイル 1 本（#98 で確かめた手順。**`CreatePluginStyle` は呼んではいけない**
	// ——文書の PIO が全滅する）。
	//
	// **`Style` と名乗らない。** macOS の `MacTypes.h` が `typedef unsigned char Style;` を
	// グローバルへ出しているため（`probes/runtime/README.md`）。
	struct ProbeStyle
	{
		RefNumber ref = 0;
		MCObjectHandle symDef = nullptr;
		double span = 0; // スタイルが持つバウンドの span（判定に使う）
	};

	ProbeStyle MakeStyle(vwprobe::Report& probe, const std::string& label, double styleHigh,
						 bool boundByInstance)
	{
		ProbeStyle style;
		style.span = styleHigh - kStyleLow;

		MCObjectHandle seed = CreateBare(0, 0, 0);
		if (seed == nullptr)
		{
			probe.fail("A: " + label + " の元にする部材を作れなかった——" + gCreateFailure);
			return style;
		}
		WriteLayerElevationBound(seed, 0, kStyleLow);
		WriteLayerElevationBound(seed, 1, styleHigh);
		VWParametricObj seedObj(seed);
		seedObj.SetParamString("MemberID", kStyleMemberID);
		seedObj.SetParamReal("MajorBreadth", kStyleBreadth);
		gSDK->ResetObject(seed);

		// **名前がぶつかると `CreateSymbolDefinition` は nil を返す**（同じ図面で 2 度
		// 走らせたとき）。空きが見つかるまで番号を振る。
		MCObjectHandle symDef = nullptr;
		TXString used;
		for (int attempt = 1; attempt <= 60 && symDef == nullptr; ++attempt)
		{
			TXString name(("試験スタイル #125 " + label + " " + Count(attempt)).c_str());
			symDef = gSDK->CreateSymbolDefinition(name);
			if (symDef != nullptr)
				used = name;
		}
		if (symDef == nullptr)
		{
			probe.fail("A: " + label + " のシンボル定義を作れなかった（名前が尽きた）");
			return style;
		}

		const bool added = gSDK->AddObjectToContainer(seed, symDef);
		gSDK->ResetObject(symDef); // **サブタイプを書く前に通す**（#98）
		const Sint32 internalID = static_cast<Sint32>(VWParametricObj::GetInternalID(seed));
		gSDK->SetSymbolDefSubType(symDef, internalID);

		// **由来表を組む。** 全件 by-style にしてから、バウンドを握る 1 件だけを
		// by-instance へ裏返す（#118 で確定した作り方）。`boundByInstance=false` の群は
		// 「バウンドも by-style」——G 群の対照になる。
		gSDK->SetAllPluginStyleParameters(symDef, kPluginStyleParameter_ByStyle);
		if (boundByInstance)
			gSDK->SetPluginStyleParameterType(symDef, Tx(kBoundParam),
											  kPluginStyleParameter_ByInstance);

		const bool isStyle = gSDK->IsPluginStyle(symDef);
		const RefNumber ref = static_cast<RefNumber>(gSDK->GetObjectInternalIndex(symDef));
		probe.log("  " + label + ": 名前=\"" + Str(used) + "\" ／ 中へ入れた=" + YesNo(added) +
				  " ／ IsPluginStyle=" + YesNo(isStyle) +
				  " ／ ref=" + Count(static_cast<long long>(ref)) + " ／ バウンド " +
				  Num(kStyleLow) + "/" + Num(styleHigh) + "（span " + Num(style.span) + "）");
		probe.log("    バウンドの由来: " +
				  DescribeStyleType(gSDK->GetPluginStyleParameterType(symDef, Tx(kBoundParam))) +
				  "（`" + kBoundParam + "`） ／ 由来表の全件: " + TallyMap(symDef));
		if (!isStyle || ref == 0)
		{
			probe.fail("A: " + label + " をプラグインスタイルにできなかった");
			return style;
		}
		style.ref = ref;
		style.symDef = symDef;
		return style;
	}

	// -----------------------------------------------------------------------
	// 測る本 1 つぶん。
	struct ProbeBar
	{
		MCObjectHandle pio = nullptr;
		std::string label;
		double low = 0;	 // いま書いてあるバウンド（下端）
		double high = 0; // いま書いてあるバウンド（上端）
		double lastDz = 0;
		bool hasLastDz = false;
	};

	ProbeBar MakeBar(vwprobe::Report& probe, const std::string& where, const std::string& label,
					 double low, double high, RefNumber styleRef)
	{
		ProbeBar bar;
		bar.label = label;
		bar.low = low;
		bar.high = high;

		MCObjectHandle pio = CreateBare(kPlanar, 0, 0);
		if (pio == nullptr)
		{
			probe.fail(where + ": " + label + " を作れなかった——" + gCreateFailure);
			return bar;
		}
		WriteLayerElevationBound(pio, 0, low);
		WriteLayerElevationBound(pio, 1, high);
		VWParametricObj obj(pio);
		obj.SetParamString("MemberID", kInstanceMemberID);
		obj.SetParamReal("MajorBreadth", kInstanceBreadth);
		gSDK->SetPluginObjectStyle(pio, styleRef);
		bar.pio = pio;
		return bar;
	}

	// **この調査の判定。** 実測の Δz が、考えられる説明のどれと一致したかを並べる。
	// 数字も必ず添えるので、どれにも当たらなくても読み手が自分で当てられる。
	std::string Match(double dz, double resolvedSpan, double writtenSpan, double styleSpan)
	{
		std::string text;
		const bool writtenDiffers = !Near(writtenSpan, resolvedSpan);
		if (Near(dz, resolvedSpan))
			text += "**解決バウンドどおり（正しい）**";
		if (writtenDiffers && Near(dz, writtenSpan))
			text += (text.empty() ? "" : " ＋ ") + std::string("**書いた span どおり**");
		if (Near(dz, resolvedSpan - styleSpan))
			text += (text.empty() ? "" : " ＋ ") +
					std::string("**差分仮説どおり（解決 span − スタイル span）**");
		if (writtenDiffers && Near(dz, writtenSpan - styleSpan))
			text += (text.empty() ? "" : " ＋ ") +
					std::string("**差分仮説どおり（書いた span − スタイル span）**");
		if (text.empty())
			text = "**どの説明にも当たらない**";
		return text;
	}

	// 1 行だけ（作り直す前の状態など、判定の要らない場面用）。
	void LogBrief(vwprobe::Report& probe, ProbeBar& bar, const std::string& when)
	{
		if (bar.pio == nullptr)
			return;
		const Snap snap = Read(bar.pio);
		bar.lastDz = snap.dz;
		bar.hasLastDz = true;
		probe.log("    " + bar.label + " " + when + ": Δz=" + Num(snap.dz) +
				  " ／ 挿入点Z=" + Num(snap.insertZ) +
				  " ／ レコード offset=" + Num(snap.recOffset[0]) + "/" + Num(snap.recOffset[1]) +
				  " ／ 解決=" + Num(snap.resolved[0]) + "/" + Num(snap.resolved[1]));
	}

	// 2 行（測った値 ＋ 判定）。
	void LogFull(vwprobe::Report& probe, ProbeBar& bar, double styleSpan, const std::string& when)
	{
		if (bar.pio == nullptr)
			return;
		const Snap snap = Read(bar.pio);
		const double written = bar.high - bar.low;
		const double resolvedSpan = snap.resolved[1] - snap.resolved[0];
		probe.log("    " + bar.label + " " + when + ": Δz=" + Num(snap.dz) +
				  " ／ 挿入点Z=" + Num(snap.insertZ) +
				  " ／ **終端の絶対Z=" + Num(snap.insertZ + snap.dz) + "**（解決バウンドの終端は " +
				  Num(snap.resolved[1]) + "） ／ 件数=" + Count(snap.boundCount) +
				  " ／ レコード offset=" + Num(snap.recOffset[0]) + "/" + Num(snap.recOffset[1]) +
				  " ／ 解決=" + Num(snap.resolved[0]) + "/" + Num(snap.resolved[1]) +
				  " ／ MemberID=\"" + snap.memberID + "\"（スタイル側=" +
				  (snap.memberID == std::string(kStyleMemberID) ? "はい" : "いいえ") +
				  "） ／ MajorBreadth=" + Num(snap.breadth) +
				  "（スタイル側=" + (Near(snap.breadth, kStyleBreadth) ? "はい" : "いいえ") + "）");
		probe.log("      → 書いた span=" + Num(written) + " ／ 解決 span=" + Num(resolvedSpan) +
				  " ／ スタイル span=" + Num(styleSpan) + " ／ 差分仮説の予測=" +
				  Num(resolvedSpan - styleSpan) + " ／ 実測 Δz=" + Num(snap.dz) + " → " +
				  Match(snap.dz, resolvedSpan, written, styleSpan));
		bar.lastDz = snap.dz;
		bar.hasLastDz = true;
	}

	std::string BarName(const char* prefix, int index)
	{
		return std::string(prefix) + Count(index + 1);
	}

} // namespace

VW_PROBE("update-styled-bound-delta",
		 "UpdateStyledObjects だけで作り直すと終端が「スタイルの span」だけずれるのか",
		 "ResetObject を通していない材へ UpdateStyledObjects を流したときのずれが、"
		 "スタイルのバウンドの span に追随するのかを 2 本のスタイルで測り、"
		 "後から ResetObject を通せば直るか・2 度流すと累積するか・"
		 "バウンドが by-style のときはどうかまで確かめる。新規の空図面で走らせること")
{
	// =======================================================================
	probe.log("=== この図面について ===");
	{
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		TXString layerName;
		if (currentLayer != nullptr)
			gSDK->GetObjectName(currentLayer, layerName);
		probe.log(std::string("いまのレイヤ: \"") + Str(layerName) + "\"");
		probe.log("物差し: **水平材**（平面長 " + Num(kPlanar) +
				  " ／ 作るときのパスの Z 差は 0）に両端の offset を書き、"
				  "スタイルを当ててから作り直し、**パスの Z 差（Δz）**を読む");
		probe.log("  正しく作り直されていれば Δz ＝ 解決バウンドの span。"
				  "#118 の読み（差分仮説）が正しければ Δz ＝ 自分の span − スタイルの span");
		DefineStructuralMember(probe);
	}

	// =======================================================================
	probe.log("=== A. 下ごしらえ（パラメータ表とスタイル 5 本） ===");
	{
		MCObjectHandle sample = CreateBare(kPlanar, 0, 0);
		if (sample == nullptr)
		{
			probe.fail("A: 列挙用の部材を作れなかった——" + gCreateFailure);
			return;
		}
		VWParametricObj sampleObj(sample);
		const size_t count = sampleObj.GetParamsCount();
		bool found = false;
		for (size_t i = 0; i < count; ++i)
		{
			const std::string name = Str(sampleObj.GetParamName(i));
			if (name.empty())
				continue;
			gParamNames.push_back(name);
			if (name == std::string(kBoundParam))
				found = true;
		}
		probe.log("  `GetParamsCount` = " + Count(static_cast<long long>(count)) +
				  " ／ 名前を引けたもの=" + Count(static_cast<long long>(gParamNames.size())) +
				  " 件 ／ バウンドを握る `" + kBoundParam + "` は表にあるか: " + YesNo(found));
		if (!found)
			probe.fail(std::string("A: `") + kBoundParam +
					   "` がパラメータ表に無い（#118 の作り方が使えないので、"
					   "以降の by-instance 群は成立しない）");
	}

	// **群ごとにスタイルを分ける**（`UpdateStyledObjects` は当てた全件を舐めるため）。
	const ProbeStyle wide = MakeStyle(probe, "span3000 B/C 群", kStyleHighWide, true);
	const ProbeStyle narrow = MakeStyle(probe, "span2000 D 群", kStyleHighNarrow, true);
	const ProbeStyle twice = MakeStyle(probe, "span3000 F 群", kStyleHighWide, true);
	const ProbeStyle byStyle =
		MakeStyle(probe, "span3000 G 群・バウンドも by-style", kStyleHighWide, false);
	const ProbeStyle rewrite = MakeStyle(probe, "span3000 H 群", kStyleHighWide, true);
	if (wide.ref == 0 || narrow.ref == 0 || twice.ref == 0 || byStyle.ref == 0 || rewrite.ref == 0)
	{
		probe.fail("A: スタイルを 5 本そろえられなかった（以降の群は成立しない）");
		return;
	}

	// =======================================================================
	probe.log("=== B/C. #118 の再現（スタイル span 3000・バウンドは by-instance） ===");
	std::vector<ProbeBar> wideBars;
	{
		probe.log("  R1〜R3 は 1 本ごとに `ResetObject` を通し、U1〜U3 は通さない。"
				  "最後に `UpdateStyledObjects` を 1 回だけ流す（#118 と同じ手順）");
		for (int i = 0; i < 3; ++i)
			wideBars.push_back(
				MakeBar(probe, "B/C", BarName("R", i), kBarLow[i], kBarHigh[i], wide.ref));
		for (int i = 0; i < 3; ++i)
			wideBars.push_back(
				MakeBar(probe, "B/C", BarName("U", i), kBarLow[i], kBarHigh[i], wide.ref));
		for (size_t i = 0; i < wideBars.size(); ++i)
		{
			if (wideBars[i].pio != nullptr && wideBars[i].label[0] == 'R')
				gSDK->ResetObject(wideBars[i].pio);
		}
		for (size_t i = 0; i < wideBars.size(); ++i)
			LogBrief(probe, wideBars[i], "（Update 前）");

		gSDK->UpdateStyledObjects(wide.ref);
		probe.log("  `UpdateStyledObjects` を 1 回流した後:");
		for (size_t i = 0; i < wideBars.size(); ++i)
			LogFull(probe, wideBars[i], wide.span, "（Update 後）");
		probe.log("  → 判定: R が「正しい」・U が「差分仮説どおり」なら **#118 の実測が再現した**。"
				  "U も「正しい」なら #118 は再現しない（条件が別にある）");
	}

	// =======================================================================
	probe.log("=== D. 機構の検証（スタイルだけを span 2000 に替えて同じことをする） ===");
	{
		probe.log("  ずれ幅が **3000 から 2000 へ追随すれば**、"
				  "`Δz ＝ 自分の span − スタイルの span` が機構として確定する");
		std::vector<ProbeBar> narrowBars;
		narrowBars.push_back(MakeBar(probe, "D", "N-R3", kBarLow[2], kBarHigh[2], narrow.ref));
		for (int i = 0; i < 3; ++i)
			narrowBars.push_back(
				MakeBar(probe, "D", BarName("N-U", i), kBarLow[i], kBarHigh[i], narrow.ref));
		for (size_t i = 0; i < narrowBars.size(); ++i)
		{
			if (narrowBars[i].pio != nullptr && narrowBars[i].label[2] == 'R')
				gSDK->ResetObject(narrowBars[i].pio);
		}
		for (size_t i = 0; i < narrowBars.size(); ++i)
			LogBrief(probe, narrowBars[i], "（Update 前）");

		gSDK->UpdateStyledObjects(narrow.ref);
		probe.log("  `UpdateStyledObjects` を 1 回流した後:");
		for (size_t i = 0; i < narrowBars.size(); ++i)
			LogFull(probe, narrowBars[i], narrow.span, "（Update 後）");
		probe.log("  → 判定: N-U の Δz が「自分の span − **2000**」なら式は確定。"
				  "3000 のままずれていれば、ずれ幅はスタイルの span ではない");
	}

	// =======================================================================
	probe.log("=== E. 救えるか（B/C 群で狂った U1〜U3 へ、後から `ResetObject` を通す） ===");
	{
		for (size_t i = 0; i < wideBars.size(); ++i)
		{
			if (wideBars[i].pio == nullptr || wideBars[i].label[0] != 'U')
				continue;
			gSDK->ResetObject(wideBars[i].pio);
			LogFull(probe, wideBars[i], wide.span, "（後から ResetObject を通した後）");
		}
		probe.log("  → 判定: **3 本とも「正しい」なら救える**（順序の問題で済む）。"
				  "狂ったままなら、一度この状態にした材は `ResetObject` では救えない");
	}

	// =======================================================================
	probe.log("=== F. `UpdateStyledObjects` を 2 度流す（累積か収束か） ===");
	{
		std::vector<ProbeBar> twiceBars;
		twiceBars.push_back(MakeBar(probe, "F", "T-U1", kBarLow[0], kBarHigh[0], twice.ref));
		twiceBars.push_back(MakeBar(probe, "F", "T-U3", kBarLow[2], kBarHigh[2], twice.ref));
		for (size_t i = 0; i < twiceBars.size(); ++i)
			LogBrief(probe, twiceBars[i], "（Update 前）");

		gSDK->UpdateStyledObjects(twice.ref);
		probe.log("  1 回目の後:");
		for (size_t i = 0; i < twiceBars.size(); ++i)
			LogFull(probe, twiceBars[i], twice.span, "（Update 1 回目の後）");

		// 2 回目を流す前に、**累積したときの予測**を出しておく（後から読み合わせられるように）。
		for (size_t i = 0; i < twiceBars.size(); ++i)
		{
			if (twiceBars[i].pio == nullptr || !twiceBars[i].hasLastDz)
				continue;
			const double own = twiceBars[i].high - twiceBars[i].low;
			probe.log("    " + twiceBars[i].label + " の予測: 収束なら Δz=" + Num(own) +
					  "（正しい形） ／ 1 回目のまま動かないなら Δz=" + Num(twiceBars[i].lastDz) +
					  " ／ 累積するなら Δz=" + Num(twiceBars[i].lastDz + (own - twice.span)));
		}
		gSDK->UpdateStyledObjects(twice.ref);
		probe.log("  2 回目の後:");
		for (size_t i = 0; i < twiceBars.size(); ++i)
			LogFull(probe, twiceBars[i], twice.span, "（Update 2 回目の後）");
		probe.log("  → 判定: 上の 3 つの予測のどれに当たったかを読む。"
				  "**累積するなら、流すたびに狂いが増える**");
	}

	// =======================================================================
	probe.log("=== G. バウンドが by-style のときはどうか（#81 では区別が付かなかった） ===");
	{
		probe.log("  このスタイルは**バウンドも by-style**なので、作り直しでレコードが"
				  "スタイルの 2500/5500（span 3000）へ置き換わる（#112）。"
				  "**書く span をスタイルと違えて（0 と 5500）**測るので、"
				  "「解決バウンドから作り直している」と「差分で動かしている」が分かれる");
		std::vector<ProbeBar> styleBars;
		styleBars.push_back(MakeBar(probe, "G", "S-R3", kBarLow[2], kBarHigh[2], byStyle.ref));
		styleBars.push_back(MakeBar(probe, "G", "S-U1", kBarLow[0], kBarHigh[0], byStyle.ref));
		styleBars.push_back(MakeBar(probe, "G", "S-U3", kBarLow[2], kBarHigh[2], byStyle.ref));
		if (styleBars[0].pio != nullptr)
			gSDK->ResetObject(styleBars[0].pio);
		for (size_t i = 0; i < styleBars.size(); ++i)
			LogBrief(probe, styleBars[i], "（Update 前）");

		gSDK->UpdateStyledObjects(byStyle.ref);
		probe.log("  `UpdateStyledObjects` を 1 回流した後:");
		for (size_t i = 0; i < styleBars.size(); ++i)
			LogFull(probe, styleBars[i], byStyle.span, "（Update 後）");
		probe.log("  → 判定: S-U の Δz が **3000（解決バウンドどおり）なら by-style は安全**"
				  "——ずれるのはバウンドが by-instance のときだけ。"
				  "**書いた span − 3000（S-U1 なら −3000・S-U3 なら 2500）なら by-style でも"
				  "同じ機構が効いている**（#81 で見えなかったのは span が同じだったため）");
	}

	// =======================================================================
	probe.log(
		"=== H. 実務で踏みやすい形（作り直し済みの本のバウンドを書き換えて Update だけ） ===");
	{
		probe.log("  一度 `ResetObject` で正しく建ててから**バウンドを書き換え**、"
				  "`UpdateStyledObjects` だけを流す。差分で動いているなら、"
				  "**作り直し済みの材でも書き換えのたびに狂う**ことになる");
		std::vector<ProbeBar> rewriteBars;
		// どちらも最初は 1000/3000（span 2000）で正しく建てる。書き換え先を
		// **広げる本と狭める本**に分けると、考えられる説明がすべて違う数字になる。
		rewriteBars.push_back(MakeBar(probe, "H", "H-広げる", 1000, 3000, rewrite.ref));
		rewriteBars.push_back(MakeBar(probe, "H", "H-狭める", 1000, 3000, rewrite.ref));
		for (size_t i = 0; i < rewriteBars.size(); ++i)
		{
			if (rewriteBars[i].pio != nullptr)
				gSDK->ResetObject(rewriteBars[i].pio);
			LogFull(probe, rewriteBars[i], rewrite.span, "（ResetObject で建てた直後）");
		}

		const double newHigh[2] = {6500, 2000}; // span 5500 へ広げる ／ span 1000 へ狭める
		for (size_t i = 0; i < rewriteBars.size(); ++i)
		{
			if (rewriteBars[i].pio == nullptr)
				continue;
			const double before = rewriteBars[i].lastDz;
			WriteLayerElevationBound(rewriteBars[i].pio, 1, newHigh[i]);
			rewriteBars[i].high = newHigh[i];
			const double own = rewriteBars[i].high - rewriteBars[i].low;
			probe.log("    " + rewriteBars[i].label + " のバウンドを " + Num(rewriteBars[i].low) +
					  "/" + Num(newHigh[i]) + "（span " + Num(own) +
					  "）へ書き換えた ／ 予測: "
					  "正しいなら Δz=" +
					  Num(own) + " ／ 差分が「いまの形」に足されるなら Δz=" +
					  Num(before + (own - rewrite.span)) +
					  " ／ 書き換えが効かないなら Δz=" + Num(before));
		}
		gSDK->UpdateStyledObjects(rewrite.ref);
		probe.log("  `UpdateStyledObjects` を 1 回流した後:");
		for (size_t i = 0; i < rewriteBars.size(); ++i)
			LogFull(probe, rewriteBars[i], rewrite.span, "（書き換え → Update の後）");
		probe.log("  → 判定: 「正しい」なら **`ResetObject` を 1 度通した材は以後 "
				  "`UpdateStyledObjects` だけで書き換えられる**。"
				  "狂うなら、**バウンドを書き換えたら必ずその本を `ResetObject` する**しかない");
	}

	// =======================================================================
	probe.log("=== まとめ ===");
	probe.log("  読む順: B/C（#118 の再現）→ D（ずれ幅はスタイルの span に追随するか）"
			  "→ E（後から救えるか）→ F（2 度流すと累積するか）→ G（by-style ではどうか）"
			  "→ H（書き換え → Update だけで狂うか）");
	probe.log("  答えたいのは 1 つ: **`UpdateStyledObjects` だけで作り直してよいのはどういう"
			  "ときか**（省いてよい条件と、狂ったときに救う手）");
}
