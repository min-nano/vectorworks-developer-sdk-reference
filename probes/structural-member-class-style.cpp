//
//	probes/structural-member-class-style.cpp
//
//	[issue #158] Findings「取り込みで使う形（構造材＝クラススタイル…）」に載せる手順が、
//	書いたとおりでコンパイルを通るかだけを確かめる（構文チェック。実行はしない）。
//	確認できたら消す。
//

#include "VectorworksSDK.h"

using namespace VWFC::VWObjects;

void probe_structural_member_class_style(MCObjectHandle member)
{
	VWParametricObj pio(member); // CreateCustomObjectPath で作った構造材
	const char* const kFaces[] = {"_Above", "_At", "_Below"};
	for (const char* face : kFaces)
	{
		// 構造材＝クラススタイル（面 6 / 線 4）。クラス欄は**名前**で書く。
		pio.SetParamValue(TXString("MemberClass") + face, TXString("木構造_柱"));
		pio.SetParamValue(TXString("MemberFillStyle") + face, TXString("6"));
		pio.SetParamValue(TXString("MemberPenStyle") + face, TXString("4"));
		pio.SetParamBool(TXString("MemberDisplay") + face, true);
		// 被覆と中心線は非表示。
		pio.SetParamBool(TXString("CoverDisplay") + face, false);
		pio.SetParamBool(TXString("CenterlineDisplay") + face, false);
		// 端部は両端（＝始端と終端の両方を表示）。
		pio.SetParamBool(TXString("StartCapDisplay") + face, true);
		pio.SetParamBool(TXString("EndCapDisplay") + face, true);
	}
	gSDK->ResetObject(member);
}
