//
//	probes/runtime/pio-matrix-reset/probe.cpp
//
//	[issue #185] **原点から離し・回転させた PIO** を作り、「取り込み時と同じ経路
//	（`CreateCustomObject` → `ResetObject`）」の `Recalculate` の中で**行列を読む 4 つの口**が
//	何を返すかを記録する ①。
//
//	【なぜ測り直すのか】issue #183 は同じことを測ったが、被験 PIO が
//	**offset=(0,0,0) U=(1,0,0)**（原点・無回転）に置かれていた。だから「編集時だけ行列が
//	単位行列に化ける」が起きていても**区別が付かない**。ここでは
//	`(5000, 3000)`・`30°` に置くので、単位行列との見分けが数値で付く。
//
//	【測る口は 4 つ】SDK の実装（`sdk-grep`）では
//	`VWParametricObj::GetObjectToWorldTransform` と `VWObject::GetObjectMatrix` が
//	**同じ `GS_GetEntityMatrix(…, bUseLegacyZ=true)` に落ち**、`gSDK->GetEntityMatrix` と
//	`VWObject::GetObjectModelMatrix` が**ISDK の口**に落ちる。**「口を変えれば直る」のか
//	「どの口でも同じ」なのかを 1 回の実行で決める**ため、4 つ並べて記録する。
//
//	【測る側は殻にいる】`Recalculate` に立てられるのは自前の PIO の中だけで、その登録は
//	殻（`plugin/src/ExtPioMatrix.cpp`）にしか置けない。だから**このプローブは「印を入れて、
//	溜まった行を吐き出す」係**で、実際に SDK を叩いているのは殻の側である（受け渡しは
//	`plugin/src/PioMatrixTrace.h` のファイル 1 本）。
//
//	【このプローブは公開ビルドでは動かない】殻が変わっているので、**PR の Actions の
//	成果物を手で入れてもらう**（`plugin/README.md`「PR のビルドを手で入れて確かめるとき」）。
//
//	【2 本で 1 組】OIP 編集の側は `pio-matrix-oip` が受け持つ。こちらを走らせた後、
//	利用者が OIP でパラメータを編集し、そのあとで向こうを走らせる。
//

