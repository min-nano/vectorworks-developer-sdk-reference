//
//	probes/runtime/member-profile-source/probe.cpp
//
//	[issue #169] **構造材の断面（太さ）は何が決めているのか。第 2 版。**
//
//	第 1 版で分かったこと（PR #170 の 1 本目。VW 2026 / mac・新規の空図面・16 個体）
//	------------------------------------------------------------------------------
//	**断面を決めているのは `ProfileShape` と `ProfileSize` の 2 欄**だった。
//	  ・4 欄の素性: `ProfileShape`=[H形鋼] / `ProfileSeries`=[AISC (Inch)] /
//	    `ProfileSize`=[W12 X 30] は**どれも欄型 14（`kFieldStaticText`）**で、
//	    `ProfileSymbol` は欄型 30（`kFieldSymDef`）だが**既定は空**。
//	    ＝**既定の断面はシンボルではなくカタログ（AISC の W12 X 30）から来ている。**
//	  ・**プロファイル（断面）グループは生成物**。既定の群は Polyline 1 つで外接
//	    (-82.8,0→82.8,313.4)、つまり**描かれた断面 165.6 × 313.4 とぴったり一致**する。
//	    [E] で矩形へ差し替えると `SetCustomObjectProfileGroup` は true を返すのに、
//	    `ResetObject` の後に読み直すと**元の Polyline に戻っていた**。[F] で群の子を
//	    消しても同じ。**`ResetObject` が 2 欄から毎回作り直すので、外から書いても消える。**
//	  ・`ProfileShape` か `ProfileSize` を**空にすると断面が変わった**
//	    （165.6×313.4 → **405.1×1118.1**）。`ProfileSeries` を空にしても、
//	    `ProfileSymbol`（既に空）を空にしても**何も変わらない**。
//	  ・`ProfileShape` へ 0〜7 を書くと**値は定着するのに、どの値でも 405.1×1118.1**。
//	    ＝**解決できない綴りを書くと、この 1 つの断面へ落ちる**と読める。
//	  ・**寸法 4 欄（`MajorBreadth` / `MinorBreadth` / `MajorDepth` / `MinorDepth`）は
//	    6 条件すべてで 1mm も絵を動かさなかった。**
//	  ・**手がかりは残らない。** `ResetObject`=true・読み戻し一致・そして
//	    `B`/`B1`/`D`/`D1` は**書いた値をそのまま写した**（200/60/800/90）。
//	    あの 4 欄は寸法欄の鏡で、描かれた実寸ではない。
//
//	この版で確かめること——**「決めている」の裏を取る**
//	--------------------------------------------------
//	第 1 版は「空にすると変わる」しか示していない。**意図した断面にできるのか**が
//	残った宿題で、これは issue の問 1 の範囲内（＝取りに行けば取れる答え）である。
//
//	  G  `ProfileSize` を**同じ系列の別項目**へ書く（`W12 X 26` / `W14 X 30` /
//	     `W44 X 335` / 空白なし `W12X30` / 小文字 `w12 x 30`）
//	     → 断面がその項目の寸法になるか・綴りはどこまで厳しいか。
//	     **`W44 X 335` は 1117.6 × 403.9mm（AISC で最大の W）で、第 1 版の
//	     フォールバック 405.1 × 1118.1 とほぼ一致する**——これが「解決失敗の落ち先は
//	     表の最後（最大）」なのかを見る。
//	  H  `ProfileShape` を別の綴りへ（`角形鋼管` / `溝形鋼` / `W Shape`）
//	  I  `ProfileSeries` を `AISC (Metric)` へ（`ProfileSize` はそのまま）
//	  J  `ProfileShape` と `ProfileSize` の**両方**を空 → 落ち先は G の `W44 X 335` と同じか
//	  K  **`ProfileSymbol` にシンボル定義を与える**（矩形 Y120×Z240 を 1 つ入れた定義。
//	     `SetParamSymDef`（参照）と `SetParamValue`（名前）の 2 通りを別個体で）
//	     → **任意の断面を SDK から与える道があるか。取り込みの実装がここで決まる。**
//	  L  `MemberType`（索引 2・4 択）を 0〜3 へ → 断面の決め方が変わる欄ではないか
//
//	**どの試験も末尾で寸法 4 欄を書く**（第 1 版で見つからなかった「効く条件」を、
//	新しい条件の上でもう一度探すため）。判定は第 1 版と同じ 2 つの数字だけ——
//	PIO の外接の **Y 幅**と立方の **Z 高さ**。基準は [A] の 165.6 / 313.4。
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

	// 断面を指す 4 欄。第 1 版で ProfileShape と ProfileSize が効くと分かった。
	const char* const kProfProfileFields[] = {"ProfileShape", "ProfileSeries", "ProfileSize",
											  "ProfileSymbol"};
	const size_t kProfProfileCount = sizeof(kProfProfileFields) / sizeof(kProfProfileFields[0]);

	// 断面の寸法らしい名前の 4 欄。第 1 版ではどの条件でも効かなかった。
	const char* const kProfDimFields[] = {"MajorBreadth", "MinorBreadth", "MajorDepth",
										  "MinorDepth"};
	const double kProfDimValues[] = {200.0, 60.0, 800.0, 90.0};
	const size_t kProfDimCount = sizeof(kProfDimFields) / sizeof(kProfDimFields[0]);

	// OIP の読み取り専用の表示欄。第 1 版で「寸法欄の鏡」と分かっている。
	const char* const kProfStaticFields[] = {"B", "B1", "D", "D1"};
	const size_t kProfStaticCount = sizeof(kProfStaticFields) / sizeof(kProfStaticFields[0]);

	// [G] ProfileSize へ書く綴り。**W44 X 335 は AISC で最大の W（1117.6×403.9mm）**で、
	// 第 1 版のフォールバック 405.1×1118.1 とほぼ一致する。
	const char* const kProfSizeSpellings[] = {"W12 X 26", "W14 X 30", "W44 X 335", "W12X30",
											  "w12 x 30"};
	const size_t kProfSizeCount = sizeof(kProfSizeSpellings) / sizeof(kProfSizeSpellings[0]);

	// [H] ProfileShape へ書く綴り。既定が日本語（H形鋼）なので、日本語の別項目と
	// 英語の綴りを混ぜて投げる。
	const char* const kProfShapeSpellings[] = {"角形鋼管", "溝形鋼", "W Shape"};
	const size_t kProfShapeCount = sizeof(kProfShapeSpellings) / sizeof(kProfShapeSpellings[0]);

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

	// **判定はここだけ。** 断面の大きさは外接の Y 幅と立方の Z 高さに出る。
	// 基準は [A] の 165.6 / 313.4、第 1 版のフォールバックは 405.1 / 1118.1。
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
			else
				line += " 群=（外接取れず）";
			size_t kids = 0;
			for (MCObjectHandle kid = gSDK->FirstMemberObj(group); kid != nil;
				 kid = gSDK->NextObject(kid))
				if (gSDK->GetObjectTypeN(kid) != 0)
					++kids;
			line += " 群の子" + ProfWhole(static_cast<long long>(kids));
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

	// 「寸法 4 欄を書いて reset して測る」——どの試験の末尾でも同じことをする。
	void ProfTryDims(vwprobe::Report& probe, MCObjectHandle member, const std::string& tag)
	{
		ProfSetDims(probe, member);
		const std::string reset = ProfReset(member);
		ProfSnap(probe, tag + " 寸法 4 欄を書いた reset=" + reset, member);
		ProfDumpWitness(probe, member, tag + " 寸法後");
	}

	// 「1 欄へ 1 綴りを書いて reset して測り、そのあと寸法 4 欄を試す」を 1 個体で。
	void ProfTrial(vwprobe::Report& probe, const std::string& tag, double originY,
				   const char* field, const std::string& value)
	{
		MCObjectHandle member = ProfMake(probe, tag, originY);
		if (member == nil)
			return;
		const std::string first = ProfReset(member);
		ProfSnap(probe, tag + " 既定 reset=" + first, member);
		ProfSetValue(probe, member, field, value);
		const std::string second = ProfReset(member);
		ProfSnap(probe, tag + " " + field + "=[" + value + "] reset=" + second, member);
		ProfTryDims(probe, member, tag);
	}
} // namespace

