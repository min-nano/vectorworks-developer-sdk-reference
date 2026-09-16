//
//	probes/runtime/reset-object-path-rebuild-threshold/probe.cpp
//
//	[issue #61] **`ResetObject` は「どんなパスなら作り直すのか」——作り直すかどうかを
//	分けているのが `CreateCustomObjectPath` で渡したパスの長さ（退化の度合い）なのか、
//	分けるならその閾値はいくつで、絶対値なのか座標の大きさに対する相対なのか。**
//
//	実機の事故（あちらの PR #113）では、同じ取り込みの中で柱 197 本のうち 46 本だけが
//	長さ 0 で描かれた。3 地点の読み戻しで分かっているのは次の形である（issue #61 の表）:
//
//	    　　　　　│ ① Create 直後 │ ② バウンド 2 本の直後 │ ③ ResetObject 直後
//	    潰れる個体│ 4.54747e-13   │ 4.54747e-13           │ 4.54747e-13（作り直されていない）
//	    無事な個体│ 0             │ 0                     │ 2843（作り直された）
//
//	潰れた個体でも ③ で `GetObjectBoundElevation` は 572 / 3531 と**正しく解決されている**
//	（バウンドは白。#59 で確定）。**唯一の差は ① のパスが厳密に 0 長か、1 ULP だけ
//	非ゼロか**（`4.54747e-13` = 2^-41 = 2048〜4096 の double の 1 ULP）。
//
//	## いま分かっていること（`Findings/Parametric Objects.md`）と、噛み合わないところ
//
//	* 長さ **2959** のまともなパスは作り直された（#56 の 3）。
//	* 長さ **1** のでたらめなパス（世界座標 `0→1`）も作り直された（#56 の 5）。
//	* 長さ **0** のパスも作り直された（上の「無事な個体」。バウンドが 2 本あるとき）。
//	* しかし長さ **1 ULP** のパスは作り直されない（上の「潰れる個体」）。
//
//	つまり「非ゼロなら温存する」でも「短いほど作り直す」でもない——**0 と 1 の間の
//	どこかに、作り直しが止まる帯がある**という形になる。この帯の在処と縁を、
//	1 本の図面の中で**長さだけを振って**測るのがこのプローブである。
//
//	## 測り方の方針
//
//	* **長さ以外はすべて同じにする。** バウンド 2 本・レイヤ・向きを固定し、
//	  `CreateCustomObjectPath` へ渡す 2 点目の Z だけを振る。1 ケース 1 オブジェクト
//	  （前のケースの残りを持ち込まない）。
//	* **どんな図面でも走る。** 必要なのは「**異なる 2 つの絶対Zへ解決されるバウンド 2 本**」
//	  だけで、それは階が無くても `eStoryObjectBound_LayerElevation` ＋ `fOffset` で作れる
//	  （#59 の実測: `{LayerElevation, …, offset −40}` はレイヤ高さ 572 に対して 532 へ
//	  解決した＝**レイヤ高さ＋offset**）。階が 2 つ以上ある図面なら、実務と同じ
//	  `eStoryObjectBound_Story`（#59 の A の形）を選ぶ。**どちらで走ったかは必ずログへ出す。**
//	  この問いの主語はパス側なので、バウンドの作り方が変わっても答えは変わらない
//	  ——その前提自体も、下の「T. 物差し」でまともなパスが作り直されることを確かめて裏を取る。
//	* **振り幅は 0 から 100 まで 11 段**。うち 2 段は**基準 Z の 1 ULP / 4 ULP**
//	  （`std::nextafter`）で、事故と同じ「ビットでしか違わない」領域を直接踏む。
//	* **桁を丸めて印字しない。** 4.5e-13 は `%f` では `0.000000` になる。全部 `%.17g` と
//	  16 進（`%a`）の両方で出す——**ビット一致かどうかがこの調査の主語**だから。
//	* **絶対値か相対かは、基準 Z を 1000 倍にした同じ振り**（群 C）で分ける。閾値が
//	  座標の大きさに比例して動くなら相対、動かないなら絶対。
//	* **Z 以外の長さも効くか**を群 D で見る（dz は 1 ULP のまま dx を 1000 にする＝
//	  曲線としての長さは十分ある）。効くなら判定は「曲線の長さ」、効かないなら「Z の差」。
//	* **差し替え経路（`SetCustomObjectPath`）にも同じ判定が掛かるか**を群 E で見る
//	  （issue #61 の問い 3）。
//
//	【読み方】最後に出る「まとめ」の表がこのプローブの結論そのもの。各行の「③ の z1-z0」が
//	  * **±(上端の解決Z − 下端の解決Z)** … 作り直された（バウンドが実体を決めた）
//	  * **① と同じ値**                    … 作り直されなかった（呼び出し側のパスが温存された）
//	  * **0**                             … 潰れた
//	のどれかになる。作り直された／されなかったの境目が閾値である。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace
{
	// **丸めて出さない。** 1 ULP の差がこの調査の主語なので、10 進 17 桁と 16 進の
	// 両方を出す（16 進のほうはビット一致の判定に使える）。
	std::string Num(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.17g", value);
		return std::string(buffer);
	}

	std::string NumBits(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%a", value);
		return std::string(buffer);
	}

	std::string NumBoth(double value)
	{
		return Num(value) + " [" + NumBits(value) + "]";
	}

	std::string Str(const TXString& text)
	{
		return std::string(static_cast<const char*>(text));
	}

	// 基準値から n ULP だけ上（下）の double。「ビットでしか違わない 2 点」を作る唯一の
	// 正しい手立て（1e-13 のような定数を書くと、基準値の大きさによって 0 ULP にも
	// 数 ULP にもなってしまう）。
	double UlpsAway(double base, int ulps)
	{
		double value = base;
		const double direction = ulps >= 0 ? HUGE_VAL : -HUGE_VAL;
		const int steps = ulps >= 0 ? ulps : -ulps;
		for (int i = 0; i < steps; ++i)
			value = std::nextafter(value, direction);
		return value;
	}

	MockUp::SStoryObjectData MakeBound(MockUp::EStoryObjectBound bound, int story,
									   const TXString& levelType, WorldCoord offset)
	{
		MockUp::SStoryObjectData data;
		data.fBound = bound;
		data.fBoundStory = static_cast<Sint8>(story);
		data.fLayerLevelType = levelType;
		data.fOffset = offset;
		return data;
	}

	// パスの両端（**局所座標**。#56 で確定: Create は最初の点を挿入点にして相対で持つ）。
	bool ReadPathEndpoints(MCObjectHandle pio, WorldPt3& outP0, WorldPt3& outP1)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
			return false;
		const Boolean got0 = gSDK->NurbsGetPt3D(path, 0, 0, outP0);
		const Boolean got1 = gSDK->NurbsGetPt3D(path, 0, 1, outP1);
		return got0 && got1;
	}

	// 「いまのパスの z1−z0」。読めなければ NaN を返す（読めなかったことが分かるように）。
	double PathDeltaZ(MCObjectHandle pio)
	{
		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(pio, p0, p1))
			return std::nan("");
		return p1.z - p0.z;
	}

	void LogPath(vwprobe::Report& probe, const std::string& prefix, MCObjectHandle pio)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
		{
			probe.log(prefix + " GetCustomObjectPath が nil を返した");
			return;
		}
		// **点の数も出す。** ビットでしか違わない 2 点が 1 点へ畳まれているなら、
		// 「作り直されない」の理由はそこにある（2 点のままなら別の理由）。
		const Sint32 pointCount = gSDK->NurbsGetNumPts(path, 0);
		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(pio, p0, p1))
		{
			probe.log(prefix + " パスの点が読めない（NurbsGetNumPts=" +
					  std::to_string(static_cast<long long>(pointCount)) +
					  " / NurbsGetPt3D が false）");
			return;
		}
		VWParametricObj obj(pio);
		const WorldPt3 pos = obj.GetObjectModelPos();
		probe.log(prefix + " 点数=" + std::to_string(static_cast<long long>(pointCount)) +
				  " パス[0]=(" + Num(p0.x) + ", " + Num(p0.y) + ", " + Num(p0.z) + ") パス[1]=(" +
				  Num(p1.x) + ", " + Num(p1.y) + ", " + Num(p1.z) + ")");
		probe.log(prefix + " **z1-z0=" + NumBoth(p1.z - p0.z) + "** 挿入点Z=" + Num(pos.z) +
				  " 端点の絶対Z: [0]=" + Num(pos.z + p0.z) + " [1]=" + Num(pos.z + p1.z));
	}

	MCObjectHandle MakeStraightPath(WorldPt3 from, WorldPt3 to)
	{
		MCObjectHandle curve = gSDK->CreateNurbsCurve(from, false, 1);
		if (curve == nullptr)
			return nullptr;
		gSDK->Add3DVertex(curve, to, true);
		return curve;
	}

	struct StoryInfo
	{
		MCObjectHandle handle = nullptr;
		TXString name;
		WorldCoord elevation = 0;
		MCObjectHandle anyLayer = nullptr;
	};

	// **レイヤの列挙は ForEachLayerN**（#56 で確定）。
	std::vector<StoryInfo> CollectStories(vwprobe::Report& probe)
	{
		std::vector<StoryInfo> stories;
		gSDK->ForEachLayerN(
			[&](MCObjectHandle layer)
			{
				MCObjectHandle story = gSDK->GetStoryOfLayer(layer);
				if (story == nullptr)
					return;
				for (size_t i = 0; i < stories.size(); ++i)
					if (stories[i].handle == story)
						return;
				StoryInfo info;
				info.handle = story;
				gSDK->GetObjectName(story, info.name);
				info.elevation = gSDK->GetStoryElevation(story);
				info.anyLayer = layer;
				stories.push_back(info);
			});
		probe.log("P. ストーリ " + std::to_string(stories.size()) +
				  " 件（GetNumStories = " + std::to_string(gSDK->GetNumStories()) + "）");
		return stories;
	}

	// まとめの表 1 行ぶん。
	struct Outcome
	{
		std::string label;
		double requestedDeltaZ = 0;	  // 渡したかった Z の差
		double createdDeltaZ = 0;	  // ① Create 直後に実際に読めた z1−z0
		double afterBoundsDeltaZ = 0; // ② バウンドを書いた直後
		double afterResetDeltaZ = 0;  // ③ ResetObject 後
		std::string verdict;
	};
} // namespace

