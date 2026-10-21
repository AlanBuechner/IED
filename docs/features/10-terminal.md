# Features: terminal (TRM)

Owner: `plugins/terminal` - its UI (xterm.js) and its server code (`code/`: terminals under a pseudo console, compiled into the server on the plugin API, `docs/26` "Built-ins on the API"). Tasks and the AI's shell run their processes themselves.

## Dock and instances

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-TRM-1 | Always-present bottom dock across all activities, full width minus sidebar; collapse/expand (Ctrl+`), drag to resize, maximize, height remembered per device class | P0 | US-SHL-2 |
| F-TRM-2 | Multiple terminal instances with a VS-style dropdown/tab selector; new, rename, kill, restart; split horizontally | P0 (instances), P1 (split) | US-TRM-1 |
| F-TRM-3 | Profiles: PowerShell 5.1, pwsh, cmd, Git Bash, WSL distros (auto-detected), sandbox shells (from the sandbox plugin), custom (argv + env + cwd) | P0 | US-TRM-1 |
| F-TRM-4 | Default cwd = active project's root (or workspace root); "Open in terminal" from the tree | P0 | US-TRM-1 |

## PTY supervisor (server)

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-TRM-10 | ConPTY-backed sessions on Windows (pty on Linux later), resize, UTF-8 | P0 | US-TRM-1 |
| F-TRM-11 | Sessions outlive the browser; scrollback kept server-side (bounded ring, e.g. 10k lines/5 MB) and replayed on attach; multiple clients can view, one types at a time (input lease) | P0 | US-TRM-2 |
| F-TRM-12 | Job objects so killing a terminal kills its process tree | P0 | US-BLD-5 |
| F-TRM-13 | Environment: inherits the server's env + workspace env overrides (`.ied/` env file, gitignored local part) | P1 | - |

## Integration

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-TRM-20 | Link detection: `file:line:col`, `file(line,col)`, URLs; Ctrl+click opens in editor/browser pane | P1 | US-TRM-4 |
| F-TRM-21 | Shell integration (OSC 133 / OSC 633 marks via injected PowerShell/bash prompt hooks): command boundaries, exit codes, cwd tracking, "copy last command output", navigate between commands | P1 | US-TRM-4 |
| F-TRM-22 | Find in terminal, copy/paste (Ctrl+Shift+C/V and right-click), select-all, clear | P0 | - |
| F-TRM-23 | AI terminals: each chat that runs shell commands gets its own named terminal ("AI: <chat title>"), so the human sees every command and its output live; the human can type into it only after pausing the agent | P0 | US-TRM-3 |
| F-TRM-24 | Background processes for the AI: long-running commands (dev servers, watchers) started detached in their own terminal with a handle the AI can read/kill later | P1 | US-TOOL-1 |
| F-TRM-25 | "Explain / fix with AI" on a terminal selection or the last failed command | P1 | US-INL-2 |
| F-TRM-26 | Saved commands (predecessor parity): named one-click commands per workspace | P2 | - |
| F-TRM-27 | Phone: terminal as a slide-up drawer with an extra key row (Esc, Tab, Ctrl, arrows) | P1 | US-SRV-2 |
