//
//	Probe.cpp
//
//	プローブのレジストリ（Probe.h の実装）。
//
//	【静的初期化の順序に依存しない】プローブの登録（VW_PROBE が作る静的オブジェクト）と
//	出所表の登録（生成された ProbeProvenance.cpp の静的オブジェクト）は**別々の翻訳単位**の
//	静的オブジェクトなので、どちらが先に走るかは決まっていない。そこで器はどちらも
//	**関数ローカル static**（最初に触ったときに作られる）にしてある——順序に依存する形で
//	名前空間スコープの器を持つと、片方が空の器へ登録して消える。
//
//	【読むのは実行時だけ】probes() / provenanceOf() を呼ぶのはメニューコマンドが走るとき
//	だけで、そのときには静的初期化はすべて終わっている。
//

#include "PluginPrefix.h"
#include "Probe.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace vwprobe
{
	namespace
	{
		// 登録の器（上記のとおり関数ローカル static）。
		std::vector<Probe>& registry()
		{
			static std::vector<Probe> sRegistry;
			return sRegistry;
		}

		std::vector<Provenance>& provenanceTable()
		{
			static std::vector<Provenance> sTable;
			return sTable;
		}

		// 環境変数を std::string で読む（未設定・空なら空文字）。
		std::string envValue(const char* name)
		{
			// NOLINTNEXTLINE(concurrency-mt-unsafe): 起動直後の 1 度きりで、他スレッドは無い。
			const char* value = std::getenv(name);
			return (value != nullptr) ? std::string(value) : std::string();
		}
	} // namespace

	// -----------------------------------------------------------------------
	Registrar::Registrar(const char* id, const char* title, const char* summary, ProbeFn run)
	{
		Probe probe;
		probe.id = (id != nullptr) ? id : "";
		probe.title = (title != nullptr) ? title : "";
		probe.summary = (summary != nullptr) ? summary : "";
		probe.run = run;
		registry().push_back(probe);
	}

	ProvenanceRegistrar::ProvenanceRegistrar(const std::vector<Provenance>& entries)
	{
		std::vector<Provenance>& table = provenanceTable();
		table.insert(table.end(), entries.begin(), entries.end());
	}

	// -----------------------------------------------------------------------
	const std::vector<Probe>& probes()
	{
		// id 昇順に整えたものを 1 度だけ作って返す（**リンク順・初期化順に依存しない**
		// 決定的な並び。並べ替えの基準は id ひとつで、表示の順序はメニュー側が決める）。
		static const std::vector<Probe> sSorted = []
		{
			std::vector<Probe> all = registry();
			std::sort(all.begin(), all.end(),
					  [](const Probe& a, const Probe& b) { return a.id < b.id; });
			return all;
		}();
		return sSorted;
	}

	const Provenance* provenanceOf(const std::string& id)
	{
		for (const Provenance& entry : provenanceTable())
		{
			if (entry.id == id)
				return &entry;
		}
		return nullptr;
	}

	std::string defaultLogPath(const std::string& id)
	{
		// 差し替えの逃げ道（一時ディレクトリ以外へ出したいとき）。
		const std::string custom = envValue("VW_PROBE_LOG");
		if (!custom.empty())
			return custom;

#if defined(_WINDOWS)
		std::string dir = envValue("TEMP");
		if (dir.empty())
			dir = envValue("TMP");
		if (dir.empty())
			dir = "C:\\Windows\\Temp";
		const char separator = '\\';
#else
		std::string dir = envValue("TMPDIR");
		if (dir.empty())
			dir = "/tmp";
		const char separator = '/';
#endif
		if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
			dir.pop_back();
		return dir + separator + "VwSdkProbes-" + id + ".log";
	}

	// -----------------------------------------------------------------------
	Report::~Report()
	{
		if (fFile != nullptr)
		{
			(void)std::fclose(static_cast<std::FILE*>(fFile));
			fFile = nullptr;
		}
	}

	void Report::openLog(const std::string& path)
	{
		if (path.empty())
			return;
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): FILE* は fclose で閉じる（デストラクタ）。
		std::FILE* file = std::fopen(path.c_str(), "wb");
		if (file == nullptr)
			return; // 開けなくても黙って続ける（本文はメモリに溜まる）
		fFile = file;
		fLogPath = path;
	}

	void Report::log(const std::string& line)
	{
		fText += line;
		fText += '\n';
		if (fFile != nullptr)
		{
			std::FILE* file = static_cast<std::FILE*>(fFile);
			(void)std::fputs(line.c_str(), file);
			(void)std::fputc('\n', file);
			// **1 行ごとに flush する。** プローブが VectorWorks ごと落ちても、そこまでが
			// ファイルに残っているようにするため（落ちること自体が知見になる調査がある）。
			(void)std::fflush(file);
		}
	}

	void Report::log(const TXString& line)
	{
		// TXString の operator const char*() は UTF-8。
		this->log(std::string(static_cast<const char*>(line)));
	}

	void Report::fail(const std::string& why)
	{
		if (!fFailed)
		{
			fFailed = true;
			fFailure = why;
		}
		this->log("【失敗】" + why);
	}
} // namespace vwprobe
