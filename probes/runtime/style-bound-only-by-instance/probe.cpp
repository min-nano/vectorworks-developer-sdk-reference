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
//	これは**全パラメータ**を切り替える——つまり逃げ道は「スタイルをほぼ無力化する」ことと
//	引き換えだった。取り込みが実際にやりたいのは「**断面や材種はスタイルで揃えつつ、
//	高さだけは本ごとに指定する**」で、それが作れるかで実プラグインがスタイルを使えるかが決まる。
//
//	## 1 度目の実行で分かったこと（2026-09-25 / VW 2026 / mac。PR #119 のコメント）
//
//	  * **1 つずつ切り替える口は効く。** 181 件すべてが by-style / by-instance を往復し、
//	    `SetAllPluginStyleParameters` は表の全件に行き渡る（外れ値ゼロ）。
//	  * **バウンドは名前付きパラメータ 1 つに紐づいていた**——181 件を両方向に総当たりして、
//	    **どちらの向きでも `DialogStartElevationReference` ただ 1 件**が動いた。
//	    「バウンドはパラメータではないから名指しできない」という読みは誤りだった。
//	  * よって**求めていた形は作れる**見込み: 全件 by-style にしたうえで、その 1 件だけを
//	    `SetPluginStyleParameterType(..., kPluginStyleParameter_ByInstance)` にする。
//
//	## この 2 度目で取りに行くもの
//
//	  1. **その作り方が本当に実用になるか。** 1 度目の G 群は**別の作り方（全件 by-style）で
//	     測ってしまっていた**ので、「高さは本ごと・断面はスタイルで揃う」の確認になっていない。
//	     今回は**掃引で見つけた名前をそのまま使う**（名前を決め打ちしない）。
//	  2. **物差しが片方の端しか見ていなかった。** 1 度目は「書く 2500/2500・スタイル
//	     2500/5500」だったので、**差が出るのは終端（ID 1）だけ**だった。`DialogStart…` と
//	     いう名前なのに終端が動いているのは筋が通らないので、**書く値を 3000/3000**（スタイルの
//	     どちらとも違う）に替え、**ID 0 と ID 1 を別々に判定**する。始端と終端で別の名前が
//	     要るなら、ここで出る。
//	  3. **副産物の切り分け。** 1 度目の G 群では、`ResetObject` で作り直した本は
//	     `MemberID` / `MajorBreadth` が**インスタンスの値のまま**だったのに、
//	     `UpdateStyledObjects` を流した本は**スタイルの値**になっていた。**バウンドは
//	     `ResetObject` でも配られるのに、パラメータの値は配られない**ように見える。
//	     同じ本を流す前後で読み直して確かめる。
//
//	## 群
//
//	  A 群 … 下ごしらえ。パラメータ表を数え、**高さに関わる名前だけ**を並べる
//	          （181 件の全一覧は 1 度目の結果コメントに残っているので繰り返さない）。
//	  B 群 … `SetAllPluginStyleParameters` が表の全件に行き渡ることの再確認。
//	  C 群 … **掃引。** 全件 by-style から**1 つだけ**by-instance にして総当たりし、
//	          **ID 0 が自由になる名前**と**ID 1 が自由になる名前**を別々に集める。
//	  D 群 … **逆の掃引。** 全件 by-instance から**1 つだけ**by-style へ戻して総当たりし、
//	          **ID 0 / ID 1 を取る名前**を別々に集める。
//	  E 群 … **C 群で見つかった名前だけを裏返した形**（＝求めている形）で、両端とも
//	          自由になるかを確かめる。
//	  F 群 … **実用形の総合確認。** E 群の形で本を 6 つ置き、①高さは本ごとに別々
//	          ②断面・材種（`MemberID` / `MajorBreadth`）はスタイルで揃う、を
//	          **`ResetObject` 経路と `UpdateStyledObjects` 経路の両方**で見る。
//	          **`UpdateStyledObjects` を流した後に 6 つ全部を読み直す**ので、
//	          上記 3（値がいつ配られるか）もここで決まる。
//
//	## 読み方
//
//	各群の末尾に「→ 判定:」の 1 行を出す。掃引は**baseline と違う結果になった名前だけ**を
//	並べる。プローブは新規の空図面で走る前提で、undo イベントは開かない。
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
	//
	// **物差しは両端で効かせる。** 書く値をスタイルのどちらの端とも違う 3000 にすると、
	// 「ID 0 が自由か」「ID 1 が自由か」を別々に読める（1 度目は書く値の片方が
	// スタイルと同じだったので、始端の動きが見えなかった）。
	const double kWant = 3000;		// 対象に書く offset（両端とも）
	const double kStyleLow = 2500;	// スタイルが持つ下端 offset
	const double kStyleHigh = 5500; // スタイルが持つ上端 offset
	const double kPlanar = 2000;	// 水平材の平面上の長さ

	// 一致と見なす窓。作り直しは 1〜2 ULP の残差を残す（#67 / #71）ので 1e-6 で見る。
	const double kEpsilon = 1e-6;

	// F 群で「スタイル側の値が流れてくるか」を見るための、見分けの付く値。
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

	bool Contains(const std::string& haystack, const char* needle)
	{
		return haystack.find(needle) != std::string::npos;
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
	// 読み戻し。**バウンドのレコードと解決した絶対Z**をひと揃いで取る。
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

	std::string DescribeSnap(const Snap& snap)
	{
		return "Δz=" + Num(snap.dz) + " 挿入点Z=" + Num(snap.insertZ) +
			   " ／ 件数=" + Count(snap.boundCount) +
			   " ／ レコード offset=" + Num(snap.recOffset[0]) + "/" + Num(snap.recOffset[1]) +
			   " ／ 解決=" + Num(snap.resolved[0]) + "/" + Num(snap.resolved[1]) +
			   " ／ styleRef=" + Count(static_cast<long long>(snap.styleRef));
	}

	// **この調査の物差し。** ID ごとに「書いた値が残ったか」を別々に読む。
	struct Freedom
	{
		bool free0 = false;
		bool free1 = false;
	};

	Freedom Judge(const Snap& snap, double want0, double want1)
	{
		Freedom freedom;
		freedom.free0 = Near(snap.resolved[0], want0);
		freedom.free1 = Near(snap.resolved[1], want1);
		return freedom;
	}

	std::string DescribeFreedom(const Freedom& freedom)
	{
		return std::string("ID0=") + (freedom.free0 ? "**自由**" : "**取られた**") +
			   " ／ ID1=" + (freedom.free1 ? "**自由**" : "**取られた**");
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
	// `CreateCustomObject*` は、その名前の PIO が文書に未定義なら定義を作り、その
	// `prefWhen` の**既定が `kCustomObjectPrefAlways`**——つまり**最初の 1 個で
	// ダイアログが出て止まる**（[Findings「生成時に『オブジェクトの設定』ダイアログが
	// 出る」](../../../Findings/Parametric%20Objects.md)）。`gSDK->CreateCustomObjectPath`
	// を直に叩くこのプローブには、VWFC の `VWParametricObj` が内側で呼んでいる
	// `GS_DefineCustomObject(..., kCustomObjectPrefNever)`（`VWParametricObj.cpp:33`）が
	// 掛からないので、**自分で 1 度呼ぶ**。
	//
	// **実際に踏んだ。** 1 度目の実行は構造材が使われている文書だったので素通りし、
	// 2 度目はまっさらな文書（レイヤ「レイヤ-1」）で**構造材の「オブジェクトの設定」が
	// 出て**、キャンセルされ、`CreateCustomObjectPath` が nil を返して A 群で止まった。
	void DefineStructuralMember(vwprobe::Report& probe)
	{
		// 戻り値は定義のハンドル（`ISDK.h:1087`）。**nil でも止めない**——既に定義が
		// ある文書で nil が返るのか確かめていないので、ここで判断材料だけ残す。
		MCObjectHandle definition =
			gSDK->DefineCustomObject("StructuralMember", kCustomObjectPrefNever);
		probe.log(std::string("`DefineCustomObject(\"StructuralMember\", "
							  "kCustomObjectPrefNever)`: ") +
				  (definition != nullptr ? "定義のハンドルが返った"
										 : "**nil が返った**（既に定義済みの文書かもしれない）") +
				  " ／ これを呼ばないと、**PIO が未定義の文書では最初の 1 個で"
				  "「オブジェクトの設定」ダイアログが出て止まる**");
	}

	// `CreateBare` が nil を返した理由（直近）。**どちらの呼び出しで落ちたかを
	// 呼び出し側のメッセージへ乗せる**ため——2 度目の実行では「部材を作れなかった」
	// としか出ず、ダイアログが出たことはログから分からなかった。
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
			gCreateFailure = "`CreateCustomObjectPath(\"StructuralMember\", …)` が nil を"
							 "返した（「オブジェクトの設定」ダイアログが出てキャンセル"
							 "された可能性。抑止は走り出しの `DefineCustomObject` で"
							 "済ませてある）";
		return pio;
	}

	// 対象（**水平材**）を作り、書きたいバウンドを両端へ書く。
	MCObjectHandle CreateTargetWithBounds(vwprobe::Report& probe, const std::string& where,
										  double low, double high)
	{
		MCObjectHandle pio = CreateBare(kPlanar, 0, 0);
		if (pio == nullptr)
		{
			probe.fail(where + ": 対象を作れなかった——" + gCreateFailure);
			return nullptr;
		}
		WriteLayerElevationBound(pio, 0, low);
		WriteLayerElevationBound(pio, 1, high);
		return pio;
	}

	// 物差しを 1 回引く: 対象を作る → スタイルを当てる → `ResetObject` → 読む。
	Freedom MeasureWithReset(vwprobe::Report& probe, const std::string& where, RefNumber styleRef,
							 Snap& snapOut)
	{
		MCObjectHandle pio = CreateTargetWithBounds(probe, where, kWant, kWant);
		if (pio == nullptr)
			return Freedom();
		gSDK->SetPluginObjectStyle(pio, styleRef);
		gSDK->ResetObject(pio);
		snapOut = Read(pio);
		return Judge(snapOut, kWant, kWant);
	}

	// -----------------------------------------------------------------------
	// プラグインスタイルを 1 本作る（#98 で確かめた手順。**`CreatePluginStyle` は
	// 呼んではいけない**——文書の PIO が全滅する）。
	//
	// **`Style` と名乗らない。** macOS の `MacTypes.h` が `typedef unsigned char Style;` を
	// グローバルへ出しているので、無名名前空間に入れていても参照が曖昧になる
	// （probes/runtime/README.md「短い名前・ありふれた名前を使わない」）。
	struct ProbeStyle
	{
		RefNumber ref = 0;
		MCObjectHandle symDef = nullptr;
	};

	ProbeStyle MakeStyle(vwprobe::Report& probe, const std::string& label)
	{
		ProbeStyle style;

		MCObjectHandle seed = CreateBare(0, 0, 0);
		if (seed == nullptr)
		{
			probe.fail("A: " + label + " の元にする部材を作れなかった——" + gCreateFailure);
			return style;
		}
		// 元にする材のバウンドは 2500 / 5500。**対象が書く 3000 / 3000 とは
		// 両端とも違う**ので、取られた端が一目で分かる。
		WriteLayerElevationBound(seed, 0, kStyleLow);
		WriteLayerElevationBound(seed, 1, kStyleHigh);
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
		gSDK->SetAllPluginStyleParameters(symDef, kPluginStyleParameter_ByStyle);

		const bool isStyle = gSDK->IsPluginStyle(symDef);
		const RefNumber ref = static_cast<RefNumber>(gSDK->GetObjectInternalIndex(symDef));
		probe.log("  " + label + ": 名前=\"" + Str(used) + "\" ／ 中へ入れた=" + YesNo(added) +
				  " ／ IsPluginStyle=" + YesNo(isStyle) +
				  " ／ ref=" + Count(static_cast<long long>(ref)) + " ／ バウンド " +
				  Num(kStyleLow) + "/" + Num(kStyleHigh) + " ／ MemberID=\"" + kStyleMemberID +
				  "\" ／ MajorBreadth=" + Num(kStyleBreadth));
		if (!isStyle || ref == 0)
		{
			probe.fail("A: " + label + " をプラグインスタイルにできなかった");
			return style;
		}
		style.ref = ref;
		style.symDef = symDef;
		return style;
	}

	// 由来表を「全件 <base>」にしてから、名前を 1 つだけ <flipped> にする。
	// `flipIndex` が names の範囲外なら、1 つも裏返さない（baseline 用）。
	void ShapeMap(const ProbeStyle& style, const std::vector<std::string>& names,
				  EPluginStyleParameter base, size_t flipIndex, EPluginStyleParameter flipped)
	{
		gSDK->SetAllPluginStyleParameters(style.symDef, base);
		if (flipIndex < names.size())
			gSDK->SetPluginStyleParameterType(style.symDef, Tx(names[flipIndex]), flipped);
	}

	// 由来表を全件読み、種別ごとに数える。
	std::string TallyMap(MCObjectHandle target, const std::vector<std::string>& names)
	{
		long long counts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		long long other = 0;
		for (size_t i = 0; i < names.size(); ++i)
		{
			const EPluginStyleParameter type =
				gSDK->GetPluginStyleParameterType(target, Tx(names[i]));
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

	void AddUnique(std::vector<std::string>& into, const std::string& name)
	{
		for (size_t i = 0; i < into.size(); ++i)
		{
			if (into[i] == name)
				return;
		}
		into.push_back(name);
	}

	// F 群で置く本 1 つぶん。
	struct Bar
	{
		MCObjectHandle pio = nullptr;
		std::string label;
		double low = 0;
		double high = 0;
	};

	void LogBar(vwprobe::Report& probe, const Bar& bar, const std::string& when)
	{
		if (bar.pio == nullptr)
			return;
		const Snap snap = Read(bar.pio);
		const Freedom freedom = Judge(snap, bar.low, bar.high);
		VWParametricObj obj(bar.pio);
		const std::string memberID = Str(obj.GetParamValue("MemberID"));
		const double breadth = obj.GetParamReal("MajorBreadth");
		probe.log("    " + bar.label + " " + when + ": " + DescribeSnap(snap));
		probe.log("      書いた " + Num(bar.low) + "/" + Num(bar.high) + " のままか: " +
				  DescribeFreedom(freedom) + " ／ MemberID=\"" + memberID + "\"（スタイル側=" +
				  (memberID == std::string(kStyleMemberID) ? "**はい**" : "**いいえ**") +
				  "） ／ MajorBreadth=" + Num(breadth) + "（スタイル側=" +
				  (Near(breadth, kStyleBreadth) ? "**はい**" : "**いいえ**") + "）");
	}

} // namespace

VW_PROBE("style-bound-only-by-instance", "バウンドだけ by-instance のプラグインスタイルは作れるか",
		 "構造材のパラメータ表の由来（by-style / by-instance）を 1 つずつ組み替えて総当たりし、"
		 "ストーリバウンドを握っている名前を ID 0 / ID 1 別に特定する。"
		 "見つけた名前だけを by-instance にした形で、"
		 "『高さは本ごと・断面はスタイルで揃う』が本当に成り立つかまで確かめる。"
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
				  Num(kWant) + " を書き、**バウンド " + Num(kStyleLow) + " / " + Num(kStyleHigh) +
				  " のスタイル**を当てて `ResetObject` する");
		probe.log("  書く値はスタイルの**どちらの端とも違う**ので、**ID 0 と ID 1 を別々に**"
				  "判定できる（1 度目は終端しか見えていなかった）");
		// **何かを作る前に必ずここを通す。** これが無いと、構造材が使われていない文書では
		// 最初の 1 個で「オブジェクトの設定」ダイアログが出て止まる（2 度目の実行で踏んだ）。
		DefineStructuralMember(probe);
	}

	// =======================================================================
	probe.log("=== A. パラメータ表を数え、高さに関わる名前を並べる ===");
	std::vector<std::string> names;
	{
		MCObjectHandle sample = CreateBare(kPlanar, 0, 0);
		if (sample == nullptr)
		{
			probe.fail("A: 列挙用の部材を作れなかった——" + gCreateFailure);
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
		probe.log("  名前を引けたもの: " + Count(static_cast<long long>(names.size())) +
				  " 件（**181 件の全一覧は 1 度目の結果コメントに残っている**ので繰り返さない）");
		// 高さ・階・バウンドに関わりそうな名前だけを並べる（掃引の結果と突き合わせる用）。
		std::vector<std::string> related;
		for (size_t i = 0; i < names.size(); ++i)
		{
			if (Contains(names[i], "Elev") || Contains(names[i], "Story") ||
				Contains(names[i], "Bound") || Contains(names[i], "Level") ||
				Contains(names[i], "Height") || Contains(names[i], "Dialog"))
				related.push_back("[" + Count(static_cast<long long>(i)) + "]" + names[i]);
		}
		probe.log("  高さ・階・バウンドに関わりそうな名前: " + JoinList(related));
		if (names.empty())
		{
			probe.fail("A: パラメータ名を 1 つも引けなかった（以降の群は成立しない）");
			return;
		}
	}

	const ProbeStyle work = MakeStyle(probe, "作業用");
	if (work.ref == 0 || work.symDef == nullptr)
	{
		probe.fail("A: 作業用スタイルを作れなかった（以降の群は成立しない）");
		return;
	}

	// =======================================================================
	probe.log("=== B. 一括の口が表の全件に行き渡るか（1 度目の再確認） ===");
	{
		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByStyle);
		probe.log("  `SetAllPluginStyleParameters(ByStyle)` の後: " + TallyMap(work.symDef, names));
		gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByInstance);
		probe.log("  `SetAllPluginStyleParameters(ByInstance)` の後: " +
				  TallyMap(work.symDef, names));
	}

	// =======================================================================
	probe.log("=== C. 掃引: 全件 by-style から 1 つだけ by-instance にする ===");
	std::vector<std::string> freeing0;
	std::vector<std::string> freeing1;
	{
		Snap snap;
		ShapeMap(work, names, kPluginStyleParameter_ByStyle, names.size(),
				 kPluginStyleParameter_ByStyle);
		const Freedom baseline = MeasureWithReset(probe, "C", work.ref, snap);
		probe.log("  baseline（1 つも裏返さない）: " + DescribeFreedom(baseline) + " ／ " +
				  DescribeSnap(snap));

		for (size_t i = 0; i < names.size(); ++i)
		{
			ShapeMap(work, names, kPluginStyleParameter_ByStyle, i,
					 kPluginStyleParameter_ByInstance);
			const Freedom freedom = MeasureWithReset(probe, "C", work.ref, snap);
			if (freedom.free0 != baseline.free0)
				freeing0.push_back(names[i]);
			if (freedom.free1 != baseline.free1)
				freeing1.push_back(names[i]);
		}
		probe.log("  試した件数=" + Count(static_cast<long long>(names.size())));
		probe.log("  **ID 0（始端）が自由になった名前**: " + JoinList(freeing0));
		probe.log("  **ID 1（終端）が自由になった名前**: " + JoinList(freeing1));
		probe.log("  → 判定: 両方に同じ名前が並べば、**1 つの名前が両端を握っている**。"
				  "別々なら**端ごとに名前が要る**");
	}

	// =======================================================================
	probe.log("=== D. 逆の掃引: 全件 by-instance から 1 つだけ by-style へ戻す ===");
	{
		Snap snap;
		ShapeMap(work, names, kPluginStyleParameter_ByInstance, names.size(),
				 kPluginStyleParameter_ByInstance);
		const Freedom baseline = MeasureWithReset(probe, "D", work.ref, snap);
		probe.log("  baseline（1 つも戻さない）: " + DescribeFreedom(baseline) + " ／ " +
				  DescribeSnap(snap));

		std::vector<std::string> taking0;
		std::vector<std::string> taking1;
		for (size_t i = 0; i < names.size(); ++i)
		{
			ShapeMap(work, names, kPluginStyleParameter_ByInstance, i,
					 kPluginStyleParameter_ByStyle);
			const Freedom freedom = MeasureWithReset(probe, "D", work.ref, snap);
			if (freedom.free0 != baseline.free0)
				taking0.push_back(names[i]);
			if (freedom.free1 != baseline.free1)
				taking1.push_back(names[i]);
		}
		probe.log("  試した件数=" + Count(static_cast<long long>(names.size())));
		probe.log("  **ID 0（始端）を取った名前**: " + JoinList(taking0));
		probe.log("  **ID 1（終端）を取った名前**: " + JoinList(taking1));
		probe.log("  → 判定: C 群と同じ顔ぶれなら、**その名前がバウンドの持ち主**で確定");
	}

	// =======================================================================
	probe.log("=== E. 見つかった名前だけを by-instance にする（＝求めている形） ===");
	std::vector<std::string> recipe;
	{
		for (size_t i = 0; i < freeing0.size(); ++i)
			AddUnique(recipe, freeing0[i]);
		for (size_t i = 0; i < freeing1.size(); ++i)
			AddUnique(recipe, freeing1[i]);
		probe.log("  by-instance にする名前: " + JoinList(recipe) + "（" +
				  Count(static_cast<long long>(recipe.size())) +
				  " 件。**C 群が見つけたものを"
				  "そのまま使う**——決め打ちしない）");
		if (recipe.empty())
		{
			probe.fail("E: バウンドを握る名前が 1 つも見つからなかった");
		}
		else
		{
			gSDK->SetAllPluginStyleParameters(work.symDef, kPluginStyleParameter_ByStyle);
			for (size_t i = 0; i < recipe.size(); ++i)
				gSDK->SetPluginStyleParameterType(work.symDef, Tx(recipe[i]),
												  kPluginStyleParameter_ByInstance);
			probe.log("  由来表の読み戻し: " + TallyMap(work.symDef, names) + "（by-instance が " +
					  Count(static_cast<long long>(recipe.size())) + " 件だけなら狙いどおり）");

			Snap snap;
			const Freedom freedom = MeasureWithReset(probe, "E", work.ref, snap);
			probe.log("  結果: " + DescribeSnap(snap));
			probe.log("  → 判定: " + DescribeFreedom(freedom) +
					  "。**両端とも自由なら、求めている形は作れる**");
		}
	}

	// =======================================================================
	probe.log("=== F. 実用形の総合確認（高さは本ごと・断面はスタイルで揃うか） ===");
	if (!recipe.empty())
	{
		// E 群で組んだ由来表のまま使う。
		probe.log("  本ごとに違うバウンドを書き、**スタイルとは違う** MemberID / MajorBreadth を"
				  "インスタンスへ書いてからスタイルを当てる");
		const double lows[6] = {kWant, 3200, 1000, kWant, 3200, 1000};
		const double highs[6] = {kWant, 4000, 6500, kWant, 4000, 6500};
		std::vector<Bar> bars;
		for (int index = 0; index < 6; ++index)
		{
			Bar bar;
			bar.low = lows[index];
			bar.high = highs[index];
			bar.label = std::string(index < 3 ? "R" : "U") + Count(index % 3 + 1);
			bar.pio = CreateTargetWithBounds(probe, "F", bar.low, bar.high);
			if (bar.pio == nullptr)
				continue;
			VWParametricObj obj(bar.pio);
			obj.SetParamString("MemberID", kInstanceMemberID);
			obj.SetParamReal("MajorBreadth", kInstanceBreadth);
			gSDK->SetPluginObjectStyle(bar.pio, work.ref);
			bars.push_back(bar);
		}

		// R1〜R3 だけ 1 本ごとに `ResetObject`。U1〜U3 は触らない。
		probe.log("  R1〜R3 を 1 本ごとに `ResetObject`（U1〜U3 はまだ触らない）");
		for (size_t i = 0; i < bars.size(); ++i)
		{
			if (bars[i].label[0] == 'R')
				gSDK->ResetObject(bars[i].pio);
		}
		for (size_t i = 0; i < bars.size(); ++i)
			LogBar(probe, bars[i], "（Reset 後 / Update 前）");

		// ここで `UpdateStyledObjects` を 1 回。**6 本すべてを読み直す**ので、
		// 「値はいつ配られるのか」（1 度目の副産物）もここで決まる。
		probe.log("  `UpdateStyledObjects` を 1 回流して、**6 本すべてを読み直す**");
		gSDK->UpdateStyledObjects(work.ref);
		for (size_t i = 0; i < bars.size(); ++i)
			LogBar(probe, bars[i], "（Update 後）");

		probe.log("  → 判定: **6 本とも「書いた値のまま＝ID0/ID1 とも自由」かつ"
				  "「スタイル側＝はい」なら、求めている形は実用になる**。"
				  "高さが取られていれば作れない。値が「いいえ」のままなら"
				  "**スタイルが効いていない**（＝全部 by-instance と同じ）");
		probe.log("  → 併せて読む: **`ResetObject` だけの時点（Reset 後 / Update 前）で"
				  "値が「いいえ」なのに、`UpdateStyledObjects` の後で「はい」に変われば、"
				  "パラメータの値を配るのは `UpdateStyledObjects` だけ**"
				  "（バウンドは `ResetObject` でも配られるのに）");
	}

	// =======================================================================
	probe.log("=== まとめ ===");
	probe.log("  読む順: C / D（どの名前がバウンドを握るか）→ E（その名前だけ裏返す）"
			  "→ F（実用になるか）");
	probe.log("  答えたいのは 1 つ: **「バウンドだけ by-instance、他は by-style」は作れるか**"
			  "（作れるならその手順）");
}
