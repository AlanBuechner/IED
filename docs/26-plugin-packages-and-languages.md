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

| Point | Change |
|---|---|
| `code` | new: `{entry, sources?: ["code"], activation?}` - the C4 entry class |
| `toolCalls` | implemented by `{function: "Ns::Fn"}` (C4) or `{run: [argv], cwd?, output}` (`text` or `json`) (a command) instead of a binding only **(owner)** |
| `skills` | read: each listed folder of `<name>/SKILL.md` joins the workspace's skill index |
| `systemMessageSections` | read: `{id, when}` with one of `file`, `text` or `function`; `when` can name the workspace's languages (`workspaceLanguage == 'rust'`) |
| `taskPresets` | `extends: "<preset id>"`: a preset inherits another's variables, tasks and toolbar and overrides by key (a language's Nuke preset extends Nuke's own) |
| `outputLinks` | new: a compiler's location shapes in plain output (today hard-coded in the editor's `outputlinks.js`) |
| `markdownFences` | new: fence names that mean the plugin's language (today the editor's `mdpreview.js` map) |
| `languages` | `controlKeywords` for the grammar (today the editor's `monaco.js` list) |

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
| Editing | `headerSourcePairs`, `snippets`, `fileNesting`, `markdownFences` |
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
| `c4` (in every C4 toolchain) | C4 | as now, plus its skills from the plugin and a Nuke preset extending Nuke's | C4 code: `c4_*` AI tools, test provider, project templates |

The editor keeps no language knowledge: the compiler location shapes, Markdown fence names and Monaco control keywords it holds today move into the language plugins (section 3).

## 6. Builds: native first, Nuke opt-in (owner)

**Without a `nuke.toml`** a project builds with its language's own tool, exactly as its developers build it outside IED: the language plugin's task preset runs `cargo`, `go`, `cmake`, `dotnet` or `npm` (detected by `Cargo.toml`, `go.mod`, `CMakeLists.txt`, ...), with the plugin's problem matchers and launch configurations. Nuke is not involved. This is how `ied.rust`'s `rust.cargo` preset works today.

**With a `nuke.toml`** Nuke orchestrates the workspace - C4 projects natively, others through **language drivers** shipped in the language plugin's `nuke/` folder **(owner)**. A non-C4 project names its language:

```toml
[project]
name = "Tools"
language = "rust"            # its driver comes from the package pinned below
depends = ["Engine"]

# the workspace's nuke.toml
[languages]
rust = { package = "toolchains/ied-plugins/ied.rust" }    # a folder with nuke/nuke-language.toml
```

A driver (`nuke/nuke-language.toml` naming a Python module) implements:

| Hook | Does |
|---|---|
| `configure(project, config, arch)` | maps Nuke's configuration to the native one (Debug -> cargo `dev`, Development -> `release` with debug info, Dist -> `release`) |
| `build(ctx)`, `test(ctx)`, `clean(ctx)`, `run(ctx)` | run the native tool with those flags, its output streamed through Nuke |
| `outputs(ctx)` | the final artifacts, which Nuke copies to `build/bin/{config}-{system}-{arc}/{project}/` |

Rules that keep the native toolchain intact:

1. Never write the native manifests or lockfiles (`Cargo.toml`, `Cargo.lock`, `go.mod`, `go.sum`, `package.json`, `CMakeLists.txt`); dependency resolution stays the native tool's.
2. Leave native caches and target folders where the tool puts them (`target/`, the Go build cache, `node_modules/`), so building inside and outside IED shares one cache. A project may opt into `build/int/...` (`nativeTarget = "nuke"`), never by default.
3. Always invoke the native tool and let it decide what is up to date; Nuke's own input hashing is for C4 and script steps only.
4. Nuke owns only what is cross-language: the project graph (`depends` across languages), the configuration names, the staged outputs, and the one command a person or IED runs.

`nuke init --ied` gives each project the preset of its language's plugin (`rust.nuke` extends `nuke`) and pins each language package in the solution's `localPlugins`, instead of today's `c4.nuke` for every project - which wrongly gives the C++ `C4` project a C4 debug launch. Nuke's own presets, matchers, `nuke.toml` icon and skill move out of the C4 plugin into a built-in `ied.nuke` plugin, since IED invokes Nuke for any language it builds (`AGENTS.md` rule 3).

Cross-language linking (a C4 game calling a Rust library through a C header) is the drivers' and C4 interop's business, later.

## 7. Where plugins come from

| Scope | Where | Loads when |
|---|---|---|
| Built-in | `<install>/plugins/<id>/`, code compiled into the server | always (disable-able) |
| App | `%APPDATA%/IED/plugins/<id>/` | the owner approved its content hash and permissions |
| Project | **`.ied/plugins/<id>/`** in the workspace (new: found automatically, in folder workspaces too), and the solution's `localPlugins` paths (a toolchain's plugin) | the workspace is trusted |

A project that invents its own markup language commits `.ied/plugins/mymarkup/` (grammar, icons, a C4 formatter as a tool call, a Nuke driver that renders it); whoever opens the trusted workspace gets it, compiled on load and reloaded as it is edited. Precedence is unchanged: project > app > built-in, so a repo pins its version of any plugin.

## 8. The AI side

- **Skills**: the index merges `.ied/skills/`, app skills and every loaded plugin's `skills` folders; `skill_search` and `skill_read` reach them (`docs/04`). `nuke init` stops copying the C4 and Nuke skills into `.ied/skills/`: they come from the `c4` and `ied.nuke` plugins.
- **System message**: plugin sections are item 5 of `docs/14`'s order, with its caps; a language plugin's section appears only when the workspace uses the language (its `when`), so a C4 workspace does not pay for Rust advice.
- **Tool calls**: named `<prefix>_<tool>` in the plugin's group, enabled per mode like the built-in groups (`docs/13`). A C4-function tool runs in the server under the plugin's permissions; a command tool runs through the same runner, approvals and output limits as `shell_run`.

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
