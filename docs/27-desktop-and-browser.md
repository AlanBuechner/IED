# 27 - The desktop app and the browser

## Purpose

Two features built on one engine: the **desktop app** (`ied-desktop.exe`, F-OPS-26, US-DST-4), a native window that shows the same web client as any browser, and the **browser** (`plugins/browser`, F-EDT-93/94, US-EDT-10, T-BRW-1..8), web pages that live on the server so the AI and the user can render, look at and drive them together. Owner, 2026-10-21: "a desktop app as well ... effectively just a web browser that way we need just the one ui and so we can re use the web browser tech in the back end to allow for the AI to better work with web projects by rendering them as well as allowing the user to open a webpage in the IDE ... this client application should auto spawn the server if it is not already running ... the client should still be written in c4".

The engine is **Microsoft Edge's** (Chromium), which Windows 10 and 11 already carry twice: the Edge browser and the WebView2 Runtime. Nothing the size of Chromium is bundled. The desktop app hosts the web client in WebView2; the server drives Edge headless over the Chrome DevTools Protocol (CDP). Both are written in C4: the desktop app binds the WebView2 SDK header through CBind (`C4/docs/language/13-interop.md`), the server speaks CDP over Bedrock's WebSocket client.

## The desktop app (`desktop/`, `ied-desktop.exe`)

### What it is

A thin native shell: one top-level window per IED view, each holding a WebView2 control pointed at the user's `ied-server`. The web client inside is the one a browser gets, unchanged; there is no second UI. Closing a window closes a view, never work: chats, builds and terminals carry on in the server (`docs/01`).

```
ied-desktop.exe  (C4, one process, many windows)
  +- launcher module        find the user's server on the control pipe, start ied-server.exe when none answers
  +- window                 Win32 top-level window: dark caption, placement kept, icon, title from the page
  +- webview                WebView2 environment (one per process) and a controller per window, COM in C4
  +- watch                  a thread that notices the server is gone and starts it again
```

Command line: `ied-desktop [<path>[:<line>[:<col>]]] [--control <name>] [--data <dir>] [--debug-port <n>]`, or `ied-desktop --register` / `--unregister`.

- With a path, the window opens it as `ied <path>` does (the control pipe's `open`); without one, it asks for `url` and opens IED's home.
- `--control` names another server's pipe (tests); `--data` moves the app's own state and the WebView2 profile (tests); `--debug-port` opens the WebView2's CDP port on loopback so a test can drive and screenshot the window (Playwright's `connect_over_cdp`).
- `--register` puts IED in the user's Start menu, a shortcut to this program (`%APPDATA%\Microsoft\Windows\Start Menu\Programs\IED.lnk`, made with `IShellLinkW` and saved through `IPersistFile`), so Windows key, "ied", Enter opens it; `--unregister` removes it. Both open no window and answer with the exit code (0 done, 1 not). `nuke install-ied` installs IED and registers the installed copy (`docs/21` "As built").

### Starting the server

The CLI's control-channel client moved into a C4 module, `launcher/` (namespace `Launch`), that `ied` and `ied-desktop` both compile in: connect to `Net::UserLocalName("ied")`, else start `ied-server.exe` (beside the program, or in the sibling bin folder of the build layout) detached with `--static <frontend>` when the source tree's web client is found, then wait for its pipe (15 s). The server outlives the app: it is not in the app's job and gets no console of the app's.

The sign-in is the loopback one of `docs/11`: each window asks the pipe for a fresh one-time URL (`url` or `open`), so the token never travels on a command line. The WebView2 profile keeps the session cookie, so a window that reloads stays signed in.

**The server dies while windows are open.** A watch thread probes the pipe every 2 s. When nobody listens (not merely a slow answer) it starts the server again, at most three times in a minute; then each window asks for a new URL and navigates only when the server's origin changed (it binds 8765 again unless that port was taken); otherwise the web client's own reconnect resumes the page (`docs/10`). After three failures the windows show the error page below. `ied stop` with windows open therefore restarts the server; close the windows first to stop it for good.

### The window

