//
//	probes/runtime/section-vp-1050/probe.cpp
//
//	[issue #151] 「SDK 製の断面ビューポートは `UpdateViewport` のたびにビュー行列
//	（`ovViewportViewMatrix` ＝ 1050）が単位行列へ戻る」——その理由を突き止める調査。
//
//	**1 回目の実測（実物件の図面）で、見立てが変わった。** 表示レイヤとクラスを表示へ倒して
//	から更新すると、**SDK 製でも 1050 は更新のときに 1055 と同じ値へ自分で入り、以後の更新でも
//	保たれた**（空図面では単位行列のままだった）。つまり 1050 は**断面の描画結果から引き直される
//	派生値**で、「戻る」のは**描くものが無かったとき**ではないか。このプローブはそれを
//	A/B で確定させる。
//
//	  1. 図面にある断面ビューポートを並べ、1050・1007・注釈のレベル基準線（と `Elev`）を読む。
//	     **高さが 0 でない**レベル基準線を 1 本選び、それを基準（＝答え合わせの正解）にする。
//	  2. 既存の断面ビューポートを更新したら 1050 と `Elev` はどうなるか。あわせて
//	     「ラベルを動かす・書き換える・線の太さを変える・作り直す」で高さが落ちるかを測る。
//	  3. **A/B/C**: 同じ座標で SDK 製の断面ビューポートを 3 枚作って更新し、1050 を比べる。
//	       A … 表示レイヤ・クラスをいじらない（既定のまま）
//	       B … 表示レイヤ・クラスを表示へ倒す
//	       C … B と同じに倒すが、断面線を建物から遠くへ引く（切るものが無い）
//	     さらに B を「全レイヤ非表示にして再更新」する（D）。**A と C と D が単位行列で
//	     B だけが 1055 と一致するなら、引き直しの素は「描けた断面」だと確定する。**
//	  4. B の注釈へレベル基準線を置き、**1050 へ何も写さずに** `ResetObject` して `Elev` を読む。
//	     正解（1 で選んだ高さ）と一致すれば、**「1055 を 1050 へ写す」手順は要らない**。
//	     そのあと移動・欄の書き換え・クラス・線の太さ・作り直しで落ちないかを測る。
//	  5. 断面線（645）が図面のどこにいるか（デザインレイヤか、ビューポートの断面群の中か）。
//
//	**実物件の図面（断面ビューポートと、高さが 0 でないレベル基準線があるもの）で走らせる。**
//	試験用のシートレイヤと断面ビューポートを足し、既存のビューポートも更新するので、
//	**走らせた後は保存しない**。
//

#include "Probe.h"

