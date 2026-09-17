//
//	probes/runtime/path-z-residual-origin/probe.cpp
//
//	[issue #67] **`(0, 1e-7)` の帯に落ちた残差は、どこで生まれたのか。**
//	呼び出し側が**厳密な整数 mm**を渡していても残差が出るのか、出るならそれは
//	**どの段**（曲線／`CreateCustomObjectPath`／その後の作り直し）で生まれ、
//	**値そのもの**（mm ↔ インチの往復で戻らない値）で決まるのか。
//
//	## 1 回目の実測（VW 2026 / mac。PR #69 のコメント）で分かったこと
//
//	  * **⓪ 曲線も ① `CreateCustomObjectPath` も、渡した値をビット一致で保つ。**
//	    挿入点＝始点の絶対Z、局所パスの差＝渡した差（8 値すべて。3531 を含む）。
//	    `CreateCustomObjectPathNoOffset` でも同じ。**つまり生成の段では変換されない。**
//	  * **残差は ② 作り直し（regen）で生まれる。** バウンドを 1 本も書かずに
//	    `ResetObject` して 0 長へ潰すとき、`572 → 3531` のケースだけ
//	    **`+4.5474735088646412e-13`（＝2^-41）**が残り、他は厳密に 0 だった
//	    ——**事故で潰れた 46 本の残差（`4.54747e-13`・上端 3531）と同じ値**。
//	  * 残差が出た値と出なかった値を `v / 25.4 * 25.4 != v` で分けると、
//	    **8 ケースすべてで当たった**（6374・5500 は「戻る」側で、実測も残差なし）。
//	    SDK ヘッダの inline と同じ**逆数を掛ける形** `v * (1/25.4) * 25.4` は
//	    6374・5500 を「戻らない」と予測するので、**そちらは外れた**。
//	  * 残差が乗るのは**端点の絶対Z**であって局所の長さではない（長さ 13 / 1 の
//	    ケース——どちらも往復で戻らない値——に残差が出ていない）。
//
//	### 1 回目でこちらが踏んだ落とし穴（これも知見）
//
//	**プローブの中で予測式を計算したら、コンパイラが FMA へ縮約して別物になっていた。**
//	`value / 25.4 * 25.4 - value` は clang の既定（`-ffp-contract=on`）で
//	`fma(value/25.4, 25.4, -value)` になり、**丸めが 1 回減るぶん値が変わる**
//	（3531 なら `-2.885e-13` と出る。本当の往復誤差は `-4.547e-13`）。
//	1 回目のログの「予測B」列はこの縮約された値で、**素朴な往復ではなかった**。
//	この版では `volatile` で中間値を毎回 double へ落として縮約を止めている。
//
//	## この版（2 回目）で埋める穴
//
//	1. **予測式を正しく計算する**（上記の縮約を止める）。両方の形を並べて出す。
//	2. **下端（z0）側も振る**（F 群）。1 回目は z0 = 572（往復で戻る値）に固定して
//	   いたので、「**下端が戻らない値なら残差が出るのか**」を測っていない。
//	3. **予測を規則として試す**（G 群）。往復で戻らない値 9 つと戻る値 9 つを
//	   1 ケース 1 行で振り、**全件で予測と実測が一致するか**を数える。
//	   1 回目の 8 ケースでは当たったが、それだけでは「規則」と言えない。
//
//	## 何を測るか（1 ケース 1 オブジェクト・4 地点）
//
//	    ⓪  `CreateNurbsCurve` ＋ `Add3DVertex` した曲線をそのまま読み戻す
//	    ⓪' `NurbsSetPt3D` で座標を入れ直してから読み戻す（事故のプラグインと同じ作法）
//	    ①  `CreateCustomObjectPath` の直後（局所パス＋挿入点＝絶対Zへ戻して比べる）
//	    ②  `ResetObject` の直後（**バウンドを 1 本も書かない**＝潰れる経路。#56 の 4）
//
//	T 群だけ 4 地点を全文で出し、残りは 1 行にまとめる（**ログが長いと読まれない**。
//	1 回目で ⓪・⓪'・① がどの値でもビット一致だと分かったので、以後は誤差だけ出せば足りる）。
//
//	事故のプラグインが「生成直後に 197 本すべて 0 長」を見たのは `SetPluginObjectStyle` を
//	挟んだ後で、ここで代用しているのは `ResetObject`。**どちらも作り直し（regen）**だが、
//	同一だと確かめたわけではない——ただし 1 回目で**事故と同じ端点から同じ残差が出た**ので、
//	残差の出どころとしては同じものを見ていると読める。
//
//	【読み方】最後の「まとめ」と「予測の成績」がこのプローブの結論。
//	  * **②の差が 0**        … その値では残差が生まれていない
//	  * **②の差 ≠ 0**       … 作り直しが残差を残した（その大きさを予測と突き合わせる）
//	  * ①の誤差は**全件 0 のはず**（0 でなければ、生成の段でも変換が入っている）
//

