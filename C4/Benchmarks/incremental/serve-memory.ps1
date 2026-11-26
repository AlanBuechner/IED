# Measures a C4 build server's memory across builds: one `C4.exe --serve` process (requests on stdin) builds a copy of a
# program's sources after each edit in turn; after each answer it samples the server's private commit (and its peak) and
# prints -ftime's `release` (the build before let go ahead of a cold build). docs/compiler/07 "The server's memory".
#   serve-memory.ps1 -Compiler C4.exe -ArgsFile <compile.args> -Root <folder the sources are under> -Scratch <folder>
#     -Body 'file|find|replace' -Signature 'file|find|replace' [-Rounds 3]
param(
	[Parameter(Mandatory)] [string] $Compiler,
	[Parameter(Mandatory)] [string] $ArgsFile,
	[Parameter(Mandatory)] [string] $Root,
	[Parameter(Mandatory)] [string] $Scratch,
	[Parameter(Mandatory)] [string] $Body,
	[Parameter(Mandatory)] [string] $Signature,
	[int] $Rounds = 3,
	[double] $Settle = 3
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path.Replace('\', '/').TrimEnd('/')

function Copy-Sources([string] $projectsFile, [string] $snapshot) {
	$text = Get-Content $projectsFile -Raw
	$folders = [regex]::Matches($text, '"' + [regex]::Escape($Root) + '/([^"]*)"') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
	foreach ($folder in $folders) {
		$from = "$Root/$folder"
		if (Test-Path $from -PathType Container) {
			robocopy $from "$snapshot/$folder" /E /NFL /NDL /NJH /NJS /NP /XD build .git node_modules | Out-Null
		}
	}
	return $text.Replace("$Root/", "$snapshot/")
}

function Compile-Args([string] $projects, [string] $int, [string] $out) {
	$lines = Get-Content $ArgsFile | Select-Object -Skip 1
	$result = @()
	for ($i = 0; $i -lt $lines.Count; $i++) {
		$line = $lines[$i]
		if ($line -eq '--projects') { $result += $line, $projects; $i++; continue }
		if ($line -eq '--int-dir') { $result += $line, $int; $i++; continue }
		if ($line -eq '-o') { $result += $line, $out; $i++; continue }
		$result += $line
	}
	return $result + '-ftime' + '--serve'
}

function Edit([string] $spec, [string] $snapshot, [bool] $undo) {
	$file, $find, $replace = $spec.Split('|')
	if ($undo) { $find, $replace = $replace, $find }
	$path = "$snapshot/$file"
	$text = [IO.File]::ReadAllText($path)
	if (-not $text.Contains($find)) { throw "$file has no '$find'" }
	[IO.File]::WriteAllText($path, $text.Replace($find, $replace))
}

if (Test-Path $Scratch) { Remove-Item $Scratch -Recurse -Force }
$snapshot = "$Scratch/src".Replace('\', '/')
$projectsIn = (Get-Content $ArgsFile | Select-String -Pattern '^--projects$' -Context 0, 1).Context.PostContext[0]
New-Item -ItemType Directory "$Scratch/int", "$Scratch/out" -Force | Out-Null
$projects = "$Scratch/projects.toml"
Set-Content $projects (Copy-Sources $projectsIn $snapshot) -NoNewline
$compileArgs = Compile-Args $projects "$Scratch/int" "$Scratch/out/program.exe"

$info = New-Object System.Diagnostics.ProcessStartInfo
$info.FileName = $Compiler
$info.Arguments = ($compileArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
$info.UseShellExecute = $false
$info.RedirectStandardInput = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.WorkingDirectory = $snapshot
$server = [System.Diagnostics.Process]::Start($info)
$server.BeginErrorReadLine()

function Serve([string] $name) {
	$watch = [Diagnostics.Stopwatch]::StartNew()
	$script:release = ''
	$server.StandardInput.WriteLine('build')
	$served = $false
	while ($true) {
		$line = $server.StandardOutput.ReadLine()
		if ($null -eq $line) { throw "the server ended during '$name'" }
		if ($line -match 'bodies again') { $served = $true }
		if ($line -match '^\s*release\b') { $script:release = $line.Trim() }
		if ($line -match '^done (\d+)') { $code = [int]$Matches[1]; break }
	}
	$watch.Stop()
	Start-Sleep -Seconds $Settle
	$server.Refresh()
	'{0,-14} code {1}  {2,6:N2} s  {3,-6}  commit {4,5:N2} GB  peak {5,5:N2} GB  ws {6,5:N2} GB  {7}' -f $name, $code, $watch.Elapsed.TotalSeconds,
		$(if ($served) { 'served' } else { 'cold' }), ($server.PagedMemorySize64 / 1GB), ($server.PeakPagedMemorySize64 / 1GB), ($server.WorkingSet64 / 1GB), $script:release
}

try {
	Serve 'first'
	Serve 'unchanged'
	for ($round = 1; $round -le $Rounds; $round++) {
		Edit $Body $snapshot $false; Serve "body $round"
		Edit $Body $snapshot $true; Serve "body undo $round"
		Edit $Signature $snapshot $false; Serve "sig $round"
		Edit $Signature $snapshot $true; Serve "sig undo $round"
	}
	$server.StandardInput.WriteLine('quit')
	if (-not $server.WaitForExit(60000)) { $server.Kill() }
} finally {
	if (-not $server.HasExited) { $server.Kill() }
}
