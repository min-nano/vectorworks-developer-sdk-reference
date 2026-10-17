//
//	probes/runtime/deferred-legend-tag/probe.cpp
//
//	[issue #245] グラフィック凡例とデータタグの「置いた時点で決まる / 描くまで分からない」を、
//	**書いた直後（T0）・ランループを回した後（T1）・undo イベントを閉じた直後（Te）・VW の
//	イベントループを経た 2 回目（T2）**の 4 時点で測り直す。過去の結論は、どれも**書いたのと
//	同じ呼び出しの中で読んで**出したものなので、「後で変わっていた」可能性を潰していない。
//	仕立ては Deferred.h（既定のテンプレートから開いた自分用の図面で走る）。
//
//	  tag: Findings「Data Tags」——「タグは指定した挿入点に留まらない」「タグの実寸は描くまで
//	       分からない（置いた後に GetObjectBounds で測って動かす）」「関連付け先からの相対は
//	       横だけ一定量ずれ」。矩形に関連付けたタグ t1 を既知の点に置き、外接・挿入点・描いた
//	       文字を読む。t2 は置いた後 MoveObject(+1000,+500) だけ（ResetObject なし）で、
//	       外接が追随するか。
//	  lg:  Findings「Graphic Legends」——「既定のソースは空なので、スタイル無しで置いた凡例は
//	       中身が 0 セット」「中身は置いた時点の ResetObject で決まる」「凡例イメージ（節点型
//	       122）は作り直しの後にしか無い」。シンボル 2 種を置いた図面のシート上に
//	         g0: ソース無し＋ResetObject
//	         g1: ソース 'GrLe'（T=SYMBOL）＋ResetObject
//	         g2: ソース 'GrLe'（T=SYMBOL）で ResetObject を呼ばない
//	       を置き、その後でシンボル lt-s3 を足す（g1 は作り直さない）。
//
//	測る側（measure）は読むだけ（SDK へ書かない）。図形は名前で引き直す。
//

#include "Deferred.h"

