//
//	probes/runtime/story-bound-rewrite-on-path-replace/probe.cpp
//
//	[issue #63] **`SetCustomObjectPath` で差し替えた後の `ResetObject` は、
//	`eStoryObjectBound_Story` のバウンドも書き換えるのか。** 書き換えるなら、
//	**ID 0 側も動くのか**、そして**書き換わった後もその部材は階に追従するのか**。
//
//	## 出発点（issue #61 / PR #62 で実機確認済み）
//
//	一度 `ResetObject` を通した個体のパスを `SetCustomObjectPath` で差し替えると、
//	**その後の `ResetObject` はパスを作り直さず、逆に ID 1 のバウンドのレコードを
//	差し替えたパスの上端へ合わせて書き換える**（`Findings/Parametric Objects.md`
//	「`SetCustomObjectPath` で差し替えたパスには、この判定が掛からない」）:
//
//	    差し替えたパス │ ResetObject 後のパス │ ID 1 の fOffset（差し替え前は 5500）
//	    0 長           │ 0 長のまま           │ 2500
//	    1 ULP          │ 1 ULP のまま         │ 2500.0000000000005
//	    100            │ 100 のまま           │ 2600
//
//	**ただしこれは `eStoryObjectBound_LayerElevation` のバウンドでの実測**（新規の空図面で
//	走らせたため階が無く、`LayerElevation` へ落ちた）。実務で使うのは
//	`eStoryObjectBound_Story`（他階基準＋レベル種別）で、そちらは `fOffset` のほかに
//	`fBound` / `fBoundStory` / `fLayerLevelType` を持つ——**書き換えが `fOffset` だけで
//	済むのか、`fBound` ごと `LayerElevation` へ落ちるのか**で、実務上の意味がまるで違う:
//
//	  * `fBound` が `Story` のまま `fOffset` だけ動く → **階への従属は残る**
//	    （その階が動けば部材も動く。ずれた分だけ位置が狂う）
//	  * `fBound` が `LayerElevation` へ落ちる       → **その部材は以後その階に追従しない**
//	    （差し替えを対症療法に使っているコードは書き直しが要る）
//
//	## この 1 回で答えを出す 3 つの問い（issue #63）
//
//	1. `{Story, 上階, <レベル種別>, 0}` を書いた個体でパスを差し替えて `ResetObject` すると、
//	   **ID 1 のレコードはどうなるか**（`fOffset` だけが動くのか、`fBound` が落ちるのか）。
//	2. **ID 0 側は書き換わるか。** `LayerElevation` での実測では ID 0 は動かなかったが、
//	   **差し替えたパスの始点が常に挿入点（＝ID 0 の解決Z）と一致していた**ので、
//	   「動かない」のか「動いた先がたまたま同じ」のかが区別できていない。
//	   ここでは**始点が挿入点と一致しないパス**（局所 `100 → 200` など）へ差し替えて分ける。
//	3. 書き換わった後の**レコードそのもの**（`fBound` / `fBoundStory` / `fLayerLevelType` /
//	   `fOffset`）と解決Zを並べて、階への従属が残るかを読む。
//
//	## 測り方
//
//	* **1 ケース 1 オブジェクト。** 毎回「長さ 100 のパスで作る → バウンド 2 本 →
//	  `ResetObject`（③ ここで正常に作り直される）→ `SetCustomObjectPath` で差し替え（④）
//	  → もう一度 `ResetObject`（⑤）」を通し、**③ ④ ⑤ の 3 地点でバウンドのレコードを
//	  丸ごと出す**（`fBound` / `fBoundStory` / `fLayerLevelType` / `fOffset` / 解決Z）。
//	* **差し替えるパスは局所座標**（`SetCustomObjectPath` は変換をしない。#56 の 2）。
//	  ③ の後の挿入点は ID 0 の解決Z なので、局所 0 が「ID 0 の解決Z そのもの」に当たる。
//	* **振るのは差し替えるパスの両端だけ**（下の R-0〜R-6）。始点を挿入点からずらした形
//	  （R-3 / R-4 / R-5）が問い 2 の切り分けそのもので、ID 0 が「始点に合わせて動く」なら
//	  ここで初めて値が変わる。
//	* **`_Story` モードで走らなければ、この調査の答えは出ない。** 階が 2 つ以上あって
//	  同じレベル種別を両階が持つ図面が要る（事故のモデルがそれ）。無い図面で走った場合は
//	  `LayerElevation` で同じ測定をした上で**失敗として返す**——ログは残るが、
//	  問い 1 には答えていないことがひと目で分かるように。
//
//	【読み方】最後の「まとめ」の表がこのプローブの結論。各行の ID 0 / ID 1 の欄が
//	「変化なし」なら書き換わっていない。変化していれば**何がどう変わったか**（`fOffset`
//	だけか、`fBound` ごとか）がそのまま出る。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// **丸めて出さない。** 1 ULP の差もこの調査に出てくる（#61 の続きなので）。
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

	std::string Int(long long value)
	{
		return std::to_string(value);
	}

	// 基準値から n ULP だけ離れた double（「ビットでしか違わない 2 点」を作る唯一の
	// 正しい手立て。#61 で使ったものと同じ）。
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

	const char* BoundKindName(MockUp::EStoryObjectBound bound)
	{
		switch (bound)
		{
		case MockUp::eStoryObjectBound_LayerElevation:
			return "LayerElevation";
		case MockUp::eStoryObjectBound_LayerWallHeight:
			return "LayerWallHeight";
		case MockUp::eStoryObjectBound_Story:
			return "Story";
		}
		return "?";
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

	double PathDeltaZ(MCObjectHandle pio)
	{
		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(pio, p0, p1))
			return std::nan("");
		return p1.z - p0.z;
	}

	double InsertionZ(MCObjectHandle pio)
	{
		VWParametricObj obj(pio);
		return obj.GetObjectModelPos().z;
	}

	void LogPath(vwprobe::Report& probe, const std::string& prefix, MCObjectHandle pio)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
		{
			probe.log(prefix + " GetCustomObjectPath が nil を返した");
			return;
		}
		const Sint32 pointCount = gSDK->NurbsGetNumPts(path, 0);
		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(pio, p0, p1))
		{
			probe.log(prefix + " パスの点が読めない（NurbsGetNumPts=" +
					  Int(static_cast<long long>(pointCount)) + " / NurbsGetPt3D が false）");
			return;
		}
		const double insertion = InsertionZ(pio);
		probe.log(prefix + " 点数=" + Int(static_cast<long long>(pointCount)) + " パス[0]=(" +
				  Num(p0.x) + ", " + Num(p0.y) + ", " + Num(p0.z) + ") パス[1]=(" + Num(p1.x) +
				  ", " + Num(p1.y) + ", " + Num(p1.z) + ")");
		probe.log(prefix + " **z1-z0=" + NumBoth(p1.z - p0.z) + "** 挿入点Z=" + Num(insertion) +
				  " 端点の絶対Z: [0]=" + Num(insertion + p0.z) + " [1]=" + Num(insertion + p1.z));
	}

	// バウンド 1 本ぶんの写し。**レコードを丸ごと持つ**——`fOffset` だけを見ていると、
	// `fBound` が落ちた（＝階から外れた）ことを見落とす。
	struct BoundRecord
	{
		MockUp::TObjectBoundID id = 0;
		bool got = false;
		MockUp::SStoryObjectData data;
		double resolved = 0;
	};

	std::vector<BoundRecord> ReadBounds(MCObjectHandle pio)
	{
		std::vector<BoundRecord> records;
		const size_t count = gSDK->GetObjectStoryBoundsCount(pio);
		for (size_t i = 0; i < count; ++i)
		{
			BoundRecord record;
			record.id = gSDK->GetObjectStoryBoundsAt(pio, i);
			record.got = gSDK->GetObjectStoryBound(pio, record.id, record.data);
			record.resolved = gSDK->GetObjectBoundElevation(pio, record.id);
			records.push_back(record);
		}
		return records;
	}

	std::string DescribeBound(const BoundRecord& record)
	{
		if (!record.got)
			return "GetObjectStoryBound=false 解決Z=" + Num(record.resolved);
		return "{" + std::string(BoundKindName(record.data.fBound)) +
			   ", 階=" + Int(static_cast<long long>(record.data.fBoundStory)) + ", \"" +
			   Str(record.data.fLayerLevelType) + "\", offset=" + Num(record.data.fOffset) +
			   "} 解決Z=" + Num(record.resolved);
	}

	void LogBounds(vwprobe::Report& probe, const std::string& prefix,
				   const std::vector<BoundRecord>& records)
	{
		std::string line = prefix + " バウンド 件数=" + Int(static_cast<long long>(records.size()));
		for (size_t i = 0; i < records.size(); ++i)
			line += " ／ ID=" + Int(static_cast<long long>(records[i].id)) + " " +
					DescribeBound(records[i]);
		probe.log(line);
	}

	const BoundRecord* FindBound(const std::vector<BoundRecord>& records, MockUp::TObjectBoundID id)
	{
		for (size_t i = 0; i < records.size(); ++i)
			if (records[i].id == id)
				return &records[i];
		return nullptr;
	}

	// 「③ から ⑤ へ何が変わったか」を 1 行で。**何も変わっていなければ「変化なし」**
	// ——まとめの表はこの文字列をそのまま並べる。
	std::string DescribeChange(const std::vector<BoundRecord>& before,
							   const std::vector<BoundRecord>& after, MockUp::TObjectBoundID id)
	{
		const BoundRecord* b = FindBound(before, id);
		const BoundRecord* a = FindBound(after, id);
		if (b == nullptr && a == nullptr)
			return "両方とも無し";
		if (b == nullptr)
			return "**増えた** → " + DescribeBound(*a);
		if (a == nullptr)
			return "**消えた**（差し替え前は " + DescribeBound(*b) + "）";
		if (!b->got || !a->got)
			return "読めない（got: " + std::string(b->got ? "true" : "false") + " → " +
				   (a->got ? "true" : "false") + "）";

		std::string changes;
		if (a->data.fBound != b->data.fBound)
			changes += std::string(changes.empty() ? "" : " ／ ") + "**fBound " +
					   BoundKindName(b->data.fBound) + " → " + BoundKindName(a->data.fBound) + "**";
		if (a->data.fBoundStory != b->data.fBoundStory)
			changes += std::string(changes.empty() ? "" : " ／ ") + "**階 " +
					   Int(static_cast<long long>(b->data.fBoundStory)) + " → " +
					   Int(static_cast<long long>(a->data.fBoundStory)) + "**";
		if (Str(a->data.fLayerLevelType) != Str(b->data.fLayerLevelType))
			changes += std::string(changes.empty() ? "" : " ／ ") + "**レベル種別 \"" +
					   Str(b->data.fLayerLevelType) + "\" → \"" + Str(a->data.fLayerLevelType) +
					   "\"**";
		if (a->data.fOffset != b->data.fOffset)
			changes += std::string(changes.empty() ? "" : " ／ ") + "**fOffset " +
					   Num(b->data.fOffset) + " → " + Num(a->data.fOffset) + "**";
		if (changes.empty())
		{
			// レコードが 1 ビットも動いていないのに解決Zだけ動いたなら、動いたのは
			// レコードではなく**解決の文脈**（レイヤ等）である。それも書き分ける。
			if (a->resolved != b->resolved)
				return "レコードは変化なし（解決Zだけ " + Num(b->resolved) + " → " +
					   Num(a->resolved) + "）";
			return "変化なし";
		}
		changes += " ／ 解決Z " + Num(b->resolved) + " → " + Num(a->resolved);
		return changes;
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

	// **レイヤの列挙は ForEachLayerN**（#56 で確定。階のハンドルはレイヤ経由でしか取れない）。
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
		probe.log("P. ストーリ " + Int(static_cast<long long>(stories.size())) +
				  " 件（GetNumStories = " + Int(static_cast<long long>(gSDK->GetNumStories())) +
				  "）");
		return stories;
	}

	// まとめの表 1 行ぶん。
	struct Outcome
	{
		std::string label;
		std::string replaced;  // 差し替えた局所パス（z0 → z1）
		std::string pathAfter; // ⑤ のパス（z1−z0）
		std::string changeId0; // ID 0 のレコードの変化
		std::string changeId1; // ID 1 のレコードの変化
		std::string verdict;
	};
} // namespace

