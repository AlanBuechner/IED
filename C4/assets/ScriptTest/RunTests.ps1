# ScriptTest runner - scripting end to end (docs/runtime/04). One folder = one case: host/ is a program built with
# --script-host (with Common/ beside its files), scripts/ holds the scripts it compiles while it runs, copied beside
# the built program. The host checks everything and exits 0 when all of it held. Without -O2 the host and its
# scripts are -O0; with it both are -O2. With -DebugScripts every host is built with -g, so its scripts are debug
# scripts (docs/runtime/04 section 6.4: each a DLL with a PDB). A case's flags.txt holds more compiler flags for its
# host (a target such as -march=x86-64-v3, which its scripts then compile for too). Scratch output goes to
# assets/build-output/script.
#
# Usage:  .\RunTests.ps1 [-Filter "Reload"] [-O2] [-DebugScripts]
param(
    [string]$Filter = "",
    [switch]$O2,
    [switch]$DebugScripts,
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = "Continue"

$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $root "..\..")).Path
$compiler = Join-Path (Split-Path -Parent $repoRoot) "build/bin/Development-windows-x64/C4/C4.exe"
$mode     = if ($O2) { "O2" } else { "O0" }   # what each host compiles its scripts with
$variant  = $mode + $(if ($DebugScripts) { "-debug" } else { "" })
$work     = Join-Path $repoRoot "assets/build-output/script/$variant"
$common   = Join-Path $root "Common"

if (-not (Test-Path $compiler)) { Write-Host "COMPILER NOT FOUND: $compiler - build it first." -ForegroundColor Red; exit 1 }

Write-Host "========================================" -ForegroundColor Cyan
Write-Host ("C4 ScriptTest Runner (" + $variant + ")" + $(if ($Filter) { " (filter: $Filter)" })) -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$cases = Get-ChildItem $root -Directory | Where-Object { Test-Path (Join-Path $_.FullName "host") }
if ($Filter) { $cases = $cases | Where-Object { $_.Name -like "*$Filter*" } }

function Copy-Sources([string]$from, [string]$to) {
    New-Item -ItemType Directory -Force $to | Out-Null
    Get-ChildItem $from -File | Where-Object { $_.Extension -in ".cf",".ch",".c",".h" } | ForEach-Object { Copy-Item $_.FullName (Join-Path $to $_.Name) -Force }
}

$passed = 0; $failed = 0
foreach ($case in $cases) {
    $sub = Join-Path $work $case.Name
    if (Test-Path $sub) { Remove-Item $sub -Recurse -Force }
    $hostDir = Join-Path $sub "host"
    $outDir = Join-Path $sub "out"
    Copy-Sources (Join-Path $case.FullName "host") $hostDir
    Copy-Sources $common $hostDir
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $scripts = Join-Path $case.FullName "scripts"
    if (Test-Path $scripts) { Copy-Item $scripts (Join-Path $outDir "scripts") -Recurse -Force }

    $exe = Join-Path $outDir "host.exe"
    $buildArgs = @($hostDir, "-o", $exe, "--script-host")
    if ($O2) { $buildArgs += "-O2" }
    if ($DebugScripts) { $buildArgs += "-g" }
    $flagsFile = Join-Path $case.FullName "flags.txt"
    if (Test-Path $flagsFile) { $buildArgs += @((Get-Content $flagsFile -Raw).Split(@(" ", "`t", "`r", "`n"), [StringSplitOptions]::RemoveEmptyEntries)) }
    $build = & $compiler @buildArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("  FAIL (build)  " + $case.Name) -ForegroundColor Red
        $build | Select-Object -First 30 | ForEach-Object { Write-Host ("      " + $_) }
        $failed++
        continue
    }

    $log = Join-Path $sub "run.log"
    $errLog = Join-Path $sub "run.err.log"
    $process = Start-Process -FilePath $exe -ArgumentList $mode -WorkingDirectory $outDir -NoNewWindow -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError $errLog
    $null = $process.Handle # keeps the exit code readable after the wait
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        Write-Host ("  FAIL (timeout " + $TimeoutSeconds + "s)  " + $case.Name) -ForegroundColor Red
        Get-Content $log, $errLog -ErrorAction SilentlyContinue | Select-Object -Last 30 | ForEach-Object { Write-Host ("      " + $_) }
        $failed++
        continue
    }
    if ($process.ExitCode -ne 0) {
        Write-Host ("  FAIL (exit " + $process.ExitCode + ")  " + $case.Name) -ForegroundColor Red
        Get-Content $log, $errLog -ErrorAction SilentlyContinue | Select-Object -Last 40 | ForEach-Object { Write-Host ("      " + $_) }
        $failed++
    } else {
        Write-Host ("  PASS  " + $case.Name) -ForegroundColor Green
        $passed++
    }
}

Write-Host ""
Write-Host ("PASSED: " + $passed + "   FAILED: " + $failed + "   TOTAL: " + ($passed + $failed))
if ($failed -gt 0) { exit 1 }
