//
//	probes/runtime/pio-matrix-oip/probe.cpp
//
//	[issue #185] **OIP でパラメータを 1 つ編集したときの `Recalculate`** で行列を読む
//	4 つの口が何を返したかを吐き出し、①（`pio-matrix-reset` ＝ `ResetObject`）と、
//	**外から読んだ真値**の両方に**機械で**突き合わせる ②。
//
//	【なぜ「真値」と比べるのか】issue #183 は「中（`ResetObject` 時）と中（OIP 編集時）」
//	だけを比べて「同じ」と結論した。**両方が同じように化けていれば、それでも「同じ」と
//	出る**——被験体が原点・無回転だったので、化けても値が変わらなかった。ここでは
//	`(5000, 3000)`・`30°` に置いた PIO の**外から読んだ行列**を真値として持ち、
//	「中の値が真値と一致するか」を口ごとに出す。
//
//	【手順】① を走らせる → 図面の PIO を選んで OIP の「覚え書き」欄を編集する → これを
//	走らせる。**編集せずに走らせた場合はそれが分かる**（`kParameterChangedReset` の塊が
//	無い、と出て失敗する）ので、目視も転記も要らない。
//
//	【このプローブも公開ビルドでは動かない】記録しているのは殻の PIO（issue #185 のために
//	足した一時的なもの）なので、PR の Actions の成果物を手で入れてもらう。
//

#include "Probe.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	// -------------------------------------------------------------------
	// **殻（plugin/src/PioMatrixTrace.h）と同じ約束事を、ここへ書き写したもの。**
	// include しない理由は `pio-matrix-reset/probe.cpp` の頭と同じ。
	constexpr const char* kMatrixPioObjectName = "VwSdkProbes-MatrixPio";
	constexpr const char* kMatrixParamNote = "TraceNote";
	constexpr const char* kMatrixSeparator = " ## ";
	constexpr const char* kMatrixBlockMarker = "=== Recalculate 開始";
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

	std::string MatrixInt(long value)
	{
		char buffer[32];
		(void)std::snprintf(buffer, sizeof(buffer), "%ld", value);
		return std::string(buffer);
	}

	// 行頭の時刻の印（`HH:MM:SS.mmm `）を落とす。**印は行の順序を読むためのもので、
	// 突き合わせの材料ではない**（付いたまま比べれば全部「違う」になる）。
	std::string MatrixStripStamp(const std::string& line)
	{
		if (line.size() > 13 && line[2] == ':' && line[5] == ':' && line[8] == '.' &&
			line[12] == ' ')
			return line.substr(13);
		return line;
	}

	// 「鍵の末尾」だけを採る（`入口/GetObjectMatrix` → `GetObjectMatrix`）。
	// 真値の鍵は `外/…` なので、**場面をまたいで同じ口どうしを比べる**にはこれが要る。
	std::string MatrixKeyTail(const std::string& key)
	{
		const std::string::size_type slash = key.rfind('/');
		return slash == std::string::npos ? key : key.substr(slash + 1);
	}

	struct MatrixPair
	{
		std::string key;
		std::string value;
	};

	// `鍵 ## 値` を切る。切れなければ false。
	bool MatrixSplitPair(const std::string& line, MatrixPair& out)
	{
		const std::string body = MatrixStripStamp(line);
		const std::string::size_type at = body.find(kMatrixSeparator);
		if (at == std::string::npos)
			return false;
		out.key = body.substr(0, at);
		out.value = body.substr(at + std::string(kMatrixSeparator).size());
		return true;
	}

	struct MatrixBlock
	{
		std::string reason;
		std::vector<MatrixPair> pairs;
	};

	const std::string* MatrixValueFor(const std::vector<MatrixPair>& pairs, const std::string& key)
	{
		for (const MatrixPair& pair : pairs)
			if (pair.key == key)
				return &pair.value;
		return nullptr;
	}

	const std::string* MatrixValueForTail(const std::vector<MatrixPair>& pairs,
										  const std::string& tail)
	{
		for (const MatrixPair& pair : pairs)
			if (MatrixKeyTail(pair.key) == tail)
				return &pair.value;
		return nullptr;
	}

	// 理由の綴りで塊を探す（**最後のもの**を採る——同じ文脈で何度も走っていても、
	// 直近が「いま確かめたい 1 回」である）。見つからなければ -1。
	long MatrixFindLastBlock(const std::vector<MatrixBlock>& blocks, const char* needle)
	{
		for (long i = static_cast<long>(blocks.size()) - 1; i >= 0; --i)
			if (blocks[static_cast<size_t>(i)].reason.find(needle) != std::string::npos)
				return i;
		return -1;
	}

	// 1 つの塊について「中の値が真値と一致するか」を口ごとに出す。
	// 戻り値は**食い違った口の数**。
	long MatrixCompareWithTruth(vwprobe::Report& probe, const MatrixBlock& block,
								const std::vector<MatrixPair>& truth, const char* scene,
								const char* label)
	{
		long mismatched = 0;
		probe.log(std::string("  --- ") + label + " の「" + scene + "」を真値と比べる ---");
		for (const MatrixPair& pair : block.pairs)
		{
			if (pair.key.compare(0, std::string(scene).size(), scene) != 0)
				continue;
			const std::string tail = MatrixKeyTail(pair.key);
			const std::string* expected = MatrixValueForTail(truth, tail);
			if (expected == nullptr)
			{
				probe.log("    [真値なし] " + tail);
				continue;
			}
			if (*expected == pair.value)
			{
				probe.log("    [真値と一致] " + tail + ": " + pair.value);
				continue;
			}
			++mismatched;
			probe.log("    [**真値と違う**] " + tail);
			probe.log("        真値（外）: " + *expected);
			probe.log("        中の値    : " + pair.value);
		}
		return mismatched;
	}
} // namespace

