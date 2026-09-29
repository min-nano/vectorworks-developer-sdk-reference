//
//	probes/runtime/pio-recalc-env-oip/probe.cpp
//
//	[issue #183] **OIP で編集したときの `Recalculate` の中身**を吐き出し、①（
//	`pio-recalc-env-reset`＝取り込み時のリセット）で記録したものと**機械で突き合わせる** ②。
//
//	【なぜ突き合わせをプローブ側でやるか】溜まった行を並べて人が読み比べると、「同じ」と
//	見えたのに実は違っていた（あるいは逆）が起きる。知りたいのは**同じか違うか**そのもの
//	なので、**同じ鍵の行を機械で比べて「一致／不一致」を出す**。目で読むための全文も
//	併せて出す（Findings へ写すのは数値なので）。
//
//	【手順】① を走らせる → 図面の PIO を選んで OIP の「覚え書き」欄を編集する → これを
//	走らせる。**編集せずに走らせた場合はそれが分かる**（`kParameterChangedReset` の塊が
//	無い、と出て失敗する）ので、目視も転記も要らない。
//
//	【このプローブも公開ビルドでは動かない】記録しているのは殻の PIO（issue #183 のために
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

	std::string Int(long value)
	{
		char buffer[32];
		(void)std::snprintf(buffer, sizeof(buffer), "%ld", value);
		return std::string(buffer);
	}

	// `Recalculate` 1 回ぶんの塊。
	struct Block
	{
		std::string state;				 // 直前の OnAddState（理由の綴り）
		std::vector<std::string> keys;	 // 行の鍵（": " の前）
		std::vector<std::string> values; // 行の残り
	};

	// 行の頭から時刻（`HH:MM:SS.mmm `）を落とす。
	std::string StripStamp(const std::string& line)
	{
		const size_t space = line.find(' ');
		if (space == std::string::npos)
			return line;
		return line.substr(space + 1);
	}

	// `  [Recalculate #3] 鍵: 値` から `鍵` と `値` を取り出す。タグが無ければ false。
	bool SplitTagged(const std::string& line, std::string& outKey, std::string& outValue)
	{
		const std::string body = StripStamp(line);
		const size_t open = body.find("[Recalculate #");
		if (open == std::string::npos)
			return false;
		const size_t close = body.find(']', open);
		if (close == std::string::npos)
			return false;
		std::string rest = body.substr(close + 1);
		while (!rest.empty() && rest.front() == ' ')
			rest.erase(rest.begin());
		const size_t colon = rest.find(':');
		if (colon == std::string::npos)
		{
			outKey = rest;
			outValue = "";
			return true;
		}
		outKey = rest.substr(0, colon);
		outValue = rest.substr(colon + 1);
		while (!outValue.empty() && outValue.front() == ' ')
			outValue.erase(outValue.begin());
		return true;
	}

	// 溜まった行を `Recalculate` ごとの塊に割る。
	std::vector<Block> SplitBlocks(const std::vector<std::string>& lines)
	{
		std::vector<Block> blocks;
		for (const std::string& line : lines)
		{
			const std::string body = StripStamp(line);
			if (body.rfind("Recalculate 開始", 0) == 0)
			{
				Block block;
				const std::string marker = "直前の OnAddState=";
				const size_t at = body.find(marker);
				block.state = (at == std::string::npos) ? std::string("（不明）")
														: body.substr(at + marker.size());
				blocks.push_back(block);
				continue;
			}
			std::string key;
			std::string value;
			if (!blocks.empty() && SplitTagged(line, key, value))
			{
				blocks.back().keys.push_back(key);
				blocks.back().values.push_back(value);
			}
		}
		return blocks;
	}

	// 理由の綴りで塊を探す（**最後のもの**を採る——同じ文脈で何度も走っていても、
	// 直近が「いま確かめたい 1 回」である）。見つからなければ -1。
	long FindLastBlock(const std::vector<Block>& blocks, const char* needle)
	{
		for (long i = static_cast<long>(blocks.size()) - 1; i >= 0; --i)
			if (blocks[static_cast<size_t>(i)].state.find(needle) != std::string::npos)
				return i;
		return -1;
	}

	const std::string* ValueFor(const Block& block, const std::string& key)
	{
		for (size_t i = 0; i < block.keys.size(); ++i)
			if (block.keys[i] == key)
				return &block.values[i];
		return nullptr;
	}
} // namespace

