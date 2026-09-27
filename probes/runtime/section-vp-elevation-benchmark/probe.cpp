//
//	probes/runtime/section-vp-elevation-benchmark/probe.cpp
//
//	[issue #130] 断面ビューポートの注釈へ「レベル（標高）を示すオブジェクト」を置き、
//	  1) その PIO の universal 名（SDK ヘッダにある内部 ID kInternalID_ElevationBenchmark /
//	     …Benchmark2 に対応するもの）が何か
//	  2) SDK から素直に作れるか（CreateCustomObject で足りるか・スタイルが要るか）と
//	     全パラメータ（universal 名・ローカライズ名・欄型・初期値・ポップアップの選択肢）
//	  3) **断面ビューポートの注釈へ置いたとき、表示される高さが何で決まるか**
//	     （注釈空間の Y＝Z から自動で読むのか／パラメータの値が出るのか。動かしたら追うのか）
//	  4) 表示名（"GL" など）を書く口と、高さの数値を出さない設定があるか
//	を実機で確かめる。
//
//	**目視に頼らない作り**にしてある——レベルオブジェクトが「何を描いたか」は、PIO が
//	吐いた図形を辿ってテキスト（kTextNode）を読み出して比べる。だから「▼ の右に 2800 と
//	出たか」を人に尋ねなくてよい。4) の総当たり（文字欄に書く・真偽欄を反転する）も、
//	描かれたテキストがどう変わったかで判定する。
//

#include "Probe.h"

// レベルオブジェクトが「何かに拘束されているか」を見る口。**このヘッダは
// VectorworksSDK.h からは引き込まれない**ので、名指しで include する
// （SDKLib/Include/Interfaces が -I に入っている前提。plugin/CMakeLists.txt）。
#include "VectorWorks/Extension/IMarkersPluginSupport.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// --- 小さな道具 ------------------------------------------------------------

	std::string ProbeToStd(const TXString& s)
	{
		return std::string(static_cast<const char*>(s));
	}

	std::string ProbeNum(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.4f", v);
		return std::string(buf);
	}

	// PIO が描いた図形を辿って、テキストオブジェクトの中身を集める。
	// **これが「絵に何が出たか」の代わりになる**（目視を頼まないため）。
	void ProbeCollectTexts(MCObjectHandle h, std::vector<std::string>& out, int depth)
	{
		if (h == nil || depth > 5)
			return;
		for (MCObjectHandle child = gSDK->FirstMemberObj(h); child != nil;
			 child = gSDK->NextObject(child))
		{
			const short type = gSDK->GetObjectTypeN(child);
			if (type == kTextNode)
			{
				std::string text = ProbeToStd(gSDK->GetTextChars(child));
				// 改行はログ 1 行に畳む。
				for (size_t i = 0; i < text.size(); ++i)
					if (text[i] == '\n' || text[i] == '\r')
						text[i] = ' ';
				out.push_back(text);
			}
			else
			{
				ProbeCollectTexts(child, out, depth + 1);
			}
		}
	}

	std::string ProbeTextsOf(MCObjectHandle h)
	{
		std::vector<std::string> texts;
		ProbeCollectTexts(h, texts, 0);
		if (texts.empty())
			return "(テキストなし)";
		std::string joined;
		for (size_t i = 0; i < texts.size(); ++i)
		{
			if (i != 0)
				joined += " | ";
			joined += "〈" + texts[i] + "〉";
		}
		return joined;
	}

	// パラメータ表の 1 行。
	struct ProbeParamRow
	{
		std::string name;	   // universal 名
		std::string localized; // ローカライズ名（実機の言語）
		short style = 0;	   // EFieldStyle
		std::string value;	   // 文字列にした値
	};

	std::vector<ProbeParamRow> ProbeReadParams(MCObjectHandle h)
	{
		std::vector<ProbeParamRow> rows;
		if (h == nil)
			return rows;
		size_t count = 0;
		try
		{
			VWParametricObj probeObj(h);
			count = probeObj.GetParamsCount();
		}
		catch (...)
		{
			return rows;
		}
		VWParametricObj obj(h);
		for (size_t i = 0; i < count; ++i)
		{
			ProbeParamRow row;
			TXString uname;
			try
			{
				uname = obj.GetParamName(i);
				row.name = ProbeToStd(uname);
			}
			catch (...)
			{
				row.name = "(GetParamName が例外)";
				rows.push_back(row);
				continue;
			}
			try
			{
				row.localized = ProbeToStd(obj.GetParamLocalizedName(i));
			}
			catch (...)
			{
				row.localized = "(例外)";
			}
			try
			{
				row.style = static_cast<short>(obj.GetParamStyle(uname));
			}
			catch (...)
			{
				row.style = -1;
			}
			try
			{
				row.value = ProbeToStd(obj.GetParamAsString(uname));
			}
			catch (...)
			{
				row.value = "(例外)";
			}
			rows.push_back(row);
		}
		return rows;
	}

	// 2 つの表の差分（値が変わった欄）を 1 行ずつ出す。件数を返す。
	size_t ProbeLogDiff(vwprobe::Report& probe, const std::string& tag,
						const std::vector<ProbeParamRow>& base,
						const std::vector<ProbeParamRow>& other)
	{
		if (base.size() != other.size())
		{
			probe.log(tag + ": 欄の件数が違う（" + std::to_string(base.size()) + " 対 " +
					  std::to_string(other.size()) + "）");
			return 0;
		}
		size_t diffs = 0;
		for (size_t i = 0; i < base.size(); ++i)
		{
			if (base[i].value == other[i].value)
				continue;
			++diffs;
			probe.log(tag + ": [" + std::to_string(i) + "] " + base[i].name + " = 〈" +
					  base[i].value + "〉→〈" + other[i].value + "〉");
		}
		if (diffs == 0)
			probe.log(tag + ": 値の違う欄は 0 件");
		return diffs;
	}

	// 注釈空間での位置と実位置（外形）をログへ出す。
	void ProbeLogPlace(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		try
		{
			VWParametricObj obj(h);
			const VWPoint2D pos = obj.GetPointObjectPos();
			probe.log(tag + " 挿入点: (" + ProbeNum(pos.x) + ", " + ProbeNum(pos.y) + ")");
		}
		catch (...)
		{
			probe.log(tag + " 挿入点: (GetPointObjectPos が例外)");
		}
		WorldRect bounds;
		if (gSDK->GetObjectBounds(h, bounds))
			probe.log(tag + " 外形: left=" + ProbeNum(bounds.left) +
					  " top=" + ProbeNum(bounds.top) + " right=" + ProbeNum(bounds.right) +
					  " bottom=" + ProbeNum(bounds.bottom));
		else
			probe.log(tag + " 外形: GetObjectBounds が false");
	}
} // namespace

