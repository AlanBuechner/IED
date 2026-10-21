# Features: plugins and extensibility (PLG)

Owner: `src/plugins` (host) + `frontend/` (client-side contribution loader) + `shared/host-api` (contract). Design: `docs/02`; schema: `shared/host-api/schemas/plugin-manifest.v1.schema.json`.

## Runtimes

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PLG-1 | **Built-in plugins** (the six defaults) are C4 modules compiled into the server (C4 has no DLL output, `docs/17`), registered through the same manifest + contribution API as any other plugin, individually disable-able | P0 | US-PLG-1 |
| F-PLG-2 | **Declarative plugins**: manifest-only contributions (languages, LSP profiles, DAP providers, problem matchers, toolbar presets, skills, snippets, themes-as-token-maps, system-message sections, templates) - no code at all; this covers most of what C4Assist does today | P0 | US-PLG-1, US-XRP-1 |
| F-PLG-3 | **JS plugin logic** in QuickJS-ng on the server (one runtime per plugin, own heap, interrupt handler for runaway scripts, async host calls via promises) | P1 | US-PLG-1 |
| F-PLG-4 | **Process plugins** ("backends"): any executable (C4, Go, Rust, Python...) speaking JSON-RPC 2.0 over stdio (`ied-plugin-rpc/1`); lazy start, restart with backoff, crash quarantine | P1 | US-PLG-1 |
| F-PLG-5 | **UI modules**: ES modules served by the server and loaded into the web client; render into host-provided containers using `shared/ui-kit` and theme tokens; talk to their logic only through the host API | P0 (built-ins), P1 (third party) | US-PLG-1 |

## Contribution points (full list in `docs/02`)

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PLG-10 | Shell: `activities` (sidebar icons + views), `toolWindows`, `commands`, `menus`, `keybindings`, `statusBarItems`, `toolbars`, `notifications` kinds | P0 | US-PLG-1 |
| F-PLG-11 | Editor: `languages` (ids, extensions, comment tokens, brackets, auto-close pairs, indentation rules, Monarch grammar), `semanticTokenColors` (token type/modifier -> theme token), `fileIcons`, `projectIcons`, `fileViewers` (custom editors), `snippets`, `headerSourcePairs`, `fileNesting`, `spellDictionaries` | P0 (languages, colors, icons), P1 (rest) | US-PLG-5 |
| F-PLG-12 | Build/debug: `taskPresets` (toolbar presets), `problemMatchers`, `testProviders`, `dapProviders`, `lspProfiles`, `projectTemplates`, `outputSources` | P0 | US-PLG-5 |
| F-PLG-13 | AI: `toolCalls`, `skills`, `systemMessageSections`, `agentModes`, `mcpServers`, `modelProviders`, `chatSlashCommands` | P0 (tools, skills, sections), P1 (rest) | US-PLG-5 |
| F-PLG-14 | Infra: `settings` (JSON Schema per tier), `bindings` (provide/consume), `sandboxProviders`, `sourceControlProviders`, `profilerImporters` | P1 | US-PLG-5 |
| F-PLG-15 | `when` clauses on contributions (context keys: `workspaceTrusted`, `resourceExtname`, `debugState`, `config`, setting values...) so features toggle without code (C4Assist's 9 feature toggles become settings + `when`) | P0 | US-PLG-5 |

## Bindings

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PLG-20 | Binding registry: plugins `provide` named, versioned, schema-typed functions and events; others `consume` them; resolution at load time; missing required binding = load error, optional = feature degrades | P0 | US-PLG-3 |
| F-PLG-21 | Bindings are callable from server logic (C4/JS/process), UI modules, task steps (`binding` step kind) and - when marked `aiExposed` - as AI tools | P0 | US-PLG-3 |
| F-PLG-22 | Event bindings (pub/sub) with typed payloads; delivery on the server; UI can subscribe via the WS hub | P0 | US-PLG-3 |

## Lifecycle, trust and safety

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PLG-30 | Discovery: built-ins, app-tier plugins dir, project-local pins in the solution; precedence when ids clash: project-local (trusted) > app > built-in, with a visible "overridden" note | P0 | US-PLG-2 |
| F-PLG-31 | Validation against the manifest schema, API version range (`ied.api: "^1"`), permissions; errors shown in Settings > Plugins and the IED output source | P0 | US-PLG-4 |
| F-PLG-32 | Permissions declared and enforced at each host API call (`fs.read`, `fs.write`, `process.spawn`, `net.egress`, `ai.invoke`, `ai.tools`, `terminal`, `secrets`, `ui.notifications`, ...); shown before enabling; a permission increase on update requires re-approval | P0 (declare/show), P1 (enforce everywhere) | US-PLG-7 |
| F-PLG-33 | Project-local plugins load only in trusted workspaces; app-tier plugins need one-time owner approval (content hash) | P0 | US-PLG-2 |
| F-PLG-34 | Safe mode (`ied --safe-mode`): starts with all non-built-in plugins disabled; repeated crashes quarantine a plugin automatically | P1 | US-OPS-1 |
| F-PLG-35 | Install from folder, zip, or git URL into the app-tier dir; staged + reviewed before activation; uninstall; update | P1 | US-PLG-6 |

## Plugin developer experience

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PLG-40 | Hot reload: manifest/UI module/JS changes reload the plugin without restarting the server (built-ins excluded) | P1 | US-PLG-4 |
| F-PLG-41 | Per-plugin output source ("Plugin: <id>") with log levels; host API call tracing toggle | P1 | US-PLG-4 |
| F-PLG-42 | Scaffold command + project template "IED plugin" (declarative / JS / process variants) | P1 | US-PLG-4 |
| F-PLG-43 | `ied-plugin-authoring` skill + AI tools to scaffold, validate and stage a plugin; nothing runs before approval | P1 | US-PLG-6 |
| F-PLG-44 | Published typings (`ied.d.ts`) for JS and UI modules, generated from `shared/host-api` | P1 | US-PLG-4 |
| F-PLG-45 | Contract test kit: a plugin author can run their plugin against a mock host in CI | P2 | US-PLG-4 |
