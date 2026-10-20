# C4 vs C++ generated-code benchmark (integer GEMM, 512^3).
# Both are compiled with the vendored clang at -O2 for x86_64-pc-windows-msvc: C4 through its own
# pipeline, C++ directly. -Check builds both and compares their results (the gate); without it the
# runs are timed externally (Measure-Command), end to end.
param([switch]$Check)
$ErrorActionPreference = "Continue"
$dir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo  = (Resolve-Path (Join-Path $dir "../..")).Path
$c4    = Join-Path $repo "bin/Development-windows-x64/C4/C4.exe"
$clang = Join-Path $repo "Frost/vendor/Compiler/bin/clang.exe"
$out   = Join-Path $repo "assets/build-output/benchmarks/gemm"
New-Item -ItemType Directory -Force $out | Out-Null
$c4Exe  = Join-Path $out "gemm_c4.exe"
$cppExe = Join-Path $out "gemm_cpp.exe"

# --- build the C4 side (from its own source file, so the folder's .cpp is not compiled) ---
Remove-Item $c4Exe, $cppExe -ErrorAction SilentlyContinue
& $c4 (Join-Path $dir "gemm.cf") -o $c4Exe "-O2" *> (Join-Path $out "c4.log")
if (-not (Test-Path $c4Exe)) { Write-Host "C4 build failed:"; Get-Content (Join-Path $out "c4.log") | Select-Object -Last 15; exit 1 }

# --- build the C++ side with the SAME clang and flags C4 uses for codegen and linking ---
& $clang -target x86_64-pc-windows-msvc -mstackrealign -fexceptions -O2 (Join-Path $dir "gemm.cpp") -o $cppExe "-Wl,/SUBSYSTEM:CONSOLE" 2> (Join-Path $out "cpp.log")
if (-not (Test-Path $cppExe)) { Write-Host "C++ build failed:"; Get-Content (Join-Path $out "cpp.log"); exit 1 }

# --- the two must compute the same result ---
$r1 = & $c4Exe; $r2 = & $cppExe
Write-Host ("result c4='{0}' cpp='{1}'" -f $r1, $r2)
if ($r1 -ne $r2) { Write-Host "RESULT MISMATCH"; exit 1 }
if ($Check) { Write-Host "gemm benchmark builds and matches C++."; exit 0 }

# --- time: 7 runs each, report the median ms ---
function Median([double[]]$v) { $s = $v | Sort-Object; return $s[[int]($s.Count/2)] }
$runs = 7
$c4t  = 1..$runs | ForEach-Object { (Measure-Command { & $c4Exe  *> $null }).TotalMilliseconds }
$cppT = 1..$runs | ForEach-Object { (Measure-Command { & $cppExe *> $null }).TotalMilliseconds }
$c4med  = [math]::Round((Median $c4t), 0)
$cppMed = [math]::Round((Median $cppT), 0)
Write-Host ("C4   median: {0} ms  (runs: {1})" -f $c4med, (($c4t | ForEach-Object {[int]$_}) -join ', '))
Write-Host ("C++  median: {0} ms  (runs: {1})" -f $cppMed, (($cppT | ForEach-Object {[int]$_}) -join ', '))
$ratio = [math]::Round($c4med / $cppMed, 3)
Write-Host ("C4/C++ ratio: {0} (1.0 = parity, <1 = C4 faster)" -f $ratio)
