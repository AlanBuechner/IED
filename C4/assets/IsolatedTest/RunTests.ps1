# IsolatedTest runner - compiles + runs EVERY program under assets/IsolatedTest (one folder = one
# program) and reports per-folder results. This is the gate that catches regressions in the isolated
# feature tests, which are NOT part of the monolithic TestCode suite.
#
# Classification:
#   - A folder listed in NegativeTests.json is EXPECTED to fail compilation; the runner additionally
#     requires the diagnostic to contain one of its "expect" substrings (a clear error, not a crash). An
#     entry may give "compileFlags" (-Werror makes a warning the expected error).
#   - A folder listed in SkipTests.json is environment-dependent (e.g. needs a GPU) and is reported
#     as SKIPPED, not failed.
#   - A folder listed in ExpectedExit.json must compile and terminate with the recorded exit code
#     (used for programs that are correct precisely because they die: uncaught-exception tests).
#     Every "mustContain" substring must appear in their output and no "mustNotContain" one may. An entry
#     may also give "compileFlags" and "runArgs" ({dir} is the case's build folder), a "jsonFile" the run
#     must write there as valid JSON (the test harness's -test-json summary), and "compileMustContain":
#     substrings the compiler's own output must contain (warnings of a build that succeeds).
#   - A folder listed in ExpectedIR.json must also emit LLVM IR containing every "irContains" substring
#     (for features whose effect is the code itself: volatile accesses, thread_local storage).
#   - Every other folder must compile AND run with exit 0.
#
# Usage:  .\RunTests.ps1 [-Filter "Types"] [-O2] [-DebugInfo] [-Partitions <n>]   (-DebugInfo: every program built with
#         -g; -Partitions: every program assembled into <n> objects, the parallel code generation path)
param(
    [string]$Filter = "",
    [switch]$O2,
    [switch]$DebugInfo,
    [int]$Partitions = 0
)
$ErrorActionPreference = "Continue"

$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $root "..\..")).Path
$compiler = Join-Path $repoRoot "bin/Development-windows-x64/C4/C4.exe"
# Scratch build output lives under assets/build-output (never the repo root, never dot-prefixed).
$work     = Join-Path $repoRoot "assets/build-output/isolated"

if (-not (Test-Path $compiler)) { Write-Host "COMPILER NOT FOUND: $compiler - build it first." -ForegroundColor Red; exit 1 }

function Load-Manifest([string]$path) {
    if (-not (Test-Path $path)) { return @{} }
    $json = Get-Content $path -Raw | ConvertFrom-Json
    $map = @{}
    foreach ($p in $json.PSObject.Properties) {
        $v = $p.Value
        if ($v -is [string]) { $map[$p.Name] = @{ expect = @(); reason = $v; compileFlags = @() } }
        else { $map[$p.Name] = @{ expect = @($v.expect); reason = [string]$v.reason; compileFlags = @($v.compileFlags | Where-Object { $_ }) } }
    }
    return $map
}

$negative = Load-Manifest (Join-Path $root "NegativeTests.json")
$skipped  = Load-Manifest (Join-Path $root "SkipTests.json")

# ExpectedIR.json: substrings the case's IR (an extra -S compile writes out.ll) must contain.
function Load-IRManifest([string]$path) {
    if (-not (Test-Path $path)) { return @{} }
    $json = Get-Content $path -Raw | ConvertFrom-Json
    $map = @{}
    foreach ($p in $json.PSObject.Properties) { $map[$p.Name] = @($p.Value.irContains) }
    return $map
}
$irExpected = Load-IRManifest (Join-Path $root "ExpectedIR.json")

# ExpectedExit.json: programs that must compile and then terminate with a specific non-zero code.
function Load-ExpectedManifest([string]$path) {
    if (-not (Test-Path $path)) { return @{} }
    $json = Get-Content $path -Raw | ConvertFrom-Json
    $map = @{}
    foreach ($p in $json.PSObject.Properties) {
        $v = $p.Value
        $mustNot = @()
        if ($v.mustNotContain) { $mustNot = @($v.mustNotContain) }
        $must = @()
        if ($v.mustContain) { $must = @($v.mustContain) }
        $compileFlags = @()
        if ($v.compileFlags) { $compileFlags = @($v.compileFlags) }
        $runArgs = @()
        if ($v.runArgs) { $runArgs = @($v.runArgs) }
        $compileMust = @()
        if ($v.compileMustContain) { $compileMust = @($v.compileMustContain) }
        $map[$p.Name] = @{ exit = [int]$v.exit; must = $must; mustNot = $mustNot; reason = [string]$v.reason;
            compileFlags = $compileFlags; runArgs = $runArgs; jsonFile = [string]$v.jsonFile; compileMust = $compileMust }
    }
    return $map
}
$expected = Load-ExpectedManifest (Join-Path $root "ExpectedExit.json")

