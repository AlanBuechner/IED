# 26 - Plugin packages and language plugins

## Purpose

Owner direction (2026-10-21): every feature of IED is a plugin - the chat, the text editor, the profiler and every language alike - and a plugin is a **package** that carries whatever the feature needs: C4 code for its server logic (compiled and loaded by IED itself through C4's scripting), configuration, Python for the build system, JavaScript for its browser UI, skills and system-prompt text for the AI, and AI tool calls that are either C4 functions or commands. The host knows concepts (a language, a build system, a tool call, an icon), never a particular language, so a new language - even one a single project invents for itself - is a package dropped into place, not a change to IED.

This doc is the design; it supersedes the four-runtime model of `docs/02` (whose sections now point here) and resolves `docs/99` #6 again. Decisions taken by the owner are marked **(owner)**.

## 1. A plugin is a package

```
<plugin>/
  ied-plugin.json            identity, dependencies, permissions, contributions (data)
  code/                      C4 sources: the plugin's server logic, compiled by IED's script host
    plugin.cf                  a class implementing IED::Plugin (section 2)
  ui/                        ES modules run in the browser: views, client bindings
  nuke/                      the plugin's Nuke language driver: nuke-language.toml + Python (section 6)
  skills/<name>/SKILL.md     AI skills
  prompts/<section>.md       system-message sections
  grammars/ icons/ schemas/  data the contributions name
```

Every part is optional; a plugin is the folder with `ied-plugin.json`. Each consumer reads only its own part: the IED server the manifest, `code/`, `skills/` and `prompts/`; the browser `ui/`; Nuke `nuke/` (it never parses the IED manifest, and IED never runs Nuke's Python).

| Part | Language | Runs | Replaces |
|---|---|---|---|
| Server logic | C4 (`code/`) | in `ied-server`, compiled by C4's script host (`C4/docs/runtime/04-scripting.md`) **(owner)** | the planned `js` (QuickJS-ng) and `process` (`ied-plugin-rpc/1`) runtimes, both dropped |
| Configuration | JSON(C), TOML, Monarch, SVG | read by the host | - (declarative plugins stay: a package with no `code/` is one) |
| Build | Python (`nuke/`) | in Nuke, only in a workspace with a `nuke.toml` **(owner)** | Nuke's C4-only project kinds |
| UI | JavaScript (`ui/`) | in the browser | - (UI modules as built, `docs/02`) |
| AI | Markdown + C4 functions or commands | the AI runtime | the never-consumed `skills`, `systemMessageSections`, `toolCalls` points |

A plugin that needs a long-running service in another language starts it from its C4 code (`Process::Child`) or names it as a command: no separate process runtime is needed. LSP servers and debug adapters stay processes the generic pools run (`docs/24`, `docs/25`).

## 2. Server logic: C4 code in the script host

`ied-server` is built with `--script-host`, so it ships `ied-server.c4i` (its interface) and `C4Script.dll` beside it. A plugin's `code/` is a C4 script: compiled against the server's interface when the plugin loads, sharing the server's GC, types, reflection, threads and exceptions; hot-reloaded when its files change (`Host::Reload`); a hardware fault in it stops that call and marks the plugin `failed` instead of taking the server down.

**The plugin API** is a set of server headers under `src/api/` - the script allow-list, so a plugin includes nothing else of the server (Bedrock is always allowed):

| Header | Gives |
|---|---|
| `api/plugin.ch` | `IED::Plugin` (`Activate(PluginContext&)`, `Deactivate()`), the plugin's identity, settings, log and output source |
| `api/bindings.ch` | provide and call server bindings by name, with JSON arguments and results (section 4) |
| `api/tools.ch` | register AI tool calls implemented by C4 functions |
| `api/contributions.ch` | add or replace contributions at run time (a computed `projectIcons` or `taskPresets`), in addition to the manifest's static ones |
| `api/workspace.ch` | the workspace: files (permission-checked), documents, solution and projects, trust, events (file changed, document saved, task finished) |
| `api/prompts.ch` | system-message sections computed per workspace |
| `api/process.ch` | run commands and children under the workspace's rules (sandbox, environment, output source) |

The plugin's class is found by reflection: the manifest names it (`"code": {"entry": "Rust::RustPlugin"}`), the host checks it implements `IED::Plugin`, constructs it and calls `Activate` on its activation events (`onLanguage:rust`, `onWorkspaceContains:Cargo.toml`, `onCommand:...`, `onTool:...`), or at load when it lists none.

**Built-in plugins** (chat, editor, git, terminal, profiler, sandbox) use the same API, but their `code/` is compiled into the server rather than JIT-compiled at start: no compile on every start, and they stay debuggable (script code has no PDB yet, `C4/docs/runtime/04` section 8). The same sources can also load as a script for a hot-reload loop while developing them. Today's host modules (`src/ai`, `src/git`, ...) move behind the API one at a time (section 9).

**Trust, honestly.** Plugin code runs in the server process, so its permissions are declared, shown before approval and enforced at the API, but they are not a sandbox: a plugin can reach Bedrock directly. The boundary is the trust decision - built-ins ship with IED, an app plugin loads only after the owner approves its content hash (as now), a project plugin only in a trusted workspace (`docs/11`). A plugin that faults is isolated per call; one that hangs is a known limit (no watchdog inside the process).

## 3. Contributions: data first, code when it must

Every contribution point keeps its static shape in the manifest; most of a language plugin is still data (its grammar, icons, LSP profile). Code adds what data cannot: a project detector that reads `go.mod`, task presets computed from `package.json` scripts, prompt text that names the workspace's crates. Code contributions go through `api/contributions.ch` and reach clients through the same `plugins.contributions` answer, marked with the plugin that made them.

New and changed points (schema `plugin-manifest.v1`):

| Point | Change | Built |
|---|---|---|
| `code` | new: `{entry, sources?: ["code"], activation?}` - the C4 entry class | P3 |
| `toolCalls` | `{name, description, class, inputSchema, when}` and one of `run` (a command: argv with `${args.<name>}` - a list argument as a whole word is a word per item - and `${config:<key>}`; `cwd` relative to the root; `output` `text`, the exit code and the output's tail, or `json`, all of stdout; `timeoutSeconds`), `function` (a C4 function, P3) or `binding` **(owner)** | `run` |
| `skills` | read: each listed folder (inside the plugin) of `<name>/SKILL.md` joins the workspace's skills | yes |
| `systemMessageSections` | read: `{id, when, maxTokens}` with one of `text` or `file` (`function`, P3); `when` can name the workspace's languages (`workspaceLanguage.rust`) or a file in its root (`workspaceContains.nuke.toml`) | yes |
| `taskPresets` | `extends: "<preset id>"`: the base is read first (its base before it; a preset that extends itself is a problem) and this one overrides by key; `problemMatcher`: the matchers of every run step that names none, inherited steps included (`c4.nuke` gives Nuke's steps C4's) | yes |
| `outputLinks` | new: `{id, regexp, file, line, column?}` - a tool's location shapes in plain output, which the Output window underlines (before, the editor's own list) | yes |
| `languages` | `fences`: Markdown fence names that mean the language (before, the preview's own map); `controlKeywords`: keywords the grammar names one by one that read as control flow (before, the editor's list) | yes |
| `companionTabs` | new: `{kind, languages}` - a tab that opens beside a document of those languages opened from outside IED (the Markdown preview, J10; before, the editor knew Markdown) | yes |

## 4. Composition: plugins for plugins

Plugins work together only through **bindings** and **service interfaces**, never through each other's files:

- A **binding** is a named function or event with a JSON schema (`docs/02` "Bindings"). It is provided by a plugin's C4 code (server side), by its UI module (client side), or by a command. Any plugin calls it if its manifest consumes it. A binding marked `aiExposed` is also an AI tool, and any binding can be a task step or a command handler (the M11 "server bindings" item, now built on C4 code).
- A **service interface** is an `IED::` interface declared in `src/api/` (`IED::SourceControl`, `IED::BuildSystem`, `IED::TestProvider`, ...): a plugin implements it in C4 and registers the object; a consumer asks the host for the implementations. This is how the chat's tools reach git, the profiler hears about task runs, and an engine plugin adds tools to the chat without either knowing the other.

A plugin depending on another names it in `dependencies` (semver ranges, cycles refused - as now); it may then consume the other's bindings and services but still not include its sources (scripts compile separately).

## 5. Language plugins

A language plugin owns everything about its language. The host and the default feature plugins (editor, chat, git) know none:

| Concern | Contribution |
|---|---|
| Recognition, comments, brackets, grammar, control keywords | `languages` |
| Colors | `semanticTokenColors` |
| Icons | `fileIcons`, `projectIcons` (`docs/15` Icons) |
| Editing | `headerSourcePairs`, `snippets`, `fileNesting`, `languages.fences`, `companionTabs` |
| Language server, debugger | `lspProfiles`, `dapProviders` |
| Build without Nuke | `taskPresets` that run the language's own tool, detected by marker files |
| Build with Nuke | the `nuke/` driver plus a `taskPresets` entry extending Nuke's |
| Diagnostics from builds | `problemMatchers`, `outputLinks` |
| Tests | `testProviders` |
| New projects | `projectTemplates` |
| AI | `skills`, `systemMessageSections` (shown when the workspace uses the language), `toolCalls` (`cargo_metadata`, `go_list`, `c4_explain_diagnostic`) |
| Settings | `settings` (the tool paths, server options) |

**The split (owner: one plugin per language).** Today's `ied.languages` grab-bag and the C/C++ and Rust plugins become:

| Plugin | Languages | Brings now | Brings later |
|---|---|---|---|
| `ied.cpp` | C, C++ | clangd, CodeLLDB, icons, matchers; CMake/meson markers | CMake and MSBuild presets, a CMake Nuke driver |
| `ied.rust` | Rust | rust-analyzer, cargo preset, icons | cargo Nuke driver, `cargo_metadata` tool |
| `ied.go` | Go | icons, markers | gopls, go preset, go Nuke driver |
| `ied.python` | Python | icons, markers | a language server, venv-aware run/test presets |
| `ied.web` | JavaScript, TypeScript, HTML, CSS, SCSS | icons, markers | a TS server, npm-script presets |
| `ied.csharp` | C# | icons, markers | `dotnet` presets |
| `ied.java` | Java | icons, markers | Gradle/Maven presets |
| `ied.shaders` | HLSL, GLSL, WGSL | icons | shader language servers (engine plugins may supersede) |
| `ied.data` | JSON, YAML, TOML, XML, INI | icons, the `.ied.sln/.ied.proj` icons | schema validation |
| `ied.markdown` | Markdown | icons, the Markdown preview (moved out of the editor) | |
| `ied.scripts` | Shell, PowerShell, Batch | icons | |
| `ied.lua`, `ied.sql` | Lua, SQL | icons | |
| `ied.filetypes` | (no language) | media, archive, binary, license icons; plain text | |
| `c4` (in every C4 toolchain) | C4 | as now, plus its skills (`C4/IedPlugin/skills/`, shipped as `<toolchain>/ied/skills/`), a system-message section and `c4.nuke` extending Nuke's preset | C4 code: `c4_*` AI tools, test provider, project templates |
| `ied.nuke` (built in) | (no language: the build system) | the `nuke` preset, `nuke.command`, the `nuke.toml` icon, the Nuke skill, a section and the `nuke_list` tool | |

The editor keeps no language knowledge: the compiler location shapes, Markdown fence names and Monaco control keywords it held moved into the language plugins (section 3), and its Markdown preview into `ied.markdown`, which colors fenced code through the editor's `editor.colorize` binding (now `{languageId | path, lines}`).

## 6. Builds: native first, Nuke opt-in (owner)

**Without a `nuke.toml`** a project builds with its language's own tool, exactly as its developers build it outside IED: the language plugin's task preset runs `cargo`, `go`, `cmake`, `dotnet` or `npm` (detected by `Cargo.toml`, `go.mod`, `CMakeLists.txt`, ...), with the plugin's problem matchers and launch configurations. Nuke is not involved. This is how `ied.rust`'s `rust.cargo` preset works today.

**With a `nuke.toml`** Nuke orchestrates the workspace - C4 projects natively, others through **language drivers** shipped in the language plugin's `nuke/` folder **(owner)**. A non-C4 project names its language:

```toml
[project]
name = "Tools"
language = "rust"            # executable (the default) or library; built by the rust driver
depends = ["Engine"]

# the workspace's nuke.toml: only for a package IED does not ship
[languages]
dialogue = "tools/dialogue"  # a folder with nuke/nuke-language.toml
```

A driver (`nuke/nuke-language.toml` naming a Python module and the IED preset of its projects) implements `build(ctx)` -> the artifacts Nuke copies to `build/bin/{config}-{system}-{arc}/{project}/`, `program(ctx)` -> the file `nuke run` starts, `test(ctx)` -> an exit code, and optionally `clean(ctx)` and `has_tests(ctx)`; it maps Nuke's configuration onto the native one itself (cargo: no optimization is the dev profile, otherwise `--release`, with debug info through `CARGO_PROFILE_RELEASE_DEBUG`; go: `-gcflags=all=-N -l` and `-ldflags=-s -w`). As built: `Nuke/nuke/languages.py`, `plugins/rust/nuke/cargo.py`, `plugins/go/nuke/gotool.py`, the presets `rust.nuke` and `go.nuke` (and Go's native `go` preset and gopls profile), a two-line `rustc-human` matcher for cargo's own diagnostics, and `${projectOutput}` from the project file's `buildSystem.output`, which `nuke init --ied` writes. A C4 + Rust + Go workspace builds, runs and tests in one `nuke` command (`Nuke/tests/test_languages.py` with the real cargo and go), and in IED a Rust error from the Nuke build lands in the Error List.

Rules that keep the native toolchain intact:

1. Never write the native manifests or lockfiles (`Cargo.toml`, `Cargo.lock`, `go.mod`, `go.sum`, `package.json`, `CMakeLists.txt`); dependency resolution stays the native tool's.
2. Leave native caches and target folders where the tool puts them (`target/`, the Go build cache, `node_modules/`), so building inside and outside IED shares one cache. A project may opt into `build/int/...` (`nativeTarget = "nuke"`), never by default.
3. Always invoke the native tool and let it decide what is up to date; Nuke's own input hashing is for C4 and script steps only.
4. Nuke owns only what is cross-language: the project graph (`depends` across languages), the configuration names, the staged outputs, and the one command a person or IED runs.

`nuke init --ied` names `<language>.nuke` in each project's file - the preset its language's plugin offers by that convention, extending `nuke` (`c4.nuke`, `cpp.nuke`, later `rust.nuke`) - and `nuke` for a project without a language; it used to give every project `c4.nuke`, which handed the C++ `C4` project a C4 debug launch. Nuke's own preset, the `nuke.command` setting, the `nuke.toml` icon and the Nuke skill are the built-in `ied.nuke` plugin's, since IED invokes Nuke for any language it builds (`AGENTS.md` rule 3). With P2, `nuke init --ied` also pins each language package the workspace's `[languages]` names in the solution's `localPlugins`.

Cross-language linking (a C4 game calling a Rust library through a C header) is the drivers' and C4 interop's business, later.

## 7. Where plugins come from

| Scope | Where | Loads when |
|---|---|---|
| Built-in | `<install>/plugins/<id>/`, code compiled into the server | always (disable-able) |
| App | `%APPDATA%/IED/plugins/<id>/` | the owner approved its content hash and permissions |
| Project | **`.ied/plugins/<id>/`** in the workspace (new: found automatically, in folder workspaces too), and the solution's `localPlugins` paths (a toolchain's plugin) | the workspace is trusted |

