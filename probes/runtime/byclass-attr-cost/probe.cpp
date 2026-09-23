//
//	probes/runtime/byclass-attr-cost/probe.cpp
//
//	[issue #94] 描画属性を「クラスに従う」にする 7 つの書き込み（SetPColorsByClass 等）の
//	費用が何で決まるのかを実測する。取り込み側の実測では 6 つが 1 回 3.6ms 掛かる一方、
//	`SetArrowByClass` だけがほぼ 0ms だった。
//
//	確かめること:
//	  (1) **費用は「書いたこと」ではなく「状態が変わったこと」に掛かるのか。**
//	      同じ書き込みを 2 度続け、2 度目が無料かを見る。これは
//	      「`SetArrowByClass` だけ無料なのは、マーカーが文書の既定で既に by-class で、
//	      1 度目から『変わらない書き込み』だからだ」という読みの検証でもある
//	      ——だから最初に文書の既定 7 つを読んで記録する。
//	  (2) **文書の既定（`SetDefaultClass` / `SetDefault*ByClass`）を先に立てておくと、
//	      生まれたオブジェクトが最初から by-class になり、per-object の 8 つを省けるか。**
//	  (3) 「`SetObjectClass` だけでは属性は by-instance のまま」という前提は今も正しいか。
//
//	2 種類のオブジェクトで測る——軽い 2D（矩形）と 3D 実体を持つもの（球）。
//	取り込み側の実測では「実体を持つオブジェクトほど高い」傾向が出ていたため。
//
//	新規の空図面で走らせる（プローブは undo イベントを自分では開かない）。
//

