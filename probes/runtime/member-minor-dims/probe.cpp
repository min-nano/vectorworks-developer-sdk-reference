//
//	probes/runtime/member-minor-dims/probe.cpp
//
//	[issue #173] **構造材の `MinorBreadth` / `MinorDepth` は何の寸法か。第 2 版。**
//
//	第 1 版で確定したこと（PR #174 の 1 本目。VW 2026 / mac・新規の空図面・26 個体）
//	--------------------------------------------------------------------------
//	**`MemberType`=`1`（コンクリート）/ `2`（木）の断面は矩形ではなく H 形である**
//	（群の子は常に 12 頂点の `Polygon`）。4 欄はその H 形の寸法で、頂点は必ずこの形:
//
//	  (-B/2,0) (-B/2,d) (-b/2,d) (-b/2,D-d) (-B/2,D-d) (-B/2,D)
//	  ( B/2,D) ( B/2,D-d) ( b/2,D-d) ( b/2,d) ( B/2,d) ( B/2,0)
//
//	    B = `MajorBreadth`（主幅＝フランジ幅）   D = `MajorDepth`（主高さ＝全せい）
//	    b = `MinorBreadth`（副幅＝**ウェブ厚**） d = `MinorDepth`（副高さ＝**フランジ厚**）
//
//	  ・**`d` は上下 2 枚のフランジに 1 回ずつ効く**（下 `0`〜`d`・上 `D-d`〜`D`）。
//	    だから `2d > D` になると上下が交差して輪郭が自己交差し、**3D 実体（型 84
//	    `kCSGTree`）が作られない**——`d`=300（＝`D`/2 ちょうど）は生きて、301 で壊れた。
//	  ・**`b` に壊れる閾値は無い。** 149 / 150 / 151 / 299 / 300 / 400 すべて実体あり。
//	    `b`>`B` ではウェブがフランジから食み出し、**外接がウェブ幅まで広がった**
//	    （`b`=400 で 幅Y=400）。
//	  ・木（`2`）はコンクリート（`1`）と**同一**。`ProfileShape`（`角形鋼管` / `溝形鋼`）は
//	    **完全に無視される**（頂点列が 1 つも動かない）。
//	  ・**壊しても直る。** `d` を戻して `ResetObject` すると実体 84 が戻った。
//	    壊れている間も `ResetObject`=`true`・読み戻し一致・`B1`/`D1` は書いた値を写す。
//
//	この版で閉じること——**残っている 3 つ。どれも issue の問 2 の範囲内**
//	----------------------------------------------------------------
//	  G **`0` と負で壊れるのはどちらの欄か。** 第 1 版は副幅・副高さを**同時に** `0` に
//	    した回しか無く、切り分けが付いていない。1 欄ずつ `0` と負を与える。
//	  H **境界は「主高さの半分」か、それとも 300 という絶対値か。** 第 1 版は主高さ 600
//	    しか振っていない。主高さ 400 / 1000 でも `D`/2 が境かを確かめる。
//	  I **副を 1 文字も書かなくても壊れるのではないか。** 副高さの既定は **100** なので、
//	    `2*100 > D` すなわち**主高さが 200 未満の部材を作っただけで壊れる**見込み。
//	    そうなら「Minor を触らない」という当面の指針は**安全ではない**ことになる。
//	    ここは取り込み側の実装に直結するので、必ず取りに行く。
//
//	判定は第 1 版と同じ **(1) 実体84 の有無 (2) 高さZ (3) 群の子の頂点列**。
//	走らせるのは新規の空図面。図面は壊れる前提（undo イベントは開かない）。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kMnrPioName = "StructuralMember";

	std::string MnrText(const TXString& src)
	{
		const char* utf8 = static_cast<const char*>(src);
		return utf8 ? std::string(utf8) : std::string();
	}

	std::string MnrWhole(long long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%lld", value);
		return std::string(buffer);
	}

	std::string MnrCoord(double value)
	{
		char buffer[48];
		std::snprintf(buffer, sizeof(buffer), "%.1f", value);
		return std::string(buffer);
	}

	const char* MnrTypeName(short type)
	{
		switch (type)
		{
		case 0:
			return "Term";
		case 2:
			return "Line";
		case 3:
			return "Rect";
		case 5:
			return "Polygon";
		case 11:
			return "Group";
		case 15:
			return "Symbol";
		case 21:
			return "Polyline";
		case 24:
			return "Extrude";
		case 84:
			return "CSGTree";
		case 86:
			return "Parametric";
		case 90:
			return "UndoPlaceholder";
		default:
			return "?";
		}
	}

	std::string MnrRectText(const WorldRect& rect)
	{
		return "(" + MnrCoord(double(rect.left)) + "," + MnrCoord(double(rect.bottom)) + "→" +
			   MnrCoord(double(rect.right)) + "," + MnrCoord(double(rect.top)) + ")";
	}

	// 断面の輪郭を頂点で出す（種別 1 / 2 では必ず 12 頂点の H 形になる）。
	void MnrDumpVertices(vwprobe::Report& probe, MCObjectHandle kid, const std::string& indent)
	{
		const short type = gSDK->GetObjectTypeN(kid);
		if (type != 5 && type != 21)
			return;
		try
		{
			VWPolygon2DObj poly(kid);
			const size_t count = poly.GetVertexCount();
			std::string line = indent + "頂点" + MnrWhole(static_cast<long long>(count)) + ":";
			for (size_t at = 0; at < count && at < 40; ++at)
			{
				const VWPoint2D pt = poly.GetVertexPoint(at);
				line += " (" + MnrCoord(pt.x) + "," + MnrCoord(pt.y) + ")";
			}
			if (count > 40)
				line += " …（41 件目以降は略）";
			probe.log(line);
		}
		catch (...)
		{
			probe.log(indent + "頂点=（VWPolygon2DObj が例外を投げた）");
		}
	}

	// 1 個体を測る。**判定に使うのはこの 1 行 ＋ 続く群の子の行だけ。**
	void MnrSnap(vwprobe::Report& probe, const std::string& tag, MCObjectHandle pio)
	{
		size_t geometry = 0;
		size_t solids = 0;
		std::vector<short> types;
		std::vector<size_t> counts;
		for (MCObjectHandle child = gSDK->FirstMemberObj(pio); child != nil;
			 child = gSDK->NextObject(child))
		{
			const short type = gSDK->GetObjectTypeN(child);
			if (type == 90 || type == 0)
				continue;
			++geometry;
			if (type == 84)
				++solids;
			bool found = false;
			for (size_t at = 0; at < types.size(); ++at)
			{
				if (types[at] == type)
				{
					++counts[at];
					found = true;
					break;
				}
			}
			if (!found)
			{
				types.push_back(type);
				counts.push_back(1);
			}
		}

		std::string line = tag + " 幾何" + MnrWhole(static_cast<long long>(geometry)) + " 型[";
		for (size_t at = 0; at < types.size(); ++at)
		{
			if (at != 0)
				line += ",";
			line += MnrWhole(types[at]);
			line += std::string("(") + MnrTypeName(types[at]) + ")x";
			line += MnrWhole(static_cast<long long>(counts[at]));
		}
		line += "]";
		line += solids != 0 ? " 実体84=有" : " **実体84=無**";

		WorldRect bounds;
		if (gSDK->GetObjectBounds(pio, bounds))
			line += " **幅Y=" + MnrCoord(double(bounds.top) - double(bounds.bottom)) + "**";
		else
			line += " 幅Y=（取れず）";

		WorldCube cube;
		gSDK->GetObjectCube(pio, cube);
		line += " **高さZ=" + MnrCoord(double(cube.MaxZ()) - double(cube.MinZ())) + "**";
		probe.log(line);

		MCObjectHandle group = gSDK->GetCustomObjectProfileGroup(pio);
		if (group == nil)
		{
			probe.log("  群=nil");
			return;
		}
		WorldRect groupBounds;
		std::string head = "  群";
		if (gSDK->GetObjectBounds(group, groupBounds))
			head += MnrRectText(groupBounds);
		probe.log(head);
		for (MCObjectHandle kid = gSDK->FirstMemberObj(group); kid != nil;
			 kid = gSDK->NextObject(kid))
		{
			const short type = gSDK->GetObjectTypeN(kid);
			if (type == 0)
				continue;
			std::string kidLine = "    群の子 型" + MnrWhole(type) + "(" + MnrTypeName(type) + ")";
			WorldRect kidBounds;
			if (gSDK->GetObjectBounds(kid, kidBounds))
				kidLine += " 外接" + MnrRectText(kidBounds);
			probe.log(kidLine);
			MnrDumpVertices(probe, kid, "      ");
		}
	}

	std::string MnrReset(MCObjectHandle member)
	{
		return gSDK->ResetObject(member) ? "true" : "**false**";
	}

	void MnrSetReal(vwprobe::Report& probe, MCObjectHandle member, const char* name, double value)
	{
		VWParametricObj pio(member);
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.fail(std::string("欄 ") + name + " を名前で引けなかった");
			return;
		}
		pio.SetParamReal(index, value);
	}

	void MnrSetValue(vwprobe::Report& probe, MCObjectHandle member, const char* name,
					 const std::string& value)
	{
		VWParametricObj pio(member);
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.fail(std::string("欄 ") + name + " を名前で引けなかった");
			return;
		}
		pio.SetParamValue(index, TXString(value.c_str()));
	}

	// 4 欄を読み戻す。**[I] では「副を書いていない」ことの証拠になる**（既定 100 / 100）。
	void MnrDumpFields(vwprobe::Report& probe, MCObjectHandle member)
	{
		VWParametricObj pio(member);
		probe.log("  読み戻し 主幅=" + MnrCoord(pio.GetParamReal(TXString("MajorBreadth"))) +
				  " 副幅=" + MnrCoord(pio.GetParamReal(TXString("MinorBreadth"))) +
				  " 主高さ=" + MnrCoord(pio.GetParamReal(TXString("MajorDepth"))) +
				  " 副高さ=" + MnrCoord(pio.GetParamReal(TXString("MinorDepth"))) +
				  " ／ 表示欄 B=[" + MnrText(pio.GetParamValue(TXString("B"))) + "] B1=[" +
				  MnrText(pio.GetParamValue(TXString("B1"))) + "] D=[" +
				  MnrText(pio.GetParamValue(TXString("D"))) + "] D1=[" +
				  MnrText(pio.GetParamValue(TXString("D1"))) + "]");
	}

	MCObjectHandle MnrMake(vwprobe::Report& probe, const std::string& tag, double originY)
	{
		VWPolygon2DObj path({VWPoint2D(0.0, originY), VWPoint2D(3000.0, originY)});
		MCObjectHandle pathHandle = path.GetThisObject();
		if (pathHandle == nil)
		{
			probe.fail(tag + " パス（VWPolygon2DObj）を作れなかった");
			return nil;
		}
		MCObjectHandle member = gSDK->CreateCustomObjectPath(kMnrPioName, pathHandle, nil, false);
		if (member == nil)
			probe.fail(tag + " CreateCustomObjectPath(StructuralMember) が nil を返した");
		return member;
	}

	// 1 個体。**`writeMinor` が false のときは副 2 欄へ 1 文字も書かない**（[I] 用）。
	void MnrCase(vwprobe::Report& probe, const std::string& tag, double& originY, int memberType,
				 double majorBreadth, double majorDepth, bool writeMinor, double minorBreadth,
				 double minorDepth)
	{
		MCObjectHandle member = MnrMake(probe, tag, originY);
		originY += 2000.0;
		if (member == nil)
			return;
		gSDK->ResetObject(member);
		MnrSetValue(probe, member, "MemberType", MnrWhole(memberType));
		MnrSetReal(probe, member, "MajorBreadth", majorBreadth);
		MnrSetReal(probe, member, "MajorDepth", majorDepth);
		if (writeMinor)
		{
			MnrSetReal(probe, member, "MinorBreadth", minorBreadth);
			MnrSetReal(probe, member, "MinorDepth", minorDepth);
		}
		const std::string reset = MnrReset(member);
		std::string head = tag + " 種別" + MnrWhole(memberType) + " 主" + MnrCoord(majorBreadth) +
						   "x" + MnrCoord(majorDepth);
		if (writeMinor)
			head += " 副幅" + MnrCoord(minorBreadth) + " 副高さ" + MnrCoord(minorDepth);
		else
			head += " **副は書かない**";
		MnrSnap(probe, head + " reset=" + reset, member);
		MnrDumpFields(probe, member);
	}
} // namespace

