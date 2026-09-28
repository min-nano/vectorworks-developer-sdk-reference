//
//	probes/runtime/dim-association/probe.cpp
//
//	[issue #138] ISDK::AssociateLinearDimension をどう呼べば、図形が動いたときに寸法が
//	追従するのか。**残っているのは「画面でドラッグしたら追うか」の 1 点だけ**なので、
//	このプローブはそれだけを見る。
//
//	【1 巡目・2 巡目で確定したこと】（Findings「寸法」に反映済み）
//
//	  * 効く手順は
//	      AssociateLinearDimension(寸法, false)
//	      → CreateConstraintModel(nil, true) → MoveObject → UpdateConstraintModel()
//	    #134 の「8 通り空振り」は関連付けの失敗ではなく、解く段を呼んでいなかっただけ。
//	  * 関連付いたかは HasConstraint / 制約ノード（型 110）で読める。
//	  * 門は varAssociateDims(28) ただ 1 つ。varAutoAssociateDims(134) は関与しない。
//	  * 関連付けの正体は「同一点上（coincident）」の拘束。
//
//	【この巡が取りに行くもの】——1 点だけ
//
//	  **利用者が画面で図形を動かしたときに、寸法が追うか。** プラグインの用途
//	  （伏図・軸組図の寸法）では動かすのはたいてい利用者なので、ここは要る。
//
//	【2 回走らせる必要がある——そしてそれは「同じ図面で」】
//
//	  画面のドラッグはプローブから起こせないので、**走らせる → 画面で動かす →
//	  もう一度走らせる**の 3 手が要る。**2 回目は必ず同じ図面で走らせてもらう**
//	  ——ここが前の巡で 2 度空振りした。リポジトリの作法が「新規の空図面で走らせる」
//	  なので、利用者が毎回まっさらな図面を開き、そのたびに 1 組を作り直していた
//	  （＝判定に入れない）。そこで、この巡では:
//
//	    * **作るのは 1 組だけ**にした（環境設定の総当たりは答えが出たので落とした）。
//	      新規の空図面で走らせれば、図面にあるのはこの 1 組と説明の文字だけになる。
//	    * **原点のすぐ上（y = +4000）に置く**。前は y = -100000（原点の 100m 下）に
//	      置いていたので、そもそも見つけにくかった。
//	    * **お願いをログの先頭にも出す**（結果ダイアログで末尾まで読まなくてよいように）。
//	    * **判定を図面の中だけで完結させる**。「線A が動いたか」は線B（触らない側）
//	      との間隔で見るので、作った座標を覚えておく必要がない——**別の図面で作られた
//	      1 組でも、そのまま判定できる**。
//

#include "Probe.h"

#include <cmath>
#include <string>

namespace
{
	// 短い名前・ありふれた名前は SDK と OS のヘッダとぶつかるので接頭辞を付ける
	// （probes/runtime/README.md「短い名前・ありふれた名前を使わない」）。

	// 制約ノードのオブジェクト型（Objs.TDType.h:176 の kConstraintNode）。
	const short kDimAssocConstraintNode = 110;

	// 試験の 1 組。**原点のすぐ上**に置く（開いた図面ですぐ目に入るように）。
	const WorldCoord kDimAssocY = 4000;
	const WorldCoord kDimAssocX1 = 0;
	const WorldCoord kDimAssocX2 = 3000;
	// 線A と線B の間隔。**線A が動いたかはこの間隔で見る**ので、作った座標を
	// 覚えておく必要がない。
	const double kDimAssocSpan = 3000.0;

	const char* const kDimAssocDimName = "#138 寸法（触らない）";
	const char* const kDimAssocLineAName = "#138 線A（これを右へ動かす）";
	const char* const kDimAssocLineBName = "#138 線B（触らない）";

	std::string DimAssocNum(double value)
	{
		std::string s = std::to_string(value);
		const std::string::size_type dot = s.find('.');
		if (dot != std::string::npos && s.size() > dot + 4)
			s.erase(dot + 4);
		return s;
	}

	std::string DimAssocYesNo(bool value)
	{
		return value ? "はい" : "いいえ";
	}

