# frontend/ - web client (html/css/js)

The user-facing half of IED: a web app **served by the IED server**, running in any browser - desktop, phone, another machine. The server owns all state; this client is a disposable view that reconnects and resumes (`docs/01`, `docs/10`).

- Everything goes over one multiplexed WebSocket per tab (+ HTTP for blobs/uploads). No direct OS access.
- Phone widths are first-class (`docs/15`): device classes desktop / tablet / phone, each with its own server-stored layout.
- Monaco and xterm.js run client-side; LSP, builds, AI, git, docker and profiler analysis stay server-side.
- **This is the part that can be built before the C4 compiler is ready**: develop against `tools/mock-server/` (protocol v1) and verify with Playwright at 1920x1080, 1366x768 and 390x844 (`docs/18` M0, `docs/19`).

Planned layout:

```
frontend/
+- index.html
+- css/
|  +- theme.css      ALL design tokens (grayscale ramp, semantic accents, syntax tokens) - docs/07. Single source of visual truth.
|  +- layout.css     shell chrome: sidebar, menu bar, toolbar, docking, terminal dock, status bar, phone shell
+- src/
|  +- shell/         layout tree + docking, activity registry, command registry, keybindings (chords, when), menus, focus
|  +- ws/            protocol v1 client: hello/resume, topics + cursors, requests + cancellation, opId retries, gap resync
|  +- plugins/       UI module loader (contributions from manifests)
|  +- components/    app-level composition of ../shared/ui-kit/ parts
+- mocks/            static HTML mocks of the six review screens (M0)
+- tests/            in-browser unit tests run headless by Playwright
```

Monaco is configured by the editor plugin's UI module; its theme is generated from `theme.css` tokens.
