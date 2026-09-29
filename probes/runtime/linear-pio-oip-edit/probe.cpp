//
//	probes/runtime/linear-pio-oip-edit/probe.cpp
//
//	[issue #181] 線分 PIO を利用者が OIP（オブジェクト情報パレット）で編集したとき、
//	VectorWorks が PIO の**行列・長さ・外接**を変えるかを実測する。
//
//	OIP の編集は SDK から起こせない（`SetParamReal` ＋ `ResetObject` で真似ると、
//	まさに調べたい「VW が編集のときに横から何かするか」を迂回してしまう）。そこで
//	**2 段**で測る。同じプローブを 2 回走らせるだけでよい。
//
//	  ① 1 回目: 線分 PIO を 1 本作り、読み取った値（行列・長さ・外接・全パラメータの
//	     ハッシュ）を**その図形の名前へ書き込んで**残す。
//	  ② 利用者が OIP でその PIO のパラメータを 1 つ編集する。
//	  ③ 2 回目: 名前に残した値と、いま読み取った値を**文字列として突き合わせる**。
//	     一致＝「VW は行列も長さも触っていない」。相違＝触っている（どこが変わったかも出る）。
//
//	パラメータのハッシュも突き合わせるので、**編集せずに 2 回目を走らせた場合はそれが分かる**
//	（「まだ編集されていない」と出る）。目視も転記も要らない。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	// 被験体の候補（linear-pio-endpoints と同じ綴りの並び）。LineLength を持つ最初の
	// ものを選ぶ。1 つも持たなければ、定義が引けた最初のものを使う。
	const char* const kProbeOipCandidateNames[] = {
		"Break Line",		 "BreakLine",	  "Straight Truss",	 "Straight Guardrail",
		"Straight Handrail", "Joist",		  "Framing Member",	 "Clothes Rod",
		"Grab Bars",		 "Scale Bar",	  "Leader Line",	 "Center Line Marker",
		"Linear Material",	 "Lighting Pipe", "Irrigation Line", "Simple Ramp",
		"StructuralMember",
	};

	const char* const kProbeOipMarker = "VWPROBE181";

	std::string ProbeOipNum(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.3f", value);
		return std::string(buffer);
	}

	std::string ProbeOipInt(long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%ld", value);
		return std::string(buffer);
	}

	// 全パラメータの値を 1 本の文字列に畳んだ 32bit FNV-1a。**値が 1 つでも変わればここが
	// 変わる**ので、「利用者が本当に編集したか」を機械で確かめられる。
	std::string ProbeOipParamHash(const VWParametricObj& pio)
	{
		Uint32 hash = 2166136261u;
		const size_t count = pio.GetParamsCount();
		for (size_t i = 0; i < count; ++i)
		{
			const TXString value = pio.GetParamValue(i);
			const char* p = static_cast<const char*>(value);
			for (; p != nullptr && *p != '\0'; ++p)
			{
				hash ^= static_cast<Uint32>(static_cast<unsigned char>(*p));
				hash *= 16777619u;
			}
			hash ^= 0x1fu;
			hash *= 16777619u;
		}
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%08x", static_cast<unsigned>(hash));
		return std::string(buffer);
	}

	// 行列・長さ・外接・パラメータハッシュを 1 本の文字列にする。**この文字列の一致を
	// もって「変わっていない」と言う**ので、読み取り経路はここ 1 箇所に閉じておく。
	std::string ProbeOipStamp(MCObjectHandle h)
	{
		const VWParametricObj pio(h);

		TransformMatrix matrix;
		gSDK->GetEntityMatrix(h, matrix);

		WorldRect bounds;
		if (!gSDK->GetObjectBounds(h, bounds))
		{
			bounds.left = bounds.right = 0;
			bounds.top = bounds.bottom = 0;
		}

		VWPoint2D ptA;
		VWPoint2D ptB;
		pio.GetLinearObjectPos(ptA, ptB);

		const bool hasLen = pio.GetParamIndex("LineLength") != static_cast<size_t>(-1);

		return "off=" + ProbeOipNum(matrix.v1.xOff) + "," + ProbeOipNum(matrix.v1.yOff) +
			   ";i=" + ProbeOipNum(matrix.v1.a00) + "," + ProbeOipNum(matrix.v1.a01) +
			   ";len=" + (hasLen ? ProbeOipNum(pio.GetParamReal("LineLength")) : std::string("-")) +
			   ";ep=" + ProbeOipNum(ptA.x) + "," + ProbeOipNum(ptA.y) + "," + ProbeOipNum(ptB.x) +
			   "," + ProbeOipNum(ptB.y) + ";bw=" + ProbeOipNum(bounds.right - bounds.left) +
			   ";bh=" + ProbeOipNum(bounds.top - bounds.bottom) + ";ph=" + ProbeOipParamHash(pio);
	}

	// アクティブレイヤから、名前が目印で始まる図形を探す。
	MCObjectHandle ProbeOipFindMarked(std::string& outName)
	{
		const MCObjectHandle layer = gSDK->GetActiveLayer();
		if (layer == nil)
			return nil;
		const std::string marker(kProbeOipMarker);
		for (MCObjectHandle m = gSDK->FirstMemberObj(layer); m != nil; m = gSDK->NextObject(m))
		{
			if (gSDK->GetObjectTypeN(m) == 0) // kTermNode（walk の終端）
				break;
			TXString name;
			gSDK->GetObjectName(m, name);
			const std::string asStd(static_cast<const char*>(name));
			if (asStd.compare(0, marker.size(), marker) == 0)
			{
				outName = asStd;
				return m;
			}
		}
		return nil;
	}
} // namespace

