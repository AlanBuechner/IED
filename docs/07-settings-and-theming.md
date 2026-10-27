# 07 - Settings and theming

## Purpose

Two things: the settings tier model (what lives where, who wins) and the visual language (dark, grayscale, color only with meaning). Features: `docs/features/14-settings.md`, `docs/features/02-shell-and-layout.md`.

## Settings tiers

| Tier | Location | Scope | Examples |
|---|---|---|---|
| **App** | `%APPDATA%/IED/settings.json` (+ `keybindings.json`) | this owner, all workspaces | model profiles and groups, prompt overrides, egress profiles, remote access, global plugins, keybindings, UI density/fonts |
| **Solution** | `<solution>/.ied/solution.json` (or `.ied/solutions/<Name>.json` when several solutions share a folder) - committed | this solution on every machine | default model group, `aiEgress` policy, permission rules (restrict only), toolbar defaults, plugin settings |
| **Project** | `<solution>/.ied/projects/<Name>.json` - committed | one project | LSP profile choice, sandbox image, project plugin settings |
| Workspace state | `<solution>/.ied/local/` - gitignored; for a folder or file workspace `<app data>/workspaces/<key>/` (opening a folder writes nothing into it; `key` the first 16 hex digits of the SHA-256 of the folder's path compared without case or separator kind). A workspace-relative path under `.ied/local/` names a file there wherever it is - the protocol (`fs.*`, `GET /w/<wsId>/files/`), plugins (`IED::WorkspaceFile`, `docs/26`), the AI and the clients use the one path. What an older server kept of IED's own in a plain folder's `.ied/local/` moves there when the folder opens (a folder holding a solution file keeps the solution's); a build's `lsp-options` stay. A folder made a solution (`docs/03` "Managing projects") keeps its app-data state while it stays open, and that state (chats, layouts, breakpoints, selections, the browser's, captures) moves into the solution's `.ied/local/` when it next opens, what the solution has already kept | this machine | layouts per device class, open documents, toolbar selections, startup project, breakpoints, bookmarks, chats and their media, the browser's pages and shots, GPU and profiler captures |

Rules:

- Every setting declares allowed tiers and a merge kind (replace / deep-merge object / append list) in its schema (`x-ied-tiers`, `x-ied-merge`). The effective value is computed per resource (the project owning a file).
- Security-relevant settings can only be *tightened* by repo tiers (a cloned repo cannot widen AI permissions or egress beyond the owner's app tier).
- Validation is visible: invalid values show in the settings window and as Error List entries pointing at the JSON line; the previous valid value stays in effect.
- Settings files are watched (git pull, hand edits apply live) and written preserving comments.
- **Secrets never live in settings files**: settings reference `{ "secret": "<name>" }` (Windows Credential Manager) or `{ "env": "VAR" }`.

### Settings on the wire (M1)

**Where settings are declared.** A plugin's `contributes.settings.schema` names a JSON Schema in the plugin. Each of that schema's top-level `properties` is one setting, keyed by its full dotted name (`"editor.tabSize"`). A property carries its own schema plus:

- `default`;
- `x-ied-tiers` (the tiers it may be set in; default all);
- `x-ied-merge`: `replace` (the default), `merge` (objects are merged member by member, deeper tiers winning) or `append` (lists are concatenated, app first).

A key claimed by two plugins belongs to the first one loaded; the second gets a plugin problem. The app tier knows the settings of global plugins (built-in and app). A workspace's tiers also know those of its loaded project-local plugins, so a repository's language plugin brings its settings with it once the workspace is trusted.

**Tier files.** Each is a JSONC object of `"<key>": value` members:

| Tier | File |
|---|---|
| `app` | `<app data>/settings.json` |
| `solution` | `<workspace root>/.ied/solution.json` (a folder workspace's tier too) |
| `project` | `<solution root>/.ied/projects/<Name>.json` (with solutions) |

**The effective value** of a key for a workspace starts from its `default`, then applies app, solution and project in that order, each by the key's merge kind.

**Problems** are kept per tier file, each with its line:

- not JSONC;
- an unknown key;
- a key set in a tier its `x-ied-tiers` leaves out;
- a value its schema refuses.

An invalid value is ignored and the last valid value from the same file stays in effect. It is never silently defaulted.

**Live.** Tier files are watched: the app file by its own watcher, a workspace's by the workspace's. A change re-reads the file, bumps its revision and publishes `settings.changed`.

**Writing** edits the file's text in place: a member's value is replaced, a new member goes after the last one, and a reset removes the member with its comma. The file's comments and layout survive. Writes are revision-checked: a write naming an old revision gets `conflict {revision}`.

| Method | Does |
|---|---|
| `settings.schema` | `{settings: {<key>: {plugin, schema, default, tiers, merge, page?}}}` |
| `settings.schema {wsId?}` | the same, for a workspace's catalog |
| `settings.get {wsId?, project?}` | the snapshot of `settings:<wsId>`, or `settings:app` without a `wsId`; with `project`, the values that project's files get (its tier last) |
| `settings.set {tier, wsId?, project?, key, value, expectedRevision?}` | `{revision}`; `tier` is `app`, `solution` or `project` (with `project`). `bad_request` for an unknown key, a tier the key does not allow, or a value its schema refuses. |
| `settings.reset {tier, wsId?, project?, key, expectedRevision?}` | `{revision}`: the member is removed from that tier's file |

Topics are `settings:app` and `settings:<wsId>`. The snapshot is:

```
{tiers: {app, solution: {file, revision, problems: [{line, key?, message}]}},
 projects: {<Name>: <tier state>},
 values: {<key>: {value, source}}}
```

`source` is `default` or the tier that last set the key. `settings.changed` carries the same.

## The settings window

The bottom sidebar icon opens Settings as a **docked view** (not a modal), styled like Visual Studio's *Tools > Options*: a tree on the left (Environment, Keyboard, Text Editor > per language, Projects and Solutions, Build and Run, Debugging, AI > Models / Groups / Routing / Privacy / Prompts / Skills / MCP, Web and Egress, Source Control, Terminal, Sandbox, Profiler, Plugins, Accounts, Remote Access, Diagnostics), a tier selector (User / Solution / Project) at the top, search, and "edit as JSON" per tier. Every row shows the effective value, which tier set it, and override/reset actions. Pages render from plugin JSON Schemas; dedicated editors exist for model groups, keybindings, plugins and remote access.

### The settings window as built

`shell:settings` (the Manage menu's Settings, or the palette; Ctrl+, stays Visual Studio's Go To All) is a tab in the editor's document area: search (Ctrl+E inside it), the tier being edited (User = app, Solution, Project with the project chosen), and the tree of pages. A page is a settings contribution's `page`; the first slash nests (`"Text Editor/C/C++"` is C/C++ under Text Editor). Each row is a setting: its title from the key (`cpp.clangd.path` reads Clangd Path), the key, the description, a control from the schema (a checkbox for a boolean, a list for an enum, a field for a string or a number, a JSON box for the rest; Enter or leaving the field writes, Escape restores), the tier that set the value in effect, Reset when the edited tier sets it, the tiers it may be set in when that is not the edited one, and the tier files' problems for the key with their lines. A refused write shows the server's reason with the schema's problems ("-1 is less than minimum 0"). A key a plugin's own page edits (`settingsPages[].keys`) is found by the search but set on that page: its row there is a button that opens it, so the page decides whether and how it is shown (developer mode's switch only where IED is built from source, `docs/28`).

`shell.settings` takes `{page?, tier?, project?}`, and a plugin's Settings... on a project in Solution Explorer uses the last two to land on that project's tier (`ied.explorer.projectSettings`). The window itself answers `show(page, tier, project)`; with nothing to change it is only shown. It reads and writes its tiers through `settings:<wsId>` and `settings.schema {wsId}`, so it needs a workspace: `openSettings` with none shown (the start page, whose window has no layout to put a tab in either) says so instead of failing. Editing user settings with no project open is not built: it would read the `settings:app` topic, which the shell already follows for the owner's own keys.

A plugin draws its own editor for some keys with a `settingsPages` contribution (`docs/02`): it goes under its page, those keys leave the generic rows, and its view gets `ctx.settings` - `keys`, `schema(key)`, `value(key)` (`{value, source}`), `tier()`, `set(key, value, tier?)` (the error to show, or null), `reset(key, tier?)`, `row(key)` (the window's own row for a key, for a page that draws only some of its keys its own way; drawn again by the page on a change) and `onChange(fn)`. The chat plugin's Models and Model Groups are two (`docs/04` "As built"), and its Claude page draws `ai.claude.paceDays` (Every day / Work days only) and `ai.claude.workDays` (a toggle per day) its own way and the window's rows for `ai.claude.command` and `ai.claude.usageRefreshMinutes` (`docs/04` "The work week"), and Environment > Developer draws `developer.enabled` with `row` above its desktop build (`docs/28`). On a phone the tree is a row of pages above the page.

### MCP servers

The `ied.mcp` plugin's (`plugins/mcp/settings.schema.json`, Settings > AI > MCP Servers, `docs/04` "MCP"): `mcp.servers`
(app, solution and project tiers, merged member by member - a deeper tier's entry of a name replaces the one above it; each
`{command, args?, env?, cwd?}` or `{url, headers?}` with `enabled`, `permission` - ask, auto, read -, `preload`, `privacy`,
`timeoutSeconds`, `startSeconds` and `exclude`; a value of `env` or `headers` may be `{"env": "NAME"}`, read from IED's
environment, so no secret is written in the file) and `mcp.idleMinutes` (app, solution; 10). The page lists the workspace's
servers live with their state, tools, resources and output, and adds, edits, turns off and removes User-tier ones; a
solution's or project's servers are changed in their file, and a plugin's (`mcpServers`, `docs/02`) by an `mcp.servers`
entry of its name.

### Host settings

The host declares a few settings of its own next to the plugins': `shared/host-api/schemas/host-settings.v1.schema.json`, owner `ied`, page Environment, read from the install (`--home`) and listed first in `settings.schema`:

- `lsp.idleStopMinutes` (app and solution tiers, 10): a language server none of whose documents a window shows for this many minutes stops and gives its memory back; it starts again when one is shown or asked about; 0 keeps servers running (`docs/24` "Lifecycle").
- `keyboard.bindings` (app tier): the owner's keyboard shortcuts, `[{command, key?, when?, args?, remove?}]`. An entry with a `key` adds that key (or chord) to the command; one with `remove: true` takes the command's default `key` away, or every default key when it names none. The owner's bindings come after the shell's and the plugins' and so win; the shell applies them as the app tier changes (`settings:app`), in every window. Settings > Environment > **Keyboard** edits them (Help > Keyboard Shortcuts opens it): every command with its keys, searchable, "Only what I changed"; Add records a key or a two-key chord (Enter adds, Escape cancels; the commands that already have it are named) with a when-clause defaulting to the command's first default's; a key's x takes it off (a default shows struck through until its command is reset); Reset drops everything the owner said about the command.

### The editor's settings

The editor plugin's (`plugins/editor/settings.schema.json`, Settings > Text Editor, app tier, applied to open editors as they change): `editor.renderWhitespace` (all by default - owner 2026-10-21; trailing, boundary, selection, none; View White Space, Ctrl+R, Ctrl+W, flips all/none), `editor.wordWrap` (off; Word Wrap, Ctrl+E, Ctrl+W, flips it), `editor.lineNumbers` (on, relative, off), `editor.minimap` (on; never on a phone), `editor.stickyScroll` (on), `editor.fontSize` (0: the theme's; Ctrl+Shift+. and Ctrl+Shift+, zoom for a while) and `editor.bracketPairColorization` (off: color in code carries a token's kind).

## Theme

**Dark only.** Grayscale base; color only where it carries meaning; every color is a token in `frontend/css/theme.css`, which is authoritative for the values below. Plugins must use tokens - raw hex values in plugin CSS or color contributions are rejected at validation (the `semanticTokenColors` contribution maps token types to `--syntax-*` names, never to colors). The values were checked on the M0 static mocks (`frontend/mocks/`, screenshots at 1920x1080, 1366x768 and 390x844); `frontend/mocks/00-tokens.html` shows every token with its live value.

### Grayscale ramp

| Token | Value | Use |
|---|---|---|
| `--bg-0` | `#0e0e0e` | app chrome background, sidebar |
| `--bg-1` | `#141414` | panels, tool windows, Solution Explorer |
| `--bg-2` | `#1a1a1a` | editor and document surfaces |
| `--bg-3` | `#222222` | hover, raised rows, input fields |
| `--bg-4` | `#2c2c2c` | inactive selection, pressed |
| `--border-0` | `#262626` | separators |
| `--border-1` | `#363636` | input borders, focused panel edges |
| `--text-0` | `#e8e8e8` | primary text |
| `--text-1` | `#b4b4b4` | secondary text, labels |
| `--text-2` | `#808080` | tertiary, line numbers, placeholders |
| `--text-3` | `#565656` | disabled |
| `--focus` | `#d0d0d0` | focus ring (grayscale; 1 px) |
| `--selection` | `#3a3a3a` | text selection background |
| `--selection-active` | `#474747` | selected row in the focused list |

### Semantic accents (the only UI colors)

| Token | Value | Meaning |
|---|---|---|
| `--accent-saved` | `#5aa65a` | changed and saved (margin), success, passed tests |
| `--accent-unsaved` | `#d4b13c` | unsaved changes (margin, tab dot) |
| `--accent-error` | `#d9534f` | errors, failed builds/tests/tool calls |
| `--accent-warn` | `#d08a3a` | warnings |
| `--accent-info` | `#4a8fd6` | information, links, "building" status |
| `--accent-ai` | `#9a85cf` | AI-originated content (AI edit markers, thinking headers), used sparingly |
| `--accent-debug` | `#c0679a` | debugging state (status bar while debugging, current-statement marker) |

Accents are used at low area (thin bars, dots, icons, 1-2 px markers), never as large fills, so the UI reads grayscale with meaning "delicately" highlighted as the plan asks. Derived backgrounds (`--tint-*`, diff backgrounds, status bar states) are `color-mix()` tints of the accents, never new hues. Primary buttons are inverted grayscale (`--inverse-bg` / `--inverse-text`), not blue.

### Syntax tokens

Syntax colors carry meaning (token kinds), so they are a sanctioned palette (`docs/99` #16). Every kind has a color token plus `-weight` and `-style` companions (`--syntax-keyword`, `--syntax-keyword-weight`, `--syntax-keyword-style`, ...), and `--syntax-plain` colors untokenized text.

Two palettes ship:

- **Visual Assist X** - the default (owner decision, 2026-10-19). Values are C4Assist's `c4-visual-assist-dark` theme, so C4 code looks the way it did with C4Assist:

| Token | Value | Kind |
|---|---|---|
| `--syntax-keyword` | `#569cd6` | keywords and built-in types (`int32`, `string`) |
| `--syntax-control` | `#d8a0df` | control flow (`if`, `for`, `return`) |
| `--syntax-type` | `#ffd700` | user types (class, struct, enum, type parameter) |
| `--syntax-function` | `#ff8000` | functions and methods |
| `--syntax-variable` | `#bdb76b` | variables |
| `--syntax-parameter` | `#bdb76b` | parameters |
| `--syntax-member` | `#bdb76b` | fields and properties |
| `--syntax-constant` | `#b8d7a3` | enum members, constants (VS dark value; C4Assist leaves it to the base theme) |
| `--syntax-namespace` | `#b5cea8` | namespaces |
| `--syntax-macro` | `#bd63c5` | macro names where defined and where used (`SOME_MACRO` in `#define SOME_MACRO`), C4 attributes |
| `--syntax-preprocessor` | `#9b9b9b` | directives: `#define`, `#include`, `#if`, C4's `include` (Visual Studio's preprocessor keyword gray; owner choice 2026-10-21); code a preprocessor condition leaves out |
| `--syntax-string` | `#d69d85` | strings |
| `--syntax-number` | `#b5cea8` | numbers |
| `--syntax-comment` | `#57a64a` | comments |
| `--syntax-operator`, `--syntax-plain` | `#b4b4b4` | operators, plain text |
| `--syntax-invalid` | `var(--accent-error)` | invalid tokens |

- **Grayscale** - luminance, weight and italics only; a per-user choice (`data-syntax="grayscale"` on the root or on any subtree).

Language plugins map their server's semantic token legend onto these names with `semanticTokenColors` (`docs/02`; the C4 plugin's in `plugins/c4/ied-plugin.json`, clangd's in `plugins/cpp`, rust-analyzer's in `plugins/rust`). Grammar (Monarch) tokens map by name: `keyword.directive` is the preprocessor gray, an `#include`'s path a string, a grammar's per-keyword control-flow names (`keyword.return`) the control color. Monaco's theme, the xterm/ANSI palette and the Output window are generated from the same tokens, so editor, terminal and chat cannot drift. Tokens a generator reads (syntax, ANSI, terminal) are plain hex or `var()` of plain hex; `color-mix()` is only for CSS-only tints.

### Prose

Markdown read at length - the chat's replies - is a third sanctioned palette (owner, 2026-10-22: "more color from the AI output like there is in Claude Code", pleasant in a dark room). Its colors mean which Markdown element: warm for structure (headings, list markers), cool for code, so a reply scans by shape. Body copy is softer than `--text-0` and set larger and looser than the dense UI; fenced code uses the syntax tokens (through the editor's colorizer).

| Token | Value | Use |
|---|---|---|
| `--prose-text` | `#d4d4d4` | body copy (VS Code's editor foreground: bright enough, no glare) |
| `--prose-strong` | `#f0f0f0` | bold, table headers |
| `--prose-heading` | `#e6c384` | headings (warm sand) |
| `--prose-marker` | `#c9a86a` | list bullets and numbers, task boxes |
| `--prose-code` / `--prose-code-bg` | `#b1b9f9` / its 9% tint | inline code (Claude Code's lavender) |
| `--prose-link` | `#7aaee6` | links (a lighter `--accent-info`, readable on the dark surface) |
| `--prose-quote` / `--prose-quote-bar` | `#a6a6a6` / the heading's 45% tint | block quotes |
| `--prose-rule` | `var(--border-1)` | horizontal rules |
| `--prose-font-size` / `--prose-line-height` | `14px` (15 px on a phone) / `1.6` | prose type |

### Other token groups

`frontend/css/theme.css` holds these too; the names are the contract, values are tuned there.

| Group | Tokens | Notes |
|---|---|---|
| Editor surfaces and markers | `--editor-bg`, `--gutter-bg`, `--line-number(-active)`, `--current-line(-border)`, `--indent-guide(-active)`, `--ref-highlight`, `--find-match(-border)`, `--ghost-text`, `--squiggle-error/warn/info/spell`, `--marker-saved/unsaved/ai/breakpoint/current` | breakpoints use `--accent-error` (VS convention), the current statement `--accent-debug`, AI-edited lines `--accent-ai` |
| Diff | `--diff-add-bg`, `--diff-del-bg`, `--diff-add-word`, `--diff-del-word`, `--diff-add-gutter`, `--diff-del-gutter`, `--diff-filler` | tints of saved and error |
| Terminal and Output | `--ansi-*` (16 colors), `--terminal-bg`, `--terminal-fg`, `--terminal-cursor` | red/green/yellow/blue are the accents; magenta and cyan are the only extra hues |
| Identity | `--hue-red/orange/amber/yellow/lime/green/teal/cyan/blue/indigo/violet/pink/gray`, `--series-1` .. `--series-8` | meaning = which thing (a file type or a project's language, a git lane, a profiler thread, a chart series); small areas only. Plugins name the hues in `fileIcons` and `projectIcons` (`docs/02`) |
| File glyphs | `--icon-line`, `--icon-tint`, `--icon-folder(-back)`, `--icon-project`, `--icon-solution` | the page, folder, project tile and solution of `shared/ui-kit/fileglyphs.js` (`docs/15` Icons); a tile is filled with only `--icon-tint` of its hue |
| Profiler | `--prof-zone-0..3` (grayscale by depth), `--prof-zone-border`, `--prof-rename-tint-a/b`, `--prof-rename-edge`, `--prof-frame-bar/over/spike`, `--prof-budget-line`, `--prof-selection`, `--prof-gc` | renamed thread segments are a neutral tint with the name at the boundary |
| Status bar | `--status-idle-bg`, `--status-building-bg`, `--status-debugging-bg`, `--status-failed-bg` | tints of the dark bar (F-SHL-6) |
| Surfaces | `--veil`, `--scroll-thumb(-hover)`, `--map-viewport`, `--inverse-bg`, `--inverse-text`, `--shadow-popup`, `--scrim`, `--link` | |
| Type, spacing, metrics | `--font-ui`, `--font-mono`, `--font-size-xs/sm/ui/lg/xl/code`, `--line-height-code`, `--space-1..8`, `--radius-1..3`, `--row-h`, `--tab-h`, `--sidebar-w`, `--menubar-h`, `--toolbar-h`, `--statusbar-h`, `--panel-header-h`, `--input-h`, `--icon-size`, `--motion-fast/normal` | one `max-width: 719px` block overrides sizes for phones |

### Typography and density

- UI: `system-ui` stack (Segoe UI on Windows, the platform font on phones); 13 px base desktop, 15 px phone.
- Code/terminal: a bundled monospace (Cascadia Mono, OFL) with fallbacks; 13 px default, owner-adjustable.
- Rows 22 px (desktop), 40 px touch targets (phone); 1 px borders; 2-4 px radii; shadows only on menus/popups; minimal animation (respect reduced motion).

## Layout conventions (VS parity)

Thin left activity sidebar; menu bar and project toolbar on top; VS-shaped editor activity (Solution Explorer left, document well center, tool windows docked bottom/right with auto-hide); always-present bottom terminal; status bar. Indentation guides on by default; tabs by default for new files (also a standing AI instruction). Context menus wherever a right-click lands; every command has a rebindable shortcut. Wireframes and the keymap: `docs/15`.

## Open items

- Owner sign-off on the M0 mocks (`frontend/mocks/index.html` lists the open look questions: inverted grayscale primary buttons, grayscale Start button, tinted status bar states, identity colors, wrapped lines in the phone editor). Ramp and accent values held through the mock review; the syntax palette is decided (Visual Assist X).
- Signature "explosion" identity moment (`docs/99` O9).
