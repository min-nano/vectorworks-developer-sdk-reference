//
//	probes/runtime/dim-class-fixang-vs-sloped/probe.cpp
//
//	[issue #134] 直線寸法の dimType（= ovDimClass）の 0（fix_ang）と 1（sloped）の
//	違いが、実際にどこへ出るかを実測する。
//
//	【1 巡目で分かったこと】（PR #136 の 1 本目のログ＋利用者が送ってくれた図面の絵）
//
//	  - **寸法線の角度は ovDimDirection(12) が単独で決めている。** SetObjectVariable で
//	    書けて（true が返り、読み戻しも一致し、絵でも寸法線が回った）、**測点
//	    （ovDimStartPt/ovDimEndPt）を動かしても再計算されない**。
//	  - **表示される寸法値は「測点間ベクトルの ovDimDirection への射影」。** 実長ではない。
//	    絵で確かめた 3 例:
//	      dir=(1,0)     測点差 (1000,600) → 「1,000」   （実長 1166.19 ではない）
//	      dir=(0.857,0.514) 測点差 (1000,0) → 「857 1/2」（実長 1000 ではない）
//	      dir=(0.707,0.707) 測点差 (1000,0) → 「707 1/20」
//	  - **ここまで dimType 0 と 1 はすべて同一**（値・向き・外接矩形・絵）。
//	  - AssociateLinearDimension は**関連付かなかった**——軌跡点を 2 つ選んで寸法を
//	    作り、片方を MoveObject しても測点は動かず、絵でも追従していない。
//	    **これは「0 と 1 に差が無い」ではなく「試験が成立しなかった」**なので取り直す。
//
//	【この 2 巡目が確かめること】
//
//	  0. **いま図面にある寸法を先に全部読む。** 1 巡目で作った寸法を利用者が OIP や
//	     ドラッグで触った後にもう一度走らせれば、**触った結果がそのままログに出る**
//	     ——利用者はログを貼らなくてよいし、番号も打たなくてよい。
//	  1. **関連付けを取り直す。** 軌跡点ではなく線分で、h に寸法／図形のどちらを渡すか、
//	     selectedObjectsMode の true / false——組み合わせを総当たりする。
//	     「平行移動でも追わない」を先に見るので、関連付いたか否かが切り分けられる。
//	  2. **手で触ってもらう用の 1 組**を、どちらがどちらか分かるよう文字を添えて作る。
//

#include "Probe.h"

#include <cmath>
#include <string>

namespace
{
	// 短い名前・ありふれた名前は SDK と OS のヘッダとぶつかるので接頭辞を付ける
	// （probes/runtime/README.md「短い名前・ありふれた名前を使わない」）。

	// 寸法のオブジェクト型。Objs.TDType.h:129 の dimHeaderNode（Findings「寸法」）。
	const short kDimClassDimHeaderNode = 63;

	std::string DimClassNum(double value)
	{
		std::string s = std::to_string(value);
		const std::string::size_type dot = s.find('.');
		if (dot != std::string::npos && s.size() > dot + 4)
			s.erase(dot + 4);
		return s;
	}

	std::string DimClassInt(long long value)
	{
		return std::to_string(value);
	}

	std::string DimClassReadSint8(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		Sint8 raw = 0;
		if (!v.GetSint8(raw))
			return "(Sint8 として読めない。型=" + DimClassInt(static_cast<long long>(v.GetType())) +
				   ")";
		return DimClassInt(raw);
	}

	std::string DimClassReadPoint(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		WorldPt raw;
		if (!v.GetWorldPt(raw))
			return "(WorldPt として読めない。型=" +
				   DimClassInt(static_cast<long long>(v.GetType())) + ")";
		return "(" + DimClassNum(raw.x) + ", " + DimClassNum(raw.y) + ")";
	}

