# IED one-command installer (Windows x64). Clones (or updates) the repository, runs setup, builds everything and
# installs IED. Safe to re-run: it pulls, then continues where the last run stopped.
#
#   irm https://raw.githubusercontent.com/AlanBuechner/IED/main/install.ps1 | iex
#
# Environment variables (set before the command): IED_DIR = where to clone (default %USERPROFILE%\IED),
# IED_BRANCH = branch to install (default main), IED_REPO = repository URL.
$ErrorActionPreference = 'Stop'

$repo = if ($env:IED_REPO) { $env:IED_REPO } else { 'https://github.com/AlanBuechner/IED.git' }
$branch = if ($env:IED_BRANCH) { $env:IED_BRANCH } else { 'main' }
$dir = if ($env:IED_DIR) { $env:IED_DIR } else { Join-Path $env:USERPROFILE 'IED' }

function Say($text) { Write-Host "[install] $text" -ForegroundColor Cyan }

function Run([string]$what, [scriptblock]$command) {
	& $command
	if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" }
}

# The prebuilt LLVM the C4 compiler links needs a recent MSVC C++ runtime: older toolsets lack symbols such as
# __std_find_first_of_trivial_pos_1 and fail at link time. Returns the newest toolset folder, or $null.
function NewestMsvcToolset {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path $vswhere)) { return $null }
	$installs = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	$toolsets = foreach ($install in $installs) {
		Get-ChildItem (Join-Path $install 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue
	}
	return $toolsets | Sort-Object { [version]$_.Name } | Select-Object -Last 1
}

function RuntimeHasRecentStl([string]$toolset) {
	$library = Join-Path $toolset 'lib\x64\msvcprt.lib'
	if (-not (Test-Path $library)) { return $false }
	$text = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($library))
	return $text.Contains('__std_find_first_of_trivial_pos_1')
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
	throw 'git is required: install it from https://git-scm.com/download/win and run this again.'
}
$toolset = NewestMsvcToolset
if (-not $toolset) {
	throw 'The Visual Studio C++ build tools (MSVC x64 and the Windows SDK) are required: https://visualstudio.microsoft.com/visual-cpp-build-tools/ - then run this again.'
}
if (-not (RuntimeHasRecentStl $toolset.FullName)) {
	throw "The newest MSVC toolset here ($($toolset.Name)) is too old for the prebuilt LLVM (its C++ runtime lacks __std_find_first_of_trivial_pos_1). Update Visual Studio (or the Build Tools) to the latest release, or install the newest 'MSVC v14x build tools' component in the Visual Studio Installer, then run this again."
}

if (Test-Path (Join-Path $dir '.git')) {
	Say "updating $dir"
	Run 'git fetch' { git -C $dir fetch origin $branch }
	Run 'git checkout' { git -C $dir checkout $branch }
	Run 'git pull' { git -C $dir pull --ff-only origin $branch }
} else {
	Say "cloning $repo into $dir"
	Run 'git clone' { git clone --branch $branch $repo $dir }
}

Set-Location $dir

Say 'setup: private Python, submodules, LLVM, web libraries, and the C4 compiler (the first build takes a while)'
Run 'setup' { cmd /c setup.bat --toolchain --add-to-path }

Say 'building everything'
Run 'nuke build' { cmd /c nuke.bat build }

Say 'installing IED'
Run 'nuke install-ied' { cmd /c nuke.bat install-ied }

Say 'done. Open a NEW terminal and run `ied <folder>`, or start IED from the Start menu.'
