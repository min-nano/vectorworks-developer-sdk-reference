//
//	probes/runtime/pio-create-one-regen/probe.cpp
//
//	[issue #122] 点で置く PIO（ドア・窓）を「パラメータを指定した状態で 1 回の再生成」で
//	作れるかを実測する。
//
//	いまの作りは「作る → パラメトリックレコードを書く → ResetObject」で、作成時の再生成と
//	ResetObject の再生成で **2 回**描いている。パスで置く PIO には
//	`CreateCustomObjectPath(..., doRegen=false)` という逃げ道があり、それで描画時間がほぼ
//	半分になることは実測済み（Findings/Parametric Objects.md）。**点で置く PIO 用の
//	`CreateCustomObject` には doRegen が無い**ので、代わりになる経路があるかを確かめる。
//
//	ヘッダから分かるのはここまで（ci-debug の sdk-grep で確認済み）:
//
//	  - パラメータ値を渡す引数を持つ生成 API は ISDK に無い。
//	  - `CreateCustomObject(name, location, angle, bInsert)` と
//	    `CreateCustomObjectByMatrixEx(name, matrix, bInsert)` に bInsert がある。**意味は
//	    ヘッダにコメントが無く、SDKLib/Source にも実装が無い**（カーネル側）。
//	    「図面に入れない（＝再生成しない）」のか「壁へ挿入する」のかは実機でしか分からない。
//	  - `VWRecordFormatObj::SetParamReal` 等でレコード書式そのものを書ける。書式の値が
//	    新しいインスタンスの既定値になるなら、作成時の 1 回だけで目的の値に描ける。
//
//	そこで次の 5 経路を、同じ PIO・同じ本数で順に走らせて比べる:
//
//	  R1  CreateCustomObject(bInsert=true) → 書く → ResetObject     … いまのやり方
//	  R2  CreateCustomObject(bInsert=false) → 書く → AddObjectToContainer → ResetObject
//	  R3  CreateCustomObjectByMatrixEx(bInsert=false) → 同上
//	  R4  書式の既定値を書いてから CreateCustomObject（ResetObject を呼ばない）
//	  R5  点で置く PIO へ CreateCustomObjectPath(name, nil, nil, doRegen=false)
//
//	**走らせる順は R1 → R4 → R2 → R3 → R5**（番号順ではない）。R2 / R3 は図面に入って
//	いないかもしれないオブジェクトへ書き込み、R5 はパス用の口を点の PIO へ向けるので、
//	**落ちるとすればこの 3 つ**である。落ちてもそこまでのログは残る（Probe.h）ので、
//	**答えの価値が高い R4（書式の既定値）を先に済ませておく**。
//
//	**「作成時に再生成が走ったか」の判定は、ResetObject を呼ぶ前の子オブジェクトの数で行う**
//	（PIO の実体は子として持たれるので、再生成していなければ 0 件になるはず）。所要時間は
//	その裏づけに使う（再生成 1 回はこの機種で 10ms 前後かかることが分かっている）。
//	目視でしか分からないことに依存しないよう、判定材料はすべて数で取る。
//

#include "Probe.h"

