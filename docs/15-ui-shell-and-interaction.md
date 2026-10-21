# 15 - UI shell and interaction design

## Purpose

Pin down what IED looks like and how it is operated - desktop and phone - so the frontend can be built without guessing. Visual Studio (proper, not VS Code) is the reference for density, window names, menus and keys; the plan adds a VS Code-like activity sidebar and an always-present bottom terminal. Theme rules: `docs/07`.

## Desktop layout

```
+----+------------------------------------------------------------------------------------+
|    | File  Edit  View  Git  Project  Build  Debug  Test  Analyze  Tools  Extensions  ... |  menu bar
| WS +------------------------------------------------------------------------------------+
| -- | [<] [>] | [Development v] [x64 v] [Game v] | [> Start v] [Build] [Test] [Profile] |  toolbar (project-driven)
| C  +--------------------+---------------------------------------------+-----------------+
| E  | Solution Explorer  |  Ring.cf  x | Ring.ch  x | App.cf          | Properties /    |
| G  |  [search........]  |  [Core v] [Ring v] [Push(T) v]               | tool windows    |
| P  |  v Engine          |  12 |	void Push(T item)                     | (right dock,    |
| ...|    > Core          |  13 |	{                                     |  auto-hide)     |
|    |    v Math          |  14 |		...                             |#|                 |
|    |       Vec3.cf      |                                          map |#|                 |
|    |  v Game            |                                          mode|#|                 |
|    |                    +---------------------------------------------+ |                 |
|    |                    | Output  [Show output from: Build v]  [clr]  | |                 |
|    |                    | Error List | Find Results | Call Stack ...  | |                 |
|    |                    | ========== Building: Game, Development ===  | |                 |
+----+--------------------+---------------------------------------------+-----------------+
| [>_ PowerShell v] [+] [split] [trash]                              [^ maximize] [v hide] |  terminal dock
| PS C:\Work\MyGame> nuke build                                                           |  (all activities)
+-----------------------------------------------------------------------------------------+
| Ready  |  AI: 2 running, 1 needs approval  |  Ln 13, Col 2  Tabs  UTF-8  CRLF  C4  | main | 2 devices |  status bar
+-----------------------------------------------------------------------------------------+
  ^ sidebar: WS = workspace switcher, C = chat, E = editor, G = git, P = profiler, ... ; settings gear at the very bottom
```

- **Sidebar** (~40 px): workspace switcher at top (icon of the current workspace, click = switcher menu), then activities in order: Chat, Editor, Git, then plugin activities (Profiler, GPU, Sandbox...). Bottom: notifications bell, settings gear.
- **Menu bar and toolbar** belong to the window (shared by all activities), like VS. The toolbar shows the active project's toolbar config; activities may add their own toolbar strips (e.g. the git view's Fetch/Pull/Push).
- **Activities** replace the center area. The Editor activity is VS-shaped: Solution Explorer left, document well center, tool windows docked bottom/right. The Chat activity is a document well of chat tabs (each chat optionally with its right breakdown panel).
- **Terminal dock** spans the full width under every activity (plan requirement), collapsible to a header row.
- **Status bar**: global state left, editor state right (F-SHL-5); color changes with state (blue while building, accent while debugging, red on failure) using semantic tokens.

## Chat activity