	bool DimAssocPointValue(MCObjectHandle h, short selector, WorldPt& outPt)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return false;
		return v.GetWorldPt(outPt) != 0;
	}

	std::string DimAssocReadPoint(MCObjectHandle h, short selector)
	{
		WorldPt pt;
		if (!DimAssocPointValue(h, selector, pt))
			return "(読めない)";
		return "(" + DimAssocNum(pt.x) + ", " + DimAssocNum(pt.y) + ")";
	}

	// 関連付けの有無（1・2 巡目で HasConstraint と制約ノードが一致することを確認済み）。
	std::string DimAssocConstraintState(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		const bool has = gSDK->HasConstraint(h) != 0;
		const MCObjectHandle node = gSDK->FindAuxObject(h, kDimAssocConstraintNode);
		return "HasConstraint=" + DimAssocYesNo(has) +
			   " / 制約ノード(110)=" + DimAssocYesNo(node != nil);
	}

	// 縦棒の x。縦棒なので外接矩形の左右が同じ値になる。
	bool DimAssocStubX(MCObjectHandle line, double& outX)
	{
		WorldRect bounds;
		if (line == nil || !gSDK->GetObjectBounds(line, bounds))
			return false;
		outX = static_cast<double>(bounds.left);
		return true;
	}

	// お願い。**1 組が実際にどこにあるか**を添える——古い巡が作った 1 組
	// （原点の 100m 下）がそのまま残っている図面でも、迷わず探せるように。
	void DimAssocAsk(vwprobe::Report& probe, bool knowWhere, double atX, double atY)
	{
		probe.log("**この図面を閉じないでください。** 次の 2 手で答えが出ます:");
		if (knowWhere)
			probe.log("  1. 座標 (" + DimAssocNum(atX) + ", " + DimAssocNum(atY) +
					  ") にある横向きの 3,000 の寸法——その**左端の縦棒**を");
		else
			probe.log("  1. 原点のすぐ上にある、横向きの 3,000 の寸法——その**左端の縦棒**を");
		probe.log("     画面でつまんで**右へ動かす**（どれだけでも構いません）");
		probe.log("  2. **同じ図面のまま**、このプローブをもう一度走らせる");
		probe.log("");
		probe.log("**新しい図面を開かないでください**——開くと 1 組を作り直すだけになり、");
		probe.log("判定に入れません（ここまで 2 回それで空振りしました。すみません）。");
		probe.log("判定はプローブが出すので、ログを貼る必要も数字を読む必要もありません。");
	}
} // namespace

