// probes/runtime/dialog-external/helper-mac.swift
//
// [issue #302] CLI の代わりに「別のプロセス」から Vectorworks のダイアログを見つけ、読み、押す。
// プラグインには入らない（.cpp ではないのでビルドされない）。手元で次のように作る。
//
//   swiftc -O -o vwdlg probes/runtime/dialog-external/helper-mac.swift
//
//   vwdlg trust                       許可の答えと、誰の許可として数えられるか
//   vwdlg cg                          CGWindowList の窓（AX の許可なし）
//   vwdlg list [--depth N]            AX で窓と中身を読む
//   vwdlg press --title T | --default | --cancel [--window N]
//   vwdlg click --title T             CGEvent の本物のクリック（人の操作の代わり）
//   vwdlg watch --press T [--settle MS] [--timeout S] [--count N] [--marker P]
//                                     新しいダイアログを待って読み、押し、閉じるまで測る
//   vwdlg menu <メニュー> <項目>      メニュー項目を AXPress する（利用者の操作の代わり）
//   vwdlg front                       前面のアプリ
//   vwdlg ping [--timeout S]          AX の往復を測る（VW が忙しいときの答え）
//
import AppKit
import ApplicationServices
import Foundation

func now() -> String {
    let f = DateFormatter()
    f.dateFormat = "HH:mm:ss.SSS"
    return f.string(from: Date())
}

func say(_ s: String) {
    print("[\(now())] \(s)")
    fflush(stdout)
}

func axName(_ e: AXError) -> String {
    switch e {
    case .success: return "success"
    case .failure: return "failure(-25200)"
    case .illegalArgument: return "illegalArgument(-25201)"
    case .invalidUIElement: return "invalidUIElement(-25202)"
    case .invalidUIElementObserver: return "invalidUIElementObserver(-25203)"
    case .cannotComplete: return "cannotComplete(-25204)"
    case .attributeUnsupported: return "attributeUnsupported(-25205)"
    case .actionUnsupported: return "actionUnsupported(-25206)"
    case .notificationUnsupported: return "notificationUnsupported(-25207)"
    case .notImplemented: return "notImplemented(-25208)"
    case .notificationAlreadyRegistered: return "notificationAlreadyRegistered(-25209)"
    case .notificationNotRegistered: return "notificationNotRegistered(-25210)"
    case .apiDisabled: return "apiDisabled(-25211)"
    case .noValue: return "noValue(-25212)"
    case .parameterizedAttributeUnsupported: return "parameterizedAttributeUnsupported(-25213)"
    case .notEnoughPrecision: return "notEnoughPrecision(-25214)"
    @unknown default: return "unknown(\(e.rawValue))"
    }
}

func vwApp() -> NSRunningApplication? {
    NSWorkspace.shared.runningApplications.first {
        ($0.localizedName ?? "").hasPrefix("Vectorworks 20")
    }
}

func requireVW() -> NSRunningApplication {
    guard let app = vwApp() else {
        say("Vectorworks が居ない")
        exit(3)
    }
    return app
}

func attr(_ e: AXUIElement, _ name: String) -> (AnyObject?, AXError) {
    var v: AnyObject?
    let r = AXUIElementCopyAttributeValue(e, name as CFString, &v)
    return (v, r)
}

func str(_ e: AXUIElement, _ name: String) -> String? {
    let (v, _) = attr(e, name)
    if let s = v as? String { return s }
    if let n = v as? NSNumber { return n.stringValue }
    return nil
}

func bool(_ e: AXUIElement, _ name: String) -> Bool? {
    (attr(e, name).0 as? NSNumber)?.boolValue
}

func elem(_ e: AXUIElement, _ name: String) -> AXUIElement? {
    guard let v = attr(e, name).0 else { return nil }
    guard CFGetTypeID(v) == AXUIElementGetTypeID() else { return nil }
    return (v as! AXUIElement)
}

func elems(_ e: AXUIElement, _ name: String) -> [AXUIElement] {
    (attr(e, name).0 as? [AXUIElement]) ?? []
}

