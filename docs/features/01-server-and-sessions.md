# Features: server and sessions (SRV)

Design: `docs/01` (architecture), `docs/10` (protocol), `docs/11` (security and multi-client).

## Server lifecycle

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SRV-1 | Standalone server process `ied-server`; runs with zero clients connected | P0 | US-SRV-1 | src/host |
| F-SRV-2 | Single-instance per user: a second launch (`ied ./`, double-click `.ied.sln`) hands its request to the running server over a local control channel and exits | P0 | US-SRV-3, US-WSP-3 | src/host |
| F-SRV-3 | Startup modes: foreground (console log), background (no console), start-on-login (Run key / scheduled task); Windows service is P2 | P1 | US-SRV-8 | src/host |
| F-SRV-4 | Tray icon: open UI, show status (running chats/builds), copy LAN URL, stop server, open logs | P1 | US-SRV-8 | src/host |
| F-SRV-5 | Graceful shutdown: refuses (with a list) while autonomous runs are active unless forced; persists all session state; kills child processes via job objects | P0 | US-OPS-1 | src/host |
| F-SRV-6 | Crash-safe restart: every long-lived thing (chats, agent runs, terminals' metadata, debug sessions' metadata, layouts) is rebuilt from disk on start; interrupted agent runs are marked `interrupted` (resume policy: F-AIR-36) | P0 | US-AGT-4 | src/host |
| F-SRV-7 | `ied` CLI: `ied <path>[:line[:col]]`, `ied --new-window`, `ied --wait <file>` (git editor), `ied diff a b`, `ied status`, `ied stop`, `ied url` | P0 (path), P1 (rest) | US-WSP-3 | src/host |
| F-SRV-8 | Health endpoint + diagnostics page: uptime, memory, GC stats, thread/queue depths, per-session ring-buffer usage, child process list | P1 | US-OPS-4 | src/host, frontend |

## Multi-workspace

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SRV-10 | Server hosts N open workspaces at once; each has its own plugin set (project-local plugins), LSP pool, terminals, chats | P1 (P0: one at a time, but the data model is N from day one) | US-SRV-7 | src/host |
| F-SRV-11 | Workspace URL routing: `/w/<workspaceId>/...`; each browser tab views one workspace; deep links to a file/line or a chat | P0 | US-SRV-3 | frontend, src/web |
| F-SRV-12 | Idle workspace policy: a workspace with no clients and no running work can unload its LSPs after N minutes (setting), reloading on demand | P2 | - | src/host |

## Sessions, resume and real-time

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SRV-20 | One multiplexed WebSocket per client; typed frames (request/response, events by topic, streams, cancellation) | P0 | US-SRV-1 | src/web, shared/host-api |
| F-SRV-21 | Per-topic sequence numbers + server ring buffers; resume-from-seq on reconnect; snapshot fallback when the gap is too old | P0 | US-SRV-1 | src/web |
| F-SRV-22 | Backpressure: per-client send budget; floods (build output, terminal) are coalesced, never block the producer, never drop silently (gap -> resync) | P0 | US-SRV-1 | src/web |
| F-SRV-23 | Client UI state on the server: open documents, layout, scroll positions, toolbar selections - keyed by workspace + device class (desktop / tablet / phone) | P0 | US-SHL-7 | src/host |
| F-SRV-24 | Presence: list of connected clients (device name, class, last activity); shown in the status bar and settings | P1 | US-SRV-5 | src/web, frontend |
| F-SRV-25 | Revision-checked mutations everywhere state can be contended (files, debug controls, chat sends, layout); conflict -> clear UI choice, never last-writer-wins silently | P0 | US-EDT-15 | src/* |

## Access and devices

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SRV-30 | Default bind `127.0.0.1`; LAN bind is opt-in in settings with a warning | P0 | US-SRV-4 | src/web |
| F-SRV-31 | Localhost hardening: Host-header allowlist, Origin check on WS upgrade and state-changing HTTP, per-launch token in the URL the CLI opens (exchanged for a cookie) | P0 | US-SRV-4 | src/web |
| F-SRV-32 | Owner account: password (Argon2id), first-run setup code printed to the console/tray, rate-limited login, 30-day device sessions | P0 (required before LAN bind is allowed) | US-SRV-4 | src/web |
| F-SRV-33 | Device list with revoke; new-device sign-in notification to other devices | P1 | US-SRV-5 | src/web, frontend |
| F-SRV-34 | Built-in TLS (self-signed CA generated per install, with QR code to install on the phone) - optional; documented Tailscale/WireGuard path is the recommended remote route | P2 | US-SRV-10 | src/web |
| F-SRV-35 | Optional TOTP second factor for non-loopback logins | P2 | US-SRV-10 | src/web |

## Notifications

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SRV-40 | Notification service: events (approval needed, run finished/failed/blocked, build finished, plugin crashed) with severity, workspace, deep link | P0 | US-SRV-6, US-SHL-11 | src/host |
| F-SRV-41 | In-app toasts + notification center with history (survives reconnect) | P0 | US-SHL-11 | frontend |
| F-SRV-42 | Browser notifications (Notification API) while a tab is open but hidden; optional sound | P1 | US-SRV-6 | frontend |
| F-SRV-43 | Web Push (service worker) to the phone when no tab is open - requires HTTPS (F-SRV-34 or Tailscale); falls back to ntfy-style webhook target configured in settings | P2 | US-SRV-6 | src/web, frontend |
| F-SRV-44 | Notification rules: per event kind on/off, quiet hours, "only when no client active" | P2 | US-SRV-6 | src/host |

## Port forwarding (preview from any device)

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SRV-50 | Reverse proxy `/w/<ws>/port/<n>/...` to `localhost:<n>` on the server machine, with WebSocket passthrough; same auth as IED | P1 | US-SRV-9, US-EDT-10 | src/web |
| F-SRV-51 | Auto-detect listening ports opened by processes IED launched (toolbar runs, terminals, sandbox) and offer "open in browser pane" | P2 | US-SRV-9 | src/terminal, frontend |
