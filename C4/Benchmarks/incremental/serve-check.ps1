# Checks a build server's builds against cold builds (docs/compiler/07, R1): the program's sources are copied to a scratch
# folder, then each edit is applied in turn and the program built twice - by a one-shot C4.exe and by one `C4.exe --serve`
# kept across the edits. Both build into the same folder (`build`, each its own moved there while it builds): a build's
# paths are in its output - a binding's file is in the int folder, and where a file's functions go hangs on its path. After
# each, the two builds' objects (named by their bitcode's hash), their script interfaces and their executables (but the
# link's time stamps) must be the same, byte for byte. Each line says how the served build went: "bodies" when it lowered
# only the edited bodies again, "cold" with why when it did not.
#   serve-check.ps1 -Compiler C4.exe -ArgsFile <compile.args (Nuke writes it in the project's int folder)> -Root <the folder
#     its sources are under> -Scratch <folder> -Edits 'file|find|replace~~file|find|replace...' [-Opt O0] [-Extra '-fstack-maps']
param(
	[Parameter(Mandatory)] [string] $Compiler,
	[Parameter(Mandatory)] [string] $ArgsFile,
	[Parameter(Mandatory)] [string] $Root,
	[Parameter(Mandatory)] [string] $Scratch,
	[Parameter(Mandatory)] [string] $Edits, # edits in turn, between ~~ (a script's -File arguments are text)
	[string] $Opt = '',
	[string] $Extra = ''  # more compiler arguments, between spaces
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
		if ($Opt -and $line -match '^-O\d$') { $result += "-$Opt"; continue }
		$result += $line
	}
	if ($Extra) { $result += $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries) }
	return $result + '-ftime'
}

# A spec's \n is a line break and \t a tab (a command line holds neither).
function Edit([string] $spec, [string] $snapshot) {
	$file, $find, $replace = $spec.Replace('\n', "`n").Replace('\t', "`t").Split('|')
	$path = "$snapshot/$file"
	$text = [IO.File]::ReadAllText($path)
	if (-not $text.Contains($find)) { throw "$file has no '$find'" }
	[IO.File]::WriteAllText($path, $text.Replace($find, $replace))
}

# The executable's hash without what differs between two links of the same objects into folders of the same length:
# the link's time stamp (the COFF header's and the debug directory's copies of it), and the PDB's signature, age and path.
Add-Type -TypeDefinition @'
public static class LinkStamps
{
	public static void Clear(byte[] bytes, int stamp)
	{
		for (int i = 0; i + 4 <= bytes.Length; i += 4)
			if (System.BitConverter.ToInt32(bytes, i) == stamp) System.Array.Clear(bytes, i, 4);
	}
}
'@

function Linked-Hash([string] $exe) {
	$bytes = [IO.File]::ReadAllBytes($exe)
	$pe = [BitConverter]::ToInt32($bytes, 0x3c)
	[LinkStamps]::Clear($bytes, [BitConverter]::ToInt32($bytes, $pe + 8))
	$text = [Text.Encoding]::ASCII.GetString($bytes)
	$rsds = $text.IndexOf('RSDS')
	while ($rsds -ge 0) {
		$end = $text.IndexOf([char]0, $rsds + 24)
		[Array]::Clear($bytes, $rsds + 4, $end - $rsds - 4)
		$rsds = $text.IndexOf('RSDS', $rsds + 4)
	}
	return [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($bytes))
}

function Objects([string] $int) {
	return (Get-ChildItem "$int/program.objects" -Filter '*.obj' | ForEach-Object { $_.Name } | Sort-Object) -join ' '
}

function Same-Builds([string] $cold, [string] $served) {
	$problems = @()
	if ((Objects "$cold/int") -ne (Objects "$served/int")) { $problems += 'objects differ' }
	$coldInterface = "$cold/out/program.c4i"
	if ((Test-Path $coldInterface) -and ((Get-FileHash $coldInterface).Hash -ne (Get-FileHash "$served/out/program.c4i").Hash)) { $problems += 'interfaces differ' }
	if ((Linked-Hash "$cold/out/program.exe") -ne (Linked-Hash "$served/out/program.exe")) { $problems += 'executables differ' }
	return $problems
}

