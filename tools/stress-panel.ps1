# Stress test with the real mouse and keyboard: quick clicks on the +/- of the object
# panel's number fields, typing values, switching the keyboard language and dragging,
# on a hollow rounded bowl and a table joined from five parts. Reports for each one
# whether the app crashed (and where the crash report is) or came through.
#
# Takes the mouse and keyboard for a few minutes: start it and leave the computer.
# Where to click comes from the app itself (UI automation "layout", window off-screen,
# no real input), then tools/drive.ps1 does it for real with the window in front.
#
#   tools\stress-panel.ps1 [-Exe path\to\dune3d.exe] [-Rounds 4]
param([string]$Exe = "$PSScriptRoot\..\dist\dune3d\dune3d.exe", [int]$Rounds = 4,
      # the window's frame around what the app draws, as drive.ps1 sees it
      [int]$FrameX = 14, [int]$FrameY = 13)
$ErrorActionPreference = "Stop"
$Exe = (Resolve-Path $Exe).Path
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$work = Join-Path $env:TEMP "tavnit-stress"
New-Item -ItemType Directory -Force $work | Out-Null
$crashes = Join-Path $env:LOCALAPPDATA "dune3d\crashes"
$w = $work.Replace("\", "/")

# runs the app with an automation script, the window off-screen
function Invoke-Automation([string]$text) {
    $script = Join-Path $work "automation.txt"
    [IO.File]::WriteAllText($script, $text, (New-Object Text.UTF8Encoding $false)) # no BOM
    # one copy at a time: a second one hands its work to the first and quits
    while (Get-Process dune3d -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 300 }
    $env:DUNE3D_AUTOMATION = $script
    try { $p = Start-Process -FilePath $Exe -PassThru -WindowStyle Hidden; $p.WaitForExit(120000) | Out-Null }
    finally { Remove-Item Env:DUNE3D_AUTOMATION }
    if (!$p.HasExited) { $p.Kill(); throw "automation didn't finish" }
}

# the documents
Copy-Item "$PSScriptRoot\..\tests\ui\table.json" (Join-Path $work "table.json") -Force
Invoke-Automation @"
wait 1500
action NEW_DOCUMENT
wait 800
recipe $w/table.json
action VIEW_FIT
wait 500
save $w/table.d3ddoc
action NEW_DOCUMENT
wait 800
action ADD_BOWL
wait 1500
action VIEW_FIT
wait 500
save $w/bowl.d3ddoc
quit
"@

# per document: which row of the tree is the object (the joined table is the last "Top"),
# values to type into each field
$cases = @(
    @{ Name = "bowl"; Tree = "Bowl"; Values = @{ "Round edges" = @("0.5", "3", "1"); "Hollow" = @("1", "40", "2.5"); "X" = @("20", "0"); "Y" = @("-10", "0") } },
    @{ Name = "table"; Tree = "Top 4"; Values = @{ "Round edges" = @("1", "9", "0"); "Hollow" = @("2", "50", "0"); "X" = @("30", "0"); "Y" = @("-20", "0") } }
)

$results = @()
foreach ($c in $cases) {
    $doc = Join-Path $work "$($c.Name).d3ddoc"
    $layout = Join-Path $work "$($c.Name)-layout.txt"
    Invoke-Automation @"
wait 1500
open $($doc.Replace("\", "/"))
wait 2000
tree $($c.Tree)
wait 800
layout $($layout.Replace("\", "/"))
quit
"@
    $object = $null; $fields = @()
    foreach ($line in Get-Content $layout) {
        $f = $line -split "`t"
        if ($f[0] -eq "object") { $object = @(([int]$f[1] + $FrameX), ([int]$f[2] + $FrameY)) }
        elseif ($f[0] -eq "field") {
            $fields += @{ Label = $f[1]; Text = "$([int]$f[2] + $FrameX):$([int]$f[3] + $FrameY)";
                          Minus = "$([int]$f[4] + $FrameX):$([int]$f[5] + $FrameY)"; Plus = "$([int]$f[6] + $FrameX):$([int]$f[7] + $FrameY)" }
        }
    }
    if (!$object -or !$fields) { $results += "$($c.Name): couldn't find the object or its fields"; continue }

    $o = "$($object[0]):$($object[1])"
    $steps = @("wait:3000", "click:${o}", "wait:1200")
    for ($r = 0; $r -lt $Rounds; $r++) {
        foreach ($fl in $fields) {
            $steps += (1..8 | ForEach-Object { "click:$($fl.Plus),wait:30" })
            $steps += (1..8 | ForEach-Object { "click:$($fl.Minus),wait:30" })
            $vals = $c.Values[$fl.Label]
            if ($vals) {
                $v = $vals[$r % $vals.Count]
                # type it, then switch the keyboard language (Alt+Shift) while the field has focus
                $steps += "click:$($fl.Text),key:{CLEAR}$v{ENTER},wait:80,key:%+,wait:80"
            }
        }
        $steps += "drag:${o}:$($object[0] + 60):$($object[1] + 30):left,wait:300"
        $steps += "drag:$($object[0] + 60):$($object[1] + 30):${o}:left,wait:300,click:${o},wait:600"
    }
    $shot = Join-Path $work "$($c.Name)-end.png"
    $steps += "wait:3000,shot:$shot"

    while (Get-Process dune3d -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 300 }
    $before = Get-Date
    $failed = $null
    try {
        & "$PSScriptRoot\drive.ps1" -Exe $Exe -Doc $doc -Width (1400 + 2 * $FrameX) -Height (900 + 2 * $FrameY) -Steps ($steps -join ",")
    }
    catch { $failed = $_.Exception.Message }
    Start-Sleep 2
    $report = Get-ChildItem $crashes -Filter "crash-*.txt" -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $before } | Sort-Object LastWriteTime | Select-Object -Last 1
    Get-Process dune3d -ErrorAction SilentlyContinue | Stop-Process -Force # the crash message, if any
    if ($report) { $results += "$($c.Name): CRASHED - $($report.FullName)" }
    elseif ($failed -like "*could not focus*") { $results += "$($c.Name): not run - the app couldn't get the focus (someone's using the computer?)" }
    elseif ($failed) { $results += "$($c.Name): FAILED - $failed (did the window go away?)" }
    else { $results += "$($c.Name): ok ($Rounds rounds, $($fields.Count) fields; last look: $shot)" }
}
$results | ForEach-Object { Write-Output $_ }
