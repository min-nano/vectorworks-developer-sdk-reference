//
//	probes/runtime/member-profile-source/probe.cpp
//
//	[issue #169] **構造材の断面（太さ）は何が決めているのか。**
//
//	分かっていること（PR #168 の 1 本目。VW 2026 / mac・新規の空図面）
//	------------------------------------------------------------------
//	`MajorDepth` へ 600 → 800 → 1000 → 600 と書き、毎回 `ResetObject` を呼んでも、
//	**値は毎回そのまま読み戻せるのに外接も立方も 1mm も動かなかった**。子の数と型の
//	内訳も一度も動かない。`ResetObject` の戻り値も `true` のままなので、
//	**書き手の側からは「効いた」と「効かなかった」の区別が付かない**。
//
//	この調査で確かめること（issue の 3 問）
//	--------------------------------------
//	1. **何が断面を決めているのか。** 見立ては 2 つある:
//	   (a) `ProfileShape` / `ProfileSeries` / `ProfileSize` / `ProfileSymbol` のどれか
//	       （＝既定がカタログのシンボル断面なので `MajorDepth` は使われない）
//	   (b) **プロファイル（断面）グループ**——`ISDK::GetCustomObjectProfileGroup` /
//	       `SetCustomObjectProfileGroup` が指す、PIO が持つ 2D 図形。
//	   既定の 1 本の外接は Y 幅 165.6・立方 Z が ±156.7（＝断面 165.6 × 313.4）で、
//	   **`MajorBreadth`=300 / `MajorDepth`=600 のどちらとも合わない**。だから断面は
//	   この 4 欄かグループのどちらかから来ている。**両方を機械で測って決める。**
//	2. **`MajorDepth` が効く条件はあるか。** 断面を指す欄を空にする・`ProfileShape` を
//	   振る・グループを空にする——のどれかで効くようになるなら、その組み合わせが答え。
//	3. **効かない欄を書いたときに手がかりは残るか。** `ResetObject` の戻り値・読み戻し・
//	   静的表示欄（`B` / `B1` / `D` / `D1`）・断面を指す 4 欄・グループの外接——
//	   **書き手が見られるものを全部並べて、1 つでも動くかを見る。**
//
//	試験（すべて **1 値 1 個体**。同じ個体で振って比べない——Findings「調査の作法」）
//	--------------------------------------------------------------------------------
//	  A  既定の 1 本の素性（4 欄の欄型・値・選択肢／子の内訳／グループの中身）
//	  B  寸法 4 欄（`MajorBreadth` / `MinorBreadth` / `MajorDepth` / `MinorDepth`）を
//	     書く → 何が動き、何が動かないか（＝問 3 の答え）
//	  C  断面を指す 4 欄を **1 欄ずつ空にする** → そのあと寸法 4 欄を書く
//	  D  `ProfileShape` に 0〜7 を書く → そのあと寸法 4 欄を書く
//	  E  **プロファイルグループを矩形（Y 120 × Z 240）へ差し替える**
//	  F  **プロファイルグループの子を消す**（空にする）→ そのあと寸法 4 欄を書く
//
//	読み方: 行末の `幅Y` と `高さZ` が **[A] の 165.6 / 313.4 から動いたか**だけを見る。
//	動いた行の直前に書いたものが断面を決めている。
//
//	走らせるのは**新規の空図面**。図面は壊れる前提（undo イベントは開かない）。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kProfPioName = "StructuralMember";

	// 断面を指していると疑っている 4 欄（issue の見立て (a)）。
	const char* const kProfProfileFields[] = {"ProfileShape", "ProfileSeries", "ProfileSize",
											  "ProfileSymbol"};
	const size_t kProfProfileCount = sizeof(kProfProfileFields) / sizeof(kProfProfileFields[0]);

	// 断面の寸法らしい名前の 4 欄。**これが効くかどうかがこの調査の的。**
	const char* const kProfDimFields[] = {"MajorBreadth", "MinorBreadth", "MajorDepth",
										  "MinorDepth"};
	const double kProfDimValues[] = {200.0, 60.0, 800.0, 90.0};
	const size_t kProfDimCount = sizeof(kProfDimFields) / sizeof(kProfDimFields[0]);

	// OIP の読み取り専用の表示欄。寸法欄を書いたときに追随するかを見る（問 3）。
	const char* const kProfStaticFields[] = {"B", "B1", "D", "D1"};
	const size_t kProfStaticCount = sizeof(kProfStaticFields) / sizeof(kProfStaticFields[0]);

	std::string ProfText(const TXString& src)
	{
		const char* utf8 = static_cast<const char*>(src);
		return utf8 ? std::string(utf8) : std::string();
	}

	std::string ProfWhole(long long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%lld", value);
		return std::string(buffer);
	}

	std::string ProfCoord(double value)
	{
		char buffer[48];
		std::snprintf(buffer, sizeof(buffer), "%.1f", value);
		return std::string(buffer);
	}

	const char* ProfTypeName(short type)
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
		case 10:
			return "Text";
		case 11:
			return "Group";
		case 15:
			return "Symbol";
		case 21:
			return "Polyline";
		case 24:
			return "Extrude";
		case 40:
			return "Mesh";
		case 84:
			return "CSGTree";
		case 85:
			return "BoundaryRep";
		case 86:
			return "Parametric";
		case 90:
			return "UndoPlaceholder";
		case 95:
			return "Solid";
		default:
			return "?";
		}
	}

	// EFieldStyle（Kernel/API/MiniCadCallBacks.h:1063）の値 → 名前。
	const char* ProfFieldStyleName(int style)
	{
		switch (style)
		{
		case 1:
			return "LongInt";
		case 2:
			return "Boolean";
		case 3:
			return "Real";
		case 4:
			return "Text";
		case 7:
			return "CoordDisp";
		case 8:
			return "PopUp";
		case 9:
			return "Radio";
		case 14:
			return "StaticText";
		case 15:
			return "ControlPoint";
		case 18:
			return "ClassesPopup";
		case 20:
			return "Angle";
		case 23:
			return "Class";
		case 30:
			return "SymDef";
		default:
			return "?";
		}
	}

	std::string ProfRectText(const WorldRect& rect)
	{
		return "(" + ProfCoord(double(rect.left)) + "," + ProfCoord(double(rect.bottom)) + "→" +
			   ProfCoord(double(rect.right)) + "," + ProfCoord(double(rect.top)) + ")";
	}

	// -------------------------------------------------------------------
	// **判定はここだけ。** 断面の大きさは PIO の外接の **Y 方向の幅**（材は X 方向へ
	// 伸びる）と、立方の **Z 方向の高さ**に出る。[A] の 165.6 / 313.4 から動いたかを見る。
	void ProfSnap(vwprobe::Report& probe, const std::string& tag, MCObjectHandle pio)
	{
		size_t placeholders = 0;
		size_t geometry = 0;
		std::vector<short> types;
		std::vector<size_t> counts;
		for (MCObjectHandle child = gSDK->FirstMemberObj(pio); child != nil;
			 child = gSDK->NextObject(child))
		{
			const short type = gSDK->GetObjectTypeN(child);
			if (type == 90)
			{
				++placeholders;
				continue;
			}
			if (type == 0)
				continue;
			++geometry;
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

		std::string line = tag + " 幾何" + ProfWhole(static_cast<long long>(geometry)) + " 置石" +
						   ProfWhole(static_cast<long long>(placeholders)) + " 型[";
		for (size_t at = 0; at < types.size(); ++at)
		{
			if (at != 0)
				line += ",";
			line += ProfWhole(types[at]);
			line += std::string("(") + ProfTypeName(types[at]) + ")x";
			line += ProfWhole(static_cast<long long>(counts[at]));
		}
		line += "]";

		WorldRect bounds;
		if (gSDK->GetObjectBounds(pio, bounds))
		{
			line += " 外接" + ProfRectText(bounds);
			line += " **幅Y=" + ProfCoord(double(bounds.top) - double(bounds.bottom)) + "**";
		}
		else
		{
			line += " 外接=（取れず）";
		}

		WorldCube cube;
		gSDK->GetObjectCube(pio, cube);
		line += " 立方Z=(" + ProfCoord(double(cube.MinZ())) + "→" + ProfCoord(double(cube.MaxZ())) +
				")";
		line += " **高さZ=" + ProfCoord(double(cube.MaxZ()) - double(cube.MinZ())) + "**";

		// **グループも一緒に測る。** 断面がグループから来ているなら、断面が動くときは
		// グループも動く（逆も然り）。
		MCObjectHandle group = gSDK->GetCustomObjectProfileGroup(pio);
		if (group == nil)
		{
			line += " 群=nil";
		}
		else
		{
			WorldRect groupBounds;
			if (gSDK->GetObjectBounds(group, groupBounds))
				line += " 群" + ProfRectText(groupBounds);
			else
				line += " 群=（外接取れず）";
		}
		probe.log(line);
	}

	// 子を 1 つずつ並べる（[A] と [E] だけ。どの子が断面なのかを突き合わせるため）。
	void ProfDumpChildren(vwprobe::Report& probe, MCObjectHandle container)
	{
		if (container == nil)
			return;
		for (MCObjectHandle child = gSDK->FirstMemberObj(container); child != nil;
			 child = gSDK->NextObject(child))
		{
			const short type = gSDK->GetObjectTypeN(child);
			if (type == 90 || type == 0)
				continue;
			std::string line =
				std::string("    子 型") + ProfWhole(type) + "(" + ProfTypeName(type) + ")";
			WorldRect bounds;
			if (gSDK->GetObjectBounds(child, bounds))
				line += " 外接" + ProfRectText(bounds);
			WorldCube cube;
			gSDK->GetObjectCube(child, cube);
			line += " 立方Z=(" + ProfCoord(double(cube.MinZ())) + "→" +
					ProfCoord(double(cube.MaxZ())) + ")";
			probe.log(line);
		}
	}

	// プロファイル（断面）グループを 3 経路で引いて、中身まで出す（[A]）。
	void ProfDumpGroups(vwprobe::Report& probe, MCObjectHandle member)
	{
		struct ProfGroupRoute
		{
			const char* name;
			MCObjectHandle handle;
		};
		const ProfGroupRoute routes[] = {
			{"GetCustomObjectProfileGroup", gSDK->GetCustomObjectProfileGroup(member)},
			{"GetCustomObjectSecondProfileGroup", gSDK->GetCustomObjectSecondProfileGroup(member)},
			{"GetCustomObjectProfileGroupInAux", gSDK->GetCustomObjectProfileGroupInAux(member)},
		};
		for (size_t at = 0; at < sizeof(routes) / sizeof(routes[0]); ++at)
		{
			if (routes[at].handle == nil)
			{
				probe.log(std::string("  ") + routes[at].name + " = **nil**");
				continue;
			}
			std::string line = std::string("  ") + routes[at].name + " = 型" +
							   ProfWhole(gSDK->GetObjectTypeN(routes[at].handle)) + "(" +
							   ProfTypeName(gSDK->GetObjectTypeN(routes[at].handle)) + ")";
			WorldRect bounds;
			if (gSDK->GetObjectBounds(routes[at].handle, bounds))
				line += " 外接" + ProfRectText(bounds);
			probe.log(line);
			ProfDumpChildren(probe, routes[at].handle);
		}
	}

	// 1 欄の素性（欄型・値・選択肢）を 1 行で出す（[A]）。
	void ProfDumpParam(vwprobe::Report& probe, MCObjectHandle member, const char* name)
	{
		VWParametricObj pio(member);
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.log(std::string("  ") + name + " = **名前で引けない**");
			return;
		}
		std::string line = std::string("  ") + name + " 索引" +
						   ProfWhole(static_cast<long long>(index)) + " 欄型" +
						   ProfWhole(static_cast<int>(pio.GetParamStyle(index))) + "(" +
						   ProfFieldStyleName(static_cast<int>(pio.GetParamStyle(index))) + ")" +
						   " 値=[" + ProfText(pio.GetParamValue(index)) + "]" +
						   " 実数=" + ProfCoord(pio.GetParamReal(index)) +
						   " シンボル索引=" + ProfWhole(pio.GetParamSymDef(index));

		TXStringSTLArray keys;
		TXStringSTLArray shown;
		const bool gotKeys = pio.GetParamChoices(index, keys);
		pio.GetParamLocalizedChoices(index, shown);
		line += " 選択肢=" + ProfWhole(static_cast<long long>(keys.size())) +
				(gotKeys ? "" : "（取れず）");
		for (size_t at = 0; at < keys.size() && at < 8; ++at)
		{
			line += " [" + ProfText(keys[at]) + "=";
			line += (at < shown.size() ? ProfText(shown[at]) : std::string("?"));
			line += "]";
		}
		probe.log(line);
	}

	MCObjectHandle ProfMake(vwprobe::Report& probe, const std::string& tag, double originY)
	{
		VWPolygon2DObj path({VWPoint2D(0.0, originY), VWPoint2D(3000.0, originY)});
		MCObjectHandle pathHandle = path.GetThisObject();
		if (pathHandle == nil)
		{
			probe.fail(tag + " パス（VWPolygon2DObj）を作れなかった");
			return nil;
		}
		MCObjectHandle member = gSDK->CreateCustomObjectPath(kProfPioName, pathHandle, nil, false);
		if (member == nil)
			probe.fail(tag + " CreateCustomObjectPath(StructuralMember) が nil を返した");
		return member;
	}

	std::string ProfReset(MCObjectHandle member)
	{
		return gSDK->ResetObject(member) ? "true" : "**false**";
	}

	// 文字として書いて読み戻す（「書けている」ことの確認）。
	void ProfSetValue(vwprobe::Report& probe, MCObjectHandle member, const char* name,
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
		const std::string back = ProfText(pio.GetParamValue(index));
		probe.log(std::string("  ") + name + " ← [" + value + "] 読み戻し=[" + back + "]" +
				  (back == value ? "" : "  ← **定着せず**"));
	}

	// **issue と同じ書き方**（`SetParamReal`）で寸法 4 欄を書く。
	void ProfSetDims(vwprobe::Report& probe, MCObjectHandle member)
	{
		VWParametricObj pio(member);
		std::string line = "  寸法 4 欄 ← ";
		for (size_t at = 0; at < kProfDimCount; ++at)
		{
			const size_t index = pio.GetParamIndex(TXString(kProfDimFields[at]));
			if (index == size_t(-1) || index >= pio.GetParamsCount())
			{
				line += std::string(kProfDimFields[at]) + "=（引けず） ";
				continue;
			}
			pio.SetParamReal(index, kProfDimValues[at]);
			line += std::string(kProfDimFields[at]) + "=" + ProfCoord(kProfDimValues[at]) +
					"→読み戻し" + ProfCoord(pio.GetParamReal(index)) + " ";
		}
		probe.log(line);
	}

	// 読み取り専用の表示欄（`B` / `B1` / `D` / `D1`）と断面を指す 4 欄を並べる。
	// **寸法欄を書いたときにここが動くなら、それが「手がかり」になる**（問 3）。
	void ProfDumpWitness(vwprobe::Report& probe, MCObjectHandle member, const std::string& tag)
	{
		VWParametricObj pio(member);
		std::string line = "  " + tag + " 表示欄";
		for (size_t at = 0; at < kProfStaticCount; ++at)
			line += std::string(" ") + kProfStaticFields[at] + "=[" +
					ProfText(pio.GetParamValue(TXString(kProfStaticFields[at]))) + "]";
		line += " ／ 断面欄";
		for (size_t at = 0; at < kProfProfileCount; ++at)
			line += std::string(" ") + kProfProfileFields[at] + "=[" +
					ProfText(pio.GetParamValue(TXString(kProfProfileFields[at]))) + "]";
		probe.log(line);
	}

	// 「寸法 4 欄を書いて reset して測る」——どの試験の末尾でも同じことをする。
	void ProfTryDims(vwprobe::Report& probe, MCObjectHandle member, const std::string& tag)
	{
		ProfSetDims(probe, member);
		const std::string reset = ProfReset(member);
		ProfSnap(probe, tag + " 寸法 4 欄を書いた reset=" + reset, member);
		ProfDumpWitness(probe, member, tag + " 寸法を書いた後");
	}
} // namespace

