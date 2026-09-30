//
//	ExtPioMatrix.cpp
//
//	`ExtPioMatrix.h` の実装。**`Recalculate` の中で行列を読む 4 つの口を並べて、
//	`PioMatrixTrace.h` のファイルへ 1 行ずつ書き溜めるだけ**の PIO である。
//
//	【絶対に守ること】`Recalculate` は VectorWorks が好きなときに何度でも呼ぶ。
//	  * **例外を境界の外へ出さない**（未捕捉例外は VectorWorks ごと落とす。
//	    Findings「Progress and Diagnostics」）。だから全体を try/catch で包む。
//	  * **ダイアログを出さない**（リセットは取り込み中にも起きる。止めたら取り込みが止まる）。
//	  * **他の図形を変えない**（見て測るだけ。`ResetObject` も呼ばない——`Recalculate` の
//	    中から呼べば入れ子になる）。
//
//	【入口と出口の両方で測る】絵を描く前（入口）と描いた後（出口）で同じ 4 つを読む。
//	**「描いたことで行列が変わる」なら、絵を描く前に読むか後に読むかで結論が変わる**
//	——実プラグインは描きながら読むので、そこを区別できないと直し方が決まらない。
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "ExtPioMatrix.h"
#include "PioMatrixTrace.h"

#include <cstdio>
#include <string>

// IMPLEMENT_VWParametricExtension は名前空間の外で展開するマクロなので、クラス名を素で
// 書けるように名前空間を開いておく（ProbeMenu.cpp と同じ作法）。
using namespace vwprobe;

namespace vwprobe
{
	namespace
	{
		// -------------------------------------------------------------------
		// PIO の定義。**サブタイプは線分**（issue #183 と同じ被験体にするため。
		// `ExtPioMatrix.h`）。
		//
		// `ResetOnMove` / `ResetOnRotate` を立てるのは、**動かす・回すでも
		// `Recalculate` が来る**ようにして、置き直しでも行列が測れるようにするため。
		//
		// 関数ローカル static で持つ理由は SMenuDef と同じ（ProbeMenu.cpp）——
		// SDK 側の非ローカル static を初期化子から参照するため、初期化順序に依らない形に
		// しておく。**配列は拡張が持ち続けるので、寿命はプロセスと同じでなければならない**
		// （`VWExtensionParametric` はポインタを写す）。
		const SParametricDef& matrixParametricDef()
		{
			static const SParametricDef def = {
				/*LocalizedName*/ {PLUGIN_VWR_ID, "pioName"},
				/*SubType*/ VectorWorks::Extension::kParametricSubType_Linear,
				/*ResetOnMove*/ true,
				/*ResetOnRotate*/ true,
				/*WallInsertOnEdge*/ false,
				/*WallInsertNoBreak*/ false,
				/*WallInsertHalfBreak*/ false,
				/*WallInsertHideCaps*/ false,
			};
			return def;
		}

		// パラメータ 2 つ。**OIP で編集してもらうのは `TraceNote`**（文字欄なので、
		// 打ち間違えても図が壊れない）。表の終わりは**ユニバーサル名が空の行**で示す
		// （`SDKLib/Source/VWSDK/VWFC/PluginSupport/VWExtensionParametric.cpp:79`）。
		const SParametricParamDef* matrixParametricParams()
		{
			static const SParametricParamDef params[] = {
				{pioMatrix::kParamNote, {PLUGIN_VWR_ID, "pioNote"}, "", "", kFieldText, 0},
				{pioMatrix::kParamLength,
				 {PLUGIN_VWR_ID, "pioLength"},
				 "12\"",
				 "300",
				 kFieldCoordDisp,
				 0},
				{"", {PLUGIN_VWR_ID, "pioName"}, "", "", EFieldStyle(0), 0},
			};
			return params;
		}

		// -------------------------------------------------------------------
		// 数値は**必ず同じ桁数**で出す（突き合わせが文字列比較なので、桁数が揺れると
		// 「違う」になってしまう）。
		std::string MatrixNum(double value)
		{
			char buffer[64];
			(void)std::snprintf(buffer, sizeof(buffer), "%.4f", value);
			return std::string(buffer);
		}

