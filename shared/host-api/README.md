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
| `examples/` | A game-shaped solution + project (`MyGame.ied.sln`, `Game.ied.proj`), and `markup-plugin`: a project's own language as a plugin with C4 code (`docs/26`) | illustrative; the plugin's code compiles in the server checks | - |

Validate the examples after any schema change. The schemas use a draft 2020-12 subset: the assertions type, required, properties, additionalProperties, items, enum, const, pattern, minLength, maxLength, minimum, maximum, minItems, maxItems, uniqueItems, oneOf, anyOf and local or cross-file `$ref`, with annotations (title, description, default, format, `$defs`, `x-` keys). `tools/pylib/minischema.py` and Bedrock's `Serialization::ValidateJson` implement exactly that dialect (and a few more assertions: exclusiveMinimum, exclusiveMaximum, allOf, not) and reject any other keyword, so a schema cannot rely on one they ignore.

## Conventions shared by all formats

- JSON with `//` comments allowed when read by IED (JSONC); written files preserve comments (F-SET-5).
- Every top-level format has `version` (or `manifestVersion`) as an integer. Readers reject unknown major versions with a visible error.
- Paths are relative to the file that contains them unless stated; `/` separators accepted on Windows.
- **Variables:** `${name}` everywhere (commands, paths, launch configs). Built-ins: `solutionDir`, `projectRoot`, `projectFile`, `project`, `pluginDir`, `config`, `arch`, `system`, `file`, `fileDir`, `fileBasename`, `line`, `column`, `selection`, `startupProject`, `testFilter`, `cwd`, `lspIncludePaths`; plus `env:NAME`, `config:key`, `input:prompt text`, `param.<name>` (templates), `buildSystem.<field>`, and any variable the project or preset declares. Unknown variables are an error at task start (shown, not silently empty).
- **`when` expressions:** `&&`, `||`, `!`, `==`, `!=`, `=~` (regex), parentheses; operands are context keys (`workspaceTrusted`, `resourceExtname`, `resourceLangId`, `debugState`, `config`, `config.<setting>`, `activeActivity`, `deviceClass`) and string literals in single quotes.
- **Ids:** plugin ids dotted lowercase; contribution ids unique within the plugin and referenced globally as `<pluginId>/<id>` when ambiguous.

## Plugin code (C4)

For `runtime.kind = "c4"` (`docs/26` section 2; the `js` and `process` runtimes are gone). The server compiles the plugin's `runtime.sources` (default `code/`) with its script host when the code starts, against the server headers under `src/api/` - the only ones a plugin may include:

```c4
include "api/plugin.ch"

namespace Markup
{
	// toolCalls[].function: IED::ToolResult <Name>(const IED::ToolCall& call)
	IED::ToolResult Outline(const IED::ToolCall& call) { ... IED::ReadFile(call.workspace, path, text, problem) ... }

	// systemMessageSections[].function: Core::String <Name>(const IED::Workspace& ws)
	Core::String Section(const IED::Workspace& ws) { ... }

	// runtime.entry
	void Activate(IED::PluginContext& context)
	{
		context.Provide(Core::StringView("markup.format"), [](const Serialization::DomNode& args) -> IED::Result { ... });
		context.Listen(Core::StringView("tree.changed"), [](const IED::Event& event) { ... });
		context.OnWorkspace([](const IED::Workspace& ws) { ... });   // then Contribute(point, ws.id, entries, problem)
	}
}
```

Calls into a plugin's code run on its own thread, one at a time; `src/api/*.ch` documents each call and the permission it needs (`fs.read`, `fs.write`, `process.spawn`). `shared/host-api/examples/markup-plugin` is a complete one.

## UI module API (shape)

UI modules (browser) receive an `ied` object (`docs/02` "UI modules" has the members as built):

```ts
export function register(ui: IedClient): void;
ui.views.register("myplugin.view", (container, props) => dispose);
ui.commands.register("myplugin.cmd", handler);
ui.bindings.call("myplugin.doThing", params);   // a server binding when no UI module provides it (plugins.call)
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