$flags = @()
if ($O2) { $flags += "-O2" }
if ($DebugInfo) { $flags += "-g" }
if ($Partitions -gt 0) { $flags += @("--partitions", "$Partitions") }

# A test program still running after this long is killed and fails its case: a hang (an infinite loop the
# optimizer made of a recursion, a deadlock) must not stall the whole run.
$runTimeoutSeconds = 60

function Invoke-TestProgram([string]$exe, [string[]]$runArgs = @()) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = ($runArgs | ForEach-Object { '"' + $_ + '"' }) -join " "
    $psi.WorkingDirectory = (Get-Location).Path
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEndAsync()
    $stderr = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit($runTimeoutSeconds * 1000)) {
        $proc.Kill()
        $proc.WaitForExit()
        return @{ timedOut = $true; exit = -1; output = "" }
    }
    $proc.WaitForExit()
    return @{ timedOut = $false; exit = $proc.ExitCode; output = $stdout.Result + $stderr.Result }
}

function Timeout-Result([string]$rel) {
    return [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "run timed out after $runTimeoutSeconds s" }
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host ("C4 IsolatedTest Runner" + $(if ($Filter) { " (filter: $Filter)" } else { "" }) + $(if ($O2) { " [-O2]" })) -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$folders = Get-ChildItem $root -Recurse -Directory | Where-Object { (Get-ChildItem $_.FullName -Filter "*.cf").Count -gt 0 }
if ($Filter) { $folders = $folders | Where-Object { $_.Name -like "*$Filter*" -or $_.Parent.Name -eq $Filter } }

$results = @()
foreach ($f in $folders) {
    $rel = $f.FullName.Substring($root.Length + 1).Replace("\", "/")
    $sub = Join-Path $work $rel
    # Fresh build dir per run: a persistent dir would keep stale files from earlier runs (e.g. a
    # .ch that was later deleted from the test folder) and recompile them, failing the test for a
    # reason unrelated to its current source. Tests must not depend on leftover build state.
    if (Test-Path $sub) { Remove-Item $sub -Recurse -Force }
    New-Item -ItemType Directory -Force $sub | Out-Null
    Get-ChildItem $f.FullName -File | Where-Object { $_.Extension -in ".cf",".ch",".c",".cpp",".h",".hpp" } | ForEach-Object { Copy-Item $_.FullName (Join-Path $sub $_.Name) -Force }
    Remove-Item (Join-Path $sub "out.exe") -Force -ErrorAction SilentlyContinue

    $exe = Join-Path $sub "out.exe"
    # A case with [test] functions but no `main` is a test-harness program: build it with -test so the
    # compiler emits the synthetic harness main and the tests actually run (exit 0 = all assertions pass).
    # Without this, such a case has no entry point and now fails to compile. Use a per-case copy of
    # $flags so the -test addition does not leak into later cases.
    $caseFlags = @($flags)
    $caseSrc = (Get-ChildItem $sub -File | Where-Object { $_.Extension -in ".cf", ".ch" } | ForEach-Object { Get-Content $_.FullName -Raw }) -join "`n"
    $hasTests = $caseSrc -match '\[test'
    if ($hasTests -and $caseSrc -match '(?<![\w])main\s*\(') {
        # Under -test the harness main replaces the program's, so a case with both would never run its tests.
        $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "has [test] functions and a main, so its tests would never run: remove main" }
        continue
    }
    if ($hasTests) { $caseFlags += "-test" }
    if ($expected.ContainsKey($rel)) { $caseFlags += @($expected[$rel].compileFlags | ForEach-Object { $_.Replace("{dir}", $sub) }) }
    if ($negative.ContainsKey($rel)) { $caseFlags += @($negative[$rel].compileFlags) }
    $output = & $compiler $sub -o $exe @caseFlags 2>&1 | ForEach-Object { [string]$_ }

    if ($negative.ContainsKey($rel)) {
        $spec = $negative[$rel]
        if ($LASTEXITCODE -eq 0) {
            $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "expected a compile error but it compiled" }
        } elseif ($spec.expect.Count -gt 0) {
            $joined = $output -join "`n"
            $hit = $spec.expect | Where-Object { $joined.Contains($_) } | Select-Object -First 1
            if ($hit) { $results += [pscustomobject]@{ Name = $rel; Status = "PASS"; Detail = "negative (error as expected)" } }
            else { $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "expected error containing '$($spec.expect -join "' or '")' got: $($joined.Trim().Substring(0, [Math]::Min(160, $joined.Trim().Length)))" } }
        } else {
            $results += [pscustomobject]@{ Name = $rel; Status = "PASS"; Detail = "negative (failed as expected)" }
        }
        continue
    }

    if ($skipped.ContainsKey($rel)) {
        $results += [pscustomobject]@{ Name = $rel; Status = "SKIP"; Detail = $skipped[$rel].reason }
        continue
    }

    if ($LASTEXITCODE -ne 0) {
        $err = ($output | Where-Object { $_ -match "error:" } | Select-Object -First 3) -join " | "
        $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "compile: " + $err }
        continue
    }

    if ($irExpected.ContainsKey($rel)) {
        # The compiler writes the IR only when asked (-S stops after it), so the case is compiled once more for it.
        $llPath = [System.IO.Path]::ChangeExtension($exe, ".ll")
        Remove-Item $llPath -ErrorAction SilentlyContinue
        & $compiler $sub -o $exe -S @caseFlags 2>&1 | Out-Null
        $ir = if (Test-Path $llPath) { Get-Content $llPath -Raw } else { "" }
        $absent = $irExpected[$rel] | Where-Object { -not $ir.Contains($_) } | Select-Object -First 1
        if ($absent) {
            $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "the emitted IR must contain '$absent'" }
            continue
        }
    }

    if ($expected.ContainsKey($rel)) {
        $spec = $expected[$rel]
        $run = Invoke-TestProgram $exe @($spec.runArgs | ForEach-Object { $_.Replace("{dir}", $sub) })
        if ($run.timedOut) { $results += Timeout-Result $rel; continue }
        $runOut = $run.output
        $bad = $spec.mustNot | Where-Object { $runOut.Contains($_) } | Select-Object -First 1
        $missing = $spec.must | Where-Object { -not $runOut.Contains($_) } | Select-Object -First 1
        $compileText = $output -join "`n"
        $missingWarning = $spec.compileMust | Where-Object { -not $compileText.Contains($_) } | Select-Object -First 1
        $jsonProblem = ""
        if ($spec.jsonFile) {
            $jsonPath = Join-Path $sub $spec.jsonFile
            if (-not (Test-Path $jsonPath)) { $jsonProblem = "the run did not write $($spec.jsonFile)" }
            else { try { Get-Content $jsonPath -Raw | ConvertFrom-Json | Out-Null } catch { $jsonProblem = "$($spec.jsonFile) is not valid JSON: $_" } }
        }
        if ($run.exit -ne $spec.exit) {
            $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "expected exit $($spec.exit), got $($run.exit)" }
        } elseif ($bad) {
            $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "output must not contain '$bad'" }
        } elseif ($missing) {
            $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "output must contain '$missing'" }
        } elseif ($missingWarning) {
            $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "the compiler's output must contain '$missingWarning'" }
        } elseif ($jsonProblem) {
            $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = $jsonProblem }
        } else {
            $results += [pscustomobject]@{ Name = $rel; Status = "PASS"; Detail = "" }
        }
        continue
    }

    $run = Invoke-TestProgram $exe
    if ($run.timedOut) { $results += Timeout-Result $rel }
    elseif ($run.exit -eq 0) { $results += [pscustomobject]@{ Name = $rel; Status = "PASS"; Detail = "" } }
    else { $results += [pscustomobject]@{ Name = $rel; Status = "FAIL"; Detail = "run exit $($run.exit)" } }
}

Write-Host ""
$pass = @($results | Where-Object { $_.Status -eq "PASS" }).Count
$fail = @($results | Where-Object { $_.Status -eq "FAIL" }).Count
$skip = @($results | Where-Object { $_.Status -eq "SKIP" }).Count
Write-Host ("PASSED: {0}   FAILED: {1}   SKIPPED: {2}   TOTAL: {3}" -f $pass, $fail, $skip, $results.Count) -ForegroundColor $(if ($fail -gt 0) { "Red" } else { "Green" })
Write-Host ""
$results | Where-Object { $_.Status -ne "PASS" } | ForEach-Object { Write-Host ("{0}  {1}" -f $_.Name, $_.Detail) -ForegroundColor Red }

if ($fail -gt 0) { exit 1 }
Write-Host "All isolated tests passed." -ForegroundColor Green
