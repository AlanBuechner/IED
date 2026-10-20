# Bedrock Math benchmark, two suites timed in process:
# - per-vector: seven per-frame kernels (transform building, matrix products and inverses, point transforms,
#   quaternion rotation and slerp, normalized crosses). C4 builds per-vector/math.cf at -O2; the C++ side builds math.cpp with
#   the same clang and flags, once as plain GLM and once with GLM's SSE paths (GLM_FORCE_INTRINSICS over aligned
#   types). -March <cpu> builds all three for that processor (default: the baseline x86-64).
# - batch: six kernels over eight vectors at a time (transforms, also through pointers held in a struct, rotations,
#   normalized crosses, a particle step, distances), always for x86-64-v3 (AVX2, FMA). C4 builds batch/batch.cf over Bedrock's FVector3x8; batch/batch.cpp is
#   built as per-vector GLM loops and as hand-written AVX2 intrinsics. Each suite has a folder of its own: C4.exe
#   compiles a file with every source under its folder.
# -Check builds every program and compares their checksums (the gate); without it each program runs 5 times and the
# median milliseconds per kernel are reported.
param([switch]$Check, [string]$March = "")
$ErrorActionPreference = "Continue"
$dir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo  = (Resolve-Path (Join-Path $dir "../..")).Path
$c4    = Join-Path $repo "bin/Development-windows-x64/C4/C4.exe"
$clang = Join-Path $repo "Frost/vendor/Compiler/bin/clang.exe"
$out   = Join-Path $repo "assets/build-output/benchmarks/math"
New-Item -ItemType Directory -Force $out | Out-Null
# Kernels whose results differ only by float rounding between the libraries agree to this relative tolerance.
$tolerance = 1e-4
$runs = 5

$perVectorFlags = @(); if ($March) { $perVectorFlags = @("-march=$March") }
$batchFlags = @("-march=x86-64-v3")
$suites = @(
    [ordered]@{ name = "per-vector"; kernels = 7; c4Source = "per-vector/math.cf"; cppSource = "per-vector/math.cpp"; flags = $perVectorFlags
        cpp = [ordered]@{ "GLM" = @(); "GLM-SIMD" = @("-DGLM_FORCE_INTRINSICS", "-DGLM_FORCE_DEFAULT_ALIGNED_GENTYPES") } },
    [ordered]@{ name = "batch"; kernels = 6; c4Source = "batch/batch.cf"; cppSource = "batch/batch.cpp"; flags = $batchFlags
        cpp = [ordered]@{ "GLM" = @(); "AVX2" = @("-DBATCH_AVX2") } }
)

# --- build: suite -> program name -> exe, or $null when a build failed ---
function Build-Suite($suite) {
    $programs = [ordered]@{}
    $exe = Join-Path $out ("{0}_c4.exe" -f $suite.name)
    Remove-Item $exe -ErrorAction SilentlyContinue
    $log = Join-Path $out ("{0}_c4.log" -f $suite.name)
    & $c4 (Join-Path $dir $suite.c4Source) -o $exe "-O2" @($suite.flags) *> $log
    if (-not (Test-Path $exe)) { Write-Host "$($suite.name) C4 build failed:"; Get-Content $log | Select-Object -Last 15 | ForEach-Object { Write-Host $_ }; return $null }
    $programs["C4"] = $exe
    foreach ($name in $suite.cpp.Keys) {
        $exe = Join-Path $out ("{0}_{1}.exe" -f $suite.name, $name.ToLower())
        Remove-Item $exe -ErrorAction SilentlyContinue
        $log = Join-Path $out ("{0}_{1}.log" -f $suite.name, $name.ToLower())
        $cppFlags = @("-target", "x86_64-pc-windows-msvc", "-mstackrealign", "-fexceptions", "-O2", "-std=c++17") + @($suite.flags) + @($suite.cpp[$name]) +
            @((Join-Path $dir $suite.cppSource), "-Wl,/SUBSYSTEM:CONSOLE", "-o", $exe)
        & $clang @cppFlags 2> $log
        if (-not (Test-Path $exe)) { Write-Host "$($suite.name) $name build failed:"; Get-Content $log | Select-Object -Last 15 | ForEach-Object { Write-Host $_ }; return $null }
        $programs[$name] = $exe
    }
    return $programs
}

