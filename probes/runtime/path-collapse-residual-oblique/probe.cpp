//
//	probes/runtime/path-collapse-residual-oblique/probe.cpp
//
//	[issue #73] **鉛直でないパスを 0 長へ潰すと、何がどう残るのか。**
//
//	#71（PR #72）の走査で分かっているのは**鉛直（dx = dy = 0）のパス**についてで、
//	そこは片付いている（残差は `round(m) × ULP(L)`。符号は L と同じ。仮数だけで決まる）。
//	その走査の最後に**非鉛直を 3 通りだけ**通したところ、**規則が違った**:
//
//	  | 渡したパス | 潰した後 |
//	  | --- | --- |
//	  | (2959, 0, 0) / (3428, 0, 0) / (3000, 0, 0)（水平） | **潰れない。** X の差がそのまま残る |
//	  | (2959, 0, 2959)（斜め） | X はそのまま。Z の残差は **0**（鉛直では +1 ULP） |
//	  | (3428, 0, 3428) | X はそのまま。Z の残差 +2 ULP（鉛直と同じ） |
//	  | (3000, 0, 3000) | X はそのまま。Z の残差 **−1 ULP**（鉛直では 0。**符号が逆**） |
//
//	3 ケースずつしか見ていないので、規則そのものは未調査である。この走査で次の 4 つを決める。
//
//	  1. **潰れるのは Z の差だけなのか。** 水平が丸ごと保たれたのは「Z が元から 0
//	     だから」なのか「X / Y は作り直しが触らないから」なのか。**両方に値がある**
//	     パス（dx ≠ dz）で分ける。→ B 群
//	  2. **Z の残差は何で決まるのか。** 鉛直では「Z の差 L の仮数」だけだった。斜めでは
//	     **3 次元の長さ**なのか、`dx` と `dz` の組なのか。→ C 群（dz を固定して dx を振る）
//	     と、**C6 群（ピタゴラス数で 3 次元長を厳密に揃えた 3 つ組）が最も鋭い**——
//	     3 次元長だけで決まるなら、(4000,0,3000) と (3000,0,4000) と (0,0,5000) は
//	     **同じ残差**になる。
//	  3. **符号はいつ逆になるのか。取りうる値は何種類か。** → D 群（dx × dz の格子）と、
//	     最後の「比の内訳」（走査全体で出た比を数え上げる）
//	  4. **Y 成分でも同じか**（#71 の走査は X しか振っていない）。→ E 群
//
//	## 測り方（#72 と同じ。1 ケース 1 オブジェクト・読むのは 1 地点だけ）
//
//	    (x0,y0,z0) → (x0+dx, y0+dy, z0+dz) のパスで PIO を作る
//	      → **バウンドを 1 本も書かずに** ResetObject（＝0 長へ潰れる経路）
//	      → 局所パスの両端を読み、**残った差**を成分ごとに出す
//
//	断面（プロファイル）は渡さない——#67 が**断面あり／なし／NoOffset のどれでも同じ残差**
//	だと確かめ、#72 の A 群が再確認している。
//
//	## 読み方
//
//	各群は **1 文字 1 ケースの地図**で出す。文字は **Z の残差 ÷ ULP(基準)**（基準は
//	dz ≠ 0 なら dz、そうでなければ dx か dy。符号は基準に揃える）:
//
//	    0       厳密に 0（Z は完全に潰れた）
//	    1〜9    +k ULP 残った
//	    a〜i    **−1〜−9 ULP 残った**（＝符号が逆。#73 の主題のひとつ）
//	    !       **X か Y の差が渡した値から動いた**（1 の答えが「Z だけではない」になる）
//	    ?       比が整数にならなかった
//	    x       測れなかった
//
//	**予測式はプローブの中で計算しない**——`Findings/Investigation Techniques.md`
//	「1 ULP を追う調査では、プローブの中で予測式を計算しない」（FMA 縮約で別の値になる）。
//	出すのは実測値と、丸めの入らない値（仮数・指数・ULP との比）だけである。
//