VW_PROBE("story-bound-rewrite-on-path-replace",
		 "差し替えたパスの ResetObject が _Story バウンドを書き換えるかを実測する",
		 "SetCustomObjectPath で差し替えてから ResetObject を呼び、ID 0 / ID 1 の "
		 "_Story バウンドのレコード（fBound / 階 / レベル種別 / fOffset）が書き換わるかを"
		 "読む。始点を挿入点からずらした差し替えで ID 0 側も切り分ける。"
		 "**階が 2 つ以上あって同じレベル種別を両階が持つ図面で走らせること**")
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
		probe.log("P. レベル種別 " + Int(static_cast<long long>(allLevelTypes.size())) + " 件");

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
			probe.log("P. **Story モードで測る**（＝この調査が答えを出せる形）。下=\"" +
					  Str(lower.name) + "\"(" + Num(lower.elevation) + ") 上=\"" + Str(upper.name) +
					  "\"(" + Num(upper.elevation) + ") レベル種別 L=\"" + Str(levelType) +
					  "\" 置くレイヤ=\"" + Str(layerName) + "\"");
			probe.log("P. 階内相対Z: 下の \"" + Str(levelType) +
					  "\"=" + Num(gSDK->GetStoryLevelElevation(lower.handle, levelType)) +
					  " ／ 上の \"" + Str(levelType) +
					  "\"=" + Num(gSDK->GetStoryLevelElevation(upper.handle, levelType)));
			probe.log("P. 書くバウンド: ID0={Story, 階=0（自階）, \"" + Str(levelType) +
					  "\", offset=0} ／ ID1={Story, 階=1（上階）, \"" + Str(levelType) +
					  "\", offset=0}（#59 の A と同じ形）");
		}
		else
		{
			// 階が無くても同じ測定は走るが、**この調査の問い 1 には答えられない**。
			// 走らせた人がログを読み違えないよう、最後に失敗として返す。
			boundMode = "LayerElevation";
			boundLower = MakeBound(MockUp::eStoryObjectBound_LayerElevation, 0, TXString(), 2500);
			boundUpper = MakeBound(MockUp::eStoryObjectBound_LayerElevation, 0, TXString(), 5500);
			probe.log("P. **LayerElevation モードへ落ちた**（この図面には「同じレベル種別を"
					  "両方が持つ隣り合う 2 階」が無い）。バウンドは ID0={LayerElevation, "
					  "offset=2500} / ID1={LayerElevation, offset=5500}。");
			probe.log("P. **この実行は issue #63 の問い 1（_Story でも同じ書き換えが起きるか）"
					  "には答えない。** 測定自体は走るので ID 0 の切り分け（問い 2）は読めるが、"
					  "結果は失敗として返す——階のある図面（事故のモデル）で走らせ直してほしい。");
			MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
			TXString layerName;
			if (currentLayer != nullptr)
				gSDK->GetObjectName(currentLayer, layerName);
			probe.log("P. 置くレイヤ（いまのレイヤ）: \"" + Str(layerName) + "\"");
		}
	}

	// =======================================================================
	probe.log("=== T. 物差し（このモードで「まともなパス」が作り直されるかを先に確かめる） ===");
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
				  " ID1=" + (setUpper ? "true" : "false") +
				  " ／ 件数=" + Int(static_cast<long long>(gSDK->GetObjectStoryBoundsCount(pio))) +
				  " ／ **解決Z: ID0=" + Num(resolvedLower) + " ID1=" + Num(resolvedUpper) +
				  " 差=" + Num(resolvedUpper - resolvedLower) + "**");
		gSDK->ResetObject(pio);
		LogPath(probe, "T. ③ ResetObject 後:", pio);
		LogBounds(probe, "T. ③", ReadBounds(pio));
		const double afterReset = PathDeltaZ(pio);
		probe.log("T. 物差しの判定: " +
				  std::string(std::fabs(std::fabs(afterReset) -
										std::fabs(resolvedUpper - resolvedLower)) <= 1e-6
								  ? "**まともなパスは作り直された**（このモードで測れる）"
								  : "**作り直されなかった**——以降の読みが変わる。"
									"ログをそのまま読むこと"));
	}

	if (resolvedUpper - resolvedLower == 0)
	{
		probe.fail("T. 2 本のバウンドが同じ絶対Zへ解決された（差が 0）。これでは"
				   "「書き換わった」と「元から同じ」が見分けられない。ログの解決Zを見て、"
				   "バウンドの作り方を直す必要がある。");
		return;
	}

	const double expectedHeight = resolvedUpper - resolvedLower;
	// 作るときのパスの基準Z。0 の近くでは 1 ULP が意味を持たない（denormal）ので、
	// 解決Zが小さすぎるときだけ 2500 へ逃がす（パスの絶対Zは結果に効かない。#56 の 5）。
	const double pathBase = std::fabs(resolvedLower) >= 1 ? resolvedLower : 2500;
	const double ulpStep = UlpsAway(pathBase, 1) - pathBase;
	probe.log("T. 作り直されたときの長さ = " + Num(expectedHeight) +
			  " ／ 作るときの基準Z = " + NumBoth(pathBase) + "（1 ULP = " + Num(ulpStep) + "）");

	// =======================================================================
	probe.log("=== R. 差し替えてからの ResetObject が、バウンドのレコードをどうするか ===");
	probe.log("R. 各ケースの流れ: 長さ 100 のパスで作る → バウンド 2 本 → ③ ResetObject"
			  "（ここまでは正常に作り直される）→ ④ SetCustomObjectPath で差し替え"
			  "→ ⑤ もう一度 ResetObject。**③ と ⑤ のレコードを比べる。**");

	std::vector<Outcome> outcomes;
	{
		struct Replacement
		{
			std::string label;
			double startLocal; // 差し替えるパスの始点（局所Z。0 が ③ 後の挿入点）
			double endLocal; // 終点
			std::string why;
		};

		const double createLength = 100;
		std::vector<Replacement> replacements;
		// 始点が挿入点と一致する形（#61 が LayerElevation で測ったのと同じ 3 通り）。
		replacements.push_back({"R-0 0 長（始点＝挿入点）", 0, 0,
								"#61 の表の 1 行目と同じ形。_Story でも ID 1 の fOffset が"
								"挿入点へ合わせて書き換わるか"});
		replacements.push_back({"R-1 1 ULP（始点＝挿入点）", 0, ulpStep,
								"#61 の表の 2 行目と同じ形。事故で潰れた個体の形でもある"});
		replacements.push_back({"R-2 長さ 100（始点＝挿入点）", 0, 100,
								"#61 の表の 3 行目と同じ形。まともな長さでも書き換わるか"});
		// **ここからが issue #63 の問い 2 の切り分け**——始点を挿入点からずらす。
		replacements.push_back({"R-3 100 → 200（始点が挿入点と一致しない）", 100, 200,
								"ID 0 が「パスの始点に合わせて動く」なら、ここで初めて ID 0 の"
								"レコードが変わる。変わらなければ ID 0 は書き換えの対象外"});
		replacements.push_back({"R-4 -100 → 0（終点が挿入点と一致）", -100, 0,
								"上下を入れ替えた形。ID 1 が「終点に合わせて動く」なら、"
								"ID 1 は挿入点の高さへ落ちる"});
		replacements.push_back({"R-5 100 → 100（0 長・挿入点の 100 上）", 100, 100,
								"0 長のまま始点だけずらした形。ID 0 と ID 1 が同じ高さへ"
								"揃うなら、書き換えは「パスの両端へ合わせる」で説明が付く"});
		replacements.push_back({"R-6 0 → " + Num(expectedHeight * 2) + "（解決済みの倍）", 0,
								expectedHeight * 2,
								"階の間隔より長いパスへ差し替えたとき、fBoundStory や"
								"レベル種別まで変わるのか、fOffset だけで吸収するのか"});

		for (size_t i = 0; i < replacements.size(); ++i)
		{
			const Replacement& replacement = replacements[i];
			probe.log("--- " + replacement.label + " ---");
			probe.log("  ねらい: " + replacement.why);

			MCObjectHandle path =
				MakeStraightPath(WorldPt3(0, 0, pathBase), WorldPt3(0, 0, pathBase + createLength));
			MCObjectHandle pio =
				path != nullptr ? gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr)
								: nullptr;
			if (pio == nullptr)
			{
				probe.log("  CreateCustomObjectPath が nil を返した");
				continue;
			}
			const bool setLower = gSDK->SetObjectStoryBound(pio, 0, boundLower);
			const bool setUpper = gSDK->SetObjectStoryBound(pio, 1, boundUpper);
			gSDK->ResetObject(pio);
			probe.log(std::string("  SetObjectStoryBound: ID0=") + (setLower ? "true" : "false") +
					  " ID1=" + (setUpper ? "true" : "false") +
					  " ／ 作るときのパスの長さ=" + Num(createLength));
			LogPath(probe, "  ③ 作って ResetObject まで通した:", pio);
			const std::vector<BoundRecord> before = ReadBounds(pio);
			LogBounds(probe, "  ③", before);

			MCObjectHandle newPath = MakeStraightPath(WorldPt3(0, 0, replacement.startLocal),
													  WorldPt3(0, 0, replacement.endLocal));
			if (newPath == nullptr)
			{
				probe.log("  差し替え用の CreateNurbsCurve が nil を返した");
				continue;
			}
			const bool replaced = gSDK->SetCustomObjectPath(pio, newPath);
			probe.log(std::string("  SetCustomObjectPath=") + (replaced ? "true" : "false") +
					  " 渡した局所座標: z0=" + NumBoth(replacement.startLocal) +
					  " z1=" + NumBoth(replacement.endLocal) +
					  " 長さ=" + NumBoth(replacement.endLocal - replacement.startLocal));
			LogPath(probe, "  ④ 差し替えた直後:", pio);
			LogBounds(probe, "  ④", ReadBounds(pio));
			const double afterReplace = PathDeltaZ(pio);

			gSDK->ResetObject(pio);
			LogPath(probe, "  ⑤ もう一度 ResetObject:", pio);
			const std::vector<BoundRecord> after = ReadBounds(pio);
			LogBounds(probe, "  ⑤", after);

			// ⑤ の端点の絶対Zと、⑤ のバウンドの解決Zを突き合わせる——「パスの両端へ
			// 合わせて書き換えた」なら、この 2 つが一致する。
			WorldPt3 p0, p1;
			std::string matchLine = "  ⑤ パスの端点とバウンドの解決Zの突き合わせ: 読めない";
			if (ReadPathEndpoints(pio, p0, p1))
			{
				const double insertion = InsertionZ(pio);
				const BoundRecord* a0 = FindBound(after, 0);
				const BoundRecord* a1 = FindBound(after, 1);
				const double abs0 = insertion + p0.z;
				const double abs1 = insertion + p1.z;
				matchLine =
					"  ⑤ パスの端点とバウンドの解決Zの突き合わせ: 始点の絶対Z=" + Num(abs0) +
					" と ID0 の解決Z=" + (a0 != nullptr ? Num(a0->resolved) : std::string("無し")) +
					" → " +
					((a0 != nullptr && std::fabs(abs0 - a0->resolved) <= 1e-6) ? "**一致**"
																			   : "一致しない") +
					" ／ 終点の絶対Z=" + Num(abs1) +
					" と ID1 の解決Z=" + (a1 != nullptr ? Num(a1->resolved) : std::string("無し")) +
					" → " +
					((a1 != nullptr && std::fabs(abs1 - a1->resolved) <= 1e-6) ? "**一致**"
																			   : "一致しない");
			}
			probe.log(matchLine);

			Outcome outcome;
			outcome.label = replacement.label;
			outcome.replaced = Num(replacement.startLocal) + " → " + Num(replacement.endLocal);
			outcome.pathAfter = Num(PathDeltaZ(pio));
			outcome.changeId0 = DescribeChange(before, after, 0);
			outcome.changeId1 = DescribeChange(before, after, 1);

			// 判定は 2 つの軸で書く: パスが温存されたか（#61 の結論の再確認）と、
			// バウンドが書き換わったか（この調査の主語）。
			const double afterReset = PathDeltaZ(pio);
			std::string pathVerdict;
			if (afterReset != afterReset) // NaN
				pathVerdict = "パスが読めない";
			else if (afterReset == afterReplace)
				pathVerdict = "パスは温存";
			else if (std::fabs(std::fabs(afterReset) - std::fabs(expectedHeight)) <= 1e-6)
				pathVerdict = "**パスが作り直された**";
			else
				pathVerdict = "パスがそのどれでもない値になった";

			const bool changed0 = outcome.changeId0 != "変化なし";
			const bool changed1 = outcome.changeId1 != "変化なし";
			std::string boundVerdict;
			if (!changed0 && !changed1)
				boundVerdict = "バウンドは書き換わらなかった";
			else if (changed0 && changed1)
				boundVerdict = "**ID 0 も ID 1 も書き換わった**";
			else if (changed1)
				boundVerdict = "**ID 1 だけ書き換わった**";
			else
				boundVerdict = "**ID 0 だけ書き換わった**";

			// 階への従属が残るかは `fBound` が `Story` のままかで決まる（この行が
			// issue #63 の問い 3 への答えそのもの）。
			std::string followVerdict;
			if (boundMode == "Story")
			{
				const BoundRecord* a0 = FindBound(after, 0);
				const BoundRecord* a1 = FindBound(after, 1);
				const bool story0 =
					a0 != nullptr && a0->got && a0->data.fBound == MockUp::eStoryObjectBound_Story;
				const bool story1 =
					a1 != nullptr && a1->got && a1->data.fBound == MockUp::eStoryObjectBound_Story;
				if (story0 && story1)
					followVerdict = "⑤ でも両方 fBound=Story（階への従属は残る）";
				else
					followVerdict = std::string("**fBound が Story でなくなった**（ID0=") +
									(story0 ? "Story" : "Story でない") +
									" / ID1=" + (story1 ? "Story" : "Story でない") + "）";
			}
			outcome.verdict = pathVerdict + " ／ " + boundVerdict +
							  (followVerdict.empty() ? "" : " ／ " + followVerdict);
			probe.log("  → 判定: " + outcome.verdict);
			probe.log("  → ID 0 の変化: " + outcome.changeId0);
			probe.log("  → ID 1 の変化: " + outcome.changeId1);
			outcomes.push_back(outcome);
		}
	}

	// =======================================================================
	probe.log("=== まとめ（この表が結論） ===");
	probe.log("バウンドの作り方: " + boundMode + " ／ 解決Z: 下端=" + Num(resolvedLower) +
			  " 上端=" + Num(resolvedUpper) +
			  " ／ 正常に作り直されたときの長さ=" + Num(expectedHeight));
	probe.log("ケース | 差し替えた局所パス | ⑤ のパス z1-z0 | ID 0 のレコード | "
			  "ID 1 のレコード | 判定");
	for (size_t i = 0; i < outcomes.size(); ++i)
	{
		const Outcome& outcome = outcomes[i];
		probe.log(outcome.label + " | " + outcome.replaced + " | " + outcome.pathAfter + " | " +
				  outcome.changeId0 + " | " + outcome.changeId1 + " | " + outcome.verdict);
	}
	probe.log("読み方: **ID 1 の欄**が問い 1（_Story でも書き換わるか。fOffset だけか、"
			  "fBound ごとか）。**R-3 / R-4 / R-5 の ID 0 の欄**が問い 2（始点が挿入点と"
			  "一致しない差し替えで ID 0 が動くか）。**判定の 3 つ目**が問い 3"
			  "（書き換わった後も階に追従するか）。");

	if (boundMode != "Story")
		probe.fail("この実行は LayerElevation モードで走った（図面に「同じレベル種別を両方が"
				   "持つ隣り合う 2 階」が無い）ため、**issue #63 の問い 1 には答えていない**。"
				   "階が 2 つ以上ある図面——事故のモデル——の捨ててよい複製で走らせ直してほしい。"
				   "上のログは LayerElevation での測定として読める（ID 0 の切り分けは読める）。");
}