#include "Probe.h"

#include "VWFC/VWObjects/VWGroupObj.h"
#include "VWFC/VWObjects/VWPolygon2DObj.h"

#include <cmath>
#include <cstdio>
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

	// 予測1「割ってから掛ける」: issue #67 が事故の 5 点と突き合わせた形。
	// **`volatile` は飾りではない**——これが無いと clang が `fma` 1 回へ縮約し、
	// 丸めが 1 回減って**別の値**になる（1 回目のログの「予測B」列がそれだった）。
	double RoundTripDivide(double value)
	{
		volatile double inches = value / kWorldCoordsPerInch;
		volatile double back = inches * kWorldCoordsPerInch;
		return back - value;
	}

	// 予測2「逆数を掛けてから掛ける」: SDK ヘッダの inline と同じ形
	// （`WorldCoordToInches` は `kInchesPerWorldCoord = 1 / 25.4` を掛ける）。
	double RoundTripReciprocal(double value)
	{
		volatile double inches = value * kInchesPerWorldCoord;
		volatile double back = inches * kWorldCoordsPerInch;
		return back - value;
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

	bool ReadCurveEndpoints(MCObjectHandle curve, WorldPt3& outP0, WorldPt3& outP1)
	{
		if (curve == nullptr)
			return false;
		const Boolean got0 = gSDK->NurbsGetPt3D(curve, 0, 0, outP0);
		const Boolean got1 = gSDK->NurbsGetPt3D(curve, 0, 1, outP1);
		return got0 && got1;
	}

	// 1 ケースぶんの観測（まとめの表 1 行）。
	struct Observation
	{
		std::string label;
		double passedZ0 = 0;
		double passedZ1 = 0;
		double createdError = 0;	// ① 局所の差 − 渡した差（0 のはず）
		double afterResetDelta = 0; // ② ResetObject 直後の局所 z1−z0（＝残差）
		bool divPredictsResidual = false;
		bool reciprocalPredictsResidual = false;
		std::string verdict;
		bool measured = false;
	};

	// 断面（矩形 120 × 120 のグループ）。事故のプラグインと同じ作り
	// （空のグループを渡すと実体が出ない——あちらの draw/DrawUtil）。
	MCObjectHandle MakeProfile()
	{
		VWPolygon2DObj profile(
			{VWPoint2D(-60, -60), VWPoint2D(-60, 60), VWPoint2D(60, 60), VWPoint2D(60, -60)});
		profile.SetClosed(true);
		const MCObjectHandle profileHandle = profile.GetThisObject();
		if (profileHandle == nullptr)
			return nullptr;
		VWGroupObj group;
		group.AddObject(profileHandle);
		return group.GetThisObject();
	}

	// 曲線を作る（事故のプラグインと同じ作法: Add3DVertex で足してから NurbsSetPt3D で
	// 入れ直す）。1 回目で ⓪・⓪' はどの値でもビット一致だと分かったので、**ずれたときだけ**
	// ログを出す。
	MCObjectHandle MakeStraightPath(vwprobe::Report& probe, const std::string& prefix,
									WorldPt3 from, WorldPt3 to, bool verbose)
	{
		MCObjectHandle curve = gSDK->CreateNurbsCurve(from, false, 1);
		if (curve == nullptr)
		{
			probe.log(prefix + " CreateNurbsCurve が nil を返した");
			return nullptr;
		}
		gSDK->Add3DVertex(curve, to, true);

		WorldPt3 p0, p1;
		if (ReadCurveEndpoints(curve, p0, p1))
		{
			if (verbose || p0.z != from.z || p1.z != to.z)
				probe.log(prefix + " ⓪ 曲線（Add3DVertex 直後）: z0=" + NumBoth(p0.z) + " z1=" +
						  NumBoth(p1.z) + " ／ 渡した値との差: z0=" + NumBoth(p0.z - from.z) +
						  " z1=" + NumBoth(p1.z - to.z));
		}
		else
			probe.log(prefix + " ⓪ 曲線の点が読めない（点数=" +
					  std::to_string(static_cast<long long>(gSDK->NurbsGetNumPts(curve, 0))) +
					  "）");

		// **座標を明示的に入れ直す**（Add3DVertex が足した点が渡した位置にならないことが
		// ある）。点が 2 つになっていなければ触らない。
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, from);
			gSDK->NurbsSetPt3D(curve, 0, 1, to);
		}
		if (verbose && ReadCurveEndpoints(curve, p0, p1))
			probe.log(prefix + " ⓪' 曲線（NurbsSetPt3D 後）: z0=" + NumBoth(p0.z) +
					  " z1=" + NumBoth(p1.z) + " z1-z0=" + NumBoth(p1.z - p0.z));
		return curve;
	}

	const char* EntryName(bool noOffset)
	{
		return noOffset ? "CreateCustomObjectPathNoOffset" : "CreateCustomObjectPath";
	}
} // namespace

