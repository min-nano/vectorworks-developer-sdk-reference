//
//	probes/runtime/section-vp-1050/probe.cpp
//
//	[issue #151] SDK 製の断面ビューポートは `UpdateViewport` のたびにビュー行列
//	（`ovViewportViewMatrix` ＝ 1050）が単位行列へ戻る。その理由を突き止めるために、
//	**同じ図面・同じ 1 回の実行のうちに**次を測る。
//
//	  1. UI 製の断面ビューポートを更新したら 1050 はどうなるのか（＝非対称はあるのか）
//	  2. 更新の前後で 1050 以外に変わる欄はあるか（UI 製・SDK 製の両方で総当り）
//	  3. SDK 製の断面はそもそも描かれているのか（キャッシュ群の中身を種類ごとに数える）
//	  4. レベル基準線の高さは、属性・クラス・パラメータの変更で落ちるのか
//	     （＝人が後から触る図面で壊れるのか）
//	  5. UI 製の断面線（645）が持っている欄（UI と同じ経路の候補だった
//	     `GS_CreateSectionLineInstance` は**リンクできない**ことが分かったので、
//	     代わりに断面線そのものを読む）
//
//	**実物件の図面（UI が断面ツールで作った断面ビューポートがあるもの）で走らせる。**
//	図面へ試験用のシートレイヤと断面ビューポートを足すので、**走らせた後は保存しない**。
//

#include "Probe.h"

#include "VectorWorks/Extension/IMarkersPluginSupport.h"

#include <cstdio>
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
	MCObjectHandle ProbeFindBenchmark(MCObjectHandle viewport)
	{
		MCObjectHandle annotations =
			gSDK->GetViewportGroup(viewport, 2 /* kViewportGroupAnnotation */);
		if (annotations == nil)
			return nil;
		for (MCObjectHandle member = gSDK->FirstMemberObj(annotations); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) != kParametricNode)
				continue;
			VWParametricObj parametric(member);
			if (parametric.GetParametricName() == TXString(kProbeBenchmarkName))
				return member;
		}
		return nil;
	}

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
} // namespace

