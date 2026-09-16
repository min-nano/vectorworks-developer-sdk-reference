//
//	probes/runtime/custom-object-path-coordinate-system/probe.cpp
//
//	[issue #56] CreateCustomObjectPath / SetCustomObjectPath が受け取る・返すパスの座標系
//	（世界座標か、挿入点相対か）と、SetObjectStoryBound が汎用カスタムオブジェクトの
//	パス（実体）に影響するかどうかを、汎用のカスタムオブジェクトを使って実測する。
//
//	初回実行（build 5c704191dbe0）は A 系列の最初の一歩（CreateNurbsCurve か
//	CreateCustomObjectPath）で 0.01 秒のうちに失敗し、どちらが nil を返したのか
//	区別が付かなかった。今回はステップごとに個別のログ／fail を出して切り分ける。
//

#include "Probe.h"

#include <string>

namespace
{
	void LogPoint(vwprobe::Report& probe, const char* label, const WorldPt3& pt)
	{
		probe.log(std::string(label) + ": (" + std::to_string(pt.x) + ", " + std::to_string(pt.y) +
				  ", " + std::to_string(pt.z) + ")");
	}

	// 2 点の直線パス（NURBS）を世界座標で作る（(0,0,z0) -> (0,0,z1)）。
	// 途中の各ステップをログに出し、失敗した箇所が分かるようにする。
	MCObjectHandle MakeVerticalWorldPath(vwprobe::Report& probe, const char* stepLabel,
										 WorldCoord z0, WorldCoord z1)
	{
		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(0, 0, z0), false, 1);
		if (curve == nullptr)
		{
			probe.log(std::string(stepLabel) +
					  ": CreateNurbsCurve が nil を返した（z0=" + std::to_string(z0) + "）");
			return nullptr;
		}
		probe.log(std::string(stepLabel) + ": CreateNurbsCurve は非nilを返した");

		gSDK->Add3DVertex(curve, WorldPt3(0, 0, z1), true);

		WorldPt3 v0, v1;
		Boolean ok0 = gSDK->NurbsGetPt3D(curve, 0, 0, v0);
		Boolean ok1 = gSDK->NurbsGetPt3D(curve, 0, 1, v1);
		probe.log(std::string(stepLabel) + ": Add3DVertex 直後の読み戻し ok0=" +
				  (ok0 ? "true" : "false") + " ok1=" + (ok1 ? "true" : "false"));
		if (ok0)
			LogPoint(probe, (std::string(stepLabel) + ": curve[0]").c_str(), v0);
		if (ok1)
			LogPoint(probe, (std::string(stepLabel) + ": curve[1]").c_str(), v1);

		return curve;
	}

	// PIO が内部に持つパスの両端点を読み戻す。
	bool ReadPathEndpoints(vwprobe::Report& probe, MCObjectHandle pio, WorldPt3& outP0,
						   WorldPt3& outP1)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
		{
			probe.log("  GetCustomObjectPath が nil を返した");
			return false;
		}
		Boolean ok0 = gSDK->NurbsGetPt3D(path, 0, 0, outP0);
		Boolean ok1 = gSDK->NurbsGetPt3D(path, 0, 1, outP1);
		if (!ok0 || !ok1)
		{
			probe.log("  NurbsGetPt3D が false を返した");
			return false;
		}
		return true;
	}
} // namespace

