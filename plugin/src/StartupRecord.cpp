//
//	StartupRecord.cpp
//
//	起動直後の記録の実装。設計の理由は StartupRecord.h に書いてある。
//

#include "PluginPrefix.h"

#include "StartupRecord.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace vwprobe
{
	namespace
	{
		// 図面が開いてから何刻み記録するか、と、記録の上限（ミリ秒）。
		constexpr int kTicksAfterOpen = 20;
		constexpr long long kMaxMs = 3LL * 60 * 1000;

		std::filesystem::path Dir()
		{
			std::error_code ec;
			std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
			return ec ? std::filesystem::path() : dir;
		}

		struct RecordState
		{
			bool decided = false; // 札を確かめたか
			bool active = false;
			std::string path;
			std::chrono::steady_clock::time_point startedAt;
			long long ticks = 0;
			int lastDocs = -2;
			const void* lastLayer = nullptr;
			std::string lastKey;
			int ticksAfterOpen = 0;
			long long docs0Null = 0, docs0Some = 0, docsNNull = 0, docsNSome = 0;
		};

		RecordState& State()
		{
			static RecordState sState;
			return sState;
		}

		// 1 行書いて閉じる。**毎回開き直す**ので、Vectorworks ごと落ちてもそこまでが残る。
		void Append(const std::string& path, const std::string& line)
		{
			std::ofstream out(path, std::ios::app);
			if (out)
				out << line << "\n";
		}

		std::string Hex(const void* p)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%p", p);
			return buf;
		}

		long long ElapsedMs(const RecordState& st)
		{
			return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
											  std::chrono::steady_clock::now() - st.startedAt)
											  .count());
		}

		void Finish(RecordState& st, const std::string& why)
		{
			st.active = false;
			Append(st.path, "counts docs0_layer_null=" + std::to_string(st.docs0Null) +
								" docs0_layer_some=" + std::to_string(st.docs0Some) +
								" docsN_layer_null=" + std::to_string(st.docsNNull) +
								" docsN_layer_some=" + std::to_string(st.docsNSome));
			Append(st.path, "end t_ms=" + std::to_string(ElapsedMs(st)) +
								" ticks=" + std::to_string(st.ticks) + " why=" + why);
		}

		// 札があれば消して記録を始める。無ければ以後何もしない。
		void Decide(RecordState& st)
		{
			st.decided = true;
			const std::filesystem::path dir = Dir();
			if (dir.empty())
				return;
			const std::filesystem::path request = dir / "VwSdkProbes-startup-record.request";
			std::error_code ec;
			if (!std::filesystem::exists(request, ec))
				return;
			// **SDK を呼ぶ前に札を消す**（落ちても次の起動で繰り返さない）。消せなければ
			// 記録しない——消せないまま落ちれば、起動のたびに落ちる。
			std::filesystem::remove(request, ec);
			if (ec || std::filesystem::exists(request, ec))
				return;
			st.path = (dir / "VwSdkProbes-startup-record.txt").string();
			std::ofstream(st.path, std::ios::trunc).close();
			st.active = true;
			st.startedAt = std::chrono::steady_clock::now();
#if GS_WIN
			Append(st.path, "begin platform=Windows");
#else
			Append(st.path, "begin platform=mac");
#endif
		}

		void Record(RecordState& st)
		{
			++st.ticks;
			const long long now = ElapsedMs(st);
			if (now > kMaxMs)
			{
				Finish(st, "time-limit");
				return;
			}

			VectorWorks::TVWArray_OpenFileInformation list;
			gSDK->GetOpenFilesList(list);
			const int docs = static_cast<int>(list.GetSize());
			if (docs != st.lastDocs)
			{
				Append(st.path,
					   "before t_ms=" + std::to_string(now) + " tick=" + std::to_string(st.ticks) +
						   " docs=" + std::to_string(docs) + " → GetCurrentLayer() を呼ぶ");
				st.lastDocs = docs;
			}
			MCObjectHandle layer = gSDK->GetCurrentLayer();
			std::string key = "docs=" + std::to_string(docs) +
							  " layer=" + (layer == nullptr ? std::string("nullptr") : Hex(layer));
			if (layer != nullptr)
			{
				if (layer != st.lastLayer)
					Append(st.path, "before t_ms=" + std::to_string(now) + " layer=" + Hex(layer) +
										" → GetObjectName / GetObjectTypeN を呼ぶ");
				TXString name;
				gSDK->GetObjectName(layer, name);
				const char* utf8 = static_cast<const char*>(name);
				key += " name=\"" + std::string(utf8 != nullptr ? utf8 : "") +
					   "\" type=" + std::to_string(gSDK->GetObjectTypeN(layer));
			}
			st.lastLayer = layer;
			const bool agree = (docs > 0) == (layer != nullptr);
			key += agree ? " agree=yes" : " agree=NO";
			if (docs == 0)
				++(layer == nullptr ? st.docs0Null : st.docs0Some);
			else
				++(layer == nullptr ? st.docsNNull : st.docsNSome);
			if (key != st.lastKey)
			{
				Append(st.path, "state t_ms=" + std::to_string(now) +
									" tick=" + std::to_string(st.ticks) + " " + key);
				st.lastKey = key;
			}
			if (docs > 0 && ++st.ticksAfterOpen >= kTicksAfterOpen)
				Finish(st, "opened");
		}
	} // namespace

	// NOLINTBEGIN(bugprone-empty-catch): OS のタイマーのコールバックへ例外を漏らさない。
	void StartupRecordTick()
	{
		RecordState& st = State();
		try
		{
			if (!st.decided)
				Decide(st);
			if (st.active && gSDK != nullptr)
				Record(st);
		}
		catch (...)
		{
			st.active = false;
		}
	}
	// NOLINTEND(bugprone-empty-catch)
} // namespace vwprobe
