//
//	probes/runtime/deferred-dimension/probe.cpp
//
//	[issue #245] 寸法の「書いても反映されない／古いまま／届かない」を、読む時刻を 3 つに
//	分けて測り直す。これまでの結論はどれも「書いた直後に同じ呼び出しの中で読んだ」もので、
//	Vectorworks へ制御を返した後に読んだことは無い。
//
//	  T0 = 書いた直後（同じ呼び出しの中）
//	  T1 = 呼び出しの中でランループを 1.5 秒回した後
//	  T2 = 2 回目の実行（VW のイベントループを通った後）
//
//	ケース（どれも Findings「Dimensions」の既存の結論の測り直し）:
//	  c1a〜c1d  連続寸法（型 86）の文字の大きさ（#164 の H0 / K1 / K2）。
//	            型 86 へ SetTextStyleRef（ResetObject 無し／有り）、中の型 63 へ
//	            ovDimFontSize（ResetObject 無し／有り）。中の 2D 表現のグループ（型 11）も読む。
//	  c2        by-class の寸法と、クラスの文字スタイルの差し替え（#157）。
//	  c3        寸法規格が指す文字スタイルの大きさを変える（#163 ④a）。
//	  c4        作った直後の直線寸法に描かれた値を読めるか（#134）。
//	  c5        関連付けた寸法と MoveObject + ResetObject（#134 の経路。#138）。
//
//	図形は名前で引き直す（T2 は別の実行なのでハンドルを持ち越さない）。
//	文字スタイル・クラス・寸法規格は、仕立てが開いた新規図面の中（各ケースの setup）で作る。
//

