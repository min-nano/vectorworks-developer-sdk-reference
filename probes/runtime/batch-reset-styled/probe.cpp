//
//	probes/runtime/batch-reset-styled/probe.cpp
//
//	[issue #81] **1 本ごとの `ResetObject` を、まとめて 1 度に寄せられるか。**（2 回目）
//
//	## 1 回目（ビルド b3e9dd722956）で実機から取れたもの
//
//	  - **まとめて作り直す口は無い。** 置いただけの部材 5 本に対して
//	    `ResetObject(レイヤのハンドル)` は 0 本しか作り直さず（所要 0.2ms ＝ 何もしていない）、
//	    `RedrawRect` も 0 本だった（13.2ms）。→ **この 2 つはもう調べ直さない**ので、
//	    この版では叩かない。
//	  - **所要（30 本、mac）**: いまの作り（`doRegen=true` → バウンド → 1 本ごとの
//	    `ResetObject`）は 1 本 23.6ms（作る 12.0ms ／ リセット 11.5ms ／ バウンド 0.02ms）。
//	    `doRegen=false` にすると「作る」が 12.0ms → 0.45ms になり、1 本 11.9ms へ半減した。
//	    **「全部置いてから第 2 パスでまとめてリセット」は 11.8ms で、半減以上の得は無い**
//	    （リセットの回数が同じだから）。
//	  - **ところが `doRegen=false` で作ると、後の `ResetObject` の結果が狂う。**
//	    バウンドの span は 3000 なのに、読み戻した Z の差が **3001**（渡したパスの
//	    長さ 1 のぶんだけ長い）になった。30 本すべてで同じ。
//	  - **`CreatePluginStyle` ではスタイルを用意できなかった**（`GetPluginObjectStyle` が
//	    false、`styleRef` は 0）。**issue の (b)——`UpdateStyledObjects` がジオメトリの
//	    作り直しを兼ねるか——は測れていない。** ここがこの版の主題。
//
//	## この版で決めること
//
//	  1. **スタイルを実機で用意する道を見つける**（C 群）。4 通りを順に試し、
//	     どれが効いたかをログに残す:
//	       R1 `GetPluginStyleForTool`（構造材ツールに設定されているスタイル）
//	       R2 `CreatePluginStyle` → `GetPluginStyleSymbol`（**関連付けは別で、
//	          スタイル自体はできているかもしれない**——1 回目はここを見ていない）
//	       R3 文書の資源一覧（`BuildResourceListUnsorted(kSymDefNode, 0, ...)`）から
//	          `IsPluginStyle` が真のものを拾う。**R2 の前後で 2 度数える**ので、
//	          `CreatePluginStyle` が何か作ったかどうかも分かる
//	       R4 `CreateSymbolDefinition` ＋ `SetSymbolDefSubType` を 0..12 で振り、
//	          `IsPluginStyle` が真になる値を探す（SDK のヘッダに定数が無いため実測で探す）
//	     どれかで `RefNumber` が取れたら、**スタイルを当てた部材を `ResetObject` 抜きで
//	     置いて `UpdateStyledObjects` を 1 回だけ呼び**、実体が出るかを読む（issue の (b)）。
//	  2. **`doRegen=false` の狂いの規則**（E 群）。渡す長さ（seed）を 0 / 1 / 5 / 100 と
//	     振って、作り直しの結果が **span + seed** になるのかを見る。**seed = 0 なら
//	     ぴたりと span になるなら、「0 長で作って `doRegen=false`」が速くて正しい作りに
//	     なる**（作るのが 26 倍速くなるので、ここが通れば取り込みは半分の時間で済む）。
//	     2 度目の `ResetObject` で更に伸びるか（累積するか）も見る。
//	  3. **その作りの所要と正しさ**（D 群）。1 回目の D1（いまの作り）を物差しに、
//	     **`doRegen=false` ＋ seed 0** を 30 本で計り、30 本とも span になるかを読む。
//
//	## 読み方
//
//	バウンドは `LayerElevation` の offset 2500 / 5500（span 3000）。**読み戻した Z の差が
//	3000 なら作り直された**、渡した seed のままなら作り直されていない、それ以外
//	（3001 など）は**狂っている**。
//

