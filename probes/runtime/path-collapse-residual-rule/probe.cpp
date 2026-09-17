//
//	probes/runtime/path-collapse-residual-rule/probe.cpp
//
//	[issue #71] **0 長へ潰す作り直しは、どの長さ L で 1〜2 ULP を残すのか。**
//
//	#67（PR #69）で分かっているのはここまで:
//
//	  * `CreateCustomObjectPath` は渡した世界座標を**ビット一致で保つ**（65 ケース全件）。
//	  * **残差は作り直し（regen）で生まれる。** バウンドを 1 本も持たないまま
//	    `ResetObject` で 0 長へ潰すと、**L によっては厳密な 0 にならず L の 1〜2 ULP が
//	    残る**（符号は L と同じ）。決めているのは L だけで、端点の絶対Zに依らない。
//	  * **決定的**（順番・レイヤ・実行に依らずビット一致）。
//	  * mm ↔ インチの往復では**予測できない**。
//
//	残っているのは規則そのもの。#67 の 26 点を手元で整理すると、**規則の形はかなり
//	絞れている**——ただしどれも 26 点からの読みなので、**この走査で確かめる仮説**である。
//
//	  (あ) 残差は必ず **k × ULP(L)**（k = 0 / 1 / 2）で、**符号は L と同じ**。
//	  (い) k ≠ 0 の 5 点は、**すべて「相対 2^-52 を 1 段だけ失った」形**で説明が付く
//	       （仮数 m < 1.5 なら 1 ULP、m > 1.5 なら 2 ULP。2959 → 1 / 3428 → 2 /
//	        3518 → 2 / 7430 → 2 / −569 → 1 のすべてがこれ）。**1 段より深い欠けは無い。**
//	  (う) **決めているのは仮数だけ**らしい。26 点の中に**仮数が同じで指数だけ違う**組が
//	       3 つ（1857 と 7428、2000 と 4000、241 と 1928）あり、**3 組とも一致**した。
//
//	## この走査で決めること
//
//	(あ)(い)(う) を潰したうえで、**「どの L か」を輪郭から当てる**。
//
//	  * **B 群（2 の冪倍）で (う) を正面から試す。** 同じ仮数を指数だけ変えて 13 本ずつ
//	    通す。**(う) が正しければ 1 本の系列は全部同じ k になる**。これが通れば
//	    「危ない値」の話は**仮数だけの話**に縮み、走査は 1 binade で足りる。
//	  * **C / D 群（整数 mm の連続走査）で「飛び飛びか、まとまるか」を見る。**
//	    2900〜3060 と 3420〜3530 を 1 mm 刻みで埋める（既知の 2959 / 3428 / 3518 を含む）。
//	  * **E 群（1 ULP 刻み）で規則の細かさを測る。** 隣り合う double で k が変わるなら
//	    「丸めが数回起きる計算」、長い区間で変わらないなら別の形。**これが当てはめの
//	    当たりを一番強く絞る。**
//	  * **F 群（小数の L）で整数 mm の外を見る。** #67 の実測は整数 mm だけだった。
//	  * **G 群（符号・成分）で「Z 特有か」を見る。** 負の L と、**X 方向だけの水平パス**・
//	    斜めのパスを通す。x に同じ残差が出るなら、これは Z の話ではなく**座標一般の話**。
//
//	## 測り方（1 ケース 1 オブジェクト・読むのは 1 地点だけ）
//
//	    (0,0,0) → (dx,0,dz) のパスで PIO を作る
//	      → **バウンドを 1 本も書かずに** ResetObject（＝0 長へ潰れる経路）
//	      → 局所パスの両端を読み、**残った差**を出す
//
//	断面（プロファイル）は渡さない——#67 で**断面あり／なし／NoOffset のどれでも
//	同じ残差**だと分かっているため。A 群の 2 本だけ断面ありで通し、それを確かめる。
//
//	## 読み方
//
//	各群は **1 文字 1 ケースの地図**で出す。文字は `残差 ÷ ULP(L)`（符号は L に揃える）で、
//	`0` が「厳密に 0（安全）」、`1` `2` が「その ULP だけ残った（危険）」。
//	**`?` が出たら (あ) が崩れている**——その明細は下の「非 0 の明細」に全文で出る。
//	明細には仮数 m も添える（(い) の突き合わせに使う。**予測式はプローブの中で計算しない**
//	——`Findings/Investigation Techniques.md`「1 ULP を追う調査では…」）。
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

	std::string NumBoth(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%a", value);
		return Num(value) + " [" + std::string(buffer) + "]";
	}

	// ULP（その値のところでの double の刻み）。**演算ではなく指数の取り出しだけ**なので
	// 丸めも FMA 縮約も入らない。
	double UlpOf(double value)
	{
		const double magnitude = std::fabs(value);
		if (!(magnitude > 0))
			return 0;
		return std::ldexp(1.0, std::ilogb(magnitude) - 52);
	}

	// 仮数（[1, 2) に正規化した値）。frexp は厳密（誤差を持ち込まない）。
	double SignificandOf(double value)
	{
		int exponent = 0;
		return std::fabs(std::frexp(value, &exponent)) * 2.0;
	}

	int ExponentOf(double value)
	{
		const double magnitude = std::fabs(value);
		if (!(magnitude > 0))
			return 0;
		return std::ilogb(magnitude);
	}

	// 1 ケースぶんの観測。
	struct Observation
	{
		std::string group;
		double dx = 0;		  // 渡した X の差
		double dz = 0;		  // 渡した Z の差
		double residualX = 0; // 潰した後に残った X の差
		double residualZ = 0; // 同 Z
		double ratio = 0; // 残差 ÷ ULP（符号は渡した値に揃える）。地図の文字はこれ
		bool measured = false; // 読み戻せたか
	};

	std::vector<Observation> gNonZero; // 非 0 だったケース（明細に出す）
	std::vector<Observation> gOdd; // 比が整数でなかったケース（(あ) が崩れた証拠）
	int gFailures = 0;

	// 断面（矩形 120 × 120 のグループ）。A 群の対照でだけ使う。
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

	bool ReadPathEndpoints(MCObjectHandle pio, WorldPt3& outP0, WorldPt3& outP1)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
			return false;
		const Boolean got0 = gSDK->NurbsGetPt3D(path, 0, 0, outP0);
		const Boolean got1 = gSDK->NurbsGetPt3D(path, 0, 1, outP1);
		return got0 && got1;
	}

	// 1 ケース。**(0,0,0) → (dx, 0, dz)** のパスで PIO を作り、バウンドを書かずに
	// ResetObject して、残った差を読む。返すのは地図に出す 1 文字。
	char RunCase(const std::string& group, double dx, double dz, bool withProfile)
	{
		Observation obs;
		obs.group = group;
		obs.dx = dx;
		obs.dz = dz;

		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(0, 0, 0), false, 1);
		if (curve == nullptr)
		{
			++gFailures;
			return 'x';
		}
		gSDK->Add3DVertex(curve, WorldPt3(dx, 0, dz), true);
		// **座標を明示的に入れ直す**（#67 のプローブと同じ作法。Add3DVertex が足した点が
		// 渡した位置にならないことがある）。
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, WorldPt3(0, 0, 0));
			gSDK->NurbsSetPt3D(curve, 0, 1, WorldPt3(dx, 0, dz));
		}

		MCObjectHandle profile = withProfile ? MakeProfile() : nullptr;
		MCObjectHandle pio = gSDK->CreateCustomObjectPath("StructuralMember", curve, profile);
		if (pio == nullptr)
		{
			++gFailures;
			return 'x';
		}

		gSDK->ResetObject(pio);

		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(pio, p0, p1))
		{
			++gFailures;
			return 'x';
		}
		obs.measured = true;
		obs.residualX = p1.x - p0.x;
		obs.residualZ = p1.z - p0.z;

		// 比を取る基準は「渡した差のうち 0 でないほう」。両方 0 でないケース（斜め）は
		// Z を基準にし、X の残差は明細に出す。
		const double reference = (dz != 0) ? dz : dx;
		const double residual = (dz != 0) ? obs.residualZ : obs.residualX;
		const double unit = UlpOf(reference);
		if (unit > 0)
			obs.ratio = (reference > 0) ? residual / unit : -residual / unit;

		// 地図の文字は**基準にした成分**の比。斜めのケースで X 側だけに残差が出た場合は
		// 文字が `0` のままになるので、明細に載せる条件は「どちらかの成分が非 0」にする。
		char mark = '?';
		if (residual == 0)
			mark = '0';
		else if (obs.ratio >= 0 && obs.ratio <= 9 && obs.ratio == std::floor(obs.ratio))
			mark = static_cast<char>('0' + static_cast<int>(obs.ratio));

		if (mark == '?')
			gOdd.push_back(obs);
		if (obs.residualZ != 0 || obs.residualX != 0)
			gNonZero.push_back(obs);
		return mark;
	}

	// 群を 1 つ走らせて、地図として出す。`values` の並びがそのまま文字の並びになる。
	void RunMap(vwprobe::Report& probe, const std::string& group, const std::string& how,
				const std::vector<double>& values)
	{
		probe.log("--- " + group + " " + how + "（" +
				  std::to_string(static_cast<long long>(values.size())) + " ケース） ---");
		std::string row;
		size_t rowStart = 0;
		for (size_t i = 0; i < values.size(); ++i)
		{
			row += RunCase(group, 0, values[i], false);
			if (row.size() == 50 || i + 1 == values.size())
			{
				probe.log("  " + group + " [" + std::to_string(static_cast<long long>(rowStart)) +
						  "..] L=" + Num(values[rowStart]) + " から: " + row);
				row.clear();
				rowStart = i + 1;
			}
		}
	}

	std::vector<double> Range(double from, double to, double step)
	{
		std::vector<double> values;
		for (double value = from; value <= to; value += step)
			values.push_back(value);
		return values;
	}
} // namespace