# A folder a scanner has just opened a file of cannot be renamed for a moment.
function Rename-Folder([string] $from, [string] $to) {
	for ($try = 1; ; $try++) {
		try { Rename-Item "$Scratch/$from" $to -ErrorAction Stop; return }
		catch { if ($try -ge 50) { throw } ; Start-Sleep -Milliseconds 100 }
	}
}

# The cold build's folder is `build` while it builds; the server's waits beside it (the server is idle then).
function Build-Cold([string[]] $arguments) {
	Rename-Folder 'build' 'served'
	Rename-Folder 'cold' 'build'
	try { return & $Compiler @arguments 2>&1 | Out-String }
	finally {
		Rename-Folder 'build' 'cold'
		Rename-Folder 'served' 'build'
	}
}

if (Test-Path $Scratch) { Remove-Item $Scratch -Recurse -Force }
$Scratch = (New-Item -ItemType Directory $Scratch -Force).FullName.Replace('\', '/')
$snapshot = "$Scratch/src"
$projectsIn = (Get-Content $ArgsFile | Select-String -Pattern '^--projects$' -Context 0, 1).Context.PostContext[0]
New-Item -ItemType Directory "$Scratch/build/int", "$Scratch/build/out", "$Scratch/cold/int", "$Scratch/cold/out" -Force | Out-Null
$projects = "$Scratch/projects.toml"
Set-Content $projects (Copy-Sources $projectsIn $snapshot) -NoNewline
$buildArgs = Compile-Args $projects "$Scratch/build/int" "$Scratch/build/out/program.exe"

$info = New-Object Diagnostics.ProcessStartInfo $Compiler
$info.Arguments = (($buildArgs + '--serve') | ForEach-Object { '"' + $_ + '"' }) -join ' '
$info.UseShellExecute = $false
$info.RedirectStandardInput = $true
$info.RedirectStandardOutput = $true
$server = [Diagnostics.Process]::Start($info)
$failures = 0
foreach ($step in @('(none)') + ($Edits -split '~~')) {
	if ($step -ne '(none)') { Edit $step $snapshot }
	$watch = [Diagnostics.Stopwatch]::StartNew()
	$coldOutput = Build-Cold $buildArgs
	$coldSeconds = $watch.Elapsed.TotalSeconds
	if ($LASTEXITCODE -ne 0) { Write-Output $coldOutput; throw "the cold build after '$step' failed" }
	$watch.Restart()
	$server.StandardInput.WriteLine('build')
	$answer = @()
	while ($true) {
		$line = $server.StandardOutput.ReadLine()
		if ($null -eq $line) { throw 'the server ended' }
		if ($line -match '^done ') { break }
		$answer += $line
	}
	$servedSeconds = $watch.Elapsed.TotalSeconds
	if ($line -ne 'done 0') { Write-Output ($answer -join "`n"); throw "the served build after '$step' answered '$line'" }
	$how = if ($answer -match 'bodies again') { 'bodies' } else { 'cold' }
	$why = ($answer | Where-Object { $_ -match '^serve: ' }) -join '; '
	$problems = Same-Builds "$Scratch/cold" "$Scratch/build"
	if ($problems) { $failures++ }
	Write-Output ("{0,-7} cold {1,6:N2} s, served {2,6:N2} s  {3}  {4}  {5}" -f $how, $coldSeconds, $servedSeconds, ($(if ($problems) { 'DIFFERENT: ' + ($problems -join ', ') } else { 'same' })), $step, $why)
}
$server.Refresh()
Write-Output ("server memory between builds: {0:N1} GB private, {1:N1} GB working set (peak {2:N1} GB)" -f ($server.PrivateMemorySize64 / 1GB), ($server.WorkingSet64 / 1GB), ($server.PeakWorkingSet64 / 1GB))
$server.StandardInput.WriteLine('quit')
$server.WaitForExit(60000) | Out-Null
if ($failures) { throw "$failures served build(s) differ from the cold build" }
