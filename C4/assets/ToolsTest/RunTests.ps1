# ToolsTest runner - checks of the build output, the toolchain layout and the compiler's flags, and a toolchain
# built and installed for Nuke. Each check returns its failures; an empty list passes.
#
# Usage:  .\RunTests.ps1
$ErrorActionPreference = "Continue"

$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $root "..\..")).Path
$compiler = Join-Path (Split-Path -Parent $repoRoot) "build/bin/Development-windows-x64/C4/C4.exe"
$scratch  = Join-Path $repoRoot "assets/build-output/toolstest"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "C4 ToolsTest Runner" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

function Get-Tree([string]$dir) {
    $tree = @{}
    Get-ChildItem $dir -Recurse -File | ForEach-Object {
        $tree[$_.FullName.Substring($dir.Length + 1).Replace("\", "/")] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
    }
    return $tree
}

# The Bedrock copy next to C4.exe is exactly Bedrock/ (no missing, extra or different file; BUG-099).
function Test-ShippedBedrockMirrorsSource {
    $shipped = Join-Path (Split-Path -Parent $repoRoot) "build/bin/Development-windows-x64/C4/Bedrock"
    $source  = Join-Path $repoRoot "Bedrock"
    if (-not (Test-Path $shipped)) { return @("no Bedrock next to C4.exe: $shipped (build first)") }
    $failures = @()
    $want = Get-Tree $source
    $have = Get-Tree $shipped
    foreach ($path in $want.Keys) {
        if (-not $have.ContainsKey($path)) { $failures += "missing from the shipped Bedrock: $path" }
        elseif ($have[$path] -ne $want[$path]) { $failures += "differs from the source: $path" }
    }
    foreach ($path in $have.Keys) {
        if (-not $want.ContainsKey($path)) { $failures += "stale file in the shipped Bedrock: $path" }
    }
    return $failures
}

# Compiles `source` (one main.cf) with `flags` in a fresh folder and returns the program's exit code, or a
# failure message when it does not build.
function Invoke-Program([string]$name, [string]$source, [string[]]$flags) {
    $dir = Join-Path $scratch $name
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Force $dir | Out-Null
    Set-Content -Path (Join-Path $dir "main.cf") -Value $source -Encoding ascii
    $exe = Join-Path $dir "out.exe"
    $output = & $compiler $dir -o $exe @flags 2>&1 | ForEach-Object { [string]$_ }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) { return "did not build: " + (($output | Select-Object -Last 6) -join " | ") }
    & $exe | Out-Null
    return $LASTEXITCODE
}

# -D NAME, -DNAME=VALUE and an override of a predefined macro reach every file (B4).
function Test-DefineFlag {
    $source = @"
int32 main()
{
#if !defined(FROM_CLI) || FROM_CLI != 1
	return 1;
#endif
#if VALUE != 42
	return 2;
#endif
#if PLATFORM_WINDOWS != 7 || !defined(ARCH_X64) || defined(TEST)
	return 3;
#endif
	return 0;
}
"@
    $result = Invoke-Program "define" $source @("-D", "FROM_CLI", "-DVALUE=42", "-D", "PLATFORM_WINDOWS=7")
    if ($result -is [string]) { return @($result) }
    if ($result -ne 0) { return @("the -D program exited $result (1: -D NAME, 2: -DNAME=VALUE, 3: overriding a predefined macro)") }
    return @()
}

# --version prints the compiler's version.
function Test-VersionFlag {
    $output = (& $compiler --version 2>&1 | ForEach-Object { [string]$_ }) -join "`n"
    if ($output -notmatch '^C4 \d+\.\d+\.\d+') { return @("--version printed '$output'") }
    return @()
}

# Runs the compiler with `arguments` and returns its exit code and output.
function Invoke-Compiler([string]$exePath, [string[]]$arguments) {
    $output = & $exePath @arguments 2>&1 | ForEach-Object { [string]$_ }
    return @{ exit = $LASTEXITCODE; output = ($output -join "`n") }
}

# A fresh folder under the scratch area with `files` (name -> text) written into it.
function New-Sources([string]$name, [hashtable]$files) {
    $dir = Join-Path $scratch $name
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach ($file in $files.Keys) {
        $path = Join-Path $dir $file
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        Set-Content -Path $path -Value $files[$file] -Encoding ascii
    }
    return $dir
}

$hello = "int32 main() { printf(""hello\n""); return 0; }"

# No input and an unknown option are usage errors (exit 2), each with a message (B21).
function Test-UsageErrors {
    $failures = @()
    $none = Invoke-Compiler $compiler @()
    if ($none.exit -ne 2 -or $none.output -notmatch "no input") { $failures += "no input: exit $($none.exit), '$($none.output)'" }
    $unknown = Invoke-Compiler $compiler @("-frobnicate", "x")
    if ($unknown.exit -ne 2 -or $unknown.output -notmatch "unknown option '-frobnicate'") { $failures += "unknown option: exit $($unknown.exit), '$($unknown.output)'" }
    $missing = Invoke-Compiler $compiler @("x", "-o")
    if ($missing.exit -ne 2 -or $missing.output -notmatch "'-o' needs a value") { $failures += "missing value: exit $($missing.exit), '$($missing.output)'" }
    return $failures
}

# -h lists every option (BUG-048).
function Test-HelpListsEveryOption {
    $help = (Invoke-Compiler $compiler @("-h")).output
    $failures = @()
    foreach ($flag in @("-o ", "-I ", "-D ", "-O ", "--threads", "--partitions", "--incremental", "-march=", "-m ", "--int-dir", "-l ", "-L ", "-c ", "-bc", "-S", "-test", "-test-json", "-reflect-tag",
        "--script-host", "-gc-stats", "-Werror", "-w ", "-export-macros", "-ftime", "--pretty-print", "-v ", "--version", "-h")) {
        if (-not $help.Contains($flag)) { $failures += "-h does not list '$($flag.Trim())'" }
    }
    return $failures
}

# -c stops at the object, -bc writes bitcode, --int-dir moves the intermediates (BUG-042).
function Test-OutputFlags {
    $dir = New-Sources "outputs" @{ "src/main.cf" = $hello }
    $src = Join-Path $dir "src"
    $failures = @()
    $object = Invoke-Compiler $compiler @($src, "-o", (Join-Path $dir "c/app.exe"), "-c")
    if ($object.exit -ne 0 -or -not (Test-Path (Join-Path $dir "c/app.obj")) -or (Test-Path (Join-Path $dir "c/app.exe"))) { $failures += "-c did not stop at app.obj" }
    $bitcode = Invoke-Compiler $compiler @($src, "-o", (Join-Path $dir "bc/app.exe"), "-bc")
    $bc = Join-Path $dir "bc/app.bc"
    if ($bitcode.exit -ne 0 -or -not (Test-Path $bc)) { $failures += "-bc wrote no app.bc" }
    elseif ([System.IO.File]::ReadAllBytes($bc)[0..1] -join "," -ne "66,67") { $failures += "app.bc is not LLVM bitcode" }
    $moved = Invoke-Compiler $compiler @($src, "-o", (Join-Path $dir "out/app.exe"), "--int-dir", (Join-Path $dir "int"))
    if ($moved.exit -ne 0 -or @(Get-ChildItem (Join-Path $dir "int/app.objects") -Filter "*.obj" -ErrorAction SilentlyContinue).Count -eq 0 -or (Test-Path (Join-Path $dir "out/app.objects"))) { $failures += "--int-dir did not take the intermediates" }
    elseif ((& (Join-Path $dir "out/app.exe")) -ne "hello") { $failures += "the --int-dir build does not run" }
    return $failures
}

# -gc-stats makes the program print the GC's statistics to stderr after each collection (BUG-042).
function Test-GcStatsFlag {
    $dir = New-Sources "gcstats" @{ "main.cf" = "int32 main() { int32* p = new int32(); *p = 1; gc_collect(); return 0; }" }
    $exe = Join-Path $dir "app.exe"
    $build = Invoke-Compiler $compiler @($dir, "-o", $exe, "-gc-stats")
    if ($build.exit -ne 0) { return @("-gc-stats did not build: $($build.output)") }
    $run = (& $exe 2>&1 | ForEach-Object { [string]$_ }) -join "`n"
    if ($run -notmatch "\[GC\] allocs=") { return @("-gc-stats printed no statistics: '$run'") }
    return @()
}

# --time-phases prints each phase's time when the build succeeds, the backend in its steps (BUG-234).
function Test-TimePhasesFlag {
    $dir = New-Sources "timephases" @{ "main.cf" = $hello }
    $build = Invoke-Compiler $compiler @($dir, "-o", (Join-Path $dir "app.exe"), "--time-phases")
    if ($build.exit -ne 0) { return @("--time-phases did not build: $($build.output)") }
    $failures = @()
    foreach ($phase in @("Phase timing:", "parse", "model", "lower", "optimize", "codegen", "runtime+link")) {
        if (-not $build.output.Contains($phase)) { $failures += "--time-phases printed no '$phase'" }
    }
    return $failures
}

# --script-host writes <out>.c4i beside the program, which still runs; a program that uses the scripting API's table
# without it fails with the flag to use (docs/runtime/04 section 3).
function Test-ScriptHostFlag {
    $dir = New-Sources "scripthost" @{ "main.cf" = "int32 Twice(int32 v) { return v * 2; }`nint32 main() { return Twice(2) == 4 ? 0 : 1; }" }
    $exe = Join-Path $dir "host.exe"
    $build = Invoke-Compiler $compiler @($dir, "-o", $exe, "--script-host")
    if ($build.exit -ne 0) { return @("--script-host did not build: $($build.output)") }
    $failures = @()
    $interface = Join-Path $dir "host.c4i"
    if (-not (Test-Path $interface)) { $failures += "--script-host wrote no $interface" }
    else {
        $bytes = [System.IO.File]::ReadAllBytes($interface)
        if ([System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne "C4IF") { $failures += "host.c4i does not start with C4IF" }
    }
    & $exe
    if ($LASTEXITCODE -ne 0) { $failures += "the script host exited $LASTEXITCODE" }

    $user = New-Sources "scriptuser" @{ "main.cf" = "[link(""__c4_script_table"")] int64 g_table;`nint32 main() { return (int32)(uint64)&g_table; }" }
    $refused = Invoke-Compiler $compiler @($user, "-o", (Join-Path $user "app.exe"))
    if ($refused.exit -eq 0) { $failures += "a program that uses the script table built without --script-host" }
    elseif (-not $refused.output.Contains("build it with --script-host")) { $failures += "no --script-host hint: $($refused.output)" }
    return $failures
}

# A relink that fails because the program is running (Windows keeps its file locked) leaves the program's .c4i and
# .pdb as they were, so the three still match; once the program stops, the relink replaces all three (BUG-322).
function Test-FailedRelinkKeepsTheProgramsFiles {
    $wait = "include ""bedrock.ch""`nint32 main() {`n`tfor (int32 i = 0; i < 1200 && !FS::FileExists(""stop""); ++i) Threading::Thread::Sleep(50000000);`n`treturn 0;`n}"
    $dir = New-Sources "relink" @{ "main.cf" = $wait }
    $exe = Join-Path $dir "host.exe"
    $flags = @($dir, "-o", $exe, "--script-host", "-g")
    $first = Invoke-Compiler $compiler $flags
    if ($first.exit -ne 0) { return @("the first build failed: $($first.output)") }
    $interface = Join-Path $dir "host.c4i"
    $pdb = Join-Path $dir "host.pdb"
    if (-not (Test-Path $interface) -or -not (Test-Path $pdb)) { return @("the first build wrote no host.c4i or host.pdb") }
    $interfaceBefore = (Get-FileHash $interface).Hash
    $pdbBefore = (Get-FileHash $pdb).Hash
    $failures = @()
    $running = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru -WindowStyle Hidden
    try {
        Add-Content -Path (Join-Path $dir "main.cf") -Value "int32 Added() { return 1; }"
        $locked = Invoke-Compiler $compiler $flags
        if ($locked.exit -eq 0) { $failures += "relinking the running program succeeded, so nothing was tested" }
        if ((Get-FileHash $interface).Hash -ne $interfaceBefore) { $failures += "a failed relink replaced host.c4i" }
        if ((Get-FileHash $pdb).Hash -ne $pdbBefore) { $failures += "a failed relink replaced host.pdb" }
        if (Test-Path "$pdb.partial") { $failures += "a failed relink left host.pdb.partial behind" }
    } finally {
        Set-Content -Path (Join-Path $dir "stop") -Value "" -Encoding ascii
        if (-not $running.WaitForExit(30000)) { $running.Kill() }
    }
    $relinked = Invoke-Compiler $compiler $flags
    if ($relinked.exit -ne 0) { $failures += "the relink after the program stopped failed: $($relinked.output)" }
    elseif ((Get-FileHash $interface).Hash -eq $interfaceBefore -or (Get-FileHash $pdb).Hash -eq $pdbBefore) { $failures += "the relink kept the old host.c4i or host.pdb" }
    elseif (Test-Path "$pdb.partial") { $failures += "a relink left host.pdb.partial behind" }
    return $failures
}
# -I compiles the folder it names with the program: its headers resolve and its sources link (BUG-041 notes, B38).
function Test-IncludeProject {
    $dir = New-Sources "project" @{
        "app/main.cf" = "include ""shared.ch""`nint32 main() { return Shared() == 7 ? 0 : 1; }"
        "lib/shared.ch" = "int32 Shared();"
        "lib/shared.cf" = "include ""shared.ch""`nint32 Shared() { return 7; }"
    }
    $exe = Join-Path $dir "app.exe"
    $build = Invoke-Compiler $compiler @((Join-Path $dir "app"), "-I", (Join-Path $dir "lib"), "-o", $exe)
    if ($build.exit -ne 0) { return @("-I did not build: $($build.output)") }
    & $exe | Out-Null
    if ($LASTEXITCODE -ne 0) { return @("the -I program exited $LASTEXITCODE") }
    return @()
}

# -l links a library and -L names where to look for one (BUG-042): timeGetTime is in winmm.lib only.
function Test-LinkFlags {
    $dir = New-Sources "link" @{ "main.cf" = "[link(""timeGetTime"")] uint32 TimeGetTime();`nint32 main() { return TimeGetTime() > 0 ? 0 : 1; }" }
    $failures = @()
    $without = Invoke-Compiler $compiler @($dir, "-o", (Join-Path $dir "a.exe"))
    if ($without.exit -eq 0) { $failures += "timeGetTime linked without -l winmm" }
    $with = Invoke-Compiler $compiler @($dir, "-o", (Join-Path $dir "b.exe"), "-l", "winmm")
    if ($with.exit -ne 0) { $failures += "-l winmm did not link: $($with.output)" }
    $sdk = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Lib\*\um\x64\winmm.lib" -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
    if ($sdk) {
        $libs = Join-Path $dir "libs"
        New-Item -ItemType Directory -Force $libs | Out-Null
        Copy-Item $sdk.FullName (Join-Path $libs "mywinmm.lib")
        $found = Invoke-Compiler $compiler @($dir, "-o", (Join-Path $dir "c.exe"), "-L", $libs, "-l", "mywinmm")
        if ($found.exit -ne 0) { $failures += "-L did not find mywinmm.lib: $($found.output)" }
    }
    return $failures
}

# A copy of the install outside the repository builds and runs a program, an extern "c" one too, with its own
# toolchain (D-56, BUG-043, BUG-045).
function Test-InstallOutsideRepository {
    $bin = Join-Path (Split-Path -Parent $repoRoot) "build/bin/Development-windows-x64"
    $kit = Join-Path $env:TEMP "c4-toolstest/kit"
    New-Item -ItemType Directory -Force $kit | Out-Null
    foreach ($folder in @("Bedrock", "C4Runtime", "toolchain")) {
        robocopy (Join-Path $bin "C4/$folder") (Join-Path $kit $folder) /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    }
    robocopy (Join-Path $bin "C4") $kit "C4.exe" "*.dll" /NFL /NDL /NJH /NJS /NP | Out-Null
    robocopy (Join-Path $bin "CBind") (Join-Path $kit "CBind") "CBind.exe" "*.dll" /NFL /NDL /NJH /NJS /NP | Out-Null
    $c4 = Join-Path $kit "C4.exe"
    $work = Join-Path $env:TEMP "c4-toolstest/programs"
    if (Test-Path $work) { Remove-Item $work -Recurse -Force }
    New-Item -ItemType Directory -Force (Join-Path $work "hello"), (Join-Path $work "native") | Out-Null
    Set-Content (Join-Path $work "hello/main.cf") $hello -Encoding ascii
    Set-Content (Join-Path $work "native/main.cf") "extern ""c"" include ""native.h""`nextern ""c"" source ""native.c""`nint32 main() { return Twice(21) == 42 ? 0 : 1; }" -Encoding ascii
    Set-Content (Join-Path $work "native/native.h") "int Twice(int value);" -Encoding ascii
    Set-Content (Join-Path $work "native/native.c") "#include ""native.h""`nint Twice(int value) { return value * 2; }" -Encoding ascii
    $failures = @()
    Push-Location $work
    try {
        foreach ($program in @("hello", "native")) {
            $exe = Join-Path $work "$program.exe"
            $build = Invoke-Compiler $c4 @($program, "-o", $exe, "-v")
            if ($build.exit -ne 0) { $failures += "$program did not build from the copied install: " + ($build.output -split "`n" | Select-Object -Last 4) -join " | "; continue }
            if (-not $build.output.Contains((Join-Path $kit "toolchain").Replace("\", "/")) -and -not $build.output.Contains((Join-Path $kit "toolchain"))) {
                $failures += "$program did not use the copied toolchain"
            }
            & $exe | Out-Null
            if ($LASTEXITCODE -ne 0) { $failures += "$program exited $LASTEXITCODE" }
        }
    } finally { Pop-Location }
    return $failures
}

# C4.exe on its own reports the missing toolchain by name, and $C4_HOME points it at an install elsewhere (D-56).
# Runs after Test-InstallOutsideRepository, whose kit it borrows.
function Test-HomeVariable {
    $kit = Join-Path $env:TEMP "c4-toolstest/kit"
    $bare = Join-Path $env:TEMP "c4-toolstest/bare"
    if (Test-Path $bare) { Remove-Item $bare -Recurse -Force }
    robocopy $kit $bare "C4.exe" "*.dll" /NFL /NDL /NJH /NJS /NP | Out-Null
    $source = Join-Path $env:TEMP "c4-toolstest/programs/hello"
    $exe = Join-Path $bare "hello.exe"
    $failures = @()
    $alone = Invoke-Compiler (Join-Path $bare "C4.exe") @($source, "-o", $exe)
    if ($alone.exit -eq 0 -or $alone.output -notmatch "C4_HOME") { $failures += "a bare C4.exe did not report its missing install: '$($alone.output)'" }
    $env:C4_HOME = $kit
    try { $homed = Invoke-Compiler (Join-Path $bare "C4.exe") @($source, "-o", $exe) }
    finally { Remove-Item Env:\C4_HOME }
    if ($homed.exit -ne 0) { $failures += "C4_HOME did not lead to the install: $($homed.output)" }
    elseif ((& $exe) -ne "hello") { $failures += "the C4_HOME build does not run" }
    return $failures
}

# Runs a nuke command line in `folder` and returns its exit code and output.
function Invoke-Nuke([string]$folder, [string]$command) {
    Push-Location $folder
    try { $output = cmd /c "$command 2>&1" | ForEach-Object { [string]$_ } }
    finally { Pop-Location }
    return @{ exit = $LASTEXITCODE; output = ($output -join "`n") }
}

# A toolchain assembled by MakeToolchain.py and installed into a scratch store (never the shared one) builds a
# workspace outside the repository through Nuke: `nuke init` makes a project that builds and runs, intermediates
# go to build/int, a module's sources, system libraries and own defines reach the program it is compiled into
# (the defines only the module's files, through the build description Nuke writes, D-59), `nuke run` returns the
# program's exit code, and a failed build fails `nuke build` and `nuke run` (BUG-045, BUG-046, BUG-199).
function Test-NukeToolchainProject {
    $scratchRoot = Join-Path $env:TEMP "c4-toolstest/nuke"
    if (Test-Path $scratchRoot) { Remove-Item $scratchRoot -Recurse -Force }
    $image = Join-Path $scratchRoot "image"
    $store = Join-Path $scratchRoot "store"
    $root = Join-Path $scratchRoot "Game"
    $python = Join-Path (Split-Path $repoRoot) "vendor/python/python.exe"
    if (-not (Test-Path $python)) { $python = "python" }
    $nuke = Join-Path (Split-Path $repoRoot) "bin/nuke.cmd"
    & $python (Join-Path $repoRoot "MakeToolchain.py") --out $image | Out-Null
    if ($LASTEXITCODE -ne 0) { return @("MakeToolchain.py did not assemble a toolchain") }
    $version = (Get-Content (Join-Path $image "toolchain.json") -Raw | ConvertFrom-Json).version
    $env:NUKE_TOOLCHAINS = $store
    try {
        $install = Invoke-Nuke $scratchRoot """$nuke"" toolchain install ""$image"""
        if ($install.exit -ne 0) { return @("nuke toolchain install failed: $($install.output)") }
        New-Item -ItemType Directory -Force $root | Out-Null
        $init = Invoke-Nuke $root """$nuke"" init Game --toolchain $version"
        if ($init.exit -ne 0) { return @("nuke init failed: $($init.output)") }
        $failures = @()
        $run = Invoke-Nuke $root """$nuke"" run"
        if ($run.exit -ne 0 -or $run.output -notmatch "(?m)^Game\s*$") { return @("the nuke init project did not build and run: " + ($run.output -split "`n" | Select-Object -Last 6) -join " | ") }
        $built = "build/{0}/Development-windows-x64/Game/Game.objects"
        if (-not (Test-Path (Join-Path $root ($built -f "int"))) -or (Test-Path (Join-Path $root ($built -f "bin")))) { $failures += "Nuke did not put the intermediates in build/int" }

        New-Item -ItemType Directory -Force (Join-Path $root "Engine/src") | Out-Null
        Set-Content (Join-Path $root "Engine/nuke.toml") "[project]`nkind = ""module""`nlinks = [""winmm.lib""]`ndefines = { ENGINE_FAST = ""1"" }" -Encoding ascii
        Set-Content (Join-Path $root "Engine/src/engine.ch") "uint32 EngineTicks();" -Encoding ascii
        Set-Content (Join-Path $root "Engine/src/engine.cf") "include ""engine.ch""`n[link(""timeGetTime"")] uint32 TimeGetTime();`n#if ENGINE_FAST`nuint32 EngineTicks() { return TimeGetTime(); }`n#else`nuint32 EngineTicks() { return 0; }`n#endif" -Encoding ascii
        $manifest = Join-Path $root "nuke.toml"
        (Get-Content $manifest -Raw).Replace("kind = ""executable""", "kind = ""executable""`ndepends = [""Engine""]") | Set-Content $manifest -Encoding ascii -NoNewline
        Set-Content (Join-Path $root "src/main.cf") "include ""engine.ch""`n#ifdef ENGINE_FAST`nint32 main() { return 9; }`n#else`nint32 main() { return EngineTicks() > 0 ? 3 : 1; }`n#endif" -Encoding ascii
        $linked = Invoke-Nuke $root """$nuke"" run"
        if ($linked.exit -ne 3) { $failures += "the program with a module did not build and return 3 (9: the module's define reached the game; 1: it did not reach the module): exit $($linked.exit), " + ($linked.output -split "`n" | Select-Object -Last 6) -join " | " }

        Set-Content (Join-Path $root "src/main.cf") "int32 main() { return undefinedName; }" -Encoding ascii
        $broken = Invoke-Nuke $root """$nuke"" build"
        if ($broken.exit -eq 0) { $failures += "nuke build exited 0 for a program that does not compile" }
        $brokenRun = Invoke-Nuke $root """$nuke"" run"
        if ($brokenRun.exit -eq 0 -or $brokenRun.exit -eq 3) { $failures += "nuke run of a program that does not compile exited $($brokenRun.exit) (it ran the old executable or reported success)" }
        return $failures
    } finally { Remove-Item Env:\NUKE_TOOLCHAINS -ErrorAction SilentlyContinue }
}

# A build description (--projects, docs/language/15-projects.md): each project's files get its defines, and an include
# carries what a header defines, not its project's settings.
function Test-ProjectDefines {
    $dir = New-Sources "projects-defines" @{
        "Engine/src/engine.ch" = "int32 EngineValue();"
        "Engine/src/engine.cf" = "include ""engine.ch""`n#if ENGINE_FAST`nint32 EngineValue() { return 1; }`n#else`nint32 EngineValue() { return 2; }`n#endif"
        "Game/src/main.cf" = "include ""engine.ch""`n#ifdef ENGINE_FAST`nint32 g_seen = 100;`n#else`nint32 g_seen = 0;`n#endif`nint32 main() { return EngineValue() * 10 + GAME_MODE + g_seen; }"
        "int/projects.toml" = "root = ""Game""`n[[project]]`nname = ""Game""`nsources = [""../Game/src""]`ndepends = [""Engine""]`n[project.defines]`nGAME_MODE = ""7""`n[[project]]`nname = ""Engine""`nsources = [""../Engine/src""]`n[project.defines]`nENGINE_FAST = ""1"""
    }
    $exe = Join-Path $dir "int/game.exe"
    $build = Invoke-Compiler $compiler @("--projects", (Join-Path $dir "int/projects.toml"), "-o", $exe)
    if ($build.exit -ne 0) { return @("the two-project build failed: $($build.output)") }
    & $exe | Out-Null
    if ($LASTEXITCODE -ne 17) { return @("the projects did not each get their own defines: exit $LASTEXITCODE (17 expected: engine 1*10, game 7, no engine define in the game)") }
    # One program: at -O2 the engine's function is inlined into the game's main, which then calls nothing.
    $object = Join-Path $dir "int/inlined.exe"
    $optimized = Invoke-Compiler $compiler @("--projects", (Join-Path $dir "int/projects.toml"), "-O2", "-c", "-o", $object)
    if ($optimized.exit -ne 0) { return @("the -O2 object did not build: $($optimized.output)") }
    $objdump = Join-Path (Split-Path -Parent $repoRoot) "vendor/llvm/bin/llvm-objdump.exe"
    $main = (& $objdump -d --no-show-raw-insn (Join-Path $dir "int/inlined.obj") | Out-String) -split "`r?`n`r?`n" | Where-Object { $_ -match "<main>:" }
    if (-not $main -or $main -match "call") { return @("at -O2 the game's main still calls the engine: $main") }
    return @()
}

# A project's `includes` folder reaches CBind and the foreign C compile (BUG-317), and a relative --int-dir still gets
# bindings (BUG-318): the build runs from the project folder with `--int-dir obj`.
function Test-ProjectInterop {
    $dir = New-Sources "projects-interop" @{
        "vendor/inc/vendor.h" = "typedef struct Bar { int b; } Bar;`n#define VENDOR_SCALE 3"
        "app/src/bar.h" = "#include ""vendor.h""`nint bar_get(const Bar* bar);"
        "app/src/bar.c" = "#include ""bar.h""`nint bar_get(const Bar* bar) { return bar->b * VENDOR_SCALE; }"
        "app/src/main.cf" = "extern ""c"" include ""bar.h""`nextern ""c"" source ""bar.c""`nint32 main() { Bar bar; bar.b = 14; return bar_get(&bar); }"
        "app/projects.toml" = "root = ""App""`n[[project]]`nname = ""App""`nsources = [""src""]`nincludes = [""../vendor/inc""]"
    }
    Push-Location (Join-Path $dir "app")
    try { $build = Invoke-Compiler $compiler @("--projects", "projects.toml", "-o", "out/app.exe", "--int-dir", "obj") }
    finally { Pop-Location }
    if ($build.exit -ne 0) { return @("the build with a vendored include folder and a relative --int-dir failed: $($build.output)") }
    & (Join-Path $dir "app/out/app.exe") | Out-Null
    if ($LASTEXITCODE -ne 42) { return @("the bound function returned $LASTEXITCODE, not 42") }
    return @()
}

# The rules of a build description: a project reaches only what it depends on, a path two dependencies have is
# ambiguous, and a broken description names its line or its problem.
function Test-ProjectBoundaries {
    $dir = New-Sources "projects-boundaries" @{
        "Engine/src/engine.ch" = "int32 EngineValue();"
        "Engine/src/engine.cf" = "include ""engine.ch""`nint32 EngineValue() { return 1; }"
        "Game/src/player.ch" = "struct Player { public int32 hp; }"
        "Game/src/main.cf" = "include ""engine.ch""`ninclude ""player.ch""`nint32 main() { return EngineValue(); }"
        "Tools/src/util.ch" = "int32 Util() { return 1; }"
        "Core/src/util.ch" = "int32 CoreUtil() { return 2; }"
        "int/game.toml" = "root = ""Game""`n[[project]]`nname = ""Game""`nsources = [""../Game/src""]`ndepends = [""Engine""]`n[[project]]`nname = ""Engine""`nsources = [""../Engine/src""]"
        "int/ambiguous.toml" = "root = ""Game""`n[[project]]`nname = ""Game""`nsources = [""../Game/src""]`ndepends = [""Tools"", ""Core"", ""Engine""]`n[[project]]`nname = ""Engine""`nsources = [""../Engine/src""]`n[[project]]`nname = ""Tools""`nsources = [""../Tools/src""]`n[[project]]`nname = ""Core""`nsources = [""../Core/src""]"
        "int/cycle.toml" = "root = ""Game""`n[[project]]`nname = ""Game""`nsources = [""../Game/src""]`ndepends = [""Engine""]`n[[project]]`nname = ""Engine""`nsources = [""../Engine/src""]`ndepends = [""Game""]"
        "int/typo.toml" = "root = ""Game""`n[[project]]`nname = ""Game""`nsoruces = [""../Game/src""]"
    }
    $expect = {
        param($what, $arguments, $message)
        $run = Invoke-Compiler $compiler $arguments
        if ($run.exit -eq 0 -or -not $run.output.Contains($message)) { $script:boundaryFailures += "$($what): expected '$message', got exit $($run.exit): " + ($run.output -split "`n" | Select-String "error" | Select-Object -First 2) -join " | " }
    }
    $script:boundaryFailures = @()
    $out = Join-Path $dir "int/x.exe"
    Set-Content (Join-Path $dir "Engine/src/bad.cf") "include ""player.ch""" -Encoding ascii
    & $expect "an engine file including a game header" @("--projects", (Join-Path $dir "int/game.toml"), "-o", $out) "cannot include 'player.ch': it is a file of project 'Game', and project 'Engine' does not depend on 'Game'"
    Remove-Item (Join-Path $dir "Engine/src/bad.cf")
    Set-Content (Join-Path $dir "Engine/src/holder.ch") "struct Holder { public Player* owner; }" -Encoding ascii
    & $expect "an engine header naming a game type" @("--projects", (Join-Path $dir "int/game.toml"), "-o", $out) "'Player' is declared in project 'Game', and project 'Engine' does not depend on 'Game'"
    Remove-Item (Join-Path $dir "Engine/src/holder.ch")
    Set-Content (Join-Path $dir "Game/src/main.cf") "include ""util.ch""`nint32 main() { return Util(); }" -Encoding ascii
    & $expect "a path two dependencies have" @("--projects", (Join-Path $dir "int/ambiguous.toml"), "-o", $out) "'util.ch' is a file of both 'Tools'"
    & $expect "a dependency cycle" @("--projects", (Join-Path $dir "int/cycle.toml"), "-o", $out) "the projects depend on each other in a cycle: Game -> Engine -> Game"
    & $expect "a misspelled key" @("--projects", (Join-Path $dir "int/typo.toml"), "-o", $out) "typo.toml: line 4: 'soruces' is not a key of a project"
    $both = Invoke-Compiler $compiler @($dir, "--projects", (Join-Path $dir "int/game.toml"))
    if ($both.exit -ne 2) { $script:boundaryFailures += "inputs with --projects exited $($both.exit), not 2" }
    return $script:boundaryFailures
}

# -g writes a .pdb beside the executable (B28): the program's functions, their parameters and locals, its globals and
# the lines of its source, as a Windows debugger reads them. Without -g there is none.
function Test-DebugInfoFlag {
    $dir = New-Sources "debuginfo" @{ "main.cf" = "int32 g_seed = 3;`nint32 Twice(int32 value) {`n`tint32 doubled = value * 2;`n`treturn doubled;`n}`nint32 main() {`n`tint32 result = Twice(g_seed);`n`treturn result - 6;`n}" }
    $exe = Join-Path $dir "app.exe"
    $build = Invoke-Compiler $compiler @($dir, "-g", "-o", $exe)
    if ($build.exit -ne 0) { return @("-g did not build: $($build.output)") }
    & $exe | Out-Null
    if ($LASTEXITCODE -ne 0) { return @("the -g program exited $LASTEXITCODE") }
    $pdb = Join-Path $dir "app.pdb"
    if (-not (Test-Path $pdb)) { return @("-g wrote no app.pdb") }
    $pdbutil = Join-Path (Split-Path -Parent $repoRoot) "vendor/llvm/bin/llvm-pdbutil.exe"
    $symbols = (& $pdbutil dump --symbols $pdb | Out-String)
    $lines = (& $pdbutil dump -l $pdb | Out-String)
    $globals = (& $pdbutil dump --globals $pdb | Out-String)
    $failures = @()
    foreach ($name in @("Twice", "value", "doubled", "result")) {
        if ($symbols -notmatch ("S_(GPROC32|LOCAL) \[size = \d+\] " + [regex]::Escape("``$name``"))) { $failures += "the .pdb does not describe $name" }
    }
    if ($globals -notmatch "S_GDATA32 .*``g_seed``") { $failures += "the .pdb has no g_seed" }
    if ($lines -notmatch "main\.cf" -or $lines -notmatch "(?<!\d)3 [0-9A-F]{8}") { $failures += "the .pdb has no line 3 of main.cf" }
    Remove-Item $pdb
    $plain = Invoke-Compiler $compiler @($dir, "-o", $exe)
    if ($plain.exit -ne 0 -or (Test-Path $pdb)) { $failures += "a build without -g wrote a .pdb" }
    return $failures
}

# BUG-356: a program in a folder whose path passes MAX_PATH builds and runs: its sources found, read and compiled.
function Test-DeepFolder {
    $base = [System.IO.Path]::GetFullPath((Join-Path $scratch "deep"))
    $deep = $base + "\" + ("d" * 120) + "\" + ("e" * 120)
    New-Item -ItemType Directory -Force -Path ("\\?\" + $deep) | Out-Null
    Set-Content -LiteralPath ("\\?\" + $deep + "\main.cf") -Value "int32 main() { return 7; }" -Encoding ascii
    $exe = Join-Path $scratch "deep.exe"
    $build = Invoke-Compiler $compiler @($deep, "-o", $exe)
    $failures = @()
    if ($build.exit -ne 0) { $failures += "a folder past MAX_PATH did not build: $($build.output)" }
    else {
        & $exe
        if ($LASTEXITCODE -ne 7) { $failures += "the program from the deep folder exited $LASTEXITCODE" }
    }
    Remove-Item -LiteralPath ("\\?\" + $base) -Recurse -Force -ErrorAction SilentlyContinue
    return $failures
}

# Platform::SourceLineOf: a program built with -g finds the line its own function starts on in its .pdb; without -g
# it knows no line.
function Test-SourceLines {
    $source = "include ""bedrock.ch""`nint32 Twice(int32 value) {`n`treturn value * 2;`n}`nint32 main() {`n" +
        "`tfuncptr<int32(int32)> twice = Twice;`n`tPlatform::SourceLine at = Platform::SourceLineOf((const void*)twice);`n" +
        "`tprintf(""%s:%d\n"", at.file.CStr(), at.line);`n`treturn twice(3) - 6;`n}"
    $dir = New-Sources "sourcelines" @{ "main.cf" = $source }
    $exe = Join-Path $dir "app.exe"
    $failures = @()
    $build = Invoke-Compiler $compiler @($dir, "-g", "-o", $exe)
    if ($build.exit -ne 0) { return @("-g did not build: $($build.output)") }
    $withLines = (& $exe | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { $failures += "the -g program exited $LASTEXITCODE" }
    if ($withLines -notmatch "main\.cf:[23]$") { $failures += "with -g, Twice is at '$withLines', not main.cf:2" }
    Remove-Item (Join-Path $dir "app.pdb") -ErrorAction SilentlyContinue
    $plain = Invoke-Compiler $compiler @($dir, "-o", $exe)
    if ($plain.exit -ne 0) { return $failures + @("the plain build failed: $($plain.output)") }
    $withoutLines = (& $exe | Out-String).Trim()
    if ($withoutLines -ne ":0") { $failures += "without -g, Twice is at '$withoutLines', not a line unknown" }
    return $failures
}

# The debug adapter over real stdio (B28 step 2): a program built with -g stops at its breakpoint with the right
# stack and locals, then runs to its end with its output and exit code; disconnect ends the adapter with 0.
function Test-DapAdapter {
    $source = "int32 g_seed = 3;`nint32 Twice(int32 value) {`n`tint32 doubled = value * 2;`n`treturn doubled;`n}`n" +
        "int32 main() {`n`tprintf(`"hello from the debuggee\n`");`n`tint32 result = Twice(g_seed);`n`treturn result - 6;`n}"
    $dir = New-Sources "dapadapter" @{ "main.cf" = $source }
    $exe = Join-Path $dir "app.exe"
    $build = Invoke-Compiler $compiler @($dir, "-g", "-o", $exe)
    if ($build.exit -ne 0) { return @("the program did not build: $($build.output)") }
    $adapter = Join-Path (Split-Path -Parent $repoRoot) "build/bin/Development-windows-x64/C4DAP/C4DAP.exe"
    $failures = @(python (Join-Path $root "StdioClient.py") dap $adapter $exe (Join-Path $dir "main.cf"))
    if ($LASTEXITCODE -ne 0 -and $failures.Count -eq 0) { $failures += "the client exited $LASTEXITCODE" }
    return $failures
}

# The language server over real stdio: initialize, a malformed frame it drops (BUG-202), shutdown and exit 0.
function Test-LspStdio {
    $server = Join-Path (Split-Path -Parent $repoRoot) "build/bin/Development-windows-x64/C4LSP/C4LSP.exe"
    $failures = @(python (Join-Path $root "StdioClient.py") lsp $server)
    if ($LASTEXITCODE -ne 0 -and $failures.Count -eq 0) { $failures += "the client exited $LASTEXITCODE" }
    return $failures
}

# -march / -m pick the processor (D-62): its SIMD_* macros, the target on every function, and the same flags for a
# foreign source and the header CBind binds, so C4 and C agree on it; a name that selects nothing is a usage error.
function Test-TargetFlags {
    $failures = @()
    $macros = @"
int32 main()
{
#if !SIMD_AVX2 || SIMD_FMA || SIMD_FLOAT_LANES != 8
	return 1;
#endif
	return 0;
}
"@
    $result = Invoke-Program "target-macros" $macros @("-march=x86-64-v3", "-mno-fma")
    if ($result -is [string]) { $failures += $result } elseif ($result -ne 0) { $failures += "-march=x86-64-v3 -mno-fma gave the wrong SIMD_* macros" }
    $dir = New-Sources "target-ir" @{ "main.cf" = $hello }
    $ir = Invoke-Compiler $compiler @($dir, "-o", (Join-Path $dir "app.exe"), "-S", "-march=znver4")
    $text = if (Test-Path (Join-Path $dir "app.ll")) { Get-Content (Join-Path $dir "app.ll") -Raw } else { "" }
    if ($ir.exit -ne 0 -or -not $text.Contains('"target-cpu"="znver4"') -or -not $text.Contains("+avx512f")) { $failures += "-march=znver4 is not on the functions in the IR" }
    $cpu = Invoke-Compiler $compiler @("x", "-march=pentium-turbo")
    if ($cpu.exit -ne 2 -or $cpu.output -notmatch "names no x86-64 processor") { $failures += "an unknown -march: exit $($cpu.exit), '$($cpu.output)'" }
    $feature = Invoke-Compiler $compiler @("x", "-mavx3")
    if ($feature.exit -ne 2 -or $feature.output -notmatch "names no processor feature: the features are") { $failures += "an unknown -m: exit $($feature.exit), '$($feature.output)'" }
    $native = @{
        "main.cf" = "extern ""c"" include ""probe.h""`nextern ""c"" source ""probe.c""`nint32 main() { return ProbeAvx2() + HeaderSawAvx2() == 2 ? 0 : 1; }"
        "probe.h" = "int ProbeAvx2(void);`n#ifdef __AVX2__`nint HeaderSawAvx2(void);`n#endif"
        "probe.c" = "#include ""probe.h""`nint ProbeAvx2(void) {`n#ifdef __AVX2__`n  return 1;`n#else`n  return 0;`n#endif`n}`nint HeaderSawAvx2(void) { return 1; }"
    }
    $foreign = New-Sources "target-foreign" $native
    $exe = Join-Path $foreign "app.exe"
    $build = Invoke-Compiler $compiler @($foreign, "-o", $exe, "-march=x86-64-v3")
    if ($build.exit -ne 0) { $failures += "the foreign program did not build: $($build.output)" }
    else { & $exe | Out-Null; if ($LASTEXITCODE -ne 0) { $failures += "the foreign source or the bound header did not see -march=x86-64-v3" } }
    return $failures
}

# No source file outside the build folders and installed packages (node_modules, BUG-266) is hidden by .gitignore, so a fresh checkout has every file the build needs (BUG-261: `output*` hid C4DAP's
# OutputPipes sources).
function Test-NoIgnoredSources {
    $patterns = @("*.cpp", "*.h", "*.hpp", "*.c", "*.cf", "*.ch", "*.py", "*.ps1")
    $ignored = @(git -C $repoRoot ls-files --others --ignored --exclude-standard -- @patterns 2>$null |
        Where-Object { $_ -notmatch '^(bin|bin-int|assets/build-output|\.claude)/' -and $_ -notmatch '__pycache__' -and
            $_ -notmatch '(^|/)node_modules/' })
    return @($ignored | ForEach-Object { "$_ is ignored by .gitignore" })
}

# -fno-strict-aliasing (D-64): the IR carries no alias tags (a default build does), and a pointer pun the optimizer
# cannot see through (the pointers come from volatile variables) reads what was written, at -O2.
function Test-StrictAliasingFlag {
    $failures = @()
    $pun = @"
float g_storage;
volatile uint32* g_bits;
volatile float* g_real;
int32 main()
{
	g_bits = (uint32*)&g_storage;
	g_real = &g_storage;
	uint32* bits = g_bits;
	float* real = g_real;
	*bits = 0u;
	*real = 1.0f;
	return *bits == 1065353216u ? 0 : 1;
}
"@
    $result = Invoke-Program "no-strict-aliasing" $pun @("-O2", "-fno-strict-aliasing")
    if ($result -is [string]) { $failures += $result } elseif ($result -ne 0) { $failures += "-fno-strict-aliasing did not keep a pointer pun working at -O2" }
    $dir = New-Sources "alias-ir" @{ "main.cf" = $pun }
    $strict = Invoke-Compiler $compiler @($dir, "-o", (Join-Path $dir "strict.exe"), "-S")
    $strictIr = if (Test-Path (Join-Path $dir "strict.ll")) { Get-Content (Join-Path $dir "strict.ll") -Raw } else { "" }
    if ($strict.exit -ne 0 -or -not $strictIr.Contains("!tbaa")) { $failures += "a default build has no alias tags in its IR" }
    $loose = Invoke-Compiler $compiler @($dir, "-o", (Join-Path $dir "loose.exe"), "-S", "-fno-strict-aliasing")
    $looseIr = if (Test-Path (Join-Path $dir "loose.ll")) { Get-Content (Join-Path $dir "loose.ll") -Raw } else { "" }
    if ($loose.exit -ne 0 -or $looseIr -eq "" -or $looseIr.Contains("!tbaa")) { $failures += "-fno-strict-aliasing left alias tags in the IR" }
    return $failures
}

# A program split into partitions (parallel code generation, roadmap 5.1) links and runs like the one-object build:
# a static global whose name a foreign C global also has, file-scope assembly, virtual calls, template instances,
# exceptions, thread_local, reflection and a global with a create, with -g and on one thread too, and by default
# (as many objects as its size is worth). -c writes one object, a one-line program stays one, and the counts are
# checked.
function Test-Partitions {
    $program = @'
extern "c" include "counter.h"
extern "c" source "counter.c"
include "bedrock.ch"

static int32 counter = 5;
static int32 Bump() { counter += 1; return counter; }

asm {
	".text";
	".globl c4_part_answer";
	"c4_part_answer:";
	"mov eax, 42";
	"ret";
}
[link("c4_part_answer")] int32 Answer();

class Shape { public virtual int32 Area() { return 0; } }
class Square : Shape { public int32 side; public int32 Area() override { return side * side; } }

template<typename T> T Twice(T v) { return v + v; }

void Raise(int32 v) { throw v; }

thread_local int32 t_calls = 0;
int32 Calls() { t_calls += 1; return t_calls; }

struct Item { public int32 weight; }

Core::String g_greeting = Core::String("partitioned");

int32 main()
{
	if (Bump() != 6) return 1;
	if (ReadCounter() != 7) return 2;
	if (Answer() != 42) return 3;
	Square square;
	square.side = 3;
	Shape* shape = &square;
	if (shape->Area() != 9) return 4;
	if (Twice<int32>(21) != 42 || Twice<float>(1.5f) != 3.0f) return 5;
	int32 caught = 0;
	try { Raise(7); } catch (int32 e) { caught = e; }
	if (caught != 7) return 6;
	Calls();
	if (Calls() != 2) return 7;
	if (!Reflection::FindType("Item").IsValid()) return 8;
	if (g_greeting != "partitioned") return 9;
	return 0;
}
'@
    $dir = New-Sources "partitions" @{ "src/main.cf" = $program; "src/counter.h" = "int ReadCounter(void);"; "src/counter.c" = "int counter = 7;`nint ReadCounter(void) { return counter; }" }
    $src = Join-Path $dir "src"
    $failures = @()
    foreach ($case in @(@{ name = "four"; flags = @("--partitions", "4"); objects = 4 }, @{ name = "debug"; flags = @("--partitions", "3", "-g"); objects = 3 },
        @{ name = "onethread"; flags = @("--partitions", "3", "-j", "1", "-O2"); objects = 3 }, @{ name = "default"; flags = @(); objects = 0 })) {
        $out = Join-Path $dir $case.name
        $build = Invoke-Compiler $compiler (@($src, "-o", (Join-Path $out "app.exe")) + $case.flags)
        if ($build.exit -ne 0) { $failures += "$($case.name): did not build: $($build.output)"; continue }
        & (Join-Path $out "app.exe") | Out-Null
        if ($LASTEXITCODE -ne 0) { $failures += "$($case.name): the program exited $LASTEXITCODE" }
        $objects = @(Get-ChildItem (Join-Path $out "app.objects") -Filter "*.obj" -ErrorAction SilentlyContinue).Count
        if ($case.objects -gt 0 -and $objects -ne $case.objects) { $failures += "$($case.name): $objects objects, expected $($case.objects)" }
    }
    $tiny = New-Sources "partitions-tiny" @{ "main.cf" = $hello }
    $small = Invoke-Compiler $compiler @($tiny, "-o", (Join-Path $tiny "app.exe"), "-O2")
    if ($small.exit -ne 0 -or @(Get-ChildItem (Join-Path $tiny "app.objects") -Filter "*.obj" -ErrorAction SilentlyContinue).Count -ne 1) { $failures += "a one-line program was not assembled as one object by default" }
    $pdbutil = Join-Path (Split-Path -Parent $repoRoot) "vendor/llvm/bin/llvm-pdbutil.exe"
    $pdb = Join-Path $dir "debug/app.pdb"
    $symbols = if (Test-Path $pdb) { (& $pdbutil dump --symbols $pdb | Out-String) } else { "" }
    if ($symbols -notmatch "``main``" -or $symbols -notmatch "``Raise``") { $failures += "the partitioned -g build's .pdb lacks main or Raise" }
    $one = Invoke-Compiler $compiler @($src, "-o", (Join-Path $dir "c/app.exe"), "-c")
    if ($one.exit -ne 0 -or @(Get-ChildItem (Join-Path $dir "c") -Filter "*.obj").Count -ne 1) { $failures += "-c did not write exactly one object" }
    foreach ($bad in @(@("-j", "0"), @("-j", "x"), @("-j65"), @("--partitions", "0"), @("--partitions", "-3"))) {
        $r = Invoke-Compiler $compiler (@($src) + $bad)
        if ($r.exit -ne 2 -or $r.output -notmatch "needs a count from 1 to 64") { $failures += "'$($bad -join ' ')': exit $($r.exit), '$($r.output)'" }
    }
    $c = Invoke-Compiler $compiler @($src, "-c", "--partitions", "2")
    if ($c.exit -ne 2 -or $c.output -notmatch "--partitions needs a link") { $failures += "-c with --partitions 2: exit $($c.exit), '$($c.output)'" }
    return $failures
}

# The object cache (roadmap 5.3): a rebuild of unchanged code reuses every object, an edit to one function
# reassembles only the partitions it touched and the program shows the edit, another -O level reuses nothing, the
# folder keeps only the latest build's objects, and -c writes no cache.
function Test-ObjectCache {
    $program = @'
include "bedrock.ch"
int32 Value() { return 1; }
int32 Doubled(int32 v) { return v * 2; }
int32 Tripled(int32 v) { return v * 3; }
int32 Squared(int32 v) { return v * v; }
int32 main()
{
	Core::String label = Core::String("cache");
	return label == "cache" ? Value() + Doubled(1) + Tripled(1) + Squared(2) : 100;
}
'@
    $dir = New-Sources "objectcache" @{ "src/main.cf" = $program }
    $src = Join-Path $dir "src"
    $exe = Join-Path $dir "out/app.exe"
    $cache = Join-Path $dir "out/app.objects"
    $failures = @()
    $build = { param([string[]]$extra) $r = Invoke-Compiler $compiler (@($src, "-o", $exe, "-v", "--partitions", "4") + $extra)
        $m = [regex]::Match($r.output, "in (\d+) object\(s\), (\d+) reused")
        return @{ exit = $r.exit; objects = [int]$m.Groups[1].Value; reused = [int]$m.Groups[2].Value; output = $r.output } }
    $first = & $build @()
    if ($first.exit -ne 0) { return @("the program did not build: $($first.output)") }
    & $exe | Out-Null
    if ($LASTEXITCODE -ne 10) { $failures += "the first build exited $LASTEXITCODE, expected 10" }
    if ($first.reused -ne 0) { $failures += "a first build reused $($first.reused) objects" }
    $again = & $build @()
    if ($again.reused -ne $again.objects -or $again.objects -ne 4) { $failures += "an unchanged rebuild reused $($again.reused) of $($again.objects) objects" }
    Set-Content -Path (Join-Path $src "main.cf") -Value ($program -replace "return v \* 3;", "return v * 30;") -Encoding ascii
    $edited = & $build @()
    & $exe | Out-Null
    if ($LASTEXITCODE -ne 37) { $failures += "after editing Tripled the program exited $LASTEXITCODE, expected 37 (a stale object was linked)" }
    if ($edited.reused -lt 1 -or $edited.reused -ge $edited.objects) { $failures += "editing one function reused $($edited.reused) of $($edited.objects) objects" }
    if (@(Get-ChildItem $cache -Filter "*.obj").Count -ne 4 -or @(Get-ChildItem $cache -Filter "*.tmp").Count -ne 0) { $failures += "the cache keeps more than the latest build's objects" }
    $optimized = & $build @("-O2")
    & $exe | Out-Null
    if ($optimized.exit -ne 0 -or $LASTEXITCODE -ne 37 -or $optimized.reused -ne 0) { $failures += "an -O2 build after -O0 reused $($optimized.reused) objects or exited $LASTEXITCODE" }
    $compileOnly = Invoke-Compiler $compiler @($src, "-o", (Join-Path $dir "c/app.exe"), "-c")
    if ($compileOnly.exit -ne 0 -or (Test-Path (Join-Path $dir "c/app.objects"))) { $failures += "-c made an object cache" }
    return $failures
}

# --incremental (D-76): each object optimized alone, so an edit reoptimizes only the objects it changes.
function Test-IncrementalBuild {
    $program = @'
include "bedrock.ch"
int32 Value() { return 1; }
int32 Doubled(int32 v) { return v * 2; }
int32 Tripled(int32 v) { return v * 3; }
int32 Squared(int32 v) { return v * v; }
int32 main()
{
	Core::String label = Core::String("incremental");
	return label == "incremental" ? Value() + Doubled(1) + Tripled(1) + Squared(2) : 100;
}
'@
    $dir = New-Sources "incremental" @{ "src/main.cf" = $program }
    $src = Join-Path $dir "src"
    $exe = Join-Path $dir "out/app.exe"
    $failures = @()
    $build = { param([string[]]$extra) $r = Invoke-Compiler $compiler (@($src, "-o", $exe, "-v", "-ftime", "--partitions", "4", "-O2") + $extra)
        $m = [regex]::Match($r.output, "in (\d+) object\(s\), (\d+) reused")
        return @{ exit = $r.exit; objects = [int]$m.Groups[1].Value; reused = [int]$m.Groups[2].Value; output = $r.output } }
    $first = & $build @("--incremental")
    if ($first.exit -ne 0) { return @("the program did not build: $($first.output)") }
    & $exe | Out-Null
    if ($LASTEXITCODE -ne 10) { $failures += "the first build exited $LASTEXITCODE, expected 10" }
    if ($first.output -notmatch "optimize\+codegen" -or $first.output -match "(?m)^\s+optimize\s") { $failures += "-ftime does not report the optimizer inside code generation" }
    $again = & $build @("--incremental")
    # Four partitions and the reflection image's module (D-82).
    if ($again.reused -ne 5 -or $again.objects -ne 5) { $failures += "an unchanged rebuild reused $($again.reused) of $($again.objects) objects" }
    Set-Content -Path (Join-Path $src "main.cf") -Value ($program -replace "return v \* 3;", "return v * 30;") -Encoding ascii
    $edited = & $build @("--incremental")
    & $exe | Out-Null
    if ($LASTEXITCODE -ne 37) { $failures += "after editing Tripled the program exited $LASTEXITCODE, expected 37 (a stale object was linked)" }
    if ($edited.reused -lt 1 -or $edited.reused -ge $edited.objects) { $failures += "editing one function reused $($edited.reused) of $($edited.objects) objects" }
    $whole = & $build @()
    & $exe | Out-Null
    if ($whole.exit -ne 0 -or $LASTEXITCODE -ne 37 -or $whole.reused -ne 0) { $failures += "a whole-program build after an incremental one reused $($whole.reused) objects or exited $LASTEXITCODE" }
    $compileOnly = Invoke-Compiler $compiler @($src, "-o", (Join-Path $dir "c/app.exe"), "-c", "--incremental")
    if ($compileOnly.exit -ne 2 -or $compileOnly.output -notmatch "--incremental needs a link") { $failures += "-c --incremental: exit $($compileOnly.exit), '$($compileOnly.output)'" }
    return $failures
}

# BUG-360: a filter reads standard input (Platform::InputLines, Platform::ReadAllInput), a line ending in "\r\n" or
# "\n" or nothing; the standard streams carry the bytes as written, so a "\n" written is one byte.
function Test-StandardInput {
    $lines = @'
include "bedrock.ch"
int32 main()
{
	Platform::InputLines lines;
	Core::String line;
	Core::StringBuilder out;
	int32 count = 0;
	while (lines.Next(line)) {
		out.Append("[");
		out.Append(line.View());
		out.Append("]\n");
		count += 1;
	}
	Platform::WriteOutput(out.View());
	printf("printf %d\n", count);
	return count;
}
'@
    $all = @'
include "bedrock.ch"
int32 main()
{
	Core::StringBuilder text;
	if (!Platform::ReadAllInput(text)) return -1;
	return (int32)text.ByteLength();
}
'@
    $failures = @()
    $dir = New-Sources "stdin" @{ "lines/main.cf" = $lines; "all/main.cf" = $all }
    $linesExe = Join-Path $dir "lines.exe"
    $allExe = Join-Path $dir "all.exe"
    foreach ($program in @(@{ src = "lines"; exe = $linesExe }, @{ src = "all"; exe = $allExe })) {
        $build = Invoke-Compiler $compiler @((Join-Path $dir $program.src), "-o", $program.exe)
        if ($build.exit -ne 0) { return @("the $($program.src) program did not build: $($build.output)") }
    }
    $inputFile = Join-Path $dir "input.txt"
    $output = Join-Path $dir "output.txt"
    [System.IO.File]::WriteAllBytes($inputFile, [System.Text.Encoding]::ASCII.GetBytes("one`r`ntwo`nthree"))
    cmd /c "`"$linesExe`" < `"$inputFile`" > `"$output`""
    if ($LASTEXITCODE -ne 3) { $failures += "the line reader read $LASTEXITCODE lines, expected 3" }
    $written = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($output))
    if ($written -ne "[one]`n[two]`n[three]`nprintf 3`n") { $failures += "the filter wrote '$($written -replace "`r", '\r' -replace "`n", '\n')'" }
    cmd /c "`"$allExe`" < `"$inputFile`""
    if ($LASTEXITCODE -ne 14) { $failures += "ReadAllInput read $LASTEXITCODE bytes, expected 14" }
    [System.IO.File]::WriteAllBytes($inputFile, [byte[]]@())
    cmd /c "`"$allExe`" < `"$inputFile`""
    if ($LASTEXITCODE -ne 0) { $failures += "ReadAllInput read $LASTEXITCODE bytes of an empty input" }
    return $failures
}

$checks = @("Test-ShippedBedrockMirrorsSource", "Test-DefineFlag", "Test-VersionFlag", "Test-UsageErrors", "Test-HelpListsEveryOption",
    "Test-OutputFlags", "Test-GcStatsFlag", "Test-TimePhasesFlag", "Test-ScriptHostFlag", "Test-FailedRelinkKeepsTheProgramsFiles", "Test-IncludeProject", "Test-LinkFlags", "Test-InstallOutsideRepository",
    "Test-HomeVariable", "Test-NukeToolchainProject", "Test-ProjectDefines", "Test-ProjectBoundaries", "Test-ProjectInterop", "Test-DebugInfoFlag", "Test-SourceLines", "Test-DeepFolder",
    "Test-NoIgnoredSources", "Test-LspStdio", "Test-DapAdapter", "Test-TargetFlags", "Test-StrictAliasingFlag", "Test-Partitions", "Test-ObjectCache", "Test-IncrementalBuild", "Test-StandardInput")
$passed = 0
$failed = 0
foreach ($check in $checks) {
    $failures = @(& $check)
    $name = $check.Substring(5)
    if ($failures.Count -eq 0) {
        Write-Host "  PASS  $name" -ForegroundColor Green
        $passed++
    } else {
        $failures | ForEach-Object { Write-Host "  FAIL  ${name}: $_" -ForegroundColor Red }
        $failed++
    }
}
$color = if ($failed -eq 0) { "Green" } else { "Red" }
Write-Host ("PASSED: {0}   FAILED: {1}   TOTAL: {2}" -f $passed, $failed, $checks.Count) -ForegroundColor $color
if ($failed -ne 0) { exit 1 }
exit 0
