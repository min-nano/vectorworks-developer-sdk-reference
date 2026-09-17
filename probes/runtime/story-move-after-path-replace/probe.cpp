//
//	probes/runtime/story-move-after-path-replace/probe.cpp
//
//	[issue #65] **`SetCustomObjectPath` で差し替えて `fOffset` を書き換えられた部材は、
//	階を動かすと長さが変わるのか。**
//
//	## 出発点（issue #63 / PR #64 で実機確認済み）
//
//	一度 `ResetObject` を通した個体のパスを `SetCustomObjectPath` で差し替えると、
//	**その後の `ResetObject` はパスを作り直さず、逆にバウンドのほうを差し替えたパスの
//	両端へ合わせて書き換える**。`_Story` バウンドでも書き換わるのは `fOffset` だけで、
//	`fBound` / `fBoundStory` / `fLayerLevelType` は変わらない——つまり
//	**その部材は「嘘の `fOffset`」を持ったまま、階への従属だけは残っている**
//	（0 長へ差し替えた部材は `ID1 = {Story, 上階, 耐力壁, −2959}`。
//	その瞬間の階間隔が `fOffset` へ焼き込まれた形）。
//
//	## この調査の問い（issue #65）
//
//	1. **階を動かすと、その部材の長さは変わるのか。** ID 0 は自階基準・ID 1 は上階基準の
//	   まま、それぞれ固定の `fOffset` で解決されるなら、**上階を 100 上げれば 0 長だった
//	   部材が 100 の長さになる**はず（＝潰れた部材が「勝手に生えてくる」）。
//	2. **生えてくるとして、それは「いつ」か。** `SetStoryElevation` を呼んだ時点で
//	   VW が勝手に作り直すのか、こちらが `ResetObject` を呼んだときだけなのか。
//	3. **そのとき、バウンドのレコードはまた書き換わるのか。** 差し替え経路の
//	   「パスが正・バウンドが従う」が**その個体に残り続ける**なら、階を動かしてから
//	   `ResetObject` を呼んでも長さは変わらず、代わりに `fOffset` がもう一度
//	   書き換わる（嘘が自分で更新されて生き延びる）はずである。
//	   3 と 1 は**背反**で、どちらが起きるかがこの調査の中心。
//
//	## 測り方
//
//	**3 体を同じ図面に並べ、同じ階の移動を通す**（1 体だけでは「階を動かしたのだから
//	変わって当然」と「差し替えたから変わらない」を切り分けられない）:
//
//	    A 事故の形   作る → バウンド 2 本 → ResetObject → **0 長へ差し替え** → ResetObject
//	                 （この時点で ID1 の fOffset に −階間隔 が焼き込まれている）
//	    B 対照       作る → バウンド 2 本 → ResetObject（差し替えない＝ふつうの部材）
//	    C 差し替え   A と同じだが**長さ 100 を残して**差し替えた（0 長でなくても同じか）
//
//	段階は次の 8 つ。**各段階で 3 体すべてのパス・挿入点・バウンドのレコード・解決Zを
//	読む**（レコードを丸ごと読むのは、`fOffset` だけを見ていると `fBound` が落ちたのを
//	見落とすため）:
//
//	    S0 階を動かす前
//	    S1 **上階を +100**（`SetStoryElevation`）した直後。**まだ ResetObject を呼ばない**
//	       → ここで長さが動いていれば、VW が勝手に作り直している（問い 2）
//	    S2 3 体に `ResetObject` を呼んだ後 → 問い 1 と問い 3 の答えはここに出る
//	    S3 上階を**元へ戻した**直後（ResetObject なし）
//	    S4 戻してから `ResetObject` した後 → 行って帰ってこられるのか（可逆か）
//	    S5 **自階（下階）を +100** した直後（ResetObject なし）
//	       → 部材が乗っているレイヤごと動く形。ID 0 側も同じ規則で動くか
//	    S6 `ResetObject` した後
//	    S7 下階を元へ戻して `ResetObject` した後（＝階の高さを元通りにして終える）
//
//	* **階の高さは最後に必ず元へ戻す**（`SetStoryElevation` で読んだ値を書き戻す）。
//	  ただし**作った部材 3 体は図面に残る**ので、捨ててよい複製で走らせてもらうこと。
//	* **物差しを 1 本持つ**: オブジェクトを作らずにバウンドを解かせる
//	  `GetStoryObjectDataBoundHeight` を各段階で呼び、「階が実際に動いたか」を
//	  部材とは独立に確かめる（部材が動かなかったとき、「階が動かなかった」のか
//	  「部材が追従しなかった」のかを分けるため）。
//	* **階のある図面で走らせてもらうほかない。** この調査は階が無いと測れないのに、
//	  プローブはふつう**新規の空図面**で走らせる（`probes/runtime/README.md`）ので、
//	  まず**プローブ自身に階を用意させよう**とした。**2 度試してどちらも通らず**、
//	  SDK だけでは階を用意できないと確定した（`Findings/Layers and Stories.md`
//	  「打ち切った調査: SDK だけで階を用意する」）。よってここでは**階が無ければ
//	  何も触らずに失敗として返す**——`LayerElevation` へも落とさない
//	  （階を持たない図面で階の移動は測れないので、落としても答えにならない）。
//
//	【読み方】最後の「まとめ」の表がこのプローブの結論。**A の行の「パス z1-z0」**が
//	問い 1（0 のままか、100 になるか）、**S1 と S2 の差**が問い 2（いつ変わるか）、
//	**A の ID 1 の `fOffset`**が問い 3（嘘がまた書き換わるか）への答えである。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// **丸めて出さない。** 差し替え経路の値は 1 ULP の差も意味を持つ（#61 / #63）。
	std::string Num(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.17g", value);
		return std::string(buffer);
	}

	std::string Str(const TXString& text)
	{
		return std::string(static_cast<const char*>(text));
	}

	std::string Int(long long value)
	{
		return std::to_string(value);
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

	double InsertionZ(MCObjectHandle pio)
	{
		VWParametricObj obj(pio);
		return obj.GetObjectModelPos().z;
	}

	MCObjectHandle MakeStraightPath(WorldPt3 from, WorldPt3 to)
	{
		MCObjectHandle curve = gSDK->CreateNurbsCurve(from, false, 1);
		if (curve == nullptr)
			return nullptr;
		gSDK->Add3DVertex(curve, to, true);
		return curve;
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

	std::string DescribeBound(const BoundRecord& record)
	{
		if (!record.got)
			return "GetObjectStoryBound=false 解決Z=" + Num(record.resolved);
		return "{" + std::string(BoundKindName(record.data.fBound)) +
			   ", 階=" + Int(static_cast<long long>(record.data.fBoundStory)) + ", \"" +
			   Str(record.data.fLayerLevelType) + "\", offset=" + Num(record.data.fOffset) +
			   "} 解決Z=" + Num(record.resolved);
	}

	// ある段階で 1 体から読み取れるものすべて。段階どうしを引き算するために持ち回る。
	struct Snapshot
	{
		bool pathRead = false;
		double deltaZ = 0;	  // パスの z1−z0（＝部材の長さ。負なら下向き）
		double insertion = 0; // 挿入点Z
		double absStart = 0;  // 始点の絶対Z（挿入点 ＋ パスZ）
		double absEnd = 0;	  // 終点の絶対Z
		std::vector<BoundRecord> bounds;
	};

	const BoundRecord* FindBound(const Snapshot& snapshot, MockUp::TObjectBoundID id)
	{
		for (size_t i = 0; i < snapshot.bounds.size(); ++i)
			if (snapshot.bounds[i].id == id)
				return &snapshot.bounds[i];
		return nullptr;
	}

	Snapshot Capture(MCObjectHandle pio)
	{
		Snapshot snapshot;
		WorldPt3 p0, p1;
		if (ReadPathEndpoints(pio, p0, p1))
		{
			snapshot.pathRead = true;
			snapshot.deltaZ = p1.z - p0.z;
			snapshot.insertion = InsertionZ(pio);
			snapshot.absStart = snapshot.insertion + p0.z;
			snapshot.absEnd = snapshot.insertion + p1.z;
		}
		const size_t count = gSDK->GetObjectStoryBoundsCount(pio);
		for (size_t i = 0; i < count; ++i)
		{
			BoundRecord record;
			record.id = gSDK->GetObjectStoryBoundsAt(pio, i);
			record.got = gSDK->GetObjectStoryBound(pio, record.id, record.data);
			record.resolved = gSDK->GetObjectBoundElevation(pio, record.id);
			snapshot.bounds.push_back(record);
		}
		return snapshot;
	}

	std::string BoundCell(const Snapshot& snapshot, MockUp::TObjectBoundID id)
	{
		const BoundRecord* record = FindBound(snapshot, id);
		if (record == nullptr)
			return "無し";
		return DescribeBound(*record);
	}

	// 「前の段階から何が変わったか」を 1 行で。**何も変わっていなければ「変化なし」**。
	std::string DescribeChange(const Snapshot& before, const Snapshot& after,
							   MockUp::TObjectBoundID id)
	{
		const BoundRecord* b = FindBound(before, id);
		const BoundRecord* a = FindBound(after, id);
		if (b == nullptr && a == nullptr)
			return "両方とも無し";
		if (b == nullptr)
			return "**増えた** → " + DescribeBound(*a);
		if (a == nullptr)
			return "**消えた**（前は " + DescribeBound(*b) + "）";
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
			// レコードではなく**解決の文脈**（＝階の高さ）である。この調査ではむしろ
			// こちらが本命なので、はっきり書き分ける。
			if (a->resolved != b->resolved)
				return "レコードは変化なし（**解決Zだけ " + Num(b->resolved) + " → " +
					   Num(a->resolved) + "**）";
			return "変化なし";
		}
		changes += " ／ 解決Z " + Num(b->resolved) + " → " + Num(a->resolved);
		return changes;
	}

	// 測る 1 体。
	struct Member
	{
		std::string label;
		std::string how; // どう作ったか（まとめの表に出す）
		MCObjectHandle pio = nullptr;
		std::vector<Snapshot> stages; // 段階ごとの写し（S0 から順に積む）
	};

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

	// **登録済みのレベル種別**（添字は 1 始まり。0 は空文字。#56 で確定）。
	std::vector<TXString> CollectLevelTypes()
	{
		std::vector<TXString> types;
		const short count = gSDK->GetNumLayerLevelTypes();
		for (short i = 1; i <= count; ++i)
		{
			const TXString type = gSDK->GetLayerLevelTypeName(i);
			if (!Str(type).empty())
				types.push_back(type);
		}
		return types;
	}

	// 「隣り合う 2 階が同じレベル種別を両方持つ」組を探す。**この組が無ければ測れない**
	// （#59 の A と同じ形）。
	bool FindStoryPair(const std::vector<StoryInfo>& stories,
					   const std::vector<TXString>& levelTypes, StoryInfo& outLower,
					   StoryInfo& outUpper, TXString& outLevelType)
	{
		for (size_t i = 0; i < stories.size(); ++i)
		{
			MCObjectHandle above = gSDK->GetStoryAbove(stories[i].handle);
			if (above == nullptr)
				continue;
			for (size_t j = 0; j < stories.size(); ++j)
			{
				if (stories[j].handle != above)
					continue;
				for (size_t k = 0; k < levelTypes.size(); ++k)
				{
					if (gSDK->GetLayerForStory(stories[i].handle, levelTypes[k]) == nullptr)
						continue;
					if (gSDK->GetLayerForStory(stories[j].handle, levelTypes[k]) == nullptr)
						continue;
					outLower = stories[i];
					outUpper = stories[j];
					outLevelType = levelTypes[k];
					return true;
				}
			}
		}
		return false;
	}

} // namespace

