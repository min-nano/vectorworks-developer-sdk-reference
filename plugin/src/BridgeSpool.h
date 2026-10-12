//
//	BridgeSpool.h
//
//	**MCP ブリッジの、SDK を知らない半分。** 要求／応答の形と、その受け渡しに使う
//	スプール（ファイルの置き場）の作法だけを持つ。Vectorworks で何をするか（＝道具の中身）は
//	McpBridge.cpp にある。
//
//	    Claude Code ──MCP(stdio)──▶ plugin/mcp/vw-mcp-server.py
//	                                   │ <id>.req.json を書く／<id>.res.json を待つ
//	                                   ▼
//	                             スプール（一時ディレクトリの VwSdkProbes-mcp）
//	                                   ▲
//	                                   │ 取得する／応答する（Vectorworks が動いている間ずっと）
//	                             Vectorworks（殻の時計が数百 ms ごとに McpBridge を呼ぶ）
//
//	【出どころ】実プラグイン（vectorworks-plugin-import-ifc-homeskz の src/core/Bridge.{h,cpp}）の
//	作法をそのまま写したもの（ヘッダだけの形にした。BridgeJson.h と同じ理由）。Python 側も
//	同じものを写しているので、**綴り（ファイル名の接尾辞・生存の印の判定）を変えるときは
//	plugin/mcp/vw-mcp-server.py と両方を直す**（plugin/tests/vw-mcp-server.test.py が
//	Python 側を、plugin/tests/BridgeSpoolTests.cpp がこちらを押さえている）。
//
//	【なぜファイルなのか】ソケットを開くと (1) macOS のファイアウォールが利用者に問い、
//	(2) Winsock と BSD ソケットで分岐が増え、(3) 受け口をメインスレッド以外に置くと SDK 呼び出しの
//	前提（Vectorworks はメインスレッドから呼ぶ）を壊す。**ファイルならどれも起きない**——
//	ブリッジは Vectorworks のメインスレッドで時計に呼ばれるたびに、置かれたファイルを取得して
//	応答するだけでよい。速さは要らない（人が待つ対話の速度）。
//
//	【安全側の決めごと】スプールへ書くのは**別プロセス**である。したがって:
//	  * id は綴りを検査してからファイル名に使う（`../` を混ぜてスプールの外へ書かせない）。
//	  * 要求 1 件の大きさに上限を設ける（巨大なファイルを丸ごと読み込まない）。
//	  * 1 回のポーリングで処理する件数に上限を設ける（溜まっていても Vectorworks を占有し
//	    続けない）。
//	  * 壊れた要求は**削除してから**エラーで応答する（同じものを永久に再取得しない）。
//	  * スプールは**自分のもので・自分にしか書けない**ディレクトリでなければ使わない（POSIX）。
//

#pragma once

#include "BridgeJson.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#	include <sys/stat.h>
#	include <unistd.h>
#endif

namespace vwprobe::bridge
{
	// スプールに置くファイルの綴り。**Python 側（plugin/mcp/vw-mcp-server.py）と対。**
	inline constexpr const char* kRequestSuffix = ".req.json";
	inline constexpr const char* kResponseSuffix = ".res.json";
	inline constexpr const char* kStatusFile = "bridge.json";
	inline constexpr const char* kTempSuffix = ".tmp";

	// 受け渡しの版。**要求／応答の形を変えたら上げる**（プラグインと Python サーバは別々に
	// 配られうる）。生存の印に載せて Python 側が確かめる。
	inline constexpr int kProtocolVersion = 1;

	// 要求 1 件の大きさの上限（バイト）と、1 回のポーリングで処理する件数の上限。
	inline constexpr std::size_t kMaxRequestBytes = 1U << 20U; // 1 MiB
	inline constexpr std::size_t kMaxRequestsPerPoll = 16;

	// 生存の印（`beat`）がこれより古ければ「動いていない」と判定する（秒）。**Python 側の
	// STATUS_STALE_SECONDS と対。** プラグイン側は数秒ごとに書き直す。
	inline constexpr long long kStatusStaleSeconds = 15;

	// スプールの置き場所（一時ディレクトリの下の `<プラグイン名>-mcp`）。区切りは '/'
	// （Windows も受け付ける）。**両側が同じ場所を指すとは限らない**——一時ディレクトリは
	// 環境変数で決まるので、探すのは Python 側の役割にしてある（spool_candidates）。
	inline std::string SpoolDir(const std::string& tempDir, const std::string& pluginName)
	{
		std::string base = tempDir;
		while (!base.empty() && (base.back() == '/' || base.back() == '\\'))
			base.pop_back();
		return base + "/" + pluginName + "-mcp";
	}

