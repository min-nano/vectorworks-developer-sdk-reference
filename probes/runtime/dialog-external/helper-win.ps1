# probes/runtime/dialog-external/helper-win.ps1
#
# [issue #302] CLI の代わりに「別のプロセス」から Vectorworks のダイアログを見つけ、読み、押す（Windows）。
# プラグインには入らない。mac の helper-mac.swift と同じ問いを Win32 で測る。
#
#   helper-win.ps1 trust                      自分と VW の昇格（整合性レベル）を読む
#   helper-win.ps1 list [-All]                VW の窓を読む（既定は可視の #32770 と中身）
#   helper-win.ps1 press -Id N | -Title T     最前の可視の #32770 のボタンへ BM_CLICK
#   helper-win.ps1 watch -Title T [-Count N] [-SettleMs 800] [-TimeoutSec 60] [-Marker P]
#                                             新しいダイアログを待って読み、押し、閉じるまで測る
#   helper-win.ps1 busy                       SendMessageTimeout で VW が答えるかを測る
#
# 非同期の -Title は表示言語で変わるので、ID（-Id）も併記して記録する。
param(
    [Parameter(Position = 0, Mandatory = $true)][string]$Command,
    [switch]$All,
    [int]$Id = -1,
    [string]$Title = '',
    [int]$Count = 1,
    [int]$SettleMs = 800,
    [int]$TimeoutSec = 60,
    [string]$Marker = ''
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class VwDlg {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int i);
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll", SetLastError = true)] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("kernel32.dll")] public static extern uint GetLastError();
    [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("advapi32.dll", SetLastError = true)] public static extern bool OpenProcessToken(IntPtr p, uint access, out IntPtr t);
    [DllImport("advapi32.dll", SetLastError = true)] public static extern bool GetTokenInformation(IntPtr t, int cls, IntPtr info, int len, out int ret);
    [DllImport("advapi32.dll")] public static extern IntPtr GetSidSubAuthority(IntPtr sid, uint n);
    [DllImport("advapi32.dll")] public static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);

    public const uint WM_GETTEXT = 0x000D;
    public const uint BM_CLICK = 0x00F5;
    public const uint SMTO_ABORTIFHUNG = 2;

    public static string Class(IntPtr h) { var s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }

    public static string Text(IntPtr h) {
        // 別のプロセスの窓の文字は WM_GETTEXT で読む（GetWindowText は止まっている窓では待たない代わりに
        // 別プロセスのコントロールの文字を返さないことがある）。
        var buf = Marshal.AllocHGlobal(8192);
        try {
            IntPtr r;
            var ok = SendMessageTimeoutW(h, WM_GETTEXT, (IntPtr)4000, buf, SMTO_ABORTIFHUNG, 2000, out r);
            if (ok == IntPtr.Zero) return "(読めない err=" + Marshal.GetLastWin32Error() + ")";
            return Marshal.PtrToStringUni(buf);
        } finally { Marshal.FreeHGlobal(buf); }
    }

    public static List<IntPtr> Tops(uint pid) {
        var o = new List<IntPtr>();
        EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid) o.Add(h); return true; }, IntPtr.Zero);
        return o;
    }

    public static List<IntPtr> Children(IntPtr parent) {
        var o = new List<IntPtr>();
        EnumChildWindows(parent, (h, l) => { o.Add(h); return true; }, IntPtr.Zero);
        return o;
    }

    public static string Integrity(uint pid) {
        var p = pid == 0 ? GetCurrentProcess() : OpenProcess(0x1000, false, pid);
        if (p == IntPtr.Zero) return "(開けない err=" + Marshal.GetLastWin32Error() + ")";
        IntPtr t;
        if (!OpenProcessToken(p, 8, out t)) return "(トークンを開けない err=" + Marshal.GetLastWin32Error() + ")";
        int len;
        GetTokenInformation(t, 25, IntPtr.Zero, 0, out len);
        var buf = Marshal.AllocHGlobal(len);
        try {
            if (!GetTokenInformation(t, 25, buf, len, out len)) return "(読めない err=" + Marshal.GetLastWin32Error() + ")";
            var sid = Marshal.ReadIntPtr(buf);
            var cnt = Marshal.ReadByte(GetSidSubAuthorityCount(sid));
            var rid = (uint)Marshal.ReadInt32(GetSidSubAuthority(sid, (uint)(cnt - 1)));
            return "0x" + rid.ToString("x4");
        } finally { Marshal.FreeHGlobal(buf); CloseHandle(t); }
    }

    public static string Click(IntPtr button, int ms, out int lastError) {
        IntPtr r;
        var ok = SendMessageTimeoutW(button, BM_CLICK, IntPtr.Zero, IntPtr.Zero, SMTO_ABORTIFHUNG, (uint)ms, out r);
        lastError = Marshal.GetLastWin32Error();
        return ok == IntPtr.Zero ? "戻らなかった" : "ok";
    }
}
'@