func point(_ e: AXUIElement, _ name: String) -> CGPoint? {
    guard let v = attr(e, name).0, CFGetTypeID(v) == AXValueGetTypeID() else { return nil }
    var p = CGPoint.zero
    return AXValueGetValue(v as! AXValue, .cgPoint, &p) ? p : nil
}

func size(_ e: AXUIElement, _ name: String) -> CGSize? {
    guard let v = attr(e, name).0, CFGetTypeID(v) == AXValueGetTypeID() else { return nil }
    var p = CGSize.zero
    return AXValueGetValue(v as! AXValue, .cgSize, &p) ? p : nil
}

func actions(_ e: AXUIElement) -> [String] {
    var names: CFArray?
    guard AXUIElementCopyActionNames(e, &names) == .success else { return [] }
    return (names as? [String]) ?? []
}

func describe(_ e: AXUIElement) -> String {
    var parts: [String] = [str(e, "AXRole") ?? "?"]
    if let s = str(e, "AXSubrole") { parts.append("sub=\(s)") }
    for key in ["AXTitle", "AXValue", "AXDescription", "AXIdentifier", "AXHelp"] {
        if let s = str(e, key), !s.isEmpty { parts.append("\(key.dropFirst(2))=\"\(s)\"") }
    }
    if let b = bool(e, "AXEnabled") { parts.append("enabled=\(b)") }
    if let p = point(e, "AXPosition"), let z = size(e, "AXSize") {
        parts.append("rect=\(Int(p.x)),\(Int(p.y)),\(Int(z.width))x\(Int(z.height))")
    }
    let a = actions(e)
    if !a.isEmpty { parts.append("actions=\(a.joined(separator: ","))") }
    return parts.joined(separator: " ")
}

func dumpTree(_ e: AXUIElement, _ indent: String, _ depth: Int) {
    say(indent + describe(e))
    if depth <= 0 { return }
    for c in elems(e, "AXChildren") { dumpTree(c, indent + "  ", depth - 1) }
}

func windowLine(_ w: AXUIElement) -> String {
    var parts: [String] = []
    parts.append("sub=\(str(w, "AXSubrole") ?? "-")")
    parts.append("role=\(str(w, "AXRole") ?? "-")")
    parts.append("title=\"\(str(w, "AXTitle") ?? "")\"")
    for k in ["AXModal", "AXMain", "AXFocused", "AXMinimized"] {
        parts.append("\(k.dropFirst(2))=\(bool(w, k).map { "\($0)" } ?? "-")")
    }
    parts.append("id=\(str(w, "AXIdentifier") ?? "-")")
    parts.append("doc=\(str(w, "AXDocument") ?? "-")")
    if let p = point(w, "AXPosition"), let z = size(w, "AXSize") {
        parts.append("rect=\(Int(p.x)),\(Int(p.y)),\(Int(z.width))x\(Int(z.height))")
    }
    if let d = elem(w, "AXDefaultButton") { parts.append("default=\"\(str(d, "AXTitle") ?? "")\"") }
    if let c = elem(w, "AXCancelButton") { parts.append("cancel=\"\(str(c, "AXTitle") ?? "")\"") }
    parts.append("sheets=\(elems(w, "AXSheets").count)")
    return parts.joined(separator: " ")
}

/// ダイアログか。**AXModal だけで見る**——パレットも AXSubrole=AXDialog（AXModal=false）を名乗る。
func isDialog(_ w: AXUIElement) -> Bool {
    bool(w, "AXModal") == true
}

func allWindows(_ app: AXUIElement) -> [AXUIElement] {
    var out = elems(app, "AXWindows")
    for w in elems(app, "AXWindows") { out += elems(w, "AXSheets") }
    return out
}

func walk(_ e: AXUIElement, _ depth: Int, _ visit: (AXUIElement) -> Void) {
    visit(e)
    if depth <= 0 { return }
    for c in elems(e, "AXChildren") { walk(c, depth - 1, visit) }
}

func buttons(_ w: AXUIElement) -> [AXUIElement] {
    var out: [AXUIElement] = []
    walk(w, 8) { e in
        if str(e, "AXRole") == "AXButton" { out.append(e) }
    }
    return out
}