VW_PROBE("path-collapse-residual-rule", "0 長へ潰す作り直しが残す 1〜2 ULP の規則を走査する",
		 "長さ L を 400 通りほど振って 0 長へ潰し、残る残差（L の 0 / 1 / 2 ULP）を地図にする。"
		 "2 の冪倍・1 mm 刻み・1 ULP 刻み・小数・符号と成分の 6 群。新規の空図面で走る")
{
	probe.log("=== この図面について ===");
	{
		double unitsPerInch = 0;
		const bool gotUnits = gSDK->GetProgramVariable(varUnit1UnitsPerInch, &unitsPerInch);
		probe.log(std::string("varUnit1UnitsPerInch = ") +
				  (gotUnits ? Num(unitsPerInch) : std::string("(読めない)")) +
				  "（WorldCoord 自体は図面の単位に依らず mm）");
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		TXString layerName;
		if (currentLayer != nullptr)
			gSDK->GetObjectName(currentLayer, layerName);
		probe.log(std::string("いまのレイヤ: \"") + static_cast<const char*>(layerName) + "\"");
		probe.log("地図の文字 = 残差 ÷ ULP(L)（符号は L に揃える）。0=厳密に 0（安全）／"
				  "1・2=その ULP だけ残った（危険）／?=(あ) が崩れた／x=測れなかった");
	}

	// =======================================================================
	// A. 物差し。#67 が残差を見た 5 値をそのまま通す。**ここが再現しなければ、
	//    以下の地図は読んではいけない**（測り方が #67 と違うということなので）。
	probe.log("=== A. 物差し（#67 で残差が出た 5 値。期待は 1 / 2 / 2 / 2 / 1） ===");
	{
		const double values[] = {2959, 3428, 3518, 7430, -569};
		std::string row;
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
			row += RunCase("A", 0, values[i], false);
		probe.log("  A 2959 / 3428 / 3518 / 7430 / -569 → " + row);
		// 断面ありでも同じか（#67 は「入口に依らない」と言っている）。
		std::string withProfile;
		withProfile += RunCase("A断面", 0, 2959, true);
		withProfile += RunCase("A断面", 0, 3000, true);
		probe.log("  A 断面あり 2959 / 3000 → " + withProfile + "（断面なしと同じなら 10）");
	}

	// =======================================================================
	// B. **2 の冪倍**（仮説 (う)）。同じ仮数・違う指数。**系列の中で文字が変われば
	//    「仮数だけでは決まらない」**ということで、そこが最大の分かれ目になる。
	probe.log(
		"=== B. 2 の冪倍（同じ仮数・違う指数。系列内で文字が変わらなければ (う) が立つ） ===");
	{
		const double seeds[] = {2959, 3428, 3000, 1857};
		for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); ++s)
		{
			std::string row;
			for (int power = -6; power <= 6; ++power)
				row += RunCase("B", 0, std::ldexp(seeds[s], power), false);
			probe.log("  B 種 " + Num(seeds[s]) + " × 2^(-6..+6) → " + row);
		}
	}

	// =======================================================================
	// C / D. **整数 mm の連続走査**（飛び飛びか、まとまるか）。
	probe.log("=== C/D. 整数 mm の連続走査 ===");
	RunMap(probe, "C", "2900〜3060 を 1 mm 刻み（2959 と 3000 を含む）", Range(2900, 3060, 1));
	RunMap(probe, "D", "3420〜3530 を 1 mm 刻み（3428 と 3518 を含む）", Range(3420, 3530, 1));

	// =======================================================================
	// E. **1 ULP 刻み**。隣り合う double で文字が変わるかどうかが、当てはめを一番強く絞る。
	probe.log("=== E. 1 ULP 刻み（隣り合う double。規則の細かさを測る） ===");
	{
		const double centers[] = {2959, 3000, 3428};
		for (size_t c = 0; c < sizeof(centers) / sizeof(centers[0]); ++c)
		{
			const double unit = UlpOf(centers[c]);
			std::vector<double> values;
			for (int step = -12; step <= 12; ++step)
				values.push_back(centers[c] + static_cast<double>(step) * unit);
			std::string row;
			for (size_t i = 0; i < values.size(); ++i)
				row += RunCase("E", 0, values[i], false);
			probe.log("  E " + Num(centers[c]) + " + (-12..+12) ULP → " + row +
					  "（真ん中の 13 文字目が " + Num(centers[c]) + " そのもの）");
		}
	}

	// =======================================================================
	// F. **小数の L**（#67 の実測は整数 mm だけだった）。
	probe.log("=== F. 小数の L（1/32 mm 刻み） ===");
	RunMap(probe, "F1", "2959 から 1/32 mm 刻みで 48 歩", Range(2959, 2959 + 47.0 / 32, 1.0 / 32));
	RunMap(probe, "F2", "3000 から 1/32 mm 刻みで 48 歩", Range(3000, 3000 + 47.0 / 32, 1.0 / 32));

	// =======================================================================
	// G. **符号と成分**。負の L と、**Z を使わないパス**（水平・斜め）。
	probe.log("=== G. 符号と成分（Z 特有の話なのか、座標一般の話なのか） ===");
	{
		std::string signs;
		const double values[] = {-2959, -3428, 569, -3000};
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
			signs += RunCase("G符号", 0, values[i], false);
		probe.log("  G -2959 / -3428 / 569 / -3000 → " + signs +
				  "（正の 2959 / 3428 / -569 / 3000 と同じなら 1203）");

		// X 方向だけのパス（dz = 0）。**同じ文字が出るなら Z の話ではない。**
		std::string horizontal;
		const double xs[] = {2959, 3428, 3000};
		for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i)
			horizontal += RunCase("G水平", xs[i], 0, false);
		probe.log("  G 水平（dx のみ）2959 / 3428 / 3000 → " + horizontal + "（Z と同じなら 120）");

		// 斜め（dx = dz）。長さは dz の √2 倍。**長さで決まるのか成分ごとなのか。**
		std::string diagonal;
		for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i)
			diagonal += RunCase("G斜め", xs[i], xs[i], false);
		probe.log("  G 斜め（dx = dz）2959 / 3428 / 3000 → " + diagonal +
				  "（文字は Z の残差。X の残差は明細に出る）");
	}

	// =======================================================================
	probe.log("=== 非 0 の明細（危険だった L。仮数 m と指数つき） ===");
	if (gNonZero.empty())
		probe.log("  1 件も無い（#67 の 5 値すら再現しなかったということなので、"
				  "測り方が #67 と違う。A 群の地図を先に読むこと）");
	for (size_t i = 0; i < gNonZero.size(); ++i)
	{
		const Observation& obs = gNonZero[i];
		const double reference = (obs.dz != 0) ? obs.dz : obs.dx;
		probe.log("  " + obs.group + " L=" + NumBoth(reference) + " ／ 比=" + Num(obs.ratio) +
				  " ／ 残差 z=" + NumBoth(obs.residualZ) + " x=" + NumBoth(obs.residualX) +
				  " ／ m=" + Num(SignificandOf(reference)) +
				  " e=" + std::to_string(static_cast<long long>(ExponentOf(reference))));
	}

	probe.log("=== まとめ ===");
	probe.log("  非 0 だったケース: " + std::to_string(static_cast<long long>(gNonZero.size())) +
			  " ／ 比が 0..9 の整数にならなかったケース: " +
			  std::to_string(static_cast<long long>(gOdd.size())) +
			  " ／ 測れなかったケース: " + std::to_string(static_cast<long long>(gFailures)));
	if (!gOdd.empty())
		probe.log("  **比が整数にならなかった**＝仮説 (あ)「残差は必ず k × ULP(L)」が崩れている。"
				  "上の明細の該当行がその証拠になる");
	if (gFailures > 0)
		probe.fail("測れなかったケースが " + std::to_string(static_cast<long long>(gFailures)) +
				   " 件ある（地図の x）。PIO を作れなかったか、パスを読み戻せなかった");
}
