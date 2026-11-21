# 21 - Distribution and bundled services

## Purpose

Resolve `docs/99` #8 (SearXNG packaging) and define how IED is installed, what it bundles, what it detects, and where it keeps data. Owner direction: "prefer bundled or if not possible auto install isolated. i want the distribution of this to be easy."

## Install shape

- **Per-user, no admin.** Installs to `%LOCALAPPDATA%\Programs\IED\`. Registry writes only under `HKCU` (PATH entry for `ied`, Explorer context menus, `.iedsln`/`.iedfed` association, optional Run key for start-on-login).
- **Two artifacts:**
  - `IED-<ver>-win-x64.zip` - portable (same layout; `ied.exe --register` performs the HKCU registrations on demand). First artifact built (M6).
  - `IED-Setup-<ver>.exe` - a small installer. Recommended: written in C4 itself (dogfooding; it only unpacks, registers, and creates shortcuts). Fallback if that is not ready: Inno Setup script built by a Nuke step.
- **Updates (P2):** release feed + signed packages; the server downloads in the background and swaps on the owner's restart (running work is never interrupted by an update).

### As built: `nuke install-ied`

Until the zip and the installer exist, a checkout installs itself: `nuke install-ied` (`nuke install-ied dist` for an optimized build) builds `ied-server`, `ied` and `ied-desktop`, then `tools/install/install_ied.py` copies them into `%LOCALAPPDATA%\Programs\IED\` with what the server serves and loads, in the source tree's layout (the server finds its home as the folder above it holding `shared/host-api/schemas`, and serves `frontend\` from there). Each program's PDB goes beside it when the build has one, and one the build no longer has is removed (a crash report names functions and lines with it, `docs/12` "Logs"). Only changed files are copied and files gone from the checkout are removed, and what the checkout's own work leaves in those trees - a `build/` folder (a plugin's sample built in place), a crate's `target/`, `__pycache__/` - is left out at any depth; a program that is running is renamed aside (`*.old`, removed by the next install) and replaced, so an open IED keeps running until it is restarted - and the install says to restart one running from the folder whenever anything changed, a plugin's manifest included, since its server read the plugins when it started (a desktop app reloading its page reaches that same server). The web search services come with it: setup first installs SearXNG, its packages, the private Python and Tor into the checkout when they are missing (`tools/setup/deps.toml`), and the install copies them into `vendor\` (`docs/29` "Install"); a library of them in use is renamed aside the same way. So does `vendor\pip` (the Python terminal's `pip()`, `docs/05` "Terminal profiles"), and the C/C++ plugin's tools: `vendor/llvm/bin/clangd.exe` with clang's builtin headers (`lib/clang/<version>/include`, which clangd finds beside itself) into `plugins\cpp\clangd\`, and CodeLLDB (setup's `codelldb` package, the release's `.vsix` pinned by SHA-256) - its `adapter\` and `lldb\` - into `plugins\cpp\codelldb\`; the mirror of `plugins\` leaves those two folders alone, and what the checkout lacks is named at the end ("without CodeLLDB (vendor/llvm, vendor/codelldb)"), its one-click install then offered by Settings > Languages. `install.json` says where it came from - `{source, commit, dirty, installedMs, bin}`: the checkout (forward slashes), its full commit, whether a tracked file had uncommitted changes, when, and the build folder - and is written last (renamed into place), so its presence means a complete install; an installed server reads it beside its home to know its checkout (the Developer settings page's Rebuild and relaunch). Last, `ied-desktop --register` puts IED in the user's Start menu: `%APPDATA%\Microsoft\Windows\Start Menu\Programs\IED.lnk` (target, working folder, icon, description; `IShellLinkW` from C4), and makes `.iedsln` and `.iedfed` files open in the installed IED (`HKCU\Software\Classes`, `docs/27`); `ied-desktop --unregister` undoes both. Then the install folder and its `bin\` go on the user's PATH (`HKCU`'s `Environment`, each added last and once, Explorer told with `WM_SETTINGCHANGE`), so a terminal opened afterwards runs `ied` and `nuke`. `--to <folder>` installs elsewhere, `--no-register` leaves the Start menu alone and `--no-path` the PATH (`nuke install-ied -- --no-register`). `python -I tools/install/install_ied.py --uninstall` takes IED out of the Start menu and the PATH and removes the folder, never while a program of it runs (a running program cannot be deleted, so the folder would be left half removed). Explorer context menus are still to come.

**Nuke comes with it** (owner, 2026-10-27: "nuke should be an installed command just like ied"). The install carries Nuke in the source tree's layout: `Nuke\nuke\` (the package) and `Nuke\templates\` (`nuke init`'s), with the launchers `bin\nuke.cmd` and `bin\nuke` (POSIX shells), which run it on the install's private Python (`vendor\python`) with the language drivers of the installed plugins (`plugins\<language>\nuke\`). With `bin\` on the PATH, `nuke` works in any folder of a terminal opened afterwards, and IED's Nuke tasks find it with `nuke.command` left as `nuke`. Like the programs, it is the checkout's Nuke as it was installed, and the next `nuke install-ied` brings what changed: a half-done edit of the checkout's `Nuke\` never reaches it. Two rules keep it the checkout's Nuke (`Nuke/nuke/installed.py`):

- **Its machine state is its checkout's.** Nuke's home - the folder of its toolchain store (`toolchains\`, Nuke/README section 8), its package cache (`.cache\packages`) and the LLVM release the C/C++ driver falls back on (`vendor\llvm`) - is, for the Nuke an install carries, the checkout `install.json` names (the install folder itself when that checkout no longer holds `Nuke\nuke`). So a workspace anywhere finds the `dev` toolchain the checkout's `nuke build` refreshes and the versions `nuke install-c4` installs, and the built-in `c4` plugin finds that same store through the installed `bin\nuke.cmd` (`docs/26` "The c4 plugin and its toolchains").
- **A checkout runs its own Nuke.** In a folder at or under one holding `Nuke\nuke` - an IED checkout or worktree; with `-w`, the folder it names - the installed Nuke runs that Nuke with the same words instead, on that checkout's private Python when it has one (else its own), and exits with its code. So `nuke build` in a checkout builds with the Nuke its manifests were written for, and work on Nuke itself runs as edited.

### Prebuilt releases on the owner's Gitea (`tools/dist`)

Owner, 2026-11-21: "a system to distribute my custom software like IED across my different computers including linux builds ... builds and single command install scripts ... to install it fully on ether windows or linux ... the pre built IED is for my local git server only". A machine without a checkout installs a built IED, Windows or Linux, with one command, from the owner's self-hosted Gitea on the LAN - never from GitHub, which keeps only the source (Gitea's `<owner>/IED` is a pull mirror of it). The server's address lives in each checkout's `.ied/local/dist.json` (`{server, owner, repo}`, or `DIST_SERVER`/`DIST_OWNER`), never in the repository; publishing signs in with what git already has for that server (`git credential fill`) or `DIST_TOKEN`.

```
irm <server>/api/packages/<owner>/generic/ied/latest/install.ps1 | iex          (Windows PowerShell)
curl -fsSL <server>/api/packages/<owner>/generic/ied/latest/install.sh | sh     (Linux)
```

- **Storage: Gitea's generic package registry**, not release attachments (capped at 100 MB by the server's settings; one IED release is ~1.5 GB of archives). A product is a package; each version (`<commit date>-<commit>`, e.g. `2026.11.21-c38038d0`) holds its archives, `SHA256SUMS` and `release.json` (uploaded last: its presence means a whole version); the version `latest` holds the newest release's `release.json` with the installers made for this server. Downloads need no sign-in (the owner is a public account on a LAN server), so a new machine needs nothing first. Identical archives are stored once (Gitea keeps package files by SHA-256), and `publish.py` keeps the 3 newest versions plus the one `latest` names (`promote` rolls back, `list`, `remove`, `commands`).
- **Product-neutral** (`tools/dist/publish.py`, `install.ps1`, `install.sh`): `release.json` says per platform the install folder, the archives and where each goes (`to`), what a new version leaves alone (`keep`), the PATH folders, the registration commands (Windows), the system packages, the programs `ldd` checks and the oldest glibc (Linux). Another program of the owner's ships the same way with its own packaging script and `publish.py publish <folder>`.
- **IED's release** (`python -I tools/dist/ied_release.py`): builds a commit in a release worktree (`../IED-release`, detached; vendor/ a junction, the submodules copied at the release's commits) - never the checkout, whose uncommitted work must not ship. Windows: `nuke build C4` (the worktree's own `dev` toolchain), then `ied-server ied ied-desktop flashbang`; Linux: the same in `tools/linux/builder`'s container (the build in the Docker volume `ied-release-linux-build`, `NUKE_TOOLCHAINS` inside it). Packages: Windows `ied-<v>-windows-x64.zip` (tools/install's install folder, with the private Python, the web search services, clangd and CodeLLDB), `c4-<v>-windows-x64.zip` (the `dev` toolchain, with the Linux sysroot for `--system linux`) and `msvc-<v>-windows-x64.zip` (the MSVC and Windows SDK kit, `vendor\msvc`, which the compiler finds above its toolchain and links Windows programs with - apart, because it rarely changes and an unchanged archive is never downloaded again); Linux `ied-<v>-linux-x64.tar.gz` (the server, `ied`, the web client, the plugins, Nuke) and `c4-<v>-linux-x64.tar.gz` (the `dev` toolchain with the Debian 11 sysroot: C4 programs link with no `-dev` package and run on glibc 2.31). It publishes the version, then installs it from the server into scratch folders - Windows here, Linux in a clean Debian 12 container - where the server must serve the web client and Nuke must build and run a new C4 program; only then does `latest` point at it.
- **The Windows installer** (PowerShell 5.1, no administrator): installs into `%LOCALAPPDATA%\Programs\IED` (`DIST_DIR` elsewhere) with Windows' own `curl.exe`, `tar.exe` and `robocopy` (by their whole paths: Git's Unix tools on the PATH have others). Each archive is checked against its SHA-256 before anything changes, unpacked beside the folder, and mirrored into its place (`robocopy /MIR`: only what changed, files gone from the release removed - but the other archives' folders, `toolchains\` and `.cache\`); a file a running program has loaded (IED's programs, C4LSP, a loaded DLL) is renamed aside first (`*.old`, removed by the next run), so a running IED keeps running and is restarted to use the new one. Then `ied-desktop --register` (Start menu, file types), the folder and `bin\` on the user's PATH (the value's kind kept, Explorer told), `ied-update.cmd`, and `install.json` last: `{product, name, version, commit, built, installedMs, dist: {server, owner, platform, archives: {<to>: sha256}}}` - no `source`, so Nuke's home is the install folder (its `toolchains\c4\dev` the release's toolchain) and the Developer page's Rebuild is off. A folder some other install made (a checkout's `nuke install-ied`: its `install.json` has no `dist`) is left alone unless `DIST_FORCE=1`. `DIST_VERSION`, `DIST_NO_PATH`, `DIST_NO_REGISTER`, `DIST_UNINSTALL=1`.
- **The Linux installer** (POSIX sh, then Python 3, which Nuke needs anyway; installed first if missing): installs into `~/.local/opt/ied`, the same way (files moved into place by rename, so a running program keeps its file), checks the programs with `ldd` and installs the missing libraries' packages (apt, dnf, pacman or zypper, with sudo - which asks itself), writes `~/.local/opt/ied/env` (the folder and `bin/` on the PATH) and a line sourcing it in `~/.profile`, `~/.bashrc` and zsh's files - not symlinks in `~/.local/bin`: Nuke's launcher and the c4 plugin find their home from where `nuke` is. It needs glibc 2.36 (Debian 12, Ubuntu 24.04 or later: the builder's). On a machine with a desktop (a session now, or `/usr/share/xsessions` / `wayland-sessions` offering one) it also installs what the desktop app loads when it starts - WebKitGTK 4.1 (the release's `desktopLibraries`, looked up with `ldconfig -p`, and `desktopPackages`) - and runs the release's `register`, `ied-desktop --register`: IED in the applications menu with its icon, `.iedsln` and `.iedfed` opening in it (`docs/27` "Linux"); uninstalling runs `unregister`, and `DIST_NO_REGISTER=1` skips it. A server without a desktop gets neither. `ied` opens a folder in the desktop app there (without a desktop session it prints the sign-in URL), `ied url --no-browser`, `ied status`, `ied stop`. The release's checks install it twice: headless in Debian 12 (sign-in, web client, a C4 program) and in a Debian 12 with only a virtual display, where the installer must bring WebKitGTK, the menu's entry must pass `desktop-file-validate`, and the desktop app must open IED signed in to the server it started (its screenshot kept beside the release's logs).

## On-disk layout

```
%LOCALAPPDATA%\Programs\IED\            (install, read-only at runtime)
  ied.exe                                CLI + launcher (talks to the server over the named pipe; opens folders in ied-desktop)
  ied-desktop.exe                        the desktop app: the web client in WebView2, starting the server (docs/27)
  ied-server.exe                         the server, a C4 script host
  ied-server.c4i                         its interface: what plugin code compiles against (docs/26 section 2)
  C4Script.dll                           C4's script compiler and JIT, loaded only from beside the exe (without it the server runs, and plugin code says why it cannot)
  ied-server.pdb, ied-desktop.pdb, ied.pdb   debug information when the build has it (-g): crash reports name functions and lines (docs/12 "Logs")
  install.json                           the checkout, commit and build installed (nuke install-ied)
  bin\nuke.cmd, bin\nuke                 Nuke's launchers; bin\ is on the PATH too, so `nuke` runs in any folder
  Nuke\nuke\, Nuke\templates\            Nuke, run on vendor\python with the plugins' drivers; its home is the checkout ("As built")
  frontend\                              the web client (served as is: no bundling step)
  plugins\                               built-in plugins: manifests, UI modules, C4 code (code\), Nuke drivers (nuke\); c4\ among them
    cpp\clangd\bin\clangd.exe, cpp\clangd\lib\clang\<v>\include\   clangd and clang's builtin headers (from vendor\llvm; ~49 MB + 16 MB)
    cpp\codelldb\adapter\, cpp\codelldb\lldb\   CodeLLDB's adapter and its LLDB with LLDB's Python (~7 MB + 130 MB)
  shared\host-api\schemas\, shared\ui-kit\   the host API's schemas (how the server finds this folder), the ui-kit
  vendor\web\, third_party\codicons\     Monaco, xterm and the other web libraries; the codicons
  vendor\python\                         the private embeddable CPython that runs SearXNG and web_fetch's fetcher (docs/29), and the Python terminal (docs/05)
  vendor\pip\                            pip, for the Python terminal's pip() (packages go to %LOCALAPPDATA%\IED\plugins\ied.python\packages)
  vendor\searxng\searx\, vendor\searxng-packages\   SearXNG and its Python packages
  vendor\tor\                            the Tor Expert Bundle: tor\tor.exe, data\geoip[6]
  services\
    ripgrep\    rg.exe (planned)
  licenses\                              third-party notices

