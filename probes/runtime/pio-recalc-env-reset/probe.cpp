//
//	probes/runtime/pio-recalc-env-reset/probe.cpp
//
//	[issue #183] **自前の PIO の `Recalculate` の中から見える環境**を、「取り込み時と
//	同じ経路（作る → `ResetObject`）」で記録する ①。
//
//	測るのは 3 つ（issue #183）:
//	  (1) `VWParametricObj::GetObjectToWorldTransform`（自分の行列）
//	  (2) `gSDK->GetNamedLayer(名前)`（他のレイヤを名前で引く）
//	  (3) 他オブジェクトの `GetObjectBounds`（＝**自分以外の図形を探して測れるか**）
//	併せて、線分 PIO が `Recalculate` の中で自分の両端・長さをどう読むか、そして
//	**SDK で登録した線分 PIO のパラメータ表に `LineLength` が現れるか**も記録する。
//
//	【測る側は殻にいる】`Recalculate` に立てられるのは自前の PIO の中だけで、その登録は
//	殻（`plugin/src/ExtPioRecalcEnv.cpp`）にしか置けない。だから**このプローブは
//	「印を入れて、溜まった行を吐き出す」係**で、実際に SDK を叩いているのは殻の側である
//	（受け渡しは `plugin/src/PioRecalcTrace.h` のファイル 1 本）。
//
//	【このプローブは公開ビルドでは動かない】殻が変わっているので、**PR の Actions の
//	成果物を手で入れてもらう**（`plugin/README.md`「PR のビルドを手で入れて確かめるとき」）。
//
//	【2 本で 1 組】OIP 編集の側は `pio-recalc-env-oip` が受け持つ。こちらを走らせた後、
//	利用者が OIP でパラメータを編集し、そのあとで向こうを走らせる。
//

