# Dev helper for visual checks: launches the built app, then runs a script of
# steps against its window and closes it again. Steps (comma separated):
#   click:X:Y      left click at window-relative pixel X,Y (as seen in a shot)
#   rclick:X:Y     right click
#   dclick:X:Y     double click
#   move:X:Y       hover
#   drag:X1:Y1:X2:Y2:BUTTON   drag with left/right/middle button
#   key:TEXT       type text; {ENTER} {ESC} {BS} {DEL} {TAB} {HOME} {END} {SPACE}
#                  and {CLEAR} (End + 20 backspaces) for special keys
#   wait:MS        sleep
#   shot:PATH      capture only the app's windows to PATH
# By default input is posted as window messages straight to the app, so the
# real mouse and keyboard are untouched and the app needn't be in front.
# -RealInput uses the real cursor/keyboard instead (needs focus).
param([string]$Steps, [string]$Doc = "", [string]$Exe = "$PSScriptRoot\..\build\dune3d.exe",
      [int]$Width = 1400, [int]$Height = 900, [switch]$RealInput)
$ErrorActionPreference = "Stop"
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public class U {
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, UIntPtr e);
[DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool alt);
public delegate bool EnumProc(IntPtr h, IntPtr p);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
// visible top-level windows of a process, bottom-most first (EnumWindows goes top-down)
public static System.Collections.Generic.List<IntPtr> ProcessWindows(uint pid) {
    var l = new System.Collections.Generic.List<IntPtr>();
    EnumWindows((h, p) => { uint q; GetWindowThreadProcessId(h, out q); if (q == pid && IsWindowVisible(h)) l.Add(h); return true; }, IntPtr.Zero);
    l.Reverse(); return l; }
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, UIntPtr e);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
[DllImport("user32.dll")] public static extern short VkKeyScan(char c);
public struct POINT { public int X, Y; }
public struct RECT { public int L, T, R, B; } }
"@
$p = if ($Doc) { Start-Process $Exe -ArgumentList "`"$Doc`"" -PassThru } else { Start-Process $Exe -PassThru }
try {
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.HasExited) { throw "app exited: $($p.ExitCode)" }; if ($p.MainWindowHandle -ne 0) { break } }
    $h = $p.MainWindowHandle
    [U]::ShowWindow($h, 9) | Out-Null
    [U]::MoveWindow($h, 0, 0, $Width, $Height, $true) | Out-Null
    Start-Sleep 3
    function Focus {
        for ($try = 0; $try -lt 8 -and [U]::GetForegroundWindow() -ne $h; $try++) {
            # Windows only lets a process steal focus right after a key event; a bare Alt tap is enough.
            [U]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [U]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
            [U]::SetForegroundWindow($h) | Out-Null; [U]::SwitchToThisWindow($h, $true)
            Start-Sleep -Milliseconds 400 }
        Start-Sleep -Milliseconds 300
        if ([U]::GetForegroundWindow() -ne $h) { throw "could not focus the app window; not sending input" } }
    function Origin { $r = New-Object U+RECT; [U]::GetWindowRect($h, [ref]$r) | Out-Null; $r }
    # topmost window of ours under a window-relative point, and the point in its client coordinates
    function Target([int]$x, [int]$y) {
        $r = Origin; $sx = $r.L + $x; $sy = $r.T + $y
        $wins = [U]::ProcessWindows([uint32]$p.Id); $wins.Reverse()
        foreach ($w in $wins) {
            $wr = New-Object U+RECT; [U]::GetWindowRect($w, [ref]$wr) | Out-Null
            if ($sx -ge $wr.L -and $sx -lt $wr.R -and $sy -ge $wr.T -and $sy -lt $wr.B) {
                $pt = New-Object U+POINT; $pt.X = $sx; $pt.Y = $sy; [U]::ScreenToClient($w, [ref]$pt) | Out-Null
                return @($w, $pt.X, $pt.Y) } }
        throw "no app window at $x,$y" }
    function Post($w, [uint32]$msg, [int64]$wp, [int]$cx, [int]$cy) {
        $lp = [IntPtr]([int64](($cy -band 0xffff) -shl 16) -bor ($cx -band 0xffff))
        [U]::PostMessage($w, $msg, [IntPtr]$wp, $lp) | Out-Null }
    # WM_*BUTTONDOWN, WM_*BUTTONUP and the MK_* flag for each button
    $msgs = @{ left = @(0x201, 0x202, 1); right = @(0x204, 0x205, 2); middle = @(0x207, 0x208, 0x10) }
    function MsgClick([int]$x, [int]$y, [string]$b, [int]$n) {
        $t = Target $x $y; $m = $msgs[$b]
        Post $t[0] 0x200 0 $t[1] $t[2]; Start-Sleep -Milliseconds 120
        for ($k = 0; $k -lt $n; $k++) {
            # the second press of a double click arrives as WM_*BUTTONDBLCLK
            $down = if ($k -eq 1) { $m[0] + 2 } else { $m[0] }
            Post $t[0] $down $m[2] $t[1] $t[2]; Start-Sleep -Milliseconds 40
            Post $t[0] $m[1] 0 $t[1] $t[2]; Start-Sleep -Milliseconds 60 }
        Start-Sleep -Milliseconds 600 }
    function MsgKey([int]$vk) {
        $sc = [int64][U]::MapVirtualKey($vk, 0)
        [U]::PostMessage($h, 0x100, [IntPtr]$vk, [IntPtr](1 -bor ($sc -shl 16))) | Out-Null; Start-Sleep -Milliseconds 30
        [U]::PostMessage($h, 0x101, [IntPtr]$vk, [IntPtr](1 -bor ($sc -shl 16) -bor 0xC0000000)) | Out-Null; Start-Sleep -Milliseconds 30 }
    $special = @{ ENTER = 0x0D; ESC = 0x1B; BS = 0x08; DEL = 0x2E; TAB = 0x09; HOME = 0x24; END = 0x23; SPACE = 0x20 }
    function MsgType([string]$text) {
        foreach ($tok in [regex]::Matches($text, '\{[A-Z]+\}|.')) {
            $t = $tok.Value
            if ($t -eq "{CLEAR}") { MsgKey 0x23; for ($k = 0; $k -lt 20; $k++) { MsgKey 0x08 } }
            elseif ($t.StartsWith("{")) { MsgKey $special[$t.Trim("{}")] }
            else { MsgKey ([U]::VkKeyScan([char]$t) -band 0xff) } }
        Start-Sleep -Milliseconds 600 }
    Start-Sleep -Milliseconds 1500
    if ($RealInput) { Focus; Start-Sleep -Milliseconds 700 }
    $btn = @{ left = @(0x2, 0x4); right = @(0x8, 0x10); middle = @(0x20, 0x40) }
    foreach ($step in $Steps.Split(",")) {
        $a = $step.Trim().Split(":", 2); $cmd = $a[0]; $arg = if ($a.Count -gt 1) { $a[1] } else { "" }
        switch ($cmd) {
            "wait" { Start-Sleep -Milliseconds ([int]$arg) }
            { -not $RealInput -and $_ -in "click", "rclick", "dclick", "move", "drag", "key" } {
                $v = $arg.Split(":")
                switch ($cmd) {
                    "click" { MsgClick $v[0] $v[1] "left" 1 }
                    "rclick" { MsgClick $v[0] $v[1] "right" 1 }
                    "dclick" { MsgClick $v[0] $v[1] "left" 2 }
                    "move" { $t = Target $v[0] $v[1]; Post $t[0] 0x200 0 $t[1] $t[2]; Start-Sleep -Milliseconds 300 }
                    "drag" {
                        $m = $msgs[$v[4]]; $t = Target $v[0] $v[1]
                        $dx = $t[1] - [int]$v[0]; $dy = $t[2] - [int]$v[1]
                        Post $t[0] 0x200 0 $t[1] $t[2]; Start-Sleep -Milliseconds 100
                        Post $t[0] $m[0] $m[2] $t[1] $t[2]; Start-Sleep -Milliseconds 60
                        for ($k = 1; $k -le 20; $k++) {
                            $x = [int]$v[0] + ([int]$v[2] - [int]$v[0]) * $k / 20; $y = [int]$v[1] + ([int]$v[3] - [int]$v[1]) * $k / 20
                            Post $t[0] 0x200 $m[2] ($x + $dx) ($y + $dy); Start-Sleep -Milliseconds 25 }
                        Post $t[0] $m[1] 0 ([int]$v[2] + $dx) ([int]$v[3] + $dy); Start-Sleep -Milliseconds 600 }
                    "key" { MsgType $arg }
                }
                continue }
            { $_ -in "click", "dclick" } {
                Focus; $r = Origin; $xy = $arg.Split(":")
                [U]::SetCursorPos($r.L + [int]$xy[0], $r.T + [int]$xy[1]) | Out-Null; Start-Sleep -Milliseconds 150
                $n = if ($cmd -eq "dclick") { 2 } else { 1 }
                for ($k = 0; $k -lt $n; $k++) { [U]::mouse_event(0x2, 0, 0, 0, [UIntPtr]::Zero); [U]::mouse_event(0x4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60 }
                Start-Sleep -Milliseconds 600 }
            "drag" {
                Focus; $r = Origin; $v = $arg.Split(":"); $b = $btn[$v[4]]
                [U]::SetCursorPos($r.L + [int]$v[0], $r.T + [int]$v[1]) | Out-Null; Start-Sleep -Milliseconds 150
                [U]::mouse_event($b[0], 0, 0, 0, [UIntPtr]::Zero)
                for ($k = 1; $k -le 20; $k++) {
                    $x = [int]$v[0] + ([int]$v[2] - [int]$v[0]) * $k / 20; $y = [int]$v[1] + ([int]$v[3] - [int]$v[1]) * $k / 20
                    [U]::SetCursorPos($r.L + $x, $r.T + $y) | Out-Null; Start-Sleep -Milliseconds 25 }
                [U]::mouse_event($b[1], 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 600 }
            "key" { Focus; [System.Windows.Forms.SendKeys]::SendWait($arg); Start-Sleep -Milliseconds 600 }
            "shot" {
                # main window plus the app's own popups (menus are separate windows), nothing else
                $r = Origin
                $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
                $g = [System.Drawing.Graphics]::FromImage($bmp)
                foreach ($w in [U]::ProcessWindows([uint32]$p.Id)) {
                    $wr = New-Object U+RECT; [U]::GetWindowRect($w, [ref]$wr) | Out-Null
                    $ww = $wr.R - $wr.L; $wh = $wr.B - $wr.T
                    if ($ww -le 0 -or $wh -le 0) { continue }
                    $wb = New-Object System.Drawing.Bitmap $ww, $wh
                    $wg = [System.Drawing.Graphics]::FromImage($wb); $hdc = $wg.GetHdc()
                    [U]::PrintWindow($w, $hdc, 2) | Out-Null; $wg.ReleaseHdc($hdc); $wg.Dispose()
                    $g.DrawImage($wb, $wr.L - $r.L, $wr.T - $r.T); $wb.Dispose()
                }
                $bmp.Save($arg); $g.Dispose(); $bmp.Dispose() }
            default { throw "unknown step: $step" }
        }
    }
}
finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -ErrorAction SilentlyContinue }
}
