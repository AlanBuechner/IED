# 02 - Plugin architecture

## Purpose

Define how IED is extended, because "the main parts of the project will be implemented as plugins" (IDEplan). The contract is frozen in `shared/host-api/schemas/plugin-manifest.v1.schema.json`; this doc explains the model behind it. Feature list: `docs/features/13-plugins.md`.

## Four runtimes, one contribution model

C4 cannot produce or safely load DLLs (process-global GC state, no DLL output - `docs/17`), so "native C4 plugins loaded at runtime" is not a v1 option. Plugins come in four kinds, all using the same manifest and contribution points:

| Kind | Code runs | Used for | Isolation |
|---|---|---|---|
| `builtin` | compiled into `ied-server` (C4 modules) | the default plugins: chat, editor, git, terminal, sandbox, profiler | none (trusted, but declare permissions and can be disabled) |
| `declarative` | nowhere - manifest + data files only | language plugins (C4!), task presets, problem matchers, LSP/DAP profiles, skills, snippets, templates, token color maps, system-message sections | n/a |
| `js` | QuickJS-ng on the server, one runtime per plugin | glue logic: bindings, AI tools, small services | own heap + interrupt handler; host API permission-checked |
| `process` | a separate executable speaking `ied-plugin-rpc/1` (JSON-RPC over stdio) | heavy or native logic in any language - including a C4 program built by Nuke | OS process; crash restart + quarantine |

Every plugin may also ship **UI modules** (ES modules served by the server, executed in the browser) that render activities, tool windows, viewers and status items using `shared/ui-kit` and theme tokens. Plugin *logic* always runs server-side (work continues with no browser open); UI is only a view.

Most third-party language/engine support should need **no code at all**: the C4 language plugin (C4Assist rewrite) is fully declarative - see `shared/host-api/examples/c4-plugin/ied-plugin.json`.

## Locations and trust

| Scope | Where | Loads when |
|---|---|---|
| Built-in | inside `ied-server` + `plugins/` assets in the install | always (disable-able) |
| App (global) | `%APPDATA%/IED/plugins/<id>/` | owner approved the plugin's content hash and permissions |
| Project-local | path referenced by the solution's `localPlugins` | the workspace is **trusted** (`docs/11`) |

If ids clash: project-local (trusted) > app > built-in, with a visible "overridden by" note in Settings > Plugins. That is how a repo pins *its* version of a language plugin (a C4 project vendors its plugin under `Nuke/tools/ied`, a game engine can ship a shader-language or GPU-debugger plugin).

## Lifecycle

```
discover (built-ins, app dir, solution localPlugins)
 -> validate manifest (schema, ied.api range, permissions, dependency graph)
 -> trust/approval check
 -> register declarative contributions (always; cheap)
 -> activate code lazily on its activation events (onLanguage:c4, onCommand:..., onTool:..., onView:...)
 -> ready
disable/unload: unregister contributions, deactivate(), stop process, drop JS runtime; state persisted per tier
```

Validation failures appear in Settings > Plugins and the *IED* output source - never silent. Hot reload (non-built-ins) re-runs the lifecycle for one plugin without restarting the server.

## The host as built (M1)

`src/plugins/` loads `builtin` and `declarative` plugins. `js` and `process` manifests are read and validated, then refused with "cannot load yet" until M11.

**Where plugins come from.**

- Built-ins live in `<home>/plugins/<folder>/ied-plugin.json`, where `<home>` is the install: the nearest folder above `ied-server.exe` that holds `shared/host-api/schemas/`, or `--home`.
- App plugins live in `<app data>/plugins/<folder>/` (`%APPDATA%\IED`, or `--app-data`). Changes there reload the plugins without a restart.
- Project-local plugins are a solution's `localPlugins`. They are read per workspace, are `untrusted` until the workspace is trusted, and fail when their manifest's id is not the one the solution names.

A folder without `ied-plugin.json` is not a plugin.

**Checks.** Each manifest is JSONC, validated against `plugin-manifest.v1.schema.json` with Bedrock's `Serialization::ValidateJson`; problems carry the JSON pointer. The host then checks:

- `ied.api` against the host API version (1.0.0) with semver ranges (`^`, `~`, `>=`, `1.x`, exact);
- the runtime kind: `builtin` only from the install;
- required dependencies: loaded and in range, with no cycles. A plugin whose dependency fails fails too.

Problems go to the server log; the IED output source comes later.

**States.** `loaded`, `disabled` (the owner's switch, or the solution's `enabled: false`), `failed` (with its problems), `needsApproval`, `untrusted` (a local plugin of an untrusted workspace), or `overridden` (another plugin with the id won; `overriddenBy` names its source).

