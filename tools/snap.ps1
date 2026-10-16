# Dev helper: launches the built app (optionally with a document), waits for its window,
# captures only that window to a PNG, then closes the app it started.
param([string]$Out, [string]$Doc = "", [string]$Exe = "$PSScriptRoot\..\build\dune3d.exe", [int]$Settle = 4)
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$args_ = @(); if ($Doc) { $args_ = @("`"$Doc`"") }
$p = if ($args_.Count) { Start-Process $Exe -ArgumentList $args_ -PassThru } else { Start-Process $Exe -PassThru }
for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.HasExited) { "app exited: $($p.ExitCode)"; exit 1 }; if ($p.MainWindowHandle -ne 0) { break } }
Start-Sleep $Settle
& "$PSScriptRoot\screenshot.ps1" -Out $Out -ProcId $p.Id
Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
