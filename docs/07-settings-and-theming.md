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

**Dark only.** Grayscale base; color only where it carries meaning; every color is a token in `frontend/css/theme.css`. Plugins must use tokens - raw hex values in plugin CSS or color contributions are rejected at validation (the `semanticTokenColors` contribution maps token types to `--syntax-*` names, never to colors).

### Grayscale ramp (proposed values; tune on the M0 mocks)

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

### Semantic accents (the only UI colors)

| Token | Value (proposed) | Meaning |
|---|---|---|
| `--accent-saved` | `#5aa65a` | changed and saved (margin), success, passed tests |
| `--accent-unsaved` | `#d4b13c` | unsaved changes (margin, tab dot) |
| `--accent-error` | `#d9534f` | errors, failed builds/tests/tool calls |
| `--accent-warn` | `#d08a3a` | warnings |
| `--accent-info` | `#4a8fd6` | information, links, "building" status |
| `--accent-ai` | `#9a85cf` | AI-originated content (AI edit markers, thinking headers), used sparingly |
| `--accent-debug` | `#c0679a` | debugging state (status bar while debugging, current-statement marker) |

Accents are used at low area (thin bars, dots, icons, 1-2 px markers), never as large fills, so the UI reads grayscale with meaning "delicately" highlighted as the plan asks.

### Syntax tokens

Syntax colors carry meaning (token kinds), so they are a sanctioned palette - restrained and owner-selectable (`docs/99` O4):

`--syntax-keyword`, `--syntax-control`, `--syntax-type`, `--syntax-function`, `--syntax-variable`, `--syntax-parameter`, `--syntax-member`, `--syntax-constant`, `--syntax-namespace`, `--syntax-macro`, `--syntax-string`, `--syntax-number`, `--syntax-comment`, `--syntax-operator`, `--syntax-invalid`.

Two palettes ship: **Muted** (desaturated hues in the Visual Assist tradition, which C4Assist users know) and **Grayscale** (luminance, weight and italics only). Language plugins map their semantic token legend onto these names (C4 example in `shared/host-api/examples/c4-plugin/ied-plugin.json`). Monaco's theme, the xterm/ANSI palette and the Output window are generated from the same tokens, so editor, terminal and chat cannot drift.

### Typography and density

- UI: `system-ui` stack (Segoe UI on Windows, the platform font on phones); 13 px base desktop, 15 px phone.
- Code/terminal: a bundled monospace (Cascadia Mono, OFL) with fallbacks; 13 px default, owner-adjustable.
- Rows 22 px (desktop), 40 px touch targets (phone); 1 px borders; 2-4 px radii; shadows only on menus/popups; minimal animation (respect reduced motion).

## Layout conventions (VS parity)

Thin left activity sidebar; menu bar and project toolbar on top; VS-shaped editor activity (Solution Explorer left, document well center, tool windows docked bottom/right with auto-hide); always-present bottom terminal; status bar. Indentation guides on by default; tabs by default for new files (also a standing AI instruction). Context menus wherever a right-click lands; every command has a rebindable shortcut. Wireframes and the keymap: `docs/15`.

## Open items

- Final hex values and the two syntax palettes are tuned on the M0 static mocks with screenshots at three viewports.
- Signature "explosion" identity moment (`docs/99` O9).
