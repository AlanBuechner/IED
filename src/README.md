# src/ — IED server (C4 sources, added when the compiler is ready)

The native half of IED: a **standalone background server that owns all state**. The web client (`frontend/`) is just one view onto it — close the browser and everything keeps running (see `../docs/01`). Planned layout:

```
src/
├── host/        # server lifecycle, config, startup modes (foreground / background; service install later)
├── web/         # static asset serving + HTTP API + multiplexed WebSocket hub (protocol types in ../shared/host-api/)
├── settings/    # 3-tier settings store (app / solution / project) — docs/07
├── plugins/     # plugin discovery, validation, loading, trust, lifecycle — docs/02
├── jsruntime/   # QuickJS-ng runtime + host API bindings for JS plugin logic (third_party/quickjs-ng-v0.17.0)
├── lsp/         # generic LSP client pool (servers registered by plugins)
├── dap/         # DAP client pool (built-in debugger support)
├── ai/          # AI runtime: model groups, streaming, tool executor, compression, egress — docs/04
├── terminal/    # PTY supervisor: bottom shell, toolbar commands, AI shell tool (streams over WS)
├── git/         # git plumbing for the git plugin
├── sandbox/     # docker container lifecycle — docs/05
└── fs/          # file watching + FS operations backing editor tree and AI tools
```

Nothing here may contain C4-language or Claymore-specific logic (AGENTS.md rule 1).
