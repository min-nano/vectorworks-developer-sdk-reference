//
//	probes/runtime/oblique-blindspot-rebuild/probe.cpp
//
//	[issue #75] **水平成分を持つ部材は「0 長の死角」に落ちないのか。**
//
//	`Findings/Parametric Objects.md` に実機確認済みで載っている 2 つを繋ぐと、推論が 1 つ出る:
//
//	  1. **`ResetObject` が作り直すかどうかは 3 次元長で決まり、閾値は 1e-7**（#61）。
//	     0 でも 1e-7 以上でもない長さのパスは作り直されず、0 長のまま図に出ない
//	     （＝「柱 46 本」の事故）。
//	  2. **0 長へ潰す作り直しが触るのは Z の差だけ**（#73 / PR #74 の実測。1119 件全件で
//	     `dx` / `dy` はビット一致で残り、Z には `±k ULP`（|k| ≤ 3）しか残らない）。
//
//	つまり潰れた後のパスは `(dx, dy, ±k ULP)` で、**その 3 次元長はほぼ `|水平成分|`**。
//	だから**水平成分が 1e-7 以上ある斜め材は、必ず作り直される（死角に落ちない）**はずで、
//	**「柱 46 本」の事故は鉛直材に固有**ということになる。**これはまだ実機で測っていない。**
//
//	## この走査で決めること
//
//	  1. **推論そのもの。** 潰した後にバウンドを書いて `ResetObject` すると、
//	     水平成分がどこから「作り直される」へ切り替わるか。→ B 群（`dx` を死角の前後で振る）
//	  2. **見ているのは成分ではなく 3 次元長か。** `dx` も `dy` も 1e-7 未満なのに
//	     長さが 1e-7 を超える組で分ける。→ D 群
//	  3. **作り直された斜め材はどんな形になるのか。** 水平成分は保たれたまま Z だけが
//	     バウンドへ従うのか（＝元の傾きが変わる）、それとも鉛直へ作り直されるのか。
//	     → E 群（実寸の斜め材）
//	  4. **水平材（dz = 0）で同じ機構が働くか。** `Findings`「検証範囲の限界」が
//	     **未確認**としている点。→ F 群
//
//	## 測り方（1 ケース 1 オブジェクト。**`ResetObject` を 2 回**呼ぶのがこの調査の要）
//
//	    (0,0,0) → (dx, dy, dz) のパスで構造材 PIO を作る
//	      → ① **バウンドを書かずに** ResetObject（＝0 長へ潰す。ここまでは #73 と同じ）
//	      → 潰れた後の局所パスを読む（残るのは (dx, dy, ±k ULP) のはず）
//	      → ② `LayerElevation` のバウンドを 2 本書いて（解決Z 2500 / 5500 ＝ span 3000）
//	         **もう一度** ResetObject
//	      → 局所パスを読み直し、**バウンドどおりの長さに作り直されたか**を見る
//
//	`dz` は**鉛直（dx = 0）で残差の出る値**を選ぶ（2959 = 1 ULP / 3428 = 2 ULP）。
//	そうしないと ① で厳密に 0 になり、②で必ず作り直されてしまって死角の話にならない
//	——A 群がその物差しで、`dz = 3000`（鉛直で残差 0）は**作り直される側**に出るはずである。
//
//	バウンドは**新規の空図面で成立する書き方**を使う（#61 と同じ。
//	`{eStoryObjectBound_LayerElevation, 0, "", offset}` を ID 0 / ID 1 に offset 違いで
//	書くと、解決Zは「レイヤの高さ + offset」になる）。階も `_Story` バウンドも要らない。
//
//	## 読み方（この地図の文字は #73 のものとは別物）
//
//	    R   **作り直された**（②の後の Z の差がバウンドの span 3000 に厳密に一致）
//	    .   **作り直されなかった**（②の後も潰れたまま＝**死角に落ちた**）
//	    !   作り直されたが**水平成分が失われた**（X / Y の差が ② で変わった）
//	    ?   どれでもない（明細を読むこと）
//	    x   測れなかった
//
//	**予測式はプローブの中で計算しない**——`Findings/Investigation Techniques.md`
//	「1 ULP を追う調査では、プローブの中で予測式を計算しない」（FMA 縮約で別の値になる）。
//	出すのは実測値と、丸めの入らない値（ULP との比）だけである。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// バウンドの解決Z。**新規の空図面（レイヤの高さ 0）で 2500 / 5500 になる**ように
	// offset で書く。作り直されたときのパスの長さはこの差（3000）になるはず。
	const double kBoundOffsetLow = 2500;
	const double kBoundOffsetHigh = 5500;
	const double kBoundSpan = kBoundOffsetHigh - kBoundOffsetLow;

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

	// 1 ケースぶんの観測。
	struct Observation
	{
		std::string group;
		double dx = 0, dy = 0, dz = 0; // 渡した差

		// ① バウンド無しの ResetObject（0 長へ潰す）の後
		double collapsedX = 0, collapsedY = 0, collapsedZ = 0;
		double collapsedLength = 0; // 3 次元長（**この値が死角 (0, 1e-7) に入るかが主語**）
		double collapsedRatio = 0; // Z の残差 ÷ ULP(dz)（符号は dz に揃える）

		// ② バウンドを 2 本書いて ResetObject した後
		double finalX = 0, finalY = 0, finalZ = 0;
		double boundLow = 0, boundHigh = 0; // GetObjectBoundElevation の読み戻し
		bool wroteBounds = false; // SetObjectStoryBound が 2 本とも true を返したか

		bool keptHorizontal = false; // ② で X / Y の差がビット一致で残ったか
		bool rebuiltZ = false; // ② で Z の差の大きさが span にビット一致したか
		bool measured = false;
		char mark = 'x';
	};

	std::vector<Observation> gAll;
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

	// `LayerElevation` のバウンドを 1 本書く（#61 と同じ書き方）。
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

	// 1 ケース。**ResetObject を 2 回**（①潰す → ②バウンドから作り直させる）。
	Observation RunCase(const std::string& group, double dx, double dy, double dz)
	{
		Observation obs;
		obs.group = group;
		obs.dx = dx;
		obs.dy = dy;
		obs.dz = dz;

		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(0, 0, 0), false, 1);
		if (curve == nullptr)
		{
			++gFailures;
			gAll.push_back(obs);
			return obs;
		}
		gSDK->Add3DVertex(curve, WorldPt3(dx, dy, dz), true);
		// **座標を明示的に入れ直す**（#67 / #72 / #74 と同じ作法。Add3DVertex が足した点が
		// 渡した位置にならないことがある）。
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, WorldPt3(0, 0, 0));
			gSDK->NurbsSetPt3D(curve, 0, 1, WorldPt3(dx, dy, dz));
		}

		MCObjectHandle noProfile = nullptr;
		MCObjectHandle pio = gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile);
		if (pio == nullptr)
		{
			++gFailures;
			gAll.push_back(obs);
			return obs;
		}

		// --- ① バウンドを書かずに ResetObject（0 長へ潰す） ---
		gSDK->ResetObject(pio);

		WorldPt3 c0, c1;
		if (!ReadPathEndpoints(pio, c0, c1))
		{
			++gFailures;
			gAll.push_back(obs);
			return obs;
		}
		obs.collapsedX = c1.x - c0.x;
		obs.collapsedY = c1.y - c0.y;
		obs.collapsedZ = c1.z - c0.z;
		// 3 次元長。**hypot ではなく素直な式**で出す（値そのものが主語ではなく、
		// 死角 (0, 1e-7) のどちら側かだけが主語なので、この精度で足りる）。
		obs.collapsedLength =
			std::sqrt(obs.collapsedX * obs.collapsedX + obs.collapsedY * obs.collapsedY +
					  obs.collapsedZ * obs.collapsedZ);
		{
			const double unit = UlpOf(dz);
			if (unit > 0)
				obs.collapsedRatio = (dz > 0) ? obs.collapsedZ / unit : -(obs.collapsedZ / unit);
		}

		// --- ② バウンドを 2 本書いて、もう一度 ResetObject ---
		const bool wrote0 = WriteLayerElevationBound(pio, 0, kBoundOffsetLow);
		const bool wrote1 = WriteLayerElevationBound(pio, 1, kBoundOffsetHigh);
		obs.wroteBounds = wrote0 && wrote1;
		obs.boundLow = gSDK->GetObjectBoundElevation(pio, static_cast<MockUp::TObjectBoundID>(0));
		obs.boundHigh = gSDK->GetObjectBoundElevation(pio, static_cast<MockUp::TObjectBoundID>(1));

		gSDK->ResetObject(pio);

		WorldPt3 f0, f1;
		if (!ReadPathEndpoints(pio, f0, f1))
		{
			++gFailures;
			gAll.push_back(obs);
			return obs;
		}
		obs.measured = true;
		obs.finalX = f1.x - f0.x;
		obs.finalY = f1.y - f0.y;
		obs.finalZ = f1.z - f0.z;

		// **作り直されたか**の判定は「Z の差の大きさが span にビット一致するか」。
		// 作り直しは符号を反転させることがある（Findings の 1〜5。挿入点が上端へ移る）
		// ので、大きさで見る。
		obs.rebuiltZ = (std::fabs(obs.finalZ) == kBoundSpan);
		// **水平成分が保たれたか**は、① の後の値とのビット一致で見る
		// （渡した dx とではなく——① が触らないことは #73 が確かめている）。
		obs.keptHorizontal = (obs.finalX == obs.collapsedX) && (obs.finalY == obs.collapsedY);

		if (obs.rebuiltZ)
			obs.mark = obs.keptHorizontal ? 'R' : '!';
		else if (std::fabs(obs.finalZ) < 1e-6)
			obs.mark = '.';
		else
			obs.mark = '?';

		gAll.push_back(obs);
		return obs;
	}

	// 1 ケースの明細（3 行）。**この調査は件数が少ないので、地図と明細を両方出す**
	// （#73 と違い「どの dx で切り替わるか」が主語なので、1 件ずつ読めるほうがよい）。
	void Describe(vwprobe::Report& probe, const Observation& obs)
	{
		const std::string head = "    " + obs.group + " 渡した差 (" + Num(obs.dx) + ", " +
								 Num(obs.dy) + ", " + Num(obs.dz) + ")";
		if (!obs.measured)
		{
			probe.log(head + " → **測れなかった**");
			return;
		}
		probe.log(head);
		probe.log(
			"      ① 潰した後 (" + NumBoth(obs.collapsedX) + ", " + NumBoth(obs.collapsedY) + ", " +
			NumBoth(obs.collapsedZ) + ")" + " ／ 3 次元長=" + Num(obs.collapsedLength) +
			"（死角 (0, 1e-7) の" +
			std::string((obs.collapsedLength > 0 && obs.collapsedLength < 1e-7) ? "**内**" : "外") +
			"）" + " ／ z の残差 ÷ ULP(dz)=" + Num(obs.collapsedRatio));
		probe.log("      ② バウンド後 (" + NumBoth(obs.finalX) + ", " + NumBoth(obs.finalY) + ", " +
				  NumBoth(obs.finalZ) + ")" + " ／ 解決Z " + Num(obs.boundLow) + " / " +
				  Num(obs.boundHigh) + "（span=" + Num(obs.boundHigh - obs.boundLow) + "）" +
				  " ／ バウンドを 2 本とも書けたか=" +
				  std::string(obs.wroteBounds ? "はい" : "**いいえ**"));
		probe.log("      → **" +
				  std::string(obs.rebuiltZ ? "作り直された" : "作り直されなかった（死角）") +
				  "** ／ 水平成分は①のまま残ったか=" +
				  std::string(obs.keptHorizontal ? "はい" : "**いいえ**") +
				  " ／ 印=" + std::string(1, obs.mark));
	}

	// 1 本の dz について dx（または dy）を振り、地図 1 行＋明細を出す。
	// **`detail` が偽でも、R と . 以外（! / ? / x）の明細は必ず出す**
	// ——地図の 1 文字では読めない結果を、取りこぼさないため。
	void RunSweep(vwprobe::Report& probe, const std::string& group, const std::string& how,
				  double dz, const std::vector<double>& sweep, bool useY, bool detail)
	{
		std::string row;
		std::vector<Observation> observations;
		for (size_t i = 0; i < sweep.size(); ++i)
		{
			const Observation obs = RunCase(group, useY ? 0 : sweep[i], useY ? sweep[i] : 0, dz);
			row += obs.mark;
			observations.push_back(obs);
		}
		probe.log("  " + group + " " + how + " → " + row);
		for (size_t i = 0; i < observations.size(); ++i)
		{
			const char mark = observations[i].mark;
			if (detail || (mark != 'R' && mark != '.'))
				Describe(probe, observations[i]);
		}
	}

	// 死角（1e-7）の前後を跨ぐ水平成分の並び。**issue #75 が指定したもの**に、
	// 桁を 2 つ足してある。
	std::vector<double> BlindSpotSweep()
	{
		std::vector<double> values;
		values.push_back(0);
		values.push_back(1e-9);
		values.push_back(1e-8);
		values.push_back(9.9e-8);
		values.push_back(1e-7);
		values.push_back(1.1e-7);
		values.push_back(1e-6);
		values.push_back(1e-5);
		values.push_back(1e-3);
		values.push_back(1);
		values.push_back(1000);
		return values;
	}

	const char* kBlindSpotLegend =
		"（並びは dx = 0 / 1e-9 / 1e-8 / 9.9e-8 / 1e-7 / 1.1e-7 / 1e-6 / 1e-5 / 1e-3 / 1 / 1000）";
} // namespace