function Say([string]$s) { Write-Output ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss.fff'), $s) }

function Get-VwPid {
    $p = Get-Process | Where-Object { $_.ProcessName -like 'Vectorworks20*' } | Select-Object -First 1
    if (-not $p) { Say 'Vectorworks が居ない'; exit 3 }
    return [uint32]$p.Id
}

function Get-Dialogs([uint32]$vw) {
    return @([VwDlg]::Tops($vw) | Where-Object { [VwDlg]::IsWindowVisible($_) -and [VwDlg]::Class($_) -eq '#32770' })
}

function Show-Window([IntPtr]$h) {
    $owner = [VwDlg]::GetWindow($h, 4)
    $style = [VwDlg]::GetWindowLongW($h, -16)
    Say ("hwnd=0x{0:x} class={1} title=`"{2}`" visible={3} enabled={4} owner=0x{5:x}[{6}] style=0x{7:x8} foreground={8}" -f `
        [int64]$h, [VwDlg]::Class($h), [VwDlg]::Text($h), [VwDlg]::IsWindowVisible($h), [VwDlg]::IsWindowEnabled($h), `
        [int64]$owner, $(if ($owner -ne [IntPtr]::Zero) { [VwDlg]::Class($owner) } else { '-' }), $style, ([VwDlg]::GetForegroundWindow() -eq $h))
}

function Show-Controls([IntPtr]$h) {
    foreach ($c in [VwDlg]::Children($h)) {
        Say ("  child class={0} id={1} visible={2} enabled={3} text=`"{4}`"" -f `
            [VwDlg]::Class($c), [VwDlg]::GetDlgCtrlID($c), [VwDlg]::IsWindowVisible($c), [VwDlg]::IsWindowEnabled($c), [VwDlg]::Text($c))
    }
}

function Find-Button([IntPtr]$dlg, [int]$id, [string]$title) {
    foreach ($c in [VwDlg]::Children($dlg)) {
        if ([VwDlg]::Class($c) -ne 'Button') { continue }
        if ($id -ge 0 -and [VwDlg]::GetDlgCtrlID($c) -eq $id) { return $c }
        if ($title -ne '') {
            foreach ($t in $title.Split('|')) { if ([VwDlg]::Text($c) -eq $t) { return $c } }
        }
    }
    return [IntPtr]::Zero
}

