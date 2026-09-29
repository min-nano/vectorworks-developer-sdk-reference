//
//	probes/runtime/pio-value-sweep-breaks-draw/probe.cpp
//
//	[issue #166] 同じ構造材 PIO で値を振り続けると描画が壊れることがある。**何が壊すのか**を
//	切り分ける。**第 2 版**。
//
//	第 1 版で分かったこと（PR #168 の実行ログ 1 本目。VW 2026 / mac・新規の空図面）
//	--------------------------------------------------------------------------
//	**壊れなかった。1 行も。** 11 通りすべてで、置き石以外の子は **10 個**・型の内訳は
//	`11(Group)x2, 84(CSGTree)x1, 21(Polyline)x4, 5(Polygon)x2, 0x1` のまま・外接も立方も
//	1 度も動かなかった:
//	  ・`AttributesMode` を 1 個体で 0→1→2→3→0（＝issue が再現手順としたもの）
//	  ・1 値 1 個体 / 同じ値を 4 回 / `MemberID` を 4 回 / `MajorDepth` を 4 回 /
//	    `ResetObject` だけ 4 回 / 置き石 10 個の上で 1 回書く / undo イベント中で振る
//	  ・振った直後に作った新しい個体も、全部やった後の新しい個体も既定どおり
//	ついでに分かったこと:
//	  ・置き石（型 90）は `ResetObject` 1 回ごとに 1 つ積もる。**undo イベントが開いて
//	    いても積もる**（[G] で 0→5）。そして**積もっても描画には何も起きない**。
//	  ・置き石は**消せない**。型 90 の子へ `DeleteObject(h, useUndo=false)` を 7 個ぶん
//	    呼んでも 1 つも減らず、`ClearUndoTableDueToUnsupportedAction()` でも減らない。
//	  ・`DeleteRegenerableSubObjects` は**外から呼んでも効く**（幾何 10 → 3）。次の
//	    `ResetObject` で 10 へ戻る。置き石は減らない。
//	  ・**走り出しは undo イベント中=no なのに、途中から yes になっていた**——
//	    プローブは開いていないので、**SDK 内部（PIO の生成か再生成）が自前で開いている**。
//
//	つまり issue の「`AttributesMode` を振ると壊れる」は**この条件では起きない**。
//	すると #159 で「消えた」ものは別の原因のはずで、そこが残った宿題である。
//
//	この版で確かめるもの
//	--------------------
//	疑っているのは**表示欄**（`MemberDisplay` / `CoverDisplay` / `CenterlineDisplay` /
//	`StartCapDisplay` / `EndCapDisplay` の `_Above` / `_At` / `_Below`）。#159 のプローブは
//	`AttributesMode` を振る前の群でこれらを false にしており、しかも数える型を
//	5 / 21 / 84 に決め打ちしていた（型 11 が網から漏れていた）。**「`AttributesMode` の
//	せいで消えた」は取り違えではないか**を、欄と子の対応を採って決める。
//
//	  P  既定の 1 本（基準）
//	  Q  **表示欄を 1 つずつ false にする** → どの欄がどの子を消すか
//	  R  **逆順に true へ戻す** → 戻るか（issue の「戻らなかった」に当たる）
//	  S  #159 と同じ負荷を 1 個体へ重ねてから `AttributesMode` を振る
//	     （per-part のクラス・色・`MemberFillStyle`/`MemberPenStyle` を順に書いて
//	      reset を重ね、最後に 0→1→2→3→0）
//	  T  3D 側（`AttributesMode3D` / `MemberAttributes_3D`）を 1 個体で振る
//
//	数え方は第 1 版から 1 つだけ直した——**型 0 を別に数える**（`kTermNode`。毎回ちょうど
//	1 つ在り、`DeleteRegenerableSubObjects` でも消えないので、描かれた子ではない見込み）。
//	だから「幾何」は第 1 版の 10 ではなく **9** から始まる。
//
//	undo イベントについて（プローブの決まりの例外）
//	----------------------------------------------
//	第 1 版で「プローブが開かなくても SDK 内部が開く」と分かったので、この版は
//	**自分では開かない**（`probes/runtime/README.md` の決まりどおり）。取り消しも
//	実行しない。走らせるのは新規の空図面で、終わったら保存せずに捨ててよい。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kSweepPioName = "StructuralMember";
	const char* const kSweepProbeClass = "プローブ_166_試験";

	const char* const kSweepFaces[] = {"_Above", "_At", "_Below"};

	std::string SweepText(const TXString& src)
	{
		const char* utf8 = static_cast<const char*>(src);
		return utf8 ? std::string(utf8) : std::string();
	}

	std::string SweepWhole(long long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%lld", value);
		return std::string(buffer);
	}

	std::string SweepCoord(double value)
	{
		char buffer[48];
		std::snprintf(buffer, sizeof(buffer), "%.1f", value);
		return std::string(buffer);
	}

	const char* SweepTypeName(short type)
	{
		switch (type)
		{
		case 0:
			return "Term";
		case 2:
			return "Line";
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
		case 111:
			return "PieceWiseNurbsCurve";
		case 113:
			return "NurbsSurface";
		case 114:
			return "CompositeSurface";
		default:
			return "?";
		}
	}

	// **判定はここだけ。** 置き石（型 90）と終端（型 0）を除いた子の数と型の内訳、
	// そして PIO 自身の 2D 外接・3D 立方を 1 行で出す。
	size_t SweepSnap(vwprobe::Report& probe, const std::string& tag, MCObjectHandle pio)
	{
		size_t total = 0;
		size_t placeholders = 0;
		size_t terms = 0;
		size_t geometry = 0;
		std::vector<short> types;
		std::vector<size_t> counts;
		for (MCObjectHandle child = gSDK->FirstMemberObj(pio); child != nil;
			 child = gSDK->NextObject(child))
		{
			const short type = gSDK->GetObjectTypeN(child);
			++total;
			if (type == 90)
			{
				++placeholders;
				continue;
			}
			if (type == 0)
			{
				++terms;
				continue;
			}
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

		std::string line = tag + " 子=全" + SweepWhole(static_cast<long long>(total)) + " 置石" +
						   SweepWhole(static_cast<long long>(placeholders)) + " 終端" +
						   SweepWhole(static_cast<long long>(terms)) + " 幾何" +
						   SweepWhole(static_cast<long long>(geometry)) + " 型[";
		for (size_t at = 0; at < types.size(); ++at)
		{
			if (at != 0)
				line += ",";
			line += SweepWhole(types[at]);
			line += std::string("(") + SweepTypeName(types[at]) + ")x";
			line += SweepWhole(static_cast<long long>(counts[at]));
		}
		line += "]";

		WorldRect bounds;
		if (gSDK->GetObjectBounds(pio, bounds))
		{
			line += " 外接=(" + SweepCoord(double(bounds.left)) + "," +
					SweepCoord(double(bounds.bottom)) + "→" + SweepCoord(double(bounds.right)) +
					"," + SweepCoord(double(bounds.top)) + ")";
		}
		else
		{
			line += " 外接=（取れず）";
		}

		WorldCube cube;
		gSDK->GetObjectCube(pio, cube);
		line += " 立方Z=(" + SweepCoord(double(cube.MinZ())) + "→" +
				SweepCoord(double(cube.MaxZ())) + ")";
		line += std::string(" 描画=") + (geometry > 0 ? "あり" : "**なし**");
		probe.log(line);
		return geometry;
	}

	MCObjectHandle SweepMake(vwprobe::Report& probe, const std::string& tag, double originY)
	{
		VWPolygon2DObj path({VWPoint2D(0.0, originY), VWPoint2D(3000.0, originY)});
		MCObjectHandle pathHandle = path.GetThisObject();
		if (pathHandle == nil)
		{
			probe.fail(tag + " パス（VWPolygon2DObj）を作れなかった");
			return nil;
		}
		MCObjectHandle member = gSDK->CreateCustomObjectPath(kSweepPioName, pathHandle, nil, false);
		if (member == nil)
			probe.fail(tag + " CreateCustomObjectPath(StructuralMember) が nil を返した");
		return member;
	}

	std::string SweepReset(MCObjectHandle member)
	{
		return gSDK->ResetObject(member) ? "true" : "**false**";
	}

	void SweepWriteValue(vwprobe::Report& probe, MCObjectHandle member, const char* name,
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
		const std::string back = SweepText(pio.GetParamValue(index));
		probe.log(std::string("  ") + name + " ← [" + value + "] 読み戻し=[" + back + "]" +
				  (back == value ? "" : "  ← **一致しない**"));
	}

	// 3 面（_Above / _At / _Below）すべてへ真偽を書き、読み戻しを 1 行で出す。
	void SweepWriteBoolAllFaces(vwprobe::Report& probe, MCObjectHandle member, const char* base,
								bool value)
	{
		VWParametricObj pio(member);
		std::string line =
			std::string("  ") + base + "_* ← " + (value ? "true" : "false") + " 読み戻し=";
		for (size_t face = 0; face < 3; ++face)
		{
			const std::string name = std::string(base) + kSweepFaces[face];
			const size_t index = pio.GetParamIndex(TXString(name.c_str()));
			if (index == size_t(-1) || index >= pio.GetParamsCount())
			{
				line += std::string(kSweepFaces[face]) + "=（引けず） ";
				continue;
			}
			pio.SetParamBool(index, value);
			line +=
				std::string(kSweepFaces[face]) + "=" + (pio.GetParamBool(index) ? "t" : "f") + " ";
		}
		probe.log(line);
	}

	void SweepWriteValueAllFaces(vwprobe::Report& probe, MCObjectHandle member, const char* base,
								 const std::string& value)
	{
		VWParametricObj pio(member);
		std::string line = std::string("  ") + base + "_* ← [" + value + "] 読み戻し=";
		for (size_t face = 0; face < 3; ++face)
		{
			const std::string name = std::string(base) + kSweepFaces[face];
			const size_t index = pio.GetParamIndex(TXString(name.c_str()));
			if (index == size_t(-1) || index >= pio.GetParamsCount())
			{
				line += std::string(kSweepFaces[face]) + "=（引けず） ";
				continue;
			}
			pio.SetParamValue(index, TXString(value.c_str()));
			line +=
				std::string(kSweepFaces[face]) + "=[" + SweepText(pio.GetParamValue(index)) + "] ";
		}
		probe.log(line);
	}

	// 表示欄は「消す順」に並べてある。R ではこれを逆から true へ戻す。
	const char* const kSweepDisplayFields[] = {"MemberDisplay", "CoverDisplay", "CenterlineDisplay",
											   "StartCapDisplay", "EndCapDisplay"};
	const size_t kSweepDisplayCount = sizeof(kSweepDisplayFields) / sizeof(kSweepDisplayFields[0]);

	const int kSweepModes[] = {0, 1, 2, 3, 0};
} // namespace