		std::string MatrixInt(long value)
		{
			char buffer[32];
			(void)std::snprintf(buffer, sizeof(buffer), "%ld", value);
			return std::string(buffer);
		}

		std::string MatrixPoint(const VWPoint3D& point)
		{
			return "(" + MatrixNum(point.x) + ", " + MatrixNum(point.y) + ", " +
				   MatrixNum(point.z) + ")";
		}

		// VWFC の行列を 1 行に畳む。**`恒等=` を付けるのがここの要点**——「返さないなら
		// 何を返すか（単位行列か）」が問いなので、目で見比べずに決まるようにする。
		std::string MatrixDescribe(const VWTransformMatrix& matrix)
		{
			return "off=" + MatrixPoint(matrix.GetOffset()) +
				   " U=" + MatrixPoint(matrix.GetUVector()) +
				   " V=" + MatrixPoint(matrix.GetVVector()) +
				   " W=" + MatrixPoint(matrix.GetWVector()) +
				   " 恒等=" + (matrix.IsIdentity() ? "はい" : "いいえ");
		}

		// 素の `TransformMatrix`（`gSDK->GetEntityMatrix` が書き込む型）を 1 行に畳む。
		// VWFC を通さない値も残しておく——**通す途中で化けていないこと**の裏取り。
		std::string MatrixDescribeRaw(const TransformMatrix& matrix)
		{
			return "off=(" + MatrixNum(matrix.v1.xOff) + ", " + MatrixNum(matrix.v1.yOff) + ", " +
				   MatrixNum(matrix.v1.zOff) + ") i=(" + MatrixNum(matrix.v1.a00) + ", " +
				   MatrixNum(matrix.v1.a01) + ", " + MatrixNum(matrix.v1.a02) + ") j=(" +
				   MatrixNum(matrix.v1.a10) + ", " + MatrixNum(matrix.v1.a11) + ", " +
				   MatrixNum(matrix.v1.a12) + ") k=(" + MatrixNum(matrix.v1.a20) + ", " +
				   MatrixNum(matrix.v1.a21) + ", " + MatrixNum(matrix.v1.a22) + ")";
		}

		// リセットの理由（`ObjectState::EStateType`）を名前で出す。**数字だけでは
		// 読めない**ので綴りに直す。値は決め打ちしない——enum の綴りで switch する。
		const char* MatrixStateName(Sint32 specifier)
		{
			switch (static_cast<ObjectState::EStateType>(specifier))
			{
			case ObjectState::kFirstRegenReset:
				return "kFirstRegenReset";
			case ObjectState::kMovedReset:
				return "kMovedReset";
			case ObjectState::kRotatedReset:
				return "kRotatedReset";
			case ObjectState::kParameterChangedReset:
				return "kParameterChangedReset";
			case ObjectState::kObjectChangedReset:
				return "kObjectChangedReset";
			case ObjectState::kLayerChangedReset:
				return "kLayerChangedReset";
			case ObjectState::kExitFromEditGroup:
				return "kExitFromEditGroup";
			case ObjectState::kNameChanged:
				return "kNameChanged";
			case ObjectState::kObjectUndoRestore:
				return "kObjectUndoRestore";
			case ObjectState::kObjectUndoRemove:
				return "kObjectUndoRemove";
			case ObjectState::kPlanarRefChanged:
				return "kPlanarRefChanged";
			case ObjectState::kBeforeExportReset:
				return "kBeforeExportReset";
			case ObjectState::kObjectUndoModify:
				return "kObjectUndoModify";
			case ObjectState::kObjectCreated:
				return "kObjectCreated";
			case ObjectState::kObjectDeleteBefore:
				return "kObjectDeleteBefore";
			case ObjectState::kObjectUngroupBefore:
				return "kObjectUngroupBefore";
			case ObjectState::kObjectExternalReset:
				return "kObjectExternalReset";
			case ObjectState::kObjectConnectionsRemapped:
				return "kObjectConnectionsRemapped";
			case ObjectState::kObjectReshaped:
				return "kObjectReshaped";
			case ObjectState::kObjectLockStatusChanged:
				return "kObjectLockStatusChanged";
			case ObjectState::kObjectPathReversed:
				return "kObjectPathReversed";
			case ObjectState::kObjectIsGeneratedBy:
				return "kObjectIsGeneratedBy";
			case ObjectState::kObjectScaleReset:
				return "kObjectScaleReset";
			case ObjectState::kObjectConvertingToUnstyled:
				return "kObjectConvertingToUnstyled";
			case ObjectState::kBuildingMaterialChangedReset:
				return "kBuildingMaterialChangedReset";
			case ObjectState::kObjectConvertedToUnstyled:
				return "kObjectConvertedToUnstyled";
			default:
				return "（この SDK に無い値）";
			}
		}

