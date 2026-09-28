//
//	probes/runtime/dim-association/probe.cpp
//
//	[issue #138] ISDK::AssociateLinearDimension をどう呼べば、図形が動いたときに寸法が
//	追従するのか。「こう呼べば効く」か「SDK からは使えない」のどちらかに確定させる。
//
//	【#134 で分かっていること】（Findings「寸法」）
//
//	  寸法の測点とちょうど重なる端点を持つ線分を立て、AssociateLinearDimension を呼んで
//	  から線分を MoveObject で動かし ResetObject を呼ぶ——を 8 通り（h＝寸法／図形 ×
//	  selectedObjectsMode × dimType）試して、**どれも測点が 1mm も動かなかった**。
//	  AssociateLinearDimension は void なので、関連付いたかを知る手立ても無かった。
//
//	【この調査が新しく持ち込むもの】——どれも #134 では読んでも呼んでもいない
//
//	  1. **h は寸法である**とヘッダに書いてあった（APIBase.Legacy.Defs.h:6559）。
//	     "Associates a linear dimension with an object when the dimension's endpoints are
//	      coincident with objects in the drawing. When selectedObjectsMode is true, only
//	      selected objects will be checked…"
//	     つまり **selectedObjectsMode=false は「図面の全図形を調べる」**という意味で、
//	     選択が足りないという筋は消える。図形を h に渡す 4 通りも試さなくてよい。
//
//	  2. **文書環境設定**——varAssociateDims(28) と varAutoAssociateDims(134)。どちらも
//	     GetProgramVariable / SetProgramVariable の Boolean selector。off のまま呼んで
//	     いたなら 8 通りすべてが空振りになる。
//
//	  3. **関連付けは「パラメトリック制約」として持たれているはず**という見当。SDK には
//	     制約の一式があり、**そこに更新の引き金がある**:
//	       CreateConstraintModel(obj, useSelection)  図形を動かす**前**に呼ぶ
//	       AddToConstraintModel(obj)                 模型へ図形を足す
//	       UpdateConstraintModel()                   制約を解く（false なら undo せよ）
//	     UpdateConstraintModel の説明が決め手:「模型に入れた図形の現在位置へ幾何を
//	     合わせ、そのうえで制約を解こうとする」。**#134 は MoveObject と ResetObject
//	     しか呼んでいない**ので、解く段が一度も走っていない可能性がある。
//
//	  4. **関連付いたかを機械で読む口がある**:
//	       HasConstraint(obj)          「その図形が制約ノードを持つか」
//	       FindAuxObject(obj, 110)     制約ノードは kConstraintNode = 110
//	     void の戻り値の代わりになるので、追従を見る前に成否が分かる。
//
//	  5. **寸法制約を直に張る口**（VW 2021 で足された。説明文は無い）:
//	       SetHorizontalDimensionConstraint / SetVerticalDimensionConstraint /
//	       DeleteDimensionConstraints
//	     および汎用の SetBinaryConstraint（type 1 = coincident）。
//	     AssociateLinearDimension が効かなくても、**こちらで追従が作れれば用は足りる**。
//
//	  6. **画面でドラッグした結果を、次の実行が機械で読む。** 固定座標に名前付きの 1 組を
//	     残すので、利用者は「動かして、もう一度走らせる」だけでよい——目視の報告も
//	     ログの貼り付けも要らない。
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

	// SetBinaryConstraint の type（APIBase.Legacy.Defs.h:5355 の説明より）。
	const short kDimAssocBinaryCoincident = 1;

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
	// VW が 1 バイトしか書かなくても残りは 0 のまま読める（selector の実際の幅が
	// ヘッダに書かれていないため）。
	std::string DimAssocGetPref(vwprobe::Report& probe, short selector, const char* name)
	{
		union
		{
			Boolean asBoolean;
			Sint32 asWide;
		} u;
		u.asWide = 0;
		const bool ok = gSDK->GetProgramVariable(selector, &u) != 0;
		std::string line = std::string("  ") + name + "(" + DimAssocInt(selector) + ") = ";
		if (!ok)
		{
			line += "(GetProgramVariable が false)";
			probe.log(line);
			return "?";
		}
		line += DimAssocInt(u.asBoolean) + "（4 バイトとして " + DimAssocInt(u.asWide) + "）";
		probe.log(line);
		return DimAssocInt(u.asBoolean);
	}

	void DimAssocSetPref(vwprobe::Report& probe, short selector, const char* name, bool on)
	{
		Boolean value = on ? 1 : 0;
		const bool ok = gSDK->SetProgramVariable(selector, &value) != 0;
		probe.log(std::string("  ") + name + " へ " + (on ? "1" : "0") +
				  " を書いた: SetProgramVariable = " + DimAssocYesNo(ok));
		DimAssocGetPref(probe, selector, name);
	}

	void DimAssocSetBothPrefs(vwprobe::Report& probe, bool on)
	{
		DimAssocSetPref(probe, varAssociateDims, "varAssociateDims", on);
		DimAssocSetPref(probe, varAutoAssociateDims, "varAutoAssociateDims", on);
	}

	// -----------------------------------------------------------------------
	// 「関連付いたか」を**追従を見る前に**読む。AssociateLinearDimension は void
	// なので、ここが戻り値の代わりになる。
	std::string DimAssocConstraintState(MCObjectHandle h)
	{
		if (h == nil)
			return "(nil)";
		const bool has = gSDK->HasConstraint(h) != 0;
		const MCObjectHandle node = gSDK->FindAuxObject(h, kDimAssocConstraintNode);
		std::string s = "HasConstraint=" + DimAssocYesNo(has) +
						" / 制約ノード(110)=" + DimAssocYesNo(node != nil);

		// 補助オブジェクトを一通り歩いて、型を並べる（何が付いたのかを見るため）。
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

	// 寸法 1 本の素性。**測点と向きと制約の有無**だけに絞る（この調査に要るのはそこ）。
	void DimAssocReportDim(vwprobe::Report& probe, MCObjectHandle dim, const std::string& when)
	{
		if (dim == nil)
		{
			probe.log("  [" + when + "] 寸法が nil");
			return;
		}
		probe.log("  [" + when + "]");
		probe.log("    測点 = " + DimAssocReadPoint(dim, ovDimStartPt) + " → " +
				  DimAssocReadPoint(dim, ovDimEndPt) +
				  " / 向き = " + DimAssocReadPoint(dim, ovDimDirection));
		probe.log("    " + DimAssocConstraintState(dim));
	}

	// -----------------------------------------------------------------------
	// 1 組（寸法 1 本＋その測点に端点を重ねた縦棒 2 本）を作る。
	struct DimAssocRig
	{
		MCObjectHandle dim = nil;
		MCObjectHandle lineA = nil; // 始点側
		MCObjectHandle lineB = nil; // 終点側
		WorldCoord y = 0;
	};

	bool DimAssocBuildRig(vwprobe::Report& probe, WorldCoord y, DimAssocRig& outRig)
	{
		outRig.y = y;
		outRig.lineA = gSDK->CreateLine(WorldPt(0, y), WorldPt(0, y - 500));
		outRig.lineB = gSDK->CreateLine(WorldPt(3000, y), WorldPt(3000, y - 500));
		outRig.dim =
			gSDK->CreateLinearDimension(WorldPt(0, y), WorldPt(3000, y), 600, 0, Vector2(0, 0), 0);
		if (outRig.lineA == nil || outRig.lineB == nil || outRig.dim == nil)
		{
			probe.fail("1 組を作れなかった（CreateLine / CreateLinearDimension が nil）");
			return false;
		}
		return true;
	}

	// 測点が線分に追ったかを 1 行で判定する。**追従の有無はここだけで読む**
	// ——「関連付いたが向きが変わらなかった」と混ざらないよう、動かすのは平行移動。
	void DimAssocVerdict(vwprobe::Report& probe, const DimAssocRig& rig, double movedBy)
	{
		WorldPt start;
		WorldPt end;
		if (!DimAssocPointValue(rig.dim, ovDimStartPt, start) ||
			!DimAssocPointValue(rig.dim, ovDimEndPt, end))
		{
			probe.log("    ★ 判定できない（測点が読めない）");
			return;
		}
		const double expectedIfFollowed = movedBy;
		const double actual = static_cast<double>(start.x) - 0.0;
		const bool followed = std::fabs(actual - expectedIfFollowed) < 0.5;
		probe.log(std::string("    ★ 判定: 始点の x = ") + DimAssocNum(actual) + "（追従すれば " +
				  DimAssocNum(expectedIfFollowed) + "、追従しなければ 0）→ **" +
				  (followed ? "追従した" : "追従していない") + "**");
		probe.log("       終点の x = " + DimAssocNum(static_cast<double>(end.x)) +
				  "（線分を動かしていない側。3000 のままが正しい）");
	}

	// -----------------------------------------------------------------------
	// 更新の引き金の種類。**ここが #134 との差**。
	enum DimAssocTrigger
	{
		kDimAssocTriggerResetOnly,	 // #134 の再現（対照）
		kDimAssocTriggerModelSelect, // CreateConstraintModel(nil, true)
		kDimAssocTriggerModelObject, // CreateConstraintModel(線分, false) + AddToConstraintModel
		kDimAssocTriggerBuildRecord	 // BuildConstraintModelForObject + RecordModified…
	};

	const char* DimAssocTriggerName(DimAssocTrigger trigger)
	{
		switch (trigger)
		{
		case kDimAssocTriggerResetOnly:
			return "MoveObject + ResetObject だけ（#134 の再現＝対照）";
		case kDimAssocTriggerModelSelect:
			return "CreateConstraintModel(nil, true) → MoveObject → UpdateConstraintModel";
		case kDimAssocTriggerModelObject:
			return "CreateConstraintModel(線分, false) + AddToConstraintModel(寸法) → "
				   "MoveObject → UpdateConstraintModel";
		case kDimAssocTriggerBuildRecord:
			return "BuildConstraintModelForObject + RecordModifiedObjectInConstraintModel → "
				   "MoveObject → UpdateConstraintModel";
		}
		return "?";
	}

	// 1 通りを試す。**関連付けの成否（HasConstraint）を、追従を見る前に読む**ので、
	// 「関連付いていない」と「関連付いたが更新が走らない」が切り分けられる。
	void DimAssocTry(vwprobe::Report& probe, WorldCoord y, bool selectedObjectsMode,
					 DimAssocTrigger trigger, bool prefsOn)
	{
		const std::string label = std::string("selectedObjectsMode=") +
								  (selectedObjectsMode ? "true" : "false") +
								  " / 引き金=" + DimAssocTriggerName(trigger) +
								  " / 環境設定=" + (prefsOn ? "両方 on" : "両方 off");
		probe.log("");
		probe.log("--- " + label);

		DimAssocSetBothPrefs(probe, prefsOn);

		DimAssocRig rig;
		if (!DimAssocBuildRig(probe, y, rig))
			return;
		DimAssocReportDim(probe, rig.dim, "作った直後");
		probe.log("    線分A: " + DimAssocConstraintState(rig.lineA));

		// 関連付ける。h は**寸法**（ヘッダがそう書いている）。
		gSDK->DeselectAll();
		gSDK->SelectObject(rig.lineA, true);
		gSDK->SelectObject(rig.lineB, true);
		gSDK->AssociateLinearDimension(rig.dim, selectedObjectsMode);
		probe.log("    AssociateLinearDimension(寸法, " +
				  std::string(selectedObjectsMode ? "true" : "false") + ") を呼んだ（void）");
		probe.log("    寸法: " + DimAssocConstraintState(rig.dim));
		probe.log("    線分A: " + DimAssocConstraintState(rig.lineA));

		// 線分A だけを +1200 平行移動する。追従すれば始点の x が 1200 になる。
		const double kDimAssocMove = 1200.0;
		switch (trigger)
		{
		case kDimAssocTriggerResetOnly:
			gSDK->MoveObject(rig.lineA, static_cast<WorldCoord>(kDimAssocMove), 0);
			gSDK->ResetObject(rig.dim);
			break;

		case kDimAssocTriggerModelSelect:
			// 動かす前に模型を作る（選択されている線分 2 本と、それらに制約された
			// 図形＝寸法が模型に入るはず）。
			gSDK->DeselectAll();
			gSDK->SelectObject(rig.lineA, true);
			gSDK->CreateConstraintModel(nil, true);
			gSDK->MoveObject(rig.lineA, static_cast<WorldCoord>(kDimAssocMove), 0);
			probe.log(
				"    UpdateConstraintModel = " + DimAssocYesNo(gSDK->UpdateConstraintModel() != 0) +
				"（false なら制約が解けなかったという意味）");
			break;

		case kDimAssocTriggerModelObject:
			gSDK->DeselectAll();
			gSDK->CreateConstraintModel(rig.lineA, false);
			gSDK->AddToConstraintModel(rig.dim);
			gSDK->MoveObject(rig.lineA, static_cast<WorldCoord>(kDimAssocMove), 0);
			probe.log("    UpdateConstraintModel = " +
					  DimAssocYesNo(gSDK->UpdateConstraintModel() != 0));
			break;

		case kDimAssocTriggerBuildRecord:
			gSDK->DeselectAll();
			gSDK->BuildConstraintModelForObject(rig.lineA, true);
			gSDK->MoveObject(rig.lineA, static_cast<WorldCoord>(kDimAssocMove), 0);
			gSDK->RecordModifiedObjectInConstraintModel(rig.lineA, false);
			probe.log("    UpdateConstraintModel = " +
					  DimAssocYesNo(gSDK->UpdateConstraintModel() != 0));
			break;
		}
		gSDK->DeselectAll();
		probe.log("    線分A を +1200 平行移動した");
		DimAssocReportDim(probe, rig.dim, "動かした後");
		DimAssocVerdict(probe, rig, kDimAssocMove);
	}

	// -----------------------------------------------------------------------
	// レイヤ直下の図形の件数（新しい図形が生えたかを見るため）。
	long long DimAssocCountMembers(MCObjectHandle layer)
	{
		long long n = 0;
		MCObjectHandle it = gSDK->FirstMemberObj(layer);
		while (it != nil && n < 100000)
		{
			++n;
			it = gSDK->NextObject(it);
		}
		return n;
	}
} // namespace