#include "VWFC/VWObjects/VWParametricObj.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace
{
	// ---------------------------------------------------------------------------
	// 小物

	std::string LtFromTX(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	std::string LtOneLine(const std::string& value)
	{
		std::string out;
		for (char c : value)
		{
			if (c == '\r')
				continue;
			if (c == '\n')
				out += "\\n";
			else if (c == '\t')
				out += "\\t";
			else
				out += c;
		}
		return out;
	}

	std::string LtParam(MCObjectHandle h, const char* param)
	{
		if (h == nullptr)
			return "(nil)";
		try
		{
			const VWFC::VWObjects::VWParametricObj pio(h);
			return "'" + LtOneLine(LtFromTX(pio.GetParamValue(param))) + "'";
		}
		catch (...)
		{
			return "(例外)";
		}
	}

	bool LtModelPos(MCObjectHandle h, double& x, double& y)
	{
		if (h == nullptr)
			return false;
		try
		{
			const VWFC::VWObjects::VWParametricObj pio(h);
			const VWFC::Math::VWPoint3D p = pio.GetObjectModelPos();
			x = p.x;
			y = p.y;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::string LtPos(MCObjectHandle h)
	{
		double x = 0.0;
		double y = 0.0;
		if (!LtModelPos(h, x, y))
			return h == nullptr ? "(nil)" : "(例外)";
		return "X" + DfNum(x, 1) + " Y" + DfNum(y, 1);
	}

	// 外接と、要求した点との関係（含むか・外接の中心と点の差・大きさ）。
	std::string LtVsPoint(MCObjectHandle h, double px, double py)
	{
		if (h == nullptr)
			return "(nil)";
		WorldRect r;
		if (!gSDK->GetObjectBounds(h, r))
			return "(false)";
		if (r.right < r.left)
			return "(空)";
		const bool inside = px >= r.left && px <= r.right && py >= r.bottom && py <= r.top;
		const double cx = (r.left + r.right) / 2.0;
		const double cy = (r.top + r.bottom) / 2.0;
		return "含む=" + DfBool(inside) + " 中心-点 dx=" + DfNum(cx - px, 1) +
			   " dy=" + DfNum(cy - py, 1) + " W=" + DfNum(r.right - r.left, 1) +
			   " H=" + DfNum(r.top - r.bottom, 1);
	}

	// ---------------------------------------------------------------------------
	// データタグ（旧プローブ deferred-datatag の DtCollectTexts / DtDrawn / DtExtracted /
	// DtTagText / DtApplyFormula / DtMakeTag をそのまま。名前の接頭辞だけ Lt に替えた）

	void LtCollectTexts(MCObjectHandle parent, std::vector<std::string>& out, int depth)
	{
		for (MCObjectHandle c = gSDK->FirstMemberObj(parent); c != nullptr; c = gSDK->NextObject(c))
		{
			const short type = gSDK->GetObjectTypeN(c);
			if (type == kTextNode)
				out.push_back(LtOneLine(LtFromTX(gSDK->GetTextChars(c))));
			else if (type == kGroupNode && depth < 3)
				LtCollectTexts(c, out, depth + 1);
		}
	}

	std::string LtDrawn(MCObjectHandle hTag)
	{
		if (hTag == nullptr)
			return "(nil)";
		std::vector<std::string> texts;
		LtCollectTexts(hTag, texts, 0);
		std::string s = "(テキスト " + std::to_string(texts.size()) + " 件)";
		for (const std::string& t : texts)
			s += " '" + (t.size() > 40 ? t.substr(0, 40) + "…" : t) + "'";
		return s;
	}

	std::string LtExtracted(MCObjectHandle hTag)
	{
		VectorWorks::Extension::IDataTagSupportPtr tagSupport(
			VectorWorks::Extension::IID_DataTagSupport);
		if (!tagSupport)
			return "(IDataTagSupport 無し)";
		VectorWorks::Extension::TXStringSTLPairArray extracted;
		tagSupport->GetDataTagExtractedData(hTag, extracted);
		if (extracted.empty())
			return "(0 件)";
		std::string out;
		for (const auto& pair : extracted)
			out += "'" + LtOneLine(LtFromTX(pair.second)) + "'";
		return out;
	}

	MCObjectHandle LtTagText(MCObjectHandle hTag)
	{
		MCObjectHandle hLiveGroup = gSDK->GetCustomObjectProfileGroup(hTag);
		if (hLiveGroup == nullptr)
			return nullptr;
		for (MCObjectHandle m = gSDK->FirstMemberObj(hLiveGroup); m != nullptr;
			 m = gSDK->NextObject(m))
			if (gSDK->GetObjectTypeN(m) == kTextNode)
				return m;
		return nullptr;
	}

	bool LtApplyFormula(MCObjectHandle hTag, const char* formula)
	{
		VectorWorks::Extension::IDataTagSupportPtr tagSupport(
			VectorWorks::Extension::IID_DataTagSupport);
		VectorWorks::Extension::IDataTagTextLinkSupportPtr linkSupport(
			VectorWorks::Extension::IID_DataTagTextLinkSupport);
		MCObjectHandle hText = LtTagText(hTag);
		if (!tagSupport || !linkSupport || hText == nullptr)
			return false;
		linkSupport->SetIsLinked(hText, true);
		linkSupport->SetFormula(hText, TXString(formula), false);
		tagSupport->UpdateUserDefinedTextsUIDs(hTag);
		tagSupport->UpdateDataTag(hTag);
		gSDK->ResetObject(hTag);
		return true;
	}

	void LtMakeTag(vwprobe::Report& probe, MCObjectHandle hMember, MCObjectHandle hLayer,
				   const std::string& name, const char* formula, double x, double y)
	{
		VectorWorks::Extension::IDataTagSupportPtr tagSupport(
			VectorWorks::Extension::IID_DataTagSupport);
		if (!tagSupport || hMember == nullptr)
		{
			probe.fail("タグ " + name + ": IDataTagSupport か部材が無い");
			return;
		}
		MCObjectHandle hTag = gSDK->CreateCustomObject("Data Tag", WorldPt(x, y), 0.0, true);
		if (hTag == nullptr)
		{
			probe.fail("タグ " + name + " を作れなかった（CreateCustomObject が nil）");
			return;
		}
		if (hLayer != nullptr)
			gSDK->AddObjectToContainer(hTag, hLayer);
		const bool associated = tagSupport->AssociateWithObject(hTag, hMember);

		MCObjectHandle hGroup = gSDK->CreateGroup(false);
		MCObjectHandle hText = gSDK->CreateTextBlock("T245", WorldPt(0.0, 0.0), false, 0.0);
		if (hGroup == nullptr || hText == nullptr)
		{
			probe.fail("タグ " + name + ": レイアウトを組めなかった");
			return;
		}
		gSDK->AddObjectToContainer(hText, hGroup);
		gSDK->SetCustomObjectProfileGroup(hTag, hGroup); // 中身を入れてから渡す
		DfSetName(probe, hTag, name);
		if (!LtApplyFormula(hTag, formula))
			probe.fail("タグ " + name + ": レイアウトにテキストが無い（式を持たせられない）");
		probe.log("  タグ " + name + " '" + LtOneLine(formula) +
				  "' Associate=" + DfBool(associated) + " → " + LtExtracted(hTag));
	}

	// ---------------------------------------------------------------------------
	// tag: タグの置き場所と実寸

	const char* const kLtFormula = "#IPZ#";
	// t1: 矩形 r1 の右に置く。t2: 矩形 r2 の右に置き、置いた後で (+1000, +500) 動かす。
	constexpr double kLtT1X = 3000.0;
	constexpr double kLtT1Y = -5500.0;
	constexpr double kLtT2X = 3000.0;
	constexpr double kLtT2Y = -8500.0;
	constexpr double kLtMoveX = 1000.0;
	constexpr double kLtMoveY = 500.0;

	void LtSetupTag(vwprobe::Report& probe)
	{
		gSDK->DefineCustomObject("Data Tag", kCustomObjectPrefNever);
		MCObjectHandle hLayer = gSDK->CreateLayer("LT245-TAG", kLayerDesign);
		if (hLayer == nullptr)
		{
			probe.fail("デザインレイヤ LT245-TAG を作れなかった");
			return;
		}
		gSDK->SetCurrentLayer(hLayer);

		MCObjectHandle hR1 = gSDK->CreateRectangle(WorldRect(0.0, -5000.0, 2000.0, -6000.0));
		MCObjectHandle hR2 = gSDK->CreateRectangle(WorldRect(0.0, -8000.0, 2000.0, -9000.0));
		if (hR1 == nullptr || hR2 == nullptr)
		{
			probe.fail("矩形を作れなかった");
			return;
		}
		DfSetName(probe, hR1, "lt-r1");
		DfSetName(probe, hR2, "lt-r2");
		probe.log("  矩形 lt-r1 " + DfBounds(hR1) + " / lt-r2 " + DfBounds(hR2));

		probe.log("  旧結論の期待（T0 で確かめる）: タグは指定した挿入点に留まらない"
				  "（関連付け先からの相対は「高さは合って横だけ一定量ずれ」）。実寸は描くまで"
				  "分からない＝置いた直後の外接は当てにならず、測って動かす。");
		LtMakeTag(probe, hR1, hLayer, "lt-t1", kLtFormula, kLtT1X, kLtT1Y);
		MCObjectHandle hT1 = DfFind("lt-t1");
		probe.log("  t1 要求点 X" + DfNum(kLtT1X, 1) + " Y" + DfNum(kLtT1Y, 1) + " → 挿入点 " +
				  LtPos(hT1) + " 外接 " + DfBounds(hT1) + " " + LtVsPoint(hT1, kLtT1X, kLtT1Y) +
				  " 描いた " + LtDrawn(hT1));

		LtMakeTag(probe, hR2, hLayer, "lt-t2", kLtFormula, kLtT2X, kLtT2Y);
		MCObjectHandle hT2 = DfFind("lt-t2");
		if (hT2 == nullptr)
			return;
		probe.log("  t2 動かす前: 挿入点 " + LtPos(hT2) + " 外接 " + DfBounds(hT2));
		gSDK->MoveObject(hT2, kLtMoveX, kLtMoveY);
		probe.log("  t2 MoveObject(+" + DfNum(kLtMoveX, 0) + ",+" + DfNum(kLtMoveY, 0) +
				  ")（ResetObject なし）直後: 挿入点 " + LtPos(hT2) + " 外接 " + DfBounds(hT2));
		probe.log("  期待の手掛かり: Findings「Parametric Objects」では ResetObject の後に"
				  " SetEntityMatrix で動かした PIO の外接は undo イベントが閉じるまで古い位置"
				  "（T0/T1 古い・Te/T2 新しい）。MoveObject でも同じかを見る。");
	}

	void LtReadTag(DeferredReadings& rs, const std::string& k, const char* tagName,
				   const char* rectName, double px, double py)
	{
		MCObjectHandle h = DfFind(tagName);
		rs.emplace_back(k + ".bounds", DfBounds(h));
		rs.emplace_back(k + ".pos", LtPos(h));
		rs.emplace_back(k + ".vsPoint", LtVsPoint(h, px, py));
		rs.emplace_back(k + ".drawn", LtDrawn(h));
		rs.emplace_back(k + ".ext", h != nullptr ? LtExtracted(h) : "(nil)");
		rs.emplace_back(k + ".kids", DfChildren(h));
		rs.emplace_back(k + ".rect", DfBounds(DfFind(rectName)));
	}

	DeferredReadings LtMeasureTag()
	{
		DeferredReadings rs;
		LtReadTag(rs, "t1", "lt-t1", "lt-r1", kLtT1X, kLtT1Y);
		// t2 は「動かした先」の点と比べる。
		LtReadTag(rs, "t2", "lt-t2", "lt-r2", kLtT2X + kLtMoveX, kLtT2Y + kLtMoveY);
		return rs;
	}

	// ---------------------------------------------------------------------------
	// lg: グラフィック凡例

	constexpr OSType kLtSourceContainer = 0x47724C65; // 'GrLe'
	constexpr Sint32 kLtSourceType = 1;				  // kTaggedDataByteArrayTypeID
	constexpr Sint32 kLtSourceTag = 0;
	// 実プラグイン（draw/Legend.cpp）の 22 バイト。6 番目の 16 ビット値（10 バイト目）が型。
	constexpr std::array<Uint8, 22> kLtSourceDefinition{
		0x9a, 0x02, 0x96, 0x02, 0x1e, 0x03, 0x96, 0x02, 0x09, 0x03, 0x0f,
		0x00, 0x47, 0x06, 0x65, 0x06, 0x4f, 0x06, 0xa5, 0x02, 0x90, 0x06}; // 型 15＝シンボル

	bool LtApplySource(MCObjectHandle hLegend)
	{
		if (!gSDK->TaggedDataCreate(hLegend, kLtSourceContainer, kLtSourceType, kLtSourceTag,
									static_cast<Sint32>(kLtSourceDefinition.size())))
			return false;
		for (size_t i = 0; i < kLtSourceDefinition.size(); ++i)
		{
			Uint8 value = kLtSourceDefinition[i];
			if (gSDK->TaggedDataSet(hLegend, kLtSourceContainer, kLtSourceType, kLtSourceTag,
									static_cast<Sint32>(i), &value) == 0)
				return false;
		}
		return true;
	}

	// シンボル定義を作る（中身は矩形 1 つ。入れた後の ResetObject を忘れない——Findings
	// 「Symbols」）。
	bool LtMakeSymbolDef(vwprobe::Report& probe, const char* name, double w, double h)
	{
		TXString defName(name);
		MCObjectHandle hDef = gSDK->CreateSymbolDefinition(defName);
		if (hDef == nullptr || LtFromTX(defName) != name)
		{
			probe.fail(std::string("シンボル定義 ") + name + " を作れなかった → '" +
					   LtFromTX(defName) + "'");
			return false;
		}
		MCObjectHandle hRect = gSDK->CreateRectangle(WorldRect(0.0, h, w, 0.0));
		if (hRect == nullptr)
		{
			probe.fail(std::string("シンボル ") + name + " の中身の矩形を作れなかった");
			return false;
		}
		gSDK->AddObjectToContainer(hRect, hDef);
		gSDK->SetPlanarRefID(hRect, kPlanarRefID_ScreenPlane);
		gSDK->ResetObject(hDef);
		return true;
	}

	bool LtPlaceSymbol(vwprobe::Report& probe, MCObjectHandle hLayer, const char* defName,
					   const char* instName, double x, double y)
	{
		MCObjectHandle h = gSDK->PlaceSymbolByNameN(TXString(defName), WorldPt(x, y), 0.0);
		if (h == nullptr)
		{
			probe.fail(std::string("シンボル ") + instName + " を置けなかった");
			return false;
		}
		gSDK->AddObjectToContainer(h, hLayer);
		if (gSDK->GetObjectTypeN(h) != kSymbolNode)
		{
			probe.fail(std::string("シンボル ") + instName + " の型が " +
					   std::to_string(gSDK->GetObjectTypeN(h)));
			return false;
		}
		DfSetName(probe, h, instName);
		return true;
	}

	// 凡例の中を辿る（グループだけ潜る。ビューポート＝凡例イメージには潜らない——Findings
	// 「Graphic Legends」）。型ごとの数・テキスト・並んだシンボルの定義名を集める。
	struct LtLegendWalk
	{
		std::map<short, int> byType;
		std::vector<std::string> texts;
		std::vector<std::string> symbols;
		std::vector<std::string> viewports; // 凡例イメージの外接
	};

	void LtWalkLegend(MCObjectHandle parent, LtLegendWalk& w, int depth)
	{
		for (MCObjectHandle c = gSDK->FirstMemberObj(parent); c != nullptr; c = gSDK->NextObject(c))
		{
			const short type = gSDK->GetObjectTypeN(c);
			++w.byType[type];
			if (type == kTextNode)
				w.texts.push_back(LtOneLine(LtFromTX(gSDK->GetTextChars(c))));
			else if (type == kSymbolNode)
				w.symbols.push_back(DfName(gSDK->GetDefinition(c)));
			else if (type == kViewportNode)
				w.viewports.push_back(DfBounds(c));
			else if (type == kGroupNode && depth < 6)
				LtWalkLegend(c, w, depth + 1);
		}
	}

	std::string LtJoin(const std::vector<std::string>& items, size_t maxItems = 8)
	{
		std::string s = std::to_string(items.size()) + " 件";
		for (size_t i = 0; i < items.size() && i < maxItems; ++i)
			s += " '" + (items[i].size() > 30 ? items[i].substr(0, 30) + "…" : items[i]) + "'";
		if (items.size() > maxItems)
			s += " …";
		return s;
	}

	std::string LtSourceCount(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		Sint32 n = -1;
		const bool ok = gSDK->TaggedDataGetNumElements(h, kLtSourceContainer, kLtSourceType,
													   kLtSourceTag, &n) != 0;
		return ok ? std::to_string(n) : "(無し)";
	}

	void LtReadLegend(DeferredReadings& rs, const std::string& k, const char* name)
	{
		MCObjectHandle h = DfFind(name);
		rs.emplace_back(k + ".bounds", DfBounds(h));
		rs.emplace_back(k + ".kids", DfChildren(h));
		if (h == nullptr)
			return;
		LtLegendWalk w;
		LtWalkLegend(h, w, 0);
		std::string hist;
		for (const auto& [type, count] : w.byType)
			hist += (hist.empty() ? "" : " ") + std::to_string(type) + ":" + std::to_string(count);
		rs.emplace_back(k + ".walk", "[" + hist + "]");
		rs.emplace_back(k + ".image122", LtJoin(w.viewports, 3));
		rs.emplace_back(k + ".symbols", LtJoin(w.symbols));
		rs.emplace_back(k + ".texts", LtJoin(w.texts));
		rs.emplace_back(k + ".BoxWidth", LtParam(h, "BoxWidth"));
		rs.emplace_back(k + ".ImageScale", LtParam(h, "ImageScale"));
		rs.emplace_back(k + ".SummarizeBy", LtParam(h, "SummarizeBy"));
		rs.emplace_back(k + ".GrLe", LtSourceCount(h));
	}

	MCObjectHandle LtMakeLegend(vwprobe::Report& probe, const char* name, double x, double y,
								bool withSource, bool reset)
	{
		MCObjectHandle h = gSDK->CreateCustomObject("GraphicLegend", WorldPt(x, y), 0.0, true);
		if (h == nullptr)
		{
			probe.fail(std::string("凡例 ") + name +
					   " を作れなかった（CreateCustomObject が nil）");
			return nullptr;
		}
		DfSetName(probe, h, name);
		bool widthOk = false;
		try
		{
			VWFC::VWObjects::VWParametricObj pio(h);
			pio.SetParamReal("BoxWidth", 40.0);
			widthOk = true;
		}
		catch (...)
		{
		}
		const bool sourceOk = withSource ? LtApplySource(h) : false;
		if (reset)
			gSDK->ResetObject(h);
		probe.log(std::string("  凡例 ") + name + ": BoxWidth=" + DfBool(widthOk) +
				  " ソース 'GrLe'=" + (withSource ? DfBool(sourceOk) : std::string("書かない")) +
				  " ResetObject=" + DfBool(reset) + " → 外接 " + DfBounds(h) + " 子 " +
				  DfChildren(h));
		return h;
	}

	void LtSetupLegend(vwprobe::Report& probe)
	{
		gSDK->DefineCustomObject("GraphicLegend", kCustomObjectPrefNever);
		MCObjectHandle hDesign = gSDK->CreateLayer("LT245-LGD", kLayerDesign);
		if (hDesign == nullptr)
		{
			probe.fail("デザインレイヤ LT245-LGD を作れなかった");
			return;
		}
		gSDK->SetCurrentLayer(hDesign);
		if (!LtMakeSymbolDef(probe, "lt-s1", 300.0, 200.0) ||
			!LtMakeSymbolDef(probe, "lt-s2", 500.0, 500.0) ||
			!LtMakeSymbolDef(probe, "lt-s3", 800.0, 100.0))
			return;
		LtPlaceSymbol(probe, hDesign, "lt-s1", "lt-i1a", 0.0, 0.0);
		LtPlaceSymbol(probe, hDesign, "lt-s1", "lt-i1b", 1000.0, 0.0);
		LtPlaceSymbol(probe, hDesign, "lt-s2", "lt-i2", 2000.0, 0.0);
		// 素の図形も少し（並ぶものが無いと言えないように）。
		if (MCObjectHandle hP = gSDK->CreateRectangle(WorldRect(0.0, 2000.0, 600.0, 1500.0)))
			DfSetName(probe, hP, "lt-p1");

		MCObjectHandle hSheet = gSDK->CreateLayer("LT245-SH", kLayerSheet);
		if (hSheet == nullptr)
		{
			probe.fail("シートレイヤ LT245-SH を作れなかった");
			return;
		}
		gSDK->SetCurrentLayer(hSheet);
		probe.log("  旧結論の期待（T0 で確かめる）: g0（ソース無し）は既定のソースが空なので"
				  "中身 0 セット＝並ぶシンボル 0・凡例イメージ(122) 0。g1（'GrLe' T=SYMBOL＋"
				  "ResetObject）は lt-s1 / lt-s2 が並ぶ見込み（Findings では「並ぶかは実機で見て"
				  "決める」で未確定）。g2（ResetObject なし）は中身が ResetObject で決まるので"
				  "空・凡例イメージは作り直しの後にしか無いので 122 は 0。");
		LtMakeLegend(probe, "lt-g0", 20.0, 250.0, false, true);
		LtMakeLegend(probe, "lt-g1", 120.0, 250.0, true, true);
		LtMakeLegend(probe, "lt-g2", 220.0, 250.0, true, false);

		// 作り直した後でシンボル lt-s3 を足す（g1 は作り直さない）。
		LtPlaceSymbol(probe, hDesign, "lt-s3", "lt-i3", 3000.0, 0.0);
		probe.log("  凡例を置いた後で lt-s3 を 1 つ足した（g1 は作り直さない）。期待: 中身は"
				  " ResetObject の時点で決まるので g1 に lt-s3 は並ばない（T0〜T2 とも）。");
		probe.log("  アクティブレイヤ='" + DfName(gSDK->GetActiveLayer()) + "'");
	}

	DeferredReadings LtMeasureLegend()
	{
		DeferredReadings rs;
		LtReadLegend(rs, "g0", "lt-g0");
		LtReadLegend(rs, "g1", "lt-g1");
		LtReadLegend(rs, "g2", "lt-g2");
		return rs;
	}
} // namespace

VW_PROBE("deferred-legend-tag", "凡例とタグを後で読み直す", "凡例の中身とタグの外接を後で測り直す")
{
	const std::vector<DeferredCase> cases = {
		{"tag", "データタグ: 挿入点に留まるか・実寸・MoveObject 後の外接", &LtSetupTag,
		 &LtMeasureTag},
		{"lg", "グラフィック凡例: ソース無し / 'GrLe' / ResetObject なし の中身と凡例イメージ",
		 &LtSetupLegend, &LtMeasureLegend}};
	RunDeferred(probe, "deferred-legend-tag", cases, false, true);
}