VW_PROBE("pio-value-sweep-breaks-draw", "何が構造材の子を消すのか（表示欄か属性か）",
		 "表示欄を 1 つずつ false にして子の消え方を採り、true へ戻して戻るかを見る。"
		 "#159 と同じ負荷を重ねてから AttributesMode を振る試験も置く")
{
	gSDK->DefineCustomObject(kSweepPioName, kCustomObjectPrefNever);
	probe.log("開始時 undo イベント中=" +
			  std::string(gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no") +
			  "（第 1 版では no で始まり、途中から yes になった＝SDK 内部が開いている）");

	// ---- [P] 既定の 1 本（基準） ----------------------------------------
	{
		MCObjectHandle member = SweepMake(probe, "[P]", 0.0);
		if (member != nil)
		{
			const std::string reset = SweepReset(member);
			SweepSnap(probe, "[P] 既定のまま reset=" + reset + "（以降はこれとの差で読む）",
					  member);
		}
	}

	// ---- [Q] 表示欄を 1 つずつ false にする -----------------------------
	// **本命。** どの欄がどの子を消すのか。ここで型 84 / 21 / 5 が落ちるなら、
	// #159 の「AttributesMode で消えた」は取り違えだったと決まる。
	{
		MCObjectHandle member = SweepMake(probe, "[Q]", 2000.0);
		if (member != nil)
		{
			const std::string first = SweepReset(member);
			SweepSnap(probe, "[Q] 既定 reset=" + first, member);
			for (size_t at = 0; at < kSweepDisplayCount; ++at)
			{
				SweepWriteBoolAllFaces(probe, member, kSweepDisplayFields[at], false);
				const std::string reset = SweepReset(member);
				SweepSnap(probe,
						  std::string("[Q] ") + kSweepDisplayFields[at] +
							  "_* = false まで falseにした reset=" + reset,
						  member);
			}

			// ---- [R] 逆順に true へ戻す（戻るか） ----------------------
			for (size_t at = kSweepDisplayCount; at > 0; --at)
			{
				SweepWriteBoolAllFaces(probe, member, kSweepDisplayFields[at - 1], true);
				const std::string reset = SweepReset(member);
				SweepSnap(probe,
						  std::string("[R] ") + kSweepDisplayFields[at - 1] +
							  "_* = true まで戻した reset=" + reset,
						  member);
			}
		}
	}

	// ---- [S] #159 と同じ負荷を重ねてから AttributesMode を振る -----------
	// per-part のクラス・色・スタイルを 1 個体へ順に書いて reset を重ね、最後に
	// `AttributesMode` を 0→1→2→3→0。第 1 版の [A] は**素の個体**で振ったので、
	// 「先に他の欄を書いてあること」が要るのかをここで見る。
	{
		const InternalIndex probeClass = gSDK->AddClass(TXString(kSweepProbeClass));
		MCObjectHandle member = SweepMake(probe, "[S]", 4500.0);
		if (member != nil)
		{
			if (probeClass != 0)
				gSDK->SetObjectClass(member, probeClass);
			const std::string first = SweepReset(member);
			SweepSnap(probe, "[S] 既定 reset=" + first, member);

			SweepWriteValueAllFaces(probe, member, "MemberClass", kSweepProbeClass);
			const std::string r1 = SweepReset(member);
			SweepSnap(probe, "[S] MemberClass_* を書いた reset=" + r1, member);

			SweepWriteValueAllFaces(probe, member, "MemberFillStyle", "6"); // クラス属性
			const std::string r2 = SweepReset(member);
			SweepSnap(probe, "[S] MemberFillStyle_*=6 reset=" + r2, member);

			SweepWriteValueAllFaces(probe, member, "MemberPenStyle", "4"); // クラス属性
			const std::string r3 = SweepReset(member);
			SweepSnap(probe, "[S] MemberPenStyle_*=4 reset=" + r3, member);

			for (size_t at = 0; at < sizeof(kSweepModes) / sizeof(kSweepModes[0]); ++at)
			{
				const std::string value = SweepWhole(kSweepModes[at]);
				SweepWriteValue(probe, member, "AttributesMode", value);
				const std::string reset = SweepReset(member);
				SweepSnap(probe,
						  "[S] 負荷の上で AttributesMode=" + value + "（" +
							  SweepWhole(static_cast<long long>(at + 1)) + "回目）reset=" + reset,
						  member);
			}
		}
	}

	// ---- [T] 3D 側を振る -------------------------------------------------
	// 型 84（CSGTree）は 3D 実体なので、消すとしたら 3D 側の欄の見込み。
	{
		MCObjectHandle member = SweepMake(probe, "[T]", 7000.0);
		if (member != nil)
		{
			const std::string first = SweepReset(member);
			SweepSnap(probe, "[T] 既定 reset=" + first, member);
			for (size_t at = 0; at < sizeof(kSweepModes) / sizeof(kSweepModes[0]); ++at)
			{
				const std::string value = SweepWhole(kSweepModes[at]);
				SweepWriteValue(probe, member, "AttributesMode3D", value);
				const std::string reset = SweepReset(member);
				SweepSnap(probe, "[T] AttributesMode3D=" + value + " reset=" + reset, member);
			}
			for (int mode = 0; mode <= 1; ++mode)
			{
				SweepWriteValue(probe, member, "MemberAttributes_3D", SweepWhole(mode));
				const std::string reset = SweepReset(member);
				SweepSnap(probe, "[T] MemberAttributes_3D=" + SweepWhole(mode) + " reset=" + reset,
						  member);
			}
		}
	}

	probe.log("読み方: 行末の「描画=あり / なし」と「幾何」の数・型の内訳を [P] と比べる。"
			  "幾何が減った行の直前に書いた欄が、その子を消した欄である。");
	probe.log("おわり");
}
