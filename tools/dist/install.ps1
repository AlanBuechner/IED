# @DIST_PRODUCT@ for Windows (x64): installs or updates the newest build published at @DIST_SERVER@ - one command in
# PowerShell, nothing to install first, no administrator rights:
#
#   irm @DIST_SERVER@/api/packages/@DIST_OWNER@/generic/@DIST_PRODUCT@/latest/install.ps1 | iex
#
# Made by tools/dist/publish.py from tools/dist/install.ps1 (IED docs/21 "Prebuilt releases on the owner's Gitea").
# It reads the release's release.json, downloads the archives this machine does not have yet (each checked against its
# SHA-256), puts them into the install folder (only what changed; a program in use is renamed aside, so a running copy
# keeps running until it is restarted), runs the release's registration (IED: the Start menu and file types), puts its
# folders on the user's PATH and writes install.json last. Run it again to update.
#
# Set before the command, e.g. `$env:DIST_VERSION = '2026.11.21-c38038d0'`:
#   DIST_VERSION      install that published version instead of the newest
#   DIST_DIR          the install folder (default: the release's, %LOCALAPPDATA%\Programs\<name>)
#   DIST_NO_PATH      1: leave the user's PATH alone
#   DIST_NO_REGISTER  1: skip the registration (Start menu, file types)
#   DIST_FORCE        1: replace an install that something else made in that folder (a checkout's nuke install-ied)
#   DIST_UNINSTALL    1: remove it (the registration, the PATH entries, the folder)
& {
	$ErrorActionPreference = 'Stop'
	$ProgressPreference = 'SilentlyContinue'
	$server = if ($env:DIST_SERVER) { $env:DIST_SERVER.TrimEnd('/') } else { '@DIST_SERVER@' }
	$owner = '@DIST_OWNER@'
	$product = '@DIST_PRODUCT@'
	$platform = 'windows-x64'
	$base = "$server/api/packages/$owner/generic/$product"
	$marker = '.dist-installing'
	# Windows' own tools by their whole path: Git for Windows' Unix tools on the PATH have a tar and a curl of their own.
	$system32 = Join-Path $env:SystemRoot 'System32'
	$tar = Join-Path $system32 'tar.exe'
	$curl = Join-Path $system32 'curl.exe'
	$robocopy = Join-Path $system32 'robocopy.exe'

	function Say([string]$text) { Write-Host "[$product] $text" -ForegroundColor Cyan }
	function Flag([string]$name) { $value = [Environment]::GetEnvironmentVariable($name); return [bool]($value -and $value -ne '0') }
	function Now { return [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
	function Under([string]$path, [string]$folder) { return $path.StartsWith($folder.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) }
	function Inside([string]$folder, [string]$relative) {
		if (-not $relative) { return $folder }
		return Join-Path $folder ($relative -replace '/', '\')
	}
	# An archive's key in install.json: its place in the folder ('.' for the folder itself).
	function KeyOf([string]$to) { if ($to) { return $to } return '.' }

	function ReleaseOf([string]$version) {
		$url = "$base/$version/release.json"
		try { $text = Invoke-RestMethod -UseBasicParsing -Uri $url }
		catch { throw "no release '$version' of $product at ${server}: $($_.Exception.Message) ($url)" }
		if ($text -is [string]) { return $text | ConvertFrom-Json }
		return $text
	}

	function Fetch([string]$url, [string]$file) {
		if (Test-Path -LiteralPath $curl) {
			& $curl -fsSL --retry 3 -o $file $url
			if ($LASTEXITCODE -ne 0) { throw "downloading $url failed (curl exit code $LASTEXITCODE)" }
		}
		else { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $file }
	}

	function ReadRecord([string]$folder) {
		$path = Join-Path $folder 'install.json'
		if (-not (Test-Path -LiteralPath $path)) { return $null }
		try { return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json } catch { return $null }
	}

	function WriteUtf8([string]$path, [string]$text) {
		$temporary = "$path.tmp"
		[IO.File]::WriteAllText($temporary, $text, (New-Object Text.UTF8Encoding $false))
		Move-Item -Force -LiteralPath $temporary -Destination $path
	}

	# The files under `folder` that a running process has loaded (its program and DLLs), and those processes' names.
	function InUse([string]$folder) {
		$files = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
		$names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
		foreach ($process in Get-Process) {
			try { $modules = $process.Modules } catch { continue }
			foreach ($module in $modules) {
				$file = $module.FileName
				if ($file -and (Under $file $folder)) { [void]$files.Add($file); [void]$names.Add($process.ProcessName) }
			}
		}
		return @{ files = $files; names = $names }
	}

	function RemoveAside([string]$folder) {
		if (-not (Test-Path -LiteralPath $folder)) { return }
		Get-ChildItem -LiteralPath $folder -Recurse -Force -File -Filter '*.old' -ErrorAction SilentlyContinue |
			ForEach-Object { try { Remove-Item -Force -LiteralPath $_.FullName } catch {} }
	}

	# `to` made like `from`: changed files copied, files gone from it removed - but under `keep` (whole paths) and the
	# renamed-aside *.old ones.
	function Mirror([string]$from, [string]$to, [string[]]$keep) {
		New-Item -ItemType Directory -Force -Path $to | Out-Null
		# robocopy purges a folder the source lacks as a whole, before an exclusion inside it counts: the source gets
		# the folders that hold what is kept.
		foreach ($kept in $keep) {
			if (-not (Under $kept $to)) { continue }
			$parent = Split-Path -Parent $kept.Substring($to.TrimEnd('\').Length + 1)
			if ($parent) { New-Item -ItemType Directory -Force -Path (Join-Path $from $parent) | Out-Null }
		}
		$arguments = @($from, $to, '/MIR', '/R:3', '/W:1', '/NFL', '/NDL', '/NP', '/NJH', '/NJS', '/XF', '*.old', $marker)
		if ($keep.Count) { $arguments += '/XD'; $arguments += $keep }
		& $robocopy @arguments | Out-Null
		if ($LASTEXITCODE -ge 8) { throw "copying into $to failed (robocopy exit code $LASTEXITCODE); is a file there open in another program?" }
	}

	function Broadcast {
		try {
			if (-not ('DistEnvironment' -as [type])) {
				Add-Type -Namespace '' -Name DistEnvironment -MemberDefinition '[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, UIntPtr wParam, string lParam, uint flags, uint timeout, out UIntPtr result);'
			}
			$result = [UIntPtr]::Zero
			[void][DistEnvironment]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result)
		}
		catch {}
	}

	# Adds (or with -Remove takes out) folders on the user's PATH, keeping its other entries and its kind (an
	# expandable string keeps %VARIABLES% working); programs started afterwards see it, and so does this session.
	function ChangePath([string[]]$folders, [switch]$Remove) {
		$key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
		try {
			$value = [string]$key.GetValue('Path', '', 'DoNotExpandEnvironmentNames')
			$kind = [Microsoft.Win32.RegistryValueKind]::ExpandString
			if ($value) { $found = $key.GetValueKind('Path'); if ($found -eq 'String') { $kind = $found } }
			$entries = @($value -split ';' | Where-Object { $_ })
			$same = { param($entry, $folder) [Environment]::ExpandEnvironmentVariables($entry).Trim('"').TrimEnd('\') -ieq $folder.TrimEnd('\') }
			$changed = $false
			foreach ($folder in $folders) {
				$there = @($entries | Where-Object { & $same $_ $folder })
				if ($Remove -and $there.Count) { $entries = @($entries | Where-Object { -not (& $same $_ $folder) }); $changed = $true }
				elseif (-not $Remove -and -not $there.Count) { $entries += $folder; $changed = $true }
			}
			if ($changed) { $key.SetValue('Path', ($entries -join ';'), $kind); Broadcast }
		}
		finally { $key.Close() }
		if (-not $Remove) {
			foreach ($folder in $folders) { if (@($env:Path -split ';') -notcontains $folder) { $env:Path = "$env:Path;$folder" } }
		}
		return $changed
	}

	function RunAll($commands, [string]$folder, [string]$what) {
		foreach ($command in @($commands)) {
			if (-not $command) { continue }
			$parts = @($command)
			$program = Inside $folder $parts[0]
			if (-not (Test-Path -LiteralPath $program)) { Say "$what`: $($parts[0]) is not there; skipped"; continue }
			$arguments = @($parts | Select-Object -Skip 1)
			& $program @arguments
			if ($LASTEXITCODE -ne 0) { throw "$what`: $($parts -join ' ') exited with $LASTEXITCODE" }
		}
	}

	$version = if ($env:DIST_VERSION) { $env:DIST_VERSION } else { 'latest' }
	$release = ReleaseOf $version
	$target = $release.platforms.$platform
	if (-not $target) { throw "$product $($release.version) has no $platform build" }
	$name = if ($release.name) { $release.name } else { $product }
	$folder = [IO.Path]::GetFullPath($(if ($env:DIST_DIR) { $env:DIST_DIR } else { [Environment]::ExpandEnvironmentVariables($target.folder) }))
	$pathFolders = @(@($target.path) | Where-Object { $_ -ne $null } | ForEach-Object { Inside $folder $_ })
	$current = ReadRecord $folder

	# Only a folder this installer made (install.json with "dist", or its marker from a run that stopped) is changed;
	# another install there - IED's own `nuke install-ied` from a checkout - only with DIST_FORCE.
	if (Test-Path -LiteralPath $folder) {
		$empty = -not (Get-ChildItem -LiteralPath $folder -Force | Select-Object -First 1)
		$ours = $empty -or ($current -and $current.dist) -or (Test-Path -LiteralPath (Join-Path $folder $marker))
		if (-not $ours -and -not (Flag 'DIST_FORCE')) {
			$how = if ($current -and $current.source) { "installed from the checkout $($current.source) (nuke install-ied)" } else { 'installed some other way' }
			throw "$folder holds $name $how. Set `$env:DIST_FORCE = '1' to replace it with the published build, or `$env:DIST_DIR to install elsewhere."
		}
	}

	if (Flag 'DIST_UNINSTALL') {
		if (-not (Test-Path -LiteralPath $folder)) { Say "$name is not installed in $folder"; return }
		$use = InUse $folder
		if ($use.names.Count) { throw "$name is running from $folder ($($use.names -join ', ')); close it (ied stop) and run this again" }
		RunAll $target.unregister $folder 'unregister'
		if (ChangePath $pathFolders -Remove) { Say 'taken off your PATH' }
		Remove-Item -Recurse -Force -LiteralPath $folder
		Say "removed $name from $folder"
		return
	}

	Say "$name $($release.version) (commit $("$($release.commit)".Substring(0, [Math]::Min(10, "$($release.commit)".Length)))) into $folder"
	$installed = @{}
	if ($current -and $current.dist -and $current.dist.archives) {
		foreach ($property in $current.dist.archives.PSObject.Properties) { $installed[$property.Name] = $property.Value }
	}
	$archives = @($target.archives)
	$work = Join-Path $env:TEMP "dist-$product-$($release.version)"
	$staging = "$folder.staging"
	New-Item -ItemType Directory -Force -Path $work, $folder | Out-Null
	New-Item -ItemType File -Force -Path (Join-Path $folder $marker) | Out-Null
	RemoveAside $folder

	# Download what is new, each checked against its SHA-256.
	$wanted = @()
	foreach ($archive in $archives) {
		$destination = Inside $folder $archive.to
		if ($installed[(KeyOf $archive.to)] -eq $archive.sha256 -and (Test-Path -LiteralPath $destination)) {
			Say "$($archive.file): already installed"
			continue
		}
		$file = Join-Path $work $archive.file
		if (-not ((Test-Path -LiteralPath $file) -and (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash -eq $archive.sha256)) {
			Say ("downloading {0} ({1:N0} MB)" -f $archive.file, ($archive.size / 1MB))
			Fetch "$base/$($release.version)/$($archive.file)" $file
			if ((Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash -ne $archive.sha256) {
				Remove-Item -Force -LiteralPath $file
				throw "$($archive.file) does not match its SHA-256 in release.json; nothing was changed"
			}
		}
		$wanted += $archive
	}

	# Unpack them beside the folder, then rename aside what a running program has loaded from where they go.
	if (Test-Path -LiteralPath $staging) { Remove-Item -Recurse -Force -LiteralPath $staging }
	for ($i = 0; $i -lt $wanted.Count; $i++) {
		$unpacked = Join-Path $staging $i
		New-Item -ItemType Directory -Force -Path $unpacked | Out-Null
		Say "unpacking $($wanted[$i].file)"
		if (Test-Path -LiteralPath $tar) {
			& $tar -xf (Join-Path $work $wanted[$i].file) -C $unpacked
			if ($LASTEXITCODE -ne 0) { throw "unpacking $($wanted[$i].file) failed (tar exit code $LASTEXITCODE)" }
		}
		else { Expand-Archive -LiteralPath (Join-Path $work $wanted[$i].file) -DestinationPath $unpacked }
	}
	# What an archive's folder holds that is not the archive's: the other archives' folders inside it and, for the
	# install folder itself, the release's keep list (other toolchains, caches).
	$others = @($archives | ForEach-Object { Inside $folder $_.to })
	function KeepOf($archive) {
		$destination = Inside $folder $archive.to
		$keep = @($others | Where-Object { $_ -ne $destination -and (Under $_ $destination) })
		if (-not $archive.to) { $keep += @(@($target.keep) | Where-Object { $_ } | ForEach-Object { Inside $folder $_ }) }
		return ,$keep
	}
	$use = InUse $folder
	$stamp = Now
	foreach ($file in $use.files) {
		foreach ($archive in $wanted) {
			$destination = Inside $folder $archive.to
			if (-not (Under $file $destination) -or -not (Test-Path -LiteralPath $file)) { continue }
			$kept = KeepOf $archive
			if (@($kept | Where-Object { $file -eq $_ -or (Under $file $_) }).Count) { continue }
			Rename-Item -LiteralPath $file -NewName ('{0}.{1}.old' -f (Split-Path -Leaf $file), $stamp)
			break
		}
	}

	# Each archive into its place: its folder made like the archive, leaving what KeepOf names alone. install.json goes
	# first and comes back last: without it the folder is mid-install.
	Remove-Item -Force -LiteralPath (Join-Path $folder 'install.json') -ErrorAction SilentlyContinue
	for ($i = 0; $i -lt $wanted.Count; $i++) {
		Say "installing $($wanted[$i].file)"
		Mirror (Join-Path $staging $i) (Inside $folder $wanted[$i].to) (KeepOf $wanted[$i])
		$installed[(KeyOf $wanted[$i].to)] = $wanted[$i].sha256
	}
	if (Test-Path -LiteralPath $staging) { Remove-Item -Recurse -Force -LiteralPath $staging }

	# An update command beside the programs: this installer again, from the same server.
	if ($pathFolders.Count) {
		$update = Join-Path $pathFolders[0] "$product-update.cmd"
		WriteUtf8 $update ("@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -Command `"irm $base/latest/install.ps1 | iex`"`r`n")
	}
	if (-not (Flag 'DIST_NO_REGISTER')) { RunAll $target.register $folder 'register' }
	if (-not (Flag 'DIST_NO_PATH') -and $pathFolders.Count) {
		if (ChangePath $pathFolders) { Say "on your PATH: $($pathFolders -join ', ') (terminals opened from now on find it)" }
	}

	$archivesDone = [ordered]@{}
	foreach ($archive in $archives) { $key = KeyOf $archive.to; if ($installed.ContainsKey($key)) { $archivesDone[$key] = $installed[$key] } }
	$record = [ordered]@{
		product = $product; name = $name; version = $release.version; commit = $release.commit; built = $release.built
		installedMs = Now
		dist = [ordered]@{ server = $server; owner = $owner; platform = $platform; archives = $archivesDone }
	}
	WriteUtf8 (Join-Path $folder 'install.json') ($record | ConvertTo-Json -Depth 5)
	Remove-Item -Force -LiteralPath (Join-Path $folder $marker) -ErrorAction SilentlyContinue
	Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
	Say "installed $name $($release.version) in $folder"
	if ($use.names.Count) { Say "$name was running ($($use.names -join ', ')): restart it to use this version (ied stop, then open it again)" }
	if ($pathFolders.Count) { Say "update later with: $product-update" }
}