#include "Probe.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace
{
	// -------------------------------------------------------------------
	// **殻（plugin/src/PioMatrixTrace.h）と同じ約束事を、ここへ書き写したもの。**
	//
	// 【なぜ include しないのか】公開ビルドは **main の殻 ＋ 各 PR の
	// `probes/runtime/` だけ**で組まれる（`scripts/gather-probes.sh` は PR の木から
	// `probes/runtime` しか取り出さない）。だから**プローブは、同じ PR で足した殻の
	// ヘッダを include できない**（`probes/runtime/README.md` の「決まり」）。
	// 値を変えるときは `plugin/src/PioMatrixTrace.h` と**両方**直すこと。
	constexpr const char* kMatrixPioUniversalName = "VwSdkProbesMatrix";
	constexpr const char* kMatrixPioObjectName = "VwSdkProbes-MatrixPio";
	constexpr const char* kMatrixParamNote = "TraceNote";
	constexpr const char* kMatrixParamLength = "TraceLength";
	constexpr double kMatrixPlaceX = 5000.0;
	constexpr double kMatrixPlaceY = 3000.0;
	constexpr double kMatrixPlaceAngleDeg = 30.0;
	constexpr double kMatrixProbeWorldX = 1500.0;
	constexpr double kMatrixProbeWorldY = 2000.0;
	constexpr const char* kMatrixSeparator = " ## ";
	constexpr const char* kMatrixTruthPrefix = "TRUTH ";
	constexpr const char* kMatrixAbortMarker = "!!! (1) 中止: この殻に調査用 PIO が入っていない";

	// 書き溜め先（殻の `pioMatrix::Path()` と同じ規則）。
	std::string MatrixTracePath()
	{
		const char* custom = std::getenv("VW_PROBE_PIO_MATRIX");
		if (custom != nullptr && custom[0] != '\0')
			return std::string(custom);
#if defined(_WINDOWS)
		const char* env = std::getenv("TEMP");
		if (env == nullptr || env[0] == '\0')
			env = std::getenv("TMP");
		std::string dir = (env != nullptr && env[0] != '\0') ? std::string(env)
															 : std::string("C:\\Windows\\Temp");
		const char separator = '\\';
#else
		const char* env = std::getenv("TMPDIR");
		std::string dir =
			(env != nullptr && env[0] != '\0') ? std::string(env) : std::string("/tmp");
		const char separator = '/';
#endif
		if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
			dir.pop_back();
		return dir + separator + "VwSdkProbes-pio-matrix-trace.log";
	}

	// 溜まった行を読む（無ければ空）。
	std::vector<std::string> MatrixTraceRead()
	{
		std::vector<std::string> lines;
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
		std::FILE* file = std::fopen(MatrixTracePath().c_str(), "rb");
		if (file == nullptr)
			return lines;
		std::string current;
		int character = 0;
		while ((character = std::fgetc(file)) != EOF)
		{
			if (character == '\n')
			{
				lines.push_back(current);
				current.clear();
				continue;
			}
			if (character != '\r')
				current.push_back(static_cast<char>(character));
		}
		if (!current.empty())
			lines.push_back(current);
		(void)std::fclose(file);
		return lines;
	}

	// 印を 1 行足す（殻の `pioMatrix::Append()` と同じ形。時刻の印も同じ規則）。
	void MatrixTraceAppend(const std::string& line)
	{
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
		std::FILE* file = std::fopen(MatrixTracePath().c_str(), "ab");
		if (file == nullptr)
			return;
		const auto now = std::chrono::system_clock::now();
		const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
		const auto millis =
			std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
			1000;
		std::tm parts{};
#if defined(_WINDOWS)
		(void)localtime_s(&parts, &seconds);
#else
		(void)localtime_r(&seconds, &parts);
#endif
		char stamp[32];
		(void)std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d", parts.tm_hour,
							parts.tm_min, parts.tm_sec, static_cast<int>(millis));
		const std::string text = std::string(stamp) + " " + line + "\n";
		(void)std::fwrite(text.data(), 1, text.size(), file);
		(void)std::fclose(file);
	}

	// 空にする（調査を始めるときに 1 度だけ）。
	bool MatrixTraceReset()
	{
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
		std::FILE* file = std::fopen(MatrixTracePath().c_str(), "wb");
		if (file == nullptr)
			return false;
		(void)std::fclose(file);
		return true;
	}

	// -------------------------------------------------------------------
	// **殻と同じ桁数・同じ綴りで書く。** 突き合わせは文字列比較なので、ここが違うと
	// 「外と中で違う」と誤って出る（`plugin/src/ExtPioMatrix.cpp` の `MatrixNum` ほか）。
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

	std::string MatrixPointText(const VWPoint3D& point)
	{
		return "(" + MatrixNum(point.x) + ", " + MatrixNum(point.y) + ", " + MatrixNum(point.z) +
			   ")";
	}

	std::string MatrixDescribeText(const VWTransformMatrix& matrix)
	{
		return "off=" + MatrixPointText(matrix.GetOffset()) +
			   " U=" + MatrixPointText(matrix.GetUVector()) +
			   " V=" + MatrixPointText(matrix.GetVVector()) +
			   " W=" + MatrixPointText(matrix.GetWVector()) +
			   " 恒等=" + (matrix.IsIdentity() ? "はい" : "いいえ");
	}

	std::string MatrixDescribeRawText(const TransformMatrix& matrix)
	{
		return "off=(" + MatrixNum(matrix.v1.xOff) + ", " + MatrixNum(matrix.v1.yOff) + ", " +
			   MatrixNum(matrix.v1.zOff) + ") i=(" + MatrixNum(matrix.v1.a00) + ", " +
			   MatrixNum(matrix.v1.a01) + ", " + MatrixNum(matrix.v1.a02) + ") j=(" +
			   MatrixNum(matrix.v1.a10) + ", " + MatrixNum(matrix.v1.a11) + ", " +
			   MatrixNum(matrix.v1.a12) + ") k=(" + MatrixNum(matrix.v1.a20) + ", " +
			   MatrixNum(matrix.v1.a21) + ", " + MatrixNum(matrix.v1.a22) + ")";
	}

	// **外から読んだ 4 つの口を「真値」として書き溜める。** ② はこれと `Recalculate` の
	// 中の値を突き合わせるので、**この行が無いと「中が正しいか」は決められない**
	// （中と中を比べるだけでは、両方が同じように化けていても「同じ」と出てしまう
	// ——issue #183 の表がまさにその形だった）。
	void MatrixWriteTruth(vwprobe::Report& probe, MCObjectHandle pio, const std::string& scene)
	{
		VWParametricObj parametric(pio);
		VWTransformMatrix objectToWorld;
		parametric.GetObjectToWorldTransform(objectToWorld);

		VWObject object(pio);
		VWTransformMatrix objectMatrix;
		object.GetObjectMatrix(objectMatrix);

		TransformMatrix entity;
		gSDK->GetEntityMatrix(pio, entity);

		const VWTransformMatrix modelMatrix = object.GetObjectModelMatrix();

		const VWPoint3D world(kMatrixProbeWorldX, kMatrixProbeWorldY, 0.0);

		struct Pair
		{
			std::string key;
			std::string value;
		};
		const Pair pairs[] = {
			{scene + "/GetObjectToWorldTransform", MatrixDescribeText(objectToWorld)},
			{scene + "/GetObjectMatrix", MatrixDescribeText(objectMatrix)},
			{scene + "/GetEntityMatrix(生)", MatrixDescribeRawText(entity)},
			{scene + "/GetObjectModelMatrix", MatrixDescribeText(modelMatrix)},
			{scene + "/逆変換 世界→ローカル",
			 MatrixPointText(objectToWorld.InversePointTransform(world))},
		};
		for (const Pair& pair : pairs)
		{
			MatrixTraceAppend(std::string(kMatrixTruthPrefix) + pair.key + kMatrixSeparator +
							  pair.value);
			probe.log("  " + pair.key + " = " + pair.value);
		}
	}

	// 溜まった行を全部プローブのログへ写す。**これが結果の本体**なので省略しない。
	void MatrixDumpTrace(vwprobe::Report& probe, const char* heading)
	{
		const std::vector<std::string> lines = MatrixTraceRead();
		probe.log("");
		probe.log(std::string("=== ") + heading + "（" +
				  MatrixInt(static_cast<long>(lines.size())) + " 行）===");
		if (lines.empty())
		{
			probe.log("（1 行も無い。殻の PIO が 1 度も Recalculate されていない）");
			return;
		}
		for (const std::string& line : lines)
			probe.log(line);
	}
} // namespace

