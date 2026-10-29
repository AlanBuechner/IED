# IED one-command installer (Windows 10/11 x64). Needs nothing installed first: it gets git when the machine has none,
# clones (or updates) the repository, and setup downloads everything else IED builds with - a private Python, LLVM, and
# the MSVC C/C++ runtime and Windows SDK (no Visual Studio) - then it builds IED and installs it. Safe to re-run: it pulls,
# then continues where the last run stopped.
#
#   irm https://raw.githubusercontent.com/AlanBuechner/IED/main/install.ps1 | iex
#
# Environment variables (set before the command): IED_DIR = where to clone (default %USERPROFILE%\IED),
# IED_BRANCH = branch to install (default main), IED_REPO = repository URL.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Windows PowerShell's progress bar slows Expand-Archive many times over

$repo = if ($env:IED_REPO) { $env:IED_REPO } else { 'https://github.com/AlanBuechner/IED.git' }
$branch = if ($env:IED_BRANCH) { $env:IED_BRANCH } else { 'main' }
$dir = if ($env:IED_DIR) { $env:IED_DIR } else { Join-Path $env:USERPROFILE 'IED' }

# Git for Windows' portable MinGit (with Git Credential Manager, which signs in to GitHub), for a machine without git.
$minGitVersion = '2.56.0.4'
$minGitUrl = 'https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.4/MinGit-2.56.0.4-64-bit.zip'
$minGitSha256 = 'E3BF78351FE777FBA47DDEE5950082F8A98BB0F02604FDC7BA3EE2CB68FFA877'
$minGitHome = Join-Path $env:LOCALAPPDATA 'IED\git'

function Say($text) { Write-Host "[install] $text" -ForegroundColor Cyan }

function Run([string]$what, [scriptblock]$command) {
	& $command
	if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" }
}

function AddToUserPath([string]$folder) {
	$current = [Environment]::GetEnvironmentVariable('Path', 'User')
	$entries = if ($current) { $current -split ';' | Where-Object { $_ } } else { @() }
	if ($entries -notcontains $folder) {
		[Environment]::SetEnvironmentVariable('Path', (@($entries) + $folder) -join ';', 'User')
	}
}

function InstallMinGit {
	$zip = Join-Path $env:TEMP "MinGit-$minGitVersion-64-bit.zip"
	Say "no git on this machine: installing MinGit $minGitVersion into $minGitHome"
	Run 'downloading MinGit' { curl.exe -fsSL --retry 3 -o $zip $minGitUrl }
	if ((Get-FileHash -Algorithm SHA256 $zip).Hash -ne $minGitSha256) {
		Remove-Item $zip
		throw 'the MinGit download does not match its pinned SHA256; not installing it'
	}
	if (Test-Path $minGitHome) { Remove-Item -Recurse -Force $minGitHome }
	Expand-Archive -Force $zip $minGitHome
	Remove-Item $zip
}

function EnsureGit {
	if (Get-Command git -ErrorAction SilentlyContinue) { return }
	$cmd = Join-Path $minGitHome 'cmd'
	if (-not (Test-Path (Join-Path $cmd 'git.exe'))) { InstallMinGit }
	$env:Path = "$cmd;$env:Path"
	AddToUserPath $cmd
}

EnsureGit

if (Test-Path (Join-Path $dir '.git')) {
	Say "updating $dir"
	Run 'git fetch' { git -C $dir fetch origin $branch }
	Run 'git checkout' { git -C $dir checkout $branch }
	Run 'git pull' { git -C $dir pull --ff-only origin $branch }
} else {
	Say "cloning $repo into $dir"
	Run 'git clone' { git -c core.longpaths=true clone --branch $branch $repo $dir }
}

Set-Location $dir

Say 'setup: Python, git submodules, LLVM, the MSVC runtime and Windows SDK, web libraries; then the C4 compiler (the first run takes a while)'
Run 'setup' { cmd /c setup.bat --toolchain --add-to-path }

Say 'building and installing IED'
Run 'nuke install-ied' { cmd /c nuke.bat install-ied }

Say 'done. Open a NEW terminal and run `ied <folder>`, or start IED from the Start menu.'