		// 直前に届いたリセットの理由。**`Recalculate` の中で 1 度だけ読んで空にする**
		// ——「理由が来ていない `Recalculate`」も知見なので、持ち越さない。
		Sint32 gMatrixLastState = -1;
		bool gMatrixHasLastState = false;

		// **4 つの口を並べて書き溜める。** 鍵は `<場面>/<口>` で、`②` は同じ鍵どうしを
		// 比べる（`PioMatrixTrace.h` の「1 行の形」）。
		void TraceMatrixReads(MCObjectHandle self, const std::string& scene)
		{
			// (1) VWParametricObj::GetObjectToWorldTransform（→ GS_GetEntityMatrix）
			VWParametricObj pio(self);
			VWTransformMatrix objectToWorld;
			pio.GetObjectToWorldTransform(objectToWorld);
			pioMatrix::AppendPair(scene + "/GetObjectToWorldTransform",
								  MatrixDescribe(objectToWorld));

			// (2) VWObject::GetObjectMatrix（→ GS_GetEntityMatrix。(1) と同じ呼び出し）
			VWObject object(self);
			VWTransformMatrix objectMatrix;
			object.GetObjectMatrix(objectMatrix);
			pioMatrix::AppendPair(scene + "/GetObjectMatrix", MatrixDescribe(objectMatrix));

			// (3) gSDK->GetEntityMatrix（ISDK の口。素の TransformMatrix のまま）
			TransformMatrix entity;
			gSDK->GetEntityMatrix(self, entity);
			pioMatrix::AppendPair(scene + "/GetEntityMatrix(生)", MatrixDescribeRaw(entity));

			// (4) VWObject::GetObjectModelMatrix（→ gSDK->GetEntityMatrix）
			const VWTransformMatrix modelMatrix = object.GetObjectModelMatrix();
			pioMatrix::AppendPair(scene + "/GetObjectModelMatrix", MatrixDescribe(modelMatrix));

			// (5) **実プラグインがやっていること**——世界座標の点を PIO のローカルへ落とす。
			// 行列が単位行列に化けていれば、ここに「落ちていない（世界座標のまま）」値が出る。
			const VWPoint3D world(pioMatrix::kProbeWorldX, pioMatrix::kProbeWorldY, 0.0);
			pioMatrix::AppendPair(scene + "/逆変換 世界→ローカル",
								  MatrixPoint(objectToWorld.InversePointTransform(world)));
		}

		// 絵は線 1 本だけ（**PIO のローカル座標で描く**。Findings「自作 PIO を足すときの
		// 3 点」の 2 番目）。選べる図形が無いと OIP で編集できないので、描くこと自体が要る。
		void DrawMatrixGeometry(MCObjectHandle self)
		{
			VWParametricObj pio(self);
			double length = pio.GetParamReal(pioMatrix::kParamLength);
			if (length <= 0.0)
				length = 300.0;
			VWLine2DObj line(VWPoint2D(0.0, 0.0), VWPoint2D(length, 0.0));
			(void)line;
		}
	} // namespace
} // namespace vwprobe

