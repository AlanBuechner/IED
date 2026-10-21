# Bedrock against the C++ standard library: hash maps, arrays, sorting and number text (bedrock.cf, bedrock.cpp).
# Both are built at -O2 with the vendored clang. Every kernel prints its time and a checksum; the checksums must agree.
# -Check builds both and compares the checksums once (the gate); without it each side runs 5 times and the median
# time of every kernel is reported with the C4/C++ ratio (below 1: Bedrock is faster).
param([switch]$Check)
$ErrorActionPreference = "Continue"
$dir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo  = (Resolve-Path (Join-Path $dir "../..")).Path
$c4    = Join-Path $repo "bin/Development-windows-x64/C4/C4.exe"
$clang = Join-Path $repo "Frost/vendor/Compiler/bin/clang.exe"
$out   = Join-Path $repo "assets/build-output/benchmarks/bedrock"
New-Item -ItemType Directory -Force $out | Out-Null
$c4Exe  = Join-Path $out "bedrock_c4.exe"
$cppExe = Join-Path $out "bedrock_cpp.exe"

Remove-Item $c4Exe, $cppExe -ErrorAction SilentlyContinue
& $c4 (Join-Path $dir "bedrock.cf") -o $c4Exe "-O2" --int-dir (Join-Path $out "int") *> (Join-Path $out "c4.log")
if (-not (Test-Path $c4Exe)) { Write-Host "C4 build failed:"; Get-Content (Join-Path $out "c4.log") | Select-Object -Last 15; exit 1 }
& $clang -target x86_64-pc-windows-msvc -std=c++20 -fexceptions -O2 (Join-Path $dir "bedrock.cpp") -o $cppExe "-Wl,/SUBSYSTEM:CONSOLE" 2> (Join-Path $out "cpp.log")
if (-not (Test-Path $cppExe)) { Write-Host "C++ build failed:"; Get-Content (Join-Path $out "cpp.log"); exit 1 }

# kernel -> @(times), and the checksum of each side's first run
function Run([string]$exe, [int]$runs) {
	$times = @{}; $sums = @{}; $order = @()
	for ($r = 0; $r -lt $runs; $r++) {
		foreach ($line in (& $exe)) {
			$parts = $line -split ' '
			if ($parts.Count -ne 3) { continue }
			if (-not $times.ContainsKey($parts[0])) { $times[$parts[0]] = @(); $sums[$parts[0]] = $parts[2]; $order += $parts[0] }
			$times[$parts[0]] += [double]$parts[1]
		}
	}
	return @{ times = $times; sums = $sums; order = $order }
}
function Median([double[]]$v) { $s = $v | Sort-Object; return $s[[int][math]::Floor($s.Count / 2)] }

$runs = if ($Check) { 1 } else { 5 }
$c4Run = Run $c4Exe $runs
$cppRun = Run $cppExe $runs
$failed = 0
foreach ($kernel in $c4Run.order) {
	if ($c4Run.sums[$kernel] -ne $cppRun.sums[$kernel]) {
		Write-Host ("CHECKSUM MISMATCH {0}: c4={1} cpp={2}" -f $kernel, $c4Run.sums[$kernel], $cppRun.sums[$kernel]); $failed++
	}
}
if ($c4Run.order.Count -eq 0) { Write-Host "the C4 benchmark printed no kernels"; exit 1 }
if ($failed -gt 0) { exit 1 }
if ($Check) { Write-Host ("Bedrock benchmark: {0} kernels build and match C++." -f $c4Run.order.Count); exit 0 }

Write-Host ("{0,-20} {1,10} {2,10} {3,8}" -f "kernel", "C4 ms", "C++ ms", "C4/C++")
foreach ($kernel in $c4Run.order) {
	$a = Median $c4Run.times[$kernel]; $b = Median $cppRun.times[$kernel]
	$ratio = if ($b -gt 0) { [math]::Round($a / $b, 2) } else { 0 }
	Write-Host ("{0,-20} {1,10:N2} {2,10:N2} {3,8}" -f $kernel, $a, $b, $ratio)
}
