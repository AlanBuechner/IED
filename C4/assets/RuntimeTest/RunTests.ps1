# RuntimeTest runner - compiles + runs EVERY program under assets/RuntimeTest (one folder = one
# program). These are small end-to-end runtime behaviors (printf, struct return ABI) that must both
# compile AND run with exit 0. Scratch output goes to assets/build-output/runtime (never the repo
# root or the source folders).
#
# Usage:  .\RunTests.ps1 [-Filter "Printf"]
param(
    [string]$Filter = ""
)
$ErrorActionPreference = "Continue"

$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $root "..\..")).Path
$compiler = Join-Path $repoRoot "bin/Development-windows-x64/C4/C4.exe"
$work     = Join-Path $repoRoot "assets/build-output/runtime"

if (-not (Test-Path $compiler)) { Write-Host "COMPILER NOT FOUND: $compiler - build it first." -ForegroundColor Red; exit 1 }

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "C4 RuntimeTest Runner" + $(if ($Filter) { "(filter: $Filter)" }) -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$folders = Get-ChildItem $root -Recurse -Directory | Where-Object { (Get-ChildItem $_.FullName -Filter "*.cf").Count -gt 0 }
if ($Filter) { $folders = $folders | Where-Object { $_.Name -like "*$Filter*" -or $_.Parent.Name -eq $Filter } }

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
    $run = & $exe 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("  FAIL (exit " + $LASTEXITCODE + ")  " + $rel) -ForegroundColor Red
        $run | ForEach-Object { Write-Host ("      " + $_) }
        $failed++
    } else {
        Write-Host ("  PASS  " + $rel) -ForegroundColor Green
        $passed++
    }
}

Write-Host ""
Write-Host ("PASSED: " + $passed + "   FAILED: " + $failed + "   TOTAL: " + ($passed + $failed))
if ($failed -gt 0) { exit 1 }