#include "Probe.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// バウンドの解決Z（新規の空図面＝レイヤ高さ 0 で 2500 / 5500 になる）。
	const double kBoundOffsetLow = 2500;
	const double kBoundOffsetHigh = 5500;
	const double kBoundSpan = kBoundOffsetHigh - kBoundOffsetLow; // 3000

	// 既定の seed（作るときに渡す Z の差）。1 回目と同じ値。
	const double kDefaultSeed = 1;

	const int kStyledGroupCount = 5; // C 群（UpdateStyledObjects）の本数
	const int kTimedGroupCount = 30; // D 群（所要）の本数。1 回目と同じ

	// 文書の資源一覧を引くときの種別と置き場（`Objs.TDType.h` の kSymDefNode、
	// および VectorScript の BuildResourceList と同じ「0 ＝ いまの文書」）。
	const short kSymbolDefinitionType = 16;

	std::string Num(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.17g", value);
		return std::string(buffer);
	}

	std::string Ms(double milliseconds)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.1f", milliseconds);
		return std::string(buffer);
	}

	std::string Count(long long value)
	{
		return std::to_string(value);
	}

	class Stopwatch
	{
	public:
		Stopwatch() : fStart(std::chrono::steady_clock::now()) {}
		void restart()
		{
			fStart = std::chrono::steady_clock::now();
		}
		double elapsedMs() const
		{
			const std::chrono::duration<double, std::milli> delta =
				std::chrono::steady_clock::now() - fStart;
			return delta.count();
		}

	private:
		std::chrono::steady_clock::time_point fStart;
	};

	bool ReadPathEndpoints(MCObjectHandle pio, WorldPt3& outP0, WorldPt3& outP1)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
			return false;
		const Boolean got0 = gSDK->NurbsGetPt3D(path, 0, 0, outP0);
		const Boolean got1 = gSDK->NurbsGetPt3D(path, 0, 1, outP1);
		return got0 && got1;
	}

	// 読み戻した Z の差。読めなければ NaN。
	double ReadSpan(MCObjectHandle pio)
	{
		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(pio, p0, p1))
			return std::nan("");
		return p1.z - p0.z;
	}

	// 作り直しは 1〜2 ULP の残差を残す（#67 / #71）ので 1e-6 の窓で見る。
	bool LooksRebuilt(double span)
	{
		return std::fabs(std::fabs(span) - kBoundSpan) < 1e-6;
	}

	std::string DescribeSpan(double span, double seed)
	{
		if (std::isnan(span))
			return "(パスを読めなかった)";
		std::string text = Num(span);
		if (LooksRebuilt(span))
			text += "（**バウンドどおり**）";
		else if (std::fabs(std::fabs(span) - seed) < 1e-9)
			text += "（渡したまま＝**作り直されていない**）";
		else if (std::fabs(std::fabs(span) - (kBoundSpan + seed)) < 1e-6)
			text += "（**span + seed ＝ 狂っている**）";
		else
			text += "（**どれでもない**）";
		return text;
	}

	bool WriteLayerElevationBound(MCObjectHandle pio, short id, double offset)
	{
		MockUp::SStoryObjectData data;
		data.fBound = MockUp::eStoryObjectBound_LayerElevation;
		data.fBoundStory = 0;
		data.fLayerLevelType = "";
		data.fOffset = offset;
		return gSDK->SetObjectStoryBound(pio, static_cast<MockUp::TObjectBoundID>(id), data);
	}

	struct Member
	{
		MCObjectHandle handle = nullptr;
		double seed = kDefaultSeed;
	};

	// 構造材 PIO を 1 本作る。**`ResetObject` は呼ばない**。
	Member CreateMember(bool doRegen, bool writeBounds, double seed)
	{
		Member member;
		member.seed = seed;

		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(0, 0, 0), false, 1);
		if (curve == nullptr)
			return member;
		gSDK->Add3DVertex(curve, WorldPt3(0, 0, seed), true);
		// 座標を明示的に入れ直す（#67 / #74 / #75 と同じ作法）。
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, WorldPt3(0, 0, 0));
			gSDK->NurbsSetPt3D(curve, 0, 1, WorldPt3(0, 0, seed));
		}

		MCObjectHandle noProfile = nullptr;
		member.handle = gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile, doRegen);
		if (member.handle == nullptr)
			return member;

		if (writeBounds)
		{
			WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
			WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
		}
		return member;
	}

	struct GroupReadback
	{
		long long rebuilt = 0;	 // バウンドどおり
		long long untouched = 0; // 渡したまま
		long long other = 0;	 // それ以外（狂い）
		long long unreadable = 0;
		double firstSpan = std::nan("");
	};

	GroupReadback ReadGroup(const std::vector<Member>& members)
	{
		GroupReadback readback;
		for (size_t i = 0; i < members.size(); ++i)
		{
			const double span = ReadSpan(members[i].handle);
			if (i == 0)
				readback.firstSpan = span;
			if (std::isnan(span))
				++readback.unreadable;
			else if (LooksRebuilt(span))
				++readback.rebuilt;
			else if (std::fabs(std::fabs(span) - members[i].seed) < 1e-9)
				++readback.untouched;
			else
				++readback.other;
		}
		return readback;
	}

	void LogGroup(vwprobe::Report& probe, const std::string& what,
				  const std::vector<Member>& members)
	{
		const GroupReadback readback = ReadGroup(members);
		const double seed = members.empty() ? kDefaultSeed : members[0].seed;
		probe.log("    " + what + ": " + Count(static_cast<long long>(members.size())) +
				  " 本中 **バウンドどおり " + Count(readback.rebuilt) + " 本** ／ 渡したまま " +
				  Count(readback.untouched) + " 本 ／ 狂い " + Count(readback.other) +
				  " 本 ／ 読めなかった " + Count(readback.unreadable) + " 本");
		probe.log("      1 本目の Z の差 = " + DescribeSpan(readback.firstSpan, seed));
	}

	// -----------------------------------------------------------------------
	// スタイルを探す・作る（C 群の下ごしらえ）。

	// 文書の中のプラグインスタイルを数えて名前を出す。**`CreatePluginStyle` の前後で
	// 2 度呼ぶ**ので、何か増えたかどうかが分かる。
	long long InventoryStyles(vwprobe::Report& probe, const std::string& when,
							  RefNumber& outFirstStyleRef)
	{
		outFirstStyleRef = 0;
		Sint32 numItems = 0;
		// folderIndex は VectorScript の BuildResourceList と同じ「0 ＝ いまの文書」。
		const Sint32 listID = gSDK->BuildResourceListUnsorted(
			kSymbolDefinitionType, static_cast<FolderSpecifier>(0), "", numItems);
		long long styles = 0;
		for (Sint32 i = 0; i < numItems; ++i)
		{
			MCObjectHandle resource = gSDK->GetResourceFromList(listID, i);
			if (resource == nullptr)
				continue;
			if (!gSDK->IsPluginStyle(resource))
				continue;
			++styles;
			TXString name;
			gSDK->GetObjectName(resource, name);
			const RefNumber ref = static_cast<RefNumber>(gSDK->GetObjectInternalIndex(resource));
			if (outFirstStyleRef == 0)
				outFirstStyleRef = ref;
			if (styles <= 5) // 多すぎるときは頭だけ
				probe.log("      - \"" + std::string(static_cast<const char*>(name)) +
						  "\" ref=" + Count(static_cast<long long>(ref)) + " subType=" +
						  Count(static_cast<long long>(gSDK->GetSymbolDefSubType(resource))));
		}
		probe.log("    " + when + ": シンボル定義 " + Count(static_cast<long long>(numItems)) +
				  " 件中、プラグインスタイル " + Count(styles) + " 件");
		return styles;
	}
} // namespace