- Per-monitor DPI aware (v2, from the manifest); the WebView2 follows the monitor's scale.
- Dark caption (`DWMWA_USE_IMMERSIVE_DARK_MODE`) and, on Windows 11, caption in `--bg-0` (`#0e0e0e`), border in `--border-0` and title in `--text-0`, so the window frame meets the web client's menu bar; the window and the WebView2's default background are `--bg-0` too, so nothing flashes white while the page loads.
- Title: the page's (`<workspace> - IED`). Icon: resource 1 of the exe (`desktop/resources/ied.ico`), for the window, the taskbar and Explorer.
- Placement (position, size, maximized) of the last window closed is kept in `<LOCALAPPDATA>\IED\desktop\window.json` and used for the next one; a further window cascades 32 px from the newest; a placement on no monitor falls back to the primary one, centered.

### The page and its keys

- Browser accelerator keys are off (`AreBrowserAcceleratorKeysEnabled`), so F5, Ctrl+R, Ctrl+F, Ctrl+P, F12, Ctrl+Shift+I, Ctrl+W, Ctrl+T, Ctrl+N and Ctrl+Tab reach the web client: the "app window only" bindings of `docs/15` work here. Zoom by Ctrl+wheel is off (Monaco's own zoom keys stay), the status bar and default browser chrome are off; the default context menu stays where the page shows none (text fields).
- The app adds `window.iedDesktop` before the page's scripts: `{version, newWindow(), devTools(), openFolder(), openExternal(url)}`, carried by `chrome.webview.postMessage`. The shell sets the `desktopApp` context key when it finds it; the `desktop` plugin (`plugins/desktop`, UI only) contributes Window > New Window and Help > Developer Tools, shown only `when: desktopApp`.
- **File > Open Folder** (Ctrl+Shift+Alt+O) is the shell's own command (`shell.openFolder`, `docs/15`), not the plugin's: it is needed most where no workspace, and so no plugin, is loaded - IED's home shows it as a button, and the workspace switcher lists it. It calls `openFolder()`; the window posts itself a message (the picker is modal, so it does not run inside WebView2's event) and shows the system's folder picker (`IFileOpenDialog` in folder mode, owned by the window). The picked folder goes to the control pipe's `open`, as `ied <folder>` does, and the window navigates to the URL it answers: the folder opens in this window, Visual Studio's way, while the workspace shown before stays open on the server. Cancel changes nothing; a failure is a message box and the page stays. A browser has no Open Folder: it cannot pick a folder of the server's.
- **Navigation stays on the server.** A top-level navigation to another origin is cancelled and opened in the system's default browser; a new window for the server's origin (`window.open`, the New Window command) is a new IED window in the same process, any other is the default browser's. Permission requests from the server's origin for the clipboard and notifications are granted; everything else is the WebView2's default (ask).
- A renderer that crashes reloads; a WebView2 browser process that exits is created again.

### Failures shown to the user