#include "VectorWorks/Extension/IMarkersPluginSupport.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace
{
	// オブジェクト変数の総当りの範囲（#147 と同じ幅）。
	const short kProbeOvFirst = 1000;
	const short kProbeOvLast = 1150;

	// 見るキャッシュ群（EViewportGroupType。1〜15）。
	const short kProbeGroupFirst = 1;
	const short kProbeGroupLast = 15;

	// レベル基準線（現行のツール）。Findings「レベル（標高）オブジェクト」参照。
	const char* const kProbeBenchmarkName = "Elevation Benchmark2";

	// -------------------------------------------------------------------------
	std::string ProbeFormatNumber(double value)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.4g", value);
		return std::string(buf);
	}

	std::string ProbeFormatInt(long long value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%lld", value);
		return std::string(buf);
	}

	// 行列を 12 個の数値へ畳む（比較できる形にするだけ。読みやすさより一意性）。
	std::string ProbeMatrixDigest(const TransformMatrix& matrix)
	{
		std::string out = "[";
		for (int row = 0; row < 4; ++row)
		{
			for (int col = 0; col < 3; ++col)
			{
				if (row != 0 || col != 0)
					out += " ";
				out += ProbeFormatNumber(matrix.mat[row][col]);
			}
			if (row != 3)
				out += " |";
		}
		out += "]";
		return out;
	}

	bool ProbeMatrixIsIdentity(const TransformMatrix& matrix)
	{
		for (int row = 0; row < 4; ++row)
		{
			for (int col = 0; col < 3; ++col)
			{
				const double expected = (row == col) ? 1.0 : 0.0;
				const double actual = matrix.mat[row][col];
				const double diff = (actual > expected) ? (actual - expected) : (expected - actual);
				if (diff > 1e-9)
					return false;
			}
		}
		return true;
	}

	// TVariableBlock を文字列へ。**型ごとの getter は型が合わないと false を返す**ので、
	// 順に試せば型名の列挙子に依存せずに済む。
	std::string ProbeDescribeBlock(const TVariableBlock& block)
	{
		std::string out = "t" + ProbeFormatInt(static_cast<long long>(block.GetType())) + ":";

		TransformMatrix matrix;
		if (block.GetTransformMatrix(matrix))
			return out + ProbeMatrixDigest(matrix);

		WorldRect rect;
		if (block.GetWorldRect(rect))
			return out + "rect(" + ProbeFormatNumber(rect.left) + "," +
				   ProbeFormatNumber(rect.top) + "," + ProbeFormatNumber(rect.right) + "," +
				   ProbeFormatNumber(rect.bottom) + ")";

		WorldPt3 pt3;
		if (block.GetWorldPt3(pt3))
			return out + "pt3(" + ProbeFormatNumber(pt3.X()) + "," + ProbeFormatNumber(pt3.Y()) +
				   "," + ProbeFormatNumber(pt3.Z()) + ")";

		WorldPt pt;
		if (block.GetWorldPt(pt))
			return out + "pt(" + ProbeFormatNumber(pt.x) + "," + ProbeFormatNumber(pt.y) + ")";

		Real64 real = 0.0;
		if (block.GetReal64(real))
			return out + ProbeFormatNumber(real);

		MCObjectHandle handle = nil;
		if (block.GetMCObjectHandle(handle))
			return out + (handle != nil ? "handle(有)" : "handle(nil)");

		Sint32 s32 = 0;
		if (block.GetSint32(s32))
			return out + ProbeFormatInt(s32);

		Uint32 u32 = 0;
		if (block.GetUint32(u32))
			return out + ProbeFormatInt(static_cast<long long>(u32));

		Sint16 s16 = 0;
		if (block.GetSint16(s16))
			return out + ProbeFormatInt(s16);

		Sint8 s8 = 0;
		if (block.GetSint8(s8))
			return out + ProbeFormatInt(s8);

		Uint8 u8 = 0;
		if (block.GetUint8(u8))
			return out + ProbeFormatInt(static_cast<long long>(u8));

		bool flag = false;
		if (block.GetBoolean(flag))
			return out + (flag ? "true" : "false");

		TXString text;
		if (block.GetTXString(text))
			return out + "\"" + std::string(static_cast<const char*>(text)) + "\"";

		return out + "?";
	}

	// 1 つのオブジェクト変数を読む。読めなければ空文字列。
	std::string ProbeReadVariable(MCObjectHandle object, short selector)
	{
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(object, selector, block))
			return std::string();
		return ProbeDescribeBlock(block);
	}

	// 1000〜1150 を一度に読む（索引は selector - kProbeOvFirst）。
	std::vector<std::string> ProbeSnapshotVariables(MCObjectHandle object)
	{
		std::vector<std::string> out;
		for (short selector = kProbeOvFirst; selector <= kProbeOvLast; ++selector)
			out.push_back(ProbeReadVariable(object, selector));
		return out;
	}

	// 2 つのスナップショットの差だけを出す。
	void ProbeLogVariableDiff(vwprobe::Report& probe, const std::string& what,
							  const std::vector<std::string>& before,
							  const std::vector<std::string>& after)
	{
		size_t changed = 0;
		for (size_t index = 0; index < before.size() && index < after.size(); ++index)
		{
			if (before[index] == after[index])
				continue;
			++changed;
			const short selector = static_cast<short>(kProbeOvFirst + index);
			probe.log("  " + what + " ov" + ProbeFormatInt(selector) + ": " +
					  (before[index].empty() ? std::string("(読めない)") : before[index]) + " -> " +
					  (after[index].empty() ? std::string("(読めない)") : after[index]));
		}
		probe.log("  " + what +
				  " 変わった欄の数 = " + ProbeFormatInt(static_cast<long long>(changed)));
	}

	// 個体 1 つを「型（PIO なら名前つき）」で呼ぶ。
	std::string ProbeDescribeObject(MCObjectHandle object)
	{
		if (object == nil)
			return "nil";
		const short type = gSDK->GetObjectTypeN(object);
		std::string out = "type" + ProbeFormatInt(type);
		if (type == kParametricNode)
		{
			VWParametricObj parametric(object);
			out +=
				"(" + std::string(static_cast<const char*>(parametric.GetParametricName())) + ")";
		}
		return out;
	}

	// 容れ物の直下を種類ごとに数える（何が入っているかを短く言うため）。
	std::string ProbeCensus(MCObjectHandle container)
	{
		if (container == nil)
			return "(群そのものが無い)";
		std::map<std::string, size_t> counts;
		size_t total = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(container); member != nil;
			 member = gSDK->NextObject(member))
		{
			++total;
			++counts[ProbeDescribeObject(member)];
			if (total >= 5000)
				break; // 実物件の図面で数千件入ることがあるので歯止め
		}
		if (total == 0)
			return "0 件（無）";
		std::string out = ProbeFormatInt(static_cast<long long>(total)) + " 件: ";
		bool first = true;
		for (std::map<std::string, size_t>::const_iterator it = counts.begin(); it != counts.end();
			 ++it)
		{
			if (!first)
				out += ", ";
			first = false;
			out += it->first + " x" + ProbeFormatInt(static_cast<long long>(it->second));
		}
		return out;
	}

	void ProbeLogGroups(vwprobe::Report& probe, const std::string& what, MCObjectHandle viewport)
	{
		for (short group = kProbeGroupFirst; group <= kProbeGroupLast; ++group)
		{
			MCObjectHandle groupHandle = gSDK->GetViewportGroup(viewport, group);
			if (groupHandle == nil)
				continue; // 無い群は並べない（読みやすさのため）
			probe.log("  " + what + " 群" + ProbeFormatInt(group) + " = " +
					  ProbeCensus(groupHandle));
		}
	}

	// 1050 / 1055 を読んで「単位行列か・一致するか」を言う。
	void ProbeLogMatrices(vwprobe::Report& probe, const std::string& what, MCObjectHandle viewport)
	{
		TVariableBlock block1050;
		TVariableBlock block1055;
		TransformMatrix matrix1050;
		TransformMatrix matrix1055;
		const bool got1050 = gSDK->GetObjectVariable(viewport, 1050, block1050) &&
							 block1050.GetTransformMatrix(matrix1050);
		const bool got1055 = gSDK->GetObjectVariable(viewport, 1055, block1055) &&
							 block1055.GetTransformMatrix(matrix1055);
		if (!got1050)
		{
			probe.log("  " + what + " 1050 を読めない");
			return;
		}
		std::string line = "  " + what + " 1050 = " + ProbeMatrixDigest(matrix1050) +
						   " 単位行列か=" + (ProbeMatrixIsIdentity(matrix1050) ? "はい" : "いいえ");
		if (got1055)
			line += " 1055 と一致=" +
					std::string(ProbeMatrixDigest(matrix1050) == ProbeMatrixDigest(matrix1055)
									? "はい"
									: "いいえ");
		probe.log(line);
	}

	// 1055 を 1050 へ写す（Findings「ビューポート」の現行手順）。
	bool ProbeCopy1055To1050(MCObjectHandle viewport)
	{
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(viewport, 1055, block))
			return false;
		return gSDK->SetObjectVariable(viewport, 1050, block) != 0;
	}

	// 図面のビューポートを全部集める（レイヤ直下だけ。ビューポートは入れ子にしない）。
	void ProbeCollectViewports(std::vector<MCObjectHandle>& outViewports,
							   std::vector<MCObjectHandle>& outDesignLayers)
	{
		std::vector<MCObjectHandle>* viewports = &outViewports;
		std::vector<MCObjectHandle>* designLayers = &outDesignLayers;
		gSDK->ForEachLayerN(
			[viewports, designLayers](MCObjectHandle layer)
			{
				TVariableBlock block;
				Sint16 layerType = 0;
				if (gSDK->GetObjectVariable(layer, 154 /* ovLayerType */, block))
					block.GetSint16(layerType);
				if (layerType == 1)
					designLayers->push_back(layer);
				for (MCObjectHandle member = gSDK->FirstMemberObj(layer); member != nil;
					 member = gSDK->NextObject(member))
				{
					if (gSDK->GetObjectTypeN(member) == kViewportNode)
						viewports->push_back(member);
				}
			});
	}

	bool ProbeIsSectionViewport(MCObjectHandle viewport)
	{
		TVariableBlock block;
		bool isSection = false;
		if (gSDK->GetObjectVariable(viewport, 1054 /* ovIsSectionViewport */, block))
			block.GetBoolean(isSection);
		return isSection;
	}

	bool ProbeIs1050Identity(MCObjectHandle viewport)
	{
		TVariableBlock block;
		TransformMatrix matrix;
		if (!gSDK->GetObjectVariable(viewport, 1050, block) || !block.GetTransformMatrix(matrix))
			return true;
		return ProbeMatrixIsIdentity(matrix);
	}

	std::string ProbeObjectName(MCObjectHandle object)
	{
		TXString name;
		gSDK->GetObjectName(object, name);
		return std::string(static_cast<const char*>(name));
	}

	// 注釈群から最初のレベル基準線を拾う（UI が置いた 3 つ組を借りるため）。
	struct ProbeBenchmarkTriple
	{
		TXString fStoryName;
		TXString fLevelTypeName;
		TXString fDatum;
		bool fFound = false;
	};

	// レベル基準線を注釈へ置いて 3 つ組を書くところまで（**作り直さない**）。
	// 1055 を 1050 へ写すのは「注釈を置き終えてから」なので、`ResetObject` は呼び手が
	// 写した後に呼ぶ（Findings「ビューポート」の現行手順）。
	std::string ProbePlaceBenchmark(MCObjectHandle viewport, const ProbeBenchmarkTriple& triple,
									double x, double y, MCObjectHandle& outMarker)
	{
		outMarker = nil;
		gSDK->DefineCustomObject(kProbeBenchmarkName, kCustomObjectPrefNever);
		MCObjectHandle marker = gSDK->CreateCustomObject(kProbeBenchmarkName, WorldPt(x, y), 0.0);
		if (marker == nil)
			return "CreateCustomObject が nil";
		if (!gSDK->AddViewportAnnotationObject(viewport, marker))
			return "AddViewportAnnotationObject が false";
		VWParametricObj parametric(marker);
		parametric.SetPointObjectPos(VWPoint2D(x, y));
		if (triple.fFound)
		{
			parametric.SetParamValue("__StoryName", triple.fStoryName);
			parametric.SetParamValue("__LevelTypeName", triple.fLevelTypeName);
			parametric.SetParamValue("Datum", triple.fDatum);
		}
		outMarker = marker;
		return "置けた";
	}

	std::string ProbeReadElev(MCObjectHandle marker)
	{
		if (marker == nil)
			return "(nil)";
		VWParametricObj parametric(marker);
		return std::string(static_cast<const char*>(parametric.GetParamValue("Elev")));
	}
	// 文字列の高さが「実質 0」かどうか。0 の高さを基準にすると、何を測っても 0 になって
	// 「落ちた」と「もともと 0」が見分けられない（1 回目の実測でそれを踏んだ）。
	bool ProbeElevIsZero(const std::string& elev)
	{
		return std::fabs(std::atof(elev.c_str())) < 1e-9;
	}

	// 注釈の中のレベル基準線から、**高さが 0 でないもの**を優先して 1 本返す
	// （最初に見つかる 1 本が GL＝0 のことがあり、それを基準にすると何も分からない）。
	MCObjectHandle ProbeFindBenchmarkPreferNonZero(MCObjectHandle viewport)
	{
		MCObjectHandle annotations =
			gSDK->GetViewportGroup(viewport, 2 /* kViewportGroupAnnotation */);
		if (annotations == nil)
			return nil;
		MCObjectHandle firstFound = nil;
		for (MCObjectHandle member = gSDK->FirstMemberObj(annotations); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) != kParametricNode)
				continue;
			VWParametricObj parametric(member);
			if (parametric.GetParametricName() != TXString(kProbeBenchmarkName))
				continue;
			if (firstFound == nil)
				firstFound = member;
			if (!ProbeElevIsZero(ProbeReadElev(member)))
				return member;
		}
		return firstFound;
	}

	// 断面ビューポートの下ごしらえ（Findings「ビューポート」の作法）。
	// `showAll` が false のときは表示レイヤ・クラスに触らない（A の対照）。
	void ProbeSetUpSectionViewport(vwprobe::Report& probe, const std::string& what,
								   MCObjectHandle viewport,
								   const std::vector<MCObjectHandle>& designLayers, bool showAll)
	{
		if (showAll)
		{
			for (size_t index = 0; index < designLayers.size(); ++index)
				gSDK->SetViewportLayerVisibility(viewport, designLayers[index], 0 /* 表示 */);
			size_t classCount = 0;
			size_t* classCounter = &classCount;
			MCObjectHandle target = viewport;
			gSDK->ForEachClass(true,
							   [target, classCounter](MCObjectHandle classHandle)
							   {
								   gSDK->SetViewportClassVisibility(
									   target, gSDK->GetObjectInternalIndex(classHandle),
									   0 /* 表示 */);
								   ++(*classCounter);
							   });
			probe.log("  " + what + " レイヤ " +
					  ProbeFormatInt(static_cast<long long>(designLayers.size())) + " 枚・クラス " +
					  ProbeFormatInt(static_cast<long long>(classCount)) + " 件を表示へ倒した");
		}
		else
		{
			probe.log("  " + what + " 表示レイヤ・クラスには触らない（既定のまま）");
		}
		{
			VWViewportObj viewportObj(viewport);
			viewportObj.SetRenderType(renderFinalHiddenLine);
		}
		TVariableBlock trueBlock;
		trueBlock = static_cast<Boolean>(true);
		gSDK->SetObjectVariable(viewport, 1064 /* 切断面より奥を表示 */, trueBlock);
	}

	// 「その個体を触ったら Elev が落ちるか」を一並びで測る。
	void ProbeLogElevDurability(vwprobe::Report& probe, const std::string& what,
								MCObjectHandle marker, MCObjectHandle viewport,
								const std::string& expected)
	{
		if (marker == nil)
			return;
		VWParametricObj parametric(marker);
		const VWPoint2D where = parametric.GetPointObjectPos();
		parametric.SetPointObjectPos(VWPoint2D(where.x + 100.0, where.y + 100.0));
		probe.log("  " + what + " ① ラベルを 100mm 動かした後の Elev = " + ProbeReadElev(marker));
		parametric.SetParamValue("EPfx", "+");
		probe.log("  " + what +
				  " ② 見た目の欄（EPfx）を書いた後の Elev = " + ProbeReadElev(marker));
		GS_SetLineWeight(gCBP, marker, 50);
		probe.log("  " + what + " ③ 線の太さを変えた後の Elev = " + ProbeReadElev(marker));
		const InternalIndex originalClass = gSDK->GetObjectClass(marker);
		InternalIndex otherClass = originalClass;
		InternalIndex* otherClassPtr = &otherClass;
		const InternalIndex* originalClassPtr = &originalClass;
		gSDK->ForEachClass(
			false,
			[otherClassPtr, originalClassPtr](MCObjectHandle classHandle)
			{
				const InternalIndex index = gSDK->GetObjectInternalIndex(classHandle);
				if (*otherClassPtr == *originalClassPtr && index != *originalClassPtr)
					*otherClassPtr = index;
			});
		if (otherClass != originalClass)
		{
			gSDK->SetObjectClass(marker, otherClass);
			probe.log("  " + what + " ④ クラスを変えた後の Elev = " + ProbeReadElev(marker));
		}
		gSDK->ResetObject(marker);
		probe.log("  " + what + " ⑤ ResetObject した後の Elev = " + ProbeReadElev(marker));
		gSDK->UpdateViewport(viewport);
		probe.log("  " + what + " ⑥ ビューポートを更新した後の Elev = " + ProbeReadElev(marker) +
				  "（1050 が単位行列か=" + (ProbeIs1050Identity(viewport) ? "はい" : "いいえ") +
				  "）");
		gSDK->ResetObject(marker);
		probe.log("  " + what + " ⑦ 更新の後に ResetObject した Elev = " + ProbeReadElev(marker) +
				  "  ← 正解は " + expected);
	}

} // namespace