VW_PROBE("batch-reset-styled", "UpdateStyledObjects と doRegen=false を実測する",
		 "スタイルを 4 通りで用意して UpdateStyledObjects が"
		 "ジオメトリを作り直すかを読む。doRegen=false の狂い（span + seed）の規則と、"
		 "seed 0 なら正しく速いのかも測る。新規の空図面で走る")
{
	probe.log("=== この図面について ===");
	{
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		TXString layerName;
		if (currentLayer != nullptr)
			gSDK->GetObjectName(currentLayer, layerName);
		probe.log(std::string("いまのレイヤ: \"") + static_cast<const char*>(layerName) + "\"");
		probe.log("バウンドの span = " + Num(kBoundSpan) +
				  "（作り直されればこの値になる）／ 1 回目で分かったことは冒頭のコメント");
	}

	// =======================================================================
	// A. 物差し（1 回目と同じ。ここが崩れていたら以下は読まない）。
	probe.log("=== A. 物差し（リセット無し → 作り直されない／ResetObject 1 回 → span） ===");
	{
		const Member control = CreateMember(true, true, kDefaultSeed);
		if (control.handle == nullptr)
		{
			probe.fail("A: 部材を作れなかった");
		}
		else
		{
			probe.log("    リセット前: Z の差 = " +
					  DescribeSpan(ReadSpan(control.handle), control.seed));
			gSDK->ResetObject(control.handle);
			probe.log("    ResetObject 1 回の後: Z の差 = " +
					  DescribeSpan(ReadSpan(control.handle), control.seed));
		}
	}

	// =======================================================================
	// C. **UpdateStyledObjects（issue の (b)）。** まずスタイルを用意する。
	probe.log("=== C. UpdateStyledObjects（issue の (b)。スタイルを 4 通りで探す） ===");
	RefNumber styleRef = 0;
	std::string styleRoute;
	{
		// --- R1: 構造材ツールに設定されているスタイル ---
		{
			RefNumber toolStyle = 0;
			const bool got = gSDK->GetPluginStyleForTool("StructuralMember", toolStyle);
			probe.log("  R1 GetPluginStyleForTool(\"StructuralMember\") → " +
					  std::string(got ? "true" : "false") +
					  " ／ ref=" + Count(static_cast<long long>(toolStyle)));
			if (got && toolStyle != 0)
			{
				styleRef = toolStyle;
				styleRoute = "R1 ツールの既定スタイル";
			}
		}

		// --- R3(前半): いま文書にあるプラグインスタイルを数える ---
		RefNumber existing = 0;
		probe.log("  R3 文書の資源一覧（CreatePluginStyle の前）");
		InventoryStyles(probe, "前", existing);
		if (styleRef == 0 && existing != 0)
		{
			styleRef = existing;
			styleRoute = "R3 文書に元からあったスタイル";
		}

		// --- R2: CreatePluginStyle を呼び、**関連付けとは別に**スタイルができたかを見る ---
		{
			const Member seed = CreateMember(true, true, kDefaultSeed);
			if (seed.handle == nullptr)
			{
				probe.fail("C: スタイルの元にする部材を作れなかった");
			}
			else
			{
				gSDK->ResetObject(seed.handle);
				probe.log("  R2 CreatePluginStyle を呼ぶ（ダイアログが出たらこの行が最後になる）");
				gSDK->CreatePluginStyle(seed.handle);

				RefNumber associated = 0;
				const Boolean got = gSDK->GetPluginObjectStyle(seed.handle, associated);
				MCObjectHandle styleSymbol = nullptr;
				const bool gotSymbol = gSDK->GetPluginStyleSymbol(seed.handle, styleSymbol);
				probe.log("    GetPluginObjectStyle → " + std::string(got ? "true" : "false") +
						  " ／ ref=" + Count(static_cast<long long>(associated)) +
						  " ／ GetPluginStyleSymbol → " +
						  std::string(gotSymbol ? "true" : "false") +
						  " ／ シンボル=" + std::string(styleSymbol != nullptr ? "あり" : "nil"));
				if (styleRef == 0 && got && associated != 0)
				{
					styleRef = associated;
					styleRoute = "R2 CreatePluginStyle（関連付けまで効いた）";
				}
				else if (styleRef == 0 && styleSymbol != nullptr)
				{
					styleRef = static_cast<RefNumber>(gSDK->GetObjectInternalIndex(styleSymbol));
					styleRoute = "R2 CreatePluginStyle（シンボルから ref を引いた）";
				}
			}

			// --- R3(後半): CreatePluginStyle で増えたか ---
			RefNumber afterRef = 0;
			probe.log("  R3 文書の資源一覧（CreatePluginStyle の後）");
			InventoryStyles(probe, "後", afterRef);
			if (styleRef == 0 && afterRef != 0)
			{
				styleRef = afterRef;
				styleRoute = "R3 CreatePluginStyle が作ったスタイル";
			}
		}

		// --- R4: シンボル定義を作り、subType を振って IsPluginStyle が真になる値を探す ---
		if (styleRef == 0)
		{
			TXString name("試験プラグインスタイル");
			MCObjectHandle symDef = gSDK->CreateSymbolDefinition(name);
			probe.log("  R4 CreateSymbolDefinition → " +
					  std::string(symDef != nullptr ? "できた" : "**nil**"));
			if (symDef != nullptr)
			{
				const Sint32 originalSubType = gSDK->GetSymbolDefSubType(symDef);
				probe.log("    作った直後: subType=" +
						  Count(static_cast<long long>(originalSubType)) + " ／ IsPluginStyle=" +
						  std::string(gSDK->IsPluginStyle(symDef) ? "true" : "false"));
				std::string hits;
				for (Sint32 candidate = 0; candidate <= 12; ++candidate)
				{
					gSDK->SetSymbolDefSubType(symDef, candidate);
					if (gSDK->IsPluginStyle(symDef))
					{
						hits +=
							(hits.empty() ? "" : ", ") + Count(static_cast<long long>(candidate));
						if (styleRef == 0)
						{
							styleRef = static_cast<RefNumber>(gSDK->GetObjectInternalIndex(symDef));
							styleRoute = "R4 subType=" + Count(static_cast<long long>(candidate)) +
										 " を書いたシンボル定義";
						}
					}
				}
				probe.log("    IsPluginStyle が真になった subType: " +
						  (hits.empty() ? std::string("**無し**") : hits));
				if (hits.empty())
					gSDK->SetSymbolDefSubType(symDef, originalSubType);
			}
		}

		probe.log("  → 使うスタイル: " +
				  (styleRef == 0
					   ? std::string("**用意できなかった**")
					   : styleRoute + " ／ ref=" + Count(static_cast<long long>(styleRef))));
	}

	if (styleRef == 0)
	{
		probe.fail("C: スタイルを用意できなかったので UpdateStyledObjects を測れていない");
	}
	else
	{
		probe.log("  C1 スタイルを当てた部材を **ResetObject 抜き**で " + Count(kStyledGroupCount) +
				  " 本置く");
		std::vector<Member> styled;
		long long associated = 0;
		for (int i = 0; i < kStyledGroupCount; ++i)
		{
			Member member = CreateMember(true, false, kDefaultSeed);
			if (member.handle == nullptr)
			{
				probe.fail("C1: 部材を作れなかった");
				break;
			}
			// **スタイルを当ててからバウンドを書く**（実プラグインと同じ順序）。
			if (gSDK->SetPluginObjectStyle(member.handle, styleRef))
				++associated;
			WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
			WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
			styled.push_back(member);
		}
		probe.log("    SetPluginObjectStyle が true を返した本数: " + Count(associated) + " / " +
				  Count(static_cast<long long>(styled.size())));
		{
			// 読み戻して、ほんとうに当たっているかを見る（setter の戻り値を信じない）。
			RefNumber readBack = 0;
			if (!styled.empty())
				gSDK->GetPluginObjectStyle(styled[0].handle, readBack);
			probe.log(
				"    1 本目の GetPluginObjectStyle → ref=" +
				Count(static_cast<long long>(readBack)) +
				std::string(readBack == styleRef ? "（当たっている）" : "（**当たっていない**）"));
		}
		LogGroup(probe, "UpdateStyledObjects の前", styled);

		probe.log("  C2 UpdateStyledObjects を 1 回だけ呼ぶ");
		Stopwatch watch;
		gSDK->UpdateStyledObjects(styleRef);
		const double elapsed = watch.elapsedMs();
		LogGroup(probe, "UpdateStyledObjects の後", styled);
		probe.log("      所要 " + Ms(elapsed) + " ms（" +
				  Count(static_cast<long long>(styled.size())) + " 本ぶん）");
		probe.log("      → **バウンドどおりが 0 本なら、UpdateStyledObjects は"
				  "ジオメトリを作り直さない**（issue の (b) の答え）");

		probe.log("  C3 続けて 1 本ごとの ResetObject（効き目が残っているかの確認）");
		for (size_t i = 0; i < styled.size(); ++i)
			gSDK->ResetObject(styled[i].handle);
		LogGroup(probe, "ResetObject の後", styled);
	}

	// =======================================================================
	// E. **`doRegen=false` の狂いの規則。** seed を振って「span + seed」かを見る。
	probe.log("=== E. doRegen=false の狂い（seed を振る。span + seed になるか） ===");
	{
		const double seeds[] = {0, 1, 5, 100};
		for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i)
		{
			const double seed = seeds[i];
			Member member = CreateMember(false, true, seed);
			if (member.handle == nullptr)
			{
				probe.fail("E: 部材を作れなかった（seed=" + Num(seed) + "）");
				continue;
			}
			gSDK->ResetObject(member.handle);
			const double first = ReadSpan(member.handle);
			gSDK->ResetObject(member.handle);
			const double second = ReadSpan(member.handle);
			probe.log("  seed=" + Num(seed) + " ／ 1 度目 " + DescribeSpan(first, seed) +
					  " ／ 2 度目 " + DescribeSpan(second, seed) +
					  "（2 度目で伸びるなら**累積する**）");

			// 同じ seed を doRegen=true でも通して、差が `doRegen` だけによることを示す。
			Member control = CreateMember(true, true, seed);
			if (control.handle != nullptr)
			{
				gSDK->ResetObject(control.handle);
				probe.log("    （物差し: 同じ seed を doRegen=true で作ると " +
						  DescribeSpan(ReadSpan(control.handle), seed) + "）");
			}
		}
		probe.log("  → **seed=0 の行が「バウンドどおり」なら、"
				  "「0 長で作って doRegen=false」が速くて正しい作りになる**");
	}

	// =======================================================================
	// D. 所要。1 回目の D1（いまの作り）を物差しに、seed 0 ＋ doRegen=false を測る。
	probe.log("=== D. 所要（" + Count(kTimedGroupCount) + " 本ずつ） ===");
	{
		// D1: いまの作り（1 回目と同じ条件。この実機・この図面での物差し）。
		{
			double createMs = 0, resetMs = 0;
			Stopwatch total;
			for (int i = 0; i < kTimedGroupCount; ++i)
			{
				Stopwatch watch;
				Member member = CreateMember(true, false, kDefaultSeed);
				createMs += watch.elapsedMs();
				if (member.handle == nullptr)
				{
					probe.fail("D1: 部材を作れなかった");
					break;
				}
				WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
				WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
				watch.restart();
				gSDK->ResetObject(member.handle);
				resetMs += watch.elapsedMs();
			}
			const double totalMs = total.elapsedMs();
			probe.log("  D1 いまの作り（doRegen=true, seed=" + Num(kDefaultSeed) +
					  " → バウンド → 1 本ごとに ResetObject）");
			probe.log("    合計 " + Ms(totalMs) + " ms ／ 1 本 " + Ms(totalMs / kTimedGroupCount) +
					  " ms（作る " + Ms(createMs) + " ms ／ リセット " + Ms(resetMs) + " ms）");
		}

		// D2: **doRegen=false ＋ seed 0**。速さと正しさを同時に見る。
		{
			std::vector<Member> members;
			double createMs = 0, resetMs = 0;
			Stopwatch total;
			for (int i = 0; i < kTimedGroupCount; ++i)
			{
				Stopwatch watch;
				Member member = CreateMember(false, false, 0);
				createMs += watch.elapsedMs();
				if (member.handle == nullptr)
				{
					probe.fail("D2: 部材を作れなかった");
					break;
				}
				WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
				WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
				watch.restart();
				gSDK->ResetObject(member.handle);
				resetMs += watch.elapsedMs();
				members.push_back(member);
			}
			const double totalMs = total.elapsedMs();
			probe.log("  D2 **doRegen=false ＋ seed 0**（速くて正しいかを見る）");
			probe.log("    合計 " + Ms(totalMs) + " ms ／ 1 本 " + Ms(totalMs / kTimedGroupCount) +
					  " ms（作る " + Ms(createMs) + " ms ／ リセット " + Ms(resetMs) + " ms）");
			LogGroup(probe, "D2 の読み戻し", members);
			probe.log("    → **30 本ともバウンドどおりなら、この作りに替えてよい**");
		}
	}

	probe.log("=== まとめ ===");
	probe.log("  読む順: A（物差し）→ C（UpdateStyledObjects ＝ issue の (b)）"
			  "→ E（doRegen=false の規則）→ D（所要）");
}