#include "Deferred.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
	// ---------------------------------------------------------------------------
	// 小道具

	bool DmGetReal(MCObjectHandle h, short selector, double& out)
	{
		TVariableBlock block;
		if (h == nullptr || !gSDK->GetObjectVariable(h, selector, block))
			return false;
		Real64 value = 0.0;
		if (!block.GetReal64(value))
			return false;
		out = value;
		return true;
	}

	bool DmSetReal(MCObjectHandle h, short selector, double value)
	{
		if (h == nullptr)
			return false;
		TVariableBlock block;
		block = static_cast<Real64>(value);
		return gSDK->SetObjectVariable(h, selector, block) != 0;
	}

	bool DmSetBool(MCObjectHandle h, short selector, bool value)
	{
		if (h == nullptr)
			return false;
		TVariableBlock block;
		block = static_cast<Boolean>(value ? 1 : 0);
		return gSDK->SetObjectVariable(h, selector, block) != 0;
	}

	std::string DmRealStr(MCObjectHandle h, short selector)
	{
		double v = 0.0;
		if (!DmGetReal(h, selector, v))
			return "(読めない)";
		return DfNum(v);
	}

	std::string DmBoolVar(MCObjectHandle h, short selector)
	{
		TVariableBlock block;
		if (h == nullptr || !gSDK->GetObjectVariable(h, selector, block))
			return "(読めない)";
		bool b = false;
		if (block.GetBoolean(b))
			return DfBool(b);
		return "(型違い)";
	}

	std::string DmPtStr(MCObjectHandle h, short selector)
	{
		TVariableBlock block;
		if (h == nullptr || !gSDK->GetObjectVariable(h, selector, block))
			return "(読めない)";
		WorldPt pt;
		if (!block.GetWorldPt(pt))
			return "(型違い)";
		return "(" + DfNum(pt.x, 1) + "," + DfNum(pt.y, 1) + ")";
	}

	std::string DmIndexName(InternalIndex index)
	{
		if (index == 0)
			return "";
		TXString name;
		gSDK->InternalIndexToNameN(index, name);
		return static_cast<const char*>(name);
	}

	// 文字スタイルの当たり方: "ref(名前)" と by-class なら " cls"。
	std::string DmStyleStr(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		const InternalIndex ref = gSDK->GetTextStyleRef(h);
		return std::to_string(static_cast<long long>(ref)) + "(" + DmIndexName(ref) + ")" +
			   (gSDK->GetTextStyleByClass(h) ? " cls" : "");
	}

	// 名前の付いた資源（文字スタイルなど）の InternalIndex。無ければ 0。
	InternalIndex DmIndexOf(const std::string& name)
	{
		MCObjectHandle h = DfFind(name);
		return h != nullptr ? gSDK->GetObjectInternalIndex(h) : 0;
	}

	// 容れ物の直下から、ある型の図形を集める（終端 0 で打ち切る）。
	std::vector<MCObjectHandle> DmMembers(MCObjectHandle container, short wanted)
	{
		std::vector<MCObjectHandle> out;
		if (container == nullptr)
			return out;
		size_t guard = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nullptr;
			 m = gSDK->NextObject(m))
		{
			if (++guard > 200)
				break;
			const short type = gSDK->GetObjectTypeN(m);
			if (type == 0)
				break;
			if (type == wanted)
				out.push_back(m);
		}
		return out;
	}

	// 中に描かれている文字図形（型 10）を全部拾う（深さ 5 段まで）。
	struct DmDrawnText
	{
		double size = 0.0;
		std::string text;
	};

	void DmWalkText(MCObjectHandle container, int depth, std::vector<DmDrawnText>& out)
	{
		if (container == nullptr || depth > 5)
			return;
		size_t guard = 0;
		for (MCObjectHandle m = gSDK->FirstMemberObj(container); m != nullptr;
			 m = gSDK->NextObject(m))
		{
			if (++guard > 500 || out.size() > 50)
				break;
			const short type = gSDK->GetObjectTypeN(m);
			if (type == 0)
				break; // kTermNode（walk の終端）
			if (type == kTextNode)
			{
				DmDrawnText t;
				WorldCoord charSize = 0;
				gSDK->GetTextSize(m, 1, charSize);
				t.size = static_cast<double>(charSize);
				const TXString chars = gSDK->GetTextChars(m);
				t.text = static_cast<const char*>(chars);
				out.push_back(t);
				continue;
			}
			DmWalkText(m, depth + 1, out);
		}
	}

	// "n=2 2.1167'1,000' 2.1167'1,000'"（先頭 2 件まで）。
	std::string DmTextStr(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		std::vector<DmDrawnText> texts;
		DmWalkText(h, 0, texts);
		std::string s = "n=" + std::to_string(texts.size());
		for (size_t i = 0; i < texts.size() && i < 2; ++i)
			s += " " + DfNum(texts[i].size) + "'" + DfEscape(texts[i].text) + "'";
		return s;
	}

	// 直線寸法（型 63）1 本ぶんの欄。
	void DmDimRead(DeferredReadings& rs, const std::string& prefix, MCObjectHandle dim)
	{
		if (dim == nullptr)
		{
			rs.emplace_back(prefix + "dim", "(nil)");
			return;
		}
		rs.emplace_back(prefix + "style", DmStyleStr(dim));
		rs.emplace_back(prefix + "font", DmRealStr(dim, ovDimFontSize));
		rs.emplace_back(prefix + "txt", DmTextStr(dim));
		rs.emplace_back(prefix + "bnd", DfBounds(dim));
	}

	// 連続寸法（型 86）を名前で引き、本体・直下の型 63・2D 表現のグループ（型 11）を分けて読む。
	DeferredReadings DmChainRead(const std::string& name)
	{
		DeferredReadings rs;
		MCObjectHandle chain = DfFind(name);
		if (chain == nullptr)
		{
			rs.emplace_back("86", "(見つからない)");
			return rs;
		}
		rs.emplace_back("86.type", std::to_string(gSDK->GetObjectTypeN(chain)));
		rs.emplace_back("86.style", DmStyleStr(chain));
		rs.emplace_back("86.font", DmRealStr(chain, ovDimFontSize));
		rs.emplace_back("86.bnd", DfBounds(chain));
		rs.emplace_back("86.kids", DfChildren(chain));
		const std::vector<MCObjectHandle> dims = DmMembers(chain, dimHeaderNode);
		for (size_t i = 0; i < dims.size() && i < 2; ++i)
		{
			const std::string p = "63[" + std::to_string(i) + "].";
			rs.emplace_back(p + "font", DmRealStr(dims[i], ovDimFontSize));
			rs.emplace_back(p + "txt", DmTextStr(dims[i]));
		}
		const std::vector<MCObjectHandle> groups = DmMembers(chain, kGroupNode);
		for (size_t g = 0; g < groups.size() && g < 2; ++g)
		{
			const std::string p = "11[" + std::to_string(g) + "].";
			rs.emplace_back(p + "bnd", DfBounds(groups[g]));
			rs.emplace_back(p + "txt", DmTextStr(groups[g]));
		}
		return rs;
	}

	void DmLogPre(vwprobe::Report& probe, const std::string& label, const DeferredReadings& rs)
	{
		for (const auto& [k, v] : rs)
			probe.log("  前 " + label + " " + k + " = " + v);
	}

	std::string DmActiveScale()
	{
		MCObjectHandle layer = gSDK->GetActiveLayer();
		if (layer == nullptr)
			return "(アクティブレイヤが無い)";
		double_gs scale = 0.0;
		gSDK->GetLayerScaleN(layer, scale);
		return "1/" + DfNum(static_cast<double>(scale), 2);
	}

	MCObjectHandle DmMakeDim(double x0, double x1, double y, bool showValue)
	{
		MCObjectHandle dim = gSDK->CreateLinearDimension(
			WorldPt(x0, y), WorldPt(x1, y), static_cast<WorldCoord>(500.0), 0, Vector2(0, 0), 0);
		if (dim != nullptr && showValue)
			DmSetBool(dim, ovDimShowValue, true);
		return dim;
	}

	// 端点を共有する直線寸法 2 本を作って繋ぎ、名前を付ける。
	MCObjectHandle DmMakeChain(vwprobe::Report& probe, double y, const std::string& name)
	{
		MCObjectHandle a = DmMakeDim(0.0, 1000.0, y, true);
		MCObjectHandle b = DmMakeDim(1000.0, 2000.0, y, true);
		if (a == nullptr || b == nullptr)
		{
			probe.fail("CreateLinearDimension が nil（" + name + "）");
			return nullptr;
		}
		MCObjectHandle chain = gSDK->CreateChainDimension(a, b);
		if (chain == nullptr)
		{
			probe.fail("CreateChainDimension が nil（" + name + "）");
			return nullptr;
		}
		return DfSetName(probe, chain, name) ? chain : nullptr;
	}

	MCObjectHandle DmMakeNamedDim(vwprobe::Report& probe, double x0, double x1, double y,
								  bool showValue, const std::string& name)
	{
		MCObjectHandle dim = DmMakeDim(x0, x1, y, showValue);
		if (dim == nullptr)
		{
			probe.fail("CreateLinearDimension が nil（" + name + "）");
			return nullptr;
		}
		return DfSetName(probe, dim, name) ? dim : nullptr;
	}

	// 文字スタイルを作って大きさ（pt）を書く（ovTextStyleSize の単位はインチ）。
	InternalIndex DmMakeTextStyle(vwprobe::Report& probe, const std::string& name, double pt)
	{
		MCObjectHandle style = DfFind(name);
		if (style == nullptr)
			style = gSDK->CreateTextStyleResource(TXString(name.c_str()));
		if (style == nullptr)
		{
			probe.fail("CreateTextStyleResource が nil: " + name);
			return 0;
		}
		DmSetReal(style, ovTextStyleSize, pt / 72.0);
		const InternalIndex ref = gSDK->GetObjectInternalIndex(style);
		probe.log("  文字スタイル " + name + " ref=" + std::to_string(static_cast<long long>(ref)) +
				  " ovTextStyleSize=" + DmRealStr(style, ovTextStyleSize) + "inch");
		return ref;
	}

	// 規格を名前で探す（総当り）。
	bool DmFindStandard(const std::string& wanted, short& outIndex)
	{
		for (short index = 9; index >= -30; --index)
		{
			TVariableBlock block;
			if (!gSDK->GetDimensionStandardVariable(index, dimStdstandardName, block))
				continue;
			TXString name;
			if (!block.GetTXString(name))
				continue;
			if (std::string(static_cast<const char*>(name)) == wanted)
			{
				outIndex = index;
				return true;
			}
		}
		return false;
	}

	std::string DmStandardStyleStr(const std::string& standardName)
	{
		short index = 0;
		if (!DmFindStandard(standardName, index))
			return "(規格が無い)";
		TVariableBlock block;
		if (!gSDK->GetDimensionStandardVariable(index, dimStdTextStyle, block))
			return "(読めない)";
		Sint32 ref = 0;
		if (!block.GetSint32(ref))
			return "(型違い)";
		return std::to_string(ref) + "(" + DmIndexName(static_cast<InternalIndex>(ref)) + ")";
	}

	// ---------------------------------------------------------------------------
	// 名前（どれも 63 文字より十分短い）

	const char* const kDmTs1 = "dm-ts1";   // c1: 24pt
	const char* const kDmTs2 = "dm-ts2";   // c2: 30pt（クラスへ後から当てる）
	const char* const kDmTs2x = "dm-ts2x"; // c2: 9pt（クラス b が最初に持つ）
	const char* const kDmTs3 = "dm-ts3";   // c3: 6pt → 24pt
	const char* const kDmCl2a = "dm-cl2a"; // c2: 最初は文字スタイル無し（#157 の再現）
	const char* const kDmCl2b = "dm-cl2b"; // c2: 最初から dm-ts2x
	const char* const kDmStd3 = "dm-std3"; // c3: カスタム寸法規格
	const double kDmInnerWrite = 12.7;	   // c1c / c1d で中の型 63 へ書く ovDimFontSize
	const double kDmMove = 1200.0;		   // c5 で線A を動かす量

	// ---------------------------------------------------------------------------
	// c1: 連続寸法（#164）

	enum class DmChainOp
	{
		StyleOnChain,
		InnerFontSize
	};

	void DmSetupChain(vwprobe::Report& probe, const std::string& name, double y, DmChainOp op,
					  bool reset)
	{
		const InternalIndex ts1 =
			DmIndexOf(kDmTs1) != 0 ? DmIndexOf(kDmTs1) : DmMakeTextStyle(probe, kDmTs1, 24.0);
		probe.log("  アクティブレイヤの縮尺 " + DmActiveScale());
		MCObjectHandle chain = DmMakeChain(probe, y, name);
		if (chain == nullptr)
			return;
		DmLogPre(probe, name, DmChainRead(name));
		if (op == DmChainOp::StyleOnChain)
		{
			gSDK->SetTextStyleRef(chain, ts1);
			probe.log("  SetTextStyleRef(型 86, " + std::string(kDmTs1) + ")");
		}
		else
		{
			for (MCObjectHandle dim : DmMembers(chain, dimHeaderNode))
				probe.log("  中の型 63 へ ovDimFontSize ← " + DfNum(kDmInnerWrite) + " → " +
						  DfBool(DmSetReal(dim, ovDimFontSize, kDmInnerWrite)));
		}
		if (reset)
			probe.log("  ResetObject(型 86) → " + DfBool(gSDK->ResetObject(chain) != 0));
		else
			probe.log("  ResetObject は呼ばない");
	}

	// ---------------------------------------------------------------------------
	// c2: by-class（#157）

	InternalIndex DmClassIndex(const std::string& name)
	{
		InternalIndex index = 0;
		if (!gSDK->NameToInternalIndexN(TXString(name.c_str()), index))
			return 0;
		return index;
	}

	std::string DmClassStr(const std::string& name)
	{
		const InternalIndex cls = DmClassIndex(name);
		if (cls == 0)
			return "(クラスが無い)";
		const InternalIndex ref = gSDK->GetClTextStyleRef(cls);
		return "use=" + DfBool(gSDK->GetClUseTextStyle(cls)) +
			   " ref=" + std::to_string(static_cast<long long>(ref)) + "(" + DmIndexName(ref) + ")";
	}

	void DmSetupByClass(vwprobe::Report& probe)
	{
		const InternalIndex ts2 = DmMakeTextStyle(probe, kDmTs2, 30.0);
		const InternalIndex ts2x = DmMakeTextStyle(probe, kDmTs2x, 9.0);
		const InternalIndex clA = gSDK->AddClass(TXString(kDmCl2a));
		const InternalIndex clB = gSDK->AddClass(TXString(kDmCl2b));
		if (ts2 == 0 || ts2x == 0 || clA == 0 || clB == 0)
		{
			probe.fail("文字スタイルかクラスを作れなかった");
			return;
		}
		gSDK->SetClUseTextStyle(clB, true);
		gSDK->SetClTextStyleRef(clB, ts2x);
		probe.log("  クラス a: " + DmClassStr(kDmCl2a) + " / b: " + DmClassStr(kDmCl2b));

		struct Row
		{
			const char* name;
			InternalIndex cls;
			double y;
		};
		const Row rows[] = {{"dm-c2a0", clA, 30000.0},
							{"dm-c2a1", clA, 32000.0},
							{"dm-c2b0", clB, 34000.0},
							{"dm-c2b1", clB, 36000.0}};
		for (const Row& r : rows)
		{
			MCObjectHandle dim = DmMakeNamedDim(probe, 0.0, 1000.0, r.y, true, r.name);
			if (dim == nullptr)
				continue;
			gSDK->SetObjectClass(dim, r.cls);
			gSDK->SetTextStyleByClass(dim);
			DeferredReadings pre;
			DmDimRead(pre, "", dim);
			DmLogPre(probe, r.name, pre);
		}

		// クラスの文字スタイルを差し替える（a: 無し → ts2、b: ts2x → ts2）。
		gSDK->SetClUseTextStyle(clA, true);
		gSDK->SetClTextStyleRef(clA, ts2);
		gSDK->SetClTextStyleRef(clB, ts2);
		probe.log("  差し替えた: クラス a: " + DmClassStr(kDmCl2a) +
				  " / b: " + DmClassStr(kDmCl2b));
		// 末尾 1 の 2 本だけ ResetObject。
		for (const char* name : {"dm-c2a1", "dm-c2b1"})
			probe.log("  ResetObject(" + std::string(name) + ") → " +
					  DfBool(gSDK->ResetObject(DfFind(name)) != 0));
	}

	DeferredReadings DmMeasureByClass()
	{
		DeferredReadings rs;
		rs.emplace_back("clA", DmClassStr(kDmCl2a));
		rs.emplace_back("clB", DmClassStr(kDmCl2b));
		for (const char* name : {"dm-c2a0", "dm-c2a1", "dm-c2b0", "dm-c2b1"})
			DmDimRead(rs, std::string(name).substr(3) + ".", DfFind(name));
		return rs;
	}

	// ---------------------------------------------------------------------------
	// c3: 規格の文字スタイルの大きさ（#163 ④a）

	void DmSetupStandard(vwprobe::Report& probe)
	{
		const InternalIndex ts3 = DmMakeTextStyle(probe, kDmTs3, 6.0);
		if (ts3 == 0)
			return;
		(void)gSDK->CreateCustomDimensionStandard(TXString(kDmStd3));
		short stdIndex = 0;
		if (!DmFindStandard(kDmStd3, stdIndex))
		{
			probe.fail("作ったカスタム規格が見つからない");
			return;
		}
		TVariableBlock link;
		link = static_cast<Sint32>(ts3);
		probe.log(
			"  規格 index=" + std::to_string(stdIndex) +
			" SetCustomDimensionStandardVariable(dimStdTextStyle) → " +
			DfBool(gSDK->SetCustomDimensionStandardVariable(stdIndex, dimStdTextStyle, link) != 0) +
			" 読み戻し=" + DmStandardStyleStr(kDmStd3));

		short docStd = -99;
		gSDK->GetProgramVariable(varDimStandard, &docStd);
		short newStd = stdIndex;
		probe.log("  varDimStandard " + std::to_string(docStd) + " ← " + std::to_string(stdIndex) +
				  " → " + DfBool(gSDK->SetProgramVariable(varDimStandard, &newStd) != 0));

		MCObjectHandle chainA = DmMakeChain(probe, 40000.0, "dm-c3a");
		MCObjectHandle chainB = DmMakeChain(probe, 42000.0, "dm-c3b");
		MCObjectHandle single = DmMakeNamedDim(probe, 0.0, 1000.0, 44000.0, true, "dm-c3s");
		MCObjectHandle explicitA = DmMakeNamedDim(probe, 0.0, 1000.0, 46000.0, true, "dm-c3x");
		MCObjectHandle explicitB = DmMakeNamedDim(probe, 0.0, 1000.0, 48000.0, true, "dm-c3y");
		if (explicitA != nullptr)
			gSDK->SetTextStyleRef(explicitA, ts3);
		if (explicitB != nullptr)
			gSDK->SetTextStyleRef(explicitB, ts3);

		// 文書の既定は戻しておく（後のケースの寸法に効かないように）。
		gSDK->SetProgramVariable(varDimStandard, &docStd);

		DmLogPre(probe, "dm-c3a", DmChainRead("dm-c3a"));
		for (const char* name : {"dm-c3s", "dm-c3x"})
		{
			DeferredReadings pre;
			DmDimRead(pre, "", DfFind(name));
			DmLogPre(probe, name, pre);
		}

		// 規格が指している文字スタイルの大きさを 6pt → 24pt に変える。
		MCObjectHandle style = DfFind(kDmTs3);
		probe.log("  " + std::string(kDmTs3) + " の ovTextStyleSize ← 24/72 → " +
				  DfBool(DmSetReal(style, ovTextStyleSize, 24.0 / 72.0)) + " 読み戻し " +
				  DmRealStr(style, ovTextStyleSize));
		// ResetObject を呼ぶのは dm-c3b と dm-c3y だけ。
		if (chainB != nullptr)
			probe.log("  ResetObject(dm-c3b) → " + DfBool(gSDK->ResetObject(chainB) != 0));
		if (explicitB != nullptr)
			probe.log("  ResetObject(dm-c3y) → " + DfBool(gSDK->ResetObject(explicitB) != 0));
		(void)chainA;
		(void)single;
	}

	DeferredReadings DmMeasureStandard()
	{
		DeferredReadings rs;
		rs.emplace_back("ts3.size", DmRealStr(DfFind(kDmTs3), ovTextStyleSize));
		rs.emplace_back("std.style", DmStandardStyleStr(kDmStd3));
		for (const char* name : {"dm-c3a", "dm-c3b"})
		{
			const std::string p = std::string(name).substr(3) + ".";
			MCObjectHandle chain = DfFind(name);
			rs.emplace_back(p + "86.style", DmStyleStr(chain));
			const std::vector<MCObjectHandle> dims = DmMembers(chain, dimHeaderNode);
			if (dims.empty())
				rs.emplace_back(p + "63", "(無い)");
			else
			{
				rs.emplace_back(p + "63.style", DmStyleStr(dims[0]));
				rs.emplace_back(p + "63.font", DmRealStr(dims[0], ovDimFontSize));
				rs.emplace_back(p + "63.txt", DmTextStr(dims[0]));
			}
			const std::vector<MCObjectHandle> groups = DmMembers(chain, kGroupNode);
			if (!groups.empty())
				rs.emplace_back(p + "11.txt", DmTextStr(groups[0]));
		}
		for (const char* name : {"dm-c3s", "dm-c3x", "dm-c3y"})
			DmDimRead(rs, std::string(name).substr(3) + ".", DfFind(name));
		return rs;
	}

	// ---------------------------------------------------------------------------
	// c4: 描かれた値を読めるか（#134）

	void DmSetupDrawnValue(vwprobe::Report& probe)
	{
		// c4: 作ったまま（ovDimShowValue も触らない）／c4v: ovDimShowValue だけ立てる／
		// c4r: 立てて ResetObject。長さを変えて、出た値が行の名前になるようにする。
		(void)DmMakeNamedDim(probe, 0.0, 1100.0, 50000.0, false, "dm-c4");
		(void)DmMakeNamedDim(probe, 0.0, 1200.0, 52000.0, true, "dm-c4v");
		MCObjectHandle r = DmMakeNamedDim(probe, 0.0, 1300.0, 54000.0, true, "dm-c4r");
		if (r != nullptr)
			probe.log("  ResetObject(dm-c4r) → " + DfBool(gSDK->ResetObject(r) != 0));
	}

	DeferredReadings DmMeasureDrawnValue()
	{
		DeferredReadings rs;
		for (const char* name : {"dm-c4", "dm-c4v", "dm-c4r"})
		{
			const std::string p = std::string(name).substr(3) + ".";
			MCObjectHandle dim = DfFind(name);
			rs.emplace_back(p + "show", DmBoolVar(dim, ovDimShowValue));
			rs.emplace_back(p + "kids", DfChildren(dim));
			rs.emplace_back(p + "txt", DmTextStr(dim));
			rs.emplace_back(p + "font", DmRealStr(dim, ovDimFontSize));
			rs.emplace_back(p + "bnd", DfBounds(dim));
		}
		return rs;
	}

	// ---------------------------------------------------------------------------
	// c5: 関連付けた寸法（#134 の経路: MoveObject + ResetObject だけ）

	void DmSetupAssociation(vwprobe::Report& probe)
	{
		union
		{
			Boolean asBoolean;
			Sint32 asWide;
		} pref;
		pref.asWide = 0;
		gSDK->GetProgramVariable(varAssociateDims, &pref);
		const Boolean startPref = pref.asBoolean;
		probe.log("  varAssociateDims(28) = " + std::string(startPref ? "1" : "0"));
		if (!startPref)
		{
			Boolean on = 1;
			gSDK->SetProgramVariable(varAssociateDims, &on);
		}

		const double y = 60000.0;
		MCObjectHandle lineA = gSDK->CreateLine(WorldPt(0.0, y), WorldPt(0.0, y - 500.0));
		MCObjectHandle lineB = gSDK->CreateLine(WorldPt(3000.0, y), WorldPt(3000.0, y - 500.0));
		MCObjectHandle dim =
			gSDK->CreateLinearDimension(WorldPt(0.0, y), WorldPt(3000.0, y),
										static_cast<WorldCoord>(600.0), 0, Vector2(0, 0), 0);
		if (lineA == nullptr || lineB == nullptr || dim == nullptr)
		{
			probe.fail("c5 の 1 組を作れなかった");
		}
		else
		{
			DmSetBool(dim, ovDimShowValue, true);
			// 関連付ける相手は**選択で渡す**（旧プローブ dim-association の手順。選ばずに
			// 呼ぶと関連付けが成立しない——#245 の 1 巡目で HasConstraint=no だった）。
			gSDK->DeselectAll();
			gSDK->SelectObject(lineA, true);
			gSDK->SelectObject(lineB, true);
			gSDK->AssociateLinearDimension(dim, false);
			gSDK->DeselectAll();
			DfSetName(probe, lineA, "dm-c5la");
			DfSetName(probe, lineB, "dm-c5lb");
			DfSetName(probe, dim, "dm-c5d");
			probe.log(
				"  関連付けた後 HasConstraint(寸法)=" + DfBool(gSDK->HasConstraint(dim) != 0) +
				" 測点 " + DmPtStr(dim, ovDimStartPt) + "→" + DmPtStr(dim, ovDimEndPt));
		}

		if (!startPref)
		{
			Boolean back = 0;
			gSDK->SetProgramVariable(varAssociateDims, &back);
			probe.log("  varAssociateDims を 0 へ戻した");
		}
		if (lineA == nullptr || dim == nullptr)
			return;

		gSDK->MoveObject(lineA, static_cast<WorldCoord>(kDmMove), 0);
		probe.log("  MoveObject(線A, +" + DfNum(kDmMove, 0) + ", 0) → ResetObject(寸法) " +
				  DfBool(gSDK->ResetObject(dim) != 0) + "（UpdateConstraintModel は呼ばない）");
	}

	DeferredReadings DmMeasureAssociation()
	{
		DeferredReadings rs;
		MCObjectHandle dim = DfFind("dm-c5d");
		MCObjectHandle lineA = DfFind("dm-c5la");
		WorldRect r;
		rs.emplace_back("lineA.x", lineA != nullptr && gSDK->GetObjectBounds(lineA, r)
									   ? DfNum(static_cast<double>(r.Left()), 1)
									   : std::string("(読めない)"));
		rs.emplace_back("start", DmPtStr(dim, ovDimStartPt));
		rs.emplace_back("end", DmPtStr(dim, ovDimEndPt));
		rs.emplace_back("hasCons", dim != nullptr ? DfBool(gSDK->HasConstraint(dim) != 0)
												  : std::string("(nil)"));
		rs.emplace_back("txt", DmTextStr(dim));
		rs.emplace_back("bnd", DfBounds(dim));
		return rs;
	}
} // namespace

