//
//	probes/runtime/doc-dirty-staged/cocoa_windows.cpp
//
//	[issue #240] macOS 側の観測（Objective-C ランタイム経由。Windows では何もしない）。
//
//	  * DirtyStagedCocoaWindows … 見えている窓の「編集済み」（NSWindow の isDocumentEdited）
//	  * DirtyStagedWatchStart / Stop … 保存の確認が出たかを機械で記録する。
//	      kCFRunLoopCommonModes のタイマー（確認の最中も刻む。Findings「Timers and
//	      Notifications」4 の 9）から NSApp.modalWindow と各窓の attachedSheet を見て、
//	      モーダルが出たら題を書き留め、abortModal で閉じる。
//

#include "Probe.h"

#include <string>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#	include <objc/message.h>
#	include <objc/runtime.h>
#endif

#if GS_MAC
namespace
{
	using IdFn = id (*)(id, SEL);
	using IdAtFn = id (*)(id, SEL, unsigned long);
	using CountFn = unsigned long (*)(id, SEL);
	using BoolFn = signed char (*)(id, SEL);
	using CStrFn = const char* (*)(id, SEL);
	using VoidFn = void (*)(id, SEL);
	using BoolIdFn = signed char (*)(id, SEL, id);

	id DirtyStagedSend(id target, const char* sel)
	{
		return target ? reinterpret_cast<IdFn>(objc_msgSend)(target, sel_registerName(sel))
					  : nullptr;
	}

	std::string DirtyStagedText(id nsString)
	{
		if (nsString == nullptr)
			return "";
		const char* text =
			reinterpret_cast<CStrFn>(objc_msgSend)(nsString, sel_registerName("UTF8String"));
		return text ? text : "";
	}

	bool DirtyStagedBool(id target, const char* sel)
	{
		return target && reinterpret_cast<BoolFn>(objc_msgSend)(target, sel_registerName(sel)) != 0;
	}

	id DirtyStagedApp()
	{
		return DirtyStagedSend(reinterpret_cast<id>(objc_getClass("NSApplication")),
							   "sharedApplication");
	}

	// NSAlert の窓なら、中の文言（NSTextField の stringValue）を拾って題に足す。
	void DirtyStagedCollectTexts(id view, std::string& out, int depth)
	{
		if (view == nullptr || depth > 6)
			return;
		if (reinterpret_cast<BoolIdFn>(objc_msgSend)(
				view, sel_registerName("isKindOfClass:"),
				reinterpret_cast<id>(objc_getClass("NSTextField"))) != 0)
		{
			const std::string text = DirtyStagedText(DirtyStagedSend(view, "stringValue"));
			if (!text.empty())
				out += (out.empty() ? "" : " / ") + text;
		}
		if (reinterpret_cast<BoolIdFn>(objc_msgSend)(
				view, sel_registerName("isKindOfClass:"),
				reinterpret_cast<id>(objc_getClass("NSButton"))) != 0)
		{
			const std::string text = DirtyStagedText(DirtyStagedSend(view, "title"));
			if (!text.empty())
				out += (out.empty() ? "" : " / ") + std::string("[") + text + "]";
		}
		const id subviews = DirtyStagedSend(view, "subviews");
		const unsigned long n =
			subviews ? reinterpret_cast<CountFn>(objc_msgSend)(subviews, sel_registerName("count"))
					 : 0;
		for (unsigned long i = 0; i < n; ++i)
			DirtyStagedCollectTexts(reinterpret_cast<IdAtFn>(objc_msgSend)(
										subviews, sel_registerName("objectAtIndex:"), i),
									out, depth + 1);
	}

	std::string DirtyStagedDescribe(id window)
	{
		std::string out = "「" + DirtyStagedText(DirtyStagedSend(window, "title")) + "」";
		std::string texts;
		DirtyStagedCollectTexts(DirtyStagedSend(window, "contentView"), texts, 0);
		if (!texts.empty())
			out += " " + texts;
		return out;
	}

	struct DirtyStagedWatch
	{
		CFRunLoopTimerRef timer = nullptr;
		CFAbsoluteTime started = 0;
		int ticks = 0;
		int modalTicks = 0;
		bool aborted = false;
		std::string seen;
	};