VW_PROBE("custom-object-path-coordinate-system", "CreateCustomObjectPath 系のパス座標系を実測する",
		 "世界座標で渡した2点パスがPIO内部でどう保持されるか、"
		 "SetCustomObjectPathとの非対称、SetObjectStoryBoundが汎用PIOのパスを変えるかを読み戻して確"
		 "かめる")
{
	const TXString kPioName = "VwSdkProbesPathCoordTest";
	const WorldCoord kBottomZ = 572;
	const WorldCoord kTopZ = 3531;

	MCObjectHandle formatNode = gSDK->DefineCustomObject(kPioName, kCustomObjectPrefNever);
	probe.log(std::string("DefineCustomObject は ") + (formatNode != nullptr ? "非nil" : "nil") +
			  " を返した");

	probe.log("=== A. CreateCustomObjectPath に世界座標の2点パスを渡す ===");
	MCObjectHandle worldPathA = MakeVerticalWorldPath(probe, "A", kBottomZ, kTopZ);
	MCObjectHandle pioA = nullptr;
	if (worldPathA == nullptr)
	{
		probe.fail("A: CreateNurbsCurve が nil を返した（詳細は上のログ）");
	}
	else
	{
		pioA = gSDK->CreateCustomObjectPath(kPioName, worldPathA, nullptr);
		if (pioA == nullptr)
		{
			probe.fail("A: CreateCustomObjectPath が nil を返した（CreateNurbsCurve 自体は成功）");
		}
	}

	if (pioA != nullptr)
	{
		VWParametricObj objA(pioA);
		WorldPt3 insertionA = objA.GetObjectModelPos();
		LogPoint(probe, "A. 挿入点（GetObjectModelPos）", insertionA);

		WorldPt3 a0, a1;
		if (ReadPathEndpoints(probe, pioA, a0, a1))
		{
			LogPoint(probe, "A. 読み戻したパス[0]", a0);
			LogPoint(probe, "A. 読み戻したパス[1]", a1);
			probe.log("A. z1-z0 = " + std::to_string(a1.z - a0.z) + "（渡した差は " +
					  std::to_string(kTopZ - kBottomZ) + "）");
		}

		probe.log("=== B. 同じオブジェクトへ SetObjectStoryBound（自階の LayerElevation "
				  "基準）を掛ける ===");
		MockUp::SStoryObjectData bottomBound;
		bottomBound.fBound = MockUp::eStoryObjectBound_LayerElevation;
		bottomBound.fBoundStory = 0;
		bottomBound.fOffset = kBottomZ;
		MockUp::SStoryObjectData topBound;
		topBound.fBound = MockUp::eStoryObjectBound_LayerElevation;
		topBound.fBoundStory = 0;
		topBound.fOffset = kTopZ;

		const MockUp::TObjectBoundID kBottomBoundID = 1;
		const MockUp::TObjectBoundID kTopBoundID = 0;
		bool setBottomOk = gSDK->SetObjectStoryBound(pioA, kBottomBoundID, bottomBound);
		bool setTopOk = gSDK->SetObjectStoryBound(pioA, kTopBoundID, topBound);
		probe.log(std::string("B. SetObjectStoryBound 戻り値: bottom=") +
				  (setBottomOk ? "true" : "false") + " top=" + (setTopOk ? "true" : "false"));
		probe.log("B. GetObjectBoundElevation: bottom=" +
				  std::to_string(gSDK->GetObjectBoundElevation(pioA, kBottomBoundID)) +
				  " top=" + std::to_string(gSDK->GetObjectBoundElevation(pioA, kTopBoundID)));

		gSDK->ResetObject(pioA);

		WorldPt3 b0, b1;
		if (ReadPathEndpoints(probe, pioA, b0, b1))
		{
			LogPoint(probe, "B. ResetObject 後のパス[0]", b0);
			LogPoint(probe, "B. ResetObject 後のパス[1]", b1);
			probe.log("B. z1-z0 = " + std::to_string(b1.z - b0.z));
		}
	}

	probe.log("=== C. CreateCustomObjectPathNoOffset との比較（同じ世界座標パス） ===");
	MCObjectHandle worldPathC = MakeVerticalWorldPath(probe, "C", kBottomZ, kTopZ);
	MCObjectHandle pioC = nullptr;
	if (worldPathC == nullptr)
	{
		probe.fail("C: CreateNurbsCurve が nil を返した（詳細は上のログ）");
	}
	else
	{
		pioC = gSDK->CreateCustomObjectPathNoOffset(kPioName, worldPathC, nullptr);
		if (pioC == nullptr)
			probe.fail("C: CreateCustomObjectPathNoOffset が nil を返した");
	}
	if (pioC != nullptr)
	{
		VWParametricObj objC(pioC);
		LogPoint(probe, "C. 挿入点（GetObjectModelPos）", objC.GetObjectModelPos());

		WorldPt3 c0, c1;
		if (ReadPathEndpoints(probe, pioC, c0, c1))
		{
			LogPoint(probe, "C. 読み戻したパス[0]", c0);
			LogPoint(probe, "C. 読み戻したパス[1]", c1);
		}
	}

	probe.log("=== D. SetCustomObjectPath の座標系（世界座標 vs 相対座標で差し替え） ===");
	MCObjectHandle basePathD = MakeVerticalWorldPath(probe, "D-base", kBottomZ, kTopZ);
	MCObjectHandle pioD = nullptr;
	if (basePathD == nullptr)
	{
		probe.fail("D: 土台の CreateNurbsCurve が nil を返した（詳細は上のログ）");
	}
	else
	{
		pioD = gSDK->CreateCustomObjectPath(kPioName, basePathD, nullptr);
		if (pioD == nullptr)
			probe.fail("D: 土台の CreateCustomObjectPath が nil を返した");
	}
	if (pioD != nullptr)
	{
		MCObjectHandle worldReplacement = MakeVerticalWorldPath(probe, "D-world", kBottomZ, kTopZ);
		if (worldReplacement != nullptr && gSDK->SetCustomObjectPath(pioD, worldReplacement))
		{
			WorldPt3 d0, d1;
			if (ReadPathEndpoints(probe, pioD, d0, d1))
			{
				LogPoint(probe, "D. 世界座標で差し替えた後のパス[0]", d0);
				LogPoint(probe, "D. 世界座標で差し替えた後のパス[1]", d1);
			}
		}
		else
		{
			probe.log("D. 世界座標での SetCustomObjectPath に失敗した（差し替え用パスの作成失敗、"
					  "または SetCustomObjectPath 自体が false）");
		}

		MCObjectHandle localReplacement =
			MakeVerticalWorldPath(probe, "D-local", 0, kTopZ - kBottomZ);
		if (localReplacement != nullptr && gSDK->SetCustomObjectPath(pioD, localReplacement))
		{
			WorldPt3 e0, e1;
			if (ReadPathEndpoints(probe, pioD, e0, e1))
			{
				LogPoint(probe, "D. 相対座標(0..差分)で差し替えた後のパス[0]", e0);
				LogPoint(probe, "D. 相対座標(0..差分)で差し替えた後のパス[1]", e1);
			}
		}
		else
		{
			probe.log("D. 相対座標での SetCustomObjectPath に失敗した（差し替え用パスの作成失敗、"
					  "または SetCustomObjectPath 自体が false）");
		}
	}

	probe.log("=== E. 実在の「構造材」PIO（内部登録名 \"StructuralMember\"。空白なし）===");
	// ホームズ君プラグイン draw/StructuralMember.h の kStructuralMemberPlugin より
	// （PR #57 のコメントで確認: "Structural Member" ではなく空白なしの "StructuralMember"）。
	MCObjectHandle worldPathE = MakeVerticalWorldPath(probe, "E", kBottomZ, kTopZ);
	MCObjectHandle pioE = nullptr;
	if (worldPathE != nullptr)
		pioE = gSDK->CreateCustomObjectPath("StructuralMember", worldPathE, nullptr);
	if (pioE == nullptr)
	{
		probe.log("E. \"StructuralMember\" でも作れなかった（詳細は上の CreateNurbsCurve の"
				  "ログを見比べること。名前以外の要因が疑わしい）。");
	}
	else
	{
		VWParametricObj objE(pioE);
		LogPoint(probe, "E. 挿入点（GetObjectModelPos）", objE.GetObjectModelPos());
		WorldPt3 f0, f1;
		if (ReadPathEndpoints(probe, pioE, f0, f1))
		{
			LogPoint(probe, "E. 読み戻したパス[0]", f0);
			LogPoint(probe, "E. 読み戻したパス[1]", f1);
			probe.log("E. z1-z0 = " + std::to_string(f1.z - f0.z) + "（渡した差は " +
					  std::to_string(kTopZ - kBottomZ) + "）");
		}

		probe.log("=== F. 実在の構造材PIOへ SetObjectStoryBound（自階の LayerElevation "
				  "基準）を掛ける ===");
		MockUp::SStoryObjectData bottomBoundF;
		bottomBoundF.fBound = MockUp::eStoryObjectBound_LayerElevation;
		bottomBoundF.fBoundStory = 0;
		bottomBoundF.fOffset = kBottomZ;
		MockUp::SStoryObjectData topBoundF;
		topBoundF.fBound = MockUp::eStoryObjectBound_LayerElevation;
		topBoundF.fBoundStory = 0;
		topBoundF.fOffset = kTopZ;

		const MockUp::TObjectBoundID kBottomBoundIDF = 1;
		const MockUp::TObjectBoundID kTopBoundIDF = 0;
		bool setBottomOkF = gSDK->SetObjectStoryBound(pioE, kBottomBoundIDF, bottomBoundF);
		bool setTopOkF = gSDK->SetObjectStoryBound(pioE, kTopBoundIDF, topBoundF);
		probe.log(std::string("F. SetObjectStoryBound 戻り値: bottom=") +
				  (setBottomOkF ? "true" : "false") + " top=" + (setTopOkF ? "true" : "false"));
		probe.log("F. GetObjectBoundElevation: bottom=" +
				  std::to_string(gSDK->GetObjectBoundElevation(pioE, kBottomBoundIDF)) +
				  " top=" + std::to_string(gSDK->GetObjectBoundElevation(pioE, kTopBoundIDF)));

		gSDK->ResetObject(pioE);

		LogPoint(probe, "F. ResetObject 後の挿入点（GetObjectModelPos）", objE.GetObjectModelPos());

		WorldPt3 g0, g1;
		if (ReadPathEndpoints(probe, pioE, g0, g1))
		{
			LogPoint(probe, "F. ResetObject 後のパス[0]", g0);
			LogPoint(probe, "F. ResetObject 後のパス[1]", g1);
			probe.log("F. z1-z0 = " + std::to_string(g1.z - g0.z));
		}

		probe.log("=== G. 両端のバウンドを同じ絶対Zへ解決させる（潰れの直接再現を試みる） ===");
		MockUp::SStoryObjectData sameBound;
		sameBound.fBound = MockUp::eStoryObjectBound_LayerElevation;
		sameBound.fBoundStory = 0;
		sameBound.fOffset = kTopZ; // 上下とも同じ絶対Z（kTopZ）に解決させる
		bool setBottomOkG = gSDK->SetObjectStoryBound(pioE, kBottomBoundIDF, sameBound);
		bool setTopOkG = gSDK->SetObjectStoryBound(pioE, kTopBoundIDF, sameBound);
		probe.log(std::string("G. SetObjectStoryBound 戻り値: bottom=") +
				  (setBottomOkG ? "true" : "false") + " top=" + (setTopOkG ? "true" : "false"));
		probe.log("G. GetObjectBoundElevation: bottom=" +
				  std::to_string(gSDK->GetObjectBoundElevation(pioE, kBottomBoundIDF)) +
				  " top=" + std::to_string(gSDK->GetObjectBoundElevation(pioE, kTopBoundIDF)));

		gSDK->ResetObject(pioE);

		LogPoint(probe, "G. ResetObject 後の挿入点（GetObjectModelPos）", objE.GetObjectModelPos());

		WorldPt3 h0, h1;
		if (ReadPathEndpoints(probe, pioE, h0, h1))
		{
			LogPoint(probe, "G. ResetObject 後のパス[0]", h0);
			LogPoint(probe, "G. ResetObject 後のパス[1]", h1);
			probe.log("G. z1-z0 = " + std::to_string(h1.z - h0.z) +
					  "（両端の絶対Zを一致させたので、ここが 0 に潰れれば issue #56 の"
					  "現象を直接再現したことになる）");
		}

		probe.log("=== H. パスの絶対Zはバウンド解決後も意味を持つか（わざと嘘のパスで作る） ===");
		// F と全く同じバウンド（bottom=572, top=3531）を、新しいオブジェクトへ
		// **わざと大きく外れた絶対Z のパス**（0 -> 1）で作ってから掛ける。もし結果が F と
		// 一致すれば、ResetObject 後の実体はパスの絶対Zに依らずバウンドだけで決まる
		// ＝「パスにも絶対Zを持たせる」現行の二重指定は不要という根拠になる。
		MCObjectHandle worldPathH = MakeVerticalWorldPath(probe, "H", 0, 1);
		MCObjectHandle pioH = nullptr;
		if (worldPathH != nullptr)
			pioH = gSDK->CreateCustomObjectPath("StructuralMember", worldPathH, nullptr);
		if (pioH == nullptr)
		{
			probe.log("H. \"StructuralMember\" の2つ目のインスタンスが作れなかった。");
		}
		else
		{
			VWParametricObj objH(pioH);
			LogPoint(probe, "H. ResetObject 前の挿入点（GetObjectModelPos）",
					 objH.GetObjectModelPos());

			MockUp::SStoryObjectData bottomBoundH;
			bottomBoundH.fBound = MockUp::eStoryObjectBound_LayerElevation;
			bottomBoundH.fBoundStory = 0;
			bottomBoundH.fOffset = kBottomZ;
			MockUp::SStoryObjectData topBoundH;
			topBoundH.fBound = MockUp::eStoryObjectBound_LayerElevation;
			topBoundH.fBoundStory = 0;
			topBoundH.fOffset = kTopZ;
			gSDK->SetObjectStoryBound(pioH, kBottomBoundIDF, bottomBoundH);
			gSDK->SetObjectStoryBound(pioH, kTopBoundIDF, topBoundH);

			gSDK->ResetObject(pioH);

			LogPoint(probe, "H. ResetObject 後の挿入点（GetObjectModelPos）",
					 objH.GetObjectModelPos());
			WorldPt3 i0, i1;
			if (ReadPathEndpoints(probe, pioH, i0, i1))
			{
				LogPoint(probe, "H. ResetObject 後のパス[0]", i0);
				LogPoint(probe, "H. ResetObject 後のパス[1]", i1);
				probe.log("H. z1-z0 = " + std::to_string(i1.z - i0.z) +
						  "（F と同じ (0,-2959) 相当になれば、パスの絶対Zはバウンド解決後は"
						  "意味を持たない＝二重指定は不要、という根拠になる）");
			}
		}
	}
}
