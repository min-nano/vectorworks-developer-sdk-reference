//
//	ProbeLibrary.cpp
//
//	「呼ばれる側」の実装。設計の理由は ProbeLibrary.h に書いてある。
//

#include "PluginPrefix.h"

#include "BuildConfig.h"
#include "ProbeLibrary.h"

#include <cstring>
#include <string>

namespace vwprobe
{
	namespace
	{
		// TXString → std::string（UTF-8）。`operator const char*` が UTF-8 を返す。
		std::string ToUtf8(const TXString& s)
		{
			const char* p = static_cast<const char*>(s);
			return p != nullptr ? std::string(p) : std::string();
		}

		// 登録する関数の表。**最後の要素は `fName == nullptr`**（終端）。
		// fParams は 11 個の固定長なので、使う分だけ書けば残りは {} で埋まる
		// （= fName が nullptr になり、引数の数の数え方と揃う）。
		const SFunctionDef kFunctions[] = {
			{kLibEchoName,
			 "VwSdkProbes",
			 "引数の文字列に echo: を付けて返す（#217）",
			 1,
			 kVLIBScopeUniversal,
			 true,
			 {{"input", kStringArgType}}},
			{kLibSumName,
			 "VwSdkProbes",
			 "2 つの整数の和を返す（#217）",
			 1,
			 kVLIBScopeUniversal,
			 true,
			 {{"a", kLongArgType}, {"b", kLongArgType}}},
			{kLibOutName,
			 "VwSdkProbes",
			 "VAR 引数へ書き戻して true を返す（#217）",
			 1,
			 kVLIBScopeUniversal,
			 true,
			 {{"input", kStringArgType},
			  {"outText", kStringVarArgType},
			  {"outCount", kLongVarArgType}}},
			{nullptr, nullptr, nullptr, 0, kVLIBScopeUniversal, false, {}},
		};

		// 終端を含まない本数。DispatchRoutine の範囲検査に使う。
		constexpr size_t kFunctionCount = (sizeof(kFunctions) / sizeof(kFunctions[0])) - 1;
	} // namespace

	const SFunctionDef* libraryDef()
	{
		return kFunctions;
	}

	// -----------------------------------------------------------------------
	void CProbeLibraryRoutine::DispatchRoutine(Sint32 routineSelector,
											   VWPluginLibraryArgTable& argTable)
	{
		// **選択子は表の添字**（SDK の BEGIN_LIB_DISPATCH_MAP_Ex と同じ）。範囲の外は
		// 黙って捨てる——ここで表の外を読むと、呼ばれ方が想定と違った瞬間に落ちる。
		if (routineSelector < 0 || static_cast<size_t>(routineSelector) >= kFunctionCount)
			return;

		const char* name = kFunctions[routineSelector].fName;
		if (name == nullptr)
			return;

		if (std::strcmp(name, kLibEchoName) == 0)
			Echo(argTable);
		else if (std::strcmp(name, kLibSumName) == 0)
			Sum(argTable);
		else if (std::strcmp(name, kLibOutName) == 0)
			Out(argTable);
	}

	void CProbeLibraryRoutine::Echo(VWPluginLibraryArgTable& argTable)
	{
		const std::string input = ToUtf8(argTable.GetArgument(0).GetArgString());
		const std::string out = "echo:" + input;
		argTable.GetResult().SetArgString(TXString(out.c_str()));
	}

	void CProbeLibraryRoutine::Sum(VWPluginLibraryArgTable& argTable)
	{
		const Sint32 a = argTable.GetArgument(0).GetArgLong();
		const Sint32 b = argTable.GetArgument(1).GetArgLong();
		argTable.GetResult().SetArgLong(a + b);
	}

	void CProbeLibraryRoutine::Out(VWPluginLibraryArgTable& argTable)
	{
		const std::string input = ToUtf8(argTable.GetArgument(0).GetArgString());
		const std::string out = "out:" + input;

		argTable.GetArgument(1).SetArgString(TXString(out.c_str()));
		argTable.GetArgument(2).GetArgLongVar() = static_cast<Sint32>(input.size());
		argTable.GetResult().SetArgBoolean(true);
	}

	// -----------------------------------------------------------------------
	CProbeLibrary_EventSink::CProbeLibrary_EventSink(IVWUnknown* parent)
		: VWVSFunctions_EventSink(parent)
	{
		// isLocalMemory = true: この受け口の始末と一緒に delete される。
		this->AddRoutine(SDK_NEW CProbeLibraryRoutine, true);
	}

	CProbeLibrary_EventSink::~CProbeLibrary_EventSink() = default;

	// -----------------------------------------------------------------------
	CExtProbeLibrary::CExtProbeLibrary(CallBackPtr cbp)
		: VWExtensionVSFunctions(cbp, vwprobe::libraryDef())
	{
	}

	CExtProbeLibrary::~CExtProbeLibrary() = default;

	Uint32 CExtProbeLibrary::Initialize()
	{
		return VectorWorks::Extension::kExtensionVSFunctionsInitFlag_DontOpenResource;
	}

	void CExtProbeLibrary::InitRoutines() {}
} // namespace vwprobe

// ---------------------------------------------------------------------------
// 拡張機能の一意な ID とユニバーサル名。メニュー拡張（ProbeMenu.cpp）とは**別の
// UUID・別のユニバーサル名**でなければならない（1 モジュールが 2 つの拡張機能を
// 登録する形）。
//
// NOLINT: IMPLEMENT_VWVSFunctionsExtension は SDK のマクロで、展開の中に clang-tidy が
// const を求める `static VWIID iid` がある（マクロ側のコード）。
// NOLINTBEGIN(misc-const-correctness)
// UUID: 6f1a9c84-2d57-4c1b-9f30-7ab5c6e21d48
IMPLEMENT_VWVSFunctionsExtension(
	/*Extension class*/ vwprobe::CExtProbeLibrary,
	/*Event sink*/ vwprobe::CProbeLibrary_EventSink,
	/*Universal name*/ PLUGIN_LIBRARY_UNIVERSAL_NAME,
	/*Version*/ 1,
	/*UUID*/ 0x6f1a9c84, 0x2d57, 0x4c1b, 0x9f, 0x30, 0x7a, 0xb5, 0xc6, 0xe2, 0x1d, 0x48);
// NOLINTEND(misc-const-correctness)
