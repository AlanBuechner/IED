# Negative e2e test runner - compiles EVERY program under assets/NegativeTest and asserts that it
# FAILS with the specific, clear diagnostic recorded in NegativeTests.json. This is the gate that
# catches regressions in error reporting: a case that suddenly compiles (error swallowed) or whose
# message drifted (no longer actionable) both fail here.
#
# One folder = one invalid program. Every folder MUST be listed in NegativeTests.json with an
# "expect" substring; an unregistered folder is a FAIL so the manifest stays complete by construction.
# Expect substrings match literally (brackets, quotes and ? included).
#
# Usage:  .\RunTests.ps1 [-Filter "Identifier"] [-O2]
param(
    [string]$Filter = "",
    [switch]$O2
)
$ErrorActionPreference = "Continue"

$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $root "..\..")).Path
$compiler = Join-Path $repoRoot "bin/Development-windows-x64/C4/C4.exe"
$work     = Join-Path $repoRoot "assets/build-output/negative"

if (-not (Test-Path $compiler)) { Write-Host "COMPILER NOT FOUND: $compiler - build it first." -ForegroundColor Red; exit 1 }

$json = Get-Content (Join-Path $root "NegativeTests.json") -Raw | ConvertFrom-Json
$manifest = @{}
foreach ($p in $json.PSObject.Properties) {
    $v = $p.Value
    if ($v -is [string]) { $manifest[$p.Name] = @{ expect = @(); reason = $v } }
    else { $manifest[$p.Name] = @{ expect = @($v.expect); reason = [string]$v.reason } }
}

$flags = @()
if ($O2) { $flags += "-O2" }

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "C4 Negative Test Runner" + $(if ($Filter) { "(filter: $Filter)" } else { "" }) + $(if ($O2) { " [-O2]" }) -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$folders = Get-ChildItem $root -Recurse -Directory | Where-Object { (Get-ChildItem $_.FullName -Filter "*.cf").Count -gt 0 }
if ($Filter) { $folders = $folders | Where-Object { $_.Name -like "*$Filter*" -or $_.Parent.Name -eq $Filter } }

$results = @()
foreach ($f in $folders) {
    $rel = $f.FullName.Substring($root.Length + 1).Replace("\", "/")
    $sub = Join-Path $work $rel
    # Fresh build dir per run: a persistent dir would keep stale files from earlier runs and
    # recompile them, failing the test for a reason unrelated to its current source.
    if (Test-Path $sub) { Remove-Item $sub -Recurse -Force }
    New-Item -ItemType Directory -Force $sub | Out-Null
    Get-ChildItem $f.FullName -File | Where-Object { $_.Extension -in ".cf",".ch",".c",".cpp",".h",".hpp" } | ForEach-Object { Copy-Item $_.FullName (Join-Path $sub $_.Name) -Force }
    Remove-Item (Join-Path $sub "out.exe") -Force -ErrorAction SilentlyContinue

    $exe = Join-Path $sub "out.exe"
    $output = & $compiler $sub -o $exe @flags 2>&1 | ForEach-Object { [string]$_ }

    if (-not $manifest.ContainsKey($rel)) {
        $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "NOT REGISTERED in NegativeTests.json (add an expect substring)" }
        continue
    }
    $spec = $manifest[$rel]

    if ($LASTEXITCODE -eq 0) {
        $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "expected a compile error but it COMPILED (error was swallowed)" }
        continue
    }
    if ($spec.expect.Count -gt 0) {
        $joined = $output -join "`n"
        $hit = $spec.expect | Where-Object { $joined.Contains($_) } | Select-Object -First 1
        if ($hit) { $results += [pscustomobject]@{ Name = $rel; Status = "PASS"; Detail = "clear error: '$hit'" } }
        else {
            $want = ($spec.expect -join " / ")
            $got = $joined.Trim().Substring(0, [Math]::Min(160, $joined.Trim().Length))
            $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "error missing expected substring [$want]; got: $got" }
        }
    } else {
        $results += [pscustomobject]@{ Name = $rel; Status = "PASS"; Detail = "failed as expected (no substring asserted)" }
    }
}

Write-Host ""
$pass = @($results | Where-Object { $_.Status -eq "PASS" }).Count
$fail = @($results | Where-Object { $_.Status -eq "FAIL" }).Count
Write-Host ("PASSED: {0}   FAILED: {1}   TOTAL: {2}" -f $pass, $fail, $results.Count) -ForegroundColor $(if ($fail -gt 0) { "Red" } else { "Green" })
Write-Host ""
$results | ForEach-Object {
    $color = if ($_.Status -eq "PASS") { "Green" } else { "Red" }
    Write-Host ("{0,-32} {1}" -f $_.Name, $_.Detail) -ForegroundColor $color
}

if ($fail -gt 0) { exit 1 }
Write-Host "All negative tests passed." -ForegroundColor Green