**Approval.** An app plugin loads only once the owner approved the SHA-256 of its folder: every file's path and bytes, in name order. A changed file needs approval again, and approvals go to the security log.

**Precedence.** Of the plugins with one id, the one from the strongest source that can load wins: project, then app, then built-in. An app plugin that awaits approval therefore leaves the built-in serving.

**The owner's choices** (disabled ids, approved digests) are kept in `<app data>/plugins.json`, written atomically.

| Method | Does |
|---|---|
| `plugins.list {wsId?}` | `{plugins: [{id, name, version, kind, source, state, overriddenBy?, problems, permissions, contributes: [point names]}]}`, with a workspace's local plugins when `wsId` is given (without it, also the `plugins` topic's snapshot; `plugins.changed` carries the same) |
| `plugins.contributions {wsId?}` | `{plugins: [{id, source, contributes, ui?}]}` for the loaded ones, in load order, `ui` the URL of the plugin's UI module: what the web client builds menus, commands, keybindings and languages from. With `wsId`, a loaded local plugin replaces a global one of its id. |
| `plugins.setEnabled {id, enabled}` | the plugin's summary after the change |
| `plugins.approve {id}` | approves the app plugin's current content |
| `plugins.reload` | reads every plugin folder again |

`GET /plugins/<id>/<path>` serves a loaded plugin's files (grammars, icons, UI modules) to a signed-in client, never one outside the plugin's folder; `GET /w/<wsId>/plugins/<id>/<path>` serves the plugin loaded for a workspace, its project-local plugins included (only while the workspace is trusted). `runtime.ui` must name a file in the plugin's folder by a path a URL carries as it is (letters, digits, `.`, `_`, `-`, `/`), or the plugin fails.

### UI modules (host API v1, browser side)

A plugin's `runtime.ui` names an ES module in its folder; `plugins.contributions` gives a loaded plugin's module as `ui` (`/plugins/<id>/<path>`). The web client imports each module once per page, calls its `activate(ied)` export (when it has one) before rendering, and renders a contribution's `view` (activity, tool window, status bar item) by calling the module's export of that name: `view(ctx)` fills `ctx.el` and may return `{dispose(), focus(), visible(shown)}`. `ctx` is `{el, ied, ref, wsId, tab}`, with `ref` the tab's ref (`docs/15`) and `tab` its handle: `setTitle(text)`, `setDirty(bool)`, `pin()` (keep a preview tab), `close()`. A view that throws, or a module that fails to load, shows its error in place and in a notification.

| Member of `ied` | |
|---|---|
| `client` | the protocol client: `request(method, params, {opId, signal}) -> Promise`, `subscribe(topic, {snap, evt, gone}) -> unsubscribe` (resubscribed after a resume or gap, `snap` called again), `newOpId()`, `clientId` (this tab, as leases and `by` fields name it) |
| `commands.handle(id, fn)` | `fn(args)` runs a contributed command; `commands.run(id, args)` runs any command |
| `tabs.kind(kind, {view, title(arg), icon(arg), resource(arg)})` | a tab kind: refs `<kind>:<arg>` render with the module's export `view`; `resource` names the workspace file a tab shows (the context keys `resourcePath`, `resourceExtname`) |
| `tabs.open(ref, {preview})` | opens or focuses a tab in the current activity's well; `preview` is the VS preview tab (replaced by the next preview, kept once edited or double-clicked) |
| `context.set(key, value)`, `context.get(key)` | context keys for when-clauses |
| `contributions(point)` | every loaded plugin's entries of a contribution point, in load order: `[{plugin: {id, base}, value}]` (the editor reads `languages` this way) |
| `menus.show(menuId, at, args)` | shows a contributed menu (e.g. `explorer/context`) at a mouse event or element; its commands get `args` |
| `workspace` | `{wsId, name, kind, root, trusted}` of the shown workspace; `workspace.on(fn)` for changes |
| `notify(text, level)` | a notification for every device (`server.notify`) |
| `report(message, error)` | a failure for this person (a toast and the console), attributed to the plugin |
| `toast(text, {level, action})` | news for this person only, with an optional `action: {label, run}` button (Undo after a delete) |
| `plugin` | `{id, base}`: the plugin's id and the URL its files are served under |

Shared components (`shared/ui-kit/`, served at `/ui-kit/`) are imported by URL: `import { h, icon } from "/ui-kit/dom.js"`. Among them: `tree.js` (a virtualized tree with in-place naming), `dialog.js` (`confirmDialog`, the one modal question), `menu.js`, `docsync.js`, `ansi.js`, `markdown.js` (GitHub-flavored Markdown to elements).

