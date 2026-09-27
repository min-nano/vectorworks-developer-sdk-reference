//
//	probes/runtime/dim-class-fixang-vs-sloped/probe.cpp
//
//	[issue #134] 直線寸法の dimType（= ovDimClass）の 0（fix_ang）と 1（sloped）の
//	違いが、実際にどこへ出るかを実測する。
//
//	分かっていること（#129 の実測。Findings「寸法」）:
//	  - dimType はそのまま ovDimClass(26) に入る（0〜5 を渡して読み戻し、全一致）。
//	  - **作った直後は 0 と 1 の区別が付かない。** 同じ 2 点に対して外接矩形も
//	    ovDimDirection(12) も完全に同一で、斜めの 2 点を渡せば 0 でも斜めになる。
//
//	ヘッダには 0 と 1 の違いが**書かれていない**。ObjectVariables.h:292 は
//	`(fix_ang) = 0, (sloped) = 1, …` と名前だけを並べ、APIBase.Legacy.Defs.h:2100 の
//	CreateLinearDimension の説明も「水平と垂直だけを許すか、p1→p2 の向きへ回すか、
//	ordinate を作るか」としか言わない（どの数値がどれかも書いていない）。SDK が同梱する
//	実装ソース（SDKLib/Source）にも寸法の実装は入っていないので、**実機でしか答えが出ない**。
//
//	そこで「作った後に測点が動いたらどうなるか」を見る。名前どおりなら
//	fix_ang は寸法線の角度を保ち（＝測点を結ぶ向きから外れる）、sloped は測点に追従する。
//
//	目視に頼らないための工夫:
//	  - 寸法線の向きは ovDimDirection(12) を読む。
//	  - **表示されている寸法値は、寸法（型 63 = dimHeaderNode）の中身を歩いて
//	    GetTextChars で文字列として読む。** これが取れれば「fix_ang は射影した長さを
//	    出すのか、測点間の実長を出すのか」まで、ログだけで決められる。
//	  - 外接矩形も併せて出す（寸法線が回ったかどうかの裏取り）。
//

#include "Probe.h"

#include <cmath>
#include <string>

namespace
{
	// 短い名前・ありふれた名前は SDK と OS のヘッダとぶつかるので接頭辞を付ける
	// （probes/runtime/README.md「短い名前・ありふれた名前を使わない」）。

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

