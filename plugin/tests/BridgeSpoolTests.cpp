//
//	BridgeSpoolTests.cpp
//
//	**MCP ブリッジの受け渡し**（plugin/src/BridgeJson.h / BridgeSpool.h /
//	BridgeParse.h）の単体テスト。
//
//	【なぜ要るか】受け渡しは別プロセス（plugin/mcp/vw-mcp-server.py）と対になっていて、
//	**壊れても静かに壊れる**——綴りが 1 文字ずれれば Python 側は「応答がありません」と
//	待ち続けるだけで、実機では何が起きているか分からない。SDK にもプラットフォームにも
//	依存しない部分なので、CI の lint で毎回走らせる。
//
//	走らせ方（CI の lint ワークフローも同じ）:
//	    g++ -std=c++20 -Wall -Wextra -Werror -I plugin/src
//	        plugin/tests/BridgeSpoolTests.cpp -o /tmp/t && /tmp/t
//

#include "BridgeParse.h"
#include "BridgeSpool.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#	include <sys/stat.h>
#endif

using namespace vwprobe::bridge;

namespace
{
	int gFailures = 0;

	void checkEq(const std::string& actual, const std::string& expected, const char* what)
	{
		if (actual != expected)
		{
			std::printf("FAIL: %s\n  期待: [%s]\n  実際: [%s]\n", what, expected.c_str(),
						actual.c_str());
			++gFailures;
		}
	}

	void checkEq(size_t actual, size_t expected, const char* what)
	{
		if (actual != expected)
		{
			std::printf("FAIL: %s\n  期待: [%zu]\n  実際: [%zu]\n", what, expected, actual);
			++gFailures;
		}
	}

	void checkTrue(bool value, const char* what)
	{
		if (!value)
		{
			std::printf("FAIL: %s\n", what);
			++gFailures;
		}
	}

	void writeFile(const std::filesystem::path& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << text;
	}

	std::string readFile(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return text;
	}

	// テストごとの空のスプール（一時ディレクトリの下。終わったら消す）。
	std::filesystem::path freshRoot()
	{
		std::error_code ec;
		const std::filesystem::path root =
			std::filesystem::temp_directory_path(ec) / "vwprobe-bridge-spool-test";
		std::filesystem::remove_all(root, ec);
		std::filesystem::create_directories(root, ec);
		return root;
	}
} // namespace