# --- one run: kernel -> (checksum, ms) ---
function Run([string]$exe) {
    $rows = @{}
    & $exe | ForEach-Object {
        $parts = ($_ -split "\s+") | Where-Object { $_ }
        if ($parts.Count -eq 3) { $rows[$parts[0]] = @([double]$parts[1], [double]$parts[2]) }
    }
    return $rows
}

# --- every program's checksums match C4's; returns the kernel names, or $null on a mismatch ---
function Check-Suite($suite, $programs) {
    $first = [ordered]@{}
    foreach ($name in $programs.Keys) { $first[$name] = Run $programs[$name] }
    $kernels = @($first["C4"].Keys | Sort-Object)
    if ($kernels.Count -ne $suite.kernels) { Write-Host "$($suite.name): C4 printed $($kernels.Count) kernels, expected $($suite.kernels)"; return $null }
    $mismatch = $false
    foreach ($k in $kernels) {
        $mine = $first["C4"][$k][0]
        foreach ($name in $suite.cpp.Keys) {
            if (-not $first[$name].ContainsKey($k)) { Write-Host "$($suite.name): $name has no kernel $k"; $mismatch = $true; continue }
            $theirs = $first[$name][$k][0]
            $scale = [math]::Max([math]::Abs($theirs), 1.0)
            if ([math]::Abs($mine - $theirs) / $scale -gt $tolerance) {
                Write-Host ("CHECKSUM MISMATCH {0} {1}: C4 {2} vs {3} {4}" -f $suite.name, $k, $mine, $name, $theirs)
                $mismatch = $true
            }
        }
    }
    if ($mismatch) { return $null }
    return $kernels
}

# --- 5 runs each, the median ms per kernel, as a table with C4's ratio to each C++ build ---
function Median([double[]]$v) { $s = $v | Sort-Object; return $s[[int]($s.Count / 2)] }

function Time-Suite($suite, $programs, $kernels) {
    $times = @{}
    foreach ($name in $programs.Keys) {
        $samples = @{}
        foreach ($k in $kernels) { $samples[$k] = New-Object System.Collections.Generic.List[double] }
        for ($r = 0; $r -lt $runs; ++$r) {
            $rows = Run $programs[$name]
            foreach ($k in $kernels) { $samples[$k].Add($rows[$k][1]) }
        }
        $times[$name] = @{}
        foreach ($k in $kernels) { $times[$name][$k] = Median $samples[$k].ToArray() }
    }
    $others = @($suite.cpp.Keys)
    $header = "{0,-18} {1,10}" -f ("$($suite.name) (ms)"), "C4"
    foreach ($name in $others) { $header += " {0,10}" -f $name }
    foreach ($name in $others) { $header += " {0,9}" -f "C4/$name" }
    Write-Host $header
    foreach ($k in $kernels) {
        $line = "{0,-18} {1,10:N2}" -f $k, $times["C4"][$k]
        foreach ($name in $others) { $line += " {0,10:N2}" -f $times[$name][$k] }
        foreach ($name in $others) { $line += " {0,9:N2}" -f ($times["C4"][$k] / $times[$name][$k]) }
        Write-Host $line
    }
}

$failed = $false
$checked = @()
foreach ($suite in $suites) {
    $programs = Build-Suite $suite
    if (-not $programs) { $failed = $true; continue }
    $kernels = Check-Suite $suite $programs
    if (-not $kernels) { $failed = $true; continue }
    $checked += , @($suite, $programs, $kernels)
}
if ($failed) { exit 1 }
if ($Check) { Write-Host "math benchmarks build and match GLM and the AVX2 intrinsics."; exit 0 }
foreach ($entry in $checked) { Time-Suite $entry[0] $entry[1] $entry[2]; Write-Host "" }
Write-Host "(ratio < 1 means C4 is faster)"
