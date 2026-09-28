//
//	probes/runtime/dim-association/probe.cpp
//
//	[issue #138] ISDK::AssociateLinearDimension をどう呼べば、図形が動いたときに寸法が
//	追従するのか。
//
//	【1 巡目で確定したこと】（PR #139 の 1 本目のログ＋利用者が送ってくれたダイアログ）
//
//	  * **AssociateLinearDimension はちゃんと関連付けている。** 呼ぶ前後で寸法も相手の
//	    線分も HasConstraint が いいえ → はい、補助オブジェクトの型が [76] → [110]
//	    （kConstraintNode）。#134 の「8 通り空振り」は関連付けの失敗ではなかった。
//	  * **追従しなかったのは、制約を解く段を呼んでいなかったから。** 効く手順は
//	      CreateConstraintModel(nil, true) → MoveObject → UpdateConstraintModel()
//	    または
//	      BuildConstraintModelForObject → MoveObject → RecordModified… → Update
//	    どちらも測点が線分に追った（0 → 1200）。MoveObject + ResetObject だけでは追わない。
//	  * **CreateConstraintModel(線分, false) + AddToConstraintModel(寸法) は使ってはいけない。**
//	    VW が「関連する拘束を削除しますか？」を出し、拘束が消える（補助オブジェクトの型が
//	    [90] = kUndoPlaceholderNode になった）。**この 2 組をこの巡から外した**
//	    ——利用者にダイアログを押させないため（抑止する環境設定は SDK に無い）。
//	  * **関連付けの正体は「同一点上（coincident）」の拘束。** 上のダイアログに続く
//	    「拘束確認」が、タイプ「同一点上」／カテゴリ「寸法」と名乗った。
//	  * **文書環境設定が門になっている。** varAssociateDims(28) と
//	    varAutoAssociateDims(134) を**両方 off** にして呼ぶと、HasConstraint は いいえ の
//	    まま＝何も起きない。
//	  * 通らなかった道: GetClosestPt が**寸法に対して -1（型が非対応）**を返すので
//	    SetBinaryConstraint で自分で coincident を張れない。
//	    SetHorizontalDimensionConstraint は寸法図形を作らない（図形数が増えない）。
//
//	【この 2 巡目が取りに行くもの】——残り 2 点だけ
//
//	  1. **画面でドラッグしたら追うか。** 1 巡目が固定座標に残した名前付きの 1 組を、
//	     利用者が画面で動かした後に読む。**判定はプローブがやる**ので、目視の報告も
//	     ログの貼り付けも要らない（== 1b ==）。
//	  2. **どちらの環境設定が門なのか。** 1 巡目は「両方 on」と「両方 off」しか見ていない。
//	     4 通りを総当たりして、片方だけで足りるのかを決める（== 2 ==）。
//	     プラグインは利用者の文書設定を勝手に変えたくないので、**どちらを見ればよいか**を
//	     知る必要がある。
//
//	この巡はダイアログを出さない（出す経路を外した）。走り出しの設定値は最後に戻す。
//

#include "Probe.h"

#include <cmath>
#include <string>

namespace
{
	// 短い名前・ありふれた名前は SDK と OS のヘッダとぶつかるので接頭辞を付ける
	// （probes/runtime/README.md「短い名前・ありふれた名前を使わない」）。

	// 寸法のオブジェクト型（Objs.TDType.h:129 の dimHeaderNode。Findings「寸法」）。
	const short kDimAssocDimHeaderNode = 63;
	// 制約ノードのオブジェクト型（Objs.TDType.h:176 の kConstraintNode）。
	const short kDimAssocConstraintNode = 110;