	std::string DimClassReadReal(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		Real64 raw = 0.0;
		if (!v.GetReal64(raw))
			return "(Real64 として読めない。型=" +
				   DimClassInt(static_cast<long long>(v.GetType())) + ")";
		return DimClassNum(raw);
	}

	bool DimClassPointValue(MCObjectHandle h, short selector, WorldPt& outPt)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return false;
		return v.GetWorldPt(outPt) != 0;
	}

	// 寸法 1 本の素性をログへ出す。**射影も一緒に出す**——1 巡目で「表示値＝測点間
	// ベクトルの ovDimDirection への射影」と分かったので、絵を見なくても表示値が
	// 予想できる（予想と絵が食い違ったらそこが新しい知見）。
	void DimClassReport(vwprobe::Report& probe, MCObjectHandle dim, const char* when)
	{
		if (dim == nil)
		{
			probe.log(std::string("  ") + when + ": 寸法が nil");
			return;
		}
		probe.log(std::string("  [") + when + "]");
		probe.log("    ovDimClass(26) = " + DimClassReadSint8(dim, ovDimClass) +
				  " / ovDimDirection(12) = " + DimClassReadPoint(dim, ovDimDirection));
		probe.log("    ovDimStartPt(13) = " + DimClassReadPoint(dim, ovDimStartPt) +
				  " / ovDimEndPt(14) = " + DimClassReadPoint(dim, ovDimEndPt) +
				  " / ovDimStartOffset(15) = " + DimClassReadReal(dim, ovDimStartOffset));

		WorldPt a, b, dir;
		if (DimClassPointValue(dim, ovDimStartPt, a) && DimClassPointValue(dim, ovDimEndPt, b))
		{
			const double dx = static_cast<double>(b.x) - static_cast<double>(a.x);
			const double dy = static_cast<double>(b.y) - static_cast<double>(a.y);
			std::string line = "    測点間: dx=" + DimClassNum(dx) + " dy=" + DimClassNum(dy) +
							   " 実長=" + DimClassNum(std::sqrt(dx * dx + dy * dy));
			if (DimClassPointValue(dim, ovDimDirection, dir))
			{
				const double proj =
					dx * static_cast<double>(dir.x) + dy * static_cast<double>(dir.y);
				line += " / ovDimDirection への射影=" + DimClassNum(proj < 0 ? -proj : proj) +
						" ← 表示値はこれになるはず";
			}
			probe.log(line);
		}

		WorldRect bounds;
		if (gSDK->GetObjectBounds(dim, bounds))
			probe.log("    外接矩形(mm) = 左" + DimClassNum(bounds.left) + " 上" +
					  DimClassNum(bounds.top) + " 右" + DimClassNum(bounds.right) + " 下" +
					  DimClassNum(bounds.bottom));
		else
			probe.log("    GetObjectBounds が false");
	}

	MCObjectHandle DimClassCreate(vwprobe::Report& probe, const WorldPt& p1, const WorldPt& p2,
								  WorldCoord startOffset, short dimType, const char* label)
	{
		MCObjectHandle dim =
			gSDK->CreateLinearDimension(p1, p2, startOffset, 0, Vector2(0, 0), dimType);
		if (dim == nil)
			probe.fail(std::string(label) + ": CreateLinearDimension が nil を返した");
		return dim;
	}

	// 測点の位置に「端点を持つ線分」を 2 本立てる（関連付けの相手）。
	// 寸法の測点とちょうど重なる端点を持たせるのが狙い。
	void DimClassMakeStubs(WorldCoord x1, WorldCoord x2, WorldCoord y, MCObjectHandle& outLeft,
						   MCObjectHandle& outRight)
	{
		outLeft = gSDK->CreateLine(WorldPt(x1, y), WorldPt(x1, y - 500));
		outRight = gSDK->CreateLine(WorldPt(x2, y), WorldPt(x2, y - 500));
	}

	// 関連付けの 1 通りを試す。passDimToAssociate が false なら、寸法ではなく
	// 図形のほうを AssociateLinearDimension へ渡す（ヘッダの h がどちらを指すのか
	// 書かれていないため、両方試す）。
	void DimClassTryAssociation(vwprobe::Report& probe, short dimType, WorldCoord baseY,
								bool selectedObjectsMode, bool passDimToAssociate)
	{
		const std::string label =
			std::string("dimType=") + DimClassInt(dimType) + " / " +
			"selectedObjectsMode=" + (selectedObjectsMode ? "true" : "false") +
			" / h に渡すもの=" + (passDimToAssociate ? "寸法" : "図形");
		probe.log("");
		probe.log("--- " + label);

		MCObjectHandle stubLeft = nil;
		MCObjectHandle stubRight = nil;
		DimClassMakeStubs(0, 1000, baseY, stubLeft, stubRight);
		if (stubLeft == nil || stubRight == nil)
		{
			probe.fail(label + ": CreateLine が nil を返した");
			return;
		}
		MCObjectHandle dim = DimClassCreate(probe, WorldPt(0, baseY), WorldPt(1000, baseY), 300,
											dimType, label.c_str());
		if (dim == nil)
			return;
		DimClassReport(probe, dim, "作った直後");

		gSDK->DeselectAll();
		gSDK->SelectObject(stubLeft, true);
		gSDK->SelectObject(stubRight, true);
		if (passDimToAssociate)
		{
			gSDK->AssociateLinearDimension(dim, selectedObjectsMode);
		}
		else
		{
			gSDK->AssociateLinearDimension(stubLeft, selectedObjectsMode);
			gSDK->AssociateLinearDimension(stubRight, selectedObjectsMode);
		}
		gSDK->DeselectAll();
		probe.log("    AssociateLinearDimension を呼んだ（戻り値なし）");

		// まず**平行移動**。関連付いていれば測点が両方とも追う。ここが動かないなら
		// 「関連付いていない」——角度が変わる試験の結果を読んではいけない。
		gSDK->MoveObject(stubLeft, 0, 400);
		gSDK->MoveObject(stubRight, 0, 400);
		gSDK->ResetObject(dim);
		probe.log("    【対照】両方の線分を +400 平行移動して ResetObject");
		DimClassReport(probe, dim, "平行移動の後（測点が追えば関連付いている）");

		// 次に**片端だけ**。ここで角度が変わり、0 と 1 の差が出るなら出る。
		gSDK->MoveObject(stubRight, 0, 600);
		gSDK->ResetObject(dim);
		probe.log("    【本番】終点側の線分だけを更に +600 動かして ResetObject");
		DimClassReport(probe, dim, "片端だけ動かした後");
	}
} // namespace

