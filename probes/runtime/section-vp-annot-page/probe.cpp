//
//	probes/runtime/section-vp-annot-page/probe.cpp
//
//	[issue #200] 断面ビューポートの「注釈座標（モデル mm）」と「用紙座標（シートレイヤ mm）」の
//	対応、および **GL（モデル Z=0）が用紙のどこに来るか**を実測する。
//
//	【3 巡目】1・2 巡目で**変換そのものは確定した**（下記）。残っているのは 2 つだけで、
//	どちらも「断面に何も映らなかった」ために取れていない:
//	  (a) `GetObjectBounds(viewport)` が映っているモデル・高さ範囲を含むか（issue の 2 番）
//	  (b) **GL（モデル Z=0）が注釈座標／用紙座標のどこに来るか**（この調査の本題）
//
//	2 巡目で確定した分（この巡では末尾で 1 回だけ回帰確認する）:
//	  ・**用紙座標 = 注釈座標 ÷ 縮尺。オフセットは無い**（k=0.0100 ＝ 1/100 ちょうど、
//	    off は左下の角・右上の角の両方から 0.0000、ずれ 0.0000。2 枚で同一）
//	  ・1024/1025 は用紙 mm の位置で `MoveObject` に正確に追従する
//	    ——ただし**変換の原点ではない**（動かしても off は 0 のまま。差は移動量そのもの）
//	  ・注釈の中の図形の `GetObjectBounds` は**ビューポートを動かすと「移動量 × 縮尺」ずれる**
//	  ・`GetObjectBounds(viewport)` に注釈が入るのは **`ResetObject` の後だけ**
//	  ・1049 の平行移動 = 位置 × 縮尺 ／ 1051 は読めない ／ 1050・1055 は断面線 pt1 基準の
//	    モデル→断面ビュー行列で用紙とは無関係 ／ 1056 は単位行列 ／ crop は無い
//	  ・空の容れ物の外接は**反転した矩形**（左 > 右）で返る
//
//	**断面が空だった原因を、この巡で総当たりに切り分ける。** 2 巡目で表示設定は正しく
//	入っていた（表示レイヤ 3/3・クラス 46/46・1064 読み戻し true・1050 は断面の向き）のに、
//	キャッシュ群は 4 番（2 件・外接は空）だけで 3/5/6/7/15 はすべて nil だった
//	——これは Findings「Viewports」#151 の表の行 C（＝**切るものが無い**）と同じ顔である。
//	つまり表示設定ではなく**幾何の与え方**が外れている。そこで**モデルは固定し、断面線の
//	引き方・奥行き・高さ範囲・1064/1065 を 1 つずつだけ変えた 8 通り**を並べて、
//	どれでキャッシュ群に中身が入るかを見る（#189 が符号の位置を割り出したのと同じやり方）。
//
//	**そして 8 通りのそれぞれを「高さ 3000 の箱」と「高さ 6000 の箱」の 2 枚で作る。**
//	こうすると、中身が入った通りについては**その場で (a) と (b) の答えも出る**
//	——高さ範囲は 16 枚すべて同じなので、外接の高さが 2 枚で違えば「高さ範囲は入らない」、
//	そして中身の外接の下端が 2 枚で一致すれば「GL は映るモデルの高さに依らず同じ所に来る」。
//	どの通りが当たっても 1 回で終わるように組んである。
//

#include "Probe.h"

#include <cstdio>
#include <string>

namespace
{
	const double kProbeI200Scale = 100.0; // 1:100 のつもりで ovViewportScale へ書く値

	// モデル（固定）。**y を 0 から離してある**——2 巡目は y 0〜1000 で、ビュー行列から
	// 読める「切断面は局所 z=0」とちょうど境界で接していた疑いがあるため。
	const double kProbeI200BoxY0 = 2000.0;
	const double kProbeI200BoxY1 = 3000.0;

	std::string ProbeI200Num(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.4f", v);
		return std::string(buf);
	}

	// **空の容れ物の外接は「反転した矩形」で返る**（左=+DBL_MAX・右=-DBL_MAX）。2 巡目で確定。
	bool ProbeI200RectIsEmpty(const WorldRect& r)
	{
		return static_cast<double>(r.left) > static_cast<double>(r.right) ||
			   static_cast<double>(r.bottom) > static_cast<double>(r.top);
	}

