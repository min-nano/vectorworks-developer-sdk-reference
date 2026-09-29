//
//	ExtPioRecalcEnv.cpp
//
//	`ExtPioRecalcEnv.h` の実装。**`Recalculate` の中から SDK を叩いて、見えたものを
//	`PioRecalcTrace.h` のファイルへ 1 行ずつ書き溜めるだけ**の PIO である。
//
//	【絶対に守ること】`Recalculate` は VectorWorks が好きなときに何度でも呼ぶ。
//	  * **例外を境界の外へ出さない**（未捕捉例外は VectorWorks ごと落とす。
//	    Findings「Progress and Diagnostics」）。だから全体を try/catch で包む。
//	  * **ダイアログを出さない**（リセットは取り込み中にも起きる。止めたら取り込みが止まる）。
//	  * **他の図形を変えない**（見て測るだけ。`ResetObject` も呼ばない——`Recalculate` の
//	    中から呼べば入れ子になる）。
//
//	【測る順序に意味がある】プローブが入れる印（「いまから ResetObject する」）と
//	この中の行は**同じファイルの同じ時刻軸**に並ぶ。だから各行の頭には必ず時刻が付く
//	（`PioRecalcTrace::Append`）。
//

#include "PluginPrefix.h"
#include "BuildConfig.h"
#include "ExtPioRecalcEnv.h"
#include "PioRecalcTrace.h"

