//
//	probes/runtime/doregen-nonvertical/probe.cpp
//
//	[issue #109] `doRegen=false` で作った**水平材・斜め材**は、最初の `ResetObject` で
//	正しい形になるか（作り直しを 2 回から 1 回へ）。
//
//	## なぜこれを走らせるか
//
//	Findings「Parametric Objects」の「`doRegen=false` は速い。ただし 0 長のパスで作ること」
//	（#81）は**鉛直材だけ**で取った結論である。1 度目の `ResetObject` の狂いは
//	「バウンドの span ＋ 作るときに渡したパスの長さ」だったが、#81 が振ったのは
//	**Z の差だけ**なので、狂いの源が
//
//	  - **渡した Z の差**なら → 水平材・斜め材は「Z を 0 で渡す」だけで 1 回目から正しい。
//	  - **渡したパスの 3 次元長**なら → 水平材は平面上の長さ（＝部材長そのもの）を
//	    0 にできないので、**この速くする道は鉛直材にしか使えない**。
//
//	のどちらなのかが分からない。取り込みが作る構造材 517 本のうち鉛直材は約 200 本で、
//	残りは水平材 266 本・斜め材 54 本なので、ここが決まらないと実装に入れない。
//
//	## 群
//
//	  B 群 … **狂いの源の切り分け。** 鉛直 / 水平 / 斜めを、渡すパスとバウンドを
//	          組み替えて作る。**同じケースを `doRegen` の true と false で 2 本ずつ**作り、
//	          1 回目・2 回目の `ResetObject` の後を読み比べる。
//	  C 群 … **端部オフセット（`StartOffset` / `EndOffset`）とプラグインスタイル**を
//	          当てても B 群の結論が変わらないか。
//	  D 群 … **自己修復**（1 回目の後に潰れていたら `SetCustomObjectPath` → `ResetObject`）が
//	          `doRegen=false` で作った材でも効くか。
//	  E 群 … **所要。** 水平材・斜め材で `doRegen` の true / false を 30 本ずつ。
//
//	## 読み方
//
//	**`doRegen=true` の行がこの実機での「正しい形」**である（#81 の物差しと同じ考え方）。
//	`doRegen=false` の 1 回目がそれと一致していれば「1 回の `ResetObject` で正しい」。
//	一致しないときは**平面長 / Z の差**のどちらがどれだけずれたかを見る——ずれが
//	「渡した 3 次元長」と一致するなら源は 3 次元長、「渡した Z の差」と一致するなら
//	源は Z の差である。各ケースの末尾に、その突き合わせを 1 行で出す。
//
//	プローブは新規の空図面で走る前提で、undo イベントは開かない。
//

