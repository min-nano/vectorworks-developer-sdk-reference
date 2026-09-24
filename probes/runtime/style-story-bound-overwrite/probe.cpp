//
//	probes/runtime/style-story-bound-overwrite/probe.cpp
//
//	[issue #112] `SetPluginObjectStyle` は、既に書いたストーリバウンドを上書きするのか。
//
//	## なぜこれを走らせるか
//
//	[#109](https://github.com/min-nano/vectorworks-developer-sdk-reference/issues/109) の
//	プローブ（`doregen-nonvertical`）の C 群で、**調べていたこととは別に**次が出た。
//	水平材（両端とも offset 2500 で書いた、平面長 2000・Z 差 0 のパス）に対し:
//
//	  * 端部オフセットだけ当てる  → `ResetObject` 後の解決バウンドは **2500 / 2500**（書いたとおり）
//	  * **スタイルを当てる**      → 解決バウンドが **2500 / 5500**（**書いていない値**）
//
//	そのスタイルの元にした PIO は**バウンド 2500 / 5500 の鉛直材**だった。つまり
//	**スタイルの持つバウンドが、後から当てた先のバウンドを上書きした**ように見える。
//
//	Findings「Parametric Objects」は「上下端のバウンドが同じ絶対Zへ解決されないことを
//	**書き込み側で保証する**」「`SetObjectStoryBound` の戻り値が `true` でも
//	`GetObjectBoundElevation` で読み比べる」と書いている。**その読み比べの後にスタイルを
//	当てると値が変わる**なら、検査のタイミングそのものが間違っていることになる
//	——横架材が意図しない高さに生える事故に直結する。
//
//	## 群
//
//	  A 群 … **下ごしらえ。** バウンドの異なるスタイルを 4 本作る（#98 の手順）。
//	          高い(2500/5500) / 低い(2500/2500) / バウンド無し / 高い(by-instance)。
//	  B 群 … **上書きは誰がやっているか。** `ResetObject` を挟まずに
//	          `SetPluginObjectStyle` の**直前と直後**を読む。ここで値が変われば
//	          `SetPluginObjectStyle` そのものが書いている。変わらなければ、書くのは
//	          `ResetObject`（＝スタイルを見て作り直す）側である。
//	  C 群 … **順序で避けられるか。** 同じ材・同じスタイルで 4 通りの順序を並べる。
//	  D 群 … **スタイル側のバウンドが効いているのか。** A 群の 4 本を同じ材へ当て分ける。
//	          当てた先の解決バウンドがスタイルごとに変われば、効いているのはスタイルの
//	          バウンドである（変わらなければ別の要因）。
//	  E 群 … **`UpdateStyledObjects` でも同じか**（1 本ごとの `ResetObject` を省く経路）。
//	  F 群 … **バウンドを 1 本も持たない材にスタイルを当てるとバウンドが生えるか。**
//	          生えるなら「スタイルがバウンドを配っている」と言い切れる。**生えたバウンドを
//	          読み直せるか**も、ここで併せて見る（下記「この 2 度目で取りに行くもの」1）。
//
//	## 1 度目の実行で分かったこと（2026-09-24 / VW 2026 / mac。PR #114 のコメント）
//
//	  * `SetPluginObjectStyle` **単体ではバウンドを触らない**（当てる直前と直後で、
//	    レコードも解決も同じ。変わるのは `styleRef` だけ）。書き換えるのは
//	    **`ResetObject` / `UpdateStyledObjects`** の側である。
//	  * **順序では避けられない。** バウンド先・スタイル先・スタイルの後に書き直し——
//	    3 通りとも同じようにスタイルの値へ書き換わった。
//	  * **効いているのはスタイルのバウンド。** 2500/5500 のスタイルを当てれば 2500/5500、
//	    2500/2500 のスタイルなら書いた値のまま、バウンドを持たないスタイルなら 0/0。
//	  * **`kPluginStyleParameter_ByInstance` にすると書いた値が残る**（by-style だけが
//	    上書きする）。issue #112 の 5 の読み（「バウンドはパラメータではないので
//	    by-instance にしても効かないのでは」）とは逆だった。
//
//	## この 2 度目で取りに行くもの
//
//	  1. **F 群で生えたバウンドが読めなかった件。** バウンドを持たない材へスタイルを
//	     当てて `ResetObject` すると件数が 0 → 2 になり、実体は 2500→5500 に建った。
//	     ところが `GetObjectStoryBound` の `fBound` / `fBoundStory` は読めない値で、
//	     `GetObjectBoundElevation` は **0** を返した。**これはプローブ側の落ち度でも
//	     ありうる**——渡した `SStoryObjectData` を既定構築のままにしていたので、
//	     VW が書かなかったフィールドは**こちらの値が残っただけ**かもしれない。
//	     そこで今回は **番兵を入れてから渡し**、**並んでいる ID を
//	     `GetObjectStoryBoundsAt` で数えて引く**（ID 0 / 1 が並んでいる保証は無い）。
//	  2. **by-instance の逃げ道は `UpdateStyledObjects` でも使えるか**（E 群）。
//	     1 度目の E 群は by-style しか流していない。実装で 1 本ごとの `ResetObject` を
//	     省く経路を採るなら、ここが決まらないと使えない。
//
//	## 読み方
//
//	各行は「**形**」（パス・挿入点・端部の絶対Z）と「**バウンド**」（件数・記録された
//	レコード・解決した絶対Z）を続けて出す。**レコード（`GetObjectStoryBound` が返す
//	offset）と解決（`GetObjectBoundElevation`）を分けて読むこと**——どちらが書き換わって
//	いるかで、上書きの正体が変わる。
//
//	  * **レコードごと書き換わる** → スタイルは対象のバウンド設定そのものを配っている。
//	  * **レコードは書いたまま・解決だけ変わる** → 解決の基準（階・レベル）がスタイル側に
//	    引っ張られている。
//
//	各群の末尾に「→ 判定:」の 1 行を出すので、そこだけ拾えば結論は分かる。
//
//	プローブは新規の空図面で走る前提で、undo イベントは開かない。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	// -----------------------------------------------------------------------
	// 形の定数。**新規の空図面（レイヤ高さ 0）では `LayerElevation` の offset が
	// そのまま解決Zになる**（#81 / #109 と同じ置き方）。
	const double kElevLow = 2500;  // 下端の offset（どの材も共通）
	const double kElevHigh = 5500; // 「高い」スタイルの上端 offset
	const double kPlanar = 2000;   // 水平材の平面上の長さ

	// 一致と見なす窓。作り直しは 1〜2 ULP の残差を残す（#67 / #71）ので 1e-6 で見る。
	const double kEpsilon = 1e-6;

	// `GetObjectStoryBound` へ渡す前に入れておく番兵。**読み戻しにこの値が残っていたら、
	// VW はそのフィールドを書いていない**（呼び出し側の値がそのまま残っただけ）。
	const int kSentinelStory = -12112;
	const double kSentinelOffset = -1211200;

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

	// -----------------------------------------------------------------------
	// 読み戻し。**パス・挿入点・バウンドのレコード・解決した絶対Z**をひと揃いで取る。
	struct Snap
	{
		bool pathOk = false;
		double dx = 0, dy = 0, dz = 0;
		double planar = 0;
		double insertZ = 0;

		long long boundCount = 0;
		bool hasBounds = false;
		// **実際に並んでいる ID**（`GetObjectStoryBoundsAt` で数えて引いたもの）。
		// ID 0 / 1 が並んでいるとは限らないので、決め打ちで読む前にこれを見る。
		std::string boundIDs;
		// ID 0 / 1 のレコードと解決結果。
		bool recOk[2] = {false, false};
		int recBound[2] = {-1, -1};	  // fBound（EStoryObjectBound）
		int recStory[2] = {0, 0};	  // fBoundStory
		double recOffset[2] = {0, 0}; // fOffset
		double resolved[2] = {0, 0};  // GetObjectBoundElevation

		RefNumber styleRef = 0;
	};

	// 端点の絶対Z。**PIO のジオメトリはローカル座標で持たれる**ので挿入点を足す。
	double AbsZ0(const Snap& snap)
	{
		return snap.insertZ;
	}

	double AbsZ1(const Snap& snap)
	{
		return snap.insertZ + snap.dz;
	}

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
				snap.dx = p1.x - p0.x;
				snap.dy = p1.y - p0.y;
				snap.dz = p1.z - p0.z;
				snap.planar = std::sqrt(snap.dx * snap.dx + snap.dy * snap.dy);
			}
		}

		TransformMatrix matrix;
		gSDK->GetEntityMatrix(pio, matrix);
		// **平行移動の成分は `P()`**（`offset` は無名共用体の中の名前で、
		// `TransformMatrix` の直接のメンバではない。`MathCoordTypes.h`）。
		snap.insertZ = matrix.P().z;

		snap.hasBounds = gSDK->HasObjectStoryBounds(pio);
		const size_t count = gSDK->GetObjectStoryBoundsCount(pio);
		snap.boundCount = static_cast<long long>(count);
		// **並んでいる ID を数えて引く**（Findings「書いたら数えて読む」）。1 度目の実行で、
		// スタイルが生やしたバウンドの `fBound` / `fBoundStory` が読めない値になったので、
		// 「そもそも ID 0 / 1 が並んでいるのか」をここで見えるようにする。
		for (size_t index = 0; index < count && index < 8; ++index)
		{
			if (!snap.boundIDs.empty())
				snap.boundIDs += ",";
			snap.boundIDs +=
				Count(static_cast<long long>(gSDK->GetObjectStoryBoundsAt(pio, index)));
		}

		for (int id = 0; id < 2; ++id)
		{
			const MockUp::TObjectBoundID boundID = static_cast<MockUp::TObjectBoundID>(id);
			// **番兵を入れてから呼ぶ。** `SStoryObjectData` は POD 寄りの受け渡し用
			// レコードなので、VW が書かなかったフィールドは**呼び出し側の値のまま**
			// 残る。既定構築のまま渡すと、それが「読めない値」として出てしまい、
			// 「VW が変な値を書いた」と読み違える（1 度目の実行で踏んだ）。
			MockUp::SStoryObjectData data;
			data.fBound = MockUp::eStoryObjectBound_LayerElevation;
			data.fBoundStory = kSentinelStory;
			data.fLayerLevelType = "";
			data.fOffset = kSentinelOffset;
			if (gSDK->GetObjectStoryBound(pio, boundID, data))
			{
				snap.recOk[id] = true;
				snap.recBound[id] = static_cast<int>(data.fBound);
				snap.recStory[id] = static_cast<int>(data.fBoundStory);
				snap.recOffset[id] = data.fOffset;
			}
			snap.resolved[id] = gSDK->GetObjectBoundElevation(pio, boundID);
		}

		gSDK->GetPluginObjectStyle(pio, snap.styleRef);
		return snap;
	}

	std::string DescribeShape(const Snap& snap)
	{
		if (!snap.pathOk)
			return "形: **パスを読めなかった**";
		return "形: Δ=(" + Num(snap.dx) + ", " + Num(snap.dy) + ", " + Num(snap.dz) +
			   ") 平面長=" + Num(snap.planar) + " 挿入点Z=" + Num(snap.insertZ) +
			   " 端部の絶対Z=" + Num(AbsZ0(snap)) + "→" + Num(AbsZ1(snap));
	}

	std::string DescribeRecord(const Snap& snap, int id)
	{
		if (!snap.recOk[id])
			return "無し";
		const std::string story = (snap.recStory[id] == kSentinelStory)
									  ? std::string("**VW は書かなかった**")
									  : Count(snap.recStory[id]);
		const std::string offset = Near(snap.recOffset[id], kSentinelOffset)
									   ? std::string("**VW は書かなかった**")
									   : Num(snap.recOffset[id]);
		return "種別" + Count(snap.recBound[id]) + "/階" + story + "/offset " + offset;
	}

	std::string DescribeBounds(const Snap& snap)
	{
		return "バウンド: 件数=" + Count(snap.boundCount) + "(has=" + YesNo(snap.hasBounds) +
			   ") 並んでいる ID=[" + snap.boundIDs + "] ／ レコード ID0[" +
			   DescribeRecord(snap, 0) + "] ID1[" + DescribeRecord(snap, 1) +
			   "] ／ 解決=" + Num(snap.resolved[0]) + "/" + Num(snap.resolved[1]) +
			   " ／ styleRef=" + Count(static_cast<long long>(snap.styleRef));
	}

	void LogSnap(vwprobe::Report& probe, const std::string& indent, const std::string& when,
				 const Snap& snap)
	{
		probe.log(indent + when + "  " + DescribeShape(snap));
		probe.log(indent + std::string(when.size(), ' ') + "  " + DescribeBounds(snap));
	}

	// 2 つの読み戻しが「バウンドについて」同じか。
	bool SameBounds(const Snap& a, const Snap& b)
	{
		if (a.boundCount != b.boundCount)
			return false;
		for (int id = 0; id < 2; ++id)
		{
			if (a.recOk[id] != b.recOk[id])
				return false;
			if (a.recOk[id] &&
				(a.recBound[id] != b.recBound[id] || a.recStory[id] != b.recStory[id] ||
				 !Near(a.recOffset[id], b.recOffset[id])))
				return false;
			if (!Near(a.resolved[id], b.resolved[id]))
				return false;
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// 作る。

	bool WriteLayerElevationBound(MCObjectHandle pio, short id, double offset)
	{
		// **型は `MockUp` 名前空間にある**（`ISDK.h`）。修飾しないと構文チェックが通らない。
		MockUp::SStoryObjectData data;
		data.fBound = MockUp::eStoryObjectBound_LayerElevation;
		data.fBoundStory = 0;
		data.fLayerLevelType = "";
		data.fOffset = offset;
		return gSDK->SetObjectStoryBound(pio, static_cast<MockUp::TObjectBoundID>(id), data);
	}

	// 2 点の NURBS 曲線。**座標を明示的に入れ直す**（#67 / #74 / #75 と同じ作法——
	// `Add3DVertex` が足した点が渡した位置にならないことがある）。
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

	// 構造材 PIO を 1 本作る。**バウンドも `ResetObject` も呼ばない**（順序を群ごとに
	// 組み替えたいので、ここでは作るだけにする）。
	MCObjectHandle CreateBare(double dx, double dy, double dz)
	{
		MCObjectHandle curve = MakePath(dx, dy, dz);
		if (curve == nullptr)
			return nullptr;
		MCObjectHandle noProfile = nullptr;
		return gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile, true);
	}

	// この調査でずっと使う「対象」——**水平材**（平面長 2000・Z 差 0）。
	MCObjectHandle CreateTarget()
	{
		return CreateBare(kPlanar, 0, 0);
	}

	// 対象へ「書きたいバウンド」を書く（両端とも 2500 ＝ 水平）。
	bool WriteFlatBounds(MCObjectHandle pio)
	{
		const bool a = WriteLayerElevationBound(pio, 0, kElevLow);
		const bool b = WriteLayerElevationBound(pio, 1, kElevLow);
		return a && b;
	}

	// -----------------------------------------------------------------------
	// A 群: プラグインスタイルを 1 本作る（#98 で確かめた手順。**`CreatePluginStyle` は
	// 呼んではいけない**——文書の PIO が全滅する）。
	//
	//   lowOffset / highOffset … 元にする PIO に書くバウンドの offset
	//   withBounds             … false なら元にする PIO にバウンドを 1 本も書かない
	//   byInstance             … true なら `kPluginStyleParameter_ByInstance` にする
	// **`Style` と名乗らない。** macOS の `MacTypes.h` が `typedef unsigned char Style;` を
	// グローバルへ出しているので、無名名前空間に入れていても「参照が曖昧」でコンパイルが
	// 通らない（実際に踏んだ。probes/runtime/README.md「短い名前・ありふれた名前」）。
	struct ProbeStyle
	{
		RefNumber ref = 0;
		std::string label;
	};

	ProbeStyle MakeStyle(vwprobe::Report& probe, const std::string& label, bool withBounds,
						 double lowOffset, double highOffset, bool byInstance)
	{
		ProbeStyle style;
		style.label = label;

		MCObjectHandle seed = CreateBare(0, 0, 0);
		if (seed == nullptr)
		{
			probe.fail("A: " + label + " の元にする部材を作れなかった");
			return style;
		}
		if (withBounds)
		{
			WriteLayerElevationBound(seed, 0, lowOffset);
			WriteLayerElevationBound(seed, 1, highOffset);
		}
		gSDK->ResetObject(seed);
		const Snap seedSnap = Read(seed);

		// **名前がぶつかると `CreateSymbolDefinition` は nil を返す**（同じ図面で 2 度
		// 走らせたとき）。空きが見つかるまで番号を振る。
		MCObjectHandle symDef = nullptr;
		TXString used;
		for (int attempt = 1; attempt <= 40 && symDef == nullptr; ++attempt)
		{
			TXString name(("試験スタイル #112 " + label + " " + Count(attempt)).c_str());
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
		probe.log("  " + label + ": 名前=\"" + static_cast<const char*>(used) +
				  "\" ／ 中へ入れた=" + YesNo(added) + " ／ IsPluginStyle=" + YesNo(isStyle) +
				  " ／ ref=" + Count(static_cast<long long>(ref)) +
				  " ／ パラメータ=" + (byInstance ? "by-instance" : "by-style"));
		probe.log("    元にした PIO の " + DescribeBounds(seedSnap));
		if (!isStyle || ref == 0)
		{
			probe.fail("A: " + label + " をプラグインスタイルにできなかった");
			return style;
		}
		style.ref = ref;
		return style;
	}

} // namespace

VW_PROBE("style-story-bound-overwrite",
		 "SetPluginObjectStyle は書いたストーリバウンドを上書きするか",
		 "水平材（両端 2500）へ、バウンドの違うプラグインスタイルを当て分け、"
		 "スタイルを当てる前後・順序違い・UpdateStyledObjects 経路で、"
		 "記録されたバウンドと解決した絶対Zがどう変わるかを読み比べる。"
		 "新規の空図面で走らせること")
{
	probe.log("=== この図面について ===");
	{
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		TXString layerName;
		if (currentLayer != nullptr)
			gSDK->GetObjectName(currentLayer, layerName);
		probe.log(std::string("いまのレイヤ: \"") + static_cast<const char*>(layerName) + "\"");
		probe.log("対象はどれも**水平材**（平面長 " + Num(kPlanar) +
				  " ／ Z 差 0）で、書きたいバウンドは両端とも offset " + Num(kElevLow) +
				  "（新規の空図面ならこの値がそのまま解決Zになる）");
		probe.log("「高い」スタイルの元にした材は " + Num(kElevLow) + " / " + Num(kElevHigh) +
				  " ＝ **書きたい値と違う**。上書きが起きればここへ引っ張られる");
	}

	// =======================================================================
	probe.log("=== A. スタイルを 4 本作る（#98 の手順） ===");
	const ProbeStyle styleTall =
		MakeStyle(probe, "高い(2500/5500)", true, kElevLow, kElevHigh, false);
	const ProbeStyle styleFlat =
		MakeStyle(probe, "低い(2500/2500)", true, kElevLow, kElevLow, false);
	const ProbeStyle styleNone = MakeStyle(probe, "バウンド無し", false, 0, 0, false);
	const ProbeStyle styleTallByInstance =
		MakeStyle(probe, "高い(by-instance)", true, kElevLow, kElevHigh, true);

	// =======================================================================
	probe.log("=== B. ResetObject を挟まずに、当てる直前と直後を読む ===");
	probe.log("  （ここで変われば `SetPluginObjectStyle` そのものが書いている。"
			  "変わらなければ、書くのは `ResetObject` 側）");
	{
		const ProbeStyle* cases[] = {&styleTall, &styleFlat};
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		{
			const ProbeStyle& style = *cases[i];
			if (style.ref == 0)
				continue;
			probe.log("  " + style.label + " を当てる");
			MCObjectHandle pio = CreateTarget();
			if (pio == nullptr)
			{
				probe.fail("B: 対象を作れなかった");
				continue;
			}
			probe.log("    バウンドを書く: " + YesNo(WriteFlatBounds(pio)));
			const Snap before = Read(pio);
			LogSnap(probe, "    ", "当てる直前", before);

			const bool set = gSDK->SetPluginObjectStyle(pio, style.ref);
			const Snap after = Read(pio);
			LogSnap(probe, "    ", "当てた直後", after);
			probe.log("    SetPluginObjectStyle=" + YesNo(set));
			probe.log(std::string("    → 判定: `SetPluginObjectStyle` 単体でバウンドは") +
					  (SameBounds(before, after) ? "**変わらない**" : "**変わった**"));

			gSDK->ResetObject(pio);
			const Snap reset = Read(pio);
			LogSnap(probe, "    ", "Reset の後 ", reset);
			probe.log(std::string("    → 判定: `ResetObject` を挟むとバウンドは") +
					  (SameBounds(after, reset) ? "**変わらない**" : "**変わった**") +
					  " ／ 書いた " + Num(kElevLow) + "/" + Num(kElevLow) + " のままか: " +
					  (Near(reset.resolved[0], kElevLow) && Near(reset.resolved[1], kElevLow)
						   ? "**はい**"
						   : "**いいえ（上書きされた）**"));
		}
	}

	// =======================================================================
	probe.log("=== C. 順序で避けられるか（どれも「高い(2500/5500)」スタイル） ===");
	if (styleTall.ref != 0)
	{
		for (int order = 0; order < 4; ++order)
		{
			MCObjectHandle pio = CreateTarget();
			if (pio == nullptr)
			{
				probe.fail("C: 対象を作れなかった");
				continue;
			}
			std::string label;
			switch (order)
			{
			case 0: // 実プラグインと同じ（#109 のプローブと同じ）
				label = "C1 バウンド → スタイル → Reset";
				WriteFlatBounds(pio);
				gSDK->SetPluginObjectStyle(pio, styleTall.ref);
				break;
			case 1: // #81 が通した順序
				label = "C2 スタイル → バウンド → Reset";
				gSDK->SetPluginObjectStyle(pio, styleTall.ref);
				WriteFlatBounds(pio);
				break;
			case 2: // 救済策になりうる形
				label = "C3 バウンド → スタイル → バウンド書き直し → Reset";
				WriteFlatBounds(pio);
				gSDK->SetPluginObjectStyle(pio, styleTall.ref);
				WriteFlatBounds(pio);
				break;
			default: // 対照（スタイルを当てない）
				label = "C4 バウンドのみ（スタイル無し） → Reset";
				WriteFlatBounds(pio);
				break;
			}
			gSDK->ResetObject(pio);
			const Snap snap = Read(pio);
			probe.log("  " + label);
			LogSnap(probe, "    ", "Reset の後", snap);
			probe.log(std::string("    → 書いた ") + Num(kElevLow) + "/" + Num(kElevLow) +
					  " のままか: " +
					  (Near(snap.resolved[0], kElevLow) && Near(snap.resolved[1], kElevLow)
						   ? "**はい**"
						   : "**いいえ（上書きされた）**"));
		}
		probe.log("  → 判定: 「はい」と出た順序があれば、**その順序で書けば上書きを避けられる**。"
				  "C4 だけが「はい」なら、順序では避けられない");
	}

	// =======================================================================
	probe.log("=== D. スタイル側のバウンドが効いているのか（当て分け。順序は C1 と同じ） ===");
	{
		const ProbeStyle* cases[] = {&styleTall, &styleFlat, &styleNone, &styleTallByInstance};
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		{
			const ProbeStyle& style = *cases[i];
			if (style.ref == 0)
				continue;
			MCObjectHandle pio = CreateTarget();
			if (pio == nullptr)
			{
				probe.fail("D: 対象を作れなかった");
				continue;
			}
			WriteFlatBounds(pio);
			gSDK->SetPluginObjectStyle(pio, style.ref);
			gSDK->ResetObject(pio);
			const Snap snap = Read(pio);
			probe.log("  " + style.label);
			LogSnap(probe, "    ", "Reset の後", snap);
		}
		probe.log("  → 判定: 解決バウンドが**スタイルごとに違えば、効いているのはスタイルの"
				  "バウンド**。どれも同じなら別の要因（スタイルを当てたこと自体）である。"
				  "「高い(by-instance)」が「高い(2500/5500)」と同じなら、**バウンドは"
				  "パラメータではないので by-instance にしても外せない**");
	}

	// =======================================================================
	probe.log("=== E. UpdateStyledObjects でも同じことが起きるか（ResetObject を呼ばない） ===");
	probe.log("  （**by-style と by-instance を並べる。** 1 度目の実行で、`ResetObject` 経路では"
			  "by-instance のスタイルだけが書いたバウンドを残した——その逃げ道が、"
			  "`ResetObject` を省く経路でも使えるのかを見る）");
	{
		const ProbeStyle* cases[] = {&styleTall, &styleTallByInstance};
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		{
			const ProbeStyle& style = *cases[i];
			if (style.ref == 0)
				continue;
			MCObjectHandle pio = CreateTarget();
			if (pio == nullptr)
			{
				probe.fail("E: 対象を作れなかった");
				continue;
			}
			probe.log("  " + style.label);
			WriteFlatBounds(pio);
			gSDK->SetPluginObjectStyle(pio, style.ref);
			const Snap before = Read(pio);
			LogSnap(probe, "    ", "流す前 ", before);
			gSDK->UpdateStyledObjects(style.ref);
			const Snap after = Read(pio);
			LogSnap(probe, "    ", "流した後", after);
			probe.log(std::string("    → 判定: 書いた ") + Num(kElevLow) + "/" + Num(kElevLow) +
					  " のままか: " +
					  (Near(after.resolved[0], kElevLow) && Near(after.resolved[1], kElevLow)
						   ? "**はい**"
						   : "**いいえ（`ResetObject` を呼ばなくても上書きされる）**"));
		}
	}

	// =======================================================================
	probe.log("=== F. バウンドを 1 本も持たない材にスタイルを当てるとバウンドが生えるか ===");
	if (styleTall.ref != 0)
	{
		MCObjectHandle pio = CreateTarget();
		if (pio == nullptr)
		{
			probe.fail("F: 対象を作れなかった");
		}
		else
		{
			const Snap born = Read(pio);
			LogSnap(probe, "    ", "作った直後", born);
			gSDK->SetPluginObjectStyle(pio, styleTall.ref);
			const Snap applied = Read(pio);
			LogSnap(probe, "    ", "当てた直後", applied);
			gSDK->ResetObject(pio);
			const Snap reset = Read(pio);
			LogSnap(probe, "    ", "Reset の後 ", reset);
			probe.log(std::string("    → 判定: バウンドの件数 ") + Count(born.boundCount) + " → " +
					  Count(applied.boundCount) + " → " + Count(reset.boundCount) +
					  "。増えていれば**スタイルがバウンドを配っている**と言い切れる");
		}
	}

	probe.log("=== まとめ ===");
	probe.log("  読む順: B（誰が書くか）→ C（順序）→ D（スタイル別）→ E（UpdateStyledObjects）"
			  "→ F（バウンドが生えるか）");
	probe.log("  知りたいのは 2 つ: **バウンドとスタイルはどちらを先に書くか**と、"
			  "**バウンドの検査をどの時点で行うべきか**");
}