#include "VWFC/Math/VWTransformMatrix.h"
#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWRecordFormatObj.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	using namespace VWFC::Math;
	using namespace VWFC::VWObjects;

	// 1 経路あたりの本数。再生成 1 回が 10ms 前後なので、20 本なら経路ごとの差は
	// 測定のぶれ（1〜2 割）に埋もれない。
	const size_t kProbePioCount = 20;

	// 経路ごとに y をずらして置く（図面上で見分けが付くように。判定には使わない）。
	const double kProbeRouteSpacingY = 6000.0;
	const double kProbeItemSpacingX = 2000.0;

	double ProbeNowMs()
	{
		using namespace std::chrono;
		return duration_cast<duration<double, std::milli>>(steady_clock::now().time_since_epoch())
			.count();
	}

	std::string ProbeNum(double value, int digits = 3)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
		return std::string(buffer);
	}

	std::string ProbeCount(long value)
	{
		return std::to_string(value);
	}

	// PIO の実体（描かれたジオメトリ）は子オブジェクトとして持たれる。**ResetObject を
	// 呼ぶ前にこれが 0 件なら、作成時に再生成は走っていない**。
	size_t ProbeCountMembers(MCObjectHandle hObject)
	{
		size_t count = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hObject); member != nil;
			 member = gSDK->NextObject(member))
			++count;
		return count;
	}

	// そのハンドルがレイヤの直下に並んでいるか（bInsert=false が「図面に入れない」意味
	// なのかを、数で確かめるため）。
	bool ProbeIsInLayer(MCObjectHandle hLayer, MCObjectHandle hObject)
	{
		if (hLayer == nil || hObject == nil)
			return false;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hLayer); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (member == hObject)
				return true;
		}
		return false;
	}

	// universal 名を表から探す（GetParamIndex の「無いとき」の戻りがヘッダに書かれて
	// いないので、表を舐めて自分で判定する）。
	bool ProbeHasParam(const VWParametricObj& pio, const TXString& univName)
	{
		const size_t count = pio.GetParamsCount();
		for (size_t index = 0; index < count; ++index)
		{
			if (pio.GetParamName(index) == univName)
				return true;
		}
		return false;
	}

	// 1 経路ぶんの測定結果。
	struct ProbeRouteResult
	{
		std::string label;
		double createMs = 0.0;		   // 生成の呼び出しだけの合計
		double writeMs = 0.0;		   // パラメータ書き込みの合計
		double attachMs = 0.0;		   // AddObjectToContainer の合計
		double resetMs = 0.0;		   // ResetObject の合計
		size_t created = 0;			   // nil でなかった本数
		size_t membersAfterCreate = 0; // ResetObject 前の子の数（全本の合計）
		size_t membersAfterReset = 0;  // ResetObject 後の子の数（全本の合計）
		size_t inLayer = 0;		 // レイヤ直下に並んでいた本数（最後に数える）
		size_t valueOk = 0;		 // 最後に読み戻した値が狙いどおりだった本数
		double firstValue = 0.0; // 1 本目の読み戻し値（狙いと違うときの手掛かり）
	};

	void ProbeLogRoute(::vwprobe::Report& probe, const ProbeRouteResult& result)
	{
		const double total = result.createMs + result.writeMs + result.attachMs + result.resetMs;
		const double per = kProbePioCount > 0 ? total / double(kProbePioCount) : 0.0;
		probe.log("[" + result.label + "] 作れた " + ProbeCount(long(result.created)) + "/" +
				  ProbeCount(long(kProbePioCount)) + " 本");
		probe.log("[" + result.label + "] 所要 合計 " + ProbeNum(total) + "ms ＝ " + ProbeNum(per) +
				  "ms/本（生成 " + ProbeNum(result.createMs) + "ms ／ 書き込み " +
				  ProbeNum(result.writeMs) + "ms ／ 図面へ入れる " + ProbeNum(result.attachMs) +
				  "ms ／ ResetObject " + ProbeNum(result.resetMs) + "ms）");
		probe.log("[" + result.label + "] 子オブジェクト 生成直後の合計 " +
				  ProbeCount(long(result.membersAfterCreate)) + " 件 ／ 最後の合計 " +
				  ProbeCount(long(result.membersAfterReset)) + " 件");
		probe.log("[" + result.label + "] レイヤ直下にあった " + ProbeCount(long(result.inLayer)) +
				  " 本 ／ 値が狙いどおり " + ProbeCount(long(result.valueOk)) +
				  " 本（1 本目の読み戻し " + ProbeNum(result.firstValue) + "）");
		probe.log(
			"[" + result.label + "] 判定: 作成時の再生成は " +
			(result.membersAfterCreate == 0 ? "走っていない（子 0 件）" : "走った（子がある）"));
	}
} // namespace

