//
//	probes/runtime/member-minor-dims/probe.cpp
//
//	[issue #173] **構造材の `MinorBreadth` / `MinorDepth` は何の寸法か。**
//	（ある値を書くと 3D 実体——型 84 `kCSGTree`——が消える。#169 / PR #170 の副産物）
//
//	いま分かっていること（PR #170 の 4 本目 `[N2]`。VW 2026 / mac・新規の空図面）
//	------------------------------------------------------------------------
//	`MemberType`=`1`（コンクリート）/ `2`（木）では **`MajorBreadth` × `MajorDepth` が
//	そのまま外接になる**（300 × 600 → 200 × 800）。`Minor` の 2 欄は**外接に出ない**が、
//	`MajorBreadth`=300 / `MajorDepth`=600 のまま
//
//	  ・`MinorBreadth`=250 だけ    → 変化なし（型 84 あり・高さ 600）
//	  ・さらに `MinorDepth`=500    → **型 84 が消え、`GetObjectCube` の高さが 0.0**
//	                                 群の子が `Polyline` → `Polygon` へ変わった
//
//	`ResetObject` は `true`・4 欄とも読み戻せる・`B`/`B1`/`D`/`D1` も追随する——
//	**書き手からは成功と区別が付かない。** 既定は `B`=300 `B1`=100 `D`=600 `D1`=100
//	なので、`Minor` の既定は 100 / 100 である（PR #170 の `[M0]` の表示欄）。
//
//	この版で採ること
//	----------------
//	  A **断面の頂点を読む。** 外接だけでは「矩形のどこにも効かない」としか言えない。
//	    プロファイル（断面）群の子の**頂点列**を出せば、断面が矩形なのか H 形なのかが
//	    その場で分かり、`Minor` を振ったときにどの座標が動くかで**何の寸法か**が決まる。
//	    群の 2D 座標は x＝材の幅(Y)・y＝材の高さ(Z)（外接 `(-150,0→150,600)` より）。
//	  B **3D が消える境界を振る。** `Minor` を 1 欄ずつ動かし、どちらがいくつで壊れるかを
//	    採る。**`Major` の半分**（`MajorDepth`=600 に対する 300）が境なら、`Minor` は
//	    「両側に 2 回効く寸法」＝フランジ厚・ウェブ厚の類だと言い切れる。`2`（木）も同じ。
//	  C **壊れたことを知る道と、直る道。** 型 84 の有無・`GetObjectCube` の高さ・
//	    群の子の型のうち、どれが指標になるか。良い値を書き戻せば戻るのか。
//	  D **`ProfileShape` は `MemberType`=1 でも断面の形を選ぶのか。** #170 は外接しか
//	    見ていないので、`角形鋼管` / `溝形鋼` を与えて**頂点で**確かめる。
//
//	判定は外接（**幅Y** / **高さZ**）と**型 84 の有無**、そして**群の子の頂点列**。
//	走らせるのは新規の空図面。図面は壊れる前提（undo イベントは開かない）。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kMnrPioName = "StructuralMember";

	// 断面の既定（`MemberType`=1 / 2 のとき）。この 2 つは全試験で固定する。
	const double kMnrMajorBreadth = 300.0;
	const double kMnrMajorDepth = 600.0;

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

	// **この調査の本命。** 断面の輪郭を頂点で出す。矩形なら 4、H 形なら 12 のはず。
	// 型 3（Rect）は頂点を持たないので外接だけ、型 5 / 21 は VWPolygon2DObj で読む。
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
			probe.log("  " + tag + " 群=nil");
			return;
		}
		WorldRect groupBounds;
		std::string head = "  " + tag + " 群";
		if (gSDK->GetObjectBounds(group, groupBounds))
			head += MnrRectText(groupBounds);
		probe.log(head);
		for (MCObjectHandle kid = gSDK->FirstMemberObj(group); kid != nil;
			 kid = gSDK->NextObject(kid))
		{
			const short type = gSDK->GetObjectTypeN(kid);
			if (type == 0)
				continue;
			std::string kidLine =
				"    " + tag + " 群の子 型" + MnrWhole(type) + "(" + MnrTypeName(type) + ")";
			WorldRect kidBounds;
			if (gSDK->GetObjectBounds(kid, kidBounds))
				kidLine += " 外接" + MnrRectText(kidBounds);
			probe.log(kidLine);
			MnrDumpVertices(probe, kid, "      " + tag + " ");
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

	// 4 欄をそのまま読み戻して 1 行に出す（**書けたこと**と**効いたこと**を分けて見る）。
	void MnrDumpFields(vwprobe::Report& probe, MCObjectHandle member, const std::string& tag)
	{
		VWParametricObj pio(member);
		probe.log("  " + tag +
				  " 読み戻し MajorBreadth=" + MnrCoord(pio.GetParamReal(TXString("MajorBreadth"))) +
				  " MinorBreadth=" + MnrCoord(pio.GetParamReal(TXString("MinorBreadth"))) +
				  " MajorDepth=" + MnrCoord(pio.GetParamReal(TXString("MajorDepth"))) +
				  " MinorDepth=" + MnrCoord(pio.GetParamReal(TXString("MinorDepth"))) +
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

	// 1 個体を「`MemberType` を倒して 4 欄を書いて reset して測る」まで通す。
	// **4 欄は毎回 4 つとも明示的に書く**（既定に頼らない）。
	MCObjectHandle MnrCase(vwprobe::Report& probe, const std::string& tag, double& originY,
						   int memberType, double minorBreadth, double minorDepth)
	{
		MCObjectHandle member = MnrMake(probe, tag, originY);
		originY += 2000.0;
		if (member == nil)
			return nil;
		gSDK->ResetObject(member);
		MnrSetValue(probe, member, "MemberType", MnrWhole(memberType));
		MnrSetReal(probe, member, "MajorBreadth", kMnrMajorBreadth);
		MnrSetReal(probe, member, "MajorDepth", kMnrMajorDepth);
		MnrSetReal(probe, member, "MinorBreadth", minorBreadth);
		MnrSetReal(probe, member, "MinorDepth", minorDepth);
		const std::string reset = MnrReset(member);
		MnrSnap(probe,
				tag + " 種別" + MnrWhole(memberType) + " 主" + MnrCoord(kMnrMajorBreadth) + "x" +
					MnrCoord(kMnrMajorDepth) + " 副幅" + MnrCoord(minorBreadth) + " 副高さ" +
					MnrCoord(minorDepth) + " reset=" + reset,
				member);
		return member;
	}
} // namespace

VW_PROBE("member-minor-dims", "構造材の副幅・副高さは何の寸法か（#173）",
		 "断面の頂点を読み、3D 実体が消える境界を振る")
{
	gSDK->DefineCustomObject(kMnrPioName, kCustomObjectPrefNever);
	probe.log("主幅=300 / 主高さ=600 に固定し、副幅・副高さだけを振る。群の 2D 座標は "
			  "x=材の幅(Y) / y=材の高さ(Z)。");
	probe.log("見どころ: (1) 群の子の頂点列——矩形なら 4 頂点、H 形なら 12 頂点 "
			  "(2) 実体84 の有無 (3) 高さZ が 0 になるか。");

	double originY = 0.0;

	// ---- [A] 断面の形と、副 2 欄がどの座標を動かすか ------------------------
	probe.log("=== [A] 断面の頂点を読む（副 2 欄がどの座標を動かすか）===");
	MnrCase(probe, "[A0]", originY, 1, 100.0, 100.0); // 既定の副（B1=D1=100）
	MnrCase(probe, "[A1]", originY, 1, 50.0, 100.0);  // 副幅だけ小さく
	MnrCase(probe, "[A2]", originY, 1, 100.0, 50.0);  // 副高さだけ小さく
	MnrCase(probe, "[A3]", originY, 1, 200.0, 100.0); // 副幅だけ大きく
	MnrCase(probe, "[A4]", originY, 1, 100.0, 200.0); // 副高さだけ大きく
	MnrCase(probe, "[A5]", originY, 1, 0.0, 0.0);	  // 両方 0

	// ---- [B] 3D が消える境界 -----------------------------------------------
	// 主高さ 600 の**半分**（300）が境なら、副高さは「上下 2 回効く寸法」である。
	probe.log("=== [B] 副高さを振る（主高さ 600・副幅 100 固定）===");
	MnrCase(probe, "[B1]", originY, 1, 100.0, 250.0);
	MnrCase(probe, "[B2]", originY, 1, 100.0, 299.0);
	MnrCase(probe, "[B3]", originY, 1, 100.0, 300.0);
	MnrCase(probe, "[B4]", originY, 1, 100.0, 301.0);
	MnrCase(probe, "[B5]", originY, 1, 100.0, 500.0);
	MnrCase(probe, "[B6]", originY, 1, 100.0, 600.0);

	probe.log("=== [C] 副幅を振る（主幅 300・副高さ 100 固定）===");
	MnrCase(probe, "[C1]", originY, 1, 149.0, 100.0);
	MnrCase(probe, "[C2]", originY, 1, 150.0, 100.0);
	MnrCase(probe, "[C3]", originY, 1, 151.0, 100.0);
	MnrCase(probe, "[C4]", originY, 1, 299.0, 100.0);
	MnrCase(probe, "[C5]", originY, 1, 300.0, 100.0);
	MnrCase(probe, "[C6]", originY, 1, 400.0, 100.0);

	// PR #170 の [N2] と同じ組み合わせ（250 / 500）。再現するかを確かめる。
	probe.log("=== [D] PR #170 [N2] の再現（副 250 / 500）と、木（種別 2）で同じか ===");
	MnrCase(probe, "[D1]", originY, 1, 250.0, 500.0);
	MnrCase(probe, "[D2]", originY, 2, 250.0, 500.0);
	MnrCase(probe, "[D3]", originY, 2, 100.0, 100.0);
	MnrCase(probe, "[D4]", originY, 2, 100.0, 300.0);
	MnrCase(probe, "[D5]", originY, 2, 100.0, 301.0);
	MnrCase(probe, "[D6]", originY, 2, 300.0, 100.0);

	// ---- [E] 壊れたことを知る道・直る道 -------------------------------------
	probe.log("=== [E] 壊してから良い値を書き戻す（直るか・何が指標になるか）===");
	{
		MCObjectHandle member = MnrCase(probe, "[E1] 壊す", originY, 1, 250.0, 500.0);
		if (member != nil)
		{
			MnrDumpFields(probe, member, "[E1] 壊れた直後");
			MnrSetReal(probe, member, "MinorDepth", 100.0);
			const std::string reset = MnrReset(member);
			MnrSnap(probe, "[E1] 副高さ=100 へ書き戻した reset=" + reset, member);
			MnrDumpFields(probe, member, "[E1] 戻した後");
		}
	}

	// ---- [F] `ProfileShape` は種別 1 でも断面の形を選ぶのか -------------------
	// #170 は外接しか見ていない。頂点で見れば「矩形のまま」か「形が変わる」かが分かる。
	probe.log("=== [F] 種別 1 で ProfileShape を振る（頂点で見る）===");
	{
		MCObjectHandle member = MnrCase(probe, "[F1] 角形鋼管", originY, 1, 100.0, 100.0);
		if (member != nil)
		{
			MnrSetValue(probe, member, "ProfileShape", "角形鋼管");
			const std::string reset = MnrReset(member);
			MnrSnap(probe, "[F1] ProfileShape=角形鋼管 reset=" + reset, member);
		}
	}
	{
		MCObjectHandle member = MnrCase(probe, "[F2] 溝形鋼", originY, 1, 100.0, 100.0);
		if (member != nil)
		{
			MnrSetValue(probe, member, "ProfileShape", "溝形鋼");
			const std::string reset = MnrReset(member);
			MnrSnap(probe, "[F2] ProfileShape=溝形鋼 reset=" + reset, member);
		}
	}

	probe.log("おわり");
}
