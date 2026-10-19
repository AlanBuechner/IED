# tools/fetch-deps.ps1 — pre-download third-party dependencies for IED.
#
# Idempotent + resumable: already-downloaded archives in third_party\_archives\ are skipped.
# Re-run any time; use -Force to re-download everything. Versions are pinned here on purpose —
# bump a pin, delete the matching archive+folder (or pass -Force), re-run.
#
# After running, third_party\README.md documents what each dependency is for.

param([switch]$Force)

$ErrorActionPreference = "Stop"
$root    = Split-Path -Parent $PSScriptRoot           # repo root
$tp      = Join-Path $root "third_party"
$arcs    = Join-Path $tp "_archives"
New-Item -ItemType Directory -Force -Path $arcs | Out-Null

# ---------------- pinned versions (the manifest of record) ----------------
$PINS = [ordered]@{
    monaco        = "0.57.0"          # microsoft/monaco-editor — text editor core (editor plugin)
    xterm         = "6.0.0"           # @xterm/xterm — terminal renderer (terminal plugin + output window)
    codicons      = "v0.0.46-45"      # microsoft/vscode-codicons — VS-style icon set (ui-kit)
    quickjsng     = "v0.17.0"         # quickjs-ng/quickjs — JS plugin runtime (amalgam source; ChakraCore dropped: unmaintained since 2020, official binary links dead)
    webview2      = "1.0.4258.31"     # Microsoft.Web.WebView2 — webview embed headers/libs (host)
    searxng       = "HEAD-stable"     # searxng/searxng — packaged metasearcher for AI web egress (design-phase packaging)
    tracy         = "HEAD-master"     # psymon/tracy — profiler reference implementation (profiler plugin seam)
    libgit2       = "HEAD-master"     # libgit2/libgit2 — git plumbing candidate (vs git CLI; see docs/05)
}

function Get-OrFetch([string]$name, [string]$url, [string]$file) {
    $dest = Join-Path $arcs $file
    if ((Test-Path $dest) -and -not $Force) { Write-Host "  skip (cached): $file"; return $dest }
    Write-Host "  fetch: $url"
    curl.exe -sSL --retry 3 -o $dest $url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
    return $dest
}

function Expand-Zip([string]$zip, [string]$destDir) {
    if (Test-Path $destDir) { Remove-Item -Recurse -Force $destDir }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    tar.exe -xf $zip -C $destDir
}

function Expand-Tgz([string]$tgz, [string]$destDir) {
    if (Test-Path $destDir) { Remove-Item -Recurse -Force $destDir }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    tar.exe -xzf $tgz -C $destDir --strip-components=1
}

function Clone-Shallow([string]$repo, [string]$tag, [string]$dir, [string[]]$sparsePatterns = @()) {
    if ((Test-Path $dir) -and -not $Force) { Write-Host "  skip (cached): $dir"; return }
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Write-Host "  clone: $repo ${tag}"
    if ($tag -ne "HEAD-master" -and $tag -ne "HEAD-stable") {
        git clone --depth 1 --branch $tag $repo $dir
    } else {
        git clone --depth 1 $repo $dir
    }
    # Windows-illegal paths (e.g. names containing ':') break checkout: re-clone sparse with the given patterns.
    if ($LASTEXITCODE -ne 0) {
        if (-not $sparsePatterns.Count) { throw "git clone failed: $repo" }
        Write-Host "  retry with sparse checkout (non-cone)"
        Remove-Item -Recurse -Force $dir
        git clone --depth 1 --no-checkout $repo $dir | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "git clone (sparse) failed: $repo" }
        git -C $dir sparse-checkout set --no-cone $sparsePatterns
        if ($LASTEXITCODE -ne 0) { throw "git sparse-checkout failed: $repo" }
        git -C $dir checkout HEAD
        if ($LASTEXITCODE -ne 0) { throw "git checkout failed (sparse): $repo" }
    }
    $commit = (git -C $dir rev-parse HEAD)
    Remove-Item -Recurse -Force (Join-Path $dir ".git")
    Set-Content -Path (Join-Path $dir "UPSTREAM_COMMIT.txt") -Value "$repo @ $commit"
}

# ---------------- 1. monaco-editor (npm) ----------------
Write-Host "[monaco $($PINS.monaco)]"
$arc = Join-Path $arcs "monaco-editor-$($PINS.monaco).tgz"
if ((Test-Path $arc) -and -not $Force) { Write-Host "  skip (cached)" } else {
    npm pack "monaco-editor@$($PINS.monaco)" --pack-destination $arcs --loglevel=error | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "npm pack monaco failed" }
}
Expand-Tgz $arc (Join-Path $tp "monaco-editor-$($PINS.monaco)")

