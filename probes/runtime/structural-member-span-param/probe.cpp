//
//	probes/runtime/structural-member-span-param/probe.cpp
//
//	[issue #95] 構造材 PIO（StructuralMember）に「スパン」「部材長」を読めるパラメータが
//	在るのか無いのかを、**パラメータ表の全数列挙**で確定させる。
//
//	なぜ実機なのか
//	--------------
//	VW 標準 PIO のパラメータ表は SDK ヘッダには無い（プラグイン定義として VW 本体が
//	持っており、文書へ焼き込まれる）。`sdk-grep` で確かめられるのは「SDK に Span という
//	識別子が在るか」までで、**パラメータ表そのものは実機で PIO を 1 本作って読むしか
//	見る手立てが無い**。
//
//	確かめること
//	------------
//	  1) 構造材 PIO のパラメータを **1 件残らず** 列挙する（索引・universal 名・
//	     ローカライズ名・型・値）。「別の綴り」「別のローカライズ名」で在る可能性を
//	     名前の照合ではなく全数列挙で潰す。
//	  2) 列挙した値のうち、**こちらが与えた部材長と一致するものが在るか**を機械で
//	     突き合わせる（名前が想像と違っても、値が長さなら見つかる）。
//	  3) 水平材（2D ポリライン）と鉛直材（NURBS）の両方で行う——issue の実機報告は
//	     柱（鉛直材）のものなので、水平材でパラメータ表が変わらないことも確かめる。
//	  4) 参考として、`GetCustomObjectPath` で読み戻したパスの両端の距離を測る
//	     （「パラメータではなくパスで測る」という当面の手立ての裏取り）。
//
//	新規の空図面で走らせる前提（プローブは undo イベントを開かない）。
//

#include "Probe.h"