VW_PROBE("dim-association", "寸法の関連付け（AssociateLinearDimension）はどうすれば効くか",
		 "文書環境設定と制約モデル（CreateConstraintModel / UpdateConstraintModel）を持ち込んで"
		 "関連付けを取り直し、画面でドラッグしてもらう 1 組を固定座標に残す")
{
	MCObjectHandle layer = gSDK->GetActiveLayer();
	if (layer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した（アクティブレイヤが無い）");
		return;
	}

	// =======================================================================
	// 0. 文書環境設定を読む。**#134 はここを読んでも書いてもいない。**
	probe.log("== 0. 文書環境設定（寸法の関連付け）==");
	probe.log("走り出しの値:");
	const std::string before28 = DimAssocGetPref(probe, varAssociateDims, "varAssociateDims");
	const std::string before134 =
		DimAssocGetPref(probe, varAutoAssociateDims, "varAutoAssociateDims");
	probe.log("→ 走り出しは varAssociateDims=" + before28 + " / varAutoAssociateDims=" + before134);
	probe.log("この 2 つが 0 なら、#134 の 8 通りはすべて「設定が off のまま」試していた");
	probe.log("ことになる（それだけで空振りの説明が付く）。");

	// =======================================================================
	// 1. 図面の棚卸し。**2 回目以降の実行では、ここが「画面で動かした後」になる。**
	probe.log("");
	probe.log("== 1. いま図面にある寸法（型 63）を全部読む ==");
	probe.log("寸法ツールで作った寸法がこの図面にあれば、プローブ製との差はここに出る");
	probe.log("（制約ノード・HasConstraint・補助オブジェクトの型）。");
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
				probe.log("");
				probe.log("  寸法 " + DimAssocInt(dimCount) + "（名前 \"" +
						  std::string(static_cast<const char*>(name)) + "\"）");
				probe.log("    測点 = " + DimAssocReadPoint(it, ovDimStartPt) + " → " +
						  DimAssocReadPoint(it, ovDimEndPt) +
						  " / 向き = " + DimAssocReadPoint(it, ovDimDirection));
				probe.log("    " + DimAssocConstraintState(it));
			}
			it = gSDK->NextObject(it);
		}
	}
	if (dimCount == 0)
		probe.log("  （寸法は 1 本も見つかりませんでした＝まっさらな図面です）");
	else
		probe.log("  合計 " + DimAssocInt(dimCount) + " 本。");

	// -----------------------------------------------------------------------
	// 1b. 前回の実行が残した「画面でドラッグしてもらう 1 組」の判定。
	//     **これが目視の代わり**——利用者は動かして走らせるだけでよい。
	probe.log("");
	probe.log("== 1b. 画面でドラッグする試験の判定 ==");
	MCObjectHandle dragDim = gSDK->GetNamedObject(TXString(kDimAssocDragDimName));
	MCObjectHandle dragLineA = gSDK->GetNamedObject(TXString(kDimAssocDragLineAName));
	MCObjectHandle dragLineB = gSDK->GetNamedObject(TXString(kDimAssocDragLineBName));
	const bool dragRigExists = (dragDim != nil && dragLineA != nil && dragLineB != nil);
	if (!dragRigExists)
	{
		probe.log("  この図面には試験の 1 組がまだありません（下の「== 4 ==」で作ります）。");
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
			probe.log("  ★ **追従した。** 線A を画面で動かしたぶん、寸法の測点も動いています。");
			probe.log("     → 関連付けは成立しており、引き金が画面の操作にしか無い、が確定。");
		}
		else
		{
			probe.log("  ★ **追従していない。** 線A は動いたのに、寸法の測点は "
					  "作ったときの位置のままです。");
			probe.log("     → 画面でドラッグしても追わない、が確定。");
		}
	}

	// 以降で作るものが既にある図形と重ならないように、見つかった寸法の本数ぶん下へずらす。
	const WorldCoord bandTop = static_cast<WorldCoord>(-8000 * (dimCount + 1));

	// =======================================================================
	// 2. AssociateLinearDimension × 更新の引き金。
	probe.log("");
	probe.log("== 2. AssociateLinearDimension を、環境設定と制約モデルを添えて取り直す ==");
	probe.log("各組で、追従を見る前に HasConstraint を読む——**関連付いていない** と");
	probe.log("**関連付いたが更新が走らない** を切り分けるため。");
	{
		WorldCoord y = bandTop;
		const DimAssocTrigger triggers[] = {kDimAssocTriggerResetOnly, kDimAssocTriggerModelSelect,
											kDimAssocTriggerModelObject,
											kDimAssocTriggerBuildRecord};
		for (int t = 0; t < 4; ++t)
		{
			for (int selMode = 1; selMode >= 0; --selMode)
			{
				DimAssocTry(probe, y, selMode != 0, triggers[t], true);
				y -= 4000;
			}
		}
		// 設定の効きを分けて見るための対照——同じ手順を、環境設定を両方 off にして。
		probe.log("");
		probe.log("--- ここから下は【対照】環境設定を両方 off にした同じ手順 ---");
		DimAssocTry(probe, y, false, kDimAssocTriggerModelSelect, false);
		y -= 4000;
		DimAssocTry(probe, y, false, kDimAssocTriggerResetOnly, false);
		y -= 4000;
		// 以後の節は設定 on で走らせたいので戻す。
		DimAssocSetBothPrefs(probe, true);
	}

	// =======================================================================
	// 3. 寸法制約を直に張る口（AssociateLinearDimension を経由しない道）。
	probe.log("");
	probe.log("== 3. 制約を直に張る（AssociateLinearDimension を通さない道）==");
	probe.log("AssociateLinearDimension が効かなくても、ここで追従が作れれば用は足りる。");

	// 3a. SetHorizontalDimensionConstraint（VW 2021。説明文が SDK に無い）
	{
		const WorldCoord y = static_cast<WorldCoord>(bandTop - 48000);
		probe.log("");
		probe.log("--- 3a. SetHorizontalDimensionConstraint(線分, pt1, pt2, distance, offset)");
		MCObjectHandle line = gSDK->CreateLine(WorldPt(0, y), WorldPt(3000, y));
		if (line == nil)
		{
			probe.fail("3a: CreateLine が nil を返した");
		}
		else
		{
			const long long before = DimAssocCountMembers(layer);
			const Boolean ok = gSDK->SetHorizontalDimensionConstraint(line, WorldPt(0, y),
																	  WorldPt(3000, y), 3000, 600);
			const long long after = DimAssocCountMembers(layer);
			probe.log("    戻り値 = " + DimAssocYesNo(ok != 0) + " / レイヤの図形数 " +
					  DimAssocInt(before) + " → " + DimAssocInt(after) + "（増えた分 " +
					  DimAssocInt(after - before) + "）");
			probe.log("    線分: " + DimAssocConstraintState(line));

			// 生えた寸法を探して、線分を動かしたときに追うかを見る。
			MCObjectHandle madeDim = nil;
			{
				int guard = 0;
				MCObjectHandle it = gSDK->FirstMemberObj(layer);
				while (it != nil && guard < 20000)
				{
					++guard;
					if (gSDK->GetObjectTypeN(it) == kDimAssocDimHeaderNode)
					{
						WorldPt s;
						if (DimAssocPointValue(it, ovDimStartPt, s) &&
							std::fabs(static_cast<double>(s.y) - static_cast<double>(y)) < 1500)
							madeDim = it;
					}
					it = gSDK->NextObject(it);
				}
			}
			if (madeDim == nil)
			{
				probe.log("    この帯には寸法が生えていない（この口は寸法を作らない）。");
			}
			else
			{
				probe.log("    寸法が生えた: " + DimAssocReadPoint(madeDim, ovDimStartPt) + " → " +
						  DimAssocReadPoint(madeDim, ovDimEndPt));
				probe.log("    寸法: " + DimAssocConstraintState(madeDim));
				gSDK->DeselectAll();
				gSDK->SelectObject(line, true);
				gSDK->CreateConstraintModel(nil, true);
				gSDK->MoveObject(line, 1200, 0);
				probe.log("    線分を +1200 動かして UpdateConstraintModel = " +
						  DimAssocYesNo(gSDK->UpdateConstraintModel() != 0));
				gSDK->DeselectAll();
				probe.log("    動かした後の測点 = " + DimAssocReadPoint(madeDim, ovDimStartPt) +
						  " → " + DimAssocReadPoint(madeDim, ovDimEndPt));
				probe.log("    ★ 始点の x が 1200 になっていれば追従した（0 のままなら追わない）");
			}
		}
	}

	// 3b. SetBinaryConstraint（type 1 = coincident）で寸法の測点を線分の端点へ縛る。
	{
		const WorldCoord y = static_cast<WorldCoord>(bandTop - 56000);
		probe.log("");
		probe.log("--- 3b. SetBinaryConstraint(1 = coincident, 寸法, 線分, …)");
		probe.log("    頂点の番号は GetClosestPt に測点の座標を渡して引く。");
		DimAssocRig rig;
		if (DimAssocBuildRig(probe, y, rig))
		{
			MCObjectHandle dimObj = rig.dim;
			short dimVertex = 0;
			Sint32 dimContained = 0;
			gSDK->GetClosestPt(dimObj, WorldPt(0, y), dimVertex, dimContained);
			MCObjectHandle lineObj = rig.lineA;
			short lineVertex = 0;
			Sint32 lineContained = 0;
			gSDK->GetClosestPt(lineObj, WorldPt(0, y), lineVertex, lineContained);
			probe.log("    GetClosestPt: 寸法の頂点 = " + DimAssocInt(dimVertex) +
					  "（handle が別物に差し替わったか: " + DimAssocYesNo(dimObj != rig.dim) +
					  "）/ 線分の頂点 = " + DimAssocInt(lineVertex) +
					  "（差し替わったか: " + DimAssocYesNo(lineObj != rig.lineA) + "）");
			const Boolean ok = gSDK->SetBinaryConstraint(
				kDimAssocBinaryCoincident, rig.dim, rig.lineA, dimVertex, -1, lineVertex, -1, 0, 0);
			probe.log("    SetBinaryConstraint = " + DimAssocYesNo(ok != 0));
			probe.log("    寸法: " + DimAssocConstraintState(rig.dim));
			probe.log("    線分A: " + DimAssocConstraintState(rig.lineA));

			gSDK->DeselectAll();
			gSDK->SelectObject(rig.lineA, true);
			gSDK->CreateConstraintModel(nil, true);
			gSDK->MoveObject(rig.lineA, 1200, 0);
			probe.log("    線分A を +1200 動かして UpdateConstraintModel = " +
					  DimAssocYesNo(gSDK->UpdateConstraintModel() != 0));
			gSDK->DeselectAll();
			DimAssocReportDim(probe, rig.dim, "動かした後");
			DimAssocVerdict(probe, rig, 1200.0);
		}
	}

	// =======================================================================
	// 4. 画面でドラッグしてもらう 1 組（固定座標・名前付き）。
	probe.log("");
	probe.log("== 4. 画面でドラッグしてもらう 1 組 ==");
	if (dragRigExists)
	{
		probe.log("  もうこの図面にあります（上の「== 1b ==」が判定しました）。作り直しません。");
	}
	else
	{
		MCObjectHandle lineA = gSDK->CreateLine(WorldPt(kDimAssocDragX1, kDimAssocDragY),
												WorldPt(kDimAssocDragX1, kDimAssocDragY - 500));
		MCObjectHandle lineB = gSDK->CreateLine(WorldPt(kDimAssocDragX2, kDimAssocDragY),
												WorldPt(kDimAssocDragX2, kDimAssocDragY - 500));
		MCObjectHandle dim = gSDK->CreateLinearDimension(WorldPt(kDimAssocDragX1, kDimAssocDragY),
														 WorldPt(kDimAssocDragX2, kDimAssocDragY),
														 600, 0, Vector2(0, 0), 0);
		if (lineA == nil || lineB == nil || dim == nil)
		{
			probe.fail("4: 1 組を作れなかった");
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
			probe.log("  作りました（環境設定を両方 on にして AssociateLinearDimension 済み）。");
			probe.log("  寸法: " + DimAssocConstraintState(dim));
			probe.log("  線A: " + DimAssocConstraintState(lineA));
		}
	}

	probe.log("");
	probe.log("== お願い ==");
	probe.log("**図面のいちばん下にある、文字が添えてある横向きの 3,000 の寸法**——その");
	probe.log("左端の縦棒（線A）を画面でつまんで右へ動かし、**このプローブをもう一度**");
	probe.log("走らせてください。上の「== 1b ==」が追従したかを自分で判定して出します");
	probe.log("（ログを貼る必要も、数字を読む必要もありません）。");
}