# ---------------- 2. xterm.js (npm: core + fit + web-links addons) ----------------
Write-Host "[xterm $($PINS.xterm)]"
foreach ($pkg in @("@xterm/xterm", "@xterm/addon-fit", "@xterm/addon-web-links")) {
    if ($pkg -eq "@xterm/xterm") { $ver = $PINS.xterm } else { $ver = (npm view "$pkg" version) }
    # npm names scoped-package tarballs as <scope>-<name>-<ver>.tgz ('@' dropped, '/' -> '-')
    $safeName = ($pkg -replace '@','' -replace '/','-')
    $arc = Join-Path $arcs ("{0}-{1}.tgz" -f $safeName, $ver)
    if ((Test-Path $arc) -and -not $Force) { Write-Host "  skip (cached): $pkg@$ver" } else {
        npm pack "$pkg@$ver" --pack-destination $arcs --loglevel=error | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "npm pack $pkg failed" }
    }
    Expand-Tgz $arc (Join-Path $tp ("xterm-{0}-{1}" -f ($pkg -replace '@xterm/',''), $ver))
}

# ---------------- 3. codicons (git tag) ----------------
Write-Host "[codicons $($PINS.codicons)]"
Clone-Shallow "https://github.com/microsoft/vscode-codicons.git" $PINS.codicons (Join-Path $tp "codicons")

# ---------------- 4. quickjs-ng amalgam (release asset) ----------------
Write-Host "[quickjs-ng $($PINS.quickjsng)]"
$ver = $PINS.quickjsng -replace '^v',''
$arc = Get-OrFetch "quickjs" "https://github.com/quickjs-ng/quickjs/releases/download/v$ver/quickjs-amalgam.zip" "quickjs-amalgam-v$ver.zip"
Expand-Zip $arc (Join-Path $tp "quickjs-ng-v$ver")

# ---------------- 5. WebView2 NuGet (headers + libs for MSVC) ----------------
Write-Host "[webview2 $($PINS.webview2)]"
$nugetUrl = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$($PINS.webview2)/microsoft.web.webview2.$($PINS.webview2).nupkg"
$arc = Get-OrFetch "webview2" $nugetUrl "microsoft.web.webview2.$($PINS.webview2).nupkg"
Expand-Zip $arc (Join-Path $tp "webview2-$($PINS.webview2)")

# ---------------- 6. SearXNG (codeload tarball; packaging is design-phase work) ----------------
# The repo contains template files with Windows-illegal names (containing ':') that break `git clone`
# checkout, sparse-checkout AND `git archive` on Windows. Codeload tarball + bsdtar instead: bsdtar
# skips the 4 illegal entries and extracts everything else. Exit code 1 from tar is expected.
Write-Host "[searxng]"
$arc = Get-OrFetch "searxng" "https://codeload.github.com/searxng/searxng/tar.gz/refs/heads/master" "searxng-master.tar.gz"
if ((Test-Path (Join-Path $tp "searxng")) -and -not $Force) {
    Write-Host "  skip (cached)"
} else {
    if (Test-Path (Join-Path $tp "searxng")) { Remove-Item -Recurse -Force (Join-Path $tp "searxng") }
    New-Item -ItemType Directory -Force -Path (Join-Path $tp "searxng") | Out-Null
    tar.exe -xzf $arc -C (Join-Path $tp "searxng") --strip-components=1 2>$null
    $sha = curl.exe -s "https://api.github.com/repos/searxng/searxng/commits/master" | python -c "import json,sys; print(json.load(sys.stdin)['sha'])"
    Set-Content (Join-Path $tp "searxng\UPSTREAM_COMMIT.txt") "https://github.com/searxng/searxng.git @ $sha (codeload tarball; 4 Windows-illegal ':socket' template files skipped on extract)"
}

# ---------------- 7. Tracy (shallow clone; profiler reference + trace format seam) ----------------
Write-Host "[tracy]"
Clone-Shallow "https://github.com/wolfpld/tracy.git" $PINS.tracy (Join-Path $tp "tracy")

# ---------------- 8. libgit2 (shallow clone; git plumbing candidate) ----------------
Write-Host "[libgit2]"
Clone-Shallow "https://github.com/libgit2/libgit2.git" $PINS.libgit2 (Join-Path $tp "libgit2")

# ---------------- manifest: record what is on disk + archive hashes ----------------
$manifest = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    note        = "Generated by tools/fetch-deps.ps1. Archive SHA256s are the integrity record; extracted folders derive from them."
    archives    = @{}
}
Get-ChildItem $arcs -File | Where-Object { $_.Name -ne "manifest.json" } | ForEach-Object {
    $manifest.archives[$_.Name] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToUpper()
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $arcs "manifest.json")
# committed copy: third_party/ is gitignored, this file in tools/ is the durable integrity record
$manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot "deps-manifest.json")

Write-Host ""
Write-Host "Done. Contents:"
Get-ChildItem $tp -Directory | ForEach-Object {
    $mb = [math]::Round(((Get-ChildItem $_.FullName -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
    Write-Host ("  {0,-32} {1,8} MB" -f $_.Name, $mb)
}
