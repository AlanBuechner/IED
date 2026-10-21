# Features: operations and distribution (OPS)

Owner: `src/host` + installer tooling. Design: `docs/21` (distribution and bundled services).

## Diagnostics and logs

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-OPS-1 | Structured server log (JSON lines, rotated) in `%LOCALAPPDATA%/IED/logs/`; viewable as the *IED* output source with level filter | P0 | US-OPS-2 |
| F-OPS-2 | LSP/DAP/MCP protocol trace toggles per server (off by default), into their output sources and optional files | P0 | US-OPS-2 |
| F-OPS-3 | Model request log = trajectory (F-AIR-64); summary per request in the *AI* output source (model, tokens, timings, finish reason, routing) | P0 | US-OPS-2 |
| F-OPS-4 | Diagnostics page (F-SRV-8) + "Collect diagnostics" bundle (logs, versions, settings with secrets redacted) | P1 | US-OPS-4 |
| F-OPS-5 | Client error reporting: uncaught browser errors sent to the server log with the client id | P0 | US-OPS-2 |

## Resilience

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-OPS-10 | Supervisor for every child process (LSP, DAP, MCP, SearXNG, tor, plugin backends, sandbox helpers): restart with backoff, crash count, give-up threshold with a notification | P0 | US-OPS-1 |
| F-OPS-11 | Worker isolation inside the server: each subsystem's worker entry wraps exceptions (an escaped exception in C4 kills the process), reports, and restarts the subsystem | P0 | US-OPS-1 |
| F-OPS-12 | Atomic writes for every persisted file (write temp + flush + rename, with Windows rename retry), journal + compaction for append logs | P0 | US-CHT-6 |
| F-OPS-13 | Recoverable trash for files deleted by the user or the AI (app-data store with list/restore/purge, size cap) | P0 | US-OPS-3 |
| F-OPS-14 | Disk usage view and cleanup for chats, trajectories, traces, trash, caches | P1 | - |

## Distribution

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-OPS-20 | Windows x64 installer (per-user, no admin): server exe, frontend assets, built-in plugins, bundled services, `ied` on PATH | P1 | US-DST-1, US-DST-2 |
| F-OPS-21 | Portable zip build (same layout, no registry changes) for dev/testing | P0 | US-DST-1 |
| F-OPS-22 | Bundled services: SearXNG (embedded Python + wheels), tor (Tor Expert Bundle), ripgrep; each versioned and replaceable in settings | P0 (ripgrep, SearXNG), P1 (tor) | US-DST-1 |
| F-OPS-23 | Explorer context menu + `.ied.sln`/`.ied.fed` file association + start-on-login option (installer) | P1 | US-DST-2 |
| F-OPS-24 | First-run setup in the browser: optional remote password, detect local model servers (llama.cpp :8080, Ollama :11434, LM Studio :1234, vLLM :8000) and LAN endpoints entered by the user, create a default group | P1 | US-DST-3 |
| F-OPS-25 | Self-update: check a release feed, download, verify signature, swap on restart (server keeps running work until the user restarts) | P2 | - |
| F-OPS-26 | Desktop app (`ied-desktop`, C4 + WebView2, `docs/27`): the web client in native windows that start the server, app-level shortcuts (Ctrl+W/T/N/Tab work), dark frame, kept placement, taskbar icon (owner, 2026-10-21) | P1 | US-DST-4 |
| F-OPS-27 | Dev-loop for IED itself: build IED with Nuke inside IED, restart the server into the new build while keeping clients connected (they reconnect and resume) | P1 | - |
