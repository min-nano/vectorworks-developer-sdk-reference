//
//	probes/runtime/linear-pio-endpoints/probe.cpp
//
//	[issue #181] 線分 PIO（kParametricSubType_Linear）の両端・長さを SDK から与える／読む
//	口を実測する。
//
//	確かめること:
//	  (1) `VWParametricObj::SetLinearObjectPos`（＝`GS_SetEndPoints`）は PIO に効くか。
//	  (2) `GetLinearObjectPos`（＝`GS_GetEndPoints`）は PIO で何を返すか。
//	  (3) 隠しパラメータ `LineLength`（`Info/Parametric Object Types.md`）はパラメータ表に
//	      在るか。在れば `SetParamReal` で書けるか・外接に効くか。
//
//	SDK 側はここまで分かっている【ヘッダ根拠】: VWFC の Get/SetLinearObjectPos は
//	GS_Get/SetEndPoints を素通しで呼ぶだけ（判定も戻り値も無い）で、その GS_* も
//	コールバックを呼ぶだけの trampoline。SDK 内で同じ口を使っているのは線分
//	（VWLine2DObj）と壁（VWWallObj）だけ。つまり「PIO に効くか」はここでは決まらない。
//
//	被験体は組み込みの PIO。**どの綴りが当たるかも調査の一部**なので、候補を順に試して
//	当たらなかったものも結果に残す（綴りは MiniCadHookIntf.h の kInternalID_* から起こした）。
//	対照として、線分でないと分かっている PIO（2D パスの StructuralMember・点の Door）も
//	同じ手順に掛ける。
//

#include "Probe.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	// 被験体の候補。上が「線分らしい」もの、末尾 2 つが対照。
	const char* const kProbeCandidateNames[] = {
		"Break Line",
		"BreakLine",
		"Straight Truss",
		"Straight Guardrail",
		"Straight Handrail",
		"Joist",
		"Framing Member",
		"Clothes Rod",
		"Grab Bars",
		"Scale Bar",
		"Leader Line",
		"Center Line Marker",
		"Linear Material",
		"Lighting Pipe",
		"Irrigation Line",
		"Simple Ramp",
		// 対照（線分ではない）
		"StructuralMember",
		"Door",
	};

	const short kProbeOvParametricInternalID = 1165;

	std::string ProbeNum(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.3f", value);
		return std::string(buffer);
	}

	std::string ProbeInt(long value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%ld", value);
		return std::string(buffer);
	}

	// プラグインフォルダに実在する定義のファイル名を舐めて、**線分らしい綴りだけ**を出す。
	// 候補の綴りを全部外したときに、次の round で使う正しい名前がここから拾えるように。
	const char* const kProbeStemHints[] = {
		"Line", "Straight", "Joist",  "Truss", "Rod",  "Bar",
		"Duct", "Pipe",		"Member", "Rail",  "Beam", "Linear",
	};

	bool ProbeStemLooksLinear(const std::string& stem)
	{
		const size_t hintCount = sizeof(kProbeStemHints) / sizeof(kProbeStemHints[0]);
		for (size_t i = 0; i < hintCount; ++i)
			if (stem.find(kProbeStemHints[i]) != std::string::npos)
				return true;
		return false;
	}

	// PIO の内部 ID（ovParametricInternalID）。読めなければ -1。
	long ProbeInternalID(MCObjectHandle h)
	{
		TVariableBlock block;
		if (!gSDK->GetObjectVariable(h, kProbeOvParametricInternalID, block))
			return -1;
		Sint16 id = 0;
		if (!block.GetSint16(id))
			return -1;
		return static_cast<long>(id);
	}

	// 両端・行列・外接を 3 行で出す。**知りたいものは呼び出しの前に出しておく**ので、
	// タグには「どの段か」を必ず入れる。
	void ProbeDumpGeometry(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		VWPoint2D ptA;
		VWPoint2D ptB;
		VWParametricObj pio(h);
		pio.GetLinearObjectPos(ptA, ptB);

		TransformMatrix matrix;
		gSDK->GetEntityMatrix(h, matrix);

		WorldRect bounds;
		const Boolean gotBounds = gSDK->GetObjectBounds(h, bounds);

		const double dx = ptB.x - ptA.x;
		const double dy = ptB.y - ptA.y;

		probe.log(tag + " GetLinearObjectPos: A=(" + ProbeNum(ptA.x) + ", " + ProbeNum(ptA.y) +
				  ") B=(" + ProbeNum(ptB.x) + ", " + ProbeNum(ptB.y) +
				  ") |AB|=" + ProbeNum(std::sqrt(dx * dx + dy * dy)));
		probe.log(tag + " 行列: offset=(" + ProbeNum(matrix.v1.xOff) + ", " +
				  ProbeNum(matrix.v1.yOff) + ") i=(" + ProbeNum(matrix.v1.a00) + ", " +
				  ProbeNum(matrix.v1.a01) + ")");
		if (gotBounds)
			probe.log(tag + " 外接: 幅=" + ProbeNum(bounds.right - bounds.left) +
					  " 高=" + ProbeNum(bounds.top - bounds.bottom) +
					  " left=" + ProbeNum(bounds.left) + " bottom=" + ProbeNum(bounds.bottom));
		else
			probe.log(tag + " 外接: GetObjectBounds が false");
	}

	// パラメータが表に在るか、在るなら欄型と値を出す。在るかどうかが結論なので、
	// 無いときも 1 行残す。
	void ProbeDumpParam(vwprobe::Report& probe, MCObjectHandle h, const char* paramName)
	{
		VWParametricObj pio(h);
		const size_t index = pio.GetParamIndex(paramName);
		if (index == static_cast<size_t>(-1))
		{
			probe.log(std::string("    パラメータ ") + paramName +
					  ": **表に無い**（GetParamIndex が (size_t)-1）");
			return;
		}
		probe.log(std::string("    パラメータ ") + paramName +
				  ": 索引=" + ProbeInt(static_cast<long>(index)) + " 欄型=" +
				  ProbeInt(static_cast<long>(pio.GetParamStyle(paramName))) + " 値文字列=\"" +
				  std::string(static_cast<const char*>(pio.GetParamValue(paramName))) +
				  "\" 実数=" + ProbeNum(pio.GetParamReal(paramName)));
	}
} // namespace

