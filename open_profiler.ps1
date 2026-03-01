# Opens the easy_profiler GUI to view profiler_dump.prof
# Usage: .\open_profiler.ps1 [path-to-dump]

param(
    [string]$DumpFile = ""
)

$buildDir = Join-Path $PSScriptRoot "build"
$profilerGui = Get-ChildItem -Path $buildDir -Recurse -Filter "profiler_gui.exe" -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $profilerGui) {
    Write-Error "profiler_gui.exe not found under $buildDir. Make sure the project has been built."
    exit 1
}

if ($DumpFile -eq "") {
    $candidates = @(
        Join-Path $buildDir "profiler_dump.prof"
        Join-Path $PSScriptRoot "profiler_dump.prof"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            $DumpFile = $c
            break
        }
    }
}

Write-Host "Profiler GUI: $($profilerGui.FullName)"

if ($DumpFile -ne "" -and (Test-Path $DumpFile)) {
    Write-Host "Opening dump:  $DumpFile"
    & $profilerGui.FullName $DumpFile
} else {
    if ($DumpFile -ne "") {
        Write-Warning "Dump file not found: $DumpFile"
    } else {
        Write-Warning "No profiler_dump.prof found in build/ or project root."
        Write-Host "Run your app first so it writes profiler_dump.prof on exit, then re-run this script."
    }
    Write-Host "Launching profiler GUI without a dump file..."
    & $profilerGui.FullName
}
