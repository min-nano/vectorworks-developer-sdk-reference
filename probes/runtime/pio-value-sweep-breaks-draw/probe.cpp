//
//	probes/runtime/pio-value-sweep-breaks-draw/probe.cpp
//
//	[issue #166] 同じ構造材 PIO で値を振り続けると描画が壊れることがある。**何が壊すのか**を
//	切り分ける。
//
//	発端（#158 / PR #159 の副産物）: 1 本の構造材へ `AttributesMode` を 0 → 1 → 2 → 3 と
//	書き換えて毎回 `ResetObject` を呼んだところ、描かれた子が段階的に消え、**値を 0 へ
//	戻しても戻らなかった**。一方、
//	  ・`ResetObject` を 30 回繰り返すだけなら描画は保たれた（置き石が積もるだけ）
//	  ・1 値 1 個体で測り直すと 0〜3 のどれでも壊れなかった
//	という食い違いがある。つまり「同じ個体で値を振ること」に何かがある。
//
//	切り分けの作り
//	--------------
//	**判定は機械で読める 4 つだけ**にする（絵はログに写らないので目視を頼まない）:
//	  ① 子の総数  ② 置き石（型 90 = `kUndoPlaceholderNode`）の数
//	  ③ **置き石以外の子（＝幾何）の数と型の内訳**  ④ PIO 自身の 2D 外接と 3D 立方
//	③ を「型 5 / 21 / 84 だけ」と決め打ちにしないのが前の版との違い——PR #159 の 3 版目では
//	子が型 11（`kGroupNode`）になっていて、決め打ちの網から漏れていた。**型は数えて並べる。**
//	④ は「子の数え方そのものが当てにならない」場合の保険で、幾何が無ければ外接は潰れる。
//
//	置く順番に意味がある。**再現（A）を最初に置く**——以前「以降の測定すべてで何も
//	描かれなかった」ので、壊れが文書全体に及ぶ疑いがあり、それを他の測定より前に
//	採らないと何も信じられなくなる。直後に H（まったく新しい個体）を置くのがその判定で、
//	最後に Z（もう一度まったく新しい個体）を置いて「この実行全体で文書が汚れたか」も見る。
//	**直す手立て（I）はいちばん危ない**（子を消す・undo 表を消す）ので末尾に置く。
//
//	  A  1 個体で `AttributesMode` を 0→1→2→3→0 と振る（再現）
//	  H  A の直後に**まったく新しい個体**を作る → 壊れは個体か文書か
//	  B  1 値 1 個体（0/1/2/3 を別々の個体で）→ 対照
//	  C  **同じ値**（0）を 4 回書いて毎回 reset → 「値の変化」が要るのか
//	  D  **別の欄**（`MemberID`＝文字欄。幾何に関係しない）を 4 回振る → 欄に依るのか
//	  D2 **幾何を変える欄**（`MajorDepth`）を 4 回振る → 書き換え一般で起きるのか
//	  E  何も書かずに `ResetObject` を 4 回 → 対照（置き石だけ）
//	  F  置き石を 10 個積んでから**1 回だけ**書く → 「置き石 × 書き換え」の組み合わせか
//	  G  **undo イベントを開いた中で** A と同じ振り方 → 置き石が残らない経路でも起きるか
//	  Z  最後にまったく新しい個体 → この実行で文書が汚れたか
//	  I  A の壊れた個体を直せるか（reset 追加 / 置き石を消す / undo 表を消す /
//	     `DeleteRegenerableSubObjects`）
//
//	G について（プローブの決まりの例外）
//	------------------------------------
//	`probes/runtime/README.md` は「プローブは undo イベントを自分では開かない」と決めて
//	いる。G はその**唯一の例外**で、issue #166 が確かめると決めた項目そのものだから置く。
//	**取り消しは一度も実行しない**（`EndUndoEvent` で閉じるだけ）ので、Findings「Undo」の
//	「半端な記録を取り消すと図面が壊れる」は踏まない。走らせるのは新規の空図面で、
//	終わったら保存せずに捨ててよい。
//

