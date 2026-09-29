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
#include "PioRecalcTrace.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
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
	using namespace vwprobe;

	probe.log(std::string("書き溜め先: ") + pioTrace::Path());
	const std::vector<std::string> lines = pioTrace::Read();
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
	const MCObjectHandle pio = gSDK->GetNamedObject(pioTrace::kPioObjectName);
	probe.log("");
	if (pio == nullptr)
	{
		probe.log(std::string("図面に \"") + pioTrace::kPioObjectName +
				  "\" が見つからない（消したか、別の図面で走らせている）");
		return;
	}
	VWParametricObj obj(pio);
	probe.log(std::string("外から見た ") + pioTrace::kPioObjectName + ": " +
			  static_cast<const char*>(obj.GetParamName(0)) + " ほか " +
			  Int(static_cast<long>(obj.GetParamsCount())) + " 個 / TraceNote=\"" +
			  static_cast<const char*>(obj.GetParamValue(pioTrace::kParamNote)) + "\"");
}