#include "VWFC/VWObjects/VWParametricObj.h"
#include "VWFC/VWObjects/VWPolygon2DObj.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	// 構造材 PIO の universal 名。Findings「Parametric Objects」で使っているものと同じ。
	const char* const kStructuralMemberName = "StructuralMember";

	// こちらが与える部材長。パラメータの値と突き合わせるので、**他の既定値と紛れない
	// 半端な値**にしておく（3000 のような丸い値だと「たまたま一致」が出かねない）。
	const double kHorizontalLength = 3137.0;
	const double kVerticalLength = 2411.0;

	// 値の一致判定。図面単位（mm）での読み書きなので、絶対誤差で足りる。
	const double kMatchTolerance = 0.5;

	std::string Num(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.6g", value);
		return std::string(buffer);
	}

	std::string Utf8(const TXString& text)
	{
		return std::string(static_cast<const char*>(text));
	}

	// EFieldStyle（Kernel/API/MiniCadCallBacks.h）を読める名前にする。
	std::string FieldStyleName(short style)
	{
		switch (style)
		{
		case kFieldLongInt:
			return "整数";
		case kFieldBoolean:
			return "真偽";
		case kFieldReal:
			return "実数";
		case kFieldText:
			return "文字列";
		case kFieldCoordDisp:
			return "座標（距離）";
		case kFieldPopUp:
			return "ポップアップ";
		case kFieldRadio:
			return "ラジオ";
		case kFieldCoordLocX:
			return "座標X";
		case kFieldCoordLocY:
			return "座標Y";
		case kFieldStaticText:
			return "静的テキスト";
		case kFieldControlPoint:
			return "コントロールポイント";
		case kFieldDimStdPopUp:
			return "寸法規格ポップアップ";
		case kFieldPrecisionPopUp:
			return "精度ポップアップ";
		case kFieldClassesPopup:
			return "クラスポップアップ";
		case kFieldLayersPopup:
			return "レイヤポップアップ";
		case kFieldAngle:
			return "角度";
		case kFieldArea:
			return "面積";
		case kFieldVolume:
			return "体積";
		case kFieldClass:
			return "クラス";
		case kFieldBuildingMaterial:
			return "建材";
		case kFieldFill:
			return "塗り";
		case kFieldPenStyle:
			return "線種";
		case kFieldPenWeight:
			return "線の太さ";
		case kFieldColor:
			return "色";
		case kFieldTexture:
			return "テクスチャ";
		case kFieldSymDef:
			return "シンボル定義";
		case kFieldDimUnitPopUp:
			return "寸法単位ポップアップ";
		default:
			break;
		}
		return "不明(" + std::to_string(static_cast<int>(style)) + ")";
	}

	// 値の文字列を数として読めるなら読む。読めなければ false。
	// 「12.5mm」のように単位が付いて返る型があるので、先頭から読める分だけを見る。
	bool ParseNumber(const std::string& text, double& outValue)
	{
		const char* begin = text.c_str();
		char* end = nullptr;
		const double value = std::strtod(begin, &end);
		if (end == begin)
			return false;
		outValue = value;
		return true;
	}

	bool ContainsUtf8(const std::string& haystack, const char* needle)
	{
		return haystack.find(needle) != std::string::npos;
	}

	// 2 点間の距離。
	double Distance(const WorldPt3& a, const WorldPt3& b)
	{
		const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
		const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
		const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	// ---------------------------------------------------------------------
	// パラメータ表の全数列挙。**ここがこのプローブの本体**。
	void DumpParameters(vwprobe::Report& probe, MCObjectHandle object, const char* label,
						double expectedLength)
	{
		probe.log("");
		probe.log(std::string("===== ") + label + " のパラメータ全数 =====");

		VWParametricObj pio(object);
		probe.log("PIO 名: " + Utf8(pio.GetParametricName()) +
				  " / ローカライズ名: " + Utf8(pio.GetLocalizedParametricName()));

		const size_t count = pio.GetParamsCount();
		probe.log("パラメータ数: " + std::to_string(count));
		if (count == 0)
		{
			probe.fail(std::string(label) + ": パラメータが 1 件も取れなかった"
											"（列挙そのものが効いていない見込み）");
			return;
		}

		std::vector<std::string> nameHits; // 名前が「スパン / Span / 長」に触れるもの
		std::vector<std::string> valueHits; // 値が部材長と一致するもの

		for (size_t i = 0; i < count; ++i)
		{
			std::string universal;
			std::string localized;
			std::string style = "(型が読めない)";
			std::string value = "(値が読めない)";

			try
			{
				universal = Utf8(pio.GetParamName(i));
				localized = Utf8(pio.GetParamLocalizedName(i));
			}
			catch (...)
			{
				probe.log("[" + std::to_string(i) + "] 名前が読めない（例外）");
				continue;
			}

			try
			{
				style = FieldStyleName(static_cast<short>(pio.GetParamStyle(universal.c_str())));
			}
			catch (...)
			{
			}

			try
			{
				value = Utf8(pio.GetParamAsString(universal.c_str()));
			}
			catch (...)
			{
			}

			probe.log("[" + std::to_string(i) + "] " + universal + "(" + localized +
					  ") 型=" + style + " 値=" + value);

			if (ContainsUtf8(universal, "Span") || ContainsUtf8(universal, "span") ||
				ContainsUtf8(universal, "Length") || ContainsUtf8(universal, "Len") ||
				ContainsUtf8(localized, "スパン") || ContainsUtf8(localized, "長") ||
				ContainsUtf8(localized, "丈"))
			{
				nameHits.push_back(universal + "(" + localized + ")=" + value);
			}

			double numeric = 0.0;
			if (ParseNumber(value, numeric) &&
				std::fabs(numeric - expectedLength) <= kMatchTolerance)
			{
				valueHits.push_back(universal + "(" + localized + ")=" + value);
			}
		}

		probe.log("");
		probe.log(std::string("--- ") + label + " の突き合わせ ---");
		probe.log("与えた部材長: " + Num(expectedLength));

		if (nameHits.empty())
			probe.log("名前が「スパン / Span / 長さ」に触れるパラメータ: 1 件も無い");
		else
		{
			probe.log("名前が「スパン / Span / 長さ」に触れるパラメータ: " +
					  std::to_string(nameHits.size()) + " 件");
			for (const std::string& hit : nameHits)
				probe.log("  * " + hit);
		}

		if (valueHits.empty())
			probe.log("値が部材長と一致するパラメータ: 1 件も無い"
					  "（＝パラメータ側に部材長を読む道は無い）");
		else
		{
			probe.log("値が部材長と一致するパラメータ: " + std::to_string(valueHits.size()) +
					  " 件");
			for (const std::string& hit : valueHits)
				probe.log("  * " + hit);
		}
	}

	// ---------------------------------------------------------------------
	// 参考: パスの両端の距離（「パラメータではなくパスで測る」の裏取り）。
	void MeasurePath(vwprobe::Report& probe, MCObjectHandle object, const char* label)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(object);
		if (path == nil)
		{
			probe.log(std::string(label) + ": GetCustomObjectPath が nil を返した");
			return;
		}

		const short pathType = gSDK->GetObjectTypeN(path);
		probe.log(std::string(label) +
				  ": パスのオブジェクト種別=" + std::to_string(static_cast<int>(pathType)));

		// NURBS として読めるならそちらで測る（鉛直材）。
		const Sint32 pieces = gSDK->NurbsCurveGetNumPieces(path);
		if (pieces > 0)
		{
			for (Sint32 piece = 0; piece < pieces; ++piece)
			{
				const Sint32 points = gSDK->NurbsGetNumPts(path, piece);
				probe.log(std::string(label) + ": NURBS piece" + std::to_string(piece) + " は " +
						  std::to_string(points) + " 点");
				if (points < 2)
					continue;

				WorldPt3 first(0, 0, 0);
				WorldPt3 last(0, 0, 0);
				if (gSDK->NurbsGetPt3D(path, piece, 0, first) &&
					gSDK->NurbsGetPt3D(path, piece, points - 1, last))
				{
					probe.log(std::string(label) + ": 両端 (" + Num(first.x) + ", " + Num(first.y) +
							  ", " + Num(first.z) + ") → (" + Num(last.x) + ", " + Num(last.y) +
							  ", " + Num(last.z) + ") 距離=" + Num(Distance(first, last)));
				}
			}
			return;
		}

		// 2D ポリライン（水平材）。
		if (!VWPolygon2DObj::IsPoly2DObject(path))
		{
			probe.log(std::string(label) + ": NURBS でも 2D ポリラインでもないパス");
			return;
		}

		VWPolygon2DObj poly(path);
		const size_t vertices = poly.GetVertexCount();
		probe.log(std::string(label) + ": 2D ポリラインは " + std::to_string(vertices) + " 頂点");
		if (vertices >= 2)
		{
			const VWPoint2D first = poly.GetVertexPoint(0);
			const VWPoint2D last = poly.GetVertexPoint(vertices - 1);
			const double dx = last.x - first.x;
			const double dy = last.y - first.y;
			probe.log(std::string(label) + ": 両端 (" + Num(first.x) + ", " + Num(first.y) +
					  ") → (" + Num(last.x) + ", " + Num(last.y) +
					  ") 距離=" + Num(std::sqrt(dx * dx + dy * dy)));
		}
	}
} // namespace

