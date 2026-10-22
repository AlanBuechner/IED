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

`ied-server` is built with `--script-host` (`compiler-args` in `src/nuke.toml`), so it ships `ied-server.c4i` (its interface) and `C4Script.dll` beside it - all three files travel together; an exe without the DLL starts, and plugin code fails with `Host::Open`'s reason (the DLL is delay-loaded since C4 a1fd894). A plugin whose `runtime.kind` is `c4` has C4 code: every `.cf` file under its `runtime.sources` (default `["code"]`, inside its folder) is compiled as one script against the server's interface when the plugin's code starts, sharing the server's GC, types, reflection, threads and exceptions (`C4/docs/runtime/04-scripting.md`). As built (P3): `src/plugins/code.cf` (the running copies), `src/plugins/codecalls.cf` (bindings, sections, events), `src/plugins/codediagnostics.cf`, `plugins/chat/code/codetools.cf`, and the API in `src/api/`.

**The plugin API** is the server headers under `src/api/` - the script allow-list (`Allow("api/")`), so a plugin includes nothing else of the server (Bedrock is always allowed; an include of another server header is the compile error "not on the allow-list"). Its namespace is `IED`; the server's own is `Ied`.

| Header | Gives |
|---|---|
| `api/plugin.ch` | the entry's contract and `IED::PluginContext`: `Id()`, `Folder()`, `WorkspaceId()` (a project plugin's), `StateFolder()` (`<data>/plugins/<id>`: machine-local state that is no workspace's, such as the browser plugin's Edge profile), `Log(line)`, `Setting(key, wsId)` (the effective value as JSON), `Provide(binding, handler)`, `Listen(eventKind, handler)`, `OnWorkspace(handler)`, `OnDeactivate(handler)`, `Contribute(point, wsId, entries)` |
| `api/workspace.ch` | `IED::Workspace` (id, name, root, kind, trusted, languages, projects with their folder, kind and language), `FindWorkspace`, `Workspaces()` (those the plugin is loaded for), `OpenFolder` (a folder opened as a workspace, `workspace.open`; developer mode's source, `docs/28`), `ReadFile` / `WriteFile` through the server (`fs.read` / `fs.write`) |
| `api/bindings.ch` | `IED::Result` (`Answer(json)` / `Refusal(error)`), `IED::Binding`, `CallBinding(name, args)` (a binding its manifest consumes), `IED::Event` (`topic`, `kind`, `data`) |
| `api/tools.ch` | `IED::ToolCall` (`name`, `args`, `workspace`, `chatId`, `Cancelled()`), `IED::ToolResult` (`ToolText`, `ToolFailure`; `images`: `IED::ToolImage {mime, path}`, files in the call's workspace the result shows the model, `docs/13` "Images in tool results") |
| `api/process.ch` | `RunCommand(ws, argv, cwd, seconds)`: in a trusted workspace, under `process.spawn`, output kept (1 MB) |

**Entry and functions.** `runtime.entry` names a free function, `void <Name>(IED::PluginContext& context)` (`"Go::Activate"`), that registers what the plugin provides; a manifest's `function` entries name free functions of the code, checked against the signature their use expects before they are called (a mismatch is a problem naming what the function returns and takes):

| Manifest entry | Signature |
|---|---|
| `toolCalls[].function` | `IED::ToolResult <Name>(const IED::ToolCall& call)` |
| `systemMessageSections[].function` | `Core::String <Name>(const IED::Workspace& ws)` - the section's text, empty for none |

**Editing plugin code.** A language server profile with `pluginCodeOption` (a key) gets, under that key of its `initializationOptions`, the workspace's plugin code scopes: one per c4 plugin loaded for it, `{folders: [its source folders], interface: "<the server's .c4i>", allow: ["api/"]}` - the shape C4LSP's `scripts` option takes, so the `c4` plugin's profile names `"pluginCodeOption": "scripts"` and plugin code is checked as the server compiles it (C4LSP: `C4/docs/tools/03` section 1). The scopes are those of the server's start: a plugin added later needs the language server restarted.

**Running copies.** A built-in or app plugin's code runs once; a project plugin's once per workspace that loads it (only in a trusted workspace: trusting it starts the code, untrusting stops it). Each copy has its own thread: its compile, its entry, its bindings, tool calls, sections and event handlers all run there, one at a time and never holding the server lock, so the script is never unloaded under a call and plugin code needs no locking of its own. Callers queue a job and wait with the server lock released (a binding or a section 30 s, a tool its `timeoutSeconds` or 120 s, a first call also the compile); a call from a plugin's code into its own binding runs at once. The `PluginContext` methods that register work only on the plugin's thread.

**When code starts.** On its first activation event (`activation`): none listed or `onStartup` - when the plugin loads; `onWorkspaceOpen`, `onWorkspaceContains:<name>`, `onLanguage:<id>` - when a workspace it is loaded for opens with that (the `workspaceContains.` / `workspaceLanguage.` context of section 8); and a call into it (a tool, a binding, a section) starts it whatever it lists. `OnWorkspace` handlers run for the workspaces open when they register, then for each that opens.

**Hot reload and failures.** The code compiles again when its files change - a project plugin's as they are edited (the workspace watcher), any plugin's on `plugins.reload` - and the new build replaces the running one (its `OnDeactivate` handlers run, everything it registered goes, its script unloads). A new build that does not compile leaves the running one in place, with the diagnostics as its problem ("the new build did not compile, the previous one runs"). A hardware fault in a call (`exception 0xc0000005 at script+0x24b, writing 0x10`) stops that call and marks the code `failed` until it builds again (an edit, or `plugins.reload`, which also retries failed code); an exception it throws and does not catch fails only that call. Compiled code is kept in the server's data folder (`<data>/script-cache`, C4's compiled-script cache): a start whose plugin code did not change links the kept build in milliseconds instead of compiling it. Each build's script is named `<plugin id>[@<wsId>]#<build>` - its reflection tag, unique among the loaded scripts (a new build loads before the running one retires, and a project plugin runs once per workspace). The code's state is `plugins.list`'s `code` field (`idle`, `compiling`, `active`, `failed`, with its problem, build count and `cached` when the running build was linked from the cache), shown on the plugin's card in the Plugins view, and a project plugin's compile errors are in its workspace's Error List (source `plugin <id>`, on the file and line).

**Built-in plugins** (chat, editor, git, terminal, profiler, sandbox) use the same API, but their code is compiled into the server rather than JIT-compiled at start: no compile on every start, and they stay debuggable (script code has no PDB yet, `C4/docs/runtime/04` section 8). The host modules that were features moved behind the API ("Built-ins on the API" below, P4): git, the terminal and the chat (`plugins/git/code/`, `plugins/terminal/code/`, `plugins/chat/code/`). The language plugins shipped in `plugins/` are ordinary c4 plugins when they have code: `ied.go` computes its system-message section from the workspace's `go.mod` files (`plugins/go/code/go.cf`, started `onLanguage:go`).

**Cost.** A compile takes 4-5 s against the server's 6.4 MB interface (several plugins compile at once, each on its own thread); the compiled-script cache and a profile of script compile time are on the C4 session's list (`docs/17`).

**Trust, honestly.** Plugin code runs in the server process, so its permissions are declared, shown before approval and enforced at the API, but they are not a sandbox: a plugin can reach Bedrock directly. The boundary is the trust decision - built-ins ship with IED, an app plugin loads only after the owner approves its content hash (as now), a project plugin only in a trusted workspace (`docs/11`). A plugin that faults is isolated per call; one that hangs keeps its own thread busy and its callers get "no answer within N s", but nothing stops the hung call (no watchdog inside the process).

### Built-ins on the API (P4)

A built-in plugin's server logic moves from `src/<module>/` to its plugin folder's `code/` and is written against `src/api/` alone, like any plugin's; what changes is how it is loaded.

**Compiled in.** Each built-in's `code/` folder is a source folder of the `ied-server` project (`sources` in `src/nuke.toml`), so its code is part of the server: no compile at start, a PDB, the same API. Its manifest says `runtime: {kind: "builtin", entry: "<Name>"}`; the host finds the entry by reflection in its own image (`Reflection::FindFunction`, checked against `void <Name>(IED::PluginContext&)`) and runs it on the plugin's thread like a script's, and finds its `function` entries the same way. A built-in never reloads (its code is the server's); disabling it runs its `OnDeactivate` handlers and drops what it registered. Its `code/` cannot also load as a script (a script may not define what its host defines): a built-in is developed by rebuilding the server. Entries, tool and section functions are called through `Reflection::Function::Invoke` for scripts and built-ins alike (a script's inside its guard).

**The API's second half** - what git and the terminal need, kept generic:

| Header | Gives |
|---|---|
| `api/methods.ch`, `api/protocol.ch` | `PluginContext::Method(name, handler)`: a protocol method (`docs/10`), `Core::String handler(const IED::Request&)` - the method, its `IED::Params`, the calling client, `Cancelled()` - answering JSON or throwing `IED::RpcError` with a stable code. A third-party plugin names its methods `<pluginId>/<method>`; a built-in keeps its protocol names (`git.*`, `term.*`); a name already registered is refused. Methods run on their requests' threads, several at once (a plugin with state guards it with a lock of its own), the server lock released; a script's code unloads only once its running methods have returned. `IED::JsonWriter`, `IED::Params`, `IED::RpcError`, `IED::ErrorCode` are the server's own |
| `api/topics.ch` | `PluginContext::Topic(key, snapshot)` (pushed: the text is the topic's snapshot until the next call), `TopicFrom(key, snapshot)` (pulled: a function the hub calls when a client subscribes - with the server lock held, so it may take a lock of the plugin's only if the plugin never calls the API while holding that lock), `Publish(key, kind, data)`, `RemoveTopic(key)`. A third-party plugin's topics are `<pluginId>/...`; a built-in keeps its own (`git:`, `gitrepos:`, `term:`, `terms:`) |
| `api/outputs.ch` | `PluginContext::Output(wsId, source, text)`: text for an output source (the Output window) the manifest's `outputSources` declares |
| `api/plugin.ch` (more) | `PluginContext::Atomically(change)`: runs a quick change of the plugin's state with the server lock held, its events published through `IED::Events` inside it, so a pulled snapshot reads all of a change or none (the terminal's scrollback and its `data` event) |
| `api/text.ch` | `BoundedText(tail, text, limit)`, `WholeCharacters(bytes)`: the server's own helpers for output kept by the byte |
| `api/plugin.ch` (more) | `IED::HostLock` (holds the server lock for a scope; the API's calls work inside it without taking it again, `Wait` releases it); `PluginContext::StartThread(body)` (a thread on which the API's free functions know the plugin) |
| `api/documents.ch`, `api/tasks.ch`, `api/contributions.ch` | for the chat: `TextAsEdited`, `Exists`, `WriteThrough`, `Search`; `RunTask` (and its outcome: label, time, tail, problems), `Problems`; `Contributions(wsId, point)`, `PluginSection`, `RunPluginTool`, `WhenHolds`, `AppFolder`, `HomeFolder`; and `GlobMatch`, `ExpandGlob`, `NodeJson`, `IED::Merge` with `PublishMergeable` |
| `api/tasks.ch` (for the profiler) | `SetTaskVariable(wsId, name, value)`: a variable a plugin offers the workspace's tasks (`${profiler.connect}`, `docs/06`); `StartTask` (a run started without waiting: its id), `TaskEnded(runId, outcome)`, `CancelTask(runId)` - how `profile_capture` runs the Profile task, records and stops the program |
| `api/workspace.ch` (more) | `OnTrustChanged(handler)`; `OnFilesChanged(handler)` - a workspace's file changes as its watcher batches them, `.git` and other hidden folders included (`IED::FileChanges`: `wsId`, `changes` of `{path, change}`, `overflowed`); `WatchFolder(folder, handler)` for a folder outside every workspace (a repository's refs above it); `MoveToTrash(wsId, path)` (the recoverable trash, `docs/11`; `fs.write`); `RelativeTo(root, path, relative)` and `DefaultExcludes()` (the folders search leaves out) |

