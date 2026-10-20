# 08 - User stories and core journeys

## Purpose

Capture *who* IED is for and *what they need to do*, so every feature in `docs/features/` traces back to a real need and every milestone in `docs/18-roadmap.md` has acceptance scenarios. `IDEplan.md` is the source of intent; this doc restates it as testable stories and fills the gaps the plan implies but does not spell out.

## How to read this

- **IDs:** `US-<EPIC>-<n>`. Features in `docs/features/` cite these IDs; tests in `docs/19-testing-strategy.md` cite the journeys (`J<n>`).
- **Source tag:**
  - `plan` - stated in IDEplan.md (or the owner's follow-up notes).
  - `plan+` - stated in the plan; the detail here is an interpretation.
  - `ext` - extrapolated: not in the plan, but needed for the plan's goals to work. **The owner should skim `ext` items and strike anything unwanted.**
- **Priority:**
  - `P0` - needed before IED can replace the predecessor IDE as the owner's daily driver (MVP).
  - `P1` - needed for "v1 complete" (everything the plan asks for).
  - `P2` - later / nice to have.
- **AC** = acceptance criteria. Only stated where it is not obvious from the story.

## Personas

| Persona | Who | What they care about |
|---|---|---|
| **Owner** | Builds the C4 compiler, a game engine and games, on a Windows desktop. Runs local models (llama.cpp on a LAN box, e.g. the DGX Spark) and sometimes Claude. Lives in Visual Studio muscle memory. Prefers tabs. | Speed, density, VS-parity, AI that does real work unattended, privacy, nothing lost on crash. |
| **Remote owner** | The same person away from the desk: phone on the couch, laptop elsewhere. | See what the AI is doing, approve/steer it, read diffs, kick off a build - without a desktop. |
| **Agent** | The AI itself (main loop and sub-agents) as a *user* of IED's tool surface. | Precise, cheap tools: navigate by symbol, build/test/debug, find skills, never needing whole files in context. Clear errors it can act on. |
| **Plugin author** | The C4 team shipping the C4 language plugin; a game-engine team shipping engine plugins (shader LSP, GPU debugger, scene tools); third parties. | A stable, documented API; project-local plugins that just work in trusted workspaces; fast reload while developing. |
| **Engine user** | Future users of C4 and engines built on it who are not the owner. | Open a project and have build/run/debug/AI work with zero setup. |
| **Drive-by** | Anyone opening a single file or folder (`ied ./`, Explorer right-click). | Instant open, no project ceremony. |

## Core journeys

End-to-end scenarios. Each is a test script for the roadmap's exit criteria (see `docs/19`). Every one must pass at desktop width and the starred (*) ones also at phone width.

- **J1 - Morning build loop.** Owner double-clicks `MyGame.ied.sln` (or runs `ied .`). The browser opens to the solution with yesterday's tabs, splits and toolbar selections restored. They pick `Development | x64`, press F5: build output streams to *Build*, the error list fills with clickable errors, they fix one (green/yellow margins track it), press F5 again, the game launches under the debugger, hits a breakpoint, they inspect locals and step.
- **J2 - Leave it running* .** Owner gives a chat a multi-hour task ("port the logging subsystem from the C++ reference engine"), closes the browser and leaves. The agent keeps working on the server. From the phone they open IED, see the task list half done, read the modified-files list and a diff, inject "use the existing Ring buffer type", and approve a pending shell command. Back at the desk later, everything is where the phone left it.
- **J3 - Planner + implementer.** Owner selects model group "plan-claude/impl-local": Claude writes the plan into the chat's task list and scratchpad; temp-0 local workers implement each task; the main loop verifies with build + tests. When Claude hits its session limit, planning routes to the strong local model with a visible note.
- **J4 - Crash triage with AI.** A test fails. Owner right-clicks the failure -> "Fix with AI". The agent reads the failure, sets a breakpoint through the debugger tools, runs the test, inspects the stack, navigates by symbol via LSP tools, edits, rebuilds, reruns, and reports. Every tool call is visible live with timing and status.
- **J5 - Frame hitch.** Owner clicks *Profile*. The game launches instrumented; the profiler shows a frame spike; in *by task* view the pooled worker threads regroup by job name (renames tinted); a GPU pass is long; they jump from a zone to its source in the editor. The AI is asked "why is frame 812 slow?" and answers using profiler query tools.
- **J6 - Verify on Linux.** The agent is asked to make a change portable. It runs the build and tests in the Docker sandbox on its own, sees a Linux-only failure, fixes it, re-verifies on both OSes, and reports.
- **J7 - Language plugin ships with the repo.** The C4 team commits a project-local plugin pin in their solution. A teammate clones, opens the solution, marks it trusted: C4 highlighting, LSP, debugger, problem matcher, project templates and C4 skills light up with no global install.
- **J8 - Private chat.** Owner starts a chat about personal finances in the generic (no-project) chat, toggled *private*. Only local models are eligible; the chat header says so; web search goes through tor; nothing reaches a cloud endpoint even when the local model is busy (it queues instead).
- **J9 - The week-long chat.** A chat runs for days across many compressions and two IED server restarts. Context never overflows; the context panel shows what was compressed and when; old messages scroll back from disk; a crash loses at most the in-flight token stream.
- **J10 - Drive-by edit*.** Right-click a `.md` file in Explorer -> *Open with IED*: it opens in a lightweight folder workspace with markdown preview; ticking a checkbox in the preview writes back to the file.
- **J11 - Model went quiet.** A local model stops mid-answer. IED shows exactly why ("stream idle 90 s", "finish_reason=length", "ended after announcing a tool call"), automatically continues or retries per policy, and the trajectory view shows the raw exchange so the owner can tell harness from model.
- **J12 - Game-engine assist.** With the game engine's plugin and its MCP server enabled, the agent inspects the running game's scene, takes a screenshot of the game window, compares it to a reference image, tweaks a shader, hot-reloads, and confirms visually.

## Stories by epic

### SRV - Server, sessions and remote access

- **US-SRV-1** `plan` `P0` - As Owner, I can close the browser while chats, builds and terminals run, and reopen to find them still running (or finished) with full output. *AC: kill the browser mid-stream; reopen within 10 min; the stream continues live, missed output is replayed once, nothing duplicated.*
- **US-SRV-2** `plan` `P0` - As Remote owner, I can use IED from a phone browser with a layout designed for that width (not a shrunken desktop).
- **US-SRV-3** `plan+` `P0` - As Owner, any browser tab / device I open shows the same server state: open workspaces, tabs, running sessions.
- **US-SRV-4** `ext` `P0` - As Owner, remote (non-localhost) access requires a login, and localhost access is protected from malicious web pages (DNS-rebinding / cross-site WebSocket attacks), because the server can run shell commands.
- **US-SRV-5** `ext` `P0` - As Owner, I can see which devices are connected and revoke a remembered device.
- **US-SRV-6** `ext` `P1` - As Remote owner, I get a notification (in-app, and push where the browser allows) when a chat needs approval, finishes, or fails, and when a build finishes.
- **US-SRV-7** `ext` `P1` - As Owner, the server can host several workspaces at once (the C4 compiler and a game project both have agents running) and I switch between them without stopping either.
- **US-SRV-8** `ext` `P1` - As Owner, the server starts on login (optional), runs headless in the background, and has a tray icon / CLI to open, stop, and show status.
- **US-SRV-9** `ext` `P1` - As Remote owner, I can preview a web app the project serves on `localhost:<port>` of the server machine from my phone (port forwarding through IED).
- **US-SRV-10** `ext` `P2` - As Owner, I can reach IED from outside my LAN securely (documented Tailscale/WireGuard path; optional built-in TLS).

### SHL - Shell, layout and navigation

- **US-SHL-1** `plan` `P0` - As Owner, a thin icon sidebar on the left switches the main view (chat on top, then editor, git, then plugin icons), with the settings icon pinned at the bottom.
- **US-SHL-2** `plan` `P0` - As Owner, the terminal is an always-present collapsible panel across the full width at the bottom (minus the sidebar), in every view.
- **US-SHL-3** `plan+` `P0` - As Owner, the editor view behaves like Visual Studio: menu bar, toolbar, Solution Explorer, document well with tabs, docked tool windows (Output, Error List, Find Results, Call Stack...).
- **US-SHL-4** `plan` `P0` - As Owner, I can split and dock panes (chats and documents) side by side and stacked, but nothing ever floats in its own window.
- **US-SHL-5** `plan+` `P0` - As Owner, VS keyboard shortcuts work (F5, Ctrl+Shift+B, F9-F12, Ctrl+K chords, Ctrl+, / Ctrl+T go-to-all, Ctrl+Q feature search), and I can rebind any of them.
- **US-SHL-6** `ext` `P0` - As Owner, a status bar shows build/AI/LSP state, cursor position, indentation mode, encoding, line endings, branch, and notifications.
- **US-SHL-7** `ext` `P0` - As Owner, layout (pane sizes, open tool windows, splits) persists per workspace and per device class, so the phone layout does not wreck the desktop layout.
- **US-SHL-8** `plan` `P0` - As Owner, at the top of the sidebar I can switch between open workspaces (solutions, folders, federations) and create or open new ones.
- **US-SHL-9** `ext` `P1` - As Owner, a feature search box (VS Ctrl+Q) finds any command, setting, file, or tool window by name.
- **US-SHL-10** `ext` `P1` - As Owner, a start page shows recent workspaces, clone-a-repo, open folder/solution, and new project.
- **US-SHL-11** `ext` `P1` - As Owner, a notification center keeps a history of errors and long-running task results (nothing important is only a transient toast).

### WSP - Workspaces: solutions, projects, folders, files

- **US-WSP-1** `plan` `P0` - As Owner, a solution file groups project files; each project file can point its root at a folder other than its own (so generated project files live under `build/`).
- **US-WSP-2** `plan` `P0` - As Owner, settings and skills live in a committed `.ied/` folder at the solution root, so deleting generated files loses nothing.
- **US-WSP-3** `plan` `P0` - As Drive-by, `ied ./` opens the folder without creating a project; `ied file.cf:42` opens a file at a line.
- **US-WSP-4** `plan` `P1` - As Drive-by, Explorer's right-click menu has *Open with IED* for files and folders.
- **US-WSP-5** `plan+` `P1` - As Owner, I can open a **federation** - several solutions/folders from different repos (this repo with IED + C4, plus a game project) in one workspace - so one chat can work across them. *(Interpretation of "federated projects" - confirm in `docs/99`.)*
- **US-WSP-6** `plan` `P0` - As Owner, the Solution Explorer shows groups -> projects -> folders -> files with distinct file-type icons, like VS.
- **US-WSP-7** `ext` `P0` - As Owner, opening an untrusted workspace asks whether to trust it; until trusted, project-local plugins, project-defined toolbar commands and AI tools that execute code stay off.
- **US-WSP-8** `ext` `P1` - As Owner, *New Project* offers templates contributed by plugins (e.g. "C4 executable", or a game template from an engine plugin), and the generator writes `.ied.proj` / `.ied/` files idempotently.
- **US-WSP-9** `ext` `P1` - As Owner, I choose a startup project (VS concept) when the solution has several executables; F5 uses it.
- **US-WSP-10** `ext` `P1` - As Owner, file operations in the tree (new, rename, move, delete-to-recoverable-trash, drag-drop, copy path, reveal in Explorer) work like VS, and renames can trigger LSP refactors.
- **US-WSP-11** `ext` `P1` - As Owner, a single loose file opened in a solution shows under *Miscellaneous Files* (VS parity) instead of being lost.
- **US-WSP-12** `ext` `P2` - As Owner, I can open a project's properties as a form (generated from the project-file schema) instead of editing JSON.

### EDT - Code editing and navigation

- **US-EDT-1** `plan` `P0` - As Owner, I edit in Monaco with full LSP features from language plugins: completion, hover, signature help, go-to-definition/declaration/implementation, find all references, rename, formatting, code actions, semantic highlighting, folding, document symbols.
- **US-EDT-2** `plan` `P0` - As Owner, the margin shows green for lines changed and saved, yellow for unsaved changes, like VS.
- **US-EDT-3** `plan` `P0` - As Owner, indentation guides are visible by default and new files default to tabs.
- **US-EDT-4** `plan` `P0` - As Owner, I search in the current file, the project, or the whole solution, with regex and case/word toggles, and replace with preview.
- **US-EDT-5** `plan` `P1` - As Owner, the scrollbar is a minimap (VS "map mode") with markers for changes, errors, breakpoints, search hits and the caret.
- **US-EDT-6** `plan` `P1` - As Owner, I can split the editor, and use a *bound split* that automatically shows the paired header/source for whatever I open.
- **US-EDT-7** `plan` `P1` - As Owner, a code-aware spell checker flags misspellings in identifiers (split on case and underscores), comments and strings, without flagging language keywords or symbols the LSP knows about; I can add words to a project dictionary.
- **US-EDT-8** `plan` `P1` - As Owner, a *Fix indentation* command (and AI tool) normalizes a file or selection to the project's indentation (tabs by default).
- **US-EDT-9** `plan` `P1` - As Owner, markdown files have a live preview whose checkboxes (in lists and tables) are clickable and write back to the file.
- **US-EDT-10** `plan` `P1` - As Owner, a built-in browser pane can show web pages (especially the project's own dev server) next to code.
- **US-EDT-11** `plan` `P1` - As Owner, any file opens in something sensible: images, video, audio, hex for binaries, and plugin-provided viewers (e.g. profiler traces, scene files).
- **US-EDT-12** `plan+` `P0` - As Owner, right-click menus match VS: cut/copy/paste, go to definition/declaration, find all references, rename, quick actions, format, comment, breakpoint, run to cursor, git blame, AI actions.
- **US-EDT-13** `ext` `P1` - As Owner, VS navigation features exist: navigate back/forward, go to line, go to all, navigation bar (type/member dropdowns), peek definition, call hierarchy, document outline, bookmarks, task list (TODO comments), CodeLens where the LSP provides it.
- **US-EDT-14** `ext` `P0` - As Owner, unsaved changes survive closing the browser and are visible from another device, because buffers live on the server.
- **US-EDT-15** `ext` `P1` - As Owner, if two devices edit the same file, one holds the edit lease and the other sees a live read-only mirror with *Take over*; nobody silently overwrites anyone.
- **US-EDT-16** `ext` `P1` - As Owner, when a file changes on disk (git checkout, AI edit, external tool) and I have no unsaved edits, it reloads silently; if I do, I get a merge/compare prompt.
- **US-EDT-17** `ext` `P1` - As Owner, a diff editor (side-by-side and inline) is used everywhere: git, AI edits, compare-two-files.
- **US-EDT-18** `ext` `P2` - As Owner, multi-caret and column selection, clipboard ring, snippets, surround-with, duplicate/move line all use VS bindings.

### BLD - Build, run, test and output

- **US-BLD-1** `plan` `P0` - As Owner, the toolbar has per-project dropdowns (configuration, architecture, platform, any custom variable) and buttons whose command lists use the dropdown values.
- **US-BLD-2** `plan` `P0` - As Owner, a button runs several steps, each routed to an output tab, with conditions (run only if the build succeeded).
- **US-BLD-3** `plan` `P0` - As Owner, the Output window has a dropdown of sources (Build, Run, Debug, Test, plus plugin ones) and renders ANSI console colors.
- **US-BLD-4** `ext` `P0` - As Owner, build errors and warnings are parsed into an Error List (with LSP diagnostics merged in) and double-clicking an output line or list row jumps to the file and line.
- **US-BLD-5** `ext` `P0` - As Owner, a running step shows progress on its button and in the status bar; I can cancel it (kills the whole process tree).
- **US-BLD-6** `plan+` `P1` - As Owner, a Test Explorer lists tests, runs all/failed/selected, shows durations and failures with jump-to-assert, once the build system reports structured results.
- **US-BLD-7** `ext` `P1` - As Owner, I can run or debug a single test from a CodeLens above it.
- **US-BLD-8** `ext` `P2` - As Owner, a build-insights view shows per-project and per-file compile times from the build system's trace output.

### DBG - Debugging

- **US-DBG-1** `plan` `P0` - As Owner, F5 starts the startup project under the debugger (after building), via the language's DAP adapter; breakpoints, step over/into/out, continue, pause, stop work with VS keys.
- **US-DBG-2** `plan+` `P0` - As Owner, I have VS debug windows: Locals, Autos, Watch, Call Stack, Threads, Breakpoints, Immediate, Modules, plus DataTips on hover - each enabled when the adapter supports it.
- **US-DBG-3** `ext` `P1` - As Owner, conditional breakpoints, hit counts, tracepoints (log points), function and data breakpoints, run to cursor, set next statement, attach to process, memory and disassembly views - capability-gated per adapter.
- **US-DBG-4** `ext` `P1` - As Owner, a debug session survives the browser closing; I can reconnect from another device and keep stepping.
- **US-DBG-5** `ext` `P2` - As Owner, live CPU/memory graphs while debugging (VS Diagnostic Tools), fed by the profiler plugin.

### CHT - Chat experience

- **US-CHT-1** `plan` `P0` - As Owner, the chat view has browser-like tabs, one per conversation; I can dock several side by side.
- **US-CHT-2** `plan` `P0` - As Owner, thinking, text and tool calls stream live; each segment shows duration and token counts; each tool call shows success/failure and elapsed time.
- **US-CHT-3** `plan` `P0` - As Owner, the chat bar shows token totals and current tokens/sec, and lets me pick the model group and effort.
- **US-CHT-4** `plan` `P0` - As Owner, I attach files by drag-drop or picker and type `@` to get workspace path suggestions.
- **US-CHT-5** `plan` `P0` - As Owner, I can inject a message into a running agent to steer it or add work, without stopping it.
- **US-CHT-6** `plan` `P0` - As Owner, chats are saved to disk as they happen and come back after a crash or restart; old messages are evicted from memory and reload on scroll.
- **US-CHT-7** `plan` `P1` - As Owner, a collapsible right panel shows context usage by category, modified files (click for diff), tool-call stats, timing and cost.
- **US-CHT-8** `plan` `P1` - As Owner, a scratchpad for planning/notes opens inline in the chat or docked next to it; both I and the AI can edit it.
- **US-CHT-9** `plan` `P1` - As Owner, the chat has milestone/task tracking: the AI maintains a task list I can see and edit; progress is visible at a glance (including on the tab).
- **US-CHT-10** `plan` `P0` - As Owner, there is a generic chat not attached to any project.
- **US-CHT-11** `ext` `P0` - As Owner, I can stop a run, edit a previous message and resend, regenerate, or fork a new chat from any message.
- **US-CHT-12** `ext` `P1` - As Owner, every AI file change is checkpointed per turn, so I can rewind the workspace (and/or the conversation) to before any turn.
- **US-CHT-13** `ext` `P0` - As Owner, I choose a permission mode per chat (ask for everything / auto-accept edits / plan-only read-only / autonomous) and set allow/deny rules for tools and paths.
- **US-CHT-14** `ext` `P0` - As Owner, when the agent wants approval, the request is a clear card (command, diff, or question with options) that I can answer from any device.
- **US-CHT-15** `ext` `P1` - As Owner, messages I send while the agent is busy are queued (sent at the next turn) or injected (delivered at the next tool boundary) - my choice per message.
- **US-CHT-16** `ext` `P1` - As Owner, I can search across all chat history, rename/pin/archive chats, and export a chat to markdown.
- **US-CHT-17** `ext` `P1` - As Owner, a trajectory view shows the raw event stream of a chat (requests, responses, timings, finish reasons) for debugging the harness or a model.
- **US-CHT-18** `ext` `P1` - As Owner, I can paste images (screenshots) into the chat input.

### AGT - Agent runtime: modes, sub-agents, tasks, reliability

- **US-AGT-1** `plan` `P0` - As Owner, IED protects against models that stop randomly: it detects stalls and premature stops, tells me the exact reason, and continues/retries per policy. *(J11)*
- **US-AGT-2** `plan` `P1` - As Owner, the agent can delegate to sub-agents (research, worker) that run on the roles' models and temperatures, in parallel when the models allow, shown as collapsible sections I can expand into tabs.
- **US-AGT-3** `ext` `P0` - As Owner, agent modes exist: *Ask* (general), *Plan* (read-only, produces a plan for approval), *Agent* (autonomous until done, verified), plus custom modes defined in `.ied/` (instructions, tool allowlist, path restrictions, model group).
- **US-AGT-4** `ext` `P1` - As Owner, an autonomous run survives an IED server restart: it shows "interrupted - resume?" or resumes automatically per setting.
- **US-AGT-5** `ext` `P1` - As Owner, loops are caught: the same failing tool call repeated N times stops the run with an explanation instead of burning hours.
- **US-AGT-6** `ext` `P1` - As Owner, run budgets (max tokens, wall time, tool calls, cost) can be set per chat or mode; hitting one pauses with a summary.
- **US-AGT-7** `ext` `P2` - As Owner, user-defined hooks run on agent events (after an edit -> format; before a shell command -> policy check).

### MDL - Models, routing and privacy

- **US-MDL-1** `plan` `P0` - As Owner, I configure model endpoints (OpenAI-compatible: llama.cpp, vLLM, LM Studio, Ollama...) with context size, capabilities, default sampling and privacy class.
- **US-MDL-2** `plan` `P0` - As Owner, I define model groups that assign a model + temperature (+ effort) per role: primary, planning, implementation, research sub-agent, worker sub-agent, summarizer.
- **US-MDL-3** `plan` `P1` - As Owner, requests reroute when a model hits token/rate limits, with a visible note.
- **US-MDL-4** `plan` `P1` - As Owner, routing prefers idle models and respects each local server's parallel-slot count.
- **US-MDL-5** `plan` `P0` - As Owner, private requests never go to cloud models: per-chat private toggle, per-solution local-only policy, and automatic "this chat touched a sensitive file, it is now local-only" tainting.
- **US-MDL-6** `ext` `P0` - As Owner, IED discovers models on an endpoint and probes capabilities (tool calling, vision, context size) instead of me typing them.
- **US-MDL-7** `ext` `P0` - As Owner, local models whose servers emit tool calls as text (Hermes/Qwen XML, etc.) or reasoning in odd fields (`reasoning_content`, `<think>` tags) still work - IED normalizes dialects per model.
- **US-MDL-8** `ext` `P1` - As Owner, routing also matches capability (a request with images only goes to a vision model; a huge context only to a long-context model).

### CTX - Context, prompts, skills, memory

- **US-CTX-1** `plan` `P0` - As Owner, long chats never overflow: fast, high-quality, visible compression keeps them going.
- **US-CTX-2** `plan` `P0` - As Owner, the system message includes the solution's `AGENTS.md` and short sections contributed by plugins.
- **US-CTX-3** `plan` `P0` - As Owner, a strong base system prompt encodes my coding style (tabs, design discipline) and is editable.
- **US-CTX-4** `plan` `P0` - As Agent, I can search skills by keyword and load only what I need, even with thousands of skills.
- **US-CTX-5** `ext` `P1` - As Agent, `AGENTS.md` files in subfolders (and in each root of a federation) are picked up when I start working in that area.
- **US-CTX-6** `ext` `P1` - As Owner, the AI can create or update skills from a conversation (with my approval), so lessons stick.
- **US-CTX-7** `ext` `P1` - As Owner, compression is designed around local-model prompt caching (stable prefixes), so long chats stay fast on llama.cpp.
- **US-CTX-8** `ext` `P1` - As Owner, a persistent project memory (facts, decisions) the AI can read and append, stored in `.ied/` and reviewable.

### TOOL - AI tools

- **US-TOOL-1** `plan` `P0` - As Agent, I have file tools (read ranges, write, exact edit, multi-edit, glob, grep), shell, and workspace search.
- **US-TOOL-2** `plan` `P0` - As Agent, I can navigate code through LSP wrappers (definition, references, symbols, hover/type, read-a-symbol's-body, diagnostics, rename, format) without reading whole files.
- **US-TOOL-3** `plan` `P0` - As Agent, I can build, run, test and read structured errors through the same toolbar commands the human uses.
- **US-TOOL-4** `plan` `P1` - As Agent, I can drive the debugger: breakpoints, run, step, stack, locals, evaluate.
- **US-TOOL-5** `plan` `P1` - As Agent, I can load images and video frames (when the model supports vision), take screenshots of a web page / app window, and drive a headless browser (navigate, click, type, read console and network).
- **US-TOOL-6** `plan` `P1` - As Agent, I can use MCP servers (Blender, a game engine, others) as ordinary tools.
- **US-TOOL-7** `plan` `P1` - As Agent, I have game/engine-oriented tools: window capture of a running app, image compare/diff, process CPU/memory stats, profiler queries, sandbox runs.
- **US-TOOL-8** `ext` `P1` - As Agent, I can ask the user a structured question (options) and wait, instead of guessing.
- **US-TOOL-9** `ext` `P1` - As Owner, IED itself is an MCP server, so external agents (e.g. Claude Code in the terminal) can use IED's LSP/build/editor tools.

### WEB - Web access and egress

- **US-WEB-1** `plan` `P0` - As Agent, I can search the web through a SearXNG instance that ships with IED (no separate setup).
- **US-WEB-2** `plan` `P1` - As Owner, in *secure mode* all AI web traffic (search, fetch, browser, MCP egress) goes through tor or a configured VPN/proxy, per solution.
- **US-WEB-3** `ext` `P1` - As Owner, fetched pages arrive as clean markdown with the source URL, and large pages are summarized rather than dumped into context.

### INL - Inline AI in the editor

- **US-INL-1** `plan` `P1` - As Owner, Ctrl+I opens an inline chat at the cursor/selection; proposed changes appear as an inline diff I accept or reject (undoable).
- **US-INL-2** `ext` `P1` - As Owner, AI actions sit on the right-click menu and error lightbulb: explain, fix this error, write tests, document, refactor.
- **US-INL-3** `ext` `P1` - As Owner, ghost-text completions come from a fast local fill-in-the-middle model.

### GIT - Source control

- **US-GIT-1** `plan` `P0` - As Owner, a Fork/GitKraken-class git view: commit graph, branches, remotes, tags, stashes, file history, diffs.
- **US-GIT-2** `plan+` `P0` - As Owner, I stage by file, hunk or line, commit (amend, AI-drafted message), fetch/pull/push.
- **US-GIT-3** `plan+` `P1` - As Owner, I merge, rebase (including an interactive-rebase UI), cherry-pick, revert, reset, and resolve conflicts in a 3-way merge editor with optional AI help.
- **US-GIT-4** `ext` `P1` - As Owner, Git LFS (game assets), submodules and worktrees are handled.
- **US-GIT-5** `ext` `P1` - As Owner, blame is available inline in the editor and as a view.
- **US-GIT-6** `ext` `P1` - As Owner, agents can work in separate git worktrees so parallel agents do not collide.

### TRM - Terminal

- **US-TRM-1** `plan` `P0` - As Owner, the bottom terminal runs PowerShell or bash, with multiple instances.
- **US-TRM-2** `ext` `P0` - As Owner, terminals keep running and keep their scrollback when the browser closes.
- **US-TRM-3** `plan+` `P0` - As Owner, AI shell commands run in visible terminals (one per chat), so I see exactly what the AI ran and its output.
- **US-TRM-4** `ext` `P1` - As Owner, `file:line` in terminal output is clickable; shell integration marks commands and exit codes.

### SBX - Sandbox

- **US-SBX-1** `plan` `P1` - As Agent, I can build/test/run the project in a Linux container autonomously and get results back.
- **US-SBX-2** `plan+` `P1` - As Owner, a sandbox image is configured per project; the solution is mounted; egress is deny-by-default with grants.
- **US-SBX-3** `plan` `P2` - As Owner, other OS targets: macOS cannot legally run in Docker on PC hardware, so a remote SSH host (e.g. a Mac, or an ARM Linux box) is a sandbox provider too. *(See `docs/features/11-sandbox.md`.)*
- **US-SBX-4** `ext` `P2` - As Owner, I can watch and take over the sandbox's desktop (GUI apps) from the browser.

### PRF - Profiler

- **US-PRF-1** `plan` `P1` - As Owner, a Tracy-class profiler: timeline, zones, frames, plots, messages, locks, statistics, find-zone, flame graph, compare captures.
- **US-PRF-2** `plan` `P1` - As Owner, memory profiling finds leaks: live allocations, callstacks per allocation, pools, memory over time, plus GC events for GC'd languages.
- **US-PRF-3** `plan` `P1` - As Owner, threads can be renamed mid-run; the timeline tints renamed segments, and a *by task* view regroups pooled threads by the task they ran.
- **US-PRF-4** `plan` `P1` - As Owner, GPU zones (D3D12 first) appear alongside CPU time, correctly calibrated.
- **US-PRF-5** `plan` `P1` - As Owner, C4 programs are profileable out of the box through Bedrock.
- **US-PRF-6** `ext` `P1` - As Owner/Agent, profiler data is queryable by the AI (hot zones, frame breakdown, leak list).
- **US-PRF-7** `ext` `P2` - As Owner, traces from other tools import (Chrome trace JSON, which Nuke's `build-trace.json` and the game engine's planned profile output already use).

### GPU - Graphics debugger

- **US-GPU-1** `plan` `P1` - As Owner, a GPU activity in the sidebar captures a frame of my D3D12 or Vulkan program and lets me inspect every event, its state, its resources and its outputs, at PIX/RenderDoc depth.
- **US-GPU-2** `plan` `P1` - As Owner, I debug a shader at a pixel, vertex or compute thread with source-level stepping, and I see a pixel's full history with the reason each write passed or failed.
- **US-GPU-3** `plan+` `P1` - As Owner, I see per-event and per-pass GPU time, the queue timeline, and (where the vendor allows) hardware counters with a verdict on what limits a pass.
- **US-GPU-4** `plan` `P1` - As Owner, the debugger needs no Python runtime and its backend is swappable (RenderDoc now, IED's own later) without changing the UI.
- **US-GPU-5** `ext` `P1` - As Owner/Agent, the AI can query captures (state, pixels, shader traces, timings) and answer "why is this pixel black?" or "why is this pass slow?" with event links.
- **US-GPU-6** `ext` `P1` - As Owner, I compare two captures (two builds or commits) event by event: state, output and timing differences.
- **US-GPU-7** `ext` `P2` - As Remote owner, I triage a capture from the phone: event list with timings, previews, pixel history, AI answers.
- **US-GPU-8** `ext` `P1` - As Owner, I edit a shader in the IED editor, replay the capture with the edit, see the output change, and keep the edit as a real file change.
- **US-GPU-9** `ext` `P2` - As Owner, C4 programs capture through Bedrock without injection, with engine metadata (bindless names, work graphs), and can keep a rolling buffer of frames that is written out when the profiler sees a hitch.

Journey added for this epic:

- **J15 - Black pixel*.** A black speck shows on a crate in the game. Owner clicks *Capture*; the next frame lands in the GPU library. They click the pixel: the history says the lit pass wrote NaN, which the blit turned black. *Debug Pixel* steps `DefaultLit` into `PBR.hlsli` and stops at `DistributionGGX` returning 0 / 0: the crate's material binds the engine's black texture as its roughness map. They ask the AI; it explains the chain and proposes clamping roughness, Edit and Replay shows the 26 NaN pixels gone, the change is saved to the shader file and the game rebuilds. On the phone the same capture shows the pixel history and the AI's answer.

### PLG - Plugins and extensibility

- **US-PLG-1** `plan` `P0` - As Plugin author, I write plugins that add sidebar views, tool windows, toolbar items, output tabs, LSP/DAP configs, skills, AI tools, bindings, settings and system-message sections.
- **US-PLG-2** `plan` `P0` - As Plugin author, a project-local plugin referenced from the solution loads automatically in trusted workspaces.
- **US-PLG-3** `plan` `P0` - As Plugin author, my plugin can call named bindings other plugins publish, and publish its own.
- **US-PLG-4** `ext` `P1` - As Plugin author, I get hot reload, a plugin log output tab, clear validation errors, and a scaffold template.
- **US-PLG-5** `ext` `P1` - As Plugin author, I can contribute language definitions (extensions, grammar, comment tokens, brackets), semantic token colors mapped to theme tokens, problem matchers, project templates, file viewers/custom editors, menus, commands, keybindings, status bar items, snippets, MCP servers, model providers, sandbox providers.
- **US-PLG-6** `ext` `P1` - As Owner, the AI can scaffold and write a plugin for me, and nothing it writes runs until I approve it.
- **US-PLG-7** `ext` `P1` - As Owner, I see each plugin's permissions before enabling it.

### SET - Settings

- **US-SET-1** `plan` `P0` - As Owner, a settings window (icon at the bottom of the sidebar) configures everything, with app / solution / project tiers and a clear "set at which tier" indicator.
- **US-SET-2** `plan` `P0` - As Owner, model and model-group configuration has a proper UI (roles x model x temperature x effort x fallback list), with *test connection*.
- **US-SET-3** `ext` `P0` - As Owner, secrets (API keys, VPN credentials, passwords) are stored in the OS credential store, never in committed or plain JSON files.
- **US-SET-4** `ext` `P1` - As Owner, a keybinding editor with VS defaults, conflict detection and chords.
- **US-SET-5** `ext` `P1` - As Owner, settings are searchable, and every setting can also be edited as JSON.

### CLD - Cloud models (Claude)

- **US-CLD-1** `plan` `P1` - As Owner, I can use Claude through IED (Agent SDK path) with IED's tools, streaming and UI.
- **US-CLD-2** `plan` `P1` - As Owner, I see my Claude usage: what is left in the current session window, weekly limits, and reset times, like Claude Code shows.
- **US-CLD-3** `ext` `P1` - As Owner, I can also use the Anthropic API directly with an API key (per-token billing, cost shown).

### OPS - Reliability, diagnostics and data safety

- **US-OPS-1** `ext` `P0` - As Owner, a crash of the server, a plugin, an LSP or an MCP server is reported with a reason and the component restarts; it never takes the whole IDE down silently.
- **US-OPS-2** `ext` `P0` - As Owner, IED's own logs (server, plugins, LSP traces, model requests) are viewable in the Output window.
- **US-OPS-3** `ext` `P0` - As Owner, deletes go to a recoverable trash (files deleted by me or the AI).
- **US-OPS-4** `ext` `P1` - As Owner, the server reports its own health (memory, open sessions, queue depths) on a diagnostics page.

### DST - Install and distribution

- **US-DST-1** `plan` `P1` - As Owner, IED installs from one package, with SearXNG and tor bundled (or auto-installed in isolation) - no manual setup.
- **US-DST-2** `ext` `P1` - As Owner, the installer puts `ied` on PATH, registers Explorer context menus and `.ied.sln` file association.
- **US-DST-3** `ext` `P1` - As Owner, first run walks me through: (optional) remote password, detecting local model servers on common ports, picking a first model group.
- **US-DST-4** `ext` `P2` - As Owner, an optional desktop window (WebView2) gives an app-like experience where browser-reserved shortcuts (Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab) work.

### XRP - Family integration (generic features the C4/Nuke/game-engine plugins use)

- **US-XRP-1** `plan` `P0` - As Owner, C4 gets first-class support through a C4 plugin (maintained in this repo alongside `C4/` and `Nuke/`): language definition, C4LSP, C4DAP, problem matcher for compiler output, Nuke toolbar presets, project templates, C4 skills.
- **US-XRP-2** `plan` `P0` - As Owner, Nuke keeps LSP options in sync with real builds (`.ied/local/lsp-options/`), and `nuke init --ied` writes IED solution/project files.
- **US-XRP-3** `plan` `P1` - As Owner, C4Assist is rewritten for IED (its features become C4 plugin contributions).
- **US-XRP-4** `plan` `P1` - As Owner, Bedrock exposes profiler hooks so C4 apps are profileable by default.
- **US-XRP-5** `ext` `P1` - As Owner, a game engine can ship plugins for its shader language, scene/asset files, session control (named-pipe CLI) and GPU captures, all through generic contribution points.

### ORC - Orchestration and mission control (where IED goes furthest beyond its predecessor)

- **US-ORC-1** `ext` `P0` - As Remote owner, one Mission Control screen shows every run on the server across workspaces, and one attention queue holds everything waiting on me, so I can triage from my phone in a minute.
- **US-ORC-2** `ext` `P1` - As Owner, I keep a queue/board of goals per workspace; agents pick up the next ready goal when model capacity frees up, so my local GPUs work while I do not.
- **US-ORC-3** `ext` `P1` - As Owner, parallel agents work in separate git worktrees and land their results in a Review column where I merge, request changes, or discard.
- **US-ORC-4** `ext` `P1` - As Owner, an agent's "done" is checked by an independent verifier (a different model at temperature 0) against executable acceptance criteria before it reaches me.
- **US-ORC-5** `ext` `P1` - As Owner, the scheduler knows each model server's slots and gives my interactive chat priority over background goals.
- **US-ORC-6** `ext` `P1` - As Owner, I schedule recurring goals ("nightly: run all tests, fix what fails") and cap daily cloud spend.
- **US-ORC-7** `ext` `P1` - As Owner, in away mode a goal that needs approval waits without blocking the others, and I get a digest when I return.
- **US-ORC-8** `ext` `P1` - As Owner, a goal can span repos (fix C4 -> refresh the game's vendored compiler -> rebuild the game), and an agent that finds a bug in another repo files a goal there instead of working around it.
- **US-ORC-9** `ext` `P1` - As Owner, IED keeps scorecards of how each model actually performs per role (stalls, tool-call validity, verification pass rate, speed) and can run an eval suite across model groups, so I choose models from evidence.

Journeys added for this epic:

- **J13 - Overnight queue.** Owner queues five goals in the game project and two in C4, sets away mode and goes to bed. Workers run in worktrees on the local box's slots; the verifier rejects one result and the agent fixes it; one goal waits for a `git push` approval without blocking the rest. In the morning the phone shows a digest: four in Review with green evidence, one needs an answer, one failed twice with a post-mortem.
- **J14 - Cross-repo bug.** A game-project agent hits a C4 compiler bug, writes a minimal repro, and files a goal in the C4 workspace via handoff. The C4 agent fixes the compiler and runs the C4 gates. A federation goal then refreshes the game's vendored `C4.exe` and resumes the original game goal - all visible in Mission Control.

## Coverage check

Every `plan` item in IDEplan.md maps to at least one story above. Items the plan names that are deliberately *not* stories (handled as constraints instead): "dark theme / grayscale" (`docs/07`), "commit at checkpoints / screenshot validation" (`AGENTS.md`, `docs/19`), "no language-specific code in the host" (`AGENTS.md` rule 1).