#include "Probe.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace
{
	// -------------------------------------------------------------------
	// **殻（plugin/src/PioRecalcTrace.h）と同じ約束事を、ここへ書き写したもの。**
	//
	// 【なぜ include しないのか】公開ビルドは **main の殻 ＋ 各 PR の
	// `probes/runtime/` だけ**で組まれる（`scripts/gather-probes.sh` は PR の木から
	// `probes/runtime` しか取り出さない）。だから**プローブは、同じ PR で足した殻の
	// ヘッダを include できない**——include するとその群だけコンパイルが落ちる
	// （実測: この PR の 1 回目の `Probe auto update / publish` がそれで落ちた）。
	// 値を変えるときは `plugin/src/PioRecalcTrace.h` と**両方**直すこと（食い違うと
	// 「見つからない」としか出ない）。
	constexpr const char* kPioUniversalName = "VwSdkProbesRecalcEnv";
	constexpr const char* kTargetObjectName = "VwSdkProbes-Target";
	constexpr const char* kPioObjectName = "VwSdkProbes-Pio";
	constexpr const char* kOtherLayerName = "VwSdkProbes-Other";
	constexpr const char* kParamNote = "TraceNote";
	constexpr const char* kParamLength = "TraceLength";

	// 書き溜め先（殻の `pioTrace::Path()` と同じ規則）。
	std::string TracePath()
	{
		const char* custom = std::getenv("VW_PROBE_PIO_TRACE");
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
		return dir + separator + "VwSdkProbes-pio-recalc-trace.log";
	}

	// 溜まった行を読む（無ければ空）。
	std::vector<std::string> TraceRead()
	{
		std::vector<std::string> lines;
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
		std::FILE* file = std::fopen(TracePath().c_str(), "rb");
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

	// 印を 1 行足す（殻の `pioTrace::Append()` と同じ形。時刻の印も同じ規則）。
	void TraceAppend(const std::string& line)
	{
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
		std::FILE* file = std::fopen(TracePath().c_str(), "ab");
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
	bool TraceReset()
	{
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
		std::FILE* file = std::fopen(TracePath().c_str(), "wb");
		if (file == nullptr)
			return false;
		(void)std::fclose(file);
		return true;
	}

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

	// 溜まった行を全部プローブのログへ写す。**これが結果の本体**なので省略しない。
	void DumpTrace(vwprobe::Report& probe, const char* heading)
	{
		const std::vector<std::string> lines = TraceRead();
		probe.log("");
		probe.log(std::string("=== ") + heading + "（" + Int(static_cast<long>(lines.size())) +
				  " 行）===");
		if (lines.empty())
		{
			probe.log("（1 行も無い。殻の PIO が 1 度も Recalculate されていない）");
			return;
		}
		for (const std::string& line : lines)
			probe.log(line);
	}
} // namespace

VW_PROBE("pio-recalc-env-reset",
		 "① 自前 PIO を作って ResetObject し、Recalculate の中から見えたものを記録する",
		 "取り込み時と同じ経路（CreateCustomObject → ResetObject）で Recalculate の中の"
		 "環境を記録する。この後 OIP で編集して pio-recalc-env-oip を走らせる")
{
	// --- 0) 書き溜め先を空にする -------------------------------------------
	probe.log(std::string("書き溜め先: ") + TracePath());
	if (!TraceReset())
		probe.fail("書き溜め先を開けなかった（この後の記録は読めない）: " + TracePath());
	TraceAppend("=== プローブ pio-recalc-env-reset 開始 ===");

	// --- 1) PIO が実機に登録されているかを先に確かめる ----------------------
	// **殻を入れ替えていなければここで落ちる**（公開ビルドの殻にはこの PIO が無い）。
	// `kCustomObjectPrefNever` は「生成時に設定ダイアログを出さない」印
	// （Findings「自作 PIO を足すときの 3 点」の 1 番目）。
	const MCObjectHandle definition =
		gSDK->DefineCustomObject(kPioUniversalName, kCustomObjectPrefNever);
	if (definition == nullptr)
	{
		// **② がここで止まったことを読めるように、書き溜め先へも残す。** さもないと
		// ② は「塊が 0 個」しか分からず、`OnAddState` が効いていない疑いと区別が付かない
		// ——「OIP を編集してください」と**見当違いのお願い**をしてしまう（実測で踏んだ）。
		TraceAppend("!!! ① 中止: この殻に調査用 PIO が入っていない"
					"（DefineCustomObject が nil）。PR の成果物を手で入れて再起動が要る。");
		probe.fail(std::string("DefineCustomObject(\"") + kPioUniversalName +
				   "\") が nil。**この殻には調査用 PIO が入っていない**"
				   "——PR の Actions の成果物（VwSdkProbes-mac / -windows）を手で入れて"
				   "**Vectorworks を再起動**してから走らせてください"
				   "（ピッカー先頭の入れ替えで取れるのは main の殻なので入っていません）。");
		return;
	}
	probe.log(std::string("DefineCustomObject(\"") + kPioUniversalName +
			  "\"): 定義が見つかった（この殻に PIO が登録されている）");

	// --- 2) 「他のレイヤ」を用意する（問い 2 の被験体）----------------------
	// **レイヤを作るとアクティブレイヤが移ることがある**ので、作る前のレイヤを覚えて
	// 戻す（Findings「Dimensions」の「シートレイヤを作るとアクティブレイヤが移る」）。
	const MCObjectHandle layerBefore = gSDK->GetCurrentLayer();
	MCObjectHandle otherLayer = gSDK->GetNamedLayer(kOtherLayerName);
	if (otherLayer == nullptr)
		otherLayer = gSDK->CreateLayerN(kOtherLayerName, 1.0);
	gSDK->SetCurrentLayer(layerBefore);
	probe.log(std::string("他のレイヤ \"") + kOtherLayerName +
			  "\": " + (otherLayer != nullptr ? "有り" : "**作れなかった**"));
	probe.log(std::string("作業レイヤ: \"") + NameOf(gSDK->GetCurrentLayer()) +
			  "\"（アクティブ=\"" + NameOf(gSDK->GetActiveLayer()) + "\"）");

	// --- 3) 「自分以外の図形」を用意する（問い 3 の被験体）------------------
	// 名前を付けるのは、`Recalculate` の中から**名前で探す**ため（`GetNamedObject`）。
	MCObjectHandle target = gSDK->GetNamedObject(kTargetObjectName);
	if (target == nullptr)
	{
		WorldRect rect;
		rect.left = 1000;
		rect.right = 2000;
		rect.top = 2500;
		rect.bottom = 1500;
		target = gSDK->CreateRectangle(rect);
		if (target != nullptr)
			(void)gSDK->SetObjectName(target, kTargetObjectName);
	}
	if (target == nullptr)
	{
		probe.fail("被験体の矩形を作れなかった（問い 3 は測れない）");
	}
	else
	{
		WorldRect bounds;
		const Boolean got = gSDK->GetObjectBounds(target, bounds);
		probe.log(std::string("被験体の矩形 \"") + kTargetObjectName +
				  "\": 型=" + Int(gSDK->GetObjectTypeN(target)) +
				  (got ? (" 外接 left=" + Num(bounds.left) + " bottom=" + Num(bounds.bottom) +
						  " 幅=" + Num(bounds.right - bounds.left) +
						  " 高=" + Num(bounds.top - bounds.bottom))
					   : std::string(" 外接: GetObjectBounds が false")));
	}

	// --- 4) PIO を作る（＝1 回目の Recalculate が走る）----------------------
	TraceAppend("--- 印: いまから CreateCustomObject を呼ぶ（取り込み時と同じ経路）---");
	const WorldPt origin(0, 0);
	const MCObjectHandle pio = gSDK->CreateCustomObject(kPioUniversalName, origin, 0.0);
	TraceAppend("--- 印: CreateCustomObject から戻った ---");
	if (pio == nullptr)
	{
		probe.fail("CreateCustomObject が nil を返した（PIO を作れなかった）");
		DumpTrace(probe, "ここまでに溜まった行");
		return;
	}
	(void)gSDK->SetObjectName(pio, kPioObjectName);
	probe.log(std::string("PIO を作った: 型=") + Int(gSDK->GetObjectTypeN(pio)) + " 名前=\"" +
			  NameOf(pio) + "\"");

	// --- 5) 両端を与える（取り込みの手順と同じ）-----------------------------
	// **効くかどうかは #181 で測っている。** ここでは「与えた後に Recalculate の中で
	// 何が見えるか」が知りたいので、同じ手順を踏むことだけが目的である。
	TraceAppend("--- 印: いまから SetLinearObjectPos((0,0),(3000,0)) を呼ぶ ---");
	{
		VWParametricObj obj(pio);
		obj.SetLinearObjectPos(VWPoint2D(0.0, 0.0), VWPoint2D(3000.0, 0.0));
	}

	// --- 6) ResetObject（＝取り込み時のリセット）----------------------------
	TraceAppend("--- 印: いまから ResetObject を呼ぶ（取り込み時のリセット）---");
	const Boolean reset = gSDK->ResetObject(pio);
	TraceAppend(std::string("--- 印: ResetObject から戻った（戻り値=") +
				(reset ? "true" : "false") + "）---");
	probe.log(std::string("ResetObject の戻り値: ") + (reset ? "true" : "false"));

	// --- 7) 外から見た値（`Recalculate` の中と突き合わせるため）-------------
	{
		VWParametricObj obj(pio);
		VWPoint2D ptA;
		VWPoint2D ptB;
		obj.GetLinearObjectPos(ptA, ptB);
		const double dx = ptB.x - ptA.x;
		const double dy = ptB.y - ptA.y;
		probe.log("外から GetLinearObjectPos: A=(" + Num(ptA.x) + ", " + Num(ptA.y) + ") B=(" +
				  Num(ptB.x) + ", " + Num(ptB.y) + ") |AB|=" + Num(std::sqrt(dx * dx + dy * dy)));

		const size_t count = obj.GetParamsCount();
		std::string names;
		for (size_t i = 0; i < count; ++i)
		{
			if (i > 0)
				names += ", ";
			names += static_cast<const char*>(obj.GetParamName(i));
		}
		probe.log("外から見たパラメータ " + Int(static_cast<long>(count)) + " 個: " + names);
		const size_t lineLength = obj.GetParamIndex("LineLength");
		probe.log(std::string("外から見た LineLength: ") +
				  (lineLength == static_cast<size_t>(-1)
					   ? "**表に無い**"
					   : ("索引=" + Int(static_cast<long>(lineLength)) +
						  " 実数=" + Num(obj.GetParamReal("LineLength")))));

		WorldRect bounds;
		if (gSDK->GetObjectBounds(pio, bounds))
			probe.log("外から見た PIO の外接: 幅=" + Num(bounds.right - bounds.left) +
					  " 高=" + Num(bounds.top - bounds.bottom) + " left=" + Num(bounds.left) +
					  " bottom=" + Num(bounds.bottom));
		else
			probe.log("外から見た PIO の外接: GetObjectBounds が false");
	}

	// --- 8) 選んでおく（利用者が OIP をすぐ開けるように）--------------------
	gSDK->SelectObject(pio, true);
	probe.log(std::string("PIO を選択した（IsSelected=") +
			  (gSDK->IsSelected(pio) ? "true" : "false") + "）");

	// --- 9) 溜まった行を吐き出す -------------------------------------------
	DumpTrace(probe, "Recalculate の中から見えたもの（取り込み時の経路）");

	probe.log("");
	probe.log("=== 次にお願いしたいこと ===");
	probe.log("1. いま選ばれている PIO（オブジェクト情報パレットに出ています）の");
	probe.log(std::string("   「覚え書き」欄（ユニバーサル名 ") + kParamNote +
			  "）に何か文字を入れて確定してください。");
	probe.log("2. そのあとメニューから pio-recalc-env-oip を走らせてください");
	probe.log("   （書き溜め先は消しません——同じファイルに続けて溜まります）。");
}