%APPDATA%\IED\                           (roaming: owner settings and content)
  settings.json, keybindings.json, modes\, skills\, plugins\ (installed third-party), prompts\ (overrides)
  chats\                                 generic (no-project) chats

%LOCALAPPDATA%\IED\                      (machine-local state)
  logs\ (server.log the master's, workspace-<key>.log each workspace server's, desktop.log: docs/12 "Logs"; security.log: docs/11), cache\ (indexes, symbol caches), trash\, services\ (SearXNG and tor runtime state), recent.json (recent projects and favorites, docs/10), trust.json, devices.json
  servers\<key>\                         a workspace server's own state: its plugins' state folders (the browser's Edge profile), its script cache (docs/01 "Processes")
  tools\<id>\<version>\                  language tools IED installed (docs/26 "Language tools": Go, gopls, Delve, CodeLLDB...)
  plugins\<id>\                           plugins' machine-local state (the Python terminal's packages)
```

Per workspace: `.ied/` (committed) and `.ied/local/` (gitignored) as in `docs/03`.

## Bundled vs detected

| Component | Decision | Why |
|---|---|---|
| Monaco, xterm.js, codicons, fonts | **bundled** (`web/`) | client assets, small |
| ripgrep | **bundled** | fast search is core; single exe, permissive license |
| SearXNG | **bundled** with an embeddable Python (see below) | plan: "packaged with this application" |
| tor | **bundled** (Tor Expert Bundle) | small, self-contained; managed process |
| Headless browser for AI browser tools and the Browser tab | **detected**: Microsoft Edge (always present on Windows 10/11) driven over CDP; Chrome as alternative (`browser.executable`) | no 150 MB Chromium bundle needed (`docs/27`) |
| WebView2 Runtime (the desktop app's engine) | **detected**: part of Windows 10/11 (Evergreen); without it `ied-desktop` offers the default browser | the SDK's static loader is linked into `ied-desktop.exe` |
| Video frame extraction | **Media Foundation** through a C shim (built into Windows); ffmpeg optional if the owner points to one | avoids an 80 MB bundle and LGPL packaging work |
| Window capture | Windows Graphics Capture API (shim) | built into Windows |
| RenderDoc (GPU debugger `rdc` backend) | **bundled**, pinned version: `renderdoc.dll` + the IED adapter process (`docs/22`); an installed RenderDoc is never required | MIT license; replay API is C++, so no Python ships with it |
| PIX, Nsight Graphics | **detected** for hand-off only ("Open in PIX", "Launch in Nsight") | closed tools, installed by the owner |
| Git | **detected** (Git for Windows); if missing, offer a one-click isolated install of MinGit into `%LOCALAPPDATA%\IED\services\git` | git CLI is the git backend (`docs/features/09-git.md`) |
| Docker | **detected** (Docker Desktop); sandbox shows setup guidance if absent | too large and system-level to bundle |
| Claude Code CLI | **detected** (`claude` on PATH) for the Claude bridge | the user installs and signs in themselves (policy, `docs/04`) |
| Model servers (llama.cpp etc.) | **never bundled** | the owner runs them; IED connects (first-run detection of common ports) |
| C/C++'s language server and debugger (clangd, CodeLLDB) | **bundled** in the `ied.cpp` plugin's folder (owner, 2026-10-26: "ship with all the c/c++ tools including a DAP and LSP as part of the language plugin") | clangd is the vendored LLVM's (Apache-2.0 with LLVM exception, statically linked, ~65 MB with its headers); CodeLLDB is MIT and carries LLDB (Apache-2.0 with LLVM exception) and Python (PSF), ~137 MB. `cpp.clangd.path` / `cpp.lldb.path` still name others |
| Other languages' compilers, language servers and debuggers (Go, gopls, Delve, rustup, cargo, rust-analyzer) | **detected**, with a **one-click install** (owner, 2026-10-26) into `%LOCALAPPDATA%\IED\tools` - downloads checked against pinned SHA-256s - or, asked first, where their makers install them for the user (rustup) | `docs/26` "Language tools": the version a workspace pins wins over the newest |
| Language toolchains that come with projects (C4) | **never bundled** | project-local plugins, vendored kits; the built-in `c4` plugin finds an installed C4 toolchain by itself - the `c4.toolchain` setting, the workspace's `nuke.toml`, the newest in Nuke's store (`docs/26` "The c4 plugin and its toolchains"). Nuke itself comes with the install, its store staying the checkout's ("As built: `nuke install-ied`") |

## SearXNG packaging (resolves `docs/99` #8)

SearXNG is a Python web app; it is not officially supported on Windows (its production deployment uses uWSGI, which does not run on Windows). Plan:

1. **Primary - embedded Python, no Docker:** ship the CPython *embeddable* distribution plus pre-built wheels for SearXNG's dependencies. The server starts SearXNG's WSGI app with a pure-Python/Windows-capable WSGI server (waitress) bound to `127.0.0.1:<random port>`, with a generated `settings.yml` (limiter off for loopback, JSON output enabled, outgoing proxy = Tor). **Spike done (2026-10-21), built:** the pinned commit runs on Windows under waitress with every dependency as a `win_amd64` wheel; `setup.bat` installs SearXNG's source, a hash-pinned set of its packages and the Tor Expert Bundle into `vendor/`, `nuke install-ied` copies them into the install, and the `ied.websearch` plugin (not the host) runs it - `docs/29`. The four uWSGI template files with Windows-illegal names are not needed at runtime.
2. **Fallback - isolated auto-install:** not needed (the spike succeeded); an owner's own SearXNG can be named instead (`websearch.instanceUrl`, `docs/29`).
3. **Degraded mode:** with no SearXNG, `web_search` returns a clear error and `web_fetch` still works; search can fall back to a single configured engine's HTML endpoint through the egress proxy (opt-in; rate-limit-prone).

Egress: SearXNG's outgoing requests are pointed at IED's egress proxy, so secure mode (tor/VPN) applies to search automatically (`docs/04`).

## tor

- `tor.exe` started on demand by the host with a generated `torrc` (SOCKS on a random loopback port, ControlPort with cookie auth, data dir under `%LOCALAPPDATA%\IED\services\tor`).
- Health: bootstrap percentage shown in Settings > Web > Search. As built by the `ied.tor` plugin, for web search only (owner): `docs/29`.
- `NEWNYM` (new circuit) on rate limiting/captcha detection, throttled per tor's rules.

## First run

1. `ied.exe` (or the Start-menu shortcut) starts `ied-server.exe` in the background and opens the browser with a one-time token (`docs/11`).
2. Setup page: optional owner password (required only for LAN access), then **model detection** - probes `127.0.0.1` on llama.cpp (8080), Ollama (11434), LM Studio (1234), vLLM (8000), plus any host the owner types (e.g. the DGX Spark's LAN address) - and creates a "default" model group from what it finds.
3. Offers to open a folder or solution, or start a generic chat.

## Licenses

Bundled third-party components are listed with their licenses in `licenses\` and in Help > About: Monaco (MIT), xterm.js (MIT), codicons (CC-BY-4.0 / MIT), ripgrep (MIT/Unlicense), SearXNG (AGPL-3.0 - shipped unmodified as a separate local process with its source location listed; confirm the obligations before any public release), Python (PSF), pip (MIT), tor (BSD-3), clangd (LLVM: Apache-2.0 WITH LLVM-exception), CodeLLDB (MIT) with LLDB (Apache-2.0 WITH LLVM-exception) and Python (PSF), fonts (OFL). `plugins/cpp/NOTICE.txt` names the C/C++ tools' licenses and where their texts are; their full texts must be in `licenses\` before a public release (the LLVM tarball setup installs carries none). Re-check before the first public release.