| What | Shown |
|---|---|
| No WebView2 Runtime | a message box: IED needs the Microsoft Edge WebView2 Runtime (link), and the offer to open IED in the default browser instead |
| `ied-server.exe` missing or not starting | an error page in the window (IED's colors): what failed, the server's own words, Retry |
| The server keeps dying | the same page after the third restart in a minute |

### Building and shipping

`desktop/nuke.toml` is an executable project depending on the `launcher` module, with `subsystem = "windows"` (Nuke passes the compiler's `--subsystem windows`: no console window, `main` stays the entry). `desktop/native.ch` binds `desktop/native/webview.h` (which includes `<windows.h>`, `<objbase.h>`, `<dwmapi.h>` and the SDK's `WebView2.h`) through CBind; the COM objects WebView2 calls back on are C4 structs whose first field is their vtable of C4 function pointers (`desktop/com.ch`), kept by their window. The SDK's `WebView2LoaderStatic.lib` is linked in, and a pre-build step compiles `desktop/resources/ied.rc` (the icon, `VERSIONINFO`, and a manifest asking for per-monitor DPI and common controls 6) with LLVM's `llvm-rc` into `ied.res`, which the link takes: `ied-desktop.exe` is all that ships. The SDK is the pinned `webview2` download of `tools/setup/deps.toml` (group `ied`, installed by `setup.bat`) into `vendor/webview2/`; the runtime is Windows'. The icon is `desktop/resources/ied.svg` rendered at ten sizes by `desktop/resources/make_icon.py`.

**Status (2026-10-21):** built and checked by hand: `ied <file>` with no server running starts the server and opens a window on the file; killing the server makes the watch start it again and the page reconnects; the dark frame, the icon and the kept placement; Window > New Window opens the same workspace; Help > Developer Tools; Ctrl+R stays with IED; a polite close saves the placement and the process ends with its last window. Building it found four C4 interop bugs, fixed by the C4 session (BUG-317..320: a project's include folders reach CBind, a bound `<windows.h>` no longer hides the prelude's `memmove`, COM interfaces get no destructor bindings, relative `--int-dir`), and Nuke now expands variables in `compiler-args` and `links`.

`ied` opens folders and files in the desktop app when `ied-desktop.exe` sits beside it; `ied --browser` uses the default browser, `--no-browser` prints the URL (unchanged).

## The browser (`plugins/browser`)

### Engine

The server starts Edge on first use: `msedge.exe --headless=new --remote-debugging-port=0 --remote-debugging-address=127.0.0.1 --user-data-dir=<data>\plugins\ied.browser\profile --no-first-run --no-default-browser-check --disable-extensions --disable-sync --disable-background-networking --mute-audio about:blank`, reads the port from the profile's `DevToolsActivePort` and connects one WebSocket to the browser endpoint (Bedrock's `Http::WebSocket`), with sessions flattened onto it (`Target.attachToTarget {flatten: true}`). The process runs in the server's job and ends with the server; a browser that exits is started again on the next use. The executable is the `browser.executable` setting, else the first of Edge (`Program Files (x86)`, `Program Files`, `LOCALAPPDATA`) and Chrome found. The profile is IED's own, in the plugin's state folder of the server's data folder (`IED::PluginContext::StateFolder`), never the user's Edge profile, so two servers (a test's beside the user's) never share one.

