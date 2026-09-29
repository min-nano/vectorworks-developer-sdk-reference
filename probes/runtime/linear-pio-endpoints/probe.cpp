//
//	probes/runtime/linear-pio-endpoints/probe.cpp
//
//	[issue #181] 線分 PIO（kParametricSubType_Linear）の両端・長さを SDK から与える／読む
//	口を実測する。
//
//	1 巡目（build 11807dd3f139）で次まで決まった。**`LineLength` を持つ PIO では
//	`Set/GetLinearObjectPos` が効き、持たない PIO では `Get` が `(0,0), (0,0)` を返して
//	`Set` は完全な no-op**（被験体 10 件が 3 対 7 にきれいに割れた）。
//
//	この巡で残りを取る: **始端を別の点にしたとき、行列の offset は動くのか。**
//	1 巡目は始端をいつも「いまの offset」と同じ点にしていたので、そこが未確認だった。
//	被験体は 1 巡目で実在が確かめられた綴りだけに絞る（線分 2 件＋対照 1 件）。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	// 1 巡目で実在（GetPluginType=true / 種別 2）が確かめられた綴りだけ。
	// 先頭 2 件は LineLength を持つ（線分）、末尾 1 件は持たない（対照）。
	const char* const kProbeCandidateNames[] = {
		"Break Line",
		"Joist",
		"Grab Bars",
	};

	const short kProbeOvParametricInternalID = 1165;

	std::string ProbeNum(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.3f", value);
		return std::string(buffer);
	}

	std::string ProbeInt(long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%ld", value);
		return std::string(buffer);
	}

	// PIO の内部 ID（ovParametricInternalID）。読めなければ -1。
	long ProbeInternalID(MCObjectHandle h)
	{
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(h, kProbeOvParametricInternalID, block))
			return -1;
		Sint16 id = 0;
		if (!block.GetSint16(id))
			return -1;
		return static_cast<long>(id);
	}

	// 両端・行列・外接・LineLength を出す。**知りたいものは呼び出しの前に出しておく**ので、
	// タグには「どの段か」を必ず入れる。
	void ProbeDumpGeometry(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		VWPoint2D ptA;
		VWPoint2D ptB;
		VWParametricObj pio(h);
		pio.GetLinearObjectPos(ptA, ptB);

		TransformMatrix matrix;
		gSDK->GetEntityMatrix(h, matrix);

		WorldRect bounds;
		const Boolean gotBounds = gSDK->GetObjectBounds(h, bounds);

		const double dx = ptB.x - ptA.x;
		const double dy = ptB.y - ptA.y;

		std::string line = tag + " 両端 A=(" + ProbeNum(ptA.x) + ", " + ProbeNum(ptA.y) + ") B=(" +
						   ProbeNum(ptB.x) + ", " + ProbeNum(ptB.y) +
						   ") |AB|=" + ProbeNum(std::sqrt(dx * dx + dy * dy)) + " / offset=(" +
						   ProbeNum(matrix.v1.xOff) + ", " + ProbeNum(matrix.v1.yOff) + ") i=(" +
						   ProbeNum(matrix.v1.a00) + ", " + ProbeNum(matrix.v1.a01) + ")";
		if (pio.GetParamIndex("LineLength") != static_cast<size_t>(-1))
			line += " / LineLength=" + ProbeNum(pio.GetParamReal("LineLength"));
		probe.log(line);

		// 外接は「何も描いていない PIO」では反転した空矩形（±DBL_MAX）が返り、しかも
		// 戻り値は true になる（1 巡目の実測）。幅・高さだけでは読めないので旗も出す。
		if (gotBounds)
			probe.log(tag + " 外接: 幅=" + ProbeNum(bounds.right - bounds.left) + " 高=" +
					  ProbeNum(bounds.top - bounds.bottom) + " left=" + ProbeNum(bounds.left) +
					  " bottom=" + ProbeNum(bounds.bottom) + "（戻り値 true）");
		else
			probe.log(tag + " 外接: GetObjectBounds が false");
	}
} // namespace

VW_PROBE("linear-pio-endpoints", "線分 PIO の始端を動かせるか",
		 "SetLinearObjectPos に別の始端を渡して行列の offset が動くかを実測する")
{
	const size_t candidateCount = sizeof(kProbeCandidateNames) / sizeof(kProbeCandidateNames[0]);

	for (size_t i = 0; i < candidateCount; ++i)
	{
		const char* const name = kProbeCandidateNames[i];

		// **作る前に名前をログへ出す**——ここで落ちたらどの候補で落ちたかが残るように。
		EVSPluginType pluginType = kVSPluginMenu;
		const Boolean isPlugin = gSDK->GetPluginType(name, pluginType);
		probe.log(std::string("[候補] \"") + name +
				  "\": GetPluginType=" + (isPlugin ? "true" : "false") +
				  " 種別=" + ProbeInt(static_cast<long>(pluginType)) + "（2=オブジェクト）");
		if (!isPlugin || pluginType != kVSPluginObject)
		{
			probe.log("  → この綴りのオブジェクトプラグインは無い。飛ばす");
			continue;
		}

		// ダイアログを止めてから作る（止めないと 1 個目で待たされて先へ進めない）。
		if (gSDK->DefineCustomObject(name, kCustomObjectPrefNever) == nil)
		{
			probe.log("  → 定義が引けなかった。飛ばす");
			continue;
		}
		const MCObjectHandle h = gSDK->CreateCustomObject(name, WorldPt(10000, 5000), 0.0, true);
		if (h == nil)
		{
			probe.log("  → CreateCustomObject が nil。飛ばす");
			continue;
		}

		VWParametricObj pio(h);
		const bool hasLen = pio.GetParamIndex("LineLength") != static_cast<size_t>(-1);
		probe.log(std::string("  素性: 内部 ID=") + ProbeInt(ProbeInternalID(h)) +
				  " パラメータ数=" + ProbeInt(static_cast<long>(pio.GetParamsCount())) +
				  " LineLength=" + (hasLen ? "在る" : "無い"));
		ProbeDumpGeometry(probe, "  [1 作成直後]", h);

		// --- (5) 始端を別の点にする ------------------------------------------
		// (12000, 6000) → (12000, 11000)。始端が offset と違う点なのが要点。
		probe.log("  [5] SetLinearObjectPos((12000, 6000), (12000, 11000))"
				  "（始端を 2000, 1000 ずらす。長さ 5000・90°）");
		pio.SetLinearObjectPos(VWPoint2D(12000, 6000), VWPoint2D(12000, 11000));
		ProbeDumpGeometry(probe, "  [5 書いた直後]", h);
		gSDK->ResetObject(h);
		ProbeDumpGeometry(probe, "  [5 ResetObject 後]", h);

		// --- (6) 終端だけを動かす（始端はいまの offset のまま） ---------------
		probe.log("  [6] SetLinearObjectPos((12000, 6000), (15000, 10000))"
				  "（始端は据え置き。長さ 5000・53.130°）");
		pio.SetLinearObjectPos(VWPoint2D(12000, 6000), VWPoint2D(15000, 10000));
		gSDK->ResetObject(h);
		ProbeDumpGeometry(probe, "  [6 ResetObject 後]", h);

		// --- (7) LineLength だけを書き換えたら、終端はどちらへ伸びるか --------
		if (hasLen)
		{
			probe.log("  [7] SetParamReal(\"LineLength\", 1000) → ResetObject");
			pio.SetParamReal("LineLength", 1000.0);
			gSDK->ResetObject(h);
			ProbeDumpGeometry(probe, "  [7 ResetObject 後]", h);
		}
	}
}
