//
//	probes/runtime/style-bound-only-by-instance/probe.cpp
//
//	[issue #118] 「ストーリバウンドだけ by-instance、他のパラメータは by-style」の
//	プラグインスタイルは作れるか。
//
//	## なぜこれを走らせるか
//
//	[#112](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/112) で、
//	**スタイルを当てた PIO のストーリバウンドは、次の `ResetObject` /
//	`UpdateStyledObjects` でスタイルの持つバウンドに置き換わる**と実機で確定した
//	（順序では避けられない）。唯一の逃げ道が
//	`SetAllPluginStyleParameters(hSymDef, kPluginStyleParameter_ByInstance)` で、
//	これなら書いたバウンドが残る。
//
//	ところが**この口は名前のとおり全パラメータを切り替える**ので、現状の逃げ道は
//	「スタイルをほぼ無力化する」ことと引き換えになっている。取り込みが実際にやりたいのは
//	**「断面や材種はスタイルで揃えつつ、高さだけは本ごとに指定する」**——それが作れるかで、
//	実プラグインがスタイルを使えるかどうかが決まる。
//
//	## 先に SDK のソースで潰したこと（実機へ持っていく前に分かったもの）
//
//	**VWFC の参照実装では、由来表（`'PSMP'`）の鍵は「パラメータ名」しかない。**
//	【ソース根拠】`Source/VWSDK/VWFC/Tools/VWStyleSupport.cpp`:
//
//	    bool VWStyleSupport::CreateUnstyledMap( MCObjectHandle hObject ) {
//	        VWParametricObj paramObj( hObject );
//	        size_t numOfFields = paramObj.GetParamsCount();
//	        for (size_t i = 0; i < numOfFields; i++) {
//	            TXString name = paramObj.GetParamName( i );   // ← 鍵はパラメータ名だけ
//	            ... fmapStyleInfo.insert( TStyleInfo_Pair( name, sInfo ) );
//	        }
//	    }
//	    bool VWStyleSupport::SetAllStyleTypes( EPluginStyleParameter styleType ) {
//	        for ( iter = fmapStyleInfo.begin(); iter != fmapStyleInfo.end(); iter++ )
//	            iter->second.styleType = styleType;        // ← 表の全件を舐めるだけ
//	    }
//
//	つまり「全部」とは**パラメータ表の全件**のことで、そこにバウンドの席は無い。
//	**にもかかわらず #112 ではバウンドが by-style / by-instance に従った**——この食い違いが
//	調査の核心で、**ここから先はヘッダでは決められない**（VW 本体側の
//	`SetAllPluginStyleParameters` が VWFC の参照実装と同じとは限らない）。
//
//	## 何をどう切り分けるか
//
//	判定はどの群も同じ「物差し」で行う。**水平材（両端とも offset 2500 で書く）に、
//	バウンド 2500 / 5500 のスタイルを当てて `ResetObject` する**。
//
//	  * 解決バウンドが 2500 / 2500 のまま → **バウンドは自由**（by-instance のふるまい）
//	  * 解決バウンドが 2500 / 5500 になる → **バウンドはスタイルに取られた**
//
//	  A 群 … 下ごしらえ。パラメータ表を全数列挙し、スタイルを 1 本作る。
//	  B 群 … **1 つずつ切り替える口は本当に効くのか。** `SetPluginStyleParameterType` で
//	          書いて `GetPluginStyleParameterType` で読み戻す。`SetAllPluginStyleParameters`
//	          が表の全件に行き渡るのかも、ここで全数確認する（issue の 3 ＝
//	          「バウンドを名指しできる名前があるか」への答えでもある——**全件が揃って
//	          動くなら、バウンドだけ別扱いになっている席は表に無い**）。
//	  C 群 … **名指しで全部 by-instance にすると、バウンドは自由になるか**（issue の 1）。
//	          自由になるなら、バウンドは**名前で届く**。ならないなら、
//	          `SetAllPluginStyleParameters` は名前の表以外の何かも書いている。
//	  D 群 … **逆向き。全部 by-instance にした後、名指しで全部 by-style へ戻す**
//	          （issue の 2）。**バウンドが自由なままなら、それが求めている形そのもの**
//	          ——「他は全部スタイル、バウンドだけ本ごと」。
//	  E 群 … **挟み撃ち（掃引）。** 全部 by-style の状態から**1 つだけ**by-instance にして
//	          181 通り試し、バウンドが自由になる名前を探す。
//	  F 群 … **逆の掃引。** 全部 by-instance の状態から**1 つだけ**by-style へ戻して
//	          181 通り試し、バウンドを取る名前を探す。**D 群が「取られる」だったときは、
//	          ここで見つかった名前だけを by-instance に残せばよい**ことになる。
//	  G 群 … **実用形の総合確認。** D 群（または F 群）で分かった作り方でスタイルを組み、
//	          **①高さは本ごとに別々** ②**断面・材種はスタイルで揃う** ③`ResetObject` でも
//	          `UpdateStyledObjects` でも成り立つ、の 3 つを同時に確かめる。
//	          **②が崩れていたら「スタイルを使えている」とは言えない**ので、
//	          パラメータの値（`MemberID` ＝テキスト、`MajorBreadth` ＝実数）が
//	          スタイル側の値に揃うかまで読む。
//
//	掃引（E / F）は**同じスタイル 1 本を使い回して由来表だけ書き換える**（毎回スタイルを
//	作ると資源が 362 本増えるため）。判定に使う材は毎回作り直す。
//
//	## 読み方
//
//	各群の末尾に「→ 判定:」の 1 行を出す。掃引は**baseline と違う結果になった名前だけ**を
//	並べる（181 行を全部出さない）ので、「（該当なし）」ならその向きでは 1 つも動かない。
//
//	プローブは新規の空図面で走る前提で、undo イベントは開かない。
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
	// そのまま解決Zになる**（#81 / #109 / #112 と同じ置き方）。
	const double kElevLow = 2500; // 対象に書く offset（両端とも）
	const double kElevHigh = 5500; // スタイルが持つ上端 offset（＝書いた値と食い違う）
	const double kPlanar = 2000; // 水平材の平面上の長さ

	// 一致と見なす窓。作り直しは 1〜2 ULP の残差を残す（#67 / #71）ので 1e-6 で見る。
	const double kEpsilon = 1e-6;

	// G 群で「スタイル側の値が流れてくるか」を見るための、見分けの付く値。
	const char* const kStyleMemberID = "STYLE-GAWA";
	const char* const kInstanceMemberID = "HON-GOTO";
	const double kStyleBreadth = 300;
	const double kInstanceBreadth = 120;

	// 掃引で試す名前の上限（安全弁。構造材は 181 件なので通常は掛からない）。
	const size_t kSweepLimit = 400;
	// 掃引の結果を並べるときの上限（ログが膨れないように）。
	const size_t kListLimit = 30;

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

	// -----------------------------------------------------------------------
	// 読み戻し。**バウンドのレコードと解決した絶対Z**をひと揃いで取る（#112 の Snap を
	// この調査に要るぶんだけ残したもの）。
	struct Snap
	{
		bool pathOk = false;
		double dz = 0;
		double insertZ = 0;
		long long boundCount = 0;
		double recOffset[2] = {0, 0};
		double resolved[2] = {0, 0};
		RefNumber styleRef = 0;
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
		return snap;
	}

	// **この調査の物差し。** 書いた 2500 / 2500 が残っていれば「バウンドは自由」。
	bool BoundStayed(const Snap& snap)
	{
		return Near(snap.resolved[0], kElevLow) && Near(snap.resolved[1], kElevLow);
	}

	std::string DescribeSnap(const Snap& snap)
	{
		return "Δz=" + Num(snap.dz) + " 挿入点Z=" + Num(snap.insertZ) +
			   " ／ 件数=" + Count(snap.boundCount) +
			   " ／ レコード offset=" + Num(snap.recOffset[0]) + "/" + Num(snap.recOffset[1]) +
			   " ／ 解決=" + Num(snap.resolved[0]) + "/" + Num(snap.resolved[1]) +
			   " ／ styleRef=" + Count(static_cast<long long>(snap.styleRef));
	}

	std::string Verdict(const Snap& snap)
	{
		return BoundStayed(snap) ? "**バウンドは自由（書いた 2500/2500 のまま）**"
								 : "**バウンドはスタイルに取られた**";
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

	MCObjectHandle CreateBare(double dx, double dy, double dz)
	{
		MCObjectHandle curve = MakePath(dx, dy, dz);
		if (curve == nullptr)
			return nullptr;
		MCObjectHandle noProfile = nullptr;
		return gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile, true);
	}

	// この調査でずっと使う「対象」——**水平材**（平面長 2000・Z 差 0）に、
	// 書きたいバウンド（両端とも 2500）を書いたもの。
	MCObjectHandle CreateTargetWithBounds(vwprobe::Report& probe, const std::string& where,
										  double lowOffset, double highOffset)
	{
		MCObjectHandle pio = CreateBare(kPlanar, 0, 0);
		if (pio == nullptr)
		{
			probe.fail(where + ": 対象を作れなかった");
			return nullptr;
		}
		WriteLayerElevationBound(pio, 0, lowOffset);
		WriteLayerElevationBound(pio, 1, highOffset);
		return pio;
	}

	// 物差しを 1 回引く: 対象を作る → スタイルを当てる → `ResetObject` → 読む。
	Snap MeasureWithReset(vwprobe::Report& probe, const std::string& where, RefNumber styleRef)
	{
		Snap snap;
		MCObjectHandle pio = CreateTargetWithBounds(probe, where, kElevLow, kElevLow);
		if (pio == nullptr)
			return snap;
		gSDK->SetPluginObjectStyle(pio, styleRef);
		gSDK->ResetObject(pio);
		return Read(pio);
	}

	// -----------------------------------------------------------------------
	// A 群: プラグインスタイルを 1 本作る（#98 で確かめた手順。**`CreatePluginStyle` は
	// 呼んではいけない**——文書の PIO が全滅する）。
	//
	// **`Style` と名乗らない。** macOS の `MacTypes.h` が `typedef unsigned char Style;` を
	// グローバルへ出しているので、無名名前空間に入れていても参照が曖昧になる
	// （probes/runtime/README.md「短い名前・ありふれた名前を使わない」）。
	struct ProbeStyle
	{
		RefNumber ref = 0;
		MCObjectHandle symDef = nullptr;
		std::string label;
	};

	ProbeStyle MakeStyle(vwprobe::Report& probe, const std::string& label, bool byInstance,
						 bool distinctiveParams)
	{
		ProbeStyle style;
		style.label = label;

		MCObjectHandle seed = CreateBare(0, 0, 0);
		if (seed == nullptr)
		{
			probe.fail("A: " + label + " の元にする部材を作れなかった");
			return style;
		}
		// 元にする材は**鉛直材で 2500 / 5500**。当てた先の「書いた値（2500/2500）」と
		// 食い違うので、取られたかどうかが一目で分かる。
		WriteLayerElevationBound(seed, 0, kElevLow);
		WriteLayerElevationBound(seed, 1, kElevHigh);
		if (distinctiveParams)
		{
			VWParametricObj seedObj(seed);
			seedObj.SetParamString("MemberID", kStyleMemberID);
			seedObj.SetParamReal("MajorBreadth", kStyleBreadth);
		}
		gSDK->ResetObject(seed);

		// **名前がぶつかると `CreateSymbolDefinition` は nil を返す**（同じ図面で 2 度
		// 走らせたとき）。空きが見つかるまで番号を振る。
		MCObjectHandle symDef = nullptr;
		TXString used;
		for (int attempt = 1; attempt <= 60 && symDef == nullptr; ++attempt)
		{
			TXString name(("試験スタイル #118 " + label + " " + Count(attempt)).c_str());
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
		gSDK->SetAllPluginStyleParameters(symDef, byInstance ? kPluginStyleParameter_ByInstance
															 : kPluginStyleParameter_ByStyle);

		const bool isStyle = gSDK->IsPluginStyle(symDef);
		const RefNumber ref = static_cast<RefNumber>(gSDK->GetObjectInternalIndex(symDef));
		probe.log("  " + label + ": 名前=\"" + Str(used) + "\" ／ 中へ入れた=" + YesNo(added) +
				  " ／ IsPluginStyle=" + YesNo(isStyle) +
				  " ／ ref=" + Count(static_cast<long long>(ref)) +
				  " ／ 既定の由来=" + (byInstance ? "by-instance" : "by-style"));
		if (!isStyle || ref == 0)
		{
			probe.fail("A: " + label + " をプラグインスタイルにできなかった");
			return style;
		}
		style.ref = ref;
		style.symDef = symDef;
		return style;
	}

	// 由来表を「全件 <type>」にしてから、名前を 1 つだけ別の由来にする。
	// `flipIndex` が names の範囲外なら、1 つも裏返さない（baseline 用）。
	void ShapeMap(const ProbeStyle& style, const std::vector<std::string>& names,
				  EPluginStyleParameter base, size_t flipIndex, EPluginStyleParameter flipped)
	{
		gSDK->SetAllPluginStyleParameters(style.symDef, base);
		if (flipIndex < names.size())
			gSDK->SetPluginStyleParameterType(style.symDef, Tx(names[flipIndex]), flipped);
	}

	// 由来表を「名指しで全件 <type>」にする（`SetAllPluginStyleParameters` は使わない）。
	void SetEveryNameTo(const ProbeStyle& style, const std::vector<std::string>& names,
						EPluginStyleParameter type)
	{
		for (size_t i = 0; i < names.size(); ++i)
			gSDK->SetPluginStyleParameterType(style.symDef, Tx(names[i]), type);
	}

	// 由来表を全件読み、種別ごとに数える。`expected` と違う名前は `outliers` へ。
	std::string TallyMap(MCObjectHandle target, const std::vector<std::string>& names,
						 EPluginStyleParameter expected, std::vector<std::string>& outliers)
	{
		long long counts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		long long other = 0;
		outliers.clear();
		for (size_t i = 0; i < names.size(); ++i)
		{
			const EPluginStyleParameter type =
				gSDK->GetPluginStyleParameterType(target, Tx(names[i]));
			if (type >= 0 && type < 8)
				++counts[type];
			else
				++other;
			if (type != expected && outliers.size() < kListLimit)
				outliers.push_back(names[i] + "=" + DescribeStyleType(type));
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

	std::string JoinList(const std::vector<std::string>& items)
	{
		if (items.empty())
			return "（該当なし）";
		std::string text;
		for (size_t i = 0; i < items.size() && i < kListLimit; ++i)
		{
			if (!text.empty())
				text += ", ";
			text += items[i];
		}
		if (items.size() > kListLimit)
			text += ", …（他 " + Count(static_cast<long long>(items.size() - kListLimit)) + " 件）";
		return text;
	}

} // namespace

VW_PROBE("style-bound-only-by-instance", "バウンドだけ by-instance のプラグインスタイルは作れるか",
		 "構造材のパラメータ表を全数列挙し、由来（by-style / by-instance）を"
		 "名指しで組み替えながら、書いたストーリバウンドがスタイルに取られるかを測る。"
		 "『他はスタイルで揃え、高さだけ本ごと』が作れるかに yes / no で答える。"
		 "新規の空図面で走らせること")
{
	// =======================================================================
	probe.log("=== この図面について ===");
	{
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		TXString layerName;
		if (currentLayer != nullptr)
			gSDK->GetObjectName(currentLayer, layerName);
		probe.log(std::string("いまのレイヤ: \"") + Str(layerName) + "\"");
		probe.log("物差し: **水平材**（平面長 " + Num(kPlanar) + " ／ Z 差 0）に両端とも offset " +
				  Num(kElevLow) + " を書き、**バウンド " + Num(kElevLow) + " / " + Num(kElevHigh) +
				  " のスタイル**を当てて `ResetObject` する");
		probe.log("  解決が " + Num(kElevLow) + "/" + Num(kElevLow) +
				  " のまま → **バウンドは自由** ／ " + Num(kElevLow) + "/" + Num(kElevHigh) +
				  " になる → **スタイルに取られた**");
	}

	// =======================================================================
	probe.log("=== A. パラメータ表を全数列挙し、スタイルを 2 本作る ===");
	std::vector<std::string> names;
	{
		MCObjectHandle sample = CreateBare(kPlanar, 0, 0);
		if (sample == nullptr)
		{
			probe.fail("A: 列挙用の部材を作れなかった");
			return;
		}
		VWParametricObj sampleObj(sample);
		const size_t count = sampleObj.GetParamsCount();
		probe.log("  `GetParamsCount` = " + Count(static_cast<long long>(count)));
		for (size_t i = 0; i < count && i < kSweepLimit; ++i)
		{
			const std::string name = Str(sampleObj.GetParamName(i));
			if (!name.empty())
				names.push_back(name);
		}
		probe.log("  名前を引けたもの: " + Count(static_cast<long long>(names.size())) + " 件" +
				  (count > kSweepLimit ? "（上限 " + Count(kSweepLimit) + " で打ち切り）" : ""));
		// 10 件ずつ並べる。**掃引で動いた名前を突き合わせるための一覧**なので省略しない。
		for (size_t i = 0; i < names.size(); i += 10)
		{
			std::string line = "    [" + Count(static_cast<long long>(i)) + "] ";
			for (size_t j = i; j < i + 10 && j < names.size(); ++j)
			{
				if (j != i)
					line += ", ";
				line += names[j];
			}
			probe.log(line);
		}
		if (names.empty())
		{
			probe.fail("A: パラメータ名を 1 つも引けなかった（以降の群は成立しない）");
			return;
		}
	}

	const ProbeStyle work = MakeStyle(probe, "作業用", false, true);
	if (work.ref == 0 || work.symDef == nullptr)
	{
		probe.fail("A: 作業用スタイルを作れなかった（以降の群は成立しない）");
		return;
	}

	// =======================================================================
	probe.log("=== B. 1 つずつ切り替える口は効くのか（by-style / by-instance の読み戻し） ===");
	{
		std::vector<std::string> outliers;

		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByStyle);
		probe.log("  `SetAllPluginStyleParameters(ByStyle)` の後（定義から読む）: " +
				  TallyMap(work.symDef, names, kPluginStyleParameter_ByStyle, outliers));
		probe.log("    by-style でなかった名前: " + JoinList(outliers));

		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByInstance);
		probe.log("  `SetAllPluginStyleParameters(ByInstance)` の後（定義から読む）: " +
				  TallyMap(work.symDef, names, kPluginStyleParameter_ByInstance, outliers));
		probe.log("    by-instance でなかった名前: " + JoinList(outliers));

		// 1 つだけ裏返して読み戻す（**書けたことを読み戻さずに信じない**）。
		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByStyle);
		const std::string first = names[0];
		gSDK->SetPluginStyleParameterType(work.symDef, Tx(first), kPluginStyleParameter_ByInstance);
		probe.log("  `SetPluginStyleParameterType(\"" + first + "\", ByInstance)` の読み戻し: " +
				  DescribeStyleType(gSDK->GetPluginStyleParameterType(work.symDef, Tx(first))) +
				  " ／ 隣の \"" + names[names.size() > 1 ? 1 : 0] + "\" は " +
				  DescribeStyleType(gSDK->GetPluginStyleParameterType(
					  work.symDef, Tx(names[names.size() > 1 ? 1 : 0]))));

		// **`GetPluginStyleParameterType` の第 1 引数は「object」**（`ISDK.h:2921`）。
		// 定義とインスタンスのどちらを受けるのかはヘッダからは決まらないので両方引く。
		MCObjectHandle probeInstance = CreateTargetWithBounds(probe, "B", kElevLow, kElevLow);
		if (probeInstance != nullptr)
		{
			gSDK->SetPluginObjectStyle(probeInstance, work.ref);
			probe.log(
				"  同じ名前をインスタンスから読む: " +
				DescribeStyleType(gSDK->GetPluginStyleParameterType(probeInstance, Tx(first))));
		}

		// 表に無い名前を引くと何が返るか（「バウンドを名指しできる名前」を探すときの物差し）。
		probe.log("  表に無い名前（\"VwProbeNoSuchParam\"）を引くと: " +
				  DescribeStyleType(gSDK->GetPluginStyleParameterType(
					  work.symDef, TXString("VwProbeNoSuchParam"))));
		probe.log("  → 判定: 全件が揃って by-style / by-instance を往復するなら、"
				  "**由来表の席はパラメータ名だけ**で、バウンド専用の名前は無い（issue の 3）");
	}

	// =======================================================================
	probe.log("=== C. 名指しで全部 by-instance にすると、バウンドは自由になるか（issue の 1） ===");
	{
		// 対照 1: `SetAllPluginStyleParameters(ByStyle)`（#112 の既知の形）
		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByStyle);
		const Snap byStyle = MeasureWithReset(probe, "C", work.ref);
		probe.log("  C1 一括 by-style（対照・#112 で取られた形）: " + DescribeSnap(byStyle));
		probe.log("     → " + Verdict(byStyle));

		// 対照 2: `SetAllPluginStyleParameters(ByInstance)`（#112 の逃げ道）
		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByInstance);
		const Snap byInstance = MeasureWithReset(probe, "C", work.ref);
		probe.log("  C2 一括 by-instance（対照・#112 の逃げ道）: " + DescribeSnap(byInstance));
		probe.log("     → " + Verdict(byInstance));

		// 本題: 一括の口を使わず、**名指しで全件**を by-instance にする。
		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByStyle);
		SetEveryNameTo(work, names, kPluginStyleParameter_ByInstance);
		const Snap named = MeasureWithReset(probe, "C", work.ref);
		probe.log("  C3 **名指しで全件 by-instance**: " + DescribeSnap(named));
		probe.log("     → " + Verdict(named));

		probe.log(std::string("  → 判定: C3 が C2 と同じなら、**バウンドは名前で届いている**"
							  "（どれかの名前に紐づく）。C3 が C1 と同じなら、"
							  "**`SetAllPluginStyleParameters` は名前の表以外の何かも書いている**"
							  "——名指しでは届かない。実測: C1=") +
				  (BoundStayed(byStyle) ? "自由" : "取られた") +
				  " ／ C2=" + (BoundStayed(byInstance) ? "自由" : "取られた") +
				  " ／ C3=" + (BoundStayed(named) ? "自由" : "取られた"));
	}

	// =======================================================================
	probe.log("=== D. 一括 by-instance の後、名指しで全部 by-style へ戻す（issue の 2） ===");
	bool recipeWorks = false;
	{
		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByInstance);
		SetEveryNameTo(work, names, kPluginStyleParameter_ByStyle);

		std::vector<std::string> outliers;
		probe.log("  由来表の読み戻し: " +
				  TallyMap(work.symDef, names, kPluginStyleParameter_ByStyle, outliers));
		probe.log("    by-style でなかった名前: " + JoinList(outliers));

		const Snap snap = MeasureWithReset(probe, "D", work.ref);
		probe.log("  結果: " + DescribeSnap(snap));
		recipeWorks = BoundStayed(snap);
		probe.log("  → 判定: " + Verdict(snap) +
				  "。**自由なら、これが求めている形そのもの**"
				  "——名前を持つパラメータは全部スタイル、バウンドだけ本ごと");
	}

	// =======================================================================
	probe.log("=== E. 掃引: 全部 by-style から 1 つだけ by-instance にする ===");
	{
		ShapeMap(work, names, kPluginStyleParameter_ByStyle, names.size(),
				 kPluginStyleParameter_ByStyle);
		const Snap baseline = MeasureWithReset(probe, "E", work.ref);
		probe.log("  baseline（1 つも裏返さない）: " + Verdict(baseline));

		std::vector<std::string> hits;
		size_t tried = 0;
		for (size_t i = 0; i < names.size(); ++i)
		{
			ShapeMap(work, names, kPluginStyleParameter_ByStyle, i,
					 kPluginStyleParameter_ByInstance);
			const Snap snap = MeasureWithReset(probe, "E", work.ref);
			++tried;
			if (BoundStayed(snap) != BoundStayed(baseline))
				hits.push_back(names[i]);
		}
		probe.log("  試した件数=" + Count(static_cast<long long>(tried)));
		probe.log("  **baseline と違う結果になった名前**: " + JoinList(hits));
		probe.log("  → 判定: 1 件でもあれば、**バウンドはその名前に紐づいている**"
				  "（＝その名前だけ by-instance にすれば、他はスタイルのままにできる）");
	}

	// =======================================================================
	probe.log("=== F. 掃引: 全部 by-instance から 1 つだけ by-style へ戻す ===");
	{
		ShapeMap(work, names, kPluginStyleParameter_ByInstance, names.size(),
				 kPluginStyleParameter_ByInstance);
		const Snap baseline = MeasureWithReset(probe, "F", work.ref);
		probe.log("  baseline（1 つも戻さない）: " + Verdict(baseline));

		std::vector<std::string> hits;
		size_t tried = 0;
		for (size_t i = 0; i < names.size(); ++i)
		{
			ShapeMap(work, names, kPluginStyleParameter_ByInstance, i,
					 kPluginStyleParameter_ByStyle);
			const Snap snap = MeasureWithReset(probe, "F", work.ref);
			++tried;
			if (BoundStayed(snap) != BoundStayed(baseline))
				hits.push_back(names[i]);
		}
		probe.log("  試した件数=" + Count(static_cast<long long>(tried)));
		probe.log("  **baseline と違う結果になった名前**: " + JoinList(hits));
		probe.log("  → 判定: ここに並んだ名前が**バウンドを取る名前**。"
				  "D 群が「取られた」だったときは、**この名前だけを by-instance に残せば**"
				  "『他はスタイル・高さは本ごと』になる");
	}

	// =======================================================================
	probe.log("=== G. 実用形の総合確認（高さは本ごと・断面はスタイルで揃うか） ===");
	{
		// D 群の作り方（一括 by-instance → 名指しで全件 by-style）で組み直す。
		// **D 群で駄目だったとしても、そのまま測る**——「何が崩れるか」が知見になる。
		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByInstance);
		SetEveryNameTo(work, names, kPluginStyleParameter_ByStyle);
		probe.log(std::string("  使う作り方: 一括 by-instance → 名指しで全件 by-style"
							  "（D 群の判定は ") +
				  (recipeWorks ? "「自由」" : "「取られた」") + "）");
		probe.log("  スタイル側の値: MemberID=\"" + std::string(kStyleMemberID) +
				  "\" ／ MajorBreadth=" + Num(kStyleBreadth));

		const double wanted[3] = {kElevLow, 4000, 6000};
		for (int index = 0; index < 3; ++index)
		{
			MCObjectHandle pio = CreateTargetWithBounds(probe, "G", kElevLow, wanted[index]);
			if (pio == nullptr)
				continue;
			VWParametricObj obj(pio);
			obj.SetParamString("MemberID", kInstanceMemberID);
			obj.SetParamReal("MajorBreadth", kInstanceBreadth);
			gSDK->SetPluginObjectStyle(pio, work.ref);
			// 3 本目だけ `UpdateStyledObjects` 経路で作り直す（#112 の E 群と同じ切り分け）。
			if (index == 2)
				gSDK->UpdateStyledObjects(work.ref);
			else
				gSDK->ResetObject(pio);

			const Snap snap = Read(pio);
			VWParametricObj after(pio);
			const std::string memberID = Str(after.GetParamValue("MemberID"));
			const double breadth = after.GetParamReal("MajorBreadth");
			probe.log("  本 " + Count(index + 1) + "（書いたバウンド " + Num(kElevLow) + "/" +
					  Num(wanted[index]) + " ／ 作り直しは " +
					  (index == 2 ? "`UpdateStyledObjects`" : "`ResetObject`") + "）");
			probe.log("    " + DescribeSnap(snap));
			probe.log("    高さは書いたとおりか: " +
					  std::string((Near(snap.resolved[0], kElevLow) &&
								   Near(snap.resolved[1], wanted[index]))
									  ? "**はい**"
									  : "**いいえ**") +
					  " ／ MemberID=\"" + memberID + "\"（スタイル側の値か: " +
					  (memberID == std::string(kStyleMemberID) ? "**はい**" : "**いいえ**") +
					  "） ／ MajorBreadth=" + Num(breadth) + "（スタイル側の値か: " +
					  (Near(breadth, kStyleBreadth) ? "**はい**" : "**いいえ**") + "）");
		}
		probe.log("  → 判定: **3 本とも「高さは書いたとおり＝はい」かつ"
				  "「スタイル側の値か＝はい」なら、求めている形は作れる**。"
				  "高さが「いいえ」なら作れない。値が「いいえ」なら"
				  "**スタイルが無力化されている**（＝「全部 by-instance」と同じ）");
	}

	// =======================================================================
	probe.log("=== まとめ ===");
	probe.log("  読む順: B（口は効くか）→ C（名指しで届くか）→ D（求める形）→ E / F（掃引）"
			  "→ G（総合）");
	probe.log("  答えたいのは 1 つ: **「バウンドだけ by-instance、他は by-style」は作れるか**"
			  "（作れるならその手順）");
}
