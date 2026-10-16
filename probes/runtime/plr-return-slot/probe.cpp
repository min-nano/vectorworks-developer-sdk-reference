//
//	probes/runtime/plr-return-slot/probe.cpp
//
//	[issue #233] 殻のプラグインライブラリ関数の定義に**戻り値の欄**（`fParams` の最後）を
//	足したあとで、
//	  ① 実行時の受け渡し（#217 で確かめたもの）が壊れていないか——3 本を名前で呼ぶ
//	  ② VW が Plug-ins 直下に書き出す `VWPluginLibraryRoutines.h` / `.p` の定義が
//	     実際の署名どおりになったか——`VwSdkProbes_` の行を読み出す
//	を確かめる。② のファイルは**起動時**に書かれるので、殻を入れ替えて起動し直した後に走らせる。
//
//	名前は殻（plugin/src/ProbeLibrary.h）の写し（殻のヘッダは include しない）。
//

#include "Probe.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	constexpr const char* kPlrsEcho = "VwSdkProbes_Echo";
	constexpr const char* kPlrsSum = "VwSdkProbes_Sum";
	constexpr const char* kPlrsOut = "VwSdkProbes_Out";

	using PlrsArgTable = VWFC::PluginSupport::VWPluginLibraryArgTable;

	std::string PlrsUtf8(const TXString& s)
	{
		const char* p = static_cast<const char*>(s);
		return p != nullptr ? std::string(p) : std::string();
	}

	// 型を見てから読む（getter は型が合わないと VWFC_ASSERT を踏む）。
	std::string PlrsDescribe(const PluginLibraryArg& arg)
	{
		std::string out = "argType=" + std::to_string(static_cast<int>(arg.argType));
		switch (arg.argType)
		{
		case kStringArgType:
		case kStringVarArgType:
			out += " \"" + PlrsUtf8(arg.strValue) + "\"";
			break;
		case kLongArgType:
		case kLongVarArgType:
			out += " " + std::to_string(static_cast<long>(arg.longValue));
			break;
		case kBooleanArgType:
		case kBooleanVarArgType:
			out += arg.boolValue ? " true" : " false";
			break;
		default:
			break;
		}
		return out;
	}

	std::filesystem::path PlrsPluginsDir()
	{
#if GS_WIN
		const char* base = std::getenv("APPDATA");
		if (base == nullptr)
			return {};
		return std::filesystem::path(base) / "Nemetschek" / "Vectorworks" / "2026" / "Plug-ins";
#else
		const char* home = std::getenv("HOME");
		if (home == nullptr)
			return {};
		return std::filesystem::path(home) / "Library" / "Application Support" / "Vectorworks" /
			   "2026" / "Plug-ins";
#endif
	}

	void PlrsDumpDefinitions(vwprobe::Report& probe, const char* fileName)
	{
		const std::filesystem::path path = PlrsPluginsDir() / fileName;
		std::ifstream in(path);
		if (!in)
		{
			probe.log(std::string("  ") + fileName + ": 開けない（" + path.string() + "）");
			return;
		}
		std::error_code ec;
		const auto when = std::filesystem::last_write_time(path, ec);
		probe.log(
			std::string("  ") + fileName + "（更新 " +
			(ec ? std::string("?")
				: std::to_string(static_cast<long long>(
					  std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch())
						  .count()))) +
			"）:");
		std::string line;
		int found = 0;
		while (std::getline(in, line))
		{
			if (line.find("VwSdkProbes_") != std::string::npos)
			{
				probe.log("    " + line);
				++found;
			}
		}
		if (found == 0)
			probe.log("    （VwSdkProbes_ の行が無い）");
	}
} // namespace

VW_PROBE("plr-return-slot", "ライブラリ関数の戻り値の欄",
		 "殻の 3 関数を呼び、VW が書き出した定義を読む")
{
	bool allOk = true;

	probe.log("=== ① 実行時の受け渡し ===");
	{
		PlrsArgTable table;
		table.GetArgument(0).SetArgString("i233");
		PluginLibraryArgTable* raw = table;
		const bool ok = gSDK->CallPluginLibrary(kPlrsEcho, raw, 0) != 0;
		const bool good = ok && raw->functionResult.argType == kStringArgType &&
						  PlrsUtf8(raw->functionResult.strValue) == "echo:i233";
		probe.log(std::string("  Echo(\"i233\") -> ") + (ok ? "true" : "false") + " / 結果欄 " +
				  PlrsDescribe(raw->functionResult) + (good ? "  ◯" : "  ✕"));
		allOk = allOk && good;
	}
	{
		PlrsArgTable table;
		table.GetArgument(0).SetArgLong(40);
		table.GetArgument(1).SetArgLong(2);
		PluginLibraryArgTable* raw = table;
		const bool ok = gSDK->CallPluginLibrary(kPlrsSum, raw, 0) != 0;
		const bool good = ok && raw->functionResult.argType == kLongArgType &&
						  raw->functionResult.longValue == 42;
		probe.log(std::string("  Sum(40, 2) -> ") + (ok ? "true" : "false") + " / 結果欄 " +
				  PlrsDescribe(raw->functionResult) + (good ? "  ◯" : "  ✕"));
		allOk = allOk && good;
	}
	{
		PlrsArgTable table;
		table.GetArgument(0).SetArgString("i233");
		PluginLibraryArgTable* raw = table;
		raw->args[1].argType = kStringVarArgType; // VAR の文字列には setter が無い
		table.GetArgument(2).GetArgLongVar() = 0;
		const bool ok = gSDK->CallPluginLibrary(kPlrsOut, raw, 0) != 0;
		const bool good =
			ok && raw->functionResult.argType == kBooleanArgType && raw->functionResult.boolValue &&
			PlrsUtf8(raw->args[1].strValue) == "out:i233" && raw->args[2].longValue == 4;
		probe.log(std::string("  Out(\"i233\", VAR, VAR) -> ") + (ok ? "true" : "false") +
				  " / 結果欄 " + PlrsDescribe(raw->functionResult) + " / args[1] " +
				  PlrsDescribe(raw->args[1]) + " / args[2] " + PlrsDescribe(raw->args[2]) +
				  (good ? "  ◯" : "  ✕"));
		allOk = allOk && good;
	}

	probe.log("");
	probe.log("=== ② VW が書き出した定義（起動時に書かれる） ===");
	probe.log("  Plug-ins: " + PlrsPluginsDir().string());
	PlrsDumpDefinitions(probe, "VWPluginLibraryRoutines.h");
	PlrsDumpDefinitions(probe, "VWPluginLibraryRoutines.p");
	probe.log(
		"  期待（.p）: VwSdkProbes_Echo(input: STRING) : STRING / "
		"VwSdkProbes_Sum(a: LONGINT; b: LONGINT) : LONGINT / "
		"VwSdkProbes_Out(input: STRING; VAR outText: STRING; VAR outCount: LONGINT) : BOOLEAN");

	if (!allOk)
		probe.fail("実行時の受け渡しが期待と違った");
}