	std::string DimClassReadBool(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return "(GetObjectVariable が false)";
		// GetBoolean は Boolean ではなく bool& を取る（ObjectVariables.h:108）。
		bool raw = false;
		if (!v.GetBoolean(raw))
			return "(Boolean として読めない。型=" +
				   DimClassInt(static_cast<long long>(v.GetType())) + ")";
		return raw ? "true" : "false";
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

	// ovDimStartPt / ovDimEndPt を数値で取り出す（測点間の実長を計算するため）。
	bool DimClassPointValue(MCObjectHandle h, short selector, WorldPt& outPt)
	{
		TVariableBlock v;
		if (!gSDK->GetObjectVariable(h, selector, v))
			return false;
		return v.GetWorldPt(outPt) != 0;
	}

	// 寸法の中身を歩いて、文字が入っているオブジェクトの文字列を拾う。
	// 表示されている寸法値（"1000" など）がここに出れば、目視に頼らずに読める。
	std::string DimClassTexts(MCObjectHandle dim)
	{
		if (dim == nil)
			return "(寸法が nil)";
		std::string result;
		int guard = 0;
		MCObjectHandle it = gSDK->FirstMemberObj(dim);
		while (it != nil && guard < 200)
		{
			++guard;
			const short type = gSDK->GetObjectTypeN(it);
			// 型 0 = kTermNode（末尾の番兵）。歩きはこれも数える（Findings「寸法」）。
			if (type != 0)
			{
				TXString chars = gSDK->GetTextChars(it);
				const std::string text(static_cast<const char*>(chars));
				if (!text.empty())
				{
					if (!result.empty())
						result += " / ";
					result += "型" + DimClassInt(type) + ":\"" + text + "\"";
				}
			}
			it = gSDK->NextObject(it);
		}
		if (result.empty())
			return "(文字を持つ中身は見つからなかった。歩いた数=" + DimClassInt(guard) + ")";
		return result;
	}

	// 寸法 1 本の素性を 1 まとめでログへ出す。
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
		probe.log("    ovDimReferenceAngle(31) = " + DimClassReadBool(dim, ovDimReferenceAngle) +
				  " / ovDim2ReferenceLinesAngle(42) = " +
				  DimClassReadBool(dim, ovDim2ReferenceLinesAngle));

		// 測点間の実長。表示値と突き合わせて「射影か実長か」を決める材料にする。
		WorldPt a, b;
		if (DimClassPointValue(dim, ovDimStartPt, a) && DimClassPointValue(dim, ovDimEndPt, b))
		{
			const double dx = static_cast<double>(b.x) - static_cast<double>(a.x);
			const double dy = static_cast<double>(b.y) - static_cast<double>(a.y);
			probe.log("    測点間: dx=" + DimClassNum(dx) + " dy=" + DimClassNum(dy) +
					  " 実長=" + DimClassNum(std::sqrt(dx * dx + dy * dy)) +
					  " 水平射影=" + DimClassNum(dx < 0 ? -dx : dx) +
					  " 垂直射影=" + DimClassNum(dy < 0 ? -dy : dy));
		}

		WorldRect bounds;
		if (gSDK->GetObjectBounds(dim, bounds))
			probe.log(
				"    外接矩形(mm) = 左" + DimClassNum(bounds.left) + " 上" +
				DimClassNum(bounds.top) + " 右" + DimClassNum(bounds.right) + " 下" +
				DimClassNum(bounds.bottom) + " 幅" +
				DimClassNum(static_cast<double>(bounds.right) - static_cast<double>(bounds.left)) +
				" 高" +
				DimClassNum(static_cast<double>(bounds.top) - static_cast<double>(bounds.bottom)));
		else
			probe.log("    GetObjectBounds が false");

		probe.log("    表示文字 = " + DimClassTexts(dim));
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

	// ovDimEndPt を書き換える。書けたかどうか（SetObjectVariable の戻り値）も返す。
	bool DimClassSetEndPt(vwprobe::Report& probe, MCObjectHandle dim, const WorldPt& pt)
	{
		TVariableBlock v;
		// TVariableBlock の setter は operator= だけ（Findings「寸法」）。
		v = pt;
		const bool ok = gSDK->SetObjectVariable(dim, ovDimEndPt, v) != 0;
		probe.log("    SetObjectVariable(ovDimEndPt=(" + DimClassNum(pt.x) + "," +
				  DimClassNum(pt.y) + ")) = " + (ok ? "true" : "false"));
		return ok;
	}
} // namespace

VW_PROBE("dim-class-fixang-vs-sloped", "寸法の dimType 0（fix_ang）と 1（sloped）の違い",
		 "同じ 2 点で 0 と 1 を作り、測点を動かした後・関連付けた図形を動かした後の "
		 "ovDimDirection・測点・外接矩形・表示文字を見比べる")
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
	probe.log("ObjectVariables.h:292 が名前だけを並べている 0=fix_ang / 1=sloped の差を探す。");

	// -----------------------------------------------------------------------
	probe.log("");
	probe.log("== 1. 水平な 2 点で作り、終点だけを動かして角度を変える ==");
	probe.log("(0,0)-(1000,0) で作り、ovDimEndPt を (1000,600) にする。");
	probe.log("fix_ang が名前どおりなら ovDimDirection は (1,0) のまま残り、");
	probe.log("sloped なら (1000,600) の正規化 (0.857,0.514) へ動く。");

	for (short dimType = 0; dimType <= 1; ++dimType)
	{
		const char* const label = (dimType == 0) ? "dimType=0 (fix_ang)" : "dimType=1 (sloped)";
		probe.log("");
		probe.log(std::string("--- ") + label);
		// y をずらして重ならないように置く（図面で見比べたいとき用。判定はログで行う）。
		const WorldCoord baseY = static_cast<WorldCoord>(dimType * 2000);
		MCObjectHandle dim =
			DimClassCreate(probe, WorldPt(0, baseY), WorldPt(1000, baseY), 300, dimType, label);
		if (dim == nil)
			continue;
		DimClassReport(probe, dim, "作った直後");

		DimClassSetEndPt(probe, dim, WorldPt(1000, baseY + 600));
		DimClassReport(probe, dim, "ovDimEndPt を書いた直後（ResetObject 前）");

		const bool reset = gSDK->ResetObject(dim) != 0;
		probe.log("    ResetObject = " + std::string(reset ? "true" : "false"));
		DimClassReport(probe, dim, "ResetObject の後");
	}

	// -----------------------------------------------------------------------
	probe.log("");
	probe.log("== 2. 斜めの 2 点で作り、終点を動かして水平に戻す ==");
	probe.log("(0,y)-(1000,y+600) で作り、ovDimEndPt を (1000,y) にする（1 の逆向き）。");