A project that invents its own markup language commits `.ied/plugins/mymarkup/` (grammar, icons, a C4 formatter as a tool call, a Nuke driver that renders it); whoever opens the trusted workspace gets it, compiled on load and reloaded as it is edited. Precedence is unchanged: project > app > built-in, so a repo pins its version of any plugin; of two project plugins with one id, the solution's pin wins over the `.ied/plugins/` folder (the other is `overridden`).

As built (P1): the server reads `.ied/plugins/*/ied-plugin.json` with the solution's pins on every request for a workspace's plugins (`src/plugins/host.cf`), so an edit is live; a change under `.ied/plugins/` rebuilds the workspace's settings catalog and task offers and sends `plugins.changed` on `ws:<wsId>`, and the clients read the contributions again. The mock server has no project plugins (`tools/mock-server/README.md`).

## 8. The AI side

- **Skills**: a workspace's skills are its `.ied/skills/`, then every loaded plugin's `skills` folders, then the app's `skills/`; of a name the first wins (project > plugin > app). The system message lists them (`docs/14` item 8: every skill up to 40, past that the ones the chat's first message names best and how many more there are); `skill_search` finds them by words and `skill_read` gives a skill's body or another file of its folder (`docs/13`). `nuke init` no longer copies the C4 and Nuke skills into `.ied/skills/`: they come from the `c4` and `ied.nuke` plugins. As built: `src/ai/skills.cf`, `src/ai/contributed.cf`, `src/ai/tools/skilltools.cf`.
- **System message**: plugin sections are item 5 of `docs/14`'s order - under "From the workspace's plugins", a heading per plugin, each section within its `maxTokens` (200 by default, at most 600) and all within 1,500 tokens - and only those whose `when` holds in the workspace's context (`src/plugins/context.cf`: `workspaceTrusted`, `workspaceKind`, `config.<key>`, `workspaceContains.<name>`, `workspaceLanguage.<id>` - a project naming the language, or a `projectIcons` marker of it in the root or a project's root), so a C4 workspace does not pay for Rust advice. Language servers and debug adapters use the same context.
- **Tool calls**: each turn offers the core tools and the workspace's plugins' tool calls whose `when` holds; a name already taken is a problem in the server log. A command tool (`src/ai/tools/plugintools.cf`) has `shell(<the command as run>)` as its subject, so the `ai.permissions` rules and approvals of commands apply; it never runs in an untrusted workspace; a `read` one is offered in Plan mode. Shipped: `nuke_list` (`ied.nuke`), `cargo_metadata` (`ied.rust`). A C4-function tool (P3) will run in the server under the plugin's permissions.