#include "Probe.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// -----------------------------------------------------------------------
	// 形の定数。**新規の空図面（レイヤ高さ 0）では `LayerElevation` の offset が
	// そのまま解決Zになる**（#81 と同じ置き方）。
	const double kElevLow = 2500;			   // 下端のバウンド offset
	const double kElevHigh = 5500;			   // 上端のバウンド offset
	const double kSpan = kElevHigh - kElevLow; // 3000 ＝ 鉛直材・斜め材の Z の差
	const double kPlanar = 2000; // 水平材・斜め材に持たせる平面上の長さ

	const int kTimedCount = 30; // E 群の本数（#81 と揃える）

	// C 群で当てる端部オフセット（Findings のパラメータ表の索引 22 / 18）。
	const double kStartOffsetValue = 150;
	const double kEndOffsetValue = -250;

	// 一致と見なす窓。作り直しは 1〜2 ULP の残差を残す（#67 / #71）ので 1e-6 で見る。
	const double kEpsilon = 1e-6;

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

	bool Near(double a, double b)
	{
		return std::fabs(a - b) < kEpsilon;
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

	// -----------------------------------------------------------------------
	// 読み戻し。**パスの両端・平面長・3 次元長・挿入点Z・解決バウンド**をひと揃いで取る。
	struct Snap
	{
		bool ok = false;
		WorldPt3 p0, p1;
		double dx = 0, dy = 0, dz = 0;
		double planar = 0; // 平面上の長さ（＝水平材・斜め材の「材の長さ」の素）
		double len3 = 0;	  // 3 次元長
		double insertZ = 0;	  // 挿入点Z（GetEntityMatrix の offset）
		double boundLow = 0;  // GetObjectBoundElevation(ID 0)
		double boundHigh = 0; // GetObjectBoundElevation(ID 1)
	};

	// 端点の絶対Z。**PIO のジオメトリはローカル座標で持たれる**ので挿入点を足す。
	double AbsZ0(const Snap& snap)
	{
		return snap.insertZ + snap.p0.z;
	}

	double AbsZ1(const Snap& snap)
	{
		return snap.insertZ + snap.p1.z;
	}

	Snap Read(MCObjectHandle pio)
	{
		Snap snap;
		if (pio == nullptr)
			return snap;
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
			return snap;
		if (!gSDK->NurbsGetPt3D(path, 0, 0, snap.p0))
			return snap;
		if (!gSDK->NurbsGetPt3D(path, 0, 1, snap.p1))
			return snap;
		snap.ok = true;
		snap.dx = snap.p1.x - snap.p0.x;
		snap.dy = snap.p1.y - snap.p0.y;
		snap.dz = snap.p1.z - snap.p0.z;
		snap.planar = std::sqrt(snap.dx * snap.dx + snap.dy * snap.dy);
		snap.len3 = std::sqrt(snap.planar * snap.planar + snap.dz * snap.dz);

		TransformMatrix matrix;
		gSDK->GetEntityMatrix(pio, matrix);
		// **平行移動の成分は `P()`**（`offset` は無名共用体 `v2` の中の名前で、
		// `TransformMatrix` の直接のメンバではない。`MathCoordTypes.h`）。
		snap.insertZ = matrix.P().z;

		snap.boundLow = gSDK->GetObjectBoundElevation(pio, static_cast<MockUp::TObjectBoundID>(0));
		snap.boundHigh = gSDK->GetObjectBoundElevation(pio, static_cast<MockUp::TObjectBoundID>(1));
		return snap;
	}

	std::string Describe(const Snap& snap)
	{
		if (!snap.ok)
			return "**パスを読めなかった**";
		return "Δ=(" + Num(snap.dx) + ", " + Num(snap.dy) + ", " + Num(snap.dz) +
			   ") 平面長=" + Num(snap.planar) + " 3 次元長=" + Num(snap.len3) +
			   " 端部の絶対Z=" + Num(AbsZ0(snap)) + "→" + Num(AbsZ1(snap)) +
			   " 解決バウンド=" + Num(snap.boundLow) + "/" + Num(snap.boundHigh);
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

	// 構造材 PIO を 1 本作り、バウンドを両端に書く。**`ResetObject` は呼ばない**。
	MCObjectHandle CreateMember(bool doRegen, double dx, double dy, double dz, double elevLow,
								double elevHigh)
	{
		MCObjectHandle curve = MakePath(dx, dy, dz);
		if (curve == nullptr)
			return nullptr;
		MCObjectHandle noProfile = nullptr;
		MCObjectHandle pio =
			gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile, doRegen);
		if (pio == nullptr)
			return nullptr;
		WriteLayerElevationBound(pio, 0, elevLow);
		WriteLayerElevationBound(pio, 1, elevHigh);
		return pio;
	}

	// -----------------------------------------------------------------------
	// B 群。

	struct Recipe
	{
		const char* label;
		double dx, dy, dz;		  // 作るときに渡すパスの差
		double elevLow, elevHigh; // バウンドの offset
	};

	const Recipe kRecipes[] = {
		// #81 の再現（この実機での物差し）。
		{"B1 鉛直・0 長のパス", 0, 0, 0, kElevLow, kElevHigh},
		{"B2 鉛直・Z 差 1 のパス", 0, 0, 1, kElevLow, kElevHigh},
		// **主題**——プラグインが作る形（両端の Z は 0、平面上の長さを持つ）。
		{"B3 水平・平面長 2000 / Z 差 0", kPlanar, 0, 0, kElevLow, kElevLow},
		{"B4 斜め・平面長 2000 / Z 差 0", kPlanar, 0, 0, kElevLow, kElevHigh},
		// 比較用——Z も渡した二重指定と、水平材に Z を 1 だけ持たせた形。
		{"B5 斜め・平面長 2000 / Z 差 3000", kPlanar, 0, kSpan, kElevLow, kElevHigh},
		{"B6 水平・平面長 2000 / Z 差 1", kPlanar, 0, 1, kElevLow, kElevLow},
	};

	struct Outcome
	{
		bool made = false;
		Snap born, first, second;
	};

	// 1 本作って `ResetObject` を 2 回。各段階を読み戻す。
	Outcome RunOne(vwprobe::Report& probe, const Recipe& recipe, bool doRegen)
	{
		Outcome outcome;
		MCObjectHandle pio =
			CreateMember(doRegen, recipe.dx, recipe.dy, recipe.dz, recipe.elevLow, recipe.elevHigh);
		if (pio == nullptr)
		{
			probe.fail(std::string(recipe.label) +
					   ": 部材を作れなかった（doRegen=" + (doRegen ? "true" : "false") + "）");
			return outcome;
		}
		outcome.made = true;
		const std::string tag = doRegen ? "    doRegen=true  " : "    doRegen=false ";
		outcome.born = Read(pio);
		probe.log(tag + "作った直後  " + Describe(outcome.born));
		gSDK->ResetObject(pio);
		outcome.first = Read(pio);
		probe.log(tag + "Reset 1 回目 " + Describe(outcome.first));
		gSDK->ResetObject(pio);
		outcome.second = Read(pio);
		probe.log(tag + "Reset 2 回目 " + Describe(outcome.second));
		return outcome;
	}

	// 「バウンドが決めた形」になっているか。**水平成分は渡したパスが決め、Z の差は
	// バウンドが決める**（#73 / #75）ので、その 2 つを別々に見る。
	bool LooksRight(const Snap& snap, const Recipe& recipe)
	{
		if (!snap.ok)
			return false;
		const double wantZ = recipe.elevHigh - recipe.elevLow;
		const double wantPlanar = std::sqrt(recipe.dx * recipe.dx + recipe.dy * recipe.dy);
		return Near(std::fabs(snap.dz), wantZ) && Near(std::fabs(snap.planar), wantPlanar);
	}

	// **doRegen=false の 1 回目**が **doRegen=true の 1 回目**と一致するか。
	// 一致しないときは、ずれが「渡した 3 次元長」と「渡した Z の差」のどちらに
	// 一致するかまで出す（**それが狂いの源**）。
	void Judge(vwprobe::Report& probe, const Recipe& recipe, const Outcome& test,
			   const Outcome& truth)
	{
		if (!test.first.ok || !truth.first.ok)
		{
			probe.log("    → **判定できなかった**（どちらかを読めなかった）");
			return;
		}
		const double seedLen3 =
			std::sqrt(recipe.dx * recipe.dx + recipe.dy * recipe.dy + recipe.dz * recipe.dz);
		const double gapPlanar = std::fabs(test.first.planar) - std::fabs(truth.first.planar);
		const double gapZ = std::fabs(test.first.dz) - std::fabs(truth.first.dz);
		const bool same = Near(gapPlanar, 0) && Near(gapZ, 0);

		probe.log(std::string("    → 1 回目の判定: ") +
				  (same ? "**doRegen=true と一致（1 回で正しい）**"
						: "**不一致（1 回では正しくない）**") +
				  " ／ 平面長のずれ=" + Num(gapPlanar) + " ／ Z の差のずれ=" + Num(gapZ));
		if (!same)
		{
			std::string source = "**どちらでもない**";
			if (Near(gapZ, seedLen3) || Near(gapPlanar, seedLen3))
				source = "**渡した 3 次元長（" + Num(seedLen3) + "）と一致**";
			else if (Near(gapZ, recipe.dz) || Near(gapPlanar, recipe.dz))
				source = "**渡した Z の差（" + Num(recipe.dz) + "）と一致**";
			probe.log("      ずれの正体: " + source + "（渡した 3 次元長=" + Num(seedLen3) +
					  " ／ 渡した Z の差=" + Num(recipe.dz) + "）");
		}
		// 2 回目で収まるか（#81 の鉛直材では必ず収まった）。**1 回目が正しければ
		// 2 回目は要らない**ので、ここは「狂ったときに救えるか」を見る行である。
		if (test.second.ok)
			probe.log(std::string("      2 回目の ResetObject の後: ") +
					  (LooksRight(test.second, recipe) ? "**正しい形に収まった**"
													   : "**まだ正しくない**"));
	}

	// -----------------------------------------------------------------------
	// C 群の下ごしらえ: プラグインスタイルを 1 本作る（#98 で確かめた手順。
	// **`CreatePluginStyle` は呼んではいけない**——文書の PIO が全滅する）。
	RefNumber MakeStructuralMemberStyle(vwprobe::Report& probe)
	{
		MCObjectHandle seed = CreateMember(true, 0, 0, 0, kElevLow, kElevHigh);
		if (seed == nullptr)
		{
			probe.fail("C: スタイルの元にする部材を作れなかった");
			return 0;
		}
		gSDK->ResetObject(seed);

		TXString name("試験スタイル #109");
		MCObjectHandle symDef = gSDK->CreateSymbolDefinition(name);
		if (symDef == nullptr)
		{
			probe.fail("C: CreateSymbolDefinition が nil を返した");
			return 0;
		}
		const bool added = gSDK->AddObjectToContainer(seed, symDef);
		gSDK->ResetObject(symDef); // **サブタイプを書く前に通す**（#98）
		const Sint32 internalID = static_cast<Sint32>(VWParametricObj::GetInternalID(seed));
		gSDK->SetSymbolDefSubType(symDef, internalID);
		gSDK->SetAllPluginStyleParameters(symDef, kPluginStyleParameter_ByStyle);

		const Sint32 readBack = gSDK->GetSymbolDefSubType(symDef);
		const bool isStyle = gSDK->IsPluginStyle(symDef);
		const RefNumber ref = static_cast<RefNumber>(gSDK->GetObjectInternalIndex(symDef));
		probe.log(
			"  スタイルを作る: AddObjectToContainer=" + std::string(added ? "true" : "false") +
			" ／ 内部 ID=" + Count(internalID) + " ／ subType の読み戻し=" + Count(readBack) +
			" ／ IsPluginStyle=" + std::string(isStyle ? "true" : "false") +
			" ／ ref=" + Count(static_cast<long long>(ref)));
		if (!isStyle || ref == 0)
		{
			probe.fail("C: プラグインスタイルを用意できなかった");
			return 0;
		}
		return ref;
	}

	// C 群 1 件。端部オフセットとスタイルの有無を組み替えて 1 本作り、**`ResetObject` を
	// 1 回だけ**呼んで読む。
	void RunDressed(vwprobe::Report& probe, const std::string& label, const Recipe& recipe,
					bool doRegen, bool withOffsets, RefNumber styleRef)
	{
		MCObjectHandle pio =
			CreateMember(doRegen, recipe.dx, recipe.dy, recipe.dz, recipe.elevLow, recipe.elevHigh);
		if (pio == nullptr)
		{
			probe.fail(label + ": 部材を作れなかった");
			return;
		}
		// 実プラグインと同じ順序（作る → スタイル → バウンド → パラメータ → リセット。
		// バウンドは CreateMember の中で書いてある）。
		std::string applied;
		if (styleRef != 0)
		{
			const bool set = gSDK->SetPluginObjectStyle(pio, styleRef);
			RefNumber readBack = 0;
			gSDK->GetPluginObjectStyle(pio, readBack);
			applied += " スタイル=" + std::string(set ? "true" : "false") +
					   "(読み戻し ref=" + Count(static_cast<long long>(readBack)) + ")";
		}
		if (withOffsets)
		{
			VWParametricObj(pio).SetParamReal("StartOffset", kStartOffsetValue);
			VWParametricObj(pio).SetParamReal("EndOffset", kEndOffsetValue);
			applied += " 端部オフセット=" + Num(VWParametricObj(pio).GetParamReal("StartOffset")) +
					   "/" + Num(VWParametricObj(pio).GetParamReal("EndOffset")) + "(読み戻し)";
		}
		probe.log("    " + label + applied);
		gSDK->ResetObject(pio);
		probe.log("      Reset 1 回目 " + Describe(Read(pio)));
	}

} // namespace