	for (short dimType = 0; dimType <= 1; ++dimType)
	{
		const char* const label = (dimType == 0) ? "dimType=0 (fix_ang)" : "dimType=1 (sloped)";
		probe.log("");
		probe.log(std::string("--- 斜め始まり ") + label);
		const WorldCoord baseY = static_cast<WorldCoord>(5000 + dimType * 2000);
		MCObjectHandle dim = DimClassCreate(probe, WorldPt(0, baseY), WorldPt(1000, baseY + 600),
											300, dimType, label);
		if (dim == nil)
			continue;
		DimClassReport(probe, dim, "作った直後");

		DimClassSetEndPt(probe, dim, WorldPt(1000, baseY));
		gSDK->ResetObject(dim);
		DimClassReport(probe, dim, "ovDimEndPt を水平に戻して ResetObject した後");
	}

	// -----------------------------------------------------------------------
	probe.log("");
	probe.log("== 3. 関連付け（AssociateLinearDimension）した図形を動かす ==");
	probe.log("測点の位置に軌跡点（CreateLocus）を 2 つ置いて寸法を作り、終点側の軌跡点だけを");
	probe.log("上へ 600mm 動かす。関連付いていれば測点が追い、そこで 0 と 1 の差が出るはず。");

	for (short dimType = 0; dimType <= 1; ++dimType)
	{
		const char* const label = (dimType == 0) ? "dimType=0 (fix_ang)" : "dimType=1 (sloped)";
		probe.log("");
		probe.log(std::string("--- 関連付け ") + label);
		const WorldCoord baseY = static_cast<WorldCoord>(10000 + dimType * 2000);
		const WorldPt a(0, baseY);
		const WorldPt b(1000, baseY);

		MCObjectHandle locusA = gSDK->CreateLocus(a);
		MCObjectHandle locusB = gSDK->CreateLocus(b);
		if (locusA == nil || locusB == nil)
		{
			probe.fail(std::string(label) + ": CreateLocus が nil を返した");
			continue;
		}
		MCObjectHandle dim = DimClassCreate(probe, a, b, 300, dimType, label);
		if (dim == nil)
			continue;
		DimClassReport(probe, dim, "作った直後");

		gSDK->DeselectAll();
		gSDK->SelectObject(locusA, true);
		gSDK->SelectObject(locusB, true);
		gSDK->AssociateLinearDimension(dim, true); // 戻り値は void
		gSDK->DeselectAll();
		probe.log(
			"    AssociateLinearDimension(dim, selectedObjectsMode=true) を呼んだ（戻り値なし）");
		DimClassReport(probe, dim, "関連付けた直後");

		gSDK->MoveObject(locusB, 0, 600);
		probe.log("    MoveObject(終点側の軌跡点, dx=0, dy=+600)");
		gSDK->ResetObject(dim);
		DimClassReport(probe, dim, "終点側の軌跡点を動かして ResetObject した後");
	}

	// -----------------------------------------------------------------------
	probe.log("");
	probe.log("== 4. ovDimDirection を直に書くと寸法線の角度は動くか ==");
	probe.log("fix_ang の「固定された角度」がここに入っているなら、これを書き換えれば");
	probe.log("測点を動かさずに寸法線だけが回る（ovDimDirection はヘッダ上 FracPt / Not for public "
			  "use）。");

	for (short dimType = 0; dimType <= 1; ++dimType)
	{
		const char* const label = (dimType == 0) ? "dimType=0 (fix_ang)" : "dimType=1 (sloped)";
		probe.log("");
		probe.log(std::string("--- 向きを直書き ") + label);
		const WorldCoord baseY = static_cast<WorldCoord>(15000 + dimType * 2000);
		MCObjectHandle dim =
			DimClassCreate(probe, WorldPt(0, baseY), WorldPt(1000, baseY), 300, dimType, label);
		if (dim == nil)
			continue;
		DimClassReport(probe, dim, "作った直後");

		TVariableBlock v;
		// 45 度の単位ベクトル。読み戻せた型（WorldPt）でそのまま書いてみる。
		v = WorldPt(0.7071, 0.7071);
		const bool ok = gSDK->SetObjectVariable(dim, ovDimDirection, v) != 0;
		probe.log("    SetObjectVariable(ovDimDirection=(0.7071,0.7071)) = " +
				  std::string(ok ? "true" : "false"));
		gSDK->ResetObject(dim);
		DimClassReport(probe, dim, "ovDimDirection を書いて ResetObject した後");
	}

	probe.log("");
	probe.log("== 読み方 ==");
	probe.log("1〜3 で ovDimDirection / 外接矩形 / 表示文字が dimType 0 と 1 で違えば、そこが差。");
	probe.log("どの節でも 0 と 1 が最後まで一致していたら、SDK から作る分には差が無いということ");
	probe.log("——そのときは「OIP で触ったときだけ違う」か「意味を失っている」のどちらかで、");
	probe.log("前者なら利用者の操作が要る（結論はそこで分かれる）。");
}