VW_PROBE("story-move-after-path-replace",
		 "差し替えで fOffset を書き換えられた部材が、階の移動でどうなるかを実測する",
		 "0 長へ差し替えて ID 1 の fOffset に −階間隔 を焼き込まれた部材と、差し替えて"
		 "いない対照を並べ、SetStoryElevation で上階／自階を動かして長さ・挿入点・"
		 "バウンドのレコードを読み直す。階の高さは最後に元へ戻す。"
		 "**階が 2 つ以上あって同じレベル種別を両階が持つ図面**が要る（階はプローブ側では"
		 "用意できない。Findings「打ち切った調査: SDK だけで階を用意する」）。"
		 "**階の高さを動かすので、捨ててよい複製で走らせること**")
{
	// =======================================================================
	probe.log("=== P. 測る形を決める（無ければプローブが作る） ===");

	StoryInfo lower;
	StoryInfo upper;
	TXString levelType;
	MCObjectHandle container = nullptr;
	{
		std::vector<StoryInfo> stories = CollectStories(probe);
		const std::vector<TXString> allLevelTypes = CollectLevelTypes();
		probe.log("P. レベル種別 " + Int(static_cast<long long>(allLevelTypes.size())) + " 件");

		if (!FindStoryPair(stories, allLevelTypes, lower, upper, levelType))
		{
			// **階はプローブ側で用意できない**（PR #66 で 2 度試して確定。
			// `Findings/Layers and Stories.md`「打ち切った調査: SDK だけで階を用意する」）。
			// だから**階のある図面で走らせてもらうほかない**。図面は何も触っていない。
			probe.fail("**この図面には階が無い**（「同じレベル種別を両方が持つ隣り合う 2 階」が"
					   "見つからない）。階はプローブ側では用意できないと実機で確かめてある"
					   "（`Findings/Layers and Stories.md`「打ち切った調査: SDK だけで階を"
					   "用意する」）ので、**階が 2 つ以上ある図面——事故のモデル——の"
					   "捨ててよい複製を開いて、もう一度走らせてください**。"
					   "図面は何も触っていません。");
			return;
		}

		container = gSDK->GetLayerForStory(lower.handle, levelType);
		if (container == nullptr)
			container = lower.anyLayer;
		if (container != nullptr)
			gSDK->SetCurrentLayer(container);
		TXString layerName;
		if (container != nullptr)
			gSDK->GetObjectName(container, layerName);
		probe.log("P. 下=\"" + Str(lower.name) + "\"(" + Num(lower.elevation) + ") 上=\"" +
				  Str(upper.name) + "\"(" + Num(upper.elevation) + ") レベル種別 L=\"" +
				  Str(levelType) + "\" 置くレイヤ=\"" + Str(layerName) + "\"");
		probe.log("P. 階内相対Z: 下の \"" + Str(levelType) +
				  "\"=" + Num(gSDK->GetStoryLevelElevation(lower.handle, levelType)) +
				  " ／ 上の \"" + Str(levelType) +
				  "\"=" + Num(gSDK->GetStoryLevelElevation(upper.handle, levelType)));
		probe.log("P. 書くバウンド: ID0={Story, 階=0（自階）, \"" + Str(levelType) +
				  "\", offset=0} ／ ID1={Story, 階=1（上階）, \"" + Str(levelType) +
				  "\", offset=0}（#59 / #63 と同じ形）");
	}

	const MockUp::SStoryObjectData boundLower =
		MakeBound(MockUp::eStoryObjectBound_Story, 0, levelType, 0);
	const MockUp::SStoryObjectData boundUpper =
		MakeBound(MockUp::eStoryObjectBound_Story, 1, levelType, 0);

	// **元の高さを控える。最後に必ずここへ書き戻す。**
	const WorldCoord originalLowerElevation = gSDK->GetStoryElevation(lower.handle);
	const WorldCoord originalUpperElevation = gSDK->GetStoryElevation(upper.handle);
	probe.log("P. 元の階の高さ: 下=" + Num(originalLowerElevation) +
			  " 上=" + Num(originalUpperElevation) + "（**最後にここへ戻す**）");

	// 物差し: オブジェクトを作らずにバウンドを解かせる（階が実際に動いたかを、部材とは
	// 独立に確かめるため。#56 で使ったのと同じ口）。
	const auto Witness = [&](const char* when)
	{
		if (container == nullptr)
			return;
		const double resolved0 = gSDK->GetStoryObjectDataBoundHeight(boundLower, container);
		const double resolved1 = gSDK->GetStoryObjectDataBoundHeight(boundUpper, container);
		probe.log(std::string("  物差し（オブジェクト無しでバウンドを解く）") + when +
				  ": ID0 相当=" + Num(resolved0) + " ID1 相当=" + Num(resolved1) +
				  " 差=" + Num(resolved1 - resolved0) +
				  " ／ 階の高さ: 下=" + Num(gSDK->GetStoryElevation(lower.handle)) +
				  " 上=" + Num(gSDK->GetStoryElevation(upper.handle)));
	};

	// =======================================================================
	probe.log("=== B. 測る 3 体を作る ===");
	std::vector<Member> members;
	{
		// 作るときの基準Z。0 の近くを避ける意味は無いが、#63 と同じ形にしておく
		// （パスの絶対Zは結果に効かない。#56 の 5）。
		const double pathBase = 0;
		const double createLength = 100;

		struct Recipe
		{
			std::string label;
			std::string how;
			bool replace = false;
			double startLocal = 0;
			double endLocal = 0;
		};
		std::vector<Recipe> recipes;
		recipes.push_back({"A 事故の形（0 長へ差し替え）",
						   "作る → バウンド 2 本 → ResetObject → **局所 0→0 へ差し替え** → "
						   "ResetObject",
						   true, 0, 0});
		recipes.push_back({"B 対照（差し替えていない）",
						   "作る → バウンド 2 本 → ResetObject（それだけ）", false, 0, 0});
		recipes.push_back({"C 差し替え（長さ 100 を残した）",
						   "作る → バウンド 2 本 → ResetObject → **局所 0→100 へ差し替え** → "
						   "ResetObject",
						   true, 0, 100});

		for (size_t i = 0; i < recipes.size(); ++i)
		{
			const Recipe& recipe = recipes[i];
			probe.log("--- " + recipe.label + " ---");
			MCObjectHandle path =
				MakeStraightPath(WorldPt3(0, 0, pathBase), WorldPt3(0, 0, pathBase + createLength));
			MCObjectHandle pio =
				path != nullptr ? gSDK->CreateCustomObjectPath("StructuralMember", path, nullptr)
								: nullptr;
			if (pio == nullptr)
			{
				probe.fail(recipe.label +
						   ": CreateCustomObjectPath(\"StructuralMember\") が nil を返した"
						   "（構造材 PIO がこの図面で作れない）。");
				continue;
			}
			const bool setLower = gSDK->SetObjectStoryBound(pio, 0, boundLower);
			const bool setUpper = gSDK->SetObjectStoryBound(pio, 1, boundUpper);
			gSDK->ResetObject(pio);
			probe.log(std::string("  SetObjectStoryBound: ID0=") + (setLower ? "true" : "false") +
					  " ID1=" + (setUpper ? "true" : "false") + " ／ 作るときのパスの長さ=" +
					  Num(createLength) + " ／ ResetObject 後の z1-z0=" + Num(Capture(pio).deltaZ));

			if (recipe.replace)
			{
				MCObjectHandle newPath = MakeStraightPath(WorldPt3(0, 0, recipe.startLocal),
														  WorldPt3(0, 0, recipe.endLocal));
				const bool replaced =
					newPath != nullptr ? gSDK->SetCustomObjectPath(pio, newPath) : false;
				gSDK->ResetObject(pio);
				probe.log(std::string("  SetCustomObjectPath=") + (replaced ? "true" : "false") +
						  " 渡した局所座標: z0=" + Num(recipe.startLocal) +
						  " z1=" + Num(recipe.endLocal) +
						  " ／ その後の ResetObject で z1-z0=" + Num(Capture(pio).deltaZ));
			}

			Member member;
			member.label = recipe.label;
			member.how = recipe.how;
			member.pio = pio;
			members.push_back(member);
		}
	}

	if (members.empty())
	{
		probe.fail(
			"1 体も作れなかったので、階を動かしても読むものが無い。図面は何も触っていない。");
		return;
	}

	// =======================================================================
	// 各段階の読み取り。**3 体ぶんまとめて 1 段階**（段階の途中で図面を触らないため）。
	const auto Stage = [&](const char* name, const char* what)
	{
		probe.log("--- " + std::string(name) + " " + what + " ---");
		Witness("");
		for (size_t i = 0; i < members.size(); ++i)
		{
			Member& member = members[i];
			const Snapshot snapshot = Capture(member.pio);
			std::string line = "  " + member.label + ": ";
			if (!snapshot.pathRead)
				line += "パスが読めない";
			else
				line +=
					"**z1-z0=" + Num(snapshot.deltaZ) + "** 挿入点Z=" + Num(snapshot.insertion) +
					" 端点の絶対Z: [0]=" + Num(snapshot.absStart) + " [1]=" + Num(snapshot.absEnd);
			line += " ／ ID0 " + BoundCell(snapshot, 0) + " ／ ID1 " + BoundCell(snapshot, 1);
			probe.log(line);
			if (!member.stages.empty())
			{
				const Snapshot& previous = member.stages.back();
				const std::string lengthChange =
					previous.pathRead && snapshot.pathRead
						? (previous.deltaZ == snapshot.deltaZ
							   ? std::string("長さは変化なし")
							   : "**長さ " + Num(previous.deltaZ) + " → " + Num(snapshot.deltaZ) +
									 "**")
						: std::string("長さは比べられない");
				const std::string insertionChange =
					previous.pathRead && snapshot.pathRead
						? (previous.insertion == snapshot.insertion
							   ? std::string("挿入点は変化なし")
							   : "**挿入点 " + Num(previous.insertion) + " → " +
									 Num(snapshot.insertion) + "**")
						: std::string("挿入点は比べられない");
				probe.log("    前の段階から: " + lengthChange + " ／ " + insertionChange +
						  " ／ ID0 " + DescribeChange(previous, snapshot, 0) + " ／ ID1 " +
						  DescribeChange(previous, snapshot, 1));
			}
			member.stages.push_back(snapshot);
		}
	};

	const auto ResetAll = [&]()
	{
		for (size_t i = 0; i < members.size(); ++i)
			gSDK->ResetObject(members[i].pio);
	};

	const auto MoveStory = [&](const StoryInfo& story, WorldCoord to)
	{
		const bool ok = gSDK->SetStoryElevation(story.handle, to);
		const WorldCoord readBack = gSDK->GetStoryElevation(story.handle);
		probe.log(std::string("  SetStoryElevation(\"") + Str(story.name) + "\", " + Num(to) +
				  ") = " + (ok ? "true" : "false") + " ／ 読み戻し=" + Num(readBack) +
				  (readBack == to ? "（**動いた**）" : "（**動いていない**）"));
		return readBack == to;
	};

	probe.log("=== S. 階を動かして読み直す ===");
	const double moveBy = 100;

	Stage("S0", "階を動かす前");

	probe.log("--- 上階を +" + Num(moveBy) + " する ---");
	const bool upperMoved = MoveStory(upper, originalUpperElevation + moveBy);
	Stage("S1", "上階を +100 した直後（**ResetObject を呼んでいない**）");
	ResetAll();
	Stage("S2", "その状態で ResetObject した後（**ここが問い 1 と問い 3 の答え**）");

	probe.log("--- 上階を元へ戻す ---");
	MoveStory(upper, originalUpperElevation);
	Stage("S3", "上階を元へ戻した直後（ResetObject を呼んでいない）");
	ResetAll();
	Stage("S4", "戻してから ResetObject した後（可逆か）");

	probe.log("--- 自階（下階）を +" + Num(moveBy) + " する（部材が乗っているレイヤごと動く） ---");
	MoveStory(lower, originalLowerElevation + moveBy);
	Stage("S5", "下階を +100 した直後（ResetObject を呼んでいない）");
	ResetAll();
	Stage("S6", "その状態で ResetObject した後");

	probe.log("--- 下階を元へ戻す（図面の階の高さをここで元通りにする） ---");
	MoveStory(lower, originalLowerElevation);
	ResetAll();
	Stage("S7", "下階を戻して ResetObject した後（＝階の高さは元通り）");

	// =======================================================================
	probe.log("=== まとめ（この表が結論） ===");
	const char* stageNames[] = {"S0 動かす前",
								"S1 上階+100（reset 前）",
								"S2 上階+100（reset 後）",
								"S3 上階を戻す（reset 前）",
								"S4 上階を戻す（reset 後）",
								"S5 下階+100（reset 前）",
								"S6 下階+100（reset 後）",
								"S7 下階を戻す（reset 後）"};
	probe.log("メンバ | 段階 | パス z1-z0 | 挿入点Z | 端点の絶対Z | ID 0 のレコード | "
			  "ID 1 のレコード");
	for (size_t i = 0; i < members.size(); ++i)
	{
		const Member& member = members[i];
		for (size_t s = 0; s < member.stages.size(); ++s)
		{
			const Snapshot& snapshot = member.stages[s];
			const std::string stageName =
				s < sizeof(stageNames) / sizeof(stageNames[0]) ? stageNames[s] : Int((long long)s);
			probe.log(member.label + " | " + stageName + " | " +
					  (snapshot.pathRead ? Num(snapshot.deltaZ) : std::string("読めない")) + " | " +
					  (snapshot.pathRead ? Num(snapshot.insertion) : std::string("-")) + " | " +
					  (snapshot.pathRead ? Num(snapshot.absStart) + " → " + Num(snapshot.absEnd)
										 : std::string("-")) +
					  " | " + BoundCell(snapshot, 0) + " | " + BoundCell(snapshot, 1));
		}
	}

	// 表を読まなくても結論が分かる行を、問いごとに 1 本ずつ出す。
	probe.log("=== 判定（問いごとに 1 行） ===");
	if (!upperMoved)
	{
		probe.fail("**上階が動かなかった**（SetStoryElevation の読み戻しが変わらない）。"
				   "階を動かせていないので、この実行は issue #65 の問いに答えていない。");
	}
	for (size_t i = 0; i < members.size(); ++i)
	{
		const Member& member = members[i];
		if (member.stages.size() < 3)
			continue;
		const Snapshot& s0 = member.stages[0];
		const Snapshot& s1 = member.stages[1];
		const Snapshot& s2 = member.stages[2];
		const Snapshot& s4 = member.stages.size() > 4 ? member.stages[4] : s2;

		std::string verdict1;
		if (!s0.pathRead || !s2.pathRead)
			verdict1 = "パスが読めない";
		else if (s2.deltaZ == s0.deltaZ)
			verdict1 = "**長さは変わらなかった**（" + Num(s0.deltaZ) + " のまま）";
		else
			verdict1 = "**長さが " + Num(s0.deltaZ) + " → " + Num(s2.deltaZ) + " へ変わった**";

		std::string verdict2;
		if (!s1.pathRead || !s0.pathRead)
			verdict2 = "パスが読めない";
		else if (s1.deltaZ != s0.deltaZ || s1.insertion != s0.insertion)
			verdict2 = "**ResetObject を呼ぶ前から動いていた**（VW が勝手に作り直している）";
		else
			verdict2 = "ResetObject を呼ぶまでは動かなかった";

		const std::string verdict3 = "ID 1 のレコード S0→S2: " + DescribeChange(s0, s2, 1) +
									 " ／ ID 0: " + DescribeChange(s0, s2, 0);

		const std::string verdict4 =
			(s0.pathRead && s4.pathRead && s4.deltaZ == s0.deltaZ && s4.insertion == s0.insertion &&
			 DescribeChange(s0, s4, 0) == "変化なし" && DescribeChange(s0, s4, 1) == "変化なし")
				? "**元へ戻した後は S0 と同一**（可逆）"
				: "**元へ戻しても S0 と同じにはならない**（S0→S4: 長さ " +
					  (s0.pathRead && s4.pathRead ? Num(s0.deltaZ) + " → " + Num(s4.deltaZ)
												  : std::string("読めない")) +
					  " ／ ID0 " + DescribeChange(s0, s4, 0) + " ／ ID1 " +
					  DescribeChange(s0, s4, 1) + "）";

		probe.log(member.label + " ── 問い 1（階を動かすと長さは変わるか）: " + verdict1);
		probe.log(member.label + " ── 問い 2（いつ変わるか）: " + verdict2);
		probe.log(member.label + " ── 問い 3（レコードはまた書き換わるか）: " + verdict3);
		probe.log(member.label + " ── 可逆か（S0 と S4 の比較）: " + verdict4);
	}
	probe.log("読み方: **A の問い 1** が issue #65 の中心（0 長へ潰れた部材が階の移動で"
			  "「生えてくる」のか）。**B（対照）** が同じ問いに「ふつうの部材はこう動く」と"
			  "答えるので、A と B が違えば差し替え経路のせいだと言える。");

	// 図面の階の高さは S7 で元へ戻してある。作った部材 3 体は残る（捨ててよい複製で
	// 走らせてもらう前提）。戻っていなければ、それ自体が知見なので大きく出す。
	const WorldCoord finalLower = gSDK->GetStoryElevation(lower.handle);
	const WorldCoord finalUpper = gSDK->GetStoryElevation(upper.handle);
	probe.log("後始末: 階の高さ 下=" + Num(finalLower) + "（元 " + Num(originalLowerElevation) +
			  "） 上=" + Num(finalUpper) + "（元 " + Num(originalUpperElevation) + "）" +
			  ((finalLower == originalLowerElevation && finalUpper == originalUpperElevation)
				   ? " → **元通り**"
				   : " → **元に戻っていない**"));
	if (finalLower != originalLowerElevation || finalUpper != originalUpperElevation)
		probe.fail("階の高さを元へ戻せなかった（上のログの「後始末」を参照）。"
				   "この図面はもう捨ててよい複製としてしか使えない。");
	probe.log("後始末: **作った構造材 PIO 3 体は図面に残る**（プローブは undo イベントを"
			  "開かないため）。捨ててよい複製で走らせていれば、そのまま捨ててよい。");
}