VW_PROBE("pio-recalc-env-oip",
		 "② OIP 編集での Recalculate を吐き出し、①（ResetObject）と機械で比べる",
		 "溜まった行を Recalculate ごとに割り、kObjectExternalReset の回と "
		 "kParameterChangedReset の回を同じ鍵どうしで突き合わせて一致／不一致を出す")
{
	probe.log(std::string("書き溜め先: ") + TracePath());
	const std::vector<std::string> lines = TraceRead();
	if (lines.empty())
	{
		probe.fail("書き溜め先が空。**先に pio-recalc-env-reset を走らせてください**"
				   "（それが PIO を作り、記録を始めます）。");
		return;
	}

	// --- 1) 全文（人が読む用。Findings へ写す数値はここから採る）------------
	probe.log("");
	probe.log("=== 溜まった行の全文（" + Int(static_cast<long>(lines.size())) + " 行）===");
	for (const std::string& line : lines)
		probe.log(line);

	// --- 1.5) ① がそもそも PIO を作れていない場合を先に切り分ける -----------
	// **塊が 0 個のときの原因は 2 つあり、言うべきことが正反対**である:
	//   (a) ① が「殻に PIO が入っていない」で中止した → 要るのは PR の成果物の手入れ
	//   (b) PIO はできたが `Recalculate` の記録が 1 つも無い → `kObjXPropAcceptStates` か
	//       書き溜め先の疑い
	// ① が (a) のとき書き溜め先へ `!!! ① 中止:` を残すので、ここで拾って**見当違いの
	// お願い（「OIP を編集してください」）をしない**（実測で 1 回踏んだ）。
	for (const std::string& line : lines)
		if (line.find("!!! ① 中止:") != std::string::npos)
		{
			probe.fail("① が「この殻に調査用 PIO が入っていない」で中止している。"
					   "**OIP の編集ではなく、PR の Actions の成果物"
					   "（VwSdkProbes-mac / -windows）を手で入れて Vectorworks を再起動する**"
					   "のが先です（ピッカー先頭の入れ替えで取れるのは main の殻なので、"
					   "この PIO は入っていません）。入れ替えたら ① → OIP の編集 → ② の順で。");
			return;
		}

	// --- 2) Recalculate ごとに割る ------------------------------------------
	const std::vector<Block> blocks = SplitBlocks(lines);
	probe.log("");
	probe.log("=== Recalculate の塊 " + Int(static_cast<long>(blocks.size())) + " 個 ===");
	for (size_t i = 0; i < blocks.size(); ++i)
		probe.log("  #" + Int(static_cast<long>(i) + 1) + " 理由=" + blocks[i].state +
				  " 行数=" + Int(static_cast<long>(blocks[i].keys.size())));

	// --- 3) 2 つの文脈を取り出す --------------------------------------------
	const long externalIndex = FindLastBlock(blocks, "kObjectExternalReset");
	const long paramIndex = FindLastBlock(blocks, "kParameterChangedReset");

	probe.log("");
	probe.log("=== 突き合わせ ===");
	if (externalIndex < 0)
		probe.log("kObjectExternalReset（外からの ResetObject）の塊が無い"
				  "——`kObjXPropAcceptStates` が効いていないか、理由が別の綴りで来ている。"
				  "上の全文の『理由=』を読むこと。");
	if (blocks.empty())
	{
		// ① は PIO を作れたのに `Recalculate` の記録が 1 つも無い——上の (b)。
		probe.fail("`Recalculate` の記録が 1 つも無い（塊 0 個）。① は PIO を作れているので、"
				   "疑うのは殻の書き溜め（`pioTrace::Append`）か、① と ② で書き溜め先が"
				   "食い違っていること。上の「書き溜め先」のパスを ① の結果と見比べること。");
		return;
	}
	if (paramIndex < 0)
	{
		probe.fail("kParameterChangedReset（OIP でパラメータを編集）の塊が無い。"
				   "**図面の PIO を選び、OIP の「覚え書き」欄を編集してから、もう一度"
				   "このプローブを走らせてください。**");
	}

	if (externalIndex >= 0 && paramIndex >= 0)
	{
		const Block& fromReset = blocks[static_cast<size_t>(externalIndex)];
		const Block& fromOip = blocks[static_cast<size_t>(paramIndex)];
		probe.log("取り込み時のリセット = 塊 #" + Int(externalIndex + 1) + " / OIP 編集 = 塊 #" +
				  Int(paramIndex + 1));

		long same = 0;
		long differ = 0;
		long missing = 0;
		for (size_t i = 0; i < fromReset.keys.size(); ++i)
		{
			const std::string& key = fromReset.keys[i];
			const std::string* other = ValueFor(fromOip, key);
			if (other == nullptr)
			{
				++missing;
				probe.log("  [片方だけ] " + key + " … OIP 編集の側に無い");
				continue;
			}
			if (*other == fromReset.values[i])
			{
				++same;
				probe.log("  [同じ] " + key + ": " + fromReset.values[i]);
			}
			else
			{
				++differ;
				probe.log("  [違う] " + key);
				probe.log("      取り込み時: " + fromReset.values[i]);
				probe.log("      OIP 編集  : " + *other);
			}
		}
		for (size_t i = 0; i < fromOip.keys.size(); ++i)
			if (ValueFor(fromReset, fromOip.keys[i]) == nullptr)
			{
				++missing;
				probe.log("  [片方だけ] " + fromOip.keys[i] + " … 取り込み時の側に無い");
			}

		probe.log("");
		probe.log("結論: 同じ " + Int(same) + " 件 / 違う " + Int(differ) + " 件 / 片方だけ " +
				  Int(missing) + " 件");
		if (differ == 0 && missing == 0)
			probe.log("→ **2 つの文脈で見える環境は同一**（上の全項目が一致）");
		else
			probe.log("→ **違いがある**（上の [違う] / [片方だけ] の行がその全部）");
	}

	// --- 4) いまの PIO を外から見た値（参考）-------------------------------
	const MCObjectHandle pio = gSDK->GetNamedObject(kPioObjectName);
	probe.log("");
	if (pio == nullptr)
	{
		probe.log(std::string("図面に \"") + kPioObjectName +
				  "\" が見つからない（消したか、別の図面で走らせている）");
		return;
	}
	VWParametricObj obj(pio);
	probe.log(std::string("外から見た ") + kPioObjectName + ": " +
			  static_cast<const char*>(obj.GetParamName(0)) + " ほか " +
			  Int(static_cast<long>(obj.GetParamsCount())) + " 個 / TraceNote=\"" +
			  static_cast<const char*>(obj.GetParamValue(kParamNote)) + "\"");
}