**Threads.** Registration (`Provide`, `Method`, `Listen`, the `On...` handlers) stays on the plugin's thread. `Log`, `Setting`, `Topic`, `TopicFrom`, `Publish`, `RemoveTopic` and `Output` may be called from any thread the plugin started, and from its methods. A script that starts threads stops them in `OnDeactivate`. Lock order: a plugin's own lock, then the server's (the API takes it) - never the other way round.

**What stays the host's.** The host keeps the concepts every plugin builds on - workspaces and their files, documents (the open buffers with their leases and revisions), search over a workspace, diagnostics, output sources, tasks and their runs, language servers and debug adapters (generic pools fed by plugins' profiles), settings, trust, the hub and the methods - and offers them through `src/api/`. A feature's own logic moves into its plugin. So "the editor's services" in this order means what the editor gives other plugins (documents, search, diagnostics) offered through the API - the chat needs them - not moved into `plugins/editor`. The chat's API (`api/documents.ch`, `api/tasks.ch`, `api/diagnostics.ch`, `api/contributions.ch`: documents read as edited, edits through the document's lease, saves; a task run and its end; a workspace's problems; other plugins' contributions and their code tools; a plugin's own data folder) comes with its move.

**Order.** Git first - **done**: `src/git` is `plugins/git/code` (`Git::Activate`), its UI module unchanged: the same `git.*` methods and `git:` / `gitrepos:` topics, the Git output source declared by its manifest), then the terminal - **done**: `plugins/terminal/code` (`Terminal::Activate`; `term.*`, `term:` / `terms:` unchanged, its state changed only inside `Atomically` so the pulled snapshots read it whole) - then the editor's services through the API (`api/documents.ch`: text as edited, write-through, search), the profiler (its server side does not exist yet: it is written as a plugin from the start, `docs/16`), and the chat - **done**: `src/ai` is `plugins/chat/code` (`Ai::Activate`, flattened into one folder so its includes stay beside each other; its prompts in `plugins/chat/prompts/`), the `chat.*` methods and the `chats:` / `chat:` topics unchanged. Its methods run inside `IED::HostLock` - the server lock, as the server's own methods ran - so its pulled snapshots read the chats whole; its turns run on threads it starts with `PluginContext::StartThread`, on which the API's free functions know it. Each move keeps the protocol and the checks of its module (`tools/server-checks`, e2e) passing unchanged - the proof that the API carries a real feature.

