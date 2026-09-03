//
//	PayloadCatalog.h
//
//	**カタログ**——「どの本体（.vwpayload）に、どのプローブが入っているか」の索引を読む、
//	純粋な部分。
//
//	【なぜ索引が要るか】本体は**群ごとに分かれている**（main のプローブが 1 本、PR の
//	プローブが PR ごとに 1 本。plugin/CMakeLists.txt）。殻はメニューを開いた時点では
//	**どれも読み込まず**、ダイアログで選ばれた 1 本だけを読む（plugin/src/ProbeMenu.cpp）。
//
//	  * 一覧を出すためだけに全部を読み込むと、群の数だけ 0.3〜0.4 秒が積み上がる。
//	  * **1 つの群が壊れていても（版が違う・ビルドできなかった）、他の群は選べる。**
//	    読み込みで転ぶのは、選ばれたその 1 本だけになる。
//
//	だから「本体を読み込まずに読める一覧」が要る。それがビルドが並べて置くテキスト 1 枚
//	（VwSdkProbes.probes.txt）で、ここはその読み手である。
//
//	【形】UTF-8・1 行 1 件・"|" 区切り（書き手は plugin/cmake/ProbeCatalog.cmake）:
//
//	    # コメント
//	    version=1
//	    build=<ビルド ID>        … 自動アップデートが新旧を比べる鍵
//	    shell=<殻の ID>          … 入れ替えに再起動が要るかを決める鍵
//	    built=<ISO 8601>
//	    probes=<1 行の要約>
//	    group|<群>|<ファイル名>|<PR>|<commit>|<branch>|<PR タイトル>
//	    probe|<群>|<slug>|<表示名>|<概要>
//
//	【壊れた行は飛ばす】カタログが少し壊れていても、**読めた行だけで一覧を出す**。
//	1 行の綴じ違いで「プローブが 1 件も無い」になるほうが困る（何も走らせられない）。
//	飛ばした行数は数えて返すので、呼び出し側は「n 行読めませんでした」と添えられる。
//
//	【SDK にもプラットフォームにも依存しない】std::string だけで書いてあるので、
//	plugin/tests/PayloadCatalogTests.cpp が SDK 抜きでそのまま確かめられる
//	（UpdateParse.h / PayloadHost.h の純粋な部分と同じ作法）。
//

#pragma once

#include <string>
#include <vector>

namespace vwprobe
{
	namespace catalog
	{
		// 群 1 つ（＝本体 1 本）。出所は**群に付く**——1 つの群のプローブは、すべて同じ
		// PR・同じコミットから来ているため（集約がそう分けている）。
		struct Group
		{
			std::string id;		// "main" / "pr12"
			std::string file;	// "VwSdkProbesPayload-pr12.vwpayload"（殻の隣に置かれる）
			std::string pr;		// PR 番号（main 由来は空）
			std::string commit; // 短縮 sha
			std::string branch; //
			std::string prTitle; // PR のタイトル（あれば）
		};

		// プローブ 1 件。**表示名と概要はコードにしか無い**（出所と違ってビルドでは
		// 決まらない）ので、ビルドのときにソースから読んでここへ写してある。
		struct Probe
		{
			std::string group;
			std::string id; // slug
			std::string title;
			std::string summary;
		};

		struct Catalog
		{
			std::string version;
			std::string buildId;
			std::string shellId;
			std::string branch;
			std::string commit;
			std::string buildTime;
			std::string probesLine; // 1 行の要約（リリース本文の probes= と同じもの）
			std::vector<Group> groups;
			std::vector<Probe> probes;
			// 読めなかった行の数（0 でなければ、カタログが壊れているか版が新しい）。
			int skippedLines = 0;

			bool empty() const
			{
				return groups.empty() && probes.empty();
			}

			const Group* groupOf(const std::string& id) const
			{
				for (const Group& group : groups)
				{
					if (group.id == id)
						return &group;
				}
				return nullptr;
			}
		};

		// 1 行を "|" で切る。**最後の項目だけは残り全部**を取る（PR タイトルや概要に
		// 区切り文字が混ざっても、そこで列がずれないように。書き手も落としてはいるが、
		// 読み手が壊れないほうが大事）。
		inline std::vector<std::string> SplitFields(const std::string& line, size_t fields)
		{
			std::vector<std::string> out;
			out.reserve(fields);
			std::string::size_type at = 0;
			while (out.size() + 1 < fields)
			{
				const std::string::size_type bar = line.find('|', at);
				if (bar == std::string::npos)
					break;
				out.push_back(line.substr(at, bar - at));
				at = bar + 1;
			}
			out.push_back(line.substr(at));
			return out;
		}

		// 行末の CR を落とす（Windows で書かれた／展開されたときのため）。
		inline std::string TrimEol(const std::string& line)
		{
			std::string out = line;
			while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
				out.pop_back();
			return out;
		}

		// カタログの本文を読む。**例外を投げない**（壊れた行は飛ばして数える）。
		inline Catalog Parse(const std::string& text)
		{
			Catalog out;
			std::string::size_type at = 0;
			while (at <= text.size())
			{
				const std::string::size_type eol = text.find('\n', at);
				const std::string raw = TrimEol(
					text.substr(at, (eol == std::string::npos) ? std::string::npos : eol - at));
				at = (eol == std::string::npos) ? text.size() + 1 : eol + 1;

				if (raw.empty() || raw[0] == '#')
					continue;

				if (raw.compare(0, 6, "group|") == 0)
				{
					const std::vector<std::string> f = SplitFields(raw, 7);
					if (f.size() < 7 || f[1].empty() || f[2].empty())
					{
						++out.skippedLines;
						continue;
					}
					Group group;
					group.id = f[1];
					group.file = f[2];
					group.pr = f[3];
					group.commit = f[4];
					group.branch = f[5];
					group.prTitle = f[6];
					out.groups.push_back(group);
					continue;
				}

				if (raw.compare(0, 6, "probe|") == 0)
				{
					const std::vector<std::string> f = SplitFields(raw, 5);
					if (f.size() < 5 || f[1].empty() || f[2].empty())
					{
						++out.skippedLines;
						continue;
					}
					Probe probe;
					probe.group = f[1];
					probe.id = f[2];
					probe.title = f[3].empty() ? f[2] : f[3];
					probe.summary = f[4];
					out.probes.push_back(probe);
					continue;
				}

				const std::string::size_type equals = raw.find('=');
				if (equals == std::string::npos)
				{
					++out.skippedLines;
					continue;
				}
				const std::string key = raw.substr(0, equals);
				const std::string value = raw.substr(equals + 1);
				if (key == "version")
					out.version = value;
				else if (key == "build")
					out.buildId = value;
				else if (key == "shell")
					out.shellId = value;
				else if (key == "branch")
					out.branch = value;
				else if (key == "commit")
					out.commit = value;
				else if (key == "built")
					out.buildTime = value;
				else if (key == "probes")
					out.probesLine = value;
				else
					++out.skippedLines; // 知らない鍵（版が新しい？）。数えるだけ。
			}
			return out;
		}
	} // namespace catalog
} // namespace vwprobe
