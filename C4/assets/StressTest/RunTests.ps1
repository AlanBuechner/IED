# StressTest runner - compiles the large Dx12Game stress program (one folder = one program). The
# meaningful gate is that it COMPILES clean: it is an interactive DX12 game (window + input) that
# needs a GPU and does not self-terminate, so running it is opt-in via -Run. Scratch output goes to
# assets/build-output/stress (never the repo root or the source folders).
#
# Usage:  .\RunTests.ps1 [-Run]   (-Run also executes the built exe; requires a DX12-capable GPU)
param(
    [switch]$Run
)
$ErrorActionPreference = "Continue"

$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $root "..\..")).Path
$compiler = Join-Path (Split-Path -Parent $repoRoot) "build/bin/Development-windows-x64/C4/C4.exe"
$work     = Join-Path $repoRoot "assets/build-output/stress"

if (-not (Test-Path $compiler)) { Write-Host "COMPILER NOT FOUND: $compiler - build it first." -ForegroundColor Red; exit 1 }

Write-Host "========================================" -ForegroundColor Cyan
Write-Host ("C4 StressTest Runner" + $(if ($Run) { " (compile + run)" })) -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$folders = Get-ChildItem $root -Recurse -Directory | Where-Object { (Get-ChildItem $_.FullName -Filter "*.cf").Count -gt 0 }

$passed = 0; $failed = 0
foreach ($f in $folders) {
    $rel = $f.FullName.Substring($root.Length + 1).Replace("\", "/")
    $sub = Join-Path $work $rel
    if (Test-Path $sub) { Remove-Item $sub -Recurse -Force }
    New-Item -ItemType Directory -Force $sub | Out-Null
    Get-ChildItem $f.FullName -File | Where-Object { $_.Extension -in ".cf",".ch",".c",".cpp",".h",".hpp" } | ForEach-Object { Copy-Item $_.FullName (Join-Path $sub $_.Name) -Force }

    $exe = Join-Path $sub "out.exe"
    & $compiler $sub -o $exe 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("  FAIL (build)  " + $rel) -ForegroundColor Red
        $failed++
        continue
    }
    if (-not $Run) {
        Write-Host ("  PASS (compiles)  " + $rel) -ForegroundColor Green
        $passed++
        continue
    }
    # Opt-in run: the game is interactive and needs a GPU; report exit code but do not hang.
    $run = & $exe 2>&1
    Write-Host ("  RUN exit " + $LASTEXITCODE + "  " + $rel) -ForegroundColor Yellow
    $passed++
}

Write-Host ""
Write-Host ("PASSED: " + $passed + "   FAILED: " + $failed + "   TOTAL: " + ($passed + $failed))
if ($failed -gt 0) { exit 1 }
