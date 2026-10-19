# frontend/ — web UI shell (html/css/js)

Everything the user sees and touches, running in an embedded webview. No direct OS access — all host capabilities come over IPC (`../shared/host-api/`). See `../docs/01` and `../docs/07`.

Planned layout:

```
frontend/
├── index.html
├── css/
│   ├── theme.css      # ALL design tokens (grayscale palette + semantic accents) — docs/07. Single source of visual truth.
│   └── layout.css     # shell chrome: sidebar, toolbar, panels, bottom terminal dock
└── src/
    ├── shell/         # layout engine: sidebar icon strip, panel registry, docking/splits (no free-floating windows)
    ├── ipc/           # typed client for the host IPC protocol
    └── components/    # app-level composition of ../shared/ui-kit/ parts
```

Monaco is vendored here and configured by the editor plugin's JS (LSP wiring lives in the plugin/host, not in Monaco config).