	// 画面でドラッグしてもらう 1 組の**固定座標**。次の実行はここと今の位置を
	// 突き合わせるだけで「追従したか」を判定できる（目視が要らない）。
	const WorldCoord kDimAssocDragY = -100000;
	const WorldCoord kDimAssocDragX1 = 0;
	const WorldCoord kDimAssocDragX2 = 3000;
	const char* const kDimAssocDragDimName = "#138 寸法（触らない）";
	const char* const kDimAssocDragLineAName = "#138 線A（これを右へ動かす）";
	const char* const kDimAssocDragLineBName = "#138 線B（触らない）";

	std::string DimAssocNum(double value)
	{
		std::string s = std::to_string(value);
		const std::string::size_type dot = s.find('.');
		if (dot != std::string::npos && s.size() > dot + 4)
			s.erase(dot + 4);
		return s;
	}

	std::string DimAssocInt(long long value)
	{
		return std::to_string(value);
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

	// -----------------------------------------------------------------------
	// 文書環境設定（Boolean selector）。**4 バイトを 0 で埋めてから渡す**ので、
	// VW が 1 バイトしか書かなくても残りは 0 のまま読める。
	bool DimAssocGetPref(short selector)
	{
		union
		{
			Boolean asBoolean;
			Sint32 asWide;
		} u;
		u.asWide = 0;
		if (!gSDK->GetProgramVariable(selector, &u))
			return false;
		return u.asBoolean != 0;
	}

	void DimAssocSetPref(short selector, bool on)
	{
		Boolean value = on ? 1 : 0;
		gSDK->SetProgramVariable(selector, &value);
	}

	// -----------------------------------------------------------------------
	// 「関連付いたか」を**追従を見る前に**読む。AssociateLinearDimension は void
	// なので、ここが戻り値の代わりになる（1 巡目で HasConstraint と制約ノードが
	// 一致することを確かめてある）。
	std::string DimAssocConstraintState(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		const bool has = gSDK->HasConstraint(h) != 0;
		const MCObjectHandle node = gSDK->FindAuxObject(h, kDimAssocConstraintNode);
		std::string s = "HasConstraint=" + DimAssocYesNo(has) +
						" / 制約ノード(110)=" + DimAssocYesNo(node != nil);

		std::string aux;
		int count = 0;
		MCObjectHandle it = gSDK->FirstAuxObject(h);
		while (it != nil && count < 20)
		{
			++count;
			if (!aux.empty())
				aux += ",";
			aux += DimAssocInt(gSDK->GetObjectTypeN(it));
			it = gSDK->NextAuxObject(it, 0);
		}
		s += " / 補助オブジェクトの型=[" + (aux.empty() ? std::string("なし") : aux) + "]";
		return s;
	}

	// 線分（縦棒）の x。縦棒なので外接矩形の左右が同じ値になる。
	bool DimAssocStubX(MCObjectHandle line, double& outX)
	{
		WorldRect bounds;
		if (line == nil || !gSDK->GetObjectBounds(line, bounds))
			return false;
		outX = static_cast<double>(bounds.left);
		return true;
	}

	// -----------------------------------------------------------------------
	// 環境設定の 1 通りを試す。**効くと分かっている引き金**（CreateConstraintModel(nil,
	// true) → MoveObject → UpdateConstraintModel）だけを使うので、ここで追従しなければ
	// 原因は設定にある。
	void DimAssocTryPrefs(vwprobe::Report& probe, WorldCoord y, bool assoc28, bool auto134)
	{
		const std::string label = std::string("varAssociateDims(28)=") + (assoc28 ? "1" : "0") +
								  " / varAutoAssociateDims(134)=" + (auto134 ? "1" : "0");
		probe.log("");
		probe.log("--- " + label);

		DimAssocSetPref(varAssociateDims, assoc28);
		DimAssocSetPref(varAutoAssociateDims, auto134);
		probe.log(
			"  書いた後の読み戻し: 28=" + DimAssocInt(DimAssocGetPref(varAssociateDims) ? 1 : 0) +
			" / 134=" + DimAssocInt(DimAssocGetPref(varAutoAssociateDims) ? 1 : 0));

		MCObjectHandle lineA = gSDK->CreateLine(WorldPt(0, y), WorldPt(0, y - 500));
		MCObjectHandle lineB = gSDK->CreateLine(WorldPt(3000, y), WorldPt(3000, y - 500));
		MCObjectHandle dim =
			gSDK->CreateLinearDimension(WorldPt(0, y), WorldPt(3000, y), 600, 0, Vector2(0, 0), 0);
		if (lineA == nil || lineB == nil || dim == nil)
		{
			probe.fail(label + ": 1 組を作れなかった");
			return;
		}

		gSDK->DeselectAll();
		gSDK->SelectObject(lineA, true);
		gSDK->SelectObject(lineB, true);
		gSDK->AssociateLinearDimension(dim, false);
		gSDK->DeselectAll();
		probe.log("  AssociateLinearDimension(寸法, false) を呼んだ");
		probe.log("  寸法: " + DimAssocConstraintState(dim));

		// 効くと分かっている引き金だけを使う。
		gSDK->DeselectAll();
		gSDK->SelectObject(lineA, true);
		gSDK->CreateConstraintModel(nil, true);
		gSDK->MoveObject(lineA, 1200, 0);
		const bool solved = gSDK->UpdateConstraintModel() != 0;
		gSDK->DeselectAll();
		probe.log("  線分A を +1200 平行移動 → UpdateConstraintModel = " + DimAssocYesNo(solved));

		WorldPt start;
		if (!DimAssocPointValue(dim, ovDimStartPt, start))
		{
			probe.log("  ★ 判定できない（測点が読めない）");
			return;
		}
		const bool followed = std::fabs(static_cast<double>(start.x) - 1200.0) < 0.5;
		probe.log("  ★ 判定: 始点の x = " + DimAssocNum(static_cast<double>(start.x)) +
				  "（追従すれば 1200、しなければ 0）→ **" +
				  (followed ? "追従した" : "追従していない") + "**");
	}
} // namespace

VW_PROBE("dim-association", "寸法の関連付け（AssociateLinearDimension）はどうすれば効くか",
		 "画面でドラッグした結果を読み、どちらの文書環境設定が関連付けの門なのかを総当たりで決める")
{
	MCObjectHandle layer = gSDK->GetActiveLayer();
	if (layer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した（アクティブレイヤが無い）");
		return;
	}

	// 走り出しの設定値。**最後にここへ戻す**（利用者の文書設定を勝手に変えないため）。
	const bool startPref28 = DimAssocGetPref(varAssociateDims);
	const bool startPref134 = DimAssocGetPref(varAutoAssociateDims);
	probe.log("== 0. 走り出しの文書環境設定 ==");
	probe.log("  varAssociateDims(28) = " + DimAssocInt(startPref28 ? 1 : 0) +
			  " / varAutoAssociateDims(134) = " + DimAssocInt(startPref134 ? 1 : 0));
	probe.log("  （この値は最後に書き戻します）");

	// =======================================================================
	// 1. 図面にある寸法を全部読む。**寸法ツールで作った寸法があれば、ここで
	//    プローブ製との差が出る**（制約ノードが立っているか）。
	probe.log("");
	probe.log("== 1. いま図面にある寸法（型 63）を全部読む ==");
	long long dimCount = 0;
	{
		int guard = 0;
		MCObjectHandle it = gSDK->FirstMemberObj(layer);
		while (it != nil && guard < 20000)
		{
			++guard;
			if (gSDK->GetObjectTypeN(it) == kDimAssocDimHeaderNode)
			{
				++dimCount;
				TXString name;
				gSDK->GetObjectName(it, name);
				probe.log("  寸法 " + DimAssocInt(dimCount) + " \"" +
						  std::string(static_cast<const char*>(name)) +
						  "\" 測点 = " + DimAssocReadPoint(it, ovDimStartPt) + " → " +
						  DimAssocReadPoint(it, ovDimEndPt));
				probe.log("    " + DimAssocConstraintState(it));
			}
			it = gSDK->NextObject(it);
		}
	}
	probe.log("  合計 " + DimAssocInt(dimCount) + " 本。");

	// =======================================================================
	// 1b. **この巡の本題その 1。** 前回が残した 1 組を、利用者が画面で動かした後に読む。
	probe.log("");
	probe.log("== 1b. 画面でドラッグする試験の判定 ==");
	MCObjectHandle dragDim = gSDK->GetNamedObject(TXString(kDimAssocDragDimName));
	MCObjectHandle dragLineA = gSDK->GetNamedObject(TXString(kDimAssocDragLineAName));
	MCObjectHandle dragLineB = gSDK->GetNamedObject(TXString(kDimAssocDragLineBName));
	const bool dragRigExists = (dragDim != nil && dragLineA != nil && dragLineB != nil);
	bool dragAnswered = false;
	if (!dragRigExists)
	{
		probe.log("  この図面には試験の 1 組がまだありません（下の「== 3 ==」で作ります）。");
	}
	else
	{
		double xA = 0;
		double xB = 0;
		WorldPt start;
		WorldPt end;
		const bool gotA = DimAssocStubX(dragLineA, xA);
		const bool gotB = DimAssocStubX(dragLineB, xB);
		const bool gotDim = DimAssocPointValue(dragDim, ovDimStartPt, start) &&
							DimAssocPointValue(dragDim, ovDimEndPt, end);
		probe.log("  線A の今の x = " + (gotA ? DimAssocNum(xA) : std::string("(読めない)")) +
				  "（作ったときは " + DimAssocNum(kDimAssocDragX1) + "）");
		probe.log("  線B の今の x = " + (gotB ? DimAssocNum(xB) : std::string("(読めない)")) +
				  "（作ったときは " + DimAssocNum(kDimAssocDragX2) + "）");
		if (gotDim)
			probe.log("  寸法の測点 = " + DimAssocReadPoint(dragDim, ovDimStartPt) + " → " +
					  DimAssocReadPoint(dragDim, ovDimEndPt));
		probe.log("  寸法: " + DimAssocConstraintState(dragDim));
		probe.log("  線A: " + DimAssocConstraintState(dragLineA));

		if (!gotA || !gotDim)
		{
			probe.log("  ★ 判定できない（値が読めない）");
		}
		else if (std::fabs(xA - static_cast<double>(kDimAssocDragX1)) < 0.5)
		{
			probe.log("  ★ 線A はまだ動いていません。**線A を画面でつまんで右へ動かし、");
			probe.log("     もう一度このプローブを走らせてください。**");
		}
		else if (std::fabs(static_cast<double>(start.x) - xA) < 0.5)
		{
			dragAnswered = true;
			probe.log("  ★ **追従した。** 線A を画面で動かしたぶん、寸法の測点も動いています。");
			probe.log("     → 画面の操作は、SDK と違って自分で制約を解いている。");
		}
		else
		{
			dragAnswered = true;
			probe.log("  ★ **追従していない。** 線A は動いたのに、寸法の測点は "
					  "作ったときの位置のままです。");
			probe.log("     → 画面でドラッグしても追わない。追従には SDK 側の "
					  "UpdateConstraintModel が要る。");
		}
	}

	// 以降で作るものが既にある図形と重ならないように、見つかった寸法の本数ぶん下へずらす。
	const WorldCoord bandTop = static_cast<WorldCoord>(-8000 * (dimCount + 1));

	// =======================================================================
	// 2. **この巡の本題その 2。** どちらの環境設定が門なのか。
	//    引き金は「効くと分かっているもの」に固定してあるので、追従しなければ設定のせい。
	probe.log("");
	probe.log("== 2. どちらの文書環境設定が関連付けの門なのか（4 通り）==");
	probe.log("引き金は 1 巡目で効くと分かった CreateConstraintModel(nil,true) →");
	probe.log("MoveObject → UpdateConstraintModel に固定。追従しなければ原因は設定にある。");
	{
		WorldCoord y = bandTop;
		DimAssocTryPrefs(probe, y, true, true);
		y -= 4000;
		DimAssocTryPrefs(probe, y, true, false);
		y -= 4000;
		DimAssocTryPrefs(probe, y, false, true);
		y -= 4000;
		DimAssocTryPrefs(probe, y, false, false);
	}

	// =======================================================================
	// 3. 画面でドラッグしてもらう 1 組（無ければ作る）。
	probe.log("");
	probe.log("== 3. 画面でドラッグしてもらう 1 組 ==");
	if (dragRigExists)
	{
		probe.log("  もうこの図面にあります（上の「== 1b ==」が判定しました）。作り直しません。");
	}
	else
	{
		// 作るときは設定を両方 on にしておく（関連付けが成立する側）。
		DimAssocSetPref(varAssociateDims, true);
		DimAssocSetPref(varAutoAssociateDims, true);
		MCObjectHandle lineA = gSDK->CreateLine(WorldPt(kDimAssocDragX1, kDimAssocDragY),
												WorldPt(kDimAssocDragX1, kDimAssocDragY - 500));
		MCObjectHandle lineB = gSDK->CreateLine(WorldPt(kDimAssocDragX2, kDimAssocDragY),
												WorldPt(kDimAssocDragX2, kDimAssocDragY - 500));
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(kDimAssocDragX1, kDimAssocDragY),
														 WorldPt(kDimAssocDragX2, kDimAssocDragY),
														 600, 0, Vector2(0, 0), 0);
		if (lineA == nil || lineB == nil || dim == nil)
		{
			probe.fail("3: 1 組を作れなかった");
		}
		else
		{
			gSDK->DeselectAll();
			gSDK->SelectObject(lineA, true);
			gSDK->SelectObject(lineB, true);
			gSDK->AssociateLinearDimension(dim, false);
			gSDK->DeselectAll();
			gSDK->SetObjectName(lineA, TXString(kDimAssocDragLineAName));
			gSDK->SetObjectName(lineB, TXString(kDimAssocDragLineBName));
			gSDK->SetObjectName(dim, TXString(kDimAssocDragDimName));
			gSDK->CreateTextBlock(TXString("←この縦棒を右へドラッグしてから、もう一度"
										   "プローブを走らせてください"),
								  WorldPt(kDimAssocDragX1 + 300, kDimAssocDragY - 1200), false, 0);
			probe.log("  作りました。寸法: " + DimAssocConstraintState(dim));
		}
	}

