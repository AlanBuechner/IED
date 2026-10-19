# Features: shell and layout (SHL)

Design: `docs/15` (layout, docking, wireframes, keymap), `docs/07` (theme).

The shell is the web app's chrome. It owns no feature logic: every view in it is a contribution (from a default plugin or a third-party one). It does own *layout*, *commands*, *keybindings*, *menus* and *focus*.

## Chrome

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SHL-1 | Activity sidebar (~40 px icons): workspace switcher at top, plugin activities (chat, editor, git, ... in manifest order), settings pinned at bottom; badges (unread, running, error) | P0 | US-SHL-1, US-SHL-8 | frontend |
| F-SHL-2 | Bottom terminal dock: full width minus sidebar, present in every activity, collapsible (Ctrl+` toggles), resizable, maximizable | P0 | US-SHL-2 | frontend, plugins/terminal |
| F-SHL-3 | Menu bar (VS order: File, Edit, View, Git, Project, Build, Debug, Test, Analyze, Tools, Extensions, Window, Help); every item is a contributed command | P0 | US-SHL-3 | frontend |
| F-SHL-4 | Toolbar host: renders the active project's toolbar config (dropdowns + buttons) plus plugin-contributed toolbars; overflow menu at narrow widths | P0 | US-BLD-1 | frontend, plugins/editor |
| F-SHL-5 | Status bar: left = global state (Ready / Building... / Debugging, AI run status, notifications); right = editor state (Ln/Col, selection, indent mode, encoding, EOL, language, LSP status), branch, connected devices; plugins contribute items | P0 | US-SHL-6 | frontend |
| F-SHL-6 | Status bar color states (VS): idle gray, building/info blue, debugging (accent), error red - using semantic tokens only | P1 | US-SHL-6 | frontend |
| F-SHL-7 | Title area: workspace name, active configuration, server connection indicator (live / reconnecting / offline) | P0 | US-SRV-1 | frontend |

## Docking and panes

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SHL-10 | Layout tree per activity: rows/columns of tab groups; drag a tab to a dock target (left/right/top/bottom/center) with a preview overlay; no floating windows | P0 | US-SHL-4 | frontend |
| F-SHL-11 | Tool windows (VS): dock to left/right/bottom regions of the editor activity, auto-hide (pin/unpin) to the edge, tabbed together | P1 | US-SHL-3 | frontend |
| F-SHL-12 | Document well: tabs with pinned tabs, preview tab (single-click opens italic preview, VS), dirty dot (yellow), close-all-but-this, tab overflow dropdown, tab position settings | P0 | US-SHL-3 | frontend, plugins/editor |
| F-SHL-13 | Layout persistence on the server per workspace x device class; "reset window layout" command; named layouts (VS "Save Window Layout") P2 | P0 | US-SHL-7 | frontend, src/host |
| F-SHL-14 | Cross-activity docking: a chat tab can be docked inside the editor activity (and vice versa: an editor beside chats) | P1 | US-SHL-4, US-CHT-1 | frontend |
| F-SHL-15 | Focus management: one focused pane; keyboard navigation between panes (Ctrl+Tab window switcher, Alt+F7 tool windows), focus ring visible | P1 | US-SHL-5 | frontend |

## Commands, search and keyboard

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SHL-20 | Command registry: id, title, category, enablement predicate (`when`), handler (client or server binding); everything clickable is a command | P0 | US-SHL-5 | frontend, shared/host-api |
| F-SHL-21 | Keybinding service: VS default keymap, chords (Ctrl+K, Ctrl+D), `when` contexts, user overrides, conflict detection | P0 | US-SHL-5, US-SET-4 | frontend |
| F-SHL-22 | Feature search (VS Ctrl+Q): commands, settings, tool windows, recent files, help topics, in one box | P1 | US-SHL-9 | frontend |
| F-SHL-23 | Go To All (Ctrl+T / Ctrl+,): files, types, members, symbols (LSP workspace/symbol), lines; prefix filters (`f `, `t `, `m `, `:`) as in VS | P0 | US-EDT-13 | frontend, plugins/editor |
| F-SHL-24 | Browser-reserved shortcut handling: detect which shortcuts the browser swallows (Ctrl+W/T/N/Tab), offer alternates, and recommend the desktop wrapper / installed PWA where they work | P0 | US-SHL-5, US-DST-4 | frontend |
| F-SHL-25 | Context menu service: VS-style menus, submenus, keyboard accelerators shown, separators/groups ordered by contribution `group` + `order` | P0 | US-EDT-12 | frontend, shared/ui-kit |

## Pages and overlays

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SHL-30 | Start page: recent workspaces (pin/remove), open folder / solution, clone repository, new project, "what's running" summary across workspaces | P1 | US-SHL-10 | frontend |
| F-SHL-31 | Notification center (bell): history, filters, actions, "mark all read" | P0 | US-SHL-11 | frontend |
| F-SHL-32 | Modal policy: modal dialogs only for destructive confirmation and first-run; everything else is a docked view or an inline card | P0 | - | frontend |
| F-SHL-33 | Workspace trust banner (untrusted mode) with "Trust" action and an explanation of what is disabled | P0 | US-WSP-7 | frontend |
| F-SHL-34 | Connection loss overlay: non-blocking banner, read-only mode while reconnecting, automatic resume, no lost input (pending input buffered client-side until ack) | P0 | US-SRV-1 | frontend |

## Responsive (phone and tablet)

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SHL-40 | Device classes by width: desktop >= 1100 px, tablet 720-1099, phone < 720; layout state stored per class | P0 | US-SRV-2 | frontend |
| F-SHL-41 | Phone shell: sidebar becomes a bottom nav (5 slots + more), one pane visible at a time, panes switch by swipe/tab, terminal becomes a slide-up drawer, toolbar collapses to a primary action + menu | P0 | US-SRV-2 | frontend |
| F-SHL-42 | Touch targets >= 40 px on phone; long-press = context menu; no hover-only affordances | P0 | US-SRV-2 | frontend, shared/ui-kit |
| F-SHL-43 | Phone-first views get dedicated layouts: chat (full screen, approval cards prominent), diff review (inline diff, swipe between files), build status, notification center | P0 | US-SRV-2 | plugins/chat, plugins/git |
| F-SHL-44 | Editor on phone: read + light edit (Monaco in touch mode, larger font), no minimap, simplified toolbar | P1 | US-SRV-2 | plugins/editor |
| F-SHL-45 | Installable PWA (manifest, icons, offline shell that shows "server unreachable" rather than a browser error) | P1 | US-SRV-2 | frontend |

## Theming hooks

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-SHL-50 | Single token file `frontend/css/theme.css`; Monaco theme, xterm theme and ANSI palette are generated from it at load | P0 | - | frontend |
| F-SHL-51 | UI density setting (compact / normal) and UI + editor font size settings; zoom with Ctrl+wheel in editors | P1 | - | frontend |
| F-SHL-52 | Reduced-motion and high-contrast-focus respect (accessibility basics) | P2 | - | frontend |
