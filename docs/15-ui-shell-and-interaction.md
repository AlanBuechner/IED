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
| PS C:\GameDev\Claymore> nuke build                                                      |  (all activities)
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
|    | You: port the logging subsystem from Icepick                     10:02  |   AGENTS   2.0k  |
|    |                                                                         |   messages 38k   |
|    | > Thinking  (12 s, 1.2k tok)                                            |   tools    31k   |
|    | [code_symbols Log*]  ok 38 ms                                           | v Tasks  3/7     |
|    | [fs_read Icepick/Log.cpp 1-200]  ok 4 ms                                |   [x] read ref   |
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
| Claymore v      (2) [bell]|      | < Ring.cf          [...]  |      | < Changes (4)      [...]  |
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
