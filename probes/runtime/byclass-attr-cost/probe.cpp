//
//	probes/runtime/byclass-attr-cost/probe.cpp
//
//	[issue #94] 描画属性を「クラスに従う」にする 7 つの書き込み（SetPColorsByClass 等）の
//	費用が何で決まるのかを実測する。
//
//	**第 2 周。** 第 1 周（矩形・球で測った）の結果は次のとおりで、2 つ分かって
//	1 つ外れた:
//	  - **文書の既定（SetDefaultClass / SetDefault*ByClass）は効く。** 先に立てておくと、
//	    生まれたオブジェクトは 30/30 が既定のクラスに属し、7 属性とも 30/30 が
//	    by-class だった——per-object の書き込みを 1 つも呼ばずに。
//	  - **SetObjectClass だけでは属性は by-instance のまま**（前提は今も正しい）。
//	  - **しかし費用が再現しなかった。** 矩形も球も 7 つとも 0.00ms で、取り込み側の
//	    3.6ms が出ない。**「実体（3D ソリッド）を持つほど高い」という読みは外れ**
//	    ——球は実体を持つのに無料だった。また**マーカーだけ既定で by-class という
//	    読みも外れ**（第 1 周では 7 つとも既定は no だった）。
//
//	残った問い（issue の 1）は「**では何が 3.6ms を生んでいるのか**」で、issue の
//	内訳が答えを指している——**構造材 PIO で 10ms、データタグで 0.3ms、34 倍**。
//	効いているのは「実体を持つか」ではなく「**PIO か**」ではないか。だからこの周は
//	**構造材 PIO（StructuralMember）で測る**。矩形は対照として残す。
//
//	確かめること:
//	  (1) **PIO なら費用が再現するか。** 再現するなら、費用は「属性の書き込み」ではなく
//	      **それが引き起こす PIO の作り直し**に掛かっている。
//	  (2) **そのとき Arrow だけ安いか。** 取り込み側の非対称（6 つが 3.6ms、Arrow は
//	      0ms）が PIO で再現すれば、「どの属性が再生成を起こすか」の切り分けになる。
//	  (3) **2 度目は安いか**（費用は「変化」に掛かるのか「書き込み」に掛かるのか）。
//	      第 1 周は 1 度目から 0.00ms だったので切り分けられなかった。
//	  (4) **PIO でも文書の既定で省けるか**（第 1 周で効いたのは非 PIO だけ）。
//	  (5) 第 1 周の副産物: `SetDefaultOpacityByClass()` を呼んだのに
//	      `GetDefaultOpacityByClass()` が no を返した（なのに生まれた物は by-class）。
//	      2 旗版 `GetDefaultOpacityByClassN` で読み直して、読み戻しの穴かを確かめる。
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

	// PIO は 1 つが重い見込みなので少なめに。矩形の対照も同数にして比べられるようにする。
	const int kProbeObjectCount = 20;

	// 「クラスに従わせる」7 つ。**並びは固定**で、以下の表の行に対応する。
	const int kProbeAttrCount = 7;

	const char* const kProbeAttrNames[kProbeAttrCount] = {
		"PColors (ペン色)",	 "FColors (面色)",	 "LW (線の太さ)",	  "PPat (線種)",
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

	void SetAttrByClass(int index, MCObjectHandle object)
	{
		switch (index)
		{
		case 0:
			gSDK->SetPColorsByClass(object);
			break;
		case 1:
			gSDK->SetFColorsByClass(object);
			break;
		case 2:
			gSDK->SetLWByClass(object);
			break;
		case 3:
			gSDK->SetPPatByClass(object);
			break;
		case 4:
			gSDK->SetFPatByClass(object);
			break;
		case 5:
			gSDK->SetArrowByClass(object);
			break;
		default:
			gSDK->SetOpacityByClass(object);
			break;
		}
	}

	Boolean GetAttrByClass(int index, MCObjectHandle object)
	{
		switch (index)
		{
		case 0:
			return gSDK->GetPColorsByClass(object);
		case 1:
			return gSDK->GetFColorsByClass(object);
		case 2:
			return gSDK->GetLWByClass(object);
		case 3:
			return gSDK->GetPPatByClass(object);
		case 4:
			return gSDK->GetFPatByClass(object);
		case 5:
			return gSDK->GetArrowByClass(object);
		default:
			return gSDK->GetOpacityByClass(object);
		}
	}

	void SetDefaultAttrByClass(int index)
	{
		switch (index)
		{
		case 0:
			gSDK->SetDefaultPColorsByClass();
			break;
		case 1:
			gSDK->SetDefaultFColorsByClass();
			break;
		case 2:
			gSDK->SetDefaultLWByClass();
			break;
		case 3:
			gSDK->SetDefaultPPatByClass();
			break;
		case 4:
			gSDK->SetDefaultFPatByClass();
			break;
		case 5:
			gSDK->SetDefaultArrowByClass();
			break;
		default:
			gSDK->SetDefaultOpacityByClass();
			break;
		}
	}

	Boolean GetDefaultAttrByClass(int index)
	{
		switch (index)
		{
		case 0:
			return gSDK->GetDefaultPColorsByClass();
		case 1:
			return gSDK->GetDefaultFColorsByClass();
		case 2:
			return gSDK->GetDefaultLWByClass();
		case 3:
			return gSDK->GetDefaultPPatByClass();
		case 4:
			return gSDK->GetDefaultFPatByClass();
		case 5:
			return gSDK->GetDefaultArrowByClass();
		default:
			return gSDK->GetDefaultOpacityByClass();
		}
	}

	// --- 測る対象 --------------------------------------------------------
	//
	// **構造材 PIO**（取り込み側で 10ms/呼び出しが出ていた当のもの）と、対照の矩形。
	// PIO は鉛直材の作法で作る——`CreateNurbsCurve` ＋ `Add3DVertex` で下端 → 上端の
	// 2 点を渡す（[Findings「Parametric Objects」](Findings/Parametric%20Objects.md)）。

	enum class ObjectKind
	{
		StructuralMemberPio,
		Rectangle
	};

	const char* KindName(ObjectKind kind)
	{
		return kind == ObjectKind::StructuralMemberPio ? "構造材 PIO (StructuralMember)"
													   : "矩形 (2D・対照)";
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

		// 鉛直材: 下端 → 上端の 2 点を持つ NURBS をパスにする。
		MCObjectHandle path = gSDK->CreateNurbsCurve(WorldPt3(x, 0, 0), true, 3);
		if (path == nil)
			return nil;
		gSDK->Add3DVertex(path, WorldPt3(x, 0, 3000));
		return gSDK->CreateCustomObjectPath("StructuralMember", path, nil, true);
	}

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

VW_PROBE("byclass-attr-cost", "by-class 属性の書き込み費用を PIO で実測する",
		 "第 2 周。構造材 PIO で 3.6ms が再現するか・Arrow だけ安いか・2 度目は安いか・"
		 "PIO でも文書の既定で省けるかを確かめる（第 1 周の矩形／球では全部 0.00ms だった）")
{
	// =====================================================================
	// 0. 触る前の文書の既定。
	// =====================================================================
	probe.log("## 0. 触る前の文書の既定");
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
	{
		// 第 1 周で SetDefaultOpacityByClass() の読み戻しだけが食い違った。
		// 2 旗版で読み直す（不透明度はペンと面で 2 旗ある）。
		Boolean penByClass = 0;
		Boolean fillByClass = 0;
		gSDK->GetDefaultOpacityByClassN(penByClass, fillByClass);
		probe.log(std::string("GetDefaultOpacityByClassN: pen=") + YesNo(penByClass) +
				  " fill=" + YesNo(fillByClass));
	}
	probe.log(std::string("GetDefaultClass() = ") + std::to_string(gSDK->GetDefaultClass()));
	probe.log("");

	const InternalIndex perObjectClass = gSDK->AddClass("VwProbe-94-個別");
	const InternalIndex bornWithClass = gSDK->AddClass("VwProbe-94-既定");
	if (perObjectClass == 0 || bornWithClass == 0)
	{
		probe.fail("AddClass がクラスを作れなかった（索引 0）");
		return;
	}

	const ObjectKind kinds[2] = {ObjectKind::StructuralMemberPio, ObjectKind::Rectangle};

	// =====================================================================
	// 1. いまの作り（per-object で 8 つ）。**PIO で費用が再現するかが山場。**
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

		ProbeClock::time_point mark = ProbeClock::now();
		for (int i = 0; i < kProbeObjectCount; ++i)
			objects[i] = CreateOne(kinds[k], i);
		result.createMs = MsBetween(mark, ProbeClock::now());

		for (int i = 0; i < kProbeObjectCount; ++i)
			if (objects[i] != nil)
				++result.made;

		probe.log(std::string("### ") + KindName(kinds[k]) + "（" + std::to_string(result.made) +
				  " / " + std::to_string(kProbeObjectCount) + " 個できた）");
		probe.log("");
		if (result.made == 0)
		{
			// PIO が作れなくても矩形の対照は続ける（fail は処理を止めない）。
			probe.fail(std::string("オブジェクトを 1 つも作れなかった: ") + KindName(kinds[k]));
			probe.log("**作れなかったので、この群は測れていない。**");
			probe.log("");
			continue;
		}

		for (int a = 0; a < kProbeAttrCount; ++a)
			result.atBirth[a] = GetAttrByClass(a, objects[0]);

		mark = ProbeClock::now();
		for (int i = 0; i < kProbeObjectCount; ++i)
			if (objects[i] != nil)
				gSDK->SetObjectClass(objects[i], perObjectClass);
		result.classMs = MsBetween(mark, ProbeClock::now());

		for (int a = 0; a < kProbeAttrCount; ++a)
			result.afterSetClass[a] = GetAttrByClass(a, objects[0]);

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

		// 2 度目: 状態は既に by-class なので「変わらない書き込み」になる。
		for (int a = 0; a < kProbeAttrCount; ++a)
		{
			mark = ProbeClock::now();
			for (int i = 0; i < kProbeObjectCount; ++i)
				if (objects[i] != nil)
					SetAttrByClass(a, objects[i]);
			result.secondPassMs[a] = MsBetween(mark, ProbeClock::now());
		}

		probe.log(std::string("作る: ") + FormatMs(result.createMs / result.made) +
				  "ms/個 ／ SetObjectClass: " + FormatMs(result.classMs / result.made) + "ms/回");
		probe.log("");
		LogAttrTable(probe, result);
		probe.log("");

		// 比較の軸: 6 つの平均と Arrow の差（取り込み側の非対称が再現したか）。
		double sixSum = 0;
		for (int a = 0; a < kProbeAttrCount; ++a)
			if (a != 5)
				sixSum += result.firstPassMs[a];
		probe.log(std::string("Arrow 以外 6 つの平均: ") + FormatMs(sixSum / 6.0 / result.made) +
				  "ms/回 ／ Arrow: " + FormatMs(result.firstPassMs[5] / result.made) + "ms/回");
		probe.log("");
	}

	// =====================================================================
	// 2. 文書の既定を先に立てて作る。**PIO でも省けるか。**
	// =====================================================================
	probe.log("## 2. 文書の既定を先に立て、per-object の書き込みを 1 つも呼ばずに作る");
	probe.log("");

	gSDK->SetDefaultClass(bornWithClass);
	for (int a = 0; a < kProbeAttrCount; ++a)
		SetDefaultAttrByClass(a);

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
	{
		Boolean penByClass = 0;
		Boolean fillByClass = 0;
		gSDK->GetDefaultOpacityByClassN(penByClass, fillByClass);
		probe.log(std::string("GetDefaultOpacityByClassN: pen=") + YesNo(penByClass) +
				  " fill=" + YesNo(fillByClass));
	}
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

		probe.log(std::string("### ") + KindName(kinds[k]) + "（" + std::to_string(made) + " / " +
				  std::to_string(kProbeObjectCount) + " 個できた）");
		probe.log("");
		if (made == 0)
		{
			probe.fail(std::string("既定つきでオブジェクトを作れなかった: ") + KindName(kinds[k]));
			probe.log("**作れなかったので、この群は測れていない。**");
			probe.log("");
			continue;
		}

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

		probe.log(std::string("作る: ") + FormatMs(createMs / made) + "ms/個");
		probe.log(std::string("既定のクラスで生まれた数: ") + std::to_string(inClass) + " / " +
				  std::to_string(made));
		probe.log("");
		probe.log("| 属性 | 生まれつき by-class だった数 | 省いた書き込みを今から呼ぶと |");
		probe.log("| --- | ---: | ---: |");
		for (int a = 0; a < kProbeAttrCount; ++a)
		{
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
	probe.log("- 1 の構造材 PIO で 6 つが高く Arrow が安ければ、**費用は属性の書き込みではなく"
			  "それが起こす PIO の作り直し**で、取り込み側の非対称もそれで説明が付く。");
	probe.log("- PIO でも 7 つとも 0.00ms なら、**費用はオブジェクトの種類ではなく"
			  "取り込み時の文書の状態**（規模・スタイル・階など）から来ている。");
	probe.log("- 1 で「2 度目」が 1 度目よりずっと安ければ、費用は**状態の変化**に掛かっている。");
	probe.log("- 2 で PIO も全件 by-class なら、**PIO でも per-object の 8 つは要らない**。");
}