VW_PROBE("member-minor-dims", "構造材の副幅・副高さ 第 2 版（#173）",
		 "0 と負・境界が主高さの半分か・副を書かなくても壊れるかを採る")
{
	gSDK->DefineCustomObject(kMnrPioName, kCustomObjectPrefNever);
	probe.log("第 1 版で確定: 種別 1/2 の断面は H 形（12 頂点）。主幅=フランジ幅 B / "
			  "主高さ=全せい D / 副幅=ウェブ厚 b / 副高さ=フランジ厚 d。");
	probe.log("頂点は (-B/2,0) (-B/2,d) (-b/2,d) (-b/2,D-d) (-B/2,D-d) (-B/2,D) "
			  "と、その点対称の 6 点。2d>D で自己交差して実体 84 が作られない。");

	double originY = 0.0;

	// ---- [G] 0 と負で壊れるのはどちらの欄か --------------------------------
	// 第 1 版は副幅・副高さを**同時に** 0 にした回しか無く、切り分けが付いていない。
	probe.log("=== [G] 0 と負を 1 欄ずつ（主 300x600 固定）===");
	MnrCase(probe, "[G1] 副幅だけ 0", originY, 1, 300.0, 600.0, true, 0.0, 100.0);
	MnrCase(probe, "[G2] 副高さだけ 0", originY, 1, 300.0, 600.0, true, 100.0, 0.0);
	MnrCase(probe, "[G3] 両方 0（第 1 版 [A5] の再現）", originY, 1, 300.0, 600.0, true, 0.0, 0.0);
	MnrCase(probe, "[G4] 副幅だけ負", originY, 1, 300.0, 600.0, true, -100.0, 100.0);
	MnrCase(probe, "[G5] 副高さだけ負", originY, 1, 300.0, 600.0, true, 100.0, -100.0);

	// ---- [H] 境界は「主高さの半分」か、300 という絶対値か --------------------
	probe.log("=== [H] 主高さを変えて境界を採る（副高さ＝主高さの半分とその +1）===");
	MnrCase(probe, "[H1] 主高さ400 副高さ200", originY, 1, 300.0, 400.0, true, 100.0, 200.0);
	MnrCase(probe, "[H2] 主高さ400 副高さ201", originY, 1, 300.0, 400.0, true, 100.0, 201.0);
	MnrCase(probe, "[H3] 主高さ1000 副高さ500", originY, 1, 300.0, 1000.0, true, 100.0, 500.0);
	MnrCase(probe, "[H4] 主高さ1000 副高さ501", originY, 1, 300.0, 1000.0, true, 100.0, 501.0);

	// ---- [I] 副を 1 文字も書かなくても壊れるのではないか ---------------------
	// **本命。** 副高さの既定は 100 なので、主高さが 200 未満なら 2d > D になる。
	// そうなら「Minor を触らない」という当面の指針は安全ではない。
	probe.log("=== [I] 副 2 欄へ**書かずに**主高さだけ小さくする（既定は副 100 / 100）===");
	MnrCase(probe, "[I1] 主高さ250", originY, 1, 300.0, 250.0, false, 0.0, 0.0);
	MnrCase(probe, "[I2] 主高さ200", originY, 1, 300.0, 200.0, false, 0.0, 0.0);
	MnrCase(probe, "[I3] 主高さ199", originY, 1, 300.0, 199.0, false, 0.0, 0.0);
	MnrCase(probe, "[I4] 主高さ150", originY, 1, 300.0, 150.0, false, 0.0, 0.0);
	MnrCase(probe, "[I5] 主高さ105（2x4 の実寸）", originY, 1, 89.0, 105.0, false, 0.0, 0.0);
	MnrCase(probe, "[I6] 主幅50（副幅 100 が食み出す）", originY, 1, 50.0, 600.0, false, 0.0, 0.0);
	MnrCase(probe, "[I7] 木で主高さ150", originY, 2, 300.0, 150.0, false, 0.0, 0.0);

	probe.log("おわり");
}