VW_PROBE("linear-pio-oip-edit", "OIP 編集で線分 PIO の行列・長さが変わるか",
		 "1 回目で 1 本置き、OIP で編集した後の 2 回目で読み戻して突き合わせる")
{
	// ------------------------------------------------------------------ 2 回目
	std::string storedName;
	const MCObjectHandle found = ProbeOipFindMarked(storedName);
	if (found != nil)
	{
		probe.log("目印の付いた PIO が見つかった——**2 回目**として読み戻す");
		probe.log("  図形の名前（1 回目に書き込んだ値）= " + storedName);

		const size_t sep = storedName.find(' ');
		const std::string before =
			(sep == std::string::npos) ? std::string() : storedName.substr(sep + 1);
		const std::string after = ProbeOipStamp(found);

		probe.log("  1 回目 = " + before);
		probe.log("  2 回目 = " + after);

		if (before.empty())
		{
			probe.fail("1 回目の値が名前から読めなかった（名前が書き換わっている）");
			return;
		}

		// 「本当に編集されたか」をパラメータハッシュで確かめる。
		const size_t phBefore = before.rfind(";ph=");
		const size_t phAfter = after.rfind(";ph=");
		const bool paramsChanged =
			(phBefore != std::string::npos && phAfter != std::string::npos) &&
			(before.substr(phBefore) != after.substr(phAfter));

		if (!paramsChanged)
			probe.log("  → **パラメータは 1 つも変わっていない**。OIP での編集がまだなら、"
					  "OIP でこの PIO のパラメータを 1 つ変えてから、もう一度このプローブを"
					  "走らせてください");
		else
			probe.log("  → パラメータは変わっている（OIP の編集が入っている）");

		const std::string beforeGeom =
			(phBefore == std::string::npos) ? before : before.substr(0, phBefore);
		const std::string afterGeom =
			(phAfter == std::string::npos) ? after : after.substr(0, phAfter);

		if (beforeGeom == afterGeom)
			probe.log("  → 幾何（行列・長さ・両端・外接）は**ビット表記まで一致**"
					  "＝ VW は OIP の編集で PIO の行列も長さも触っていない");
		else
			probe.log("  → 幾何が**変わっている**＝ VW は OIP の編集で PIO の行列か長さを"
					  "触っている（上の 2 行を項目ごとに読む）");

		// 目印を外して、次の走行が「1 回目」から始められるようにする。
		gSDK->SetObjectName(found, "");
		probe.log("  目印を外した（次に走らせると 1 回目からやり直す）");
		return;
	}

	// ------------------------------------------------------------------ 1 回目
	probe.log("目印の付いた PIO が無い——**1 回目**として 1 本作る");

	MCObjectHandle subject = nil;
	std::string subjectName;
	const size_t candidateCount =
		sizeof(kProbeOipCandidateNames) / sizeof(kProbeOipCandidateNames[0]);
	for (size_t i = 0; i < candidateCount; ++i)
	{
		const char* const name = kProbeOipCandidateNames[i];
		probe.log(std::string("  候補 \"") + name + "\" を引く");
		if (gSDK->DefineCustomObject(name, kCustomObjectPrefNever) == nil)
		{
			probe.log("    → 定義が引けなかった。飛ばす");
			continue;
		}
		const MCObjectHandle h = gSDK->CreateCustomObject(name, WorldPt(10000, 5000), 30.0, true);
		if (h == nil)
		{
			probe.log("    → CreateCustomObject が nil。飛ばす");
			continue;
		}
		const VWParametricObj pio(h);
		const bool hasLen = pio.GetParamIndex("LineLength") != static_cast<size_t>(-1);
		probe.log(std::string("    → 作れた。LineLength は") + (hasLen ? "**在る**" : "無い") +
				  "（パラメータ数=" + ProbeOipInt(static_cast<long>(pio.GetParamsCount())) + "）");
		if (subject == nil || hasLen)
		{
			// 先に作った仮の被験体は消して、LineLength を持つほうを採る。
			if (subject != nil && subject != h)
				gSDK->DeleteObject(subject, false);
			subject = h;
			subjectName = name;
			if (hasLen)
				break;
		}
		else
		{
			gSDK->DeleteObject(h, false);
		}
	}

	if (subject == nil)
	{
		probe.fail("候補のどれも作れなかった——組み込み PIO の綴りを変えて出し直す");
		return;
	}

	probe.log("被験体 = \"" + subjectName + "\"（原点 (10000, 5000)・30°）");

	{
		VWParametricObj pio(subject);
		if (pio.GetParamIndex("LineLength") != static_cast<size_t>(-1))
		{
			probe.log("  SetParamReal(\"LineLength\", 4321) を書いてから ResetObject する");
			pio.SetParamReal("LineLength", 4321.0);
		}
	}
	gSDK->ResetObject(subject);

	const std::string stamp = ProbeOipStamp(subject);
	const std::string name = std::string(kProbeOipMarker) + " " + stamp;
	const GSError err = gSDK->SetObjectName(subject, name);
	probe.log("  読み取った値 = " + stamp);
	probe.log("  図形の名前へ書き込んだ（SetObjectName の戻り=" +
			  ProbeOipInt(static_cast<long>(err)) + "）");

	{
		TXString readBack;
		gSDK->GetObjectName(subject, readBack);
		probe.log("  名前の読み戻し = " + std::string(static_cast<const char*>(readBack)));
		if (std::string(static_cast<const char*>(readBack)) != name)
			probe.fail("図形の名前が書き込めなかった——2 回目の突き合わせができない");
	}

	probe.log("---");
	probe.log("次の手順: この PIO を選び、OIP でパラメータを 1 つ編集してから、"
			  "**このプローブをもう一度**走らせてください（結果はまた自動で返ります）");
}
