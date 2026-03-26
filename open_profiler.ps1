# Opens the easy_profiler GUI to view profiler_dump.prof
# Usage:
#   .\open_profiler.ps1
#   .\open_profiler.ps1 -DumpFile path-to-dump
#   .\open_profiler.ps1 -Target some\path\fragment

param(
    [string]$DumpFile = "",
    [string]$Target = ""
)

$buildDir = Join-Path $PSScriptRoot "build"
$profilerGui = Get-ChildItem -Path $buildDir -Recurse -Filter "profiler_gui.exe" -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $profilerGui) {
    Write-Error "profiler_gui.exe not found under $buildDir. Make sure the project has been built."
    exit 1
}

if ($DumpFile -eq "") {
    $dumpCandidates = @()

    if (Test-Path $buildDir) {
        $dumpCandidates += Get-ChildItem -Path $buildDir -Recurse -Filter "profiler_dump.prof" -ErrorAction SilentlyContinue
    }

    $rootDump = Join-Path $PSScriptRoot "profiler_dump.prof"
    if (Test-Path $rootDump) {
        $dumpCandidates += Get-Item $rootDump
    }

    if ($Target -ne "") {
        $targetLower = $Target.ToLowerInvariant()
        $dumpCandidates = $dumpCandidates | Where-Object {
            $_.FullName.ToLowerInvariant().Contains($targetLower)
        }
    }

    $selectedDump = $dumpCandidates |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($selectedDump) {
        $DumpFile = $selectedDump.FullName
    }
}

Write-Host "Profiler GUI: $($profilerGui.FullName)"

if ($DumpFile -ne "" -and (Test-Path $DumpFile)) {
    Write-Host "Opening dump:  $DumpFile"
    & $profilerGui.FullName $DumpFile
} else {
    if ($DumpFile -ne "") {
        Write-Warning "Dump file not found: $DumpFile"
    } elseif ($Target -ne "") {
        Write-Warning "No profiler_dump.prof found matching target path fragment: $Target"
    } else {
        Write-Warning "No profiler_dump.prof found under build/ or project root."
        Write-Host "Run your app first so it writes profiler_dump.prof on exit, then re-run this script."
    }
    Write-Host "Launching profiler GUI without a dump file..."
    & $profilerGui.FullName
}
