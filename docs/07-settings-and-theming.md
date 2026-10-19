# 07 — Settings & theming

## Purpose

Two things: the settings tier model (what lives where, who wins) and the visual language (dark, grayscale, color only with meaning).

## Settings tiers

Three tiers, closest scope wins. Every setting has a canonical owner tier; lower tiers may override.

| Tier | Location | Scope | Examples |
|---|---|---|---|
| **Application** | `%APPDATA%/IED/settings.json` (+ per-plugin files) | this machine, all workspaces | AI models & model groups, SearXNG config, tor/VPN profiles, theme selection, global plugin enable/disable, keybindings |
| **Solution** | `<solution>/.ied/solution.json` (committed) | this project on every machine | toolbar dropdown defaults, secure-mode egress policy, solution-level AI group overrides, local plugin pins |
| **Project** | `<solution>/.ied/projects/<Name>.json` (committed) | one project within the solution | per-project default configuration/architecture, LSP profile selection, sandbox image |

Plus **per-machine workspace state** in `.ied/local/` (gitignored): open tabs, pane layout, last dropdown selections — restored on reopen, never shared.

Rules:

- Each plugin declares a `settings.schema.json`; the settings UI renders it per tier automatically — plugins don't build their own settings pages.
- Effective value = project ⊕ solution ⊕ app (shallow merge per key). The settings UI shows the effective value with an indicator of which tier set it and an "override here" action.
- Schemas are validated on load; invalid files produce a visible error (settings page + status bar), never silent defaults.

## Theme

**Dark only.** No light theme in v1 (the plan is explicit: dark, explosion theming).

### Palette rules

- Base is **grayscale**: near-black background (~#0d0d0d range), mid-grays for panels/borders/secondary text, white-ish for primary text. No off-grays with arbitrary warmth — every gray is a token.
- **Color only carries meaning.** The semantic set (extend deliberately, document additions):

| Token | Color (approx) | Meaning |
|---|---|---|
| `--accent-saved` | green | changed & saved to disk (line indicator) |
| `--accent-unsaved` | yellow | unsaved changes (line indicator) |
| `--accent-error` | red | errors, failed builds/tool calls |
| `--accent-info` | blue | informational highlights, links, selection affordances |
| `--accent-warn` | amber | warnings |
| `--accent-ai` | restrained violet/cyan | AI-generated content markers (inline edits, thinking blocks) — must stay "delicate" per the plan |

- Plugins **must** use tokens; raw hex in plugin CSS is a review-blocker. New semantic colors get added to the shared token file with a documented meaning.

### Token file

`frontend/css/theme.css` defines all tokens as CSS custom properties (backgrounds, borders, text levels, spacing scale, font stacks — monospace for code/terminals, UI sans elsewhere). The webview shell loads it first; Monaco and the ANSI console renderer map their palettes onto these same tokens so editor, terminal and chat can't drift apart.

### Layout conventions (VS parity)

- Thin left sidebar (icons only, ~40px), main content area per selected icon, collapsible bottom terminal, top toolbar + menu bar.
- Dense: small paddings, 12–13px UI text, no rounded-corner decoration beyond subtle 2–4px radii, 1px borders for separation (no drop shadows except on context menus/popups).
- Context menus everywhere a right-click lands; keyboard shortcuts are first-class and rebindable (app-tier setting).

## Open items

- Font pairing (UI + code) — pick during first UI spike; tokens make it a one-line change later.
- Whether "explosion theming" gets a signature accent moment (logo/title treatment) or stays strictly functional grayscale. Leaning: functional, with the family's visual identity limited to iconography and the app chrome.
