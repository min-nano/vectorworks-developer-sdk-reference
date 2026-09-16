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

	probe.log("=== E（参考・失敗して構わない）実在の「構造材」PIO 名を当ててみる ===");
	MCObjectHandle worldPathE = MakeVerticalWorldPath(probe, "E", kBottomZ, kTopZ);
	MCObjectHandle pioE = nullptr;
	if (worldPathE != nullptr)
		pioE = gSDK->CreateCustomObjectPath("Structural Member", worldPathE, nullptr);
	if (pioE == nullptr)
	{
		probe.log(
			"E. \"Structural Member\" "
			"という名前では作れなかった（内部の登録名が違う可能性が高い。あるいは A/C/D と"
			"同じ理由で CreateNurbsCurve 自体が失敗している可能性もある——上のログを見比べること）。"
			"issue #56 "
			"の報告者はホームズ君プラグイン側で使っている正しい名前を知っているはずなので、"
			"ここを差し替えて再走行してほしい——本命は E 系列で「上端の絶対 Z が別レベルの Z と"
			"ちょうど一致する」状況を作って潰れを再現することにある。");
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
		}
	}
}
