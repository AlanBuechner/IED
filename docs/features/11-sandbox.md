# Features: sandbox (SBX)

Owner: `plugins/sandbox` (UI + AI tools) on `src/sandbox` (container/remote lifecycle).

The plan asks for "a sandbox that uses docker ... on another operating system ... like macOS". Two facts shape the design:

1. **Linux** in Docker is easy (Docker Desktop's Linux containers, WSL2 backend).
2. **macOS cannot legitimately run in Docker on PC hardware.** Docker-OSX-style images need a Linux KVM host and violate Apple's license outside Apple hardware. So "other OS" is generalized into **sandbox providers**: `docker` (Linux images), `wsl` (a dedicated WSL distro), and `ssh` (a remote machine - a Mac for macOS, the DGX Spark for ARM64 Linux, any build box). All three present the same interface to the UI and the AI.

Also: C4 itself is Windows-only today (Linux port is planned upstream, `docs/17`), so for C4 projects the sandbox becomes useful once the compiler targets Linux. Non-C4 projects benefit immediately.

## Providers and lifecycle

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-SBX-1 | Provider interface: create/start/stop/destroy, exec (streamed, PTY or pipes), copy in/out, port forward, status, resource stats | P1 | US-SBX-1 |
| F-SBX-2 | Docker provider: image per project from `.ied/sandbox/<name>/Dockerfile` or an image ref; build caching; Docker Desktop detection with a clear "Docker not available" state (never silently runs on the host) | P1 | US-SBX-2 |
| F-SBX-3 | Mounts: solution root bind-mounted (read-write) at `/workspace/<root>`; `.ied/` read-only; no host home, no SSH keys, no Docker socket; optional copy-in mode for full isolation | P1 | US-SBX-2 |
| F-SBX-4 | Persistent volumes for toolchains/caches; reset levels (restart / recreate container / wipe volumes / rebuild image) | P1 | US-SBX-2 |
| F-SBX-5 | Lazy start and idle stop (defaults: 4 CPU, 6 GiB, 30 min idle), configurable per sandbox | P1 | US-SBX-2 |
| F-SBX-6 | Setup script (`.ied/sandbox/<name>/setup.sh`) runs only after the owner approves its content hash | P1 | US-SBX-2 |
| F-SBX-7 | WSL provider (dedicated distro imported from a rootfs) | P2 | US-SBX-3 |
| F-SBX-8 | SSH provider: host from app-tier SSH config, sync strategy (rsync over SSH or git push to a bare remote), remote exec | P2 | US-SBX-3 |

## Network

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-SBX-10 | Egress deny-by-default via a gateway container; grants are host:port, revocable live; private/loopback/metadata ranges blocked, DNS rechecked | P1 | US-SBX-2 |
| F-SBX-11 | Grant requests from the AI surface as approval cards (F-CHT-50) | P1 | US-CHT-14 |
| F-SBX-12 | Sandbox egress can chain through the workspace egress policy (tor/proxy) | P2 | US-WEB-2 |

## Integration

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-SBX-20 | Terminal profiles per sandbox (bottom terminal can open a shell inside it) | P1 | US-TRM-1 |
| F-SBX-21 | Task step target: a step may declare `target: sandbox:<name>` so a toolbar button ("Build (Linux)") runs inside it | P1 | US-SBX-1 |
| F-SBX-22 | AI tools (T-SBX-*): run command in sandbox, build/test via a named task with sandbox target, read files from the sandbox, request network grant | P1 | US-SBX-1 |
| F-SBX-23 | Problem matcher path mapping: container paths (`/workspace/...`) mapped back to host paths for Error List links | P1 | US-BLD-4 |
| F-SBX-24 | GUI desktop: optional image variant with Xfce + noVNC; viewed in a pane; take-control pauses AI GUI actions (Echo parity) | P2 | US-SBX-4 |
| F-SBX-25 | GPU: pass through NVIDIA GPU for compute (WSL2 CUDA); graphical apps render with software rasterizers (llvmpipe/lavapipe) - documented limitation | P2 | - |
