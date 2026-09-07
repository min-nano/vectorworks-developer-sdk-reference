//
//	ProbeMenuText.h
//
//	ダイアログに出す文字列の**組み立てだけ**（SDK にもプラットフォームにも依存しない）。
//	ProbeMenu.cpp から使い、plugin/tests/ProbeMenuTextTests.cpp が CI の lint で毎回
//	確かめる。
//
//	【なぜ切り出すか】**ダイアログの横幅は、いちばん長いコントロールで決まる**
//	（レイアウトダイアログの大きさは作るときに 1 度だけ決まる。Findings「Layout Dialogs」）。
//	素性の行（ブランチ名・コミット・ビルド ID・時刻）は放っておくと 90 文字を超え、
//	**プルダウンの倍以上に横へ伸びたダイアログ**になる（実機で確認）。だから
//	「どこまで詰めるか」をここに集めて、長さを試験できるようにしてある。
//

#pragma once

#include <cstddef>
#include <string>

namespace vwprobe
{
	namespace text
	{
		// 素性の行に出すブランチ名の上限（文字数）。`claude/probe-auto-update-workflow-2r7a8d`
		// のような自動生成のブランチ名がそのまま横幅になるので、ここで頭打ちにする。
		constexpr std::size_t kBranchChars = 22;

		// UTF-8 の**文字数**（コードポイント）。バイト数で切ると多バイト文字を割って
		// しまうので、詰めるときは必ずこちらで数える。
		inline std::size_t CharCount(const std::string& text)
		{
			std::size_t count = 0;
			for (const char c : text)
			{
				// 継続バイト（10xxxxxx）以外が 1 文字の先頭。
				if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
					++count;
			}
			return count;
		}

		// maxChars 文字に収める（超えたら末尾を「…」にする）。
		inline std::string Ellipsize(const std::string& text, std::size_t maxChars)
		{
			if (maxChars == 0)
				return "";
			if (CharCount(text) <= maxChars)
				return text;

			// 「…」のぶんを 1 文字空ける。
			const std::size_t keep = maxChars - 1;
			std::size_t count = 0;
			std::size_t cut = text.size();
			for (std::size_t i = 0; i < text.size(); ++i)
			{
				if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
					continue; // 継続バイト
				if (count == keep)
				{
					cut = i;
					break;
				}
				++count;
			}
			return text.substr(0, cut) + "…";
		}

		// ビルド時刻を詰める。"2026-09-07T10:46:35Z" → "09-07 10:46"。
		// **年と秒は落とす**——見たいのは「さっき入れ替えたものか」だけで、年をまたいで
		// 見比べることは無い（正確な値はリリースノートにある）。形が違えばそのまま返す。
		inline std::string ShortTime(const std::string& iso)
		{
			if (iso.size() < 16 || iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' ||
				iso[13] != ':')
				return iso;
			return iso.substr(5, 5) + " " + iso.substr(11, 5);
		}

		// 素性の 1 行。`<見出し>: <ブランチ> <コミット> id=<ビルドID> (<時刻>)`
		// 空の項目は詰めて出さない（「殻: 　 id=…」のような隙間を作らない）。
		inline std::string StampLine(const std::string& label, const std::string& branch,
									 const std::string& commit, const std::string& buildTime,
									 const std::string& buildId)
		{
			std::string line = label + ":";
			const auto add = [&line](const std::string& part)
			{
				if (!part.empty())
					line += " " + part;
			};
			add(Ellipsize(branch, kBranchChars));
			add(commit);
			add(buildId.empty() ? std::string() : "id=" + buildId);
			add(buildTime.empty() ? std::string() : "(" + ShortTime(buildTime) + ")");
			return line;
		}
	} // namespace text
} // namespace vwprobe