VW_PROBE("member-profile-source", "構造材の断面を決める 2 欄の裏を取る（第 2 版）",
		 "ProfileShape / ProfileSize に綴りを書いて意図した断面になるかを見る。"
		 "ProfileSymbol へシンボルを与える道も試す")
{
	gSDK->DefineCustomObject(kProfPioName, kCustomObjectPrefNever);
	probe.log("読み方: 行末の 幅Y / 高さZ だけを見る。基準は [A] の 165.6 / 313.4、"
			  "第 1 版で見た「解決失敗の落ち先」は 405.1 / 1118.1。");

	double originY = 0.0;

	// ---- [A] 基準（既定の 1 本） -----------------------------------------
	{
		MCObjectHandle member = ProfMake(probe, "[A]", originY);
		originY += 2000.0;
		if (member != nil)
		{
			const std::string reset = ProfReset(member);
			ProfSnap(probe, "[A] 既定のまま reset=" + reset, member);
			ProfDumpWitness(probe, member, "[A] 既定");

			// MemberType（索引 2・4 択）の選択肢。**断面の決め方を握る欄ではないか。**
			VWParametricObj pio(member);
			const size_t typeIndex = pio.GetParamIndex(TXString("MemberType"));
			TXStringSTLArray keys;
			TXStringSTLArray shown;
			pio.GetParamChoices(typeIndex, keys);
			pio.GetParamLocalizedChoices(typeIndex, shown);
			std::string line = "[A] MemberType 値=[" + ProfText(pio.GetParamValue(typeIndex)) +
							   "] 選択肢" + ProfWhole(static_cast<long long>(keys.size()));
			for (size_t at = 0; at < keys.size(); ++at)
			{
				line += " [" + ProfText(keys[at]) + "=";
				line += (at < shown.size() ? ProfText(shown[at]) : std::string("?"));
				line += "]";
			}
			probe.log(line);
		}
	}

	// ---- [G] `ProfileSize` を同じ系列の別項目へ（1 綴り 1 個体） ----------
	// **本命その 1。** 正しい綴りで意図した断面になるなら、取り込みはここを書けばよい。
	for (size_t at = 0; at < kProfSizeCount; ++at)
	{
		ProfTrial(probe, std::string("[G") + ProfWhole(static_cast<long long>(at + 1)) + "]",
				  originY, "ProfileSize", kProfSizeSpellings[at]);
		originY += 2000.0;
	}

	// ---- [H] `ProfileShape` を別の綴りへ（1 綴り 1 個体） ------------------
	for (size_t at = 0; at < kProfShapeCount; ++at)
	{
		ProfTrial(probe, std::string("[H") + ProfWhole(static_cast<long long>(at + 1)) + "]",
				  originY, "ProfileShape", kProfShapeSpellings[at]);
		originY += 2000.0;
	}

	// ---- [I] `ProfileSeries` を別の系列へ ---------------------------------
	ProfTrial(probe, "[I]", originY, "ProfileSeries", "AISC (Metric)");
	originY += 2000.0;

	// ---- [J] `ProfileShape` と `ProfileSize` の両方を空 -------------------
	// 落ち先が [G3]（`W44 X 335`）と同じなら、「解決失敗の落ち先は表の最大」と読める。
	{
		MCObjectHandle member = ProfMake(probe, "[J]", originY);
		originY += 2000.0;
		if (member != nil)
		{
			const std::string first = ProfReset(member);
			ProfSnap(probe, "[J] 既定 reset=" + first, member);
			ProfSetValue(probe, member, "ProfileShape", "");
			ProfSetValue(probe, member, "ProfileSize", "");
			const std::string second = ProfReset(member);
			ProfSnap(probe, "[J] Shape と Size の両方を空にした reset=" + second, member);
			ProfTryDims(probe, member, "[J]");
		}
	}

	// ---- [K] `ProfileSymbol` にシンボル定義を与える -----------------------
	// **本命その 2。** 任意の断面を SDK から与える道があるかどうか。
	// 定義には矩形（Y 120 × Z 240・下端を 0 に合わせる。既定の群も下端 0）を 1 つ入れる。
	{
		// CreateSymbolDefinition は名前を **参照で受けて書き換える**（重複していれば
		// 別名になる）。だから一時オブジェクトでは渡せない——実際に使われた名前を
		// 読み戻して、以降はそれで引く。
		TXString symName(kProfSymbolName);
		MCObjectHandle symDef = gSDK->CreateSymbolDefinition(symName);
		MCObjectHandle rect = gSDK->CreateRectangle(WorldRect(-60.0, 240.0, 60.0, 0.0));
		probe.log(std::string("[K] CreateSymbolDefinition=") + (symDef != nil ? "ok" : "**nil**") +
				  " CreateRectangle=" + (rect != nil ? "ok" : "**nil**"));
		InternalIndex symRef = 0;
		if (symDef != nil && rect != nil)
		{
			const bool added = gSDK->AddObjectToContainer(rect, symDef);
			gSDK->ResetObject(symDef);
			symRef = gSDK->GetObjectInternalIndex(symDef);
			WorldRect symBounds;
			std::string line = std::string("[K] 定義へ矩形を入れた AddObjectToContainer=") +
							   (added ? "true" : "**false**") + " 参照=" + ProfWhole(symRef) +
							   " 実際の名前=[" + ProfText(symName) + "]";
			if (gSDK->GetObjectBounds(symDef, symBounds))
				line += " 定義の外接" + ProfRectText(symBounds);
			probe.log(line);
		}

		// K1: 参照で書く（`SetParamSymDef`）。
		{
			MCObjectHandle member = ProfMake(probe, "[K1]", originY);
			originY += 2000.0;
			if (member != nil)
			{
				const std::string first = ProfReset(member);
				ProfSnap(probe, "[K1] 既定 reset=" + first, member);
				VWParametricObj pio(member);
				const size_t index = pio.GetParamIndex(TXString("ProfileSymbol"));
				pio.SetParamSymDef(index, symRef);
				probe.log("  ProfileSymbol ← SetParamSymDef(" + ProfWhole(symRef) +
						  ") 読み戻し索引=" + ProfWhole(pio.GetParamSymDef(index)) + " 値=[" +
						  ProfText(pio.GetParamValue(index)) + "]");
				const std::string second = ProfReset(member);
				ProfSnap(probe, "[K1] SetParamSymDef で書いた reset=" + second, member);
				ProfTryDims(probe, member, "[K1]");
			}
		}

		// K2: 名前で書く（`SetParamValue`）。クラス欄はこちらが正解だった（Findings）。
		ProfTrial(probe, "[K2]", originY, "ProfileSymbol", ProfText(symName));
		originY += 2000.0;

		// K3: シンボルを与えたうえで Shape と Size を空にする（どちらが勝つか）。
		{
			MCObjectHandle member = ProfMake(probe, "[K3]", originY);
			originY += 2000.0;
			if (member != nil)
			{
				const std::string first = ProfReset(member);
				ProfSnap(probe, "[K3] 既定 reset=" + first, member);
				ProfSetValue(probe, member, "ProfileSymbol", ProfText(symName));
				ProfSetValue(probe, member, "ProfileShape", "");
				ProfSetValue(probe, member, "ProfileSize", "");
				const std::string second = ProfReset(member);
				ProfSnap(probe, "[K3] シンボルを与えて Shape/Size を空にした reset=" + second,
						 member);
				ProfTryDims(probe, member, "[K3]");
			}
		}
	}

	// ---- [L] `MemberType` を 0〜3 へ（1 値 1 個体） ------------------------
	for (int type = 0; type <= 3; ++type)
	{
		ProfTrial(probe, std::string("[L") + ProfWhole(type) + "]", originY, "MemberType",
				  ProfWhole(type));
		originY += 2000.0;
	}

	probe.log("おわり");
}