func textsOf(_ w: AXUIElement) -> [String] {
    var out: [String] = []
    walk(w, 8) { e in
        if str(e, "AXRole") == "AXStaticText", let v = str(e, "AXValue"), !v.isEmpty {
            out.append(v)
        }
    }
    return out
}

func argValue(_ args: [String], _ name: String) -> String? {
    guard let i = args.firstIndex(of: name), i + 1 < args.count else { return nil }
    return args[i + 1]
}

// ---------------------------------------------------------------------------

func cmdTrust() {
    let pid = getpid()
    say("pid=\(pid) ppid=\(getppid()) exe=\(CommandLine.arguments[0])")
    typealias Resp = @convention(c) (pid_t) -> pid_t
    if let h = dlopen(nil, RTLD_NOW), let s = dlsym(h, "responsibility_get_pid_responsible_for_pid") {
        let r = unsafeBitCast(s, to: Resp.self)(pid)
        var buf = [CChar](repeating: 0, count: 4096)
        proc_pidpath(r, &buf, 4096)
        say("responsible pid=\(r) path=\(String(cString: buf))")
    }
    var cur = pid
    var chain: [String] = []
    while cur > 1 {
        var buf = [CChar](repeating: 0, count: 4096)
        proc_pidpath(cur, &buf, 4096)
        chain.append("\(cur):\(URL(fileURLWithPath: String(cString: buf)).lastPathComponent)")
        var info = kinfo_proc()
        var len = MemoryLayout<kinfo_proc>.stride
        var mib: [Int32] = [CTL_KERN, KERN_PROC, KERN_PROC_PID, cur]
        if sysctl(&mib, 4, &info, &len, nil, 0) != 0 { break }
        cur = info.kp_eproc.e_ppid
    }
    say("parents=" + chain.joined(separator: " < "))
    let t0 = Date()
    let trusted = AXIsProcessTrusted()
    say("AXIsProcessTrusted=\(trusted) (\(Int(Date().timeIntervalSince(t0) * 1000)) ms)")
    let opts = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: false] as CFDictionary
    say("AXIsProcessTrustedWithOptions(prompt=false)=\(AXIsProcessTrustedWithOptions(opts))")
    say("CGPreflightScreenCaptureAccess=\(CGPreflightScreenCaptureAccess())")
    if let app = vwApp() {
        let ax = AXUIElementCreateApplication(app.processIdentifier)
        let (v, r) = attr(ax, "AXWindows")
        say("VW pid=\(app.processIdentifier) AXWindows: err=\(axName(r)) count=\((v as? [AnyObject])?.count ?? -1)")
        let (v2, r2) = attr(ax, "AXRole")
        say("VW AXRole: err=\(axName(r2)) value=\(String(describing: v2))")
    } else {
        say("VW は居ない")
    }
}

func cmdCg() {
    let app = requireVW()
    say("VW pid=\(app.processIdentifier) active=\(app.isActive)")
    guard let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]]
    else {
        say("CGWindowListCopyWindowInfo が nil")
        return
    }
    for w in list where (w[kCGWindowOwnerPID as String] as? pid_t) == app.processIdentifier {
        let b = w[kCGWindowBounds as String] as? [String: CGFloat] ?? [:]
        let on = w[kCGWindowIsOnscreen as String] as? Bool ?? false
        if !on { continue }
        say(
            "win=\(w[kCGWindowNumber as String] ?? "?") layer=\(w[kCGWindowLayer as String] ?? "?")"
                + " onscreen=\(on) alpha=\(w[kCGWindowAlpha as String] ?? "?")"
                + " rect=\(Int(b["X"] ?? 0)),\(Int(b["Y"] ?? 0)),\(Int(b["Width"] ?? 0))x\(Int(b["Height"] ?? 0))"
                + " name=\(w[kCGWindowName as String] ?? "(読めない)")")
    }
}