VW_PROBE("deferred-dimension", "寸法: 書いた直後／ランループ後／制御を返した後で読み比べる",
		 "連続寸法・by-class・規格の文字スタイル・描かれた値・関連付けを T0/T1/T2 で測り直す")
{
	std::vector<DeferredCase> cases;

	cases.push_back(
		{"c1a", "連続寸法: 型 86 へ SetTextStyleRef（ResetObject 無し。#164 H0 の 2 列目）",
		 [](vwprobe::Report& p) { DmSetupChain(p, "dm-c1a", 0.0, DmChainOp::StyleOnChain, false); },
		 [] { return DmChainRead("dm-c1a"); }});
	cases.push_back({"c1b", "連続寸法: 型 86 へ SetTextStyleRef ＋ ResetObject（#164 H0 の対照）",
					 [](vwprobe::Report& p)
					 { DmSetupChain(p, "dm-c1b", 4000.0, DmChainOp::StyleOnChain, true); },
					 [] { return DmChainRead("dm-c1b"); }});
	cases.push_back({"c1c", "連続寸法: 中の型 63 へ ovDimFontSize（ResetObject 無し。#164 K2）",
					 [](vwprobe::Report& p)
					 { DmSetupChain(p, "dm-c1c", 8000.0, DmChainOp::InnerFontSize, false); },
					 [] { return DmChainRead("dm-c1c"); }});
	cases.push_back({"c1d", "連続寸法: 中の型 63 へ ovDimFontSize ＋ ResetObject（#164 K1）",
					 [](vwprobe::Report& p)
					 { DmSetupChain(p, "dm-c1d", 12000.0, DmChainOp::InnerFontSize, true); },
					 [] { return DmChainRead("dm-c1d"); }});
	cases.push_back({"c2",
					 "by-class の寸法: クラスの文字スタイルを差し替える（#157。末尾 1 だけ "
					 "ResetObject）",
					 [](vwprobe::Report& p) { DmSetupByClass(p); },
					 [] { return DmMeasureByClass(); }});
	cases.push_back({"c3",
					 "規格が指す文字スタイルの大きさを 6pt → 24pt（#163 ④a。c3b / c3y だけ "
					 "ResetObject）",
					 [](vwprobe::Report& p) { DmSetupStandard(p); },
					 [] { return DmMeasureStandard(); }});
	cases.push_back({"c4", "作った直後の直線寸法に描かれた値（#134。c4r だけ ResetObject）",
					 [](vwprobe::Report& p) { DmSetupDrawnValue(p); },
					 [] { return DmMeasureDrawnValue(); }});
	cases.push_back({"c5", "関連付けた寸法: MoveObject ＋ ResetObject だけ（#134 の経路・#138）",
					 [](vwprobe::Report& p) { DmSetupAssociation(p); },
					 [] { return DmMeasureAssociation(); }});

	RunDeferred(probe, "deferred-dimension", cases);
}