	std::string ProbeI200RectText(const WorldRect& r)
	{
		if (ProbeI200RectIsEmpty(r))
			return "【空＝反転した矩形】";
		return "左=" + ProbeI200Num(static_cast<double>(r.left)) +
			   " 下=" + ProbeI200Num(static_cast<double>(r.bottom)) +
			   " 右=" + ProbeI200Num(static_cast<double>(r.right)) +
			   " 上=" + ProbeI200Num(static_cast<double>(r.top)) +
			   " / 幅=" + ProbeI200Num(static_cast<double>(r.right) - static_cast<double>(r.left)) +
			   " 高=" + ProbeI200Num(static_cast<double>(r.top) - static_cast<double>(r.bottom));
	}

	bool ProbeI200ReadRealVar(MCObjectHandle h, short selector, double& out)
	{
		double scratch = 0.0;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return false;
		return block.GetReal64(out) != 0;
	}

	bool ProbeI200ReadBoolVar(MCObjectHandle h, short selector, bool& out)
	{
		Boolean scratch = false;
		TVariableBlock block(scratch);
		if (!gSDK->GetObjectVariable(h, selector, block))
			return false;
		Boolean value = false;
		if (!block.GetBoolean(value))
			return false;
		out = (value != 0);
		return true;
	}

	void ProbeI200WriteBoolVar(MCObjectHandle h, short selector, bool value)
	{
		TVariableBlock block(static_cast<Boolean>(value));
		gSDK->SetObjectVariable(h, selector, block);
	}

	size_t ProbeI200CountMembers(MCObjectHandle group)
	{
		size_t count = 0;
		for (MCObjectHandle h = gSDK->FirstMemberObj(group); h != nil; h = gSDK->NextObject(h))
			++count;
		return count;
	}

	// 1 つの直方体（x0〜x1 × y 2000〜3000 × z 0〜z1）を作る。作成先はアクティブレイヤ。
	MCObjectHandle ProbeI200MakeBox(double x0, double x1, double z1)
	{
		VWFC::Math::VWPolygon2D base;
		base.AddVertex(x0, kProbeI200BoxY0);
		base.AddVertex(x1, kProbeI200BoxY0);
		base.AddVertex(x1, kProbeI200BoxY1);
		base.AddVertex(x0, kProbeI200BoxY1);
		base.SetClosed(true);
		VWFC::VWObjects::VWExtrudeObj extrude(base, 0.0, z1);
		return extrude;
	}

	// 切り分ける 1 通り。**基準から 1 つだけ違う**ように並べる。
	struct ProbeI200Variant
	{
		const char* name;
		double lineY;  // 断面線の y
		double eyeY;   // pt3（見る側）の y
		double depth;  // CreateSectionViewport の depth
		double startH; // 高さ範囲の下
		double endH;   // 高さ範囲の上
		bool beyond;   // 1064 切断面より奥を表示
		bool before;   // 1065 切断面より手前を表示
	};

	const ProbeI200Variant kProbeI200Variants[] = {
		{"0 基準（手前から奥を見る）", -3000.0, -8000.0, 0.0, -1000.0, 9000.0, true, false},
		{"1 depth=30000（有限）", -3000.0, -8000.0, 30000.0, -1000.0, 9000.0, true, false},
		{"2 pt3 を反対側へ", -3000.0, 8000.0, 0.0, -1000.0, 9000.0, true, false},
		{"3 断面線がモデルを切る", 2500.0, -2500.0, 0.0, -1000.0, 9000.0, true, false},
		{"4 高さ範囲 0〜9000", -3000.0, -8000.0, 0.0, 0.0, 9000.0, true, false},
		{"5 1065 も true", -3000.0, -8000.0, 0.0, -1000.0, 9000.0, true, true},
		{"6 断面線を奥に置く", 6000.0, 11000.0, 0.0, -1000.0, 9000.0, true, false},
		{"7 1064=false・1065=true", -3000.0, -8000.0, 0.0, -1000.0, 9000.0, false, true},
	};