	// id としてファイル名に使ってよい綴りか（英数字・'-'・'_' のみ、1〜64 文字）。
	// **これがスプールの外へ書かせないための唯一の関門**である。
	inline bool IsValidId(const std::string& id)
	{
		if (id.empty() || id.size() > 64)
			return false;
		return std::all_of(id.begin(), id.end(),
						   [](const char ch)
						   {
							   return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
									  (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '_';
						   });
	}

	// -----------------------------------------------------------------------
	// 要求 1 件。
	struct Request
	{
		std::string id;	  // 応答のファイル名になる
		std::string tool; // 道具の名前（McpBridge.cpp の表を引く）
		Json args;		  // 道具ごとの引数（オブジェクト）
	};

	// 応答 1 件。ok が false のときだけ error に理由が入る。
	struct Response
	{
		std::string id;
		bool ok = false;
		std::string error;
		Json result;
	};

	// 要求のテキスト → Request。id / tool が無い・空なら失敗。
	inline bool ParseRequest(const std::string& text, Request& out, std::string& error)
	{
		Json value;
		if (!Json::parse(text, value, error))
			return false;
		if (!value.isObject())
		{
			error = "要求はオブジェクトでなければなりません";
			return false;
		}
		Request made;
		made.id = value.at("id").asString();
		made.tool = value.at("tool").asString();
		made.args = value.at("args");
		if (!IsValidId(made.id))
		{
			error = "id の綴りが不正です";
			return false;
		}
		if (made.tool.empty())
		{
			error = "tool がありません";
			return false;
		}
		if (!made.args.isObject())
			made.args = Json::object();
		out = std::move(made);
		return true;
	}

	// 応答 → JSON テキスト。
	inline std::string DumpResponse(const Response& response)
	{
		Json value = Json::object();
		value.set("id", Json::string(response.id));
		value.set("ok", Json::boolean(response.ok));
		if (!response.ok)
			value.set("error", Json::string(response.error));
		else
			value.set("result", response.result.isNull() ? Json::object() : response.result);
		return value.dump();
	}

	// 失敗の応答を 1 つ作る短縮。
	inline Response Failure(const std::string& id, const std::string& error)
	{
		Response response;
		response.id = id;
		response.ok = false;
		response.error = error;
		return response;
	}

	namespace spool_detail
	{
		// 上限つきで丸ごと読む。上限を越えたら false（内容は捨てる）。
		inline bool ReadCapped(const std::filesystem::path& path, std::size_t limit,
							   std::string& out)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
				return false;
			out.clear();
			std::array<char, 4096> buffer{};
			while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
				   in.gcount() > 0)
			{
				const auto got = static_cast<std::size_t>(in.gcount());
				if (out.size() + got > limit)
					return false;
				out.append(buffer.data(), got);
				if (!in)
					break;
			}
			return true;
		}

		// 同じディレクトリへ書いてから rename する。**読み手に半端な内容を読み取らせない**。
		inline bool WriteAtomically(const std::filesystem::path& target, const std::string& text,
									std::string& error)
		{
			std::filesystem::path temp = target;
			temp += kTempSuffix;
			{
				std::ofstream out(temp, std::ios::binary | std::ios::trunc);
				if (!out)
				{
					error = "書き込めません: " + temp.string();
					return false;
				}
				out.write(text.data(), static_cast<std::streamsize>(text.size()));
				if (!out)
				{
					error = "書き込みに失敗しました: " + temp.string();
					return false;
				}
			}
			std::error_code ec;
			std::filesystem::rename(temp, target, ec);
			if (ec)
			{
				std::filesystem::remove(temp, ec);
				error = "置き換えに失敗しました: " + target.string();
				return false;
			}
			return true;
		}

