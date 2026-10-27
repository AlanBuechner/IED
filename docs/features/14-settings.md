# Features: settings (SET)

Owner: `src/settings` (store, schemas, merge) + `frontend/` (settings window). Design: `docs/07`.

## Store and tiers

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-SET-1 | Tiers: app (`%APPDATA%/IED/settings.json`), solution (`.ied/solution.json` or `.ied/solutions/<Name>.json`), project (`.ied/projects/<Name>.json`), plus per-machine workspace state (`.ied/local/`) | P0 | US-SET-1 |
| F-SET-2 | Each setting declares its allowed tiers and merge kind (replace / deep-merge object / append list); effective value computed per resource (project of the file) | P0 | US-SET-1 |
| F-SET-3 | Validation with visible errors (settings window + Error List entry pointing at the JSON line); invalid values ignored with the error shown, never silently defaulted | P0 | US-SET-1 |
| F-SET-4 | Live reload when settings files change on disk (git pull, hand edit); subscribers notified | P0 | - |
| F-SET-5 | Settings writes preserve comments and formatting (JSONC-preserving edits) | P1 | US-SET-5 |

## Settings window

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-SET-10 | Opened from the bottom sidebar icon; a docked view (not a modal) with a VS Options-style tree on the left: Environment, Keyboard, Text Editor (per language), Projects and Solutions, Build and Run, Debugging, AI (Models, Groups, Routing, Privacy, Prompts, Skills, MCP), Web and Egress, Source Control, Terminal, Sandbox, Profiler, Plugins, Accounts, Remote Access, Diagnostics | P0 | US-SET-1 |
| F-SET-11 | Tier selector (User / Solution / Project) at the top; each setting shows the effective value, which tier set it, "override here" and "reset" actions | P0 | US-SET-1 |
| F-SET-12 | Schema-driven rendering: plugins contribute schemas and get pages without UI code; custom pages allowed for complex editors (model groups, keybindings) | P0 | US-SET-1 |
| F-SET-13 | Search across all settings (titles, descriptions, keys) | P1 | US-SET-5 |
| F-SET-14 | "Edit as JSON" per tier, opening the file in the editor with schema-backed completion and validation | P1 | US-SET-5 |

## Dedicated editors

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-SET-20 | Models page: endpoints list, discover models, add/edit profile (F-AIR-9 fields), test connection with live probe results, privacy class badge | P0 | US-SET-2, US-MDL-6 |
| F-SET-21 | Model groups page: grid of roles x (model, temperature, effort, fallbacks); duplicate group; mark default; validation (e.g. vision role must be vision-capable) | P0 | US-SET-2 |
| F-SET-22 | Prompts page: base system prompt (view default, edit override, diff against default, reset), style rules, per-mode instructions | P0 | US-CTX-3 |
| F-SET-23 | Keybindings page: searchable list, record-a-key, chords, `when` editor, conflict detection, VS scheme default, reset per binding | P1 | US-SET-4 |
| F-SET-24 | Plugins page: installed plugins, source (built-in / app / project), version, enabled toggle, permissions, contributions summary, errors, logs link | P0 | US-PLG-7 |
| F-SET-25 | Egress page: SearXNG status + engines, tor status (bootstrap %), proxy/VPN profiles, per-workspace policy | P1 | US-WEB-2 |
| F-SET-26 | Accounts page: Anthropic API key, Claude Code bridge status (installed CLI, signed-in state as reported by the CLI), usage history | P1 | US-CLD-2 |
| F-SET-27 | Remote access page: bind address, password, devices (revoke), TLS, URL + QR code for the phone | P0 | US-SRV-4, US-SRV-5 |
| F-SET-28 | Skills page: all skills by source, search, open, enable/disable per skill, usage counts | P1 | US-CTX-4 |
| F-SET-29 | MCP page: servers per tier, status, tools list, logs. Built: Settings > AI > MCP Servers (`plugins/mcp/ui`, `docs/07` "MCP servers") | P1 | US-TOOL-6 |

## Secrets

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-SET-30 | Secret store backed by Windows Credential Manager (DPAPI); settings reference secrets by name (`{"apiKey": {"secret": "anthropic"}}`); no plaintext fallback | P0 | US-SET-3 |
| F-SET-31 | Secrets never leave the server: not sent to clients (masked), redacted from logs, trajectories and tool results | P0 | US-SET-3 |
| F-SET-32 | Environment-variable references as an alternative (`{"env": "ANTHROPIC_API_KEY"}`) | P0 | US-SET-3 |