func cmdList(_ args: [String]) {
    let app = requireVW()
    let depth = Int(argValue(args, "--depth") ?? "6") ?? 6
    let ax = AXUIElementCreateApplication(app.processIdentifier)
    say("VW pid=\(app.processIdentifier) active=\(app.isActive)")
    let (wv, wr) = attr(ax, "AXWindows")
    say("AXWindows: err=\(axName(wr)) count=\((wv as? [AnyObject])?.count ?? -1)")
    say("AXFrontmost=\(bool(ax, "AXFrontmost").map { "\($0)" } ?? "-")")
    if let f = elem(ax, "AXFocusedWindow") { say("AXFocusedWindow: \(windowLine(f))") }
    else { say("AXFocusedWindow: err=\(axName(attr(ax, "AXFocusedWindow").1))") }
    if let m = elem(ax, "AXMainWindow") { say("AXMainWindow: \(windowLine(m))") }
    else { say("AXMainWindow: err=\(axName(attr(ax, "AXMainWindow").1))") }
    let wins = elems(ax, "AXWindows")
    for (i, w) in wins.enumerated() {
        say("window[\(i)] dialog=\(isDialog(w)) \(windowLine(w))")
        for (j, s) in elems(w, "AXSheets").enumerated() {
            say("window[\(i)].sheet[\(j)] \(windowLine(s))")
            if depth > 0 { dumpTree(s, "      ", depth) }
        }
        if depth > 0 && isDialog(w) { dumpTree(w, "    ", depth) }
    }
}

func findButton(_ w: AXUIElement, _ args: [String]) -> AXUIElement? {
    if args.contains("--default") { return elem(w, "AXDefaultButton") }
    if args.contains("--cancel") { return elem(w, "AXCancelButton") }
    if args.contains("--close-button") { return elem(w, "AXCloseButton") }
    guard let title = argValue(args, "--title") else { return nil }
    return buttons(w).first { (str($0, "AXTitle") ?? "") == title }
}

func dialogsOf(_ ax: AXUIElement) -> [AXUIElement] {
    allWindows(ax).filter { isDialog($0) }
}

func cmdPress(_ args: [String]) {
    let app = requireVW()
    let ax = AXUIElementCreateApplication(app.processIdentifier)
    let list = dialogsOf(ax)
    let index = Int(argValue(args, "--window") ?? "0") ?? 0
    guard index < list.count || list.isEmpty else {
        say("ダイアログは \(list.count) 枚")
        exit(4)
    }
    guard let w = list.isEmpty ? nil : list[index] else {
        say("ダイアログが無い（窓 \(allWindows(ax).count) 枚）")
        exit(4)
    }
    say("相手: \(windowLine(w))")
    guard let b = findButton(w, args) else {
        say("ボタンが無い: \(buttons(w).map { str($0, "AXTitle") ?? "?" })")
        exit(5)
    }
    pressAndWait(ax, w, b)
}

func pressAndWait(_ ax: AXUIElement, _ w: AXUIElement, _ b: AXUIElement) {
    say("押す: \(describe(b))")
    let t0 = Date()
    let r = AXUIElementPerformAction(b, "AXPress" as CFString)
    say("AXPress: err=\(axName(r)) \(Int(Date().timeIntervalSince(t0) * 1000)) ms")
    let t1 = Date()
    let limit = 60.0
    var busy = 0
    while Date().timeIntervalSince(t1) < limit {
        // 閉じた窓への問い合わせは invalidUIElement を返す。
        let (_, e) = attr(w, "AXRole")
        // cannotComplete は「VW が忙しくて答えない」で、閉じたことを意味しない。
        if e == .cannotComplete {
            busy += 1
            say("AX が答えない（\(axName(e))）\(busy) 回目 押してから \(Int(Date().timeIntervalSince(t0) * 1000)) ms")
            continue
        }
        if e != .success {
            say("閉じた: 窓の AXRole err=\(axName(e)) 押してから \(Int(Date().timeIntervalSince(t0) * 1000)) ms 答えなかった回数=\(busy)")
            return
        }
        usleep(20_000)
    }
    say("押してから \(limit) 秒たっても閉じない 答えなかった回数=\(busy)")
}