		inline bool EndsWith(const std::string& text, const std::string& suffix)
		{
			return text.size() >= suffix.size() &&
				   text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
		}
	} // namespace spool_detail

	// -----------------------------------------------------------------------
	// **スプール 1 つ。** ディレクトリを用意し、要求を取得し、応答を書く。SDK を知らないので
	// 単体テストできる（plugin/tests/BridgeSpoolTests.cpp）。
	class Spool
	{
	public:
		explicit Spool(std::string dir) : fDir(std::move(dir)) {}

		const std::string& dir() const
		{
			return fDir;
		}

		// ディレクトリを用意する。作れない・**持ち主や権限が不適切な**ときは false と理由。
		// `/tmp` は同じ計算機の他の利用者からも書けるので、自分のもので・自分にしか書けない
		// ディレクトリでなければ使わない（POSIX のみ。Windows の %TEMP% は利用者ごと）。
		bool prepare(std::string& error) const
		{
			error.clear();
			if (fDir.empty())
			{
				error = "スプールの場所が決まりません（一時ディレクトリが読めない？）";
				return false;
			}
#if defined(_WIN32)
			std::error_code ec;
			std::filesystem::create_directories(fDir, ec);
			if (ec && !std::filesystem::is_directory(fDir))
			{
				error = "スプールを作れません: " + fDir + "（" + ec.message() + "）";
				return false;
			}
			return true;
#else
			// **自分にしか書けない形で作る**（0700）。親（一時ディレクトリ）は既にあるので 1 段でよい。
			if (::mkdir(fDir.c_str(), S_IRWXU) != 0 && errno != EEXIST)
			{
				error = "スプールを作れません: " + fDir;
				return false;
			}
			// `stat` は関数名でもあるので型は別名で綴る。
			using FileStat = struct stat;
			FileStat info{};
			if (::stat(fDir.c_str(), &info) != 0)
			{
				error = "スプールを確かめられません: " + fDir;
				return false;
			}
			if (!S_ISDIR(info.st_mode))
			{
				error = "スプールの場所がディレクトリではありません: " + fDir;
				return false;
			}
			if (info.st_uid != ::geteuid())
			{
				error = "スプールが他の利用者のものです: " + fDir;
				return false;
			}
			if ((info.st_mode & (S_IWGRP | S_IWOTH)) != 0)
			{
				error = "スプールが他からも書き込める状態です: " + fDir;
				return false;
			}
			return true;
#endif
		}

		// 前の回の残骸（要求・応答・書きかけ）を削除する。**開始時に 1 回**呼ぶ——異常終了した
		// 回の応答を新しい回のものと取り違えないため。消した数を返す。
		std::size_t sweep() const
		{
			std::size_t removed = 0;
			std::error_code ec;
			const std::filesystem::directory_iterator it(fDir, ec);
			if (ec)
				return 0;
			for (const auto& entry : it)
			{
				const std::string name = entry.path().filename().string();
				if (!spool_detail::EndsWith(name, kRequestSuffix) &&
					!spool_detail::EndsWith(name, kResponseSuffix) &&
					!spool_detail::EndsWith(name, kTempSuffix))
					continue;
				std::error_code removeEc;
				if (std::filesystem::remove(entry.path(), removeEc))
					++removed;
			}
			return removed;
		}

		// 生存の印が在り、`beat` が now から staleSeconds 以内か（now は epoch 秒）。
		bool statusIsLive(long long now, long long staleSeconds) const
		{
			std::string text;
			if (!spool_detail::ReadCapped(std::filesystem::path(pathFor(kStatusFile)),
										  kMaxRequestBytes, text))
				return false;
			Json status;
			std::string error;
			if (!Json::parse(text, status, error) || !status.has("beat"))
				return false;
			const auto beat = static_cast<long long>(status.at("beat").asNumber(0.0));
			// 未来の印（時計が戻った）も「生きている」とみなす——削除せずに済むほうが安全。
			return now - beat <= staleSeconds;
		}

		// 置かれている要求を**名前の昇順で**取り出し、そのファイルを消す。名前の昇順は
		// Python が付ける連番の順（＝送った順）である。読めなかった要求は戻り値に入れず、
		// その id だけを `broken` に載せる（呼ぶ側はそれにエラーで応答すればよい）。
		std::vector<Request> poll(std::vector<std::string>& broken) const
		{
			std::vector<Request> requests;
			broken.clear();

			std::vector<std::string> names;
			{
				std::error_code ec;
				const std::filesystem::directory_iterator it(fDir, ec);
				if (ec)
					return requests;
				for (const auto& entry : it)
				{
					const std::string name = entry.path().filename().string();
					if (spool_detail::EndsWith(name, kRequestSuffix))
						names.push_back(name);
				}
			}
			std::sort(names.begin(), names.end());
			if (names.size() > kMaxRequestsPerPoll)
				names.resize(kMaxRequestsPerPoll);

			for (const std::string& name : names)
			{
				const std::filesystem::path path(pathFor(name));
				std::string text;
				const bool read = spool_detail::ReadCapped(path, kMaxRequestBytes, text);

				// **読めても読めなくても、まず削除する。** 残すと同じものを毎回再取得する。
				std::error_code ec;
				std::filesystem::remove(path, ec);

				const std::string id =
					name.substr(0, name.size() - std::string(kRequestSuffix).size());

				Request request;
				std::string error;
				// 中身の id とファイル名が食い違うものは受けない（応答の宛先が定まらない）。
				if (!read || !ParseRequest(text, request, error) || request.id != id)
				{
					if (IsValidId(id))
						broken.push_back(id);
					continue;
				}
				requests.push_back(std::move(request));
			}
			return requests;
		}

		// 応答を書く（同じディレクトリへ書いてから rename する）。
		bool reply(const Response& response, std::string& error) const
		{
			error.clear();
			if (!IsValidId(response.id))
			{
				error = "id の綴りが不正です";
				return false;
			}
			return spool_detail::WriteAtomically(
				std::filesystem::path(pathFor(response.id + kResponseSuffix)),
				DumpResponse(response), error);
		}

		// 生存の印。ブリッジが動いている間だけ置かれる。
		bool writeStatus(const Json& status, std::string& error) const
		{
			error.clear();
			return spool_detail::WriteAtomically(std::filesystem::path(pathFor(kStatusFile)),
												 status.dump(), error);
		}

	private:
		std::string fDir;

		std::string pathFor(const std::string& name) const
		{
			return fDir + "/" + name;
		}
	};
} // namespace vwprobe::bridge