VW_PROBE("dim-class-fixang-vs-sloped", "寸法の dimType 0（fix_ang）と 1（sloped）の違い",
		 "図面にある寸法を先に読み（触った後の再実行用）、関連付けを総当たりで取り直し、"
		 "手で触ってもらう 1 組を文字つきで作る")
{
	MCObjectHandle layer = gSDK->GetActiveLayer();
	if (layer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した（アクティブレイヤが無い）");
		return;
	}
	{
		TXString layerName;
		gSDK->GetObjectName(layer, layerName);
		probe.log("アクティブレイヤ: \"" + std::string(static_cast<const char*>(layerName)) + "\"");
	}

	// -----------------------------------------------------------------------
	// 0. いま図面にある寸法を全部読む。**2 回目以降の実行では、ここが「利用者が
	//    手で触った後の状態」になる**——これが目視の代わりになる。
	probe.log("");
	probe.log("== 0. いま図面にある寸法（型 63 = dimHeaderNode）を全部読む ==");
	probe.log("1 巡目に作った寸法を OIP やドラッグで触った後にもう一度走らせると、");
	probe.log("触った結果がここに出る（ログを貼る必要はありません）。");
	long long found = 0;
	{
		int guard = 0;
		MCObjectHandle it = gSDK->FirstMemberObj(layer);
		while (it != nil && guard < 10000)
		{
			++guard;
			if (gSDK->GetObjectTypeN(it) == kDimClassDimHeaderNode)
			{
				++found;
				DimClassReport(probe, it, ("図面にあった寸法 " + DimClassInt(found)).c_str());
			}
			it = gSDK->NextObject(it);
		}
	}
	if (found == 0)
		probe.log("  （寸法は 1 本も見つかりませんでした＝まっさらな図面です）");
	else
		probe.log("  合計 " + DimClassInt(found) + " 本。");

	// 作るものが前回のぶんと重ならないように、見つかった本数ぶん下へずらす。
	const WorldCoord shift = static_cast<WorldCoord>(-6000 * (found + 1));

	// -----------------------------------------------------------------------
	// 1. 関連付けを取り直す。1 巡目は軌跡点で試して関連付かなかったので、線分で、
	//    引数の意味の解釈（h＝寸法／図形）と selectedObjectsMode を総当たりする。
	probe.log("");
	probe.log("== 1. AssociateLinearDimension を取り直す ==");
	probe.log("ヘッダは h が寸法と図形のどちらを指すのかを書いていないので両方試す。");
	probe.log("**平行移動の対照を先に置く**——そこで測点が追わなければ関連付いていない");
	probe.log("ということなので、その組の「片端だけ」の結果は読んではいけない。");

	{
		WorldCoord y = shift;
		for (short dimType = 0; dimType <= 1; ++dimType)
		{
			for (int passDim = 1; passDim >= 0; --passDim)
			{
				for (int selMode = 1; selMode >= 0; --selMode)
				{
					DimClassTryAssociation(probe, dimType, y, selMode != 0, passDim != 0);
					y -= 3000;
				}
			}
		}
	}

	// -----------------------------------------------------------------------
	// 2. 手で触ってもらう 1 組。どちらがどちらか分かるよう文字を添える。
	const WorldCoord dragBaseY = shift - 30000;
	probe.log("");
	probe.log("== 2. 手で触ってもらう 1 組を作りました ==");
	probe.log("同じ 2 点・同じ startOffset で、dimType だけが違う寸法を 2 本、");
	probe.log(
		"それぞれの右に「dimType=0 (fix_ang)」「dimType=1 (sloped)」と文字を添えて置きます。");

	for (short dimType = 0; dimType <= 1; ++dimType)
	{
		const WorldCoord y = static_cast<WorldCoord>(dragBaseY - dimType * 3000);
		const char* const name = (dimType == 0) ? "dimType=0 (fix_ang)" : "dimType=1 (sloped)";
		MCObjectHandle dim =
			DimClassCreate(probe, WorldPt(0, y), WorldPt(1000, y), 300, dimType, name);
		gSDK->CreateTextBlock(TXString(name), WorldPt(1300, y), false, 0);
		probe.log("");
		probe.log(std::string("--- ") + name + "（文字は右側 x=1300 に置きました）");
		DimClassReport(probe, dim, "作った直後");
	}

	probe.log("");
	probe.log("== お願い ==");
	probe.log("上の 2 本（文字が添えてある横向きの 1,000 の寸法）を、**同じように**手で");
	probe.log("触ってください。知りたいのは「0 と 1 で違う動きをするか」だけです:");
	probe.log("  (a) 寸法の端点（測点側の制御点）をつまんで、斜めになるように動かす");
	probe.log("  (b) OIP に角度や向きの欄があれば、そこを触ってみる");
	probe.log("そのうえで**このプローブをもう一度走らせてください**。上の「== 0 ==」に");
	probe.log("触った後の ovDimDirection と測点が出るので、ログを貼る必要はありません。");
	probe.log("（2 本が同じ動きなら、SDK から見ても手で触っても 0 と 1 は同じ、で確定します）");
}