func cmdClick(_ args: [String]) {
    let app = requireVW()
    let ax = AXUIElementCreateApplication(app.processIdentifier)
    guard let w = dialogsOf(ax).first, let b = findButton(w, args),
        let p = point(b, "AXPosition"), let z = size(b, "AXSize")
    else {
        say("ダイアログかボタンが無い")
        exit(4)
    }
    let c = CGPoint(x: p.x + z.width / 2, y: p.y + z.height / 2)
    say("本物のクリック: \(describe(b)) at \(c)")
    app.activate()
    usleep(300_000)
    let src = CGEventSource(stateID: .hidSystemState)
    CGEvent(mouseEventSource: src, mouseType: .mouseMoved, mouseCursorPosition: c, mouseButton: .left)?
        .post(tap: .cghidEventTap)
    usleep(100_000)
    CGEvent(mouseEventSource: src, mouseType: .leftMouseDown, mouseCursorPosition: c, mouseButton: .left)?
        .post(tap: .cghidEventTap)
    usleep(60_000)
    CGEvent(mouseEventSource: src, mouseType: .leftMouseUp, mouseCursorPosition: c, mouseButton: .left)?
        .post(tap: .cghidEventTap)
    let t0 = Date()
    while Date().timeIntervalSince(t0) < 10 {
        let e = attr(w, "AXRole").1
        if e != .success && e != .cannotComplete {
            say("閉じた: err=\(axName(e)) \(Int(Date().timeIntervalSince(t0) * 1000)) ms")
            return
        }
        usleep(20_000)
    }
    say("クリックから 10 秒たっても閉じない")
}

func logPath() -> String {
    NSHomeDirectory() + "/Library/Application Support/Vectorworks/2026/VW User Log.txt"
}

func logSize() -> UInt64 {
    ((try? FileManager.default.attributesOfItem(atPath: logPath()))?[.size] as? NSNumber)?.uint64Value ?? 0
}

/// ログの from より後に増えた行のうち、ダイアログの行を、シリアルを落として出す。
func sayLogLines(from: UInt64) {
    guard let h = FileHandle(forReadingAtPath: logPath()) else { return }
    defer { try? h.close() }
    let end = logSize()
    if end < from {
        say("ログが縮んだ（\(from) → \(end)。送信で切られた）")
        return
    }
    try? h.seek(toOffset: from)
    guard let d = try? h.readToEnd(), let s = String(data: d, encoding: .utf8) else { return }
    for line in s.split(separator: "\n") where line.contains("Dialog") || line.contains("Alert") {
        guard let data = line.data(using: .utf8),
            let o = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else {
            say("ログ: （JSON でない行）")
            continue
        }
        say("ログ: ts=\(o["ts"] ?? "") cat=\(o["cat"] ?? "") message=\(o["message"] ?? "")")
    }
}

func readMarker(_ path: String?) -> String {
    guard let path, let s = try? String(contentsOfFile: path, encoding: .utf8) else { return "(無い)" }
    return s.trimmingCharacters(in: .whitespacesAndNewlines)
}

