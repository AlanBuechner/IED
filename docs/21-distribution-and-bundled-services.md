# 21 - Distribution and bundled services

## Purpose

Resolve `docs/99` #8 (SearXNG packaging) and define how IED is installed, what it bundles, what it detects, and where it keeps data. Owner direction: "prefer bundled or if not possible auto install isolated. i want the distribution of this to be easy."

## Install shape

- **Per-user, no admin.** Installs to `%LOCALAPPDATA%\Programs\IED\`. Registry writes only under `HKCU` (PATH entry for `ied`, Explorer context menus, `.ied.sln`/`.ied.fed` association, optional Run key for start-on-login).
- **Two artifacts:**
  - `IED-<ver>-win-x64.zip` - portable (same layout; `ied.exe --register` performs the HKCU registrations on demand). First artifact built (M6).
  - `IED-Setup-<ver>.exe` - a small installer. Recommended: written in C4 itself (dogfooding; it only unpacks, registers, and creates shortcuts). Fallback if that is not ready: Inno Setup script built by a Nuke step.
- **Updates (P2):** release feed + signed packages; the server downloads in the background and swaps on the owner's restart (running work is never interrupted by an update).

### As built: `nuke install-ied`

Until the zip and the installer exist, a checkout installs itself: `nuke install-ied` (`nuke install-ied dist` for an optimized build) builds `ied-server`, `ied` and `ied-desktop`, then `tools/install/install_ied.py` copies them into `%LOCALAPPDATA%\Programs\IED\` with what the server serves and loads, in the source tree's layout (the server finds its home as the folder above it holding `shared/host-api/schemas`, and serves `frontend\` from there). Only changed files are copied and files gone from the checkout are removed; a program that is running is renamed aside (`*.old`, removed by the next install) and replaced, so an open IED keeps running until it is restarted. `install.json` names the commit and the build it came from. Last, `ied-desktop --register` puts IED in the user's Start menu: `%APPDATA%\Microsoft\Windows\Start Menu\Programs\IED.lnk` (target, working folder, icon, description; `IShellLinkW` from C4, no registry write), and `ied-desktop --unregister` takes it out. Then the install folder goes on the user's PATH (`HKCU`'s `Environment`, added last and once, Explorer told with `WM_SETTINGCHANGE`), so a terminal opened afterwards runs `ied`. `--to <folder>` installs elsewhere, `--no-register` leaves the Start menu alone and `--no-path` the PATH (`nuke install-ied -- --no-register`). `python -I tools/install/install_ied.py --uninstall` takes IED out of the Start menu and the PATH and removes the folder, never while a program of it runs (a running program cannot be deleted, so the folder would be left half removed). Context menus and associations are still to come.

## On-disk layout

```
%LOCALAPPDATA%\Programs\IED\            (install, read-only at runtime)
  ied.exe                                CLI + launcher (talks to the server over the named pipe; opens folders in ied-desktop)
  ied-desktop.exe                        the desktop app: the web client in WebView2, starting the server (docs/27)
  ied-server.exe                         the server, a C4 script host
  ied-server.c4i                         its interface: what plugin code compiles against (docs/26 section 2)
  C4Script.dll                           C4's script compiler and JIT, beside the exe (the exe does not start without it)
  install.json                           the commit and build installed (nuke install-ied)
  frontend\                              the web client (served as is: no bundling step)
  plugins\                               built-in plugins: manifests, UI modules, C4 code (code\), Nuke drivers (nuke\)
  shared\host-api\schemas\, shared\ui-kit\   the host API's schemas (how the server finds this folder), the ui-kit
  vendor\web\, third_party\codicons\     Monaco, xterm and the other web libraries; the codicons
  services\
    searxng\    python\ (embeddable CPython) + site-packages\ + searxng\ + settings.yml template
    tor\        tor.exe + geoip files (Tor Expert Bundle)
    ripgrep\    rg.exe
  licenses\                              third-party notices

%APPDATA%\IED\                           (roaming: owner settings and content)
  settings.json, keybindings.json, modes\, skills\, plugins\ (installed third-party), prompts\ (overrides)
  chats\                                 generic (no-project) chats

%LOCALAPPDATA%\IED\                      (machine-local state)
  logs\, cache\ (indexes, symbol caches), trash\, services\ (SearXNG and tor runtime state), recent.json (recent projects and favorites, docs/10), trust.json, devices.json
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
| Language toolchains (C4, Nuke, LSP/DAP) | **never bundled** | they come with projects (project-local plugins, vendored kits) |

## SearXNG packaging (resolves `docs/99` #8)

SearXNG is a Python web app; it is not officially supported on Windows (its production deployment uses uWSGI, which does not run on Windows). Plan:

1. **Primary - embedded Python, no Docker:** ship the CPython *embeddable* distribution plus pre-built wheels for SearXNG's dependencies in `services\searxng\`. The server starts SearXNG's WSGI app with a pure-Python/Windows-capable WSGI server (waitress, or SearXNG's own Flask entry for development) bound to `127.0.0.1:<random port>`, with a generated `settings.yml` (secret key, limiter off for loopback, JSON output enabled, outgoing proxy = IED's egress proxy). **This needs a spike** (M0/M10): confirm SearXNG master runs on Windows under waitress with its current dependency set (lxml, etc. have Windows wheels), and pin a working commit. The vendored tarball (`third_party/searxng`) is the starting point; the four uWSGI template files with Windows-illegal names are not needed at runtime.
2. **Fallback - isolated auto-install:** if the spike fails, IED installs SearXNG into a dedicated WSL distro or a Docker container on first use (owner's "auto install isolated"), managed the same way (start/stop/health), and says so in Settings > Egress.
3. **Degraded mode:** with no SearXNG, `web_search` returns a clear error and `web_fetch` still works; search can fall back to a single configured engine's HTML endpoint through the egress proxy (opt-in; rate-limit-prone).

Egress: SearXNG's outgoing requests are pointed at IED's egress proxy, so secure mode (tor/VPN) applies to search automatically (`docs/04`).

## tor

- `tor.exe` started on demand by the host with a generated `torrc` (SOCKS on a random loopback port, ControlPort with cookie auth, data dir under `%LOCALAPPDATA%\IED\services\tor`).
- Health: bootstrap percentage shown in Settings > Egress and the status bar while egress is `tor`.
- `NEWNYM` (new circuit) on rate limiting/captcha detection, throttled per tor's rules.

## First run

1. `ied.exe` (or the Start-menu shortcut) starts `ied-server.exe` in the background and opens the browser with a one-time token (`docs/11`).
2. Setup page: optional owner password (required only for LAN access), then **model detection** - probes `127.0.0.1` on llama.cpp (8080), Ollama (11434), LM Studio (1234), vLLM (8000), plus any host the owner types (e.g. the DGX Spark's LAN address) - and creates a "default" model group from what it finds.
3. Offers to open a folder or solution, or start a generic chat.

## Licenses

Bundled third-party components are listed with their licenses in `licenses\` and in Help > About: Monaco (MIT), xterm.js (MIT), codicons (CC-BY-4.0 / MIT), ripgrep (MIT/Unlicense), SearXNG (AGPL-3.0 - shipped unmodified as a separate local process with its source location listed; confirm the obligations before any public release), Python (PSF), tor (BSD-3), fonts (OFL). Re-check before the first public release.