VW_PROBE("member-profile-source", "何が構造材の断面を決めるのか",
		 "断面を指す 4 欄とプロファイルグループを測り、寸法 4 欄が効く条件を探す")
{
	gSDK->DefineCustomObject(kProfPioName, kCustomObjectPrefNever);
	probe.log("読み方: 行末の 幅Y / 高さZ が [A] の値から動いたかだけを見る。"
			  "動いた行の直前に書いたものが断面を決めている。");

	double originY = 0.0;

	// ---- [A] 既定の 1 本の素性 ------------------------------------------
	{
		MCObjectHandle member = ProfMake(probe, "[A]", originY);
		originY += 2000.0;
		if (member != nil)
		{
			const std::string reset = ProfReset(member);
			ProfSnap(probe, "[A] 既定のまま reset=" + reset + "（以降はこれとの差で読む）", member);
			probe.log("[A] 断面を指すと疑っている 4 欄の素性:");
			for (size_t at = 0; at < kProfProfileCount; ++at)
				ProfDumpParam(probe, member, kProfProfileFields[at]);
			probe.log("[A] 寸法 4 欄の素性（既定値がここに出る）:");
			for (size_t at = 0; at < kProfDimCount; ++at)
				ProfDumpParam(probe, member, kProfDimFields[at]);
			ProfDumpWitness(probe, member, "[A] 既定");
			probe.log("[A] プロファイル（断面）グループ:");
			ProfDumpGroups(probe, member);
			probe.log("[A] 描かれた子の内訳（どれが断面か）:");
			ProfDumpChildren(probe, member);
		}
	}

	// ---- [B] 寸法 4 欄を書く（＝issue の再現） ---------------------------
	// **問 3 の答えがここに出る。** 書いた後に動くものが 1 つでもあるか。
	{
		MCObjectHandle member = ProfMake(probe, "[B]", originY);
		originY += 2000.0;
		if (member != nil)
		{
			const std::string first = ProfReset(member);
			ProfSnap(probe, "[B] 既定 reset=" + first, member);
			ProfTryDims(probe, member, "[B]");
		}
	}

	// ---- [C] 断面を指す 4 欄を 1 欄ずつ空にする（1 欄 1 個体） ------------
	for (size_t at = 0; at < kProfProfileCount; ++at)
	{
		const std::string tag = std::string("[C") + ProfWhole(static_cast<long long>(at + 1)) + "]";
		MCObjectHandle member = ProfMake(probe, tag, originY);
		originY += 2000.0;
		if (member == nil)
			continue;
		const std::string first = ProfReset(member);
		ProfSnap(probe, tag + " 既定 reset=" + first, member);
		ProfSetValue(probe, member, kProfProfileFields[at], "");
		const std::string second = ProfReset(member);
		ProfSnap(probe, tag + " " + kProfProfileFields[at] + " を空にした reset=" + second, member);
		ProfTryDims(probe, member, tag);
	}

	// ---- [D] `ProfileShape` に 0〜7 を書く（1 値 1 個体） -----------------
	// **欄型が分かる前に投げる網。** ポップアップなら整数キーで通るはずで、
	// 通らなければ「定着せず」と出る（それも答えの一部）。
	for (int shape = 0; shape <= 7; ++shape)
	{
		const std::string tag = std::string("[D") + ProfWhole(shape) + "]";
		MCObjectHandle member = ProfMake(probe, tag, originY);
		originY += 2000.0;
		if (member == nil)
			continue;
		ProfSetValue(probe, member, "ProfileShape", ProfWhole(shape));
		const std::string reset = ProfReset(member);
		ProfSnap(probe, tag + " ProfileShape=" + ProfWhole(shape) + " reset=" + reset, member);
		ProfTryDims(probe, member, tag);
	}

	// ---- [E] プロファイルグループを矩形へ差し替える ----------------------
	// **見立て (b) の本命。** 断面がグループから来ているなら、Y 120 × Z 240 の矩形へ
	// 差し替えた 1 本は 幅Y=120・高さZ=240 で描かれる。
	{
		MCObjectHandle member = ProfMake(probe, "[E]", originY);
		originY += 2000.0;
		if (member != nil)
		{
			const std::string first = ProfReset(member);
			ProfSnap(probe, "[E] 既定 reset=" + first, member);

			MCObjectHandle group = gSDK->CreateGroup(false);
			MCObjectHandle rect = gSDK->CreateRectangle(WorldRect(-60.0, 120.0, 60.0, -120.0));
			probe.log(std::string("  CreateGroup=") + (group != nil ? "ok" : "**nil**") +
					  " CreateRectangle=" + (rect != nil ? "ok" : "**nil**"));
			if (group != nil && rect != nil)
			{
				const bool added = gSDK->AddObjectToContainer(rect, group);
				const bool set = gSDK->SetCustomObjectProfileGroup(member, group) != 0;
				probe.log(std::string("  AddObjectToContainer=") + (added ? "true" : "**false**") +
						  " SetCustomObjectProfileGroup=" + (set ? "true" : "**false**"));
				const std::string reset = ProfReset(member);
				ProfSnap(probe, "[E] 群を矩形（Y120×Z240）へ差し替えた reset=" + reset, member);
				probe.log("[E] 差し替えた後のグループ:");
				ProfDumpGroups(probe, member);
				probe.log("[E] 差し替えた後の子の内訳:");
				ProfDumpChildren(probe, member);
				ProfTryDims(probe, member, "[E]");
			}
		}
	}

	// ---- [F] プロファイルグループの子を消す（空にする） ------------------
	// 「グループが断面を決めている」なら、空にすれば寸法 4 欄が使われるかもしれない
	// ——あるいは何も描かれなくなる。どちらでも答えになる。
	{
		MCObjectHandle member = ProfMake(probe, "[F]", originY);
		originY += 2000.0;
		if (member != nil)
		{
			const std::string first = ProfReset(member);
			ProfSnap(probe, "[F] 既定 reset=" + first, member);
			MCObjectHandle group = gSDK->GetCustomObjectProfileGroup(member);
			if (group == nil)
			{
				probe.log("[F] 群が nil なので空にできない（[A] の結果と突き合わせる）");
			}
			else
			{
				size_t removed = 0;
				MCObjectHandle child = gSDK->FirstMemberObj(group);
				while (child != nil)
				{
					MCObjectHandle next = gSDK->NextObject(child);
					if (gSDK->GetObjectTypeN(child) != 0)
					{
						gSDK->DeleteObject(child, false);
						++removed;
					}
					child = next;
				}
				probe.log("  群の子を " + ProfWhole(static_cast<long long>(removed)) + " 個消した");
				const std::string reset = ProfReset(member);
				ProfSnap(probe, "[F] 群を空にした reset=" + reset, member);
				ProfTryDims(probe, member, "[F]");
			}
		}
	}

	probe.log("おわり");
}