## 3. Contributions: data first, code when it must

Every contribution point keeps its static shape in the manifest; most of a language plugin is still data (its grammar, icons, LSP profile). Code adds what data cannot: a project detector that reads `go.mod`, task presets computed from `package.json` scripts, prompt text that names the workspace's modules. As built: `PluginContext::Contribute(point, wsId, entries)` gives a JSON array of entries of an array point for one workspace (`""`: every workspace it is loaded in), replacing what it gave before (`"[]"` removes them); the object points (`menus`, `fileOpener`, `fileNesting`, `settings`, `bindings`) come from the manifest only. Computed entries follow the manifest's in the plugin's `plugins.contributions` answer, and every place the server reads a point - task presets and problem matchers, project icons for `workspaceLanguage`, language server profiles, debug adapters, tool calls, system-message sections - reads them too (`PluginHost::Entries`). A change re-reads the workspace's settings catalog and task offers and sends `plugins.changed`. Computed entries go when the code that gave them stops or is replaced.

New and changed points (schema `plugin-manifest.v1`):

| Point | Change | Built |
|---|---|---|
| `runtime` | kind `c4` (`js` and `process` are gone) with `entry` (the entry function) and `sources` (default `["code"]`); `activation` as before (section 2) | yes |
| `toolCalls` | `{name, description, class, inputSchema, when}` and one of `run` (a command: argv with `${args.<name>}` - a list argument as a whole word is a word per item - and `${config:<key>}`; `cwd` relative to the root; `output` `text`, the exit code and the output's tail, or `json`, all of stdout; `timeoutSeconds`), `function` (a free function of the plugin's code, section 2) or `binding` (a server binding; its JSON result is the tool's output) **(owner)** | yes |
| `skills` | read: each listed folder (inside the plugin) of `<name>/SKILL.md` joins the workspace's skills | yes |
| `systemMessageSections` | read: `{id, when, maxTokens}` with one of `text`, `file` or `function` (computed per workspace by the plugin's code); `when` can name the workspace's languages (`workspaceLanguage.rust`) or a file in its root (`workspaceContains.nuke.toml`) | yes |
| `taskPresets` | `extends: "<preset id>"`: the base is read first (its base before it; a preset that extends itself is a problem) and this one overrides by key; `problemMatcher`: the matchers of every run step that names none, inherited steps included (`c4.nuke` gives Nuke's steps C4's) | yes |
| `outputLinks` | new: `{id, regexp, file, line, column?}` - a tool's location shapes in plain output, which the Output window underlines (before, the editor's own list) | yes |
| `languages` | `fences`: Markdown fence names that mean the language (before, the preview's own map); `controlKeywords`: keywords the grammar names one by one that read as control flow (before, the editor's list) | yes |
| `companionTabs` | new: `{kind, languages}` - a tab that opens beside a document of those languages opened from outside IED (the Markdown preview, J10; before, the editor knew Markdown) | yes |

## 4. Composition: plugins for plugins

Plugins work together only through **bindings** and **service interfaces**, never through each other's files:

- A **binding** is a named function or event with a JSON schema (`docs/02` "Bindings"). It is provided by a plugin's C4 code (server side), by its UI module (client side), or by a command. Any plugin calls it if its manifest consumes it. A binding marked `aiExposed` is also an AI tool, and any binding can be a task step or a command handler (the M11 "server bindings" item, now built on C4 code).
- A **service interface** is an `IED::` interface declared in `src/api/` (`IED::SourceControl`, `IED::BuildSystem`, `IED::TestProvider`, ...): a plugin implements it in C4 and registers the object; a consumer asks the host for the implementations. This is how the chat's tools reach git, the profiler hears about task runs, and an engine plugin adds tools to the chat without either knowing the other.

A plugin depending on another names it in `dependencies` (semver ranges, cycles refused - as now); it may then consume the other's bindings and services but still not include its sources (scripts compile separately).

As built (P3), **server bindings**: a plugin's code answers a binding its manifest lists under `bindings.provides` (side `server`, the default) with `PluginContext::Provide(name, handler)`, `handler(args) -> IED::Result`. Callers: another plugin's code (`IED::CallBinding`, for a binding its manifest consumes), a UI module (`ied.bindings.call` falls back to the server when no UI module provides the name: `plugins.call {wsId?, name, args}`, `docs/02`), a command whose contribution names a `binding` (the palette runs it through the server), and an AI tool (`toolCalls[].binding`). The provider is the plugin loaded for the caller's workspace whose manifest declares the binding; its code starts on the first call. Events: `PluginContext::Listen(kind, handler)` hears every protocol event of that kind (`docs/10`: `tree.changed`, `task.status`, ...) on its own thread, at most 1,000 queued behind it (past that they are dropped). Service interfaces are not built: a plugin offering an object other plugins call directly needs its lifetime across hot reload settled (a call into an unloaded script), so they come with the first built-in that needs one (P4); until then plugins compose through bindings.

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
| `ied.rust` | Rust | rust-analyzer, cargo preset, icons | cargo Nuke driver, `cargo_metadata` tool, the `ied-profiler` crate and a Profile task (`docs/16`) |
| `ied.go` | Go | icons, markers | gopls, go preset, go Nuke driver, the profiler client module, the Go execution-trace importer (`profilerImporters`) and a Profile task (`docs/16`) |
| `ied.python` | Python | icons, markers | a language server, venv-aware run/test presets |
| `ied.web` | JavaScript, TypeScript, HTML, CSS, SCSS | icons, markers | a TS server, npm-script presets |
| `ied.csharp` | C# | icons, markers | `dotnet` presets |
| `ied.java` | Java | icons, markers | Gradle/Maven presets |
| `ied.shaders` | HLSL, GLSL, WGSL | icons | shader language servers (engine plugins may supersede) |
| `ied.data` | JSON, YAML, TOML, XML, INI | icons, the `.iedsln/.iedproj` icons | schema validation |
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

- **Skills**: a workspace's skills are its `.ied/skills/`, then every loaded plugin's `skills` folders, then the app's `skills/`; of a name the first wins (project > plugin > app). The system message lists them (`docs/14` item 8: every skill up to 40, past that the ones the chat's first message names best and how many more there are); `skill_search` finds them by words and `skill_read` gives a skill's body or another file of its folder (`docs/13`). `nuke init` no longer copies the C4 and Nuke skills into `.ied/skills/`: they come from the `c4` and `ied.nuke` plugins. As built: `plugins/chat/code/skills.cf`, `plugins/chat/code/contributed.cf`, `plugins/chat/code/skilltools.cf`.
- **System message**: plugin sections are item 5 of `docs/14`'s order - under "From the workspace's plugins", a heading per plugin, each section within its `maxTokens` (200 by default, at most 600) and all within 1,500 tokens - and only those whose `when` holds in the workspace's context (`src/plugins/context.cf`: `workspaceTrusted`, `workspaceKind`, `config.<key>`, `workspaceContains.<name>`, `workspaceLanguage.<id>` - a project naming the language, or a `projectIcons` marker of it in the root or a project's root), so a C4 workspace does not pay for Rust advice. Language servers and debug adapters use the same context.
- **Tool calls**: each turn offers the core tools and the workspace's plugins' tool calls whose `when` holds - listed by name and loaded on demand unless the entry says `preload` (`docs/13` "Loading tools on demand"); a name already taken is a problem in the server log. A command tool (`plugins/chat/code/plugintools.cf`) has `shell(<the command as run>)` as its subject, so the `ai.permissions` rules and approvals of commands apply; it never runs in an untrusted workspace; a `read` one is offered in Plan mode. Shipped: `nuke_list` (`ied.nuke`), `cargo_metadata` (`ied.rust`). A tool of the plugin's code - a `function`, or a `binding` it provides (`plugins/chat/code/codetools.cf`) - runs on the plugin's thread under its permissions, with `plugin(<plugin id>/<tool>)` as its subject (`docs/11`), never in an untrusted workspace unless the entry says `availableUntrusted`; its card's source is the plugin. A function tool's result may show the model images (`ToolResult.images`: PNG, JPEG, WebP or GIF files in the workspace, `docs/13` "Images in tool results"); a binding's and a command's results are text. Example: `dialogue_speakers` in `shared/host-api/examples/markup-plugin`.