VW_PROBE("oblique-blindspot-rebuild", "水平成分を持つ部材が「0 長の死角」に落ちないかを実測する",
		 "0 長へ潰した後にバウンドを書いて作り直させ、水平成分のどこで"
		 "「作り直される」へ切り替わるかを見る。斜め材・水平材が"
		 "どんな形に作り直されるかも読む。新規の空図面で走る")
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
		probe.log(std::string("バウンドは LayerElevation の offset ") + Num(kBoundOffsetLow) +
				  " / " + Num(kBoundOffsetHigh) +
				  "（作り直されたときの長さ = span = " + Num(kBoundSpan) + "）");
		probe.log("印: R=作り直された（水平成分も①のまま）／.=作り直されなかった（**死角**）"
				  "／!=作り直されたが水平成分が変わった／?=どれでもない／x=測れなかった");
	}

	// =======================================================================
	// A. 物差し。**鉛直材で #61 の表がそのまま再現するか**を先に確かめる。
	//    ここが合わなければ、以下の地図は読んではいけない。
	probe.log("=== A. 物差し（鉛直材。#61 の表が再現するか。合わなければ以下は読まない） ===");
	{
		// dz = 2959（残差 1 ULP）/ 3428（2 ULP）は**死角に落ちる**はず（＝「柱 46 本」）。
		// dz = 3000（残差 0）は**作り直される**はず。
		const double lengths[] = {2959, 3428, 3000};
		std::string row;
		std::vector<Observation> observations;
		for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
		{
			const Observation obs = RunCase("A鉛直", 0, 0, lengths[i]);
			row += obs.mark;
			observations.push_back(obs);
		}
		probe.log("  A 鉛直 dz=2959 / 3428 / 3000 → " + row + "（#61・#71 のとおりなら ..R）");
		for (size_t i = 0; i < observations.size(); ++i)
			Describe(probe, observations[i]);
	}

	// =======================================================================
	// B. **主題: 水平成分を死角の前後で振る。** 推論どおりなら、切り替わる位置は
	//    `dx = 1e-7` ちょうど（3 次元長がほぼ |水平成分| になるため）。
	probe.log("=== B. 主題: 水平成分 dx を死角の前後で振る（dz は鉛直で残差の出る値） ===");
	{
		const std::vector<double> sweep = BlindSpotSweep();
		probe.log(std::string("  ") + kBlindSpotLegend);
		RunSweep(probe, "B1", "dz=2959（鉛直では残差 1 ULP）", 2959, sweep, false, true);
		RunSweep(probe, "B2", "dz=3428（鉛直では残差 2 ULP）", 3428, sweep, false, true);
		// dz=3000 は鉛直でも残差 0（＝作り直される）。dx を振っても R のままか
		// ——#73 は「dx が入ると残差が変わる（0 → −1 ULP）」ことを見ているので、
		// **ここで . が出れば「水平成分は死角を作る側にも回りうる」**ことになる。
		RunSweep(probe, "B3", "dz=3000（鉛直では残差 0）", 3000, sweep, false, false);
	}

	// =======================================================================
	// C. **Y 成分でも同じか。** B1 と 1 文字ずつ同じなら、X と Y は同じ効き方をしている。
	probe.log("=== C. Y 成分でも同じか（dx の代わりに dy を振る） ===");
	{
		const std::vector<double> sweep = BlindSpotSweep();
		probe.log(std::string("  ") + kBlindSpotLegend);
		RunSweep(probe, "C1", "dz=2959, dy を振る", 2959, sweep, true, false);
		RunSweep(probe, "C2", "dz=3428, dy を振る", 3428, sweep, true, false);
		probe.log("  ↑ B1 / B2 と 1 文字ずつ同じなら、X と Y は同じ効き方をしている");
	}

	// =======================================================================
	// D. **見ているのは成分か 3 次元長か。** `dx` も `dy` も 1e-7 未満なのに、
	//    3 次元長は 1e-7 を跨ぐ組を通す（#61 が鉛直＋X で見たのと同じ切り分け）。
	probe.log("=== D. 成分か 3 次元長か（dx・dy とも 1e-7 未満だが長さは跨ぐ組） ===");
	{
		struct Triple
		{
			double dx, dy, dz;
			const char* note;
		};
		const Triple cases[] = {
			// 7e-8 の 2 成分 → 長さ 9.9e-8（**死角の中**）。成分で見ていても長さで見ていても .
			{7e-8, 7e-8, 2959, "成分 7e-8 × 2 → 長さ 9.9e-8（死角の中）"},
			// 8e-8 の 2 成分 → 長さ 1.13e-7（**死角の外**）。**成分で見ているなら . のまま、
			// 長さで見ているなら R**——この 1 行がこの群の主語。
			{8e-8, 8e-8, 2959, "成分 8e-8 × 2 → 長さ 1.13e-7（**ここが分かれ目**）"},
			{9e-8, 9e-8, 3428, "成分 9e-8 × 2 → 長さ 1.27e-7"},
			{6e-8, 6e-8, 2959, "成分 6e-8 × 2 → 長さ 8.5e-8（死角の中）"},
		};
		std::string row;
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		{
			const Observation obs = RunCase("D", cases[i].dx, cases[i].dy, cases[i].dz);
			row += obs.mark;
			probe.log("    " + std::string(cases[i].note));
			Describe(probe, obs);
		}
		probe.log("  D 地図 → " + row +
				  "（**2 文字目が R なら「見ているのは 3 次元長」**。. なら成分で見ている）");
	}

	// =======================================================================
	// E. **作り直された斜め材はどんな形になるのか。** 実寸の斜め材を通し、
	//    ②の後のパスを丸ごと読む。水平成分が残るなら**傾きが変わる**（Z だけが
	//    バウンドへ従う）ことになり、消えるなら鉛直へ作り直されている。
	probe.log("=== E. 作り直された斜め材はどんな形になるのか（実寸の斜め材） ===");
	{
		struct Triple
		{
			double dx, dy, dz;
		};
		const Triple cases[] = {
			{2959, 0, 2959},	{1000, 0, 2959}, {3000, 0, 1000},
			{1000, 2000, 3000}, {0, 2959, 2959}, {2959, 2959, 2959},
		};
		std::string row;
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		{
			const Observation obs = RunCase("E", cases[i].dx, cases[i].dy, cases[i].dz);
			row += obs.mark;
			Describe(probe, obs);
		}
		probe.log("  E 地図 → " + row +
				  "（R なら**水平成分は保たれたまま Z だけがバウンドへ従う**＝傾きが変わる。"
				  "! なら水平成分ごと作り直されている）");
	}

	// =======================================================================
	// F. **水平材（dz = 0）。** `Findings`「検証範囲の限界」が未確認としている点。
	//    ① で潰しても Z は元から 0 なので、パスは (dx, dy, 0) のまま。
	//    ② で何が起きるか——バウンドどおりの鉛直材になるのか、水平成分が残るのか。
	probe.log("=== F. 水平材（dz = 0。Findings の「検証範囲の限界」が未確認としている点） ===");
	{
		struct Triple
		{
			double dx, dy, dz;
		};
		const Triple cases[] = {
			{2959, 0, 0},
			{3000, 0, 0},
			{0, 2959, 0},
			{1000, 2000, 0},
		};
		std::string row;
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		{
			const Observation obs = RunCase("F", cases[i].dx, cases[i].dy, cases[i].dz);
			row += obs.mark;
			Describe(probe, obs);
		}
		probe.log("  F 地図 → " + row);
	}

	// =======================================================================
	probe.log("=== まとめ ===");
	{
		int rebuilt = 0, blind = 0, lostHorizontal = 0, other = 0, unmeasured = 0;
		for (size_t i = 0; i < gAll.size(); ++i)
		{
			switch (gAll[i].mark)
			{
			case 'R':
				++rebuilt;
				break;
			case '.':
				++blind;
				break;
			case '!':
				++lostHorizontal;
				break;
			case '?':
				++other;
				break;
			default:
				++unmeasured;
				break;
			}
		}
		probe.log("  走らせたケース: " + std::to_string(static_cast<long long>(gAll.size())) +
				  " ／ R（作り直された）: " + std::to_string(static_cast<long long>(rebuilt)) +
				  " ／ .（死角に落ちた）: " + std::to_string(static_cast<long long>(blind)) +
				  " ／ !（水平成分が変わった）: " +
				  std::to_string(static_cast<long long>(lostHorizontal)) +
				  " ／ ?: " + std::to_string(static_cast<long long>(other)) +
				  " ／ x: " + std::to_string(static_cast<long long>(unmeasured)));
		if (gFailures > 0)
			probe.fail("測れなかったケースが " + std::to_string(static_cast<long long>(gFailures)) +
					   " 件ある");
	}
}