```
+----+-------------------------------------------------------------------------+------------------+
|    | [Port logging *] [Fix test 3/7] [General]  [+]                           | v Context  62%   |
|    +-------------------------------------------------------------------------+   system   4.1k  |
|    | You: port the logging subsystem from RefEngine                   10:02  |   AGENTS   2.0k  |
|    |                                                                         |   messages 38k   |
|    | > Thinking  (12 s, 1.2k tok)                                            |   tools    31k   |
|    | [code_symbols Log*]  ok 38 ms                                           | v Tasks  3/7     |
|    | [fs_read RefEngine/Log.cpp 1-200]  ok 4 ms                              |   [x] read ref   |
|    | [shell_run nuke build]  ok 18.2 s   exit 0                              |   [>] port sink  |
|    | [fs_edit Engine/src/Core/Log.cf]  awaiting approval                     |   [ ] tests      |
|    |  +-------------------------------------------------------------------+  | v Modified (4)   |
|    |  | diff ...                                    [Allow] [Always] [Deny]|  |   Log.cf  +120   |
|    |  +-------------------------------------------------------------------+  | v Scratchpad     |
|    | -- harness: continued after max_tokens (1x) --                          | v Sub-agents     |
|    +-------------------------------------------------------------------------+ v Run info        |
|    | [@ attach] message...                              [Queue] [Inject] [>] |                  |
|    | group: plan-claude/impl-local v  effort: high v  mode: Agent v  [lock]  |                  |
|    | 41.3k in / 3.9k out | 38.2 tok/s | TTFT 1.4 s | ctx [######----] 62%   |                  |
+----+-------------------------------------------------------------------------+------------------+
```

- Tab glyphs: `*` running, `!` needs approval, `3/7` task progress, dot = unread result.
- Approval cards and questions are inline and also aggregated in the notification center.
- Harness notes (nudges, continues, retries, routing) are thin grey rows - always visible, never hidden in a log.

## Phone layout (< 720 px)

```
+---------------------------+      +---------------------------+      +---------------------------+
| MyGame v        (2) [bell]|      | < Ring.cf          [...]  |      | < Changes (4)      [...]  |
+---------------------------+      +---------------------------+      +---------------------------+
| [Port logging *][Fix 3/7] |      | 12 	void Push(T item) |      | Engine/src/Core/Log.cf    |
|                           |      | 13 	{                 |      |  - old line               |
| You: port the logging ... |      | 14 		...           |      |  + new line               |
| > Thinking (12 s)         |      |                           |      |                           |
| [shell_run nuke build] ok |      |                           |      | [ < prev ]   [ next > ]   |
| +-----------------------+ |      |                           |      |                           |
| | fs_edit Log.cf        | |      |                           |      |                           |
| | [Allow] [Deny] [...]  | |      |                           |      |                           |
| +-----------------------+ |      |                           |      |                           |
| [message...]  [Inj] [>]   |      | [Build v]        [ > Run ]|      |                           |
+---------------------------+      +---------------------------+      +---------------------------+
| Chat | Code | Git | Out | =|      | Chat | Code | Git | Out | =|      | Chat | Code | Git | Out | =|
+---------------------------+      +---------------------------+      +---------------------------+
```

- Bottom nav replaces the sidebar (5 slots: Chat, Code, Git, Output, More - the More sheet lists plugin activities, terminal, settings, workspace switcher).
- One pane at a time; back arrow returns to the previous pane; swipe between chat tabs.
- The terminal is a slide-up drawer with an extra key row.
- Approval cards are big and thumb-reachable; notifications deep-link straight to them.
- The editor opens read-mostly (tap to edit); Monaco touch mode, no minimap, larger font.
- Tablet (720-1099 px): desktop structure with the sidebar collapsed to icons, tool windows auto-hidden, one side panel at a time.

## Docking model

- Each activity has a **layout tree**: splits (row/column with sizes) whose leaves are **tab groups**.
- Drag a tab: drop targets appear on the hovered group (left/right/top/bottom/center) and on the activity edges. Dropping in a center target joins the group; edges create a split. No floating windows (plan).
- Tool windows (Output, Error List, Find Results, Call Stack, Locals, Watch, Breakpoints, Test Explorer, Bookmarks, Task List, Document Outline, Git Changes...) are tabs in **dock regions** (left/right/bottom of the Editor activity) with VS auto-hide: unpinning a region collapses it to edge tabs that slide out on click.
- Chats can be docked into the Editor activity and editors into the Chat activity (F-SHL-14): a tab carries its kind; any group can host any kind.
- Layout state is server-side per workspace x device class (F-SHL-13). "Window > Reset Window Layout" restores the default for the activity.