## 9. Order of work

| Phase | Delivers | Needs |
|---|---|---|
| P1 | The split into one plugin per language; `outputLinks`, `markdownFences`, `controlKeywords` moved out of the editor; `.ied/plugins/` discovery; plugin `skills` and `systemMessageSections` read by the AI (with `skill_search` / `skill_read`); command tool calls; built-in `ied.nuke` with preset `extends`; `nuke init --ied` giving each project its language's preset | nothing new in C4 |
| P2 | Nuke language drivers: the interface, the cargo and go drivers, a mixed C4 + Rust workspace end to end | P1 |
| P3 | The script host in `ied-server`: `--script-host`, `src/api/`, compiling a plugin's code when it activates, hot reload, server bindings (from code, UI modules, commands and AI tools), protocol events, C4-function tool calls, computed contributions and prompt sections, compile errors in the Error List. **Done (2026-10-21)**, but for the compiled-script cache (C4) and service interfaces (P4, with their first user) | C4: the compiled-script cache in `C4Script` and faster script compiles; C4LSP checking script code (`docs/17`) |
| P4 | Built-ins behind the API, smallest first: git, terminal, the editor's services, profiler, chat | P3 |

## Open items

- Script debugging (no PDB for JIT code); until then a plugin is debugged by loading its code as a built-in. Agreed with the C4 session (2026-10-21, not built): a host option (default: the host's own `-g`) links each script with lld into a DLL and PDB under a fresh name per load (`<plugin id>#<build>-<digest>.dll`, so stacks read `ied.git#3!Git::Status`) instead of the in-memory JIT, so C4DAP, VS and WinDbg see ordinary modules - breakpoints by file:line in the plugin's sources, stepping, locals, stacks across host and script frames. A breakpoint stops the server it is in, so IED debugs plugin code in a second server - a plugin host it launches under C4DAP with the plugin under development, like an extension development host - never in the one serving the IDE.
- A hang in plugin code (no watchdog inside the process): its callers time out, its thread stays busy.
- Compile time: about 0.2-0.3 s per plugin against the interface since C4 8bad750 (was 4-5 s); a start with unchanged code links the kept build.
- Permissions enforced at the API are a courtesy for in-process code; real isolation of third-party code would need a process again (`docs/99` "Later").