	// 表示設定——Findings「Viewports」#151 の実証済みの手順。2 巡目でこれは正しく
	// 入ることを確かめた（表示レイヤ 3/3・クラス 46/46）ので、この巡では戻り値だけ数える。
	void ProbeI200PrepareViewport(MCObjectHandle vp, const ProbeI200Variant& variant,
								  size_t& outLayerNg, size_t& outClassNg)
	{
		outLayerNg = 0;
		outClassNg = 0;
		size_t* layerNg = &outLayerNg;
		size_t* classNg = &outClassNg;
		gSDK->ForEachLayerN(
			[vp, layerNg](MCObjectHandle layer)
			{
				if (!gSDK->SetViewportLayerVisibility(vp, layer, 0))
					++(*layerNg);
			});
		gSDK->ForEachClass(
			true,
			[vp, classNg](MCObjectHandle cls)
			{
				if (!gSDK->SetViewportClassVisibility(vp, gSDK->GetObjectInternalIndex(cls), 0))
					++(*classNg);
			});

		VWFC::VWObjects::VWViewportObj vpObj(vp);
		vpObj.SetRenderType(renderFinalHiddenLine);
		ProbeI200WriteBoolVar(vp, ovSectionViewportDisplayObjectsBeyondCutPlane, variant.beyond);
		ProbeI200WriteBoolVar(vp, ovSectionViewportDisplayObjectsBeforeCutPlane, variant.before);
		ProbeI200WriteBoolVar(vp, ovViewportDisplayPlanar, false);
		ProbeI200WriteBoolVar(vp, ovViewportDisplay2DComponents, true);
		TVariableBlock scaleBlock(kProbeI200Scale);
		gSDK->SetObjectVariable(vp, ovViewportScale, scaleBlock);
		gSDK->UpdateViewport(vp);
	}

	// キャッシュ群の件数を 1 行に畳む。**中身が入った群の外接を outContent へ合成する**
	// ——それが「断面に実際に描かれたもの」の外接（注釈座標）である。
	std::string ProbeI200Caches(MCObjectHandle vp, WorldRect& outContent, bool& outHasContent)
	{
		outHasContent = false;
		const short kProbeI200Groups[] = {3, 4, 5, 6, 7, 15};
		std::string line;
		for (short groupType : kProbeI200Groups)
		{
			if (!line.empty())
				line += " ";
			line += std::to_string(groupType) + "=";
			MCObjectHandle group = gSDK->GetViewportGroup(vp, groupType);
			if (group == nil)
			{
				line += "nil";
				continue;
			}
			line += std::to_string(ProbeI200CountMembers(group));
			WorldRect rect;
			if (!gSDK->GetObjectBounds(group, rect) || ProbeI200RectIsEmpty(rect))
			{
				line += "(空)";
				continue;
			}
			// 群 4（断面群）は中身そのものではないので外接の合成には使わない。
			if (groupType == 4)
				continue;
			if (!outHasContent)
			{
				outContent = rect;
				outHasContent = true;
			}
			else
			{
				if (static_cast<double>(rect.left) < static_cast<double>(outContent.left))
					outContent.left = rect.left;
				if (static_cast<double>(rect.bottom) < static_cast<double>(outContent.bottom))
					outContent.bottom = rect.bottom;
				if (static_cast<double>(rect.right) > static_cast<double>(outContent.right))
					outContent.right = rect.right;
				if (static_cast<double>(rect.top) > static_cast<double>(outContent.top))
					outContent.top = rect.top;
			}
		}
		return line;
	}
} // namespace

