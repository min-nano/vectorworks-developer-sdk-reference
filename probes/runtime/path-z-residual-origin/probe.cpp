//
//	probes/runtime/path-z-residual-origin/probe.cpp
//
//	[issue #67] **`(0, 1e-7)` の帯に落ちた残差は、どこで生まれたのか。**
//	呼び出し側が**厳密な整数 mm**を渡していても残差が出るのか、出るならそれは
//	**どの段**（曲線を作る／`CreateCustomObjectPath`／その後の作り直し）で生まれ、
//	**値そのもの**（mm ↔ インチの往復で戻らない値）で決まるのか。
//
//	## 前提（ここまでに確定していること）
//
//	* `ResetObject` が作り直すのは「長さ 0」か「長さ 1e-7 以上」のパスだけで、
//	  その間の帯は温存される（#61。`Findings/Parametric Objects.md`）。**閾値の話は
//	  もう片付いている。**
//	* 事故（柱 197 本中 46 本が長さ 0）の個体は、生成直後に **`4.54747e-13`**
//	  ＝ `2^-41` ＝ 3531 付近の double の 1 ULP を持っていた。
//	* 現在の `Findings` は原因を「**両端の世界座標Zを別々に計算しており**、片方に丸めが
//	  残った」＝**呼び出し側の算術**に帰属させている。
//
//	## この帰属に合わない実測（issue #67。プラグイン側で全数）
//
//	渡していた 2 つの world Z は**ほぼ全数が厳密な整数 mm**で、事故と同じ本数の
//	フィクスチャでは**非整数が 1 本も無い**。「別々の式で計算したせいで片方に丸めが
//	残った」では 197 本が 0 本と 46 本に分かれたことを説明できない。
//
//	一方、`4.54747e-13` は **mm をインチへ直して戻す往復**の誤差とビット単位で一致する:
//
//	    3531 / 25.4 * 25.4 - 3531 == -4.547473508864641e-13   ← 事故の残差と同じ
//	    572 / 25.4 * 25.4 - 572   == 0                        ← 潰れた柱の下端
//	    2429 / 6374 / 5905        == 0                        ← 無事な柱の端点
//
//	ただし整数 mm の 12.6% が往復で戻らないので、**5 点の一致は偶然でも約 7% 起きる**。
//	断定できる水準ではない——だから実機で振る。
//
//	## ヘッダ側で分かっていること（`sdk-grep`。実機確認とは独立）
//
//	    Kernel/Math/MathCoordTypes.h:
//	      const extern double kWorldCoordsPerInch;   // 25.4
//	      const extern double kWorldCoordsPerMM;     // 1
//	      typedef double WorldCoord;
//	      inline WorldCoord InchesToWorldCoord(WorldInches inches) { return inches * kWorldCoordsPerInch; }
//	      inline double     WorldCoordToInches(WorldCoord coord)   { return coord * kInchesPerWorldCoord; }
//	    Kernel/Math/MathCoordTypes.cpp:
//	      kWorldCoordsPerInch  = 25.4      kWorldCoordsPerMM = 1
//	      kInchesPerWorldCoord = 1 / kWorldCoordsPerInch    ← **逆数を先に作って掛ける**
//
//	つまり **`WorldCoord` の単位は常に mm**（図面の単位に依らない）。そして SDK 自身の
//	往復は `v * (1/25.4) * 25.4` であって `v / 25.4 * 25.4` ではない。**この 2 つは同じ
//	値にならない**——6374 と 5500 は前者では残差が出て、後者では出ない。だから
//	**どちらの形の往復なのかまで、この 1 本で見分けられる**（下記 A 群の値の選び方）。
//
//	## 何を測るか（1 ケース 1 オブジェクト・4 地点）
//
//	    ⓪  `CreateNurbsCurve` ＋ `Add3DVertex` した曲線をそのまま読み戻す
//	    ⓪' `NurbsSetPt3D` で座標を入れ直してから読み戻す（プラグインと同じ作法）
//	    ①  `CreateCustomObjectPath` の直後（局所パス＋挿入点＝絶対Zへ戻して比べる）
//	    ②  `ResetObject` の直後（**バウンドを 1 本も書かない**＝潰れる経路。#56 の 4）
//
//	事故のプラグインが「生成直後に 197 本すべて 0 長」を見たのは、`CreateCustomObjectPath`
//	の直後ではなく **`SetPluginObjectStyle` を挟んだ後**だった（issue #67 の自己申告）。
//	スタイルは図面に無いかもしれないので、ここでは**同じ「作り直し（regen）」を起こす
//	`ResetObject`** で代用する。潰れる経路であることは #56 の 4 で確定している。
//
//	**各ケースで予測を 2 つ計算して並べる**ので、ログはそのまま突き合わせとして読める:
//
//	    予測A（SDK の形）: InchesToWorldCoord(WorldCoordToInches(v)) - v   ＝ v*(1/25.4)*25.4 - v
//	    予測B（素朴な形）: v / 25.4 * 25.4 - v
//
//	## 値の選び方（A 群）——**予測 A と予測 B を割るように選んである**
//
//	    値      予測A（SDK）   予測B（素朴）   ねらい
//	    572     0              0              事故で潰れた柱の下端（対照）
//	    2429    0              0              事故で無事だった柱の上端（対照）
//	    5905    0              0              同上
//	    3531    ≠0             ≠0             **事故で潰れた柱の上端そのもの**
//	    13      ≠0             ≠0             小さい値でも出るか
//	    1       ≠0             ≠0             同上（残差は 1e-16 台。1e-7 の帯より下）
//	    6374    ≠0             0              **予測 A と B を分ける値**（無事だった柱の上端）
//	    5500    ≠0             0              同上
//
//	予測 B だけが当たるなら、事故の 5 点の一致は本物で、往復は素朴な形。
//	予測 A だけが当たるなら、6374 の柱が無事だった説明が別に要る。
//	**どちらも当たらない（残差が 1 つも出ない）なら、残差は単位変換では生まれていない**
//	——そのときは「呼び出し側が厳密なら安全」であって、現在の帰属のほうが正しい。
//
//	## 群
//
//	    T   事故と同じ形（572 → 3531・断面あり）を 1 本だけ通す物差し
//	    A   z0 = 572 固定・z1 を上の 8 値で振る（断面あり。事故と同じ作り）
//	    B   **厳密に退化**（z0 = z1 = v）——プラグインの直し方そのもの。
//	        **ここで残差が出たら「同じ変数を渡す」でも避けられない**ことになる
//	    C   断面を渡さない（#61 のプローブと同じ形。断面の有無で ① が変わるか）
//	    D   `CreateCustomObjectPathNoOffset`（`ISDK.h`。挿入点への変換を飛ばす入口）
//	    E   同じ値を X に入れる（残差が Z 特有か、座標一般か）
//
//	【読み方】最後の「まとめ」の表が結論。各行の
//	  * **残差が 0**            … その値では残差が生まれていない
//	  * **残差 = 予測A / 予測B** … 単位の往復で説明が付く（どちらの形かも分かる）
//	  * **そのどちらでもない**   … 別の出どころ。値をそのまま持ち帰って考える
//	どの地点（⓪/⓪'/①/②）で初めて 0 でなくなったかが、**残差が生まれた段**である。
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

	// 予測A: **SDK 自身の往復**（MathCoordTypes.h の inline をそのまま呼ぶ。
	// 逆数を掛ける形なので、素朴な割り算とは結果が違う値がある）。
	double RoundTripSdk(double value)
	{
		return InchesToWorldCoord(WorldCoordToInches(value)) - value;
	}

	// 予測B: 素朴な往復（issue #67 が事故の 5 点と突き合わせた形）。
	double RoundTripNaive(double value)
	{
		return value / 25.4 * 25.4 - value;
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
		double predictSdk = 0;		// 予測A（z1 について）
		double predictNaive = 0;	// 予測B（z1 について）
		double curveDelta = 0;		// ⓪' 曲線の z1−z0
		double createdDelta = 0;	// ① Create 直後の局所 z1−z0
		double afterResetDelta = 0; // ② ResetObject 直後の局所 z1−z0
		std::string verdict;
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

	// 曲線を作る。**プラグインと同じ作法**（Add3DVertex で足してから NurbsSetPt3D で
	// 入れ直す）にして、⓪ と ⓪' の両方を出す——足した点がずれるのか、入れ直しても
	// 残るのかを分けるため。
	MCObjectHandle MakeStraightPath(vwprobe::Report& probe, const std::string& prefix,
									WorldPt3 from, WorldPt3 to)
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
			probe.log(prefix + " ⓪ 曲線（Add3DVertex 直後）: z0=" + NumBoth(p0.z) +
					  " z1=" + NumBoth(p1.z) + " z1-z0=" + NumBoth(p1.z - p0.z));
		else
			probe.log(prefix + " ⓪ 曲線の点が読めない（点数=" +
					  std::to_string(static_cast<long long>(gSDK->NurbsGetNumPts(curve, 0))) +
					  "）");

		// **座標を明示的に入れ直す**（事故のプラグインと同じ作法。Add3DVertex が足した点が
		// 渡した位置にならないことがある）。点が 2 つになっていなければ触らない。
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, from);
			gSDK->NurbsSetPt3D(curve, 0, 1, to);
		}
		return curve;
	}

	const char* EntryName(bool noOffset)
	{
		return noOffset ? "CreateCustomObjectPathNoOffset" : "CreateCustomObjectPath";
	}
} // namespace

