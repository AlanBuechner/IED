# src/ — native host (C4 sources, added when the compiler is ready)

The native half of IED: everything that needs OS access, performance, or long-lived processes. See `../docs/01-architecture-overview.md` for the module map. Planned layout:

```
src/
├── host/        # app lifecycle, window, webview embed (entry point)
├── ipc/         # IPC server + dispatch (protocol types in ../shared/host-api/)
├── settings/    # 3-tier settings store (app / solution / project) — docs/07
├── plugins/     # plugin discovery, validation, loading, trust, lifecycle — docs/02
├── jsruntime/   # vendored JS engine + host API bindings for JS plugins
├── lsp/         # generic LSP client pool (servers registered by plugins)
├── dap/         # DAP client pool (built-in debugger support)
├── ai/          # AI runtime: model groups, streaming, tool executor, compression, egress — docs/04
├── terminal/    # process supervisor: bottom shell, toolbar commands, AI shell tool
├── git/         # git plumbing for the git plugin
├── sandbox/     # docker container lifecycle — docs/05
└── fs/          # file watching + FS operations backing editor tree and AI tools
```

Nothing here may contain C4-language or Claymore-specific logic (AGENTS.md rule 1).
