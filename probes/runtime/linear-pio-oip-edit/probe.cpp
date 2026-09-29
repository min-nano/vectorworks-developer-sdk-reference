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
//	  ① 1 回目: 線分 PIO を 1 本作り、読み取った値の**指紋（2 つのハッシュ）を
//	     その図形の名前へ書き込んで**残す。値そのものはログに出る。
//	  ② 利用者が OIP でその PIO のパラメータを 1 つ編集する。
//	  ③ 2 回目: 名前に残した指紋と、いま計算した指紋を突き合わせる。
//	     幾何の指紋が一致＝「VW は行列も長さも触っていない」。
//
//	パラメータの指紋も別に持つので、**編集せずに 2 回目を走らせた場合はそれが分かる**
//	（「まだ編集されていない」と出る）。目視も転記も要らない。
//
//	**名前へ値そのものを書かないのは、1 巡目でそれを踏んだため**——`SetObjectName` は
//	`0`（成功）を返しながら**63 文字で黙って切る**。だから名前に載せるのは短い指紋だけに
//	して、長さも読み戻して確かめる。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	// 1 巡目で実在が確かめられた線分 PIO（LineLength を持つ）。
	const char* const kProbeOipCandidateNames[] = {
		"Break Line",
		"Joist",
		"Clothes Rod",
	};

	const char* const kProbeOipMarker = "VWPROBE181";

	// 図形の名前へ書ける長さの上限（1 巡目の実測。これを越えると黙って切られる）。
	const size_t kProbeOipNameLimit = 63;

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

	// 32bit FNV-1a。短く畳んで名前へ載せるためだけに使う。
	std::string ProbeOipHash(const std::string& text)
	{
		Uint32 hash = 2166136261u;
		for (size_t i = 0; i < text.size(); ++i)
		{
			hash ^= static_cast<Uint32>(static_cast<unsigned char>(text[i]));
			hash *= 16777619u;
		}
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%08x", static_cast<unsigned>(hash));
		return std::string(buffer);
	}

	// 全パラメータの値を 1 本の文字列に畳む。値が 1 つでも変わればここが変わるので、
	// 「利用者が本当に編集したか」を機械で確かめられる。
	std::string ProbeOipParamText(const VWParametricObj& pio)
	{
		std::string text;
		const size_t count = pio.GetParamsCount();
		for (size_t i = 0; i < count; ++i)
		{
			const TXString value = pio.GetParamValue(i);
			const char* const utf8 = static_cast<const char*>(value);
			text += (utf8 != nullptr) ? utf8 : "";
			text += '\x1f';
		}
		return text;
	}

	// 行列・長さ・両端・外接を 1 本の文字列にする。**この文字列の一致をもって
	// 「変わっていない」と言う**ので、読み取り経路はここ 1 箇所に閉じておく。
	std::string ProbeOipGeomText(MCObjectHandle h)
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
			   ";bh=" + ProbeOipNum(bounds.top - bounds.bottom);
	}

	// 名前に載せる指紋。`VWPROBE181 g=xxxxxxxx p=xxxxxxxx` で 31 文字（上限 63 に収まる）。
	std::string ProbeOipStampName(MCObjectHandle h)
	{
		const VWParametricObj pio(h);
		return std::string(kProbeOipMarker) + " g=" + ProbeOipHash(ProbeOipGeomText(h)) +
			   " p=" + ProbeOipHash(ProbeOipParamText(pio));
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
			const char* const utf8 = static_cast<const char*>(name);
			const std::string asStd((utf8 != nullptr) ? utf8 : "");
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
		probe.log("  1 回目の指紋（図形の名前）= " + storedName);

		const std::string nowName = ProbeOipStampName(found);
		probe.log("  2 回目の指紋（いま計算）  = " + nowName);
		probe.log("  2 回目の値 = " + ProbeOipGeomText(found));

		// 指紋は "VWPROBE181 g=xxxxxxxx p=xxxxxxxx"。位置で切り出す。
		const size_t gPos = storedName.find(" g=");
		const size_t pPos = storedName.find(" p=");
		const size_t gNow = nowName.find(" g=");
		const size_t pNow = nowName.find(" p=");
		if (gPos == std::string::npos || pPos == std::string::npos || gNow == std::string::npos ||
			pNow == std::string::npos)
		{
			probe.fail("指紋の形が読めなかった（名前が書き換わっている）");
			return;
		}

		const std::string geomBefore = storedName.substr(gPos + 3, 8);
		const std::string paramBefore = storedName.substr(pPos + 3, 8);
		const std::string geomAfter = nowName.substr(gNow + 3, 8);
		const std::string paramAfter = nowName.substr(pNow + 3, 8);

		if (paramBefore == paramAfter)
			probe.log("  → **パラメータは 1 つも変わっていない**。OIP での編集がまだなら、"
					  "OIP でこの PIO のパラメータを 1 つ変えてから、もう一度このプローブを"
					  "走らせてください");
		else
			probe.log("  → パラメータは変わっている（OIP の編集が入っている）");

		if (geomBefore == geomAfter)
			probe.log("  → 幾何（行列・長さ・両端・外接）は**まったく同じ**"
					  "＝ VW は OIP の編集で PIO の行列も長さも触っていない");
		else
			probe.log("  → 幾何が**変わっている**＝ VW は OIP の編集で PIO の行列か長さを"
					  "触っている（1 回目の値は前のコメントのログにある）");

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
		EVSPluginType pluginType = kVSPluginMenu;
		const Boolean isPlugin = gSDK->GetPluginType(name, pluginType);
		probe.log(std::string("  候補 \"") + name +
				  "\": GetPluginType=" + (isPlugin ? "true" : "false") +
				  " 種別=" + ProbeOipInt(static_cast<long>(pluginType)) + "（2=オブジェクト）");
		if (!isPlugin || pluginType != kVSPluginObject)
		{
			probe.log("    → この綴りのオブジェクトプラグインは無い。飛ばす");
			continue;
		}
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
		if (pio.GetParamIndex("LineLength") == static_cast<size_t>(-1))
		{
			probe.log("    → LineLength を持っていない（線分ではない）。消して次へ");
			gSDK->DeleteObject(h, false);
			continue;
		}
		probe.log("    → 線分 PIO を作れた（パラメータ数=" +
				  ProbeOipInt(static_cast<long>(pio.GetParamsCount())) + "）");
		subject = h;
		subjectName = name;
		break;
	}

	if (subject == nil)
	{
		probe.fail("線分 PIO を 1 つも作れなかった——組み込み PIO の綴りを変えて出し直す");
		return;
	}

	probe.log("被験体 = \"" + subjectName + "\"（原点 (10000, 5000)・30°）");

	{
		VWParametricObj pio(subject);
		probe.log("  SetParamReal(\"LineLength\", 4321) を書いてから ResetObject する");
		pio.SetParamReal("LineLength", 4321.0);
	}
	gSDK->ResetObject(subject);

	// **値はログへ、指紋だけを名前へ。** 名前は 63 文字で黙って切られるため。
	probe.log("  1 回目の値 = " + ProbeOipGeomText(subject));
	const std::string name = ProbeOipStampName(subject);
	const GSError err = gSDK->SetObjectName(subject, name);
	probe.log("  1 回目の指紋 = " + name + "（" + ProbeOipInt(static_cast<long>(name.size())) +
			  " 文字 / 上限 " + ProbeOipInt(static_cast<long>(kProbeOipNameLimit)) +
			  "）SetObjectName の戻り=" + ProbeOipInt(static_cast<long>(err)));

	{
		TXString readBack;
		gSDK->GetObjectName(subject, readBack);
		const char* const utf8 = static_cast<const char*>(readBack);
		const std::string asStd((utf8 != nullptr) ? utf8 : "");
		probe.log("  名前の読み戻し = " + asStd + "（" +
				  ProbeOipInt(static_cast<long>(asStd.size())) + " 文字）");
		if (asStd != name)
			probe.fail("図形の名前が書き込めなかった——2 回目の突き合わせができない");
	}

	probe.log("---");
	probe.log("次の手順: この PIO を選び、OIP でパラメータを 1 つ編集してから、"
			  "**このプローブをもう一度**走らせてください（結果はまた自動で返ります）");
}