VW_PROBE("dim-association", "寸法の関連付け: 画面でドラッグしたら追うか",
		 "原点のすぐ上に試験の 1 組を置き、次の実行で「線を動かしたぶん測点が追ったか」を"
		 "プローブ自身が判定する（同じ図面で 2 回走らせる）")
{
	MCObjectHandle layer = gSDK->GetActiveLayer();
	if (layer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した（アクティブレイヤが無い）");
		return;
	}

	MCObjectHandle dim = gSDK->GetNamedObject(TXString(kDimAssocDimName));
	MCObjectHandle lineA = gSDK->GetNamedObject(TXString(kDimAssocLineAName));
	MCObjectHandle lineB = gSDK->GetNamedObject(TXString(kDimAssocLineBName));

	// =======================================================================
	// 1 組がまだ無い＝1 回目。作って、お願いを出して終わる。
	if (dim == nil || lineA == nil || lineB == nil)
	{
		probe.log("== この図面には試験の 1 組がまだありません。作ります ==");
		probe.log("");
		DimAssocAsk(probe, false, 0, 0);
		probe.log("");

		// 門は varAssociateDims(28) ただ 1 つ（2 巡目で確定）。走り出しの値を読み、
		// off なら関連付けが成立しないので、その場で立てて**最後に書き戻す**。
		union
		{
			Boolean asBoolean;
			Sint32 asWide;
		} pref;
		pref.asWide = 0;
		gSDK->GetProgramVariable(varAssociateDims, &pref);
		const Boolean startPref = pref.asBoolean;
		probe.log("走り出しの varAssociateDims(28) = " + std::string(startPref ? "1" : "0"));
		if (!startPref)
		{
			Boolean on = 1;
			gSDK->SetProgramVariable(varAssociateDims, &on);
			probe.log("  off だったので、関連付けのあいだだけ 1 に立てます（最後に戻します）。");
		}

		lineA = gSDK->CreateLine(WorldPt(kDimAssocX1, kDimAssocY),
								 WorldPt(kDimAssocX1, kDimAssocY - 500));
		lineB = gSDK->CreateLine(WorldPt(kDimAssocX2, kDimAssocY),
								 WorldPt(kDimAssocX2, kDimAssocY - 500));
		dim =
			gSDK->CreateLinearDimension(WorldPt(kDimAssocX1, kDimAssocY),
										WorldPt(kDimAssocX2, kDimAssocY), 600, 0, Vector2(0, 0), 0);
		if (lineA == nil || lineB == nil || dim == nil)
		{
			probe.fail("1 組を作れなかった（CreateLine / CreateLinearDimension が nil）");
			return;
		}

		gSDK->DeselectAll();
		gSDK->SelectObject(lineA, true);
		gSDK->SelectObject(lineB, true);
		gSDK->AssociateLinearDimension(dim, false);
		gSDK->DeselectAll();

		gSDK->SetObjectName(lineA, TXString(kDimAssocLineAName));
		gSDK->SetObjectName(lineB, TXString(kDimAssocLineBName));
		gSDK->SetObjectName(dim, TXString(kDimAssocDimName));
		gSDK->CreateTextBlock(TXString("↓この左の縦棒を右へドラッグしてから、"
									   "同じ図面でもう一度プローブを走らせてください"),
							  WorldPt(kDimAssocX1, kDimAssocY + 1200), false, 0);

		probe.log("");
		probe.log("作りました（原点のすぐ上、y = " + DimAssocNum(kDimAssocY) + "）。");
		probe.log("  寸法: " + DimAssocConstraintState(dim));
		probe.log("  線A: " + DimAssocConstraintState(lineA));
		probe.log("  → 関連付けが成立しています（ここが「いいえ」なら、この先を読む意味は"
				  "ありません）。");

		if (!startPref)
		{
			Boolean back = 0;
			gSDK->SetProgramVariable(varAssociateDims, &back);
			probe.log("  varAssociateDims(28) を 0 へ戻しました。");
		}
		probe.log("");
		DimAssocAsk(probe, true, kDimAssocX1, kDimAssocY);
		return;
	}

	// =======================================================================
	// 1 組がある＝2 回目。**判定するだけ**（図面は触らない）。
	probe.log("== 試験の 1 組が見つかりました。判定します ==");

	double xA = 0;
	double xB = 0;
	WorldPt start;
	WorldPt end;
	const bool gotA = DimAssocStubX(lineA, xA);
	const bool gotB = DimAssocStubX(lineB, xB);
	const bool gotDim =
		DimAssocPointValue(dim, ovDimStartPt, start) && DimAssocPointValue(dim, ovDimEndPt, end);

	probe.log("  線A の x = " + (gotA ? DimAssocNum(xA) : std::string("(読めない)")));
	probe.log("  線B の x = " + (gotB ? DimAssocNum(xB) : std::string("(読めない)")) +
			  "（触らない側。線A との間隔が " + DimAssocNum(kDimAssocSpan) +
			  " のままなら、線A はまだ動いていない）");
	probe.log("  寸法の測点 = " + DimAssocReadPoint(dim, ovDimStartPt) + " → " +
			  DimAssocReadPoint(dim, ovDimEndPt));
	probe.log("  寸法: " + DimAssocConstraintState(dim));
	probe.log("  線A: " + DimAssocConstraintState(lineA));
	probe.log("");

	if (!gotA || !gotB || !gotDim)
	{
		probe.fail("値が読めないので判定できない");
		return;
	}

	const double span = xB - xA;
	if (std::fabs(span - kDimAssocSpan) < 0.5)
	{
		probe.log("★ **線A はまだ動いていません**（線B との間隔が " + DimAssocNum(span) +
				  " のまま）。");
		probe.log("");
		DimAssocAsk(probe, true, xA, static_cast<double>(start.y));
		return;
	}

	probe.log("線A は動いています（線B との間隔が " + DimAssocNum(kDimAssocSpan) + " → " +
			  DimAssocNum(span) + "）。");
	if (std::fabs(static_cast<double>(start.x) - xA) < 0.5)
	{
		probe.log("★ **追従した。** 画面で動かしたぶん、寸法の測点も動いています");
		probe.log("   （測点の始点 " + DimAssocNum(static_cast<double>(start.x)) + " ＝ 線A の x " +
				  DimAssocNum(xA) + "）。");
		probe.log("   → 画面の操作は、SDK と違って**自分で制約を解いている**。");
	}
	else
	{
		probe.log("★ **追従していない。** 線A は動いたのに、寸法の測点は付いていっていません");
		probe.log("   （測点の始点 " + DimAssocNum(static_cast<double>(start.x)) + " ≠ 線A の x " +
				  DimAssocNum(xA) + "）。");
		probe.log("   → 追従には SDK 側の UpdateConstraintModel が要る。");
	}
	probe.log("");
	probe.log("**これで #138 は終わりです。もう頼むことはありません。ありがとうございました。**");
}