function Press([IntPtr]$dlg, [int]$id, [string]$title) {
    $b = Find-Button $dlg $id $title
    if ($b -eq [IntPtr]::Zero) { Say 'ボタンが無い'; Show-Controls $dlg; return $false }
    Say ("押す: id={0} text=`"{1}`"" -f [VwDlg]::GetDlgCtrlID($b), [VwDlg]::Text($b))
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $err = 0
    # BM_CLICK は押した先の処理が終わるまで戻らないことがあるので、上限を付ける。
    $r = [VwDlg]::Click($b, 5000, [ref]$err)
    Say ("BM_CLICK: {0} lastError={1} {2} ms" -f $r, $err, $sw.ElapsedMilliseconds)
    while ($sw.Elapsed.TotalSeconds -lt 60) {
        if (-not [VwDlg]::IsWindow($dlg) -or -not [VwDlg]::IsWindowVisible($dlg)) {
            Say ("閉じた: 押してから {0} ms" -f $sw.ElapsedMilliseconds)
            return $true
        }
        Start-Sleep -Milliseconds 20
    }
    Say '押してから 60 秒たっても閉じない'
    return $false
}

function Read-Marker {
    if ($Marker -eq '' -or -not (Test-Path -LiteralPath $Marker)) { return '(無い)' }
    return (Get-Content -LiteralPath $Marker -Raw).Trim()
}

function Get-LogPath { return Join-Path $env:APPDATA 'Nemetschek\Vectorworks\2026\VW User Log.txt' }

function Show-Log([int64]$from) {
    $path = Get-LogPath
    if (-not (Test-Path -LiteralPath $path)) { Say "ログが無い: $path"; return }
    $len = (Get-Item -LiteralPath $path).Length
    if ($len -lt $from) { Say "ログが縮んだ（$from → $len）"; return }
    $fs = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite,Delete')
    try {
        $null = $fs.Seek($from, 'Begin')
        $text = (New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)).ReadToEnd()
    } finally { $fs.Dispose() }
    foreach ($line in $text -split "`n") {
        if ($line -notmatch 'Dialog') { continue }
        try { $o = $line | ConvertFrom-Json; Say ("ログ: ts={0} cat={1} message={2}" -f $o.ts, $o.cat, $o.message) }
        catch { Say 'ログ: （JSON でない行）' }
    }
}

switch ($Command) {
    'trust' {
        Say ("self pid={0} integrity={1}" -f $PID, [VwDlg]::Integrity(0))
        $vw = Get-VwPid
        Say ("VW pid={0} integrity={1}" -f $vw, [VwDlg]::Integrity($vw))
    }
    'list' {
        $vw = Get-VwPid
        Say ("VW pid=$vw integrity=" + [VwDlg]::Integrity($vw))
        foreach ($h in [VwDlg]::Tops($vw)) {
            $isDlg = [VwDlg]::IsWindowVisible($h) -and [VwDlg]::Class($h) -eq '#32770'
            if (-not $All -and -not $isDlg) { continue }
            if (-not [VwDlg]::IsWindowVisible($h) -and $All) { continue }
            Show-Window $h
            if ($isDlg) { Show-Controls $h }
        }
    }
    'press' {
        $vw = Get-VwPid
        $d = Get-Dialogs $vw
        if ($d.Count -eq 0) { Say 'ダイアログが無い'; exit 4 }
        Show-Window $d[0]
        if (-not (Press $d[0] $Id $Title)) { exit 5 }
    }
    'watch' {
        $vw = Get-VwPid
        $path = Get-LogPath
        $logStart = if (Test-Path -LiteralPath $path) { (Get-Item -LiteralPath $path).Length } else { 0 }
        Say ("見張り開始 既にあるダイアログ={0}" -f (Get-Dialogs $vw).Count)
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $done = 0
        while ($done -lt $Count -and $sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
            $d = Get-Dialogs $vw
            if ($d.Count -eq 0) { Start-Sleep -Milliseconds 30; continue }
            Say ("ダイアログを見つけた: 開始から {0} ms 枚数={1}" -f $sw.ElapsedMilliseconds, $d.Count)
            Say ("marker=" + (Read-Marker))
            foreach ($h in $d) { Show-Window $h; Show-Controls $h }
            Show-Log $logStart
            Start-Sleep -Milliseconds $SettleMs
            $null = Press $d[0] $Id $Title
            $done++
            Start-Sleep -Milliseconds 150
            Say ("押した後 marker=" + (Read-Marker))
        }
        if ($done -lt $Count) { Say "時間切れ: 押した回数=$done" }
        Start-Sleep -Seconds 1
        Say ("1 秒後 marker=" + (Read-Marker))
        Show-Log $logStart
    }
    'busy' {
        $vw = Get-VwPid
        $h = ([VwDlg]::Tops($vw) | Where-Object { [VwDlg]::IsWindowVisible($_) } | Select-Object -First 1)
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        Say ("title=`"{0}`" {1} ms" -f [VwDlg]::Text($h), $sw.ElapsedMilliseconds)
    }
    default { Write-Output '使い方は先頭のコメントを見る'; exit 2 }
}
