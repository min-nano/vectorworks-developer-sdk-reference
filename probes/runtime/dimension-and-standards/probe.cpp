//
//	probes/runtime/dimension-and-standards/probe.cpp
//
//	[issue #129] 直線寸法（ISDK::CreateLinearDimension）と寸法規格の実測。**2 巡目。**
//
//	1 巡目（PR #132 のコメント）で分かったこと・分からなかったことは次のとおりで、
//	この版はその残りだけを取りに行く:
//
//	  済: 規格の index は組み込み 1〜9（Arch/ASME/BSI/DIN/ISO/JIS/SIA/ASME Dual
//	      SideBySide/ASME Dual Stacked）・カスタムは 0 から下へ。ovDimStandard = 0 は
//	      **有効**（ヘッダの「zero is invalid」は実機と合わない）。作った寸法は
//	      アクティブレイヤに入る（型 63）。規格は index でも名前でも書けて他方が即座に
//	      追随し、ResetObject は要らない。存在しない名前は SetObjectVariable が false。
//	  残 1: **ovDimClass が読めなかった**（「型=11」）。t_Sint8 = 11 なので
//	        **GetSint8 で読む**のが正しかった。これが読めれば dimType の数値と
//	        「fix_ang / sloped / ordinate …」の対応が決まる。
//	  残 2: **startOffset の符号がどちら側を指すのか**。1 巡目は水平（+ が上）と
//	        垂直（+ が右）の 2 例だけで、一般の規則にならなかった。**4 方向を測る。**
//	  残 3: 連続寸法を作るとレイヤ直下の要素数が 11→13 と **+2** になった理由。
//	        直下の型を列挙して内訳を出す。
//	  残 4: 注釈へ移った証拠。1 巡目の在籍判定は**シートレイヤを作った時点で
//	        アクティブレイヤが移っていた**ため「移す前から いいえ」で、何も言えて
//	        いなかった。**作る直前のアクティブレイヤ**で判定し直す。
//

#include "Probe.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
	std::string DimProbeNum(double value)
	{
		std::string s = std::to_string(value);
		const std::string::size_type dot = s.find('.');
		if (dot != std::string::npos && s.size() > dot + 4)
			s.erase(dot + 4);
		return s;
	}

	std::string DimProbeInt(long long value)
	{
		return std::to_string(value);
	}

	// **型を決め打ちしない読み口。** 1 巡目は ovDimClass を Uint8 と決め打って読めず、
	// 1 往復むだにした（ヘッダは "unsigned char" と書いてあるのに実体は t_Sint8）。
	// 順に試して「効いた型」ごと返す。
	std::string DimProbeReadAny(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";

		Sint8 s8 = 0;
		if (v.GetSint8(s8))
			return DimProbeInt(s8) + " [Sint8]";
		Uint8 u8 = 0;
		if (v.GetUint8(u8))
			return DimProbeInt(u8) + " [Uint8]";
		Sint16 s16 = 0;
		if (v.GetSint16(s16))
			return DimProbeInt(s16) + " [Sint16]";
		Sint32 s32 = 0;
		if (v.GetSint32(s32))
			return DimProbeInt(s32) + " [Sint32]";
		Real64 r64 = 0.0;
		if (v.GetReal64(r64))
			return DimProbeNum(r64) + " [Real64]";
		bool b = false;
		if (v.GetBoolean(b))
			return std::string(b ? "true" : "false") + " [Boolean]";
		WorldPt pt;
		if (v.GetWorldPt(pt))
			return "(" + DimProbeNum(pt.x) + ", " + DimProbeNum(pt.y) + ") [WorldPt]";
		TXString str;
		if (v.GetTXString(str))
			return "\"" + std::string(static_cast<const char*>(str)) + "\" [TXString]";
		return "(値は取れたが既知のどの型でも読めない。型番号=" +
			   DimProbeInt(static_cast<long long>(v.GetType())) + ")";
	}

	bool DimProbeStandardName(short index, std::string& outName)
	{
		TVariableBlock v;
		if (!gSDK->GetDimensionStandardVariable(index, dimStdstandardName, v))
			return false;
		TXString raw;
		if (!v.GetTXString(raw))
			return false;
		outName = std::string(static_cast<const char*>(raw));
		return true;
	}

	// コンテナ直下の要素の「型の並び」。連続寸法を作ったときの増減の内訳を見るため。
	std::string DimProbeMemberTypes(MCObjectHandle container)
	{
		if (container == nil)
			return "(nil)";
		std::string out;
		long long count = 0;
		MCObjectHandle it = gSDK->FirstMemberObj(container);
		while (it != nil && count < 200)
		{
			if (!out.empty())
				out += ",";
			out += DimProbeInt(gSDK->GetObjectTypeN(it));
			++count;
			it = gSDK->NextObject(it);
		}
		return DimProbeInt(count) + " 件 [" + out + "]";
	}

	bool DimProbeIsInLayer(MCObjectHandle layer, MCObjectHandle target)
	{
		if (layer == nil || target == nil)
			return false;
		MCObjectHandle it = gSDK->FirstMemberObj(layer);
		int guard = 0;
		while (it != nil && guard < 10000)
		{
			if (it == target)
				return true;
			it = gSDK->NextObject(it);
			++guard;
		}
		return false;
	}
} // namespace