VW_PROBE("pio-matrix-reset",
		 "① 原点から離し回転させた PIO を作り、ResetObject 時の Recalculate で行列 4 口を測る",
		 "(5000,3000)・30°に置いた PIO を CreateCustomObject → ResetObject し、"
		 "Recalculate の中の行列 4 口と外から見た真値を記録する。"
		 "この後 OIP で編集して pio-matrix-oip を走らせる")
{
	// --- 0) 書き溜め先を空にする -------------------------------------------
	probe.log(std::string("書き溜め先: ") + MatrixTracePath());
	if (!MatrixTraceReset())
		probe.fail("書き溜め先を開けなかった（この後の記録は読めない）: " + MatrixTracePath());
	MatrixTraceAppend("=== プローブ pio-matrix-reset 開始 ===");

	// --- 1) PIO が実機に登録されているかを先に確かめる ----------------------
	// **殻を入れ替えていなければここで落ちる**（公開ビルドの殻にはこの PIO が無い）。
	// `kCustomObjectPrefNever` は「生成時に設定ダイアログを出さない」印
	// （Findings「自作 PIO を足すときの 3 点」の 1 番目）。
	const MCObjectHandle definition =
		gSDK->DefineCustomObject(kMatrixPioUniversalName, kCustomObjectPrefNever);
	if (definition == nullptr)
	{
		// **② がここで止まったことを読めるように、書き溜め先へも残す**（issue #183 では
		// これが無くて「OIP を編集してください」と見当違いのお願いをした）。
		MatrixTraceAppend(std::string(kMatrixAbortMarker) +
						  "（DefineCustomObject が nil）。PR の成果物を手で入れて再起動が要る。");
		probe.fail(std::string("DefineCustomObject(\"") + kMatrixPioUniversalName +
				   "\") が nil。**この殻には調査用 PIO が入っていない**"
				   "——PR の Actions の成果物 **vwlibrary-zip**（macOS）/ "
				   "**VwSdkProbes-windows**（Windows）を手で入れて**Vectorworks を再起動**"
				   "してから走らせてください（ピッカー先頭の入れ替えで取れるのは main の殻"
				   "なので、この PIO は入っていません）。");
		return;
	}
	probe.log(std::string("DefineCustomObject(\"") + kMatrixPioUniversalName +
			  "\"): 定義が見つかった（この殻に PIO が登録されている）");

	// --- 2) PIO を作る（＝1 回目の Recalculate が走る）----------------------
	// **原点から離し、軸に乗らない角度で置く**のがこの調査の全部
	// （issue #183 は原点・無回転だったので、化けても区別が付かなかった）。
	MatrixTraceAppend("--- 印: いまから CreateCustomObject((" + MatrixNum(kMatrixPlaceX) + ", " +
					  MatrixNum(kMatrixPlaceY) + "), " + MatrixNum(kMatrixPlaceAngleDeg) +
					  "°) を呼ぶ ---");
	const WorldPt placement(kMatrixPlaceX, kMatrixPlaceY);
	const MCObjectHandle pio =
		gSDK->CreateCustomObject(kMatrixPioUniversalName, placement, kMatrixPlaceAngleDeg);
	MatrixTraceAppend("--- 印: CreateCustomObject から戻った ---");
	if (pio == nullptr)
	{
		probe.fail("CreateCustomObject が nil を返した（PIO を作れなかった）");
		MatrixDumpTrace(probe, "ここまでに溜まった行");
		return;
	}
	(void)gSDK->SetObjectName(pio, kMatrixPioObjectName);
	probe.log(std::string("PIO を作った: 型=") + MatrixInt(gSDK->GetObjectTypeN(pio)) + " 名前=\"" +
			  kMatrixPioObjectName + "\"");

	// --- 3) 置けたかを確かめ、置けていなければ行列を直に書いて置き直す -------
	// **`CreateCustomObject` の角度が効かない実機もあり得る**ので、ここで確かめる。
	// 単位行列のままだと**この調査そのものが成立しない**（原点・無回転の #183 の
	// やり直しになる）ので、そのときは `SetEntityMatrix` で直に置く。
	{
		VWObject object(pio);
		VWTransformMatrix current;
		object.GetObjectMatrix(current);
		probe.log("外から見た置き位置（作った直後）: " + MatrixDescribeText(current));
		if (current.IsIdentity())
		{
			probe.log("→ **単位行列だった。** CreateCustomObject の位置・角度が効いていない"
					  "ので、SetObjectMatrix で直に置き直す。");
			VWTransformMatrix placed;
			placed.SetRotation(kMatrixPlaceAngleDeg, VWPoint3D(0.0, 0.0, 1.0));
			placed.SetOffset(kMatrixPlaceX, kMatrixPlaceY, 0.0);
			MatrixTraceAppend("--- 印: いまから SetObjectMatrix で置き直す ---");
			object.SetObjectMatrix(placed);
			VWTransformMatrix again;
			object.GetObjectMatrix(again);
			probe.log("置き直した後: " + MatrixDescribeText(again));
			if (again.IsIdentity())
				probe.fail("**行列を書いても単位行列のまま。** 原点・無回転でない PIO が"
						   "作れていないので、この調査は成立しない（結果を Findings へ"
						   "写さないこと）。");
		}
	}

	// --- 4) ResetObject（＝取り込み時のリセット）----------------------------
	MatrixTraceAppend("--- 印: いまから ResetObject を呼ぶ（取り込み時のリセット）---");
	const Boolean reset = gSDK->ResetObject(pio);
	MatrixTraceAppend(std::string("--- 印: ResetObject から戻った（戻り値=") +
					  (reset ? "true" : "false") + "）---");
	probe.log(std::string("ResetObject の戻り値: ") + (reset ? "true" : "false"));

	// --- 5) 外から読んだ真値を書き溜める（② がこれと中の値を突き合わせる）---
	probe.log("");
	probe.log("=== 外（Recalculate の外）から読んだ 4 口 ＝ 真値 ===");
	MatrixWriteTruth(probe, pio, "外");

	// --- 6) 溜まった行を吐き出す -------------------------------------------
	MatrixDumpTrace(probe, "Recalculate の中から読んだ行列（取り込み時の経路）");

	// --- 7) 選んでおく（利用者が OIP をすぐ開けるように）--------------------
	gSDK->SelectObject(pio, true);
	probe.log("");
	probe.log(std::string("PIO を選択した（IsSelected=") +
			  (gSDK->IsSelected(pio) ? "true" : "false") + "）");

	probe.log("");
	probe.log("=== 次にお願いしたいこと ===");
	probe.log("1. いま選ばれている PIO（オブジェクト情報パレットに出ています）の");
	probe.log(std::string("   「覚え書き」欄（ユニバーサル名 ") + kMatrixParamNote +
			  "）に何か文字を入れて確定してください。");
	probe.log(std::string("   （長さの欄 ") + kMatrixParamLength +
			  " は触らなくてよいです——動かす・回すと別の理由のリセットが混ざります。）");
	probe.log("2. そのあとメニューから pio-matrix-oip を走らせてください");
	probe.log("   （書き溜め先は消しません——同じファイルに続けて溜まります）。");
}
