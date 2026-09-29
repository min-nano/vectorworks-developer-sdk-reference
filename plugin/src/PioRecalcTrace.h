//
//	PioRecalcTrace.h
//
//	**殻の PIO（`ExtPioRecalcEnv.cpp`）とプローブが共有する、たった 1 つの約束事**
//	——「`Recalculate` の中で見えたものを書き溜めるファイルはどこか」。
//
//	【なぜファイルなのか】プローブは**本体（`.vwpayload`）側**にあり、PIO の登録と
//	`Recalculate` は**殻側**にある（拡張の登録はモジュールの読み込みで起きるので、
//	入れ替えできる本体には置けない。`plugin/README.md`「殻と本体」）。境界
//	（`PayloadAbi.h`）は「殻 → 本体」の一方向しか持たず、**本体から殻の持ち物を読む口は
//	無い**。しかも `Recalculate` が呼ばれるのは利用者が OIP を編集した瞬間で、そのとき
//	本体は読み込まれてさえいない。だから受け渡しは**プロセスにも読み込み状態にも依らない
//	もの**＝ファイルで行う。1 行ごとに開いて閉じるので、**VectorWorks が落ちてもそこまでが
//	残る**（プローブのログと同じ考え方。`payload/Probe.h`）。
//
//	【SDK を include しない】std の文字列と `<cstdio>` だけで書く（`PayloadAbi.h` と
//	同じ理由）。
//
//	【プローブはこれを include しない——書き写す】公開ビルドは **main の殻 ＋ 各 PR の
//	`probes/runtime/` だけ**で組まれるので、**同じ PR で足したこのヘッダはプローブから
//	は見えない**（include するとその群だけコンパイルが落ちる。実測: この PR の 1 回目の
//	`Probe auto update / publish`）。だから `probes/runtime/pio-recalc-env-*` は同じ値と
//	同じ規則を**自分の無名名前空間へ書き写して**いる。**ここを直したら、プローブ 2 本の
//	写しも直すこと。**
//
//	【役目を終えたら消す】この仕組みは issue #183 の調査のためのもので、常駐させない
//	（`CLAUDE.md`「実機確認プラグイン」）。消すときは
//	`ExtPioRecalcEnv.*` ／ `probes/runtime/pio-recalc-env-*` ／ `.vwstrings` の
//	`pio*` の行と一緒に落とす。
//

#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace vwprobe
{
	namespace pioTrace
	{
		// -------------------------------------------------------------------
		// **殻とプローブが同じ綴りを使うための名前。** 図面に置くものの名前もここに
		// 集める——`Recalculate` は「この名前で探す」、プローブは「この名前で作る」ので、
		// 食い違えば「見つからない」としか出ない。

		// PIO のユニバーサル名（`IMPLEMENT_VWExtension` の第 3 引数と一致させる）。
		constexpr const char* kPioUniversalName = "VwSdkProbesRecalcEnv";
		// `Recalculate` が名前で探す「自分以外の図形」。プローブが矩形を作って命名する。
		constexpr const char* kTargetObjectName = "VwSdkProbes-Target";
		// プローブが作った PIO そのものに付ける名前（2 本目のプローブが名前で拾う）。
		constexpr const char* kPioObjectName = "VwSdkProbes-Pio";
		// `Recalculate` が `GetNamedLayer` で引く「他のレイヤ」。プローブが作る。
		constexpr const char* kOtherLayerName = "VwSdkProbes-Other";
		// PIO のパラメータ（ユニバーサル名）。OIP で編集してもらうのは `TraceNote`。
		constexpr const char* kParamNote = "TraceNote";
		constexpr const char* kParamLength = "TraceLength";
		constexpr const char* kParamFlag = "TraceFlag";

		// -------------------------------------------------------------------
		// 書き溜め先。環境変数 `VW_PROBE_PIO_TRACE` で差し替えられる（逃げ道）。
		// 既定は一時ディレクトリ（`payload/Probe.cpp` の `defaultLogPath` と同じ規則）。
		inline std::string Path()
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

		// 時刻の印（`HH:MM:SS.mmm`）。**行の順序と「どれがひと塊か」は時刻で読む**
		// ——`Recalculate` は VW が好きなときに呼ぶので、プローブが入れた印との前後が
		// これでしか分からない。
		inline std::string Stamp()
		{
			const auto now = std::chrono::system_clock::now();
			const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
			const auto millis =
				std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
					.count() %
				1000;
			std::tm parts{};
#if defined(_WINDOWS)
			(void)localtime_s(&parts, &seconds);
#else
			(void)localtime_r(&seconds, &parts);
#endif
			char buffer[32];
			(void)std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", parts.tm_hour,
								parts.tm_min, parts.tm_sec, static_cast<int>(millis));
			return std::string(buffer);
		}

		// 1 行足す（末尾の改行は不要）。**呼ぶたびに開いて閉じる**——`Recalculate` の
		// 途中で落ちても、そこまでの行が残るように。開けなければ黙って諦める
		// （`Recalculate` の中なので、ここで例外を出しては困る）。
		inline void Append(const std::string& line)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
			std::FILE* file = std::fopen(Path().c_str(), "ab");
			if (file == nullptr)
				return;
			const std::string text = Stamp() + " " + line + "\n";
			(void)std::fwrite(text.data(), 1, text.size(), file);
			(void)std::fclose(file);
		}

		// 空にする（プローブが調査を始めるときに 1 度だけ呼ぶ）。
		inline bool Reset()
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
			std::FILE* file = std::fopen(Path().c_str(), "wb");
			if (file == nullptr)
				return false;
			(void)std::fclose(file);
			return true;
		}

		// 溜まった行を読む（プローブが吐き出すときに呼ぶ）。ファイルが無ければ空。
		inline std::vector<std::string> Read()
		{
			std::vector<std::string> lines;
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): その場で fclose する。
			std::FILE* file = std::fopen(Path().c_str(), "rb");
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
	} // namespace pioTrace
} // namespace vwprobe