VW_PROBE("dimension-and-standards", "直線寸法の作り方と寸法規格（2 巡目）",
		 "dimType と ovDimClass の対応・startOffset の符号がどちら側か（4 方向）・"
		 "連続寸法を作ったときのレイヤの増減の内訳・注釈へ本当に移るかを実測する")
{
	probe.log("== 1. 寸法規格の一覧（1 巡目の再確認） ==");
	probe.log("NumberCustomDimensionStandards() = " +
			  DimProbeInt(gSDK->NumberCustomDimensionStandards()));
	for (short index = 12; index >= -12; --index)
	{
		std::string name;
		if (DimProbeStandardName(index, name))
			probe.log("  index " + DimProbeInt(index) + " = \"" + name + "\"");
	}
	{
		short current = 0;
		if (gSDK->GetProgramVariable(varDimStandard, &current))
			probe.log("文書の既定 varDimStandard = " + DimProbeInt(current));
		else
			probe.log("文書の既定 varDimStandard: GetProgramVariable が false");
	}

	MCObjectHandle designLayer = gSDK->GetActiveLayer();
	if (designLayer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した");
		return;
	}

	probe.log("");
	probe.log("== 2. dimType と ovDimClass の対応（残 1） ==");
	probe.log("ovDimClass のヘッダの割り当て: 0:fix_ang 1:sloped 2:ordinate 3:radial 4:diametrical "
			  "5:ang");
	probe.log("同じ 2 点に対して dimType だけを変え、ovDimClass を読み戻す。");
	probe.log("2 点は水平 (0,0)-(1000,0) と斜め (0,2000)-(1000,2600)、startOffset=300。");
	for (short dimType = 0; dimType <= 5; ++dimType)
	{
		MCObjectHandle flat = gSDK->CreateLinearDimension(WorldPt(0, 0), WorldPt(1000, 0), 300, 0,
														  Vector2(0, 0), dimType);
		MCObjectHandle slant = gSDK->CreateLinearDimension(WorldPt(0, 2000), WorldPt(1000, 2600),
														   300, 0, Vector2(0, 0), dimType);
		probe.log("");
		probe.log("--- dimType = " + DimProbeInt(dimType));
		if (flat == nil)
		{
			probe.log("  水平2点: CreateLinearDimension = nil");
		}
		else
		{
			probe.log("  水平2点: 型=" + DimProbeInt(gSDK->GetObjectTypeN(flat)) +
					  " ovDimClass=" + DimProbeReadAny(flat, ovDimClass) +
					  " ovDimDirection=" + DimProbeReadAny(flat, ovDimDirection));
			WorldRect box;
			if (gSDK->GetObjectBounds(flat, box))
				probe.log("    外接矩形 = 左" + DimProbeNum(box.left) + " 上" +
						  DimProbeNum(box.top) + " 右" + DimProbeNum(box.right) + " 下" +
						  DimProbeNum(box.bottom));
		}
		if (slant == nil)
		{
			probe.log("  斜め2点: CreateLinearDimension = nil");
		}
		else
		{
			probe.log("  斜め2点: 型=" + DimProbeInt(gSDK->GetObjectTypeN(slant)) +
					  " ovDimClass=" + DimProbeReadAny(slant, ovDimClass) +
					  " ovDimDirection=" + DimProbeReadAny(slant, ovDimDirection));
			WorldRect box;
			if (gSDK->GetObjectBounds(slant, box))
				probe.log("    外接矩形 = 左" + DimProbeNum(box.left) + " 上" +
						  DimProbeNum(box.top) + " 右" + DimProbeNum(box.right) + " 下" +
						  DimProbeNum(box.bottom));
		}
	}

	probe.log("");
	probe.log("== 3. startOffset の符号はどちら側か（残 2） ==");
	probe.log("同じ長さ 1000mm の寸法を 4 方向へ作り、startOffset=+300 で寸法線が");
	probe.log("どちらへ出るかを外接矩形から測る。p1 は各行の始点。");
	probe.log("「左法線」は p1→p2 を +90 度（反時計回り）に回した向き。符号の規則が");
	probe.log("「進行方向の左」なら、外接矩形の中心は必ず左法線の側へ寄る。");
	{
		struct DimProbeCase
		{
			const char* label;
			WorldPt p1;
			WorldPt p2;
		};
		const DimProbeCase cases[4] = {
			{"+x 向き (0,0)->(1000,0)", WorldPt(0, 5000), WorldPt(1000, 5000)},
			{"-x 向き (1000,0)->(0,0)", WorldPt(3000, 5000), WorldPt(2000, 5000)},
			{"+y 向き (0,0)->(0,1000)", WorldPt(5000, 5000), WorldPt(5000, 6000)},
			{"-y 向き (0,1000)->(0,0)", WorldPt(7000, 6000), WorldPt(7000, 5000)},
		};
		for (int i = 0; i < 4; ++i)
		{
			MCObjectHandle dim =
				gSDK->CreateLinearDimension(cases[i].p1, cases[i].p2, 300, 0, Vector2(0, 0), 0);
			if (dim == nil)
			{
				probe.log(std::string("  ") + cases[i].label + ": nil");
				continue;
			}
			WorldRect box;
			if (!gSDK->GetObjectBounds(dim, box))
			{
				probe.log(std::string("  ") + cases[i].label + ": GetObjectBounds が false");
				continue;
			}
			// 線分の中点と外接矩形の中心のずれを、左法線へ射影する。
			const double midX = (cases[i].p1.x + cases[i].p2.x) / 2.0;
			const double midY = (cases[i].p1.y + cases[i].p2.y) / 2.0;
			const double cenX = (box.left + box.right) / 2.0;
			const double cenY = (box.top + box.bottom) / 2.0;
			const double dx = cases[i].p2.x - cases[i].p1.x;
			const double dy = cases[i].p2.y - cases[i].p1.y;
			const double len = (dx * dx + dy * dy > 0) ? std::sqrt(dx * dx + dy * dy) : 1.0;
			// 左法線 = (-dy, dx) / len
			const double leftDot = ((cenX - midX) * (-dy) + (cenY - midY) * dx) / len;
			probe.log(std::string("  ") + cases[i].label);
			probe.log("    外接矩形 = 左" + DimProbeNum(box.left) + " 上" + DimProbeNum(box.top) +
					  " 右" + DimProbeNum(box.right) + " 下" + DimProbeNum(box.bottom));
			probe.log("    中点からのずれ = (" + DimProbeNum(cenX - midX) + ", " +
					  DimProbeNum(cenY - midY) + ") / 左法線への射影 = " + DimProbeNum(leftDot) +
					  "（正なら進行方向の左、負なら右）");
		}
	}

	probe.log("");
	probe.log("== 4. 連続寸法を作るとレイヤ直下がどう動くか（残 3） ==");
	{
		probe.log("  作る前のレイヤ直下: " + DimProbeMemberTypes(designLayer));
		MCObjectHandle chainA = gSDK->CreateLinearDimension(WorldPt(0, -2000), WorldPt(1000, -2000),
															300, 0, Vector2(0, 0), 0);
		MCObjectHandle chainB = gSDK->CreateLinearDimension(
			WorldPt(1000, -2000), WorldPt(2500, -2000), 300, 0, Vector2(0, 0), 0);
		if (chainA == nil || chainB == nil)
		{
			probe.log("  材料の寸法を作れなかった");
		}
		else
		{
			probe.log("  材料 2 本を作った後のレイヤ直下: " + DimProbeMemberTypes(designLayer));
			MCObjectHandle chain = gSDK->CreateChainDimension(chainA, chainB);
			probe.log("  CreateChainDimension = " + std::string(chain == nil ? "nil" : "成功"));
			probe.log("  作った後のレイヤ直下: " + DimProbeMemberTypes(designLayer));
			if (chain != nil)
			{
				probe.log("  連続寸法: 型=" + DimProbeInt(gSDK->GetObjectTypeN(chain)) +
						  " / レイヤ直下にいるか=" +
						  (DimProbeIsInLayer(designLayer, chain) ? "はい" : "いいえ"));
				probe.log("  連続寸法の中身: " + DimProbeMemberTypes(chain));
				probe.log("  元の A はレイヤ直下か=" +
						  std::string(DimProbeIsInLayer(designLayer, chainA) ? "はい" : "いいえ") +
						  " / B=" +
						  std::string(DimProbeIsInLayer(designLayer, chainB) ? "はい" : "いいえ"));
				probe.log("  連続寸法へ ovDimStandardName を読む: " +
						  DimProbeReadAny(chain, ovDimStandardName));
				probe.log("  連続寸法へ ovDimStandard を読む: " +
						  DimProbeReadAny(chain, ovDimStandard));
				// 取り込まれた子が寸法として読めるなら、規格はそこへ当てられる。
				MCObjectHandle firstChild = gSDK->FirstMemberObj(chain);
				if (firstChild != nil)
					probe.log("  連続寸法の 1 番目の子（型 " +
							  DimProbeInt(gSDK->GetObjectTypeN(firstChild)) +
							  "）の ovDimStandardName = " +
							  DimProbeReadAny(firstChild, ovDimStandardName));
			}
		}
	}

	probe.log("");
	probe.log("== 5. 注釈へ本当に移るか（残 4） ==");
	{
		MCObjectHandle sheet = gSDK->CreateLayer("寸法調査シート", kLayerSheet);
		if (sheet == nil)
		{
			probe.fail("シートレイヤを作れなかった");
		}
		else
		{
			MCObjectHandle viewport = gSDK->CreateViewport(sheet);
			if (viewport == nil)
			{
				probe.fail("CreateViewport が nil を返した");
			}
			else
			{
				gSDK->SetViewportLayerVisibility(viewport, designLayer, 0); // 0 = 表示
				gSDK->UpdateViewport(viewport);
				probe.log("ビューポートを作った（型 " +
						  DimProbeInt(gSDK->GetObjectTypeN(viewport)) + "）");

				// **ここが 1 巡目の取りこぼし。** シートレイヤを作るとアクティブレイヤが
				// 移るので、「作った寸法がどこへ入ったか」は**作る直前のアクティブレイヤ**
				// を見ないと分からない。
				MCObjectHandle activeNow = gSDK->GetActiveLayer();
				probe.log(
					"寸法を作る直前のアクティブレイヤはシートレイヤか = " +
					std::string(activeNow == sheet ? "はい" : "いいえ") +
					" / デザインレイヤか = " + (activeNow == designLayer ? "はい" : "いいえ"));

				MCObjectHandle annot = gSDK->CreateLinearDimension(WorldPt(0, 0), WorldPt(100, 0),
																   20, 0, Vector2(0, 0), 0);
				if (annot == nil)
				{
					probe.fail("注釈用の寸法を作れなかった");
				}
				else
				{
					probe.log("移す前: 作成時のアクティブレイヤ直下にいるか = " +
							  std::string(DimProbeIsInLayer(activeNow, annot) ? "はい" : "いいえ"));
					const bool added = gSDK->AddViewportAnnotationObject(viewport, annot) != 0;
					probe.log("AddViewportAnnotationObject = " +
							  std::string(added ? "true" : "false"));
					probe.log("移した後: 作成時のアクティブレイヤ直下にいるか = " +
							  std::string(DimProbeIsInLayer(activeNow, annot) ? "はい" : "いいえ") +
							  "（いいえ＝レイヤから抜けて注釈へ移った）");
					probe.log("移した後も寸法として読めるか: ovDimStandardName = " +
							  DimProbeReadAny(annot, ovDimStandardName) +
							  " / ovDimClass = " + DimProbeReadAny(annot, ovDimClass));
					probe.log("紙の上の文字の大きさ: ovDimTextSizeInPoints = " +
							  DimProbeReadAny(annot, ovDimTextSizeInPoints) +
							  " / 図面上 ovDimFontSize = " + DimProbeReadAny(annot, ovDimFontSize));
					WorldRect box;
					if (gSDK->GetObjectBounds(annot, box))
						probe.log("移した後の外接矩形(mm) = 左" + DimProbeNum(box.left) + " 上" +
								  DimProbeNum(box.top) + " 右" + DimProbeNum(box.right) + " 下" +
								  DimProbeNum(box.bottom));
					gSDK->UpdateViewport(viewport);
				}
			}
		}
	}

	probe.log("");
	probe.log("== 目で見ないと分からないこと（利用者へ） ==");
	probe.log("デザインレイヤに dimType ごとの寸法が並んでいる（上のログの順。水平のものと");
	probe.log("斜めのものが交互）。");
	probe.log("(1) 斜めの 2 点に掛けた寸法のうち、**寸法線が斜めに傾いているもの**と");
	probe.log("    **水平（または垂直）のままのもの**がどの dimType か。");
	probe.log("(2) 見た目が「連続寸法（1 本の線に並ぶ）」「ordinate（基準からの座標）」に");
	probe.log("    なっているものがあれば、その dimType。");
	probe.log("(3) シートレイヤ「寸法調査シート」の注釈の寸法は見えているか。");
}
