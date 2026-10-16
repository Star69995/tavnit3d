# Dev helper for visual checks: launches the built app, then runs a script of
# steps against its window and closes it again. Steps (comma separated):
#   click:X:Y      left click at window-relative pixel X,Y (as seen in a shot)
#   dclick:X:Y     double click
#   drag:X1:Y1:X2:Y2:BUTTON   drag with left/right/middle button
#   key:TEXT       send keys (System.Windows.Forms.SendKeys syntax, e.g. {ENTER})
#   wait:MS        sleep
#   shot:PATH      capture only the app window to PATH
param([string]$Steps, [string]$Doc = "", [string]$Exe = "$PSScriptRoot\..\build\dune3d.exe",
      [int]$Width = 1400, [int]$Height = 900)
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
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, UIntPtr e);
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
        if ([U]::GetForegroundWindow() -ne $h) {
            # Windows only lets a process steal focus right after a key event; a bare Alt tap is enough.
            [U]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [U]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
            [U]::SetForegroundWindow($h) | Out-Null; [U]::SwitchToThisWindow($h, $true) }
        Start-Sleep -Milliseconds 300
        if ([U]::GetForegroundWindow() -ne $h) { throw "could not focus the app window; not sending input" } }
    function Origin { $r = New-Object U+RECT; [U]::GetWindowRect($h, [ref]$r) | Out-Null; $r }
    Focus; Start-Sleep -Milliseconds 700
    $btn = @{ left = @(0x2, 0x4); right = @(0x8, 0x10); middle = @(0x20, 0x40) }
    foreach ($step in $Steps.Split(",")) {
        $a = $step.Trim().Split(":", 2); $cmd = $a[0]; $arg = if ($a.Count -gt 1) { $a[1] } else { "" }
        switch ($cmd) {
            "wait" { Start-Sleep -Milliseconds ([int]$arg) }
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
                $r = Origin
                $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
                $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
                [U]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc); $bmp.Save($arg); $g.Dispose(); $bmp.Dispose() }
            default { throw "unknown step: $step" }
        }
    }
}
finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -ErrorAction SilentlyContinue }
}