#include <cmath>
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
		// PIO の定義。サブタイプは**線分**（`kParametricSubType_Linear`）——
		// issue #181 の裏取り（「SDK で登録した線分 PIO のパラメータ表に `LineLength` が
		// 現れるか」）をここで取るため。
		//
		// `ResetOnMove` / `ResetOnRotate` を立てるのは、**動かしただけでも
		// `Recalculate` が来るか**を実機で見たいから（Findings「自作 PIO を足すときの
		// 3 点」の 3 番目そのもの）。
		//
		// 関数ローカル static で持つ理由は SMenuDef と同じ（ProbeMenu.cpp）——
		// SDK 側の非ローカル static を初期化子から参照するため、初期化順序に依らない形に
		// しておく。**配列は拡張が持ち続けるので、寿命はプロセスと同じでなければならない**
		// （`VWExtensionParametric` はポインタを写す）。
		const SParametricDef& parametricDef()
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

		// パラメータ 3 つ。**OIP で編集してもらうのは `TraceNote`**（文字欄なので、
		// 打ち間違えても図が壊れない）。表の終わりは**ユニバーサル名が空の行**で示す
		// （`VWExtensionParametric` の実装がそこで数え終える。`SDKLib/Source/VWSDK/VWFC/
		//  PluginSupport/VWExtensionParametric.cpp:79`）。
		const SParametricParamDef* parametricParams()
		{
			static const SParametricParamDef params[] = {
				{pioTrace::kParamNote, {PLUGIN_VWR_ID, "pioNote"}, "", "", kFieldText, 0},
				{pioTrace::kParamLength,
				 {PLUGIN_VWR_ID, "pioLength"},
				 "12\"",
				 "300",
				 kFieldCoordDisp,
				 0},
				{pioTrace::kParamFlag,
				 {PLUGIN_VWR_ID, "pioFlag"},
				 "False",
				 "False",
				 kFieldBoolean,
				 0},
				{"", {PLUGIN_VWR_ID, "pioName"}, "", "", EFieldStyle(0), 0},
			};
			return params;
		}

		// -------------------------------------------------------------------
		std::string Num(double value)
		{
			char buffer[64];
			(void)std::snprintf(buffer, sizeof(buffer), "%.3f", value);
			return std::string(buffer);
		}

		std::string Int(long value)
		{
			char buffer[32];
			(void)std::snprintf(buffer, sizeof(buffer), "%ld", value);
			return std::string(buffer);
		}

		std::string NameOf(MCObjectHandle h)
		{
			if (h == nullptr)
				return "（nil）";
			TXString name;
			gSDK->GetObjectName(h, name);
			return std::string(static_cast<const char*>(name));
		}

		// リセットの理由（`ObjectState::EStateType`）を名前で出す。**数字だけでは
		// 読めない**ので綴りに直す。値は決め打ちしない——enum の綴りで switch する。
		const char* StateName(Sint32 specifier)
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
				return "kParameterChangedReset（OIP でパラメータを編集）";
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
				return "kObjectExternalReset（外からの ResetObject）";
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
		Sint32 gLastState = -1;
		bool gHasLastState = false;

		// 自分の行列（問い 1）。
		void TraceSelfMatrix(MCObjectHandle self, const std::string& tag)
		{
			VWParametricObj pio(self);
			VWTransformMatrix objectToWorld;
			pio.GetObjectToWorldTransform(objectToWorld);
			const VWPoint3D offset = objectToWorld.GetOffset();
			const VWPoint3D uVector = objectToWorld.GetUVector();
			pioTrace::Append(tag + " GetObjectToWorldTransform: offset=(" + Num(offset.x) + ", " +
							 Num(offset.y) + ", " + Num(offset.z) + ") U=(" + Num(uVector.x) +
							 ", " + Num(uVector.y) + ", " + Num(uVector.z) + ")");

			TransformMatrix entity;
			gSDK->GetEntityMatrix(self, entity);
			pioTrace::Append(tag + " GetEntityMatrix: offset=(" + Num(entity.v1.xOff) + ", " +
							 Num(entity.v1.yOff) + ") i=(" + Num(entity.v1.a00) + ", " +
							 Num(entity.v1.a01) + ")");
		}

		// 自分の両端・長さ・パラメータ表（問い「線分 PIO が自分の長さをどう読むか」）。
		void TraceSelfGeometry(MCObjectHandle self, const std::string& tag)
		{
			VWParametricObj pio(self);

			VWPoint2D ptA;
			VWPoint2D ptB;
			pio.GetLinearObjectPos(ptA, ptB);
			const double dx = ptB.x - ptA.x;
			const double dy = ptB.y - ptA.y;
			pioTrace::Append(tag + " GetLinearObjectPos: A=(" + Num(ptA.x) + ", " + Num(ptA.y) +
							 ") B=(" + Num(ptB.x) + ", " + Num(ptB.y) +
							 ") |AB|=" + Num(std::sqrt(dx * dx + dy * dy)));

			WorldRect bounds;
			if (gSDK->GetObjectBounds(self, bounds))
				pioTrace::Append(tag + " 自分の外接: 幅=" + Num(bounds.right - bounds.left) +
								 " 高=" + Num(bounds.top - bounds.bottom) +
								 " left=" + Num(bounds.left) + " bottom=" + Num(bounds.bottom));
			else
				pioTrace::Append(tag + " 自分の外接: GetObjectBounds が false");

			// パラメータ表を全数。**`LineLength` が在るか**がここで分かる（登録したのは
			// 3 つだけなので、それ以上あれば VW が足したものである）。
			const size_t count = pio.GetParamsCount();
			std::string names;
			for (size_t i = 0; i < count; ++i)
			{
				if (i > 0)
					names += ", ";
				names += static_cast<const char*>(pio.GetParamName(i));
			}
			pioTrace::Append(tag + " パラメータ " + Int(static_cast<long>(count)) +
							 " 個: " + names);

			const size_t lineLengthIndex = pio.GetParamIndex("LineLength");
			if (lineLengthIndex == static_cast<size_t>(-1))
				pioTrace::Append(tag + " LineLength: **表に無い**（GetParamIndex が (size_t)-1）");
			else
				pioTrace::Append(
					tag + " LineLength: 索引=" + Int(static_cast<long>(lineLengthIndex)) +
					" 実数=" + Num(pio.GetParamReal("LineLength")) + " 文字列=\"" +
					std::string(static_cast<const char*>(pio.GetParamValue("LineLength"))) + "\"");
			pioTrace::Append(
				tag + " TraceNote=\"" +
				std::string(static_cast<const char*>(pio.GetParamValue(pioTrace::kParamNote))) +
				"\" TraceLength=" + Num(pio.GetParamReal(pioTrace::kParamLength)));
		}

		// 他のレイヤ（問い 2）と、自分以外の図形（問い 3）。
		void TraceEnvironment(MCObjectHandle self, const std::string& tag)
		{
			const MCObjectHandle activeLayer = gSDK->GetActiveLayer();
			pioTrace::Append(tag + " GetActiveLayer: " + (activeLayer != nullptr ? "有り" : "nil") +
							 " 名前=\"" + NameOf(activeLayer) + "\"");

			const MCObjectHandle other = gSDK->GetNamedLayer(pioTrace::kOtherLayerName);
			pioTrace::Append(std::string(tag) + " GetNamedLayer(\"" + pioTrace::kOtherLayerName +
							 "\"): " + (other != nullptr ? "見つかった" : "**nil**") + " 名前=\"" +
							 NameOf(other) + "\"");

			// (a) 名前で引く。
			const MCObjectHandle target = gSDK->GetNamedObject(pioTrace::kTargetObjectName);
			if (target == nullptr)
			{
				pioTrace::Append(std::string(tag) + " GetNamedObject(\"" +
								 pioTrace::kTargetObjectName + "\"): **nil**");
			}
			else
			{
				WorldRect bounds;
				const Boolean got = gSDK->GetObjectBounds(target, bounds);
				pioTrace::Append(
					std::string(tag) + " GetNamedObject(\"" + pioTrace::kTargetObjectName +
					"\"): 型=" + Int(gSDK->GetObjectTypeN(target)) +
					(got ? (" 外接 left=" + Num(bounds.left) + " bottom=" + Num(bounds.bottom) +
							" 幅=" + Num(bounds.right - bounds.left) +
							" 高=" + Num(bounds.top - bounds.bottom))
						 : std::string(" 外接: GetObjectBounds が false")));
			}

			// (b) レイヤを舐めて探す（取り込みのプラグインがやっているのはこちら）。
			long members = 0;
			bool sawSelf = false;
			bool sawTarget = false;
			if (activeLayer != nullptr)
			{
				for (MCObjectHandle h = gSDK->FirstMemberObj(activeLayer); h != nullptr;
					 h = gSDK->NextObject(h))
				{
					++members;
					if (h == self)
						sawSelf = true;
					TXString name;
					gSDK->GetObjectName(h, name);
					if (std::string(static_cast<const char*>(name)) ==
						std::string(pioTrace::kTargetObjectName))
						sawTarget = true;
				}
			}
			pioTrace::Append(tag + " アクティブレイヤの走査: " + Int(members) +
							 " 件 / 自分が見えた=" + (sawSelf ? "はい" : "**いいえ**") +
							 " / 対象が見えた=" + (sawTarget ? "はい" : "**いいえ**"));
		}

		// 絵は線 1 本だけ（**PIO のローカル座標で描く**。Findings「自作 PIO を足すときの
		// 3 点」の 2 番目）。選べる図形が無いと OIP で編集できないので、描くこと自体が要る。
		void DrawGeometry(MCObjectHandle self)
		{
			VWParametricObj pio(self);
			double length = pio.GetParamReal(pioTrace::kParamLength);
			if (length <= 0.0)
				length = 300.0;
			VWLine2DObj line(VWPoint2D(0.0, 0.0), VWPoint2D(length, 0.0));
			(void)line;
		}
	} // namespace
} // namespace vwprobe

