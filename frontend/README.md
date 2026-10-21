# frontend/ - web client (html/css/js)

The user-facing half of IED: a web app **served by the IED server**, running in any browser - desktop, phone, another machine. The server owns all state; this client is a disposable view that reconnects and resumes (`docs/01`, `docs/10`). Design: `docs/15` ("The web client as built").

- Plain ES modules served as they are: no bundler, no npm. `index.html` loads `css/theme.css`, `/ui-kit/ui-kit.css`, `css/layout.css` and `src/main.js`.
- Everything goes over one multiplexed WebSocket per tab (+ HTTP for blobs/uploads). No direct OS access.
- Phone widths are first-class: device classes desktop / tablet / phone, each with its own server-stored layout.
- The shell owns no feature: every view comes from a plugin's UI module (`plugins/*/ui/`, `docs/02` "UI modules").

```
frontend/
+- index.html          the app; login.html + login.js sign a device in (docs/11)
+- css/
|  +- theme.css        ALL design tokens (grayscale ramp, semantic accents, syntax tokens) - docs/07
|  +- layout.css       the chrome: sidebar, menu bar, toolbar, banners, docking, status bar, toasts, palette, phone shell
+- src/
|  +- main.js          one Shell per tab
|  +- ws/              client.js: protocol v1 (hello/resume, requests + opIds + cancellation, topics + cursors, gaps,
|  |                   pings, backoff); identity.js: client id per tab, device id per browser, device class
|  +- shell/           shell.js composes: commands, keybindings (chords, when), menus, views, contributions,
|                      layouttree.js + layoutdoc.js (pure), layoutstore.js (server sync), dock.js (docking),
|                      desktop.js / phone.js (chrome), statusbar, banners, toasts, palette, notifications,
|                      workspaces, hostcommands.js + hostviews.js (the shell's own commands and tabs)
+- tests/              in-browser unit tests (harness.js, *.test.js; open /tests/index.html)
+- mocks/              static HTML mocks of the review screens (M0)
```

Run it against the mock (`python -I tools/mock-server/mock_server.py --static frontend`, then http://127.0.0.1:8765/) or the real server (`nuke build ied-server`, then `ied <folder>`). `nuke e2e` runs the lint, the unit tests and the end-to-end scenarios (`tests/e2e/`) at 1920x1080, 1366x768 and 390x844 against both; screenshots land in `build/e2e/`. `window.ied_shell` is the page's shell, for debugging and for those tests.