VW_PROBE("section-vp-1050", "断面の 1050 が更新で戻る理由を測る",
		 "UI 製と SDK 製を同じ図面で突き合わせ、更新前後の全欄・キャッシュ群・レベル基準線を測る")
{
	probe.log("【この調査は実物件の図面で走らせる】UI が断面ツールで作った断面ビューポートが");
	probe.log(
		"必要。試験用のシートレイヤと断面ビューポートを足すので、走らせた後は保存しないこと。");

	// =====================================================================
	probe.log("");
	probe.log("== 1. 図面のビューポートを数える");
	std::vector<MCObjectHandle> viewports;
	std::vector<MCObjectHandle> designLayers;
	ProbeCollectViewports(viewports, designLayers);
	probe.log("ビューポート " + ProbeFormatInt(static_cast<long long>(viewports.size())) +
			  " 件 / デザインレイヤ " +
			  ProbeFormatInt(static_cast<long long>(designLayers.size())) + " 枚");

	MCObjectHandle uiViewport = nil;
	for (size_t index = 0; index < viewports.size(); ++index)
	{
		MCObjectHandle viewport = viewports[index];
		const bool isSection = ProbeIsSectionViewport(viewport);
		const bool identity = ProbeIs1050Identity(viewport);
		probe.log("  [" + ProbeFormatInt(static_cast<long long>(index)) + "] 名前=\"" +
				  ProbeObjectName(viewport) + "\" 断面か=" + (isSection ? "はい" : "いいえ") +
				  " 1050 が単位行列か=" + (identity ? "はい" : "いいえ") +
				  " ビューの向き(1007)=" + ProbeReadVariable(viewport, 1007));
		if (isSection && !identity && uiViewport == nil)
			uiViewport = viewport;
	}

	// =====================================================================
	probe.log("");
	probe.log("== 2. UI 製の断面ビューポートを更新したら 1050 はどうなるか");
	ProbeBenchmarkTriple triple;
	if (uiViewport == nil)
	{
		probe.fail("UI 製の断面ビューポート（1050 が単位行列でない断面 VP）が図面に無い。"
				   "UI の断面ツールで作った断面ビューポートのある図面で走らせること。");
	}
	else
	{
		probe.log("対象: \"" + ProbeObjectName(uiViewport) + "\"");
		ProbeLogMatrices(probe, "更新前", uiViewport);
		ProbeLogGroups(probe, "更新前", uiViewport);

		MCObjectHandle uiBenchmark = ProbeFindBenchmark(uiViewport);
		if (uiBenchmark != nil)
		{
			VWParametricObj parametric(uiBenchmark);
			triple.fStoryName = parametric.GetParamValue("__StoryName");
			triple.fLevelTypeName = parametric.GetParamValue("__LevelTypeName");
			triple.fDatum = parametric.GetParamValue("Datum");
			triple.fFound = true;
			probe.log("  注釈にある UI 製のレベル基準線から 3 つ組を借りる: __StoryName=\"" +
					  std::string(static_cast<const char*>(triple.fStoryName)) +
					  "\" __LevelTypeName=\"" +
					  std::string(static_cast<const char*>(triple.fLevelTypeName)) + "\" Datum=\"" +
					  std::string(static_cast<const char*>(triple.fDatum)) +
					  "\" Elev=" + ProbeReadElev(uiBenchmark));
			VectorWorks::Extension::IMarkersPluginSupportPtr markers(
				VectorWorks::Extension::IID_MarkersPluginSupport);
			if (markers)
				probe.log(
					std::string("  IsElevationBenchmarkConstrained(UI 製の個体) = ") +
					(markers->IsElevationBenchmarkConstrained(uiBenchmark) ? "true" : "false"));
			else
				probe.log("  IMarkersPluginSupport を取れなかった");
		}
		else
		{
			probe.log("  注釈に UI 製のレベル基準線は無い（3 つ組は借りられない）");
		}

		std::vector<std::string> before = ProbeSnapshotVariables(uiViewport);
		probe.log("  UpdateViewport を呼ぶ");
		gSDK->UpdateViewport(uiViewport);
		std::vector<std::string> after = ProbeSnapshotVariables(uiViewport);
		ProbeLogVariableDiff(probe, "UI 製", before, after);
		ProbeLogMatrices(probe, "更新後", uiViewport);
		probe.log("  ↑ 1050 が単位行列になっていなければ「UI 製では保たれる」＝非対称が確定する");
	}

	// =====================================================================
	probe.log("");
	probe.log("== 3. SDK 製の断面ビューポートを同じ図面へ作る");
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
		probe.log("  デザインレイヤの図形の範囲が取れなかったので ±20000 で引く");
	}
	probe.log("  断面線を引く範囲: left=" + ProbeFormatNumber(bounds.left) + " right=" +
			  ProbeFormatNumber(bounds.right) + " top=" + ProbeFormatNumber(bounds.top) +
			  " bottom=" + ProbeFormatNumber(bounds.bottom));

	const double centerY = (bounds.top + bounds.bottom) / 2.0;
	const double centerX = (bounds.left + bounds.right) / 2.0;
	const double spanY = (bounds.top - bounds.bottom);
	const WorldPt sectionPt1(bounds.left - spanY, centerY);
	const WorldPt sectionPt2(bounds.right + spanY, centerY);
	const WorldPt sectionPt3(centerX, centerY - (spanY > 0 ? spanY : 10000.0));

	MCObjectHandle sheetLayer = gSDK->CreateLayer("VW調査151 断面試験", kLayerSheet);
	if (sheetLayer == nil)
		probe.log("  CreateLayer が nil（既に同名のレイヤがあるかもしれない）");
	MCObjectHandle sdkViewport = nil;
	if (sheetLayer != nil)
		sdkViewport = gSDK->CreateSectionViewport(sectionPt1, sectionPt2, sectionPt3, 0.0, -10000.0,
												  100000.0, sheetLayer);
	if (sdkViewport == nil)
	{
		probe.fail("CreateSectionViewport が nil を返した（シートレイヤ=" +
				   std::string(sheetLayer == nil ? "nil" : "有") + "）");
	}
	else
	{
		probe.log("  できた。まだ何も設定していない状態:");
		ProbeLogMatrices(probe, "作成直後", sdkViewport);
		probe.log("  作成直後 断面か(1054)=" + ProbeReadVariable(sdkViewport, 1054) +
				  " ビューの向き(1007)=" + ProbeReadVariable(sdkViewport, 1007) +
				  " 1056=" + ProbeReadVariable(sdkViewport, 1056));
		ProbeLogGroups(probe, "作成直後", sdkViewport);

		// 下ごしらえ（Findings「ビューポート」の作法）。**描かれない理由を「見えないから」で
		// 取り違えないために、レイヤとクラスを明示的に表示へ倒す。**
		for (size_t index = 0; index < designLayers.size(); ++index)
			gSDK->SetViewportLayerVisibility(sdkViewport, designLayers[index], 0 /* 表示 */);
		size_t classCount = 0;
		size_t* classCounter = &classCount;
		MCObjectHandle viewportForClasses = sdkViewport;
		gSDK->ForEachClass(true,
						   [viewportForClasses, classCounter](MCObjectHandle classHandle)
						   {
							   gSDK->SetViewportClassVisibility(
								   viewportForClasses, gSDK->GetObjectInternalIndex(classHandle),
								   0 /* 表示 */);
							   ++(*classCounter);
						   });
		probe.log("  レイヤ " + ProbeFormatInt(static_cast<long long>(designLayers.size())) +
				  " 枚・クラス " + ProbeFormatInt(static_cast<long long>(classCount)) +
				  " 件を表示へ倒した");
		{
			VWViewportObj viewportObj(sdkViewport);
			viewportObj.SetRenderType(renderFinalHiddenLine);
		}
		TVariableBlock trueBlock;
		trueBlock = static_cast<Boolean>(true);
		gSDK->SetObjectVariable(sdkViewport, 1064 /* 切断面より奥を表示 */, trueBlock);

		probe.log("  UpdateViewport を呼ぶ（1 回目）");
		gSDK->UpdateViewport(sdkViewport);
		ProbeLogMatrices(probe, "1 回目の更新後", sdkViewport);
		ProbeLogGroups(probe, "1 回目の更新後", sdkViewport);
		probe.log(
			"  ↑ 群 4（断面）・5/6/7/15（キャッシュ）の中身が「無」なら、断面は描かれていない");

		std::vector<std::string> beforeSecond = ProbeSnapshotVariables(sdkViewport);
		probe.log("  UpdateViewport を呼ぶ（2 回目。何も触っていない）");
		gSDK->UpdateViewport(sdkViewport);
		std::vector<std::string> afterSecond = ProbeSnapshotVariables(sdkViewport);
		ProbeLogVariableDiff(probe, "SDK 製 (更新だけ)", beforeSecond, afterSecond);

		probe.log("  1055 を 1050 へ写す: " +
				  std::string(ProbeCopy1055To1050(sdkViewport) ? "成功" : "失敗"));
		ProbeLogMatrices(probe, "写した直後", sdkViewport);
		std::vector<std::string> beforeThird = ProbeSnapshotVariables(sdkViewport);
		probe.log("  UpdateViewport を呼ぶ（3 回目。1050 を写した後）");
		gSDK->UpdateViewport(sdkViewport);
		std::vector<std::string> afterThird = ProbeSnapshotVariables(sdkViewport);
		ProbeLogVariableDiff(probe, "SDK 製 (写した後の更新)", beforeThird, afterThird);
		ProbeLogMatrices(probe, "写した後の更新後", sdkViewport);
	}

	// =====================================================================
	probe.log("");
	probe.log("== 4. レベル基準線の高さは、何をしたら落ちるのか");
	if (sdkViewport == nil)
	{
		probe.log("  SDK 製のビューポートが無いので測れない");
	}
	else
	{
		MCObjectHandle marker = nil;
		probe.log("  SDK 製の注釈へレベル基準線を置いて 3 つ組を書く: " +
				  ProbePlaceBenchmark(sdkViewport, triple, 0.0, 0.0, marker));
		if (marker != nil)
		{
			probe.log("  置いた直後（まだ作り直していない）の Elev = " + ProbeReadElev(marker));
			probe.log("  注釈を置き終えたので 1055 を 1050 へ写す: " +
					  std::string(ProbeCopy1055To1050(sdkViewport) ? "成功" : "失敗") +
					  "（1050 が単位行列か=" +
					  (ProbeIs1050Identity(sdkViewport) ? "はい" : "いいえ") + "）");
			gSDK->ResetObject(marker);
			probe.log("  写してから ResetObject した後の Elev = " + ProbeReadElev(marker) +
					  " ← ここが数値でなければ以降の①〜⑥は読めない");
			VectorWorks::Extension::IMarkersPluginSupportPtr markers(
				VectorWorks::Extension::IID_MarkersPluginSupport);
			if (markers)
				probe.log(std::string("  IsElevationBenchmarkConstrained(いま置いた個体) = ") +
						  (markers->IsElevationBenchmarkConstrained(marker) ? "true" : "false"));

			GS_SetLineWeight(gCBP, marker, 50);
			probe.log("  ① 線の太さを変えた（GS_SetLineWeight）後の Elev = " +
					  ProbeReadElev(marker));

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
				probe.log("  ② クラスを変えた後の Elev = " + ProbeReadElev(marker));
			}
			else
			{
				probe.log("  ② 変えられる別のクラスが無かった");
			}

			{
				VWParametricObj parametric(marker);
				parametric.SetParamValue("EPfx", "+");
			}
			probe.log("  ③ 見た目のパラメータ（EPfx）を書いた後の Elev = " + ProbeReadElev(marker));

			gSDK->UpdateViewport(sdkViewport);
			probe.log("  ④ ビューポートを更新した後の Elev = " + ProbeReadElev(marker) +
					  "（1050 が単位行列か=" +
					  (ProbeIs1050Identity(sdkViewport) ? "はい" : "いいえ") + "）");

			gSDK->ResetObject(marker);
			probe.log("  ⑤ ResetObject した後の Elev = " + ProbeReadElev(marker));

			probe.log("  1055 を 1050 へ写し直して ResetObject: " +
					  std::string(ProbeCopy1055To1050(sdkViewport) ? "写せた" : "写せない"));
			gSDK->ResetObject(marker);
			probe.log("  ⑥ 写し直して ResetObject した後の Elev = " + ProbeReadElev(marker));
		}
	}

	if (uiViewport != nil)
	{
		probe.log("  --- 対照: UI 製の注釈へ同じことをする（ビューポートの側だけが違う）");
		MCObjectHandle control = nil;
		probe.log("  UI 製の注釈へレベル基準線を置いて 3 つ組を書く: " +
				  ProbePlaceBenchmark(uiViewport, triple, 0.0, 0.0, control));
		if (control != nil)
		{
			probe.log("  1050 へは何も写さない（いま 1050 が単位行列か=" +
					  std::string(ProbeIs1050Identity(uiViewport) ? "はい" : "いいえ") + "）");
			gSDK->ResetObject(control);
			probe.log("  写さずに ResetObject した Elev = " + ProbeReadElev(control) +
					  " ← ここが数値なら「UI 製では写す手順がそもそも要らない」");
			gSDK->UpdateViewport(uiViewport);
			probe.log("  更新した後の Elev = " + ProbeReadElev(control) + "（1050 が単位行列か=" +
					  (ProbeIs1050Identity(uiViewport) ? "はい" : "いいえ") + "）");
			gSDK->ResetObject(control);
			probe.log("  更新した後に ResetObject した Elev = " + ProbeReadElev(control) +
					  " ← ここが数値なら「UI 製では 1050 を写す必要がそもそも無い」");
			GS_SetLineWeight(gCBP, control, 50);
			probe.log("  線の太さを変えた後の Elev = " + ProbeReadElev(control));
		}
	}

	// =====================================================================
	probe.log("");
	probe.log("== 5. UI 製の断面線（645）は何を持っているか");
	probe.log("  ※ UI と同じ経路に見えた GS_CreateSectionLineInstance /");
	probe.log("     GS_IsSectionLineLinkedToViewport は**呼べない**——ヘッダ（APIBase.Legacy.h の");
	probe.log("     APP_API_FUNCTION）にはあるが、CB_ シンボルが libVWSDK.a に入っていないので");
	probe.log("     リンクで「Undefined symbols」になる。だから代わりに、UI 製の断面線が");
	probe.log(
		"     "
		"どんな欄を持っているかを読み出す（ビューポートとの結び付きがここに出るなら見える）。");
	size_t sectionLines = 0;
	MCObjectHandle firstSectionLine = nil;
	for (size_t index = 0; index < designLayers.size(); ++index)
	{
		for (MCObjectHandle member = gSDK->FirstMemberObj(designLayers[index]); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) != kParametricNode)
				continue;
			VWParametricObj parametric(member);
			if (parametric.GetInternalID() != kInternalID_SectionLine2)
				continue;
			++sectionLines;
			if (firstSectionLine == nil)
				firstSectionLine = member;
		}
	}
	probe.log("  デザインレイヤにある断面線（645）= " +
			  ProbeFormatInt(static_cast<long long>(sectionLines)) + " 件");
	if (firstSectionLine != nil)
	{
		VWParametricObj sectionLine(firstSectionLine);
		const size_t count = sectionLine.GetParamsCount();
		probe.log("  最初の断面線の欄 " + ProbeFormatInt(static_cast<long long>(count)) + " 件:");
		for (size_t index = 0; index < count && index < 80; ++index)
		{
			const TXString name = sectionLine.GetParamName(index);
			const TXString value = sectionLine.GetParamValue(index);
			probe.log("    " + std::string(static_cast<const char*>(name)) + " = " +
					  std::string(static_cast<const char*>(value)));
		}
	}

	probe.log("");
	probe.log("おわり。**この図面は保存しないこと**（試験用のシートレイヤ「VW調査151 断面試験」と");
	probe.log("注釈へ置いたレベル基準線が残っている）。");
}
