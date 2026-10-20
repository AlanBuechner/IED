# 07 - Settings and theming

## Purpose

Two things: the settings tier model (what lives where, who wins) and the visual language (dark, grayscale, color only with meaning). Features: `docs/features/14-settings.md`, `docs/features/02-shell-and-layout.md`.

## Settings tiers

| Tier | Location | Scope | Examples |
|---|---|---|---|
| **App** | `%APPDATA%/IED/settings.json` (+ `keybindings.json`) | this owner, all workspaces | model profiles and groups, prompt overrides, egress profiles, remote access, global plugins, keybindings, UI density/fonts |
| **Solution** | `<solution>/.ied/solution.json` (or `.ied/solutions/<Name>.json` when several solutions share a folder) - committed | this solution on every machine | default model group, `aiEgress` policy, permission rules (restrict only), toolbar defaults, plugin settings |
| **Project** | `<solution>/.ied/projects/<Name>.json` - committed | one project | LSP profile choice, sandbox image, project plugin settings |
| Workspace state | `<solution>/.ied/local/` - gitignored | this machine | layouts per device class, open documents, toolbar selections, startup project, breakpoints, bookmarks |

Rules:

- Every setting declares allowed tiers and a merge kind (replace / deep-merge object / append list) in its schema (`x-ied-tiers`, `x-ied-merge`). The effective value is computed per resource (the project owning a file).
- Security-relevant settings can only be *tightened* by repo tiers (a cloned repo cannot widen AI permissions or egress beyond the owner's app tier).
- Validation is visible: invalid values show in the settings window and as Error List entries pointing at the JSON line; the previous valid value stays in effect.
- Settings files are watched (git pull, hand edits apply live) and written preserving comments.
- **Secrets never live in settings files**: settings reference `{ "secret": "<name>" }` (Windows Credential Manager) or `{ "env": "VAR" }`.

## The settings window

The bottom sidebar icon opens Settings as a **docked view** (not a modal), styled like Visual Studio's *Tools > Options*: a tree on the left (Environment, Keyboard, Text Editor > per language, Projects and Solutions, Build and Run, Debugging, AI > Models / Groups / Routing / Privacy / Prompts / Skills / MCP, Web and Egress, Source Control, Terminal, Sandbox, Profiler, Plugins, Accounts, Remote Access, Diagnostics), a tier selector (User / Solution / Project) at the top, search, and "edit as JSON" per tier. Every row shows the effective value, which tier set it, and override/reset actions. Pages render from plugin JSON Schemas; dedicated editors exist for model groups, keybindings, plugins and remote access.

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

- **Visual Assist X** - the default (owner decision, 2026-10-19). Values are C4Assist's `c4-visual-assist-dark` theme (`C4/C4Assist/plugin/echo-plugin.json`), so C4 code looks the way it did with C4Assist:

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
| `--syntax-macro` | `#bd63c5` | macros, attributes, `include` |
| `--syntax-string` | `#d69d85` | strings |
| `--syntax-number` | `#b5cea8` | numbers |
| `--syntax-comment` | `#57a64a` | comments |
| `--syntax-operator`, `--syntax-plain` | `#b4b4b4` | operators, plain text |
| `--syntax-invalid` | `var(--accent-error)` | invalid tokens |

- **Grayscale** - luminance, weight and italics only; a per-user choice (`data-syntax="grayscale"` on the root or on any subtree).

Language plugins map their semantic token legend onto these names (C4 example in `shared/host-api/examples/c4-plugin/ied-plugin.json`). Monaco's theme, the xterm/ANSI palette and the Output window are generated from the same tokens, so editor, terminal and chat cannot drift. Tokens a generator reads (syntax, ANSI, terminal) are plain hex or `var()` of plain hex; `color-mix()` is only for CSS-only tints.

### Other token groups

`frontend/css/theme.css` holds these too; the names are the contract, values are tuned there.

| Group | Tokens | Notes |
|---|---|---|
| Editor surfaces and markers | `--editor-bg`, `--gutter-bg`, `--line-number(-active)`, `--current-line(-border)`, `--indent-guide(-active)`, `--ref-highlight`, `--find-match(-border)`, `--ghost-text`, `--squiggle-error/warn/info/spell`, `--marker-saved/unsaved/ai/breakpoint/current` | breakpoints use `--accent-error` (VS convention), the current statement `--accent-debug`, AI-edited lines `--accent-ai` |
| Diff | `--diff-add-bg`, `--diff-del-bg`, `--diff-add-word`, `--diff-del-word`, `--diff-add-gutter`, `--diff-del-gutter`, `--diff-filler` | tints of saved and error |
| Terminal and Output | `--ansi-*` (16 colors), `--terminal-bg`, `--terminal-fg`, `--terminal-cursor` | red/green/yellow/blue are the accents; magenta and cyan are the only extra hues |
| Identity | `--filetype-source/header/data/doc/script/shader/media/folder/project`, `--series-1` .. `--series-8` | meaning = which thing (file type, git lane, profiler thread, chart series); small areas only |
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