VW_PROBE("path-z-residual-origin", "厳密な整数 mm の世界座標に残差が生まれるかを測る",
		 "CreateCustomObjectPath へ厳密な整数 mm の world Z を渡し、曲線・生成直後・"
		 "ResetObject 直後の 3 地点で読み戻して、mm↔インチの往復で説明が付く残差が"
		 "生まれるかを確かめる。新規の空図面で走る")
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
	}

	probe.log("=== 予測の表（この実行の double で計算した値。ここは実機に依らない） ===");
	{
		const double values[] = {572, 2429, 5905, 2500, 3531, 13, 1, 6374, 5500};
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
		{
			const double v = values[i];
			probe.log("  v=" + Num(v) +
					  " 予測A（SDK: v*(1/25.4)*25.4−v）=" + NumBoth(RoundTripSdk(v)) +
					  " ／ 予測B（素朴: v/25.4*25.4−v）=" + NumBoth(RoundTripNaive(v)));
		}
		probe.log("  ※ 予測A と 予測B が食い違う値（6374・5500）が、2 つの形を分ける。");
	}

	std::vector<Observation> observations;

	// 1 ケース＝1 オブジェクト。⓪ 曲線 → ① Create 直後 → ② ResetObject（バウンド無し）。
	auto runCase = [&](const std::string& label, double x0, double z0, double x1, double z1,
					   bool withProfile, bool noOffset, const std::string& why)
	{
		probe.log("--- " + label + " ---");
		probe.log("  ねらい: " + why);
		probe.log("  渡す世界座標: (" + Num(x0) + ", 0, " + NumBoth(z0) + ") → (" + Num(x1) +
				  ", 0, " + NumBoth(z1) + ") ／ 渡した Z の差=" + NumBoth(z1 - z0));
		probe.log("  予測（z1 について）: A=" + NumBoth(RoundTripSdk(z1)) +
				  " B=" + NumBoth(RoundTripNaive(z1)) + " ／（z0 について）: A=" +
				  NumBoth(RoundTripSdk(z0)) + " B=" + NumBoth(RoundTripNaive(z0)));

		Observation obs;
		obs.label = label;
		obs.passedZ0 = z0;
		obs.passedZ1 = z1;
		obs.predictSdk = RoundTripSdk(z1);
		obs.predictNaive = RoundTripNaive(z1);

		MCObjectHandle curve =
			MakeStraightPath(probe, "  ", WorldPt3(x0, 0, z0), WorldPt3(x1, 0, z1));
		if (curve == nullptr)
			return;
		{
			WorldPt3 p0, p1;
			if (ReadCurveEndpoints(curve, p0, p1))
			{
				obs.curveDelta = p1.z - p0.z;
				probe.log("  ⓪' 曲線（NurbsSetPt3D 後）: z0=" + NumBoth(p0.z) +
						  " z1=" + NumBoth(p1.z) + " z1-z0=" + NumBoth(p1.z - p0.z) +
						  " ／ 渡した値との差: z0=" + NumBoth(p0.z - z0) +
						  " z1=" + NumBoth(p1.z - z1));
			}
			else
			{
				probe.log("  ⓪' 曲線の点が読めない");
				return;
			}
		}

		MCObjectHandle profile = withProfile ? MakeProfile() : nullptr;
		if (withProfile && profile == nullptr)
		{
			probe.log("  断面のグループが作れなかった（このケースは測れない）");
			return;
		}

		MCObjectHandle pio =
			noOffset ? gSDK->CreateCustomObjectPathNoOffset("StructuralMember", curve, profile)
					 : gSDK->CreateCustomObjectPath("StructuralMember", curve, profile);
		if (pio == nullptr)
		{
			probe.log(std::string("  ") + EntryName(noOffset) + " が nil を返した");
			return;
		}

		auto logStage = [&](const std::string& stage, double& outDelta)
		{
			MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
			if (path == nullptr)
			{
				probe.log("  " + stage + " GetCustomObjectPath が nil を返した");
				outDelta = std::nan("");
				return;
			}
			const Sint32 pointCount = gSDK->NurbsGetNumPts(path, 0);
			WorldPt3 p0, p1;
			if (!ReadPathEndpoints(pio, p0, p1))
			{
				probe.log("  " + stage + " パスの点が読めない（点数=" +
						  std::to_string(static_cast<long long>(pointCount)) + "）");
				outDelta = std::nan("");
				return;
			}
			VWParametricObj obj(pio);
			const WorldPt3 pos = obj.GetObjectModelPos();
			const double absZ0 = pos.z + p0.z;
			const double absZ1 = pos.z + p1.z;
			outDelta = p1.z - p0.z;
			probe.log("  " + stage + " 点数=" + std::to_string(static_cast<long long>(pointCount)) +
					  " 挿入点Z=" + NumBoth(pos.z) + " 局所 z0=" + Num(p0.z) + " z1=" + Num(p1.z));
			probe.log("  " + stage + " **z1-z0=" + NumBoth(p1.z - p0.z) +
					  "** 絶対Z: [0]=" + NumBoth(absZ0) + " [1]=" + NumBoth(absZ1));
			probe.log("  " + stage + " 渡した値との差: 挿入点−z0=" + NumBoth(pos.z - z0) +
					  " 絶対[0]−z0=" + NumBoth(absZ0 - z0) + " 絶対[1]−z1=" + NumBoth(absZ1 - z1) +
					  " (z1-z0) の誤差=" + NumBoth((p1.z - p0.z) - (z1 - z0)));
		};

		logStage(std::string("① ") + EntryName(noOffset) + " 直後:", obs.createdDelta);

		probe.log("  （バウンドは 1 本も書かない＝#56 の 4 の「潰れる」経路）");
		gSDK->ResetObject(pio);
		logStage("② ResetObject 直後:", obs.afterResetDelta);

		// 判定。**「残差が出たか」と「それは予測のどちらと一致するか」の 2 段**で見る。
		// 比較はビット一致（この調査の主語がビットだから）。
		const double residual = obs.afterResetDelta;
		const double expectedDegenerate = z1 - z0;
		if (residual != residual)
			obs.verdict = "パスが読めない";
		else if (residual == expectedDegenerate && expectedDegenerate != 0)
			obs.verdict = "潰れていない（渡した長さのまま）";
		else if (residual == 0)
			obs.verdict = "**残差なし（厳密に 0）**";
		else if (residual == obs.predictSdk || residual == -obs.predictSdk)
			obs.verdict = "**残差あり＝予測A（SDK の往復）とビット一致**";
		else if (residual == obs.predictNaive || residual == -obs.predictNaive)
			obs.verdict = "**残差あり＝予測B（素朴な往復）とビット一致**";
		else if (residual == RoundTripSdk(z0) || residual == -RoundTripSdk(z0) ||
				 residual == RoundTripNaive(z0) || residual == -RoundTripNaive(z0))
			obs.verdict = "**残差あり＝z0 側の往復と一致**（効いているのは始点の値）";
		else
			obs.verdict = "**残差あり。どの予測とも一致しない**";
		probe.log("  → 判定: " + obs.verdict);
		observations.push_back(obs);
	};

	// =======================================================================
	probe.log("=== T. 物差し（事故と同じ形を 1 本。572 → 3531・断面あり） ===");
	runCase("T 事故と同じ柱（572 → 3531）", 0, 572, 0, 3531, true, false,
			"事故で潰れた 46 本と同じ端点。ここで残差が出れば、それが事故の残差そのもの");

	// =======================================================================
	probe.log("=== A. z0=572 固定・z1 を振る（断面あり＝事故と同じ作り） ===");
	{
		struct Step
		{
			const char* label;
			double z1;
			const char* why;
		};
		const Step steps[] = {
			{"A-1 z1 = 2429", 2429, "予測A・B とも 0（事故で無事だった柱の上端）"},
			{"A-2 z1 = 5905", 5905, "予測A・B とも 0（対照）"},
			{"A-3 z1 = 3531", 3531, "**予測A・B とも ≠0。事故で潰れた柱の上端そのもの**"},
			{"A-4 z1 = 6374", 6374, "**予測A は ≠0・予測B は 0。2 つの形を分ける値**"},
			{"A-5 z1 = 5500", 5500, "同上（もう 1 点）"},
			{"A-6 z1 = 2500", 2500, "予測A・B とも 0（#61 のプローブが使った基準Z）"},
			{"A-7 z1 = 585", 585, "予測A・B とも ≠0。**差が 13**——短い材でも起きるか"},
			{"A-8 z1 = 573", 573, "予測A・B とも ≠0。**差が 1**——最小の材"},
		};
		for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
			runCase(steps[i].label, 0, 572, 0, steps[i].z1, true, false, steps[i].why);
	}

	// =======================================================================
	probe.log("=== B. 厳密に退化させて渡す（z0 = z1。プラグインの直し方そのもの） ===");
	{
		const double values[] = {3531, 6374, 13, 572};
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
		{
			const double v = values[i];
			runCase("B 退化 z0 = z1 = " + Num(v), 0, v, 0, v, true, false,
					"**両端に同じ値**。ここで残差が出たら「同じ変数を渡す」でも避けられない"
					"——直し方そのものが崩れる");
		}
	}

	// =======================================================================
	probe.log("=== C. 断面を渡さない（#61 のプローブと同じ形。断面の有無で ① が変わるか） ===");
	{
		const double values[] = {3531, 6374};
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
			runCase("C 断面なし 572 → " + Num(values[i]), 0, 572, 0, values[i], false, false,
					"#61 のプローブは断面を渡さずに測って「① は渡した差と一致」と出した。"
					"断面（＝実体が出る作り）で結果が変わるかを見る");
	}

	// =======================================================================
	probe.log("=== D. CreateCustomObjectPathNoOffset（挿入点への変換を飛ばす入口） ===");
	{
		const double values[] = {3531, 6374};
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
			runCase("D NoOffset 572 → " + Num(values[i]), 0, 572, 0, values[i], true, true,
					"変換がどの段で入るかを絞る。挿入点への変換を飛ばしても残差が出るなら、"
					"出どころは挿入点の計算ではない");
	}

	// =======================================================================
	probe.log("=== E. 同じ値を X に入れる（残差は Z 特有か、座標一般か） ===");
	runCase("E-1 X に 3531（Z は 572 で固定）", 0, 572, 3531, 572, true, false,
			"往復で戻らない値を X に置く。Z の差は厳密に 0 なので、**長さが 0 でなければ "
			"X 側にも同じことが起きている**");
	runCase("E-2 X に 6374（Z は 572 で固定）", 0, 572, 6374, 572, true, false,
			"同上（予測A と B を分ける値で）");

	// =======================================================================
	probe.log("=== まとめ（この表がこのプローブの結論） ===");
	probe.log("ラベル | 渡した z0→z1 | 予測A(z1) | 予測B(z1) | ⓪'曲線の差 | ①の差 | ②の差 | 判定");
	for (size_t i = 0; i < observations.size(); ++i)
	{
		const Observation& obs = observations[i];
		probe.log(obs.label + " | " + Num(obs.passedZ0) + "→" + Num(obs.passedZ1) + " | " +
				  Num(obs.predictSdk) + " | " + Num(obs.predictNaive) + " | " +
				  Num(obs.curveDelta) + " | " + Num(obs.createdDelta) + " | " +
				  Num(obs.afterResetDelta) + " | " + obs.verdict);
	}
	probe.log("※ 「どの地点で初めて 0 でなくなったか」が残差の生まれた段である"
			  "（⓪' なら曲線、① なら生成、② なら作り直し）。");
}
