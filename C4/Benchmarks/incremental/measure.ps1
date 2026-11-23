# Measures a program's rebuilds after edits (roadmap 5.3, R1): cold, nothing changed, a statement edit, a header
# declaration edit. The program's sources are copied to a scratch folder first, so the edits never touch the real tree.
#   measure.ps1 -Compiler C4.exe -ArgsFile <compile.args (Nuke writes it in the project's int folder)> -Root <the folder its sources are under> -Scratch <folder>
#     -Statement 'file|find|replace' -Header 'file|find|replace' [-Opt O0] [-Extra '--build-server ...']
param(
	[Parameter(Mandatory)] [string] $Compiler,
	[Parameter(Mandatory)] [string] $ArgsFile,
	[Parameter(Mandatory)] [string] $Root,
	[Parameter(Mandatory)] [string] $Scratch,
	[Parameter(Mandatory)] [string] $Statement,
	[Parameter(Mandatory)] [string] $Header,
	[string] $Opt = 'O0',
	[string] $Extra = ''
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
		if ($line -match '^-O\d$') { $result += "-$Opt"; continue }
		$result += $line
	}
	return $result + '-ftime' + ($Extra -split ' ' | Where-Object { $_ })
}

function Edit([string] $spec, [string] $snapshot) {
	$file, $find, $replace = $spec.Split('|')
	$path = "$snapshot/$file"
	$text = [IO.File]::ReadAllText($path)
	if (-not $text.Contains($find)) { throw "$file has no '$find'" }
	[IO.File]::WriteAllText($path, $text.Replace($find, $replace))
}

function Build([string] $name, [string[]] $compileArgs) {
	$watch = [Diagnostics.Stopwatch]::StartNew()
	$output = & $Compiler @compileArgs 2>&1 | Out-String
	$watch.Stop()
	if ($LASTEXITCODE -ne 0) { Write-Output $output; throw "$name failed ($LASTEXITCODE)" }
	$phases = ($output -split "`n" | Where-Object { $_ -match '^\s*(register|preprocess|parse|model|lower|optimize|codegen|inline|interface|runtime|script|total|front)' }) -join "`n"
	Write-Output ("{0,-10} {1,6:N2} s" -f $name, $watch.Elapsed.TotalSeconds)
	Write-Output $phases
	Write-Output ''
}

if (Test-Path $Scratch) { Remove-Item $Scratch -Recurse -Force }
$snapshot = "$Scratch/src".Replace('\', '/')
$projectsIn = (Get-Content $ArgsFile | Select-String -Pattern '^--projects$' -Context 0, 1).Context.PostContext[0]
New-Item -ItemType Directory "$Scratch/int", "$Scratch/out" -Force | Out-Null
$projects = "$Scratch/projects.toml"
Set-Content $projects (Copy-Sources $projectsIn $snapshot) -NoNewline
$compileArgs = Compile-Args $projects "$Scratch/int" "$Scratch/out/program.exe"

Build 'cold' $compileArgs
Build 'unchanged' $compileArgs
Edit $Statement $snapshot
Build 'statement' $compileArgs
Edit $Header $snapshot
Build 'header' $compileArgs