	// 走り出しの設定値へ戻す。
	DimAssocSetPref(varAssociateDims, startPref28);
	DimAssocSetPref(varAutoAssociateDims, startPref134);
	probe.log("");
	probe.log(
		"走り出しの設定へ戻しました: 28=" + DimAssocInt(DimAssocGetPref(varAssociateDims) ? 1 : 0) +
		" / 134=" + DimAssocInt(DimAssocGetPref(varAutoAssociateDims) ? 1 : 0));

	probe.log("");
	probe.log("== お願い ==");
	if (dragAnswered)
	{
		probe.log("**もう頼むことはありません。** ドラッグの試験は上の「== 1b ==」で");
		probe.log("答えが出ています。ありがとうございました。");
	}
	else
	{
		probe.log("**図面のいちばん下にある、文字が添えてある横向きの 3,000 の寸法**——その");
		probe.log("左端の縦棒（線A）を画面でつまんで右へ動かし、**このプローブをもう一度**");
		probe.log("走らせてください。上の「== 1b ==」が追従したかを自分で判定して出します。");
	}
	probe.log("");
	probe.log("（この巡はダイアログを出しません。前回「関連する拘束を削除しますか？」を");
	probe.log("出した経路——CreateConstraintModel(線分,false) + AddToConstraintModel——は、");
	probe.log("使ってはいけないと分かったので外しました。お手数をおかけしました。）");
}