// ---------------------------------------------------------------------------
// 拡張機能の一意な ID とユニバーサル名。**ユニバーサル名は `PioMatrixTrace.h` の
// `kPioUniversalName` と一致させる**（プローブがその綴りで `CreateCustomObject` する）。
//
// NOLINT: IMPLEMENT_VWParametricExtension は SDK のマクロで、展開の中に clang-tidy が
// const を求める `static VWIID iid` がある（マクロ側のコード）。
// NOLINTBEGIN(misc-const-correctness)
// UUID: 2b8f5d13-7c4a-4e69-b0d2-53a1c7e84f60
IMPLEMENT_VWParametricExtension(
	/*Extension class*/ CExtObjPioMatrix,
	/*Event sink*/ CPioMatrix_EventSink,
	/*Universal name*/ "VwSdkProbesMatrix",
	/*Version*/ 1,
	/*UUID*/ 0x2b8f5d13, 0x7c4a, 0x4e69, 0xb0, 0xd2, 0x53, 0xa1, 0xc7, 0xe8, 0x4f, 0x60);
// NOLINTEND(misc-const-correctness)

// ---------------------------------------------------------------------------
vwprobe::CExtObjPioMatrix::CExtObjPioMatrix(CallBackPtr cbp)
	: VWExtensionParametric(cbp, vwprobe::matrixParametricDef(), vwprobe::matrixParametricParams())
{
}

vwprobe::CExtObjPioMatrix::~CExtObjPioMatrix() = default;

// ---------------------------------------------------------------------------
vwprobe::CPioMatrix_EventSink::CPioMatrix_EventSink(IVWUnknown* parent)
	: VWParametric_EventSink(parent)
{
}

vwprobe::CPioMatrix_EventSink::~CPioMatrix_EventSink() = default;

// ---------------------------------------------------------------------------
// **`kObjXPropAcceptStates` を立てるのがここの全部。** これが無いと
// `ObjectState::kAction` が来ないので、リセットの理由が分からない
// （`Info/Parametric Extended Properties.md`。issue #183 で実測）。
EObjectEvent vwprobe::CPioMatrix_EventSink::OnInitXProperties(CodeRefID objectID)
{
	try
	{
		using namespace VectorWorks::Extension;
		VCOMPtr<IExtendedProps> extProps(IID_ExtendedProps);
		if (extProps != nullptr)
			(void)extProps->SetObjectProperty(objectID, kObjXPropAcceptStates, true);
	}
	catch (...)
	{
		// ここで落ちても調査は続けられる（理由が分からなくなるだけ）。
	}
	return kObjectEventNoErr;
}

// ---------------------------------------------------------------------------
EObjectEvent vwprobe::CPioMatrix_EventSink::OnAddState(ObjectState& stateInfo)
{
	try
	{
		vwprobe::gMatrixLastState = stateInfo.fSpecifier;
		vwprobe::gMatrixHasLastState = true;
	}
	catch (...)
	{
	}
	return VWParametric_EventSink::OnAddState(stateInfo);
}

// ---------------------------------------------------------------------------
EObjectEvent vwprobe::CPioMatrix_EventSink::Recalculate()
{
	static long sMatrixCount = 0;
	++sMatrixCount;

	try
	{
		// **塊の先頭。** ② はこの行で割り、`理由=` を読む（`PioMatrixTrace.h`）。
		vwprobe::pioMatrix::Append(std::string(vwprobe::pioMatrix::kBlockMarker) + " #" +
								   vwprobe::MatrixInt(sMatrixCount) + " 理由=" +
								   (vwprobe::gMatrixHasLastState
										? (vwprobe::MatrixInt(vwprobe::gMatrixLastState) + " " +
										   vwprobe::MatrixStateName(vwprobe::gMatrixLastState))
										: std::string("-1 （OnAddState が来ていない）")));
		vwprobe::gMatrixHasLastState = false;

		if (fhObject == nullptr)
		{
			vwprobe::pioMatrix::AppendPair("入口/fhObject", "nil（何も測れない）");
			return kObjectEventNoErr;
		}

		vwprobe::TraceMatrixReads(fhObject, "入口");
		vwprobe::DrawMatrixGeometry(fhObject);
		vwprobe::TraceMatrixReads(fhObject, "出口");
	}
	catch (...)
	{
		// **例外を VW へ返さない。** 落ちたことだけ残して普通に戻る。
		try
		{
			vwprobe::pioMatrix::AppendPair("入口/例外", "**例外で中断**");
		}
		catch (...)
		{
		}
	}
	return kObjectEventNoErr;
}