VW_PROBE("doregen-nonvertical", "doRegen=false を水平材・斜め材で確かめる",
		 "doRegen=false で作った水平材・斜め材が 1 回の ResetObject で正しい形になるかを、"
		 "doRegen=true と並べて読み比べる。端部オフセット・スタイル・自己修復・所要も測る。"
		 "新規の空図面で走らせること")
{
	probe.log("=== この図面について ===");
	{
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		TXString layerName;
		if (currentLayer != nullptr)
			gSDK->GetObjectName(currentLayer, layerName);
		probe.log(std::string("いまのレイヤ: \"") + static_cast<const char*>(layerName) + "\"");
		probe.log("バウンド offset は " + Num(kElevLow) + " / " + Num(kElevHigh) +
				  "（新規の空図面ならこの値がそのまま解決Zになる）");
	}

	// =======================================================================
	probe.log("=== B. 狂いの源の切り分け（鉛直 / 水平 / 斜め × doRegen） ===");
	for (size_t i = 0; i < sizeof(kRecipes) / sizeof(kRecipes[0]); ++i)
	{
		const Recipe& recipe = kRecipes[i];
		probe.log("  " + std::string(recipe.label) + ": 渡したパス Δ=(" + Num(recipe.dx) + ", " +
				  Num(recipe.dy) + ", " + Num(recipe.dz) + ") ／ バウンド offset " +
				  Num(recipe.elevLow) + " / " + Num(recipe.elevHigh) + " ／ 期待する Z の差 " +
				  Num(recipe.elevHigh - recipe.elevLow) + " ／ 期待する平面長 " +
				  Num(std::sqrt(recipe.dx * recipe.dx + recipe.dy * recipe.dy)));
		const Outcome truth = RunOne(probe, recipe, true);
		const Outcome test = RunOne(probe, recipe, false);
		if (truth.made && test.made)
			Judge(probe, recipe, test, truth);
	}

	// =======================================================================
	probe.log("=== C. 端部オフセットとプラグインスタイルを当てても変わらないか ===");
	{
		const RefNumber styleRef = MakeStructuralMemberStyle(probe);
		// B3（水平）と B4（斜め）——プラグインが実際に作る形——だけを着せ替える。
		const Recipe dressed[] = {kRecipes[2], kRecipes[3]};
		for (size_t i = 0; i < sizeof(dressed) / sizeof(dressed[0]); ++i)
		{
			const Recipe& recipe = dressed[i];
			probe.log("  " + std::string(recipe.label) + " を着せ替える");
			for (int pass = 0; pass < 2; ++pass)
			{
				const bool doRegen = (pass == 0);
				const std::string tag =
					doRegen ? std::string("doRegen=true ") : std::string("doRegen=false");
				RunDressed(probe, tag + " オフセットのみ", recipe, doRegen, true, 0);
				if (styleRef != 0)
				{
					RunDressed(probe, tag + " スタイルのみ", recipe, doRegen, false, styleRef);
					RunDressed(probe, tag + " 両方", recipe, doRegen, true, styleRef);
				}
			}
		}
		probe.log("  → **doRegen=false の行が doRegen=true の行と同じなら、"
				  "端部オフセットもスタイルも結論を変えない**");
	}

	// =======================================================================
	probe.log("=== D. 自己修復（1 回目が潰れていたら差し替えてもう一度 ResetObject） ===");
	{
		// 水平・斜め・鉛直（Z 差 1 ＝ #81 で狂うと分かっている形）の 3 つで試す。
		const Recipe repaired[] = {kRecipes[2], kRecipes[3], kRecipes[1]};
		for (size_t i = 0; i < sizeof(repaired) / sizeof(repaired[0]); ++i)
		{
			const Recipe& recipe = repaired[i];
			MCObjectHandle pio = CreateMember(false, recipe.dx, recipe.dy, recipe.dz,
											  recipe.elevLow, recipe.elevHigh);
			if (pio == nullptr)
			{
				probe.fail(std::string(recipe.label) + ": D 群の部材を作れなかった");
				continue;
			}
			gSDK->ResetObject(pio);
			const Snap first = Read(pio);
			probe.log("  " + std::string(recipe.label) + " ／ doRegen=false ／ Reset 1 回目 " +
					  Describe(first));
			probe.log("    1 回目で正しいか: " +
					  std::string(LooksRight(first, recipe) ? "yes（修復は要らない）" : "**no**"));

			// **正しい最終形**（ローカル座標）へ差し替えてもう一度リセットする。
			// `SetCustomObjectPath` は変換を一切しない（Findings）ので、渡す形が最終形。
			const double wantZ = recipe.elevHigh - recipe.elevLow;
			MCObjectHandle fixed = MakePath(recipe.dx, recipe.dy, wantZ);
			if (fixed == nullptr)
			{
				probe.fail(std::string(recipe.label) + ": 差し替えるパスを作れなかった");
				continue;
			}
			const bool replaced = gSDK->SetCustomObjectPath(pio, fixed);
			gSDK->ResetObject(pio);
			const Snap after = Read(pio);
			probe.log("    差し替え(" + std::string(replaced ? "true" : "false") +
					  ") → Reset      " + Describe(after));
			probe.log("    修復できたか: " +
					  std::string(LooksRight(after, recipe) ? "**yes**" : "**no**") +
					  "（差し替えはバウンドを書き換える——解決バウンドの欄を見る）");
		}
	}

	// =======================================================================
	probe.log("=== E. 所要（" + Count(kTimedCount) + " 本ずつ） ===");
	{
		const Recipe timed[] = {kRecipes[2], kRecipes[3]}; // 水平・斜め
		for (size_t i = 0; i < sizeof(timed) / sizeof(timed[0]); ++i)
		{
			const Recipe& recipe = timed[i];
			for (int pass = 0; pass < 2; ++pass)
			{
				const bool doRegen = (pass == 0);
				double createMs = 0, resetMs = 0;
				long long right = 0;
				Stopwatch total;
				for (int n = 0; n < kTimedCount; ++n)
				{
					Stopwatch watch;
					MCObjectHandle pio = CreateMember(doRegen, recipe.dx, recipe.dy, recipe.dz,
													  recipe.elevLow, recipe.elevHigh);
					createMs += watch.elapsedMs();
					if (pio == nullptr)
					{
						probe.fail(std::string(recipe.label) + ": E 群の部材を作れなかった");
						break;
					}
					watch.restart();
					gSDK->ResetObject(pio);
					resetMs += watch.elapsedMs();
					if (LooksRight(Read(pio), recipe))
						++right;
				}
				const double totalMs = total.elapsedMs();
				probe.log("  " + std::string(recipe.label) +
						  " ／ doRegen=" + (doRegen ? "true " : "false") + " ／ 1 本 " +
						  Ms(totalMs / kTimedCount) + " ms（合計 " + Ms(totalMs) + " ms ＝ 作る " +
						  Ms(createMs) + " ms ＋ リセット " + Ms(resetMs) +
						  " ms ＋ 読み戻し）／ 1 回で正しかった本数 " + Count(right) + " / " +
						  Count(kTimedCount));
			}
		}
		probe.log("  → **doRegen=false の「1 回で正しかった本数」が " + Count(kTimedCount) +
				  " なら、水平材・斜め材でもこの作りに替えてよい**");
	}

	probe.log("=== まとめ ===");
	probe.log("  読む順: B（狂いの源）→ C（オフセット・スタイル）→ D（自己修復）→ E（所要）");
}
