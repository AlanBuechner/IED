# GC pause and throughput under game-like churn (B32): a live world of entities, per-frame garbage and
# replacements, on the main thread and with the frame's churn on job-system workers. -Check runs a small world in
# both modes and requires the same checksum (the gate); without it the full sizes run, 3 times each, and the
# median run (by average frame time) of each mode is reported.
param([switch]$Check)
$ErrorActionPreference = "Continue"
$dir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $dir "../..")).Path
$c4   = Join-Path $repo "bin/Development-windows-x64/C4/C4.exe"
$out  = Join-Path $repo "assets/build-output/benchmarks/gc"
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $out "gc.exe"

Remove-Item $exe -ErrorAction SilentlyContinue
& $c4 (Join-Path $dir "gc.cf") -o $exe "-O2" *> (Join-Path $out "c4.log")
if (-not (Test-Path $exe)) { Write-Host "C4 build failed:"; Get-Content (Join-Path $out "c4.log") | Select-Object -Last 15; exit 1 }

function Field([string]$line, [string]$name) { if ($line -match "$name=([^ ]+)") { return $Matches[1] } return "" }

if ($Check) {
    $alone = & $exe 20000 60 4000 50 0
    $jobs  = & $exe 20000 60 4000 50 4
    Write-Host "alone: $alone"
    Write-Host "jobs:  $jobs"
    if ($LASTEXITCODE -ne 0 -or (Field $alone "checksum") -eq "" -or (Field $alone "checksum") -ne (Field $jobs "checksum")) {
        Write-Host "GC BENCHMARK MISMATCH"; exit 1
    }
    Write-Host "gc benchmark runs the same world alone and with workers."
    exit 0
}

$workers = [Math]::Max(1, [Environment]::ProcessorCount - 1)
foreach ($mode in @(@{ name = "alone"; workers = 0 }, @{ name = "jobs ($workers workers)"; workers = $workers })) {
    $runs = 1..3 | ForEach-Object { & $exe 200000 600 20000 200 $mode.workers }
    $median = $runs | Sort-Object { [double](Field $_ "avg_frame_ms") } | Select-Object -Index 1
    Write-Host ("{0}: {1}" -f $mode.name, $median)
}