// ---------------------------------------------------------------------------
// 拡張機能の一意な ID とユニバーサル名。**ユニバーサル名は `PioRecalcTrace.h` の
// `kPioUniversalName` と一致させる**（プローブがその綴りで `CreateCustomObject` する）。
//
// NOLINT: IMPLEMENT_VWParametricExtension は SDK のマクロで、展開の中に clang-tidy が
// const を求める `static VWIID iid` がある（マクロ側のコード）。
// NOLINTBEGIN(misc-const-correctness)
// UUID: 6f2a7c41-0d3e-4b58-9a6c-1e7d4f905b12
IMPLEMENT_VWParametricExtension(
	/*Extension class*/ CExtObjPioRecalcEnv,
	/*Event sink*/ CPioRecalcEnv_EventSink,
	/*Universal name*/ "VwSdkProbesRecalcEnv",
	/*Version*/ 1,
	/*UUID*/ 0x6f2a7c41, 0x0d3e, 0x4b58, 0x9a, 0x6c, 0x1e, 0x7d, 0x4f, 0x90, 0x5b, 0x12);
// NOLINTEND(misc-const-correctness)

// ---------------------------------------------------------------------------
vwprobe::CExtObjPioRecalcEnv::CExtObjPioRecalcEnv(CallBackPtr cbp)
	: VWExtensionParametric(cbp, vwprobe::parametricDef(), vwprobe::parametricParams())
{
}