VW_PROBE("linear-pio-endpoints", "線分 PIO の両端と長さを与えて読み戻す",
		 "組み込み PIO を被験体に Set/GetLinearObjectPos と LineLength を実測する")
{
	// --- 0) プラグインフォルダに実在する定義の綴りを拾う ---------------------
	// 組み込み PIO の universal 名は SDK から引けないので、**候補の綴りを全部外したときの
	// 逃げ道**として、実在するファイル名（線分らしいものだけ）をここで出しておく。
	{
		size_t shown = 0;
		size_t total = 0;
		const EForEachFileResult walked = gSDK->ForEachFilePathInPluginFolderN(
			[&probe, &shown, &total](const char* fullPath, const char* fileName,
									 const char* fileExtension) -> EForEachFileResult
			{
				(void)fullPath;
				++total;
				const std::string ext(fileExtension != nullptr ? fileExtension : "");
				const std::string name(fileName != nullptr ? fileName : "");
				if (ext != "vso" && ext != "vst")
					return kContinueForEachFile;
				const size_t dot = name.rfind('.');
				const std::string stem = (dot == std::string::npos) ? name : name.substr(0, dot);
				if (!ProbeStemLooksLinear(stem))
					return kContinueForEachFile;
				if (shown < 60)
					probe.log("  実在: \"" + stem + "\" (." + ext + ")");
				++shown;
				return kContinueForEachFile;
			});
		probe.log("[0] プラグインフォルダ: 走査結果=" + ProbeInt(static_cast<long>(walked)) +
				  " 全ファイル=" + ProbeInt(static_cast<long>(total)) + " 線分らしい定義=" +
				  ProbeInt(static_cast<long>(shown)) + "（60 件まで上に出した）");
	}

	const size_t candidateCount = sizeof(kProbeCandidateNames) / sizeof(kProbeCandidateNames[0]);
	size_t subjectCount = 0;

	for (size_t i = 0; i < candidateCount; ++i)
	{
		const char* const name = kProbeCandidateNames[i];

		// **作る前に名前をログへ出す**——ここで落ちたらどの候補で落ちたかが残るように。
		// まず GetPluginType で綴りを確かめる（図形を作らずに済むので安い）。
		EVSPluginType pluginType = kVSPluginMenu;
		const Boolean isPlugin = gSDK->GetPluginType(name, pluginType);
		probe.log(std::string("[候補] \"") + name +
				  "\": GetPluginType=" + (isPlugin ? "true" : "false") +
				  " 種別=" + ProbeInt(static_cast<long>(pluginType)) + "（2=オブジェクト）");
		if (!isPlugin || pluginType != kVSPluginObject)
		{
			probe.log("  → この綴りのオブジェクトプラグインは無い。飛ばす");
			continue;
		}

		// ダイアログを止めてから作る（止めないと 1 個目で待たされて先へ進めない）。
		probe.log("  DefineCustomObject → CreateCustomObject を呼ぶ");
		const MCObjectHandle definition = gSDK->DefineCustomObject(name, kCustomObjectPrefNever);
		if (definition == nil)
		{
			probe.log("  → 定義が引けなかった。飛ばす");
			continue;
		}

		probe.log(std::string("  → 定義あり。CreateCustomObject(\"") + name +
				  "\", (10000, 5000), 0°) を呼ぶ");
		const MCObjectHandle h = gSDK->CreateCustomObject(name, WorldPt(10000, 5000), 0.0, true);
		if (h == nil)
		{
			probe.log("  → CreateCustomObject が nil。飛ばす");
			continue;
		}
		++subjectCount;

		VWParametricObj pio(h);
		probe.log(std::string("  素性: universal 名=\"") +
				  std::string(static_cast<const char*>(pio.GetParametricName())) + "\" 内部 ID=" +
				  ProbeInt(ProbeInternalID(h)) + " 型=" + ProbeInt(gSDK->GetObjectTypeN(h)) +
				  " パラメータ数=" + ProbeInt(static_cast<long>(pio.GetParamsCount())));
		ProbeDumpParam(probe, h, "LineLength");
		ProbeDumpParam(probe, h, "BoxWidth");
		ProbeDumpGeometry(probe, "  [1 作成直後]", h);

		// --- (3) LineLength を実数で書く -------------------------------------
		if (pio.GetParamIndex("LineLength") != static_cast<size_t>(-1))
		{
			probe.log("  [2] SetParamReal(\"LineLength\", 4321) を呼ぶ");
			pio.SetParamReal("LineLength", 4321.0);
			probe.log("  [2 書いた直後] LineLength 読み戻し=" +
					  ProbeNum(pio.GetParamReal("LineLength")));
			gSDK->ResetObject(h);
			probe.log("  [2 ResetObject 後] LineLength 読み戻し=" +
					  ProbeNum(pio.GetParamReal("LineLength")));
			ProbeDumpGeometry(probe, "  [2 ResetObject 後]", h);
		}

		// --- (1) SetLinearObjectPos で両端を与える ---------------------------
		// (10000, 5000) → (13000, 9000)。長さ 5000・角度 atan2(4, 3) = 53.13°。
		probe.log("  [3] SetLinearObjectPos((10000, 5000), (13000, 9000)) を呼ぶ"
				  "（長さ 5000・53.130°）");
		pio.SetLinearObjectPos(VWPoint2D(10000, 5000), VWPoint2D(13000, 9000));
		ProbeDumpGeometry(probe, "  [3 書いた直後]", h);
		if (pio.GetParamIndex("LineLength") != static_cast<size_t>(-1))
			probe.log("  [3 書いた直後] LineLength 読み戻し=" +
					  ProbeNum(pio.GetParamReal("LineLength")));
		gSDK->ResetObject(h);
		ProbeDumpGeometry(probe, "  [3 ResetObject 後]", h);
		if (pio.GetParamIndex("LineLength") != static_cast<size_t>(-1))
			probe.log("  [3 ResetObject 後] LineLength 読み戻し=" +
					  ProbeNum(pio.GetParamReal("LineLength")));

		// --- 対照: ISDK の SetEndPoints を直に叩いても同じか ------------------
		probe.log("  [4] gSDK->SetEndPoints((10000, 5000), (10000, 12000)) を直に呼ぶ"
				  "（長さ 7000・90°）");
		gSDK->SetEndPoints(h, WorldPt(10000, 5000), WorldPt(10000, 12000));
		gSDK->ResetObject(h);
		ProbeDumpGeometry(probe, "  [4 ResetObject 後]", h);
	}

	probe.log("---");
	probe.log("被験体になった PIO の数=" + ProbeInt(static_cast<long>(subjectCount)) +
			  " / 試した候補=" + ProbeInt(static_cast<long>(candidateCount)));
	if (subjectCount == 0)
		probe.fail("候補のどれも定義が引けなかった——組み込み PIO の綴りを変えて出し直す");
}