#include "Probe.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace
{
	using ProbeClock = std::chrono::steady_clock;

	// このプローブが 1 つの群で作るオブジェクトの数。per-call の費用を平均で出すため。
	const int kProbeObjectCount = 30;

	// 「クラスに従わせる」7 つ。**並びは固定**で、以下の表の行に対応する。
	const int kProbeAttrCount = 7;

	const char* const kProbeAttrNames[kProbeAttrCount] = {
		"PColors (ペン色)", "FColors (面色)",		 "LW (線の太さ)", "PPat (線種)",
		"FPat (面パターン)", "Arrow (マーカー)", "Opacity (不透明度)"};

	double MsBetween(ProbeClock::time_point from, ProbeClock::time_point to)
	{
		return std::chrono::duration<double, std::milli>(to - from).count();
	}

	std::string FormatMs(double ms)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.2f", ms);
		return std::string(buffer);
	}

	const char* YesNo(Boolean value)
	{
		return value != 0 ? "yes" : "no";
	}

	// --- 7 つの口を添字で呼ぶ（表を回すため） ------------------------------

	void SetAttrByClass(int index, MCObjectHandle object)
	{
		switch (index)
		{
		case 0: gSDK->SetPColorsByClass(object); break;
		case 1: gSDK->SetFColorsByClass(object); break;
		case 2: gSDK->SetLWByClass(object); break;
		case 3: gSDK->SetPPatByClass(object); break;
		case 4: gSDK->SetFPatByClass(object); break;
		case 5: gSDK->SetArrowByClass(object); break;
		default: gSDK->SetOpacityByClass(object); break;
		}
	}

	Boolean GetAttrByClass(int index, MCObjectHandle object)
	{
		switch (index)
		{
		case 0: return gSDK->GetPColorsByClass(object);
		case 1: return gSDK->GetFColorsByClass(object);
		case 2: return gSDK->GetLWByClass(object);
		case 3: return gSDK->GetPPatByClass(object);
		case 4: return gSDK->GetFPatByClass(object);
		case 5: return gSDK->GetArrowByClass(object);
		default: return gSDK->GetOpacityByClass(object);
		}
	}

	void SetDefaultAttrByClass(int index)
	{
		switch (index)
		{
		case 0: gSDK->SetDefaultPColorsByClass(); break;
		case 1: gSDK->SetDefaultFColorsByClass(); break;
		case 2: gSDK->SetDefaultLWByClass(); break;
		case 3: gSDK->SetDefaultPPatByClass(); break;
		case 4: gSDK->SetDefaultFPatByClass(); break;
		case 5: gSDK->SetDefaultArrowByClass(); break;
		default: gSDK->SetDefaultOpacityByClass(); break;
		}
	}

	Boolean GetDefaultAttrByClass(int index)
	{
		switch (index)
		{
		case 0: return gSDK->GetDefaultPColorsByClass();
		case 1: return gSDK->GetDefaultFColorsByClass();
		case 2: return gSDK->GetDefaultLWByClass();
		case 3: return gSDK->GetDefaultPPatByClass();
		case 4: return gSDK->GetDefaultFPatByClass();
		case 5: return gSDK->GetDefaultArrowByClass();
		default: return gSDK->GetDefaultOpacityByClass();
		}
	}

	// --- 測る対象のオブジェクト ------------------------------------------

	// 2 種類作る。**軽い 2D（矩形）と 3D 実体を持つもの（球）**——取り込み側の実測で
	// 「実体を持つオブジェクトほど 1 呼び出しが高い」傾向が出ていたので、その差が
	// by-class の書き込みにも出るかを見る。どちらも 1 呼び出しで作れるので、
	// 作りの違い（パス・プロファイル）が測定に混ざらない。
	enum class ObjectKind
	{
		Rectangle,
		Sphere
	};

	const char* KindName(ObjectKind kind)
	{
		return kind == ObjectKind::Rectangle ? "矩形 (2D)" : "球 (3D 実体)";
	}

	MCObjectHandle CreateOne(ObjectKind kind, int serial)
	{
		const WorldCoord step = 1000;
		const WorldCoord x = static_cast<WorldCoord>(serial) * step;
		if (kind == ObjectKind::Rectangle)
		{
			WorldRect bounds(x, step, x + step, 0);
			return gSDK->CreateRectangle(bounds);
		}
		WorldPt3 center(x, 0, 0);
		return gSDK->CreateSphere(center, step / 2);
	}

	// 1 つの群（1 種類のオブジェクト）を測った結果。
	struct GroupResult
	{
		double createMs = 0;
		double classMs = 0;
		double firstPassMs[kProbeAttrCount] = {};
		double secondPassMs[kProbeAttrCount] = {};
		Boolean atBirth[kProbeAttrCount] = {};
		Boolean afterSetClass[kProbeAttrCount] = {};
		Boolean afterFirstPass[kProbeAttrCount] = {};
		int made = 0;
	};

	// 見出し行 + 1 行 1 属性の表を書く。
	void LogAttrTable(vwprobe::Report& probe, const GroupResult& result)
	{
		probe.log("| 属性 | 生成直後 | SetObjectClass 後 | 1 度目 | 書込後 | 2 度目 |");
		probe.log("| --- | --- | --- | ---: | --- | ---: |");
		for (int i = 0; i < kProbeAttrCount; ++i)
		{
			std::string row = "| ";
			row += kProbeAttrNames[i];
			row += " | ";
			row += YesNo(result.atBirth[i]);
			row += " | ";
			row += YesNo(result.afterSetClass[i]);
			row += " | ";
			row += FormatMs(result.firstPassMs[i] / result.made);
			row += "ms | ";
			row += YesNo(result.afterFirstPass[i]);
			row += " | ";
			row += FormatMs(result.secondPassMs[i] / result.made);
			row += "ms |";
			probe.log(row);
		}
	}
} // namespace