func cmdWatch(_ args: [String]) {
    let app = requireVW()
    let ax = AXUIElementCreateApplication(app.processIdentifier)
    let timeout = Double(argValue(args, "--timeout") ?? "60") ?? 60
    let settle = UInt32(argValue(args, "--settle") ?? "800") ?? 800
    let count = Int(argValue(args, "--count") ?? "1") ?? 1
    let marker = argValue(args, "--marker")
    let press = args.contains("--no-press") ? nil : (argValue(args, "--press") ?? "")
    let useClick = args.contains("--real-click")
    let logStart = logSize()
    say("見張り開始 trust=\(AXIsProcessTrusted()) 窓=\(allWindows(ax).count) 既にあるダイアログ=\(dialogsOf(ax).count)")
    var done = 0
    let t0 = Date()
    while done < count && Date().timeIntervalSince(t0) < timeout {
        guard let w = dialogsOf(ax).first else {
            usleep(30_000)
            continue
        }
        say("ダイアログを見つけた: 開始から \(Int(Date().timeIntervalSince(t0) * 1000)) ms")
        say("marker=\(readMarker(marker))")
        say(windowLine(w))
        say("AXFocusedWindow=\(elem(ax, "AXFocusedWindow").map(windowLine) ?? "-")")
        say("AXMainWindow=\(elem(ax, "AXMainWindow").map(windowLine) ?? "-")")
        say("AXFrontmost=\(bool(ax, "AXFrontmost").map { "\($0)" } ?? "-") active=\(app.isActive)")
        dumpTree(w, "  ", 6)
        for (i, o) in allWindows(ax).enumerated() {
            say("全窓[\(i)] dialog=\(isDialog(o)) \(windowLine(o))")
        }
        cmdCg()
        sayLogLines(from: logStart)
        usleep(settle * 1000)
        guard let press else {
            say("押さない（--no-press）。ここで抜ける")
            return
        }
        let target = press.split(separator: "|").map(String.init)
        guard
            let b = buttons(w).first(where: { target.contains(str($0, "AXTitle") ?? "") })
                ?? (press == "" ? elem(w, "AXDefaultButton") : nil)
        else {
            say("押すボタンが無い: \(buttons(w).map { str($0, "AXTitle") ?? "?" })")
            return
        }
        if useClick {
            cmdClick(["--title", str(b, "AXTitle") ?? ""])
        } else {
            pressAndWait(ax, w, b)
        }
        done += 1
        usleep(150_000)
        say("押した後 marker=\(readMarker(marker)) 残りのダイアログ=\(dialogsOf(ax).count) active=\(app.isActive)")
    }
    if done < count { say("時間切れ: 押した回数=\(done)") }
    usleep(1_000_000)
    say("1 秒後 marker=\(readMarker(marker)) ダイアログ=\(dialogsOf(ax).count) 前面=\(NSWorkspace.shared.frontmostApplication?.localizedName ?? "-")")
    sayLogLines(from: logStart)
}

func cmdMenu(_ args: [String]) {
    guard args.count >= 2 else { exit(2) }
    let app = requireVW()
    let ax = AXUIElementCreateApplication(app.processIdentifier)
    guard let bar = elem(ax, "AXMenuBar") else {
        say("メニューバーが読めない: \(axName(attr(ax, "AXMenuBar").1))")
        exit(4)
    }
    for top in elems(bar, "AXChildren") where str(top, "AXTitle") == args[0] {
        for menu in elems(top, "AXChildren") {
            for item in elems(menu, "AXChildren") where str(item, "AXTitle") == args[1] {
                say("押す: \(describe(item))")
                let t0 = Date()
                let r = AXUIElementPerformAction(item, "AXPress" as CFString)
                say("AXPress: err=\(axName(r)) \(Int(Date().timeIntervalSince(t0) * 1000)) ms")
                return
            }
            say("項目: \(elems(menu, "AXChildren").map { str($0, "AXTitle") ?? "-" })")
        }
    }
    say("見つからない。トップ: \(elems(bar, "AXChildren").map { str($0, "AXTitle") ?? "-" })")
}

func cmdPing(_ args: [String]) {
    let app = requireVW()
    let ax = AXUIElementCreateApplication(app.processIdentifier)
    if let t = argValue(args, "--timeout"), let v = Float(t) {
        say("AXUIElementSetMessagingTimeout(\(v))=\(axName(AXUIElementSetMessagingTimeout(ax, v)))")
    }
    let t0 = Date()
    let (v, r) = attr(ax, "AXWindows")
    say("AXWindows: err=\(axName(r)) count=\((v as? [AnyObject])?.count ?? -1) \(Int(Date().timeIntervalSince(t0) * 1000)) ms")
}

let args = Array(CommandLine.arguments.dropFirst())
switch args.first ?? "" {
case "trust": cmdTrust()
case "cg": cmdCg()
case "list": cmdList(args)
case "press": cmdPress(args)
case "click": cmdClick(args)
case "watch": cmdWatch(args)
case "menu": cmdMenu(Array(args.dropFirst()))
case "ping": cmdPing(args)
case "front": say("前面=\(NSWorkspace.shared.frontmostApplication?.localizedName ?? "-")")
default:
    print("使い方は先頭のコメントを見る")
    exit(2)
}
