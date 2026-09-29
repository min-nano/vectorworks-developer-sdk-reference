//
//	probes/runtime/member-profile-source/probe.cpp
//
//	[issue #169] **構造材の断面（太さ）は何が決めているのか。第 3 版——表を閉じる。**
//
//	第 1・2 版で分かったこと（PR #170 の 1・3 本目。VW 2026 / mac・新規の空図面）
//	--------------------------------------------------------------------------
//	**断面の決め方を握っているのは `MemberType`（索引 2・4 択）だった。**
//	選択肢は **0=スチール / 1=コンクリート / 2=木 / 3=カスタム**（新規の 1 本は 0）。
//
//	  ・**0（スチール）**: 断面はカタログ（`ProfileShape` ＋ `ProfileSize` の綴り）から。
//	    既定は AISC (Inch) の `W12 X 30` ＝ 165.6 × 313.4。**寸法 4 欄は無視される。**
//	    - 綴りが解ければその寸法になる: `W12 X 26`→164.8×310.4・`W14 X 30`→170.9×351.5
//	      （どちらも AISC の実寸と一致）。`角形鋼管`→609.6×762.0・`溝形鋼`→94.4×381.0。
//	    - **綴りは厳密**。`W12X30`（空白なし）と `W Shape`（英語）は解けず、
//	      **`w12 x 30`（小文字）は解けた** ＝ 大文字小文字は問わない・空白と言語は問う。
//	    - **解けないときの落ち先は `W44 X 335`**（405.1 × 1118.1。AISC の W で最大）。
//	      `ProfileSize` を空・`ProfileShape` を空・`ProfileShape` に 0〜7・両方空——
//	      **どの壊し方でも同じ値**で、`W44 X 335` を明示的に書いた [G3] と一致した。
//	    - `ProfileSeries` は空にしても `AISC (Metric)` にしても**断面が動かない**。
//	    - **`ProfileSymbol` は効かない**。`SetParamSymDef`（索引 113 が読み戻せる）でも
//	      名前でも、断面は既定のまま。Shape/Size を空にすると落ち先へ行き、
//	      **シンボルは使われなかった**。
//	  ・**1（コンクリート）/ 2（木）**: **寸法 4 欄がそのまま断面になる。**
//	    書き換える前は 300 × 600（＝`MajorBreadth`/`MajorDepth` の既定そのもの）で、
//	    200 / 800 を書いたら **200 × 800 になった**。**ここが「効く条件」である。**
//	  ・**3（カスタム）**: 60 × 60 になり、群は `(-30,-30→30,30)`。
//	    **寸法 4 欄は効かない。** 何が断面を決めているのかは**未確認**。
//	  ・プロファイル（断面）グループは**生成物**。`MemberType`=0 で矩形へ差し替えても
//	    `SetCustomObjectProfileGroup` が true を返すだけで、`ResetObject` の後には
//	    元へ戻っていた。群の子を消しても復活する。
//	  ・**手がかりは残らない**（`ResetObject`=true・読み戻し一致・`B`/`B1`/`D`/`D1` は
//	    書いた値をそのまま写す）。効いたかどうかは幾何を測るしかない。
//
//	この版で閉じること——**issue が求めた「組み合わせの表」の空欄**
//	------------------------------------------------------------
//	残っているのは 2 つで、どちらも issue の問 1・2 の範囲内（＝取りに行けば取れる）。
//
//	  M  **`MemberType`=3（カスタム）では何が断面を決めるのか。**
//	     3 の上で、シンボル（名前／参照）・プロファイルグループの差し替えを試す。
//	     カタログが使われないモードなら、**群が生成物ではなく入力になる**見込み。
//	     既定の 60×60 の群の中身も出す（何が入っているのか）。
//	  N  **1（コンクリート）/ 2（木）で寸法 4 欄はどこまで効くのか。**
//	     `MinorBreadth` / `MinorDepth` は外接に出ていない——何に効く欄なのか。
//	     形状名（`角形鋼管`）を併せて書いたらどちらが勝つのか。書く順序に依るのか。
//	     木（2）でもシンボルは無視されるのか。
//
//	判定はこれまでと同じ 2 つの数字（**幅Y** / **高さZ**）。目印:
//	165.6×313.4＝スチール既定 / 405.1×1118.1＝解決失敗 / 300×600＝寸法欄の既定 /
//	200×800＝この版で書く寸法 / 120×240＝与えるシンボルと矩形。
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

	const char* const kProfProfileFields[] = {"ProfileShape", "ProfileSeries", "ProfileSize",
											  "ProfileSymbol"};
	const size_t kProfProfileCount = sizeof(kProfProfileFields) / sizeof(kProfProfileFields[0]);

	const char* const kProfDimFields[] = {"MajorBreadth", "MinorBreadth", "MajorDepth",
										  "MinorDepth"};
	const double kProfDimValues[] = {200.0, 60.0, 800.0, 90.0};
	const size_t kProfDimCount = sizeof(kProfDimFields) / sizeof(kProfDimFields[0]);

	const char* const kProfStaticFields[] = {"B", "B1", "D", "D1"};
	const size_t kProfStaticCount = sizeof(kProfStaticFields) / sizeof(kProfStaticFields[0]);

	const char* const kProfSymbolName = "プローブ_169_断面";

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

	std::string ProfRectText(const WorldRect& rect)
	{
		return "(" + ProfCoord(double(rect.left)) + "," + ProfCoord(double(rect.bottom)) + "→" +
			   ProfCoord(double(rect.right)) + "," + ProfCoord(double(rect.top)) + ")";
	}

	// **判定はここだけ。** 目印: 165.6×313.4=スチール既定 / 405.1×1118.1=解決失敗 /
	// 300×600=寸法欄の既定 / 200×800=この版で書く寸法 / 120×240=与える矩形。
	void ProfSnap(vwprobe::Report& probe, const std::string& tag, MCObjectHandle pio)
	{
		size_t geometry = 0;
		std::vector<short> types;
		std::vector<size_t> counts;
		for (MCObjectHandle child = gSDK->FirstMemberObj(pio); child != nil;
			 child = gSDK->NextObject(child))
		{
			const short type = gSDK->GetObjectTypeN(child);
			if (type == 90 || type == 0)
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

		std::string line = tag + " 幾何" + ProfWhole(static_cast<long long>(geometry)) + " 型[";
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
			line += " **幅Y=" + ProfCoord(double(bounds.top) - double(bounds.bottom)) + "**";
		else
			line += " 幅Y=（取れず）";

		WorldCube cube;
		gSDK->GetObjectCube(pio, cube);
		line += " **高さZ=" + ProfCoord(double(cube.MaxZ()) - double(cube.MinZ())) + "**";

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
			size_t kids = 0;
			for (MCObjectHandle kid = gSDK->FirstMemberObj(group); kid != nil;
				 kid = gSDK->NextObject(kid))
				if (gSDK->GetObjectTypeN(kid) != 0)
					++kids;
			line += " 群の子" + ProfWhole(static_cast<long long>(kids));
		}
		probe.log(line);
	}

	// 群の中身を 1 行ずつ（[M] で「カスタムの群には何が入っているか」を見る）。
	void ProfDumpGroup(vwprobe::Report& probe, MCObjectHandle pio, const std::string& tag)
	{
		MCObjectHandle group = gSDK->GetCustomObjectProfileGroup(pio);
		if (group == nil)
		{
			probe.log("  " + tag + " 群=nil");
			return;
		}
		for (MCObjectHandle kid = gSDK->FirstMemberObj(group); kid != nil;
			 kid = gSDK->NextObject(kid))
		{
			const short type = gSDK->GetObjectTypeN(kid);
			if (type == 0)
				continue;
			std::string line =
				"  " + tag + " 群の子 型" + ProfWhole(type) + "(" + ProfTypeName(type) + ")";
			WorldRect bounds;
			if (gSDK->GetObjectBounds(kid, bounds))
				line += " 外接" + ProfRectText(bounds);
			probe.log(line);
		}
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

	void ProfSetReal(vwprobe::Report& probe, MCObjectHandle member, const char* name, double value)
	{
		VWParametricObj pio(member);
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.fail(std::string("欄 ") + name + " を名前で引けなかった");
			return;
		}
		pio.SetParamReal(index, value);
		probe.log(std::string("  ") + name + " ← " + ProfCoord(value) +
				  " 読み戻し=" + ProfCoord(pio.GetParamReal(index)));
	}

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
		line += " シンボル索引=" + ProfWhole(pio.GetParamSymDef(TXString("ProfileSymbol")));
		probe.log(line);
	}

	void ProfTryDims(vwprobe::Report& probe, MCObjectHandle member, const std::string& tag)
	{
		ProfSetDims(probe, member);
		const std::string reset = ProfReset(member);
		ProfSnap(probe, tag + " 寸法 4 欄を書いた reset=" + reset, member);
		ProfDumpWitness(probe, member, tag + " 寸法後");
	}

	// `MemberType` を書いて reset し、測ってから返す（どの試験もここから始まる）。
	MCObjectHandle ProfMakeWithType(vwprobe::Report& probe, const std::string& tag, double originY,
									int memberType)
	{
		MCObjectHandle member = ProfMake(probe, tag, originY);
		if (member == nil)
			return nil;
		const std::string first = ProfReset(member);
		ProfSnap(probe, tag + " 既定（MemberType=0）reset=" + first, member);
		ProfSetValue(probe, member, "MemberType", ProfWhole(memberType));
		const std::string second = ProfReset(member);
		ProfSnap(probe, tag + " MemberType=" + ProfWhole(memberType) + " reset=" + second, member);
		return member;
	}
} // namespace

