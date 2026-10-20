# C4 Test Runner - Clean, Build, Run, Report
# Native tools (C4.exe, lld) emit diagnostics on stderr even on success; only the exit code is authoritative.
# Usage:  .\RunTests.ps1 [-DebugInfo] [-Partitions <n>] [-Incremental]   (-DebugInfo: both builds with -g; -Partitions:
#         both builds assembled into <n> objects, the parallel code generation path; -Incremental: each object of the
#         -O2 build optimized alone, with copies of the small functions it calls from the others)
param([switch]$DebugInfo, [int]$Partitions = 0, [switch]$Incremental)
$ErrorActionPreference = "Continue"

# Paths are relative to this script (never hardcoded) so the suite runs from any checkout location.
$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $root "..\..")).Path
$compiler = Join-Path $repoRoot "bin/Development-windows-x64/C4/C4.exe"
$testDir  = $root
$flags    = @()
if ($DebugInfo) { $flags += "-g" }
if ($Partitions -gt 0) { $flags += @("--partitions", "$Partitions") }
if ($Incremental) { $flags += "--incremental" }
# Build artifacts go to assets/build-output/testcode, not into the test source folder.
$outputExe = Join-Path $repoRoot "assets/build-output/testcode/Main.exe"
$outputIrr = Join-Path $repoRoot "assets/build-output/testcode/Main.exe.ll"
New-Item -ItemType Directory -Force (Split-Path $outputExe) | Out-Null

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "C4 Test Runner" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Clean build intermediates
Write-Host "[1/4] Cleaning build intermediates..." -ForegroundColor Yellow
Get-ChildItem -Path $testDir -File -Include "*.obj", "*.o", "*.ll", "*.exe", "*.pdb", "*.args", "*.d" -Recurse | Remove-Item -Force -ErrorAction SilentlyContinue
Write-Host "  Cleaned." -ForegroundColor Green
Write-Host ""

# Build
Write-Host "[2/4] Building test suite (default -O0)..." -ForegroundColor Yellow
$buildStart = Get-Date
& $compiler $testDir -o $outputExe @flags 2>&1 | ForEach-Object { Write-Host $_ }
$buildEnd = Get-Date
$buildTime = ($buildEnd - $buildStart).TotalSeconds

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "BUILD FAILED!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "  Built in $($buildTime.ToString('F2'))s." -ForegroundColor Green
Write-Host ""

# Run
Write-Host "[3/4] Running test suite (default -O0)..." -ForegroundColor Yellow
Write-Host ""
& $outputExe 2>&1 | ForEach-Object { Write-Host $_ }

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "TESTS CRASHED!" -ForegroundColor Red
    exit 1
}

# Optimized-path gate: rebuild the SAME suite at -O2 and run it again. The default build is
# unoptimized (optLevel defaults to 0, matching C/C++ compilers), so this is the only place the
# clang -O<n> pipeline is exercised across every feature. A regression that only appears under
# optimization (e.g. UB the optimizer exposes) fails here even if the -O0 build passes.
Write-Host ""
Write-Host "[4/4] Optimized-path gate: rebuilding + running at -O2..." -ForegroundColor Yellow
$optimizedExe = Join-Path $repoRoot "assets/build-output/testcode/Main_O2.exe"
& $compiler $testDir -o $optimizedExe -O2 @flags 2>&1 | ForEach-Object { Write-Host $_ }

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "OPTIMIZED BUILD FAILED!" -ForegroundColor Red
    exit 1
}

Write-Host ""
& $optimizedExe 2>&1 | ForEach-Object { Write-Host $_ }

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "OPTIMIZED TESTS CRASHED!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Test run complete." -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
