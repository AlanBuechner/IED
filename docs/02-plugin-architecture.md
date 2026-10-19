# 02 — Plugin architecture

## Purpose

Define how IED is extended, because "the main parts of the project will be implemented as plugins" (IDEplan). This doc sketches **manifest v1 and the contribution model** concretely enough to start coding; exact field names get finalized in `shared/host-api/`.

## Two plugin languages, one host API

- **C4 plugins** — native, loaded by the host. Used where performance or OS access matters (the editor's LSP/DAP glue, sandbox docker control).
- **JS plugins** — run in a bundled JS runtime inside the host. Used for UI-centric logic and anything quick to iterate on.

Both implement the same contribution interfaces; the manifest declares which runtime a plugin uses. v1 ships both runtimes but every default plugin may start as JS and go native where profiling demands.

## Plugin locations & trust

| Scope | Where | Trust model |
|---|---|---|
| **Global** | user app-data dir (e.g. `%APPDATA%/IED/plugins/`) | installed by the user; enable/disable per plugin |
| **Project-local** | referenced from the solution file, path relative to the solution root | only auto-enabled when the workspace is **trusted**; a project can pin its own language plugin + LSP this way (this is how C4 vendors its language support, and how Claymore could ship a GPU-debugger/shader-LSP plugin) |

Trust: opening an untrusted workspace shows a banner; project-local plugins and AI egress stay off until the user marks it trusted. Per-project "secure mode" (tor/VPN egress for AI web access) is a separate setting — see 04.

## Manifest v1 sketch

```jsonc
{
  "id": "ied.chat",                 // unique, dotted; stable forever
  "name": "Chat",
  "version": "0.1.0",
  "runtime": "js",                  // or "c4"
  "entry": "./main.js",             // or native lib name for c4 runtime
  "icon": "./icon.svg",             // sidebar icon if it registers one
  "permissions": [                  // explicit; host validates at load, UI shows them on install
    "fs.read", "fs.write", "shell.exec", "ai.models", "terminal.spawn"
  ],
  "contributions": {
    "sidebarIcons": [ { "id": "chat", "order": 0 } ],   // thin left sidebar; order = top-to-bottom
    "panels":       [ { "iconId": "chat", "title": "Chat" } ],
    "toolbarButtons": [ ... ],      // see 06 — command templates + dropdown injection
    "outputTabs":   [ { "id": "build", "title": "Build" } ],
    "lspProfiles":  [ { "id": "c4", "command": "...", "initializationOptionsFile": ".ied/lsp-options/c4.json" } ],
    "dapProviders": [ ... ],
    "skills":       [ { "name": "...", "path": "./skills/..." } ],
    "toolCalls":    [ ... ],        // AI-facing tools this plugin offers
    "bindings":     [ ... ],        // named hooks other plugins can call (see below)
    "settings":     { "schema": "./settings.schema.json" },
    "systemMessageSections": [ { "text": "Docs for this project: docs/..." } ]
  },
  "dependencies": [ { "id": "ied.editor", "version": ">=0.1" } ]
}
```

## Contribution points (the full v1 list)

| Point | Consumer | Notes |
|---|---|---|
| `sidebarIcons` | shell | thin icon strip, left; plugins may or may not add one |
| `panels` | shell | the main content area per sidebar icon; panels contain tabs/dockable views |
| `toolbarButtons` / `toolbars` | editor plugin (hosted) | VS-style top toolbar: dropdowns + buttons with command templates — see 06 |
| `outputTabs` | editor plugin's output window | dropdown-selected tabs, ANSI-colored console rendering |
| `lspProfiles` | host LSP pool | a profile = how to launch/configure one language server; the editor consumes profiles generically |
| `dapProviders` | host DAP pool | debug adapter launch config per language/target |
| `skills` | AI runtime | markdown skills, discoverable by keyword search (see 04) |
| `toolCalls` | AI runtime | named tools with JSON schemas; the chat plugin's defaults are themselves a plugin contribution |
| `bindings` | any plugin | **the composition mechanism** — see below |
| `settings` | settings UI | per-plugin schema, rendered in app/solution/project tiers (see 07) |
| `systemMessageSections` | AI runtime | small text appended to the AI system message; e.g. "we have some docs for this over here <path>" |

## Bindings — composing plugins on plugins

A binding is a **named, typed hook** one plugin publishes and another subscribes to or calls. Examples:

- `editor.openFile(path, line?)` — git plugin jumps from a commit diff to the editor; chat's file tools open results in the editor.
- `editor.getSelectedText()` / `editor.applyEdit(range, text)` — inline AI edit tool targets.
- `terminal.run(command)` — build buttons and the AI shell tool share one implementation.
- `chat.injectMessage(text)` — "inject commands into the chat" feature; other plugins can feed guidance.
- `build.status` (event) — profiler plugin starts a session when a run button's process starts.

Rules: bindings are declared in the manifest (name + schema), so callers never import callee internals; calling an undeclared binding is a load-time error; events flow through the same registry. This is what lets "the C4 language configure its own vendored language plugin" and later let Claymore's plugins call into IED's editor/chat without either knowing about the other.

## Skills & tool calls (AI-facing surface)

- **Skills**: markdown files with frontmatter (name, description, keywords). Stored per-project in `.ied/skills/` (committed — survives build cleanup), globally in app-data, plus plugin-bundled ones. The AI searches them by keyword at runtime instead of loading all into context (see 04 for the search design).
- **Tool calls**: JSON-schema-described functions the model can invoke. Default set ships with the chat plugin: file read/write/search, shell exec, image/video load, screenshot/webpage render + console capture + DOM interaction. Plugins add more (editor build/run/debug tools, LSP navigation, profiler queries…).

## Lifecycle

```
discover -> validate manifest (schema + permissions) -> [trust check for project-local]
-> enable -> load runtime entry -> register contributions -> ready
disable: unregister contributions, dispose resources; state persisted per tier
```

Validation failures surface in a "Plugins" settings page with the error text — never silent.

## Open items

- Final manifest schema → `shared/host-api/` (blocking, see 99).
- JS runtime choice (QuickJS vs ChakraCore) — decide when first JS plugin lands; both are vendored single binaries, low risk either way.
- Plugin sandboxing: v1 trusts enabled plugins within their declared permissions (permissions enforced by the host at each API call). Out-of-process isolation is a later hardening step.
