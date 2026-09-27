//
//	probes/runtime/dimension-and-standards/probe.cpp
//
//	[issue #129] 直線寸法（ISDK::CreateLinearDimension）の引数の意味と、寸法規格の
//	一覧の取り方・寸法への当て方を実測する。ビューポートの注釈へ移せるかも見る。
//
//	ヘッダから分かっていること（APIBase.Legacy.Defs.h / ObjectVariables.h /
//	MiniCadCallBacks.h のコメント）を、実機で確かめ直すのがこのプローブの役目:
//
//	  - startOffset は「p1 から寸法線までの距離」、textOffset は CURRENTLY UNUSED、
//	    dir は (0,0) を渡すと p2-p1 から自動計算。
//	  - dimType は「水平／垂直だけ」「p1→p2 の向きへ回す」「ordinate」の 3 択らしいが、
//	    **どの数値がどれなのかはヘッダに書かれていない**（ovDimClass の 0=fix_ang /
//	    1=sloped / 2=ordinate と対応するかを読み戻して確かめる）。
//	  - 寸法規格は組み込みが index 1〜9、カスタムが index 0〜-8。
//	  - ovDimStandard は「0 は無効」と書いてあるのに、カスタムの範囲には 0 が入る
//	    ——**食い違っているので実測で決める**。
//

#include "Probe.h"

#include <string>

namespace
{
	// 短い名前・ありふれた名前は SDK と OS のヘッダとぶつかるので、すべて接頭辞を付ける。
	// （probes/runtime/README.md「短い名前・ありふれた名前を使わない」）