## Keyboard (VS default scheme)

Browser constraint: in a normal browser tab, **Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Shift+T, Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PageUp/PageDown** are reserved by the browser and cannot be intercepted. F5, Ctrl+S, Ctrl+P, Ctrl+F, Ctrl+H, Ctrl+G, Ctrl+K chords, F-keys and most others can (`preventDefault` on keydown). Consequences:

- The default scheme avoids depending on reserved keys; where VS uses one (Ctrl+F4 close document is fine; Ctrl+Tab window switcher is not), IED binds an alternate (`Ctrl+Alt+Tab`... configurable) and shows a one-time tip.
- The **installed PWA** (Chrome/Edge "Install app") and the **desktop wrapper** (F-OPS-26) receive more shortcuts; the keymap marks bindings "full in app window" vs "browser tab".

Default bindings (subset; full table generated from the command registry):

| Area | Keys |
|---|---|
| General | Ctrl+Q feature search; Ctrl+T or Ctrl+, Go To All; Ctrl+Shift+P command list (alias); Ctrl+S save; Ctrl+Shift+S save all; Alt+F7 tool window switcher; Ctrl+` terminal |
| Navigate | F12 definition; Alt+F12 peek; Ctrl+F12 implementation; Shift+F12 references; Ctrl+- / Ctrl+Shift+- back/forward; Ctrl+G line; Ctrl+] matching brace; Ctrl+K, Ctrl+O header/source |
| Edit | Ctrl+K, Ctrl+C / Ctrl+K, Ctrl+U comment/uncomment; Ctrl+K, Ctrl+D format document; Ctrl+K, Ctrl+F format selection; Ctrl+R, Ctrl+R rename; Ctrl+. quick actions; Ctrl+D duplicate line; Alt+Up/Down move line; Ctrl+Space complete; Ctrl+Shift+Space parameter info; Ctrl+R, Ctrl+W whitespace; Ctrl+M, Ctrl+M toggle outlining; Ctrl+I inline AI |
| Find | Ctrl+F; Ctrl+H; Ctrl+Shift+F find in files; Ctrl+Shift+H replace in files; F3 / Shift+F3 next/prev |
| Build | Ctrl+Shift+B build; Ctrl+Break cancel; Ctrl+F5 run without debugging |
| Debug | F5 start/continue; Shift+F5 stop; Ctrl+Shift+F5 restart; F9 breakpoint; Ctrl+F9 enable/disable; F10 step over; F11 step into; Shift+F11 step out; Ctrl+F10 run to cursor; Ctrl+Alt+P attach |
| Bookmarks | Ctrl+K, Ctrl+K toggle; Ctrl+K, Ctrl+N / Ctrl+K, Ctrl+P next/prev |
| Chat | Ctrl+Alt+I focus chat; Esc stop run (in chat); Ctrl+Enter send (when Enter = newline); Alt+Enter inject |

## Context menus (examples of required groups)

- **Editor:** Quick Actions; Rename; Go To Definition / Declaration / Implementation; Peek Definition; Find All References; View Call Hierarchy | Breakpoint >; Run To Cursor | Cut / Copy / Paste | Comment / Uncomment; Format Selection; Fix Indentation | Git > Blame / History / Revert Selected Lines | AI > Explain / Fix / Document / Write Tests / Send to Chat | plugin groups.
- **Solution Explorer file:** Open; Open With...; Open in Split | Cut / Copy / Paste / Delete / Rename | Copy Full Path / Relative Path; Open Containing Folder; Open in Terminal | Git > ...; Add to Chat | plugin groups.
- **Tab:** Close; Close All But This; Close All; Pin; Copy Full Path; Open Containing Folder; Move to New Group; Open in Bound Split.

## Interaction rules

- **No modal dialogs** except destructive confirmations and first-run (F-SHL-32). Settings, plugin pages, find-in-files, etc. are docked views.
- **Never lose input:** typed text in chat input, find boxes and commit messages is kept server-side as draft UI state (per client) and survives reloads.
- **Everything has a keyboard path and a context-menu path**; nothing important is hover-only (phones).
- **Long work shows progress where it was started** (button spinner) and globally (status bar), and finishes with a notification if the page is hidden.
- **Errors are actionable**: every error toast/row has a "show details" (output source, log line, or trajectory event).
- **Density:** 22 px list rows, 13 px UI text, 12-13 px code (user setting), 1 px separators, 2-4 px radii, no shadows except menus/popups (`docs/07`).

## Icons

Codicons (vendored) for UI glyphs. File-type icons: a VS-like set in `shared/ui-kit/icons/` (distinct shape + restrained color per file category, since color here carries meaning: type identity); languages contribute icons for their extensions (`fileIcons`).

## Screens to design-review first (before building everything)

1. Editor activity with Solution Explorer, two documents, Output and Error List, terminal open - desktop.
2. Chat activity mid-run with an approval card and the right panel - desktop and phone.
3. Phone diff review flow from a notification.
4. Settings window: Models and Model Groups pages.
5. Git activity: graph + commit details.
6. Profiler: timeline in thread view and by-task view.
7. GPU debugger: frame debugger, shader debugging with pixel history, performance and compare (`docs/22`).

Each gets a static HTML mock against `theme.css` (no server needed) and screenshots at 1920x1080, 1366x768, 390x844 - the first frontend deliverable (`docs/18`, M0). Built: `frontend/mocks/` (open `index.html`; screenshots in `frontend/mocks/screenshots/`).

## The web client as built (M1)

`frontend/` is plain ES modules served as they are (no bundler, no npm): `index.html` loads `css/theme.css`, `css/layout.css` and `src/main.js`. Components shared with plugin UI modules live in `shared/ui-kit/`, served at `/ui-kit/`. Codicons are served one SVG each at `/codicons/<name>.svg` (from `third_party/codicons`) and painted as CSS masks in the current text color, so any codicon a manifest names works.

**Composition.** The shell owns chrome, layout, commands, keybindings, menus and focus; every view in it comes from a plugin's UI module (`docs/02`). For the shown workspace it reads `plugins.contributions {wsId}`, imports each loaded plugin's `ui` module, and builds:

- the sidebar from `activities` by `order`: the workspace switcher on top, the notifications bell and the settings gear at the bottom;
- the menu bar from `menus["menubar/<name>"]` in VS order (File, Edit, View, Git, Project, Build, Debug, Test, Analyze, Tools, Extensions, Window, Help); a menu shows only when something is in it, and menus that do not fit go into a trailing `...` menu. View lists every tool window;
- the command registry from `commands`, keybindings from `keybindings`, status items from `statusBarItems`.

An activity with a `view` renders that export over the whole center. One without is a **dock host**: its tool windows (`toolWindows` whose `activity` names it, `ied.editor` by default) and its document well are laid out by its layout tree. While no plugin contributes a dock host (every one disabled), the shell adds its own, **Home**, so its tabs (Plugins, Keyboard Shortcuts) always have a place and a person can turn plugins back on.

The terminal dock and the chat activity are contributions of the `terminal` and `chat` plugins; they appear when those plugins ship (their server sides come in M3 and M4). A plugin with a status bar item, a toolbar (`menus["toolbar/main"]`) or a context menu (`explorer/context`, `tab/context`, `output/context`) gets it rendered the same way.

**Layout document.** The server stores it verbatim per workspace and device class (`docs/10`); the client defines it:

```jsonc
{ "v": 1, "activity": "ied.editor",
  "activities": { "ied.editor": <node> },
  "phone": { "pane": "<ref>", "back": ["<ref>"] } }