VW_PROBE("reset-object-path-rebuild-threshold", "ResetObject がパスを作り直す／温存する境目を測る",
		 "CreateCustomObjectPath へ渡す 2 点の Z の差だけを 0 から 100 まで振り（1 ULP・"
		 "4 ULP を含む）、同じ 2 本のバウンドを書いて ResetObject したときに、パスが"
		 "作り直されるか温存されるかを読み比べる。新規の空図面でも走る")
{
	// =======================================================================
	probe.log("=== P. バウンドの作り方を決める（図面には階もレイヤも足さない） ===");

	MockUp::SStoryObjectData boundLower;
	MockUp::SStoryObjectData boundUpper;
	std::string boundMode;

	{
		std::vector<StoryInfo> stories = CollectStories(probe);

		std::vector<TXString> allLevelTypes;
		// **添字は 1 始まり**（0 は空文字。#56 で確定）。
		const short levelTypeCount = gSDK->GetNumLayerLevelTypes();
		for (short i = 1; i <= levelTypeCount; ++i)
		{
			const TXString type = gSDK->GetLayerLevelTypeName(i);
			if (!Str(type).empty())
				allLevelTypes.push_back(type);
		}
		probe.log("P. レベル種別 " + std::to_string(allLevelTypes.size()) + " 件");

		// 階が 2 つ以上あって同じレベル種別を両階が持つなら、実務と同じ形（#59 の A）で測る。
		StoryInfo lower;
		StoryInfo upper;
		TXString levelType;
		for (size_t i = 0; i < stories.size() && lower.handle == nullptr; ++i)
		{
			MCObjectHandle above = gSDK->GetStoryAbove(stories[i].handle);
			if (above == nullptr)
				continue;
			for (size_t j = 0; j < stories.size() && lower.handle == nullptr; ++j)
			{
				if (stories[j].handle != above)
					continue;
				for (size_t k = 0; k < allLevelTypes.size(); ++k)
				{
					if (gSDK->GetLayerForStory(stories[i].handle, allLevelTypes[k]) == nullptr)
						continue;
					if (gSDK->GetLayerForStory(stories[j].handle, allLevelTypes[k]) == nullptr)
						continue;
					lower = stories[i];
					upper = stories[j];
					levelType = allLevelTypes[k];
					break;
				}
			}
		}

		if (lower.handle != nullptr)
		{
			boundMode = "Story";
			boundLower = MakeBound(MockUp::eStoryObjectBound_Story, 0, levelType, 0);
			boundUpper = MakeBound(MockUp::eStoryObjectBound_Story, 1, levelType, 0);
			MCObjectHandle container = gSDK->GetLayerForStory(lower.handle, levelType);
			if (container == nullptr)
				container = lower.anyLayer;
			if (container != nullptr)
				gSDK->SetCurrentLayer(container);
			TXString layerName;
			if (container != nullptr)
				gSDK->GetObjectName(container, layerName);
			probe.log("P. **Story モードで測る**（実務と同じ形）。下=\"" + Str(lower.name) + "\"(" +
					  Num(lower.elevation) + ") 上=\"" + Str(upper.name) + "\"(" +
					  Num(upper.elevation) + ") レベル種別 L=\"" + Str(levelType) +
					  "\" 置くレイヤ=\"" + Str(layerName) + "\"");
		}
		else
		{
			// **階が無くても測れる。** 要るのは「異なる 2 つの絶対Zへ解決されるバウンド
			// 2 本」だけで、それはレイヤ高さ＋offset で作れる（#59 の実測）。offset を
			// 2500 / 5500 にするのは、**基準Zを 2048〜4096 の帯に置く**ため——事故で
			// 踏んだ 1 ULP（4.54747e-13 = 2^-41）とちょうど同じ大きさの ULP になる。
			boundMode = "LayerElevation";
			boundLower = MakeBound(MockUp::eStoryObjectBound_LayerElevation, 0, TXString(), 2500);
			boundUpper = MakeBound(MockUp::eStoryObjectBound_LayerElevation, 0, TXString(), 5500);
			probe.log("P. **LayerElevation モードで測る**（この図面には「同じレベル種別を"
					  "両方が持つ隣り合う 2 階」が無い）。バウンドは ID0={LayerElevation, "
					  "offset=2500} / ID1={LayerElevation, offset=5500}——レイヤ高さ＋offset で"
					  "**異なる 2 つの絶対Z**へ解決される（#59 の実測）。");
			probe.log("P. この問いの主語は**パス側**なので、バウンドの作り方が変わっても"
					  "答えは変わらない。その前提は次の「T. 物差し」で裏を取る"
					  "（まともなパスがこのモードで作り直されるか）。");
			MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
			TXString layerName;
			if (currentLayer != nullptr)
				gSDK->GetObjectName(currentLayer, layerName);
			probe.log("P. 置くレイヤ（いまのレイヤ）: \"" + Str(layerName) +
					  "\"（レイヤ高さを読む口は無いので、実際の解決Zは次の T で測る）");
		}
	}

	// =======================================================================
	probe.log("=== T. 物差し（このモードで「まともなパス」が作り直されるかを先に確かめる） ===");
	// ここで測るもの: 下端／上端の解決Z（＝作り直されたときの長さ）と、振る基準Z。
	double resolvedLower = 0;
	double resolvedUpper = 0;
	{
		MCObjectHandle path = MakeStraightPath(WorldPt3(0, 0, 0), WorldPt3(0, 0, 1000));
		MCObjectHandle pio = path != nullptr
								 ? gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr)
								 : nullptr;
		if (pio == nullptr)
		{
			probe.fail("T. CreateCustomObjectPath(\"StructuralMember\") が nil を返した"
					   "（構造材 PIO がこの図面で作れない）。以降は測れない。");
			return;
		}
		LogPath(probe, "T. ① Create 直後:", pio);
		const bool setLower = gSDK->SetObjectStoryBound(pio, 0, boundLower);
		const bool setUpper = gSDK->SetObjectStoryBound(pio, 1, boundUpper);
		resolvedLower = gSDK->GetObjectBoundElevation(pio, 0);
		resolvedUpper = gSDK->GetObjectBoundElevation(pio, 1);
		probe.log(std::string("T. SetObjectStoryBound: ID0=") + (setLower ? "true" : "false") +
				  " ID1=" + (setUpper ? "true" : "false") + " ／ 件数=" +
				  std::to_string(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(pio))) +
				  " ／ **解決Z: ID0=" + Num(resolvedLower) + " ID1=" + Num(resolvedUpper) +
				  " 差=" + Num(resolvedUpper - resolvedLower) + "**");
		gSDK->ResetObject(pio);
		LogPath(probe, "T. ③ ResetObject 後:", pio);
		const double afterReset = PathDeltaZ(pio);
		probe.log("T. 物差しの判定: " +
				  std::string(std::fabs(std::fabs(afterReset) -
										std::fabs(resolvedUpper - resolvedLower)) <= 1e-6
								  ? "**まともなパスは作り直された**（このモードで測れる）"
								  : "**作り直されなかった**——以降の「作り直されない」は"
									"パスのせいとは言えない。ログをそのまま読むこと"));
	}

	if (resolvedUpper - resolvedLower == 0)
	{
		probe.fail("T. 2 本のバウンドが同じ絶対Zへ解決された（差が 0）。これでは"
				   "「作り直された」と「潰れた」が見分けられない。ログの解決Zを見て、"
				   "バウンドの作り方を直す必要がある。");
		return;
	}

	const double expectedHeight = resolvedUpper - resolvedLower;
	// 振りの基準Z。**0 の近くでは 1 ULP が意味を持たない**（denormal）ので、解決Zが
	// 小さすぎるときだけ 2500 へ逃がす（パスの絶対Zは結果に効かない。#56 の 5）。
	const double pathBase = std::fabs(resolvedLower) >= 1 ? resolvedLower : 2500;
	probe.log("T. 作り直されたときの長さ = " + Num(expectedHeight) + " ／ 振りの基準Z = " +
			  NumBoth(pathBase) + "（1 ULP = " + Num(UlpsAway(pathBase, 1) - pathBase) + "）");

	std::vector<Outcome> outcomes;

	// 1 ケース＝1 オブジェクト。① Create 直後 → ② バウンド 2 本 → ③ ResetObject の
	// 3 地点で z1−z0 を読む（issue #61 の表と同じ採り方）。
	auto runCase = [&](const std::string& label, double baseZ, double deltaX, double deltaZ,
					   const std::string& why)
	{
		probe.log("--- " + label + " ---");
		probe.log("  ねらい: " + why);
		const double z0 = baseZ;
		const double z1 = baseZ + deltaZ;
		probe.log("  渡す世界座標: (0, 0, " + NumBoth(z0) + ") → (" + Num(deltaX) + ", 0, " +
				  NumBoth(z1) + ") ／ 実際の Z の差=" + NumBoth(z1 - z0));

		Outcome outcome;
		outcome.label = label;
		outcome.requestedDeltaZ = z1 - z0;

		MCObjectHandle path = MakeStraightPath(WorldPt3(0, 0, z0), WorldPt3(deltaX, 0, z1));
		if (path == nullptr)
		{
			probe.log("  CreateNurbsCurve が nil を返した");
			return;
		}
		MCObjectHandle pio = gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr);
		if (pio == nullptr)
		{
			probe.log("  CreateCustomObjectPath が nil を返した");
			return;
		}

		LogPath(probe, "  ① Create 直後:", pio);
		outcome.createdDeltaZ = PathDeltaZ(pio);

		const bool setLower = gSDK->SetObjectStoryBound(pio, 0, boundLower);
		const bool setUpper = gSDK->SetObjectStoryBound(pio, 1, boundUpper);
		const WorldCoord elevationLower = gSDK->GetObjectBoundElevation(pio, 0);
		const WorldCoord elevationUpper = gSDK->GetObjectBoundElevation(pio, 1);
		probe.log(std::string("  SetObjectStoryBound: ID0=") + (setLower ? "true" : "false") +
				  " ID1=" + (setUpper ? "true" : "false") + " ／ 件数=" +
				  std::to_string(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(pio))) +
				  " ／ 解決Z: ID0=" + Num(elevationLower) + " ID1=" + Num(elevationUpper) +
				  " 差=" + Num(elevationUpper - elevationLower));
		LogPath(probe, "  ② バウンド 2 本の直後:", pio);
		outcome.afterBoundsDeltaZ = PathDeltaZ(pio);

		gSDK->ResetObject(pio);
		LogPath(probe, "  ③ ResetObject 後:", pio);
		outcome.afterResetDeltaZ = PathDeltaZ(pio);

		// 判定は「作り直されたか」の 1 点。**そのオブジェクト自身の解決Zの差**と比べる
		// （ケースごとに読んでいるので、物差しと食い違っていてもそのまま読める）。符号は
		// 作り方に依るので絶対値で比べる（#59 で ID0=始点 / ID1=終点と確定している）。
		const double expected = elevationUpper - elevationLower;
		const double after = outcome.afterResetDeltaZ;
		if (after != after) // NaN
			outcome.verdict = "パスが読めない";
		else if (expected == 0)
			outcome.verdict = "判定不能（このケースはバウンドが同じ絶対Zへ解決された）";
		else if (std::fabs(std::fabs(after) - std::fabs(expected)) <= 1e-6)
			outcome.verdict = "作り直された";
		else if (after == outcome.createdDeltaZ)
			outcome.verdict = "**温存された（作り直されていない）**";
		else if (after == 0)
			outcome.verdict = "0 へ潰れた";
		else
			outcome.verdict = "そのどれでもない";
		probe.log("  → 判定: " + outcome.verdict);
		outcomes.push_back(outcome);
	};

	// =======================================================================
	probe.log("=== A. 長さだけを振る（ここがこのプローブの本体） ===");
	{
		struct Step
		{
			const char* label;
			int ulps;		 // 非 0 ならこの ULP 数で作る（absolute は無視）
			double absolute; // ULP 指定でないときの絶対値
			const char* why;
		};
		const Step steps[] = {
			{"A-00 Z の差 = 0（厳密に退化）", 0, 0,
			 "事故で無事だった個体と同じ形。作り直されるはず（対照）"},
			{"A-01 Z の差 = 1 ULP", 1, 0,
			 "事故で潰れた個体と同じ形。ここが温存されるなら「ビット一致かどうか」が境目"},
			{"A-02 Z の差 = 4 ULP", 4, 0, "1 ULP が特別なのか、その近傍がまとめて特別なのか"},
			{"A-03 Z の差 = 1e-11", 0, 1e-11, "ULP より上・実用よりはるか下"},
			{"A-04 Z の差 = 1e-9", 0, 1e-9, "同上"},
			{"A-05 Z の差 = 1e-7", 0, 1e-7, "図形の許容値としてありがちな帯の下端"},
			{"A-06 Z の差 = 1e-5", 0, 1e-5, "同上"},
			{"A-07 Z の差 = 1e-3", 0, 1e-3, "図面の単位で「ほぼ 0」だが計算では有意"},
			{"A-08 Z の差 = 1e-1", 0, 1e-1, "実用の下限"},
			{"A-09 Z の差 = 1", 0, 1, "#56 の 5 が「作り直された」と報告している長さ"},
			{"A-10 Z の差 = 100", 0, 100, "まともな長さ（対照。作り直されるはず）"},
		};
		for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
		{
			const Step& step = steps[i];
			const double deltaZ =
				step.ulps != 0 ? UlpsAway(pathBase, step.ulps) - pathBase : step.absolute;
			runCase(step.label, pathBase, 0, deltaZ, step.why);
		}
	}

	// =======================================================================
	probe.log("=== C. 同じ振りを 1000 倍の高さで（閾値は絶対値か、座標に対する相対か） ===");
	{
		const double baseZ = pathBase * 1000 + 1e6; // 図面から遠い、桁の大きい座標
		const double deltas[] = {0, 1e-9, 1e-7, 1e-5, 1e-3};
		for (size_t i = 0; i < sizeof(deltas) / sizeof(deltas[0]); ++i)
			runCase("C-" + std::to_string(i) + " 基準Z=" + Num(baseZ) +
						" / Z の差=" + Num(deltas[i]),
					baseZ, 0, deltas[i],
					"A の同じ差と結果が変われば閾値は座標の大きさに対する相対、"
					"変わらなければ絶対");
		// 1 ULP も（基準Zが大きいぶん ULP 自体が大きくなる）。
		runCase("C-5 基準Z=" + Num(baseZ) + " / Z の差=1 ULP", baseZ, 0, UlpsAway(baseZ, 1) - baseZ,
				"A-01 と同じ「ビットで隣」。ULP の絶対値だけが違う");
	}

	// =======================================================================
	probe.log("=== D. Z の差は 1 ULP のまま、曲線としての長さを与える（何を見ているか） ===");
	{
		runCase("D-0 dx=1000 / Z の差=1 ULP", pathBase, 1000, UlpsAway(pathBase, 1) - pathBase,
				"判定が「曲線の長さ」なら作り直される。「Z の差」なら A-01 と同じ結果になる");
		runCase("D-1 dx=1000 / Z の差=0", pathBase, 1000, 0,
				"水平材の形。0 長ではないパスを厳密に水平で渡したときの対照");
	}

	// =======================================================================
	probe.log("=== E. 差し替え経路（SetCustomObjectPath）にも同じ判定が掛かるか（問い 3） ===");
	// **`SetCustomObjectPath` は変換をしない**（#56 の 2）ので、渡すのは挿入点からの
	// 相対＝局所座標。ここでは「まともに作り直された個体のパスを、退化したものへ
	// 差し替えてもう一度 ResetObject する」形で見る。
	{
		struct Replacement
		{
			const char* label;
			int ulps;
			double absolute;
			const char* why;
		};
		const Replacement replacements[] = {
			{"E-0 差し替えるパスの Z の差 = 0（厳密に退化）", 0, 0,
			 "作り直されれば、差し替え経路にも「退化なら作り直す」が掛かっている"},
			{"E-1 差し替えるパスの Z の差 = 1 ULP", 1, 0,
			 "A-01 と同じ結果なら、判定は経路に依らず「いまのパスの形」だけで決まる"},
			{"E-2 差し替えるパスの Z の差 = 100", 0, 100,
			 "対照。まともなパスへ差し替えたあとの ResetObject は何をするか"},
		};
		for (size_t i = 0; i < sizeof(replacements) / sizeof(replacements[0]); ++i)
		{
			const Replacement& replacement = replacements[i];
			probe.log(std::string("--- ") + replacement.label + " ---");
			probe.log(std::string("  ねらい: ") + replacement.why);

			MCObjectHandle path =
				MakeStraightPath(WorldPt3(0, 0, pathBase), WorldPt3(0, 0, pathBase + 100));
			MCObjectHandle pio =
				path != nullptr ? gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr)
								: nullptr;
			if (pio == nullptr)
			{
				probe.log("  CreateCustomObjectPath が nil を返した");
				continue;
			}
			gSDK->SetObjectStoryBound(pio, 0, boundLower);
			gSDK->SetObjectStoryBound(pio, 1, boundUpper);
			gSDK->ResetObject(pio);
			LogPath(probe, "  ③ まず正常に作り直させた:", pio);
			const double beforeReplace = PathDeltaZ(pio);

			// 差し替えるのは局所座標（挿入点は動かさない）。ULP 指定のときは、事故と同じ
			// 「ビットで隣」を**基準Zの高さのところ**で作ってから差へ落とす。
			const double deltaZ = replacement.ulps != 0
									  ? UlpsAway(pathBase, replacement.ulps) - pathBase
									  : replacement.absolute;
			MCObjectHandle newPath = MakeStraightPath(WorldPt3(0, 0, 0), WorldPt3(0, 0, deltaZ));
			if (newPath == nullptr)
			{
				probe.log("  差し替え用の CreateNurbsCurve が nil を返した");
				continue;
			}
			const bool replaced = gSDK->SetCustomObjectPath(pio, newPath);
			probe.log(std::string("  SetCustomObjectPath=") + (replaced ? "true" : "false") +
					  " 渡した局所座標の Z の差=" + NumBoth(deltaZ));
			LogPath(probe, "  ④ 差し替えた直後:", pio);
			const double afterReplace = PathDeltaZ(pio);

			gSDK->ResetObject(pio);
			LogPath(probe, "  ⑤ もう一度 ResetObject:", pio);

			Outcome outcome;
			outcome.label = replacement.label;
			outcome.requestedDeltaZ = deltaZ;
			outcome.createdDeltaZ = afterReplace;
			outcome.afterBoundsDeltaZ = beforeReplace; // ここだけ「差し替える前の長さ」
			outcome.afterResetDeltaZ = PathDeltaZ(pio);
			const double after = outcome.afterResetDeltaZ;
			if (after != after)
				outcome.verdict = "パスが読めない";
			else if (std::fabs(std::fabs(after) - std::fabs(expectedHeight)) <= 1e-6)
				outcome.verdict = "作り直された";
			else if (after == outcome.createdDeltaZ)
				outcome.verdict = "**温存された（作り直されていない）**";
			else if (after == 0)
				outcome.verdict = "0 へ潰れた";
			else
				outcome.verdict = "そのどれでもない";
			probe.log("  → 判定: " + outcome.verdict);
			outcomes.push_back(outcome);
		}
	}

	// =======================================================================
	probe.log("=== まとめ（この表が結論） ===");
	probe.log("バウンドの作り方: " + boundMode + " ／ 解決Z: 下端=" + Num(resolvedLower) +
			  " 上端=" + Num(resolvedUpper) + " ／ 作り直されたときの長さ=" + Num(expectedHeight));
	probe.log("ケース | 渡した Z の差 | ① Create 直後 | ② バウンド後 | ③ Reset 後 | 判定");
	for (size_t i = 0; i < outcomes.size(); ++i)
	{
		const Outcome& outcome = outcomes[i];
		probe.log(outcome.label + " | " + Num(outcome.requestedDeltaZ) + " | " +
				  Num(outcome.createdDeltaZ) + " | " + Num(outcome.afterBoundsDeltaZ) + " | " +
				  Num(outcome.afterResetDeltaZ) + " | " + outcome.verdict);
	}
	probe.log("（E の行だけは ② の欄が「差し替える前の長さ」、① の欄が「差し替えた直後の"
			  "長さ」を指す）");
	probe.log("読み方: 「作り直された」と「温存された」の境目にある 2 行が閾値を挟む。"
			  "A と C で同じ絶対値の差が同じ判定になれば閾値は**絶対**、C だけ判定が動けば"
			  "**座標の大きさに対する相対**。D-0 が A-01 と違えば判定は Z の差ではなく"
			  "**曲線の長さ**を見ている。E は差し替え経路に同じ判定が掛かるかどうか。");
}