vwprobe::CExtObjPioRecalcEnv::~CExtObjPioRecalcEnv() = default;

// ---------------------------------------------------------------------------
vwprobe::CPioRecalcEnv_EventSink::CPioRecalcEnv_EventSink(IVWUnknown* parent)
	: VWParametric_EventSink(parent)
{
}

vwprobe::CPioRecalcEnv_EventSink::~CPioRecalcEnv_EventSink() = default;

// ---------------------------------------------------------------------------
// **`kObjXPropAcceptStates` を立てるのがここの全部。** これが無いと
// `ObjectState::kAction` が来ないので、リセットの理由が分からない
// （`Info/Parametric Extended Properties.md`）。
EObjectEvent vwprobe::CPioRecalcEnv_EventSink::OnInitXProperties(CodeRefID objectID)
{
	try
	{
		using namespace VectorWorks::Extension;
		VCOMPtr<IExtendedProps> extProps(IID_ExtendedProps);
		if (extProps != nullptr)
		{
			(void)extProps->SetObjectProperty(objectID, kObjXPropAcceptStates, true);
			// 印刷・書き出しの直前にも作り直させる（Findings「自作 PIO を足すときの 3 点」）。
			(void)extProps->SetObjectProperty(objectID, kObjXPropResetBeforeExport, true);
		}
	}
	catch (...)
	{
		// ここで落ちても調査は続けられる（理由が分からなくなるだけ）。
	}
	return kObjectEventNoErr;
}

// ---------------------------------------------------------------------------
EObjectEvent vwprobe::CPioRecalcEnv_EventSink::OnAddState(ObjectState& stateInfo)
{
	try
	{
		vwprobe::gLastState = stateInfo.fSpecifier;
		vwprobe::gHasLastState = true;
		vwprobe::pioTrace::Append(std::string("OnAddState: fSpecifier=") +
								  vwprobe::Int(stateInfo.fSpecifier) + " " +
								  vwprobe::StateName(stateInfo.fSpecifier));
	}
	catch (...)
	{
	}
	return VWParametric_EventSink::OnAddState(stateInfo);
}

// ---------------------------------------------------------------------------
EObjectEvent vwprobe::CPioRecalcEnv_EventSink::Recalculate()
{
	static long sCount = 0;
	++sCount;
	const std::string tag = "  [Recalculate #" + vwprobe::Int(sCount) + "]";

	try
	{
		vwprobe::pioTrace::Append(
			"Recalculate 開始 #" + vwprobe::Int(sCount) + " 直前の OnAddState=" +
			(vwprobe::gHasLastState ? (vwprobe::Int(vwprobe::gLastState) + " " +
									   vwprobe::StateName(vwprobe::gLastState))
									: std::string("**来ていない**")));
		vwprobe::gHasLastState = false;

		if (fhObject == nullptr)
		{
			vwprobe::pioTrace::Append(tag + " fhObject が nil（何も測れない）");
			return kObjectEventNoErr;
		}

		vwprobe::TraceSelfMatrix(fhObject, tag);
		vwprobe::TraceSelfGeometry(fhObject, tag);
		vwprobe::TraceEnvironment(fhObject, tag);
		vwprobe::DrawGeometry(fhObject);
		vwprobe::pioTrace::Append(tag + " 終わり");
	}
	catch (...)
	{
		// **例外を VW へ返さない。** 落ちたことだけ残して普通に戻る。
		try
		{
			vwprobe::pioTrace::Append(tag + " **例外で中断**");
		}
		catch (...)
		{
		}
	}
	return kObjectEventNoErr;
}