## 9. Order of work

| Phase | Delivers | Needs |
|---|---|---|
| P1 | The split into one plugin per language; `outputLinks`, `markdownFences`, `controlKeywords` moved out of the editor; `.ied/plugins/` discovery; plugin `skills` and `systemMessageSections` read by the AI (with `skill_search` / `skill_read`); command tool calls; built-in `ied.nuke` with preset `extends`; `nuke init --ied` giving each project its language's preset | nothing new in C4 |
| P2 | Nuke language drivers: the interface, the cargo and go drivers, a mixed C4 + Rust workspace end to end | P1 |
| P3 | The script host in `ied-server`: `--script-host`, `src/api/`, compiling `code/` on load with a cache, hot reload, server bindings, service interfaces, C4-function tool calls, computed contributions and prompt sections | C4: a compiled-script cache in `C4Script` (no JIT compile on every start) - `docs/17` |
| P4 | Built-ins behind the API, smallest first: git, terminal, the editor's services, profiler, chat | P3 |

## Open items

- Script debugging (no PDB for JIT code) - C4's "later"; until then a plugin is debugged by loading its code as a built-in.
- A hang in plugin code (no watchdog inside the process).
- Permissions enforced at the API are a courtesy for in-process code; real isolation of third-party code would need a process again (`docs/99` "Later").