VW_PROBE("section-vp-1050", "断面の 1050 が更新で戻る理由を測る",
		 "表示を倒してから更新すると 1050 が自分で入るのかを A/B で確かめ、レベル基準線の高さが"
		 "何で落ちるかを測る")
{
	probe.log("【実物件の図面で走らせる】断面ビューポートと、**高さが 0 でない**レベル基準線が");
	probe.log("ある図面が必要。試験用のシートレイヤと断面ビューポートを足し、既存のビューポートも");
	probe.log("更新するので、**走らせた後は保存しないこと。**");

	// =====================================================================
	probe.log("");
	probe.log("== 1. 図面の断面ビューポートと、答え合わせの基準を選ぶ");
	std::vector<MCObjectHandle> viewports;
	std::vector<MCObjectHandle> designLayers;
	ProbeCollectViewports(viewports, designLayers);
	probe.log("ビューポート " + ProbeFormatInt(static_cast<long long>(viewports.size())) +
			  " 件 / デザインレイヤ " +
			  ProbeFormatInt(static_cast<long long>(designLayers.size())) + " 枚");

	// **出どころ（UI 製かインポータ製か）は名乗らせない**——1050 の中身だけでは分からない。
	MCObjectHandle referenceViewport = nil;	 // 基準に使う既存の断面ビューポート
	MCObjectHandle referenceBenchmark = nil; // 高さが 0 でないレベル基準線
	ProbeBenchmarkTriple triple;
	std::string expectedElev = "（基準が見つからなかった）";
	size_t sectionCount = 0;
	for (size_t index = 0; index < viewports.size(); ++index)
	{
		MCObjectHandle viewport = viewports[index];
		if (!ProbeIsSectionViewport(viewport))
			continue;
		++sectionCount;
		MCObjectHandle benchmark = ProbeFindBenchmarkPreferNonZero(viewport);
		const std::string elev = (benchmark != nil) ? ProbeReadElev(benchmark) : std::string();
		// 断面 VP は多いので、基準が決まるまでと最初の数枚だけ並べる。
		if (sectionCount <= 3 || (benchmark != nil && referenceBenchmark == nil))
			probe.log("  [" + ProbeFormatInt(static_cast<long long>(index)) + "] 名前=\"" +
					  ProbeObjectName(viewport) + "\" 1050 が単位行列か=" +
					  (ProbeIs1050Identity(viewport) ? "はい" : "いいえ") + " ビューの向き(1007)=" +
					  ProbeReadVariable(viewport, 1007) + " 注釈のレベル基準線=" +
					  (benchmark != nil ? std::string("有 Elev=") + elev : std::string("無")));
		if (benchmark != nil && referenceBenchmark == nil && !ProbeElevIsZero(elev))
		{
			referenceBenchmark = benchmark;
			referenceViewport = viewport;
			expectedElev = elev;
		}
	}
	probe.log("  断面ビューポートは " + ProbeFormatInt(static_cast<long long>(sectionCount)) +
			  " 件");
	if (referenceBenchmark != nil)
	{
		VWParametricObj parametric(referenceBenchmark);
		triple.fStoryName = parametric.GetParamValue("__StoryName");
		triple.fLevelTypeName = parametric.GetParamValue("__LevelTypeName");
		triple.fDatum = parametric.GetParamValue("Datum");
		triple.fFound = true;
		probe.log("  基準にする 3 つ組: __StoryName=\"" +
				  std::string(static_cast<const char*>(triple.fStoryName)) +
				  "\" __LevelTypeName=\"" +
				  std::string(static_cast<const char*>(triple.fLevelTypeName)) + "\" Datum=\"" +
				  std::string(static_cast<const char*>(triple.fDatum)) +
				  "\" → 正解の Elev = " + expectedElev);
		VectorWorks::Extension::IMarkersPluginSupportPtr markers(
			VectorWorks::Extension::IID_MarkersPluginSupport);
		if (markers)
			probe.log(
				std::string("  IsElevationBenchmarkConstrained(基準の個体) = ") +
				(markers->IsElevationBenchmarkConstrained(referenceBenchmark) ? "true" : "false"));
	}
	else
	{
		probe.fail("高さが 0 でないレベル基準線が、断面ビューポートの注釈に 1 本も無い。"
				   "**0 を基準にすると「落ちた」と「もともと 0」が見分けられない**ので、"
				   "GL 以外（1FL・2FL・軒高など）のレベル基準線が注釈にある図面で"
				   "もう一度走らせてほしい。3 以降（1050 の A/B）は測れているので載っている。");
	}

	// =====================================================================
	probe.log("");
	probe.log("== 2. 既存の断面ビューポートを更新したら 1050 と Elev はどうなるか");
	if (referenceViewport == nil)
	{
		probe.log("  基準の断面ビューポートが無いので、この段は測れない");
	}
	else
	{
		probe.log("  対象: \"" + ProbeObjectName(referenceViewport) + "\"（正解 " + expectedElev +
				  "）");
		ProbeLogMatrices(probe, "更新前", referenceViewport);
		ProbeLogGroups(probe, "更新前", referenceViewport);
		std::vector<std::string> before = ProbeSnapshotVariables(referenceViewport);
		probe.log("  UpdateViewport を呼ぶ");
		gSDK->UpdateViewport(referenceViewport);
		std::vector<std::string> after = ProbeSnapshotVariables(referenceViewport);
		ProbeLogVariableDiff(probe, "既存", before, after);
		ProbeLogMatrices(probe, "更新後", referenceViewport);
		probe.log("  更新後の Elev = " + ProbeReadElev(referenceBenchmark) + "（正解 " +
				  expectedElev + "）");
		ProbeLogElevDurability(probe, "既存", referenceBenchmark, referenceViewport, expectedElev);
	}

	// =====================================================================
	probe.log("");
	probe.log("== 3. SDK 製を 3 枚作って比べる（A: 表示を倒さない / B: 倒す / C: 切るものが無い）");
	WorldRect bounds;
	bounds.left = 0;
	bounds.right = 0;
	bounds.top = 0;
	bounds.bottom = 0;
	bool haveBounds = false;
	for (size_t index = 0; index < designLayers.size(); ++index)
	{
		for (MCObjectHandle member = gSDK->FirstMemberObj(designLayers[index]); member != nil;
			 member = gSDK->NextObject(member))
		{
			WorldRect one;
			if (!gSDK->GetObjectBounds(member, one))
				continue;
			if (!haveBounds)
			{
				bounds = one;
				haveBounds = true;
				continue;
			}
			if (one.left < bounds.left)
				bounds.left = one.left;
			if (one.right > bounds.right)
				bounds.right = one.right;
			if (one.top > bounds.top)
				bounds.top = one.top;
			if (one.bottom < bounds.bottom)
				bounds.bottom = one.bottom;
		}
	}
	if (!haveBounds)
	{
		bounds.left = -20000;
		bounds.right = 20000;
		bounds.top = 20000;
		bounds.bottom = -20000;
		probe.log("  デザインレイヤの図形の範囲が取れなかったので ±20000 で引く（＝空図面）");
	}
	probe.log("  断面線を引く範囲: left=" + ProbeFormatNumber(bounds.left) + " right=" +
			  ProbeFormatNumber(bounds.right) + " top=" + ProbeFormatNumber(bounds.top) +
			  " bottom=" + ProbeFormatNumber(bounds.bottom));

	const double centerY = (bounds.top + bounds.bottom) / 2.0;
	const double centerX = (bounds.left + bounds.right) / 2.0;
	const double spanY = (bounds.top - bounds.bottom) > 0 ? (bounds.top - bounds.bottom) : 10000.0;
	const WorldPt sectionPt1(bounds.left - spanY, centerY);
	const WorldPt sectionPt2(bounds.right + spanY, centerY);
	const WorldPt sectionPt3(centerX, centerY - spanY);
	// C は「切るものが無い」断面（建物からうんと離す）。
	const double farAway = 1.0e6;
	const WorldPt farPt1(bounds.left - spanY, centerY + farAway);
	const WorldPt farPt2(bounds.right + spanY, centerY + farAway);
	const WorldPt farPt3(centerX, centerY + farAway - spanY);

	MCObjectHandle sheetLayer = gSDK->CreateLayer("VW調査151 断面試験", kLayerSheet);
	if (sheetLayer == nil)
		probe.log("  CreateLayer が nil（既に同名のレイヤがあるかもしれない）");

	MCObjectHandle viewportA = nil;
	MCObjectHandle viewportB = nil;
	MCObjectHandle viewportC = nil;
	if (sheetLayer != nil)
	{
		viewportA = gSDK->CreateSectionViewport(sectionPt1, sectionPt2, sectionPt3, 0.0, -10000.0,
												100000.0, sheetLayer);
		viewportB = gSDK->CreateSectionViewport(sectionPt1, sectionPt2, sectionPt3, 0.0, -10000.0,
												100000.0, sheetLayer);
		viewportC = gSDK->CreateSectionViewport(farPt1, farPt2, farPt3, 0.0, -10000.0, 100000.0,
												sheetLayer);
	}
	if (viewportA == nil || viewportB == nil || viewportC == nil)
	{
		probe.fail("CreateSectionViewport が nil を返した（A=" +
				   std::string(viewportA != nil ? "有" : "nil") +
				   " B=" + std::string(viewportB != nil ? "有" : "nil") +
				   " C=" + std::string(viewportC != nil ? "有" : "nil") + "）");
	}
	else
	{
		probe.log("  3 枚できた。どれも作成直後は 1050 = 単位行列・1007 = 7（上）:");
		ProbeLogMatrices(probe, "A 作成直後", viewportA);
		ProbeLogMatrices(probe, "B 作成直後", viewportB);
		ProbeLogMatrices(probe, "C 作成直後", viewportC);

		ProbeSetUpSectionViewport(probe, "A", viewportA, designLayers, false);
		ProbeSetUpSectionViewport(probe, "B", viewportB, designLayers, true);
		ProbeSetUpSectionViewport(probe, "C", viewportC, designLayers, true);

		probe.log("  3 枚とも UpdateViewport を呼ぶ");
		gSDK->UpdateViewport(viewportA);
		gSDK->UpdateViewport(viewportB);
		gSDK->UpdateViewport(viewportC);

		probe.log("  --- A（表示を倒していない）");
		ProbeLogMatrices(probe, "A 更新後", viewportA);
		ProbeLogGroups(probe, "A 更新後", viewportA);
		probe.log("  --- B（表示を倒した）");
		ProbeLogMatrices(probe, "B 更新後", viewportB);
		ProbeLogGroups(probe, "B 更新後", viewportB);
		probe.log("  --- C（倒したが、断面線を建物から 1,000,000mm 離した）");
		ProbeLogMatrices(probe, "C 更新後", viewportC);
		ProbeLogGroups(probe, "C 更新後", viewportC);
		probe.log("  ↑ **B だけが 1055 と一致し、A と C が単位行列なら、1050 は「描けた断面」から");
		probe.log("     引き直される派生値**だと確定する（＝写す手順ではなく、表示の作法の話）");

		// D: B を全レイヤ非表示にして再更新する（引き直しが可逆なら単位行列へ戻るはず）。
		probe.log("  --- D（B を全レイヤ非表示にして再更新）");
		for (size_t index = 0; index < designLayers.size(); ++index)
			gSDK->SetViewportLayerVisibility(viewportB, designLayers[index], -1 /* 非表示 */);
		gSDK->UpdateViewport(viewportB);
		ProbeLogMatrices(probe, "D 更新後", viewportB);
		ProbeLogGroups(probe, "D 更新後", viewportB);
		probe.log("  ↑ ここで単位行列へ戻るなら、「戻る」の正体は**描くものが無いこと**である");
		// 4 の測定のために B を元へ戻す。
		for (size_t index = 0; index < designLayers.size(); ++index)
			gSDK->SetViewportLayerVisibility(viewportB, designLayers[index], 0 /* 表示 */);
		gSDK->UpdateViewport(viewportB);
		ProbeLogMatrices(probe, "B 戻した後", viewportB);
	}

	// =====================================================================
	probe.log("");
	probe.log("== 4. B の注釈へレベル基準線を置く——**1050 へ何も写さずに**高さが出るか");
	if (viewportB == nil)
	{
		probe.log("  B が無いので測れない");
	}
	else
	{
		MCObjectHandle marker = nil;
		probe.log("  置いて 3 つ組を書く: " +
				  ProbePlaceBenchmark(viewportB, triple, centerX, 0.0, marker));
		if (marker != nil)
		{
			probe.log("  置いた直後（まだ作り直していない）の Elev = " + ProbeReadElev(marker));
			probe.log("  **1050 へは何も写さない**（いま単位行列か=" +
					  std::string(ProbeIs1050Identity(viewportB) ? "はい" : "いいえ") + "）");
			gSDK->ResetObject(marker);
			probe.log("  ResetObject した後の Elev = " + ProbeReadElev(marker) + "（正解 " +
					  expectedElev + "）");
			probe.log("  ↑ 正解と一致すれば、**「1055 を 1050 へ写す」手順は要らない**");
			VectorWorks::Extension::IMarkersPluginSupportPtr markers(
				VectorWorks::Extension::IID_MarkersPluginSupport);
			if (markers)
				probe.log(std::string("  IsElevationBenchmarkConstrained(置いた個体) = ") +
						  (markers->IsElevationBenchmarkConstrained(marker) ? "true" : "false"));
			ProbeLogElevDurability(probe, "B", marker, viewportB, expectedElev);
		}
	}

	// =====================================================================
	probe.log("");
	probe.log("== 5. 断面線（645）はどこにいるか");
	size_t sectionLinesOnLayers = 0;
	for (size_t index = 0; index < designLayers.size(); ++index)
	{
		for (MCObjectHandle member = gSDK->FirstMemberObj(designLayers[index]); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) != kParametricNode)
				continue;
			VWParametricObj parametric(member);
			if (parametric.GetInternalID() == kInternalID_SectionLine2)
				++sectionLinesOnLayers;
		}
	}
	probe.log("  デザインレイヤにある断面線（645）= " +
			  ProbeFormatInt(static_cast<long long>(sectionLinesOnLayers)) +
			  " 件（0 なら、断面線はビューポートの断面群（4）の中だけにいる）");
	if (referenceViewport != nil)
	{
		MCObjectHandle sectionGroup = gSDK->GetViewportGroup(referenceViewport, 4);
		probe.log("  既存の断面群（4）= " + ProbeCensus(sectionGroup));
		if (sectionGroup != nil)
		{
			for (MCObjectHandle member = gSDK->FirstMemberObj(sectionGroup); member != nil;
				 member = gSDK->NextObject(member))
			{
				if (gSDK->GetObjectTypeN(member) != kParametricNode)
					continue;
				VWParametricObj sectionLine(member);
				if (sectionLine.GetInternalID() != kInternalID_SectionLine2)
					continue;
				const size_t count = sectionLine.GetParamsCount();
				probe.log("  その中の断面線の欄 " + ProbeFormatInt(static_cast<long long>(count)) +
						  " 件:");
				for (size_t index = 0; index < count && index < 60; ++index)
					probe.log(
						"    " +
						std::string(static_cast<const char*>(sectionLine.GetParamName(index))) +
						" = " +
						std::string(static_cast<const char*>(sectionLine.GetParamValue(index))));
				break;
			}
		}
	}

	probe.log("");
	probe.log("おわり。**この図面は保存しないこと**（試験用のシートレイヤ「VW調査151 断面試験」・");
	probe.log("そこへ作った断面ビューポート 3 枚・注釈へ置いたレベル基準線が残っており、既存の");
	probe.log("ビューポートも更新してある）。");
}