VW_PROBE("path-z-residual-origin", "厳密な整数 mm の世界座標に残差が生まれるかを測る",
		 "厳密な整数 mm の world Z を渡し、曲線・生成直後・作り直し直後の 3 地点で読み戻して、"
		 "残差がどの段で生まれるか・mm↔インチの往復で説明が付くかを 30 ケースで確かめる。"
		 "新規の空図面で走る")
{
	probe.log("=== この図面について ===");
	{
		double unitsPerInch = 0;
		const bool gotUnits = gSDK->GetProgramVariable(varUnit1UnitsPerInch, &unitsPerInch);
		probe.log(std::string("varUnit1UnitsPerInch = ") +
				  (gotUnits ? Num(unitsPerInch) : std::string("(読めない)")) +
				  "（mm の図面なら 25.4。**WorldCoord 自体は図面の単位に依らず mm** で、"
				  "kWorldCoordsPerMM = 1 / kWorldCoordsPerInch = 25.4）");
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		TXString layerName;
		if (currentLayer != nullptr)
			gSDK->GetObjectName(currentLayer, layerName);
		probe.log(std::string("いまのレイヤ: \"") + static_cast<const char*>(layerName) + "\"");
		probe.log("予測の計算は volatile で FMA 縮約を止めてある（止めないと別の値になる。"
				  "先頭コメント参照）。確かめ: 3531 の予測1 は -4.5474735088646412e-13 "
				  "[-0x1p-41] のはず。実際= " +
				  NumBoth(RoundTripDivide(3531)));
	}

	std::vector<Observation> observations;

	// 1 ケース＝1 オブジェクト。⓪ 曲線 → ① Create 直後 → ② ResetObject（バウンド無し）。
	auto runCase = [&](const std::string& label, double x0, double z0, double x1, double z1,
					   bool withProfile, bool noOffset, bool verbose, const std::string& why)
	{
		Observation obs;
		obs.label = label;
		obs.passedZ0 = z0;
		obs.passedZ1 = z1;
		obs.divPredictsResidual = RoundTripDivide(z0) != 0 || RoundTripDivide(z1) != 0;
		obs.reciprocalPredictsResidual =
			RoundTripReciprocal(z0) != 0 || RoundTripReciprocal(z1) != 0;

		if (verbose)
		{
			probe.log("--- " + label + " ---");
			probe.log("  ねらい: " + why);
			probe.log("  渡す世界座標: (" + Num(x0) + ", 0, " + NumBoth(z0) + ") → (" + Num(x1) +
					  ", 0, " + NumBoth(z1) + ") ／ 渡した Z の差=" + NumBoth(z1 - z0));
			probe.log("  予測1（割る→掛ける）: z0=" + NumBoth(RoundTripDivide(z0)) +
					  " z1=" + NumBoth(RoundTripDivide(z1)));
			probe.log("  予測2（逆数を掛ける＝SDK の inline）: z0=" +
					  NumBoth(RoundTripReciprocal(z0)) + " z1=" + NumBoth(RoundTripReciprocal(z1)));
		}

		MCObjectHandle curve =
			MakeStraightPath(probe, "  ", WorldPt3(x0, 0, z0), WorldPt3(x1, 0, z1), verbose);
		if (curve == nullptr)
			return;

		MCObjectHandle profile = withProfile ? MakeProfile() : nullptr;
		if (withProfile && profile == nullptr)
		{
			probe.log("  " + label + ": 断面のグループが作れなかった（このケースは測れない）");
			return;
		}

		MCObjectHandle pio =
			noOffset ? gSDK->CreateCustomObjectPathNoOffset("StructuralMember", curve, profile)
					 : gSDK->CreateCustomObjectPath("StructuralMember", curve, profile);
		if (pio == nullptr)
		{
			probe.log("  " + label + ": " + EntryName(noOffset) + " が nil を返した");
			return;
		}

		// 読み戻し 1 回ぶん。verbose なら全文、そうでなければ値だけ返す。
		auto readStage = [&](const std::string& stage, double& outDelta)
		{
			WorldPt3 p0, p1;
			if (!ReadPathEndpoints(pio, p0, p1))
			{
				probe.log("  " + label + ": " + stage + " パスの点が読めない");
				outDelta = std::nan("");
				return;
			}
			VWParametricObj obj(pio);
			const WorldPt3 pos = obj.GetObjectModelPos();
			outDelta = p1.z - p0.z;
			if (verbose)
			{
				const Sint32 pointCount = gSDK->NurbsGetNumPts(gSDK->GetCustomObjectPath(pio), 0);
				probe.log(
					"  " + stage + " 点数=" + std::to_string(static_cast<long long>(pointCount)) +
					" 挿入点Z=" + NumBoth(pos.z) + " 局所 z0=" + Num(p0.z) + " z1=" + Num(p1.z));
				probe.log("  " + stage + " **z1-z0=" + NumBoth(p1.z - p0.z) + "** 絶対Z: [0]=" +
						  NumBoth(pos.z + p0.z) + " [1]=" + NumBoth(pos.z + p1.z));
				probe.log("  " + stage + " 渡した値との差: 挿入点−z0=" + NumBoth(pos.z - z0) +
						  " 絶対[1]−z1=" + NumBoth((pos.z + p1.z) - z1) +
						  " (z1-z0) の誤差=" + NumBoth((p1.z - p0.z) - (z1 - z0)));
			}
		};

		double createdDelta = 0;
		readStage(std::string("① ") + EntryName(noOffset) + " 直後:", createdDelta);
		obs.createdError = createdDelta - (z1 - z0);

		gSDK->ResetObject(pio);
		readStage("② ResetObject 直後（バウンド無し＝潰れる経路）:", obs.afterResetDelta);
		obs.measured = true;

		// 判定。**「残差が出たか」と「予測が当たったか」の 2 段**で見る。
		const double residual = obs.afterResetDelta;
		if (residual != residual)
			obs.verdict = "パスが読めない";
		else if (residual == z1 - z0 && z1 != z0)
			obs.verdict = "潰れていない（渡した長さのまま）";
		else if (residual == 0)
			obs.verdict = obs.divPredictsResidual ? "残差なし（**予測1 は残差ありと言った**）"
												  : "残差なし（予測1 と一致）";
		else if (residual == RoundTripDivide(z1) || residual == -RoundTripDivide(z1))
			obs.verdict = "**残差あり＝往復誤差（上端 z1）とビット一致**";
		else if (residual == RoundTripDivide(z0) || residual == -RoundTripDivide(z0))
			obs.verdict = "**残差あり＝往復誤差（下端 z0）とビット一致**";
		else
			obs.verdict = "**残差あり。大きさはどの往復誤差とも一致しない**";

		if (verbose)
			probe.log("  → 判定: " + obs.verdict);
		else
			probe.log("  " + label + " | z0→z1=" + Num(z0) + "→" + Num(z1) +
					  " | 予測1=" + (obs.divPredictsResidual ? "あり" : "なし") +
					  " 予測2=" + (obs.reciprocalPredictsResidual ? "あり" : "なし") +
					  " | ①の誤差=" + Num(obs.createdError) +
					  " | **②の差=" + NumBoth(obs.afterResetDelta) + "** | " + obs.verdict);
		observations.push_back(obs);
	};

	// =======================================================================
	probe.log("=== T. 物差し（事故と同じ形を 1 本。572 → 3531・断面あり。ここだけ全文） ===");
	runCase("T 事故と同じ柱（572 → 3531）", 0, 572, 0, 3531, true, false, true,
			"事故で潰れた 46 本と同じ端点。1 回目はここで +4.5474735088646412e-13 が出た");

	// =======================================================================
	probe.log("=== A. z0=572 固定・z1 を振る（1 回目の再現。断面あり＝事故と同じ作り） ===");
	{
		const double values[] = {2429, 5905, 3531, 6374, 5500, 2500, 585, 573};
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
			runCase("A-" + Num(values[i]), 0, 572, 0, values[i], true, false, false, "");
	}

	// =======================================================================
	probe.log("=== B. 厳密に退化させて渡す（z0 = z1。プラグインの直し方そのもの） ===");
	{
		const double values[] = {3531, 6374, 13, 572};
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
			runCase("B-退化-" + Num(values[i]), 0, values[i], 0, values[i], true, false, false, "");
	}

	// =======================================================================
	probe.log("=== C/D. 断面なし・NoOffset（出どころが入口に依るか） ===");
	runCase("C-断面なし-3531", 0, 572, 0, 3531, false, false, false, "");
	runCase("C-断面なし-6374", 0, 572, 0, 6374, false, false, false, "");
	runCase("D-NoOffset-3531", 0, 572, 0, 3531, true, true, false, "");
	runCase("D-NoOffset-6374", 0, 572, 0, 6374, true, true, false, "");

	// =======================================================================
	probe.log("=== F. **下端（z0）側を振る**（1 回目は z0=572 に固定していた） ===");
	runCase("F-1 z0=3531→z1=6374", 0, 3531, 0, 6374, true, false, false, "");
	runCase("F-2 z0=3531→z1=2429", 0, 3531, 0, 2429, true, false, false, "");
	runCase("F-3 z0=13→z1=572", 0, 13, 0, 572, true, false, false, "");
	runCase("F-4 z0=13→z1=3531", 0, 13, 0, 3531, true, false, false, "");
	runCase("F-5 z0=8002→z1=8000", 0, 8002, 0, 8000, true, false, false, "");

	// =======================================================================
	probe.log("=== G. 予測を規則として試す（z0=572 固定。前半 9 つが「往復で戻らない」値） ===");
	{
		// 前半: v/25.4*25.4 != v（＝予測1 が「残差あり」と言う値）。桁を散らしてある。
		// 後半: v/25.4*25.4 == v（同「残差なし」）。**うち 5・50・900・6374 などは
		// 予測2（逆数を掛ける形）では「残差あり」になる**——2 つの形を分ける値。
		const double values[] = {1, 2, 13, 103, 813, 1626, 3531, 6503, 8002,
								 3, 5, 50, 300, 900, 2000, 4000, 6374, 8000};
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
			runCase("G-" + Num(values[i]), 0, 572, 0, values[i], true, false, false, "");
	}

	// =======================================================================
	probe.log("=== まとめ（この表がこのプローブの結論） ===");
	probe.log(
		"ラベル | 渡した z0→z1 | 予測1（割る→掛ける） | 予測2（逆数） | ①の誤差 | ②の差 | 判定");
	size_t divHit = 0, divMiss = 0, recHit = 0, recMiss = 0, createdDirty = 0, measured = 0;
	for (size_t i = 0; i < observations.size(); ++i)
	{
		const Observation& obs = observations[i];
		if (!obs.measured)
			continue;
		++measured;
		const bool residual =
			obs.afterResetDelta != 0 && obs.afterResetDelta == obs.afterResetDelta;
		if (obs.createdError != 0)
			++createdDirty;
		// 退化（z0 == z1）は「渡した時点で差が無い」ケースなので予測の採点から外す
		// ——どちらの予測も「端点の値が戻らない」と言うが、差が 0 なら残差の出ようがない。
		if (obs.passedZ0 != obs.passedZ1)
		{
			if (obs.divPredictsResidual == residual)
				++divHit;
			else
				++divMiss;
			if (obs.reciprocalPredictsResidual == residual)
				++recHit;
			else
				++recMiss;
		}
		probe.log(obs.label + " | " + Num(obs.passedZ0) + "→" + Num(obs.passedZ1) + " | " +
				  (obs.divPredictsResidual ? "あり" : "なし") + " | " +
				  (obs.reciprocalPredictsResidual ? "あり" : "なし") + " | " +
				  Num(obs.createdError) + " | " + Num(obs.afterResetDelta) + " | " + obs.verdict);
	}
	probe.log("=== 予測の成績（退化ケースを除く。ここが規則かどうかの判定） ===");
	probe.log("  予測1（v / 25.4 * 25.4 != v）: 一致 " + std::to_string(divHit) + " / 不一致 " +
			  std::to_string(divMiss));
	probe.log("  予測2（v * (1/25.4) * 25.4 != v ＝ SDK の inline）: 一致 " +
			  std::to_string(recHit) + " / 不一致 " + std::to_string(recMiss));
	probe.log("  ① で渡した差からずれたケース: " + std::to_string(createdDirty) + " / " +
			  std::to_string(measured) +
			  "（0 なら「生成の段では変換されない」が全件で裏付けられた）");
}
