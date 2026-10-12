//
//	BridgeParse.h
//
//	MCP ブリッジの道具（McpBridge.cpp）が使う**純粋な部分**。SDK を知らないので
//	plugin/tests/BridgeSpoolTests.cpp が確かめる。
//
//	  * KeepTail   … 長いログを、古いほうを削って末尾だけ返す（MCP の 1 応答に収める）。
//	  * FindProbe  … slug（と群）から、カタログの 1 件を引き当てる。
//

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vwprobe::bridge
{
	// ログを返すときの既定の上限（バイト）。Claude が 1 度に読める量に収める。
	inline constexpr std::size_t kDefaultLogBytes = 60000;

	// text が limit を超えていれば**古いほう（先頭）を削って**末尾を返す。
	//   * 行の切れ目で切る（行の途中から始めない）。切れ目が無ければ、
	//   * UTF-8 の文字の途中からは始めない（継続バイト 10xxxxxx を読み飛ばす）。
	inline std::string KeepTail(const std::string& text, std::size_t limit)
	{
		if (text.size() <= limit)
			return text;
		std::size_t start = text.size() - limit;
		if (start > 0 && text[start - 1] == '\n')
			return text.substr(start); // ちょうど行の頭
		const std::size_t newline = text.find('\n', start);
		if (newline != std::string::npos && newline + 1 < text.size())
			return text.substr(newline + 1);
		while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xC0U) == 0x80U)
			++start;
		return text.substr(start);
	}

	// カタログの 1 件の鍵（slug と群）。
	struct ProbeKey
	{
		std::string id;
		std::string group;
	};

	inline constexpr std::size_t kNoProbe = static_cast<std::size_t>(-1);

	// id（と group）に合う 1 件の添字。**曖昧なら選ばない**——別々の PR が同じ slug を
	// 使っていることはあり（plugin/README.md）、取り違えると別の PR の版を走らせてしまう。
	// 見つからない・曖昧なときは kNoProbe と理由（候補の群を並べる）を返す。
	inline std::size_t FindProbe(const std::vector<ProbeKey>& keys, const std::string& id,
								 const std::string& group, std::string& error)
	{
		error.clear();
		std::vector<std::size_t> hits;
		for (std::size_t i = 0; i < keys.size(); ++i)
		{
			if (keys[i].id == id && (group.empty() || keys[i].group == group))
				hits.push_back(i);
		}
		if (hits.size() == 1)
			return hits[0];
		if (hits.empty())
		{
			error = "プローブが見つかりません: " + id;
			if (!group.empty())
				error += "（群 " + group + "）";
			error += "。vw_probes で一覧を確かめてください。";
			return kNoProbe;
		}
		error = "同じ slug のプローブが複数の群にあります: " + id + "（群:";
		for (const std::size_t hit : hits)
			error += " " + keys[hit].group;
		error += "）。group で群を指定してください。";
		return kNoProbe;
	}
} // namespace vwprobe::bridge