int main()
{
	// --- JSON: 読み書きが往復し、日本語はそのまま通る ------------------------
	{
		Json value;
		std::string error;
		checkTrue(Json::parse(R"({"a":[1,2.5,"xあ"],"b":null,"c":true})", value, error),
				  "素直な JSON を読める");
		checkEq(value.dump(), R"({"a":[1,2.5,"xあ"],"b":null,"c":true})", "書き戻しが一致する");
		checkEq(value.at("missing").asString("既定"), "既定", "無い鍵は既定値");
	}

	// --- JSON: 壊れた入力・深すぎる入力で落ちない ---------------------------
	{
		Json value;
		std::string error;
		checkTrue(!Json::parse("{\"a\":", value, error), "途中で切れた JSON は失敗");
		checkTrue(!error.empty(), "失敗の理由が入る");
		checkTrue(!Json::parse(std::string(200, '['), value, error), "入れ子が深すぎれば失敗");
	}

	// --- id の綴り（スプールの外へ書かせない唯一の関門）-----------------------
	{
		checkTrue(IsValidId("000000000001-abcd1234"), "Python が付ける形の id は通る");
		checkTrue(!IsValidId("../evil"), "../ は通さない");
		checkTrue(!IsValidId(""), "空は通さない");
		checkTrue(!IsValidId(std::string(65, 'a')), "65 文字は通さない");
		checkTrue(!IsValidId("a/b"), "区切りは通さない");
	}

	// --- 要求の読み取り -------------------------------------------------------
	{
		Request request;
		std::string error;
		checkTrue(ParseRequest(R"({"id":"1-a","tool":"vw_ping","args":{"x":1}})", request, error),
				  "要求を読める");
		checkEq(request.tool, "vw_ping", "tool");
		checkTrue(request.args.isObject(), "args はオブジェクト");
		checkTrue(ParseRequest(R"({"id":"1-a","tool":"vw_ping"})", request, error),
				  "args が無くても読める");
		checkTrue(request.args.isObject(), "args が無ければ空のオブジェクト");
		checkTrue(!ParseRequest(R"({"id":"../x","tool":"vw_ping"})", request, error),
				  "id が不正なら失敗");
		checkTrue(!ParseRequest(R"({"id":"1-a"})", request, error), "tool が無ければ失敗");
	}

	// --- 応答の形（Python 側の call が読む鍵）--------------------------------
	{
		Response ok;
		ok.id = "1-a";
		ok.ok = true;
		checkEq(DumpResponse(ok), R"({"id":"1-a","ok":true,"result":{}})", "成功の応答");
		checkEq(DumpResponse(Failure("1-a", "だめ")), R"({"id":"1-a","ok":false,"error":"だめ"})",
				"失敗の応答");
	}

	// --- スプール: 用意・取得・応答・掃除 ------------------------------------
	{
		const std::filesystem::path root = freshRoot();
		const Spool spool(SpoolDir(root.string() + "/", "VwSdkProbes"));
		checkEq(spool.dir(), root.string() + "/VwSdkProbes-mcp",
				"置き場所の綴り（末尾の / は畳む）");

		std::string error;
		checkTrue(spool.prepare(error), "スプールを用意できる");
#if !defined(_WIN32)
		struct stat info
		{
		};
		checkTrue(::stat(spool.dir().c_str(), &info) == 0 && (info.st_mode & 0777) == 0700,
				  "スプールは 0700 で作る");
#endif
		const std::filesystem::path dir(spool.dir());

		// 送った順（＝名前の昇順）に取り出し、壊れたものは broken へ。
		writeFile(dir / "000000000002-b.req.json", R"({"id":"000000000002-b","tool":"two"})");
		writeFile(dir / "000000000001-a.req.json", R"({"id":"000000000001-a","tool":"one"})");
		writeFile(dir / "000000000003-c.req.json", "{壊れている");
		writeFile(dir / "000000000004-d.req.json", R"({"id":"other","tool":"x"})");
		writeFile(dir / "000000000005-e.req.json.tmp", "書きかけ");

		std::vector<std::string> broken;
		const std::vector<Request> got = spool.poll(broken);
		checkEq(got.size(), size_t(2), "読める要求は 2 件");
		if (got.size() == 2)
		{
			checkEq(got[0].tool, "one", "名前の昇順で 1 件目");
			checkEq(got[1].tool, "two", "名前の昇順で 2 件目");
		}
		checkEq(broken.size(), size_t(2), "壊れた要求と id の食い違いは broken へ");
		checkTrue(!std::filesystem::exists(dir / "000000000003-c.req.json"),
				  "壊れた要求も消す（再取得しない）");
		checkTrue(std::filesystem::exists(dir / "000000000005-e.req.json.tmp"),
				  "書きかけ（.tmp）は取得しない");

		Response response;
		response.id = "000000000001-a";
		response.ok = true;
		checkTrue(spool.reply(response, error), "応答を書ける");
		checkEq(readFile(dir / "000000000001-a.res.json"),
				R"({"id":"000000000001-a","ok":true,"result":{}})", "応答の中身");
		checkTrue(!spool.reply(Failure("../x", "e"), error), "不正な id へは書かない");

		// 生存の印。
		Json status = Json::object();
		status.set("beat", Json::integer(1000));
		checkTrue(spool.writeStatus(status, error), "生存の印を書ける");
		checkTrue(spool.statusIsLive(1010, kStatusStaleSeconds), "15 秒以内なら生きている");
		checkTrue(!spool.statusIsLive(1016, kStatusStaleSeconds), "16 秒経てば止まっている");

		// 掃除は要求・応答・書きかけだけを消し、生存の印は残す。
		checkEq(spool.sweep(), size_t(2), "応答と書きかけを掃除する");
		checkTrue(std::filesystem::exists(dir / kStatusFile), "生存の印は掃除しない");

		std::error_code ec;
		std::filesystem::remove_all(root, ec);
	}

#if !defined(_WIN32)
	// --- 他からも書けるスプールは使わない ------------------------------------
	{
		const std::filesystem::path root = freshRoot();
		const Spool spool(SpoolDir(root.string(), "VwSdkProbes"));
		std::string error;
		checkTrue(spool.prepare(error), "一度用意する");
		::chmod(spool.dir().c_str(), 0777);
		checkTrue(!spool.prepare(error), "誰でも書けるなら使わない");
		checkTrue(error.find("他からも書き込める") != std::string::npos, "理由を言う");
		std::error_code ec;
		std::filesystem::remove_all(root, ec);
	}
#endif

	// --- BridgeParse: 末尾を残して詰める --------------------------------------
	{
		checkEq(KeepTail("abc", 10), "abc", "上限以内ならそのまま");
		const std::string kept = KeepTail("1\n2\n3\n4\n", 4);
		checkEq(kept, "3\n4\n", "古いほうを行の切れ目で削る");
		// UTF-8 の途中で切らない（行が長すぎて切れ目が無いとき）。
		const std::string jp = "あいう"; // 9 バイト
		checkEq(KeepTail(jp, 4), "う", "文字の途中から始めない");
	}

	// --- BridgeParse: プローブの引き当て --------------------------------------
	{
		const std::vector<ProbeKey> keys = {
			{"layer-order", "main"}, {"layer-order", "pr12"}, {"tag-formula", "pr15"}};
		std::string error;
		checkEq(FindProbe(keys, "tag-formula", "", error), size_t(2),
				"1 件しか無ければ群は要らない");
		checkEq(FindProbe(keys, "layer-order", "pr12", error), size_t(1), "群で絞れる");
		checkEq(FindProbe(keys, "layer-order", "", error), kNoProbe, "曖昧なら選ばない");
		checkTrue(error.find("main") != std::string::npos &&
					  error.find("pr12") != std::string::npos,
				  "曖昧なときは候補の群を並べる");
		checkEq(FindProbe(keys, "nothing", "", error), kNoProbe, "無ければ選ばない");
		checkEq(FindProbe(keys, "layer-order", "pr99", error), kNoProbe, "群が違えば選ばない");
	}

	if (gFailures > 0)
	{
		std::printf("\n%d 件失敗しました。\n", gFailures);
		return EXIT_FAILURE;
	}
	std::printf("BridgeSpool: すべて通りました。\n");
	return EXIT_SUCCESS;
}
