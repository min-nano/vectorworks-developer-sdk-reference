//
//	probes/runtime/member-minor-dims/probe.cpp
//
//	[issue #173] **構造材の `MinorBreadth` / `MinorDepth` は何の寸法か。第 3 版——境界を言い切る。**
//
//	第 1・2 版で確定したこと（PR #174 の 1・3 本目。VW 2026 / mac・新規の空図面・計 42 個体）
//	------------------------------------------------------------------------------------
//	**`MemberType`=`1`（コンクリート）/ `2`（木）の断面は矩形ではなく H 形である**
//	（群の子は常に 12 頂点の `Polygon`）。頂点は例外なくこの形:
//
//	  (-B/2,0) (-B/2,d) (-b/2,d) (-b/2,D-d) (-B/2,D-d) (-B/2,D)
//	  ( B/2,D) ( B/2,D-d) ( b/2,D-d) ( b/2,d) ( B/2,d) ( B/2,0)
//
//	    B = `MajorBreadth`（主幅＝フランジ幅）   D = `MajorDepth`（主高さ＝全せい）
//	    b = `MinorBreadth`（副幅＝**ウェブ厚**） d = `MinorDepth`（副高さ＝**フランジ厚**）
//
//	  ・**`d` は上下 2 枚のフランジに 1 回ずつ効く。** 境界は **`D`/2** で、絶対値ではない
//	    ——`D`=400 は 200 で生き 201 で壊れ、`D`=600 は 300 / 301、`D`=1000 は 500 / 501。
//	  ・**壊すのは `d`<0・`d`>`D`/2・`b`=0 の 3 つ。** `d`=0 は壊さない（実体 84 が残る）。
//	    `b` は負（-100）でも `B` 超（400）でも壊れない。`b`>`B` ではウェブが食み出して
//	    **外接がウェブ幅まで広がる**。
//	  ・**副へ 1 文字も書かなくても壊れる。** `d` の既定は **100** なので、`MajorDepth` を
//	    199 以下にしただけで壊れた（250・200 は生き、199・150 で壊れた。木でも同じ）。
//	  ・木（`2`）はコンクリート（`1`）と同一。`ProfileShape` は種別 1 では完全に無視される。
//	  ・壊しても `d` を戻せば直る。壊れている間も `ResetObject`=`true`・読み戻し一致・
//	    `B1`/`D1` は書いた値を写す——**幾何を測る以外に気付く道は無い。**
//
//	この版で閉じること——**`d`>`D`/2 の例外 1 件**
//	----------------------------------------------
//	第 2 版の `[I5]`（主 89 × 105・副 100 / 100）は **`d`=100 > `D`/2=52.5 なのに実体 84 が
//	作られた**。他の `d`>`D`/2 の回（10 件）はすべて壊れているので、**この 1 件だけが例外**で
//	ある。`[I5]` が他と違うのは **`b`(100) > `B`(89)**——ウェブがフランジより広い——ことだけ。
//
//	  J **`b`>`B` なら `d`>`D`/2 でも壊れないのか。** そうなら壊れる条件は
//	    「`d`<0 ／ `b`=0 ／（`d`>`D`/2 **かつ** `b`≤`B`）」と言い切れる。境界が `b`=`B` の
//	    どちら側にあるかも採る（`b`=`B` ちょうど・`B`+1）。負の `d` も `b`>`B` で救われるのか。
//
//	判定はこれまでと同じ **(1) 実体84 の有無 (2) 高さZ (3) 群の子の頂点列**。
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

VW_PROBE("member-minor-dims", "構造材の副幅・副高さ 第 3 版（#173）",
		 "d>D/2 でも壊れなかった 1 件の正体——b>B なら救われるのかを採る")
{
	gSDK->DefineCustomObject(kMnrPioName, kCustomObjectPrefNever);
	probe.log("B=主幅(フランジ幅) D=主高さ(全せい) b=副幅(ウェブ厚) d=副高さ(フランジ厚)。"
			  "ここまでの規則: d<0 / d>D/2 / b=0 で実体 84 が作られない。");
	probe.log("ただし第 2 版 [I5]（B=89 D=105 b=100 d=100）だけは d>D/2 なのに作られた。"
			  "他と違うのは b>B であること。ここを振って言い切る。");

	double originY = 0.0;

	// ---- [J] `b` と `B` の大小が `d`>`D`/2 を救うのか ------------------------
	// J1〜J4 は B=300 / D=600 / d=400（>D/2=300）を固定し、**b だけ**を動かす。
	probe.log("=== [J] B=300 D=600 d=400（>D/2）で b だけを動かす ===");
	MnrCase(probe, "[J1] b=200（<B）", originY, 1, 300.0, 600.0, true, 200.0, 400.0);
	MnrCase(probe, "[J2] b=300（=B）", originY, 1, 300.0, 600.0, true, 300.0, 400.0);
	MnrCase(probe, "[J3] b=301（B+1）", originY, 1, 300.0, 600.0, true, 301.0, 400.0);
	MnrCase(probe, "[J4] b=400（>B）", originY, 1, 300.0, 600.0, true, 400.0, 400.0);

	// [K] は第 2 版 [I5] そのもの（B=89 D=105 d=100）で b だけを動かし、
	// **b を B 以下にしたら壊れるか**——例外が b>B のせいだったかを直に確かめる。
	probe.log("=== [K] 第 2 版 [I5]（B=89 D=105 d=100）で b だけを動かす ===");
	MnrCase(probe, "[K1] b=50（<B=89）", originY, 1, 89.0, 105.0, true, 50.0, 100.0);
	MnrCase(probe, "[K2] b=89（=B）", originY, 1, 89.0, 105.0, true, 89.0, 100.0);
	MnrCase(probe, "[K3] b=90（B+1）", originY, 1, 89.0, 105.0, true, 90.0, 100.0);
	MnrCase(probe, "[K4] b=100（>B。[I5] の再現）", originY, 1, 89.0, 105.0, true, 100.0, 100.0);

	// ---- [L] 負の d と b=0 も b>B で救われるのか ----------------------------
	probe.log("=== [L] 負の d ／ b=0 を b>B と組む ===");
	MnrCase(probe, "[L1] b=400（>B） d=-100", originY, 1, 300.0, 600.0, true, 400.0, -100.0);
	MnrCase(probe, "[L2] b=0 d=400（>D/2）", originY, 1, 300.0, 600.0, true, 0.0, 400.0);
	MnrCase(probe, "[L3] 木で b=400（>B） d=400", originY, 2, 300.0, 600.0, true, 400.0, 400.0);

	probe.log("おわり");
}