#include "Probe.h"

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
		double dx = 0, dy = 0, dz = 0;						// 渡した差
		double ox = 0, oy = 0, oz = 0;						// 渡した始点（世界座標）
		double p0x = 0, p0y = 0, p0z = 0;					// 潰した後の局所パスの始点
		double p1x = 0, p1y = 0, p1z = 0;					// 同 終点
		double residualX = 0, residualY = 0, residualZ = 0; // 潰した後に残った差
		double reference = 0; // 比の基準（dz ≠ 0 なら dz、そうでなければ dx か dy）
		double ratio = 0;	  // Z の残差 ÷ ULP(基準)（符号は基準に揃える）
		bool keptXY = true; // X / Y の差が渡した値とビット一致で残ったか
		bool measured = false;
		char mark = 'x';
	};

	std::vector<Observation> gAll;	   // 全件（最後の数え上げに使う）
	std::vector<Observation> gMovedXY; // **X か Y が動いた件**（1 の答えが変わる証拠）
	int gFailures = 0;

	bool ReadPathEndpoints(MCObjectHandle pio, WorldPt3& outP0, WorldPt3& outP1)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
			return false;
		const Boolean got0 = gSDK->NurbsGetPt3D(path, 0, 0, outP0);
		const Boolean got1 = gSDK->NurbsGetPt3D(path, 0, 1, outP1);
		return got0 && got1;
	}

	// 1 ケース。**(ox,oy,oz) → (ox+dx, oy+dy, oz+dz)** のパスで PIO を作り、
	// バウンドを書かずに ResetObject して、成分ごとに残った差を読む。
	Observation RunCase(const std::string& group, double dx, double dy, double dz, double ox = 0,
						double oy = 0, double oz = 0)
	{
		Observation obs;
		obs.group = group;
		obs.dx = dx;
		obs.dy = dy;
		obs.dz = dz;
		obs.ox = ox;
		obs.oy = oy;
		obs.oz = oz;

		// 比の基準は「渡した差のうち Z を優先して 0 でないもの」。
		obs.reference = (dz != 0) ? dz : ((dx != 0) ? dx : dy);

		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(ox, oy, oz), false, 1);
		if (curve == nullptr)
		{
			++gFailures;
			gAll.push_back(obs);
			return obs;
		}
		gSDK->Add3DVertex(curve, WorldPt3(ox + dx, oy + dy, oz + dz), true);
		// **座標を明示的に入れ直す**（#67 / #72 と同じ作法。Add3DVertex が足した点が
		// 渡した位置にならないことがある）。
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, WorldPt3(ox, oy, oz));
			gSDK->NurbsSetPt3D(curve, 0, 1, WorldPt3(ox + dx, oy + dy, oz + dz));
		}

		MCObjectHandle noProfile = nullptr;
		MCObjectHandle pio = gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile);
		if (pio == nullptr)
		{
			++gFailures;
			gAll.push_back(obs);
			return obs;
		}

		gSDK->ResetObject(pio);

		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(pio, p0, p1))
		{
			++gFailures;
			gAll.push_back(obs);
			return obs;
		}
		obs.measured = true;
		obs.p0x = p0.x;
		obs.p0y = p0.y;
		obs.p0z = p0.z;
		obs.p1x = p1.x;
		obs.p1y = p1.y;
		obs.p1z = p1.z;
		obs.residualX = p1.x - p0.x;
		obs.residualY = p1.y - p0.y;
		obs.residualZ = p1.z - p0.z;
		// **ビット一致で残ったか**（「X / Y は作り直しが触らない」の判定はこれ）。
		obs.keptXY = (obs.residualX == dx) && (obs.residualY == dy);

		const double unit = UlpOf(obs.reference);
		if (unit > 0)
			obs.ratio = (obs.reference > 0) ? obs.residualZ / unit : -(obs.residualZ / unit);

		if (!obs.keptXY)
		{
			obs.mark = '!';
			gMovedXY.push_back(obs);
		}
		else if (obs.residualZ == 0)
		{
			obs.mark = '0';
		}
		else if (obs.ratio == std::floor(obs.ratio) && obs.ratio >= 1 && obs.ratio <= 9)
		{
			obs.mark = static_cast<char>('0' + static_cast<int>(obs.ratio));
		}
		else if (obs.ratio == std::floor(obs.ratio) && obs.ratio <= -1 && obs.ratio >= -9)
		{
			// a = −1 ULP、b = −2 ULP …（**符号が逆**の側）。
			obs.mark = static_cast<char>('a' + static_cast<int>(-obs.ratio) - 1);
		}
		else
		{
			obs.mark = '?';
		}

		gAll.push_back(obs);
		return obs;
	}

	// 1 ケースの明細（B / F / C6 群のように、地図では足りない群で使う）。
	std::string Describe(const Observation& obs)
	{
		std::string line =
			obs.group + " 渡した差 (" + Num(obs.dx) + ", " + Num(obs.dy) + ", " + Num(obs.dz) + ")";
		if (obs.ox != 0 || obs.oy != 0 || obs.oz != 0)
			line += " 始点 (" + Num(obs.ox) + ", " + Num(obs.oy) + ", " + Num(obs.oz) + ")";
		if (!obs.measured)
			return line + " → **測れなかった**";
		line += " → 残差 x=" + NumBoth(obs.residualX) + " y=" + NumBoth(obs.residualY) +
				" z=" + NumBoth(obs.residualZ);
		line +=
			" ／ X・Y はビット一致で残ったか=" + std::string(obs.keptXY ? "はい" : "**いいえ**");
		line += " ／ z の比=" + Num(obs.ratio) + "（基準 " + Num(obs.reference) +
				" の ULP=" + Num(UlpOf(obs.reference)) + "）";
		line += " ／ 局所パス p0=(" + Num(obs.p0x) + ", " + Num(obs.p0y) + ", " + Num(obs.p0z) +
				") p1=(" + Num(obs.p1x) + ", " + Num(obs.p1y) + ", " + Num(obs.p1z) + ")";
		return line;
	}

	// dz を 1 本固定して、dx（または dy）を振った地図を 1 行で出す。
	void RunSweep(vwprobe::Report& probe, const std::string& group, const std::string& how,
				  double dz, const std::vector<double>& sweep, bool useY)
	{
		std::string row;
		for (size_t i = 0; i < sweep.size(); ++i)
			row += RunCase(group, useY ? 0 : sweep[i], useY ? sweep[i] : 0, dz).mark;
		probe.log("  " + group + " " + how + " → " + row);
	}

	// dz × dx の格子。行が dz、列が dx。
	void RunGrid(vwprobe::Report& probe, const std::string& group, const std::string& how,
				 const std::vector<double>& dzValues, const std::vector<double>& sweep, bool useY)
	{
		probe.log("--- " + group + " " + how + "（" +
				  std::to_string(static_cast<long long>(dzValues.size() * sweep.size())) +
				  " ケース。行 = dz、列 = " + std::string(useY ? "dy" : "dx") + " が " +
				  Num(sweep.front()) + " から " + Num(sweep.back()) + " まで " +
				  std::to_string(static_cast<long long>(sweep.size())) + " 本） ---");
		for (size_t r = 0; r < dzValues.size(); ++r)
		{
			std::string row;
			for (size_t c = 0; c < sweep.size(); ++c)
				row += RunCase(group, useY ? 0 : sweep[c], useY ? sweep[c] : 0, dzValues[r]).mark;
			probe.log("  " + group + " dz=" + Num(dzValues[r]) + ": " + row);
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

VW_PROBE("path-collapse-residual-oblique", "鉛直でないパスを 0 長へ潰すときの残差の規則を走査する",
		 "水平・斜めのパスを 800 通りほど潰し、成分ごとに残る差を地図にする。"
		 "X/Y は触られるのか・Z の残差は何で決まるのか・符号はいつ逆になるのか・"
		 "Y でも同じか。新規の空図面で走る")
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
		probe.log("地図の文字 = Z の残差 ÷ ULP(基準)（基準は dz。dz=0 のときは dx か dy。"
				  "符号は基準に揃える）。0=厳密に 0／1〜9=+k ULP／**a〜i=−1〜−9 ULP**／"
				  "**!=X か Y の差が動いた**／?=比が整数でない／x=測れなかった");
	}

	// =======================================================================
	// A. 物差し。**#72 の G 群がそのまま出るか**を先に確かめる。ここが再現しなければ、
	//    以下の地図は読んではいけない（測り方が #72 と違うということなので）。
	probe.log("=== A. 物差し（#72 の G 群の再現。ここが合わなければ以下は読まない） ===");
	{
		std::string vertical;
		const double lengths[] = {2959, 3428, 3000};
		for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
			vertical += RunCase("A鉛直", 0, 0, lengths[i]).mark;
		probe.log("  A 鉛直 2959 / 3428 / 3000 → " + vertical + "（#71 と同じなら 120）");

		std::string oblique;
		for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
			oblique += RunCase("A斜め", lengths[i], 0, lengths[i]).mark;
		probe.log("  A 斜め dx=dz 2959 / 3428 / 3000 → " + oblique + "（#72 と同じなら 02a）");

		std::string horizontal;
		for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
			horizontal += RunCase("A水平", lengths[i], 0, 0).mark;
		probe.log("  A 水平 dx のみ 2959 / 3428 / 3000 → " + horizontal +
				  "（X が丸ごと残り Z が 0 なら 000）");
	}

	// =======================================================================
	// B. **問い 1: 潰れるのは Z の差だけなのか。** dx と dz の**両方に値がある**パスを
	//    通し、X / Y の差が**渡した値とビット一致で残るか**を見る。1 件でも動けば
	//    地図に `!` が出る。明細は全件出す（11 件しかない）。
	probe.log("=== B. 問い 1: 潰れるのは Z の差だけか（dx・dy と dz の両方に値があるパス） ===");
	{
		struct Triple
		{
			double dx, dy, dz;
		};
		const Triple cases[] = {
			{2959, 0, 100},		{100, 0, 2959},	 {3000, 0, 1},	 {1, 0, 3000},
			{2959, 0, 3000},	{3000, 0, 2959}, {0, 2959, 100}, {0, 100, 2959},
			{1000, 2000, 3000}, {2959, 0, 0},	 {0, 2959, 0},
		};
		std::string row;
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		{
			const Observation obs = RunCase("B", cases[i].dx, cases[i].dy, cases[i].dz);
			row += obs.mark;
			probe.log("  " + Describe(obs));
		}
		probe.log("  B 地図 → " + row +
				  "（**! が 1 つも無ければ「X / Y の差は作り直しが触らない」**）");
	}

	// =======================================================================
	// C. **問い 2: Z の残差は何で決まるのか。** dz を固定して dx を振る。
	probe.log("=== C. 問い 2: Z の残差は何で決まるのか（dz を固定して dx を振る） ===");
	{
		const std::vector<double> sweep = Range(0, 47, 1);
		probe.log("--- C1〜C3 dx = 0〜47（1 mm 刻み。左端が dx=0 ＝ 鉛直そのもの） ---");
		RunSweep(probe, "C1", "dz=2959, dx=0..47", 2959, sweep, false);
		RunSweep(probe, "C2", "dz=3000, dx=0..47", 3000, sweep, false);
		RunSweep(probe, "C3", "dz=3428, dx=0..47", 3428, sweep, false);

		// dx を固定して dz を振る（逆向き。C1〜C3 と合わせて「組で決まるのか」を見る）。
		probe.log("--- C4 dx を固定して dz を振る ---");
		{
			const std::vector<double> dzSweep = Range(2976, 3023, 1);
			std::string row;
			for (size_t i = 0; i < dzSweep.size(); ++i)
				row += RunCase("C4", 3000, 0, dzSweep[i]).mark;
			probe.log("  C4 dx=3000, dz=2976..3023 → " + row);
			std::string bare;
			for (size_t i = 0; i < dzSweep.size(); ++i)
				bare += RunCase("C4鉛直", 0, 0, dzSweep[i]).mark;
			probe.log("  C4 鉛直（dx=0）, dz=2976..3023 → " + bare +
					  "（上と同じなら dx は効いていない）");
		}

		// dx を桁で振る（3 次元長が大きく変わる）。
		probe.log("--- C5 dz=3000 のまま dx を桁で振る ---");
		{
			const double xs[] = {0, 1e-3, 1, 10, 100, 1000, 3000, 10000, 100000, 1e7};
			std::string row;
			for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i)
				row += RunCase("C5", xs[i], 0, 3000).mark;
			probe.log("  C5 dx = 0 / 1e-3 / 1 / 10 / 100 / 1000 / 3000 / 1e4 / 1e5 / 1e7 → " + row);
		}

		// **C6 がこの問いの本命。** ピタゴラス数で 3 次元長を**厳密な整数に揃えた**
		// 3 つ組を通す。3 次元長だけで決まるなら、同じ組の 3 件は**残差そのもの**
		// （比ではなく値）が一致するはずである。
		probe.log("--- C6 3 次元長を厳密に揃えた 3 つ組（**3 次元長で決まるか**の本命） ---");
		{
			struct Family
			{
				double a, b, len; // (a,0,b) と (b,0,a) と (0,0,len) を通す
			};
			const Family families[] = {
				{3000, 4000, 5000},	  // 3-4-5 × 1000
				{1250, 3000, 3250},	  // 5-12-13 × 250
				{1600, 3000, 3400},	  // 8-15-17 × 200
				{875, 3000, 3125},	  // 7-24-25 × 125
				{8877, 11836, 14795}, // 3-4-5 × 2959（事故の柱の長さから作った）
			};
			for (size_t i = 0; i < sizeof(families) / sizeof(families[0]); ++i)
			{
				const Family& f = families[i];
				probe.log("  C6 3 次元長 " + Num(f.len) + " の 3 つ組:");
				probe.log("    " + Describe(RunCase("C6", f.a, 0, f.b)));
				probe.log("    " + Describe(RunCase("C6", f.b, 0, f.a)));
				probe.log("    " + Describe(RunCase("C6", 0, 0, f.len)));
			}
			probe.log("  ↑ **3 件の残差 z が同じ値なら「3 次元長だけで決まる」**。"
					  "違えば dx と dz の組で決まっている");
		}
	}

	// =======================================================================
	// D. **問い 3: 符号はいつ逆になるのか。** dx × dz の格子でまとめて舐める。
	probe.log("=== D. 問い 3: 符号はいつ逆になるのか（dx × dz の格子） ===");
	RunGrid(probe, "D1", "dz=2990〜3010 × dx=0〜20", Range(2990, 3010, 1), Range(0, 20, 1), false);
	RunGrid(probe, "D2", "dz=2955〜2965 × dx=0〜10", Range(2955, 2965, 1), Range(0, 10, 1), false);
	RunGrid(probe, "D3", "dz=3424〜3434 × dx=0〜10", Range(3424, 3434, 1), Range(0, 10, 1), false);

	// =======================================================================
	// E. **問い 4: Y 成分でも同じか。** C1〜C3 の dx を dy に置き換える。
	//    **X と同じ地図が出れば「水平成分がどちらかは効かない」**。
	probe.log("=== E. 問い 4: Y 成分でも同じか（dx の代わりに dy を振る） ===");
	{
		const std::vector<double> sweep = Range(0, 47, 1);
		RunSweep(probe, "E1", "dz=2959, dy=0..47", 2959, sweep, true);
		RunSweep(probe, "E2", "dz=3000, dy=0..47", 3000, sweep, true);
		RunSweep(probe, "E3", "dz=3428, dy=0..47", 3428, sweep, true);
		probe.log("  ↑ **C1〜C3 と 1 文字ずつ同じなら、X と Y は同じ効き方をしている**");

		// dx と dy を**同時に**入れる（水平成分が 2 つある斜め材）。
		const double diagonals[] = {2959, 3000, 3428};
		std::string row;
		for (size_t i = 0; i < sizeof(diagonals) / sizeof(diagonals[0]); ++i)
			row += RunCase("E4", diagonals[i], diagonals[i], diagonals[i]).mark;
		probe.log("  E4 dx = dy = dz（2959 / 3000 / 3428） → " + row);
	}

	// =======================================================================
	// F. **対照: 端点の絶対座標に依らないか。** #67 は鉛直について「決めているのは
	//    長さだけ」と確かめている。斜めでも同じかを見る（同じ差を別の始点で通す）。
	probe.log("=== F. 対照: 同じ差を別の始点で（#67 の「絶対座標に依らない」を斜めでも） ===");
	{
		struct Origin
		{
			double x, y, z;
		};
		const Origin origins[] = {{0, 0, 0}, {1000, 0, 0}, {0, 0, 572}, {1000, 2000, 3531}};
		std::string row;
		for (size_t i = 0; i < sizeof(origins) / sizeof(origins[0]); ++i)
		{
			const Observation obs =
				RunCase("F", 3000, 0, 3000, origins[i].x, origins[i].y, origins[i].z);
			row += obs.mark;
			probe.log("  " + Describe(obs));
		}
		probe.log("  F 地図（差はすべて (3000, 0, 3000)） → " + row +
				  "（4 文字とも同じなら始点に依らない）");
	}

	// =======================================================================
	probe.log("=== X か Y が動いたケース（問い 1 の答えを変える証拠） ===");
	if (gMovedXY.empty())
		probe.log("  1 件も無い＝**X / Y の差は、この経路の作り直しでは触られていない**"
				  "（走査した全ケースでビット一致）");
	for (size_t i = 0; i < gMovedXY.size(); ++i)
		probe.log("  " + Describe(gMovedXY[i]));

	// =======================================================================
	// 比の内訳。**取りうる値が何種類あるか**（問い 3）はここで数える。
	probe.log("=== 比（Z の残差 ÷ ULP(基準)）の内訳 ===");
	{
		std::vector<double> values;
		std::vector<long long> counts;
		long long oddCount = 0;
		for (size_t i = 0; i < gAll.size(); ++i)
		{
			if (!gAll[i].measured || !gAll[i].keptXY)
				continue;
			const double ratio = gAll[i].ratio;
			if (ratio != std::floor(ratio))
				++oddCount;
			bool found = false;
			for (size_t v = 0; v < values.size(); ++v)
			{
				if (values[v] == ratio)
				{
					++counts[v];
					found = true;
					break;
				}
			}
			if (!found)
			{
				values.push_back(ratio);
				counts.push_back(1);
			}
		}
		// 小さい順に並べ替えて出す（比較だけなので誤差は入らない）。
		for (size_t i = 0; i < values.size(); ++i)
			for (size_t j = i + 1; j < values.size(); ++j)
				if (values[j] < values[i])
				{
					const double tmpValue = values[i];
					values[i] = values[j];
					values[j] = tmpValue;
					const long long tmpCount = counts[i];
					counts[i] = counts[j];
					counts[j] = tmpCount;
				}
		for (size_t i = 0; i < values.size(); ++i)
			probe.log("  比 " + Num(values[i]) + " … " +
					  std::to_string(static_cast<long long>(counts[i])) + " 件");
		probe.log("  種類: " + std::to_string(static_cast<long long>(values.size())) +
				  " ／ 整数にならなかった比: " + std::to_string(oddCount) + " 件");
	}

	// =======================================================================
	// 非 0 だったケースの明細。**地図で 0 以外の文字が出たところの実値**がここに出る。
	probe.log("=== 非 0 の明細（Z の残差が残ったケース。仮数 m と指数つき） ===");
	{
		long long shown = 0;
		for (size_t i = 0; i < gAll.size(); ++i)
		{
			const Observation& obs = gAll[i];
			if (!obs.measured || obs.residualZ == 0)
				continue;
			++shown;
			// **明細が膨れるとログが読めなくなる**ので、格子（D 群）は代表だけ出す。
			if (obs.group.compare(0, 1, "D") == 0 && (shown % 20) != 1)
				continue;
			probe.log("  " + obs.group + " (dx,dy,dz)=(" + Num(obs.dx) + ", " + Num(obs.dy) + ", " +
					  Num(obs.dz) + ") ／ 比=" + Num(obs.ratio) +
					  " ／ 残差 z=" + NumBoth(obs.residualZ) + " x=" + NumBoth(obs.residualX) +
					  " ／ 基準=" + NumBoth(obs.reference) +
					  " m=" + Num(SignificandOf(obs.reference)) +
					  " e=" + std::to_string(static_cast<long long>(ExponentOf(obs.reference))));
		}
		probe.log("  非 0 だったケース: " + std::to_string(shown) +
				  " 件"
				  "（D 群は 20 件に 1 件だけ明細を出している。地図のほうが本体）");
	}

	probe.log("=== まとめ ===");
	probe.log(
		"  走らせたケース: " + std::to_string(static_cast<long long>(gAll.size())) +
		" ／ X か Y が動いたケース: " + std::to_string(static_cast<long long>(gMovedXY.size())) +
		" ／ 測れなかったケース: " + std::to_string(static_cast<long long>(gFailures)));
	if (gFailures > 0)
		probe.fail("測れなかったケースが " + std::to_string(static_cast<long long>(gFailures)) +
				   " 件ある（地図の x）。PIO を作れなかったか、パスを読み戻せなかった");
}