VW_PROBE("section-vp-annot-page", "断面VPの注釈→用紙座標", "空になる原因を総当たりしGLを測る")
{
	// ---- 1. モデル（高さ 3000 と 6000 の直方体。y は 2000〜3000 で固定）----------
	probe.log("■ 1. デザインレイヤと直方体 2 つ（高さ 3000 / 6000・y は 2000〜3000）");
	MCObjectHandle designLayer = gSDK->CreateLayer("I200 モデル", static_cast<short>(kLayerDesign));
	if (designLayer == nil)
	{
		probe.fail("CreateLayer(kLayerDesign) が nil を返した");
		return;
	}
	MCObjectHandle boxes[2] = {ProbeI200MakeBox(500.0, 2500.0, 3000.0),
							   ProbeI200MakeBox(5500.0, 7500.0, 6000.0)};
	const char* const kProbeI200BoxNames[2] = {"低(3000)", "高(6000)"};
	const double kProbeI200LineX0[2] = {0.0, 5000.0};
	const double kProbeI200LineX1[2] = {3000.0, 8000.0};
	const double kProbeI200BoxTop[2] = {3000.0, 6000.0};

	for (int b = 0; b < 2; ++b)
	{
		if (boxes[b] == nil)
		{
			probe.fail(std::string("VWExtrudeObj が nil（") + kProbeI200BoxNames[b] + "）");
			return;
		}
		// **押し出しが本当に厚みを持っているかを読み戻す。** 2 巡目で疑ったが確かめる術が
		// 無かった筋（厚み 0 の潰れた押し出しなら断面に映らない）をここで潰す。
		VWFC::VWObjects::VWExtrudeObj ext(boxes[b]);
		double baseElev = 0.0, thickness = 0.0;
		ext.GetExtrudeValues(baseElev, thickness);
		WorldRect planBounds;
		const bool okPlan = gSDK->GetObjectBounds(boxes[b], planBounds) != 0;
		MCObjectHandle parent = gSDK->ParentObject(boxes[b]);
		probe.log(std::string("  ") + kProbeI200BoxNames[b] +
				  ": 型=" + std::to_string(gSDK->GetObjectTypeN(boxes[b])) +
				  " 基準高=" + ProbeI200Num(baseElev) + " 厚み=" + ProbeI200Num(thickness) +
				  " 親はレイヤか=" + (parent == designLayer ? "はい" : "いいえ"));
		probe.log(std::string("    平面上の外接 ") +
				  (okPlan ? ProbeI200RectText(planBounds) : std::string("読めず")));
	}

	// ---- 2. シートレイヤ ---------------------------------------------------
	MCObjectHandle sheet = gSDK->CreateLayer("I200 用紙", static_cast<short>(kLayerSheet));
	if (sheet == nil)
	{
		probe.fail("CreateLayer(kLayerSheet) が nil を返した");
		return;
	}
	probe.log("■ 2. シートレイヤを作った");

	// ---- 3. 8 通り × 2 枚 を作って、どれで中身が入るかを見る ----------------
	probe.log("■ 3. 断面線の引き方・奥行き・高さ範囲・1064/1065 を 1 つずつ変えた 8 通り");
	probe.log("  キャッシュ群の読み方: 群4 は断面群（空でも必ず在る）。**3/5/6/7/15 に件数が"
			  "入った通りが「描けた」**（Findings #151 の表）");

	const int kProbeI200NumVariants =
		static_cast<int>(sizeof(kProbeI200Variants) / sizeof(kProbeI200Variants[0]));
	WorldRect contentRect[8][2];
	bool hasContent[8][2];
	WorldRect pageRect[8][2];
	bool hasPage[8][2];

	for (int v = 0; v < kProbeI200NumVariants; ++v)
	{
		const ProbeI200Variant& variant = kProbeI200Variants[v];
		probe.log(std::string("— 通り ") + variant.name + ": 線y=" + ProbeI200Num(variant.lineY) +
				  " pt3y=" + ProbeI200Num(variant.eyeY) + " depth=" + ProbeI200Num(variant.depth) +
				  " 高さ=" + ProbeI200Num(variant.startH) + "〜" + ProbeI200Num(variant.endH) +
				  " 1064=" + (variant.beyond ? "T" : "F") +
				  " 1065=" + (variant.before ? "T" : "F"));
		for (int b = 0; b < 2; ++b)
		{
			hasContent[v][b] = false;
			hasPage[v][b] = false;
			const WorldPt pt1(kProbeI200LineX0[b], variant.lineY);
			const WorldPt pt2(kProbeI200LineX1[b], variant.lineY);
			const WorldPt pt3((kProbeI200LineX0[b] + kProbeI200LineX1[b]) * 0.5, variant.eyeY);
			MCObjectHandle vp = gSDK->CreateSectionViewport(pt1, pt2, pt3, variant.depth,
															variant.startH, variant.endH, sheet);
			if (vp == nil)
			{
				probe.log(std::string("    ") + kProbeI200BoxNames[b] +
						  ": CreateSectionViewport が nil");
				continue;
			}
			size_t layerNg = 0, classNg = 0;
			ProbeI200PrepareViewport(vp, variant, layerNg, classNg);

			bool readBeyond = false, readBefore = false;
			ProbeI200ReadBoolVar(vp, ovSectionViewportDisplayObjectsBeyondCutPlane, readBeyond);
			ProbeI200ReadBoolVar(vp, ovSectionViewportDisplayObjectsBeforeCutPlane, readBefore);
			std::string cacheLine = ProbeI200Caches(vp, contentRect[v][b], hasContent[v][b]);
			hasPage[v][b] = gSDK->GetObjectBounds(vp, pageRect[v][b]) != 0 &&
							!ProbeI200RectIsEmpty(pageRect[v][b]);
			probe.log(std::string("    ") + kProbeI200BoxNames[b] + ": 群 " + cacheLine +
					  " / 1064読=" + (readBeyond ? "T" : "F") +
					  " 1065読=" + (readBefore ? "T" : "F") +
					  (layerNg || classNg ? " ※表示設定に失敗あり" : ""));
			probe.log(std::string("      用紙の外接 ") + (hasPage[v][b]
															  ? ProbeI200RectText(pageRect[v][b])
															  : std::string("空か読めず")));
			if (hasContent[v][b])
				probe.log(std::string("      ★中身の外接（注釈座標） ") +
						  ProbeI200RectText(contentRect[v][b]));
		}
	}

	// ---- 4. 中身が入った通りについて (a) と (b) を出す ----------------------
	probe.log("■ 4. 中身が入った通りの読み解き——(a) 外接が何で決まるか・(b) GL の在り所");
	probe.log("  高さ範囲は 16 枚すべて同じ。モデルの Z は 低=0〜3000 / 高=0〜6000");
	int drawn = 0;
	for (int v = 0; v < kProbeI200NumVariants; ++v)
	{
		if (!hasContent[v][0] && !hasContent[v][1])
			continue;
		++drawn;
		probe.log(std::string("— 通り ") + kProbeI200Variants[v].name);
		for (int b = 0; b < 2; ++b)
		{
			if (!hasContent[v][b])
			{
				probe.log(std::string("    ") + kProbeI200BoxNames[b] + ": 中身なし");
				continue;
			}
			const double bottom = static_cast<double>(contentRect[v][b].bottom);
			const double top = static_cast<double>(contentRect[v][b].top);
			probe.log(std::string("    ") + kProbeI200BoxNames[b] +
					  ": 中身の y 下=" + ProbeI200Num(bottom) + " 上=" + ProbeI200Num(top) +
					  " → 用紙 y 下=" + ProbeI200Num(bottom / kProbeI200Scale) +
					  " 上=" + ProbeI200Num(top / kProbeI200Scale));
			probe.log(std::string("      モデルの上端 ") + ProbeI200Num(kProbeI200BoxTop[b]) +
					  " との差: 上端 " + ProbeI200Num(top - kProbeI200BoxTop[b]) +
					  " / 下端は Z=0 からの差 " + ProbeI200Num(bottom));
		}
		if (hasContent[v][0] && hasContent[v][1])
		{
			const double d = static_cast<double>(contentRect[v][1].bottom) -
							 static_cast<double>(contentRect[v][0].bottom);
			probe.log("    ◎ 2 枚の下端の差 = " + ProbeI200Num(d) +
					  "（0 なら GL は映るモデルの高さに依らず同じ所に来る）");
			if (hasPage[v][0] && hasPage[v][1])
			{
				const double dh = (static_cast<double>(pageRect[v][1].top) -
								   static_cast<double>(pageRect[v][1].bottom)) -
								  (static_cast<double>(pageRect[v][0].top) -
								   static_cast<double>(pageRect[v][0].bottom));
				probe.log("    ◎ 用紙の外接の高さの差 = " + ProbeI200Num(dh) +
						  "（0 なら高さ範囲が外接を決めている／違えば映るモデルが決めている）");
			}
		}
	}
	if (drawn == 0)
		probe.fail("8 通りのどれでも断面に中身が入らなかった（キャッシュ群 3/5/6/7/15 が全部空）"
				   "——幾何の与え方ではなく、SDK 製の断面そのものが描けない可能性を次に見る");

	// ---- 5. 変換の回帰確認（2 巡目で確定した分を 1 回だけ） -----------------
	// 注釈へ 4 辺を自分で作る大きさの矩形を置き、ResetObject して k と off を解く。
	probe.log("■ 5. 変換の回帰確認（用紙 = 注釈 ÷ 縮尺・オフセット無し を 1 枚で再確認）");
	{
		const WorldPt pt1(0.0, -3000.0), pt2(3000.0, -3000.0), pt3(1500.0, -8000.0);
		MCObjectHandle vp = gSDK->CreateSectionViewport(pt1, pt2, pt3, 0.0, -1000.0, 9000.0, sheet);
		if (vp == nil)
		{
			probe.log("  CreateSectionViewport が nil——回帰確認は飛ばす");
		}
		else
		{
			size_t layerNg = 0, classNg = 0;
			ProbeI200PrepareViewport(vp, kProbeI200Variants[0], layerNg, classNg);
			WorldRect want;
			want.left = -300000.0;
			want.right = 100000.0;
			want.bottom = -400000.0;
			want.top = 200000.0;
			MCObjectHandle mark = gSDK->CreateRectangle(want);
			if (mark == nil || !gSDK->AddViewportAnnotationObject(vp, mark))
			{
				probe.log("  注釈へ矩形を置けなかった——回帰確認は飛ばす");
			}
			else
			{
				gSDK->ResetObject(vp);
				gSDK->MoveObject(vp, 137.0, -59.0);
				gSDK->ResetObject(vp);
				WorldRect page, annot;
				if (gSDK->GetObjectBounds(vp, page) && gSDK->GetObjectBounds(mark, annot) &&
					!ProbeI200RectIsEmpty(page) && !ProbeI200RectIsEmpty(annot))
				{
					const double annotW =
						static_cast<double>(annot.right) - static_cast<double>(annot.left);
					const double annotH =
						static_cast<double>(annot.top) - static_cast<double>(annot.bottom);
					const double kx =
						(static_cast<double>(page.right) - static_cast<double>(page.left)) / annotW;
					const double ky =
						(static_cast<double>(page.top) - static_cast<double>(page.bottom)) / annotH;
					const double offXlo =
						static_cast<double>(page.left) - static_cast<double>(annot.left) * kx;
					const double offYlo =
						static_cast<double>(page.bottom) - static_cast<double>(annot.bottom) * ky;
					const double offXhi =
						static_cast<double>(page.right) - static_cast<double>(annot.right) * kx;
					const double offYhi =
						static_cast<double>(page.top) - static_cast<double>(annot.top) * ky;
					double px = 0.0, py = 0.0;
					ProbeI200ReadRealVar(vp, ovViewportXPosition, px);
					ProbeI200ReadRealVar(vp, ovViewportYPosition, py);
					probe.log("  用紙の外接 " + ProbeI200RectText(page));
					probe.log("  注釈の矩形 " + ProbeI200RectText(annot));
					probe.log("  k=(" + ProbeI200Num(kx) + ", " + ProbeI200Num(ky) +
							  ")  1/縮尺=" + ProbeI200Num(1.0 / kProbeI200Scale));
					probe.log("  off 左下から=(" + ProbeI200Num(offXlo) + ", " +
							  ProbeI200Num(offYlo) + ")  右上から=(" + ProbeI200Num(offXhi) + ", " +
							  ProbeI200Num(offYhi) + ")");
					probe.log("  1024/1025=(" + ProbeI200Num(px) + ", " + ProbeI200Num(py) +
							  ")（動かした量そのもの。off とは別物）");
				}
				else
				{
					probe.log("  外接が読めず——回帰確認は飛ばす");
				}
			}
		}
	}

	probe.log("■ 終わり。図面は壊れたままでよい（新規の空図面で走らせる前提）");
}
