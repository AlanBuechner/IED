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

**As built (M4, first slice):** the `chat` plugin's activity (`docs/05` "chat"), checked by screenshot at 1366x768 and 375x812 against mockup 02: a running turn with an approval card and the right panel, the same card at phone width as a bottom-stacked button grid, and the resume path: a page reloaded mid-turn gets the turn's text so far from the snapshot and keeps streaming to the end (e2e `chat_reload_mid_turn`), and after a server restart the chats come back from disk (a turn the restart cut off ends `interrupted` and its chat waits paused: server checks). A turn against a real local model (llama.cpp) streamed its thinking, ran its tool calls and finished. On a phone the right panel is a sheet over the transcript, opened from the tab bar's details button and closed by its own; opening a file or a page from it closes it (2026-10-21). A chat narrower than 640 px (a tab in a side group) shows its details as that sheet too.

**Dockable chats and the Chats list (2026-10-22, owner: "the tabs in the chat view ... dockable and ... a side bar on the left that shows all the chats and their status with the open ones grouped at the top"):** the Chat activity is a dock host like the Editor (its manifest names no `view`): its document well holds chats as ordinary tabs - `chat:<chatId>`, titled by the chat, its icon the chat's state (a spinner while it runs; a shield, question mark or pause while it waits on its owner) - split into groups, dragged, auto-hidden or moved to the Editor's well ("Move to Editor" on the tab's menu; a file tab moves to the Chat's the same way), with Rename and Delete on the menu too. `documents: ["chat"]` keeps other tabs out of it: a file opened from a chat opens in the Editor, a page the chat drives opens beside it, tethered. New Chat (Ctrl+Alt+N, the empty well's first hint) opens `chat:new`, a draft whose first message makes the chat; the chat's tab then takes the draft's place. The **Chats** tool window (`ied.chat.list`, docked left) lists every chat of the workspace with its state, tasks or last activity: those open as tabs in this window first (each shown one marked), then the rest, then **Deleted** (folded; `docs/04` "Deleting a chat"), with a search box and New Chat. A click opens a chat (or shows it where it is open); its menu opens, renames (F2, in place), closes its tab or deletes it (Del) - with an Undo toast and no confirmation, since a deleted chat is kept; a deleted row restores (Enter, double-click, its button) or is deleted permanently. On a phone the Chat slot shows the open chat, else the Chats list (it takes no slot of its own, `phoneNav: false`); a chat's header has a button back to the list. e2e `chat_tabs_dock`, `chat_delete_and_restore`.

```
+----+----------------------+--------------------------------------------------------+-----------------+
|    | Chats                | (*) Port logging  x | (!) Fix test x |                   | v Context  62%  |
|    | [search chats ] [+]  +--------------------------------------------------------+   system   4.1k |
|    | v OPEN            2  | You: port the logging subsystem from RefEngine  10:02  |   messages 38k  |
|    | |(*) Port logging now| > Thinking  (12 s, 1.2k tok)                           | v Tasks  3/7    |
|    |  (!) Fix test   3/7  | [fs_read RefEngine/Log.cpp 1-200]  ok 4 ms             |   [x] read ref  |
|    | v RECENT          9  | [fs_edit Engine/src/Core/Log.cf]  awaiting approval    |   [>] port sink |
|    |  ( ) General     2h  |  +--------------------------------------------------+  | v Modified (4)  |
|    |  ( ) Shaders     3d  |  | diff ...                  [Allow] [Always] [Deny]|  |   Log.cf  +120  |
|    | > DELETED         2  |  +--------------------------------------------------+  |                 |
|    |                      | -- harness: continued after max_tokens (1x) --         |                 |
|    |                      +--------------------------------------------------------+                 |
|    |                      | [@ attach] message...                [Queue] [Inject] [>]                 |
|    |                      | group: plan-claude/impl-local v  effort: high v  mode: Agent v           |
+----+----------------------+--------------------------------------------------------+-----------------+
```

- A chat tab's icon is its state; the Chats list adds the tasks done (`3/7`) and says "Needs approval" / "Needs an answer" / "Paused" in amber.
- Approval cards and questions are inline and also aggregated in the notification center.
- Harness notes (nudges, continues, retries, routing) are thin grey rows - always visible, never hidden in a log.
- A reply reads as prose, colored the way Claude Code colors its output (owner, 2026-10-22: easy on the eyes in a dark room): body text softer than the UI's and set larger and looser (`--prose-*`, `docs/07` "Prose"), headings and list markers warm, inline code lavender on a faint tint, links blue, the agent's name in the AI accent. Fenced code takes the editor's syntax colors through `editor.colorize` (once its closing fence arrives, cached by text so a streaming reply does not flicker), and a change's diff in an approval card or a tool result is colored as its file's language. The prose column is at most 940 px wide; the composer's Send button is a step dimmer than white.

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
- Tool windows (Output, Error List, Find Results, Call Stack, Locals, Watch, Breakpoints, Test Explorer, Bookmarks, Task List, Document Outline, Git Changes...) are tabs in **dock regions** (left/right/bottom of the Editor activity) with VS auto-hide: each tool window is either docked or unpinned to a tab on the activity's edge that slides out over the documents on click (as built: "Auto-hide" below).
- Chats can be docked into the Editor activity and editors into the Chat activity (F-SHL-14): a tab carries its kind; any group can host any kind.
- Layout state is server-side per workspace x device class (F-SHL-13). "Window > Reset Window Layout" restores the default for the activity.

### Tethered tabs

Two tabs can be **tethered**: they belong together and show together. A Markdown file and its preview, a header and its code file, and a chat opened as a tab and the page it drives.

- Activating either tab (clicking it, Ctrl+Tab, Go to Definition into it) also shows the other in its own group, so the pair stays side by side. A partner in the same group stays behind; tethering is about groups beside each other.
- A tether may **close** its follower with its lead: closing a Markdown file closes the preview opened beside it. Closing either tab, or "Untether" on its context menu, drops the tether.
- Tethered tabs carry a link glyph after their name; its tooltip names the partner.
- **A Markdown preview beside its file follows its group** (owner, 2026-10-22: "like in vs code"): when another `.md` file becomes the active tab of the group holding the previewed file, the preview shows it instead, in its place and tethered to it (`plugins/markdown/ui/follow.js`, through `ied.tabs.replace`); a tab of another kind leaves it on the last Markdown file, and a preview opened in place (Open Preview, Ctrl+Shift+V) does not follow. A Markdown file's tab strip has the button for it, beside the Open documents list (the `tab/title` menu below). e2e `markdown_preview_follows_its_group`.
- **A header or code file beside its partner follows its group too** (owner, 2026-10-22: "the tethering should work with source and header files as well"), where the language plugin's `headerSourcePairs` entry says `"follow": true` (the C/C++ and C4 plugins do): when another file of a following pair becomes the active tab of either group, the other group shows its counterpart - in the old one's place, or, when the counterpart is open already in another group, there - tethered to it (`plugins/editor/ui/counterpartfollow.js`). A file without a counterpart leaves the pair as it was; a file that is already tethered is left alone. Each language plugin puts "Open Header / Code File to the Side" on its files' tab strips with its own `tab/title` item. e2e `counterpart_follows_its_group`.
- Made by "Open Preview to the Side" (Markdown, Ctrl+K V, the tab strip's button; also when `ied open` opens a file with a companion tab), "Open Header / Code File to the Side" (Ctrl+K Ctrl+Shift+O, the tab strip's button), "Tether with the Tab Beside" on a tab's context menu (the tab shown in the nearest other document group), and by plugins through `ied.tabs.openBeside` / `ied.tabs.tether`.
- Tethers live in the layout document (`tethers: [{lead, follower, closes}]`), so they follow the layout across reloads and windows of the same device class. On a phone one pane shows at a time: tethers are kept but only closing follows them.

## Keyboard (VS default scheme)

Browser constraint: in a normal browser tab, **Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Shift+T, Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PageUp/PageDown** are reserved by the browser and cannot be intercepted. F5, Ctrl+S, Ctrl+P, Ctrl+F, Ctrl+H, Ctrl+G, Ctrl+K chords, F-keys and most others can (`preventDefault` on keydown). Consequences:

- The default scheme avoids depending on reserved keys; where VS uses one (Ctrl+F4 close document is fine; Ctrl+Tab window switcher is not), IED binds an alternate (`Ctrl+Alt+Tab`... configurable) and shows a one-time tip.
- The **installed PWA** (Chrome/Edge "Install app") and the **desktop app** (F-OPS-26, `ied-desktop`, `docs/27`: browser accelerator keys off, so F5, Ctrl+R, Ctrl+F, Ctrl+P, F12, Ctrl+W/T/N and Ctrl+Tab reach the web client) receive more shortcuts; the keymap marks bindings "full in app window" vs "browser tab".

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

Codicons (vendored) for UI glyphs. Files, folders, projects and solutions get one simple glyph each (`shared/ui-kit/fileglyphs.js`), as Visual Studio and VS Code draw them: a distinct shape for each kind of thing so a glance tells them apart, a muted hue for which language, and no solid fills.

- **Solution**: two outlined tiles in `--icon-solution` (purple, as in Visual Studio).
- **Project**: a thin rounded tile in its language's hue, faintly tinted, with the language's letters (`C4`, `++`, `RS`, `GO`); a project no plugin recognizes gets a plain tile.
- **Folder** (a solution's groups and Solution Items too): Visual Studio's tan folder, open while expanded.
- **File**: its type's letters alone in the type's hue (`C4`, `C`, `C++`, `{}`, `MD`), as VS Code's Seti icons; the second file of a pair has its own letter (a header is `h`), so a source and its header differ in shape, not only in color. A plugin may give its own picture instead (a tinted codicon: images, archives, binaries, config files, package manifests). A file no plugin names is a plain page.

The shell knows no language: which file or project gets which letters and hue comes from the plugins' `fileIcons` and `projectIcons` (`docs/02`), with hues from the theme's identity tokens (`docs/07`). As shipped: C4 teal (`.cf` "C4", `.ch` "h", `nuke.toml`), C blue, C++ and C/C++ headers violet (`ied.languages`; the C/C++ plugin adds C++ projects by `CMakeLists.txt`, `*.vcxproj`, `meson.build`, `compile_commands.json`), Rust orange (`Cargo.toml` projects), Go and Python cyan, TypeScript blue, JavaScript yellow, C# green, JSON amber, Markdown gray, shaders pink, Git's files red. A project is matched by the `language` its `.iedproj` names (`nuke init --ied` writes it), else its presets or build system, else marker files in its root folder (listed once, when no other rule decides). The glyphs appear in the Solution Explorer, document tabs, Find in Files, Go To All, the Git activity's file lists and the chat's modified files.

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
- the command registry from `commands`, keybindings from `keybindings`, status items from `statusBarItems`, toolbar views from `toolbarItems` (after the toolbar's commands, highest priority first; on a phone they sit in a strip above the bottom nav).

An activity with a `view` renders that export over the whole center. One without is a **dock host**: its tool windows (`toolWindows` whose `activity` names it, `ied.editor` by default) and its document well are laid out by its layout tree. While no plugin contributes a dock host (every one disabled), the shell adds its own, **Home**, so its tabs (Plugins, Keyboard Shortcuts) always have a place and a person can turn plugins back on.

The terminal dock and the chat activity are contributions of the `terminal` and `chat` plugins; they appear when those plugins ship (their server sides come in M3 and M4). A plugin with a status bar item, a toolbar (`menus["toolbar/main"]`) or a context menu (`explorer/context`, `tab/context`, `output/context`) gets it rendered the same way.

**Layout document.** The server stores it verbatim per workspace and device class (`docs/10`); the client defines it:

```jsonc
{ "v": 1, "activity": "ied.editor",
  "activities": { "ied.editor": <node> },
  "mode": "debug", "modes": { "debug": { "activities": { "ied.editor": <node> } } },
  "phone": { "pane": "<ref>", "back": ["<ref>"] } }
// node: { "kind": "split", "dir": "row" | "column", "sizes": [0.2, 0.8], "children": [<node>, ...] }
//     | { "kind": "tabs", "tabs": ["<ref>", ...], "active": "<ref>", "well": true, "preview": "<ref>", "dock": "left",
//         "autoHidden": ["<ref>", ...], "autoHide": "left" | "right" | "top" | "bottom", "autoHideSize": 0.3 }
// ref:  a tool window id ("ied.output"), "<kind>:<argument>" for a tab a plugin opens ("doc:src/main.cf"),
//       or "activity:<id>" for an activity that renders itself (the phone's pane)
```

`well` marks a document group (tabs on top; documents open into it, VS); `preview` is that group's preview tab; `dock`
is the region a tool-window group was made for, so a tool window shown later joins it. `autoHidden` are the group's
tabs that are auto-hidden to the activity's edge `autoHide`, and `autoHideSize` is how much of the activity's width
(left, right) or height (top, bottom) they take when they slide out. The operations on a tree are pure functions in
`frontend/src/shell/layouttree.js`.

- No stored layout (revision 0), or one of another `v`, starts from the default the contributions imply: tool windows placed by `defaultDock` (left, right, bottom; `document` joins the well), `dockGroup`, `autoHide` and `open` around one document well ("Window layouts" below). A tablet's default has every tool window auto-hidden. `modes` holds the trees of the layout modes other than the default one, and `mode` names the one shown (the default when absent).
- A tab whose ref nothing resolves (its plugin is disabled) stays in the tree, hidden, and comes back with its plugin. A tool window the tree lacks is added where its `defaultDock` says when it is first shown.
- A split has at least two children and sizes that sum to 1; a group has one active tab; closing the last tab of a group removes the group, except the well, which stays (empty, as in VS).
- Changes are written with `layout.set` 400 ms after the last one, and no later than a second after the first one not yet written. A change made before the server's document has arrived waits for it and is made to it. A conflict retries on the new revision (last writer wins, `docs/11`); a `layout.changed` from another client of the same device class is applied unless a drag is under way.
- Window > Reset Window Layout writes the default.

**Docking.** Tabs behave as in a web browser (owner, 2026-10-22): a tab dragged along its own strip rides the pointer and the tabs it passes slide aside (its leading edge crossing a neighbor's middle), and it lands where it is let go (`tabslide.js`); dragged off the strip (24 px above or below it), it docks instead, and brought back along it, it slides again. Docking targets, in order: a tab strip - over it or within 20 px of it, the tab joins that group, a mark showing where it goes between the tabs (strips come before the activity's edges, so the top strip, which lies along the dock's top edge, still takes tabs) - then the activity's edges, then the zones of the hovered group's body below its strip (center joins it, the four edges split it), with a preview overlay. At the strip's right end, beside its Open documents list, are the buttons plugins contribute to the `tab/title` menu for the group's active tab (`when`-clauses see that tab as `tabRef`, `tabKind`, `resourcePath`, `resourceExtname`; the command gets `{ref, path}` with that group focused) - a Markdown file's Open Preview to the Side, a C/C++ or C4 file's Open Header / Code File to the Side. A document strip ends in "+" (a double click on its free end does the same): the activity's `newTab` command opens a new tab in that group - a new chat in the Chat activity, a new file in the Editor. Tabs shrink (to 64 px) before the strip overflows, keeping "+" in reach. **New files** are untitled until saved: `Untitled-N.txt` (the first N free) in the workspace's local state (`.ied/local/untitled/`), so the server keeps it like any document and it is saved a second after each edit; Ctrl+S (or File > Save) asks for its path in the workspace and moves it there with `fs.rename` (its document and tab follow; an existing file is refused, never overwritten); closing an empty one deletes it, one with text stays and the toast offers Reopen. File > New File and Ctrl+N do the same as "+". e2e `tabs_like_a_browser`. Splitters resize their neighbors (80 px minimum). A group that empties (its last tab closed or dragged away) gives its room to the neighbor beside it that holds documents - the one before it first - and only to a tool window when neither does, so tool windows keep their size, as in VS (`layouttree.js` `heirOf`; a chat docked to the side and back used to widen the Chats list). A tab's context menu: Close, Close All But This, Close All, Move to New Group Right, Move to New Group Below, Tether with the Tab Beside / Untether, Auto Hide / Dock (tool windows), Copy Reference; a tool window's title bar has the same menu. No floating windows.

**Auto-hide** (VS, F-SHL-11). Each tool window is docked, as in the layout above, or auto-hidden, on its own: a tool window's title bar has VS's pin (Auto Hide) beside Close, and it hides that window alone to the edge of the activity its group lies on (the side of the document group where their branches of the tree part: Solution Explorer left, Output and Error List bottom). The rest of its group stays docked; a group with no window left docked takes no room, and the documents get it. A hidden window keeps its place among its group's tabs, so Dock puts it back there.

- The edges carry strips of the hidden windows' tabs, clustered by group: vertical text on the left and right, horizontal on the top and bottom.
- A click on a strip tab slides that window out over the documents, focused, from its edge (as large as its group was in the layout; a drag on its inner edge resizes it, kept as `autoHideSize` for the group's hidden windows). One window is out at a time. Dragging a strip tab onto a group docks it there.
- It slides back when a click or the focus goes anywhere else (menus and popups excepted, so a context menu in it works), on Escape (the focus goes back where it was), or on a second click of its strip tab.
- Asking for a hidden window (View > Output, Ctrl+Alt+O, a build showing its output, Ctrl+Shift+F) slides it out, as VS does.
- Its slid-out title bar has Dock (the pin turned sideways) and Close. Auto Hide and Dock are also on the tab's context menu and the Window menu, with Window > Auto Hide All for every tool window of the activity; Reset Window Layout docks them all again.
- Hidden or docked is layout state, so it follows the person to every window of the device class and survives reloads; which window is slid out belongs to the browser window. A tool window added to a group whose every window is hidden is hidden too. Documents never hide. A phone has no edges: its tool windows are panes, whatever the desktop layout says.
- Asking for a tool window that is not open puts it where the current layout mode's default says: auto-hidden (and slid out) when that default hides it.

**Window layouts** (VS's design and debug layouts). The editor area has a layout for editing and one for debugging; each is the person's to change, and each is remembered as they left it.

- Plugins contribute **layout modes** (`layoutModes: [{id, title, when}]`); the current mode is the first, in load order, whose `when` holds, else the default one. The shell sets the `layoutMode` context key. The editor plugin's `debug` mode holds while a debug session lives past its pre-launch build (`debugState && debugState != 'building'`), so F5's build runs in the editing layout, as in VS.
- Each mode keeps its own tree per activity: the default mode's in `activities`, another's in `modes.<id>.activities`; a mode without one starts from its default. The layout document's `mode` is the one shown, so every window of the device class shows the same: a window switches the document when the mode it works out changes (another window may have switched it already). Only a switch back to the default waits until 2 s after the window's plugins load, since until their context keys arrive with their topics "no mode holds" may mean "not heard yet"; then the window makes the document agree with it, which also covers a session that ended while no window was open.
- The documents follow the person across a switch: the new mode's tree gets the old one's document area (the smallest subtree holding every document group, its splits and tabs as they were) in place of its own, and keeps its tool windows where they were in that mode. Documents the old tree had among tool windows go to the first document group.
- A mode's default comes from each tool window's placement: `defaultDock`, `dockGroup` (tool windows of one dock and group share a tab group; another group of the same dock goes beside it, to the right at the bottom and below on a side), `defaultSize` (the share of the activity's width or height a new group for it takes), `autoHide` and `open` (false: left out, its menu item opens it), each overridable for a mode under `modes.<id>` (`dock`, `group`, `size`, `autoHide`, `open`).
- As shipped: **editing** has Solution Explorer docked on the left, Output auto-hidden on the right edge (sliding out 40% of the width), Error List and Find in Files auto-hidden at the bottom, and the debugger's windows closed. **Debugging** has Solution Explorer on the left, Output, Error List, Call Stack and Breakpoints docked at the bottom beside Locals, Watch and Threads, Diagnostic Tools on the right (the program's memory and CPU, `docs/25` "Diagnostic Tools"), and Find in Files hidden. Window > Reset Window Layout resets the current mode's.
- Starting a build shows Output, as VS does: docked, it comes to the front of its group; auto-hidden, it slides out.

**Commands and keys.**

**The editor's keys** are Visual Studio's, as named commands (Edit category) of the editor plugin, each rebindable on Settings > Environment > Keyboard (`docs/07` "Host settings") and active in a document tab: Comment / Uncomment Selection (Ctrl+K, Ctrl+C / Ctrl+K, Ctrl+U), Toggle Line / Block Comment (Ctrl+/, Ctrl+Shift+/), Format Document / Selection (Ctrl+K, Ctrl+D / Ctrl+K, Ctrl+F), Duplicate (Ctrl+D), Cut Line (Ctrl+L), Delete Line (Ctrl+Shift+L), Move Selected Lines (Alt+Up / Alt+Down), Insert Line Above / Below (Ctrl+Enter / Ctrl+Shift+Enter), Make Lowercase / Uppercase (Ctrl+U / Ctrl+Shift+U), Join Lines (Shift+Alt+L, Shift+Alt+J), Sort Lines, Expand / Contract Selection (Shift+Alt+= / Shift+Alt+-), Insert Next Matching Caret (Shift+Alt+.), Insert Carets at All Matching (Shift+Alt+;), Insert Caret Above / Below (Ctrl+Alt+Up / Down), Box Select (Shift+Alt+arrows), Go To Brace (Ctrl+]) and Go To Brace Extend (Ctrl+Shift+]), Go To Line (Ctrl+G), Find / Replace (Ctrl+F / Ctrl+H), Find Next / Previous (F3 / Shift+F3), Find Next Selected (Ctrl+F3), Complete Word (Ctrl+Space, Ctrl+J), Parameter Info (Ctrl+Shift+Space), Quick Info (Ctrl+K, Ctrl+I), Quick Actions (Ctrl+., Alt+Enter), Rename (Ctrl+R, Ctrl+R), Go To Definition (F12), Peek Definition (Alt+F12), Go To Implementation (Ctrl+F12), Find All References (Shift+F12), outlining (Ctrl+M, Ctrl+M toggle; Ctrl+M, Ctrl+L toggle all; Ctrl+M, Ctrl+O collapse to definitions; Ctrl+M, Ctrl+P stop), View White Space (Ctrl+R, Ctrl+W), Word Wrap (Ctrl+E, Ctrl+W) and Zoom In / Out (Ctrl+Shift+. / Ctrl+Shift+,). The shell reads keys before Monaco, so these win over Monaco's own; Ctrl+D, Ctrl+L, Ctrl+U, Ctrl+H, Ctrl+J, F3 and F12 do not reach the browser while a document has focus.


- A command has an id, a title, a category, an icon, an `enablement` when-clause and a handler: a UI module's (`ied.commands.handle`), a server binding (`binding`, from M11), or the shell's own (`shell.*`).
- When-clauses: `!`, `&&`, `||`, parentheses, `==`, `!=`, `<`, `<=`, `>`, `>=`, `=~ /re/flags`, quoted strings, numbers, `true`/`false`; a bare key is true when its value is truthy. The shell sets `deviceClass`, `connected`, `desktopApp` (inside IED's desktop app, `docs/27`), `workspaceTrusted`, `workspaceKind`, `activeActivity`, `activeTab` (its ref), `activeTabKind`, `inputFocus`, `resourcePath` and `resourceExtname` (when the active tab names a file), and for the focused tab (or the one a tab menu is for) `tabTethered`, `tabInToolGroup` and `tabAutoHidden`.
- A key is `[ctrl+][shift+][alt+][meta+]<key>` (modifiers in any order), and a chord is two keys separated by a space (`ctrl+k ctrl+d`). `<key>` is the physical key (`KeyboardEvent.code`) named as on a US keyboard: `a`-`z`, `0`-`9`, `f1`-`f24`, `` ` ``, `-`, `=`, `[`, `]`, `\`, `;`, `'`, `,`, `.`, `/`, `enter`, `escape`, `tab`, `space`, `backspace`, `delete`, `insert`, `home`, `end`, `pageup`, `pagedown`, `up`, `down`, `left`, `right`, `break`.
- The first key of a chord waits 2 s for the second; the status bar shows it. For one key, the last binding whose when-clause holds wins (shell, then plugins in load order, then the owner's). A key without a modifier other than Shift does nothing while a text field has focus, except F-keys and Escape.
- Browser-reserved keys (Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Shift+T, Ctrl+Shift+N, Ctrl+Shift+W, Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PageUp, Ctrl+PageDown) are never bound by default. A binding on one is marked "app window only" in Help > Keyboard Shortcuts: it fires only in an installed app window.

Shell commands: `shell.commandPalette` (Ctrl+Shift+P, Ctrl+Q), `shell.showView {id}` (View > tool windows), `shell.showActivity {id}`, `shell.closeTab` (Ctrl+F4), `shell.nextTab` / `shell.previousTab` (Ctrl+Alt+PageDown / PageUp), `shell.autoHideTab` / `shell.dockTab` {ref} (Auto Hide / Dock), `shell.autoHideAll`, `shell.resetLayout`, `shell.showNotifications`, `shell.clearNotifications`, `shell.switchWorkspace`, `shell.keyboardShortcuts`, `shell.plugins` (Extensions > Manage Plugins), `shell.trustWorkspace`, `shell.startPage` (File > Start Page), `shell.about`, and inside the desktop app `shell.openFolder` (File > Open Folder..., Ctrl+Shift+Alt+O: the system's folder picker, the folder opened on the server and shown in the window; `docs/27`). The menu `shell/openWorkspace` holds the ways to open a workspace: a page without one shows them as buttons above the server's command line (`ied <folder or .iedsln>`), and the workspace switcher lists them under the open workspaces.

**The address** names the workspace a page shows (`?ws=<wsId>`, kept with `history.replaceState`), so a reload, a bookmark or the desktop app's relaunch comes back to it; the start page drops it.

**Start page.** A window with no workspace shows IED's start page, and so does one asked for it: the `#start` fragment (what the desktop app opens without a path, even while workspaces are open on the server) and File > Start Page (the workspace stays open on the server). On the left, "Open recent": the workspaces open on the server, then the owner's favorites and the other recent projects not open, newest first, from the server's list (`docs/10` "Workspaces"); a row is a project's kind glyph, its name and path, how long ago it was opened (or "open", "shown", "not found"), a star that makes it a favorite or not, and, unless it is open, a button that takes it off the list. A click shows an open workspace or opens a listed project again (`ws.openRecent`); a project that is gone stays dim and only says so. On the right, "Get started": the ways to open a new project (the menu `shell/openWorkspace`) as buttons, and the server's command line. Nothing of a workspace's plugins stays on it: no activities, toolbars or panel, as on a page that never showed one. The workspace switcher (the sidebar's workspace button, the phone header's) lists the same: the open workspaces, the favorites, the ten newest recent projects, then a star for the shown workspace, Start Page and the ways to open one. On a phone the rows are a column, their buttons always shown.

**Status bar.** Left: the connection (`Ready`, `Reconnecting...`, `Offline`) and the chord in progress, then plugin items; right: plugin items, the connected devices and the notifications count.

**Connection.** The client keeps `clientId` in `sessionStorage` (a reload is the same client: its leases and opIds hold) and `deviceId` in `localStorage`. Cursors live with the page: a reconnect resumes every subscribed topic from its cursor (`docs/10` Resume), while a reload starts from snapshots, which hold everything a view needs (an in-flight chat segment included). It pings every 20 s, takes a socket that answers no ping within 10 s as dead, and reconnects with jittered backoff from 0.5 s to 15 s. While disconnected a non-blocking banner says so and views stay readable; requests that carry an `opId` are sent again after the resume, others fail with `unavailable`. `bye {reason: "auth-revoked"}`, and a socket that keeps failing while `/api/auth/state` says the device is signed out, go to the sign-in page; `protocol-mismatch` reloads the page. A topic subscribed by a second view is subscribed again, so every view of it starts from a fresh snapshot; a topic no view wants is unsubscribed 5 s later.

**Phone (< 720 px).** A header (back, pane title, workspace, bell), the plugins' toolbar views in a strip above the bottom nav (the startup project's configuration and its Run, Build and Test buttons; the strip takes the status bar's color while a build runs and after one fails, since a phone has no status bar), and a bottom nav: the activities that ask for `phoneSlot`, then tool windows, then the other activities by `order` fill four slots (an activity that did not ask never pushes a tool window off the bar); More lists the others, the Command Palette (every command, the menu bar's included), notifications, Settings, keyboard shortcuts, plugins and the workspace switcher. One pane at a time: an activity with a view shows it; a dock host shows the active tab of its well or, with none open, its first tool window. Opening a tab pushes the current pane on `back`. Menus open as bottom sheets and a long press is the context menu.

**The editor plugin** (`plugins/editor`, M2) brings the editor activity's views:

- **Solution Explorer**: a solution's groups, projects and Solution Items, or a folder; a solution's Switch Views button (the toolbar's first, as in VS) shows its folder instead and back, remembered per workspace on the device; New File / New Folder (named in place in the tree, as VS does), Rename (F2, in place) and Delete (Del, after a confirmation; a folder with everything in it) on files and folders. A renamed file's open document follows it to its new name, unsaved edits included (`doc.renamed`, `docs/10`). A file change on disk refreshes only the folders it touches, so expanded folders stay expanded.
- **Documents** in Monaco (`docs/10` "The client side as built"), with the language servers' features (`docs/24`): hover, completion, signature help, go to definition / declaration / type definition / implementation (another file opens in a preview tab; a file outside the workspace - a toolchain's or a library's source, a system header - opens read-only, its tab named by the file and a bar saying where it is, with the features of the language server that led there: `docs/10` "Files outside the workspace"), find all references and peek (other files' text is loaded for the peek), highlights, document symbols (the outline and breadcrumbs), folding, semantic tokens in the theme's `--syntax-*` colors, inlay hints, format document and selection, rename symbol, and code actions (the light bulb). Diagnostics are squiggles. The **track-changes margin** marks lines changed since the last save (yellow) and lines changed and saved since the server opened the document (green), from the server's baselines (`doc.baseline`, `docs/10`), so every window shows the same marks; undoing back to the file's text makes a document clean again.
- **Error List**: every diagnostic of the workspace with Errors / Warnings / Messages toggles and counts, search, and the VS columns (Code, Description, File, Line, Source); Enter or a double click opens the place. On a phone each entry is two lines.
- **Find in Files** (Ctrl+Shift+F, Edit menu): a docked view with the query (the editor's selection to start), Match Case, Regular Expression (RE2's syntax), files to include and to exclude (globs; without any, build output, vendored code, `node_modules` and `.git` are skipped), and the results by file with each hit marked; a match opens at its place. It searches open documents as they are, unsaved edits included (`fs.search`, `docs/10`).
- **Go To All** (Ctrl+, as VS's Ctrl+T, which a browser keeps; Edit menu): a quick pick of files by name (`fs.files`, fuzzy) and the language servers' workspace symbols (`lsp.workspaceSymbols`) ranked together by how well the name matches (the name itself, its start, inside it, its word starts, its letters scattered; a file found only through its folders last); `f ` asks for files only, `# ` for symbols only. A pick opens the file, a symbol at its name.
- **Markdown preview** (Open Preview: Ctrl+Shift+V in a Markdown editor, the Explorer's context menu on a `.md` file; `mdpreview:<path>` tabs): the document rendered as it changes, by anyone, GitHub-flavored (headings, lists and task lists, quotes, tables with alignment, fenced code colored by Monaco, links, images, strikethrough); text never becomes markup and only web, mail and relative addresses are links. Ticking a task box writes `[x]` into the document through the store (taking the lease as typing would); a relative link opens the workspace file, a web link a new browser window; the workspace's images come from `/w/<wsId>/files/` (`docs/10`). A file a launch URL opens (`ied open notes.md`) comes with its preview (J10).
- **Output**, and the editor status item (Ln, Col, indentation, line endings, language).
- **Terminal dock** (the `terminal` plugin's panel, M3): full width under the activity, collapsed to its tab row until a terminal opens or Ctrl+` shows it; a drag on its top edge sizes it (kept per device class in the layout). Details in `docs/05` "terminal".
- **Debugging** (`docs/25`, M3): F9 or a click in the glyph margin sets a breakpoint (a red dot; hollow while the session has not bound it, with the adapter's reason on hover; drawn on the line the adapter moved it to); F5 starts the startup project's debug configuration (the toolbar's Start Debugging button names it) after its pre-launch build, or continues; F10 / F11 / Shift+F11 step, Ctrl+Alt+Break breaks in, Shift+F5 stops, Ctrl+Shift+F5 restarts, from the Debug menu and the toolbar's run controls too (on a phone, in the toolbar strip). A stop opens its file at the statement - an arrow in the margin and the line in the debugging color - and the status bar takes the debugging color while a session lives. **Call Stack** lists the stopped thread's frames (a click shows a frame and makes it the one Locals shows; a caller's line gets a lighter arrow) and **Locals** the frame's variables as a tree, children asked of the adapter when a row opens, values in a column that lines up at every depth. **Watch** evaluates the workspace's expressions in the shown frame at every stop (a field at its foot adds one, Delete removes one; an expression that does not evaluate says why in red), **Threads** lists the program's threads (a click shows that thread's stack), **Breakpoints** (Ctrl+Alt+B) lists every file's with its state (a click goes there, the box disables one, the cross deletes it), and a hover over an expression in the editor shows its value. Every window shows the same session at the same stop, and a step from a stale stop is refused.
- **Tasks** (`docs/06` "Protocol and state", M3): the startup project's toolbar - a startup project dropdown when the solution has several, then the toolbar its project file or preset lists (`Configuration` and `Platform` dropdowns, toggles and text fields, Run / Build / Test buttons) - shared by every window through `tasks:<wsId>`. Build > Build (Ctrl+Shift+B), Rebuild, Clean and Cancel (Ctrl+Break), Debug > Start Debugging (F5) and Start Without Debugging (Ctrl+F5), Test > Run All Tests run the startup project's task in that slot and show its output; Set as Startup Project is on the context menu of a project that runs (not a library or a plugin: `explorerProjectKind`), and from the palette it asks which; Solution Explorer draws the startup project in bold. Start Debugging builds first in the editing layout with the Build output showing (`dbg.start`'s `output`), then the program's Debug output once it starts; a failed build keeps its output and a toast says Launch stopped. e2e `startup_project_runs`. A task's button spins while it runs. The status bar says what runs and for how long (`Build demo... 0:12`), then how it ended, and is blue while a build runs and red after one fails; a click opens the Error List after a failure, else the Output. The server saves dirty documents first (`saveBefore`) and its problem matchers fill the Error List.

**Tests.** `frontend/tests/` holds the in-browser unit tests (layout tree operations, keys and chords, when-clauses, the protocol client's resume logic against a fake socket), run headless by `tests/e2e/run_e2e.py`, which then drives the shell with Playwright at 1920x1080, 1366x768 and 390x844 against the mock server and the real one.