VW_PROBE("byclass-attr-cost", "by-class 属性の書き込み費用を実測する",
		 "7 つの Set*ByClass の費用が『状態の変化』に掛かるのかを 2 度書きで切り分け、"
		 "文書の既定（SetDefault*ByClass）で per-object の書き込みを省けるかを確かめる")
{
	// =====================================================================
	// 0. 文書の既定を、何も触る前に読む。
	//    **`SetArrowByClass` だけ無料なのはなぜか**の答えがここに出る見込み
	//    （マーカーだけ既定で by-class なら、1 度目から「変わらない書き込み」になる）。
	// =====================================================================
	probe.log("## 0. 触る前の文書の既定（SetDefault*ByClass の読み戻し）");
	probe.log("");
	probe.log("| 属性 | 既定で by-class か |");
	probe.log("| --- | --- |");
	for (int i = 0; i < kProbeAttrCount; ++i)
	{
		std::string row = "| ";
		row += kProbeAttrNames[i];
		row += " | ";
		row += YesNo(GetDefaultAttrByClass(i));
		row += " |";
		probe.log(row);
	}
	const InternalIndex defaultClassAtStart = gSDK->GetDefaultClass();
	probe.log("");
	probe.log(std::string("GetDefaultClass() = ") + std::to_string(defaultClassAtStart));
	probe.log("");

	// 測定用のクラスを 2 つ作る。1 つは per-object で与える用、
	// もう 1 つは `SetDefaultClass` で「生まれつき」与える用。
	const InternalIndex perObjectClass = gSDK->AddClass("VwProbe-94-個別");
	const InternalIndex bornWithClass = gSDK->AddClass("VwProbe-94-既定");
	probe.log(std::string("AddClass: 個別用 = ") + std::to_string(perObjectClass) +
			  " / 既定用 = " + std::to_string(bornWithClass));
	if (perObjectClass == 0 || bornWithClass == 0)
	{
		probe.fail("AddClass がクラスを作れなかった（索引 0）");
		return;
	}
	probe.log("");

	const ObjectKind kinds[2] = {ObjectKind::Rectangle, ObjectKind::Sphere};

	// =====================================================================
	// 1. いまの作り（per-object で 8 つ）を測る。
	//    ついでに 2 度目の同じ書き込みを測り、**費用が「変化」に掛かるのか
	//    「書き込み」に掛かるのか**を切り分ける。
	// =====================================================================
	probe.log("## 1. いまの作り: SetObjectClass ＋ 7 つを per-object で");
	probe.log("");
	probe.log(std::string("1 種類あたり ") + std::to_string(kProbeObjectCount) +
			  " 個。数値は 1 呼び出しあたりの平均。");
	probe.log("");

	for (int k = 0; k < 2; ++k)
	{
		GroupResult result;
		MCObjectHandle objects[kProbeObjectCount] = {};

		// --- 作る ---
		ProbeClock::time_point mark = ProbeClock::now();
		for (int i = 0; i < kProbeObjectCount; ++i)
			objects[i] = CreateOne(kinds[k], i);
		result.createMs = MsBetween(mark, ProbeClock::now());

		for (int i = 0; i < kProbeObjectCount; ++i)
			if (objects[i] != nil)
				++result.made;
		if (result.made == 0)
		{
			probe.fail(std::string("オブジェクトを 1 つも作れなかった: ") + KindName(kinds[k]));
			continue;
		}

		// --- 生成直後の by-class 状態（文書の既定がそのまま出るはず） ---
		for (int a = 0; a < kProbeAttrCount; ++a)
			result.atBirth[a] = GetAttrByClass(a, objects[0]);

		// --- SetObjectClass だけを与える（(3) の前提の確認） ---
		mark = ProbeClock::now();
		for (int i = 0; i < kProbeObjectCount; ++i)
			if (objects[i] != nil)
				gSDK->SetObjectClass(objects[i], perObjectClass);
		result.classMs = MsBetween(mark, ProbeClock::now());

		for (int a = 0; a < kProbeAttrCount; ++a)
			result.afterSetClass[a] = GetAttrByClass(a, objects[0]);

		// --- 7 つを 1 度目 ---
		for (int a = 0; a < kProbeAttrCount; ++a)
		{
			mark = ProbeClock::now();
			for (int i = 0; i < kProbeObjectCount; ++i)
				if (objects[i] != nil)
					SetAttrByClass(a, objects[i]);
			result.firstPassMs[a] = MsBetween(mark, ProbeClock::now());
		}

		for (int a = 0; a < kProbeAttrCount; ++a)
			result.afterFirstPass[a] = GetAttrByClass(a, objects[0]);

		// --- 7 つを 2 度目（**状態は既に by-class なので、変わらない書き込み**） ---
		for (int a = 0; a < kProbeAttrCount; ++a)
		{
			mark = ProbeClock::now();
			for (int i = 0; i < kProbeObjectCount; ++i)
				if (objects[i] != nil)
					SetAttrByClass(a, objects[i]);
			result.secondPassMs[a] = MsBetween(mark, ProbeClock::now());
		}

		probe.log(std::string("### ") + KindName(kinds[k]) + "（" + std::to_string(result.made) +
				  " 個）");
		probe.log("");
		probe.log(std::string("作る: ") + FormatMs(result.createMs / result.made) +
				  "ms/個 ／ SetObjectClass: " + FormatMs(result.classMs / result.made) + "ms/回");
		probe.log("");
		LogAttrTable(probe, result);
		probe.log("");
	}

	// =====================================================================
	// 2. 文書の既定を立ててから作る（(2)）。
	//    **per-object の書き込みを 1 つも呼ばずに** by-class になるか。
	// =====================================================================
	probe.log("## 2. 文書の既定を先に立て、per-object の書き込みを 1 つも呼ばずに作る");
	probe.log("");

	ProbeClock::time_point defaultsMark = ProbeClock::now();
	gSDK->SetDefaultClass(bornWithClass);
	for (int a = 0; a < kProbeAttrCount; ++a)
		SetDefaultAttrByClass(a);
	const double defaultsMs = MsBetween(defaultsMark, ProbeClock::now());

	probe.log(std::string("既定を立てるのに掛かった時間（8 回ぶん合計）: ") + FormatMs(defaultsMs) +
			  "ms");
	probe.log("");
	probe.log("| 属性 | 既定を立てた後の GetDefault*ByClass |");
	probe.log("| --- | --- |");
	for (int a = 0; a < kProbeAttrCount; ++a)
	{
		std::string row = "| ";
		row += kProbeAttrNames[a];
		row += " | ";
		row += YesNo(GetDefaultAttrByClass(a));
		row += " |";
		probe.log(row);
	}
	probe.log(std::string("GetDefaultClass() = ") + std::to_string(gSDK->GetDefaultClass()) +
			  "（立てた値は " + std::to_string(bornWithClass) + "）");
	probe.log("");

	for (int k = 0; k < 2; ++k)
	{
		MCObjectHandle objects[kProbeObjectCount] = {};

		ProbeClock::time_point mark = ProbeClock::now();
		for (int i = 0; i < kProbeObjectCount; ++i)
			objects[i] = CreateOne(kinds[k], i + kProbeObjectCount);
		const double createMs = MsBetween(mark, ProbeClock::now());

		int made = 0;
		for (int i = 0; i < kProbeObjectCount; ++i)
			if (objects[i] != nil)
				++made;
		if (made == 0)
		{
			probe.fail(std::string("既定つきでオブジェクトを作れなかった: ") +
					   KindName(kinds[k]));
			continue;
		}

		// **全件**を数える（1 個目だけ見て判断しない）。
		int inClass = 0;
		int fullyByClass[kProbeAttrCount] = {};
		for (int i = 0; i < kProbeObjectCount; ++i)
		{
			if (objects[i] == nil)
				continue;
			if (gSDK->GetObjectClass(objects[i]) == bornWithClass)
				++inClass;
			for (int a = 0; a < kProbeAttrCount; ++a)
				if (GetAttrByClass(a, objects[i]) != 0)
					++fullyByClass[a];
		}

		probe.log(std::string("### ") + KindName(kinds[k]) + "（" + std::to_string(made) + " 個）");
		probe.log("");
		probe.log(std::string("作る: ") + FormatMs(createMs / made) + "ms/個");
		probe.log(std::string("既定のクラスで生まれた数: ") + std::to_string(inClass) + " / " +
				  std::to_string(made));
		probe.log("");
		probe.log("| 属性 | 生まれつき by-class だった数 | 省いた per-object の書き込みを今から呼ぶと |");
		probe.log("| --- | ---: | ---: |");
		for (int a = 0; a < kProbeAttrCount; ++a)
		{
			// 既に by-class のはずのものへ改めて書く——**無料なら「変わらない書き込みは
			// 無料」が確定**し、1 の 2 度目の結果と合わせて機構が決まる。
			mark = ProbeClock::now();
			for (int i = 0; i < kProbeObjectCount; ++i)
				if (objects[i] != nil)
					SetAttrByClass(a, objects[i]);
			const double redundantMs = MsBetween(mark, ProbeClock::now());

			std::string row = "| ";
			row += kProbeAttrNames[a];
			row += " | ";
			row += std::to_string(fullyByClass[a]);
			row += " / ";
			row += std::to_string(made);
			row += " | ";
			row += FormatMs(redundantMs / made);
			row += "ms |";
			probe.log(row);
		}
		probe.log("");
	}

	probe.log("## 読み方");
	probe.log("");
	probe.log("- 0 の表で Arrow だけ yes なら、`SetArrowByClass` が無料なのは"
			  "「既に by-class ＝ 変わらない書き込み」だからである。");
	probe.log("- 1 の表で「2 度目」が 1 度目よりずっと安いなら、費用は"
			  "**書き込みそのものではなく状態の変化**に掛かっている。");
	probe.log("- 2 で「生まれつき by-class だった数」が全件なら、**文書の既定を先に立てれば"
			  "per-object の 7 つは要らない**。");
}