VW_PROBE("member-profile-source", "構造材の断面——カスタムと寸法欄の表を閉じる（第 3 版）",
		 "MemberType=3 で何が断面を決めるかと、1/2 で寸法 4 欄がどこまで効くかを採る")
{
	gSDK->DefineCustomObject(kProfPioName, kCustomObjectPrefNever);
	probe.log("目印: 165.6x313.4=スチール既定 / 405.1x1118.1=解決失敗 / 300x600=寸法欄の既定 "
			  "/ 200x800=この版で書く寸法 / 120x240=与える矩形とシンボル。");

	double originY = 0.0;

	// 与えるシンボル定義（矩形 Y120 × Z240・下端 0）。[M1] [M2] [N5] で使う。
	TXString symName(kProfSymbolName);
	MCObjectHandle symDef = gSDK->CreateSymbolDefinition(symName);
	MCObjectHandle symRect = gSDK->CreateRectangle(WorldRect(-60.0, 240.0, 60.0, 0.0));
	InternalIndex symRef = 0;
	if (symDef != nil && symRect != nil)
	{
		gSDK->AddObjectToContainer(symRect, symDef);
		gSDK->ResetObject(symDef);
		symRef = gSDK->GetObjectInternalIndex(symDef);
	}
	probe.log("シンボル定義: 名前=[" + ProfText(symName) + "] 参照=" + ProfWhole(symRef) +
			  "（矩形 Y120×Z240）");

	// ---- [M] `MemberType`=3（カスタム）では何が断面を決めるのか ------------
	// **本命。** カタログが使われないモードなら、群は生成物ではなく入力かもしれない。

	// M0: カスタムの既定（60×60）の群に何が入っているのか。
	{
		MCObjectHandle member = ProfMakeWithType(probe, "[M0]", originY, 3);
		originY += 2000.0;
		if (member != nil)
		{
			ProfDumpGroup(probe, member, "[M0]");
			ProfDumpWitness(probe, member, "[M0] カスタム既定");
		}
	}

	// M1: カスタム ＋ シンボルを名前で。
	{
		MCObjectHandle member = ProfMakeWithType(probe, "[M1]", originY, 3);
		originY += 2000.0;
		if (member != nil)
		{
			ProfSetValue(probe, member, "ProfileSymbol", ProfText(symName));
			const std::string reset = ProfReset(member);
			ProfSnap(probe, "[M1] カスタム＋シンボル（名前）reset=" + reset, member);
			ProfDumpGroup(probe, member, "[M1]");
			ProfTryDims(probe, member, "[M1]");
		}
	}

	// M2: カスタム ＋ シンボルを参照で（`SetParamSymDef`）。
	{
		MCObjectHandle member = ProfMakeWithType(probe, "[M2]", originY, 3);
		originY += 2000.0;
		if (member != nil)
		{
			VWParametricObj pio(member);
			const size_t index = pio.GetParamIndex(TXString("ProfileSymbol"));
			pio.SetParamSymDef(index, symRef);
			probe.log("  ProfileSymbol ← SetParamSymDef(" + ProfWhole(symRef) +
					  ") 読み戻し索引=" + ProfWhole(pio.GetParamSymDef(index)) + " 値=[" +
					  ProfText(pio.GetParamValue(index)) + "]");
			const std::string reset = ProfReset(member);
			ProfSnap(probe, "[M2] カスタム＋シンボル（参照）reset=" + reset, member);
			ProfTryDims(probe, member, "[M2]");
		}
	}

	// M3: カスタム ＋ プロファイルグループを矩形へ差し替える。
	// `MemberType`=0 では reset で戻された（第 1 版 [E]）。カスタムでは残るか。
	{
		MCObjectHandle member = ProfMakeWithType(probe, "[M3]", originY, 3);
		originY += 2000.0;
		if (member != nil)
		{
			MCObjectHandle group = gSDK->CreateGroup(false);
			MCObjectHandle rect = gSDK->CreateRectangle(WorldRect(-60.0, 240.0, 60.0, 0.0));
			if (group != nil && rect != nil)
			{
				const bool added = gSDK->AddObjectToContainer(rect, group);
				const bool set = gSDK->SetCustomObjectProfileGroup(member, group) != 0;
				probe.log(std::string("  AddObjectToContainer=") + (added ? "true" : "**false**") +
						  " SetCustomObjectProfileGroup=" + (set ? "true" : "**false**"));
				const std::string reset = ProfReset(member);
				ProfSnap(probe, "[M3] カスタム＋群を矩形へ差し替え reset=" + reset, member);
				ProfDumpGroup(probe, member, "[M3]");
				ProfTryDims(probe, member, "[M3]");
			}
		}
	}

	// ---- [N] 1（コンクリート）/ 2（木）で寸法 4 欄はどこまで効くのか -------

	// N1: コンクリート ＋ 形状名（`角形鋼管`）。寸法が勝つか形状が勝つか。
	{
		MCObjectHandle member = ProfMakeWithType(probe, "[N1]", originY, 1);
		originY += 2000.0;
		if (member != nil)
		{
			ProfSetValue(probe, member, "ProfileShape", "角形鋼管");
			const std::string reset = ProfReset(member);
			ProfSnap(probe, "[N1] コンクリート＋ProfileShape=角形鋼管 reset=" + reset, member);
			ProfTryDims(probe, member, "[N1]");
		}
	}

	// N2: コンクリート ＋ `MinorBreadth` だけ変える（Major は既定 300/600 のまま）。
	// **`MinorBreadth` / `MinorDepth` は L1 の外接に出ていなかった**——何の欄なのか。
	{
		MCObjectHandle member = ProfMakeWithType(probe, "[N2]", originY, 1);
		originY += 2000.0;
		if (member != nil)
		{
			ProfSetReal(probe, member, "MinorBreadth", 250.0);
			const std::string reset = ProfReset(member);
			ProfSnap(probe, "[N2] コンクリート＋MinorBreadth=250 のみ reset=" + reset, member);
			ProfSetReal(probe, member, "MinorDepth", 500.0);
			const std::string second = ProfReset(member);
			ProfSnap(probe, "[N2] さらに MinorDepth=500 reset=" + second, member);
			ProfDumpGroup(probe, member, "[N2]");
		}
	}

	// N3: 寸法 4 欄を**先に**書いてから `MemberType`=1。順序に依るか。
	{
		MCObjectHandle member = ProfMake(probe, "[N3]", originY);
		originY += 2000.0;
		if (member != nil)
		{
			const std::string first = ProfReset(member);
			ProfSnap(probe, "[N3] 既定 reset=" + first, member);
			ProfSetDims(probe, member);
			const std::string second = ProfReset(member);
			ProfSnap(probe, "[N3] 先に寸法 4 欄（MemberType=0 のまま）reset=" + second, member);
			ProfSetValue(probe, member, "MemberType", "1");
			const std::string third = ProfReset(member);
			ProfSnap(probe, "[N3] あとから MemberType=1 reset=" + third, member);
			ProfDumpWitness(probe, member, "[N3] 順序を逆にした後");
		}
	}

	// N4: 木（2）＋ シンボル。木でもシンボルは無視されるのか。
	{
		MCObjectHandle member = ProfMakeWithType(probe, "[N4]", originY, 2);
		originY += 2000.0;
		if (member != nil)
		{
			ProfSetValue(probe, member, "ProfileSymbol", ProfText(symName));
			const std::string reset = ProfReset(member);
			ProfSnap(probe, "[N4] 木＋シンボル（名前）reset=" + reset, member);
			ProfTryDims(probe, member, "[N4]");
		}
	}

	probe.log("おわり");
}