VW_PROBE("section-vp-elevation-benchmark", "断面ビューポートの注釈へレベル（標高マーカー）を置く",
		 "標高マーカー PIO の universal 名・全パラメータ・選択肢を出し、断面ビューポートの"
		 "注釈へ高さ違いで 3 本置いて、描かれた文字が注釈空間の Y（＝Z）から決まるのかを"
		 "実測する。表示名を書く口と数値を消す設定も、文字欄・真偽欄の総当たりで探す")
{
	// =========================================================================
	// 0) レベルオブジェクトの universal 名を突き止める
	//    SDK ヘッダには内部 ID しか無い（kInternalID_ElevationBenchmark = 102 /
	//    …Benchmark2 = 663）ので、名前は実機に尋ねるしかない。GetPluginType は
	//    「その名前のプラグインが在るか・種別は何か」を返すので、候補を総当たりできる。
	// =========================================================================
	static const char* const kCandidateNames[] = {
		"Elevation Benchmark",
		"ElevationBenchmark",
		"Elevation Benchmark2",
		"ElevationBenchmark2",
		"ElevBenchmark",
		"Benchmark",
		"Elevation Marker",
		"ElevationMarker",
		"Level Marker",
		"LevelMarker",
		"Level",
		"Stake Object",
		"StakeObject",
		"Section Elevation Marker",
		"SectionElevationMarker",
		"Reference Marker",
		"ReferenceMarker",
		"Datum Marker",
	};

	std::string pioName;
	for (const char* candidate : kCandidateNames)
	{
		const TXString name(candidate);
		EVSPluginType type = kVSPluginMenu;
		const bool found = gSDK->GetPluginType(name, type) != 0;
		TXString localized;
		const bool hasLocalized = gSDK->GetLocalizedPluginName(name, localized) != 0;
		probe.log(std::string("候補 〈") + candidate + "〉: GetPluginType=" +
				  (found ? "true" : "false") + " type=" + std::to_string(static_cast<int>(type)) +
				  " ローカライズ名=" + (hasLocalized ? ProbeToStd(localized) : "(取れず)"));
		// 種別 2（kVSPluginObject）＝ PIO。最初に見つかったものを採る。
		if (found && type == kVSPluginObject && pioName.empty())
			pioName = candidate;
	}

	if (pioName.empty())
	{
		probe.fail("候補のどれも PIO として見つからなかった（GetPluginType が種別 2 を返した"
				   "名前が 1 つも無い）。上の一覧を見て候補を足す");
		return;
	}
	probe.log("採用した universal 名: 〈" + pioName + "〉");

	// =========================================================================
	// 1) デザインレイヤに 1 本作って、パラメータ表を丸ごと出す（基準 A）
	// =========================================================================
	const MCObjectHandle designLayer = gSDK->GetCurrentLayer();
	// 「オブジェクトの設定」ダイアログで止まらないように、作る前に 1 度定義する
	// （probes/runtime/README.md の決まり）。
	gSDK->DefineCustomObject(pioName, kCustomObjectPrefNever);

	const MCObjectHandle a = gSDK->CreateCustomObject(pioName, WorldPt(0, 0), 0.0);
	if (a == nil)
	{
		probe.fail("CreateCustomObject(〈" + pioName + "〉) が nil を返した（デザインレイヤ）");
		return;
	}
	probe.log("A（デザインレイヤ）を作れた: 型=" + std::to_string(gSDK->GetObjectTypeN(a)));
	try
	{
		VWParametricObj obj(a);
		probe.log("A の PIO 名=" + ProbeToStd(obj.GetParametricName()) +
				  " ローカライズ名=" + ProbeToStd(obj.GetLocalizedParametricName()) +
				  " 内部 ID=" + std::to_string(static_cast<int>(obj.GetInternalID())));
	}
	catch (...)
	{
		probe.log("A: VWParametricObj が例外（PIO として扱えない）");
	}

	const std::vector<ProbeParamRow> paramsA = ProbeReadParams(a);
	probe.log("A のパラメータ件数: " + std::to_string(paramsA.size()));
	for (size_t i = 0; i < paramsA.size(); ++i)
	{
		const ProbeParamRow& row = paramsA[i];
		probe.log("  [" + std::to_string(i) + "] " + row.name + " / " + row.localized +
				  " 欄型=" + std::to_string(row.style) + " 値=〈" + row.value + "〉");
	}
	ProbeLogPlace(probe, "A", a);
	probe.log("A が描いた文字: " + ProbeTextsOf(a));

	// ポップアップ欄（8）・ラジオ欄（9）の選択肢を出す。「基準」「表示の種類」のような
	// 欄が何を選べるかは、これで分かる。
	for (const ProbeParamRow& row : paramsA)
	{
		if (row.style != 8 && row.style != 9)
			continue;
		std::string choices;
		for (Sint32 idx = 0; idx < 24; ++idx)
		{
			TXString choice;
			if (!gSDK->GetLocalizedPluginChoice(TXString(pioName.c_str()),
												TXString(row.name.c_str()), idx, choice))
				break;
			if (!choices.empty())
				choices += " / ";
			choices += std::to_string(idx) + ":" + ProbeToStd(choice);
		}
		probe.log("  選択肢 " + row.name + ": " + (choices.empty() ? "(取れず)" : choices));
	}

	// レベルオブジェクトが「何かに拘束されているか」を見る口が SDK にある
	// （IMarkersPluginSupport::IsElevationBenchmarkConstrained）。ストーリ／断面との
	// 関連付けがここに出るなら、注釈へ置いた個体との差になって現れるはず。
	using namespace VectorWorks::Extension;
	IMarkersPluginSupportPtr markers(IID_MarkersPluginSupport);
	if (markers)
		probe.log(std::string("A の拘束: IsElevationBenchmarkConstrained=") +
				  (markers->IsElevationBenchmarkConstrained(a) ? "true" : "false"));
	else
		probe.log("IMarkersPluginSupport を取得できなかった（拘束は見られない）");

	// =========================================================================
	// 2) 断面ビューポートを作る
	// =========================================================================
	// 断面に何か映るように、壁を 1 枚置く（高さは既定のまま）。
	const MCObjectHandle wall = gSDK->CreateWall(WorldPt(-2000, 0), WorldPt(2000, 0), 200);
	probe.log(std::string("試験用の壁: ") + (wall != nil ? "作れた" : "作れなかった"));

	const MCObjectHandle sheet = gSDK->CreateLayer("プローブ用シート", 2 /* シートレイヤ */);
	if (sheet == nil)
	{
		probe.fail("CreateLayer(…, 2) が nil を返した（シートレイヤを作れない）");
		return;
	}
	{
		TVariableBlock value;
		Sint16 layerType = 0;
		if (gSDK->GetObjectVariable(sheet, 154 /* ovLayerType */, value) &&
			value.GetSint16(layerType))
			probe.log("作ったレイヤの種類（ovLayerType。1=デザイン 2=シート）: " +
					  std::to_string(static_cast<int>(layerType)));
		else
			probe.log("作ったレイヤの種類（ovLayerType）: 読めなかった");
	}

	// 断面線は Y=0 の通りを横切る向きに引き、見る側は +Y 側。
	const MCObjectHandle vp = gSDK->CreateSectionViewport(WorldPt(-3000, 0), WorldPt(3000, 0),
														  WorldPt(0, 3000), 0, -1000, 10000, sheet);
	if (vp == nil)
	{
		probe.fail("CreateSectionViewport が nil を返した");
		return;
	}
	probe.log("断面ビューポートを作れた: 型=" + std::to_string(gSDK->GetObjectTypeN(vp)));
	gSDK->UpdateViewport(vp);

	// =========================================================================
	// 3) 注釈へ高さ違いで 3 本置く（注釈空間の Y＝Z）
	// =========================================================================
	static const double kTargetY[] = {0.0, 2800.0, 5600.0};
	MCObjectHandle placed[3] = {nil, nil, nil};
	for (int i = 0; i < 3; ++i)
	{
		gSDK->SetCurrentLayer(designLayer);
		MCObjectHandle h = gSDK->CreateCustomObject(pioName, WorldPt(1000, kTargetY[i]), 0.0);
		if (h == nil)
		{
			probe.fail("注釈用の " + std::to_string(i) +
					   " 本目を作れなかった"
					   "（CreateCustomObject が nil）");
			continue;
		}
		if (!gSDK->AddViewportAnnotationObject(vp, h))
		{
			probe.fail("AddViewportAnnotationObject が false を返した（" + std::to_string(i) +
					   " 本目）");
			continue;
		}
		// 注釈へ移すと VW がどこへ落とすかは当てにできない（Findings「Data Tags」）ので、
		// 注釈空間での座標を明示的に書き直す。
		try
		{
			VWParametricObj obj(h);
			obj.SetPointObjectPos(VWPoint2D(1000.0, kTargetY[i]));
		}
		catch (...)
		{
			probe.log(std::to_string(i) + " 本目: SetPointObjectPos が例外");
		}
		gSDK->ResetObject(h);
		placed[i] = h;
	}
	gSDK->UpdateViewport(vp);

	for (int i = 0; i < 3; ++i)
	{
		if (placed[i] == nil)
			continue;
		const std::string tag = "注釈 Y=" + ProbeNum(kTargetY[i]);
		ProbeLogPlace(probe, tag, placed[i]);
		probe.log(tag + " が描いた文字: " + ProbeTextsOf(placed[i]));
		if (markers)
			probe.log(tag + " の拘束: " +
					  (markers->IsElevationBenchmarkConstrained(placed[i]) ? "true" : "false"));
		ProbeLogDiff(probe, tag + " と A の差", paramsA, ProbeReadParams(placed[i]));
	}

	// =========================================================================
	// 4) 置いた 1 本を上げる（Y=5600 → 7000）。表示が追うか。
	// =========================================================================
	if (placed[2] != nil)
	{
		const std::vector<ProbeParamRow> before = ProbeReadParams(placed[2]);
		gSDK->MoveObject(placed[2], 0, 1400 /* 5600 → 7000 */);
		gSDK->ResetObject(placed[2]);
		gSDK->UpdateViewport(vp);
		ProbeLogPlace(probe, "移動後（狙いは Y=7000）", placed[2]);
		probe.log("移動後が描いた文字: " + ProbeTextsOf(placed[2]));
		ProbeLogDiff(probe, "移動の前後", before, ProbeReadParams(placed[2]));
	}

	// =========================================================================
	// 5) 文字欄の総当たり——表示名（"GL"）を書く口はどれか
	//    欄型 4（kFieldText）の欄ごとに 1 本ずつ注釈へ置き、目印を書いて描き直し、
	//    **描かれた文字に目印が出たか**で判定する。
	// =========================================================================
	probe.log("--- 文字欄（欄型 4）に書いて、絵に出るかを見る ---");
	int textTried = 0;
	for (const ProbeParamRow& row : paramsA)
	{
		if (row.style != 4)
			continue;
		if (++textTried > 12)
		{
			probe.log("文字欄が 12 件を超えたので打ち切る");
			break;
		}
		gSDK->SetCurrentLayer(designLayer);
		MCObjectHandle h = gSDK->CreateCustomObject(pioName, WorldPt(2000, 2800), 0.0);
		if (h == nil || !gSDK->AddViewportAnnotationObject(vp, h))
		{
			probe.log("  " + row.name + ": 個体を作れなかった");
			continue;
		}
		const std::string mark = "GLしるし";
		try
		{
			VWParametricObj obj(h);
			obj.SetPointObjectPos(VWPoint2D(2000.0, 2800.0));
			obj.SetParamString(TXString(row.name.c_str()), TXString(mark.c_str()));
			gSDK->ResetObject(h);
			const std::string readBack =
				ProbeToStd(obj.GetParamAsString(TXString(row.name.c_str())));
			probe.log("  " + row.name + ": 読み戻し=〈" + readBack +
					  "〉 描かれた文字=" + ProbeTextsOf(h));
		}
		catch (...)
		{
			probe.log("  " + row.name + ": 書き込みで例外");
		}
		gSDK->DeleteObject(h, true);
	}
	gSDK->UpdateViewport(vp);

	// =========================================================================
	// 6) 真偽欄の総当たり——高さの数値を出さない設定はどれか
	//    欄型 2（kFieldBoolean）の欄ごとに 1 本ずつ置いて反転し、描かれた文字の変化を見る。
	// =========================================================================
	probe.log("--- 真偽欄（欄型 2）を反転して、絵の文字がどう変わるかを見る ---");
	std::string baselineTexts;
	{
		gSDK->SetCurrentLayer(designLayer);
		MCObjectHandle h = gSDK->CreateCustomObject(pioName, WorldPt(3000, 2800), 0.0);
		if (h != nil && gSDK->AddViewportAnnotationObject(vp, h))
		{
			try
			{
				VWParametricObj obj(h);
				obj.SetPointObjectPos(VWPoint2D(3000.0, 2800.0));
			}
			catch (...)
			{
			}
			gSDK->ResetObject(h);
			baselineTexts = ProbeTextsOf(h);
			probe.log("  反転なしの基準: " + baselineTexts);
			gSDK->DeleteObject(h, true);
		}
	}
	int boolTried = 0;
	for (const ProbeParamRow& row : paramsA)
	{
		if (row.style != 2)
			continue;
		if (++boolTried > 24)
		{
			probe.log("真偽欄が 24 件を超えたので打ち切る");
			break;
		}
		gSDK->SetCurrentLayer(designLayer);
		MCObjectHandle h = gSDK->CreateCustomObject(pioName, WorldPt(3000, 2800), 0.0);
		if (h == nil || !gSDK->AddViewportAnnotationObject(vp, h))
		{
			probe.log("  " + row.name + ": 個体を作れなかった");
			continue;
		}
		try
		{
			VWParametricObj obj(h);
			obj.SetPointObjectPos(VWPoint2D(3000.0, 2800.0));
			const TXString uname(row.name.c_str());
			const bool was = obj.GetParamBool(uname);
			obj.SetParamBool(uname, !was);
			gSDK->ResetObject(h);
			const std::string texts = ProbeTextsOf(h);
			probe.log("  " + row.name + " (" + row.localized + "): " + (was ? "true" : "false") +
					  "→" + (!was ? "true" : "false") + " 文字=" + texts +
					  (texts == baselineTexts ? "（基準と同じ）" : "（基準と違う）"));
		}
		catch (...)
		{
			probe.log("  " + row.name + ": 反転で例外");
		}
		gSDK->DeleteObject(h, true);
	}
	gSDK->UpdateViewport(vp);

	probe.log("おわり（図面には試験用のシートレイヤ・断面ビューポート・壁が残る）");
}
