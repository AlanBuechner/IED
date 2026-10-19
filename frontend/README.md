# frontend/ — web UI client (html/css/js)

The user-facing half of IED: an ordinary web app **served by the IED server** over HTTP. It runs in any browser — desktop, phone, another machine on the network. The server owns all state; this client is a thin, disposable view that reconnects and resumes (see `../docs/01`).

- No direct OS access — everything goes over the single multiplexed WebSocket (+ HTTP for simple ops).
- Must work at phone widths: responsive layout is a requirement, not a stretch goal.
- Monaco / xterm.js execute client-side; heavy lifting (LSP, builds, AI, git, docker) stays server-side.

Planned layout:

```
frontend/
├── index.html
├── css/
│   ├── theme.css      # ALL design tokens (grayscale palette + semantic accents) — docs/07. Single source of visual truth.
│   └── layout.css     # shell chrome: sidebar, toolbar, panels, bottom terminal dock
└── src/
    ├── shell/         # layout engine: sidebar icon strip, panel registry, docking/splits (no free-floating windows)
    ├── ws/            # typed client for the server WS protocol (+ reconnect/resume)
    └── components/    # app-level composition of ../shared/ui-kit/ parts
```

Monaco is vendored here and configured by the editor plugin's JS (LSP wiring lives in the plugin/server, not in Monaco config).