VW_PROBE("structural-member-span-param",
		 "構造材 PIO のパラメータを全数列挙する（スパン／部材長は在るか）",
		 "水平材・鉛直材を 1 本ずつ作り、パラメータの索引・universal 名・ローカライズ名・"
		 "型・値を 1 件残らず出して、与えた部材長と一致する値が在るかを突き合わせる")
{
	probe.log("構造材 PIO の定義を先に作る（設定ダイアログを抑止する）");
	gSDK->DefineCustomObject(kStructuralMemberName, kCustomObjectPrefNever);

	// --- 水平材（2D ポリライン） ---------------------------------------
	probe.log("水平材のパスを作る: (0, 0) → (" + Num(kHorizontalLength) + ", 0)");
	MCObjectHandle horizontalPath = nil;
	{
		VWPolygon2DObj poly;
		poly.AddVertex(0.0, 0.0);
		poly.AddVertex(kHorizontalLength, 0.0);
		horizontalPath = poly.GetThisObject();
	}
	if (horizontalPath == nil)
	{
		probe.fail("水平材のパス（2D ポリライン）を作れなかった");
		return;
	}

	MCObjectHandle horizontal =
		gSDK->CreateCustomObjectPath(kStructuralMemberName, horizontalPath, nil, true);
	if (horizontal == nil)
		probe.fail("水平材の構造材 PIO を作れなかった");

	// --- 鉛直材（NURBS） -----------------------------------------------
	probe.log("鉛直材のパスを作る: (0, 0, 0) → (0, 0, " + Num(kVerticalLength) + ")");
	MCObjectHandle verticalPath = gSDK->CreateNurbsCurve(WorldPt3(0, 0, 0), true, 3);
	if (verticalPath == nil)
	{
		probe.fail("鉛直材のパス（NURBS）を作れなかった");
	}
	else
	{
		gSDK->Add3DVertex(verticalPath, WorldPt3(0, 0, kVerticalLength), true);
	}

	MCObjectHandle vertical = nil;
	if (verticalPath != nil)
	{
		vertical = gSDK->CreateCustomObjectPath(kStructuralMemberName, verticalPath, nil, true);
		if (vertical == nil)
			probe.fail("鉛直材の構造材 PIO を作れなかった");
	}

	// --- 本題: パラメータの全数列挙 -------------------------------------
	if (horizontal != nil)
	{
		DumpParameters(probe, horizontal, "水平材", kHorizontalLength);
		MeasurePath(probe, horizontal, "水平材");
	}
	if (vertical != nil)
	{
		DumpParameters(probe, vertical, "鉛直材", kVerticalLength);
		MeasurePath(probe, vertical, "鉛直材");
	}

	probe.log("");
	probe.log("列挙はここまで。上の一覧に「スパン」に当たる行が 1 つも無ければ、"
			  "OIP の「スパン」欄はパラメータではないことになる。");
}