VW_PROBE("pio-create-one-regen", "点で置く PIO を 1 回の再生成で作れるか",
		 "ドア／窓を 5 つの経路で 20 本ずつ作り、作成時に再生成が走るか・パラメータを"
		 "指定した状態で作れるかを、子オブジェクトの数と所要時間で測る")
{
	MCObjectHandle hLayer = gSDK->GetActiveLayer();
	probe.log(std::string("アクティブレイヤ: ") + (hLayer != nil ? "あり" : "**nil**"));
	if (hLayer == nil)
	{
		probe.fail("GetActiveLayer が nil を返した（新規の空図面で走らせること）");
		return;
	}

	// -----------------------------------------------------------------------
	// 対象の PIO を決める。universal 名が実機の登録と違うと、CreateCustomObject は
	// **その名前の空の定義を作ってしまう**（Findings/Parametric Objects.md「生成時に
	// 『オブジェクトの設定』ダイアログが出る」）ので、パラメータ表の件数で「本物か」を
	// 先に確かめる。本物のドア／窓は数十〜百件のパラメータを持つ。
	// -----------------------------------------------------------------------
	const TXString kPioNames[] = {TXString("Door"), TXString("Window")};
	TXString pioName;
	size_t pioParamCount = 0;
	{
		for (size_t i = 0; i < 2 && pioName.IsEmpty(); ++i)
		{
			const TXString& candidate = kPioNames[i];
			probe.log(std::string("試し置き: CreateCustomObject(\"") +
					  static_cast<const char*>(candidate) + "\") を 1 本");
			MCObjectHandle hTrial =
				gSDK->CreateCustomObject(candidate, WorldPt(-20000, -20000), 0.0, true);
			if (hTrial == nil)
			{
				probe.log(std::string("  → nil。この名前は使えない"));
				continue;
			}
			VWParametricObj trial(hTrial);
			const size_t count = trial.GetParamsCount();
			probe.log(std::string("  → できた。型 ") +
					  ProbeCount(long(gSDK->GetObjectTypeN(hTrial))) + " ／ パラメータ " +
					  ProbeCount(long(count)) + " 件 ／ 名前 " +
					  static_cast<const char*>(trial.GetParametricName()));
			if (count >= 10)
			{
				pioName = candidate;
				pioParamCount = count;
			}
			else
			{
				probe.log("  → パラメータが少なすぎる。実機に登録された PIO ではない"
						  "（空の定義が作られた）と見て、この名前は使わない");
			}
			gSDK->DeleteObject(hTrial, false);
		}
	}
	if (pioName.IsEmpty())
	{
		probe.fail("Door / Window のどちらも実機の PIO として引けなかった。"
				   "universal 名を実機の登録から採り直すこと");
		return;
	}
	probe.log(std::string("対象: ") + static_cast<const char*>(pioName) + "（パラメータ " +
			  ProbeCount(long(pioParamCount)) + " 件）");

	// -----------------------------------------------------------------------
	// 書き換えるパラメータを決める。**候補を順に試し、表にあるものを使う**
	// （名前を決め打ちにすると、版が変わったときに黙って外れる）。
	// -----------------------------------------------------------------------
	TXString paramName;
	double baseValue = 0.0;
	MCObjectHandle hSeed = gSDK->CreateCustomObject(pioName, WorldPt(-20000, -10000), 0.0, true);
	if (hSeed == nil)
	{
		probe.fail("パラメータを調べるための 1 本を作れなかった");
		return;
	}
	{
		VWParametricObj seed(hSeed);
		const TXString kCandidates[] = {TXString("Width"), TXString("OverallWidth"),
										TXString("DoorWidth"), TXString("WinWidth")};
		for (size_t i = 0; i < 4 && paramName.IsEmpty(); ++i)
		{
			if (ProbeHasParam(seed, kCandidates[i]))
			{
				paramName = kCandidates[i];
				baseValue = seed.GetParamReal(paramName);
			}
		}
		// 見つからなければ、名前に Width を含むものを表から拾う。
		if (paramName.IsEmpty())
		{
			const size_t count = seed.GetParamsCount();
			for (size_t index = 0; index < count; ++index)
			{
				const TXString name = seed.GetParamName(index);
				if (std::string(static_cast<const char*>(name)).find("Width") != std::string::npos)
				{
					paramName = name;
					baseValue = seed.GetParamReal(paramName);
					break;
				}
			}
		}
		// 参考として、表にある Width / Height 系の universal 名を出しておく
		// （次に走らせる人が名前を探さなくて済むように）。
		{
			std::string names;
			const size_t count = seed.GetParamsCount();
			for (size_t index = 0; index < count; ++index)
			{
				const std::string name(static_cast<const char*>(seed.GetParamName(index)));
				if (name.find("Width") != std::string::npos ||
					name.find("Height") != std::string::npos)
				{
					if (!names.empty())
						names += " ";
					names += name;
				}
			}
			probe.log("表にある Width / Height 系の universal 名: " +
					  (names.empty() ? std::string("（無し）") : names));
		}
	}
	gSDK->DeleteObject(hSeed, false);
	if (paramName.IsEmpty())
	{
		probe.fail("書き換えるパラメータを決められなかった（Width を含む名前が表に無い）");
		return;
	}
	const double targetValue = baseValue + 300.0;
	probe.log(std::string("書き換えるパラメータ: ") + static_cast<const char*>(paramName) +
			  "（既定 " + ProbeNum(baseValue) + " → 狙い " + ProbeNum(targetValue) + "）");

	std::vector<ProbeRouteResult> results;

	// -----------------------------------------------------------------------
	// R1: いまのやり方。CreateCustomObject(bInsert=true) → 書く → ResetObject。
	// -----------------------------------------------------------------------
	{
		ProbeRouteResult route;
		route.label = "R1 いまのやり方 bInsert=true";
		std::vector<MCObjectHandle> handles;
		probe.log("---- R1 を走らせる ----");
		for (size_t i = 0; i < kProbePioCount; ++i)
		{
			const WorldPt where(double(i) * kProbeItemSpacingX, 0.0);
			double t0 = ProbeNowMs();
			MCObjectHandle h = gSDK->CreateCustomObject(pioName, where, 0.0, true);
			route.createMs += ProbeNowMs() - t0;
			if (h == nil)
				continue;
			++route.created;
			route.membersAfterCreate += ProbeCountMembers(h);

			t0 = ProbeNowMs();
			VWParametricObj(h).SetParamReal(paramName, targetValue);
			route.writeMs += ProbeNowMs() - t0;

			t0 = ProbeNowMs();
			gSDK->ResetObject(h);
			route.resetMs += ProbeNowMs() - t0;
			handles.push_back(h);
		}
		for (size_t i = 0; i < handles.size(); ++i)
		{
			route.membersAfterReset += ProbeCountMembers(handles[i]);
			if (ProbeIsInLayer(hLayer, handles[i]))
				++route.inLayer;
			const double value = VWParametricObj(handles[i]).GetParamReal(paramName);
			if (i == 0)
				route.firstValue = value;
			if (value == targetValue)
				++route.valueOk;
		}
		ProbeLogRoute(probe, route);
		results.push_back(route);
	}

	// -----------------------------------------------------------------------
	// R4: レコード書式の既定値を書いてから作る。**ResetObject もパラメータの書き込みも
	// しない**——それで狙いの値で生まれるなら、再生成は作成時の 1 回だけで済む。
	// -----------------------------------------------------------------------
	{
		ProbeRouteResult route;
		route.label = "R4 書式の既定値を書いてから作る（Reset 無し）";
		std::vector<MCObjectHandle> handles;
		probe.log("---- R4 を走らせる ----");

		MCObjectHandle hFormatSeed =
			gSDK->CreateCustomObject(pioName, WorldPt(-20000, 0.0), 0.0, true);
		if (hFormatSeed == nil)
		{
			probe.fail("[R4] 書式を引くための 1 本を作れなかった");
		}
		else
		{
			VWRecordFormatObj format = VWParametricObj(hFormatSeed).GetRecordFormat();
			const double beforeDefault = format.GetParamReal(paramName);
			probe.log("[R4] 書式の既定値（書く前）: " + ProbeNum(beforeDefault));

			format.SetParamReal(paramName, targetValue);
			const double afterDefault = format.GetParamReal(paramName);
			probe.log("[R4] 書式の既定値（書いた後）: " + ProbeNum(afterDefault) +
					  (afterDefault == targetValue ? "（書けた）" : "（**書けていない**）"));
			// 既にある本へ波及したかも見る（R1 の本は targetValue のままのはず）。
			probe.log("[R4] 書式を書いた時点で、この 1 本の値は " +
					  ProbeNum(VWParametricObj(hFormatSeed).GetParamReal(paramName)));

			for (size_t i = 0; i < kProbePioCount; ++i)
			{
				const WorldPt where(double(i) * kProbeItemSpacingX, kProbeRouteSpacingY * 3.0);
				double t0 = ProbeNowMs();
				MCObjectHandle h = gSDK->CreateCustomObject(pioName, where, 0.0, true);
				route.createMs += ProbeNowMs() - t0;
				if (h == nil)
					continue;
				++route.created;
				route.membersAfterCreate += ProbeCountMembers(h);
				handles.push_back(h);
			}
			for (size_t i = 0; i < handles.size(); ++i)
			{
				route.membersAfterReset += ProbeCountMembers(handles[i]);
				if (ProbeIsInLayer(hLayer, handles[i]))
					++route.inLayer;
				const double value = VWParametricObj(handles[i]).GetParamReal(paramName);
				if (i == 0)
					route.firstValue = value;
				if (value == targetValue)
					++route.valueOk;
			}
			ProbeLogRoute(probe, route);
			results.push_back(route);

			// 既定値を戻す（issue #122 の約束）。
			format.SetParamReal(paramName, beforeDefault);
			probe.log("[R4] 既定値を戻した: " + ProbeNum(format.GetParamReal(paramName)));
			gSDK->DeleteObject(hFormatSeed, false);
		}
	}

	// -----------------------------------------------------------------------
	// R2: bInsert=false で作り、パラメータを書いてから図面へ入れ、ResetObject。
	// bInsert が「図面に入れない」意味なら、生成直後の子は 0 件になるはず。
	// -----------------------------------------------------------------------
	{
		ProbeRouteResult route;
		route.label = "R2 bInsert=false → 書く → 図面へ → Reset";
		std::vector<MCObjectHandle> handles;
		probe.log("---- R2 を走らせる ----");
		for (size_t i = 0; i < kProbePioCount; ++i)
		{
			const WorldPt where(double(i) * kProbeItemSpacingX, kProbeRouteSpacingY);
			double t0 = ProbeNowMs();
			MCObjectHandle h = gSDK->CreateCustomObject(pioName, where, 0.0, false);
			route.createMs += ProbeNowMs() - t0;
			if (h == nil)
				continue;
			++route.created;
			const size_t members = ProbeCountMembers(h);
			route.membersAfterCreate += members;
			if (i == 0)
				probe.log("[R2] 1 本目: 生成直後の子 " + ProbeCount(long(members)) +
						  " 件 ／ レイヤ直下に " +
						  (ProbeIsInLayer(hLayer, h) ? "ある" : "**無い**"));

			t0 = ProbeNowMs();
			VWParametricObj(h).SetParamReal(paramName, targetValue);
			route.writeMs += ProbeNowMs() - t0;

			t0 = ProbeNowMs();
			const bool added = gSDK->AddObjectToContainer(h, hLayer);
			route.attachMs += ProbeNowMs() - t0;
			if (i == 0)
				probe.log(std::string("[R2] 1 本目: AddObjectToContainer は ") +
						  (added ? "true" : "**false**") + " ／ その直後の子 " +
						  ProbeCount(long(ProbeCountMembers(h))) + " 件");

			t0 = ProbeNowMs();
			gSDK->ResetObject(h);
			route.resetMs += ProbeNowMs() - t0;
			handles.push_back(h);
		}
		for (size_t i = 0; i < handles.size(); ++i)
		{
			route.membersAfterReset += ProbeCountMembers(handles[i]);
			if (ProbeIsInLayer(hLayer, handles[i]))
				++route.inLayer;
			const double value = VWParametricObj(handles[i]).GetParamReal(paramName);
			if (i == 0)
				route.firstValue = value;
			if (value == targetValue)
				++route.valueOk;
		}
		ProbeLogRoute(probe, route);
		results.push_back(route);
	}

	// -----------------------------------------------------------------------
	// R3: CreateCustomObjectByMatrixEx(bInsert=false) で同じことをする。
	// -----------------------------------------------------------------------
	{
		ProbeRouteResult route;
		route.label = "R3 ByMatrixEx bInsert=false";
		std::vector<MCObjectHandle> handles;
		probe.log("---- R3 を走らせる ----");
		for (size_t i = 0; i < kProbePioCount; ++i)
		{
			VWTransformMatrix matrix;
			matrix.SetOffset(double(i) * kProbeItemSpacingX, kProbeRouteSpacingY * 2.0, 0.0);
			double t0 = ProbeNowMs();
			MCObjectHandle h = gSDK->CreateCustomObjectByMatrixEx(pioName, matrix, false);
			route.createMs += ProbeNowMs() - t0;
			if (h == nil)
				continue;
			++route.created;
			const size_t members = ProbeCountMembers(h);
			route.membersAfterCreate += members;
			if (i == 0)
				probe.log("[R3] 1 本目: 生成直後の子 " + ProbeCount(long(members)) +
						  " 件 ／ レイヤ直下に " +
						  (ProbeIsInLayer(hLayer, h) ? "ある" : "**無い**"));

			t0 = ProbeNowMs();
			VWParametricObj(h).SetParamReal(paramName, targetValue);
			route.writeMs += ProbeNowMs() - t0;

			t0 = ProbeNowMs();
			gSDK->AddObjectToContainer(h, hLayer);
			route.attachMs += ProbeNowMs() - t0;

			t0 = ProbeNowMs();
			gSDK->ResetObject(h);
			route.resetMs += ProbeNowMs() - t0;
			handles.push_back(h);
		}
		for (size_t i = 0; i < handles.size(); ++i)
		{
			route.membersAfterReset += ProbeCountMembers(handles[i]);
			if (ProbeIsInLayer(hLayer, handles[i]))
				++route.inLayer;
			const double value = VWParametricObj(handles[i]).GetParamReal(paramName);
			if (i == 0)
				route.firstValue = value;
			if (value == targetValue)
				++route.valueOk;
		}
		ProbeLogRoute(probe, route);
		results.push_back(route);
	}

	// -----------------------------------------------------------------------
	// R5: 点で置く PIO へ CreateCustomObjectPath(name, nil, nil, doRegen=false) を使えるか。
	// パス PIO 用の口なので、点 PIO に対して何が起きるかはヘッダから読めない。
	// -----------------------------------------------------------------------
	{
		ProbeRouteResult route;
		route.label = "R5 CreateCustomObjectPath(nil, nil, doRegen=false)";
		std::vector<MCObjectHandle> handles;
		probe.log("---- R5 を走らせる ----");
		for (size_t i = 0; i < kProbePioCount; ++i)
		{
			double t0 = ProbeNowMs();
			MCObjectHandle h = gSDK->CreateCustomObjectPath(pioName, nil, nil, false);
			route.createMs += ProbeNowMs() - t0;
			if (h == nil)
			{
				if (i == 0)
					probe.log("[R5] 1 本目から nil が返った（点で置く PIO には使えない）");
				continue;
			}
			++route.created;
			const size_t members = ProbeCountMembers(h);
			route.membersAfterCreate += members;
			if (i == 0)
				probe.log("[R5] 1 本目: 型 " + ProbeCount(long(gSDK->GetObjectTypeN(h))) +
						  " ／ 生成直後の子 " + ProbeCount(long(members)) + " 件 ／ レイヤ直下に " +
						  (ProbeIsInLayer(hLayer, h) ? "ある" : "無い"));

			t0 = ProbeNowMs();
			VWParametricObj(h).SetParamReal(paramName, targetValue);
			route.writeMs += ProbeNowMs() - t0;

			// 置き場所を与える（パス PIO 用の口なので挿入点が決まっていない可能性がある）。
			t0 = ProbeNowMs();
			VWParametricObj(h).SetPointObjectPos(
				VWPoint2D(double(i) * kProbeItemSpacingX, kProbeRouteSpacingY * 4.0));
			route.attachMs += ProbeNowMs() - t0;

			t0 = ProbeNowMs();
			gSDK->ResetObject(h);
			route.resetMs += ProbeNowMs() - t0;
			handles.push_back(h);
		}
		for (size_t i = 0; i < handles.size(); ++i)
		{
			route.membersAfterReset += ProbeCountMembers(handles[i]);
			if (ProbeIsInLayer(hLayer, handles[i]))
				++route.inLayer;
			const double value = VWParametricObj(handles[i]).GetParamReal(paramName);
			if (i == 0)
				route.firstValue = value;
			if (value == targetValue)
				++route.valueOk;
		}
		ProbeLogRoute(probe, route);
		results.push_back(route);
	}

	// -----------------------------------------------------------------------
	// まとめ（R1 を物差しにして比を出す）。
	// -----------------------------------------------------------------------
	probe.log("==== まとめ（1 本あたり ms・R1 比） ====");
	double baseline = 0.0;
	for (size_t i = 0; i < results.size(); ++i)
	{
		const ProbeRouteResult& route = results[i];
		const double total = route.createMs + route.writeMs + route.attachMs + route.resetMs;
		const double per = kProbePioCount > 0 ? total / double(kProbePioCount) : 0.0;
		if (i == 0)
			baseline = per;
		const double ratio = baseline > 0.0 ? per / baseline : 0.0;
		probe.log(route.label + ": " + ProbeNum(per) + "ms/本（R1 比 " + ProbeNum(ratio, 2) +
				  "）／ 作成時の再生成 " + (route.membersAfterCreate == 0 ? "無し" : "有り") +
				  " ／ 値が狙いどおり " + ProbeCount(long(route.valueOk)) + "/" +
				  ProbeCount(long(route.created)) + " 本");
	}
}