#include "Probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const char* const kSweepPioName = "StructuralMember";

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

	// ログを読む人が型番号を引き直さなくて済むように、出てきそうなものだけ名前を添える。
	const char* SweepTypeName(short type)
	{
		switch (type)
		{
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

	struct SweepShot
	{
		size_t total = 0;		 // 子の総数
		size_t placeholders = 0; // 型 90（置き石）
		size_t geometry = 0;	 // 置き石以外（＝描かれうる子）
		double width = 0.0;		 // PIO 自身の 2D 外接の幅
		double height = 0.0;
	};

	// **判定はここだけ。** 子の内訳と PIO 自身の外接・立方を 1 行で出す。
	SweepShot SweepSnap(vwprobe::Report& probe, const std::string& tag, MCObjectHandle pio)
	{
		SweepShot shot;
		std::vector<short> types;
		std::vector<size_t> counts;
		for (MCObjectHandle child = gSDK->FirstMemberObj(pio); child != nil;
			 child = gSDK->NextObject(child))
		{
			const short type = gSDK->GetObjectTypeN(child);
			++shot.total;
			if (type == 90)
			{
				++shot.placeholders;
				continue;
			}
			++shot.geometry;
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

		std::string line = tag + " 子=全" + SweepWhole(static_cast<long long>(shot.total)) +
						   " 置石" + SweepWhole(static_cast<long long>(shot.placeholders)) +
						   " 幾何" + SweepWhole(static_cast<long long>(shot.geometry)) + " 型[";
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
			shot.width = double(bounds.right) - double(bounds.left);
			shot.height = double(bounds.top) - double(bounds.bottom);
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
		line += " 立方=(" + SweepCoord(double(cube.MinX())) + "→" +
				SweepCoord(double(cube.MaxX())) + " / " + SweepCoord(double(cube.MinY())) + "→" +
				SweepCoord(double(cube.MaxY())) + " / " + SweepCoord(double(cube.MinZ())) + "→" +
				SweepCoord(double(cube.MaxZ())) + ")";

		// **この 1 語で読む。** 幾何の子が 1 つも無ければ「描かれていない」。
		line += std::string(" 描画=") + (shot.geometry > 0 ? "あり" : "**なし**");
		probe.log(line);
		return shot;
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

	size_t SweepIndex(vwprobe::Report& probe, VWParametricObj& pio, const char* name)
	{
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
		{
			probe.fail(std::string("欄 ") + name + " を名前で引けなかった");
			return size_t(-1);
		}
		return index;
	}

	// 欄へ書いて読み戻しをログへ出す。**読み戻しが一致しないなら、壊れているのは描画
	// ではなく書き込みのほう**——そこを混ぜないために毎回出す。
	void SweepWrite(vwprobe::Report& probe, MCObjectHandle member, const char* name,
					const std::string& value)
	{
		VWParametricObj pio(member);
		const size_t index = SweepIndex(probe, pio, name);
		if (index == size_t(-1))
			return;
		pio.SetParamValue(index, TXString(value.c_str()));
		const std::string back = SweepText(pio.GetParamValue(index));
		probe.log(std::string("  ") + name + " ← [" + value + "] 読み戻し=[" + back + "]" +
				  (back == value ? "" : "  ← **一致しない**"));
	}

	std::string SweepReadBack(MCObjectHandle member, const char* name)
	{
		VWParametricObj pio(member);
		const size_t index = pio.GetParamIndex(TXString(name));
		if (index == size_t(-1) || index >= pio.GetParamsCount())
			return std::string("—");
		return SweepText(pio.GetParamValue(index));
	}

	std::string SweepReset(MCObjectHandle member)
	{
		return gSDK->ResetObject(member) ? "true" : "**false**";
	}

	std::string SweepInEvent()
	{
		return gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no";
	}

	// A・G で使う振り方（0→1→2→3→0）。最後の 0 で戻るかどうかがいちばんの見どころ。
	const int kSweepModes[] = {0, 1, 2, 3, 0};

	size_t SweepAttributesMode(vwprobe::Report& probe, const std::string& tag,
							   MCObjectHandle member)
	{
		size_t lastGeometry = 0;
		for (size_t at = 0; at < sizeof(kSweepModes) / sizeof(kSweepModes[0]); ++at)
		{
			const std::string value = SweepWhole(kSweepModes[at]);
			SweepWrite(probe, member, "AttributesMode", value);
			const std::string reset = SweepReset(member);
			lastGeometry = SweepSnap(probe,
									 tag + " " + SweepWhole(static_cast<long long>(at + 1)) +
										 "回目 AttributesMode=" + value + " reset=" + reset,
									 member)
							   .geometry;
		}
		return lastGeometry;
	}
} // namespace

VW_PROBE("pio-value-sweep-breaks-draw", "同じ PIO で値を振ると描画が壊れる原因を切り分ける",
		 "AttributesMode を振る / 同じ値を書く / 別の欄を振る / reset だけ / undo イベントの中"
		 "で振る、を比べ、壊れた個体を直せるかまで見る")
{
	gSDK->DefineCustomObject(kSweepPioName, kCustomObjectPrefNever);
	probe.log("開始時 undo イベント中=" + SweepInEvent() +
			  "（プローブが開く前から開いているなら、置き石の前提が崩れる）");

	// ---- [A] 再現: 1 個体で AttributesMode を 0→1→2→3→0 と振る ----------
	MCObjectHandle broken = SweepMake(probe, "[A]", 0.0);
	if (broken != nil)
	{
		probe.log("[A] 1 個体で AttributesMode を 0→1→2→3→0 と振る（毎回 ResetObject）");
		SweepSnap(probe, "[A] 作った直後（reset 前）", broken);
		const std::string reset = SweepReset(broken);
		SweepSnap(probe, "[A] 最初の ResetObject だけ reset=" + reset, broken);
		SweepAttributesMode(probe, "[A]", broken);
	}

	// ---- [H] A の直後に、まったく新しい個体 ------------------------------
	// **壊れが個体の中の話か、文書（PIO の定義）に及ぶ話か。** 以前「以降の測定すべてで
	// 何も描かれなかった」ので、ここで分かれる。
	{
		MCObjectHandle fresh = SweepMake(probe, "[H]", 2000.0);
		if (fresh != nil)
		{
			const std::string reset = SweepReset(fresh);
			SweepSnap(probe, "[H] A の直後に作った新しい個体（値は既定）reset=" + reset, fresh);
		}
	}

	// ---- [B] 1 値 1 個体（対照） ----------------------------------------
	for (int mode = 0; mode <= 3; ++mode)
	{
		MCObjectHandle member = SweepMake(probe, "[B]", 4000.0 + 1000.0 * double(mode));
		if (member == nil)
			continue;
		SweepWrite(probe, member, "AttributesMode", SweepWhole(mode));
		const std::string reset = SweepReset(member);
		SweepSnap(probe, "[B] 1 値 1 個体 AttributesMode=" + SweepWhole(mode) + " reset=" + reset,
				  member);
	}

	// ---- [C] 同じ値（0）を 4 回 ----------------------------------------
	// 「値が変わること」が要るのか、「書いて reset する」だけで足りるのか。
	{
		MCObjectHandle member = SweepMake(probe, "[C]", 9000.0);
		if (member != nil)
		{
			for (int round = 1; round <= 4; ++round)
			{
				SweepWrite(probe, member, "AttributesMode", "0");
				const std::string reset = SweepReset(member);
				SweepSnap(probe, "[C] 同じ値(0) " + SweepWhole(round) + "回目 reset=" + reset,
						  member);
			}
		}
	}

	// ---- [D] 別の欄（MemberID＝文字欄。幾何に関係しない）を 4 回 --------
	{
		MCObjectHandle member = SweepMake(probe, "[D]", 10000.0);
		if (member != nil)
		{
			for (int round = 1; round <= 4; ++round)
			{
				SweepWrite(probe, member, "MemberID", "ID-" + SweepWhole(round));
				const std::string reset = SweepReset(member);
				SweepSnap(probe, "[D] MemberID " + SweepWhole(round) + "回目 reset=" + reset,
						  member);
			}
		}
	}

	// ---- [D2] 幾何を変える欄（MajorDepth）を 4 回 -----------------------
	{
		MCObjectHandle member = SweepMake(probe, "[D2]", 11500.0);
		if (member != nil)
		{
			const double kDepths[] = {600.0, 800.0, 1000.0, 600.0};
			for (size_t at = 0; at < sizeof(kDepths) / sizeof(kDepths[0]); ++at)
			{
				{
					VWParametricObj pio(member);
					const size_t index = SweepIndex(probe, pio, "MajorDepth");
					if (index != size_t(-1))
					{
						pio.SetParamReal(index, kDepths[at]);
						probe.log("  MajorDepth ← [" + SweepCoord(kDepths[at]) + "] 読み戻し=[" +
								  SweepCoord(pio.GetParamReal(index)) + "]");
					}
				}
				const std::string reset = SweepReset(member);
				SweepSnap(probe, "[D2] MajorDepth=" + SweepCoord(kDepths[at]) + " reset=" + reset,
						  member);
			}
		}
	}

	// ---- [E] 何も書かずに ResetObject を 4 回（対照） -------------------
	{
		MCObjectHandle member = SweepMake(probe, "[E]", 13000.0);
		if (member != nil)
		{
			for (int round = 1; round <= 4; ++round)
			{
				const std::string reset = SweepReset(member);
				SweepSnap(probe, "[E] reset だけ " + SweepWhole(round) + "回目 reset=" + reset,
						  member);
			}
		}
	}

	// ---- [F] 置き石を 10 個積んでから 1 回だけ書く -----------------------
	// 「置き石 × 値の書き換え」の組み合わせが犯人なら、ここで壊れる。
	{
		MCObjectHandle member = SweepMake(probe, "[F]", 14000.0);
		if (member != nil)
		{
			for (int round = 0; round < 10; ++round)
				gSDK->ResetObject(member);
			SweepSnap(probe, "[F] reset を 10 回（まだ何も書いていない）", member);
			SweepWrite(probe, member, "AttributesMode", "3");
			const std::string reset = SweepReset(member);
			SweepSnap(probe, "[F] 置き石 10 個の上で AttributesMode=3 を 1 回 reset=" + reset,
					  member);
			SweepWrite(probe, member, "AttributesMode", "0");
			const std::string back = SweepReset(member);
			SweepSnap(probe, "[F] さらに 0 へ戻す reset=" + back, member);
		}
	}

	// ---- [G] undo イベントを開いた中で A と同じ振り方 --------------------
	// **取り消しは実行しない。**（決まりの例外。冒頭のコメントを読むこと）
	{
		MCObjectHandle member = SweepMake(probe, "[G]", 15500.0);
		if (member != nil)
		{
			const std::string first = SweepReset(member);
			SweepSnap(probe, "[G] イベントを開く前 reset=" + first, member);
			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
			{
				probe.log("[G] すでに undo イベント中だった——入れ子にせず、開かずに振る");
				SweepAttributesMode(probe, "[G/イベント中(他人の)]", member);
			}
			else
			{
				gSDK->SetUndoMethod(kUndoSwapObjects);
				gSDK->NameUndoEvent(TXString("プローブ #166: 値を振る"));
				probe.log("[G] イベントを開いた → イベント中=" + SweepInEvent());
				const bool added = gSDK->AddBothSwapObject(member) ? true : false;
				probe.log(std::string("[G] AddBothSwapObject=") + (added ? "true" : "false"));
				SweepAttributesMode(probe, "[G/イベント中]", member);
				const bool ended = gSDK->EndUndoEvent() ? true : false;
				probe.log(std::string("[G] EndUndoEvent=") + (ended ? "true" : "false") +
						  " イベント中=" + SweepInEvent());
				SweepSnap(probe, "[G] イベントを閉じた後（何も書かず・reset せず）", member);
			}
		}
	}

	// ---- [Z] この実行全体で文書が汚れたか -------------------------------
	{
		MCObjectHandle fresh = SweepMake(probe, "[Z]", 17500.0);
		if (fresh != nil)
		{
			const std::string reset = SweepReset(fresh);
			SweepSnap(probe, "[Z] 全部やった後に作った新しい個体（値は既定）reset=" + reset, fresh);
		}
	}

	// ---- [I] A の壊れた個体を直せるか（危ないので最後） ------------------
	if (broken != nil)
	{
		probe.log("[I] A の個体（AttributesMode は 0 に戻してある）を直す手立てを順に試す");
		SweepSnap(probe,
				  "[I] 直す前 AttributesMode=[" + SweepReadBack(broken, "AttributesMode") + "]",
				  broken);

		// I-1: ResetObject をもう 2 回（「もう一度呼べば戻る」か）
		for (int round = 1; round <= 2; ++round)
		{
			const std::string reset = SweepReset(broken);
			SweepSnap(probe, "[I-1] ResetObject 追加 " + SweepWhole(round) + "回目 reset=" + reset,
					  broken);
		}

		// I-2: 置き石（型 90）の子を全部消してから ResetObject
		{
			std::vector<MCObjectHandle> victims;
			for (MCObjectHandle child = gSDK->FirstMemberObj(broken); child != nil;
				 child = gSDK->NextObject(child))
			{
				if (gSDK->GetObjectTypeN(child) == 90)
					victims.push_back(child);
			}
			probe.log("[I-2] 置き石を " + SweepWhole(static_cast<long long>(victims.size())) +
					  " 個消す（useUndo=false）——ここで落ちる可能性がある");
			for (size_t at = 0; at < victims.size(); ++at)
				gSDK->DeleteObject(victims[at], false);
			SweepSnap(probe, "[I-2] 置き石を消した直後（reset 前）", broken);
			const std::string reset = SweepReset(broken);
			SweepSnap(probe, "[I-2] 置き石を消して ResetObject reset=" + reset, broken);
		}

		// I-3: undo 表そのものを捨ててから ResetObject
		{
			probe.log("[I-3] ClearUndoTableDueToUnsupportedAction を呼ぶ（undo 表を捨てる）");
			gSDK->ClearUndoTableDueToUnsupportedAction();
			const std::string reset = SweepReset(broken);
			SweepSnap(probe, "[I-3] undo 表を捨てて ResetObject reset=" + reset, broken);
		}

		// I-4: DeleteRegenerableSubObjects（ヘッダは「Regenerate イベント中に使え」と
		// 言っているので、外から呼んで効くかは分からない）
		{
			probe.log("[I-4] DeleteRegenerableSubObjects を外から呼ぶ"
					  "（ヘッダは Regenerate イベント中に使えと言っている）");
			gSDK->DeleteRegenerableSubObjects(broken);
			SweepSnap(probe, "[I-4] 呼んだ直後（reset 前）", broken);
			const std::string reset = SweepReset(broken);
			SweepSnap(probe, "[I-4] さらに ResetObject reset=" + reset, broken);
		}
	}

	probe.log("読み方: 各行の末尾の「描画=あり / なし」だけで壊れたかを判定する"
			  "（幾何の子が 1 つも無い＝なし）。型の内訳と外接・立方はその裏付け。");
	probe.log("おわり");
}