VW_PROBE("pio-matrix-oip",
		 "② OIP 編集時の Recalculate の行列 4 口を、①（ResetObject）と外から読んだ真値に比べる",
		 "溜まった行を Recalculate ごとに割り、kObjectExternalReset の回と "
		 "kParameterChangedReset の回を同じ鍵どうしで突き合わせ、さらに両方を"
		 "外から読んだ真値と比べて「どの口が実際の配置を返すか」を出す")
{
	probe.log(std::string("書き溜め先: ") + MatrixTracePath());
	const std::vector<std::string> lines = MatrixTraceRead();
	if (lines.empty())
	{
		probe.fail("書き溜め先が空。**先に pio-matrix-reset を走らせてください**"
				   "（それが PIO を作り、記録を始めます）。");
		return;
	}

	// --- 1) 全文（人が読む用。Findings へ写す数値はここから採る）------------
	probe.log("");
	probe.log("=== 溜まった行の全文（" + MatrixInt(static_cast<long>(lines.size())) + " 行）===");
	for (const std::string& line : lines)
		probe.log(line);

	// --- 2) ① がそもそも PIO を作れていない場合を先に切り分ける -------------
	// **言うべきことが正反対**なので、ここを取り違えると見当違いのお願いになる
	// （issue #183 で 1 回踏んだ）。
	for (const std::string& line : lines)
		if (line.find(kMatrixAbortMarker) != std::string::npos)
		{
			probe.fail("① が「この殻に調査用 PIO が入っていない」で中止している。"
					   "**OIP の編集ではなく、この PR のビルドの成果物（macOS は "
					   "vwlibrary-zip / Windows は VwSdkProbes-windows）を手で入れて "
					   "Vectorworks を再起動する**のが先です。"
					   " ◆**ピッカー先頭の入れ替えでも、リリース（タグ probes）の zip でも"
					   "入りません**——どちらも殻は main のものです。"
					   " ◆**見分け方は、この投稿の「ビルドの素性」の `殻:` の行**"
					   "（`main` のままなら入れ替え先が違います）。"
					   "入れ替えたら ① → OIP の編集 → ② の順で。");
			return;
		}

	// --- 3) 真値と塊に割る --------------------------------------------------
	std::vector<MatrixPair> truth;
	std::vector<MatrixBlock> blocks;
	for (const std::string& line : lines)
	{
		const std::string body = MatrixStripStamp(line);
		if (body.find(kMatrixBlockMarker) != std::string::npos)
		{
			MatrixBlock block;
			const std::string::size_type at = body.find("理由=");
			// **`+ 9` と書かない。** `理由=` は UTF-8 で 7 バイト（漢字 2 文字＋`=`）で、
			// 文字数で数えると値の先頭が削れる。綴りの長さは綴りから採る。
			block.reason = at == std::string::npos ? std::string("（理由の綴りが無い）")
												   : body.substr(at + std::string("理由=").size());
			blocks.push_back(block);
			continue;
		}
		MatrixPair pair;
		if (!MatrixSplitPair(line, pair))
			continue;
		if (pair.key.compare(0, std::string(kMatrixTruthPrefix).size(), kMatrixTruthPrefix) == 0)
		{
			pair.key = pair.key.substr(std::string(kMatrixTruthPrefix).size());
			truth.push_back(pair);
			continue;
		}
		if (!blocks.empty())
			blocks.back().pairs.push_back(pair);
	}

	probe.log("");
	probe.log("=== 外から読んだ真値 " + MatrixInt(static_cast<long>(truth.size())) + " 件 ===");
	for (const MatrixPair& pair : truth)
		probe.log("  " + pair.key + " = " + pair.value);

	probe.log("");
	probe.log("=== Recalculate の塊 " + MatrixInt(static_cast<long>(blocks.size())) + " 個 ===");
	for (size_t i = 0; i < blocks.size(); ++i)
		probe.log("  #" + MatrixInt(static_cast<long>(i) + 1) + " 理由=" + blocks[i].reason +
				  " 行数=" + MatrixInt(static_cast<long>(blocks[i].pairs.size())));

	if (blocks.empty())
	{
		probe.fail("`Recalculate` の記録が 1 つも無い（塊 0 個）。① は PIO を作れているので、"
				   "疑うのは殻の書き溜め（`pioMatrix::Append`）か、① と ② で書き溜め先が"
				   "食い違っていること。上の「書き溜め先」のパスを ① の結果と見比べること。");
		return;
	}
	if (truth.empty())
		probe.fail("外から読んだ真値が 1 件も無い（① が途中で止まっている）。"
				   "**真値が無いと「中の値が正しいか」は決められない**"
				   "——① をもう一度走らせてください。");

	// --- 4) 2 つの文脈を取り出す --------------------------------------------
	const long externalIndex = MatrixFindLastBlock(blocks, "kObjectExternalReset");
	const long paramIndex = MatrixFindLastBlock(blocks, "kParameterChangedReset");

	probe.log("");
	probe.log("=== 突き合わせ ===");
	if (externalIndex < 0)
		probe.log("kObjectExternalReset（外からの ResetObject）の塊が無い"
				  "——`kObjXPropAcceptStates` が効いていないか、理由が別の綴りで来ている。"
				  "上の『理由=』を読むこと。");
	if (paramIndex < 0)
		probe.fail("kParameterChangedReset（OIP でパラメータを編集）の塊が無い。"
				   "**図面の PIO を選び、OIP の「覚え書き」欄を編集してから、もう一度"
				   "このプローブを走らせてください。**");

	// --- 5) 中と中（①の回 と OIP 編集の回）--------------------------------
	if (externalIndex >= 0 && paramIndex >= 0)
	{
		const MatrixBlock& fromReset = blocks[static_cast<size_t>(externalIndex)];
		const MatrixBlock& fromOip = blocks[static_cast<size_t>(paramIndex)];
		probe.log("取り込み時のリセット = 塊 #" + MatrixInt(externalIndex + 1) +
				  " / OIP 編集 = 塊 #" + MatrixInt(paramIndex + 1));

		long same = 0;
		long differ = 0;
		long missing = 0;
		for (const MatrixPair& pair : fromReset.pairs)
		{
			const std::string* other = MatrixValueFor(fromOip.pairs, pair.key);
			if (other == nullptr)
			{
				++missing;
				probe.log("  [片方だけ] " + pair.key + " … OIP 編集の側に無い");
				continue;
			}
			if (*other == pair.value)
			{
				++same;
				probe.log("  [同じ] " + pair.key + ": " + pair.value);
				continue;
			}
			++differ;
			probe.log("  [違う] " + pair.key);
			probe.log("      取り込み時: " + pair.value);
			probe.log("      OIP 編集  : " + *other);
		}
		for (const MatrixPair& pair : fromOip.pairs)
			if (MatrixValueFor(fromReset.pairs, pair.key) == nullptr)
			{
				++missing;
				probe.log("  [片方だけ] " + pair.key + " … 取り込み時の側に無い");
			}

		probe.log("");
		probe.log("中と中の結論: 同じ " + MatrixInt(same) + " 件 / 違う " + MatrixInt(differ) +
				  " 件 / 片方だけ " + MatrixInt(missing) + " 件");
		probe.log(
			differ == 0 && missing == 0
				? "→ **2 つの文脈で読めた行列は同一**"
				: "→ **2 つの文脈で読めた行列は違う**（上の [違う] / [片方だけ] がその全部）");
	}

	// --- 6) 中と真値（**これが issue #185 の答え**）------------------------
	if (!truth.empty())
	{
		probe.log("");
		probe.log("=== 中の値は実際の配置（真値）と一致するか ===");
		long total = 0;
		if (externalIndex >= 0)
		{
			const MatrixBlock& block = blocks[static_cast<size_t>(externalIndex)];
			total += MatrixCompareWithTruth(probe, block, truth, "入口", "取り込み時");
			total += MatrixCompareWithTruth(probe, block, truth, "出口", "取り込み時");
		}
		if (paramIndex >= 0)
		{
			const MatrixBlock& block = blocks[static_cast<size_t>(paramIndex)];
			total += MatrixCompareWithTruth(probe, block, truth, "入口", "OIP 編集");
			total += MatrixCompareWithTruth(probe, block, truth, "出口", "OIP 編集");
		}
		probe.log("");
		if (total == 0)
			probe.log("→ **どちらの文脈でも、4 つの口すべてが実際の配置を返している**"
					  "（行列が化ける現象は再現しなかった）");
		else
			probe.log("→ **真値と食い違う口がある**（" + MatrixInt(total) +
					  " 件）。上の [**真値と違う**] の行がその全部で、"
					  "「どの口を読むべきか」はそこで決まる。");
	}

	// --- 7) いまの PIO を外から見た値（参考）-------------------------------
	const MCObjectHandle pio = gSDK->GetNamedObject(kMatrixPioObjectName);
	probe.log("");
	if (pio == nullptr)
	{
		probe.log(std::string("図面に \"") + kMatrixPioObjectName +
				  "\" が見つからない（消したか、別の図面で走らせている）");
		return;
	}
	VWParametricObj parametric(pio);
	probe.log(std::string("外から見た ") + kMatrixPioObjectName + ": TraceNote=\"" +
			  static_cast<const char*>(parametric.GetParamValue(kMatrixParamNote)) + "\"");
}
