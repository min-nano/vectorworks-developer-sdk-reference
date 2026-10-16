//
//	probes/runtime/doc-dirty-staged/cocoa_windows.cpp
//
//	[issue #240] macOS の窓の「編集済み」（NSWindow の isDocumentEdited。閉じるボタンの点の元）を
//	Objective-C ランタイム経由で読む。outSaved とは別の口で「変更ありになったか」を確かめるため。
//	Windows では何も読まない。
//

#include "Probe.h"

#include <string>

#if GS_MAC
#	include <objc/message.h>
#	include <objc/runtime.h>
#endif

std::string DirtyStagedCocoaWindows()
{
#if GS_MAC
	using IdFn = id (*)(id, SEL);
	using IdAtFn = id (*)(id, SEL, unsigned long);
	using CountFn = unsigned long (*)(id, SEL);
	using BoolFn = signed char (*)(id, SEL);
	using CStrFn = const char* (*)(id, SEL);

	const IdFn sendId = reinterpret_cast<IdFn>(objc_msgSend);
	const IdAtFn sendIdAt = reinterpret_cast<IdAtFn>(objc_msgSend);
	const CountFn sendCount = reinterpret_cast<CountFn>(objc_msgSend);
	const BoolFn sendBool = reinterpret_cast<BoolFn>(objc_msgSend);
	const CStrFn sendCStr = reinterpret_cast<CStrFn>(objc_msgSend);

	const id app = sendId(reinterpret_cast<id>(objc_getClass("NSApplication")),
						  sel_registerName("sharedApplication"));
	if (app == nullptr)
		return "(NSApp なし)";
	const id windows = sendId(app, sel_registerName("windows"));
	if (windows == nullptr)
		return "(windows なし)";

	std::string out;
	const unsigned long count = sendCount(windows, sel_registerName("count"));
	for (unsigned long i = 0; i < count; ++i)
	{
		const id window = sendIdAt(windows, sel_registerName("objectAtIndex:"), i);
		if (window == nullptr || !sendBool(window, sel_registerName("isVisible")))
			continue;
		const id title = sendId(window, sel_registerName("title"));
		const id file = sendId(window, sel_registerName("representedFilename"));
		const char* titleText = title ? sendCStr(title, sel_registerName("UTF8String")) : nullptr;
		const char* fileText = file ? sendCStr(file, sel_registerName("UTF8String")) : nullptr;
		const bool edited = sendBool(window, sel_registerName("isDocumentEdited")) != 0;
		if (!out.empty())
			out += " | ";
		out += std::string("「") + (titleText ? titleText : "") +
			   "」 edited=" + (edited ? "yes" : "no");
		if (fileText != nullptr && fileText[0] != '\0')
			out += std::string(" file=") + fileText;
	}
	return out.empty() ? "(見える窓なし)" : out;
#else
	return "(mac のみ)";
#endif
}
