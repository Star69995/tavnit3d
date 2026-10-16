# Captures only the main window of a running process to a PNG (dev helper for visual checks).
# Uses PrintWindow so other windows on screen are never captured.
param([string]$Out, [int]$ProcId = 0, [string]$ProcName = "dune3d", [int]$Width = 1400, [int]$Height = 900)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
[DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
public struct RECT { public int L, T, R, B; } }
"@
$p = if ($ProcId) { Get-Process -Id $ProcId } else { Get-Process $ProcName | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1 }
$h = $p.MainWindowHandle
[W]::ShowWindow($h, 9) | Out-Null
[W]::MoveWindow($h, 0, 0, $Width, $Height, $true) | Out-Null
Start-Sleep -Milliseconds 1500
$r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[W]::PrintWindow($h, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$bmp.Save($Out)
