# shared/host-api - the contract

Versioned definitions that the server, the web client and every plugin code against. **Frozen formats live here first**; docs explain them, code implements them. Changing a frozen format needs a version bump and a migration note in `docs/99` history.

| Path | What | Status | Doc |
|---|---|---|---|
| `schemas/plugin-manifest.v1.schema.json` | `ied-plugin.json` | v1 draft, ready | `docs/02` |
| `schemas/solution.v1.schema.json` | `<Name>.ied.sln` | v1 draft, ready | `docs/03` |
| `schemas/project.v1.schema.json` | `<Name>.ied.proj` incl. variables, tasks, steps, toolbar, debug configs | v1 draft, ready | `docs/03`, `docs/06` |
| `schemas/federation.v1.schema.json` | `<Name>.ied.fed` | v1 draft | `docs/03` |
| `schemas/agent-mode.v1.schema.json` | `.ied/modes/<id>.json` | v1 draft | `docs/12` |
| `protocol/frames.v1.schema.json` | WebSocket frames | v1 draft, ready | `docs/10` |
| `examples/` | A game-shaped solution + project (`MyGame.ied.sln`, `Game.ied.proj`), and the C4 language plugin as a declarative manifest | illustrative | - |

Validate the examples after any schema change (a minimal validator is enough; the schemas use only: type, required, properties, additionalProperties, items, enum, const, pattern, oneOf, minItems, local and cross-file `$ref`).

## Conventions shared by all formats

- JSON with `//` comments allowed when read by IED (JSONC); written files preserve comments (F-SET-5).
- Every top-level format has `version` (or `manifestVersion`) as an integer. Readers reject unknown major versions with a visible error.
- Paths are relative to the file that contains them unless stated; `/` separators accepted on Windows.
- **Variables:** `${name}` everywhere (commands, paths, launch configs). Built-ins: `solutionDir`, `projectRoot`, `projectFile`, `project`, `pluginDir`, `config`, `arch`, `system`, `file`, `fileDir`, `fileBasename`, `line`, `column`, `selection`, `startupProject`, `testFilter`, `cwd`, `lspIncludePaths`; plus `env:NAME`, `config:key`, `input:prompt text`, `param.<name>` (templates), `buildSystem.<field>`, and any variable the project or preset declares. Unknown variables are an error at task start (shown, not silently empty).
- **`when` expressions:** `&&`, `||`, `!`, `==`, `!=`, `=~` (regex), parentheses; operands are context keys (`workspaceTrusted`, `resourceExtname`, `resourceLangId`, `debugState`, `config`, `config.<setting>`, `activeActivity`, `deviceClass`) and string literals in single quotes.
- **Ids:** plugin ids dotted lowercase; contribution ids unique within the plugin and referenced globally as `<pluginId>/<id>` when ambiguous.

## Process plugin protocol `ied-plugin-rpc/1`

For `runtime.kind = "process"`. JSON-RPC 2.0, one JSON object per line on stdin/stdout (stderr goes to the plugin's output source).

```
host -> plugin   initialize { protocol: "ied-plugin-rpc/1", pluginId, pluginDir, workspace?: { id, roots[], trusted }, settings, hostVersion }
plugin -> host   result     { provides: [binding names it implements], subscribes: [event names] }
host -> plugin   binding/call   { name, params, invocationId, deadlineMs?, workspaceId? }   -> result | error
host -> plugin   event          { name, payload }                                          (notification)
host -> plugin   $/cancelRequest { id }
host -> plugin   settings/changed { settings }
host -> plugin   shutdown {} ; exit
plugin -> host   host/<api>     (calls into the host API, permission-checked: fs/read, fs/write, process/spawn, ai/invoke, binding/call, notify, log ...)
```

Integer request ids only (some family tools mishandle string ids). Cancellation is mandatory to honor for calls longer than 1 s. The host restarts a crashed backend with backoff and quarantines it after 5 crashes in 10 minutes.

## JS and UI module API (shape)

JS logic modules (QuickJS-ng, server) and UI modules (browser) both receive an `ied` object; the surfaces differ:

```ts
// server-side JS
export function activate(ied: IedServer): void | Promise<void>;
ied.bindings.provide("myplugin.doThing", async (params, ctx) => result);
ied.bindings.call("editor.openFile", { path, line });
ied.events.on("build.status", payload => ...);
ied.fs.read(path) / write / watch      // permission-checked
ied.process.spawn(argv, opts)          // streams
ied.ai.invoke({ role, messages, tools }) // uses model groups + routing + privacy
ied.settings.get(key, resource?)
ied.log.info(...)

// browser UI module
export function register(ui: IedClient): void;
ui.views.register("myplugin.view", (container, props) => dispose);
ui.commands.register("myplugin.cmd", handler);
ui.server.call("myplugin.doThing", params);  // -> server binding
ui.topics.subscribe("myplugin/thing:42", evt => ...);
ui.kit                                       // shared/ui-kit components (tree, tabs, console, menus, dropdowns)
ui.theme.token("--accent-error")             // tokens only, never raw colors
```

The generated typings (`ied.d.ts`, F-PLG-44) are the authoritative form once the host exists.

## Skill file format

Skills are folders `<name>/SKILL.md` (plus optional resources next to it), found in `.ied/skills/`, the app-tier skills dir, and plugin `skills` paths.

```markdown
---
name: c4-testing                      # unique per source; collisions: project > plugin > app (shown in Settings > Skills)
description: How to write and run [test] functions in C4 with the -test harness.
keywords: [test, assert, fixture, nuke test]        # IED keyword field
triggers: ["write a test", "test is failing"]       # accepted for compatibility with C4's skills (C4/IedPlugin/skills/); indexed like keywords
tools: [build, debug]                 # optional: tool groups enabled when this skill is read
---

# body: markdown, loaded only on skill_read
```

`keywords` and `triggers` are both optional; `name` and `description` are required. Unknown frontmatter keys are kept and ignored.