// node: { "kind": "split", "dir": "row" | "column", "sizes": [0.2, 0.8], "children": [<node>, ...] }
//     | { "kind": "tabs", "tabs": ["<ref>", ...], "active": "<ref>", "well": true, "preview": "<ref>", "dock": "left" }
// ref:  a tool window id ("ied.output"), "<kind>:<argument>" for a tab a plugin opens ("doc:src/main.cf"),
//       or "activity:<id>" for an activity that renders itself (the phone's pane)
```

`well` marks a document group (tabs on top; documents open into it, VS); `preview` is that group's preview tab; `dock`
is the region a tool-window group was made for, so a tool window shown later joins it. The operations on a tree are
pure functions in `frontend/src/shell/layouttree.js`.

- No stored layout (revision 0), or one of another `v`, starts from the default the contributions imply: tool windows placed by `defaultDock` (left, right, bottom; `document` joins the well) around one document well.
- A tab whose ref nothing resolves (its plugin is disabled) stays in the tree, hidden, and comes back with its plugin. A tool window the tree lacks is added where its `defaultDock` says when it is first shown.
- A split has at least two children and sizes that sum to 1; a group has one active tab; closing the last tab of a group removes the group, except the well, which stays (empty, as in VS).
- Changes are written with `layout.set` 400 ms after the last one. A conflict retries on the new revision (last writer wins, `docs/11`); a `layout.changed` from another client of the same device class is applied unless a drag is under way.
- Window > Reset Window Layout writes the default.

**Docking.** Dragging a tab shows targets on the hovered group (center joins it, the four edges split it) and on the activity's edges, with a preview overlay. Splitters resize their neighbors (80 px minimum). A tab's context menu: Close, Close All But This, Close All, Move to New Group Right, Move to New Group Below, Copy Reference. No floating windows.

**Commands and keys.**

- A command has an id, a title, a category, an icon, an `enablement` when-clause and a handler: a UI module's (`ied.commands.handle`), a server binding (`binding`, from M11), or the shell's own (`shell.*`).
- When-clauses: `!`, `&&`, `||`, parentheses, `==`, `!=`, `<`, `<=`, `>`, `>=`, `=~ /re/flags`, quoted strings, numbers, `true`/`false`; a bare key is true when its value is truthy. The shell sets `deviceClass`, `connected`, `workspaceTrusted`, `workspaceKind`, `activeActivity`, `activeTab` (its ref), `activeTabKind`, `inputFocus`, `resourcePath` and `resourceExtname` (when the active tab names a file).
- A key is `[ctrl+][shift+][alt+][meta+]<key>` (modifiers in any order), and a chord is two keys separated by a space (`ctrl+k ctrl+d`). `<key>` is the physical key (`KeyboardEvent.code`) named as on a US keyboard: `a`-`z`, `0`-`9`, `f1`-`f24`, `` ` ``, `-`, `=`, `[`, `]`, `\`, `;`, `'`, `,`, `.`, `/`, `enter`, `escape`, `tab`, `space`, `backspace`, `delete`, `insert`, `home`, `end`, `pageup`, `pagedown`, `up`, `down`, `left`, `right`, `break`.
- The first key of a chord waits 2 s for the second; the status bar shows it. For one key, the last binding whose when-clause holds wins (shell, then plugins in load order, then the owner's). A key without a modifier other than Shift does nothing while a text field has focus, except F-keys and Escape.
- Browser-reserved keys (Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Shift+T, Ctrl+Shift+N, Ctrl+Shift+W, Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PageUp, Ctrl+PageDown) are never bound by default. A binding on one is marked "app window only" in Help > Keyboard Shortcuts: it fires only in an installed app window.

Shell commands: `shell.commandPalette` (Ctrl+Shift+P, Ctrl+Q), `shell.showView {id}` (View > tool windows), `shell.showActivity {id}`, `shell.closeTab` (Ctrl+F4), `shell.nextTab` / `shell.previousTab` (Ctrl+Alt+PageDown / PageUp), `shell.resetLayout`, `shell.showNotifications`, `shell.clearNotifications`, `shell.switchWorkspace`, `shell.keyboardShortcuts`, `shell.plugins` (Extensions > Manage Plugins), `shell.trustWorkspace`, `shell.about`.

**Status bar.** Left: the connection (`Ready`, `Reconnecting...`, `Offline`) and the chord in progress, then plugin items; right: plugin items, the connected devices and the notifications count.

**Connection.** The client keeps `clientId` in `sessionStorage` (a reload is the same client: its leases and opIds hold) and `deviceId` in `localStorage`. Cursors live with the page: a reconnect resumes every subscribed topic from its cursor (`docs/10` Resume), while a reload starts from snapshots, which hold everything a view needs (an in-flight chat segment included). It pings every 20 s, takes a socket that answers no ping within 10 s as dead, and reconnects with jittered backoff from 0.5 s to 15 s. While disconnected a non-blocking banner says so and views stay readable; requests that carry an `opId` are sent again after the resume, others fail with `unavailable`. `bye {reason: "auth-revoked"}`, and a socket that keeps failing while `/api/auth/state` says the device is signed out, go to the sign-in page; `protocol-mismatch` reloads the page. A topic subscribed by a second view is subscribed again, so every view of it starts from a fresh snapshot; a topic no view wants is unsubscribed 5 s later.

**Phone (< 720 px).** A header (back, pane title, workspace, bell) and a bottom nav: the activities that ask for `phoneSlot`, then the rest by `order`, then tool windows fill four slots; More lists the others, notifications, keyboard shortcuts, plugins and the workspace switcher. One pane at a time: an activity with a view shows it; a dock host shows the active tab of its well or, with none open, its first tool window. Opening a tab pushes the current pane on `back`. Menus open as bottom sheets and a long press is the context menu.

**The editor plugin** (`plugins/editor`, M2) brings the editor activity's views:

- **Solution Explorer**: a solution's groups, projects and Solution Items, or a folder; New File / New Folder (named in place in the tree, as VS does), Rename (F2, in place) and Delete (Del, after a confirmation; a folder with everything in it) on files and folders. A renamed file's open document follows it to its new name, unsaved edits included (`doc.renamed`, `docs/10`). A file change on disk refreshes only the folders it touches, so expanded folders stay expanded.
- **Documents** in Monaco (`docs/10` "The client side as built"), with the language servers' features (`docs/24`): hover, completion, signature help, go to definition / declaration / type definition / implementation (another file opens in a preview tab), find all references and peek (other files' text is loaded for the peek), highlights, document symbols (the outline and breadcrumbs), folding, semantic tokens in the theme's `--syntax-*` colors, inlay hints, format document and selection, rename symbol, and code actions (the light bulb). Diagnostics are squiggles.
- **Error List**: every diagnostic of the workspace with Errors / Warnings / Messages toggles and counts, search, and the VS columns (Code, Description, File, Line, Source); Enter or a double click opens the place. On a phone each entry is two lines.
- **Find in Files** (Ctrl+Shift+F, Edit menu): a docked view with the query (the editor's selection to start), Match Case, Regular Expression (RE2's syntax), files to include and to exclude (globs; without any, build output, vendored code, `node_modules` and `.git` are skipped), and the results by file with each hit marked; a match opens at its place. It searches open documents as they are, unsaved edits included (`fs.search`, `docs/10`).
- **Output**, and the editor status item (Ln, Col, indentation, line endings, language).

**Tests.** `frontend/tests/` holds the in-browser unit tests (layout tree operations, keys and chords, when-clauses, the protocol client's resume logic against a fake socket), run headless by `tests/e2e/run_e2e.py`, which then drives the shell with Playwright at 1920x1080, 1366x768 and 390x844 against the mock server and the real one.