	DirtyStagedWatch& DirtyStagedWatchState()
	{
		static DirtyStagedWatch watch;
		return watch;
	}

	void DirtyStagedOnTick(CFRunLoopTimerRef, void*)
	{
		DirtyStagedWatch& watch = DirtyStagedWatchState();
		++watch.ticks;
		const id app = DirtyStagedApp();
		const id modal = DirtyStagedSend(app, "modalWindow");
		std::string sheets;
		const id windows = DirtyStagedSend(app, "windows");
		const unsigned long n =
			windows ? reinterpret_cast<CountFn>(objc_msgSend)(windows, sel_registerName("count"))
					: 0;
		for (unsigned long i = 0; i < n; ++i)
		{
			const id window = reinterpret_cast<IdAtFn>(objc_msgSend)(
				windows, sel_registerName("objectAtIndex:"), i);
			const id sheet = DirtyStagedSend(window, "attachedSheet");
			if (sheet != nullptr)
				sheets += (sheets.empty() ? "" : " | ") + DirtyStagedDescribe(sheet);
		}
		if (modal == nullptr && sheets.empty())
			return;
		++watch.modalTicks;
		if (watch.seen.empty())
		{
			const int ms = static_cast<int>((CFAbsoluteTimeGetCurrent() - watch.started) * 1000);
			watch.seen = "t=" + std::to_string(ms) + "ms";
			if (modal != nullptr)
				watch.seen += " modal=" + DirtyStagedDescribe(modal);
			if (!sheets.empty())
				watch.seen += " sheet=" + sheets;
		}
		// 1 回目は見るだけ（描かれる前かもしれない）。2 回目の刻みで閉じる。
		if (modal != nullptr && watch.modalTicks >= 2 && !watch.aborted)
		{
			watch.aborted = true;
			reinterpret_cast<VoidFn>(objc_msgSend)(app, sel_registerName("abortModal"));
		}
	}
} // namespace
#endif

std::string DirtyStagedCocoaWindows()
{
#if GS_MAC
	const id windows = DirtyStagedSend(DirtyStagedApp(), "windows");
	if (windows == nullptr)
		return "(windows なし)";
	std::string out;
	const unsigned long count =
		reinterpret_cast<CountFn>(objc_msgSend)(windows, sel_registerName("count"));
	for (unsigned long i = 0; i < count; ++i)
	{
		const id window =
			reinterpret_cast<IdAtFn>(objc_msgSend)(windows, sel_registerName("objectAtIndex:"), i);
		if (!DirtyStagedBool(window, "isVisible"))
			continue;
		if (!out.empty())
			out += " | ";
		out += "「" + DirtyStagedText(DirtyStagedSend(window, "title")) +
			   "」 edited=" + (DirtyStagedBool(window, "isDocumentEdited") ? "yes" : "no");
	}
	return out.empty() ? "(見える窓なし)" : out;
#else
	return "(mac のみ)";
#endif
}

void DirtyStagedWatchStart()
{
#if GS_MAC
	DirtyStagedWatch& watch = DirtyStagedWatchState();
	watch = DirtyStagedWatch();
	watch.started = CFAbsoluteTimeGetCurrent();
	watch.timer = CFRunLoopTimerCreate(kCFAllocatorDefault, watch.started + 0.05, 0.1, 0, 0,
									   &DirtyStagedOnTick, nullptr);
	if (watch.timer != nullptr)
		CFRunLoopAddTimer(CFRunLoopGetMain(), watch.timer, kCFRunLoopCommonModes);
#endif
}

std::string DirtyStagedWatchStop()
{
#if GS_MAC
	DirtyStagedWatch& watch = DirtyStagedWatchState();
	if (watch.timer != nullptr)
	{
		CFRunLoopTimerInvalidate(watch.timer);
		CFRelease(watch.timer);
		watch.timer = nullptr;
	}
	std::string out = "刻み " + std::to_string(watch.ticks) + " 回・モーダルを見た刻み " +
					  std::to_string(watch.modalTicks) + " 回";
	out += watch.seen.empty() ? "・確認は出なかった" : "・確認が出た: " + watch.seen;
	if (watch.aborted)
		out += "・abortModal で閉じた";
	return out;
#else
	return "(mac のみ)";
#endif
}