	std::string DimProbeNum(double value)
	{
		// std::to_string の既定（小数 6 桁）だと mm の値が読みにくいので短く整える。
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

	// オブジェクト変数を型ごとに読み、「読めたか／値」を 1 行で返す。
	std::string DimProbeReadSint8(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		Sint8 raw = 0;
		if (!v.GetSint8(raw))
			return "(Sint8 として読めない。型=" + DimProbeInt(static_cast<long long>(v.GetType())) +
				   ")";
		return DimProbeInt(raw);
	}

	std::string DimProbeReadUint8(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		Uint8 raw = 0;
		if (!v.GetUint8(raw))
			return "(Uint8 として読めない。型=" + DimProbeInt(static_cast<long long>(v.GetType())) +
				   ")";
		return DimProbeInt(raw);
	}

	std::string DimProbeReadReal(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		Real64 raw = 0.0;
		if (!v.GetReal64(raw))
			return "(Real64 として読めない。型=" +
				   DimProbeInt(static_cast<long long>(v.GetType())) + ")";
		return DimProbeNum(raw);
	}

	std::string DimProbeReadString(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		TXString raw;
		if (!v.GetTXString(raw))
			return "(TXString として読めない。型=" +
				   DimProbeInt(static_cast<long long>(v.GetType())) + ")";
		return std::string(static_cast<const char*>(raw));
	}

	std::string DimProbeReadPoint(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		WorldPt raw;
		if (!v.GetWorldPt(raw))
			return "(WorldPt として読めない。型=" +
				   DimProbeInt(static_cast<long long>(v.GetType())) + ")";
		return "(" + DimProbeNum(raw.x) + ", " + DimProbeNum(raw.y) + ")";
	}

	// 規格 index に対して「名前が引けるか」を返す。引けなければ空。
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

	// コンテナの中身を数える（FirstMemberObj → NextObject）。
	long long DimProbeCountMembers(MCObjectHandle container)
	{
		if (container == nil)
			return -1;
		long long count = 0;
		MCObjectHandle it = gSDK->FirstMemberObj(container);
		while (it != nil && count < 10000)
		{
			++count;
			it = gSDK->NextObject(it);
		}
		return count;
	}

	// その寸法が「いまアクティブレイヤの中にいるか」を、レイヤを歩いて確かめる。
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

	// 寸法 1 本を作り、素性を読み戻してログへ出す。
	MCObjectHandle DimProbeMakeAndReport(vwprobe::Report& probe, MCObjectHandle layer,
										 const WorldPt& p1, const WorldPt& p2,
										 WorldCoord startOffset, short dimType, const char* label)
	{
		probe.log("");
		probe.log(std::string("--- ") + label + ": CreateLinearDimension(p1=(" + DimProbeNum(p1.x) +
				  "," + DimProbeNum(p1.y) + "), p2=(" + DimProbeNum(p2.x) + "," +
				  DimProbeNum(p2.y) + "), startOffset=" + DimProbeNum(startOffset) +
				  ", textOffset=0, dir=(0,0), dimType=" + DimProbeInt(dimType) + ")");

		const long long before = DimProbeCountMembers(layer);
		MCObjectHandle dim =
			gSDK->CreateLinearDimension(p1, p2, startOffset, 0, Vector2(0, 0), dimType);
		const long long after = DimProbeCountMembers(layer);

		if (dim == nil)
		{
			probe.fail(std::string(label) + ": CreateLinearDimension が nil を返した");
			return nil;
		}

		probe.log("  できた。オブジェクト型 GetObjectTypeN = " +
				  DimProbeInt(gSDK->GetObjectTypeN(dim)));
		probe.log("  アクティブレイヤの要素数: " + DimProbeInt(before) + " -> " +
				  DimProbeInt(after) + " / この寸法はレイヤの中にいるか: " +
				  (DimProbeIsInLayer(layer, dim) ? "はい" : "いいえ"));
		probe.log("  ovDimClass(26) = " + DimProbeReadUint8(dim, ovDimClass) +
				  "  ※0:fix_ang 1:sloped 2:ordinate 3:radial 4:diametrical 5:ang");
		probe.log("  ovDimStartPt(13) = " + DimProbeReadPoint(dim, ovDimStartPt) +
				  " / ovDimEndPt(14) = " + DimProbeReadPoint(dim, ovDimEndPt));
		probe.log("  ovDimStartOffset(15) = " + DimProbeReadReal(dim, ovDimStartOffset) +
				  " / ovDimStartOffsetInCurrUnits(45) = " +
				  DimProbeReadReal(dim, ovDimStartOffsetInCurrUnits));
		probe.log("  ovDimStandard(0) = " + DimProbeReadSint8(dim, ovDimStandard) +
				  " / ovDimStandardName(27) = " + DimProbeReadString(dim, ovDimStandardName));
		probe.log("  ovDimTextSizeInPoints(40) = " + DimProbeReadReal(dim, ovDimTextSizeInPoints) +
				  " / ovDimFontSize(17) = " + DimProbeReadReal(dim, ovDimFontSize));

		WorldRect bounds;
		if (gSDK->GetObjectBounds(dim, bounds))
			probe.log("  バウンディングボックス(mm) = 左" + DimProbeNum(bounds.left) + " 上" +
					  DimProbeNum(bounds.top) + " 右" + DimProbeNum(bounds.right) + " 下" +
					  DimProbeNum(bounds.bottom));
		else
			probe.log("  GetObjectBounds が false");

		return dim;
	}
} // namespace

VW_PROBE("dimension-and-standards", "直線寸法の作り方と寸法規格の一覧・当て方",
		 "CreateLinearDimension の引数の意味・作った寸法の行き先・規格の index と名前・"
		 "ビューポート注釈へ移せるか・連続寸法（CreateChainDimension）を実測する")
{
	probe.log("== 1. 寸法規格の一覧 ==");
	probe.log("NumberCustomDimensionStandards() = " +
			  DimProbeInt(gSDK->NumberCustomDimensionStandards()));
	probe.log("ヘッダの主張: 組み込みは index 1〜9、カスタムは index 0〜-8。");
	probe.log("index -12〜12 を総当たりして、名前が引けるものだけを並べる:");
	for (short index = 12; index >= -12; --index)
	{
		std::string name;
		if (DimProbeStandardName(index, name))
			probe.log("  index " + DimProbeInt(index) + " = \"" + name + "\"");
	}
	probe.log("（上に出なかった index は GetDimensionStandardVariable が false を返したもの）");

	probe.log("");
	probe.log("文書の既定の規格（ProgramVariable varDimStandard=71。ヘッダ上 short）:");
	{
		// GetProgramVariable は TVariableBlock ではなく void* を取る（ISDK.h:784）。
		short current = 0;
		if (gSDK->GetProgramVariable(varDimStandard, &current))
			probe.log("  varDimStandard = " + DimProbeInt(current));
		else
			probe.log("  GetProgramVariable が false");
	}

	probe.log("");
	probe.log("== 2. CreateLinearDimension: dimType と startOffset の符号 ==");
	MCObjectHandle layer = gSDK->GetActiveLayer();
	if (layer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した（アクティブレイヤが無い）");
		return;
	}
	{
		TXString layerName;
		gSDK->GetObjectName(layer, layerName);
		probe.log("アクティブレイヤ: \"" + std::string(static_cast<const char*>(layerName)) +
				  "\" 要素数 " + DimProbeInt(DimProbeCountMembers(layer)));
	}

	// 水平な 2 点（1000mm 離す）。同じ 2 点に対し dimType だけを変える。
	const WorldPt kDimProbeHorizA(0, 0);
	const WorldPt kDimProbeHorizB(1000, 0);
	// 斜めの 2 点（水平／垂直しか許さない dimType との違いが出るように）。
	const WorldPt kDimProbeSlantA(0, 2000);
	const WorldPt kDimProbeSlantB(1000, 2600);

	MCObjectHandle dimType0 = DimProbeMakeAndReport(probe, layer, kDimProbeHorizA, kDimProbeHorizB,
													300, 0, "水平2点 dimType=0");
	MCObjectHandle dimType1 = DimProbeMakeAndReport(probe, layer, kDimProbeHorizA, kDimProbeHorizB,
													300, 1, "水平2点 dimType=1");
	DimProbeMakeAndReport(probe, layer, kDimProbeHorizA, kDimProbeHorizB, 300, 2,
						  "水平2点 dimType=2");
	DimProbeMakeAndReport(probe, layer, kDimProbeHorizA, kDimProbeHorizB, 300, 3,
						  "水平2点 dimType=3");
	DimProbeMakeAndReport(probe, layer, kDimProbeSlantA, kDimProbeSlantB, 300, 0,
						  "斜め2点 dimType=0");
	DimProbeMakeAndReport(probe, layer, kDimProbeSlantA, kDimProbeSlantB, 300, 1,
						  "斜め2点 dimType=1");
	// startOffset の符号。バウンディングボックスが基準線のどちら側へ出るかで向きが分かる。
	DimProbeMakeAndReport(probe, layer, kDimProbeHorizA, kDimProbeHorizB, -300, 0,
						  "水平2点 startOffset=-300");
	// 垂直な 2 点（縦の寸法が dir 自動計算で出るか）。
	DimProbeMakeAndReport(probe, layer, WorldPt(3000, 0), WorldPt(3000, 1500), 300, 0,
						  "垂直2点 dimType=0");

	probe.log("");
	probe.log("== 3. 作った寸法へ規格を当てる ==");
	if (dimType0 != nil)
	{
		// 一覧のうち、いま当たっているものと違う index を 1 つ選ぶ。
		const std::string before = DimProbeReadSint8(dimType0, ovDimStandard);
		probe.log("当てる前: ovDimStandard = " + before +
				  " / ovDimStandardName = " + DimProbeReadString(dimType0, ovDimStandardName));

		// (a) index で書く
		for (short index = 1; index <= 9; ++index)
		{
			std::string name;
			if (!DimProbeStandardName(index, name))
				continue;
			if (DimProbeInt(index) == before)
				continue;
			probe.log("(a) ovDimStandard に index " + DimProbeInt(index) + "（\"" + name +
					  "\"）を書く:");
			// TVariableBlock の DEFINE_TYPE は setter を operator= で作る（SetSint8 は無い）。
			TVariableBlock v;
			v = static_cast<Sint8>(index);
			const bool ok = gSDK->SetObjectVariable(dimType0, ovDimStandard, v) != 0;
			probe.log("    SetObjectVariable = " + std::string(ok ? "true" : "false"));
			probe.log("    ResetObject の前: ovDimStandard = " +
					  DimProbeReadSint8(dimType0, ovDimStandard) +
					  " / ovDimStandardName = " + DimProbeReadString(dimType0, ovDimStandardName));
			const bool reset = gSDK->ResetObject(dimType0) != 0;
			probe.log("    ResetObject = " + std::string(reset ? "true" : "false") +
					  " の後: ovDimStandard = " + DimProbeReadSint8(dimType0, ovDimStandard) +
					  " / ovDimStandardName = " + DimProbeReadString(dimType0, ovDimStandardName));
			break;
		}
	}
	if (dimType1 != nil)
	{
		// (b) 名前で書く。いまと違う名前を一覧から 1 つ選ぶ。
		const std::string current = DimProbeReadString(dimType1, ovDimStandardName);
		for (short index = 9; index >= -9; --index)
		{
			std::string name;
			if (!DimProbeStandardName(index, name))
				continue;
			if (name == current || name.empty())
				continue;
			probe.log("(b) ovDimStandardName に \"" + name + "\"（index " + DimProbeInt(index) +
					  "）を書く:");
			TVariableBlock v;
			v = TXString(name.c_str());
			const bool ok = gSDK->SetObjectVariable(dimType1, ovDimStandardName, v) != 0;
			probe.log("    SetObjectVariable = " + std::string(ok ? "true" : "false"));
			probe.log("    ResetObject の前: ovDimStandard = " +
					  DimProbeReadSint8(dimType1, ovDimStandard) +
					  " / ovDimStandardName = " + DimProbeReadString(dimType1, ovDimStandardName));
			const bool reset = gSDK->ResetObject(dimType1) != 0;
			probe.log("    ResetObject = " + std::string(reset ? "true" : "false") +
					  " の後: ovDimStandard = " + DimProbeReadSint8(dimType1, ovDimStandard) +
					  " / ovDimStandardName = " + DimProbeReadString(dimType1, ovDimStandardName));
			break;
		}
		probe.log("(c) 存在しない名前 \"存在しない規格名 zzz\" を書いたらどうなるか:");
		{
			TVariableBlock v;
			v = TXString("存在しない規格名 zzz");
			const bool ok = gSDK->SetObjectVariable(dimType1, ovDimStandardName, v) != 0;
			probe.log("    SetObjectVariable = " + std::string(ok ? "true" : "false") +
					  " / 直後の ovDimStandard = " + DimProbeReadSint8(dimType1, ovDimStandard) +
					  " / ovDimStandardName = " + DimProbeReadString(dimType1, ovDimStandardName));
		}
	}

	probe.log("");
	probe.log("== 4. 連続寸法（CreateChainDimension） ==");
	{
		// 一直線に並ぶ、端点を共有する 2 本を作って繋いでみる。
		MCObjectHandle chainA = gSDK->CreateLinearDimension(WorldPt(0, -2000), WorldPt(1000, -2000),
															300, 0, Vector2(0, 0), 0);
		MCObjectHandle chainB = gSDK->CreateLinearDimension(
			WorldPt(1000, -2000), WorldPt(2500, -2000), 300, 0, Vector2(0, 0), 0);
		if (chainA == nil || chainB == nil)
		{
			probe.log("  繋ぐ材料の寸法を作れなかった（CreateLinearDimension が nil）");
		}
		else
		{
			const long long before = DimProbeCountMembers(layer);
			MCObjectHandle chain = gSDK->CreateChainDimension(chainA, chainB);
			const long long after = DimProbeCountMembers(layer);
			if (chain == nil)
			{
				probe.log(
					"  CreateChainDimension = nil（端点を共有する同じ向きの 2 本でも繋がらない）");
			}
			else
			{
				probe.log("  CreateChainDimension = 成功。GetObjectTypeN = " +
						  DimProbeInt(gSDK->GetObjectTypeN(chain)));
				probe.log("  レイヤの要素数: " + DimProbeInt(before) + " -> " + DimProbeInt(after) +
						  " / 連続寸法はレイヤの中か: " +
						  (DimProbeIsInLayer(layer, chain) ? "はい" : "いいえ"));
				probe.log(
					"  元の 2 本はレイヤに残っているか: A=" +
					std::string(DimProbeIsInLayer(layer, chainA) ? "はい" : "いいえ") +
					" B=" + std::string(DimProbeIsInLayer(layer, chainB) ? "はい" : "いいえ"));
				probe.log("  連続寸法の中身の数（FirstMemberObj/NextObject）= " +
						  DimProbeInt(DimProbeCountMembers(chain)));
				probe.log("  連続寸法の ovDimStandardName = " +
						  DimProbeReadString(chain, ovDimStandardName));
			}
		}
	}

	probe.log("");
	probe.log("== 5. ビューポートの注釈へ寸法を入れる ==");
	{
		MCObjectHandle sheet = gSDK->CreateLayer("寸法調査シート", kLayerSheet);
		if (sheet == nil)
		{
			probe.fail("シートレイヤを作れなかった（CreateLayer が nil）");
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
				gSDK->SetViewportLayerVisibility(viewport, layer, 0); // 0 = 表示
				gSDK->UpdateViewport(viewport);
				probe.log("ビューポートを作った。GetObjectTypeN = " +
						  DimProbeInt(gSDK->GetObjectTypeN(viewport)));

				// 注釈用の寸法を「用紙の寸法」で作る。注釈空間は用紙座標なので、
				// 100mm 離した 2 点は用紙上で 100mm になるはず。
				MCObjectHandle annot = gSDK->CreateLinearDimension(WorldPt(0, 0), WorldPt(100, 0),
																   20, 0, Vector2(0, 0), 0);
				if (annot == nil)
				{
					probe.fail("注釈用の寸法を作れなかった");
				}
				else
				{
					probe.log("注釈へ移す前: レイヤの中にいるか = " +
							  std::string(DimProbeIsInLayer(layer, annot) ? "はい" : "いいえ"));
					WorldRect beforeBounds;
					const bool gotBefore = gSDK->GetObjectBounds(annot, beforeBounds) != 0;
					if (gotBefore)
						probe.log("  移す前のバウンディングボックス(mm) = 左" +
								  DimProbeNum(beforeBounds.left) + " 上" +
								  DimProbeNum(beforeBounds.top) + " 右" +
								  DimProbeNum(beforeBounds.right) + " 下" +
								  DimProbeNum(beforeBounds.bottom));

					const bool added = gSDK->AddViewportAnnotationObject(viewport, annot) != 0;
					probe.log("AddViewportAnnotationObject = " +
							  std::string(added ? "true" : "false"));
					probe.log("  移した後: レイヤの中にいるか = " +
							  std::string(DimProbeIsInLayer(layer, annot) ? "はい" : "いいえ") +
							  "（いいえ＝レイヤから抜けて注釈へ移った）");
					probe.log("  移した後の ovDimStandardName = " +
							  DimProbeReadString(annot, ovDimStandardName) +
							  " / ovDimClass = " + DimProbeReadUint8(annot, ovDimClass));
					WorldRect afterBounds;
					if (gSDK->GetObjectBounds(annot, afterBounds))
						probe.log("  移した後のバウンディングボックス(mm) = 左" +
								  DimProbeNum(afterBounds.left) + " 上" +
								  DimProbeNum(afterBounds.top) + " 右" +
								  DimProbeNum(afterBounds.right) + " 下" +
								  DimProbeNum(afterBounds.bottom));
					else
						probe.log("  移した後は GetObjectBounds が false");

					gSDK->UpdateViewport(viewport);
					probe.log("  UpdateViewport 後: この寸法はまだ生きているか（型 = " +
							  DimProbeInt(gSDK->GetObjectTypeN(annot)) + "）");
				}
			}
		}
	}

	probe.log("");
	probe.log("== 目で見ないと分からないこと（利用者へ） ==");
	probe.log("(1) デザインレイヤに並んだ寸法のうち、水平/垂直だけに見えるもの・斜めに");
	probe.log("    傾いたものはどれか（上の dimType ごとのラベルと突き合わせてほしい）。");
	probe.log("(2) シートレイヤ「寸法調査シート」のビューポートの注釈に、寸法が 1 本");
	probe.log("    見えているか。見えているなら、その文字の大きさは用紙上で規格どおりか。");
}