The code (`plugins/browser/code`, compiled into the server): `cdp` (the connection: ids, replies, events, one reader thread), `engine` (finding and starting the browser), `page` and `store` (a page's state and `pages.json`), `browser` and `events` (the service: contexts, loading, frames, what the browser tells), `actions` (input, snapshots, screenshots, acting, eval), `methods`, `bindings` and `tools`.

Each workspace gets its own browser context (`Target.createBrowserContext`), so cookies and storage of one workspace's pages are invisible to another's.

### Pages

A page is server state with an id (`pg-` and 12 hex digits), owned by a workspace: its URL, title, loading state, history flags, viewport (CSS width, height, device scale), who opened it (`user` or `ai`), a ring of its last 500 console messages and last 200 failed requests (a network error or a status of 400 and up). The workspace's pages are kept in `.ied/local/browser/pages.json` (id, URL, title, viewport, opener), so a Browser tab outlives a server restart: its page is loaded again at its last URL the first time someone looks at it. The live CDP target exists only while the page is loaded.

Loading a page makes a target at `about:blank`, attaches, enables `Page`, `Runtime`, `Network`, `Log` and `Inspector`, adds the title reporter, sets the viewport (`Emulation.setDeviceMetricsOverride`), then navigates, so no event of the page's own load is missed. Headless browsers do not tell when a script sets `document.title`: a small script added to every document (`Page.addScriptToEvaluateOnNewDocument`) reports the top document's title through a CDP binding (`Runtime.addBinding`, `Runtime.bindingCalled`), and the worker reads it again after each load. JavaScript dialogs are accepted (the console says so); a crashed renderer unloads the page, which loads again when next shown.

A page a page opens (a `target="_blank"` link, `window.open`) becomes a page of its own in the same workspace (`Target.targetCreated` with an opener of ours): attached, with its opener's viewport and `opener`, its `parent` naming the page that opened it; `page.added` tells every window, which offers it in a toast with Show.

A page nobody has looked at (pulled a frame) and nobody has used (an AI tool, a person's input) for `browser.unloadAfterSeconds` (600; 0: never) leaves the browser: it stays listed and loads again at its last address when shown or used, its in-page state gone. The browser stops once no page has been loaded for that long, and starts again on the next use.

URLs allowed: `http`, `https`, `about:blank`, and `file:` inside the workspace's folder (no `..`); a bare host gets `http://` when it is local (`localhost:5173`, `127.0.0.1`) and `https://` otherwise; anything else (`javascript:`, `chrome:`, ...) is refused.

### Protocol

| | |
|---|---|
| `browser.open {wsId, url, width?, height?, scale?}` | a new page (`opener: user`); `{pageId}` |
| `browser.navigate {pageId, url}`, `browser.back`, `browser.forward`, `browser.reload`, `browser.stop {pageId}` | |
| `browser.resize {pageId, width, height, scale}` | the viewport in CSS pixels (shared: the last viewer to resize wins) |
| `browser.input {pageId, events: [...]}` | `{type: mouse, action: down\|up\|move\|wheel, x, y, button, buttons, clickCount, deltaX, deltaY, modifiers}`, `{type: key, action: down\|up, key, code, keyCode, text, modifiers}`, `{type: text, text}`, in page CSS pixels |
| `browser.frame {pageId, after}` | the newest screencast frame after seq `after`, waiting up to 10 s for one: `{seq, mime, data (base64 JPEG), width, height}` (the frame's CSS size), or `{seq: after}` when none came. A page sends frames only while someone pulls them (the screencast stops 5 s after the last pull), and a slow client simply pulls less often: flow control without a ring of stale images. The screencast starts again at the new size after a resize and after each new document (a navigation may move the page to another renderer) |
| `browser.close {pageId}` | |
| `browser.pages {wsId}` | the workspace's pages |
| topic `browser:<wsId>` | snapshot `{pages: [...]}`; events `page.added`, `page.changed {pageId, url, title, loading, canGoBack, canGoForward}`, `page.removed` |
| topic `page:<pageId>` | snapshot `{page, console, network, lastAction}`; events `changed` (the page's summary), `console {entries}`, `network {entries}`, `action {seq, by: ai, kind, label, x?, y?, box?}` (what the AI just did, for the viewers), `closed` |

The bindings (server bindings of `docs/02`, so other plugins can drive pages too): `browser.openPage`, `browser.navigatePage`, `browser.snapshot`, `browser.screenshot`, `browser.act`, `browser.console`, `browser.eval`, `browser.closePage`, each `{wsId, args}` with the tool's arguments, answering `{text, image?, isError?}`.

### The Browser tab

View > Web Browser (Ctrl+Alt+R; `ied.browser.open`, with a `url` argument from other plugins) opens a document tab `browser:<pageId>`; View in Browser on an `.html`, `.htm` or `.svg` file of the Solution Explorer opens it as a `file:` URL. The tab (`plugins/browser/ui`): a toolbar in the editor's density (back, forward, reload or stop, the address box, an `AI` badge on the AI's pages, the viewport menu - fit to the pane, the page's own size, or desktop, laptop, tablet and phone sizes - open in the system browser, and the console toggle with a count of errors), the frames it pulls while visible drawn to fit the pane at the frame's own aspect, and input forwarded: pointer and wheel in page pixels, special keys and shortcuts as key events, typed and pasted text (IME text included) through a hidden text area. In "fit" the page's viewport follows the pane's size and the device's pixel ratio, so the picture is sharp. The console panel lists console messages and failed requests, colored by level with the theme's semantic colors. Closing the tab closes its page (`ied.tabs.onClose`, `docs/02`: only in the window where the person closed it). View > Browser Pages... lists the workspace's pages - the AI's too - to open one in a tab. On a phone the tab is the same, with taps as clicks and the console without its location column.

**Watching the AI.** The AI's pages need no window and no viewer: they render headless whether or not anyone looks. When the AI opens one, every window of the workspace gets a toast with Show. Opening the page in a tab shows it live, and each AI action is drawn over the frames as it happens (the `action` events): a ring where it clicked, the box of the element it acted on, and a caption line under the toolbar (`AI: click button "Sign in"`, `AI: type textbox "Email"`), all in the theme's AI accent, kept on the picture as the pane resizes. The user can scroll, click and type in the same page meanwhile.

### AI tools

The plugin's `toolCalls` (`docs/13` T-BRW, as built), C4 functions that call the bindings above: `browser_open(url, width?, height?)` (waits for the load event, then for the page to stop loading, and names console errors), `browser_navigate(page, url)`, `browser_snapshot(page, mode: a11y|text|dom)`, `browser_screenshot(page, full_page?)`, `browser_act(page, action: click|type|press|select|scroll|hover, ref?, selector?, text?)` (says what it did, the page afterwards and new console errors), `browser_console(page, since?)`, `browser_network(page, since?)`, `browser_eval(page, js)`, `browser_close(page)`; classes `net` for open, navigate and act, `read` for snapshot, screenshot, console and network, `exec` for eval, `meta` for close. A system-message section says the browser exists, and the plugin's `browser` skill gives the workflow for checking a web change. Pages the AI opens are ordinary pages (`opener: ai`). The `a11y` snapshot is the accessibility tree as indented lines - role, name, value, states, and a ref per element (`button "Sign in" [focused] [ref=e12]`) that `browser_act` takes until the next snapshot; text runs a parent already names and markers are left out, and it is cut at 60 KB. A screenshot is a PNG in `.ied/local/browser/shots/` whose path the result names, with the image itself in the result (`IED::ToolResult.images`) for models that see images.

## Decisions

- One engine, Edge's, for the window and the AI's pages (no Chromium bundle, no CEF); the desktop is WebView2, the server Edge headless over CDP, both C4 - the desktop with no C++ shim, its COM callbacks written in C4.
- The in-IDE browser is server-rendered (frames pulled by the client, input sent back), not a native WebView2 laid over the page: it works in every client, phones included, survives closing the window, and is the same page the AI sees.
- The desktop app is optional and adds no feature the browser client lacks except the keys a browser reserves; it is a view like any browser tab.
- The browser's capabilities are server bindings first and AI tools second, so other plugins compose on them (hard rule 7).

## Tests

- `launcher/tests`, `desktop/tests` (`nuke test launcher ied-desktop`): locations, the control channel's lines, which URLs may leave IED; the desktop's command line, placement, navigation targets, its own pages and script, the restart budget, the Start menu's options and shortcut path.
- `tools/install/tests` (in `tools/selftest.py`): the installer's mirror (changed files copied, skipped folders left out, files gone removed) and a running program renamed aside and replaced.
- `tools/server-checks/tests/test_browser.py` (real Edge; skipped without one): a page's title, console and failed requests; frames and input; the AI's bindings end to end (snapshot refs, type, click, the action event, eval, screenshot, console, network); URL rules; pages across a restart; closing; and the tools in a chat turn against the fake model, the screenshot reaching a model that sees images as an `image_url` part.
- `tools/server-checks/tests/test_desktop.py` (with `--desktop` and `--cli`, in `nuke conformance`): `ied-desktop` with no server running starts one and its window signs in on the folder it was given (checked over the app's `--debug-port`); a killed server is started again and the window is back on it; Open Folder on IED's home shows the real folder picker (driven through its Win32 controls), a cancelled picker leaves the page, the home keeps its button across the phone and desktop layouts, and the picked folder opens in the same window.
- `frontend/tests/desktop.test.js`: Open Folder only inside the desktop app, first in File, its key, the home page's button asking the app.
- `tests/e2e` `browser_tab` (real server, desktop and phone): View in Browser, a click and keys reaching the page, its console, an AI action drawn over it. `frontend/tests/browser.test.js`: fitting, page coordinates, modifiers, typed addresses, file URLs.

## Open items

- One process for every window of the user (a second `ied-desktop` hands its request to the running one).
- Full DevTools for a server page in the desktop app (Edge serves its DevTools front end on the CDP port).
- The egress policy (`docs/04`) for the browser: `--proxy-server` to IED's egress proxy in secure mode.
- Downloads; file uploads (`DOM.setFileInputFiles`).
- A restarted server reopens nothing: workspace ids live for one server run, so after the watch starts the server again the windows show "No workspace is open" (and, when the server came back on another port because 8765 was taken, the window goes to its home). The server should keep which workspaces are open, with their ids, and open them again when it starts.
- A jump list of recent workspaces, start on login.
