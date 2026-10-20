# Compile times (B32): C4.exe on programs of several sizes, at -O0 and -O2, end to end (front end, lowering, the
# optimizer and object, the runtime and the link), the median of 3 builds each, with the phase times of that
# build (--time-phases). The runtime's objects are cached, so a first build is not timed. -Check builds each once
# and requires success (the gate).
param([switch]$Check)
$ErrorActionPreference = "Continue"
$dir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $dir "../..")).Path
$c4   = Join-Path $repo "bin/Development-windows-x64/C4/C4.exe"
$out  = Join-Path $repo "assets/build-output/benchmarks/compile"
New-Item -ItemType Directory -Force $out | Out-Null

$programs = @(
    @{ name = "hello";         source = (Join-Path $dir "hello.cf");             flags = @() },
    @{ name = "bedrock tests"; source = (Join-Path $repo "Bedrock/Tests");       flags = @("-test") },
    @{ name = "TestCode";      source = (Join-Path $repo "assets/TestCode");     flags = @() }
)

# One build: its time in ms, whether it succeeded, and its phase lines ("lower 0.012 s").
function Build($program, [string]$opt) {
    $exe = Join-Path $out ("{0}{1}.exe" -f ($program.name -replace ' ', '_'), $opt)
    Remove-Item $exe -ErrorAction SilentlyContinue
    $buildArgs = @($program.source, "-o", $exe, "--time-phases") + $program.flags
    if ($opt -ne "-O0") { $buildArgs += $opt }
    $script:output = @()
    $ms = (Measure-Command { $script:output = & $c4 @buildArgs 2>&1 | ForEach-Object { [string]$_ } }).TotalMilliseconds
    $phases = ($script:output | Where-Object { $_ -match '^\s+[a-z+]+\s+[\d.]+ s' } | ForEach-Object { ($_.Trim() -replace '\s+\(.*\)$', '') -replace '\s+', ' ' }) -join ", "
    return @{ ms = $ms; ok = (Test-Path $exe); phases = $phases }
}

$failed = $false
foreach ($program in $programs) {
    foreach ($opt in @("-O0", "-O2")) {
        $first = Build $program $opt
        if (-not $first.ok) { Write-Host ("{0} {1}: BUILD FAILED" -f $program.name, $opt); $failed = $true; continue }
        if ($Check) { continue }
        $runs = 1..3 | ForEach-Object { Build $program $opt } | Sort-Object { $_.ms }
        Write-Host ("{0,-14} {1}: {2,6:N0} ms   {3}" -f $program.name, $opt, $runs[1].ms, $runs[1].phases)
    }
}
if ($failed) { exit 1 }
if ($Check) { Write-Host "compile benchmark programs build." }