## Contribution points

Grouped; full shapes in the schema.

| Group | Points |
|---|---|
| Shell | `activities` (sidebar icons + views), `toolWindows`, `commands`, `menus`, `keybindings`, `statusBarItems`, `toolbarItems` (a view rendered in the toolbar after its commands: the task toolbar's dropdowns and buttons), `panels` (`{id, title, icon, view}`: the full-width dock under every activity, collapsible to its view's header row - the terminal dock; the first loaded plugin's), `outputSources` |
| Editor | `languages` (extensions, comments, brackets, auto-close, indentation, Monarch grammar, task comments), `semanticTokenColors` (token -> `--syntax-*` theme token; raw colors rejected), `fileIcons`, `fileViewers` (custom editors), `snippets`, `headerSourcePairs`, `fileNesting`, `spellDictionaries`, `fileOpener` (`{command}`: the command the shell runs with `{path, line, col}`, 1-based, for a file a launch URL names; the last loaded plugin's wins) |
| Build / debug | `lspProfiles`, `dapProviders`, `problemMatchers`, `testProviders`, `taskPresets` (toolbar presets), `projectTemplates` |
| AI | `toolCalls`, `toolGroups`, `skills`, `systemMessageSections`, `agentModes`, `mcpServers`, `modelProviders`, `chatSlashCommands` |
| Infra | `settings` (JSON Schema with tier hints), `bindings` (provides/consumes), `sandboxProviders`, `sourceControlProviders`, `profilerImporters` |

LSP profiles carry the quirk switches a family's servers need (`launchMode: staged`, `positionEncoding: bytes`, `requestIds: integer`, `refetchOnDiagnostics`, `codeActionsNeedClientFilter`), so the host's LSP client stays generic and a server's quirks live in its plugin (the C4 plugin needs `launchMode`, `refetchOnDiagnostics` and `codeActionsNeedClientFilter` since C4LSP's L1 and L2, `docs/17`).

## Bindings - composing plugins on plugins

A binding is a **named, versioned, schema-typed function or event** that one plugin `provides` and others `consume`:

- `editor.openFile { path, line?, column?, split? }` - git, chat, profiler and the AI's `ide_open` all jump to code through it.
- `editor.applyEdit`, `editor.getSelection` - inline AI and refactor tools.
- `terminal.run { argv, cwd, title }` - tasks, AI shell tool and humans share one implementation.
- `chat.inject { chatId, text }` - "inject commands into the chat", usable by any plugin.
- `tasks.run { task, vars }`, event `tasks.status` - the profiler starts capturing when a run launches.
- `orc.createGoal { workspace, title, ... }` - cross-workspace handoff (`docs/features/16`).

Rules: a consumer never imports a provider's internals; a missing *required* binding is a load error, a missing *optional* one degrades the feature visibly; events flow through the same registry. Bindings marked `aiExposed` can surface as AI tools; any binding can be a task step (`{ "binding": "..." }`) and a command handler. This is how the C4 plugin, a game-engine plugin and IED's built-ins cooperate without knowing about each other.

## Skills and AI tools

- **Skills:** markdown `<name>/SKILL.md` with frontmatter (`name`, `description`, optional `keywords`, `triggers`, `tools`) - format in `shared/host-api/README.md`. Sources: `.ied/skills/` (committed), app-tier skills, plugin `skills` paths. The AI gets an index and searches/loads bodies on demand (`docs/04`).
- **Tool calls:** declared in the manifest with JSON schemas, class (read/write/exec/net/meta), group and a binding that implements them. The host runs permission checks and approvals (`docs/11`) and renders them live (`docs/13` for the built-in catalog).

## Permissions

Declared in the manifest (`fs.read`, `fs.write`, `fs.watch`, `process.spawn`, `terminal`, `net.egress`, `net.listen`, `ai.invoke`, `ai.tools`, `ai.context`, `lsp`, `dap`, `git`, `sandbox`, `profiler`, `secrets`, `settings.write`, `ui.notifications`, `ui.clipboard`, `ui.externalLinks`), shown before enabling, enforced at the host API for JS and process plugins. A permission increase on update requires re-approval. UI modules run with full client privileges, so they are exactly as trusted as their plugin; untrusted third-party UI uses the sandboxed iframe view kind (`docs/11`).

## Open items

- Freeze the manifest schema after owner review (M0).
- Typings generation (`ied.d.ts`) from `shared/host-api` (M11).
